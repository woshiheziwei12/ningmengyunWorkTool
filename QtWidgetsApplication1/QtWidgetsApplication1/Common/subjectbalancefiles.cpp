#include "subjectbalancefiles.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace {
QString classify(const QString &baseName)
{
    const QString name = baseName.toLower();
    const QList<QPair<QString, QString>> patterns = {
        {"trialbalance", QStringLiteral("余额|科目期初|期初表|trial[ _-]*balance|account[ _-]*balance")},
        {"voucher", QStringLiteral("记账凭证|会计凭证|凭证导入模板|凭证列表|凭证数据|凭证表|凭证|序时账|会计分录|voucher|journal")},
        {"subject", QStringLiteral("会计科目表|会计科目|科目数据|科目表|科目|chart[ _-]*of[ _-]*accounts|subjects?")}
    };
    for (const auto &pattern : patterns) {
        const QRegularExpression regex(pattern.second);
        if (!regex.match(name).hasMatch()) continue;
        return pattern.first;
    }
    return {};
}

QString normalizedPath(const QString &path)
{
    const auto info = QFileInfo(path);
    const auto canonical = info.canonicalFilePath();
    return (canonical.isEmpty() ? info.absoluteFilePath() : canonical).toLower();
}
}

BalanceFileSuggestions suggestBalanceFiles(const BalanceImportSoftware &software,
                                           const QString &selectedFile,
                                           const QMap<QString, QString> &existing)
{
    BalanceFileSuggestions result;
    const QFileInfo selected(selectedFile);
    if (!selected.isFile()) return result;
    QSet<QString> used;
    for (const auto &path : existing) if (!path.isEmpty()) used.insert(normalizedPath(path));
    used.insert(normalizedPath(selectedFile));
    const auto siblings = selected.dir().entryInfoList(QDir::Files | QDir::Readable | QDir::NoSymLinks, QDir::Name);
    for (const auto &file : siblings) {
        if (file.fileName().startsWith("~$") || file.size() <= 0 || used.contains(normalizedPath(file.filePath()))) continue;
        const auto role = classify(file.completeBaseName());
        if (role.isEmpty() || !existing.value(role).isEmpty()) continue;
        for (const auto &slot : software.files) {
            if (slot.key != role || !slot.extensions.contains(file.suffix().toLower())) continue;
            result.candidates[role].append(file.absoluteFilePath());
            break;
        }
    }
    for (auto it = result.candidates.constBegin(); it != result.candidates.constEnd(); ++it)
        if (!it.value().isEmpty())
            result.matches.insert(it.key(), it.value().first());
    return result;
}
