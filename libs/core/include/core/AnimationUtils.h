#pragma once

#include <QString>
#include <QJsonObject>
#include <optional>

namespace Core {

// Parse the .mcmeta file adjacent to imagePath and return the "animation" object.
// Returns nullopt when the file doesn't exist, JSON is invalid, or the "animation"
// key is absent. Returns a (possibly empty) QJsonObject when the key is present —
// "animation": {} is valid Minecraft syntax meaning "animate with default settings".
std::optional<QJsonObject> parseAnimationMcmeta(const QString &imagePath);

// Return the number of physical frames stored in the image.
// Animated MC textures stack frames vertically: each frame is width×width pixels.
// Returns 1 for non-animated textures (anim has no value).
int getAnimationFrameCount(const QString &imagePath, const std::optional<QJsonObject> &anim);

} // namespace Core
