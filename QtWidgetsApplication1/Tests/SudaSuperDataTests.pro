QT += core
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = SudaSuperDataTests
INCLUDEPATH += ../QtWidgetsApplication1
SOURCES += SudaSuperDataTests.cpp ../QtWidgetsApplication1/Common/SudaBackupUnpacker.cpp
win32-msvc:QMAKE_CXXFLAGS += /utf-8
