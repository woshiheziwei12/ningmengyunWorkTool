#include "Common/BakBackupRestorer.h"
#include "Common/SqlBackupCompatibility.h"
#include "Common/SqlServer211.h"
#include "Common/RestorePreferences.h"
#include "BakRestoreWidget.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
void checkBak(bool condition, const char* message)
{ if (!condition) qFatal("BakRestore: %s", message); }

void writeFixture(const QString& path, const QByteArray& bytes = "not a SQL backup")
{
    checkBak(QDir().mkpath(QFileInfo(path).absolutePath()), "create fixture directory");
    QFile file(path);
    checkBak(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write fixture");
}

BakBackupEntry entry(const QString& path, const QString& database, int position = 1)
{
    BakBackupEntry value;
    value.sourcePath = path;
    value.softwareVersionBuild = 1601;
    value.compatibilityLevel = 100;
    value.backupType = 1;
    value.backupTypeDescription = "Database";
    value.mediaFamilyCount = 1;
    value.analysis.success = true;
    value.analysis.preparedPath = path;
    value.analysis.preparedServer = ".";
    value.analysis.targetServer = ".";
    value.analysis.server.name = "test-local";
    value.analysis.server.version = "16.0.1000.6";
    value.analysis.server.databaseVersion = 957;
    value.analysis.server.dataDirectory = "D:/isolated-test/data";
    value.analysis.server.logDirectory = "D:/isolated-test/log";
    T3BackupSet backup;
    backup.position = position;
    backup.databaseName = database;
    backup.backupDate = "2026-07-24 17:11:00";
    backup.backupSetId = QUuid::createUuid().toString();
    backup.softwareVersionMajor = 13;
    backup.softwareVersionMinor = 0;
    backup.databaseVersion = 852;
    value.analysis.sets.append(backup);
    return value;
}

QByteArray fileHash(const QString& path)
{
    QFile file(path);
    checkBak(file.open(QIODevice::ReadOnly), "open original sample for checksum");
    QCryptographicHash digest(QCryptographicHash::Sha256);
    checkBak(digest.addData(&file), "hash original sample");
    return digest.result();
}

QString tableText(const QTableWidget* table, int row, int column)
{ const auto* item = table->item(row, column); return item ? item->text() : QString(); }

void requireIntegration(bool condition, const QString& message)
{ if (!condition) throw message.isEmpty() ? QStringLiteral("SQL integration assertion failed") : message; }

// Creates only synthetic fixtures on a local instance. Customer backups never enter this path.
void runBakIntegration()
{
    if (qEnvironmentVariable("BAK_RESTORE_INTEGRATION") != "1") return;
    const QString token=QUuid::createUuid().toString(QUuid::Id128);
    const QString sourceA="BakTest_"+token+"_SourceA", sourceB="BakTest_"+token+"_SourceB";
    const QString targetA="BakTest_"+token+"_RestoredA", targetB="BakTest_"+token+"_RestoredB";
    const QString targetSecond="BakTest_"+token+"_Snapshot2", targetRejected="BakTest_"+token+"_Rejected";
    const QStringList names{sourceA,sourceB,targetA,targetB,targetSecond,targetRejected};
    const auto lit=T3BackupRestorer::quoteLiteral;
    const auto ident=T3BackupRestorer::quoteIdentifier;
    T3SqlConnection options; options.server=".";
    const auto drivers=T3BackupRestorer::availableDrivers();
    checkBak(!drivers.isEmpty(), "integration requires an installed SQL ODBC driver"); options.driver=drivers.first();
    T3ServerInfo server; QString error;
    checkBak(T3BackupRestorer::testConnection(options,&server,&error),qPrintable(error));
    const QString stagingRoot=qEnvironmentVariable("BAK_RESTORE_STAGING");
    checkBak(!stagingRoot.isEmpty() && QDir(stagingRoot).exists(), "integration requires an explicit SQL-readable staging root");
    QTemporaryDir backupDirectory(QDir(stagingRoot).filePath("BakIntegration_XXXXXX"));
    checkBak(backupDirectory.isValid(), "create isolated integration backup directory");
    const QString backupA=backupDirectory.filePath("multiple-snapshots.bak");
    const QString backupB=backupDirectory.filePath("second-database.BAK");
    const QString connectionName="BakIntegration_"+token;
    QSqlDatabase db=QSqlDatabase::addDatabase("QODBC",connectionName);
    db.setConnectOptions("SQL_ATTR_LOGIN_TIMEOUT=8;SQL_ATTR_CONNECTION_TIMEOUT=15");
    db.setDatabaseName(T3BackupRestorer::connectionString(options));
    checkBak(db.open(),qPrintable(db.lastError().text()));
    struct OwnedDatabase { int id=0; QString created; QStringList files; };
    QMap<QString,OwnedDatabase> owned;
    bool namesReserved=false;
    QString failure,cleanupFailure;
    const auto execute=[&](const QString& sql) {
        QSqlQuery query(db);
        if(!query.exec(sql)) throw query.lastError().text();
        do { while(query.next()) {} } while(query.nextResult());
        if(query.lastError().isValid()) throw query.lastError().text();
    };
    const auto exists=[&](const QString& name) {
        QSqlQuery query(db);
        requireIntegration(query.exec("SELECT DB_ID("+lit(name)+")") && query.next(),query.lastError().text());
        return !query.value(0).isNull();
    };
    const auto observe=[&](const QString& name) {
        QSqlQuery query(db); OwnedDatabase result;
        requireIntegration(query.exec("SELECT database_id,CONVERT(nvarchar(23),create_date,126) FROM sys.databases WHERE name="+lit(name)),query.lastError().text());
        if(!query.next()) return result;
        result.id=query.value(0).toInt(); result.created=query.value(1).toString(); query.finish();
        requireIntegration(query.exec("SELECT physical_name FROM sys.master_files WHERE database_id="+QString::number(result.id)+" ORDER BY file_id"),query.lastError().text());
        while(query.next())result.files.append(query.value(0).toString());
        requireIntegration(!query.lastError().isValid(),query.lastError().text());
        return result;
    };
    const auto capture=[&]() {
        if(!namesReserved)return;
        for(const auto& name:names) if(!owned.contains(name)) {
            const auto database=observe(name);
            if(database.id>0) {
                owned.insert(name,database);
                qInfo().noquote()<<"BAK_TEST_OWNED:"<<name<<"id="<<database.id
                    <<"created="<<database.created<<"files="<<database.files.join(" | ");
            }
        }
    };
    const auto cleanup=[&]() {
        try {
            capture();
            for(auto it=owned.cbegin();it!=owned.cend();++it) {
                const auto current=observe(it.key());
                if(current.id==0)continue;
                requireIntegration(current.id==it->id && current.created==it->created
                    && !current.files.isEmpty() && current.files==it->files,
                    "Test database ownership changed; retained "+it.key());
                execute("USE master; DROP DATABASE "+ident(it.key())+";");
                requireIntegration(!exists(it.key()),"Test cleanup did not remove "+it.key());
            }
        } catch(const QString& message) { cleanupFailure=message; }
    };
    try {
        for(const auto& name:names)requireIntegration(!exists(name),"Random test database already exists: "+name);
        namesReserved=true;
        const auto createSource=[&](const QString& name,const QString& suffix) {
            const QString data=QDir::toNativeSeparators(QDir(server.dataDirectory).filePath("BakFixture_"+token+suffix+".mdf"));
            const QString log=QDir::toNativeSeparators(QDir(server.logDirectory).filePath("BakFixture_"+token+suffix+".ldf"));
            requireIntegration(!QFileInfo::exists(data) && !QFileInfo::exists(log),"Random SQL fixture files already exist");
            execute("CREATE DATABASE "+ident(name)+" ON PRIMARY (NAME=N'BakFixtureData',FILENAME="+lit(data)
                +") LOG ON (NAME=N'BakFixtureLog',FILENAME="+lit(log)+");");
            capture();
            execute("USE "+ident(name)+"; CREATE TABLE dbo.Probe(Id int PRIMARY KEY,Marker nvarchar(50)); "
                "INSERT INTO dbo.Probe VALUES(1,N'first'),(2,N'keep'); USE master;");
        };
        createSource(sourceA,"A"); createSource(sourceB,"B");
        execute("BACKUP DATABASE "+ident(sourceA)+" TO DISK="+lit(backupA)+" WITH INIT,COPY_ONLY,CHECKSUM;");
        execute("USE "+ident(sourceA)+"; UPDATE dbo.Probe SET Marker=N'second' WHERE Id=1; USE master;");
        execute("BACKUP DATABASE "+ident(sourceA)+" TO DISK="+lit(backupA)+" WITH NOINIT,COPY_ONLY,CHECKSUM;");
        execute("BACKUP DATABASE "+ident(sourceB)+" TO DISK="+lit(backupB)+" WITH INIT,COPY_ONLY,CHECKSUM;");
        writeFixture(backupDirectory.filePath("broken.bak"),"TAPE-broken-fixture");
        const auto progress=[](const QString& text) { qInfo().noquote()<<"BAK_INTEGRATION:"<<text; };
        auto batch=BakBackupRestorer::read({options},backupDirectory.path(),backupDirectory.path(),{},progress);
        int indexA=-1,indexB=-1,indexSecond=-1,errorRows=0;
        for(int index=0;index<batch.entries.size();++index) {
            const auto& row=batch.entries[index];
            if(!row.readError.isEmpty()){++errorRows;continue;}
            requireIntegration(row.analysis.sets.size()==1,"One visible row is required per backup set");
            const auto& set=row.analysis.sets.first();
            if(QFileInfo(row.sourcePath).fileName()=="multiple-snapshots.bak") {
                if(set.position==1)indexA=index; if(set.position==2)indexSecond=index;
            } else if(QFileInfo(row.sourcePath).fileName()=="second-database.BAK")indexB=index;
        }
        requireIntegration(batch.entries.size()==4 && indexA>=0 && indexSecond>=0 && indexB>=0 && errorRows==1,
            "Real multi-position BAK and independent bad file must all remain visible");
        batch=BakBackupRestorer::validateTarget(batch,options,backupDirectory.path(),{},progress);
        requireIntegration(batch.entries[indexA].analysis.success && batch.entries[indexB].analysis.success
            && batch.entries[indexSecond].analysis.success,"All complete synthetic backups validate on local target");
        const auto restore=[&](const BakBackupBatch& analysis,const QVector<BakRestoreSelection>& selected,
            const BakBackupRestorer::Cancel& cancel=BakBackupRestorer::Cancel()) {
            auto result=BakBackupRestorer::restore(options,analysis,selected,server.dataDirectory,server.logDirectory,cancel,progress);
            capture(); return result;
        };
        auto result=restore(batch,{{indexA,targetRejected}},[](){return true;});
        requireIntegration(result.stopped && !exists(targetRejected),"Cancelled restore must create no database");
        auto changed=batch; changed.entries[indexA].analysis.sets[0].backupSetId="stale-guid";
        result=restore(changed,{{indexA,targetRejected}});
        requireIntegration(!result.error.isEmpty() && !exists(targetRejected),"Changed backup-set identity must fail before restore");
        result=restore(batch,{{indexA,targetRejected},{indexB,sourceB}});
        requireIntegration(!result.error.isEmpty() && !exists(targetRejected),"Existing later target must reject the whole plan before first restore");
        result=restore(batch,{{indexA,targetA},{indexB,targetB}});
        requireIntegration(result.error.isEmpty() && result.items.size()==2
            && result.items[0].success && result.items[1].success,
            "Two different files at position one must restore successfully: "+result.error);
        result=restore(batch,{{indexSecond,targetSecond}});
        requireIntegration(result.error.isEmpty() && result.items.size()==1 && result.items[0].success,
            "An explicitly selected second backup set must restore successfully: "+result.error);
        const auto marker=[&](const QString& name) {
            QSqlQuery query(db);
            requireIntegration(query.exec("SELECT Marker FROM "+ident(name)+".dbo.Probe WHERE Id=1") && query.next(),query.lastError().text());
            return query.value(0).toString();
        };
        requireIntegration(marker(targetA)=="first" && marker(targetB)=="first" && marker(targetSecond)=="second",
            "Restored data must correspond to the chosen file and backup position");
        result=restore(batch,{{indexA,targetA}});
        requireIntegration(!result.error.isEmpty() && marker(targetA)=="first","A repeat restore must preserve the existing database");
        qInfo()<<"PASS: BAK real multi-file restore, backup positions, preflight collisions, cancellation, identity checks and exact restored data";
    } catch(const QString& message) { failure=message; }
    catch(...) { failure="Unexpected exception in isolated BAK integration"; }
    cleanup();
    db.close(); db=QSqlDatabase(); QSqlDatabase::removeDatabase(connectionName);
    if(!cleanupFailure.isEmpty())failure+="; CLEANUP: "+cleanupFailure;
    checkBak(failure.isEmpty(),qPrintable(failure));
    qInfo()<<"PASS: BAK integration owned databases removed; synthetic backup directory released";
}
}

void runBakRestoreTests()
{
    const QString originalSource = RestorePreferences::backupDirectory();
    const QString originalStaging = RestorePreferences::stagingDirectory();
    QTemporaryDir temporary;
    QTemporaryDir outside;
    checkBak(temporary.isValid() && outside.isValid(), "isolated fixture roots");
    const QString first = temporary.filePath("one.bak");
    const QString second = temporary.filePath("nested/TWO.BAK");
    const QString third = temporary.filePath(QString::fromUtf8(u8"nested/中文目录/third.Bak"));
    writeFixture(first); writeFixture(second); writeFixture(third);
    writeFixture(temporary.filePath("notes.txt"));
    writeFixture(temporary.filePath("backup.zip"));
    writeFixture(outside.filePath("must-not-follow.bak"));
    bool linkedDirectory = false;
#ifdef Q_OS_WIN
    const QString link = QDir::toNativeSeparators(temporary.filePath("linked-directory"));
    const QString destination = QDir::toNativeSeparators(outside.path());
    linkedDirectory = CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(link.utf16()),
        reinterpret_cast<LPCWSTR>(destination.utf16()), SYMBOLIC_LINK_FLAG_DIRECTORY | 0x2) != FALSE;
#else
    linkedDirectory = QFile::link(outside.path(), temporary.filePath("linked-directory"));
#endif
    QString error;
    const auto files = BakBackupRestorer::discoverFiles(temporary.path(), &error);
    checkBak(error.isEmpty() && files.size() == 3, "recursive scan includes all BAK variants and excludes non-BAK files and linked directories");
    for (const auto& path : {first, second, third})
        checkBak(files.contains(QFileInfo(path).absoluteFilePath()), "recursive scan retains each source path");
    checkBak(BakBackupRestorer::discoverFiles(second, &error).size() == 1 && error.isEmpty(), "single uppercase BAK is accepted");
    checkBak(BakBackupRestorer::discoverFiles(temporary.filePath("notes.txt"), &error).isEmpty()
        && !error.isEmpty(), "non-BAK input is rejected");
    checkBak(BakBackupRestorer::discoverFiles(temporary.filePath("missing"), &error).isEmpty()
        && !error.isEmpty(), "missing input is rejected");
    QDir().mkpath(temporary.filePath("empty"));
    checkBak(BakBackupRestorer::discoverFiles(temporary.filePath("empty"), &error).isEmpty()
        && !error.isEmpty(), "empty directory is explained");
    if (!linkedDirectory) qInfo() << "SKIP: creating directory symlink is unavailable; recursive BAK scan was still checked";

    // No candidates means no SQL/network access; every discovered file must survive as an error row.
    const auto unavailable = BakBackupRestorer::read({}, temporary.path(), temporary.filePath("stage"));
    checkBak(unavailable.entries.size() == 3, "a missing SQL reader does not hide any BAK file");
    for (const auto& row : unavailable.entries)
        checkBak(!row.readError.isEmpty() && !BakBackupRestorer::isRestorable(row), "unreadable file is retained and cannot restore");
    const auto cancelled = BakBackupRestorer::read({}, temporary.path(), temporary.filePath("stage"), []() { return true; });
    checkBak(cancelled.stopped, "read cancellation is distinct from success");

    BakBackupBatch batch;
    batch.readerServer = ".";
    batch.entries = {entry(first, "Business_A"), entry(second, "UFTSystem"), entry(first, "Business_A", 2)};
    checkBak(BakBackupRestorer::sourceVersionText(batch.entries[0]).contains("SQL Server 2016")
        && BakBackupRestorer::sourceVersionText(batch.entries[0]).contains("13.0.1601"), "reported source version includes product and actual build");
    checkBak(BakBackupRestorer::validateSelections(batch, {{0,"Restored_A"},{1,"Restored_B"}}).isEmpty(), "position one from different files is valid");
    checkBak(BakBackupRestorer::validateSelections(batch, {{0,"Snapshot_1"},{2,"Snapshot_2"}}).isEmpty(), "two distinct positions in one BAK are independent rows");
    checkBak(BakBackupRestorer::validateSelections(batch, {{1,"UFTSystem"}}).isEmpty(), "ordinary software control database has no special registration restrictions");
    checkBak(!BakBackupRestorer::validateSelections(batch, {}).isEmpty(), "empty selection is refused");
    checkBak(!BakBackupRestorer::validateSelections(batch, {{0,"Duplicate"},{1,"duplicate"}}).isEmpty(), "target names are unique ignoring case");
    checkBak(!BakBackupRestorer::validateSelections(batch, {{0,"First"},{0,"Second"}}).isEmpty(), "a source backup set cannot be selected twice");
    checkBak(!BakBackupRestorer::validateSelections(batch, {{-1,"Outside"}}).isEmpty()
        && !BakBackupRestorer::validateSelections(batch, {{3,"Outside"}}).isEmpty(), "invalid row indices fail closed");
    for (const auto& name : {"master","model","msdb","tempdb","MSSQLSYSTEMRESOURCE","Distribution"}) {
        checkBak(!BakBackupRestorer::validateSelections(batch, {{0,QString::fromLatin1(name)}}).isEmpty(), "system target names are protected");
        auto systemSource = batch;
        systemSource.entries[0].analysis.sets[0].databaseName = QString::fromLatin1(name);
        checkBak(!BakBackupRestorer::validateSelections(systemSource, {{0,"Renamed_system"}}).isEmpty(), "renaming cannot bypass system source protection");
    }
    auto duplicateSource = batch;
    duplicateSource.entries[2].analysis.sets[0].position = 1;
    checkBak(!BakBackupRestorer::validateSelections(duplicateSource, {{0,"One"},{2,"Two"}}).isEmpty(), "source path plus position identifies a backup set");
    const auto expectRejected = [&](const BakBackupEntry& modified, const char* reason) {
        auto invalid = batch; invalid.entries[0] = modified;
        checkBak(!BakBackupRestorer::validateSelections(invalid, {{0,"Rejected"}}).isEmpty(), reason);
    };
    auto bad = batch.entries[0]; bad.backupType = 2; expectRejected(bad, "transaction log cannot be used as a complete restore");
    bad = batch.entries[0]; bad.backupType = 5; expectRejected(bad, "differential backup cannot be used as a complete restore");
    bad = batch.entries[0]; bad.mediaFamilyCount = 2; expectRejected(bad, "striped multi-device media is not treated as standalone BAK");
    bad = batch.entries[0]; bad.mediaFamilyCount = 0; expectRejected(bad, "unknown media family count cannot restore");
    bad = batch.entries[0]; bad.damaged = true; expectRejected(bad, "damaged header does not enable restore");
    bad = batch.entries[0]; bad.readError = "header failed"; expectRejected(bad, "per-file read error cannot restore");
    bad = batch.entries[0]; bad.analysis.success = false; expectRejected(bad, "unvalidated target cannot restore");
    bad = batch.entries[0]; bad.analysis.sets[0].softwareVersionMajor = 0; expectRejected(bad, "unknown source version cannot restore");
    bad = batch.entries[0]; bad.analysis.server = SqlServer211::version(); expectRejected(bad, "SQL 2016 cannot restore onto 211 SQL 2008 R2 even at compatibility 100");
    checkBak(!SqlBackupCompatibility::error(batch.entries[0].analysis.sets, SqlServer211::version()).isEmpty(), "211 compatibility is derived from backup engine version");

    BakBackupEntry failed;
    failed.sourcePath = temporary.filePath("unreadable.bak"); failed.readError = "sample read error";
    auto logBackup = batch.entries[0]; logBackup.backupType = 2; logBackup.backupTypeDescription = "Transaction Log";
    logBackup.sourcePath = temporary.filePath("log.bak");
    batch.entries.append(failed); batch.entries.append(logBackup);
    {
        BakRestoreWidget widget;
        widget.setAttribute(Qt::WA_DontShowOnScreen);
        widget.m_batch = batch;
        widget.m_dataDirectory->setText(batch.entries[0].analysis.server.dataDirectory);
        widget.m_logDirectory->setText(batch.entries[0].analysis.server.logDirectory);
        widget.renderEntries(false);
        auto* table = widget.findChild<QTableWidget*>("bakEntries");
        auto* target = widget.findChild<QComboBox*>("bakTargetServer");
        auto* restore = widget.findChild<QPushButton*>("bakRestore");
        auto* selectAll = widget.findChild<QPushButton*>("bakSelectAll");
        checkBak(table && target && restore && selectAll, "generic restore exposes file list, target and explicit restore controls");
        checkBak(table->rowCount() == 5 && table->columnCount() == 10, "every set and failed file has a visible row");
        checkBak(tableText(table,0,3).contains("13.0.1601") && tableText(table,0,3).contains("SQL Server 2016")
            && tableText(table,0,4) == "852" && tableText(table,0,5) == "100", "table shows exact engine build, database internal version and compatibility separately");
        checkBak(tableText(table,3,9).contains("sample read error"), "failed-file reason remains visible beside successful files");
        checkBak(!restore->isEnabled(), "reading a directory never starts or preselects restore");
        for (int row=0; row<table->rowCount(); ++row)
            checkBak(table->item(row,0)->checkState() == Qt::Unchecked, "initial selection is empty");
        checkBak(!(table->item(3,0)->flags() & Qt::ItemIsEnabled)
            && !(table->item(4,0)->flags() & Qt::ItemIsEnabled), "failed and non-full backups cannot be selected");
        selectAll->click();
        checkBak(table->item(0,0)->checkState() == Qt::Checked && table->item(1,0)->checkState() == Qt::Checked
            && table->item(2,0)->checkState() == Qt::Checked && table->item(3,0)->checkState() == Qt::Unchecked
            && table->item(4,0)->checkState() == Qt::Unchecked, "select-all selects only restorable rows");
        table->item(0,8)->setText("Renamed_business");
        table->item(2,8)->setText("Another_snapshot");
        checkBak(restore->isEnabled(), "valid distinct targets enable restore");
        table->item(2,8)->setText("renamed_BUSINESS");
        checkBak(!restore->isEnabled(), "duplicate selected target disables restore before any SQL");
        table->item(2,8)->setText("Another_snapshot");
        widget.m_batch.entries[0].analysis.success = false;
        widget.m_batch.entries[0].analysis.error = "incompatible target";
        widget.renderEntries(true);
        checkBak(table->item(0,0)->checkState() == Qt::Checked && tableText(table,0,8) == "Renamed_business"
            && !restore->isEnabled(), "target validation preserves selection and renamed targets while preventing invalid restore");
        widget.m_batch.entries[0] = batch.entries[0]; widget.renderEntries(true);
        checkBak(table->item(0,0)->checkState() == Qt::Checked && tableText(table,0,8) == "Renamed_business"
            && restore->isEnabled(), "returning to a compatible target preserves the user's plan");
        for (int row=0; row<3; ++row) widget.m_batch.entries[row].analysis.success=false;
        widget.renderEntries(true);
        checkBak(selectAll->isEnabled() && selectAll->text()==QString::fromUtf8(u8"取消全选"),
            "an all-incompatible selected plan can still be cleared");
        selectAll->click();
        for (int row=0; row<table->rowCount(); ++row)
            checkBak(table->item(row,0)->checkState()==Qt::Unchecked, "clear-all also clears invalid selections");
        checkBak(!selectAll->isEnabled() && !restore->isEnabled(), "invalid unchecked rows cannot be selected again");
        auto* scan = target->parentWidget()->findChild<QPushButton*>(target->objectName()+"Scan");
        checkBak(scan, "local scan remains available");
        widget.setBusy(true);
        checkBak(!target->isEnabled() && !scan->isEnabled() && !restore->isEnabled(), "background work locks target scanning and restore");
        widget.setBusy(false);
        checkBak(target->isEnabled() && scan->isEnabled(), "background completion unlocks target controls");
        for (auto* label : widget.findChildren<QLabel*>())
            checkBak(!label->text().contains("T+") && !label->text().contains(QString::fromUtf8(u8"畅捷通")), "generic page has no software-specific branding");
        qInfo() << "PASS: BAK discovery, per-file errors, backup positions, version protection, target selection and busy state";
    }

    // Explicit opt-in: read the actual directory through the real UI; never call restore.
    const QString sample = qEnvironmentVariable("BAK_RESTORE_SAMPLE_DIR");
    if (!sample.isEmpty()) {
        QString scanError;
        const auto sampleFiles = BakBackupRestorer::discoverFiles(sample, &scanError);
        checkBak(scanError.isEmpty() && !sampleFiles.isEmpty(), qPrintable(scanError));
        QMap<QString,QByteArray> originalHashes;
        for (const auto& path : sampleFiles) originalHashes.insert(path,fileHash(path));
        const QString staging = qEnvironmentVariable("BAK_RESTORE_STAGING");
        checkBak(!staging.isEmpty() && QDir(staging).exists(), "sample read requires explicit SQL-readable staging root");
        {
            BakRestoreWidget widget;
            widget.setAttribute(Qt::WA_DontShowOnScreen);
            auto* target = widget.findChild<QComboBox*>("bakTargetServer");
            const int remote = target->findData(SqlServer211::endpoint());
            if (remote >= 0) target->removeItem(remote);
            checkBak(target->count() > 0, "a local SQL reader is available");
            widget.findChild<QLineEdit*>("bakSource")->setText(sample);
            widget.m_staging->setText(staging);
            QObject::connect(&widget,&BakRestoreWidget::operationLog,&widget,[](const QString& text) { qInfo().noquote() << text; });
            QEventLoop loop; bool success = false; bool timedOut = false;
            QObject::connect(&widget,&BakRestoreWidget::operationFinished,&loop,[&](bool ok) { success=ok; loop.quit(); });
            QTimer timeout; timeout.setSingleShot(true);
            QObject::connect(&timeout,&QTimer::timeout,&loop,[&]() {
                timedOut=true;
                if (auto* stop=widget.findChild<QPushButton*>("bakStop")) stop->click();
                loop.quit();
            });
            timeout.start(180000); widget.findChild<QPushButton*>("bakRead")->click(); loop.exec();
            timeout.stop();
            checkBak(success && !timedOut, "real directory header reading finishes successfully");
            QSet<QString> observedFiles;
            const QString expectedVersion=qEnvironmentVariable("BAK_RESTORE_EXPECT_VERSION");
            const int expectedInternal=qEnvironmentVariableIntValue("BAK_RESTORE_EXPECT_INTERNAL");
            const int expectedCompatibility=qEnvironmentVariableIntValue("BAK_RESTORE_EXPECT_COMPATIBILITY");
            for (const auto& row : widget.m_batch.entries) {
                observedFiles.insert(QFileInfo(row.sourcePath).absoluteFilePath());
                checkBak(row.readError.isEmpty() && row.analysis.sets.size()==1, "each real BAK has readable header metadata");
                const auto& backup=row.analysis.sets.first();
                if (!expectedVersion.isEmpty()) checkBak(BakBackupRestorer::sourceVersionText(row).contains(expectedVersion), "real BAK engine version matches expected header");
                if (expectedInternal) checkBak(backup.databaseVersion==expectedInternal, "real BAK internal version matches expected header");
                if (expectedCompatibility) checkBak(row.compatibilityLevel==expectedCompatibility, "real BAK compatibility level matches expected header");
                if (backup.softwareVersionMajor>10)
                    checkBak(!SqlBackupCompatibility::error(row.analysis.sets,SqlServer211::version()).isEmpty(), "newer real BAK explicitly refuses 211 downgrade");
                qInfo().noquote() << "READONLY_BAK:" << QFileInfo(row.sourcePath).fileName()
                    << BakBackupRestorer::sourceVersionText(row) << "internal=" << backup.databaseVersion
                    << "compatibility=" << row.compatibilityLevel;
            }
            checkBak(observedFiles.size()==sampleFiles.size(), "real directory displays all BAK files");
            checkBak(!widget.findChild<QPushButton*>("bakRestore")->isEnabled(), "real analysis does not select or execute restore");
            QScrollArea preview;
            preview.setAttribute(Qt::WA_DontShowOnScreen); preview.setWidgetResizable(true); preview.setWidget(&widget);
            preview.resize(1600,1000); preview.show(); QApplication::processEvents();
            const QString output=QCoreApplication::arguments().value(1);
            checkBak(preview.grab().save(output+"/bak-real-directory.png"), "save actual directory version screenshot");
            preview.resize(800,480); QApplication::processEvents();
            checkBak(preview.grab().save(output+"/bak-real-directory-small.png"), "save small-window directory screenshot");
            preview.takeWidget();
        }
        for (auto it=originalHashes.cbegin(); it!=originalHashes.cend(); ++it)
            checkBak(fileHash(it.key())==it.value(), "original backup SHA-256 remains unchanged after real UI reading");
        qInfo() << "PASS: BAK read-only real directory, exact versions, 211 incompatibility, screenshots and source SHA-256";
    }
    runBakIntegration();
    RestorePreferences::setBackupDirectory(originalSource);
    RestorePreferences::setStagingDirectory(originalStaging);
}
