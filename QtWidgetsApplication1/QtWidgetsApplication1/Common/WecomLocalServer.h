#pragma once
#include <QObject>
#include <QDate>
#include <QJsonObject>
#include <QLocalServer>
#include <functional>
#include <memory>
class QLockFile;
class QLocalSocket;

// A single read-only endpoint for the running desktop tool, restricted to its Windows user.
class WecomLocalServer : public QObject
{
    Q_OBJECT
public:
    using Reply = std::function<void(const QJsonObject &)>;
    using ReadHandler = std::function<void(const QDate &, Reply)>;
    explicit WecomLocalServer(ReadHandler handler, QObject *parent = nullptr);
    ~WecomLocalServer() override;
    bool start(const QString &name = QString(), const QString &lockDirectory = QString());
    QString statusText() const;
    static QString defaultServerName();
    static QJsonObject error(const QString &code, const QString &message);
    static QJsonObject success(const QJsonObject &data);
private:
    void accept(QLocalSocket *socket);
    QLocalServer m_server;
    std::unique_ptr<QLockFile> m_lock;
    ReadHandler m_read;
    QString m_error;
    bool m_readActive = false;
};
