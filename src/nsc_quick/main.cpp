// ─── main.cpp — clearCore-quick entry point ──────────────────────────────────

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("clearCore"));
    app.setOrganizationName(QStringLiteral("clearCore"));
    app.setApplicationVersion(QStringLiteral("1.0"));

    // All controls in the ClearCore module define their own contentItem /
    // background, so Basic is the correct (lightest) base style.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("ClearCore", "Main");

    return app.exec();
}
