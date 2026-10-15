#include "deleteaccountsetwidget.h"
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
#include <QUrlQuery>
#include <QNetworkCookie>

DeleteAccountSetWidget::DeleteAccountSetWidget(GlobalData *globalData, QWidget *parent)
    : QWidget(parent),
      m_pGlobalData(globalData),
      m_nCurrentDeleteIndex(0)
{
    setupUi();
}

void DeleteAccountSetWidget::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_pStackedWidget = new QStackedWidget(this);
    mainLayout->addWidget(m_pStackedWidget);

    setupLoadingPage();
    setupListPage();
    setupResultPage();
}

void DeleteAccountSetWidget::setupLoadingPage()
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

void DeleteAccountSetWidget::setupListPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    // 标题行
    QHBoxLayout *titleLayout = new QHBoxLayout();
    QLabel *titleLabel = new QLabel(QStringLiteral("\u8bf7\u9009\u62e9\u8981\u5220\u9664\u7684\u8d26\u5957"), page);
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

    // 底部按钮
    QHBoxLayout *btnLayout = new QHBoxLayout();
    m_pBtnBack = new QPushButton(QStringLiteral("\u8fd4\u56de"), page);
    m_pBtnBack->setObjectName("btnBackToMain");
    m_pBtnBack->setProperty("grayButton", true);
    m_pBtnBack->setMaximumWidth(100);
    m_pBtnBack->setMinimumHeight(36);

    m_pBtnDelete = new QPushButton(QStringLiteral("\u5220\u9664\u9009\u4e2d\u8d26\u5957"), page);
    m_pBtnDelete->setObjectName("btnDeleteSelected");
    m_pBtnDelete->setMaximumWidth(140);
    m_pBtnDelete->setMinimumHeight(36);

    btnLayout->addWidget(m_pBtnBack);
    btnLayout->addStretch();
    btnLayout->addWidget(m_pBtnDelete);
    layout->addLayout(btnLayout);

    connect(m_pBtnSelectAll, &QPushButton::toggled, this, [this](bool checked) {
        onSelectAllChanged(checked ? Qt::Checked : Qt::Unchecked);
    });
    connect(m_pBtnDelete, &QPushButton::clicked, this, &DeleteAccountSetWidget::onDeleteBtnClicked);
    connect(m_pBtnBack, &QPushButton::clicked, this, &DeleteAccountSetWidget::onBackClicked);

    m_pStackedWidget->addWidget(page); // index 1
}

void DeleteAccountSetWidget::setupResultPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    QLabel *titleLabel = new QLabel(QStringLiteral("\u5220\u9664\u7ed3\u679c"), page);
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

    connect(m_pBtnResultBack, &QPushButton::clicked, this, &DeleteAccountSetWidget::onBackClicked);

    m_pStackedWidget->addWidget(page); // index 2
}

void DeleteAccountSetWidget::showLoading()
{
    m_pStackedWidget->setCurrentIndex(0);
}

void DeleteAccountSetWidget::showList()
{
    m_pStackedWidget->setCurrentIndex(1);
}

void DeleteAccountSetWidget::showResult()
{
    setProperty("sessionBusyCloudTask", false);
    m_pStackedWidget->setCurrentIndex(2);
}

QString DeleteAccountSetWidget::apiHost() const
{
    return lemoncofighelper::JproApiHost();
}

QString DeleteAccountSetWidget::buildCookieString() const
{
    return buildCookieStringForPlan(m_pGlobalData->m_nServiceId);
}

void DeleteAccountSetWidget::loadAccountSetList()
{
    showLoading();
    m_pLoadingLabel->setText(QStringLiteral("\u6b63\u5728\u5237\u65b0\u8d26\u5957\u4fe1\u606f..."));
    m_accountSetList.clear();
    m_pAccountTable->setRowCount(0);

    // 先刷新 appasid，再加载账套列表
    refreshAppAsId();
}

void DeleteAccountSetWidget::refreshAppAsId()
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

void DeleteAccountSetWidget::onRefreshAppAsIdFinished(QNetworkReply *reply)
{
    reply->deleteLater();

    // 始终通过plan列表获取所有匹配的plan
    fetchPlanServiceParams();
}

void DeleteAccountSetWidget::fetchPlanServiceParams()
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

void DeleteAccountSetWidget::onFetchPlanFinished(QNetworkReply *reply)
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

    m_pGlobalData->m_nServiceId = m_matchedPlans[0].serviceId;
    m_pGlobalData->m_strAppAsId = m_matchedPlans[0].appAsId;

    doLoadAccountSetList();
}

void DeleteAccountSetWidget::doLoadAccountSetList()
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

void DeleteAccountSetWidget::onFetchListFinished(QNetworkReply *reply, int planIndex)
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
                    AccountSetInfo info;
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

void DeleteAccountSetWidget::populateTable()
{
    m_pAccountTable->setRowCount(m_accountSetList.size());
    for (int i = 0; i < m_accountSetList.size(); ++i)
    {
        const AccountSetInfo &info = m_accountSetList[i];

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

QString DeleteAccountSetWidget::buildCookieStringForPlan(int serviceId) const
{
    return lemoncofighelper::BuildJproCookie(m_pGlobalData->m_strNingMengCookie,
                                             serviceId,
                                             m_pGlobalData->m_nUserId,
                                             m_pGlobalData->m_strAuthName);
}

void DeleteAccountSetWidget::onSelectAllChanged(int state)
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

void DeleteAccountSetWidget::onDeleteBtnClicked()
{
    // 收集选中项
    m_deleteTasks.clear();
    m_deleteResults.clear();
    m_nCurrentDeleteIndex = 0;

    for (int i = 0; i < m_pAccountTable->rowCount(); ++i)
    {
        QWidget *w = m_pAccountTable->cellWidget(i, 0);
        if (!w) continue;
        QCheckBox *cb = w->findChild<QCheckBox *>();
        if (cb && cb->isChecked())
        {
            DeleteTask task;
            task.asId = m_accountSetList[i].asId;
            task.asName = m_accountSetList[i].asName;
            task.appAsId = m_accountSetList[i].appAsId;
            task.serviceId = m_accountSetList[i].serviceId;
            m_deleteTasks.append(task);
        }
    }

    if (m_deleteTasks.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("\u63d0\u793a"), QStringLiteral("\u8bf7\u5148\u9009\u62e9\u8981\u5220\u9664\u7684\u8d26\u5957"));
        return;
    }

    int ret = QMessageBox::question(this, QStringLiteral("\u786e\u8ba4\u5220\u9664"),
        QStringLiteral("\u786e\u5b9a\u8981\u5220\u9664\u9009\u4e2d\u7684 %1 \u4e2a\u8d26\u5957\u5417\uff1f\n\u5220\u9664\u540e\u8d26\u5957\u5c06\u653e\u5165\u56de\u6536\u7ad9\u3002")
            .arg(m_deleteTasks.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (ret != QMessageBox::Yes) return;

    setProperty("sessionBusyCloudTask", true);
    m_pBtnDelete->setEnabled(false);
    m_pBtnDelete->setText(QStringLiteral("\u5220\u9664\u4e2d..."));
    deleteNextAccountSet();
}

void DeleteAccountSetWidget::deleteNextAccountSet()
{
    if (m_nCurrentDeleteIndex >= m_deleteTasks.size())
    {
        // 全部完成，显示结果
        m_pBtnDelete->setEnabled(true);
        m_pBtnDelete->setText(QStringLiteral("\u5220\u9664\u9009\u4e2d\u8d26\u5957"));

        // 填充结果表
        m_pResultTable->setRowCount(m_deleteResults.size());
        for (int i = 0; i < m_deleteResults.size(); ++i)
        {
            const DeleteResult &res = m_deleteResults[i];
            m_pResultTable->setItem(i, 0, new QTableWidgetItem(res.asName));

            QTableWidgetItem *statusItem = new QTableWidgetItem(
                res.success ? QStringLiteral("\u6210\u529f") : QStringLiteral("\u5931\u8d25"));
            statusItem->setForeground(res.success ? QColor(110, 224, 177) : QColor(255, 139, 162));
            m_pResultTable->setItem(i, 1, statusItem);

            m_pResultTable->setItem(i, 2, new QTableWidgetItem(res.message));
        }

        showResult();
        return;
    }

    const DeleteTask &task = m_deleteTasks[m_nCurrentDeleteIndex];

    QString host = apiHost();
    QString url = host + "/api/AccountSetOnlyAuth/DropAccountSetToRecycleBinV2"
                  "?asId=" + QString::number(task.asId) +
                  "&serviceid=" + QString::number(task.serviceId) +
                  "&appasid=" + task.appAsId;

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    CHttpRequestHelper::configureSsl(request);
    CHttpRequestHelper::setJproApiHeaders(request);
    request.setRawHeader("appasid", task.appAsId.toUtf8());
    request.setRawHeader("Cookie", buildCookieStringForPlan(task.serviceId).toUtf8());

    QNetworkReply *reply = CHttpRequestHelper::networkManager()->post(request, QByteArray());
    CHttpRequestHelper::handleSslErrors(reply);

    int asId = task.asId;
    QString asName = task.asName;
    connect(reply, &QNetworkReply::finished, this, [this, reply, asId, asName]() {
        onDeleteOneFinished(reply, asId, asName);
    });
}

void DeleteAccountSetWidget::onDeleteOneFinished(QNetworkReply *reply, int asId, const QString &asName)
{
    DeleteResult result;
    result.asName = asName;

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError || statusCode != 200)
    {
        result.success = false;
        result.message = reply->errorString();
        qDebug() << QStringLiteral("\u5220\u9664\u8d26\u5957\u5931\u8d25:") << asId << asName << result.message;
    }
    else
    {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject root = doc.object();
        int state = root["state"].toInt();
        bool dataVal = root["data"].toBool();

        if (state == 1000 && dataVal)
        {
            result.success = true;
            result.message = QStringLiteral("\u5220\u9664\u6210\u529f");
        }
        else
        {
            result.success = false;
            result.message = root["msg"].toString();
            if (result.message.isEmpty())
                result.message = QStringLiteral("\u63a5\u53e3\u8fd4\u56de\u5931\u8d25\uff0cstate=") + QString::number(state);
        }
    }

    reply->deleteLater();
    m_deleteResults.append(result);

    m_nCurrentDeleteIndex++;
    m_pBtnDelete->setText(QStringLiteral("\u5220\u9664\u4e2d(%1/%2)...")
                          .arg(m_nCurrentDeleteIndex).arg(m_deleteTasks.size()));
    deleteNextAccountSet();
}

void DeleteAccountSetWidget::onBackClicked()
{
    emit backToMain();
}
