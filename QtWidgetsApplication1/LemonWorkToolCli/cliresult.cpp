#include "cliresult.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QTextStream>

QString isoNow()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

QString makeRequestId()
{
    static int counter = 0;
    ++counter;
    return QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss-zzz-")
        + QString::number(counter).rightJustified(4, '0');
}

CliResult CliResult::success(const QString &command, const QJsonObject &data)
{
    CliResult result;
    result.ok = true;
    result.command = command;
    result.requestId = makeRequestId();
    result.timestamp = isoNow();
    result.data = data;
    result.exitCode = ExitSuccess;
    return result;
}

CliResult CliResult::failure(const QString &command, const QString &code, const QString &message,
                             int exitCode, const QJsonObject &details)
{
    CliResult result;
    result.ok = false;
    result.command = command;
    result.requestId = makeRequestId();
    result.timestamp = isoNow();
    result.errorCode = code;
    result.errorMessage = message;
    result.exitCode = exitCode;
    result.errorDetails = details;
    return result;
}

QJsonObject CliResult::toJson() const
{
    QJsonObject root;
    root["ok"] = ok;
    root["command"] = command;
    root["requestId"] = requestId;
    root["timestamp"] = timestamp;
    root["durationMs"] = QString::number(durationMs).toDouble();
    root["warnings"] = QJsonArray();

    if (ok)
    {
        root["data"] = data;
    }
    else
    {
        root["data"] = QJsonValue::Null;
        QJsonObject err;
        err["code"] = errorCode;
        err["message"] = errorMessage;
        err["exitCode"] = exitCode;
        err["details"] = errorDetails;
        root["error"] = err;
    }

    QJsonObject log;
    log["path"] = logPath;
    root["log"] = log;
    return root;
}

void printJson(const QJsonObject &obj, bool pretty)
{
    QTextStream stream(stdout);
    stream.setCodec("UTF-8");
    stream << QString::fromUtf8(QJsonDocument(obj).toJson(
        pretty ? QJsonDocument::Indented : QJsonDocument::Compact));
    stream.flush();
}
