#pragma once

#include <QDir>
#include <QStandardPaths>

namespace LemonWorkToolPaths {
// Returns a stable location for fallback files belonging to the current Windows user.
inline QString userDataDirectory()
{
    const QString localData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return localData.isEmpty() ? QString() : QDir(localData).filePath("LemonCloud/LemonWorkTool");
}
}
