#pragma once
#include "T3BackupRestorer.h"

// Use the backup engine version, not the database compatibility-level setting.
// Sources: Microsoft RESTORE HEADERONLY and RESTORE compatibility documentation.
namespace SqlBackupCompatibility {
inline int engineVersion(const QString& version)
{
    bool ok = false;
    const int major = version.section('.', 0, 0).toInt(&ok);
    if (!ok) return 0;
    return major * 100 + (major == 10 && version.section('.', 1, 1).toInt() >= 50 ? 50 : 0);
}
inline int sourceVersion(const T3BackupSet& set)
{
    if (set.softwareVersionMajor >= 8 && set.softwareVersionMajor <= 17)
        return set.softwareVersionMajor * 100 + (set.softwareVersionMajor == 10 && set.softwareVersionMinor >= 50 ? 50 : 0);
    // SQL 2000 headers read on newer engines can omit software version fields.
    return set.softwareVersionMajor == 0 && set.databaseVersion == 539 ? 800 : 0;
}
inline QString versionName(int version)
{
    if (version == 800) return QStringLiteral("SQL Server 2000");
    if (version == 900) return QStringLiteral("SQL Server 2005");
    if (version == 1000) return QStringLiteral("SQL Server 2008");
    if (version == 1050) return QStringLiteral("SQL Server 2008 R2");
    const QMap<int, QString> years{{1100,"2012"},{1200,"2014"},{1300,"2016"},
        {1400,"2017"},{1500,"2019"},{1600,"2022"},{1700,"2025"}};
    return years.contains(version) ? QStringLiteral("SQL Server ") + years.value(version)
        : QString::fromUtf8(u8"未知 SQL Server 版本（%1）").arg(version);
}
inline bool known(const T3BackupSet& set)
{
    const int version = sourceVersion(set);
    return set.databaseVersion > 0 && (version == 800 || version == 900 || version == 1000
        || version == 1050 || (version >= 1100 && version <= 1700 && version % 100 == 0));
}
inline QString summary(const QVector<T3BackupSet>& sets)
{
    if (sets.isEmpty()) return QString::fromUtf8(u8"尚未读取到备份版本，无法判断兼容实例。");
    QStringList descriptions;
    int minimum = 900, maximum = 1700;
    for (const auto& set : sets) {
        if (!known(set)) return QString::fromUtf8(u8"备份版本无法可靠识别（内部版本 %1），请使用可读取该备份的实例重新读取；不会猜测版本或中转。").arg(set.databaseVersion);
        const int source = sourceVersion(set);
        const QString text = versionName(source) + QString::fromUtf8(u8"（内部版本 %1）").arg(set.databaseVersion);
        if (!descriptions.contains(text)) descriptions.append(text);
        minimum = qMax(minimum, source);
        if (source == 800 || set.databaseVersion == 539) maximum = qMin(maximum, 1050);
    }
    QStringList legacyTargets;
    for (int version : {900, 1000, 1050})
        if (version >= minimum && version <= maximum) legacyTargets.append(versionName(version));
    const QString range = minimum > maximum ? QString::fromUtf8(u8"没有可同时直接恢复全部备份集的版本，请拆分处理")
        : maximum == 1050 ? legacyTargets.join(QString::fromUtf8(u8"、"))
        : versionName(minimum) + QString::fromUtf8(u8" 或更高版本（当前识别至 SQL Server 2025）");
    return QString::fromUtf8(u8"备份来源：%1。建议使用：%2。直接恢复，不进行中转；仍需通过文件、权限和恢复校验。")
        .arg(descriptions.join(QStringLiteral(" / ")), range);
}
inline QString error(const QVector<T3BackupSet>& sets, const T3ServerInfo& server)
{
    const int target = engineVersion(server.version);
    if (target < 900 || target > 1700 || server.databaseVersion <= 0)
        return QString::fromUtf8(u8"无法确认实例版本，或实例不在本工具支持范围（SQL Server 2005–2025）。");
    if (sets.isEmpty()) return summary(sets);
    for (const auto& set : sets) {
        if (!known(set)) return summary(sets);
        if (target < sourceVersion(set) || set.databaseVersion > server.databaseVersion
            || ((sourceVersion(set) == 800 || set.databaseVersion == 539) && target >= 1100))
            return QString::fromUtf8(u8"当前实例 %1（%2）不能直接恢复此备份。%3")
                .arg(server.name, versionName(target), summary(sets));
    }
    return {};
}

inline bool versionMaySupport(const QVector<T3BackupSet>& sets, const QString& version)
{
    if (sets.isEmpty() || version.isEmpty()) return true; // Need a live probe/header.
    const int target = engineVersion(version);
    if (target < 900 || target > 1700) return false;
    for (const auto& set : sets)
        if (!known(set) || target < sourceVersion(set)
            || ((sourceVersion(set) == 800 || set.databaseVersion == 539) && target >= 1100)) return false;
    return true;
}
}
