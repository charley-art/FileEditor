#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtGlobal>
#include <QtQml>

#include "paintededitoritem.h"
#include "workspacecontroller.h"

// Compile-time switch for the in-app performance overlay.
// 0: off (default), 1: on.
#ifndef NCEDITOR_PERF_OVERLAY_ENABLED
#define NCEDITOR_PERF_OVERLAY_ENABLED 0
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("NCEditor");
    QCoreApplication::setApplicationName("NCEditor");

    WorkspaceController controller;
    bool ok = false;
    const int pasteLimitKb = qEnvironmentVariableIntValue("NCEDITOR_PASTE_LIMIT_KB", &ok);
    if (ok) {
        controller.setPasteLimitBytes(pasteLimitKb * 1024);
    }
    const bool perfOverlayEnabled = (NCEDITOR_PERF_OVERLAY_ENABLED != 0);

    qmlRegisterType<PaintedEditorItem>("NCEditor", 1, 0, "PaintedEditorItem");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("workspaceController", &controller);
    engine.rootContext()->setContextProperty("perfOverlayEnabled", perfOverlayEnabled);
    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
