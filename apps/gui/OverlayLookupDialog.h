#pragma once

#include "core/Types.h"
#include <QDialog>

class QLineEdit;
class QCheckBox;
class QLabel;
class QTextEdit;

// Dialog: search which target textures are assigned to a given overlay filename.
class OverlayLookupDialog : public QDialog {
    Q_OBJECT
public:
    explicit OverlayLookupDialog(const QList<Core::TextureAssignment> &assignments, QWidget *parent = nullptr);

private slots:
    void onSearch();

private:
    QList<Core::TextureAssignment> m_assignments;
    QLineEdit *m_input      = nullptr;
    QCheckBox *m_exactMatch = nullptr;
    QLabel    *m_resultLbl  = nullptr;
    QTextEdit *m_results    = nullptr;
};
