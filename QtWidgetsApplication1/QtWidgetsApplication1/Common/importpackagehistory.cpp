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
            if (importId.isEmpty() || seenImportIds.contains(importId))
                continue;
            seenImportIds.insert(importId);
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
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    return file.commit();
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

void ImportPackageHistory::recordSuccessfulUpload(
    const QString &zipPath,
    const QString &importId,
    int serviceId,
    const QString &importerApiHost)
{
    const QString trimmedImportId = importId.trimmed();
    if (trimmedImportId.isEmpty())
        return;

    QMutexLocker mutexLocker(&historyMutex());
    const QString path = historyFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QLockFile processLock(path + QStringLiteral(".lock"));
    processLock.setStaleLockTime(30000);
    if (!processLock.tryLock(5000))
    {
        AppLogger::warning(QStringLiteral("导入任务历史文件正被占用，记录失败 importId=%1 path=%2")
            .arg(trimmedImportId, QDir::toNativeSeparators(path)));
        return;
    }

    QJsonArray existing = readHistoryArray(path);
    QJsonArray withoutCurrent;
    for (const QJsonValue &value : existing)
    {
        if (!value.isObject()
            || value.toObject().value(QStringLiteral("importId")).toString().trimmed()
                != trimmedImportId)
        {
            withoutCurrent.append(value);
        }
    }

    QJsonObject record;
    record.insert(QStringLiteral("importId"), trimmedImportId);
    record.insert(QStringLiteral("uploadTime"), QDateTime::currentDateTime().toString(Qt::ISODate));
    record.insert(QStringLiteral("fileName"), QFileInfo(zipPath).fileName());
    record.insert(QStringLiteral("serviceId"), serviceId);
    if (!importerApiHost.trimmed().isEmpty())
        record.insert(QStringLiteral("importerApiHost"), importerApiHost.trimmed());

    QJsonArray updated;
    updated.append(record);
    for (const QJsonValue &value : withoutCurrent)
    {
        if (updated.size() >= 100)
            break;
        updated.append(value);
    }

    if (!writeHistoryArray(path, updated))
    {
        AppLogger::warning(QStringLiteral("写入共享导入任务历史失败 importId=%1 path=%2")
            .arg(trimmedImportId, QDir::toNativeSeparators(path)));
    }
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
