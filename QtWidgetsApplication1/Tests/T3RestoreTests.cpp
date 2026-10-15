#include "Common/SqlBackupCompatibility.h"
#include "Common/LocalSqlInstances.h"
#include "Common/T3BackupRestorer.h"
#include "Common/RemoteFileTransfer.h"
#include "Common/repairpathsettings.h"
#include "T3RestoreWidget.h"
#include "ThirdParty/zlib/zlib.h"
#include <QtTest>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDragEnterEvent>
#include <QDropEvent>
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
#include <windows.h>
#include <cstdio>

static QByteArray gzip(QByteArray data)
{
    z_stream stream={};
    if(deflateInit2(&stream,6,Z_DEFLATED,31,8,Z_DEFAULT_STRATEGY)!=Z_OK)return {};
    QByteArray out(compressBound(data.size())+64,0);
    stream.next_in=reinterpret_cast<Bytef*>(data.data());stream.avail_in=data.size();
    stream.next_out=reinterpret_cast<Bytef*>(out.data());stream.avail_out=out.size();
    const int result=deflate(&stream,Z_FINISH);out.resize(stream.total_out);deflateEnd(&stream);
    return result==Z_STREAM_END?out:QByteArray();
}
static QString write(const QDir& dir,const QString& name,const QByteArray& data)
{
    const QString path=dir.filePath(name);QFile file(path);
    if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size())return {};
    return path;
}
static bool drop(QWidget* target,const QString& path,bool finish=true,Qt::DropActions actions=Qt::CopyAction)
{
    QMimeData mime;mime.setUrls({QUrl::fromLocalFile(path)});
    QDragEnterEvent enter(QPoint(3,3),actions,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(target,&enter);
    if(!finish||!enter.isAccepted())return enter.isAccepted();
    QDropEvent event(QPointF(3,3),actions,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(target,&event);return event.isAccepted()&&event.dropAction()==Qt::CopyAction;
}
class T3RestoreTests:public QObject
{
    Q_OBJECT
    QTemporaryDir m_root;
    T3SqlConnection m_options;
    QStringList m_created;
    QSqlDatabase m_db;
    QString m_prefix;
    QStringList m_restoreDatabases;
    bool m_ownsControl=false;
    bool execute(const QString& sql)
    {
        QSqlQuery query(m_db);
        if(!query.exec(sql))return false;
        do{while(query.next()) {}}while(query.nextResult());
        return !query.lastError().isValid();
    }
private slots:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,m_root.path());
        QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,m_root.path());
        m_prefix="CodexT3_"+QUuid::createUuid().toString(QUuid::Id128);
        m_options.server=qEnvironmentVariable("T3_TEST_SERVER","lpc:.");
        m_options.user=qEnvironmentVariable("T3_TEST_USER");
        m_options.password=qEnvironmentVariable("T3_TEST_PASSWORD");
        m_options.windowsAuthentication=m_options.user.isEmpty();
        const auto drivers=T3BackupRestorer::availableDrivers();
        if(!drivers.isEmpty())m_options.driver=qEnvironmentVariable("T3_TEST_DRIVER",drivers.first());
    }
    void formatsAndCleanup()
    {
        QTemporaryDir work;QDir dir(work.path());QString error;
        const QByteArray content=QByteArray("TAPE")+QByteArray(190000,'x');
        const QString source=write(dir,"UFDATA.BA_",gzip(content));QVERIFY(!source.isEmpty());
        QCOMPARE(T3BackupRestorer::resolveSource(dir.path(),&error),source);
        const QString manifest=write(dir,"UF2KAct.Lst","test");
        QCOMPARE(T3BackupRestorer::resolveSource(manifest,&error),source);
        const QString decoded=T3BackupRestorer::prepareFile(source,dir.path(),&error);
        QVERIFY2(!decoded.isEmpty(),qPrintable(error));QFile file(decoded);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),content);file.close();QVERIFY(QFile::remove(decoded));
        const QString raw=write(dir,"raw.bak",content);
        const QString copied=T3BackupRestorer::prepareFile(raw,dir.path(),&error);
        QVERIFY(!copied.isEmpty());QVERIFY(copied!=raw);QVERIFY(QFile::remove(copied));
        QList<QByteArray> invalid={QByteArray("invalid"),gzip("not a backup"),gzip(content).left(20),gzip(content)+"extra"};
        QByteArray badCrc=gzip(content);badCrc[badCrc.size()-8]=char(badCrc.at(badCrc.size()-8)^1);invalid.append(badCrc);
        for(int i=0;i<invalid.size();++i)
        {
            const QString bad=write(dir,QString("bad%1").arg(i),invalid.at(i));
            QVERIFY(T3BackupRestorer::prepareFile(bad,dir.path(),&error).isEmpty());QVERIFY(!error.isEmpty());
        }
        QVERIFY(T3BackupRestorer::prepareFile(source,dir.path(),&error,[](){return true;}).isEmpty());
        QVERIFY(T3BackupRestorer::prepareFile(source,dir.path(),&error,{},1024).isEmpty());
        QVERIFY(T3BackupRestorer::prepareFile(raw,dir.path(),&error,{},1024).isEmpty());
        QCOMPARE(dir.entryList({"T3Restore_*.bak"},QDir::Files).size(),0);
    }
    void legacyRoutingAndTransfer()
    {
        T3BackupSet backup;backup.databaseVersion=539;backup.softwareVersionMajor=8;
        T3ServerInfo modern;modern.version="16.0.1000.1";modern.databaseVersion=957;
        T3ServerInfo compatible;compatible.version="10.50.4000.0";compatible.databaseVersion=661;
        QVERIFY(!SqlBackupCompatibility::error({backup},modern).isEmpty());
        QVERIFY(SqlBackupCompatibility::error({backup},compatible).isEmpty());
        QTemporaryDir dir;const QByteArray bytes=QByteArray("TAPE")+QByteArray(2*1024*1024,'z');
        const QString source=write(QDir(dir.path()),"original.bak",bytes);
        auto copy=RemoteFileTransfer::stageCopy(source,dir.path());
        QVERIFY2(copy.error.isEmpty(),qPrintable(copy.error));QVERIFY(copy.sharedPath!=source);
        QFile file(copy.sharedPath);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),bytes);file.close();
        const QString path=copy.sharedPath;copy.job.reset();QVERIFY(!QFile::exists(path));
        int count=0;copy=RemoteFileTransfer::stageCopy(source,dir.path(),[&](){return ++count>=2;});
        QVERIFY(!copy.error.isEmpty());QVERIFY(!copy.job);QVERIFY(QFile::exists(source));
        QCOMPARE(QDir(dir.path()).entryList({"LemonCopy_*.tmp"},QDir::Files).size(),0);
        copy=RemoteFileTransfer::stageCopy(source,dir.path()+"/missing");QVERIFY(!copy.error.isEmpty());
    }
    void sqlAndValidation()
    {
        T3SqlConnection options;options.windowsAuthentication=false;options.server="1.2.3.4,1433";
        options.user="u};x";options.password="p};word'";
        const QString cs=T3BackupRestorer::connectionString(options);
        QVERIFY(cs.contains("UID={u}};x};PWD={p}};word'};"));QVERIFY(cs.contains("Trusted_Connection=No;"));
        options.windowsAuthentication=true;QVERIFY(!T3BackupRestorer::connectionString(options).contains("PWD="));
        QVERIFY(T3BackupRestorer::isLocalServer("lpc:."));QVERIFY(T3BackupRestorer::isLocalServer(".\\SQLEXPRESS"));
        QVERIFY(!T3BackupRestorer::isLocalServer("192.168.1.10,1433"));
        T3RestoreSelection selection;selection.backup.position=2;selection.targetDatabase="new]db'";
        const QString sql=T3BackupRestorer::restoreSql("D:\\input'\\backup.bak",selection,{{"logical'","D",1},{"log","L",2}},"D:\\DATA","E:\\LOG","abcd");
        QVERIFY(sql.contains("RESTORE DATABASE [new]]db']"));QVERIFY(sql.contains("FILE=2"));
        QVERIFY(sql.contains("D:\\DATA\\T3_abcd_1.mdf"));QVERIFY(sql.contains("E:\\LOG\\T3_abcd_2.ldf"));
        QVERIFY(sql.contains("logical''"));QVERIFY(sql.contains("DB_ID(N'new]db''')"));QVERIFY(!sql.contains("REPLACE"));
        const QString uncSql=T3BackupRestorer::restoreSql("//server/share/backup.bak",selection,{{"f","D",1}},"D:\\DATA","D:\\DATA","unc_test");
        QVERIFY(uncSql.contains("FROM DISK=N'\\\\server\\share\\backup.bak'"));
        QVERIFY(!T3BackupRestorer::validateSelections({selection,selection}).isEmpty());
        selection.targetDatabase="master";QVERIFY(!T3BackupRestorer::validateSelections({selection}).isEmpty());
        selection.targetDatabase=" valid ";QVERIFY(!T3BackupRestorer::validateSelections({selection}).isEmpty());
        selection.targetDatabase="valid";QVERIFY(T3BackupRestorer::restoreSql("x",selection,{{"f","S",1}},"a","b","token").isEmpty());
    }
    void collectionValidation()
    {
        T3RestoreSelection item;item.backup.position=1;item.backup.databaseName="UFDATA_212_2026";
        item.targetDatabase="UFDATA_001_2026";item.accountName=QString::fromUtf8(u8"测试账套's");item.tradeKind=QString::fromUtf8(u8"小企业会计准则");
        QVERIFY(T3BackupRestorer::validateCollectionSelections({item}).isEmpty());
        const QString sql=T3BackupRestorer::registerAccountSql(item);
        QVERIFY(sql.contains("N'001'"));QVERIFY(sql.contains("账套''s"));QVERIFY(sql.contains("2026"));
        for(const QString& name:QStringList{"custom_2026","UFDATA_1_2026","UFDATA_001_2025","UFSystem"})
        {auto bad=item;bad.targetDatabase=name;QVERIFY(!T3BackupRestorer::validateCollectionSelections({bad}).isEmpty());}
        auto bad=item;bad.tradeKind.clear();QVERIFY(T3BackupRestorer::registerAccountSql(bad).isEmpty());
        bad=item;bad.accountName="  ";QVERIFY(T3BackupRestorer::registerAccountSql(bad).isEmpty());
        auto earlier=item;earlier.backup.position=2;earlier.backup.databaseName="UFDATA_212_2025";earlier.targetDatabase="UFDATA_001_2025";
        QVERIFY(T3BackupRestorer::validateCollectionSelections({item,earlier}).isEmpty());
        const QString metadataSql=T3BackupRestorer::accountMetadataSql(item);
        QVERIFY(metadataSql.contains("[UFDATA_001_2026].dbo.UA_Account"));QVERIFY(metadataSql.contains("N'212'"));
    }
    void widgetAndDrop()
    {
        QDir dir(m_root.path());const QString input=dir.filePath("shared");QDir().mkpath(input);RepairPathSettings::setInputDir(input);
        const QString source=write(dir,"drag.BA_",gzip("TAPEfixture"));
        T3RestoreWidget widget;widget.setAttribute(Qt::WA_DontShowOnScreen);widget.show();QApplication::processEvents();
        auto* auth=widget.findChild<QComboBox*>("t3TargetServer");
        QVERIFY(auth);QCOMPARE(auth->count(),LocalSqlInstances::discover().size()+1);QVERIFY(!auth->isEditable());
        QVERIFY(!widget.findChild<QLineEdit*>("t3AccountName"));QVERIFY(!widget.findChild<QComboBox*>("t3TradeKind"));
        QVERIFY(!widget.findChild<QLineEdit*>("t3Password"));QVERIFY(!widget.findChild<QLineEdit*>("t3User"));
        QCOMPARE(auth->itemData(0).toString(),LocalSqlInstances::discover().first());QVERIFY(auth->findData(QString("192.168.1.211"))>=0);
        const QString sharedStaging=widget.findChild<QLineEdit*>("t3Staging")->text();
        auth->setCurrentIndex(auth->findData(QString("192.168.1.211")));
        QCOMPARE(widget.findChild<QLineEdit*>("t3Staging")->text(),sharedStaging);auth->setCurrentIndex(0);
        QVERIFY(drop(&widget,source,false));QCOMPARE(QDir(input).entryList(QDir::Files).size(),0);
        QVERIFY(!drop(&widget,source,true,Qt::MoveAction));QVERIFY(drop(&widget,source));
        QCOMPARE(QDir(input).entryList(QDir::Files).size(),1);QVERIFY(QFile::exists(source));
        QVERIFY(drop(&widget,source));QVERIFY(QFile::exists(QDir(input).filePath("drag_2.BA_")));
        auto* sourceEdit=widget.findChild<QLineEdit*>("t3Source");QVERIFY(sourceEdit);
        QCOMPARE(QDir::fromNativeSeparators(sourceEdit->text()),QDir(input).filePath("drag_2.BA_"));
        QVERIFY(!widget.findChild<QPushButton*>("t3Restore")->isEnabled());
    }
    void configuredConnection()
    {
        if(qEnvironmentVariable("T3_TEST_SERVER").isEmpty())QSKIP("Set T3_TEST_SERVER for the configured connection check.");
        T3ServerInfo server;QString error;
        QVERIFY2(T3BackupRestorer::testConnection(m_options,&server,&error),qPrintable(error));
        QVERIFY(!server.dataDirectory.isEmpty());QVERIFY(!server.logDirectory.isEmpty());
        qInfo().noquote()<<"Connected:"<<server.name<<server.version<<m_options.driver;
    }
    void realWidgetAnalyze()
    {
        const QString source=qEnvironmentVariable("T3_TEST_SOURCE");
        if(source.isEmpty())QSKIP("Set T3_TEST_SOURCE for SQL-backed widget test.");
        T3RestoreWidget widget;
        widget.setAttribute(Qt::WA_DontShowOnScreen);widget.show();
        auto* target = widget.findChild<QComboBox*>("t3TargetServer");
        QString endpoint = m_options.server;
        if (endpoint.startsWith("lpc:", Qt::CaseInsensitive)) endpoint = endpoint.mid(4);
        const int targetIndex = target->findData(endpoint);
        QVERIFY2(targetIndex >= 0, "configured endpoint must be available in the target selector");
        target->setCurrentIndex(targetIndex);
        widget.findChild<QComboBox*>("t3Driver")->setCurrentText(m_options.driver);
        widget.findChild<QLineEdit*>("t3Staging")->setText(qEnvironmentVariable("T3_TEST_STAGING"));
        widget.findChild<QCheckBox*>("t3RemoteAgent")->setChecked(qEnvironmentVariable("T3_TEST_REMOTE_AGENT")=="1");
        auto* input=widget.findChild<QLineEdit*>("t3Source");input->setText(source);
        auto* read=widget.findChild<QPushButton*>("t3Analyze");
        auto* restore=widget.findChild<QPushButton*>("t3Restore");
        auto* table=widget.findChild<QTableWidget*>("t3BackupSets");
        QSignalSpy completed(&widget,&T3RestoreWidget::operationFinished);
        read->click();QVERIFY(!read->isEnabled());QVERIFY(!restore->isEnabled());
        // A large backup over SMB can take several minutes to prepare.
        QVERIFY(completed.wait(300000));QVERIFY(completed.first().first().toBool());
        QCOMPARE(table->rowCount(),6);QVERIFY(restore->isEnabled());
        QCOMPARE(table->item(5,3)->text(),QString("UFDATA_212_2026"));
        input->setText(source+"changed");QCOMPARE(table->rowCount(),0);QVERIFY(!restore->isEnabled());
    }
    void realBackupRestore()
    {
        const QString source=qEnvironmentVariable("T3_TEST_SOURCE");if(source.isEmpty())QSKIP("Set T3_TEST_SOURCE for real SQL restore integration test.");
        QVERIFY(!T3BackupRestorer::availableDrivers().isEmpty());
        const auto report=[](const QString& value){qInfo().noquote()<<value;};
        T3BackupAnalysis analysis=T3BackupRestorer::analyze(m_options,source,qEnvironmentVariable("T3_TEST_STAGING"), {},report,qEnvironmentVariable("T3_TEST_REMOTE_AGENT")=="1");
        QVERIFY2(analysis.success,qPrintable(analysis.error));QCOMPARE(analysis.sets.size(),6);
        QString readable=analysis.preparedPath;
        if(qEnvironmentVariable("T3_TEST_REMOTE_AGENT")=="1")readable=QDir(*analysis.stagingFile).filePath("content.bin");
        QFile decoded(readable);QVERIFY(decoded.open(QIODevice::ReadOnly));QCryptographicHash hash(QCryptographicHash::Sha256);QVERIFY(hash.addData(&decoded));
        QCOMPARE(hash.result().toHex(),QByteArray("3fca8c99596229769155c5330c66f58b3078adf74807e18f8dfdfd389a31090c"));decoded.close();
        m_db=QSqlDatabase::addDatabase("QODBC","T3TestVerify");m_db.setDatabaseName(T3BackupRestorer::connectionString(m_options));QVERIFY2(m_db.open(),qPrintable(m_db.lastError().text()));
        QSqlQuery control(m_db);QVERIFY(control.exec("SELECT DB_ID(N'UFSystem')"));QVERIFY(control.next());
        if(!control.value(0).isNull())QSKIP("Full restore test requires an instance without UFSystem; existing control databases are never touched.");
        control.finish();
        QVector<T3RestoreSelection> selected;
        for(int i=0;i<analysis.sets.size();++i)
        {
            QCOMPARE(analysis.sets.at(i).databaseName,QString("UFDATA_212_%1").arg(2021+i));
            const QString name=QString("UFDATA_987_%1").arg(2021+i);QSqlQuery exists(m_db);
            QVERIFY(exists.exec("SELECT DB_ID("+T3BackupRestorer::quoteLiteral(name)+")"));QVERIFY(exists.next());QVERIFY(exists.value(0).isNull());
            selected.append({analysis.sets.at(i),name});
            m_created.append(name);m_restoreDatabases.append(name);
        }
        const auto stopped=T3BackupRestorer::restore(m_options,analysis,selected,analysis.server.dataDirectory,analysis.server.logDirectory,[](){return true;});
        QVERIFY(stopped.stopped);QVERIFY(stopped.items.isEmpty());
        QVERIFY(control.exec("SELECT DB_ID(N'UFSystem')"));QVERIFY(control.next());QVERIFY(control.value(0).isNull());control.finish();
        m_ownsControl=true;m_created.append("UFSystem");
        const auto restored=T3BackupRestorer::restore(m_options,analysis,selected,analysis.server.dataDirectory,analysis.server.logDirectory,{},report);
        QVERIFY2(restored.error.isEmpty(),qPrintable(restored.error));QCOMPARE(restored.items.size(),6);
        for(const auto& item:restored.items)
        {
            QVERIFY2(item.success,qPrintable(item.error));QVERIFY(item.databaseRestored);QSqlQuery query(m_db);
            QVERIFY(query.exec("SELECT state_desc FROM sys.databases WHERE name="+T3BackupRestorer::quoteLiteral(item.database)));QVERIFY(query.next());QCOMPARE(query.value(0).toString(),QString("ONLINE"));query.finish();
            QVERIFY(query.exec("SELECT COUNT(*) FROM "+T3BackupRestorer::quoteIdentifier(item.database)+".sys.tables"));QVERIFY(query.next());QVERIFY(query.value(0).toInt()>100);
            qInfo().noquote()<<item.database<<"tables:"<<query.value(0).toInt();
        }
        // Exact list query used by T3StdAccountSetManager, against the new control database.
        QVERIFY(control.exec("select 'UFData_'+cAcc_Id+'_'+cast(iYear as varchar),cAcc_Name,iYear,cTradeKind from UFSystem.dbo.UA_Account"));
        QVERIFY(control.next());QCOMPARE(control.value(0).toString(),QString("UFData_987_2021"));
        const QString collectedName=control.value(1).toString(),collectedTrade=control.value(3).toString();
        QVERIFY(!collectedName.isEmpty());QVERIFY(!collectedTrade.isEmpty());
        QVERIFY(!control.next());control.finish();
        QVERIFY(control.exec(T3BackupRestorer::accountMetadataSql(selected.first(),true)));QVERIFY(control.next());
        QCOMPARE(control.value(0).toString().trimmed(),collectedName);QCOMPARE(control.value(1).toString().trimmed(),collectedTrade);control.finish();
        const auto blocked=T3BackupRestorer::restore(m_options,analysis,selected,analysis.server.dataDirectory,analysis.server.logDirectory);
        QVERIFY(!blocked.error.isEmpty());QVERIFY(blocked.items.isEmpty());qInfo().noquote()<<"Existing database protection:"<<blocked.error;
        auto mixed=selected;mixed[0].targetDatabase="UFDATA_986_2021";
        const auto blockedBatch=T3BackupRestorer::restore(m_options,analysis,mixed,analysis.server.dataDirectory,analysis.server.logDirectory);
        QVERIFY(!blockedBatch.error.isEmpty());QVERIFY(blockedBatch.items.isEmpty());
        QSqlQuery exists(m_db);QVERIFY(exists.exec("SELECT DB_ID("+T3BackupRestorer::quoteLiteral(mixed.first().targetDatabase)+")"));QVERIFY(exists.next());QVERIFY(exists.value(0).isNull());exists.finish();
        QSqlQuery check(m_db);QVERIFY2(check.exec("DBCC CHECKDB ("+T3BackupRestorer::quoteIdentifier(selected.last().targetDatabase)+") WITH NO_INFOMSGS"),qPrintable(check.lastError().text()));
        do{while(check.next()) {}}while(check.nextResult());QVERIFY2(!check.lastError().isValid(),qPrintable(check.lastError().text()));
        const QString staged=readable;analysis=T3BackupAnalysis();QVERIFY(!QFile::exists(staged));
    }
    void controlRegistration()
    {
        if(qEnvironmentVariable("T3_TEST_SERVER").isEmpty())QSKIP("Set T3_TEST_SERVER for isolated control database tests.");
        if(!m_db.isValid())
        {m_db=QSqlDatabase::addDatabase("QODBC","T3TestVerify");m_db.setDatabaseName(T3BackupRestorer::connectionString(m_options));QVERIFY(m_db.open());}
        T3ServerInfo server;QString error;QVERIFY(T3BackupRestorer::testConnection(m_options,&server,&error));
        const QString name=m_prefix+"_control";m_created.append(name);
        QVERIFY(execute(T3BackupRestorer::controlDatabaseSql(server.dataDirectory,server.logDirectory,QUuid::createUuid().toString(QUuid::Id128),name)));
        QVERIFY(execute(T3BackupRestorer::controlSchemaSql(name)));
        T3RestoreSelection item;item.backup.position=1;item.backup.databaseName="UFDATA_212_2026";
        item.targetDatabase="UFDATA_001_2026";item.accountName=QString::fromUtf8(u8"中文账套's");item.tradeKind=QString::fromUtf8(u8"企业会计准则");
        QVERIFY(execute(T3BackupRestorer::registerAccountSql(item,true,name)));
        QSqlQuery query(m_db);QVERIFY(query.exec("SELECT COUNT(*) FROM dbo.UA_Account"));QVERIFY(query.next());QCOMPARE(query.value(0).toInt(),0);query.finish();
        QVERIFY(execute(T3BackupRestorer::registerAccountSql(item,false,name)));
        QVERIFY(execute(T3BackupRestorer::registerAccountSql(item,false,name)));
        item.backup.databaseName="UFDATA_212_2024";item.targetDatabase="UFDATA_001_2024";
        QVERIFY(execute(T3BackupRestorer::registerAccountSql(item,false,name)));
        QVERIFY(query.exec("SELECT cAcc_Id,cAcc_Name,iYear,cTradeKind FROM dbo.UA_Account"));QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(),QString("001"));QCOMPARE(query.value(1).toString(),item.accountName);
        QCOMPARE(query.value(2).toInt(),2024);QCOMPARE(query.value(3).toString(),item.tradeKind);QVERIFY(!query.next());query.finish();
        auto conflict=item;conflict.accountName="conflicting name";QVERIFY(!execute(T3BackupRestorer::registerAccountSql(conflict,false,name)));
        conflict=item;conflict.tradeKind=QString::fromUtf8(u8"小企业会计准则");QVERIFY(!execute(T3BackupRestorer::registerAccountSql(conflict,false,name)));
        QVERIFY(query.exec("SELECT cAcc_Name,cTradeKind FROM dbo.UA_Account"));QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(),item.accountName);QCOMPARE(query.value(1).toString(),item.tradeKind);query.finish();
        QVERIFY(execute("ALTER TABLE dbo.UA_Account ADD extraRequired int NOT NULL DEFAULT 1 WITH VALUES"));
        QVERIFY(execute(T3BackupRestorer::controlSchemaSql(name)));
        QVERIFY(execute("ALTER TABLE dbo.UA_Account ADD missingValue int NOT NULL DEFAULT 1 WITH VALUES"));
        QVERIFY(query.exec("SELECT name FROM sys.default_constraints WHERE parent_object_id=OBJECT_ID('dbo.UA_Account') AND parent_column_id=COLUMNPROPERTY(OBJECT_ID('dbo.UA_Account'),'missingValue','ColumnId')"));
        QVERIFY(query.next());const QString constraint=query.value(0).toString();query.finish();
        QVERIFY(execute("ALTER TABLE dbo.UA_Account DROP CONSTRAINT "+T3BackupRestorer::quoteIdentifier(constraint)));
        QVERIFY(!execute(T3BackupRestorer::controlSchemaSql(name)));
        QVERIFY(execute("USE master"));
    }
    void automaticMetadataRestore()
    {
        if(qEnvironmentVariable("T3_TEST_SERVER").isEmpty() || !m_options.windowsAuthentication)
            QSKIP("Requires a local Windows-authenticated test instance without UFSystem.");
        if(!m_db.isValid())
        {m_db=QSqlDatabase::addDatabase("QODBC","T3TestVerify");m_db.setDatabaseName(T3BackupRestorer::connectionString(m_options));QVERIFY(m_db.open());}
        QSqlQuery query(m_db);QVERIFY(query.exec("SELECT DB_ID(N'UFSystem'),DB_ID(N'UFDATA_989_2026'),DB_ID(N'UFDATA_988_2026')"));QVERIFY(query.next());
        if(!query.value(0).isNull()||!query.value(1).isNull()||!query.value(2).isNull())QSKIP("Reserved fixture database already exists; leaving it untouched.");
        query.finish();
        T3ServerInfo server;QString error;QVERIFY(T3BackupRestorer::testConnection(m_options,&server,&error));
        m_restoreDatabases<<"UFDATA_989_2026"<<"UFDATA_988_2026";
        m_created<<"UFDATA_989_2026"<<"UFDATA_988_2026";
        if(!m_ownsControl){m_ownsControl=true;m_created<<"UFSystem";}
        QVERIFY(execute(T3BackupRestorer::controlDatabaseSql(server.dataDirectory,server.logDirectory,QUuid::createUuid().toString(QUuid::Id128),"UFDATA_989_2026")));
        QVERIFY(execute(T3BackupRestorer::controlSchemaSql("UFDATA_989_2026")));
        const QString accountName=QString::fromUtf8(u8"自动提取测试's"),trade=QString::fromUtf8(u8"2013年小企业会计准则");
        QVERIFY(execute("INSERT INTO dbo.UA_Account VALUES('989',"+T3BackupRestorer::quoteLiteral(accountName)+",2026,"+T3BackupRestorer::quoteLiteral(trade)+"); USE master;"));
        const QString backup=QDir(server.backupDirectory).filePath(m_prefix+".bak");
        QVERIFY(!QFile::exists(backup));
        QVERIFY(execute("BACKUP DATABASE [UFDATA_989_2026] TO DISK="+T3BackupRestorer::quoteLiteral(QDir::toNativeSeparators(backup))));
        auto analysis=T3BackupRestorer::analyze(m_options,backup,server.backupDirectory);
        QFile::remove(backup);
        QVERIFY2(analysis.success,qPrintable(analysis.error));QCOMPARE(analysis.sets.size(),1);
        T3RestoreSelection selection{analysis.sets.first(),"UFDATA_988_2026"};
        const auto result=T3BackupRestorer::restore(m_options,analysis,{selection},server.dataDirectory,server.logDirectory,{},[](const QString& text){qInfo().noquote()<<text;});
        for(const auto& item:result.items)if(!item.success)qWarning().noquote()<<item.error;
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.items.size(),1);
        QVERIFY2(result.items.first().success,qPrintable(result.items.first().error));
        QVERIFY(query.exec("SELECT cAcc_Id,cAcc_Name,iYear,cTradeKind FROM UFSystem.dbo.UA_Account"));QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(),QString("988"));QCOMPARE(query.value(1).toString(),accountName);
        QCOMPARE(query.value(2).toInt(),2026);QCOMPARE(query.value(3).toString(),trade);QVERIFY(!query.next());query.finish();
        // Source metadata remains unchanged despite renaming the restored database.
        QVERIFY(query.exec("SELECT cAcc_Id FROM UFDATA_988_2026.dbo.UA_Account"));QVERIFY(query.next());QCOMPARE(query.value(0).toString(),QString("989"));query.finish();

        // Real account packages only retain UA_Account_Ex in the FIRST backup set.
        // Selecting only a later year must still recover that first set's metadata.
        for(const auto& name:QStringList{"UFDATA_989_2025","UFDATA_986_2026"})
        {QVERIFY(query.exec("SELECT DB_ID("+T3BackupRestorer::quoteLiteral(name)+")"));QVERIFY(query.next());QVERIFY(query.value(0).isNull());query.finish();m_created<<name;m_restoreDatabases<<name;}
        QVERIFY(execute(T3BackupRestorer::controlDatabaseSql(server.dataDirectory,server.logDirectory,QUuid::createUuid().toString(QUuid::Id128),"UFDATA_989_2025")));
        QVERIFY(execute(T3BackupRestorer::controlSchemaSql("UFDATA_989_2025")));
        QVERIFY(execute("SELECT * INTO dbo.UA_Account_Ex FROM UFDATA_989_2026.dbo.UA_Account; DELETE FROM UFDATA_989_2026.dbo.UA_Account; USE master;"));
        QVERIFY(execute("BACKUP DATABASE [UFDATA_989_2025] TO DISK="+T3BackupRestorer::quoteLiteral(QDir::toNativeSeparators(backup))));
        QVERIFY(execute("BACKUP DATABASE [UFDATA_989_2026] TO DISK="+T3BackupRestorer::quoteLiteral(QDir::toNativeSeparators(backup))));
        analysis=T3BackupRestorer::analyze(m_options,backup,server.backupDirectory);QFile::remove(backup);
        QVERIFY2(analysis.success,qPrintable(analysis.error));QCOMPARE(analysis.sets.size(),2);
        const T3RestoreSelection later{analysis.sets.last(),"UFDATA_986_2026"};
        const auto laterResult=T3BackupRestorer::restore(m_options,analysis,{later},server.dataDirectory,server.logDirectory);
        QVERIFY2(laterResult.error.isEmpty(),qPrintable(laterResult.error));QCOMPARE(laterResult.items.size(),1);QVERIFY(laterResult.items.first().success);
        QVERIFY(query.exec("SELECT cAcc_Name,iYear,cTradeKind FROM UFSystem.dbo.UA_Account WHERE cAcc_Id='986'"));QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(),accountName);QCOMPARE(query.value(1).toInt(),2026);QCOMPARE(query.value(2).toString(),trade);query.finish();
        QVERIFY(query.exec("SELECT COUNT(*) FROM UFDATA_986_2026.dbo.UA_Account"));QVERIFY(query.next());QCOMPARE(query.value(0).toInt(),0);query.finish();
        QVERIFY(query.exec("SELECT COUNT(*) FROM sys.databases WHERE name LIKE 'T3Metadata[_]%'"));QVERIFY(query.next());QCOMPARE(query.value(0).toInt(),0);
    }
    void legacyDirectCompatibility()
    {
        const QString source=qEnvironmentVariable("T3_LEGACY_SOURCE");
        if(source.isEmpty())QSKIP("Set T3_LEGACY_SOURCE for read-only legacy compatibility verification.");
        const auto analysis=T3BackupRestorer::analyze(m_options,source,qEnvironmentVariable("T3_TEST_STAGING"));
        QVERIFY(!analysis.sets.isEmpty());
        for(const auto& set:analysis.sets)QCOMPARE(set.databaseVersion,539);
        QCOMPARE(analysis.success,SqlBackupCompatibility::error(analysis.sets,analysis.server).isEmpty());
        if(!analysis.success)QVERIFY(analysis.error.contains("2008"));
    }
    void cleanupTestCase()
    {
        if(m_db.isOpen())for(const auto& name:m_created)
        {
            QVERIFY(name.startsWith(m_prefix+"_")||m_restoreDatabases.contains(name)||(name=="UFSystem"&&m_ownsControl));QSqlQuery query(m_db);
            const QString quoted=T3BackupRestorer::quoteIdentifier(name);
            QVERIFY2(query.exec("USE master; IF DB_ID("+T3BackupRestorer::quoteLiteral(name)+") IS NOT NULL BEGIN ALTER DATABASE "+quoted+" SET SINGLE_USER WITH ROLLBACK IMMEDIATE; DROP DATABASE "+quoted+"; END"),qPrintable(query.lastError().text()));
            do{while(query.next()) {}}while(query.nextResult());QVERIFY(!query.lastError().isValid());
            qInfo().noquote()<<"Removed test database"<<name;
        }
        if(m_db.isValid()){m_db.close();m_db=QSqlDatabase();QSqlDatabase::removeDatabase("T3TestVerify");}
    }
};
int main(int argc,char** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    qputenv("QTEST_FUNCTION_TIMEOUT","1800000");
    QApplication app(argc,argv);T3RestoreTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "T3RestoreTests.moc"
