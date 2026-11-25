#include <QtWidgets>
#include <QStandardPaths>
#include <cstdio>
// This offline harness can inspect navigation without adding a login bypass to the app.
#include "QtWidgetsApplication1.h"
#include "functionselectwidget.h"
#include "lemoncofighelper.h"
#include "deleteaccountsetwidget.h"
#include "backupaccountsetwidget.h"
#include "subjectconflictdialog.h"
#include "Common/globaldata.h"
#include "Common/LocalToolEndpoint.h"
#include "Common/LocalSqlInstances.h"
#include "Common/repairpathsettings.h"
#include "Common/WecomDocumentData.h"
#include "Common/WecomLocalServer.h"
#include "subjectbalanceimportwidget.h"
#include <QLocalSocket>
#include <QTcpServer>
#include <QTcpSocket>

void runSqlInstanceTests();
void runBakRestoreTests();
void runRestoreCompatibilityTests();
void runDatabaseCleanerTests();
void runDatabaseCleanupWidgetTests();
int runWorkspaceExitTest(const QString &scenario);

static void require(bool condition, const char *message)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static void settle()
{
    for (int i = 0; i < 5; ++i) QApplication::processEvents();
}

// Exchanges a single JSON request with an isolated workbench test pipe.
static QJsonObject queryWorkspacePipe(const QString &endpoint, const QJsonObject &request)
{
    QLocalSocket pipe;
    QElapsedTimer timer;
    timer.start();
    pipe.connectToServer(endpoint);
    while (pipe.state() != QLocalSocket::ConnectedState && timer.elapsed() < 3000) {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    require(pipe.state() == QLocalSocket::ConnectedState, "workspace test pipe accepts connection");
    pipe.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    pipe.flush();
    QByteArray response;
    while (!response.endsWith('\n') && timer.elapsed() < 5000) {
        QApplication::processEvents();
        response += pipe.readAll();
        QThread::msleep(1);
    }
    require(response.endsWith('\n'), "workspace test pipe replies with a complete frame");
    return QJsonDocument::fromJson(response).object();
}

// Exercises the live workbench session with synthetic values only.
static void verifyDesktopCookie(QtWidgetsApplication1 &window, GlobalData &data)
{
    const QString endpoint = LocalToolEndpoint::name("Auth") + ".test." + QString::number(QCoreApplication::applicationPid());
    QJsonObject request{{"protocolVersion", LocalToolEndpoint::ProtocolVersion}, {"action", "read_cookie"}, {"reveal", true}};
    const auto errorCode = [&]() {
        return queryWorkspacePipe(endpoint, request).value("error").toObject().value("code").toString();
    };
    data.m_strNingMengCookie = "synthetic-desktop-cookie-one";
    require(errorCode() == "auth_required", "logged-out workspace cannot expose a retained Cookie");

    data.m_bLogin = true;
    const auto environment = lemoncofighelper::CurrentHttpEnvironment();
    request["environment"] = environment == "Pro" ? "Dev2" : "Pro";
    require(errorCode() == "environment_mismatch", "mismatched environment cannot read a Cookie");
    request.remove("environment");
    request["reveal"] = "true";
    require(errorCode() == "invalid_request", "invalid reveal value does not expose a Cookie");

    request["reveal"] = false;
    const auto masked = queryWorkspacePipe(endpoint, request).value("data").toObject();
    require(masked.value("ningmengCookie") == QJsonValue("***") && masked.value("revealed") == QJsonValue(false)
        && masked.value("environment") == QJsonValue(environment) && masked.value("source") == QJsonValue("desktop"),
        "desktop Cookie reply defaults to masked and identifies its environment");

    request["reveal"] = true;
    const auto first = queryWorkspacePipe(endpoint, request).value("data").toObject();
    require(first.value("ningmengCookie") == QJsonValue(data.m_strNingMengCookie)
        && first.value("revealed") == QJsonValue(true), "explicit reveal returns the active synthetic Cookie");
    data.m_strNingMengCookie = "synthetic-desktop-cookie-two";
    const auto refreshed = queryWorkspacePipe(endpoint, request).value("data").toObject();
    require(refreshed.value("ningmengCookie") == QJsonValue(data.m_strNingMengCookie), "next request reads the refreshed Cookie");

    window.setEnabled(false);
    require(errorCode() == "busy", "environment switch blocks Cookie reads");
    window.setEnabled(true);
    data.m_bLogin = false;
    data.m_strNingMengCookie.clear();
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--exit-test")
        return runWorkspaceExitTest(QString::fromLocal8Bit(argv[2]));
    require(argc == 2, "screenshot directory argument");
    const QString output = QString::fromLocal8Bit(argv[1]);
    QDir().mkpath(output);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, output + "/settings");
    QStandardPaths::setTestModeEnabled(true);
    RepairPathSettings::setInputDir(output + "/sample-input");
    RepairPathSettings::setOutputDir(output + "/sample-output");
    QDir().mkpath(RepairPathSettings::inputDir());
    runBakRestoreTests();
    if (qEnvironmentVariable("BAK_RESTORE_ONLY") == "1") return 0;
    runRestoreCompatibilityTests();
    if (qEnvironmentVariable("SQL_RESTORE_COMPATIBILITY_ONLY") == "1") return 0;
    runSqlInstanceTests();
    runDatabaseCleanerTests();
    runDatabaseCleanupWidgetTests();
    GlobalData data;
    data.m_strUserName = QStringLiteral("界面预览");
    QtWidgetsApplication1 window(&data);
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    window.resize(1440, 960);
    settle();
    require(window.findChild<WecomLocalServer*>("wecomLocalServer"), "local document API starts with workspace");
    const auto status = queryWorkspacePipe(WecomLocalServer::defaultServerName(),
        {{"protocolVersion", LocalToolEndpoint::ProtocolVersion}, {"action", "status"}});
    require(status.value("ok").toBool(), "workspace serves structured status");
    require(!window.findChild<QWidget*>("wecomDocumentPage"), "status does not instantiate document page or browser");
    verifyDesktopCookie(window, data);
    auto capture = [&](const QString &name) {
        settle();
        require(window.grab().save(output + "/" + name + ".png"), "save screenshot");
    };
    auto *catalog = window.findChild<FunctionSelectWidget*>();
    auto *mainStack = window.findChild<QStackedWidget*>("mainStack");
    require(catalog, "catalog exists");
    require(catalog->findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly).isEmpty(),
        "all cards owned by category canvases, no floating cards");
    capture("00-workspace");
    QSet<int> ids;
    const int counts[] = {0, 4, 6, 5, 5, 3};
    auto *pages = catalog->findChild<QStackedWidget*>("categoryPages");
    for (int category = 1; category <= 5; ++category) {
        window.findChild<QPushButton*>(QString("navCategory%1").arg(category))->click();
        settle();
        require(catalog->currentCategory() == category, "sidebar switches category page");
        int count = 0;
        for (auto *button : pages->currentWidget()->findChildren<QPushButton*>()) {
            const int id = button->property("functionId").toInt();
            if (!id) continue;
            require(FunctionSelectWidget::categoryForFunction(id) == category, "tool in correct category");
            require(!ids.contains(id), "no duplicate category entry");
            ids.insert(id);
            ++count;
        }
        require(count == counts[category], "expected category tool count");
        capture(QString("0%1-category").arg(category));
    }
    require(ids.size() == 23, "23 entries including Wecom document reader");
    require(ids.contains(20) && !ids.contains(19) && !ids.contains(22) && !ids.contains(25)
        && !ids.contains(26) && !ids.contains(27), "old restore cards merged into a single entry");
    window.findChild<QPushButton*>("btnSidebarHome")->click();
    auto *search = catalog->findChild<QLineEdit*>("toolSearch0");
    search->setText("json");
    settle();
    int matches = 0;
    for (auto *button : pages->currentWidget()->findChildren<QPushButton*>())
        if (button->property("functionId").isValid() && button->isVisible()) {
            ++matches;
            const int id = button->property("functionId").toInt();
            require(id == 8 || id == 28, "case-insensitive global search includes JSON export tool");
        }
    require(matches == 2, "global search finds JSON query and Wecom JSON export");
    capture("06-search");
    search->setText("no-such-tool-982734");
    settle();
    require(pages->currentWidget()->findChild<QLabel*>("emptySearchState")->isVisible(), "empty search feedback");
    search->clear();
    catalog->findChild<QPushButton*>("categoryCard2")->click();
    require(catalog->currentCategory() == 2, "dashboard category card navigation");
    auto *categorySearch = catalog->findChild<QLineEdit*>("toolSearch2");
    categorySearch->setText(QStringLiteral("空"));
    window.findChild<QPushButton*>("navCategory3")->click();
    window.findChild<QPushButton*>("navCategory2")->click();
    require(categorySearch->text() == QStringLiteral("空"), "category search preserved on return");
    categorySearch->clear();
    window.findChild<QPushButton*>("btnSidebarHome")->click();
    catalog->findChild<QPushButton*>("btnBackupRestoreShortcut")->click();
    require(mainStack->currentWidget() != catalog, "dashboard shortcut opens tool");
    auto *backPage = qobject_cast<QScrollArea*>(mainStack->currentWidget())->widget();
    require(QMetaObject::invokeMethod(backPage, "backToMain"), "tool back signal connected");
    require(catalog->currentCategory() == 4 && mainStack->currentWidget() == catalog,
        "shortcut returns to owning category");

    // Local page navigation: no processing, clipboard access, upload, deletion or cloud requests.
    for (int id : {3, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 25, 26, 27, 28}) {
        require(QMetaObject::invokeMethod(&window, "onFunctionSelected", Qt::DirectConnection, Q_ARG(int, id)), "open tool route");
        settle();
        auto *scroll = qobject_cast<QScrollArea*>(mainStack->currentWidget());
        require(scroll && scroll->widget(), "tool page has scrolling container");
        if (id == 20) {
            auto* software = scroll->widget()->findChild<QComboBox*>("restoreSoftware");
            require(software && software->currentData().toInt() == 19, "reopening unified entry preserves selected software");
            require(software->findData(28) >= 0, "XinZhongDa is available in unified restore entry");
            software->setCurrentIndex(software->findData(28));
            settle();
            require(scroll->widget()->findChild<QTableWidget*>("xzdYears"), "XinZhongDa annual snapshot selector exists");
            capture("tool-xzd");
            window.resize(800, 480);
            settle();
            require(window.width() == 800 && window.height() == 480, "XinZhongDa supports small window");
            capture("small-xzd");
            window.resize(1440, 960);
            require(software->findData(29) >= 0, "generic BAK restore is available in unified entry");
            software->setCurrentIndex(software->findData(29));
            settle();
            require(scroll->widget()->findChild<QTableWidget*>("bakEntries"), "generic BAK per-file version table exists");
            capture("tool-bak");
            window.resize(800, 480);
            settle();
            require(window.width() == 800 && window.height() == 480, "generic BAK supports small window");
            capture("small-bak");
            window.resize(1440, 960);
            software->setCurrentIndex(software->findData(20));
            settle();
        }
        if (id == 21) {
            auto* target = scroll->widget()->findChild<QComboBox*>("cleanupTarget");
            auto* remove = scroll->widget()->findChild<QPushButton*>("cleanupDelete");
            auto* table = scroll->widget()->findChild<QTableWidget*>("cleanupDatabases");
            require(target && target->count() == qMax(1, LocalSqlInstances::discover().size()) + 1 && !target->isEditable(), "discovered local instances and fixed remote target");
            require(remove && !remove->isEnabled(), "delete disabled without inspected database list");
            require(scroll->widget()->findChildren<QLineEdit*>().isEmpty(), "no password or server text input");
            target->setCurrentIndex(1);
            require(table->rowCount() == 0 && !remove->isEnabled(), "switching target clears plan without opening connection");
            target->setCurrentIndex(0);
        }
        capture(QString("tool-%1").arg(id, 2, 10, QLatin1Char('0')));
        window.resize(800, 480);
        settle();
        require(window.width() == 800 && window.height() == 480, "tool does not force oversized window");
        require(scroll->widget()->size().height() >= scroll->viewport()->height(), "small window content accessible");
        if (id == 8 || id == 18 || id == 19 || id == 20 || id == 21 || id == 22 || id == 23 || id == 24 || id == 25 || id == 26 || id == 27 || id == 28) capture(QString("small-%1").arg(id));
        if (id == 28) {
            auto *selector = scroll->widget()->findChild<QComboBox*>("wecomDocumentSelector");
            require(selector && selector->count() > 0, "document dropdown starts with a saved choice");
            auto *popup = selector->view()->window();
            popup->setAttribute(Qt::WA_DontShowOnScreen);
            selector->showPopup();
            settle();
            const auto index = selector->model()->index(selector->currentIndex(), selector->modelColumn());
            require(selector->view()->visualRect(index).height() >= 36, "document popup has a readable row height");
            auto preview = window.grab();
            QPainter painter(&preview);
            painter.drawPixmap(window.mapFromGlobal(popup->mapToGlobal(QPoint())), popup->grab());
            painter.end();
            require(preview.save(output + "/wecom-document-popup.png"), "save opened document popup");
            selector->hidePopup();
        }
        require(QMetaObject::invokeMethod(&window, "onBackToMain"), "back route");
        require(catalog->currentCategory() == FunctionSelectWidget::categoryForFunction(id), "back returns to owning category");
        require(mainStack->currentWidget() == catalog, "back shows catalog");
        window.resize(1440, 960);
    }
    // The balance page queries requirements on entry; supply loopback data instead of a cloud host.
    {
        QTcpServer server;
        require(server.listen(QHostAddress::LocalHost), "balance requirements loopback listener");
        const QJsonArray rules{QJsonObject{{"softwareId", 99999}, {"softwareName", "Template"},
            {"subjectStatus", 3}, {"trialBalanceStatus", 1}, {"voucherStatus", 2}, {"remark", ""}}};
        const auto body = QJsonDocument(QJsonObject{{"state", 1000}, {"data", rules}}).toJson();
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, body]() {
            auto *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, body]() {
                const auto request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n") || socket->property("handled").toBool()) return;
                require(request.startsWith("GET /api/ImportFromOther/GetSoftwareFileRequirements "), "only requirements GET permitted");
                socket->setProperty("handled", true);
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        auto *requirements = new BalanceImportRequirements(&window, output + "/requirements");
        requirements->load("http://127.0.0.1:" + QString::number(server.serverPort()));
        QElapsedTimer timer; timer.start();
        while (requirements->loading() && timer.elapsed() < 6000) { QApplication::processEvents(); QThread::msleep(1); }
        require(!requirements->catalog().isEmpty(), "balance requirements loaded offline");
        auto *page = new SubjectBalanceImportWidget(&data, &window, output + "/balance-history.json", requirements);
        auto *scroll = new QScrollArea(mainStack);
        scroll->setWidgetResizable(true); scroll->setWidget(page);
        mainStack->addWidget(scroll); mainStack->setCurrentWidget(scroll);
        window.findChild<QLabel*>("headerTitle")->setText(QStringLiteral("科目余额表导账"));
        auto *software = page->findChild<QComboBox*>("balanceSoftware");
        require(software && software->count() == 2, "server software entry and placeholder");
        require(!page->findChild<QCheckBox*>("balanceImportZero")->isChecked(), "zero balance disabled by default");
        software->setCurrentIndex(software->findData(99999));
        require(page->findChild<QLineEdit*>("balanceFile_trialbalance"), "dynamic file fields");
        capture("tool-24"); window.resize(800, 480); capture("small-24"); window.resize(1440, 960);
    }
    // Cloud tables are instantiated directly so that loadAccountSetList never issues a request.
    for (int id : {1, 2}) {
        QWidget *page = id == 1 ? static_cast<QWidget*>(new DeleteAccountSetWidget(&data, &window))
            : static_cast<QWidget*>(new BackupAccountSetWidget(&data, &window));
        window.findChild<QPushButton*>("navCategory1")->click();
        auto *scroll = new QScrollArea(mainStack);
        scroll->setWidgetResizable(true);
        scroll->setWidget(page);
        mainStack->addWidget(scroll);
        mainStack->setCurrentWidget(scroll);
        window.findChild<QLabel*>("headerTitle")->setText(id == 1 ? QStringLiteral("删除账套") : QStringLiteral("备份下载"));
        window.findChild<QLabel*>("headerSubtitle")->setText(QStringLiteral("界面预览 · 示例数据"));
        auto *table = page->findChild<QTableWidget*>("accountTable");
        require(table, "cloud account table exists");
        auto *cloudPages = page->findChild<QStackedWidget*>();
        require(cloudPages && cloudPages->count() == 3, "loading, list and result subpages exist");
        cloudPages->setCurrentIndex(1);
        settle();
        require(table->isVisible(), "cloud table visible without network loading");
        table->setRowCount(3);
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < table->columnCount(); ++col)
                table->setItem(row, col, new QTableWidgetItem(col == 1
                    ? QStringLiteral("示例账套 %1").arg(row + 1) : QStringLiteral("—")));
        capture(QString("tool-%1").arg(id, 2, 10, QLatin1Char('0')));
        cloudPages->setCurrentIndex(2);
        auto *resultTable = page->findChild<QTableWidget*>("resultTable");
        require(resultTable && resultTable->isVisible(), "result table visible");
        resultTable->setRowCount(1);
        resultTable->setItem(0, 0, new QTableWidgetItem(QStringLiteral("示例账套")));
        resultTable->setItem(0, 1, new QTableWidgetItem(QStringLiteral("示例状态")));
        resultTable->setItem(0, 2, new QTableWidgetItem(QStringLiteral("仅验证页面布局，未执行业务操作")));
        capture(QString("result-%1").arg(id));
    }
    SubjectConflict conflict;
    conflict.acctCode = "1001";
    conflict.names = QStringList{QStringLiteral("库存现金"), QStringLiteral("现金")};
    SubjectConflictDialog dialog({conflict}, QStringLiteral("示例账套"), &window);
    dialog.setAttribute(Qt::WA_DontShowOnScreen);
    dialog.show();
    settle();
    require(dialog.grab().save(output + "/conflict-dialog.png"), "dialog screenshot");
    dialog.close();
    window.findChild<QPushButton*>("btnSidebarHome")->click();
    window.resize(800, 480);
    capture("small-workspace");
    window.resize(1440, 960);
    capture("00-workspace");
    auto *tray = window.findChild<QSystemTrayIcon*>();
    require(tray && tray->isVisible(), "tray icon stays registered");
    const bool production = lemoncofighelper::CurrentHttpEnvironment() == "Pro";
    const QString environmentText = production ? QStringLiteral("正式环境") : QStringLiteral("测试环境");
    require(window.windowTitle().contains(environmentText), "window title identifies environment");
    require(tray->toolTip().contains(environmentText), "tray tooltip identifies environment");
    require(window.findChild<QPushButton*>("btnSwitchEnvironment")->text().contains(environmentText), "all pages show environment menu");
    require(window.windowIcon().pixmap(32).save(output + "/tray-icon.png"), "tray icon preview");
    auto *busyPage = new QWidget(&window);
    busyPage->setProperty("sessionBusyTest", true);
    require(window.hasActiveTasks(), "busy pages block environment switching even between requests");
    busyPage->setProperty("sessionBusyTest", false);
    require(!window.hasActiveTasks(), "idle workspace permits switching");
    int switchRequests = 0;
    QObject::connect(&window, &QtWidgetsApplication1::switchEnvironmentRequested, [&]() { ++switchRequests; });
    window.findChild<QAction*>("actionSwitchEnvironment")->trigger();
    require(switchRequests == 1, "environment menu dispatches switch");
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        window.close();
        settle();
        require(!window.isVisible() && tray->isVisible(), "close hides only window and keeps tray");
        tray->activated(QSystemTrayIcon::Trigger);
        settle();
        require(window.isVisible(), "tray click restores window");
    }
    {
        // The advertised IPC document stays fixed even when the page selects another table.
        auto *documents = window.findChild<QComboBox*>("wecomDocumentSelector");
        require(documents && !documents->isEditable(), "saved document selector exists after route tests");
        documents->addItem(QStringLiteral("示例文档"), QUrl("https://doc.weixin.qq.com/sheet/smoke_example"));
        documents->setCurrentIndex(documents->count() - 1);
        window.hide();
        QLocalSocket pipe; pipe.connectToServer(WecomLocalServer::defaultServerName());
        QElapsedTimer timer; timer.start();
        while (pipe.state()!=QLocalSocket::ConnectedState && timer.elapsed()<3000) { QApplication::processEvents(); QThread::msleep(1); }
        require(pipe.state()==QLocalSocket::ConnectedState, "hidden workspace keeps API available");
        pipe.write("{\"protocolVersion\":1,\"action\":\"status\"}\n"); pipe.flush();
        QByteArray response;
        while (!response.endsWith('\n') && timer.elapsed()<5000) { QApplication::processEvents(); response+=pipe.readAll(); QThread::msleep(1); }
        const auto object=QJsonDocument::fromJson(response).object();
        require(object.value("ok").toBool() && object.value("data").toObject().value("documentId").toString() == WecomDocument::documentId(),
            "IPC advertises the fixed project document independently of selection");
        require(!window.hasActiveTasks() && !window.isVisible(), "background status leaves workspace idle and hidden");
    }
    fprintf(stdout, "PASS: workspace API auto-start, lazy browser, and fixed document status after switching tables.\n");
    fprintf(stdout, "PASS: environment palette/menu/icon, task guard, close-to-tray and restore.\n");
    fprintf(stdout, "PASS: 23 tool entries, 5 categories, search, 18 local routes, mocked balance page and 5 restore aliases, category return, small windows, cloud tables and dialog.\n");
    return 0;
}
