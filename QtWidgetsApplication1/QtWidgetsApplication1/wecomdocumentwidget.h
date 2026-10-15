#pragma once
#include <QWidget>
#include "Common/WecomDocumentData.h"
#include "Common/WecomLocalServer.h"
class QDialog;
class QLineEdit;
class QLabel;
class QPushButton;
class QTabWidget;
class WecomDocumentReader;
class WecomBrowser;

class WecomDocumentWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WecomDocumentWidget(QWidget *parent = nullptr);
    ~WecomDocumentWidget() override;
    void readForAssistant(const QDate &fromDate, WecomLocalServer::Reply reply);
    void setAssistantStatus(const QString &message);
signals:
    void backToMain();
private:
    void clearResults();
    void showSnapshot(const WecomDocument::Snapshot &snapshot);
    void exportJson();
    void finishAssistant(const QJsonObject &response);
    WecomLocalServer::Reply m_assistantReply;
    QLabel *m_assistantStatus;
    QLineEdit *m_url;
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
