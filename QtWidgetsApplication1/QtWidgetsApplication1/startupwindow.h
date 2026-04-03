#ifndef STARTUPWINDOW_H
#define STARTUPWINDOW_H

#include <QDialog>
#include <QLabel>

#include "Common/globaldata.h"

class PwdLoginWidget;
class SmsLoginWidget;
class MicroMsgLoginWidget;

enum LoginMothed
{
    WeiXinSaoMa,
    UserAndName,
    PhoneAndSms
};

namespace Ui {
class StartUpWindow;
}

class StartUpWindow : public QDialog
{
    Q_OBJECT

public:
    explicit StartUpWindow(GlobalData* globalData, QWidget *parent = nullptr);
    ~StartUpWindow();

protected:
    bool eventFilter(QObject *obj, QEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void keyPressEvent(QKeyEvent *event);
    void closeEvent(QCloseEvent *event);

private slots:
    void on_btnMin_clicked();
    void on_btnExit_clicked();
    void on_btnPwdMode_clicked();
    void on_btnSmsMode_clicked();
    void on_btnMicroMsgMode_clicked();
    void on_btnRegistor_clicked();
    void on_login_success();

private:
    void changeLoginWidget(QWidget *pLoginWidget);

    Ui::StartUpWindow *ui;

    bool mDrag;
    QPoint mDragPos;
    QRect mLocation;

    MicroMsgLoginWidget *m_pMicroMsgImageWidget;
    PwdLoginWidget *m_pPwdLoginForm;
    SmsLoginWidget *m_pSmsLoginForm;

    QString registerUrl;

public:
    GlobalData* m_pGlobalData;
    LoginMothed m_loginMothed;
};

#endif // STARTUPWINDOW_H
