#pragma once

#include <QJsonObject>
#include <QVariantMap>
#include <QWidget>

class QListWidget;
class QGroupBox;
class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QComboBox;
class QPushButton;

// Widget for editing the per-path setting overrides map.
// Displays a list of path entries; selecting one shows its detail panel.
// Keys can be exact file paths ("pack.png") or directory prefixes ("assets/block").
class PathConfigWidget : public QWidget {
    Q_OBJECT
public:
    explicit PathConfigWidget(const QJsonObject &config = {}, QWidget *parent = nullptr);

    QJsonObject toJson() const;
    void fromJson(const QJsonObject &obj);

signals:
    void changed();

private slots:
    void onAddEntry();
    void onRemoveEntry();
    void onSelectionChanged(int row);
    void onDetailChanged();

private:
    void build();
    QString formatItem(const QVariantMap &data) const;

    bool m_loading = false;

    QListWidget   *m_list        = nullptr;
    QPushButton   *m_removeBtn   = nullptr;
    QGroupBox     *m_detail      = nullptr;

    QLineEdit     *m_prefixEdit  = nullptr;
    QCheckBox     *m_scaleCb     = nullptr;
    QSpinBox      *m_scaleSpin   = nullptr;
    QCheckBox     *m_alphaCb     = nullptr;
    QDoubleSpinBox*m_alphaSpin   = nullptr;
    QCheckBox     *m_ovlCb       = nullptr;
    QDoubleSpinBox*m_ovlSpin     = nullptr;
    QCheckBox     *m_kaCb        = nullptr;
    QComboBox     *m_kaCombo     = nullptr;
    QCheckBox     *m_fastOvlCb   = nullptr;
    QSpinBox      *m_fastOvlSpin = nullptr;
};
