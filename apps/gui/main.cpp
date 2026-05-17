#include "MainWindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("MCOverlayer"));
    QApplication::setApplicationVersion(QStringLiteral("1.0"));
    QApplication::setOrganizationName(QStringLiteral("MCOverlayer"));

    MainWindow w;
    w.show();

    return app.exec();
}
