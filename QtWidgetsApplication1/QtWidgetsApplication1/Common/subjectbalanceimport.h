#pragma once
#include <functional>

#include <QDate>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QStringList>
#include <QTimer>

struct BalanceImportFileSlot
{
    QString key, label, notes;
    bool required = false;
    QStringList extensions;
};

struct BalanceImportSoftware
{
    int id = -1;
    QString name;
    QList<BalanceImportFileSlot> files;
    bool supportsAuxiliaryFiles = false;
    QString notes;
    QList<int> accountingStandards;
};

struct BalanceImportSession
{
    QString host, token, owner;
    int serviceId = 0;
};

struct BalanceImportInput
{
    int software = -1;
    QString accountName;
    QDate startMonth;
    int accountingStandard = 0, taxType = 0;
    bool importZero = false;
    QMap<QString, QString> files;
};

struct BalanceImportOutcome
{
    // -1: invalid/unknown, 0: processing, 1: success, 2: partial, 3: failure.
    int status = -1;
    QString error, warnings, startMonth, accountId;
};

namespace SubjectBalanceImport
{
QList<BalanceImportSoftware> softwareCatalog(QString *error = nullptr);
QString validate(const BalanceImportInput &input, const BalanceImportSession &session);
QUrl submitUrl(const BalanceImportInput &input, const QString &host, int serviceId);
BalanceImportOutcome parseOutcome(const QByteArray &body, const QString &expectedId);
}

// Owns one submit/query operation. Stopping queries never sends a cancellation to the server.
class SubjectBalanceImportClient : public QObject
{
    Q_OBJECT
public:
    explicit SubjectBalanceImportClient(QObject *parent = nullptr);
    ~SubjectBalanceImportClient();
    bool busy() const;
    void setTokenProvider(std::function<QString()> provider) { m_tokenProvider = std::move(provider); }
    void submit(const BalanceImportInput &input, const BalanceImportSession &session);
    void query(const QString &importId, const BalanceImportSession &session);
    void stopQuery();

signals:
    void accepted(const QString &importId);
    void progress(const QString &message);
    void outcome(int status, const QString &error, const QString &warnings,
                 const QString &startMonth, const QString &accountId);
    void failed(const QString &message, bool submissionUncertain);
    void busyChanged(bool busy);

private:
    void poll();
    void watch(QNetworkReply *reply, bool submission);
    void finishError(const QString &message, bool uncertain = false);
    void setBusy(bool busy);
    QNetworkRequest request(const QUrl &url) const;

    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_pollTimer;
    BalanceImportSession m_session;
    std::function<QString()> m_tokenProvider;
    QString m_importId;
    bool m_busy = false, m_submitting = false;
};
