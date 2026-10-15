#include "Common/SqlTargetSelector.h"
#include "Common/RestorePreferences.h"
#include "Common/DatabaseCleaner.h"
#include "T3RestoreWidget.h"
#include "TPlusRestoreWidget.h"
#include "BackupRestoreWidget.h"
#include <QPointer>
#include "U8RestoreWidget.h"
#include "KisProRestoreWidget.h"
#include "InspurRestoreWidget.h"
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QDebug>
#include <QSet>
#include <QGroupBox>
#include <QVBoxLayout>

static void checkInstance(bool value, const char* message)
{
    if (!value) qFatal("SqlInstances: %s", message);
}

void runSqlInstanceTests()
{
    const QString named = QStringLiteral(".\\SQL2008R2");
    const auto locals = LocalSqlInstances::endpoints({"SQL2008R2", "MSSQLSERVER", "sql2008r2", "mssqlserver", "bad;server"});
    checkInstance(locals == QStringList({".", named}), "deduplicate both registry views and preserve default/named endpoints");
    checkInstance(LocalSqlInstances::endpoints({}).isEmpty(), "no hardcoded default when registration is absent");
    checkInstance(LocalSqlInstances::endpoints({"SQL2008R2"}) == QStringList({named}), "named-only installation has no phantom default");
    checkInstance(!LocalSqlInstances::isLocalEndpoint("") && !LocalSqlInstances::isLocalEndpoint("192.168.1.211"), "missing/local endpoints cannot select remote server");
    checkInstance(!DatabaseCleaner::inspect(DatabaseCleaner::Target::Local, "").error.isEmpty(), "missing local target fails instead of connecting to default");

    checkInstance(LocalSqlInstances::endpoints(LocalSqlInstances::namesFromServices(
        {"MSSQLSERVER", "MSSQL$CUSTOM", "mssql$custom", "SQLBrowser", "SQLAgent$CUSTOM", "MSSQL$"}))
        == QStringList({".", ".\\CUSTOM"}), "discover only database engine services and deduplicate");
    QComboBox empty;
    SqlTargetSelector::populate(&empty, {});
    checkInstance(empty.count() == 2 && !SqlTargetSelector::hasTarget(&empty)
        && empty.findData(".") == -1, "empty scan has placeholder and 211 without selecting either server");
    SqlTargetSelector::populate(&empty, {named, ".\\CUSTOM", "arbitrary-remote", named.toLower()});
    checkInstance(empty.property("localInstanceCount").toInt() == 2 && !SqlTargetSelector::hasTarget(&empty),
        "scan accepts only unique local endpoints and does not silently switch target");
    empty.setCurrentIndex(empty.findData(named));
    SqlTargetSelector::populate(&empty, {".\\CUSTOM", named});
    checkInstance(empty.currentData().toString() == named, "rescan preserves endpoint across ordering changes");
    SqlTargetSelector::populate(&empty, {".\\CUSTOM"});
    checkInstance(!SqlTargetSelector::hasTarget(&empty), "disappearing endpoint never selects another database");
    empty.setCurrentIndex(empty.findData(SqlTargetSelector::remoteServer()));
    SqlTargetSelector::populate(&empty, {});
    checkInstance(SqlTargetSelector::isRemote(&empty), "explicit 211 selection survives empty scan");

    T3RestoreWidget t3;
    TPlusRestoreWidget tplus;
    U8RestoreWidget u8;
    KisProRestoreWidget kis;
    InspurRestoreWidget inspur;
    for(QWidget* page:QList<QWidget*>{&t3,&tplus,&u8,&kis,&inspur}) {
        auto* group=page->findChild<QGroupBox*>();
        auto* layout=qobject_cast<QVBoxLayout*>(page->layout());
        QPushButton* read=nullptr;
        for(auto* button:page->findChildren<QPushButton*>())if(button->objectName().endsWith("Analyze"))read=button;
        checkInstance(group && read && layout && layout->indexOf(read)<layout->indexOf(group),"instance confirmation follows package reading on all five pages");
    }
    // Access is intentionally limited to this offline test entry point.
    auto verify = [&](QComboBox* target, QLineEdit* data, QLineEdit* log, QLineEdit* staging,
                      QCheckBox* agent, QTableWidget* table, QPushButton* restore,
                      T3BackupAnalysis& analysis, const std::function<T3SqlConnection()>& options,
                      const std::function<void(bool)>& busy, const QString&) {
        checkInstance(target->count() == qMax(1, LocalSqlInstances::discover().size()) + 1 && !target->isEditable(), "all discovered instances present");
        int index = target->findData(named);
        if (index < 0) { target->addItem("test local instance", named); index = target->count() - 1; }
        const int remote = target->findData(SqlTargetSelector::remoteServer());
        target->setCurrentIndex(remote);
        checkInstance(!options().windowsAuthentication && options().server == SqlTargetSelector::remoteServer()
            && options().user == "sa" && agent->isEnabled() && !staging->text().isEmpty(), "211 retains auth, staging and transfer behavior at dynamic index");
        analysis.success = true; table->setRowCount(1); restore->setEnabled(true);
        data->setText("old-instance-data"); log->setText("old-instance-log");
        target->setCurrentIndex(index);
        const auto connection = options();
        checkInstance(connection.server == named && connection.windowsAuthentication && connection.user.isEmpty()
            && connection.password.isEmpty(), "named instance uses exact endpoint and Windows authentication");
        const auto connectionString = T3BackupRestorer::connectionString(connection);
        checkInstance(connectionString.contains("SERVER={.\\SQL2008R2}") && connectionString.contains("Trusted_Connection=Yes"), "ODBC receives named instance");
        checkInstance(!analysis.success && table->rowCount() == 1 && !restore->isEnabled()
            && data->text().isEmpty() && log->text().isEmpty() && staging->text() == RestorePreferences::stagingDirectory() && !agent->isEnabled(), "switch retains rows and shared staging while clearing target readiness and directories");
        auto* scan = target->parentWidget()->findChild<QPushButton*>(target->objectName() + "Scan");
        checkInstance(scan, "scan button available beside target");
        busy(true); checkInstance(!target->isEnabled() && !scan->isEnabled(), "target and scan locked while work runs");
        busy(false); checkInstance(target->isEnabled() && scan->isEnabled(), "target and scan unlocked when idle");
        analysis.success = true; table->setRowCount(1); restore->setEnabled(true);
        data->setText("old-instance-data"); log->setText("old-instance-log");
        SqlTargetSelector::populate(target, {named});
        checkInstance(target->currentData().toString() == named && !analysis.success && table->rowCount() == 1
            && data->text().isEmpty() && log->text().isEmpty() && !restore->isEnabled(), "same target rescan preserves rows and clears stale target readiness");
        SqlTargetSelector::populate(target, {});
        checkInstance(!SqlTargetSelector::hasTarget(target), "missing target is not silently redirected");
        scan->click();
        checkInstance(target->property("localInstanceCount").toInt() == LocalSqlInstances::discover().size(), "scan button reloads actual local inventory");
    };
    verify(t3.m_target, t3.m_dataDirectory, t3.m_logDirectory, t3.m_staging, t3.m_remoteAgent,
        t3.m_table, t3.m_restoreButton, t3.m_analysis, [&]() { return t3.connectionOptions(); }, [&](bool busy) { t3.setBusy(busy); }, QString());
    verify(tplus.m_target, tplus.m_dataDirectory, tplus.m_logDirectory, tplus.m_staging, tplus.m_remoteAgent,
        tplus.m_table, tplus.m_restoreButton, tplus.m_analysis, [&]() { return tplus.connectionOptions(); }, [&](bool busy) { tplus.setBusy(busy); }, QStringLiteral("D:\\Backup"));
    verify(u8.m_target, u8.m_dataDirectory, u8.m_logDirectory, u8.m_staging, u8.m_remoteAgent,
        u8.m_table, u8.m_restoreButton, u8.m_analysis, [&]() { return u8.connectionOptions(); }, [&](bool busy) { u8.setBusy(busy); }, QStringLiteral("D:\\Backup"));
    verify(kis.m_target, kis.m_dataDirectory, kis.m_logDirectory, kis.m_staging, kis.m_remoteAgent,
        kis.m_table, kis.m_restoreButton, kis.m_analysis, [&]() { return kis.connectionOptions(); }, [&](bool busy) { kis.setBusy(busy); }, QStringLiteral("D:\\Backup"));
    verify(inspur.m_target, inspur.m_dataDirectory, inspur.m_logDirectory, inspur.m_staging, inspur.m_remoteAgent,
        inspur.m_table, inspur.m_restoreButton, inspur.m_analysis, [&]() { return inspur.connectionOptions(); }, [&](bool busy) { inspur.setBusy(busy); }, QStringLiteral("D:\\Backup"));
    qInfo() << "PASS: local instance discovery, restore target routing and stale plan invalidation";

    BackupRestoreWidget unified;
    auto* software = unified.findChild<QComboBox*>("restoreSoftware");
    checkInstance(software && software->count() == 7 && !software->isEditable(), "one entry with seven fixed software choices");
    const QStringList pageNames = {"t3RestorePage", "u8RestorePage", "tplusRestorePage", "kisProRestorePage", "inspurRestorePage", "xinZhongDaRestorePage", "sudaBackupPage"};
    for (int i = 0; i < software->count(); ++i) {
        software->setCurrentIndex(i);
        auto* page = unified.findChild<QWidget*>(pageNames[i]);
        checkInstance(page, qPrintable("software page: " + pageNames[i]));
        QPointer<QWidget> previous(page);
        page->setProperty("sessionBusyTest", true);
        checkInstance(!software->isEnabled(), "software switch disabled immediately during work");
        software->setCurrentIndex((i + 1) % software->count());
        checkInstance(software->currentIndex() == i && previous, "programmatic switch cannot destroy active task");
        page->setProperty("sessionBusyTest", false);
        checkInstance(software->isEnabled(), "software switch reenabled after work");
        software->setCurrentIndex((i + 1) % software->count());
        checkInstance(previous.isNull(), "switch discards old backup analysis and form");
    }
    unified.selectSoftware(20);
    SqlTargetSelector::populate(unified.findChild<QComboBox*>("t3TargetServer"), {});
    checkInstance(!unified.findChild<QPushButton*>("t3TestConnection")->isEnabled()
        && unified.findChild<QPushButton*>("t3Analyze")->isEnabled(), "no target disables manual connection but allows automatic matching");
    qInfo() << "PASS: unified software selection, busy protection, scan refresh and no-target guards";

    if (qEnvironmentVariable("SQL_INSTANCE_CONNECTION_CHECK") != "1") return;
    QSet<QString> identities;
    bool allConnected = true;
    for (const auto& endpoint : LocalSqlInstances::discover()) {
        T3SqlConnection options; options.server = endpoint;
        const auto drivers = T3BackupRestorer::availableDrivers();
        checkInstance(!drivers.isEmpty(), "installed ODBC driver"); options.driver = drivers.first();
        T3ServerInfo server; QString error;
        const bool connected = T3BackupRestorer::testConnection(options, &server, &error);
        if (!connected) {
            qWarning().noquote() << "FAIL: read-only connection" << endpoint << error;
            allConnected = false;
            continue;
        }
        checkInstance(!server.dataDirectory.isEmpty() && !server.logDirectory.isEmpty(), "instance directories readable");
        const auto catalog = DatabaseCleaner::inspect(DatabaseCleaner::Target::Local, endpoint);
        checkInstance(catalog.error.isEmpty(), qPrintable(catalog.error));
        checkInstance(catalog.localInstance == endpoint && !catalog.server.isEmpty(), "catalog retains selected endpoint");
        checkInstance(!identities.contains(catalog.server.toLower()), "different endpoints reach different installed instances");
        identities.insert(catalog.server.toLower());
        qInfo().noquote() << "PASS: read-only connection" << endpoint << server.name << server.version;
    }
    checkInstance(allConnected, "one or more installed instances could not be reached; see connection errors above");
}
