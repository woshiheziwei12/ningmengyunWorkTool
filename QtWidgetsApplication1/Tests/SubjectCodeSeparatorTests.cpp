// Include the production translation unit to exercise its private ZIP pipeline.
#include "../QtWidgetsApplication1/subjectcodeseparatorwidget.cpp"
#include <QTemporaryDir>
#include <cstdio>
#include <stdexcept>

static void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

static QByteArray readBytes(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "read fixture/output");
    return file.readAll();
}

static void writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "write fixture");
    require(file.write(bytes) == bytes.size(), "write complete fixture");
}

static QJsonObject readObject(const QString &path)
{
    return QJsonDocument::fromJson(readBytes(path)).object();
}

static void extract(const QString &zip, const QString &dir)
{
    require(QDir().mkpath(dir), "create extraction directory");
    QString error;
    require(extractJsonFilesFromZip(zip, dir, &error), "extract test ZIP");
}

static QString makeZip(const QString &dir, const QMap<QString, QByteArray> &files)
{
    require(QDir().mkpath(dir), "create fixture directory");
    for (auto it = files.begin(); it != files.end(); ++it)
        writeBytes(dir + "/" + it.key(), it.value());
    QString error;
    require(zipDirectory(dir, dir + ".zip", &error), "create fixture ZIP");
    return dir + ".zip";
}

static void verifyReferences(const QJsonValue &before, const QJsonValue &after,
                             const QMap<QString, QString> &mapping)
{
    if (before.isArray()) {
        const auto a = before.toArray(), b = after.toArray();
        require(a.size() == b.size(), "reference array count preserved");
        for (int i = 0; i < a.size(); ++i) verifyReferences(a[i], b[i], mapping);
    } else if (before.isObject()) {
        const auto a = before.toObject(), b = after.toObject();
        require(a.keys() == b.keys(), "reference object fields preserved");
        for (auto it = a.begin(); it != a.end(); ++it) {
            if (it.key() == "AcctCode" && mapping.contains(it.value().toString()))
                require(b.value(it.key()).toString() == mapping.value(it.value().toString()), "reference follows account mapping");
            else verifyReferences(it.value(), b.value(it.key()), mapping);
        }
    } else require(before == after, "non-code data preserved");
}

static void testFixtures(const QString &base)
{
    const QByteArray accounts = R"({"GLAccountList":[{"AcctCode":"1001"},{"AcctCode":"100101"}],"GLVoucherList":[{"AcctCode":"100101","Amount":12.34}],"GLBalanceList":[{"AcctCode":"100101"}]})";
    const QByteArray companion = "{\n  \"periods\": [], \"sheetContext\": {\"value\": 1234567890123456789}\n}\n";
    for (int order = 0; order < 2; ++order) {
        const QString name = order ? "z-report.json" : "a-report.json";
        const QString dir = base + QString("/mixed%1").arg(order);
        const QString zip = makeZip(dir, {{"AccountSet.json", accounts}, {name, companion}});
        const QByteArray source = readBytes(zip);
        const auto result = processSubjectCodeInput(zip, dir + "-work", dir + "-output");
        require(result.ok, "mixed account/report ZIP succeeds");
        require(result.jsonCount == 1 && result.accountCount == 2 && result.changedAccountCount == 1,
                "companions excluded from processed account counts");
        require(result.referenceUpdateCount == 2, "voucher and balance references updated");
        extract(result.outputZipPath, dir + "-check");
        require(readBytes(dir + "-check/" + name) == companion, "companion bytes preserved exactly");
        const auto output = readObject(dir + "-check/AccountSet.json");
        const auto child = output.value("GLAccountList").toArray()[1].toObject();
        require(child.value("AcctCode").toString() == "1001.01" && child.value("AcctParent").toString() == "1001",
                "child separator and parent correct");
        require(output.value("GLVoucherList").toArray()[0].toObject().value("AcctCode").toString() == "1001.01",
                "voucher code matches child");
        require(readBytes(result.mappingPath).contains("1001.01"), "mapping CSV includes changed code");
        require(readBytes(zip) == source, "source ZIP unchanged");
    }
    const QString plain = base + "/single.json";
    writeBytes(plain, accounts);
    require(processSubjectCodeInput(plain, base + "/single-work", base + "/single-out").ok,
            "standalone account JSON still succeeds");
    const QList<QByteArray> invalid = {companion, "{\"GLAccountList\":[]}",
        "{\"GLAccountList\":null}", "{\"GLAccountList\":{}}", "broken json"};
    for (int i = 0; i < invalid.size(); ++i) {
        const QString dir = base + QString("/invalid%1").arg(i);
        const QString zip = makeZip(dir, {{"input.json", invalid[i]}});
        const auto result = processSubjectCodeInput(zip, dir + "-work", dir + "-out");
        require(!result.ok && result.outputZipPath.isEmpty(), "ZIP without valid accounts fails without output");
        if (i < 4) require(result.errorMessage.contains("GLAccountList"), "missing/invalid list error identifies account data");
        if (i > 0) {
            const QString mixed = makeZip(dir + "-mixed", {{"AccountSet.json", accounts}, {"bad.json", invalid[i]}});
            require(!processSubjectCodeInput(mixed, dir + "-mixed-work", dir + "-mixed-out").ok,
                    "valid account file does not hide malformed/empty account JSON");
        }
    }
    writeBytes(plain, companion);
    require(!processSubjectCodeInput(plain, base + "/plain-work", base + "/plain-out").ok,
            "standalone companion JSON still fails");
    std::puts("PASS synthetic regression cases: mixed ZIPs, exact companion bytes, references, standalone JSON, invalid inputs");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    try {
        require(temp.isValid(), "temporary directory");
        if (app.arguments().value(1) == "--fixtures") {
            testFixtures(temp.path());
            return 0;
        }
        const QString input = app.arguments().value(1);
        require(!input.isEmpty(), "provide input ZIP path");
        const QByteArray source = readBytes(input);
        const auto result = processSubjectCodeInput(input, temp.path() + "/work", temp.path() + "/output");
        if (!result.ok) {
            std::fprintf(stderr, "FAIL actual ZIP pipeline: %s\n", result.errorMessage.toUtf8().constData());
            return 1;
        }
        require(result.jsonCount == 1 && result.accountCount > 0, "one account JSON processed");
        extract(input, temp.path() + "/before");
        extract(result.outputZipPath, temp.path() + "/after");
        const QDir beforeDir(temp.path() + "/before"), afterDir(temp.path() + "/after");
        require(beforeDir.entryList(QDir::Files) == afterDir.entryList(QDir::Files), "all input JSON files retained");
        for (const auto &name : beforeDir.entryList(QDir::Files)) {
            auto before = readObject(beforeDir.filePath(name));
            auto after = readObject(afterDir.filePath(name));
            if (!before.contains("GLAccountList")) {
                require(readBytes(beforeDir.filePath(name)) == readBytes(afterDir.filePath(name)), "original companion bytes unchanged");
                continue;
            }
            const auto a = before.take("GLAccountList").toArray();
            const auto b = after.take("GLAccountList").toArray();
            require(a.size() == b.size(), "original account count preserved");
            QMap<QString, QString> mapping;
            for (int i = 0; i < a.size(); ++i)
                mapping.insert(a[i].toObject().value("AcctCode").toString(), b[i].toObject().value("AcctCode").toString());
            verifyReferences(before, after, mapping);
        }
        require(readBytes(input) == source, "original input ZIP unchanged");
        require(QFileInfo(result.mappingPath).size() > 0, "mapping CSV generated");
        std::printf("PASS actual ZIP pipeline: accounts=%d changed=%d references=%d\n",
            result.accountCount, result.changedAccountCount, result.referenceUpdateCount);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL %s\n", e.what());
        return 1;
    }
    return 0;
}
