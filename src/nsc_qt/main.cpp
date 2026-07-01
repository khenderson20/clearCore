#include "nsc_qt/main_window.h"
#include <QApplication>
#include <QSettings>
#include <QStringLiteral>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("clearCore-gui"));
    app.setOrganizationName(QStringLiteral("nsc-qt"));
    app.setApplicationVersion(QStringLiteral("1.1.0"));

    QSettings::setDefaultFormat(QSettings::IniFormat);

    nsc::qt::MainWindow window;
    window.show();

    return app.exec();
}