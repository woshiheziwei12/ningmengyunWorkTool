#include "importpackagehistory.h"
#include "logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

namespace
{
QMutex &historyMutex()
{
    static QMutex mutex;
    return mutex;
}

// Used under historyMutex when the accepted task could not be saved to disk.
QJsonArray &unsavedTasks()
{
    static QJsonArray tasks;
    return tasks;
}

QJsonArray readHistoryArray(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QJsonArray();

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    return error.error == QJsonParseError::NoError && document.isArray()
        ? document.array()
        : QJsonArray();
}

// Legacy entries without environment or account stay separate from scoped entries.
QString historyKey(const QJsonObject &record)
{
    return QStringLiteral("%1:%2:%3:%4")
        .arg(record.value("environment").toString())
        .arg(record.value("userId").toInt()).arg(record.value("serviceId").toInt())
        .arg(record.value("importId").toString().trimmed());
}

QJsonArray mergeHistoryArrays(const QJsonArray &primary, const QJsonArray &additional)
{
    QJsonArray merged;
    QSet<QString> seenImportIds;
    const auto appendRecords = [&merged, &seenImportIds](const QJsonArray &records) {
        for (const QJsonValue &value : records)
        {
            if (!value.isObject())
                continue;
            const QString importId = value.toObject()
                .value(QStringLiteral("importId")).toString().trimmed();
            const QString key = historyKey(value.toObject());
            if (importId.isEmpty() || seenImportIds.contains(key))
                continue;
            seenImportIds.insert(key);
            merged.append(value);
            if (merged.size() >= 100)
                return;
        }
    };
    appendRecords(primary);
    if (merged.size() < 100)
        appendRecords(additional);
    return merged;
}

bool writeHistoryArray(const QString &path, const QJsonArray &array)
{
    QDir directory(QFileInfo(path).absolutePath());
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
        return false;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    const auto data = QJsonDocument(array).toJson(QJsonDocument::Indented);
    return file.write(data) == data.size() && file.commit();
}
}

QString ImportPackageHistory::historyFilePath()
{
    QString dataDirectory = QStandardPaths::writableLocation(
        QStandardPaths::GenericDataLocation);
    if (dataDirectory.trimmed().isEmpty())
        dataDirectory = QCoreApplication::applicationDirPath();
    return QDir(dataDirectory).filePath(
        QStringLiteral("LemonCloud/LemonWorkTool/import_package_upload_history.json"));
}

bool ImportPackageHistory::recordSuccessfulUpload(
    const QString &zipPath,
    const QString &importId,
    int serviceId,
    const QString &importerApiHost,
    const QString &environment,
    int userId)
{
    const QString trimmedImportId = importId.trimmed();
    if (trimmedImportId.isEmpty())
        return false;

    QMutexLocker mutexLocker(&historyMutex());
    QJsonObject record;
    record.insert(QStringLiteral("importId"), trimmedImportId);
    record.insert(QStringLiteral("uploadTime"), QDateTime::currentDateTime().toString(Qt::ISODate));
    record.insert(QStringLiteral("fileName"), QFileInfo(zipPath).fileName());
    record.insert(QStringLiteral("serviceId"), serviceId);
    if (!environment.isEmpty() && userId > 0) {
        record["environment"] = environment;
        record["userId"] = userId;
    }
    if (!importerApiHost.trimmed().isEmpty())
        record.insert(QStringLiteral("importerApiHost"), importerApiHost.trimmed());

    const QString path = historyFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QLockFile processLock(path + QStringLiteral(".lock"));
    processLock.setStaleLockTime(30000);
    if (!processLock.tryLock(5000))
    {
        unsavedTasks() = mergeHistoryArrays(QJsonArray{record}, unsavedTasks());
        AppLogger::warning(QStringLiteral("导入任务历史文件正被占用，记录失败 importId=%1 path=%2")
            .arg(trimmedImportId, QDir::toNativeSeparators(path)));
        return false;
    }

    const auto updated = mergeHistoryArrays(mergeHistoryArrays(QJsonArray{record}, unsavedTasks()), readHistoryArray(path));
    const bool saved = writeHistoryArray(path, updated);
    if (!saved)
    {
        unsavedTasks() = mergeHistoryArrays(QJsonArray{record}, unsavedTasks());
        AppLogger::warning(QStringLiteral("写入共享导入任务历史失败 importId=%1 path=%2")
            .arg(trimmedImportId, QDir::toNativeSeparators(path)));
    }
    else {
        unsavedTasks() = {};
    }
    return saved;
}

QJsonObject ImportPackageHistory::scopedTask(const QString &importId, const QString &environment, int userId, int serviceId)
{
    for (const auto &value : scopedTasks(environment, userId, serviceId)) {
        const auto record = value.toObject();
        if (record.value("importId").toString() == importId) return record;
    }
    return {};
}

QJsonArray ImportPackageHistory::scopedTasks(const QString &environment, int userId, int serviceId)
{
    if (userId <= 0 || serviceId <= 0 || environment.isEmpty()) return {};
    QMutexLocker mutexLocker(&historyMutex());
    const auto records = mergeHistoryArrays(unsavedTasks(), readHistoryArray(historyFilePath()));
    QJsonArray result;
    for (const auto &value : records) {
        const auto record = value.toObject();
        if (record.value("environment").toString() == environment
            && record.value("userId").toInt() == userId && record.value("serviceId").toInt() == serviceId)
            result.append(record);
    }
    return result;
}

void ImportPackageHistory::migrateCurrentApplicationHistory()
{
    QString legacyDirectory = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    if (legacyDirectory.trimmed().isEmpty())
        legacyDirectory = QCoreApplication::applicationDirPath();
    const QString legacyPath = QDir(legacyDirectory).filePath(
        QStringLiteral("import_package_upload_history.json"));
    const QString sharedPath = historyFilePath();
    if (QDir::cleanPath(legacyPath).compare(QDir::cleanPath(sharedPath), Qt::CaseInsensitive) == 0
        || !QFileInfo::exists(legacyPath))
    {
        return;
    }

    QMutexLocker mutexLocker(&historyMutex());
    QDir().mkpath(QFileInfo(sharedPath).absolutePath());
    QLockFile processLock(sharedPath + QStringLiteral(".lock"));
    processLock.setStaleLockTime(30000);
    if (!processLock.tryLock(5000))
        return;

    const QJsonArray shared = readHistoryArray(sharedPath);
    const QJsonArray legacy = readHistoryArray(legacyPath);
    const QJsonArray merged = mergeHistoryArrays(shared, legacy);
    if (!legacy.isEmpty() && !writeHistoryArray(sharedPath, merged))
    {
        AppLogger::warning(QStringLiteral("迁移旧导入任务历史失败 source=%1 target=%2")
            .arg(QDir::toNativeSeparators(legacyPath),
                 QDir::toNativeSeparators(sharedPath)));
    }
}
