#ifndef SMSLOGINFORM_H
#define SMSLOGINFORM_H

#include <QWidget>

class QNetworkReply;
class GlobalData;

enum SmsLoginReqEnum
{
    SmsSendReq,
    SmsLoginReq,
    SmsAuthCodeSign
};

namespace Ui {
class SmsLoginWidget;
}

class SmsLoginWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SmsLoginWidget(GlobalData *globalData, QWidget *parent = nullptr);
    ~SmsLoginWidget();

private slots:
    void on_btnSmsLogin_clicked();
    void on_btnSendSms_clicked();
    void on_AuthCodeSign_Reveive(const QString &authCode);

protected:
    void timerEvent(QTimerEvent *event);

signals:
    void AuthCodeSign(const QString &authCode);
    void login_success();
    void tologin();

private:
    void sendSmsRequest();
    void onReplyFinished(QNetworkReply *reply, SmsLoginReqEnum reqType);
    void fetchJwtToken();
    void onJwtFinished(QNetworkReply *reply);
    void fetchServiceParams();
    void onFetchServiceParamsFinished(QNetworkReply *reply);
    void fetchPlanServiceParams();
    void onFetchPlanFinished(QNetworkReply *reply);

    Ui::SmsLoginWidget *ui;
    GlobalData *m_pGlobalData;

    QString m_strStamp;
    QString m_strTimeStamp;
    QString m_strAuthCode;
    QString m_strSendSmsUrl;
    QString m_strLoginUrl;

    int m_nTimer;
    int m_iSecond;
};

#endif // SMSLOGINFORM_H
