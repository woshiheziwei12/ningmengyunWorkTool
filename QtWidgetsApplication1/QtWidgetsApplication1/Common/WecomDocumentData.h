#pragma once
#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QUrl>
#include <QVector>

namespace WecomDocument {
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
    QString title;
    QDate fromDate;
    QDateTime fetchedAt;
    QVector<Sheet> sheets;
    bool cancelled = false;
    QJsonObject toJson() const;
};
QString documentId();
QUrl defaultUrl();
bool validateUrl(const QUrl &url, QString *error);
QDate sheetDate(const QString &name, int year);
bool parseMetadata(const QJsonObject &response, Metadata *metadata, QString *error);
QVector<Sheet> selectSheets(const Metadata &metadata, const QDate &fromDate);
// Decodes the verified sheet protocol, not a string scan. Unsupported values fail explicitly.
bool parseSheet(const QJsonObject &response, Sheet *sheet, QString *error);
}
