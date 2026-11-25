#ifndef IMPORTPACKAGEUPLOADER_H
#define IMPORTPACKAGEUPLOADER_H

#include <QString>
#include <QStringList>
#include <QtGlobal>

enum class ImportPackageServiceType
{
    AutoByServiceId,
    Standard,
    Professional,
    Aa,
    AaProfessional
};

struct ImportPackageUploadRequest
{
    QString asId;
    QString asName;
    QString zipFileName;
    QString zipFilePath;
    QString accessToken;
    QString ningMengCookie;
    QString importerApiHost;
    QString environment;
    int userId = 0;
    int serviceId = 0;
    bool rewriteProServiceId = true;
    int timeoutMs = 120000;
    ImportPackageServiceType serviceType = ImportPackageServiceType::Professional;
};

struct ImportPackageUploadResult
{
    bool ok = false;
    QString importId;
    QString errorMessage;
    QStringList logs;
    int httpStatus = 0;
    int responseCode = -1;
    qint64 fileSize = 0;
    bool historySaved = false;
    bool acceptanceUnknown = false;
};

class ImportPackageUploader
{
public:
    static ImportPackageUploadResult upload(const ImportPackageUploadRequest &request);
};

#endif // IMPORTPACKAGEUPLOADER_H
