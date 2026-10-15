QT += core gui widgets
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = SudaBackupTests
INCLUDEPATH += ../QtWidgetsApplication1
SOURCES += SudaBackupTests.cpp \
    ../QtWidgetsApplication1/SudaBackupWidget.cpp \
    ../QtWidgetsApplication1/Common/repairpathconfigwidget.cpp \
    ../QtWidgetsApplication1/Common/SudaBackupUnpacker.cpp
HEADERS += ../QtWidgetsApplication1/SudaBackupWidget.h \
    ../QtWidgetsApplication1/Common/SudaBackupUnpacker.h
win32-msvc:QMAKE_CXXFLAGS += /utf-8
