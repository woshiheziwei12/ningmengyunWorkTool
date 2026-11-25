#include "WecomLocalServer.h"
#include "WecomDocumentData.h"
#include "LocalToolEndpoint.h"
#include <QCoreApplication>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVariant>

WecomLocalServer::WecomLocalServer(ReadHandler handler, ListHandler listDocuments, QObject *parent)
    : QObject(parent),
      m_server([this](const QJsonObject &request, Reply reply) { dispatch(request, std::move(reply)); },
          300000, error("read_timeout", QStringLiteral("读取超过五分钟，请查看工具中的进度或停止后重试。"))),
      m_read(std::move(handler)),
      m_listDocuments(std::move(listDocuments))
{
    setObjectName("wecomLocalServer");
}

WecomLocalServer::~WecomLocalServer() = default;

QString WecomLocalServer::defaultServerName()
{
    QString name = LocalToolEndpoint::name("Wecom");
    if (!name.isEmpty() && QStandardPaths::isTestModeEnabled())
        name += ".test." + QString::number(QCoreApplication::applicationPid());
    return name;
}

bool WecomLocalServer::start(const QString &requestedName, const QString &lockDirectory)
{
    const QString name = requestedName.isEmpty() ? defaultServerName() : requestedName;
    const QString directory = lockDirectory.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/LemonCloud/LemonWorkTool/WecomBridge"
        : lockDirectory;
    return m_server.start(name, directory);
}

QString WecomLocalServer::statusText() const
{
    return m_server.isListening()
        ? QStringLiteral("本机读取接口已就绪：助手可请求最新文档，工具最小化到托盘后仍可使用。")
        : m_server.errorText();
}

QJsonObject WecomLocalServer::error(const QString &code, const QString &message)
{
    return LocalToolServer::error(code, message);
}

QJsonObject WecomLocalServer::success(const QJsonObject &data)
{
    return LocalToolServer::success(data);
}

void WecomLocalServer::dispatch(const QJsonObject &request, Reply reply)
{
    const QString action = request.value("action").toString();
    const bool manual = action == "read_url";
    for (const auto &key : request.keys()) {
        if (key != "protocolVersion" && key != "action" && key != "fromDate"
            && !(key == "documentId" && action == "read_document") && !(key == "url" && manual)) {
            reply(error("invalid_request", QStringLiteral("请求含不支持的字段。")));
            return;
        }
    }
    if (request.value("protocolVersion") != QJsonValue(LocalToolEndpoint::ProtocolVersion)) {
        reply(error("unsupported_version", QStringLiteral("接口版本不匹配。")));
        return;
    }
    if (action == "status" && !request.contains("fromDate")) {
        reply(success({{"running", true}, {"readInProgress", m_readActive},
            {"processId", double(QCoreApplication::applicationPid())}, {"documentId", WecomDocument::documentId()}}));
        return;
    }
    if (action == "documents" && !request.contains("fromDate") && m_listDocuments) {
        reply(m_listDocuments());
        return;
    }
    if (action != "read" && action != "read_document" && !manual) {
        reply(error("unsupported_action", QStringLiteral("不支持请求的企业微信操作。")));
        return;
    }
    const QString documentId = request.value("documentId").toString();
    static const QRegularExpression idPattern(QStringLiteral("\\A[A-Za-z0-9_-]+\\z"));
    if (action == "read_document" && !idPattern.match(documentId).hasMatch()) {
        reply(error("invalid_document", QStringLiteral("请使用表格清单中的文档 ID。")));
        return;
    }
    QUrl url;
    if (manual) {
        url = QUrl(request.value("url").toString(), QUrl::StrictMode);
        QString errorMessage;
        if (!WecomDocument::validateUrl(url, &errorMessage)) {
            reply(error("invalid_url", errorMessage));
            return;
        }
    }
    const auto date = WecomDocument::fromDate(request.value("fromDate"));
    if (!date.isValid()) {
        reply(error("invalid_date", QStringLiteral("日期必须为今天或之后，格式为 yyyy-MM-dd。")));
        return;
    }
    if (m_readActive) {
        reply(error("busy", QStringLiteral("正在读取文档，请稍后重试。")));
        return;
    }
    m_readActive = true;
    setProperty("sessionBusyWecomBridge", true);
    const auto completed = std::make_shared<bool>(false);
    const QPointer<WecomLocalServer> guard(this);
    m_read(date, documentId, url, [guard, reply, completed](const QJsonObject &response) {
        if (!guard || *completed) return;
        *completed = true;
        guard->m_readActive = false;
        guard->setProperty("sessionBusyWecomBridge", false);
        reply(response);
    });
}
