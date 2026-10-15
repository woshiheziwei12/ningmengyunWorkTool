#include "WecomDocumentData.h"
#include "../ThirdParty/zlib/zlib.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace {
constexpr int MaxDecodedBytes = 64 * 1024 * 1024;
constexpr int MaxCells = 500000;
struct ProtocolError {};
struct Field { int number = 0; int wire = 0; quint64 integer = 0; QByteArray bytes; };
using Message = QVector<Field>;
quint64 varint(const QByteArray &bytes, int &position)
{
    quint64 value = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (position >= bytes.size()) throw ProtocolError();
        const quint8 next = quint8(bytes.at(position++));
        if (shift == 63 && next > 1) throw ProtocolError();
        value |= quint64(next & 127) << shift;
        if (!(next & 128)) return value;
    }
    throw ProtocolError();
}
Message decode(const QByteArray &bytes)
{
    Message message;
    int position = 0;
    while (position < bytes.size()) {
        const quint64 tag = varint(bytes, position);
        if ((tag >> 3) == 0 || (tag >> 3) > 0x1fffffff) throw ProtocolError();
        Field field; field.number = int(tag >> 3); field.wire = int(tag & 7);
        if (field.wire == 0) field.integer = varint(bytes, position);
        else {
            quint64 size = 0;
            if (field.wire == 2) size = varint(bytes, position);
            else if (field.wire == 1) size = 8;
            else if (field.wire == 5) size = 4;
            else throw ProtocolError();
            if (size > quint64(bytes.size() - position)) throw ProtocolError();
            field.bytes = bytes.mid(position, int(size)); position += int(size);
        }
        message.append(std::move(field));
        if (message.size() > MaxCells * 4) throw ProtocolError();
    }
    return message;
}
QByteArray bytesAt(const Message &message, int number)
{
    for (const auto &field : message) if (field.number == number) {
        if (field.wire != 2) throw ProtocolError();
        return field.bytes;
    }
    return {};
}
quint64 integerAt(const Message &message, int number)
{
    for (const auto &field : message) if (field.number == number) {
        if (field.wire != 0) throw ProtocolError();
        return field.integer;
    }
    return 0;
}
QString wrappedString(const QByteArray &bytes)
{
    const auto raw = bytesAt(decode(bytes), 1);
    const auto text = QString::fromUtf8(raw);
    if (text.toUtf8() != raw) throw ProtocolError();
    return text;
}
QByteArray decompress(const QString &encoded)
{
    if (encoded.isEmpty() || encoded.size() > MaxDecodedBytes) throw ProtocolError();
    const auto raw = encoded.toLatin1();
    const QByteArray compressed = QByteArray::fromBase64(raw);
    // Qt 5.12's Base64 decoder otherwise silently accepts malformed input.
    if (compressed.toBase64() != raw) throw ProtocolError();
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK) throw ProtocolError();
    struct End { z_stream *stream; ~End() { inflateEnd(stream); } } end{&stream};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.constData()));
    stream.avail_in = uInt(compressed.size());
    QByteArray result;
    char buffer[32768];
    int status = Z_OK;
    do {
        stream.next_out = reinterpret_cast<Bytef *>(buffer); stream.avail_out = sizeof(buffer);
        status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END) throw ProtocolError();
        const int size = int(sizeof(buffer) - stream.avail_out);
        if (result.size() > MaxDecodedBytes - size) throw ProtocolError();
        result.append(buffer, size);
    } while (status != Z_STREAM_END);
    if (stream.avail_in) throw ProtocolError();
    return result;
}
WecomDocument::Cell richCell(const QByteArray &bytes)
{
    WecomDocument::Cell result;
    const auto rich = decode(bytes);
    bool hasRun = false;
    for (const auto &field : rich) if (field.number == 3 && field.wire == 2) {
        hasRun = true;
        const auto run = decode(field.bytes);
        result.text += wrappedString(bytesAt(run, 3));
        const auto hyperlink = decode(bytesAt(run, 7));
        const auto url = wrappedString(bytesAt(hyperlink, 11));
        if (!url.isEmpty() && !result.links.contains(url)) result.links.append(url);
    }
    if (!hasRun) result.text = wrappedString(bytesAt(rich, 1));
    return result;
}
void parseBatch(const QByteArray &bytes, WecomDocument::Sheet *sheet)
{
    const auto batch = decode(bytes);
    const auto pool = decode(bytesAt(batch, 5));
    QVector<QString> strings;
    QVector<WecomDocument::Cell> rich;
    QVector<double> numbers;
    QVector<QByteArray> styles, formats;
    for (const auto &entry : decode(bytesAt(batch, 4))) {
        if (entry.number == 1 && entry.wire == 2) styles.append(entry.bytes);
        if (entry.number == 2 && entry.wire == 2) formats.append(entry.bytes);
    }
    for (const auto &entry : pool) {
        if (entry.wire != 2) continue;
        if (entry.number == 1) strings.append(wrappedString(entry.bytes));
        else if (entry.number == 2) rich.append(richCell(entry.bytes));
        else if (entry.number == 3) {
            double number = 0;
            for (const auto &field : decode(entry.bytes)) if (field.number == 1) {
                if (field.wire != 1) throw ProtocolError();
                const quint64 bits = qFromLittleEndian<quint64>(reinterpret_cast<const uchar *>(field.bytes.constData()));
                std::memcpy(&number, &bits, sizeof(number));
            }
            if (!std::isfinite(number)) throw ProtocolError();
            numbers.append(number);
        }
    }
    for (const auto &entry : batch) if (entry.number == 6 && entry.wire == 2) {
        const auto coordinate = decode(entry.bytes);
        const auto data = decode(bytesAt(coordinate, 3));
        const quint64 kind = integerAt(data, 1);
        if (kind == 0) continue; // Style-only/empty cell, keeps its position via dimensions.
        const quint64 row = integerAt(coordinate, 1), column = integerAt(coordinate, 2);
        if (row >= quint64(sheet->rowCount) || column >= quint64(sheet->columnCount)) throw ProtocolError();
        const auto value = decode(bytesAt(data, 2));
        const quint64 index = integerAt(value, 1);
        WecomDocument::Cell cell;
        if (kind == 4) {
            if (index >= quint64(strings.size())) throw ProtocolError();
            cell.text = strings[int(index)];
        } else if (kind == 6) {
            if (index >= quint64(rich.size())) throw ProtocolError();
            cell = rich[int(index)];
        } else if (kind == 2) {
            double number;
            if (index <= 128) number = double(index);
            else {
                if (index - 129 >= quint64(numbers.size())) throw ProtocolError();
                number = numbers[int(index - 129)];
            }
            cell.text = QString::number(number, 'g', 17);
            cell.rawNumber = number;
            bool hasStyle = false;
            for (const auto &field : data) if (field.number == 4) hasStyle = true;
            if (hasStyle) {
                const quint64 styleIndex = integerAt(decode(bytesAt(data, 4)), 1);
                if (styleIndex >= quint64(styles.size())) throw ProtocolError();
                const auto formatId = integerAt(decode(bytesAt(decode(styles[int(styleIndex)]), 7)), 1);
                if (formatId == 0) cell.numberFormat = "General";
                else if (formatId == 49) cell.numberFormat = "@";
                else if (formatId == 72) cell.numberFormat = "yyyy/m/d";
                else if (formatId >= 300) {
                    if (formatId - 300 >= quint64(formats.size())) throw ProtocolError();
                    cell.numberFormat = wrappedString(bytesAt(decode(formats[int(formatId - 300)]), 3));
                } else cell.numberFormat = QStringLiteral("builtin:%1").arg(formatId);
                const QString dateFormat = cell.numberFormat.section(';', 0, 0).toLower();
                if (dateFormat == "yyyy-mm-dd" || dateFormat == "yyyy/m/d" || dateFormat == "yyyy/mm/dd") {
                    if (number < 61 || number >= 2958466) throw ProtocolError();
                    const QDate date = QDate(1899, 12, 30).addDays(qint64(std::floor(number)));
                    QString qtFormat = dateFormat; qtFormat.replace('m', 'M');
                    cell.text = date.toString(qtFormat);
                }
            }
        } else {
            // Never silently export a formula/date/error using an unverified type mapping.
            throw std::runtime_error("unsupported-cell-type");
        }
        cell.row = int(row); cell.column = int(column);
        if (sheet->cells.size() >= MaxCells) throw ProtocolError();
        sheet->cells.append(std::move(cell));
    }
}
}

namespace WecomDocument {
QString documentId() { return QStringLiteral("e3_ARwAqwY-AAkCNmen80OXPQyS100NJ"); }
QUrl defaultUrl() { return QUrl("https://doc.weixin.qq.com/sheet/" + documentId()); }
bool validateUrl(const QUrl &url, QString *error)
{
    const bool valid = url.isValid() && url.scheme() == "https" && url.host() == "doc.weixin.qq.com"
        && url.port(443) == 443 && url.userInfo().isEmpty() && url.path() == defaultUrl().path();
    if (!valid && error) *error = QStringLiteral("请输入“2026-运营支持组项目进度表”的企业微信文档链接。当前功能仅支持这份文档。");
    return valid;
}
QDate sheetDate(const QString &name, int year)
{
    static const QRegularExpression shortDate("^(\\d{1,2})-(\\d{1,2})$");
    static const QRegularExpression fullDate("^(\\d{4})-(\\d{1,2})-(\\d{1,2})$");
    const auto full = fullDate.match(name.trimmed());
    if (full.hasMatch()) return QDate(full.captured(1).toInt(), full.captured(2).toInt(), full.captured(3).toInt());
    const auto partial = shortDate.match(name.trimmed());
    return partial.hasMatch() ? QDate(year, partial.captured(1).toInt(), partial.captured(2).toInt()) : QDate();
}
bool parseMetadata(const QJsonObject &response, Metadata *metadata, QString *error)
{
    *metadata = Metadata();
    const auto client = response.value("clientVars").toObject();
    const auto collab = client.value("collab_client_vars").toObject();
    metadata->title = client.value("title").toString();
    metadata->padId = collab.value("padId").toString();
    const auto revision = collab.value("rev");
    metadata->revision = revision.isString() ? revision.toString() : QString::number(revision.toDouble(), 'f', 0);
    static const QRegularExpression yearPattern("^\\s*(20\\d{2})(?:\\D|$)");
    const auto year = yearPattern.match(metadata->title);
    metadata->year = year.captured(1).toInt();
    if (collab.isEmpty() || metadata->padId.isEmpty() || revision.isUndefined() || !year.hasMatch() || !collab.value("header").isArray()) {
        if (error) *error = QStringLiteral("无法获取文档目录或年份，请确认已扫码登录且有权查看该文档。"); return false;
    }
    QSet<QString> ids;
    for (const auto &group : collab.value("header").toArray()) {
        for (const auto &entry : group.toObject().value("d").toArray()) {
            const auto object = entry.toObject();
            Sheet sheet; sheet.id = object.value("id").toString(); sheet.name = object.value("name").toString();
            if (sheet.id.isEmpty() || sheet.name.isEmpty() || ids.contains(sheet.id)) {
                if (error) *error = QStringLiteral("文档目录格式异常或存在重复工作表。"); return false;
            }
            ids.insert(sheet.id); sheet.date = sheetDate(sheet.name, metadata->year);
            metadata->sheets.append(sheet);
        }
    }
    return true;
}
QVector<Sheet> selectSheets(const Metadata &metadata, const QDate &fromDate)
{
    QVector<Sheet> result;
    for (const auto &sheet : metadata.sheets) if (sheet.date.isValid() && sheet.date >= fromDate) result.append(sheet);
    std::stable_sort(result.begin(), result.end(), [](const Sheet &a, const Sheet &b) { return a.date < b.date; });
    return result;
}
bool parseSheet(const QJsonObject &response, Sheet *sheet, QString *error)
{
    Sheet parsed = *sheet; parsed.cells.clear(); parsed.loaded = false;
    parsed.rowCount = 0; parsed.columnCount = 0;
    auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (!response.contains("retcode") || response.value("retcode").toInt(-1) != 0)
        return fail(QStringLiteral("工作表接口拒绝读取，请检查登录状态及访问权限。"));
    const auto data = response.value("data").toObject();
    if (data.value("startrow").toInt(-2) != 0 || data.value("endrow").toInt(-2) != -1)
        return fail(QStringLiteral("接口没有返回请求的完整工作表范围，未导出部分数据。"));
    // Synchronous successful responses also carry async_open_done=false. It is only
    // meaningful after the separate 200156 async-poll response (handled by the reader).
    if (data.value("padSubId").toString() != sheet->id && data.value("sheetId").toString() != sheet->id)
        return fail(QStringLiteral("接口返回的工作表与请求不一致。"));
    const auto texts = data.value("initialAttributedText").toObject().value("text");
    if (!texts.isArray() || texts.toArray().isEmpty()) return fail(QStringLiteral("工作表缺少内容，不能作为空表导出。"));
    try {
        bool foundCells = false;
        bool verifiedEmpty = true;
        for (const auto &value : texts.toArray()) {
            const auto text = value.toObject();
            const int rows = text.value("max_row").toInt(-1), columns = text.value("max_col").toInt(-1);
            if (rows < 0 || columns < 0 || rows > 100000 || columns > 2000 || qint64(rows) * columns > 2000000) throw ProtocolError();
            parsed.rowCount = qMax(parsed.rowCount, rows); parsed.columnCount = qMax(parsed.columnCount, columns);
            const auto root = decode(decompress(text.value("related_sheet").toString()));
            bool foundCommand = false;
            for (const auto &outer : root) if (outer.number == 1 && outer.wire == 2) {
                foundCommand = true;
                const auto command = decode(outer.bytes);
                if (integerAt(command, 1) != 1 || wrappedString(bytesAt(command, 4)) != "3.6.6") verifiedEmpty = false;
                for (const auto &mutation : command) if (mutation.number == 5 && mutation.wire == 2) {
                    verifiedEmpty = false;
                    const auto fields = decode(mutation.bytes);
                    if (integerAt(fields, 1) == 18) { foundCells = true; parseBatch(bytesAt(fields, 19), &parsed); }
                }
            }
            if (!foundCommand) verifiedEmpty = false;
        }
        if (!foundCells && !verifiedEmpty) return fail(QStringLiteral("未识别到完整单元格数据，拒绝导出不完整工作表。"));
        std::sort(parsed.cells.begin(), parsed.cells.end(), [](const Cell &a, const Cell &b) {
            return a.row == b.row ? a.column < b.column : a.row < b.row;
        });
        for (int i = 1; i < parsed.cells.size(); ++i)
            if (parsed.cells[i].row == parsed.cells[i-1].row && parsed.cells[i].column == parsed.cells[i-1].column)
                return fail(QStringLiteral("响应包含重复单元格，无法确认完整快照，请重新获取。"));
    } catch (const std::runtime_error &) {
        return fail(QStringLiteral("工作表含尚未支持的单元格类型（公式、日期或其他类型），未导出以免丢失内容。"));
    } catch (const ProtocolError &) {
        return fail(QStringLiteral("工作表数据不完整、格式已变更或超过读取上限，未导出。"));
    }
    parsed.loaded = true; parsed.error.clear(); *sheet = std::move(parsed); return true;
}
QJsonObject Snapshot::toJson() const
{
    QJsonArray tables;
    bool complete = !cancelled;
    for (const auto &sheet : sheets) {
        QJsonArray cells;
        for (const auto &cell : sheet.cells) {
            QJsonArray links; for (const auto &link : cell.links) links.append(link);
            QJsonObject item{{"row", cell.row}, {"column", cell.column}, {"text", cell.text}, {"links", links}};
            if (cell.rawNumber.isDouble()) { item.insert("rawNumber", cell.rawNumber); item.insert("numberFormat", cell.numberFormat); }
            cells.append(item);
        }
        complete = complete && sheet.loaded;
        tables.append(QJsonObject{{"id", sheet.id}, {"name", sheet.name}, {"date", sheet.date.toString(Qt::ISODate)},
            {"rowCount", sheet.rowCount}, {"columnCount", sheet.columnCount}, {"loaded", sheet.loaded},
            {"error", sheet.error}, {"cells", cells}});
    }
    // Share codes, request URLs, browser state and authentication tokens never enter exports.
    return {{"schemaVersion", 1}, {"documentId", documentId()}, {"title", title}, {"fromDate", fromDate.toString(Qt::ISODate)},
        {"fetchedAt", fetchedAt.toUTC().toString(Qt::ISODate)}, {"coordinateBase", 0}, {"complete", complete},
        {"cancelled", cancelled}, {"sheets", tables}};
}
}
