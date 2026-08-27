#ifndef ACCOUNTSETSERVICE_H
#define ACCOUNTSETSERVICE_H

#include "authservice.h"
#include "sessionstore.h"

#include <QList>
#include <QString>

class AccountSetService
{
public:
    explicit AccountSetService(int timeoutMs = 120000);

    ServiceResult listPlans(const SessionInfo &session) const;
    ServiceResult listAccountSets(const SessionInfo &session) const;
    ServiceResult deleteAccountSets(const SessionInfo &session, const QList<int> &asIds) const;

private:
    QString buildCookieString(const SessionInfo &session, int serviceId) const;
    int m_timeoutMs;
};

#endif // ACCOUNTSETSERVICE_H
