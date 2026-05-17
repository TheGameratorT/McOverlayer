#pragma once

#include <QString>
#include <QHash>
#include <QVariantMap>

namespace Core {

using PathConfigMap = QHash<QString, QVariantMap>;

// Parse a JSON string like '{"block":{"scale":8},"pack.png":{"scale":1}}'.
// Keys can be directory prefixes ("assets/block") or exact file paths ("pack.png").
// Supported override keys: "scale" (int), "alpha" (double),
//                          "overlay-scale" (double), "keep-aspect" (bool),
//                          "fast-overlay-size" (int, 0 = disabled).
// Throws std::runtime_error on invalid JSON.
PathConfigMap parsePathConfig(const QString &jsonStr);

// Return overrides for target.
// Exact path match takes priority over prefix match; among prefix matches, longest wins.
// target is a relative path with forward-slash separators.
// Returns an empty map when nothing matches.
QVariantMap getPathOverrides(const QString &target, const PathConfigMap &config);

} // namespace Core
