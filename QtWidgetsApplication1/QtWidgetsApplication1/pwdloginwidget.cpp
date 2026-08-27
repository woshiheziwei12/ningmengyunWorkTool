#include "pwdloginwidget.h"
#include "ui_pwdloginwidget.h"
#include "httprequesthelper.h"
#include "lemoncofighelper.h"
#include "Common/globaldata.h"

#include <QDesktopServices>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpressionValidator>
#include <QNetworkCookie>

PwdLoginWidget::PwdLoginWidget(GlobalData *globalData, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PwdLoginWidget),
    m_pGlobalData(globalData)
{
    ui->setupUi(this);
    ui->labelPwdError->setVisible(false);
    ui->labelRight->setVisible(false);

    QRegularExpression regx("[0-9]+$");
    QValidator *validator = new QRegularExpressionValidator(regx, this);
    ui->editUserName->setValidator(validator);

    lemoncofighelper configHelper;
    forgetPwdUrl = configHelper.GetAccountUrlByKey(lemoncofighelper::kForgotPasswordUrlKey);

    connect(this, &PwdLoginWidget::tologin, this, &PwdLoginWidget::on_btnPwdLogin_clicked);

    QString appPath = qApp->applicationDirPath();
    QString filePath = appPath + "/LemonApp.dat";
    QFile file(filePath);

    if (file.exists())
    {
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QByteArray bytes = file.readAll();
            QJsonDocument document = QJsonDocument::fromJson(bytes);
            if (document.isObject())
            {
                QJsonObject jsonObj = document.object();
                if (jsonObj.contains("re"))
                {
                    if (jsonObj["re"].toInt() == 1)
                    {
                        ui->checkBtnAutoPwd->setChecked(true);
                        ui->editPwd->setText(jsonObj["p"].toString());
                        ui->editUserName->setText(jsonObj["u"].toString());
                    }
                }
            }
            file.close();
        }
    }
}

PwdLoginWidget::~PwdLoginWidget()
{
    delete ui;
}

void PwdLoginWidget::on_btnForgetPwd_clicked()
{
    QDesktopServices::openUrl(QUrl(forgetPwdUrl));
}

void PwdLoginWidget::on_btnPwdLogin_clicked()
{
    ui->labelPwdError->setText("");
    ui->labelPwdError->setVisible(false);
    QString strPwd = ui->editPwd->text();
    QString strUserName = ui->editUserName->text();
    if (strPwd.isEmpty() || strUserName.isEmpty())
    {
        ui->labelPwdError->setText(QStringLiteral("密码不能为空"));
        ui->labelPwdError->setVisible(true);
        return;
    }

    lemoncofighelper configHelper;
    QString strLoginUrl = configHelper.GetAccountUrlByKey("signInUrl");
    if (strLoginUrl.isEmpty())
    {
        ui->labelPwdError->setText(QStringLiteral("请求地址不能为空"));
        ui->labelPwdError->setVisible(true);
        return;
    }

    QMap<QString, QString> reqParams;
    reqParams["client_id"] = "123456";
    reqParams["client_secret"] = "abcdef";
    reqParams["grant_type"] = "password";
    reqParams["username"] = strUserName;
    reqParams["password"] = strPwd;

    QNetworkRequest request;
    request.setUrl(QUrl(strLoginUrl.trimmed()));
    CHttpRequestHelper::configureSsl(request);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setHeader(QNetworkRequest::UserAgentHeader, "lemonClient");

    QString strPostData;
    for (auto it = reqParams.constBegin(); it != reqParams.constEnd(); ++it)
    {
        if (!strPostData.isEmpty())
            strPostData += '&';
        strPostData += it.key() + '=' + QUrl::toPercentEncoding(it.value());
    }

    QNetworkReply *reply = CHttpRequestHelper::networkManager()->post(request, strPostData.toUtf8());
    CHttpRequestHelper::handleSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onLoginFinished(reply);
    });
}

void PwdLoginWidget::onLoginFinished(QNetworkReply *reply)
{
    int reqCode = 0;
    QVariant variant = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (variant.isValid())
        reqCode = variant.toInt();

    if (reqCode != 200)
    {
        reply->deleteLater();
        ui->labelPwdError->setText(QStringLiteral("登录失败，请检查用户名密码"));
        ui->labelPwdError->setVisible(true);
        return;
    }

    QByteArray body = reply->readAll();
    reply->deleteLater();

    QJsonParseError jsonError;
    QJsonDocument document = QJsonDocument::fromJson(body, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !document.isObject())
    {
        ui->labelPwdError->setText(QStringLiteral("登录失败，响应格式错误"));
        ui->labelPwdError->setVisible(true);
        return;
    }

    QJsonObject obj = document.object();
    QString accessToken = obj["access_token"].toString();
    if (accessToken.isEmpty())
    {
        ui->labelPwdError->setText(QStringLiteral("登录失败，未获取到认证信息"));
        ui->labelPwdError->setVisible(true);
        return;
    }

    // access_token is the ningmengcookie
    m_pGlobalData->m_strNingMengCookie = accessToken;

    if (obj.contains("refresh_token"))
        m_pGlobalData->m_userToken.SetRefreshToken(obj["refresh_token"].toString());
    if (obj.contains("expires_in"))
        m_pGlobalData->m_userToken.SetExpirenIn(obj["expires_in"].toInt());

    // get JWT token for Bearer auth
    fetchJwtToken();
}

void PwdLoginWidget::fetchJwtToken()
{
    lemoncofighelper configHelper;
    QString authHost = configHelper.GetAccountUrlByKey("authHost");
    if (authHost.isEmpty())
    {
        ui->labelPwdError->setText(QStringLiteral("authHost 配置为空"));
        ui->labelPwdError->setVisible(true);
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

void PwdLoginWidget::onJwtFinished(QNetworkReply *reply)
{
    int reqCode = 0;
    QVariant variant = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (variant.isValid())
        reqCode = variant.toInt();

    if (reqCode != 200)
    {
        reply->deleteLater();
        ui->labelPwdError->setText(QStringLiteral("获取JWT令牌失败"));
        ui->labelPwdError->setVisible(true);
        return;
    }

    QByteArray body = reply->readAll();
    reply->deleteLater();

    QJsonParseError jsonError;
    QJsonDocument document = QJsonDocument::fromJson(body, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !document.isObject())
    {
        ui->labelPwdError->setText(QStringLiteral("JWT响应格式错误"));
        ui->labelPwdError->setVisible(true);
        return;
    }

    QJsonObject obj = document.object();
    if (!obj["success"].toBool())
    {
        ui->labelPwdError->setText(QStringLiteral("获取JWT令牌失败"));
        ui->labelPwdError->setVisible(true);
        return;
    }

    QString jwtToken = obj["token"].toString();
    if (jwtToken.isEmpty())
    {
        ui->labelPwdError->setText(QStringLiteral("JWT令牌为空"));
        ui->labelPwdError->setVisible(true);
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

void PwdLoginWidget::fetchServiceParams()
{
    lemoncofighelper configHelper;
    QString wbgateUrl = configHelper.GetAccountUrlByKey("wbgateMenuUrl");
    if (wbgateUrl.isEmpty())
    {
        ui->labelPwdError->setText(QStringLiteral("wbgate地址配置为空"));
        ui->labelPwdError->setVisible(true);
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

void PwdLoginWidget::onFetchServiceParamsFinished(QNetworkReply *reply)
{
    int reqCode = 0;
    QVariant variant = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (variant.isValid())
        reqCode = variant.toInt();

    m_pGlobalData->m_nServiceId = 0;
    m_pGlobalData->m_strAppAsId.clear();

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

        bool hasServiceParams = m_pGlobalData->m_nServiceId != 0 && !m_pGlobalData->m_strAppAsId.isEmpty();
        reply->deleteLater();

        if (!hasServiceParams)
        {
            qDebug() << QStringLiteral("wbgate未返回serviceId/appAsId，尝试从工作台套餐列表获取");
            fetchPlanServiceParams();
            return;
        }

        m_pGlobalData->m_bLogin = true;
        saveCredentials();
        emit login_success();
    }
    else
    {
        reply->deleteLater();
        qDebug() << QStringLiteral("wbgate未返回302，尝试从工作台套餐列表获取, HTTP:") << reqCode;
        fetchPlanServiceParams();
    }
}

void PwdLoginWidget::fetchPlanServiceParams()
{
    lemoncofighelper configHelper;
    QString planUrl = configHelper.GetAccountUrlByKey(lemoncofighelper::kWbgatePlanUrlKey);
    if (planUrl.isEmpty())
    {
        ui->labelPwdError->setText(QStringLiteral("wbgatePlanUrl 配置为空"));
        ui->labelPwdError->setVisible(true);
        return;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(planUrl));
    CHttpRequestHelper::configureSsl(request);
    CHttpRequestHelper::setWbgateBrowserHeaders(request);
    // JWT for Bearer auth, ningmengcookie for Cookie
    request.setRawHeader("Authorization", ("Bearer " + m_pGlobalData->m_userToken.m_strAccessToken).toUtf8());
    request.setRawHeader("Cookie", ("ningmengcookie=" + m_pGlobalData->m_strNingMengCookie).toUtf8());

    QNetworkReply *reply = CHttpRequestHelper::networkManager()->get(request);
    CHttpRequestHelper::handleSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onFetchPlanFinished(reply);
    });
}

void PwdLoginWidget::onFetchPlanFinished(QNetworkReply *reply)
{
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray body = reply->readAll();
    reply->deleteLater();

    QJsonParseError jsonError;
    QJsonDocument doc = QJsonDocument::fromJson(body, &jsonError);
    if (statusCode != 200 || jsonError.error != QJsonParseError::NoError || !doc.isObject())
    {
        ui->labelPwdError->setText(QStringLiteral("获取工作台账套信息失败"));
        ui->labelPwdError->setVisible(true);
        return;
    }

    QJsonArray plans = doc.object()["data"].toObject()["data"].toArray();

    // 优先选择 administrator 匹配当前登录用户 且 产品类型为柠檬云财务 的 plan
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
            m_pGlobalData->m_bLogin = true;
            saveCredentials();
            emit login_success();
            return;
        }

        // 记录第一个有效的作为 fallback
        if (fallbackServiceId == 0)
        {
            fallbackServiceId = serviceId;
            fallbackAppAsId = appAsId;
        }
    }

    // 没有找到 administrator 匹配的，使用 fallback
    if (fallbackServiceId != 0 && !fallbackAppAsId.isEmpty())
    {
        m_pGlobalData->m_nServiceId = fallbackServiceId;
        m_pGlobalData->m_strAppAsId = fallbackAppAsId;
        m_pGlobalData->m_bLogin = true;
        saveCredentials();
        emit login_success();
        return;
    }

    ui->labelPwdError->setText(QStringLiteral("未获取到账套服务编号，请确认账号是否有柠檬云财务账套"));
    ui->labelPwdError->setVisible(true);
}

void PwdLoginWidget::saveCredentials()
{
    QString appPath = qApp->applicationDirPath();
    QString filePath = appPath + "/LemonApp.dat";
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QJsonObject jsonObj;
        jsonObj.insert("re", ui->checkBtnAutoPwd->checkState() == Qt::Checked ? 1 : 0);
        jsonObj.insert("u", ui->editUserName->text().trimmed());
        jsonObj.insert("p", ui->editPwd->text().trimmed());
        QJsonDocument document;
        document.setObject(jsonObj);
        file.write(document.toJson());
        file.close();
    }
}
