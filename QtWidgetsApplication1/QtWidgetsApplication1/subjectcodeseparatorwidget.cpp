#include "Common/repairinputfileutils.h"
#include "subjectcodeseparatorwidget.h"
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
#include <QDirIterator>
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
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QSharedPointer>
#include <QStringList>
#include <QTextEdit>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
struct SubjectCodeEntry
{
    QString oldCode;
    QString newCode;
    QString parentOldCode;
    QString parentNewCode;
    QString rootOldCode;
    QString rootNewCode;
    int originalIndex = -1;
    int level = 1;
    bool reassigned = false;
    bool changed = false;
};

struct SubjectCodeProcessResult
{
    bool ok = false;
    QString inputPath;
    QString outputZipPath;
    QString mappingPath;
    QString accountSetName;
    QString errorMessage;
    QStringList logs;
    int jsonCount = 0;
    int accountCount = 0;
    int changedAccountCount = 0;
    int reassignedCount = 0;
    int referenceUpdateCount = 0;
    int accountCodeSpaceCount = 0;
    int referenceCodeSpaceCount = 0;
};

bool isSupportedInputFile(const QString &filePath)
{
    const QFileInfo info(filePath);
    if (!info.isFile())
        return false;

    return info.suffix().compare("zip", Qt::CaseInsensitive) == 0
        || info.suffix().compare("json", Qt::CaseInsensitive) == 0;
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

bool containsCodeSpace(const QString &text)
{
    for (const QChar &ch : text)
    {
        if (ch.isSpace())
            return true;
    }
    return false;
}

QString removeCodeSpaces(const QString &text)
{
    QString result;
    result.reserve(text.size());
    for (const QChar &ch : text)
    {
        if (!ch.isSpace())
            result.append(ch);
    }
    return result;
}

QString codeFromValue(const QJsonValue &value)
{
    if (value.isString())
        return removeCodeSpaces(value.toString());
    if (value.isDouble())
    {
        const double number = value.toDouble();
        const qint64 rounded = static_cast<qint64>(number);
        if (qAbs(number - static_cast<double>(rounded)) < 0.000001)
            return QString::number(rounded);
        return QString::number(number, 'f', 6);
    }
    return QString();
}

int countAccountCodesWithSpaces(const QJsonArray &accountList)
{
    int count = 0;
    for (const QJsonValue &value : accountList)
    {
        const QJsonObject account = value.toObject();
        const QJsonValue codeValue = account.value("AcctCode");
        if (codeValue.isString() && containsCodeSpace(codeValue.toString()))
            ++count;
    }
    return count;
}

bool isAsciiDigit(const QChar &ch)
{
    return ch >= QLatin1Char('0') && ch <= QLatin1Char('9');
}

bool isDigits(const QString &text)
{
    if (text.isEmpty())
        return false;
    for (const QChar &ch : text)
    {
        if (!isAsciiDigit(ch))
            return false;
    }
    return true;
}

QString digitsOnly(const QString &text)
{
    QString result;
    result.reserve(text.size());
    for (const QChar &ch : text)
    {
        if (isAsciiDigit(ch))
            result.append(ch);
    }
    return result;
}

QString normalizeCodePart(const QString &text)
{
    QString result;
    result.reserve(text.size());
    bool lastWasSeparator = true;

    for (const QChar &ch : text)
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

bool isOutputCode(const QString &text)
{
    if (text.isEmpty())
        return false;

    bool lastWasSeparator = true;
    for (const QChar &ch : text)
    {
        if (isAsciiDigit(ch))
        {
            lastWasSeparator = false;
            continue;
        }

        if (ch == QLatin1Char('.') && !lastWasSeparator)
        {
            lastWasSeparator = true;
            continue;
        }

        return false;
    }

    return !lastWasSeparator;
}

bool isParentPrefix(const QString &parentCode, const QString &childCode)
{
    if (parentCode.isEmpty() || childCode.size() <= parentCode.size())
        return false;
    if (!childCode.startsWith(parentCode))
        return false;

    // Existing dotted parents must match on a dot boundary. This keeps
    // sibling codes like 1221.03 and 1221.030 from being treated as parent/child.
    if (parentCode.contains('.'))
        return childCode.at(parentCode.size()) == '.';

    return true;
}

QString childSegmentFromOldCode(const QString &parentOldCode, const QString &oldCode)
{
    QString segment = oldCode.mid(parentOldCode.size());
    while (segment.startsWith('.'))
        segment.remove(0, 1);
    return normalizeCodePart(segment);
}

QString makeChildCode(const QString &parentNewCode, const QString &segment)
{
    if (parentNewCode.isEmpty())
        return segment;
    return parentNewCode + "." + segment;
}

bool isAvailableCode(const QString &candidate, const QString &currentOldCode, const QSet<QString> &usedNewCodes, const QSet<QString> &allOldCodes)
{
    if (!isOutputCode(candidate))
        return false;
    if (usedNewCodes.contains(candidate))
        return false;
    if (candidate != currentOldCode && allOldCodes.contains(candidate))
        return false;
    return true;
}

QString allocateRootCode(const QString &preferredCode, const QString &currentOldCode, const QSet<QString> &usedNewCodes, const QSet<QString> &allOldCodes)
{
    if (isAvailableCode(preferredCode, currentOldCode, usedNewCodes, allOldCodes))
        return preferredCode;

    if (isDigits(preferredCode))
    {
        bool ok = false;
        int number = preferredCode.toInt(&ok);
        const int width = preferredCode.size();
        if (ok)
        {
            for (int i = number + 1; i < 10000; ++i)
            {
                QString candidate = QString::number(i);
                if (candidate.size() < width)
                    candidate = candidate.rightJustified(width, '0');
                if (isAvailableCode(candidate, currentOldCode, usedNewCodes, allOldCodes))
                    return candidate;
            }
        }
    }

    const QString baseCode = digitsOnly(preferredCode).isEmpty()
        ? QStringLiteral("1")
        : digitsOnly(preferredCode);
    for (int i = 1; i < 10000; ++i)
    {
        const QString candidate = baseCode + QString::number(i);
        if (isAvailableCode(candidate, currentOldCode, usedNewCodes, allOldCodes))
            return candidate;
    }

    return QString();
}

QString allocateChildCode(const QString &parentNewCode, const QString &preferredSegment, const QString &currentOldCode, const QSet<QString> &usedNewCodes, const QSet<QString> &allOldCodes)
{
    const QString preferredCode = makeChildCode(parentNewCode, preferredSegment);
    if (isAvailableCode(preferredCode, currentOldCode, usedNewCodes, allOldCodes))
        return preferredCode;

    if (isDigits(preferredSegment))
    {
        bool ok = false;
        int number = preferredSegment.toInt(&ok);
        const int width = preferredSegment.size();
        if (ok)
        {
            for (int i = number + 1; i < 10000; ++i)
            {
                QString segment = QString::number(i);
                if (segment.size() < width)
                    segment = segment.rightJustified(width, '0');

                const QString candidate = makeChildCode(parentNewCode, segment);
                if (isAvailableCode(candidate, currentOldCode, usedNewCodes, allOldCodes))
                    return candidate;
            }
        }
    }

    for (int i = 1; i < 10000; ++i)
    {
        const QString candidate = makeChildCode(parentNewCode, preferredSegment + QString::number(i));
        if (isAvailableCode(candidate, currentOldCode, usedNewCodes, allOldCodes))
            return candidate;
    }

    return QString();
}

QList<SubjectCodeEntry> buildCodeMapping(const QJsonArray &accountList, QMap<QString, QString> *oldToNew)
{
    QList<SubjectCodeEntry> entries;
    QMap<QString, int> oldCodeToEntryIndex;
    QSet<QString> allOldCodes;

    for (int i = 0; i < accountList.size(); ++i)
    {
        const QJsonObject account = accountList.at(i).toObject();
        const QString code = codeFromValue(account.value("AcctCode"));
        if (code.isEmpty() || oldCodeToEntryIndex.contains(code))
            continue;

        SubjectCodeEntry entry;
        entry.oldCode = code;
        entry.originalIndex = i;
        entries.append(entry);
        oldCodeToEntryIndex.insert(code, entries.size() - 1);
        allOldCodes.insert(code);
    }

    QList<int> sortedIndexes;
    for (int i = 0; i < entries.size(); ++i)
        sortedIndexes.append(i);
    std::sort(sortedIndexes.begin(), sortedIndexes.end(), [&entries](int a, int b) {
        return entries[a].oldCode < entries[b].oldCode;
    });

    QMap<QString, QString> localOldToNew;
    QSet<QString> usedNewCodes;

    for (int sortedPos = 0; sortedPos < sortedIndexes.size(); ++sortedPos)
    {
        SubjectCodeEntry &entry = entries[sortedIndexes[sortedPos]];

        int parentEntryIndex = -1;
        for (int prev = sortedPos - 1; prev >= 0; --prev)
        {
            const int candidateIndex = sortedIndexes[prev];
            if (isParentPrefix(entries[candidateIndex].oldCode, entry.oldCode))
            {
                parentEntryIndex = candidateIndex;
                break;
            }
        }

        if (parentEntryIndex < 0)
        {
            entry.level = 1;
            QString preferredCode = normalizeCodePart(entry.oldCode);
            if (preferredCode.isEmpty())
                preferredCode = QStringLiteral("1");

            QString newCode = allocateRootCode(preferredCode, entry.oldCode, usedNewCodes, allOldCodes);
            if (newCode.isEmpty())
                newCode = preferredCode;

            entry.newCode = newCode;
            entry.rootOldCode = entry.oldCode;
            entry.rootNewCode = entry.newCode;
            entry.reassigned = newCode != preferredCode;
        }
        else
        {
            const SubjectCodeEntry &parent = entries[parentEntryIndex];
            entry.parentOldCode = parent.oldCode;
            entry.parentNewCode = parent.newCode;
            entry.level = parent.level + 1;
            entry.rootOldCode = parent.rootOldCode;
            entry.rootNewCode = parent.rootNewCode;

            QString segment = childSegmentFromOldCode(parent.oldCode, entry.oldCode);
            if (segment.isEmpty())
                segment = QStringLiteral("01");

            const QString preferredCode = makeChildCode(parent.newCode, segment);
            QString newCode = allocateChildCode(parent.newCode, segment, entry.oldCode, usedNewCodes, allOldCodes);
            if (newCode.isEmpty())
                newCode = preferredCode;

            entry.newCode = newCode;
            entry.reassigned = newCode != preferredCode;
        }

        entry.changed = entry.oldCode != entry.newCode;
        usedNewCodes.insert(entry.newCode);
        localOldToNew.insert(entry.oldCode, entry.newCode);
    }

    if (oldToNew)
        *oldToNew = localOldToNew;
    return entries;
}

QJsonValue updateAcctCodeReferences(const QJsonValue &value, const QMap<QString, QString> &oldToNew, int *updateCount, int *spaceCleanupCount)
{
    if (value.isArray())
    {
        QJsonArray result;
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array)
            result.append(updateAcctCodeReferences(item, oldToNew, updateCount, spaceCleanupCount));
        return result;
    }

    if (!value.isObject())
        return value;

    QJsonObject object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it)
    {
        if (it.key() == "AcctCode")
        {
            const QString normalizedCode = codeFromValue(it.value());
            if (normalizedCode.isEmpty())
                continue;

            const QString currentCode = it.value().isString()
                ? it.value().toString()
                : codeFromValue(it.value());
            const bool hasSpaces = it.value().isString() && containsCodeSpace(currentCode);
            const QString newCode = oldToNew.value(normalizedCode, normalizedCode);
            if (newCode != currentCode)
            {
                it.value() = newCode;
                if (updateCount)
                    ++(*updateCount);
                if (hasSpaces && spaceCleanupCount)
                    ++(*spaceCleanupCount);
            }
        }
        else
        {
            it.value() = updateAcctCodeReferences(it.value(), oldToNew, updateCount, spaceCleanupCount);
        }
    }
    return object;
}

QJsonArray applyAccountMapping(const QJsonArray &accountList, const QList<SubjectCodeEntry> &entries)
{
    QMap<QString, SubjectCodeEntry> entryByOldCode;
    for (const SubjectCodeEntry &entry : entries)
        entryByOldCode.insert(entry.oldCode, entry);

    QJsonArray result;
    for (int i = 0; i < accountList.size(); ++i)
    {
        QJsonObject account = accountList.at(i).toObject();
        const QString oldCode = codeFromValue(account.value("AcctCode"));
        if (!entryByOldCode.contains(oldCode))
        {
            result.append(account);
            continue;
        }

        const SubjectCodeEntry entry = entryByOldCode.value(oldCode);
        account["AcctCode"] = entry.newCode;
        account["AcctParent"] = entry.parentNewCode.isEmpty() ? QStringLiteral("0") : entry.parentNewCode;
        account["AcctLevel"] = entry.level;

        const QString fullCode = codeFromValue(account.value("AcctFullCode"));
        if (!fullCode.isEmpty() && fullCode == entry.oldCode)
            account["AcctFullCode"] = entry.newCode;

        const QString rootCode = codeFromValue(account.value("AcctRoot"));
        if (!rootCode.isEmpty() && rootCode == entry.rootOldCode)
            account["AcctRoot"] = entry.rootNewCode;

        result.append(account);
    }
    return result;
}

QString csvEscape(const QString &value)
{
    QString escaped = value;
    escaped.replace("\"", "\"\"");
    return "\"" + escaped + "\"";
}

bool writeMappingFile(const QString &mappingPath, const QList<SubjectCodeEntry> &entries, QString *errorMessage)
{
    AppLogger::operation(QStringLiteral("写入科目编码映射 mapping=%1 entries=%2")
        .arg(QDir::toNativeSeparators(mappingPath))
        .arg(entries.size()));
    QSaveFile file(mappingPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("写入科目映射文件失败");
        return false;
    }

    file.write("\xEF\xBB\xBF");
    file.write("OldCode,NewCode,ParentOldCode,ParentNewCode,Level,Reassigned\n");
    for (const SubjectCodeEntry &entry : entries)
    {
        const QString line = QString("%1,%2,%3,%4,%5,%6\n")
            .arg(csvEscape(entry.oldCode),
                 csvEscape(entry.newCode),
                 csvEscape(entry.parentOldCode),
                 csvEscape(entry.parentNewCode),
                 QString::number(entry.level),
                 entry.reassigned ? QStringLiteral("1") : QStringLiteral("0"));
        file.write(line.toUtf8());
    }

    if (!file.commit())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("保存科目映射文件失败");
        return false;
    }
    return true;
}

bool processJsonFile(const QString &jsonPath, SubjectCodeProcessResult *summary, QList<SubjectCodeEntry> *allEntries, QString *errorMessage,
                     bool preserveNonAccountJson = false)
{
    AppLogger::operation(QStringLiteral("开始处理科目编码 JSON path=%1").arg(QDir::toNativeSeparators(jsonPath)));
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("打开 JSON 失败：") + QDir::toNativeSeparators(jsonPath);
        return false;
    }

    const QByteArray jsonData = file.readAll();
    file.close();

    QJsonParseError jsonError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("JSON 解析失败：") + jsonError.errorString();
        return false;
    }

    QJsonObject root = doc.object();
    // ZIP packages may also contain report data such as FinancialTables.json.
    // Leave companion files byte-for-byte intact; an explicitly present but
    // invalid/empty account list must still fail validation below.
    if (preserveNonAccountJson && !root.contains(QStringLiteral("GLAccountList")))
    {
        if (summary)
            summary->logs.append(QStringLiteral("原样保留非科目 JSON：") + QFileInfo(jsonPath).fileName());
        return true;
    }
    if (summary && summary->accountSetName.isEmpty())
    {
        summary->accountSetName = root.value(QStringLiteral("AccountSet"))
            .toObject()
            .value(QStringLiteral("AsName"))
            .toString()
            .trimmed();
    }
    const QJsonArray accountList = root.value("GLAccountList").toArray();
    if (accountList.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("未找到 GLAccountList 科目数据");
        return false;
    }

    const int accountCodesWithSpaces = countAccountCodesWithSpaces(accountList);
    if (accountCodesWithSpaces > 0)
    {
        AppLogger::operation(QStringLiteral("检测到科目编码包含空格 path=%1 count=%2")
            .arg(QDir::toNativeSeparators(jsonPath))
            .arg(accountCodesWithSpaces));
    }

    QMap<QString, QString> oldToNew;
    QList<SubjectCodeEntry> entries = buildCodeMapping(accountList, &oldToNew);
    root["GLAccountList"] = applyAccountMapping(accountList, entries);

    int referenceUpdates = 0;
    int referenceCodeSpaces = 0;
    for (auto it = root.begin(); it != root.end(); ++it)
    {
        if (it.key() == "GLAccountList")
            continue;
        it.value() = updateAcctCodeReferences(it.value(), oldToNew, &referenceUpdates, &referenceCodeSpaces);
    }

    QSaveFile saveFile(jsonPath);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("写入修改后的 JSON 失败");
        return false;
    }
    const QByteArray outputJson = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (saveFile.write(outputJson) != outputJson.size())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("写入修改后的 JSON 失败：") + saveFile.errorString();
        AppLogger::error(QStringLiteral("写入修改后的 JSON 失败 path=%1 error=%2")
            .arg(QDir::toNativeSeparators(jsonPath), saveFile.errorString()));
        return false;
    }

    if (!saveFile.commit())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("保存修改后的 JSON 失败：") + saveFile.errorString();
        AppLogger::error(QStringLiteral("保存修改后的 JSON 失败 path=%1 error=%2")
            .arg(QDir::toNativeSeparators(jsonPath), saveFile.errorString()));
        return false;
    }

    int changedAccounts = 0;
    int reassignedAccounts = 0;
    for (const SubjectCodeEntry &entry : entries)
    {
        if (entry.changed)
            ++changedAccounts;
        if (entry.reassigned)
            ++reassignedAccounts;
    }

    if (summary)
    {
        ++summary->jsonCount;
        summary->accountCount += entries.size();
        summary->changedAccountCount += changedAccounts;
        summary->reassignedCount += reassignedAccounts;
        summary->referenceUpdateCount += referenceUpdates;
        summary->accountCodeSpaceCount += accountCodesWithSpaces;
        summary->referenceCodeSpaceCount += referenceCodeSpaces;
        summary->logs.append(QStringLiteral("%1：科目 %2 个，科目表去空格 %3 个，引用去空格 %4 处，改码 %5 个，冲突重分配 %6 个，引用更新 %7 处")
            .arg(QFileInfo(jsonPath).fileName())
            .arg(entries.size())
            .arg(accountCodesWithSpaces)
            .arg(referenceCodeSpaces)
            .arg(changedAccounts)
            .arg(reassignedAccounts)
            .arg(referenceUpdates));
    }

    if (allEntries)
        allEntries->append(entries);
    AppLogger::operation(QStringLiteral("完成处理科目编码 JSON path=%1 accounts=%2 accountSpaces=%3 referenceSpaces=%4 changed=%5 reassigned=%6 references=%7")
        .arg(QDir::toNativeSeparators(jsonPath))
        .arg(entries.size())
        .arg(accountCodesWithSpaces)
        .arg(referenceCodeSpaces)
        .arg(changedAccounts)
        .arg(reassignedAccounts)
        .arg(referenceUpdates));
    return true;
}

bool runPowerShell(const QString &script, const QProcessEnvironment &env, int timeoutMs, QString *errorMessage)
{
    QProcess process;
    process.setProcessEnvironment(env);
    const QStringList args = QStringList()
        << "-NoProfile"
        << "-ExecutionPolicy"
        << "Bypass"
        << "-Command"
        << script;
    AppLogger::command("powershell.exe", args);
    process.start("powershell.exe", args);

    if (!process.waitForFinished(timeoutMs))
    {
        process.kill();
        process.waitForFinished(3000);
        if (errorMessage)
            *errorMessage = QStringLiteral("执行 PowerShell 超时");
        AppLogger::error(QStringLiteral("PowerShell 执行超时 timeoutMs=%1").arg(timeoutMs));
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        const QString stdErr = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (errorMessage)
            *errorMessage = stdErr.isEmpty() ? QStringLiteral("执行 PowerShell 失败") : stdErr;
        AppLogger::error(QStringLiteral("PowerShell 执行失败 exitCode=%1 error=%2").arg(process.exitCode()).arg(stdErr));
        return false;
    }
    AppLogger::operation(QStringLiteral("PowerShell 执行成功"));
    return true;
}

bool extractJsonFilesFromZip(const QString &zipPath, const QString &destDir, QString *errorMessage)
{
    AppLogger::operation(QStringLiteral("解压 ZIP 中 JSON zip=%1 dest=%2")
        .arg(QDir::toNativeSeparators(zipPath), QDir::toNativeSeparators(destDir)));
    const QString script =
        "$ErrorActionPreference='Stop';"
        "Add-Type -AssemblyName System.IO.Compression;"
        "Add-Type -AssemblyName System.IO.Compression.FileSystem;"
        "$zip=$env:LEMON_SUBJECT_ZIP_PATH;$dest=$env:LEMON_SUBJECT_JSON_DEST;"
        "if([string]::IsNullOrWhiteSpace($zip)){throw 'ZIP path is empty'}"
        "if([string]::IsNullOrWhiteSpace($dest)){throw 'Destination path is empty'}"
        "$archive=[System.IO.Compression.ZipFile]::OpenRead($zip);"
        "$count=0;"
        "try{"
        "  foreach($entry in $archive.Entries){"
        "    if([string]::IsNullOrWhiteSpace($entry.Name)){continue}"
        "    if(-not $entry.Name.EndsWith('.json',[System.StringComparison]::OrdinalIgnoreCase)){continue}"
        "    $name=[System.IO.Path]::GetFileName($entry.Name);"
        "    if([string]::IsNullOrWhiteSpace($name)){continue}"
        "    $target=Join-Path $dest $name;"
        "    if(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target -Force}"
        "    [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$target);"
        "    $count++;"
        "  }"
        "}finally{$archive.Dispose()}"
        "if($count -eq 0){throw 'No JSON files found in ZIP'}";

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LEMON_SUBJECT_ZIP_PATH", QFileInfo(zipPath).absoluteFilePath());
    env.insert("LEMON_SUBJECT_JSON_DEST", QDir::toNativeSeparators(destDir));
    return runPowerShell(script, env, 60000, errorMessage);
}

bool zipDirectory(const QString &sourceDir, const QString &zipPath, QString *errorMessage)
{
    AppLogger::operation(QStringLiteral("压缩目录 source=%1 zip=%2")
        .arg(QDir::toNativeSeparators(sourceDir), QDir::toNativeSeparators(zipPath)));
    const QString script =
        "$ErrorActionPreference='Stop';"
        "Add-Type -AssemblyName System.IO.Compression;"
        "Add-Type -AssemblyName System.IO.Compression.FileSystem;"
        "$src=$env:LEMON_SUBJECT_ZIP_SRC;$zip=$env:LEMON_SUBJECT_ZIP_OUT;"
        "if(Test-Path -LiteralPath $zip){Remove-Item -LiteralPath $zip -Force}"
        "[System.IO.Compression.ZipFile]::CreateFromDirectory($src,$zip,"
        "[System.IO.Compression.CompressionLevel]::Optimal,$false)";

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LEMON_SUBJECT_ZIP_SRC", QDir::toNativeSeparators(sourceDir));
    env.insert("LEMON_SUBJECT_ZIP_OUT", QDir::toNativeSeparators(zipPath));
    return runPowerShell(script, env, 60000, errorMessage);
}

QString uniqueFilePath(const QString &dirPath, const QString &baseName, const QString &suffix)
{
    QString path = QDir(dirPath).absoluteFilePath(baseName + suffix);
    int index = 1;
    while (QFile::exists(path))
        path = QDir(dirPath).absoluteFilePath(baseName + QString("_%1").arg(index++) + suffix);
    return path;
}

SubjectCodeProcessResult processSubjectCodeInput(const QString &inputPath, const QString &workRoot, const QString &outputRoot)
{
    SubjectCodeProcessResult result;
    result.inputPath = inputPath;
    AppLogger::operation(QStringLiteral("开始科目编码添加分隔符 input=%1 workRoot=%2 outputRoot=%3")
        .arg(QDir::toNativeSeparators(inputPath),
             QDir::toNativeSeparators(workRoot),
             QDir::toNativeSeparators(outputRoot)));

    const QFileInfo inputInfo(inputPath);
    if (!inputInfo.exists() || !inputInfo.isFile())
    {
        result.errorMessage = QStringLiteral("文件不存在：") + QDir::toNativeSeparators(inputPath);
        return result;
    }

    QDir rootDir(workRoot);
    if (!rootDir.exists() && !rootDir.mkpath("."))
    {
        result.errorMessage = QStringLiteral("创建工作目录失败：") + QDir::toNativeSeparators(workRoot);
        return result;
    }

    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz");
    const QString jobDirPath = rootDir.absoluteFilePath(inputInfo.completeBaseName() + "_" + timestamp);
    const QString jsonDirPath = QDir(jobDirPath).absoluteFilePath("json");
    const QString outputDirPath = QDir::cleanPath(outputRoot);

    if (!QDir().mkpath(jsonDirPath) || !QDir().mkpath(outputDirPath))
    {
        result.errorMessage = QStringLiteral("创建处理目录失败");
        return result;
    }

    if (inputInfo.suffix().compare("zip", Qt::CaseInsensitive) == 0)
    {
        QString errorMessage;
        if (!extractJsonFilesFromZip(inputPath, jsonDirPath, &errorMessage))
        {
            result.errorMessage = errorMessage;
            return result;
        }
    }
    else if (inputInfo.suffix().compare("json", Qt::CaseInsensitive) == 0)
    {
        const QString targetPath = QDir(jsonDirPath).absoluteFilePath(QStringLiteral("AccountSet.json"));
        AppLogger::operation(QStringLiteral("复制 JSON 到工作目录 source=%1 target=%2")
            .arg(QDir::toNativeSeparators(inputInfo.absoluteFilePath()), QDir::toNativeSeparators(targetPath)));
        if (!QFile::copy(inputInfo.absoluteFilePath(), targetPath))
        {
            result.errorMessage = QStringLiteral("复制 JSON 到工作目录失败");
            return result;
        }
    }
    else
    {
        result.errorMessage = QStringLiteral("仅支持 JSON 或 ZIP 文件");
        return result;
    }

    QList<SubjectCodeEntry> allEntries;
    QDirIterator it(jsonDirPath, QStringList() << "*.json", QDir::Files, QDirIterator::NoIteratorFlags);
    while (it.hasNext())
    {
        QString errorMessage;
        const QString jsonPath = it.next();
        if (!processJsonFile(jsonPath, &result, &allEntries, &errorMessage,
                             inputInfo.suffix().compare("zip", Qt::CaseInsensitive) == 0))
        {
            result.errorMessage = QFileInfo(jsonPath).fileName() + QStringLiteral("：") + errorMessage;
            return result;
        }
    }

    if (result.jsonCount <= 0)
    {
        result.errorMessage = QStringLiteral("未找到 GLAccountList 科目数据：输入文件中没有可处理的科目 JSON");
        return result;
    }

    const QString outputBaseName = RepairFileNameUtils::makeBaseName(
        result.accountSetName,
        inputInfo.completeBaseName(),
        QStringLiteral("科目编码分隔符修复"));
    result.outputZipPath = uniqueFilePath(outputDirPath, outputBaseName, ".zip");
    result.mappingPath = uniqueFilePath(outputDirPath, outputBaseName + QStringLiteral("_编码映射"), ".csv");

    QString errorMessage;
    if (!writeMappingFile(result.mappingPath, allEntries, &errorMessage))
    {
        result.errorMessage = errorMessage;
        return result;
    }

    if (!zipDirectory(jsonDirPath, result.outputZipPath, &errorMessage))
    {
        result.errorMessage = errorMessage;
        return result;
    }

    result.ok = true;
    result.logs.prepend(QStringLiteral("输入文件：") + QDir::toNativeSeparators(inputPath));
    result.logs.append(QStringLiteral("输出压缩包：") + QDir::toNativeSeparators(result.outputZipPath));
    result.logs.append(QStringLiteral("编码映射：") + QDir::toNativeSeparators(result.mappingPath));
    AppLogger::operation(QStringLiteral("完成科目编码添加分隔符 output=%1 mapping=%2 jsonCount=%3 accounts=%4 accountSpaces=%5 referenceSpaces=%6 changed=%7 reassigned=%8 references=%9")
        .arg(QDir::toNativeSeparators(result.outputZipPath),
             QDir::toNativeSeparators(result.mappingPath))
        .arg(result.jsonCount)
        .arg(result.accountCount)
        .arg(result.accountCodeSpaceCount)
        .arg(result.referenceCodeSpaceCount)
        .arg(result.changedAccountCount)
        .arg(result.reassignedCount)
        .arg(result.referenceUpdateCount));
    return result;
}
}

SubjectCodeSeparatorWidget::SubjectCodeSeparatorWidget(GlobalData *globalData, QWidget *parent)
    : QWidget(parent),
      m_pGlobalData(globalData),
      m_pDirLabel(nullptr),
      m_pPathConfig(nullptr),
      m_pInputFilePicker(nullptr),
      m_pDropLabel(nullptr),
      m_pStatusLabel(nullptr),
      m_pLogEdit(nullptr),
      m_pBtnBrowse(nullptr),
      m_pBtnOutputDir(nullptr),
      m_pBtnBack(nullptr),
      m_requestId(0),
      m_batchProcessing(false),
      m_batchSuccessCount(0),
      m_batchFailureCount(0)
{
    setAcceptDrops(true);
    m_outputDir = defaultOutputDir();
    setupUi();
    refreshInputFiles();
}

void SubjectCodeSeparatorWidget::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    QHBoxLayout *titleLayout = new QHBoxLayout();
    QLabel *titleLabel = new QLabel(QStringLiteral("科目编码添加分隔符"), this);
    titleLabel->setObjectName("labelPageTitle");
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();

    m_pBtnBrowse = new QPushButton(QStringLiteral("选择文件"), this);
    m_pBtnBrowse->setObjectName("btnBrowseSubjectCodePackage");
    m_pBtnBrowse->setProperty("flatGreen", true);
    m_pBtnBrowse->setMaximumWidth(100);
    titleLayout->addWidget(m_pBtnBrowse);
    layout->addLayout(titleLayout);

    m_pPathConfig = new RepairPathConfigWidget(this);
    m_pPathConfig->setPathsChangedCallback([this]() { refreshInputFiles(); });
    layout->addWidget(m_pPathConfig);

    m_pInputFilePicker = new RepairInputFilePickerWidget(
        QStringList() << QStringLiteral("*.zip") << QStringLiteral("*.json"), this);
    m_pInputFilePicker->setProcessCallback(
        [this](const QStringList &paths) { processSelectedFiles(paths); });
    m_pInputFilePicker->setFilesChangedCallback([this](int count) {
        if (!m_batchProcessing && m_pStatusLabel)
            m_pStatusLabel->setText(QStringLiteral("\u5df2\u626b\u63cf\u8f93\u5165\u76ee\u5f55\uff0c\u53d1\u73b0 %1 \u4e2a JSON/ZIP").arg(count));
    });
    layout->addWidget(m_pInputFilePicker);

    m_pDropLabel = new QLabel(QStringLiteral("将 AccountSet.json 或 ZIP 压缩包拖入此处，系统会重写科目编码并生成新的 ZIP"), this);
    m_pDropLabel->setObjectName("labelRepairDropArea");
    m_pDropLabel->setAlignment(Qt::AlignCenter);
    m_pDropLabel->setMinimumHeight(80);
    m_pDropLabel->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(m_pDropLabel);

    m_pStatusLabel = new QLabel(QStringLiteral("待处理"), this);
    m_pStatusLabel->setWordWrap(true);
    layout->addWidget(m_pStatusLabel);

    m_pLogEdit = new QTextEdit(this);
    m_pLogEdit->setObjectName("subjectCodeLog");
    m_pLogEdit->setReadOnly(true);
    m_pLogEdit->setMinimumHeight(220);
    layout->addWidget(m_pLogEdit, 1);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    m_pBtnBack = new QPushButton(QStringLiteral("返回"), this);
    m_pBtnBack->setObjectName("btnBackToMain");
    m_pBtnBack->setProperty("grayButton", true);
    m_pBtnBack->setMaximumWidth(100);
    m_pBtnBack->setMinimumHeight(36);
    btnLayout->addWidget(m_pBtnBack);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    connect(m_pBtnBrowse, &QPushButton::clicked, this, &SubjectCodeSeparatorWidget::onBrowseClicked);
    connect(m_pBtnBack, &QPushButton::clicked, this, &SubjectCodeSeparatorWidget::onBackClicked);
}

QString SubjectCodeSeparatorWidget::workRootDir() const
{
    return QDir::cleanPath(qApp->applicationDirPath() + "/SubjectCodeSeparatorPackages");
}

QString SubjectCodeSeparatorWidget::defaultOutputDir() const
{
    return RepairPathSettings::outputDir();
}

QString SubjectCodeSeparatorWidget::outputDir() const
{
    return RepairPathSettings::outputDir();
}

void SubjectCodeSeparatorWidget::updateOutputDirLabel()
{
    if (m_pPathConfig)
        m_pPathConfig->refreshPaths();
}

void SubjectCodeSeparatorWidget::setRunning(bool running, const QString &message)
{
    setProperty("sessionBusysetRunning", running);
    m_pBtnBrowse->setEnabled(!running);
    if (m_pBtnOutputDir)
        m_pBtnOutputDir->setEnabled(!running);
    if (m_pPathConfig)
        m_pPathConfig->setEnabled(!running);
    if (m_pInputFilePicker)
        m_pInputFilePicker->setRunning(running);
    m_pBtnBack->setEnabled(!running);
    setAcceptDrops(!running);
    if (!message.isEmpty())
        m_pStatusLabel->setText(message);
}

void SubjectCodeSeparatorWidget::refreshInputFiles()
{
    if (m_pPathConfig)
        m_pPathConfig->refreshPaths();
    if (m_pInputFilePicker)
        m_pInputFilePicker->refreshFiles();
}

void SubjectCodeSeparatorWidget::processSelectedFiles(const QStringList &filePaths)
{
    if (filePaths.isEmpty())
        return;
    m_batchProcessing = true;
    m_batchSuccessCount = 0;
    m_batchFailureCount = 0;
    m_batchResults.clear();
    m_pendingBatchPaths = filePaths;
    m_pLogEdit->clear();
    setRunning(true, QStringLiteral("\u6b63\u5728\u6309\u987a\u5e8f\u5904\u7406 %1 \u4e2a\u6587\u4ef6...").arg(filePaths.size()));
    processNextBatchFile();
}

void SubjectCodeSeparatorWidget::processNextBatchFile()
{
    if (!m_batchProcessing)
        return;
    if (!m_pendingBatchPaths.isEmpty())
    {
        const QString path = m_pendingBatchPaths.takeFirst();
        m_pLogEdit->append(QStringLiteral("\n===== %1 =====").arg(QFileInfo(path).fileName()));
        startProcessZip(path);
        return;
    }

    m_batchProcessing = false;
    setRunning(false, QStringLiteral("\u6279\u91cf\u5904\u7406\u5b8c\u6210\uff1a\u6210\u529f %1\uff0c\u5931\u8d25 %2")
        .arg(m_batchSuccessCount)
        .arg(m_batchFailureCount));
    QMessageBox::information(this, QStringLiteral("\u79d1\u76ee\u7f16\u7801\u6279\u91cf\u5904\u7406"),
        QStringLiteral("\u6210\u529f\uff1a%1\n\u5931\u8d25\uff1a%2\n\n%3")
            .arg(m_batchSuccessCount)
            .arg(m_batchFailureCount)
            .arg(m_batchResults.join(QLatin1Char('\n'))));
}

void SubjectCodeSeparatorWidget::startProcessZip(const QString &zipPath)
{
    if (zipPath.isEmpty())
        return;

    if (!isSupportedInputFile(zipPath))
    {
        AppLogger::warning(QStringLiteral("科目编码添加分隔符选择了不支持的文件 path=%1").arg(QDir::toNativeSeparators(zipPath)));
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请选择 JSON 或 ZIP 文件"));
        return;
    }

    const int requestId = ++m_requestId;
    const QString inputPath = zipPath;
    const QString rootPath = workRootDir();
    const QString outputPath = outputDir();
    setRunning(true, QStringLiteral("正在后台处理科目编码..."));
    if (m_batchProcessing)
        m_pLogEdit->append(QStringLiteral("正在处理：") + QDir::toNativeSeparators(inputPath));
    else
        m_pLogEdit->setPlainText(QStringLiteral("正在处理：") + QDir::toNativeSeparators(inputPath));
    AppLogger::operation(QStringLiteral("后台处理科目编码任务启动 requestId=%1 input=%2")
        .arg(requestId)
        .arg(QDir::toNativeSeparators(inputPath)));

    QSharedPointer<SubjectCodeProcessResult> result(new SubjectCodeProcessResult);
    QThread *thread = QThread::create([result, inputPath, rootPath, outputPath]() {
        *result = processSubjectCodeInput(inputPath, rootPath, outputPath);
    });

    connect(thread, &QThread::finished, this, [this, result, requestId]() {
        if (requestId != m_requestId)
            return;

        if (!m_batchProcessing)
            setRunning(false);
        if (!result->ok)
        {
            AppLogger::error(QStringLiteral("科目编码添加分隔符处理失败 requestId=%1 error=%2")
                .arg(requestId)
                .arg(result->errorMessage));
            if (m_batchProcessing)
            {
                ++m_batchFailureCount;
                m_batchResults.append(QFileInfo(result->inputPath).fileName()
                    + QStringLiteral("：失败 - ") + result->errorMessage);
                m_pLogEdit->append(result->logs.join("\n") + "\n" + result->errorMessage);
                processNextBatchFile();
                return;
            }
            m_pStatusLabel->setText(QStringLiteral("处理失败：") + result->errorMessage);
            m_pLogEdit->setPlainText(result->logs.join("\n") + "\n" + result->errorMessage);
            QMessageBox::warning(this, QStringLiteral("处理失败"), result->errorMessage);
            return;
        }

        m_pStatusLabel->setText(QStringLiteral("处理完成：科目 %1 个，科目表去空格 %2 个，引用去空格 %3 处，改码 %4 个，引用更新 %5 处")
            .arg(result->accountCount)
            .arg(result->accountCodeSpaceCount)
            .arg(result->referenceCodeSpaceCount)
            .arg(result->changedAccountCount)
            .arg(result->referenceUpdateCount));
        if (m_batchProcessing)
            m_pLogEdit->append(result->logs.join("\n"));
        else
            m_pLogEdit->setPlainText(result->logs.join("\n"));
        AppLogger::operation(QStringLiteral("科目编码添加分隔符处理完成 requestId=%1 output=%2")
            .arg(requestId)
            .arg(QDir::toNativeSeparators(result->outputZipPath)));
        if (m_batchProcessing)
        {
            ++m_batchSuccessCount;
            m_batchResults.append(QFileInfo(result->inputPath).fileName()
                + QStringLiteral("：成功 -> ")
                + QDir::toNativeSeparators(result->outputZipPath));
            processNextBatchFile();
            return;
        }
        QMessageBox::information(this, QStringLiteral("科目编码添加分隔符"),
            QStringLiteral("处理完成，已生成压缩包：\n%1\n\n编码映射文件：\n%2")
                .arg(QDir::toNativeSeparators(result->outputZipPath),
                     QDir::toNativeSeparators(result->mappingPath)));
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void SubjectCodeSeparatorWidget::onBrowseClicked()
{
    AppLogger::operation(QStringLiteral("科目编码添加分隔符选择文件"));
    const QString filePath = QFileDialog::getOpenFileName(this,
        QStringLiteral("选择 AccountSet.json 或 ZIP"),
        RepairPathSettings::inputDir(),
        QStringLiteral("账套数据 (*.json *.zip);;JSON 文件 (*.json);;ZIP 压缩包 (*.zip)"));

    if (!filePath.isEmpty())
    {
        AppLogger::operation(QStringLiteral("科目编码添加分隔符已选择文件 path=%1").arg(QDir::toNativeSeparators(filePath)));
        startProcessZip(filePath);
    }
}

void SubjectCodeSeparatorWidget::onBrowseOutputDirClicked()
{
    AppLogger::operation(QStringLiteral("选择科目编码输出目录"));
    const QString dirPath = QFileDialog::getExistingDirectory(this,
        QStringLiteral("选择输出目录"),
        outputDir(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (dirPath.isEmpty())
        return;

    RepairPathSettings::setOutputDir(dirPath);
    updateOutputDirLabel();
    AppLogger::operation(QStringLiteral("已选择科目编码输出目录 dir=%1")
        .arg(QDir::toNativeSeparators(RepairPathSettings::outputDir())));
}

void SubjectCodeSeparatorWidget::onBackClicked()
{
    AppLogger::operation(QStringLiteral("科目编码添加分隔符返回主界面"));
    emit backToMain();
}

void SubjectCodeSeparatorWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (hasSupportedInputFile(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void SubjectCodeSeparatorWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (hasSupportedInputFile(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void SubjectCodeSeparatorWidget::dropEvent(QDropEvent *event)
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
            AppLogger::operation(QStringLiteral("科目编码添加分隔符拖入文件 path=%1").arg(QDir::toNativeSeparators(url.toLocalFile())));
            startProcessZip(inputPath);
            return;
        }
    }
    event->ignore();
}
