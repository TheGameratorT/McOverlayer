#pragma once

#include <QString>
#include <QStringList>

namespace Core {

// Recursively walk root and return all files with supported image extensions.
// Supported: .png, .jpg, .jpeg (case-insensitive)
QStringList getImagesInDir(const QString &root);

// Return path of file relative to dir, correctly handling relative inputs by
// forcing both sides through their absolute forms first.
QString relPath(const QString &dir, const QString &file);

} // namespace Core
