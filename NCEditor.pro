TEMPLATE = app
TARGET = NCEditor

QT += core gui qml quick widgets concurrent

CONFIG += c++17

INCLUDEPATH += $$PWD/src

SOURCES += \
    src/main.cpp \
    src/workspacecontroller.cpp \
    src/documentmanager.cpp \
    src/documentsession.cpp \
    src/paintededitoritem.cpp \
    src/piecetablebuffer.cpp \
    src/lineindex.cpp \
    src/nc/ncdialects.cpp \
    src/nc/ncdiagnosticmessages.cpp \
    src/nc/ncparser.cpp

HEADERS += \
    src/workspacecontroller.h \
    src/documentmanager.h \
    src/documentsession.h \
    src/paintededitoritem.h \
    src/pathutils.h \
    src/piecetablebuffer.h \
    src/lineindex.h \
    src/nc/ncdialects.h \
    src/nc/ncdiagnosticmessages.h \
    src/nc/ncparser.h

RESOURCES += \
    qml.qrc

DISTFILES += \
    docs/NC_PREINTERPRETER_ARCHITECTURE.md \
    tests/ncparser_smoke.cpp \
    tests/ncparser_smoke.pro \
    qml/main.qml \
    qml/components/EditorPane.qml \
    qml/components/EditorViewport.qml \
    qml/components/FloatingMenu.qml \
    qml/components/FindReplaceDialog.qml
