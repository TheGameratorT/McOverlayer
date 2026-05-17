#include "OverlayLookupDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QTextEdit>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFont>

OverlayLookupDialog::OverlayLookupDialog(const QList<Core::TextureAssignment> &assignments, QWidget *parent)
    : QDialog(parent), m_assignments(assignments)
{
    setWindowTitle(QStringLiteral("Overlay Lookup"));
    setMinimumSize(520, 400);

    auto *layout = new QVBoxLayout(this);

    auto *row = new QHBoxLayout();
    row->addWidget(new QLabel(QStringLiteral("Overlay filename:"), this));
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(QStringLiteral("partial or full name, e.g. sakura.png"));
    m_input->setFont(QFont(QStringLiteral("monospace"), 9));
    connect(m_input, &QLineEdit::returnPressed, this, &OverlayLookupDialog::onSearch);
    row->addWidget(m_input, 1);
    m_exactMatch = new QCheckBox(QStringLiteral("Exact name"), this);
    row->addWidget(m_exactMatch);
    auto *btn = new QPushButton(QStringLiteral("Search"), this);
    connect(btn, &QPushButton::clicked, this, &OverlayLookupDialog::onSearch);
    row->addWidget(btn);
    layout->addLayout(row);

    m_resultLbl = new QLabel(QStringLiteral("Enter an overlay filename and press Search."), this);
    layout->addWidget(m_resultLbl);

    m_results = new QTextEdit(this);
    m_results->setReadOnly(true);
    m_results->setFont(QFont(QStringLiteral("monospace"), 9));
    layout->addWidget(m_results, 1);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(btns);
}

void OverlayLookupDialog::onSearch()
{
    const QString query = m_input->text().trimmed().toLower();
    if (query.isEmpty()) return;

    const bool exact = m_exactMatch->isChecked();
    QStringList matches;

    auto matchesFilter = [&](const QString &ovPath) {
        const QString name = QFileInfo(ovPath).fileName().toLower();
        return exact ? (query == name) : name.contains(query);
    };

    for (const Core::TextureAssignment &a : m_assignments) {
        bool found = matchesFilter(a.overlayPath);
        if (!found) {
            for (const QString &ov : a.faceOverlayPaths) {
                if (matchesFilter(ov)) { found = true; break; }
            }
        }
        if (found)
            matches.append(a.targetPath);
    }

    const QString matchType = exact ? QStringLiteral("exactly matching") : QStringLiteral("matching");
    if (!matches.isEmpty()) {
        m_resultLbl->setText(
            QString::number(matches.size()) + QStringLiteral(" texture(s) assigned to overlays ") +
            matchType + QStringLiteral(" '") + query + QStringLiteral("':"));
        m_results->setPlainText(matches.join(QLatin1Char('\n')));
    } else {
        m_resultLbl->setText(QStringLiteral("No textures found ") + matchType + QStringLiteral(" '") + query + QStringLiteral("'."));
        m_results->clear();
    }
}
