#include "core/PathConfig.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <stdexcept>

namespace Core {

PathConfigMap parsePathConfig(const QString &jsonStr)
{
    if (jsonStr.isEmpty())
        return {};

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &err);
    if (doc.isNull())
        throw std::runtime_error("Invalid path-config JSON: " + err.errorString().toStdString());

    const QJsonObject root = doc.object();
    PathConfigMap result;

    for (const QString &key : root.keys()) {
        const QJsonObject settings = root.value(key).toObject();
        QVariantMap m;
        if (settings.contains(QStringLiteral("scale")))
            m.insert(QStringLiteral("scale"), settings.value(QStringLiteral("scale")).toInt());
        if (settings.contains(QStringLiteral("alpha")))
            m.insert(QStringLiteral("alpha"), settings.value(QStringLiteral("alpha")).toDouble());
        if (settings.contains(QStringLiteral("overlay-scale")))
            m.insert(QStringLiteral("overlay-scale"), settings.value(QStringLiteral("overlay-scale")).toDouble());
        if (settings.contains(QStringLiteral("keep-aspect")))
            m.insert(QStringLiteral("keep-aspect"), settings.value(QStringLiteral("keep-aspect")).toBool());
        if (settings.contains(QStringLiteral("fast-overlay-size")))
            m.insert(QStringLiteral("fast-overlay-size"), settings.value(QStringLiteral("fast-overlay-size")).toInt());
        result.insert(key, m);
    }

    return result;
}

QVariantMap getPathOverrides(const QString &target, const PathConfigMap &config)
{
    const QString normTarget = QString(target).replace(QLatin1Char('\\'), QLatin1Char('/'));

    // Exact path match takes priority — unambiguously targets a single file.
    for (auto it = config.cbegin(); it != config.cend(); ++it) {
        if (normTarget == QString(it.key()).replace(QLatin1Char('\\'), QLatin1Char('/')))
            return it.value();
    }

    // Prefix match for directory overrides: longest matching prefix wins.
    QVariantMap best;
    int bestLen = 0;
    for (auto it = config.cbegin(); it != config.cend(); ++it) {
        const QString normKey = QString(it.key()).replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (normTarget.startsWith(normKey) && normKey.size() > bestLen) {
            best    = it.value();
            bestLen = normKey.size();
        }
    }
    return best;
}

} // namespace Core
