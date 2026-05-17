#include "ApplyDialog.h"
#include "core/FileUtils.h"
#include "core/ImageCache.h"
#include "core/ImageProcessor.h"
#include "core/OverlayMapper.h"
#include "core/EntityMapper.h"
#include "core/EntityLoader.h"
#include "core/PathConfig.h"
#include "core/Hash.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QProgressBar>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>

// ---------------------------------------------------------------------------
// ApplyWorker
// ---------------------------------------------------------------------------

ApplyWorker::ApplyWorker(const Core::MappingConfig &config, QObject *parent)
    : QThread(parent), m_config(config)
{}

void ApplyWorker::requestStop() { m_stop.storeRelaxed(1); }

void ApplyWorker::run()
{
    const Core::MappingConfig &cfg = m_config;
    const bool referenceMode = !cfg.outputDir.isEmpty();

    if (referenceMode) {
        QDir dst(cfg.outputDir);
        if (dst.exists()) {
            if (!dst.removeRecursively()) {
                emit logMessage(QStringLiteral("[Error] Could not remove existing output dir"));
                emit finished(false);
                return;
            }
        }
        if (!QDir().mkpath(cfg.outputDir)) {
            emit logMessage(QStringLiteral("[Error] Could not create output dir"));
            emit finished(false);
            return;
        }
    }

    const QString effectiveTarget = cfg.textureDir;

    // Scan images
    const QStringList overlays = Core::getImagesInDir(cfg.overlayDir);
    const QStringList targets  = Core::getImagesInDir(effectiveTarget);
    if (overlays.isEmpty() || targets.isEmpty()) {
        emit logMessage(QStringLiteral("[Error] No images found in source directories"));
        emit finished(false);
        return;
    }

    Core::ImageCache cache;

    if (m_stop.loadRelaxed()) { emit finished(false); return; }

    // Load entity regions
    Core::EntityRegions er;
    if (!cfg.entityRegionsDir.isEmpty()) {
        try {
            er = Core::loadEntityRegions(cfg.entityRegionsDir);
        } catch (const std::exception &e) {
            emit logMessage(QStringLiteral("[Error] ") + QString::fromStdString(e.what()));
            emit finished(false);
            return;
        }
    }

    // Categorise targets
    struct EntityTarget { QString path, entityId, canonicalTex; };
    QList<EntityTarget> entityTargets;
    QStringList regularTargets;

    QList<QPair<QString,QString>> normTexPaths;
    for (auto it = er.textureToEntity.cbegin(); it != er.textureToEntity.cend(); ++it)
        normTexPaths.append({QString(it.key()).replace(QLatin1Char('\\'), QLatin1Char('/')), it.key()});

    for (const QString &t : targets) {
        const QString normT = QString(t).replace(QLatin1Char('\\'), QLatin1Char('/'));
        bool matched = false;
        for (const auto &[normTex, canonTex] : normTexPaths) {
            if (normT.endsWith(normTex)) {
                entityTargets.append({t, er.textureToEntity.value(canonTex), canonTex});
                matched = true;
                break;
            }
        }
        if (!matched)
            regularTargets.append(t);
    }

    // Parse path config
    Core::PathConfigMap pathConfig;
    try {
        pathConfig = Core::parsePathConfig(cfg.pathConfig);
    } catch (...) {}

    // Resolve per-target fast-overlay-size from dir config.
    // Entities fall back to the global fastEntityOverlay setting; regular textures default to 0.
    auto resolveFastSize = [&](const QString &path) -> int {
        const QString relT = Core::relPath(effectiveTarget, path);
        const QVariantMap ov = Core::getPathOverrides(relT, pathConfig);
        if (ov.contains(QStringLiteral("fast-overlay-size")))
            return ov.value(QStringLiteral("fast-overlay-size")).toInt();
        return cfg.fastOverlaySize;
    };

    // Collect unique non-zero sizes to preload; note whether any target skips fast mode.
    QSet<int> fastSizes;
    bool anyNonFast = false;
    for (const EntityTarget &et : entityTargets) {
        const int sz = resolveFastSize(et.path);
        if (sz > 0) fastSizes.insert(sz); else anyNonFast = true;
    }
    for (const QString &t : regularTargets) {
        const int sz = resolveFastSize(t);
        if (sz > 0) fastSizes.insert(sz); else anyNonFast = true;
    }

    // Preload: one preloadFixed call per unique size (shared across entity + regular).
    for (int sz : fastSizes) {
        emit logMessage(QStringLiteral("Preloading ") + QString::number(overlays.size())
            + QStringLiteral(" overlays at ") + QString::number(sz) + QStringLiteral("px..."));
        cache.preloadFixed(overlays, sz, cfg.keepAspect,
                           [this](int done, int total){ emit progressChanged(done, total); });
        if (m_stop.loadRelaxed()) { emit finished(false); return; }
    }
    if (anyNonFast) {
        emit logMessage(QStringLiteral("Warming up overlay cache (") + QString::number(overlays.size())
            + QStringLiteral(" overlays, loading on demand)..."));
        cache.preloadOverlays(overlays, [this](int done, int total){ emit progressChanged(done, total); });
    }
    if (m_stop.loadRelaxed()) { emit finished(false); return; }

    // Build mappers
    QScopedPointer<Core::OverlayMapper> mapper;
    QScopedPointer<Core::EntityMapper>  entityMapper;
    if (!regularTargets.isEmpty())
        mapper.reset(new Core::OverlayMapper(regularTargets, overlays, cfg.seed,
                                              cfg.perFrame, effectiveTarget, cfg.overlayDir));
    if (!entityTargets.isEmpty())
        entityMapper.reset(new Core::EntityMapper(er.entities, overlays,
                                                   cfg.entityFaceMode, cfg.entityTextureMode,
                                                   cfg.seed, cfg.overlayDir));

    const int total = targets.size();
    QAtomicInt doneCount{0};
    QAtomicInt shutdownFlag{0};

    emit logMessage(QStringLiteral("Processing ") + QString::number(total) + QStringLiteral(" images..."));
    emit progressChanged(0, total);

    QList<QFuture<QPair<QString,QString>>> futures;
    QThreadPool pool;
    pool.setMaxThreadCount(qMax(1, qMin(8, QThread::idealThreadCount())));

    auto outputPathFor = [&](const QString &srcPath) -> QString {
        if (!referenceMode) return {};
        return cfg.outputDir + QLatin1Char('/') + Core::relPath(effectiveTarget, srcPath);
    };

    for (const EntityTarget &et : entityTargets) {
        const QString relT = Core::relPath(effectiveTarget, et.path);
        const QVariantMap ov = Core::getPathOverrides(relT, pathConfig);
        const int fastSize = resolveFastSize(et.path);
        const Core::EntityData &data = er.entities.value(et.entityId);
        QList<Core::EntityRegion> regions;
        QStringList faceOvPaths;
        try { entityMapper->getFaceOverlays(et.canonicalTex, regions, faceOvPaths); }
        catch (...) { continue; }

        const QString outPath = outputPathFor(et.path);
        futures.append(QtConcurrent::run(&pool, [=, &cache, &shutdownFlag, &doneCount, this]() {
            auto r = Core::processEntityImage(et.path, outPath, regions, faceOvPaths,
                data.textureWidth, data.textureHeight, cache,
                ov.value(QStringLiteral("scale"),         cfg.scale).toInt(),
                ov.value(QStringLiteral("alpha"),         cfg.alpha).toDouble(),
                &shutdownFlag,
                ov.value(QStringLiteral("keep-aspect"),   cfg.keepAspect).toBool(),
                ov.value(QStringLiteral("overlay-scale"), cfg.overlayScale).toDouble(),
                fastSize);
            int n = doneCount.fetchAndAddRelaxed(1) + 1;
            emit progressChanged(n, total);
            emit logMessage(QStringLiteral("  Overlaid ") + QFileInfo(r.first).fileName());
            return r;
        }));
    }

    if (mapper) {
        for (const QString &t : regularTargets) {
            if (m_stop.loadRelaxed()) { shutdownFlag.storeRelaxed(1); break; }
            const QString relT = Core::relPath(effectiveTarget, t);
            const QVariantMap ov = Core::getPathOverrides(relT, pathConfig);
            const int fastSize = resolveFastSize(t);
            const QString outPath = outputPathFor(t);
            futures.append(QtConcurrent::run(&pool, [=, &cache, &mapper, &shutdownFlag, &doneCount, this]() {
                auto r = Core::processImage(t, outPath, *mapper, cache,
                    ov.value(QStringLiteral("scale"),         cfg.scale).toInt(),
                    ov.value(QStringLiteral("alpha"),         cfg.alpha).toDouble(),
                    &shutdownFlag,
                    ov.value(QStringLiteral("keep-aspect"),   cfg.keepAspect).toBool(),
                    ov.value(QStringLiteral("overlay-scale"), cfg.overlayScale).toDouble(),
                    fastSize);
                int n = doneCount.fetchAndAddRelaxed(1) + 1;
                emit progressChanged(n, total);
                emit logMessage(QStringLiteral("  Overlaid ") + QFileInfo(r.first).fileName());
                return r;
            }));
        }
    }

    for (auto &f : futures) {
        if (m_stop.loadRelaxed()) shutdownFlag.storeRelaxed(1);
        f.waitForFinished();
    }

    if (!m_stop.loadRelaxed() && referenceMode) {
        // Copy non-image files from the texture dir to the output dir
        const QSet<QString> imageExts = {
            QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg")
        };
        QDirIterator it(cfg.textureDir, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (m_stop.loadRelaxed()) break;
            const QString srcPath = it.next();
            QFileInfo fi(srcPath);
            if (!fi.isFile() || imageExts.contains(fi.suffix().toLower()))
                continue;
            const QString rel     = Core::relPath(cfg.textureDir, srcPath);
            const QString dstPath = cfg.outputDir + QLatin1Char('/') + rel;
            QDir().mkpath(QFileInfo(dstPath).absolutePath());
            QFile::copy(srcPath, dstPath);
        }
    }

    if (!m_stop.loadRelaxed()) {
        const QString metaDir = referenceMode ? cfg.outputDir : effectiveTarget;
        QFile meta(metaDir + QStringLiteral("/overlay_meta.json"));
        if (meta.open(QIODevice::WriteOnly | QIODevice::Text))
            meta.write(QJsonDocument(cfg.toJson()).toJson());
    }

    emit finished(!m_stop.loadRelaxed());
}

// ---------------------------------------------------------------------------
// ApplyDialog
// ---------------------------------------------------------------------------

ApplyDialog::ApplyDialog(const Core::MappingConfig &config, QWidget *parent)
    : QDialog(parent), m_config(config)
{
    setWindowTitle(QStringLiteral("Apply Mapping"));
    setMinimumSize(720, 520);
    build();
}

void ApplyDialog::build()
{
    auto *layout = new QVBoxLayout(this);

    // Output directory (reference mode)
    auto *outGrp = new QGroupBox(QStringLiteral("Output Directory"), this);
    auto *outLay = new QVBoxLayout(outGrp);
    outLay->addWidget(new QLabel(
        QStringLiteral("Leave empty to modify textures in-place.\n"
                        "When set, overlaid images are written here and remaining files are copied."),
        this));

    auto *texRow = new QHBoxLayout();
    texRow->addWidget(new QLabel(QStringLiteral("Source:"), this));
    auto *texEdit = new QLineEdit(m_config.textureDir, this);
    texEdit->setReadOnly(true);
    texRow->addWidget(texEdit);
    outLay->addLayout(texRow);

    auto *outRow = new QHBoxLayout();
    outRow->addWidget(new QLabel(QStringLiteral("Output:"), this));
    m_outEdit = new QLineEdit(m_config.outputDir, this);
    m_outEdit->setPlaceholderText(QStringLiteral("e.g., output/"));
    outRow->addWidget(m_outEdit);
    m_outBtn = new QPushButton(QStringLiteral("…"), this);
    m_outBtn->setMaximumWidth(26);
    connect(m_outBtn, &QPushButton::clicked, this, &ApplyDialog::browseOutputDir);
    outRow->addWidget(m_outBtn);
    auto *clearBtn = new QPushButton(QStringLiteral("✕"), this);
    clearBtn->setMaximumWidth(26);
    clearBtn->setToolTip(QStringLiteral("Clear — revert to in-place mode"));
    connect(clearBtn, &QPushButton::clicked, this, [this]{ m_outEdit->clear(); });
    outRow->addWidget(clearBtn);
    outLay->addLayout(outRow);

    layout->addWidget(outGrp);

    auto *progressRow = new QHBoxLayout();
    m_progress = new QProgressBar(this);
    m_progress->setVisible(false);
    progressRow->addWidget(m_progress, 1);
    m_elapsed = new QLabel(this);
    m_elapsed->setVisible(false);
    m_elapsed->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_elapsed->setMinimumWidth(70);
    progressRow->addWidget(m_elapsed);
    layout->addLayout(progressRow);

    layout->addWidget(new QLabel(QStringLiteral("Output:"), this));
    m_log = new QTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setFont(QFont(QStringLiteral("monospace"), 9));
    layout->addWidget(m_log, 1);

    auto *btns = new QDialogButtonBox(this);
    m_runBtn  = btns->addButton(QStringLiteral("▶ Run"),  QDialogButtonBox::AcceptRole);
    m_stopBtn = btns->addButton(QStringLiteral("■ Stop"), QDialogButtonBox::DestructiveRole);
    auto *closeBtn = btns->addButton(QStringLiteral("Close"), QDialogButtonBox::RejectRole);
    m_stopBtn->setEnabled(false);
    connect(m_runBtn,  &QPushButton::clicked, this, &ApplyDialog::onRun);
    connect(m_stopBtn, &QPushButton::clicked, this, &ApplyDialog::onStop);
    connect(closeBtn,  &QPushButton::clicked, this, &QDialog::reject);
    layout->addWidget(btns);
}

void ApplyDialog::browseOutputDir()
{
    const QString p = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Output Folder"), m_outEdit->text());
    if (!p.isEmpty()) m_outEdit->setText(p);
}

void ApplyDialog::onRun()
{
    m_log->clear();
    m_runBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);

    Core::MappingConfig cfg = m_config;
    cfg.outputDir = m_outEdit->text().trimmed();

    m_progress->setValue(0);
    m_progress->setVisible(true);
    m_elapsed->setText(QStringLiteral("0:00"));
    m_elapsed->setVisible(true);
    m_elapsedTimer.start();
    connect(&m_tickTimer, &QTimer::timeout, this, [this]() {
        const qint64 secs = m_elapsedTimer.elapsed() / 1000;
        m_elapsed->setText(QStringLiteral("%1:%2")
            .arg(secs / 60)
            .arg(secs % 60, 2, 10, QLatin1Char('0')));
    });
    m_tickTimer.start(1000);

    m_worker = new ApplyWorker(cfg, this);
    connect(m_worker, &ApplyWorker::logMessage,      this, [this](const QString &msg){ m_log->append(msg); });
    connect(m_worker, &ApplyWorker::progressChanged, this, &ApplyDialog::onProgress);
    connect(m_worker, &ApplyWorker::finished,        this, &ApplyDialog::onFinished);
    m_worker->start();
}

void ApplyDialog::onStop()
{
    if (m_worker && m_worker->isRunning())
        m_worker->requestStop();
}

void ApplyDialog::onProgress(int done, int total)
{
    m_progress->setMaximum(total);
    m_progress->setValue(done);
    m_progress->setFormat(QStringLiteral("%1/%2 (%p%)").arg(done).arg(total));
}

void ApplyDialog::onFinished(bool success)
{
    m_tickTimer.stop();
    m_tickTimer.disconnect();
    m_runBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);
    m_progress->setVisible(false);
    const qint64 secs = m_elapsedTimer.elapsed() / 1000;
    const QString timeStr = QStringLiteral("%1:%2")
        .arg(secs / 60)
        .arg(secs % 60, 2, 10, QLatin1Char('0'));
    m_elapsed->setText(timeStr);
    m_log->append(success
        ? QStringLiteral("\n[Done — %1]").arg(timeStr)
        : QStringLiteral("\n[Stopped or failed — %1]").arg(timeStr));
}

void ApplyDialog::closeEvent(QCloseEvent *event)
{
    if (m_worker && m_worker->isRunning()) {
        m_worker->requestStop();
        m_worker->wait();
    }
    QDialog::closeEvent(event);
}
