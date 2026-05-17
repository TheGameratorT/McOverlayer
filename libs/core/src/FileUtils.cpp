#include "core/FileUtils.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

namespace Core {

static const QStringList kSupportedExts = {
    QStringLiteral("png"),
    QStringLiteral("jpg"),
    QStringLiteral("jpeg"),
};

QStringList getImagesInDir(const QString &root)
{
    QStringList result;
    QDirIterator it(root, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString path = it.next();
        QFileInfo fi(path);
        if (fi.isFile() && kSupportedExts.contains(fi.suffix().toLower()))
            result.append(path);
    }
    return result;
}

QString relPath(const QString &dir, const QString &file)
{
    return QDir(QDir(dir).absolutePath()).relativeFilePath(QFileInfo(file).absoluteFilePath());
}

} // namespace Core
