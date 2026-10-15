QT += core gui widgets sql network testlib
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = U8RestoreTests
INCLUDEPATH += ../QtWidgetsApplication1
SOURCES += U8RestoreTests.cpp \
    ../QtWidgetsApplication1/U8RestoreWidget.cpp \
    ../QtWidgetsApplication1/Common/U8BackupRestorer.cpp \
    ../QtWidgetsApplication1/Common/T3BackupRestorer.cpp \
    ../QtWidgetsApplication1/Common/RemoteFileTransfer.cpp \
    ../QtWidgetsApplication1/Common/repairpathconfigwidget.cpp
HEADERS += ../QtWidgetsApplication1/U8RestoreWidget.h
win32-msvc:QMAKE_CXXFLAGS += /utf-8
win32-msvc:DEFINES += _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
