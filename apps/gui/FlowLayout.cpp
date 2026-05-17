#include "FlowLayout.h"

#include <QWidget>

FlowLayout::FlowLayout(QWidget *parent, int hGap, int vGap)
    : QLayout(parent), m_hGap(hGap), m_vGap(vGap)
{}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem *item = takeAt(0))
        delete item;
}

void FlowLayout::addItem(QLayoutItem *item)
{
    m_items.append(item);
}

int FlowLayout::count() const { return m_items.size(); }

QLayoutItem *FlowLayout::itemAt(int index) const
{
    return (index >= 0 && index < m_items.size()) ? m_items[index] : nullptr;
}

QLayoutItem *FlowLayout::takeAt(int index)
{
    return (index >= 0 && index < m_items.size()) ? m_items.takeAt(index) : nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const { return {}; }
bool             FlowLayout::hasHeightForWidth() const   { return true; }

int FlowLayout::heightForWidth(int width) const
{
    return doLayout(QRect(0, 0, width, 0), true);
}

void FlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout::sizeHint() const { return minimumSize(); }

QSize FlowLayout::minimumSize() const
{
    QSize s;
    for (const QLayoutItem *item : m_items)
        s = s.expandedTo(item->minimumSize());
    const QMargins m = contentsMargins();
    return s + QSize(m.left() + m.right(), m.top() + m.bottom());
}

int FlowLayout::doLayout(const QRect &rect, bool dryRun) const
{
    const QMargins m = contentsMargins();
    const QRect r    = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    int x = r.x(), y = r.y(), lineH = 0;

    for (QLayoutItem *item : m_items) {
        const QSize hint = item->sizeHint();
        const int nextX  = x + hint.width() + m_hGap;
        if (nextX - m_hGap > r.right() && lineH > 0) {
            x = r.x();
            y += lineH + m_vGap;
            lineH = 0;
        }
        if (!dryRun)
            item->setGeometry(QRect(QPoint(x, y), hint));
        x = x + hint.width() + m_hGap;
        lineH = qMax(lineH, hint.height());
    }

    return y + lineH - rect.y() + m.bottom();
}
