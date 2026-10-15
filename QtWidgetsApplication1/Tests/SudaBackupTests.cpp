#include "Common/SudaBackupUnpacker.h"
#include "SudaBackupWidget.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <stdexcept>
#include <windows.h>

static void check(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
    std::printf("PASS %s\n", message);
}
static QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("read failed");
    return file.readAll();
}
static void writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        throw std::runtime_error("fixture write failed");
}
static QByteArray packed(const QByteArray& header, const QByteArray& content)
{
    QByteArray data = qCompress(content).mid(4);
    quint32 seed = 0xBC2FFu;
    for (int index = 0; index < data.size(); ++index)
    {
        seed = seed * 0x08088405u + 1u;
        const auto delta = (quint64(seed) * quint64(index % 16384 + 1)) >> 32;
        data[index] = static_cast<char>(static_cast<unsigned char>(data.at(index)) + delta);
    }
    return header.left(181) + data;
}
static QByteArray digest(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("digest open failed");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) throw std::runtime_error("digest read failed");
    return hash.result();
}

int main(int argc, char** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    QApplication app(argc, argv);
    try
    {
        const auto args = app.arguments();
        if (args.size() != 4) throw std::runtime_error("usage: tests source reference screenshot.png");
        const QString source = args.at(1), reference = args.at(2);
        QTemporaryDir temporary;
        check(temporary.isValid(), "temporary workspace");
        const QDir dir(temporary.path());
        const auto originalHash = digest(source);
        const QString actual = dir.filePath(QStringLiteral("真实样本.bak"));
        auto result = SudaBackupUnpacker::unpack(source.toStdWString(), actual.toStdWString());
        check(result.success && result.outputBytes == 112150016ULL, "real sample unpack");
        check(digest(actual) == digest(reference), "byte content matches Xiaochang SHA-256");
        check(!SudaBackupUnpacker::unpack(source.toStdWString(),actual.toStdWString()).success
            && digest(actual) == digest(reference), "refuse overwrite");
        check(!SudaBackupUnpacker::unpack(source.toStdWString(),source.toStdWString()).success,
            "refuse identical source and target");
        const auto original = readFile(source);
        const auto failure = [&](const QByteArray& data, const char* name, quint64 limit = 16ULL*1024*1024*1024) {
            const QString input = dir.filePath(QStringLiteral("invalid.input"));
            const QString output = dir.filePath(QStringLiteral("invalid.output"));
            writeFile(input,data);
            const auto failed = SudaBackupUnpacker::unpack(input.toStdWString(),output.toStdWString(),limit);
            check(!failed.success && !QFile::exists(output),name);
            check(dir.entryList(QStringList() << QStringLiteral("*.unpacking-*"),QDir::Files).isEmpty(),"temporary output cleaned");
        };
        failure(original.left(30),"truncated header");
        auto invalid = original; invalid[0] = 0;
        failure(invalid,"wrong signature");
        invalid = original; invalid[33] = 17;
        failure(invalid,"invalid header string length");
        failure(original.left(original.size()-5),"truncated payload");
        invalid = original; invalid[9000] = static_cast<char>(invalid.at(9000)^128);
        failure(invalid,"corrupted payload");
        failure(original+"trailing","trailing bytes");
        failure(original,"output limit",100);
        failure(packed(original,"SQLite format 3\0"),"reject non SQL Server content");
        failure(packed(original,QByteArray()),"reject empty content");
        const QString cancelled = dir.filePath(QStringLiteral("cancelled.bak"));
        check(!SudaBackupUnpacker::unpack(source.toStdWString(),cancelled.toStdWString(),
            16ULL*1024*1024*1024,[](){return true;}).success && !QFile::exists(cancelled),"cancel core operation");
        for (const int size : {16380,16384,16385,400000})
        {
            QByteArray content("TAPE");
            quint32 state=42;
            for(int i=0;i<size;++i){state=state*1664525u+1013904223u;content.append(static_cast<char>(state>>24));}
            const QString input=dir.filePath(QStringLiteral("boundary%1.input").arg(size));
            const QString output=dir.filePath(QStringLiteral("boundary%1.bak").arg(size));
            writeFile(input,packed(original,content));
            check(SudaBackupUnpacker::unpack(input.toStdWString(),output.toStdWString()).success
                && readFile(output)==content,"compressed block boundary");
        }
        SudaBackupWidget page;
        page.resize(1000,620);
        auto* sourceEdit=page.findChild<QLineEdit*>(QStringLiteral("sudaSourcePath"));
        auto* outputEdit=page.findChild<QLineEdit*>(QStringLiteral("sudaOutputPath"));
        auto* start=page.findChild<QPushButton*>(QStringLiteral("sudaStartButton"));
        auto* cancel=page.findChild<QPushButton*>(QStringLiteral("sudaCancelButton"));
        check(sourceEdit && outputEdit && start && cancel,"widget controls exist");
        sourceEdit->setText(source);
        check(outputEdit->text().endsWith(QStringLiteral("_解包.bak")),"suggest output name");
        const QString guiOutput=dir.filePath(QStringLiteral("界面验证"));
        outputEdit->setText(guiOutput);
        bool finished=false,success=false;
        QObject::connect(&page,&SudaBackupWidget::unpackFinished,&page,[&](bool ok,const QString&){finished=true;success=ok;});
        start->click();
        check(!start->isEnabled() && cancel->isEnabled(),"widget busy state");
        QElapsedTimer timer;timer.start();
        while(!finished && timer.elapsed()<30000){app.processEvents();QThread::msleep(5);}
        check(finished && success && start->isEnabled() && !cancel->isEnabled(),"widget completion state");
        check(digest(guiOutput+QStringLiteral(".bak"))==digest(reference),"widget writes matching bak and appends suffix");
        page.grab().save(args.at(3));
        finished=false;success=true;
        outputEdit->setText(dir.filePath(QStringLiteral("取消界面.bak")));
        start->click();cancel->click();timer.restart();
        while(!finished && timer.elapsed()<30000){app.processEvents();QThread::msleep(5);}
        check(finished && !success && !QFile::exists(outputEdit->text()),"widget cancellation");
        check(digest(source)==originalHash,"source unchanged");
        check(dir.entryList(QStringList() << QStringLiteral("*.unpacking-*"),QDir::Files).isEmpty(),"no abandoned partial files");
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    catch(const std::exception& error)
    {
        std::fprintf(stderr,"FAIL %s\n",error.what());
        return 1;
    }
}
