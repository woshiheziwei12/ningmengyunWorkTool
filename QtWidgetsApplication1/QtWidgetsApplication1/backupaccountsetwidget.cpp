#include "backupaccountsetwidget.h"
#include "httprequesthelper.h"
#include "lemoncofighelper.h"
#include "Common/globaldata.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QMessageBox>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariant>
#include <QDebug>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDir>
#include <QUrlQuery>
#include <QLineEdit>
#include <QSettings>
#include <QNetworkCookie>

BackupAccountSetWidget::BackupAccountSetWidget(GlobalData *globalData, QWidget *parent)
    : QWidget(parent),
      m_pGlobalData(globalData),
      m_nCompletedCount(0),
      m_pPollTimer(nullptr)
{
    setupUi();
    loadSavePath();
}

void BackupAccountSetWidget::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_pStackedWidget = new QStackedWidget(this);
    mainLayout->addWidget(m_pStackedWidget);

    setupLoadingPage();
    setupListPage();
    setupResultPage();
}

void BackupAccountSetWidget::setupLoadingPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);

    m_pLoadingLabel = new QLabel(QStringLiteral("\u6b63\u5728\u52a0\u8f7d\u8d26\u5957\u5217\u8868..."), page);
    m_pLoadingLabel->setObjectName("labelLoading");
    m_pLoadingLabel->setAlignment(Qt::AlignCenter);

    layout->addStretch();
    layout->addWidget(m_pLoadingLabel);
    layout->addStretch();

    m_pStackedWidget->addWidget(page); // index 0
}

void BackupAccountSetWidget::setupListPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    // 标题行
    QHBoxLayout *titleLayout = new QHBoxLayout();
    QLabel *titleLabel = new QLabel(QStringLiteral("\u8bf7\u9009\u62e9\u8981\u5907\u4efd\u4e0b\u8f7d\u7684\u8d26\u5957"), page);
    titleLabel->setObjectName("labelPageTitle");
    titleLayout->addWidget(titleLabel, 1);

    m_pBtnSelectAll = new QPushButton(QStringLiteral("\u5168\u9009"), page);
    m_pBtnSelectAll->setObjectName("btnSelectAll");
    m_pBtnSelectAll->setProperty("flatGreen", true);
    m_pBtnSelectAll->setCheckable(true);
    m_pBtnSelectAll->setMaximumWidth(60);
    titleLayout->addWidget(m_pBtnSelectAll);
    layout->addLayout(titleLayout);

    // 账套表格
    m_pAccountTable = new QTableWidget(page);
    m_pAccountTable->setObjectName("accountTable");
    m_pAccountTable->setColumnCount(5);
    m_pAccountTable->setHorizontalHeaderLabels({
        QStringLiteral("\u9009\u62e9"),
        QStringLiteral("\u8d26\u5957\u540d\u79f0"),
        QStringLiteral("\u542f\u7528\u65e5\u671f"),
        QStringLiteral("\u4ea7\u54c1\u7c7b\u578b"),
        QStringLiteral("\u670d\u52a1\u7f16\u53f7")
    });
    m_pAccountTable->horizontalHeader()->setStretchLastSection(true);
    m_pAccountTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_pAccountTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_pAccountTable->setColumnWidth(0, 50);
    m_pAccountTable->setColumnWidth(2, 100);
    m_pAccountTable->setColumnWidth(3, 100);
    m_pAccountTable->setColumnWidth(4, 90);
    m_pAccountTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pAccountTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pAccountTable->verticalHeader()->setVisible(false);
    m_pAccountTable->verticalHeader()->setDefaultSectionSize(38);
    m_pAccountTable->horizontalHeader()->setHighlightSections(false);
    m_pAccountTable->setAlternatingRowColors(true);
    m_pAccountTable->setShowGrid(false);
    layout->addWidget(m_pAccountTable);

    // 保存路径选择行
    QHBoxLayout *savePathLayout = new QHBoxLayout();
    QLabel *savePathLabel = new QLabel(QStringLiteral("\u4fdd\u5b58\u5230:"), page);
    savePathLabel->setMinimumWidth(50);
    m_pEditSavePath = new QLineEdit(page);
    m_pEditSavePath->setObjectName("editSavePath");
    m_pEditSavePath->setReadOnly(true);
    m_pBtnBrowse = new QPushButton(QStringLiteral("\u6d4f\u89c8..."), page);
    m_pBtnBrowse->setObjectName("btnBrowse");
    m_pBtnBrowse->setMaximumWidth(70);
    m_pBtnBrowse->setMinimumHeight(30);
    savePathLayout->addWidget(savePathLabel);
    savePathLayout->addWidget(m_pEditSavePath);
    savePathLayout->addWidget(m_pBtnBrowse);
    layout->addLayout(savePathLayout);

    // 底部按钮
    QHBoxLayout *btnLayout = new QHBoxLayout();
    m_pBtnBack = new QPushButton(QStringLiteral("\u8fd4\u56de"), page);
    m_pBtnBack->setObjectName("btnBackToMain");
    m_pBtnBack->setProperty("grayButton", true);
    m_pBtnBack->setMaximumWidth(100);
    m_pBtnBack->setMinimumHeight(36);

    m_pBtnBackup = new QPushButton(QStringLiteral("\u5907\u4efd\u4e0b\u8f7d"), page);
    m_pBtnBackup->setObjectName("btnBackupSelected");
    m_pBtnBackup->setMaximumWidth(140);
    m_pBtnBackup->setMinimumHeight(36);

    btnLayout->addWidget(m_pBtnBack);
    btnLayout->addStretch();
    btnLayout->addWidget(m_pBtnBackup);
    layout->addLayout(btnLayout);

    connect(m_pBtnSelectAll, &QPushButton::toggled, this, [this](bool checked) {
        onSelectAllChanged(checked ? Qt::Checked : Qt::Unchecked);
    });
    connect(m_pBtnBackup, &QPushButton::clicked, this, &BackupAccountSetWidget::onBackupBtnClicked);
    connect(m_pBtnBack, &QPushButton::clicked, this, &BackupAccountSetWidget::onBackClicked);
    connect(m_pBtnBrowse, &QPushButton::clicked, this, &BackupAccountSetWidget::onBrowseSavePath);

    m_pStackedWidget->addWidget(page); // index 1
}

void BackupAccountSetWidget::setupResultPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    QLabel *titleLabel = new QLabel(QStringLiteral("\u5907\u4efd\u4e0b\u8f7d\u7ed3\u679c"), page);
    titleLabel->setObjectName("labelPageTitle");
    layout->addWidget(titleLabel);

    m_pResultTable = new QTableWidget(page);
    m_pResultTable->setObjectName("resultTable");
    m_pResultTable->setColumnCount(3);
    m_pResultTable->setHorizontalHeaderLabels({
        QStringLiteral("\u8d26\u5957\u540d\u79f0"),
        QStringLiteral("\u7ed3\u679c"),
        QStringLiteral("\u8bf4\u660e")
    });
    m_pResultTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_pResultTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_pResultTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_pResultTable->setColumnWidth(1, 80);
    m_pResultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pResultTable->verticalHeader()->setVisible(false);
    m_pResultTable->verticalHeader()->setDefaultSectionSize(38);
    m_pResultTable->horizontalHeader()->setHighlightSections(false);
    m_pResultTable->setAlternatingRowColors(true);
    m_pResultTable->setShowGrid(false);
    layout->addWidget(m_pResultTable);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    m_pBtnResultBack = new QPushButton(QStringLiteral("\u8fd4\u56de\u529f\u80fd\u9009\u62e9"), page);
    m_pBtnResultBack->setObjectName("btnResultBack");
    m_pBtnResultBack->setMaximumWidth(140);
    m_pBtnResultBack->setMinimumHeight(36);
    btnLayout->addStretch();
    btnLayout->addWidget(m_pBtnResultBack);
    layout->addLayout(btnLayout);

    connect(m_pBtnResultBack, &QPushButton::clicked, this, &BackupAccountSetWidget::onBackClicked);

    m_pStackedWidget->addWidget(page); // index 2
}

void BackupAccountSetWidget::showLoading()
{
    m_pStackedWidget->setCurrentIndex(0);
}

void BackupAccountSetWidget::showList()
{
    m_pStackedWidget->setCurrentIndex(1);
}

void BackupAccountSetWidget::showResult()
{
    setProperty("sessionBusyCloudTask", false);
    m_pStackedWidget->setCurrentIndex(2);
}

QString BackupAccountSetWidget::apiHost() const
{
    return lemoncofighelper::JproApiHost();
}

QString BackupAccountSetWidget::buildCookieString() const
{
    return buildCookieStringForPlan(m_pGlobalData->m_nServiceId);
}

// ======================== 加载账套列表 ========================

void BackupAccountSetWidget::loadAccountSetList()
{
    showLoading();
    m_pLoadingLabel->setText(QStringLiteral("\u6b63\u5728\u5237\u65b0\u8d26\u5957\u4fe1\u606f..."));
    m_accountSetList.clear();
    m_pAccountTable->setRowCount(0);

    // 先刷新 appasid，再加载账套列表
    refreshAppAsId();
}

void BackupAccountSetWidget::refreshAppAsId()
{
    lemoncofighelper configHelper;
    QString wbgateUrl = configHelper.GetAccountUrlByKey("wbgateMenuUrl");
    if (wbgateUrl.isEmpty())
    {
        m_pLoadingLabel->setText(QStringLiteral("\u914d\u7f6e\u9519\u8bef\uff1awbgateMenuUrl \u672a\u914d\u7f6e"));
        return;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(wbgateUrl));
    CHttpRequestHelper::configureSsl(request);
    CHttpRequestHelper::setWbgateBrowserHeaders(request);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    QString cookie = "ningmengcookie=" + m_pGlobalData->m_strNingMengCookie;
    request.setRawHeader("Cookie", cookie.toUtf8());

    QNetworkReply *reply = CHttpRequestHelper::networkManager()->get(request);
    CHttpRequestHelper::handleSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onRefreshAppAsIdFinished(reply);
    });
}

void BackupAccountSetWidget::onRefreshAppAsIdFinished(QNetworkReply *reply)
{
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    bool hasServiceParams = false;

    m_pGlobalData->m_nServiceId = 0;
    m_pGlobalData->m_strAppAsId.clear();

    if (statusCode == 302 || statusCode == 301)
    {
        QString location = QString::fromUtf8(reply->rawHeader("Location"));
        QUrl redirectUrl(location);
        QUrlQuery query(redirectUrl);

        QString serviceId = query.queryItemValue("serviceID");
        QString appAsId = query.queryItemValue("appasid");

        if (!serviceId.isEmpty())
            m_pGlobalData->m_nServiceId = serviceId.toInt();
        if (!appAsId.isEmpty())
            m_pGlobalData->m_strAppAsId = appAsId;

        qDebug() << QStringLiteral("\u5237\u65b0appasid\u6210\u529f:") << m_pGlobalData->m_strAppAsId
                 << "serviceId:" << m_pGlobalData->m_nServiceId;
        hasServiceParams = m_pGlobalData->m_nServiceId != 0 && !m_pGlobalData->m_strAppAsId.isEmpty();
    }
    else
    {
        qDebug() << QStringLiteral("\u5237\u65b0appasid\u672a\u83b7\u53d6\u5230\u91cd\u5b9a\u5411\uff0cHTTP\u72b6\u6001\u7801:") << statusCode;
    }

    reply->deleteLater();

    // 始终通过plan列表获取所有匹配的plan
    fetchPlanServiceParams();
}

void BackupAccountSetWidget::fetchPlanServiceParams()
{
    lemoncofighelper configHelper;
    QString planUrl = configHelper.GetAccountUrlByKey(lemoncofighelper::kWbgatePlanUrlKey);
    if (planUrl.isEmpty())
    {
        m_pLoadingLabel->setText(QStringLiteral("配置错误：wbgatePlanUrl 未配置"));
        return;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(planUrl));
    CHttpRequestHelper::configureSsl(request);
    CHttpRequestHelper::setWbgateBrowserHeaders(request);
    request.setRawHeader("Authorization", ("Bearer " + m_pGlobalData->m_userToken.m_strAccessToken).toUtf8());
    request.setRawHeader("Cookie", ("ningmengcookie=" + m_pGlobalData->m_strNingMengCookie).toUtf8());

    QNetworkReply *reply = CHttpRequestHelper::networkManager()->get(request);
    CHttpRequestHelper::handleSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onFetchPlanFinished(reply);
    });
}

void BackupAccountSetWidget::onFetchPlanFinished(QNetworkReply *reply)
{
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonParseError jsonErr;
    QJsonDocument doc = QJsonDocument::fromJson(data, &jsonErr);
    if (statusCode != 200 || jsonErr.error != QJsonParseError::NoError || !doc.isObject())
    {
        m_pLoadingLabel->setText(QStringLiteral("\u83b7\u53d6\u5de5\u4f5c\u53f0\u8d26\u5957\u4fe1\u606f\u5931\u8d25"));
        return;
    }

    QJsonArray plans = doc.object()["data"].toObject()["data"].toArray();
    QList<MatchedPlan> preferredPlans;
    QList<MatchedPlan> fallbackPlans;
    m_matchedPlans.clear();

    for (const QJsonValue &value : plans)
    {
        QJsonObject plan = value.toObject();
        if (plan["productType"].toString() != "acc")
            continue;
        if ((plan.contains("isValid") && !plan["isValid"].toBool())
            || plan["expired"].toBool()
            || plan["Closed"].toBool())
            continue;

        int serviceId = plan["id"].toInt();
        QString appAsId = plan["lastAccountSet"].toObject()["appAsid"].toString();
        if (serviceId == 0 || appAsId.isEmpty())
            continue;

        int administrator = plan["administrator"].toInt();

        MatchedPlan mp;
        mp.serviceId = serviceId;
        mp.appAsId = appAsId;
        mp.productTypeDescription = plan["productTypeDescription"].toString();

        if (m_pGlobalData->m_nUserId != 0
            && administrator != 0
            && administrator != m_pGlobalData->m_nUserId)
            fallbackPlans.append(mp);
        else
            preferredPlans.append(mp);
    }

    m_matchedPlans = preferredPlans.isEmpty() ? fallbackPlans : preferredPlans;

    if (m_matchedPlans.isEmpty())
    {
        m_pLoadingLabel->setText(QStringLiteral("\u672a\u83b7\u53d6\u5230\u8d26\u5957\u670d\u52a1\u7f16\u53f7\uff0c\u8bf7\u786e\u8ba4\u8d26\u53f7\u662f\u5426\u6709\u67e0\u6aac\u4e91\u8d22\u52a1\u8d26\u5957"));
        return;
    }

    // 使用第一个匹配plan设置全局参数
    m_pGlobalData->m_nServiceId = m_matchedPlans[0].serviceId;
    m_pGlobalData->m_strAppAsId = m_matchedPlans[0].appAsId;

    doLoadAccountSetList();
}

void BackupAccountSetWidget::doLoadAccountSetList()
{
    m_pLoadingLabel->setText(QStringLiteral("\u6b63\u5728\u52a0\u8f7d\u8d26\u5957\u5217\u8868..."));

    QString host = apiHost();
    if (host.isEmpty())
    {
        m_pLoadingLabel->setText(QStringLiteral("\u914d\u7f6e\u9519\u8bef\uff1ajproApiHost \u672a\u914d\u7f6e"));
        return;
    }

    m_nPlanFetchCompleted = 0;

    for (int i = 0; i < m_matchedPlans.size(); ++i)
    {
        const MatchedPlan &mp = m_matchedPlans[i];

        QString url = host + "/api/AccountSetOnlyAuth/TopInfoV2"
                      "?serviceId=" + QString::number(mp.serviceId) +
                      "&appasid=" + mp.appAsId;

        QNetworkRequest request;
        request.setUrl(QUrl(url));
        CHttpRequestHelper::configureSsl(request);
        CHttpRequestHelper::setJproApiHeaders(request);
        request.setRawHeader("appasid", mp.appAsId.toUtf8());
        request.setRawHeader("Cookie", buildCookieStringForPlan(mp.serviceId).toUtf8());

        QNetworkReply *reply = CHttpRequestHelper::networkManager()->get(request);
        CHttpRequestHelper::handleSslErrors(reply);
        connect(reply, &QNetworkReply::finished, this, [this, reply, i]() {
            onFetchListFinished(reply, i);
        });
    }
}

void BackupAccountSetWidget::onFetchListFinished(QNetworkReply *reply, int planIndex)
{
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() == QNetworkReply::NoError && statusCode == 200)
    {
        QByteArray data = reply->readAll();
        QJsonParseError jsonErr;
        QJsonDocument doc = QJsonDocument::fromJson(data, &jsonErr);

        if (jsonErr.error == QJsonParseError::NoError && doc.isObject())
        {
            QJsonObject root = doc.object();
            if (root["state"].toInt() == 1000)
            {
                const MatchedPlan &mp = m_matchedPlans[planIndex];
                QJsonArray arr = root["data"].toObject()["accountList"].toArray();
                for (int i = 0; i < arr.size(); ++i)
                {
                    QJsonObject obj = arr[i].toObject();
                    BackupAccountSetInfo info;
                    info.asId = obj["asid"].toInt();
                    info.asName = obj["asname"].toString();
                    info.appAsId = obj["appAsId"].toString();
                    info.asStartDate = QString::number(obj["asStartYear"].toInt()) + "-"
                                     + QString::number(obj["asStartMonth"].toInt()).rightJustified(2, '0');
                    info.serviceId = mp.serviceId;
                    info.productTypeDescription = mp.productTypeDescription;
                    m_accountSetList.append(info);
                }
            }
        }
    }
    else
    {
        qDebug() << QStringLiteral("\u83b7\u53d6\u8d26\u5957\u5217\u8868\u5931\u8d25, planIndex:") << planIndex
                 << "statusCode:" << statusCode;
    }

    reply->deleteLater();
    m_nPlanFetchCompleted++;

    if (m_nPlanFetchCompleted >= m_matchedPlans.size())
    {
        if (m_accountSetList.isEmpty())
        {
            m_pLoadingLabel->setText(QStringLiteral("\u672a\u67e5\u8be2\u5230\u8d26\u5957\u4fe1\u606f"));
            return;
        }
        populateTable();
    }
}

void BackupAccountSetWidget::populateTable()
{
    m_pAccountTable->setRowCount(m_accountSetList.size());
    for (int i = 0; i < m_accountSetList.size(); ++i)
    {
        const BackupAccountSetInfo &info = m_accountSetList[i];

        QWidget *checkWidget = new QWidget(m_pAccountTable);
        QHBoxLayout *checkLayout = new QHBoxLayout(checkWidget);
        checkLayout->setContentsMargins(0, 0, 0, 0);
        checkLayout->setAlignment(Qt::AlignCenter);
        QCheckBox *checkBox = new QCheckBox(checkWidget);
        checkBox->setObjectName(QString("check_%1").arg(i));
        checkLayout->addWidget(checkBox);
        m_pAccountTable->setCellWidget(i, 0, checkWidget);

        m_pAccountTable->setItem(i, 1, new QTableWidgetItem(info.asName));
        m_pAccountTable->setItem(i, 2, new QTableWidgetItem(info.asStartDate));
        m_pAccountTable->setItem(i, 3, new QTableWidgetItem(info.productTypeDescription));
        m_pAccountTable->setItem(i, 4, new QTableWidgetItem(QString::number(info.serviceId)));
    }

    showList();
}

QString BackupAccountSetWidget::buildCookieStringForPlan(int serviceId) const
{
    return lemoncofighelper::BuildJproCookie(m_pGlobalData->m_strNingMengCookie,
                                             serviceId,
                                             m_pGlobalData->m_nUserId,
                                             m_pGlobalData->m_strAuthName);
}

// ======================== 全选 ========================

void BackupAccountSetWidget::onSelectAllChanged(int state)
{
    bool checked = (state == Qt::Checked);
    for (int i = 0; i < m_pAccountTable->rowCount(); ++i)
    {
        QWidget *w = m_pAccountTable->cellWidget(i, 0);
        if (w)
        {
            QCheckBox *cb = w->findChild<QCheckBox *>();
            if (cb) cb->setChecked(checked);
        }
    }
    m_pBtnSelectAll->setText(checked ? QStringLiteral("\u53d6\u6d88\u5168\u9009") : QStringLiteral("\u5168\u9009"));
}

// ======================== 开始备份 ========================

void BackupAccountSetWidget::onBackupBtnClicked()
{
    m_backupTasks.clear();
    m_backupResults.clear();
    m_nCompletedCount = 0;

    for (int i = 0; i < m_pAccountTable->rowCount(); ++i)
    {
        QWidget *w = m_pAccountTable->cellWidget(i, 0);
        if (!w) continue;
        QCheckBox *cb = w->findChild<QCheckBox *>();
        if (cb && cb->isChecked())
        {
            BackupTask task;
            task.asId = m_accountSetList[i].asId;
            task.asName = m_accountSetList[i].asName;
            task.appAsId = m_accountSetList[i].appAsId;
            task.serviceId = m_accountSetList[i].serviceId;
            task.progress = 0;
            task.finished = false;
            m_backupTasks.append(task);
        }
    }

    if (m_backupTasks.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("\u63d0\u793a"),
                             QStringLiteral("\u8bf7\u5148\u9009\u62e9\u8981\u5907\u4efd\u4e0b\u8f7d\u7684\u8d26\u5957"));
        return;
    }

    // 预填充结果列表，与任务索引一一对应
    m_backupResults.clear();
    for (int i = 0; i < m_backupTasks.size(); ++i)
    {
        BackupResult res;
        res.asName = m_backupTasks[i].asName;
        res.success = false;
        res.message = QStringLiteral("\u5904\u7406\u4e2d...");
        m_backupResults.append(res);
    }

    setProperty("sessionBusyCloudTask", true);
    m_pBtnBackup->setEnabled(false);
    m_pBtnBackup->setText(QStringLiteral("\u5907\u4efd\u4e2d(0/%1)...").arg(m_backupTasks.size()));

    // 并行启动所有备份任务
    for (int i = 0; i < m_backupTasks.size(); ++i)
    {
        startBackupTask(i);
    }
}

void BackupAccountSetWidget::startBackupTask(int taskIndex)
{
    BackupTask &task = m_backupTasks[taskIndex];

    QString host = apiHost();
    QString url = host + "/api/Backup/ProcessBackup"
                  "?allowTemporaryFile=1&isProcess=1"
                  "&appasid=" + task.appAsId;

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    CHttpRequestHelper::configureSsl(request);
    CHttpRequestHelper::setJproApiHeaders(request);
    request.setRawHeader("appasid", task.appAsId.toUtf8());
    request.setRawHeader("Cookie", buildCookieStringForPlan(task.serviceId).toUtf8());

    QNetworkReply *reply = CHttpRequestHelper::networkManager()->post(request, QByteArray("{}"));
    CHttpRequestHelper::handleSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, taskIndex]() {
        onBackupStartFinished(reply, taskIndex);
    });
}

void BackupAccountSetWidget::onTaskFinished(int taskIndex)
{
    m_backupTasks[taskIndex].finished = true;
    m_nCompletedCount++;

    m_pBtnBackup->setText(QStringLiteral("\u5907\u4efd\u4e2d(%1/%2)...")
                          .arg(m_nCompletedCount).arg(m_backupTasks.size()));

    if (m_nCompletedCount >= m_backupTasks.size())
    {
        // 全部任务完成，显示结果
        m_pBtnBackup->setEnabled(true);
        m_pBtnBackup->setText(QStringLiteral("\u5907\u4efd\u4e0b\u8f7d"));

        m_pResultTable->setRowCount(m_backupResults.size());
        for (int i = 0; i < m_backupResults.size(); ++i)
        {
            const BackupResult &res = m_backupResults[i];
            m_pResultTable->setItem(i, 0, new QTableWidgetItem(res.asName));

            QTableWidgetItem *statusItem = new QTableWidgetItem(
                res.success ? QStringLiteral("\u6210\u529f") : QStringLiteral("\u5931\u8d25"));
            statusItem->setForeground(res.success ? QColor(110, 224, 177) : QColor(255, 139, 162));
            m_pResultTable->setItem(i, 1, statusItem);

            m_pResultTable->setItem(i, 2, new QTableWidgetItem(res.message));
        }

        showResult();
    }
}

void BackupAccountSetWidget::onBackupStartFinished(QNetworkReply *reply, int taskIndex)
{
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError || statusCode != 200)
    {
        m_backupResults[taskIndex].success = false;
        m_backupResults[taskIndex].message = QStringLiteral("\u53d1\u8d77\u5907\u4efd\u5931\u8d25: ") + reply->errorString();
        reply->deleteLater();
        onTaskFinished(taskIndex);
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject root = doc.object();
    int state = root["state"].toInt();

    if (state != 1000)
    {
        m_backupResults[taskIndex].success = false;
        m_backupResults[taskIndex].message = QStringLiteral("\u5907\u4efd\u63a5\u53e3\u8fd4\u56de\u9519\u8bef: ") + root["msg"].toString();
        onTaskFinished(taskIndex);
        return;
    }

    // 取出 fileName
    QJsonObject dataObj = root["data"].toObject();
    m_backupTasks[taskIndex].fileName = dataObj["fileName"].toString();

    qDebug() << QStringLiteral("\u5907\u4efd\u5df2\u53d1\u8d77:") << m_backupTasks[taskIndex].asName
             << "fileName:" << m_backupTasks[taskIndex].fileName;

    // 第二步：轮询进度
    pollProgress(taskIndex);
}

// ======================== 轮询备份进度 ========================

void BackupAccountSetWidget::pollProgress(int taskIndex)
{
    const BackupTask &task = m_backupTasks[taskIndex];

    QString host = apiHost();
    QString encodedFileName = QUrl::toPercentEncoding(task.fileName);
    QString url = host + "/api/Backup/GetFileRecord"
                  "?filename=" + encodedFileName +
                  "&appasid=" + task.appAsId;

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    CHttpRequestHelper::configureSsl(request);
    CHttpRequestHelper::setJproApiHeaders(request);
    request.setRawHeader("appasid", task.appAsId.toUtf8());
    request.setRawHeader("Cookie", buildCookieStringForPlan(task.serviceId).toUtf8());

    QNetworkReply *reply = CHttpRequestHelper::networkManager()->post(request, QByteArray("{}"));
    CHttpRequestHelper::handleSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, taskIndex]() {
        onPollProgressFinished(reply, taskIndex);
    });
}

void BackupAccountSetWidget::onPollProgressFinished(QNetworkReply *reply, int taskIndex)
{
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError || statusCode != 200)
    {
        m_backupResults[taskIndex].success = false;
        m_backupResults[taskIndex].message = QStringLiteral("\u67e5\u8be2\u5907\u4efd\u8fdb\u5ea6\u5931\u8d25: ") + reply->errorString();
        reply->deleteLater();
        onTaskFinished(taskIndex);
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject root = doc.object();
    QJsonObject dataObj = root["data"].toObject();
    int progress = dataObj["progress"].toInt();

    m_backupTasks[taskIndex].progress = progress;

    qDebug() << m_backupTasks[taskIndex].asName << "progress:" << progress;

    if (progress >= 100)
    {
        // 备份完成，开始下载
        downloadFile(taskIndex);
    }
    else
    {
        // 继续轮询，3秒后再查
        QTimer::singleShot(3000, this, [this, taskIndex]() {
            pollProgress(taskIndex);
        });
    }
}

// ======================== 下载文件 ========================

void BackupAccountSetWidget::downloadFile(int taskIndex)
{
    const BackupTask &task = m_backupTasks[taskIndex];

    QString host = apiHost();
    QString encodedFileName = QUrl::toPercentEncoding(task.fileName);
    QString url = host + "/api/Backup/DownloadFile"
                  "?fileName=" + encodedFileName +
                  "&fileType=0"
                  "&appasid=" + task.appAsId;

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    CHttpRequestHelper::configureSsl(request);
    CHttpRequestHelper::setJproApiHeaders(request);
    request.setRawHeader("Cookie", buildCookieStringForPlan(task.serviceId).toUtf8());
    // 手动处理 302 重定向
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);

    QNetworkReply *reply = CHttpRequestHelper::networkManager()->get(request);
    CHttpRequestHelper::handleSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, taskIndex]() {
        onDownloadRedirectFinished(reply, taskIndex);
    });
}

void BackupAccountSetWidget::onDownloadRedirectFinished(QNetworkReply *reply, int taskIndex)
{
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (statusCode == 302 || statusCode == 301)
    {
        // 跟随重定向到真实下载地址
        QString location = QString::fromUtf8(reply->rawHeader("Location"));
        reply->deleteLater();

        if (location.isEmpty())
        {
            m_backupResults[taskIndex].success = false;
            m_backupResults[taskIndex].message = QStringLiteral("\u4e0b\u8f7d\u91cd\u5b9a\u5411\u5730\u5740\u4e3a\u7a7a");
            onTaskFinished(taskIndex);
            return;
        }

        qDebug() << QStringLiteral("\u4e0b\u8f7d\u91cd\u5b9a\u5411\u5230:") << location;

        QNetworkRequest dlRequest;
        dlRequest.setUrl(QUrl(location));
        CHttpRequestHelper::configureSsl(dlRequest);
        CHttpRequestHelper::setJproApiHeaders(dlRequest);
        dlRequest.setRawHeader("Cookie", buildCookieStringForPlan(m_backupTasks[taskIndex].serviceId).toUtf8());

        QNetworkReply *dlReply = CHttpRequestHelper::networkManager()->get(dlRequest);
        CHttpRequestHelper::handleSslErrors(dlReply);
        connect(dlReply, &QNetworkReply::finished, this, [this, dlReply, taskIndex]() {
            onDownloadFileFinished(dlReply, taskIndex);
        });
    }
    else if (statusCode == 200)
    {
        // 直接返回文件内容（未重定向的情况）
        onDownloadFileFinished(reply, taskIndex);
    }
    else
    {
        m_backupResults[taskIndex].success = false;
        m_backupResults[taskIndex].message = QStringLiteral("\u4e0b\u8f7d\u5931\u8d25\uff0cHTTP\u72b6\u6001\u7801: ") + QString::number(statusCode);
        reply->deleteLater();
        onTaskFinished(taskIndex);
    }
}

void BackupAccountSetWidget::onDownloadFileFinished(QNetworkReply *reply, int taskIndex)
{
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError || statusCode != 200)
    {
        m_backupResults[taskIndex].success = false;
        m_backupResults[taskIndex].message = QStringLiteral("\u4e0b\u8f7d\u6587\u4ef6\u5931\u8d25: ") + reply->errorString();
        reply->deleteLater();
        onTaskFinished(taskIndex);
        return;
    }

    QByteArray fileData = reply->readAll();
    reply->deleteLater();

    // 保存到用户选择的目录
    QString saveDirPath = savePath();
    QDir saveDir(saveDirPath);
    if (!saveDir.exists())
        saveDir.mkpath(saveDirPath);

    QString localFileName = m_backupTasks[taskIndex].fileName;
    QString localFilePath = saveDirPath + "/" + localFileName;

    QFile file(localFilePath);

    if (file.open(QIODevice::WriteOnly))
    {
        file.write(fileData);
        file.close();
        m_backupResults[taskIndex].success = true;
        m_backupResults[taskIndex].message = QStringLiteral("\u5df2\u4fdd\u5b58\u5230: ") + localFilePath;
        m_backupResults[taskIndex].localFilePath = localFilePath;
        qDebug() << QStringLiteral("\u4e0b\u8f7d\u5b8c\u6210:") << localFilePath << "size:" << fileData.size();
    }
    else
    {
        m_backupResults[taskIndex].success = false;
        m_backupResults[taskIndex].message = QStringLiteral("\u4fdd\u5b58\u6587\u4ef6\u5931\u8d25: ") + file.errorString();
    }

    onTaskFinished(taskIndex);
}

// ======================== 保存路径管理 ========================

QString BackupAccountSetWidget::savePath() const
{
    return m_pEditSavePath->text();
}

void BackupAccountSetWidget::loadSavePath()
{
    QSettings settings(qApp->applicationDirPath() + "/LemonBackup.ini", QSettings::IniFormat);
    QString saved = settings.value("backup/savePath").toString();

    if (saved.isEmpty() || !QDir(saved).exists())
    {
        saved = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (saved.isEmpty())
            saved = QDir::homePath() + "/Downloads";
    }

    m_pEditSavePath->setText(QDir::toNativeSeparators(saved));
}

void BackupAccountSetWidget::persistSavePath()
{
    QSettings settings(qApp->applicationDirPath() + "/LemonBackup.ini", QSettings::IniFormat);
    settings.setValue("backup/savePath", QDir::fromNativeSeparators(m_pEditSavePath->text()));
}

void BackupAccountSetWidget::onBrowseSavePath()
{
    QString current = m_pEditSavePath->text();
    QString dir = QFileDialog::getExistingDirectory(this,
        QStringLiteral("\u9009\u62e9\u5907\u4efd\u4e0b\u8f7d\u4fdd\u5b58\u76ee\u5f55"), current);
    if (!dir.isEmpty())
    {
        m_pEditSavePath->setText(QDir::toNativeSeparators(dir));
        persistSavePath();
    }
}

// ======================== 返回 ========================

void BackupAccountSetWidget::onBackClicked()
{
    emit backToMain();
}
