#ifndef CLIRESULT_H
#define CLIRESULT_H

#include <QJsonObject>
#include <QString>

enum CliExitCode
{
    ExitSuccess = 0,
    ExitArgumentError = 1,
    ExitFileError = 2,
    ExitAuthError = 3,
    ExitNetworkError = 4,
    ExitServerError = 5,
    ExitDataError = 6,
    ExitUserTimeout = 7,
    ExitNotImplemented = 8,
    ExitUnknownError = 9
};

class CliResult
{
public:
    static CliResult success(const QString &command, const QJsonObject &data = QJsonObject());
    static CliResult failure(const QString &command, const QString &code, const QString &message,
                             int exitCode, const QJsonObject &details = QJsonObject());

    QJsonObject toJson() const;

    bool ok = false;
    QString command;
    QString requestId;
    QString timestamp;
    qint64 durationMs = 0;
    QJsonObject data;
    QString errorCode;
    QString errorMessage;
    int exitCode = ExitSuccess;
    QJsonObject errorDetails;
    QString logPath;
};

QString makeRequestId();
QString isoNow();
void printJson(const QJsonObject &obj, bool pretty);

#endif // CLIRESULT_H
