#pragma once
#include "repairpathsettings.h"
#include <QComboBox>
#include <QDir>
#include <QLineEdit>
#include <QSettings>

namespace RestorePreferences {
inline QString backupDirectory()
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
        QStringLiteral("LemonCloud"), QStringLiteral("LemonWorkTool"));
    return settings.value(QStringLiteral("BackupRestore/backupDirectory"), RepairPathSettings::inputDir()).toString();
}
inline void setBackupDirectory(const QString& path)
{
    if (path.trimmed().isEmpty()) return;
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
        QStringLiteral("LemonCloud"), QStringLiteral("LemonWorkTool"));
    settings.setValue(QStringLiteral("BackupRestore/backupDirectory"), QDir::toNativeSeparators(path));
}
inline QString stagingDirectory()
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
        QStringLiteral("LemonCloud"), QStringLiteral("LemonWorkTool"));
    const QString key = QStringLiteral("BackupRestore/stagingDirectory");
    if (!settings.contains(key)) {
        // Migrate one existing preference deterministically; all targets share it thereafter.
        settings.beginGroup(QStringLiteral("BackupRestore/staging"));
        QStringList keys = settings.childKeys();
        const QString local = QStringLiteral("2e");
        if (keys.removeOne(local)) keys.prepend(local);
        QString path = QStringLiteral("D:\\Backup");
        for (const auto& oldKey : keys) {
            const QString saved = settings.value(oldKey).toString().trimmed();
            if (!saved.isEmpty()) { path = saved; break; }
        }
        settings.endGroup();
        settings.setValue(key, path);
    }
    return settings.value(key).toString();
}
inline void setStagingDirectory(const QString& path)
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
        QStringLiteral("LemonCloud"), QStringLiteral("LemonWorkTool"));
    settings.setValue(QStringLiteral("BackupRestore/stagingDirectory"), path.trimmed());
}
inline void bindStaging(QLineEdit* edit)
{
    edit->setText(stagingDirectory());
    QObject::connect(edit, &QLineEdit::textChanged, edit, [edit]() {
        setStagingDirectory(edit->text());
    });
}
}
