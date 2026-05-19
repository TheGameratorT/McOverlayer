#pragma once

#include "core/MappingConfig.h"
#include <QDialog>
#include <QList>

class QListWidget;
class QLabel;
class QPushButton;

struct Preset {
    QString name;
    bool builtIn   = false;
    bool storeDirs = false;
    Core::MappingConfig config;
};

class PresetsDialog : public QDialog {
    Q_OBJECT
public:
    explicit PresetsDialog(const Core::MappingConfig &currentConfig, QWidget *parent = nullptr);

signals:
    void presetApplied(Core::MappingConfig config);

private slots:
    void onSelectionChanged(int row);
    void onApply();
    void onSaveCurrentAsPreset();
    void onDelete();

private:
    void build();
    void loadPresets();
    void refreshList();
    Core::MappingConfig mergePreset(const Preset &preset) const;
    QString buildPreview(const Preset &preset) const;
    QString presetsDir() const;

    Core::MappingConfig m_currentConfig;
    QList<Preset>       m_presets;

    QListWidget *m_list      = nullptr;
    QLabel      *m_preview   = nullptr;
    QPushButton *m_applyBtn  = nullptr;
    QPushButton *m_deleteBtn = nullptr;
};
