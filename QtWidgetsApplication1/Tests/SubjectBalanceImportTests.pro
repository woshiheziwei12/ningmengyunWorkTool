QT += core gui widgets network testlib
CONFIG += console c++11 release
CONFIG -= app_bundle debug
TEMPLATE = app
TARGET = SubjectBalanceImportTests
INCLUDEPATH += ../QtWidgetsApplication1
SOURCES += SubjectBalanceImportTests.cpp \
    ../QtWidgetsApplication1/Common/subjectbalanceimport.cpp \
    ../QtWidgetsApplication1/subjectbalanceimportwidget.cpp \
    ../QtWidgetsApplication1/lemoncofighelper.cpp \
    ../QtWidgetsApplication1/Common/globaldata.cpp \
    ../QtWidgetsApplication1/Common/usertoken.cpp
HEADERS += ../QtWidgetsApplication1/Common/subjectbalanceimport.h \
    ../QtWidgetsApplication1/subjectbalanceimportwidget.h \
    ../QtWidgetsApplication1/Common/usertoken.h
RESOURCES += ../QtWidgetsApplication1/themestyle.qrc
win32-msvc:QMAKE_CXXFLAGS += /utf-8
win32-msvc:DEFINES += _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING

SOURCES += ../QtWidgetsApplication1/Common/subjectbalancefiles.cpp \
    ../LemonWorkToolCli/networkutils.cpp \
    ../QtWidgetsApplication1/startupwindow.cpp \
    ../QtWidgetsApplication1/pwdloginwidget.cpp \
    ../QtWidgetsApplication1/httprequesthelper.cpp
HEADERS += ../QtWidgetsApplication1/startupwindow.h \
    ../QtWidgetsApplication1/pwdloginwidget.h
FORMS += ../QtWidgetsApplication1/startupwindow.ui \
    ../QtWidgetsApplication1/pwdloginwidget.ui
