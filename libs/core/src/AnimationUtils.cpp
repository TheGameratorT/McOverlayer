#include "core/AnimationUtils.h"

#include <QFile>
#include <QJsonDocument>
#include <QImage>

namespace Core {

std::optional<QJsonObject> parseAnimationMcmeta(const QString &imagePath)
{
    QFile f(imagePath + QStringLiteral(".mcmeta"));
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isNull() || !doc.isObject())
        return std::nullopt;
    const QJsonValue animVal = doc.object().value(QStringLiteral("animation"));
    if (animVal.isUndefined())
        return std::nullopt;
    return animVal.toObject();
}

int getAnimationFrameCount(const QString &imagePath, const std::optional<QJsonObject> &anim)
{
    if (!anim.has_value())
        return 1;

    QImage img(imagePath);
    if (img.isNull() || img.width() == 0)
        return 1;

    int frameH = img.width();  // each MC frame is width × width
    return qMax(1, img.height() / frameH);
}

} // namespace Core
