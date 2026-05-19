#include "MainWindow.h"
#include <stdexcept>
#include "ConfigPanel.h"
#include "AssignmentCard.h"
#include "ThumbnailLoader.h"
#include "FlowLayout.h"
#include "ApplyDialog.h"
#include "OverlayLookupDialog.h"
#include "SeedSearchDialog.h"
#include "core/MappingConfig.h"
#include "core/Hash.h"

#include <QToolBar>
#include <QScrollArea>
#include <QSplitter>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QStatusBar>
#include <QApplication>
#include <QClipboard>
#include <QShortcut>
#include <QKeySequence>
#include <QRandomGenerator>
#include <QTimer>
#include <QWidget>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QMenuBar>

static const QStringList kPriorityPatterns = {
    QStringLiteral("grass_block_top"),
    QStringLiteral("stone"),
    QStringLiteral("dirt"),
    QStringLiteral("sand"),
    QStringLiteral("gravel"),
    QStringLiteral("oak_planks"),
    QStringLiteral("cobblestone"),
    QStringLiteral("water_still"),
    QStringLiteral("lava_still"),
    QStringLiteral("pack.png"),
    QStringLiteral("pack.mcmeta"),
};

static int priorityScore(const QString &path)
{
    const QString lower = path.toLower();
    for (int i = 0; i < kPriorityPatterns.size(); ++i)
        if (lower.contains(kPriorityPatterns[i]))
            return i;
    return kPriorityPatterns.size();
}

// ---------------------------------------------------------------------------

MainWindow::~MainWindow()
{
    if (m_loader) {
        m_loader->cancel();
        m_loader->wait();
    }
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("MC Overlayer"));
    setMinimumSize(900, 600);

    try {
        m_config = Core::MappingConfig::fromLastRun(Core::MappingConfig::lastRunPath());
    } catch (...) {
        m_config.entityRegionsDir = Core::MappingConfig::defaultEntityRegionsDir();
    }

    build();
    rebuild();
}

void MainWindow::build()
{
    // ---- Menu bar ----
    auto *toolsMenu = menuBar()->addMenu(QStringLiteral("&Tools"));

    auto *lookupAction = toolsMenu->addAction(QStringLiteral("&Overlay Lookup"));
    lookupAction->setToolTip(QStringLiteral("Search which overlay is assigned to a specific texture path."));
    connect(lookupAction, &QAction::triggered, this, &MainWindow::onOverlayLookup);

    auto *seedSearchAction = toolsMenu->addAction(QStringLiteral("&Seed Search"));
    seedSearchAction->setToolTip(QStringLiteral("Scan a range of seeds to find one that assigns a desired overlay to a specific texture."));
    connect(seedSearchAction, &QAction::triggered, this, &MainWindow::onSeedSearch);

    auto *helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));

    auto *aboutAction = helpMenu->addAction(QStringLiteral("&About MC Overlayer"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onAbout);

    auto *aboutQtAction = helpMenu->addAction(QStringLiteral("About &Qt"));
    connect(aboutQtAction, &QAction::triggered, qApp, &QApplication::aboutQt);

    // ---- Toolbar ----
    auto *tb = addToolBar(QStringLiteral("Main"));
    tb->setMovable(false);
    tb->setFloatable(false);

    auto *seedLbl = new QLabel(QStringLiteral(" Seed: "), this);
    seedLbl->setToolTip(QStringLiteral("The seed determines which overlay is assigned to which texture. "
                                       "The same seed always produces the same mapping. "
                                       "Type a number or any text and press Enter."));
    tb->addWidget(seedLbl);
    m_seedEdit = new QLineEdit(QString::number(m_config.seed), this);
    m_seedEdit->setFixedWidth(120);
    m_seedEdit->setPlaceholderText(QStringLiteral("seed or text"));
    m_seedEdit->setToolTip(seedLbl->toolTip());
    connect(m_seedEdit, &QLineEdit::returnPressed, this, [this]{
        const QString t = m_seedEdit->text().trimmed();
        bool ok;
        qint64 s = t.toLongLong(&ok);
        if (!ok) s = Core::stringToSeed(t);
        m_config.seed = s;
        m_seedEdit->setText(QString::number(s));
        rebuild();
    });
    tb->addWidget(m_seedEdit);

    auto *randBtn = new QPushButton(QStringLiteral("⟳"), this);
    randBtn->setToolTip(QStringLiteral("Randomize seed  [R / Space]"));
    randBtn->setFixedWidth(28);
    connect(randBtn, &QPushButton::clicked, this, &MainWindow::onRandomize);
    tb->addWidget(randBtn);

    auto *copyBtn = new QPushButton(QStringLiteral("⎘"), this);
    copyBtn->setToolTip(QStringLiteral("Copy seed to clipboard"));
    copyBtn->setFixedWidth(28);
    connect(copyBtn, &QPushButton::clicked, this, &MainWindow::onCopySeed);
    tb->addWidget(copyBtn);

    tb->addSeparator();

    auto *filterLbl = new QLabel(QStringLiteral(" Filter: "), this);
    filterLbl->setToolTip(QStringLiteral("Filter the preview grid by texture or overlay filename."));
    tb->addWidget(filterLbl);
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setFixedWidth(160);
    m_filterEdit->setPlaceholderText(QStringLiteral("texture or overlay name"));
    m_filterEdit->setToolTip(filterLbl->toolTip());
    m_filterEdit->setClearButtonEnabled(true);
    m_filterTimer = new QTimer(this);
    m_filterTimer->setSingleShot(true);
    m_filterTimer->setInterval(300);
    connect(m_filterTimer, &QTimer::timeout, this, &MainWindow::onFilterChanged);
    connect(m_filterEdit, &QLineEdit::textChanged, this, [this]{ m_filterTimer->start(); });
    tb->addWidget(m_filterEdit);

    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItems({QStringLiteral("All"), QStringLiteral("Entity"), QStringLiteral("Regular")});
    m_typeCombo->setToolTip(QStringLiteral("Show all assignments, only entity skin assignments, or only regular texture assignments."));
    connect(m_typeCombo, &QComboBox::currentIndexChanged, this, [this](int){ onFilterChanged(); });
    tb->addWidget(m_typeCombo);

    auto *maxLbl = new QLabel(QStringLiteral(" Max: "), this);
    maxLbl->setToolTip(QStringLiteral("Maximum number of cards to display in the preview grid. Lower values are faster to render."));
    tb->addWidget(maxLbl);
    m_maxSpin = new QSpinBox(this);
    m_maxSpin->setRange(10, 5000);
    m_maxSpin->setValue(200);
    m_maxSpin->setSingleStep(50);
    m_maxSpin->setFixedWidth(72);
    m_maxSpin->setToolTip(maxLbl->toolTip());
    connect(m_maxSpin, &QSpinBox::valueChanged, this, [this](int){ onFilterChanged(); });
    tb->addWidget(m_maxSpin);

    tb->addSeparator();

    auto *applyBtn = new QPushButton(QStringLiteral("▶ Apply"), this);
    applyBtn->setToolTip(QStringLiteral("Composite the overlays onto the textures and write the output files to disk."));
    connect(applyBtn, &QPushButton::clicked, this, &MainWindow::onApply);
    tb->addWidget(applyBtn);

    // ---- Central splitter ----
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    m_configPanel = new ConfigPanel(m_config, this);
    m_configPanel->setMinimumWidth(220);
    m_configPanel->setMaximumWidth(360);
    connect(m_configPanel, &ConfigPanel::configChanged, this, &MainWindow::onConfigChanged);
    splitter->addWidget(m_configPanel);

    m_cardsContainer = new QWidget(this);
    auto *flowLayout = new FlowLayout(m_cardsContainer, 8, 8);
    m_cardsContainer->setLayout(flowLayout);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setWidget(m_cardsContainer);
    splitter->addWidget(m_scrollArea);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    setCentralWidget(splitter);

    // ---- Status bar ----
    m_statusLabel = new QLabel(this);
    statusBar()->addWidget(m_statusLabel);

    // ---- Shortcuts ----
    auto *shortcutR = new QShortcut(QKeySequence(Qt::Key_R), this);
    connect(shortcutR, &QShortcut::activated, this, &MainWindow::onRandomize);

    auto *shortcutSpace = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(shortcutSpace, &QShortcut::activated, this, &MainWindow::onRandomize);
}

void MainWindow::rebuild()
{
    // Keep the ConfigPanel's internal copy in sync so "Use These Settings" never
    // emits a stale seed back to us.
    if (m_configPanel) m_configPanel->setSeed(m_config.seed);

    if (m_loader) {
        m_loader->cancel();
        m_loader->wait();
        delete m_loader;
        m_loader = nullptr;
    }

    // Delete existing cards
    qDeleteAll(m_cards);
    m_cards.clear();
    m_visibleIndices.clear();

    if (m_config.overlayDir.isEmpty() || m_config.textureDir.isEmpty()) {
        updateStatus();
        if (m_statusLabel)
            m_statusLabel->setText(QStringLiteral(
                "Getting started: set Overlay Images and Texture Images directories in the left panel, "
                "then click “Use These Settings” to see the assignment grid. "
                "Use “▶ Apply” in the toolbar to write the output."));
        return;
    }

    try {
        m_buildResult = Core::buildAssignments(m_config);
    } catch (const std::exception &e) {
        if (m_statusLabel)
            m_statusLabel->setText(QStringLiteral("Error: ") + QString::fromStdString(e.what()));
        return;
    }

    if (m_seedSearchDlg)
        m_seedSearchDlg->updatePaths(m_buildResult.targetPaths, m_buildResult.overlayPaths);

    applyFilter();  // creates and starts the ThumbnailLoader
    updateStatus();
    saveConfig();
}

void MainWindow::applyFilter()
{
    // Stop and restart loader if we already have one
    if (m_loader) {
        m_loader->cancel();
        m_loader->wait();
        delete m_loader;
        m_loader = nullptr;
    }

    qDeleteAll(m_cards);
    m_cards.clear();
    m_visibleIndices.clear();

    const QString filterText = m_filterEdit ? m_filterEdit->text().trimmed().toLower() : QString{};
    const int typeIdx = m_typeCombo ? m_typeCombo->currentIndex() : 0;
    const int maxCount = m_maxSpin ? m_maxSpin->value() : 200;

    const auto &assignments = m_buildResult.assignments;

    // Collect matching indices with priority score
    QList<QPair<int,int>> scored; // (score, index)
    for (int i = 0; i < assignments.size(); ++i) {
        const auto &a = assignments[i];

        // Type filter
        if (typeIdx == 1 && !a.isEntity) continue;
        if (typeIdx == 2 &&  a.isEntity) continue;

        // Text filter
        if (!filterText.isEmpty()) {
            const bool matchTarget  = a.targetPath.toLower().contains(filterText);
            const bool matchOverlay = a.overlayPath.toLower().contains(filterText);
            bool matchFace = false;
            for (const QString &fp : a.faceOverlayPaths)
                if (fp.toLower().contains(filterText)) { matchFace = true; break; }
            if (!matchTarget && !matchOverlay && !matchFace) continue;
        }

        scored.append({priorityScore(a.targetPath), i});
    }

    // Sort by priority then by original index
    std::stable_sort(scored.begin(), scored.end(), [](const auto &a, const auto &b){
        return a.first != b.first ? a.first < b.first : a.second < b.second;
    });

    if (scored.size() > maxCount)
        scored.resize(maxCount);

    // Build cards and record visible indices
    auto *flowLayout = qobject_cast<FlowLayout *>(m_cardsContainer->layout());
    for (const auto &[score, idx] : scored) {
        m_visibleIndices.append(idx);
        auto *card = new AssignmentCard(assignments[idx], m_cardsContainer);
        m_cards.append(card);
        if (flowLayout) flowLayout->addWidget(card);
    }

    updateStatus();

    if (m_buildResult.assignments.isEmpty()) return;

    m_loader = new ThumbnailLoader(m_buildResult.assignments, this,
                                   m_config.alpha, m_config.keepAspect, m_config.overlayScale);
    m_loader->setIndices(m_visibleIndices);
    connect(m_loader, &ThumbnailLoader::loaded, this, &MainWindow::onThumbnailLoaded);
    m_loader->start();
}

void MainWindow::onThumbnailLoaded(int index, const QImage &image)
{
    // Find which card corresponds to this assignment index
    const int pos = m_visibleIndices.indexOf(index);
    if (pos >= 0 && pos < m_cards.size())
        m_cards[pos]->setCompositeImage(image);
}

void MainWindow::onRandomize()
{
    const qint64 seed = static_cast<qint64>(
        QRandomGenerator::global()->bounded(static_cast<quint32>(0xFFFFFFFF)));
    m_config.seed = seed;
    if (m_seedEdit) m_seedEdit->setText(QString::number(seed));
    rebuild();
}

void MainWindow::onCopySeed()
{
    QApplication::clipboard()->setText(
        m_seedEdit ? m_seedEdit->text() : QString::number(m_config.seed));
}

void MainWindow::onFilterChanged()
{
    applyFilter();
}

void MainWindow::onConfigChanged(const Core::MappingConfig &config)
{
    m_config = config;
    if (m_seedEdit) m_seedEdit->setText(QString::number(m_config.seed));
    rebuild();
}

void MainWindow::onOverlayLookup()
{
    auto *dlg = new OverlayLookupDialog(m_buildResult.assignments, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::onSeedSearch()
{
    if (!m_seedSearchDlg) {
        m_seedSearchDlg = new SeedSearchDialog(
            m_config,
            m_buildResult.targetPaths,
            m_buildResult.overlayPaths,
            this);
        connect(m_seedSearchDlg, &SeedSearchDialog::seedSelected, this, [this](qint64 seed){
            m_config.seed = seed;
            if (m_seedEdit) m_seedEdit->setText(QString::number(seed));
            rebuild();
        });
    }
    m_seedSearchDlg->show();
    m_seedSearchDlg->raise();
    m_seedSearchDlg->activateWindow();
}

void MainWindow::onAbout()
{
    const QString version = QApplication::applicationVersion();
    
    QString bodyText = QString("<p><strong>MC Overlayer</strong></p>"
        "<p>Version: %1</p>"
        "<p>Copyright &copy; 2026 TheGameratorT</p>"
        "<p><span style=\"text-decoration: underline;\">License:</span></p>"
        "<p style=\"padding-left: 30px;\">This application is licensed under the GNU General Public License v3.</p>"
        "<p style=\"padding-left: 30px;\">This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.</p>"
        "<p style=\"padding-left: 30px;\">This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.<br />See the GNU General Public License for more details.</p>"
        "<p style=\"padding-left: 30px;\">For details read the LICENSE file bundled with the program or visit:</p>"
        "<p style=\"padding-left: 30px;\"><a href=\"https://www.gnu.org/licenses/\">https://www.gnu.org/licenses/</a></p>").arg(version);
    
    QMessageBox::about(this, "About MC Overlayer", bodyText);
}

void MainWindow::onApply()
{
    if (m_configPanel && m_configPanel->isDirty()) {
        QMessageBox msg(this);
        msg.setIcon(QMessageBox::Warning);
        msg.setWindowTitle(QStringLiteral("Unapplied Settings"));
        msg.setText(QStringLiteral("You have changed settings that haven't been applied yet."));
        msg.setInformativeText(QStringLiteral(
            "The output may not reflect your current inputs. "
            "Click “Use These Settings” to update the preview first, "
            "or “Apply Anyway” to proceed with the previously applied settings."));
        auto *useBtn    = msg.addButton(QStringLiteral("Use These Settings"), QMessageBox::AcceptRole);
        auto *anywayBtn = msg.addButton(QStringLiteral("Apply Anyway"),       QMessageBox::DestructiveRole);
        msg.addButton(QMessageBox::Cancel);
        msg.setDefaultButton(useBtn);
        msg.exec();

        const QAbstractButton *clicked = msg.clickedButton();
        if (clicked == msg.button(QMessageBox::Cancel)) return;
        if (clicked == useBtn) m_configPanel->applySettings();
        Q_UNUSED(anywayBtn);
    }

    auto *dlg = new ApplyDialog(m_config, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::saveConfig()
{
    m_config.saveLastRun(Core::MappingConfig::lastRunPath());
}

void MainWindow::updateStatus()
{
    if (!m_statusLabel) return;
    const int total   = m_buildResult.assignments.size();
    const int shown   = m_cards.size();
    const int overlays = m_buildResult.overlayPaths.size();
    const int targets  = m_buildResult.targetPaths.size();
    m_statusLabel->setText(
        QStringLiteral("Seed: ") + QString::number(m_config.seed) +
        QStringLiteral("  |  Textures: ") + QString::number(targets) +
        QStringLiteral("  |  Overlays: ") + QString::number(overlays) +
        QStringLiteral("  |  Showing: ") + QString::number(shown) +
        QStringLiteral(" / ") + QString::number(total));
}
