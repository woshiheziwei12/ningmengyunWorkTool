#include "SqlServerCredentials.h"
#include "T3BackupRestorer.h"
#include "SqlBackupCompatibility.h"
#include "RemoteFileTransfer.h"
#include "../ThirdParty/zlib/zlib.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryFile>
#include <QUuid>
#include <QVariant>
#include <array>
#include <cstring>
#include <windows.h>
#include <winnetwk.h>
#pragma comment(lib, "Mpr.lib")

static QString t3Redact(QString message, const T3SqlConnection& options)
{
    if (!options.password.isEmpty()) message.replace(options.password, QStringLiteral("***"));
    return message;
}
static QString t3OdbcValue(QString value)
{
    value.replace('}', "}}");
    return '{' + value + '}';
}
static bool t3ConnectConfiguredShare(const T3SqlConnection& options,const QString& directory,QString* error)
{
    // Never disconnect another task's SMB session or send credentials to a custom host/share.
    const QString path=QDir::fromNativeSeparators(directory);
    const QString root=QStringLiteral("//192.168.1.211/hzw");
    if(options.server!=QStringLiteral("192.168.1.211")
        || (path.compare(root,Qt::CaseInsensitive)!=0 && !path.startsWith(root+'/',Qt::CaseInsensitive)))return true;
    if(QDir(directory).exists())return true;
    QString remote=QStringLiteral("\\\\192.168.1.211\\hzw");
    NETRESOURCEW resource={};resource.dwType=RESOURCETYPE_DISK;
    resource.lpRemoteName=reinterpret_cast<LPWSTR>(remote.data());
    const auto credentials = SqlServerCredentials::share211();
    if (credentials.first.isEmpty() || credentials.second.isEmpty()) {
        *error = QString::fromUtf8(u8"211 共享凭据未配置，请配置 SqlServer211.local.json 或先在 Windows 中连接共享。");
        return false;
    }
    const DWORD status=WNetAddConnection2W(&resource,reinterpret_cast<LPCWSTR>(credentials.second.utf16()),reinterpret_cast<LPCWSTR>(credentials.first.utf16()),0);
    if(status==NO_ERROR)return true;
    *error=status==ERROR_SESSION_CREDENTIAL_CONFLICT
        ?QString::fromUtf8(u8"211 共享已有其他账号的连接，请在 Windows 中处理该连接后重试；工具未断开现有会话。")
        :QString::fromUtf8(u8"使用本机配置账号连接 211 共享失败（Windows 错误 %1）。").arg(status);
    return false;
}
static bool t3Cancelled(const T3BackupRestorer::Cancel& cancel) { return cancel && cancel(); }
bool T3BackupRestorer::connectConfiguredShare(const T3SqlConnection& options, const QString& directory, QString* error)
{ return t3ConnectConfiguredShare(options, directory, error); }
static void t3Report(const T3BackupRestorer::Progress& progress, const QString& message)
{
    if (progress) progress(message);
}
struct T3SqlSession
{
    QString name = QUuid::createUuid().toString();
    QSqlDatabase db;
    T3SqlSession() : db(QSqlDatabase::addDatabase(QStringLiteral("QODBC"), name)) {}
    ~T3SqlSession()
    {
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
    bool open(const T3SqlConnection& options, QString* error)
    {
        if (options.server.trimmed().isEmpty() || options.server.contains(QChar(0))
            || options.user.contains(QChar(0)) || options.password.contains(QChar(0)))
        { *error = QString::fromUtf8(u8"服务器、账号或密码格式无效。"); return false; }
        if (options.windowsAuthentication && !T3BackupRestorer::isLocalServer(options.server))
        { *error = QString::fromUtf8(u8"Windows 身份验证模式请填写本机实例名；远程服务器请选择账号密码模式。"); return false; }
        if (!options.windowsAuthentication && options.user.trimmed().isEmpty())
        { *error = QString::fromUtf8(u8"请填写 SQL Server 账号。"); return false; }
        if (!options.windowsAuthentication && options.password.isEmpty())
        { *error = QString::fromUtf8(u8"SQL Server 密码未配置，请检查本机 SqlServer211.local.json 或环境变量。"); return false; }
        if (!T3BackupRestorer::availableDrivers().contains(options.driver))
        { *error = QString::fromUtf8(u8"未安装所选的 32 位 SQL Server ODBC 驱动，请安装 ODBC Driver 17 或 18。"); return false; }
        db.setConnectOptions(QStringLiteral("SQL_ATTR_LOGIN_TIMEOUT=10;SQL_ATTR_CONNECTION_TIMEOUT=15"));
        db.setDatabaseName(T3BackupRestorer::connectionString(options));
        if (db.open()) return true;
        *error = t3Redact(db.lastError().text(), options);
        return false;
    }
};
static bool t3Execute(QSqlDatabase& db, const QString& sql, QString* error)
{
    QSqlQuery query(db);
    if (!query.exec(sql)) { *error = query.lastError().text(); return false; }
    // Drain all result sets: RESTORE can return informational messages and errors late.
    do { while (query.next()) {} } while (query.nextResult());
    if (query.lastError().isValid()) { *error = query.lastError().text(); return false; }
    return true;
}
static QString t3ParentPath(const QString& path)
{
    const int split = qMax(path.lastIndexOf('/'),path.lastIndexOf('\\'));
    return split >= 0 ? path.left(split + 1) : QString();
}
static QString t3JoinServerPath(QString directory, const QString& name)
{
    if (!directory.endsWith('/') && !directory.endsWith('\\'))
        directory += directory.contains('\\') || directory.contains(':') ? '\\' : '/';
    return directory + name;
}
// SQL Server 2008 treats //host/share as a relative device path. Qt file APIs
// accept forward slashes, but every SQL backup device must use native UNC syntax.
static QString t3DiskLiteral(const QString& path)
{
    return T3BackupRestorer::quoteLiteral(QDir::toNativeSeparators(path));
}
static bool t3ServerInfo(QSqlDatabase& db, T3ServerInfo* server, QString* error)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT CONVERT(nvarchar(128),@@SERVERNAME),"
        "CONVERT(nvarchar(128),SERVERPROPERTY('ProductVersion')),"
        "CONVERT(nvarchar(4000),SERVERPROPERTY('InstanceDefaultBackupPath')),"
        "CONVERT(nvarchar(4000),SERVERPROPERTY('InstanceDefaultDataPath')),"
        "CONVERT(nvarchar(4000),SERVERPROPERTY('InstanceDefaultLogPath')),"
        "CONVERT(int,DATABASEPROPERTYEX(N'master',N'Version'))")) || !query.next())
    { *error = query.lastError().text(); return false; }
    server->name=query.value(0).toString();server->version=query.value(1).toString();
    server->backupDirectory=query.value(2).toString();server->dataDirectory=query.value(3).toString();
    server->logDirectory=query.value(4).toString();server->databaseVersion=query.value(5).toInt();query.finish();
    if (server->dataDirectory.isEmpty() || server->logDirectory.isEmpty())
    {
        if (!query.exec(QStringLiteral("SELECT type,physical_name FROM sys.master_files WHERE database_id=1")))
        { *error=query.lastError().text();return false; }
        while(query.next())
        {
            if(query.value(0).toInt()==0 && server->dataDirectory.isEmpty())server->dataDirectory=t3ParentPath(query.value(1).toString());
            if(query.value(0).toInt()==1 && server->logDirectory.isEmpty())server->logDirectory=t3ParentPath(query.value(1).toString());
        }
    }
    if(server->backupDirectory.isEmpty())server->backupDirectory=server->dataDirectory;
    return true;
}

QStringList T3BackupRestorer::availableDrivers()
{
    QSettings registry(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\ODBC\\ODBCINST.INI\\ODBC Drivers"),QSettings::NativeFormat);
    QStringList drivers;
    for(const auto& name : {QStringLiteral("ODBC Driver 18 for SQL Server"),QStringLiteral("ODBC Driver 17 for SQL Server")})
        if(registry.value(name).toString().compare(QStringLiteral("Installed"),Qt::CaseInsensitive)==0)drivers.append(name);
    return drivers;
}
QString T3BackupRestorer::connectionString(const T3SqlConnection& options)
{
    QString result=QStringLiteral("DRIVER=%1;SERVER=%2;DATABASE={master};Encrypt=Yes;TrustServerCertificate=%3;")
        .arg(t3OdbcValue(options.driver),t3OdbcValue(options.server.trimmed()),options.trustServerCertificate?QStringLiteral("Yes"):QStringLiteral("No"));
    if(options.windowsAuthentication)return result+QStringLiteral("Trusted_Connection=Yes;");
    return result+QStringLiteral("Trusted_Connection=No;UID=%1;PWD=%2;").arg(t3OdbcValue(options.user),t3OdbcValue(options.password));
}
bool T3BackupRestorer::isLocalServer(const QString& server)
{
    QString host=server.trimmed();
    if(host.startsWith("lpc:",Qt::CaseInsensitive)||host.startsWith("tcp:",Qt::CaseInsensitive))host=host.mid(4);
    host=host.section('\\',0,0).section(',',0,0);
    return host==QStringLiteral(".")||host.compare("(local)",Qt::CaseInsensitive)==0
        ||host.compare("localhost",Qt::CaseInsensitive)==0||host==QStringLiteral("127.0.0.1")
        ||host==QStringLiteral("::1")||host.compare(QHostInfo::localHostName(),Qt::CaseInsensitive)==0;
}
QString T3BackupRestorer::resolveSource(const QString& path, QString* error)
{
    if(error)error->clear();
    QFileInfo info(path);
    if(info.isDir() || info.fileName().compare(QStringLiteral("UF2KAct.Lst"),Qt::CaseInsensitive)==0)
    {
        QDir directory=info.isDir()?QDir(info.absoluteFilePath()):info.dir();
        const auto files=directory.entryInfoList(QDir::Files);
        for(const auto& entry:files)
            if(entry.fileName().compare(QStringLiteral("UFDATA.BA_"),Qt::CaseInsensitive)==0)return entry.absoluteFilePath();
        for(const auto& entry:files)
            if(entry.fileName().compare(QStringLiteral("UFDATA.BAK"),Qt::CaseInsensitive)==0)return entry.absoluteFilePath();
        if(error)*error=QString::fromUtf8(u8"此目录中未找到 UFDATA.BA_ 或 UFDATA.BAK。");return {};
    }
    if(!info.isFile()){if(error)*error=QString::fromUtf8(u8"备份文件不存在。");return {};}
    return info.absoluteFilePath();
}
QString T3BackupRestorer::prepareFile(const QString& source,const QString& directory,QString* error,
    const Cancel& cancel,quint64 outputLimit)
{
    error->clear();
    const QString resolved=resolveSource(source,error);if(resolved.isEmpty())return {};
    QFile input(resolved);
    if(!input.open(QIODevice::ReadOnly)){*error=input.errorString();return {};}
    const QByteArray magic=input.peek(4);
    const bool gzip=magic.startsWith(QByteArray::fromHex("1f8b08"));
    if(!gzip && magic!=QByteArray("TAPE",4)){*error=QString::fromUtf8(u8"不支持此备份格式：请选择 T3 的 GZIP 备份包或标准 SQL Server .bak 文件。");return {};}
    if(directory.trimmed().isEmpty() || !QDir(directory).exists())
    { *error=QString::fromUtf8(u8"备份暂存目录不存在或当前 Windows 用户无法访问。");return {}; }
    QTemporaryFile output(QDir(directory).filePath(QStringLiteral("T3Restore_XXXXXX.bak")));
    if(!output.open()){*error=QString::fromUtf8(u8"无法写入备份暂存目录：%1").arg(output.errorString());return {};}
    struct Inflater { z_stream stream={}; bool active=false; ~Inflater(){if(active)inflateEnd(&stream);} } inflater;
    if(gzip)
    {
        if(inflateInit2(&inflater.stream,31)!=Z_OK){*error=QString::fromUtf8(u8"无法初始化 GZIP 解压。");return {};}
        inflater.active=true;
    }
    std::array<char,65536> buffer={};quint64 total=0;QByteArray signature;bool finished=!gzip;
    while(!input.atEnd())
    {
        if(t3Cancelled(cancel)){*error=QString::fromUtf8(u8"已取消。");return {};}
        QByteArray data=input.read(65536);
        if(data.isEmpty()){*error=QString::fromUtf8(u8"读取备份失败：%1").arg(input.errorString());return {};}
        inflater.stream.next_in=reinterpret_cast<Bytef*>(data.data());inflater.stream.avail_in=static_cast<uInt>(data.size());
        do
        {
            if(t3Cancelled(cancel)){*error=QString::fromUtf8(u8"已取消。");return {};}
            const char* bytes=data.constData();int size=data.size();int status=Z_OK;
            if(gzip)
            {
                inflater.stream.next_out=reinterpret_cast<Bytef*>(buffer.data());inflater.stream.avail_out=static_cast<uInt>(buffer.size());
                status=inflate(&inflater.stream,Z_NO_FLUSH);
                if(status!=Z_OK && status!=Z_STREAM_END && status!=Z_BUF_ERROR)
                { *error=QString::fromUtf8(u8"GZIP 数据损坏、校验失败或格式不受支持。");return {}; }
                bytes=buffer.data();size=static_cast<int>(buffer.size()-inflater.stream.avail_out);
            }
            if(quint64(size)>outputLimit-total){*error=QString::fromUtf8(u8"解包后文件超过大小上限。");return {};}
            if(signature.size()<4)signature.append(bytes,qMin(size,4-signature.size()));
            if(signature.size()==4 && signature!="TAPE"){*error=QString::fromUtf8(u8"解包内容不是 SQL Server 备份。");return {};}
            if(output.write(bytes,size)!=size){*error=QString::fromUtf8(u8"写入暂存文件失败：%1").arg(output.errorString());return {};}
            total+=quint64(size);
            if(!gzip)break;
            if(status==Z_STREAM_END)
            {
                if(inflater.stream.avail_in || !input.atEnd()){*error=QString::fromUtf8(u8"备份尾部包含额外数据，暂不支持分卷或拼接压缩包。");return {};}
                finished=true;break;
            }
            if(status==Z_BUF_ERROR && inflater.stream.avail_in){*error=QString::fromUtf8(u8"解压没有进展，备份数据异常。");return {};}
        }while(inflater.stream.avail_in || !inflater.stream.avail_out);
    }
    if(!finished || signature!="TAPE" || input.error()!=QFile::NoError)
    { *error=QString::fromUtf8(u8"备份被截断或未完整读取。");return {}; }
    if(!output.flush()){*error=output.errorString();return {};}
    if(t3Cancelled(cancel)){*error=QString::fromUtf8(u8"已取消。");return {};}
    const QString path=output.fileName();output.close();output.setAutoRemove(false);return path;
}
bool T3BackupRestorer::testConnection(const T3SqlConnection& options,T3ServerInfo* server,QString* error)
{
    error->clear();T3SqlSession session;
    if(!session.open(options,error))return false;
    const bool ok=t3ServerInfo(session.db,server,error);*error=t3Redact(*error,options);return ok;
}
bool T3BackupRestorer::preparedFileExists(const T3SqlConnection& options,const QString& path,QString* error)
{
    error->clear();T3SqlSession session;
    if(!session.open(options,error))return false;
    QSqlQuery query(session.db);
    // Extended procedures do not support the scrollable cursor requested by QODBC by default.
    query.setForwardOnly(true);
    if(!query.exec(QStringLiteral("EXEC master.dbo.xp_fileexist %1").arg(t3DiskLiteral(path))))
    { *error=t3Redact(query.lastError().text(),options);return false; }
    if(!query.next() || query.value(0).toInt()!=1)
    { *error=QString::fromUtf8(u8"目标实例无法访问已暂存文件：%1。请检查共享目录和 SQL Server 服务账号权限。").arg(path);return false; }
    return true;
}
static QVector<T3BackupSet> t3ReadSets(QSqlDatabase& db,const QString& path,QString* error)
{
    QVector<T3BackupSet> sets;QSqlQuery query(db);
    if(!query.exec(QStringLiteral("RESTORE HEADERONLY FROM DISK=%1").arg(t3DiskLiteral(path))))
    { *error=query.lastError().text();return sets; }
    const QSqlRecord record=query.record();
    const auto value=[&](const char* name){return query.value(record.indexOf(QLatin1String(name)));};
    while(query.next())
    {
        if(value("BackupType").toInt()!=1)continue;
        T3BackupSet set;set.position=value("Position").toInt();set.databaseName=value("DatabaseName").toString();
        set.databaseVersion=value("DatabaseVersion").toInt();set.backupSetId=value("BackupSetGUID").toString();
        set.backupDate=value("BackupFinishDate").toString();
        set.softwareVersionMajor=value("SoftwareVersionMajor").toInt();
        set.softwareVersionMinor=value("SoftwareVersionMinor").toInt();
        if(set.position>0 && !set.databaseName.isEmpty())sets.append(set);
    }
    if(query.lastError().isValid())*error=query.lastError().text();
    if(sets.isEmpty() && error->isEmpty())*error=QString::fromUtf8(u8"备份中没有可恢复的完整数据库备份集。");
    return sets;
}
T3BackupAnalysis T3BackupRestorer::analyze(const T3SqlConnection& options,const QString& source,
    const QString& stagingDirectory,const Cancel& cancel,const Progress& progress,bool useRemoteAgent)
{
    T3BackupAnalysis result;T3SqlSession session;
    if(!session.open(options,&result.error))return result;
    if(!t3ServerInfo(session.db,&result.server,&result.error))return result;
    QString staging=stagingDirectory.trimmed();
    if(isLocalServer(options.server))
    { if(staging.isEmpty())staging=result.server.backupDirectory; }
    else if(!QDir::fromNativeSeparators(staging).startsWith(QStringLiteral("//")))
    {result.error=QString::fromUtf8(u8"远程恢复请填写 UNC 备份暂存目录（例如 \\\\服务器\\共享目录），该目录需允许当前 Windows 用户写入、SQL Server 服务账号读取。");return result;}
    if(!t3ConnectConfiguredShare(options,staging,&result.error))return result;
    t3Report(progress,QString::fromUtf8(u8"正在准备 SQL Server 可读取的备份文件..."));
    if(useRemoteAgent)
    {
        const QString resolved=resolveSource(source,&result.error);if(resolved.isEmpty())return result;
        QFile input(resolved);if(!input.open(QIODevice::ReadOnly)){result.error=input.errorString();return result;}
        const auto magic=input.peek(4);input.close();
        if(magic!="TAPE"&&!magic.startsWith(QByteArray::fromHex("1f8b08")))
        {result.error=QString::fromUtf8(u8"请选择 GZIP T3 备份或标准 SQL Server .bak。");return result;}
        const auto transferred=RemoteFileTransfer::upload(resolved,staging,
            magic=="TAPE"?RemoteFileTransfer::InputEncoding::RawFile:RemoteFileTransfer::InputEncoding::Gzip,cancel,progress);
        result.error=transferred.error;if(!result.error.isEmpty())return result;
        result.preparedPath=transferred.serverPath;result.stagingFile=transferred.job;
    }
    else
    {
        result.preparedPath=prepareFile(source,staging,&result.error,cancel);
        if(result.preparedPath.isEmpty())return result;
        result.stagingFile=std::shared_ptr<QString>(new QString(result.preparedPath),[](QString* path){QFile::remove(*path);delete path;});
    }
    t3Report(progress,QString::fromUtf8(u8"读取备份中的年度数据库..."));
    result.sets=t3ReadSets(session.db,result.preparedPath,&result.error);
    if(result.error.isEmpty())result.error=SqlBackupCompatibility::error(result.sets,result.server);
    result.success=result.error.isEmpty() && !t3Cancelled(cancel);
    if(!result.success)
    {
        if(result.error.isEmpty())result.error=QString::fromUtf8(u8"已取消。");
        result.error=t3Redact(result.error,options);
        result.stagingFile.reset();result.preparedPath.clear();
    }
    return result;
}
QString T3BackupRestorer::quoteLiteral(const QString& value)
{ QString escaped=value;escaped.replace('\'',"''");return "N'"+escaped+'\''; }
QString T3BackupRestorer::quoteIdentifier(const QString& value)
{ QString escaped=value;escaped.replace(']',"]]");return '['+escaped+']'; }
QString T3BackupRestorer::validateSelections(const QVector<T3RestoreSelection>& selections)
{
    if(selections.isEmpty())return QString::fromUtf8(u8"请至少选择一个年度数据库。");
    QSet<QString> names;QSet<int> positions;
    for(const auto& item:selections)
    {
        const QString name=item.targetDatabase;
        if(name.trimmed().isEmpty() || name!=name.trimmed() || name.size()>128 || name.contains(QChar(0))
            || name.contains('\r') || name.contains('\n'))return QString::fromUtf8(u8"目标数据库名为空、过长或包含无效字符。");
        const QString key=name.toCaseFolded();
        if(QStringList{"master","model","msdb","tempdb"}.contains(key))return QString::fromUtf8(u8"不能恢复到 SQL Server 系统数据库。");
        if(names.contains(key) || positions.contains(item.backup.position))return QString::fromUtf8(u8"不能重复选择备份集或使用重复的目标数据库名。");
        if(item.backup.position<=0)return QString::fromUtf8(u8"备份集编号无效。");
        names.insert(key);positions.insert(item.backup.position);
    }
    return {};
}
static QRegularExpressionMatch t3DatabaseParts(const QString& name)
{
    return QRegularExpression(QStringLiteral("^UFDATA_([0-9]{3})_([1-9][0-9]{3})$"),
        QRegularExpression::CaseInsensitiveOption).match(name);
}
QString T3BackupRestorer::validateCollectionSelections(const QVector<T3RestoreSelection>& selections)
{
    const QString error=validateSelections(selections);if(!error.isEmpty())return error;
    for(const auto& item:selections)
    {
        const auto target=t3DatabaseParts(item.targetDatabase);
        const auto source=t3DatabaseParts(item.backup.databaseName);
        if(!target.hasMatch() || !source.hasMatch() || target.captured(2)!=source.captured(2))
            return QString::fromUtf8(u8"采集需要 UFDATA_三位账套号_四位年度 格式的库名，且目标年度必须与备份一致；重名时请修改三位账套号。");
    }
    return {};
}
QString T3BackupRestorer::controlDatabaseSql(const QString& dataDirectory,const QString& logDirectory,
    const QString& fileToken,const QString& controlDatabase)
{
    if(dataDirectory.trimmed().isEmpty() || logDirectory.trimmed().isEmpty()
        || !QRegularExpression(QStringLiteral("^[a-zA-Z0-9_]+$")).match(fileToken).hasMatch())return {};
    return QStringLiteral("IF DB_ID(%1) IS NULL BEGIN CREATE DATABASE %2 ON PRIMARY "
        "(NAME=N'T3ControlData',FILENAME=%3) LOG ON (NAME=N'T3ControlLog',FILENAME=%4); END;")
        .arg(quoteLiteral(controlDatabase),quoteIdentifier(controlDatabase),
            quoteLiteral(t3JoinServerPath(dataDirectory,"T3_Control_"+fileToken+".mdf")),
            quoteLiteral(t3JoinServerPath(logDirectory,"T3_Control_"+fileToken+".ldf")));
}
QString T3BackupRestorer::controlSchemaSql(const QString& controlDatabase)
{
    // Collection-only schema, not a T3 installation. Never alter an existing table.
    return QStringLiteral("USE %1; SET XACT_ABORT ON; "
        "IF OBJECT_ID(N'dbo.UA_Account') IS NULL "
        "CREATE TABLE dbo.UA_Account (cAcc_Id varchar(3) NOT NULL PRIMARY KEY, "
        "cAcc_Name nvarchar(120) NOT NULL, iYear int NOT NULL, cTradeKind nvarchar(120) NOT NULL); "
        "SELECT TOP (0) cAcc_Id,cAcc_Name,iYear,cTradeKind FROM dbo.UA_Account; "
        "IF EXISTS (SELECT 1 FROM sys.columns WHERE object_id=OBJECT_ID(N'dbo.UA_Account') "
        "AND name NOT IN (N'cAcc_Id',N'cAcc_Name',N'iYear',N'cTradeKind') "
        "AND is_nullable=0 AND is_identity=0 AND is_computed=0 AND default_object_id=0 AND system_type_id<>189) "
        "RAISERROR(N'Existing UA_Account requires additional fields; cannot register collection account.',16,1);")
        .arg(quoteIdentifier(controlDatabase));
}
QString T3BackupRestorer::registerAccountSql(const T3RestoreSelection& selection,bool checkOnly,
    const QString& controlDatabase)
{
    if(!validateCollectionSelections({selection}).isEmpty() || selection.accountName.trimmed().isEmpty()
        || selection.tradeKind.trimmed().isEmpty() || selection.accountName.size()>120 || selection.tradeKind.size()>120
        || selection.accountName.contains(QChar(0)) || selection.tradeKind.contains(QChar(0)))return {};
    const auto parts=t3DatabaseParts(selection.targetDatabase);
    // Lock the account through registration; preflight exercises constraints then rolls back.
    return QStringLiteral("USE %1; SET XACT_ABORT ON; BEGIN TRY BEGIN TRANSACTION; "
        "IF EXISTS (SELECT 1 FROM dbo.UA_Account WITH (UPDLOCK,HOLDLOCK) WHERE cAcc_Id=%2 "
        "AND (cAcc_Name IS NULL OR cTradeKind IS NULL OR iYear IS NULL "
        "OR cAcc_Name<>%3 OR cTradeKind<>%4)) "
        "RAISERROR(N'Account ID already has different metadata; use another account ID.',16,1); "
        "IF (SELECT COUNT(*) FROM dbo.UA_Account WHERE cAcc_Id=%2)>1 "
        "RAISERROR(N'Duplicate account ID in UA_Account.',16,1); "
        "IF EXISTS (SELECT 1 FROM dbo.UA_Account WHERE cAcc_Id=%2) "
        "UPDATE dbo.UA_Account SET iYear=CASE WHEN iYear>%5 THEN %5 ELSE iYear END WHERE cAcc_Id=%2; "
        "ELSE INSERT INTO dbo.UA_Account(cAcc_Id,cAcc_Name,iYear,cTradeKind) VALUES(%2,%3,%5,%4); "
        "%6 TRANSACTION; END TRY BEGIN CATCH "
        "IF @@TRANCOUNT>0 ROLLBACK TRANSACTION; DECLARE @message nvarchar(2048); "
        "SET @message=ERROR_MESSAGE(); RAISERROR(N'%s',16,1,@message); END CATCH;")
        .arg(quoteIdentifier(controlDatabase),quoteLiteral(parts.captured(1)),
            quoteLiteral(selection.accountName.trimmed()),quoteLiteral(selection.tradeKind),
            parts.captured(2),checkOnly?QStringLiteral("ROLLBACK"):QStringLiteral("COMMIT"));
}
QString T3BackupRestorer::accountMetadataSql(const T3RestoreSelection& selection,bool extended)
{
    if(!validateSelections({selection}).isEmpty() || !t3DatabaseParts(selection.backup.databaseName).hasMatch())return {};
    // Account backups keep metadata in UA_Account_Ex in the first annual set;
    // standalone backups may instead contain a populated UA_Account.
    return QStringLiteral("SELECT cAcc_Name,cTradeKind FROM %1.dbo.%2 WHERE cAcc_Id=%3")
        .arg(quoteIdentifier(selection.targetDatabase),extended?QStringLiteral("UA_Account_Ex"):QStringLiteral("UA_Account"),
            quoteLiteral(t3DatabaseParts(selection.backup.databaseName).captured(1)));
}
static bool t3ReadAccount(QSqlDatabase& db,T3RestoreSelection* selection,QString* error)
{
    for(bool extended:{true,false})
    {
        const QString table=extended?QStringLiteral("UA_Account_Ex"):QStringLiteral("UA_Account");
        QSqlQuery query(db);
        if(!query.exec(QStringLiteral("SELECT OBJECT_ID(%1)").arg(T3BackupRestorer::quoteLiteral(selection->targetDatabase+".dbo."+table)))||!query.next())
        {*error=query.lastError().text();return false;}
        const bool exists=!query.value(0).isNull();query.finish();if(!exists)continue;
        if(!query.exec(T3BackupRestorer::accountMetadataSql(*selection,extended)))
        {*error=query.lastError().text();return false;}
        if(!query.next())
        {if(query.lastError().isValid()){*error=query.lastError().text();return false;}continue;}
        selection->accountName=query.value(0).toString().trimmed();selection->tradeKind=query.value(1).toString().trimmed();
        if(query.next() || query.lastError().isValid())
        {*error=QString::fromUtf8(u8"备份中的账套信息不唯一或读取失败。");return false;}
        if(!selection->accountName.isEmpty() && !selection->tradeKind.isEmpty()
            && selection->accountName.size()<=120 && selection->tradeKind.size()<=120
            && !selection->accountName.contains(QChar(0)) && !selection->tradeKind.contains(QChar(0)))return true;
    }
    *error=QString::fromUtf8(u8"备份的 UA_Account_Ex / UA_Account 中未找到完整的账套名称和行业性质。");return false;
}
QString T3BackupRestorer::restoreSql(const QString& backupPath,const T3RestoreSelection& selection,
    const QVector<T3RestoreFile>& files,const QString& dataDirectory,const QString& logDirectory,const QString& fileToken)
{
    if(!validateSelections({selection}).isEmpty() || files.isEmpty() || dataDirectory.trimmed().isEmpty()
        ||logDirectory.trimmed().isEmpty() || !QRegularExpression(QStringLiteral("^[a-zA-Z0-9_]+$")).match(fileToken).hasMatch())return {};
    QStringList moves;QSet<int> ids;
    for(const auto& file:files)
    {
        if((file.type!="D"&&file.type!="L")||file.logicalName.isEmpty()||file.fileId<=0||ids.contains(file.fileId))return {};
        ids.insert(file.fileId);
        const QString name=QStringLiteral("T3_%1_%2%3").arg(fileToken).arg(file.fileId).arg(file.type=="L"?".ldf":(file.fileId==1?".mdf":".ndf"));
        moves.append(QStringLiteral("MOVE %1 TO %2").arg(quoteLiteral(file.logicalName),
            quoteLiteral(t3JoinServerPath(file.type=="L"?logDirectory:dataDirectory,name))));
    }
    return QString::fromUtf8(u8"IF DB_ID(%1) IS NOT NULL BEGIN RAISERROR(N'目标数据库已存在，拒绝覆盖。',16,1); RETURN; END; "
        "RESTORE DATABASE %2 FROM DISK=%3 WITH FILE=%4, %5, RECOVERY;")
        .arg(quoteLiteral(selection.targetDatabase),quoteIdentifier(selection.targetDatabase),t3DiskLiteral(backupPath))
        .arg(selection.backup.position).arg(moves.join(QStringLiteral(", ")));
}
static bool t3LoadBackupAccount(QSqlDatabase& db,const QString& backupPath,const T3BackupSet& firstSet,
    const QString& dataDirectory,const QString& logDirectory,T3RestoreSelection* account,QString* error,
    const T3BackupRestorer::Progress& progress)
{
    const QString token=QUuid::createUuid().toString(QUuid::Id128);
    account->backup=firstSet;account->targetDatabase="T3Metadata_"+token;
    QSqlQuery query(db);
    if(!query.exec(QStringLiteral("RESTORE FILELISTONLY FROM DISK=%1 WITH FILE=%2").arg(t3DiskLiteral(backupPath)).arg(firstSet.position)))
    {*error=query.lastError().text();return false;}
    const auto columns=query.record();QVector<T3RestoreFile> files;
    while(query.next())files.append({query.value(columns.indexOf("LogicalName")).toString(),
        query.value(columns.indexOf("Type")).toString(),query.value(columns.indexOf("FileId")).toInt()});
    if(query.lastError().isValid()){*error=query.lastError().text();return false;}query.finish();
    const QString sql=T3BackupRestorer::restoreSql(backupPath,*account,files,dataDirectory,logDirectory,token);
    if(sql.isEmpty()){*error=QString::fromUtf8(u8"无法读取首年度备份的数据库文件信息。");return false;}
    // The metadata database and its files have a unique token. Clean only a database
    // whose actual files carry our token, including when RESTORE fails partway.
    bool cleaned=false;
    const auto cleanup=[&](){
        if(cleaned)return true;cleaned=true;
        QString cleanupError;
        const QString drop=QStringLiteral("USE master; IF DB_ID(%1) IS NOT NULL BEGIN "
            "IF EXISTS (SELECT 1 FROM sys.master_files WHERE database_id=DB_ID(%1) AND CHARINDEX(%2,physical_name)=0) "
            "RAISERROR(N'Metadata database ownership check failed.',16,1); ELSE DROP DATABASE %3; END;")
            .arg(T3BackupRestorer::quoteLiteral(account->targetDatabase),T3BackupRestorer::quoteLiteral("T3_"+token+"_"),
                T3BackupRestorer::quoteIdentifier(account->targetDatabase));
        if(t3Execute(db,drop,&cleanupError))return true;
        *error+=QString::fromUtf8(u8"\n临时信息库 %1 清理失败，已保留：%2").arg(account->targetDatabase,cleanupError);
        return false;
    };
    struct Cleanup { std::function<void()> action; ~Cleanup(){action();} } guard{[&](){cleanup();}};
    t3Report(progress,QString::fromUtf8(u8"从首年度 %1 自动提取账套信息（完成后清理临时库）...").arg(firstSet.databaseName));
    bool ok=t3Execute(db,sql,error) && t3ReadAccount(db,account,error);
    if(!cleanup())ok=false;
    return ok;
}

T3RestoreResult T3BackupRestorer::restore(const T3SqlConnection& options,const T3BackupAnalysis& analysis,
    const QVector<T3RestoreSelection>& selections,const QString& dataDirectory,const QString& logDirectory,
    const Cancel& cancel,const Progress& progress)
{
    T3RestoreResult result;result.error=validateCollectionSelections(selections);if(!result.error.isEmpty())return result;
    if(!analysis.success || analysis.preparedPath.isEmpty()){result.error=QString::fromUtf8(u8"请先读取备份。");return result;}
    T3SqlSession session;if(!session.open(options,&result.error))return result;
    auto current=t3ReadSets(session.db,analysis.preparedPath,&result.error);if(!result.error.isEmpty())return result;
    T3ServerInfo targetInfo;if(!t3ServerInfo(session.db,&targetInfo,&result.error))return result;
    result.error=SqlBackupCompatibility::error(current,targetInfo);
    if(!result.error.isEmpty())return result;
    // Reject every conflicting target before creating any database.
    for(const auto& selected:selections)
    {
        if(t3Cancelled(cancel)){result.stopped=true;return result;}
        bool found=false;
        for(const auto& set:current)
        {
            if(set.position==selected.backup.position&&set.databaseName==selected.backup.databaseName
                &&set.backupSetId==selected.backup.backupSetId&&set.databaseVersion==selected.backup.databaseVersion)found=true;
        }
        if(!found){result.error=QString::fromUtf8(u8"备份内容已变化，请重新读取。");return result;}
        QSqlQuery exists(session.db);
        if(!exists.exec(QStringLiteral("SELECT DB_ID(%1)").arg(quoteLiteral(selected.targetDatabase)))||!exists.next())
        {result.error=exists.lastError().text();return result;}
        if(!exists.value(0).isNull()){result.error=QString::fromUtf8(u8"目标数据库 %1 已存在，请修改名称；没有开始恢复。").arg(selected.targetDatabase);return result;}
    }
    QVector<QString> commands;
    // Preflight every selected database before restoring any of them.
    for(const auto& selected:selections)
    {
        if(t3Cancelled(cancel)){result.stopped=true;return result;}
        bool found=false;
        for(const auto& set:current)
            if(set.position==selected.backup.position && set.databaseName==selected.backup.databaseName
                && set.backupSetId==selected.backup.backupSetId)found=true;
        if(!found){result.error=QString::fromUtf8(u8"备份内容已变化，请重新读取。");return result;}
        QSqlQuery query(session.db);
        if(!query.exec(QStringLiteral("SELECT DB_ID(%1)").arg(quoteLiteral(selected.targetDatabase)))||!query.next())
        {result.error=query.lastError().text();return result;}
        if(!query.value(0).isNull()){result.error=QString::fromUtf8(u8"目标数据库 %1 已存在，请修改名称；没有开始恢复。").arg(selected.targetDatabase);return result;}
        query.finish();
        if(!query.exec(QStringLiteral("RESTORE FILELISTONLY FROM DISK=%1 WITH FILE=%2").arg(t3DiskLiteral(analysis.preparedPath)).arg(selected.backup.position)))
        {result.error=query.lastError().text();return result;}
        const QSqlRecord columns=query.record();QVector<T3RestoreFile> files;
        while(query.next())
        {
            T3RestoreFile file;file.logicalName=query.value(columns.indexOf("LogicalName")).toString();
            file.type=query.value(columns.indexOf("Type")).toString();file.fileId=query.value(columns.indexOf("FileId")).toInt();files.append(file);
        }
        if(query.lastError().isValid()){result.error=query.lastError().text();return result;}
        query.finish(); // Release FILELISTONLY before issuing another ODBC command.
        const QString token=QUuid::createUuid().toString(QUuid::Id128);
        const QString command=restoreSql(analysis.preparedPath,selected,files,dataDirectory,logDirectory,token);
        if(command.isEmpty()){result.error=QString::fromUtf8(u8"数据库文件类型或服务器数据/日志目录无效。");return result;}
        QStringList verifyMoves;
        for(const auto& file:files)
        {
            const QString filename=QStringLiteral("T3_%1_%2%3").arg(token).arg(file.fileId).arg(file.type=="L"?".ldf":(file.fileId==1?".mdf":".ndf"));
            verifyMoves.append(QStringLiteral("MOVE %1 TO %2").arg(quoteLiteral(file.logicalName),quoteLiteral(t3JoinServerPath(file.type=="L"?logDirectory:dataDirectory,filename))));
        }
        t3Report(progress,QString::fromUtf8(u8"校验 %1...").arg(selected.backup.databaseName));
        if(!t3Execute(session.db,QStringLiteral("RESTORE VERIFYONLY FROM DISK=%1 WITH FILE=%2, %3")
            .arg(t3DiskLiteral(analysis.preparedPath)).arg(selected.backup.position).arg(verifyMoves.join(", ")),&result.error))return result;
        commands.append(command);
    }
    if(t3Cancelled(cancel)){result.stopped=true;return result;}
    QMap<QString,T3RestoreSelection> accounts;
    for(const auto& selected:selections)
    {
        const QString sourceId=t3DatabaseParts(selected.backup.databaseName).captured(1);
        if(accounts.contains(sourceId))continue;
        if(t3Cancelled(cancel)){result.stopped=true;return result;}
        auto firstSet=selected.backup;
        for(const auto& set:current)
        {
            const auto parts=t3DatabaseParts(set.databaseName);
            if(parts.hasMatch() && parts.captured(1)==sourceId && set.position<firstSet.position)firstSet=set;
        }
        T3RestoreSelection account;
        if(!t3LoadBackupAccount(session.db,analysis.preparedPath,firstSet,dataDirectory,logDirectory,&account,&result.error,progress))
        {result.error=QString::fromUtf8(u8"自动读取备份账套信息失败，尚未恢复目标年度库：%1").arg(t3Redact(result.error,options));return result;}
        accounts.insert(sourceId,account);
        t3Report(progress,QString::fromUtf8(u8"已自动读取账套：%1；行业性质：%2").arg(account.accountName,account.tradeKind));
    }
    if(t3Cancelled(cancel)){result.stopped=true;return result;}
    t3Report(progress,QString::fromUtf8(u8"准备采集控制库 UFSystem，并检查账套登记信息..."));
    if(!t3Execute(session.db,controlDatabaseSql(dataDirectory,logDirectory,QUuid::createUuid().toString(QUuid::Id128)),&result.error)
        || !t3Execute(session.db,controlSchemaSql(),&result.error))
    {result.error=QString::fromUtf8(u8"控制库准备失败，尚未恢复年度库：%1").arg(t3Redact(result.error,options));return result;}
    QVector<T3RestoreSelection> registrations=selections;
    QMap<QString,QStringList> targetMetadata;
    for(auto& selected:registrations)
    {
        const auto account=accounts.value(t3DatabaseParts(selected.backup.databaseName).captured(1));
        selected.accountName=account.accountName;selected.tradeKind=account.tradeKind;
        const QString id=t3DatabaseParts(selected.targetDatabase).captured(1);
        const QStringList metadata{selected.accountName,selected.tradeKind};
        if(targetMetadata.contains(id) && targetMetadata.value(id)!=metadata)
        {result.error=QString::fromUtf8(u8"不同账套不能登记到同一目标账套号；尚未恢复目标年度库。");return result;}
        targetMetadata.insert(id,metadata);
        if(t3Cancelled(cancel)){result.stopped=true;return result;}
        if(!t3Execute(session.db,registerAccountSql(selected,true),&result.error))
        {result.error=QString::fromUtf8(u8"控制库登记检查失败，尚未恢复目标年度库：%1").arg(t3Redact(result.error,options));return result;}
    }
    if(!t3Execute(session.db,QStringLiteral("USE master;"),&result.error))return result;
    for(int index=0;index<selections.size();++index)
    {
        if(t3Cancelled(cancel)){result.stopped=true;break;}
        T3RestoreItem item;item.database=selections.at(index).targetDatabase;
        t3Report(progress,QString::fromUtf8(u8"正在恢复 %1（%2/%3）...").arg(item.database).arg(index+1).arg(selections.size()));
        item.success=t3Execute(session.db,commands.at(index),&item.error);
        if(item.success)
        {
            QSqlQuery state(session.db);
            if(!state.exec(QStringLiteral("SELECT state_desc FROM sys.databases WHERE name=%1").arg(quoteLiteral(item.database)))
                ||!state.next()||state.value(0).toString()!=QStringLiteral("ONLINE"))
            {item.success=false;item.error=QString::fromUtf8(u8"恢复命令已执行，但未确认数据库为 ONLINE，请检查服务器。");}
        }
        item.databaseRestored=item.success;
        if(item.success)
        {
            t3Report(progress,QString::fromUtf8(u8"登记 %1 到 UFSystem.UA_Account...").arg(item.database));
            item.success=t3Execute(session.db,registerAccountSql(registrations.at(index)),&item.error);
            if(!item.success)item.error=QString::fromUtf8(u8"年度库已恢复，但控制库登记失败，尚不能确认可采集：%1").arg(item.error);
        }
        result.items.append(item);
        t3Report(progress,item.success?QString::fromUtf8(u8"成功：%1（ONLINE，已登记采集控制库）").arg(item.database)
            :QString::fromUtf8(u8"失败：%1\n%2").arg(item.database,t3Redact(item.error,options)));
        if(!item.success)
        {result.error=item.databaseRestored?QString::fromUtf8(u8"恢复已停止：年度库已保留，控制库登记失败。请检查 UFSystem 权限及账套信息。")
            :QString::fromUtf8(u8"恢复已停止。已成功的数据库予以保留；失败目标可能处于 RESTORING 状态，请检查后处理。");break;}
        if(!t3Execute(session.db,QStringLiteral("USE master;"),&result.error))break;
    }
    result.error=t3Redact(result.error,options);return result;
}
