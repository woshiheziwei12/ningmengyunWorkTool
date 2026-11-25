#include "LocalToolServer.h"
#include "LocalToolEndpoint.h"
#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QLockFile>
#include <QPointer>
#include <QTimer>

namespace {
constexpr int MaxClients = 8;
constexpr int RequestFrameTimeoutMs = 5000;

struct RequestFrame {
    bool parsed = false;
    bool replied = false;
    QByteArray input;
};
}

LocalToolServer::LocalToolServer(Handler handler, int timeoutMs, QJsonObject timeoutResponse, QObject *parent)
    : QObject(parent), m_handler(std::move(handler)), m_timeoutMs(timeoutMs), m_timeoutResponse(std::move(timeoutResponse))
{
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    m_server.setMaxPendingConnections(MaxClients);
    connect(&m_server, &QLocalServer::newConnection, this, [this]() {
        while (auto *socket = m_server.nextPendingConnection()) {
            if (findChildren<QLocalSocket*>(QString(), Qt::FindDirectChildrenOnly).size() >= MaxClients) {
                socket->abort();
                socket->deleteLater();
                continue;
            }
            socket->setParent(this);
            accept(socket);
        }
    });
}

LocalToolServer::~LocalToolServer()
{
    m_server.close();
    for (auto *socket : findChildren<QLocalSocket*>()) socket->abort();
}

bool LocalToolServer::start(const QString &name, const QString &lockDirectory)
{
    if (m_server.isListening()) return true;
    if (name.isEmpty() || !QDir().mkpath(lockDirectory)) {
        m_error = QStringLiteral("本机接口初始化失败。");
        return false;
    }
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256).toHex());
    m_lock = std::make_unique<QLockFile>(QDir(lockDirectory).filePath(hash + ".lock"));
    m_lock->setStaleLockTime(0);
    // Windows allows separate servers with the same pipe name to compete for callers.
    if (!m_lock->tryLock()) {
        m_error = QStringLiteral("本机接口被另一个工具实例占用，请使用先打开的实例。");
        return false;
    }
    if (!m_server.listen(name)) {
        m_lock.reset();
        m_error = QStringLiteral("本机接口启动失败，请重新打开工具。");
        return false;
    }
    return true;
}

QJsonObject LocalToolServer::success(const QJsonObject &data)
{
    return {{"protocolVersion", LocalToolEndpoint::ProtocolVersion}, {"ok", true}, {"data", data}};
}

QJsonObject LocalToolServer::error(const QString &code, const QString &message, const QJsonObject &details)
{
    QJsonObject error{{"code", code}, {"message", message}};
    if (!details.isEmpty()) error["details"] = details;
    return {{"protocolVersion", LocalToolEndpoint::ProtocolVersion}, {"ok", false}, {"error", error}};
}

void LocalToolServer::accept(QLocalSocket *socket)
{
    socket->setReadBufferSize(LocalToolEndpoint::MaxRequestBytes + 1);
    connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    const QPointer<QLocalSocket> peer(socket);
    const auto frame = std::make_shared<RequestFrame>();
    const Reply reply = [peer, frame](const QJsonObject &response) {
        if (frame->replied) return;
        frame->replied = true;
        if (!peer || peer->state() != QLocalSocket::ConnectedState) return;
        QByteArray data = QJsonDocument(response).toJson(QJsonDocument::Compact);
        if (data.size() > LocalToolEndpoint::MaxResponseBytes)
            data = QJsonDocument(error("response_too_large", QStringLiteral("结果超过本机接口上限，请在工具中查看结果。"))).toJson(QJsonDocument::Compact);
        peer->write(data + '\n');
        peer->disconnectFromServer();
    };
    QTimer::singleShot(RequestFrameTimeoutMs, socket, [frame, reply]() {
        if (!frame->parsed) reply(error("invalid_request", QStringLiteral("请求不完整或超时。")));
    });
    const auto consume = [this, peer, frame, reply]() {
        if (!peer || frame->parsed || frame->replied) return;
        frame->input += peer->readAll();
        if (frame->input.size() > LocalToolEndpoint::MaxRequestBytes) {
            frame->parsed = true;
            reply(error("invalid_request", QStringLiteral("请求过大。")));
            return;
        }
        const int end = frame->input.indexOf('\n');
        if (end < 0) return;
        frame->parsed = true;
        const auto document = QJsonDocument::fromJson(frame->input.left(end));
        if (!document.isObject() || !frame->input.mid(end + 1).trimmed().isEmpty()) {
            reply(error("invalid_request", QStringLiteral("每次连接只接受一条 JSON 请求。")));
            return;
        }
        QTimer::singleShot(m_timeoutMs, peer, [reply, response = m_timeoutResponse]() { reply(response); });
        m_handler(document.object(), reply);
    };
    connect(socket, &QLocalSocket::readyRead, this, consume);
    if (socket->bytesAvailable()) consume();
}
