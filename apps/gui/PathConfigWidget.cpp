#include "PathConfigWidget.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QSplitter>
#include <QJsonDocument>
#include <QJsonObject>

PathConfigWidget::PathConfigWidget(const QString &jsonStr, QWidget *parent)
    : QWidget(parent)
{
    build();
    if (!jsonStr.isEmpty())
        fromJsonStr(jsonStr);
}

void PathConfigWidget::build()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->setChildrenCollapsible(false);
    layout->addWidget(splitter);

    // Top pane: list + Add/Remove buttons
    auto *topPane = new QWidget(splitter);
    auto *topLay  = new QVBoxLayout(topPane);
    topLay->setContentsMargins(0, 0, 0, 4);
    topLay->setSpacing(4);

    m_list = new QListWidget(topPane);
    connect(m_list, &QListWidget::currentRowChanged, this, &PathConfigWidget::onSelectionChanged);
    topLay->addWidget(m_list);

    auto *btnRow = new QHBoxLayout();
    auto *addBtn = new QPushButton(QStringLiteral("+ Add"), topPane);
    addBtn->setMaximumWidth(60);
    connect(addBtn, &QPushButton::clicked, this, &PathConfigWidget::onAddEntry);
    btnRow->addWidget(addBtn);
    m_removeBtn = new QPushButton(QStringLiteral("- Remove"), topPane);
    m_removeBtn->setMaximumWidth(75);
    m_removeBtn->setEnabled(false);
    connect(m_removeBtn, &QPushButton::clicked, this, &PathConfigWidget::onRemoveEntry);
    btnRow->addWidget(m_removeBtn);
    btnRow->addStretch();
    topLay->addLayout(btnRow);

    splitter->addWidget(topPane);
    splitter->setStretchFactor(0, 1);

    // Bottom pane: entry detail
    m_detail = new QGroupBox(QStringLiteral("Entry Settings"), splitter);
    splitter->addWidget(m_detail);
    splitter->setStretchFactor(1, 2);
    m_detail->setVisible(false);
    auto *det = new QVBoxLayout(m_detail);
    det->setSpacing(3);

    // Path key (exact file or directory prefix)
    auto *prefixRow = new QHBoxLayout();
    prefixRow->addWidget(new QLabel(QStringLiteral("Path:"), this));
    m_prefixEdit = new QLineEdit(this);
    m_prefixEdit->setPlaceholderText(QStringLiteral("e.g. pack.png, assets/block"));
    connect(m_prefixEdit, &QLineEdit::textChanged, this, &PathConfigWidget::onDetailChanged);
    prefixRow->addWidget(m_prefixEdit);
    det->addLayout(prefixRow);

    auto addSpinRow = [&](QCheckBox *&cb, const QString &label, auto *&spin) {
        auto *row = new QHBoxLayout();
        cb = new QCheckBox(label, this);
        connect(cb, &QCheckBox::toggled, this, &PathConfigWidget::onDetailChanged);
        row->addWidget(cb);
        spin->setEnabled(false);
        connect(cb, &QCheckBox::toggled, spin, &QWidget::setEnabled);
        connect(spin, &std::remove_pointer_t<std::decay_t<decltype(spin)>>::valueChanged, this, &PathConfigWidget::onDetailChanged);
        row->addWidget(spin);
        row->addStretch();
        det->addLayout(row);
    };

    m_scaleSpin = new QSpinBox(this); m_scaleSpin->setRange(1, 32); m_scaleSpin->setValue(4);
    addSpinRow(m_scaleCb, QStringLiteral("Scale:"), m_scaleSpin);

    m_alphaSpin = new QDoubleSpinBox(this);
    m_alphaSpin->setRange(0.0, 1.0); m_alphaSpin->setSingleStep(0.05); m_alphaSpin->setDecimals(2); m_alphaSpin->setValue(0.75);
    addSpinRow(m_alphaCb, QStringLiteral("Alpha:"), m_alphaSpin);

    m_ovlSpin = new QDoubleSpinBox(this);
    m_ovlSpin->setRange(0.0, 2.0); m_ovlSpin->setSingleStep(0.1); m_ovlSpin->setDecimals(2); m_ovlSpin->setValue(1.0);
    addSpinRow(m_ovlCb, QStringLiteral("Overlay scale:"), m_ovlSpin);

    m_fastOvlSpin = new QSpinBox(this);
    m_fastOvlSpin->setRange(0, 2048); m_fastOvlSpin->setSingleStep(64); m_fastOvlSpin->setValue(128);
    m_fastOvlSpin->setSuffix(QStringLiteral(" px"));
    m_fastOvlSpin->setSpecialValueText(QStringLiteral("Off"));
    addSpinRow(m_fastOvlCb, QStringLiteral("Fast overlay size:"), m_fastOvlSpin);

    // Keep aspect (bool, uses combo not spin)
    auto *kaRow = new QHBoxLayout();
    m_kaCb = new QCheckBox(QStringLiteral("Keep aspect:"), this);
    connect(m_kaCb, &QCheckBox::toggled, this, &PathConfigWidget::onDetailChanged);
    kaRow->addWidget(m_kaCb);
    m_kaCombo = new QComboBox(this);
    m_kaCombo->addItems({QStringLiteral("Yes"), QStringLiteral("No")});
    m_kaCombo->setEnabled(false);
    connect(m_kaCb, &QCheckBox::toggled, m_kaCombo, &QWidget::setEnabled);
    connect(m_kaCombo, &QComboBox::currentIndexChanged, this, &PathConfigWidget::onDetailChanged);
    kaRow->addWidget(m_kaCombo);
    kaRow->addStretch();
    det->addLayout(kaRow);

}

QString PathConfigWidget::formatItem(const QVariantMap &data) const
{
    QStringList parts;
    parts.append(data.value(QStringLiteral("prefix"), QStringLiteral("unnamed")).toString());
    if (data.contains(QStringLiteral("scale")))
        parts.append(QStringLiteral("scale=") + QString::number(data.value(QStringLiteral("scale")).toInt()));
    if (data.contains(QStringLiteral("alpha")))
        parts.append(QStringLiteral("α=") + QString::number(data.value(QStringLiteral("alpha")).toDouble(), 'f', 2));
    if (data.contains(QStringLiteral("overlay-scale")))
        parts.append(QStringLiteral("ovl=") + QString::number(data.value(QStringLiteral("overlay-scale")).toDouble(), 'f', 2));
    if (data.contains(QStringLiteral("keep-aspect")))
        parts.append(data.value(QStringLiteral("keep-aspect")).toBool() ? QStringLiteral("ka=Y") : QStringLiteral("ka=N"));
    if (data.contains(QStringLiteral("fast-overlay-size")))
        parts.append(QStringLiteral("fast=") + QString::number(data.value(QStringLiteral("fast-overlay-size")).toInt()) + QStringLiteral("px"));
    return parts.join(QStringLiteral("  "));
}

void PathConfigWidget::onAddEntry()
{
    QVariantMap data;
    data.insert(QStringLiteral("prefix"), QStringLiteral("new_path"));
    auto *item = new QListWidgetItem(formatItem(data), m_list);
    item->setData(Qt::UserRole, data);
    m_list->setCurrentRow(m_list->count() - 1);
    emit changed();
}

void PathConfigWidget::onRemoveEntry()
{
    const int row = m_list->currentRow();
    if (row >= 0) {
        delete m_list->takeItem(row);
        if (m_list->count() == 0) {
            m_detail->setVisible(false);
            m_removeBtn->setEnabled(false);
        }
        emit changed();
    }
}

void PathConfigWidget::onSelectionChanged(int row)
{
    if (row < 0) {
        m_detail->setVisible(false);
        m_removeBtn->setEnabled(false);
        return;
    }
    m_removeBtn->setEnabled(true);
    m_detail->setVisible(true);

    const QVariantMap data = m_list->item(row)->data(Qt::UserRole).toMap();
    m_loading = true;

    m_prefixEdit->setText(data.value(QStringLiteral("prefix")).toString());

    auto setRow = [&](QCheckBox *cb, auto *spin, const QString &key) {
        const bool has = data.contains(key);
        cb->setChecked(has);
        spin->setEnabled(has);
        if (has) spin->setValue(data.value(key).template value<double>());
    };
    setRow(m_scaleCb,   m_scaleSpin,   QStringLiteral("scale"));
    setRow(m_alphaCb,   m_alphaSpin,   QStringLiteral("alpha"));
    setRow(m_ovlCb,     m_ovlSpin,     QStringLiteral("overlay-scale"));
    setRow(m_fastOvlCb, m_fastOvlSpin, QStringLiteral("fast-overlay-size"));

    const bool hasKa = data.contains(QStringLiteral("keep-aspect"));
    m_kaCb->setChecked(hasKa);
    m_kaCombo->setEnabled(hasKa);
    if (hasKa)
        m_kaCombo->setCurrentText(data.value(QStringLiteral("keep-aspect")).toBool()
                                      ? QStringLiteral("Yes") : QStringLiteral("No"));

    m_loading = false;
}

void PathConfigWidget::onDetailChanged()
{
    if (m_loading) return;
    const int row = m_list->currentRow();
    if (row < 0) return;

    QVariantMap data;
    data.insert(QStringLiteral("prefix"),
                m_prefixEdit->text().trimmed().isEmpty()
                    ? QStringLiteral("unnamed") : m_prefixEdit->text().trimmed());
    if (m_scaleCb->isChecked())   data.insert(QStringLiteral("scale"),            m_scaleSpin->value());
    if (m_alphaCb->isChecked())   data.insert(QStringLiteral("alpha"),            m_alphaSpin->value());
    if (m_ovlCb->isChecked())     data.insert(QStringLiteral("overlay-scale"),    m_ovlSpin->value());
    if (m_fastOvlCb->isChecked()) data.insert(QStringLiteral("fast-overlay-size"), m_fastOvlSpin->value());
    if (m_kaCb->isChecked())      data.insert(QStringLiteral("keep-aspect"),       m_kaCombo->currentText() == QStringLiteral("Yes"));

    QListWidgetItem *item = m_list->item(row);
    item->setData(Qt::UserRole, data);
    item->setText(formatItem(data));
    emit changed();
}

QString PathConfigWidget::toJsonStr() const
{
    QJsonObject root;
    for (int i = 0; i < m_list->count(); ++i) {
        const QVariantMap data = m_list->item(i)->data(Qt::UserRole).toMap();
        const QString prefix = data.value(QStringLiteral("prefix")).toString().trimmed();
        if (prefix.isEmpty()) continue;
        QJsonObject settings;
        if (data.contains(QStringLiteral("scale")))           settings.insert(QStringLiteral("scale"),            data.value(QStringLiteral("scale")).toInt());
        if (data.contains(QStringLiteral("alpha")))           settings.insert(QStringLiteral("alpha"),            data.value(QStringLiteral("alpha")).toDouble());
        if (data.contains(QStringLiteral("overlay-scale")))   settings.insert(QStringLiteral("overlay-scale"),    data.value(QStringLiteral("overlay-scale")).toDouble());
        if (data.contains(QStringLiteral("fast-overlay-size")))settings.insert(QStringLiteral("fast-overlay-size"), data.value(QStringLiteral("fast-overlay-size")).toInt());
        if (data.contains(QStringLiteral("keep-aspect")))     settings.insert(QStringLiteral("keep-aspect"),        data.value(QStringLiteral("keep-aspect")).toBool());
        root.insert(prefix, settings);
    }
    return root.isEmpty() ? QString{} : QJsonDocument(root).toJson(QJsonDocument::Compact);
}

void PathConfigWidget::fromJsonStr(const QString &s)
{
    m_list->clear();
    m_detail->setVisible(false);
    m_removeBtn->setEnabled(false);
    if (s.isEmpty()) return;

    QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8());
    if (doc.isNull() || !doc.isObject()) return;

    for (const QString &prefix : doc.object().keys()) {
        const QJsonObject settings = doc.object().value(prefix).toObject();
        QVariantMap data;
        data.insert(QStringLiteral("prefix"), prefix);
        for (const QString &k : settings.keys())
            data.insert(k, settings.value(k).toVariant());
        auto *item = new QListWidgetItem(formatItem(data), m_list);
        item->setData(Qt::UserRole, data);
    }
}
