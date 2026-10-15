#include "vouchergroupnumrepair.h"
#include <QDate>
#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>
#include <QVector>
#include <limits>

namespace VoucherGroupNumRepair
{
namespace
{
QString monthOf(const QJsonObject &row)
{
    const QString date = row.value("MakeDate").toString().trimmed();
    static const QRegularExpression pattern(QStringLiteral("^(\\d{4})[-/](\\d{1,2})[-/]\\d{1,2}(?:$|[T ])"));
    const auto match = pattern.match(date);
    int year = 0, month = 0;
    if (match.hasMatch()) {
        year = match.captured(1).toInt();
        month = match.captured(2).toInt();
    } else {
        year = row.value("IntYear").toVariant().toInt();
        month = row.value("IntPeriod").toVariant().toInt();
    }
    return QDate(year, month, 1).isValid()
        ? QStringLiteral("%1-%2").arg(year, 4, 10, QLatin1Char('0')).arg(month, 2, 10, QLatin1Char('0'))
        : QString();
}

// Parse decimal text exactly: a double could round 1.0000000000000001 to 1.
bool parseNumber(const QString &text, bool &integer, int &number)
{
    static const QRegularExpression decimal(QStringLiteral("^[+-]?[0-9]+(?:\\.[0-9]+)?$"));
    if (!decimal.match(text).hasMatch())
        return false;
    const int dot = text.indexOf('.');
    integer = dot < 0 || !text.mid(dot + 1).contains(QRegularExpression(QStringLiteral("[1-9]")));
    if (!integer)
        return true;
    bool ok = false;
    number = (dot < 0 ? text : text.left(dot)).toInt(&ok);
    return ok;
}
}

Result repair(QJsonObject &root)
{
    Result result;
    const auto fail = [](const QString &message) { Result r; r.error = message; return r; };
    if (!root.value("GLVoucherList").isArray())
        return fail(QStringLiteral("未找到 GLVoucherList 凭证列表"));
    QJsonArray rows = root.value("GLVoucherList").toArray();
    QMap<QString, int> maximum;
    // Scan the complete month first, including integral decimals appearing later in the file.
    for (int i = 0; i < rows.size(); ++i) {
        if (!rows.at(i).isObject())
            return fail(QStringLiteral("凭证第 %1 行不是对象").arg(i + 1));
        const auto row = rows.at(i).toObject();
        const QString month = monthOf(row);
        const auto value = row.value("GroupNum");
        const QString original = value.toVariant().toString().trimmed();
        bool integer = false;
        int number = 0;
        if (month.isEmpty())
            return fail(QStringLiteral("凭证第 %1 行无法确定年月，请检查 MakeDate 或 IntYear/IntPeriod").arg(i + 1));
        if ((!value.isString() && !value.isDouble()) || !parseNumber(original, integer, number))
            return fail(QStringLiteral("凭证第 %1 行 GroupNum [%2] 不是有效的十进制编号或超出 Int32 范围").arg(i + 1).arg(original));
        if (integer && (!maximum.contains(month) || number > maximum.value(month)))
            maximum[month] = number;
    }
    QMap<QStringList, int> assigned;
    QMap<QStringList, int> detailIndexes;
    QVector<int> detailRows;
    for (int i = 0; i < rows.size(); ++i) {
        auto row = rows.at(i).toObject();
        const auto value = row.value("GroupNum");
        const QString original = value.toVariant().toString().trimmed();
        const QString month = monthOf(row);
        const QString group = row.value("VchGroup").toVariant().toString();
        const QStringList key{month, group, original};
        bool integer = false;
        int number = 0;
        parseNumber(original, integer, number);
        // Keep values already accepted by an Int32 parser, including their representation.
        bool alreadyInteger = false;
        original.toInt(&alreadyInteger);
        if (alreadyInteger)
            continue;
        if (!integer) {
            if (!assigned.contains(key)) {
                const int previous = maximum.value(month, 0);
                if (previous == (std::numeric_limits<int>::max)())
                    return fail(QStringLiteral("%1 的最大凭证编号已达到 Int32 上限，无法继续分配").arg(month));
                assigned[key] = previous + 1;
                maximum[month] = previous + 1;
            }
            number = assigned.value(key);
        }
        if (!detailIndexes.contains(key)) {
            detailIndexes[key] = result.details.size();
            detailRows.append(0);
            result.details.append(QStringLiteral("%1 %2字：GroupNum [%3] → [%4]（%5）")
                .arg(month, group, original, QString::number(number), integer
                    ? QStringLiteral("整数小数转整数") : QStringLiteral("按当月最大整数编号 + 1 分配")));
            if (integer) ++result.normalizedVouchers;
            else ++result.renumberedVouchers;
        }
        ++detailRows[detailIndexes.value(key)];
        row["GroupNum"] = QString::number(number);
        rows[i] = row;
        ++result.changedRows;
    }
    for (int i = 0; i < result.details.size(); ++i)
        result.details[i] += QStringLiteral("，%1 条分录").arg(detailRows.at(i));
    root["GLVoucherList"] = rows;
    return result;
}
}
