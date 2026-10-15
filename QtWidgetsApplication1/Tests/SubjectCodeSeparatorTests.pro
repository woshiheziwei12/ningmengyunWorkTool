QT += core gui widgets network
CONFIG += console c++11 release
CONFIG -= app_bundle debug
TEMPLATE = app
TARGET = SubjectCodeSeparatorTests
INCLUDEPATH += ../QtWidgetsApplication1
SOURCES += SubjectCodeSeparatorTests.cpp \
    ../QtWidgetsApplication1/Common/logger.cpp \
    ../QtWidgetsApplication1/Common/repairpathconfigwidget.cpp \
    ../QtWidgetsApplication1/Common/repairinputfilepickerwidget.cpp
HEADERS += ../QtWidgetsApplication1/subjectcodeseparatorwidget.h
win32-msvc:QMAKE_CXXFLAGS += /utf-8
win32-msvc:DEFINES += _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
