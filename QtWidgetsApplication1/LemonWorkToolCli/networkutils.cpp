#include "networkutils.h"

#include "httprequesthelper.h"

#include <QEventLoop>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

HttpResponse sendHttpRequest(const HttpRequestSpec &spec)
{
    HttpResponse response;

    QNetworkAccessManager manager;
    QNetworkRequest request(spec.url);
    CHttpRequestHelper::configureSsl(request);
    if (spec.manualRedirect)
    {
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::ManualRedirectPolicy);
    }

    for (const auto &header : spec.rawHeaders)
        request.setRawHeader(header.first, header.second);

    QNetworkReply *reply = nullptr;
    const QString method = spec.method.toUpper();
    if (method == "POST")
        reply = manager.post(request, spec.body);
    else if (method == "PUT")
        reply = manager.put(request, spec.body);
    else
        reply = manager.get(request);

    CHttpRequestHelper::handleSslErrors(reply);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        response.timedOut = true;
        if (reply && !reply->isFinished())
            reply->abort();
        loop.quit();
    });
    timer.start(spec.timeoutMs);
    loop.exec();
    timer.stop();

    response.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    response.networkError = static_cast<int>(reply->error());
    response.errorString = reply->errorString();
    response.body = reply->readAll();
    for (const QByteArray &name : reply->rawHeaderList())
        response.rawHeaders.append(qMakePair(name, reply->rawHeader(name)));

    delete reply;
    return response;
}

QByteArray buildFormBody(const QMap<QString, QString> &params)
{
    QByteArray body;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
    {
        if (!body.isEmpty())
            body.append('&');
        body.append(QUrl::toPercentEncoding(it.key()));
        body.append('=');
        body.append(QUrl::toPercentEncoding(it.value()));
    }
    return body;
}

QString rawHeaderValue(const HttpResponse &response, const QByteArray &name)
{
    for (const auto &header : response.rawHeaders)
    {
        if (header.first.compare(name, Qt::CaseInsensitive) == 0)
            return QString::fromUtf8(header.second);
    }
    return QString();
}
