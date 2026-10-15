#include "Common/repairinputfileutils.h"
#include "referenceaccountcoderepairwidget.h"

#include "Common/globaldata.h"
#include "Common/logger.h"
#include "Common/repairfilenameutils.h"
#include "Common/repairinputfilepickerwidget.h"
#include "Common/repairpathconfigwidget.h"
#include "Common/repairpathsettings.h"

#include <algorithm>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QMap>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QSharedPointer>
#include <QTextEdit>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
struct AccountCodeNode
{
    QString code;
    QString legacyCode;
    int parentIndex = -1;
};

struct MappingUsage
{
    int voucherCount = 0;
    int balanceCount = 0;
    int fullBalanceCount = 0;
};

struct ReferenceRepairResult
{
    bool ok = false;
    QString inputPath;
    QString outputZipPath;
    QString mappingPath;
    QString accountSetName;
    QString errorMessage;
    QStringList logs;
    int accountCount = 0;
    int inferredParentCount = 0;
    int mappingCount = 0;
    int voucherCount = 0;
    int changedVoucherCount = 0;
    int balanceCount = 0;
    int changedBalanceCount = 0;
    int fullBalanceCount = 0;
    int changedFullBalanceCount = 0;
};

bool isSupportedInputFile(const QString &filePath)
{
    const QFileInfo info(filePath);
    return info.isFile()
        && (info.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0
            || info.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0);
}

bool hasSupportedInputFile(const QMimeData *mimeData)
{
    if (!mimeData || !mimeData->hasUrls())
        return false;
    for (const QUrl &url : mimeData->urls())
    {
        if (url.isLocalFile() && isSupportedInputFile(url.toLocalFile()))
            return true;
    }
    return false;
}

QString codeFromValue(const QJsonValue &value)
{
    if (value.isString())
    {
        QString code = value.toString();
        code.remove(QChar(' '));
        code.remove(QChar('\t'));
        code.remove(QChar('\r'));
        code.remove(QChar('\n'));
        return code;
    }
    if (value.isDouble())
    {
        const double number = value.toDouble();
        const qint64 integer = static_cast<qint64>(number);
        if (qAbs(number - static_cast<double>(integer)) < 0.000001)
            return QString::number(integer);
    }
    return QString();
}

bool isAsciiDigit(const QChar ch)
{
    return ch >= QLatin1Char('0') && ch <= QLatin1Char('9');
}

QString digitsOnly(const QString &text)
{
    QString result;
    result.reserve(text.size());
    for (const QChar ch : text)
    {
        if (isAsciiDigit(ch))
            result.append(ch);
    }
    return result;
}

bool isParentPrefix(const QString &parentCode, const QString &childCode)
{
    if (parentCode.isEmpty() || childCode.size() <= parentCode.size())
        return false;
    if (!childCode.startsWith(parentCode))
        return false;

    if (parentCode.contains(QLatin1Char('.')))
        return childCode.at(parentCode.size()) == QLatin1Char('.');
    return true;
}

QString childSegment(const QString &parentCode, const QString &childCode)
{
    QString segment = childCode.mid(parentCode.size());
    while (segment.startsWith(QLatin1Char('.')))
        segment.remove(0, 1);
    return digitsOnly(segment);
}

QString toLegacySegment(const QString &canonicalSegment)
{
    if (canonicalSegment.size() > 1 && canonicalSegment.startsWith(QLatin1Char('0')))
        return canonicalSegment.mid(1);
    return canonicalSegment;
}

bool buildReferenceMapping(const QJsonArray &accountList,
                           QSet<QString> *standardCodes,
                           QMap<QString, QString> *oldToNew,
                           int *inferredParentCount,
                           QString *errorMessage)
{
    QStringList codes;
    QSet<QString> seenCodes;
    for (const QJsonValue &value : accountList)
    {
        const QString code = codeFromValue(value.toObject().value(QStringLiteral("AcctCode")));
        if (code.isEmpty() || seenCodes.contains(code))
            continue;
        seenCodes.insert(code);
        codes.append(code);
    }

    std::sort(codes.begin(), codes.end(), [](const QString &left, const QString &right) {
        return left < right;
    });

    QList<AccountCodeNode> nodes;
    for (const QString &code : codes)
    {
        AccountCodeNode node;
        node.code = code;
        nodes.append(node);
    }

    int parentCount = 0;
    for (int index = 0; index < nodes.size(); ++index)
    {
        int parentIndex = -1;
        for (int previous = index - 1; previous >= 0; --previous)
        {
            if (isParentPrefix(nodes.at(previous).code, nodes.at(index).code))
            {
                parentIndex = previous;
                break;
            }
        }

        nodes[index].parentIndex = parentIndex;
        if (parentIndex < 0)
        {
            nodes[index].legacyCode = digitsOnly(nodes.at(index).code);
            continue;
        }

        ++parentCount;
        const QString segment = childSegment(nodes.at(parentIndex).code, nodes.at(index).code);
        if (segment.isEmpty())
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("\u65e0\u6cd5\u4ece\u79d1\u76ee %1 \u63a8\u5bfc\u7236\u7ea7 %2 \u4e4b\u540e\u7684\u5b50\u7ea7\u7f16\u7801")
                    .arg(nodes.at(index).code, nodes.at(parentIndex).code);
            return false;
        }
        nodes[index].legacyCode = nodes.at(parentIndex).legacyCode + toLegacySegment(segment);
    }

    QMap<QString, QString> mapping;
    for (const AccountCodeNode &node : nodes)
    {
        if (node.legacyCode.isEmpty() || node.legacyCode == node.code)
            continue;
        if (seenCodes.contains(node.legacyCode))
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("\u63a8\u5bfc\u51fa\u7684\u65e7\u7f16\u7801 %1 \u540c\u65f6\u4e5f\u662f\u6807\u51c6\u79d1\u76ee\u7f16\u7801\uff0c\u65e0\u6cd5\u5b89\u5168\u5904\u7406")
                    .arg(node.legacyCode);
            return false;
        }
        if (mapping.contains(node.legacyCode) && mapping.value(node.legacyCode) != node.code)
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("\u63a8\u5bfc\u51fa\u7684\u65e7\u7f16\u7801 %1 \u540c\u65f6\u5bf9\u5e94 %2 \u548c %3")
                    .arg(node.legacyCode, mapping.value(node.legacyCode), node.code);
            return false;
        }
        mapping.insert(node.legacyCode, node.code);
    }

    if (standardCodes)
        *standardCodes = seenCodes;
    if (oldToNew)
        *oldToNew = mapping;
    if (inferredParentCount)
        *inferredParentCount = parentCount;
    return true;
}

bool repairReferenceArray(QJsonObject *root,
                          const QString &arrayName,
                          const QSet<QString> &standardCodes,
                          const QMap<QString, QString> &oldToNew,
                          QMap<QString, QString> *resolvedMapping,
                          QMap<QString, MappingUsage> *usage,
                          int usageType,
                          int *rowCount,
                          int *changedCount,
                          QStringList *unmatchedCodes)
{
    if (!root || !root->contains(arrayName))
        return true;
    if (!root->value(arrayName).isArray())
        return false;

    const QJsonArray source = root->value(arrayName).toArray();
    QJsonArray repaired;
    int changed = 0;
    QSet<QString> unmatched;

    for (const QJsonValue &value : source)
    {
        QJsonObject object = value.toObject();
        const QString code = codeFromValue(object.value(QStringLiteral("AcctCode")));
        if (!code.isEmpty() && !standardCodes.contains(code))
        {
            if (oldToNew.contains(code))
            {
                const QString targetCode = oldToNew.value(code);
                object.insert(QStringLiteral("AcctCode"), targetCode);
                ++changed;
                resolvedMapping->insert(code, targetCode);
                MappingUsage item = usage->value(code);
                if (usageType == 0)
                    ++item.voucherCount;
                else if (usageType == 1)
                    ++item.balanceCount;
                else
                    ++item.fullBalanceCount;
                usage->insert(code, item);
            }
            else
            {
                unmatched.insert(code);
            }
        }
        else if (code.isEmpty())
        {
            unmatched.insert(QStringLiteral("<empty>"));
        }
        repaired.append(object);
    }

    root->insert(arrayName, repaired);
    if (rowCount)
        *rowCount = source.size();
    if (changedCount)
        *changedCount = changed;
    if (unmatchedCodes)
    {
        QStringList sorted = unmatched.values();
        std::sort(sorted.begin(), sorted.end());
        unmatchedCodes->append(sorted);
    }
    return true;
}

QString csvEscape(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

bool writeMappingFile(const QString &path,
                      const QMap<QString, QString> &oldToNew,
                      const QMap<QString, MappingUsage> &usage,
                      QString *errorMessage)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u521b\u5efa\u7f16\u7801\u6620\u5c04\u6587\u4ef6\u5931\u8d25\uff1a") + file.errorString();
        return false;
    }

    file.write("\xEF\xBB\xBF");
    file.write("OldCode,NewCode,VoucherCount,BalanceCount,FullBalanceCount\n");
    for (auto it = oldToNew.constBegin(); it != oldToNew.constEnd(); ++it)
    {
        const MappingUsage item = usage.value(it.key());
        const QString line = QStringLiteral("%1,%2,%3,%4,%5\n")
            .arg(csvEscape(it.key()),
                 csvEscape(it.value()),
                 QString::number(item.voucherCount),
                 QString::number(item.balanceCount),
                 QString::number(item.fullBalanceCount));
        file.write(line.toUtf8());
    }

    if (!file.commit())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u4fdd\u5b58\u7f16\u7801\u6620\u5c04\u6587\u4ef6\u5931\u8d25\uff1a") + file.errorString();
        return false;
    }
    return true;
}

bool processJsonFile(const QString &jsonPath,
                     ReferenceRepairResult *result,
                     QMap<QString, QString> *outputMapping,
                     QMap<QString, MappingUsage> *outputUsage,
                     QString *errorMessage)
{
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u8bfb\u53d6 AccountSet.json \u5931\u8d25\uff1a") + file.errorString();
        return false;
    }
    const QByteArray jsonData = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("AccountSet.json \u89e3\u6790\u5931\u8d25\uff1a") + parseError.errorString();
        return false;
    }

    QJsonObject root = document.object();
    if (!root.value(QStringLiteral("GLAccountList")).isArray())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u672a\u627e\u5230 GLAccountList \u79d1\u76ee\u8868");
        return false;
    }

    const QJsonArray accountList = root.value(QStringLiteral("GLAccountList")).toArray();
    if (accountList.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("GLAccountList \u79d1\u76ee\u8868\u4e3a\u7a7a");
        return false;
    }

    QSet<QString> standardCodes;
    QMap<QString, QString> oldToNew;
    int inferredParents = 0;
    if (!buildReferenceMapping(accountList, &standardCodes, &oldToNew,
                               &inferredParents, errorMessage))
    {
        return false;
    }

    QMap<QString, MappingUsage> usage;
    QMap<QString, QString> resolvedMapping;
    QStringList unmatched;
    if (!repairReferenceArray(&root, QStringLiteral("GLVoucherList"),
                              standardCodes, oldToNew, &resolvedMapping, &usage, 0,
                              &result->voucherCount, &result->changedVoucherCount,
                              &unmatched)
        || !repairReferenceArray(&root, QStringLiteral("GLBalanceList"),
                                 standardCodes, oldToNew, &resolvedMapping, &usage, 1,
                                 &result->balanceCount, &result->changedBalanceCount,
                                 &unmatched)
        || !repairReferenceArray(&root, QStringLiteral("GLBalanceFullList"),
                                 standardCodes, oldToNew, &resolvedMapping, &usage, 2,
                                 &result->fullBalanceCount, &result->changedFullBalanceCount,
                                 &unmatched))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u51ed\u8bc1\u6216\u4f59\u989d\u5b57\u6bb5\u683c\u5f0f\u9519\u8bef");
        return false;
    }

    unmatched.removeDuplicates();
    if (!unmatched.isEmpty())
    {
        std::sort(unmatched.begin(), unmatched.end());
        const int displayCount = qMin(20, unmatched.size());
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("\u6709 %1 \u4e2a\u51ed\u8bc1/\u4f59\u989d\u79d1\u76ee\u65e0\u6cd5\u5bf9\u5e94 GLAccountList\uff1a%2")
                .arg(unmatched.size())
                .arg(unmatched.mid(0, displayCount).join(QStringLiteral(", ")));
        }
        return false;
    }

    QSaveFile output(jsonPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u5199\u5165\u4fee\u590d\u540e AccountSet.json \u5931\u8d25\uff1a") + output.errorString();
        return false;
    }
    const QByteArray repairedJson = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (output.write(repairedJson) != repairedJson.size() || !output.commit())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u4fdd\u5b58\u4fee\u590d\u540e AccountSet.json \u5931\u8d25\uff1a") + output.errorString();
        return false;
    }

    result->accountSetName = root.value(QStringLiteral("AccountSet"))
        .toObject().value(QStringLiteral("AsName")).toString().trimmed();
    result->accountCount = standardCodes.size();
    result->inferredParentCount = inferredParents;
    result->mappingCount = resolvedMapping.size();
    result->logs.append(QStringLiteral("\u79d1\u76ee %1 \u4e2a\uff0c\u4ece\u7f16\u7801\u63a8\u5bfc\u7236\u5b50\u5173\u7cfb %2 \u4e2a\uff0c\u5b9e\u9645\u4f7f\u7528\u6620\u5c04 %3 \u4e2a")
        .arg(result->accountCount)
        .arg(result->inferredParentCount)
        .arg(result->mappingCount));
    result->logs.append(QStringLiteral("\u51ed\u8bc1 %1 \u6761\uff0c\u4fee\u6b63 %2 \u6761")
        .arg(result->voucherCount).arg(result->changedVoucherCount));
    result->logs.append(QStringLiteral("\u4f59\u989d %1 \u6761\uff0c\u4fee\u6b63 %2 \u6761\uff1b\u5b8c\u6574\u4f59\u989d %3 \u6761\uff0c\u4fee\u6b63 %4 \u6761")
        .arg(result->balanceCount).arg(result->changedBalanceCount)
        .arg(result->fullBalanceCount).arg(result->changedFullBalanceCount));

    if (outputMapping)
        *outputMapping = resolvedMapping;
    if (outputUsage)
        *outputUsage = usage;
    return true;
}

bool runPowerShell(const QString &script,
                   const QProcessEnvironment &environment,
                   int timeoutMs,
                   QString *errorMessage)
{
    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral("powershell.exe"),
                  QStringList() << QStringLiteral("-NoProfile")
                                << QStringLiteral("-ExecutionPolicy")
                                << QStringLiteral("Bypass")
                                << QStringLiteral("-Command")
                                << script);
    if (!process.waitForFinished(timeoutMs))
    {
        process.kill();
        process.waitForFinished(3000);
        if (errorMessage)
            *errorMessage = QStringLiteral("PowerShell \u6267\u884c\u8d85\u65f6");
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        const QString error = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (errorMessage)
            *errorMessage = error.isEmpty() ? QStringLiteral("PowerShell \u6267\u884c\u5931\u8d25") : error;
        return false;
    }
    return true;
}

bool extractAccountSetJson(const QString &zipPath,
                           const QString &targetPath,
                           QString *errorMessage)
{
    const QString script =
        QStringLiteral("$ErrorActionPreference='Stop';")
        + QStringLiteral("Add-Type -AssemblyName System.IO.Compression;")
        + QStringLiteral("Add-Type -AssemblyName System.IO.Compression.FileSystem;")
        + QStringLiteral("$archive=[System.IO.Compression.ZipFile]::OpenRead($env:LEMON_REF_ZIP_IN);")
        + QStringLiteral("try{$entry=$archive.Entries|Where-Object{[System.IO.Path]::GetFileName($_.FullName)-ieq 'AccountSet.json'}|Select-Object -First 1;")
        + QStringLiteral("if($null-eq $entry){throw 'AccountSet.json not found in ZIP'};")
        + QStringLiteral("[System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$env:LEMON_REF_JSON_OUT,$true)}finally{$archive.Dispose()}");
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LEMON_REF_ZIP_IN"), QFileInfo(zipPath).absoluteFilePath());
    environment.insert(QStringLiteral("LEMON_REF_JSON_OUT"), QFileInfo(targetPath).absoluteFilePath());
    return runPowerShell(script, environment, 60000, errorMessage);
}

bool zipDirectory(const QString &sourceDir,
                  const QString &zipPath,
                  QString *errorMessage)
{
    const QString script =
        QStringLiteral("$ErrorActionPreference='Stop';")
        + QStringLiteral("Add-Type -AssemblyName System.IO.Compression;")
        + QStringLiteral("Add-Type -AssemblyName System.IO.Compression.FileSystem;")
        + QStringLiteral("if(Test-Path -LiteralPath $env:LEMON_REF_ZIP_OUT){Remove-Item -LiteralPath $env:LEMON_REF_ZIP_OUT -Force};")
        + QStringLiteral("[System.IO.Compression.ZipFile]::CreateFromDirectory($env:LEMON_REF_ZIP_SRC,$env:LEMON_REF_ZIP_OUT,[System.IO.Compression.CompressionLevel]::Optimal,$false)");
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LEMON_REF_ZIP_SRC"), QFileInfo(sourceDir).absoluteFilePath());
    environment.insert(QStringLiteral("LEMON_REF_ZIP_OUT"), QFileInfo(zipPath).absoluteFilePath());
    return runPowerShell(script, environment, 60000, errorMessage);
}

QString uniqueFilePath(const QString &directory,
                       const QString &baseName,
                       const QString &suffix)
{
    QString path = QDir(directory).absoluteFilePath(baseName + suffix);
    int index = 1;
    while (QFile::exists(path))
        path = QDir(directory).absoluteFilePath(baseName + QStringLiteral("_%1").arg(index++) + suffix);
    return path;
}

ReferenceRepairResult processInput(const QString &inputPath,
                                   const QString &workRoot,
                                   const QString &outputRoot)
{
    ReferenceRepairResult result;
    result.inputPath = inputPath;
    const QFileInfo inputInfo(inputPath);
    if (!inputInfo.isFile())
    {
        result.errorMessage = QStringLiteral("\u8f93\u5165\u6587\u4ef6\u4e0d\u5b58\u5728\uff1a") + QDir::toNativeSeparators(inputPath);
        return result;
    }

    QDir rootDir(workRoot);
    if (!rootDir.exists() && !rootDir.mkpath(QStringLiteral(".")))
    {
        result.errorMessage = QStringLiteral("\u521b\u5efa\u5de5\u4f5c\u76ee\u5f55\u5931\u8d25");
        return result;
    }

    const QString jobName = inputInfo.completeBaseName() + QLatin1Char('_')
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmsszzz"));
    const QString jobDir = rootDir.absoluteFilePath(jobName);
    const QString packageDir = QDir(jobDir).absoluteFilePath(QStringLiteral("package"));
    if (!QDir().mkpath(packageDir) || !QDir().mkpath(outputRoot))
    {
        result.errorMessage = QStringLiteral("\u521b\u5efa\u5904\u7406\u76ee\u5f55\u5931\u8d25");
        return result;
    }

    const QString jsonPath = QDir(packageDir).absoluteFilePath(QStringLiteral("AccountSet.json"));
    QString errorMessage;
    if (inputInfo.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0)
    {
        if (!extractAccountSetJson(inputPath, jsonPath, &errorMessage))
        {
            result.errorMessage = errorMessage;
            return result;
        }
    }
    else if (inputInfo.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0)
    {
        if (!QFile::copy(inputInfo.absoluteFilePath(), jsonPath))
        {
            result.errorMessage = QStringLiteral("\u590d\u5236 AccountSet.json \u5230\u5de5\u4f5c\u76ee\u5f55\u5931\u8d25");
            return result;
        }
    }
    else
    {
        result.errorMessage = QStringLiteral("\u4ec5\u652f\u6301 JSON \u6216 ZIP \u6587\u4ef6");
        return result;
    }

    QMap<QString, QString> mapping;
    QMap<QString, MappingUsage> usage;
    if (!processJsonFile(jsonPath, &result, &mapping, &usage, &errorMessage))
    {
        result.errorMessage = errorMessage;
        return result;
    }

    const QString baseName = RepairFileNameUtils::makeBaseName(
        result.accountSetName,
        inputInfo.completeBaseName(),
        QStringLiteral("\u51ed\u8bc1\u4f59\u989d\u79d1\u76ee\u7f16\u7801\u4fee\u590d"));
    result.outputZipPath = uniqueFilePath(outputRoot, baseName, QStringLiteral(".zip"));
    result.mappingPath = uniqueFilePath(outputRoot, baseName + QStringLiteral("_\u7f16\u7801\u6620\u5c04"), QStringLiteral(".csv"));

    if (!writeMappingFile(result.mappingPath, mapping, usage, &errorMessage))
    {
        result.errorMessage = errorMessage;
        return result;
    }
    if (!zipDirectory(packageDir, result.outputZipPath, &errorMessage))
    {
        result.errorMessage = errorMessage;
        return result;
    }

    result.logs.prepend(QStringLiteral("\u8f93\u5165\uff1a") + QDir::toNativeSeparators(inputPath));
    result.logs.append(QStringLiteral("\u8f93\u51fa ZIP\uff1a") + QDir::toNativeSeparators(result.outputZipPath));
    result.logs.append(QStringLiteral("\u6620\u5c04\u6e05\u5355\uff1a") + QDir::toNativeSeparators(result.mappingPath));
    result.ok = true;
    return result;
}
}

ReferenceAccountCodeRepairWidget::ReferenceAccountCodeRepairWidget(GlobalData *globalData,
                                                                   QWidget *parent)
    : QWidget(parent),
      m_pGlobalData(globalData),
      m_pPathConfig(nullptr),
      m_pInputFilePicker(nullptr),
      m_pDropLabel(nullptr),
      m_pStatusLabel(nullptr),
      m_pLogEdit(nullptr),
      m_pBtnBrowse(nullptr),
      m_pBtnBack(nullptr),
      m_requestId(0),
      m_batchProcessing(false),
      m_batchSuccessCount(0),
      m_batchFailureCount(0)
{
    setAcceptDrops(true);
    setupUi();
    refreshInputFiles();
}

void ReferenceAccountCodeRepairWidget::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    QHBoxLayout *titleLayout = new QHBoxLayout();
    QLabel *title = new QLabel(QStringLiteral("\u51ed\u8bc1\u4f59\u989d\u79d1\u76ee\u7f16\u7801\u4fee\u590d"), this);
    title->setObjectName(QStringLiteral("labelPageTitle"));
    titleLayout->addWidget(title);
    titleLayout->addStretch();
    m_pBtnBrowse = new QPushButton(QStringLiteral("\u9009\u62e9\u6587\u4ef6"), this);
    m_pBtnBrowse->setProperty("flatGreen", true);
    m_pBtnBrowse->setMaximumWidth(100);
    titleLayout->addWidget(m_pBtnBrowse);
    layout->addLayout(titleLayout);

    QLabel *description = new QLabel(
        QStringLiteral("\u6309 GLAccountList \u79d1\u76ee\u7f16\u7801\u6392\u5e8f\u5e76\u63a8\u5bfc\u6700\u8fd1\u7236\u7ea7\uff0c\u4ec5\u4fee\u6b63\u51ed\u8bc1\u548c\u4f59\u989d\u7684 AcctCode\uff1b\u4e0d\u4f7f\u7528 AcctParent/AcctLevel\uff0c\u4e0d\u4fee\u6539\u79d1\u76ee\u8868\u3002"),
        this);
    description->setWordWrap(true);
    layout->addWidget(description);

    m_pPathConfig = new RepairPathConfigWidget(this);
    m_pPathConfig->setPathsChangedCallback([this]() { refreshInputFiles(); });
    layout->addWidget(m_pPathConfig);

    m_pInputFilePicker = new RepairInputFilePickerWidget(
        QStringList() << QStringLiteral("*.zip") << QStringLiteral("*.json"), this);
    m_pInputFilePicker->setProcessCallback(
        [this](const QStringList &paths) { processSelectedFiles(paths); });
    m_pInputFilePicker->setFilesChangedCallback([this](int count) {
        if (!m_batchProcessing && m_pStatusLabel)
            m_pStatusLabel->setText(QStringLiteral("\u5df2\u626b\u63cf\u5230 %1 \u4e2a JSON/ZIP").arg(count));
    });
    layout->addWidget(m_pInputFilePicker);

    m_pDropLabel = new QLabel(
        QStringLiteral("\u5c06 AccountSet.json \u6216 ZIP \u62d6\u5165\u6b64\u5904\uff0c\u751f\u6210\u4fee\u590d\u540e ZIP \u548c\u7f16\u7801\u6620\u5c04\u6e05\u5355"), this);
    m_pDropLabel->setObjectName(QStringLiteral("labelRepairDropArea"));
    m_pDropLabel->setAlignment(Qt::AlignCenter);
    m_pDropLabel->setMinimumHeight(72);
    m_pDropLabel->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(m_pDropLabel);

    m_pStatusLabel = new QLabel(QStringLiteral("\u5f85\u5904\u7406"), this);
    m_pStatusLabel->setWordWrap(true);
    layout->addWidget(m_pStatusLabel);

    m_pLogEdit = new QTextEdit(this);
    m_pLogEdit->setReadOnly(true);
    m_pLogEdit->setMinimumHeight(190);
    layout->addWidget(m_pLogEdit, 1);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    m_pBtnBack = new QPushButton(QStringLiteral("\u8fd4\u56de"), this);
    m_pBtnBack->setObjectName(QStringLiteral("btnBackToMain"));
    m_pBtnBack->setProperty("grayButton", true);
    m_pBtnBack->setMaximumWidth(100);
    m_pBtnBack->setMinimumHeight(36);
    buttonLayout->addWidget(m_pBtnBack);
    buttonLayout->addStretch();
    layout->addLayout(buttonLayout);

    connect(m_pBtnBrowse, &QPushButton::clicked,
            this, &ReferenceAccountCodeRepairWidget::onBrowseClicked);
    connect(m_pBtnBack, &QPushButton::clicked,
            this, &ReferenceAccountCodeRepairWidget::onBackClicked);
}

QString ReferenceAccountCodeRepairWidget::workRootDir() const
{
    return QDir::cleanPath(qApp->applicationDirPath()
                           + QStringLiteral("/ReferenceAccountCodeRepairPackages"));
}

QString ReferenceAccountCodeRepairWidget::outputDir() const
{
    return RepairPathSettings::outputDir();
}

void ReferenceAccountCodeRepairWidget::setRunning(bool running, const QString &message)
{
    setProperty("sessionBusysetRunning", running);
    m_pBtnBrowse->setEnabled(!running);
    m_pBtnBack->setEnabled(!running);
    m_pPathConfig->setEnabled(!running);
    m_pInputFilePicker->setRunning(running);
    setAcceptDrops(!running);
    if (!message.isEmpty())
        m_pStatusLabel->setText(message);
}

void ReferenceAccountCodeRepairWidget::refreshInputFiles()
{
    m_pPathConfig->refreshPaths();
    m_pInputFilePicker->refreshFiles();
}

void ReferenceAccountCodeRepairWidget::processSelectedFiles(const QStringList &filePaths)
{
    if (filePaths.isEmpty() || m_batchProcessing)
        return;
    m_batchProcessing = true;
    m_batchSuccessCount = 0;
    m_batchFailureCount = 0;
    m_pendingBatchPaths = filePaths;
    m_batchResults.clear();
    m_pLogEdit->clear();
    setRunning(true, QStringLiteral("\u51c6\u5907\u6279\u91cf\u5904\u7406 %1 \u4e2a\u6587\u4ef6...").arg(filePaths.size()));
    processNextBatchFile();
}

void ReferenceAccountCodeRepairWidget::processNextBatchFile()
{
    if (!m_batchProcessing)
        return;
    if (m_pendingBatchPaths.isEmpty())
    {
        m_batchProcessing = false;
        setRunning(false, QStringLiteral("\u6279\u91cf\u5904\u7406\u5b8c\u6210\uff1a\u6210\u529f %1\uff0c\u5931\u8d25 %2")
                   .arg(m_batchSuccessCount).arg(m_batchFailureCount));
        m_pLogEdit->append(QStringLiteral("\n") + m_batchResults.join(QStringLiteral("\n")));
        return;
    }
    startProcessFile(m_pendingBatchPaths.takeFirst());
}

void ReferenceAccountCodeRepairWidget::startProcessFile(const QString &filePath)
{
    const int requestId = ++m_requestId;
    const QString inputPath = QFileInfo(filePath).absoluteFilePath();
    const QString rootPath = workRootDir();
    const QString outputPath = outputDir();
    if (!m_batchProcessing)
    {
        setRunning(true, QStringLiteral("\u6b63\u5728\u5206\u6790\u79d1\u76ee\u5c42\u7ea7\u5e76\u4fee\u6b63\u51ed\u8bc1\u4f59\u989d..."));
        m_pLogEdit->setPlainText(QStringLiteral("\u6b63\u5728\u5904\u7406\uff1a") + QDir::toNativeSeparators(inputPath));
    }
    else
    {
        m_pLogEdit->append(QStringLiteral("\u6b63\u5728\u5904\u7406\uff1a") + QDir::toNativeSeparators(inputPath));
    }

    QSharedPointer<ReferenceRepairResult> result(new ReferenceRepairResult);
    QThread *thread = QThread::create([result, inputPath, rootPath, outputPath]() {
        *result = processInput(inputPath, rootPath, outputPath);
    });
    connect(thread, &QThread::finished, this, [this, result, requestId]() {
        if (requestId != m_requestId)
            return;

        if (!result->ok)
        {
            if (m_batchProcessing)
            {
                ++m_batchFailureCount;
                m_batchResults.append(QFileInfo(result->inputPath).fileName()
                    + QStringLiteral("\uff1a\u5931\u8d25 - ") + result->errorMessage);
                m_pLogEdit->append(result->errorMessage);
                processNextBatchFile();
                return;
            }
            setRunning(false, QStringLiteral("\u5904\u7406\u5931\u8d25\uff1a") + result->errorMessage);
            m_pLogEdit->append(result->errorMessage);
            QMessageBox::warning(this, QStringLiteral("\u5904\u7406\u5931\u8d25"), result->errorMessage);
            return;
        }

        m_pLogEdit->append(result->logs.join(QStringLiteral("\n")));
        if (m_batchProcessing)
        {
            ++m_batchSuccessCount;
            m_batchResults.append(QFileInfo(result->inputPath).fileName()
                + QStringLiteral("\uff1a\u6210\u529f -> ")
                + QDir::toNativeSeparators(result->outputZipPath));
            processNextBatchFile();
            return;
        }

        setRunning(false, QStringLiteral("\u5904\u7406\u5b8c\u6210\uff1a\u51ed\u8bc1\u4fee\u6b63 %1 \u6761\uff0c\u4f59\u989d\u4fee\u6b63 %2 \u6761")
                   .arg(result->changedVoucherCount)
                   .arg(result->changedBalanceCount + result->changedFullBalanceCount));
        QMessageBox::information(
            this,
            QStringLiteral("\u51ed\u8bc1\u4f59\u989d\u79d1\u76ee\u7f16\u7801\u4fee\u590d"),
            QStringLiteral("\u5904\u7406\u5b8c\u6210\u3002\n\n\u8f93\u51fa ZIP\uff1a\n%1\n\n\u7f16\u7801\u6620\u5c04\uff1a\n%2")
                .arg(QDir::toNativeSeparators(result->outputZipPath),
                     QDir::toNativeSeparators(result->mappingPath)));
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ReferenceAccountCodeRepairWidget::onBrowseClicked()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("\u9009\u62e9 AccountSet.json \u6216 ZIP"),
        RepairPathSettings::inputDir(),
        QStringLiteral("\u8d26\u5957\u6570\u636e (*.json *.zip);;JSON (*.json);;ZIP (*.zip)"));
    if (!filePath.isEmpty())
        startProcessFile(filePath);
}

void ReferenceAccountCodeRepairWidget::onBackClicked()
{
    emit backToMain();
}

void ReferenceAccountCodeRepairWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (hasSupportedInputFile(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void ReferenceAccountCodeRepairWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (hasSupportedInputFile(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void ReferenceAccountCodeRepairWidget::dropEvent(QDropEvent *event)
{
    if (!event->possibleActions().testFlag(Qt::CopyAction))
    {
        event->ignore();
        return;
    }
    if (!hasSupportedInputFile(event->mimeData()))
    {
        event->ignore();
        return;
    }
    for (const QUrl &url : event->mimeData()->urls())
    {
        if (url.isLocalFile() && isSupportedInputFile(url.toLocalFile()))
        {
            const QString inputPath = RepairInputFileUtils::copyDroppedFile(url.toLocalFile(), this);
            if (inputPath.isEmpty())
            {
                event->ignore();
                return;
            }
            refreshInputFiles();
            event->setDropAction(Qt::CopyAction);
            event->accept();
            startProcessFile(inputPath);
            return;
        }
    }
    event->ignore();
}
