#ifndef LEMONCOFIGHELPER_H
#define LEMONCOFIGHELPER_H

#include <QMap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QCoreApplication>
#include <QDebug>
#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

class lemoncofighelper
{
private:
    QMap<QString, QString> m_mapAccountConfig;

public:
    lemoncofighelper();
    QString GetAccountUrlByKey(QString key);

    static void SetHttpEnvironment(const QString &environment);
    static QString CurrentHttpEnvironment();
    static QString AccountConfigValue(const QString &key, const QString &defaultValue = QString());
    static QString JproApiHost();
    static QString WithTrailingSlash(const QString &url);
    static QString BuildJproCookie(const QString &ningmengCookie, int serviceId,
                                   int userId = 0, const QString &authName = QString());

    // URL key constants for import API host selection
    static const QString kImporterApiHostKey;
    static const QString kProImporterApiHostKey;
    static const QString kAaImporterApiHostKey;
    static const QString kAaProImporterApiHostKey;
    static const QString kImportAccountSetFileAsyncPath;
    static const QString kGetImportResultV2Path;
    static const QString kWbgatePlanUrlKey;
    static const QString kWbgateMenuUrlKey;
    static const QString kWbgateCookieUrlKey;
    static const QString kJproApiHostKey;
    static const QString kWwwOriginKey;
    static const QString kJproOriginKey;
    static const QString kSmsRedirectUrlKey;
    static const QString kRegisterUrlKey;
    static const QString kForgotPasswordUrlKey;
    static const QString kCookieDomainKey;

    // Build a full URL from host + path, returns false if invalid
    static bool TryBuildUrl(const QString &host, const QString &path, QUrl &outUrl, QString &reason);

    QString m_strConfigRoot;
    QString m_strAccountJsonName;

private:
    void InitAccountConfig();
};

#endif // LEMONCOFIGHELPER_H
