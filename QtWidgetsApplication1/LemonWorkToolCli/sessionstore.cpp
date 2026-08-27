#include "sessionstore.h"

#include "cliarguments.h"
#include "masking.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace
{
QString defaultSessionPath(const QString &profile)
{
    const QString dir = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/config/sessions");
    return QDir(dir).absoluteFilePath(profile + ".json");
}
}

bool SessionInfo::hasAuth() const
{
    return !ningmengCookie.isEmpty();
}

QJsonObject SessionInfo::toJson(bool revealSecrets) const
{
    QJsonObject obj;
    obj["profile"] = profile;
    obj["createdAt"] = createdAt;
    obj["updatedAt"] = updatedAt;
    obj["userId"] = userId;
    obj["authName"] = authName;
    obj["serviceId"] = serviceId;
    obj["appAsId"] = appAsId;
    obj["ningmengCookie"] = revealSecrets ? ningmengCookie : maskSecret(ningmengCookie);
    obj["jwtToken"] = revealSecrets ? jwtToken : maskSecret(jwtToken);
    obj["refreshToken"] = revealSecrets ? refreshToken : maskSecret(refreshToken);
    obj["expiresIn"] = expiresIn;
    obj["hasNingmengCookie"] = !ningmengCookie.isEmpty();
    obj["hasJwtToken"] = !jwtToken.isEmpty();
    return obj;
}

SessionStore::SessionStore(const CliArguments &args)
{
    m_profile = args.value("profile", QString::fromLocal8Bit(qgetenv("LEMON_PROFILE")));
    if (m_profile.isEmpty())
        m_profile = QStringLiteral("default");
    m_sessionPath = args.value("session");
    if (m_sessionPath.isEmpty())
        m_sessionPath = defaultSessionPath(m_profile);
}

QString SessionStore::profile() const
{
    return m_profile;
}

QString SessionStore::sessionPath() const
{
    return m_sessionPath;
}

bool SessionStore::load(SessionInfo *session, QString *errorMessage) const
{
    if (!session)
        return false;

    QFile file(m_sessionPath);
    if (!file.exists())
    {
        session->profile = m_profile;
        return true;
    }

    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("打开 session 失败：") + file.errorString();
        return false;
    }

    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("session JSON 解析失败：") + jsonError.errorString();
        return false;
    }

    const QJsonObject obj = doc.object();
    session->profile = obj.value("profile").toString(m_profile);
    session->createdAt = obj.value("createdAt").toString();
    session->updatedAt = obj.value("updatedAt").toString();
    session->userId = obj.value("userId").toInt();
    session->authName = obj.value("authName").toString();
    session->serviceId = obj.value("serviceId").toInt();
    session->appAsId = obj.value("appAsId").toString();
    session->ningmengCookie = obj.value("ningmengCookie").toString();
    session->jwtToken = obj.value("jwtToken").toString();
    session->refreshToken = obj.value("refreshToken").toString();
    session->expiresIn = obj.value("expiresIn").toInt();
    return true;
}

bool SessionStore::save(const SessionInfo &session, QString *errorMessage) const
{
    QDir dir(QFileInfo(m_sessionPath).absolutePath());
    if (!dir.exists() && !dir.mkpath("."))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("创建 session 目录失败：") + dir.absolutePath();
        return false;
    }

    SessionInfo copy = session;
    copy.profile = m_profile;
    if (copy.createdAt.isEmpty())
        copy.createdAt = copy.updatedAt;

    QSaveFile file(m_sessionPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("写入 session 失败：") + file.errorString();
        return false;
    }
    file.write(QJsonDocument(copy.toJson(true)).toJson(QJsonDocument::Indented));
    if (!file.commit())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("保存 session 失败：") + file.errorString();
        return false;
    }
    return true;
}

bool SessionStore::remove(QString *errorMessage) const
{
    QFile file(m_sessionPath);
    if (!file.exists())
        return true;
    if (file.remove())
        return true;
    if (errorMessage)
        *errorMessage = QStringLiteral("删除 session 失败：") + file.errorString();
    return false;
}

QJsonObject SessionStore::statusJson() const
{
    QJsonObject obj;
    obj["profile"] = m_profile;
    obj["sessionPath"] = QDir::toNativeSeparators(m_sessionPath);
    obj["exists"] = QFile::exists(m_sessionPath);
    return obj;
}
