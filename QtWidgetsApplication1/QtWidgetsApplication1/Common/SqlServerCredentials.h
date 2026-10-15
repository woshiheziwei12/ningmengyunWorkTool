#pragma once

#include "T3BackupRestorer.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace SqlServerCredentials {
// Machine-specific credentials stay out of source control and diagnostic output.
inline QJsonObject localConfig()
{
    const QString path = qEnvironmentVariable("LEMON_SQL211_CONFIG",
        QCoreApplication::applicationDirPath() + QStringLiteral("/SqlServer211.local.json"));
    QFile file(path);
    QJsonObject config;
    if (file.open(QIODevice::ReadOnly) && file.size() <= 16384)
        config = QJsonDocument::fromJson(file.readAll()).object();
    return config;
}
inline void apply211(T3SqlConnection& options)
{
    const auto config = localConfig();
    options.user = qEnvironmentVariableIsSet("LEMON_SQL211_USER")
        ? qEnvironmentVariable("LEMON_SQL211_USER")
        : config.value(QStringLiteral("user")).toString(QStringLiteral("sa"));
    options.password = qEnvironmentVariableIsSet("LEMON_SQL211_PASSWORD")
        ? qEnvironmentVariable("LEMON_SQL211_PASSWORD")
        : config.value(QStringLiteral("password")).toString();
}
inline QPair<QString, QString> share211()
{
    const auto config = localConfig();
    return {
        qEnvironmentVariableIsSet("LEMON_SMB211_USER") ? qEnvironmentVariable("LEMON_SMB211_USER")
            : config.value(QStringLiteral("shareUser")).toString(),
        qEnvironmentVariableIsSet("LEMON_SMB211_PASSWORD") ? qEnvironmentVariable("LEMON_SMB211_PASSWORD")
            : config.value(QStringLiteral("sharePassword")).toString()
    };
}
}
