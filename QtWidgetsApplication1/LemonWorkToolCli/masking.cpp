#include "masking.h"

QString maskSecret(const QString &value)
{
    if (value.isEmpty())
        return QString();
    if (value.size() <= 8)
        return QStringLiteral("***");
    return value.left(4) + QStringLiteral("***") + value.right(4);
}

QString maskMobile(const QString &value)
{
    if (value.size() < 7)
        return maskSecret(value);
    return value.left(3) + QStringLiteral("****") + value.right(4);
}
