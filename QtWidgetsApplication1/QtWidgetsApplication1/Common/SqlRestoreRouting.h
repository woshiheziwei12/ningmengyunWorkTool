#include "SqlServerCredentials.h"
#pragma once
#include "LocalSqlInstances.h"
#include "SqlBackupCompatibility.h"
#include "SqlServer211.h"
#include <QDir>

namespace SqlRestoreRouting {
// Retarget existing metadata and the owned staging file without parsing or copying the package.
inline T3BackupAnalysis validateTarget(const T3BackupAnalysis& original, const T3SqlConnection& options,
    const T3BackupRestorer::Cancel& cancel = {},
    const std::function<bool(const T3SqlConnection&, T3ServerInfo*, QString*)>& probe = T3BackupRestorer::testConnection,
    const std::function<bool(const T3SqlConnection&, const QString&, QString*)>& fileExists = T3BackupRestorer::preparedFileExists)
{
    T3BackupAnalysis result = original;
    result.success = false; result.targetServer.clear(); result.server = T3ServerInfo(); result.error.clear();
    result.compatibilityMessage = SqlBackupCompatibility::summary(result.sets);
    const auto fail = [&](const QString& error) { result.error = error; result.compatibilityMessage += "\n" + error; return result; };
    if (cancel && cancel()) return fail(QString::fromUtf8(u8"已取消目标校验，备份清单已保留。"));
    if (options.server.isEmpty()) return fail(QString::fromUtf8(u8"请选择恢复实例，已读取的备份信息保留。"));
    if (result.preparedPath.isEmpty() || result.sets.isEmpty()) return fail(QString::fromUtf8(u8"请先读取备份包。"));
    if (!probe(options, &result.server, &result.error)) return fail(result.error);
    if (LocalSqlInstances::isLocalEndpoint(options.server)) LocalSqlInstances::cacheVersion(options.server, result.server.version);
    const QString versionError = SqlBackupCompatibility::error(result.sets, result.server);
    if (!versionError.isEmpty()) return fail(versionError);
    const QString origin = result.preparedServer.isEmpty() ? original.targetServer : result.preparedServer;
    const bool sameHost = (LocalSqlInstances::isLocalEndpoint(origin) && LocalSqlInstances::isLocalEndpoint(options.server))
        || origin.compare(options.server, Qt::CaseInsensitive) == 0;
    if (!sameHost && !QDir::fromNativeSeparators(result.preparedPath).startsWith("//"))
        return fail(QString::fromUtf8(u8"已暂存文件使用原服务器本地路径，当前实例无法共用。跨本机和 211 恢复需使用双方可访问的 UNC 共享目录；备份清单已保留。"));
    if (cancel && cancel()) return fail(QString::fromUtf8(u8"已取消目标校验，备份清单已保留。"));
    if (!fileExists(options, result.preparedPath, &result.error)) return fail(result.error);
    if (cancel && cancel()) return fail(QString::fromUtf8(u8"已取消目标校验，备份清单已保留。"));
    result.success = true; result.targetServer = options.server;
    result.compatibilityMessage += QString::fromUtf8(u8"\n已选择兼容实例：%1（%2）。已复用备份信息，无需重新读取备份包。").arg(options.server, result.server.version);
    return result;
}
inline QMap<QString, T3ServerInfo> cachedVersions(const QVector<T3SqlConnection>& candidates)
{
    QMap<QString, T3ServerInfo> versions;
    for (const auto& candidate : candidates) {
        T3ServerInfo info;
        info.name = candidate.server;
        if (candidate.server == SqlServer211::endpoint()) info = SqlServer211::version();
        else info.version = LocalSqlInstances::cachedVersion(candidate.server);
        versions.insert(candidate.server, info);
    }
    return versions;
}
inline QVector<T3SqlConnection> candidates(const T3SqlConnection& current, const QStringList& availableEndpoints)
{
    QStringList endpoints;
    if (LocalSqlInstances::isLocalEndpoint(current.server) || current.server == QStringLiteral("192.168.1.211"))
        endpoints.append(current.server);
    for (const auto& server : availableEndpoints)
        if (LocalSqlInstances::isLocalEndpoint(server) && !endpoints.contains(server, Qt::CaseInsensitive)) endpoints.append(server);
    if (availableEndpoints.contains(SqlServer211::endpoint()) && !endpoints.contains(SqlServer211::endpoint())) endpoints.append(SqlServer211::endpoint());
    QVector<T3SqlConnection> result;
    for (const auto& server : endpoints) {
        auto options = current;
        options.server = server;
        options.windowsAuthentication = LocalSqlInstances::isLocalEndpoint(server);
        if (options.windowsAuthentication) { options.user.clear(); options.password.clear(); }
        else SqlServerCredentials::apply211(options);
        result.append(options);
    }
    return result;
}

// Only probes and reads headers/packages. There is deliberately no restore,
// backup or upgrade callback. The user starts the eventual restore separately.
template<class Analysis>
Analysis read(const QVector<T3SqlConnection>& candidates, const QString& source,
    const QMap<QString, QString>& staging, bool originalRemoteAgent,
    const std::function<Analysis(const T3SqlConnection&, const QString&, const QString&,
        const T3BackupRestorer::Cancel&, const T3BackupRestorer::Progress&, bool)>& analyze,
    const T3BackupRestorer::Cancel& cancel, const T3BackupRestorer::Progress& progress,
    const std::function<bool(const T3SqlConnection&, T3ServerInfo*, QString*)>& probe = T3BackupRestorer::testConnection,
    const QMap<QString, T3ServerInfo>& cachedVersions = {})
{
    Analysis result;
    QVector<T3BackupSet> knownSets;
    QStringList failures;
    const auto stopped = [&]() { return cancel && cancel(); };
    const auto report = [&](const QString& text) { if (progress) progress(text); };
    for (int index = 0; index < candidates.size() && !stopped(); ++index) {
        const auto& options = candidates[index];
        T3ServerInfo info; QString error;
        const auto cached = cachedVersions.value(options.server);
        if (!SqlBackupCompatibility::versionMaySupport(knownSets, cached.version)) {
            failures.append(QString::fromUtf8(u8"%1：扫描记录的版本 %2 不兼容，已跳过。").arg(options.server, cached.version));
            continue;
        }
        report(QString::fromUtf8(u8"检查实例 %1 的 SQL Server 版本...").arg(options.server));
        if (options.server == SqlServer211::endpoint() && !cached.version.isEmpty()) info = cached;
        else if (!probe(options, &info, &error)) {
            failures.append(options.server + QStringLiteral(": ") + error); continue;
        }
        else if (LocalSqlInstances::isLocalEndpoint(options.server)) LocalSqlInstances::cacheVersion(options.server, info.version);
        if (stopped()) break;
        if (!knownSets.isEmpty()) {
            error = SqlBackupCompatibility::error(knownSets, info);
            if (!error.isEmpty()) { failures.append(options.server + QStringLiteral(": ") + error); continue; }
        }
        result = analyze(options, source, staging.value(options.server), cancel, progress,
            index == 0 && !options.windowsAuthentication && originalRemoteAgent);
        if (stopped()) break;
        if (!result.sets.isEmpty()) knownSets = result.sets;
        result.compatibilityMessage = SqlBackupCompatibility::summary(knownSets);
        report(result.compatibilityMessage);
        const QString versionError = SqlBackupCompatibility::error(knownSets, info);
        if (result.success && versionError.isEmpty()) {
            result.targetServer = options.server;
            result.preparedServer = options.server;
            result.compatibilityMessage += QString::fromUtf8(u8"\n已选择兼容实例：%1（%2 / %3）。请核对后点击恢复。").arg(
                options.server, SqlBackupCompatibility::versionName(SqlBackupCompatibility::engineVersion(info.version)), info.version);
            report(result.compatibilityMessage);
            return result;
        }
        // A successfully read, compatible header with another error means a
        // package/permission error, not an invitation to try a different target.
        if (!knownSets.isEmpty() && versionError.isEmpty()) {
            result.success = false;
            return result;
        }
        failures.append(options.server + QStringLiteral(": ") + (versionError.isEmpty() ? result.error
            : result.error.isEmpty() ? versionError : result.error));
        result.stagingFile.reset(); result.preparedPath.clear();
    }
    result.success = false;
    result.targetServer.clear(); result.stagingFile.reset(); result.preparedPath.clear();
    result.compatibilityMessage = SqlBackupCompatibility::summary(knownSets);
    result.error = stopped() ? QString::fromUtf8(u8"已取消读取和实例匹配。")
        : QString::fromUtf8(u8"没有找到可读取并直接恢复此备份的兼容实例。%1\n%2")
            .arg(result.compatibilityMessage, failures.join('\n'));
    return result;
}
}
