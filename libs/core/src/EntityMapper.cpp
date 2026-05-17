#include "core/EntityMapper.h"
#include "core/Hash.h"
#include "core/FileUtils.h"

#include <algorithm>
#include <stdexcept>

namespace Core {

EntityMapper::EntityMapper(
    const QHash<QString, EntityData> &entities,
    const QStringList                &overlayPaths,
    const QString                    &faceMode,
    const QString                    &textureMode,
    qint64                            seed,
    const QString                    &overlayDir)
    : m_overlayDir(overlayDir)
    , m_entities(entities)
{
    if (overlayPaths.isEmpty())
        throw std::runtime_error("No overlay images provided");
    if (faceMode != QStringLiteral("same") && faceMode != QStringLiteral("different"))
        throw std::runtime_error("Invalid face_mode: " + faceMode.toStdString());
    if (textureMode != QStringLiteral("shared") && textureMode != QStringLiteral("separate"))
        throw std::runtime_error("Invalid texture_mode: " + textureMode.toStdString());

    // Build consistent hashing ring
    m_ring.reserve(overlayPaths.size());
    for (const QString &p : overlayPaths) {
        uint32_t h = ringHash(QString::number(seed) + QStringLiteral(":overlay:") + relOverlay(p));
        m_ring.append({h, p});
    }
    std::sort(m_ring.begin(), m_ring.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });

    const QString seedStr = QString::number(seed);

    for (auto it = entities.cbegin(); it != entities.cend(); ++it) {
        const QString &entityId = it.key();
        const EntityData &data  = it.value();
        const int numFaces      = data.regions.size();

        if (faceMode == QStringLiteral("same") && textureMode == QStringLiteral("shared")) {
            const QString ov = ringSelect(m_ring, ringHash(seedStr + QLatin1Char(':') + entityId));
            for (const QString &tex : data.textures)
                m_faceOverlays.insert(tex, {entityId, QStringList(numFaces, ov)});

        } else if (faceMode == QStringLiteral("same") && textureMode == QStringLiteral("separate")) {
            for (const QString &tex : data.textures) {
                const QString ov = ringSelect(m_ring, ringHash(seedStr + QLatin1Char(':') + entityId + QLatin1Char(':') + tex));
                m_faceOverlays.insert(tex, {entityId, QStringList(numFaces, ov)});
            }

        } else if (faceMode == QStringLiteral("different") && textureMode == QStringLiteral("shared")) {
            QStringList faceOvs;
            faceOvs.reserve(numFaces);
            for (const EntityRegion &r : data.regions)
                faceOvs.append(ringSelect(m_ring, ringHash(seedStr + QLatin1Char(':') + entityId + QLatin1Char(':') + r.name)));
            for (const QString &tex : data.textures)
                m_faceOverlays.insert(tex, {entityId, faceOvs});

        } else { // different + separate
            for (const QString &tex : data.textures) {
                QStringList faceOvs;
                faceOvs.reserve(numFaces);
                for (const EntityRegion &r : data.regions)
                    faceOvs.append(ringSelect(m_ring, ringHash(seedStr + QLatin1Char(':') + entityId + QLatin1Char(':') + tex + QLatin1Char(':') + r.name)));
                m_faceOverlays.insert(tex, {entityId, faceOvs});
            }
        }
    }
}

QString EntityMapper::relOverlay(const QString &path) const
{
    return m_overlayDir.isEmpty() ? path : relPath(m_overlayDir, path);
}

void EntityMapper::getFaceOverlays(
    const QString        &texturePath,
    QList<EntityRegion>  &regionsOut,
    QStringList          &overlayPathsOut) const
{
    auto it = m_faceOverlays.constFind(texturePath);
    if (it == m_faceOverlays.constEnd())
        throw std::runtime_error("No face overlays for: " + texturePath.toStdString());

    const FaceOverlayEntry &entry = it.value();
    const EntityData &data = m_entities.value(entry.entityId);

    if (data.regions.size() != entry.overlayPaths.size())
        throw std::runtime_error("Region/overlay count mismatch for: " + texturePath.toStdString());

    regionsOut      = data.regions;
    overlayPathsOut = entry.overlayPaths;
}

} // namespace Core
