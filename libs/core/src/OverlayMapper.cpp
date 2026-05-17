#include "core/OverlayMapper.h"
#include "core/Hash.h"
#include "core/AnimationUtils.h"
#include "core/FileUtils.h"

#include <algorithm>

namespace Core {

OverlayMapper::OverlayMapper(
    const QStringList &targetPaths,
    const QStringList &overlayPaths,
    qint64             seed,
    bool               perFrame,
    const QString     &targetDir,
    const QString     &overlayDir)
    : m_perFrame(perFrame)
    , m_targetDir(targetDir)
    , m_overlayDir(overlayDir)
    , m_seed(seed)
{
    // Build consistent hashing ring over overlays
    m_ring.reserve(overlayPaths.size());
    for (const QString &p : overlayPaths) {
        uint32_t h = ringHash(QString::number(seed) + QStringLiteral(":overlay:") + relOverlay(p));
        m_ring.append({h, p});
    }
    std::sort(m_ring.begin(), m_ring.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });

    // Build target → overlay mapping
    for (const QString &t : targetPaths) {
        const QString rel = relTarget(t);
        m_mapping.insert(t, ringSelect(m_ring, ringHash(QString::number(seed) + QLatin1Char(':') + rel)));

        if (m_perFrame) {
            const auto anim  = parseAnimationMcmeta(t);
            const int frames = getAnimationFrameCount(t, anim);
            if (frames > 1) {
                m_frameMapping.insert(t + QStringLiteral("\n0"), m_mapping.value(t));
                for (int f = 1; f < frames; ++f) {
                    uint32_t h = ringHash(QString::number(seed) + QLatin1Char(':') + rel + QLatin1Char(':') + QString::number(f));
                    m_frameMapping.insert(t + QLatin1Char('\n') + QString::number(f), ringSelect(m_ring, h));
                }
            }
        }
    }
}

QString OverlayMapper::relOverlay(const QString &path) const
{
    return m_overlayDir.isEmpty() ? path : relPath(m_overlayDir, path);
}

QString OverlayMapper::relTarget(const QString &path) const
{
    return m_targetDir.isEmpty() ? path : relPath(m_targetDir, path);
}

QString OverlayMapper::getOverlay(const QString &targetPath) const
{
    return m_mapping.value(targetPath);
}


QString OverlayMapper::getOverlayForFrame(const QString &targetPath, int frameIdx) const
{
    if (m_perFrame) {
        const QString key = targetPath + QLatin1Char('\n') + QString::number(frameIdx);
        if (m_frameMapping.contains(key))
            return m_frameMapping.value(key);
    }
    return m_mapping.value(targetPath);
}

} // namespace Core
