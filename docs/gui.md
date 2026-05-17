# GUI guide — `mcoverlayer-gui`

## Overview

The GUI provides a live preview of overlay assignments and a one-click apply step.
Configuration is auto-saved to `last_run.json` in the working directory and restored on the
next launch.

## Layout

```
┌─────────────────────────────────────────────────────────────┐
│ Toolbar: Seed  ⟳  ⎘  │  Filter  Type  Max  │  Lookup  Search  ▶ Apply │
├───────────────┬─────────────────────────────────────────────┤
│               │                                             │
│  Config panel │  Assignment preview grid                    │
│  (left)       │  (scrollable cards, one per texture)        │
│               │                                             │
└───────────────┴─────────────────────────────────────────────┘
│ Status bar                                                  │
└─────────────────────────────────────────────────────────────┘
```

## Toolbar

| Control | Description |
|---------|-------------|
| **Seed** field | Current seed value. Type a number or any text (hashed to a number) and press Enter to apply |
| **⟳** | Generate a random seed and rebuild the preview. Shortcut: `R` or `Space` |
| **⎘** | Copy the current seed to the clipboard |
| **Filter** field | Filter the preview grid by texture or overlay filename (300 ms debounce) |
| **Type** dropdown | Show `All`, `Entity`, or `Regular` assignments only |
| **Max** spinner | Maximum number of cards to render in the preview (10–5000, default 200) |
| **Overlay Lookup** | Open a dialog to search which overlay is assigned to a specific texture |
| **Seed Search** | Open the seed search tool (see below) |
| **▶ Apply** | Open the apply dialog and write composited textures to disk |

## Config panel

The config panel on the left has three tabs.

### General tab

**Paths**

| Field | Description |
|-------|-------------|
| Overlay Images | Directory containing your overlay images |
| Texture Images | Source texture directory (resource pack) |

**Core Settings**

| Setting | Default | Description |
|---------|---------|-------------|
| Alpha | 0.75 | Overlay blend strength (0.0–1.0) |
| Overlay Scale | 1.0 | Resize the overlay relative to the target (0.0–2.0) |
| Scale | 4 | Output upscale factor (1–32) |
| Fast overlay size | 512 px | Pre-load overlays at this size for faster compositing; `Off` (0) disables |
| Per-frame overlays | off | Assign a different overlay per animation frame |
| Keep aspect ratio | off | Letterbox the overlay instead of stretching it |

Click **Update Preview** to apply any changes and rebuild the card grid.

### Path Config tab

Override settings for specific files or directory prefixes. Click **+ Add** to create a rule,
select it to edit its path key and overrides.

The **path key** is matched against the relative texture path. Exact file matches take priority
over prefix matches; among prefixes, the longest one wins.

| Override | Description |
|----------|-------------|
| Scale | Upscale factor for this path |
| Alpha | Blend alpha for this path |
| Overlay Scale | Overlay resize factor for this path |
| Keep Aspect | Preserve overlay aspect ratio for this path |
| Fast Overlay Size | Per-path fast cache size in px |

Common rules:

| Path | Typical use |
|------|-------------|
| `pack.png` | `scale = 1` — keep the pack icon at its native 64×64 |
| `assets/minecraft/textures/block` | Higher scale for block textures |
| `assets/minecraft/textures/entity` | Lower fast-overlay-size to save memory |

### Entity tab

| Setting | Default | Description |
|---------|---------|-------------|
| Regions | *(blank)* | Path to the `entity_regions/` directory. Browse or type the path |
| Face mode | `same` | `same`: one overlay per body part. `different`: one overlay per face |
| Texture mode | `shared` | `shared`: shared textures get the same assignment. `separate`: independent |

See [entity-regions.md](entity-regions.md) for how entity regions work.

## Preview grid

Each card in the grid shows:
- **Left image**: the source texture
- **Right image**: a composited thumbnail (loaded asynchronously)
- **Label**: relative texture path and assigned overlay filename

Cards are sorted with well-known textures (grass, stone, dirt, water, etc.) displayed first,
followed by everything else.

## Overlay Lookup dialog

Search for a specific texture path and see which overlay is currently assigned to it. Useful
for checking entity assignments without scrolling through the whole grid.

## Seed Search dialog

Searches across a range of seeds to find one that assigns a particular overlay to a particular
texture. Specify the target texture, the desired overlay, and a seed range, and the tool will
scan in parallel and list every matching seed.

## Apply dialog

Click **▶ Apply** to open the apply dialog. It runs the same processing pipeline as the CLI,
showing a live progress bar and ETA. The output directory can optionally be set here (reference
mode); if left blank, textures are modified in place.
