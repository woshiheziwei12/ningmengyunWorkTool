#include "Common/DatabaseCleaner.h"
#include "DatabaseCleanupWidget.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QDebug>
#include <QFile>
#include <cstdio>
#include <algorithm>

using namespace DatabaseCleaner;
namespace {
void check(bool value, const char* message) { if (!value) qFatal("DatabaseCleaner: %s", message); }
Database database(int id, const QString& name, int source = 0)
{
    Database item;
    item.id = id; item.name = name; item.created = "2026-09-17T09:30:00.123";
    item.state = "ONLINE"; item.sourceId = source;
    return item;
}
class FakeSession : public Session {
public:
    Catalog catalog;
    QStringList dropped;
    QString failName;
    int reads = 0;
    bool failFinalRead = false;
    bool createNewAfterDrop = false;
    Catalog read() override
    {
        ++reads;
        auto result = catalog;
        if (failFinalRead && reads > 1) result.error = "Connection lost";
        return result;
    }
    QString drop(const Database& item) override
    {
        dropped.append(item.name);
        if (item.name == failName) return "Database is in use";
        for (int i = 0; i < catalog.databases.size(); ++i)
            if (catalog.databases.at(i).id == item.id) { catalog.databases.removeAt(i); break; }
        if (createNewAfterDrop) {
            createNewAfterDrop = false;
            catalog.databases.append(database(90, "new_after_confirmation"));
        }
        return {};
    }
};
}
void runDatabaseCleanerTests()
{
    for (const auto& name : {"master", "tempdb", "model", "msdb", "MSSQLSYSTEMRESOURCE", "Distribution"}) {
        const auto item = database(10, name);
        check(!isUserDatabase(item) && dropSql(item).isEmpty(), "system names must never produce deletion SQL");
    }
    for (int id : {0, 1, 2, 3, 4, 32767})
        check(!isUserDatabase(database(id, "renamed")), "system IDs are protected regardless of name");
    auto distributor = database(8, "custom_replication_database");
    distributor.distributor = true;
    check(!isUserDatabase(distributor), "custom-named distribution database is protected");
    auto unusual = database(12, QString::fromUtf8(u8"账套']; DROP DATABASE [master];--%1"));
    const auto sql = dropSql(unusual);
    const QString sqlOutput = qEnvironmentVariable("CLEANUP_TEST_SQL_OUTPUT");
    if (!sqlOutput.isEmpty()) {
        QFile file(sqlOutput);
        check(file.open(QIODevice::WriteOnly), "open offline SQL parser fixture");
        file.write(sql.toUtf8());
    }
    check(sql.contains("账套'']; DROP DATABASE [master];--%1"), "SQL literals escape apostrophes and preserve percent placeholders");
    check(sql.contains("]]; DROP DATABASE [master]];--%1]"), "SQL identifier brackets are escaped");
    check(sql.contains("create_date=CONVERT(datetime,") && sql.contains("database_id=12")
        && sql.contains("is_distributor = 0"), "each batch rechecks identity and protected status");
    check(sql.contains("source_database_id IS NULL") && sql.contains("state=0"), "snapshot and offline deletion skip SINGLE_USER");
    check(sql.contains("END TRY BEGIN CATCH") && sql.contains("@access"), "failed drops restore original access mode");
    unusual.created = "bad date'; DROP DATABASE master;--";
    check(dropSql(unusual).isEmpty(), "invalid identity date rejected");

    Catalog confirmed;
    confirmed.server = "local-instance";
    confirmed.databases = {database(10, "source"), database(11, "snapshot", 10), database(12, "another")};
    FakeSession normal; normal.catalog = confirmed;
    auto result = removeSelected(normal, confirmed);
    check(normal.dropped == QStringList({"snapshot", "source", "another"}), "snapshots removed before their source");
    check(result.items.size() == 3 && result.remaining.isEmpty() && result.error.isEmpty(), "all drops verified by fresh catalog");
    FakeSession changed; changed.catalog = confirmed;
    changed.catalog.databases[0].created = "2026-09-17T09:31:00.123";
    result = removeSelected(changed, confirmed);
    check(changed.dropped.isEmpty() && !result.error.isEmpty(), "recreated same-name database blocks stale confirmation");
    FakeSession newDatabase; newDatabase.catalog = confirmed;
    newDatabase.catalog.databases.append(database(13, "new"));
    result = removeSelected(newDatabase, confirmed);
    check(newDatabase.dropped.size() == 3 && !newDatabase.dropped.contains("new")
        && result.remaining == QStringList({"new"}), "unselected new database remains untouched");
    Catalog selection = confirmed;
    selection.databases = {confirmed.databases.at(2)};
    FakeSession subset; subset.catalog = confirmed;
    result = removeSelected(subset, selection);
    check(subset.dropped == QStringList({"another"}) && result.remaining.size() == 2,
        "only explicitly selected database is deleted");
    Catalog emptySelection = confirmed; emptySelection.databases.clear();
    FakeSession empty; empty.catalog = confirmed;
    result = removeSelected(empty, emptySelection);
    check(empty.dropped.isEmpty() && !result.error.isEmpty(), "empty selection cannot fall back to deleting all");
    selection.databases = {confirmed.databases.at(0)};
    FakeSession sourceOnly; sourceOnly.catalog = confirmed;
    result = removeSelected(sourceOnly, selection);
    check(sourceOnly.dropped.isEmpty() && result.items.size() == 1 && !result.items.first().success,
        "unselected snapshot is never deleted implicitly with its source");
    selection.databases = {confirmed.databases.at(1)};
    FakeSession snapshotOnly; snapshotOnly.catalog = confirmed;
    result = removeSelected(snapshotOnly, selection);
    check(snapshotOnly.dropped == QStringList({"snapshot"}) && result.remaining.contains("source"),
        "selecting a snapshot preserves its source");
    selection.databases = {confirmed.databases.at(2)};
    FakeSession removed; removed.catalog = confirmed; removed.catalog.databases.removeLast();
    result = removeSelected(removed, selection);
    check(removed.dropped.isEmpty() && !result.error.isEmpty(), "missing selected database invalidates confirmation");
    FakeSession wrongServer; wrongServer.catalog = confirmed; wrongServer.catalog.server = "other";
    result = removeSelected(wrongServer, confirmed);
    check(wrongServer.dropped.isEmpty() && !result.error.isEmpty(), "server mismatch blocks all drops");
    FakeSession wrongLocal; wrongLocal.catalog = confirmed; wrongLocal.catalog.localInstance = ".\\SQL2008R2";
    result = removeSelected(wrongLocal, confirmed);
    check(wrongLocal.dropped.isEmpty() && !result.error.isEmpty(), "local endpoint mismatch blocks deletion even with matching server metadata");
    FakeSession named; named.catalog = confirmed; named.catalog.localInstance = ".\\SQL2008R2";
    result = removeSelected(named, named.catalog);
    check(result.error.isEmpty() && named.dropped.size() == 3, "named instance selection is retained through deletion");
    FakeSession wrongTarget; wrongTarget.catalog = confirmed; wrongTarget.catalog.target = Target::Server211;
    result = removeSelected(wrongTarget, confirmed);
    check(wrongTarget.dropped.isEmpty(), "target mismatch blocks all drops");
    FakeSession protectedPlan; protectedPlan.catalog = confirmed;
    protectedPlan.catalog.databases.append(distributor);
    result = removeSelected(protectedPlan, protectedPlan.catalog);
    check(protectedPlan.dropped.isEmpty(), "injected system entry rejects entire plan");
    FakeSession partial; partial.catalog = confirmed; partial.failName = "source";
    result = removeSelected(partial, confirmed);
    check(result.items.size() == 3 && !result.items[1].success && result.items[2].success
        && result.remaining == QStringList({"source"}), "one failure preserves result and continues later databases");
    FakeSession stopped; stopped.catalog = confirmed;
    bool cancel = false;
    result = removeSelected(stopped, confirmed, [&]() { return cancel; }, [&](const ItemResult&) { cancel = true; });
    check(result.stopped && stopped.dropped.size() == 1 && result.remaining.size() == 2, "stop waits for current drop then prevents later drops");
    FakeSession unreadable; unreadable.catalog = confirmed; unreadable.failFinalRead = true;
    result = removeSelected(unreadable, confirmed);
    check(!result.error.isEmpty(), "final verification failure never reports complete success");
    FakeSession lateAddition; lateAddition.catalog = confirmed; lateAddition.createNewAfterDrop = true;
    result = removeSelected(lateAddition, confirmed);
    check(!lateAddition.dropped.contains("new_after_confirmation") && result.remaining.size() == 1,
        "database created while deleting remains unapproved and is reported");
    std::puts("PASS: database cleanup offline workflow, system protection, stale plans, snapshots, failures, cancellation and SQL escaping");
}

void runDatabaseCleanupWidgetTests()
{
    DatabaseCleanupWidget widget;
    Catalog catalog;
    catalog.server = "offline-test-instance";
    catalog.databases = {database(10, "keep_this_database"), database(11, "delete_this_database"), database(12, "another_database")};
    check(!widget.m_selectAll->isEnabled(), "select all initially disabled");
    widget.displayCatalog(catalog);
    check(widget.selectedCatalog().databases.isEmpty() && !widget.m_delete->isEnabled(), "fresh list defaults to no selection");
    widget.m_table->item(1, 0)->setCheckState(Qt::Checked);
    check(widget.m_delete->isEnabled() && widget.selectedCatalog().databases.size() == 1
        && widget.selectedCatalog().databases.first().id == 11, "row checkbox builds selected-only plan");
    check(widget.m_selection->text().contains("1 / 3"), "selected count tracks row changes");
    widget.m_selectAll->click();
    check(widget.selectedCatalog().databases.size() == 3
        && widget.m_selectAll->text() == QString::fromUtf8(u8"取消全选"), "select all completes partial selection");
    widget.m_table->item(0, 0)->setCheckState(Qt::Unchecked);
    check(widget.selectedCatalog().databases.size() == 2
        && widget.m_selectAll->text() == QString::fromUtf8(u8"全选"), "individual deselection restores select-all action");
    widget.m_selectAll->click();
    widget.m_selectAll->click();
    check(widget.selectedCatalog().databases.isEmpty() && !widget.m_delete->isEnabled(), "cancel all clears every checkbox");
    widget.m_table->item(1, 0)->setCheckState(Qt::Checked);
    widget.setBusy(true);
    check(!widget.m_selectAll->isEnabled() && !widget.m_delete->isEnabled()
        && !(widget.m_table->item(1, 0)->flags() & Qt::ItemIsUserCheckable), "selection cannot change during deletion");
    widget.setBusy(false);
    bool confirmedDialogSeen = false;
    QTimer::singleShot(0, [&]() {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        check(dialog && dialog->detailedText() == "delete_this_database", "confirmation contains only checked database names");
        confirmedDialogSeen = true;
        dialog->reject();
    });
    widget.m_delete->click();
    check(confirmedDialogSeen && !widget.m_worker && widget.m_loaded, "cancel leaves selection intact and starts no deletion");
    const QString screenshotDir = QCoreApplication::arguments().value(1);
    if (!screenshotDir.isEmpty()) {
        QDir().mkpath(screenshotDir);
        QFile style(":/Resource/qss/workspace.css");
        if (style.open(QIODevice::ReadOnly)) widget.setStyleSheet(QString::fromUtf8(style.readAll()));
        widget.setAttribute(Qt::WA_DontShowOnScreen);
        widget.resize(1000, 700);
        widget.show();
        QApplication::processEvents();
        check(widget.grab().save(screenshotDir + "/cleanup-selected.png"), "save selected-databases UI screenshot");
    }
    widget.displayCatalog(catalog);
    check(widget.selectedCatalog().databases.isEmpty(), "refresh clears previous selection");
    widget.m_table->item(0, 0)->setCheckState(Qt::Checked);
    int namedIndex = widget.m_target->findData(QStringLiteral(".\\SQL2008R2"));
    if (namedIndex < 0) { widget.m_target->addItem("test local instance", QStringLiteral(".\\SQL2008R2")); namedIndex = widget.m_target->count() - 1; }
    widget.m_target->setCurrentIndex(namedIndex);
    check(widget.m_table->rowCount() == 0 && !widget.m_selectAll->isEnabled() && !widget.m_delete->isEnabled(),
        "switching target invalidates checked rows");
    catalog.databases.clear();
    widget.displayCatalog(catalog);
    check(!widget.m_selectAll->isEnabled() && !widget.m_delete->isEnabled(), "empty catalog disables selection actions");
    std::puts("PASS: database selection UI, select all, deselect all, selection count, confirmation scope, cancel, busy and refresh");
}
