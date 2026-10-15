#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <cstdio>
#include "Common/KisProBackupRestorer.h"

// Explicit command-line runner for user-requested retained restores; never called by the GUI.
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
        const auto bytes=message.toUtf8();
        std::fprintf(stdout,"%s\n",bytes.constData()); std::fflush(stdout);
    });
    const auto args=app.arguments();
    if(args.size()!=3 && args.size()!=4) { qCritical()<<"Usage: KisProRestoreRunner <backup.bak> <staging-directory> [local-instance]";return 2; }
    T3SqlConnection options;options.server=args.size()==4 ? args[3] : QStringLiteral("lpc:.");
    if(!T3BackupRestorer::isLocalServer(options.server)){qCritical()<<"Retained runner accepts local instances only";return 2;}
    const auto drivers=KisProBackupRestorer::availableDrivers();
    if(drivers.isEmpty()) {qCritical()<<"No ODBC driver";return 2;}
    options.driver=drivers.first();
    const auto progress=[](const QString& message){qInfo().noquote()<<message;};
    const auto analysis=KisProBackupRestorer::analyze(options,args[1],args[2],{},progress);
    if(!analysis.success){qCritical().noquote()<<analysis.error;return 1;}
    QVector<T3RestoreSelection> selected;
    for(const auto& set:analysis.sets)selected.append({set,set.databaseName});
    const auto result=KisProBackupRestorer::restore(options,analysis,selected,analysis.server.dataDirectory,analysis.server.logDirectory,{},progress);
    if(!result.error.isEmpty())qCritical().noquote()<<result.error;
    bool ok=result.error.isEmpty()&&!result.stopped&&result.items.size()==selected.size();
    for(const auto& item:result.items){qInfo()<<"LOCAL_DATABASE_RETAINED"<<item.database<<"restored="<<item.databaseRestored<<"registered="<<item.success;ok&=item.success;}
    if(!ok)return 1;
    QSqlDatabase db=QSqlDatabase::addDatabase("QODBC","KisProRetainedVerify");
    db.setDatabaseName(T3BackupRestorer::connectionString(options));
    if(!db.open()){qCritical()<<db.lastError().text();return 1;}
    QSqlQuery query(db);
    for(const auto& item:selected){
        if(!query.exec("DBCC CHECKDB ("+T3BackupRestorer::quoteIdentifier(item.targetDatabase)+") WITH NO_INFOMSGS;")){qCritical()<<query.lastError().text();return 1;}
        do{while(query.next()) {}}while(query.nextResult());
        if(query.lastError().isValid()){qCritical()<<query.lastError().text();return 1;}
        if(!query.exec("SELECT FDBName,FAcctName FROM AcctCtl.dbo.t_ad_kdAccount_gl WHERE FDBName="+T3BackupRestorer::quoteLiteral(item.targetDatabase))||!query.next())return 1;
        qInfo()<<"VERIFIED_AND_RETAINED"<<query.value(0).toString()<<query.value(1).toString();query.finish();
    }
    return 0;
}
