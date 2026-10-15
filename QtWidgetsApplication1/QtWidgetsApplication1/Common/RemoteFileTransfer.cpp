#include "RemoteFileTransfer.h"
#include "../ThirdParty/zlib/zlib.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSysInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QThread>
#include <QUuid>
#include <windows.h>

RemoteTransferredFile RemoteFileTransfer::stageCopy(const QString& source, const QString& directory,
    const Cancel& cancel, const Progress& progress)
{
    RemoteTransferredFile result;
    QFile input(source);
    QTemporaryFile output(QDir(directory).filePath(QStringLiteral("LemonCopy_XXXXXX.tmp")));
    if(!input.open(QIODevice::ReadOnly) || !output.open())
    {result.error=QString::fromUtf8(u8"无法打开传输源文件或创建暂存文件：%1 / %2").arg(input.errorString(),output.errorString());return result;}
    const qint64 expected=input.size();qint64 copied=0;int lastPercent=-1;
    QCryptographicHash sourceHash(QCryptographicHash::Sha256),targetHash(QCryptographicHash::Sha256);
    while(!input.atEnd())
    {
        if(cancel&&cancel()){result.error=QString::fromUtf8(u8"已取消文件传输。");return result;}
        const auto bytes=input.read(1024*1024);
        if(bytes.isEmpty() || output.write(bytes)!=bytes.size())
        {result.error=QString::fromUtf8(u8"读取或写入中转文件失败。");return result;}
        sourceHash.addData(bytes);copied+=bytes.size();
        const int percent=expected>0?int(copied*100/expected):100;
        if(progress&&(lastPercent<0||percent/10!=lastPercent/10)){progress(QString::fromUtf8(u8"传输中转文件：%1%").arg(percent));lastPercent=percent;}
    }
    if(input.error()!=QFile::NoError || copied!=expected || !output.flush() || !output.seek(0))
    {result.error=QString::fromUtf8(u8"中转文件未完整写入。");return result;}
    if(progress)progress(QString::fromUtf8(u8"核对中转文件 SHA-256..."));
    while(!output.atEnd())
    {
        if(cancel&&cancel()){result.error=QString::fromUtf8(u8"已取消文件校验。");return result;}
        const auto bytes=output.read(1024*1024);
        if(bytes.isEmpty()){result.error=QString::fromUtf8(u8"读取中转文件校验失败。");return result;}
        targetHash.addData(bytes);
    }
    if(output.error()!=QFile::NoError || sourceHash.result()!=targetHash.result())
    {result.error=QString::fromUtf8(u8"中转文件 SHA-256 不一致。");return result;}
    result.sharedPath=output.fileName();result.serverPath=QDir::toNativeSeparators(result.sharedPath);
    result.job=std::shared_ptr<QString>(new QString(result.sharedPath),[](QString* path){QFile::remove(*path);delete path;});
    output.setAutoRemove(false);
    return result;
}

namespace {
const qint64 inputLimit = 8LL * 1024 * 1024 * 1024;
const char* queueName = ".lemon-transfer";
bool stopped(const RemoteFileTransfer::Cancel& cancel) { return cancel && cancel(); }
void report(const RemoteFileTransfer::Progress& progress, const QString& text) { if (progress) progress(text); }
QString at(const QString& root, const QString& name) { return QDir(root).filePath(name); }
bool reparse(const QString& path)
{
    const auto attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()));
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}
QJsonObject readJson(const QString& path)
{
    // Qt 5's QFile omits FILE_SHARE_DELETE on Windows. Allow atomic metadata
    // replacement while a client is reading; otherwise an SMB reader stalls writers.
    const QString native=QDir::toNativeSeparators(path);
    HANDLE file=CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return {};
    LARGE_INTEGER size={};
    if(!GetFileSizeEx(file,&size)||size.QuadPart<0||size.QuadPart>65536){CloseHandle(file);return {};}
    QByteArray bytes(int(size.QuadPart),0);DWORD count=0;
    const bool ok=ReadFile(file,bytes.data(),DWORD(bytes.size()),&count,nullptr)!=FALSE;
    CloseHandle(file);if(!ok||count!=DWORD(bytes.size()))return {};
    return QJsonDocument::fromJson(bytes).object();
}
bool writeJson(const QString& path, const QJsonObject& value)
{
    const QByteArray bytes=QJsonDocument(value).toJson(QJsonDocument::Compact);
    for(int attempt=0;attempt<5;++attempt) {
        { QSaveFile file(path);
          if(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit())return true; }
        if(attempt<4)QThread::msleep(25);
    }
    return false;
}
bool validId(const QString& id)
{
    return QRegularExpression(QStringLiteral("^[a-f0-9]{32}$")).match(id).hasMatch();
}
bool safeJob(const QString& job)
{
    QFileInfo info(job);
    return validId(info.fileName()) && info.dir().dirName() == "jobs" && !reparse(job);
}
void removeJob(const QString& job)
{
    if (!safeJob(job)) return;
    const auto files = QDir(job).entryInfoList(QDir::Files | QDir::Hidden | QDir::System);
    const QStringList known = {"request.json", "status.json", "payload.gz", "content.bin", "cancel.json", "release.json"};
    for (const auto& file : files) {
        if (!reparse(file.filePath()) && (known.contains(file.fileName())
            || (file.fileName().startsWith("LemonTransfer_") && file.suffix() == "tmp")))
            QFile::remove(file.filePath());
    }
    QDir().rmdir(job); // Never recurse into unexpected files or directories.
}
bool validAgent(const QJsonObject& agent)
{
    return agent.value("protocol").toInt()==1 && agent.value("running").toBool()
        && validId(agent.value("id").toString());
}
QJsonObject liveAgent(const QString& queue,const RemoteFileTransfer::Cancel& cancel)
{
    const auto before=readJson(at(queue,"agent.json"));
    if(!validAgent(before))return {};
    QElapsedTimer timer;timer.start();
    while(timer.elapsed()<5000&&!stopped(cancel)) {
        QThread::msleep(200);const auto current=readJson(at(queue,"agent.json"));
        if(validAgent(current)&&current.value("id")==before.value("id")&&current.value("updated")!=before.value("updated"))return current;
    }
    return {};
}
bool gzipFile(const QString& source, const QString& destination, const RemoteFileTransfer::Cancel& cancel, QString* error)
{
    QFile input(source); QSaveFile output(destination);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
        *error = QString::fromUtf8(u8"无法打开本地压缩输入或临时输出。"); return false;
    }
    struct Stream { z_stream value = {}; bool ready=false; ~Stream(){if(ready)deflateEnd(&value);} } stream;
    if (deflateInit2(&stream.value, 1, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        *error=QString::fromUtf8(u8"无法初始化 GZIP 压缩。"); return false;
    }
    stream.ready=true; QByteArray buffer(1024*1024, 0);
    while (true) {
        if (stopped(cancel)) { *error=QString::fromUtf8(u8"已取消。"); return false; }
        QByteArray data=input.read(1024*1024);
        if (input.error()!=QFile::NoError) { *error=input.errorString(); return false; }
        const bool end=input.atEnd();
        stream.value.next_in=reinterpret_cast<Bytef*>(data.data()); stream.value.avail_in=data.size();
        int code=Z_OK;
        do {
            stream.value.next_out=reinterpret_cast<Bytef*>(buffer.data());stream.value.avail_out=buffer.size();
            code=deflate(&stream.value,end?Z_FINISH:Z_NO_FLUSH);
            if (code!=Z_OK && code!=Z_STREAM_END) { *error=QString::fromUtf8(u8"本地压缩失败。");return false; }
            const int count=buffer.size()-stream.value.avail_out;
            if(output.write(buffer.constData(),count)!=count){*error=output.errorString();return false;}
        } while(stream.value.avail_in || !stream.value.avail_out || (end && code!=Z_STREAM_END));
        if(end)break;
    }
    if (!output.commit()) {*error=output.errorString();return false;}
    return true;
}

bool inflateFile(const QString& source, const QString& destination,
    const RemoteFileTransfer::Cancel& cancel, QString* error)
{
    QFile input(source);QSaveFile output(destination);
    if(QFile::exists(destination)||!input.open(QIODevice::ReadOnly)||!output.open(QIODevice::WriteOnly)) {
        *error=QString::fromUtf8(u8"解压输入不可读或结果文件已存在/不可写。");return false;
    }
    struct Stream { z_stream value={};bool ready=false;~Stream(){if(ready)inflateEnd(&value);} } stream;
    if(inflateInit2(&stream.value,31)!=Z_OK){*error=QString::fromUtf8(u8"初始化解压失败。");return false;}
    stream.ready=true;QByteArray buffer(1024*1024,0);quint64 total=0;bool finished=false;
    const quint64 limit=64ULL*1024*1024*1024;
    while(!input.atEnd()) {
        QByteArray data=input.read(1024*1024);
        if(data.isEmpty()){*error=QString::fromUtf8(u8"读取压缩数据失败。");return false;}
        stream.value.next_in=reinterpret_cast<Bytef*>(data.data());stream.value.avail_in=data.size();
        do {
            if(stopped(cancel)){*error=QString::fromUtf8(u8"已取消。");return false;}
            stream.value.next_out=reinterpret_cast<Bytef*>(buffer.data());stream.value.avail_out=buffer.size();
            const int code=inflate(&stream.value,Z_NO_FLUSH);
            if(code!=Z_OK&&code!=Z_STREAM_END&&code!=Z_BUF_ERROR){*error=QString::fromUtf8(u8"GZIP 损坏或 CRC 校验失败。");return false;}
            const int count=buffer.size()-stream.value.avail_out;
            if(quint64(count)>limit-total){*error=QString::fromUtf8(u8"解压内容超过 64 GiB 限制。");return false;}
            if(output.write(buffer.constData(),count)!=count){*error=output.errorString();return false;}total+=count;
            if(code==Z_STREAM_END){finished=true;break;}
            if(code==Z_BUF_ERROR&&stream.value.avail_in){*error=QString::fromUtf8(u8"解压数据异常。");return false;}
        }while(stream.value.avail_in||!stream.value.avail_out);
        if(finished)break;
    }
    if(!finished||stream.value.avail_in||!input.atEnd()||input.error()!=QFile::NoError) {
        *error=QString::fromUtf8(u8"压缩包被截断或包含拼接数据。");return false;
    }
    if(stopped(cancel)){*error=QString::fromUtf8(u8"已取消。");return false;}
    if(!output.commit()){*error=output.errorString();return false;}
    return true;
}
}

bool RemoteFileTransfer::hasLiveAgent(const QString& sharedRoot,const Cancel& cancel)
{
    return validAgent(liveAgent(at(sharedRoot,queueName),cancel));
}
void RemoteFileTransfer::release(const QString& job)
{
    if (!safeJob(job)) return;
    const QString state=readJson(at(job,"status.json")).value("state").toString();
    if (!QFile::exists(at(job,"request.json")) || state=="done" || state=="error") removeJob(job);
    else {
        writeJson(at(job,"cancel.json"),{{"cancel",true}});
        writeJson(at(job,"release.json"),{{"release",true}});
    }
}

RemoteTransferredFile RemoteFileTransfer::upload(const QString& source, const QString& sharedRoot,
    InputEncoding encoding, const Cancel& cancel, const Progress& progress)
{
    RemoteTransferredFile result;
    const QString queue=at(sharedRoot,queueName);
    const auto agent=liveAgent(queue,cancel);
    if (!validAgent(agent)) {result.error=QString::fromUtf8(u8"远端解包助手未运行或心跳已过期。请在服务器启动助手，并确认共享目录与助手目录对应。");return result;}
    const QString localRoot=agent.value("root").toString();
    if (!QRegularExpression("^[A-Za-z]:[\\\\/].+").match(localRoot).hasMatch()) {
        result.error=QString::fromUtf8(u8"助手返回的服务器本地目录无效。");return result;
    }
    QString packed=QFileInfo(source).absoluteFilePath();
    if(packed.isEmpty())return result;
    QTemporaryDir temporary; // Must outlive input when it holds the compressed temporary file.
    QFile input(packed);
    if(!input.open(QIODevice::ReadOnly)){result.error=input.errorString();return result;}
    const auto magic=input.peek(4);input.close();
    if (encoding==InputEncoding::RawFile) {
        if(!temporary.isValid()){result.error=QString::fromUtf8(u8"无法创建本地压缩临时目录。");return result;}
        report(progress,QString::fromUtf8(u8"正在本地压缩文件，减少网络传输..."));
        const QString path=at(temporary.path(),"payload.gz");
        if(!gzipFile(packed,path,cancel,&result.error))return result;
        packed=path;
    } else if (!magic.startsWith(QByteArray::fromHex("1f8b08"))) {
        result.error=QString::fromUtf8(u8"预压缩输入不是 GZIP 文件。");return result;
    }
    input.setFileName(packed);
    if(!input.open(QIODevice::ReadOnly)||input.size()>inputLimit){result.error=QString::fromUtf8(u8"无法读取压缩包，或压缩包超过 8 GiB。");return result;}
    const QString id=QUuid::createUuid().toString(QUuid::Id128);
    const QString job=at(at(queue,"jobs"),id);
    if (!QDir(at(queue,"jobs")).mkdir(id) || reparse(job)) {
        result.error=QString::fromUtf8(u8"无法在助手任务目录创建任务，请检查共享写入权限。");return result;
    }
    result.job=std::shared_ptr<QString>(new QString(job),[](QString* path){release(*path);delete path;});
    report(progress,QString::fromUtf8(u8"上传压缩包：%1 MiB...").arg(input.size()/1024.0/1024.0,0,'f',1));
    QSaveFile output(at(job,"payload.gz"));
    if(!output.open(QIODevice::WriteOnly)){result.error=output.errorString();return result;}
    QCryptographicHash hash(QCryptographicHash::Sha256);qint64 size=0;
    while(!input.atEnd()) {
        if(stopped(cancel)){result.error=QString::fromUtf8(u8"已取消。");return result;}
        const auto block=input.read(1024*1024);
        if(block.isEmpty() || output.write(block)!=block.size()){result.error=QString::fromUtf8(u8"上传压缩包失败。");return result;}
        hash.addData(block);size+=block.size();
    }
    if(!output.commit()){result.error=output.errorString();return result;}
    if(!writeJson(at(job,"request.json"),{{"protocol",1},{"agentId",agent.value("id")},{"id",id},
        {"bytes",double(size)},{"sha256",QString::fromLatin1(hash.result().toHex())}})) {
        result.error=QString::fromUtf8(u8"无法提交远端解包任务。");return result;
    }
    report(progress,QString::fromUtf8(u8"压缩包上传完成，等待服务器本地解包..."));
    QElapsedTimer timeout,lastBeat;timeout.start();lastBeat.start();
    QJsonValue lastUpdated=agent.value("updated");
    while(timeout.elapsed()<15*60*1000) {
        if(stopped(cancel)){result.error=QString::fromUtf8(u8"已取消远端解包。");return result;}
        const auto status=readJson(at(job,"status.json"));
        if(status.value("id").toString()==id && status.value("state").toString()=="done") {
            if(!QFileInfo(at(job,"content.bin")).isFile()){result.error=QString::fromUtf8(u8"助手未生成备份文件。");return result;}
            result.serverPath=QDir::toNativeSeparators(at(at(at(localRoot,queueName),"jobs"),id)+"/content.bin");
            result.sharedPath=at(job,"content.bin");
            report(progress,QString::fromUtf8(u8"远端解包完成：%1").arg(result.serverPath));return result;
        }
        if(status.value("state").toString()=="error") {result.error=status.value("error").toString();return result;}
        const auto latest=readJson(at(queue,"agent.json"));
        if(validAgent(latest)&&latest.value("id")==agent.value("id")&&latest.value("updated")!=lastUpdated) {
            lastUpdated=latest.value("updated");lastBeat.restart();
        }
        if(lastBeat.elapsed()>30000 || (!latest.isEmpty()&&(!validAgent(latest)||latest.value("id")!=agent.value("id")))) {result.error=QString::fromUtf8(u8"远端助手已停止，请检查服务器助手窗口。");return result;}
        QThread::msleep(300);
    }
    result.error=QString::fromUtf8(u8"远端解包等待超过 15 分钟，请检查助手状态。");return result;
}

bool RemoteFileTransfer::serve(const QString& localRoot, const Cancel& cancel, const Progress& progress)
{
    const QString root=QDir(localRoot).canonicalPath();
    if(root.isEmpty() || QDir(root).isRoot() || root.startsWith("//") || reparse(root)) {
        report(progress,QString::fromUtf8(u8"请选择已存在的本地专用共享目录，例如 C:\\hzw。"));return false;
    }
    const QString queue=at(root,queueName),jobs=at(queue,"jobs");
    if(reparse(queue)||!QDir().mkpath(jobs)||reparse(jobs)) {report(progress,QString::fromUtf8(u8"无法创建任务目录，或任务目录为重解析点。"));return false;}
    QLockFile lock(at(queue,"agent.lock"));
    if(!lock.tryLock(0)){report(progress,QString::fromUtf8(u8"该目录已有助手运行，或无法获取目录锁。"));return false;}
    const QString agentId=QUuid::createUuid().toString(QUuid::Id128);
    QElapsedTimer heartbeat;heartbeat.start();
    const auto beat=[&](bool running) {
        return writeJson(at(queue,"agent.json"),{{"protocol",1},{"id",agentId},{"root",QDir::toNativeSeparators(root)},
            {"host",QSysInfo::machineHostName()},{"running",running},{"updated",double(QDateTime::currentMSecsSinceEpoch())}});
    };
    if(!beat(true)){report(progress,QString::fromUtf8(u8"无法写入助手状态。"));return false;}
    report(progress,QString::fromUtf8(u8"助手已启动，监视 %1").arg(QDir::toNativeSeparators(root)));
    while(!stopped(cancel)) {
        if(heartbeat.elapsed()>1000){
            if(!beat(true))report(progress,QString::fromUtf8(u8"心跳文件暂时不可写，将继续重试。"));
            heartbeat.restart();
        }
        for(const auto& entry:QDir(jobs).entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name)) {
            if(stopped(cancel))break;
            const QString job=entry.absoluteFilePath(),id=entry.fileName();
            if(!safeJob(job))continue;
            if(QFile::exists(at(job,"release.json"))){removeJob(job);continue;}
            if(!QFile::exists(at(job,"request.json")))continue;
            const QString previous=readJson(at(job,"status.json")).value("state").toString();
            if(previous=="done"||previous=="error")continue;
            QString error;
            const auto request=readJson(at(job,"request.json"));
            const QString payload=at(job,"payload.gz");
            const qint64 expected=request.value("bytes").toVariant().toLongLong();
            if(reparse(payload)||request.value("protocol").toInt()!=1 || request.value("id").toString()!=id
                || request.value("agentId").toString()!=agentId || expected<=0 || expected>inputLimit
                || !QRegularExpression("^[a-f0-9]{64}$").match(request.value("sha256").toString()).hasMatch())
                error=QString::fromUtf8(u8"解包任务格式无效或助手已重启，请重新提交。");
            const auto abort=[&]() {
                if(heartbeat.elapsed()>1000){beat(true);heartbeat.restart();}
                return stopped(cancel)||QFile::exists(at(job,"cancel.json"));
            };
            writeJson(at(job,"status.json"),{{"id",id},{"state","working"}});
            report(progress,QString::fromUtf8(u8"处理任务 %1").arg(id));
            if(error.isEmpty()) {
                QFile file(payload);QCryptographicHash hash(QCryptographicHash::Sha256);
                if(!file.open(QIODevice::ReadOnly)||file.size()!=expected)error=QString::fromUtf8(u8"压缩包缺失或大小不符。");
                while(error.isEmpty()&&!file.atEnd()) {
                    if(abort()){error=QString::fromUtf8(u8"已取消。");break;}
                    const auto bytes=file.read(1024*1024);
                    if(bytes.isEmpty()){error=QString::fromUtf8(u8"读取压缩包失败。");break;}hash.addData(bytes);
                }
                if(error.isEmpty()&&QString::fromLatin1(hash.result().toHex())!=request.value("sha256").toString())error=QString::fromUtf8(u8"压缩包 SHA-256 校验失败。");
            }
            if(error.isEmpty()) {
                inflateFile(payload,at(job,"content.bin"),abort,&error);
            }
            writeJson(at(job,"status.json"),{{"id",id},{"state",error.isEmpty()?"done":"error"},{"error",error}});
            report(progress,error.isEmpty()?QString::fromUtf8(u8"任务完成 %1").arg(id):QString::fromUtf8(u8"任务失败 %1：%2").arg(id,error));
            if(QFile::exists(at(job,"release.json")))removeJob(job);
        }
        QThread::msleep(300);
    }
    beat(false);report(progress,QString::fromUtf8(u8"助手已停止。"));return true;
}
