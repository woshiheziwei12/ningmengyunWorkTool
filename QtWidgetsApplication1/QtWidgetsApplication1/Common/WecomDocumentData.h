#pragma once
#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QUrl>
#include <QVector>

namespace WecomDocument {
enum class SheetSelection { FromDate, LatestDate, FromDateInName, All };
struct Document {
    QString name;
    QUrl url;
    SheetSelection selection = SheetSelection::FromDate;
};
struct Cell {
    int row = 0; // Zero based, including blank rows/columns in the source.
    int column = 0;
    QString text;
    QStringList links;
    QJsonValue rawNumber;
    QString numberFormat;
};
struct Sheet {
    QString id;
    QString name;
    QDate date;
    int rowCount = 0;
    int columnCount = 0;
    QVector<Cell> cells;
    QString error;
    bool loaded = false;
};
struct Metadata {
    QString title;
    QString padId;
    QString revision;
    int year = 0;
    QVector<Sheet> sheets;
};
struct Snapshot {
    QString documentId;
    QString title;
    QDate fromDate;
    QDateTime fetchedAt;
    QVector<Sheet> sheets;
    bool cancelled = false;
    SheetSelection selection = SheetSelection::FromDate;
    QJsonObject toJson() const;
};
QString documentId();
QUrl defaultUrl();
// Returns the ID of a supported WeCom sheet URL, or an empty string.
QString documentIdFromUrl(const QUrl &url);
bool validateUrl(const QUrl &url, QString *error);
// Parses the shared date filter. An omitted value starts at the local date.
QDate fromDate(const QJsonValue &value);
QDate sheetDate(const QString &name, int year);
// Names used in the catalog and CLI JSON.
QString sheetSelectionKey(SheetSelection selection);
bool parseMetadata(const QJsonObject &response, Metadata *metadata, QString *error, const Document &document = {});
QVector<Sheet> selectSheets(const Metadata &metadata, const QDate &fromDate, SheetSelection selection = SheetSelection::FromDate);
// Decodes the verified sheet protocol, not a string scan. Unsupported values fail explicitly.
bool parseSheet(const QJsonObject &response, Sheet *sheet, QString *error);
}
