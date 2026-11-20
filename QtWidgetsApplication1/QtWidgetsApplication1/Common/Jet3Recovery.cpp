#include "Jet3Recovery.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <memory>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

static QString recoveryExecutable()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath("LemonJetRecovery.exe");
}

bool isJet3RecoveryAvailable()
{
    return QFileInfo(recoveryExecutable()).isFile();
}

Jet3RecoveryResult recoverJet3Data(const QString& sourcePath,const QString& destinationPath,int timeoutMilliseconds)
{
    Jet3RecoveryResult result;
#ifndef Q_OS_WIN
    Q_UNUSED(sourcePath) Q_UNUSED(destinationPath) Q_UNUSED(timeoutMilliseconds)
    result.error=QStringLiteral("Jet3 数据恢复仅支持 Windows");
#else
    if(!isJet3RecoveryAvailable()) {
        result.error=QStringLiteral("缺少数据恢复组件 LemonJetRecovery.exe，请重新部署完整工具目录");
        return result;
    }
    const auto finalReport=destinationPath+".recovery.json";
    if(QFileInfo::exists(destinationPath) || QFileInfo::exists(finalReport)) {
        result.error=QStringLiteral("目标文件或验证报告已存在，未覆盖");
        return result;
    }
    QTemporaryDir staging(QFileInfo(destinationPath).absolutePath()+"/.jet-recovery-XXXXXX");
    if(!staging.isValid()) { result.error=QStringLiteral("无法创建恢复暂存目录");return result; }
    using Handle=std::unique_ptr<void,decltype(&CloseHandle)>;
    Handle job(CreateJobObjectW(nullptr,nullptr),CloseHandle);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!job || !SetInformationJobObject(job.get(),JobObjectExtendedLimitInformation,&limits,sizeof(limits))) {
        result.error=QStringLiteral("无法建立恢复进程保护，已停止操作");return result;
    }
    const auto staged=staging.filePath("recovered.ais");
    QProcess process;
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=CREATE_NO_WINDOW;});
    process.start(recoveryExecutable(),{sourcePath,staged});
    if(!process.waitForStarted(10000)) { result.error=QStringLiteral("无法启动数据恢复组件：")+process.errorString();return result; }
    Handle child(OpenProcess(PROCESS_SET_QUOTA|PROCESS_TERMINATE,FALSE,DWORD(process.processId())),CloseHandle);
    if(!child || !AssignProcessToJobObject(job.get(),child.get())) {
        process.kill();process.waitForFinished(5000);
        result.error=QStringLiteral("无法隔离恢复进程，已停止操作");return result;
    }
    process.write("run\n");process.closeWriteChannel();
    if(!process.waitForFinished(timeoutMilliseconds)) {
        job.reset();process.waitForFinished(5000);
        if(process.state()!=QProcess::NotRunning) { process.kill();process.waitForFinished(5000); }
        result.error=QStringLiteral("数据恢复超时，已结束本次恢复进程并清理暂存文件");return result;
    }
    const auto response=QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    if(process.exitStatus()!=QProcess::NormalExit || process.exitCode()!=0 || !response["success"].toBool() || response["protocolVersion"].toInt()!=1) {
        result.error=response["error"].toString();
        if(result.error.isEmpty()) result.error=QStringLiteral("恢复组件异常退出或未返回有效结果，未生成目标文件");
        return result;
    }
    QFile reportFile(staged+".recovery.json");
    if(!reportFile.open(QIODevice::ReadOnly)) { result.error=QStringLiteral("恢复验证报告缺失");return result; }
    auto report=QJsonDocument::fromJson(reportFile.readAll()).object();reportFile.close();
    QFile database(staged);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!database.open(QIODevice::ReadOnly) || !hash.addData(&database) ||
       QString::fromLatin1(hash.result().toHex())!=report["OutputSHA256"].toString() ||
       report["Rows"].toInt(-1)!=report["VerifiedRows"].toInt(-2) || !report["DataOnly"].toBool()) {
        result.error=QStringLiteral("恢复结果校验失败，未发布目标文件");return result;
    }
    database.close();
    report["Output"]=QFileInfo(destinationPath).absoluteFilePath();
    if(!QFile::copy(staged,destinationPath)) { result.error=QStringLiteral("无法保存恢复结果，目标可能已存在");return result; }
    QFile publishedReport(finalReport);
    const auto reportBytes=QJsonDocument(report).toJson();
    const bool createdReport=publishedReport.open(QIODevice::WriteOnly|QIODevice::NewOnly);
    if(!createdReport || publishedReport.write(reportBytes)!=reportBytes.size() || !publishedReport.flush()) {
        publishedReport.close();
        if(createdReport) QFile::remove(finalReport);
        QFile::remove(destinationPath);
        result.error=QStringLiteral("无法保存验证报告，已撤回本次恢复结果");return result;
    }
    result.success=true;
    result.tables=report["Tables"].toInt();result.rows=report["Rows"].toInt();result.indexes=report["Indexes"].toInt();
    result.reportPath=finalReport;
    for(const auto& warning:report["Warnings"].toArray()) result.warnings.append(warning.toString());
#endif
    return result;
}
