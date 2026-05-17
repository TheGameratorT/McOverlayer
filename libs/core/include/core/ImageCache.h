#pragma once

#include <QImage>
#include <QString>
#include <QStringList>
#include <QCache>
#include <QHash>
#include <QMutex>
#include <functional>

namespace Core {

// Thread-safe LRU cache for overlay images and their resized variants.
//
// Overlays are loaded on demand (lazy) rather than pre-loaded in bulk, so
// memory usage is bounded by kOverlayCacheMax full-res images +
// kResizedCacheMax resized images regardless of how large the overlay set is.
//
// Usage: workers call getResizedOverlay() concurrently — all cache access is
// mutex-protected; disk I/O and resizing happen outside the lock.
class ImageCache {
public:
    ImageCache();

    // Optional warm-up: loads up to kOverlayCacheMax overlays in parallel.
    // Remaining paths will be loaded on first use by getResizedOverlay().
    // progressCb(done, total) is called from worker threads.
    void preloadOverlays(
        const QStringList              &paths,
        std::function<void(int, int)>   progressCb = {}
    );

    // Pre-load ALL overlay paths resized to size×size for fast entity compositing.
    // Results are stored in an unbounded flat map (images are small at that size).
    // keepAspect: if true, fit within size×size preserving aspect ratio.
    void preloadFixed(
        const QStringList              &paths,
        int                             size,
        bool                            keepAspect,
        std::function<void(int, int)>   progressCb = {}
    );

    // Return the pre-loaded fixed-size overlay for the given size, or a null QImage if not loaded.
    QImage getFixed(const QString &path, int size) const;

    // Return a raw (full-res) overlay image (RGBA8888). Loads from disk on miss.
    QImage getOverlay(const QString &path);

    // Return an overlay resized to (w × h) with the given settings.
    // Both the raw overlay and the resized result are LRU-cached.
    QImage getResizedOverlay(
        const QString &path,
        int            w,
        int            h,
        bool           keepAspect,
        double         overlayScale
    );

    void clearResizedCache();
    void clear();

private:
    struct ResizedKey {
        QString path;
        int     w, h;
        bool    keepAspect;
        double  overlayScale;
        bool operator==(const ResizedKey &o) const noexcept;
    };
    friend size_t qHash(const ImageCache::ResizedKey &key, size_t seed) noexcept;

    struct FixedKey {
        QString path;
        int     size;
        bool operator==(const FixedKey &o) const noexcept { return path == o.path && size == o.size; }
    };
    friend size_t qHash(const ImageCache::FixedKey &key, size_t seed) noexcept;

    // Returns a copy of the cached image (cheap: QImage is ref-counted).
    // Must be called with m_mutex held.
    QImage cachedOverlay(const QString &path) const;
    QImage cachedResized(const ResizedKey &key) const;

    mutable QMutex              m_mutex;
    QCache<QString, QImage>     m_overlayCache;   // LRU, raw full-res overlays
    QCache<ResizedKey, QImage>  m_resizedCache;   // LRU, resized overlays
    QHash<FixedKey, QImage>     m_fixedCache;     // unbounded, fixed-size preloaded overlays

    // Memory bounds in bytes.
    // Raw: 128 MB covers ~512 overlays at 256×256 RGBA or ~32 at 1024×1024.
    // Resized: 64 MB for thumbnail-sized results.
    static constexpr int kOverlayCacheMaxBytes = 128 * 1024 * 1024;
    static constexpr int kResizedCacheMaxBytes =  64 * 1024 * 1024;
};

size_t qHash(const ImageCache::ResizedKey &key, size_t seed = 0) noexcept;
size_t qHash(const ImageCache::FixedKey   &key, size_t seed = 0) noexcept;

} // namespace Core
