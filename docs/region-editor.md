# Region editor — `mcoverlayer-region-editor`

## What it does

The region editor lets you visually define named rectangular UV regions on Minecraft entity
textures. These regions are saved as JSON files in the `entity_regions/` directory and used by
the CLI and GUI to apply overlays face-by-face rather than across the entire skin sheet.

## Workflow

### 1. Open a regions folder

Click **Open Folder…** in the toolbar and select `entity_regions/`.

The entity list on the left is populated with every `.json` file found in that directory.
Selecting an entity immediately loads its first texture into the canvas (if one can be resolved)
and displays all its regions as coloured overlays.

### 2. Set the texture base folder

Entity region JSON files store texture paths as **relative paths** rooted at the Minecraft
assets hierarchy, for example:

```
minecraft/textures/entity/allay/allay.png
```

To display these textures in the editor you must tell it where the `assets/` folder of a
resource pack lives. Click **Browse…** in the **Texture Base** panel at the top of the left
sidebar and select the root folder that contains the `assets/` directory — for example:

```
dev/faithful/        ← select this; it contains assets/minecraft/textures/…
```

> **Tip:** If you want to see the vanilla textures, point this at `dev/vanilla/` instead.

Once set, the canvas will automatically resolve and display the correct texture for the
selected entity.

### 3. Edit regions

With an entity and texture selected, the canvas shows the texture at the current zoom level.
Existing regions are drawn as translucent coloured rectangles with their names labelled.

**Drawing a new region**
- Left-click and drag on the canvas to draw a rectangle.
- Release to confirm. A region named `region_N` is created automatically.
- Immediately rename it in the **Name** field in the Regions panel.

**Selecting a region**
- Click any existing region rectangle on the canvas, or click its entry in the Regions list.
- The selected region is highlighted in orange.

**Renaming a region**
- Select the region, then edit the **Name** field. The label updates live.

**Flip and rotation**
- These metadata fields are stored in the JSON and used by the overlay engine to determine
  how the overlay is oriented before compositing onto the region.

| Flip value | Effect |
|------------|--------|
| `none` | No flip |
| `h` | Flip horizontally |
| `v` | Flip vertically |
| `hv` | Flip both axes |

| Rotate value | Effect |
|--------------|--------|
| `none` | No rotation |
| `cw` | Rotate 90° clockwise |
| `ccw` | Rotate 90° counter-clockwise |

**Removing a region**
- Select it and click **Remove Region**.

### 4. Edit texture size (UV size)

The **UV size** spinboxes (W × H) in the Entities group show the entity's canonical texture
dimensions — the coordinate space in which region coordinates are expressed. This is usually
`64×64` for humanoid entities, `32×32` for small mobs, etc.

If you change this value, the region rectangles are redrawn at the new scale. Save afterwards
to persist the change.

> **Note:** The UV size is independent of the actual image resolution. A faithful pack might
> use a 128×128 image for a `64×64` entity — the editor handles this automatically by scaling
> the region rectangles to match the image.

### 5. Manage textures

The **Textures** list shows all texture paths associated with the current entity. Multiple
textures can share the same set of regions (e.g. the different sheep wool colours).

- **Add…** — open a file picker and add one or more image files. Paths within the texture base
  folder are stored relative to it; paths outside are stored absolute.
- **Remove** — remove the selected texture from the entity's list (does not delete the file).

Clicking a texture entry in the list loads that image on the canvas.

### 6. Save

Click **Save Entity** to write the current entity's JSON back to disk. Each entity is stored
as a single `.json` file named after its entity ID in the regions folder.

### Zoom and navigation

| Action | Result |
|--------|--------|
| **Zoom In** toolbar button | Zoom in |
| **Zoom Out** toolbar button | Zoom out |
| **Fit** toolbar button | Fit the whole texture in the canvas |
| `Ctrl` + scroll wheel | Zoom in/out under cursor |
| Middle-mouse drag | Pan the canvas |

## JSON format

Each file in `entity_regions/` contains exactly one entity. See
[entity-regions.md](entity-regions.md) for the full format specification.

## Tips

- Work from a reference image: open the Minecraft wiki page for the entity model alongside the
  editor so you know which UV rectangle corresponds to which face.
- Name regions consistently: use `partname_facename` (e.g. `head_front`, `left_arm_top`).
  The overlay engine uses these names in its random assignment, so consistent naming makes
  `face-mode=same` work predictably across similar entities.
- After editing regions for one entity, use `analyze_entity_textures.py` to check for
  entities still missing texture references.
