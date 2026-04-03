#ifndef CHTTPREQUESTHELPER_H
#define CHTTPREQUESTHELPER_H

#include <QString>
#include <QMap>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QSslConfiguration>
#include <QtNetwork/QSslSocket>
#include <QtNetwork/QSslError>

class CHttpRequestHelper
{
public:
    // 获取全局唯一的 QNetworkAccessManager
    static QNetworkAccessManager* networkManager();

    // 将 QMap 参数拼接为 URL-encoded POST body（key1=val1&key2=val2）
    static QString buildPostData(const QMap<QString, QString> &params);

    // 为 QNetworkRequest 配置 SSL（VerifyNone）
    static void configureSsl(QNetworkRequest &request);

    // 设置默认表单提交头（UserAgent, ContentType: application/x-www-form-urlencoded）
    static void setFormHeaders(QNetworkRequest &request);

    // 设置 jproapi 请求通用头（UserAgent, ContentType, Origin, Referer）
    static void setJproApiHeaders(QNetworkRequest &request);

    // 为 QNetworkReply 忽略 SSL 错误并记录日志
    static void handleSslErrors(QNetworkReply *reply);

private:
    CHttpRequestHelper() = delete;
};

#endif // CHTTPREQUESTHELPER_H
