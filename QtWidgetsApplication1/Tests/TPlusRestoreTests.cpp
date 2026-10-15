#include <QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QUuid>
#include "TPlusRestoreWidget.h"
#include "Common/repairpathsettings.h"

static QByteArray manifest()
{
    return u8"<BackupFileAdditionalInfo><DsName>UFTData971269_000002</DsName>"
        "<AccountName>示例科技有限公司</AccountName><Year>2016</Year>"
        "<Duration>1</Duration><VersionNum>16.000.000.0256</VersionNum></BackupFileAdditionalInfo>";
}
static bool execute(QSqlDatabase& db, const QString& sql)
{
    QSqlQuery query(db);
    if (!query.exec(sql)) { qWarning().noquote() << query.lastError().text(); return false; }
    do { while (query.next()) {} } while (query.nextResult());
    if (query.lastError().isValid()) { qWarning().noquote() << query.lastError().text(); return false; }
    return true;
}
class TPlusRestoreTests : public QObject
{
    Q_OBJECT
private slots:
    void manifestValidation()
    {
        TPlusAccountMetadata account; QString error;
        QVERIFY(TPlusBackupRestorer::readMetadata(manifest(), &account, &error));
        QCOMPARE(account.databaseName, QString("UFTData971269_000002"));
        QCOMPARE(account.accountName, QString::fromUtf8(u8"示例科技有限公司"));
        QCOMPARE(account.year, 2016); QCOMPARE(account.month, 1); QCOMPARE(account.version, QString("16.000.000.0256"));
        for (const auto& bad : {manifest().replace("<Duration>1</Duration>", "<Duration>13</Duration>"),
            manifest().replace("</Year>", "</Year><Year>2026</Year>"),
            manifest().replace("UFTData971269_000002", "master"),
            manifest().replace("16.000.000.0256", ""), manifest().left(50),
            QByteArray("<!DOCTYPE x [<!ENTITY y 'name'>]>") + manifest()})
            QVERIFY(!TPlusBackupRestorer::readMetadata(bad, &account, &error));
    }
    void targetProtection()
    {
        T3BackupSet backup; backup.position = 1; backup.databaseName = "UFTData971269_000002";
        T3RestoreSelection item{backup, "UFTData_Test_123"};
        QVERIFY(TPlusBackupRestorer::validateSelections({item}).isEmpty());
        for (const QString& name : {QString("master"), QString("UFTSystem"), QString("OtherDB"), QString("UFTData_x';DROP"), QString("UFTData")}) {
            auto bad = item; bad.targetDatabase = name; QVERIFY(!TPlusBackupRestorer::validateSelections({bad}).isEmpty());
        }
        QVERIFY(!TPlusBackupRestorer::validateSelections({item, item}).isEmpty());
        TPlusAccountMetadata metadata; QString error;
        QVERIFY(TPlusBackupRestorer::readMetadata(manifest(), &metadata, &error));
        metadata.accountName = "O'Brien";
        const auto sql = TPlusBackupRestorer::registrationSql(item, metadata, true);
        QVERIFY(sql.contains("O''Brien")); QVERIFY(sql.contains("ROLLBACK TRANSACTION"));
        QVERIFY(sql.contains("UFTData_Test_123"));
    }
    void widgetDropAndInstanceSwitch()
    {
        QTemporaryDir root; const QString input = root.path() + "/input";
        RepairPathSettings::setInputDir(input);
        const QString path = root.path() + "/sample.zip";
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("PKtest"); }
        TPlusRestoreWidget widget;
        auto* source = widget.findChild<QLineEdit*>("tplusSource");
        auto* target = widget.findChild<QComboBox*>("tplusTargetServer");
        QVERIFY(source && target);
        QMimeData mime; mime.setUrls({QUrl::fromLocalFile(path)});
        QDragEnterEvent enter(QPoint(2,2), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &enter); QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(2,2), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &drop); QVERIFY(drop.isAccepted());
        QCOMPARE(QDir::fromNativeSeparators(source->text()), input + "/sample.zip");
        QVERIFY(QFile::exists(path));
        QDragEnterEvent enterAgain(QPoint(2,2), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &enterAgain); QVERIFY(enterAgain.isAccepted());
        QDropEvent second(QPointF(2,2), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &second); QVERIFY(second.isAccepted());
        QVERIFY(source->text() != QDir::toNativeSeparators(input + "/sample.zip"));
        auto* table = widget.findChild<QTableWidget*>("tplusBackupSets"); table->setRowCount(1);
        widget.findChild<QLineEdit*>("tplusDataDirectory")->setText("old path");
        target->setCurrentIndex(target->count()-1);
        QCOMPARE(table->rowCount(), 1);
        QVERIFY(widget.findChild<QLineEdit*>("tplusDataDirectory")->text().isEmpty());
        QVERIFY(!widget.findChild<QPushButton*>("tplusRestore")->isEnabled());
        QVERIFY(!widget.property("sessionBusysetBusy").toBool());
    }
    void realBackupRestore()
    {
        const QString source = qEnvironmentVariable("TPLUS_TEST_SOURCE");
        if (source.isEmpty()) QSKIP("Set TPLUS_TEST_SOURCE to run isolated SQL restore.");
        T3SqlConnection options; options.server = "lpc:.";
        const auto drivers = TPlusBackupRestorer::availableDrivers(); QVERIFY(!drivers.isEmpty()); options.driver = drivers.first();
        const auto progress = [](const QString& message) { qInfo().noquote() << message; };
        QElapsedTimer analysisTimer; analysisTimer.start();
        const auto analysis = TPlusBackupRestorer::analyze(options, source,
            qEnvironmentVariable("TPLUS_TEST_STAGING"), [&]() { return analysisTimer.elapsed() > 60000; }, progress);
        QVERIFY2(analysis.success, qPrintable(analysis.error)); QCOMPARE(analysis.sets.size(), 1);
        QVERIFY(!analysis.account.accountName.trimmed().isEmpty());
        const QString expectedName = qEnvironmentVariable("TPLUS_TEST_EXPECTED_NAME");
        if (!expectedName.isEmpty()) QCOMPARE(analysis.account.accountName, expectedName);
        QCOMPARE(analysis.account.year, 2016); QCOMPARE(analysis.account.month, 1);
        const QString token = QUuid::createUuid().toString(QUuid::Id128);
        const QString target = "UFTData_Test_" + token, control = "TPlusTestControl_" + token;
        const QString cn = "TPlusTest_" + token;
        QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", cn);
        db.setDatabaseName(T3BackupRestorer::connectionString(options)); QVERIFY2(db.open(), qPrintable(db.lastError().text()));
        const auto lit = T3BackupRestorer::quoteLiteral;
        const auto ident = T3BackupRestorer::quoteIdentifier;
        // Before any mutation prove both random test names are unused, then capture exact file ownership.
        { QSqlQuery q(db); QVERIFY(q.exec("SELECT DB_ID(" + lit(target) + "),DB_ID(" + lit(control) + ")"));
          QVERIFY(q.next()); QVERIFY(q.value(0).isNull() && q.value(1).isNull()); }
        QMap<QString, QStringList> ownedFiles;
        const auto captureFiles = [&]() {
            for (const auto& name : {target, control}) {
                QSqlQuery q(db); if (!q.exec("SELECT physical_name FROM sys.master_files WHERE database_id=DB_ID(" + lit(name) + ")")) qFatal("Cannot record test ownership");
                QStringList files; while (q.next()) files.append(q.value(0).toString());
                if (!files.isEmpty()) ownedFiles.insert(name, files);
            }
        };
        struct Cleanup { std::function<void()> action; ~Cleanup() { action(); } } guard{[&]() {
            for (auto it = ownedFiles.cbegin(); it != ownedFiles.cend(); ++it) {
                QStringList quoted; for (const auto& file : it.value()) quoted.append(lit(file));
                const QString sql = "USE master; IF DB_ID(" + lit(it.key()) + ") IS NOT NULL BEGIN "
                    "IF EXISTS(SELECT 1 FROM sys.master_files WHERE database_id=DB_ID(" + lit(it.key())
                    + ") AND physical_name NOT IN(" + quoted.join(',') + ")) RAISERROR(N'Test ownership mismatch',16,1); "
                    "ELSE DROP DATABASE " + ident(it.key()) + "; END;";
                if (!execute(db, sql)) qFatal("Test database cleanup failed");
            }
            db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(cn);
        }};
        const T3RestoreSelection selected{analysis.sets.first(), target};
        const auto restore = [&](const QVector<T3RestoreSelection>& items, const TPlusBackupRestorer::Cancel& cancel = {}) {
            auto result = TPlusBackupRestorer::restore(options, analysis, items,
                analysis.server.dataDirectory, analysis.server.logDirectory, cancel, progress, control);
            captureFiles(); return result;
        };
        auto stopped = restore({selected}, []() { return true; }); QVERIFY(stopped.stopped); QVERIFY(stopped.items.isEmpty());
        auto changed = selected; changed.backup.backupSetId = "wrong-guid";
        const auto stale = restore({changed}); QVERIFY(!stale.error.isEmpty()); QVERIFY(stale.items.isEmpty());
        QVERIFY(execute(db, T3BackupRestorer::controlDatabaseSql(analysis.server.dataDirectory,
            analysis.server.logDirectory, token, control))); captureFiles();
        QVERIFY(execute(db, "USE " + ident(control) + "; CREATE TABLE dbo.EAP_Account(DsName nvarchar(128)); USE master;"));
        const auto incompatible = restore({selected}); QVERIFY(!incompatible.error.isEmpty()); QVERIFY(incompatible.items.isEmpty());
        QVERIFY(execute(db, "USE " + ident(control) + "; DROP TABLE dbo.EAP_Account; USE master;"));
        const auto result = restore({selected});
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error)); QCOMPARE(result.items.size(), 1);
        QVERIFY(result.items.first().success && result.items.first().databaseRestored);
        const auto duplicate = restore({selected}); QVERIFY(!duplicate.error.isEmpty()); QVERIFY(duplicate.items.isEmpty());
        {
            QSqlQuery q(db);
            // The same columns and lookup the importer uses (substitute only isolated control database).
            QVERIFY(q.exec("SELECT DsName,cAcc_Name,iYear,iDuration,VersionNo FROM " + ident(control) + ".dbo.EAP_Account"));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), target); QCOMPARE(q.value(1).toString(), analysis.account.accountName);
            QCOMPARE(q.value(2).toInt(), 2016); QCOMPARE(q.value(3).toInt(), 1);
            QCOMPARE(q.value(4).toString(), QString("16.000.000.0256")); QVERIFY(!q.next());
            QVERIFY(q.exec("SELECT COUNT(*) FROM " + ident(target) + ".dbo.AA_Account")); QVERIFY(q.next()); QVERIFY(q.value(0).toInt() > 0);
            qInfo() << "Restored account subjects:" << q.value(0).toInt();
        }
        QVERIFY(execute(db, "DBCC CHECKDB (" + ident(target) + ") WITH NO_INFOMSGS;"));
        QVERIFY(!execute(db, TPlusBackupRestorer::registrationSql(selected, analysis.account, true, control)));
        qInfo() << "Real restore, importer control fields, duplicate/cancel guards and CHECKDB passed; cleaning test databases.";
    }
};
int main(int argc, char** argv)
{
    QApplication app(argc, argv); QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settings.path());
    TPlusRestoreTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "TPlusRestoreTests.moc"
