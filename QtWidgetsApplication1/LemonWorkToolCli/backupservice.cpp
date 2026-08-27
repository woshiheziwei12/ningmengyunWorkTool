#include "backupservice.h"

#include "accountsetservice.h"
#include "cliresult.h"
#include "lemoncofighelper.h"
#include "networkutils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkReply>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>

namespace
{
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

QString safeFileName(QString name)
{
    const QString invalid = "\\/:*?\"<>|";
    for (const QChar &ch : invalid)
        name.replace(ch, '_');
    return name.trimmed().isEmpty() ? QStringLiteral("backup.zip") : name;
}

QString backupSettingsPath()
{
    return QDir::cleanPath(QCoreApplication::applicationDirPath() + "/LemonBackup.ini");
}

QString fallbackDownloadPath()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (path.isEmpty())
        path = QDir::homePath() + "/Downloads";
    return QDir::cleanPath(path);
}

QString persistentBackupPath()
{
    QSettings settings(backupSettingsPath(), QSettings::IniFormat);
    QString saved = settings.value("backup/savePath").toString();
    if (!saved.isEmpty() && QDir(saved).exists())
        return QDir::cleanPath(saved);

    saved = fallbackDownloadPath();
    settings.setValue("backup/savePath", QDir::fromNativeSeparators(saved));
    settings.sync();
    return saved;
}
}

BackupService::BackupService(int timeoutMs)
    : m_timeoutMs(timeoutMs)
{
}

ServiceResult BackupService::backupAccountSets(const SessionInfo &session, const QList<int> &asIds,
                                               const QString &outputDir, int pollIntervalMs,
                                               int maxWaitMs) const
{
    ServiceResult result;
    if (asIds.isEmpty())
    {
        result.errorCode = "ARGUMENT_ERROR";
        result.errorMessage = QStringLiteral("缺少 --as-id");
        result.exitCode = ExitArgumentError;
        return result;
    }

    QString saveDirPath = outputDir;
    const bool usedPersistedOutputDir = saveDirPath.isEmpty();
    if (usedPersistedOutputDir)
        saveDirPath = persistentBackupPath();
    QDir saveDir(saveDirPath);
    if (!saveDir.exists() && !saveDir.mkpath("."))
    {
        result.errorCode = "FILE_ERROR";
        result.errorMessage = QStringLiteral("创建备份输出目录失败：") + QDir::toNativeSeparators(saveDirPath);
        result.exitCode = ExitFileError;
        return result;
    }

    AccountSetService accountSetService(m_timeoutMs);
    const ServiceResult listResult = accountSetService.listAccountSets(session);
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
        const QString appAsId = account.value("appAsId").toString();
        const int serviceId = account.value("serviceId").toInt();
        const QString asName = account.value("asName").toString();
        item["asName"] = asName;
        item["appAsId"] = appAsId;
        item["serviceId"] = serviceId;

        HttpRequestSpec startReq;
        startReq.method = "POST";
        startReq.url = QUrl(host + "/api/Backup/ProcessBackup?allowTemporaryFile=1&isProcess=1&appasid=" + appAsId);
        startReq.timeoutMs = m_timeoutMs;
        startReq.body = "{}";
        startReq.rawHeaders = jproHeaders();
        startReq.rawHeaders.append(qMakePair(QByteArray("appasid"), appAsId.toUtf8()));
        startReq.rawHeaders.append(qMakePair(QByteArray("Cookie"), buildCookieString(session, serviceId).toUtf8()));

        const HttpResponse startResp = sendHttpRequest(startReq);
        if (startResp.timedOut || startResp.networkError != QNetworkReply::NoError || startResp.httpStatus != 200)
        {
            item["ok"] = false;
            item["message"] = startResp.timedOut ? QStringLiteral("发起备份超时") : QStringLiteral("发起备份失败：") + startResp.errorString;
            item["details"] = httpDetails(startResp);
            ++failedCount;
            items.append(item);
            continue;
        }

        const QJsonObject startRoot = QJsonDocument::fromJson(startResp.body).object();
        if (startRoot.value("state").toInt() != 1000)
        {
            item["ok"] = false;
            item["message"] = startRoot.value("msg").toString(QStringLiteral("备份接口返回失败"));
            ++failedCount;
            items.append(item);
            continue;
        }

        const QString fileName = startRoot.value("data").toObject().value("fileName").toString();
        if (fileName.isEmpty())
        {
            item["ok"] = false;
            item["message"] = QStringLiteral("备份接口未返回 fileName");
            ++failedCount;
            items.append(item);
            continue;
        }
        item["fileName"] = fileName;

        int progress = 0;
        int waitedMs = 0;
        bool pollFailed = false;
        QString pollError;
        while (progress < 100 && waitedMs <= maxWaitMs)
        {
            HttpRequestSpec pollReq;
            pollReq.method = "POST";
            pollReq.url = QUrl(host + "/api/Backup/GetFileRecord?filename="
                + QString::fromUtf8(QUrl::toPercentEncoding(fileName))
                + "&appasid=" + appAsId);
            pollReq.timeoutMs = m_timeoutMs;
            pollReq.body = "{}";
            pollReq.rawHeaders = jproHeaders();
            pollReq.rawHeaders.append(qMakePair(QByteArray("appasid"), appAsId.toUtf8()));
            pollReq.rawHeaders.append(qMakePair(QByteArray("Cookie"), buildCookieString(session, serviceId).toUtf8()));

            const HttpResponse pollResp = sendHttpRequest(pollReq);
            if (pollResp.timedOut || pollResp.networkError != QNetworkReply::NoError || pollResp.httpStatus != 200)
            {
                pollFailed = true;
                pollError = pollResp.timedOut ? QStringLiteral("查询备份进度超时") : QStringLiteral("查询备份进度失败：") + pollResp.errorString;
                break;
            }

            const QJsonObject pollRoot = QJsonDocument::fromJson(pollResp.body).object();
            progress = pollRoot.value("data").toObject().value("progress").toInt();
            item["progress"] = progress;
            if (progress >= 100)
                break;
            QThread::msleep(static_cast<unsigned long>(pollIntervalMs));
            waitedMs += pollIntervalMs;
        }

        if (pollFailed || progress < 100)
        {
            item["ok"] = false;
            item["message"] = pollFailed ? pollError : QStringLiteral("备份等待超时");
            ++failedCount;
            items.append(item);
            continue;
        }

        HttpRequestSpec redirectReq;
        redirectReq.method = "GET";
        redirectReq.url = QUrl(host + "/api/Backup/DownloadFile?fileName="
            + QString::fromUtf8(QUrl::toPercentEncoding(fileName))
            + "&fileType=0&appasid=" + appAsId);
        redirectReq.timeoutMs = m_timeoutMs;
        redirectReq.manualRedirect = true;
        redirectReq.rawHeaders = jproHeaders();
        redirectReq.rawHeaders.append(qMakePair(QByteArray("Cookie"), buildCookieString(session, serviceId).toUtf8()));

        HttpResponse downloadResp = sendHttpRequest(redirectReq);
        if (downloadResp.httpStatus == 301 || downloadResp.httpStatus == 302)
        {
            const QString location = rawHeaderValue(downloadResp, "Location");
            if (location.isEmpty())
            {
                item["ok"] = false;
                item["message"] = QStringLiteral("下载重定向地址为空");
                ++failedCount;
                items.append(item);
                continue;
            }

            HttpRequestSpec realDownloadReq;
            realDownloadReq.method = "GET";
            realDownloadReq.url = QUrl(location);
            realDownloadReq.timeoutMs = m_timeoutMs;
            realDownloadReq.rawHeaders = jproHeaders();
            realDownloadReq.rawHeaders.append(qMakePair(QByteArray("Cookie"), buildCookieString(session, serviceId).toUtf8()));
            downloadResp = sendHttpRequest(realDownloadReq);
        }

        if (downloadResp.timedOut || downloadResp.networkError != QNetworkReply::NoError || downloadResp.httpStatus != 200)
        {
            item["ok"] = false;
            item["message"] = downloadResp.timedOut ? QStringLiteral("下载备份文件超时") : QStringLiteral("下载备份文件失败：") + downloadResp.errorString;
            item["details"] = httpDetails(downloadResp);
            ++failedCount;
            items.append(item);
            continue;
        }

        QString localFileName = safeFileName(fileName);
        if (!localFileName.endsWith(".zip", Qt::CaseInsensitive))
            localFileName += ".zip";
        QString localFilePath = saveDir.absoluteFilePath(localFileName);
        int suffix = 1;
        while (QFile::exists(localFilePath))
        {
            const QFileInfo info(localFileName);
            localFilePath = saveDir.absoluteFilePath(info.completeBaseName() + QString("_%1.").arg(suffix++) + info.suffix());
        }

        QFile outFile(localFilePath);
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            item["ok"] = false;
            item["message"] = QStringLiteral("保存备份文件失败：") + outFile.errorString();
            ++failedCount;
            items.append(item);
            continue;
        }
        outFile.write(downloadResp.body);
        outFile.close();

        item["ok"] = true;
        item["message"] = QStringLiteral("已保存");
        item["localFilePath"] = QDir::toNativeSeparators(localFilePath);
        item["bytes"] = QString::number(downloadResp.body.size()).toDouble();
        ++successCount;
        items.append(item);
    }

    QJsonObject data;
    data["outputDir"] = QDir::toNativeSeparators(saveDir.absolutePath());
    data["usedPersistedOutputDir"] = usedPersistedOutputDir;
    data["backupSettingsPath"] = QDir::toNativeSeparators(backupSettingsPath());
    data["total"] = asIds.size();
    data["success"] = successCount;
    data["failed"] = failedCount;
    data["items"] = items;

    result.ok = failedCount == 0;
    result.data = data;
    if (!result.ok)
    {
        result.errorCode = "BACKUP_FAILED";
        result.errorMessage = failedCount == asIds.size()
            ? QStringLiteral("账套备份失败")
            : QStringLiteral("部分账套备份失败");
        result.exitCode = ExitServerError;
        result.details = data;
    }
    return result;
}

QString BackupService::buildCookieString(const SessionInfo &session, int serviceId) const
{
    return lemoncofighelper::BuildJproCookie(session.ningmengCookie,
                                             serviceId,
                                             session.userId,
                                             session.authName);
}
