#pragma once
#include "T3BackupRestorer.h"

struct InspurAccountMetadata
{
    QString databaseName;
    QString accountName;
    QString owner;
};

struct InspurBackupAnalysis : T3BackupAnalysis
{
    InspurAccountMetadata account;
};

class InspurBackupRestorer
{
public:
    using Cancel = T3BackupRestorer::Cancel;
    using Progress = T3BackupRestorer::Progress;
    static QStringList availableDrivers() { return T3BackupRestorer::availableDrivers(); }
    static bool testConnection(const T3SqlConnection& options, T3ServerInfo* server, QString* error)
    { return T3BackupRestorer::testConnection(options, server, error); }
    static QString defaultLocalStagingDirectory() { return QStringLiteral("D:/Backup"); }
    static QString resolveSource(const QString& path, QString* error);
    static bool readMetadata(const QByteArray& ini, InspurAccountMetadata* account, QString* error);
    // Extract only the matching BAK/INI pair to fixed filenames in a fresh directory.
    static bool unpack(const QString& source, const QString& directory, QString* error,
        const Cancel& cancel = {});
    static QString validateSelections(const QVector<T3RestoreSelection>& selections);
    static InspurBackupAnalysis analyze(const T3SqlConnection& options, const QString& source,
        const QString& stagingDirectory, const Cancel& cancel = {}, const Progress& progress = {},
        bool useRemoteAgent = false);
    static QString controlSchemaSql(const QString& controlDatabase = QStringLiteral("cwmaster"));
    static QString registrationSql(const T3RestoreSelection& selection, const InspurAccountMetadata& account,
        bool checkOnly = false, const QString& controlDatabase = QStringLiteral("cwmaster"));
    static T3RestoreResult restore(const T3SqlConnection& options, const InspurBackupAnalysis& analysis,
        const QVector<T3RestoreSelection>& selections, const QString& dataDirectory,
        const QString& logDirectory, const Cancel& cancel = {}, const Progress& progress = {},
        const QString& controlDatabase = QStringLiteral("cwmaster"));
};
