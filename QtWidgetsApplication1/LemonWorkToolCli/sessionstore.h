#ifndef SESSIONSTORE_H
#define SESSIONSTORE_H

#include <QJsonObject>
#include <QString>

class CliArguments;

struct SessionInfo
{
    QString profile = QStringLiteral("default");
    QString createdAt;
    QString updatedAt;
    int userId = 0;
    QString authName;
    int serviceId = 0;
    QString appAsId;
    QString ningmengCookie;
    QString jwtToken;
    QString refreshToken;
    int expiresIn = 0;

    bool hasAuth() const;
    QJsonObject toJson(bool revealSecrets) const;
};

class SessionStore
{
public:
    explicit SessionStore(const CliArguments &args);

    QString profile() const;
    QString sessionPath() const;
    bool load(SessionInfo *session, QString *errorMessage) const;
    bool save(const SessionInfo &session, QString *errorMessage) const;
    bool remove(QString *errorMessage) const;
    QJsonObject statusJson() const;

private:
    QString m_profile;
    QString m_sessionPath;
};

#endif // SESSIONSTORE_H
