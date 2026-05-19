#include "core/MappingConfig.h"
#include "core/Hash.h"
#include "core/OverlayMapper.h"
#include "core/EntityMapper.h"
#include "core/FileUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <stdexcept>

namespace Core {

MappingConfig MappingConfig::fromJson(const QJsonObject &obj)
{
    MappingConfig c;
    c.overlayDir = obj.value(QStringLiteral("overlay_dir")).toString(
                   obj.value(QStringLiteral("input_dir")).toString(QStringLiteral("dataset/")));
    c.textureDir = obj.value(QStringLiteral("texture_dir")).toString(
                   obj.value(QStringLiteral("target_dir")).toString(QStringLiteral("target/")));
    c.outputDir  = obj.value(QStringLiteral("output_dir")).toString(
                   obj.value(QStringLiteral("source_dir")).toString());
    c.seed             = static_cast<qint64>(obj.value(QStringLiteral("seed")).toDouble());
    c.perFrame         = obj.value(QStringLiteral("per_frame")).toBool(false);
    // If the key is absent it was omitted because it matched the bundled default on
    // the exporting machine — resolve to the local bundled default instead.
    c.entityRegionsDir = obj.contains(QStringLiteral("entity_regions"))
        ? obj.value(QStringLiteral("entity_regions")).toString()
        : defaultEntityRegionsDir();
    c.entityFaceMode    = obj.value(QStringLiteral("entity_face_mode")).toString(QStringLiteral("same"));
    c.entityTextureMode = obj.value(QStringLiteral("entity_texture_mode")).toString(QStringLiteral("shared"));
    c.alpha           = obj.value(QStringLiteral("alpha")).toDouble(0.75);
    c.scale           = obj.value(QStringLiteral("scale")).toInt(4);
    c.keepAspect      = obj.value(QStringLiteral("keep_aspect")).toBool(false);
    c.overlayScale    = obj.value(QStringLiteral("overlay_scale")).toDouble(1.0);
    c.pathConfig      = obj.value(QStringLiteral("path_config")).toObject();
    c.fastOverlaySize = obj.value(QStringLiteral("fast_overlay_size")).toInt(0);
    return c;
}

MappingConfig MappingConfig::fromLastRun(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error("Config not found: " + path.toStdString());

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isNull())
        throw std::runtime_error("Invalid JSON in: " + path.toStdString());

    return fromJson(doc.object());
}

QJsonObject MappingConfig::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("overlay_dir"),          overlayDir);
    obj.insert(QStringLiteral("texture_dir"),          textureDir);
    obj.insert(QStringLiteral("seed"),                 static_cast<qint64>(seed));
    obj.insert(QStringLiteral("per_frame"),            perFrame);
    // Omit entity_regions when it equals the machine-specific bundled default so
    // the config is portable — the importing machine will discover its own default.
    // An explicitly empty value is kept so the user's choice to disable it is preserved.
    if (entityRegionsDir.isEmpty() || entityRegionsDir != defaultEntityRegionsDir())
        obj.insert(QStringLiteral("entity_regions"), entityRegionsDir);
    obj.insert(QStringLiteral("entity_face_mode"),     entityFaceMode);
    obj.insert(QStringLiteral("entity_texture_mode"),  entityTextureMode);
    obj.insert(QStringLiteral("alpha"),                alpha);
    obj.insert(QStringLiteral("scale"),                scale);
    obj.insert(QStringLiteral("keep_aspect"),          keepAspect);
    obj.insert(QStringLiteral("overlay_scale"),        overlayScale);
    if (!pathConfig.isEmpty())
        obj.insert(QStringLiteral("path_config"),      pathConfig);
    obj.insert(QStringLiteral("output_dir"),           outputDir);
    obj.insert(QStringLiteral("fast_overlay_size"), fastOverlaySize);
    return obj;
}

void MappingConfig::saveLastRun(const QString &path) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
}

QString MappingConfig::lastRunPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/last_run.json");
}

QString MappingConfig::defaultEntityRegionsDir()
{
    const QString candidate = QCoreApplication::applicationDirPath() + QStringLiteral("/entity_regions");
    if (QDir(candidate).exists())
        return candidate;

#ifdef Q_OS_LINUX
    const QStringList found = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation,
        QStringLiteral("MCOverlayer/entity_regions"),
        QStandardPaths::LocateDirectory);
    if (!found.isEmpty())
        return found.first();
#endif

    return QString{};
}

MappingConfig MappingConfig::withSeed(const QString &seedStr) const
{
    MappingConfig c = *this;
    bool ok = false;
    const qint64 n = seedStr.toLongLong(&ok);
    c.seed = ok ? n : static_cast<qint64>(Core::stringToSeed(seedStr));
    return c;
}

MappingConfig MappingConfig::withRandomSeed() const
{
    MappingConfig c = *this;
    c.seed = static_cast<qint64>(QRandomGenerator::global()->bounded(static_cast<quint32>(0xFFFFFFFF)));
    return c;
}

BuildResult buildAssignments(const MappingConfig &config)
{
    QStringList overlays = getImagesInDir(config.overlayDir);
    if (overlays.isEmpty())
        throw std::runtime_error("No overlay images found in: " + config.overlayDir.toStdString());

    QStringList targets = getImagesInDir(config.textureDir);
    if (targets.isEmpty())
        throw std::runtime_error("No texture images found in: " + config.textureDir.toStdString());

    // Load entity regions if configured
    EntityRegions er;
    if (!config.entityRegionsDir.isEmpty())
        er = loadEntityRegions(config.entityRegionsDir);

    // Categorise targets into entity vs regular
    struct EntityTarget { QString path, entityId, canonicalTex; };
    QList<EntityTarget> entityTargets;
    QStringList regularTargets;

    // Pre-normalise texture paths once
    QList<QPair<QString, QString>> normTexPaths;
    normTexPaths.reserve(er.textureToEntity.size());
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

    BuildResult result;
    result.overlayPaths = overlays;
    result.targetPaths  = targets;

    if (!regularTargets.isEmpty()) {
        OverlayMapper mapper(regularTargets, overlays, config.seed,
                             config.perFrame, config.textureDir, config.overlayDir);
        for (const QString &t : regularTargets) {
            TextureAssignment a;
            a.targetPath  = t;
            a.overlayPath = mapper.getOverlay(t);
            result.assignments.append(a);
        }
    }

    if (!entityTargets.isEmpty()) {
        EntityMapper em(er.entities, overlays,
                        config.entityFaceMode, config.entityTextureMode,
                        config.seed, config.overlayDir);
        for (const auto &et : entityTargets) {
            TextureAssignment a;
            a.targetPath = et.path;
            a.isEntity   = true;
            a.entityId   = et.entityId;
            em.getFaceOverlays(et.canonicalTex, a.faceRegions, a.faceOverlayPaths);
            a.overlayPath = a.faceOverlayPaths.isEmpty() ? overlays.first() : a.faceOverlayPaths.first();
            const EntityData &edata = er.entities.value(et.entityId);
            a.textureWidth  = edata.textureWidth;
            a.textureHeight = edata.textureHeight;
            result.assignments.append(a);
        }
    }

    return result;
}

} // namespace Core
