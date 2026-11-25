#include "Common/WecomDocumentData.h"
#include "Common/WecomBrowser.h"
#include "Common/WecomDocumentReader.h"
#include "Common/WecomSessionStore.h"
#include "wecomdocumentwidget.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace WecomDocument;
void runWecomLocalServerTests(const QString &clientScript);
namespace {
int checks = 0;
void require(bool condition, const char *message) {
    ++checks;
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
QByteArray varint(quint64 value) {
    QByteArray result;
    while (value >= 128) { result.append(char((value & 127) | 128)); value >>= 7; }
    result.append(char(value)); return result;
}
QByteArray integer(int field, quint64 value) { return varint(quint64(field) << 3) + varint(value); }
QByteArray bytes(int field, const QByteArray &value) { return varint((quint64(field) << 3) | 2) + varint(value.size()) + value; }
QByteArray string(const char *text) { return bytes(1, QByteArray(text)); }
QByteArray number(double value) {
    QByteArray buffer(8, '\0'); std::memcpy(buffer.data(), &value, 8); return varint(9) + buffer;
}
QByteArray cell(int row, int column, int kind, int index, bool style = false) {
    auto data = integer(1, kind) + bytes(2, integer(1, index));
    if (style) data += bytes(4, integer(1, 0));
    return bytes(6, integer(1, row) + integer(2, column) + bytes(3, data));
}
QJsonObject metadata() {
    QJsonArray sheets;
    for (const auto &name : {"10-13", "10-14", "10-15", "project plan", "02-30", "2027-01-01"})
        sheets.append(QJsonObject{{"id", QString(name)}, {"name", QString(name)}});
    return {{"clientVars", QJsonObject{{"title", QString::fromUtf8(u8"2026-运营支持组项目进度表")},
        {"collab_client_vars", QJsonObject{{"padId", "pad"}, {"rev", 7},
        {"header", QJsonArray{QJsonObject{{"d", sheets}}}}}}}}};
}
QByteArray samplePayload(int unknownType = 0) {
    const auto run = bytes(3, string("review")) + bytes(7, bytes(11, string("https://example.test/review")));
    const auto pool = bytes(1, string("heading")) + bytes(2, bytes(3, run)) + bytes(3, number(46307)) + bytes(3, number(20260818));
    const auto style = bytes(1, bytes(7, integer(1, 300))) + bytes(2, bytes(3, string("yyyy-mm-dd;@")));
    const auto batch = bytes(4, style) + bytes(5, pool) + cell(0, 0, unknownType ? unknownType : 4, 0)
        + cell(3, 3, 6, 0) + cell(3, 8, 2, 129, true) + cell(5, 1, 2, 130) + cell(8, 0, 2, 128);
    return bytes(1, integer(1, 1) + bytes(5, integer(1, 18) + bytes(19, batch)));
}
QJsonObject response(const QString &id = "10-15", int unknownType = 0) {
    auto compressed = qCompress(samplePayload(unknownType), 9).mid(4).toBase64();
    return {{"retcode", 0}, {"data", QJsonObject{{"rev", 7}, {"padSubId", id}, {"async_open_done", true},
        {"startrow", 0}, {"endrow", -1}, {"initialAttributedText", QJsonObject{{"text", QJsonArray{QJsonObject{
        {"max_row", 10}, {"max_col", 10}, {"related_sheet", QString::fromLatin1(compressed)}}}}}}}}};
}
class FakeBrowser final : public WecomBrowser {
public:
    bool stopped = false;
    bool saved = false, deferSave = false;
    SessionReply saving;
    QUrl last;
    QVector<Reply> pending;
    void navigate(const QUrl &url) override { last = url; }
    void get(const QUrl &url, Reply reply) override { last = url; pending.append(reply); }
    void cancelRequests() override {}
    void stop() override { stopped = true; }
    bool hasSavedSession() const override { return saved; }
    void saveSession(SessionReply reply) override { if (deferSave) saving = reply; else reply({}); }
    void resolveSave(const QString &error = {}) { auto callback = saving; saving = {}; callback(error); }
    void bootstrap(const QString &id = documentId(), const QJsonObject &body = metadata()) {
        emit bootstrapReceived(QUrl("https://doc.weixin.qq.com/dop-api/opendoc?id=" + id), body);
    }
    void resolve(QJsonObject body, const QString &error = {}) { auto callback = pending.takeFirst(); callback(body, error); }
};
QJsonObject sessionCookie() {
    return {{"name", "LemonWecomPersistenceTest"}, {"value", "synthetic-secret-only-for-tests"},
        {"domain", "doc.weixin.qq.com"}, {"path", "/"}, {"expires", -1},
        {"secure", true}, {"httpOnly", true}, {"sameSite", 1}};
}
void sessionTests() {
    QTemporaryDir directory; require(directory.isValid(), "isolated session test directory");
    const QString path = directory.path()+"/session.dat";
    QString error; QJsonArray restored;
    auto cookie = sessionCookie();
    auto expired = cookie; expired["name"] = "expired"; expired["expires"] = 100;
    auto future = cookie; future["name"] = "persistent"; future["expires"] = QDateTime::currentSecsSinceEpoch()+3600.0;
    auto foreign = cookie; foreign["domain"] = ".example.test";
    auto parent = cookie; parent["domain"] = ".weixin.qq.com";
    auto hostOnly = cookie; hostOnly["domain"] = "weixin.qq.com";
    auto malformed = cookie; malformed.remove("expires");
    const QJsonArray input{cookie, expired, future, foreign, parent, hostOnly, malformed};
    require(WecomSessionStore::usableCookies(input, QDateTime::currentSecsSinceEpoch()).size()==3, "expired, foreign, nonmatching host-only and malformed cookies filtered");
    require(WecomSessionStore::save(path,input,&error), "current-user encrypted atomic save");
    QFile file(path); require(file.open(QIODevice::ReadOnly), "open encrypted cookie file");
    auto encrypted = file.readAll(); file.close();
    require(!encrypted.contains("synthetic-secret") && !encrypted.contains("weixin.qq.com"), "no plaintext cookie or domain on disk");
    require(WecomSessionStore::load(path,&restored,&error) && restored.size()==3, "encrypted snapshot reload");
    require(restored[0].toObject()==cookie && restored[1].toObject()==future, "session semantics, expiry and security attributes preserved");
    encrypted[encrypted.size()/2] = encrypted.at(encrypted.size()/2) ^ 0x1;
    require(file.open(QIODevice::WriteOnly|QIODevice::Truncate) && file.write(encrypted)==encrypted.size(), "write corrupted test snapshot"); file.close();
    require(!WecomSessionStore::load(path,&restored,&error) && restored.isEmpty() && !error.isEmpty(), "tampered backup fails closed");
    require(WecomSessionStore::load(directory.path()+"/missing.dat",&restored,&error) && restored.isEmpty(), "first run without backup");
    require(!WecomSessionStore::save(directory.path()+"/missing/session.dat",input,&error), "save failure reported");

    FakeBrowser browser; browser.saved=true; browser.deferSave=true;
    WecomDocumentReader reader(&browser); int logins=0, completed=0;
    QString saveResult;
    QObject::connect(&reader,&WecomDocumentReader::loginRequired,[&](){++logins;});
    QObject::connect(&reader,&WecomDocumentReader::finished,[&](const Snapshot &){++completed;});
    QObject::connect(&reader,&WecomDocumentReader::signInCompleted,[&](const QString &error){saveResult=error;});
    reader.read(defaultUrl(),QDate(2028,1,1));
    require(reader.isBusy() && logins==0 && browser.last==defaultUrl(), "restart tries saved login before prompting QR");
    browser.bootstrap();
    require(reader.isBusy() && bool(browser.saving) && completed==0, "exit guard spans encrypted save");
    browser.resolveSave(); require(reader.isBusy() && bool(browser.saving), "login saved before fetching data, completion saves refreshed cookies");
    browser.resolveSave(); require(!reader.isBusy() && completed==1, "finish only after session save");
    reader.signIn(defaultUrl()); browser.bootstrap();
    require(reader.isBusy() && logins==1, "manual sign-in also waits for save");
    auto stale=browser.saving; reader.cancel(); stale({});
    require(!reader.isBusy() && completed==1 && saveResult.isEmpty(), "cancel ignores delayed save callback");
    reader.signIn(defaultUrl()); browser.bootstrap(); browser.resolveSave("simulated storage failure");
    require(!reader.isBusy() && saveResult == "simulated storage failure", "sign-in completion reports encrypted save failure");
}
void standaloneReaderTests() {
    FakeBrowser browser;
    WecomDocumentReader reader(&browser);
    int prompts = 0, completed = 0;
    QEventLoop waitForPrompt;
    QObject::connect(&reader, &WecomDocumentReader::loginRequired, [&]() { ++prompts; waitForPrompt.quit(); });
    QObject::connect(&reader, &WecomDocumentReader::finished, [&](const Snapshot &) { ++completed; });
    reader.read(defaultUrl(), QDate(2028, 1, 1), WecomDocumentReader::LoginPrompt::AfterPageLoad);
    require(prompts == 0 && reader.isBusy(), "standalone waits for authorization even without a backup file");
    emit browser.navigationFinished(true);
    browser.bootstrap();
    require(prompts == 0 && completed == 1, "verified browser session reads without prompting");
    FakeBrowser expired;
    WecomDocumentReader expiredReader(&expired);
    QObject::connect(&expiredReader, &WecomDocumentReader::loginRequired, [&]() { ++prompts; waitForPrompt.quit(); });
    expiredReader.read(defaultUrl(), QDate(2028, 1, 1), WecomDocumentReader::LoginPrompt::AfterPageLoad);
    emit expired.navigationFinished(true);
    QTimer::singleShot(15000, &waitForPrompt, &QEventLoop::quit);
    waitForPrompt.exec();
    require(prompts == 1 && expiredReader.isBusy(), "unverified official page prompts for authorization");
    expiredReader.cancel();
}
void runTests() {
    require(sheetDate("10-15", 2026) == QDate(2026,10,15), "year from document");
    require(!sheetDate("02-30",2026).isValid(), "invalid calendar date");
    require(!sheetDate(QString::fromUtf8(u8"项目计划"),2026).isValid(), "skip non-date tab");
    require(sheetDate("2027-01-01",2026) == QDate(2027,1,1), "explicit year");
    QString error; Metadata meta;
    require(parseMetadata(metadata(), &meta, &error), "parse directory");
    auto selected = selectSheets(meta,QDate(2026,10,14));
    require(selected.size()==3 && selected[0].name=="10-14" && selected[2].date.year()==2027, "inclusive dates, sorted, all future");
    require(selectSheets(meta,QDate(2027,2,1)).isEmpty(), "no future sheet");
    require(validateUrl(defaultUrl(), &error), "fixed document accepted");
    require(!validateUrl(QUrl("https://doc.weixin.qq.com.evil.test/sheet/" + documentId()), &error), "lookalike host rejected");
    require(validateUrl(QUrl("https://doc.weixin.qq.com/sheet/other"), &error), "another WeCom spreadsheet accepted");
    require(!validateUrl(QUrl("https://doc.weixin.qq.com/doc/other"), &error), "non-spreadsheet link rejected");
    require(!validateUrl(QUrl("https://doc.weixin.qq.com/sheet/other%2Fextra"), &error), "nested path rejected");
    require(!validateUrl(QUrl("http://doc.weixin.qq.com/sheet/" + documentId()), &error), "http rejected");
    Sheet sheet; sheet.id="10-15"; sheet.name="10-15";
    require(parseSheet(response(), &sheet, &error), "verified protobuf types");
    require(sheet.loaded && sheet.rowCount==10 && sheet.columnCount==10 && sheet.cells.size()==5, "dimensions and sparse cells");
    require(sheet.cells[0].row==0 && sheet.cells[0].column==0 && sheet.cells[0].text=="heading", "zero index string");
    require(sheet.cells[1].row==3 && sheet.cells[1].column==3 && sheet.cells[1].links==QStringList{"https://example.test/review"}, "rich text link mapping");
    require(sheet.cells[2].text=="2026-10-12" && sheet.cells[2].rawNumber.toDouble()==46307, "formatted date and raw serial");
    require(sheet.cells[3].text=="20260818", "ordinary number not interpreted as date");
    require(sheet.cells[4].text=="128", "inline number boundary");
    require(!parseSheet(response("wrong"), &sheet, &error), "wrong worksheet rejected");
    require(!parseSheet(response("10-15",5), &sheet, &error), "unknown formula fails explicitly");
    auto incomplete=response(); auto data=incomplete["data"].toObject(); data["async_open_done"]=false; incomplete["data"]=data;
    require(parseSheet(incomplete,&sheet,&error), "normal synchronous response may have async_done false");
    incomplete["retcode"]=200156;
    require(!parseSheet(incomplete,&sheet,&error), "async polling response not exported as complete");
    auto corrupt=response(); data=corrupt["data"].toObject();
    const QJsonObject corruptText{{"max_row",10},{"max_col",10},{"related_sheet","not-base64!"}};
    data["initialAttributedText"]=QJsonObject{{"text",QJsonArray{corruptText}}}; corrupt["data"]=data;
    require(!parseSheet(corrupt,&sheet,&error), "malformed base64 rejected");
    auto empty=response(); data=empty["data"].toObject(); data["endrow"]=-1;
    auto emptyCommand=bytes(1,integer(1,1)+bytes(3,{})+bytes(4,string("3.6.6")));
    data["initialAttributedText"]=QJsonObject{{"text",QJsonArray{QJsonObject{{"max_row",200},{"max_col",35},
        {"related_sheet",QString::fromLatin1(qCompress(emptyCommand).mid(4).toBase64())}}}}}; empty["data"]=data;
    Sheet emptySheet; emptySheet.id="10-15";
    require(parseSheet(empty,&emptySheet,&error) && emptySheet.loaded && emptySheet.cells.isEmpty() && emptySheet.rowCount==200,"verified complete empty worksheet");
    data["endrow"]=60; empty["data"]=data;
    require(!parseSheet(empty,&emptySheet,&error),"empty partial chunk not mistaken for full worksheet");
    auto partial=response(); data=partial["data"].toObject(); data["endrow"]=60; partial["data"]=data;
    require(!parseSheet(partial,&emptySheet,&error),"nonempty partial response not mistaken for full worksheet");
    Snapshot snapshot; snapshot.sheets={sheet}; snapshot.title="example"; snapshot.fromDate=QDate(2026,10,14);
    require(snapshot.toJson()["complete"].toBool(), "complete export"); snapshot.cancelled=true;
    require(!snapshot.toJson()["complete"].toBool(), "cancelled export incomplete");
    const auto json=QJsonDocument(snapshot.toJson()).toJson();
    require(!json.contains("xsrf") && !json.contains("Cookie") && !json.contains("scode"), "export has no session fields");

    FakeBrowser browser; WecomDocumentReader reader(&browser);
    int logins=0, finished=0; Snapshot result;
    QObject::connect(&reader,&WecomDocumentReader::loginRequired,[&](){++logins;});
    QObject::connect(&reader,&WecomDocumentReader::finished,[&](const Snapshot &s){++finished;result=s;});
    reader.read(defaultUrl(),QDate(2026,10,15)); require(reader.isBusy() && logins==1,"read waits for login");
    browser.bootstrap(); require(browser.pending.size()==1,"login starts first selected sheet");
    require(QUrlQuery(browser.last).queryItemValue("subId")=="10-15","requested matching sheet");
    browser.resolve(response()); require(reader.isBusy(),"busy spans sheets");
    reader.cancel(); require(!reader.isBusy() && finished==1 && result.cancelled,"cancel returns partial result");
    require(result.sheets[0].loaded && !result.sheets[1].loaded && !result.sheets[1].error.isEmpty(),"partial statuses retained");
    browser.resolve(response("2027-01-01")); require(finished==1,"late callback cannot mutate cancelled result");
    reader.read(defaultUrl(),QDate(2026,10,15)); require(logins==1 && browser.pending.size()==1,"reuse isolated login for fresh directory");
    browser.resolve(metadata()); auto changed=response(); data=changed["data"].toObject(); data["rev"]=8; changed["data"]=data;
    browser.resolve(changed); browser.resolve(response("2027-01-01"));
    require(finished==2 && !result.sheets[0].loaded && !result.sheets[0].error.isEmpty() && result.sheets[1].loaded,"revision mismatch explicitly partial");
    require(!reader.property("sessionBusyWecomRead").toBool(),"busy cleared on completion");
    reader.read(defaultUrl(),QDate(2026,10,15)); browser.resolve(metadata());
    browser.resolve(QJsonObject{{"retcode",200156},{"data",QJsonObject{{"async_poll_interval",500}}}});
    { QEventLoop loop; QTimer::singleShot(600,&loop,&QEventLoop::quit); loop.exec(); }
    require(browser.pending.size()==1 && QUrlQuery(browser.last).queryItemValue("async")=="1","200156 enters explicit async polling");
    auto pending=response(); data=pending["data"].toObject(); data["async_open_done"]=false; data["async_poll_interval"]=500; pending["data"]=data;
    browser.resolve(pending); require(reader.isBusy() && finished==2,"async incomplete data not consumed");
    reader.cancel();
    { QEventLoop loop; QTimer::singleShot(600,&loop,&QEventLoop::quit); loop.exec(); }
    require(browser.pending.isEmpty() && finished==3,"cancelled async timer does not restart reading");
    reader.read(defaultUrl(),QDate(2028,1,1)); browser.resolve(metadata());
    require(!reader.isBusy() && result.sheets.isEmpty() && finished==4,"zero matching tables completes without fetching history");
    const QUrl nextDocument("https://doc.weixin.qq.com/sheet/second_document?scode=test");
    reader.read(nextDocument, QDate(2028,1,1));
    require(browser.last == nextDocument && browser.pending.isEmpty(), "switch navigates to full new link instead of reusing directory");
    browser.bootstrap();
    require(reader.isBusy() && finished == 4, "old document bootstrap ignored after switch");
    browser.bootstrap("second_document");
    require(finished == 5 && result.toJson().value("documentId").toString() == "second_document", "export identifies the selected document");
    emit browser.browserError(QStringLiteral("synthetic browser failure"));
    reader.read(nextDocument, QDate(2028,1,1));
    require(browser.pending.isEmpty() && browser.last == nextDocument, "idle browser failure invalidates the request cache");
    browser.bootstrap("second_document");
    reader.read(nextDocument, QDate(2028,1,1));
    require(browser.pending.size() == 1, "recovered browser can refresh the directory");
    emit browser.browserError(QStringLiteral("synthetic browser failure while reading"));
    require(!reader.isBusy() && browser.stopped, "active browser failure releases the read");
    browser.resolve(metadata());
    require(finished == 6, "late reply from the failed browser is ignored");
    reader.read(nextDocument, QDate(2028,1,1));
    require(browser.pending.isEmpty() && browser.last == nextDocument, "retry navigates with a new browser after a failed read");
    browser.bootstrap("second_document");
    require(finished == 7 && !reader.isBusy(), "retry completes");
}

// Exercises the actual reader with an older tab pinned in the share URL.
void latestSheetTests()
{
    const Document document{QStringLiteral("2026-站会总结表"),
        QUrl("https://doc.weixin.qq.com/sheet/standup_test?tab=old"), SheetSelection::LatestDate};
    const QJsonArray sheets{QJsonObject{{"id", "older"}, {"name", "11.20"}},
        QJsonObject{{"id", "new"}, {"name", QStringLiteral("站会 11月22日")}},
        QJsonObject{{"id", "old"}, {"name", "2026-11-09"}},
        QJsonObject{{"id", "invalid"}, {"name", "11-31"}},
        QJsonObject{{"id", "overview"}, {"name", QStringLiteral("汇总")}}};
    const QJsonObject collab{{"padId", "standup"}, {"rev", 7}, {"header", QJsonArray{QJsonObject{{"d", sheets}}}}};
    const QJsonObject body{{"clientVars", QJsonObject{{"title", QStringLiteral("站会总结表")}, {"collab_client_vars", collab}}}};
    FakeBrowser browser;
    WecomDocumentReader reader(&browser);
    Snapshot result;
    QObject::connect(&reader, &WecomDocumentReader::finished, [&](const Snapshot &snapshot) { result = snapshot; });
    reader.read(document, QDate(2026, 12, 1));
    browser.bootstrap("standup_test", body);
    require(browser.pending.size() == 1 && QUrlQuery(browser.last).queryItemValue("subId") == "new",
        "stand-up requests only the newest dated sheet regardless of the pinned tab, tab order or current date");
    browser.resolve(response("new"));
    require(!reader.isBusy() && browser.pending.isEmpty() && result.sheets.size() == 1
        && result.sheets.first().loaded && result.sheets.first().date == QDate(2026, 11, 22),
        "stand-up completes with one loaded page and resolves the year from the configured name");
    require(result.toJson().value("sheetSelection") == QJsonValue("latest-date") && !result.toJson().contains("fromDate"),
        "stand-up export identifies its filter and omits the unused date threshold");
}

void accountingProgressDateTests()
{
    const Document document{QStringLiteral("2026-记账系统项目进度表"),
        QUrl("https://doc.weixin.qq.com/sheet/accounting_test"), SheetSelection::FromDateInName};
    const QJsonArray sheets{
        QJsonObject{{"id", "future"}, {"name", QStringLiteral("记账-11-26（上线）")}},
        QJsonObject{{"id", "past"}, {"name", QStringLiteral("11-23 已发布")}},
        QJsonObject{{"id", "today"}, {"name", QStringLiteral("11-24 需求")}},
        QJsonObject{{"id", "dot"}, {"name", QStringLiteral("V1.2.3 汇总 11.27")}},
        QJsonObject{{"id", "next-year"}, {"name", QStringLiteral("计划 2027年1月6日")}},
        QJsonObject{{"id", "invalid"}, {"name", QStringLiteral("11月31日")}},
        QJsonObject{{"id", "overview"}, {"name", QStringLiteral("说明与版本 1.2.3")}},
        QJsonObject{{"id", "plain"}, {"name", "11-25"}}};
    const QJsonObject collab{{"padId", "accounting"}, {"rev", 7}, {"header", QJsonArray{QJsonObject{{"d", sheets}}}}};
    const QJsonObject body{{"clientVars", QJsonObject{{"title", document.name}, {"collab_client_vars", collab}}}};
    const QDate start(2026, 11, 24);
    Metadata parsed;
    QString error;
    require(parseMetadata(body, &parsed, &error), "legacy progress parser can read the same directory");
    const auto legacy = selectSheets(parsed, start);
    require(legacy.size() == 1 && legacy.first().id == "plain", "legacy progress ignores dates within descriptive tab names");

    FakeBrowser browser;
    WecomDocumentReader reader(&browser);
    Snapshot result;
    QObject::connect(&reader, &WecomDocumentReader::finished, [&](const Snapshot &snapshot) { result = snapshot; });
    reader.read(document, start);
    browser.bootstrap("accounting_test", body);
    const QStringList expected{"today", "plain", "future", "dot", "next-year"};
    for (const auto &id : expected) {
        require(browser.pending.size() == 1 && QUrlQuery(browser.last).queryItemValue("subId") == id,
            "accounting progress requests each matching tab in date order");
        browser.resolve(response(id));
    }
    const auto json = result.toJson();
    require(browser.pending.isEmpty() && result.sheets.size() == expected.size()
        && json.value("fromDate") == QJsonValue("2026-11-24") && json.value("sheetSelection") == QJsonValue("from-date-in-name"),
        "accounting progress keeps the inclusive cutoff and reports its strategy");

    const QUrl accountingUrl("https://doc.weixin.qq.com/sheet/accounting_test");
    QTemporaryDir directory;
    WecomDocumentCatalog catalog(directory.filePath("WecomDocuments.json"));
    require(catalog.save(document, false, &error)
        && catalog.save({QStringLiteral("2026-记账系统"), accountingUrl}, true, &error)
        && catalog.load().isEmpty() && catalog.entryForRead("accounting_test").selection == SheetSelection::FromDateInName,
        "saving or renaming a catalog entry preserves the named-date strategy");
}

void catalogTests()
{
    QTemporaryDir directory;
    require(directory.isValid(), "isolated catalog test directory");
    const QString path = directory.filePath("WecomDocuments.json");
    WecomDocumentCatalog first(path), second(path);
    require(first.load().isEmpty() && first.urlForId(documentId()) == defaultUrl(), "missing catalog keeps default document");
    require(second.load().isEmpty(), "second instance initially reads the same catalog");
    const QUrl other("https://doc.weixin.qq.com/sheet/second_document?scode=test%2Bvalue");
    const QUrl updatedDefault(defaultUrl().toString() + "?scode=replacement");
    QString error;
    require(first.save({QStringLiteral("第二张表"), other, SheetSelection::LatestDate}, false, &error), "save a new binding");
    require(second.save({QStringLiteral("项目进度表"), updatedDefault}, true, &error), "update a named binding");
    require(first.load().isEmpty() && first.entries().size() == 2, "another instance's binding is preserved");
    require(first.urlForId("second_document") == other && first.urlForId(documentId()) == updatedDefault, "full links survive reload and lookup");
    const auto newLink = QUrl("https://doc.weixin.qq.com/sheet/new_sheet?tab=one");
    const auto changedShare = QUrl("https://doc.weixin.qq.com/sheet/second_document?scode=new-copy");
    const auto known = first.entryForUrl(changedShare);
    require(known.selection == SheetSelection::LatestDate && known.url == changedShare
        && first.entryForUrl(newLink).selection == SheetSelection::All && first.urlForId("new_sheet").isEmpty(),
        "manual read resolves by ID and leaves new links temporary");
    require(first.save({QStringLiteral("第二张表改名"), other}, true, &error) && first.load().isEmpty()
        && first.entryForRead("second_document").selection == SheetSelection::LatestDate
        && first.entryForRead({}).selection == SheetSelection::FromDate,
        "renaming a binding preserves its selection and the legacy default");
    require(!first.save({QStringLiteral("重复表格"), other}, false, &error), "a duplicate ID needs confirmation");
    const QString userPath = directory.filePath("user/WecomDocuments.json");
    WecomDocumentCatalog userCatalog(userPath);
    const QUrl userDocument("https://doc.weixin.qq.com/sheet/user_document");
    require(userCatalog.save({QStringLiteral("用户表格"), userDocument}, false, &error), "seed user fallback catalog");
    WecomDocumentCatalog preferred(path, userPath);
    require(preferred.load().isEmpty() && preferred.urlForId("second_document") == other
        && preferred.urlForId("user_document").isEmpty(), "program catalog takes priority");
    QFile corrupt(path);
    require(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate), "open invalid catalog fixture");
    const QByteArray contents = "{invalid";
    require(corrupt.write(contents) == contents.size(), "write invalid catalog fixture");
    corrupt.close();
    require(!first.save({QStringLiteral("新表"), other}, true, &error), "invalid catalog is preserved after failed save");
    require(corrupt.open(QIODevice::ReadOnly) && corrupt.readAll() == contents, "invalid catalog never overwritten");
    corrupt.close();
    require(!preferred.load().isEmpty() && preferred.urlForId("user_document").isEmpty(), "invalid program catalog reports error without loading fallback");
    require(QFile::remove(path) && preferred.load().isEmpty()
        && preferred.urlForId("user_document") == userDocument, "missing program catalog loads user fallback");
    require(preferred.save({QStringLiteral("第二张表"), other}, false, &error)
        && !QFile::exists(path) && userCatalog.load().isEmpty() && userCatalog.urlForId("second_document") == other,
        "changing active user catalog keeps it as the source");
}

void catalogWidgetTests()
{
    QTemporaryDir directory;
    require(directory.isValid(), "isolated catalog UI directory");
    const QString path = directory.filePath("WecomDocuments.json");
    WecomDocumentWidget widget(nullptr, path);
    widget.setAttribute(Qt::WA_DontShowOnScreen);
    widget.show();
    auto *selector = widget.findChild<QComboBox*>("wecomDocumentSelector");
    auto *add = widget.findChild<QPushButton*>("btnWecomAddDocument");
    auto *manual = widget.findChild<QPushButton*>("btnWecomManualAccess");
    require(selector && !selector->isEditable() && selector->count() == 1 && add && manual, "saved selector and separate visit/save actions");
    const QString name = QStringLiteral("演示表格");
    const QString link = "https://doc.weixin.qq.com/sheet/demo_document?share_code=test-value";
    QTimer::singleShot(0, &widget, [&]() {
        auto *dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        require(dialog, "add opens modal dialog");
        dialog->findChild<QLineEdit*>("wecomDocumentNameInput")->setText(name);
        dialog->findChild<QLineEdit*>("wecomDocumentLinkInput")->setText(link);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    add->click();
    require(selector->count() == 2 && selector->currentText() == name && selector->currentData().toUrl() == QUrl(link),
        "saved table appears by name and is selected");
    const auto docs = widget.assistantDocuments().value("data").toObject().value("documents").toArray();
    require(docs.size() == 2 && docs.last().toObject() == QJsonObject{{"id", "demo_document"}, {"name", name}, {"sheetSelection", "all"}},
        "CLI list uses the saved catalog and omits share parameters");
    QJsonObject unknown;
    widget.readForAssistant(QDate::currentDate(), "missing_document", {}, [&](const QJsonObject &reply) { unknown = reply; });
    require(unknown.value("error").toObject().value("code") == "document_not_found" && selector->currentText() == name,
        "an unknown CLI ID cannot change the GUI selection or start a read");
    WecomDocumentWidget reopened(nullptr, path);
    require(reopened.findChild<QComboBox*>("wecomDocumentSelector")->findText(name) >= 0, "reopened page loads saved binding");
    QTimer::singleShot(0, &widget, [&]() {
        auto *dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        require(dialog && dialog->findChild<QLineEdit*>("wecomDocumentLinkInput")->isVisible()
            && !dialog->findChild<QLineEdit*>("wecomDocumentNameInput")->isVisible(), "manual visit requires only a link");
        dialog->reject();
    });
    manual->click();
    require(selector->count() == 2, "cancelled visit leaves saved choices alone");

    QFile brokenCatalog(path);
    require(brokenCatalog.open(QIODevice::WriteOnly | QIODevice::Truncate), "replace test catalog with malformed JSON");
    require(brokenCatalog.write("{ invalid") == 9, "write malformed catalog");
    brokenCatalog.close();
    require(widget.assistantDocuments().value("error").toObject().value("code") == "catalog_error",
        "desktop lists report malformed catalogs like standalone CLI");
    QJsonObject unavailable;
    widget.readForAssistant(QDate::currentDate(), "demo_document", {}, [&](const QJsonObject &reply) { unavailable = reply; });
    require(unavailable.value("error").toObject().value("code") == "catalog_error",
        "desktop CLI reads do not use stale bindings after catalog corruption");
}
void allSheetTests()
{
    const QJsonArray sheets{QJsonObject{{"id", "overview"}, {"name", QStringLiteral("汇总")}},
        QJsonObject{{"id", "future"}, {"name", "2027-01-01"}},
        QJsonObject{{"id", "past"}, {"name", "2020-01-01"}},
        QJsonObject{{"id", "notes"}, {"name", QStringLiteral("说明")}}};
    const QJsonObject collab{{"padId", "manual"}, {"rev", 7}, {"header", QJsonArray{QJsonObject{{"d", sheets}}}}};
    const auto url = QUrl("https://doc.weixin.qq.com/sheet/new_sheet?scode=mock-share&tab=notes");
    const QJsonObject body{{"clientVars", QJsonObject{{"title", QStringLiteral("资料表")}, {"collab_client_vars", collab}}}};
    FakeBrowser browser;
    WecomDocumentReader reader(&browser);
    Snapshot result;
    QObject::connect(&reader, &WecomDocumentReader::finished, [&](const Snapshot &snapshot) { result = snapshot; });
    reader.read(WecomDocumentCatalog().entryForUrl(url), QDate(2026, 11, 24));
    browser.bootstrap("new_sheet", body);
    const QStringList order{"overview", "future", "past", "notes"};
    for (const auto &id : order) {
        require(browser.pending.size() == 1 && QUrlQuery(browser.last).queryItemValue("subId") == id,
            "all-sheet strategy requests every tab in workbook order");
        browser.resolve(response(id));
    }
    const auto json = result.toJson();
    require(json.value("complete").toBool() && json.value("sheetSelection") == QJsonValue("all")
        && !json.contains("fromDate") && !QJsonDocument(json).toJson().contains("mock-share"),
        "all-sheet export includes non-date tabs and omits date bounds and share codes");
}
void captureTests(const QString &directory) {
    QFile file(directory + "/metadata.json"); require(file.open(QIODevice::ReadOnly),"open capture metadata fixture");
    Metadata metadata; QString error;
    require(parseMetadata(QJsonDocument::fromJson(file.readAll()).object(),&metadata,&error),"real metadata parse");
    require(metadata.year==2026 && metadata.sheets.size()==16,"real year and 16 sheets");
    int count=0;
    for (const auto &name : QDir(directory).entryList({"sheet-*.json"},QDir::Files)) {
        QFile dataFile(directory+"/"+name); require(dataFile.open(QIODevice::ReadOnly),"open real sheet fixture");
        const auto response=QJsonDocument::fromJson(dataFile.readAll()).object();
        Sheet sheet; sheet.id=response["data"].toObject()["padSubId"].toString();
        if (!parseSheet(response,&sheet,&error)) fprintf(stderr,"capture parse error: %s\n",error.toUtf8().constData());
        require(sheet.loaded,"real captured full sheet parse");
        const int expected = sheet.columnCount==35 ? 165 : 1763;
        require(sheet.cells.size()==expected,"real cell count matches independent chunk/full verification");
        int links=0, dates=0;
        for (const auto &cell : sheet.cells) {links+=cell.links.size();if(cell.rawNumber.isDouble() && cell.text.contains('-'))++dates;}
        require(links==(sheet.columnCount==35?13:83),"real hyperlink count");
        if (sheet.columnCount==35) require(dates==25,"25 dash-formatted date cells");
        ++count;
    }
    require(count==2,"both real sheet fixtures tested");
}
}
int main(int argc,char **argv) {
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc,argv);
    const auto arguments=app.arguments();
    const bool recoverySmoke = arguments.contains("--session-recovery-smoke");
    if (arguments.contains("--session-smoke") || recoverySmoke) {
        const QString profile = QCoreApplication::applicationDirPath()+"/session-smoke-"+QString::number(QDateTime::currentMSecsSinceEpoch());
        require(QDir().mkpath(profile), "create isolated WebView session profile");
        auto persistent=sessionCookie(); persistent["name"]="LemonWecomPersistentTest";
        persistent["expires"]=QDateTime::currentSecsSinceEpoch()+3600.0;
        const QJsonArray original{sessionCookie(), persistent}; QString error;
        require(WecomSessionStore::save(profile+"/session.dat",original,&error), "seed encrypted synthetic restart snapshot");
        WecomBrowser browser(nullptr,profile);
        bool retrying = false;
        QObject::connect(&browser,&WecomBrowser::browserError,&app,[&](const QString &message) {
            if (!recoverySmoke || retrying) {
                fprintf(stderr, "BROWSER: %s\n", message.toUtf8().constData());
                app.exit(2);
                return;
            }
            retrying = true;
            QTimer::singleShot(0, &app, [&]() { browser.navigate(defaultUrl()); });
        });
        bool saving=false;
        QObject::connect(&browser,&WecomBrowser::sessionStatus,&app,[&](const QString &){
            if (saving) return;
            // Verify actual WebView2 import/export without making a network request.
            browser.stop();
            if (recoverySmoke && !retrying) {
                fprintf(stdout, "READY: %s\n", profile.toUtf8().constData());
                fflush(stdout);
                return;
            }
            saving=true;
            browser.saveSession([&](const QString &error) {
                QJsonArray restored; QString loadError;
                require(error.isEmpty() && WecomSessionStore::load(profile+"/session.dat",&restored,&loadError), "WebView2 cookie manager exports restored session");
                require(restored.size()==2 && restored.contains(original[0]) && restored.contains(original[1]), "WebView2 roundtrip preserves session/persistent cookies, HttpOnly, Secure, SameSite and expiry");
                fprintf(stdout,"PASS: real WebView2 encrypted session restore/save (%d checks, synthetic credentials, no network).\n",checks);
                app.exit(0);
            });
        });
        QTimer::singleShot(30000,&app,[&](){fprintf(stderr,"session restore timeout\n");app.exit(3);});
        browser.navigate(defaultUrl()); return app.exec();
    }
    if (arguments.contains("--browser-smoke")) {
        WecomBrowser browser(nullptr, QCoreApplication::applicationDirPath()+"/browser-smoke-profile");
        browser.resize(1000,700); browser.setAttribute(Qt::WA_DontShowOnScreen); browser.show();
        QObject::connect(&browser,&WecomBrowser::browserError,&app,[&](const QString &error){fprintf(stderr,"BROWSER: %s\n",error.toUtf8().constData());app.exit(2);});
        QObject::connect(&browser,&WecomBrowser::navigationFinished,&app,[&](bool ok){
            if (ok) {fprintf(stdout,"PASS: WebView2 initialized and official document page loaded (QR sign-in not completed).\n"); app.exit(0);}
        });
        QTimer::singleShot(45000,&app,[&](){fprintf(stderr,"BROWSER: navigation timeout\n");app.exit(3);});
        browser.navigate(defaultUrl()); return app.exec();
    }
    sessionTests(); standaloneReaderTests(); runTests(); latestSheetTests(); accountingProgressDateTests();
    allSheetTests(); catalogTests(); catalogWidgetTests();
    runWecomLocalServerTests(arguments.contains("--client-script") ? arguments.value(arguments.indexOf("--client-script")+1) : QString());
    if (arguments.contains("--capture-dir")) captureTests(arguments.value(arguments.indexOf("--capture-dir")+1));
    WecomDocumentWidget widget; widget.resize(1000,650); widget.setAttribute(Qt::WA_DontShowOnScreen); widget.show(); app.processEvents();
    require(widget.findChild<QPushButton*>("btnWecomRead") && widget.findChild<QPushButton*>("btnWecomLogin"),"page actions exist");
    require(!widget.findChild<QPushButton*>("btnWecomExport")->isEnabled(),"export unavailable before read");
    if (arguments.contains("--check-packaged-catalog")) {
        const QString standup = QStringLiteral("2026-运营支持组站会总结表");
        auto *selector = widget.findChild<QComboBox*>("wecomDocumentSelector");
        require(selector && selector->findText(standup) >= 0, "packaged dropdown contains the stand-up table");
        selector->setCurrentIndex(selector->findText(standup));
        require(widget.findChild<QLabel*>("wecomReadStatus")->text().contains(QStringLiteral("最新的一页")),
            "packaged stand-up item uses the newest page strategy");
        const auto documents = widget.assistantDocuments().value("data").toObject().value("documents").toArray();
        require(documents.contains(QJsonObject{{"id", "e3_ARwAqwY-AAkCNsNndJmHGR5K578aV"}, {"name", standup},
            {"sheetSelection", "latest-date"}}), "packaged CLI metadata exposes the stand-up item without its share address");
        const QString progress = QStringLiteral("2026-记账系统项目进度表");
        const QString progressId = QStringLiteral("e3_AKcA1gZhAA0CNHYIuso0NR2O0Y8CZ");
        require(selector->findText(progress) >= 0
            && documents.contains(QJsonObject{{"id", progressId}, {"name", progress}, {"sheetSelection", "from-date-in-name"}}),
            "accounting progress is available in the dropdown and CLI under its document ID");
        WecomDocumentCatalog packaged;
        require(packaged.load().isEmpty() && packaged.entryForRead(progressId).selection == SheetSelection::FromDateInName,
            "accounting progress loads its date-in-name strategy");
    }
    widget.findChild<QPushButton*>("btnWecomRead")->setFocus();
    if (arguments.contains("--screenshot")) {
        const auto path=arguments.value(arguments.indexOf("--screenshot")+1);
        require(widget.grab().save(path),"save tool page screenshot");
    }
    fprintf(stdout,"PASS: %d checks (dates, protocol, links, export, cancellation, revision, UI)\n",checks);
    return 0;
}
