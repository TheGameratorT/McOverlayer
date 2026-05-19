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
#include <QMessageBox>
#include <QFile>
#include <QDir>

ConfigPanel::ConfigPanel(const Core::MappingConfig &config, QWidget *parent)
    : QWidget(parent), m_config(config)
{
    setMinimumWidth(220);
    build();
}

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

    // Shared helper — browse button for a directory field
    auto addDirRow = [&](QWidget *container, QVBoxLayout *lay,
                         const QString &label, const QString &tip,
                         QLineEdit *&edit, auto browseSlot) {
        auto *row = new QHBoxLayout();
        auto *lbl = new QLabel(label, container);
        lbl->setToolTip(tip);
        row->addWidget(lbl);
        edit = new QLineEdit(container);
        edit->setToolTip(tip);
        row->addWidget(edit);
        auto *btn = new QPushButton(QStringLiteral("…"), container);
        btn->setMaximumWidth(26);
        btn->setToolTip(QStringLiteral("Browse…"));
        connect(btn, &QPushButton::clicked, this, browseSlot);
        row->addWidget(btn);
        lay->addLayout(row);
    };

    // ----------------------------------------------------------------
    // Tab 1 — General
    // ----------------------------------------------------------------
    auto *genPage = new QWidget;
    auto *genLay  = new QVBoxLayout(genPage);
    genLay->setContentsMargins(8, 8, 8, 8);
    genLay->setSpacing(8);

    // Workflow hint
    auto *hintLabel = new QLabel(
        QStringLiteral("<small><b>How to use:</b> Set your directories below, click "
                       "<b>Use&nbsp;These&nbsp;Settings</b> to see the overlay assignments, then "
                       "use <b><nobr>▶&nbsp;Apply</nobr></b> in the toolbar to write the output.</small>"),
        genPage);
    hintLabel->setWordWrap(true);
    genLay->addWidget(hintLabel);

    // Paths group
    auto *pathsGrp = new QGroupBox(QStringLiteral("Paths"), genPage);
    auto *pathsLay = new QVBoxLayout(pathsGrp);
    pathsLay->setSpacing(4);
    addDirRow(pathsGrp, pathsLay,
              QStringLiteral("Overlay Images:"),
              QStringLiteral("Directory containing your source artwork (the images that will be composited onto the textures)."),
              m_overlayDirEdit, &ConfigPanel::browseOverlayDir);
    m_overlayDirEdit->setText(m_config.overlayDir);
    addDirRow(pathsGrp, pathsLay,
              QStringLiteral("Texture Images:"),
              QStringLiteral("Directory containing the textures to modify — typically an extracted Minecraft resource pack."),
              m_textureDirEdit, &ConfigPanel::browseTextureDir);
    m_textureDirEdit->setText(m_config.textureDir);
    genLay->addWidget(pathsGrp);

    // Settings group (essential settings only)
    auto *settingsGrp = new QGroupBox(QStringLiteral("Settings"), genPage);
    auto *settingsLay = new QVBoxLayout(settingsGrp);
    settingsLay->setSpacing(4);

    auto addDblRow = [&](QWidget *grp, QVBoxLayout *lay, const QString &label,
                         const QString &tip, QDoubleSpinBox *&spin,
                         double lo, double hi, double step, int decimals, double val) {
        auto *row = new QHBoxLayout();
        auto *lbl = new QLabel(label, grp);
        lbl->setToolTip(tip);
        row->addWidget(lbl);
        spin = new QDoubleSpinBox(grp);
        spin->setRange(lo, hi);
        spin->setSingleStep(step);
        spin->setDecimals(decimals);
        spin->setValue(val);
        spin->setToolTip(tip);
        row->addWidget(spin);
        lay->addLayout(row);
    };

    addDblRow(settingsGrp, settingsLay,
              QStringLiteral("Alpha:"),
              QStringLiteral("Blend strength of the overlay (0.0 = invisible, 0.75 = blended, 1.0 = full replacement)."),
              m_alphaSpin, 0.0, 1.0, 0.05, 2, m_config.alpha);

    addDblRow(settingsGrp, settingsLay,
              QStringLiteral("Overlay Scale:"),
              QStringLiteral("Resize the overlay relative to the texture before blending (1.0 = fill, 0.5 = tile 4×, 2.0 = zoom in)."),
              m_ovlScaleSpin, 0.0, 2.0, 0.1, 2, m_config.overlayScale);

    m_keepAspectCb = new QCheckBox(QStringLiteral("Keep aspect ratio"), settingsGrp);
    m_keepAspectCb->setChecked(m_config.keepAspect);
    m_keepAspectCb->setToolTip(QStringLiteral("Letterbox the overlay to preserve its original proportions instead of stretching it to fill the texture."));
    settingsLay->addWidget(m_keepAspectCb);

    genLay->addWidget(settingsGrp);

    // Advanced settings (collapsible)
    m_advToggleBtn = new QPushButton(QStringLiteral("▶  Advanced Settings"), genPage);
    m_advToggleBtn->setFlat(true);
    m_advToggleBtn->setStyleSheet(QStringLiteral(
        "QPushButton { text-align: left; font-weight: bold; padding: 2px 4px; }"
        "QPushButton:hover { color: palette(highlight); }"));
    m_advToggleBtn->setCursor(Qt::PointingHandCursor);
    genLay->addWidget(m_advToggleBtn);

    m_advWidget = new QWidget(genPage);
    auto *advLay = new QVBoxLayout(m_advWidget);
    advLay->setContentsMargins(4, 0, 4, 0);
    advLay->setSpacing(4);

    auto *advGrp = new QGroupBox(m_advWidget);
    auto *advGrpLay = new QVBoxLayout(advGrp);
    advGrpLay->setSpacing(4);

    {
        auto *row = new QHBoxLayout();
        auto *lbl = new QLabel(QStringLiteral("Scale:"), advGrp);
        lbl->setToolTip(QStringLiteral("Output upscale multiplier. 4 = 16 px → 64 px output. Higher values produce larger files."));
        row->addWidget(lbl);
        m_scaleSpin = new QSpinBox(advGrp);
        m_scaleSpin->setRange(1, 32);
        m_scaleSpin->setValue(m_config.scale);
        m_scaleSpin->setToolTip(lbl->toolTip());
        row->addWidget(m_scaleSpin);
        advGrpLay->addLayout(row);
    }
    {
        auto *row = new QHBoxLayout();
        auto *lbl = new QLabel(QStringLiteral("Fast overlay size:"), advGrp);
        lbl->setToolTip(QStringLiteral("Pre-load and cache overlays at this pixel size to speed up processing. 0 (Off) scales on demand per texture."));
        row->addWidget(lbl);
        m_fastOvlSpin = new QSpinBox(advGrp);
        m_fastOvlSpin->setRange(0, 2048);
        m_fastOvlSpin->setSingleStep(64);
        m_fastOvlSpin->setValue(m_config.fastOverlaySize);
        m_fastOvlSpin->setSuffix(QStringLiteral(" px"));
        m_fastOvlSpin->setSpecialValueText(QStringLiteral("Off"));
        m_fastOvlSpin->setToolTip(lbl->toolTip());
        row->addWidget(m_fastOvlSpin);
        advGrpLay->addLayout(row);
    }

    m_perFrameCb = new QCheckBox(QStringLiteral("Per-frame overlays"), advGrp);
    m_perFrameCb->setChecked(m_config.perFrame);
    m_perFrameCb->setToolTip(QStringLiteral("Assign a different overlay to each frame of animated textures (.mcmeta). Off = one overlay for all frames."));
    advGrpLay->addWidget(m_perFrameCb);

    advLay->addWidget(advGrp);
    m_advWidget->setVisible(false);
    genLay->addWidget(m_advWidget);

    connect(m_advToggleBtn, &QPushButton::clicked, this, [this]() {
        const bool show = !m_advWidget->isVisible();
        m_advWidget->setVisible(show);
        m_advToggleBtn->setText(show ? QStringLiteral("▼  Advanced Settings")
                                     : QStringLiteral("▶  Advanced Settings"));
    });

    genLay->addStretch();
    tabs->addTab(makeScrollTab(genPage), QStringLiteral("General"));

    // ----------------------------------------------------------------
    // Tab 2 — Path Config
    // ----------------------------------------------------------------
    auto *pathPage = new QWidget;
    auto *pathLay  = new QVBoxLayout(pathPage);
    pathLay->setContentsMargins(8, 8, 8, 8);
    pathLay->setSpacing(6);

    auto *pathHint = new QLabel(
        QStringLiteral("<small>Override settings for specific files or path prefixes — "
                       "for example keep <tt>pack.png</tt> at scale&nbsp;1, or use "
                       "higher alpha for a particular folder.</small>"),
        pathPage);
    pathHint->setWordWrap(true);
    pathLay->addWidget(pathHint);

    m_pathConfigWgt = new PathConfigWidget(m_config.pathConfig, pathPage);
    pathLay->addWidget(m_pathConfigWgt);
    tabs->addTab(makeScrollTab(pathPage), QStringLiteral("Path Config"));

    // ----------------------------------------------------------------
    // Tab 3 — Entity
    // ----------------------------------------------------------------
    auto *entPage = new QWidget;
    auto *entLay  = new QVBoxLayout(entPage);
    entLay->setContentsMargins(8, 8, 8, 8);
    entLay->setSpacing(8);

    auto *entHint = new QLabel(
        QStringLiteral("<small>Entity skin settings. Assigns separate overlays to each "
                       "body-part region of a Minecraft entity skin. "
                       "Skip this tab if you only use block/item textures.</small>"),
        entPage);
    entHint->setWordWrap(true);
    entLay->addWidget(entHint);

    auto *entGrp = new QGroupBox(QStringLiteral("Entity Settings"), entPage);
    auto *entGrpLay = new QVBoxLayout(entGrp);
    entGrpLay->setSpacing(4);

    auto addComboRow = [&](const QString &label, const QString &tip, QComboBox *&combo,
                           const QStringList &items, const QString &current) {
        auto *row = new QHBoxLayout();
        auto *lbl = new QLabel(label, entGrp);
        lbl->setToolTip(tip);
        row->addWidget(lbl);
        combo = new QComboBox(entGrp);
        combo->addItems(items);
        combo->setCurrentText(current);
        combo->setToolTip(tip);
        row->addWidget(combo);
        entGrpLay->addLayout(row);
    };
    addComboRow(QStringLiteral("Face mode:"),
                QStringLiteral("\"same\" = one overlay per body part (e.g. all head faces share one image). "
                               "\"different\" = each face region gets its own independent overlay."),
                m_faceCombo,
                {QStringLiteral("same"), QStringLiteral("different")},
                m_config.entityFaceMode);
    addComboRow(QStringLiteral("Texture mode:"),
                QStringLiteral("\"shared\" = entities that share a texture file (e.g. colour variants) get the same overlay. "
                               "\"separate\" = each texture file gets an independent assignment."),
                m_texCombo,
                {QStringLiteral("shared"), QStringLiteral("separate")},
                m_config.entityTextureMode);

    entLay->addWidget(entGrp);

    // Advanced options — entity regions directory
    auto *entAdvToggleBtn = new QPushButton(QStringLiteral("▶  Advanced Settings"), entPage);
    entAdvToggleBtn->setFlat(true);
    entAdvToggleBtn->setStyleSheet(QStringLiteral(
        "QPushButton { text-align: left; font-weight: bold; padding: 2px 4px; }"
        "QPushButton:hover { color: palette(highlight); }"));
    entAdvToggleBtn->setCursor(Qt::PointingHandCursor);
    entLay->addWidget(entAdvToggleBtn);

    auto *entAdvWidget = new QWidget(entPage);
    auto *entAdvLay = new QVBoxLayout(entAdvWidget);
    entAdvLay->setContentsMargins(4, 0, 4, 0);
    entAdvLay->setSpacing(6);

    auto *entAdvGrp = new QGroupBox(entAdvWidget);
    auto *entAdvGrpLay = new QVBoxLayout(entAdvGrp);
    entAdvGrpLay->setSpacing(6);

    auto *regHint = new QLabel(
        QStringLiteral("The app-bundled <tt>entity_regions/</tt> directory is used by default. "
                       "Override it here only if you have a custom set of region definitions. "
                       "Click ⟳ to reset back to the bundled directory."),
        entAdvGrp);
    regHint->setWordWrap(true);
    entAdvGrpLay->addWidget(regHint);

    auto *regRow = new QHBoxLayout();
    auto *regLbl = new QLabel(QStringLiteral("Regions:"), entAdvGrp);
    regLbl->setToolTip(QStringLiteral("Path to the entity_regions/ directory containing JSON region definitions for each entity skin."));
    regRow->addWidget(regLbl);
    m_entityDirEdit = new QLineEdit(entAdvGrp);
    m_entityDirEdit->setPlaceholderText(QStringLiteral("entity_regions/"));
    m_entityDirEdit->setText(m_config.entityRegionsDir);
    m_entityDirEdit->setToolTip(regLbl->toolTip());
    regRow->addWidget(m_entityDirEdit);
    auto *resetEntityBtn = new QPushButton(QStringLiteral("⟳"), entAdvGrp);
    resetEntityBtn->setMaximumWidth(26);
    resetEntityBtn->setToolTip(QStringLiteral("Reset to the app-bundled entity_regions directory"));
    connect(resetEntityBtn, &QPushButton::clicked, this, [this]() {
        m_entityDirEdit->setText(Core::MappingConfig::defaultEntityRegionsDir());
    });
    regRow->addWidget(resetEntityBtn);
    auto *browseEntityBtn = new QPushButton(QStringLiteral("…"), entAdvGrp);
    browseEntityBtn->setMaximumWidth(26);
    browseEntityBtn->setToolTip(QStringLiteral("Browse…"));
    connect(browseEntityBtn, &QPushButton::clicked, this, &ConfigPanel::browseEntityDir);
    regRow->addWidget(browseEntityBtn);
    entAdvGrpLay->addLayout(regRow);

    entAdvLay->addWidget(entAdvGrp);
    entAdvWidget->setVisible(false);
    entLay->addWidget(entAdvWidget);

    connect(entAdvToggleBtn, &QPushButton::clicked, this, [entAdvWidget, entAdvToggleBtn]() {
        const bool show = !entAdvWidget->isVisible();
        entAdvWidget->setVisible(show);
        entAdvToggleBtn->setText(show ? QStringLiteral("▼  Advanced Settings")
                                      : QStringLiteral("▶  Advanced Settings"));
    });

    entLay->addStretch();
    tabs->addTab(makeScrollTab(entPage), QStringLiteral("Entity"));

    // ----------------------------------------------------------------
    // Bottom buttons
    // ----------------------------------------------------------------
    auto *loadSaveRow = new QHBoxLayout();
    auto *loadBtn = new QPushButton(QStringLiteral("Load Config…"), this);
    loadBtn->setToolTip(QStringLiteral("Load a previously saved configuration file (JSON)."));
    connect(loadBtn, &QPushButton::clicked, this, &ConfigPanel::onLoadConfig);
    loadSaveRow->addWidget(loadBtn);
    auto *saveBtn = new QPushButton(QStringLiteral("Save Config…"), this);
    saveBtn->setToolTip(QStringLiteral("Save the current configuration to a JSON file."));
    connect(saveBtn, &QPushButton::clicked, this, &ConfigPanel::onSaveConfig);
    loadSaveRow->addWidget(saveBtn);
    outerLayout->addLayout(loadSaveRow);

    auto *updateBtn = new QPushButton(QStringLiteral("Use These Settings"), this);
    updateBtn->setToolTip(QStringLiteral("Apply all settings above and rebuild the assignment preview grid."));
    connect(updateBtn, &QPushButton::clicked, this, &ConfigPanel::onUpdatePreview);
    outerLayout->addWidget(updateBtn);
}

Core::MappingConfig ConfigPanel::collectConfig() const
{
    Core::MappingConfig c = m_config;
    c.overlayDir       = m_overlayDirEdit->text().trimmed();
    c.textureDir       = m_textureDirEdit->text().trimmed();
    c.alpha            = m_alphaSpin->value();
    c.scale            = m_scaleSpin->value();
    c.overlayScale     = m_ovlScaleSpin->value();
    c.perFrame         = m_perFrameCb->isChecked();
    c.keepAspect       = m_keepAspectCb->isChecked();
    c.fastOverlaySize  = m_fastOvlSpin->value();
    c.pathConfig       = m_pathConfigWgt->toJsonStr();
    c.entityRegionsDir = m_entityDirEdit->text().trimmed();
    c.entityFaceMode   = m_faceCombo->currentText();
    c.entityTextureMode= m_texCombo->currentText();
    return c;
}

bool ConfigPanel::isDirty() const
{
    const Core::MappingConfig c = collectConfig();
    return c.overlayDir        != m_config.overlayDir
        || c.textureDir        != m_config.textureDir
        || c.alpha             != m_config.alpha
        || c.scale             != m_config.scale
        || c.overlayScale      != m_config.overlayScale
        || c.perFrame          != m_config.perFrame
        || c.keepAspect        != m_config.keepAspect
        || c.fastOverlaySize   != m_config.fastOverlaySize
        || c.pathConfig        != m_config.pathConfig
        || c.entityRegionsDir  != m_config.entityRegionsDir
        || c.entityFaceMode    != m_config.entityFaceMode
        || c.entityTextureMode != m_config.entityTextureMode;
}

void ConfigPanel::applySettings()
{
    onUpdatePreview();
}

void ConfigPanel::onUpdatePreview()
{
    m_config = collectConfig();
    emit configChanged(m_config);
}

void ConfigPanel::onLoadConfig()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Load Configuration"),
        QString{},
        QStringLiteral("JSON Config Files (*.json);;All Files (*)"));
    if (path.isEmpty()) return;

    try {
        Core::MappingConfig loaded = Core::MappingConfig::fromLastRun(path);

        if (!loaded.entityRegionsDir.isEmpty() && !QDir(loaded.entityRegionsDir).exists()) {
            const QString missing = loaded.entityRegionsDir;
            loaded.entityRegionsDir = Core::MappingConfig::defaultEntityRegionsDir();
            QMessageBox::warning(this, QStringLiteral("Entity Regions Not Found"),
                QStringLiteral("The entity regions directory from the loaded config could not be found:\n\n"
                               "%1\n\n"
                               "It has been reset to the app-bundled directory.")
                .arg(missing));
        }

        setConfig(loaded);
        emit configChanged(m_config);
    } catch (const std::exception &e) {
        QMessageBox::warning(this, QStringLiteral("Load Failed"),
                             QStringLiteral("Could not load config:\n") + QString::fromStdString(e.what()));
    }
}

void ConfigPanel::onSaveConfig()
{
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Save Configuration"),
        QStringLiteral("config.json"),
        QStringLiteral("JSON Config Files (*.json);;All Files (*)"));
    if (path.isEmpty()) return;

    const Core::MappingConfig current = collectConfig();
    current.saveLastRun(path);
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
    blockSet(m_fastOvlSpin,     [&]{ m_fastOvlSpin->setValue(config.fastOverlaySize); });
    blockSet(m_faceCombo,       [&]{ m_faceCombo->setCurrentText(config.entityFaceMode); });
    blockSet(m_texCombo,        [&]{ m_texCombo->setCurrentText(config.entityTextureMode); });
    m_pathConfigWgt->fromJsonStr(config.pathConfig);
}
