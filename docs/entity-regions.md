# Entity regions

## What they are

Minecraft entity skins are UV maps: a single flat image whose pixels are wrapped onto a 3D
model according to fixed UV coordinates baked into the model. Without region awareness,
applying an overlay to a creeper skin would composite the same pattern uniformly across the
head region, the body region, and the unused transparent padding — all in one pass.

**Entity region files** describe the rectangular UV sub-regions of each entity's texture
(head_front, body_top, left_arm_right, …). The overlay engine uses these to assign and composite
overlays face-by-face, respecting the UV boundaries.

## File format

Each entity has one JSON file in `entity_regions/`, named `<entity_id>.json`.
The structure wraps the entity data under its ID as the top-level key:

```json
{
  "allay": {
    "texture_size": [32, 32],
    "textures": [
      "minecraft/textures/entity/allay/allay.png"
    ],
    "regions": [
      { "name": "head_top",    "x": 5,  "y": 0, "width": 5, "height": 5 },
      { "name": "head_front",  "x": 5,  "y": 5, "width": 5, "height": 5 },
      { "name": "head_left",   "x": 10, "y": 5, "width": 5, "height": 5, "flip": "h" },
      { "name": "body_front",  "x": 2,  "y": 12, "width": 3, "height": 4 }
    ]
  }
}
```

### Top-level fields

| Field | Type | Description |
|-------|------|-------------|
| `texture_size` | `[int, int]` | Canonical UV dimensions `[width, height]` in the entity's coordinate space. Region coordinates are expressed in this space regardless of the actual image resolution |
| `textures` | `string[]` | Relative texture paths from the resource pack root (i.e. from the folder containing `assets/`). Multiple textures share the same region set (e.g. sheep wool colours) |
| `regions` | `object[]` | List of named rectangular UV regions (see below) |

### Region object

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | yes | Unique name within this entity, e.g. `head_front`, `left_arm_top` |
| `x` | int | yes | Left edge in canonical UV coordinates |
| `y` | int | yes | Top edge in canonical UV coordinates |
| `width` | int | yes | Width in canonical UV coordinates |
| `height` | int | yes | Height in canonical UV coordinates |
| `flip` | string | no | `"h"` horizontal, `"v"` vertical, `"hv"` both. Omit or leave empty for none |
| `rotate` | string | no | `"cw"` 90° clockwise, `"ccw"` 90° counter-clockwise. Omit for none |

### Coordinate system

Region coordinates use the **canonical** texture space defined by `texture_size`, not the
actual image pixel dimensions. The overlay engine scales the regions automatically to match
the real image resolution at runtime.

Example: an entity with `texture_size: [32, 32]` and a 64×64 faithful texture — every region
coordinate is multiplied by `64/32 = 2` when compositing.

## Generating region files

Most region files are generated from the decompiled Minecraft source using the extraction
script. Manual editing via the region editor is used for fine-tuning or for entities whose
source models are too complex to parse automatically.

### `extract_entity_regions.py`

Parses Java model and renderer source files in `dev/game_decomp/` to extract UV cube
definitions and produce entity region JSON files.

> **Version requirement:** this script targets **Minecraft 1.21.11** decompiled with the
> **Fabric toolchain** using **Yarn mappings**. Class and field names in `dev/game_decomp/`
> must use Yarn names (e.g. `EntityModelPartNames`, `ModelPart`, `ModelTransform`).
> Other Minecraft versions or mapping sets (Mojmap, MCP) will require adjustments to the
> class/field name patterns in the script.

```bash
cd scripts
python3 extract_entity_regions.py               # extract all entities
python3 extract_entity_regions.py --entity Allay  # extract one entity only
python3 extract_entity_regions.py --dry-run       # print without writing files
```

**Requirements:**
- `dev/game_decomp/client/net/minecraft/client/render/entity/model/` must contain the
  decompiled entity model Java sources (Yarn-mapped).
- `dev/game_decomp/client/net/minecraft/client/render/entity/` must contain the renderer
  sources (for texture path resolution).

The script reads `ModelPart.setTextureOffset`, `ModelPart.addCuboid`, and similar calls to
derive UV coordinates, then writes one JSON file per entity to `entity_regions/`.

### `analyze_entity_textures.py`

Reports which entities are missing texture references and which textures are shared between
multiple entities.

```bash
cd scripts
python3 analyze_entity_textures.py
```

Example output:

```
Total entities: 87

Entities missing textures: 3
  - guardian_elder
  - shulker_bullet
  - warden

Shared textures (used by multiple entities): 2

  minecraft/textures/entity/sheep/sheep.png
    - sheep
    - sheep_fur
```

Use this as a checklist after running `extract_entity_regions.py` to identify which entities
still need manual attention in the region editor.

## Editing region files

Use `mcoverlayer-region-editor` for visual editing. See
[region-editor.md](region-editor.md) for full instructions.

You can also edit the JSON files directly in a text editor — the format is simple and stable.
