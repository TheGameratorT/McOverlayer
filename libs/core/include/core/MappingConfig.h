#pragma once

#include "Types.h"
#include "EntityLoader.h"
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <optional>

namespace Core {

struct MappingConfig {
    QString overlayDir;
    QString textureDir;
    qint64  seed           = 0;
    bool    perFrame       = false;
    QString entityRegionsDir;
    QString entityFaceMode    = QStringLiteral("same");
    QString entityTextureMode = QStringLiteral("shared");
    double  alpha          = 0.75;
    int     scale          = 4;
    bool    keepAspect     = false;
    double  overlayScale   = 1.0;
    QJsonObject pathConfig;
    QString outputDir;
    int     fastOverlaySize    = 512;

    static MappingConfig fromLastRun(const QString &path);
    static MappingConfig fromJson(const QJsonObject &obj);
    void saveLastRun(const QString &path) const;
    QJsonObject toJson() const;

    // Returns the writable path for last_run.json in the OS app-data directory.
    static QString lastRunPath();

    // Returns the entity_regions/ directory to use when none is configured:
    // checks <app-dir>/entity_regions first; on Linux also searches XDG data dirs.
    // Returns an empty string if nothing is found.
    static QString defaultEntityRegionsDir();

    MappingConfig withSeed(const QString &seedStr) const;
    MappingConfig withRandomSeed() const;
};

struct BuildResult {
    QList<TextureAssignment> assignments;
    QStringList              overlayPaths;
    QStringList              targetPaths;
};

// Build overlay assignments for all targets in config.
// Throws std::runtime_error if directories are empty or inaccessible.
BuildResult buildAssignments(const MappingConfig &config);

} // namespace Core
