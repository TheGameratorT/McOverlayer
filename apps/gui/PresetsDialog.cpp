#include "PresetsDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QDialog>
#include <QInputDialog>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QFrame>

// ---------------------------------------------------------------------------
// Built-in presets
// ---------------------------------------------------------------------------

static Preset makeDefault32xPreset()
{
    Core::MappingConfig c;
    c.alpha             = 0.4;
    c.overlayScale      = 1.0;
    c.scale             = 4;
    c.keepAspect        = true;
    c.perFrame          = true;
    c.fastOverlaySize   = 512;
    c.entityFaceMode    = QStringLiteral("different");
    c.entityTextureMode = QStringLiteral("separate");
    c.pathConfig = QJsonObject{
        { QStringLiteral("assets/minecraft/textures/entity"),
          QJsonObject{{ QStringLiteral("scale"), 6 }} },
        { QStringLiteral("assets/minecraft/textures/gui"),
          QJsonObject{{ QStringLiteral("fast-overlay-size"), 0 }, { QStringLiteral("scale"), 1 }} },
        { QStringLiteral("pack.png"),
          QJsonObject{{ QStringLiteral("alpha"), 1.0 }} },
    };

    Preset p;
    p.name      = QStringLiteral("Default for 32x pack");
    p.builtIn   = true;
    p.storeDirs = false;
    p.config    = c;
    return p;
}

static QList<Preset> builtInPresets()
{
    return { makeDefault32xPreset() };
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static QString sanitizeFilename(const QString &name)
{
    QString result;
    result.reserve(name.size());
    for (const QChar &c : name)
        result += (c.isLetterOrNumber() || c == QLatin1Char('-')) ? c : QLatin1Char('_');
    return result;
}

static int pathConfigRuleCount(const QJsonObject &obj)
{
    return obj.size();
}

// ---------------------------------------------------------------------------
// PresetsDialog
// ---------------------------------------------------------------------------

PresetsDialog::PresetsDialog(const Core::MappingConfig &currentConfig, QWidget *parent)
    : QDialog(parent), m_currentConfig(currentConfig)
{
    setWindowTitle(QStringLiteral("Config Presets"));
    setMinimumSize(620, 380);
    build();
    loadPresets();
    refreshList();
}

void PresetsDialog::build()
{
    auto *outer = new QVBoxLayout(this);
    outer->setSpacing(8);

    auto *hint = new QLabel(
        QStringLiteral("Select a preset to load its settings. The current seed is always preserved. "
                       "Directories are only overridden if the preset includes them."),
        this);
    hint->setWordWrap(true);
    outer->addWidget(hint);

    // Content row: list + preview
    auto *content = new QHBoxLayout();
    content->setSpacing(8);

    m_list = new QListWidget(this);
    m_list->setMinimumWidth(200);
    connect(m_list, &QListWidget::currentRowChanged, this, &PresetsDialog::onSelectionChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &PresetsDialog::onApply);
    content->addWidget(m_list, 2);

    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    content->addWidget(sep);

    m_preview = new QLabel(this);
    m_preview->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_preview->setWordWrap(true);
    m_preview->setTextFormat(Qt::RichText);
    m_preview->setMinimumWidth(240);
    content->addWidget(m_preview, 3);

    outer->addLayout(content, 1);

    // Bottom buttons
    auto *bottom = new QHBoxLayout();

    auto *saveBtn = new QPushButton(QStringLiteral("Save Current as Preset…"), this);
    saveBtn->setToolTip(QStringLiteral("Save the current configuration as a new user preset."));
    connect(saveBtn, &QPushButton::clicked, this, &PresetsDialog::onSaveCurrentAsPreset);
    bottom->addWidget(saveBtn);

    m_deleteBtn = new QPushButton(QStringLiteral("Delete"), this);
    m_deleteBtn->setEnabled(false);
    m_deleteBtn->setToolTip(QStringLiteral("Delete the selected user preset."));
    connect(m_deleteBtn, &QPushButton::clicked, this, &PresetsDialog::onDelete);
    bottom->addWidget(m_deleteBtn);

    bottom->addStretch();

    m_applyBtn = new QPushButton(QStringLiteral("Apply"), this);
    m_applyBtn->setEnabled(false);
    m_applyBtn->setDefault(true);
    m_applyBtn->setToolTip(QStringLiteral("Apply the selected preset and close."));
    connect(m_applyBtn, &QPushButton::clicked, this, &PresetsDialog::onApply);
    bottom->addWidget(m_applyBtn);

    auto *closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    bottom->addWidget(closeBtn);

    outer->addLayout(bottom);
}

void PresetsDialog::loadPresets()
{
    m_presets = builtInPresets();

    const QDir dir(presetsDir());
    for (const QString &file : dir.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        const QString path = dir.filePath(file);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isNull() || !doc.isObject()) continue;

        const QJsonObject obj = doc.object();
        Preset p;
        p.name      = obj.value(QStringLiteral("name")).toString(QFileInfo(file).baseName());
        p.storeDirs = obj.value(QStringLiteral("store_dirs")).toBool(false);
        p.builtIn   = false;
        p.config    = Core::MappingConfig::fromJson(obj);
        m_presets.append(p);
    }
}

void PresetsDialog::refreshList()
{
    const int prevRow = m_list->currentRow();
    m_list->clear();
    for (const Preset &p : m_presets) {
        const QString label = p.builtIn
            ? QStringLiteral("%1  (built-in)").arg(p.name)
            : p.name;
        m_list->addItem(label);
    }
    const int row = (prevRow >= 0 && prevRow < m_list->count()) ? prevRow : 0;
    if (m_list->count() > 0)
        m_list->setCurrentRow(row);
}

void PresetsDialog::onSelectionChanged(int row)
{
    const bool valid = row >= 0 && row < m_presets.size();
    m_applyBtn->setEnabled(valid);
    m_deleteBtn->setEnabled(valid && !m_presets[row].builtIn);
    m_preview->setText(valid ? buildPreview(m_presets[row]) : QString{});
}

void PresetsDialog::onApply()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_presets.size()) return;
    emit presetApplied(mergePreset(m_presets[row]));
    accept();
}

void PresetsDialog::onSaveCurrentAsPreset()
{
    // Get preset name
    bool ok = false;
    const QString name = QInputDialog::getText(
        this,
        QStringLiteral("Save Preset"),
        QStringLiteral("Preset name:"),
        QLineEdit::Normal,
        QStringLiteral("My Preset"),
        &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    // Ask about directories via a small custom dialog
    QDialog dirDlg(this);
    dirDlg.setWindowTitle(QStringLiteral("Save Preset"));
    auto *dl = new QVBoxLayout(&dirDlg);
    auto *cb = new QCheckBox(
        QStringLiteral("Include overlay and texture directories"), &dirDlg);
    cb->setToolTip(QStringLiteral(
        "When checked, the preset will restore the overlay and texture directory paths "
        "in addition to the other settings."));
    dl->addWidget(cb);
    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dirDlg);
    connect(bb, &QDialogButtonBox::accepted, &dirDlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dirDlg, &QDialog::reject);
    dl->addWidget(bb);
    if (dirDlg.exec() != QDialog::Accepted) return;

    // Check for name collision with existing user presets
    for (const Preset &p : m_presets) {
        if (!p.builtIn && p.name == name.trimmed()) {
            const auto btn = QMessageBox::question(
                this,
                QStringLiteral("Overwrite Preset?"),
                QStringLiteral("A preset named “%1” already exists. Overwrite it?").arg(name.trimmed()),
                QMessageBox::Yes | QMessageBox::No);
            if (btn != QMessageBox::Yes) return;
            break;
        }
    }

    Preset p;
    p.name      = name.trimmed();
    p.builtIn   = false;
    p.storeDirs = cb->isChecked();
    p.config    = m_currentConfig;

    // Write to file
    const QString filePath = presetsDir() + QStringLiteral("/") +
                             sanitizeFilename(p.name) + QStringLiteral(".json");
    QJsonObject obj = p.config.toJson();
    obj.insert(QStringLiteral("name"),       p.name);
    obj.insert(QStringLiteral("store_dirs"), p.storeDirs);
    if (!p.storeDirs) {
        obj.remove(QStringLiteral("overlay_dir"));
        obj.remove(QStringLiteral("texture_dir"));
    }

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("Save Failed"),
                             QStringLiteral("Could not write preset file:\n%1").arg(filePath));
        return;
    }
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));

    // Reload and re-select the new preset
    loadPresets();
    refreshList();
    for (int i = 0; i < m_presets.size(); ++i) {
        if (!m_presets[i].builtIn && m_presets[i].name == p.name) {
            m_list->setCurrentRow(i);
            break;
        }
    }
}

void PresetsDialog::onDelete()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_presets.size() || m_presets[row].builtIn) return;

    const Preset &p = m_presets[row];
    const auto btn = QMessageBox::question(
        this,
        QStringLiteral("Delete Preset"),
        QStringLiteral("Delete the preset “%1”?").arg(p.name),
        QMessageBox::Yes | QMessageBox::No);
    if (btn != QMessageBox::Yes) return;

    const QString filePath = presetsDir() + QStringLiteral("/") +
                             sanitizeFilename(p.name) + QStringLiteral(".json");
    QFile::remove(filePath);

    loadPresets();
    refreshList();
}

Core::MappingConfig PresetsDialog::mergePreset(const Preset &preset) const
{
    Core::MappingConfig result = preset.config;
    // Never replace the current seed — it's a user-specific value, not a quality setting
    result.seed = m_currentConfig.seed;
    if (!preset.storeDirs) {
        result.overlayDir = m_currentConfig.overlayDir;
        result.textureDir = m_currentConfig.textureDir;
    }
    return result;
}

QString PresetsDialog::buildPreview(const Preset &preset) const
{
    const Core::MappingConfig &c = preset.config;
    const int rules = pathConfigRuleCount(c.pathConfig);

    auto yesNo = [](bool v) { return v ? QStringLiteral("Yes") : QStringLiteral("No"); };

    QString html = QStringLiteral("<table cellspacing='3'>");
    auto row = [&](const QString &key, const QString &val) {
        html += QStringLiteral("<tr><td><b>%1</b></td><td>&nbsp;&nbsp;%2</td></tr>")
                    .arg(key, val);
    };

    row(QStringLiteral("Alpha:"),              QString::number(c.alpha, 'f', 2));
    row(QStringLiteral("Scale:"),              QStringLiteral("%1×").arg(c.scale));
    row(QStringLiteral("Overlay Scale:"),      QString::number(c.overlayScale, 'f', 2));
    row(QStringLiteral("Keep Aspect:"),        yesNo(c.keepAspect));
    row(QStringLiteral("Per-frame:"),          yesNo(c.perFrame));
    row(QStringLiteral("Fast Overlay Size:"),  c.fastOverlaySize > 0
                                                   ? QStringLiteral("%1 px").arg(c.fastOverlaySize)
                                                   : QStringLiteral("Off"));
    row(QStringLiteral("Entity Face Mode:"),   c.entityFaceMode);
    row(QStringLiteral("Entity Texture Mode:"),c.entityTextureMode);
    row(QStringLiteral("Per-path Rules:"),     rules > 0
                                                   ? QString::number(rules)
                                                   : QStringLiteral("None"));
    row(QStringLiteral("Includes Dirs:"),      yesNo(preset.storeDirs));

    html += QStringLiteral("</table>");
    return html;
}

QString PresetsDialog::presetsDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/presets");
    QDir().mkpath(dir);
    return dir;
}
