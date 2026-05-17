#!/usr/bin/env python3
"""
Minecraft Texture Region Editor - PyQt6 GUI for defining named rectangular regions on textures.
Supports multiple textures sharing the same UV set, outputs to JSON.
"""

import json
import sys
from pathlib import Path
from typing import Optional
from dataclasses import dataclass, asdict

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QPushButton, QListWidget, QListWidgetItem,
    QSpinBox, QFileDialog, QMessageBox, QSplitter, QGroupBox,
    QGraphicsView, QGraphicsScene, QGraphicsRectItem,
    QGraphicsTextItem, QGraphicsItem, QInputDialog, QComboBox
)
from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtGui import QPixmap, QPen, QColor, QFont, QTransform


class ConstantWidthRectItem(QGraphicsRectItem):
    """Rectangle item with a cosmetic (screen-space constant width) pen."""

    def __init__(self, x, y, w, h, name, parent=None):
        super().__init__(x, y, w, h, parent)
        self.name = name
        pen = QPen(QColor(0, 255, 0), 1)
        pen.setCosmetic(True)
        self.setPen(pen)


@dataclass
class TextureRegion:
    """Represents a named rectangular region on a texture."""
    name: str
    x: int
    y: int
    width: int
    height: int
    flip: Optional[str] = None  # None, "v", "h", "hv", or "vh"

    def to_dict(self):
        d = asdict(self)
        # Don't include flip if it's None to keep JSON clean
        if d['flip'] is None:
            del d['flip']
        return d


@dataclass
class RegionSet:
    """Represents a set of regions shared by multiple textures."""
    name: str
    texture_size: tuple  # (width, height)
    regions: list  # List of TextureRegion
    textures: list  # List of relative paths

    def to_dict(self):
        return {
            "texture_size": list(self.texture_size),
            "regions": [r.to_dict() for r in self.regions],
            "textures": self.textures
        }


class TextureCanvas(QGraphicsView):
    """Canvas for displaying and editing texture regions with zoom support."""

    region_created = pyqtSignal(TextureRegion)
    zoom_changed = pyqtSignal(float)

    def __init__(self):
        super().__init__()
        self.scene = QGraphicsScene()
        self.setScene(self.scene)
        self.pixmap_item = None
        self.current_pixmap = None
        self.regions = []
        self.drawing = False
        self.start_pos = None
        self.temp_rect = None
        self.zoom_level = 1.0
        self.min_zoom = 0.1
        self.max_zoom = 10.0

        # Configure view for pixel-perfect rendering
        self.setOptimizationFlags(QGraphicsView.OptimizationFlag.DontSavePainterState)
        self.setDragMode(QGraphicsView.DragMode.NoDrag)
        self._panning = False
        self._pan_start = None

    def load_image(self, image_path: str):
        """Load and display an image."""
        pixmap = QPixmap(image_path)
        if pixmap.isNull():
            return False

        self.current_pixmap = pixmap
        self.scene.clear()
        self.pixmap_item = self.scene.addPixmap(pixmap)
        self.zoom_level = 1.0
        self.fit_image_in_view()
        self.redraw_regions()
        return True

    def fit_image_in_view(self):
        """Fit the image to the view size."""
        if self.pixmap_item:
            self.fitInView(self.pixmap_item, Qt.AspectRatioMode.KeepAspectRatio)
            # Get actual scale factor from transform
            transform = self.transform()
            self.zoom_level = transform.m11() if transform.m11() > 0 else 1.0
            self.zoom_changed.emit(self.zoom_level)

    def set_regions(self, regions: list):
        """Update the displayed regions."""
        self.regions = regions
        self.redraw_regions()

    def redraw_regions(self):
        """Redraw all regions on the canvas."""
        if not self.pixmap_item:
            return

        # Remove old rectangle items and text
        items_to_remove = []
        for item in self.scene.items():
            if isinstance(item, (ConstantWidthRectItem, QGraphicsTextItem)):
                items_to_remove.append(item)

        for item in items_to_remove:
            self.scene.removeItem(item)

        # Draw regions with cosmetic (constant screen-width) pen
        for region in self.regions:
            rect = ConstantWidthRectItem(region.x, region.y, region.width, region.height, region.name)
            self.scene.addItem(rect)

            # Add label — ignore view transform so it stays the same size at all zoom levels
            text_item = self.scene.addText(region.name)
            text_item.setPos(region.x + 2, region.y + 2)
            text_item.setDefaultTextColor(QColor(0, 255, 0))
            text_item.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIgnoresTransformations)
            font = QFont()
            font.setPointSize(8)
            text_item.setFont(font)

    def wheelEvent(self, event):
        """Handle mouse wheel for zooming."""
        if event.modifiers() == Qt.KeyboardModifier.ControlModifier:
            # Zoom in/out
            delta = event.angleDelta().y()
            zoom_factor = 1.1 if delta > 0 else 0.9
            self.set_zoom(self.zoom_level * zoom_factor)
        else:
            # Default scroll behavior
            super().wheelEvent(event)

    def set_zoom(self, new_zoom: float):
        """Set zoom level."""
        self.zoom_level = max(self.min_zoom, min(new_zoom, self.max_zoom))
        transform = QTransform()
        transform.scale(self.zoom_level, self.zoom_level)
        self.setTransform(transform)
        self.zoom_changed.emit(self.zoom_level)

    def zoom_in(self):
        """Zoom in."""
        self.set_zoom(self.zoom_level * 1.2)

    def zoom_out(self):
        """Zoom out."""
        self.set_zoom(self.zoom_level / 1.2)

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.MiddleButton:
            self._panning = True
            self._pan_start = event.pos()
            self.setCursor(Qt.CursorShape.ClosedHandCursor)
        elif event.button() == Qt.MouseButton.LeftButton:
            self.drawing = True
            pos = self.mapToScene(event.pos())
            self.start_pos = (int(pos.x()), int(pos.y()))
        else:
            super().mousePressEvent(event)

    def mouseMoveEvent(self, event):
        if self._panning and self._pan_start is not None:
            delta = event.pos() - self._pan_start
            self._pan_start = event.pos()
            self.horizontalScrollBar().setValue(self.horizontalScrollBar().value() - delta.x())
            self.verticalScrollBar().setValue(self.verticalScrollBar().value() - delta.y())
            return
        if self.drawing and self.start_pos:
            pos = self.mapToScene(event.pos())
            end_pos = (int(pos.x()), int(pos.y()))

            x1, y1 = self.start_pos
            x2, y2 = end_pos

            x = min(x1, x2)
            y = min(y1, y2)
            w = abs(x2 - x1)
            h = abs(y2 - y1)

            if self.temp_rect:
                self.scene.removeItem(self.temp_rect)

            self.temp_rect = self.scene.addRect(x, y, w, h)
            pen = QPen(QColor(255, 255, 0), 1)
            pen.setCosmetic(True)
            self.temp_rect.setPen(pen)

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.MouseButton.MiddleButton:
            self._panning = False
            self._pan_start = None
            self.setCursor(Qt.CursorShape.ArrowCursor)
            return
        if event.button() == Qt.MouseButton.LeftButton and self.drawing:
            self.drawing = False

            if self.start_pos:
                pos = self.mapToScene(event.pos())
                end_pos = (int(pos.x()), int(pos.y()))

                x1, y1 = self.start_pos
                x2, y2 = end_pos

                x = min(x1, x2)
                y = min(y1, y2)
                w = abs(x2 - x1)
                h = abs(y2 - y1)

                # Remove temporary rectangle
                if self.temp_rect:
                    self.scene.removeItem(self.temp_rect)
                    self.temp_rect = None

                # Create region if large enough
                if w > 3 and h > 3:  # Minimum size to avoid accidental clicks
                    region = TextureRegion(name=f"region_{len(self.regions)}", x=x, y=y, width=w, height=h)
                    self.region_created.emit(region)

            self.start_pos = None


class TextureRegionEditor(QMainWindow):
    """Main application window."""

    def __init__(self):
        super().__init__()
        self.setWindowTitle("Minecraft Texture Region Editor")
        self.setGeometry(100, 100, 1200, 800)

        self.current_folder = None
        self.region_sets = {}  # Dict of {name: RegionSet}
        self.current_region_set_name = None

        self.init_ui()

    def init_ui(self):
        """Initialize the UI."""
        # Main widget and layout
        main_widget = QWidget()
        self.setCentralWidget(main_widget)
        main_layout = QVBoxLayout(main_widget)
        main_layout.setContentsMargins(5, 5, 5, 5)
        main_layout.setSpacing(5)

        # Create canvas early so toolbar can reference it
        self.canvas = TextureCanvas()
        self.canvas.region_created.connect(self.on_region_created)

        # Top toolbar
        toolbar = QHBoxLayout()

        # Folder selection
        toolbar.addWidget(QLabel("Folder:"))
        self.folder_label = QLabel("No folder selected")
        self.folder_label.setMinimumWidth(200)
        toolbar.addWidget(self.folder_label)
        folder_btn = QPushButton("Browse")
        folder_btn.clicked.connect(self.select_folder)
        toolbar.addWidget(folder_btn)

        toolbar.addSpacing(20)

        # Region set management
        toolbar.addWidget(QLabel("Region Sets:"))
        self.region_set_combo = QComboBox()
        self.region_set_combo.currentTextChanged.connect(self.on_region_set_changed)
        self.region_set_combo.setMaximumWidth(150)
        toolbar.addWidget(self.region_set_combo)

        new_set_btn = QPushButton("New")
        new_set_btn.setMaximumWidth(60)
        new_set_btn.clicked.connect(self.create_region_set)
        toolbar.addWidget(new_set_btn)

        delete_set_btn = QPushButton("Delete")
        delete_set_btn.setMaximumWidth(60)
        delete_set_btn.clicked.connect(self.delete_region_set)
        toolbar.addWidget(delete_set_btn)

        toolbar.addSpacing(20)

        # Texture size controls
        toolbar.addWidget(QLabel("Size:"))
        self.texture_width = QSpinBox()
        self.texture_width.setMaximum(2048)
        self.texture_width.setValue(64)
        self.texture_width.setMaximumWidth(70)
        self.texture_width.valueChanged.connect(self.on_texture_size_changed)
        toolbar.addWidget(self.texture_width)

        self.texture_height = QSpinBox()
        self.texture_height.setMaximum(2048)
        self.texture_height.setValue(64)
        self.texture_height.setMaximumWidth(70)
        self.texture_height.valueChanged.connect(self.on_texture_size_changed)
        toolbar.addWidget(self.texture_height)

        toolbar.addSpacing(20)

        # Zoom controls
        toolbar.addWidget(QLabel("Zoom:"))
        zoom_in_btn = QPushButton("🔍+")
        zoom_in_btn.setMaximumWidth(50)
        zoom_in_btn.clicked.connect(self.canvas.zoom_in)
        toolbar.addWidget(zoom_in_btn)

        zoom_out_btn = QPushButton("🔍−")
        zoom_out_btn.setMaximumWidth(50)
        zoom_out_btn.clicked.connect(self.canvas.zoom_out)
        toolbar.addWidget(zoom_out_btn)

        fit_btn = QPushButton("Fit")
        fit_btn.setMaximumWidth(50)
        fit_btn.clicked.connect(self.canvas.fit_image_in_view)
        toolbar.addWidget(fit_btn)

        self.zoom_label = QLabel("100%")
        self.zoom_label.setMinimumWidth(50)
        self.canvas.zoom_changed.connect(self.update_zoom_label)
        toolbar.addWidget(self.zoom_label)

        toolbar.addStretch()

        # File operations
        load_btn = QPushButton("Load JSON")
        load_btn.clicked.connect(self.load_json)
        toolbar.addWidget(load_btn)

        save_btn = QPushButton("Save JSON")
        save_btn.clicked.connect(self.save_json)
        toolbar.addWidget(save_btn)

        new_json_btn = QPushButton("New JSON")
        new_json_btn.clicked.connect(self.new_json)
        toolbar.addWidget(new_json_btn)

        main_layout.addLayout(toolbar)

        # Main content - splitter with canvas and right panel
        splitter = QSplitter(Qt.Orientation.Horizontal)

        # Canvas takes up most of the space
        splitter.addWidget(self.canvas)

        # Right panel - regions and textures
        right_panel = QVBoxLayout()

        # Texture list
        textures_group = QGroupBox("Textures in Region Set")
        textures_layout = QVBoxLayout()
        self.texture_list = QListWidget()
        self.texture_list.itemSelectionChanged.connect(self.on_texture_selected)
        textures_layout.addWidget(self.texture_list)
        add_tex_btn = QPushButton("Add Texture File...")
        add_tex_btn.clicked.connect(self.add_texture_file)
        textures_layout.addWidget(add_tex_btn)
        remove_tex_btn = QPushButton("Remove Selected")
        remove_tex_btn.clicked.connect(self.remove_texture_file)
        textures_layout.addWidget(remove_tex_btn)
        textures_group.setLayout(textures_layout)
        right_panel.addWidget(textures_group)

        # Regions list
        regions_group = QGroupBox("Defined Regions")
        regions_layout = QVBoxLayout()
        self.regions_list = QListWidget()
        self.regions_list.itemSelectionChanged.connect(self.on_region_selected)
        regions_layout.addWidget(self.regions_list)

        # Flip controls for selected region
        flip_layout = QHBoxLayout()
        flip_layout.addWidget(QLabel("Flip:"))
        self.flip_combo = QComboBox()
        self.flip_combo.addItems(["None", "Vertical (v)", "Horizontal (h)", "Both (hv)"])
        self.flip_combo.currentTextChanged.connect(self.on_flip_changed)
        self.flip_combo.setEnabled(False)
        flip_layout.addWidget(self.flip_combo)
        regions_layout.addLayout(flip_layout)

        regions_buttons_layout = QHBoxLayout()
        rename_btn = QPushButton("Rename")
        rename_btn.clicked.connect(self.rename_region)
        delete_btn = QPushButton("Delete")
        delete_btn.clicked.connect(self.delete_region)
        regions_buttons_layout.addWidget(rename_btn)
        regions_buttons_layout.addWidget(delete_btn)
        regions_layout.addLayout(regions_buttons_layout)

        regions_group.setLayout(regions_layout)
        right_panel.addWidget(regions_group)

        # Right panel container
        right_container = QWidget()
        right_container.setLayout(right_panel)
        right_container.setMaximumWidth(300)

        splitter.addWidget(right_container)
        splitter.setStretchFactor(0, 3)
        splitter.setStretchFactor(1, 1)

        main_layout.addWidget(splitter)

    def select_folder(self):
        """Select the working folder."""
        folder = QFileDialog.getExistingDirectory(self, "Select Working Folder")
        if folder:
            self.current_folder = Path(folder)
            display_path = str(self.current_folder)
            if len(display_path) > 40:
                display_path = "..." + display_path[-37:]
            self.folder_label.setText(display_path)

    def create_region_set(self):
        """Create a new region set."""
        name, ok = QInputDialog.getText(
            self, "New Region Set", "Enter name for new region set:"
        )
        if ok and name:
            if name in self.region_sets:
                QMessageBox.warning(self, "Duplicate", f"Region set '{name}' already exists")
                return

            # Create new region set
            region_set = RegionSet(
                name=name,
                texture_size=(64, 64),
                regions=[],
                textures=[]
            )
            self.region_sets[name] = region_set

            # Update combo box
            self.region_set_combo.blockSignals(True)
            self.region_set_combo.addItem(name)
            self.region_set_combo.setCurrentText(name)
            self.region_set_combo.blockSignals(False)
            self.on_region_set_changed(name)

    def delete_region_set(self):
        """Delete the current region set."""
        if not self.current_region_set_name:
            QMessageBox.warning(self, "No Selection", "No region set selected")
            return

        reply = QMessageBox.question(
            self, "Confirm Delete",
            f"Delete region set '{self.current_region_set_name}'?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        if reply == QMessageBox.StandardButton.Yes:
            del self.region_sets[self.current_region_set_name]
            self.region_set_combo.blockSignals(True)
            idx = self.region_set_combo.currentIndex()
            self.region_set_combo.removeItem(idx)
            self.region_set_combo.blockSignals(False)
            if self.region_set_combo.count() > 0:
                self.region_set_combo.setCurrentIndex(0)
            self.on_region_set_changed(self.region_set_combo.currentText() if self.region_set_combo.count() > 0 else "")

    def on_region_set_changed(self, name: str):
        """Load a region set for editing."""
        if not name or name not in self.region_sets:
            self.current_region_set_name = None
            self.canvas.regions = []
            self.canvas.redraw_regions()
            self.texture_list.clear()
            self.regions_list.clear()
            return

        self.current_region_set_name = name
        region_set = self.region_sets[name]

        # Update texture size controls
        self.texture_width.blockSignals(True)
        self.texture_height.blockSignals(True)
        self.texture_width.setValue(region_set.texture_size[0])
        self.texture_height.setValue(region_set.texture_size[1])
        self.texture_width.blockSignals(False)
        self.texture_height.blockSignals(False)

        # Load canvas and lists
        self.canvas.regions = region_set.regions.copy()
        self.canvas.redraw_regions()

        self.texture_list.clear()
        for tex_path in region_set.textures:
            item = QListWidgetItem(tex_path)
            self.texture_list.addItem(item)

        self.update_regions_list()

        # Load first texture image
        if region_set.textures and self.current_folder:
            first_tex = self.current_folder / region_set.textures[0]
            if first_tex.exists():
                self.canvas.load_image(str(first_tex))

    def on_texture_selected(self):
        """Load the selected texture on canvas."""
        if not self.current_folder or not self.current_region_set_name:
            return

        items = self.texture_list.selectedItems()
        if not items:
            return

        tex_path = items[0].text()
        full_path = self.current_folder / tex_path
        if full_path.exists():
            self.canvas.load_image(str(full_path))

    def on_texture_size_changed(self):
        """Update the current region set's texture size."""
        if self.current_region_set_name and self.current_region_set_name in self.region_sets:
            region_set = self.region_sets[self.current_region_set_name]
            region_set.texture_size = (self.texture_width.value(), self.texture_height.value())

    def add_texture_file(self):
        """Add a texture file via file picker."""
        if not self.current_folder:
            QMessageBox.warning(self, "No Folder", "Select a working folder first")
            return

        if not self.current_region_set_name:
            QMessageBox.warning(self, "No Region Set", "Create or select a region set first")
            return

        file_paths, _ = QFileDialog.getOpenFileNames(
            self, "Select Texture Files", str(self.current_folder),
            "Image Files (*.png *.jpg *.jpeg)"
        )

        for file_path in file_paths:
            try:
                full_path = Path(file_path)
                rel_path = full_path.relative_to(self.current_folder)
                rel_path_str = str(rel_path)

                region_set = self.region_sets[self.current_region_set_name]

                # Check if already in list
                if rel_path_str in region_set.textures:
                    continue

                # Load the image to verify it's valid
                pixmap = QPixmap(file_path)
                if not pixmap.isNull():
                    region_set.textures.append(rel_path_str)
                    item = QListWidgetItem(rel_path_str)
                    self.texture_list.addItem(item)
                    # Load first image automatically
                    if len(region_set.textures) == 1:
                        self.canvas.load_image(file_path)
            except ValueError:
                QMessageBox.warning(self, "Invalid Path", f"File must be in working folder: {file_path}")

    def remove_texture_file(self):
        """Remove selected texture from list."""
        if not self.current_region_set_name:
            return

        items = self.texture_list.selectedItems()
        if not items:
            return

        region_set = self.region_sets[self.current_region_set_name]
        for item in items:
            text = item.text()
            if text in region_set.textures:
                region_set.textures.remove(text)
            self.texture_list.takeItem(self.texture_list.row(item))

    def update_zoom_label(self, zoom: float):
        """Update the zoom level display."""
        self.zoom_label.setText(f"{zoom*100:.0f}%")

    def on_region_created(self, region: TextureRegion):
        """Handle new region creation."""
        if not self.current_region_set_name:
            QMessageBox.warning(self, "No Region Set", "Create or select a region set first")
            return

        name, ok = QInputDialog.getText(
            self, "Region Name", "Enter a name for this region:",
            text=region.name
        )

        if ok and name:
            region.name = name
            # Update both canvas and region set
            self.canvas.regions.append(region)
            region_set = self.region_sets[self.current_region_set_name]
            region_set.regions.append(region)
            self.canvas.redraw_regions()
            self.update_regions_list()

    def on_region_selected(self):
        """Handle region selection."""
        items = self.regions_list.selectedItems()
        if items:
            idx = self.regions_list.row(items[0])
            if 0 <= idx < len(self.canvas.regions):
                region = self.canvas.regions[idx]
                # Update flip combo
                self.flip_combo.blockSignals(True)
                if region.flip is None:
                    self.flip_combo.setCurrentIndex(0)
                elif region.flip == "v":
                    self.flip_combo.setCurrentIndex(1)
                elif region.flip == "h":
                    self.flip_combo.setCurrentIndex(2)
                elif region.flip in ("hv", "vh"):
                    self.flip_combo.setCurrentIndex(3)
                self.flip_combo.setEnabled(True)
                self.flip_combo.blockSignals(False)
        else:
            self.flip_combo.setEnabled(False)

    def rename_region(self):
        """Rename the selected region."""
        if not self.current_region_set_name:
            return

        items = self.regions_list.selectedItems()
        if not items:
            QMessageBox.warning(self, "No Selection", "Select a region to rename")
            return

        idx = self.regions_list.row(items[0])
        if 0 <= idx < len(self.canvas.regions):
            region = self.canvas.regions[idx]
            new_name, ok = QInputDialog.getText(
                self, "Rename Region", "Enter new name:",
                text=region.name
            )
            if ok and new_name:
                region.name = new_name
                # Update region set too
                region_set = self.region_sets[self.current_region_set_name]
                if 0 <= idx < len(region_set.regions):
                    region_set.regions[idx].name = new_name
                self.canvas.redraw_regions()
                self.update_regions_list()

    def delete_region(self):
        """Delete the selected region."""
        if not self.current_region_set_name:
            return

        items = self.regions_list.selectedItems()
        if not items:
            QMessageBox.warning(self, "No Selection", "Select a region to delete")
            return

        idx = self.regions_list.row(items[0])
        if 0 <= idx < len(self.canvas.regions):
            self.canvas.regions.pop(idx)
            region_set = self.region_sets[self.current_region_set_name]
            if 0 <= idx < len(region_set.regions):
                region_set.regions.pop(idx)
            self.canvas.redraw_regions()
            self.update_regions_list()

    def on_flip_changed(self, text: str):
        """Handle flip value change."""
        items = self.regions_list.selectedItems()
        if not items:
            return

        idx = self.regions_list.row(items[0])
        if 0 <= idx < len(self.canvas.regions):
            region = self.canvas.regions[idx]
            # Map combo text to flip value
            flip_map = {
                "None": None,
                "Vertical (v)": "v",
                "Horizontal (h)": "h",
                "Both (hv)": "hv"
            }
            region.flip = flip_map.get(text)

            # Update region set too
            if self.current_region_set_name and idx < len(self.region_sets[self.current_region_set_name].regions):
                self.region_sets[self.current_region_set_name].regions[idx].flip = region.flip

            self.update_regions_list()

    def update_regions_list(self):
        """Refresh the regions list display."""
        self.regions_list.clear()
        for region in self.canvas.regions:
            flip_str = f" [{region.flip}]" if region.flip else ""
            item = QListWidgetItem(
                f"{region.name} ({region.x},{region.y}) {region.width}x{region.height}{flip_str}"
            )
            self.regions_list.addItem(item)

    def save_json(self):
        """Save all region sets to JSON."""
        if not self.region_sets:
            QMessageBox.warning(self, "No Region Sets", "Create at least one region set")
            return

        # Save to file
        file_path, _ = QFileDialog.getSaveFileName(
            self, "Save JSON", "", "JSON Files (*.json)"
        )

        if file_path:
            data = {
                name: region_set.to_dict()
                for name, region_set in self.region_sets.items()
            }

            try:
                with open(file_path, 'w') as f:
                    json.dump(data, f, indent=2)
                QMessageBox.information(self, "Success", f"Saved {len(self.region_sets)} region set(s) to {file_path}")
            except Exception as e:
                QMessageBox.critical(self, "Error", f"Failed to save: {e}")

    def load_json(self):
        """Load region sets from JSON."""
        file_path, _ = QFileDialog.getOpenFileName(
            self, "Load JSON", "", "JSON Files (*.json)"
        )

        if not file_path:
            return

        try:
            with open(file_path, 'r') as f:
                data = json.load(f)

            # Clear existing region sets
            self.region_sets.clear()
            self.region_set_combo.blockSignals(True)
            self.region_set_combo.clear()
            self.region_set_combo.blockSignals(False)

            # Load all region sets
            for name, region_data in data.items():
                width, height = region_data['texture_size']
                regions = [TextureRegion(**r) for r in region_data['regions']]
                textures = region_data['textures']

                region_set = RegionSet(
                    name=name,
                    texture_size=(width, height),
                    regions=regions,
                    textures=textures
                )
                self.region_sets[name] = region_set

                # Add to combo box
                self.region_set_combo.blockSignals(True)
                self.region_set_combo.addItem(name)
                self.region_set_combo.blockSignals(False)

            # Load the first region set
            if self.region_sets:
                first_name = next(iter(self.region_sets.keys()))
                self.region_set_combo.blockSignals(True)
                self.region_set_combo.setCurrentText(first_name)
                self.region_set_combo.blockSignals(False)
                self.on_region_set_changed(first_name)

            QMessageBox.information(self, "Success", f"Loaded {len(self.region_sets)} region set(s) from {file_path}")
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to load: {e}")

    def new_json(self):
        """Clear everything and start fresh."""
        reply = QMessageBox.question(
            self, "Confirm New",
            "Clear all region sets and start fresh?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        if reply == QMessageBox.StandardButton.Yes:
            self.region_sets.clear()
            self.current_region_set_name = None
            self.region_set_combo.blockSignals(True)
            self.region_set_combo.clear()
            self.region_set_combo.blockSignals(False)
            self.canvas.regions = []
            self.canvas.scene.clear()
            self.canvas.current_pixmap = None
            self.canvas.pixmap_item = None
            self.texture_list.clear()
            self.regions_list.clear()
            self.texture_width.blockSignals(True)
            self.texture_height.blockSignals(True)
            self.texture_width.setValue(64)
            self.texture_height.setValue(64)
            self.texture_width.blockSignals(False)
            self.texture_height.blockSignals(False)


def main():
    """Entry point."""
    app = QApplication(sys.argv)
    window = TextureRegionEditor()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
