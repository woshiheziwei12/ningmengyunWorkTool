#ifndef STARTUPWINDOW_H
#define STARTUPWINDOW_H

#include <QDialog>
#include <QLabel>

#include "Common/globaldata.h"

class PwdLoginWidget;
namespace Ui {
class StartUpWindow;
}

class StartUpWindow : public QDialog
{
    Q_OBJECT

public:
    explicit StartUpWindow(GlobalData* globalData, QWidget *parent = nullptr, const QString &credentialsDirectory = QString());
    ~StartUpWindow();
    PwdLoginWidget *sessionLogin() const { return m_pPwdLoginForm; }
    void prepareEnvironmentSwitch();

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
    void on_btnRegistor_clicked();
    void on_login_success();

private:

    Ui::StartUpWindow *ui;

    bool mDrag;
    QPoint mDragPos;
    QRect mLocation;

    PwdLoginWidget *m_pPwdLoginForm;
    bool m_environmentLocked = false;


public:
    GlobalData* m_pGlobalData;
};

#endif // STARTUPWINDOW_H
