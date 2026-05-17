#include "core/ImageCache.h"
#include "core/ImageProcessor.h"

#include <QMutexLocker>
#include <QPainter>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>

namespace Core {

// ---- ResizedKey helpers ----

bool ImageCache::ResizedKey::operator==(const ResizedKey &o) const noexcept
{
    return path == o.path && w == o.w && h == o.h
        && keepAspect == o.keepAspect
        && qFuzzyCompare(overlayScale, o.overlayScale);
}

size_t qHash(const ImageCache::ResizedKey &key, size_t seed) noexcept
{
    return qHashMulti(seed, key.path, key.w, key.h, key.keepAspect,
                      static_cast<int>(key.overlayScale * 10000));
}

size_t qHash(const ImageCache::FixedKey &key, size_t seed) noexcept
{
    return qHashMulti(seed, key.path, key.size);
}

// ---- ImageCache ----

ImageCache::ImageCache()
    : m_overlayCache(kOverlayCacheMaxBytes)
    , m_resizedCache(kResizedCacheMaxBytes)
{}

void ImageCache::preloadOverlays(
    const QStringList              &paths,
    std::function<void(int, int)>   progressCb)
{
    if (paths.isEmpty())
        return;

    // Warm up only up to kOverlayCacheMax entries — the rest load on demand.
    // Warm up a few entries; cap at 32 so startup preload doesn't consume too much.
    const int warmCount = qMin(static_cast<int>(paths.size()), 32);
    const int total     = paths.size();

    QAtomicInt done{0};

    QThreadPool pool;
    pool.setMaxThreadCount(qMax(1, qMin(8, QThread::idealThreadCount())));

    QList<QFuture<void>> futures;
    for (int i = 0; i < warmCount; ++i) {
        const QString &p = paths[i];
        futures.append(QtConcurrent::run(&pool, [this, p, total, &done, &progressCb]() {
            QImage img(p);
            if (!img.isNull()) {
                img = img.convertToFormat(QImage::Format_RGBA8888);
                QMutexLocker lk(&m_mutex);
                if (!m_overlayCache.contains(p))
                    m_overlayCache.insert(p, new QImage(img), img.sizeInBytes());
            }
            int n = done.fetchAndAddRelaxed(1) + 1;
            if (progressCb)
                progressCb(n, total);
        }));
    }

    for (auto &f : futures)
        f.waitForFinished();

    // Emit remaining progress ticks for paths we skipped (load-on-demand)
    if (progressCb) {
        for (int i = warmCount; i < total; ++i)
            progressCb(i + 1, total);
    }
}

// Must be called with m_mutex held.
QImage ImageCache::cachedOverlay(const QString &path) const
{
    QImage *p = m_overlayCache.object(path);
    return p ? *p : QImage{};
}

// Must be called with m_mutex held.
QImage ImageCache::cachedResized(const ResizedKey &key) const
{
    QImage *p = m_resizedCache.object(key);
    return p ? *p : QImage{};
}

QImage ImageCache::getOverlay(const QString &path)
{
    {
        QMutexLocker lk(&m_mutex);
        QImage *p = m_overlayCache.object(path);
        if (p) return *p;
    }

    // Load from disk outside the lock
    QImage img(path);
    if (img.isNull())
        return {};
    img = img.convertToFormat(QImage::Format_RGBA8888);

    {
        QMutexLocker lk(&m_mutex);
        // Another thread may have inserted while we loaded — let the cache handle it
        QImage *existing = m_overlayCache.object(path);
        if (existing)
            return *existing;
        m_overlayCache.insert(path, new QImage(img), img.sizeInBytes());
    }
    return img;
}

QImage ImageCache::getResizedOverlay(
    const QString &path,
    int            w,
    int            h,
    bool           keepAspect,
    double         overlayScale)
{
    const ResizedKey key{path, w, h, keepAspect, overlayScale};

    {
        QMutexLocker lk(&m_mutex);
        QImage *p = m_resizedCache.object(key);
        if (p) return *p;
    }

    // Load raw overlay (may hit disk) and resize — both outside the lock
    QImage raw = getOverlay(path);
    if (raw.isNull())
        return {};

    QImage resized = resizeOverlay(raw, w, h, keepAspect, overlayScale);

    {
        QMutexLocker lk(&m_mutex);
        QImage *existing = m_resizedCache.object(key);
        if (existing)
            return *existing;
        m_resizedCache.insert(key, new QImage(resized), resized.sizeInBytes());
    }
    return resized;
}

void ImageCache::clearResizedCache()
{
    QMutexLocker lk(&m_mutex);
    m_resizedCache.clear();
}

void ImageCache::clear()
{
    QMutexLocker lk(&m_mutex);
    m_overlayCache.clear();
    m_resizedCache.clear();
    m_fixedCache.clear();
}

void ImageCache::preloadFixed(
    const QStringList              &paths,
    int                             size,
    bool                            keepAspect,
    std::function<void(int, int)>   progressCb)
{
    if (paths.isEmpty() || size <= 0)
        return;

    const int total = paths.size();
    QAtomicInt done{0};

    QThreadPool pool;
    pool.setMaxThreadCount(qMax(1, qMin(8, QThread::idealThreadCount())));

    QList<QFuture<void>> futures;
    futures.reserve(total);
    for (const QString &p : paths) {
        futures.append(QtConcurrent::run(&pool, [this, p, size, keepAspect, total, &done, &progressCb]() {
            QImage img(p);
            if (!img.isNull()) {
                img = img.convertToFormat(QImage::Format_RGBA8888);
                // Quality downscale once; compositing will do a fast stretch from this small source.
                QImage fixed = img.scaled(size, size,
                    keepAspect ? Qt::KeepAspectRatio : Qt::IgnoreAspectRatio,
                    Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA8888);
                if (keepAspect && (fixed.width() != size || fixed.height() != size)) {
                    // Centre on transparent canvas so geometry is always size×size.
                    QImage canvas(size, size, QImage::Format_RGBA8888);
                    canvas.fill(Qt::transparent);
                    QPainter painter(&canvas);
                    painter.drawImage((size - fixed.width()) / 2, (size - fixed.height()) / 2, fixed);
                    fixed = canvas;
                }
                QMutexLocker lk(&m_mutex);
                m_fixedCache.insert({p, size}, fixed);
            }
            int n = done.fetchAndAddRelaxed(1) + 1;
            if (progressCb)
                progressCb(n, total);
        }));
    }
    for (auto &f : futures)
        f.waitForFinished();
}

QImage ImageCache::getFixed(const QString &path, int size) const
{
    QMutexLocker lk(&m_mutex);
    return m_fixedCache.value({path, size});
}

} // namespace Core
