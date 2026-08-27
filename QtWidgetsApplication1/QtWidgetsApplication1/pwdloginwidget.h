#ifndef PWDLOGINFORM_H
#define PWDLOGINFORM_H

#include <QWidget>

class QNetworkReply;
class GlobalData;

namespace Ui {
class PwdLoginWidget;
}

class PwdLoginWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PwdLoginWidget(GlobalData *globalData, QWidget *parent = nullptr);
    ~PwdLoginWidget();

private slots:
    void on_btnForgetPwd_clicked();
    void on_btnPwdLogin_clicked();
    void onLoginFinished(QNetworkReply *reply);
    void onJwtFinished(QNetworkReply *reply);
    void onFetchServiceParamsFinished(QNetworkReply *reply);
    void onFetchPlanFinished(QNetworkReply *reply);

signals:
    void login_success();
    void tologin();

private:
    void fetchJwtToken();
    void fetchServiceParams();
    void fetchPlanServiceParams();
    void saveCredentials();

    Ui::PwdLoginWidget *ui;
    GlobalData *m_pGlobalData;
    QString forgetPwdUrl;
};

#endif // PWDLOGINFORM_H
