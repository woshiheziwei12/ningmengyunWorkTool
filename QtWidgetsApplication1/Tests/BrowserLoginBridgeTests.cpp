#include "Common/BrowserLoginBridge.h"
#include <QCoreApplication>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value) { fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
bool until(const std::function<bool()> &condition, int timeout = 3000) {
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1);
    }
    return condition();
}
QByteArray exchange(const QUrl &url, const QByteArray &request, bool fragmented = false) {
    QTcpSocket socket;
    socket.connectToHost("127.0.0.1", quint16(url.port()));
    check(until([&] { return socket.state() == QAbstractSocket::ConnectedState; }), "loopback connect");
    if (fragmented) {
        socket.write(request.left(12)); socket.flush();
        QCoreApplication::processEvents();
        socket.write(request.mid(12));
    } else socket.write(request);
    socket.flush();
    QByteArray response;
    check(until([&] { response += socket.readAll(); return socket.state() == QAbstractSocket::UnconnectedState; }), "response completed");
    response += socket.readAll();
    return response;
}
QByteArray request(const QUrl &url, const QByteArray &path, const QByteArray &method = "POST", const QByteArray &extra = {}) {
    return method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(url.port())
        + "\r\nAuthorization: Bearer " + url.fragment().toLatin1()
        + "\r\nX-Lemon-Extension: " + BrowserLoginBridge::extensionId().toLatin1() + "\r\n" + extra + "\r\n";
}
bool status(const QByteArray &response, int code) { return response.startsWith("HTTP/1.1 " + QByteArray::number(code) + ' '); }
QByteArray body(const QByteArray &response) { return response.mid(response.indexOf("\r\n\r\n") + 4); }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString secret = "synthetic-test-token";
    const QUrl pro("https://wbgate.ningmengyun.com/wb/redirect/menu?productType=acc");
    QString error;
    BrowserLoginBridge bridge;
    if (app.arguments().contains("--browser-fixture")) {
        const QUrl website = app.arguments().contains("--test-environment")
            ? QUrl("http://etest.ningmengyun.com/wb/redirect/menu?productType=acc") : pro;
        bridge.finished = [&](bool ok) { QTimer::singleShot(100, &app, [&app, ok] { app.exit(ok ? 0 : 1); }); };
        const QUrl handoff = bridge.start(secret, website, &error);
        if (handoff.isEmpty()) return 2;
        printf("%s\n", handoff.toString().toUtf8().constData()); fflush(stdout);
        return app.exec();
    }
    bool completed = false, success = false;
    bridge.finished = [&](bool ok) { completed = true; success = ok; };
    check(bridge.start(secret, QUrl("https://evil.example/wb/redirect/menu?productType=acc"), &error).isEmpty(), "no external destinations");
    check(bridge.start(secret, QUrl("https://wbgate.ningmengyun.com/wb/redirect/menu?productType=acc&next=x"), &error).isEmpty(), "no redirect query override");
    check(bridge.start("bad;cookie", pro, &error).isEmpty(), "invalid cookie rejected");
    const auto url = bridge.start(secret, pro, &error);
    check(!url.isEmpty() && url.host() == "127.0.0.1" && url.fragment().size() == 64, "ephemeral local ticket");
    check(!url.toString().contains(secret) && bridge.property("sessionBusyBrowserLogin").toBool(), "no cookie in URL and busy guard");
    auto page = exchange(url, request(url, "/lemon-login/v1/", "GET"));
    check(status(page, 200) && !page.contains(secret.toUtf8()) && !page.contains(url.fragment().toUtf8()), "HTML contains neither credential nor ticket");
    check(page.contains("Cache-Control: no-store") && page.contains("Referrer-Policy: no-referrer"), "no cache or referrer leak");
    auto badTicket = url; badTicket.setFragment(QString(64, '0'));
    check(status(exchange(url, request(badTicket, "/lemon-login/v1/claim")), 403), "wrong ticket blocked");
    check(status(exchange(url, request(url, "/lemon-login/v1/claim", "POST", "Origin: https://evil.example\r\n")), 403), "web origin blocked");
    check(status(exchange(url, request(url, "/lemon-login/v1/claim", "POST", "Origin: null\r\n")), 403), "null origin blocked");
    auto badHost = request(url, "/lemon-login/v1/claim");
    badHost.replace("Host: 127.0.0.1:", "Host: evil.example:");
    check(status(exchange(url, badHost), 400), "DNS rebinding host blocked");
    check(status(exchange(url, request(url, "/lemon-login/v1/complete")), 409), "cannot complete before claim");
    check(status(exchange(url, request(url, "/lemon-login/v1/claim", "GET")), 405), "navigation cannot claim credentials");
    check(status(exchange(url, request(url, "/lemon-login/v1/claim", "POST", "Content-Length: 1\r\n")), 400), "request bodies rejected");
    check(status(exchange(url, request(url, "/lemon-login/v1/claim", "POST", "Host: duplicate\r\n")), 400), "ambiguous headers rejected");
    const QByteArray origin = "Origin: chrome-extension://" + BrowserLoginBridge::extensionId().toLatin1() + "\r\n";
    check(status(exchange(url, request(url, "/lemon-login/v1/claim", "OPTIONS", origin + "Access-Control-Request-Method: POST\r\n")), 204), "extension preflight accepted");
    auto response = exchange(url, request(url, "/lemon-login/v1/claim", "POST", origin), true);
    const auto payload = QJsonDocument::fromJson(body(response)).object();
    check(status(response, 200) && payload.value("cookie").toString() == secret && payload.value("url").toString() == pro.toString(), "fragmented authenticated claim works");
    check(status(exchange(url, request(url, "/lemon-login/v1/claim")), 409), "ticket is one use");
    check(status(exchange(url, request(url, "/lemon-login/v1/complete")), 200) && completed && success, "success receipt delivered before shutdown");
    check(!bridge.property("sessionBusyBrowserLogin").toBool(), "busy cleared on completion");
    completed = false;
    auto testUrl = bridge.start(secret, QUrl("http://etest.ningmengyun.com/wb/redirect/menu?productType=acc"), &error);
    check(testUrl.fragment() != url.fragment(), "new click has a new ticket");
    response = exchange(testUrl, request(testUrl, "/lemon-login/v1/claim"));
    check(QJsonDocument::fromJson(body(response)).object().value("url").toString().startsWith("https://etest."), "test environment upgraded to HTTPS");
    check(status(exchange(testUrl, request(testUrl, "/lemon-login/v1/failed")), 200) && completed && !success, "failure releases busy guard");
    completed = false;
    auto expired = bridge.start(secret, pro, &error, 25);
    check(until([&] { return completed; }) && !success, "pending ticket expires");
    check(!bridge.property("sessionBusyBrowserLogin").toBool(), "expiry clears busy guard");
    auto cancelled = bridge.start(secret, pro, &error);
    bridge.cancel();
    check(!bridge.property("sessionBusyBrowserLogin").toBool(), "cancel clears busy guard");
    printf("PASS: %d checks, real loopback HTTP, synthetic credentials only\n", checks);
    return 0;
}
