#include "ConfigPanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QFileDialog>
#include <QTabWidget>
#include <QScrollArea>

ConfigPanel::ConfigPanel(const Core::MappingConfig &config, QWidget *parent)
    : QWidget(parent), m_config(config)
{
    setMinimumWidth(220);
    build();
}

// Wrap a widget in a QScrollArea that expands with the content.
static QScrollArea *makeScrollTab(QWidget *contents)
{
    auto *scroll = new QScrollArea;
    scroll->setWidget(contents);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

void ConfigPanel::build()
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(6, 6, 6, 6);
    outerLayout->setSpacing(6);

    auto *tabs = new QTabWidget(this);
    outerLayout->addWidget(tabs, 1);

    // Shared helpers — parent to `this` so Qt owns them regardless of tab page lifetime
    auto addDirRow = [&](QWidget *container, QVBoxLayout *lay,
                         const QString &label, QLineEdit *&edit, auto browseSlot) {
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel(label, container));
        edit = new QLineEdit(container);
        row->addWidget(edit);
        auto *btn = new QPushButton(QStringLiteral("…"), container);
        btn->setMaximumWidth(26);
        connect(btn, &QPushButton::clicked, this, browseSlot);
        row->addWidget(btn);
        lay->addLayout(row);
    };

    // ----------------------------------------------------------------
    // Tab 1 — General (paths + core settings)
    // ----------------------------------------------------------------
    auto *genPage = new QWidget;
    auto *genLay  = new QVBoxLayout(genPage);
    genLay->setContentsMargins(8, 8, 8, 8);
    genLay->setSpacing(8);

    auto *pathsGrp = new QGroupBox(QStringLiteral("Paths"), genPage);
    auto *pathsLay = new QVBoxLayout(pathsGrp);
    pathsLay->setSpacing(4);
    addDirRow(pathsGrp, pathsLay, QStringLiteral("Overlay Images:"), m_overlayDirEdit, &ConfigPanel::browseOverlayDir);
    m_overlayDirEdit->setText(m_config.overlayDir);
    addDirRow(pathsGrp, pathsLay, QStringLiteral("Texture Images:"), m_textureDirEdit, &ConfigPanel::browseTextureDir);
    m_textureDirEdit->setText(m_config.textureDir);
    genLay->addWidget(pathsGrp);

    auto *coreGrp = new QGroupBox(QStringLiteral("Core Settings"), genPage);
    auto *coreLay = new QVBoxLayout(coreGrp);
    coreLay->setSpacing(4);

    auto addDblRow = [&](const QString &label, QDoubleSpinBox *&spin,
                         double lo, double hi, double step, int decimals, double val) {
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel(label, coreGrp));
        spin = new QDoubleSpinBox(coreGrp);
        spin->setRange(lo, hi); spin->setSingleStep(step); spin->setDecimals(decimals); spin->setValue(val);
        row->addWidget(spin);
        coreLay->addLayout(row);
    };
    addDblRow(QStringLiteral("Alpha:"),         m_alphaSpin,    0.0, 1.0, 0.05, 2, m_config.alpha);
    addDblRow(QStringLiteral("Overlay Scale:"), m_ovlScaleSpin, 0.0, 2.0, 0.1,  2, m_config.overlayScale);

    {
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel(QStringLiteral("Scale:"), coreGrp));
        m_scaleSpin = new QSpinBox(coreGrp);
        m_scaleSpin->setRange(1, 32);
        m_scaleSpin->setValue(m_config.scale);
        row->addWidget(m_scaleSpin);
        coreLay->addLayout(row);
    }
    {
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel(QStringLiteral("Fast overlay size:"), coreGrp));
        m_fastOvlSpin = new QSpinBox(coreGrp);
        m_fastOvlSpin->setRange(0, 2048);
        m_fastOvlSpin->setSingleStep(64);
        m_fastOvlSpin->setValue(m_config.fastOverlaySize);
        m_fastOvlSpin->setSuffix(QStringLiteral(" px"));
        m_fastOvlSpin->setSpecialValueText(QStringLiteral("Off"));
        row->addWidget(m_fastOvlSpin);
        coreLay->addLayout(row);
    }

    m_perFrameCb = new QCheckBox(QStringLiteral("Per-frame overlays"), coreGrp);
    m_perFrameCb->setChecked(m_config.perFrame);
    coreLay->addWidget(m_perFrameCb);

    m_keepAspectCb = new QCheckBox(QStringLiteral("Keep aspect ratio"), coreGrp);
    m_keepAspectCb->setChecked(m_config.keepAspect);
    coreLay->addWidget(m_keepAspectCb);

    genLay->addWidget(coreGrp);
    genLay->addStretch();
    tabs->addTab(makeScrollTab(genPage), QStringLiteral("General"));

    // ----------------------------------------------------------------
    // Tab 2 — Path Config
    // ----------------------------------------------------------------
    auto *pathPage = new QWidget;
    auto *pathLay  = new QVBoxLayout(pathPage);
    pathLay->setContentsMargins(8, 8, 8, 8);
    m_pathConfigWgt = new PathConfigWidget(m_config.pathConfig, pathPage);
    pathLay->addWidget(m_pathConfigWgt);
    pathLay->addStretch();
    tabs->addTab(makeScrollTab(pathPage), QStringLiteral("Path Config"));

    // ----------------------------------------------------------------
    // Tab 3 — Entity
    // ----------------------------------------------------------------
    auto *entPage = new QWidget;
    auto *entLay  = new QVBoxLayout(entPage);
    entLay->setContentsMargins(8, 8, 8, 8);
    entLay->setSpacing(8);

    auto *entGrp = new QGroupBox(QStringLiteral("Entity Settings"), entPage);
    auto *entGrpLay = new QVBoxLayout(entGrp);
    entGrpLay->setSpacing(4);
    addDirRow(entGrp, entGrpLay, QStringLiteral("Regions:"), m_entityDirEdit, &ConfigPanel::browseEntityDir);
    m_entityDirEdit->setPlaceholderText(QStringLiteral("entity_regions/"));
    m_entityDirEdit->setText(m_config.entityRegionsDir);

    auto addComboRow = [&](const QString &label, QComboBox *&combo,
                           const QStringList &items, const QString &current) {
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel(label, entGrp));
        combo = new QComboBox(entGrp);
        combo->addItems(items);
        combo->setCurrentText(current);
        row->addWidget(combo);
        entGrpLay->addLayout(row);
    };
    addComboRow(QStringLiteral("Face mode:"),    m_faceCombo,
                {QStringLiteral("same"), QStringLiteral("different")},
                m_config.entityFaceMode);
    addComboRow(QStringLiteral("Texture mode:"), m_texCombo,
                {QStringLiteral("shared"), QStringLiteral("separate")},
                m_config.entityTextureMode);

    entLay->addWidget(entGrp);
    entLay->addStretch();
    tabs->addTab(makeScrollTab(entPage), QStringLiteral("Entity"));

    // ----------------------------------------------------------------
    // Update Preview button — always visible below the tabs
    // ----------------------------------------------------------------
    auto *updateBtn = new QPushButton(QStringLiteral("Update Preview"), this);
    connect(updateBtn, &QPushButton::clicked, this, &ConfigPanel::onUpdatePreview);
    outerLayout->addWidget(updateBtn);
}

void ConfigPanel::onUpdatePreview()
{
    m_config.overlayDir       = m_overlayDirEdit->text().trimmed();
    m_config.textureDir       = m_textureDirEdit->text().trimmed();
    m_config.alpha            = m_alphaSpin->value();
    m_config.scale            = m_scaleSpin->value();
    m_config.overlayScale     = m_ovlScaleSpin->value();
    m_config.perFrame         = m_perFrameCb->isChecked();
    m_config.keepAspect       = m_keepAspectCb->isChecked();
    m_config.fastOverlaySize  = m_fastOvlSpin->value();
    m_config.pathConfig       = m_pathConfigWgt->toJsonStr();
    m_config.entityRegionsDir = m_entityDirEdit->text().trimmed();
    m_config.entityFaceMode   = m_faceCombo->currentText();
    m_config.entityTextureMode= m_texCombo->currentText();
    emit configChanged(m_config);
}

void ConfigPanel::browseOverlayDir()
{
    const QString p = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Overlay Images Directory"), m_overlayDirEdit->text());
    if (!p.isEmpty()) m_overlayDirEdit->setText(p);
}

void ConfigPanel::browseTextureDir()
{
    const QString p = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Texture Images Directory"), m_textureDirEdit->text());
    if (!p.isEmpty()) m_textureDirEdit->setText(p);
}

void ConfigPanel::browseEntityDir()
{
    const QString p = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Entity Regions Directory"), m_entityDirEdit->text());
    if (!p.isEmpty()) m_entityDirEdit->setText(p);
}

Core::MappingConfig ConfigPanel::getConfig() const { return m_config; }

void ConfigPanel::setSeed(qint64 seed) { m_config.seed = seed; }

void ConfigPanel::setConfig(const Core::MappingConfig &config)
{
    m_config = config;

    auto blockSet = [](QWidget *w, auto fn) {
        w->blockSignals(true); fn(); w->blockSignals(false);
    };

    blockSet(m_overlayDirEdit,  [&]{ m_overlayDirEdit->setText(config.overlayDir); });
    blockSet(m_textureDirEdit,  [&]{ m_textureDirEdit->setText(config.textureDir); });
    blockSet(m_alphaSpin,       [&]{ m_alphaSpin->setValue(config.alpha); });
    blockSet(m_scaleSpin,       [&]{ m_scaleSpin->setValue(config.scale); });
    blockSet(m_ovlScaleSpin,    [&]{ m_ovlScaleSpin->setValue(config.overlayScale); });
    blockSet(m_perFrameCb,      [&]{ m_perFrameCb->setChecked(config.perFrame); });
    blockSet(m_keepAspectCb,    [&]{ m_keepAspectCb->setChecked(config.keepAspect); });
    blockSet(m_entityDirEdit,   [&]{ m_entityDirEdit->setText(config.entityRegionsDir); });
    blockSet(m_fastOvlSpin, [&]{ m_fastOvlSpin->setValue(config.fastOverlaySize); });
    blockSet(m_faceCombo,   [&]{ m_faceCombo->setCurrentText(config.entityFaceMode); });
    blockSet(m_texCombo,    [&]{ m_texCombo->setCurrentText(config.entityTextureMode); });
    m_pathConfigWgt->fromJsonStr(config.pathConfig);
}
