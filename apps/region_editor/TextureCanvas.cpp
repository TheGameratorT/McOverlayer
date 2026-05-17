#include "TextureCanvas.h"

#include <QWheelEvent>
#include <QMouseEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QScrollBar>
#include <QPen>
#include <QBrush>
#include <QPixmap>
#include <QFont>

const QColor TextureCanvas::kRegionColor   = QColor(0,   200, 255, 100);
const QColor TextureCanvas::kSelectedColor = QColor(255, 160,   0, 140);
const QColor TextureCanvas::kDragColor     = QColor(100, 255, 100,  80);

TextureCanvas::TextureCanvas(QWidget *parent)
    : QGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing, false);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    setBackgroundBrush(QBrush(QColor(40, 40, 40)));
}

void TextureCanvas::loadTexture(const QString &path)
{
    m_scene->clear();
    m_pixmapItem  = nullptr;
    m_dragItem    = nullptr;
    m_regionItems.clear();
    m_regions.clear();
    m_selected = -1;

    QPixmap px(path);
    if (px.isNull()) return;

    m_imgW = px.width();
    m_imgH = px.height();

    m_pixmapItem = m_scene->addPixmap(px);
    m_pixmapItem->setZValue(0);
    m_scene->setSceneRect(px.rect());

    fitInView(m_pixmapItem, Qt::KeepAspectRatio);
}

void TextureCanvas::setCanonicalSize(int w, int h)
{
    m_canonW = w;
    m_canonH = h;
}

void TextureCanvas::setRegions(const QList<Core::EntityRegion> &regions)
{
    m_regions = regions;
    m_selected = -1;
    rebuildRegionItems();
}

QList<Core::EntityRegion> TextureCanvas::regions() const
{
    return m_regions;
}

void TextureCanvas::setSelectedRegion(int index)
{
    m_selected = index;
    rebuildRegionItems();
}

void TextureCanvas::removeRegion(int index)
{
    if (index < 0 || index >= m_regions.size()) return;
    m_regions.removeAt(index);
    if (m_selected >= m_regions.size())
        m_selected = m_regions.isEmpty() ? -1 : m_regions.size() - 1;
    rebuildRegionItems();
}

void TextureCanvas::updateRegionTransform(int index, const QString &flip, const QString &rotate)
{
    if (index < 0 || index >= m_regions.size()) return;
    m_regions[index].flip   = (flip   == QStringLiteral("none")) ? QString{} : flip;
    m_regions[index].rotate = (rotate == QStringLiteral("none")) ? QString{} : rotate;
}

void TextureCanvas::zoomIn()  { scale(1.25, 1.25); }
void TextureCanvas::zoomOut() { scale(1.0 / 1.25, 1.0 / 1.25); }
void TextureCanvas::fitView() { if (m_pixmapItem) fitInView(m_pixmapItem, Qt::KeepAspectRatio); }

double TextureCanvas::scaleX() const
{
    return (m_canonW > 0 && m_imgW > 0) ? static_cast<double>(m_imgW) / m_canonW : 1.0;
}

double TextureCanvas::scaleY() const
{
    return (m_canonH > 0 && m_imgH > 0) ? static_cast<double>(m_imgH) / m_canonH : 1.0;
}

void TextureCanvas::rebuildRegionItems()
{
    for (auto *item : m_regionItems)
        m_scene->removeItem(item);
    qDeleteAll(m_regionItems);
    m_regionItems.clear();

    if (!m_pixmapItem) return;

    const double sx = scaleX();
    const double sy = scaleY();

    QPen borderPen(Qt::white, 0);
    borderPen.setCosmetic(true);

    QFont labelFont;
    labelFont.setPointSize(8);

    for (int i = 0; i < m_regions.size(); ++i) {
        const auto &r = m_regions[i];
        const bool sel = (i == m_selected);
        const QRectF rect(r.x * sx, r.y * sy, r.width * sx, r.height * sy);

        auto *fill = m_scene->addRect(rect, QPen(Qt::NoPen),
                                      QBrush(sel ? kSelectedColor : kRegionColor));
        fill->setZValue(1);

        auto *border = m_scene->addRect(rect, borderPen, QBrush(Qt::NoBrush));
        border->setZValue(1.5);

        const QString name = r.name.isEmpty() ? QStringLiteral("?") : r.name;
        auto *label = m_scene->addText(name);
        label->setFont(labelFont);
        label->setDefaultTextColor(sel ? QColor(255, 220, 80) : QColor(220, 220, 220));
        label->setPos(rect.x() + 1, rect.y() + 1);
        label->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        label->setZValue(2);

        m_regionItems.append(fill);
        m_regionItems.append(border);
        m_regionItems.append(label);
    }
}

QRectF TextureCanvas::normalizeRect(const QPointF &a, const QPointF &b) const
{
    return QRectF(QPointF(qMin(a.x(), b.x()), qMin(a.y(), b.y())),
                  QPointF(qMax(a.x(), b.x()), qMax(a.y(), b.y())));
}

// ---- Input events ----

void TextureCanvas::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const double factor = event->angleDelta().y() > 0 ? 1.15 : (1.0 / 1.15);
        scale(factor, factor);
        event->accept();
    } else {
        QGraphicsView::wheelEvent(event);
    }
}

void TextureCanvas::mousePressEvent(QMouseEvent *event)
{
    if (!m_pixmapItem) { QGraphicsView::mousePressEvent(event); return; }

    if (event->button() == Qt::MiddleButton) {
        setDragMode(QGraphicsView::ScrollHandDrag);
        QMouseEvent fake(event->type(), event->pos(), event->globalPosition().toPoint(),
                         Qt::LeftButton, Qt::LeftButton, event->modifiers());
        QGraphicsView::mousePressEvent(&fake);
        return;
    }

    if (event->button() == Qt::LeftButton) {
        const QPointF scenePos = mapToScene(event->pos());
        const double sx = scaleX(), sy = scaleY();

        for (int i = m_regions.size() - 1; i >= 0; --i) {
            const auto &r = m_regions[i];
            if (QRectF(r.x * sx, r.y * sy, r.width * sx, r.height * sy).contains(scenePos)) {
                m_selected = i;
                rebuildRegionItems();
                emit regionSelected(i);
                return;
            }
        }

        m_dragging  = true;
        m_dragStart = scenePos;

        QPen dragPen(Qt::green, 0);
        dragPen.setCosmetic(true);
        m_dragItem = m_scene->addRect(QRectF(scenePos, scenePos), dragPen, QBrush(kDragColor));
        m_dragItem->setZValue(100);
    }

    QGraphicsView::mousePressEvent(event);
}

void TextureCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && m_dragItem)
        m_dragItem->setRect(normalizeRect(m_dragStart, mapToScene(event->pos())));
    QGraphicsView::mouseMoveEvent(event);
}

void TextureCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        setDragMode(QGraphicsView::NoDrag);
        return;
    }

    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        if (m_dragItem) {
            const QRectF rect = m_dragItem->rect();
            m_scene->removeItem(m_dragItem);
            delete m_dragItem;
            m_dragItem = nullptr;

            const QRectF imgRect(0, 0, m_imgW, m_imgH);
            const QRectF clamped = rect.intersected(imgRect);
            if (clamped.width() >= 2 && clamped.height() >= 2) {
                const double sx = scaleX(), sy = scaleY();
                Core::EntityRegion r;
                r.name   = QStringLiteral("region_%1").arg(m_regions.size());
                r.x      = static_cast<int>(clamped.x()      / sx);
                r.y      = static_cast<int>(clamped.y()      / sy);
                r.width  = static_cast<int>(clamped.width()  / sx);
                r.height = static_cast<int>(clamped.height() / sy);
                m_regions.append(r);
                m_selected = m_regions.size() - 1;
                rebuildRegionItems();
                emit regionAdded(r);
                emit regionSelected(m_selected);
            }
        }
    }

    QGraphicsView::mouseReleaseEvent(event);
}
