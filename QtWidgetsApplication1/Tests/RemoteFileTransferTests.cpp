#include "Common/RemoteFileTransfer.h"
#include "ThirdParty/zlib/zlib.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QUuid>
#include <atomic>
#include <thread>

static QByteArray gzip(QByteArray bytes)
{
    z_stream stream={};if(deflateInit2(&stream,1,Z_DEFLATED,31,8,Z_DEFAULT_STRATEGY)!=Z_OK)return {};
    QByteArray output(compressBound(bytes.size())+64,0);
    stream.next_in=reinterpret_cast<Bytef*>(bytes.data());stream.avail_in=bytes.size();
    stream.next_out=reinterpret_cast<Bytef*>(output.data());stream.avail_out=output.size();
    const int code=deflate(&stream,Z_FINISH);output.resize(stream.total_out);deflateEnd(&stream);
    return code==Z_STREAM_END?output:QByteArray();
}
static bool write(const QString& path,const QByteArray& bytes)
{QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();}
static QByteArray read(const QString& path)
{QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();}
struct Agent
{
    std::atomic_bool stop{false};std::atomic_bool finished{false};std::thread thread;
    explicit Agent(const QString& root):thread([this,root](){RemoteFileTransfer::serve(root,[this](){return stop.load();},{});finished.store(true);}){}
    ~Agent(){stop.store(true);thread.join();}
};
class TransferTests:public QObject
{
    Q_OBJECT
private slots:
    void rawFileAndLease()
    {
        QTemporaryDir local,remote;QVERIFY(local.isValid()&&remote.isValid());Agent agent(remote.path());
        QTRY_VERIFY(QFile::exists(remote.filePath(".lemon-transfer/agent.json")));
        const QByteArray bytes=QByteArray::fromHex("0001020304fffe")+QByteArray(2400000,'z');
        const QString source=local.filePath("arbitrary.dat");QVERIFY(write(source,bytes));
        auto result=RemoteFileTransfer::upload(source,remote.path());QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
        QCOMPARE(read(result.sharedPath),bytes);QCOMPARE(QDir::fromNativeSeparators(result.serverPath),result.sharedPath);
        const QString path=result.sharedPath;auto owner=result.job;result={};QVERIFY(QFile::exists(path));owner.reset();QVERIFY(!QFile::exists(path));
        QCOMPARE(read(source),bytes);
    }
    void precompressedAndCorruption()
    {
        QTemporaryDir local,remote;Agent agent(remote.path());QTRY_VERIFY(QFile::exists(remote.filePath(".lemon-transfer/agent.json")));
        const QByteArray bytes="not T3; any application payload";const auto compressed=gzip(bytes);
        const QString source=local.filePath("payload.gz");QVERIFY(write(source,compressed));
        auto result=RemoteFileTransfer::upload(source,remote.path(),RemoteFileTransfer::InputEncoding::Gzip);
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(read(result.sharedPath),bytes);
        QCOMPARE(read(QDir(*result.job).filePath("payload.gz")),compressed);result={};
        QByteArray corrupt=compressed;corrupt[corrupt.size()-8]=char(corrupt.at(corrupt.size()-8)^1);QVERIFY(write(source,corrupt));
        result=RemoteFileTransfer::upload(source,remote.path(),RemoteFileTransfer::InputEncoding::Gzip);
        QVERIFY(!result.error.isEmpty());QVERIFY(result.serverPath.isEmpty());const auto job=*result.job;result={};QVERIFY(!QDir(job).exists());
        QVERIFY(write(source,compressed.left(15)));
        result=RemoteFileTransfer::upload(source,remote.path(),RemoteFileTransfer::InputEncoding::Gzip);QVERIFY(!result.error.isEmpty());
    }
    void cancelSubmittedJob()
    {
        QTemporaryDir local,remote;Agent agent(remote.path());QTRY_VERIFY(QFile::exists(remote.filePath(".lemon-transfer/agent.json")));
        const QString source=local.filePath("payload");QVERIFY(write(source,QByteArray(400000,'x')));bool cancel=false;
        auto result=RemoteFileTransfer::upload(source,remote.path(),RemoteFileTransfer::InputEncoding::RawFile,[&](){return cancel;},
            [&](const QString& message){if(message.contains(QStringLiteral("等待服务器")))cancel=true;});
        QVERIFY(!result.error.isEmpty());QVERIFY(result.job);const QString job=*result.job;result={};
        QTRY_VERIFY(!QDir(job).exists());QVERIFY(QFile::exists(source));
    }
    void heartbeatSurvivesReaders()
    {
        QTemporaryDir remote;Agent agent(remote.path());
        const QString status=remote.filePath(".lemon-transfer/agent.json");QTRY_VERIFY(QFile::exists(status));
        QFile held(status);QVERIFY(held.open(QIODevice::ReadOnly));
        QTest::qWait(2600);QVERIFY2(!agent.finished.load(),"A temporary metadata reader must not stop the resident agent");
        held.close();const auto before=read(status);QTRY_VERIFY(read(status)!=before);
    }
    void staleAgentAndInvalidTask()
    {
        QTemporaryDir local,remote;const QString source=local.filePath("input");QVERIFY(write(source,"data"));
        auto result=RemoteFileTransfer::upload(source,remote.path());QVERIFY(!result.error.isEmpty());QVERIFY(!result.job);
        Agent agent(remote.path());const QString infoPath=remote.filePath(".lemon-transfer/agent.json");QTRY_VERIFY(QFile::exists(infoPath));
        const auto info=QJsonDocument::fromJson(read(infoPath)).object();const QString id=QUuid::createUuid().toString(QUuid::Id128);
        const QString job=remote.filePath(".lemon-transfer/jobs/"+id);QVERIFY(QDir().mkpath(job));
        const auto payload=gzip("text");QVERIFY(write(job+"/payload.gz",payload));
        QJsonObject request{{"protocol",1},{"id",id},{"agentId",info.value("id")},{"bytes",payload.size()},{"sha256",QString(64,'0')}};
        QVERIFY(write(job+"/request.json",QJsonDocument(request).toJson()));
        QTRY_COMPARE(QJsonDocument::fromJson(read(job+"/status.json")).object().value("state").toString(),QString("error"));
        QVERIFY(!QFile::exists(job+"/content.bin"));RemoteFileTransfer::release(job);QVERIFY(!QDir(job).exists());
        // Unexpected directories must not be traversed, and request data cannot choose output paths.
        const QString invalid=remote.filePath(".lemon-transfer/jobs/not-a-job");QVERIFY(QDir().mkpath(invalid));
        QVERIFY(write(invalid+"/request.json","{}"));QTest::qWait(500);QVERIFY(!QFile::exists(invalid+"/status.json"));
    }
};
QTEST_GUILESS_MAIN(TransferTests)
#include "RemoteFileTransferTests.moc"
