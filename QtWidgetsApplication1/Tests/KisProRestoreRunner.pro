QT += core sql network
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = KisProRestoreRunner
INCLUDEPATH += ../QtWidgetsApplication1
SOURCES += KisProRestoreRunner.cpp \
    ../QtWidgetsApplication1/Common/KisProBackupRestorer.cpp \
    ../QtWidgetsApplication1/Common/T3BackupRestorer.cpp \
    ../QtWidgetsApplication1/Common/RemoteFileTransfer.cpp
win32-msvc:QMAKE_CXXFLAGS += /utf-8
win32-msvc:DEFINES += _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
