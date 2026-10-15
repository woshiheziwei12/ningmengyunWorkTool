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
#include <QTimer>
#include <QDir>
#include <QSaveFile>

PwdLoginWidget::PwdLoginWidget(GlobalData *globalData, QWidget *parent, const QString &credentialsDirectory) :
    QWidget(parent),
    ui(new Ui::PwdLoginWidget),
    m_pGlobalData(globalData),
    m_credentialsDirectory(credentialsDirectory.isEmpty() ? qApp->applicationDirPath() : credentialsDirectory)
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

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(30000);
    connect(m_refreshTimer, &QTimer::timeout, this, &PwdLoginWidget::checkSessionExpiry);
    setEnvironment(lemoncofighelper::CurrentHttpEnvironment());
}

PwdLoginWidget::~PwdLoginWidget()
{
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); }
    delete ui;
}

void PwdLoginWidget::on_btnForgetPwd_clicked()
{
    QDesktopServices::openUrl(QUrl(forgetPwdUrl));
}

void PwdLoginWidget::on_btnPwdLogin_clicked()
{
    if (m_busy) return;
    ui->labelPwdError->setText("");
    ui->labelPwdError->setVisible(false);
    QString strPwd = ui->editPwd->text();
    QString strUserName = ui->editUserName->text();
    if (strPwd.isEmpty() || strUserName.isEmpty())
    {
        failLogin(QStringLiteral("账号和密码不能为空"));
        return;
    }

    lemoncofighelper configHelper;
    QString strLoginUrl = configHelper.GetAccountUrlByKey("signInUrl");
    if (strLoginUrl.isEmpty())
    {
        failLogin(QStringLiteral("请求地址不能为空"));
        return;
    }

    ++m_loginSerial;
    if (!m_refreshing) resetSession();
    m_attemptStartedAt = QDateTime::currentDateTimeUtc();
    m_pendingExpiresAt = QDateTime();
    m_pendingSession.m_strNingMengCookie.clear();
    m_pendingSession.m_userToken.SetData(QString(), QString(), 0);
    m_pendingSession.m_nServiceId = 0;
    m_pendingSession.m_strAppAsId.clear();
    m_pendingSession.m_nUserId = 0;
    m_pendingSession.m_strAuthName.clear();
    setLoginBusy(true);

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
    watchReply(reply, &PwdLoginWidget::onLoginFinished);
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
        failLogin(QStringLiteral("登录失败，请检查用户名密码"));
        return;
    }

    QByteArray body = reply->readAll();
    reply->deleteLater();

    QJsonParseError jsonError;
    QJsonDocument document = QJsonDocument::fromJson(body, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !document.isObject())
    {
        failLogin(QStringLiteral("登录失败，响应格式错误"));
        return;
    }

    QJsonObject obj = document.object();
    QString accessToken = obj["access_token"].toString();
    if (accessToken.isEmpty())
    {
        failLogin(QStringLiteral("登录失败，未获取到认证信息"));
        return;
    }

    // access_token is the ningmengcookie
    m_pendingSession.m_strNingMengCookie = accessToken;

    if (obj.contains("refresh_token"))
        m_pendingSession.m_userToken.SetRefreshToken(obj["refresh_token"].toString());

    const int expiresIn = obj.value("expires_in").toVariant().toInt();
    if (expiresIn > 0) {
        m_pendingSession.m_userToken.SetExpirenIn(expiresIn);
        m_pendingExpiresAt = m_attemptStartedAt.addSecs(expiresIn);
    }

    // get JWT token for Bearer auth
    fetchJwtToken();
}

void PwdLoginWidget::fetchJwtToken()
{
    lemoncofighelper configHelper;
    QString authHost = configHelper.GetAccountUrlByKey("authHost");
    if (authHost.isEmpty())
    {
        failLogin(QStringLiteral("authHost 配置为空"));
        return;
    }

    QString jwtUrl = authHost + "/v2/api/jwt";
    QNetworkRequest request;
    request.setUrl(QUrl(jwtUrl));
    CHttpRequestHelper::configureSsl(request);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, "lemonClient");

    QString cookie = "ningmengcookie=" + m_pendingSession.m_strNingMengCookie;
    request.setRawHeader("Cookie", cookie.toUtf8());

    QByteArray postData = "{}";
    QNetworkReply *reply = CHttpRequestHelper::networkManager()->post(request, postData);
    CHttpRequestHelper::handleSslErrors(reply);
    watchReply(reply, &PwdLoginWidget::onJwtFinished);
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
        failLogin(QStringLiteral("获取JWT令牌失败"));
        return;
    }

    QByteArray body = reply->readAll();
    reply->deleteLater();

    QJsonParseError jsonError;
    QJsonDocument document = QJsonDocument::fromJson(body, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !document.isObject())
    {
        failLogin(QStringLiteral("JWT响应格式错误"));
        return;
    }

    QJsonObject obj = document.object();
    if (!obj["success"].toBool())
    {
        failLogin(QStringLiteral("获取JWT令牌失败"));
        return;
    }

    QString jwtToken = obj["token"].toString();
    if (jwtToken.isEmpty())
    {
        failLogin(QStringLiteral("JWT令牌为空"));
        return;
    }

    // JWT token for Bearer authorization
    m_pendingSession.m_userToken.SetAccessToken(jwtToken);

    // 从JWT payload中解析用户ID（sub字段）
    QStringList jwtParts = jwtToken.split('.');
    if (jwtParts.size() >= 2)
    {
        QByteArray payload = QByteArray::fromBase64(jwtParts[1].toUtf8(), QByteArray::Base64UrlEncoding);
        QJsonDocument payloadDoc = QJsonDocument::fromJson(payload);
        if (payloadDoc.isObject())
        {
            QJsonObject payloadObj = payloadDoc.object();
            const qint64 exp = payloadObj.value("exp").toVariant().toLongLong();
            if (exp > 0) {
                const auto jwtExpiresAt = QDateTime::fromSecsSinceEpoch(exp, Qt::UTC);
                if (jwtExpiresAt.isValid() && (!m_pendingExpiresAt.isValid() || jwtExpiresAt < m_pendingExpiresAt))
                    m_pendingExpiresAt = jwtExpiresAt;
            }
            QString sub = payloadObj["sub"].toString();
            if (!sub.isEmpty())
                m_pendingSession.m_nUserId = sub.toInt();

            QString authName = payloadObj.value("http://schemas.xmlsoap.org/ws/2005/05/identity/claims/name").toString();
            if (authName.isEmpty())
                authName = payloadObj.value("name").toString();
            if (!authName.isEmpty())
                m_pendingSession.m_strAuthName = authName;
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
        failLogin(QStringLiteral("wbgate地址配置为空"));
        return;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(wbgateUrl));
    CHttpRequestHelper::configureSsl(request);
    CHttpRequestHelper::setWbgateBrowserHeaders(request);

    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);

    QString cookie = "ningmengcookie=" + m_pendingSession.m_strNingMengCookie;
    request.setRawHeader("Cookie", cookie.toUtf8());

    QNetworkReply *reply = CHttpRequestHelper::networkManager()->get(request);
    CHttpRequestHelper::handleSslErrors(reply);
    watchReply(reply, &PwdLoginWidget::onFetchServiceParamsFinished);
}

void PwdLoginWidget::onFetchServiceParamsFinished(QNetworkReply *reply)
{
    int reqCode = 0;
    QVariant variant = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (variant.isValid())
        reqCode = variant.toInt();

    m_pendingSession.m_nServiceId = 0;
    m_pendingSession.m_strAppAsId.clear();

    if (reqCode == 302 || reqCode == 301)
    {
        QString location = QString::fromUtf8(reply->rawHeader("Location"));
        QUrl redirectUrl(location);
        QUrlQuery query(redirectUrl);

        QString serviceId = query.queryItemValue("serviceID");
        QString appAsId = query.queryItemValue("appasid");

        if (!serviceId.isEmpty())
            m_pendingSession.m_nServiceId = serviceId.toInt();
        if (!appAsId.isEmpty())
            m_pendingSession.m_strAppAsId = appAsId;

        QVariant ckVar = reply->header(QNetworkRequest::SetCookieHeader);
        QList<QNetworkCookie> ckList = ckVar.value<QList<QNetworkCookie>>();
        for (const QNetworkCookie &ck : ckList)
        {
            if (ck.name() == "service-id")
                m_pendingSession.m_nServiceId = QString::fromUtf8(ck.value()).toInt();
        }

        bool hasServiceParams = m_pendingSession.m_nServiceId != 0 && !m_pendingSession.m_strAppAsId.isEmpty();
        reply->deleteLater();

        if (!hasServiceParams)
        {
            qDebug() << QStringLiteral("wbgate未返回serviceId/appAsId，尝试从工作台套餐列表获取");
            fetchPlanServiceParams();
            return;
        }

        completeLogin();
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
        failLogin(QStringLiteral("wbgatePlanUrl 配置为空"));
        return;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(planUrl));
    CHttpRequestHelper::configureSsl(request);
    CHttpRequestHelper::setWbgateBrowserHeaders(request);
    // JWT for Bearer auth, ningmengcookie for Cookie
    request.setRawHeader("Authorization", ("Bearer " + m_pendingSession.m_userToken.m_strAccessToken).toUtf8());
    request.setRawHeader("Cookie", ("ningmengcookie=" + m_pendingSession.m_strNingMengCookie).toUtf8());

    QNetworkReply *reply = CHttpRequestHelper::networkManager()->get(request);
    CHttpRequestHelper::handleSslErrors(reply);
    watchReply(reply, &PwdLoginWidget::onFetchPlanFinished);
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
        failLogin(QStringLiteral("获取工作台账套信息失败"));
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
        if (m_pendingSession.m_nUserId != 0 && administrator == m_pendingSession.m_nUserId)
        {
            m_pendingSession.m_nServiceId = serviceId;
            m_pendingSession.m_strAppAsId = appAsId;
            completeLogin();
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
        m_pendingSession.m_nServiceId = fallbackServiceId;
        m_pendingSession.m_strAppAsId = fallbackAppAsId;
        completeLogin();
        return;
    }

    failLogin(QStringLiteral("未获取到账套服务编号，请确认账号是否有柠檬云财务账套"));
}

QString PwdLoginWidget::credentialsPath() const
{
    return QDir(m_credentialsDirectory).filePath("LemonApp." + m_environment + ".dat");
}

void PwdLoginWidget::saveCredentials()
{
    QDir().mkpath(m_credentialsDirectory);
    QSaveFile file(credentialsPath());
    if (!file.open(QIODevice::WriteOnly)) return;
    const bool remember = ui->checkBtnAutoPwd->isChecked();
    QJsonObject object{{"re", remember ? 1 : 0}, {"u", ui->editUserName->text().trimmed()},
                       {"p", remember ? ui->editPwd->text() : QString()}};
    file.write(QJsonDocument(object).toJson());
    file.commit();
}

void PwdLoginWidget::loadCredentials()
{
    ui->editUserName->clear();
    ui->editPwd->clear();
    ui->checkBtnAutoPwd->setChecked(false);
    QString path = credentialsPath();
    // Existing unscoped credentials belonged to the original production login.
    if (m_environment == "Pro" && !QFileInfo::exists(path))
        path = QDir(m_credentialsDirectory).filePath("LemonApp.dat");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    ui->editUserName->setText(object.value("u").toString());
    const bool remember = object.value("re").toInt() == 1;
    ui->checkBtnAutoPwd->setChecked(remember);
    if (remember) ui->editPwd->setText(object.value("p").toString());
}

void PwdLoginWidget::resetSession()
{
    m_pGlobalData->m_bLogin = false;
    m_pGlobalData->m_strNingMengCookie.clear();
    m_pGlobalData->m_userToken.SetAccessToken(QString());
    m_pGlobalData->m_userToken.SetRefreshToken(QString());
    m_pGlobalData->m_userToken.SetExpirenIn(0);
    m_pGlobalData->m_nServiceId = 0;
    m_pGlobalData->m_nUserId = 0;
    m_pGlobalData->m_strSN.clear();
    m_pGlobalData->m_strAppAsId.clear();
    m_pGlobalData->m_strAuthName.clear();
    m_pGlobalData->m_strUserName.clear();
}

void PwdLoginWidget::setEnvironment(const QString &environment)
{
    m_refreshTimer->stop();
    m_autoRefreshEnabled = false;
    m_refreshing = false;
    m_expiresAt = QDateTime();
    m_nextRefreshAt = QDateTime();
    ++m_loginSerial;
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply.clear();
    }
    setLoginBusy(false);
    resetSession();
    m_environment = environment.compare("Pro", Qt::CaseInsensitive) == 0 ? "Pro" : "Dev2";
    lemoncofighelper::SetHttpEnvironment(m_environment);
    lemoncofighelper config;
    forgetPwdUrl = config.GetAccountUrlByKey(lemoncofighelper::kForgotPasswordUrlKey);
    ui->labelPwdError->clear();
    ui->labelPwdError->hide();
    loadCredentials();
}

void PwdLoginWidget::setLoginBusy(bool busy)
{
    m_busy = busy;
    ui->btnPwdLogin->setEnabled(!busy);
    ui->editUserName->setEnabled(!busy);
    ui->editPwd->setEnabled(!busy);
    ui->checkBtnAutoPwd->setEnabled(!busy);
    ui->btnForgetPwd->setEnabled(!busy);
    emit loginBusyChanged(busy);
}

void PwdLoginWidget::failLogin(const QString &message)
{
    if (m_refreshing) {
        m_refreshing = false;
        setLoginBusy(false);
        m_nextRefreshAt = QDateTime::currentDateTimeUtc().addSecs(5 * 60);
        emit sessionStatusChanged(QStringLiteral("授权刷新失败，5 分钟后重试；可点立即刷新。") + message, true);
        return;
    }
    resetSession();
    setLoginBusy(false);
    ui->labelPwdError->setText(message);
    ui->labelPwdError->show();
}

void PwdLoginWidget::watchReply(QNetworkReply *reply, void (PwdLoginWidget::*handler)(QNetworkReply *))
{
    m_reply = reply;
    const int serial = m_loginSerial;
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    timeout->start(60000);
    connect(timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this, [this, reply, timeout, serial, handler]() {
        timeout->stop();
        if (serial != m_loginSerial) { reply->deleteLater(); return; }
        m_reply.clear();
        (this->*handler)(reply);
    });
}

bool PwdLoginWidget::hasSavedPassword() const
{
    return !ui->editUserName->text().trimmed().isEmpty() && !ui->editPwd->text().isEmpty();
}

void PwdLoginWidget::setAutoRefreshEnabled(bool enabled)
{
    m_autoRefreshEnabled = enabled;
    if (enabled) { m_refreshTimer->start(); checkSessionExpiry(); }
    else m_refreshTimer->stop();
}

void PwdLoginWidget::checkSessionExpiry()
{
    if (!m_autoRefreshEnabled || m_busy || !m_pGlobalData->m_bLogin) return;
    const auto now = QDateTime::currentDateTimeUtc();
    if (m_nextRefreshAt.isValid() && now >= m_nextRefreshAt) refreshSession();
    else if (m_expiresAt.isValid() && now >= m_expiresAt)
        emit sessionStatusChanged(QStringLiteral("授权已过期，正在等待重试；可点立即刷新。"), true);
}

void PwdLoginWidget::refreshSession()
{
    if (m_busy || !m_pGlobalData->m_bLogin) return;
    if (lemoncofighelper::CurrentHttpEnvironment() != m_environment) return;
    m_refreshing = true;
    emit sessionStatusChanged(QStringLiteral("正在刷新登录授权…"), false);
    on_btnPwdLogin_clicked();
}

void PwdLoginWidget::completeLogin()
{
    if (m_refreshing && m_pGlobalData->m_nUserId != 0
        && m_pendingSession.m_nUserId != m_pGlobalData->m_nUserId) {
        failLogin(QStringLiteral("刷新后的账号身份不一致，请重新登录"));
        return;
    }
    if (m_pendingExpiresAt.isValid() && m_pendingExpiresAt <= QDateTime::currentDateTimeUtc()) {
        failLogin(QStringLiteral("服务端返回的授权已过期"));
        return;
    }
    // Publish the complete session on the GUI thread; failures never expose partial credentials.
    m_pGlobalData->m_strNingMengCookie = m_pendingSession.m_strNingMengCookie;
    m_pGlobalData->m_userToken.SetData(m_pendingSession.m_userToken.m_strAccessToken,
        m_pendingSession.m_userToken.m_strRefreshToken, m_pendingSession.m_userToken.m_iExpireIn);
    m_pGlobalData->m_nUserId = m_pendingSession.m_nUserId;
    m_pGlobalData->m_strAuthName = m_pendingSession.m_strAuthName;
    m_pGlobalData->m_strUserName = ui->editUserName->text().trimmed();
    // Existing pages may have selected a different plan; renewal must preserve that selection.
    if (!m_refreshing) {
        m_pGlobalData->m_nServiceId = m_pendingSession.m_nServiceId;
        m_pGlobalData->m_strAppAsId = m_pendingSession.m_strAppAsId;
        saveCredentials();
    }
    m_pGlobalData->m_bLogin = true;
    m_expiresAt = m_pendingExpiresAt;
    m_nextRefreshAt = QDateTime();
    if (m_expiresAt.isValid()) {
        const qint64 lifetime = m_attemptStartedAt.secsTo(m_expiresAt);
        // Short-lived grants refresh halfway through their lifetime instead of spinning every tick.
        const qint64 lead = qMin<qint64>(3600, qMax<qint64>(1, lifetime / 2));
        m_nextRefreshAt = m_expiresAt.addSecs(-lead);
    }
    const bool renewed = m_refreshing;
    m_refreshing = false;
    setLoginBusy(false);
    emit sessionStatusChanged(sessionStatusMessage(), !m_nextRefreshAt.isValid());
    if (!renewed) emit login_success();
}

QString PwdLoginWidget::sessionStatusMessage() const
{
    if (!m_nextRefreshAt.isValid())
        return QStringLiteral("已登录，服务端未提供有效期，请手动刷新授权");
    return QStringLiteral("授权有效，%1 自动刷新")
        .arg(m_nextRefreshAt.toLocalTime().toString("MM-dd HH:mm"));
}
