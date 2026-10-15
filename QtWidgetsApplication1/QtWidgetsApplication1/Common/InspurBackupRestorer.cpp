#include "SqlBackupCompatibility.h"
#include "InspurBackupRestorer.h"
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
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTextCodec>

static QString inspurLiteral(const QString& text) { return T3BackupRestorer::quoteLiteral(text); }
static QString inspurIdentifier(const QString& text) { return T3BackupRestorer::quoteIdentifier(text); }
static QString inspurDisk(const QString& path) { return inspurLiteral(QDir::toNativeSeparators(path)); }
static bool inspurCancelled(const InspurBackupRestorer::Cancel& cancel) { return cancel && cancel(); }
static void inspurReport(const InspurBackupRestorer::Progress& progress, const QString& message)
{ if (progress) progress(message); }
static QString inspurRedact(QString text, const T3SqlConnection& options)
{ if (!options.password.isEmpty()) text.replace(options.password, "***"); return text; }
struct InspurSqlSession
{
    QString name = "InspurRestore_" + QUuid::createUuid().toString(QUuid::Id128);
    QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", name);
    ~InspurSqlSession() { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(name); }
    bool open(const T3SqlConnection& options, QString* error)
    {
        db.setConnectOptions("SQL_ATTR_LOGIN_TIMEOUT=10;SQL_ATTR_CONNECTION_TIMEOUT=15");
        db.setDatabaseName(T3BackupRestorer::connectionString(options));
        if (db.open()) return true;
        *error = inspurRedact(db.lastError().text(), options); return false;
    }
};
static bool inspurExecute(QSqlDatabase& db, const QString& sql, QString* error)
{
    if (sql.isEmpty()) { *error = QString::fromUtf8(u8"SQL 参数无效。"); return false; }
    QSqlQuery query(db);
    if (!query.exec(sql)) { *error = query.lastError().text(); return false; }
    do { while (query.next()) {} } while (query.nextResult());
    if (query.lastError().isValid()) { *error = query.lastError().text(); return false; }
    return true;
}
static QVector<T3BackupSet> inspurReadSets(QSqlDatabase& db, const QString& path, QString* error)
{
    QVector<T3BackupSet> sets;
    QSqlQuery query(db);
    if (!query.exec("RESTORE HEADERONLY FROM DISK=" + inspurDisk(path)))
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
static bool validName(const QString& name)
{
    return name.size() <= 128 && QRegularExpression("^[A-Za-z_][A-Za-z0-9_]*$").match(name).hasMatch();
}
QString InspurBackupRestorer::resolveSource(const QString& path, QString* error)
{
    error->clear(); const QFileInfo info(path);
    if (!info.isFile() || !QStringList{"mss", "bak"}.contains(info.suffix().toLower())) {
        *error = QString::fromUtf8(u8"请选择浪潮 .MSS 备份包或 SQL Server .BAK 备份文件。"); return {};
    }
    return info.absoluteFilePath();
}
QString InspurBackupRestorer::validateSelections(const QVector<T3RestoreSelection>& selections)
{
    const QString basic = T3BackupRestorer::validateSelections(selections);
    if (!basic.isEmpty()) return basic;
    if (selections.size() != 1) return QString::fromUtf8(u8"一次请选择一个浪潮业务库备份。");
    const auto& item = selections.first();
    for (const auto& name : {item.targetDatabase, item.backup.databaseName})
        if (!validName(name) || QStringList{"master","model","msdb","tempdb","cwmaster"}.contains(name.toLower()))
            return QString::fromUtf8(u8"业务库名须以字母或下划线开头，仅含字母、数字、下划线，最长 128 字符；不能使用系统库或 cwmaster。");
    return {};
}
InspurBackupAnalysis InspurBackupRestorer::analyze(const T3SqlConnection& options, const QString& source,
    const QString& stagingDirectory, const Cancel& cancel, const Progress& progress, bool useRemoteAgent)
{
    InspurBackupAnalysis result;
    const QString resolved = resolveSource(source, &result.error);
    if (resolved.isEmpty()) return result;
    QTemporaryDir extracted(QDir::tempPath() + "/lemon-inspur-XXXXXX");
    QString native = resolved;
    if (QFileInfo(resolved).suffix().compare("mss", Qt::CaseInsensitive) == 0) {
        if (!extracted.isValid()) { result.error = QString::fromUtf8(u8"无法创建解包临时目录。"); return result; }
        inspurReport(progress, QString::fromUtf8(u8"正在解包浪潮 MSS 备份..."));
        if (!unpack(resolved, extracted.path(), &result.error, cancel)) return result;
        QFile ini(extracted.filePath("account.ini"));
        if (!ini.open(QIODevice::ReadOnly)) { result.error = ini.errorString(); return result; }
        if (!readMetadata(ini.readAll(), &result.account, &result.error)) return result;
        native = extracted.filePath("database.bak");
    }
    QFile input(native);
    if (!input.open(QIODevice::ReadOnly)) { result.error = input.errorString(); return result; }
    if (!input.peek(8).startsWith("TAPE") && !input.peek(8).startsWith("MSSQLBAK")) {
        result.error = QString::fromUtf8(u8"请选择浪潮 SQL Server 原生备份。"); return result;
    }
    input.close();
    if (inspurCancelled(cancel)) { result.error = QString::fromUtf8(u8"已取消读取。"); return result; }
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
        ? RemoteFileTransfer::upload(native, staging, RemoteFileTransfer::InputEncoding::RawFile, cancel, progress)
        : RemoteFileTransfer::stageCopy(native, staging, cancel, progress);
    result.error = transfer.error;
    if (!result.error.isEmpty()) return result;
    result.preparedPath = transfer.serverPath; result.stagingFile = transfer.job;
    InspurSqlSession session;
    if (!session.open(options, &result.error)) return result;
    result.sets = inspurReadSets(session.db, result.preparedPath, &result.error);
    if (result.error.isEmpty() && (result.sets.size() != 1 || result.sets.first().backupSetId.isEmpty()))
        result.error = QString::fromUtf8(u8"请选择只包含一个完整业务库备份集的浪潮备份。");
    if (result.error.isEmpty()) result.error = validateSelections({{result.sets.first(), result.sets.first().databaseName}});
    if (result.error.isEmpty() && !result.account.databaseName.isEmpty()
        && result.account.databaseName.compare(result.sets.first().databaseName, Qt::CaseInsensitive) != 0)
        result.error = QString::fromUtf8(u8"INI 中的数据库名与 SQL 备份不一致，拒绝恢复。");
    if (result.error.isEmpty()) result.error = SqlBackupCompatibility::error(result.sets, result.server);
    if (inspurCancelled(cancel)) result.error = QString::fromUtf8(u8"已取消读取。");
    result.error = inspurRedact(result.error, options);
    result.success = result.error.isEmpty(); return result;
}
static bool inspurFiles(QSqlDatabase& db, const QString& path, int position, QVector<T3RestoreFile>* files, QString* error)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("RESTORE FILELISTONLY FROM DISK=%1 WITH FILE=%2").arg(inspurDisk(path)).arg(position)))
    { *error = query.lastError().text(); return false; }
    const auto record = query.record();
    while (query.next()) files->append({query.value(record.indexOf("LogicalName")).toString(),
        query.value(record.indexOf("Type")).toString(), query.value(record.indexOf("FileId")).toInt()});
    if (query.lastError().isValid()) { *error = query.lastError().text(); return false; }
    return true;
}

bool InspurBackupRestorer::readMetadata(const QByteArray& ini, InspurAccountMetadata* account, QString* error)
{
    *account = {}; error->clear();
    if (ini.isEmpty() || ini.size() > 1024 * 1024) {
        *error = QString::fromUtf8(u8"浪潮 INI 信息为空或超过 1 MiB。"); return false;
    }
    QTextCodec::ConverterState state;
    QString text = QTextCodec::codecForName("UTF-8")->toUnicode(ini.constData(), ini.size(), &state);
    if (state.invalidChars) text = QTextCodec::codecForName("GB18030")->toUnicode(ini);
    if (text.startsWith(QChar(0xfeff))) text.remove(0, 1);
    QString section; QMap<QString, QString> fields;
    const QStringList required{"dbinfo/dbname", "accinfo/dbname", "accinfo/accname", "accinfo/dbowner"};
    for (const QString& raw : text.split('\n')) {
        const QString line = raw.trimmed();
        if (line.startsWith(';') || line.isEmpty()) continue;
        if (line.startsWith('[') && line.endsWith(']')) { section = line.mid(1, line.size()-2).toLower(); continue; }
        const int equals = line.indexOf('=');
        if (equals <= 0) continue;
        const QString key = section + '/' + line.left(equals).trimmed().toLower();
        // Do not retain login passwords or any unrelated INI fields.
        if (!required.contains(key)) continue;
        if (fields.contains(key)) { *error = QString::fromUtf8(u8"浪潮 INI 存在重复账套字段。"); return false; }
        fields.insert(key, line.mid(equals+1).trimmed());
    }
    account->databaseName = fields.value("accinfo/dbname");
    account->accountName = fields.value("accinfo/accname");
    account->owner = fields.value("accinfo/dbowner");
    if (!validName(account->databaseName) || !validName(account->owner) || account->accountName.isEmpty()
        || account->accountName.size() > 255 || account->accountName.contains(QChar(0))
        || fields.value("dbinfo/dbname").compare(account->databaseName, Qt::CaseInsensitive) != 0) {
        *account = {}; *error = QString::fromUtf8(u8"浪潮 INI 缺少有效账套名称、数据库名或 DBOwner，或两处库名不一致。"); return false;
    }
    return true;
}

bool InspurBackupRestorer::unpack(const QString& source, const QString& directory, QString* error, const Cancel& cancel)
{
    error->clear();
    if (inspurCancelled(cancel)) { *error = QString::fromUtf8(u8"已取消解包。"); return false; }
    // Archive paths never become output paths or executable script text. Both selected
    // entries must belong to the same backup; XML is optional and is never executed.
    const QString script = QString::fromLatin1(R"PS(
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive=[IO.Compression.ZipFile]::OpenRead($env:LEMON_INSPUR_SOURCE)
try {
  if($archive.Entries.Count -gt 32){throw 'Too many MSS entries.'}
  foreach($entry in $archive.Entries){
    if($entry.FullName -match '(^[/\\]|(^|[/\\])\.\.([/\\]|$)|:)'){throw 'Unsafe MSS entry path.'}
  }
  $entries=@($archive.Entries | Where-Object { $_.Name -ne '' })
  $backups=@($entries | Where-Object { [IO.Path]::GetExtension($_.Name) -ieq '.bak' })
  $metadata=@($entries | Where-Object { [IO.Path]::GetExtension($_.Name) -ieq '.ini' })
  if($backups.Count -ne 1 -or $metadata.Count -ne 1){throw 'Expected exactly one BAK and one INI in MSS.'}
  if([IO.Path]::ChangeExtension($backups[0].FullName,'.ini') -ine $metadata[0].FullName){throw 'BAK and INI names do not match.'}
  foreach($entry in @($metadata[0],$backups[0])){
    $isIni=[IO.Path]::GetExtension($entry.Name) -ieq '.ini'
    $limit=if($isIni){1048576L}else{68719476736L}
    if($entry.Length -le 0 -or $entry.Length -gt $limit){throw 'MSS entry size is unsupported.'}
    $fileName=if($isIni){'account.ini'}else{'database.bak'}
    $entryStream=$entry.Open()
    try {
      $output=[IO.File]::Open([IO.Path]::Combine($env:LEMON_INSPUR_OUTPUT,$fileName),[IO.FileMode]::CreateNew)
      try {
        $buffer=New-Object byte[] 1048576
        $total=0L
        while(($count=$entryStream.Read($buffer,0,$buffer.Length)) -gt 0){
          $total+=$count
          if($total -gt $limit -or $total -gt $entry.Length){throw 'MSS expanded size mismatch.'}
          $output.Write($buffer,0,$count)
        }
        if($total -ne $entry.Length){throw 'MSS entry is truncated.'}
      } finally { $output.Dispose() }
    } finally { $entryStream.Dispose() }
  }
} finally { $archive.Dispose() }
)PS");
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("LEMON_INSPUR_SOURCE", source); environment.insert("LEMON_INSPUR_OUTPUT", directory);
    process.setProcessEnvironment(environment);
    process.start("powershell.exe", {"-NoLogo", "-NoProfile", "-NonInteractive", "-Command", script});
    if (!process.waitForStarted(10000)) { *error = process.errorString(); return false; }
    process.closeWriteChannel();
    while (!process.waitForFinished(100)) {
        if (inspurCancelled(cancel)) {
            process.kill(); process.waitForFinished(-1);
            *error = QString::fromUtf8(u8"已取消解包。"); return false;
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        *error = QString::fromUtf8(u8"浪潮 MSS 解包失败：%1").arg(QString::fromLocal8Bit(process.readAllStandardError())); return false;
    }
    return true;
}

QString InspurBackupRestorer::controlSchemaSql(const QString& controlDatabase)
{
    // Minimum schema consumed by InspurPsAccountSetManager::GetAccountSetList.
    // Existing vendor control tables are validated, never altered.
    return QStringLiteral("USE %1; SET XACT_ABORT ON; BEGIN TRY BEGIN TRANSACTION; "
        "IF OBJECT_ID(N'dbo.gsdbztxx') IS NULL CREATE TABLE dbo.gsdbztxx ("
        "gsdbztxx_base nvarchar(128) NOT NULL PRIMARY KEY, gsdbztxx_owner nvarchar(128) NOT NULL, "
        "gsdbztxx_ztmc nvarchar(255) NOT NULL); "
        "IF (SELECT COUNT(*) FROM sys.columns WHERE object_id=OBJECT_ID(N'dbo.gsdbztxx') "
        "AND name IN(N'gsdbztxx_base',N'gsdbztxx_owner',N'gsdbztxx_ztmc'))<>3 "
        "RAISERROR(N'Existing cwmaster table is missing required collection columns.',16,1); "
        "IF EXISTS(SELECT 1 FROM sys.columns WHERE object_id=OBJECT_ID(N'dbo.gsdbztxx') "
        "AND name NOT IN(N'gsdbztxx_base',N'gsdbztxx_owner',N'gsdbztxx_ztmc') AND is_nullable=0 AND is_identity=0 "
        "AND is_computed=0 AND default_object_id=0 AND system_type_id<>189) "
        "RAISERROR(N'Existing cwmaster table requires additional fields.',16,1); "
        "COMMIT TRANSACTION; END TRY BEGIN CATCH IF @@TRANCOUNT>0 ROLLBACK TRANSACTION; "
        "DECLARE @message nvarchar(2048); SET @message=ERROR_MESSAGE(); RAISERROR(N'%s',16,1,@message); END CATCH;")
        .arg(inspurIdentifier(controlDatabase));
}

QString InspurBackupRestorer::registrationSql(const T3RestoreSelection& selection, const InspurAccountMetadata& account,
    bool checkOnly, const QString& controlDatabase)
{
    if (!validateSelections({selection}).isEmpty() || !validName(account.owner)
        || account.accountName.trimmed().isEmpty() || account.accountName.size()>255
        || account.accountName.contains(QChar(0))) return {};
    return QStringLiteral("USE %1; SET XACT_ABORT ON; SET ANSI_WARNINGS ON; BEGIN TRY BEGIN TRANSACTION; "
        "IF EXISTS(SELECT 1 FROM dbo.gsdbztxx WITH(UPDLOCK,HOLDLOCK) WHERE gsdbztxx_base=%2) "
        "RAISERROR(N'Target Inspur database is already registered; choose another name.',16,1); "
        "INSERT INTO dbo.gsdbztxx(gsdbztxx_base,gsdbztxx_owner,gsdbztxx_ztmc) VALUES(%2,%3,%4); "
        "IF NOT EXISTS(SELECT 1 FROM dbo.gsdbztxx WHERE gsdbztxx_base=%2 AND gsdbztxx_owner=%3 AND gsdbztxx_ztmc=%4) "
        "RAISERROR(N'Inspur registration did not preserve metadata.',16,1); "
        "%5 TRANSACTION; END TRY BEGIN CATCH IF @@TRANCOUNT>0 ROLLBACK TRANSACTION; "
        "DECLARE @message nvarchar(2048); SET @message=ERROR_MESSAGE(); RAISERROR(N'%s',16,1,@message); END CATCH;")
        .arg(inspurIdentifier(controlDatabase),inspurLiteral(selection.targetDatabase),inspurLiteral(account.owner),
            inspurLiteral(account.accountName),checkOnly ? QStringLiteral("ROLLBACK") : QStringLiteral("COMMIT"));
}

static bool inspurReadAccount(QSqlDatabase& db, const QString& database, const InspurAccountMetadata& expected,
    InspurAccountMetadata* account, QString* error)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT s.name FROM %1.sys.tables t JOIN %1.sys.schemas s ON t.schema_id=s.schema_id "
        "WHERE t.name=N'LSCONF'").arg(inspurIdentifier(database)))) { *error=query.lastError().text(); return false; }
    QStringList owners; while (query.next()) owners.append(query.value(0).toString());
    if (query.lastError().isValid()) { *error=query.lastError().text(); return false; }
    // A bare BAK must identify a single account schema. MSS names its intended schema.
    const QString owner=expected.owner.isEmpty() && owners.size()==1 ? owners.first() : expected.owner;
    if (!validName(owner) || !owners.contains(owner)) {
        *error=QString::fromUtf8(u8"无法唯一确定 LSCONF 所属架构，或与 INI 的 DBOwner 不一致。"); return false;
    }
    if (!query.exec(QStringLiteral("SELECT F_VAL FROM %1.%2.LSCONF WHERE F_VKEY=N'CW_SYDWMC'")
        .arg(inspurIdentifier(database),inspurIdentifier(owner)))) { *error=query.lastError().text(); return false; }
    if (!query.next()) { *error=QString::fromUtf8(u8"LSCONF 缺少 CW_SYDWMC 单位名称。"); return false; }
    const QString company=query.value(0).toString().trimmed();
    if (query.next() || query.lastError().isValid() || company.isEmpty() || company.size()>255 || company.contains(QChar(0))) {
        *error=QString::fromUtf8(u8"LSCONF 单位名称为空、不唯一或格式无效。"); return false;
    }
    // MSS AccName is the account-list label; CW_SYDWMC is the company name used by
    // InitAccountSet. These may legitimately differ. Preserve each original value.
    *account={database,expected.accountName.isEmpty() ? company : expected.accountName,owner};
    return true;
}
T3RestoreResult InspurBackupRestorer::restore(const T3SqlConnection& options, const InspurBackupAnalysis& analysis,
    const QVector<T3RestoreSelection>& selections, const QString& dataDirectory, const QString& logDirectory,
    const Cancel& cancel, const Progress& progress, const QString& controlDatabase)
{
    T3RestoreResult result;
    const auto perform = [&]() {
        result.error=validateSelections(selections); if (!result.error.isEmpty()) return;
        if (!analysis.success || analysis.preparedPath.isEmpty() || dataDirectory.trimmed().isEmpty() || logDirectory.trimmed().isEmpty())
        { result.error=QString::fromUtf8(u8"请先读取备份并填写服务器数据和日志目录。"); return; }
        if (controlDatabase!="cwmaster" && !QRegularExpression("^InspurTestControl_[a-f0-9]{32}$").match(controlDatabase).hasMatch())
        { result.error=QString::fromUtf8(u8"控制库名称无效。"); return; }
        if (inspurCancelled(cancel)) { result.stopped=true; return; }
        InspurSqlSession session; if (!session.open(options,&result.error)) return;
        auto current=inspurReadSets(session.db,analysis.preparedPath,&result.error);
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
            if (!query.exec("SELECT DB_ID("+inspurLiteral(item.targetDatabase)+")") || !query.next())
            { result.error=query.lastError().text(); return; }
            if (!query.value(0).isNull()) { result.error=QString::fromUtf8(u8"目标数据库 %1 已存在，拒绝覆盖；请修改目标库名。").arg(item.targetDatabase); return; }
        }
        QVector<QString> commands;
        for (const auto& item:selections) {
            if (inspurCancelled(cancel)) { result.stopped=true; return; }
            QVector<T3RestoreFile> files;
            if (!inspurFiles(session.db,analysis.preparedPath,item.backup.position,&files,&result.error)) return;
            const QString command=T3BackupRestorer::restoreSql(analysis.preparedPath,item,files,dataDirectory,logDirectory,QUuid::createUuid().toString(QUuid::Id128));
            const QString prefix="RESTORE DATABASE "+inspurIdentifier(item.targetDatabase)+" FROM DISK=";
            const QString suffix=", RECOVERY;";
            const int start=command.indexOf(prefix);
            if (start<0 || !command.endsWith(suffix)) { result.error=QString::fromUtf8(u8"数据库文件类型或目标目录无效。"); return; }
            QString verify=command.mid(start); verify.replace(0,prefix.size(),"RESTORE VERIFYONLY FROM DISK=");
            verify.chop(suffix.size()); verify+=';';
            inspurReport(progress,QString::fromUtf8(u8"校验 %1...").arg(item.backup.databaseName));
            if (!inspurExecute(session.db,verify,&result.error)) return;
            commands.append(command);
        }
        if (inspurCancelled(cancel)) { result.stopped=true; return; }
        if (!inspurExecute(session.db,T3BackupRestorer::controlDatabaseSql(dataDirectory,logDirectory,
            QUuid::createUuid().toString(QUuid::Id128),controlDatabase),&result.error)
            || !inspurExecute(session.db,controlSchemaSql(controlDatabase),&result.error)) {
            result.error=QString::fromUtf8(u8"cwmaster 控制表不兼容，尚未恢复目标库：%1").arg(result.error); return;
        }
        for (const auto& item:selections)
            if (!inspurExecute(session.db,registrationSql(item,analysis.account.accountName.isEmpty() ? InspurAccountMetadata{item.backup.databaseName,item.targetDatabase,QStringLiteral("dbo")} : analysis.account,true,controlDatabase),&result.error)) {
                result.error=QString::fromUtf8(u8"控制库登记预检失败，尚未恢复目标库：%1").arg(result.error); return;
            }
        if (!inspurExecute(session.db,"USE master;",&result.error)) return;
        for (int i=0;i<selections.size();++i) {
            if (inspurCancelled(cancel)) { result.stopped=true; return; }
            T3RestoreItem item; item.database=selections[i].targetDatabase;
            inspurReport(progress,QString::fromUtf8(u8"正在恢复 %1（%2/%3）...").arg(item.database).arg(i+1).arg(selections.size()));
            item.databaseRestored=inspurExecute(session.db,commands[i],&item.error);
            if (item.databaseRestored) {
                QSqlQuery state(session.db);
                item.databaseRestored=state.exec("SELECT state_desc FROM sys.databases WHERE name="+inspurLiteral(item.database))
                    && state.next() && state.value(0).toString()=="ONLINE";
                if (!item.databaseRestored) item.error=QString::fromUtf8(u8"尚未确认数据库 ONLINE，请检查服务器状态。");
            }
            if (item.databaseRestored) {
                InspurAccountMetadata account;
                item.success=inspurReadAccount(session.db,item.database,analysis.account,&account,&item.error)
                    && inspurExecute(session.db,registrationSql(selections[i],account,false,controlDatabase),&item.error);
                if (item.success) inspurReport(progress,QString::fromUtf8(u8"恢复及采集登记成功：%1 → %2").arg(account.accountName,item.database));
                else item.error=QString::fromUtf8(u8"数据库 %1 已恢复并保留，但采集登记失败：%2").arg(item.database,item.error);
            }
            item.error=inspurRedact(item.error,options); result.items.append(item);
            if (!item.success) { result.error=item.error; return; }
            if (!inspurExecute(session.db,"USE master;",&result.error)) return;
        }
    };
    perform(); result.error=inspurRedact(result.error,options); return result;
}
