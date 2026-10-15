#include "SqlBackupCompatibility.h"
#include "U8BackupRestorer.h"
#include "RemoteFileTransfer.h"
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <QVariant>

static QString u8Literal(const QString& text) { return T3BackupRestorer::quoteLiteral(text); }
static QString u8Identifier(const QString& text) { return T3BackupRestorer::quoteIdentifier(text); }
static QString u8Disk(const QString& path) { return u8Literal(QDir::toNativeSeparators(path)); }
static bool u8Cancelled(const U8BackupRestorer::Cancel& cancel) { return cancel && cancel(); }
static void u8Report(const U8BackupRestorer::Progress& progress, const QString& message)
{ if (progress) progress(message); }
static QRegularExpressionMatch u8Parts(const QString& name)
{
    return QRegularExpression(QStringLiteral("^(UFDATA|UFMeta)_([0-9]{3})(?:_([1-9][0-9]{3}))?$"),
        QRegularExpression::CaseInsensitiveOption).match(name);
}
static bool u8ValidName(const QString& name)
{
    const auto parts = u8Parts(name);
    return parts.hasMatch() && ((parts.captured(1).compare("UFDATA", Qt::CaseInsensitive) == 0)
        == !parts.captured(3).isEmpty());
}
static bool u8IsData(const QString& name) { return name.startsWith("UFDATA_", Qt::CaseInsensitive); }
static QString u8Redact(QString text, const T3SqlConnection& options)
{ if (!options.password.isEmpty()) text.replace(options.password, "***"); return text; }
struct U8SqlSession
{
    QString name = "U8Restore_" + QUuid::createUuid().toString(QUuid::Id128);
    QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", name);
    ~U8SqlSession() { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(name); }
    bool open(const T3SqlConnection& options, QString* error)
    {
        db.setConnectOptions("SQL_ATTR_LOGIN_TIMEOUT=10;SQL_ATTR_CONNECTION_TIMEOUT=15");
        db.setDatabaseName(T3BackupRestorer::connectionString(options));
        if (db.open()) return true;
        *error = u8Redact(db.lastError().text(), options); return false;
    }
};
static bool u8Execute(QSqlDatabase& db, const QString& sql, QString* error)
{
    if (sql.isEmpty()) { *error = QString::fromUtf8(u8"SQL 参数无效。"); return false; }
    QSqlQuery query(db);
    if (!query.exec(sql)) { *error = query.lastError().text(); return false; }
    do { while (query.next()) {} } while (query.nextResult());
    if (query.lastError().isValid()) { *error = query.lastError().text(); return false; }
    return true;
}
static QVector<T3BackupSet> u8ReadSets(QSqlDatabase& db, const QString& path, QString* error)
{
    QVector<T3BackupSet> sets;
    QSqlQuery query(db);
    if (!query.exec("RESTORE HEADERONLY FROM DISK=" + u8Disk(path)))
    { *error = query.lastError().text(); return sets; }
    const auto record = query.record();
    const auto value = [&](const char* column) { return query.value(record.indexOf(QLatin1String(column))); };
    while (query.next()) {
        if (value("BackupType").toInt() != 1) continue;
        T3BackupSet set;
        set.position = value("Position").toInt(); set.databaseName = value("DatabaseName").toString();
        set.backupSetId = value("BackupSetGUID").toString(); set.databaseVersion = value("DatabaseVersion").toInt();
        set.backupDate = value("BackupFinishDate").toString();
        set.softwareVersionMajor=value("SoftwareVersionMajor").toInt();
        set.softwareVersionMinor=value("SoftwareVersionMinor").toInt();
        sets.append(set);
    }
    if (query.lastError().isValid()) *error = query.lastError().text();
    QVector<T3RestoreSelection> selected;
    for (const auto& set : sets) selected.append({set, set.databaseName});
    if (error->isEmpty()) *error = U8BackupRestorer::validateSelections(selected);
    return sets;
}
QString U8BackupRestorer::resolveSource(const QString& path, QString* error)
{
    error->clear(); const QFileInfo info(path);
    if (info.isDir() || info.fileName().compare("UfErpAct.Lst", Qt::CaseInsensitive) == 0) {
        const QDir dir = info.isDir() ? QDir(info.absoluteFilePath()) : info.dir();
        for (const auto& entry : dir.entryInfoList(QDir::Files))
            if (entry.fileName().compare("UFDATA.BA_", Qt::CaseInsensitive) == 0) return entry.absoluteFilePath();
        for (const auto& entry : dir.entryInfoList(QDir::Files))
            if (entry.fileName().compare("UFDATA.BAK", Qt::CaseInsensitive) == 0) return entry.absoluteFilePath();
        *error = QString::fromUtf8(u8"此目录中未找到 UFDATA.BA_ 或 UFDATA.BAK。"); return {};
    }
    if (!info.isFile()) { *error = QString::fromUtf8(u8"备份文件不存在。"); return {}; }
    return info.absoluteFilePath();
}
bool U8BackupRestorer::isSupportedHeader(const QByteArray& header)
{ return header.startsWith("TAPE") || header.startsWith("MSSQLBAK")
    || header.startsWith(QByteArray::fromHex("1f8b08")); }
QString U8BackupRestorer::validateSelections(const QVector<T3RestoreSelection>& selections)
{
    const QString basic = T3BackupRestorer::validateSelections(selections);
    if (!basic.isEmpty()) return basic;
    QString sourceAccount, targetAccount; bool hasData = false; QSet<int> positions;
    for (const auto& item : selections) {
        const auto source = u8Parts(item.backup.databaseName), target = u8Parts(item.targetDatabase);
        if (!u8ValidName(item.backup.databaseName) || !u8ValidName(item.targetDatabase)
            || source.captured(1).compare(target.captured(1), Qt::CaseInsensitive) != 0
            || source.captured(3) != target.captured(3))
            return QString::fromUtf8(u8"U8 库名须为 UFDATA_三位账套号_年度 或 UFMeta_三位账套号；只能修改账套号，保留库类型和年度。");
        if ((!sourceAccount.isEmpty() && sourceAccount != source.captured(2))
            || (!targetAccount.isEmpty() && targetAccount != target.captured(2)) || positions.contains(item.backup.position))
            return QString::fromUtf8(u8"一次仅恢复一个 U8 账套；业务库与元数据库必须使用相同的目标账套号，且不能重复选择备份集。");
        sourceAccount = source.captured(2); targetAccount = target.captured(2);
        positions.insert(item.backup.position); hasData |= u8IsData(item.backup.databaseName);
    }
    return hasData ? QString() : QString::fromUtf8(u8"请至少选择一个 UFDATA 业务库，不能仅恢复 UFMeta。");
}
T3BackupAnalysis U8BackupRestorer::analyze(const T3SqlConnection& options, const QString& source,
    const QString& stagingDirectory, const Cancel& cancel, const Progress& progress, bool useRemoteAgent)
{
    T3BackupAnalysis result;
    const QString resolved = resolveSource(source, &result.error);
    if (resolved.isEmpty()) return result;
    QFile input(resolved);
    if (!input.open(QIODevice::ReadOnly)) { result.error = input.errorString(); return result; }
    if (!isSupportedHeader(input.peek(8))) {
        result.error = QString::fromUtf8(u8"请选择 U8 的 UFDATA.BA_ GZIP 备份或 SQL Server 原生备份，也可选择清单或备份目录。"); return result;
    }
    const bool gzip = input.peek(3) == QByteArray::fromHex("1f8b08");
    input.close();
    if (u8Cancelled(cancel)) { result.error = QString::fromUtf8(u8"已取消读取。"); return result; }
    if (!testConnection(options, &result.server, &result.error)) return result;
    const bool local = T3BackupRestorer::isLocalServer(options.server);
    QString staging = stagingDirectory.trimmed();
    if (local && staging.isEmpty()) staging = defaultLocalStagingDirectory();
    if (local && !QDir().mkpath(staging)) {
        result.error = QString::fromUtf8(u8"无法创建备份暂存目录：%1。请检查目录权限或选择其他目录。")
            .arg(QDir::toNativeSeparators(staging)); return result;
    }
    if (!local && !QDir::fromNativeSeparators(staging).startsWith("//")) {
        result.error = QString::fromUtf8(u8"远程恢复需要 SQL Server 可读取的 UNC 共享暂存目录。"); return result;
    }
    if (gzip && (local || !useRemoteAgent)) {
        u8Report(progress, QString::fromUtf8(u8"正在解包 U8 GZIP 备份并校验完整性..."));
        result.preparedPath = T3BackupRestorer::prepareFile(resolved, staging, &result.error, cancel);
        if (result.preparedPath.isEmpty()) return result;
        result.stagingFile = std::shared_ptr<QString>(new QString(result.preparedPath),
            [](QString* path) { QFile::remove(*path); delete path; });
    } else {
        // Native SQL compression needs no extraction; the remote agent can unpack GZIP.
        const auto transfer = !local && useRemoteAgent
            ? RemoteFileTransfer::upload(resolved, staging, gzip ? RemoteFileTransfer::InputEncoding::Gzip
                : RemoteFileTransfer::InputEncoding::RawFile, cancel, progress)
            : RemoteFileTransfer::stageCopy(resolved, staging, cancel, progress);
        result.error = transfer.error;
        if (!result.error.isEmpty()) return result;
        result.preparedPath = transfer.serverPath; result.stagingFile = transfer.job;
    }
    U8SqlSession session;
    if (!session.open(options, &result.error)) return result;
    result.sets = u8ReadSets(session.db, result.preparedPath, &result.error);
    if (result.error.isEmpty()) result.error = SqlBackupCompatibility::error(result.sets, result.server);
    if (u8Cancelled(cancel)) result.error = QString::fromUtf8(u8"已取消读取。");
    result.error = u8Redact(result.error, options);
    result.success = result.error.isEmpty(); return result;
}
QString U8BackupRestorer::controlSchemaSql(const QString& controlDatabase)
{
    return QStringLiteral("USE %1; SET XACT_ABORT ON; BEGIN TRY BEGIN TRANSACTION; "
        "IF OBJECT_ID(N'dbo.UA_Account') IS NULL CREATE TABLE dbo.UA_Account ("
        "cAcc_Id nvarchar(3) NOT NULL PRIMARY KEY,cAcc_Name nvarchar(120) NOT NULL,"
        "cUnitName nvarchar(120) NOT NULL,iYear int NOT NULL,iMonth int NOT NULL,cTradeKind nvarchar(120) NOT NULL); "
        "IF OBJECT_ID(N'dbo.UA_AccountDatabase') IS NULL CREATE TABLE dbo.UA_AccountDatabase ("
        "cAcc_Id nvarchar(3) NOT NULL,cDatabase nvarchar(128) NOT NULL PRIMARY KEY,iBeginYear int NOT NULL); "
        "IF (SELECT COUNT(*) FROM sys.columns WHERE object_id=OBJECT_ID(N'dbo.UA_Account') "
        "AND name IN(N'cAcc_Id',N'cAcc_Name',N'cUnitName',N'iYear',N'iMonth',N'cTradeKind'))<>6 "
        "RAISERROR(N'Existing UA_Account is missing required U8 columns.',16,1); "
        "IF (SELECT COUNT(*) FROM sys.columns WHERE object_id=OBJECT_ID(N'dbo.UA_AccountDatabase') "
        "AND name IN(N'cAcc_Id',N'cDatabase',N'iBeginYear'))<>3 "
        "RAISERROR(N'Existing UA_AccountDatabase is missing required U8 columns.',16,1); "
        "IF EXISTS(SELECT 1 FROM sys.columns WHERE object_id=OBJECT_ID(N'dbo.UA_Account') "
        "AND name NOT IN(N'cAcc_Id',N'cAcc_Name',N'cUnitName',N'iYear',N'iMonth',N'cTradeKind') "
        "AND is_nullable=0 AND is_identity=0 AND is_computed=0 AND default_object_id=0 AND system_type_id<>189) "
        "RAISERROR(N'Existing UA_Account requires additional fields.',16,1); "
        "IF EXISTS(SELECT 1 FROM sys.columns WHERE object_id=OBJECT_ID(N'dbo.UA_AccountDatabase') "
        "AND name NOT IN(N'cAcc_Id',N'cDatabase',N'iBeginYear') "
        "AND is_nullable=0 AND is_identity=0 AND is_computed=0 AND default_object_id=0 AND system_type_id<>189) "
        "RAISERROR(N'Existing UA_AccountDatabase requires additional fields.',16,1); "
        "COMMIT TRANSACTION; END TRY BEGIN CATCH IF @@TRANCOUNT>0 ROLLBACK TRANSACTION; "
        "DECLARE @message nvarchar(2048); SET @message=ERROR_MESSAGE(); RAISERROR(N'%s',16,1,@message); END CATCH;")
        .arg(u8Identifier(controlDatabase));
}
QString U8BackupRestorer::registrationSql(const QVector<T3RestoreSelection>& selections,
    const U8AccountMetadata& metadata, bool checkOnly, const QString& controlDatabase)
{
    if (!validateSelections(selections).isEmpty() || !QDate(metadata.year, metadata.month, 1).isValid()) return {};
    for (const auto& value : {metadata.accountName, metadata.unitName, metadata.tradeKind})
        if (value.trimmed().isEmpty() || value.size() > 120 || value.contains(QChar(0))) return {};
    const QString id = u8Literal(u8Parts(selections.first().targetDatabase).captured(2));
    QString sql = QStringLiteral("USE %1; SET XACT_ABORT ON; BEGIN TRY BEGIN TRANSACTION; "
        "IF EXISTS(SELECT 1 FROM dbo.UA_Account WITH(UPDLOCK,HOLDLOCK) WHERE cAcc_Id=%2) "
        "RAISERROR(N'Target U8 account is already registered; choose another account ID.',16,1); "
        "IF EXISTS(SELECT 1 FROM dbo.UA_AccountDatabase WITH(UPDLOCK,HOLDLOCK) WHERE cAcc_Id=%2) "
        "RAISERROR(N'Target U8 account already has database mappings.',16,1); "
        "INSERT INTO dbo.UA_Account(cAcc_Id,cAcc_Name,cUnitName,iYear,iMonth,cTradeKind) VALUES(%2,%3,%4,%5,%6,%7); ")
        .arg(u8Identifier(controlDatabase), id, u8Literal(metadata.accountName), u8Literal(metadata.unitName))
        .arg(metadata.year).arg(metadata.month).arg(u8Literal(metadata.tradeKind));
    for (const auto& item : selections) if (u8IsData(item.targetDatabase))
        sql += QStringLiteral("INSERT INTO dbo.UA_AccountDatabase(cAcc_Id,cDatabase,iBeginYear) VALUES(%1,%2,%3); ")
            .arg(id, u8Literal(item.targetDatabase), u8Parts(item.targetDatabase).captured(3));
    sql += QStringLiteral("%1 TRANSACTION; END TRY BEGIN CATCH IF @@TRANCOUNT>0 ROLLBACK TRANSACTION; "
        "DECLARE @message nvarchar(2048); SET @message=ERROR_MESSAGE(); RAISERROR(N'%s',16,1,@message); END CATCH;")
        .arg(checkOnly ? "ROLLBACK" : "COMMIT");
    return sql;
}
static bool u8Files(QSqlDatabase& db, const QString& path, int position, QVector<T3RestoreFile>* files, QString* error)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("RESTORE FILELISTONLY FROM DISK=%1 WITH FILE=%2").arg(u8Disk(path)).arg(position)))
    { *error = query.lastError().text(); return false; }
    const auto record = query.record();
    while (query.next()) files->append({query.value(record.indexOf("LogicalName")).toString(),
        query.value(record.indexOf("Type")).toString(), query.value(record.indexOf("FileId")).toInt()});
    if (query.lastError().isValid()) { *error = query.lastError().text(); return false; }
    return true;
}
static QString u8RestoreCommand(const QString& path, const T3RestoreSelection& selection,
    const QVector<T3RestoreFile>& files, const QString& data, const QString& logs, const QString& token)
{
    // Shared SQL generator supplies identifier escaping, unique file names and no-overwrite guard.
    return T3BackupRestorer::restoreSql(path, selection, files, data, logs, token);
}
static bool u8ReadMetadata(QSqlDatabase& db, const QString& database, const QString& accountId,
    U8AccountMetadata* metadata, QString* error)
{
    for (const auto& table : {QStringLiteral("UA_Account_Ex"), QStringLiteral("UA_Account")}) {
        QSqlQuery query(db);
        if (!query.exec("SELECT OBJECT_ID(" + u8Literal(database + ".dbo." + table) + ")") || !query.next())
        { *error = query.lastError().text(); return false; }
        const bool exists = !query.value(0).isNull(); query.finish(); if (!exists) continue;
        if (!query.exec(QStringLiteral("SELECT cAcc_Name,cUnitName,cTradeKind,iYear,iMonth FROM %1.dbo.%2 WHERE cAcc_Id=%3")
            .arg(u8Identifier(database), u8Identifier(table), u8Literal(accountId))))
        { *error = query.lastError().text(); return false; }
        if (!query.next()) { if (query.lastError().isValid()) { *error = query.lastError().text(); return false; } continue; }
        metadata->accountName = query.value(0).toString().trimmed(); metadata->unitName = query.value(1).toString().trimmed();
        metadata->tradeKind = query.value(2).toString().trimmed(); metadata->year = query.value(3).toInt(); metadata->month = query.value(4).toInt();
        if (query.next() || query.lastError().isValid()) { *error = QString::fromUtf8(u8"U8 账套信息不唯一或读取失败。"); return false; }
        if (!metadata->accountName.isEmpty() && !metadata->unitName.isEmpty() && !metadata->tradeKind.isEmpty()
            && QDate(metadata->year, metadata->month, 1).isValid()) return true;
    }
    *error = QString::fromUtf8(u8"未找到完整的 U8 账套名称、单位名称、准则和启用年月。"); return false;
}
static bool u8LoadMetadata(QSqlDatabase& db, const QString& path, const T3BackupSet& first,
    const QString& data, const QString& logs, U8AccountMetadata* metadata, QString* error)
{
    const QString token = QUuid::createUuid().toString(QUuid::Id128);
    const QString name = "U8Metadata_" + token;
    QVector<T3RestoreFile> files;
    if (!u8Files(db, path, first.position, &files, error)) return false;
    const QString command = u8RestoreCommand(path, {first, name}, files, data, logs, token);
    if (command.isEmpty()) { *error = QString::fromUtf8(u8"不支持备份中的数据文件类型。"); return false; }
    bool cleaned = false;
    const auto cleanup = [&]() {
        if (cleaned) return true;
        cleaned = true; QString cleanupError;
        const QString sql = QStringLiteral("USE master; IF DB_ID(%1) IS NOT NULL BEGIN "
            "IF EXISTS(SELECT 1 FROM sys.master_files WHERE database_id=DB_ID(%1) AND CHARINDEX(%2,physical_name)=0) "
            "RAISERROR(N'U8 metadata ownership mismatch.',16,1); ELSE DROP DATABASE %3; END;")
            .arg(u8Literal(name), u8Literal("T3_" + token + "_"), u8Identifier(name));
        if (u8Execute(db, sql, &cleanupError)) return true;
        *error += QString::fromUtf8(u8"\n临时信息库 %1 清理失败，已保留：%2").arg(name, cleanupError); return false;
    };
    struct Cleanup { std::function<void()> action; ~Cleanup() { action(); } } guard{[&]() { cleanup(); }};
    bool ok = u8Execute(db, command, error) && u8ReadMetadata(db, name, u8Parts(first.databaseName).captured(2), metadata, error);
    if (!cleanup()) ok = false;
    return ok;
}
T3RestoreResult U8BackupRestorer::restore(const T3SqlConnection& options, const T3BackupAnalysis& analysis,
    const QVector<T3RestoreSelection>& selections, const QString& dataDirectory, const QString& logDirectory,
    const Cancel& cancel, const Progress& progress, const QString& controlDatabase)
{
    T3RestoreResult result;
    const auto perform = [&]() {
        result.error = validateSelections(selections); if (!result.error.isEmpty()) return;
        if (!analysis.success || analysis.preparedPath.isEmpty() || dataDirectory.trimmed().isEmpty() || logDirectory.trimmed().isEmpty())
        { result.error = QString::fromUtf8(u8"请先读取备份并填写服务器数据和日志目录。"); return; }
        if (controlDatabase != "UFSystem" && !QRegularExpression("^U8TestControl_[a-f0-9]{32}$").match(controlDatabase).hasMatch())
        { result.error = QString::fromUtf8(u8"控制库名称无效。"); return; }
        U8SqlSession session; if (!session.open(options, &result.error)) return;
        const auto current = u8ReadSets(session.db, analysis.preparedPath, &result.error);
        if (!result.error.isEmpty()) return;
        T3ServerInfo server; if (!testConnection(options,&server,&result.error)) return;
        result.error=SqlBackupCompatibility::error(current,server); if (!result.error.isEmpty()) return;
        QVector<QString> commands;
        for (const auto& item : selections) {
            if (u8Cancelled(cancel)) { result.stopped = true; return; }
            bool found = false;
            for (const auto& set : current)
                if (set.position == item.backup.position && set.databaseName == item.backup.databaseName
                    && set.backupSetId == item.backup.backupSetId && set.databaseVersion == item.backup.databaseVersion) found = true;
            if (!found) { result.error = QString::fromUtf8(u8"备份内容已变化，请重新读取。"); return; }
            QSqlQuery exists(session.db);
            if (!exists.exec("SELECT DB_ID(" + u8Literal(item.targetDatabase) + ")") || !exists.next())
            { result.error = exists.lastError().text(); return; }
            if (!exists.value(0).isNull()) { result.error = QString::fromUtf8(u8"目标数据库 %1 已存在，拒绝覆盖；请统一修改目标账套号。").arg(item.targetDatabase); return; }
        }
        T3BackupSet first;
        for (const auto& set : current) if (u8IsData(set.databaseName)
            && (first.position == 0 || set.databaseName.right(4).toInt() < first.databaseName.right(4).toInt())) first = set;
        bool includesFirst = false;
        for (const auto& item : selections) if (item.backup.position == first.position) includesFirst = true;
        if (!includesFirst) {
            result.error = QString::fromUtf8(u8"U8 采集需要账套首个业务库，请同时勾选 %1。").arg(first.databaseName); return;
        }
        for (const auto& item : selections) {
            if (u8Cancelled(cancel)) { result.stopped = true; return; }
            QVector<T3RestoreFile> files;
            if (!u8Files(session.db, analysis.preparedPath, item.backup.position, &files, &result.error)) return;
            const QString command = u8RestoreCommand(analysis.preparedPath, item, files, dataDirectory, logDirectory, QUuid::createUuid().toString(QUuid::Id128));
            if (command.isEmpty()) { result.error = QString::fromUtf8(u8"数据库文件类型或目标目录无效。"); return; }
            u8Report(progress, QString::fromUtf8(u8"校验 %1...").arg(item.backup.databaseName));
            // Use exactly the future MOVE paths for SQL's space/collision validation.
            const QString restorePrefix = "RESTORE DATABASE " + u8Identifier(item.targetDatabase) + " FROM DISK=";
            const int restoreStart = command.indexOf(restorePrefix);
            const QString recoverySuffix = ", RECOVERY;";
            if (restoreStart < 0 || !command.endsWith(recoverySuffix)) {
                result.error = QString::fromUtf8(u8"无法生成备份校验命令。"); return;
            }
            QString verify = command.mid(restoreStart);
            verify.replace(0, restorePrefix.size(), "RESTORE VERIFYONLY FROM DISK=");
            verify.chop(recoverySuffix.size()); verify += ';';
            if (!u8Execute(session.db, verify, &result.error)) return;
            commands.append(command);
        }
        if (u8Cancelled(cancel)) { result.stopped = true; return; }
        U8AccountMetadata metadata;
        u8Report(progress, QString::fromUtf8(u8"读取备份内的 U8 账套信息，完成后清理临时信息库..."));
        if (!u8LoadMetadata(session.db, analysis.preparedPath, first, dataDirectory, logDirectory, &metadata, &result.error)) return;
        if (registrationSql(selections, metadata, true, controlDatabase).isEmpty())
        { result.error = QString::fromUtf8(u8"备份内账套信息格式不受支持，尚未恢复目标库。"); return; }
        if (u8Cancelled(cancel)) { result.stopped = true; return; }
        u8Report(progress, QString::fromUtf8(u8"账套：%1；启用：%2-%3；准则：%4").arg(metadata.accountName).arg(metadata.year).arg(metadata.month).arg(metadata.tradeKind));
        if (!u8Execute(session.db, T3BackupRestorer::controlDatabaseSql(dataDirectory, logDirectory,
            QUuid::createUuid().toString(QUuid::Id128), controlDatabase), &result.error)
            || !u8Execute(session.db, controlSchemaSql(controlDatabase), &result.error)
            || !u8Execute(session.db, registrationSql(selections, metadata, true, controlDatabase), &result.error)) {
            result.error = QString::fromUtf8(u8"U8 控制库登记预检失败，尚未恢复目标库。已有控制表不自动改结构：%1").arg(result.error); return;
        }
        if (!u8Execute(session.db, "USE master;", &result.error)) return;
        for (int i = 0; i < selections.size(); ++i) {
            if (u8Cancelled(cancel)) { result.stopped = true; return; }
            T3RestoreItem item; item.database = selections.at(i).targetDatabase;
            u8Report(progress, QString::fromUtf8(u8"正在恢复 %1（%2/%3）...").arg(item.database).arg(i + 1).arg(selections.size()));
            item.databaseRestored = u8Execute(session.db, commands.at(i), &item.error);
            if (item.databaseRestored) {
                QSqlQuery state(session.db);
                item.databaseRestored = state.exec("SELECT state_desc FROM sys.databases WHERE name=" + u8Literal(item.database))
                    && state.next() && state.value(0).toString() == "ONLINE";
                if (!item.databaseRestored) item.error = QString::fromUtf8(u8"尚未确认数据库 ONLINE，请检查服务器状态。");
            }
            item.error = u8Redact(item.error, options); result.items.append(item);
            if (!item.databaseRestored) { result.error = item.error; return; }
            u8Report(progress, QString::fromUtf8(u8"数据库已恢复并 ONLINE：%1；等待整组采集登记。").arg(item.database));
        }
        // Register the group atomically only after all selected databases are ONLINE.
        if (!u8Execute(session.db, registrationSql(selections, metadata, false, controlDatabase), &result.error)) {
            result.error = QString::fromUtf8(u8"数据库已保留，但 U8 采集登记失败：%1").arg(result.error); return;
        }
        for (auto& item : result.items) item.success = true;
        u8Report(progress, QString::fromUtf8(u8"全部所选数据库已恢复，U8 账套及业务库映射登记成功。"));
    };
    perform(); result.error = u8Redact(result.error, options); return result;
}
