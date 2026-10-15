#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTimer>
#include <QElapsedTimer>
#include <QUrl>
#include <functional>

class QTcpSocket;

// One click creates one short-lived, loopback-only exchange. No credentials on disk or in URLs.
class BrowserLoginBridge : public QObject
{
public:
    explicit BrowserLoginBridge(QObject *parent = nullptr);
    QUrl start(const QString &cookie, const QUrl &website, QString *error,
               int lifetimeMs = 60000);
    void cancel();
    std::function<void(bool)> finished;
    static QString extensionId();

private:
    void acceptConnections();
    void handle(QTcpSocket *socket, const QByteArray &request);
    void finish(bool success);
    void reply(QTcpSocket *socket, int status, const QByteArray &body,
               const QByteArray &contentType = "text/plain; charset=utf-8");

    QTcpServer m_server;
    QTimer m_timeout;
    QElapsedTimer m_age;
    int m_lifetimeMs = 60000;
    QByteArray m_ticket;
    QString m_cookie;
    QUrl m_website;
    bool m_claimed = false;
    bool m_active = false;
};
