#include "lemoncofighelper.h"

namespace
{
QString &httpEnvironmentOverride()
{
    static QString value;
    return value;
}

QString normalizedEnvironment(QString environment)
{
    environment = environment.trimmed();
    if (environment.isEmpty())
        return QString();

    const QString lower = environment.toLower();
    if (lower == "pro" || lower == "prod" || lower == "production")
        return QStringLiteral("Pro");
    if (lower == "dev")
        return QStringLiteral("Dev");
    if (lower == "dev2" || lower == "test" || lower == "testing")
        return QStringLiteral("Dev2");
    if (lower == "local" || lower == "localhost")
        return QStringLiteral("Local");

    return environment;
}

QString argumentHttpEnvironment()
{
    if (!QCoreApplication::instance())
        return QString();

    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i)
    {
        const QString token = args.at(i);
        if ((token == "--http-env" || token == "--env") && i + 1 < args.size())
            return args.at(i + 1);
        if (token.startsWith("--http-env="))
            return token.mid(QStringLiteral("--http-env=").size());
        if (token.startsWith("--env="))
            return token.mid(QStringLiteral("--env=").size());
    }

    return QString();
}

QString configuredEnvironment()
{
    if (!httpEnvironmentOverride().trimmed().isEmpty())
        return httpEnvironmentOverride();

    const QString argEnv = argumentHttpEnvironment();
    if (!argEnv.trimmed().isEmpty())
        return argEnv;

    const QString envVar = QString::fromLocal8Bit(qgetenv("LEMON_HTTP_ENV"));
    if (!envVar.trimmed().isEmpty())
        return envVar;

    return QStringLiteral("Pro");
}

QString resourceRootForEnvironment(const QString &environment)
{
    return QStringLiteral(":/Resource/http/") + environment + QStringLiteral("/");
}

QString externalRootForEnvironment(const QString &environment)
{
    if (!QCoreApplication::instance())
        return QString();

    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("Resource/http/") + environment) + QStringLiteral("/");
}

bool isLocalOrTestEnvironment(const QString &environment)
{
    return environment == QStringLiteral("Local")
        || environment == QStringLiteral("Dev")
        || environment == QStringLiteral("Dev2");
}

bool isProductionJproApiHost(const QString &host)
{
    const QUrl url(host);
    return url.host().compare(QStringLiteral("jproapi.ningmengyun.com"), Qt::CaseInsensitive) == 0;
}

bool isTestJproApiHost(const QString &host)
{
    const QUrl url(host);
    return url.host().compare(QStringLiteral("jproapitest2.ningmengyun.com"), Qt::CaseInsensitive) == 0;
}
}

const QString lemoncofighelper::kImporterApiHostKey = "importerApiHost";
const QString lemoncofighelper::kProImporterApiHostKey = "proImporterApiHost";
const QString lemoncofighelper::kAaImporterApiHostKey = "aaImporterApiHost";
const QString lemoncofighelper::kAaProImporterApiHostKey = "aaProImporterApiHost";
const QString lemoncofighelper::kImportAccountSetFileAsyncPath = "/api/AccImport/ImportAccountSetFileAsyncV2";
const QString lemoncofighelper::kGetImportResultV2Path = "/api/AccImport/GetImportResultV2";
const QString lemoncofighelper::kWbgatePlanUrlKey = "wbgatePlanUrl";
const QString lemoncofighelper::kWbgateMenuUrlKey = "wbgateMenuUrl";
const QString lemoncofighelper::kWbgateCookieUrlKey = "wbgateCookieUrl";
const QString lemoncofighelper::kJproApiHostKey = "jproApiHost";
const QString lemoncofighelper::kWwwOriginKey = "wwwOrigin";
const QString lemoncofighelper::kJproOriginKey = "jproOrigin";
const QString lemoncofighelper::kSmsRedirectUrlKey = "smsRedirectUrl";
const QString lemoncofighelper::kRegisterUrlKey = "registerUrl";
const QString lemoncofighelper::kForgotPasswordUrlKey = "forgotPasswordUrl";
const QString lemoncofighelper::kCookieDomainKey = "cookieDomain";

bool lemoncofighelper::TryBuildUrl(const QString &host, const QString &path, QUrl &outUrl, QString &reason)
{
    if (host.isEmpty()) {
        reason = "host is empty";
        return false;
    }
    QUrl baseUrl(host);
    if (!baseUrl.isValid() || baseUrl.scheme().isEmpty()) {
        reason = "invalid host format";
        return false;
    }
    QString scheme = baseUrl.scheme().toLower();
    if (scheme != "http" && scheme != "https") {
        reason = "scheme not allowed";
        return false;
    }
    QUrl resolvedUrl = baseUrl.resolved(QUrl(path));
    if (!resolvedUrl.isValid() || resolvedUrl.scheme().isEmpty()) {
        reason = "resolved url invalid";
        return false;
    }
    outUrl = resolvedUrl;
    return true;
}

lemoncofighelper::lemoncofighelper()
{
    m_strAccountJsonName = "Account.json";

    const QString configFile = QString::fromLocal8Bit(qgetenv("LEMON_HTTP_CONFIG_FILE")).trimmed();
    if (!configFile.isEmpty() && QFile::exists(configFile))
    {
        QFileInfo info(configFile);
        m_strConfigRoot = info.absolutePath() + QStringLiteral("/");
        m_strAccountJsonName = info.fileName();
    }
    else
    {
        const QString environment = CurrentHttpEnvironment();
        const QString externalRoot = externalRootForEnvironment(environment);
        if (!externalRoot.isEmpty() && QFile::exists(externalRoot + m_strAccountJsonName))
            m_strConfigRoot = externalRoot;
        else
            m_strConfigRoot = resourceRootForEnvironment(environment);
    }

    InitAccountConfig();
}

QString lemoncofighelper::GetAccountUrlByKey(QString key)
{
    if (m_mapAccountConfig.keys().contains(key))
    {
        return m_mapAccountConfig[key];
    }
    return "";
}

void lemoncofighelper::SetHttpEnvironment(const QString &environment)
{
    httpEnvironmentOverride() = normalizedEnvironment(environment);
}

QString lemoncofighelper::CurrentHttpEnvironment()
{
    const QString environment = normalizedEnvironment(configuredEnvironment());
    return environment.isEmpty() ? QStringLiteral("Pro") : environment;
}

QString lemoncofighelper::AccountConfigValue(const QString &key, const QString &defaultValue)
{
    lemoncofighelper configHelper;
    const QString value = configHelper.GetAccountUrlByKey(key).trimmed();
    return value.isEmpty() ? defaultValue : value;
}

QString lemoncofighelper::JproApiHost()
{
    lemoncofighelper configHelper;
    const QString host = configHelper.GetAccountUrlByKey(kJproApiHostKey).trimmed();
    if (isLocalOrTestEnvironment(CurrentHttpEnvironment())
        && (host.isEmpty() || isProductionJproApiHost(host) || isTestJproApiHost(host)))
    {
        return QStringLiteral("https://jproapitest2.ningmengyun.com");
    }
    return host;
}

QString lemoncofighelper::WithTrailingSlash(const QString &url)
{
    if (url.isEmpty() || url.endsWith('/'))
        return url;
    return url + QStringLiteral("/");
}

QString lemoncofighelper::BuildJproCookie(const QString &ningmengCookie, int serviceId,
                                          int userId, const QString &authName)
{
    QStringList cookies;
    if (userId != 0)
        cookies.append(QStringLiteral("auth=") + QString::number(userId));

    const QString trimmedAuthName = authName.trimmed();
    if (!trimmedAuthName.isEmpty())
        cookies.append(QStringLiteral("authname=") + QString::fromUtf8(QUrl::toPercentEncoding(trimmedAuthName)));

    if (!ningmengCookie.trimmed().isEmpty())
        cookies.append(QStringLiteral("ningmengcookie=") + ningmengCookie.trimmed());

    if (serviceId != 0)
        cookies.append(QStringLiteral("service-id=") + QString::number(serviceId));

    return cookies.join(QStringLiteral("; "));
}

void lemoncofighelper::InitAccountConfig()
{
    QByteArray strJsonData = "";
    QString filep = m_strConfigRoot + m_strAccountJsonName;
    qDebug() << filep;
    QFile file(filep);
    if (file.open(QFile::ReadOnly)) {
        strJsonData = file.readAll();
        file.close();
    }
    QString jsonData(strJsonData);
    jsonData = jsonData.remove("\r").remove("\n").trimmed();

    QJsonParseError jsonError;
    QJsonDocument document = QJsonDocument::fromJson(jsonData.toUtf8(), &jsonError);
    if (jsonError.error != QJsonParseError::NoError) {
        qDebug() << QString("resolve Json failed");
    }

    if (document.isObject()) {
        QJsonObject rootObj = document.object();
        QStringList keys = rootObj.keys();
        int keySize = keys.size();
        if (keySize > 0)
        {
            for (int i = 0; i < keySize; i++)
            {
                QString key = keys.at(i);
                QString value = rootObj.value(key).toString();
                m_mapAccountConfig[key] = value;
            }
        }
    }
}
