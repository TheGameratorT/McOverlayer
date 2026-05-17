#include "RegionEditorWindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("MCOverlayer Region Editor"));
    QApplication::setApplicationVersion(QStringLiteral("1.0"));

    RegionEditorWindow w;
    w.show();

    return app.exec();
}
