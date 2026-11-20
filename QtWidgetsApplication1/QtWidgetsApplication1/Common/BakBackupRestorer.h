#pragma once
#include "T3BackupRestorer.h"

// One row per backup set; an unreadable file is retained as a row with readError.
struct BakBackupEntry
{
    QString sourcePath;
    T3BackupAnalysis analysis;
    int softwareVersionBuild = 0;
    int compatibilityLevel = 0;
    int backupType = 0;
    QString backupTypeDescription;
    QString readError;
    int mediaFamilyCount = 0;
    bool damaged = false;
    QString mediaSetId;
    qint64 sourceSize = -1;
    qint64 sourceModified = 0;
};
struct BakBackupBatch
{
    QVector<BakBackupEntry> entries;
    QString readerServer;
    QString error;
    bool stopped = false;
};
struct BakRestoreSelection
{
    int entryIndex = -1;
    QString targetDatabase;
};
class BakBackupRestorer
{
public:
    using Cancel = T3BackupRestorer::Cancel;
    using Progress = T3BackupRestorer::Progress;
    // Directory scans include subdirectories, without following symbolic links.
    static QStringList discoverFiles(const QString& source, QString* error);
    static QString sourceVersionText(const BakBackupEntry& entry);
    static bool isRestorable(const BakBackupEntry& entry);
    static BakBackupBatch read(const QVector<T3SqlConnection>& candidates, const QString& source,
        const QString& stagingDirectory, const Cancel& cancel = {}, const Progress& progress = {});
    // Keeps metadata and per-row failures; only compatible rows prepare a target-visible file.
    static BakBackupBatch validateTarget(const BakBackupBatch& batch, const T3SqlConnection& options,
        const QString& stagingDirectory, const Cancel& cancel = {}, const Progress& progress = {});
    static QString validateSelections(const BakBackupBatch& batch, const QVector<BakRestoreSelection>& selections);
    static T3RestoreResult restore(const T3SqlConnection& options, const BakBackupBatch& batch,
        const QVector<BakRestoreSelection>& selections, const QString& dataDirectory,
        const QString& logDirectory, const Cancel& cancel = {}, const Progress& progress = {});
};
