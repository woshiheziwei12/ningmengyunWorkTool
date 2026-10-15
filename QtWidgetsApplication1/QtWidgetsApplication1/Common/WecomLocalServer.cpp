#include "WecomLocalServer.h"
#include "WecomDocumentData.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocalSocket>
#include <QLockFile>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QVariant>
#include <Windows.h>
#include <sddl.h>
#pragma comment(lib, "Advapi32.lib")

WecomLocalServer::WecomLocalServer(ReadHandler handler, QObject *parent)
    : QObject(parent), m_read(std::move(handler))
{
    setObjectName("wecomLocalServer");
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    m_server.setMaxPendingConnections(8);
    connect(&m_server, &QLocalServer::newConnection, this, [this]() {
        while (auto *socket = m_server.nextPendingConnection()) {
            if (findChildren<QLocalSocket*>(QString(), Qt::FindDirectChildrenOnly).size() >= 8) {
                socket->abort(); socket->deleteLater(); continue;
            }
            socket->setParent(this); accept(socket);
        }
    });
}
WecomLocalServer::~WecomLocalServer()
{
    m_server.close();
    for (auto *socket : findChildren<QLocalSocket*>()) socket->abort();
}
QString WecomLocalServer::defaultServerName()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    DWORD size = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    QByteArray buffer(int(size), '\0');
    const BOOL ok = GetTokenInformation(token, TokenUser, buffer.data(), size, &size); CloseHandle(token);
    LPWSTR sid = nullptr;
    if (!ok || !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid)) return {};
    QString name = "LemonWorkTool.Wecom.v1." + QString::fromWCharArray(sid); LocalFree(sid);
    if (QStandardPaths::isTestModeEnabled()) name += ".test." + QString::number(QCoreApplication::applicationPid());
    return name;
}
bool WecomLocalServer::start(const QString &requestedName, const QString &lockDirectory)
{
    if (m_server.isListening()) return true;
    const QString name = requestedName.isEmpty() ? defaultServerName() : requestedName;
    const QString directory = lockDirectory.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/LemonCloud/LemonWorkTool/WecomBridge"
        : lockDirectory;
    if (name.isEmpty() || !QDir().mkpath(directory)) { m_error = QStringLiteral("本机读取接口初始化失败。"); return false; }
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256).toHex());
    m_lock.reset(new QLockFile(directory + '/' + hash + ".lock"));
    m_lock->setStaleLockTime(0);
    // Windows otherwise permits multiple QLocalServers to consume the same pipe at random.
    if (!m_lock->tryLock()) { m_error = QStringLiteral("本机读取接口被另一个工具实例占用，请使用先打开的实例。"); return false; }
    if (!m_server.listen(name)) {
        m_lock.reset(); m_error = QStringLiteral("本机读取接口启动失败，请重新打开工具。"); return false;
    }
    return true;
}
QString WecomLocalServer::statusText() const
{ return m_server.isListening() ? QStringLiteral("本机读取接口已就绪：助手可请求最新文档，工具最小化到托盘后仍可使用。") : m_error; }
QJsonObject WecomLocalServer::error(const QString &code, const QString &message)
{ return {{"protocolVersion", 1}, {"ok", false}, {"error", QJsonObject{{"code", code}, {"message", message}}}}; }
QJsonObject WecomLocalServer::success(const QJsonObject &data)
{ return {{"protocolVersion", 1}, {"ok", true}, {"data", data}}; }

void WecomLocalServer::accept(QLocalSocket *socket)
{
    socket->setReadBufferSize(8193);
    connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    QPointer<QLocalSocket> peer(socket); QPointer<WecomLocalServer> guard(this);
    auto replied = std::make_shared<bool>(false);
    auto parsed = std::make_shared<bool>(false);
    auto input = std::make_shared<QByteArray>();
    Reply reply = [peer, replied](const QJsonObject &response) {
        if (*replied) return;
        *replied = true;
        if (!peer || peer->state() != QLocalSocket::ConnectedState) return;
        QByteArray data = QJsonDocument(response).toJson(QJsonDocument::Compact);
        if (data.size() > 64 * 1024 * 1024) data = QJsonDocument(error("response_too_large", QStringLiteral("结果超过本机接口上限，请在工具中导出。"))).toJson(QJsonDocument::Compact);
        data.append('\n'); peer->write(data); peer->disconnectFromServer();
    };
    QTimer::singleShot(5000, socket, [parsed, reply]() {
        if (!*parsed) reply(error("invalid_request", QStringLiteral("请求不完整或超时。")));
    });
    auto consume = [guard, peer, parsed, replied, input, reply]() {
        if (!guard || !peer || *parsed || *replied) return;
        input->append(peer->readAll());
        if (input->size() > 8192) { *parsed = true; reply(error("invalid_request", QStringLiteral("请求过大。"))); return; }
        const int end = input->indexOf('\n'); if (end < 0) return;
        *parsed = true;
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(input->left(end), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject() || !input->mid(end + 1).trimmed().isEmpty()) {
            reply(error("invalid_request", QStringLiteral("每次连接只接受一条 JSON 请求。"))); return;
        }
        const auto request = document.object();
        for (const auto &key : request.keys()) if (key != "protocolVersion" && key != "action" && key != "fromDate") {
            reply(error("invalid_request", QStringLiteral("请求含不支持的字段。"))); return;
        }
        if (request.value("protocolVersion") != QJsonValue(1)) { reply(error("unsupported_version", QStringLiteral("接口版本不匹配。"))); return; }
        const QString action = request.value("action").toString();
        if (action == "status" && !request.contains("fromDate")) {
            reply(success({{"running", true}, {"readInProgress", guard->m_readActive},
                {"processId", double(QCoreApplication::applicationPid())}, {"documentId", WecomDocument::documentId()}})); return;
        }
        if (action != "read") { reply(error("unsupported_action", QStringLiteral("仅支持 status 和 read。"))); return; }
        const auto date = request.contains("fromDate") ? QDate::fromString(request.value("fromDate").toString(), Qt::ISODate) : QDate::currentDate();
        if (!date.isValid() || date < QDate::currentDate()
            || (request.contains("fromDate") && date.toString(Qt::ISODate) != request.value("fromDate").toString())) {
            reply(error("invalid_date", QStringLiteral("日期必须为今天或之后，格式为 yyyy-MM-dd。"))); return;
        }
        if (guard->m_readActive) { reply(error("busy", QStringLiteral("正在读取文档，请稍后重试。"))); return; }
        guard->m_readActive = true; guard->setProperty("sessionBusyWecomBridge", true);
        QTimer::singleShot(300000, peer, [reply]() { reply(error("read_timeout", QStringLiteral("读取超过五分钟，请查看工具中的进度或停止后重试。"))); });
        auto completed = std::make_shared<bool>(false);
        guard->m_read(date, [guard, reply, completed](const QJsonObject &response) {
            if (!guard || *completed) return;
            *completed = true; guard->m_readActive = false; guard->setProperty("sessionBusyWecomBridge", false); reply(response);
        });
    };
    connect(socket, &QLocalSocket::readyRead, this, consume);
    if (socket->bytesAvailable()) consume();
}
