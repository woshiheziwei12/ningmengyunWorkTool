#pragma once
#include "T3BackupRestorer.h"

namespace SqlServer211 {
// Queried read-only on 2026-09-30: SQL Server 2008 R2 Express Edition.
// This is an intentional fixed deployment configuration, not a discovered local instance.
inline QString endpoint() { return QStringLiteral("192.168.1.211"); }
inline T3ServerInfo version()
{
    T3ServerInfo info;
    info.name = endpoint();
    info.version = QStringLiteral("10.50.4000.0");
    info.databaseVersion = 661;
    return info;
}
}
