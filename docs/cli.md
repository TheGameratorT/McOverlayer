# CLI reference — `mcoverlayer-cli`

## Synopsis

```
mcoverlayer-cli [options] <overlay_dir> <texture_dir>
```

| Argument | Description |
|----------|-------------|
| `overlay_dir` | Directory containing source images to composite in (`.png`, `.jpg`, `.jpeg`) |
| `texture_dir` | Source texture directory. Modified in-place unless `--output-dir` is set |

## Options

### Processing

| Flag | Default | Description |
|------|---------|-------------|
| `-w`, `--workers N` | `min(8, cpus)` | Number of parallel worker threads |
| `-s`, `--scale N` | `4` | Output upscale factor (1–32). A 16×16 texture at scale 4 → 64×64 |
| `--alpha F` | `0.75` | Overlay blend strength (0.0 = no effect, 1.0 = full replacement) |
| `--overlay-scale F` | `1.0` | Resize the overlay relative to the target before compositing (0.0–2.0) |
| `--keep-aspect` | off | Scale overlay to fit inside target bounds, preserving overlay aspect ratio |
| `--fast-overlay-size N` | `512` | Pre-load all overlays cached at this pixel size for faster compositing. Set to `0` to disable |
| `--per-frame` | off | Assign a different overlay to each frame of animated textures |

### Seed

| Flag | Default | Description |
|------|---------|-------------|
| `--seed SEED` | random | Seed controlling the overlay→texture assignment. Accepts an integer or an arbitrary string (hashed deterministically). The same seed always produces the same result |

### Output

| Flag | Default | Description |
|------|---------|-------------|
| `--output-dir PATH` | *(none)* | Write composited textures to this directory instead of modifying `texture_dir` in place. Non-image files from `texture_dir` are copied over, producing a complete resource pack. **Recommended over in-place mode** |

### Entity regions

| Flag | Default | Description |
|------|---------|-------------|
| `--entity-regions PATH` | *(none)* | Path to the `entity_regions/` directory. When set, entity skins are processed face-region by face-region instead of as whole images. See [entity-regions.md](entity-regions.md) |
| `--entity-face-mode MODE` | `same` | `same`: all faces of a body part share one overlay. `different`: each face region gets its own independent overlay |
| `--entity-texture-mode MODE` | `shared` | `shared`: entities that share a texture file receive the same overlay assignment. `separate`: every texture is assigned independently |

### Per-path overrides

| Flag | Default | Description |
|------|---------|-------------|
| `--path-config JSON` | *(none)* | JSON string of per-path setting overrides. Keys are exact file paths or directory prefixes; longest match wins. See [concepts.md § Path config](concepts.md#path-config) for supported keys and examples |

## Exit codes

| Code | Meaning |
|------|---------|
| `0` | All textures processed successfully |
| `1` | Configuration error (missing directories, bad arguments) |
| `130` | Interrupted by `Ctrl-C` (SIGINT); partial results may have been written |

## Examples

### Minimal — in-place modification

```bash
mcoverlayer-cli dev/dataset dev/faithful
```

### Reference mode with a named seed

```bash
mcoverlayer-cli dev/dataset dev/faithful \
    --seed "autumn-2024" \
    --output-dir dev/target
```

### Full pipeline with entity regions

```bash
mcoverlayer-cli dev/dataset dev/faithful \
    --entity-regions entity_regions \
    --entity-face-mode different \
    --seed 99887766 \
    --scale 4 \
    --alpha 0.70 \
    --output-dir dev/target
```

### Per-path overrides — keep pack.png at native size, upscale blocks at ×8

```bash
mcoverlayer-cli dev/dataset dev/faithful \
    --path-config '{"pack.png":{"scale":1},"assets/minecraft/textures/block":{"scale":8}}' \
    --output-dir dev/target
```

### Disable fast overlay cache (trade speed for memory)

```bash
mcoverlayer-cli dev/dataset dev/faithful --fast-overlay-size 0 --output-dir dev/target
```

## Notes

- A `last_run.json` file is written to the working directory after each run, recording the full
  configuration. The GUI reads this on startup to restore the previous session.
- An `overlay_meta.json` is also written into `texture_dir` (or `output-dir`) with the same
  configuration, useful for reproducibility.
- Worker thread count is capped at 8 by default even on machines with more cores, to avoid
  thrashing the image codec. Pass `-w N` to override.
