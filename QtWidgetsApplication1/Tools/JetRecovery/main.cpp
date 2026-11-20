#include "Jet3Reader.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <cstdio>
#include <stdexcept>
#include <windows.h>

static QByteArray hashFile(const QString& path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
        throw std::runtime_error("Cannot hash database file");
    return hash.result().toHex();
}

static void writeNewFile(const QString& path,const QByteArray& bytes)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly) || file.write(bytes)!=bytes.size())
        throw std::runtime_error("Cannot create recovery artifact");
}

static bool isJet4(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) return false;
    const auto header=file.read(24);
    return header.size()==24 && header.mid(4,15)=="Standard Jet DB" && header[20]==1;
}

int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    QTextStream output(stdout);output.setCodec("UTF-8");
    QJsonObject response{{"protocolVersion",1},{"success",false}};
    bool copiedOutput=false,copiedReport=false;
    QString destination,reportPath;
    try {
        if(app.arguments().size()!=3) throw std::runtime_error("Expected source and output arguments");
        // The GUI assigns this process to a kill-on-close Windows job before
        // sending the token. No child process can escape a conversion timeout.
        char token[8]={};
        if(!std::fgets(token,sizeof(token),stdin) || QByteArray(token).trimmed()!="run")
            throw std::runtime_error("Recovery supervisor did not authorize startup");
        const auto source=QFileInfo(app.arguments()[1]).absoluteFilePath();
        destination=QFileInfo(app.arguments()[2]).absoluteFilePath();
        reportPath=destination+".recovery.json";
        if(source.compare(destination,Qt::CaseInsensitive)==0 || QFileInfo::exists(destination) || QFileInfo::exists(reportPath))
            throw std::runtime_error("Recovery output already exists or equals source");
        // The supervisor owns the enclosing staging directory, so forced
        // termination also removes the copied source and intermediate JSON.
        QTemporaryDir temp(QFileInfo(destination).absolutePath()+"/work-XXXXXX");
        if(!temp.isValid()) throw std::runtime_error("Cannot create isolated recovery directory");
        const auto sourceHash=hashFile(source);
        const auto copiedSource=temp.filePath("source.mdb");
        if(!QFile::copy(source,copiedSource) || hashFile(copiedSource)!=sourceHash || hashFile(source)!=sourceHash)
            throw std::runtime_error("Source changed while preparing recovery copy");
        const auto snapshot=readJet3Snapshot(copiedSource);
        const auto bytes=QJsonDocument(snapshot).toJson(QJsonDocument::Compact);
        if(bytes.size()>128*1024*1024) throw std::runtime_error("Recovery snapshot exceeds 128 MiB");
        const auto jsonPath=temp.filePath("source.json");
        writeNewFile(jsonPath,bytes);
        QFile resource(":/recovery/RestoreJet4.ps1");
        if(!resource.open(QIODevice::ReadOnly)) throw std::runtime_error("Bundled recovery script is missing");
        const auto scriptPath=temp.filePath("RestoreJet4.ps1");
        writeNewFile(scriptPath,resource.readAll());
        const auto staged=temp.filePath("recovered.mdb");
        wchar_t windowsPath[MAX_PATH]={};
        if(!GetWindowsDirectoryW(windowsPath,MAX_PATH)) throw std::runtime_error("Cannot locate Windows directory");
        BOOL wow64=FALSE;
        IsWow64Process(GetCurrentProcess(),&wow64);
        const auto powershell=QString::fromWCharArray(windowsPath)+(wow64?"/SysWOW64":"/System32")+"/WindowsPowerShell/v1.0/powershell.exe";
        QProcess process;
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=CREATE_NO_WINDOW;});
        process.start(powershell,{"-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File",scriptPath,"-JsonPath",jsonPath,"-OutputPath",staged});
        if(!process.waitForStarted(10000)) throw std::runtime_error("Cannot start 32-bit Windows PowerShell");
        if(!process.waitForFinished(150000)) {
            process.kill();process.waitForFinished(5000);
            throw std::runtime_error("Jet4 recovery exceeded 150 seconds");
        }
        if(process.exitStatus()!=QProcess::NormalExit || process.exitCode()!=0) {
            const auto error=QString::fromLocal8Bit(process.readAllStandardError()).trimmed().left(2400);
            throw std::runtime_error((QStringLiteral("Jet4 写入或校验失败（需要本机 32 位 Jet/DAO 3.6）：")+error).toUtf8().constData());
        }
        QFile validation(staged+".validation.json");
        if(!validation.open(QIODevice::ReadOnly)) throw std::runtime_error("Recovery verification report is missing");
        auto report=QJsonDocument::fromJson(validation.readAll()).object();
        const int tables=snapshot["tables"].toArray().size(),rows=snapshot["rowCount"].toInt();
        if(report["Tables"].toInt(-1)!=tables || report["Rows"].toInt(-1)!=rows || report["VerifiedRows"].toInt(-1)!=rows || !isJet4(staged))
            throw std::runtime_error("Recovery verification report does not match input");
        if(hashFile(source)!=sourceHash) throw std::runtime_error("Source changed during recovery");
        report["Output"]=destination;
        report["Source"]=source;
        report["SourceSHA256"]=QString::fromLatin1(sourceHash);
        report["OutputSHA256"]=QString::fromLatin1(hashFile(staged));
        report["DataOnly"]=true;
        report["Scope"]=QStringLiteral("业务数据表及可恢复索引；不包含原工作组权限、关系约束、保存查询、窗体和宏。仅用于查询、导账，不直接替换原账套继续记账。");
        const auto stagedReport=temp.filePath("report.json");
        writeNewFile(stagedReport,QJsonDocument(report).toJson());
        if(!QFile::copy(staged,destination)) throw std::runtime_error("Cannot publish recovered database; output may already exist");
        copiedOutput=true;
        if(!QFile::copy(stagedReport,reportPath)) throw std::runtime_error("Cannot publish recovery verification report");
        copiedReport=true;
        response=QJsonObject{{"protocolVersion",1},{"success",true},{"tables",tables},{"rows",rows},
            {"indexes",report["Indexes"]},{"warnings",report["Warnings"]},{"reportPath",reportPath}};
    } catch(const std::exception& error) {
        if(copiedReport) QFile::remove(reportPath);
        if(copiedOutput) QFile::remove(destination);
        response["error"]=QString::fromUtf8(error.what());
    } catch(...) {
        if(copiedReport) QFile::remove(reportPath);
        if(copiedOutput) QFile::remove(destination);
        response["error"]=QStringLiteral("恢复组件遇到未处理错误");
    }
    output<<QJsonDocument(response).toJson(QJsonDocument::Compact)<<'\n';output.flush();
    return response["success"].toBool()?0:1;
}
