#ifndef AUTHSERVICE_H
#define AUTHSERVICE_H

#include "sessionstore.h"

#include <QJsonObject>
#include <QString>

struct ServiceResult
{
    bool ok = false;
    QString errorCode;
    QString errorMessage;
    int exitCode = 0;
    QJsonObject details;
    QJsonObject data;
};

class AuthService
{
public:
    explicit AuthService(int timeoutMs = 120000);

    ServiceResult loginPassword(const QString &username, const QString &password, SessionInfo *session) const;
    ServiceResult sendSms(const QString &mobile) const;
    ServiceResult loginSms(const QString &mobile, const QString &code, const QString &stamp,
                           const QString &timestamp, SessionInfo *session) const;
    ServiceResult refreshFromCookie(const QString &ningmengCookie, SessionInfo *session) const;

private:
    ServiceResult signInByAuthCode(const QString &authCode, SessionInfo *session) const;
    ServiceResult fetchJwt(SessionInfo *session) const;
    ServiceResult fetchDefaultPlan(SessionInfo *session) const;
    ServiceResult makeNetworkError(const QString &command, const QString &message, const QJsonObject &details) const;

    int m_timeoutMs;
};

#endif // AUTHSERVICE_H
