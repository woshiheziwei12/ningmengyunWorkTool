#pragma once

#include <QUrl>
#include <QVector>

// Document names and links that travel with a copy of the application.
class WecomDocumentCatalog
{
public:
    struct Entry {
        QString name;
        QUrl url;
    };

    // With no overrides, checks beside the application and then the user data directory.
    explicit WecomDocumentCatalog(const QString &path = {}, const QString &userPath = {});
    // Loads the catalog and retains the built-in document if the file cannot be read.
    QString load();
    const QVector<Entry> &entries() const { return m_entries; }
    // Looks up a saved link by its document ID, including its share parameters.
    QUrl urlForId(const QString &documentId) const;
    // Saves a binding atomically. Replacing an existing binding requires confirmation.
    bool save(const Entry &entry, bool replaceExisting, QString *error);

private:
    QString existingPath() const;
    bool readEntries(const QString &path, QVector<Entry> &entries, QString *error) const;
    bool saveAt(const QString &path, const Entry &entry, const QString &id, bool replaceExisting, QString *error);
    QString m_path;
    QString m_userPath;
    QVector<Entry> m_entries;
};
