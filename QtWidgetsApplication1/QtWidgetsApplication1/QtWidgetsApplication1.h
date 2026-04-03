#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_QtWidgetsApplication1.h"

class GlobalData;
class FunctionSelectWidget;
class DeleteAccountSetWidget;
class BackupAccountSetWidget;
class QStackedWidget;

class QtWidgetsApplication1 : public QMainWindow
{
    Q_OBJECT

public:
    explicit QtWidgetsApplication1(GlobalData *globalData, QWidget *parent = nullptr);
    ~QtWidgetsApplication1();

private slots:
    void onFunctionSelected(int functionId);
    void onBackToMain();

private:
    void showFunctionSelect();
    void showDeleteAccountSet();
    void showBackupAccountSet();

    Ui::QtWidgetsApplication1Class ui;
    GlobalData *m_pGlobalData;

    QStackedWidget *m_pStackedWidget;
    FunctionSelectWidget *m_pFunctionSelectWidget;
    DeleteAccountSetWidget *m_pDeleteAccountSetWidget;
    BackupAccountSetWidget *m_pBackupAccountSetWidget;
};

