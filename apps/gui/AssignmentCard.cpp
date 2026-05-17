#include "AssignmentCard.h"

#include <QVBoxLayout>
#include <QPixmap>
#include <QFileInfo>

static constexpr int kCardWidth    = 160;
static constexpr int kCardMargin   = 6;
static constexpr int kContentW     = kCardWidth - 2 * kCardMargin;
static constexpr int kThumbW       = kContentW;
static constexpr int kThumbH       = kContentW;

static QPixmap placeholderPixmap(int w, int h)
{
    QPixmap pm(w, h);
    pm.fill(QColor(QStringLiteral("#1c1c1c")));
    return pm;
}

AssignmentCard::AssignmentCard(const Core::TextureAssignment &assignment, QWidget *parent)
    : QFrame(parent), m_assignment(assignment)
{
    setFixedWidth(kCardWidth);
    setFrameShape(QFrame::StyledPanel);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(kCardMargin, kCardMargin, kCardMargin, kCardMargin);
    layout->setSpacing(3);

    if (assignment.isEntity && !assignment.entityId.isEmpty()) {
        auto *badge = new QLabel(QStringLiteral("● ") + assignment.entityId, this);
        badge->setAlignment(Qt::AlignCenter);
        badge->setStyleSheet(QStringLiteral("color: #88c0d0; font-size: 9px; font-weight: bold;"));
        layout->addWidget(badge);
    }

    m_thumbLabel = new QLabel(this);
    m_thumbLabel->setFixedSize(kThumbW, kThumbH);
    m_thumbLabel->setAlignment(Qt::AlignCenter);
    m_thumbLabel->setPixmap(placeholderPixmap(kThumbW, kThumbH));
    m_thumbLabel->setToolTip(assignment.targetPath);
    layout->addWidget(m_thumbLabel, 0, Qt::AlignCenter);

    const QString tName = QFileInfo(assignment.targetPath).fileName();
    auto *tLabel = new QLabel(tName, this);
    tLabel->setAlignment(Qt::AlignCenter);
    tLabel->setStyleSheet(QStringLiteral("font-size: 9px; color: #999;"));
    tLabel->setWordWrap(true);
    tLabel->setToolTip(assignment.targetPath);
    layout->addWidget(tLabel);

    const QString oName = QFileInfo(assignment.overlayPath).fileName();
    const QString oDisp = oName.size() > 30 ? oName.left(30) + QStringLiteral("…") : oName;
    auto *oLabel = new QLabel(oDisp, this);
    oLabel->setAlignment(Qt::AlignCenter);
    oLabel->setStyleSheet(QStringLiteral("font-size: 9px; color: #88c0d0;"));
    oLabel->setWordWrap(true);
    oLabel->setToolTip(assignment.overlayPath);
    layout->addWidget(oLabel);
}

void AssignmentCard::setCompositeImage(const QImage &img)
{
    m_thumbLabel->setPixmap(
        QPixmap::fromImage(img).scaled(
            kThumbW, kThumbH,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
}
