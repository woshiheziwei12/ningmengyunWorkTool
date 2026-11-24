#pragma once
#include <QObject>
#include <QDate>
#include <QJsonObject>
#include <QUrl>
#include "LocalToolServer.h"
#include <functional>

// A single read-only endpoint for the running desktop tool, restricted to its Windows user.
class WecomLocalServer : public QObject
{
    Q_OBJECT
public:
    using Reply = LocalToolServer::Reply;
    using ReadHandler = std::function<void(const QDate &, const QString &documentId, const QUrl &, Reply)>;
    using ListHandler = std::function<QJsonObject()>;
    explicit WecomLocalServer(ReadHandler handler, ListHandler listDocuments, QObject *parent = nullptr);
    ~WecomLocalServer() override;
    bool start(const QString &name = QString(), const QString &lockDirectory = QString());
    QString statusText() const;
    static QString defaultServerName();
    static QJsonObject error(const QString &code, const QString &message);
    static QJsonObject success(const QJsonObject &data);
private:
    void dispatch(const QJsonObject &request, Reply reply);
    LocalToolServer m_server;
    ReadHandler m_read;
    ListHandler m_listDocuments;
    bool m_readActive = false;
};
