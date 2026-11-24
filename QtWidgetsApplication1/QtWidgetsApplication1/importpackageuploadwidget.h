#ifndef IMPORTPACKAGEUPLOADWIDGET_H
#define IMPORTPACKAGEUPLOADWIDGET_H

#include <QDateTime>
#include <QList>
#include <QString>
#include <QWidget>
#include "Common/LocalToolServer.h"

class GlobalData;
class QComboBox;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QLabel;
class QNetworkReply;
class QPushButton;
class QTextEdit;
class QTimer;
struct ImportPackageUploadRequest;

class ImportPackageUploadWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ImportPackageUploadWidget(GlobalData *globalData, QWidget *parent = nullptr);
    ~ImportPackageUploadWidget();
    void reloadImportHistory();
    // Handles a CLI request with the workbench's current session and environment.
    void handleLocalRequest(const QJsonObject &request, LocalToolServer::Reply reply);

signals:
    void backToMain();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onBrowseClicked();
    void onBackClicked();
    void onQueryResultClicked();
    void onStopQueryClicked();
    void pollImportResult();

private:
    struct ImportHistoryRecord
    {
        QString importId;
        QDateTime uploadTime;
        QString fileName;
    };

    void setupUi();
    void startUpload(const QString &zipPath);
    void beginUpload(const ImportPackageUploadRequest &request, LocalToolServer::Reply reply = {});
    void startResultQuery(const QString &importId, const QString &host, LocalToolServer::Reply reply = {});
    void setRunning(bool running, const QString &message = QString());
    void setQueryRunning(bool running, const QString &message = QString());
    void loadImportHistory();
    void refreshImportHistoryCombo();
    void selectSuccessfulUpload(const QString &importId);
    QString selectedImportId() const;
    QString selectedImporterApiHost() const;
    QString buildImportResultUrl(QString *errorMessage) const;
    void onResultReplyFinished(QNetworkReply *reply, int querySerial);
    void handleImportResultResponse(const QString &importId, const QByteArray &body);
    void scheduleNextResultPoll(const QString &message);
    void finishResultQuery(const QString &message, const QString &resultText = {}, const QJsonObject &response = {});
    void appendLog(const QString &message);

    GlobalData *m_pGlobalData;
    QLabel *m_pDropLabel;
    QLabel *m_pStatusLabel;
    QComboBox *m_pImporterHostCombo;
    QComboBox *m_pImportIdCombo;
    QTextEdit *m_pLogEdit;
    QPushButton *m_pBtnBrowse;
    QPushButton *m_pBtnQueryResult;
    QPushButton *m_pBtnStopQuery;
    QPushButton *m_pBtnBack;
    int m_requestId;
    int m_querySerial;
    bool m_resultQueryRunning;
    QString m_currentQueryImportId;
    QString m_currentQueryImporterApiHost;
    QTimer *m_pResultPollTimer;
    QNetworkReply *m_pResultReply;
    LocalToolServer::Reply m_localQueryReply;
    QList<ImportHistoryRecord> m_importHistory;
};

#endif // IMPORTPACKAGEUPLOADWIDGET_H
