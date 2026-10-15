#ifndef PWDLOGINFORM_H
#define PWDLOGINFORM_H

#include <QWidget>
#include <QPointer>
#include <QDateTime>
#include "Common/globaldata.h"

class QNetworkReply;
class QTimer;
class GlobalData;

namespace Ui {
class PwdLoginWidget;
}

class PwdLoginWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PwdLoginWidget(GlobalData *globalData, QWidget *parent = nullptr,
                            const QString &credentialsDirectory = QString());
    ~PwdLoginWidget();
    void setEnvironment(const QString &environment);
    void setAutoRefreshEnabled(bool enabled);
    bool isBusy() const { return m_busy; }
    bool hasSavedPassword() const;
    QString sessionStatusMessage() const;
    QDateTime nextRefreshAt() const { return m_nextRefreshAt; }
    QDateTime sessionExpiresAt() const { return m_expiresAt; }

public slots:
    void refreshSession();
    void checkSessionExpiry();

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
    void loginBusyChanged(bool busy);
    void sessionStatusChanged(const QString &message, bool warning);

private:
    void fetchJwtToken();
    void completeLogin();
    void fetchServiceParams();
    void fetchPlanServiceParams();
    void saveCredentials();
    void loadCredentials();
    QString credentialsPath() const;
    void resetSession();
    void setLoginBusy(bool busy);
    void failLogin(const QString &message);
    void watchReply(QNetworkReply *reply, void (PwdLoginWidget::*handler)(QNetworkReply *));

    Ui::PwdLoginWidget *ui;
    GlobalData *m_pGlobalData;
    GlobalData m_pendingSession;
    QTimer *m_refreshTimer = nullptr;
    QDateTime m_attemptStartedAt, m_pendingExpiresAt, m_expiresAt, m_nextRefreshAt;
    bool m_refreshing = false;
    bool m_autoRefreshEnabled = false;
    QString forgetPwdUrl;
    QString m_environment, m_credentialsDirectory;
    QPointer<QNetworkReply> m_reply;
    int m_loginSerial = 0;
    bool m_busy = false;
};

#endif // PWDLOGINFORM_H
