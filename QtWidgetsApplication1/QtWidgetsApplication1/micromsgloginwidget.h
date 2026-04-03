#ifndef MICROMSGLOGINWIDGET_H
#define MICROMSGLOGINWIDGET_H

#include <QWidget>

class QNetworkReply;
class GlobalData;

enum WxLogUrlType {
    GetWxImgeUrl,
    GetWxImge,
    WxScanUrl,
    WxAuthCodeSign
};

namespace Ui {
class MicroMsgLoginWidget;
}

class MicroMsgLoginWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MicroMsgLoginWidget(GlobalData *globalData, QWidget *parent = nullptr);
    ~MicroMsgLoginWidget();

private slots:
    void on_AuthCodeSign();
    void on_GetWxQrCodeImage();
    void on_CheckScaned();

signals:
    void AuthCodeSign();
    void GetWxQrCodeImage();
    void login_success();

private:
    void RefreshWxQrCode();
    void StartTimer();
    void StopTimer();
    void onReplyFinished(QNetworkReply *reply, WxLogUrlType urlType);
    void fetchServiceParams();
    void onFetchServiceParamsFinished(QNetworkReply *reply);

protected:
    void timerEvent(QTimerEvent *event);
    int m_nTimerId;

private:
    Ui::MicroMsgLoginWidget *ui;
    GlobalData *m_pGlobalData;

    QString m_strWxQrTokenUrl;
    QString m_strWxQrImageUrl;
    QString m_strWxQrCheckScanedUrl;
    QString m_strAuthCodeLoginUrl;
    QString m_strSceneId;
    QString m_strWxQrImagePath;
    QString m_strAuthCode;
};

#endif // MICROMSGLOGINWIDGET_H
