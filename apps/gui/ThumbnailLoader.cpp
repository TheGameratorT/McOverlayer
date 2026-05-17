#include "ThumbnailLoader.h"
#include "core/ImageProcessor.h"

#include <QImage>
#include <QPainter>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <cmath>

static constexpr int kThumbW = 148;
static constexpr int kThumbH = 148;

ThumbnailLoader::ThumbnailLoader(
    const QList<Core::TextureAssignment> &assignments,
    QObject *parent,
    double  alpha,
    bool    keepAspect,
    double  overlayScale)
    : QThread(parent)
    , m_assignments(assignments)
    , m_alpha(alpha)
    , m_keepAspect(keepAspect)
    , m_overlayScale(overlayScale)
{}

void ThumbnailLoader::setIndices(const QList<int> &indices)
{
    m_indices = indices;
}

void ThumbnailLoader::cancel() { m_cancelled.storeRelaxed(1); }

void ThumbnailLoader::run()
{
    QThreadPool pool;
    pool.setMaxThreadCount(4);

    QList<QFuture<void>> futures;

    if (m_indices.isEmpty()) {
        futures.reserve(m_assignments.size());
        for (int i = 0; i < m_assignments.size(); ++i) {
            if (m_cancelled.loadRelaxed()) break;
            const Core::TextureAssignment &a = m_assignments[i];
            futures.append(QtConcurrent::run(&pool, [this, i, a]() {
                if (!m_cancelled.loadRelaxed()) {
                    QImage img = renderOne(a);
                    if (!img.isNull()) emit loaded(i, img);
                }
            }));
        }
    } else {
        futures.reserve(m_indices.size());
        for (int idx : m_indices) {
            if (m_cancelled.loadRelaxed()) break;
            if (idx < 0 || idx >= m_assignments.size()) continue;
            const Core::TextureAssignment &a = m_assignments[idx];
            futures.append(QtConcurrent::run(&pool, [this, idx, a]() {
                if (!m_cancelled.loadRelaxed()) {
                    QImage img = renderOne(a);
                    if (!img.isNull()) emit loaded(idx, img);
                }
            }));
        }
    }

    for (auto &f : futures)
        f.waitForFinished();
}

// Composite an overlay onto a sub-region of dst at (xs, ys, ws, hs).
static void compositeRegion(QImage &dst, const QImage &ov,
                            int xs, int ys, int ws, int hs,
                            float alpha)
{
    for (int row = 0; row < hs; ++row) {
        uchar       *d = dst.scanLine(ys + row) + xs * 4;
        const uchar *s = ov.constScanLine(row);
        for (int col = 0; col < ws; ++col) {
            const float ovA = s[3] * (alpha / 255.f);
            const float inv = 1.f - ovA;
            d[0] = static_cast<uchar>(d[0] * inv + s[0] * ovA);
            d[1] = static_cast<uchar>(d[1] * inv + s[1] * ovA);
            d[2] = static_cast<uchar>(d[2] * inv + s[2] * ovA);
            d += 4; s += 4;
        }
    }
}

QImage ThumbnailLoader::renderOne(const Core::TextureAssignment &a) const
{
    QImage bg(a.targetPath);
    if (bg.isNull())
        return {};
    bg = bg.convertToFormat(QImage::Format_RGBA8888);

    if (!a.isEntity) {
        // Regular texture: crop animated to first frame, stretch to thumbnail
        if (bg.height() > bg.width())
            bg = bg.copy(0, 0, bg.width(), bg.width());

        bg = bg.scaled(kThumbW, kThumbH, Qt::IgnoreAspectRatio, Qt::FastTransformation)
               .convertToFormat(QImage::Format_RGBA8888);

        if (a.overlayPath.isEmpty())
            return bg;

        QImage ov = QImage(a.overlayPath).convertToFormat(QImage::Format_RGBA8888);
        if (ov.isNull()) return bg;

        ov = Core::resizeOverlay(ov, kThumbW, kThumbH, m_keepAspect, m_overlayScale);
        Core::compositeInPlace(bg, ov, m_alpha);
        return bg;
    }

    // ---- Entity texture ----
    const int imgW = bg.width();
    const int imgH = bg.height();

    // Scale to fit kThumbW×kThumbH preserving aspect ratio
    const double thumbScale = std::min(
        static_cast<double>(kThumbW) / imgW,
        static_cast<double>(kThumbH) / imgH);
    const int tw = qMax(1, static_cast<int>(imgW * thumbScale));
    const int th = qMax(1, static_cast<int>(imgH * thumbScale));

    QImage thumb = bg.scaled(tw, th, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                     .convertToFormat(QImage::Format_RGBA8888);

    // sx/sy: canonical region coords → thumb pixels
    // (accounts for HD texture packs where imgW > canonW)
    const int canonW = qMax(1, a.textureWidth);
    const int canonH = qMax(1, a.textureHeight);
    const double sx = static_cast<double>(imgW) / canonW * thumbScale;
    const double sy = static_cast<double>(imgH) / canonH * thumbScale;

    const float fA = static_cast<float>(m_alpha);

    for (int i = 0; i < a.faceRegions.size() && i < a.faceOverlayPaths.size(); ++i) {
        const Core::EntityRegion &r = a.faceRegions.at(i);
        const QString &ovPath = a.faceOverlayPaths.at(i);
        if (ovPath.isEmpty()) continue;

        int xs = qRound(r.x * sx);
        int ys = qRound(r.y * sy);
        int ws = qMax(1, qRound(r.width  * sx));
        int hs = qMax(1, qRound(r.height * sy));

        // Clamp to thumbnail bounds
        xs = qBound(0, xs, tw);
        ys = qBound(0, ys, th);
        ws = qMin(ws, tw - xs);
        hs = qMin(hs, th - ys);
        if (ws <= 0 || hs <= 0) continue;

        QImage ov = QImage(ovPath).convertToFormat(QImage::Format_RGBA8888);
        if (ov.isNull()) continue;

        ov = Core::resizeOverlay(ov, ws, hs, m_keepAspect, m_overlayScale);
        ov = Core::applyFlip(ov, r.flip);
        ov = Core::applyRotation(ov, r.rotate, ws, hs, m_keepAspect, m_overlayScale);
        if (ov.width() != ws || ov.height() != hs)
            ov = ov.scaled(ws, hs, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        ov = ov.convertToFormat(QImage::Format_RGBA8888);

        compositeRegion(thumb, ov, xs, ys, ws, hs, fA);
    }

    // Center on a transparent kThumbW×kThumbH canvas if the entity texture
    // is not square (e.g. 64×32 player skin becomes 148×74)
    if (tw == kThumbW && th == kThumbH)
        return thumb;

    QImage canvas(kThumbW, kThumbH, QImage::Format_RGBA8888);
    canvas.fill(Qt::transparent);
    QPainter p(&canvas);
    p.drawImage((kThumbW - tw) / 2, (kThumbH - th) / 2, thumb);
    return canvas;
}
