#include "XinZhongDaBackupRestorer.h"
#include "repairpathsettings.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSet>
#include <algorithm>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextCodec>
#include <QUuid>
#include <QVariant>
#include <stdexcept>

static QString xzdText(const char* text)
{
    return QString::fromUtf8(text);
}
static QString xzdQuote(const QString& text)
{
    return T3BackupRestorer::quoteLiteral(text);
}
static QString xzdId(const QString& text)
{
    return T3BackupRestorer::quoteIdentifier(text);
}
static void xzdRequire(bool valid, const QString& error)
{
    if (!valid)
        throw std::runtime_error(error.toUtf8().constData());
}
static void xzdCheckCancel(const XinZhongDaBackupRestorer::Cancel& cancel)
{
    xzdRequire(!cancel || !cancel(), xzdText(u8"已取消操作。"));
}
static void xzdProgress(const XinZhongDaBackupRestorer::Progress& progress, const QString& text)
{
    if (progress)
        progress(text);
}

static QString xzdDecodeText(const QByteArray& bytes)
{
    const bool unicode = bytes.startsWith(QByteArray::fromHex("fffe"));
    xzdRequire(!unicode || bytes.size() % 2 == 0, xzdText(u8"UTF-16 数据长度不完整。"));
    auto* codec = QTextCodec::codecForName(unicode ? "UTF-16LE" : "GB18030");
    QTextCodec::ConverterState state;
    QString text = codec->toUnicode(bytes.constData(), bytes.size(), &state);
    if (!text.isEmpty() && text.at(0) == QChar(0xfeff))
        text.remove(0, 1);
    xzdRequire(!state.invalidChars && !text.contains(QChar(0)), xzdText(u8"备份文本编码无效。"));
    return text;
}

QString XinZhongDaBackupRestorer::resolveDirectory(const QString& path, QString* error)
{
    error->clear();
    QFileInfo info(path.trimmed());
    if (info.isFile() && info.fileName().compare("ZWDATA.VAL", Qt::CaseInsensitive) == 0)
        info = QFileInfo(info.absolutePath());
    if (!info.isDir() || info.isSymLink()) {
        *error = xzdText(u8"请选择包含 ZWDATA.VAL 和 .tx_ 文件的新中大备份目录，或选择 ZWDATA.VAL。压缩包请先解压。");
        return {};
    }
    return info.canonicalFilePath();
}

bool XinZhongDaBackupRestorer::decode(const QByteArray& input, QByteArray* output, QString* error, const Cancel& cancel)
{
    output->clear();
    error->clear();
    try {
        xzdRequire(!input.isEmpty() && input.size() <= 32 * 1024 * 1024, xzdText(u8"压缩表为空或超过 32 MiB。"));
        QVector<QByteArray> dictionary(65536);
        for (int i = 0; i < 256; ++i)
            dictionary[i] = QByteArray(1, static_cast<char>(i));
        int width = 9, next = 259;
        qint64 bit = 0;
        QByteArray previous;
        while (bit + width <= qint64(input.size()) * 8) {
            if ((bit & 4095) < 16)
                xzdCheckCancel(cancel);
            int code = 0;
            for (int i = 0; i < width; ++i, ++bit)
                code = (code << 1) | ((static_cast<unsigned char>(input.at(int(bit / 8))) >> (7 - bit % 8)) & 1);
            if (code == 256) {
                if (input != QByteArray::fromHex("804000")) {
                    xzdRequire(qint64(input.size()) * 8 - bit < 8, xzdText(u8"压缩表结束标记后存在额外数据。"));
                    while (bit < qint64(input.size()) * 8) {
                        xzdRequire(!(static_cast<unsigned char>(input.at(int(bit / 8))) & (1 << (7 - bit % 8))),
                            xzdText(u8"压缩表末尾填充无效。"));
                        ++bit;
                    }
                }
                return true;
            }
            if (code == 257) {
                xzdRequire(width < 16, xzdText(u8"压缩表字典位宽无效。"));
                ++width;
                continue;
            }
            if (code == 258) {
                width = 9;
                next = 259;
                previous.clear();
                continue;
            }
            QByteArray current;
            if (code < 256 || (code >= 259 && code < next))
                current = dictionary.at(code);
            else if (code == next && !previous.isEmpty())
                current = previous + previous.left(1);
            else
                xzdRequire(false, xzdText(u8"压缩表字典引用无效或文件已损坏。"));
            xzdRequire(output->size() <= 128 * 1024 * 1024 - current.size(), xzdText(u8"解压表超过 128 MiB。"));
            output->append(current);
            if (!previous.isEmpty()) {
                xzdRequire(next < dictionary.size(), xzdText(u8"压缩表字典溢出。"));
                dictionary[next++] = previous + current.left(1);
            }
            previous = current;
        }
        xzdRequire(false, xzdText(u8"压缩表缺少结束标记，文件可能已截断。"));
    } catch (const std::exception& e) {
        *error = QString::fromUtf8(e.what());
    }
    output->clear();
    return false;
}

bool XinZhongDaBackupRestorer::readMetadata(
    const QByteArray& bytes, const QString& directoryName, XinZhongDaBackupAnalysis* result)
{
    result->error.clear();
    try {
        xzdRequire(bytes.size() <= 65536, xzdText(u8"ZWDATA.VAL 超过大小限制。"));
        QString text;
        if (bytes.startsWith(QByteArray::fromHex("fffe")))
            text = xzdDecodeText(bytes);
        else {
            QTextCodec::ConverterState state;
            text = QTextCodec::codecForName("UTF-8")->toUnicode(bytes.constData(), bytes.size(), &state);
            if (state.invalidChars)
                text = xzdDecodeText(bytes);
            if (text.startsWith(QChar(0xfeff)))
                text.remove(0, 1);
        }
        const auto match
            = QRegularExpression("(?:^|[\\r\\n])ZW([A-Za-z0-9]{1,12})[ \\t]+([0-9]{4})(?:[\\r\\n]|$)").match(text);
        xzdRequire(match.hasMatch(), xzdText(u8"ZWDATA.VAL 中未找到有效的账套号和年度。"));
        result->accountCode = match.captured(1);
        result->year = match.captured(2).toInt();
        xzdRequire(result->year >= 1900 && result->year <= 2099, xzdText(u8"备份年度超出支持范围。"));
        xzdRequire(QRegularExpression("(?:^|[\\r\\n])Ver[ \\t]+3\\.1(?:[\\r\\n]|$)").match(text).hasMatch(),
            xzdText(u8"暂不支持此新中大备份格式版本，需要 Ver 3.1。"));
        const auto name = QRegularExpression("(?:^|[\\r\\n])Message=([^\\r\\n]*)").match(text);
        result->accountName = name.hasMatch() ? name.captured(1).trimmed() : QString();
        result->nameFromBackup = !result->accountName.isEmpty();
        if (!result->nameFromBackup)
            result->accountName = directoryName;
        xzdRequire(!result->accountName.isEmpty() && result->accountName.size() <= 500
                && !result->accountName.contains(QChar(0)),
            xzdText(u8"账套名称为空或过长。"));
        return true;
    } catch (const std::exception& e) {
        result->error = QString::fromUtf8(e.what());
        return false;
    }
}

struct XzdColumn {
    int position;
    QString name;
    QString type;
};
static QVector<XzdColumn> xzdColumns(const QString& table)
{
    // Source dump order is not SQL Server's physical column order. Positions
    // below are checked against both GBK and UTF-16 Ver 3.1 fixtures.
    QString spec;
    if (table == "km")
        spec = "1:kmdm:s,2:kmmc:t,3:kmlb:s,4:kmxz:s,5:yefx:s,8:bm:i";
    else if (table == "dwtx")
        spec = "1:dqh:s,2:dqmc:t,3:dwdm:s,4:dwmc:t";
    else if (table == "z_items")
        spec = "1:dm:s,2:mc:t";
    else if (table == "z_projkm")
        spec = "1:id:i,2:dm:s,3:kmdm:s";
    else if (table == "z_proj")
        spec = "1:id:s,2:dm:s,7:kmdm:s,11:dqh:s,12:dwdm:s";
    else if (table == "z_dwwl")
        spec = "1:id:i,2:mon:i,8:kmdm:s,9:j:n,10:d:n,15:dqh:s,16:dwdm:s";
    else if (table == "z_gr" || table == "z_bmry")
        spec = "1:dm:s,2:xm:t";
    else if (table == "z_grwl")
        spec = "1:id:i,2:mon:i,8:xm:s,10:kmdm:s,11:j:n,12:d:n";
    else if (table == "z_pzxm")
        spec = "1:id:i,2:mon:i,3:pzlx:s,4:pzh:s,5:id2:i,6:proj:s,7:je:n,8:kmdm:s,9:j:n,10:d:n";
    else if (QRegularExpression("^z_pz(0[1-9]|1[0-2])$").match(table).hasMatch())
        spec = "1:id:i,2:jzbz:i,3:pzrq:d,4:pzlx:s,5:pzh:s,6:fj:i,7:zy:t,8:kmdm:s,9:j:n,10:d:n,12:shy:t,13:zdy:t,14:czy:"
               "t,15:shbz:i,16:wbje:n,17:hl:n,18:dqh:s,20:sl:n,21:dj:n,22:dwdm:s,23:lxr:s,24:jsr:s,25:bmry:s,26:jzy:t,"
               "27:id1:i,28:id2:i,29:proj:s";
    else if (table == "hz") {
        spec = "1:kmdm:s,2:kmmc:t,3:jhd:s,4:ncye:n,5:jlje:n,6:dlje:n,31:yefx:s,32:sf:i";
        for (int m = 1; m <= 12; ++m)
            spec += QString(",%1:jfe%2:n,%3:dfe%2:n").arg(5 + 2 * m).arg(m, 2, 10, QChar('0')).arg(6 + 2 * m);
    } else if (table == "slhz") {
        spec = "1:kmdm:s,2:kmmc:t,3:ncjcsl:n,4:sljsl:n,5:fljsl:n,30:dw:t,31:gg:t,32:sf:i";
    }
    QVector<XzdColumn> cols;
    for (const auto& entry : spec.split(',', QString::SkipEmptyParts)) {
        const auto fields = entry.split(':');
        cols.append({ fields[0].toInt(), fields[1], fields[2] });
    }
    return cols;
}
static QStringList xzdCollectorTables()
{
    QStringList names = { "km", "hz", "slhz", "dwtx", "z_items", "z_projkm", "z_proj", "z_dwwl", "z_gr", "z_grwl",
        "z_bmry", "z_pzxm" };
    for (int m = 1; m <= 12; ++m)
        names.append(QString("z_pz%1").arg(m, 2, 10, QChar('0')));
    return names;
}
static QList<int> xzdWidths(const QString& table)
{
    if (table == "km")
        return { 9, 31 };
    if (table == "hz" || table == "slhz")
        return { 32, 35 };
    if (table == "dwtx")
        return { 20, 24 };
    if (table == "z_items")
        return { 73, 76 };
    if (table == "z_projkm")
        return { 3, 6 };
    if (table == "z_proj")
        return { 22, 36 };
    if (table == "z_dwwl")
        return { 27, 37 };
    if (table == "z_gr")
        return { 3, 6 };
    if (table == "z_grwl")
        return { 20, 23 };
    if (table == "z_bmry")
        return { 2, 5 };
    if (table == "z_pzxm")
        return { 20, 31 };
    if (table.startsWith("z_pz"))
        return { 39, 62 };
    return {};
}
static QString xzdValue(const QString& value, const XzdColumn& col)
{
    if (value.isEmpty())
        return "NULL";
    if (col.type == "s" || col.type == "t") {
        xzdRequire(value.size() <= (col.type == "s" ? 100 : 2000), xzdText(u8"字段内容过长：") + col.name);
        return xzdQuote(value);
    }
    if (col.type == "i") {
        bool ok = false;
        const qlonglong n = value.toLongLong(&ok);
        xzdRequire(ok && n >= -2147483648LL && n <= 2147483647LL, xzdText(u8"整数字段无效：") + col.name);
        return QString::number(n);
    }
    if (col.type == "d") {
        QString normalized = value;
        normalized.replace('.', '-');
        const auto dt = QDateTime::fromString(normalized, "yyyy-MM-dd HH:mm:ss");
        xzdRequire(dt.isValid(), xzdText(u8"日期字段无效：") + col.name);
        return xzdQuote(dt.toString("yyyy-MM-ddTHH:mm:ss"));
    }
    static const QRegularExpression number("^[+-]?[0-9]{1,20}(?:\\.[0-9]{1,8})?$");
    xzdRequire(number.match(value).hasMatch(), xzdText(u8"金额或数量字段格式/精度不受支持：") + col.name);
    return value;
}

XinZhongDaBackupAnalysis XinZhongDaBackupRestorer::analyze(
    const QString& path, const Cancel& cancel, const Progress& progress)
{
    XinZhongDaBackupAnalysis result;
    try {
        result.directory = resolveDirectory(path, &result.error);
        xzdRequire(!result.directory.isEmpty(), result.error);
        const auto files = QDir(result.directory).entryInfoList(QDir::Files, QDir::Name);
        QMap<QString, QByteArray> source;
        qint64 compressedSize = 0, decodedSize = 0;
        for (const auto& file : files) {
            const QString name = file.fileName().toLower();
            if (name != "zwdata.val" && !name.endsWith(".tx_"))
                continue;
            xzdCheckCancel(cancel);
            xzdRequire(
                !file.isSymLink() && file.size() <= 32 * 1024 * 1024, xzdText(u8"备份文件过大或是链接：") + name);
            compressedSize += file.size();
            xzdRequire(compressedSize <= 128 * 1024 * 1024, xzdText(u8"备份包超过 128 MiB。"));
            xzdRequire(!source.contains(name), xzdText(u8"备份中存在同名表：") + name);
            QFile input(file.absoluteFilePath());
            if (!input.open(QIODevice::ReadOnly))
                xzdRequire(false, input.errorString());
            source.insert(name, input.readAll());
            xzdRequire(input.error() == QFile::NoError, input.errorString());
        }
        xzdRequire(source.contains("zwdata.val"), xzdText(u8"目录内没有 ZWDATA.VAL。"));
        result.metadata = source.value("zwdata.val");
        xzdRequire(readMetadata(result.metadata, QFileInfo(result.directory).fileName(), &result), result.error);
        QCryptographicHash digest(QCryptographicHash::Sha256);
        for (auto it = source.cbegin(); it != source.cend(); ++it) {
            digest.addData(it.key().toUtf8());
            digest.addData(QCryptographicHash::hash(it.value(), QCryptographicHash::Sha256));
            if (it.key() == "zwdata.val")
                continue;
            const QString name = it.key().left(it.key().size() - 4);
            xzdRequire(QRegularExpression("^[a-z][a-z0-9_]{0,63}$").match(name).hasMatch(),
                xzdText(u8"无效表文件名：") + it.key());
            QByteArray raw;
            QString error;
            if (!decode(it.value(), &raw, &error, cancel))
                xzdRequire(false, name + ": " + error);
            decodedSize += raw.size();
            xzdRequire(decodedSize <= 256 * 1024 * 1024, xzdText(u8"备份解压数据超过 256 MiB。"));
            XinZhongDaTable table;
            table.compressed = it.value();
            if (!raw.isEmpty()) {
                const QString text = xzdDecodeText(raw);
                xzdRequire(text.endsWith("\r\n"), name + xzdText(u8"：缺少行结束标记。"));
                auto lines = text.split("\r\n", QString::KeepEmptyParts);
                lines.removeLast();
                const auto columns = xzdColumns(name);
                const auto widths = xzdWidths(name);
                const bool vouchers = QRegularExpression("^z_pz(0[1-9]|1[0-2])$").match(name).hasMatch();
                for (const auto& line : lines) {
                    xzdCheckCancel(cancel);
                    const auto row = line.split('\t', QString::KeepEmptyParts);
                    if (!table.columns)
                        table.columns = row.size();
                    xzdRequire(
                        row.size() == table.columns && row.size() <= 256, name + xzdText(u8"：字段数量不一致。"));
                    if (!columns.isEmpty()) {
                        xzdRequire(widths.contains(table.columns),
                            name + xzdText(u8"：尚未验证的字段布局，拒绝按列序猜测恢复。"));
                        for (const auto& col : columns)
                            xzdValue(row[col.position - 1], col);
                    }
                    if (vouchers) {
                        ++result.voucherRows;
                        if (row[14] == "1")
                            ++result.approvedVoucherRows;
                        QString date = row[2];
                        date.replace('.', '-');
                        const auto dt = QDateTime::fromString(date, "yyyy-MM-dd HH:mm:ss");
                        result.lastVoucherDate = qMax(result.lastVoucherDate, dt.toString("yyyy-MM-dd HH:mm:ss"));
                        xzdRequire(dt.date().year() == result.year && dt.date().month() == name.right(2).toInt(),
                            name + xzdText(u8"：凭证日期与备份年度/月表不一致。"));
                    }
                    table.rows.append(row);
                }
            }
            result.tables.insert(name, table);
        }
        xzdRequire(result.tables.contains("km") && !result.tables["km"].rows.isEmpty() && result.tables.contains("hz"),
            xzdText(u8"缺少科目或余额数据。"));
        for (int m = 1; m <= 12; ++m)
            xzdRequire(result.tables.contains(QString("z_pz%1").arg(m, 2, 10, QChar('0'))),
                xzdText(u8"缺少月份凭证表，不能确认备份完整。"));
        result.fingerprint = digest.result();
        result.success = true;
        xzdProgress(progress,
            xzdText(u8"已读取 %1 个表、%2 条凭证明细，其中 %3 条已审核。未审核数据保留原状态。")
                .arg(result.tables.size())
                .arg(result.voucherRows)
                .arg(result.approvedVoucherRows));
    } catch (const std::exception& e) {
        result.error = QString::fromUtf8(e.what());
    }
    return result;
}

QString XinZhongDaBackupRestorer::targetDatabase(const QString& code, int year)
{
    if (!QRegularExpression("^[A-Za-z0-9]{1,12}$").match(code).hasMatch() || year < 1900 || year > 2099)
        return {};
    return "USER" + code + QString::number(year);
}
QVector<XinZhongDaBackupAnalysis> XinZhongDaBackupRestorer::discover(
    const QString& path, QString* error, const Cancel& cancel, const Progress& progress)
{
    QVector<XinZhongDaBackupAnalysis> catalog;
    error->clear();
    try {
        const QString root = resolveDirectory(path, error);
        xzdRequire(!root.isEmpty(), *error);
        QVector<QPair<QString, int>> pending { { root, 0 } };
        int visited = 0;
        while (!pending.isEmpty()) {
            xzdCheckCancel(cancel);
            const auto item = pending.takeLast();
            xzdRequire(++visited <= 2000, xzdText(u8"目录数量超过扫描上限，请选择更具体的账套目录。"));
            const auto files = QDir(item.first).entryInfoList(QDir::Files);
            QString metadata;
            for (const auto& file : files)
                if (file.fileName().compare("ZWDATA.VAL", Qt::CaseInsensitive) == 0)
                    metadata = file.absoluteFilePath();
            if (!metadata.isEmpty()) {
                auto snapshot = analyze(metadata, cancel);
                xzdRequire(snapshot.success, metadata + "\n" + snapshot.error);
                if (!snapshot.nameFromBackup)
                    snapshot.accountName = QFileInfo(root).fileName();
                snapshot.tables.clear();
                catalog.append(snapshot);
                xzdProgress(progress,
                    xzdText(u8"发现 %1 年备份：%2（%3 条凭证）")
                        .arg(snapshot.year)
                        .arg(QDir(root).relativeFilePath(snapshot.directory))
                        .arg(snapshot.voucherRows));
                xzdRequire(catalog.size() <= 200, xzdText(u8"备份快照超过 200 个，请缩小目录范围。"));
                continue;
            }
            for (const auto& child : QDir(item.first).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                if (child.isSymLink())
                    continue;
                xzdRequire(item.second < 8, xzdText(u8"目录层级过深，请直接选择账套目录。"));
                pending.append(qMakePair(child.absoluteFilePath(), item.second + 1));
            }
        }
        xzdRequire(!catalog.isEmpty(), xzdText(u8"目录内未找到新中大 ZWDATA.VAL 备份。"));
        std::sort(
            catalog.begin(), catalog.end(), [](const XinZhongDaBackupAnalysis& a, const XinZhongDaBackupAnalysis& b) {
                if (a.year != b.year)
                    return a.year < b.year;
                if (a.lastVoucherDate != b.lastVoucherDate)
                    return a.lastVoucherDate > b.lastVoucherDate;
                if (a.voucherRows != b.voucherRows)
                    return a.voucherRows > b.voucherRows;
                if (a.approvedVoucherRows != b.approvedVoucherRows)
                    return a.approvedVoucherRows > b.approvedVoucherRows;
                return a.directory < b.directory;
            });
        const QString code = catalog.first().accountCode;
        for (const auto& snapshot : catalog)
            xzdRequire(snapshot.accountCode.compare(code, Qt::CaseInsensitive) == 0,
                xzdText(u8"目录包含多个账套号，请分别选择各公司的目录。"));
    } catch (const std::exception& e) {
        *error = QString::fromUtf8(e.what());
        catalog.clear();
    }
    return catalog;
}
QString XinZhongDaBackupRestorer::retainDroppedBackup(const QString& path, QString* error)
{
    const QString dir = resolveDirectory(path, error);
    if (dir.isEmpty())
        return {};
    // A VAL file depends on its sibling tables; retain the complete flat package.
    const QString container
        = QDir(RepairPathSettings::inputDir()).filePath("xzd_" + QUuid::createUuid().toString(QUuid::Id128));
    const QString target = QDir(container).filePath(QFileInfo(dir).fileName());
    if (!QDir().mkpath(target)) {
        *error = xzdText(u8"无法创建备份留存目录。");
        return {};
    }
    for (const auto& file : QDir(dir).entryInfoList(QDir::Files)) {
        if (file.fileName().compare("ZWDATA.VAL", Qt::CaseInsensitive) != 0
            && file.suffix().compare("tx_", Qt::CaseInsensitive) != 0)
            continue;
        if (file.isSymLink() || !QFile::copy(file.absoluteFilePath(), QDir(target).filePath(file.fileName()))) {
            *error = xzdText(u8"备份留存失败，已复制的文件保留在：") + target;
            return {};
        }
    }
    return target;
}

struct XzdSession {
    QString name = "XzdRestore_" + QUuid::createUuid().toString(QUuid::Id128);
    QSqlDatabase db = QSqlDatabase::addDatabase("QODBC", name);
    ~XzdSession()
    {
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
};
static void xzdExec(QSqlDatabase& db, const QString& sql)
{
    QSqlQuery query(db);
    if (!query.exec(sql))
        xzdRequire(false, query.lastError().text());
    do {
        while (query.next()) { }
    } while (query.nextResult());
    xzdRequire(!query.lastError().isValid(), query.lastError().text());
}
static QVariant xzdScalar(QSqlDatabase& db, const QString& sql)
{
    QSqlQuery query(db);
    if (!query.exec(sql))
        xzdRequire(false, query.lastError().text());
    xzdRequire(query.next(), xzdText(u8"数据库查询未返回结果。"));
    return query.value(0);
}
static QString xzdType(const QString& type)
{
    if (type == "s")
        return "nvarchar(100)";
    if (type == "t")
        return "nvarchar(2000)";
    if (type == "i")
        return "int";
    if (type == "d")
        return "datetime";
    return "decimal(28,8)";
}
XinZhongDaRestoreResult XinZhongDaBackupRestorer::restore(const T3SqlConnection& options,
    const XinZhongDaBackupAnalysis& analysis, const QString& accountCode, const Cancel& cancel,
    const Progress& progress, const QString& controlDatabase)
{
    return restoreMany(options, { analysis }, accountCode, cancel, progress, controlDatabase);
}
XinZhongDaRestoreResult XinZhongDaBackupRestorer::restoreMany(const T3SqlConnection& options,
    const QVector<XinZhongDaBackupAnalysis>& analyses, const QString& accountCode, const Cancel& cancel,
    const Progress& progress, const QString& controlDatabase)
{
    XinZhongDaRestoreResult result;
    if (analyses.isEmpty()) {
        result.error = xzdText(u8"请选择要恢复的年度。");
        return result;
    }
    const auto& analysis = analyses.first();
    result.database = targetDatabase(accountCode, analysis.year);
    XzdSession session;
    bool transaction = false;
    QStringList created;
    const QString token = "xzd_" + QUuid::createUuid().toString(QUuid::Id128);
    try {
        xzdRequire(analysis.success && !result.database.isEmpty(),
            xzdText(u8"请先读取有效备份，目标账套号只能包含 1—12 位字母或数字。"));
        xzdRequire(QRegularExpression("^[A-Za-z][A-Za-z0-9_]{0,63}$").match(controlDatabase).hasMatch()
                && !QStringList { "master", "model", "msdb", "tempdb" }.contains(controlDatabase.toLower()),
            xzdText(u8"控制库名称无效。"));
        QVector<XinZhongDaBackupAnalysis> snapshots;
        QSet<int> years;
        QString accountName;
        int startYear = 2099;
        for (const auto& original : analyses) {
            auto fresh = analyze(original.directory, cancel);
            xzdRequire(fresh.success && fresh.fingerprint == original.fingerprint,
                xzdText(u8"备份自读取后已改变，请重新读取。") + fresh.error);
            xzdRequire(!years.contains(fresh.year), xzdText(u8"同一年度只能选择一个备份快照，不能合并月度快照。"));
            years.insert(fresh.year);
            xzdRequire(fresh.accountCode.compare(analysis.accountCode, Qt::CaseInsensitive) == 0,
                xzdText(u8"一次只能恢复同一原始账套的多个年度。"));
            if (!fresh.nameFromBackup)
                fresh.accountName = original.accountName;
            if (accountName.isEmpty())
                accountName = fresh.accountName;
            xzdRequire(accountName == fresh.accountName,
                xzdText(u8"所选年度的账套名称不一致，请分别恢复或选择同一账套目录。"));
            startYear = qMin(startYear, fresh.year);
            snapshots.append(fresh);
            result.databases.append(targetDatabase(accountCode, fresh.year));
        }
        T3ServerInfo server;
        QString error;
        if (!T3BackupRestorer::testConnection(options, &server, &error))
            xzdRequire(false, error);
        xzdRequire(server.version.section('.', 0, 0).toInt() >= 10, xzdText(u8"需要 SQL Server 2008 或更高版本。"));
        session.db.setConnectOptions("SQL_ATTR_LOGIN_TIMEOUT=10");
        session.db.setDatabaseName(T3BackupRestorer::connectionString(options));
        if (!session.db.open())
            xzdRequire(false, session.db.lastError().text());
        xzdExec(session.db, "USE master; SET XACT_ABORT ON;");
        xzdExec(session.db,
            "DECLARE @r int; EXEC @r=sys.sp_getapplock @Resource=" + xzdQuote("LemonXzd_" + controlDatabase)
                + ",@LockMode='Exclusive',@LockOwner='Session',@LockTimeout=10000; IF @r<0 "
                  "RAISERROR(N'无法取得恢复锁，请稍后重试。',16,1);");
        for (const auto& database : result.databases)
            xzdRequire(xzdScalar(session.db, "SELECT DB_ID(" + xzdQuote(database) + ")").isNull(),
                xzdText(u8"目标数据库已存在，请修改目标账套号，已有库不会被覆盖。"));
        const bool hasControl = !xzdScalar(session.db, "SELECT DB_ID(" + xzdQuote(controlDatabase) + ")").isNull();
        const QString unit = xzdId(controlDatabase) + ".dbo.unit";
        if (hasControl) {
            xzdRequire(
                !xzdScalar(session.db, "SELECT OBJECT_ID(" + xzdQuote(controlDatabase + ".dbo.unit") + ")").isNull(),
                xzdText(u8"现有控制库没有 unit 表，未修改该库。"));
            xzdExec(session.db, "SELECT TOP(0) dwdm,dwmc,jzrq FROM " + unit);
            xzdRequire(
                xzdScalar(session.db, "SELECT COUNT(*) FROM " + unit + " WHERE RTRIM(dwdm)=" + xzdQuote(accountCode))
                        .toInt()
                    == 0,
                xzdText(u8"目标账套号已在控制库登记，请更换账套号。"));
        }
        xzdCheckCancel(cancel);
        const auto create = [&](const QString& database) {
            const QString file
                = QDir::toNativeSeparators(QDir(server.dataDirectory).filePath(token + "_" + database + ".mdf"));
            const QString log
                = QDir::toNativeSeparators(QDir(server.logDirectory).filePath(token + "_" + database + ".ldf"));
            xzdRequire(!server.dataDirectory.isEmpty() && !server.logDirectory.isEmpty(),
                xzdText(u8"无法读取 SQL Server 默认数据/日志目录。"));
            xzdExec(session.db,
                "CREATE DATABASE " + xzdId(database) + " ON PRIMARY(NAME=" + xzdQuote(database)
                    + ",FILENAME=" + xzdQuote(file) + ") LOG ON(NAME=" + xzdQuote(database + "_log")
                    + ",FILENAME=" + xzdQuote(log) + ") COLLATE Chinese_PRC_CI_AS;");
            created.append(database);
        };
        for (const auto& database : result.databases)
            create(database);
        if (!hasControl)
            create(controlDatabase);
        xzdExec(session.db, "USE " + xzdId(result.database));
        if (!session.db.transaction())
            xzdRequire(false, session.db.lastError().text());
        transaction = true;
        if (!hasControl)
            xzdExec(session.db,
                "CREATE TABLE " + unit
                    + "(dwdm nvarchar(12) NOT NULL PRIMARY KEY,dwmc nvarchar(500) NOT NULL,jzrq datetime NOT NULL)");
        for (const auto& fresh : snapshots) {
            xzdProgress(progress, xzdText(u8"正在恢复 %1 年：%2").arg(fresh.year).arg(fresh.directory));
            xzdExec(session.db, "USE " + xzdId(targetDatabase(accountCode, fresh.year)));
            for (const auto& name : xzdCollectorTables()) {
                xzdCheckCancel(cancel);
                const auto columns = xzdColumns(name);
                QStringList definitions, names;
                for (const auto& col : columns) {
                    definitions.append(xzdId(col.name) + " " + xzdType(col.type) + " NULL");
                    names.append(xzdId(col.name));
                }
                xzdExec(session.db, "CREATE TABLE dbo." + xzdId(name) + "(" + definitions.join(',') + ")");
                const auto source = fresh.tables.constFind(name);
                if (source == fresh.tables.cend())
                    continue;
                QString batch;
                int count = 0;
                for (const auto& row : source->rows) {
                    xzdCheckCancel(cancel);
                    QStringList values;
                    for (const auto& col : columns)
                        values.append(xzdValue(row[col.position - 1], col));
                    batch += "INSERT dbo." + xzdId(name) + "(" + names.join(',') + ") VALUES(" + values.join(',')
                        + ");\n";
                    if (++count % 100 == 0) {
                        xzdExec(session.db, batch);
                        batch.clear();
                    }
                }
                if (!batch.isEmpty())
                    xzdExec(session.db, batch);
                xzdRequire(xzdScalar(session.db, "SELECT COUNT(*) FROM dbo." + xzdId(name)).toLongLong()
                        == source->rows.size(),
                    name + xzdText(u8"：恢复行数不一致。"));
                xzdProgress(progress, xzdText(u8"已恢复 %1：%2 行。").arg(name).arg(source->rows.size()));
            }
            xzdExec(session.db, "CREATE SCHEMA raw AUTHORIZATION dbo");
            xzdExec(session.db,
                "CREATE TABLE raw.backup_files(name nvarchar(128) NOT NULL PRIMARY KEY,content varbinary(max) NOT "
                "NULL)");
            auto store = [&](const QString& name, const QByteArray& content) {
                xzdCheckCancel(cancel);
                QSqlQuery query(session.db);
                if (!query.prepare("INSERT raw.backup_files(name,content) VALUES(?,?)"))
                    xzdRequire(false, query.lastError().text());
                query.addBindValue(name);
                query.addBindValue(content);
                if (!query.exec())
                    xzdRequire(false, query.lastError().text());
                QSqlQuery verify(session.db);
                verify.prepare("SELECT content FROM raw.backup_files WHERE name=?");
                verify.addBindValue(name);
                xzdRequire(verify.exec() && verify.next() && verify.value(0).toByteArray() == content,
                    xzdText(u8"原始文件保存核对失败：") + name);
            };
            store("ZWDATA.VAL", fresh.metadata);
            for (auto it = fresh.tables.cbegin(); it != fresh.tables.cend(); ++it)
                store(it.key() + ".tx_", it->compressed);
        }
        xzdCheckCancel(cancel);
        xzdExec(session.db,
            "INSERT " + unit + "(dwdm,dwmc,jzrq) VALUES(" + xzdQuote(accountCode) + "," + xzdQuote(accountName) + ","
                + xzdQuote(QString::number(startYear) + "0101") + ")");
        // Read back registration: detects string truncation on older ODBC configurations.
        xzdRequire(xzdScalar(session.db, "SELECT dwmc FROM " + unit + " WHERE dwdm=" + xzdQuote(accountCode)).toString()
                == accountName,
            xzdText(u8"账套名称登记不完整，已回滚。"));
        if (!session.db.commit())
            xzdRequire(false, session.db.lastError().text());
        transaction = false;
        result.success = true;
        xzdProgress(progress,
            xzdText(u8"恢复完成：%1 / %2。请使用包含月份、科目关联条件的新版新中大采集器；未审核凭证保持未审核。")
                .arg(result.databases.join(", "), accountName));
    } catch (const std::exception& e) {
        result.error = QString::fromUtf8(e.what());
        result.cancelled = cancel && cancel();
        if (transaction) {
            session.db.rollback();
            transaction = false;
        }
        // Only databases created by this call with its unique physical file token
        // may be removed. Never change another session to SINGLE_USER.
        if (session.db.isOpen())
            for (auto it = created.crbegin(); it != created.crend(); ++it) {
                try {
                    xzdExec(session.db, "USE master");
                    xzdRequire(xzdScalar(session.db,
                                   "SELECT COUNT(*) FROM sys.master_files WHERE database_id=DB_ID(" + xzdQuote(*it)
                                       + ") AND CHARINDEX(" + xzdQuote(token) + ",physical_name)=0")
                                   .toInt()
                            == 0,
                        "ownership mismatch");
                    xzdExec(session.db, "DROP DATABASE " + xzdId(*it));
                } catch (const std::exception&) {
                    result.error += xzdText(u8"\n未清理本次创建的数据库（可能正在使用）：") + *it;
                }
            }
    }
    if (!options.password.isEmpty())
        result.error.replace(options.password, "***");
    return result;
}
