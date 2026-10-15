#include "importpackageuploader.h"
#include "importpackagehistory.h"
#include "logger.h"
#include "../httprequesthelper.h"
#include "../lemoncofighelper.h"

#include <QEventLoop>
#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopedPointer>
#include <QTimer>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariant>

namespace
{
QString hostKeyForRequest(const ImportPackageUploadRequest &request)
{
    Q_UNUSED(request);
    return lemoncofighelper::kProImporterApiHostKey;
}

bool writeBytes(QFile &file, const char *data, qint64 length, QString *errorMessage)
{
    if (length <= 0)
        return true;

    const qint64 written = file.write(data, length);
    if (written != length)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("write AccountSet.json failed: ") + file.errorString();
        return false;
    }
    return true;
}

bool writeBytes(QFile &file, const QByteArray &data, QString *errorMessage)
{
    return writeBytes(file, data.constData(), data.size(), errorMessage);
}

bool writeByte(QFile &file, char ch, QString *errorMessage)
{
    return writeBytes(file, &ch, 1, errorMessage);
}

bool isJsonWhitespace(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

bool isAsciiDigit(char ch)
{
    return ch >= '0' && ch <= '9';
}

bool runPowerShellScript(const QString &script,
                         const QProcessEnvironment &env,
                         int timeoutMs,
                         const QString &timeoutMessage,
                         const QString &failureMessage,
                         QString *errorMessage)
{
    QProcess process;
    process.setProcessEnvironment(env);
    const QStringList args = QStringList()
        << "-NoProfile"
        << "-ExecutionPolicy"
        << "Bypass"
        << "-Command"
        << script;
    AppLogger::command("powershell.exe", args);
    process.start("powershell.exe", args);

    if (!process.waitForFinished(timeoutMs))
    {
        process.kill();
        process.waitForFinished(3000);
        if (errorMessage)
            *errorMessage = timeoutMessage;
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        const QString stdErr = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        const QString stdOut = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
        if (errorMessage)
            *errorMessage = stdErr.isEmpty()
                ? (stdOut.isEmpty() ? failureMessage : stdOut)
                : stdErr;
        return false;
    }

    return true;
}

bool copyUntilKey(QFile &input, QFile &output, const QByteArray &key, QByteArray *afterKey, QString *errorMessage)
{
    QByteArray tail;
    QByteArray buffer;
    buffer.resize(1024 * 1024);

    while (true)
    {
        const qint64 bytesRead = input.read(buffer.data(), buffer.size());
        if (bytesRead < 0)
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("read AccountSet.json failed: ") + input.errorString();
            return false;
        }
        if (bytesRead == 0)
            break;

        const QByteArray chunk(buffer.constData(), static_cast<int>(bytesRead));
        const QByteArray data = tail + chunk;
        const int pos = data.indexOf(key);
        if (pos >= 0)
        {
            const int throughKey = pos + key.size();
            if (!writeBytes(output, data.constData(), throughKey, errorMessage))
                return false;
            if (afterKey)
                *afterKey = data.mid(throughKey);
            return true;
        }

        const int keep = qMin(key.size() - 1, data.size());
        const int writeLength = data.size() - keep;
        if (writeLength > 0 && !writeBytes(output, data.constData(), writeLength, errorMessage))
            return false;
        tail = data.right(keep);
    }

    if (errorMessage)
        *errorMessage = QStringLiteral("ProServiceId not found in AccountSet.json");
    return false;
}

bool copyRemaining(QFile &input,
                   QFile &output,
                   const QByteArray &pending,
                   int pendingOffset,
                   QString *errorMessage)
{
    if (pendingOffset < pending.size())
    {
        const QByteArray rest = pending.mid(pendingOffset);
        if (!writeBytes(output, rest, errorMessage))
            return false;
    }

    QByteArray buffer;
    buffer.resize(1024 * 1024);
    while (true)
    {
        const qint64 bytesRead = input.read(buffer.data(), buffer.size());
        if (bytesRead < 0)
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("read AccountSet.json failed: ") + input.errorString();
            return false;
        }
        if (bytesRead == 0)
            return true;
        if (!writeBytes(output, buffer.constData(), bytesRead, errorMessage))
            return false;
    }
}

bool patchIntegerFieldInJsonFile(const QString &sourceJsonPath,
                                 const QString &targetJsonPath,
                                 const QByteArray &fieldName,
                                 qint64 fieldValue,
                                 QString *errorMessage)
{
    QFile input(sourceJsonPath);
    if (!input.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("open AccountSet.json failed: ") + input.errorString();
        return false;
    }

    QFile output(targetJsonPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("create patched AccountSet.json failed: ") + output.errorString();
        return false;
    }

    const QString fieldNameText = QString::fromLatin1(fieldName);
    const QByteArray key = '"' + fieldName + '"';
    QByteArray pending;
    if (!copyUntilKey(input, output, key, &pending, errorMessage))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("%1 not found in AccountSet.json").arg(fieldNameText);
        return false;
    }

    int pendingOffset = 0;
    const auto readNext = [&input, &pending, &pendingOffset](char *ch) -> bool {
        if (pendingOffset < pending.size())
        {
            *ch = pending.at(pendingOffset++);
            return true;
        }
        return input.getChar(ch);
    };

    char ch = '\0';
    bool hasColon = false;
    while (readNext(&ch))
    {
        if (isJsonWhitespace(ch))
        {
            if (!writeByte(output, ch, errorMessage))
                return false;
            continue;
        }
        if (ch == ':')
        {
            if (!writeByte(output, ch, errorMessage))
                return false;
            hasColon = true;
            break;
        }

        if (errorMessage)
            *errorMessage = QStringLiteral("%1 field is invalid in AccountSet.json").arg(fieldNameText);
        return false;
    }

    if (!hasColon)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("%1 value is missing in AccountSet.json").arg(fieldNameText);
        return false;
    }

    bool hasValueStart = false;
    while (readNext(&ch))
    {
        if (isJsonWhitespace(ch))
        {
            if (!writeByte(output, ch, errorMessage))
                return false;
            continue;
        }
        hasValueStart = true;
        break;
    }

    if (!hasValueStart)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("%1 value is missing in AccountSet.json").arg(fieldNameText);
        return false;
    }

    if (ch == '-')
    {
        if (!readNext(&ch))
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("%1 value is invalid in AccountSet.json").arg(fieldNameText);
            return false;
        }
    }

    if (!isAsciiDigit(ch))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("%1 value is invalid in AccountSet.json").arg(fieldNameText);
        return false;
    }

    const QByteArray replacement = QByteArray::number(fieldValue);
    if (!writeBytes(output, replacement, errorMessage))
        return false;

    while (readNext(&ch))
    {
        if (isAsciiDigit(ch))
            continue;

        if (!writeByte(output, ch, errorMessage))
            return false;
        return copyRemaining(input, output, pending, pendingOffset, errorMessage);
    }

    return true;
}

bool extractAccountSetJsonFromZip(const QString &zipPath,
                                  const QString &jsonPath,
                                  const QString &entryNamePath,
                                  QString *errorMessage)
{
    const QString script = QString::fromLatin1(R"PS(
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=$env:LEMON_UPLOAD_ZIP_DST
$jsonPath=$env:LEMON_UPLOAD_JSON_OUT
$entryOut=$env:LEMON_UPLOAD_ENTRY_OUT
if([string]::IsNullOrWhiteSpace($zip)){throw 'zip path is empty'}
if([string]::IsNullOrWhiteSpace($jsonPath)){throw 'json output path is empty'}
if([string]::IsNullOrWhiteSpace($entryOut)){throw 'entry output path is empty'}
$archive=[System.IO.Compression.ZipFile]::OpenRead($zip)
try{
  $entry=$archive.Entries | Where-Object { [System.IO.Path]::GetFileName($_.FullName) -ieq 'AccountSet.json' } | Select-Object -First 1
  if($null -eq $entry){throw 'AccountSet.json not found in zip'}
  if(Test-Path -LiteralPath $jsonPath){Remove-Item -LiteralPath $jsonPath -Force}
  [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$jsonPath)
  [System.IO.File]::WriteAllText($entryOut,$entry.FullName,(New-Object System.Text.UTF8Encoding($false)))
}finally{$archive.Dispose()}
)PS");

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LEMON_UPLOAD_ZIP_DST", QFileInfo(zipPath).absoluteFilePath());
    env.insert("LEMON_UPLOAD_JSON_OUT", QFileInfo(jsonPath).absoluteFilePath());
    env.insert("LEMON_UPLOAD_ENTRY_OUT", QFileInfo(entryNamePath).absoluteFilePath());

    return runPowerShellScript(script,
                               env,
                               120000,
                               QStringLiteral("\u4e0a\u4f20\u524d\u89e3\u538b AccountSet.json \u8d85\u65f6"),
                               QStringLiteral("\u4e0a\u4f20\u524d\u89e3\u538b AccountSet.json \u5931\u8d25"),
                               errorMessage);
}

bool updateAccountSetJsonInZip(const QString &zipPath,
                               const QString &entryName,
                               const QString &patchedJsonPath,
                               QString *errorMessage)
{
    const QString script = QString::fromLatin1(R"PS(
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=$env:LEMON_UPLOAD_ZIP_DST
$entryName=$env:LEMON_UPLOAD_ENTRY_NAME
$jsonPath=$env:LEMON_UPLOAD_PATCHED_JSON
if([string]::IsNullOrWhiteSpace($zip)){throw 'zip path is empty'}
if([string]::IsNullOrWhiteSpace($entryName)){throw 'entry name is empty'}
if([string]::IsNullOrWhiteSpace($jsonPath)){throw 'patched json path is empty'}
$archive=[System.IO.Compression.ZipFile]::Open($zip,[System.IO.Compression.ZipArchiveMode]::Update)
try{
  $entry=$archive.GetEntry($entryName)
  if($null -eq $entry){throw 'AccountSet.json entry not found in zip'}
  $entry.Delete()
  [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$jsonPath,$entryName,[System.IO.Compression.CompressionLevel]::Optimal)|Out-Null
}finally{$archive.Dispose()}
)PS");

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LEMON_UPLOAD_ZIP_DST", QFileInfo(zipPath).absoluteFilePath());
    env.insert("LEMON_UPLOAD_ENTRY_NAME", entryName);
    env.insert("LEMON_UPLOAD_PATCHED_JSON", QFileInfo(patchedJsonPath).absoluteFilePath());

    return runPowerShellScript(script,
                               env,
                               120000,
                               QStringLiteral("\u4e0a\u4f20\u524d\u5199\u56de AccountSet.json \u8d85\u65f6"),
                               QStringLiteral("\u4e0a\u4f20\u524d\u5199\u56de AccountSet.json \u5931\u8d25"),
                               errorMessage);
}

bool rewriteAccountSetJsonProServiceId(const QString &sourceZipPath, const QString &targetZipPath, int proServiceId, QString *errorMessage)
{
    if (proServiceId <= 0)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u5f53\u524d\u767b\u5f55\u670d\u52a1\u7f16\u53f7\u65e0\u6548");
        return false;
    }

    const QString sourceAbs = QFileInfo(sourceZipPath).absoluteFilePath();
    const QString targetAbs = QFileInfo(targetZipPath).absoluteFilePath();

    QDir targetDir(QFileInfo(targetAbs).absolutePath());
    if (!targetDir.exists() && !targetDir.mkpath(QStringLiteral(".")))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u521b\u5efa\u4e34\u65f6\u538b\u7f29\u5305\u76ee\u5f55\u5931\u8d25");
        return false;
    }

    if (sourceAbs != targetAbs)
    {
        if (QFile::exists(targetAbs) && !QFile::remove(targetAbs))
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("remove existing target zip failed: ") + QDir::toNativeSeparators(targetAbs);
            return false;
        }
        if (!QFile::copy(sourceAbs, targetAbs))
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("copy zip before upload failed: ") + QDir::toNativeSeparators(sourceAbs);
            return false;
        }
    }

    QTemporaryDir workDir(QDir(QFileInfo(targetAbs).absolutePath()).filePath(QStringLiteral("accountset_patch_XXXXXX")));
    if (!workDir.isValid())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u521b\u5efa AccountSet.json \u4e34\u65f6\u76ee\u5f55\u5931\u8d25");
        return false;
    }

    const QString extractedJsonPath = QDir(workDir.path()).filePath(QStringLiteral("AccountSet.json"));
    const QString accountTypeJsonPath = QDir(workDir.path()).filePath(QStringLiteral("AccountSet_account_type.json"));
    const QString aaIdJsonPath = QDir(workDir.path()).filePath(QStringLiteral("AccountSet_aa_id.json"));
    const QString patchedJsonPath = QDir(workDir.path()).filePath(QStringLiteral("AccountSet_patched.json"));
    const QString entryNamePath = QDir(workDir.path()).filePath(QStringLiteral("entry_name.txt"));

    AppLogger::operation(QStringLiteral("\u4e0a\u4f20\u524d\u5c06 AccountSet.json \u8f6c\u4e3a\u4f01\u4e1a\u4e13\u4e1a\u7248 zip=%1 target=%2 serviceId=%3 AccountSetType=1 AAId=0")
        .arg(QDir::toNativeSeparators(sourceZipPath), QDir::toNativeSeparators(targetZipPath))
        .arg(proServiceId));

    if (!extractAccountSetJsonFromZip(targetAbs, extractedJsonPath, entryNamePath, errorMessage))
        return false;

    QFile entryNameFile(entryNamePath);
    if (!entryNameFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("read AccountSet.json entry name failed: ") + entryNameFile.errorString();
        return false;
    }
    const QString entryName = QString::fromUtf8(entryNameFile.readAll()).trimmed();
    if (entryName.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("AccountSet.json entry name is empty");
        return false;
    }

    if (!patchIntegerFieldInJsonFile(extractedJsonPath, accountTypeJsonPath,
                                     QByteArrayLiteral("AccountSetType"), 1, errorMessage))
        return false;
    if (!patchIntegerFieldInJsonFile(accountTypeJsonPath, aaIdJsonPath,
                                     QByteArrayLiteral("AAId"), 0, errorMessage))
        return false;
    if (!patchIntegerFieldInJsonFile(aaIdJsonPath, patchedJsonPath,
                                     QByteArrayLiteral("ProServiceId"), proServiceId, errorMessage))
        return false;

    if (!updateAccountSetJsonInZip(targetAbs, entryName, patchedJsonPath, errorMessage))
        return false;

    return true;
}
}

ImportPackageUploadResult ImportPackageUploader::upload(const ImportPackageUploadRequest &request)
{
    ImportPackageUploadResult result;
    const QFileInfo zipInfo(request.zipFilePath);
    result.fileSize = zipInfo.size();

    const QString asName = request.asName.isEmpty() ? zipInfo.completeBaseName() : request.asName;
    result.logs.append(QStringLiteral("上传文件：") + QDir::toNativeSeparators(request.zipFilePath));
    result.logs.append(QStringLiteral("文件大小：%1 bytes").arg(result.fileSize));

    AppLogger::operation(QStringLiteral("开始上传专业版账套压缩包 asId=%1 asName=%2 zip=%3 bytes=%4 serviceId=%5 rewriteProServiceId=%6")
        .arg(request.asId, asName, QDir::toNativeSeparators(request.zipFilePath))
        .arg(result.fileSize)
        .arg(request.serviceId)
        .arg(request.rewriteProServiceId));

    if (!zipInfo.exists() || !zipInfo.isFile())
    {
        result.errorMessage = QStringLiteral("压缩包不存在：") + QDir::toNativeSeparators(request.zipFilePath);
        return result;
    }

    if (request.accessToken.isEmpty())
    {
        result.errorMessage = QStringLiteral("未登录或 access token 为空");
        return result;
    }

    QString uploadZipPath = zipInfo.absoluteFilePath();
    QScopedPointer<QTemporaryDir> patchedZipDir;
    if (request.rewriteProServiceId)
    {
        patchedZipDir.reset(new QTemporaryDir(QDir::tempPath() + "/lemon_import_upload_XXXXXX"));
        if (!patchedZipDir->isValid())
        {
            result.errorMessage = QStringLiteral("\u521b\u5efa\u4e34\u65f6\u538b\u7f29\u5305\u76ee\u5f55\u5931\u8d25");
            return result;
        }

        const QString patchedZipPath = QDir(patchedZipDir->path()).filePath(zipInfo.fileName());
        QString patchError;
        if (!rewriteAccountSetJsonProServiceId(zipInfo.absoluteFilePath(), patchedZipPath, request.serviceId, &patchError))
        {
            result.errorMessage = patchError;
            return result;
        }

        uploadZipPath = patchedZipPath;
        result.fileSize = QFileInfo(uploadZipPath).size();
        result.logs.append(QStringLiteral("\u4e0a\u4f20\u524d\u5df2\u8f6c\u4e3a\u4f01\u4e1a\u4e13\u4e1a\u7248\uff1aAccountSetType=1\uff0cAAId=0\uff0cProServiceId=%1")
            .arg(request.serviceId));
        result.logs.append(QStringLiteral("\u4e34\u65f6\u538b\u7f29\u5305\u5927\u5c0f\uff1a%1 bytes").arg(result.fileSize));
    }

    QString importerApiHost = request.importerApiHost.trimmed();
    if (importerApiHost.isEmpty())
    {
        lemoncofighelper configHelper;
        importerApiHost = configHelper.GetAccountUrlByKey(hostKeyForRequest(request));
    }

    QUrl importUrl;
    QString reason;
    if (!lemoncofighelper::TryBuildUrl(importerApiHost, lemoncofighelper::kImportAccountSetFileAsyncPath, importUrl, reason))
    {
        result.errorMessage = QStringLiteral("上传地址无效：") + reason;
        return result;
    }

    result.logs.append(QStringLiteral("上传地址：") + importUrl.toDisplayString());

    QNetworkRequest networkRequest;
    networkRequest.setUrl(importUrl);
    CHttpRequestHelper::configureSsl(networkRequest);
    CHttpRequestHelper::setInternalSessionHeader(networkRequest);
    networkRequest.setRawHeader("Authorization", (QStringLiteral("Bearer ") + request.accessToken).toUtf8());
    networkRequest.setHeader(QNetworkRequest::UserAgentHeader, "lemonClient");
    if (!request.ningMengCookie.isEmpty())
        networkRequest.setRawHeader("Cookie", ("ningmengcookie=" + request.ningMengCookie).toUtf8());

    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
        QVariant(QStringLiteral("form-data; name=\"%1\"; filename=\"%1.zip\"").arg(request.zipFileName)));

    QFile *file = new QFile(uploadZipPath);
    file->setParent(multiPart);
    if (!file->open(QIODevice::ReadOnly))
    {
        result.errorMessage = QStringLiteral("打开压缩包失败：") + file->errorString();
        delete multiPart;
        return result;
    }

    filePart.setBodyDevice(file);
    multiPart->append(filePart);

    QNetworkAccessManager networkAccessManager;
    QNetworkReply *reply = networkAccessManager.post(networkRequest, multiPart);
    multiPart->setParent(reply);

    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);

    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, &QTimer::timeout, [&]() {
        if (reply && !reply->isFinished())
            reply->abort();
        eventLoop.quit();
    });
    timeoutTimer.start(request.timeoutMs);
    eventLoop.exec();

    const bool timedOut = !timeoutTimer.isActive();
    timeoutTimer.stop();

    result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray responseData = reply->readAll();
    const QString responseText = QString::fromUtf8(responseData).left(500);

    if (timedOut)
    {
        result.errorMessage = QStringLiteral("上传超时（%1 秒）").arg(request.timeoutMs / 1000);
        delete reply;
        return result;
    }

    if (result.httpStatus == 401 || result.httpStatus == 403)
    {
        result.errorMessage = QStringLiteral("上传鉴权失败，HTTP %1：%2")
            .arg(result.httpStatus)
            .arg(responseText.isEmpty() ? reply->errorString() : responseText);
        delete reply;
        return result;
    }

    if (reply->error() != QNetworkReply::NoError)
    {
        result.errorMessage = QStringLiteral("网络错误：") + reply->errorString();
        delete reply;
        return result;
    }

    delete reply;

    if (result.httpStatus != 200)
    {
        result.errorMessage = QStringLiteral("接口返回 HTTP %1：%2")
            .arg(result.httpStatus)
            .arg(QString::fromUtf8(responseData).left(500));
        return result;
    }

    QJsonParseError jsonError;
    const QJsonDocument document = QJsonDocument::fromJson(responseData, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !document.isObject())
    {
        result.errorMessage = QStringLiteral("解析接口返回值失败：") + jsonError.errorString();
        return result;
    }

    const QJsonObject obj = document.object();
    result.responseCode = obj.value("Code").toInt(-1);
    const QString resMessage = obj.value("Message").toString();
    result.importId = obj.value("Data").toString();

    if (result.responseCode != 0)
    {
        result.errorMessage = resMessage.isEmpty()
            ? QStringLiteral("服务端返回错误 Code=%1").arg(result.responseCode)
            : resMessage;
        return result;
    }

    result.ok = true;
    result.logs.append(QStringLiteral("上传成功，importId：") + result.importId);
    ImportPackageHistory::recordSuccessfulUpload(
        request.zipFilePath,
        result.importId,
        request.serviceId,
        importerApiHost);
    AppLogger::operation(QStringLiteral("账套压缩包上传成功 asId=%1 asName=%2 importId=%3")
        .arg(request.asId, asName, result.importId));
    return result;
}
