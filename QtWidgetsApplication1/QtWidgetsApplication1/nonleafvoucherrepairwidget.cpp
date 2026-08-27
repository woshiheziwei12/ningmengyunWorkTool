#include "nonleafvoucherrepairwidget.h"

#include "Common/globaldata.h"
#include "Common/logger.h"
#include "Common/repairfilenameutils.h"
#include "Common/repairinputfilepickerwidget.h"
#include "Common/repairpathconfigwidget.h"
#include "Common/repairpathsettings.h"

#include <algorithm>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
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
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
struct SubjectNode
{
    QString oldCode;
    QString newCode;
    QString name;
    QString rootNewCode;
    int originalIndex = -1;
    int parentIndex = -1;
    int level = 1;
    QList<int> children;
};

struct LeafOption
{
    QString code;
    QString name;
};

struct NonLeafUsage
{
    QString code;
    QString name;
    int voucherCount = 0;
    QList<LeafOption> leafOptions;
};

struct AnalysisResult
{
    bool ok = false;
    QString inputPath;
    QString jsonPath;
    QString accountSetName;
    QString errorMessage;
    QStringList logs;
    QList<NonLeafUsage> usages;
    QSet<QString> leafCodes;
    int accountCount = 0;
    int voucherCount = 0;
    int nonLeafCount = 0;
    int affectedVoucherCount = 0;
    int changedAccountCodeCount = 0;
    int changedReferenceCodeCount = 0;
    int correctedLevelCount = 0;
    int correctedParentCount = 0;
    int correctedDetailCount = 0;
};

struct FinalizeResult
{
    bool ok = false;
    QString outputZipPath;
    QString mappingPath;
    QString errorMessage;
    int changedVoucherCount = 0;
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
        QString result;
        const QString text = value.toString();
        result.reserve(text.size());
        for (const QChar ch : text)
        {
            if (!ch.isSpace())
                result.append(ch);
        }
        return result;
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

QString normalizeCodePart(const QString &text)
{
    QString result;
    bool lastWasSeparator = true;
    for (const QChar ch : text)
    {
        if (isAsciiDigit(ch))
        {
            result.append(ch);
            lastWasSeparator = false;
        }
        else if (!lastWasSeparator)
        {
            result.append(QLatin1Char('.'));
            lastWasSeparator = true;
        }
    }
    while (result.endsWith(QLatin1Char('.')))
        result.chop(1);
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
    return normalizeCodePart(segment);
}

bool buildSubjectTree(const QJsonArray &accountList,
                      QList<SubjectNode> *outputNodes,
                      QMap<QString, QString> *oldToNew,
                      QString *errorMessage)
{
    QList<SubjectNode> nodes;
    QSet<QString> seenCodes;
    for (int index = 0; index < accountList.size(); ++index)
    {
        const QJsonObject account = accountList.at(index).toObject();
        const QString code = codeFromValue(account.value(QStringLiteral("AcctCode")));
        if (code.isEmpty() || seenCodes.contains(code))
            continue;
        seenCodes.insert(code);
        SubjectNode node;
        node.oldCode = code;
        node.name = account.value(QStringLiteral("AcctName")).toString().trimmed();
        node.originalIndex = index;
        nodes.append(node);
    }

    std::sort(nodes.begin(), nodes.end(), [](const SubjectNode &left, const SubjectNode &right) {
        return left.oldCode < right.oldCode;
    });

    QMap<QString, QString> mapping;
    QMap<QString, QString> newCodeOwners;
    for (int index = 0; index < nodes.size(); ++index)
    {
        int parentIndex = -1;
        for (int previous = index - 1; previous >= 0; --previous)
        {
            if (isParentPrefix(nodes.at(previous).oldCode, nodes.at(index).oldCode))
            {
                parentIndex = previous;
                break;
            }
        }

        nodes[index].parentIndex = parentIndex;
        if (parentIndex < 0)
        {
            nodes[index].newCode = normalizeCodePart(nodes.at(index).oldCode);
            nodes[index].rootNewCode = nodes.at(index).newCode;
            nodes[index].level = 1;
        }
        else
        {
            const QString segment = childSegment(nodes.at(parentIndex).oldCode,
                                                 nodes.at(index).oldCode);
            if (segment.isEmpty())
            {
                if (errorMessage)
                    *errorMessage = QStringLiteral("\u65e0\u6cd5\u63a8\u5bfc\u79d1\u76ee %1 \u76f8\u5bf9\u7236\u7ea7 %2 \u7684\u5b50\u7ea7\u7f16\u7801")
                        .arg(nodes.at(index).oldCode, nodes.at(parentIndex).oldCode);
                return false;
            }
            nodes[index].newCode = nodes.at(parentIndex).newCode + QLatin1Char('.') + segment;
            nodes[index].rootNewCode = nodes.at(parentIndex).rootNewCode;
            nodes[index].level = nodes.at(parentIndex).level + 1;
            nodes[parentIndex].children.append(index);
        }

        if (nodes.at(index).newCode.isEmpty())
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("\u79d1\u76ee %1 \u65e0\u6cd5\u751f\u6210\u89c4\u8303\u7f16\u7801").arg(nodes.at(index).oldCode);
            return false;
        }
        if (newCodeOwners.contains(nodes.at(index).newCode)
            && newCodeOwners.value(nodes.at(index).newCode) != nodes.at(index).oldCode)
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("\u79d1\u76ee %1 \u548c %2 \u6dfb\u52a0\u5206\u9694\u7b26\u540e\u51b2\u7a81\u4e3a %3")
                    .arg(newCodeOwners.value(nodes.at(index).newCode),
                         nodes.at(index).oldCode,
                         nodes.at(index).newCode);
            return false;
        }
        newCodeOwners.insert(nodes.at(index).newCode, nodes.at(index).oldCode);
        mapping.insert(nodes.at(index).oldCode, nodes.at(index).newCode);
    }

    if (outputNodes)
        *outputNodes = nodes;
    if (oldToNew)
        *oldToNew = mapping;
    return true;
}

QJsonValue updateAcctCodeReferences(const QJsonValue &value,
                                    const QMap<QString, QString> &oldToNew,
                                    int *changedCount)
{
    if (value.isArray())
    {
        QJsonArray result;
        for (const QJsonValue &item : value.toArray())
            result.append(updateAcctCodeReferences(item, oldToNew, changedCount));
        return result;
    }
    if (!value.isObject())
        return value;

    QJsonObject object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it)
    {
        if (it.key() == QStringLiteral("AcctCode"))
        {
            const QString oldCode = codeFromValue(it.value());
            if (!oldCode.isEmpty() && oldToNew.contains(oldCode))
            {
                const QString newCode = oldToNew.value(oldCode);
                const QString currentText = it.value().isString()
                    ? it.value().toString() : oldCode;
                if (currentText != newCode)
                {
                    it.value() = newCode;
                    if (changedCount)
                        ++(*changedCount);
                }
            }
        }
        else
        {
            it.value() = updateAcctCodeReferences(it.value(), oldToNew, changedCount);
        }
    }
    return object;
}

QList<LeafOption> collectLeafOptions(int nodeIndex,
                                     const QList<SubjectNode> &nodes)
{
    QList<LeafOption> options;
    QList<int> pending = nodes.at(nodeIndex).children;
    while (!pending.isEmpty())
    {
        const int current = pending.takeFirst();
        if (nodes.at(current).children.isEmpty())
        {
            LeafOption option;
            option.code = nodes.at(current).newCode;
            option.name = nodes.at(current).name;
            options.append(option);
        }
        else
        {
            for (const int child : nodes.at(current).children)
                pending.append(child);
        }
    }
    std::sort(options.begin(), options.end(), [](const LeafOption &left, const LeafOption &right) {
        return left.code < right.code;
    });
    return options;
}

bool applyTreeToAccountList(QJsonObject *root,
                            const QList<SubjectNode> &nodes,
                            const QMap<QString, QString> &oldToNew,
                            AnalysisResult *result,
                            QString *errorMessage)
{
    const QJsonArray source = root->value(QStringLiteral("GLAccountList")).toArray();
    QMap<QString, int> nodeByOldCode;
    for (int index = 0; index < nodes.size(); ++index)
        nodeByOldCode.insert(nodes.at(index).oldCode, index);

    QJsonArray repaired;
    for (const QJsonValue &value : source)
    {
        QJsonObject account = value.toObject();
        const QString oldCode = codeFromValue(account.value(QStringLiteral("AcctCode")));
        if (!nodeByOldCode.contains(oldCode))
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("GLAccountList \u4e2d\u5b58\u5728\u7a7a\u7f16\u7801\u6216\u65e0\u6cd5\u8bc6\u522b\u7684\u79d1\u76ee");
            return false;
        }

        const SubjectNode &node = nodes.at(nodeByOldCode.value(oldCode));
        const QString parentCode = node.parentIndex < 0
            ? QStringLiteral("0") : nodes.at(node.parentIndex).newCode;
        const int detail = node.children.isEmpty() ? 1 : 0;

        if (oldCode != node.newCode)
            ++result->changedAccountCodeCount;
        if (account.value(QStringLiteral("AcctLevel")).toInt(-1) != node.level)
            ++result->correctedLevelCount;
        if (account.value(QStringLiteral("AcctIsDetail")).toInt(-1) != detail)
            ++result->correctedDetailCount;
        QString oldParent = codeFromValue(account.value(QStringLiteral("AcctParent")));
        if (oldParent.isEmpty())
            oldParent = QStringLiteral("0");
        if (oldParent != parentCode)
            ++result->correctedParentCount;

        account.insert(QStringLiteral("AcctCode"), node.newCode);
        account.insert(QStringLiteral("AcctParent"), parentCode);
        account.insert(QStringLiteral("AcctLevel"), node.level);
        account.insert(QStringLiteral("AcctIsDetail"), detail);

        const QString fullCode = codeFromValue(account.value(QStringLiteral("AcctFullCode")));
        if (!fullCode.isEmpty() && oldToNew.contains(fullCode))
            account.insert(QStringLiteral("AcctFullCode"), oldToNew.value(fullCode));
        const QString rootCode = codeFromValue(account.value(QStringLiteral("AcctRoot")));
        if (!rootCode.isEmpty())
            account.insert(QStringLiteral("AcctRoot"), node.rootNewCode);
        repaired.append(account);
    }
    root->insert(QStringLiteral("GLAccountList"), repaired);
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
        + QStringLiteral("$archive=[System.IO.Compression.ZipFile]::OpenRead($env:LEMON_NONLEAF_ZIP_IN);")
        + QStringLiteral("try{$entry=$archive.Entries|Where-Object{[System.IO.Path]::GetFileName($_.FullName)-ieq 'AccountSet.json'}|Select-Object -First 1;")
        + QStringLiteral("if($null-eq $entry){throw 'AccountSet.json not found in ZIP'};")
        + QStringLiteral("[System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$env:LEMON_NONLEAF_JSON_OUT,$true)}finally{$archive.Dispose()}");
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LEMON_NONLEAF_ZIP_IN"), QFileInfo(zipPath).absoluteFilePath());
    environment.insert(QStringLiteral("LEMON_NONLEAF_JSON_OUT"), QFileInfo(targetPath).absoluteFilePath());
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
        + QStringLiteral("if(Test-Path -LiteralPath $env:LEMON_NONLEAF_ZIP_OUT){Remove-Item -LiteralPath $env:LEMON_NONLEAF_ZIP_OUT -Force};")
        + QStringLiteral("[System.IO.Compression.ZipFile]::CreateFromDirectory($env:LEMON_NONLEAF_ZIP_SRC,$env:LEMON_NONLEAF_ZIP_OUT,[System.IO.Compression.CompressionLevel]::Optimal,$false)");
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LEMON_NONLEAF_ZIP_SRC"), QFileInfo(sourceDir).absoluteFilePath());
    environment.insert(QStringLiteral("LEMON_NONLEAF_ZIP_OUT"), QFileInfo(zipPath).absoluteFilePath());
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

AnalysisResult analyzeInput(const QString &inputPath, const QString &workRoot)
{
    AnalysisResult result;
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
    const QString jobDir = rootDir.absoluteFilePath(
        inputInfo.completeBaseName() + QLatin1Char('_')
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmsszzz")));
    const QString packageDir = QDir(jobDir).absoluteFilePath(QStringLiteral("package"));
    if (!QDir().mkpath(packageDir))
    {
        result.errorMessage = QStringLiteral("\u521b\u5efa\u5904\u7406\u76ee\u5f55\u5931\u8d25");
        return result;
    }
    result.jsonPath = QDir(packageDir).absoluteFilePath(QStringLiteral("AccountSet.json"));

    QString errorMessage;
    if (inputInfo.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0)
    {
        if (!extractAccountSetJson(inputPath, result.jsonPath, &errorMessage))
        {
            result.errorMessage = errorMessage;
            return result;
        }
    }
    else if (inputInfo.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0)
    {
        if (!QFile::copy(inputInfo.absoluteFilePath(), result.jsonPath))
        {
            result.errorMessage = QStringLiteral("\u590d\u5236 AccountSet.json \u5931\u8d25");
            return result;
        }
    }
    else
    {
        result.errorMessage = QStringLiteral("\u4ec5\u652f\u6301 JSON \u6216 ZIP \u6587\u4ef6");
        return result;
    }

    QFile file(result.jsonPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        result.errorMessage = QStringLiteral("\u8bfb\u53d6 AccountSet.json \u5931\u8d25\uff1a") + file.errorString();
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        result.errorMessage = QStringLiteral("AccountSet.json \u89e3\u6790\u5931\u8d25\uff1a") + parseError.errorString();
        return result;
    }

    QJsonObject root = document.object();
    if (!root.value(QStringLiteral("GLAccountList")).isArray()
        || !root.value(QStringLiteral("GLVoucherList")).isArray())
    {
        result.errorMessage = QStringLiteral("\u672a\u627e\u5230 GLAccountList \u6216 GLVoucherList");
        return result;
    }

    const QJsonArray accountList = root.value(QStringLiteral("GLAccountList")).toArray();
    QList<SubjectNode> nodes;
    QMap<QString, QString> oldToNew;
    if (!buildSubjectTree(accountList, &nodes, &oldToNew, &errorMessage))
    {
        result.errorMessage = errorMessage;
        return result;
    }
    result.accountCount = nodes.size();
    if (!applyTreeToAccountList(&root, nodes, oldToNew, &result, &errorMessage))
    {
        result.errorMessage = errorMessage;
        return result;
    }

    for (auto it = root.begin(); it != root.end(); ++it)
    {
        if (it.key() == QStringLiteral("GLAccountList"))
            continue;
        it.value() = updateAcctCodeReferences(it.value(), oldToNew,
                                              &result.changedReferenceCodeCount);
    }

    QMap<QString, int> nodeByNewCode;
    for (int index = 0; index < nodes.size(); ++index)
    {
        nodeByNewCode.insert(nodes.at(index).newCode, index);
        if (nodes.at(index).children.isEmpty())
            result.leafCodes.insert(nodes.at(index).newCode);
        else
            ++result.nonLeafCount;
    }

    QMap<QString, int> usageCounts;
    QStringList missingCodes;
    const QJsonArray vouchers = root.value(QStringLiteral("GLVoucherList")).toArray();
    result.voucherCount = vouchers.size();
    for (const QJsonValue &value : vouchers)
    {
        const QString code = codeFromValue(value.toObject().value(QStringLiteral("AcctCode")));
        if (!nodeByNewCode.contains(code))
        {
            if (!missingCodes.contains(code))
                missingCodes.append(code.isEmpty() ? QStringLiteral("<empty>") : code);
            continue;
        }
        const SubjectNode &node = nodes.at(nodeByNewCode.value(code));
        if (!node.children.isEmpty())
            usageCounts.insert(code, usageCounts.value(code) + 1);
    }
    if (!missingCodes.isEmpty())
    {
        result.errorMessage = QStringLiteral("\u6709 %1 \u4e2a\u51ed\u8bc1\u79d1\u76ee\u65e0\u6cd5\u5728 GLAccountList \u4e2d\u627e\u5230\uff1a%2")
            .arg(missingCodes.size())
            .arg(missingCodes.mid(0, 20).join(QStringLiteral(", ")));
        return result;
    }

    for (auto it = usageCounts.constBegin(); it != usageCounts.constEnd(); ++it)
    {
        const int nodeIndex = nodeByNewCode.value(it.key());
        NonLeafUsage usage;
        usage.code = it.key();
        usage.name = nodes.at(nodeIndex).name;
        usage.voucherCount = it.value();
        usage.leafOptions = collectLeafOptions(nodeIndex, nodes);
        if (usage.leafOptions.isEmpty())
        {
            result.errorMessage = QStringLiteral("\u975e\u672b\u7ea7\u79d1\u76ee %1 \u6ca1\u6709\u53ef\u9009\u7684\u672b\u7ea7\u5b50\u79d1\u76ee").arg(usage.code);
            return result;
        }
        result.affectedVoucherCount += usage.voucherCount;
        result.usages.append(usage);
    }
    std::sort(result.usages.begin(), result.usages.end(), [](const NonLeafUsage &left, const NonLeafUsage &right) {
        return left.code < right.code;
    });

    QSaveFile output(result.jsonPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        result.errorMessage = QStringLiteral("\u5199\u5165\u5c42\u7ea7\u6821\u6b63\u540e JSON \u5931\u8d25\uff1a") + output.errorString();
        return result;
    }
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (output.write(json) != json.size() || !output.commit())
    {
        result.errorMessage = QStringLiteral("\u4fdd\u5b58\u5c42\u7ea7\u6821\u6b63\u540e JSON \u5931\u8d25\uff1a") + output.errorString();
        return result;
    }

    result.accountSetName = root.value(QStringLiteral("AccountSet"))
        .toObject().value(QStringLiteral("AsName")).toString().trimmed();
    result.logs.append(QStringLiteral("\u79d1\u76ee %1 \u4e2a\uff0c\u63a8\u5bfc\u975e\u672b\u7ea7 %2 \u4e2a")
        .arg(result.accountCount).arg(result.nonLeafCount));
    result.logs.append(QStringLiteral("\u79d1\u76ee\u7f16\u7801\u5206\u9694\u7b26\u4fee\u6b63 %1 \u4e2a\uff0c\u5f15\u7528\u540c\u6b65 %2 \u5904")
        .arg(result.changedAccountCodeCount).arg(result.changedReferenceCodeCount));
    result.logs.append(QStringLiteral("\u5c42\u7ea7\u4fee\u6b63 %1 \u4e2a\uff0c\u7236\u7ea7\u4fee\u6b63 %2 \u4e2a\uff0c\u672b\u7ea7\u6807\u8bb0\u4fee\u6b63 %3 \u4e2a")
        .arg(result.correctedLevelCount).arg(result.correctedParentCount).arg(result.correctedDetailCount));
    result.logs.append(QStringLiteral("\u51ed\u8bc1 %1 \u6761\uff0c\u4f7f\u7528\u975e\u672b\u7ea7\u79d1\u76ee %2 \u4e2a/%3 \u6761")
        .arg(result.voucherCount).arg(result.usages.size()).arg(result.affectedVoucherCount));
    result.ok = true;
    return result;
}

QString csvEscape(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

bool writeMappingFile(const QString &path,
                      const QList<NonLeafUsage> &usages,
                      const QMap<QString, QString> &selections,
                      QString *errorMessage)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u521b\u5efa\u6620\u5c04\u6e05\u5355\u5931\u8d25\uff1a") + file.errorString();
        return false;
    }
    file.write("\xEF\xBB\xBF");
    file.write("NonLeafCode,NonLeafName,TargetLeafCode,TargetLeafName,VoucherCount\n");
    for (const NonLeafUsage &usage : usages)
    {
        const QString targetCode = selections.value(usage.code);
        QString targetName;
        for (const LeafOption &option : usage.leafOptions)
        {
            if (option.code == targetCode)
            {
                targetName = option.name;
                break;
            }
        }
        const QString line = QStringLiteral("%1,%2,%3,%4,%5\n")
            .arg(csvEscape(usage.code), csvEscape(usage.name),
                 csvEscape(targetCode), csvEscape(targetName),
                 QString::number(usage.voucherCount));
        file.write(line.toUtf8());
    }
    if (!file.commit())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u4fdd\u5b58\u6620\u5c04\u6e05\u5355\u5931\u8d25\uff1a") + file.errorString();
        return false;
    }
    return true;
}

FinalizeResult finalizeOutput(const AnalysisResult &analysis,
                              const QMap<QString, QString> &selections,
                              const QString &outputRoot)
{
    FinalizeResult result;
    QFile file(analysis.jsonPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        result.errorMessage = QStringLiteral("\u91cd\u65b0\u8bfb\u53d6\u5de5\u4f5c JSON \u5931\u8d25\uff1a") + file.errorString();
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        result.errorMessage = QStringLiteral("\u5de5\u4f5c JSON \u89e3\u6790\u5931\u8d25\uff1a") + parseError.errorString();
        return result;
    }

    for (auto it = selections.constBegin(); it != selections.constEnd(); ++it)
    {
        if (!analysis.leafCodes.contains(it.value()))
        {
            result.errorMessage = QStringLiteral("\u76ee\u6807\u79d1\u76ee %1 \u4e0d\u662f\u91cd\u65b0\u63a8\u5bfc\u540e\u7684\u672b\u7ea7\u79d1\u76ee").arg(it.value());
            return result;
        }
    }

    QJsonObject root = document.object();
    QJsonArray vouchers = root.value(QStringLiteral("GLVoucherList")).toArray();
    for (int index = 0; index < vouchers.size(); ++index)
    {
        QJsonObject voucher = vouchers.at(index).toObject();
        const QString code = codeFromValue(voucher.value(QStringLiteral("AcctCode")));
        if (selections.contains(code))
        {
            voucher.insert(QStringLiteral("AcctCode"), selections.value(code));
            vouchers.replace(index, voucher);
            ++result.changedVoucherCount;
        }
    }
    if (result.changedVoucherCount != analysis.affectedVoucherCount)
    {
        result.errorMessage = QStringLiteral("\u51ed\u8bc1\u4fee\u6b63\u6570\u91cf\u6821\u9a8c\u5931\u8d25\uff1a\u9884\u671f %1\uff0c\u5b9e\u9645 %2")
            .arg(analysis.affectedVoucherCount).arg(result.changedVoucherCount);
        return result;
    }
    root.insert(QStringLiteral("GLVoucherList"), vouchers);

    QSaveFile output(analysis.jsonPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        result.errorMessage = QStringLiteral("\u5199\u5165\u4fee\u590d\u540e JSON \u5931\u8d25\uff1a") + output.errorString();
        return result;
    }
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (output.write(json) != json.size() || !output.commit())
    {
        result.errorMessage = QStringLiteral("\u4fdd\u5b58\u4fee\u590d\u540e JSON \u5931\u8d25\uff1a") + output.errorString();
        return result;
    }

    if (!QDir().mkpath(outputRoot))
    {
        result.errorMessage = QStringLiteral("\u521b\u5efa\u8f93\u51fa\u76ee\u5f55\u5931\u8d25");
        return result;
    }
    const QFileInfo inputInfo(analysis.inputPath);
    const QString baseName = RepairFileNameUtils::makeBaseName(
        analysis.accountSetName, inputInfo.completeBaseName(),
        QStringLiteral("\u51ed\u8bc1\u975e\u672b\u7ea7\u79d1\u76ee\u8f6c\u6362"));
    result.outputZipPath = uniqueFilePath(outputRoot, baseName, QStringLiteral(".zip"));
    result.mappingPath = uniqueFilePath(outputRoot, baseName + QStringLiteral("_\u8f6c\u6362\u6620\u5c04"), QStringLiteral(".csv"));

    QString errorMessage;
    if (!writeMappingFile(result.mappingPath, analysis.usages, selections, &errorMessage))
    {
        result.errorMessage = errorMessage;
        return result;
    }
    if (!zipDirectory(QFileInfo(analysis.jsonPath).absolutePath(),
                      result.outputZipPath, &errorMessage))
    {
        result.errorMessage = errorMessage;
        return result;
    }
    result.ok = true;
    return result;
}

class NonLeafMappingDialog : public QDialog
{
public:
    explicit NonLeafMappingDialog(const QList<NonLeafUsage> &usages,
                                  QWidget *parent = nullptr)
        : QDialog(parent), m_usages(usages)
    {
        setWindowTitle(QStringLiteral("\u786e\u8ba4\u975e\u672b\u7ea7\u51ed\u8bc1\u79d1\u76ee\u8f6c\u6362"));
        resize(920, qMin(680, 230 + usages.size() * 52));
        QVBoxLayout *layout = new QVBoxLayout(this);
        QLabel *description = new QLabel(
            QStringLiteral("\u4e0b\u5217\u79d1\u76ee\u662f\u6839\u636e\u79d1\u76ee\u7f16\u7801\u91cd\u65b0\u63a8\u5bfc\u7684\u975e\u672b\u7ea7\u79d1\u76ee\u3002\u8bf7\u4e3a\u6bcf\u4e2a\u79d1\u76ee\u9009\u62e9\u4e00\u4e2a\u672b\u7ea7\u5b50\u5b59\u79d1\u76ee\uff0c\u5bf9\u5e94\u51ed\u8bc1\u5c06\u5168\u90e8\u8f6c\u6362\u3002"),
            this);
        description->setWordWrap(true);
        layout->addWidget(description);

        QTableWidget *table = new QTableWidget(usages.size(), 4, this);
        table->setHorizontalHeaderLabels(QStringList()
            << QStringLiteral("\u975e\u672b\u7ea7\u79d1\u76ee")
            << QStringLiteral("\u79d1\u76ee\u540d\u79f0")
            << QStringLiteral("\u51ed\u8bc1\u6761\u6570")
            << QStringLiteral("\u8f6c\u6362\u5230\u672b\u7ea7\u5b50\u79d1\u76ee"));
        table->verticalHeader()->setVisible(false);
        table->setAlternatingRowColors(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);

        for (int row = 0; row < usages.size(); ++row)
        {
            const NonLeafUsage &usage = usages.at(row);
            table->setItem(row, 0, new QTableWidgetItem(usage.code));
            table->setItem(row, 1, new QTableWidgetItem(usage.name));
            QTableWidgetItem *countItem = new QTableWidgetItem(QString::number(usage.voucherCount));
            countItem->setTextAlignment(Qt::AlignCenter);
            table->setItem(row, 2, countItem);

            QComboBox *combo = new QComboBox(table);
            combo->addItem(QStringLiteral("\u8bf7\u9009\u62e9\u672b\u7ea7\u5b50\u79d1\u76ee"), QString());
            for (const LeafOption &option : usage.leafOptions)
            {
                combo->addItem(option.code + QStringLiteral("  ") + option.name,
                               option.code);
            }
            if (usage.leafOptions.size() == 1)
                combo->setCurrentIndex(1);
            table->setCellWidget(row, 3, combo);
            m_combos.append(combo);
        }
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
        layout->addWidget(table, 1);

        QDialogButtonBox *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("\u786e\u8ba4\u8f6c\u6362"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("\u53d6\u6d88"));
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            for (int row = 0; row < m_combos.size(); ++row)
            {
                if (m_combos.at(row)->currentData().toString().isEmpty())
                {
                    QMessageBox::warning(this, QStringLiteral("\u8bf7\u5b8c\u6210\u9009\u62e9"),
                        QStringLiteral("\u8bf7\u4e3a\u79d1\u76ee %1 \u9009\u62e9\u76ee\u6807\u672b\u7ea7\u5b50\u79d1\u76ee\u3002")
                            .arg(m_usages.at(row).code));
                    m_combos.at(row)->setFocus();
                    return;
                }
            }
            accept();
        });
        layout->addWidget(buttons);
    }

    QMap<QString, QString> selections() const
    {
        QMap<QString, QString> result;
        for (int row = 0; row < m_usages.size(); ++row)
            result.insert(m_usages.at(row).code,
                          m_combos.at(row)->currentData().toString());
        return result;
    }

private:
    QList<NonLeafUsage> m_usages;
    QList<QComboBox*> m_combos;
};
}

NonLeafVoucherRepairWidget::NonLeafVoucherRepairWidget(GlobalData *globalData,
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
      m_requestId(0)
{
    setAcceptDrops(true);
    setupUi();
    refreshInputFiles();
}

void NonLeafVoucherRepairWidget::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    QHBoxLayout *titleLayout = new QHBoxLayout();
    QLabel *title = new QLabel(QStringLiteral("\u51ed\u8bc1\u975e\u672b\u7ea7\u79d1\u76ee\u8f6c\u6362"), this);
    title->setObjectName(QStringLiteral("labelPageTitle"));
    titleLayout->addWidget(title);
    titleLayout->addStretch();
    m_pBtnBrowse = new QPushButton(QStringLiteral("\u9009\u62e9\u6587\u4ef6"), this);
    m_pBtnBrowse->setProperty("flatGreen", true);
    m_pBtnBrowse->setMaximumWidth(100);
    titleLayout->addWidget(m_pBtnBrowse);
    layout->addLayout(titleLayout);

    QLabel *description = new QLabel(
        QStringLiteral("\u5148\u6839\u636e\u79d1\u76ee\u7f16\u7801\u6392\u5e8f\u63a8\u5bfc\u7236\u5b50\u5c42\u7ea7\u5e76\u6dfb\u52a0\u5206\u9694\u7b26\uff0c\u91cd\u7b97 AcctLevel/AcctParent/AcctIsDetail\uff1b\u518d\u8ba9\u7528\u6237\u786e\u8ba4\u975e\u672b\u7ea7\u51ed\u8bc1\u8981\u8f6c\u6362\u5230\u7684\u672b\u7ea7\u5b50\u79d1\u76ee\u3002"),
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
        if (m_pStatusLabel)
            m_pStatusLabel->setText(QStringLiteral("\u5df2\u626b\u63cf\u5230 %1 \u4e2a JSON/ZIP\uff1b\u4ea4\u4e92\u786e\u8ba4\u529f\u80fd\u8bf7\u6bcf\u6b21\u5904\u7406\u4e00\u4e2a\u6587\u4ef6").arg(count));
    });
    layout->addWidget(m_pInputFilePicker);

    m_pDropLabel = new QLabel(
        QStringLiteral("\u5c06 AccountSet.json \u6216 ZIP \u62d6\u5165\u6b64\u5904\uff0c\u626b\u63cf\u540e\u5c06\u5f39\u51fa\u975e\u672b\u7ea7\u51ed\u8bc1\u79d1\u76ee\u786e\u8ba4\u7a97\u53e3"), this);
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
    m_pLogEdit->setMinimumHeight(180);
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
            this, &NonLeafVoucherRepairWidget::onBrowseClicked);
    connect(m_pBtnBack, &QPushButton::clicked,
            this, &NonLeafVoucherRepairWidget::onBackClicked);
}

QString NonLeafVoucherRepairWidget::workRootDir() const
{
    return QDir::cleanPath(qApp->applicationDirPath()
                           + QStringLiteral("/NonLeafVoucherRepairPackages"));
}

QString NonLeafVoucherRepairWidget::outputDir() const
{
    return RepairPathSettings::outputDir();
}

void NonLeafVoucherRepairWidget::setRunning(bool running, const QString &message)
{
    m_pBtnBrowse->setEnabled(!running);
    m_pBtnBack->setEnabled(!running);
    m_pPathConfig->setEnabled(!running);
    m_pInputFilePicker->setRunning(running);
    setAcceptDrops(!running);
    if (!message.isEmpty())
        m_pStatusLabel->setText(message);
}

void NonLeafVoucherRepairWidget::refreshInputFiles()
{
    m_pPathConfig->refreshPaths();
    m_pInputFilePicker->refreshFiles();
}

void NonLeafVoucherRepairWidget::processSelectedFiles(const QStringList &filePaths)
{
    if (filePaths.size() != 1)
    {
        QMessageBox::information(this, QStringLiteral("\u8bf7\u5355\u4e2a\u5904\u7406"),
            QStringLiteral("\u6b64\u529f\u80fd\u9700\u8981\u9010\u4e2a\u786e\u8ba4\u79d1\u76ee\u6620\u5c04\uff0c\u8bf7\u6bcf\u6b21\u53ea\u9009\u62e9\u4e00\u4e2a\u6587\u4ef6\u3002"));
        return;
    }
    startProcessFile(filePaths.first());
}

void NonLeafVoucherRepairWidget::startProcessFile(const QString &filePath)
{
    const int requestId = ++m_requestId;
    const QString inputPath = QFileInfo(filePath).absoluteFilePath();
    const QString rootPath = workRootDir();
    setRunning(true, QStringLiteral("\u6b63\u5728\u91cd\u65b0\u63a8\u5bfc\u79d1\u76ee\u5c42\u7ea7\u5e76\u626b\u63cf\u51ed\u8bc1..."));
    m_pLogEdit->setPlainText(QStringLiteral("\u6b63\u5728\u5904\u7406\uff1a") + QDir::toNativeSeparators(inputPath));

    QSharedPointer<AnalysisResult> analysis(new AnalysisResult);
    QThread *thread = QThread::create([analysis, inputPath, rootPath]() {
        *analysis = analyzeInput(inputPath, rootPath);
    });
    connect(thread, &QThread::finished, this, [this, analysis, requestId]() {
        if (requestId != m_requestId)
            return;
        if (!analysis->ok)
        {
            setRunning(false, QStringLiteral("\u626b\u63cf\u5931\u8d25\uff1a") + analysis->errorMessage);
            m_pLogEdit->append(analysis->errorMessage);
            QMessageBox::warning(this, QStringLiteral("\u626b\u63cf\u5931\u8d25"), analysis->errorMessage);
            return;
        }

        m_pLogEdit->setPlainText(analysis->logs.join(QStringLiteral("\n")));
        QMap<QString, QString> selections;
        if (analysis->usages.isEmpty())
        {
            const QMessageBox::StandardButton answer = QMessageBox::question(
                this,
                QStringLiteral("\u672a\u53d1\u73b0\u771f\u5b9e\u975e\u672b\u7ea7\u51ed\u8bc1"),
                QStringLiteral("\u6309\u79d1\u76ee\u7f16\u7801\u91cd\u65b0\u63a8\u5bfc\u540e\uff0c\u672a\u53d1\u73b0\u51ed\u8bc1\u4f7f\u7528\u975e\u672b\u7ea7\u79d1\u76ee\u3002\n\n\u662f\u5426\u7ee7\u7eed\u751f\u6210\u5df2\u4fee\u6b63\u79d1\u76ee\u5206\u9694\u7b26\u3001\u5c42\u7ea7\u3001\u7236\u7ea7\u548c\u672b\u7ea7\u6807\u8bb0\u7684\u65b0\u6570\u636e\u5305\uff1f"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::Yes);
            if (answer != QMessageBox::Yes)
            {
                setRunning(false, QStringLiteral("\u5df2\u53d6\u6d88\uff0c\u672a\u751f\u6210\u8f93\u51fa\u6587\u4ef6"));
                return;
            }
        }
        else
        {
            setRunning(false, QStringLiteral("\u53d1\u73b0 %1 \u4e2a\u975e\u672b\u7ea7\u51ed\u8bc1\u79d1\u76ee\uff0c\u8bf7\u786e\u8ba4\u8f6c\u6362\u76ee\u6807")
                       .arg(analysis->usages.size()));
            NonLeafMappingDialog dialog(analysis->usages, this);
            if (dialog.exec() != QDialog::Accepted)
            {
                m_pStatusLabel->setText(QStringLiteral("\u5df2\u53d6\u6d88\uff0c\u672a\u751f\u6210\u8f93\u51fa\u6587\u4ef6"));
                return;
            }
            selections = dialog.selections();
        }

        setRunning(true, QStringLiteral("\u6b63\u5728\u6309\u786e\u8ba4\u6620\u5c04\u4fee\u6b63\u51ed\u8bc1\u5e76\u751f\u6210 ZIP..."));
        const QString outputPath = outputDir();
        QSharedPointer<FinalizeResult> finalResult(new FinalizeResult);
        QThread *finalThread = QThread::create([finalResult, analysis, selections, outputPath]() {
            *finalResult = finalizeOutput(*analysis, selections, outputPath);
        });
        connect(finalThread, &QThread::finished, this,
                [this, finalResult, analysis, requestId]() {
            if (requestId != m_requestId)
                return;
            if (!finalResult->ok)
            {
                setRunning(false, QStringLiteral("\u751f\u6210\u5931\u8d25\uff1a") + finalResult->errorMessage);
                m_pLogEdit->append(finalResult->errorMessage);
                QMessageBox::warning(this, QStringLiteral("\u751f\u6210\u5931\u8d25"), finalResult->errorMessage);
                return;
            }
            setRunning(false, QStringLiteral("\u5904\u7406\u5b8c\u6210\uff1a\u4fee\u6b63\u975e\u672b\u7ea7\u51ed\u8bc1 %1 \u6761")
                       .arg(finalResult->changedVoucherCount));
            m_pLogEdit->append(QStringLiteral("\u975e\u672b\u7ea7\u51ed\u8bc1\u4fee\u6b63\uff1a%1 \u6761").arg(finalResult->changedVoucherCount));
            m_pLogEdit->append(QStringLiteral("\u8f93\u51fa ZIP\uff1a") + QDir::toNativeSeparators(finalResult->outputZipPath));
            m_pLogEdit->append(QStringLiteral("\u8f6c\u6362\u6620\u5c04\uff1a") + QDir::toNativeSeparators(finalResult->mappingPath));
            QMessageBox::information(this,
                QStringLiteral("\u51ed\u8bc1\u975e\u672b\u7ea7\u79d1\u76ee\u8f6c\u6362"),
                QStringLiteral("\u5904\u7406\u5b8c\u6210\u3002\n\n\u8f93\u51fa ZIP\uff1a\n%1\n\n\u8f6c\u6362\u6620\u5c04\uff1a\n%2")
                    .arg(QDir::toNativeSeparators(finalResult->outputZipPath),
                         QDir::toNativeSeparators(finalResult->mappingPath)));
        });
        connect(finalThread, &QThread::finished, finalThread, &QObject::deleteLater);
        finalThread->start();
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void NonLeafVoucherRepairWidget::onBrowseClicked()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("\u9009\u62e9 AccountSet.json \u6216 ZIP"),
        RepairPathSettings::inputDir(),
        QStringLiteral("\u8d26\u5957\u6570\u636e (*.json *.zip);;JSON (*.json);;ZIP (*.zip)"));
    if (!filePath.isEmpty())
        startProcessFile(filePath);
}

void NonLeafVoucherRepairWidget::onBackClicked()
{
    emit backToMain();
}

void NonLeafVoucherRepairWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (hasSupportedInputFile(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void NonLeafVoucherRepairWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (hasSupportedInputFile(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void NonLeafVoucherRepairWidget::dropEvent(QDropEvent *event)
{
    if (!hasSupportedInputFile(event->mimeData()))
    {
        event->ignore();
        return;
    }
    for (const QUrl &url : event->mimeData()->urls())
    {
        if (url.isLocalFile() && isSupportedInputFile(url.toLocalFile()))
        {
            event->acceptProposedAction();
            startProcessFile(url.toLocalFile());
            return;
        }
    }
    event->ignore();
}
