#pragma once

#include "repairpathsettings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>

namespace RepairInputFileUtils
{
// Return the retained input path. QFile::copy never overwrites an existing file.
inline QString copyToInputDirectory(const QString &path, QString *errorMessage = nullptr)
{
    if (errorMessage)
        errorMessage->clear();
    const auto fail = [errorMessage](const QString &message) {
        if (errorMessage)
            *errorMessage = message;
        return QString();
    };
    const QFileInfo source(path);
    if (!source.isFile())
        return fail(QStringLiteral("源文件不存在或不是普通文件：%1").arg(path));

    const QString inputPath = RepairPathSettings::inputDir();
    if (!QDir().mkpath(inputPath))
        return fail(QStringLiteral("无法创建共享输入目录：%1").arg(QDir::toNativeSeparators(inputPath)));
    const QString canonicalInput = QFileInfo(inputPath).canonicalFilePath();
    const QString sourcePath = source.canonicalFilePath();
    if (canonicalInput.isEmpty() || sourcePath.isEmpty())
        return fail(QStringLiteral("无法解析源文件或共享输入目录：%1").arg(path));
    const QDir inputDir(canonicalInput);
    const QString relative = inputDir.relativeFilePath(sourcePath);
    if (!QDir::isAbsolutePath(relative) && relative != QStringLiteral("..")
        && !relative.startsWith(QStringLiteral("../")))
        return sourcePath;

    const QString extension = source.suffix().isEmpty()
        ? QString() : QLatin1Char('.') + source.suffix();
    for (quint64 number = 1; ; ++number)
    {
        const QString name = number == 1 ? source.fileName()
            : source.completeBaseName() + QStringLiteral("_%1").arg(number) + extension;
        const QString target = inputDir.filePath(name);
        const QFileInfo targetInfo(target);
        if (targetInfo.exists() || targetInfo.isSymLink())
            continue;
        QFile file(sourcePath);
        if (file.copy(target))
            return target;
        // Another drop may have created this name since the existence check.
        if (QFileInfo::exists(target))
            continue;
        return fail(QStringLiteral("复制到共享输入目录失败：%1\n%2")
            .arg(QDir::toNativeSeparators(path), file.errorString()));
    }
}

inline QString copyDroppedFile(const QString &path, QWidget *parent)
{
    QString error;
    const QString copied = copyToInputDirectory(path, &error);
    if (copied.isEmpty())
        QMessageBox::warning(parent, QStringLiteral("导入文件失败"), error);
    return copied;
}
}
