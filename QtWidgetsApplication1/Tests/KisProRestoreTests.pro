QT += core gui widgets sql network testlib
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = KisProRestoreTests
INCLUDEPATH += ../QtWidgetsApplication1
SOURCES += KisProRestoreTests.cpp \
    ../QtWidgetsApplication1/KisProRestoreWidget.cpp \
    ../QtWidgetsApplication1/Common/KisProBackupRestorer.cpp \
    ../QtWidgetsApplication1/Common/T3BackupRestorer.cpp \
    ../QtWidgetsApplication1/Common/RemoteFileTransfer.cpp \
    ../QtWidgetsApplication1/Common/repairpathconfigwidget.cpp
HEADERS += ../QtWidgetsApplication1/KisProRestoreWidget.h
win32-msvc:QMAKE_CXXFLAGS += /utf-8
win32-msvc:DEFINES += _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
