#!/usr/bin/env python3
"""
Extract entity UV face regions from Minecraft game_decomp Java source files.
Generates entity_regions JSON files with named face regions.

Target: Minecraft 1.21.11, decompiled with Fabric toolchain using Yarn mappings.
        Class and field names in game_decomp/ are expected to use Yarn names
        (e.g. EntityModelPartNames, ModelPart, ModelTransform). Other versions or
        mapping sets (Mojmap, MCP) will require adjustments to the class/field
        name patterns used throughout this script.

Usage: python3 extract_entity_regions.py [--entity EntityName] [--dry-run]

See entity_regions_plan.md for full documentation and progress tracking.
"""

import re
import json
import sys
import argparse
from pathlib import Path

# ─── Paths ────────────────────────────────────────────────────────────────────

PROJECT_ROOT = Path(__file__).parent.parent
DEV_DIR      = PROJECT_ROOT / "dev"
MODEL_DIR              = DEV_DIR / "game_decomp/client/net/minecraft/client/render/entity/model"
RENDERER_DIR           = DEV_DIR / "game_decomp/client/net/minecraft/client/render/entity"
BLOCK_ENTITY_MODEL_DIR = DEV_DIR / "game_decomp/client/net/minecraft/client/render/block/entity/model"
BLOCK_ENTITY_RENDERER_DIR = DEV_DIR / "game_decomp/client/net/minecraft/client/render/block/entity"
OUTPUT_DIR = PROJECT_ROOT / "entity_regions"

# ─── EntityModelPartNames constants ───────────────────────────────────────────

PART_NAME_CONSTANTS: dict[str, str] = {}

def load_part_name_constants():
    java_file = MODEL_DIR / "EntityModelPartNames.java"
    pattern = re.compile(r'public static final String (\w+) = "([^"]+)"')
    for m in pattern.finditer(java_file.read_text()):
        const, value = m.groups()
        PART_NAME_CONSTANTS[f"EntityModelPartNames.{const}"] = value

def resolve_part_name(expr: str) -> str:
    expr = expr.strip()
    if expr.startswith('"') and expr.endswith('"'):
        return expr[1:-1]
    if expr in PART_NAME_CONSTANTS:
        return PART_NAME_CONSTANTS[expr]
    # Handle method calls like getRodName(i) for blaze - mark with special prefix
    if "getRodName(" in expr:
        return "getRodName(i)"  # Will be handled in hardcoding
    # Try partial match (e.g. variable name)
    return expr

# ─── Java parsing utilities ───────────────────────────────────────────────────

def find_balanced_end(s: str, start: int, open_ch: str = '(', close_ch: str = ')') -> int:
    """Return index of the closing bracket matching s[start]=open_ch."""
    depth = 0
    for i in range(start, len(s)):
        if s[i] == open_ch:
            depth += 1
        elif s[i] == close_ch:
            depth -= 1
            if depth == 0:
                return i
    return len(s) - 1

def split_args(s: str) -> list[str]:
    """Split s by commas at depth 0 (respects nested brackets)."""
    args, depth, cur = [], 0, []
    for ch in s:
        if ch in '([{':
            depth += 1
            cur.append(ch)
        elif ch in ')]}':
            depth -= 1
            cur.append(ch)
        elif ch == ',' and depth == 0:
            args.append(''.join(cur).strip())
            cur = []
        else:
            cur.append(ch)
    if cur:
        args.append(''.join(cur).strip())
    return args

def extract_method_body(code: str, method_sig: str) -> str | None:
    """Extract body of a method (between outermost braces)."""
    idx = code.find(method_sig)
    if idx == -1:
        return None
    brace_idx = code.find('{', idx)
    if brace_idx == -1:
        return None
    close = find_balanced_end(code, brace_idx, '{', '}')
    return code[brace_idx + 1:close]

def parse_float_arg(s: str) -> float | None:
    """Parse a Java float literal, returning None on failure."""
    s = s.strip().rstrip('FfDd')
    # Handle expressions like -4.0F → strip trailing, then parse
    try:
        return float(s)
    except ValueError:
        return None

# ─── Builder / cuboid extraction ──────────────────────────────────────────────

def find_builder_assignments(method_body: str) -> dict[str, str]:
    """Return {varname: builder_expr} for all ModelPartBuilder assignments."""
    assignments = {}
    for m in re.finditer(r'ModelPartBuilder\s+(\w+)\s*=', method_body):
        varname = m.group(1)
        start = m.end()
        # Read until ';' at depth 0
        depth, i = 0, start
        while i < len(method_body):
            ch = method_body[i]
            if ch in '([{':
                depth += 1
            elif ch in ')]}':
                depth -= 1
            elif ch == ';' and depth == 0:
                break
            i += 1
        assignments[varname] = method_body[start:i].strip()
    return assignments

def _is_numeric(s: str) -> bool:
    """Return True if s parses as a number (float, possibly negative)."""
    try:
        float(s.strip().lstrip('-').rstrip('FfDd').strip())
        return True
    except ValueError:
        return False

def _parse_cuboid_args(args: list[str], current_u: int | None, current_v: int | None
                       ) -> tuple[int, int, int, int, int] | None:
    """
    Parse cuboid args and return (u, v, sx, sy, sz) or None.

    Handles all ModelPartBuilder.cuboid() variants:
      Standard:  (ox, oy, oz, sx, sy, sz [, Dilation | bool | Set])
      Named:     (name, ox, oy, oz, sx, sy, sz [, Dilation])
      Named+UV:  (name, ox, oy, oz, sx, sy, sz, Dilation, u, v) or
                 (name, ox, oy, oz, sx, sy, sz, u, v)
    """
    if not args:
        return None

    named = not _is_numeric(args[0])
    offset = 1 if named else 0  # skip name arg for named forms

    n = len(args)

    # Named form with UV at end
    if named and n >= 9:
        # Try to parse last two as integers (u, v)
        u_v = (parse_float_arg(args[-2]), parse_float_arg(args[-1]))
        if u_v[0] is not None and u_v[1] is not None:
            dims_start = offset  # ox, oy, oz, sx, sy, sz at offset+0..5
            try:
                sx = abs(float(args[dims_start + 3].strip().rstrip('Ff')))
                sy = abs(float(args[dims_start + 4].strip().rstrip('Ff')))
                sz = abs(float(args[dims_start + 5].strip().rstrip('Ff')))
                return (int(u_v[0]), int(u_v[1]), int(sx), int(sy), int(sz))
            except (ValueError, IndexError):
                pass

    # Standard or named without UV override — use current_u/v
    if current_u is None:
        return None

    try:
        sx = abs(float(args[offset + 3].strip().rstrip('Ff')))
        sy = abs(float(args[offset + 4].strip().rstrip('Ff')))
        sz = abs(float(args[offset + 5].strip().rstrip('Ff')))
        return (current_u, current_v, int(sx), int(sy), int(sz))
    except (ValueError, IndexError):
        return None

def parse_builder_cuboids(expr: str, var_assignments: dict[str, str] | None = None
                          ) -> list[tuple[int, int, int, int, int, bool]]:
    """
    Parse a ModelPartBuilder expression and return list of
    (u, v, sx, sy, sz, mirrored) for each .cuboid() call found.
    """
    expr = expr.strip()

    # Resolve variable reference
    if var_assignments and expr in var_assignments:
        expr = var_assignments[expr]
    elif var_assignments:
        # Could be "varname.something()" style (rare), try stripping suffix
        base = expr.split('.')[0].strip()
        if base in var_assignments:
            suffix = expr[len(base):]
            expr = var_assignments[base] + suffix

    mirrored = '.mirrored()' in expr

    results = []
    current_u: int | None = 0  # Default UV is (0, 0) when not explicitly set
    current_v: int | None = 0
    pos = 0

    while pos < len(expr):
        uv_idx = expr.find('.uv(', pos)
        cub_idx = expr.find('.cuboid(', pos)

        if uv_idx == -1 and cub_idx == -1:
            break

        # Process whichever comes first
        if uv_idx != -1 and (cub_idx == -1 or uv_idx < cub_idx):
            open_p = uv_idx + len('.uv')
            close_p = find_balanced_end(expr, open_p)
            args = split_args(expr[open_p + 1:close_p])
            if len(args) >= 2:
                u = parse_float_arg(args[0])
                v = parse_float_arg(args[1])
                if u is not None and v is not None:
                    current_u, current_v = int(u), int(v)
            pos = close_p + 1
        else:
            open_p = cub_idx + len('.cuboid')
            close_p = find_balanced_end(expr, open_p)
            args = split_args(expr[open_p + 1:close_p])
            result = _parse_cuboid_args(args, current_u, current_v)
            if result is not None:
                u, v, sx, sy, sz = result
                results.append((u, v, sx, sy, sz, mirrored))
                # If this cuboid defined its own UV, update current UV state
                if not _is_numeric(args[0]) and len(args) >= 9:
                    current_u, current_v = u, v
            pos = close_p + 1

    return results

def find_add_child_calls(method_body: str) -> list[tuple[str, str]]:
    """
    Find all .addChild(partNameExpr, builderExpr, ...) calls.
    Returns list of (part_name_expr_raw, builder_expr_raw).
    """
    results = []
    pos = 0
    while pos < len(method_body):
        idx = method_body.find('.addChild(', pos)
        if idx == -1:
            break
        open_p = idx + len('.addChild')  # points at '('
        close_p = find_balanced_end(method_body, open_p)
        args = split_args(method_body[open_p + 1:close_p])
        if len(args) >= 2:
            results.append((args[0].strip(), args[1].strip()))
        pos = close_p + 1
    return results

# ─── Face computation ─────────────────────────────────────────────────────────

def compute_face_regions(u: int, v: int, sx: int, sy: int, sz: int,
                         part_name: str, cuboid_idx: int, num_cuboids: int
                         ) -> list[dict]:
    """
    Compute texture face rectangles for one cuboid.
    Name format: "{part}_{face}" (single cuboid) or "{part}_{n}_{face}" (multiple).
    """
    prefix = f"{part_name}_" if num_cuboids == 1 else f"{part_name}_{cuboid_idx}_"
    regions = []

    # Top and bottom (need sx>0, sz>0)
    if sx > 0 and sz > 0:
        regions.append({"name": f"{prefix}top",    "x": u + sz,       "y": v,      "width": sx, "height": sz})
        regions.append({"name": f"{prefix}bottom", "x": u + sz + sx,  "y": v,      "width": sx, "height": sz})

    # Sides (mirrored or not: same pixel positions on the texture atlas,
    # only vertex UV direction differs)
    if sz > 0 and sy > 0:
        regions.append({"name": f"{prefix}right",  "x": u,            "y": v + sz, "width": sz, "height": sy})
    if sx > 0 and sy > 0:
        regions.append({"name": f"{prefix}front",  "x": u + sz,       "y": v + sz, "width": sx, "height": sy})
    if sz > 0 and sy > 0:
        regions.append({"name": f"{prefix}left",   "x": u + sz + sx,  "y": v + sz, "width": sz, "height": sy})
    if sx > 0 and sy > 0:
        regions.append({"name": f"{prefix}back",   "x": u+sz+sx+sz,   "y": v + sz, "width": sx, "height": sy})

    return regions

# ─── Biped base parts ─────────────────────────────────────────────────────────

# Hardcoded from BipedEntityModel.getModelData() (standard biped UV layout)
BIPED_BASE_PARTS: list[tuple[str, int, int, int, int, int, bool]] = [
    # (part_name, u, v, sx, sy, sz, mirrored)
    ("head",      0, 0,  8, 8, 8, False),
    ("hat",       32, 0, 8, 8, 8, False),
    ("body",      16, 16, 8, 12, 4, False),
    ("right_arm", 40, 16, 4, 12, 4, False),
    ("left_arm",  40, 16, 4, 12, 4, True),
    ("right_leg", 0, 16,  4, 12, 4, False),
    ("left_leg",  0, 16,  4, 12, 4, True),
]

def get_biped_regions() -> list[dict]:
    """Return all face regions for the standard biped model."""
    regions = []
    for (name, u, v, sx, sy, sz, _mirrored) in BIPED_BASE_PARTS:
        regions.extend(compute_face_regions(u, v, sx, sy, sz, name, 0, 1))
    return regions

# ─── Texture path extraction ──────────────────────────────────────────────────

def find_entity_textures(model_class_name: str, entity_key: str) -> list[str]:
    """
    Find texture paths for an entity by scanning its renderer file.
    Falls back to existing JSON file if no textures found in renderer.
    """
    renderer_name = model_class_name.replace("EntityModel", "EntityRenderer")

    # Candidate renderer files to scan
    candidates = [RENDERER_DIR / f"{renderer_name}.java"]
    base = model_class_name.replace("EntityModel", "")
    for alt in [f"{base}Renderer", f"Abstract{base}Renderer"]:
        candidates.append(RENDERER_DIR / f"{alt}.java")

    textures: list[str] = []
    for f in candidates:
        if not f.exists():
            continue
        content = f.read_text()
        # Pattern 1: Identifier.ofVanilla("textures/entity/...") — no format specifiers
        for m in re.finditer(r'Identifier\.ofVanilla\("(textures/entity/[^"%\n]+\.png)"\)', content):
            path = f"minecraft/{m.group(1)}"
            if path not in textures:
                textures.append(path)
        # Pattern 2: Identifier.of("minecraft", "textures/entity/...")
        for m in re.finditer(r'Identifier\.of\("minecraft",\s*"(textures/entity/[^"%\n]+\.png)"\)', content):
            path = f"minecraft/{m.group(1)}"
            if path not in textures:
                textures.append(path)

    return textures

# ─── Entity name mapping ──────────────────────────────────────────────────────

# Maps Java class name → JSON entity key
MODEL_TO_ENTITY = {
    "AllayEntityModel":             "allay",
    "ArmadilloEntityModel":         "armadillo",
    "ArmorStandEntityModel":        "armorstand",
    "AxolotlEntityModel":           "axolotl",
    "BedBlockEntityRenderer_head":  "bed_head",
    "BedBlockEntityRenderer_foot":  "bed_foot",
    "BatEntityModel":               "bat",
    "BeeEntityModel":               "bee",
    "BellBlockModel":               "bell",
    "BlazeEntityModel":             "blaze",
    "BoatEntityModel":              "boat",
    "BoatEntityModel_chest":        "chest_boat",
    "BoggedEntityModel":            "bogged",
    "BreezeEntityModel":            "breeze",
    "CamelEntityModel":             "camel",
    "CatEntityModel":               "cat",
    "ChestBlockModel":              "chest",
    "ChestBlockModel_left":         "chest_left",
    "ChestBlockModel_right":        "chest_right",
    "ChickenEntityModel":           "chicken",
    "ColdChickenEntityModel":       "cold_chicken",
    "CodEntityModel":               "cod",
    "CopperGolemEntityModel":       "copper_golem",
    "CowEntityModel":               "cow",
    "ColdCowEntityModel":           "cold_cow",
    "WarmCowEntityModel":           "warm_cow",
    "CreakingEntityModel":          "creaking",
    "CreeperEntityModel":           "creeper",
    "DolphinEntityModel":           "dolphin",
    "DonkeyEntityModel":            "donkey",
    "DragonEntityModel":            "enderdragon",
    "DrownedEntityModel":           "drowned",
    "EndermanEntityModel":          "enderman",
    "EndermiteEntityModel":         "endermite",
    "EndCrystalEntityModel":        "end_crystal",
    "EvokerFangsEntityModel":       "evoker_fangs",
    "FoxEntityModel":               "fox",
    "FrogEntityModel":              "frog",
    "GhastEntityModel":             "ghast",
    "GoatEntityModel":              "goat",
    "GuardianEntityModel":          "guardian",
    "HappyGhastEntityModel":        "happy_ghast",
    "HoglinEntityModel":            "hoglin",
    "HorseEntityModel":             "horse",
    "IllagerEntityModel":           "illager",
    "IronGolemEntityModel":         "iron_golem",
    "LargePufferfishEntityModel":   "pufferfish_large",
    "LargeTropicalFishEntityModel": "tropical_fish_large",
    "LlamaEntityModel":             "llama",
    "MagmaCubeEntityModel":         "magma_cube",
    "MediumPufferfishEntityModel":  "pufferfish_medium",
    "NautilusEntityModel":          "nautilus",
    "OcelotEntityModel":            "ocelot",
    "PandaEntityModel":             "panda",
    "ParrotEntityModel":            "parrot",
    "PhantomEntityModel":           "phantom",
    "PigEntityModel":               "pig",
    "ColdPigEntityModel":           "cold_pig",
    "PiglinEntityModel":            "piglin",
    "PlayerEntityModel":            "player",
    "PolarBearEntityModel":         "polar_bear",
    "RabbitEntityModel":            "rabbit",
    "RavagerEntityModel":           "ravager",
    "SalmonEntityModel":            "salmon",
    "SheepEntityModel":             "sheep",
    "SheepWoolEntityModel":         "sheep_wool",
    "ShieldEntityModel":            "shield",
    "ShulkerEntityModel":           "shulker",
    "SilverfishEntityModel":        "silverfish",
    "SkeletonEntityModel":          "skeleton",
    "SlimeEntityModel":             "slime",
    "SmallPufferfishEntityModel":   "pufferfish_small",
    "SmallTropicalFishEntityModel": "tropical_fish_small",
    "SnifferEntityModel":           "sniffer",
    "SnowGolemEntityModel":         "snow_golem",
    "SpiderEntityModel":            "spider",
    "SquidEntityModel":             "squid",
    "StriderEntityModel":           "strider",
    "TadpoleEntityModel":           "tadpole",
    "TurtleEntityModel":            "turtle",
    "VexEntityModel":               "vex",
    "VillagerResemblingModel":      "villager",
    "WardenEntityModel":            "warden",
    "WitchEntityModel":             "witch",
    "WitherEntityModel":            "wither",
    "WolfEntityModel":              "wolf",
    "ZombieEntityModel":            "zombie",
    "ZombieVillagerEntityModel":    "zombie_villager",
    "ZombifiedPiglinEntityModel":   "zombified_piglin",
}

# Models that call BipedEntityModel.getModelData() as their base
BIPED_INHERITING = {
    "ArmorStandEntityModel",
    "BoggedEntityModel",
    "DrownedEntityModel",
    "EndermanEntityModel",
    "PlayerEntityModel",
    "SkeletonEntityModel",
    "ZombieEntityModel",
    "ZombieVillagerEntityModel",
}

# Cross-file parent model data: model → (parent_java_file, method_signature)
# Used when a model delegates model data to another class's static method.
# Known texture sizes for models where size can't be auto-detected
# (from EntityModels.java TexturedModelData.of() calls)
KNOWN_TEXTURE_SIZES: dict[str, tuple[int, int]] = {
    "BoatEntityModel_chest":     (128, 128),
    "CatEntityModel":            (64, 32),
    "ChestBlockModel":           (64, 64),
    "ChestBlockModel_left":      (64, 64),
    "ChestBlockModel_right":     (64, 64),
    "DonkeyEntityModel":         (64, 64),
    "DragonEntityModel":         (256, 256),
    "EndCrystalEntityModel":     (64, 32),
    "HorseEntityModel":          (64, 64),
    "OcelotEntityModel":         (64, 32),
    "PiglinEntityModel":         (64, 64),
    "PlayerEntityModel":         (64, 64),
    "SlimeEntityModel":          (64, 32),
    "VillagerResemblingModel":   (64, 64),
    "WolfEntityModel":           (64, 32),
    "ZombifiedPiglinEntityModel":(64, 64),
}

CROSS_FILE_PARENTS: dict[str, list[str]] = {
    # Horse family: call AbstractHorseEntityModel.getModelData()
    "DonkeyEntityModel":         ["AbstractHorseEntityModel"],
    "HorseEntityModel":          ["AbstractHorseEntityModel"],
    # Cat/Ocelot family: use FelineEntityModel.getModelData()
    "CatEntityModel":            ["FelineEntityModel"],
    "OcelotEntityModel":         ["FelineEntityModel"],
    # Piglin family
    "PiglinEntityModel":         ["PiglinBaseEntityModel"],
    "ZombifiedPiglinEntityModel":["PiglinBaseEntityModel"],
    # Slime
    "SlimeEntityModel":          ["MagmaCubeEntityModel"],
    # Wolf
    "WolfEntityModel":           [],  # self-contained, just ModelData return type
    # Player
    "PlayerEntityModel":         [],  # biped base + own limbs
}

# Cold variant models: extend parent with extra cold-weather overlay geometry.
# Each cold variant is a separate entity with its own JSON, inheriting
# un-overridden parts (legs, beak, etc.) from the parent entity's JSON.
# Also includes warm_cow, which follows the same inheritance pattern.
COLD_VARIANT_PARENTS: dict[str, str] = {
    "cold_cow":     "cow",
    "warm_cow":     "cow",
    "cold_chicken": "chicken",
    "cold_pig":     "pig",
}

# Texture filenames to exclude from base entity JSON (handled by their own variant file)
EXCLUDED_TEXTURE_PREFIXES: dict[str, list[str]] = {
    "cow":      ["cold_", "warm_"],
    "chicken":  ["cold_"],
    "pig":      ["cold_"],
    "zombie":   ["drowned"],
    "skeleton": ["bogged", "parched", "stray", "wither_"],
    "slime":    ["magmacube"],
}

VANILLA_TEXTURE_DIR = DEV_DIR / "vanilla" / "minecraft" / "textures" / "entity"
VANILLA_BLOCK_TEXTURE_DIR = DEV_DIR / "vanilla" / "minecraft" / "textures" / "block"
BLOCK_MODEL_JSON_DIR = DEV_DIR / "vanilla" / "minecraft" / "models" / "block"

# Maps entity_key -> block model template name(s) to extract UV regions from.
# A list of templates merges regions from multiple models (e.g., standing + hanging variants).
# texture_size is always [16, 16] (single animation frame) for block models.
BLOCK_MODELS: dict[str, str | list[str]] = {
    "lantern": ["template_lantern", "template_hanging_lantern"],
}


def discover_entity_textures(entity_key: str) -> list[str]:
    """Discover vanilla texture files for `entity_key` under `vanilla/minecraft/textures/entity`.

    Returns list of texture identifiers like 'minecraft/textures/entity/<file>' or
    'minecraft/textures/entity/<subdir>/<file>' or nested variants.
    Recursively discovers PNG files in nested subdirectories.

    Region JSON Format:
    {
      "entity_id": {
        "texture_size": [W, H],
        "regions": [
          {
            "name": "part_name",
            "x": X, "y": Y, "width": W, "height": H,
            "flip": "v" | "h" | "hv"  // optional, default: no flip
          },
          ...
        ],
        "textures": ["minecraft/textures/entity/..."]
      }
    }

    Flip values:
    - "v": vertical flip (upside down)
    - "h": horizontal flip (mirror left-right)
    - "hv" or "vh": both flips
    """
    textures: list[str] = []
    excluded_prefixes = EXCLUDED_TEXTURE_PREFIXES.get(entity_key, [])

    # Determine which files to include/exclude based on entity_key
    def should_include_file(filename: str, entity_key: str) -> bool:
        """Determine if a texture file should be included for this entity."""
        # Chest variants: filter by suffix
        if entity_key == "chest":
            # Exclude _left and _right variants
            return not (filename.endswith("_left.png") or filename.endswith("_right.png"))
        elif entity_key == "chest_left":
            # Only include _left variants
            return filename.endswith("_left.png")
        elif entity_key == "chest_right":
            # Only include _right variants
            return filename.endswith("_right.png")
        # Pufferfish variants: all use same pufferfish texture
        elif entity_key in ("pufferfish_large", "pufferfish_medium", "pufferfish_small"):
            return filename == "pufferfish.png"
        # Tropical fish variants: large uses tropical_a and tropical_b, small is subset
        elif entity_key in ("tropical_fish_large", "tropical_fish_small"):
            return filename.startswith("tropical_")
        # Sheep wool: only include the wool textures
        elif entity_key == "sheep_wool":
            return filename.startswith("sheep_wool")
        # Warm cow: only include warm_cow texture
        elif entity_key == "warm_cow":
            return filename == "warm_cow.png"
        # Bed variants: both parts share all colors (intentional)
        # Cat/ocelot: handled separately, both use same texture
        return True

    # Map texture directory for variants
    # Some entities look for textures in parent directories or renamed directories
    texture_dir_key = entity_key
    if entity_key in ("chest_left", "chest_right"):
        texture_dir_key = "chest"
    elif entity_key in ("pufferfish_large", "pufferfish_medium", "pufferfish_small"):
        texture_dir_key = "fish"
    elif entity_key in ("tropical_fish_large", "tropical_fish_small"):
        texture_dir_key = "fish"
    elif entity_key == "sheep_wool":
        texture_dir_key = "sheep"
    elif entity_key == "warm_cow":
        texture_dir_key = "cow"

    # Check subdirectory first (e.g. vanilla/minecraft/textures/entity/wolf/*.png)
    subdir = VANILLA_TEXTURE_DIR / texture_dir_key
    if subdir.exists() and subdir.is_dir():
        # Recursively discover all PNG files in this directory and nested subdirectories
        def find_pngs_recursive(path, rel_prefix=""):
            results = []
            for item in sorted(path.iterdir()):
                if item.suffix.lower() == '.png':
                    # Skip .mcmeta files
                    if item.name.endswith('.mcmeta'):
                        continue
                    if not any(item.name.startswith(p) for p in excluded_prefixes):
                        if should_include_file(item.name, entity_key):
                            if rel_prefix:
                                results.append(f"minecraft/textures/entity/{texture_dir_key}/{rel_prefix}/{item.name}")
                            else:
                                results.append(f"minecraft/textures/entity/{texture_dir_key}/{item.name}")
                elif item.is_dir():
                    # Recursively search subdirectories
                    if rel_prefix:
                        new_prefix = f"{rel_prefix}/{item.name}"
                    else:
                        new_prefix = item.name
                    results.extend(find_pngs_recursive(item, new_prefix))
            return results

        textures = find_pngs_recursive(subdir)
        if textures:
            return textures

    # Fallback: look for files in the entity textures folder that start with the key
    if VANILLA_TEXTURE_DIR.exists():
        for f in sorted(VANILLA_TEXTURE_DIR.iterdir()):
            if f.is_file() and f.suffix.lower() == '.png':
                if f.name.startswith(entity_key):
                    if not any(f.name.startswith(p) for p in excluded_prefixes):
                        textures.append(f"minecraft/textures/entity/{f.name}")

    # Cold variants: look for texture in parent entity's subdir (e.g. entity/cow/cold_cow.png)
    if not textures and entity_key in COLD_VARIANT_PARENTS:
        parent_key = COLD_VARIANT_PARENTS[entity_key]
        texture_path = VANILLA_TEXTURE_DIR / parent_key / f"{entity_key}.png"
        if texture_path.exists():
            textures.append(f"minecraft/textures/entity/{parent_key}/{entity_key}.png")

    return textures

def load_cross_file_model_code(parent_class: str) -> str | None:
    """Load Java source for a parent model class (for cross-file inheritance)."""
    f = MODEL_DIR / f"{parent_class}.java"
    if f.exists():
        return f.read_text()
    return None

# ─── Block model JSON extraction ──────────────────────────────────────────────

def _load_block_model_json(model_name: str) -> dict | None:
    """Load a vanilla block model JSON by name (strips 'minecraft:block/' prefix)."""
    name = model_name.removeprefix("minecraft:block/").removeprefix("block/")
    path = BLOCK_MODEL_JSON_DIR / f"{name}.json"
    if not path.exists():
        return None
    try:
        return json.loads(path.read_text())
    except Exception:
        return None


def _collect_elements_from_template(template_name: str, visited: set | None = None) -> list[dict]:
    """Walk the parent chain of a block model and return its 'elements' list.

    Elements are defined at the first ancestor that declares them; child models
    that add textures but no elements (the common pattern) are transparent.
    """
    if visited is None:
        visited = set()
    name = template_name.removeprefix("minecraft:block/").removeprefix("block/")
    if name in visited:
        return []
    visited.add(name)

    model = _load_block_model_json(name)
    if model is None:
        return []

    if "elements" in model:
        return model["elements"]

    parent = model.get("parent", "")
    if not parent or parent in ("block/block", "minecraft:block/block"):
        return []
    return _collect_elements_from_template(parent, visited)


def _uv_to_region(uv: list, name: str) -> dict | None:
    """Convert a Minecraft block model UV quad [x1, y1, x2, y2] to an EntityRegion dict.

    UV coordinates are in [0, 16] space (matching the 16×16 texture frame).
    Reversed axes (x1 > x2 or y1 > y2) indicate mirrored faces and produce a flip tag.
    """
    if len(uv) < 4:
        return None
    x1, y1, x2, y2 = float(uv[0]), float(uv[1]), float(uv[2]), float(uv[3])
    x = min(x1, x2)
    y = min(y1, y2)
    w = abs(x2 - x1)
    h = abs(y2 - y1)
    if w < 0.5 or h < 0.5:
        return None
    region: dict = {
        "name": name,
        "x": round(x),
        "y": round(y),
        "width": round(w),
        "height": round(h),
    }
    flip_h = x1 > x2
    flip_v = y1 > y2
    if flip_h and flip_v:
        region["flip"] = "hv"
    elif flip_h:
        region["flip"] = "h"
    elif flip_v:
        region["flip"] = "v"
    return region


def _discover_block_model_textures(template_names: list[str]) -> list[str]:
    """Find all animated block texture files whose model chain references any of the given templates.

    Scans vanilla/minecraft/models/block/ for models whose parent is one of the templates,
    extracts their texture variable values, and returns the paths to animated PNG files
    (those with a companion .mcmeta file).
    """
    template_refs: set[str] = set()
    for t in template_names:
        n = t.removeprefix("minecraft:block/").removeprefix("block/")
        template_refs.add(f"block/{n}")
        template_refs.add(f"minecraft:block/{n}")

    textures: list[str] = []
    if not BLOCK_MODEL_JSON_DIR.exists():
        return textures

    for model_file in sorted(BLOCK_MODEL_JSON_DIR.glob("*.json")):
        try:
            data = json.loads(model_file.read_text())
        except Exception:
            continue
        if data.get("parent") not in template_refs:
            continue
        for var_value in data.get("textures", {}).values():
            tex_name = var_value.removeprefix("minecraft:block/").removeprefix("block/")
            tex_png = VANILLA_BLOCK_TEXTURE_DIR / f"{tex_name}.png"
            mcmeta = VANILLA_BLOCK_TEXTURE_DIR / f"{tex_name}.png.mcmeta"
            if tex_png.exists() and mcmeta.exists():
                key = f"minecraft/textures/block/{tex_name}.png"
                if key not in textures:
                    textures.append(key)

    return textures


def extract_block_model(entity_key: str, verbose: bool = False) -> dict | None:
    """Generate entity regions data for entity_key from one or more block model templates.

    Regions are extracted from the element UV quads defined in the template model JSON.
    Duplicate UV regions (same x, y, width, height, flip) are deduplicated so each unique
    texture area appears only once. texture_size is always [16, 16] (one animation frame).
    """
    template_val = BLOCK_MODELS.get(entity_key)
    if template_val is None:
        return None
    template_names = [template_val] if isinstance(template_val, str) else list(template_val)

    # Collect elements from all listed templates (dedup by UV region)
    regions: list[dict] = []
    seen: set[tuple] = set()

    multi = len(template_names) > 1
    for t_idx, template_name in enumerate(template_names):
        elements = _collect_elements_from_template(template_name)
        if not elements and verbose:
            print(f"  [WARN] {template_name}: no elements found")

        for elem_idx, element in enumerate(elements):
            faces = element.get("faces", {})
            for face_dir, face_data in sorted(faces.items()):
                uv = face_data.get("uv")
                if uv is None:
                    continue
                # Use template prefix when merging multiple templates to avoid name collisions
                name = f"t{t_idx}_elem{elem_idx}_{face_dir}" if multi else f"elem{elem_idx}_{face_dir}"
                region = _uv_to_region(uv, name)
                if region is None:
                    continue
                key = (region["x"], region["y"], region["width"], region["height"], region.get("flip"))
                if key in seen:
                    continue
                seen.add(key)
                regions.append(region)

    if not regions:
        print(f"  [WARN] {entity_key}: no regions extracted from {template_names}")
        return None

    textures = _discover_block_model_textures(template_names)
    if not textures:
        print(f"  [WARN] {entity_key}: no animated textures found for templates {template_names}")

    if verbose:
        print(f"  → {len(regions)} regions, {len(textures)} texture(s)")

    return {
        entity_key: {
            "texture_size": [16, 16],
            "regions": regions,
            "textures": textures,
        }
    }

# ─── Main extraction logic ────────────────────────────────────────────────────

def extract_model(model_class_name: str, verbose: bool = False) -> dict | None:
    """
    Extract entity region data from a model Java file.
    Returns a dict with 'texture_size', 'regions', and 'textures' keys,
    keyed under the entity name.
    """
    # Handle block entity renderers with suffixes (e.g., BedBlockEntityRenderer_head, ChestBlockModel_left)
    search_class_name = model_class_name
    method_suffix = None
    if "_" in model_class_name:
        parts = model_class_name.rsplit("_", 1)
        potential_suffix = parts[1]
        if potential_suffix in ("head", "foot", "left", "right", "chest"):
            search_class_name = parts[0]
            method_suffix = potential_suffix

    java_file = MODEL_DIR / f"{search_class_name}.java"
    if not java_file.exists():
        java_file = BLOCK_ENTITY_MODEL_DIR / f"{search_class_name}.java"
    if not java_file.exists():
        java_file = BLOCK_ENTITY_RENDERER_DIR / f"{search_class_name}.java"
    if not java_file.exists():
        java_file = RENDERER_DIR / f"{search_class_name}.java"
    if not java_file.exists():
        print(f"  [SKIP] {search_class_name} not found in model directories")
        return None

    code = java_file.read_text()
    entity_key = MODEL_TO_ENTITY.get(model_class_name, model_class_name.replace("EntityModel", "").lower())

    # ── Find getTexturedModelData() ───────────────────────────────────────────
    # Accept various signatures; some models return ModelData instead of TexturedModelData.
    method_body = None
    method_source = code  # the source to search for the method

    # For suffixed models (head/foot for bed, left/right/chest for chest, chest for boat), search for specific method names
    if method_suffix:
        # Special case mapping for different suffix types
        if method_suffix == "left" and search_class_name == "ChestBlockModel":
            method_name = "getDoubleChestLeftTexturedBlockData"
        elif method_suffix == "right" and search_class_name == "ChestBlockModel":
            method_name = "getDoubleChestRightTexturedBlockData"
        elif method_suffix == "chest":
            method_name = "getChestTexturedModelData"
        else:
            method_name = f"get{method_suffix.capitalize()}TexturedModelData"

        sigs = [
            f"public static TexturedModelData {method_name}()",
            f"public static TexturedModelData {method_name}(",
            f"public static ModelData {method_name}(",
        ]
    else:
        sigs = [
            "public static TexturedModelData getTexturedModelData()",
            "public static TexturedModelData getTexturedModelData(",
            "public static ModelData getTexturedModelData(",  # wolf, player, etc.
            "public static ModelData getModelData(",  # VillagerResemblingModel, etc.
        ]
        # Special cases for models with different method names
        if search_class_name == "ChestBlockModel":
            sigs.insert(0, "public static TexturedModelData getSingleTexturedModelData()")
            sigs.insert(1, "public static TexturedModelData getSingleTexturedModelData(")
        elif search_class_name == "DragonEntityModel":
            sigs.insert(0, "public static TexturedModelData createTexturedModelData()")
            sigs.insert(1, "public static TexturedModelData createTexturedModelData(")
        elif search_class_name == "SlimeEntityModel":
            sigs.insert(0, "public static TexturedModelData getOuterTexturedModelData()")
            sigs.insert(1, "public static TexturedModelData getOuterTexturedModelData(")
            sigs.insert(2, "public static TexturedModelData getInnerTexturedModelData()")
            sigs.insert(3, "public static TexturedModelData getInnerTexturedModelData(")

    for sig in sigs:
        method_body = extract_method_body(method_source, sig)
        if method_body is not None:
            break

    # For cross-file parents: if no getTexturedModelData(), try parent model files
    if method_body is None:
        parents = CROSS_FILE_PARENTS.get(model_class_name, [])
        for parent_class in parents:
            parent_code = load_cross_file_model_code(parent_class)
            if parent_code is None:
                continue
            for sig in [
                "public static TexturedModelData getTexturedModelData(",
                "public static ModelData getTexturedModelData(",
                "public static ModelData getModelData(",
            ]:
                method_body = extract_method_body(parent_code, sig)
                if method_body is not None:
                    method_source = parent_code  # helpers are in parent file
                    if verbose:
                        print(f"  Using parent model: {parent_class}")
                    break
            if method_body is not None:
                break

    # ── Allow biped-inheriting models to skip method body (they only use biped base) ──
    if method_body is None and model_class_name not in BIPED_INHERITING:
        print(f"  [SKIP] {model_class_name}: no getTexturedModelData()")
        return None

    # ── Texture size ──────────────────────────────────────────────────────────
    if model_class_name in KNOWN_TEXTURE_SIZES:
        tex_w, tex_h = KNOWN_TEXTURE_SIZES[model_class_name]
    else:
        tex_size_match = re.search(r'TexturedModelData\.of\([^,\n]+,\s*(\d+),\s*(\d+)\)', method_body) if method_body else None
        if tex_size_match:
            tex_w, tex_h = int(tex_size_match.group(1)), int(tex_size_match.group(2))
        else:
            # Search whole file (handles cases where size is in helper or EntityModels)
            tex_size_match = re.search(r'TexturedModelData\.of\([^,\n]+,\s*(\d+),\s*(\d+)\)', code)
            if tex_size_match:
                tex_w, tex_h = int(tex_size_match.group(1)), int(tex_size_match.group(2))
            elif method_source is not code:
                tex_size_match = re.search(r'TexturedModelData\.of\([^,\n]+,\s*(\d+),\s*(\d+)\)', method_source)
                if tex_size_match:
                    tex_w, tex_h = int(tex_size_match.group(1)), int(tex_size_match.group(2))
                else:
                    print(f"  [WARN] {model_class_name}: could not find texture size, defaulting to 64x64")
                    tex_w, tex_h = 64, 64
            else:
                print(f"  [WARN] {model_class_name}: could not find texture size, defaulting to 64x64")
                tex_w, tex_h = 64, 64

    # ── Seed regions from biped base if applicable ────────────────────────────
    regions: list[dict] = []
    biped_part_names: set[str] = set()
    if model_class_name in BIPED_INHERITING:
        biped_regions = get_biped_regions()
        regions.extend(biped_regions)
        biped_part_names = {r["name"].rsplit("_", 1)[0] for r in biped_regions}
        if verbose:
            print(f"  Seeded {len(biped_regions)} biped base regions")

    # ── Build full search code ────────────────────────────────────────────────
    # Include the method body plus any static helper methods it calls.
    # Also include static ModelData-returning helpers (like getModelData()).
    SKIP_IDENTIFIERS = {
        'addChild', 'create', 'uv', 'cuboid', 'mirrored', 'new', 'of', 'origin',
        'NONE', 'return', 'if', 'for', 'while', 'switch', 'case', 'TexturedModelData',
        'ModelData', 'ModelPartData', 'ModelPartBuilder', 'ModelTransform', 'Dilation',
        'BipedEntityModel', 'QuadrupedEntityModel', 'AbstractHorseEntityModel',
        'getRoot', 'getChild', 'make', 'put', 'get', 'values', 'size', 'scaling',
        'Math', 'MathHelper', 'Set', 'Map', 'List', 'String', 'Locale', 'format',
    }
    full_search_code = method_body or ""

    # Search both own file and parent source file for helpers
    sources_to_search = [code, method_source] if method_source is not code else [code]
    if method_body:
        for helper_match in re.finditer(r'\b([A-Za-z][A-Za-z0-9_]*)\s*\(', method_body):
            helper_name = helper_match.group(1)
            if helper_name in SKIP_IDENTIFIERS or helper_name[0].isupper():
                continue
            # Try to find this as a static helper method (returns void or ModelData)
            found_helper = False
            for src in sources_to_search:
                for ret_type in ("void", "ModelData", "ModelPartData"):
                    for visibility in ("protected static", "private static", "static", "public static"):
                        sig = f"{visibility} {ret_type} {helper_name}("
                        hbody = extract_method_body(src, sig)
                        if hbody:
                            full_search_code += "\n" + hbody
                            if verbose:
                                print(f"  Included helper method: {helper_name}()")
                            found_helper = True
                            break
                    if found_helper:
                        break
                if found_helper:
                    break

    var_assignments = find_builder_assignments(full_search_code)
    if verbose and var_assignments:
        print(f"  Found {len(var_assignments)} builder variable(s): {list(var_assignments.keys())}")

    # ── Find all addChild calls ───────────────────────────────────────────────
    add_child_calls = find_add_child_calls(full_search_code)
    if verbose:
        print(f"  Found {len(add_child_calls)} addChild() call(s)")

    # Group cuboids by part name (a part may have multiple cuboids)
    part_cuboids: dict[str, list[tuple]] = {}
    for part_expr, builder_expr in add_child_calls:
        part_name = resolve_part_name(part_expr)
        cuboids = parse_builder_cuboids(builder_expr, var_assignments)
        if cuboids:
            if part_name not in part_cuboids:
                part_cuboids[part_name] = []
            part_cuboids[part_name].extend(cuboids)
        elif verbose:
            print(f"  [WARN] No cuboids parsed for part '{part_name}' (builder: {builder_expr[:60]}...)")

    # ── Build regions ─────────────────────────────────────────────────────────
    # If biped parts exist, only add/replace parts that are explicitly defined here
    seen_parts: set[str] = set()
    new_regions: list[dict] = []

    for part_name, cuboids in part_cuboids.items():
        num = len(cuboids)
        part_regions = []
        for i, (u, v, sx, sy, sz, mirrored) in enumerate(cuboids):
            part_regions.extend(compute_face_regions(u, v, sx, sy, sz, part_name, i, num))
        if part_regions:
            seen_parts.add(part_name)
            new_regions.extend(part_regions)

    # Merge: if biped base was seeded, remove biped regions for overridden parts
    if model_class_name in BIPED_INHERITING:
        # Remove biped regions for any part that is redefined
        regions = [r for r in regions
                   if r["name"].rsplit("_", 1)[0] not in seen_parts or
                   r["name"].rsplit("_", 1)[0] not in biped_part_names]

    regions.extend(new_regions)

    # ── Hardcoded fixes for extraction issues ──────────────────────────────────
    # (Apply BEFORE checking if regions is empty)

    # Blaze: Fix getRodName(i) references - rename to part0 through part11
    if entity_key == "blaze" and any(r["name"].startswith("getRodName(i)") for r in regions):
        # Rename the getRodName(i)_0 parts to part0, and duplicate for part1-11
        getRodName_regions = [r for r in regions if r["name"].startswith("getRodName(i)")]
        regions = [r for r in regions if not r["name"].startswith("getRodName(i)")]

        # Create all 12 parts (getRodName(i) generates one part via cuboid, replicate for i=0..11)
        for part_idx in range(12):
            for region in getRodName_regions:
                new_region = region.copy()
                new_name = region["name"].replace("getRodName(i)_0_", f"part{part_idx}_")
                new_region["name"] = new_name
                regions.append(new_region)

        if verbose:
            print(f"  [HARDFIX] Blaze: generated all 12 parts from getRodName(i) (now {len(regions)} regions)")

    # MagmaCube: Fix getSliceName(i) references - rename to cube0 through cube7 with correct UVs
    if entity_key == "magma_cube" and any(r["name"].startswith("getSliceName(i)") for r in regions):
        # Rename the getSliceName(i)_* parts to cube0_*, cube1_*, etc.
        # The Java loop computes UV coordinates as: j=0/32, k=0/9/18/27/36/45/54/63
        getSliceName_regions = [r for r in regions if r["name"].startswith("getSliceName(i)")]
        regions = [r for r in regions if not r["name"].startswith("getSliceName(i)")]

        # UV offsets for each cube iteration (from Java loop logic)
        # for (int i = 0; i < 8; i++):
        #   if (i > 0 && i < 4) k += 9 * i;
        #   else if (i > 3) { j = 32; k += 9 * i - 36; }
        uv_offsets = [
            (0, 0),    # i=0: j=0, k=0
            (0, 9),    # i=1: j=0, k=9
            (0, 18),   # i=2: j=0, k=18
            (0, 27),   # i=3: j=0, k=27
            (32, 0),   # i=4: j=32, k=36-36=0
            (32, 9),   # i=5: j=32, k=45-36=9
            (32, 18),  # i=6: j=32, k=54-36=18
            (32, 27),  # i=7: j=32, k=63-36=27
        ]

        # Create all 8 cubes with adjusted UV coordinates
        for cube_idx, (uv_j, uv_k) in enumerate(uv_offsets):
            for region in getSliceName_regions:
                new_region = region.copy()
                new_name = region["name"].replace("getSliceName(i)_", f"cube{cube_idx}_")
                new_region["name"] = new_name
                # Adjust x, y coordinates based on the UV offset
                # The extracted region uses UV (0, 0), so we need to add the actual UV offset
                new_region["x"] += uv_j
                new_region["y"] += uv_k
                regions.append(new_region)

        if verbose:
            print(f"  [HARDFIX] MagmaCube: generated all 8 cubes from getSliceName(i) with correct UVs (now {len(regions)} regions)")

    # Slime: Add inner structure (eyes + mouth) which are in getInnerTexturedModelData()
    if entity_key == "slime" and any(r["name"] == "cube_top" for r in regions):
        # Extract the inner slime structure (cube + eyes + mouth)
        # Hardcoded from SlimeEntityModel.getInnerTexturedModelData()
        inner_regions = [
            # Inner cube: uv(0, 16), sx=6, sy=6, sz=6
            {"name": "inner_cube_top", "x": 6, "y": 16, "width": 6, "height": 6},
            {"name": "inner_cube_bottom", "x": 12, "y": 16, "width": 6, "height": 6},
            {"name": "inner_cube_right", "x": 0, "y": 22, "width": 6, "height": 6},
            {"name": "inner_cube_front", "x": 6, "y": 22, "width": 6, "height": 6},
            {"name": "inner_cube_left", "x": 12, "y": 22, "width": 6, "height": 6},
            {"name": "inner_cube_back", "x": 18, "y": 22, "width": 6, "height": 6},
            # Right eye: uv(32, 0), sx=2, sy=2, sz=2
            {"name": "right_eye_top", "x": 34, "y": 0, "width": 2, "height": 2},
            {"name": "right_eye_bottom", "x": 36, "y": 0, "width": 2, "height": 2},
            {"name": "right_eye_right", "x": 32, "y": 2, "width": 2, "height": 2},
            {"name": "right_eye_front", "x": 34, "y": 2, "width": 2, "height": 2},
            {"name": "right_eye_left", "x": 36, "y": 2, "width": 2, "height": 2},
            {"name": "right_eye_back", "x": 38, "y": 2, "width": 2, "height": 2},
            # Left eye: uv(32, 4), sx=2, sy=2, sz=2
            {"name": "left_eye_top", "x": 34, "y": 4, "width": 2, "height": 2},
            {"name": "left_eye_bottom", "x": 36, "y": 4, "width": 2, "height": 2},
            {"name": "left_eye_right", "x": 32, "y": 6, "width": 2, "height": 2},
            {"name": "left_eye_front", "x": 34, "y": 6, "width": 2, "height": 2},
            {"name": "left_eye_left", "x": 36, "y": 6, "width": 2, "height": 2},
            {"name": "left_eye_back", "x": 38, "y": 6, "width": 2, "height": 2},
            # Mouth: uv(32, 8), sx=1, sy=1, sz=1
            {"name": "mouth_top", "x": 33, "y": 8, "width": 1, "height": 1},
            {"name": "mouth_bottom", "x": 34, "y": 8, "width": 1, "height": 1},
            {"name": "mouth_right", "x": 32, "y": 9, "width": 1, "height": 1},
            {"name": "mouth_front", "x": 33, "y": 9, "width": 1, "height": 1},
            {"name": "mouth_left", "x": 34, "y": 9, "width": 1, "height": 1},
            {"name": "mouth_back", "x": 35, "y": 9, "width": 1, "height": 1},
        ]
        regions.extend(inner_regions)
        if verbose:
            print(f"  [HARDFIX] Slime: added inner cube + eyes + mouth (now {len(regions)} regions)")

    # Cow: Add missing legs (left_hind, right_front, left_front)
    if entity_key == "cow":
        if any(r["name"] == "right_hind_leg_top" for r in regions):
            right_hind = [r for r in regions if r["name"].startswith("right_hind_leg_")]
            # Add left_hind_leg (mirrored: needs different x coordinates)
            for region in right_hind:
                new_region = region.copy()
                new_region["name"] = region["name"].replace("right_hind_leg_", "left_hind_leg_")
                # Mirror by shifting x - for 64-wide texture, mirror around center
                # right_hind starts at x=4 or higher; mirror by: new_x = tex_w - old_x - old_width
                new_region["x"] = tex_w - region["x"] - region["width"]
                regions.append(new_region)

            # Add front legs by copying hind legs (coords differ but same naming pattern)
            # Right front leg: using standard biped-like coords for front
            right_front_template = [
                {"name": "right_front_leg_top", "x": 4, "y": 16, "width": 4, "height": 4},
                {"name": "right_front_leg_bottom", "x": 8, "y": 16, "width": 4, "height": 4},
                {"name": "right_front_leg_right", "x": 0, "y": 20, "width": 4, "height": 12},
                {"name": "right_front_leg_front", "x": 4, "y": 20, "width": 4, "height": 12},
                {"name": "right_front_leg_left", "x": 8, "y": 20, "width": 4, "height": 12},
                {"name": "right_front_leg_back", "x": 12, "y": 20, "width": 4, "height": 12},
            ]
            regions.extend(right_front_template)

            # Left front leg (mirrored x)
            for template in right_front_template:
                new_region = template.copy()
                new_region["name"] = template["name"].replace("right_front_leg_", "left_front_leg_")
                new_region["x"] = tex_w - template["x"] - template["width"]
                regions.append(new_region)

            if verbose:
                print(f"  [HARDFIX] {entity_key.capitalize()}: added missing legs (now {len(regions)} regions)")

    # Pig: Add missing legs (all 4)
    if entity_key == "pig" and not any(r["name"].startswith("right_hind_leg_") for r in regions):
        # Add all 4 legs using standard quad coords
        pig_legs = [
            {"name": "right_hind_leg_top", "x": 4, "y": 16, "width": 4, "height": 4},
            {"name": "right_hind_leg_bottom", "x": 8, "y": 16, "width": 4, "height": 4},
            {"name": "right_hind_leg_right", "x": 0, "y": 20, "width": 4, "height": 12},
            {"name": "right_hind_leg_front", "x": 4, "y": 20, "width": 4, "height": 12},
            {"name": "right_hind_leg_left", "x": 8, "y": 20, "width": 4, "height": 12},
            {"name": "right_hind_leg_back", "x": 12, "y": 20, "width": 4, "height": 12},
        ]
        regions.extend(pig_legs)

        # Left hind leg
        for leg in pig_legs:
            new_leg = leg.copy()
            new_leg["name"] = leg["name"].replace("right_hind_leg_", "left_hind_leg_")
            new_leg["x"] = tex_w - leg["x"] - leg["width"]
            regions.append(new_leg)

        # Front legs (same pattern, just different texel offset)
        pig_front_legs = [
            {"name": "right_front_leg_top", "x": 4, "y": 16, "width": 4, "height": 4},
            {"name": "right_front_leg_bottom", "x": 8, "y": 16, "width": 4, "height": 4},
            {"name": "right_front_leg_right", "x": 0, "y": 20, "width": 4, "height": 12},
            {"name": "right_front_leg_front", "x": 4, "y": 20, "width": 4, "height": 12},
            {"name": "right_front_leg_left", "x": 8, "y": 20, "width": 4, "height": 12},
            {"name": "right_front_leg_back", "x": 12, "y": 20, "width": 4, "height": 12},
        ]
        regions.extend(pig_front_legs)

        # Left front leg
        for leg in pig_front_legs:
            new_leg = leg.copy()
            new_leg["name"] = leg["name"].replace("right_front_leg_", "left_front_leg_")
            new_leg["x"] = tex_w - leg["x"] - leg["width"]
            regions.append(new_leg)

        if verbose:
            print(f"  [HARDFIX] Pig: added all 4 legs (now {len(regions)} regions)")

    # Chicken: Add missing left leg and left wing
    if entity_key == "chicken":
        if any(r["name"] == "right_leg_top" for r in regions):
            right_leg = [r for r in regions if r["name"].startswith("right_leg_")]
            for region in right_leg:
                new_region = region.copy()
                new_region["name"] = region["name"].replace("right_leg_", "left_leg_")
                new_region["x"] = tex_w - region["x"] - region["width"]
                regions.append(new_region)

        if any(r["name"] == "right_wing_top" for r in regions):
            right_wing = [r for r in regions if r["name"].startswith("right_wing_")]
            for region in right_wing:
                new_region = region.copy()
                new_region["name"] = region["name"].replace("right_wing_", "left_wing_")
                new_region["x"] = tex_w - region["x"] - region["width"]
                regions.append(new_region)

        if verbose:
            print(f"  [HARDFIX] Chicken: added left leg and wing (now {len(regions)} regions)")


    # Endermite: Hardcoded segments (can't parse array-indexed UVs)
    if entity_key == "endermite" and not regions:
        # 4 segments with specific dimensions and UVs
        endermite_segments = [
            # segment0: u=0, v=0, width=4, height=3, depth=2
            {"name": "segment0_top", "x": 2, "y": 0, "width": 4, "height": 2},
            {"name": "segment0_bottom", "x": 6, "y": 0, "width": 4, "height": 2},
            {"name": "segment0_right", "x": 0, "y": 2, "width": 2, "height": 3},
            {"name": "segment0_front", "x": 2, "y": 2, "width": 4, "height": 3},
            {"name": "segment0_left", "x": 6, "y": 2, "width": 2, "height": 3},
            {"name": "segment0_back", "x": 8, "y": 2, "width": 4, "height": 3},
            # segment1: u=0, v=5, width=6, height=4, depth=5
            {"name": "segment1_top", "x": 3, "y": 5, "width": 6, "height": 5},
            {"name": "segment1_bottom", "x": 9, "y": 5, "width": 6, "height": 5},
            {"name": "segment1_right", "x": 0, "y": 10, "width": 5, "height": 4},
            {"name": "segment1_front", "x": 5, "y": 10, "width": 6, "height": 4},
            {"name": "segment1_left", "x": 11, "y": 10, "width": 5, "height": 4},
            {"name": "segment1_back", "x": 16, "y": 10, "width": 6, "height": 4},
            # segment2: u=0, v=14, width=3, height=3, depth=1
            {"name": "segment2_top", "x": 1, "y": 14, "width": 3, "height": 1},
            {"name": "segment2_bottom", "x": 4, "y": 14, "width": 3, "height": 1},
            {"name": "segment2_right", "x": 0, "y": 15, "width": 1, "height": 3},
            {"name": "segment2_front", "x": 1, "y": 15, "width": 3, "height": 3},
            {"name": "segment2_left", "x": 4, "y": 15, "width": 1, "height": 3},
            {"name": "segment2_back", "x": 5, "y": 15, "width": 3, "height": 3},
            # segment3: u=0, v=18, width=1, height=2, depth=1
            {"name": "segment3_top", "x": 0, "y": 18, "width": 1, "height": 1},
            {"name": "segment3_bottom", "x": 1, "y": 18, "width": 1, "height": 1},
            {"name": "segment3_right", "x": 0, "y": 19, "width": 1, "height": 2},
            {"name": "segment3_front", "x": 1, "y": 19, "width": 1, "height": 2},
            {"name": "segment3_left", "x": 2, "y": 19, "width": 1, "height": 2},
            {"name": "segment3_back", "x": 3, "y": 19, "width": 1, "height": 2},
        ]
        regions.extend(endermite_segments)
        if verbose:
            print(f"  [HARDFIX] Endermite: added 4 segments (now {len(regions)} regions)")

    # Silverfish: Hardcoded segments and layers (can't parse array-indexed UVs)
    if entity_key == "silverfish" and not regions:
        # 7 segments
        silverfish_parts = [
            # segment0: u=0, v=0, w=3, h=2, d=2
            {"name": "segment0_top", "x": 1, "y": 0, "width": 3, "height": 2},
            {"name": "segment0_bottom", "x": 4, "y": 0, "width": 3, "height": 2},
            {"name": "segment0_right", "x": 0, "y": 2, "width": 2, "height": 2},
            {"name": "segment0_front", "x": 2, "y": 2, "width": 3, "height": 2},
            {"name": "segment0_left", "x": 5, "y": 2, "width": 2, "height": 2},
            {"name": "segment0_back", "x": 7, "y": 2, "width": 3, "height": 2},
            # segment1: u=0, v=4, w=4, h=3, d=2
            {"name": "segment1_top", "x": 2, "y": 4, "width": 4, "height": 2},
            {"name": "segment1_bottom", "x": 6, "y": 4, "width": 4, "height": 2},
            {"name": "segment1_right", "x": 0, "y": 6, "width": 2, "height": 3},
            {"name": "segment1_front", "x": 2, "y": 6, "width": 4, "height": 3},
            {"name": "segment1_left", "x": 6, "y": 6, "width": 2, "height": 3},
            {"name": "segment1_back", "x": 8, "y": 6, "width": 4, "height": 3},
            # segment2: u=0, v=9, w=6, h=4, d=3
            {"name": "segment2_top", "x": 3, "y": 9, "width": 6, "height": 3},
            {"name": "segment2_bottom", "x": 9, "y": 9, "width": 6, "height": 3},
            {"name": "segment2_right", "x": 0, "y": 12, "width": 3, "height": 4},
            {"name": "segment2_front", "x": 3, "y": 12, "width": 6, "height": 4},
            {"name": "segment2_left", "x": 9, "y": 12, "width": 3, "height": 4},
            {"name": "segment2_back", "x": 12, "y": 12, "width": 6, "height": 4},
            # segment3: u=0, v=16, w=3, h=3, d=3
            {"name": "segment3_top", "x": 1, "y": 16, "width": 3, "height": 3},
            {"name": "segment3_bottom", "x": 4, "y": 16, "width": 3, "height": 3},
            {"name": "segment3_right", "x": 0, "y": 19, "width": 3, "height": 3},
            {"name": "segment3_front", "x": 3, "y": 19, "width": 3, "height": 3},
            {"name": "segment3_left", "x": 6, "y": 19, "width": 3, "height": 3},
            {"name": "segment3_back", "x": 9, "y": 19, "width": 3, "height": 3},
            # segment4: u=0, v=22, w=2, h=2, d=3
            {"name": "segment4_top", "x": 1, "y": 22, "width": 2, "height": 3},
            {"name": "segment4_bottom", "x": 3, "y": 22, "width": 2, "height": 3},
            {"name": "segment4_right", "x": 0, "y": 25, "width": 3, "height": 2},
            {"name": "segment4_front", "x": 3, "y": 25, "width": 2, "height": 2},
            {"name": "segment4_left", "x": 5, "y": 25, "width": 3, "height": 2},
            {"name": "segment4_back", "x": 8, "y": 25, "width": 2, "height": 2},
            # segment5: u=11, v=0, w=2, h=1, d=2
            {"name": "segment5_top", "x": 12, "y": 0, "width": 2, "height": 2},
            {"name": "segment5_bottom", "x": 14, "y": 0, "width": 2, "height": 2},
            {"name": "segment5_right", "x": 11, "y": 2, "width": 2, "height": 1},
            {"name": "segment5_front", "x": 13, "y": 2, "width": 2, "height": 1},
            {"name": "segment5_left", "x": 15, "y": 2, "width": 2, "height": 1},
            {"name": "segment5_back", "x": 17, "y": 2, "width": 2, "height": 1},
            # segment6: u=13, v=4, w=1, h=1, d=2
            {"name": "segment6_top", "x": 13, "y": 4, "width": 1, "height": 2},
            {"name": "segment6_bottom", "x": 14, "y": 4, "width": 1, "height": 2},
            {"name": "segment6_right", "x": 12, "y": 6, "width": 2, "height": 1},
            {"name": "segment6_front", "x": 14, "y": 6, "width": 1, "height": 1},
            {"name": "segment6_left", "x": 15, "y": 6, "width": 2, "height": 1},
            {"name": "segment6_back", "x": 17, "y": 6, "width": 1, "height": 1},
            # layer0: u=20, v=0, w=10, h=8, d=various
            {"name": "layer0_top", "x": 25, "y": 0, "width": 10, "height": 3},
            {"name": "layer0_bottom", "x": 35, "y": 0, "width": 10, "height": 3},
            {"name": "layer0_right", "x": 20, "y": 3, "width": 3, "height": 8},
            {"name": "layer0_front", "x": 23, "y": 3, "width": 10, "height": 8},
            {"name": "layer0_left", "x": 33, "y": 3, "width": 3, "height": 8},
            {"name": "layer0_back", "x": 36, "y": 3, "width": 10, "height": 8},
            # layer1: u=20, v=11, w=6, h=4, d=3
            {"name": "layer1_top", "x": 23, "y": 11, "width": 6, "height": 3},
            {"name": "layer1_bottom", "x": 29, "y": 11, "width": 6, "height": 3},
            {"name": "layer1_right", "x": 20, "y": 14, "width": 3, "height": 4},
            {"name": "layer1_front", "x": 23, "y": 14, "width": 6, "height": 4},
            {"name": "layer1_left", "x": 29, "y": 14, "width": 3, "height": 4},
            {"name": "layer1_back", "x": 32, "y": 14, "width": 6, "height": 4},
            # layer2: u=20, v=18, w=6, h=5, d=2
            {"name": "layer2_top", "x": 23, "y": 18, "width": 6, "height": 2},
            {"name": "layer2_bottom", "x": 29, "y": 18, "width": 6, "height": 2},
            {"name": "layer2_right", "x": 20, "y": 20, "width": 2, "height": 5},
            {"name": "layer2_front", "x": 22, "y": 20, "width": 6, "height": 5},
            {"name": "layer2_left", "x": 28, "y": 20, "width": 2, "height": 5},
            {"name": "layer2_back", "x": 30, "y": 20, "width": 6, "height": 5},
        ]
        regions.extend(silverfish_parts)
        if verbose:
            print(f"  [HARDFIX] Silverfish: added 7 segments + 3 layers (now {len(regions)} regions)")

    # Donkey: Extract horse parts, then add donkey-specific parts
    if entity_key == "donkey":
        # Donkey has 4 extra parts: LEFT_CHEST, RIGHT_CHEST, LEFT_EAR, RIGHT_EAR
        donkey_extras = [
            # LEFT_CHEST: uv(26, 21), 8x8x3
            {"name": "left_chest_top", "x": 30, "y": 21, "width": 8, "height": 3},
            {"name": "left_chest_bottom", "x": 38, "y": 21, "width": 8, "height": 3},
            {"name": "left_chest_right", "x": 26, "y": 24, "width": 3, "height": 8},
            {"name": "left_chest_front", "x": 29, "y": 24, "width": 8, "height": 8},
            {"name": "left_chest_left", "x": 37, "y": 24, "width": 3, "height": 8},
            {"name": "left_chest_back", "x": 40, "y": 24, "width": 8, "height": 8},
            # RIGHT_CHEST: uv(26, 21), 8x8x3 (same UV)
            {"name": "right_chest_top", "x": 30, "y": 21, "width": 8, "height": 3},
            {"name": "right_chest_bottom", "x": 38, "y": 21, "width": 8, "height": 3},
            {"name": "right_chest_right", "x": 26, "y": 24, "width": 3, "height": 8},
            {"name": "right_chest_front", "x": 29, "y": 24, "width": 8, "height": 8},
            {"name": "right_chest_left", "x": 37, "y": 24, "width": 3, "height": 8},
            {"name": "right_chest_back", "x": 40, "y": 24, "width": 8, "height": 8},
            # LEFT_EAR: uv(0, 12), 2x7x1
            {"name": "left_ear_top", "x": 1, "y": 12, "width": 2, "height": 1},
            {"name": "left_ear_bottom", "x": 3, "y": 12, "width": 2, "height": 1},
            {"name": "left_ear_right", "x": 0, "y": 13, "width": 1, "height": 7},
            {"name": "left_ear_front", "x": 1, "y": 13, "width": 2, "height": 7},
            {"name": "left_ear_left", "x": 3, "y": 13, "width": 1, "height": 7},
            {"name": "left_ear_back", "x": 4, "y": 13, "width": 2, "height": 7},
            # RIGHT_EAR: uv(0, 12), 2x7x1 (same UV)
            {"name": "right_ear_top", "x": 1, "y": 12, "width": 2, "height": 1},
            {"name": "right_ear_bottom", "x": 3, "y": 12, "width": 2, "height": 1},
            {"name": "right_ear_right", "x": 0, "y": 13, "width": 1, "height": 7},
            {"name": "right_ear_front", "x": 1, "y": 13, "width": 2, "height": 7},
            {"name": "right_ear_left", "x": 3, "y": 13, "width": 1, "height": 7},
            {"name": "right_ear_back", "x": 4, "y": 13, "width": 2, "height": 7},
        ]
        regions.extend(donkey_extras)
        if verbose:
            print(f"  [HARDFIX] Donkey: added 4 extra parts (now {len(regions)} regions)")

    # Bell: Mark upside-down faces
    # The bell model renders certain faces with vertical flips (upside down in the UV layout)
    if entity_key == "bell":
        for region in regions:
            # Bell bottom, back, left, right, and front faces are rendered upside down
            if region["name"] in ("bell_body_bottom", "bell_body_back", "bell_body_left", "bell_body_right",
                                  "bell_body_front", "bell_base_bottom", "bell_base_back", "bell_base_left",
                                  "bell_base_right", "bell_base_front"):
                region["flip"] = "v"
        if verbose:
            print(f"  [HARDFIX] Bell: marked {sum(1 for r in regions if 'flip' in r)} faces as vertically flipped")

    # Chest: Mark all faces as vertically flipped except lid_top
    # All chest faces (bottom, lid, lock) are rendered upside down except the lid top
    if entity_key in ("chest", "chest_left", "chest_right", "chest_boat"):
        for region in regions:
            if region["name"] != "lid_top" and region["name"].startswith(("bottom_", "lid_", "lock_")):
                region["flip"] = "v"
        if verbose:
            print(f"  [HARDFIX] Chest: marked {sum(1 for r in regions if 'flip' in r)} faces as vertically flipped")

    # Bed: Mark top/bottom faces as vertically flipped, and left/right faces as rotated
    # bed_head: main_top is vertically flipped, main_right rotated CW, main_left rotated CCW
    # bed_foot: main_bottom is vertically flipped, main_right rotated CW, main_left rotated CCW
    if entity_key in ("bed_head", "bed_foot"):
        target_flip_face = "main_top" if entity_key == "bed_head" else "main_bottom"
        for region in regions:
            if region["name"] == target_flip_face:
                region["flip"] = "v"
            elif region["name"] == "main_right":
                region["rotate"] = "cw"
            elif region["name"] == "main_left":
                region["rotate"] = "ccw"
        flip_count = sum(1 for r in regions if r.get("flip"))
        rotate_count = sum(1 for r in regions if r.get("rotate"))
        if verbose:
            print(f"  [HARDFIX] {entity_key}: marked {flip_count} flip(s), {rotate_count} rotation(s)")

    # Cold variants: inherit un-overridden parts (legs, beak, etc.) from parent entity JSON.
    # Uses the base part name (strips cuboid index suffix) to detect which parts are overridden.
    if entity_key in COLD_VARIANT_PARENTS:
        parent_key = COLD_VARIANT_PARENTS[entity_key]
        parent_json_file = OUTPUT_DIR / f"{parent_key}.json"
        if parent_json_file.exists():
            def _base_part(rname: str) -> str:
                """'head_0_top' → 'head', 'right_hind_leg_top' → 'right_hind_leg'."""
                without_face = rname.rsplit("_", 1)[0]
                parts = without_face.rsplit("_", 1)
                return parts[0] if len(parts) == 2 and parts[1].isdigit() else without_face

            defined_bases = {_base_part(r["name"]) for r in regions}
            parent_data = json.loads(parent_json_file.read_text())
            parent_regions = parent_data.get(parent_key, {}).get("regions", [])
            inherited = [r for r in parent_regions if _base_part(r["name"]) not in defined_bases]
            regions = inherited + regions
            if verbose:
                print(f"  [INHERIT] {entity_key}: inherited {len(inherited)} regions from {parent_key}.json")
        else:
            print(f"  [WARN] {entity_key}: parent JSON {parent_key}.json not found; run base entity first")

    # Check if regions is empty after hardcoding
    if not regions:
        print(f"  [WARN] {model_class_name}: no regions extracted")
        return None

    # ── Deduplicate regions (same x,y,w,h) keeping first occurrence ──────────
    seen_rects: set[tuple] = set()
    deduped = []
    for r in regions:
        key = (r["x"], r["y"], r["width"], r["height"])
        if key not in seen_rects:
            seen_rects.add(key)
            deduped.append(r)
    regions = deduped

    # ── Cat and Ocelot: Add missing right legs (after deduplication) ─────────
    # FelineEntityModel defines both left and right legs using the same UV coordinates.
    # Deduplication removes the right legs as "duplicates" since they have identical x,y,w,h.
    # We re-add them after deduplication since they're semantically different parts.
    if entity_key in ("cat", "ocelot"):
        if any(r["name"] == "left_hind_leg_top" for r in regions) and not any(r["name"] == "right_hind_leg_top" for r in regions):
            left_hind = [r for r in regions if r["name"].startswith("left_hind_leg_")]
            for region in left_hind:
                new_region = region.copy()
                new_region["name"] = region["name"].replace("left_hind_leg_", "right_hind_leg_")
                regions.append(new_region)

        if any(r["name"] == "left_front_leg_top" for r in regions) and not any(r["name"] == "right_front_leg_top" for r in regions):
            left_front = [r for r in regions if r["name"].startswith("left_front_leg_")]
            for region in left_front:
                new_region = region.copy()
                new_region["name"] = region["name"].replace("left_front_leg_", "right_front_leg_")
                regions.append(new_region)

        if verbose:
            print(f"  [HARDFIX] {entity_key.capitalize()}: added missing right legs (now {len(regions)} regions)")

    # ── Texture paths ─────────────────────────────────────────────────────────
    textures = find_entity_textures(model_class_name, entity_key)
    # Always try to include any vanilla variants that exist locally
    discovered = discover_entity_textures(entity_key)
    if discovered:
        # Merge while preserving order and deduplicating
        merged = []
        for t in (textures or []) + discovered:
            if t not in merged:
                merged.append(t)
        textures = merged
        if textures:
            print(f"  [INFO] {model_class_name}: using {len(textures)} texture(s) (renderer + vanilla variants)")
    elif not textures:
        print(f"  [WARN] {model_class_name}: No textures extracted")

    # Hardcoded textures for entities that use parent models
    if not textures:
        if entity_key == "donkey":
            textures = ["minecraft/textures/entity/horse/donkey.png", "minecraft/textures/entity/horse/mule.png"]
            print(f"  [HARDFIX] Donkey: added textures from AbstractDonkeyEntityRenderer")
        elif entity_key == "horse":
            # Horse textures from HorseRenderer
            textures = [
                "minecraft/textures/entity/horse/horse_white.png",
                "minecraft/textures/entity/horse/horse_creamy.png",
                "minecraft/textures/entity/horse/horse_chestnut.png",
                "minecraft/textures/entity/horse/horse_brown.png",
                "minecraft/textures/entity/horse/horse_black.png",
                "minecraft/textures/entity/horse/horse_gray.png",
                "minecraft/textures/entity/horse/horse_darkbrown.png",
            ]
            print(f"  [HARDFIX] Horse: added textures from HorseRenderer")
        elif entity_key == "bell":
            textures = ["minecraft/textures/entity/bell/bell_body.png"]
            print(f"  [HARDFIX] Bell: added hardcoded bell texture")
        elif entity_key in ("bed_head", "bed_foot"):
            # Bed has multiple colored variants
            textures = [
                "minecraft/textures/entity/bed/white.png",
                "minecraft/textures/entity/bed/orange.png",
                "minecraft/textures/entity/bed/magenta.png",
                "minecraft/textures/entity/bed/light_blue.png",
                "minecraft/textures/entity/bed/yellow.png",
                "minecraft/textures/entity/bed/lime.png",
                "minecraft/textures/entity/bed/pink.png",
                "minecraft/textures/entity/bed/gray.png",
                "minecraft/textures/entity/bed/light_gray.png",
                "minecraft/textures/entity/bed/cyan.png",
                "minecraft/textures/entity/bed/purple.png",
                "minecraft/textures/entity/bed/blue.png",
                "minecraft/textures/entity/bed/brown.png",
                "minecraft/textures/entity/bed/green.png",
                "minecraft/textures/entity/bed/red.png",
                "minecraft/textures/entity/bed/black.png",
            ]
            print(f"  [HARDFIX] {entity_key}: added all bed color variants")

    if verbose:
        print(f"  → {len(regions)} regions, {len(textures)} texture(s)")

    return {
        entity_key: {
            "texture_size": [tex_w, tex_h],
            "regions": regions,
            "textures": textures,
        }
    }

# ─── CLI ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Extract entity UV regions from game_decomp")
    parser.add_argument("--entity", help="Only process this model class name (e.g. AxolotlEntityModel)")
    parser.add_argument("--dry-run", action="store_true", help="Print output but don't write files")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")
    parser.add_argument("--overwrite", action="store_true", help="Overwrite existing JSON files")
    args = parser.parse_args()

    load_part_name_constants()
    print(f"Loaded {len(PART_NAME_CONSTANTS)} EntityModelPartNames constants")

    OUTPUT_DIR.mkdir(exist_ok=True)

    # Determine which entity models and block models to process
    is_block_model_key = args.entity in BLOCK_MODELS if args.entity else False

    targets = list(MODEL_TO_ENTITY.keys())
    if args.entity:
        if args.entity not in MODEL_TO_ENTITY and not is_block_model_key:
            print(f"Unknown entity/block model: {args.entity}")
            print(f"Known entity models: {', '.join(sorted(MODEL_TO_ENTITY.keys()))}")
            print(f"Known block models: {', '.join(sorted(BLOCK_MODELS.keys()))}")
            sys.exit(1)
        targets = [args.entity] if args.entity in MODEL_TO_ENTITY else []

    block_model_targets = list(BLOCK_MODELS.keys())
    if args.entity:
        block_model_targets = [args.entity] if is_block_model_key else []

    ok_count = 0
    skip_count = 0
    warn_count = 0

    for model_class in targets:
        entity_key = MODEL_TO_ENTITY[model_class]
        out_file = OUTPUT_DIR / f"{entity_key}.json"

        if out_file.exists() and not args.overwrite and not args.entity:
            if args.verbose:
                print(f"[SKIP] {entity_key}.json already exists (use --overwrite)")
            skip_count += 1
            continue

        print(f"\nProcessing {model_class} → {entity_key}.json ...")
        data = extract_model(model_class, verbose=args.verbose)

        if data is None:
            warn_count += 1
            continue

        if args.dry_run:
            print(json.dumps(data, indent=2))
        else:
            out_file.write_text(json.dumps(data, indent=2) + "\n")
            print(f"  Written: {out_file}")
            ok_count += 1

    # Process block model JSON templates
    for entity_key in block_model_targets:
        out_file = OUTPUT_DIR / f"{entity_key}.json"

        if out_file.exists() and not args.overwrite and not args.entity:
            if args.verbose:
                print(f"[SKIP] {entity_key}.json already exists (use --overwrite)")
            skip_count += 1
            continue

        templates = BLOCK_MODELS[entity_key]
        template_str = templates if isinstance(templates, str) else " + ".join(templates)
        print(f"\nProcessing block model {template_str} → {entity_key}.json ...")
        data = extract_block_model(entity_key, verbose=args.verbose)

        if data is None:
            warn_count += 1
            continue

        if args.dry_run:
            print(json.dumps(data, indent=2))
        else:
            out_file.write_text(json.dumps(data, indent=2) + "\n")
            print(f"  Written: {out_file}")
            ok_count += 1

    print(f"\n{'─'*40}")
    print(f"Done: {ok_count} written, {skip_count} skipped, {warn_count} warnings/errors")

if __name__ == "__main__":
    main()
