#include "BakLocalCommands.h"
#include "LocalSqlInstances.h"
#include "LocalToolEndpoint.h"
#include "RestorePreferences.h"
#include "logger.h"
#include <QDir>
#include <QJsonArray>
#include <QThread>
#include <QUuid>
#include <QVariant>
#include <algorithm>
#include <memory>

namespace {
constexpr int RetainedJobs = 16;

// IDs belong to the current workbench and never expose staged file paths.
QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// Lists rows as inspected, including unreadable and unsupported backups.
QJsonArray describeCatalog(const BakBackupBatch &catalog)
{
    QJsonArray rows;
    for (int index = 0; index < catalog.entries.size(); ++index) {
        const auto &entry = catalog.entries.at(index);
        const T3BackupSet set = entry.analysis.sets.value(0);
        rows.append(QJsonObject{{"set", index + 1}, {"position", set.position}, {"backupSetId", set.backupSetId},
            {"input", QDir::toNativeSeparators(entry.sourcePath)}, {"database", set.databaseName}, {"backupDate", set.backupDate},
            {"backupType", entry.backupType}, {"backupTypeDescription", entry.backupTypeDescription},
            {"sourceVersion", BakBackupRestorer::sourceVersionText(entry)}, {"databaseVersion", set.databaseVersion},
            {"compatibilityLevel", entry.compatibilityLevel}, {"restorable", BakBackupRestorer::isRestorable(entry)},
            {"error", entry.analysis.error}});
    }
    return rows;
}

// Exposes discovered local instances without implying they have been selected.
QJsonArray describeInstances(const QStringList &instances)
{
    QJsonArray result;
    for (const auto &instance : instances)
        result.append(QJsonObject{{"instance", instance}, {"version", LocalSqlInstances::cachedVersion(instance)}});
    return result;
}
}

BakLocalCommands::BakLocalCommands(QObject *parent) : QObject(parent) {}

void BakLocalCommands::setBusy(bool busy)
{
    m_busy = busy;
    setProperty("sessionBusyBakCli", busy);
}

void BakLocalCommands::dispatch(const QJsonObject &request, LocalToolServer::Reply reply)
{
    if (request.value("protocolVersion") != QJsonValue(LocalToolEndpoint::ProtocolVersion)) {
        reply(LocalToolServer::error("unsupported_version", QStringLiteral("本机接口版本不匹配。")));
        return;
    }

    const QString action = request.value("action").toString();
    const QStringList common{"action", "protocolVersion"};
    QStringList allowed = common;
    if (action == "inspect") allowed << "input" << "stagingDirectory" << "platform" << "instance";
    else if (action == "restore") allowed << "catalogId" << "set" << "instance" << "database";
    else if (action == "status") allowed << "jobId";
    else {
        reply(LocalToolServer::error("unsupported_action", QStringLiteral("本机 BAK 接口支持 inspect、restore 和 status。")));
        return;
    }
    for (const auto &key : request.keys()) {
        if (!allowed.contains(key)) {
            reply(LocalToolServer::error("invalid_request", QStringLiteral("请求包含不支持的字段。")));
            return;
        }
    }

    if (action == "status") {
        const auto jobId = request.value("jobId");
        if (!m_jobs.isEmpty() && jobId.isUndefined()) {
            reply(LocalToolServer::success(m_jobs.last()));
            return;
        }
        for (const auto &job : m_jobs) {
            if (job.value("jobId") != jobId) continue;
            reply(LocalToolServer::success(job));
            return;
        }
        reply(LocalToolServer::error("restore_job_not_found", QStringLiteral("当前工作台没有对应的 BAK 恢复任务。")));
        return;
    }
    if (m_busy) {
        reply(LocalToolServer::error("busy", QStringLiteral("工作助手正在读取或恢复 BAK，请查看当前任务。")));
        return;
    }
    if (action == "inspect") inspect(request, std::move(reply));
    else restore(request, std::move(reply));
}

void BakLocalCommands::inspect(const QJsonObject &request, LocalToolServer::Reply reply)
{
    const QString input = request.value("input").toString();
    const QString staging = request.contains("stagingDirectory")
        ? request.value("stagingDirectory").toString() : RestorePreferences::stagingDirectory();
    if (!QDir::isAbsolutePath(input) || (request.contains("stagingDirectory") && !QDir::isAbsolutePath(staging))) {
        reply(LocalToolServer::error("invalid_request", QStringLiteral("备份和暂存目录应使用绝对路径。")));
        return;
    }
    const QString platform = request.value("platform").toString("bak");
    const bool generic = platform == "bak";
    const QString requestedInstance = request.value("instance").toString();
    if ((!generic && !PlatformBackupRestore::supports(platform))
        || (generic && request.contains("instance"))) {
        reply(LocalToolServer::error("invalid_platform",
            QStringLiteral("请选择受支持的备份恢复链路。通用 BAK 在 restore 时指定实例。")));
        return;
    }
    if (request.contains("instance") && !LocalSqlInstances::isLocalEndpoint(requestedInstance)) {
        reply(LocalToolServer::error("invalid_sql_instance", QStringLiteral("请指定本机 SQL Server 实例。")));
        return;
    }

    setBusy(true);
    const auto batch = std::make_shared<BakBackupBatch>();
    const auto platformCatalog = std::make_shared<PlatformBackupCatalog>();
    const auto instances = std::make_shared<QStringList>();
    auto *worker = QThread::create([batch, platformCatalog, instances, input, staging, platform, generic, requestedInstance]() {
        try {
            *instances = LocalSqlInstances::discover();
            const auto drivers = T3BackupRestorer::availableDrivers();
            if (drivers.isEmpty()) {
                batch->error = QStringLiteral("未安装可用的 32 位 SQL Server ODBC 驱动。");
                return;
            }
            if (!generic) {
                QString target = requestedInstance;
                if (target.isEmpty() && instances->contains(".")) target = ".";
                if (target.isEmpty() && instances->size() == 1) target = instances->first();
                if (target.isEmpty() || !instances->contains(target, Qt::CaseInsensitive)) return;
                T3SqlConnection connection;
                connection.server = target;
                connection.driver = drivers.first();
                *platformCatalog = PlatformBackupRestore::inspect(platform, input, staging, connection);
                return;
            }
            QVector<T3SqlConnection> candidates;
            for (const auto &instance : *instances) {
                T3SqlConnection connection;
                connection.server = instance;
                connection.driver = drivers.first();
                candidates.append(connection);
            }
            *batch = BakBackupRestorer::read(candidates, input, staging, {}, &AppLogger::operation);
        } catch (...) {
            batch->error = QStringLiteral("读取 BAK 时发生异常，请检查备份文件和本机 SQL Server。");
        }
    });
    connect(worker, &QThread::finished, this, [this, batch, platformCatalog, instances, staging, platform, generic, reply]() {
        setBusy(false);
        QJsonObject details{{"platform", platform}, {"defaultInstance", instances->contains(".") ? QString(".") :
                (instances->size() == 1 ? instances->first() : QString())},
            {"readerInstance", generic ? batch->readerServer : platformCatalog->instance},
            {"localInstances", describeInstances(*instances)},
            {"entries", generic ? describeCatalog(*batch) : platformCatalog->entries}};
        if (!generic && batch->error.isEmpty() && platformCatalog->instance.isEmpty()) {
            reply(LocalToolServer::error("invalid_sql_instance", QStringLiteral("未找到指定的本机实例，或存在多个可选实例且没有默认实例。"), details));
            return;
        }
        const QString error = batch->error.isEmpty() ? platformCatalog->error : batch->error;
        if (!error.isEmpty() || details.value("entries").toArray().isEmpty()) {
            reply(LocalToolServer::error("bak_inspect_failed",
                error.isEmpty() ? QStringLiteral("没有可列出的备份集。") : error, details));
            return;
        }
        m_catalog = std::move(*batch);
        m_platformCatalog = std::move(*platformCatalog);
        m_catalogId = newId();
        m_stagingDirectory = staging;
        details["catalogId"] = m_catalogId;
        reply(LocalToolServer::success(details));
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void BakLocalCommands::restore(const QJsonObject &request, LocalToolServer::Reply reply)
{
    if (m_catalogId.isEmpty() || request.value("catalogId").toString() != m_catalogId) {
        reply(LocalToolServer::error("bak_catalog_not_found", QStringLiteral("备份清单已过期，请重新执行 bak inspect。")));
        return;
    }
    const int set = request.value("set").toInt();
    const bool generic = m_platformCatalog.platform.isEmpty();
    const QString platform = generic ? QString("bak") : m_platformCatalog.platform;
    const int count = generic ? m_catalog.entries.size() : m_platformCatalog.entries.size();
    if (set < 1 || set > count) {
        reply(LocalToolServer::error("invalid_backup_set", QStringLiteral("请选择备份清单中的 set 编号。")));
        return;
    }
    const auto row = generic ? QJsonObject{} : m_platformCatalog.entries.at(set - 1).toObject();
    const auto entry = generic ? m_catalog.entries.at(set - 1) : BakBackupEntry{};
    if (generic ? !BakBackupRestorer::isRestorable(entry) : !row.value("restorable").toBool()) {
        const QString error = generic ? entry.analysis.error : row.value("error").toString();
        reply(LocalToolServer::error("invalid_backup_set",
            error.isEmpty() ? QStringLiteral("此备份集不支持单独恢复，请选择清单中标记为可恢复的项目。") : error));
        return;
    }
    const QString instance = request.value("instance").toString();
    if (!LocalSqlInstances::isLocalEndpoint(instance)) {
        reply(LocalToolServer::error("invalid_sql_instance", QStringLiteral("请指定本机 SQL Server 实例。")));
        return;
    }
    if (!generic && instance.compare(m_platformCatalog.instance, Qt::CaseInsensitive) != 0) {
        reply(LocalToolServer::error("instance_mismatch", QStringLiteral("平台备份须在读取清单的本机实例上恢复。请按目标实例重新读取。")));
        return;
    }
    const QString database = request.value("database").toString();
    if (database.trimmed().isEmpty()) {
        reply(LocalToolServer::error("invalid_request", QStringLiteral("请指定新数据库名。")));
        return;
    }

    BakBackupBatch selected;
    if (generic) selected.entries.append(entry);
    const auto platformRestore = m_platformCatalog.restore;
    const QString staging = m_stagingDirectory;
    m_jobs.append(QJsonObject{{"jobId", newId()}, {"catalogId", m_catalogId}, {"set", set},
        {"instance", instance}, {"database", database}, {"platform", platform}, {"status", "running"}, {"finished", false}});
    if (m_jobs.size() > RetainedJobs) m_jobs.removeFirst();
    setBusy(true);
    const auto result = std::make_shared<T3RestoreResult>();
    const auto accounts = std::make_shared<QJsonArray>();
    auto *worker = QThread::create([selected, result, accounts, staging, instance, database, platformRestore, platform, set]() {
        try {
            const auto instances = LocalSqlInstances::discover();
            const auto drivers = T3BackupRestorer::availableDrivers();
            if (!instances.contains(instance, Qt::CaseInsensitive) || drivers.isEmpty()) {
                result->error = QStringLiteral("找不到指定的本机实例或没有可用的 32 位 ODBC 驱动。");
                return;
            }
            T3SqlConnection connection;
            connection.server = instance;
            connection.driver = drivers.first();
            if (platformRestore) {
                *result = platformRestore(set - 1, database);
                if (result->error.isEmpty() && !result->stopped && !result->items.isEmpty()
                    && std::all_of(result->items.cbegin(), result->items.cend(), [](const auto &item) { return item.success; })) {
                    *accounts = PlatformBackupRestore::verify(platform, connection, database, &result->error);
                }
                return;
            }
            const auto validated = BakBackupRestorer::validateTarget(selected, connection, staging, {}, &AppLogger::operation);
            const auto &backup = validated.entries.first();
            if (!validated.error.isEmpty() || !backup.analysis.success) {
                result->error = validated.error.isEmpty() ? backup.analysis.error : validated.error;
                return;
            }
            const auto &server = backup.analysis.server;
            *result = BakBackupRestorer::restore(connection, validated, {{0, database}},
                server.dataDirectory, server.logDirectory, {}, &AppLogger::operation);
        } catch (...) {
            result->error = QStringLiteral("恢复 BAK 时发生异常，请在指定实例核对目标库状态。");
        }
    });
    connect(worker, &QThread::finished, this, [this, result, accounts, generic]() {
        QJsonArray items;
        for (const auto &item : result->items) {
            items.append(QJsonObject{{"database", item.database}, {"databaseRestored", item.databaseRestored},
                {"success", item.success}, {"error", item.error}});
        }
        const bool success = result->error.isEmpty() && !result->stopped
            && !result->items.isEmpty()
            && std::all_of(result->items.cbegin(), result->items.cend(), [](const auto &item) { return item.success; })
            && (generic || !accounts->isEmpty());
        auto &job = m_jobs.last();
        job["finished"] = true;
        job["status"] = success ? "succeeded" : "failed";
        job["error"] = result->error;
        job["items"] = items;
        if (!generic) {
            job["accountPickerVerified"] = !accounts->isEmpty() && success;
            job["accounts"] = *accounts;
        }
        setBusy(false);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
    reply(LocalToolServer::success(m_jobs.last()));
}
