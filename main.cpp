#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QtWebView>

#include "backend/FloodService.h"
#include "backend/LocalApiServer.h"

int main(int argc, char *argv[]) {
    QtWebView::initialize();                       // must run before QGuiApplication is created
    QGuiApplication app(argc, argv);
    app.setApplicationName("FloodMonitor");
    app.setOrganizationName("FloodMonitor");

    // App-private writable folder (on Android: the app's internal "files" directory).
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    FloodService service(dataDir);                 // original flood logic + its data files
    LocalApiServer server(&service, dataDir);      // bridge to the HTML/JS interface
    if (!server.start()) {
        qCritical("Could not start the local interface server.");
        return 1;
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("startUrl", server.url());
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("FloodApp", "Main");

    return app.exec();
}
