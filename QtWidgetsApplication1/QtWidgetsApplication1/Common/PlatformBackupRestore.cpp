#include "PlatformBackupRestore.h"
#include "BakBackupRestorer.h"
#include "InspurBackupRestorer.h"
#include "KisProBackupRestorer.h"
#include "SudaBackupUnpacker.h"
#include "TPlusBackupRestorer.h"
#include "U8BackupRestorer.h"
#include "XinZhongDaBackupRestorer.h"
#include "logger.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>

namespace {
// Picker names match the corresponding export handlers in the import client.
struct SudaPicker
{
    QString platform;
    QString databasePattern;
    QString select;
};

const SudaPicker SudaPickers[] = {
    {"suda-3000-pro", "%CWACCSET|%SDAccset", "SELECT ACCSETNAME,CORPNAME FROM %1.dbo.ACCSET"},
    {"suda-3000-std", "%CWACCSET|%SDAccset", "SELECT NAME,CORPNAME FROM %1.dbo.ACCSET WHERE NOT EXISTS "
        "(SELECT 1 FROM %1.INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA='dbo' AND TABLE_NAME='ACCSET' AND COLUMN_NAME='ACCSETNAME')"},
    {"suda-v3-industrial", "SD31802N%", "SELECT MAX(CASE WHEN optionname='ACCSETNAME' THEN optionvalue END),"
        "MAX(CASE WHEN optionname='COMPANYNAME' THEN optionvalue END) FROM %1.dbo.accoptions"},
    {"suda-v3", "SDCW%", "SELECT ACCSETNAME,CORPNAME FROM %1.dbo.ACCSET"},
    {"suda-v5", "%SDAccset", "SELECT NAME,CORPNAME FROM %1.dbo.ACCSET"},
    {"suda-v7", "%SDAccset", "SELECT NAME,CORPNAME FROM %1.dbo.ACCSET"},
    {"suda-5000-pro", "%SDAccset", "SELECT NAME,CORPNAME FROM %1.dbo.ACCSET"},
    {"suda-7000", "%SDAccset", "SELECT NAME,CORPNAME FROM %1.dbo.ACCSET"}
};

const SudaPicker *sudaPicker(const QString &platform)
{
    for (const auto &picker : SudaPickers)
        if (picker.platform == platform) return &picker;
    return nullptr;
}

// Checks the importer's initial name filter before SQL touches any target.
bool matchesPickerName(const SudaPicker &picker, const QString &database)
{
    for (const auto &pattern : picker.databasePattern.split('|')) {
        if (pattern.startsWith('%') && database.endsWith(pattern.mid(1), Qt::CaseInsensitive)) return true;
        if (pattern.endsWith('%') && database.startsWith(pattern.chopped(1), Qt::CaseInsensitive)) return true;
    }
    return false;
}

T3RestoreResult restoreError(const QString &message)
{
    T3RestoreResult result;
    result.error = message;
    return result;
}

QJsonObject describe(const T3BackupSet &set, int row)
{
    return {{"set", row}, {"position", set.position}, {"backupSetId", set.backupSetId},
        {"database", set.databaseName}, {"recommendedDatabase", set.databaseName},
        {"backupDate", set.backupDate}, {"databaseVersion", set.databaseVersion}, {"restorable", true}};
}

// Reuse each desktop adapter, preserving its backup metadata and naming rules.
template <class Restorer>
void readRegisteredBackup(PlatformBackupCatalog &catalog, const T3SqlConnection &connection,
                          const QString &source, const QString &staging, bool groupU8 = false)
{
    const auto analysis = Restorer::analyze(connection, source, staging, {}, &AppLogger::operation);
    if (!analysis.success) {
        catalog.error = analysis.error;
        return;
    }
    const auto mainSet = std::min_element(analysis.sets.cbegin(), analysis.sets.cend(), [](const auto &left, const auto &right) {
        const bool leftIsData = left.databaseName.startsWith("UFDATA_", Qt::CaseInsensitive);
        const bool rightIsData = right.databaseName.startsWith("UFDATA_", Qt::CaseInsensitive);
        return leftIsData != rightIsData ? leftIsData : left.databaseName.right(4) < right.databaseName.right(4);
    });
    for (int row = 0; row < analysis.sets.size(); ++row) {
        auto item = describe(analysis.sets.at(row), row + 1);
        if (groupU8) {
            item["restorable"] = analysis.sets.cbegin() + row == mainSet;
            item["includesAllSets"] = item.value("restorable");
        }
        catalog.entries.append(item);
    }
    catalog.restore = [analysis, connection, groupU8](int row, const QString &database) {
        const auto &set = analysis.sets.at(row);
        QVector<T3RestoreSelection> selections{{set, database}};
        if (groupU8) {
            const QString targetAccount = database.section('_', 1, 1);
            for (const auto &companion : analysis.sets) {
                if (companion.position == set.position) continue;
                auto name = companion.databaseName.split('_');
                name[1] = targetAccount;
                selections.append({companion, name.join('_')});
            }
        }
        return Restorer::restore(connection, analysis, selections, analysis.server.dataDirectory,
            analysis.server.logDirectory, {}, &AppLogger::operation);
    };
}

// Copies a 速达 archive into owned staging for the existing BAK engine.
void readSuda(PlatformBackupCatalog &catalog, const T3SqlConnection &connection,
              const QString &source, const QString &staging, const SudaPicker &picker)
{
    if (!QFileInfo(source).isFile()) {
        catalog.error = QStringLiteral("请选择一个速达备份文件。");
        return;
    }
    if (!QDir().mkpath(staging)) {
        catalog.error = QStringLiteral("无法创建速达备份暂存目录。");
        return;
    }
    const auto temporary = std::make_shared<QTemporaryDir>(QDir(staging).filePath("SudaCli-XXXXXX"));
    if (!temporary->isValid()) {
        catalog.error = temporary->errorString();
        return;
    }
    const QString unpacked = temporary->filePath("SDBackup.bak");
    const auto batch = [&]() {
        QFile input(source);
        if (QFileInfo(source).suffix().compare("bak", Qt::CaseInsensitive) == 0
            && input.open(QIODevice::ReadOnly) && BakBackupRestorer::isSupportedHeader(input.peek(8)))
            return BakBackupRestorer::read({connection}, source, staging, {}, &AppLogger::operation);
        const auto result = SudaBackupUnpacker::unpack(QDir::toNativeSeparators(source).toStdWString(),
            QDir::toNativeSeparators(unpacked).toStdWString());
        if (result.success) return BakBackupRestorer::read({connection}, unpacked, staging, {}, &AppLogger::operation);
        BakBackupBatch failed;
        failed.error = QString::fromUtf8(result.error.c_str());
        return failed;
    }();
    catalog.error = batch.error;
    for (int index = 0; index < batch.entries.size(); ++index) {
        const auto &entry = batch.entries.at(index);
        auto row = describe(entry.analysis.sets.value(0), index + 1);
        row["restorable"] = BakBackupRestorer::isRestorable(entry);
        row["error"] = entry.analysis.error;
        row["databaseNamePattern"] = picker.databasePattern;
        if (!matchesPickerName(picker, row.value("database").toString())) row.remove("recommendedDatabase");
        catalog.entries.append(row);
    }
    catalog.restore = [batch, temporary, staging, connection, picker](int row, const QString &database) {
        if (!matchesPickerName(picker, database))
            return restoreError(QStringLiteral("目标库名不符合该速达版本的账套筛选规则（%1）。").arg(picker.databasePattern));
        BakBackupBatch selected;
        selected.entries.append(batch.entries.at(row));
        const auto validated = BakBackupRestorer::validateTarget(selected, connection, staging, {}, &AppLogger::operation);
        const auto &backup = validated.entries.first();
        if (!validated.error.isEmpty() || !backup.analysis.success)
            return restoreError(validated.error.isEmpty() ? backup.analysis.error : validated.error);
        return BakBackupRestorer::restore(connection, validated, {{0, database}}, backup.analysis.server.dataDirectory,
            backup.analysis.server.logDirectory, {}, &AppLogger::operation);
    };
}

void readXinZhongDa(PlatformBackupCatalog &catalog, const T3SqlConnection &connection, const QString &source)
{
    T3ServerInfo server;
    if (!T3BackupRestorer::testConnection(connection, &server, &catalog.error)) return;
    const auto analyses = XinZhongDaBackupRestorer::discover(source, &catalog.error, {}, &AppLogger::operation);
    for (const auto &analysis : analyses) {
        const QString database = XinZhongDaBackupRestorer::targetDatabase(analysis.accountCode, analysis.year);
        catalog.entries.append(QJsonObject{{"set", catalog.entries.size() + 1}, {"database", database},
            {"recommendedDatabase", database}, {"accountName", analysis.accountName}, {"year", analysis.year},
            {"input", analysis.directory}, {"restorable", analysis.success}, {"error", analysis.error}});
    }
    catalog.restore = [analyses, connection](int row, const QString &database) {
        const auto &analysis = analyses.at(row);
        const QString code = database.mid(4, database.size() - 8);
        const QString expected = XinZhongDaBackupRestorer::targetDatabase(code, analysis.year);
        if (expected.isEmpty() || database != expected)
            return restoreError(QStringLiteral("新中大目标库名必须是 USER 加单位编号和备份年度。"));
        const auto restored = XinZhongDaBackupRestorer::restore(connection, analysis, code, {}, &AppLogger::operation);
        T3RestoreResult result = restoreError(restored.error);
        result.stopped = restored.cancelled;
        result.items.append({database, restored.success, restored.success, restored.error});
        return result;
    };
}

// Use the same registration keys and source columns as the importer's picker.
QString pickerQuery(const QString &platform, const QString &database)
{
    const auto key = T3BackupRestorer::quoteLiteral(database);
    const auto id = T3BackupRestorer::quoteLiteral(database.section('_', 1, 1));
    if (platform == "t3")
        return QStringLiteral("SELECT 'UFData_'+cAcc_Id+'_'+CAST(iYear AS varchar),cAcc_Name FROM UFSystem.dbo.UA_Account "
            "WHERE cAcc_Id=%1 AND HAS_DBACCESS('UFData_'+cAcc_Id+'_'+CAST(iYear AS varchar))=1").arg(id);
    if (platform == "u8")
        return QStringLiteral("SELECT a.cAcc_Id,a.cUnitName FROM UFSystem.dbo.UA_Account a WHERE a.cAcc_Id=%1 "
            "AND EXISTS(SELECT 1 FROM UFSystem.dbo.UA_AccountDatabase m WHERE m.cAcc_Id=a.cAcc_Id AND m.cDatabase=%2) "
            "AND HAS_DBACCESS((SELECT TOP 1 cDatabase FROM UFSystem.dbo.UA_AccountDatabase "
            "WHERE cAcc_Id=a.cAcc_Id ORDER BY iBeginYear))=1").arg(id, key);
    if (platform == "tplus")
        return QStringLiteral("SELECT DsName,cAcc_Name FROM UFTSystem.dbo.EAP_Account WHERE DsName=%1").arg(key);
    if (platform == "kis-pro")
        return QStringLiteral("SELECT FDBName,FAcctName FROM AcctCtl.dbo.t_ad_kdAccount_gl WHERE FDBName=%1").arg(key);
    if (platform == "inspur")
        return QStringLiteral("SELECT LTRIM(RTRIM(gsdbztxx_owner))+','+gsdbztxx_base,gsdbztxx_ztmc "
            "FROM cwmaster.dbo.gsdbztxx WHERE gsdbztxx_base=%1").arg(key);
    if (platform == "xinzhongda")
        return QStringLiteral("SELECT dwdm,dwmc FROM PUBDATA.dbo.unit WHERE dwdm=%1")
            .arg(T3BackupRestorer::quoteLiteral(database.mid(4, database.size() - 8)));
    return {};
}

class PickerSession
{
public:
    QString name = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlDatabase database = QSqlDatabase::addDatabase("QODBC", name);
    ~PickerSession()
    {
        database.close();
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
};
}

bool PlatformBackupRestore::supports(const QString &platform)
{
    return QStringList{"t3", "u8", "tplus", "kis-pro", "inspur", "xinzhongda"}.contains(platform) || sudaPicker(platform);
}

PlatformBackupCatalog PlatformBackupRestore::inspect(const QString &platform, const QString &source,
                                                     const QString &staging, const T3SqlConnection &connection)
{
    PlatformBackupCatalog catalog;
    catalog.platform = platform;
    catalog.instance = connection.server;
    if (platform == "t3") readRegisteredBackup<T3BackupRestorer>(catalog, connection, source, staging);
    else if (platform == "u8") readRegisteredBackup<U8BackupRestorer>(catalog, connection, source, staging, true);
    else if (platform == "tplus") readRegisteredBackup<TPlusBackupRestorer>(catalog, connection, source, staging);
    else if (platform == "kis-pro") readRegisteredBackup<KisProBackupRestorer>(catalog, connection, source, staging);
    else if (platform == "inspur") readRegisteredBackup<InspurBackupRestorer>(catalog, connection, source, staging);
    else if (platform == "xinzhongda") readXinZhongDa(catalog, connection, source);
    else if (const auto *picker = sudaPicker(platform)) readSuda(catalog, connection, source, staging, *picker);
    return catalog;
}

QJsonArray PlatformBackupRestore::verify(const QString &platform, const T3SqlConnection &connection,
                                        const QString &database, QString *error)
{
    PickerSession session;
    session.database.setConnectOptions("SQL_ATTR_LOGIN_TIMEOUT=10;SQL_ATTR_CONNECTION_TIMEOUT=15");
    session.database.setDatabaseName(T3BackupRestorer::connectionString(connection));
    if (!session.database.open()) {
        *error = session.database.lastError().text();
        return {};
    }
    QSqlQuery query(session.database);
    const auto literal = T3BackupRestorer::quoteLiteral(database);
    if (!query.exec(QStringLiteral("SELECT 1 FROM sys.databases WHERE name=%1 AND state=0 AND HAS_DBACCESS(name)=1").arg(literal))
        || !query.next()) {
        *error = QStringLiteral("目标数据库未确认在线且可连接。");
        return {};
    }
    query.finish();
    const auto *suda = sudaPicker(platform);
    if (suda) {
        QStringList filters;
        for (const auto &pattern : suda->databasePattern.split('|'))
            filters.append("name LIKE " + T3BackupRestorer::quoteLiteral(pattern));
        if (!query.exec(QStringLiteral("SELECT 1 FROM master.dbo.SysDatabases WHERE name=%1 AND (%2)")
                .arg(literal, filters.join(" OR "))) || !query.next()) {
            *error = QStringLiteral("目标库名没有通过该速达版本的账套初筛。");
            return {};
        }
        query.finish();
    }
    const QString sql = suda ? suda->select.arg(T3BackupRestorer::quoteIdentifier(database)) : pickerQuery(platform, database);
    if (sql.isEmpty() || !query.exec(sql)) {
        *error = QStringLiteral("导账工具账套列表查询失败。%1").arg(query.lastError().text());
        return {};
    }
    QJsonArray accounts;
    while (query.next()) {
        const QString id = query.value(0).toString().trimmed();
        const QString name = query.value(1).toString().trimmed();
        if (!id.isEmpty() && !name.isEmpty())
            accounts.append(QJsonObject{{"id", suda ? database + ',' + id : id}, {"name", name}});
    }
    if (query.lastError().isValid() || accounts.isEmpty()) {
        *error = QStringLiteral("该平台的账套列表没有返回可选账套。");
        return {};
    }
    return accounts;
}
