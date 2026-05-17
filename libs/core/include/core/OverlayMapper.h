#pragma once

#include <QString>
#include <QStringList>
#include <QHash>
#include <QVector>
#include <QPair>
#include <cstdint>

namespace Core {

// Deterministic overlay mapper for regular (non-entity) target images.
// Uses a consistent hashing ring so adding/removing overlays only reassigns
// the affected fraction of targets.
class OverlayMapper {
public:
    OverlayMapper(
        const QStringList &targetPaths,
        const QStringList &overlayPaths,
        qint64             seed,
        bool               perFrame,
        const QString     &targetDir,
        const QString     &overlayDir
    );

    // Return the overlay path assigned to targetPath.
    QString getOverlay(const QString &targetPath) const;

    // In per-frame mode, return the overlay for a specific animation frame.
    // Falls back to getOverlay() if no per-frame mapping exists.
    QString getOverlayForFrame(const QString &targetPath, int frameIdx) const;


private:
    QString relOverlay(const QString &path) const;
    QString relTarget(const QString &path) const;

    bool    m_perFrame;
    QString m_targetDir;
    QString m_overlayDir;
    qint64  m_seed;

    QVector<QPair<uint32_t, QString>> m_ring;  // sorted (hash, overlay_path)
    QHash<QString, QString>           m_mapping;       // target_path -> overlay_path
    QHash<QString, QString>           m_frameMapping;  // "path\nframe" -> overlay_path
};

} // namespace Core
