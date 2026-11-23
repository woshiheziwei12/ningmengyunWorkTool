#include "WecomDocumentCatalog.h"
#include "LemonWorkToolPaths.h"
#include "WecomDocumentData.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <utility>

namespace {
constexpr int CatalogVersion = 1;

// Retains access to the original document even if the catalog is missing.
WecomDocumentCatalog::Entry defaultEntry()
{
    return {QStringLiteral("2026-运营支持组项目进度表"), WecomDocument::defaultUrl()};
}
}

WecomDocumentCatalog::WecomDocumentCatalog(const QString &path, const QString &userPath)
    : m_path(path.isEmpty() ? QCoreApplication::applicationDirPath() + "/WecomDocuments.json" : path),
      m_userPath(userPath),
      m_entries{defaultEntry()}
{
    if (path.isEmpty() && m_userPath.isEmpty()) {
        const auto directory = LemonWorkToolPaths::userDataDirectory();
        if (!directory.isEmpty()) m_userPath = QDir(directory).filePath("WecomDocuments.json");
    }
}

QString WecomDocumentCatalog::load()
{
    QString error;
    QVector<Entry> entries;
    if (readEntries(existingPath(), entries, &error)) m_entries = std::move(entries);
    return error;
}

QUrl WecomDocumentCatalog::urlForId(const QString &documentId) const
{
    for (const auto &entry : m_entries) {
        if (WecomDocument::documentIdFromUrl(entry.url) == documentId) return entry.url;
    }
    return {};
}

QString WecomDocumentCatalog::existingPath() const
{
    if (QFileInfo::exists(m_path)) return m_path;
    if (QFileInfo::exists(m_userPath)) return m_userPath;
    return {};
}

bool WecomDocumentCatalog::readEntries(const QString &path, QVector<Entry> &entries, QString *error) const
{
    if (!QFileInfo::exists(path)) {
        entries.append(defaultEntry());
        return true;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取当前表格清单 WecomDocuments.json。");
        return false;
    }
    const auto catalog = QJsonDocument::fromJson(file.readAll()).object();
    const auto documents = catalog.value("documents");
    if (catalog.value("version") != QJsonValue(CatalogVersion) || !documents.isArray()) {
        *error = QStringLiteral("WecomDocuments.json 格式无效或版本不受支持。");
        return false;
    }

    QSet<QString> ids;
    for (const auto &value : documents.toArray()) {
        const auto item = value.toObject();
        Entry entry{item.value("name").toString().trimmed(), QUrl(item.value("url").toString(), QUrl::StrictMode)};
        const auto id = WecomDocument::documentIdFromUrl(entry.url);
        if (entry.name.isEmpty() || id.isEmpty() || ids.contains(id)) {
            *error = QStringLiteral("WecomDocuments.json 含无效或重复的文档绑定。");
            return false;
        }
        ids.insert(id);
        entries.append(std::move(entry));
    }
    if (entries.isEmpty()) entries.append(defaultEntry());
    return true;
}

bool WecomDocumentCatalog::save(const Entry &entry, bool replaceExisting, QString *error)
{
    const QString id = WecomDocument::documentIdFromUrl(entry.url);
    if (entry.name.trimmed().isEmpty() || id.isEmpty()) {
        *error = QStringLiteral("请填写表格名称和有效的企业微信表格链接。");
        return false;
    }
    const QString path = existingPath();
    if (!path.isEmpty()) return saveAt(path, entry, id, replaceExisting, error);
    if (saveAt(m_path, entry, id, replaceExisting, error)) return true;
    if (m_userPath.isEmpty() || QFileInfo::exists(m_path)) return false;
    return saveAt(m_userPath, entry, id, replaceExisting, error);
}

bool WecomDocumentCatalog::saveAt(const QString &path, const Entry &entry, const QString &id, bool replaceExisting, QString *error)
{
    error->clear();
    QLockFile lock(path + ".lock");
    if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !lock.tryLock()) {
        *error = QStringLiteral("无法锁定表格清单，请确认配置目录可写且没有其他实例正在编辑。");
        return false;
    }
    QVector<Entry> entries;
    if (!readEntries(path, entries, error)) return false;
    auto saved = std::find_if(entries.begin(), entries.end(), [&id](const Entry &item) {
        return WecomDocument::documentIdFromUrl(item.url) == id;
    });
    if (saved == entries.end()) {
        entries.append({entry.name.trimmed(), entry.url});
    } else if (replaceExisting) {
        *saved = {entry.name.trimmed(), entry.url};
    } else {
        *error = QStringLiteral("这张表格已在列表中。请重新打开文档页面后再更新该绑定。");
        return false;
    }

    QJsonArray documents;
    for (const auto &item : entries) {
        documents.append(QJsonObject{{"name", item.name}, {"url", item.url.toString(QUrl::FullyEncoded)}});
    }
    const QJsonDocument catalog(QJsonObject{{"version", CatalogVersion}, {"documents", documents}});
    const QByteArray data = catalog.toJson();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        *error = QStringLiteral("无法保存表格清单，请确认配置目录可写。");
        return false;
    }
    m_entries = std::move(entries);
    return true;
}
