#include "BakBackupRestorer.h"
#include "LocalSqlInstances.h"
#include "RemoteFileTransfer.h"
#include "SqlBackupCompatibility.h"
#include "SqlServer211.h"
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <QVariant>
#include <algorithm>

namespace {
using Cancel = BakBackupRestorer::Cancel;
using Progress = BakBackupRestorer::Progress;
bool stopped(const Cancel& cancel) { return cancel && cancel(); }
void report(const Progress& progress, const QString& text) { if (progress) progress(text); }
QString literal(const QString& value) { return T3BackupRestorer::quoteLiteral(value); }
QString identifier(const QString& value) { return T3BackupRestorer::quoteIdentifier(value); }
QString disk(const QString& value) { return literal(QDir::toNativeSeparators(value)); }
QString redact(QString text, const T3SqlConnection& options)
{ if (!options.password.isEmpty()) text.replace(options.password, "***"); return text; }
bool local(const QString& server) { return LocalSqlInstances::isLocalEndpoint(server); }
bool sameHost(const QString& left, const QString& right)
{ return (local(left) && local(right)) || left.compare(right, Qt::CaseInsensitive) == 0; }
bool unc(const QString& path) { return QDir::fromNativeSeparators(path).startsWith("//"); }
QString pathKey(const QString& path)
{
    QFileInfo file(path);
    const QString canonical = file.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? file.absoluteFilePath() : canonical).toCaseFolded();
}
bool systemDatabase(const QString& name)
{
    return QStringList{"master", "model", "msdb", "tempdb", "resource", "mssqlsystemresource", "distribution"}
        .contains(name.toCaseFolded());
}
struct Session
{
    QString name = "BakRestore_" + QUuid::createUuid().toString(QUuid::Id128);
    QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", name);
    ~Session() { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(name); }
    bool open(const T3SqlConnection& options, QString* error)
    {
        if (!local(options.server) && options.server != SqlServer211::endpoint()) {
            *error = QString::fromUtf8(u8"请选择本地实例或 211。"); return false;
        }
        if (options.windowsAuthentication != local(options.server)
            || (!options.windowsAuthentication && (options.user.isEmpty() || options.password.isEmpty()))) {
            *error = QString::fromUtf8(u8"数据库连接凭据未配置，或身份验证方式与实例不一致。"); return false;
        }
        if (!T3BackupRestorer::availableDrivers().contains(options.driver)) {
            *error = QString::fromUtf8(u8"未安装所选的 32 位 SQL Server ODBC 驱动。"); return false;
        }
        db.setConnectOptions("SQL_ATTR_LOGIN_TIMEOUT=8;SQL_ATTR_CONNECTION_TIMEOUT=15");
        db.setDatabaseName(T3BackupRestorer::connectionString(options));
        if (db.open()) return true;
        *error = redact(db.lastError().text(), options); return false;
    }
};
bool execute(QSqlDatabase& db, const QString& sql, QString* error)
{
    QSqlQuery query(db); query.setForwardOnly(true);
    if (!query.exec(sql)) { *error = query.lastError().text(); return false; }
    do { while (query.next()) {} } while (query.nextResult());
    if (query.lastError().isValid()) { *error = query.lastError().text(); return false; }
    return true;
}
QString parentDirectory(const QString& path)
{ return path.left(qMax(path.lastIndexOf('/'), path.lastIndexOf('\\')) + 1); }
bool liveInfo(QSqlDatabase& db, T3ServerInfo* info, QString* error)
{
    QSqlQuery query(db); query.setForwardOnly(true);
    // Query current engine/internal versions; persisted versions never authorize restore.
    if (!query.exec("SELECT CONVERT(nvarchar(128),SERVERPROPERTY('ServerName')),"
        "CONVERT(nvarchar(128),SERVERPROPERTY('ProductVersion')),"
        "CONVERT(int,DATABASEPROPERTYEX(N'master',N'Version')),"
        "CONVERT(nvarchar(4000),SERVERPROPERTY('InstanceDefaultDataPath')),"
        "CONVERT(nvarchar(4000),SERVERPROPERTY('InstanceDefaultLogPath')),"
        "CONVERT(nvarchar(4000),SERVERPROPERTY('InstanceDefaultBackupPath'))") || !query.next()) {
        *error = query.lastError().text(); return false;
    }
    info->name = query.value(0).toString(); info->version = query.value(1).toString();
    info->databaseVersion = query.value(2).toInt(); info->dataDirectory = query.value(3).toString();
    info->logDirectory = query.value(4).toString(); info->backupDirectory = query.value(5).toString();
    query.finish();
    if (info->dataDirectory.isEmpty() || info->logDirectory.isEmpty()) {
        if (!query.exec("SELECT type,physical_name FROM sys.master_files WHERE database_id=1")) {
            *error = query.lastError().text(); return false;
        }
        while (query.next()) {
            if (query.value(0).toInt() == 0 && info->dataDirectory.isEmpty()) info->dataDirectory = parentDirectory(query.value(1).toString());
            if (query.value(0).toInt() == 1 && info->logDirectory.isEmpty()) info->logDirectory = parentDirectory(query.value(1).toString());
        }
        if (query.lastError().isValid()) { *error = query.lastError().text(); return false; }
    }
    return true;
}
bool existsOnServer(QSqlDatabase& db, const QString& path, QString* error)
{
    QSqlQuery query(db); query.setForwardOnly(true);
    if (!query.exec("EXEC master.dbo.xp_fileexist " + disk(path)) || !query.next() || query.value(0).toInt() != 1) {
        *error = query.lastError().isValid() ? query.lastError().text()
            : QString::fromUtf8(u8"SQL Server 服务无法读取文件：%1").arg(path); return false;
    }
    return true;
}
QString eligibility(const BakBackupEntry& entry)
{
    if (!entry.readError.isEmpty()) return entry.readError;
    if (entry.analysis.sets.size() != 1) return QString::fromUtf8(u8"未读取到有效备份集。");
    if (entry.backupType != 1) return QString::fromUtf8(u8"仅支持完整数据库备份；日志、差异和文件备份仅展示信息。");
    if (entry.mediaFamilyCount != 1) return QString::fromUtf8(u8"分卷或多设备备份不能按单个 BAK 恢复，请提供单文件完整备份。");
    if (entry.damaged) return QString::fromUtf8(u8"备份头标记备份已损坏，禁止恢复。");
    const auto& set = entry.analysis.sets.first();
    if (set.backupSetId.isEmpty() || set.position <= 0 || set.databaseName.isEmpty()) return QString::fromUtf8(u8"备份身份信息不完整，禁止恢复。");
    if (systemDatabase(set.databaseName)) return QString::fromUtf8(u8"此入口不支持恢复 SQL Server 系统数据库。");
    if (!SqlBackupCompatibility::known(set)) return QString::fromUtf8(u8"无法确认备份来源版本，禁止恢复。");
    return {};
}
QVector<BakBackupEntry> readHeaders(QSqlDatabase& db, const QString& source, const QString& prepared,
    const T3SqlConnection& options, const T3ServerInfo& info, const std::shared_ptr<QString>& lease,
    const Cancel& cancel, QString* error)
{
    QVector<BakBackupEntry> result;
    QSqlQuery label(db); label.setForwardOnly(true);
    int families = 0; QString mediaId;
    if (!label.exec("RESTORE LABELONLY FROM DISK=" + disk(prepared)) || !label.next()) {
        *error = label.lastError().text(); if (error->isEmpty()) *error = QString::fromUtf8(u8"无法读取备份介质信息。"); return result;
    }
    families = label.value(label.record().indexOf("FamilyCount")).toInt();
    mediaId = label.value(label.record().indexOf("MediaSetId")).toString();
    label.finish();
    if (stopped(cancel)) { *error = QString::fromUtf8(u8"已取消读取。"); return result; }
    QSqlQuery query(db); query.setForwardOnly(true);
    if (!query.exec("RESTORE HEADERONLY FROM DISK=" + disk(prepared))) { *error = query.lastError().text(); return result; }
    const auto columns = query.record();
    const QStringList required{"Position", "DatabaseName", "BackupType", "BackupSetGUID", "DatabaseVersion",
        "SoftwareVersionMajor", "SoftwareVersionMinor", "SoftwareVersionBuild", "CompatibilityLevel", "BackupFinishDate"};
    for (const auto& field : required) if (columns.indexOf(field) < 0) {
        *error = QString::fromUtf8(u8"备份头缺少字段：%1；请使用可读取该版本的实例。").arg(field); return {};
    }
    const QFileInfo sourceInfo(source);
    while (query.next()) {
        if (stopped(cancel)) { *error = QString::fromUtf8(u8"已取消读取。"); return {}; }
        BakBackupEntry entry; entry.sourcePath = source;
        entry.sourceSize = sourceInfo.size(); entry.sourceModified = sourceInfo.lastModified().toMSecsSinceEpoch();
        T3BackupSet set;
        const auto value = [&](const char* name) { return query.value(columns.indexOf(name)); };
        set.position = value("Position").toInt(); set.databaseName = value("DatabaseName").toString();
        set.backupDate = value("BackupFinishDate").toDateTime().toString("yyyy-MM-dd HH:mm:ss");
        set.backupSetId = value("BackupSetGUID").toString(); set.databaseVersion = value("DatabaseVersion").toInt();
        set.softwareVersionMajor = value("SoftwareVersionMajor").toInt(); set.softwareVersionMinor = value("SoftwareVersionMinor").toInt();
        entry.softwareVersionBuild = value("SoftwareVersionBuild").toInt(); entry.compatibilityLevel = value("CompatibilityLevel").toInt();
        entry.backupType = value("BackupType").toInt(); entry.backupTypeDescription = value("BackupTypeDescription").toString();
        entry.damaged = value("IsDamaged").toBool(); entry.mediaFamilyCount = families; entry.mediaSetId = mediaId;
        entry.analysis.sets.append(set); entry.analysis.server = info; entry.analysis.preparedPath = prepared;
        entry.analysis.preparedServer = options.server; entry.analysis.targetServer = options.server; entry.analysis.stagingFile = lease;
        entry.analysis.error = eligibility(entry);
        if (entry.analysis.error.isEmpty()) entry.analysis.error = SqlBackupCompatibility::error(entry.analysis.sets, info);
        entry.analysis.success = entry.analysis.error.isEmpty(); result.append(entry);
    }
    if (query.lastError().isValid()) { *error = query.lastError().text(); return {}; }
    if (result.isEmpty()) *error = QString::fromUtf8(u8"文件中未读取到备份集。");
    return result;
}
RemoteTransferredFile stage(const T3SqlConnection& options, const QString& source, QString directory,
    const Cancel& cancel, const Progress& progress)
{
    RemoteTransferredFile result;
    if (local(options.server)) {
        if (directory.trimmed().isEmpty()) directory = "D:/Backup";
        if (!QDir().mkpath(directory)) { result.error = QString::fromUtf8(u8"无法创建备份暂存目录。"); return result; }
    } else if (!unc(directory)) {
        result.error = QString::fromUtf8(u8"远程恢复需要双方可访问的 UNC 共享暂存目录。"); return result;
    }
    if (!T3BackupRestorer::connectConfiguredShare(options, directory, &result.error)) return result;
    return RemoteFileTransfer::stageCopy(source, directory, cancel, progress);
}
bool unchanged(const BakBackupEntry& entry)
{
    const QFileInfo current(entry.sourcePath);
    return current.isFile() && current.size() == entry.sourceSize
        && current.lastModified().toMSecsSinceEpoch() == entry.sourceModified;
}
void failTarget(BakBackupEntry& entry, const QString& error)
{ entry.analysis.success = false; entry.analysis.error = error; entry.analysis.targetServer.clear(); }
bool sameBackup(const BakBackupEntry& saved, const BakBackupEntry& actual)
{
    if (!eligibility(actual).isEmpty() || saved.analysis.sets.size() != 1) return false;
    const auto& before = saved.analysis.sets.first(); const auto& now = actual.analysis.sets.first();
    return actual.mediaSetId == saved.mediaSetId && now.position == before.position
        && now.backupSetId == before.backupSetId && now.databaseName == before.databaseName
        && now.databaseVersion == before.databaseVersion && now.backupDate == before.backupDate
        && now.softwareVersionMajor == before.softwareVersionMajor && now.softwareVersionMinor == before.softwareVersionMinor
        && actual.softwareVersionBuild == saved.softwareVersionBuild && actual.compatibilityLevel == saved.compatibilityLevel;
}
}

QStringList BakBackupRestorer::discoverFiles(const QString& source, QString* error)
{
    error->clear(); const QFileInfo info(source.trimmed()); QStringList files;
    if (info.isFile() && info.suffix().compare("bak", Qt::CaseInsensitive) == 0) files.append(info.absoluteFilePath());
    else if (info.isDir()) {
        QDirIterator iterator(info.absoluteFilePath(), QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks | QDir::Hidden,
            QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QFileInfo file(iterator.next());
            if (file.suffix().compare("bak", Qt::CaseInsensitive) == 0) files.append(file.absoluteFilePath());
        }
    } else *error = QString::fromUtf8(u8"请选择一个 BAK 文件或包含 BAK 文件的目录。");
    files.sort(Qt::CaseInsensitive);
    if (files.isEmpty() && error->isEmpty()) *error = QString::fromUtf8(u8"所选目录及子目录中没有 BAK 文件。");
    return files;
}
QString BakBackupRestorer::sourceVersionText(const BakBackupEntry& entry)
{
    if (entry.analysis.sets.size() != 1) return QString::fromUtf8(u8"版本未知");
    const auto& set = entry.analysis.sets.first();
    if (!SqlBackupCompatibility::known(set)) return QString::fromUtf8(u8"版本未知（%1.%2.%3）")
        .arg(set.softwareVersionMajor).arg(set.softwareVersionMinor).arg(entry.softwareVersionBuild);
    if (set.softwareVersionMajor == 0)
        return SqlBackupCompatibility::versionName(SqlBackupCompatibility::sourceVersion(set))
            + QString::fromUtf8(u8"（依据内部版本识别；备份头未提供完整引擎版本号）");
    return QStringLiteral("%1 / %2.%3.%4").arg(SqlBackupCompatibility::versionName(SqlBackupCompatibility::sourceVersion(set)))
        .arg(set.softwareVersionMajor).arg(set.softwareVersionMinor).arg(entry.softwareVersionBuild);
}
bool BakBackupRestorer::isRestorable(const BakBackupEntry& entry) { return eligibility(entry).isEmpty(); }

BakBackupBatch BakBackupRestorer::read(const QVector<T3SqlConnection>& candidates, const QString& source,
    const QString& stagingDirectory, const Cancel& cancel, const Progress& progress)
{
    BakBackupBatch result;
    const auto files = discoverFiles(source, &result.error); if (files.isEmpty()) return result;
    struct Reader { T3SqlConnection options; T3ServerInfo info; };
    QVector<Reader> readers; QStringList failures;
    // Local readers first; querying a remote instance is unnecessary when a local reader works.
    for (bool remote : {false, true}) {
        for (const auto& options : candidates) {
            if (stopped(cancel)) break;
            if (remote == local(options.server)) continue;
            Reader reader; reader.options = options; Session session; QString error;
            if (!session.open(options, &error) || !liveInfo(session.db, &reader.info, &error)) {
                failures.append(options.server + ": " + redact(error, options)); continue;
            }
            readers.append(reader);
            if (local(options.server)) LocalSqlInstances::cacheVersion(options.server, reader.info.version, reader.info.databaseVersion);
        }
        if (!readers.isEmpty() || stopped(cancel)) break;
    }
    std::stable_sort(readers.begin(), readers.end(), [](const Reader& a, const Reader& b) {
        return SqlBackupCompatibility::engineVersion(a.info.version) > SqlBackupCompatibility::engineVersion(b.info.version);
    });
    if (!readers.isEmpty()) result.readerServer = readers.first().options.server;
    for (const auto& file : files) {
        BakBackupEntry failed; failed.sourcePath = file;
        if (stopped(cancel)) {
            result.stopped = true; failed.readError = QString::fromUtf8(u8"已取消，尚未读取此文件。");
            failed.analysis.error = failed.readError; result.entries.append(failed); continue;
        }
        report(progress, QString::fromUtf8(u8"读取备份版本：%1").arg(QDir::toNativeSeparators(file)));
        QFile input(file);
        if (!input.open(QIODevice::ReadOnly)) failed.readError = input.errorString();
        else {
            const auto header = input.peek(8); input.close();
            if (!header.startsWith("TAPE") && !header.startsWith("MSSQLBAK"))
                failed.readError = QString::fromUtf8(u8"不是 SQL Server 原生备份文件。");
        }
        QStringList errors;
        QVector<BakBackupEntry> entries; RemoteTransferredFile staged;
        if (failed.readError.isEmpty()) for (const auto& reader : readers) {
            if (stopped(cancel)) break;
            Session session; QString error;
            if (!session.open(reader.options, &error)) { errors.append(redact(error, reader.options)); continue; }
            QString prepared = file; std::shared_ptr<QString> lease;
            // A local server reads the original only; it never opens a user file for writing.
            const bool canUseOriginal = (local(reader.options.server) || unc(file))
                && existsOnServer(session.db, file, &error);
            if (!canUseOriginal) {
                if (staged.serverPath.isEmpty()) staged = stage(reader.options, file, stagingDirectory, cancel, progress);
                if (!staged.error.isEmpty()) { errors.append(redact(staged.error, reader.options)); continue; }
                prepared = staged.serverPath; lease = staged.job;
            }
            error.clear(); entries = readHeaders(session.db, file, prepared, reader.options, reader.info, lease, cancel, &error);
            if (!entries.isEmpty()) break;
            errors.append(reader.options.server + ": " + redact(error, reader.options));
        }
        if (!entries.isEmpty()) result.entries += entries;
        else {
            if (failed.readError.isEmpty()) failed.readError = stopped(cancel) ? QString::fromUtf8(u8"已取消读取。"): readers.isEmpty()
                ? QString::fromUtf8(u8"没有可读取备份头的 SQL Server 实例。%1").arg(failures.join("\n")) : errors.join("\n");
            failed.analysis.error = failed.readError; result.entries.append(failed);
        }
    }
    result.stopped = result.stopped || stopped(cancel);
    if (result.stopped) result.error = QString::fromUtf8(u8"读取已取消，已发现的文件及已读取版本保留。");
    else if (readers.isEmpty()) result.error = QString::fromUtf8(u8"没有可用的 SQL Server 实例；文件清单已保留。");
    return result;
}

BakBackupBatch BakBackupRestorer::validateTarget(const BakBackupBatch& batch, const T3SqlConnection& options,
    const QString& stagingDirectory, const Cancel& cancel, const Progress& progress)
{
    BakBackupBatch result = batch; result.error.clear(); result.stopped = false;
    Session session; T3ServerInfo info; QString error;
    if (!session.open(options, &error) || !liveInfo(session.db, &info, &error)) {
        result.error = redact(error, options);
        for (auto& entry : result.entries) failTarget(entry, entry.readError.isEmpty() ? result.error : entry.readError);
        return result;
    }
    if (local(options.server)) LocalSqlInstances::cacheVersion(options.server, info.version, info.databaseVersion);
    QMap<QString, RemoteTransferredFile> copies;
    for (auto& entry : result.entries) {
        entry.analysis.server = info;
        if (stopped(cancel)) { result.stopped = true; failTarget(entry, QString::fromUtf8(u8"目标校验已取消。")); continue; }
        QString problem = eligibility(entry);
        if (problem.isEmpty()) problem = SqlBackupCompatibility::error(entry.analysis.sets, info);
        if (problem.isEmpty() && !unchanged(entry)) problem = QString::fromUtf8(u8"源备份已变更或不存在，请重新读取。");
        if (!problem.isEmpty()) { failTarget(entry, problem); continue; }
        const bool usable = (sameHost(entry.analysis.preparedServer, options.server) || unc(entry.analysis.preparedPath))
            && existsOnServer(session.db, entry.analysis.preparedPath, &problem);
        if (!usable) {
            const QString key = pathKey(entry.sourcePath);
            if (!copies.contains(key)) {
                report(progress, QString::fromUtf8(u8"为目标实例准备备份：%1").arg(QFileInfo(entry.sourcePath).fileName()));
                copies.insert(key, stage(options, entry.sourcePath, stagingDirectory, cancel, progress));
            }
            const auto copy = copies.value(key);
            if (!copy.error.isEmpty()) { failTarget(entry, redact(copy.error, options)); continue; }
            entry.analysis.preparedPath = copy.serverPath; entry.analysis.stagingFile = copy.job;
            entry.analysis.preparedServer = options.server;
            if (!existsOnServer(session.db, copy.serverPath, &problem)) { failTarget(entry, redact(problem, options)); continue; }
        }
        entry.analysis.targetServer = options.server; entry.analysis.error.clear(); entry.analysis.success = true;
    }
    if (result.stopped) result.error = QString::fromUtf8(u8"目标校验已取消，备份清单已保留。");
    return result;
}

QString BakBackupRestorer::validateSelections(const BakBackupBatch& batch, const QVector<BakRestoreSelection>& selections)
{
    if (selections.isEmpty()) return QString::fromUtf8(u8"请至少选择一个可恢复的完整数据库备份。");
    QSet<QString> names, backups;
    for (const auto& selection : selections) {
        if (selection.entryIndex < 0 || selection.entryIndex >= batch.entries.size()) return QString::fromUtf8(u8"备份选择已失效，请重新读取。");
        const auto& entry = batch.entries.at(selection.entryIndex);
        const QString error = eligibility(entry); if (!error.isEmpty()) return error;
        if (!entry.analysis.success) return entry.analysis.error.isEmpty() ? QString::fromUtf8(u8"请先校验目标实例。") : entry.analysis.error;
        const auto& set = entry.analysis.sets.first();
        const QString versionError = SqlBackupCompatibility::error(entry.analysis.sets, entry.analysis.server);
        if (!versionError.isEmpty()) return versionError;
        const auto& name = selection.targetDatabase;
        if (name.trimmed().isEmpty() || name != name.trimmed() || name.size() > 128
            || name.contains(QChar(0)) || name.contains('\n') || name.contains('\r')) return QString::fromUtf8(u8"目标数据库名为空、过长或包含无效字符。");
        if (systemDatabase(name)) return QString::fromUtf8(u8"不能恢复到 SQL Server 系统数据库。");
        const QString backupKey = pathKey(entry.sourcePath) + '|' + QString::number(set.position);
        if (backups.contains(backupKey) || names.contains(name.toCaseFolded())) return QString::fromUtf8(u8"不能重复选择同一备份集或使用重复的目标数据库名。");
        backups.insert(backupKey); names.insert(name.toCaseFolded());
    }
    return {};
}

T3RestoreResult BakBackupRestorer::restore(const T3SqlConnection& options, const BakBackupBatch& batch,
    const QVector<BakRestoreSelection>& selections, const QString& dataDirectory, const QString& logDirectory,
    const Cancel& cancel, const Progress& progress)
{
    T3RestoreResult result;
    const auto perform = [&]() {
        result.error = validateSelections(batch, selections); if (!result.error.isEmpty()) return;
        if (dataDirectory.trimmed().isEmpty() || logDirectory.trimmed().isEmpty()) {
            result.error = QString::fromUtf8(u8"请填写服务器上的数据和日志目录。"); return;
        }
        Session session; T3ServerInfo info;
        if (!session.open(options, &result.error) || !liveInfo(session.db, &info, &result.error)) return;
        QVector<QString> commands;
        // Complete preflight for the whole selection before creating the first database.
        for (const auto& selection : selections) {
            if (stopped(cancel)) { result.stopped = true; return; }
            const auto& entry = batch.entries.at(selection.entryIndex);
            if (entry.analysis.targetServer != options.server || entry.analysis.preparedPath.isEmpty()
                || !unchanged(entry)) { result.error = QString::fromUtf8(u8"目标或源备份已变化，请重新读取并校验。"); return; }
            if (entry.analysis.server.name.trimmed().isEmpty() || info.name.trimmed().isEmpty()
                || entry.analysis.server.name.compare(info.name, Qt::CaseInsensitive) != 0) {
                result.error = QString::fromUtf8(u8"目标实例身份与读取校验时不一致，请重新校验目标。"); return;
            }
            result.error = SqlBackupCompatibility::error(entry.analysis.sets, info); if (!result.error.isEmpty()) return;
            QString error;
            const auto current = readHeaders(session.db, entry.sourcePath, entry.analysis.preparedPath,
                options, info, entry.analysis.stagingFile, cancel, &error);
            if (!error.isEmpty()) { result.error = error; return; }
            const auto& saved = entry.analysis.sets.first(); const BakBackupEntry* actual = nullptr;
            for (const auto& candidate : current) if (candidate.analysis.sets.first().position == saved.position) actual = &candidate;
            if (!actual || !sameBackup(entry, *actual)) {
                result.error = QString::fromUtf8(u8"备份集身份或版本已变化，请重新读取。"); return;
            }
            QSqlQuery query(session.db); query.setForwardOnly(true);
            if (!query.exec("SELECT DB_ID(" + literal(selection.targetDatabase) + ')') || !query.next()) {
                result.error = query.lastError().text(); return;
            }
            if (!query.value(0).isNull()) { result.error = QString::fromUtf8(u8"目标数据库已存在，拒绝覆盖：%1").arg(selection.targetDatabase); return; }
            query.finish();
            if (!query.exec(QStringLiteral("RESTORE FILELISTONLY FROM DISK=%1 WITH FILE=%2").arg(disk(entry.analysis.preparedPath)).arg(saved.position))) {
                result.error = query.lastError().text(); return;
            }
            const auto columns = query.record(); QStringList moves; QSet<int> fileIds;
            const QString token = QUuid::createUuid().toString(QUuid::Id128);
            while (query.next()) {
                const QString type = query.value(columns.indexOf("Type")).toString();
                const QString logicalName = query.value(columns.indexOf("LogicalName")).toString();
                const int fileId = query.value(columns.indexOf("FileId")).toInt();
                if ((type != "D" && type != "L") || logicalName.isEmpty() || fileId <= 0 || fileIds.contains(fileId)) {
                    result.error = QString::fromUtf8(u8"备份包含不支持的数据库文件类型或无效文件编号。"); return;
                }
                fileIds.insert(fileId);
                const QString directory = QDir::fromNativeSeparators(type == "L" ? logDirectory : dataDirectory);
                const QString filename = QStringLiteral("BAK_%1_%2%3").arg(token).arg(fileId).arg(type == "L" ? ".ldf" : fileId == 1 ? ".mdf" : ".ndf");
                const QString path = QDir::toNativeSeparators(directory + (directory.endsWith('/') ? "" : "/") + filename);
                moves.append("MOVE " + literal(logicalName) + " TO " + literal(path));
            }
            if (query.lastError().isValid() || moves.isEmpty()) { result.error = query.lastError().isValid() ? query.lastError().text() : QString::fromUtf8(u8"备份中没有数据库文件。"); return; }
            query.finish();
            const QString tail = QStringLiteral(" FROM DISK=%1 WITH FILE=%2, %3").arg(disk(entry.analysis.preparedPath)).arg(saved.position).arg(moves.join(", "));
            report(progress, QString::fromUtf8(u8"校验备份及目标文件：%1").arg(selection.targetDatabase));
            if (!execute(session.db, "RESTORE VERIFYONLY" + tail + ';', &result.error)) return;
            commands.append(QStringLiteral("IF DB_ID(%1) IS NOT NULL BEGIN RAISERROR(N'Destination database already exists.',16,1); RETURN; END; RESTORE DATABASE %2%3, RECOVERY;")
                .arg(literal(selection.targetDatabase), identifier(selection.targetDatabase), tail));
        }
        for (int i = 0; i < selections.size(); ++i) {
            if (stopped(cancel)) { result.stopped = true; return; }
            T3RestoreItem item; item.database = selections.at(i).targetDatabase;
            const auto& entry = batch.entries.at(selections.at(i).entryIndex);
            // Earlier VERIFYONLY calls can take time. Recheck identity immediately before each write.
            bool identityMatches = false;
            if (unchanged(entry)) {
                const auto current = readHeaders(session.db, entry.sourcePath, entry.analysis.preparedPath,
                    options, info, entry.analysis.stagingFile, cancel, &item.error);
                for (const auto& actual : current) if (sameBackup(entry, actual)) identityMatches = true;
            }
            if (stopped(cancel)) { result.stopped = true; return; }
            if (!identityMatches) {
                if (item.error.isEmpty()) item.error = QString::fromUtf8(u8"备份在预检后已变化，停止后续恢复。");
                item.error = redact(item.error, options); result.items.append(item); result.error = item.error; return;
            }
            report(progress, QString::fromUtf8(u8"正在恢复 %1（%2/%3）...").arg(item.database).arg(i + 1).arg(selections.size()));
            const bool executed = execute(session.db, commands.at(i), &item.error);
            if (executed) {
                QSqlQuery state(session.db); state.setForwardOnly(true);
                item.databaseRestored = state.exec("SELECT state_desc FROM sys.databases WHERE name=" + literal(item.database))
                    && state.next() && state.value(0).toString() == "ONLINE";
                item.success = item.databaseRestored;
                if (!item.success) item.error = QString::fromUtf8(u8"恢复命令已执行，但未确认数据库 ONLINE；请检查服务器状态。");
            }
            if (!item.success) {
                QSqlQuery state(session.db); state.setForwardOnly(true);
                if (state.exec("SELECT state_desc FROM sys.databases WHERE name=" + literal(item.database)) && state.next())
                    item.error += QString::fromUtf8(u8"\n目标数据库当前状态：%1；已保留，请检查后处理。").arg(state.value(0).toString());
                else item.error += QString::fromUtf8(u8"\n恢复未完成；请检查目标实例是否留下数据库或文件。");
            }
            item.error = redact(item.error, options); result.items.append(item);
            if (!item.success) { result.error = item.error; return; }
            report(progress, QString::fromUtf8(u8"恢复成功：%1").arg(item.database));
        }
    };
    perform(); result.error = redact(result.error, options); return result;
}
