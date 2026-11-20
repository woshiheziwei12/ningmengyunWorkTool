#pragma once
#include <QJsonObject>
#include <QString>

// Reads all live user-table records without changing the source. Throws on
// unsupported types, malformed pages, invalid encoding or bounded-size limits.
QJsonObject readJet3Snapshot(const QString& sourcePath);
