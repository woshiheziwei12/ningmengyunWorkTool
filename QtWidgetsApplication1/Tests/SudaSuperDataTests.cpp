#include "Common/SudaBackupUnpacker.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>
#include <stdexcept>

static void check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
    std::printf("PASS %s\n", message);
}
static QByteArray read(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("read failed");
    return file.readAll();
}
static void write(const QString& path, const QByteArray& data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        throw std::runtime_error("write failed");
}
static QByteArray sha(const QByteArray& data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}
static void put(QByteArray& data, int offset, quint32 value, int bytes = 4)
{
    for (int i = 0; i < bytes; ++i) data[offset + i] = char(value >> (i * 8));
}
static void field(QByteArray& data, const QByteArray& value)
{
    const int offset = data.size(); data.resize(offset + 4);
    put(data, offset, value.size() + 1); data += value; data += '\0';
}
// Independent, uncompressed CAB fixture with one folder and one file.
static QByteArray superData(const QByteArray& content, const QByteArray& name = "SDBackup.bak")
{
    const int block = 44 + 16 + name.size() + 1;
    QByteArray cab(block + 8, '\0');
    cab.replace(0, 4, "MSCF"); put(cab, 8, cab.size() + content.size());
    put(cab, 16, 44); cab[24] = 3; cab[25] = 1;
    put(cab, 26, 1, 2); put(cab, 28, 1, 2);
    put(cab, 36, block); put(cab, 40, 1, 2);
    put(cab, 44, content.size()); cab.replace(60, name.size(), name);
    put(cab, block + 4, content.size(), 2); put(cab, block + 6, content.size(), 2);
    cab += content;
    QByteArray result("SuperData SQLServer Backup File"); result += char(1); result += '\0';
    field(result, "103"); field(result, "01"); field(result, "test account");
    const int offset = result.size(); result.resize(offset + 4); put(result, offset, cab.size());
    return result + cab;
}
static QByteArray legacy(const QByteArray& content)
{
    QByteArray header(181, '\0');
    header.replace(0, 33, QByteArray("\x20" "SD19960617-1998-0908-SDSOFT-ZWLQ", 33));
    QByteArray payload = qCompress(content).mid(4);
    quint32 seed = 0xBC2FF;
    for (int i = 0; i < payload.size(); ++i) {
        seed = seed * 0x08088405u + 1u;
        payload[i] = char(quint8(payload.at(i)) + ((quint64(seed) * (i % 16384 + 1)) >> 32));
    }
    return header + payload;
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        check(app.arguments().size() == 2, "usage: SudaSuperDataTests sample.SDB");
        const QString source = app.arguments().at(1);
        const auto original = read(source);
        QTemporaryDir temporary;
        check(temporary.isValid(), "temporary directory");
        QDir dir(temporary.path());
        const QString output = dir.filePath(QStringLiteral("中文 输出.bak"));
        auto result = SudaBackupUnpacker::unpack(source.toStdWString(), output.toStdWString());
        if (!result.success) std::fprintf(stderr, "unpack error: %s\n", result.error.c_str());
        check(result.success && result.outputBytes == 65228288, "real SuperData sample unpacks");
        const QByteArray expectedHash("efc522c6269b0c9765075fca851cb4df02232254983b76c74ee2b55f7a67009e");
        check(sha(read(output)) == expectedHash, "matches independent Windows expand SHA-256");
        check(result.product == "103" && result.accountCode == "01" && !result.accountName.empty(), "wrapper metadata parsed");
        check(!SudaBackupUnpacker::unpack(source.toStdWString(), output.toStdWString()).success
            && sha(read(output)) == expectedHash, "existing output preserved");
        check(!SudaBackupUnpacker::unpack(source.toStdWString(), source.toStdWString()).success, "same source and output refused");
        const QString input = dir.filePath(QStringLiteral("中文 输入.SDB"));
        const QString target = dir.filePath("result.bak");
        const auto clean = [&]() {
            check(dir.entryList(QStringList() << "*.unpacking-*", QDir::Files).isEmpty(), "no abandoned temporary files");
        };
        const auto failure = [&](const QByteArray& data, const char* message, quint64 limit = 16ULL*1024*1024*1024) {
            write(input, data);
            const auto r = SudaBackupUnpacker::unpack(input.toStdWString(), target.toStdWString(), limit);
            check(!r.success && !r.error.empty() && !QFile::exists(target), message); clean();
        };
        failure(original.left(31), "truncated wrapper");
        auto bad = original; put(bad, 33, 0xffffffffu); failure(bad, "invalid string length");
        bad = original; bad[40] = 'x'; failure(bad, "missing string terminator");
        bad = original; bad[31] = 2; failure(bad, "unknown wrapper version");
        failure(original.left(original.size()-1), "truncated CAB");
        failure(original + "extra", "trailing bytes");
        bad = original; bad[9000] = char(quint8(bad.at(9000)) ^ 128); failure(bad, "corrupted MSZIP block");
        failure(original, "output size limit", 100);
        failure(superData("not a backup"), "non TAPE content");
        failure(superData(""), "empty content");
        failure(superData("TAPEcontent", "../escaped.bak"), "unsafe member name");
        bad = superData("TAPEcontent"); const int cabOffset = bad.indexOf("MSCF");
        put(bad, cabOffset + 28, 2, 2); failure(bad, "multiple members refused");
        put(bad, cabOffset + 28, 1, 2); put(bad, cabOffset + 30, 2, 2); failure(bad, "split cabinet refused");
        for (int stop : {1, 20}) {
            int polls = 0;
            result = SudaBackupUnpacker::unpack(source.toStdWString(), target.toStdWString(),
                16ULL*1024*1024*1024, [&]() { return ++polls >= stop; });
            check(!result.success && result.error == "已取消解包。" && !QFile::exists(target), "cancellation before/during extraction"); clean();
        }
        write(input, superData("TAPEcontent"));
        result = SudaBackupUnpacker::unpack(input.toStdWString(), target.toStdWString());
        check(result.success && read(target) == "TAPEcontent", "generated stored CAB and Unicode paths");
        QFile::remove(target);
        QByteArray content("TAPE"); quint32 state=42;
        for (int i=0; i<400000; ++i) { state=state*1664525u+1013904223u; content+=char(state>>24); }
        write(input, legacy(content));
        result = SudaBackupUnpacker::unpack(input.toStdWString(), target.toStdWString());
        check(result.success && read(target) == content, "legacy format across 16 KiB boundaries");
        QFile::remove(target);
        bad = legacy(content); bad[9000] = char(quint8(bad.at(9000)) ^ 128); failure(bad, "legacy corruption still rejected");
        check(read(source) == original, "original SDB unchanged"); clean();
        std::puts("ALL TESTS PASSED"); return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "FAIL %s\n", e.what()); return 1; }
}
