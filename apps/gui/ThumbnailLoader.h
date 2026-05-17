#pragma once

#include "core/Types.h"
#include <QThread>
#include <QImage>
#include <QList>

// Renders composite (target+overlay blended) thumbnails in a background thread pool.
// Emits loaded(assignmentIndex, QImage) with Qt::QueuedConnection semantics.
class ThumbnailLoader : public QThread {
    Q_OBJECT
public:
    explicit ThumbnailLoader(
        const QList<Core::TextureAssignment> &assignments,
        QObject *parent = nullptr,
        double  alpha        = 0.75,
        bool    keepAspect   = false,
        double  overlayScale = 1.0);

    // Restrict rendering to a subset of assignment indices (in order).
    // Must be called before start().
    void setIndices(const QList<int> &indices);

    void cancel();

signals:
    void loaded(int index, QImage image);

protected:
    void run() override;

private:
    QImage renderOne(const Core::TextureAssignment &a) const;

    QList<Core::TextureAssignment> m_assignments;
    QList<int>  m_indices; // empty = render all
    double      m_alpha;
    bool        m_keepAspect;
    double      m_overlayScale;
    QAtomicInt  m_cancelled{0};
};
