#ifndef BACKUPSERVICE_H
#define BACKUPSERVICE_H

#include "authservice.h"
#include "sessionstore.h"

#include <QList>
#include <QString>

class BackupService
{
public:
    explicit BackupService(int timeoutMs = 120000);

    ServiceResult backupAccountSets(const SessionInfo &session, const QList<int> &asIds,
                                    const QString &outputDir, int pollIntervalMs = 3000,
                                    int maxWaitMs = 600000) const;

private:
    QString buildCookieString(const SessionInfo &session, int serviceId) const;
    int m_timeoutMs;
};

#endif // BACKUPSERVICE_H
