QT += core gui widgets
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = SudaBackupWidgetTests
INCLUDEPATH += ../QtWidgetsApplication1
SOURCES += SudaBackupWidgetTests.cpp \
    ../QtWidgetsApplication1/SudaBackupWidget.cpp \
    ../QtWidgetsApplication1/Common/SudaBackupUnpacker.cpp \
    ../QtWidgetsApplication1/Common/repairpathconfigwidget.cpp
HEADERS += ../QtWidgetsApplication1/SudaBackupWidget.h
win32-msvc:QMAKE_CXXFLAGS += /utf-8
