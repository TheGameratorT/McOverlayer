#!/usr/bin/env python3
"""preview_mapping.py — Interactive seed explorer for MCOverlayer.

Shows which overlay is assigned to each target texture, with a config panel
for adjusting all parameters on the fly.

Usage:
    python preview_mapping.py                  # loads last_run.json
    python preview_mapping.py --config my.json
"""
from __future__ import annotations

import argparse
import copy
import json
import os
import random
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from typing import List, Optional

_ANSI_RE = re.compile(r'\x1b\[[0-9;]*[mKA-Za-z]')
_PROGRESS_RE = re.compile(r'^(\d+)/(\d+) \[')

from PyQt6.QtCore import Qt, QThread, pyqtSignal, QRect, QSize, QPoint
from PyQt6.QtWidgets import QLayout
from PyQt6.QtGui import QImage, QPixmap, QColor, QPalette, QFont, QKeySequence, QShortcut
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QScrollArea, QLabel, QLineEdit, QPushButton, QComboBox,
    QSpinBox, QDoubleSpinBox, QFrame, QStatusBar, QSizePolicy,
    QDialog, QTextEdit, QDialogButtonBox, QGroupBox, QCheckBox,
    QSplitter, QFileDialog, QListWidget, QListWidgetItem, QProgressBar,
)

from PIL import Image
import numpy as np

from overlay_engine import (
    EntityData,
    EntityOverlayMapper,
    EntityRegion,
    OverlayMapper,
    get_images_in_dir,
    load_entity_regions,
    string_to_seed,
    _composite_overlay,
    _resize_overlay,
)


# ---------------------------------------------------------------------------
# Configuration dataclass
# ---------------------------------------------------------------------------

@dataclass
class MappingConfig:
    """All parameters needed to reproduce an overlay mapping pass."""

    overlay_dir: str
    texture_dir: str
    seed: int
    per_frame: bool = False
    entity_regions_dir: Optional[str] = None
    entity_face_mode: str = "same"
    entity_texture_mode: str = "shared"
    alpha: float = 0.75
    scale: int = 4
    keep_aspect: bool = False
    overlay_scale: float = 1.0
    dir_config: Optional[str] = None
    output_dir: Optional[str] = None  # Optional output folder (copy destination)
    copy_before_apply: bool = False

    @classmethod
    def from_last_run(cls, path: str = "last_run.json") -> "MappingConfig":
        """Load configuration from a last_run.json file."""
        with open(path) as f:
            data = json.load(f)
        # Support both old and new field names for migration
        overlay_dir = data.get("overlay_dir") or data.get("input_dir", "dataset/")
        texture_dir = data.get("texture_dir") or data.get("target_dir", "target/")
        output_dir = data.get("output_dir") or data.get("source_dir")
        return cls(
            overlay_dir=overlay_dir,
            texture_dir=texture_dir,
            seed=int(data.get("seed", 0)),
            per_frame=bool(data.get("per_frame", False)),
            entity_regions_dir=data.get("entity_regions"),
            entity_face_mode=data.get("entity_face_mode", "same"),
            entity_texture_mode=data.get("entity_texture_mode", "shared"),
            alpha=float(data.get("alpha", 0.75)),
            scale=int(data.get("scale", 4)),
            keep_aspect=bool(data.get("keep_aspect", False)),
            overlay_scale=float(data.get("overlay_scale", 1.0)),
            dir_config=data.get("dir_config"),
            output_dir=output_dir,
            copy_before_apply=bool(data.get("copy_before_apply", False)),
        )

    def to_last_run_dict(self) -> dict:
        """Serialize to last_run.json format."""
        return {
            "overlay_dir": self.overlay_dir,
            "texture_dir": self.texture_dir,
            "seed": self.seed,
            "per_frame": self.per_frame,
            "entity_regions": self.entity_regions_dir,
            "entity_face_mode": self.entity_face_mode,
            "entity_texture_mode": self.entity_texture_mode,
            "alpha": self.alpha,
            "scale": self.scale,
            "keep_aspect": self.keep_aspect,
            "overlay_scale": self.overlay_scale,
            "dir_config": self.dir_config,
            "output_dir": self.output_dir,
            "copy_before_apply": self.copy_before_apply,
        }

    def with_seed(self, seed: int | str) -> "MappingConfig":
        """Return a copy with a new seed (accepts integer or string)."""
        c = copy.copy(self)
        if isinstance(seed, str):
            try:
                c.seed = int(seed)
            except ValueError:
                c.seed = string_to_seed(seed)
        else:
            c.seed = int(seed)
        return c

    def with_random_seed(self) -> "MappingConfig":
        """Return a copy with a cryptographically random seed."""
        return self.with_seed(random.SystemRandom().randint(0, 2**32 - 1))


@dataclass
class TextureAssignment:
    """Mapping result for a single target texture."""

    target_path: str
    overlay_path: str
    is_entity: bool = False
    entity_id: Optional[str] = None
    face_overlays: Optional[List[tuple[EntityRegion, str]]] = None


def build_assignments(config: MappingConfig) -> tuple[
    List[TextureAssignment], List[str], List[str]
]:
    """Build overlay assignments for all targets given a MappingConfig.

    Returns:
        (assignments, overlay_paths, target_paths)
    """
    overlays = get_images_in_dir(config.overlay_dir)
    if not overlays:
        raise ValueError(f"No overlay images found in {config.overlay_dir!r}")

    targets = get_images_in_dir(config.texture_dir)
    if not targets:
        raise ValueError(f"No texture images found in {config.texture_dir!r}")

    seed = config.seed

    entities: dict[str, EntityData] = {}
    texture_to_entity: dict[str, str] = {}
    if config.entity_regions_dir:
        entities, texture_to_entity = load_entity_regions(config.entity_regions_dir)

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

    assignments: List[TextureAssignment] = []

    if regular_targets:
        mapper = OverlayMapper(
            regular_targets, overlays, seed=seed, per_frame=config.per_frame,
            target_dir=config.texture_dir, overlay_dir=config.overlay_dir,
        )
        for t in regular_targets:
            assignments.append(TextureAssignment(
                target_path=t,
                overlay_path=mapper.get_overlay(t),
            ))

    if entity_targets:
        entity_mapper = EntityOverlayMapper(
            entities, overlays,
            face_mode=config.entity_face_mode,
            texture_mode=config.entity_texture_mode,
            seed=seed,
            overlay_dir=config.overlay_dir,
        )
        for t, entity_id, canonical_tex in entity_targets:
            face_ovs = entity_mapper.get_face_overlays(canonical_tex)
            primary = face_ovs[0][1] if face_ovs else overlays[0]
            assignments.append(TextureAssignment(
                target_path=t,
                overlay_path=primary,
                is_entity=True,
                entity_id=entity_id,
                face_overlays=face_ovs,
            ))

    return assignments, overlays, targets


# ---------------------------------------------------------------------------
# Thumbnail helpers
# ---------------------------------------------------------------------------

def load_target_thumb(path: str, size: tuple[int, int] = (80, 80)) -> Image.Image:
    """Load a target texture as a nearest-neighbour upscaled thumbnail."""
    img = Image.open(path).convert("RGBA")
    if img.height > img.width:
        img = img.crop((0, 0, img.width, img.width))
    img = img.resize(size, resample=Image.NEAREST)
    return img


def load_overlay_thumb(path: str, size: tuple[int, int] = (120, 90)) -> Image.Image:
    """Load an overlay image as a Lanczos-downscaled thumbnail, padded to exact size."""
    img = Image.open(path).convert("RGBA")
    img.thumbnail(size, resample=Image.LANCZOS)
    canvas = Image.new("RGBA", size, (0, 0, 0, 0))
    x = (size[0] - img.width) // 2
    y = (size[1] - img.height) // 2
    canvas.paste(img, (x, y))
    return canvas


def make_composite_thumb(
    target_path: str,
    overlay_path: str,
    size: tuple[int, int] = (80, 80),
    alpha: float = 0.5,
    keep_aspect: bool = True,
    overlay_scale: float = 1.0,
) -> Image.Image:
    """Render a blended composite of target + overlay at thumbnail size."""
    target = Image.open(target_path).convert("RGBA")
    if target.height > target.width:
        target = target.crop((0, 0, target.width, target.width))
    target = target.resize(size, resample=Image.NEAREST)

    overlay = Image.open(overlay_path).convert("RGBA")
    ov_arr = _resize_overlay(overlay, size[0], size[1], keep_aspect, overlay_scale)
    bg_arr = np.array(target)
    blended = _composite_overlay(bg_arr, ov_arr, alpha)
    return Image.fromarray(blended)


# ---------------------------------------------------------------------------
# Layout constants
# ---------------------------------------------------------------------------

CARD_WIDTH    = 160
CARD_MARGIN   = 6
_CONTENT_W    = CARD_WIDTH - 2 * CARD_MARGIN
COMP_THUMB_SIZE = (_CONTENT_W, _CONTENT_W)

FLOW_H_GAP = 8
FLOW_V_GAP = 8

DEFAULT_MAX = 20

_PRIORITY_NAMES = [
    "pack", "stone", "grass_block_top", "grass_block", "dirt", "sand", "gravel",
    "cobblestone", "oak_planks", "oak_log", "coal_ore", "iron_ore", "gold_ore",
    "diamond_ore", "oak_leaves", "water_still", "lava_still", "crafting_table",
    "furnace_front", "chest",
]


def _priority_score(target_path: str) -> int:
    """Return sort key: lower = higher priority."""
    name = os.path.basename(target_path).lower()
    for i, pat in enumerate(_PRIORITY_NAMES):
        if pat in name:
            return i
    return len(_PRIORITY_NAMES)


# ---------------------------------------------------------------------------
# PIL → QImage
# ---------------------------------------------------------------------------

def _pil_to_qimage(img: Image.Image) -> QImage:
    img = img.convert("RGBA")
    data = img.tobytes("raw", "RGBA")
    qi = QImage(data, img.width, img.height, QImage.Format.Format_RGBA8888)
    return qi.copy()


def _placeholder_pixmap(w: int, h: int, color: str = "#1c1c1c") -> QPixmap:
    pm = QPixmap(w, h)
    pm.fill(QColor(color))
    return pm


# ---------------------------------------------------------------------------
# Flow layout
# ---------------------------------------------------------------------------

class FlowLayout(QLayout):
    def __init__(self, parent=None, h_gap: int = FLOW_H_GAP, v_gap: int = FLOW_V_GAP):
        super().__init__(parent)
        self._items: list = []
        self._h = h_gap
        self._v = v_gap

    def addItem(self, item):
        self._items.append(item)

    def count(self) -> int:
        return len(self._items)

    def itemAt(self, index: int):
        return self._items[index] if 0 <= index < len(self._items) else None

    def takeAt(self, index: int):
        return self._items.pop(index) if 0 <= index < len(self._items) else None

    def expandingDirections(self):
        return Qt.Orientation(0)

    def hasHeightForWidth(self) -> bool:
        return True

    def heightForWidth(self, width: int) -> int:
        return self._layout(QRect(0, 0, width, 0), dry_run=True)

    def setGeometry(self, rect: QRect):
        super().setGeometry(rect)
        self._layout(rect, dry_run=False)

    def sizeHint(self) -> QSize:
        return self.minimumSize()

    def minimumSize(self) -> QSize:
        s = QSize()
        for item in self._items:
            s = s.expandedTo(item.minimumSize())
        m = self.contentsMargins()
        s += QSize(m.left() + m.right(), m.top() + m.bottom())
        return s

    def _layout(self, rect: QRect, dry_run: bool) -> int:
        m = self.contentsMargins()
        r = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom())
        x, y, line_h = r.x(), r.y(), 0

        for item in self._items:
            hint = item.sizeHint()
            next_x = x + hint.width() + self._h
            if next_x - self._h > r.right() and line_h > 0:
                x = r.x()
                y += line_h + self._v
                next_x = x + hint.width() + self._h
                line_h = 0
            if not dry_run:
                item.setGeometry(QRect(QPoint(x, y), hint))
            x = next_x
            line_h = max(line_h, hint.height())

        return y + line_h - rect.y() + m.bottom()


# ---------------------------------------------------------------------------
# Composite thumbnail loader
# ---------------------------------------------------------------------------

class ThumbnailLoader(QThread):
    """Renders composite thumbnails in a background thread pool."""
    loaded = pyqtSignal(int, QImage)

    def __init__(
        self,
        assignments: List[TextureAssignment],
        alpha: float,
        keep_aspect: bool,
        overlay_scale: float,
    ):
        super().__init__()
        self._assignments = assignments
        self._alpha = alpha
        self._keep_aspect = keep_aspect
        self._overlay_scale = overlay_scale
        self._cancelled = False

    def cancel(self):
        self._cancelled = True

    def run(self):
        pool = ThreadPoolExecutor(max_workers=4)
        try:
            futures = {
                pool.submit(self._render_one, i, a): i
                for i, a in enumerate(self._assignments)
            }
            for fut in as_completed(futures):
                if self._cancelled:
                    break
                i = futures[fut]
                try:
                    qi = fut.result()
                    if qi is not None:
                        self.loaded.emit(i, qi)
                except Exception:
                    pass
        finally:
            pool.shutdown(wait=False, cancel_futures=True)

    def _render_one(self, i: int, a: TextureAssignment) -> Optional[QImage]:
        if self._cancelled:
            return None
        try:
            img = make_composite_thumb(
                a.target_path, a.overlay_path,
                COMP_THUMB_SIZE, self._alpha, self._keep_aspect, self._overlay_scale,
            )
            return _pil_to_qimage(img)
        except Exception:
            return None


# ---------------------------------------------------------------------------
# Assignment card widget
# ---------------------------------------------------------------------------

class AssignmentCard(QFrame):
    def __init__(self, assignment: TextureAssignment):
        super().__init__()
        self.assignment = assignment
        self.setFixedWidth(CARD_WIDTH)
        self.setFrameShape(QFrame.Shape.StyledPanel)
        self._build()

    def _build(self):
        a = self.assignment
        layout = QVBoxLayout(self)
        layout.setContentsMargins(CARD_MARGIN, CARD_MARGIN, CARD_MARGIN, CARD_MARGIN)
        layout.setSpacing(3)

        if a.is_entity and a.entity_id:
            badge = QLabel(f"● {a.entity_id}")
            badge.setAlignment(Qt.AlignmentFlag.AlignCenter)
            badge.setStyleSheet("color: #88c0d0; font-size: 9px; font-weight: bold;")
            layout.addWidget(badge)

        self.comp_lbl = QLabel()
        self.comp_lbl.setFixedSize(*COMP_THUMB_SIZE)
        self.comp_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.comp_lbl.setPixmap(_placeholder_pixmap(*COMP_THUMB_SIZE, "#1c1c1c"))
        self.comp_lbl.setToolTip(a.target_path)
        layout.addWidget(self.comp_lbl, alignment=Qt.AlignmentFlag.AlignCenter)

        t_name = os.path.basename(a.target_path)
        tl = QLabel(t_name)
        tl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        tl.setStyleSheet("font-size: 9px; color: #999;")
        tl.setWordWrap(True)
        tl.setToolTip(a.target_path)
        layout.addWidget(tl)

        o_name = os.path.basename(a.overlay_path)
        ol = QLabel(o_name[:30] + "…" if len(o_name) > 30 else o_name)
        ol.setAlignment(Qt.AlignmentFlag.AlignCenter)
        ol.setStyleSheet("font-size: 9px; color: #88c0d0;")
        ol.setWordWrap(True)
        ol.setToolTip(a.overlay_path)
        layout.addWidget(ol)

    def set_image(self, qi: QImage):
        """Called from main thread when composite thumbnail is ready."""
        self.comp_lbl.setPixmap(
            QPixmap.fromImage(qi).scaled(
                *COMP_THUMB_SIZE,
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation,
            )
        )


# ---------------------------------------------------------------------------
# Directory config list widget
# ---------------------------------------------------------------------------

class DirConfigWidget(QWidget):
    """List + detail panel for per-directory setting overrides."""
    changed = pyqtSignal()

    def __init__(self, dir_config_str: Optional[str] = None):
        super().__init__()
        self._loading = False
        self._build()
        if dir_config_str:
            self.from_json_str(dir_config_str)

    def _build(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(4)

        self._list = QListWidget()
        self._list.setMaximumHeight(90)
        self._list.currentRowChanged.connect(self._on_selection_changed)
        layout.addWidget(self._list)

        btn_row = QHBoxLayout()
        add_btn = QPushButton("+ Add")
        add_btn.setMaximumWidth(60)
        add_btn.clicked.connect(self._add_entry)
        btn_row.addWidget(add_btn)
        self._remove_btn = QPushButton("- Remove")
        self._remove_btn.setMaximumWidth(75)
        self._remove_btn.setEnabled(False)
        self._remove_btn.clicked.connect(self._remove_entry)
        btn_row.addWidget(self._remove_btn)
        btn_row.addStretch()
        layout.addLayout(btn_row)

        self._detail = QGroupBox("Entry Settings")
        self._detail.setVisible(False)
        det = QVBoxLayout(self._detail)
        det.setSpacing(3)

        prefix_row = QHBoxLayout()
        prefix_row.addWidget(QLabel("Path prefix:"))
        self._prefix_edit = QLineEdit()
        self._prefix_edit.setPlaceholderText("e.g. block, assets/gui")
        self._prefix_edit.textChanged.connect(self._on_detail_changed)
        prefix_row.addWidget(self._prefix_edit)
        det.addLayout(prefix_row)

        scale_row = QHBoxLayout()
        self._scale_cb = QCheckBox("Scale:")
        self._scale_cb.toggled.connect(self._on_detail_changed)
        scale_row.addWidget(self._scale_cb)
        self._scale_spin = QSpinBox()
        self._scale_spin.setRange(1, 32)
        self._scale_spin.setValue(4)
        self._scale_spin.setEnabled(False)
        self._scale_spin.valueChanged.connect(self._on_detail_changed)
        self._scale_cb.toggled.connect(self._scale_spin.setEnabled)
        scale_row.addWidget(self._scale_spin)
        scale_row.addStretch()
        det.addLayout(scale_row)

        alpha_row = QHBoxLayout()
        self._alpha_cb = QCheckBox("Alpha:")
        self._alpha_cb.toggled.connect(self._on_detail_changed)
        alpha_row.addWidget(self._alpha_cb)
        self._alpha_spin = QDoubleSpinBox()
        self._alpha_spin.setRange(0.0, 1.0)
        self._alpha_spin.setSingleStep(0.05)
        self._alpha_spin.setDecimals(2)
        self._alpha_spin.setValue(0.75)
        self._alpha_spin.setEnabled(False)
        self._alpha_spin.valueChanged.connect(self._on_detail_changed)
        self._alpha_cb.toggled.connect(self._alpha_spin.setEnabled)
        alpha_row.addWidget(self._alpha_spin)
        alpha_row.addStretch()
        det.addLayout(alpha_row)

        ovl_row = QHBoxLayout()
        self._ovl_cb = QCheckBox("Overlay scale:")
        self._ovl_cb.toggled.connect(self._on_detail_changed)
        ovl_row.addWidget(self._ovl_cb)
        self._ovl_spin = QDoubleSpinBox()
        self._ovl_spin.setRange(0.0, 2.0)
        self._ovl_spin.setSingleStep(0.1)
        self._ovl_spin.setDecimals(2)
        self._ovl_spin.setValue(1.0)
        self._ovl_spin.setEnabled(False)
        self._ovl_spin.valueChanged.connect(self._on_detail_changed)
        self._ovl_cb.toggled.connect(self._ovl_spin.setEnabled)
        ovl_row.addWidget(self._ovl_spin)
        ovl_row.addStretch()
        det.addLayout(ovl_row)

        ka_row = QHBoxLayout()
        self._ka_cb = QCheckBox("Keep aspect:")
        self._ka_cb.toggled.connect(self._on_detail_changed)
        ka_row.addWidget(self._ka_cb)
        self._ka_combo = QComboBox()
        self._ka_combo.addItems(["Yes", "No"])
        self._ka_combo.setEnabled(False)
        self._ka_combo.currentIndexChanged.connect(self._on_detail_changed)
        self._ka_cb.toggled.connect(self._ka_combo.setEnabled)
        ka_row.addWidget(self._ka_combo)
        ka_row.addStretch()
        det.addLayout(ka_row)

        layout.addWidget(self._detail)

    def _add_entry(self):
        data: dict = {"prefix": "new_dir"}
        item = QListWidgetItem(self._format_item(data))
        item.setData(Qt.ItemDataRole.UserRole, data)
        self._list.addItem(item)
        self._list.setCurrentRow(self._list.count() - 1)
        self.changed.emit()

    def _remove_entry(self):
        row = self._list.currentRow()
        if row >= 0:
            self._list.takeItem(row)
            if self._list.count() == 0:
                self._detail.setVisible(False)
                self._remove_btn.setEnabled(False)
            self.changed.emit()

    def _on_selection_changed(self, row: int):
        if row < 0:
            self._detail.setVisible(False)
            self._remove_btn.setEnabled(False)
            return
        self._remove_btn.setEnabled(True)
        self._detail.setVisible(True)
        item = self._list.item(row)
        data = item.data(Qt.ItemDataRole.UserRole) or {}
        self._loading = True
        self._prefix_edit.setText(data.get("prefix", ""))
        for cb, spin, key in [
            (self._scale_cb, self._scale_spin, "scale"),
            (self._alpha_cb, self._alpha_spin, "alpha"),
            (self._ovl_cb,   self._ovl_spin,   "overlay-scale"),
        ]:
            has = key in data
            cb.setChecked(has)
            spin.setEnabled(has)
            if has:
                spin.setValue(data[key])
        has_ka = "keep-aspect" in data
        self._ka_cb.setChecked(has_ka)
        self._ka_combo.setEnabled(has_ka)
        if has_ka:
            self._ka_combo.setCurrentText("Yes" if data["keep-aspect"] else "No")
        self._loading = False

    def _on_detail_changed(self):
        if self._loading:
            return
        row = self._list.currentRow()
        if row < 0:
            return
        data: dict = {"prefix": self._prefix_edit.text().strip() or "unnamed"}
        if self._scale_cb.isChecked():
            data["scale"] = self._scale_spin.value()
        if self._alpha_cb.isChecked():
            data["alpha"] = self._alpha_spin.value()
        if self._ovl_cb.isChecked():
            data["overlay-scale"] = self._ovl_spin.value()
        if self._ka_cb.isChecked():
            data["keep-aspect"] = self._ka_combo.currentText() == "Yes"
        item = self._list.item(row)
        item.setData(Qt.ItemDataRole.UserRole, data)
        item.setText(self._format_item(data))
        self.changed.emit()

    def _format_item(self, data: dict) -> str:
        parts = [data.get("prefix", "unnamed")]
        if "scale" in data:
            parts.append(f"scale={data['scale']}")
        if "alpha" in data:
            parts.append(f"α={data['alpha']:.2f}")
        if "overlay-scale" in data:
            parts.append(f"ovl={data['overlay-scale']:.2f}")
        if "keep-aspect" in data:
            parts.append(f"ka={'Y' if data['keep-aspect'] else 'N'}")
        return "  ".join(parts)

    def to_json_str(self) -> Optional[str]:
        result = {}
        for i in range(self._list.count()):
            item = self._list.item(i)
            data = item.data(Qt.ItemDataRole.UserRole) or {}
            prefix = data.get("prefix", "").strip()
            if not prefix:
                continue
            result[prefix] = {k: v for k, v in data.items() if k != "prefix"}
        return json.dumps(result) if result else None

    def from_json_str(self, s: Optional[str]):
        self._list.clear()
        self._detail.setVisible(False)
        self._remove_btn.setEnabled(False)
        if not s:
            return
        try:
            data = json.loads(s)
        except (json.JSONDecodeError, TypeError):
            return
        for prefix, settings in data.items():
            item_data = {"prefix": prefix, **settings}
            item = QListWidgetItem(self._format_item(item_data))
            item.setData(Qt.ItemDataRole.UserRole, item_data)
            self._list.addItem(item)


# ---------------------------------------------------------------------------
# Config panel widget
# ---------------------------------------------------------------------------

class ConfigPanel(QWidget):
    """Left panel with all configuration controls."""
    config_changed = pyqtSignal(MappingConfig)

    def __init__(self, config: MappingConfig):
        super().__init__()
        self._config = config
        self._build()

    def _build(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(6, 6, 6, 6)
        layout.setSpacing(8)

        # Paths group
        paths_grp = QGroupBox("Paths")
        paths_lay = QVBoxLayout(paths_grp)
        paths_lay.setSpacing(4)

        # Overlay images dir
        input_row = QHBoxLayout()
        input_row.addWidget(QLabel("Overlay Images:"))
        self._input_edit = QLineEdit(self._config.overlay_dir)
        self._input_edit.textChanged.connect(self._on_change)
        input_row.addWidget(self._input_edit)
        self._input_btn = QPushButton("📁")
        self._input_btn.setMaximumWidth(26)
        self._input_btn.clicked.connect(self._browse_input)
        input_row.addWidget(self._input_btn)
        paths_lay.addLayout(input_row)

        # Texture images dir
        target_row = QHBoxLayout()
        target_row.addWidget(QLabel("Texture Images:"))
        self._target_edit = QLineEdit(self._config.texture_dir)
        self._target_edit.textChanged.connect(self._on_change)
        target_row.addWidget(self._target_edit)
        self._target_btn = QPushButton("📁")
        self._target_btn.setMaximumWidth(26)
        self._target_btn.clicked.connect(self._browse_target)
        target_row.addWidget(self._target_btn)
        paths_lay.addLayout(target_row)

        layout.addWidget(paths_grp)

        # Core settings group
        core_grp = QGroupBox("Core Settings")
        core_lay = QVBoxLayout(core_grp)
        core_lay.setSpacing(4)

        # Alpha
        alpha_row = QHBoxLayout()
        alpha_row.addWidget(QLabel("Alpha:"))
        self._alpha_spin = QDoubleSpinBox()
        self._alpha_spin.setRange(0.0, 1.0)
        self._alpha_spin.setSingleStep(0.05)
        self._alpha_spin.setDecimals(2)
        self._alpha_spin.setValue(self._config.alpha)
        self._alpha_spin.valueChanged.connect(self._on_change)
        alpha_row.addWidget(self._alpha_spin)
        core_lay.addLayout(alpha_row)

        # Scale
        scale_row = QHBoxLayout()
        scale_row.addWidget(QLabel("Scale:"))
        self._scale_spin = QSpinBox()
        self._scale_spin.setRange(1, 32)
        self._scale_spin.setValue(self._config.scale)
        self._scale_spin.valueChanged.connect(self._on_change)
        scale_row.addWidget(self._scale_spin)
        core_lay.addLayout(scale_row)

        # Overlay scale
        ovl_row = QHBoxLayout()
        ovl_row.addWidget(QLabel("Overlay Scale:"))
        self._overlay_scale_spin = QDoubleSpinBox()
        self._overlay_scale_spin.setRange(0.0, 2.0)
        self._overlay_scale_spin.setSingleStep(0.1)
        self._overlay_scale_spin.setDecimals(2)
        self._overlay_scale_spin.setValue(self._config.overlay_scale)
        self._overlay_scale_spin.valueChanged.connect(self._on_change)
        ovl_row.addWidget(self._overlay_scale_spin)
        core_lay.addLayout(ovl_row)

        # Per-frame
        self._per_frame_cb = QCheckBox("Per-frame overlays")
        self._per_frame_cb.setChecked(self._config.per_frame)
        self._per_frame_cb.stateChanged.connect(self._on_change)
        core_lay.addWidget(self._per_frame_cb)

        # Keep aspect
        self._keep_aspect_cb = QCheckBox("Keep aspect ratio")
        self._keep_aspect_cb.setChecked(self._config.keep_aspect)
        self._keep_aspect_cb.stateChanged.connect(self._on_change)
        core_lay.addWidget(self._keep_aspect_cb)

        layout.addWidget(core_grp)

        # Dir config group
        dir_config_grp = QGroupBox("Directory Config")
        dir_config_lay = QVBoxLayout(dir_config_grp)
        self._dir_config_widget = DirConfigWidget(self._config.dir_config)
        self._dir_config_widget.changed.connect(self._on_change)
        dir_config_lay.addWidget(self._dir_config_widget)
        layout.addWidget(dir_config_grp)

        # Entity settings group
        entity_grp = QGroupBox("Entity Settings")
        entity_lay = QVBoxLayout(entity_grp)
        entity_lay.setSpacing(4)

        # Entity regions dir
        entity_row = QHBoxLayout()
        entity_row.addWidget(QLabel("Regions:"))
        self._entity_edit = QLineEdit(self._config.entity_regions_dir or "")
        self._entity_edit.setPlaceholderText("entity_regions/")
        self._entity_edit.textChanged.connect(self._on_change)
        entity_row.addWidget(self._entity_edit)
        self._entity_btn = QPushButton("📁")
        self._entity_btn.setMaximumWidth(26)
        self._entity_btn.clicked.connect(self._browse_entity)
        entity_row.addWidget(self._entity_btn)
        entity_lay.addLayout(entity_row)

        # Face mode
        face_row = QHBoxLayout()
        face_row.addWidget(QLabel("Face mode:"))
        self._face_combo = QComboBox()
        self._face_combo.addItems(["same", "different"])
        self._face_combo.setCurrentText(self._config.entity_face_mode)
        self._face_combo.currentTextChanged.connect(self._on_change)
        face_row.addWidget(self._face_combo)
        entity_lay.addLayout(face_row)

        # Texture mode
        tex_row = QHBoxLayout()
        tex_row.addWidget(QLabel("Texture mode:"))
        self._texture_combo = QComboBox()
        self._texture_combo.addItems(["shared", "separate"])
        self._texture_combo.setCurrentText(self._config.entity_texture_mode)
        self._texture_combo.currentTextChanged.connect(self._on_change)
        tex_row.addWidget(self._texture_combo)
        entity_lay.addLayout(tex_row)

        layout.addWidget(entity_grp)

        # Update button
        self._update_btn = QPushButton("Update Preview")
        self._update_btn.clicked.connect(self._emit_config)
        layout.addWidget(self._update_btn)

        layout.addStretch()

    def _browse_input(self):
        path = QFileDialog.getExistingDirectory(self, "Select Overlay Images Directory", self._input_edit.text())
        if path:
            self._input_edit.setText(path)

    def _browse_target(self):
        path = QFileDialog.getExistingDirectory(self, "Select Texture Images Directory", self._target_edit.text())
        if path:
            self._target_edit.setText(path)

    def _browse_entity(self):
        path = QFileDialog.getExistingDirectory(self, "Select Entity Regions Directory", self._entity_edit.text())
        if path:
            self._entity_edit.setText(path)

    def _on_change(self):
        """Mark that something changed (for visual feedback)."""
        pass

    def _emit_config(self):
        """Build current config and emit signal."""
        self._config = MappingConfig(
            overlay_dir=self._input_edit.text().strip(),
            texture_dir=self._target_edit.text().strip(),
            seed=self._config.seed,
            per_frame=self._per_frame_cb.isChecked(),
            entity_regions_dir=self._entity_edit.text().strip() or None,
            entity_face_mode=self._face_combo.currentText(),
            entity_texture_mode=self._texture_combo.currentText(),
            alpha=self._alpha_spin.value(),
            scale=self._scale_spin.value(),
            keep_aspect=self._keep_aspect_cb.isChecked(),
            overlay_scale=self._overlay_scale_spin.value(),
            dir_config=self._dir_config_widget.to_json_str(),
            output_dir=self._config.output_dir,
            copy_before_apply=self._config.copy_before_apply,
        )
        self.config_changed.emit(self._config)

    def set_seed(self, seed: int):
        """Update the config's seed (called from toolbar)."""
        self._config = self._config.with_seed(seed)

    def get_config(self) -> MappingConfig:
        """Get current config state."""
        return self._config

    def set_config(self, config: MappingConfig):
        """Update panel to reflect new config."""
        self._config = config
        self._input_edit.blockSignals(True)
        self._target_edit.blockSignals(True)
        self._alpha_spin.blockSignals(True)
        self._scale_spin.blockSignals(True)
        self._overlay_scale_spin.blockSignals(True)
        self._per_frame_cb.blockSignals(True)
        self._keep_aspect_cb.blockSignals(True)
        self._entity_edit.blockSignals(True)
        self._face_combo.blockSignals(True)
        self._texture_combo.blockSignals(True)

        self._input_edit.setText(config.overlay_dir)
        self._target_edit.setText(config.texture_dir)
        self._alpha_spin.setValue(config.alpha)
        self._scale_spin.setValue(config.scale)
        self._overlay_scale_spin.setValue(config.overlay_scale)
        self._per_frame_cb.setChecked(config.per_frame)
        self._keep_aspect_cb.setChecked(config.keep_aspect)
        self._dir_config_widget.from_json_str(config.dir_config)
        self._entity_edit.setText(config.entity_regions_dir or "")
        self._face_combo.setCurrentText(config.entity_face_mode)
        self._texture_combo.setCurrentText(config.entity_texture_mode)

        self._input_edit.blockSignals(False)
        self._target_edit.blockSignals(False)
        self._alpha_spin.blockSignals(False)
        self._scale_spin.blockSignals(False)
        self._overlay_scale_spin.blockSignals(False)
        self._per_frame_cb.blockSignals(False)
        self._keep_aspect_cb.blockSignals(False)
        self._entity_edit.blockSignals(False)
        self._face_combo.blockSignals(False)
        self._texture_combo.blockSignals(False)


# ---------------------------------------------------------------------------
# Apply dialog with copy support
# ---------------------------------------------------------------------------

class _SubprocessWorker(QThread):
    output   = pyqtSignal(str)
    progress = pyqtSignal(int, int)   # done, total
    finished = pyqtSignal(int)

    def __init__(self, cmd: List[str], copy_src: Optional[str] = None, copy_tgt: Optional[str] = None):
        super().__init__()
        self._cmd = cmd
        self._copy_src = copy_src
        self._copy_tgt = copy_tgt

    def run(self):
        try:
            # Do copy first if requested
            if self._copy_src and self._copy_tgt:
                self.output.emit(f"Copying {self._copy_src} → {self._copy_tgt}...")
                try:
                    if os.path.exists(self._copy_tgt):
                        shutil.rmtree(self._copy_tgt)
                    shutil.copytree(self._copy_src, self._copy_tgt)
                    self.output.emit("Copy completed.")
                except Exception as e:
                    self.output.emit(f"[Copy failed: {e}]")
                    self.finished.emit(1)
                    return

            # Run overlay command
            proc = subprocess.Popen(
                self._cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )
            for raw in proc.stdout:
                line = _ANSI_RE.sub("", raw).rstrip()
                m = _PROGRESS_RE.match(line)
                if m:
                    self.progress.emit(int(m.group(1)), int(m.group(2)))
                else:
                    if line:
                        self.output.emit(line)
            proc.wait()
            self.finished.emit(proc.returncode)
        except Exception as e:
            self.output.emit(f"[Error launching process: {e}]")
            self.finished.emit(1)


class ApplyDialog(QDialog):
    def __init__(self, config: MappingConfig, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Apply Mapping")
        self.setMinimumSize(720, 520)
        self._config = config
        self._worker: Optional[_SubprocessWorker] = None
        self._build()

    def _build_cmd(self, effective_target: str) -> List[str]:
        c = self._config
        cmd = [
            sys.executable, "mc_overlayer.py",
            c.overlay_dir, effective_target,
            "--seed", str(c.seed),
            "--scale", str(c.scale),
            "--alpha", str(c.alpha),
            "--overlay-scale", str(c.overlay_scale),
        ]
        if c.per_frame:
            cmd.append("--per-frame")
        if c.keep_aspect:
            cmd.append("--keep-aspect")
        if c.entity_regions_dir:
            cmd += [
                "--entity-regions", c.entity_regions_dir,
                "--entity-face-mode", c.entity_face_mode,
                "--entity-texture-mode", c.entity_texture_mode,
            ]
        if c.dir_config:
            cmd += ["--dir-config", c.dir_config]
        return cmd

    def _build(self):
        layout = QVBoxLayout(self)

        # Copy controls
        copy_grp = QGroupBox("Output Setup (Optional)")
        copy_lay = QVBoxLayout(copy_grp)

        self._copy_cb = QCheckBox("Copy texture images to output folder first")
        self._copy_cb.setChecked(self._config.copy_before_apply)
        self._copy_cb.stateChanged.connect(self._on_copy_toggled)
        self._copy_cb.stateChanged.connect(self._refresh_cmd)
        copy_lay.addWidget(self._copy_cb)

        # Texture images (read-only source)
        tex_row = QHBoxLayout()
        tex_row.addWidget(QLabel("Texture Images:"))
        self._tex_edit = QLineEdit(self._config.texture_dir)
        self._tex_edit.setReadOnly(True)
        tex_row.addWidget(self._tex_edit)
        copy_lay.addLayout(tex_row)

        # Output folder (editable destination)
        out_row = QHBoxLayout()
        out_row.addWidget(QLabel("Output Folder:"))
        self._out_edit = QLineEdit(self._config.output_dir or "")
        self._out_edit.setPlaceholderText("e.g., target/")
        self._out_edit.setEnabled(self._config.copy_before_apply)
        self._out_edit.textChanged.connect(self._refresh_cmd)
        out_row.addWidget(self._out_edit)
        self._out_btn = QPushButton("📁")
        self._out_btn.setMaximumWidth(26)
        self._out_btn.setEnabled(self._config.copy_before_apply)
        self._out_btn.clicked.connect(self._browse_output)
        out_row.addWidget(self._out_btn)
        copy_lay.addLayout(out_row)

        layout.addWidget(copy_grp)

        layout.addWidget(QLabel("Command:"))
        effective_target = (self._out_edit.text().strip() if self._copy_cb.isChecked()
                           else self._config.texture_dir)
        self._cmd_edit = QLineEdit(" ".join(self._build_cmd(effective_target)))
        self._cmd_edit.setReadOnly(True)
        self._cmd_edit.setFont(QFont("monospace", 9))
        layout.addWidget(self._cmd_edit)

        self._progress_bar = QProgressBar()
        self._progress_bar.setVisible(False)
        self._progress_bar.setTextVisible(True)
        layout.addWidget(self._progress_bar)

        layout.addWidget(QLabel("Output:"))
        self._log = QTextEdit()
        self._log.setReadOnly(True)
        self._log.setFont(QFont("monospace", 9))
        layout.addWidget(self._log, stretch=1)

        btns = QDialogButtonBox()
        self._run_btn  = btns.addButton("▶ Run",  QDialogButtonBox.ButtonRole.AcceptRole)
        self._stop_btn = btns.addButton("■ Stop", QDialogButtonBox.ButtonRole.DestructiveRole)
        self._close_btn = btns.addButton("Close", QDialogButtonBox.ButtonRole.RejectRole)
        self._stop_btn.setEnabled(False)
        self._run_btn.clicked.connect(self._run)
        self._stop_btn.clicked.connect(self._stop)
        self._close_btn.clicked.connect(self.reject)
        layout.addWidget(btns)

    def _on_copy_toggled(self):
        """Enable/disable output folder controls based on checkbox."""
        enabled = self._copy_cb.isChecked()
        self._out_edit.setEnabled(enabled)
        self._out_btn.setEnabled(enabled)

    def _refresh_cmd(self):
        """Update the command preview based on current UI state."""
        if self._copy_cb.isChecked():
            effective_target = self._out_edit.text().strip() or "<output_folder>"
        else:
            effective_target = self._config.texture_dir
        self._cmd_edit.setText(" ".join(self._build_cmd(effective_target)))

    def _browse_output(self):
        path = QFileDialog.getExistingDirectory(self, "Select Output Folder", self._out_edit.text())
        if path:
            self._out_edit.setText(path)

    def _run(self):
        self._log.clear()
        self._run_btn.setEnabled(False)
        self._stop_btn.setEnabled(True)

        # Compute effective target based on copy checkbox
        if self._copy_cb.isChecked():
            copy_src = self._config.texture_dir
            copy_tgt = self._out_edit.text().strip()
            effective_target = copy_tgt
        else:
            copy_src = None
            copy_tgt = None
            effective_target = self._config.texture_dir

        cmd = self._build_cmd(effective_target)
        self._cmd_edit.setText(" ".join(cmd))

        self._progress_bar.setValue(0)
        self._progress_bar.setVisible(True)
        self._worker = _SubprocessWorker(cmd, copy_src, copy_tgt)
        self._worker.output.connect(self._log.append)
        self._worker.progress.connect(self._on_progress)
        self._worker.finished.connect(self._on_finished)
        self._worker.start()

    def _stop(self):
        if self._worker and self._worker.isRunning():
            self._worker.terminate()

    def _on_progress(self, done: int, total: int):
        self._progress_bar.setMaximum(total)
        self._progress_bar.setValue(done)
        self._progress_bar.setFormat(f"{done}/{total} (%p%)")

    def _on_finished(self, code: int):
        self._run_btn.setEnabled(True)
        self._stop_btn.setEnabled(False)
        self._progress_bar.setVisible(False)
        self._log.append(f"\n[Exited with code {code}]")


# ---------------------------------------------------------------------------
# Overlay lookup dialog
# ---------------------------------------------------------------------------

class OverlayLookupDialog(QDialog):
    def __init__(self, assignments: List[TextureAssignment], parent=None):
        super().__init__(parent)
        self.setWindowTitle("Overlay Lookup")
        self.setMinimumSize(520, 400)
        self._assignments = assignments
        self._build()

    def _build(self):
        layout = QVBoxLayout(self)

        row = QHBoxLayout()
        row.addWidget(QLabel("Overlay filename:"))
        self._input = QLineEdit()
        self._input.setPlaceholderText("partial or full name, e.g. sakura.png")
        self._input.setFont(QFont("monospace", 9))
        self._input.returnPressed.connect(self._search)
        row.addWidget(self._input, stretch=1)
        self._exact_match = QCheckBox("Exact name")
        self._exact_match.setToolTip("Match exact filename instead of partial")
        row.addWidget(self._exact_match)
        btn = QPushButton("Search")
        btn.clicked.connect(self._search)
        row.addWidget(btn)
        layout.addLayout(row)

        self._result_label = QLabel("Enter an overlay filename and press Search.")
        layout.addWidget(self._result_label)

        self._results = QTextEdit()
        self._results.setReadOnly(True)
        self._results.setFont(QFont("monospace", 9))
        layout.addWidget(self._results, stretch=1)

        btns = QDialogButtonBox(QDialogButtonBox.StandardButton.Close)
        btns.rejected.connect(self.reject)
        layout.addWidget(btns)

    def _search(self):
        query = self._input.text().strip().lower()
        if not query:
            return

        exact_match = self._exact_match.isChecked()
        matches: list[str] = []

        for a in self._assignments:
            overlay_name = os.path.basename(a.overlay_path).lower()
            if exact_match:
                if query == overlay_name:
                    matches.append(a.target_path)
                    continue
            else:
                if query in overlay_name:
                    matches.append(a.target_path)
                    continue

            if a.face_overlays:
                for _region, ov_path in a.face_overlays:
                    face_overlay_name = os.path.basename(ov_path).lower()
                    if exact_match:
                        if query == face_overlay_name:
                            matches.append(a.target_path)
                            break
                    else:
                        if query in face_overlay_name:
                            matches.append(a.target_path)
                            break

        if matches:
            match_type = "exactly matching" if exact_match else "matching"
            self._result_label.setText(
                f"{len(matches)} texture(s) assigned to overlays {match_type} '{query}':"
            )
            self._results.setPlainText("\n".join(matches))
        else:
            match_type = "exactly matching" if exact_match else "matching"
            self._result_label.setText(f"No textures found {match_type} '{query}'.")
            self._results.clear()


# ---------------------------------------------------------------------------
# Seed search
# ---------------------------------------------------------------------------

class _SeedSearchWorker(QThread):
    progress = pyqtSignal(int)   # seeds tried so far
    found    = pyqtSignal(int)   # a matching seed value
    finished = pyqtSignal()

    def __init__(
        self,
        config: MappingConfig,
        constraints: "list[tuple[str, str]]",
        targets: List[str],
        overlays: List[str],
        max_tries: int,
    ):
        super().__init__()
        self._config      = config
        self._all_targets = targets
        self._overlays    = overlays
        self._max_tries   = max_tries
        self._stop_flag   = False

        # Precompute relative paths (forward-slash normalised) for fast matching
        tex_dir = config.texture_dir.rstrip("/\\")
        ov_dir  = config.overlay_dir.rstrip("/\\")
        self._ov_rel: "dict[str, str]" = {
            ov: os.path.relpath(ov, ov_dir).replace("\\", "/")
            for ov in overlays
        }

        # Pre-filter which targets each texture constraint matches (by rel path)
        self._resolved: "list[tuple[list[str], str]]" = []
        for tex_filter, ov_filter in constraints:
            tf = tex_filter.replace("\\", "/").lower()
            matching = (
                [t for t in targets
                 if tf in os.path.relpath(t, tex_dir).replace("\\", "/").lower()]
                if tf else list(targets)
            )
            self._resolved.append((matching, ov_filter.replace("\\", "/").lower()))

    def stop(self):
        self._stop_flag = True

    def run(self):
        rng   = random.SystemRandom()
        tried = 0
        while tried < self._max_tries and not self._stop_flag:
            seed = rng.randint(0, 2**32 - 1)
            if self._check(seed):
                self.found.emit(seed)
            tried += 1
            if tried % 500 == 0:
                self.progress.emit(tried)
        self.progress.emit(tried)
        self.finished.emit()

    def _check(self, seed: int) -> bool:
        c = self._config
        try:
            mapper = OverlayMapper(
                self._all_targets, self._overlays,
                seed=seed,
                per_frame=c.per_frame,
                target_dir=c.texture_dir,
                overlay_dir=c.overlay_dir,
            )
            for constrained_targets, ov_filter in self._resolved:
                if not any(
                    ov_filter in self._ov_rel.get(mapper.get_overlay(t), "").lower()
                    for t in constrained_targets
                ):
                    return False
            return True
        except Exception:
            return False


class _ConstraintRow(QWidget):
    """A single texture → overlay constraint row."""
    remove_requested = pyqtSignal(object)

    def __init__(self):
        super().__init__()
        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(4)
        lay.addWidget(QLabel("Texture:"))
        self.tex_edit = QLineEdit()
        self.tex_edit.setPlaceholderText("rel. path, e.g. assets/minecraft/textures/block/grass_block_top.png")
        lay.addWidget(self.tex_edit, stretch=1)
        lay.addWidget(QLabel("→  Overlay:"))
        self.ov_edit = QLineEdit()
        self.ov_edit.setPlaceholderText("rel. path, e.g. 00839-gelbooru_abc123.png")
        lay.addWidget(self.ov_edit, stretch=1)
        rm = QPushButton("✕")
        rm.setFixedWidth(24)
        rm.clicked.connect(lambda: self.remove_requested.emit(self))
        lay.addWidget(rm)


class SeedSearchDialog(QDialog):
    """Search for a seed that satisfies a list of texture→overlay constraints."""
    seed_selected = pyqtSignal(int)

    def __init__(
        self,
        config: MappingConfig,
        targets: List[str],
        overlays: List[str],
        parent=None,
    ):
        super().__init__(parent)
        self.setWindowTitle("Seed Search")
        self.setMinimumSize(660, 500)
        self._config   = config
        self._targets  = targets
        self._overlays = overlays
        self._worker: Optional[_SeedSearchWorker] = None
        self._rows: "list[_ConstraintRow]" = []
        self._build()

    def _build(self):
        layout = QVBoxLayout(self)

        # Constraints
        cgrp = QGroupBox("Texture → Overlay Constraints")
        cgrp_lay = QVBoxLayout(cgrp)
        cgrp_lay.setSpacing(3)

        self._rows_widget = QWidget()
        self._rows_layout = QVBoxLayout(self._rows_widget)
        self._rows_layout.setContentsMargins(0, 0, 0, 0)
        self._rows_layout.setSpacing(2)
        cgrp_lay.addWidget(self._rows_widget)

        add_btn = QPushButton("+ Add Constraint")
        add_btn.setMaximumWidth(130)
        add_btn.clicked.connect(self._add_row)
        cgrp_lay.addWidget(add_btn, alignment=Qt.AlignmentFlag.AlignLeft)
        layout.addWidget(cgrp)

        # Search controls
        ctrl = QHBoxLayout()
        ctrl.addWidget(QLabel("Max seeds:"))
        self._max_spin = QSpinBox()
        self._max_spin.setRange(100, 10_000_000)
        self._max_spin.setValue(100_000)
        self._max_spin.setSingleStep(10_000)
        self._max_spin.setFixedWidth(90)
        ctrl.addWidget(self._max_spin)
        ctrl.addStretch()
        self._search_btn = QPushButton("▶ Search")
        self._search_btn.clicked.connect(self._start)
        ctrl.addWidget(self._search_btn)
        self._stop_btn = QPushButton("■ Stop")
        self._stop_btn.setEnabled(False)
        self._stop_btn.clicked.connect(self._stop)
        ctrl.addWidget(self._stop_btn)
        layout.addLayout(ctrl)

        self._progress_lbl = QLabel("Seeds tried: 0")
        layout.addWidget(self._progress_lbl)

        # Results list
        rgrp = QGroupBox("Found Seeds")
        rgrp_lay = QVBoxLayout(rgrp)
        self._results_list = QListWidget()
        self._results_list.itemSelectionChanged.connect(
            lambda: self._apply_btn.setEnabled(bool(self._results_list.selectedItems()))
        )
        rgrp_lay.addWidget(self._results_list)
        layout.addWidget(rgrp, stretch=1)

        # Buttons
        btns = QDialogButtonBox()
        self._apply_btn = btns.addButton("Apply Selected Seed", QDialogButtonBox.ButtonRole.AcceptRole)
        self._apply_btn.setEnabled(False)
        self._apply_btn.clicked.connect(self._apply)
        close_btn = btns.addButton("Close", QDialogButtonBox.ButtonRole.RejectRole)
        close_btn.clicked.connect(self.reject)
        layout.addWidget(btns)

        self._add_row()

    def _add_row(self):
        row = _ConstraintRow()
        row.remove_requested.connect(self._remove_row)
        self._rows.append(row)
        self._rows_layout.addWidget(row)

    def _remove_row(self, row: "_ConstraintRow"):
        if len(self._rows) <= 1:
            return
        self._rows.remove(row)
        self._rows_layout.removeWidget(row)
        row.deleteLater()

    def _constraints(self) -> "list[tuple[str, str]]":
        return [(r.tex_edit.text().strip(), r.ov_edit.text().strip()) for r in self._rows]

    def _start(self):
        constraints = self._constraints()
        if not any(tf or ov for tf, ov in constraints):
            return
        self._results_list.clear()
        self._search_btn.setEnabled(False)
        self._stop_btn.setEnabled(True)
        self._progress_lbl.setText("Seeds tried: 0")
        self._worker = _SeedSearchWorker(
            self._config, constraints, self._targets, self._overlays,
            max_tries=self._max_spin.value(),
        )
        self._worker.progress.connect(lambda n: self._progress_lbl.setText(f"Seeds tried: {n:,}"))
        self._worker.found.connect(self._on_found)
        self._worker.finished.connect(self._on_finished)
        self._worker.start()

    def _stop(self):
        if self._worker:
            self._worker.stop()

    def _on_found(self, seed: int):
        self._results_list.addItem(QListWidgetItem(str(seed)))
        self._apply_btn.setEnabled(True)

    def _on_finished(self):
        self._search_btn.setEnabled(True)
        self._stop_btn.setEnabled(False)

    def _apply(self):
        items = self._results_list.selectedItems()
        if not items:
            return
        self.seed_selected.emit(int(items[0].text()))
        self.accept()

    def closeEvent(self, event):
        if self._worker and self._worker.isRunning():
            self._worker.stop()
            self._worker.wait()
        super().closeEvent(event)


# ---------------------------------------------------------------------------
# Main window
# ---------------------------------------------------------------------------

class PreviewWindow(QMainWindow):
    def __init__(self, config: MappingConfig):
        super().__init__()
        self.setWindowTitle("Minecraft Overlayer")
        self.resize(1300, 760)

        self._config = config
        self._all_assignments: List[TextureAssignment] = []
        self._shown_assignments: List[TextureAssignment] = []
        self._cards: List[AssignmentCard] = []
        self._loader: Optional[ThumbnailLoader] = None
        self._n_overlays = 0
        self._n_targets  = 0
        self._overlays_list: List[str] = []
        self._targets_list: List[str] = []

        self._build_ui()

        # Keyboard shortcuts
        QShortcut(QKeySequence("R"), self, self._randomize)
        QShortcut(QKeySequence("Space"), self, self._randomize)

        self._rebuild()

    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(8, 8, 8, 4)
        root.setSpacing(6)

        root.addWidget(self._make_toolbar())

        # Main content with splitter
        splitter = QSplitter(Qt.Orientation.Horizontal)

        # Left panel
        self._config_panel = ConfigPanel(self._config)
        self._config_panel.config_changed.connect(self._on_config_changed)
        self._config_panel.setMinimumWidth(220)
        splitter.addWidget(self._config_panel)

        # Right panel with cards
        self._scroll = QScrollArea()
        self._scroll.setWidgetResizable(True)
        self._scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)

        self._flow_widget = QWidget()
        self._flow_layout = FlowLayout(self._flow_widget)
        self._flow_widget.setLayout(self._flow_layout)
        self._scroll.setWidget(self._flow_widget)
        splitter.addWidget(self._scroll)

        splitter.setStretchFactor(0, 0)
        splitter.setStretchFactor(1, 1)

        root.addWidget(splitter, stretch=1)

        self._status = QStatusBar()
        self.setStatusBar(self._status)

    def _make_toolbar(self) -> QWidget:
        bar = QWidget()
        bar.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        lay = QHBoxLayout(bar)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(6)

        # Seed
        lay.addWidget(QLabel("Seed:"))
        self._seed_input = QLineEdit()
        self._seed_input.setFixedWidth(130)
        self._seed_input.setPlaceholderText("integer or string")
        self._seed_input.returnPressed.connect(self._on_seed_entered)
        lay.addWidget(self._seed_input)

        rand_btn = QPushButton("Randomize")
        rand_btn.setMaximumWidth(90)
        rand_btn.setToolTip("Pick a random seed  [R / Space]")
        rand_btn.clicked.connect(self._randomize)
        lay.addWidget(rand_btn)

        copy_btn = QPushButton("Copy")
        copy_btn.setMaximumWidth(50)
        copy_btn.setToolTip("Copy seed to clipboard")
        copy_btn.clicked.connect(self._copy_seed)
        lay.addWidget(copy_btn)

        _add_separator(lay)

        # Filter
        lay.addWidget(QLabel("Filter:"))
        self._filter_input = QLineEdit()
        self._filter_input.setFixedWidth(120)
        self._filter_input.setPlaceholderText("filename…")
        self._filter_input.textChanged.connect(self._apply_filter)
        lay.addWidget(self._filter_input)

        lay.addWidget(QLabel("Type:"))
        self._type_combo = QComboBox()
        self._type_combo.addItems(["All", "Entity", "Regular"])
        self._type_combo.currentTextChanged.connect(self._apply_filter)
        lay.addWidget(self._type_combo)

        lay.addWidget(QLabel("Max:"))
        self._max_spin = QSpinBox()
        self._max_spin.setRange(1, 9999)
        self._max_spin.setValue(DEFAULT_MAX)
        self._max_spin.setFixedWidth(65)
        self._max_spin.valueChanged.connect(self._apply_filter)
        lay.addWidget(self._max_spin)

        _add_separator(lay)

        lookup_btn = QPushButton("Overlay")
        lookup_btn.setMaximumWidth(80)
        lookup_btn.setToolTip("Find which textures are assigned to an overlay")
        lookup_btn.clicked.connect(self._open_overlay_lookup)
        lay.addWidget(lookup_btn)

        search_btn = QPushButton("Seed Search")
        search_btn.setMaximumWidth(100)
        search_btn.setToolTip("Search for a seed that assigns specific overlays to specific textures")
        search_btn.clicked.connect(self._open_seed_search)
        lay.addWidget(search_btn)

        _add_separator(lay)

        apply_btn = QPushButton("Apply")
        apply_btn.setMaximumWidth(70)
        apply_btn.setToolTip("Open the apply dialog to run mc_overlayer.py")
        apply_btn.clicked.connect(self._open_apply_dialog)
        lay.addWidget(apply_btn)

        lay.addStretch()
        return bar

    def _rebuild(self):
        """Recompute all assignments from the current config."""
        self._status.showMessage("Building mapping…")
        QApplication.processEvents()
        try:
            assignments, overlays, targets = build_assignments(self._config)
        except Exception as e:
            self._status.showMessage(f"Error: {e}")
            return
        self._all_assignments = assignments
        self._n_overlays = len(overlays)
        self._n_targets  = len(targets)
        self._overlays_list = overlays
        self._targets_list  = targets
        self._apply_filter()
        self._seed_input.setText(str(self._config.seed))

    def _apply_filter(self):
        text        = self._filter_input.text().lower()
        type_filter = self._type_combo.currentText()
        max_items   = self._max_spin.value()

        filtered = [
            a for a in self._all_assignments
            if (type_filter == "All"
                or (type_filter == "Entity"  and a.is_entity)
                or (type_filter == "Regular" and not a.is_entity))
            and (not text or text in os.path.basename(a.target_path).lower())
        ]

        filtered.sort(key=lambda a: _priority_score(a.target_path))

        self._shown_assignments = filtered[:max_items]
        self._render_cards()

    def _render_cards(self):
        if self._loader and self._loader.isRunning():
            self._loader.cancel()
            self._loader.wait()

        while self._flow_layout.count():
            item = self._flow_layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()
        self._cards = []

        for a in self._shown_assignments:
            card = AssignmentCard(a)
            self._flow_layout.addWidget(card)
            self._cards.append(card)

        n_entity  = sum(1 for a in self._shown_assignments if a.is_entity)
        n_regular = len(self._shown_assignments) - n_entity
        self._status.showMessage(
            f"Showing {len(self._shown_assignments)} of {len(self._all_assignments)} texture images"
            f"  ({n_entity} entity, {n_regular} regular)"
            f"  │  seed = {self._config.seed}"
            f"  │  {self._n_overlays} overlay images, {self._n_targets} texture images"
        )

        if self._shown_assignments:
            self._loader = ThumbnailLoader(
                self._shown_assignments,
                alpha=self._config.alpha,
                keep_aspect=self._config.keep_aspect,
                overlay_scale=self._config.overlay_scale,
            )
            self._loader.loaded.connect(self._on_thumbnail_ready)
            self._loader.start()

    def _on_thumbnail_ready(self, index: int, qi: QImage):
        if 0 <= index < len(self._cards):
            self._cards[index].set_image(qi)

    def _on_seed_entered(self):
        text = self._seed_input.text().strip()
        if not text:
            return
        self._config = self._config.with_seed(text)
        self._config_panel.set_seed(self._config.seed)
        self._rebuild()
        self._save_config()

    def _randomize(self):
        self._config = self._config.with_random_seed()
        self._config_panel.set_seed(self._config.seed)
        self._seed_input.setText(str(self._config.seed))
        self._rebuild()
        self._save_config()

    def _copy_seed(self):
        QApplication.clipboard().setText(str(self._config.seed))
        self._status.showMessage(f"Copied seed {self._config.seed} to clipboard", 2000)

    def _on_config_changed(self, config: MappingConfig):
        """Handle config panel Update Preview."""
        self._config = config
        self._rebuild()
        self._save_config()

    def _open_overlay_lookup(self):
        OverlayLookupDialog(self._all_assignments, self).exec()

    def _open_seed_search(self):
        if not self._targets_list or not self._overlays_list:
            return
        dlg = SeedSearchDialog(self._config, self._targets_list, self._overlays_list, self)
        dlg.seed_selected.connect(self._apply_seed)
        dlg.exec()

    def _apply_seed(self, seed: int):
        self._config = self._config.with_seed(seed)
        self._config_panel.set_seed(self._config.seed)
        self._seed_input.setText(str(self._config.seed))
        self._rebuild()
        self._save_config()

    def _open_apply_dialog(self):
        ApplyDialog(self._config, self).exec()

    def _save_config(self):
        """Persist config to last_run.json."""
        try:
            with open("last_run.json", "w") as f:
                json.dump(self._config.to_last_run_dict(), f, indent=2)
        except Exception:
            pass

    def closeEvent(self, event):
        if self._loader and self._loader.isRunning():
            self._loader.cancel()
            self._loader.wait()
        super().closeEvent(event)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _add_separator(layout: QHBoxLayout):
    line = QFrame()
    line.setFrameShape(QFrame.Shape.VLine)
    line.setFrameShadow(QFrame.Shadow.Sunken)
    layout.addWidget(line)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="MCOverlayer seed preview")
    parser.add_argument(
        "--config", default="last_run.json",
        help="Path to a last_run.json config file (default: last_run.json)",
    )
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    os.chdir(script_dir)

    try:
        config = MappingConfig.from_last_run(args.config)
    except FileNotFoundError:
        print(
            f"Config not found: {args.config!r}\n"
            "Run mc_overlayer.py first to generate last_run.json.",
            file=sys.stderr,
        )
        sys.exit(1)

    app = QApplication(sys.argv)

    win = PreviewWindow(config)
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
