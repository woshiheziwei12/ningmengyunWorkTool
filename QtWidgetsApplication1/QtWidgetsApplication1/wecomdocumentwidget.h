#pragma once
#include <QWidget>
#include "Common/WecomDocumentCatalog.h"
#include "Common/WecomDocumentData.h"
#include "Common/WecomLocalServer.h"
class QComboBox;
class QDialog;
class QLabel;
class QPushButton;
class QTabWidget;
class WecomDocumentReader;
class WecomBrowser;

class WecomDocumentWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WecomDocumentWidget(QWidget *parent = nullptr, const QString &catalogPath = {});
    ~WecomDocumentWidget() override;
    // Lists saved document IDs and names without exposing their share parameters.
    QJsonObject assistantDocuments() const;
    // Reads the pinned document, a saved document, or a temporary URL.
    void readForAssistant(const QDate &fromDate, const QString &documentId, const QUrl &url, WecomLocalServer::Reply reply);
    void setAssistantStatus(const QString &message);
signals:
    void backToMain();
private:
    // Rebuilds the saved choices and selects a newly saved link when supplied.
    void populateDocuments(const QUrl &selected = {});
    // Prompts for a temporary visit or saves a named binding.
    void openDocumentDialog(bool saveToCatalog);
    // Adds an in-memory choice and starts loading it using the regular reader.
    void accessTemporaryDocument(const QUrl &url);
    QUrl selectedDocumentUrl() const;
    WecomDocument::Document selectedDocument() const;
    void clearResults();
    void showSnapshot(const WecomDocument::Snapshot &snapshot);
    void exportJson();
    void finishAssistant(const QJsonObject &response);
    WecomLocalServer::Reply m_assistantReply;
    WecomDocumentCatalog m_catalog;
    int m_temporaryIndex = -1;
    bool m_showReadInPage = true;
    QLabel *m_assistantStatus;
    QComboBox *m_documents;
    QPushButton *m_manualAccess;
    QPushButton *m_addDocument;
    QLabel *m_status;
    QPushButton *m_login;
    QPushButton *m_read;
    QPushButton *m_cancel;
    QPushButton *m_export;
    QTabWidget *m_tabs;
    QDialog *m_loginDialog;
    WecomBrowser *m_browser;
    WecomDocumentReader *m_reader;
    WecomDocument::Snapshot m_snapshot;
};
