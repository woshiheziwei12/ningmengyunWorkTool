#pragma once
#include <QString>
#include <QStringList>
#include <QMap>
#include <QVector>
#include <functional>
#include <memory>

struct T3SqlConnection
{
    QString server = QStringLiteral(".");
    bool windowsAuthentication = true;
    QString user;
    QString password;
    QString driver = QStringLiteral("ODBC Driver 17 for SQL Server");
    bool trustServerCertificate = true;
};
struct T3BackupSet
{
    int position = 0;
    QString databaseName;
    QString backupDate;
    QString backupSetId;
    int databaseVersion = 0;
    int softwareVersionMajor = 0;
    int softwareVersionMinor = 0;
};
struct T3ServerInfo
{
    QString name;
    QString version;
    QString backupDirectory;
    QString dataDirectory;
    QString logDirectory;
    int databaseVersion = 0;
};
struct T3BackupAnalysis
{
    bool success = false;
    QString error;
    QString preparedPath;
    std::shared_ptr<QString> stagingFile;
    T3ServerInfo server;
    QVector<T3BackupSet> sets;
    QString targetServer;
    QString preparedServer; // Original host for a server-local preparedPath; unchanged on target switches.
    QString compatibilityMessage;
};
struct T3RestoreSelection
{
    T3BackupSet backup;
    QString targetDatabase;
    QString accountName;
    QString tradeKind;
};
struct T3RestoreFile
{
    QString logicalName;
    QString type;
    int fileId = 0;
};
struct T3RestoreItem
{
    QString database;
    bool databaseRestored = false;
    bool success = false;
    QString error;
};
struct T3RestoreResult
{
    QVector<T3RestoreItem> items;
    QString error;
    bool stopped = false;
};

class T3BackupRestorer
{
public:
    using Cancel = std::function<bool()>;
    using Progress = std::function<void(const QString&)>;
    static QStringList availableDrivers();
    static QString connectionString(const T3SqlConnection& options);
    static bool isLocalServer(const QString& server);
    static bool connectConfiguredShare(const T3SqlConnection& options, const QString& directory, QString* error);
    static QString resolveSource(const QString& path, QString* error);
    // Owns only the newly created staging file. Caller removes it after SQL finishes.
    static QString prepareFile(const QString& source, const QString& directory,
        QString* error, const Cancel& cancel = {}, quint64 outputLimit = 64ULL * 1024 * 1024 * 1024);
    static bool testConnection(const T3SqlConnection& options, T3ServerInfo* server, QString* error);
    static bool preparedFileExists(const T3SqlConnection& options, const QString& path, QString* error);
    static T3BackupAnalysis analyze(const T3SqlConnection& options, const QString& source,
        const QString& stagingDirectory, const Cancel& cancel = {}, const Progress& progress = {},
        bool useRemoteAgent = false);
    static QString quoteLiteral(const QString& value);
    static QString quoteIdentifier(const QString& value);
    static QString validateSelections(const QVector<T3RestoreSelection>& selections);
    static QString validateCollectionSelections(const QVector<T3RestoreSelection>& selections);
    // SQL generators also allow an isolated control database in integration tests.
    static QString controlDatabaseSql(const QString& dataDirectory, const QString& logDirectory,
        const QString& fileToken, const QString& controlDatabase = QStringLiteral("UFSystem"));
    static QString controlSchemaSql(const QString& controlDatabase = QStringLiteral("UFSystem"));
    static QString accountMetadataSql(const T3RestoreSelection& selection, bool extended = false);
    static QString registerAccountSql(const T3RestoreSelection& selection, bool checkOnly = false,
        const QString& controlDatabase = QStringLiteral("UFSystem"));
    static QString restoreSql(const QString& backupPath, const T3RestoreSelection& selection,
        const QVector<T3RestoreFile>& files, const QString& dataDirectory,
        const QString& logDirectory, const QString& fileToken);
    static T3RestoreResult restore(const T3SqlConnection& options, const T3BackupAnalysis& analysis,
        const QVector<T3RestoreSelection>& selections, const QString& dataDirectory,
        const QString& logDirectory, const Cancel& cancel = {}, const Progress& progress = {});
};
