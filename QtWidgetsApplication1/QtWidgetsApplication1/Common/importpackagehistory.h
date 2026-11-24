#ifndef IMPORTPACKAGEHISTORY_H
#define IMPORTPACKAGEHISTORY_H

#include <QString>
#include <QJsonArray>
#include <QJsonObject>

class ImportPackageHistory
{
public:
    static QString historyFilePath();
    static bool recordSuccessfulUpload(const QString &zipPath,
                                       const QString &importId,
                                       int serviceId,
                                       const QString &importerApiHost,
                                       const QString &environment = {},
                                       int userId = 0);
    // Finds a task uploaded under the requested environment, account and plan.
    static QJsonObject scopedTask(const QString &importId, const QString &environment, int userId, int serviceId);
    // Lists accepted tasks for the active scope, including this session's unsaved tasks.
    static QJsonArray scopedTasks(const QString &environment, int userId, int serviceId);
    static void migrateCurrentApplicationHistory();
};

#endif // IMPORTPACKAGEHISTORY_H
