#include "core/ImageProcessor.h"
#include "core/OverlayMapper.h"
#include "core/AnimationUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QTransform>
#include <stdexcept>
#include <algorithm>

namespace Core {

QImage resizeOverlay(const QImage &overlay, int targetW, int targetH, bool keepAspect, double overlayScale)
{
    const int scaledW = static_cast<int>(targetW * overlayScale);
    const int scaledH = static_cast<int>(targetH * overlayScale);

    if (keepAspect || overlayScale < 1.0) {
        const int ow = overlay.width();
        const int oh = overlay.height();
        if (ow == 0 || oh == 0)
            return QImage(targetW, targetH, QImage::Format_RGBA8888);

        const double scale = std::min(
            static_cast<double>(scaledW) / ow,
            static_cast<double>(scaledH) / oh);
        const int fitW = std::max(1, static_cast<int>(ow * scale));
        const int fitH = std::max(1, static_cast<int>(oh * scale));

        QImage resized = overlay.scaled(fitW, fitH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                .convertToFormat(QImage::Format_RGBA8888);

        QImage canvas(targetW, targetH, QImage::Format_RGBA8888);
        canvas.fill(Qt::transparent);
        QPainter p(&canvas);
        p.drawImage((targetW - fitW) / 2, (targetH - fitH) / 2, resized);
        return canvas;
    }

    return overlay.scaled(targetW, targetH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                  .convertToFormat(QImage::Format_RGBA8888);
}

QImage applyFlip(const QImage &img, const QString &flip)
{
    if (flip.isEmpty())
        return img;
    const bool h = flip.contains(QLatin1Char('h'));
    const bool v = flip.contains(QLatin1Char('v'));
    Qt::Orientations orient;
    if (h) orient |= Qt::Horizontal;
    if (v) orient |= Qt::Vertical;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    return img.flipped(orient);
#else
    return img.mirrored(orient & Qt::Horizontal, orient & Qt::Vertical);
#endif
}

QImage applyRotation(
    const QImage &img,
    const QString &rotate,
    int targetW, int targetH,
    bool keepAspect, double overlayScale)
{
    if (rotate.isEmpty())
        return img;

    QTransform t;
    if (rotate == QStringLiteral("cw"))
        t.rotate(90);     // 90° CW on screen
    else if (rotate == QStringLiteral("ccw"))
        t.rotate(-90);    // 90° CCW on screen
    else
        return img;

    QImage rotated = img.transformed(t, Qt::SmoothTransformation)
                        .convertToFormat(QImage::Format_RGBA8888);

    if (targetW > 0 && targetH > 0 &&
        (rotated.width() != targetW || rotated.height() != targetH))
    {
        rotated = resizeOverlay(rotated, targetW, targetH, keepAspect, overlayScale);
    }

    return rotated;
}

void compositeInPlace(QImage &bg, const QImage &overlay, double alpha)
{
    Q_ASSERT(bg.format() == QImage::Format_RGBA8888);
    Q_ASSERT(overlay.format() == QImage::Format_RGBA8888);
    Q_ASSERT(bg.size() == overlay.size());

    const int w = bg.width();
    const int h = bg.height();
    const float fAlpha = static_cast<float>(alpha);

    for (int y = 0; y < h; ++y) {
        uchar       *dst = bg.scanLine(y);
        const uchar *src = overlay.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const float ovA  = src[3] * (fAlpha / 255.f);
            const float inv  = 1.f - ovA;
            dst[0] = static_cast<uchar>(dst[0] * inv + src[0] * ovA);
            dst[1] = static_cast<uchar>(dst[1] * inv + src[1] * ovA);
            dst[2] = static_cast<uchar>(dst[2] * inv + src[2] * ovA);
            // dst[3] preserved (background alpha)
            dst += 4;
            src += 4;
        }
    }
}

QImage compositeOverlay(const QImage &bg, const QImage &overlay, double alpha)
{
    QImage result = bg.convertToFormat(QImage::Format_RGBA8888);
    QImage ov     = overlay.convertToFormat(QImage::Format_RGBA8888);
    if (ov.size() != result.size())
        ov = ov.scaled(result.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
               .convertToFormat(QImage::Format_RGBA8888);
    compositeInPlace(result, ov, alpha);
    return result;
}

void saveAtomic(const QImage &img, const QString &path)
{
    const QString tmp = path + QStringLiteral("_");
    img.save(tmp, "PNG");
    QFile::remove(path);
    QFile::rename(tmp, path);
}

static QImage getOrFastResize(
    ImageCache    &cache,
    const QString &ovPath,
    int            w,
    int            h,
    bool           keepAspect,
    double         overlayScale,
    int            fastOverlaySize)
{
    if (fastOverlaySize > 0 && fastOverlaySize >= w && fastOverlaySize >= h) {
        QImage fixed = cache.getFixed(ovPath, fastOverlaySize);
        if (!fixed.isNull())
            return resizeOverlay(fixed, w, h, keepAspect, overlayScale);
    }
    return cache.getResizedOverlay(ovPath, w, h, keepAspect, overlayScale);
}

QPair<QString, QString> processImage(
    const QString  &srcPath,
    const QString  &outputPath,
    OverlayMapper  &mapper,
    ImageCache     &cache,
    int             scale,
    double          alpha,
    QAtomicInt     *shutdownFlag,
    bool            keepAspect,
    double          overlayScale,
    int             fastOverlaySize)
{
    const QString dstPath = outputPath.isEmpty() ? srcPath : outputPath;

    if (shutdownFlag && shutdownFlag->loadRelaxed())
        return {dstPath, {}};

    QImage bg(srcPath);
    if (bg.isNull())
        throw std::runtime_error("Failed to open image: " + srcPath.toStdString());
    bg = bg.convertToFormat(QImage::Format_RGBA8888);

    const auto anim      = parseAnimationMcmeta(srcPath);
    const int frameCount = getAnimationFrameCount(srcPath, anim);
    const int newW          = bg.width() * scale;
    const int newH          = bg.height() * scale;

    QImage scaled = bg.scaled(newW, newH, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                      .convertToFormat(QImage::Format_RGBA8888);

    if (!outputPath.isEmpty())
        QDir().mkpath(QFileInfo(dstPath).absolutePath());

    if (frameCount == 1) {
        const QString ovPath = mapper.getOverlay(srcPath);
        QImage ov = getOrFastResize(cache, ovPath, newW, newH, keepAspect, overlayScale, fastOverlaySize);
        if (shutdownFlag && shutdownFlag->loadRelaxed())
            return {dstPath, ovPath};
        compositeInPlace(scaled, ov, alpha);
        saveAtomic(scaled, dstPath);
        return {dstPath, ovPath};
    }

    // Animated: process frame by frame
    const int frameH    = bg.height() / frameCount;
    const int scaledFH  = frameH * scale;
    QImage result(newW, newH, QImage::Format_RGBA8888);

    // CompositionMode_Source: write frameBg pixels directly without blending against
    // the uninitialized result buffer. Matches Python's direct array-slice assignment:
    //   out_arr[y0:y1, ...] = composite_result
    // SourceOver (the QPainter default) would blend against garbage alpha in result,
    // turning transparent pixels opaque.
    QPainter p(&result);
    p.setCompositionMode(QPainter::CompositionMode_Source);

    for (int f = 0; f < frameCount; ++f) {
        if (shutdownFlag && shutdownFlag->loadRelaxed())
            return {dstPath, {}};

        const QString ovPath = mapper.getOverlayForFrame(srcPath, f);
        QImage ov = getOrFastResize(cache, ovPath, newW, scaledFH, keepAspect, overlayScale, fastOverlaySize);
        QImage frameBg = scaled.copy(0, f * scaledFH, newW, scaledFH)
                               .convertToFormat(QImage::Format_RGBA8888);
        compositeInPlace(frameBg, ov, alpha);
        p.drawImage(0, f * scaledFH, frameBg);
    }
    p.end();

    if (shutdownFlag && shutdownFlag->loadRelaxed())
        return {dstPath, {}};

    saveAtomic(result, dstPath);
    return {dstPath, {}};
}

QPair<QString, QString> processEntityImage(
    const QString            &srcPath,
    const QString            &outputPath,
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
    int                       fastOverlaySize)
{
    const QString dstPath = outputPath.isEmpty() ? srcPath : outputPath;

    if (shutdownFlag && shutdownFlag->loadRelaxed())
        return {dstPath, {}};

    QImage bg(srcPath);
    if (bg.isNull())
        throw std::runtime_error("Failed to open image: " + srcPath.toStdString());
    bg = bg.convertToFormat(QImage::Format_RGBA8888);

    const int imgW = bg.width();
    const int imgH = bg.height();

    // Determine animation layout
    const bool hasAnim  = parseAnimationMcmeta(srcPath).has_value();
    const int frameHPx  = hasAnim ? imgW : imgH;  // each MC frame is width×width
    const int frameCount = std::max(1, imgH / frameHPx);

    const int newW = imgW * scale;
    const int newH = imgH * scale;

    QImage out = bg.scaled(newW, newH, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                   .convertToFormat(QImage::Format_RGBA8888);

    const double sx = static_cast<double>(imgW) / textureW;
    const double sy = static_cast<double>(frameHPx) / textureH;
    const int scaledFrameH = frameHPx * scale;

    // Collect unique overlays for display name
    QSet<QString> uniqueOvs;
    for (const QString &ov : faceOverlayPaths)
        uniqueOvs.insert(ov);
    const QString displayOv = uniqueOvs.size() == 1 ? *uniqueOvs.cbegin() : QStringLiteral("[multiple]");

    for (int f = 0; f < frameCount; ++f) {
        const int yFrameOffset = f * scaledFrameH;

        const int faceCount = qMin(regions.size(), faceOverlayPaths.size());
        for (int i = 0; i < faceCount; ++i) {
            if (shutdownFlag && shutdownFlag->loadRelaxed())
                return {dstPath, displayOv};

            const EntityRegion &region = regions[i];
            const QString &ovPath = faceOverlayPaths[i];

            int xs = static_cast<int>(std::round(region.x * sx * scale));
            int ys = static_cast<int>(std::round(region.y * sy * scale)) + yFrameOffset;
            int ws = static_cast<int>(std::round(region.width  * sx * scale));
            int hs = static_cast<int>(std::round(region.height * sy * scale));

            if (ws == 0 || hs == 0)
                continue;

            // Clamp to image bounds
            xs = qBound(0, xs, newW);
            ys = qBound(0, ys, newH);
            const int xe = qBound(0, xs + ws, newW);
            const int ye = qBound(0, ys + hs, newH);
            if (xe <= xs || ye <= ys)
                continue;
            ws = xe - xs;
            hs = ye - ys;

            QImage ov = getOrFastResize(cache, ovPath, ws, hs, keepAspect, overlayScale, fastOverlaySize);
            ov = applyFlip(ov, region.flip);
            ov = applyRotation(ov, region.rotate, ws, hs, keepAspect, overlayScale);
            ov = ov.convertToFormat(QImage::Format_RGBA8888);
            if (ov.size() != QSize(ws, hs))
                ov = ov.scaled(ws, hs, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                       .convertToFormat(QImage::Format_RGBA8888);

            // Composite overlay into the region of out
            for (int row = 0; row < hs; ++row) {
                uchar       *dst = out.scanLine(ys + row) + xs * 4;
                const uchar *src = ov.constScanLine(row);
                const float fA = static_cast<float>(alpha);
                for (int col = 0; col < ws; ++col) {
                    const float ovA = src[3] * (fA / 255.f);
                    const float inv = 1.f - ovA;
                    dst[0] = static_cast<uchar>(dst[0] * inv + src[0] * ovA);
                    dst[1] = static_cast<uchar>(dst[1] * inv + src[1] * ovA);
                    dst[2] = static_cast<uchar>(dst[2] * inv + src[2] * ovA);
                    dst += 4;
                    src += 4;
                }
            }
        }
    }

    if (shutdownFlag && shutdownFlag->loadRelaxed())
        return {dstPath, displayOv};

    if (!outputPath.isEmpty())
        QDir().mkpath(QFileInfo(dstPath).absolutePath());

    saveAtomic(out, dstPath);
    return {dstPath, displayOv};
}

} // namespace Core
