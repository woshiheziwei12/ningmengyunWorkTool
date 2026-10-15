#include "SqlServerCredentials.h"
#include "DatabaseCleaner.h"
#include "T3BackupRestorer.h"
#include "LocalSqlInstances.h"
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <algorithm>

namespace DatabaseCleaner {
namespace {
QString literal(QString value) { return "N'" + value.replace("'", "''") + "'"; }
QString identifier(QString value) { return '[' + value.replace("]", "]]") + ']'; }
QString protectedPredicate()
{
    return QStringLiteral("database_id > 4 AND database_id <> 32767 AND is_distributor = 0 "
        "AND LOWER(name) NOT IN (N'master',N'tempdb',N'model',N'msdb',N'mssqlsystemresource',N'distribution')");
}
bool sameDatabase(const Database& a, const Database& b)
{
    return a.id == b.id && a.name == b.name && a.created == b.created
        && a.sourceId == b.sourceId && a.distributor == b.distributor;
}
class SqlSession : public Session {
    Target m_target;
    QString m_localInstance;
    QString m_name = QUuid::createUuid().toString();
    QSqlDatabase m_db;
    QString m_password;
    QString m_error;
    QString redact(QString text) const
    {
        if (!m_password.isEmpty()) text.replace(m_password, QStringLiteral("***"));
        return text;
    }
public:
    explicit SqlSession(Target target, const QString& localInstance) : m_target(target), m_localInstance(localInstance), m_db(QSqlDatabase::addDatabase("QODBC", m_name))
    {
        T3SqlConnection options;
        if (target == Target::Server211) {
            options.server = QStringLiteral("192.168.1.211");
            options.windowsAuthentication = false;
            SqlServerCredentials::apply211(options);
            if (options.password.isEmpty()) {
                m_error = QString::fromUtf8(u8"211 数据库密码未配置，请检查 SqlServer211.local.json 或环境变量。");
                return;
            }
        } else if (target != Target::Local) {
            m_error = QString::fromUtf8(u8"不支持的数据库实例。");
            return;
        }
        if (target == Target::Local) {
            if (!LocalSqlInstances::isLocalEndpoint(localInstance)) {
                m_error = QString::fromUtf8(u8"无效的本地数据库实例，请重新选择。");
                return;
            }
            options.server = localInstance;
        }
        m_password = options.password;
        const auto drivers = T3BackupRestorer::availableDrivers();
        if (drivers.isEmpty()) {
            m_error = QString::fromUtf8(u8"未安装 32 位 SQL Server ODBC Driver 17 或 18。");
            return;
        }
        options.driver = drivers.first();
        m_db.setConnectOptions("SQL_ATTR_LOGIN_TIMEOUT=10;SQL_ATTR_CONNECTION_TIMEOUT=15");
        m_db.setDatabaseName(T3BackupRestorer::connectionString(options));
        if (!m_db.open()) m_error = redact(m_db.lastError().text());
    }
    ~SqlSession() override
    {
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_name);
    }
    Catalog read() override
    {
        Catalog result;
        result.target = m_target;
        result.localInstance = m_localInstance;
        result.error = m_error;
        if (!result.error.isEmpty()) return result;
        QSqlQuery query(m_db);
        if (!query.exec("SELECT CONVERT(nvarchar(128),SERVERPROPERTY('ServerName')), IS_SRVROLEMEMBER('sysadmin')") || !query.next()) {
            result.error = redact(query.lastError().text());
            if (result.error.isEmpty()) result.error = QString::fromUtf8(u8"无法读取服务器身份。");
            return result;
        }
        result.server = query.value(0).toString();
        if (query.value(1).toInt() != 1) {
            result.error = QString::fromUtf8(u8"当前内置连接没有 sysadmin 权限，无法完整枚举并清理实例数据库。");
            return result;
        }
        query.finish();
        if (!query.exec("SELECT database_id,name,CONVERT(nvarchar(23),create_date,126),state_desc,"
                        "ISNULL(source_database_id,0),is_distributor FROM sys.databases ORDER BY name")) {
            result.error = redact(query.lastError().text());
            return result;
        }
        while (query.next()) {
            Database database;
            database.id = query.value(0).toInt();
            database.name = query.value(1).toString();
            database.created = query.value(2).toString();
            database.state = query.value(3).toString();
            database.sourceId = query.value(4).toInt();
            database.distributor = query.value(5).toBool();
            if (isUserDatabase(database)) result.databases.append(database);
        }
        if (query.lastError().isValid()) result.error = redact(query.lastError().text());
        return result;
    }
    QString drop(const Database& database) override
    {
        const QString sql = dropSql(database);
        if (sql.isEmpty()) return QString::fromUtf8(u8"拒绝删除系统库或无效数据库。");
        QSqlQuery query(m_db);
        if (!query.exec(sql)) return redact(query.lastError().text());
        // SQL Server can report an error in a later result set of a batch.
        do { while (query.next()) {} } while (query.nextResult());
        if (query.lastError().isValid()) return redact(query.lastError().text());
        query.prepare("SELECT COUNT(*) FROM sys.databases WHERE name = ?");
        query.addBindValue(database.name);
        if (!query.exec() || !query.next()) return redact(query.lastError().text()) + QString::fromUtf8(u8"；无法核实删除结果。");
        return query.value(0).toInt() == 0 ? QString() : QString::fromUtf8(u8"数据库仍存在，请刷新列表检查。");
    }
};
}

QString targetLabel(Target target, const QString& localInstance)
{
    return target == Target::Local ? LocalSqlInstances::label(localInstance) + QString::fromUtf8(u8"（Windows 身份验证）")
        : QString::fromUtf8(u8"211 数据库（192.168.1.211 / sa）");
}
bool isUserDatabase(const Database& database)
{
    static const QStringList systemNames = {"master", "tempdb", "model", "msdb", "mssqlsystemresource", "distribution"};
    return database.id > 4 && database.id != 32767 && !database.distributor
        && !database.name.isEmpty() && database.name.size() <= 128 && !database.name.contains(QChar(0))
        && !systemNames.contains(database.name, Qt::CaseInsensitive);
}
QString dropSql(const Database& database)
{
    static const QRegularExpression datePattern("^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}(?:\\.\\d{3})?$");
    if (!isUserDatabase(database) || !datePattern.match(database.created).hasMatch()) return {};
    const QString identity = QStringLiteral("database_id=%1 AND name=%2 AND create_date=CONVERT(datetime,%3,126) AND %4")
        .arg(database.id).arg(literal(database.name), literal(database.created), protectedPredicate());
    const QString quoted = identifier(database.name);
    const QString singleDrop = literal("ALTER DATABASE " + quoted + " SET SINGLE_USER WITH ROLLBACK IMMEDIATE; DROP DATABASE " + quoted + ";");
    const QString directDrop = literal("DROP DATABASE " + quoted + ";");
    // Revalidate inside the batch, then restore the original access mode if DROP fails.
    return QStringLiteral(
        "USE master; SET NOCOUNT ON; SET IMPLICIT_TRANSACTIONS OFF; SET LOCK_TIMEOUT 15000;\n"
        "IF NOT EXISTS (SELECT 1 FROM sys.databases WHERE %1)\n"
        "BEGIN RAISERROR(N'Database identity changed or database is protected. Refresh the list.',16,1); RETURN; END;\n"
        "DECLARE @access nvarchar(60), @single bit;\n"
        "SELECT @access=user_access_desc,@single=CASE WHEN state=0 AND source_database_id IS NULL THEN 1 ELSE 0 END "
        "FROM sys.databases WHERE %1;\n"
        "BEGIN TRY\n"
        "IF @single=1 EXEC(%2); ELSE EXEC(%3);\n"
        "END TRY BEGIN CATCH\n"
        "DECLARE @message nvarchar(2048); SET @message=ERROR_MESSAGE();\n"
        "IF @single=1 AND EXISTS(SELECT 1 FROM sys.databases WHERE %1)\n"
        "BEGIN TRY\n"
        "DECLARE @restore nvarchar(max); SET @restore=%4 + @access + N' WITH ROLLBACK IMMEDIATE;'; EXEC(@restore);\n"
        "END TRY BEGIN CATCH SET @message=@message+N'; Access mode restore failed: '+ERROR_MESSAGE(); END CATCH;\n"
        "RAISERROR(N'%s',16,1,@message);\n"
        "END CATCH;")
        .arg(identity, singleDrop, directDrop, literal("ALTER DATABASE " + quoted + " SET "));
}
Catalog inspect(Target target, const QString& localInstance) { SqlSession session(target, localInstance); return session.read(); }
Result removeSelected(const Catalog& confirmed, const Cancel& cancel, const Progress& progress)
{
    SqlSession session(confirmed.target, confirmed.localInstance);
    return removeSelected(session, confirmed, cancel, progress);
}
Result removeSelected(Session& session, const Catalog& confirmed, const Cancel& cancel, const Progress& progress)
{
    Result result;
    if (confirmed.databases.isEmpty()) {
        result.error = QString::fromUtf8(u8"请先选择要删除的数据库。");
        return result;
    }
    const Catalog current = session.read();
    if (!current.error.isEmpty()) { result.error = current.error; return result; }
    bool matches = confirmed.error.isEmpty() && !confirmed.server.isEmpty()
        && confirmed.server == current.server && confirmed.target == current.target
        && (confirmed.target != Target::Local || confirmed.localInstance == current.localInstance);
    QSet<int> ids;
    for (const auto& database : confirmed.databases) {
        matches = matches && isUserDatabase(database) && !dropSql(database).isEmpty() && !ids.contains(database.id)
            && std::any_of(current.databases.begin(), current.databases.end(), [&](const Database& other) { return sameDatabase(database, other); });
        ids.insert(database.id);
    }
    if (!matches) {
        result.error = QString::fromUtf8(u8"实例或所选数据库已变化，请刷新列表后重新选择。");
        return result;
    }
    auto databases = confirmed.databases;
    std::stable_sort(databases.begin(), databases.end(), [](const Database& a, const Database& b) {
        return (a.sourceId != 0) > (b.sourceId != 0);
    });
    for (const auto& database : databases) {
        if (cancel && cancel()) { result.stopped = true; break; }
        ItemResult item;
        item.name = database.name;
        QStringList unselectedSnapshots;
        for (const auto& other : current.databases)
            if (other.sourceId == database.id && !ids.contains(other.id)) unselectedSnapshots.append(other.name);
        item.error = unselectedSnapshots.isEmpty() ? session.drop(database)
            : QString::fromUtf8(u8"存在未选择的数据库快照，请同时勾选后重试：%1").arg(unselectedSnapshots.join(", "));
        item.success = item.error.isEmpty();
        result.items.append(item);
        if (progress) progress(item);
    }
    const auto after = session.read();
    if (!after.error.isEmpty()) result.error = QString::fromUtf8(u8"清理后复核失败：%1").arg(after.error);
    else for (const auto& database : after.databases) result.remaining.append(database.name);
    return result;
}
}
