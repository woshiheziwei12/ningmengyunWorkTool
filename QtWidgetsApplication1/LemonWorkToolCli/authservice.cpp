#include "authservice.h"

#include "cliresult.h"
#include "networkutils.h"
#include "lemoncofighelper.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QNetworkReply>
#include <QUrl>

namespace
{
QJsonObject httpDetails(const HttpResponse &response)
{
    QJsonObject details;
    details["httpStatus"] = response.httpStatus;
    details["networkError"] = response.networkError;
    details["timedOut"] = response.timedOut;
    if (!response.errorString.isEmpty())
        details["networkErrorMessage"] = response.errorString;
    return details;
}

QString bearer(const QString &token)
{
    return QStringLiteral("Bearer ") + token;
}
}

AuthService::AuthService(int timeoutMs)
    : m_timeoutMs(timeoutMs)
{
}

ServiceResult AuthService::loginPassword(const QString &username, const QString &password, SessionInfo *session) const
{
    ServiceResult result;
    if (!session)
    {
        result.errorCode = "INTERNAL_ERROR";
        result.errorMessage = QStringLiteral("session 参数为空");
        result.exitCode = ExitUnknownError;
        return result;
    }
    if (username.trimmed().isEmpty() || password.isEmpty())
    {
        result.errorCode = "ARGUMENT_ERROR";
        result.errorMessage = QStringLiteral("username 和 password 不能为空");
        result.exitCode = ExitArgumentError;
        return result;
    }

    lemoncofighelper configHelper;
    const QString signInUrl = configHelper.GetAccountUrlByKey("signInUrl").trimmed();
    if (signInUrl.isEmpty())
    {
        result.errorCode = "CONFIG_ERROR";
        result.errorMessage = QStringLiteral("signInUrl 未配置");
        result.exitCode = ExitArgumentError;
        return result;
    }

    QMap<QString, QString> params;
    params["client_id"] = "123456";
    params["client_secret"] = "abcdef";
    params["grant_type"] = "password";
    params["username"] = username.trimmed();
    params["password"] = password;

    HttpRequestSpec request;
    request.method = "POST";
    request.url = QUrl(signInUrl);
    request.timeoutMs = m_timeoutMs;
    request.body = buildFormBody(params);
    request.rawHeaders.append(qMakePair(QByteArray("Content-Type"), QByteArray("application/x-www-form-urlencoded")));
    request.rawHeaders.append(qMakePair(QByteArray("User-Agent"), QByteArray("lemonClient")));

    const HttpResponse response = sendHttpRequest(request);
    if (response.timedOut)
        return makeNetworkError("auth.login-password", QStringLiteral("登录请求超时"), httpDetails(response));
    if (response.networkError != QNetworkReply::NoError)
        return makeNetworkError("auth.login-password", QStringLiteral("登录网络错误：") + response.errorString, httpDetails(response));
    if (response.httpStatus != 200)
    {
        result.errorCode = "AUTH_FAILED";
        result.errorMessage = QStringLiteral("登录失败，请检查用户名密码");
        result.exitCode = ExitAuthError;
        result.details = httpDetails(response);
        return result;
    }

    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(response.body, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject())
    {
        result.errorCode = "DATA_PARSE_ERROR";
        result.errorMessage = QStringLiteral("登录响应 JSON 解析失败：") + jsonError.errorString();
        result.exitCode = ExitDataError;
        result.details = httpDetails(response);
        return result;
    }

    const QJsonObject obj = doc.object();
    const QString accessToken = obj.value("access_token").toString();
    if (accessToken.isEmpty())
    {
        result.errorCode = "AUTH_FAILED";
        result.errorMessage = QStringLiteral("登录失败，未获取到 access_token");
        result.exitCode = ExitAuthError;
        result.details = httpDetails(response);
        return result;
    }

    session->ningmengCookie = accessToken;
    session->refreshToken = obj.value("refresh_token").toString();
    session->expiresIn = obj.value("expires_in").toInt();
    return fetchJwt(session);
}

ServiceResult AuthService::sendSms(const QString &mobile) const
{
    ServiceResult result;
    if (mobile.trimmed().isEmpty())
    {
        result.errorCode = "ARGUMENT_ERROR";
        result.errorMessage = QStringLiteral("mobile 不能为空");
        result.exitCode = ExitArgumentError;
        return result;
    }

    lemoncofighelper configHelper;
    const QString smsUrl = configHelper.GetAccountUrlByKey("registerVerifyCodeUrl").trimmed();
    if (smsUrl.isEmpty())
    {
        result.errorCode = "CONFIG_ERROR";
        result.errorMessage = QStringLiteral("registerVerifyCodeUrl 未配置");
        result.exitCode = ExitArgumentError;
        return result;
    }

    QMap<QString, QString> params;
    params["Mobile"] = mobile.trimmed();
    params["ClientId"] = "123456";
    params["ClientType"] = "1017";
    params["IsVms"] = "false";

    HttpRequestSpec request;
    request.method = "POST";
    request.url = QUrl(smsUrl);
    request.timeoutMs = m_timeoutMs;
    request.body = buildFormBody(params);
    request.rawHeaders.append(qMakePair(QByteArray("Content-Type"), QByteArray("application/x-www-form-urlencoded")));
    request.rawHeaders.append(qMakePair(QByteArray("User-Agent"), QByteArray("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36")));

    const HttpResponse response = sendHttpRequest(request);
    if (response.timedOut)
        return makeNetworkError("auth.sms-send", QStringLiteral("发送短信验证码超时"), httpDetails(response));
    if (response.networkError != QNetworkReply::NoError)
        return makeNetworkError("auth.sms-send", QStringLiteral("发送短信验证码网络错误：") + response.errorString, httpDetails(response));
    if (response.httpStatus != 200)
    {
        result.errorCode = "SERVER_ERROR";
        result.errorMessage = QStringLiteral("发送短信验证码失败");
        result.exitCode = ExitServerError;
        result.details = httpDetails(response);
        return result;
    }

    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(response.body, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject())
    {
        result.errorCode = "DATA_PARSE_ERROR";
        result.errorMessage = QStringLiteral("短信验证码响应 JSON 解析失败：") + jsonError.errorString();
        result.exitCode = ExitDataError;
        result.details = httpDetails(response);
        return result;
    }

    const QJsonObject obj = doc.object();
    if (obj.contains("Result") && !obj.value("Result").toBool())
    {
        result.errorCode = "SERVER_ERROR";
        result.errorMessage = obj.value("Message").toString(QStringLiteral("发送短信验证码失败"));
        result.exitCode = ExitServerError;
        result.details = httpDetails(response);
        return result;
    }

    QJsonObject data;
    const QJsonObject dataObj = obj.value("Data").toObject();
    data["mobileMasked"] = mobile.left(3) + "****" + mobile.right(4);
    data["stamp"] = dataObj.value("Stamp").toString();
    data["timeStamp"] = dataObj.value("TimeStamp").toString();
    data["authCode"] = obj.value("AuthCode").toString();
    data["expiresInSeconds"] = 59;

    result.ok = true;
    result.data = data;
    return result;
}

ServiceResult AuthService::loginSms(const QString &mobile, const QString &code, const QString &stamp,
                                    const QString &timestamp, SessionInfo *session) const
{
    ServiceResult result;
    if (!session)
    {
        result.errorCode = "INTERNAL_ERROR";
        result.errorMessage = QStringLiteral("session 参数为空");
        result.exitCode = ExitUnknownError;
        return result;
    }
    if (mobile.trimmed().isEmpty() || code.trimmed().isEmpty() || stamp.trimmed().isEmpty() || timestamp.trimmed().isEmpty())
    {
        result.errorCode = "ARGUMENT_ERROR";
        result.errorMessage = QStringLiteral("mobile、code、stamp、timestamp 均不能为空");
        result.exitCode = ExitArgumentError;
        return result;
    }

    lemoncofighelper configHelper;
    const QString loginUrl = configHelper.GetAccountUrlByKey("verifyCodeSignInUrl").trimmed();
    if (loginUrl.isEmpty())
    {
        result.errorCode = "CONFIG_ERROR";
        result.errorMessage = QStringLiteral("verifyCodeSignInUrl 未配置");
        result.exitCode = ExitArgumentError;
        return result;
    }

    QMap<QString, QString> params;
    params["Mobile"] = mobile.trimmed();
    params["ClientId"] = "123456";
    params["ClientSecret"] = "123456";
    params["VerifyCode"] = code.trimmed();
    params["TimeStamp"] = timestamp.trimmed();
    params["Stamp"] = stamp.trimmed();
    QString redirectUrl = configHelper.GetAccountUrlByKey(lemoncofighelper::kSmsRedirectUrlKey);
    if (redirectUrl.isEmpty())
        redirectUrl = configHelper.GetAccountUrlByKey("jproCallbackUrl");
    if (redirectUrl.isEmpty())
    {
        result.errorCode = "CONFIG_ERROR";
        result.errorMessage = QStringLiteral("smsRedirectUrl 鏈厤缃?");
        result.exitCode = ExitArgumentError;
        return result;
    }
    params["RedirectUrl"] = redirectUrl;

    HttpRequestSpec request;
    request.method = "POST";
    request.url = QUrl(loginUrl);
    request.timeoutMs = m_timeoutMs;
    request.body = buildFormBody(params);
    request.rawHeaders.append(qMakePair(QByteArray("Content-Type"), QByteArray("application/x-www-form-urlencoded")));
    request.rawHeaders.append(qMakePair(QByteArray("User-Agent"), QByteArray("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36")));

    const HttpResponse response = sendHttpRequest(request);
    if (response.timedOut)
        return makeNetworkError("auth.login-sms", QStringLiteral("短信登录超时"), httpDetails(response));
    if (response.networkError != QNetworkReply::NoError)
        return makeNetworkError("auth.login-sms", QStringLiteral("短信登录网络错误：") + response.errorString, httpDetails(response));
    if (response.httpStatus != 200)
    {
        result.errorCode = "AUTH_FAILED";
        result.errorMessage = QStringLiteral("短信登录失败");
        result.exitCode = ExitAuthError;
        result.details = httpDetails(response);
        return result;
    }

    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(response.body, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject())
    {
        result.errorCode = "DATA_PARSE_ERROR";
        result.errorMessage = QStringLiteral("短信登录响应 JSON 解析失败：") + jsonError.errorString();
        result.exitCode = ExitDataError;
        result.details = httpDetails(response);
        return result;
    }

    const QString authCode = doc.object().value("AuthCode").toString();
    if (authCode.isEmpty())
    {
        result.errorCode = "AUTH_FAILED";
        result.errorMessage = QStringLiteral("短信登录失败，未获取到 AuthCode");
        result.exitCode = ExitAuthError;
        result.details = httpDetails(response);
        return result;
    }

    return signInByAuthCode(authCode, session);
}

ServiceResult AuthService::refreshFromCookie(const QString &ningmengCookie, SessionInfo *session) const
{
    ServiceResult result;
    if (!session)
    {
        result.errorCode = "INTERNAL_ERROR";
        result.errorMessage = QStringLiteral("session 参数为空");
        result.exitCode = ExitUnknownError;
        return result;
    }
    if (ningmengCookie.trimmed().isEmpty())
    {
        result.errorCode = "AUTH_REQUIRED";
        result.errorMessage = QStringLiteral("ningmengcookie 不能为空");
        result.exitCode = ExitAuthError;
        return result;
    }

    session->ningmengCookie = ningmengCookie.trimmed();
    return fetchJwt(session);
}

ServiceResult AuthService::signInByAuthCode(const QString &authCode, SessionInfo *session) const
{
    ServiceResult result;
    lemoncofighelper configHelper;
    const QString signInUrl = configHelper.GetAccountUrlByKey("signInUrl").trimmed();
    if (signInUrl.isEmpty())
    {
        result.errorCode = "CONFIG_ERROR";
        result.errorMessage = QStringLiteral("signInUrl 未配置");
        result.exitCode = ExitArgumentError;
        return result;
    }

    QMap<QString, QString> params;
    params["grant_type"] = "authorization_code";
    params["code"] = authCode;
    params["client_id"] = "123456";
    params["client_secret"] = "abcdef";
    params["redirect_uri"] = "";

    HttpRequestSpec request;
    request.method = "POST";
    request.url = QUrl(signInUrl);
    request.timeoutMs = m_timeoutMs;
    request.body = buildFormBody(params);
    request.rawHeaders.append(qMakePair(QByteArray("Content-Type"), QByteArray("application/x-www-form-urlencoded")));
    request.rawHeaders.append(qMakePair(QByteArray("User-Agent"), QByteArray("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36")));

    const HttpResponse response = sendHttpRequest(request);
    if (response.timedOut)
        return makeNetworkError("auth.login-sms", QStringLiteral("AuthCode 换 token 超时"), httpDetails(response));
    if (response.networkError != QNetworkReply::NoError)
        return makeNetworkError("auth.login-sms", QStringLiteral("AuthCode 换 token 网络错误：") + response.errorString, httpDetails(response));
    if (response.httpStatus != 200)
    {
        result.errorCode = "AUTH_FAILED";
        result.errorMessage = QStringLiteral("AuthCode 换 token 失败");
        result.exitCode = ExitAuthError;
        result.details = httpDetails(response);
        return result;
    }

    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(response.body, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject())
    {
        result.errorCode = "DATA_PARSE_ERROR";
        result.errorMessage = QStringLiteral("token 响应 JSON 解析失败：") + jsonError.errorString();
        result.exitCode = ExitDataError;
        result.details = httpDetails(response);
        return result;
    }

    const QJsonObject obj = doc.object();
    const QString accessToken = obj.value("access_token").toString();
    if (accessToken.isEmpty())
    {
        result.errorCode = "AUTH_FAILED";
        result.errorMessage = QStringLiteral("token 响应缺少 access_token");
        result.exitCode = ExitAuthError;
        result.details = httpDetails(response);
        return result;
    }

    session->ningmengCookie = accessToken;
    session->refreshToken = obj.value("refresh_token").toString();
    session->expiresIn = obj.value("expires_in").toInt();
    return fetchJwt(session);
}

ServiceResult AuthService::fetchJwt(SessionInfo *session) const
{
    ServiceResult result;
    lemoncofighelper configHelper;
    QString authHost = configHelper.GetAccountUrlByKey("authHost").trimmed();
    if (authHost.isEmpty())
    {
        result.errorCode = "CONFIG_ERROR";
        result.errorMessage = QStringLiteral("authHost 鏈厤缃?");
        result.exitCode = ExitArgumentError;
        return result;
    }

    QUrl jwtUrl(authHost + "/v2/api/jwt");
    HttpRequestSpec request;
    request.method = "POST";
    request.url = jwtUrl;
    request.timeoutMs = m_timeoutMs;
    request.body = "{}";
    request.rawHeaders.append(qMakePair(QByteArray("Content-Type"), QByteArray("application/json")));
    request.rawHeaders.append(qMakePair(QByteArray("User-Agent"), QByteArray("lemonClient")));
    request.rawHeaders.append(qMakePair(QByteArray("Cookie"), ("ningmengcookie=" + session->ningmengCookie).toUtf8()));

    const HttpResponse response = sendHttpRequest(request);
    if (response.timedOut)
        return makeNetworkError("auth.refresh", QStringLiteral("获取 JWT 超时"), httpDetails(response));
    if (response.networkError != QNetworkReply::NoError)
        return makeNetworkError("auth.refresh", QStringLiteral("获取 JWT 网络错误：") + response.errorString, httpDetails(response));
    if (response.httpStatus != 200)
    {
        result.errorCode = "AUTH_FAILED";
        result.errorMessage = QStringLiteral("获取 JWT 失败");
        result.exitCode = ExitAuthError;
        result.details = httpDetails(response);
        return result;
    }

    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(response.body, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject())
    {
        result.errorCode = "DATA_PARSE_ERROR";
        result.errorMessage = QStringLiteral("JWT 响应 JSON 解析失败：") + jsonError.errorString();
        result.exitCode = ExitDataError;
        result.details = httpDetails(response);
        return result;
    }

    const QJsonObject obj = doc.object();
    if (!obj.value("success").toBool())
    {
        result.errorCode = "AUTH_FAILED";
        result.errorMessage = QStringLiteral("获取 JWT 失败");
        result.exitCode = ExitAuthError;
        result.details = httpDetails(response);
        return result;
    }

    const QString jwtToken = obj.value("token").toString();
    if (jwtToken.isEmpty())
    {
        result.errorCode = "AUTH_FAILED";
        result.errorMessage = QStringLiteral("JWT token 为空");
        result.exitCode = ExitAuthError;
        result.details = httpDetails(response);
        return result;
    }

    session->jwtToken = jwtToken;
    const QStringList jwtParts = jwtToken.split('.');
    if (jwtParts.size() >= 2)
    {
        const QByteArray payload = QByteArray::fromBase64(jwtParts.at(1).toUtf8(), QByteArray::Base64UrlEncoding);
        const QJsonDocument payloadDoc = QJsonDocument::fromJson(payload);
        if (payloadDoc.isObject())
        {
            const QJsonObject payloadObj = payloadDoc.object();
            const QString sub = payloadObj.value("sub").toString();
            if (!sub.isEmpty())
                session->userId = sub.toInt();

            QString authName = payloadObj.value("http://schemas.xmlsoap.org/ws/2005/05/identity/claims/name").toString();
            if (authName.isEmpty())
                authName = payloadObj.value("name").toString();
            if (!authName.isEmpty())
                session->authName = authName;
        }
    }

    return fetchDefaultPlan(session);
}

ServiceResult AuthService::fetchDefaultPlan(SessionInfo *session) const
{
    ServiceResult result;
    lemoncofighelper configHelper;
    const QString planUrl = configHelper.GetAccountUrlByKey(lemoncofighelper::kWbgatePlanUrlKey);
    if (planUrl.isEmpty())
    {
        result.errorCode = "CONFIG_ERROR";
        result.errorMessage = QStringLiteral("wbgatePlanUrl 鏈厤缃?");
        result.exitCode = ExitArgumentError;
        return result;
    }

    HttpRequestSpec request;
    request.method = "GET";
    request.url = QUrl(planUrl);
    request.timeoutMs = m_timeoutMs;
    request.rawHeaders.append(qMakePair(QByteArray("User-Agent"), QByteArray("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/147.0.0.0 Safari/537.36")));
    request.rawHeaders.append(qMakePair(QByteArray("Accept"), QByteArray("application/json, text/plain, */*")));
    const QString wwwOrigin = configHelper.GetAccountUrlByKey(lemoncofighelper::kWwwOriginKey);
    if (!wwwOrigin.isEmpty())
    {
        request.rawHeaders.append(qMakePair(QByteArray("Origin"), wwwOrigin.toUtf8()));
        request.rawHeaders.append(qMakePair(QByteArray("Referer"), lemoncofighelper::WithTrailingSlash(wwwOrigin).toUtf8()));
    }
    request.rawHeaders.append(qMakePair(QByteArray("Authorization"), bearer(session->jwtToken).toUtf8()));
    request.rawHeaders.append(qMakePair(QByteArray("Cookie"), ("ningmengcookie=" + session->ningmengCookie).toUtf8()));

    const HttpResponse response = sendHttpRequest(request);
    if (response.timedOut)
        return makeNetworkError("auth.refresh", QStringLiteral("获取工作台账套信息超时"), httpDetails(response));
    if (response.networkError != QNetworkReply::NoError)
        return makeNetworkError("auth.refresh", QStringLiteral("获取工作台账套信息网络错误：") + response.errorString, httpDetails(response));
    if (response.httpStatus != 200)
    {
        result.errorCode = "AUTH_FAILED";
        result.errorMessage = QStringLiteral("获取工作台账套信息失败");
        result.exitCode = ExitAuthError;
        result.details = httpDetails(response);
        return result;
    }

    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(response.body, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject())
    {
        result.errorCode = "DATA_PARSE_ERROR";
        result.errorMessage = QStringLiteral("工作台账套信息 JSON 解析失败：") + jsonError.errorString();
        result.exitCode = ExitDataError;
        result.details = httpDetails(response);
        return result;
    }

    const QJsonArray plans = doc.object().value("data").toObject().value("data").toArray();
    int fallbackServiceId = 0;
    QString fallbackAppAsId;

    for (const QJsonValue &value : plans)
    {
        const QJsonObject plan = value.toObject();
        if (plan.value("productType").toString() != "acc")
            continue;

        const int serviceId = plan.value("id").toInt();
        const QString appAsId = plan.value("lastAccountSet").toObject().value("appAsid").toString();
        if (serviceId == 0 || appAsId.isEmpty())
            continue;

        const int administrator = plan.value("administrator").toInt();
        if (session->userId != 0 && administrator == session->userId)
        {
            session->serviceId = serviceId;
            session->appAsId = appAsId;
            session->updatedAt = isoNow();
            result.ok = true;
            result.data = session->toJson(false);
            return result;
        }

        if (fallbackServiceId == 0)
        {
            fallbackServiceId = serviceId;
            fallbackAppAsId = appAsId;
        }
    }

    if (fallbackServiceId != 0 && !fallbackAppAsId.isEmpty())
    {
        session->serviceId = fallbackServiceId;
        session->appAsId = fallbackAppAsId;
        session->updatedAt = isoNow();
        result.ok = true;
        result.data = session->toJson(false);
        return result;
    }

    result.errorCode = "NO_ACCOUNTSET_PLAN";
    result.errorMessage = QStringLiteral("未获取到账套服务编号，请确认账号是否有柠檬云财务账套");
    result.exitCode = ExitAuthError;
    return result;
}

ServiceResult AuthService::makeNetworkError(const QString &, const QString &message, const QJsonObject &details) const
{
    ServiceResult result;
    result.errorCode = "NETWORK_ERROR";
    result.errorMessage = message;
    result.exitCode = ExitNetworkError;
    result.details = details;
    return result;
}
