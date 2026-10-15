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
#include "Common/LocalSqlInstances.h"
#include "Common/repairpathsettings.h"
#include "Common/WecomLocalServer.h"
#include <QLocalSocket>

void runSqlInstanceTests();
void runRestoreCompatibilityTests();
void runDatabaseCleanerTests();
void runDatabaseCleanupWidgetTests();

static void require(bool condition, const char *message)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static void settle()
{
    for (int i = 0; i < 5; ++i) QApplication::processEvents();
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    require(argc == 2, "screenshot directory argument");
    const QString output = QString::fromLocal8Bit(argv[1]);
    QDir().mkpath(output);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, output + "/settings");
    QStandardPaths::setTestModeEnabled(true);
    RepairPathSettings::setInputDir(output + "/sample-input");
    RepairPathSettings::setOutputDir(output + "/sample-output");
    QDir().mkpath(RepairPathSettings::inputDir());
    runRestoreCompatibilityTests();
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
    {
        QLocalSocket pipe;
        pipe.connectToServer(WecomLocalServer::defaultServerName());
        QElapsedTimer timer; timer.start();
        while (pipe.state()!=QLocalSocket::ConnectedState && timer.elapsed()<3000) { QApplication::processEvents(); QThread::msleep(1); }
        require(pipe.state()==QLocalSocket::ConnectedState, "workspace named pipe available before opening document page");
        pipe.write("{\"protocolVersion\":1,\"action\":\"status\"}\n"); pipe.flush();
        QByteArray response;
        while (!response.endsWith('\n') && timer.elapsed()<5000) { QApplication::processEvents(); response+=pipe.readAll(); QThread::msleep(1); }
        require(QJsonDocument::fromJson(response).object().value("ok").toBool(), "workspace serves structured status");
        require(!window.findChild<QWidget*>("wecomDocumentPage"), "status does not instantiate document page or browser");
    }
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
    for (int id : {3, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28}) {
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
            software->setCurrentIndex(software->findData(20));
            settle();
        }
        if (id == 24) {
            auto *software = scroll->widget()->findChild<QComboBox*>("balanceSoftware");
            require(software && software->count() == 71, "70 balance import software entries and placeholder");
            require(!scroll->widget()->findChild<QCheckBox*>("balanceImportZero")->isChecked(), "zero balance disabled by default");
            software->setCurrentIndex(software->findData(2001));
            scroll->widget()->findChild<QLineEdit*>("balanceAccountName")->setText(QStringLiteral("示例账套 · 未提交"));
            require(scroll->widget()->findChild<QLineEdit*>("balanceFile_trialbalance"), "selected software attachment fields");
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
        require(QMetaObject::invokeMethod(&window, "onBackToMain"), "back route");
        require(catalog->currentCategory() == FunctionSelectWidget::categoryForFunction(id), "back returns to owning category");
        require(mainStack->currentWidget() == catalog, "back shows catalog");
        window.resize(1440, 960);
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
        // Exercise the actual window -> tool page -> reader -> IPC callback without a network login.
        auto *url = window.findChild<QLineEdit*>("wecomDocumentUrl");
        require(url, "document page exists after route tests");
        const QString originalUrl = url->text(); url->setText("https://example.test/not-the-supported-document");
        window.hide();
        QLocalSocket pipe; pipe.connectToServer(WecomLocalServer::defaultServerName());
        QElapsedTimer timer; timer.start();
        while (pipe.state()!=QLocalSocket::ConnectedState && timer.elapsed()<3000) { QApplication::processEvents(); QThread::msleep(1); }
        require(pipe.state()==QLocalSocket::ConnectedState, "hidden workspace keeps API available");
        pipe.write("{\"protocolVersion\":1,\"action\":\"read\"}\n"); pipe.flush();
        QByteArray response;
        while (!response.endsWith('\n') && timer.elapsed()<5000) { QApplication::processEvents(); response+=pipe.readAll(); QThread::msleep(1); }
        const auto object=QJsonDocument::fromJson(response).object();
        require(!object.value("ok").toBool() && object.value("error").toObject().value("code").toString()=="read_failed", "IPC reaches real reader URL validation and returns its failure");
        require(!window.hasActiveTasks(), "API completion clears workspace task guard");
        require(!window.isVisible(), "background read does not force workspace to foreground");
        url->setText(originalUrl);
    }
    fprintf(stdout, "PASS: workspace API auto-start, lazy browser, background dispatch and reader callback.\n");
    fprintf(stdout, "PASS: environment palette/menu/icon, task guard, close-to-tray and restore.\n");
    fprintf(stdout, "PASS: 23 tool entries, 5 categories, search, 19 local routes and 5 restore aliases, category return, small windows, cloud tables and dialog.\n");
    return 0;
}
