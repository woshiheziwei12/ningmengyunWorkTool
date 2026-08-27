#ifndef REPAIRFILENAMEUTILS_H
#define REPAIRFILENAMEUTILS_H

#include <QRegularExpression>
#include <QString>

namespace RepairFileNameUtils
{
inline QString safePart(QString value)
{
    value = value.trimmed();
    value.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1F]")),
                  QStringLiteral("_"));
    while (value.endsWith(QLatin1Char('.')) || value.endsWith(QLatin1Char(' ')))
        value.chop(1);
    return value;
}

inline QString makeBaseName(const QString &companyName,
                            const QString &fallbackName,
                            const QString &repairContent)
{
    QString safeCompanyName = safePart(companyName);
    if (safeCompanyName.isEmpty())
        safeCompanyName = safePart(fallbackName);
    if (safeCompanyName.isEmpty())
        safeCompanyName = QStringLiteral("AccountSet");

    const QString safeRepairContent = safePart(repairContent);
    return safeRepairContent.isEmpty()
        ? safeCompanyName
        : safeCompanyName + QLatin1Char('_') + safeRepairContent;
}
}

#endif // REPAIRFILENAMEUTILS_H
