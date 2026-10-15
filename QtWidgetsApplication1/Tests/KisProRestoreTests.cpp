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
#include "KisProRestoreWidget.h"
#include "Common/repairpathsettings.h"

static bool execute(QSqlDatabase& db, const QString& sql)
{
    QSqlQuery query(db);
    if (!query.exec(sql)) { qWarning().noquote() << query.lastError().text(); return false; }
    do { while (query.next()) {} } while (query.nextResult());
    if (query.lastError().isValid()) { qWarning().noquote() << query.lastError().text(); return false; }
    return true;
}
class KisProRestoreTests : public QObject
{
    Q_OBJECT
private slots:
    void targetProtection()
    {
        T3BackupSet backup; backup.position=1; backup.databaseName="AIS20150212103229";
        T3RestoreSelection item{backup,"AIS_Test_123"};
        QVERIFY(KisProBackupRestorer::validateSelections({item}).isEmpty());
        for (const QString& name : {QString("master"),QString("AcctCtl"),QString("OtherDB"),QString("AIS_x';DROP"),QString("AIS")}) {
            auto bad=item; bad.targetDatabase=name;
            QVERIFY(!KisProBackupRestorer::validateSelections({bad}).isEmpty());
        }
        QVERIFY(!KisProBackupRestorer::validateSelections({item,item}).isEmpty());
        QVERIFY(!KisProBackupRestorer::validateSelections({}).isEmpty());
        const auto sql=KisProBackupRestorer::registrationSql(item,"O'Brien",true);
        QVERIFY(sql.contains("O''Brien")); QVERIFY(sql.contains("ROLLBACK TRANSACTION"));
        QVERIFY(KisProBackupRestorer::registrationSql(item,"").isEmpty());
        QVERIFY(KisProBackupRestorer::isSupportedHeader("TAPE1234"));
        QVERIFY(KisProBackupRestorer::isSupportedHeader("MSSQLBAK"));
        QVERIFY(!KisProBackupRestorer::isSupportedHeader("PKzip"));
        QTemporaryDir directory; QString error;
        for (const QString& name:{QString("one.bak"),QString("two.bak")}) {
            QFile file(directory.filePath(name)); QVERIFY(file.open(QIODevice::WriteOnly));file.write("TAPE");
        }
        QVERIFY(KisProBackupRestorer::resolveSource(directory.path(),&error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void widgetDropAndInstanceSwitch()
    {
        QTemporaryDir root; const QString input = root.path() + "/input";
        RepairPathSettings::setInputDir(input);
        const QString path = root.path() + "/sample.bak";
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("TAPEtest"); }
        KisProRestoreWidget widget;
        auto* source = widget.findChild<QLineEdit*>("kisProSource");
        auto* target = widget.findChild<QComboBox*>("kisProTargetServer");
        QVERIFY(source && target);
        QMimeData mime; mime.setUrls({QUrl::fromLocalFile(path)});
        QDragEnterEvent enter(QPoint(2,2), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &enter); QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(2,2), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &drop); QVERIFY(drop.isAccepted());
        QCOMPARE(QDir::fromNativeSeparators(source->text()), input + "/sample.bak");
        QVERIFY(QFile::exists(path));
        QDragEnterEvent enterAgain(QPoint(2,2), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &enterAgain); QVERIFY(enterAgain.isAccepted());
        QDropEvent second(QPointF(2,2), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &second); QVERIFY(second.isAccepted());
        QVERIFY(source->text() != QDir::toNativeSeparators(input + "/sample.bak"));
        auto* table = widget.findChild<QTableWidget*>("kisProBackupSets"); table->setRowCount(1);
        widget.findChild<QLineEdit*>("kisProDataDirectory")->setText("old path");
        target->setCurrentIndex(target->count()-1);
        QCOMPARE(table->rowCount(), 1);
        QVERIFY(widget.findChild<QLineEdit*>("kisProDataDirectory")->text().isEmpty());
        QVERIFY(!widget.findChild<QPushButton*>("kisProRestore")->isEnabled());
        QVERIFY(!widget.property("sessionBusysetBusy").toBool());
    }
    void realBackupRestore()
    {
        const QString source = qEnvironmentVariable("KISPRO_TEST_SOURCE");
        if (source.isEmpty()) QSKIP("Set KISPRO_TEST_SOURCE to run isolated SQL restore.");
        T3SqlConnection options; options.server = "lpc:.";
        const auto drivers = KisProBackupRestorer::availableDrivers(); QVERIFY(!drivers.isEmpty()); options.driver = drivers.first();
        const auto progress = [](const QString& message) { qInfo().noquote() << message; };
        QElapsedTimer analysisTimer; analysisTimer.start();
        const auto analysis = KisProBackupRestorer::analyze(options, source,
            qEnvironmentVariable("KISPRO_TEST_STAGING"), [&]() { return analysisTimer.elapsed() > 60000; }, progress);
        QVERIFY2(analysis.success, qPrintable(analysis.error)); QCOMPARE(analysis.sets.size(), 1);

        const QString token = QUuid::createUuid().toString(QUuid::Id128);
        const QString target = "AIS_Test_" + token, control = "KisProTestControl_" + token;
        const QString cn = "KisProTest_" + token;
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
        const auto restore = [&](const QVector<T3RestoreSelection>& items, const KisProBackupRestorer::Cancel& cancel = {}) {
            auto result = KisProBackupRestorer::restore(options, analysis, items,
                analysis.server.dataDirectory, analysis.server.logDirectory, cancel, progress, control);
            captureFiles(); return result;
        };
        auto stopped = restore({selected}, []() { return true; }); QVERIFY(stopped.stopped); QVERIFY(stopped.items.isEmpty());
        auto changed = selected; changed.backup.backupSetId = "wrong-guid";
        const auto stale = restore({changed}); QVERIFY(!stale.error.isEmpty()); QVERIFY(stale.items.isEmpty());
        QVERIFY(execute(db, T3BackupRestorer::controlDatabaseSql(analysis.server.dataDirectory,
            analysis.server.logDirectory, token, control))); captureFiles();
        QVERIFY(execute(db, "USE " + ident(control) + "; CREATE TABLE dbo.t_ad_kdAccount_gl(FDBName nvarchar(128)); USE master;"));
        QVERIFY(!execute(db,KisProBackupRestorer::controlSchemaSql(control)));
        QVERIFY(execute(db, "USE " + ident(control) + "; DROP TABLE dbo.t_ad_kdAccount_gl; USE master;"));
        const auto result = restore({selected});
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error)); QCOMPARE(result.items.size(), 1);
        QVERIFY(result.items.first().success && result.items.first().databaseRestored);
        const auto duplicate = restore({selected}); QVERIFY(!duplicate.error.isEmpty()); QVERIFY(duplicate.items.isEmpty());
        {
            QSqlQuery q(db);
            // Exact importer list columns, with only the isolated control DB substituted.
            QVERIFY(q.exec("SELECT FDBName,FAcctName FROM "+ident(control)+".dbo.t_ad_kdAccount_gl"));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),target);
            const QString accountName=q.value(1).toString(); QVERIFY(!accountName.trimmed().isEmpty()); QVERIFY(!q.next());
            QVERIFY(q.exec("SELECT FValue FROM "+ident(target)+".dbo.t_SystemProfile WHERE FCategory='General' AND FKey='CompanyName'"));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),accountName);
            QVERIFY(q.exec("SELECT COUNT(*) FROM "+ident(target)+".dbo.t_Account")); QVERIFY(q.next()); QVERIFY(q.value(0).toInt()>0);
            qInfo() << "Restored account:" << accountName << "subjects:" << q.value(0).toInt();
            QVERIFY(!execute(db,KisProBackupRestorer::registrationSql(selected,accountName,true,control)));
        }
        QVERIFY(execute(db,"DBCC CHECKDB ("+ident(target)+") WITH NO_INFOMSGS;"));
        qInfo() << "Real restore, importer control fields, duplicate/cancel guards and CHECKDB passed; cleaning test databases.";
    }
};
int main(int argc, char** argv)
{
    QApplication app(argc, argv); QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settings.path());
    KisProRestoreTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "KisProRestoreTests.moc"
