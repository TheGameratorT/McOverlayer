#pragma once

#include <QString>
#include <QStringList>
#include <QList>

namespace Core {

struct EntityRegion {
    QString name;
    int x = 0, y = 0, width = 0, height = 0;
    QString flip;    // "v", "h", "hv", or empty
    QString rotate;  // "cw", "ccw", or empty
};

struct EntityData {
    QString entityId;
    int textureWidth = 64;
    int textureHeight = 64;
    QList<EntityRegion> regions;
    QStringList textures;
};

struct TextureAssignment {
    QString targetPath;
    QString overlayPath;
    bool isEntity = false;
    QString entityId;
    // Canonical texture dimensions from EntityData — needed to scale region
    // coordinates to actual image pixels (image may be 2× or 4× the canonical size).
    int textureWidth  = 64;
    int textureHeight = 64;
    // Parallel lists: one entry per face region
    QList<EntityRegion> faceRegions;
    QStringList faceOverlayPaths;
};

} // namespace Core
