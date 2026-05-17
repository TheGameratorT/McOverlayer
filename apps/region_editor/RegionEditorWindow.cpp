#include "RegionEditorWindow.h"
#include "TextureCanvas.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QToolButton>
#include <QListWidget>
#include <QSplitter>
#include <QToolBar>
#include <QAction>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QIcon>
#include <QStyle>

// ---------------------------------------------------------------------------
// JSON helpers — format: { "entity_id": { "texture_size": [w,h], "regions": [...], "textures": [...] } }
// ---------------------------------------------------------------------------

static void writeEntityJson(const QString &path, const Core::EntityData &data)
{
    QJsonArray ts;
    ts.append(data.textureWidth);
    ts.append(data.textureHeight);

    QJsonArray texArr;
    for (const QString &t : data.textures)
        texArr.append(t);

    QJsonArray regArr;
    for (const Core::EntityRegion &r : data.regions) {
        QJsonObject obj;
        obj.insert(QStringLiteral("name"),   r.name);
        obj.insert(QStringLiteral("x"),      r.x);
        obj.insert(QStringLiteral("y"),      r.y);
        obj.insert(QStringLiteral("width"),  r.width);
        obj.insert(QStringLiteral("height"), r.height);
        if (!r.flip.isEmpty())   obj.insert(QStringLiteral("flip"),   r.flip);
        if (!r.rotate.isEmpty()) obj.insert(QStringLiteral("rotate"), r.rotate);
        regArr.append(obj);
    }

    QJsonObject inner;
    inner.insert(QStringLiteral("texture_size"), ts);
    inner.insert(QStringLiteral("textures"), texArr);
    inner.insert(QStringLiteral("regions"),  regArr);

    QJsonObject root;
    root.insert(data.entityId, inner);

    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

static Core::EntityData parseEntityJson(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};

    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.isEmpty())
        return {};

    const QString entityId = root.keys().constFirst();
    const QJsonObject info = root.value(entityId).toObject();

    Core::EntityData d;
    d.entityId = entityId;

    const QJsonArray ts = info.value(QStringLiteral("texture_size")).toArray();
    d.textureWidth  = ts.size() > 0 ? ts[0].toInt(64) : 64;
    d.textureHeight = ts.size() > 1 ? ts[1].toInt(64) : 64;

    for (const QJsonValue &v : info.value(QStringLiteral("textures")).toArray())
        d.textures.append(v.toString());

    for (const QJsonValue &v : info.value(QStringLiteral("regions")).toArray()) {
        const QJsonObject obj = v.toObject();
        Core::EntityRegion r;
        r.name   = obj.value(QStringLiteral("name")).toString();
        r.x      = obj.value(QStringLiteral("x")).toInt();
        r.y      = obj.value(QStringLiteral("y")).toInt();
        r.width  = obj.value(QStringLiteral("width")).toInt();
        r.height = obj.value(QStringLiteral("height")).toInt();
        r.flip   = obj.value(QStringLiteral("flip")).toString();
        r.rotate = obj.value(QStringLiteral("rotate")).toString();
        d.regions.append(r);
    }
    return d;
}

// ---------------------------------------------------------------------------
// RegionEditorWindow
// ---------------------------------------------------------------------------

RegionEditorWindow::RegionEditorWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Region Editor"));
    setMinimumSize(1100, 700);
    build();
}

static QIcon themeIcon(const QString &name)
{
    return QIcon::fromTheme(name);
}

void RegionEditorWindow::build()
{
    // ---- Toolbar ----
    auto *tb = addToolBar(QStringLiteral("Main"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->setIconSize(QSize(16, 16));

    auto *openAct = tb->addAction(
        themeIcon(QStringLiteral("folder-open")),
        QStringLiteral("Open Folder…"));
    openAct->setToolTip(QStringLiteral("Open entity regions folder"));
    connect(openAct, &QAction::triggered, this, &RegionEditorWindow::onBrowseDir);

    tb->addSeparator();

    auto *zoomInAct = tb->addAction(
        themeIcon(QStringLiteral("zoom-in")),
        QStringLiteral("Zoom In"));
    zoomInAct->setToolTip(QStringLiteral("Zoom in (Ctrl+Scroll)"));
    connect(zoomInAct, &QAction::triggered, this, [this]{ m_canvas->zoomIn(); });

    auto *zoomOutAct = tb->addAction(
        themeIcon(QStringLiteral("zoom-out")),
        QStringLiteral("Zoom Out"));
    zoomOutAct->setToolTip(QStringLiteral("Zoom out (Ctrl+Scroll)"));
    connect(zoomOutAct, &QAction::triggered, this, [this]{ m_canvas->zoomOut(); });

    auto *fitAct = tb->addAction(
        themeIcon(QStringLiteral("zoom-fit-best")),
        QStringLiteral("Fit"));
    fitAct->setToolTip(QStringLiteral("Fit image in view"));
    connect(fitAct, &QAction::triggered, this, [this]{ m_canvas->fitView(); });

    tb->addSeparator();

    m_saveAct = tb->addAction(
        themeIcon(QStringLiteral("document-save")),
        QStringLiteral("Save Entity"));
    m_saveAct->setToolTip(QStringLiteral("Save current entity JSON"));
    m_saveAct->setEnabled(false);
    connect(m_saveAct, &QAction::triggered, this, &RegionEditorWindow::onSave);

    // ---- Central splitter: left panel | canvas ----
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    // ---- Left panel ----
    auto *leftPanel = new QWidget(this);
    leftPanel->setMinimumWidth(240);
    leftPanel->setMaximumWidth(340);
    auto *leftLay = new QVBoxLayout(leftPanel);
    leftLay->setContentsMargins(4, 4, 4, 4);
    leftLay->setSpacing(6);

    // Texture Base row (top of panel — needed to resolve relative texture paths)
    auto *texBaseGrp = new QGroupBox(QStringLiteral("Texture Base"), leftPanel);
    auto *texBaseLay = new QVBoxLayout(texBaseGrp);
    texBaseLay->setSpacing(3);

    m_texBaseLabel = new QLabel(QStringLiteral("Not set"), texBaseGrp);
    m_texBaseLabel->setWordWrap(false);
    m_texBaseLabel->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));
    texBaseLay->addWidget(m_texBaseLabel);

    auto *texBaseBtn = new QPushButton(QStringLiteral("Browse…"), texBaseGrp);
    connect(texBaseBtn, &QPushButton::clicked, this, &RegionEditorWindow::onBrowseTexBase);
    texBaseLay->addWidget(texBaseBtn);

    leftLay->addWidget(texBaseGrp);

    // Entity list + texture size spinboxes
    auto *entGrp = new QGroupBox(QStringLiteral("Entities"), leftPanel);
    auto *entLay = new QVBoxLayout(entGrp);
    m_entityList = new QListWidget(entGrp);
    connect(m_entityList, &QListWidget::currentRowChanged,
            this, &RegionEditorWindow::onEntitySelected);
    entLay->addWidget(m_entityList);

    auto *texSizeRow = new QHBoxLayout();
    texSizeRow->addWidget(new QLabel(QStringLiteral("UV size:"), entGrp));
    m_texWSpin = new QSpinBox(entGrp);
    m_texWSpin->setRange(1, 4096);
    m_texWSpin->setValue(64);
    m_texWSpin->setEnabled(false);
    connect(m_texWSpin, &QSpinBox::valueChanged, this, &RegionEditorWindow::onTexWidthChanged);
    texSizeRow->addWidget(m_texWSpin);
    texSizeRow->addWidget(new QLabel(QStringLiteral("×"), entGrp));
    m_texHSpin = new QSpinBox(entGrp);
    m_texHSpin->setRange(1, 4096);
    m_texHSpin->setValue(64);
    m_texHSpin->setEnabled(false);
    connect(m_texHSpin, &QSpinBox::valueChanged, this, &RegionEditorWindow::onTexHeightChanged);
    texSizeRow->addWidget(m_texHSpin);
    entLay->addLayout(texSizeRow);
    leftLay->addWidget(entGrp);

    // Texture list + add/remove
    auto *texGrp = new QGroupBox(QStringLiteral("Textures"), leftPanel);
    auto *texLay = new QVBoxLayout(texGrp);
    m_textureList = new QListWidget(texGrp);
    connect(m_textureList, &QListWidget::currentRowChanged,
            this, &RegionEditorWindow::onTextureSelected);
    texLay->addWidget(m_textureList);

    auto *texBtnsRow = new QHBoxLayout();
    m_addTexBtn = new QPushButton(QStringLiteral("Add…"), texGrp);
    m_addTexBtn->setEnabled(false);
    connect(m_addTexBtn, &QPushButton::clicked, this, &RegionEditorWindow::onAddTexture);
    texBtnsRow->addWidget(m_addTexBtn);
    m_removeTexBtn = new QPushButton(QStringLiteral("Remove"), texGrp);
    m_removeTexBtn->setEnabled(false);
    connect(m_removeTexBtn, &QPushButton::clicked, this, &RegionEditorWindow::onRemoveTexture);
    texBtnsRow->addWidget(m_removeTexBtn);
    texLay->addLayout(texBtnsRow);
    leftLay->addWidget(texGrp);

    // Region list + controls
    auto *regGrp = new QGroupBox(QStringLiteral("Regions"), leftPanel);
    auto *regLay = new QVBoxLayout(regGrp);
    m_regionList = new QListWidget(regGrp);
    connect(m_regionList, &QListWidget::currentRowChanged,
            this, &RegionEditorWindow::onRegionSelected);
    regLay->addWidget(m_regionList);

    auto *nameRow = new QHBoxLayout();
    nameRow->addWidget(new QLabel(QStringLiteral("Name:"), regGrp));
    m_nameEdit = new QLineEdit(regGrp);
    connect(m_nameEdit, &QLineEdit::textEdited, this, &RegionEditorWindow::onNameChanged);
    nameRow->addWidget(m_nameEdit);
    regLay->addLayout(nameRow);

    auto *xfRow = new QHBoxLayout();
    xfRow->addWidget(new QLabel(QStringLiteral("Flip:"), regGrp));
    m_flipCombo = new QComboBox(regGrp);
    m_flipCombo->addItems({QStringLiteral("none"), QStringLiteral("h"),
                           QStringLiteral("v"),    QStringLiteral("hv")});
    connect(m_flipCombo, &QComboBox::currentIndexChanged,
            this, &RegionEditorWindow::onFlipChanged);
    xfRow->addWidget(m_flipCombo);
    xfRow->addWidget(new QLabel(QStringLiteral("Rot:"), regGrp));
    m_rotCombo = new QComboBox(regGrp);
    m_rotCombo->addItems({QStringLiteral("none"), QStringLiteral("cw"), QStringLiteral("ccw")});
    connect(m_rotCombo, &QComboBox::currentIndexChanged,
            this, &RegionEditorWindow::onRotateChanged);
    xfRow->addWidget(m_rotCombo);
    regLay->addLayout(xfRow);

    m_removeBtn = new QPushButton(QStringLiteral("Remove Region"), regGrp);
    m_removeBtn->setEnabled(false);
    connect(m_removeBtn, &QPushButton::clicked, this, &RegionEditorWindow::onRemoveRegion);
    regLay->addWidget(m_removeBtn);

    leftLay->addWidget(regGrp, 1);
    splitter->addWidget(leftPanel);

    // ---- Canvas ----
    m_canvas = new TextureCanvas(this);
    connect(m_canvas, &TextureCanvas::regionAdded,
            this, &RegionEditorWindow::onRegionAdded);
    connect(m_canvas, &TextureCanvas::regionSelected, this, [this](int i) {
        onRegionSelected(i);
        selectRegionInList(i);
    });
    splitter->addWidget(m_canvas);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    m_statusLabel = new QLabel(QStringLiteral("Open a regions folder to begin."), this);
    statusBar()->addWidget(m_statusLabel);
}

// ---------------------------------------------------------------------------

void RegionEditorWindow::onBrowseDir()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Open Entity Regions Folder"), m_dirPath);
    if (!dir.isEmpty())
        loadDir(dir);
}

void RegionEditorWindow::onBrowseTexBase()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Texture Base Folder"), m_texBasePath);
    if (dir.isEmpty()) return;
    m_texBasePath = dir;

    QString display = QDir(dir).dirName();
    m_texBaseLabel->setText(display);
    m_texBaseLabel->setToolTip(dir);
    m_texBaseLabel->setStyleSheet(QString{});

    // Reload the currently displayed texture with the new base
    const int row = m_textureList->currentRow();
    if (row >= 0 && !m_currentEntityId.isEmpty())
        onTextureSelected(row);
}

void RegionEditorWindow::loadDir(const QString &dirPath)
{
    m_dirPath = dirPath;
    m_entities.clear();
    m_entityIds.clear();

    const QStringList files =
        QDir(dirPath).entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);

    for (const QString &fn : files) {
        Core::EntityData d = parseEntityJson(dirPath + QLatin1Char('/') + fn);
        if (d.entityId.isEmpty())
            d.entityId = QFileInfo(fn).baseName();
        m_entities.insert(d.entityId, d);
        m_entityIds.append(d.entityId);
    }

    refreshEntityList();
    m_statusLabel->setText(
        QStringLiteral("Loaded %1 entities from %2").arg(m_entityIds.size()).arg(dirPath));
}

void RegionEditorWindow::refreshEntityList()
{
    m_entityList->clear();
    for (const QString &id : m_entityIds)
        m_entityList->addItem(id);
}

void RegionEditorWindow::onEntitySelected(int row)
{
    if (row < 0 || row >= m_entityIds.size()) return;
    m_currentEntityId    = m_entityIds[row];
    m_currentRegionIndex = -1;

    const Core::EntityData &d = m_entities[m_currentEntityId];

    m_texWSpin->blockSignals(true);
    m_texHSpin->blockSignals(true);
    m_texWSpin->setValue(d.textureWidth);
    m_texHSpin->setValue(d.textureHeight);
    m_texWSpin->blockSignals(false);
    m_texHSpin->blockSignals(false);
    m_texWSpin->setEnabled(true);
    m_texHSpin->setEnabled(true);

    m_addTexBtn->setEnabled(true);
    m_removeTexBtn->setEnabled(true);

    m_canvas->setCanonicalSize(d.textureWidth, d.textureHeight);

    refreshTextureList();
    m_saveAct->setEnabled(true);

    // Auto-preview: select first texture, or clear if none
    if (!d.textures.isEmpty())
        m_textureList->setCurrentRow(0); // triggers onTextureSelected
    else
        m_canvas->setRegions({});
}

void RegionEditorWindow::refreshTextureList()
{
    m_textureList->blockSignals(true);
    m_textureList->clear();
    if (!m_currentEntityId.isEmpty()) {
        for (const QString &t : m_entities[m_currentEntityId].textures)
            m_textureList->addItem(t);
    }
    m_textureList->blockSignals(false);
}

void RegionEditorWindow::onTextureSelected(int row)
{
    if (row < 0 || m_currentEntityId.isEmpty()) return;
    const QStringList &textures = m_entities[m_currentEntityId].textures;
    if (row >= textures.size()) return;

    const Core::EntityData &d = m_entities[m_currentEntityId];
    m_canvas->setCanonicalSize(d.textureWidth, d.textureHeight);

    const QString resolved = resolveTexPath(textures[row]);
    m_canvas->loadTexture(resolved);
    m_canvas->setRegions(d.regions);
    refreshRegionList();

    if (!resolved.isEmpty() && QPixmap(resolved).isNull())
        m_statusLabel->setText(QStringLiteral("Texture not found: %1").arg(textures[row]));
}

void RegionEditorWindow::refreshRegionList()
{
    m_regionList->clear();
    if (m_currentEntityId.isEmpty()) return;
    for (const auto &r : m_entities[m_currentEntityId].regions) {
        m_regionList->addItem(
            QStringLiteral("%1  [%2,%3 %4×%5]")
            .arg(r.name.isEmpty() ? QStringLiteral("(unnamed)") : r.name)
            .arg(r.x).arg(r.y).arg(r.width).arg(r.height));
    }
}

void RegionEditorWindow::onRegionAdded(const Core::EntityRegion &region)
{
    if (m_currentEntityId.isEmpty()) return;
    m_entities[m_currentEntityId].regions.append(region);
    refreshRegionList();
    m_currentRegionIndex = m_entities[m_currentEntityId].regions.size() - 1;
    selectRegionInList(m_currentRegionIndex);
}

void RegionEditorWindow::onRegionSelected(int index)
{
    m_currentRegionIndex = index;
    const bool valid = !m_currentEntityId.isEmpty() &&
                       index >= 0 &&
                       index < m_entities[m_currentEntityId].regions.size();
    m_removeBtn->setEnabled(valid);

    if (!valid) {
        m_nameEdit->clear();
        m_flipCombo->setCurrentIndex(0);
        m_rotCombo->setCurrentIndex(0);
        return;
    }

    const Core::EntityRegion &r = m_entities[m_currentEntityId].regions[index];

    m_nameEdit->blockSignals(true);
    m_nameEdit->setText(r.name);
    m_nameEdit->blockSignals(false);

    const int flipIdx = m_flipCombo->findText(r.flip.isEmpty() ? QStringLiteral("none") : r.flip);
    m_flipCombo->blockSignals(true);
    m_flipCombo->setCurrentIndex(qMax(0, flipIdx));
    m_flipCombo->blockSignals(false);

    const int rotIdx = m_rotCombo->findText(r.rotate.isEmpty() ? QStringLiteral("none") : r.rotate);
    m_rotCombo->blockSignals(true);
    m_rotCombo->setCurrentIndex(qMax(0, rotIdx));
    m_rotCombo->blockSignals(false);

    m_canvas->setSelectedRegion(index);
}

void RegionEditorWindow::selectRegionInList(int index)
{
    m_regionList->blockSignals(true);
    m_regionList->setCurrentRow(index);
    m_regionList->blockSignals(false);
    onRegionSelected(index);
}

void RegionEditorWindow::onRemoveRegion()
{
    if (m_currentEntityId.isEmpty() || m_currentRegionIndex < 0) return;
    m_entities[m_currentEntityId].regions.removeAt(m_currentRegionIndex);
    m_canvas->removeRegion(m_currentRegionIndex);
    m_currentRegionIndex = -1;
    refreshRegionList();
}

void RegionEditorWindow::onFlipChanged(int)
{
    if (m_currentEntityId.isEmpty() || m_currentRegionIndex < 0) return;
    const QString flip = m_flipCombo->currentText();
    m_entities[m_currentEntityId].regions[m_currentRegionIndex].flip =
        (flip == QStringLiteral("none")) ? QString{} : flip;
    m_canvas->updateRegionTransform(m_currentRegionIndex, flip, m_rotCombo->currentText());
}

void RegionEditorWindow::onRotateChanged(int)
{
    if (m_currentEntityId.isEmpty() || m_currentRegionIndex < 0) return;
    const QString rot = m_rotCombo->currentText();
    m_entities[m_currentEntityId].regions[m_currentRegionIndex].rotate =
        (rot == QStringLiteral("none")) ? QString{} : rot;
    m_canvas->updateRegionTransform(m_currentRegionIndex, m_flipCombo->currentText(), rot);
}

void RegionEditorWindow::onNameChanged(const QString &text)
{
    if (m_currentEntityId.isEmpty() || m_currentRegionIndex < 0) return;
    m_entities[m_currentEntityId].regions[m_currentRegionIndex].name = text;
    m_canvas->setSelectedRegion(m_currentRegionIndex); // redraws labels
    refreshRegionList();
    m_regionList->blockSignals(true);
    m_regionList->setCurrentRow(m_currentRegionIndex);
    m_regionList->blockSignals(false);
}

void RegionEditorWindow::onTexWidthChanged(int value)
{
    if (m_currentEntityId.isEmpty()) return;
    m_entities[m_currentEntityId].textureWidth = value;
    m_canvas->setCanonicalSize(value, m_entities[m_currentEntityId].textureHeight);
    m_canvas->setSelectedRegion(m_currentRegionIndex); // redraws with new scale
}

void RegionEditorWindow::onTexHeightChanged(int value)
{
    if (m_currentEntityId.isEmpty()) return;
    m_entities[m_currentEntityId].textureHeight = value;
    m_canvas->setCanonicalSize(m_entities[m_currentEntityId].textureWidth, value);
    m_canvas->setSelectedRegion(m_currentRegionIndex);
}

void RegionEditorWindow::onAddTexture()
{
    if (m_currentEntityId.isEmpty()) return;

    const QString startDir = m_texBasePath.isEmpty() ? m_dirPath : m_texBasePath;
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Add Texture Files"), startDir,
        QStringLiteral("Images (*.png *.jpg *.jpeg)"));

    if (paths.isEmpty()) return;

    Core::EntityData &d = m_entities[m_currentEntityId];
    for (const QString &absPath : paths) {
        QString stored = absPath;
        if (!m_texBasePath.isEmpty() && absPath.startsWith(m_texBasePath))
            stored = QDir(m_texBasePath).relativeFilePath(absPath);
        if (!d.textures.contains(stored))
            d.textures.append(stored);
    }
    refreshTextureList();
}

void RegionEditorWindow::onRemoveTexture()
{
    if (m_currentEntityId.isEmpty()) return;
    const int row = m_textureList->currentRow();
    if (row < 0) return;
    m_entities[m_currentEntityId].textures.removeAt(row);
    refreshTextureList();
    m_canvas->setRegions({});
}

void RegionEditorWindow::onSave()
{
    if (!saveEntityJson(m_currentEntityId))
        QMessageBox::warning(this, QStringLiteral("Save Failed"),
                             QStringLiteral("Could not write entity JSON."));
    else
        m_statusLabel->setText(QStringLiteral("Saved: %1").arg(m_currentEntityId));
}

bool RegionEditorWindow::saveEntityJson(const QString &entityId)
{
    if (entityId.isEmpty() || m_dirPath.isEmpty()) return false;
    writeEntityJson(m_dirPath + QLatin1Char('/') + entityId + QStringLiteral(".json"),
                    m_entities[entityId]);
    return true;
}

QString RegionEditorWindow::resolveTexPath(const QString &relPath) const
{
    if (relPath.isEmpty()) return {};
    if (QFileInfo(relPath).isAbsolute()) return relPath;
    if (!m_texBasePath.isEmpty()) {
        const QString candidate = m_texBasePath + QLatin1Char('/') + relPath;
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return relPath;
}
