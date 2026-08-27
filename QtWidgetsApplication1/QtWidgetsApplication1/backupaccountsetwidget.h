#ifndef BACKUPACCOUNTSETWIDGET_H
#define BACKUPACCOUNTSETWIDGET_H

#include <QWidget>
#include <QList>
#include <QTimer>

class GlobalData;
class QStackedWidget;
class QTableWidget;
class QLabel;
class QPushButton;
class QLineEdit;
class QNetworkReply;
class QProgressBar;

struct BackupAccountSetInfo
{
    int asId;
    QString asName;
    QString appAsId;
    QString asStartDate;
    int serviceId;
    QString productTypeDescription;
};

class BackupAccountSetWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BackupAccountSetWidget(GlobalData *globalData, QWidget *parent = nullptr);

    void loadAccountSetList();

signals:
    void backToMain();

private slots:
    void onBackupBtnClicked();
    void onSelectAllChanged(int state);
    void onBackClicked();
    void onBrowseSavePath();

private:
    void setupUi();
    void setupLoadingPage();
    void setupListPage();
    void setupResultPage();

    void showLoading();
    void showList();
    void showResult();

    void startBackupTask(int taskIndex);
    void onTaskFinished(int taskIndex);
    void onBackupStartFinished(QNetworkReply *reply, int taskIndex);
    void pollProgress(int taskIndex);
    void onPollProgressFinished(QNetworkReply *reply, int taskIndex);
    void downloadFile(int taskIndex);
    void onDownloadRedirectFinished(QNetworkReply *reply, int taskIndex);
    void onDownloadFileFinished(QNetworkReply *reply, int taskIndex);

    void refreshAppAsId();
    void onRefreshAppAsIdFinished(QNetworkReply *reply);
    void fetchPlanServiceParams();
    void onFetchPlanFinished(QNetworkReply *reply);
    void doLoadAccountSetList();
    void onFetchListFinished(QNetworkReply *reply, int planIndex);
    void populateTable();

    QString buildCookieString() const;
    QString buildCookieStringForPlan(int serviceId) const;
    QString apiHost() const;
    QString savePath() const;
    void loadSavePath();
    void persistSavePath();

    GlobalData *m_pGlobalData;
    QStackedWidget *m_pStackedWidget;

    // 加载页
    QLabel *m_pLoadingLabel;

    // 列表页
    QTableWidget *m_pAccountTable;
    QPushButton *m_pBtnBackup;
    QPushButton *m_pBtnBack;
    QPushButton *m_pBtnSelectAll;
    QLineEdit *m_pEditSavePath;
    QPushButton *m_pBtnBrowse;

    // 结果页
    QTableWidget *m_pResultTable;
    QPushButton *m_pBtnResultBack;

    QList<BackupAccountSetInfo> m_accountSetList;

    // 匹配的plan列表（用于遍历查询账套）
    struct MatchedPlan
    {
        int serviceId;
        QString appAsId;
        QString productTypeDescription;
    };
    QList<MatchedPlan> m_matchedPlans;
    int m_nPlanFetchCompleted;

    // 备份任务
    struct BackupTask
    {
        int asId;
        QString asName;
        QString appAsId;
        int serviceId;
        QString fileName;    // 备份生成的文件名
        int progress;        // 备份进度 0~100
        bool finished;       // 该任务是否已完成（成功或失败）
    };
    QList<BackupTask> m_backupTasks;
    int m_nCompletedCount;   // 已完成（含失败）的任务数

    struct BackupResult
    {
        QString asName;
        bool success;
        QString message;
        QString localFilePath;
    };
    QList<BackupResult> m_backupResults;

    QTimer *m_pPollTimer;
};

#endif // BACKUPACCOUNTSETWIDGET_H
