#pragma once

#include "BakBackupRestorer.h"
#include "LocalToolServer.h"
#include "PlatformBackupRestore.h"

// Keeps the latest generic or platform backup catalog and recent jobs in the workbench.
class BakLocalCommands final : public QObject
{
public:
    explicit BakLocalCommands(QObject *parent = nullptr);
    void dispatch(const QJsonObject &request, LocalToolServer::Reply reply);

private:
    void inspect(const QJsonObject &request, LocalToolServer::Reply reply);
    void restore(const QJsonObject &request, LocalToolServer::Reply reply);
    void setBusy(bool busy);

    BakBackupBatch m_catalog;
    PlatformBackupCatalog m_platformCatalog;
    QString m_catalogId;
    QString m_stagingDirectory;
    QVector<QJsonObject> m_jobs;
    bool m_busy = false;
};
