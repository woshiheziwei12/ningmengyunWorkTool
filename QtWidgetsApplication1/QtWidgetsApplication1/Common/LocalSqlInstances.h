#pragma once
#include <QSettings>
#include <QStringList>
#include <QRegularExpression>
#include <QDateTime>
#include <QMap>

namespace LocalSqlInstances {
inline QString cacheKey(const QString& server)
{ return QStringLiteral("SqlInstances/versions/") + QString::fromLatin1(server.toCaseFolded().toUtf8().toHex()); }
inline QString cachedVersion(const QString& server)
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "LemonCloud", "LemonWorkTool");
    return settings.value(cacheKey(server) + QStringLiteral("/version")).toString();
}
inline void cacheVersion(const QString& server, const QString& version)
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "LemonCloud", "LemonWorkTool");
    settings.setValue(cacheKey(server) + QStringLiteral("/version"), version);
    settings.setValue(cacheKey(server) + QStringLiteral("/scannedAt"), QDateTime::currentDateTimeUtc());
}
inline QString registryEngineVersion(const QString& current, const QString& setup)
{
    // CurrentVersion describes the engine. Setup may report a package version
    // such as 10.52 while the SQL 2008 R2 engine still reports 10.50.
    const QRegularExpression valid(QStringLiteral("^[0-9]+\\.[0-9]+(?:\\.[0-9]+){0,2}$"));
    if (valid.match(current).hasMatch()) return current;
    return valid.match(setup).hasMatch() ? setup : QString();
}
// Only canonical local endpoints are accepted; never interpret a missing value
// as another instance when reconnecting for a destructive operation.
inline bool isLocalEndpoint(const QString& server)
{
    return server == QStringLiteral(".") || (server.startsWith(QStringLiteral(".\\"))
        && QRegularExpression(QStringLiteral("^[\\p{L}\\p{N}_$#-]+$")).match(server.mid(2)).hasMatch());
}

inline QStringList endpoints(const QStringList& instanceNames)
{
    QStringList result;
    for (const auto& name : instanceNames) {
        const QString server = name.compare(QStringLiteral("MSSQLSERVER"), Qt::CaseInsensitive) == 0
            ? QStringLiteral(".") : QStringLiteral(".\\") + name;
        if (isLocalEndpoint(server) && !result.contains(server, Qt::CaseInsensitive)) result.append(server);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

inline QStringList namesFromServices(const QStringList& services)
{
    QStringList names;
    for (const auto& service : services) {
        if (service.compare(QStringLiteral("MSSQLSERVER"), Qt::CaseInsensitive) == 0)
            names.append(QStringLiteral("MSSQLSERVER"));
        else if (service.startsWith(QStringLiteral("MSSQL$"), Qt::CaseInsensitive)
            && service.size() > 6)
            names.append(service.mid(6));
    }
    return names;
}

inline QStringList discover()
{
    QStringList names;
    QMap<QString, QString> versions;
#ifdef Q_OS_WIN
    // The application is 32-bit; SQL Server may be registered in either view.
    for (auto format : {QSettings::Registry64Format, QSettings::Registry32Format}) {
        QSettings instances(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Microsoft SQL Server\\Instance Names\\SQL"), format);
        names.append(instances.childKeys());
        for (const auto& name : instances.childKeys()) {
            const QString instanceId = instances.value(name).toString();
            const QString base = QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Microsoft SQL Server\\") + instanceId;
            QSettings current(base + QStringLiteral("\\MSSQLServer\\CurrentVersion"), format);
            QSettings setup(base + QStringLiteral("\\Setup"), format);
            const QString version = registryEngineVersion(current.value(QStringLiteral("CurrentVersion")).toString(),
                setup.value(QStringLiteral("PatchLevel"), setup.value(QStringLiteral("Version"))).toString());
            if (!version.isEmpty() && !versions.contains(name.toCaseFolded())) versions.insert(name.toCaseFolded(), version);
        }
        QSettings installed(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Microsoft SQL Server"), format);
        names.append(installed.value(QStringLiteral("InstalledInstances")).toStringList());
    }
    QSettings services(QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services"), QSettings::NativeFormat);
    names.append(namesFromServices(services.childGroups()));
#endif
    const auto result = endpoints(names);
    for (const auto& server : result)
        cacheVersion(server, versions.value(server == QStringLiteral(".") ? QStringLiteral("mssqlserver") : server.mid(2).toCaseFolded()));
    return result;
}

inline QString label(const QString& server)
{
    return server == QStringLiteral(".") ? QString::fromUtf8(u8"本地默认实例（.）")
        : QString::fromUtf8(u8"本地实例（%1）").arg(server);
}
}
