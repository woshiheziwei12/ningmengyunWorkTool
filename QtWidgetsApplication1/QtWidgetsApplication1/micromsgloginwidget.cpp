#include "micromsgloginwidget.h"
#include "ui_micromsgloginwidget.h"
#include "httprequesthelper.h"
#include "lemoncofighelper.h"
#include "Common/globaldata.h"

#include <QDesktopServices>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QDir>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QNetworkCookie>

MicroMsgLoginWidget::MicroMsgLoginWidget(GlobalData *globalData, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MicroMsgLoginWidget),
    m_pGlobalData(globalData),
    m_nTimerId(0)
{
    ui->setupUi(this);
    ui->labelError->setVisible(false);

    QString appPath = qApp->applicationDirPath();
    QDir qdir(appPath + "/Tmp/");
    if (!qdir.exists())
    {
        qdir.mkdir(appPath + "/Tmp/");
    }
    m_strWxQrImagePath = appPath + "/Tmp/WxQr.png";

    if (!QSslSocket::supportsSsl()) {
        qDebug() << QStringLiteral("系统不支持ssl.");
    }

    connect(this, &MicroMsgLoginWidget::AuthCodeSign, this, &MicroMsgLoginWidget::on_AuthCodeSign);
    connect(this, &MicroMsgLoginWidget::GetWxQrCodeImage, this, &MicroMsgLoginWidget::on_GetWxQrCodeImage);

    RefreshWxQrCode();
}

MicroMsgLoginWidget::~MicroMsgLoginWidget()
{
    delete ui;
    if (m_nTimerId != 0)
    {
        killTimer(m_nTimerId);
        m_nTimerId = 0;
    }
}

void MicroMsgLoginWidget::timerEvent(QTimerEvent *)
{
    this->on_CheckScaned();
}

void MicroMsgLoginWidget::StartTimer()
{
    if (m_nTimerId != 0)
        StopTimer();
    if (!m_pGlobalData->m_bLogin)
        m_nTimerId = startTimer(3000);
}

void MicroMsgLoginWidget::StopTimer()
{
    if (m_nTimerId != 0)
    {
        killTimer(m_nTimerId);
        m_nTimerId = 0;
    }
}

void MicroMsgLoginWidget::RefreshWxQrCode()
{
    lemoncofighelper configHelper;

    if (m_strWxQrTokenUrl.isEmpty())
        m_strWxQrTokenUrl = configHelper.GetAccountUrlByKey("wxQrTokenUrl");
    if (m_strWxQrTokenUrl.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("请求地址不能为空"));
        return;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(m_strWxQrTokenUrl));
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 LemonImporter");
    CHttpRequestHelper::configureSsl(request);

    QNetworkReply *reply = CHttpRequestHelper::networkManager()->get(request);
    CHttpRequestHelper::handleSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReplyFinished(reply, GetWxImgeUrl);
    });
}

void MicroMsgLoginWidget::on_GetWxQrCodeImage()
{
    if (m_strSceneId.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("数据为空"));
    }
    lemoncofighelper configHelper;
    m_strWxQrImageUrl = configHelper.GetAccountUrlByKey("wxQrCodeUrl");
    m_strWxQrImageUrl = m_strWxQrImageUrl + "?sceneid=" + m_strSceneId;

    QNetworkRequest request;
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 LemonImporter");
    request.setUrl(QUrl(m_strWxQrImageUrl));
    CHttpRequestHelper::configureSsl(request);

    QNetworkReply *reply = CHttpRequestHelper::networkManager()->get(request);
    CHttpRequestHelper::handleSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReplyFinished(reply, GetWxImge);
    });
}

void MicroMsgLoginWidget::on_CheckScaned()
{
    lemoncofighelper configHelper;

    m_strWxQrCheckScanedUrl = configHelper.GetAccountUrlByKey("wxLoginScanedUrl");
    if (m_strWxQrTokenUrl.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("请求地址不能为空"));
        return;
    }
    m_strWxQrCheckScanedUrl = m_strWxQrCheckScanedUrl + "?sceneid=" + m_strSceneId;

    QNetworkRequest request;
    request.setUrl(QUrl(m_strWxQrCheckScanedUrl));
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 LemonImporter");
    CHttpRequestHelper::configureSsl(request);

    QNetworkReply *reply = CHttpRequestHelper::networkManager()->get(request);
    CHttpRequestHelper::handleSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReplyFinished(reply, WxScanUrl);
    });
}

void MicroMsgLoginWidget::on_AuthCodeSign()
{
    qDebug() << QStringLiteral("开始获取token。");

    lemoncofighelper configHelper;
    m_strAuthCodeLoginUrl = configHelper.GetAccountUrlByKey("signInUrl");
    m_strAuthCodeLoginUrl = m_strAuthCodeLoginUrl.trimmed();
    if (m_strAuthCodeLoginUrl.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("请求地址不能为空"));
        return;
    }

    QMap<QString, QString> reqParamers;
    reqParamers["grant_type"] = "authorization_code";
    reqParamers["code"] = m_strAuthCode;
    reqParamers["client_id"] = "123456";
    reqParamers["client_secret"] = "abcdef";
    reqParamers["redirect_uri"] = "";

    QNetworkRequest request;
    request.setUrl(QUrl(m_strAuthCodeLoginUrl));
    CHttpRequestHelper::configureSsl(request);
    CHttpRequestHelper::setFormHeaders(request);

    QByteArray bytes = CHttpRequestHelper::buildPostData(reqParamers).toUtf8();
    QNetworkReply *reply = CHttpRequestHelper::networkManager()->post(request, bytes);
    CHttpRequestHelper::handleSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReplyFinished(reply, WxAuthCodeSign);
    });
}

void MicroMsgLoginWidget::onReplyFinished(QNetworkReply *reply, WxLogUrlType urlType)
{
    QString strError = reply->errorString();
    if (strError != "Unknown error")
        qDebug() << QStringLiteral("请求失败。") + strError;

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
        if (urlType == GetWxImge)
        {
            QByteArray imgBytes = reply->readAll();
            QPixmap pixMap;
            pixMap.loadFromData(imgBytes);
            pixMap.scaled(300, 300, Qt::KeepAspectRatio);
            ui->labelErWeiMa->setPixmap(pixMap);

            this->StartTimer();
            reply->deleteLater();
            return;
        }

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
            if (keys.size() > 0)
            {
                if (urlType == GetWxImgeUrl)
                {
                    if (keys.contains("Data"))
                    {
                        QJsonObject jsonData = obj["Data"].toObject();
                        if (keys.contains("State") && obj["State"].toBool())
                        {
                            qDebug() << QStringLiteral("获取登录二维码图片地址成功。");
                            m_strSceneId = jsonData["SceneId"].toVariant().toString();
                            emit GetWxQrCodeImage();
                        }
                    }
                }

                if (urlType == WxScanUrl)
                {
                    this->StopTimer();
                    QString state = obj["state"].toString();
                    if (!state.isEmpty())
                    {
                        if (state == "timeout") {
                            this->StopTimer();
                        }
                        else if (state == "unbind")
                        {
                            ui->labelError->setVisible(true);
                            reply->deleteLater();
                            return;
                        }
                        else if (state == "false") {
                            this->StartTimer();
                        }
                        else
                        {
                            m_strAuthCode = state;
                            emit AuthCodeSign();
                        }
                    }
                }

                if (urlType == WxAuthCodeSign)
                {
                    if (keys.contains("access_token"))
                    {
                        m_pGlobalData->m_bLogin = true;
                        m_pGlobalData->m_userToken.SetAccessToken(obj["access_token"].toString());
                    }
                    if (keys.contains("refresh_token"))
                    {
                        m_pGlobalData->m_userToken.SetRefreshToken(obj["refresh_token"].toString());
                    }
                    if (keys.contains("expires_in"))
                    {
                        m_pGlobalData->m_userToken.SetExpirenIn(obj["expires_in"].toInt());
                    }

                    if (m_pGlobalData->m_bLogin)
                    {
                        m_pGlobalData->m_strNingMengCookie = m_pGlobalData->m_userToken.m_strAccessToken;
                        // 先获取JWT token，再动态获取 serviceId 和 appAsId
                        reply->deleteLater();
                        fetchJwtToken();
                        return;
                    }
                }
            }
        }
    }

    reply->deleteLater();
}

void MicroMsgLoginWidget::fetchJwtToken()
{
    lemoncofighelper configHelper;
    QString authHost = configHelper.GetAccountUrlByKey("authHost");
    if (authHost.isEmpty())
    {
        fetchServiceParams();
        return;
    }

    QString jwtUrl = authHost + "/v2/api/jwt";
    QNetworkRequest request;
    request.setUrl(QUrl(jwtUrl));
    CHttpRequestHelper::configureSsl(request);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, "lemonClient");

    QString cookie = "ningmengcookie=" + m_pGlobalData->m_strNingMengCookie;
    request.setRawHeader("Cookie", cookie.toUtf8());

    QByteArray postData = "{}";
    QNetworkReply *reply = CHttpRequestHelper::networkManager()->post(request, postData);
    CHttpRequestHelper::handleSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onJwtFinished(reply);
    });
}

void MicroMsgLoginWidget::onJwtFinished(QNetworkReply *reply)
{
    int reqCode = 0;
    QVariant variant = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (variant.isValid())
        reqCode = variant.toInt();

    if (reqCode != 200)
    {
        reply->deleteLater();
        fetchServiceParams();
        return;
    }

    QByteArray body = reply->readAll();
    reply->deleteLater();

    QJsonParseError jsonError;
    QJsonDocument document = QJsonDocument::fromJson(body, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !document.isObject())
    {
        fetchServiceParams();
        return;
    }

    QJsonObject obj = document.object();
    if (!obj["success"].toBool())
    {
        fetchServiceParams();
        return;
    }

    QString jwtToken = obj["token"].toString();
    if (jwtToken.isEmpty())
    {
        fetchServiceParams();
        return;
    }

    // JWT token for Bearer authorization
    m_pGlobalData->m_userToken.SetAccessToken(jwtToken);

    // 从JWT payload中解析用户ID（sub字段）
    QStringList jwtParts = jwtToken.split('.');
    if (jwtParts.size() >= 2)
    {
        QByteArray payload = QByteArray::fromBase64(jwtParts[1].toUtf8(), QByteArray::Base64UrlEncoding);
        QJsonDocument payloadDoc = QJsonDocument::fromJson(payload);
        if (payloadDoc.isObject())
        {
            QJsonObject payloadObj = payloadDoc.object();
            QString sub = payloadObj["sub"].toString();
            if (!sub.isEmpty())
                m_pGlobalData->m_nUserId = sub.toInt();

            QString authName = payloadObj.value("http://schemas.xmlsoap.org/ws/2005/05/identity/claims/name").toString();
            if (authName.isEmpty())
                authName = payloadObj.value("name").toString();
            if (!authName.isEmpty())
                m_pGlobalData->m_strAuthName = authName;
        }
    }

    fetchServiceParams();
}

void MicroMsgLoginWidget::fetchServiceParams()
{
    lemoncofighelper configHelper;
    QString wbgateUrl = configHelper.GetAccountUrlByKey("wbgateMenuUrl");
    if (wbgateUrl.isEmpty())
    {
        qDebug() << QStringLiteral("wbgate地址配置为空");
        emit login_success();
        return;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(wbgateUrl));
    CHttpRequestHelper::configureSsl(request);
    CHttpRequestHelper::setWbgateBrowserHeaders(request);
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

void MicroMsgLoginWidget::onFetchServiceParamsFinished(QNetworkReply *reply)
{
    int reqCode = 0;
    QVariant variant = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (variant.isValid())
        reqCode = variant.toInt();

    m_pGlobalData->m_nServiceId = 0;
    m_pGlobalData->m_strAppAsId.clear();

    bool hasServiceParams = false;
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
        hasServiceParams = m_pGlobalData->m_nServiceId != 0 && !m_pGlobalData->m_strAppAsId.isEmpty();
    }

    reply->deleteLater();
    if (!hasServiceParams)
    {
        fetchPlanServiceParams();
        return;
    }
    emit login_success();
}

void MicroMsgLoginWidget::fetchPlanServiceParams()
{
    lemoncofighelper configHelper;
    QString planUrl = configHelper.GetAccountUrlByKey(lemoncofighelper::kWbgatePlanUrlKey);
    if (planUrl.isEmpty())
        return;

    QNetworkRequest request;
    request.setUrl(QUrl(planUrl));
    CHttpRequestHelper::configureSsl(request);
    CHttpRequestHelper::setWbgateBrowserHeaders(request);
    request.setRawHeader("Authorization", ("Bearer " + m_pGlobalData->m_userToken.m_strAccessToken).toUtf8());
    request.setRawHeader("Cookie", ("ningmengcookie=" + m_pGlobalData->m_strNingMengCookie).toUtf8());

    QNetworkReply *reply = CHttpRequestHelper::networkManager()->get(request);
    CHttpRequestHelper::handleSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onFetchPlanFinished(reply);
    });
}

void MicroMsgLoginWidget::onFetchPlanFinished(QNetworkReply *reply)
{
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray body = reply->readAll();
    reply->deleteLater();

    QJsonParseError jsonError;
    QJsonDocument doc = QJsonDocument::fromJson(body, &jsonError);
    if (statusCode != 200 || jsonError.error != QJsonParseError::NoError || !doc.isObject())
        return;

    QJsonArray plans = doc.object()["data"].toObject()["data"].toArray();

    // 优先选择 administrator 匹配当前登录用户的 plan
    int fallbackServiceId = 0;
    QString fallbackAppAsId;

    for (const QJsonValue &value : plans)
    {
        QJsonObject plan = value.toObject();
        if (plan["productType"].toString() != "acc")
            continue;

        int serviceId = plan["id"].toInt();
        QString appAsId = plan["lastAccountSet"].toObject()["appAsid"].toString();
        if (serviceId == 0 || appAsId.isEmpty())
            continue;

        int administrator = plan["administrator"].toInt();
        if (m_pGlobalData->m_nUserId != 0 && administrator == m_pGlobalData->m_nUserId)
        {
            m_pGlobalData->m_nServiceId = serviceId;
            m_pGlobalData->m_strAppAsId = appAsId;
            emit login_success();
            return;
        }

        if (fallbackServiceId == 0)
        {
            fallbackServiceId = serviceId;
            fallbackAppAsId = appAsId;
        }
    }

    if (fallbackServiceId != 0 && !fallbackAppAsId.isEmpty())
    {
        m_pGlobalData->m_nServiceId = fallbackServiceId;
        m_pGlobalData->m_strAppAsId = fallbackAppAsId;
        emit login_success();
        return;
    }
}
