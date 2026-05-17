#!/usr/bin/env python3
"""Analyze entity_regions for missing textures and shared texture usage."""

import json
import os
from pathlib import Path
from collections import defaultdict

def analyze_entity_textures(entity_regions_dir):
    """Analyze entity_regions for missing textures and shared textures."""

    missing_textures = []
    texture_to_entities = defaultdict(list)
    total_entities = 0

    # Load all JSON files
    for json_file in sorted(Path(entity_regions_dir).glob("*.json")):
        with open(json_file, 'r') as f:
            data = json.load(f)

        for entity_id, entity_data in data.items():
            total_entities += 1
            textures = entity_data.get("textures", [])

            if not textures:
                missing_textures.append(entity_id)
            else:
                for texture in textures:
                    texture_to_entities[texture].append(entity_id)

    # Find shared textures
    shared_textures = {tex: entities for tex, entities in texture_to_entities.items()
                      if len(entities) > 1}

    # Print results
    print(f"Total entities: {total_entities}\n")

    print(f"Entities missing textures: {len(missing_textures)}")
    if missing_textures:
        for entity in missing_textures:
            print(f"  - {entity}")
    else:
        print("  None\n")

    print(f"\nShared textures (used by multiple entities): {len(shared_textures)}")
    if shared_textures:
        for texture, entities in sorted(shared_textures.items()):
            print(f"\n  {texture}")
            for entity in sorted(entities):
                print(f"    - {entity}")
    else:
        print("  None")

if __name__ == "__main__":
    entity_regions_dir = Path(__file__).parent.parent / "entity_regions"
    analyze_entity_textures(entity_regions_dir)
