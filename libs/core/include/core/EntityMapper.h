#pragma once

#include "Types.h"
#include <QHash>
#include <QVector>
#include <QPair>
#include <cstdint>

namespace Core {

// Deterministic overlay mapper for entity textures.
// Assigns overlays to face regions based on face_mode and texture_mode.
class EntityMapper {
public:
    EntityMapper(
        const QHash<QString, EntityData> &entities,
        const QStringList                &overlayPaths,
        const QString                    &faceMode,     // "same" or "different"
        const QString                    &textureMode,  // "shared" or "separate"
        qint64                            seed,
        const QString                    &overlayDir
    );

    // Return (region, overlay_path) pairs for every face of a texture.
    // The lists are parallel and ordered by entity region definition order.
    // Throws std::runtime_error if texturePath is not registered.
    void getFaceOverlays(
        const QString        &texturePath,
        QList<EntityRegion>  &regionsOut,
        QStringList          &overlayPathsOut
    ) const;

private:
    QString relOverlay(const QString &path) const;

    QString m_overlayDir;

    QVector<QPair<uint32_t, QString>>   m_ring;
    QHash<QString, EntityData>          m_entities;
    // texture_path -> {entityId, overlay paths parallel to entity's regions}
    struct FaceOverlayEntry { QString entityId; QStringList overlayPaths; };
    QHash<QString, FaceOverlayEntry>    m_faceOverlays;
};

} // namespace Core
