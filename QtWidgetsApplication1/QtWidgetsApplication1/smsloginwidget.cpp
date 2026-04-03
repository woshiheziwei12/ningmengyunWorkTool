#include "smsloginwidget.h"
#include "ui_smsloginwidget.h"
#include "httprequesthelper.h"
#include "lemoncofighelper.h"
#include "Common/globaldata.h"

#include <QDesktopServices>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QRegularExpressionValidator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QMessageBox>
#include <QUrlQuery>
#include <QNetworkCookie>

SmsLoginWidget::SmsLoginWidget(GlobalData *globalData, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SmsLoginWidget),
    m_pGlobalData(globalData),
    m_nTimer(0),
    m_iSecond(59)
{
    ui->setupUi(this);
    ui->labelLoginError->setText("");
    ui->labelLoginError->setVisible(false);
    ui->labelRight->setVisible(false);

    QRegularExpression regx("[0-9]+$");
    QValidator *validator = new QRegularExpressionValidator(regx, this);
    ui->editPhone->setValidator(validator);

    connect(this, &SmsLoginWidget::tologin, this, &SmsLoginWidget::on_btnSmsLogin_clicked);
    connect(this, &SmsLoginWidget::AuthCodeSign, this, &SmsLoginWidget::on_AuthCodeSign_Reveive);
}

SmsLoginWidget::~SmsLoginWidget()
{
    delete ui;
}

void SmsLoginWidget::on_btnSendSms_clicked()
{
    QString strPhone = ui->editPhone->text();
    if (strPhone.isEmpty())
    {
        ui->labelLoginError->setText(QStringLiteral("手机号不能为空"));
        ui->labelLoginError->setVisible(true);
        return;
    }

    ui->labelLoginError->setText("");
    ui->labelLoginError->setVisible(false);
    sendSmsRequest();
}

void SmsLoginWidget::sendSmsRequest()
{
    QString strPhone = ui->editPhone->text();

    lemoncofighelper configHelper;
    m_strSendSmsUrl = configHelper.GetAccountUrlByKey("registerVerifyCodeUrl");
    m_strSendSmsUrl = m_strSendSmsUrl.trimmed();
    if (m_strSendSmsUrl.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("请求地址不能为空"));
        return;
    }

    QMap<QString, QString> reqParamers;
    reqParamers["Mobile"] = strPhone;
    reqParamers["ClientId"] = "123456";
    reqParamers["ClientType"] = "1017";
    reqParamers["IsVms"] = "false";

    QNetworkRequest request;
    request.setUrl(QUrl(m_strSendSmsUrl));
    CHttpRequestHelper::configureSsl(request);
    CHttpRequestHelper::setFormHeaders(request);

    QByteArray bytes = CHttpRequestHelper::buildPostData(reqParamers).toUtf8();
    QNetworkReply *reply = CHttpRequestHelper::networkManager()->post(request, bytes);
    CHttpRequestHelper::handleSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReplyFinished(reply, SmsSendReq);
    });

    m_iSecond = 59;
    m_nTimer = startTimer(1000);
    ui->btnSendSms->setEnabled(false);
}

void SmsLoginWidget::timerEvent(QTimerEvent *)
{
    m_iSecond--;
    if (m_iSecond == 0)
    {
        if (m_nTimer != 0)
        {
            killTimer(m_nTimer);
            m_nTimer = 0;
        }
        ui->btnSendSms->setText(QStringLiteral("发送验证码"));
        ui->btnSendSms->setEnabled(true);
    }
    else
    {
        ui->btnSendSms->setText(QString(QStringLiteral("重新发送(%1)")).arg(m_iSecond));
        ui->btnSendSms->setEnabled(false);
    }
}

void SmsLoginWidget::on_btnSmsLogin_clicked()
{
    QString strPhone = ui->editPhone->text();
    QString strSmsNo = ui->editSmsNo->text();

    if (strPhone.isEmpty())
    {
        ui->labelLoginError->setText(QStringLiteral("手机号不能为空"));
        ui->labelLoginError->setVisible(true);
        return;
    }

    if (strSmsNo.isEmpty())
    {
        ui->labelLoginError->setText(QStringLiteral("验证码不能为空"));
        ui->labelLoginError->setVisible(true);
        return;
    }

    if (m_strStamp.isEmpty() || m_strTimeStamp.isEmpty())
    {
        ui->labelLoginError->setText(QStringLiteral("验证码输入错误"));
        ui->labelLoginError->setVisible(true);
        return;
    }

    lemoncofighelper configHelper;
    m_strLoginUrl = configHelper.GetAccountUrlByKey("verifyCodeSignInUrl");
    m_strLoginUrl = m_strLoginUrl.trimmed();
    if (m_strLoginUrl.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("请求地址不能为空"));
        return;
    }

    QMap<QString, QString> reqParamers;
    reqParamers["Mobile"] = strPhone;
    reqParamers["ClientId"] = "123456";
    reqParamers["ClientSecret"] = "123456";
    reqParamers["VerifyCode"] = strSmsNo;
    reqParamers["TimeStamp"] = m_strTimeStamp;
    reqParamers["Stamp"] = m_strStamp;
    reqParamers["RedirectUrl"] = "https://j3.ningmengyun.com";

    QNetworkRequest request;
    request.setUrl(QUrl(m_strLoginUrl));
    CHttpRequestHelper::configureSsl(request);
    CHttpRequestHelper::setFormHeaders(request);

    // 注意：原始代码对 value 做了 toPercentEncoding，在 buildPostData 之外单独处理
    QString strPostData;
    for (auto it = reqParamers.constBegin(); it != reqParamers.constEnd(); ++it)
    {
        if (!strPostData.isEmpty())
            strPostData += '&';
        strPostData += it.key() + '=' + it.value().toUtf8().toPercentEncoding();
    }
    QByteArray bytes = strPostData.toUtf8();

    QNetworkReply *reply = CHttpRequestHelper::networkManager()->post(request, bytes);
    CHttpRequestHelper::handleSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReplyFinished(reply, SmsLoginReq);
    });
}

void SmsLoginWidget::on_AuthCodeSign_Reveive(const QString &authCode)
{
    ui->labelLoginError->setText("");
    ui->labelLoginError->setVisible(false);
    if (authCode.isEmpty())
    {
        ui->labelLoginError->setText(QStringLiteral("验证码输入错误"));
        ui->labelLoginError->setVisible(true);
        return;
    }

    lemoncofighelper configHelper;
    m_strLoginUrl = configHelper.GetAccountUrlByKey("signInUrl");
    m_strLoginUrl = m_strLoginUrl.trimmed();
    if (m_strLoginUrl.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("请求地址不能为空"));
        return;
    }

    QMap<QString, QString> reqParamers;
    reqParamers["grant_type"] = "authorization_code";
    reqParamers["code"] = authCode;
    reqParamers["client_id"] = "123456";
    reqParamers["client_secret"] = "abcdef";
    reqParamers["redirect_uri"] = "";

    QNetworkRequest request;
    request.setUrl(QUrl(m_strLoginUrl));
    CHttpRequestHelper::configureSsl(request);
    CHttpRequestHelper::setFormHeaders(request);

    QByteArray bytes = CHttpRequestHelper::buildPostData(reqParamers).toUtf8();
    QNetworkReply *reply = CHttpRequestHelper::networkManager()->post(request, bytes);
    CHttpRequestHelper::handleSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReplyFinished(reply, SmsAuthCodeSign);
    });
}

void SmsLoginWidget::onReplyFinished(QNetworkReply *reply, SmsLoginReqEnum reqType)
{
    QString strError = reply->errorString();
    if (strError != "Unknown error")
        qDebug() << QStringLiteral("短信登录失败。") + strError;

    int reqCode = 0;
    QVariant variant = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (variant.isValid())
    {
        reqCode = variant.toInt();
    }

    if (reqCode == 400)
    {
        reply->deleteLater();
        return;
    }

    if (reqCode == 200)
    {
        QByteArray bytes = reply->readAll();
        QJsonParseError jsonError;
        QJsonDocument document = QJsonDocument::fromJson(bytes, &jsonError);
        if (jsonError.error != QJsonParseError::NoError) {
            reply->deleteLater();
            return;
        }

        if (document.isObject())
        {
            QJsonObject obj = document.object();
            QStringList keys = obj.keys();
            if (reqType == SmsLoginReq)
            {
                m_strAuthCode = obj["AuthCode"].toString();
                emit AuthCodeSign(m_strAuthCode);
            }
            else if (reqType == SmsAuthCodeSign)
            {
                if (obj.contains("access_token"))
                {
                    m_pGlobalData->m_bLogin = true;
                    m_pGlobalData->m_userToken.SetAccessToken(obj["access_token"].toString());
                }
                if (obj.contains("refresh_token"))
                {
                    m_pGlobalData->m_userToken.SetRefreshToken(obj["refresh_token"].toString());
                }
                if (obj.contains("expires_in"))
                {
                    m_pGlobalData->m_userToken.SetExpirenIn(obj["expires_in"].toInt());
                }
            }
            else if (keys.contains("Result"))
            {
                if (obj["Result"].toBool())
                {
                    QJsonObject data = obj["Data"].toObject();
                    if (data.contains("Stamp"))
                        m_strStamp = data["Stamp"].toString();
                    if (data.contains("TimeStamp"))
                        m_strTimeStamp = data["TimeStamp"].toString();

                    if (obj.contains("AuthCode"))
                    {
                        m_strAuthCode = obj["AuthCode"].toString();
                        emit AuthCodeSign(m_strAuthCode);
                    }
                }
            }
        }
    }

    if (reqType == SmsAuthCodeSign)
    {
        if (m_pGlobalData->m_bLogin)
        {
            m_pGlobalData->m_strNingMengCookie = m_pGlobalData->m_userToken.m_strAccessToken;
            // 动态获取 serviceId 和 appAsId
            fetchServiceParams();
        }
        else {
            ui->labelLoginError->setText(QStringLiteral("登录失败"));
            ui->labelLoginError->setVisible(true);
        }
    }

    reply->deleteLater();
}

void SmsLoginWidget::fetchServiceParams()
{
    lemoncofighelper configHelper;
    QString wbgateUrl = configHelper.GetAccountUrlByKey("wbgateMenuUrl");
    if (wbgateUrl.isEmpty())
    {
        ui->labelLoginError->setText(QStringLiteral("wbgate地址配置为空"));
        ui->labelLoginError->setVisible(true);
        return;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(wbgateUrl));
    CHttpRequestHelper::configureSsl(request);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    QString cookie = "ningmengcookie=" + m_pGlobalData->m_strNingMengCookie;
    request.setRawHeader("Cookie", cookie.toUtf8());

    QNetworkReply *reply = CHttpRequestHelper::networkManager()->get(request);
    CHttpRequestHelper::handleSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onFetchServiceParamsFinished(reply);
    });
}

void SmsLoginWidget::onFetchServiceParamsFinished(QNetworkReply *reply)
{
    int reqCode = 0;
    QVariant variant = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (variant.isValid())
        reqCode = variant.toInt();

    if (reqCode == 302 || reqCode == 301)
    {
        QString location = QString::fromUtf8(reply->rawHeader("Location"));
        QUrl redirectUrl(location);
        QUrlQuery query(redirectUrl);

        QString serviceId = query.queryItemValue("serviceID");
        QString appAsId = query.queryItemValue("appasid");

        if (!serviceId.isEmpty())
            m_pGlobalData->m_nServiceId = serviceId.toInt();
        if (!appAsId.isEmpty())
            m_pGlobalData->m_strAppAsId = appAsId;

        QVariant ckVar = reply->header(QNetworkRequest::SetCookieHeader);
        QList<QNetworkCookie> ckList = ckVar.value<QList<QNetworkCookie>>();
        for (const QNetworkCookie &ck : ckList)
        {
            if (ck.name() == "service-id")
                m_pGlobalData->m_nServiceId = QString::fromUtf8(ck.value()).toInt();
        }
    }

    reply->deleteLater();
    emit login_success();
}