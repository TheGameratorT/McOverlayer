#!/usr/bin/env python3
"""High-performance image overlayer.

Features:
- Uses Pillow + NumPy for fast pixel operations
- Multithreaded processing with ThreadPoolExecutor
- Random non-repeating overlay selection (refills when exhausted)
- Entity texture face-by-face overlay support
"""
import os
import sys
import argparse
import random
import threading
import time
import json
from concurrent.futures import ThreadPoolExecutor, as_completed, CancelledError
from typing import List

from overlay_engine import (
    EntityOverlayMapper,
    OverlayMapper,
    get_images_in_dir,
    load_entity_regions,
    preload_overlays,
    process_entity_image,
    process_image,
    string_to_seed,
    _parse_path_config,
    _get_path_overrides,
)


_GREEN_BOLD = "\x1b[1;32m"
_RED_BOLD   = "\x1b[1;31m"
_RESET      = "\x1b[0m"


def _is_tty() -> bool:
    try:
        return sys.stdout.isatty()
    except Exception:
        return False


def _rel(path: str) -> str:
    """Return path relative to cwd, falling back to the original."""
    try:
        return os.path.relpath(path)
    except ValueError:
        return path


def _print_status(status: str, msg: str, color: str = ""):
    """Print a Cargo-style right-aligned status line.

    On a TTY the current line (e.g. the progress bar) is erased first so
    the status appears above it.
    """
    if color and _is_tty():
        line = f"{color}{status:>12}{_RESET} {msg}"
    else:
        line = f"{status:>12} {msg}"
    if _is_tty():
        sys.stdout.write(f"\r\x1b[K{line}\n")
    else:
        print(line)
    sys.stdout.flush()


def _print_progress(done: int, total: int, start: float, width: int = 40):
    """Overwrite the current line with a progress bar + ETA."""
    pct = done / total if total else 1.0
    filled = int(width * pct)
    bar = "=" * filled + " " * (width - filled)
    elapsed = time.time() - start
    rate = done / elapsed if elapsed > 0 else 0.0
    eta = (max(total - done, 0) / rate) if rate > 0 else 0.0
    line = f"{done}/{total} [{bar}] {pct * 100:5.1f}% ETA: {int(eta // 60):02d}:{int(eta % 60):02d}"
    if _is_tty():
        sys.stdout.write("\r" + line + "\x1b[K")
    else:
        print(line)
    sys.stdout.flush()


def _handle_future(fut, target: str, done: List[int], total: int, start: float, input_dir: str = ""):
    """Process one completed future: print its status and refresh the progress bar."""
    try:
        _, ov_path = fut.result()
        ov_display = os.path.relpath(ov_path, input_dir) if input_dir else _rel(ov_path)
        _print_status("Overlaying", f"{target} <- {ov_display}", _GREEN_BOLD)
    except CancelledError:
        return  # Silently skip tasks cancelled during shutdown
    except Exception as e:
        _print_status("error", f"processing {target}: {e}", _RED_BOLD)
    done[0] += 1
    _print_progress(done[0], total, start)


def main():
    parser = argparse.ArgumentParser(description="High-performance image overlayer")
    parser.add_argument("input_dir",  help="Directory with overlay images (source)")
    parser.add_argument("target_dir", help="Directory with target/background images to modify")
    parser.add_argument("--workers", "-w", type=int, default=None,
                        help="Worker threads (default: min(32, cpu_count×2))")
    parser.add_argument("--scale", "-s", type=int, default=4,
                        help="Resize scale factor (default: 4)")
    parser.add_argument("--alpha", type=float, default=0.75,
                        help="Global overlay alpha 0.0–1.0 (default: 0.75)")
    parser.add_argument("--seed", type=str, default=None,
                        help="Random seed as integer or string like Minecraft (default: random)")
    parser.add_argument("--per-frame", action="store_true",
                        help="Apply different overlays to each frame of animated textures")
    parser.add_argument("--entity-regions", type=str, default=None,
                        help="Path to entity_regions directory for face-based overlay")
    parser.add_argument("--entity-face-mode", choices=["same", "different"], default="same",
                        help="Entity face overlay mode: same (all faces get 1 overlay) or different (each face gets unique overlay) (default: same)")
    parser.add_argument("--entity-texture-mode", choices=["shared", "separate"], default="shared",
                        help="Entity texture mode: shared (all textures share overlay pool) or separate (each texture gets own pool) (default: shared)")
    parser.add_argument("--keep-aspect", action="store_true",
                        help="Preserve overlay aspect ratio (fit within target, letterbox with transparency)")
    parser.add_argument("--overlay-scale", type=float, default=1.0,
                        help="Overlay scale factor 0.0–1.0 (default: 1.0). Scales overlay size and centers it on target")
    parser.add_argument("--path-config", type=str, default=None,
                        help="Per-path setting overrides as JSON. Keys are exact file paths or directory prefixes. "
                             "Exact match takes priority over prefix match; longest prefix wins among prefix matches. "
                             "E.g. '{\"pack.png\":{\"scale\":1},\"assets/gui\":{\"scale\":2,\"alpha\":0.5}}'. "
                             "Supported keys: scale, alpha, overlay-scale, keep-aspect.")
    args = parser.parse_args()

    overlays = get_images_in_dir(args.input_dir)
    if not overlays:
        print(f"error: no overlay images found in {args.input_dir!r}", file=sys.stderr)
        sys.exit(1)

    targets = get_images_in_dir(args.target_dir)
    if not targets:
        print(f"error: no target images found in {args.target_dir!r}", file=sys.stderr)
        sys.exit(1)

    workers = args.workers or max(1, min(32, (os.cpu_count() or 1) * 2))

    # Parse seed: convert string to int using Minecraft's hashcode, or parse as int
    if args.seed is not None:
        try:
            seed = int(args.seed)
        except ValueError:
            seed = string_to_seed(args.seed)
    else:
        seed = random.SystemRandom().randint(0, 2**32 - 1)

    # Load entity regions if provided
    entities = {}
    texture_to_entity = {}
    if args.entity_regions:
        try:
            entities, texture_to_entity = load_entity_regions(args.entity_regions)
        except Exception as e:
            print(f"error: failed to load entity regions: {e}", file=sys.stderr)
            sys.exit(1)

    # Categorize targets into entity and regular.
    # Normalize tex_path separators once so we don't repeat .replace() in the inner loop.
    # entity_targets stores (target_path, entity_id, canonical_tex_path) to avoid a
    # duplicate lookup at submission time.
    entity_targets: list[tuple[str, str, str]] = []
    regular_targets: list[str] = []
    norm_tex_paths = [(tex.replace("\\", "/"), tex) for tex in texture_to_entity]
    for target in targets:
        norm_target = target.replace("\\", "/")
        matched_tex = None
        for norm_tex, canonical_tex in norm_tex_paths:
            if norm_target.endswith(norm_tex):
                matched_tex = canonical_tex
                break
        if matched_tex is not None:
            entity_targets.append((target, texture_to_entity[matched_tex], matched_tex))
        else:
            regular_targets.append(target)

    _print_status(
        "Overlaying",
        f"{len(targets)} images ({len(entity_targets)} entity, {len(regular_targets)} regular) with {workers} workers "
        f"(scale={args.scale}, alpha={args.alpha}, seed={seed})",
        _GREEN_BOLD,
    )

    # Pre-load all overlay images once so workers share decoded pixel data
    _print_status("Loading", "overlay images...", _GREEN_BOLD)
    start_load = time.time()
    _print_progress(0, len(overlays), start_load, width=30)
    try:
        def _progress_cb(done: int, total: int):
            _print_progress(done, total, start_load, width=30)

        preload_overlays(overlays, progress_cb=_progress_cb)
    except Exception as e:
        print(f"error: failed to pre-load overlays: {e}", file=sys.stderr)
        sys.exit(1)
    if _is_tty():
        sys.stdout.write("\r\x1b[K")
        sys.stdout.flush()

    # Save run config so the seed (and other args) can be recovered
    run_config = {
        "input_dir": args.input_dir,
        "target_dir": args.target_dir,
        "scale": args.scale,
        "path_config": args.path_config,
        "alpha": args.alpha,
        "seed": seed,
        "workers": workers,
        "per_frame": args.per_frame,
        "keep_aspect": args.keep_aspect,
        "overlay_scale": args.overlay_scale,
        "entity_regions": args.entity_regions,
        "entity_face_mode": args.entity_face_mode,
        "entity_texture_mode": args.entity_texture_mode,
    }
    try:
        with open("last_run.json", "w") as f:
            json.dump(run_config, f, indent=2)
    except OSError as e:
        _print_status("warning", f"could not save last_run.json: {e}", _RED_BOLD)

    meta_path = os.path.join(args.target_dir, "overlaymeta.json")
    try:
        with open(meta_path, "w") as f:
            json.dump(run_config, f, indent=2)
    except OSError as e:
        _print_status("warning", f"could not save overlaymeta.json: {e}", _RED_BOLD)

    # Parse per-path setting overrides
    try:
        path_config = _parse_path_config(args.path_config)
    except Exception as e:
        print(f"error: --path-config is not valid JSON: {e}", file=sys.stderr)
        sys.exit(1)

    # Build mappers
    mapper = None
    entity_mapper = None
    if regular_targets:
        mapper = OverlayMapper(regular_targets, overlays, seed=seed, per_frame=args.per_frame,
                               target_dir=args.target_dir, overlay_dir=args.input_dir)
    if entity_targets:
        entity_mapper = EntityOverlayMapper(
            entities, overlays,
            face_mode=args.entity_face_mode,
            texture_mode=args.entity_texture_mode,
            seed=seed,
            overlay_dir=args.input_dir
        )

    total  = len(targets)
    done   = [0]   # mutable int so _handle_future can update it
    start  = time.time()
    shutdown_event = threading.Event()

    _print_progress(done[0], total, start)

    interrupted = False
    with ThreadPoolExecutor(max_workers=workers) as exc:
        futures_map = {}

        # Submit entity targets (entity_id and canonical_tex_path already resolved)
        for t, entity_id, canonical_tex in entity_targets:
            rel_t = os.path.relpath(t, args.target_dir)
            entity_data = entities[entity_id]
            face_overlays = entity_mapper.get_face_overlays(canonical_tex)
            ov = _get_path_overrides(rel_t, path_config)
            fut = exc.submit(
                process_entity_image, t, face_overlays,
                entity_data.texture_size,
                ov.get("scale", args.scale),
                ov.get("alpha", args.alpha),
                shutdown_event,
                ov.get("keep-aspect", args.keep_aspect),
                ov.get("overlay-scale", args.overlay_scale),
            )
            futures_map[fut] = rel_t

        # Submit regular targets
        if mapper:
            for t in regular_targets:
                rel_t = os.path.relpath(t, args.target_dir)
                ov = _get_path_overrides(rel_t, path_config)
                fut = exc.submit(
                    process_image, t, mapper,
                    ov.get("scale", args.scale),
                    ov.get("alpha", args.alpha),
                    shutdown_event,
                    ov.get("keep-aspect", args.keep_aspect),
                    ov.get("overlay-scale", args.overlay_scale),
                )
                futures_map[fut] = rel_t

        try:
            for fut in as_completed(futures_map):
                _handle_future(fut, futures_map[fut], done, total, start, args.input_dir)
        except KeyboardInterrupt:
            interrupted = True
            shutdown_event.set()
            for f in futures_map:
                if not f.done():
                    f.cancel()
            # ThreadPoolExecutor.__exit__ (called when the `with` block exits)
            # will wait for any already-running tasks to finish.

    # Move past the progress bar before printing the final status line.
    if _is_tty():
        sys.stdout.write("\r\x1b[K")

    if interrupted:
        _print_status("Interrupted", f"{done[0]}/{total} images processed", _RED_BOLD)
    else:
        _print_status("Finished", f"{total} images in {time.time() - start:.2f}s", _GREEN_BOLD)


if __name__ == "__main__":
    main()
