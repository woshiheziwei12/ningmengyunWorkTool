#ifndef DELETEACCOUNTSETWIDGET_H
#define DELETEACCOUNTSETWIDGET_H

#include <QWidget>
#include <QList>

class GlobalData;
class QStackedWidget;
class QTableWidget;
class QLabel;
class QPushButton;
class QNetworkReply;

struct AccountSetInfo
{
    int asId;
    QString asName;
    QString appAsId;
    QString asStartDate;
    QString asCurrentPeriod;
};

class DeleteAccountSetWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DeleteAccountSetWidget(GlobalData *globalData, QWidget *parent = nullptr);

    void loadAccountSetList();

signals:
    void backToMain();

private slots:
    void onFetchListFinished(QNetworkReply *reply);
    void onDeleteBtnClicked();
    void onSelectAllChanged(int state);
    void onBackClicked();

private:
    void setupUi();
    void setupLoadingPage();
    void setupListPage();
    void setupResultPage();

    void showLoading();
    void showList();
    void showResult();

    void deleteNextAccountSet();
    void onDeleteOneFinished(QNetworkReply *reply, int asId, const QString &asName);

    QString buildCookieString() const;
    QString apiHost() const;

    GlobalData *m_pGlobalData;
    QStackedWidget *m_pStackedWidget;

    // 加载页
    QLabel *m_pLoadingLabel;

    // 列表页
    QTableWidget *m_pAccountTable;
    QPushButton *m_pBtnDelete;
    QPushButton *m_pBtnBack;
    QPushButton *m_pBtnSelectAll;

    // 结果页
    QTableWidget *m_pResultTable;
    QPushButton *m_pBtnResultBack;

    QList<AccountSetInfo> m_accountSetList;

    // 删除进度追踪
    struct DeleteTask
    {
        int asId;
        QString asName;
        QString appAsId;
    };
    QList<DeleteTask> m_deleteTasks;
    int m_nCurrentDeleteIndex;

    struct DeleteResult
    {
        QString asName;
        bool success;
        QString message;
    };
    QList<DeleteResult> m_deleteResults;
};

#endif // DELETEACCOUNTSETWIDGET_H
