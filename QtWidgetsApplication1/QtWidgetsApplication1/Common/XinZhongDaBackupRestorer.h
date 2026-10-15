#pragma once
#include "T3BackupRestorer.h"
#include <QByteArray>

struct XinZhongDaTable {
    QByteArray compressed;
    QVector<QStringList> rows;
    int columns = 0;
};
struct XinZhongDaBackupAnalysis {
    bool success = false;
    QString error;
    QString directory;
    QString accountName;
    QString accountCode;
    int year = 0;
    bool nameFromBackup = false;
    QByteArray metadata;
    QByteArray fingerprint;
    QMap<QString, XinZhongDaTable> tables;
    qint64 voucherRows = 0;
    qint64 approvedVoucherRows = 0;
    QString lastVoucherDate;
};
struct XinZhongDaRestoreResult {
    bool success = false;
    bool cancelled = false;
    QString database;
    QStringList databases;
    QString error;
};
class XinZhongDaBackupRestorer {
public:
    using Cancel = T3BackupRestorer::Cancel;
    using Progress = T3BackupRestorer::Progress;
    static QString resolveDirectory(const QString& path, QString* error);
    static bool decode(const QByteArray& input, QByteArray* output, QString* error, const Cancel& cancel = {});
    static bool readMetadata(const QByteArray& bytes, const QString& directoryName, XinZhongDaBackupAnalysis* result);
    static XinZhongDaBackupAnalysis analyze(
        const QString& path, const Cancel& cancel = {}, const Progress& progress = {});
    // Recursive catalog: each item is one annual snapshot, never merged with
    // another snapshot. Table payloads are released after cataloguing.
    static QVector<XinZhongDaBackupAnalysis> discover(
        const QString& path, QString* error, const Cancel& cancel = {}, const Progress& progress = {});
    static QString targetDatabase(const QString& accountCode, int year);
    static QString retainDroppedBackup(const QString& path, QString* error);
    static XinZhongDaRestoreResult restore(const T3SqlConnection& options, const XinZhongDaBackupAnalysis& analysis,
        const QString& accountCode, const Cancel& cancel = {}, const Progress& progress = {},
        const QString& controlDatabase = QStringLiteral("PUBDATA"));
    static XinZhongDaRestoreResult restoreMany(const T3SqlConnection& options,
        const QVector<XinZhongDaBackupAnalysis>& analyses, const QString& accountCode, const Cancel& cancel = {},
        const Progress& progress = {}, const QString& controlDatabase = QStringLiteral("PUBDATA"));
};
