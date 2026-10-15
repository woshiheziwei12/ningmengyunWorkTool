#include "Common/WecomLocalServer.h"
#include "Common/WecomDocumentData.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QVariant>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <cstdio>
#include <cstdlib>

namespace {
void check(bool value, const char *message) { if (!value) { fprintf(stderr,"FAIL IPC: %s\n",message); std::exit(1); } }
bool until(const std::function<bool()> &condition, int timeout = 5000) {
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed()<timeout) { QCoreApplication::processEvents(QEventLoop::AllEvents,10); QThread::msleep(1); }
    return condition();
}
QByteArray request(const QString &action, const QJsonObject &extra = {}) {
    auto object = extra; object["protocolVersion"]=1; object["action"]=action;
    return QJsonDocument(object).toJson(QJsonDocument::Compact)+'\n';
}
void send(QLocalSocket &socket, const QString &name, const QByteArray &body) {
    socket.connectToServer(name);
    check(until([&](){return socket.state()==QLocalSocket::ConnectedState;}),"connect actual named pipe");
    socket.write(body); socket.flush();
}
QJsonObject receive(QLocalSocket &socket) {
    QByteArray bytes;
    check(until([&](){bytes+=socket.readAll(); return bytes.endsWith('\n');}),"receive complete JSON frame");
    const auto document=QJsonDocument::fromJson(bytes);
    check(document.isObject(),"valid JSON response"); return document.object();
}
QString code(const QJsonObject &object) { return object.value("error").toObject().value("code").toString(); }
}

void runWecomLocalServerTests(const QString &clientScript)
{
    QTemporaryDir directory; check(directory.isValid(),"isolated lock directory");
    const QString name="LemonWorkTool.Wecom.test."+QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVector<WecomLocalServer::Reply> pending; QDate requested; int reads=0;
    WecomLocalServer server([&](const QDate &date, WecomLocalServer::Reply reply) { requested=date; ++reads; pending.append(reply); });
    check(server.start(name,directory.path()),"start service");
    WecomLocalServer duplicate([](const QDate &,WecomLocalServer::Reply){});
    check(!duplicate.start(name,directory.path()),"second instance cannot steal or share endpoint");
    auto roundtrip=[&](const QByteArray &body) { QLocalSocket socket; send(socket,name,body); return receive(socket); };
    auto status=roundtrip(request("status"));
    check(status.value("ok").toBool() && status.value("data").toObject().value("documentId").toString()==WecomDocument::documentId() && reads==0,"status does not trigger login or read");
    check(code(roundtrip("bad json\n"))=="invalid_request","malformed JSON");
    check(code(roundtrip("{\"protocolVersion\":2,\"action\":\"read\"}\n"))=="unsupported_version","protocol version");
    check(code(roundtrip(request("delete")))=="unsupported_action","no document mutations");
    check(code(roundtrip(request("read",{{"url","https://example.test"}})))=="invalid_request","no arbitrary document or URL override");
    check(code(roundtrip(request("read",{{"fromDate",QDate::currentDate().addDays(-1).toString(Qt::ISODate)}})))=="invalid_date","no historical reads");
    check(code(roundtrip(request("read",{{"fromDate","not-a-date"}})))=="invalid_date","strict date input");
    check(code(roundtrip(QByteArray(8193,'x')))=="invalid_request","bounded request size");
    check(code(roundtrip(request("status")+request("read")))=="invalid_request","one request per connection");

    QLocalSocket fragmented; send(fragmented,name,"{\"protocolVersion\":1,");
    fragmented.write("\"action\":\"read\"}\n"); fragmented.flush();
    check(until([&](){return pending.size()==1;}),"fragmented read dispatched once");
    check(requested==QDate::currentDate() && server.property("sessionBusyWecomBridge").toBool(),"today default and busy guard");
    check(code(roundtrip(request("read")))=="busy" && reads==1,"concurrent request rejected");
    WecomDocument::Snapshot snapshot; snapshot.fromDate=requested; snapshot.fetchedAt=QDateTime::currentDateTimeUtc();
    WecomDocument::Sheet sheet; sheet.id="test"; sheet.name=QStringLiteral("合成中文数据"); sheet.error="simulated partial failure"; snapshot.sheets.append(sheet);
    auto first=pending.takeFirst(); first(WecomLocalServer::success(snapshot.toJson()));
    auto result=receive(fragmented);
    check(result.value("ok").toBool() && !result.value("data").toObject().value("complete").toBool(),"partial snapshot retains incomplete flag");
    check(!server.property("sessionBusyWecomBridge").toBool(),"busy clears after completion");
    first(WecomLocalServer::error("late","ignored"));

    QLocalSocket dropped; send(dropped,name,request("read")); check(until([&](){return pending.size()==1;}),"next fresh read");
    dropped.abort(); QCoreApplication::processEvents();
    check(code(roundtrip(request("read")))=="busy","disconnect does not start a duplicate reader");
    pending.takeFirst()(WecomLocalServer::error("read_failed","simulated failure"));
    check(!server.property("sessionBusyWecomBridge").toBool(),"late result to disconnected client is safe");
    QLocalSocket login; send(login,name,request("read")); check(until([&](){return pending.size()==1;}),"login-required request");
    pending.takeFirst()(WecomLocalServer::error("login_required",QStringLiteral("请扫码")));
    check(code(receive(login))=="login_required","login-required returned as structured error");

    // A separate PowerShell process uses the exact deployed caller with the default user endpoint.
    // Test mode appends a PID, so temporarily use the explicit production-form name in this harness.
    if (!clientScript.isEmpty()) {
        QString realName=WecomLocalServer::defaultServerName();
        realName.remove(".test."+QString::number(QCoreApplication::applicationPid()));
        WecomLocalServer scripted([&](const QDate &date, WecomLocalServer::Reply reply) {
            reply(WecomLocalServer::success({{"fromDate",date.toString(Qt::ISODate)}, {"text",QStringLiteral("中文内容与链接")},
                {"padding",QString(12000,'x')}, {"links",QJsonArray{"https://example.test/item"}}}));
        });
        // Never bind a live tool's default endpoint. The test caller gets a distinct Windows user SID
        // suffix via a test-only script copy; the production script exposes no endpoint override.
        const QString scriptName=realName+".testclient."+QString::number(QCoreApplication::applicationPid());
        check(scripted.start(scriptName,directory.path()),"isolated script test endpoint");
        QFile source(clientScript); check(source.open(QIODevice::ReadOnly),"read production client script");
        auto script=source.readAll(); script.replace("$identity.User.Value",("($identity.User.Value + '.testclient."+QString::number(QCoreApplication::applicationPid())+"')").toUtf8());
        QFile copy(directory.path()+"/client.ps1"); check(copy.open(QIODevice::WriteOnly) && copy.write(script)==script.size(),"create isolated caller copy"); copy.close();
        auto call=[&](const QStringList &arguments) {
            QProcess process; process.start("pwsh",QStringList{"-NoProfile","-File",copy.fileName()}+arguments);
            check(until([&](){return process.state()==QProcess::NotRunning;},20000),"PowerShell client completes");
            const auto object=QJsonDocument::fromJson(process.readAllStandardOutput()).object();
            check(!object.isEmpty(),"PowerShell emits machine-readable JSON only");
            return qMakePair(process.exitCode(),object);
        };
        auto info=call({"-Status"}); check(info.first==0 && info.second.value("ok").toBool(),"separate process status");
        auto data=call({}); check(data.first==0 && data.second.value("data").toObject().value("text").toString()==QStringLiteral("中文内容与链接")
            && data.second.value("data").toObject().value("padding").toString().size()==12000,"separate process multi-chunk Unicode JSON read");
        auto bad=call({"-FromDate","bad"}); check(bad.first==2 && code(bad.second)=="invalid_date","PowerShell error exit code");
    }
    fprintf(stdout,"PASS: named-pipe API (status, fresh read, concurrency, dates, limits, partial data, disconnect, login errors, PowerShell client).\n");
}
