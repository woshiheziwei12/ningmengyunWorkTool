#pragma once

#include "T3BackupRestorer.h"
#include <QJsonArray>

// An inspected platform backup bound to one local SQL instance.
struct PlatformBackupCatalog
{
    QString platform;
    QString instance;
    QString error;
    QJsonArray entries;
    std::function<T3RestoreResult(int, const QString &)> restore;
};

// Platform restore adapters share the desktop restore engines and verify the
// account-picker contract with the same SQL instance after restoring.
namespace PlatformBackupRestore {
bool supports(const QString &platform);
PlatformBackupCatalog inspect(const QString &platform, const QString &source, const QString &staging,
                              const T3SqlConnection &connection);
QJsonArray verify(const QString &platform, const T3SqlConnection &connection,
                  const QString &database, QString *error);
}
