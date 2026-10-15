#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QUrl>
#include <functional>
#include <memory>

// An isolated official-site browser session, with a current-user encrypted cookie backup.
// This is the transport seam used by the reusable reader; it is not a DOM scraper.
class WecomBrowser : public QWidget
{
    Q_OBJECT
public:
    using Reply = std::function<void(const QJsonObject &, const QString &)>;
    using SessionReply = std::function<void(const QString &error)>;
    explicit WecomBrowser(QWidget *parent = nullptr, const QString &profileDirectory = QString());
    ~WecomBrowser() override;
    virtual void navigate(const QUrl &url);
    virtual void get(const QUrl &url, Reply reply);
    virtual void cancelRequests();
    virtual void stop();
    virtual bool hasSavedSession() const;
    virtual void saveSession(SessionReply reply);
signals:
    void bootstrapReceived(const QUrl &requestUrl, const QJsonObject &response);
    void browserError(const QString &message);
    void navigationFinished(bool success);
    void sessionStatus(const QString &message);
protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
    void initialize();
    void syncBounds();
    void restoreSession();
};
