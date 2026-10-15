QT += core testlib
QT -= gui
CONFIG += console c++11
TEMPLATE = app
TARGET = RemoteFileTransferTests
INCLUDEPATH += ../QtWidgetsApplication1
SOURCES += RemoteFileTransferTests.cpp ../QtWidgetsApplication1/Common/RemoteFileTransfer.cpp
win32-msvc:QMAKE_CXXFLAGS += /utf-8
