#pragma once

#include "PathConfigWidget.h"
#include "core/MappingConfig.h"
#include <QWidget>

class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QComboBox;
class QPushButton;
class QWidget;

// Left sidebar: all mapping configuration controls.
// Emits configChanged(config) when the user clicks "Use These Settings".
class ConfigPanel : public QWidget {
    Q_OBJECT
public:
    explicit ConfigPanel(const Core::MappingConfig &config, QWidget *parent = nullptr);

    Core::MappingConfig getConfig() const;
    void setConfig(const Core::MappingConfig &config);
    void setSeed(qint64 seed);

    // Returns true if any UI control has changed since the last "Use These Settings" click.
    bool isDirty() const;
    // Equivalent to clicking "Use These Settings" — applies and emits configChanged.
    void applySettings();

signals:
    void configChanged(Core::MappingConfig config);

private slots:
    void onUpdatePreview();
    void browseOverlayDir();
    void browseTextureDir();
    void browseEntityDir();
    void onLoadConfig();
    void onSaveConfig();

private:
    void build();
    Core::MappingConfig collectConfig() const;

    Core::MappingConfig m_config;

    QLineEdit      *m_overlayDirEdit = nullptr;
    QLineEdit      *m_textureDirEdit = nullptr;
    QDoubleSpinBox *m_alphaSpin      = nullptr;
    QSpinBox       *m_scaleSpin      = nullptr;
    QDoubleSpinBox *m_ovlScaleSpin   = nullptr;
    QCheckBox      *m_perFrameCb     = nullptr;
    QCheckBox      *m_keepAspectCb   = nullptr;
    PathConfigWidget*m_pathConfigWgt  = nullptr;
    QLineEdit      *m_entityDirEdit  = nullptr;
    QSpinBox       *m_fastOvlSpin    = nullptr;
    QComboBox      *m_faceCombo      = nullptr;
    QComboBox      *m_texCombo       = nullptr;

    // Advanced section
    QWidget        *m_advWidget      = nullptr;
    QPushButton    *m_advToggleBtn   = nullptr;
};
