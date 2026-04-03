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
    layout->setContentsMargins(20, 15, 20, 15);
    layout->setSpacing(10);

    // 标题行
    QHBoxLayout *titleLayout = new QHBoxLayout();
    QLabel *titleLabel = new QLabel(QStringLiteral("\u8bf7\u9009\u62e9\u8981\u5220\u9664\u7684\u8d26\u5957"), page);
    titleLabel->setObjectName("labelPageTitle");
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();

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
    m_pAccountTable->setColumnCount(4);
    m_pAccountTable->setHorizontalHeaderLabels({
        QStringLiteral("\u9009\u62e9"),
        QStringLiteral("\u8d26\u5957\u540d\u79f0"),
        QStringLiteral("\u542f\u7528\u65e5\u671f"),
        QStringLiteral("\u5f53\u524d\u671f\u95f4")
    });
    m_pAccountTable->horizontalHeader()->setStretchLastSection(true);
    m_pAccountTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_pAccountTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_pAccountTable->setColumnWidth(0, 50);
    m_pAccountTable->setColumnWidth(2, 120);
    m_pAccountTable->setColumnWidth(3, 120);
    m_pAccountTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pAccountTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pAccountTable->verticalHeader()->setVisible(false);
    m_pAccountTable->setAlternatingRowColors(true);
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
    layout->setContentsMargins(20, 15, 20, 15);
    layout->setSpacing(10);

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
    m_pResultTable->setAlternatingRowColors(true);
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
    m_pStackedWidget->setCurrentIndex(2);
}

QString DeleteAccountSetWidget::apiHost() const
{
    lemoncofighelper configHelper;
    return configHelper.GetAccountUrlByKey("jproApiHost");
}

QString DeleteAccountSetWidget::buildCookieString() const
{
    QString cookie;
    cookie += "ningmengcookie=" + m_pGlobalData->m_strNingMengCookie;
    cookie += "; service-id=" + QString::number(m_pGlobalData->m_nServiceId);
    return cookie;
}

void DeleteAccountSetWidget::loadAccountSetList()
{
    showLoading();
    m_pLoadingLabel->setText(QStringLiteral("\u6b63\u5728\u52a0\u8f7d\u8d26\u5957\u5217\u8868..."));
    m_accountSetList.clear();
    m_pAccountTable->setRowCount(0);

    QString host = apiHost();
    if (host.isEmpty())
    {
        m_pLoadingLabel->setText(QStringLiteral("\u914d\u7f6e\u9519\u8bef\uff1ajproApiHost \u672a\u914d\u7f6e"));
        return;
    }

    QString url = host + "/api/AccountSet/FullListV2"
                  "?searchText="
                  "&serviceId=" + QString::number(m_pGlobalData->m_nServiceId) +
                  "&appasid=" + m_pGlobalData->m_strAppAsId;

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    CHttpRequestHelper::configureSsl(request);
    CHttpRequestHelper::setJproApiHeaders(request);
    request.setRawHeader("appasid", m_pGlobalData->m_strAppAsId.toUtf8());
    request.setRawHeader("Cookie", buildCookieString().toUtf8());

    QNetworkReply *reply = CHttpRequestHelper::networkManager()->get(request);
    CHttpRequestHelper::handleSslErrors(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onFetchListFinished(reply);
    });
}

void DeleteAccountSetWidget::onFetchListFinished(QNetworkReply *reply)
{
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError || statusCode != 200)
    {
        QString errMsg = reply->errorString();
        qDebug() << QStringLiteral("\u83b7\u53d6\u8d26\u5957\u5217\u8868\u5931\u8d25:") << errMsg << "statusCode:" << statusCode;
        m_pLoadingLabel->setText(QStringLiteral("\u52a0\u8f7d\u5931\u8d25\uff1a") + errMsg);
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonParseError jsonErr;
    QJsonDocument doc = QJsonDocument::fromJson(data, &jsonErr);
    if (jsonErr.error != QJsonParseError::NoError || !doc.isObject())
    {
        m_pLoadingLabel->setText(QStringLiteral("\u89e3\u6790\u54cd\u5e94\u5931\u8d25"));
        return;
    }

    QJsonObject root = doc.object();
    int state = root["state"].toInt();
    if (state != 1000)
    {
        m_pLoadingLabel->setText(QStringLiteral("\u63a5\u53e3\u8fd4\u56de\u9519\u8bef\uff0cstate=") + QString::number(state));
        return;
    }

    QJsonArray arr = root["data"].toArray();
    for (int i = 0; i < arr.size(); ++i)
    {
        QJsonObject obj = arr[i].toObject();
        AccountSetInfo info;
        info.asId = obj["asId"].toInt();
        info.asName = obj["asName"].toString();
        info.appAsId = obj["appAsId"].toString();
        info.asStartDate = obj["asStartYear"].toString() + "-" + obj["asStartMonth"].toString().rightJustified(2, '0');
        info.asCurrentPeriod = obj["asCurrentYear"].toString() + "-" + obj["asCurrentMonth"].toString().rightJustified(2, '0');
        m_accountSetList.append(info);
    }

    // 填充表格
    m_pAccountTable->setRowCount(m_accountSetList.size());
    for (int i = 0; i < m_accountSetList.size(); ++i)
    {
        const AccountSetInfo &info = m_accountSetList[i];

        // 选择列 - 使用居中的 checkbox
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
        m_pAccountTable->setItem(i, 3, new QTableWidgetItem(info.asCurrentPeriod));
    }

    showList();
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
            statusItem->setForeground(res.success ? QColor(1, 194, 87) : QColor(255, 77, 79));
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
                  "&serviceid=" + QString::number(m_pGlobalData->m_nServiceId) +
                  "&appasid=" + task.appAsId;

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    CHttpRequestHelper::configureSsl(request);
    CHttpRequestHelper::setJproApiHeaders(request);
    request.setRawHeader("appasid", task.appAsId.toUtf8());
    request.setRawHeader("Cookie", buildCookieString().toUtf8());

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
