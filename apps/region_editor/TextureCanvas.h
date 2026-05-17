#pragma once

#include "core/Types.h"
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QColor>

// Interactive canvas for viewing a texture and drawing/editing entity face regions.
// Coordinate system: scene pixels map 1-to-1 to image pixels.
class TextureCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit TextureCanvas(QWidget *parent = nullptr);

    // Load a texture image (absolute path). Clears all region overlays.
    void loadTexture(const QString &path);

    // Canonical UV dimensions of the entity texture (e.g. 32×32 for allay).
    // Must be set before setRegions so regions scale correctly when the image
    // is at a higher resolution than the canonical size.
    void setCanonicalSize(int w, int h);

    // Replace all drawn regions (does not emit regionAdded).
    void setRegions(const QList<Core::EntityRegion> &regions);

    QList<Core::EntityRegion> regions() const;

    // Highlight a single region (index into current region list, -1 = none).
    void setSelectedRegion(int index);

    // Remove a region by index.
    void removeRegion(int index);

    // Update flip/rotate label of a region (does not affect geometry).
    void updateRegionTransform(int index, const QString &flip, const QString &rotate);

    void zoomIn();
    void zoomOut();
    void fitView();

signals:
    void regionAdded(const Core::EntityRegion &region);
    void regionSelected(int index);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void rebuildRegionItems();
    QRectF normalizeRect(const QPointF &a, const QPointF &b) const;

    QGraphicsScene      *m_scene      = nullptr;
    QGraphicsPixmapItem *m_pixmapItem = nullptr;
    QGraphicsRectItem   *m_dragItem   = nullptr;

    QList<Core::EntityRegion> m_regions;
    QList<QGraphicsItem *>    m_regionItems; // rects + labels interleaved

    QPointF m_dragStart;
    bool    m_dragging = false;
    int     m_selected = -1;

    double scaleX() const;
    double scaleY() const;

    int m_imgW   = 0;
    int m_imgH   = 0;
    int m_canonW = 0;
    int m_canonH = 0;

    static const QColor kRegionColor;
    static const QColor kSelectedColor;
    static const QColor kDragColor;
};
