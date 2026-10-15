#include "Common/SqlRestoreRouting.h"
#include "Common/RestorePreferences.h"
#include "T3RestoreWidget.h"
#include "U8RestoreWidget.h"
#include <QDebug>
#include <QLabel>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QScrollArea>

static void checkRestore(bool ok, const char* message)
{ if (!ok) qFatal("RestoreCompatibility: %s", message); }

static T3ServerInfo serverInfo(const QString& version, int internal)
{ T3ServerInfo info; info.name="test"; info.version=version; info.databaseVersion=internal; return info; }
static T3BackupSet backupSet(int major, int minor, int internal)
{ T3BackupSet set; set.databaseName="UFDATA_001_2026"; set.softwareVersionMajor=major; set.softwareVersionMinor=minor; set.databaseVersion=internal; return set; }

void runRestoreCompatibilityTests()
{
    const auto old=backupSet(8,0,539), r2=backupSet(10,50,661), recent=backupSet(16,0,957);
    const auto modern=serverInfo("16.0.1000.1",957), legacy=serverInfo("10.50.4000.0",661);
    checkRestore(!SqlBackupCompatibility::error({old},modern).isEmpty(), "SQL 2000 is rejected on SQL 2022");
    checkRestore(SqlBackupCompatibility::error({old},legacy).isEmpty(), "SQL 2000 directly restores to 2008 R2");
    checkRestore(!SqlBackupCompatibility::error({r2},serverInfo("10.0.6000.1",655)).isEmpty(), "2008 R2 cannot downgrade to 2008");
    checkRestore(!SqlBackupCompatibility::error({recent},legacy).isEmpty(), "new backup cannot restore on old engine");
    checkRestore(SqlBackupCompatibility::error({backupSet(9,0,611)},modern).isEmpty(), "2005 user backups support modern engines");
    checkRestore(!SqlBackupCompatibility::error({backupSet(0,0,1234)},modern).isEmpty(), "unknown backup fails closed");
    checkRestore(SqlBackupCompatibility::summary({old,recent}).contains(QString::fromUtf8(u8"没有可同时")), "mixed incompatible years report no common version");
    checkRestore(!SqlBackupCompatibility::summary({old,r2}).contains("SQL Server 2005"), "mixed package recommendation intersects version ranges");

    T3SqlConnection current; current.server=".";
    const QString named=".\\COMPATIBLE", remote="192.168.1.211";
    const auto candidates=SqlRestoreRouting::candidates(current,{named,"other-server",named.toLower(),remote});
    checkRestore(candidates.size()==3 && candidates[1].server==named && candidates.last().server==remote,
        "only local and 211 candidates, deduplicated, current first then local");
    QMap<QString,T3ServerInfo> servers{{".",modern},{named,legacy},{remote,legacy}};
    QMap<QString,QString> paths{{".","D:/first"},{named,"E:/custom"},{remote,"//192.168.1.211/hzw/custom"}};
    QStringList probes, reads;
    bool cancel=false, unreadableHeader=false, packageError=false;
    T3BackupSet source=old;
    auto probe=[&](const T3SqlConnection& options,T3ServerInfo* info,QString*) {
        probes.append(options.server); *info=servers.value(options.server); return true;
    };
    auto reader=[&](const T3SqlConnection& options,const QString&,const QString& staging,
        const T3BackupRestorer::Cancel&,const T3BackupRestorer::Progress&,bool) {
        reads.append(options.server);
        checkRestore(staging==paths.value(options.server), "candidate receives its own saved staging path");
        T3BackupAnalysis result; result.server=servers.value(options.server);
        if (unreadableHeader && options.server==".") {result.error="unreadable media header";return result;}
        result.sets={source};result.error=SqlBackupCompatibility::error(result.sets,result.server);
        result.success=result.error.isEmpty();
        if(packageError){result.success=false;result.error="invalid package account name";}
        return result;
    };
    auto run=[&]() {
        probes.clear();reads.clear();
        return SqlRestoreRouting::read<T3BackupAnalysis>(candidates,"fixture",paths,false,reader,
            [&](){return cancel;},{},probe);
    };
    auto result=run();
    checkRestore(result.success && result.targetServer==named && !probes.contains(remote), "old backup selects compatible local without contacting 211");
    checkRestore(result.compatibilityMessage.contains("SQL Server 2000") && result.compatibilityMessage.contains(named), "show source, supported version and exact selected endpoint");
    servers["."]=legacy; result=run();
    checkRestore(result.success && result.targetServer=="." && probes.size()==1, "keep compatible current target");
    servers["."]=modern;servers[named]=modern;result=run();
    checkRestore(result.success && result.targetServer==remote, "211 can be final compatible destination, never an intermediate");
    servers[remote]=modern;result=run();
    checkRestore(!result.success && result.targetServer.isEmpty() && reads.size()==1, "no compatible target stops; skips rereading known incompatible instances");
    servers["."]=legacy;source=recent;unreadableHeader=true;result=run();
    checkRestore(result.success && result.targetServer==named, "unsupported header on old engine can be read by modern local engine");
    unreadableHeader=false;source=backupSet(0,0,1234);result=run();
    checkRestore(!result.success && result.compatibilityMessage.contains(QString::fromUtf8(u8"无法可靠识别")), "unknown metadata never selects a destination");
    source=old;packageError=true;result=run();
    checkRestore(!result.success && result.error=="invalid package account name" && probes.size()==1, "compatible package error does not trigger target changes");
    packageError=false;cancel=true;result=run();
    checkRestore(!result.success && probes.isEmpty(), "cancel stops matching before connections");

    checkRestore(SqlServer211::version().version=="10.50.4000.0" && SqlServer211::version().databaseVersion==661, "211 uses queried fixed version");
    checkRestore(LocalSqlInstances::registryEngineVersion("10.50.4000.0","10.52.4000.0")=="10.50.4000.0", "scan prefers actual engine registry version over package version");
    checkRestore(SqlBackupCompatibility::engineVersion("10.52.4000.0")==1050, "fallback package version identifies 2008 R2 family");
    LocalSqlInstances::cacheVersion(named,"10.50.4000.0");
    checkRestore(LocalSqlInstances::cachedVersion(named.toLower())=="10.50.4000.0", "scanned versions persist case-insensitively");
    const auto cache=SqlRestoreRouting::cachedVersions(candidates);
    checkRestore(cache.value(named).version=="10.50.4000.0" && cache.value(remote).databaseVersion==661, "matching snapshot contains local cache and fixed 211");
    cancel=false;source=recent;servers["."]=legacy;servers[named]=legacy;servers[remote]=legacy;
    probes.clear();reads.clear();
    QMap<QString,T3ServerInfo> cached{{".",legacy},{named,legacy},{remote,legacy}};
    result=SqlRestoreRouting::read<T3BackupAnalysis>(candidates,"fixture",paths,false,reader,{}, {},probe,cached);
    checkRestore(!result.success && probes==QStringList({"."}), "known incompatible cached instances skipped without connections");
    source=old;servers["."]=modern;servers[named]=modern;servers[remote]=legacy;
    cached["."]=modern;cached[named]=modern;
    probes.clear();reads.clear();
    result=SqlRestoreRouting::read<T3BackupAnalysis>(candidates,"fixture",paths,false,reader,{}, {},probe,cached);
    checkRestore(result.success && result.targetServer==remote && !probes.contains(remote), "fixed 211 version used without version-probe round trip");

    T3BackupAnalysis prepared;prepared.success=true;prepared.sets={old};
    prepared.preparedPath="D:/shared/existing.bak";prepared.targetServer=".";prepared.preparedServer=".";
    prepared.stagingFile=std::make_shared<QString>(prepared.preparedPath);
    T3SqlConnection next;next.server=named;servers[named]=legacy;
    int fileChecks=0;
    auto exists=[&](const T3SqlConnection&,const QString& path,QString*) {
        ++fileChecks;checkRestore(path==prepared.preparedPath,"retarget reuses existing file");return true;
    };
    const int readCount=reads.size();
    auto switched=SqlRestoreRouting::validateTarget(prepared,next,{},probe,exists);
    checkRestore(switched.success && switched.targetServer==named && switched.stagingFile==prepared.stagingFile
        && switched.preparedServer=="." && reads.size()==readCount && fileChecks==1,"retarget reuses metadata and ownership without invoking package reader");
    servers[named]=modern;
    switched=SqlRestoreRouting::validateTarget(switched,next,{},probe,exists);
    checkRestore(!switched.success && switched.sets.size()==1 && switched.stagingFile==prepared.stagingFile && fileChecks==1,
        "incompatible target preserves backup for another selection and skips file check");
    servers[named]=legacy;
    switched=SqlRestoreRouting::validateTarget(switched,next,{},probe,exists);
    checkRestore(switched.success,"switching back restores readiness without rereading");
    switched=SqlRestoreRouting::validateTarget(switched,next,[]{return true;},probe,exists);
    checkRestore(!switched.success && switched.stagingFile==prepared.stagingFile,"cancelling target validation retains prepared backup");
    next.server=remote;
    switched=SqlRestoreRouting::validateTarget(prepared,next,{},probe,exists);
    checkRestore(!switched.success && fileChecks==2,"cross-host local path cannot refer to a different file on 211");
    prepared.preparedPath="//192.168.1.211/hzw/existing.bak";
    switched=SqlRestoreRouting::validateTarget(prepared,next,{},probe,exists);
    checkRestore(switched.success && fileChecks==3,"shared UNC file supports local to 211 switch without package reread");
    switched=SqlRestoreRouting::validateTarget(prepared,next,{},probe,[](const T3SqlConnection&,const QString&,QString* error){*error="access denied";return false;});
    checkRestore(!switched.success && switched.error=="access denied" && switched.stagingFile==prepared.stagingFile,"inaccessible staging blocks restore but preserves backup");

    QTemporaryDir temporary;
    checkRestore(temporary.isValid(),"temporary directory");
    RestorePreferences::setBackupDirectory(temporary.path()+"/backups");
    RestorePreferences::setStagingDirectory(temporary.path()+"/stage-default");
    {
        T3RestoreWidget widget;
        auto* target=widget.findChild<QComboBox*>("t3TargetServer");
        if(target->findData(".")<0)target->addItem("default",".");
        target->setCurrentIndex(target->findData("."));
        auto* staging=widget.findChild<QLineEdit*>("t3Staging");
        checkRestore(staging->text()==temporary.path()+"/stage-default", "widget loads saved local staging");
        staging->setText(temporary.path()+"/edited");
        target->setCurrentIndex(target->findData(remote));
        checkRestore(staging->text()==temporary.path()+"/edited", "211 uses the same edited staging path");
        target->setCurrentIndex(target->findData("."));
        checkRestore(staging->text()==temporary.path()+"/edited", "target switch preserves manually edited local path");
    }
    {
        T3RestoreWidget reopened;
        auto* target=reopened.findChild<QComboBox*>("t3TargetServer");
        if(target->findData(".")<0)target->addItem("default",".");
        target->setCurrentIndex(target->findData("."));
        checkRestore(reopened.findChild<QLineEdit*>("t3Staging")->text()==temporary.path()+"/edited", "recreated page persists user staging");
        checkRestore(QDir::fromNativeSeparators(RestorePreferences::backupDirectory())==temporary.path()+"/backups", "backup folder survives page recreation");
        checkRestore(reopened.findChild<QLabel*>("t3SharedInput")->text().contains("backups"), "backup folder displayed for user");
    }
    checkRestore(RestorePreferences::stagingDirectory()==temporary.path()+"/edited", "all targets share one persisted staging preference");
    {
        U8RestoreWidget otherSoftware;
        checkRestore(otherSoftware.findChild<QLineEdit*>("u8Staging")->text()==temporary.path()+"/edited", "shared staging persists across software");
    }
    QSettings settings(QSettings::defaultFormat(),QSettings::UserScope,"LemonCloud","LemonWorkTool");
    settings.remove("BackupRestore");settings.sync();
    settings.setValue("BackupRestore/staging/2e",temporary.path()+"/migrated");settings.sync();
    checkRestore(RestorePreferences::stagingDirectory()==temporary.path()+"/migrated","old default-instance preference migrates to shared setting");
    RestorePreferences::setStagingDirectory("");
    checkRestore(RestorePreferences::stagingDirectory().isEmpty(),"explicit empty shared preference persists without remigration");
    settings.remove("BackupRestore");settings.sync();
    qInfo()<<"PASS: restore version rules, compatible instance selection without relay, cancellation and persistent folders";

    // Explicit opt-in: inspect an existing backup using local instances only.
    // This test never clicks Restore and never calls a database-writing API.
    const QString sample=qEnvironmentVariable("RESTORE_COMPATIBILITY_SAMPLE");
    if (sample.isEmpty()) return;
    const auto hash=[&]() {
        QFile file(sample);checkRestore(file.open(QIODevice::ReadOnly),"open original sample");
        QCryptographicHash digest(QCryptographicHash::Sha256);checkRestore(digest.addData(&file),"hash sample");return digest.result();
    };
    const auto before=hash();
    const QString stagingRoot=qEnvironmentVariable("RESTORE_COMPATIBILITY_STAGING");
    checkRestore(!stagingRoot.isEmpty() && QDir(stagingRoot).exists(),"explicit SQL-readable staging root required");
    QTemporaryDir staging(QDir(stagingRoot).filePath("LemonVersionRead_XXXXXX"));
    checkRestore(staging.isValid(),"create isolated staging directory");
    qInfo().noquote()<<"READONLY_STAGE:"<<staging.path();
    {
        T3RestoreWidget widget;
        auto* target=widget.findChild<QComboBox*>("t3TargetServer");
        const int remoteIndex=target->findData(SqlServer211::endpoint());
        if(remoteIndex>=0)target->removeItem(remoteIndex);
        // The candidate list is restricted to the local targets displayed here.
        bool reachedRemote=false;
        QObject::connect(&widget,&T3RestoreWidget::operationLog,&widget,[&](const QString& message){
            qInfo().noquote()<<message;
            if(message.contains(SqlServer211::endpoint())){
                reachedRemote=true;widget.findChild<QPushButton*>("t3Stop")->click();
            }
        });
        RestorePreferences::setStagingDirectory(staging.path());
        widget.findChild<QLineEdit*>("t3Staging")->setText(staging.path());
        widget.findChild<QLineEdit*>("t3Source")->setText(sample);
        QEventLoop loop;bool success=false;
        QObject::connect(&widget,&T3RestoreWidget::operationFinished,&loop,[&](bool ok){success=ok;loop.quit();});
        QTimer timeout;timeout.setSingleShot(true);
        QObject::connect(&timeout,&QTimer::timeout,&loop,[&](){widget.findChild<QPushButton*>("t3Stop")->click();loop.quit();});
        timeout.start(180000);
        widget.findChild<QPushButton*>("t3Analyze")->click();loop.exec();
        checkRestore(success && !reachedRemote,"real old backup automatically matches local instance without 211");
        const QString selected=target->currentData().toString();
        checkRestore(LocalSqlInstances::isLocalEndpoint(selected) && selected!=".","UI switched from incompatible default to named local instance");
        const QString hint=widget.findChild<QLabel*>("t3Compatibility")->text();
        checkRestore(hint.contains("SQL Server 2000") && hint.contains("SQL Server 2008 R2"),"real header displays old backup version and compatible target");
        auto* table=widget.findChild<QTableWidget*>("t3BackupSets");
        checkRestore(table->rowCount()>0,"real years populated");
        const int rows=table->rowCount();
        table->item(0,0)->setCheckState(Qt::Unchecked);
        table->item(0,3)->setText("UFDATA_987_2018");
        const auto stagedFiles=QDir(staging.path()).entryList(QDir::Files|QDir::NoDotAndDotDot);
        checkRestore(stagedFiles.size()==1,"one prepared file before target switch");
        success=true;timeout.start(180000);
        target->setCurrentIndex(target->findData("."));loop.exec();
        checkRestore(!success && !widget.findChild<QPushButton*>("t3Restore")->isEnabled(),"incompatible real target disables restore");
        checkRestore(table->rowCount()==rows && table->item(0,0)->checkState()==Qt::Unchecked
            && table->item(0,3)->text()=="UFDATA_987_2018","incompatible switch retains years, checks and renamed target");
        success=false;timeout.start(180000);
        target->setCurrentIndex(target->findData(selected));loop.exec();
        if(!success){QFile detail(QCoreApplication::arguments().value(1)+"/target-check-error.txt");if(detail.open(QIODevice::WriteOnly))detail.write(widget.findChild<QLabel*>("t3Compatibility")->text().toUtf8());}
        checkRestore(success && widget.findChild<QPushButton*>("t3Restore")->isEnabled(),"switch back validates existing backup without reread");
        checkRestore(table->rowCount()==rows && table->item(0,0)->checkState()==Qt::Unchecked
            && table->item(0,3)->text()=="UFDATA_987_2018" && QDir(staging.path()).entryList(QDir::Files|QDir::NoDotAndDotDot)==stagedFiles,
            "switch back preserves user edits and original staged file");
        checkRestore(widget.findChild<QLineEdit*>("t3Staging")->text()==staging.path(),"real target switch keeps shared staging setting");
        timeout.stop();
        const QString screenshot=QCoreApplication::arguments().value(1)+"/t3-compatible-read.png";
        QFile style(":/Resource/qss/workspace.css");if(style.open(QIODevice::ReadOnly))widget.setStyleSheet(QString::fromUtf8(style.readAll()));
        QScrollArea preview;preview.setWidgetResizable(true);preview.setWidget(&widget);
        preview.setAttribute(Qt::WA_DontShowOnScreen);preview.resize(1200,1500);preview.show();
        for(int i=0;i<5;++i)QCoreApplication::processEvents();
        const bool captured=preview.grab().save(screenshot);preview.takeWidget();
        checkRestore(captured,"save real compatible target screenshot");
        qInfo().noquote()<<"PASS: real T3 header read selected"<<selected<<"without restoring databases";
    }
    checkRestore(QDir(staging.path()).entryList(QDir::Files|QDir::NoDotAndDotDot).isEmpty(),"temporary backup copies cleaned");
    checkRestore(hash()==before,"original backup SHA-256 unchanged");
    settings.remove("BackupRestore");settings.sync();
}
