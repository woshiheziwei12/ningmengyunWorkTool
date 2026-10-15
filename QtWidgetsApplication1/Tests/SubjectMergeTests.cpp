#include "Common/subjectmergeengine.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <cstdio>
#include <stdexcept>

static bool storageWarning = false;
static void check(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

static QJsonObject account(const QString &code, const QString &name, int direction = 1)
{
    return {{"AcctCode", code}, {"AcctName", name}, {"AcctDC", direction}};
}

static SubjectMergeResult mergeChecked(const QJsonObject &root, const QStringList &codes,
                                       const QString &target)
{
    SubjectMergeRequest request;
    request.sourceCodes = codes;
    request.targetCode = target;
    SubjectMergeResult result;
    QString error;
    check(SubjectMergeEngine::merge(root, request, &result, &error), error.toUtf8().constData());
    check(!storageWarning, "JSON storage limit warning during merge");
    const auto before = root.value("GLVoucherList").toArray();
    const auto after = result.root.value("GLVoucherList").toArray();
    check(before.size() == after.size(), "voucher count changed");
    int changed = 0;
    for (int i = 0; i < before.size(); ++i) {
        auto expected = before.at(i).toObject();
        const QString oldCode = expected.value("AcctCode").toString();
        const QString newCode = result.codeMapping.value(oldCode, oldCode);
        if (newCode != oldCode) { expected.insert("AcctCode", newCode); ++changed; }
        check(after.at(i).toObject() == expected, "voucher mapping or non-code fields changed incorrectly");
    }
    check(changed == result.changedVoucherCount, "voucher change count mismatch");
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (it.key() != "GLAccountList" && it.key() != "GLBalanceList" && it.key() != "GLVoucherList")
            check(result.root.value(it.key()) == it.value(), "unrelated business data changed");
    }
    QSet<QString> live;
    for (const auto &value : result.root.value("GLAccountList").toArray()) {
        const QString code = value.toObject().value("AcctCode").toString();
        check(!live.contains(code), "duplicate account code");
        live.insert(code);
    }
    for (auto it = result.codeMapping.constBegin(); it != result.codeMapping.constEnd(); ++it)
        check(live.contains(it.value()), "mapping target missing");
    return result;
}

static void syntheticSequence()
{
    QJsonArray accounts, vouchers;
    for (int i = 0; i < 25; ++i) accounts.append(account(QString::number(1000 + i), "subject"));
    const QString padding(600, 'x');
    for (int i = 0; i < 18000; ++i)
        vouchers.append(QJsonObject{{"AcctCode", QString::number(1000 + i % 25)},
            {"DebitBal", i + 0.25}, {"CreditBal", 0}, {"Summary", padding}});
    QJsonObject root{{"GLAccountList", accounts}, {"GLVoucherList", vouchers},
        {"GLBalanceList", QJsonArray()}, {"GLBalanceFullList", QJsonArray{QJsonObject{{"sentinel", 42}}}}};
    for (int i = 1; i < 25; ++i) {
        root = mergeChecked(root, {"1000", QString::number(1000 + i)}, "1000").root;
        std::printf("synthetic round %d passed\n", i);
        std::fflush(stdout);
    }
    check(root.value("GLAccountList").toArray().size() == 1, "sequence lost accounts");
    check(QJsonDocument::fromJson(QJsonDocument(root).toJson()).object() == root, "JSON export roundtrip failed");
}

static void balanceAndFailureCases()
{
    QJsonObject root{{"GLAccountList", QJsonArray{account("1000", "A"), account("2000", "B", -1)}},
        {"GLBalanceList", QJsonArray{QJsonObject{{"AcctCode", "1000"}, {"BegBal", 100}},
                                    QJsonObject{{"AcctCode", "2000"}, {"BegBal", 40}}}},
        {"GLVoucherList", QJsonArray{QJsonObject{{"AcctCode", "2000"}, {"DebitBal", 40}, {"CreditBal", 0}}}}};
    const auto result = mergeChecked(root, {"1000", "2000"}, "1000");
    check(result.root.value("GLBalanceList").toArray().size() == 1, "balance identity merge failed");
    check(result.root.value("GLBalanceList").toArray().first().toObject().value("BegBal").toDouble() == 60,
          "opposite direction opening balance incorrect");
    root.insert("GLVoucherList", QJsonArray{17});
    const auto before = root;
    SubjectMergeResult sentinel;
    sentinel.root = QJsonObject{{"untouched", true}};
    sentinel.changedVoucherCount = 123;
    SubjectMergeRequest request;
    request.sourceCodes = QStringList{"1000", "2000"}; request.targetCode = "1000";
    QString error;
    check(!SubjectMergeEngine::merge(root, request, &sentinel, &error), "malformed voucher accepted");
    check(!error.isEmpty() && root == before, "failure changed source or omitted error");
    check(sentinel.root.value("untouched").toBool() && sentinel.changedVoucherCount == 123,
          "failure exposed partial result");

    root = QJsonObject{{"GLAccountList", QJsonArray{account("1000", "leaf"), account("2000", "parent"),
                                                    account("2000.01", "child")}},
                      {"GLVoucherList", QJsonArray{QJsonObject{{"AcctCode", "1000"}, {"DebitBal", 12}}}}};
    mergeChecked(root, {"1000", "2000"}, "1000");
    mergeChecked(root, {"1000", "2000"}, "2000");
    std::puts("balance, mixed hierarchy and failure atomicity passed");
}

static void realSequence(const QString &path, const QString &outputPath)
{
    QFile file(path); check(file.open(QIODevice::ReadOnly), "cannot open input fixture");
    const auto original = QJsonDocument::fromJson(file.readAll()).object();
    mergeChecked(original, {"3141", "4104"}, "3141");
    mergeChecked(original, {"3141", "4104"}, "4104");
    QJsonObject root = original;
    const QList<QStringList> pairs = {{"1904", "1111"}, {"1123", "1918"}, {"1122", "1919"},
        {"1221", "1133"}, {"1906", "1141"}, {"1908", "1201"}, {"1405", "1243"},
        {"1407", "1244"}, {"3141", "4104"}, {"4001", "3101"}, {"4002", "3111"}, {"4202", "3121"}};
    for (const auto &pair : pairs) root = mergeChecked(root, pair, pair.first()).root;
    const auto encoded = QJsonDocument(root).toJson(QJsonDocument::Compact);
    check(QJsonDocument::fromJson(encoded).object() == root, "real fixture JSON roundtrip failed");
    if (!outputPath.isEmpty()) {
        QFile output(outputPath); check(output.open(QIODevice::WriteOnly | QIODevice::NewOnly), "cannot create verification output");
        check(output.write(encoded) == encoded.size(), "verification output write failed");
    }
    std::puts("real fixture: both profit targets and 12 consecutive merges passed");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
        if (message.contains("Document too large")) storageWarning = true;
    });
    try {
        syntheticSequence();
        balanceAndFailureCases();
        if (argc > 1) realSequence(app.arguments().at(1), argc > 2 ? app.arguments().at(2) : QString());
        std::puts("PASS SubjectMergeTests"); return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL %s\n", error.what()); return 1;
    }
}
