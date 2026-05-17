#pragma once

#include "Types.h"
#include <QHash>
#include <QString>

namespace Core {

struct EntityRegions {
    QHash<QString, EntityData> entities;          // entity_id -> EntityData
    QHash<QString, QString>    textureToEntity;   // canonical_tex_path -> entity_id
};

// Load all *.json files from dirPath and build entity region metadata.
// Merges bed_head and bed_foot into a single "bed" entity.
// Throws std::runtime_error on malformed JSON.
EntityRegions loadEntityRegions(const QString &dirPath);

} // namespace Core
