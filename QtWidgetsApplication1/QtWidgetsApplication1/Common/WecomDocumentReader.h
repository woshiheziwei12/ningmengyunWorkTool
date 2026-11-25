#pragma once
#include "WecomDocumentData.h"
#include <QObject>
#include <QTimer>
class WecomBrowser;

// Async data API. Pass an isolated browser transport once; no dependency on the tool page.
class WecomDocumentReader : public QObject
{
    Q_OBJECT
public:
    enum class LoginPrompt { Default, AfterPageLoad };
    explicit WecomDocumentReader(WecomBrowser *browser, QObject *parent = nullptr);
    bool isBusy() const { return m_busy; }
    void signIn(const QUrl &documentUrl);
    void signIn(const WecomDocument::Document &document);
    void read(const QUrl &documentUrl, const QDate &fromDate = QDate::currentDate(),
              LoginPrompt prompt = LoginPrompt::Default);
    void read(const WecomDocument::Document &document, const QDate &fromDate = QDate::currentDate(),
              LoginPrompt prompt = LoginPrompt::Default);
    void cancel();
signals:
    void loginRequired();
    void authenticated();
    void signInCompleted(const QString &sessionError);
    void busyChanged(bool busy);
    void progress(const QString &message);
    void failed(const QString &message);
    void finished(const WecomDocument::Snapshot &snapshot);
private:
    void begin(const WecomDocument::Document &document, bool readAfterLogin, const QDate &date, LoginPrompt prompt);
    void acceptMetadata(const QJsonObject &response);
    void readNextSheet(int retry = 0);
    void fail(const QString &message);
    void setBusy(bool busy);
    void finish();
    WecomBrowser *m_browser;
    QTimer m_loginTimeout;
    bool m_busy = false;
    bool m_waitingBootstrap = false;
    bool m_readAfterLogin = false;
    bool m_deferLoginPrompt = false;
    bool m_asyncPolling = false;
    int m_generation = 0;
    int m_sheetIndex = 0;
    WecomDocument::Document m_document;
    QUrl m_openRequest;
    WecomDocument::Metadata m_metadata;
    WecomDocument::Snapshot m_snapshot;
};
Q_DECLARE_METATYPE(WecomDocument::Snapshot)
