#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QThreadPool>
#include <QAtomicInt>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrent/QtConcurrent>

#include <cstdio>
#include <stdexcept>
#include <csignal>
#include <atomic>
#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

#include "core/MappingConfig.h"
#include "core/ImageProcessor.h"
#include "core/OverlayMapper.h"
#include "core/EntityMapper.h"
#include "core/PathConfig.h"
#include "core/FileUtils.h"
#include "core/EntityLoader.h"
#include "core/Hash.h"

static std::atomic<bool> g_interrupted{false};

static void handleSigInt(int) { g_interrupted.store(true); }

static bool isTty()
{
    return isatty(fileno(stdout)) != 0;
}

static void printStatus(const char *label, const QString &msg, bool isError = false)
{
    const char *bold = isError ? "\x1b[1;31m" : "\x1b[1;32m";
    const char *reset = "\x1b[0m";
    if (isTty()) {
        printf("\r\x1b[K");
        if (isError)
            printf("%s%12s%s %s\n", bold, label, reset, qPrintable(msg));
        else
            printf("%s%12s%s %s\n", bold, label, reset, qPrintable(msg));
    } else {
        printf("%12s %s\n", label, qPrintable(msg));
    }
    fflush(stdout);
}

static void printProgress(int done, int total, double elapsed)
{
    const double pct    = total > 0 ? static_cast<double>(done) / total : 1.0;
    const int    filled = static_cast<int>(40 * pct);
    const double rate   = elapsed > 0 ? done / elapsed : 0.0;
    const double eta    = rate > 0 ? (total - done) / rate : 0.0;
    const int etaMin    = static_cast<int>(eta / 60);
    const int etaSec    = static_cast<int>(eta) % 60;

    char bar[42];
    for (int i = 0; i < 40; ++i)
        bar[i] = i < filled ? '=' : ' ';
    bar[40] = '\0';

    if (isTty())
        printf("\r%d/%d [%s] %5.1f%% ETA: %02d:%02d\x1b[K", done, total, bar, pct * 100, etaMin, etaSec);
    else
        printf("%d/%d [%s] %5.1f%%\n", done, total, bar, pct * 100);
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MCOverlayer"));
    QCoreApplication::setApplicationName(QStringLiteral("mcoverlayer-cli"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    std::signal(SIGINT, handleSigInt);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("High-performance Minecraft texture overlayer"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("overlay_dir"),  QStringLiteral("Directory with overlay images"));
    parser.addPositionalArgument(QStringLiteral("texture_dir"),  QStringLiteral("Source texture directory (modified in-place unless --output-dir is set)"));

    QCommandLineOption workersOpt({QStringLiteral("w"), QStringLiteral("workers")},
                                  QStringLiteral("Worker threads (default: min(32, cpu*2))"),
                                  QStringLiteral("N"));
    QCommandLineOption scaleOpt({QStringLiteral("s"), QStringLiteral("scale")},
                                QStringLiteral("Upscale factor 1-32 (default: 4)"),
                                QStringLiteral("N"), QStringLiteral("4"));
    QCommandLineOption alphaOpt(QStringLiteral("alpha"),
                                QStringLiteral("Blend alpha 0.0-1.0 (default: 0.75)"),
                                QStringLiteral("F"), QStringLiteral("0.75"));
    QCommandLineOption seedOpt(QStringLiteral("seed"),
                               QStringLiteral("Seed as integer or string (default: random)"),
                               QStringLiteral("SEED"));
    QCommandLineOption perFrameOpt(QStringLiteral("per-frame"),
                                   QStringLiteral("Different overlay per animation frame"));
    QCommandLineOption entityRegionsOpt(QStringLiteral("entity-regions"),
                                       QStringLiteral("Path to entity_regions/ directory"),
                                       QStringLiteral("PATH"));
    QCommandLineOption faceModeOpt(QStringLiteral("entity-face-mode"),
                                   QStringLiteral("\"same\" or \"different\" (default: same)"),
                                   QStringLiteral("MODE"), QStringLiteral("same"));
    QCommandLineOption texModeOpt(QStringLiteral("entity-texture-mode"),
                                  QStringLiteral("\"shared\" or \"separate\" (default: shared)"),
                                  QStringLiteral("MODE"), QStringLiteral("shared"));
    QCommandLineOption keepAspectOpt(QStringLiteral("keep-aspect"),
                                     QStringLiteral("Preserve overlay aspect ratio"));
    QCommandLineOption fastOverlaySizeOpt(QStringLiteral("fast-overlay-size"),
                                          QStringLiteral("Pre-load overlays at this size (px) for faster compositing; 0 = off (default: 512)"),
                                          QStringLiteral("N"), QStringLiteral("512"));
    QCommandLineOption overlayScaleOpt(QStringLiteral("overlay-scale"),
                                       QStringLiteral("Overlay size factor 0.0-2.0 (default: 1.0)"),
                                       QStringLiteral("F"), QStringLiteral("1.0"));
    QCommandLineOption pathConfigOpt(QStringLiteral("path-config"),
                                     QStringLiteral("Per-path overrides as JSON. Keys are exact file paths or directory prefixes. "
                                                    "E.g. '{\"pack.png\":{\"scale\":1},\"assets/block\":{\"scale\":8}}'"),
                                     QStringLiteral("JSON"));
    QCommandLineOption outputDirOpt(QStringLiteral("output-dir"),
                                    QStringLiteral("Write results here and copy remaining files (reference mode); "
                                                   "if omitted, textures are modified in-place"),
                                    QStringLiteral("PATH"));

    parser.addOption(workersOpt);
    parser.addOption(scaleOpt);
    parser.addOption(alphaOpt);
    parser.addOption(seedOpt);
    parser.addOption(perFrameOpt);
    parser.addOption(entityRegionsOpt);
    parser.addOption(faceModeOpt);
    parser.addOption(texModeOpt);
    parser.addOption(keepAspectOpt);
    parser.addOption(overlayScaleOpt);
    parser.addOption(pathConfigOpt);
    parser.addOption(fastOverlaySizeOpt);
    parser.addOption(outputDirOpt);

    parser.process(app);

    const QStringList posArgs = parser.positionalArguments();
    if (posArgs.size() < 2) {
        parser.showHelp(1);
    }

    const QString overlayDir  = posArgs[0];
    const QString targetDir   = posArgs[1];
    const QString outputDir   = parser.value(outputDirOpt);
    const bool referenceMode  = !outputDir.isEmpty();

    const QStringList overlays = Core::getImagesInDir(overlayDir);
    if (overlays.isEmpty()) {
        fprintf(stderr, "error: no overlay images found in '%s'\n", qPrintable(overlayDir));
        return 1;
    }

    const QStringList targets = Core::getImagesInDir(targetDir);
    if (targets.isEmpty()) {
        fprintf(stderr, "error: no target images found in '%s'\n", qPrintable(targetDir));
        return 1;
    }

    const int maxWorkers = qMin(8, QThread::idealThreadCount());
    const int workers    = parser.isSet(workersOpt)
                               ? parser.value(workersOpt).toInt()
                               : qMax(1, maxWorkers);
    const int scale      = parser.value(scaleOpt).toInt();
    const double alpha   = parser.value(alphaOpt).toDouble();
    const double ovlScale= parser.value(overlayScaleOpt).toDouble();
    const bool perFrame  = parser.isSet(perFrameOpt);
    const bool keepAspect        = parser.isSet(keepAspectOpt);
    const int  fastOverlaySize   = parser.value(fastOverlaySizeOpt).toInt();
    QString entityRegDir = parser.value(entityRegionsOpt);
    if (entityRegDir.isEmpty())
        entityRegDir = Core::MappingConfig::defaultEntityRegionsDir();
    const QString faceMode     = parser.value(faceModeOpt);
    const QString texMode      = parser.value(texModeOpt);
    QJsonObject pathConfigObj;
    if (parser.isSet(pathConfigOpt)) {
        const QString pathConfigJson = parser.value(pathConfigOpt);
        QJsonParseError parseErr;
        const QJsonDocument pathDoc = QJsonDocument::fromJson(pathConfigJson.toUtf8(), &parseErr);
        if (pathDoc.isNull() || !pathDoc.isObject()) {
            fprintf(stderr, "error: --path-config: invalid JSON: %s\n",
                    qPrintable(parseErr.errorString()));
            return 1;
        }
        pathConfigObj = pathDoc.object();
    }

    // Resolve seed
    qint64 seed = 0;
    if (parser.isSet(seedOpt)) {
        const QString seedStr = parser.value(seedOpt);
        bool ok = false;
        seed = seedStr.toLongLong(&ok);
        if (!ok)
            seed = static_cast<qint64>(Core::stringToSeed(seedStr));
    } else {
        seed = static_cast<qint64>(QRandomGenerator::global()->bounded(static_cast<quint32>(0xFFFFFFFF)));
    }

    // Load entity regions
    Core::EntityRegions er;
    if (!entityRegDir.isEmpty()) {
        try {
            er = Core::loadEntityRegions(entityRegDir);
        } catch (const std::exception &e) {
            fprintf(stderr, "error: failed to load entity regions: %s\n", e.what());
            return 1;
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

    printStatus("Overlaying",
        QString::number(targets.size()) + QStringLiteral(" images (") +
        QString::number(entityTargets.size()) + QStringLiteral(" entity, ") +
        QString::number(regularTargets.size()) + QStringLiteral(" regular) with ") +
        QString::number(workers) + QStringLiteral(" workers") +
        QStringLiteral("  scale=") + QString::number(scale) +
        QStringLiteral(" alpha=") + QString::number(alpha) +
        QStringLiteral(" seed=") + QString::number(seed));

    // Pre-load overlays
    Core::ImageCache cache;
    QElapsedTimer loadTimer;
    loadTimer.start();

    const Core::PathConfigMap pathConfig = Core::parsePathConfig(pathConfigObj);

    auto resolveFastSize = [&](const QString &path) -> int {
        const QString relT = Core::relPath(targetDir, path);
        const QVariantMap ov = Core::getPathOverrides(relT, pathConfig);
        if (ov.contains(QStringLiteral("fast-overlay-size")))
            return ov.value(QStringLiteral("fast-overlay-size")).toInt();
        return fastOverlaySize;
    };

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

    printProgress(0, overlays.size(), 0.0);
    for (int sz : fastSizes) {
        printStatus("Loading", QStringLiteral("overlays at %1px...").arg(sz));
        cache.preloadFixed(overlays, sz, keepAspect, [&](int done, int total) {
            printProgress(done, total, loadTimer.elapsed() / 1000.0);
        });
    }
    if (anyNonFast) {
        printStatus("Loading", QStringLiteral("overlay images..."));
        cache.preloadOverlays(overlays, [&](int done, int total) {
            printProgress(done, total, loadTimer.elapsed() / 1000.0);
        });
    }
    if (isTty()) { printf("\r\x1b[K"); fflush(stdout); }

    // Build mappers
    QScopedPointer<Core::OverlayMapper> mapper;
    QScopedPointer<Core::EntityMapper>  entityMapper;

    if (!regularTargets.isEmpty())
        mapper.reset(new Core::OverlayMapper(regularTargets, overlays, seed, perFrame, targetDir, overlayDir));

    if (!entityTargets.isEmpty())
        entityMapper.reset(new Core::EntityMapper(er.entities, overlays, faceMode, texMode, seed, overlayDir));

    // Process all images with thread pool
    QAtomicInt shutdownFlag{0};
    QAtomicInt doneCount{0};
    const int total = static_cast<int>(targets.size());
    QElapsedTimer timer;
    timer.start();

    printProgress(0, total, 0.0);

    QThreadPool pool;
    pool.setMaxThreadCount(workers);

    // We use QtConcurrent::run for each task and collect futures
    QList<QFuture<QPair<QString,QString>>> futures;
    futures.reserve(total);

    auto outputPathFor = [&](const QString &srcPath) -> QString {
        if (!referenceMode) return {};
        return outputDir + QLatin1Char('/') + Core::relPath(targetDir, srcPath);
    };

    for (const EntityTarget &et : entityTargets) {
        const QString relT = Core::relPath(targetDir, et.path);
        const QVariantMap ov = Core::getPathOverrides(relT, pathConfig);
        const int fastSize = resolveFastSize(et.path);
        const QString outPath = outputPathFor(et.path);

        const Core::EntityData &data = er.entities.value(et.entityId);
        QList<Core::EntityRegion> regions;
        QStringList faceOvPaths;
        entityMapper->getFaceOverlays(et.canonicalTex, regions, faceOvPaths);

        futures.append(QtConcurrent::run(&pool, [=, &cache, &shutdownFlag, &doneCount, &timer]() {
            auto r = Core::processEntityImage(
                et.path, outPath, regions, faceOvPaths,
                data.textureWidth, data.textureHeight,
                cache,
                ov.value(QStringLiteral("scale"), scale).toInt(),
                ov.value(QStringLiteral("alpha"), alpha).toDouble(),
                &shutdownFlag,
                ov.value(QStringLiteral("keep-aspect"), keepAspect).toBool(),
                ov.value(QStringLiteral("overlay-scale"), ovlScale).toDouble(),
                fastSize);
            int n = doneCount.fetchAndAddRelaxed(1) + 1;
            printProgress(n, total, timer.elapsed() / 1000.0);
            return r;
        }));
    }

    if (mapper) {
        for (const QString &t : regularTargets) {
            if (g_interrupted.load()) { shutdownFlag.storeRelaxed(1); break; }
            const QString relT = Core::relPath(targetDir, t);
            const QVariantMap ov = Core::getPathOverrides(relT, pathConfig);
            const int fastSize = resolveFastSize(t);
            const QString outPath = outputPathFor(t);

            futures.append(QtConcurrent::run(&pool, [=, &cache, &mapper, &shutdownFlag, &doneCount, &timer]() {
                auto r = Core::processImage(
                    t, outPath, *mapper, cache,
                    ov.value(QStringLiteral("scale"), scale).toInt(),
                    ov.value(QStringLiteral("alpha"), alpha).toDouble(),
                    &shutdownFlag,
                    ov.value(QStringLiteral("keep-aspect"), keepAspect).toBool(),
                    ov.value(QStringLiteral("overlay-scale"), ovlScale).toDouble(),
                    fastSize);
                int n = doneCount.fetchAndAddRelaxed(1) + 1;
                printProgress(n, total, timer.elapsed() / 1000.0);
                return r;
            }));
        }
    }

    // Wait for all tasks
    for (auto &f : futures) {
        if (g_interrupted.load())
            shutdownFlag.storeRelaxed(1);
        f.waitForFinished();
    }

    if (!g_interrupted.load() && referenceMode) {
        const QSet<QString> imageExts = {
            QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg")
        };
        QDirIterator it(targetDir, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString srcPath = it.next();
            QFileInfo fi(srcPath);
            if (!fi.isFile() || imageExts.contains(fi.suffix().toLower()))
                continue;
            const QString rel     = Core::relPath(targetDir, srcPath);
            const QString dstPath = outputDir + QLatin1Char('/') + rel;
            QDir().mkpath(QFileInfo(dstPath).absolutePath());
            QFile::copy(srcPath, dstPath);
            printStatus("Copying", rel);
        }
    }

    if (isTty()) { printf("\r\x1b[K"); fflush(stdout); }

    if (g_interrupted.load())
        printStatus("Interrupted",
            QString::number(doneCount.loadRelaxed()) + QStringLiteral("/") + QString::number(total) + QStringLiteral(" images processed"),
            true);
    else
        printStatus("Finished",
            QString::number(total) + QStringLiteral(" images in ") +
            QString::number(timer.elapsed() / 1000.0, 'f', 2) + QStringLiteral("s"));

    // Save run config
    Core::MappingConfig cfg;
    cfg.overlayDir       = overlayDir;
    cfg.textureDir       = targetDir;
    cfg.seed             = seed;
    cfg.perFrame         = perFrame;
    cfg.entityRegionsDir = entityRegDir;
    cfg.entityFaceMode   = faceMode;
    cfg.entityTextureMode= texMode;
    cfg.alpha            = alpha;
    cfg.scale            = scale;
    cfg.keepAspect       = keepAspect;
    cfg.overlayScale     = ovlScale;
    cfg.pathConfig       = pathConfigObj;
    cfg.fastOverlaySize  = fastOverlaySize;
    cfg.outputDir        = outputDir;

    QFile lastRun(Core::MappingConfig::lastRunPath());
    if (lastRun.open(QIODevice::WriteOnly | QIODevice::Text))
        lastRun.write(QJsonDocument(cfg.toJson()).toJson());

    QFile meta(targetDir + QStringLiteral("/overlay_meta.json"));
    if (meta.open(QIODevice::WriteOnly | QIODevice::Text))
        meta.write(QJsonDocument(cfg.toJson()).toJson());

    return g_interrupted.load() ? 130 : 0;
}
