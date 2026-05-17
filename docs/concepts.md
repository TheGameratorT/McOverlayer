# Core concepts

## Source images and target textures

**Source images** (called "overlay images" in the UI and CLI) are the images you want to
composite into the pack — a folder of anime pictures, photographs, drawings, or anything else.
Any resolution is accepted; they are scaled to match the target texture at composite time.

**Target textures** are the textures you want to modify — typically a Minecraft resource pack
extracted to a folder. The tool walks the directory recursively and finds every `.png`/`.jpg`.

## Seeds

A **seed** determines which source image is assigned to which texture. The same seed always
produces the same mapping, so a seed functions as a "version" of your pack. Seeds can be:

- An integer: `12345`
- A string (hashed to an integer): `"my-anime-pack"` → a deterministic number

Change the seed to shuffle all assignments at once. The GUI's `⟳` button and the
`R` / `Space` shortcuts generate a new random seed instantly.

## Scale factor

The **scale** (`--scale`, default 4) is the output upscale multiplier. A vanilla 16×16 texture
with `--scale 4` produces a 64×64 output. The source image is also scaled accordingly.

## Alpha

**Alpha** (0.0–1.0, default 0.75) is the blend strength. `0.0` leaves the texture unchanged;
`1.0` replaces it entirely with the source image. Values around `0.6`–`0.8` give a tinted look
where the original texture is still recognisable underneath.

## Overlay scale

**Overlay scale** (0.0–2.0, default 1.0) independently resizes the source image relative to
the target texture before compositing. `0.5` tiles the image at half-size; `2.0` zooms it in
so only the centre portion is used.

## Per-frame overlays

Animated textures (`.mcmeta` strips) are normally given one source image for all frames. With
`--per-frame` each animation frame gets its own independent assignment, creating frame-by-frame
variation.

## Keep aspect ratio

When enabled, the source image is scaled to fit inside the target texture bounds while
preserving its own aspect ratio (letterboxed). When disabled (default) it is stretched to fill
the texture exactly.

## Fast overlay size

Source images can be pre-loaded and cached at a fixed pixel size before processing begins.
This eliminates repeated rescaling per-texture and significantly speeds up batch runs. The
default is 512 px. Set to 0 to disable (images are scaled on demand per texture).

## Entity regions

Entity skins in Minecraft are UV maps — a flat image that wraps around a 3D model. Without
special handling, a source image would be composited uniformly across the entire skin sheet,
ignoring which part of the model each pixel belongs to.

**Entity regions** solve this by defining named rectangular UV regions for each entity
(head_front, body_top, left_arm_right, etc.). The tool assigns a separate source image to each
region independently, so for example the head gets a different image than the body.

Region data is stored in JSON files in the `entity_regions/` directory at the project root.
See [entity-regions.md](entity-regions.md) for the file format and how to generate or edit
these files.

### Face mode

| Value | Behaviour |
|-------|-----------|
| `same` (default) | All faces of a body part use the same source image |
| `different` | Every individual face region gets its own independent assignment |

### Texture mode

| Value | Behaviour |
|-------|-----------|
| `shared` (default) | Entities sharing a texture (e.g. differently coloured sheep) receive the same assignment |
| `separate` | Each texture gets an independent assignment even if shared |

## Output mode vs in-place mode

- **In-place** (default): textures in `texture_dir` are modified directly.
- **Reference mode** (`--output-dir`): results are written to a new directory; non-image files
  from `texture_dir` are copied over so the output is a complete, valid resource pack.

Reference mode is strongly recommended whenever you want to preserve the originals.

## Path config

Per-path overrides let you apply different settings to specific files or directories. For
example, render `pack.png` at `scale=1` (so it stays the right size) while the rest of the
pack uses `scale=4`, or reduce alpha for translucent glass textures.

See the **Path Config** tab in the GUI, or the `--path-config` flag in the CLI.
Rules are stored as a JSON string:

```json
{
  "pack.png":       { "scale": 1 },
  "assets/block":   { "scale": 8, "alpha": 0.6 },
  "assets/entity":  { "fast-overlay-size": 256 }
}
```

Keys are matched as exact paths first, then as directory prefixes (longest match wins).

Supported override keys:

| Key | Type | Description |
|-----|------|-------------|
| `scale` | int | Output upscale factor for this path |
| `alpha` | float | Blend alpha for this path |
| `overlay-scale` | float | Source image resize factor for this path |
| `keep-aspect` | bool | Preserve source image aspect ratio for this path |
| `fast-overlay-size` | int | Per-path fast cache size in px; 0 = off |
