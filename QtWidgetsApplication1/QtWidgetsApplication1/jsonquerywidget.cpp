#include "jsonquerywidget.h"
#include "Common/repairinputfileutils.h"
#include "Common/globaldata.h"
#include "Common/logger.h"

#include <algorithm>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QDate>
#include <QDir>
#include <QDirIterator>
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
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QModelIndex>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSharedPointer>
#include <QShortcut>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>

namespace
{
const int kMaxResultRows = 3000;

struct JsonQueryIndexData
{
    QMap<QString, QString> acctNameByCode;
    QMap<QString, QString> acctNameByYearAndCode;
    QMap<QString, QStringList> acctProjTypesByCode;
    QMap<QString, QStringList> acctProjTypesByYearAndCode;
    QMap<QString, QString> projectTypeNames;
};

struct JsonQueryFilter
{
    QString source;
    QString year;
    QString acctCode;
    QString customerCode;
    QString supplierCode;
    QString keyword;
};

struct JsonQueryScanResult
{
    QList<JsonQueryFileInfo> files;
    QString errorMessage;
    bool ok = false;
};

struct JsonQueryImportResult
{
    int successCount = 0;
    QStringList errors;
};

struct JsonQueryLoadResult
{
    QJsonDocument doc;
    JsonQueryIndexData indexes;
    QString jsonPath;
    QString errorMessage;
    bool ok = false;
};

struct JsonQueryResult
{
    QList<JsonQueryRecord> records;
    QStringList columns;
    int total = 0;
};

bool isSupportedDataFile(const QString &filePath)
{
    QFileInfo fileInfo(filePath);
    if (!fileInfo.isFile())
        return false;

    const QString suffix = fileInfo.suffix();
    return suffix.compare("zip", Qt::CaseInsensitive) == 0
        || suffix.compare("json", Qt::CaseInsensitive) == 0;
}

bool hasSupportedDataFile(const QMimeData *mimeData)
{
    if (!mimeData || !mimeData->hasUrls())
        return false;

    for (const QUrl &url : mimeData->urls())
    {
        if (url.isLocalFile() && isSupportedDataFile(url.toLocalFile()))
            return true;
    }

    return false;
}

QString shortDate(const QString &rawDate)
{
    return rawDate.size() >= 10 ? rawDate.left(10) : rawDate;
}

QString trimTrailingZeros(QString number)
{
    if (!number.contains('.'))
        return number;

    while (number.endsWith('0'))
        number.chop(1);
    if (number.endsWith('.'))
        number.chop(1);
    return number;
}

QString jsonValueToString(const QJsonValue &value)
{
    if (value.isUndefined() || value.isNull())
        return QString();
    if (value.isString())
        return value.toString();
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isDouble())
    {
        const double number = value.toDouble();
        const qint64 rounded = static_cast<qint64>(number);
        if (qAbs(number - static_cast<double>(rounded)) < 0.000001)
            return QString::number(rounded);
        return trimTrailingZeros(QString::number(number, 'f', 6));
    }

    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));

    return QString();
}

QString firstValueByKeys(const QJsonObject &obj, const QStringList &keys)
{
    for (const QString &key : keys)
    {
        const QString value = jsonValueToString(obj.value(key)).trimmed();
        if (!value.isEmpty())
            return value;
    }

    return QString();
}

QStringList valuesByKeys(const QJsonObject &obj, const QStringList &keys)
{
    QStringList values;
    for (const QString &key : keys)
    {
        const QString value = jsonValueToString(obj.value(key)).trimmed();
        if (!value.isEmpty() && !values.contains(value))
            values.append(value);
    }
    return values;
}

QString yearFromDateText(const QString &dateText)
{
    if (dateText.size() < 4)
        return QString();

    bool ok = false;
    const int year = dateText.left(4).toInt(&ok);
    if (ok && year > 0)
        return QString::number(year);

    return QString();
}

QString yearFromObject(const QJsonObject &obj)
{
    const QStringList yearKeys = {
        "AccountYear",
        "IntYear",
        "Year",
        "FiscalYear",
        "AcctYear"
    };

    for (const QString &key : yearKeys)
    {
        const QJsonValue value = obj.value(key);
        if (value.isDouble())
        {
            const int year = value.toInt();
            if (year > 0)
                return QString::number(year);
        }

        const QString text = jsonValueToString(value).trimmed();
        if (!text.isEmpty() && text != "0")
        {
            const QString year = yearFromDateText(text);
            if (!year.isEmpty())
                return year;
        }
    }

    const QStringList dateKeys = {
        "MakeDate",
        "Date",
        "VchDate",
        "StartDate",
        "EndDate",
        "CreateDate",
        "BizDate"
    };

    for (const QString &key : dateKeys)
    {
        const QString year = yearFromDateText(jsonValueToString(obj.value(key)).trimmed());
        if (!year.isEmpty())
            return year;
    }

    return QString();
}

QString periodFromObject(const QJsonObject &obj)
{
    const QString period = firstValueByKeys(obj, QStringList() << "IntPeriod" << "Period" << "AcctPeriod");
    if (!period.isEmpty() && period != "0")
        return period;

    const QString dateText = firstValueByKeys(obj, QStringList() << "MakeDate" << "Date" << "VchDate" << "BizDate");
    if (dateText.size() >= 7)
        return dateText.mid(5, 2);

    return QString();
}

QString compactRawJson(const QJsonObject &obj)
{
    QString raw = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    if (raw.size() > 1200)
        raw = raw.left(1200) + QStringLiteral("...");
    return raw;
}

QString fullRawJson(const QJsonObject &obj)
{
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

bool containsText(const QString &source, const QString &needle)
{
    return source.contains(needle, Qt::CaseInsensitive);
}

bool listContainsText(const QStringList &values, const QString &needle)
{
    for (const QString &value : values)
    {
        if (containsText(value, needle))
            return true;
    }
    return false;
}

QString joinUnique(const QStringList &values)
{
    QStringList unique;
    for (const QString &value : values)
    {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty() && !unique.contains(trimmed))
            unique.append(trimmed);
    }
    return unique.join(",");
}

void appendUnique(QStringList &values, const QString &value)
{
    const QString trimmed = value.trimmed();
    if (!trimmed.isEmpty() && !values.contains(trimmed))
        values.append(trimmed);
}

QString projectCodeString(const QJsonObject &obj)
{
    QStringList codes;
    for (int i = 1; i <= 6; ++i)
    {
        const QString value = jsonValueToString(obj.value(QString("ProjCode%1").arg(i))).trimmed();
        if (!value.isEmpty() && !codes.contains(value))
            codes.append(value);
    }
    return codes.join(",");
}

bool sourceObjectContains(const QJsonObject &obj, const QString &text)
{
    if (text.isEmpty())
        return true;

    return containsText(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)), text);
}

bool ensureDirectoryExists(const QString &dirPath, QString *errorMessage)
{
    QDir dir(dirPath);
    if (dir.exists())
        return true;

    if (dir.mkpath("."))
        return true;

    if (errorMessage)
        *errorMessage = QStringLiteral("创建目录失败：") + QDir::toNativeSeparators(dirPath);
    return false;
}

bool parseJsonQueryFileInfo(const QString &filePath, JsonQueryFileInfo &info, QString *errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("打开失败");
        return false;
    }

    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("JSON 解析失败");
        return false;
    }

    const QJsonObject root = doc.object();
    const QJsonObject accountSet = root.value("AccountSet").toObject();
    const QString asName = accountSet.value("AsName").toString(QFileInfo(filePath).completeBaseName());

    int arrayCount = 0;
    int recordCount = 0;
    for (auto it = root.constBegin(); it != root.constEnd(); ++it)
    {
        if (!it.value().isArray())
            continue;

        const int count = it.value().toArray().size();
        if (count > 0)
            ++arrayCount;
        recordCount += count;
    }

    info.asName = asName;
    info.startDate = shortDate(accountSet.value("StartDate").toString());
    info.endPeriod = jsonValueToString(root.value("EndPeriod"));
    info.jsonPath = filePath;
    info.status = QStringLiteral("可查询");
    info.arrayCount = arrayCount;
    info.recordCount = recordCount;
    info.valid = true;
    return true;
}

JsonQueryScanResult scanJsonQueryFiles(const QString &rootPath)
{
    JsonQueryScanResult result;
    QString errorMessage;
    if (!ensureDirectoryExists(rootPath, &errorMessage))
    {
        result.errorMessage = errorMessage;
        return result;
    }

    QDirIterator it(rootPath, QStringList() << "*.json", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString filePath = it.next();

        JsonQueryFileInfo info;
        QString parseError;
        if (!parseJsonQueryFileInfo(filePath, info, &parseError))
        {
            info.asName = QFileInfo(filePath).completeBaseName();
            info.startDate.clear();
            info.endPeriod.clear();
            info.jsonPath = filePath;
            info.status = parseError;
            info.arrayCount = 0;
            info.recordCount = 0;
            info.valid = false;
        }
        result.files.append(info);
    }

    result.ok = true;
    return result;
}

bool extractZipToJsonQueryDir(const QString &zipPath, const QString &rootPath, QString *errorMessage)
{
    if (!ensureDirectoryExists(rootPath, errorMessage))
        return false;

    QFileInfo zipInfo(zipPath);
    if (!zipInfo.exists() || !zipInfo.isFile())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("压缩包不存在");
        return false;
    }

    const QString outDir = QDir::cleanPath(rootPath + "/" + zipInfo.completeBaseName());
    QDir targetDir(outDir);
    if (targetDir.exists())
    {
        const QString rootCanonical = QDir::fromNativeSeparators(QDir(rootPath).canonicalPath());
        const QString targetCanonical = QDir::fromNativeSeparators(targetDir.canonicalPath());
        if (rootCanonical.isEmpty() || targetCanonical.isEmpty()
            || !targetCanonical.startsWith(rootCanonical + "/", Qt::CaseInsensitive))
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("目标目录校验失败");
            return false;
        }
        targetDir.removeRecursively();
    }

    if (!QDir().mkpath(outDir))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("创建解压目录失败");
        return false;
    }

    const QString script =
        "$ErrorActionPreference='Stop';"
        "Add-Type -AssemblyName System.IO.Compression.FileSystem;"
        "$zip=$env:LEMON_JSON_ZIP_PATH;$dest=$env:LEMON_JSON_ZIP_DEST;"
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

    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LEMON_JSON_ZIP_PATH", zipInfo.absoluteFilePath());
    env.insert("LEMON_JSON_ZIP_DEST", outDir);
    process.setProcessEnvironment(env);
    const QStringList args = QStringList()
        << "-NoProfile"
        << "-ExecutionPolicy"
        << "Bypass"
        << "-Command"
        << script;
    AppLogger::operation(QStringLiteral("JSON 查询解压 ZIP zip=%1 dest=%2")
        .arg(QDir::toNativeSeparators(zipInfo.absoluteFilePath()), QDir::toNativeSeparators(outDir)));
    AppLogger::command("powershell.exe", args);
    process.start("powershell.exe", args);

    if (!process.waitForFinished(60000))
    {
        process.kill();
        process.waitForFinished(3000);
        if (errorMessage)
            *errorMessage = QStringLiteral("解压超时");
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        const QString stdErr = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (errorMessage)
        {
            if (stdErr.contains("No JSON files found in ZIP", Qt::CaseInsensitive))
                *errorMessage = QStringLiteral("ZIP 中未找到 JSON 文件");
            else
                *errorMessage = stdErr.isEmpty() ? QStringLiteral("解压失败") : stdErr;
        }
        return false;
    }

    return true;
}

bool copyJsonToJsonQueryDir(const QString &jsonPath, const QString &rootPath, QString *errorMessage)
{
    if (!ensureDirectoryExists(rootPath, errorMessage))
        return false;

    QFileInfo jsonInfo(jsonPath);
    QString targetPath = QDir::cleanPath(rootPath + "/" + jsonInfo.fileName());
    int suffix = 1;
    while (QFile::exists(targetPath))
    {
        targetPath = QDir::cleanPath(rootPath + "/"
            + jsonInfo.completeBaseName()
            + QStringLiteral("_%1.").arg(suffix++)
            + jsonInfo.suffix());
    }

    if (!QFile::copy(jsonInfo.absoluteFilePath(), targetPath))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("复制 JSON 文件失败");
        return false;
    }

    return true;
}

bool importJsonQueryFile(const QString &filePath, const QString &rootPath, QString *errorMessage)
{
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("文件不存在");
        return false;
    }

    if (fileInfo.suffix().compare("zip", Qt::CaseInsensitive) == 0)
        return extractZipToJsonQueryDir(filePath, rootPath, errorMessage);

    if (fileInfo.suffix().compare("json", Qt::CaseInsensitive) == 0)
        return copyJsonToJsonQueryDir(filePath, rootPath, errorMessage);

    if (errorMessage)
        *errorMessage = QStringLiteral("仅支持 ZIP 或 JSON 文件");
    return false;
}

QString displayNameForJsonSource(const QString &source)
{
    if (source == "GLAccountList")
        return QStringLiteral("科目");
    if (source == "GLBalanceList")
        return QStringLiteral("余额");
    if (source == "GLVoucherList")
        return QStringLiteral("凭证");
    if (source == "GLProjectList")
        return QStringLiteral("辅助核算");
    if (source == "GLProjectTypeList")
        return QStringLiteral("辅助核算类别");
    if (source == "FADepreciationList")
        return QStringLiteral("折旧");
    if (source == "GLFixedAssetList")
        return QStringLiteral("固定资产");
    if (source == "GLInvoiceList")
        return QStringLiteral("发票");
    if (source == "GLJournalList")
        return QStringLiteral("日记账");
    if (source == "GLPeriodList")
        return QStringLiteral("期间");
    return source;
}

JsonQueryIndexData buildJsonQueryIndexes(const QJsonDocument &doc)
{
    JsonQueryIndexData indexes;
    if (!doc.isObject())
        return indexes;

    const QJsonObject root = doc.object();
    const QJsonArray projectTypes = root.value("GLProjectTypeList").toArray();
    for (const QJsonValue &value : projectTypes)
    {
        const QJsonObject obj = value.toObject();
        const QString code = obj.value("ProjTypeCode").toString();
        const QString name = obj.value("ProjTypeName").toString();
        if (!code.isEmpty())
            indexes.projectTypeNames.insert(code, name);
    }

    const QJsonArray accounts = root.value("GLAccountList").toArray();
    for (const QJsonValue &value : accounts)
    {
        const QJsonObject obj = value.toObject();
        const QString acctCode = obj.value("AcctCode").toString();
        if (acctCode.isEmpty())
            continue;

        const QString year = yearFromObject(obj);
        const QString acctName = firstValueByKeys(obj, QStringList() << "AcctName" << "AcctFullName");
        if (!acctName.isEmpty() && !indexes.acctNameByCode.contains(acctCode))
            indexes.acctNameByCode.insert(acctCode, acctName);

        QStringList projTypes;
        for (int i = 1; i <= 6; ++i)
            projTypes.append(obj.value(QString("AcctProjTypeCode%1").arg(i)).toString());

        if (!indexes.acctProjTypesByCode.contains(acctCode))
            indexes.acctProjTypesByCode.insert(acctCode, projTypes);

        if (!year.isEmpty())
        {
            const QString key = year + "\n" + acctCode;
            if (!acctName.isEmpty())
                indexes.acctNameByYearAndCode.insert(key, acctName);
            indexes.acctProjTypesByYearAndCode.insert(key, projTypes);
        }
    }

    return indexes;
}

QString accountNameForIndexes(const JsonQueryIndexData &indexes, const QString &year, const QString &acctCode)
{
    if (acctCode.isEmpty())
        return QString();

    if (!year.isEmpty())
    {
        const QString key = year + "\n" + acctCode;
        if (indexes.acctNameByYearAndCode.contains(key))
            return indexes.acctNameByYearAndCode.value(key);
    }

    return indexes.acctNameByCode.value(acctCode);
}

QStringList projectCodesByTypeNameForIndexes(const JsonQueryIndexData &indexes, const QJsonObject &obj, const QString &year, const QString &acctCode, const QString &typeNamePart)
{
    QStringList result;
    if (acctCode.isEmpty())
        return result;

    QStringList projTypes;
    if (!year.isEmpty())
        projTypes = indexes.acctProjTypesByYearAndCode.value(year + "\n" + acctCode);
    if (projTypes.isEmpty())
        projTypes = indexes.acctProjTypesByCode.value(acctCode);

    for (int i = 1; i <= 6 && i <= projTypes.size(); ++i)
    {
        const QString typeCode = projTypes[i - 1];
        const QString typeName = indexes.projectTypeNames.value(typeCode);
        const bool isCustomer = typeNamePart == QStringLiteral("客户")
            && (typeName.contains(QStringLiteral("客户")) || typeCode == "003");
        const bool isSupplier = typeNamePart == QStringLiteral("供应商")
            && (typeName.contains(QStringLiteral("供应商")) || typeCode == "004");

        if (!isCustomer && !isSupplier)
            continue;

        const QString projectCode = jsonValueToString(obj.value(QString("ProjCode%1").arg(i))).trimmed();
        if (!projectCode.isEmpty() && !result.contains(projectCode))
            result.append(projectCode);
    }

    return result;
}

JsonQueryRecord makeJsonQueryRecord(const QString &source, const QJsonObject &obj, const JsonQueryIndexData &indexes)
{
    JsonQueryRecord record;
    record.sourceKey = source;
    record.source = displayNameForJsonSource(source);
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        record.fields.insert(it.key(), jsonValueToString(it.value()));

    record.year = yearFromObject(obj);
    record.period = periodFromObject(obj);
    record.acctCode = firstValueByKeys(obj, QStringList()
        << "AcctCode" << "AccountCode" << "SubjectCode" << "Code");
    record.acctName = firstValueByKeys(obj, QStringList()
        << "AcctName" << "AcctFullName" << "SubjectName" << "ProjName" << "ProjectName" << "Name" << "ProjTypeName");

    if (record.acctName.isEmpty() && !record.acctCode.isEmpty())
        record.acctName = accountNameForIndexes(indexes, record.year, record.acctCode);

    QStringList customerCodes = valuesByKeys(obj, QStringList()
        << "CustomerCode" << "CustCode" << "CustNo" << "CustNO" << "ClientCode" << "ClientNo");
    customerCodes.append(projectCodesByTypeNameForIndexes(indexes, obj, record.year, record.acctCode, QStringLiteral("客户")));
    record.customerCode = joinUnique(customerCodes);

    QStringList supplierCodes = valuesByKeys(obj, QStringList()
        << "SupplierCode" << "VendorCode" << "SupplyCode" << "ProviderCode");
    supplierCodes.append(projectCodesByTypeNameForIndexes(indexes, obj, record.year, record.acctCode, QStringLiteral("供应商")));
    record.supplierCode = joinUnique(supplierCodes);

    const QString projectTypeCode = obj.value("ProjTypeCode").toString();
    const QString projectTypeName = indexes.projectTypeNames.value(projectTypeCode);
    const QString projectCode = firstValueByKeys(obj, QStringList() << "ProjCode" << "ProjectCode" << "Code");
    if (record.customerCode.isEmpty() && !projectCode.isEmpty()
        && (projectTypeName.contains(QStringLiteral("客户")) || projectTypeCode == "003"))
    {
        record.customerCode = projectCode;
    }
    if (record.supplierCode.isEmpty() && !projectCode.isEmpty()
        && (projectTypeName.contains(QStringLiteral("供应商")) || projectTypeCode == "004"))
    {
        record.supplierCode = projectCode;
    }

    record.summary = firstValueByKeys(obj, QStringList()
        << "Summary" << "摘要" << "AcctName" << "AcctFullName" << "ProjName" << "ProjectName" << "Name" << "ProjTypeName");
    record.debit = firstValueByKeys(obj, QStringList()
        << "DebitBal" << "Debit" << "DebitAmount" << "FX_LJ_JF" << "JF");
    record.credit = firstValueByKeys(obj, QStringList()
        << "CreditBal" << "Credit" << "CreditAmount" << "FX_LJ_DF" << "DF");
    record.amount = firstValueByKeys(obj, QStringList()
        << "FX_Amount_s" << "Amount" << "Amt" << "BegBal" << "FX_BAmount_s");
    record.rawJson = fullRawJson(obj);

    if (!record.acctName.isEmpty() && !record.fields.contains("AcctName"))
        record.fields.insert("AcctName", record.acctName);
    if (!record.customerCode.isEmpty() && !record.fields.contains("CustomerCode"))
        record.fields.insert("CustomerCode", record.customerCode);
    if (!record.supplierCode.isEmpty() && !record.fields.contains("SupplierCode"))
        record.fields.insert("SupplierCode", record.supplierCode);

    return record;
}

bool jsonQueryRecordMatches(const JsonQueryRecord &record, const QJsonObject &obj, const JsonQueryFilter &filter)
{
    if (!filter.year.isEmpty() && !containsText(record.year, filter.year) && !sourceObjectContains(obj, filter.year))
        return false;

    if (!filter.acctCode.isEmpty()
        && !containsText(record.acctCode, filter.acctCode)
        && !containsText(record.acctName, filter.acctCode))
    {
        return false;
    }

    if (!filter.customerCode.isEmpty()
        && !containsText(record.customerCode, filter.customerCode)
        && !containsText(projectCodeString(obj), filter.customerCode))
    {
        return false;
    }

    if (!filter.supplierCode.isEmpty()
        && !containsText(record.supplierCode, filter.supplierCode)
        && !containsText(projectCodeString(obj), filter.supplierCode))
    {
        return false;
    }

    if (!filter.keyword.isEmpty())
    {
        const QString combined = record.source + "\n"
            + record.year + "\n"
            + record.period + "\n"
            + record.acctCode + "\n"
            + record.acctName + "\n"
            + record.customerCode + "\n"
            + record.supplierCode + "\n"
            + record.summary + "\n"
            + record.rawJson;
        if (!containsText(combined, filter.keyword))
            return false;
    }

    return true;
}

JsonQueryResult queryJsonRecords(const QJsonDocument &doc, const JsonQueryIndexData &indexes, const JsonQueryFilter &filter)
{
    JsonQueryResult result;
    if (!doc.isObject())
        return result;

    result.columns.append(QStringLiteral("来源"));
    const QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it)
    {
        if (!it.value().isArray())
            continue;

        const QString source = it.key();
        if (!filter.source.isEmpty() && source != filter.source)
            continue;

        const QJsonArray array = it.value().toArray();
        for (const QJsonValue &value : array)
        {
            if (!value.isObject())
                continue;

            const QJsonObject obj = value.toObject();
            const JsonQueryRecord record = makeJsonQueryRecord(source, obj, indexes);
            if (!jsonQueryRecordMatches(record, obj, filter))
                continue;

            ++result.total;
            result.records.append(record);

            for (auto fit = record.fields.constBegin(); fit != record.fields.constEnd(); ++fit)
                appendUnique(result.columns, fit.key());
        }
    }

    appendUnique(result.columns, QStringLiteral("原始 JSON"));
    return result;
}

JsonQueryLoadResult loadJsonQueryFile(const QString &filePath)
{
    JsonQueryLoadResult result;
    result.jsonPath = filePath;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        result.errorMessage = QStringLiteral("打开 JSON 失败");
        return result;
    }

    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject())
    {
        result.errorMessage = QStringLiteral("JSON 解析失败：") + jsonError.errorString();
        return result;
    }

    result.doc = doc;
    result.indexes = buildJsonQueryIndexes(doc);
    result.ok = true;
    return result;
}
}

JsonQueryWidget::JsonQueryWidget(GlobalData *globalData, QWidget *parent)
    : QWidget(parent),
      m_pGlobalData(globalData),
      m_pDirLabel(nullptr),
      m_pDropLabel(nullptr),
      m_pFileTable(nullptr),
      m_pDatasetCombo(nullptr),
      m_pYearEdit(nullptr),
      m_pAcctCodeEdit(nullptr),
      m_pCustomerCodeEdit(nullptr),
      m_pSupplierCodeEdit(nullptr),
      m_pKeywordEdit(nullptr),
      m_pResultLabel(nullptr),
      m_pResultTable(nullptr),
      m_pPageSizeCombo(nullptr),
      m_pPageLabel(nullptr),
      m_pBtnFirstPage(nullptr),
      m_pBtnPrevPage(nullptr),
      m_pBtnNextPage(nullptr),
      m_pBtnLastPage(nullptr),
      m_pBtnBrowse(nullptr),
      m_pBtnRefresh(nullptr),
      m_pBtnLoadSelected(nullptr),
      m_pBtnSearch(nullptr),
      m_pBtnClear(nullptr),
      m_pBtnBack(nullptr),
      m_currentPage(0),
      m_pageSize(100),
      m_rawJsonColumn(-1),
      m_blockFileSelection(false),
      m_scanRequestId(0),
      m_importRequestId(0),
      m_loadRequestId(0),
      m_queryRequestId(0)
{
    setAcceptDrops(true);
    setupUi();
}

void JsonQueryWidget::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(10);

    QHBoxLayout *titleLayout = new QHBoxLayout();
    QLabel *titleLabel = new QLabel(QStringLiteral("解析 JSON 查询数据"), this);
    titleLabel->setObjectName("labelPageTitle");
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();

    m_pBtnBrowse = new QPushButton(QStringLiteral("选择文件"), this);
    m_pBtnBrowse->setObjectName("btnBrowseJsonPackage");
    m_pBtnBrowse->setProperty("flatGreen", true);
    m_pBtnBrowse->setMaximumWidth(100);
    titleLayout->addWidget(m_pBtnBrowse);

    m_pBtnRefresh = new QPushButton(QStringLiteral("刷新目录"), this);
    m_pBtnRefresh->setObjectName("btnRefreshJsonList");
    m_pBtnRefresh->setMaximumWidth(100);
    m_pBtnRefresh->setMinimumHeight(30);
    titleLayout->addWidget(m_pBtnRefresh);
    layout->addLayout(titleLayout);

    m_pDirLabel = new QLabel(this);
    m_pDirLabel->setWordWrap(true);
    layout->addWidget(m_pDirLabel);

    m_pDropLabel = new QLabel(QStringLiteral("将 ZIP 压缩包或 JSON 文件拖入此处，系统会自动解压并加载 JSON"), this);
    m_pDropLabel->setObjectName("labelRepairDropArea");
    m_pDropLabel->setAlignment(Qt::AlignCenter);
    m_pDropLabel->setMinimumHeight(66);
    m_pDropLabel->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(m_pDropLabel);

    m_pFileTable = new QTableWidget(this);
    m_pFileTable->setObjectName("jsonFileTable");
    m_pFileTable->setColumnCount(6);
    m_pFileTable->setHorizontalHeaderLabels({
        QStringLiteral("账套名称"),
        QStringLiteral("启用日期"),
        QStringLiteral("结束期间"),
        QStringLiteral("数组/记录"),
        QStringLiteral("文件"),
        QStringLiteral("状态")
    });
    m_pFileTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_pFileTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_pFileTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_pFileTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_pFileTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_pFileTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    m_pFileTable->setColumnWidth(1, 100);
    m_pFileTable->setColumnWidth(2, 90);
    m_pFileTable->setColumnWidth(3, 90);
    m_pFileTable->setColumnWidth(5, 95);
    m_pFileTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pFileTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pFileTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pFileTable->verticalHeader()->setVisible(false);
    m_pFileTable->verticalHeader()->setDefaultSectionSize(34);
    m_pFileTable->horizontalHeader()->setHighlightSections(false);
    m_pFileTable->setAlternatingRowColors(true);
    m_pFileTable->setShowGrid(false);
    m_pFileTable->setMaximumHeight(130);
    layout->addWidget(m_pFileTable);

    QHBoxLayout *loadLayout = new QHBoxLayout();
    m_pBtnLoadSelected = new QPushButton(QStringLiteral("加载选中 JSON"), this);
    m_pBtnLoadSelected->setObjectName("btnLoadSelectedJson");
    m_pBtnLoadSelected->setMaximumWidth(140);
    m_pBtnLoadSelected->setMinimumHeight(32);
    loadLayout->addWidget(m_pBtnLoadSelected);
    loadLayout->addStretch();
    layout->addLayout(loadLayout);

    QHBoxLayout *filterLayout = new QHBoxLayout();
    filterLayout->setSpacing(8);

    m_pDatasetCombo = new QComboBox(this);
    m_pDatasetCombo->setObjectName("comboJsonDataset");
    m_pDatasetCombo->setMinimumHeight(32);
    m_pDatasetCombo->setMinimumWidth(150);
    m_pDatasetCombo->addItem(QStringLiteral("全部数据"), QString());
    filterLayout->addWidget(m_pDatasetCombo);

    m_pYearEdit = new QLineEdit(this);
    m_pYearEdit->setPlaceholderText(QStringLiteral("年份"));
    m_pYearEdit->setMaximumWidth(82);
    filterLayout->addWidget(m_pYearEdit);

    m_pAcctCodeEdit = new QLineEdit(this);
    m_pAcctCodeEdit->setPlaceholderText(QStringLiteral("科目编码"));
    m_pAcctCodeEdit->setMaximumWidth(120);
    filterLayout->addWidget(m_pAcctCodeEdit);

    m_pCustomerCodeEdit = new QLineEdit(this);
    m_pCustomerCodeEdit->setPlaceholderText(QStringLiteral("客户编码"));
    m_pCustomerCodeEdit->setMaximumWidth(120);
    filterLayout->addWidget(m_pCustomerCodeEdit);

    m_pSupplierCodeEdit = new QLineEdit(this);
    m_pSupplierCodeEdit->setPlaceholderText(QStringLiteral("供应商编码"));
    m_pSupplierCodeEdit->setMaximumWidth(120);
    filterLayout->addWidget(m_pSupplierCodeEdit);
    layout->addLayout(filterLayout);

    QHBoxLayout *keywordLayout = new QHBoxLayout();
    keywordLayout->setSpacing(8);

    m_pKeywordEdit = new QLineEdit(this);
    m_pKeywordEdit->setPlaceholderText(QStringLiteral("摘要/名称/任意字段"));
    keywordLayout->addWidget(m_pKeywordEdit, 1);

    m_pBtnSearch = new QPushButton(QStringLiteral("查询"), this);
    m_pBtnSearch->setObjectName("btnSearchJsonData");
    m_pBtnSearch->setMaximumWidth(80);
    keywordLayout->addWidget(m_pBtnSearch);

    m_pBtnClear = new QPushButton(QStringLiteral("清空"), this);
    m_pBtnClear->setObjectName("btnClearJsonFilters");
    m_pBtnClear->setProperty("grayButton", true);
    m_pBtnClear->setMaximumWidth(80);
    keywordLayout->addWidget(m_pBtnClear);
    layout->addLayout(keywordLayout);

    m_pResultLabel = new QLabel(QStringLiteral("请先导入或选择 JSON"), this);
    m_pResultLabel->setWordWrap(true);
    layout->addWidget(m_pResultLabel);

    QHBoxLayout *pageLayout = new QHBoxLayout();
    pageLayout->setSpacing(8);
    pageLayout->addWidget(new QLabel(QStringLiteral("每页"), this));

    m_pPageSizeCombo = new QComboBox(this);
    m_pPageSizeCombo->setObjectName("comboJsonPageSize");
    m_pPageSizeCombo->addItem(QStringLiteral("50"), 50);
    m_pPageSizeCombo->addItem(QStringLiteral("100"), 100);
    m_pPageSizeCombo->addItem(QStringLiteral("200"), 200);
    m_pPageSizeCombo->addItem(QStringLiteral("500"), 500);
    m_pPageSizeCombo->setCurrentIndex(1);
    m_pPageSizeCombo->setMaximumWidth(80);
    pageLayout->addWidget(m_pPageSizeCombo);

    m_pBtnFirstPage = new QPushButton(QStringLiteral("首页"), this);
    m_pBtnPrevPage = new QPushButton(QStringLiteral("上一页"), this);
    m_pBtnNextPage = new QPushButton(QStringLiteral("下一页"), this);
    m_pBtnLastPage = new QPushButton(QStringLiteral("末页"), this);
    m_pPageLabel = new QLabel(QStringLiteral("第 0 / 0 页"), this);
    m_pPageLabel->setMinimumWidth(100);
    m_pPageLabel->setAlignment(Qt::AlignCenter);

    m_pBtnFirstPage->setProperty("grayButton", true);
    m_pBtnPrevPage->setProperty("grayButton", true);
    m_pBtnNextPage->setProperty("grayButton", true);
    m_pBtnLastPage->setProperty("grayButton", true);
    m_pBtnFirstPage->setMaximumWidth(70);
    m_pBtnPrevPage->setMaximumWidth(80);
    m_pBtnNextPage->setMaximumWidth(80);
    m_pBtnLastPage->setMaximumWidth(70);

    pageLayout->addWidget(m_pBtnFirstPage);
    pageLayout->addWidget(m_pBtnPrevPage);
    pageLayout->addWidget(m_pPageLabel);
    pageLayout->addWidget(m_pBtnNextPage);
    pageLayout->addWidget(m_pBtnLastPage);
    pageLayout->addStretch();
    layout->addLayout(pageLayout);

    m_pResultTable = new QTableWidget(this);
    m_pResultTable->setObjectName("jsonQueryResultTable");
    m_pResultTable->setColumnCount(0);
    m_pResultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pResultTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_pResultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pResultTable->verticalHeader()->setVisible(false);
    m_pResultTable->verticalHeader()->setDefaultSectionSize(34);
    m_pResultTable->horizontalHeader()->setHighlightSections(false);
    m_pResultTable->setAlternatingRowColors(true);
    m_pResultTable->setShowGrid(false);
    m_pResultTable->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_pResultTable, 1);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    m_pBtnBack = new QPushButton(QStringLiteral("返回"), this);
    m_pBtnBack->setObjectName("btnBackToMain");
    m_pBtnBack->setProperty("grayButton", true);
    m_pBtnBack->setMaximumWidth(100);
    m_pBtnBack->setMinimumHeight(36);
    btnLayout->addWidget(m_pBtnBack);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    connect(m_pBtnBrowse, &QPushButton::clicked, this, &JsonQueryWidget::onBrowseClicked);
    connect(m_pBtnRefresh, &QPushButton::clicked, this, &JsonQueryWidget::onRefreshClicked);
    connect(m_pBtnLoadSelected, &QPushButton::clicked, this, &JsonQueryWidget::onLoadSelectedClicked);
    connect(m_pBtnSearch, &QPushButton::clicked, this, &JsonQueryWidget::onSearchClicked);
    connect(m_pBtnClear, &QPushButton::clicked, this, &JsonQueryWidget::onClearClicked);
    connect(m_pBtnBack, &QPushButton::clicked, this, &JsonQueryWidget::onBackClicked);
    connect(m_pBtnFirstPage, &QPushButton::clicked, this, &JsonQueryWidget::onFirstPageClicked);
    connect(m_pBtnPrevPage, &QPushButton::clicked, this, &JsonQueryWidget::onPrevPageClicked);
    connect(m_pBtnNextPage, &QPushButton::clicked, this, &JsonQueryWidget::onNextPageClicked);
    connect(m_pBtnLastPage, &QPushButton::clicked, this, &JsonQueryWidget::onLastPageClicked);
    connect(m_pPageSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &JsonQueryWidget::onPageSizeChanged);
    connect(m_pDatasetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_currentDoc.isObject())
            clearResults(QStringLiteral("数据类型已切换，请点击“查询”重新查询。"));
    });
    connect(m_pFileTable, &QTableWidget::currentCellChanged, this, &JsonQueryWidget::onFileCurrentCellChanged);
    connect(m_pResultTable, &QTableWidget::customContextMenuRequested,
            this, &JsonQueryWidget::onResultTableContextMenuRequested);
    connect(m_pFileTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        onLoadSelectedClicked();
    });

    QShortcut *copyShortcut = new QShortcut(QKeySequence::Copy, m_pResultTable);
    connect(copyShortcut, &QShortcut::activated, this, &JsonQueryWidget::onCopyResultSelection);

    updatePageControls();
}

void JsonQueryWidget::loadFileList()
{
    AppLogger::operation(QStringLiteral("JSON 查询加载文件列表"));
    startScanFiles(true);
}

void JsonQueryWidget::startScanFiles(bool autoLoadFirst)
{
    const int requestId = ++m_scanRequestId;
    const QString rootPath = queryDir();
    AppLogger::operation(QStringLiteral("JSON 查询后台扫描开始 requestId=%1 root=%2 autoLoadFirst=%3")
        .arg(requestId)
        .arg(QDir::toNativeSeparators(rootPath))
        .arg(autoLoadFirst));
    setFileOperationRunning(true, QStringLiteral("正在后台扫描 JSON 文件..."));

    QSharedPointer<JsonQueryScanResult> result(new JsonQueryScanResult);
    QThread *thread = QThread::create([result, rootPath]() {
        *result = scanJsonQueryFiles(rootPath);
    });

    connect(thread, &QThread::finished, this, [this, result, requestId, autoLoadFirst]() {
        if (requestId != m_scanRequestId)
            return;

        setFileOperationRunning(false);
        AppLogger::operation(QStringLiteral("JSON 查询后台扫描结束 requestId=%1 ok=%2 files=%3")
            .arg(requestId)
            .arg(result->ok)
            .arg(result->files.size()));
        if (!result->ok)
        {
            m_pResultLabel->setText(result->errorMessage);
            return;
        }

        m_fileList = result->files;
        fillFileTable();

        int rowToSelect = -1;
        for (int i = 0; i < m_fileList.size(); ++i)
        {
            if (!m_fileList[i].valid)
                continue;

            if (m_fileList[i].jsonPath == m_currentJsonPath)
            {
                rowToSelect = i;
                break;
            }
            if (rowToSelect < 0)
                rowToSelect = i;
        }

        m_blockFileSelection = true;
        if (rowToSelect >= 0)
            m_pFileTable->setCurrentCell(rowToSelect, 0);
        else
            m_pFileTable->clearSelection();
        m_blockFileSelection = false;

        if (autoLoadFirst && rowToSelect >= 0)
        {
            startLoadFile(m_fileList[rowToSelect].jsonPath);
        }
        else if (rowToSelect < 0)
        {
            ++m_loadRequestId;
            ++m_queryRequestId;
            m_currentDoc = QJsonDocument();
            m_currentJsonPath.clear();
            m_acctNameByCode.clear();
            m_acctNameByYearAndCode.clear();
            m_acctProjTypesByCode.clear();
            m_acctProjTypesByYearAndCode.clear();
            m_projectTypeNames.clear();
            populateDatasetCombo();
            clearResults(QStringLiteral("请先导入或选择 JSON"));
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void JsonQueryWidget::startImportFiles(const QStringList &filePaths)
{
    if (filePaths.isEmpty())
        return;

    const int requestId = ++m_importRequestId;
    const QString rootPath = queryDir();
    AppLogger::operation(QStringLiteral("JSON 查询后台导入开始 requestId=%1 files=%2")
        .arg(requestId)
        .arg(filePaths.join("|")));
    setFileOperationRunning(true, QStringLiteral("正在后台导入文件..."));

    QSharedPointer<JsonQueryImportResult> result(new JsonQueryImportResult);
    QThread *thread = QThread::create([result, filePaths, rootPath]() {
        for (const QString &filePath : filePaths)
        {
            QString errorMessage;
            if (importJsonQueryFile(filePath, rootPath, &errorMessage))
                ++result->successCount;
            else
                result->errors.append(QFileInfo(filePath).fileName() + ": " + errorMessage);
        }
    });

    connect(thread, &QThread::finished, this, [this, result, requestId]() {
        if (requestId != m_importRequestId)
            return;

        setFileOperationRunning(false);
        AppLogger::operation(QStringLiteral("JSON 查询后台导入结束 requestId=%1 success=%2 errors=%3")
            .arg(requestId)
            .arg(result->successCount)
            .arg(result->errors.size()));
        if (!result->errors.isEmpty())
        {
            QMessageBox::warning(this, QStringLiteral("部分文件导入失败"), result->errors.join("\n"));
        }
        else if (result->successCount > 0)
        {
            QMessageBox::information(this, QStringLiteral("已导入"),
                QStringLiteral("已导入 %1 个文件并重新扫描。").arg(result->successCount));
        }

        startScanFiles(true);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void JsonQueryWidget::startLoadFile(const QString &filePath)
{
    if (filePath.isEmpty())
        return;

    const int requestId = ++m_loadRequestId;
    ++m_queryRequestId;
    AppLogger::operation(QStringLiteral("JSON 查询后台加载开始 requestId=%1 file=%2")
        .arg(requestId)
        .arg(QDir::toNativeSeparators(filePath)));
    setQueryOperationRunning(true);
    setFileOperationRunning(true, QStringLiteral("正在后台加载 JSON 并建立索引..."));
    clearResults(QStringLiteral("正在后台加载 JSON 并建立索引..."));

    QSharedPointer<JsonQueryLoadResult> result(new JsonQueryLoadResult);
    QThread *thread = QThread::create([result, filePath]() {
        *result = loadJsonQueryFile(filePath);
    });

    connect(thread, &QThread::finished, this, [this, result, requestId]() {
        if (requestId != m_loadRequestId)
            return;

        setFileOperationRunning(false);
        if (!result->ok)
        {
            AppLogger::error(QStringLiteral("JSON 查询后台加载失败 requestId=%1 error=%2")
                .arg(requestId)
                .arg(result->errorMessage));
            m_currentDoc = QJsonDocument();
            m_currentJsonPath.clear();
            populateDatasetCombo();
            setQueryOperationRunning(false);
            m_pResultLabel->setText(result->errorMessage);
            QMessageBox::warning(this, QStringLiteral("加载 JSON 失败"), result->errorMessage);
            return;
        }

        m_currentDoc = result->doc;
        m_currentJsonPath = result->jsonPath;
        m_acctNameByCode = result->indexes.acctNameByCode;
        m_acctNameByYearAndCode = result->indexes.acctNameByYearAndCode;
        m_acctProjTypesByCode = result->indexes.acctProjTypesByCode;
        m_acctProjTypesByYearAndCode = result->indexes.acctProjTypesByYearAndCode;
        m_projectTypeNames = result->indexes.projectTypeNames;
        populateDatasetCombo();
        clearResults(QStringLiteral("JSON 已加载，请设置查询条件后点击“查询”。"));
        setQueryOperationRunning(false);
        AppLogger::operation(QStringLiteral("JSON 查询后台加载完成 requestId=%1 file=%2")
            .arg(requestId)
            .arg(QDir::toNativeSeparators(result->jsonPath)));
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void JsonQueryWidget::startQuery()
{
    if (!m_currentDoc.isObject())
    {
        m_pResultTable->setRowCount(0);
        m_pResultLabel->setText(QStringLiteral("请先导入或选择 JSON"));
        return;
    }

    JsonQueryIndexData indexes;
    indexes.acctNameByCode = m_acctNameByCode;
    indexes.acctNameByYearAndCode = m_acctNameByYearAndCode;
    indexes.acctProjTypesByCode = m_acctProjTypesByCode;
    indexes.acctProjTypesByYearAndCode = m_acctProjTypesByYearAndCode;
    indexes.projectTypeNames = m_projectTypeNames;

    JsonQueryFilter filter;
    filter.source = m_pDatasetCombo->currentData().toString();
    filter.year = m_pYearEdit->text().trimmed();
    filter.acctCode = m_pAcctCodeEdit->text().trimmed();
    filter.customerCode = m_pCustomerCodeEdit->text().trimmed();
    filter.supplierCode = m_pSupplierCodeEdit->text().trimmed();
    filter.keyword = m_pKeywordEdit->text().trimmed();

    const QJsonDocument doc = m_currentDoc;
    const QString currentJsonPath = m_currentJsonPath;
    const int requestId = ++m_queryRequestId;
    AppLogger::operation(QStringLiteral("JSON 查询后台查询开始 requestId=%1 source=%2 year=%3 acct=%4 customer=%5 supplier=%6 keyword=%7")
        .arg(requestId)
        .arg(filter.source, filter.year, filter.acctCode, filter.customerCode, filter.supplierCode, filter.keyword));
    setQueryOperationRunning(true, QStringLiteral("正在后台查询..."));

    QSharedPointer<JsonQueryResult> result(new JsonQueryResult);
    QThread *thread = QThread::create([result, doc, indexes, filter]() {
        *result = queryJsonRecords(doc, indexes, filter);
    });

    connect(thread, &QThread::finished, this, [this, result, currentJsonPath, requestId]() {
        if (requestId != m_queryRequestId)
            return;

        setQueryOperationRunning(false);
        m_currentResultRecords = result->records;
        m_currentResultColumns = result->columns;
        m_currentPage = 0;
        renderCurrentPage();

        const QString fileName = QFileInfo(currentJsonPath).fileName();
        QString label = QStringLiteral("当前文件：%1，匹配 %2 条").arg(fileName).arg(result->total);
        if (result->total > 0)
            label += QStringLiteral("，当前显示第 %1 页").arg(m_currentPage + 1);
        m_pResultLabel->setText(label);
        AppLogger::operation(QStringLiteral("JSON 查询后台查询完成 requestId=%1 total=%2 columns=%3")
            .arg(requestId)
            .arg(result->total)
            .arg(result->columns.size()));
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void JsonQueryWidget::setFileOperationRunning(bool running, const QString &message)
{
    setProperty("sessionBusysetFileOperationRunning", running);
    bool hasValidFile = false;
    for (const JsonQueryFileInfo &info : m_fileList)
    {
        if (info.valid)
        {
            hasValidFile = true;
            break;
        }
    }

    m_pBtnBrowse->setEnabled(!running);
    m_pBtnRefresh->setEnabled(!running);
    m_pBtnLoadSelected->setEnabled(!running && hasValidFile);
    m_pFileTable->setEnabled(!running);
    setAcceptDrops(!running);

    if (running && !message.isEmpty())
        m_pResultLabel->setText(message);
}

void JsonQueryWidget::setQueryOperationRunning(bool running, const QString &message)
{
    setProperty("sessionBusysetQueryOperationRunning", running);
    m_pBtnSearch->setEnabled(!running && m_currentDoc.isObject());
    m_pBtnClear->setEnabled(!running && m_currentDoc.isObject());
    m_pDatasetCombo->setEnabled(!running && m_currentDoc.isObject());

    if (running && !message.isEmpty())
        m_pResultLabel->setText(message);
}

QString JsonQueryWidget::queryDir() const
{
    return QDir::cleanPath(qApp->applicationDirPath() + "/JsonQueryPackages");
}

void JsonQueryWidget::ensureQueryDir() const
{
    QDir dir(queryDir());
    if (!dir.exists())
        dir.mkpath(".");
}

void JsonQueryWidget::scanJsonFiles()
{
    m_fileList.clear();

    QDirIterator it(queryDir(), QStringList() << "*.json", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString filePath = it.next();

        JsonQueryFileInfo info;
        QString errorMessage;
        if (!parseFileInfo(filePath, info, &errorMessage))
        {
            info.asName = QFileInfo(filePath).completeBaseName();
            info.startDate.clear();
            info.endPeriod.clear();
            info.jsonPath = filePath;
            info.status = errorMessage;
            info.arrayCount = 0;
            info.recordCount = 0;
            info.valid = false;
        }
        m_fileList.append(info);
    }
}

void JsonQueryWidget::fillFileTable()
{
    m_pDirLabel->setText(QStringLiteral("扫描目录：") + QDir::toNativeSeparators(queryDir()));
    m_pFileTable->setRowCount(m_fileList.size());

    QDir rootDir(queryDir());
    int validCount = 0;
    for (int i = 0; i < m_fileList.size(); ++i)
    {
        const JsonQueryFileInfo &info = m_fileList[i];
        if (info.valid)
            ++validCount;

        QTableWidgetItem *nameItem = new QTableWidgetItem(info.asName);
        m_pFileTable->setItem(i, 0, nameItem);
        m_pFileTable->setItem(i, 1, new QTableWidgetItem(info.startDate));
        m_pFileTable->setItem(i, 2, new QTableWidgetItem(info.endPeriod));
        m_pFileTable->setItem(i, 3, new QTableWidgetItem(QStringLiteral("%1/%2").arg(info.arrayCount).arg(info.recordCount)));
        m_pFileTable->setItem(i, 4, new QTableWidgetItem(QDir::toNativeSeparators(rootDir.relativeFilePath(info.jsonPath))));

        QTableWidgetItem *statusItem = new QTableWidgetItem(info.status);
        statusItem->setForeground(info.valid ? QColor(110, 224, 177) : QColor(255, 139, 162));
        m_pFileTable->setItem(i, 5, statusItem);

        for (int col = 0; col < m_pFileTable->columnCount(); ++col)
        {
            QTableWidgetItem *item = m_pFileTable->item(i, col);
            if (item)
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        }
    }

    m_pBtnLoadSelected->setEnabled(validCount > 0);
    m_pBtnSearch->setEnabled(m_currentDoc.isObject());
    m_pBtnClear->setEnabled(m_currentDoc.isObject());
    m_pDatasetCombo->setEnabled(m_currentDoc.isObject());
    m_pDropLabel->setText(QStringLiteral("已扫描 %1 个 JSON，其中 %2 个可查询。可继续拖入 ZIP 压缩包或 JSON 文件。")
        .arg(m_fileList.size())
        .arg(validCount));
}

bool JsonQueryWidget::parseFileInfo(const QString &filePath, JsonQueryFileInfo &info, QString *errorMessage) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("打开失败");
        return false;
    }

    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("JSON 解析失败");
        return false;
    }

    const QJsonObject root = doc.object();
    const QJsonObject accountSet = root.value("AccountSet").toObject();
    const QString asName = accountSet.value("AsName").toString(QFileInfo(filePath).completeBaseName());

    int arrayCount = 0;
    int recordCount = 0;
    for (auto it = root.constBegin(); it != root.constEnd(); ++it)
    {
        if (!it.value().isArray())
            continue;

        const int count = it.value().toArray().size();
        if (count > 0)
            ++arrayCount;
        recordCount += count;
    }

    info.asName = asName;
    info.startDate = shortDate(accountSet.value("StartDate").toString());
    info.endPeriod = jsonValueToString(root.value("EndPeriod"));
    info.jsonPath = filePath;
    info.status = QStringLiteral("可查询");
    info.arrayCount = arrayCount;
    info.recordCount = recordCount;
    info.valid = true;
    return true;
}

bool JsonQueryWidget::loadJsonFile(const QString &filePath, QString *errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("打开 JSON 失败");
        return false;
    }

    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("JSON 解析失败：") + jsonError.errorString();
        return false;
    }

    m_currentDoc = doc;
    m_currentJsonPath = filePath;
    buildIndexes();
    populateDatasetCombo();

    if (errorMessage)
        errorMessage->clear();
    return true;
}

void JsonQueryWidget::populateDatasetCombo()
{
    const QString currentSource = m_pDatasetCombo->currentData().toString();
    m_pDatasetCombo->blockSignals(true);
    m_pDatasetCombo->clear();
    m_pDatasetCombo->addItem(QStringLiteral("全部数据"), QString());

    if (m_currentDoc.isObject())
    {
        const QJsonObject root = m_currentDoc.object();
        for (auto it = root.constBegin(); it != root.constEnd(); ++it)
        {
            if (!it.value().isArray())
                continue;

            const int count = it.value().toArray().size();
            if (count <= 0)
                continue;

            const QString source = it.key();
            m_pDatasetCombo->addItem(QStringLiteral("%1 (%2)").arg(displayNameForSource(source)).arg(count), source);
        }
    }

    const int index = m_pDatasetCombo->findData(currentSource);
    if (index >= 0)
        m_pDatasetCombo->setCurrentIndex(index);
    m_pDatasetCombo->blockSignals(false);
}

void JsonQueryWidget::buildIndexes()
{
    m_acctNameByCode.clear();
    m_acctNameByYearAndCode.clear();
    m_acctProjTypesByCode.clear();
    m_acctProjTypesByYearAndCode.clear();
    m_projectTypeNames.clear();

    if (!m_currentDoc.isObject())
        return;

    const QJsonObject root = m_currentDoc.object();
    const QJsonArray projectTypes = root.value("GLProjectTypeList").toArray();
    for (const QJsonValue &value : projectTypes)
    {
        const QJsonObject obj = value.toObject();
        const QString code = obj.value("ProjTypeCode").toString();
        const QString name = obj.value("ProjTypeName").toString();
        if (!code.isEmpty())
            m_projectTypeNames.insert(code, name);
    }

    const QJsonArray accounts = root.value("GLAccountList").toArray();
    for (const QJsonValue &value : accounts)
    {
        const QJsonObject obj = value.toObject();
        const QString acctCode = obj.value("AcctCode").toString();
        if (acctCode.isEmpty())
            continue;

        const QString year = yearFromObject(obj);
        const QString acctName = firstValueByKeys(obj, QStringList() << "AcctName" << "AcctFullName");
        if (!acctName.isEmpty() && !m_acctNameByCode.contains(acctCode))
            m_acctNameByCode.insert(acctCode, acctName);

        QStringList projTypes;
        for (int i = 1; i <= 6; ++i)
            projTypes.append(obj.value(QString("AcctProjTypeCode%1").arg(i)).toString());

        if (!m_acctProjTypesByCode.contains(acctCode))
            m_acctProjTypesByCode.insert(acctCode, projTypes);

        if (!year.isEmpty())
        {
            const QString key = year + "\n" + acctCode;
            if (!acctName.isEmpty())
                m_acctNameByYearAndCode.insert(key, acctName);
            m_acctProjTypesByYearAndCode.insert(key, projTypes);
        }
    }
}

void JsonQueryWidget::runQuery()
{
    startQuery();
}

QList<JsonQueryRecord> JsonQueryWidget::queryRecords() const
{
    QList<JsonQueryRecord> records;
    if (!m_currentDoc.isObject())
        return records;

    const QString selectedSource = m_pDatasetCombo->currentData().toString();
    const QJsonObject root = m_currentDoc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it)
    {
        if (!it.value().isArray())
            continue;

        const QString source = it.key();
        if (!selectedSource.isEmpty() && source != selectedSource)
            continue;

        const QJsonArray array = it.value().toArray();
        for (const QJsonValue &value : array)
        {
            if (!value.isObject())
                continue;

            const QJsonObject obj = value.toObject();
            const JsonQueryRecord record = makeRecord(source, obj);
            if (recordMatches(record, obj))
                records.append(record);
        }
    }

    return records;
}

JsonQueryRecord JsonQueryWidget::makeRecord(const QString &source, const QJsonObject &obj) const
{
    JsonQueryRecord record;
    record.source = displayNameForSource(source);
    record.year = yearFromObject(obj);
    record.period = periodFromObject(obj);
    record.acctCode = firstValueByKeys(obj, QStringList()
        << "AcctCode" << "AccountCode" << "SubjectCode" << "Code");
    record.acctName = firstValueByKeys(obj, QStringList()
        << "AcctName" << "AcctFullName" << "SubjectName" << "ProjName" << "ProjectName" << "Name" << "ProjTypeName");

    if (record.acctName.isEmpty() && !record.acctCode.isEmpty())
        record.acctName = accountNameFor(record.year, record.acctCode);

    QStringList customerCodes = valuesByKeys(obj, QStringList()
        << "CustomerCode" << "CustCode" << "CustNo" << "CustNO" << "ClientCode" << "ClientNo");
    customerCodes.append(projectCodesByTypeName(obj, record.year, record.acctCode, QStringLiteral("客户")));
    record.customerCode = joinUnique(customerCodes);

    QStringList supplierCodes = valuesByKeys(obj, QStringList()
        << "SupplierCode" << "VendorCode" << "SupplyCode" << "ProviderCode");
    supplierCodes.append(projectCodesByTypeName(obj, record.year, record.acctCode, QStringLiteral("供应商")));
    record.supplierCode = joinUnique(supplierCodes);

    const QString projectTypeCode = obj.value("ProjTypeCode").toString();
    const QString projectTypeName = m_projectTypeNames.value(projectTypeCode);
    const QString projectCode = firstValueByKeys(obj, QStringList() << "ProjCode" << "ProjectCode" << "Code");
    if (record.customerCode.isEmpty() && !projectCode.isEmpty()
        && (projectTypeName.contains(QStringLiteral("客户")) || projectTypeCode == "003"))
    {
        record.customerCode = projectCode;
    }
    if (record.supplierCode.isEmpty() && !projectCode.isEmpty()
        && (projectTypeName.contains(QStringLiteral("供应商")) || projectTypeCode == "004"))
    {
        record.supplierCode = projectCode;
    }

    record.summary = firstValueByKeys(obj, QStringList()
        << "Summary" << "摘要" << "AcctName" << "AcctFullName" << "ProjName" << "ProjectName" << "Name" << "ProjTypeName");
    record.debit = firstValueByKeys(obj, QStringList()
        << "DebitBal" << "Debit" << "DebitAmount" << "FX_LJ_JF" << "JF");
    record.credit = firstValueByKeys(obj, QStringList()
        << "CreditBal" << "Credit" << "CreditAmount" << "FX_LJ_DF" << "DF");
    record.amount = firstValueByKeys(obj, QStringList()
        << "FX_Amount_s" << "Amount" << "Amt" << "BegBal" << "FX_BAmount_s");
    record.rawJson = fullRawJson(obj);
    return record;
}

bool JsonQueryWidget::recordMatches(const JsonQueryRecord &record, const QJsonObject &obj) const
{
    const QString year = m_pYearEdit->text().trimmed();
    const QString acctCode = m_pAcctCodeEdit->text().trimmed();
    const QString customerCode = m_pCustomerCodeEdit->text().trimmed();
    const QString supplierCode = m_pSupplierCodeEdit->text().trimmed();
    const QString keyword = m_pKeywordEdit->text().trimmed();

    if (!year.isEmpty() && !containsText(record.year, year) && !sourceObjectContains(obj, year))
        return false;

    if (!acctCode.isEmpty()
        && !containsText(record.acctCode, acctCode)
        && !containsText(record.acctName, acctCode))
    {
        return false;
    }

    if (!customerCode.isEmpty()
        && !containsText(record.customerCode, customerCode)
        && !containsText(projectCodeString(obj), customerCode))
    {
        return false;
    }

    if (!supplierCode.isEmpty()
        && !containsText(record.supplierCode, supplierCode)
        && !containsText(projectCodeString(obj), supplierCode))
    {
        return false;
    }

    if (!keyword.isEmpty())
    {
        const QString combined = record.source + "\n"
            + record.year + "\n"
            + record.period + "\n"
            + record.acctCode + "\n"
            + record.acctName + "\n"
            + record.customerCode + "\n"
            + record.supplierCode + "\n"
            + record.summary + "\n"
            + record.rawJson;
        if (!containsText(combined, keyword))
            return false;
    }

    return true;
}

void JsonQueryWidget::fillResultTable(const QList<JsonQueryRecord> &records)
{
    m_currentResultRecords = records;
    m_currentPage = 0;
    renderCurrentPage();
}

void JsonQueryWidget::renderCurrentPage()
{
    const int totalRows = m_currentResultRecords.size();
    const int pages = pageCount();
    if (m_currentPage >= pages)
        m_currentPage = qMax(0, pages - 1);

    m_rawJsonColumn = m_currentResultColumns.indexOf(QStringLiteral("原始 JSON"));
    m_pResultTable->clear();
    m_pResultTable->setColumnCount(m_currentResultColumns.size());
    m_pResultTable->setHorizontalHeaderLabels(m_currentResultColumns);
    m_pResultTable->setRowCount(0);

    if (m_currentResultColumns.isEmpty() || totalRows == 0)
    {
        updatePageControls();
        return;
    }

    const int start = m_currentPage * m_pageSize;
    const int end = qMin(start + m_pageSize, totalRows);
    m_pResultTable->setRowCount(end - start);

    for (int sourceRow = start; sourceRow < end; ++sourceRow)
    {
        const int tableRow = sourceRow - start;
        const JsonQueryRecord &record = m_currentResultRecords[sourceRow];
        for (int col = 0; col < m_currentResultColumns.size(); ++col)
        {
            const QString column = m_currentResultColumns[col];
            QString value;
            if (column == QStringLiteral("来源"))
                value = record.source;
            else if (column == QStringLiteral("原始 JSON"))
                value = record.rawJson;
            else
                value = record.fields.value(column);

            const QString displayText = (col == m_rawJsonColumn) ? QStringLiteral("完整 JSON") : value;
            QTableWidgetItem *item = new QTableWidgetItem(displayText);
            item->setData(Qt::UserRole, value);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            if (col == m_rawJsonColumn)
                item->setToolTip(QStringLiteral("右键可复制完整原始 JSON"));
            else
                item->setToolTip(value);
            m_pResultTable->setItem(tableRow, col, item);
        }
    }

    for (int col = 0; col < m_currentResultColumns.size(); ++col)
    {
        if (col == m_rawJsonColumn)
        {
            m_pResultTable->setColumnHidden(col, true);
            continue;
        }

        m_pResultTable->setColumnHidden(col, false);
        if (m_currentResultColumns[col] == QStringLiteral("来源"))
            m_pResultTable->setColumnWidth(col, 90);
        else
            m_pResultTable->setColumnWidth(col, 130);
    }

    updatePageControls();
}

void JsonQueryWidget::clearResults(const QString &message)
{
    ++m_queryRequestId;
    m_currentResultRecords.clear();
    m_currentResultColumns.clear();
    m_currentPage = 0;
    m_rawJsonColumn = -1;
    m_pResultTable->clear();
    m_pResultTable->setColumnCount(0);
    m_pResultTable->setRowCount(0);
    if (!message.isEmpty())
        m_pResultLabel->setText(message);
    updatePageControls();
}

void JsonQueryWidget::updatePageControls()
{
    const int pages = pageCount();
    const bool hasPages = pages > 0;
    if (!hasPages)
        m_currentPage = 0;

    m_pPageLabel->setText(hasPages
        ? QStringLiteral("第 %1 / %2 页").arg(m_currentPage + 1).arg(pages)
        : QStringLiteral("第 0 / 0 页"));

    m_pBtnFirstPage->setEnabled(hasPages && m_currentPage > 0);
    m_pBtnPrevPage->setEnabled(hasPages && m_currentPage > 0);
    m_pBtnNextPage->setEnabled(hasPages && m_currentPage + 1 < pages);
    m_pBtnLastPage->setEnabled(hasPages && m_currentPage + 1 < pages);
    m_pPageSizeCombo->setEnabled(true);
}

int JsonQueryWidget::pageCount() const
{
    if (m_currentResultRecords.isEmpty() || m_pageSize <= 0)
        return 0;

    return (m_currentResultRecords.size() + m_pageSize - 1) / m_pageSize;
}

QString JsonQueryWidget::resultCellCopyText(int row, int column) const
{
    QTableWidgetItem *item = m_pResultTable->item(row, column);
    if (!item)
        return QString();

    const QVariant fullValue = item->data(Qt::UserRole);
    if (fullValue.isValid())
        return fullValue.toString();

    return item->text();
}

QString JsonQueryWidget::selectedResultText(bool includeHiddenRawJson) const
{
    QList<int> rows;
    const QModelIndexList selectedRows = m_pResultTable->selectionModel()
        ? m_pResultTable->selectionModel()->selectedRows()
        : QModelIndexList();

    for (const QModelIndex &index : selectedRows)
    {
        if (!rows.contains(index.row()))
            rows.append(index.row());
    }
    std::sort(rows.begin(), rows.end());

    if (!rows.isEmpty())
    {
        QStringList lines;
        for (int row : rows)
        {
            QStringList cells;
            for (int col = 0; col < m_pResultTable->columnCount(); ++col)
            {
                if (m_pResultTable->isColumnHidden(col) && !(includeHiddenRawJson && col == m_rawJsonColumn))
                    continue;
                cells.append(resultCellCopyText(row, col));
            }
            lines.append(cells.join("\t"));
        }
        return lines.join("\n");
    }

    const QModelIndexList indexes = m_pResultTable->selectionModel()
        ? m_pResultTable->selectionModel()->selectedIndexes()
        : QModelIndexList();
    if (indexes.isEmpty())
        return QString();

    QList<int> selectedRowList;
    QList<int> selectedColumnList;
    for (const QModelIndex &index : indexes)
    {
        if (!selectedRowList.contains(index.row()))
            selectedRowList.append(index.row());
        if (!selectedColumnList.contains(index.column()) && !m_pResultTable->isColumnHidden(index.column()))
            selectedColumnList.append(index.column());
    }
    std::sort(selectedRowList.begin(), selectedRowList.end());
    std::sort(selectedColumnList.begin(), selectedColumnList.end());

    QStringList lines;
    for (int row : selectedRowList)
    {
        QStringList cells;
        for (int col : selectedColumnList)
            cells.append(resultCellCopyText(row, col));
        lines.append(cells.join("\t"));
    }
    return lines.join("\n");
}

bool JsonQueryWidget::importLocalFile(const QString &filePath, QString *errorMessage)
{
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("文件不存在");
        return false;
    }

    if (fileInfo.suffix().compare("zip", Qt::CaseInsensitive) == 0)
        return extractZipToQueryDir(filePath, errorMessage);

    if (fileInfo.suffix().compare("json", Qt::CaseInsensitive) == 0)
        return copyJsonToQueryDir(filePath, errorMessage);

    if (errorMessage)
        *errorMessage = QStringLiteral("仅支持 ZIP 或 JSON 文件");
    return false;
}

bool JsonQueryWidget::extractZipToQueryDir(const QString &zipPath, QString *errorMessage)
{
    ensureQueryDir();

    QFileInfo zipInfo(zipPath);
    if (!zipInfo.exists() || !zipInfo.isFile())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("压缩包不存在");
        return false;
    }

    const QString outDir = QDir::cleanPath(queryDir() + "/" + zipInfo.completeBaseName());
    QDir targetDir(outDir);
    if (targetDir.exists())
    {
        const QString rootCanonical = QDir::fromNativeSeparators(QDir(queryDir()).canonicalPath());
        const QString targetCanonical = QDir::fromNativeSeparators(targetDir.canonicalPath());
        if (rootCanonical.isEmpty() || targetCanonical.isEmpty()
            || !targetCanonical.startsWith(rootCanonical + "/", Qt::CaseInsensitive))
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("目标目录校验失败");
            return false;
        }
        targetDir.removeRecursively();
    }

    if (!QDir().mkpath(outDir))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("创建解压目录失败");
        return false;
    }

    const QString script =
        "$ErrorActionPreference='Stop';"
        "Add-Type -AssemblyName System.IO.Compression.FileSystem;"
        "$zip=$env:LEMON_JSON_ZIP_PATH;$dest=$env:LEMON_JSON_ZIP_DEST;"
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

    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LEMON_JSON_ZIP_PATH", zipInfo.absoluteFilePath());
    env.insert("LEMON_JSON_ZIP_DEST", outDir);
    process.setProcessEnvironment(env);
    const QStringList args = QStringList()
        << "-NoProfile"
        << "-ExecutionPolicy"
        << "Bypass"
        << "-Command"
        << script;
    AppLogger::operation(QStringLiteral("JSON 查询解压 ZIP zip=%1 dest=%2")
        .arg(QDir::toNativeSeparators(zipInfo.absoluteFilePath()), QDir::toNativeSeparators(outDir)));
    AppLogger::command("powershell.exe", args);
    process.start("powershell.exe", args);

    if (!process.waitForFinished(60000))
    {
        process.kill();
        process.waitForFinished(3000);
        if (errorMessage)
            *errorMessage = QStringLiteral("解压超时");
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        const QString stdErr = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (errorMessage)
        {
            if (stdErr.contains("No JSON files found in ZIP", Qt::CaseInsensitive))
                *errorMessage = QStringLiteral("ZIP 中未找到 JSON 文件");
            else
                *errorMessage = stdErr.isEmpty() ? QStringLiteral("解压失败") : stdErr;
        }
        return false;
    }

    return true;
}

bool JsonQueryWidget::copyJsonToQueryDir(const QString &jsonPath, QString *errorMessage)
{
    ensureQueryDir();

    QFileInfo jsonInfo(jsonPath);
    QString targetPath = QDir::cleanPath(queryDir() + "/" + jsonInfo.fileName());
    int suffix = 1;
    while (QFile::exists(targetPath))
    {
        targetPath = QDir::cleanPath(queryDir() + "/"
            + jsonInfo.completeBaseName()
            + QStringLiteral("_%1.").arg(suffix++)
            + jsonInfo.suffix());
    }

    if (!QFile::copy(jsonInfo.absoluteFilePath(), targetPath))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("复制 JSON 文件失败");
        return false;
    }

    return true;
}

int JsonQueryWidget::selectedFileRow() const
{
    const int row = m_pFileTable->currentRow();
    if (row < 0 || row >= m_fileList.size())
        return -1;
    if (!m_fileList[row].valid)
        return -1;
    return row;
}

QString JsonQueryWidget::displayNameForSource(const QString &source) const
{
    if (source == "GLAccountList")
        return QStringLiteral("科目");
    if (source == "GLBalanceList")
        return QStringLiteral("余额");
    if (source == "GLVoucherList")
        return QStringLiteral("凭证");
    if (source == "GLProjectList")
        return QStringLiteral("辅助核算");
    if (source == "GLProjectTypeList")
        return QStringLiteral("辅助核算类别");
    if (source == "FADepreciationList")
        return QStringLiteral("折旧");
    if (source == "GLFixedAssetList")
        return QStringLiteral("固定资产");
    if (source == "GLInvoiceList")
        return QStringLiteral("发票");
    if (source == "GLJournalList")
        return QStringLiteral("日记账");
    if (source == "GLPeriodList")
        return QStringLiteral("期间");
    return source;
}

QString JsonQueryWidget::accountNameFor(const QString &year, const QString &acctCode) const
{
    if (acctCode.isEmpty())
        return QString();

    if (!year.isEmpty())
    {
        const QString key = year + "\n" + acctCode;
        if (m_acctNameByYearAndCode.contains(key))
            return m_acctNameByYearAndCode.value(key);
    }

    return m_acctNameByCode.value(acctCode);
}

QStringList JsonQueryWidget::projectCodesByTypeName(const QJsonObject &obj, const QString &year, const QString &acctCode, const QString &typeNamePart) const
{
    QStringList result;
    if (acctCode.isEmpty())
        return result;

    QStringList projTypes;
    if (!year.isEmpty())
        projTypes = m_acctProjTypesByYearAndCode.value(year + "\n" + acctCode);
    if (projTypes.isEmpty())
        projTypes = m_acctProjTypesByCode.value(acctCode);

    for (int i = 1; i <= 6 && i <= projTypes.size(); ++i)
    {
        const QString typeCode = projTypes[i - 1];
        const QString typeName = m_projectTypeNames.value(typeCode);
        const bool isCustomer = typeNamePart == QStringLiteral("客户")
            && (typeName.contains(QStringLiteral("客户")) || typeCode == "003");
        const bool isSupplier = typeNamePart == QStringLiteral("供应商")
            && (typeName.contains(QStringLiteral("供应商")) || typeCode == "004");

        if (!isCustomer && !isSupplier)
            continue;

        const QString projectCode = jsonValueToString(obj.value(QString("ProjCode%1").arg(i))).trimmed();
        if (!projectCode.isEmpty() && !result.contains(projectCode))
            result.append(projectCode);
    }

    return result;
}

void JsonQueryWidget::onBackClicked()
{
    AppLogger::operation(QStringLiteral("JSON 查询返回主界面"));
    emit backToMain();
}

void JsonQueryWidget::onBrowseClicked()
{
    AppLogger::operation(QStringLiteral("JSON 查询选择文件"));
    const QStringList filePaths = QFileDialog::getOpenFileNames(this,
        QStringLiteral("选择数据包或 JSON"),
        QString(),
        QStringLiteral("数据包 (*.zip *.json);;ZIP 压缩包 (*.zip);;JSON 文件 (*.json)"));

    if (filePaths.isEmpty())
        return;

    AppLogger::operation(QStringLiteral("JSON 查询已选择文件 files=%1").arg(filePaths.join("|")));
    startImportFiles(filePaths);
}

void JsonQueryWidget::onRefreshClicked()
{
    AppLogger::operation(QStringLiteral("JSON 查询刷新目录"));
    loadFileList();
}

void JsonQueryWidget::onLoadSelectedClicked()
{
    AppLogger::operation(QStringLiteral("JSON 查询加载选中文件"));
    const int row = selectedFileRow();
    if (row < 0)
    {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先选择一个可查询的 JSON"));
        return;
    }

    startLoadFile(m_fileList[row].jsonPath);
}

void JsonQueryWidget::onSearchClicked()
{
    AppLogger::operation(QStringLiteral("JSON 查询点击查询"));
    runQuery();
}

void JsonQueryWidget::onClearClicked()
{
    AppLogger::operation(QStringLiteral("JSON 查询清空查询条件"));
    m_pYearEdit->clear();
    m_pAcctCodeEdit->clear();
    m_pCustomerCodeEdit->clear();
    m_pSupplierCodeEdit->clear();
    m_pKeywordEdit->clear();
    clearResults(QStringLiteral("查询条件已清空，请点击“查询”重新查询。"));
}

void JsonQueryWidget::onResultTableContextMenuRequested(const QPoint &pos)
{
    const int row = m_pResultTable->rowAt(pos.y());
    if (row >= 0)
    {
        if (!m_pResultTable->selectionModel()->isRowSelected(row, QModelIndex()))
            m_pResultTable->selectRow(row);
        m_pResultTable->setCurrentCell(row, qMax(0, m_pResultTable->currentColumn()));
    }

    QMenu menu(this);
    QAction *copySelectionAction = menu.addAction(QStringLiteral("复制选中内容"));
    QAction *copyRowsAction = menu.addAction(QStringLiteral("复制选中行（含完整原始 JSON）"));
    QAction *copySelectedRawJsonAction = menu.addAction(QStringLiteral("复制选中行原始 JSON"));
    QAction *copyRawJsonAction = menu.addAction(QStringLiteral("复制当前行完整 JSON"));

    const bool hasRows = m_pResultTable->rowCount() > 0;
    copySelectionAction->setEnabled(hasRows && !m_pResultTable->selectedItems().isEmpty());
    copyRowsAction->setEnabled(hasRows && !m_pResultTable->selectedItems().isEmpty());
    copySelectedRawJsonAction->setEnabled(hasRows && !m_pResultTable->selectedItems().isEmpty() && m_rawJsonColumn >= 0);
    copyRawJsonAction->setEnabled((row >= 0 || m_pResultTable->currentRow() >= 0) && m_rawJsonColumn >= 0);

    QAction *selectedAction = menu.exec(m_pResultTable->viewport()->mapToGlobal(pos));
    if (selectedAction == copySelectionAction)
    {
        const QString text = selectedResultText(false);
        if (!text.isEmpty())
            QApplication::clipboard()->setText(text);
    }
    else if (selectedAction == copyRowsAction)
    {
        onCopyResultSelection();
    }
    else if (selectedAction == copySelectedRawJsonAction)
    {
        onCopySelectedRawJson();
    }
    else if (selectedAction == copyRawJsonAction)
    {
        onCopyCurrentRawJson();
    }
}

void JsonQueryWidget::onCopyResultSelection()
{
    QString text = selectedResultText(true);
    if (text.isEmpty() && m_pResultTable->currentRow() >= 0)
    {
        QStringList cells;
        const int row = m_pResultTable->currentRow();
        for (int col = 0; col < m_pResultTable->columnCount(); ++col)
        {
            if (m_pResultTable->isColumnHidden(col) && col != m_rawJsonColumn)
                continue;
            cells.append(resultCellCopyText(row, col));
        }
        text = cells.join("\t");
    }

    if (!text.isEmpty())
        QApplication::clipboard()->setText(text);
}

void JsonQueryWidget::onCopyCurrentRawJson()
{
    const int row = m_pResultTable->currentRow();
    if (row < 0 || m_rawJsonColumn < 0)
        return;

    const QString rawJson = resultCellCopyText(row, m_rawJsonColumn);
    if (!rawJson.isEmpty())
        QApplication::clipboard()->setText(rawJson);
}

void JsonQueryWidget::onCopySelectedRawJson()
{
    if (m_rawJsonColumn < 0)
        return;

    QList<int> rows;
    const QModelIndexList selectedRows = m_pResultTable->selectionModel()
        ? m_pResultTable->selectionModel()->selectedRows()
        : QModelIndexList();
    for (const QModelIndex &index : selectedRows)
    {
        if (!rows.contains(index.row()))
            rows.append(index.row());
    }

    if (rows.isEmpty() && m_pResultTable->currentRow() >= 0)
        rows.append(m_pResultTable->currentRow());

    std::sort(rows.begin(), rows.end());
    QStringList rawList;
    for (int row : rows)
    {
        const QString rawJson = resultCellCopyText(row, m_rawJsonColumn);
        if (!rawJson.isEmpty())
            rawList.append(rawJson);
    }

    if (!rawList.isEmpty())
        QApplication::clipboard()->setText(rawList.join("\n"));
}

void JsonQueryWidget::onFirstPageClicked()
{
    if (m_currentPage == 0)
        return;
    m_currentPage = 0;
    renderCurrentPage();
}

void JsonQueryWidget::onPrevPageClicked()
{
    if (m_currentPage <= 0)
        return;
    --m_currentPage;
    renderCurrentPage();
}

void JsonQueryWidget::onNextPageClicked()
{
    if (m_currentPage + 1 >= pageCount())
        return;
    ++m_currentPage;
    renderCurrentPage();
}

void JsonQueryWidget::onLastPageClicked()
{
    const int pages = pageCount();
    if (pages <= 0 || m_currentPage == pages - 1)
        return;
    m_currentPage = pages - 1;
    renderCurrentPage();
}

void JsonQueryWidget::onPageSizeChanged(int)
{
    const int newPageSize = m_pPageSizeCombo->currentData().toInt();
    if (newPageSize <= 0 || newPageSize == m_pageSize)
        return;

    const int firstVisibleIndex = m_currentPage * m_pageSize;
    m_pageSize = newPageSize;
    m_currentPage = m_pageSize > 0 ? firstVisibleIndex / m_pageSize : 0;
    renderCurrentPage();
}

void JsonQueryWidget::onFileCurrentCellChanged(int currentRow, int, int, int)
{
    if (m_blockFileSelection)
        return;
    if (currentRow < 0 || currentRow >= m_fileList.size() || !m_fileList[currentRow].valid)
        return;
    if (m_fileList[currentRow].jsonPath == m_currentJsonPath)
        return;

    startLoadFile(m_fileList[currentRow].jsonPath);
}

void JsonQueryWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (hasSupportedDataFile(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void JsonQueryWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (hasSupportedDataFile(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void JsonQueryWidget::dropEvent(QDropEvent *event)
{
    if (!event->possibleActions().testFlag(Qt::CopyAction))
    {
        event->ignore();
        return;
    }
    if (!hasSupportedDataFile(event->mimeData()))
    {
        event->ignore();
        return;
    }

    QStringList filePaths;
    for (const QUrl &url : event->mimeData()->urls())
    {
        if (!url.isLocalFile())
            continue;

        if (!isSupportedDataFile(url.toLocalFile()))
            continue;
        const QString inputPath = RepairInputFileUtils::copyDroppedFile(url.toLocalFile(), this);
        if (!inputPath.isEmpty())
            filePaths.append(inputPath);
    }

    if (filePaths.isEmpty())
    {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
    AppLogger::operation(QStringLiteral("JSON 查询拖入文件 files=%1").arg(filePaths.join("|")));
    startImportFiles(filePaths);
}
