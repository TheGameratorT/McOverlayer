#!/usr/bin/env python3
"""overlay_engine.py — Shared overlay mapping and image processing core.

Contains all data structures, mapping classes, and image processing functions
shared by mc_overlayer.py (CLI apply pipeline) and mapping_core.py (preview tooling).
Neither mc_overlayer nor mapping_core should import each other; both import from here.
"""
from __future__ import annotations

import bisect
import json
import os
import threading
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from typing import Any, List

from PIL import Image
import numpy as np


SUPPORTED_EXT = {".png", ".jpg", ".jpeg"}

# Pre-loaded overlay PIL Images (populated before workers start, safe for concurrent reads)
_overlay_images: dict[str, "Image.Image"] = {}
# Resized overlay cache: (path, w, h, keep_aspect, overlay_scale) -> numpy array
# CPython dict writes are atomic under the GIL, so no lock needed.
_resized_cache: dict[tuple, np.ndarray] = {}


# ---------------------------------------------------------------------------
# Path utility
# ---------------------------------------------------------------------------

def _rel(path: str) -> str:
    """Return path relative to cwd, falling back to the original."""
    try:
        return os.path.relpath(path)
    except ValueError:
        return path


# ---------------------------------------------------------------------------
# Hashing utilities
# ---------------------------------------------------------------------------

def _ring_hash(s: str) -> int:
    """Unsigned 32-bit hash used to place overlays and targets on the consistent hashing ring.

    Uses Java's String.hashCode() polynomial followed by a MurmurHash3-style finalizer
    so that similar strings (e.g. panorama_0, panorama_1) land at well-separated ring
    positions rather than clustering together. Without the finalizer, strings differing
    only in a trailing digit are spaced just 31^4 = 923,521 apart on a 2^32 ring —
    a gap too small for any overlay to land in with a typical dataset size.
    """
    h = 0
    for c in s:
        h = (31 * h + ord(c)) & 0xFFFFFFFF
    # Finalizer: mix bits so small input differences scatter across the full ring
    h ^= h >> 16
    h = (h * 0x45d9f3b) & 0xFFFFFFFF
    h ^= h >> 16
    return h


def _ring_select(ring: list, pos: int) -> str:
    """Return the overlay path at the nearest clockwise position >= pos on the ring.

    ring: sorted list of (position, overlay_path) pairs.
    Wraps around to ring[0] if pos is past the last entry.
    """
    idx = bisect.bisect_left(ring, (pos,))
    if idx >= len(ring):
        idx = 0
    return ring[idx][1]


def string_to_seed(seed_str: str) -> int:
    """Convert a string seed to an integer using Minecraft's hash algorithm.

    Minecraft uses Java's String.hashCode() which computes:
    h = 0
    for each character c:
        h = 31 * h + ord(c)

    Since Java uses 32-bit signed integers, we simulate that overflow behavior
    to match Minecraft's implementation exactly.
    """
    h = 0
    for c in seed_str:
        h = 31 * h + ord(c)

    # Convert to unsigned 32-bit, then to signed to match Java behavior
    h = h & 0xFFFFFFFF
    if h >= 0x80000000:
        h -= 0x100000000

    return h


# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------

@dataclass
class EntityRegion:
    """A rectangular face region within an entity texture atlas.

    flip: optional string indicating texture flips:
      - "v": vertical flip (upside down)
      - "h": horizontal flip (mirror left-right)
      - "hv" or "vh": both flips
    rotate: optional string indicating texture rotation:
      - "cw": 90° clockwise rotation
      - "ccw": 90° counter-clockwise rotation
    """
    name: str
    x: int
    y: int
    width: int
    height: int
    flip: str | None = None
    rotate: str | None = None


@dataclass
class EntityData:
    """Metadata for an entity texture atlas."""
    entity_id: str
    texture_size: tuple[int, int]  # canonical (width, height)
    regions: List[EntityRegion]    # face regions
    textures: List[str]            # all texture paths for this entity


# ---------------------------------------------------------------------------
# File discovery
# ---------------------------------------------------------------------------

def get_images_in_dir(root: str) -> List[str]:
    out = []
    for dirpath, _, filenames in os.walk(root):
        for fn in filenames:
            if os.path.splitext(fn)[1].lower() in SUPPORTED_EXT:
                out.append(os.path.join(dirpath, fn))
    return out


# ---------------------------------------------------------------------------
# Entity regions loading
# ---------------------------------------------------------------------------

def load_entity_regions(dir_path: str) -> tuple[dict[str, EntityData], dict[str, str]]:
    """Load entity region definitions from JSON files in a directory.

    Returns:
        (entities, texture_to_entity) where:
        - entities: dict[entity_id -> EntityData]
        - texture_to_entity: dict[texture_path -> entity_id]
    """
    entities: dict[str, EntityData] = {}
    texture_to_entity: dict[str, str] = {}

    if not os.path.isdir(dir_path):
        raise ValueError(f"Entity regions directory not found: {dir_path}")

    for fn in os.listdir(dir_path):
        if not fn.endswith(".json"):
            continue

        path = os.path.join(dir_path, fn)
        try:
            with open(path, "r") as f:
                data = json.load(f)
        except json.JSONDecodeError as e:
            raise ValueError(f"Invalid JSON in {path}: {e}")

        # Each file contains one entity under a single key
        for entity_id, entity_info in data.items():
            texture_size = tuple(entity_info["texture_size"])
            regions = [
                EntityRegion(
                    name=r["name"],
                    x=r["x"],
                    y=r["y"],
                    width=r["width"],
                    height=r["height"],
                    flip=r.get("flip"),
                    rotate=r.get("rotate"),
                )
                for r in entity_info.get("regions", [])
            ]
            textures = entity_info.get("textures", [])

            entity_data = EntityData(
                entity_id=entity_id,
                texture_size=texture_size,
                regions=regions,
                textures=textures,
            )
            entities[entity_id] = entity_data

            for tex_path in textures:
                texture_to_entity[tex_path] = entity_id

    # bed_head and bed_foot share the same textures but cover different halves
    # of the bed model.  Merge them into a single "bed" entity so every bed
    # texture gets regions from both files.
    if "bed_head" in entities and "bed_foot" in entities:
        head = entities["bed_head"]
        foot = entities["bed_foot"]
        merged_regions = (
            [EntityRegion(f"head_{r.name}", r.x, r.y, r.width, r.height, r.flip, r.rotate) for r in head.regions] +
            [EntityRegion(f"foot_{r.name}", r.x, r.y, r.width, r.height, r.flip, r.rotate) for r in foot.regions]
        )
        # Textures are identical in both files; deduplicate while preserving order
        seen: set[str] = set()
        merged_textures: list[str] = []
        for t in head.textures + foot.textures:
            if t not in seen:
                seen.add(t)
                merged_textures.append(t)
        entities["bed"] = EntityData(
            entity_id="bed",
            texture_size=head.texture_size,
            regions=merged_regions,
            textures=merged_textures,
        )
        del entities["bed_head"]
        del entities["bed_foot"]
        for tex_path in merged_textures:
            texture_to_entity[tex_path] = "bed"

    return entities, texture_to_entity


# ---------------------------------------------------------------------------
# Animation helpers
# ---------------------------------------------------------------------------

def parse_animation_mcmeta(image_path: str) -> dict | None:
    """Parse .mcmeta file adjacent to image_path; return animation dict or None."""
    mcmeta_path = image_path + ".mcmeta"
    try:
        with open(mcmeta_path, "r") as f:
            data = json.load(f)
            return data.get("animation")
    except (FileNotFoundError, json.JSONDecodeError):
        return None


def get_animation_frame_count(image_path: str, anim: dict | None) -> int:
    """Get number of physical frames for an animated texture. Returns 1 if not animated.

    The `frames` list in mcmeta is the playback sequence (animation order), not the
    physical frame layout. Physical frames are always inferred from image dimensions:
    each frame is width×width pixels stacked vertically.
    """
    if anim is None:
        return 1

    # Always infer physical frame count from image dimensions.
    # The mcmeta `frames` list is playback order and may repeat indices - it does NOT
    # represent the number of unique frames stored in the image.
    try:
        img = Image.open(image_path)
        frame_height = img.width  # MC textures: each frame is width×width
        return max(1, img.height // frame_height)
    except Exception:
        return 1


# ---------------------------------------------------------------------------
# Overlay mappers
# ---------------------------------------------------------------------------

class EntityOverlayMapper:
    """Deterministic overlay mapper for entity textures.

    Assigns overlays to face regions based on two independent modes:
    - face_mode: "same" (all faces → 1 overlay) or "different" (each face → unique overlay)
    - texture_mode: "shared" (all textures of entity share overlay pool) or "separate" (each texture gets own pool)
    """
    def __init__(
        self,
        entities: dict[str, EntityData],
        overlay_paths: List[str],
        face_mode: str = "same",
        texture_mode: str = "shared",
        seed: int | None = None,
        overlay_dir: str | None = None,
    ):
        if not overlay_paths:
            raise ValueError("No overlay images provided")
        if face_mode not in ("same", "different"):
            raise ValueError(f"Invalid face_mode: {face_mode}")
        if texture_mode not in ("shared", "separate"):
            raise ValueError(f"Invalid texture_mode: {texture_mode}")

        self._entities = entities
        self._face_mode = face_mode
        self._texture_mode = texture_mode
        self._seed = seed
        self._overlay_dir = overlay_dir
        # Build consistent hashing ring shared across all entity assignments.
        # Use paths relative to overlay_dir so the ring is stable regardless of
        # where the overlay directory is located on disk.
        self._ring = sorted(
            (_ring_hash(f"{seed}:overlay:{self._rel_overlay(p)}"), p)
            for p in overlay_paths
        )

        # texture_path -> list[overlay_path] (index aligns with entity_data.regions)
        self._face_overlays: dict[str, List[str]] = {}
        self._build_mappings()

    def _rel_overlay(self, path: str) -> str:
        if self._overlay_dir:
            try:
                return os.path.relpath(path, self._overlay_dir)
            except ValueError:
                pass
        return path

    def _build_mappings(self):
        """Build overlay assignments for all entity textures.

        Uses a consistent hashing ring so that adding or removing overlays only
        reassigns the affected entries. Keys are composed from entity id, texture
        path, and/or region name — never positional indices — so adding or removing
        entities, textures, or regions only affects those specific items.
        """
        seed = self._seed
        ring = self._ring
        for entity_id, entity_data in self._entities.items():
            num_faces = len(entity_data.regions)

            if self._face_mode == "same" and self._texture_mode == "shared":
                # 1 overlay for entire entity; keyed by entity id only
                overlay = _ring_select(ring, _ring_hash(f"{seed}:{entity_id}"))
                for tex_path in entity_data.textures:
                    self._face_overlays[tex_path] = [overlay] * num_faces

            elif self._face_mode == "same" and self._texture_mode == "separate":
                # 1 overlay per texture; keyed by entity id + texture path
                for tex_path in entity_data.textures:
                    overlay = _ring_select(ring, _ring_hash(f"{seed}:{entity_id}:{tex_path}"))
                    self._face_overlays[tex_path] = [overlay] * num_faces

            elif self._face_mode == "different" and self._texture_mode == "shared":
                # 1 overlay per face; keyed by entity id + region name (all textures share)
                face_overlays = [
                    _ring_select(ring, _ring_hash(f"{seed}:{entity_id}:{r.name}"))
                    for r in entity_data.regions
                ]
                for tex_path in entity_data.textures:
                    self._face_overlays[tex_path] = list(face_overlays)

            elif self._face_mode == "different" and self._texture_mode == "separate":
                # 1 overlay per (texture, face) pair; keyed by entity id + texture path + region name
                for tex_path in entity_data.textures:
                    self._face_overlays[tex_path] = [
                        _ring_select(ring, _ring_hash(f"{seed}:{entity_id}:{tex_path}:{r.name}"))
                        for r in entity_data.regions
                    ]

    def get_face_overlays(self, texture_path: str) -> List[tuple[EntityRegion, str]]:
        """Get (region, overlay_path) tuples for each face of a texture.

        Returns list in region order.
        """
        if texture_path not in self._face_overlays:
            raise KeyError(f"No face overlays found for {texture_path}")

        entity_data = None
        for eid, edata in self._entities.items():
            if texture_path in edata.textures:
                entity_data = edata
                break

        if entity_data is None:
            raise KeyError(f"Texture not found in any entity: {texture_path}")

        overlays = self._face_overlays[texture_path]
        return list(zip(entity_data.regions, overlays))


class OverlayMapper:
    """Deterministic overlay mapper based on seed.

    Creates a mapping from target image paths to overlay paths that is
    consistent for the same seed and image sets. Optionally supports per-frame
    overlay assignment for animated textures.
    """
    def __init__(self, target_paths: List[str], overlay_paths: List[str], seed: int | None = None, per_frame: bool = False, target_dir: str | None = None, overlay_dir: str | None = None):
        if not overlay_paths:
            raise ValueError("No overlay images provided")
        self._seed = seed
        self._per_frame = per_frame
        self._target_dir = target_dir
        self._overlay_dir = overlay_dir
        self._mapping: dict[str, str] = {}
        self._frame_mapping: dict[tuple[str, int], str] = {}  # (path, frame_idx) -> overlay
        # Build consistent hashing ring using paths relative to overlay_dir so the
        # ring position of each overlay is stable regardless of where the directory lives.
        self._ring = sorted(
            (_ring_hash(f"{seed}:overlay:{self._rel_overlay(p)}"), p)
            for p in overlay_paths
        )
        self._build_mapping(target_paths)

    def _rel_overlay(self, path: str) -> str:
        if self._overlay_dir:
            try:
                return os.path.relpath(path, self._overlay_dir)
            except ValueError:
                pass
        return path

    def _rel_target(self, path: str) -> str:
        if self._target_dir:
            try:
                return os.path.relpath(path, self._target_dir)
            except ValueError:
                pass
        return path

    def _build_mapping(self, target_paths: List[str]):
        """Build deterministic mapping from target to overlay paths.

        Uses a consistent hashing ring so that adding or removing overlays only
        reassigns targets that were mapped to the affected overlay. Adding or
        removing other targets never affects any existing assignment.
        Hash keys use paths relative to their base directories so the mapping
        is stable regardless of where the directories are located on disk.
        """
        seed = self._seed
        ring = self._ring
        for target_path in target_paths:
            rel = self._rel_target(target_path)
            self._mapping[target_path] = _ring_select(ring, _ring_hash(f"{seed}:{rel}"))

            # Handle per-frame mapping for animated textures
            if self._per_frame:
                anim = parse_animation_mcmeta(target_path)
                frame_count = get_animation_frame_count(target_path, anim)

                if frame_count > 1:
                    # Frame 0 uses the base mapping overlay
                    self._frame_mapping[(target_path, 0)] = self._mapping[target_path]

                    # Frames 1+ use a per-path+frame key, independent of other targets
                    for f in range(1, frame_count):
                        self._frame_mapping[(target_path, f)] = _ring_select(
                            ring, _ring_hash(f"{seed}:{rel}:{f}")
                        )

    def get_overlay(self, target_path: str) -> str:
        """Get the overlay path for a given target image."""
        if target_path not in self._mapping:
            raise KeyError(f"No mapping found for {target_path}")
        return self._mapping[target_path]

    def get_overlay_for_frame(self, target_path: str, frame_idx: int) -> str:
        """Get the overlay path for a specific frame of a target image.

        In per-frame mode, returns the per-frame overlay if available.
        Otherwise, returns the base overlay for all frames.
        """
        if self._per_frame and (target_path, frame_idx) in self._frame_mapping:
            return self._frame_mapping[(target_path, frame_idx)]
        return self._mapping[target_path]


# ---------------------------------------------------------------------------
# Image processing primitives
# ---------------------------------------------------------------------------

def _composite_overlay(
    bg_arr: np.ndarray,
    ov_arr: np.ndarray,
    overlay_alpha: float = 0.75,
) -> np.ndarray:
    """Composite overlay onto background, preserving background shape.

    The background (Minecraft texture) alpha takes priority - overlay (anime pose)
    colors are blended in but don't affect transparency. This creates a natural
    texture-mixing effect where the Minecraft texture shape is preserved.
    Transparent areas of the overlay won't darken the background.
    """
    bg_rgb = bg_arr[..., :3].astype(np.float32)
    ov_rgb = ov_arr[..., :3].astype(np.float32)
    ov_a = ov_arr[..., 3].astype(np.float32) / 255.0

    # Blend using both the global overlay_alpha and the overlay's own alpha
    # Transparent areas of the overlay (low ov_a) won't darken the background
    blend_factor = overlay_alpha * ov_a[..., np.newaxis]
    blended_rgb = bg_rgb * (1.0 - blend_factor) + ov_rgb * blend_factor

    out_arr = np.empty((bg_arr.shape[0], bg_arr.shape[1], 4), dtype=np.uint8)
    out_arr[..., :3] = blended_rgb.astype(np.uint8)
    out_arr[..., 3] = bg_arr[..., 3]
    return out_arr


def _resize_overlay(ov: Image.Image, target_w: int, target_h: int, keep_aspect: bool, overlay_scale: float = 1.0) -> np.ndarray:
    """Resize overlay to (target_w, target_h). If keep_aspect or overlay_scale < 1.0,
    fit within bounds while preserving aspect ratio and center on transparent canvas;
    otherwise stretch to fill.

    If overlay_scale < 1.0, scales the fit bounds and centers the overlay.
    """
    # Apply overlay_scale to the target bounds
    scaled_target_w = int(target_w * overlay_scale)
    scaled_target_h = int(target_h * overlay_scale)

    # If keep_aspect or overlay_scale < 1.0, fit overlay within scaled bounds while preserving aspect
    if keep_aspect or overlay_scale < 1.0:
        ov_w, ov_h = ov.size
        scale = min(scaled_target_w / ov_w, scaled_target_h / ov_h)
        fit_w, fit_h = int(ov_w * scale), int(ov_h * scale)
        resized = ov.resize((fit_w, fit_h), resample=Image.BICUBIC)
        canvas = Image.new("RGBA", (target_w, target_h), (0, 0, 0, 0))
        canvas.paste(resized, ((target_w - fit_w) // 2, (target_h - fit_h) // 2))
        return np.array(canvas)

    # Otherwise stretch to fill the full target
    return np.array(ov.resize((target_w, target_h), resample=Image.BICUBIC))


def _apply_flip(arr: np.ndarray, flip: str | None) -> np.ndarray:
    """Apply flips to an overlay array.

    flip values:
      - "v": vertical flip (upside down)
      - "h": horizontal flip (mirror left-right)
      - "hv" or "vh": both flips
    Returns a flipped copy of the array.
    """
    if flip is None:
        return arr

    if "v" in flip:
        arr = np.flipud(arr)
    if "h" in flip:
        arr = np.fliplr(arr)

    return arr


def _apply_rotation(
    arr: np.ndarray,
    rotate: str | None,
    target_shape: tuple[int, int] | None = None,
    keep_aspect: bool = False,
    overlay_scale: float = 1.0,
) -> np.ndarray:
    """Apply rotation to an overlay array.

    rotate values:
      - "cw": 90° clockwise rotation
      - "ccw": 90° counter-clockwise rotation

    If rotation changes dimensions and target_shape is provided, resize back to target_shape
    respecting keep_aspect and overlay_scale (same logic as _resize_overlay).
    """
    if rotate is None:
        return arr

    if rotate == "ccw":
        arr = np.rot90(arr, k=1)
    elif rotate == "cw":
        arr = np.rot90(arr, k=3)  # 3 * 90° = 270° = -90° (clockwise)

    # If rotation changed dimensions, resize back to target shape respecting keep_aspect
    if target_shape is not None and arr.shape[:2] != target_shape:
        target_h, target_w = target_shape
        arr_img = Image.fromarray(arr)
        arr = _resize_overlay(arr_img, target_w, target_h, keep_aspect, overlay_scale)

    return arr


# ---------------------------------------------------------------------------
# Overlay preloading and caching
# ---------------------------------------------------------------------------

def _load_single_overlay(path: str) -> tuple[str, "Image.Image"]:
    """Load and decode a single overlay image. Returns (path, PIL Image)."""
    img = Image.open(path).convert("RGBA")
    img.load()  # Force full decode; closes internal file handle
    return path, img


def preload_overlays(paths: List[str], progress_cb=None) -> None:
    """Pre-load all overlay images into memory before workers start using multithreading.

    After this, _overlay_images[path] is a fully decoded RGBA PIL Image safe
    for concurrent reads (resize creates new objects and does not mutate the source).

    progress_cb: optional callable(done: int, total: int) for progress reporting.
    """
    if not paths:
        return

    num_workers = min(8, len(paths))
    done = [0]

    def update_and_load(path: str) -> tuple[str, "Image.Image"]:
        result = _load_single_overlay(path)
        done[0] += 1
        if progress_cb:
            progress_cb(done[0], len(paths))
        return result

    with ThreadPoolExecutor(max_workers=num_workers) as exc:
        for path, img in exc.map(update_and_load, paths):
            _overlay_images[path] = img


def _get_overlay_arr(ov_path: str, w: int, h: int, keep_aspect: bool, overlay_scale: float = 1.0) -> np.ndarray:
    """Return a resized overlay as a uint8 RGBA array, using the global cache.

    The returned array must not be modified by the caller (it is shared).
    """
    key = (ov_path, w, h, keep_aspect, overlay_scale)
    arr = _resized_cache.get(key)
    if arr is not None:
        return arr
    ov = _overlay_images.get(ov_path)
    if ov is None:
        ov = Image.open(ov_path).convert("RGBA")
    arr = _resize_overlay(ov, w, h, keep_aspect, overlay_scale)

    # Limit cache size: if it exceeds 100 entries, clear it (prevents unbounded growth with large scales)
    if len(_resized_cache) >= 100:
        _resized_cache.clear()
    _resized_cache[key] = arr  # CPython dict assignment is atomic under the GIL
    return arr


# ---------------------------------------------------------------------------
# Per-image processing
# ---------------------------------------------------------------------------

def process_entity_image(
    target_path: str,
    face_overlays: List[tuple[EntityRegion, str]],
    texture_size: tuple[int, int],
    scale: int = 4,
    overlay_alpha: float = 0.75,
    shutdown_event: threading.Event | None = None,
    keep_aspect: bool = False,
    overlay_scale: float = 1.0,
) -> tuple[str, str]:
    """Apply face-specific overlays to an entity texture. Returns (target_path, last_overlay_path)."""
    if shutdown_event is not None and shutdown_event.is_set():
        return target_path, ""

    try:
        bg = Image.open(target_path).convert("RGBA")
    except Exception as e:
        raise RuntimeError(f"failed to open image: {e}")
    img_w, img_h = bg.size

    # Check for animation: animated block textures (e.g. lanterns) stack frames vertically
    # and have a .mcmeta file. texture_size represents one frame's canonical dimensions.
    # Minecraft frames are always width×width pixels; frame_count derived from image dimensions.
    if parse_animation_mcmeta(target_path) is not None:
        frame_h_px = img_w  # one frame is width×width
        frame_count = max(1, img_h // frame_h_px)
    else:
        frame_h_px = img_h
        frame_count = 1

    # Compute scale factor from canonical texture_size to actual image (frame) size
    sx = img_w / texture_size[0]
    sy = frame_h_px / texture_size[1]  # scale relative to one frame's height

    # Upscale image by scale factor
    new_w = img_w * scale
    new_h = img_h * scale
    # Build output directly from resize result — no redundant full-array copy
    out_arr = np.array(bg.resize((new_w, new_h), resample=Image.NEAREST))

    unique_overlays = {ov for _, ov in face_overlays}
    display_ov_path = "[multiple]" if len(unique_overlays) > 1 else next(iter(unique_overlays), "")

    scaled_frame_h = frame_h_px * scale
    for f in range(frame_count):
        y_frame_offset = f * scaled_frame_h
        for region, ov_path in face_overlays:
            if shutdown_event is not None and shutdown_event.is_set():
                return target_path, display_ov_path

            # Compute scaled region coordinates in the output image
            x_s = int(round(region.x * sx * scale))
            y_s = int(round(region.y * sy * scale)) + y_frame_offset
            w_s = int(round(region.width * sx * scale))
            h_s = int(round(region.height * sy * scale))

            # Skip zero-area regions
            if w_s == 0 or h_s == 0:
                continue

            # Clamp region to image bounds to handle out-of-bounds region definitions
            x_s = max(0, min(x_s, new_w))
            y_s = max(0, min(y_s, new_h))
            x_e = max(0, min(x_s + w_s, new_w))
            y_e = max(0, min(y_s + h_s, new_h))

            # Skip if region is entirely outside bounds
            if x_e <= x_s or y_e <= y_s:
                continue

            w_s = x_e - x_s
            h_s = y_e - y_s

            try:
                ov_arr = _get_overlay_arr(ov_path, w_s, h_s, keep_aspect, overlay_scale)
            except Exception as e:
                raise RuntimeError(f"failed to load overlay {_rel(ov_path)}: {e}")

            # Apply flips if specified
            ov_arr = _apply_flip(ov_arr, region.flip)

            # Apply rotations if specified, resizing back to target shape if needed
            ov_arr = _apply_rotation(ov_arr, region.rotate, (h_s, w_s), keep_aspect, overlay_scale)

            # Composite overlay onto region in-place (view passed directly; _composite_overlay only reads bg_arr)
            out_arr[y_s:y_e, x_s:x_e] = _composite_overlay(out_arr[y_s:y_e, x_s:x_e], ov_arr, overlay_alpha)

    if shutdown_event is not None and shutdown_event.is_set():
        return target_path, display_ov_path

    # Atomic save
    tmp = target_path + "_"
    Image.fromarray(out_arr, mode="RGBA").save(tmp, format="PNG", compress_level=1)
    os.replace(tmp, target_path)
    return target_path, display_ov_path


def process_image(
    target_path: str,
    mapper: OverlayMapper,
    scale: int = 4,
    overlay_alpha: float = 0.75,
    shutdown_event: threading.Event | None = None,
    keep_aspect: bool = False,
    overlay_scale: float = 1.0,
) -> tuple[str, str]:
    """Overlay one image. Returns (target_path, overlay_path)."""
    if shutdown_event is not None and shutdown_event.is_set():
        return target_path, ""

    try:
        bg = Image.open(target_path).convert("RGBA")
    except Exception as e:
        raise RuntimeError(f"failed to open image: {e}")
    new_w = bg.width * scale

    # Check if animated
    anim = parse_animation_mcmeta(target_path)
    frame_count = get_animation_frame_count(target_path, anim)

    if frame_count == 1:
        # Non-animated: single image
        ov_path = mapper.get_overlay(target_path)
        new_h = bg.height * scale
        bg_arr = np.array(bg.resize((new_w, new_h), resample=Image.NEAREST))
        try:
            ov_arr = _get_overlay_arr(ov_path, new_w, new_h, keep_aspect, overlay_scale)
        except Exception as e:
            raise RuntimeError(f"failed to load overlay {_rel(ov_path)}: {e}")

        if shutdown_event is not None and shutdown_event.is_set():
            return target_path, ov_path

        out_arr = _composite_overlay(bg_arr, ov_arr, overlay_alpha)

        if shutdown_event is not None and shutdown_event.is_set():
            return target_path, ov_path

        tmp = target_path + "_"
        Image.fromarray(out_arr, mode="RGBA").save(tmp, format="PNG", compress_level=1)
        os.replace(tmp, target_path)
        return target_path, ov_path

    else:
        # Animated: process each frame
        frame_h = bg.height // frame_count
        new_h = bg.height * scale
        bg_arr = np.array(bg.resize((new_w, new_h), resample=Image.NEAREST))
        out_arr = np.empty_like(bg_arr)

        for f in range(frame_count):
            if shutdown_event is not None and shutdown_event.is_set():
                return target_path, ""

            ov_path = mapper.get_overlay_for_frame(target_path, f)
            scaled_fh = frame_h * scale

            try:
                ov_arr = _get_overlay_arr(ov_path, new_w, scaled_fh, keep_aspect, overlay_scale)
            except Exception as e:
                raise RuntimeError(f"failed to load overlay {_rel(ov_path)}: {e}")

            # Extract frame slice and composite (pass view directly; _composite_overlay only reads bg_arr)
            y0, y1 = f * scaled_fh, (f + 1) * scaled_fh
            out_arr[y0:y1, ...] = _composite_overlay(bg_arr[y0:y1, ...], ov_arr, overlay_alpha)

        if shutdown_event is not None and shutdown_event.is_set():
            return target_path, ""

        tmp = target_path + "_"
        Image.fromarray(out_arr, mode="RGBA").save(tmp, format="PNG", compress_level=1)
        os.replace(tmp, target_path)
        return target_path, ""


# ---------------------------------------------------------------------------
# Per-directory override helpers
# ---------------------------------------------------------------------------

def _parse_path_config(path_config_str: str | None) -> dict[str, dict[str, Any]]:
    """Parse --path-config JSON string into {path_key: {key: value}}.

    Keys can be exact file paths ("pack.png") or directory prefixes ("assets/gui").
    Example: '{"pack.png": {"scale": 1}, "assets/gui": {"scale": 2, "alpha": 0.5}}'
    Supported keys: scale (int), alpha (float), overlay-scale (float), keep-aspect (bool).
    """
    if not path_config_str:
        return {}
    return json.loads(path_config_str)


def _get_path_overrides(target: str, path_config: dict[str, dict[str, Any]]) -> dict[str, Any]:
    """Return overrides for target.

    Exact path match takes priority over prefix match; among prefix matches, longest wins.
    Normalize separators to forward slashes for consistent matching.
    Returns {} when nothing matches.
    """
    norm_target = target.replace("\\", "/")

    # Exact file match — unambiguously targets a single file.
    for key, overrides in path_config.items():
        if norm_target == key.replace("\\", "/"):
            return overrides

    # Prefix match for directory overrides: longest matching prefix wins.
    best: dict[str, Any] = {}
    best_prefix_len = 0
    for key, overrides in path_config.items():
        key_norm = key.replace("\\", "/")
        if norm_target.startswith(key_norm) and len(key_norm) > best_prefix_len:
            best = overrides
            best_prefix_len = len(key_norm)
    return best
