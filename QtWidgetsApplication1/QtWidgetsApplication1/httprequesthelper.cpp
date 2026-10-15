#include "httprequesthelper.h"
#include "lemoncofighelper.h"
#include <QDebug>
#include <QCoreApplication>
#include <QNetworkCookieJar>
#include <QNetworkCookie>

// 不自动存储/发送 cookie 的空 jar，所有 cookie 由业务代码手动管理
class NoCookieJar : public QNetworkCookieJar
{
public:
    using QNetworkCookieJar::QNetworkCookieJar;
    bool setCookiesFromUrl(const QList<QNetworkCookie> &, const QUrl &) override { return false; }
    QList<QNetworkCookie> cookiesForUrl(const QUrl &) const override { return {}; }
};

// 在发送边界统一设置，确保登录、JWT、工作台和账套请求不会遗漏。
class LemonNetworkAccessManager : public QNetworkAccessManager
{
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData = nullptr) override
    {
        QNetworkRequest internalRequest(request);
        CHttpRequestHelper::setInternalSessionHeader(internalRequest);
        return QNetworkAccessManager::createRequest(operation, internalRequest, outgoingData);
    }
};

QNetworkAccessManager* CHttpRequestHelper::networkManager()
{
    static QNetworkAccessManager *instance = nullptr;
    if (!instance)
    {
        instance = new LemonNetworkAccessManager(qApp);
        instance->setCookieJar(new NoCookieJar(instance));
    }
    return instance;
}

void CHttpRequestHelper::setInternalSessionHeader(QNetworkRequest &request)
{
    request.setRawHeader("lmlimit", "internal");
}

QString CHttpRequestHelper::buildPostData(const QMap<QString, QString> &params)
{
    QString postData;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
    {
        if (!postData.isEmpty())
            postData += '&';
        postData += it.key() + '=' + it.value();
    }
    return postData;
}

void CHttpRequestHelper::configureSsl(QNetworkRequest &request)
{
    QSslConfiguration conf = request.sslConfiguration();
    conf.setPeerVerifyMode(QSslSocket::VerifyNone);
    conf.setProtocol(QSsl::AnyProtocol);
    request.setSslConfiguration(conf);
}

void CHttpRequestHelper::setFormHeaders(QNetworkRequest &request)
{
    request.setHeader(QNetworkRequest::UserAgentHeader,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
}

void CHttpRequestHelper::setJproApiHeaders(QNetworkRequest &request)
{
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/149.0.0.0 Safari/537.36");
    request.setRawHeader("Accept", "application/json, text/plain, */*");
    request.setRawHeader("Sec-Fetch-Site", "same-site");
    request.setRawHeader("Sec-Fetch-Mode", "cors");
    request.setRawHeader("Sec-Fetch-Dest", "empty");
    const QString jproOrigin = lemoncofighelper::AccountConfigValue(lemoncofighelper::kJproOriginKey);
    if (!jproOrigin.isEmpty())
    {
        request.setRawHeader("Origin", jproOrigin.toUtf8());
        request.setRawHeader("Referer", lemoncofighelper::WithTrailingSlash(jproOrigin).toUtf8());
    }
}

void CHttpRequestHelper::setWbgateBrowserHeaders(QNetworkRequest &request)
{
    request.setHeader(QNetworkRequest::UserAgentHeader,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/147.0.0.0 Safari/537.36");
    request.setRawHeader("Accept", "application/json, text/plain, */*");

    const QString wwwOrigin = lemoncofighelper::AccountConfigValue(lemoncofighelper::kWwwOriginKey);
    if (!wwwOrigin.isEmpty())
    {
        request.setRawHeader("Origin", wwwOrigin.toUtf8());
        request.setRawHeader("Referer", lemoncofighelper::WithTrailingSlash(wwwOrigin).toUtf8());
    }
}

void CHttpRequestHelper::handleSslErrors(QNetworkReply *reply)
{
    QObject::connect(reply, QOverload<const QList<QSslError> &>::of(&QNetworkReply::sslErrors),
        [](const QList<QSslError> &errors) {
            for (const QSslError &err : errors)
            {
                qDebug() << QStringLiteral("SSL错误:") << err.errorString();
            }
        });
    reply->ignoreSslErrors();
}
