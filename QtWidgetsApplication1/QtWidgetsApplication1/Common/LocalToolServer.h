#pragma once

#include <QJsonObject>
#include <QLocalServer>
#include <functional>
#include <memory>

class QLockFile;
class QLocalSocket;

// Accepts one bounded JSON request per connection from the current Windows user.
class LocalToolServer final : public QObject
{
public:
    using Reply = std::function<void(const QJsonObject &)>;
    using Handler = std::function<void(const QJsonObject &, Reply)>;
    LocalToolServer(Handler handler, int timeoutMs, QJsonObject timeoutResponse, QObject *parent = nullptr);
    ~LocalToolServer() override;
    bool start(const QString &name, const QString &lockDirectory);
    bool isListening() const { return m_server.isListening(); }
    QString errorText() const { return m_error; }
    static QJsonObject success(const QJsonObject &data);
    static QJsonObject error(const QString &code, const QString &message, const QJsonObject &details = {});

private:
    void accept(QLocalSocket *socket);
    QLocalServer m_server;
    std::unique_ptr<QLockFile> m_lock;
    Handler m_handler;
    int m_timeoutMs;
    QJsonObject m_timeoutResponse;
    QString m_error;
};
