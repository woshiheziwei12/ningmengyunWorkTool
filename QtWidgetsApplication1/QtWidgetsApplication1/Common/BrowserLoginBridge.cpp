#include "BrowserLoginBridge.h"
#include <QTcpSocket>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QHash>
#include <QVariant>

namespace {
const QByteArray pagePath("/lemon-login/v1/");
const QByteArray claimPath("/lemon-login/v1/claim");
const QByteArray completePath("/lemon-login/v1/complete");
const QByteArray failedPath("/lemon-login/v1/failed");
constexpr int maxRequestBytes = 8192;

bool validCookie(const QString &cookie)
{
    if (cookie.isEmpty() || cookie.size() > 16384) return false;
    for (const QChar c : cookie) {
        const ushort u = c.unicode();
        if (u <= 0x20 || u >= 0x7f || u == '"' || u == ',' || u == ';' || u == '\\') return false;
    }
    return true;
}
}

QString BrowserLoginBridge::extensionId()
{
    return QStringLiteral("nmahgegihmcfiendmbakhelgokloehgi");
}

BrowserLoginBridge::BrowserLoginBridge(QObject *parent) : QObject(parent)
{
    m_server.setMaxPendingConnections(8);
    m_timeout.setSingleShot(true);
    connect(&m_server, &QTcpServer::newConnection, this, [this] { acceptConnections(); });
    connect(&m_timeout, &QTimer::timeout, this, [this] { finish(false); });
}

QUrl BrowserLoginBridge::start(const QString &cookie, const QUrl &website, QString *error, int lifetimeMs)
{
    cancel();
    if (error) error->clear();
    const bool allowedHost = website.host() == "wbgate.ningmengyun.com" || website.host() == "etest.ningmengyun.com";
    if (!website.isValid() || !allowedHost || (website.scheme() != "http" && website.scheme() != "https")
        || website.port() != -1 || !website.userInfo().isEmpty() || website.hasFragment()
        || website.path() != "/wb/redirect/menu" || website.query() != "productType=acc") {
        if (error) *error = QStringLiteral("官网地址不在登录同步支持的范围内，请检查当前环境配置。");
        return {};
    }
    if (!validCookie(cookie)) {
        if (error) *error = QStringLiteral("当前登录 Cookie 无效，请重新登录后再打开官网。");
        return {};
    }
    if (!m_server.listen(QHostAddress::LocalHost, 0)) {
        if (error) *error = QStringLiteral("无法创建本机浏览器连接，请稍后重试。");
        return {};
    }
    m_cookie = cookie;
    m_website = website;
    // Both gateway hosts support HTTPS. Never send the shared domain cookie over HTTP.
    m_website.setScheme("https");
    QByteArray entropy;
    for (int i = 0; i < 8; ++i) {
        const quint32 value = QRandomGenerator::system()->generate();
        entropy.append(reinterpret_cast<const char *>(&value), sizeof(value));
    }
    m_ticket = entropy.toHex();
    m_claimed = false;
    m_active = true;
    setProperty("sessionBusyBrowserLogin", true);
    m_lifetimeMs = qBound(1, lifetimeMs, 60000);
    m_age.start();
    m_timeout.start(m_lifetimeMs);
    QUrl url(QStringLiteral("http://127.0.0.1:%1/lemon-login/v1/").arg(m_server.serverPort()));
    url.setFragment(QString::fromLatin1(m_ticket));
    return url;
}

void BrowserLoginBridge::cancel()
{
    m_active = false;
    m_timeout.stop();
    m_server.close();
    m_cookie.clear();
    m_ticket.clear();
    setProperty("sessionBusyBrowserLogin", false);
    for (auto *socket : m_server.findChildren<QTcpSocket*>()) {
        socket->abort();
        socket->deleteLater();
    }
}

void BrowserLoginBridge::finish(bool success)
{
    if (!m_active) return;
    m_active = false;
    m_timeout.stop();
    m_server.close();
    m_cookie.clear();
    m_ticket.clear();
    setProperty("sessionBusyBrowserLogin", false);
    // Responses already queued on a socket may finish draining after the listener closes.
    if (finished) finished(success);
}

void BrowserLoginBridge::acceptConnections()
{
    while (auto *socket = m_server.nextPendingConnection()) {
        if (m_server.findChildren<QTcpSocket*>().size() > 16 || !socket->peerAddress().isLoopback()) {
            socket->abort(); socket->deleteLater(); continue;
        }
        socket->setReadBufferSize(maxRequestBytes + 1);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QTimer::singleShot(5000, socket, [socket] { socket->abort(); socket->deleteLater(); });
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            if (socket->property("handled").toBool()) return;
            QByteArray buffer = socket->property("request").toByteArray() + socket->readAll();
            if (buffer.size() > maxRequestBytes) {
                socket->setProperty("handled", true); reply(socket, 413, "Request too large"); return;
            }
            if (!buffer.contains("\r\n\r\n")) { socket->setProperty("request", buffer); return; }
            socket->setProperty("handled", true);
            socket->setProperty("request", QVariant());
            handle(socket, buffer);
        });
    }
}

void BrowserLoginBridge::reply(QTcpSocket *socket, int status, const QByteArray &body, const QByteArray &contentType)
{
    QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " Response\r\nConnection: close\r\n";
    response += "Cache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff\r\n";
    response += "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'\r\n";
    response += "Access-Control-Allow-Origin: chrome-extension://" + extensionId().toLatin1() + "\r\n";
    response += "Access-Control-Allow-Methods: POST\r\nAccess-Control-Allow-Headers: authorization, x-lemon-extension\r\n";
    response += "Content-Type: " + contentType + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n";
    socket->write(response + body);
    socket->disconnectFromHost();
}

void BrowserLoginBridge::handle(QTcpSocket *socket, const QByteArray &request)
{
    if (!m_active || m_age.elapsed() >= m_lifetimeMs) { reply(socket, 410, "Expired"); finish(false); return; }
    if (!request.endsWith("\r\n\r\n") || request.indexOf("\r\n\r\n") != request.size() - 4) {
        reply(socket, 400, "Invalid request"); return;
    }
    auto lines = request.left(request.size() - 4).split('\n');
    const auto first = lines.takeFirst().trimmed().split(' ');
    if (first.size() != 3 || first[2] != "HTTP/1.1") { reply(socket, 400, "Invalid request"); return; }
    QHash<QByteArray, QByteArray> headers;
    for (const QByteArray &line : lines) {
        const int colon = line.indexOf(':');
        if (colon <= 0) { reply(socket, 400, "Invalid headers"); return; }
        const auto name = line.left(colon).toLower();
        if (headers.contains(name)) { reply(socket, 400, "Duplicate header"); return; }
        headers.insert(name, line.mid(colon + 1).trimmed());
    }
    const auto host = "127.0.0.1:" + QByteArray::number(m_server.serverPort());
    const auto origin = "chrome-extension://" + extensionId().toLatin1();
    if (headers.value("host") != host || headers.contains("transfer-encoding")
        || (headers.contains("content-length") && headers.value("content-length") != "0")) {
        reply(socket, 400, "Invalid request"); return;
    }
    const QByteArray method = first[0], path = first[1];
    if (method == "GET" && path == pagePath) {
        const QString page = QStringLiteral(R"HTML(<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta name="lemon-login-bridge" content="1"><title>柠檬云登录同步</title>
<style>body{font:16px/1.8 "Microsoft YaHei",sans-serif;background:#f4f7fb;color:#172c49;margin:60px auto;padding:32px;max-width:720px}main{background:white;padding:32px;border-radius:16px}h1{font-size:26px}a{color:#1765cc}code{overflow-wrap:anywhere}#status{font-weight:bold;color:#1765cc}</style>
<main><h1>正在连接柠檬云登录助手</h1><p id="status">等待扩展接收本次登录…</p>
<p>成功后将自动打开官网，并使用桌面工具当前登录的账号。</p>
<details open><summary>如果页面一直停留在这里</summary><ol><li>请使用已安装“柠檬云账套工具”的 Chrome，并将它设为默认浏览器。</li>
<li>在 <code>chrome://extensions/</code> 打开开发者模式，加载提供的 <code>LemonChromeExtension/unpacked</code> 文件夹。</li>
<li>回到桌面工具重新点击“打开官网”。每次连接在 60 秒后失效。</li></ol></details>
<p><a href="%1" rel="noreferrer">不切换登录账号，直接打开官网</a></p></main></html>)HTML");
        reply(socket, 200, page.arg(m_website.toString().toHtmlEscaped()).toUtf8(), "text/html; charset=utf-8");
        return;
    }
    if (path != claimPath && path != completePath && path != failedPath) { reply(socket, 404, "Not found"); return; }
    if (headers.contains("origin") && headers.value("origin") != origin) { reply(socket, 403, "Forbidden"); return; }
    if (method == "OPTIONS" && headers.value("origin") == origin
        && headers.value("access-control-request-method") == "POST") { reply(socket, 204, {}); return; }
    if (method != "POST") { reply(socket, 405, "Method not allowed"); return; }
    if (headers.value("authorization") != "Bearer " + m_ticket
        || headers.value("x-lemon-extension") != extensionId().toLatin1()) { reply(socket, 403, "Forbidden"); return; }
    if (path == claimPath) {
        if (m_claimed) { reply(socket, 409, "Already used"); return; }
        m_claimed = true;
        const QJsonObject data{{"protocol", 1}, {"url", m_website.toString()}, {"cookie", m_cookie}};
        m_cookie.clear();
        reply(socket, 200, QJsonDocument(data).toJson(QJsonDocument::Compact), "application/json");
        return;
    }
    if (!m_claimed) { reply(socket, 409, "Not claimed"); return; }
    reply(socket, 200, "OK");
    finish(path == completePath);
}
