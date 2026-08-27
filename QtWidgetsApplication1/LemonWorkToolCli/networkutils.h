#ifndef NETWORKUTILS_H
#define NETWORKUTILS_H

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QPair>
#include <QUrl>

struct HttpResponse
{
    int httpStatus = 0;
    int networkError = 0;
    bool timedOut = false;
    QString errorString;
    QByteArray body;
    QList<QPair<QByteArray, QByteArray>> rawHeaders;
};

struct HttpRequestSpec
{
    QString method = QStringLiteral("GET");
    QUrl url;
    QList<QPair<QByteArray, QByteArray>> rawHeaders;
    QByteArray body;
    bool manualRedirect = false;
    int timeoutMs = 120000;
};

HttpResponse sendHttpRequest(const HttpRequestSpec &spec);
QByteArray buildFormBody(const QMap<QString, QString> &params);
QString rawHeaderValue(const HttpResponse &response, const QByteArray &name);

#endif // NETWORKUTILS_H
