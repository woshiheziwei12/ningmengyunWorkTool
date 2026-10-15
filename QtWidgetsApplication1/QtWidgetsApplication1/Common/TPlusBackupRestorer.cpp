#include "SqlBackupCompatibility.h"
#include "TPlusBackupRestorer.h"
#include "RemoteFileTransfer.h"
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryDir>
#include <QUuid>
#include <QVariant>
#include <QXmlStreamReader>

namespace {
QString literal(const QString& value) { return T3BackupRestorer::quoteLiteral(value); }
QString identifier(const QString& value) { return T3BackupRestorer::quoteIdentifier(value); }
QString disk(const QString& value) { return literal(QDir::toNativeSeparators(value)); }
bool cancelled(const TPlusBackupRestorer::Cancel& cancel) { return cancel && cancel(); }
void report(const TPlusBackupRestorer::Progress& progress, const QString& message) { if (progress) progress(message); }
bool validDatabase(const QString& name)
{
    return name.size() <= 128 && QRegularExpression("^UFTData[a-zA-Z0-9_]+$",
        QRegularExpression::CaseInsensitiveOption).match(name).hasMatch();
}
bool validMetadata(const TPlusAccountMetadata& account)
{
    return validDatabase(account.databaseName) && !account.accountName.trimmed().isEmpty()
        && account.accountName.size() <= 200 && !account.accountName.contains(QChar(0))
        && QDate(account.year, account.month, 1).isValid() && account.year >= 1900
        && QRegularExpression("^[0-9]{2}\\.[0-9]{3}\\.[0-9]{3}\\.[0-9]{4}$").match(account.version).hasMatch();
}
struct Session
{
    QString name = "TPlusRestore_" + QUuid::createUuid().toString(QUuid::Id128);
    QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", name);
    ~Session() { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(name); }
    bool open(const T3SqlConnection& options, QString* error)
    {
        db.setConnectOptions("SQL_ATTR_LOGIN_TIMEOUT=10;SQL_ATTR_CONNECTION_TIMEOUT=15");
        db.setDatabaseName(T3BackupRestorer::connectionString(options));
        if (db.open()) return true;
        *error = db.lastError().text(); return false;
    }
};
bool execute(QSqlDatabase& db, const QString& sql, QString* error)
{
    if (sql.isEmpty()) { *error = QString::fromUtf8(u8"SQL 参数无效。"); return false; }
    QSqlQuery query(db);
    if (!query.exec(sql)) { *error = query.lastError().text(); return false; }
    do { while (query.next()) {} } while (query.nextResult());
    if (query.lastError().isValid()) { *error = query.lastError().text(); return false; }
    return true;
}
QVector<T3BackupSet> readSets(QSqlDatabase& db, const QString& path, QString* error)
{
    QVector<T3BackupSet> sets;
    QSqlQuery query(db);
    if (!query.exec("RESTORE HEADERONLY FROM DISK=" + disk(path)))
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
        set.softwareVersionMinor=value("SoftwareVersionMinor").toInt(); sets.append(set);
    }
    if (query.lastError().isValid()) *error = query.lastError().text();
    if (error->isEmpty() && (sets.size() != 1 || !validDatabase(sets.first().databaseName)
        || sets.first().backupSetId.isEmpty()))
        *error = QString::fromUtf8(u8"备份须包含一个完整的 UFTData 业务库备份集。");
    return sets;
}
// Paths travel through the environment; no archive name or path is executable script text.
// Extract only two identified entries into fixed names, never archive-provided paths.
bool unpack(const QString& source, const QString& directory, QString* error,
    const TPlusBackupRestorer::Cancel& cancel)
{
    const QString script = QString::fromLatin1(R"PS(
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive=[IO.Compression.ZipFile]::OpenRead($env:LEMON_TPLUS_SOURCE)
try {
  $entries=@($archive.Entries | Where-Object { $_.Name -ne '' })
  $metadata=@($entries | Where-Object { $_.Name -ieq 'AdditionalXml.xml' })
  $backups=@($entries | Where-Object { $_.Name -ine 'AdditionalXml.xml' })
  if($metadata.Count -ne 1 -or $backups.Count -ne 1){throw 'Expected AdditionalXml.xml and one database backup.'}
  foreach($entry in $entries){
    if($entry.FullName -match '(^[/\\]|(^|[/\\])\.\.([/\\]|$)|:)'){throw 'Unsafe ZIP entry path.'}
    $isXml=$entry.Name -ieq 'AdditionalXml.xml'
    $limit=if($isXml){1048576L}else{68719476736L}
    if($entry.Length -le 0 -or $entry.Length -gt $limit){throw 'ZIP entry size is not supported.'}
    $name=if($isXml){'AdditionalXml.xml'}else{'database.bak'}
    $entryStream=$entry.Open()
    try {
      $output=[IO.File]::Open([IO.Path]::Combine($env:LEMON_TPLUS_OUTPUT,$name),[IO.FileMode]::CreateNew)
      try {
        $buffer=New-Object byte[] 1048576
        $total=0L
        while(($count=$entryStream.Read($buffer,0,$buffer.Length)) -gt 0){
          $total+=$count
          if($total -gt $limit -or $total -gt $entry.Length){throw 'ZIP expanded size mismatch.'}
          $output.Write($buffer,0,$count)
        }
        if($total -ne $entry.Length){throw 'ZIP entry is truncated.'}
      } finally { $output.Dispose() }
    } finally { $entryStream.Dispose() }
  }
} finally { $archive.Dispose() }
)PS");
    if (cancelled(cancel)) { *error = QString::fromUtf8(u8"已取消解包。"); return false; }
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("LEMON_TPLUS_SOURCE", source); environment.insert("LEMON_TPLUS_OUTPUT", directory);
    process.setProcessEnvironment(environment);
    process.start("powershell.exe", {"-NoLogo", "-NoProfile", "-NonInteractive", "-Command", script});
    if (!process.waitForStarted(10000)) { *error = process.errorString(); return false; }
    // The script has no stdin contract. Send EOF so PowerShell cannot wait on the parent pipe.
    process.closeWriteChannel();
    while (!process.waitForFinished(100)) {
        if (cancelled(cancel)) {
            process.kill(); process.waitForFinished(-1);
            *error = QString::fromUtf8(u8"已取消解包。"); return false;
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        *error = QString::fromUtf8(u8"T+ ZIP 解包失败：%1").arg(QString::fromLocal8Bit(process.readAllStandardError())); return false;
    }
    return true;
}
}

QString TPlusBackupRestorer::resolveSource(const QString& path, QString* error)
{
    error->clear(); const QFileInfo info(path);
    if (!info.isFile() || info.suffix().compare("zip", Qt::CaseInsensitive) != 0) {
        *error = QString::fromUtf8(u8"请选择含 AdditionalXml.xml 的 T+ ZIP 备份包。"); return {};
    }
    return info.absoluteFilePath();
}
bool TPlusBackupRestorer::readMetadata(const QByteArray& xml, TPlusAccountMetadata* account, QString* error)
{
    error->clear(); *account = {};
    if (xml.size() > 1024 * 1024) { *error = QString::fromUtf8(u8"T+ 账套信息文件过大。"); return false; }
    QXmlStreamReader reader(xml); QMap<QString, QString> fields; bool root = false;
    const QStringList required{"DsName", "AccountName", "Year", "Duration", "VersionNum"};
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isDTD() || reader.isEntityReference()) { reader.raiseError("DTD/entities are not supported"); break; }
        if (!reader.isStartElement()) continue;
        if (!root) {
            if (reader.name() != "BackupFileAdditionalInfo") { reader.raiseError("Invalid T+ manifest root"); break; }
            root = true; continue;
        }
        const QString name = reader.name().toString();
        if (required.contains(name)) {
            if (fields.contains(name)) { reader.raiseError("Duplicate manifest field"); break; }
            fields.insert(name, reader.readElementText().trimmed());
        } else reader.skipCurrentElement();
    }
    account->databaseName = fields.value("DsName"); account->accountName = fields.value("AccountName");
    account->version = fields.value("VersionNum"); account->year = fields.value("Year").toInt();
    account->month = fields.value("Duration").toInt();
    if (reader.hasError() || !root || !validMetadata(*account)) {
        *error = QString::fromUtf8(u8"T+ 账套信息不完整或格式无效（库名、名称、启用年月、版本号）：%1").arg(reader.errorString()); return false;
    }
    return true;
}
QString TPlusBackupRestorer::validateSelections(const QVector<T3RestoreSelection>& selections)
{
    const QString error = T3BackupRestorer::validateSelections(selections);
    if (!error.isEmpty()) return error;
    if (selections.size() != 1 || !validDatabase(selections.first().backup.databaseName)
        || !validDatabase(selections.first().targetDatabase))
        return QString::fromUtf8(u8"请选择一个 T+ 业务库；目标库名须以 UFTData 开头，后接字母、数字或下划线，最长 128 字符。");
    return {};
}
TPlusBackupAnalysis TPlusBackupRestorer::analyze(const T3SqlConnection& options, const QString& source,
    const QString& stagingDirectory, const Cancel& cancel, const Progress& progress, bool useRemoteAgent)
{
    TPlusBackupAnalysis result;
    const auto perform = [&]() {
        const QString resolved = resolveSource(source, &result.error); if (resolved.isEmpty()) return;
        QTemporaryDir extracted(QDir::tempPath() + "/lemon-tplus-XXXXXX");
        if (!extracted.isValid()) { result.error = QString::fromUtf8(u8"无法创建解包临时目录。"); return; }
        report(progress, QString::fromUtf8(u8"正在解包 T+ ZIP 并读取账套信息..."));
        if (!unpack(resolved, extracted.path(), &result.error, cancel)) return;
        QFile xml(extracted.filePath("AdditionalXml.xml"));
        if (!xml.open(QIODevice::ReadOnly)) { result.error = xml.errorString(); return; }
        if (!readMetadata(xml.readAll(), &result.account, &result.error)) return;
        QFile backup(extracted.filePath("database.bak"));
        if (!backup.open(QIODevice::ReadOnly)) { result.error = backup.errorString(); return; }
        const auto header = backup.peek(8); backup.close();
        if (!header.startsWith("MSSQLBAK") && !header.startsWith("TAPE")) {
            result.error = QString::fromUtf8(u8"ZIP 内文件不是 SQL Server 原生备份。"); return;
        }
        if (cancelled(cancel)) { result.error = QString::fromUtf8(u8"已取消读取。"); return; }
        if (!testConnection(options, &result.server, &result.error)) return;
        const bool local = T3BackupRestorer::isLocalServer(options.server);
        QString staging = stagingDirectory.trimmed();
        if (local && staging.isEmpty()) staging = defaultLocalStagingDirectory();
        if (local && !QDir().mkpath(staging)) { result.error = QString::fromUtf8(u8"无法创建备份暂存目录。"); return; }
        if (!local && !QDir::fromNativeSeparators(staging).startsWith("//")) {
            result.error = QString::fromUtf8(u8"远程恢复需要 SQL Server 可读取的 UNC 共享暂存目录。"); return;
        }
        const auto transfer = !local && useRemoteAgent
            ? RemoteFileTransfer::upload(backup.fileName(), staging, RemoteFileTransfer::InputEncoding::RawFile, cancel, progress)
            : RemoteFileTransfer::stageCopy(backup.fileName(), staging, cancel, progress);
        result.error = transfer.error; if (!result.error.isEmpty()) return;
        result.preparedPath = transfer.serverPath; result.stagingFile = transfer.job;
        Session session; if (!session.open(options, &result.error)) return;
        result.sets = readSets(session.db, result.preparedPath, &result.error);
        if (!result.error.isEmpty()) return;
        if (result.sets.first().databaseName.compare(result.account.databaseName, Qt::CaseInsensitive) != 0) {
            result.error = QString::fromUtf8(u8"备份原库名与 AdditionalXml.xml 的 DsName 不一致，拒绝登记错误账套。"); return;
        }
        result.error = SqlBackupCompatibility::error(result.sets, result.server);
        if (!result.error.isEmpty()) return;
        if (cancelled(cancel)) { result.error = QString::fromUtf8(u8"已取消读取。"); return; }
        result.sourceServer = options.server;
        report(progress, QString::fromUtf8(u8"账套：%1；启用：%2-%3；T+ 版本：%4。恢复时将重建 UFTSystem 采集控制库登记。")
            .arg(result.account.accountName).arg(result.account.year).arg(result.account.month).arg(result.account.version));
        result.success = true;
    };
    perform(); if (!options.password.isEmpty()) result.error.replace(options.password, "***"); return result;
}

QString TPlusBackupRestorer::controlSchemaSql(const QString& controlDatabase)
{
    return QStringLiteral("USE %1; SET XACT_ABORT ON; BEGIN TRY BEGIN TRANSACTION; "
        "IF OBJECT_ID(N'dbo.EAP_Account') IS NULL CREATE TABLE dbo.EAP_Account ("
        "DsName nvarchar(128) NOT NULL PRIMARY KEY,cAcc_Name nvarchar(200) NOT NULL,"
        "iYear int NOT NULL,iDuration int NOT NULL,VersionNo nvarchar(32) NOT NULL); "
        "IF (SELECT COUNT(*) FROM sys.columns WHERE object_id=OBJECT_ID(N'dbo.EAP_Account') "
        "AND name IN(N'DsName',N'cAcc_Name',N'iYear',N'iDuration',N'VersionNo'))<>5 "
        "RAISERROR(N'Existing EAP_Account is missing required T+ columns.',16,1); "
        "IF EXISTS(SELECT 1 FROM sys.columns WHERE object_id=OBJECT_ID(N'dbo.EAP_Account') "
        "AND name NOT IN(N'DsName',N'cAcc_Name',N'iYear',N'iDuration',N'VersionNo') "
        "AND is_nullable=0 AND is_identity=0 AND is_computed=0 AND default_object_id=0 AND system_type_id<>189) "
        "RAISERROR(N'Existing EAP_Account requires additional fields.',16,1); "
        "COMMIT TRANSACTION; END TRY BEGIN CATCH IF @@TRANCOUNT>0 ROLLBACK TRANSACTION; "
        "DECLARE @message nvarchar(2048); SET @message=ERROR_MESSAGE(); RAISERROR(N'%s',16,1,@message); END CATCH;")
        .arg(identifier(controlDatabase));
}
QString TPlusBackupRestorer::registrationSql(const T3RestoreSelection& selection,
    const TPlusAccountMetadata& account, bool checkOnly, const QString& controlDatabase)
{
    if (!validateSelections({selection}).isEmpty() || !validMetadata(account)
        || selection.backup.databaseName.compare(account.databaseName, Qt::CaseInsensitive) != 0) return {};
    // Exercise the actual insert (including existing constraints) before RESTORE; never update other accounts.
    return QStringLiteral("USE %1; SET XACT_ABORT ON; BEGIN TRY BEGIN TRANSACTION; "
        "IF EXISTS(SELECT 1 FROM dbo.EAP_Account WITH(UPDLOCK,HOLDLOCK) WHERE DsName=%2) "
        "RAISERROR(N'Target T+ database is already registered; choose another name.',16,1); "
        "INSERT INTO dbo.EAP_Account(DsName,cAcc_Name,iYear,iDuration,VersionNo) VALUES(%2,%3,%4,%5,%6); "
        "%7 TRANSACTION; END TRY BEGIN CATCH IF @@TRANCOUNT>0 ROLLBACK TRANSACTION; "
        "DECLARE @message nvarchar(2048); SET @message=ERROR_MESSAGE(); RAISERROR(N'%s',16,1,@message); END CATCH;")
        .arg(identifier(controlDatabase), literal(selection.targetDatabase), literal(account.accountName),
            QString::number(account.year), QString::number(account.month), literal(account.version),
            checkOnly ? "ROLLBACK" : "COMMIT");
}
T3RestoreResult TPlusBackupRestorer::restore(const T3SqlConnection& options, const TPlusBackupAnalysis& analysis,
    const QVector<T3RestoreSelection>& selections, const QString& dataDirectory, const QString& logDirectory,
    const Cancel& cancel, const Progress& progress, const QString& controlDatabase)
{
    T3RestoreResult result;
    const auto perform = [&]() {
        result.error = validateSelections(selections); if (!result.error.isEmpty()) return;
        if (!analysis.success || analysis.preparedPath.isEmpty() || !validMetadata(analysis.account)
            || options.server != analysis.sourceServer || dataDirectory.trimmed().isEmpty() || logDirectory.trimmed().isEmpty()) {
            result.error = QString::fromUtf8(u8"请先在当前实例读取备份，并填写服务器数据和日志目录。"); return;
        }
        if (controlDatabase != "UFTSystem" && !QRegularExpression("^TPlusTestControl_[a-f0-9]{32}$").match(controlDatabase).hasMatch()) {
            result.error = QString::fromUtf8(u8"控制库名称无效。"); return;
        }
        if (cancelled(cancel)) { result.stopped = true; return; }
        Session session; if (!session.open(options, &result.error)) return;
        const auto current = readSets(session.db, analysis.preparedPath, &result.error);
        if (!result.error.isEmpty()) return;
        T3ServerInfo server; if (!testConnection(options,&server,&result.error)) return;
        result.error=SqlBackupCompatibility::error(current,server); if (!result.error.isEmpty()) return;
        const auto& selection = selections.first(); const auto& actual = current.first();
        if (actual.position != selection.backup.position || actual.databaseName != selection.backup.databaseName
            || actual.backupSetId != selection.backup.backupSetId || actual.databaseVersion != selection.backup.databaseVersion
            || registrationSql(selection, analysis.account, true, controlDatabase).isEmpty()) {
            result.error = QString::fromUtf8(u8"备份内容或账套信息已变化，请重新读取。"); return;
        }
        QSqlQuery exists(session.db);
        if (!exists.exec("SELECT DB_ID(" + literal(selection.targetDatabase) + ")") || !exists.next()) {
            result.error = exists.lastError().text(); return;
        }
        if (!exists.value(0).isNull()) { result.error = QString::fromUtf8(u8"目标数据库已存在，拒绝覆盖；请修改目标库名。"); return; }
        exists.finish();
        QVector<T3RestoreFile> files; QSqlQuery list(session.db);
        if (!list.exec(QStringLiteral("RESTORE FILELISTONLY FROM DISK=%1 WITH FILE=%2").arg(disk(analysis.preparedPath)).arg(actual.position))) {
            result.error = list.lastError().text(); return;
        }
        const auto columns = list.record();
        while (list.next()) files.append({list.value(columns.indexOf("LogicalName")).toString(),
            list.value(columns.indexOf("Type")).toString(), list.value(columns.indexOf("FileId")).toInt()});
        if (list.lastError().isValid()) { result.error = list.lastError().text(); return; }
        list.finish();
        const QString command = T3BackupRestorer::restoreSql(analysis.preparedPath, selection, files,
            dataDirectory, logDirectory, QUuid::createUuid().toString(QUuid::Id128));
        const QString prefix = "RESTORE DATABASE " + identifier(selection.targetDatabase) + " FROM DISK=";
        const int start = command.indexOf(prefix); const QString suffix = ", RECOVERY;";
        if (start < 0 || !command.endsWith(suffix)) { result.error = QString::fromUtf8(u8"数据库文件类型或目标目录无效。"); return; }
        QString verify = command.mid(start); verify.replace(0, prefix.size(), "RESTORE VERIFYONLY FROM DISK=");
        verify.chop(suffix.size()); verify += ';';
        report(progress, QString::fromUtf8(u8"正在校验 T+ 备份和目标文件路径..."));
        if (!execute(session.db, verify, &result.error)) return;
        if (cancelled(cancel)) { result.stopped = true; return; }
        if (!execute(session.db, T3BackupRestorer::controlDatabaseSql(dataDirectory, logDirectory,
            QUuid::createUuid().toString(QUuid::Id128), controlDatabase), &result.error)
            || !execute(session.db, controlSchemaSql(controlDatabase), &result.error)
            || !execute(session.db, registrationSql(selection, analysis.account, true, controlDatabase), &result.error)) {
            result.error = QString::fromUtf8(u8"T+ 控制库预检失败，尚未恢复业务库；已有表不自动改结构：%1").arg(result.error); return;
        }
        if (cancelled(cancel)) { result.stopped = true; return; }
        if (!execute(session.db, "USE master;", &result.error)) return;
        report(progress, QString::fromUtf8(u8"正在恢复 %1...").arg(selection.targetDatabase));
        T3RestoreItem item; item.database = selection.targetDatabase;
        item.databaseRestored = execute(session.db, command, &item.error);
        if (item.databaseRestored) {
            QSqlQuery state(session.db);
            item.databaseRestored = state.exec("SELECT state_desc FROM sys.databases WHERE name=" + literal(item.database))
                && state.next() && state.value(0).toString() == "ONLINE";
            if (!item.databaseRestored) item.error = QString::fromUtf8(u8"尚未确认数据库 ONLINE，请检查服务器状态。");
        }
        result.items.append(item);
        if (!item.databaseRestored) { result.error = item.error; return; }
        // Completing the current database includes registration, even if stop was requested during RESTORE.
        if (!execute(session.db, registrationSql(selection, analysis.account, false, controlDatabase), &result.error)) {
            result.error = QString::fromUtf8(u8"业务库已恢复并保留，但 T+ 控制库登记失败：%1").arg(result.error); return;
        }
        result.items.first().success = true;
        report(progress, QString::fromUtf8(u8"T+ 业务库已 ONLINE，控制库 %1 的账套登记成功。可使用导账工具采集。").arg(controlDatabase));
    };
    perform();
    if (!options.password.isEmpty()) {
        result.error.replace(options.password, "***"); for (auto& item : result.items) item.error.replace(options.password, "***");
    }
    return result;
}
