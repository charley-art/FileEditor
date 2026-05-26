TEMPLATE = app
TARGET = ncparser_smoke

QT -= gui
QT += core
CONFIG += console c++17
CONFIG -= app_bundle

INCLUDEPATH += $$PWD/../src

SOURCES += \
    $$PWD/ncparser_smoke.cpp \
    $$PWD/../src/nc/ncdialects.cpp \
    $$PWD/../src/nc/ncdiagnosticmessages.cpp \
    $$PWD/../src/nc/ncparser.cpp

HEADERS += \
    $$PWD/../src/nc/ncdialects.h \
    $$PWD/../src/nc/ncdiagnosticmessages.h \
    $$PWD/../src/nc/ncparser.h

DESTDIR = $$PWD/bin
OBJECTS_DIR = $$PWD/obj
MOC_DIR = $$PWD/moc
RCC_DIR = $$PWD/rcc
UI_DIR = $$PWD/ui
