#include "httprequesthelper.h"
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

QNetworkAccessManager* CHttpRequestHelper::networkManager()
{
    static QNetworkAccessManager *instance = nullptr;
    if (!instance)
    {
        instance = new QNetworkAccessManager(qApp);
        instance->setCookieJar(new NoCookieJar(instance));
    }
    return instance;
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
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36");
    request.setRawHeader("Origin", "https://jpro6.ningmengyun.com");
    request.setRawHeader("Referer", "https://jpro6.ningmengyun.com/");
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
