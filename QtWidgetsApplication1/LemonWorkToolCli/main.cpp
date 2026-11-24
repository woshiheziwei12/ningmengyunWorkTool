#include "accountsetservice.h"
#include "authservice.h"
#include "backupservice.h"
#include "cliarguments.h"
#include "cliresult.h"
#include "masking.h"
#include "sessionstore.h"
#include "toolcommands.h"


#include "Common/logger.h"
#include "Common/LocalToolEndpoint.h"
#include "lemoncofighelper.h"

#include <QApplication>
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
#include <memory>

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
        "accountsets backup"
    };
    for (const QString &name : names)
        commands.append(name);
    const QJsonArray details = toolCommandHelp();
    for (const auto &detail : details) commands.append(detail.toObject().value("name"));

    QJsonObject globals;
    globals["profile"] = "--profile default";
    globals["httpEnv"] = "--http-env Dev2";
    globals["session"] = "--session D:\\path\\session.json";
    globals["pretty"] = "--pretty";
    globals["timeoutMs"] = "--timeout-ms 120000";

    QJsonObject data;
    data["commands"] = commands;
    data["commandDetails"] = details;
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

    if (first.isEmpty() || (first == "help" && pos.size() == 1))
        return CliResult::success("help", helpData());
    if (first == "help")
    {
        const QString name = pos.mid(1).join(' ');
        const QJsonArray details = toolCommandHelp();
        for (const auto &detail : details)
            if (detail.toObject().value("name").toString() == name)
                return CliResult::success("help", detail.toObject());
        return CliResult::failure("help", "COMMAND_HELP_NOT_FOUND",
                                  QStringLiteral("没有该命令的专项帮助，请使用 help 查看命令列表。"), ExitArgumentError);
    }
    if (first == "wecom" || first == "access" || first == "packages" || first == "bak")
        return runToolCommand(args);
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


    return CliResult::failure(first.isEmpty() ? "unknown" : args.commandPath(),
                              "NOT_IMPLEMENTED",
                              QStringLiteral("命令尚未实现或不存在：") + args.commandPath(),
                              ExitNotImplemented);
}
}

int main(int argc, char *argv[])
{
    // Only the commands that can host WebView2 need a Windows widget application.
    QStringList startupArguments;
    for (int i = 0; i < argc; ++i) startupArguments.append(QString::fromLocal8Bit(argv[i]));
    const QStringList command = CliArguments::parse(startupArguments).positionals();
    const bool browserCommand = command == QStringList{"wecom", "read"} || command == QStringList{"wecom", "login"};
    std::unique_ptr<QCoreApplication> app;
    if (browserCommand) {
        QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
        app = std::make_unique<QApplication>(argc, argv);
        QApplication::setQuitOnLastWindowClosed(false);
    } else {
        app = std::make_unique<QCoreApplication>(argc, argv);
    }
    QCoreApplication::setApplicationName("LemonWorkToolCli");
    QCoreApplication::setApplicationVersion("0.1.0");

    AppLogger::initialize();
    QElapsedTimer timer;
    timer.start();

    const CliArguments args = CliArguments::parse(app->arguments());
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
    if (args.positionals() == QStringList{"wecom", "read"} && args.has("legacy-json")) {
        QJsonObject response{{"protocolVersion", LocalToolEndpoint::ProtocolVersion}, {"ok", result.ok}};
        if (result.ok) {
            response["data"] = result.data;
        } else {
            const QString loginCommand = result.errorDetails.value("loginCommand").toString();
            const QString message = loginCommand.isEmpty() ? result.errorMessage : result.errorMessage + ' ' + loginCommand;
            response["error"] = QJsonObject{{"code", result.errorCode.toLower()}, {"message", message}};
        }
        printJson(response, false);
        AppLogger::shutdown();
        return result.ok ? 0 : 2;
    }
    printJson(result.toJson(), args.has("pretty"));

    const int exitCode = result.exitCode;
    AppLogger::shutdown();
    return exitCode;
}
