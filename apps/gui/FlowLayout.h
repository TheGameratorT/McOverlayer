#pragma once

#include <QLayout>
#include <QRect>
#include <QSize>

// Wrapping flow layout that places children left-to-right and wraps to the
// next row when the viewport width is exceeded. Supports heightForWidth().
class FlowLayout : public QLayout {
    Q_OBJECT
public:
    explicit FlowLayout(QWidget *parent = nullptr, int hGap = 8, int vGap = 8);
    ~FlowLayout() override;

    void addItem(QLayoutItem *item) override;
    int  count() const override;
    QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;

    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int  heightForWidth(int width) const override;
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override;
    QSize minimumSize() const override;

private:
    int doLayout(const QRect &rect, bool dryRun) const;

    QList<QLayoutItem *> m_items;
    int m_hGap;
    int m_vGap;
};
