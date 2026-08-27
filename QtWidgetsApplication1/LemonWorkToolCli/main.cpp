#include "accountsetservice.h"
#include "authservice.h"
#include "backupservice.h"
#include "cliarguments.h"
#include "cliresult.h"
#include "masking.h"
#include "sessionstore.h"

#include "Common/importpackageuploader.h"
#include "Common/logger.h"
#include "lemoncofighelper.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <exception>

namespace
{
int timeoutMs(const CliArguments &args)
{
    bool ok = false;
    const int value = args.value("timeout-ms", "120000").toInt(&ok);
    return ok && value > 0 ? value : 120000;
}

CliResult serviceFailure(const QString &command, const ServiceResult &serviceResult)
{
    return CliResult::failure(command, serviceResult.errorCode, serviceResult.errorMessage,
                              serviceResult.exitCode, serviceResult.details);
}

CliResult requireSession(const QString &command, const SessionStore &store, SessionInfo *session)
{
    QString error;
    if (!store.load(session, &error))
        return CliResult::failure(command, "SESSION_ERROR", error, ExitAuthError);
    if (!session->hasAuth())
        return CliResult::failure(command, "AUTH_REQUIRED", QStringLiteral("请先登录，或通过 --session 指定会话文件"), ExitAuthError);
    return CliResult::success(command);
}

QJsonObject versionData()
{
    QJsonObject build;
    build["time"] = QStringLiteral(__DATE__ " " __TIME__);

    QJsonObject data;
    data["name"] = QStringLiteral("LemonWorkToolCli");
    data["version"] = QStringLiteral("0.1.0");
    data["qtVersion"] = QStringLiteral(QT_VERSION_STR);
    data["build"] = build;
    return data;
}

QJsonObject helpData()
{
    QJsonArray commands;
    const QStringList names = {
        "version",
        "help",
        "config show",
        "auth status",
        "auth cookie",
        "auth sms-send",
        "auth login-sms",
        "auth login-password",
        "auth refresh",
        "auth logout",
        "accountsets plans",
        "accountsets list",
        "accountsets delete",
        "accountsets backup",
        "packages upload"
    };
    for (const QString &name : names)
        commands.append(name);

    QJsonObject globals;
    globals["profile"] = "--profile default";
    globals["httpEnv"] = "--http-env Dev2";
    globals["session"] = "--session D:\\path\\session.json";
    globals["pretty"] = "--pretty";
    globals["timeoutMs"] = "--timeout-ms 120000";

    QJsonObject data;
    data["commands"] = commands;
    data["globalOptions"] = globals;
    data["output"] = QStringLiteral("stdout 始终输出单个 JSON 对象");
    return data;
}

QJsonObject configData(const CliArguments &args, const SessionStore &store)
{
    lemoncofighelper configHelper;

    QJsonObject data;
    data["applicationDir"] = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    data["httpEnv"] = lemoncofighelper::CurrentHttpEnvironment();
    data["httpConfigRoot"] = QDir::toNativeSeparators(configHelper.m_strConfigRoot);
    data["httpConfigFile"] = QDir::toNativeSeparators(configHelper.m_strConfigRoot + configHelper.m_strAccountJsonName);
    data["profile"] = store.profile();
    data["sessionPath"] = QDir::toNativeSeparators(store.sessionPath());
    data["timeoutMs"] = timeoutMs(args);
    return data;
}

ImportPackageServiceType serviceTypeFromText(const QString &text)
{
    Q_UNUSED(text);
    return ImportPackageServiceType::Professional;
}

CliResult runPackagesUpload(const CliArguments &args, const SessionStore &store)
{
    const QString command = "packages.upload";
    const QString input = args.value("input");
    if (input.isEmpty())
        return CliResult::failure(command, "ARGUMENT_ERROR", QStringLiteral("缺少 --input"), ExitArgumentError);

    const QFileInfo zipInfo(input);
    if (!zipInfo.exists() || !zipInfo.isFile())
        return CliResult::failure(command, "FILE_ERROR", QStringLiteral("输入文件不存在：") + QDir::toNativeSeparators(input), ExitFileError);
    if (zipInfo.suffix().compare("zip", Qt::CaseInsensitive) != 0)
        return CliResult::failure(command, "FILE_ERROR", QStringLiteral("packages upload 仅支持 ZIP 文件"), ExitFileError);

    SessionInfo session;
    CliResult sessionCheck = requireSession(command, store, &session);
    if (!sessionCheck.ok)
        return sessionCheck;

    bool serviceIdOk = false;
    const int explicitServiceId = args.value("service-id").toInt(&serviceIdOk);
    const int serviceId = serviceIdOk && explicitServiceId > 0 ? explicitServiceId : session.serviceId;

    ImportPackageUploadRequest request;
    request.asId = args.value("as-id");
    request.asName = args.value("as-name", zipInfo.completeBaseName());
    request.zipFileName = args.value("zip-file-name", zipInfo.completeBaseName());
    request.zipFilePath = zipInfo.absoluteFilePath();
    request.accessToken = args.value("access-token", session.ningmengCookie);
    request.ningMengCookie = args.value("ningmeng-cookie", session.ningmengCookie);
    request.serviceId = serviceId;
    request.rewriteProServiceId = true;
    request.timeoutMs = timeoutMs(args);
    request.serviceType = serviceTypeFromText(args.value("edition"));

    const ImportPackageUploadResult uploadResult = ImportPackageUploader::upload(request);
    if (!uploadResult.ok)
    {
        QJsonObject details;
        details["httpStatus"] = uploadResult.httpStatus;
        details["apiCode"] = uploadResult.responseCode;
        details["fileSize"] = QString::number(uploadResult.fileSize).toDouble();
        return CliResult::failure(command, "UPLOAD_FAILED", uploadResult.errorMessage, ExitServerError, details);
    }

    QJsonObject data;
    data["input"] = QDir::toNativeSeparators(zipInfo.absoluteFilePath());
    data["fileSize"] = QString::number(uploadResult.fileSize).toDouble();
    data["httpStatus"] = uploadResult.httpStatus;
    data["apiCode"] = uploadResult.responseCode;
    data["importId"] = uploadResult.importId;
    return CliResult::success(command, data);
}

CliResult runLoginPassword(const CliArguments &args, const SessionStore &store)
{
    const QString command = "auth.login-password";
    const QString username = args.value("username");
    const QString password = args.value("password");
    AuthService auth(timeoutMs(args));

    SessionInfo session;
    QString loadError;
    if (!store.load(&session, &loadError))
        return CliResult::failure(command, "SESSION_ERROR", loadError, ExitAuthError);

    ServiceResult serviceResult = auth.loginPassword(username, password, &session);
    if (!serviceResult.ok)
        return serviceFailure(command, serviceResult);

    bool saved = false;
    if (args.has("save-session") || !args.has("no-save-session"))
    {
        QString saveError;
        if (!store.save(session, &saveError))
            return CliResult::failure(command, "SESSION_ERROR", saveError, ExitAuthError);
        saved = true;
    }

    QJsonObject data = session.toJson(false);
    data["sessionSaved"] = saved;
    data["mobileMasked"] = maskMobile(username);
    return CliResult::success(command, data);
}

CliResult runAuthRefresh(const CliArguments &args, const SessionStore &store)
{
    const QString command = "auth.refresh";
    SessionInfo session;
    QString loadError;
    if (!store.load(&session, &loadError))
        return CliResult::failure(command, "SESSION_ERROR", loadError, ExitAuthError);

    const QString cookie = args.value("ningmeng-cookie", session.ningmengCookie);
    AuthService auth(timeoutMs(args));
    ServiceResult serviceResult = auth.refreshFromCookie(cookie, &session);
    if (!serviceResult.ok)
        return serviceFailure(command, serviceResult);

    bool saved = false;
    if (args.has("save-session") || !args.has("no-save-session"))
    {
        QString saveError;
        if (!store.save(session, &saveError))
            return CliResult::failure(command, "SESSION_ERROR", saveError, ExitAuthError);
        saved = true;
    }

    QJsonObject data = session.toJson(false);
    data["sessionSaved"] = saved;
    return CliResult::success(command, data);
}

CliResult runSmsSend(const CliArguments &args)
{
    const QString command = "auth.sms-send";
    AuthService auth(timeoutMs(args));
    const ServiceResult serviceResult = auth.sendSms(args.value("mobile"));
    if (!serviceResult.ok)
        return serviceFailure(command, serviceResult);
    return CliResult::success(command, serviceResult.data);
}

CliResult runLoginSms(const CliArguments &args, const SessionStore &store)
{
    const QString command = "auth.login-sms";
    SessionInfo session;
    QString loadError;
    if (!store.load(&session, &loadError))
        return CliResult::failure(command, "SESSION_ERROR", loadError, ExitAuthError);

    AuthService auth(timeoutMs(args));
    const ServiceResult serviceResult = auth.loginSms(
        args.value("mobile"),
        args.value("code"),
        args.value("stamp"),
        args.value("timestamp"),
        &session);
    if (!serviceResult.ok)
        return serviceFailure(command, serviceResult);

    bool saved = false;
    if (args.has("save-session") || !args.has("no-save-session"))
    {
        QString saveError;
        if (!store.save(session, &saveError))
            return CliResult::failure(command, "SESSION_ERROR", saveError, ExitAuthError);
        saved = true;
    }

    QJsonObject data = session.toJson(false);
    data["sessionSaved"] = saved;
    data["mobileMasked"] = maskMobile(args.value("mobile"));
    return CliResult::success(command, data);
}

CliResult runAccountSetDelete(const CliArguments &args, const SessionStore &store)
{
    const QString command = "accountsets.delete";
    if (!args.has("yes"))
        return CliResult::failure(command, "CONFIRM_REQUIRED", QStringLiteral("删除账套必须显式传入 --yes"), ExitArgumentError);

    QList<int> asIds;
    for (const QString &value : args.values("as-id"))
    {
        bool ok = false;
        const int asId = value.toInt(&ok);
        if (ok && asId > 0)
            asIds.append(asId);
    }
    if (asIds.isEmpty())
        return CliResult::failure(command, "ARGUMENT_ERROR", QStringLiteral("缺少有效的 --as-id"), ExitArgumentError);

    SessionInfo session;
    CliResult sessionCheck = requireSession(command, store, &session);
    if (!sessionCheck.ok)
        return sessionCheck;

    AccountSetService service(timeoutMs(args));
    const ServiceResult serviceResult = service.deleteAccountSets(session, asIds);
    if (!serviceResult.ok)
        return serviceFailure(command, serviceResult);
    return CliResult::success(command, serviceResult.data);
}

CliResult runAccountSetBackup(const CliArguments &args, const SessionStore &store)
{
    const QString command = "accountsets.backup";
    QList<int> asIds;
    for (const QString &value : args.values("as-id"))
    {
        bool ok = false;
        const int asId = value.toInt(&ok);
        if (ok && asId > 0)
            asIds.append(asId);
    }
    if (asIds.isEmpty())
        return CliResult::failure(command, "ARGUMENT_ERROR", QStringLiteral("缺少有效的 --as-id"), ExitArgumentError);

    bool pollOk = false;
    const int pollIntervalMs = args.value("poll-interval-ms", "3000").toInt(&pollOk);
    bool maxWaitOk = false;
    const int maxWaitMs = args.value("max-wait-ms", "600000").toInt(&maxWaitOk);

    SessionInfo session;
    CliResult sessionCheck = requireSession(command, store, &session);
    if (!sessionCheck.ok)
        return sessionCheck;

    BackupService service(timeoutMs(args));
    const ServiceResult serviceResult = service.backupAccountSets(
        session,
        asIds,
        args.value("out"),
        pollOk && pollIntervalMs > 0 ? pollIntervalMs : 3000,
        maxWaitOk && maxWaitMs > 0 ? maxWaitMs : 600000);
    if (!serviceResult.ok)
        return serviceFailure(command, serviceResult);
    return CliResult::success(command, serviceResult.data);
}

CliResult route(const CliArguments &args)
{
    const QStringList pos = args.positionals();
    const QString first = pos.value(0);
    const QString second = pos.value(1);
    SessionStore store(args);

    if (first.isEmpty() || first == "help")
        return CliResult::success("help", helpData());
    if (first == "version")
        return CliResult::success("version", versionData());
    if (first == "config" && second == "show")
        return CliResult::success("config.show", configData(args, store));

    if (first == "auth" && second == "status")
    {
        SessionInfo session;
        QString error;
        if (!store.load(&session, &error))
            return CliResult::failure("auth.status", "SESSION_ERROR", error, ExitAuthError);
        QJsonObject data = store.statusJson();
        data["session"] = session.toJson(false);
        data["authenticated"] = session.hasAuth();
        return CliResult::success("auth.status", data);
    }

    if (first == "auth" && second == "cookie")
    {
        SessionInfo session;
        CliResult sessionCheck = requireSession("auth.cookie", store, &session);
        if (!sessionCheck.ok)
            return sessionCheck;
        QJsonObject data;
        data["profile"] = store.profile();
        data["ningmengCookie"] = args.has("reveal") ? session.ningmengCookie : maskSecret(session.ningmengCookie);
        data["revealed"] = args.has("reveal");
        return CliResult::success("auth.cookie", data);
    }

    if (first == "auth" && second == "logout")
    {
        QString error;
        if (!store.remove(&error))
            return CliResult::failure("auth.logout", "SESSION_ERROR", error, ExitAuthError);
        QJsonObject data;
        data["profile"] = store.profile();
        data["sessionPath"] = QDir::toNativeSeparators(store.sessionPath());
        data["removed"] = true;
        return CliResult::success("auth.logout", data);
    }

    if (first == "auth" && second == "login-password")
        return runLoginPassword(args, store);

    if (first == "auth" && second == "sms-send")
        return runSmsSend(args);

    if (first == "auth" && second == "login-sms")
        return runLoginSms(args, store);

    if (first == "auth" && second == "refresh")
        return runAuthRefresh(args, store);

    if (first == "accountsets" && (second == "plans" || second == "list"))
    {
        SessionInfo session;
        CliResult sessionCheck = requireSession("accountsets." + second, store, &session);
        if (!sessionCheck.ok)
            return sessionCheck;

        AccountSetService service(timeoutMs(args));
        const ServiceResult serviceResult = second == "plans"
            ? service.listPlans(session)
            : service.listAccountSets(session);
        if (!serviceResult.ok)
            return serviceFailure("accountsets." + second, serviceResult);
        return CliResult::success("accountsets." + second, serviceResult.data);
    }

    if (first == "accountsets" && second == "delete")
        return runAccountSetDelete(args, store);

    if (first == "accountsets" && second == "backup")
        return runAccountSetBackup(args, store);

    if (first == "packages" && second == "upload")
        return runPackagesUpload(args, store);

    return CliResult::failure(first.isEmpty() ? "unknown" : args.commandPath(),
                              "NOT_IMPLEMENTED",
                              QStringLiteral("命令尚未实现或不存在：") + args.commandPath(),
                              ExitNotImplemented);
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("LemonWorkToolCli");
    QCoreApplication::setApplicationVersion("0.1.0");

    AppLogger::initialize();
    QElapsedTimer timer;
    timer.start();

    const CliArguments args = CliArguments::parse(app.arguments());
    CliResult result;
    try
    {
        result = route(args);
    }
    catch (const std::exception &ex)
    {
        result = CliResult::failure("unknown", "UNKNOWN_ERROR",
                                    QString::fromLocal8Bit(ex.what()), ExitUnknownError);
    }
    catch (...)
    {
        result = CliResult::failure("unknown", "UNKNOWN_ERROR",
                                    QStringLiteral("未知异常"), ExitUnknownError);
    }

    result.durationMs = timer.elapsed();
    result.logPath = QDir::toNativeSeparators(AppLogger::logFilePath());
    printJson(result.toJson(), args.has("pretty"));

    const int exitCode = result.exitCode;
    AppLogger::shutdown();
    return exitCode;
}
