#pragma once

#include <QHash>
#include <QJsonObject>
#include <QVariantMap>

namespace Core {

using PathConfigMap = QHash<QString, QVariantMap>;

// Build a PathConfigMap from a QJsonObject whose keys are exact file paths or
// directory prefixes and whose values are objects of per-path override settings.
// Supported override keys: "scale" (int), "alpha" (double),
//                          "overlay-scale" (double), "keep-aspect" (bool),
//                          "fast-overlay-size" (int, 0 = disabled).
PathConfigMap parsePathConfig(const QJsonObject &obj);

// Return overrides for target.
// Exact path match takes priority over prefix match; among prefix matches, longest wins.
// target is a relative path with forward-slash separators.
// Returns an empty map when nothing matches.
QVariantMap getPathOverrides(const QString &target, const PathConfigMap &config);

} // namespace Core
