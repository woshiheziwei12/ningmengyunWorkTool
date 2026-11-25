#include "toolcommands.h"
#include "cliarguments.h"
#include "cliresult.h"
#include "Common/WecomBrowser.h"
#include "Common/WecomDocumentCatalog.h"
#include "Common/WecomDocumentReader.h"
#include "Common/LocalToolEndpoint.h"
#include "Common/access2003converter.h"

#include <QDir>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QThread>
#include <QUuid>
#include <QVBoxLayout>

namespace {
constexpr int ConnectTimeoutMs = 2000;
constexpr int QueryTimeoutMs = 5000;
constexpr int ReadTimeoutMs = 310000;
constexpr int LoginTimeoutMs = 180000;
constexpr int MaxTimeoutMs = 600000;
constexpr int PackageQueryTimeoutMs = 35000;
constexpr int PackageWaitTimeoutMs = 300000;
constexpr int PackagePollIntervalMs = 3000;
constexpr int BakWaitTimeoutMs = 310000;

// Checks arguments for the new commands without changing older CLI callers.
QString validateOptions(const CliArguments &args, const QStringList &flags, const QStringList &options)
{
    for (const auto &name : args.optionNames()) {
        if (!flags.contains(name) && !options.contains(name) && name != "pretty" && name != "http-env")
            return QStringLiteral("不支持参数 --%1").arg(name);
        if (options.contains(name) && args.hasImplicitValue(name))
            return QStringLiteral("--%1 需要参数值").arg(name);
        if (flags.contains(name) && !args.hasImplicitValue(name))
            return QStringLiteral("--%1 是开关，无需参数值").arg(name);
        if (name != "input" && args.values(name).size() > 1)
            return QStringLiteral("--%1 只能指定一次").arg(name);
    }
    return {};
}

// Maps pipe errors to stable CLI codes and the existing exit code categories.
CliResult localFailure(const QString &command, const QString &code, const QString &message, const QJsonObject &details = {})
{
    int exitCode = ExitServerError;
    if (code == "login_required" || code == "access_denied" || code == "session_save_failed"
        || code == "auth_required" || code == "environment_mismatch" || code == "importer_mismatch") exitCode = ExitAuthError;
    else if (code == "invalid_date" || code == "invalid_document" || code == "invalid_url" || code == "document_not_found"
        || code == "invalid_request" || code == "invalid_environment" || code == "invalid_import_id"
        || code == "invalid_backup_set" || code == "invalid_sql_instance" || code == "bak_catalog_not_found"
        || code == "invalid_platform" || code == "instance_mismatch") exitCode = ExitArgumentError;
    else if (code == "file_error" || code == "catalog_error") exitCode = ExitFileError;
    else if (code == "unsupported_action" || code == "unsupported_version") exitCode = ExitNotImplemented;
    else if (code == "response_too_large" || code == "invalid_response") exitCode = ExitDataError;
    else if (code == "tool_not_running" || code == "read_timeout" || code == "tool_disconnected"
        || code == "connection_failed" || code == "result_wait_timeout"
        || code == "upload_state_unknown" || code == "restore_state_unknown")
        exitCode = ExitNetworkError;
    return CliResult::failure(command, code.toUpper(), message, exitCode, details);
}

// Sends one UTF-8 JSON request to the same user's running desktop tool.
CliResult callLocalTool(const QString &command, const QString &service, const QJsonObject &request, int timeout)
{
    const auto endpoint = LocalToolEndpoint::name(service);
    if (endpoint.isEmpty())
        return localFailure(command, "access_denied", QStringLiteral("无法获取当前 Windows 用户的工作助手接口。"));

    QElapsedTimer timer;
    timer.start();
    QLocalSocket socket;
    socket.setReadBufferSize(LocalToolEndpoint::MaxResponseBytes + 1);
    socket.connectToServer(endpoint);
    if (socket.state() != QLocalSocket::ConnectedState && !socket.waitForConnected(qMin(timeout, ConnectTimeoutMs))) {
        if (socket.error() == QLocalSocket::SocketAccessError)
            return localFailure(command, "access_denied", QStringLiteral("请在与桌面工具相同的 Windows 用户下运行命令。"));
        return localFailure(command, "tool_not_running", QStringLiteral("打开新版桌面工具并进入工作台后重试，工具可以缩到托盘。"));
    }

    const QByteArray frame = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    if (socket.write(frame) != frame.size())
        return localFailure(command, "connection_failed", QStringLiteral("无法向桌面工具发送请求。"));
    socket.flush();

    QByteArray response;
    for (;;) {
        response += socket.readAll();
        const int newline = response.indexOf('\n');
        if ((newline < 0 ? response.size() : newline) > LocalToolEndpoint::MaxResponseBytes)
            return localFailure(command, "response_too_large", QStringLiteral("读取结果超过本机接口上限。"));
        if (newline >= 0) {
            response.truncate(newline);
            break;
        }
        if (socket.state() == QLocalSocket::UnconnectedState)
            return localFailure(command, "tool_disconnected", QStringLiteral("桌面工具在返回结果前已断开。"));
        const int remaining = timeout - int(timer.elapsed());
        if (remaining <= 0)
            return localFailure(command, "read_timeout", QStringLiteral("等待结果超时，请查看桌面工具的任务进度。"));
        socket.waitForReadyRead(remaining);
    }

    const auto data = QJsonDocument::fromJson(response).object();
    if (data.value("protocolVersion") != QJsonValue(LocalToolEndpoint::ProtocolVersion) || !data.value("ok").isBool())
        return localFailure(command, "invalid_response", QStringLiteral("桌面工具未返回预期版本的 JSON 响应。"));
    if (data.value("ok").toBool() && data.value("data").isObject())
        return CliResult::success(command, data.value("data").toObject());
    const auto error = data.value("error").toObject();
    if (!data.value("ok").toBool() && !error.value("code").toString().isEmpty())
        return localFailure(command, error.value("code").toString(), error.value("message").toString(), error.value("details").toObject());
    return localFailure(command, "invalid_response", QStringLiteral("桌面工具返回的响应缺少业务数据。"));
}

// Hosts the same WebView2 reader as the GUI for the lifetime of a single command.
CliResult runStandaloneBrowser(const QString &command, const WecomDocument::Document &document, const QString &loginCommand,
                               const QDate &date, int timeout)
{
    const bool login = command == "wecom.login";
    QDialog window;
    window.setWindowTitle(QStringLiteral("企业微信文档 · 扫码登录"));
    window.resize(1000, 720);
    auto *layout = new QVBoxLayout(&window);
    WecomBrowser browser(&window);
    layout->addWidget(&browser);
    layout->activate();
    WecomDocumentReader reader(&browser);
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    CliResult result;
    bool completed = false;
    auto finish = [&](const CliResult &response) {
        if (completed) return;
        completed = true;
        result = response;
        loop.quit();
    };
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&]() {
        finish(localFailure(command, "read_timeout", QStringLiteral("等待企业微信操作超时，请检查网络或重新扫码。")));
    });
    QObject::connect(&reader, &WecomDocumentReader::failed, &loop, [&](const QString &message) {
        finish(localFailure(command, "read_failed", message));
    });
    QObject::connect(&reader, &WecomDocumentReader::loginRequired, &loop, [&]() {
        if (login) return;
        finish(localFailure(command, "login_required", QStringLiteral("文档授权尚未通过验证，请运行扫码命令并确认账号有访问权限。"),
                            {{"loginCommand", loginCommand}}));
    });
    QObject::connect(&reader, &WecomDocumentReader::signInCompleted, &loop, [&](const QString &error) {
        if (!error.isEmpty()) finish(localFailure(command, "session_save_failed", error));
        else finish(CliResult::success(command, {{"saved", true}, {"documentId", WecomDocument::documentIdFromUrl(document.url)}}));
    });
    QObject::connect(&reader, &WecomDocumentReader::finished, &loop, [&](const WecomDocument::Snapshot &snapshot) {
        const auto data = snapshot.toJson();
        if (QJsonDocument(data).toJson(QJsonDocument::Compact).size() > LocalToolEndpoint::MaxResponseBytes)
            finish(localFailure(command, "response_too_large", QStringLiteral("读取结果超过 64 MiB。")));
        else finish(CliResult::success(command, data));
    });
    QObject::connect(&window, &QDialog::rejected, &loop, [&]() {
        finish(localFailure(command, "login_required", QStringLiteral("已关闭扫码窗口，登录未完成。")));
    });

    deadline.start(timeout);
    QTimer::singleShot(0, &loop, [&]() {
        if (login) {
            window.show();
            reader.signIn(document);
        } else {
            reader.read(document, date, WecomDocumentReader::LoginPrompt::AfterPageLoad);
        }
    });
    loop.exec();
    return result;
}

// Uses the desktop bridge when available and starts WebView2 locally when needed.
CliResult runWecomCommand(const CliArguments &args, const QString &command)
{
    const bool read = command == "wecom.read";
    const bool login = command == "wecom.login";
    const bool status = command == "wecom.status";
    QStringList options{"timeout-ms"};
    QStringList flags;
    if (read || login) {
        options << "document" << "url";
        flags << "url-stdin";
    }
    if (read) options << "from-date";
    if (read) flags << "legacy-json";
    const QString error = validateOptions(args, flags, options);
    if (!error.isEmpty()) return CliResult::failure(command, "ARGUMENT_ERROR", error, ExitArgumentError);

    bool validTimeout = false;
    const int defaultTimeout = read ? ReadTimeoutMs : (login ? LoginTimeoutMs : QueryTimeoutMs);
    const int timeout = args.value("timeout-ms", QString::number(defaultTimeout)).toInt(&validTimeout);
    if (!validTimeout || timeout < 1 || timeout > MaxTimeoutMs)
        return CliResult::failure(command, "ARGUMENT_ERROR", QStringLiteral("--timeout-ms 必须是 1 至 600000 的整数。"), ExitArgumentError);

    const bool stdinUrl = args.has("url-stdin");
    const bool manual = args.has("url") || stdinUrl;
    if (manual && (args.has("document") || (stdinUrl && args.has("url"))))
        return CliResult::failure(command, "ARGUMENT_ERROR", QStringLiteral("--document、--url 和 --url-stdin 只能选一种。"), ExitArgumentError);
    QUrl url;
    if (manual) {
        QString raw = args.value("url");
        if (stdinUrl) {
            QTextStream input(stdin, QIODevice::ReadOnly);
            input.setCodec("UTF-8");
            raw = input.readLine(LocalToolEndpoint::MaxRequestBytes + 1);
        }
        url = QUrl(raw.trimmed(), QUrl::StrictMode);
        QString urlError;
        if (raw.size() > LocalToolEndpoint::MaxRequestBytes || !WecomDocument::validateUrl(url, &urlError))
            return localFailure(command, "invalid_url", urlError.isEmpty() ? QStringLiteral("企业微信表格链接过长。") : urlError);
    }

    QString document = args.value("document", "project-progress");
    if (document == "project-progress") {
        document.clear();
    } else {
        static const QRegularExpression idPattern(QStringLiteral("\\A[A-Za-z0-9_-]+\\z"));
        if (!idPattern.match(document).hasMatch())
            return localFailure(command, "invalid_document", QStringLiteral("请传表格清单中的文档 ID。"));
    }

    QString action = status || login ? "status" : "documents";
    QJsonObject request{{"protocolVersion", LocalToolEndpoint::ProtocolVersion}};
    if (read) {
        if (manual) {
            action = "read_url";
            request["url"] = url.toString(QUrl::FullyEncoded);
        } else if (!document.isEmpty()) {
            action = "read_document";
            request["documentId"] = document;
        } else {
            action = "read";
        }
        if (args.has("from-date")) request["fromDate"] = args.value("from-date");
    }
    const auto date = WecomDocument::fromDate(request.value("fromDate"));
    if (!date.isValid())
        return localFailure(command, "invalid_date", QStringLiteral("日期必须为今天或之后，格式为 yyyy-MM-dd。"));
    request["action"] = action;
    if (manual && read && QJsonDocument(request).toJson(QJsonDocument::Compact).size() + 1 > LocalToolEndpoint::MaxRequestBytes)
        return localFailure(command, "invalid_url", QStringLiteral("企业微信表格链接过长。"));
    QElapsedTimer budget;
    budget.start();
    auto remote = callLocalTool(command, "Wecom", request, login ? qMin(timeout, QueryTimeoutMs) : timeout);
    if (remote.errorCode != "TOOL_NOT_RUNNING") {
        if (manual && read && (remote.errorCode == "INVALID_REQUEST" || remote.errorCode == "UNSUPPORTED_ACTION"))
            return localFailure(command, "unsupported_version", QStringLiteral("当前后台工作台不支持手动链接读取，请开启和 CLI 配套的新版工作助手。"));
        if (remote.ok && login)
            return localFailure(command, "busy", QStringLiteral("桌面工具正在运行，请在其企业微信文档页面扫码，或退出工具后执行此命令。"));
        if (remote.ok && status) remote.data["mode"] = "desktop";
        return remote;
    }
    if (status) {
        const QString profile = WecomBrowser::defaultProfileDirectory();
        const bool saved = !profile.isEmpty() && QFileInfo::exists(QDir(profile).filePath("session.dat"));
        return CliResult::success(command, {{"running", false}, {"mode", "standalone"}, {"savedSession", saved}});
    }
    WecomDocumentCatalog catalog;
    const QString catalogError = catalog.load();
    if (!catalogError.isEmpty() && (action == "documents" || manual || !document.isEmpty()))
        return localFailure(command, "catalog_error", catalogError);
    if (action == "documents") return CliResult::success(command, catalog.documentList());
    const auto selected = manual ? catalog.entryForUrl(url) : catalog.entryForRead(document);
    if (selected.url.isEmpty())
        return localFailure(command, "document_not_found", QStringLiteral("当前表格清单中没有此文档。"));
    const int remaining = timeout - int(budget.elapsed());
    if (remaining <= 0)
        return localFailure(command, "read_timeout", QStringLiteral("等待企业微信操作超时。"));
    QString loginHint = document.isEmpty() ? "wecom login" : "wecom login --document " + document;
    if (manual) loginHint = stdinUrl ? "wecom login --url-stdin" : QStringLiteral("wecom login --url '<原企业微信链接>'");
    return runStandaloneBrowser(command, selected, loginHint, date, remaining);
}

// Polls an accepted task without reissuing the request that created it.
CliResult waitForLocalTask(const QString &command, const QString &service, const QJsonObject &request,
                           int timeout, QJsonObject last)
{
    QElapsedTimer budget;
    budget.start();
    while (budget.elapsed() < timeout) {
        const int remaining = timeout - int(budget.elapsed());
        if (remaining <= 0) break;
        const auto response = callLocalTool(command, service, request, qMin(remaining, PackageQueryTimeoutMs));
        if (!response.ok) {
            if (remaining <= PackageQueryTimeoutMs && response.errorCode == "READ_TIMEOUT" && budget.elapsed() >= timeout) break;
            return response;
        }
        if (response.data.value("finished").toBool()) return response;
        last = response.data;
        const int pause = qMin(PackagePollIntervalMs, timeout - int(budget.elapsed()));
        if (pause > 0) QThread::msleep(static_cast<unsigned long>(pause));
    }
    return localFailure(command, "result_wait_timeout", QStringLiteral("在等待上限内未返回任务终态，请用原任务 ID 继续查询。"), last);
}

// Uploads or checks a professional ZIP via the active desktop workbench.
CliResult runPackagesCommand(const CliArguments &args, const QString &command)
{
    const bool upload = command == "packages.upload";
    const bool status = command == "packages.status";
    const auto flags = status ? QStringList{"wait"} : QStringList{};
    QStringList options{"http-env", "timeout-ms"};
    if (upload) options << "input" << "as-name" << "as-id" << "zip-file-name" << "edition" << "expected-importer";
    if (status) options << "import-id";
    const QString error = validateOptions(args, flags, options);
    if (!error.isEmpty()) return CliResult::failure(command, "ARGUMENT_ERROR", error, ExitArgumentError);
    QString environment = args.value("http-env");
    if (environment.compare("Pro", Qt::CaseInsensitive) == 0) environment = "Pro";
    else if (environment.compare("Dev2", Qt::CaseInsensitive) == 0) environment = "Dev2";
    else return localFailure(command, "invalid_environment", QStringLiteral("请显式指定 --http-env Pro 或 --http-env Dev2。"));

    const bool wait = args.has("wait");
    bool validTimeout = false;
    const int defaultTimeout = upload ? ReadTimeoutMs : (wait ? PackageWaitTimeoutMs : PackageQueryTimeoutMs);
    const int timeout = args.value("timeout-ms", QString::number(defaultTimeout)).toInt(&validTimeout);
    if (!validTimeout || timeout < 1 || timeout > MaxTimeoutMs)
        return CliResult::failure(command, "ARGUMENT_ERROR", QStringLiteral("--timeout-ms 必须是 1 至 600000 的整数。"), ExitArgumentError);

    QJsonObject request{{"protocolVersion", LocalToolEndpoint::ProtocolVersion}, {"environment", environment},
        {"action", command.section('.', -1)}};
    if (!upload && !status) return callLocalTool(command, "Packages", request, timeout);
    if (upload) {
        const QString path = args.value("input");
        if (path.trimmed().isEmpty() || args.values("input").size() != 1)
            return CliResult::failure(command, "ARGUMENT_ERROR", QStringLiteral("请指定一个 --input ZIP 路径。"), ExitArgumentError);
        const QFileInfo zip(path);
        if (!zip.isFile() || !zip.isReadable() || zip.suffix().compare("zip", Qt::CaseInsensitive) != 0)
            return localFailure(command, "file_error", QStringLiteral("请选择可读取的 ZIP 文件。"));
        const QString edition = args.value("edition", "professional").toLower();
        if (edition != "professional" && edition != "pro")
            return CliResult::failure(command, "ARGUMENT_ERROR", QStringLiteral("此命令支持专业版账套 ZIP。"), ExitArgumentError);
        request["input"] = zip.absoluteFilePath();
        if (args.has("as-name")) request["asName"] = args.value("as-name");
        if (args.has("as-id")) request["asId"] = args.value("as-id");
        if (args.has("zip-file-name")) request["zipFileName"] = args.value("zip-file-name");
        if (args.has("expected-importer")) request["expectedImporter"] = args.value("expected-importer");
        const auto response = callLocalTool(command, "Packages", request, timeout);
        if (response.errorCode == "READ_TIMEOUT" || response.errorCode == "TOOL_DISCONNECTED")
            return localFailure(command, "upload_state_unknown",
                QStringLiteral("上传受理状态未确认，请先核对工作助手的导入记录。"), {{"environment", environment}});
        return response;
    }

    const QString importId = args.value("import-id").trimmed();
    if (importId.isEmpty() || importId.size() > 200)
        return localFailure(command, "invalid_import_id", QStringLiteral("请指定有效的 --import-id。"));
    request["importId"] = importId;
    if (!wait) return callLocalTool(command, "Packages", request, timeout);

    return waitForLocalTask(command, "Packages", request, timeout, {{"environment", environment}, {"importId", importId}});
}

// Requests a BAK inventory, a new restore job, or its latest state from the workbench.
CliResult runBakCommand(const CliArguments &args, const QString &action)
{
    const QString command = "bak." + action;
    QStringList options{"timeout-ms"};
    if (action == "inspect") options << "input" << "staging-dir" << "platform" << "instance";
    else if (action == "restore") options << "catalog-id" << "set" << "instance" << "database";
    else options << "job-id";
    const QString error = validateOptions(args, action == "restore" ? QStringList{"wait"} : QStringList{}, options);
    if (!error.isEmpty()) return CliResult::failure(command, "ARGUMENT_ERROR", error, ExitArgumentError);
    const bool wait = args.has("wait");
    bool validTimeout = false;
    const int defaultTimeout = action == "inspect" || wait ? BakWaitTimeoutMs : QueryTimeoutMs;
    const int timeout = args.value("timeout-ms", QString::number(defaultTimeout)).toInt(&validTimeout);
    if (!validTimeout || timeout < 1 || timeout > MaxTimeoutMs)
        return CliResult::failure(command, "ARGUMENT_ERROR", QStringLiteral("--timeout-ms 必须是 1 至 600000 的整数。"), ExitArgumentError);

    QJsonObject request{{"protocolVersion", LocalToolEndpoint::ProtocolVersion}, {"action", action}};
    if (action == "inspect") {
        const QFileInfo input(args.value("input"));
        const QString platform = args.value("platform", "bak").toLower();
        if (args.values("input").size() != 1 || args.value("input").trimmed().isEmpty())
            return CliResult::failure(command, "ARGUMENT_ERROR", QStringLiteral("请指定一个备份文件或目录。"), ExitArgumentError);
        if (!(input.isDir() || (input.isFile() && input.isReadable()
            && (platform != "bak" || input.suffix().compare("bak", Qt::CaseInsensitive) == 0))))
            return localFailure(command, "file_error", QStringLiteral("请选择可读取的备份文件或目录。"));
        request["input"] = input.absoluteFilePath();
        if (args.has("platform")) request["platform"] = platform;
        if (args.has("instance")) request["instance"] = args.value("instance");
        if (args.has("staging-dir")) {
            const QString staging = args.value("staging-dir");
            if (staging.trimmed().isEmpty())
                return CliResult::failure(command, "ARGUMENT_ERROR", QStringLiteral("--staging-dir 需要目录路径。"), ExitArgumentError);
            request["stagingDirectory"] = QDir().absoluteFilePath(staging);
        }
        return callLocalTool(command, "Bak", request, timeout);
    }
    if (action == "status") {
        if (args.has("job-id")) {
            const QString job = args.value("job-id");
            if (QUuid(job).isNull())
                return CliResult::failure(command, "ARGUMENT_ERROR", QStringLiteral("请指定有效的恢复任务 ID。"), ExitArgumentError);
            request["jobId"] = job;
        }
        return callLocalTool(command, "Bak", request, timeout);
    }

    bool validSet = false;
    const int set = args.value("set").toInt(&validSet);
    const QString catalog = args.value("catalog-id");
    const QString instance = args.value("instance");
    const QString database = args.value("database");
    if (QUuid(catalog).isNull() || !validSet || set < 1 || instance.trimmed().isEmpty() || database.trimmed().isEmpty())
        return CliResult::failure(command, "ARGUMENT_ERROR",
            QStringLiteral("请指定有效的清单 ID、备份集编号、本机实例和新数据库名。"), ExitArgumentError);
    request["catalogId"] = catalog;
    request["set"] = set;
    request["instance"] = instance;
    request["database"] = database;
    QElapsedTimer budget;
    budget.start();
    const auto started = callLocalTool(command, "Bak", request, qMin(timeout, QueryTimeoutMs));
    if (started.errorCode == "READ_TIMEOUT" || started.errorCode == "TOOL_DISCONNECTED") {
        return localFailure(command, "restore_state_unknown", QStringLiteral("未确认恢复请求是否已受理，请先执行 bak status。"),
            {{"catalogId", catalog}, {"set", set}, {"instance", instance}, {"database", database}});
    }
    if (!started.ok || !wait) return started;

    const QJsonObject statusRequest{{"protocolVersion", LocalToolEndpoint::ProtocolVersion},
        {"action", "status"}, {"jobId", started.data.value("jobId")}};
    auto result = waitForLocalTask(command, "Bak", statusRequest, timeout - int(budget.elapsed()), started.data);
    if (!result.ok) result.errorDetails["jobId"] = started.data.value("jobId");
    if (result.ok && result.data.value("status") == QJsonValue("failed")) {
        const QString reason = result.data.value("error").toString();
        return localFailure(command, "bak_restore_failed",
            reason.isEmpty() ? QStringLiteral("BAK 恢复失败，请核对目标库状态。") : reason, result.data);
    }
    return result;
}

// Keeps input-discovery failures and converter outcomes in the same item format.
QJsonObject accessItemResult(const Access2003ConversionItem &item)
{
    QJsonArray warnings;
    for (const auto &warning : item.warnings) warnings.append(warning);
    return {{"input", item.sourcePath}, {"output", item.success ? item.destinationPath : QString()},
        {"ok", item.success}, {"copied", item.copiedBecauseAlreadyCompatible}, {"recovered", item.recoveredData},
        {"recoveredTables", item.recoveredTables}, {"recoveredRows", item.recoveredRows},
        {"report", item.recoveryReportPath}, {"warnings", warnings}, {"error", item.errorMessage}};
}

// Converts files through the same Access service used by the desktop page.
CliResult runAccessCommand(const CliArguments &args)
{
    const QString command = "access.convert";
    const QString error = validateOptions(args, {"no-recursive", "password-stdin"}, {"input", "output-dir", "mode"});
    if (!error.isEmpty()) return CliResult::failure(command, "ARGUMENT_ERROR", error, ExitArgumentError);
    const auto inputs = args.values("input");
    const QString output = args.value("output-dir");
    if (inputs.isEmpty() || output.trimmed().isEmpty())
        return CliResult::failure(command, "ARGUMENT_ERROR", QStringLiteral("请至少指定一个 --input，并指定 --output-dir。"), ExitArgumentError);
    const QString mode = args.value("mode", "recovery");
    if (mode != "recovery" && mode != "access2003")
        return CliResult::failure(command, "ARGUMENT_ERROR", QStringLiteral("--mode 只接受 recovery 或 access2003。"), ExitArgumentError);
    if (args.has("password-stdin") && mode != "access2003")
        return CliResult::failure(command, "ARGUMENT_ERROR", QStringLiteral("--password-stdin 仅适用于 access2003 模式。"), ExitArgumentError);

    QJsonArray items;
    QStringList sources;
    QSet<QString> seen;
    for (const auto &input : inputs) {
        if (input.trimmed().isEmpty()) {
            Access2003ConversionItem item;
            item.sourcePath = input;
            item.errorMessage = QStringLiteral("--input 不能为空。");
            items.append(accessItemResult(item));
            continue;
        }
        const QFileInfo info(input);
        const QStringList paths = info.isDir()
            ? Access2003Converter::collectDatabaseFiles(info.absoluteFilePath(), !args.has("no-recursive"))
            : QStringList{info.absoluteFilePath()};
        if (paths.isEmpty()) {
            Access2003ConversionItem item;
            item.sourcePath = info.absoluteFilePath();
            item.errorMessage = QStringLiteral("输入目录没有可转换的文件。");
            items.append(accessItemResult(item));
            continue;
        }
        for (const auto &path : paths) {
            const QString canonical = QFileInfo(path).canonicalFilePath();
            const QString key = QDir::cleanPath(canonical.isEmpty() ? path : canonical).toCaseFolded();
            if (!seen.contains(key)) {
                seen.insert(key);
                sources.append(path);
            }
        }
    }

    if (sources.isEmpty())
        return CliResult::failure(command, "FILE_ERROR", QStringLiteral("没有可转换的输入，请查看逐项结果。"), ExitFileError,
                                  {{"succeeded", 0}, {"failed", items.size()}, {"items", items}});
    QString password;
    if (args.has("password-stdin")) {
        QTextStream input(stdin, QIODevice::ReadOnly);
        input.setCodec("UTF-8");
        password = input.readLine();
        if (password.isNull())
            return CliResult::failure(command, "ARGUMENT_ERROR", QStringLiteral("标准输入缺少密码行。"), ExitArgumentError);
    }
    const QString outputDirectory = QDir::cleanPath(QDir().absoluteFilePath(output));
    const auto batch = Access2003Converter::convertFiles(sources, outputDirectory, password,
        mode == "access2003" ? Access2003ConversionMode::Access2003 : Access2003ConversionMode::PreferDataRecovery);
    if (!batch.batchStarted)
        return CliResult::failure(command, "CONVERSION_START_FAILED", batch.startupError, ExitFileError,
                                  {{"outputDirectory", outputDirectory}});

    int succeeded = 0;
    for (const auto &item : batch.items) {
        items.append(accessItemResult(item));
        if (item.success) ++succeeded;
    }
    const int failed = items.size() - succeeded;
    const QJsonObject result{{"mode", mode}, {"outputDirectory", outputDirectory}, {"accessStarted", batch.accessStarted},
        {"accessVersion", batch.accessVersion}, {"succeeded", succeeded}, {"failed", failed}, {"items", items}};
    if (failed)
        return CliResult::failure(command, succeeded ? "PARTIAL_CONVERSION" : "CONVERSION_FAILED",
                                  QStringLiteral("有文件转换失败，请查看逐文件结果。"), ExitDataError, result);
    return CliResult::success(command, result);
}
}

QJsonArray toolCommandHelp()
{
    return {
        QJsonObject{{"name", "auth cookie"}, {"usage", "auth cookie [--source session|desktop] [--reveal] [--http-env Pro|Dev2] [--timeout-ms MS]"},
            {"summary", QStringLiteral("读取 CLI 会话或后台工作台当前登录的柠檬云 Cookie。")},
            {"options", QJsonObject{{"--source", QStringLiteral("session 读取 CLI 登录会话（默认），desktop 读取当前工作台。")},
                {"--reveal", QStringLiteral("在本次 JSON 的 data.ningmengCookie 返回完整值。默认脱敏。")},
                {"--http-env", QStringLiteral("desktop 模式可指定预期环境 Pro 或 Dev2，工作台环境不匹配时报错。")},
                {"--timeout-ms", QStringLiteral("desktop 模式的等待上限，默认 5000 毫秒。")}}},
            {"examples", QJsonArray{".\\LemonWorkToolCli.exe auth cookie",
                ".\\LemonWorkToolCli.exe auth cookie --source desktop --http-env Dev2"}}},
        QJsonObject{{"name", "wecom status"}, {"usage", "wecom status [--timeout-ms MS]"},
            {"summary", QStringLiteral("检查企业微信使用桌面接口还是独立读取模式。")},
            {"options", QJsonObject{{"--timeout-ms", QStringLiteral("等待上限，默认 5000 毫秒。")}}},
            {"examples", QJsonArray{".\\LemonWorkToolCli.exe wecom status"}}},
        QJsonObject{{"name", "wecom documents list"}, {"usage", "wecom documents list [--timeout-ms MS]"},
            {"summary", QStringLiteral("列出当前表格清单中的 ID 和显示名称。")},
            {"options", QJsonObject{{"--timeout-ms", QStringLiteral("等待上限，默认 5000 毫秒。")}}},
            {"examples", QJsonArray{".\\LemonWorkToolCli.exe wecom documents list"}}},
        QJsonObject{{"name", "wecom read"}, {"usage", "wecom read [--document ID | --url URL | --url-stdin] [--from-date YYYY-MM-DD] [--timeout-ms MS] [--legacy-json]"},
            {"summary", QStringLiteral("读取已保存的文档或临时访问企业微信表格链接。新链接默认读取全部页签。")},
            {"options", QJsonObject{{"--document", QStringLiteral("清单中的 ID。未指定文档或链接时使用固定进度表 project-progress。")},
                {"--url", QStringLiteral("手动访问完整企业微信表格链接。同一个文档 ID 已保存时沿用清单策略。")},
                {"--url-stdin", QStringLiteral("从标准输入读取一行 UTF-8 链接，适合含分享码的链接。")},
                {"--from-date", QStringLiteral("日期范围模式的起始日期，含当天。默认本机今天，允许今天或未来。")},
                {"--timeout-ms", QStringLiteral("等待上限，默认 310000 毫秒。")},
                {"--legacy-json", QStringLiteral("供原 PowerShell 调用使用，输出 v1 JSON，退出码为 0 或 2。")}}},
            {"examples", QJsonArray{".\\LemonWorkToolCli.exe wecom read --document project-progress",
                ".\\LemonWorkToolCli.exe wecom read --url 'https://doc.weixin.qq.com/sheet/new-sheet-id'",
                "$link | .\\LemonWorkToolCli.exe wecom read --url-stdin"}}},
        QJsonObject{{"name", "wecom login"}, {"usage", "wecom login [--document ID | --url URL | --url-stdin] [--timeout-ms MS]"},
            {"summary", QStringLiteral("桌面工具退出后，打开官方授权窗口并保存已验证的扫码会话。")},
            {"options", QJsonObject{{"--document", QStringLiteral("用于验证权限的清单 ID，默认固定项目进度表。")},
                {"--url", QStringLiteral("用完整企业微信表格链接验证扫码授权。")},
                {"--url-stdin", QStringLiteral("从标准输入读取一行 UTF-8 链接，用于验证扫码授权。")},
                {"--timeout-ms", QStringLiteral("等待上限，默认 180000 毫秒。")}}},
            {"examples", QJsonArray{".\\LemonWorkToolCli.exe wecom login"}}},
        QJsonObject{{"name", "packages target"}, {"usage", "packages target --http-env Pro|Dev2 [--timeout-ms MS]"},
            {"summary", QStringLiteral("核对后台工作助手此环境的实际专业版导账地址。")},
            {"options", QJsonObject{{"--http-env", QStringLiteral("必填，须与工作助手当前环境一致。")},
                {"--timeout-ms", QStringLiteral("单次查询默认等待 35000 毫秒。")}}},
            {"examples", QJsonArray{".\\LemonWorkToolCli.exe packages target --http-env Dev2"}}},
        QJsonObject{{"name", "packages upload"}, {"usage", "packages upload --http-env Pro|Dev2 --input FILE [--expected-importer ADDRESS] [--timeout-ms MS] [--as-name NAME] [--as-id ID] [--zip-file-name NAME] [--edition professional]"},
            {"summary", QStringLiteral("用后台工作助手的当前登录会话提交专业版账套 ZIP，返回导入任务 ID。")},
            {"options", QJsonObject{{"--http-env", QStringLiteral("必填。Pro 为正式环境，Dev2 为测试环境，须与工具当前登录环境一致。")},
                {"--input", QStringLiteral("要上传的本地 ZIP 文件。")},
                {"--expected-importer", QStringLiteral("packages target 返回的完整地址。实际地址变化时拒绝上传。")},
                {"--timeout-ms", QStringLiteral("CLI 等待受理响应的上限，默认 310000 毫秒。")},
                {"--as-name", QStringLiteral("账套名称，默认 ZIP 文件名。")},
                {"--as-id", QStringLiteral("账套标识，用于业务日志。")},
                {"--zip-file-name", QStringLiteral("上传表单中的文件名，不含 .zip，默认取文件名。")},
                {"--edition", QStringLiteral("可省略，支持 pro 或 professional。")}}},
            {"examples", QJsonArray{".\\LemonWorkToolCli.exe packages upload --http-env Dev2 --input 'D:\\input\\账套.zip'",
                                    ".\\LemonWorkToolCli.exe packages upload --http-env Pro --input 'D:\\input\\账套.zip'"}}},
        QJsonObject{{"name", "packages status"}, {"usage", "packages status --http-env Pro|Dev2 --import-id ID [--wait] [--timeout-ms MS]"},
            {"summary", QStringLiteral("通过提交时的导账地址查询指定任务，返回实际业务结果。")},
            {"options", QJsonObject{{"--http-env", QStringLiteral("必填。须与工作助手及导入任务的环境一致。")},
                {"--import-id", QStringLiteral("上传返回的导入任务 ID。")},
                {"--wait", QStringLiteral("轮询到结果返回，间隔 3 秒。")},
                {"--timeout-ms", QStringLiteral("单次查询默认 35000 毫秒。带 --wait 时默认共等待 300000 毫秒。")}}},
            {"examples", QJsonArray{".\\LemonWorkToolCli.exe packages status --http-env Dev2 --import-id '任务ID' --wait"}}},
        QJsonObject{{"name", "bak inspect"}, {"usage", "bak inspect --input FILE|DIR [--platform PLATFORM] [--instance INSTANCE] [--staging-dir DIR] [--timeout-ms MS]"},
            {"summary", QStringLiteral("通过后台工作助手读取指定恢复链路的备份清单。")},
            {"options", QJsonObject{{"--input", QStringLiteral("与所选平台匹配的备份文件或目录。通用 BAK 在目录内递归查找 .bak。")},
                {"--platform", QStringLiteral("bak（默认）、t3、u8、tplus、kis-pro、inspur、xinzhongda、suda-3000-pro、suda-3000-std、suda-v3-industrial、suda-v3、suda-v5、suda-v7、suda-5000-pro、suda-7000。")},
                {"--instance", QStringLiteral("平台恢复可在读取时指定本机实例。默认使用 .，或唯一发现的命名实例。通用 BAK 在 restore 指定。")},
                {"--staging-dir", QStringLiteral("SQL Server 服务可读取的暂存目录，默认沿用工作助手配置。")},
                {"--timeout-ms", QStringLiteral("等待清单的上限，默认 310000 毫秒。")}}},
            {"examples", QJsonArray{".\\LemonWorkToolCli.exe bak inspect --input 'D:\\Backup\\accounting.bak'",
                ".\\LemonWorkToolCli.exe bak inspect --platform t3 --input 'D:\\Backup\\UFData.BA_'"}}},
        QJsonObject{{"name", "bak restore"}, {"usage", "bak restore --catalog-id ID --set N --instance INSTANCE --database NAME [--wait] [--timeout-ms MS]"},
            {"summary", QStringLiteral("将选定的备份恢复为本机 SQL Server 新库，平台链路同时核对可选账套。")},
            {"options", QJsonObject{{"--catalog-id", QStringLiteral("bak inspect 返回的 catalogId。")},
                {"--set", QStringLiteral("清单中的 set 编号，从 1 开始。")},
                {"--instance", QStringLiteral("清单中的本机 SQL 实例，例如 . 或 .\\SQLEXPRESS。平台恢复须与读取实例一致。")},
                {"--database", QStringLiteral("要新建的目标数据库名称。")},
                {"--wait", QStringLiteral("等待恢复结果，省略时返回任务 ID。")},
                {"--timeout-ms", QStringLiteral("受理等待默认 5000 毫秒，带 --wait 时总等待默认 310000 毫秒。")}}},
            {"examples", QJsonArray{".\\LemonWorkToolCli.exe bak restore --catalog-id $catalog.data.catalogId --set 2 --instance '.' --database 'Accounting_Restored' --wait"}}},
        QJsonObject{{"name", "bak status"}, {"usage", "bak status [--job-id ID] [--timeout-ms MS]"},
            {"summary", QStringLiteral("读取当前工作助手保留的 BAK 恢复任务状态。")},
            {"options", QJsonObject{{"--job-id", QStringLiteral("bak restore 返回的 jobId，省略时查最近任务。")},
                {"--timeout-ms", QStringLiteral("单次查询默认等待 5000 毫秒。")}}},
            {"examples", QJsonArray{".\\LemonWorkToolCli.exe bak status --job-id $restore.data.jobId"}}},
        QJsonObject{{"name", "access convert"}, {"usage", "access convert --input PATH [--input PATH ...] --output-dir DIR [--mode MODE] [--no-recursive] [--password-stdin]"},
            {"summary", QStringLiteral("批量转换 AIS、AIY、MDB，在输出目录生成 Access 2002–2003 格式文件。")},
            {"options", QJsonObject{{"--input", QStringLiteral("输入文件或目录，可重复。目录默认递归扫描。")},
                {"--output-dir", QStringLiteral("输出目录，缺失时自动创建。")},
                {"--mode", QStringLiteral("recovery 为默认数据恢复模式，access2003 使用旧版 Access 转换。")},
                {"--no-recursive", QStringLiteral("目录只扫描当前层。")},
                {"--password-stdin", QStringLiteral("仅 access2003 模式，从标准输入读取一行 UTF-8 密码。")}}},
            {"examples", QJsonArray{".\\LemonWorkToolCli.exe access convert --input 'D:\\input\\sample.mdb' --output-dir 'D:\\output'",
                ".\\LemonWorkToolCli.exe access convert --input 'D:\\input\\batch' --output-dir 'D:\\output' --no-recursive"}}}
    };
}

CliResult runDesktopCookieCommand(const CliArguments &args)
{
    const QString command = "auth.cookie";
    const QString error = validateOptions(args, {"reveal"}, {"source", "http-env", "timeout-ms"});
    if (!error.isEmpty())
        return CliResult::failure(command, "ARGUMENT_ERROR", error, ExitArgumentError);
    if (args.positionals().size() != 2)
        return CliResult::failure(command, "ARGUMENT_ERROR", QStringLiteral("auth cookie 不接受额外的位置参数。"), ExitArgumentError);

    bool validTimeout = false;
    const int timeout = args.value("timeout-ms", QString::number(QueryTimeoutMs)).toInt(&validTimeout);
    if (!validTimeout || timeout < 1 || timeout > MaxTimeoutMs)
        return CliResult::failure(command, "ARGUMENT_ERROR", QStringLiteral("--timeout-ms 必须是 1 至 600000 的整数。"), ExitArgumentError);

    QJsonObject request{{"protocolVersion", LocalToolEndpoint::ProtocolVersion}, {"action", "read_cookie"}, {"reveal", args.has("reveal")}};
    if (args.has("http-env")) {
        const QString expected = args.value("http-env");
        if (expected != "Pro" && expected != "Dev2")
            return localFailure(command, "invalid_environment", QStringLiteral("预期环境只接受 Pro 或 Dev2。"));
        request["environment"] = expected;
    }
    return callLocalTool(command, "Auth", request, timeout);
}

CliResult runToolCommand(const CliArguments &args)
{
    const QString name = args.positionals().join(' ');
    if (name == "wecom status") return runWecomCommand(args, "wecom.status");
    if (name == "wecom documents list") return runWecomCommand(args, "wecom.documents.list");
    if (name == "wecom read") return runWecomCommand(args, "wecom.read");
    if (name == "wecom login") return runWecomCommand(args, "wecom.login");
    if (name == "packages target") return runPackagesCommand(args, "packages.target");
    if (name == "packages upload") return runPackagesCommand(args, "packages.upload");
    if (name == "packages status") return runPackagesCommand(args, "packages.status");
    if (name == "bak inspect") return runBakCommand(args, "inspect");
    if (name == "bak restore") return runBakCommand(args, "restore");
    if (name == "bak status") return runBakCommand(args, "status");
    if (name == "access convert") return runAccessCommand(args);
    return CliResult::failure(args.commandPath(), "NOT_IMPLEMENTED", QStringLiteral("命令不存在，请使用 help 查询。"), ExitNotImplemented);
}
