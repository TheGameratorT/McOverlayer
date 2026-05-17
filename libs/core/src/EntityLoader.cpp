#include "core/EntityLoader.h"

#include <QDir>
#include <QFile>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <stdexcept>

namespace Core {

static EntityRegion regionFromJson(const QJsonObject &obj)
{
    EntityRegion r;
    r.name   = obj.value(QStringLiteral("name")).toString();
    r.x      = obj.value(QStringLiteral("x")).toInt();
    r.y      = obj.value(QStringLiteral("y")).toInt();
    r.width  = obj.value(QStringLiteral("width")).toInt();
    r.height = obj.value(QStringLiteral("height")).toInt();
    r.flip   = obj.value(QStringLiteral("flip")).toString();
    r.rotate = obj.value(QStringLiteral("rotate")).toString();
    return r;
}

EntityRegions loadEntityRegions(const QString &dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists())
        throw std::runtime_error("Entity regions directory not found: " + dirPath.toStdString());

    EntityRegions result;

    const QStringList jsonFiles = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &fn : jsonFiles) {
        QFile f(dir.filePath(fn));
        if (!f.open(QIODevice::ReadOnly))
            continue;

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (doc.isNull())
            throw std::runtime_error("Invalid JSON in " + fn.toStdString() + ": " + err.errorString().toStdString());

        const QJsonObject root = doc.object();
        for (const QString &entityId : root.keys()) {
            const QJsonObject info = root.value(entityId).toObject();

            QJsonArray tsArr = info.value(QStringLiteral("texture_size")).toArray();
            int tw = tsArr.size() > 0 ? tsArr[0].toInt(64) : 64;
            int th = tsArr.size() > 1 ? tsArr[1].toInt(64) : 64;

            EntityData data;
            data.entityId     = entityId;
            data.textureWidth  = tw;
            data.textureHeight = th;

            for (const QJsonValue &rv : info.value(QStringLiteral("regions")).toArray())
                data.regions.append(regionFromJson(rv.toObject()));

            for (const QJsonValue &tv : info.value(QStringLiteral("textures")).toArray())
                data.textures.append(tv.toString());

            result.entities.insert(entityId, data);
            for (const QString &tex : data.textures)
                result.textureToEntity.insert(tex, entityId);
        }
    }

    // Merge bed_head + bed_foot into a single "bed" entity
    if (result.entities.contains(QStringLiteral("bed_head")) &&
        result.entities.contains(QStringLiteral("bed_foot")))
    {
        const EntityData &head = result.entities.value(QStringLiteral("bed_head"));
        const EntityData &foot = result.entities.value(QStringLiteral("bed_foot"));

        EntityData bed;
        bed.entityId     = QStringLiteral("bed");
        bed.textureWidth  = head.textureWidth;
        bed.textureHeight = head.textureHeight;

        for (const EntityRegion &r : head.regions) {
            EntityRegion nr = r;
            nr.name = QStringLiteral("head_") + r.name;
            bed.regions.append(nr);
        }
        for (const EntityRegion &r : foot.regions) {
            EntityRegion nr = r;
            nr.name = QStringLiteral("foot_") + r.name;
            bed.regions.append(nr);
        }

        QSet<QString> seen;
        for (const QString &t : head.textures + foot.textures) {
            if (!seen.contains(t)) {
                seen.insert(t);
                bed.textures.append(t);
            }
        }

        result.entities.remove(QStringLiteral("bed_head"));
        result.entities.remove(QStringLiteral("bed_foot"));
        result.entities.insert(QStringLiteral("bed"), bed);

        for (const QString &t : bed.textures)
            result.textureToEntity.insert(t, QStringLiteral("bed"));
    }

    return result;
}

} // namespace Core
