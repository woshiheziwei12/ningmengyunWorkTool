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
#include <QTextCodec>
#include <QProcess>
#include <QProcessEnvironment>
#include <QJsonArray>
#include <QJsonDocument>
#include "InspurRestoreWidget.h"
#include "Common/repairpathsettings.h"

static bool execute(QSqlDatabase& db, const QString& sql)
{
    QSqlQuery query(db);
    if (!query.exec(sql)) { qWarning().noquote() << query.lastError().text(); return false; }
    do { while (query.next()) {} } while (query.nextResult());
    if (query.lastError().isValid()) { qWarning().noquote() << query.lastError().text(); return false; }
    return true;
}
class InspurRestoreTests : public QObject
{
    Q_OBJECT
private slots:
    void targetProtection()
    {
        T3BackupSet backup; backup.position=1; backup.databaseName="cwbase6";
        T3RestoreSelection item{backup,"cwbase6_copy"};
        QVERIFY(InspurBackupRestorer::validateSelections({item}).isEmpty());
        for (const QString& name : {QString("master"),QString("cwmaster"),QString("CWMASTER"),QString("x';DROP"),QString(""),QString(129,'x')}) {
            auto bad=item; bad.targetDatabase=name;
            QVERIFY(!InspurBackupRestorer::validateSelections({bad}).isEmpty());
        }
        QVERIFY(!InspurBackupRestorer::validateSelections({item,item}).isEmpty());
        QVERIFY(!InspurBackupRestorer::validateSelections({}).isEmpty());
        InspurAccountMetadata account{"cwbase6","O'Brien","lc0069999"};
        const auto sql=InspurBackupRestorer::registrationSql(item,account,true);
        QVERIFY(sql.contains("O''Brien")); QVERIFY(sql.contains("ROLLBACK TRANSACTION"));
        account.owner="dbo,master";
        QVERIFY(InspurBackupRestorer::registrationSql(item,account).isEmpty());
    }
    void metadataEncodingAndValidation()
    {
        const QString ini=QString::fromUtf8(u8"[DBInfo]\r\nDBName=cwbase6\r\n[AccInfo]\r\nDBName=cwbase6\r\nAccName=中联实业12.0\r\nDBOwner=lc0069999\r\nDBPass=do-not-retain\r\n");
        InspurAccountMetadata account;QString error;
        QVERIFY(InspurBackupRestorer::readMetadata(ini.toUtf8(),&account,&error));
        QCOMPARE(account.accountName,QString::fromUtf8(u8"中联实业12.0"));
        QCOMPARE(account.owner,QString("lc0069999"));
        QVERIFY(InspurBackupRestorer::readMetadata(QTextCodec::codecForName("GB18030")->fromUnicode(ini),&account,&error));
        QCOMPARE(account.accountName,QString::fromUtf8(u8"中联实业12.0"));
        QVERIFY(!InspurBackupRestorer::readMetadata((ini+"DBOwner=dbo").toUtf8(),&account,&error));
        QVERIFY(!error.contains("do-not-retain"));
        QString mismatch=ini;mismatch.replace("[DBInfo]\r\nDBName=cwbase6","[DBInfo]\r\nDBName=other");
        QVERIFY(!InspurBackupRestorer::readMetadata(mismatch.toUtf8(),&account,&error));
        QVERIFY(!InspurBackupRestorer::readMetadata(QByteArray(1024*1024+1,'x'),&account,&error));
    }
    void archiveBoundaries()
    {
        QTemporaryDir root; int index=0;
        const auto makeZip=[&](const QStringList& entries) {
            const QString path=root.filePath(QString::number(++index)+".MSS");
            QJsonArray names;for(const auto& name:entries)names.append(name);
            QProcess process;auto env=QProcessEnvironment::systemEnvironment();
            env.insert("TEST_ZIP",path);env.insert("TEST_NAMES",QString::fromUtf8(QJsonDocument(names).toJson(QJsonDocument::Compact)));
            process.setProcessEnvironment(env);
            process.start("powershell.exe",{"-NoProfile","-NonInteractive","-Command",QString::fromLatin1(R"PS(
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
$zip=[IO.Compression.ZipFile]::Open($env:TEST_ZIP,[IO.Compression.ZipArchiveMode]::Create)
try{foreach($name in (ConvertFrom-Json $env:TEST_NAMES)){$entry=$zip.CreateEntry($name);$stream=$entry.Open();try{$bytes=[Text.Encoding]::UTF8.GetBytes('TAPEtest');$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}}}finally{$zip.Dispose()}
)PS")});
            if (!process.waitForStarted(10000)) { qWarning() << process.errorString(); return QString(); }
            process.closeWriteChannel();
            if(!process.waitForFinished(30000)||process.exitCode()!=0){qWarning().noquote()<<process.readAllStandardError();return QString();}
            return path;
        };
        for(const auto& names:QVector<QStringList>{{"one.BAK","one.INI","one.XML"},{"one.BAK"},{"one.BAK","two.INI"},{"one.BAK","one.INI","other.BAK"},{"../one.BAK","../one.INI"}}){
            const QString path=makeZip(names);QVERIFY(!path.isEmpty());QTemporaryDir output;QString error;
            const bool ok=InspurBackupRestorer::unpack(path,output.path(),&error);
            QCOMPARE(ok,names.size()==3&&names.last()=="one.XML");
            if(ok){QFile file(output.filePath("database.bak"));QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("TAPEtest"));
                QVERIFY(!InspurBackupRestorer::unpack(path,output.path(),&error));}
        }
        QTemporaryDir output;QString error;
        QVERIFY(!InspurBackupRestorer::unpack("missing.MSS",output.path(),&error,[]{return true;}));
        QVERIFY(QDir(output.path()).entryList(QDir::Files).isEmpty());
    }
    void widgetDropAndInstanceSwitch()
    {
        QTemporaryDir root; const QString input = root.path() + "/input";
        RepairPathSettings::setInputDir(input);
        const QString path = root.path() + "/sample.MSS";
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("TAPEtest"); }
        InspurRestoreWidget widget;
        auto* source = widget.findChild<QLineEdit*>("inspurSource");
        auto* target = widget.findChild<QComboBox*>("inspurTargetServer");
        QVERIFY(source && target);
        QMimeData mime; mime.setUrls({QUrl::fromLocalFile(path)});
        QDragEnterEvent enter(QPoint(2,2), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &enter); QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(2,2), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &drop); QVERIFY(drop.isAccepted());
        QCOMPARE(QDir::fromNativeSeparators(source->text()), input + "/sample.MSS");
        QVERIFY(QFile::exists(path));
        QDragEnterEvent enterAgain(QPoint(2,2), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &enterAgain); QVERIFY(enterAgain.isAccepted());
        QDropEvent second(QPointF(2,2), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &second); QVERIFY(second.isAccepted());
        QVERIFY(source->text() != QDir::toNativeSeparators(input + "/sample.MSS"));
        auto* table = widget.findChild<QTableWidget*>("inspurBackupSets"); table->setRowCount(1);
        widget.findChild<QLineEdit*>("inspurDataDirectory")->setText("old path");
        target->setCurrentIndex(target->count()-1);
        QCOMPARE(table->rowCount(), 1);
        QVERIFY(widget.findChild<QLineEdit*>("inspurDataDirectory")->text().isEmpty());
        QVERIFY(!widget.findChild<QPushButton*>("inspurRestore")->isEnabled());
        QVERIFY(!widget.property("sessionBusysetBusy").toBool());
    }
    void realBackupRestore()
    {
        const QString source = qEnvironmentVariable("INSPUR_TEST_SOURCE");
        if (source.isEmpty()) QSKIP("Set INSPUR_TEST_SOURCE to run isolated SQL restore.");
        T3SqlConnection options; options.server = qEnvironmentVariable("INSPUR_TEST_SERVER", "lpc:.\\SQL2008R2");
        const auto drivers = InspurBackupRestorer::availableDrivers(); QVERIFY(!drivers.isEmpty()); options.driver = drivers.first();
        const auto progress = [](const QString& message) { qInfo().noquote() << message; };
        QElapsedTimer analysisTimer; analysisTimer.start();
        const auto analysis = InspurBackupRestorer::analyze(options, source,
            qEnvironmentVariable("INSPUR_TEST_STAGING"), [&]() { return analysisTimer.elapsed() > 60000; }, progress);
        QVERIFY2(analysis.success, qPrintable(analysis.error)); QCOMPARE(analysis.sets.size(), 1);

        const QString token = QUuid::createUuid().toString(QUuid::Id128);
        const QString target = "Inspur_Test_" + token, control = "InspurTestControl_" + token;
        const QString cn = "InspurTest_" + token;
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
        const auto restore = [&](const QVector<T3RestoreSelection>& items, const InspurBackupRestorer::Cancel& cancel = {}) {
            auto result = InspurBackupRestorer::restore(options, analysis, items,
                analysis.server.dataDirectory, analysis.server.logDirectory, cancel, progress, control);
            captureFiles(); return result;
        };
        auto stopped = restore({selected}, []() { return true; }); QVERIFY(stopped.stopped); QVERIFY(stopped.items.isEmpty());
        auto changed = selected; changed.backup.backupSetId = "wrong-guid";
        const auto stale = restore({changed}); QVERIFY(!stale.error.isEmpty()); QVERIFY(stale.items.isEmpty());
        QVERIFY(execute(db, T3BackupRestorer::controlDatabaseSql(analysis.server.dataDirectory,
            analysis.server.logDirectory, token, control))); captureFiles();
        QVERIFY(execute(db, "USE " + ident(control) + "; CREATE TABLE dbo.gsdbztxx(gsdbztxx_base nvarchar(128)); USE master;"));
        QVERIFY(!execute(db,InspurBackupRestorer::controlSchemaSql(control)));
        QVERIFY(execute(db, "USE " + ident(control) + "; DROP TABLE dbo.gsdbztxx; USE master;"));
        const auto result = restore({selected});
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error)); QCOMPARE(result.items.size(), 1);
        QVERIFY(result.items.first().success && result.items.first().databaseRestored);
        const auto duplicate = restore({selected}); QVERIFY(!duplicate.error.isEmpty()); QVERIFY(duplicate.items.isEmpty());
        {
            QSqlQuery q(db);
            // Exact importer list columns, with only the isolated control DB substituted.
            QVERIFY(q.exec("SELECT gsdbztxx_base,gsdbztxx_ztmc,gsdbztxx_owner FROM "+ident(control)+".dbo.gsdbztxx"));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),target);
            const QString accountName=q.value(1).toString(),owner=q.value(2).toString();
            QVERIFY(!accountName.trimmed().isEmpty()); QVERIFY(!owner.isEmpty()); QVERIFY(!q.next());
            if(!analysis.account.owner.isEmpty()) QCOMPARE(owner,analysis.account.owner);
            QVERIFY(q.exec("SELECT F_VAL FROM "+ident(target)+"."+ident(owner)+".LSCONF WHERE F_VKEY='CW_SYDWMC'"));
            QVERIFY(q.next()); const QString company=q.value(0).toString().trimmed(); QVERIFY(!company.isEmpty());
            QCOMPARE(accountName,analysis.account.accountName.isEmpty() ? company : analysis.account.accountName);
            QVERIFY(q.exec("SELECT LTRIM(RTRIM(gsdbztxx_owner))+','+gsdbztxx_base,gsdbztxx_ztmc FROM "+ident(control)+".dbo.gsdbztxx"));
            QVERIFY(q.next());QCOMPARE(q.value(0).toString(),owner+","+target);q.finish();
            QVERIFY(!execute(db,InspurBackupRestorer::registrationSql(selected,{target,accountName,owner},true,control)));
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
    InspurRestoreTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "InspurRestoreTests.moc"
