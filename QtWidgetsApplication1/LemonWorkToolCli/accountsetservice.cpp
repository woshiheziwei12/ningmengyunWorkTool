#include "accountsetservice.h"

#include "cliresult.h"
#include "lemoncofighelper.h"
#include "networkutils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMap>
#include <QNetworkReply>
#include <QUrl>

namespace
{
QJsonObject httpDetailsForAccountSet(const HttpResponse &response)
{
    QJsonObject details;
    details["httpStatus"] = response.httpStatus;
    details["networkError"] = response.networkError;
    details["timedOut"] = response.timedOut;
    if (!response.errorString.isEmpty())
        details["networkErrorMessage"] = response.errorString;
    return details;
}

bool isAuthReady(const SessionInfo &session)
{
    return !session.ningmengCookie.isEmpty() && !session.jwtToken.isEmpty();
}

QList<QPair<QByteArray, QByteArray>> browserHeaders()
{
    QList<QPair<QByteArray, QByteArray>> headers;
    headers.append(qMakePair(QByteArray("User-Agent"), QByteArray("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/147.0.0.0 Safari/537.36")));
    headers.append(qMakePair(QByteArray("Accept"), QByteArray("application/json, text/plain, */*")));
    const QString wwwOrigin = lemoncofighelper::AccountConfigValue(lemoncofighelper::kWwwOriginKey);
    if (!wwwOrigin.isEmpty())
    {
        headers.append(qMakePair(QByteArray("Origin"), wwwOrigin.toUtf8()));
        headers.append(qMakePair(QByteArray("Referer"), lemoncofighelper::WithTrailingSlash(wwwOrigin).toUtf8()));
    }
    return headers;
}

QList<QPair<QByteArray, QByteArray>> jproHeaders()
{
    QList<QPair<QByteArray, QByteArray>> headers;
    headers.append(qMakePair(QByteArray("Content-Type"), QByteArray("application/json")));
    headers.append(qMakePair(QByteArray("User-Agent"), QByteArray("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/149.0.0.0 Safari/537.36")));
    headers.append(qMakePair(QByteArray("Accept"), QByteArray("application/json, text/plain, */*")));
    headers.append(qMakePair(QByteArray("Sec-Fetch-Site"), QByteArray("same-site")));
    headers.append(qMakePair(QByteArray("Sec-Fetch-Mode"), QByteArray("cors")));
    headers.append(qMakePair(QByteArray("Sec-Fetch-Dest"), QByteArray("empty")));
    const QString jproOrigin = lemoncofighelper::AccountConfigValue(lemoncofighelper::kJproOriginKey);
    if (!jproOrigin.isEmpty())
    {
        headers.append(qMakePair(QByteArray("Origin"), jproOrigin.toUtf8()));
        headers.append(qMakePair(QByteArray("Referer"), lemoncofighelper::WithTrailingSlash(jproOrigin).toUtf8()));
    }
    return headers;
}
}

AccountSetService::AccountSetService(int timeoutMs)
    : m_timeoutMs(timeoutMs)
{
}

ServiceResult AccountSetService::listPlans(const SessionInfo &session) const
{
    ServiceResult result;
    if (!isAuthReady(session))
    {
        result.errorCode = "AUTH_REQUIRED";
        result.errorMessage = QStringLiteral("请先登录，或提供有效 session");
        result.exitCode = ExitAuthError;
        return result;
    }

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
    request.rawHeaders = browserHeaders();
    request.rawHeaders.append(qMakePair(QByteArray("Authorization"), ("Bearer " + session.jwtToken).toUtf8()));
    request.rawHeaders.append(qMakePair(QByteArray("Cookie"), ("ningmengcookie=" + session.ningmengCookie).toUtf8()));

    const HttpResponse response = sendHttpRequest(request);
    if (response.timedOut || response.networkError != QNetworkReply::NoError)
    {
        result.errorCode = "NETWORK_ERROR";
        result.errorMessage = response.timedOut
            ? QStringLiteral("获取套餐列表超时")
            : QStringLiteral("获取套餐列表网络错误：") + response.errorString;
        result.exitCode = ExitNetworkError;
        result.details = httpDetailsForAccountSet(response);
        return result;
    }
    if (response.httpStatus != 200)
    {
        result.errorCode = "SERVER_ERROR";
        result.errorMessage = QStringLiteral("获取套餐列表失败");
        result.exitCode = ExitServerError;
        result.details = httpDetailsForAccountSet(response);
        return result;
    }

    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(response.body, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject())
    {
        result.errorCode = "DATA_PARSE_ERROR";
        result.errorMessage = QStringLiteral("套餐列表 JSON 解析失败：") + jsonError.errorString();
        result.exitCode = ExitDataError;
        result.details = httpDetailsForAccountSet(response);
        return result;
    }

    const QJsonArray plans = doc.object().value("data").toObject().value("data").toArray();
    QJsonArray items;
    for (const QJsonValue &value : plans)
    {
        const QJsonObject plan = value.toObject();
        QJsonObject item;
        item["serviceId"] = plan.value("id").toInt();
        item["appAsId"] = plan.value("lastAccountSet").toObject().value("appAsid").toString();
        item["productType"] = plan.value("productType").toString();
        item["productTypeDescription"] = plan.value("productTypeDescription").toString();
        item["validStatus"] = plan.value("validStatus").toString();
        item["isValid"] = plan.value("isValid").toBool();
        item["expired"] = plan.value("expired").toBool();
        item["closed"] = plan.value("Closed").toBool();
        item["administrator"] = plan.value("administrator").toInt();
        items.append(item);
    }

    QJsonObject data;
    data["count"] = items.size();
    data["items"] = items;
    result.ok = true;
    result.data = data;
    return result;
}

ServiceResult AccountSetService::listAccountSets(const SessionInfo &session) const
{
    ServiceResult result;
    const ServiceResult planResult = listPlans(session);
    if (!planResult.ok)
        return planResult;

    QJsonArray preferredPlans;
    QJsonArray fallbackPlans;
    const QJsonArray plans = planResult.data.value("items").toArray();
    for (const QJsonValue &value : plans)
    {
        const QJsonObject plan = value.toObject();
        if (plan.value("productType").toString() != "acc")
            continue;
        if ((plan.contains("isValid") && !plan.value("isValid").toBool())
            || plan.value("expired").toBool()
            || plan.value("closed").toBool())
            continue;
        if (session.userId != 0 && plan.value("administrator").toInt() != session.userId)
            fallbackPlans.append(plan);
        else
            preferredPlans.append(plan);
    }

    QJsonArray matchedPlans;
    const QJsonArray candidatePlans = preferredPlans.isEmpty() ? fallbackPlans : preferredPlans;
    for (const QJsonValue &value : candidatePlans)
    {
        const QJsonObject plan = value.toObject();
        if (plan.value("serviceId").toInt() == 0 || plan.value("appAsId").toString().isEmpty())
            continue;
        matchedPlans.append(plan);
    }

    if (matchedPlans.isEmpty())
    {
        result.errorCode = "NO_ACCOUNTSET_PLAN";
        result.errorMessage = QStringLiteral("未获取到账套服务编号，请确认账号是否有有效财务账套");
        result.exitCode = ExitAuthError;
        return result;
    }

    const QString host = lemoncofighelper::JproApiHost();
    if (host.isEmpty())
    {
        result.errorCode = "CONFIG_ERROR";
        result.errorMessage = QStringLiteral("jproApiHost 未配置");
        result.exitCode = ExitArgumentError;
        return result;
    }

    QJsonArray accountItems;
    QJsonArray failures;
    for (const QJsonValue &planValue : matchedPlans)
    {
        const QJsonObject plan = planValue.toObject();
        const int serviceId = plan.value("serviceId").toInt();
        const QString appAsId = plan.value("appAsId").toString();
        const QString url = host + "/api/AccountSetOnlyAuth/TopInfoV2"
            + "?serviceId=" + QString::number(serviceId)
            + "&appasid=" + appAsId;

        HttpRequestSpec request;
        request.method = "GET";
        request.url = QUrl(url);
        request.timeoutMs = m_timeoutMs;
        request.rawHeaders = jproHeaders();
        request.rawHeaders.append(qMakePair(QByteArray("appasid"), appAsId.toUtf8()));
        request.rawHeaders.append(qMakePair(QByteArray("Cookie"), buildCookieString(session, serviceId).toUtf8()));

        const HttpResponse response = sendHttpRequest(request);
        if (response.timedOut || response.networkError != QNetworkReply::NoError || response.httpStatus != 200)
        {
            QJsonObject failure;
            failure["serviceId"] = serviceId;
            failure["appAsId"] = appAsId;
            failure["httpStatus"] = response.httpStatus;
            failure["message"] = response.timedOut ? QStringLiteral("请求超时") : response.errorString;
            failures.append(failure);
            continue;
        }

        QJsonParseError jsonError;
        const QJsonDocument doc = QJsonDocument::fromJson(response.body, &jsonError);
        if (jsonError.error != QJsonParseError::NoError || !doc.isObject())
        {
            QJsonObject failure;
            failure["serviceId"] = serviceId;
            failure["appAsId"] = appAsId;
            failure["message"] = QStringLiteral("账套列表 JSON 解析失败：") + jsonError.errorString();
            failures.append(failure);
            continue;
        }

        const QJsonObject root = doc.object();
        if (root.value("state").toInt() != 1000)
        {
            QJsonObject failure;
            failure["serviceId"] = serviceId;
            failure["appAsId"] = appAsId;
            failure["message"] = root.value("msg").toString(QStringLiteral("接口返回失败"));
            failures.append(failure);
            continue;
        }

        const QJsonArray arr = root.value("data").toObject().value("accountList").toArray();
        for (const QJsonValue &accountValue : arr)
        {
            const QJsonObject obj = accountValue.toObject();
            QJsonObject item;
            item["asId"] = obj.value("asid").toInt();
            item["asName"] = obj.value("asname").toString();
            item["appAsId"] = obj.value("appAsId").toString();
            item["asStartDate"] = QString::number(obj.value("asStartYear").toInt()) + "-"
                + QString::number(obj.value("asStartMonth").toInt()).rightJustified(2, '0');
            item["serviceId"] = serviceId;
            item["productTypeDescription"] = plan.value("productTypeDescription").toString();
            accountItems.append(item);
        }
    }

    QJsonObject data;
    data["count"] = accountItems.size();
    data["items"] = accountItems;
    data["failures"] = failures;
    result.ok = true;
    result.data = data;
    return result;
}

ServiceResult AccountSetService::deleteAccountSets(const SessionInfo &session, const QList<int> &asIds) const
{
    ServiceResult result;
    if (asIds.isEmpty())
    {
        result.errorCode = "ARGUMENT_ERROR";
        result.errorMessage = QStringLiteral("缺少 --as-id");
        result.exitCode = ExitArgumentError;
        return result;
    }

    const ServiceResult listResult = listAccountSets(session);
    if (!listResult.ok)
        return listResult;

    QMap<int, QJsonObject> accountsById;
    const QJsonArray accounts = listResult.data.value("items").toArray();
    for (const QJsonValue &value : accounts)
    {
        const QJsonObject item = value.toObject();
        accountsById.insert(item.value("asId").toInt(), item);
    }

    const QString host = lemoncofighelper::JproApiHost();
    if (host.isEmpty())
    {
        result.errorCode = "CONFIG_ERROR";
        result.errorMessage = QStringLiteral("jproApiHost 未配置");
        result.exitCode = ExitArgumentError;
        return result;
    }

    QJsonArray items;
    int successCount = 0;
    int failedCount = 0;

    for (int asId : asIds)
    {
        QJsonObject item;
        item["asId"] = asId;

        if (!accountsById.contains(asId))
        {
            item["ok"] = false;
            item["message"] = QStringLiteral("未在当前账号账套列表中找到该 asId");
            ++failedCount;
            items.append(item);
            continue;
        }

        const QJsonObject account = accountsById.value(asId);
        const int serviceId = account.value("serviceId").toInt();
        const QString appAsId = account.value("appAsId").toString();
        const QString asName = account.value("asName").toString();
        item["asName"] = asName;
        item["serviceId"] = serviceId;
        item["appAsId"] = appAsId;

        const QString url = host + "/api/AccountSetOnlyAuth/DropAccountSetToRecycleBinV2"
            + "?asId=" + QString::number(asId)
            + "&serviceid=" + QString::number(serviceId)
            + "&appasid=" + appAsId;

        HttpRequestSpec request;
        request.method = "POST";
        request.url = QUrl(url);
        request.timeoutMs = m_timeoutMs;
        request.rawHeaders = jproHeaders();
        request.rawHeaders.append(qMakePair(QByteArray("appasid"), appAsId.toUtf8()));
        request.rawHeaders.append(qMakePair(QByteArray("Cookie"), buildCookieString(session, serviceId).toUtf8()));

        const HttpResponse response = sendHttpRequest(request);
        item["httpStatus"] = response.httpStatus;
        if (response.timedOut || response.networkError != QNetworkReply::NoError || response.httpStatus != 200)
        {
            item["ok"] = false;
            item["message"] = response.timedOut ? QStringLiteral("请求超时") : response.errorString;
            ++failedCount;
            items.append(item);
            continue;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(response.body);
        const QJsonObject root = doc.object();
        const int state = root.value("state").toInt();
        const bool dataValue = root.value("data").toBool();
        item["apiState"] = state;
        if (state == 1000 && dataValue)
        {
            item["ok"] = true;
            item["message"] = QStringLiteral("删除成功");
            ++successCount;
        }
        else
        {
            item["ok"] = false;
            item["message"] = root.value("msg").toString(QStringLiteral("接口返回失败，state=") + QString::number(state));
            ++failedCount;
        }
        items.append(item);
    }

    QJsonObject data;
    data["total"] = asIds.size();
    data["success"] = successCount;
    data["failed"] = failedCount;
    data["items"] = items;
    result.ok = failedCount == 0;
    result.data = data;
    if (!result.ok)
    {
        result.errorCode = "PARTIAL_FAILED";
        result.errorMessage = QStringLiteral("部分账套删除失败");
        result.exitCode = ExitServerError;
        result.details = data;
    }
    return result;
}

QString AccountSetService::buildCookieString(const SessionInfo &session, int serviceId) const
{
    return lemoncofighelper::BuildJproCookie(session.ningmengCookie,
                                             serviceId,
                                             session.userId,
                                             session.authName);
}
