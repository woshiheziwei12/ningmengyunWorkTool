#pragma once
#include "T3BackupRestorer.h"

struct TPlusAccountMetadata
{
    QString databaseName;
    QString accountName;
    QString version;
    int year = 0;
    int month = 0;
};
struct TPlusBackupAnalysis : T3BackupAnalysis
{
    TPlusAccountMetadata account;
    QString sourceServer;
};

// Reconstructs the collection control database from the package manifest.
// This is not a replacement for the T+ application's system administration restore.
class TPlusBackupRestorer
{
public:
    using Cancel = T3BackupRestorer::Cancel;
    using Progress = T3BackupRestorer::Progress;
    static QStringList availableDrivers() { return T3BackupRestorer::availableDrivers(); }
    static bool testConnection(const T3SqlConnection& options, T3ServerInfo* server, QString* error)
    { return T3BackupRestorer::testConnection(options, server, error); }
    static QString defaultLocalStagingDirectory() { return QStringLiteral("D:/Backup"); }
    static QString resolveSource(const QString& path, QString* error);
    static bool readMetadata(const QByteArray& xml, TPlusAccountMetadata* account, QString* error);
    static QString validateSelections(const QVector<T3RestoreSelection>& selections);
    static TPlusBackupAnalysis analyze(const T3SqlConnection& options, const QString& source,
        const QString& stagingDirectory, const Cancel& cancel = {}, const Progress& progress = {},
        bool useRemoteAgent = false);
    static QString controlSchemaSql(const QString& controlDatabase = QStringLiteral("UFTSystem"));
    static QString registrationSql(const T3RestoreSelection& selection, const TPlusAccountMetadata& account,
        bool checkOnly = false, const QString& controlDatabase = QStringLiteral("UFTSystem"));
    static T3RestoreResult restore(const T3SqlConnection& options, const TPlusBackupAnalysis& analysis,
        const QVector<T3RestoreSelection>& selections, const QString& dataDirectory,
        const QString& logDirectory, const Cancel& cancel = {}, const Progress& progress = {},
        const QString& controlDatabase = QStringLiteral("UFTSystem"));
};
