#ifndef ACCESS2003CONVERTER_H
#define ACCESS2003CONVERTER_H

#include <QString>
#include <QStringList>
#include <QVector>

struct Access2003ConversionItem
{
    QString sourcePath;
    QString destinationPath;
    bool success = false;
    bool copiedBecauseAlreadyCompatible = false;
    QString errorMessage;
};

struct Access2003BatchResult
{
    bool accessStarted = false;
    QString accessVersion;
    QString startupError;
    QVector<Access2003ConversionItem> items;
};

class Access2003Converter
{
public:
    static QStringList collectDatabaseFiles(const QString &directoryPath,
                                            bool recursive = true);
    static QString suggestedOutputFileName(const QString &sourcePath);
    static QString uniqueOutputPath(const QString &outputDirectory,
                                    const QString &sourcePath,
                                    const QStringList &reservedPaths = QStringList());
    static Access2003BatchResult convertFiles(const QStringList &sourcePaths,
                                              const QString &outputDirectory,
                                              const QString &password = QString());
    static bool isAccess2003Registered(QString *registrationName = nullptr);
};

#endif // ACCESS2003CONVERTER_H
