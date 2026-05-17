#pragma once

#include "core/Types.h"
#include <QFrame>
#include <QLabel>
#include <QImage>

// Fixed-width card showing a composite thumbnail, target filename and overlay filename.
class AssignmentCard : public QFrame {
    Q_OBJECT
public:
    explicit AssignmentCard(const Core::TextureAssignment &assignment, QWidget *parent = nullptr);

    // Called from the main thread when the background thumbnail loader finishes.
    void setCompositeImage(const QImage &img);

private:
    Core::TextureAssignment m_assignment;
    QLabel *m_thumbLabel = nullptr;
};
