#include "SqlBackupCompatibility.h"
#include "KisProBackupRestorer.h"
#include "RemoteFileTransfer.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <QVariant>

static QString kisLiteral(const QString& text) { return T3BackupRestorer::quoteLiteral(text); }
static QString kisIdentifier(const QString& text) { return T3BackupRestorer::quoteIdentifier(text); }
static QString kisDisk(const QString& path) { return kisLiteral(QDir::toNativeSeparators(path)); }
static bool kisCancelled(const KisProBackupRestorer::Cancel& cancel) { return cancel && cancel(); }
static void kisReport(const KisProBackupRestorer::Progress& progress, const QString& message)
{ if (progress) progress(message); }
static QString kisRedact(QString text, const T3SqlConnection& options)
{ if (!options.password.isEmpty()) text.replace(options.password, "***"); return text; }
struct KisProSqlSession
{
    QString name = "KisProRestore_" + QUuid::createUuid().toString(QUuid::Id128);
    QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", name);
    ~KisProSqlSession() { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(name); }
    bool open(const T3SqlConnection& options, QString* error)
    {
        db.setConnectOptions("SQL_ATTR_LOGIN_TIMEOUT=10;SQL_ATTR_CONNECTION_TIMEOUT=15");
        db.setDatabaseName(T3BackupRestorer::connectionString(options));
        if (db.open()) return true;
        *error = kisRedact(db.lastError().text(), options); return false;
    }
};
static bool kisExecute(QSqlDatabase& db, const QString& sql, QString* error)
{
    if (sql.isEmpty()) { *error = QString::fromUtf8(u8"SQL 参数无效。"); return false; }
    QSqlQuery query(db);
    if (!query.exec(sql)) { *error = query.lastError().text(); return false; }
    do { while (query.next()) {} } while (query.nextResult());
    if (query.lastError().isValid()) { *error = query.lastError().text(); return false; }
    return true;
}
static QVector<T3BackupSet> kisReadSets(QSqlDatabase& db, const QString& path, QString* error)
{
    QVector<T3BackupSet> sets;
    QSqlQuery query(db);
    if (!query.exec("RESTORE HEADERONLY FROM DISK=" + kisDisk(path)))
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
    if (sets.isEmpty() && error->isEmpty()) *error = QString::fromUtf8(u8"备份中没有完整数据库备份集。");
    return sets;
}
QString KisProBackupRestorer::resolveSource(const QString& path, QString* error)
{
    error->clear(); const QFileInfo info(path);
    if (info.isDir()) {
        QStringList candidates;
        for (const auto& file : QDir(info.absoluteFilePath()).entryInfoList(QDir::Files))
            if (file.suffix().compare("bak", Qt::CaseInsensitive) == 0) candidates.append(file.absoluteFilePath());
        if (candidates.size() == 1) return candidates.first();
        *error = QString::fromUtf8(u8"目录中必须只有一个 .bak；有多个备份时请选择具体文件。"); return {};
    }
    if (!info.isFile() || info.suffix().compare("bak", Qt::CaseInsensitive) != 0) {
        *error = QString::fromUtf8(u8"请选择金蝶 KIS 专业版 .bak 备份文件。"); return {};
    }
    return info.absoluteFilePath();
}
bool KisProBackupRestorer::isSupportedHeader(const QByteArray& header)
{ return header.startsWith("TAPE") || header.startsWith("MSSQLBAK"); }
QString KisProBackupRestorer::validateSelections(const QVector<T3RestoreSelection>& selections)
{
    if (selections.isEmpty()) return QString::fromUtf8(u8"请至少选择一个数据库备份。");
    const QString basic = T3BackupRestorer::validateSelections(selections);
    if (!basic.isEmpty()) return basic;
    const QRegularExpression name("^AIS[A-Za-z0-9_]+$", QRegularExpression::CaseInsensitiveOption);
    for (const auto& item : selections)
        if (!name.match(item.targetDatabase).hasMatch() || !name.match(item.backup.databaseName).hasMatch())
            return QString::fromUtf8(u8"金蝶专业版原库及目标库名须以 AIS 开头，仅含字母、数字和下划线；重名时请修改目标库名。");
    return {};
}
T3BackupAnalysis KisProBackupRestorer::analyze(const T3SqlConnection& options, const QString& source,
    const QString& stagingDirectory, const Cancel& cancel, const Progress& progress, bool useRemoteAgent)
{
    T3BackupAnalysis result;
    const QString resolved = resolveSource(source, &result.error);
    if (resolved.isEmpty()) return result;
    QFile input(resolved);
    if (!input.open(QIODevice::ReadOnly)) { result.error = input.errorString(); return result; }
    if (!isSupportedHeader(input.peek(8))) {
        result.error = QString::fromUtf8(u8"请选择金蝶专业版 SQL Server 原生 .bak 备份。"); return result;
    }
    input.close();
    if (kisCancelled(cancel)) { result.error = QString::fromUtf8(u8"已取消读取。"); return result; }
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
    if (!T3BackupRestorer::connectConfiguredShare(options, staging, &result.error)) return result;
    const auto transfer = !local && useRemoteAgent
        ? RemoteFileTransfer::upload(resolved, staging, RemoteFileTransfer::InputEncoding::RawFile, cancel, progress)
        : RemoteFileTransfer::stageCopy(resolved, staging, cancel, progress);
    result.error = transfer.error;
    if (!result.error.isEmpty()) return result;
    result.preparedPath = transfer.serverPath; result.stagingFile = transfer.job;
    KisProSqlSession session;
    if (!session.open(options, &result.error)) return result;
    result.sets = kisReadSets(session.db, result.preparedPath, &result.error);
    // A native media file can contain several full snapshots of the same database.
    // List each one; duplicate target names are checked only for the user's selection.
    for (const auto& set : result.sets) {
        if (!result.error.isEmpty()) break;
        result.error = validateSelections({{set, set.databaseName}});
    }
    if (result.error.isEmpty()) result.error = SqlBackupCompatibility::error(result.sets, result.server);
    if (kisCancelled(cancel)) result.error = QString::fromUtf8(u8"已取消读取。");
    result.error = kisRedact(result.error, options);
    result.success = result.error.isEmpty(); return result;
}
static bool kisFiles(QSqlDatabase& db, const QString& path, int position, QVector<T3RestoreFile>* files, QString* error)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("RESTORE FILELISTONLY FROM DISK=%1 WITH FILE=%2").arg(kisDisk(path)).arg(position)))
    { *error = query.lastError().text(); return false; }
    const auto record = query.record();
    while (query.next()) files->append({query.value(record.indexOf("LogicalName")).toString(),
        query.value(record.indexOf("Type")).toString(), query.value(record.indexOf("FileId")).toInt()});
    if (query.lastError().isValid()) { *error = query.lastError().text(); return false; }
    return true;
}

QString KisProBackupRestorer::controlSchemaSql(const QString& controlDatabase)
{
    return QStringLiteral("USE %1; SET XACT_ABORT ON; BEGIN TRY BEGIN TRANSACTION; "
        "IF OBJECT_ID(N'dbo.t_ad_kdAccount_gl') IS NULL CREATE TABLE dbo.t_ad_kdAccount_gl ("
        "FDBName nvarchar(128) NOT NULL PRIMARY KEY, FAcctName nvarchar(255) NOT NULL); "
        "IF (SELECT COUNT(*) FROM sys.columns WHERE object_id=OBJECT_ID(N'dbo.t_ad_kdAccount_gl') "
        "AND name IN(N'FDBName',N'FAcctName'))<>2 "
        "RAISERROR(N'Existing AcctCtl table is missing FDBName or FAcctName.',16,1); "
        "IF EXISTS(SELECT 1 FROM sys.columns WHERE object_id=OBJECT_ID(N'dbo.t_ad_kdAccount_gl') "
        "AND name NOT IN(N'FDBName',N'FAcctName') AND is_nullable=0 AND is_identity=0 "
        "AND is_computed=0 AND default_object_id=0 AND system_type_id<>189) "
        "RAISERROR(N'Existing AcctCtl table requires additional fields.',16,1); "
        "COMMIT TRANSACTION; END TRY BEGIN CATCH IF @@TRANCOUNT>0 ROLLBACK TRANSACTION; "
        "DECLARE @message nvarchar(2048); SET @message=ERROR_MESSAGE(); RAISERROR(N'%s',16,1,@message); END CATCH;")
        .arg(kisIdentifier(controlDatabase));
}
QString KisProBackupRestorer::registrationSql(const T3RestoreSelection& selection, const QString& accountName,
    bool checkOnly, const QString& controlDatabase)
{
    if (!validateSelections({selection}).isEmpty() || accountName.trimmed().isEmpty()
        || accountName.size()>255 || accountName.contains(QChar(0))) return {};
    return QStringLiteral("USE %1; SET XACT_ABORT ON; BEGIN TRY BEGIN TRANSACTION; "
        "IF EXISTS(SELECT 1 FROM dbo.t_ad_kdAccount_gl WITH(UPDLOCK,HOLDLOCK) WHERE FDBName=%2) "
        "RAISERROR(N'Target KIS database is already registered; choose another name.',16,1); "
        "INSERT INTO dbo.t_ad_kdAccount_gl(FDBName,FAcctName) VALUES(%2,%3); "
        "%4 TRANSACTION; END TRY BEGIN CATCH IF @@TRANCOUNT>0 ROLLBACK TRANSACTION; "
        "DECLARE @message nvarchar(2048); SET @message=ERROR_MESSAGE(); RAISERROR(N'%s',16,1,@message); END CATCH;")
        .arg(kisIdentifier(controlDatabase),kisLiteral(selection.targetDatabase),kisLiteral(accountName),
            checkOnly ? QStringLiteral("ROLLBACK") : QStringLiteral("COMMIT"));
}
static bool kisReadAccount(QSqlDatabase& db, const QString& database, QString* name, QString* error)
{
    QSqlQuery query(db);
    // Same CompanyName field as KisProExportHandler::InitAccountSet, never infer from filenames.
    if (!query.exec(QStringLiteral("SELECT FValue FROM %1.dbo.t_SystemProfile "
        "WHERE FCategory=N'General' AND FKey=N'CompanyName'").arg(kisIdentifier(database))))
    { *error=query.lastError().text(); return false; }
    if (!query.next()) { *error=QString::fromUtf8(u8"账套缺少 t_SystemProfile 公司名称，数据库已保留。"); return false; }
    *name=query.value(0).toString();
    if (query.next() || query.lastError().isValid() || name->trimmed().isEmpty()
        || name->size()>255 || name->contains(QChar(0))) {
        *error=QString::fromUtf8(u8"账套公司名称为空、不唯一或格式无效，数据库已保留。"); return false;
    }
    return true;
}
T3RestoreResult KisProBackupRestorer::restore(const T3SqlConnection& options, const T3BackupAnalysis& analysis,
    const QVector<T3RestoreSelection>& selections, const QString& dataDirectory, const QString& logDirectory,
    const Cancel& cancel, const Progress& progress, const QString& controlDatabase)
{
    T3RestoreResult result;
    const auto perform = [&]() {
        result.error=validateSelections(selections); if (!result.error.isEmpty()) return;
        if (!analysis.success || analysis.preparedPath.isEmpty() || dataDirectory.trimmed().isEmpty() || logDirectory.trimmed().isEmpty())
        { result.error=QString::fromUtf8(u8"请先读取备份并填写服务器数据和日志目录。"); return; }
        if (controlDatabase!="AcctCtl" && !QRegularExpression("^KisProTestControl_[a-f0-9]{32}$").match(controlDatabase).hasMatch())
        { result.error=QString::fromUtf8(u8"控制库名称无效。"); return; }
        if (kisCancelled(cancel)) { result.stopped=true; return; }
        KisProSqlSession session; if (!session.open(options,&result.error)) return;
        auto current=kisReadSets(session.db,analysis.preparedPath,&result.error);
        if (!result.error.isEmpty()) return;
        T3ServerInfo server; if (!testConnection(options,&server,&result.error)) return;
        result.error=SqlBackupCompatibility::error(current,server); if (!result.error.isEmpty()) return;
        for (const auto& item:selections) {
            bool found=false;
            for (const auto& set:current)
                if (set.position==item.backup.position && set.databaseName==item.backup.databaseName
                    && set.backupSetId==item.backup.backupSetId && set.databaseVersion==item.backup.databaseVersion) found=true;
            if (!found) { result.error=QString::fromUtf8(u8"备份内容已变化，请重新读取。"); return; }
            QSqlQuery query(session.db);
            if (!query.exec("SELECT DB_ID("+kisLiteral(item.targetDatabase)+")") || !query.next())
            { result.error=query.lastError().text(); return; }
            if (!query.value(0).isNull()) { result.error=QString::fromUtf8(u8"目标数据库 %1 已存在，拒绝覆盖；请修改目标库名。").arg(item.targetDatabase); return; }
        }
        QVector<QString> commands;
        for (const auto& item:selections) {
            if (kisCancelled(cancel)) { result.stopped=true; return; }
            QVector<T3RestoreFile> files;
            if (!kisFiles(session.db,analysis.preparedPath,item.backup.position,&files,&result.error)) return;
            const QString command=T3BackupRestorer::restoreSql(analysis.preparedPath,item,files,dataDirectory,logDirectory,QUuid::createUuid().toString(QUuid::Id128));
            const QString prefix="RESTORE DATABASE "+kisIdentifier(item.targetDatabase)+" FROM DISK=";
            const QString suffix=", RECOVERY;";
            const int start=command.indexOf(prefix);
            if (start<0 || !command.endsWith(suffix)) { result.error=QString::fromUtf8(u8"数据库文件类型或目标目录无效。"); return; }
            QString verify=command.mid(start); verify.replace(0,prefix.size(),"RESTORE VERIFYONLY FROM DISK=");
            verify.chop(suffix.size()); verify+=';';
            kisReport(progress,QString::fromUtf8(u8"校验 %1...").arg(item.backup.databaseName));
            if (!kisExecute(session.db,verify,&result.error)) return;
            commands.append(command);
        }
        if (kisCancelled(cancel)) { result.stopped=true; return; }
        if (!kisExecute(session.db,T3BackupRestorer::controlDatabaseSql(dataDirectory,logDirectory,
            QUuid::createUuid().toString(QUuid::Id128),controlDatabase),&result.error)
            || !kisExecute(session.db,controlSchemaSql(controlDatabase),&result.error)) {
            result.error=QString::fromUtf8(u8"AcctCtl 控制表不兼容，尚未恢复目标库：%1").arg(result.error); return;
        }
        for (const auto& item:selections)
            if (!kisExecute(session.db,registrationSql(item,item.targetDatabase,true,controlDatabase),&result.error)) {
                result.error=QString::fromUtf8(u8"控制库登记预检失败，尚未恢复目标库：%1").arg(result.error); return;
            }
        if (!kisExecute(session.db,"USE master;",&result.error)) return;
        for (int i=0;i<selections.size();++i) {
            if (kisCancelled(cancel)) { result.stopped=true; return; }
            T3RestoreItem item; item.database=selections[i].targetDatabase;
            kisReport(progress,QString::fromUtf8(u8"正在恢复 %1（%2/%3）...").arg(item.database).arg(i+1).arg(selections.size()));
            item.databaseRestored=kisExecute(session.db,commands[i],&item.error);
            if (item.databaseRestored) {
                QSqlQuery state(session.db);
                item.databaseRestored=state.exec("SELECT state_desc FROM sys.databases WHERE name="+kisLiteral(item.database))
                    && state.next() && state.value(0).toString()=="ONLINE";
                if (!item.databaseRestored) item.error=QString::fromUtf8(u8"尚未确认数据库 ONLINE，请检查服务器状态。");
            }
            if (item.databaseRestored) {
                QString name;
                item.success=kisReadAccount(session.db,item.database,&name,&item.error)
                    && kisExecute(session.db,registrationSql(selections[i],name,false,controlDatabase),&item.error);
                if (item.success) kisReport(progress,QString::fromUtf8(u8"恢复及采集登记成功：%1 → %2").arg(name,item.database));
                else item.error=QString::fromUtf8(u8"数据库 %1 已恢复并保留，但采集登记失败：%2").arg(item.database,item.error);
            }
            item.error=kisRedact(item.error,options); result.items.append(item);
            if (!item.success) { result.error=item.error; return; }
            if (!kisExecute(session.db,"USE master;",&result.error)) return;
        }
    };
    perform(); result.error=kisRedact(result.error,options); return result;
}
