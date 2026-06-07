#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml>

#include "editorconfig.h"
#include "paintededitoritem.h"
#include "documentsession.h"
#include "workspacecontroller.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("NCEditor");
    QCoreApplication::setApplicationName("NCEditor");

    WorkspaceController controller;
    EditorConfig &config = EditorConfig::instance();

    qmlRegisterType<PaintedEditorItem>("NCEditor", 1, 0, "PaintedEditorItem");
    qmlRegisterType<DocumentSession>("NCEditor", 1, 0, "DocumentSession");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("editorConfig", &config);
    engine.rootContext()->setContextProperty("workspaceController", &controller);
    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
