# GUI guide — `mcoverlayer-gui`

## Overview

The GUI provides a live preview of overlay assignments and a one-click apply step.
Configuration is auto-saved to `last_run.json` in the OS app-data directory and restored on the
next launch. You can also load and save named configuration files with **Load Config…** / **Save Config…**.

## Layout

```
┌─────────────────────────────────────────────────────────────┐
│ Menu bar: Tools  Help                                        │
├─────────────────────────────────────────────────────────────┤
│ Toolbar: Seed  ⟳  ⎘  │  Filter  Type  Max  │  ▶ Apply      │
├───────────────┬─────────────────────────────────────────────┤
│               │                                             │
│  Config panel │  Assignment preview grid                    │
│  (left)       │  (scrollable cards, one per texture)        │
│               │                                             │
└───────────────┴─────────────────────────────────────────────┘
│ Status bar                                                  │
└─────────────────────────────────────────────────────────────┘
```

## Menu bar

### Tools

| Action | Description |
|--------|-------------|
| **Overlay Lookup** | Open a dialog to search which overlay is assigned to a specific texture |
| **Seed Search** | Open the seed search tool (see below) |

### Help

| Action | Description |
|--------|-------------|
| **About MC Overlayer** | Version, copyright, and license information |
| **About Qt** | Qt framework version and license information |

## Toolbar

| Control | Description |
|---------|-------------|
| **Seed** field | Current seed value. Type a number or any text (hashed to a number) and press Enter to apply |
| **⟳** | Generate a random seed and rebuild the preview. Shortcut: `R` or `Space` |
| **⎘** | Copy the current seed to the clipboard |
| **Filter** field | Filter the preview grid by texture or overlay filename (300 ms debounce) |
| **Type** dropdown | Show `All`, `Entity`, or `Regular` assignments only |
| **Max** spinner | Maximum number of cards to render in the preview (10–5000, default 200) |
| **▶ Apply** | Open the apply dialog and write composited textures to disk |

> **Tip:** If you change settings in the config panel without clicking **Use These Settings** first,
> clicking **▶ Apply** will warn you and offer to apply the new settings before proceeding.

## Config panel

The config panel on the left has three tabs. After changing any settings, click
**Use These Settings** to rebuild the preview grid.

Use **Load Config…** / **Save Config…** at the bottom of the panel to load or save a named JSON
configuration file. Loading a config immediately rebuilds the preview.

### General tab

A brief **How to use** hint is shown at the top.

**Paths**

| Field | Description |
|-------|-------------|
| Overlay Images | Directory containing your source artwork (the images composited onto the textures) |
| Texture Images | Directory containing the textures to modify (typically an extracted Minecraft resource pack) |

**Settings**

| Setting | Default | Description |
|---------|---------|-------------|
| Alpha | 0.75 | Overlay blend strength (0.0 = invisible, 1.0 = full replacement) |
| Overlay Scale | 1.0 | Resize the overlay relative to the target before blending (1.0 = fill, 0.5 = tile 4×, 2.0 = zoom in) |
| Keep aspect ratio | off | Letterbox the overlay instead of stretching it to fill the texture |

**▶ Advanced Settings** *(collapsed by default — click to expand)*

| Setting | Default | Description |
|---------|---------|-------------|
| Scale | 4 | Output upscale multiplier. 4 = 16 px → 64 px output. Higher values produce larger files |
| Fast overlay size | 512 px | Pre-load and cache overlays at this pixel size to speed up processing; `Off` (0) scales on demand |
| Per-frame overlays | off | Assign a different overlay per animation frame for animated textures (`.mcmeta`) |

Click **Use These Settings** to apply any changes and rebuild the card grid.

### Path Config tab

Override settings for specific files or directory prefixes. A short description is shown at the
top of the tab. Click **+ Add** to create a rule, select it to edit its path key and overrides.

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

Entity skin settings. Assigns separate overlays to each body-part region of a Minecraft entity skin.
A hint at the top of the tab notes that this tab can be skipped if you only use block/item textures.

**Entity Settings**

| Setting | Default | Description |
|---------|---------|-------------|
| Face mode | `same` | `same`: one overlay per body part. `different`: one overlay per face region |
| Texture mode | `shared` | `shared`: shared textures get the same assignment. `separate`: independent |

**▶ Advanced Settings** *(collapsed by default — click to expand)*

Contains the entity regions directory override. The app-bundled `entity_regions/` directory is
used automatically when nothing is configured here.

| Control | Description |
|---------|-------------|
| Regions field | Path to the `entity_regions/` directory containing JSON region definitions. Browse with **…** |
| **⟳** | Reset the field to the auto-detected app-bundled `entity_regions/` directory |

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

Available via **Tools → Overlay Lookup**.

## Seed Search dialog

Searches across a range of seeds to find one that assigns a particular overlay to a particular
texture. Specify the target texture, the desired overlay, and a seed range, and the tool will
scan in parallel and list every matching seed.

Available via **Tools → Seed Search**.

## Apply dialog

Click **▶ Apply** to open the apply dialog. It runs the same processing pipeline as the CLI,
showing a live progress bar and ETA. The output directory can optionally be set here (reference
mode); if left blank, textures are modified in place.

If you have changed settings in the config panel without clicking **Use These Settings**, a
warning dialog will appear offering to apply the settings first or proceed with the previously
applied configuration.
