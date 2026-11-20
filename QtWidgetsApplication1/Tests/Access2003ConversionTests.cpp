#include "Common/access2003converter.h"
#include "Common/logger.h"
#include "Common/Jet3Recovery.h"

#include <QApplication>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QFile>

namespace AppLogger
{
void operation(const QString &) {}
void error(const QString &) {}
}

int runRecoveryUiTest(const QString& source,const QString& outputDirectory);

// Run against disposable fixture copies under an external process timeout.
// Multiple inputs exercise resetting the password attempt limit between files.
int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QTextStream output(stdout);
    output.setCodec("UTF-8");
    const QStringList args = application.arguments();
    if (args.size() < 4)
        return 2;
    const QString expectation = args.at(1);
    const QStringList inputs = args.mid(3);
    if(expectation=="ui") return runRecoveryUiTest(inputs.first(),args[2]);
    if(expectation=="preserved") {
        QDir().mkpath(args[2]);
        const auto destination=QDir(args[2]).filePath("existing.ais");
        QFile sentinel(destination);
        if(!sentinel.open(QIODevice::WriteOnly|QIODevice::NewOnly)) return 2;
        sentinel.write("existing-data");sentinel.close();
        const auto result=recoverJet3Data(inputs.first(),destination);
        sentinel.open(QIODevice::ReadOnly);
        const bool passed=!result.success && sentinel.readAll()=="existing-data"
            && !QFileInfo::exists(destination+".recovery.json");
        output<<(passed?"PASS":"FAIL")<<" existing output preserved\n";
        return passed?0:1;
    }
    if(expectation=="timeout") {
        QDir().mkpath(args[2]);
        const auto destination=QDir(args[2]).filePath("timeout.ais");
        const int requested=qEnvironmentVariableIntValue("JET_RECOVERY_TEST_TIMEOUT_MS");
        const auto result=recoverJet3Data(inputs.first(),destination,requested>0?requested:1);
        const bool passed=!result.success && result.error.contains(QStringLiteral("超时")) && !QFileInfo::exists(destination)
            && QDir(args[2]).entryList(QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden).isEmpty();
        output << (passed?"PASS":"FAIL") << "; error=" << result.error << '\n';
        return passed?0:1;
    }
    const auto result = Access2003Converter::convertFiles(
        inputs, args.at(2), qEnvironmentVariable("ACCESS_TEST_PASSWORD"),
        expectation=="password-rejected"?Access2003ConversionMode::Access2003:Access2003ConversionMode::PreferDataRecovery);
    bool passed = result.batchStarted && result.items.size() == inputs.size();
    output << "startupError=" << result.startupError << '\n';
    for (const auto &item : result.items)
    {
        output << "success=" << item.success << "; error=" << item.errorMessage << '\n';
        if (expectation == "password-rejected")
            passed = passed && !item.success && item.errorMessage.contains(QStringLiteral("密码"));
        else if (expectation == "success")
            passed = passed && item.success;
        else if (expectation == "recovered")
            passed = passed && item.success && item.recoveredData && item.recoveredTables>0 && item.recoveredRows>0
                && QFileInfo::exists(item.recoveryReportPath) && !result.accessStarted;
        else if (expectation == "copied")
            passed = passed && item.success && item.copiedBecauseAlreadyCompatible && !result.accessStarted;
        else if (expectation == "failure")
            passed = passed && !item.success && !item.errorMessage.isEmpty();
        else
            passed = false;
        if(item.recoveredData) output << "tables=" << item.recoveredTables << "; rows=" << item.recoveredRows << "; warnings=" << item.warnings.size() << '\n';
    }
    output << (passed ? "PASS" : "FAIL") << '\n';
    return passed ? 0 : 1;
}
