#include "Common/LocalSqlInstances.h"
#include "Common/U8BackupRestorer.h"
#include "Common/repairpathsettings.h"
#include "U8RestoreWidget.h"
#include <QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QUuid>

static T3BackupSet backup(int position, const QString& name)
{ T3BackupSet result; result.position = position; result.databaseName = name; return result; }
static bool execute(QSqlDatabase& db, const QString& sql)
{
    QSqlQuery query(db); if (!query.exec(sql)) { qWarning() << query.lastError().text(); return false; }
    do { while (query.next()) {} } while (query.nextResult());
    if (query.lastError().isValid()) { qWarning() << query.lastError().text(); return false; } return true;
}
static QString writeFile(const QString& directory, const QString& name, const QByteArray& bytes)
{
    const QString path = QDir(directory).filePath(name); QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
    return path;
}
class U8RestoreTests : public QObject
{
    Q_OBJECT
private slots:
    void compressedU8Source()
    {
        QTemporaryDir dir; QString error;
        const auto path = writeFile(dir.path(), "UFDATA.BA_", QByteArray::fromHex("1f8b080000"));
        const auto manifest = writeFile(dir.path(), "UfErpAct.Lst", "[BackRetInfo]\r\nVersionEx=V8.510\r\n");
        QCOMPARE(U8BackupRestorer::resolveSource(dir.path(), &error), path);
        QCOMPARE(U8BackupRestorer::resolveSource(manifest, &error), path);
        QVERIFY(U8BackupRestorer::isSupportedHeader(QByteArray::fromHex("1f8b080000")));
    }
    void formatsAndManifest()
    {
        QTemporaryDir dir; QString error;
        const auto path = writeFile(dir.path(), "UFDATA.BAK", "MSSQLBAKtest");
        const auto manifest = writeFile(dir.path(), "UfErpAct.Lst", "[BackRetInfo]\r\n");
        QCOMPARE(U8BackupRestorer::resolveSource(dir.path(), &error), path);
        QCOMPARE(U8BackupRestorer::resolveSource(manifest, &error), path);
        QVERIFY(U8BackupRestorer::isSupportedHeader("MSSQLBAK"));
        QVERIFY(U8BackupRestorer::isSupportedHeader("TAPEabcd"));
        QVERIFY(!U8BackupRestorer::isSupportedHeader("MSSQLBA"));
        QVERIFY(U8BackupRestorer::isSupportedHeader(QByteArray::fromHex("1f8b080000")));
        QVERIFY(!U8BackupRestorer::isSupportedHeader(QByteArray::fromHex("1f8b00")));
        QFile::remove(path);
        QVERIFY(U8BackupRestorer::resolveSource(manifest, &error).isEmpty()); QVERIFY(!error.isEmpty());
    }
    void selectionProtection()
    {
        QVector<T3RestoreSelection> items{{backup(1, "UFDATA_404_2022"), "UFDATA_984_2022"},
            {backup(2, "UFMeta_404"), "UFMeta_984"}};
        QVERIFY(U8BackupRestorer::validateSelections(items).isEmpty());
        auto bad = items; bad[1].targetDatabase = "UFMeta_983";
        QVERIFY(!U8BackupRestorer::validateSelections(bad).isEmpty());
        bad = items; bad[0].targetDatabase = "UFDATA_984_2026";
        QVERIFY(!U8BackupRestorer::validateSelections(bad).isEmpty());
        bad = items; bad[0].targetDatabase = "master";
        QVERIFY(!U8BackupRestorer::validateSelections(bad).isEmpty());
        QVERIFY(!U8BackupRestorer::validateSelections({items[1]}).isEmpty());
        bad = items; bad[1].backup.position = 1;
        QVERIFY(!U8BackupRestorer::validateSelections(bad).isEmpty());
        bad = items; bad[1].backup.databaseName = "UFMeta_403";
        QVERIFY(!U8BackupRestorer::validateSelections(bad).isEmpty());
        bad = items; bad[1].targetDatabase = "UFMeta_984_2022";
        QVERIFY(!U8BackupRestorer::validateSelections(bad).isEmpty());
    }
    void widgetDropAndInvalidation()
    {
        QFETCH(QString, fileName);
        QFETCH(QByteArray, header);
        QTemporaryDir root; const QString input = root.path() + "/input";
        RepairPathSettings::setInputDir(input);
        const QString source = writeFile(root.path(), fileName, header);
        const QString manifest = writeFile(root.path(), "UfErpAct.Lst", "test");
        U8RestoreWidget widget;
        auto* target = widget.findChild<QComboBox*>("u8TargetServer");
        auto* edit = widget.findChild<QLineEdit*>("u8Source");
        auto* restore = widget.findChild<QPushButton*>("u8Restore");
        QVERIFY(target && target->count() == LocalSqlInstances::discover().size()+1 && !target->isEditable());
        QVERIFY(edit && restore && !restore->isEnabled());
        QVERIFY(!widget.findChild<QCheckBox*>("u8RemoteAgent")->isChecked());
        QMimeData mime; mime.setUrls({QUrl::fromLocalFile(manifest)});
        QDragEnterEvent enter(QPoint(2, 2), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &enter); QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(2, 2), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &drop); QVERIFY(drop.isAccepted());
        QVERIFY(QFileInfo(source).exists()); QVERIFY(QFileInfo(edit->text()).exists());
        QCOMPARE(QFileInfo(edit->text()).absolutePath(), input);
        QVERIFY(edit->text() != source);
        target->setCurrentIndex(1);
        QVERIFY(!restore->isEnabled());
        QCOMPARE(widget.findChild<QTableWidget*>("u8BackupSets")->rowCount(), 0);
    }
    void widgetDropAndInvalidation_data()
    {
        QTest::addColumn<QString>("fileName"); QTest::addColumn<QByteArray>("header");
        QTest::newRow("native") << QString("UFDATA.BAK") << QByteArray("MSSQLBAKtest");
        QTest::newRow("gzip") << QString("UFDATA.BA_") << QByteArray::fromHex("1f8b080000");
    }
    void legacyBackupRestore()
    {
        const QString source = qEnvironmentVariable("U8_LEGACY_TEST_SOURCE");
        if (source.isEmpty()) QSKIP("U8_LEGACY_TEST_SOURCE not set.");
        T3SqlConnection options; options.server = qEnvironmentVariable("U8_TEST_SERVER", "lpc:.");
        const auto drivers = U8BackupRestorer::availableDrivers(); QVERIFY(!drivers.isEmpty()); options.driver = drivers.first();
        QTemporaryDir staging;
        const auto analysis = U8BackupRestorer::analyze(options, source, staging.path(), {},
            [](const QString& message) { qInfo().noquote() << message; });
        QVERIFY2(analysis.success, qPrintable(analysis.error));
        QCOMPARE(analysis.sets.size(), 20);
        QSet<QString> names;
        for (const auto& set : analysis.sets) { names.insert(set.databaseName); qInfo() << set.databaseName << set.databaseVersion; }
        for (int year = 2007; year <= 2026; ++year) QVERIFY(names.contains(QString("UFDATA_001_%1").arg(year)));
        if (qEnvironmentVariableIsSet("U8_TEST_ANALYZE_ONLY")) return;
        const QString connectionName = "U8LegacyTest_" + QUuid::createUuid().toString(QUuid::Id128);
        QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", connectionName);
        db.setDatabaseName(T3BackupRestorer::connectionString(options)); QVERIFY2(db.open(), qPrintable(db.lastError().text()));
        const QString control = "U8TestControl_" + QUuid::createUuid().toString(QUuid::Id128);
        QStringList created;
        struct Cleanup { std::function<void()> action; ~Cleanup() { action(); } } guard{[&]() {
            for (const auto& name : created) {
                if (!execute(db, QStringLiteral("USE master; IF DB_ID(%1) IS NOT NULL BEGIN "
                    "IF EXISTS(SELECT 1 FROM sys.master_files WHERE database_id=DB_ID(%1) AND physical_name NOT LIKE N'%T3[_]%') "
                    "RAISERROR(N'Test database ownership mismatch.',16,1); ELSE DROP DATABASE %2; END;")
                    .arg(T3BackupRestorer::quoteLiteral(name), T3BackupRestorer::quoteIdentifier(name)))) qFatal("Legacy test cleanup failed");
            }
            db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(connectionName);
        }};
        QString id;
        for (int n = 970; n <= 989; ++n) {
            QSqlQuery q(db);
            QVERIFY(q.exec(QString("SELECT COUNT(*) FROM sys.databases WHERE name LIKE N'UFDATA[_]%1[_]%%'").arg(n)));
            QVERIFY(q.next()); if (q.value(0).toInt() == 0) { id = QString::number(n); break; }
        }
        QVERIFY(!id.isEmpty());
        QVector<T3RestoreSelection> selected;
        for (const auto& set : analysis.sets) { QString name = set.databaseName; name.replace("_001_", "_" + id + "_"); selected.append({set, name}); }
        const auto result = U8BackupRestorer::restore(options, analysis, selected,
            analysis.server.dataDirectory, analysis.server.logDirectory, {},
            [](const QString& message) { qInfo().noquote() << message; }, control);
        for (const auto& item : result.items) created.append(item.database);
        created.append(control);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error)); QCOMPARE(result.items.size(), 20);
        for (const auto& item : result.items) QVERIFY(item.success && item.databaseRestored);
        QSqlQuery q(db);
        QVERIFY(q.exec("SELECT COUNT(*),MIN(iBeginYear),MAX(iBeginYear) FROM [" + control + "].dbo.UA_AccountDatabase"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 20); QCOMPARE(q.value(1).toInt(), 2007); QCOMPARE(q.value(2).toInt(), 2026);
        QVERIFY(q.exec("SELECT iYear,iMonth FROM [" + control + "].dbo.UA_Account"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 2007); QVERIFY(q.value(1).toInt() >= 1 && q.value(1).toInt() <= 12);
    }
    void realBackupRestore()
    {
        const QString source = qEnvironmentVariable("U8_TEST_SOURCE");
        if (source.isEmpty()) QSKIP("U8_TEST_SOURCE not set; real restore is opt-in.");
        const QString connectionName = "U8Test_" + QUuid::createUuid().toString(QUuid::Id128);
        T3SqlConnection options; options.server = "lpc:.";
        const auto drivers = U8BackupRestorer::availableDrivers(); QVERIFY(!drivers.isEmpty()); options.driver = drivers.first();
        QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", connectionName);
        db.setDatabaseName(T3BackupRestorer::connectionString(options)); QVERIFY2(db.open(), qPrintable(db.lastError().text()));
        const QString control = "U8TestControl_" + QUuid::createUuid().toString(QUuid::Id128);
        QStringList created;
        struct Cleanup {
            std::function<void()> action;
            ~Cleanup() { action(); }
        } guard{[&]() {
            for (const QString& name : created) {
                const QString sql = QStringLiteral("USE master; IF DB_ID(%1) IS NOT NULL BEGIN "
                    "IF EXISTS(SELECT 1 FROM sys.master_files WHERE database_id=DB_ID(%1) AND physical_name NOT LIKE N'%T3[_]%') "
                    "RAISERROR(N'Test database ownership mismatch.',16,1); ELSE DROP DATABASE %2; END;")
                    .arg(T3BackupRestorer::quoteLiteral(name), T3BackupRestorer::quoteIdentifier(name));
                if (!execute(db, sql)) qFatal("U8 integration cleanup failed; inspect logged database names.");
            }
            db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(connectionName);
        }};
        T3ServerInfo server; QString error; QVERIFY2(U8BackupRestorer::testConnection(options, &server, &error), qPrintable(error));
        const auto analysis = U8BackupRestorer::analyze(options, source, "", {}, [](const QString& message) { qInfo().noquote() << message; });
        QVERIFY2(analysis.success, qPrintable(analysis.error)); QCOMPARE(analysis.sets.size(), 2);
        QCOMPARE(analysis.sets.at(0).databaseName, QString("UFDATA_404_2022"));
        QCOMPARE(analysis.sets.at(1).databaseName, QString("UFMeta_404"));
        QString id;
        for (int n = 970; n <= 989; ++n) {
            QSqlQuery query(db);
            QVERIFY(query.exec(QString("SELECT COUNT(*) FROM sys.databases WHERE name IN(N'UFDATA_%1_2022',N'UFMeta_%1')").arg(n)));
            QVERIFY(query.next()); if (query.value(0).toInt() == 0) { id = QString::number(n); break; }
        }
        QVERIFY(!id.isEmpty());
        QVector<T3RestoreSelection> selected;
        for (const auto& set : analysis.sets) { QString name = set.databaseName; name.replace("404", id); selected.append({set, name}); }
        auto cancelled = U8BackupRestorer::restore(options, analysis, selected, server.dataDirectory, server.logDirectory, []() { return true; }, {}, control);
        QVERIFY(cancelled.stopped); QVERIFY(cancelled.items.isEmpty());
        auto changed = selected; changed[0].backup.backupSetId = "wrong-guid";
        const auto stale = U8BackupRestorer::restore(options, analysis, changed, server.dataDirectory, server.logDirectory, {}, {}, control);
        QVERIFY(!stale.error.isEmpty()); QVERIFY(stale.items.isEmpty());
        // An incompatible T3 control table must fail without leaving a new mapping table.
        QVERIFY(execute(db, T3BackupRestorer::controlDatabaseSql(server.dataDirectory, server.logDirectory,
            QUuid::createUuid().toString(QUuid::Id128), control)));
        created.append(control);
        QVERIFY(execute(db, "USE [" + control + "]; CREATE TABLE dbo.UA_Account(cAcc_Id varchar(3),cAcc_Name nvarchar(120),iYear int,cTradeKind nvarchar(120));"));
        QVERIFY(!execute(db, U8BackupRestorer::controlSchemaSql(control)));
        { QSqlQuery q(db); QVERIFY(q.exec("SELECT OBJECT_ID(N'" + control + ".dbo.UA_AccountDatabase')")); QVERIFY(q.next()); QVERIFY(q.value(0).isNull()); }
        QVERIFY(execute(db, "USE [" + control + "]; DROP TABLE dbo.UA_Account; USE master;"));
        bool stop = false;
        const auto partial = U8BackupRestorer::restore(options, analysis, selected, server.dataDirectory, server.logDirectory,
            [&]() { return stop; }, [&](const QString& message) {
                qInfo().noquote() << message;
                if (message.startsWith(QString::fromUtf8(u8"数据库已恢复并 ONLINE："))) stop = true;
            }, control);
        for (const auto& item : partial.items) if (item.databaseRestored) created.append(item.database);
        { QSqlQuery q(db); QVERIFY(q.exec("SELECT DB_ID(" + T3BackupRestorer::quoteLiteral(control) + ")")); QVERIFY(q.next());
          if (!q.value(0).isNull() && !created.contains(control)) created.append(control); }
        QVERIFY2(partial.error.isEmpty(), qPrintable(partial.error)); QVERIFY(partial.stopped);
        QCOMPARE(partial.items.size(), 1); QVERIFY(partial.items.first().databaseRestored); QVERIFY(!partial.items.first().success);
        { QSqlQuery q(db); QVERIFY(q.exec("SELECT COUNT(*) FROM [" + control + "].dbo.UA_Account")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0); }
        const QString partialName = partial.items.first().database;
        QVERIFY(execute(db, "USE master; DROP DATABASE " + T3BackupRestorer::quoteIdentifier(partialName)));
        created.removeAll(partialName);
        const auto result = U8BackupRestorer::restore(options, analysis, selected, server.dataDirectory, server.logDirectory,
            {}, [](const QString& message) { qInfo().noquote() << message; }, control);
        for (const auto& item : result.items) if (item.databaseRestored) created.prepend(item.database);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error)); QCOMPARE(result.items.size(), 2);
        for (const auto& item : result.items) QVERIFY(item.success && item.databaseRestored);
        const auto duplicate = U8BackupRestorer::restore(options, analysis, selected, server.dataDirectory, server.logDirectory, {}, {}, control);
        QVERIFY(!duplicate.error.isEmpty()); QVERIFY(duplicate.items.isEmpty());
        {
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT cAcc_Id,cUnitName,iYear,iMonth,cTradeKind FROM [" + control + "].dbo.UA_Account"));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), id);
            QVERIFY(!q.value(1).toString().trimmed().isEmpty());
            QSqlQuery sourceUnit(db);
            QVERIFY(sourceUnit.exec("SELECT TOP (1) cUnitName FROM [UFDATA_" + id + "_2022].dbo.UA_Account_Ex"));
            QVERIFY(sourceUnit.next());
            QCOMPARE(q.value(1).toString(), sourceUnit.value(0).toString());
            const QString expectedName = qEnvironmentVariable("U8_TEST_EXPECTED_NAME");
            if (!expectedName.isEmpty()) QCOMPARE(q.value(1).toString(), expectedName);
            QCOMPARE(q.value(2).toInt(), 2022); QCOMPARE(q.value(3).toInt(), 1);
            QCOMPARE(q.value(4).toString(), QString::fromUtf8(u8"2007 年新会计准则科目")); QVERIFY(!q.next());
            QVERIFY(q.exec("SELECT cDatabase,iBeginYear FROM [" + control + "].dbo.UA_AccountDatabase WHERE cAcc_Id=N'" + id + "' ORDER BY iBeginYear"));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), "UFDATA_" + id + "_2022"); QCOMPARE(q.value(1).toInt(), 2022); QVERIFY(!q.next());
            QVERIFY(q.exec("SELECT DISTINCT iYear FROM [UFDATA_" + id + "_2022].dbo.GL_mend ORDER BY iYear"));
            QList<int> years; while (q.next()) years.append(q.value(0).toInt());
            QCOMPARE(years, QList<int>({2022, 2023, 2024, 2025, 2026}));
            QVERIFY(q.exec("SELECT cAcc_Id FROM [UFDATA_" + id + "_2022].dbo.UA_Account_Ex")); QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString("404"));
        }
        for (const auto& item : result.items)
            QVERIFY(execute(db, "DBCC CHECKDB (" + T3BackupRestorer::quoteIdentifier(item.database) + ") WITH NO_INFOMSGS;"));
        U8AccountMetadata metadata{QString::fromUtf8(u8"示例单位"), QString::fromUtf8(u8"示例单位"), QString::fromUtf8(u8"2007 年新会计准则科目"), 2022, 1};
        QVERIFY(!execute(db, U8BackupRestorer::registrationSql(selected, metadata, true, control)));
    }
};
int main(int argc, char** argv)
{
    QApplication app(argc, argv); QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settings.path());
    U8RestoreTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "U8RestoreTests.moc"
