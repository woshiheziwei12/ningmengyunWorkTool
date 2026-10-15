QT += core gui widgets sql network testlib
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = T3RestoreTests
INCLUDEPATH += ../QtWidgetsApplication1
SOURCES += T3RestoreTests.cpp \
    ../QtWidgetsApplication1/T3RestoreWidget.cpp \
    ../QtWidgetsApplication1/Common/T3BackupRestorer.cpp \
    ../QtWidgetsApplication1/Common/RemoteFileTransfer.cpp \
    ../QtWidgetsApplication1/Common/repairpathconfigwidget.cpp
HEADERS += ../QtWidgetsApplication1/T3RestoreWidget.h
win32-msvc:QMAKE_CXXFLAGS += /utf-8
