#pragma once
#include "T3BackupRestorer.h"

class KisProBackupRestorer
{
public:
    using Cancel = T3BackupRestorer::Cancel;
    using Progress = T3BackupRestorer::Progress;
    static QStringList availableDrivers() { return T3BackupRestorer::availableDrivers(); }
    static bool testConnection(const T3SqlConnection& options, T3ServerInfo* server, QString* error)
    { return T3BackupRestorer::testConnection(options, server, error); }
    static QString defaultLocalStagingDirectory() { return QStringLiteral("D:/Backup"); }
    static QString resolveSource(const QString& path, QString* error);
    static bool isSupportedHeader(const QByteArray& header);
    static QString validateSelections(const QVector<T3RestoreSelection>& selections);
    static T3BackupAnalysis analyze(const T3SqlConnection& options, const QString& source,
        const QString& stagingDirectory, const Cancel& cancel = {}, const Progress& progress = {},
        bool useRemoteAgent = false);
    static QString controlSchemaSql(const QString& controlDatabase = QStringLiteral("AcctCtl"));
    static QString registrationSql(const T3RestoreSelection& selection, const QString& accountName,
        bool checkOnly = false, const QString& controlDatabase = QStringLiteral("AcctCtl"));
    static T3RestoreResult restore(const T3SqlConnection& options, const T3BackupAnalysis& analysis,
        const QVector<T3RestoreSelection>& selections, const QString& dataDirectory,
        const QString& logDirectory, const Cancel& cancel = {}, const Progress& progress = {},
        const QString& controlDatabase = QStringLiteral("AcctCtl"));
};
