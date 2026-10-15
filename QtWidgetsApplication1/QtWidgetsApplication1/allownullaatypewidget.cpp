#include "Common/repairinputfileutils.h"
#include "allownullaatypewidget.h"
#include "Common/globaldata.h"
#include "Common/logger.h"
#include "Common/repairfilenameutils.h"
#include "Common/repairinputfilepickerwidget.h"
#include "Common/repairpathconfigwidget.h"
#include "Common/repairpathsettings.h"

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
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QList>
#include <QMap>
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
#include <QtGlobal>
#include <algorithm>

namespace
{
const int kMaxDetailLogCount = 200;

struct AllowNullAaProcessResult
{
    bool ok = false;
    QString inputPath;
    QString outputZipPath;
    QString accountSetName;
    QString errorMessage;
    QStringList logs;
    int jsonCount = 0;
    int accountCount = 0;
    int voucherCount = 0;
    int missingAaTypeCount = 0;
    int appendedAaTypeCount = 0;
    int affectedAccountCount = 0;
    int removedBalanceCount = 0;
    int missingAccountCount = 0;
};

struct JsonRange
{
    int start = -1;
    int end = -1;

    bool isValid() const
    {
        return start >= 0 && end >= start;
    }
};

struct JsonReplacement
{
    JsonRange range;
    QByteArray data;
};

struct AccountRecord
{
    QByteArray originalJson;
    QJsonObject object;
    QString acctCode;
    bool changed = false;
};

bool isZipFile(const QString &filePath)
{
    const QFileInfo info(filePath);
    return info.isFile() && info.suffix().compare("zip", Qt::CaseInsensitive) == 0;
}

bool hasZipFile(const QMimeData *mimeData)
{
    if (!mimeData || !mimeData->hasUrls())
        return false;

    for (const QUrl &url : mimeData->urls())
    {
        if (url.isLocalFile() && isZipFile(url.toLocalFile()))
            return true;
    }
    return false;
}

bool isJsonWhitespace(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

void skipJsonWhitespace(const QByteArray &data, int *pos, int limit)
{
    if (*pos == 0
        && limit >= 3
        && static_cast<unsigned char>(data.at(0)) == 0xEF
        && static_cast<unsigned char>(data.at(1)) == 0xBB
        && static_cast<unsigned char>(data.at(2)) == 0xBF)
    {
        *pos = 3;
    }

    while (*pos < limit && isJsonWhitespace(data.at(*pos)))
        ++(*pos);
}

bool skipJsonString(const QByteArray &data, int *pos, int limit)
{
    if (*pos >= limit || data.at(*pos) != '"')
        return false;

    ++(*pos);
    while (*pos < limit)
    {
        const char ch = data.at(*pos);
        if (ch == '\\')
        {
            *pos += 2;
            continue;
        }
        ++(*pos);
        if (ch == '"')
            return true;
    }

    return false;
}

int findMatchingJsonToken(const QByteArray &data, int start, char openToken, char closeToken)
{
    if (start < 0 || start >= data.size() || data.at(start) != openToken)
        return -1;

    int depth = 0;
    int pos = start;
    while (pos < data.size())
    {
        const char ch = data.at(pos);
        if (ch == '"')
        {
            if (!skipJsonString(data, &pos, data.size()))
                return -1;
            continue;
        }

        if (ch == openToken)
        {
            ++depth;
        }
        else if (ch == closeToken)
        {
            --depth;
            if (depth == 0)
                return pos;
        }
        ++pos;
    }

    return -1;
}

bool findTopLevelArrayRange(const QByteArray &data, const QByteArray &key, JsonRange *range, QString *errorMessage)
{
    if (!range)
        return false;

    int pos = 0;
    skipJsonWhitespace(data, &pos, data.size());
    if (pos >= data.size() || data.at(pos) != '{')
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("JSON \u6839\u8282\u70b9\u4e0d\u662f\u5bf9\u8c61");
        return false;
    }

    int objectDepth = 0;
    int arrayDepth = 0;
    while (pos < data.size())
    {
        const char ch = data.at(pos);
        if (ch == '"')
        {
            const int stringStart = pos;
            if (!skipJsonString(data, &pos, data.size()))
            {
                if (errorMessage)
                    *errorMessage = QStringLiteral("JSON \u5b57\u7b26\u4e32\u672a\u6b63\u786e\u7ed3\u675f");
                return false;
            }

            if (objectDepth == 1 && arrayDepth == 0)
            {
                const QByteArray rawKey = data.mid(stringStart + 1, pos - stringStart - 2);
                int next = pos;
                skipJsonWhitespace(data, &next, data.size());
                if (next < data.size() && data.at(next) == ':' && rawKey == key)
                {
                    ++next;
                    skipJsonWhitespace(data, &next, data.size());
                    if (next >= data.size() || data.at(next) != '[')
                    {
                        if (errorMessage)
                            *errorMessage = QStringLiteral("%1 \u4e0d\u662f JSON \u6570\u7ec4").arg(QString::fromLatin1(key));
                        return false;
                    }

                    const int end = findMatchingJsonToken(data, next, '[', ']');
                    if (end < 0)
                    {
                        if (errorMessage)
                            *errorMessage = QStringLiteral("%1 \u6570\u7ec4\u672a\u6b63\u786e\u7ed3\u675f").arg(QString::fromLatin1(key));
                        return false;
                    }

                    range->start = next;
                    range->end = end;
                    return true;
                }
            }
            continue;
        }

        if (ch == '{')
        {
            ++objectDepth;
        }
        else if (ch == '}')
        {
            --objectDepth;
        }
        else if (ch == '[')
        {
            ++arrayDepth;
        }
        else if (ch == ']')
        {
            --arrayDepth;
        }
        ++pos;
    }

    if (errorMessage)
        *errorMessage = QStringLiteral("\u672a\u627e\u5230 %1").arg(QString::fromLatin1(key));
    return false;
}

bool findOptionalTopLevelArrayRange(const QByteArray &data, const QByteArray &key, JsonRange *range, QString *errorMessage)
{
    QString localError;
    if (findTopLevelArrayRange(data, key, range, &localError))
        return true;

    if (localError == QStringLiteral("\u672a\u627e\u5230 %1").arg(QString::fromLatin1(key)))
        return false;

    if (errorMessage)
        *errorMessage = localError;
    return false;
}

bool nextJsonObjectRangeInArray(const QByteArray &data, const JsonRange &arrayRange, int *pos, JsonRange *objectRange, QString *errorMessage)
{
    if (!pos || !objectRange || !arrayRange.isValid())
        return false;

    int current = *pos;
    const int limit = arrayRange.end;
    while (current < limit)
    {
        skipJsonWhitespace(data, &current, limit);
        if (current < limit && data.at(current) == ',')
        {
            ++current;
            continue;
        }
        skipJsonWhitespace(data, &current, limit);
        break;
    }

    if (current >= limit)
    {
        *pos = current;
        return false;
    }

    if (data.at(current) != '{')
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u6570\u7ec4\u5185\u5b58\u5728\u975e\u5bf9\u8c61\u9879");
        *pos = current;
        return false;
    }

    const int objectEnd = findMatchingJsonToken(data, current, '{', '}');
    if (objectEnd < 0 || objectEnd > limit)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u6570\u7ec4\u5185\u5bf9\u8c61\u672a\u6b63\u786e\u7ed3\u675f");
        *pos = current;
        return false;
    }

    objectRange->start = current;
    objectRange->end = objectEnd;
    *pos = objectEnd + 1;
    return true;
}

QByteArray jsonForRange(const QByteArray &data, const JsonRange &range)
{
    return data.mid(range.start, range.end - range.start + 1);
}

QString textFromJsonValue(const QJsonValue &value)
{
    if (value.isString())
        return value.toString().trimmed();

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

bool isBlankValue(const QJsonValue &value)
{
    return textFromJsonValue(value).isEmpty();
}

bool anyProjectCodeHasValue(const QJsonObject &object)
{
    for (int i = 1; i <= 6; ++i)
    {
        if (!isBlankValue(object.value(QStringLiteral("ProjCode%1").arg(i))))
            return true;
    }
    return false;
}

bool allProjectCodesBlank(const QJsonObject &object)
{
    for (int i = 1; i <= 6; ++i)
    {
        if (!isBlankValue(object.value(QStringLiteral("ProjCode%1").arg(i))))
            return false;
    }
    return true;
}

QString voucherDisplayName(const QJsonObject &voucher, int row)
{
    QString displayName = textFromJsonValue(voucher.value("MakeDate"))
        + textFromJsonValue(voucher.value("VchGroup"))
        + textFromJsonValue(voucher.value("GroupNum"));

    if (displayName.isEmpty())
        displayName = QStringLiteral("\u7b2c%1\u884c").arg(row + 1);

    return displayName;
}

void appendDetailLog(AllowNullAaProcessResult *summary, const QString &message)
{
    if (!summary)
        return;

    if (summary->logs.size() < kMaxDetailLogCount)
    {
        summary->logs.append(message);
    }
    else if (summary->logs.size() == kMaxDetailLogCount)
    {
        summary->logs.append(QStringLiteral("\u660e\u7ec6\u8f83\u591a\uff0c\u540e\u7eed\u4ec5\u7edf\u8ba1\u6c47\u603b\u3002"));
    }
}

bool allowNullAaTypesContains(const QString &allowNullAaTypes, const QString &aaTypeCode)
{
    const QStringList parts = allowNullAaTypes.split(',');
    for (const QString &part : parts)
    {
        if (part.trimmed() == aaTypeCode)
            return true;
    }
    return false;
}

bool appendAllowNullAaType(QJsonObject *account, const QString &aaTypeCode)
{
    if (!account || aaTypeCode.isEmpty())
        return false;

    const QString current = textFromJsonValue(account->value("AllowNullAATypes"));
    if (allowNullAaTypesContains(current, aaTypeCode))
        return false;

    QString nextValue = current;
    if (nextValue.isEmpty())
    {
        nextValue = aaTypeCode;
    }
    else if (nextValue.endsWith(','))
    {
        nextValue += aaTypeCode;
    }
    else
    {
        nextValue += "," + aaTypeCode;
    }

    (*account)["AllowNullAATypes"] = nextValue;
    return true;
}

bool parseJsonObjectBytes(const QByteArray &objectJson, QJsonObject *object, QString *errorMessage)
{
    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(objectJson, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("JSON \u5bf9\u8c61\u89e3\u6790\u5931\u8d25\uff1a") + jsonError.errorString();
        return false;
    }

    if (object)
        *object = doc.object();
    return true;
}

QByteArray compactJsonObject(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray buildAccountListJson(const QList<AccountRecord> &accounts)
{
    QByteArray output;
    output.append('[');
    for (int i = 0; i < accounts.size(); ++i)
    {
        if (i > 0)
            output.append(',');

        output.append(accounts.at(i).changed
            ? compactJsonObject(accounts.at(i).object)
            : accounts.at(i).originalJson);
    }
    output.append(']');
    return output;
}

QByteArray applyJsonReplacements(const QByteArray &data, QList<JsonReplacement> replacements)
{
    std::sort(replacements.begin(), replacements.end(), [](const JsonReplacement &left, const JsonReplacement &right) {
        return left.range.start < right.range.start;
    });

    QByteArray output;
    int cursor = 0;
    for (const JsonReplacement &replacement : replacements)
    {
        if (!replacement.range.isValid() || replacement.range.start < cursor)
            continue;

        output.append(data.constData() + cursor, replacement.range.start - cursor);
        output.append(replacement.data);
        cursor = replacement.range.end + 1;
    }

    output.append(data.constData() + cursor, data.size() - cursor);
    return output;
}

bool buildAccountsFromRawJson(const QByteArray &data,
                              const JsonRange &accountRange,
                              QList<AccountRecord> *accounts,
                              QMap<QString, int> *accountIndexByCode,
                              QString *errorMessage)
{
    if (!accounts || !accountIndexByCode)
        return false;

    accounts->clear();
    accountIndexByCode->clear();

    int pos = accountRange.start + 1;
    int index = 0;
    JsonRange objectRange;
    while (nextJsonObjectRangeInArray(data, accountRange, &pos, &objectRange, errorMessage))
    {
        AccountRecord record;
        record.originalJson = jsonForRange(data, objectRange);
        if (!parseJsonObjectBytes(record.originalJson, &record.object, errorMessage))
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("GLAccountList \u7b2c %1 \u6761%2").arg(index + 1).arg(errorMessage->isEmpty() ? QString() : QStringLiteral("：") + *errorMessage);
            return false;
        }

        record.acctCode = textFromJsonValue(record.object.value("AcctCode"));
        if (!record.acctCode.isEmpty() && !accountIndexByCode->contains(record.acctCode))
            accountIndexByCode->insert(record.acctCode, accounts->size());

        accounts->append(record);
        ++index;
    }

    if (pos < accountRange.end)
        return false;

    return true;
}

bool scanVouchersAndUpdateAccounts(const QByteArray &data,
                                   const JsonRange &voucherRange,
                                   QList<AccountRecord> *accounts,
                                   const QMap<QString, int> &accountIndexByCode,
                                   QSet<QString> *balanceRemovalCandidates,
                                   AllowNullAaProcessResult *summary,
                                   QString *errorMessage)
{
    if (!accounts || !balanceRemovalCandidates)
        return false;

    QStringList missingAccountErrors;
    int missingAaTypeCount = 0;
    int appendedAaTypeCount = 0;
    int voucherCount = 0;
    QSet<QString> affectedAccounts;

    int pos = voucherRange.start + 1;
    JsonRange objectRange;
    while (nextJsonObjectRangeInArray(data, voucherRange, &pos, &objectRange, errorMessage))
    {
        QJsonObject voucher;
        const QByteArray voucherJson = jsonForRange(data, objectRange);
        if (!parseJsonObjectBytes(voucherJson, &voucher, errorMessage))
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("GLVoucherList \u7b2c %1 \u6761%2").arg(voucherCount + 1).arg(errorMessage->isEmpty() ? QString() : QStringLiteral("：") + *errorMessage);
            return false;
        }

        const QString acctCode = textFromJsonValue(voucher.value("AcctCode"));
        if (!acctCode.isEmpty())
        {
            if (!accountIndexByCode.contains(acctCode))
            {
                const QString error = QStringLiteral("\u51ed\u8bc1\u300c%1\u300d\u4e2d\u79d1\u76ee\u300c%2\u300d\u4e0d\u5b58\u5728")
                    .arg(voucherDisplayName(voucher, voucherCount), acctCode);
                missingAccountErrors.append(error);
            }
            else
            {
                AccountRecord &accountRecord = (*accounts)[accountIndexByCode.value(acctCode)];
                bool accountChanged = false;

                for (int i = 1; i <= 6; ++i)
                {
                    const QString aaTypeCode = textFromJsonValue(accountRecord.object.value(QStringLiteral("AcctProjTypeCode%1").arg(i)));
                    if (aaTypeCode.isEmpty() || !isBlankValue(voucher.value(QStringLiteral("ProjCode%1").arg(i))))
                        continue;

                    ++missingAaTypeCount;
                    balanceRemovalCandidates->insert(acctCode);
                    appendDetailLog(summary, QStringLiteral("\u51ed\u8bc1\u300c%1\u300d\u4e2d\u79d1\u76ee\u300c%2\u300d\u542f\u7528\u4e86\u3010%3\u3011\uff0c\u51ed\u8bc1\u7f3a\u5c11\u8be5\u8f85\u52a9\u6838\u7b97\u9879\u76ee")
                        .arg(voucherDisplayName(voucher, voucherCount),
                             textFromJsonValue(accountRecord.object.value("AcctName")).isEmpty() ? acctCode : textFromJsonValue(accountRecord.object.value("AcctName")),
                             aaTypeCode));

                    if (appendAllowNullAaType(&accountRecord.object, aaTypeCode))
                    {
                        ++appendedAaTypeCount;
                        accountChanged = true;
                        affectedAccounts.insert(acctCode);
                    }
                }

                if (accountChanged)
                    accountRecord.changed = true;
            }
        }

        ++voucherCount;
    }

    if (pos < voucherRange.end)
        return false;

    if (!missingAccountErrors.isEmpty())
    {
        if (summary)
            summary->missingAccountCount += missingAccountErrors.size();
        if (errorMessage)
            *errorMessage = missingAccountErrors.join("\n");
        return false;
    }

    if (summary)
    {
        summary->voucherCount += voucherCount;
        summary->missingAaTypeCount += missingAaTypeCount;
        summary->appendedAaTypeCount += appendedAaTypeCount;
        summary->affectedAccountCount += affectedAccounts.size();
    }

    return true;
}

bool accountHasProjectBalance(const QByteArray &data,
                              const JsonRange &balanceRange,
                              const QSet<QString> &candidateAccounts,
                              QSet<QString> *accountsWithProjectBalance,
                              QString *errorMessage)
{
    if (!accountsWithProjectBalance)
        return false;

    accountsWithProjectBalance->clear();
    if (candidateAccounts.isEmpty() || !balanceRange.isValid())
        return true;

    int balanceCount = 0;
    int pos = balanceRange.start + 1;
    JsonRange objectRange;
    while (nextJsonObjectRangeInArray(data, balanceRange, &pos, &objectRange, errorMessage))
    {
        QJsonObject balance;
        if (!parseJsonObjectBytes(jsonForRange(data, objectRange), &balance, errorMessage))
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("GLBalanceList \u7b2c %1 \u6761%2").arg(balanceCount + 1).arg(errorMessage->isEmpty() ? QString() : QStringLiteral("：") + *errorMessage);
            return false;
        }

        const QString acctCode = textFromJsonValue(balance.value("AcctCode"));
        if (candidateAccounts.contains(acctCode) && anyProjectCodeHasValue(balance))
            accountsWithProjectBalance->insert(acctCode);

        ++balanceCount;
    }

    if (pos < balanceRange.end)
        return false;

    return true;
}

bool buildBalanceListJsonAfterRemoval(const QByteArray &data,
                                      const JsonRange &balanceRange,
                                      const QSet<QString> &accountsWithProjectBalance,
                                      QByteArray *balanceListJson,
                                      int *removedBalanceCount,
                                      QStringList *detailLogs,
                                      QString *errorMessage)
{
    if (!balanceListJson || !removedBalanceCount)
        return false;

    *removedBalanceCount = 0;
    balanceListJson->clear();
    balanceListJson->append('[');

    QMap<QString, int> removedByAccount;
    bool first = true;
    int balanceCount = 0;
    int pos = balanceRange.start + 1;
    JsonRange objectRange;
    while (nextJsonObjectRangeInArray(data, balanceRange, &pos, &objectRange, errorMessage))
    {
        const QByteArray originalBalanceJson = jsonForRange(data, objectRange);
        QJsonObject balance;
        if (!parseJsonObjectBytes(originalBalanceJson, &balance, errorMessage))
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("GLBalanceList \u7b2c %1 \u6761%2").arg(balanceCount + 1).arg(errorMessage->isEmpty() ? QString() : QStringLiteral("：") + *errorMessage);
            return false;
        }

        const QString acctCode = textFromJsonValue(balance.value("AcctCode"));
        if (accountsWithProjectBalance.contains(acctCode) && allProjectCodesBlank(balance))
        {
            ++(*removedBalanceCount);
            removedByAccount[acctCode] = removedByAccount.value(acctCode) + 1;
        }
        else
        {
            if (!first)
                balanceListJson->append(',');
            balanceListJson->append(originalBalanceJson);
            first = false;
        }

        ++balanceCount;
    }

    if (pos < balanceRange.end)
        return false;

    balanceListJson->append(']');

    if (detailLogs)
    {
        for (auto it = removedByAccount.constBegin(); it != removedByAccount.constEnd(); ++it)
        {
            detailLogs->append(QStringLiteral("\u79d1\u76ee\u300c%1\u300d\u7684\u671f\u521d\u5217\u8868\u5df2\u5220\u9664 %2 \u6761\u6838\u7b97\u9879\u76ee\u5168\u7a7a\u7684\u8bb0\u5f55")
                .arg(it.key())
                .arg(it.value()));
        }
    }

    return true;
}

bool processAccountSetJsonFile(const QString &jsonPath, AllowNullAaProcessResult *summary, QString *errorMessage)
{
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u6253\u5f00 JSON \u5931\u8d25\uff1a") + QDir::toNativeSeparators(jsonPath);
        return false;
    }

    const QByteArray jsonData = file.readAll();
    file.close();

    if (summary && summary->accountSetName.isEmpty())
    {
        QJsonParseError metadataError;
        const QJsonDocument metadataDocument = QJsonDocument::fromJson(jsonData, &metadataError);
        if (metadataError.error == QJsonParseError::NoError && metadataDocument.isObject())
        {
            summary->accountSetName = metadataDocument.object()
                .value(QStringLiteral("AccountSet"))
                .toObject()
                .value(QStringLiteral("AsName"))
                .toString()
                .trimmed();
        }
    }

    JsonRange accountRange;
    JsonRange voucherRange;
    JsonRange balanceRange;
    if (!findTopLevelArrayRange(jsonData, "GLAccountList", &accountRange, errorMessage)
        || !findTopLevelArrayRange(jsonData, "GLVoucherList", &voucherRange, errorMessage))
    {
        return false;
    }

    QString optionalArrayError;
    const bool hasBalanceList = findOptionalTopLevelArrayRange(jsonData, "GLBalanceList", &balanceRange, &optionalArrayError);
    if (!optionalArrayError.isEmpty())
    {
        if (errorMessage)
            *errorMessage = optionalArrayError;
        return false;
    }

    QList<AccountRecord> accounts;
    QMap<QString, int> accountIndexByCode;
    if (!buildAccountsFromRawJson(jsonData, accountRange, &accounts, &accountIndexByCode, errorMessage))
        return false;

    if (accounts.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u672a\u627e\u5230 GLAccountList \u79d1\u76ee\u6570\u636e");
        return false;
    }

    QSet<QString> balanceRemovalCandidates;
    const int logCountBeforeVoucherScan = summary ? summary->logs.size() : 0;
    if (!scanVouchersAndUpdateAccounts(jsonData, voucherRange, &accounts, accountIndexByCode, &balanceRemovalCandidates, summary, errorMessage))
        return false;

    int removedBalanceCount = 0;
    QByteArray balanceListJson;
    if (hasBalanceList && !balanceRemovalCandidates.isEmpty())
    {
        QSet<QString> accountsWithProjectBalance;
        if (!accountHasProjectBalance(jsonData, balanceRange, balanceRemovalCandidates, &accountsWithProjectBalance, errorMessage))
            return false;

        QStringList balanceDetailLogs;
        if (!accountsWithProjectBalance.isEmpty()
            && !buildBalanceListJsonAfterRemoval(jsonData, balanceRange, accountsWithProjectBalance, &balanceListJson, &removedBalanceCount, &balanceDetailLogs, errorMessage))
        {
            return false;
        }

        for (const QString &detailLog : balanceDetailLogs)
            appendDetailLog(summary, detailLog);
    }

    QList<JsonReplacement> replacements;
    JsonReplacement accountReplacement;
    accountReplacement.range = accountRange;
    accountReplacement.data = buildAccountListJson(accounts);
    replacements.append(accountReplacement);

    if (removedBalanceCount > 0)
    {
        JsonReplacement balanceReplacement;
        balanceReplacement.range = balanceRange;
        balanceReplacement.data = balanceListJson;
        replacements.append(balanceReplacement);
    }

    const QByteArray outputJson = applyJsonReplacements(jsonData, replacements);

    QSaveFile saveFile(jsonPath);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u5199\u5165\u4fee\u6539\u540e\u7684 JSON \u5931\u8d25");
        return false;
    }

    if (saveFile.write(outputJson) != outputJson.size())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u5199\u5165\u4fee\u6539\u540e\u7684 JSON \u5931\u8d25\uff1a") + saveFile.errorString();
        return false;
    }

    if (!saveFile.commit())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u4fdd\u5b58\u4fee\u6539\u540e\u7684 JSON \u5931\u8d25\uff1a") + saveFile.errorString();
        return false;
    }

    if (summary)
    {
        ++summary->jsonCount;
        summary->accountCount += accounts.size();
        summary->removedBalanceCount += removedBalanceCount;
        summary->logs.insert(logCountBeforeVoucherScan, QStringLiteral("\u5df2\u5904\u7406 JSON\uff1a%1").arg(QDir::toNativeSeparators(jsonPath)));
    }

    AppLogger::operation(QStringLiteral("\u51ed\u8bc1\u7f3a\u5c11\u8f85\u52a9\u6838\u7b97\u8865\u975e\u5fc5\u5f55 JSON \u5b8c\u6210 path=%1 accounts=%2 vouchers=%3 missing=%4 appended=%5 removedBalances=%6")
        .arg(QDir::toNativeSeparators(jsonPath))
        .arg(accounts.size())
        .arg(summary ? summary->voucherCount : 0)
        .arg(summary ? summary->missingAaTypeCount : 0)
        .arg(summary ? summary->appendedAaTypeCount : 0)
        .arg(removedBalanceCount));

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
            *errorMessage = QStringLiteral("\u6267\u884c PowerShell \u8d85\u65f6");
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        const QString stdErr = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (errorMessage)
            *errorMessage = stdErr.isEmpty() ? QStringLiteral("\u6267\u884c PowerShell \u5931\u8d25") : stdErr;
        return false;
    }

    return true;
}

bool extractZipToDirectory(const QString &zipPath, const QString &destDir, QString *errorMessage)
{
    AppLogger::operation(QStringLiteral("\u89e3\u538b\u5f85\u8865\u975e\u5fc5\u5f55\u8f85\u52a9\u6838\u7b97 ZIP zip=%1 dest=%2")
        .arg(QDir::toNativeSeparators(zipPath), QDir::toNativeSeparators(destDir)));

    const QString script =
        "$ErrorActionPreference='Stop';"
        "Add-Type -AssemblyName System.IO.Compression;"
        "Add-Type -AssemblyName System.IO.Compression.FileSystem;"
        "$zip=$env:LEMON_AA_ZIP_PATH;$dest=$env:LEMON_AA_ZIP_DEST;"
        "if([string]::IsNullOrWhiteSpace($zip)){throw 'ZIP path is empty'}"
        "if([string]::IsNullOrWhiteSpace($dest)){throw 'Destination path is empty'}"
        "if(Test-Path -LiteralPath $dest){Remove-Item -LiteralPath $dest -Recurse -Force}"
        "New-Item -ItemType Directory -Path $dest -Force|Out-Null;"
        "[System.IO.Compression.ZipFile]::ExtractToDirectory($zip,$dest)";

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LEMON_AA_ZIP_PATH", QFileInfo(zipPath).absoluteFilePath());
    env.insert("LEMON_AA_ZIP_DEST", QDir::toNativeSeparators(destDir));
    return runPowerShell(script, env, 60000, errorMessage);
}

bool zipDirectory(const QString &sourceDir, const QString &zipPath, QString *errorMessage)
{
    AppLogger::operation(QStringLiteral("\u91cd\u65b0\u538b\u7f29\u8f85\u52a9\u6838\u7b97\u4fee\u590d\u76ee\u5f55 source=%1 zip=%2")
        .arg(QDir::toNativeSeparators(sourceDir), QDir::toNativeSeparators(zipPath)));

    const QString script =
        "$ErrorActionPreference='Stop';"
        "Add-Type -AssemblyName System.IO.Compression;"
        "Add-Type -AssemblyName System.IO.Compression.FileSystem;"
        "$src=$env:LEMON_AA_ZIP_SRC;$zip=$env:LEMON_AA_ZIP_OUT;"
        "if(Test-Path -LiteralPath $zip){Remove-Item -LiteralPath $zip -Force}"
        "[System.IO.Compression.ZipFile]::CreateFromDirectory($src,$zip,"
        "[System.IO.Compression.CompressionLevel]::Optimal,$false)";

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LEMON_AA_ZIP_SRC", QDir::toNativeSeparators(sourceDir));
    env.insert("LEMON_AA_ZIP_OUT", QDir::toNativeSeparators(zipPath));
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

QStringList candidateJsonFiles(const QString &extractDir)
{
    QStringList accountSetFiles;
    QStringList otherFiles;

    QDirIterator it(extractDir, QStringList() << "*.json", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString path = it.next();
        if (QFileInfo(path).fileName().compare("AccountSet.json", Qt::CaseInsensitive) == 0)
            accountSetFiles.append(path);
        else
            otherFiles.append(path);
    }

    return accountSetFiles.isEmpty() ? otherFiles : accountSetFiles;
}

AllowNullAaProcessResult processAllowNullAaZip(const QString &inputPath, const QString &workRoot, const QString &outputRoot)
{
    AllowNullAaProcessResult result;
    result.inputPath = inputPath;

    AppLogger::operation(QStringLiteral("\u5f00\u59cb\u51ed\u8bc1\u7f3a\u5c11\u8f85\u52a9\u6838\u7b97\u8865\u975e\u5fc5\u5f55 input=%1 workRoot=%2 outputRoot=%3")
        .arg(QDir::toNativeSeparators(inputPath),
             QDir::toNativeSeparators(workRoot),
             QDir::toNativeSeparators(outputRoot)));

    const QFileInfo inputInfo(inputPath);
    if (!inputInfo.exists() || !inputInfo.isFile())
    {
        result.errorMessage = QStringLiteral("\u538b\u7f29\u5305\u4e0d\u5b58\u5728\uff1a") + QDir::toNativeSeparators(inputPath);
        return result;
    }

    if (inputInfo.suffix().compare("zip", Qt::CaseInsensitive) != 0)
    {
        result.errorMessage = QStringLiteral("\u8bf7\u9009\u62e9 ZIP \u538b\u7f29\u5305");
        return result;
    }

    QDir workDir(workRoot);
    if (!workDir.exists() && !workDir.mkpath("."))
    {
        result.errorMessage = QStringLiteral("\u521b\u5efa\u5de5\u4f5c\u76ee\u5f55\u5931\u8d25\uff1a") + QDir::toNativeSeparators(workRoot);
        return result;
    }

    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz");
    const QString jobDirPath = workDir.absoluteFilePath(inputInfo.completeBaseName() + "_" + timestamp);
    const QString extractDirPath = QDir(jobDirPath).absoluteFilePath("extracted");
    const QString outputDirPath = QDir::cleanPath(outputRoot);

    if (!QDir().mkpath(jobDirPath) || !QDir().mkpath(outputDirPath))
    {
        result.errorMessage = QStringLiteral("\u521b\u5efa\u5904\u7406\u76ee\u5f55\u5931\u8d25");
        return result;
    }

    QString errorMessage;
    if (!extractZipToDirectory(inputInfo.absoluteFilePath(), extractDirPath, &errorMessage))
    {
        result.errorMessage = errorMessage;
        return result;
    }

    const QStringList jsonFiles = candidateJsonFiles(extractDirPath);
    if (jsonFiles.isEmpty())
    {
        result.errorMessage = QStringLiteral("ZIP \u4e2d\u672a\u627e\u5230 JSON \u6587\u4ef6");
        return result;
    }

    bool hasProcessedJson = false;
    QStringList skippedJsonErrors;
    for (const QString &jsonPath : jsonFiles)
    {
        QString jsonError;
        AllowNullAaProcessResult before = result;
        if (processAccountSetJsonFile(jsonPath, &result, &jsonError))
        {
            hasProcessedJson = true;
        }
        else
        {
            result = before;
            skippedJsonErrors.append(QFileInfo(jsonPath).fileName() + ": " + jsonError);
        }
    }

    if (!hasProcessedJson)
    {
        result.errorMessage = QStringLiteral("\u672a\u627e\u5230\u53ef\u5904\u7406\u7684\u8d26\u5957 JSON\uff1a\n") + skippedJsonErrors.join("\n");
        return result;
    }

    const QString outputBaseName = RepairFileNameUtils::makeBaseName(
        result.accountSetName,
        inputInfo.completeBaseName(),
        QStringLiteral("辅助核算补非必录修复"));
    result.outputZipPath = uniqueFilePath(outputDirPath, outputBaseName, ".zip");

    if (!zipDirectory(extractDirPath, result.outputZipPath, &errorMessage))
    {
        result.errorMessage = errorMessage;
        return result;
    }

    result.ok = true;
    result.logs.prepend(QStringLiteral("\u8f93\u5165\u538b\u7f29\u5305\uff1a") + QDir::toNativeSeparators(inputPath));
    result.logs.append(QStringLiteral("\u8f93\u51fa\u538b\u7f29\u5305\uff1a") + QDir::toNativeSeparators(result.outputZipPath));

    AppLogger::operation(QStringLiteral("\u5b8c\u6210\u51ed\u8bc1\u7f3a\u5c11\u8f85\u52a9\u6838\u7b97\u8865\u975e\u5fc5\u5f55 output=%1 jsonCount=%2 accounts=%3 vouchers=%4 missing=%5 appended=%6 removedBalances=%7")
        .arg(QDir::toNativeSeparators(result.outputZipPath))
        .arg(result.jsonCount)
        .arg(result.accountCount)
        .arg(result.voucherCount)
        .arg(result.missingAaTypeCount)
        .arg(result.appendedAaTypeCount)
        .arg(result.removedBalanceCount));

    return result;
}
}

AllowNullAaTypeWidget::AllowNullAaTypeWidget(GlobalData *globalData, QWidget *parent)
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

void AllowNullAaTypeWidget::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    QHBoxLayout *titleLayout = new QHBoxLayout();
    QLabel *titleLabel = new QLabel(QStringLiteral("\u51ed\u8bc1\u8f85\u52a9\u6838\u7b97\u8865\u975e\u5fc5\u5f55"), this);
    titleLabel->setObjectName("labelPageTitle");
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();

    m_pBtnBrowse = new QPushButton(QStringLiteral("\u9009\u62e9\u538b\u7f29\u5305"), this);
    m_pBtnBrowse->setObjectName("btnBrowseAllowNullAaPackage");
    m_pBtnBrowse->setProperty("flatGreen", true);
    m_pBtnBrowse->setMaximumWidth(110);
    titleLayout->addWidget(m_pBtnBrowse);
    layout->addLayout(titleLayout);

    m_pPathConfig = new RepairPathConfigWidget(this);
    m_pPathConfig->setPathsChangedCallback([this]() { refreshInputFiles(); });
    layout->addWidget(m_pPathConfig);

    m_pInputFilePicker = new RepairInputFilePickerWidget(
        QStringList() << QStringLiteral("*.zip"), this);
    m_pInputFilePicker->setProcessCallback(
        [this](const QStringList &paths) { processSelectedFiles(paths); });
    m_pInputFilePicker->setFilesChangedCallback([this](int count) {
        if (!m_batchProcessing && m_pStatusLabel)
            m_pStatusLabel->setText(QStringLiteral("\u5df2\u626b\u63cf\u8f93\u5165\u76ee\u5f55\uff0c\u53d1\u73b0 %1 \u4e2a ZIP").arg(count));
    });
    layout->addWidget(m_pInputFilePicker);

    m_pDropLabel = new QLabel(QStringLiteral("\u5c06 ZIP \u538b\u7f29\u5305\u62d6\u5165\u6b64\u5904\uff0c\u7cfb\u7edf\u4f1a\u4e3a\u51ed\u8bc1\u7f3a\u5c11\u7684\u79d1\u76ee\u8f85\u52a9\u6838\u7b97\u7c7b\u578b\u8865\u5145\u975e\u5fc5\u5f55\u5e76\u751f\u6210\u65b0 ZIP"), this);
    m_pDropLabel->setObjectName("labelRepairDropArea");
    m_pDropLabel->setAlignment(Qt::AlignCenter);
    m_pDropLabel->setMinimumHeight(84);
    m_pDropLabel->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(m_pDropLabel);

    m_pStatusLabel = new QLabel(QStringLiteral("\u5f85\u5904\u7406"), this);
    m_pStatusLabel->setWordWrap(true);
    layout->addWidget(m_pStatusLabel);

    m_pLogEdit = new QTextEdit(this);
    m_pLogEdit->setObjectName("allowNullAaLog");
    m_pLogEdit->setReadOnly(true);
    m_pLogEdit->setMinimumHeight(220);
    layout->addWidget(m_pLogEdit, 1);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    m_pBtnBack = new QPushButton(QStringLiteral("\u8fd4\u56de"), this);
    m_pBtnBack->setObjectName("btnBackToMain");
    m_pBtnBack->setProperty("grayButton", true);
    m_pBtnBack->setMaximumWidth(100);
    m_pBtnBack->setMinimumHeight(36);
    btnLayout->addWidget(m_pBtnBack);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    connect(m_pBtnBrowse, &QPushButton::clicked, this, &AllowNullAaTypeWidget::onBrowseClicked);
    connect(m_pBtnBack, &QPushButton::clicked, this, &AllowNullAaTypeWidget::onBackClicked);
}

QString AllowNullAaTypeWidget::workRootDir() const
{
    return QDir::cleanPath(qApp->applicationDirPath() + "/AllowNullAaTypePackages");
}

QString AllowNullAaTypeWidget::defaultOutputDir() const
{
    return RepairPathSettings::outputDir();
}

QString AllowNullAaTypeWidget::outputDir() const
{
    return RepairPathSettings::outputDir();
}

void AllowNullAaTypeWidget::updateOutputDirLabel()
{
    if (m_pPathConfig)
        m_pPathConfig->refreshPaths();
}

void AllowNullAaTypeWidget::setRunning(bool running, const QString &message)
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

void AllowNullAaTypeWidget::refreshInputFiles()
{
    if (m_pPathConfig)
        m_pPathConfig->refreshPaths();
    if (m_pInputFilePicker)
        m_pInputFilePicker->refreshFiles();
}

void AllowNullAaTypeWidget::processSelectedFiles(const QStringList &zipPaths)
{
    if (zipPaths.isEmpty())
        return;
    m_batchProcessing = true;
    m_batchSuccessCount = 0;
    m_batchFailureCount = 0;
    m_batchResults.clear();
    m_pendingBatchPaths = zipPaths;
    m_pLogEdit->clear();
    setRunning(true, QStringLiteral("\u6b63\u5728\u6309\u987a\u5e8f\u5904\u7406 %1 \u4e2a ZIP...").arg(zipPaths.size()));
    processNextBatchFile();
}

void AllowNullAaTypeWidget::processNextBatchFile()
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
    QMessageBox::information(this, QStringLiteral("\u8f85\u52a9\u6838\u7b97\u6279\u91cf\u5904\u7406"),
        QStringLiteral("\u6210\u529f\uff1a%1\n\u5931\u8d25\uff1a%2\n\n%3")
            .arg(m_batchSuccessCount)
            .arg(m_batchFailureCount)
            .arg(m_batchResults.join(QLatin1Char('\n'))));
}

void AllowNullAaTypeWidget::startProcessZip(const QString &zipPath)
{
    if (zipPath.isEmpty())
        return;

    if (!isZipFile(zipPath))
    {
        AppLogger::warning(QStringLiteral("\u51ed\u8bc1\u8f85\u52a9\u6838\u7b97\u8865\u975e\u5fc5\u5f55\u9009\u62e9\u4e86\u4e0d\u652f\u6301\u7684\u6587\u4ef6 path=%1").arg(QDir::toNativeSeparators(zipPath)));
        QMessageBox::warning(this, QStringLiteral("\u63d0\u793a"), QStringLiteral("\u8bf7\u9009\u62e9 ZIP \u538b\u7f29\u5305"));
        return;
    }

    const int requestId = ++m_requestId;
    const QString inputPath = zipPath;
    const QString rootPath = workRootDir();
    const QString outputPath = outputDir();

    setRunning(true, QStringLiteral("\u6b63\u5728\u540e\u53f0\u5904\u7406\u51ed\u8bc1\u8f85\u52a9\u6838\u7b97..."));
    if (m_batchProcessing)
        m_pLogEdit->append(QStringLiteral("\u6b63\u5728\u5904\u7406\uff1a") + QDir::toNativeSeparators(inputPath));
    else
        m_pLogEdit->setPlainText(QStringLiteral("\u6b63\u5728\u5904\u7406\uff1a") + QDir::toNativeSeparators(inputPath));
    AppLogger::operation(QStringLiteral("\u51ed\u8bc1\u8f85\u52a9\u6838\u7b97\u8865\u975e\u5fc5\u5f55\u540e\u53f0\u4efb\u52a1\u542f\u52a8 requestId=%1 input=%2")
        .arg(requestId)
        .arg(QDir::toNativeSeparators(inputPath)));

    QSharedPointer<AllowNullAaProcessResult> result(new AllowNullAaProcessResult);
    QThread *thread = QThread::create([result, inputPath, rootPath, outputPath]() {
        *result = processAllowNullAaZip(inputPath, rootPath, outputPath);
    });

    connect(thread, &QThread::finished, this, [this, result, requestId]() {
        if (requestId != m_requestId)
            return;

        if (!m_batchProcessing)
            setRunning(false);
        if (!result->ok)
        {
            AppLogger::error(QStringLiteral("\u51ed\u8bc1\u8f85\u52a9\u6838\u7b97\u8865\u975e\u5fc5\u5f55\u5904\u7406\u5931\u8d25 requestId=%1 error=%2")
                .arg(requestId)
                .arg(result->errorMessage));
            if (m_batchProcessing)
            {
                ++m_batchFailureCount;
                m_batchResults.append(QFileInfo(result->inputPath).fileName()
                    + QStringLiteral("\uff1a\u5931\u8d25 - ") + result->errorMessage);
                m_pLogEdit->append(result->logs.join("\n") + "\n" + result->errorMessage);
                processNextBatchFile();
                return;
            }
            m_pStatusLabel->setText(QStringLiteral("\u5904\u7406\u5931\u8d25\uff1a") + result->errorMessage);
            m_pLogEdit->setPlainText(result->logs.join("\n") + "\n" + result->errorMessage);
            QMessageBox::warning(this, QStringLiteral("\u5904\u7406\u5931\u8d25"), result->errorMessage);
            return;
        }

        m_pStatusLabel->setText(QStringLiteral("\u5904\u7406\u5b8c\u6210\uff1aJSON %1 \u4e2a\uff0c\u51ed\u8bc1 %2 \u884c\uff0c\u53d1\u73b0\u7f3a\u5c11\u8f85\u52a9\u6838\u7b97 %3 \u5904\uff0c\u65b0\u589e\u975e\u5fc5\u5f55 %4 \u5904\uff0c\u5220\u9664\u671f\u521d %5 \u6761")
            .arg(result->jsonCount)
            .arg(result->voucherCount)
            .arg(result->missingAaTypeCount)
            .arg(result->appendedAaTypeCount)
            .arg(result->removedBalanceCount));
        if (m_batchProcessing)
            m_pLogEdit->append(result->logs.join("\n"));
        else
            m_pLogEdit->setPlainText(result->logs.join("\n"));
        AppLogger::operation(QStringLiteral("\u51ed\u8bc1\u8f85\u52a9\u6838\u7b97\u8865\u975e\u5fc5\u5f55\u5904\u7406\u5b8c\u6210 requestId=%1 output=%2")
            .arg(requestId)
            .arg(QDir::toNativeSeparators(result->outputZipPath)));
        if (m_batchProcessing)
        {
            ++m_batchSuccessCount;
            m_batchResults.append(QFileInfo(result->inputPath).fileName()
                + QStringLiteral("\uff1a\u6210\u529f -> ")
                + QDir::toNativeSeparators(result->outputZipPath));
            processNextBatchFile();
            return;
        }
        QMessageBox::information(this, QStringLiteral("\u51ed\u8bc1\u8f85\u52a9\u6838\u7b97\u8865\u975e\u5fc5\u5f55"),
            QStringLiteral("\u5904\u7406\u5b8c\u6210\uff0c\u5df2\u751f\u6210\u538b\u7f29\u5305\uff1a\n%1")
                .arg(QDir::toNativeSeparators(result->outputZipPath)));
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void AllowNullAaTypeWidget::onBrowseClicked()
{
    AppLogger::operation(QStringLiteral("\u51ed\u8bc1\u8f85\u52a9\u6838\u7b97\u8865\u975e\u5fc5\u5f55\u9009\u62e9\u538b\u7f29\u5305"));
    const QString filePath = QFileDialog::getOpenFileName(this,
        QStringLiteral("\u9009\u62e9\u8d26\u5957 ZIP \u538b\u7f29\u5305"),
        RepairPathSettings::inputDir(),
        QStringLiteral("ZIP \u538b\u7f29\u5305 (*.zip)"));

    if (!filePath.isEmpty())
        startProcessZip(filePath);
}

void AllowNullAaTypeWidget::onBrowseOutputDirClicked()
{
    AppLogger::operation(QStringLiteral("\u9009\u62e9\u51ed\u8bc1\u8f85\u52a9\u6838\u7b97\u8865\u975e\u5fc5\u5f55\u8f93\u51fa\u76ee\u5f55"));
    const QString dirPath = QFileDialog::getExistingDirectory(this,
        QStringLiteral("\u9009\u62e9\u8f93\u51fa\u76ee\u5f55"),
        outputDir(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (dirPath.isEmpty())
        return;

    RepairPathSettings::setOutputDir(dirPath);
    updateOutputDirLabel();
}

void AllowNullAaTypeWidget::onBackClicked()
{
    AppLogger::operation(QStringLiteral("\u51ed\u8bc1\u8f85\u52a9\u6838\u7b97\u8865\u975e\u5fc5\u5f55\u8fd4\u56de\u4e3b\u754c\u9762"));
    emit backToMain();
}

void AllowNullAaTypeWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (hasZipFile(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void AllowNullAaTypeWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (hasZipFile(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void AllowNullAaTypeWidget::dropEvent(QDropEvent *event)
{
    if (!event->possibleActions().testFlag(Qt::CopyAction))
    {
        event->ignore();
        return;
    }
    if (!hasZipFile(event->mimeData()))
    {
        event->ignore();
        return;
    }

    for (const QUrl &url : event->mimeData()->urls())
    {
        if (url.isLocalFile() && isZipFile(url.toLocalFile()))
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
            AppLogger::operation(QStringLiteral("\u51ed\u8bc1\u8f85\u52a9\u6838\u7b97\u8865\u975e\u5fc5\u5f55\u62d6\u5165 ZIP path=%1").arg(QDir::toNativeSeparators(url.toLocalFile())));
            startProcessZip(inputPath);
            return;
        }
    }

    event->ignore();
}
