#include <QtTest>
#include <QtWidgets>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include "Common/subjectbalanceimport.h"
#include "Common/globaldata.h"
#include "Common/importerhostselector.h"
#include "subjectbalanceimportwidget.h"
#include "Common/subjectbalancefiles.h"
#include "startupwindow.h"
#include "pwdloginwidget.h"
#include "../LemonWorkToolCli/networkutils.h"

namespace {
const QString taskId = "ddda7f90-6c4d-4a14-a876-2cb9104d4123";

QJsonArray fileRequirements()
{
    QFile file(QFINDTESTDATA("fixtures/subject-balance-requirements.json"));
    if (!file.open(QIODevice::ReadOnly)) qFatal("Missing requirements fixture");
    return QJsonDocument::fromJson(file.readAll()).array();
}

QByteArray requirementsBody(const QJsonArray &rules = fileRequirements())
{
    return QJsonDocument(QJsonObject{{"state", 1000}, {"data", rules}}).toJson();
}

QList<BalanceImportSoftware> testCatalog()
{
    return SubjectBalanceImport::applyRequirements(fileRequirements());
}

QByteArray resultBody(int status, const QString &id = taskId)
{
    const QJsonObject detail{{"Id", id}, {"Status", status}, {"AsStartDate", "2025-11-01T00:00:00"},
        {"Error_Msg", status == 2 || status == 3 ? "sample error" : ""}, {"Reminder_Msg", "sample warning"}, {"AppAsId", "123"}};
    return QJsonDocument(QJsonObject{{"State", 1000}, {"Data", QJsonObject{{"Code", 0}, {"Data", detail}}}}).toJson();
}

class FakeServer : public QTcpServer
{
public:
    QList<QByteArray> requests;
    std::function<QByteArray(const QByteArray&)> response;
    int httpStatus = 200;
    FakeServer() {
        connect(this, &QTcpServer::newConnection, this, [this]() {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                    QByteArray data = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", data);
                    const int end = data.indexOf("\r\n\r\n");
                    if (end < 0 || socket->property("handled").toBool()) return;
                    qint64 length = 0;
                    for (const auto &line : data.left(end).split('\n'))
                        if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toLongLong();
                    if (data.size() < end + 4 + length) return;
                    socket->setProperty("handled", true);
                    requests.append(data);
                    if (!response) return;
                    const QByteArray body = response(data);
                    socket->write("HTTP/1.1 " + QByteArray::number(httpStatus) + " Test\r\nContent-Type: application/json\r\nContent-Length: "
                        + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
        if (!listen(QHostAddress::LocalHost)) qFatal("Cannot listen on loopback");
    }
    QString host() const { return "http://127.0.0.1:" + QString::number(serverPort()); }
};

void prepareRequirements(BalanceImportRequirements &requirements, FakeServer &server)
{
    server.response = [](const QByteArray &) { return requirementsBody(); };
    requirements.load(server.host());
    QTRY_VERIFY(!requirements.loading());
    QVERIFY(!requirements.catalog().isEmpty());
}
}

class SubjectBalanceImportTests : public QObject
{
    Q_OBJECT
    QTemporaryDir m_temp;
    QString sample(const QString &name = QStringLiteral("原始 表格.xlsx")) {
        const QString path = m_temp.filePath(name);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) file.write("ORIGINAL-SAMPLE-BYTES");
        return path;
    }
    BalanceImportSession session(const QString &host) {
        BalanceImportSession s; s.host = host; s.token = "test-session-token"; s.serviceId = 45; s.owner = "test-owner"; return s;
    }
    BalanceImportInput input() {
        BalanceImportInput i;
        i.software = 99999; i.accountName = QStringLiteral("测试 + & # 账套"); i.startMonth = QDate(2026, 1, 25);
        i.accountingStandard = 1; i.taxType = 2; i.files.insert("trialbalance", sample()); return i;
    }
private slots:
    void initTestCase() {
        QVERIFY(m_temp.isValid());
        qputenv("APPDATA", m_temp.path().toUtf8());
        qputenv("LOCALAPPDATA", m_temp.path().toUtf8());
        QStandardPaths::setTestModeEnabled(true);
        lemoncofighelper::SetHttpEnvironment("Local");
    }

    void catalogAndValidation() {
        QString error;
        const auto catalog = SubjectBalanceImport::applyRequirements(fileRequirements(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(catalog.size(), 70);
        QSet<int> ids;
        for (const auto &software : catalog) {
            QVERIFY(!ids.contains(software.id)); ids.insert(software.id);
            auto i = input(); i.software = software.id; i.files.clear();
            for (const auto &file : software.files) {
                QVERIFY(!file.extensions.isEmpty());
                i.files.insert(file.key, sample(file.key + "." + file.extensions.first()));
            }
            QVERIFY2(SubjectBalanceImport::validate(i, session("http://127.0.0.1"), testCatalog()).isEmpty(), qPrintable(software.name));
            for (const auto &file : software.files) if (file.required) {
                auto missing = i; missing.files.remove(file.key);
                QVERIFY2(!SubjectBalanceImport::validate(missing, session("http://127.0.0.1"), testCatalog()).isEmpty(), qPrintable(software.name + file.key));
            }
        }
        auto i = input(); i.software = 22001; i.files.clear(); i.files["subject"] = sample("subjects.txt");
        QVERIFY(SubjectBalanceImport::validate(i, session("http://127.0.0.1"), testCatalog()).isEmpty()); // Special combinations are checked by the server.
        i.files["trialbalance"] = sample();
        QVERIFY(SubjectBalanceImport::validate(i, session("http://127.0.0.1"), testCatalog()).isEmpty());
        i.files["voucher"] = sample();
        QVERIFY(!SubjectBalanceImport::validate(i, session("http://127.0.0.1"), testCatalog()).isEmpty());
        i = input(); i.files["trialbalance"] = m_temp.path();
        QVERIFY(!SubjectBalanceImport::validate(i, session("http://127.0.0.1"), testCatalog()).isEmpty());
        i = input(); i.files["trialbalance"] = sample("bad.zip");
        QVERIFY(!SubjectBalanceImport::validate(i, session("http://127.0.0.1"), testCatalog()).isEmpty());
    }

    void dynamicRequirementsAndValidation() {
        auto rules = QJsonArray{QJsonObject{{"softwareId", 99999}, {"softwareName", "Template"},
            {"subjectStatus", 1}, {"trialBalanceStatus", 4}, {"voucherStatus", 3}, {"remark", "server-only"}}};
        QString error;
        const auto catalog = SubjectBalanceImport::applyRequirements(rules, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        auto i = input(); i.files.clear();
        QVERIFY(!SubjectBalanceImport::validate(i, session("http://127.0.0.1"), catalog).isEmpty());
        i.files["subject"] = sample();
        QVERIFY(SubjectBalanceImport::validate(i, session("http://127.0.0.1"), catalog).isEmpty());
        i.files["voucher"] = sample();
        QVERIFY(!SubjectBalanceImport::validate(i, session("http://127.0.0.1"), catalog).isEmpty());
        i.files.remove("voucher");
        i.software = 2001; // Missing software never falls back to a local upload requirement.
        QVERIFY(!SubjectBalanceImport::validate(i, session("http://127.0.0.1"), catalog).isEmpty());
        QVERIFY(!SubjectBalanceImport::validate(input(), session("http://127.0.0.1"), {}).isEmpty());
        for (const auto &software : catalog) if (software.id == 99999) {
            QCOMPARE(software.files.size(), 2);
            QVERIFY(software.requirementsAvailable);
            for (const auto &slot : software.files) {
                if (slot.key == "subject") QVERIFY(slot.required);
                if (slot.key == "trialbalance") QVERIFY(slot.special && !slot.required);
            }
        }
        for (const auto &bad : {QJsonValue(0), QJsonValue(5), QJsonValue(1.5), QJsonValue("1"), QJsonValue()}) {
            auto item = rules.first().toObject(); item["subjectStatus"] = bad;
            QVERIFY(SubjectBalanceImport::applyRequirements(QJsonArray{item}, &error).isEmpty());
            QVERIFY(!error.isEmpty());
        }
        QVERIFY(SubjectBalanceImport::applyRequirements({}).isEmpty());
        rules.append(rules.first());
        QVERIFY(SubjectBalanceImport::applyRequirements(rules).isEmpty());
    }

    void anonymousRequirementsCacheAndFailure() {
        QTemporaryDir cache;
        FakeServer server;
        BalanceImportRequirements loader(nullptr, cache.path());
        prepareRequirements(loader, server);
        QCOMPARE(server.requests.size(), 1);
        const auto request = server.requests.first().toLower();
        QVERIFY(request.startsWith("get /api/importfromother/getsoftwarefilerequirements http/"));
        for (const auto &header : {"authorization:", "cookie:", "withcredentials:", "asid:"}) QVERIFY(!request.contains(header));
        QCOMPARE(request.mid(request.indexOf("\r\n\r\n") + 4).size(), 0);
        const auto paths = QDir(cache.path()).entryList({"*.json"}, QDir::Files);
        QCOMPARE(paths.size(), 1);
        QFile cached(QDir(cache.path()).filePath(paths.first()));
        QVERIFY(cached.open(QIODevice::ReadOnly)); const auto original = cached.readAll(); cached.close();
        QVERIFY(QJsonDocument::fromJson(original).isArray());
        loader.load(server.host());
        QVERIFY(!loader.loading()); QCOMPARE(server.requests.size(), 1);
        server.response = [](const QByteArray &) { return QByteArray("{\"state\":1000,\"data\":[]}"); };
        BalanceImportRequirements restarted(nullptr, cache.path());
        restarted.load(server.host());
        QTRY_VERIFY(!restarted.loading());
        QVERIFY(!restarted.catalog().isEmpty());
        QVERIFY(restarted.message().contains(QStringLiteral("上次有效")));
        QVERIFY(cached.open(QIODevice::ReadOnly)); QCOMPARE(cached.readAll(), original); cached.close();
        FakeServer other;
        other.httpStatus = 503;
        other.response = [](const QByteArray &) { return requirementsBody(); };
        restarted.load(other.host());
        QVERIFY(restarted.catalog().isEmpty());
        QTRY_VERIFY(!restarted.loading());
        QVERIFY(restarted.catalog().isEmpty());
        QVERIFY(restarted.message().contains(QStringLiteral("配置不可用")));
        QVERIFY(cached.open(QIODevice::WriteOnly | QIODevice::Truncate)); cached.write("broken"); cached.close();
        BalanceImportRequirements corrupt(nullptr, cache.path()); corrupt.load(server.host());
        QTRY_VERIFY(!corrupt.loading()); QVERIFY(corrupt.catalog().isEmpty());
    }

    void requirementsSwitchTimeoutAndSaveFailure() {
        QTemporaryDir cache;
        FakeServer delayed, current;
        current.response = [](const QByteArray &) { return requirementsBody(); };
        BalanceImportRequirements loader(nullptr, cache.path());
        loader.load(delayed.host());
        QTRY_COMPARE(delayed.requests.size(), 1);
        loader.load(current.host());
        QTRY_VERIFY(!loader.loading());
        QVERIFY(!loader.catalog().isEmpty());
        QCOMPARE(loader.host(), current.host());
        FakeServer silent;
        loader.load(silent.host());
        QTRY_VERIFY_WITH_TIMEOUT(!loader.loading(), 6500);
        QVERIFY(loader.catalog().isEmpty());
        QFile blocked(cache.filePath("not-a-directory")); QVERIFY(blocked.open(QIODevice::WriteOnly)); blocked.close();
        BalanceImportRequirements unwritable(nullptr, blocked.fileName());
        unwritable.load(current.host());
        QTRY_VERIFY(!unwritable.loading());
        QVERIFY(unwritable.catalog().isEmpty());
        QVERIFY(unwritable.message().contains(QStringLiteral("无法保存")));
        loader.load("http://account:password@127.0.0.1");
        QVERIFY(!loader.loading()); QVERIFY(loader.catalog().isEmpty());
        loader.load("file:///local");
        QVERIFY(!loader.loading()); QVERIFY(loader.catalog().isEmpty());
    }

    void requirementsWidgetHostSwitch() {
        QTemporaryDir cache;
        FakeServer first, second;
        BalanceImportRequirements loader(nullptr, cache.path());
        prepareRequirements(loader, first);
        GlobalData global;
        SubjectBalanceImportWidget widget(&global, nullptr, cache.filePath("history.json"), &loader);
        auto *software = widget.findChild<QComboBox*>("balanceSoftware");
        software->setCurrentIndex(software->findData(99999));
        widget.selectSourceFile("trialbalance", sample());
        auto *host = widget.findChild<QComboBox*>("balanceImporterHost");
        host->addItem("Second", second.host()); host->setCurrentIndex(host->count() - 1);
        QVERIFY(!widget.findChild<QPushButton*>("balanceSubmit")->isEnabled());
        QVERIFY(widget.property("sessionBusyFileRequirements").toBool());
        QVERIFY(!widget.findChild<QLineEdit*>("balanceFile_trialbalance"));
        // Cancel the stalled second request and reuse the first host's snapshot.
        host->setCurrentIndex(host->findData(first.host()));
        QVERIFY(!widget.property("sessionBusyFileRequirements").toBool());
        QVERIFY(widget.findChild<QLineEdit*>("balanceFile_trialbalance")->text().isEmpty());
        QCOMPARE(first.requests.size(), 1);
        auto row = QJsonObject{{"softwareId", 99999}, {"softwareName", "Template"},
            {"subjectStatus", 1}, {"trialBalanceStatus", 4}, {"voucherStatus", 3}, {"remark", "hidden remark"}};
        second.response = [row](const QByteArray &) { return requirementsBody(QJsonArray{row}); };
        host->setCurrentIndex(host->findData(second.host()));
        QTRY_VERIFY(!loader.loading());
        QVERIFY(widget.findChild<QLineEdit*>("balanceFile_subject"));
        QVERIFY(!widget.findChild<QLineEdit*>("balanceFile_voucher"));
        QCOMPARE(software->findData(2001), -1);
        QCOMPARE(software->count(), 2); // Only the server's one software plus the placeholder.
    }

    void serverSoftwareListRefreshAndImport() {
        QTemporaryDir cache;
        FakeServer server;
        const int newId = 900001;
        QJsonArray rules{QJsonObject{{"softwareId", newId}, {"softwareName", QStringLiteral("新增财务软件")},
            {"subjectStatus", 1}, {"trialBalanceStatus", 4}, {"voucherStatus", 3}, {"remark", ""}}};
        for (const auto &value : fileRequirements()) {
            const auto row = value.toObject();
            if (row.value("softwareId").toInt() == 22001 || row.value("softwareId").toInt() == 2001)
                rules.append(row);
        }
        server.response = [&rules](const QByteArray &request) {
            if (request.contains("GetSoftwareFileRequirements")) return requirementsBody(rules);
            return request.startsWith("POST")
                ? QJsonDocument(QJsonObject{{"Code", 0}, {"Data", taskId}}).toJson() : resultBody(1);
        };
        BalanceImportRequirements loader(nullptr, cache.path());
        loader.load(server.host());
        QTRY_VERIFY(!loader.loading());
        QCOMPARE(loader.catalog().size(), 3);
        QCOMPARE(loader.catalog().first().id, newId);
        QVERIFY(!loader.catalog().first().supportsAuxiliaryFiles);
        for (const auto &software : loader.catalog()) {
            if (software.id == 2001) QVERIFY(software.supportsAuxiliaryFiles);
            if (software.id == 22001) for (const auto &slot : software.files)
                if (slot.key == "subject") QVERIFY(slot.extensions.contains("txt"));
        }
        GlobalData global; global.m_strSN = "test-owner"; global.m_nServiceId = 45;
        global.m_strNingMengCookie = "test-session-token";
        SubjectBalanceImportWidget widget(&global, nullptr, cache.filePath("history.json"), &loader);
        auto *software = widget.findChild<QComboBox*>("balanceSoftware");
        QCOMPARE(software->count(), 4);
        QCOMPARE(software->itemData(1).toInt(), newId);
        software->completer()->setCompletionPrefix(QStringLiteral("新增"));
        QCOMPARE(software->completer()->completionCount(), 1);
        software->setCurrentIndex(software->findData(newId));
        QVERIFY(widget.findChild<QLineEdit*>("balanceFile_subject"));
        QVERIFY(widget.findChild<QLineEdit*>("balanceFile_trialbalance"));
        QVERIFY(!widget.findChild<QLineEdit*>("balanceFile_voucher"));
        auto *submit = widget.findChild<QPushButton*>("balanceSubmit");
        submit->click();
        QCOMPARE(server.requests.size(), 1); // Missing required subject blocks the upload.
        widget.selectSourceFile("subject", sample());
        submit->click();
        QTRY_VERIFY(widget.findChild<QLabel*>("balanceStatus")->text().contains(QStringLiteral("成功")));
        QCOMPARE(server.requests.size(), 3);
        QVERIFY(server.requests.at(1).startsWith("POST"));
        QVERIFY(server.requests.at(1).contains("software=900001"));
        QVERIFY(server.requests.at(1).contains("name=\"subject\""));
        QVERIFY(!server.requests.at(1).contains("name=\"voucher\""));

        auto renamed = rules.first().toObject();
        renamed["softwareName"] = QStringLiteral("更新后的软件名称");
        rules = QJsonArray{renamed};
        widget.findChild<QPushButton*>("balanceRefreshRequirements")->click();
        QVERIFY(!software->isEnabled());
        QTRY_VERIFY(!loader.loading());
        QCOMPARE(software->count(), 2);
        QCOMPARE(software->currentData().toInt(), newId);
        QCOMPARE(software->currentText(), QStringLiteral("更新后的软件名称"));
        QCOMPARE(software->findData(2001), -1);
        QVERIFY(widget.findChild<QLineEdit*>("balanceFile_subject")->text().isEmpty());
        QCOMPARE(widget.findChild<QLineEdit*>("balanceAccountName")->text(), QStringLiteral("更新后的软件名称账套"));
        software->completer()->setCompletionPrefix(QStringLiteral("更新后"));
        QCOMPARE(software->completer()->completionCount(), 1);

        server.httpStatus = 503;
        BalanceImportRequirements restarted(nullptr, cache.path());
        restarted.load(server.host());
        QTRY_VERIFY(!restarted.loading());
        QCOMPARE(restarted.catalog().size(), 1);
        QCOMPARE(restarted.catalog().first().id, newId);
        QCOMPARE(restarted.catalog().first().name, QStringLiteral("更新后的软件名称"));
        // A valid response that removes the selected software must clear its selection and files.
        server.httpStatus = 200;
        renamed["softwareId"] = 900002;
        rules = QJsonArray{renamed};
        widget.findChild<QPushButton*>("balanceRefreshRequirements")->click();
        QTRY_VERIFY(!loader.loading());
        QCOMPARE(software->findData(newId), -1);
        QCOMPARE(software->currentIndex(), -1);
        QVERIFY(!widget.findChild<QLineEdit*>("balanceFile_subject"));
    }

    void resultContract() {
        for (int status = 0; status <= 3; ++status) {
            const auto out = SubjectBalanceImport::parseOutcome(resultBody(status), taskId);
            QCOMPARE(out.status, status);
            QCOMPARE(out.startMonth, QString("2025-11"));
            QCOMPARE(out.warnings, QString("sample warning"));
        }
        QCOMPARE(SubjectBalanceImport::parseOutcome(resultBody(99), taskId).status, -1);
        QCOMPARE(SubjectBalanceImport::parseOutcome(resultBody(1, "different"), taskId).status, -1);
        QCOMPARE(SubjectBalanceImport::parseOutcome("{}", taskId).status, -1);
        QCOMPARE(SubjectBalanceImport::parseOutcome("not json", taskId).status, -1);
        QCOMPARE(SubjectBalanceImport::parseOutcome(R"({"State":1000,"Data":{"Code":0,"Data":null}})", taskId).status, 0);
        QCOMPARE(SubjectBalanceImport::parseOutcome(R"({"State":1000,"Data":{"Code":1,"Message":"error"}})", taskId).status, -1);
        auto doc = QJsonDocument::fromJson(resultBody(0));
        auto root = doc.object(); auto wrapper = root["Data"].toObject(); auto detail = wrapper["Data"].toObject();
        detail["Status"] = QJsonValue::Null; wrapper["Data"] = detail; root["Data"] = wrapper;
        QCOMPARE(SubjectBalanceImport::parseOutcome(QJsonDocument(root).toJson(), taskId).status, -1);
    }

    void actualMultipartAndPolling() {
        FakeServer server;
        server.response = [](const QByteArray &request) {
            if (request.startsWith("POST")) return QJsonDocument(QJsonObject{{"Code", 0}, {"Data", taskId}}).toJson();
            return resultBody(2);
        };
        SubjectBalanceImportClient client;
        QSignalSpy accepted(&client, &SubjectBalanceImportClient::accepted), outcome(&client, &SubjectBalanceImportClient::outcome);
        auto i = input(); auto s = session(server.host());
        client.submit(i, s, testCatalog());
        client.submit(i, s, testCatalog()); // A second click must not submit again.
        QTRY_COMPARE(outcome.count(), 1);
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(outcome.first().at(0).toInt(), 2);
        QCOMPARE(server.requests.size(), 2);
        const auto post = server.requests.first();
        QVERIFY(post.startsWith("POST /api/ImportFromOther/ImportAsFromClientBatch?"));
        QVERIFY(post.contains("accountSetVersion=2"));
        QVERIFY(post.contains("importSystem=1"));
        QVERIFY(post.contains("serviceId=45"));
        QVERIFY(post.contains("isZeroImport=2"));
        QVERIFY(post.contains("asStartPeriod=2026-01-01"));
        const auto url = QUrl::fromEncoded(post.split(' ').at(1));
        QCOMPARE(QUrlQuery(url).queryItemValue("asName", QUrl::FullyDecoded), i.accountName);
        QVERIFY(post.contains("%2B"));
        QVERIFY(post.contains("Authorization: Bearer test-session-token"));
        QVERIFY(post.contains("name=\"trialbalance\""));
        QVERIFY(post.contains("filename*=UTF-8''"));
        QVERIFY(post.contains("ORIGINAL-SAMPLE-BYTES"));
        QVERIFY(server.requests.last().startsWith("GET /api/ImportFromYun/GetImportInfoById?importInfoId="));
        QVERIFY(!client.busy());
        QFile original(i.files["trialbalance"]); QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("ORIGINAL-SAMPLE-BYTES"));
    }

    void camelCaseSubmissionError() {
        FakeServer server;
        const QString message = QStringLiteral("导入任务创建失败：亲，请使用正确的导入模板导入！");
        server.response = [message](const QByteArray &) {
            return QJsonDocument(QJsonObject{{"code", 1}, {"message", message}, {"data", ""}}).toJson();
        };
        SubjectBalanceImportClient client;
        QSignalSpy failed(&client, &SubjectBalanceImportClient::failed);
        QSignalSpy accepted(&client, &SubjectBalanceImportClient::accepted);
        client.submit(input(), session(server.host()), testCatalog());
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(failed.first().at(0).toString(), message);
        QVERIFY(!failed.first().at(1).toBool());
        QCOMPARE(accepted.count(), 0);
        QCOMPARE(server.requests.size(), 1);
    }

    void camelCaseSubmissionAndPolling() {
        FakeServer server;
        server.response = [](const QByteArray &request) {
            if (request.startsWith("POST"))
                return QJsonDocument(QJsonObject{{"code", 0}, {"data", taskId}}).toJson();
            const QJsonObject detail{{"id", taskId}, {"status", 2}, {"error_Msg", "sample error"},
                {"reminder_Msg", "sample warning"}, {"asStartDate", "2023-06-01"}, {"appAsId", "123"}};
            return QJsonDocument(QJsonObject{{"state", 1000}, {"data", QJsonObject{{"code", 0}, {"data", detail}}}}).toJson();
        };
        SubjectBalanceImportClient client;
        QSignalSpy accepted(&client, &SubjectBalanceImportClient::accepted), outcome(&client, &SubjectBalanceImportClient::outcome);
        QSignalSpy failed(&client, &SubjectBalanceImportClient::failed);
        client.submit(input(), session(server.host()), testCatalog());
        QTRY_VERIFY(!client.busy());
        QCOMPARE(failed.count(), 0);
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(outcome.count(), 1);
        QCOMPARE(outcome.first().at(0).toInt(), 2);
        QCOMPARE(outcome.first().at(1).toString(), QString("sample error"));
        QCOMPARE(outcome.first().at(2).toString(), QString("sample warning"));
        QCOMPARE(outcome.first().at(3).toString(), QString("2023-06"));
        QCOMPARE(outcome.first().at(4).toString(), QString("123"));
        QCOMPARE(server.requests.size(), 2);
        for (const auto &request : server.requests)
            QVERIFY((request.left(request.indexOf("\r\n\r\n")) + "\r\n").toLower().contains("\r\nlmlimit: internal\r\n"));
    }

    void stopResumeAndFailures() {
        FakeServer server;
        server.response = [](const QByteArray &) { return resultBody(0); };
        SubjectBalanceImportClient client;
        QSignalSpy progress(&client, &SubjectBalanceImportClient::progress), failure(&client, &SubjectBalanceImportClient::failed);
        client.query(taskId, session(server.host()));
        QTRY_VERIFY(progress.count() > 0);
        client.stopQuery();
        QVERIFY(!client.busy());
        const int prior = server.requests.size();
        QTest::qWait(10100); // Wait beyond the poll interval: stopping must actually cancel the timer.
        QCOMPARE(server.requests.size(), prior);
        server.response = [](const QByteArray &) { return resultBody(3); };
        QSignalSpy outcome(&client, &SubjectBalanceImportClient::outcome);
        client.query(taskId, session(server.host()));
        QTRY_COMPARE(outcome.count(), 1);
        QCOMPARE(outcome.first().at(0).toInt(), 3);
        server.httpStatus = 401;
        client.submit(input(), session(server.host()), testCatalog());
        QTRY_COMPARE(failure.count(), 1);
        QVERIFY(!failure.last().at(1).toBool());
        server.httpStatus = 200;
        server.response = [](const QByteArray &) { return QByteArray("unparseable"); };
        client.submit(input(), session(server.host()), testCatalog());
        QTRY_COMPARE(failure.count(), 2);
        QVERIFY(failure.last().at(1).toBool());
        const int count = server.requests.size();
        QTest::qWait(30);
        QCOMPARE(server.requests.size(), count); // No automatic resubmission.
    }

    void pollingUsesRenewedToken() {
        FakeServer server;
        server.response = [](const QByteArray &) { return resultBody(0); };
        SubjectBalanceImportClient client;
        QString liveToken = "first-token";
        client.setTokenProvider([&]() { return liveToken; });
        QSignalSpy progress(&client, &SubjectBalanceImportClient::progress);
        client.query(taskId, session(server.host()));
        QTRY_VERIFY(progress.count() > 0);
        QVERIFY(server.requests.last().contains("Bearer first-token"));
        liveToken = "renewed-token";
        QTRY_VERIFY_WITH_TIMEOUT(server.requests.size() >= 2, 12000);
        QVERIFY(server.requests.last().contains("Bearer renewed-token"));
        QVERIFY(server.requests.last().contains("ningmengcookie=renewed-token"));
        client.stopQuery();
    }

    void auxiliaryAndZeroImport() {
        FakeServer server;
        server.response = [](const QByteArray &r) {
            return r.startsWith("POST") ? QJsonDocument(QJsonObject{{"Code", 0}, {"Data", taskId}}).toJson() : resultBody(1);
        };
        auto i = input(); i.software = 2001; i.importZero = true;
        i.files[QStringLiteral("客户")] = sample("customers.xlsx");
        SubjectBalanceImportClient client;
        QSignalSpy done(&client, &SubjectBalanceImportClient::outcome);
        client.submit(i, session(server.host()), testCatalog());
        QTRY_COMPARE(done.count(), 1);
        QVERIFY(server.requests.first().contains(QStringLiteral("name=\"客户\"").toUtf8()));
        QVERIFY(server.requests.first().contains("isZeroImport=1"));
        i.files["bad\r\nheader"] = sample();
        QVERIFY(!SubjectBalanceImport::validate(i, session(server.host()), testCatalog()).isEmpty());
    }

    void softwareSearchAndPopupSpacing() {
        GlobalData global;
        FakeServer rulesServer;
        BalanceImportRequirements requirements(nullptr, m_temp.filePath("requirements"));
        prepareRequirements(requirements, rulesServer);
        SubjectBalanceImportWidget widget(&global, nullptr, m_temp.filePath("search-history.json"), &requirements);
        QFile theme(":/Resource/qss/workspace.css"); QVERIFY(theme.open(QIODevice::ReadOnly));
        widget.setStyleSheet(QString::fromUtf8(theme.readAll()));
        widget.setAttribute(Qt::WA_DontShowOnScreen);
        widget.resize(900, 850); widget.show();
        auto *combo = widget.findChild<QComboBox*>("balanceSoftware");
        QVERIFY(combo->isEditable());
        QCOMPARE(combo->count(), 71);
        combo->showPopup(); QTest::qWait(30);
        const auto first = combo->view()->visualRect(combo->model()->index(1, 0));
        const auto second = combo->view()->visualRect(combo->model()->index(2, 0));
        QVERIFY2(first.height() >= 36, "software row must fit text and vertical padding");
        QVERIFY2(second.top() > first.bottom(), "software rows must not overlap");
        const auto captureDir = qEnvironmentVariable("BALANCE_TEST_SCREENSHOTS");
        if (!captureDir.isEmpty()) {
            QDir().mkpath(captureDir);
            QVERIFY(combo->view()->window()->grab().save(captureDir + "/software-options.png"));
        }
        combo->hidePopup();
        combo->setCurrentIndex(combo->findData(2001));
        auto *editor = combo->lineEdit();
        editor->setFocus(); editor->selectAll(); QTest::keyClicks(editor, "t3");
        QCOMPARE(combo->currentIndex(), -1); // A query cannot leave a stale software ID behind.
        auto *completer = combo->completer();
        QTRY_VERIFY(completer->completionCount() > 0);
        for (int row = 0; row < completer->completionCount(); ++row)
            QVERIFY(completer->completionModel()->index(row, 0).data().toString().contains("t3", Qt::CaseInsensitive));
        auto *popup = completer->popup();
        QTest::qWait(30);
        const auto match = completer->completionModel()->index(0, 0);
        const int expectedId = match.data(Qt::UserRole).toInt();
        QVERIFY(expectedId > 0);
        QVERIFY(popup->visualRect(match).height() >= 36);
        if (!captureDir.isEmpty()) QVERIFY(popup->grab().save(captureDir + "/software-search-t3.png"));
        QTest::mouseClick(popup->viewport(), Qt::LeftButton, Qt::NoModifier, popup->visualRect(match).center());
        QTRY_COMPARE(combo->currentData().toInt(), expectedId);
        QVERIFY(widget.findChild<QLineEdit*>("balanceFile_trialbalance"));
        // Chinese input method commit must also filter by a substring anywhere in the name.
        editor->setFocus(); editor->selectAll();
        QInputMethodEvent chinese;
        chinese.setCommitString(QStringLiteral("金蝶"));
        QApplication::sendEvent(editor, &chinese);
        QTRY_VERIFY(completer->completionCount() > 0);
        for (int row = 0; row < completer->completionCount(); ++row)
            QVERIFY(completer->completionModel()->index(row, 0).data().toString().contains(QStringLiteral("金蝶")));
        if (!captureDir.isEmpty()) QVERIFY(popup->grab().save(captureDir + "/software-search-chinese.png"));
        QVERIFY(popup->isVisible());
        QTest::keyClick(popup, Qt::Key_Down);
        QTest::keyClick(popup, Qt::Key_Return);
        QTRY_VERIFY(combo->currentData().toInt() > 0);
        QVERIFY(combo->currentText().contains(QStringLiteral("金蝶")));
        QTest::qWait(30); // Newly-created file rows become visible on the next layout pass.
        QVERIFY(widget.findChild<QLineEdit*>("balanceFile_trialbalance")->isVisible());
        if (!captureDir.isEmpty()) QVERIFY(widget.grab().save(captureDir + "/software-search-form.png"));
        editor->selectAll(); QTest::keyClicks(editor, "no-such-software-987");
        QCOMPARE(completer->completionCount(), 0);
        QCOMPARE(combo->currentIndex(), -1);
        widget.findChild<QPushButton*>("balanceSubmit")->click();
        QVERIFY(widget.findChild<QLabel*>("balanceStatus")->text().contains(QStringLiteral("选择来源软件")));
        editor->selectAll(); QTest::keyClick(editor, Qt::Key_Backspace);
        QCOMPARE(combo->count(), 71);
        combo->showPopup();
        QVERIFY(combo->view()->model()->rowCount() == 71);
        combo->hidePopup();
    }

    void widgetAndHistory() {
        FakeServer server;
        server.response = [](const QByteArray &r) {
            if (r.contains("GetSoftwareFileRequirements")) return requirementsBody();
            return r.startsWith("POST") ? QJsonDocument(QJsonObject{{"Code", 0}, {"Data", taskId}}).toJson() : resultBody(1);
        };
        GlobalData global; global.m_strSN = "test-owner"; global.m_nServiceId = 45; global.m_strNingMengCookie = "test-session-token";
        FakeServer rulesServer;
        BalanceImportRequirements requirements(nullptr, m_temp.filePath("requirements"));
        prepareRequirements(requirements, rulesServer);
        SubjectBalanceImportWidget widget(&global, nullptr, m_temp.filePath("history.json"), &requirements);
        widget.setAttribute(Qt::WA_DontShowOnScreen);
        auto *software = widget.findChild<QComboBox*>("balanceSoftware");
        QCOMPARE(software->count(), 71);
        QVERIFY(!widget.findChild<QCheckBox*>("balanceImportZero")->isChecked());
        QCOMPARE(widget.findChild<QComboBox*>("balanceStandard")->currentData().toInt(), 1);
        QCOMPARE(widget.findChild<QComboBox*>("balanceTaxType")->currentData().toInt(), 2);
        QCOMPARE(widget.findChild<QDateEdit*>("balanceStartMonth")->date().toString("yyyy-MM"), QDate::currentDate().toString("yyyy-MM"));
        software->setCurrentIndex(software->findData(99999));
        QCOMPARE(widget.findChild<QLineEdit*>("balanceAccountName")->text(), QStringLiteral("通用模板账套"));
        widget.findChild<QLineEdit*>("balanceFile_trialbalance")->setText(sample());
        software->setCurrentIndex(software->findData(2001));
        QVERIFY(widget.findChild<QLineEdit*>("balanceFile_trialbalance")->text().isEmpty());
        software->setCurrentIndex(software->findData(99999));
        widget.findChild<QLineEdit*>("balanceFile_trialbalance")->setText(sample());
        widget.findChild<QLineEdit*>("balanceAccountName")->setText(QStringLiteral("离线测试账套"));
        widget.findChild<QComboBox*>("balanceStandard")->setCurrentIndex(1);
        widget.findChild<QComboBox*>("balanceTaxType")->setCurrentIndex(1);
        auto *host = widget.findChild<QComboBox*>("balanceImporterHost");
        QComboBox shared; populateProfessionalImporterHosts(&shared);
        QCOMPARE(host->count(), shared.count() + 1); QCOMPARE(host->currentData().toString(), rulesServer.host());
        host->addItem("Mock", server.host()); host->setCurrentIndex(host->count() - 1);
        QTRY_VERIFY(!requirements.loading());
        widget.findChild<QLineEdit*>("balanceFile_trialbalance")->setText(sample());
        widget.findChild<QPushButton*>("balanceSubmit")->click();
        QTRY_VERIFY(widget.findChild<QLabel*>("balanceStatus")->text().contains(QStringLiteral("成功")));
        auto *history = widget.findChild<QComboBox*>("balanceHistory");
        QCOMPARE(history->count(), 1);
        QVERIFY(!QJsonDocument(QJsonObject::fromVariantMap(history->currentData().toMap())).toJson().contains("test-session-token"));
        host->setCurrentIndex(host->findData(rulesServer.host())); // Resume must use the saved host, never the form host.
        widget.findChild<QPushButton*>("balanceResumeQuery")->click();
        QTRY_COMPARE(server.requests.size(), 4);
        QTRY_VERIFY(widget.findChild<QPushButton*>("balanceSubmit")->isEnabled());
        global.m_strSN = "another-owner"; widget.reloadHistory(); QCOMPARE(history->count(), 0);
        global.m_strSN = "test-owner"; global.m_nServiceId = 999; widget.reloadHistory(); QCOMPARE(history->count(), 0);
        widget.resize(720, 850); widget.show(); QTest::qWait(20);
        const auto captureDir = qEnvironmentVariable("BALANCE_TEST_SCREENSHOTS");
        if (!captureDir.isEmpty()) { QDir().mkpath(captureDir); QVERIFY(widget.grab().save(captureDir + "/balance-import.png")); }
    }

    void siblingSuggestionsAndDefaults() {
        QTemporaryDir files;
        const auto create = [&files](const QString &name) {
            const auto path = files.filePath(name);
            QFile file(path); if (file.open(QIODevice::WriteOnly)) file.write("SAMPLE");
            return path;
        };
        const auto voucher = create(QStringLiteral("甲公司_凭证表_2026-09.xlsx"));
        const auto balance = create(QStringLiteral("甲公司_科目余额表_2026-09.xlsx"));
        const auto subject = create(QStringLiteral("甲公司_科目表.xlsx"));
        create(QStringLiteral("~$甲公司_科目表.xlsx"));
        create(QStringLiteral("甲公司_科目余额表.txt"));
        QDir().mkpath(files.filePath("nested"));
        create(QStringLiteral("nested/甲公司_科目表.xlsx"));
        BalanceImportSoftware selected;
        for (const auto &software : testCatalog()) if (software.id == 1007) selected = software;
        const auto suggestions = suggestBalanceFiles(selected, voucher, {{"voucher", voucher}});
        QCOMPARE(suggestions.matches.value("trialbalance"), balance);
        QCOMPARE(suggestions.matches.value("subject"), subject);
        const auto manual = suggestBalanceFiles(selected, voucher, {{"voucher", voucher}, {"subject", "existing"}});
        QVERIFY(!manual.matches.contains("subject"));
        const auto secondSubject = create(QStringLiteral("甲公司_会计科目.xlsx"));
        const auto ambiguous = suggestBalanceFiles(selected, voucher, {{"voucher", voucher}});
        QCOMPARE(ambiguous.candidates.value("subject").size(), 2);
        QCOMPARE(ambiguous.matches.value("subject"), ambiguous.candidates.value("subject").first());

        GlobalData global;
        FakeServer rulesServer;
        BalanceImportRequirements requirements(nullptr, m_temp.filePath("requirements"));
        prepareRequirements(requirements, rulesServer);
        SubjectBalanceImportWidget widget(&global, nullptr, files.filePath("history.json"), &requirements);
        auto *software = widget.findChild<QComboBox*>("balanceSoftware");
        software->setCurrentIndex(software->findData(1007));
        widget.selectSourceFile("voucher", voucher);
        QCOMPARE(widget.findChild<QLineEdit*>("balanceFile_trialbalance")->text(), balance);
        QCOMPARE(widget.findChild<QLineEdit*>("balanceFile_subject")->text(), ambiguous.matches.value("subject"));
        QVERIFY(!widget.findChild<QComboBox*>("balanceCandidates_subject"));
        widget.selectSourceFile("subject", secondSubject);
        QCOMPARE(widget.findChild<QLineEdit*>("balanceFile_subject")->text(), secondSubject);
        widget.findChild<QLineEdit*>("balanceAccountName")->setText(QStringLiteral("自定义账套"));
        software->setCurrentIndex(software->findData(99999));
        QCOMPARE(widget.findChild<QLineEdit*>("balanceAccountName")->text(), QStringLiteral("自定义账套"));
        QVERIFY(widget.findChild<QLineEdit*>("balanceFile_trialbalance")->text().isEmpty());
        QVERIFY(widget.findChildren<QProcess*>().isEmpty()); // No spreadsheet/date reader was added.
        create(QStringLiteral("乙公司_科目余额表.xlsx"));
        const auto differentNames = suggestBalanceFiles(selected, voucher, {{"voucher", voucher}});
        QCOMPARE(differentNames.candidates.value("trialbalance").size(), 2);
        QCOMPARE(differentNames.matches.value("trialbalance"), differentNames.candidates.value("trialbalance").first());
    }

    void subjectFirstAndKeywordAutofill() {
        QTemporaryDir files;
        const auto create = [&files](const QString &name) {
            const auto path = files.filePath(name);
            QFile file(path); if (file.open(QIODevice::WriteOnly)) file.write("SAMPLE");
            return path;
        };
        const auto subject = create(QStringLiteral("科目(8).xlsx"));
        const auto balance = create(QStringLiteral("2023年发生额及余额表.xlsx"));
        const auto voucher = create(QStringLiteral("凭证导入导出(1).xlsx"));
        create(QStringLiteral("科目(8).xlsx.补齐前.bak"));
        GlobalData global;
        FakeServer rulesServer;
        BalanceImportRequirements requirements(nullptr, m_temp.filePath("requirements"));
        prepareRequirements(requirements, rulesServer);
        SubjectBalanceImportWidget widget(&global, nullptr, files.filePath("history.json"), &requirements);
        widget.setAttribute(Qt::WA_DontShowOnScreen);
        auto *software = widget.findChild<QComboBox*>("balanceSoftware");
        software->setCurrentIndex(software->findData(2011));
        widget.selectSourceFile("subject", subject);
        auto *subjectInput = widget.findChild<QLineEdit*>("balanceFile_subject");
        auto *balanceInput = widget.findChild<QLineEdit*>("balanceFile_trialbalance");
        auto *voucherInput = widget.findChild<QLineEdit*>("balanceFile_voucher");
        QCOMPARE(subjectInput->text(), subject);
        QCOMPARE(balanceInput->text(), balance);
        QCOMPARE(voucherInput->text(), voucher);
        auto *layout = qobject_cast<QFormLayout*>(subjectInput->parentWidget()->parentWidget()->layout());
        QVERIFY(layout);
        int subjectRow = -1, balanceRow = -1, voucherRow = -1;
        QFormLayout::ItemRole role;
        layout->getWidgetPosition(subjectInput->parentWidget(), &subjectRow, &role);
        layout->getWidgetPosition(balanceInput->parentWidget(), &balanceRow, &role);
        layout->getWidgetPosition(voucherInput->parentWidget(), &voucherRow, &role);
        QCOMPARE(subjectRow, 0);
        QVERIFY(subjectRow < balanceRow && balanceRow < voucherRow);
        widget.resize(950, 1000); widget.show(); QTest::qWait(20);
        const auto captureDir = qEnvironmentVariable("BALANCE_TEST_SCREENSHOTS");
        if (!captureDir.isEmpty()) QVERIFY(widget.grab().save(captureDir + "/subject-first-autofill.png"));

        // "余额" matches even without "表" and without a common filename prefix.
        QVERIFY(QFile::rename(balance, files.filePath(QStringLiteral("其他前缀余额数据.xlsx"))));
        software->setCurrentIndex(0); software->setCurrentIndex(software->findData(2011));
        widget.selectSourceFile("subject", subject);
        balanceInput = widget.findChild<QLineEdit*>("balanceFile_trialbalance");
        QCOMPARE(balanceInput->text(), files.filePath(QStringLiteral("其他前缀余额数据.xlsx")));
    }

    void localImporterHostIsIndependentOfLoginEnvironment() {
        for (const auto &environment : QStringList{"Dev", "Dev2", "Local"}) {
            lemoncofighelper::SetHttpEnvironment(environment);
            QComboBox hosts;
            populateProfessionalImporterHosts(&hosts);
            QCOMPARE(hosts.count(), 4);
            QCOMPARE(hosts.itemData(3).toString(), QString("http://localhost:55169"));
            QCOMPARE(hosts.itemText(3), QString("Local - http://localhost:55169"));
            QCOMPARE(hosts.currentIndex(), environment == "Dev" ? 0 : environment == "Dev2" ? 1 : 3);
            QCOMPARE(lemoncofighelper::CurrentHttpEnvironment(), environment);
        }
        lemoncofighelper::SetHttpEnvironment("Local");
    }

    void passwordOnlyAndEnvironmentIsolation() {
        QTemporaryDir credentials;
        const auto save = [&credentials](const QString &environment, const QString &user) {
            QFile file(credentials.filePath("LemonApp." + environment + ".dat"));
            if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(QJsonObject{{"re", 1}, {"u", user}, {"p", "sample-password"}}).toJson());
        };
        save("Pro", "13000000001"); save("Dev2", "13000000002");
        lemoncofighelper::SetHttpEnvironment("Pro");
        GlobalData global;
        StartUpWindow startup(&global, nullptr, credentials.path());
        startup.setAttribute(Qt::WA_DontShowOnScreen);
        auto *environment = startup.findChild<QComboBox*>("loginEnvironment");
        QVERIFY(environment); QCOMPARE(environment->count(), 2);
        QVERIFY(!startup.findChild<QPushButton*>("btnSmsMode"));
        QVERIFY(!startup.findChild<QPushButton*>("btnMicroMsgMode"));
        QCOMPARE(startup.findChildren<PwdLoginWidget*>().size(), 1);
        auto *password = startup.findChild<PwdLoginWidget*>();
        QCOMPARE(startup.findChild<QLineEdit*>("editUserName")->text(), QString("13000000001"));
        QCOMPARE(startup.findChild<QLineEdit*>("editPwd")->text(), QString("sample-password"));
        QFile migrated(credentials.filePath("LemonApp.Pro.dpapi"));
        QVERIFY(migrated.open(QIODevice::ReadOnly));
        const auto encrypted = migrated.readAll();
        QVERIFY(!encrypted.isEmpty() && !encrypted.contains("sample-password"));
        migrated.close();

        QTemporaryDir package;
        QVERIFY(package.isValid());
        QFile portableLegacy(package.filePath("LemonApp.Pro.dat"));
        QVERIFY(portableLegacy.open(QIODevice::WriteOnly));
        QVERIFY(portableLegacy.write(R"({"re":1,"u":"13000000003","p":"program-password"})") > 0);
        portableLegacy.close();
        GlobalData portableGlobal;
        PwdLoginWidget preferred(&portableGlobal, nullptr, package.path(), credentials.path());
        QCOMPARE(preferred.findChild<QLineEdit*>("editPwd")->text(), QString("program-password"));
        const QString portablePath = package.filePath("LemonApp.Pro.dpapi");
        QVERIFY(QFileInfo::exists(portablePath));
        QVERIFY(portableLegacy.remove());
        preferred.setEnvironment("Pro");
        QCOMPARE(preferred.findChild<QLineEdit*>("editPwd")->text(), QString("program-password"));
        QVERIFY(QFile::remove(portablePath));
        preferred.setEnvironment("Pro");
        QCOMPARE(preferred.findChild<QLineEdit*>("editPwd")->text(), QString("sample-password"));
        global.m_strNingMengCookie = "old-cookie"; global.m_nServiceId = 123; global.m_bLogin = true;
        global.m_userToken.SetAccessToken("old-jwt");
        environment->setCurrentIndex(1);
        QCOMPARE(lemoncofighelper::CurrentHttpEnvironment(), QString("Dev2"));
        QVERIFY(lemoncofighelper::AccountConfigValue("signInUrl").contains("authtest."));
        QVERIFY(lemoncofighelper::AccountConfigValue("wbgatePlanUrl").contains("etest."));
        QVERIFY(global.m_strNingMengCookie.isEmpty()); QVERIFY(global.m_userToken.m_strAccessToken.isEmpty());
        QVERIFY(!global.m_bLogin); QCOMPARE(global.m_nServiceId, 0);
        QCOMPARE(startup.findChild<QLineEdit*>("editUserName")->text(), QString("13000000002"));
        QVERIFY(QFileInfo::exists(credentials.filePath("LemonApp.Dev2.dpapi")));
        QVERIFY(QMetaObject::invokeMethod(password, "loginBusyChanged", Qt::DirectConnection, Q_ARG(bool, true)));
        QVERIFY(!environment->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(password, "loginBusyChanged", Qt::DirectConnection, Q_ARG(bool, false)));
        environment->setCurrentIndex(0);
        QCOMPARE(lemoncofighelper::CurrentHttpEnvironment(), QString("Pro"));
        QVERIFY(lemoncofighelper::AccountConfigValue("signInUrl").contains("https://auth."));
        startup.findChild<QLineEdit*>("editPwd")->clear();
        startup.findChild<QPushButton*>("btnPwdLogin")->click(); // Validation only; no real request.
        QVERIFY(environment->isEnabled()); QVERIFY(!global.m_bLogin);
        startup.show(); QTest::qWait(20);
        const auto captureDir = qEnvironmentVariable("BALANCE_TEST_SCREENSHOTS");
        if (!captureDir.isEmpty()) QVERIFY(startup.grab().save(captureDir + "/password-login.png"));
        lemoncofighelper::SetHttpEnvironment("Local");
    }

    void cliRequestsCarryInternalSessionHeader() {
        FakeServer server;
        server.response = [](const QByteArray &) { return QByteArray("{}"); };
        for (const auto &method : QStringList{"POST", "GET"}) {
            HttpRequestSpec request;
            request.method = method;
            request.url = QUrl(server.host() + (method == "POST" ? "/token" : "/accountsets"));
            request.timeoutMs = 3000;
            request.rawHeaders.append(qMakePair(QByteArray("Cookie"), QByteArray("ningmengcookie=fixture")));
            request.body = method == "POST" ? QByteArray("fixture-body") : QByteArray();
            const auto response = sendHttpRequest(request);
            QCOMPARE(response.httpStatus, 200);
            QVERIFY(!response.timedOut);
            const auto sent = server.requests.last();
            const auto headers = (sent.left(sent.indexOf("\r\n\r\n")) + "\r\n").toLower();
            QVERIFY(headers.contains("\r\nlmlimit: internal\r\n"));
            QVERIFY(headers.contains("\r\ncookie: ningmengcookie=fixture\r\n"));
            QVERIFY(sent.endsWith(request.body));
        }
        QCOMPARE(server.requests.size(), 2);
    }

    void refreshDeadlineUsesServerExpiry_data() {
        QTest::addColumn<int>("cookieSeconds");
        QTest::addColumn<int>("jwtSeconds");
        QTest::addColumn<int>("expectedRefreshSeconds");
        QTest::newRow("missing-expiry") << 0 << 0 << -1;
        QTest::newRow("12-hour-grants") << 43200 << 43200 << 39600;
        QTest::newRow("24-hour-grants") << 86400 << 86400 << 82800;
        QTest::newRow("24-hour-cookie-no-jwt-exp") << 86400 << 0 << 82800;
        QTest::newRow("jwt-only") << 0 << 86400 << 82800;
        QTest::newRow("48-hour-grants") << 172800 << 172800 << 169200;
        QTest::newRow("shorter-jwt") << 86400 << 7200 << 3600;
        QTest::newRow("shorter-cookie") << 7200 << 86400 << 3600;
        QTest::newRow("short-grant") << 1800 << 86400 << 900;
    }

    void refreshDeadlineUsesServerExpiry() {
        QFETCH(int, cookieSeconds);
        QFETCH(int, jwtSeconds);
        QFETCH(int, expectedRefreshSeconds);
        FakeServer server;
        QTemporaryDir credentials;
        const QString configPath = QCoreApplication::applicationDirPath() + "/Resource/http/Dev2/Account.json";
        QVERIFY(!QFileInfo::exists(configPath));
        struct Cleanup {
            QString path, environment;
            ~Cleanup() { QFile::remove(path); lemoncofighelper::SetHttpEnvironment(environment); }
        } cleanup{configPath, lemoncofighelper::CurrentHttpEnvironment()};
        QDir().mkpath(QFileInfo(configPath).absolutePath());
        QFile config(configPath); QVERIFY(config.open(QIODevice::WriteOnly));
        config.write(QJsonDocument(QJsonObject{{"signInUrl", server.host() + "/token"}, {"authHost", server.host()},
            {"wbgateMenuUrl", server.host() + "/menu"}, {"wbgatePlanUrl", server.host() + "/plan"}}).toJson());
        config.close();
        const auto started = QDateTime::currentDateTimeUtc();
        server.response = [=](const QByteArray &request) {
            if (request.startsWith("POST /token")) {
                QJsonObject token{{"access_token", "fixture-cookie"}};
                if (cookieSeconds) token["expires_in"] = cookieSeconds;
                return QJsonDocument(token).toJson();
            }
            if (request.startsWith("POST /v2/api/jwt")) {
                QJsonObject payload{{"sub", "123"}};
                if (jwtSeconds) payload["exp"] = double(started.addSecs(jwtSeconds).toSecsSinceEpoch());
                const auto encoded = QJsonDocument(payload).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding);
                return QJsonDocument(QJsonObject{{"success", true}, {"token", "header." + QString::fromLatin1(encoded) + ".signature"}}).toJson();
            }
            if (request.startsWith("GET /plan"))
                return QByteArray(R"({"data":{"data":[{"productType":"acc","id":45,"administrator":123,"lastAccountSet":{"appAsid":"678"}}]}})");
            return QByteArray("{}");
        };
        lemoncofighelper::SetHttpEnvironment("Dev2");
        GlobalData global;
        PwdLoginWidget login(&global, nullptr, credentials.path());
        login.findChild<QLineEdit*>("editUserName")->setText("13000000001");
        login.findChild<QLineEdit*>("editPwd")->setText("fixture-password");
        QSignalSpy success(&login, &PwdLoginWidget::login_success);
        QSignalSpy status(&login, &PwdLoginWidget::sessionStatusChanged);
        login.findChild<QPushButton*>("btnPwdLogin")->click();
        QTRY_COMPARE(success.count(), 1);
        if (expectedRefreshSeconds < 0) {
            QVERIFY(global.m_bLogin);
            QVERIFY(!login.sessionExpiresAt().isValid());
            QVERIFY(!login.nextRefreshAt().isValid());
            QVERIFY(status.last().at(1).toBool());
            QVERIFY(status.last().at(0).toString().contains(QStringLiteral("未提供有效期")));
            return;
        }
        QVERIFY(!status.last().at(1).toBool());
        const qint64 actual = started.secsTo(login.nextRefreshAt());
        QCOMPARE(login.nextRefreshAt().toLocalTime().toSecsSinceEpoch(), login.nextRefreshAt().toSecsSinceEpoch());
        QVERIFY(status.last().at(0).toString().contains(login.nextRefreshAt().toLocalTime().toString("MM-dd HH:mm")));
        QVERIFY2(qAbs(actual - expectedRefreshSeconds) <= 3,
            qPrintable(QString("refresh delay: actual=%1 seconds, expected=%2 seconds").arg(actual).arg(expectedRefreshSeconds)));
        QCOMPARE(login.nextRefreshAt().toLocalTime().toSecsSinceEpoch(), login.nextRefreshAt().toSecsSinceEpoch());
    }

    void loginRequestsStayInSelectedEnvironment() {
        FakeServer server;
        QTemporaryDir credentials;
        struct ConfigCleanup {
            QStringList paths;
            ~ConfigCleanup() { for (const auto &path : paths) QFile::remove(path); }
        } cleanup;
        for (const auto &environment : QStringList{"Pro", "Dev2"}) {
            const QString path = QCoreApplication::applicationDirPath() + "/Resource/http/" + environment + "/Account.json";
            QVERIFY(!QFileInfo::exists(path)); // Only the dedicated test executable's new config files.
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile config(path); QVERIFY(config.open(QIODevice::WriteOnly));
            const QString host = server.host() + "/" + environment;
            config.write(QJsonDocument(QJsonObject{{"signInUrl", host + "/token"}, {"authHost", host},
                {"wbgateMenuUrl", host + "/menu"}, {"wbgatePlanUrl", host + "/plan"}}).toJson());
            config.close(); cleanup.paths.append(path);
        }
        lemoncofighelper::SetHttpEnvironment("Pro");
        GlobalData global;
        StartUpWindow startup(&global, nullptr, credentials.path());
        auto *password = startup.findChild<PwdLoginWidget*>();
        auto *environment = startup.findChild<QComboBox*>("loginEnvironment");
        auto *user = startup.findChild<QLineEdit*>("editUserName");
        auto *secret = startup.findChild<QLineEdit*>("editPwd");
        user->setText("13000000001"); secret->setText("test+&password");
        startup.findChild<QPushButton*>("btnPwdLogin")->click();
        QTRY_COMPARE(server.requests.size(), 1); // Held response: no request can continue to another environment.
        QVERIFY(!environment->isEnabled());
        startup.findChild<QPushButton*>("btnPwdLogin")->click();
        QCOMPARE(server.requests.size(), 1);
        environment->setCurrentIndex(1); // Programmatic change must abort the previous attempt too.
        QVERIFY(environment->isEnabled());
        QVERIFY(!global.m_bLogin);
        server.response = [](const QByteArray &request) {
            if (request.startsWith("POST /Dev2/token")) return QByteArray(R"({"access_token":"test-cookie","expires_in":43200})");
            if (request.startsWith("POST /Dev2/v2/api/jwt"))
                return QByteArray(R"({"success":true,"token":"header.eyJzdWIiOiIxMjMifQ.signature"})");
            if (request.startsWith("GET /Dev2/plan"))
                return QByteArray(R"({"data":{"data":[{"productType":"acc","id":45,"administrator":123,"lastAccountSet":{"appAsid":"678"}}]}})");
            return QByteArray("{}");
        };
        user->setText("13000000002"); secret->setText("test+&password");
        QSignalSpy success(password, &PwdLoginWidget::login_success);
        startup.findChild<QPushButton*>("btnPwdLogin")->click();
        QTRY_COMPARE(success.count(), 1);
        QCOMPARE(server.requests.size(), 5);
        for (const auto &request : server.requests)
            QVERIFY((request.left(request.indexOf("\r\n\r\n")) + "\r\n").toLower().contains("\r\nlmlimit: internal\r\n"));
        for (int i = 1; i < server.requests.size(); ++i) QVERIFY(server.requests[i].split('\n').first().contains("/Dev2/"));
        QVERIFY(server.requests[1].contains("password=test%2B%26password"));
        QCOMPARE(global.m_nServiceId, 45); QCOMPARE(global.m_nUserId, 123);
        QCOMPARE(global.m_strNingMengCookie, QString("test-cookie"));
        QVERIFY(QFileInfo::exists(credentials.filePath("LemonApp.Dev2.dpapi")));
        {
            GlobalData reopened;
            StartUpWindow noPassword(&reopened, nullptr, credentials.path());
            QCOMPARE(noPassword.findChild<QLineEdit*>("editUserName")->text(), QString("13000000002"));
            QVERIFY(noPassword.findChild<QLineEdit*>("editPwd")->text().isEmpty());
        }
        // Renewal uses the same complete login chain, with an atomic session replacement.
        const auto next = password->nextRefreshAt();
        QVERIFY(qAbs(QDateTime::currentDateTimeUtc().secsTo(next) - 11 * 3600) < 10);
        QVERIFY(qAbs(QDateTime::currentDateTimeUtc().secsTo(password->sessionExpiresAt()) - 12 * 3600) < 10);
        const auto originalJwt = global.m_userToken.m_strAccessToken;
        const auto originalExpiry = password->sessionExpiresAt();
        global.m_nServiceId = 777; global.m_strAppAsId = "selected-plan";
        server.response = [](const QByteArray &request) {
            if (request.startsWith("POST /Dev2/token")) return QByteArray(R"({"access_token":"uncommitted-cookie"})");
            return QByteArray("{}"); // JWT failure after the Cookie was acquired.
        };
        QSignalSpy status(password, &PwdLoginWidget::sessionStatusChanged);
        password->refreshSession();
        password->refreshSession(); // Busy attempts must not duplicate requests.
        QTRY_VERIFY(!password->isBusy());
        QCOMPARE(server.requests.size(), 7);
        QCOMPARE(global.m_strNingMengCookie, QString("test-cookie"));
        QCOMPARE(global.m_userToken.m_strAccessToken, originalJwt);
        QCOMPARE(global.m_nServiceId, 777); QCOMPARE(global.m_strAppAsId, QString("selected-plan"));
        QCOMPARE(password->sessionExpiresAt(), originalExpiry);
        QVERIFY(global.m_bLogin);
        QVERIFY(status.last().at(1).toBool());
        QVERIFY(qAbs(QDateTime::currentDateTimeUtc().secsTo(password->nextRefreshAt()) - 300) < 10);
        QCOMPARE(success.count(), 1); // Renewal cannot close/reopen the login window.
        server.response = [](const QByteArray &request) {
            if (request.startsWith("POST /Dev2/token")) return QByteArray(R"({"access_token":"renewed-cookie","expires_in":43200})");
            if (request.startsWith("POST /Dev2/v2/api/jwt")) {
                const auto payload = QJsonDocument(QJsonObject{{"sub", "123"},
                    {"exp", double(QDateTime::currentSecsSinceEpoch() + 7200)}}).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding);
                return QJsonDocument(QJsonObject{{"success", true}, {"token", "header." + QString::fromLatin1(payload) + ".signature"}}).toJson();
            }
            if (request.startsWith("GET /Dev2/plan"))
                return QByteArray(R"({"data":{"data":[{"productType":"acc","id":45,"administrator":123,"lastAccountSet":{"appAsid":"678"}}]}})");
            return QByteArray("{}");
        };
        password->refreshSession();
        QTRY_VERIFY(!password->isBusy());
        QCOMPARE(global.m_strNingMengCookie, QString("renewed-cookie"));
        QCOMPARE(global.m_nServiceId, 777); QCOMPARE(global.m_strAppAsId, QString("selected-plan"));
        QVERIFY(!status.last().at(1).toBool());
        QVERIFY(qAbs(QDateTime::currentDateTimeUtc().secsTo(password->nextRefreshAt()) - 3600) < 10);
        QCOMPARE(success.count(), 1);
        // Environment switch uses only the destination's persisted credentials.
        startup.findChild<QCheckBox*>("checkBtnAutoPwd")->setChecked(true);
        secret->setText("saved+test&password");
        startup.findChild<QPushButton*>("btnPwdLogin")->click();
        QTRY_COMPARE(success.count(), 2);
        GlobalData switched;
        StartUpWindow target(&switched, nullptr, credentials.path());
        QCOMPARE(target.findChild<QLineEdit*>("editPwd")->text(), QString("saved+test&password"));
        target.prepareEnvironmentSwitch();
        QVERIFY(!target.findChild<QComboBox*>("loginEnvironment")->isEnabled());
        QSignalSpy switchedSuccess(target.sessionLogin(), &PwdLoginWidget::login_success);
        QTRY_COMPARE(switchedSuccess.count(), 1);
        QVERIFY(switched.m_bLogin);
        QVERIFY(!target.findChild<QComboBox*>("loginEnvironment")->isEnabled());
        const auto requestCount = server.requests.size();
        QVERIFY(server.requests[requestCount - 4].contains("password=saved%2Btest%26password"));
        QFile legacy(credentials.filePath("LemonApp.Dev2.dat"));
        QVERIFY(legacy.open(QIODevice::WriteOnly));
        legacy.write(R"({"re":1,"u":"13000000002","p":"obsolete-password"})");
        legacy.close();
        target.findChild<QCheckBox*>("checkBtnAutoPwd")->setChecked(false);
        target.findChild<QPushButton*>("btnPwdLogin")->click();
        QTRY_COMPARE(switchedSuccess.count(), 2);
        GlobalData reopened;
        StartUpWindow forgotten(&reopened, nullptr, credentials.path());
        QCOMPARE(forgotten.findChild<QLineEdit*>("editUserName")->text(), QString("13000000002"));
        QVERIFY(forgotten.findChild<QLineEdit*>("editPwd")->text().isEmpty());
        QTemporaryDir portableDirectory;
        QVERIFY(portableDirectory.isValid());
        GlobalData portableGlobal;
        PwdLoginWidget portable(&portableGlobal, nullptr, portableDirectory.path(), credentials.path());
        portable.findChild<QCheckBox*>("checkBtnAutoPwd")->setChecked(true);
        portable.findChild<QLineEdit*>("editPwd")->setText("copied-build-password");
        QSignalSpy portableSuccess(&portable, &PwdLoginWidget::login_success);
        portable.findChild<QPushButton*>("btnPwdLogin")->click();
        QTRY_COMPARE(portableSuccess.count(), 1);
        const QString portablePath = portableDirectory.filePath("LemonApp.Dev2.dpapi");
        QVERIFY(QFileInfo::exists(portablePath));
        QVERIFY(QFile::remove(portablePath));
        portable.setEnvironment("Dev2");
        QCOMPARE(portable.findChild<QLineEdit*>("editPwd")->text(), QString("copied-build-password"));
        lemoncofighelper::SetHttpEnvironment("Local");
    }
};

QTEST_MAIN(SubjectBalanceImportTests)
#include "SubjectBalanceImportTests.moc"
