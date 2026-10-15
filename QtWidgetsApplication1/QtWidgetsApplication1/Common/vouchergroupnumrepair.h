#pragma once

#include <QJsonObject>
#include <QStringList>

namespace VoucherGroupNumRepair
{
struct Result
{
    int changedRows = 0;
    int normalizedVouchers = 0;
    int renumberedVouchers = 0;
    QStringList details;
    QString error;
};

// Changes only GLVoucherList[*].GroupNum; leaves root unchanged on failure.
Result repair(QJsonObject &root);
}
