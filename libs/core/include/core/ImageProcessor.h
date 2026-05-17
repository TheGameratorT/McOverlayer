#pragma once

#include "Types.h"
#include "ImageCache.h"
#include <QImage>
#include <QAtomicInt>
#include <QPair>

namespace Core {

// Forward declaration — avoids circular include with OverlayMapper.h
class OverlayMapper;

// Resize overlay image to (targetW x targetH).
// If keepAspect || overlayScale < 1.0: fit within scaled bounds preserving
// aspect ratio and centre on a transparent canvas of the target size.
// Otherwise: stretch to fill target dimensions.
QImage resizeOverlay(
    const QImage &overlay,
    int            targetW,
    int            targetH,
    bool           keepAspect,
    double         overlayScale
);

// Apply flips in-place on a copy. flip: "v", "h", "hv" / "vh", or empty.
QImage applyFlip(const QImage &img, const QString &flip);

// Apply 90° rotation. rotate: "cw", "ccw", or empty.
// If the rotation changes the image dimensions and targetW/targetH are provided,
// the result is re-scaled to match using resizeOverlay().
QImage applyRotation(
    const QImage &img,
    const QString &rotate,
    int            targetW = 0,
    int            targetH = 0,
    bool           keepAspect = false,
    double         overlayScale = 1.0
);

// Composite overlay onto bg using alpha-weighted blending.
// Background alpha (channel 3) is always preserved from bg.
// Both images must be Format_RGBA8888 and the same size.
void compositeInPlace(QImage &bg, const QImage &overlay, double alpha);

// Convenience: same as compositeInPlace but returns a new image.
QImage compositeOverlay(const QImage &bg, const QImage &overlay, double alpha);

// Atomically save img as PNG to path (writes to path+"_" then renames).
void saveAtomic(const QImage &img, const QString &path);

// Process a regular (non-entity) target image.
// srcPath is read; result is written to outputPath (or srcPath if outputPath is empty).
// Returns (output_path, overlay_path). overlay_path is empty on cancellation.
// Throws std::runtime_error on I/O failure.
// fastOverlaySize > 0: stretch from a preloaded fixed-size source (faster compositing).
QPair<QString, QString> processImage(
    const QString  &srcPath,
    const QString  &outputPath,    // empty = overwrite srcPath in-place
    OverlayMapper  &mapper,
    ImageCache     &cache,
    int             scale,
    double          alpha,
    QAtomicInt     *shutdownFlag,  // nullptr = no cancellation
    bool            keepAspect,
    double          overlayScale,
    int             fastOverlaySize = 0
);

// Process an entity target image with per-face overlay regions.
// srcPath is read; result is written to outputPath (or srcPath if outputPath is empty).
// Returns (output_path, representative_overlay_path).
// Throws std::runtime_error on I/O failure.
// fastOverlaySize > 0: stretch from a preloaded fixed-size source (faster compositing).
QPair<QString, QString> processEntityImage(
    const QString            &srcPath,
    const QString            &outputPath,  // empty = overwrite srcPath in-place
    const QList<EntityRegion> &regions,
    const QStringList         &faceOverlayPaths,
    int                       textureW,
    int                       textureH,
    ImageCache               &cache,
    int                       scale,
    double                    alpha,
    QAtomicInt               *shutdownFlag,
    bool                      keepAspect,
    double                    overlayScale,
    int                       fastOverlaySize = 0
);

} // namespace Core
