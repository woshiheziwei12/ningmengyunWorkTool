#pragma once
#include <QString>
#include <QStringList>

struct Jet3RecoveryResult
{
    bool success = false;
    int tables = 0;
    int rows = 0;
    int indexes = 0;
    QString reportPath;
    QStringList warnings;
    QString error;
};

// All recovery work runs in a supervised child process. Neither a timeout nor
// a failed validation publishes an unverified database at destinationPath.
Jet3RecoveryResult recoverJet3Data(const QString& sourcePath, const QString& destinationPath,
                                  int timeoutMilliseconds = 180000);
bool isJet3RecoveryAvailable();
