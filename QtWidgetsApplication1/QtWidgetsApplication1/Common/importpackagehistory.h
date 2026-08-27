#ifndef IMPORTPACKAGEHISTORY_H
#define IMPORTPACKAGEHISTORY_H

#include <QString>

class ImportPackageHistory
{
public:
    static QString historyFilePath();
    static void recordSuccessfulUpload(const QString &zipPath,
                                       const QString &importId,
                                       int serviceId,
                                       const QString &importerApiHost = QString());
    static void migrateCurrentApplicationHistory();
};

#endif // IMPORTPACKAGEHISTORY_H
