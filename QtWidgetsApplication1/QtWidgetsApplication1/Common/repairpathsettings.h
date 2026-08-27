#ifndef REPAIRPATHSETTINGS_H
#define REPAIRPATHSETTINGS_H

#include <QString>

namespace RepairPathSettings
{
QString inputDir();
QString outputDir();
void setInputDir(const QString &path);
void setOutputDir(const QString &path);
}

#endif // REPAIRPATHSETTINGS_H
