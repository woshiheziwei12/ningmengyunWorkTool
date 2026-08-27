#include "importpackageuploadwidget.h"
#include "Common/globaldata.h"
#include "Common/importpackagehistory.h"
#include "Common/importpackageuploader.h"
#include "Common/logger.h"
#include "httprequesthelper.h"
#include "lemoncofighelper.h"

#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSet>
#include <QSharedPointer>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
bool isZipFile(const QString &filePath)
{
    const QFileInfo info(filePath);
    return info.isFile() && info.suffix().compare("zip", Qt::CaseInsensitive) == 0;
}

bool hasZipFile(const QMimeData *mimeData)
{
    if (!mimeData || !mimeData->hasUrls())
        return false;

    for (const QUrl &url : mimeData->urls())
    {
        if (url.isLocalFile() && isZipFile(url.toLocalFile()))
            return true;
    }
    return false;
}
}

ImportPackageUploadWidget::ImportPackageUploadWidget(GlobalData *globalData, QWidget *parent)
    : QWidget(parent),
      m_pGlobalData(globalData),
      m_pDropLabel(nullptr),
      m_pStatusLabel(nullptr),
      m_pImporterHostCombo(nullptr),
      m_pImportIdCombo(nullptr),
      m_pLogEdit(nullptr),
      m_pBtnBrowse(nullptr),
      m_pBtnQueryResult(nullptr),
      m_pBtnStopQuery(nullptr),
      m_pBtnBack(nullptr),
      m_requestId(0),
      m_querySerial(0),
      m_resultQueryRunning(false),
      m_pResultPollTimer(nullptr),
      m_pResultReply(nullptr)
{
    setAcceptDrops(true);
    setupUi();
    reloadImportHistory();
}

ImportPackageUploadWidget::~ImportPackageUploadWidget()
{
    if (m_pResultPollTimer)
        m_pResultPollTimer->stop();

    if (m_pResultReply)
    {
        disconnect(m_pResultReply, nullptr, this, nullptr);
        if (!m_pResultReply->isFinished())
            m_pResultReply->abort();
        m_pResultReply->deleteLater();
        m_pResultReply = nullptr;
    }
}

void ImportPackageUploadWidget::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    QHBoxLayout *titleLayout = new QHBoxLayout();
    QLabel *titleLabel = new QLabel(QStringLiteral("上传压缩包（专业版）"), this);
    titleLabel->setObjectName("labelPageTitle");
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();

    m_pBtnBrowse = new QPushButton(QStringLiteral("选择压缩包"), this);
    m_pBtnBrowse->setObjectName("btnBrowseImportPackage");
    m_pBtnBrowse->setProperty("flatGreen", true);
    m_pBtnBrowse->setMaximumWidth(110);
    titleLayout->addWidget(m_pBtnBrowse);
    layout->addLayout(titleLayout);

    QHBoxLayout *hostLayout = new QHBoxLayout();
    hostLayout->setSpacing(8);
    QLabel *hostLabel = new QLabel(QStringLiteral("\u4e0a\u4f20/\u67e5\u8be2\u57df\u540d\uff1a"), this);
    m_pImporterHostCombo = new QComboBox(this);
    m_pImporterHostCombo->setObjectName("comboProfessionalImporterHost");
    m_pImporterHostCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    const bool productionEnvironment = lemoncofighelper::CurrentHttpEnvironment()
        .compare(QStringLiteral("Pro"), Qt::CaseInsensitive) == 0;
    if (productionEnvironment)
    {
        lemoncofighelper configHelper;
        const QString proHost = configHelper.GetAccountUrlByKey(lemoncofighelper::kProImporterApiHostKey).trimmed();
        m_pImporterHostCombo->addItem(QStringLiteral("Pro - %1").arg(proHost), proHost);
        m_pImporterHostCombo->addItem(QStringLiteral("PT - https://prodaozhangapipt.ningmengyun.com"),
            QStringLiteral("https://prodaozhangapipt.ningmengyun.com"));
        m_pImporterHostCombo->addItem(QStringLiteral("KF - https://kfprodaozhangapi.ningmengyun.com"),
            QStringLiteral("https://kfprodaozhangapi.ningmengyun.com"));
    }
    else
    {
        m_pImporterHostCombo->addItem(QStringLiteral("Dev - https://proimportertest.ningmengyun.com"),
            QStringLiteral("https://proimportertest.ningmengyun.com"));
        m_pImporterHostCombo->addItem(QStringLiteral("Dev2 - https://proimportertest2.ningmengyun.com"),
            QStringLiteral("https://proimportertest2.ningmengyun.com"));
        m_pImporterHostCombo->addItem(QStringLiteral("Dev3 - https://proimportertest3.ningmengyun.com"),
            QStringLiteral("https://proimportertest3.ningmengyun.com"));
        lemoncofighelper configHelper;
        const QString localHost = configHelper.GetAccountUrlByKey(lemoncofighelper::kProImporterApiHostKey).trimmed();
        m_pImporterHostCombo->addItem(QStringLiteral("Local - %1").arg(localHost), localHost);
        m_pImporterHostCombo->setCurrentIndex(3);
    }

    hostLayout->addWidget(hostLabel);
    hostLayout->addWidget(m_pImporterHostCombo, 1);
    layout->addLayout(hostLayout);

    m_pDropLabel = new QLabel(QStringLiteral("将专业版账套数据 ZIP 压缩包拖入此处，系统会上传并返回导入任务 ID"), this);
    m_pDropLabel->setObjectName("labelRepairDropArea");
    m_pDropLabel->setAlignment(Qt::AlignCenter);
    m_pDropLabel->setMinimumHeight(90);
    m_pDropLabel->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(m_pDropLabel);

    m_pStatusLabel = new QLabel(QStringLiteral("待上传"), this);
    m_pStatusLabel->setWordWrap(true);
    layout->addWidget(m_pStatusLabel);

    QHBoxLayout *queryLayout = new QHBoxLayout();
    queryLayout->setSpacing(8);
    QLabel *queryLabel = new QLabel(QStringLiteral("导入任务 ID："), this);
    m_pImportIdCombo = new QComboBox(this);
    m_pImportIdCombo->setEditable(true);
    m_pImportIdCombo->setInsertPolicy(QComboBox::NoInsert);
    m_pImportIdCombo->setMinimumWidth(360);
    m_pImportIdCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_pBtnQueryResult = new QPushButton(QStringLiteral("查询结果"), this);
    m_pBtnQueryResult->setObjectName("btnQueryImportResult");
    m_pBtnQueryResult->setProperty("flatGreen", true);
    m_pBtnQueryResult->setMinimumHeight(32);
    m_pBtnQueryResult->setMaximumWidth(100);

    m_pBtnStopQuery = new QPushButton(QStringLiteral("停止查询"), this);
    m_pBtnStopQuery->setObjectName("btnStopImportResultQuery");
    m_pBtnStopQuery->setProperty("grayButton", true);
    m_pBtnStopQuery->setMinimumHeight(32);
    m_pBtnStopQuery->setMaximumWidth(100);
    m_pBtnStopQuery->setEnabled(false);

    queryLayout->addWidget(queryLabel);
    queryLayout->addWidget(m_pImportIdCombo, 1);
    queryLayout->addWidget(m_pBtnQueryResult);
    queryLayout->addWidget(m_pBtnStopQuery);
    layout->addLayout(queryLayout);

    m_pLogEdit = new QTextEdit(this);
    m_pLogEdit->setObjectName("importPackageUploadLog");
    m_pLogEdit->setReadOnly(true);
    m_pLogEdit->setMinimumHeight(220);
    layout->addWidget(m_pLogEdit, 1);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    m_pBtnBack = new QPushButton(QStringLiteral("返回"), this);
    m_pBtnBack->setObjectName("btnBackToMain");
    m_pBtnBack->setProperty("grayButton", true);
    m_pBtnBack->setMaximumWidth(100);
    m_pBtnBack->setMinimumHeight(36);
    btnLayout->addWidget(m_pBtnBack);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    connect(m_pBtnBrowse, &QPushButton::clicked, this, &ImportPackageUploadWidget::onBrowseClicked);
    connect(m_pBtnQueryResult, &QPushButton::clicked, this, &ImportPackageUploadWidget::onQueryResultClicked);
    connect(m_pBtnStopQuery, &QPushButton::clicked, this, &ImportPackageUploadWidget::onStopQueryClicked);
    connect(m_pBtnBack, &QPushButton::clicked, this, &ImportPackageUploadWidget::onBackClicked);

    m_pResultPollTimer = new QTimer(this);
    m_pResultPollTimer->setInterval(10000);
    m_pResultPollTimer->setSingleShot(true);
    connect(m_pResultPollTimer, &QTimer::timeout, this, &ImportPackageUploadWidget::pollImportResult);
}

void ImportPackageUploadWidget::setRunning(bool running, const QString &message)
{
    m_pBtnBrowse->setEnabled(!running);
    m_pBtnBack->setEnabled(!running);
    m_pImporterHostCombo->setEnabled(!running && !m_resultQueryRunning);
    setAcceptDrops(!running);
    if (!message.isEmpty())
        m_pStatusLabel->setText(message);
}

void ImportPackageUploadWidget::setQueryRunning(bool running, const QString &message)
{
    m_resultQueryRunning = running;
    m_pBtnQueryResult->setEnabled(!running);
    m_pBtnStopQuery->setEnabled(running);
    m_pImportIdCombo->setEnabled(!running);
    m_pImporterHostCombo->setEnabled(!running && m_pBtnBrowse->isEnabled());
    if (!message.isEmpty())
        m_pStatusLabel->setText(message);
}

void ImportPackageUploadWidget::startUpload(const QString &zipPath)
{
    if (zipPath.isEmpty())
        return;

    if (!isZipFile(zipPath))
    {
        AppLogger::warning(QStringLiteral("上传压缩包选择了不支持的文件 path=%1").arg(QDir::toNativeSeparators(zipPath)));
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请选择 ZIP 压缩包"));
        return;
    }

    QString accessToken;
    if (m_pGlobalData)
        accessToken = m_pGlobalData->m_strNingMengCookie;

    if (accessToken.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先登录后再上传压缩包"));
        return;
    }

    const int currentServiceId = m_pGlobalData ? m_pGlobalData->m_nServiceId : 0;
    if (currentServiceId <= 0)
    {
        QMessageBox::warning(this, QStringLiteral("\u63d0\u793a"),
            QStringLiteral("\u672a\u83b7\u53d6\u5230\u5f53\u524d\u767b\u5f55\u670d\u52a1\u7f16\u53f7\uff0c\u8bf7\u91cd\u65b0\u767b\u5f55\u540e\u518d\u4e0a\u4f20\u538b\u7f29\u5305"));
        return;
    }

    const QFileInfo fileInfo(zipPath);
    ImportPackageUploadRequest uploadRequest;
    uploadRequest.asName = fileInfo.completeBaseName();
    uploadRequest.zipFilePath = fileInfo.absoluteFilePath();
    uploadRequest.zipFileName = fileInfo.completeBaseName();
    uploadRequest.accessToken = accessToken;
    uploadRequest.ningMengCookie = accessToken;
    uploadRequest.importerApiHost = selectedImporterApiHost();
    uploadRequest.serviceId = currentServiceId;
    uploadRequest.rewriteProServiceId = true;
    uploadRequest.serviceType = ImportPackageServiceType::Professional;

    const int requestId = ++m_requestId;
    setRunning(true, QStringLiteral("正在后台上传压缩包..."));
    m_pLogEdit->setPlainText(QStringLiteral("正在上传：") + QDir::toNativeSeparators(uploadRequest.zipFilePath));
    AppLogger::operation(QStringLiteral("后台上传压缩包任务启动 requestId=%1 path=%2")
        .arg(requestId)
        .arg(QDir::toNativeSeparators(uploadRequest.zipFilePath)));

    m_pLogEdit->setPlainText(QStringLiteral("\u6b63\u5728\u4e0a\u4f20\uff1a") + QDir::toNativeSeparators(uploadRequest.zipFilePath)
        + QStringLiteral("\nProServiceId\uff1a") + QString::number(currentServiceId));

    QSharedPointer<ImportPackageUploadResult> result(new ImportPackageUploadResult);
    QThread *thread = QThread::create([result, uploadRequest]() {
        *result = ImportPackageUploader::upload(uploadRequest);
    });

    connect(thread, &QThread::finished, this, [this, result, requestId, uploadRequest]() {
        if (requestId != m_requestId)
            return;

        setRunning(false);
        if (!result->ok)
        {
            AppLogger::error(QStringLiteral("专业版压缩包上传失败 requestId=%1 error=%2")
                .arg(requestId)
                .arg(result->errorMessage));
            m_pStatusLabel->setText(QStringLiteral("上传失败：") + result->errorMessage);
            m_pLogEdit->setPlainText(result->logs.join("\n") + "\n" + result->errorMessage);
            QMessageBox::warning(this, QStringLiteral("上传失败"), result->errorMessage);
            return;
        }

        recordSuccessfulUpload(uploadRequest.zipFilePath, result->importId, uploadRequest.serviceId);
        m_pStatusLabel->setText(QStringLiteral("上传成功，importId：") + result->importId);
        m_pLogEdit->setPlainText(result->logs.join("\n"));
        QMessageBox::information(this, QStringLiteral("上传成功"),
            QStringLiteral("压缩包已上传，导入任务 ID：\n") + result->importId);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ImportPackageUploadWidget::onBrowseClicked()
{
    AppLogger::operation(QStringLiteral("选择上传压缩包"));
    const QString filePath = QFileDialog::getOpenFileName(this,
        QStringLiteral("选择 ZIP 压缩包"),
        QString(),
        QStringLiteral("ZIP 压缩包 (*.zip)"));

    if (!filePath.isEmpty())
        startUpload(filePath);
}

void ImportPackageUploadWidget::onBackClicked()
{
    AppLogger::operation(QStringLiteral("上传压缩包返回主界面"));
    emit backToMain();
}

void ImportPackageUploadWidget::onQueryResultClicked()
{
    const QString importId = selectedImportId();
    if (importId.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请选择或输入导入任务 ID"));
        return;
    }

    if (!m_pGlobalData)
    {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("登录信息为空，请重新登录后再查询"));
        return;
    }

    const QString ningMengCookieToken = m_pGlobalData->m_strNingMengCookie.trimmed();
    const QString jwtToken = m_pGlobalData->m_userToken.m_strAccessToken.trimmed();
    if (ningMengCookieToken.isEmpty() && jwtToken.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先登录后再查询导账结果"));
        return;
    }

    if (m_pResultPollTimer)
        m_pResultPollTimer->stop();
    if (m_pResultReply)
    {
        disconnect(m_pResultReply, nullptr, this, nullptr);
        if (!m_pResultReply->isFinished())
            m_pResultReply->abort();
        m_pResultReply->deleteLater();
        m_pResultReply = nullptr;
    }

    m_currentQueryImportId = importId;
    m_currentQueryImporterApiHost = selectedImporterApiHost();
    setQueryRunning(true, QStringLiteral("正在查询导账结果：") + importId);
    appendLog(QStringLiteral("开始查询导账结果 importId=%1").arg(importId));
    pollImportResult();
}

void ImportPackageUploadWidget::onStopQueryClicked()
{
    ++m_querySerial;
    if (m_pResultPollTimer)
        m_pResultPollTimer->stop();
    if (m_pResultReply)
    {
        disconnect(m_pResultReply, nullptr, this, nullptr);
        if (!m_pResultReply->isFinished())
            m_pResultReply->abort();
        m_pResultReply->deleteLater();
        m_pResultReply = nullptr;
    }
    finishResultQuery(QStringLiteral("已停止查询导账结果"));
}

void ImportPackageUploadWidget::pollImportResult()
{
    if (!m_resultQueryRunning || m_currentQueryImportId.isEmpty() || m_pResultReply)
        return;

    QString errorMessage;
    const QString url = buildImportResultUrl(&errorMessage);
    if (url.isEmpty())
    {
        finishResultQuery(QStringLiteral("查询地址无效：") + errorMessage);
        return;
    }

    QJsonArray importIdsArray;
    importIdsArray.append(m_currentQueryImportId);
    const QByteArray postData = QJsonDocument(importIdsArray).toJson(QJsonDocument::Compact);

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    CHttpRequestHelper::configureSsl(request);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, "lemonClient");

    if (m_pGlobalData)
    {
        QString bearerToken = m_pGlobalData->m_strNingMengCookie.trimmed();
        if (bearerToken.isEmpty())
            bearerToken = m_pGlobalData->m_userToken.m_strAccessToken.trimmed();
        if (!bearerToken.isEmpty())
            request.setRawHeader("Authorization", (QStringLiteral("Bearer ") + bearerToken).toUtf8());

        QString cookie;
        if (!m_pGlobalData->m_strNingMengCookie.trimmed().isEmpty())
            cookie = QStringLiteral("ningmengcookie=") + m_pGlobalData->m_strNingMengCookie.trimmed();
        if (m_pGlobalData->m_nServiceId > 0)
        {
            if (!cookie.isEmpty())
                cookie += QStringLiteral("; ");
            cookie += QStringLiteral("service-id=") + QString::number(m_pGlobalData->m_nServiceId);
        }
        if (!cookie.isEmpty())
            request.setRawHeader("Cookie", cookie.toUtf8());
    }

    const int querySerial = ++m_querySerial;
    AppLogger::operation(QStringLiteral("查询导账结果 importId=%1 url=%2").arg(m_currentQueryImportId, url));
    m_pResultReply = CHttpRequestHelper::networkManager()->post(request, postData);
    CHttpRequestHelper::handleSslErrors(m_pResultReply);
    QNetworkReply *reply = m_pResultReply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, querySerial]() {
        onResultReplyFinished(reply, querySerial);
    });
}

void ImportPackageUploadWidget::loadImportHistory()
{
    m_importHistory.clear();

    QFile file(historyFilePath());
    if (!file.exists())
        return;

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        AppLogger::warning(QStringLiteral("读取导账 importId 历史失败 path=%1 error=%2")
            .arg(QDir::toNativeSeparators(file.fileName()), file.errorString()));
        return;
    }

    QJsonParseError jsonError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !document.isArray())
    {
        AppLogger::warning(QStringLiteral("解析导账 importId 历史失败 path=%1 error=%2")
            .arg(QDir::toNativeSeparators(file.fileName()), jsonError.errorString()));
        return;
    }

    QSet<QString> seenImportIds;
    const QJsonArray array = document.array();
    for (const QJsonValue &value : array)
    {
        if (!value.isObject())
            continue;

        const QJsonObject object = value.toObject();
        ImportHistoryRecord record;
        record.importId = object.value(QStringLiteral("importId")).toString().trimmed();
        if (record.importId.isEmpty() || seenImportIds.contains(record.importId))
            continue;

        record.uploadTime = QDateTime::fromString(object.value(QStringLiteral("uploadTime")).toString(), Qt::ISODate);
        if (!record.uploadTime.isValid())
            record.uploadTime = QDateTime::fromString(object.value(QStringLiteral("uploadTime")).toString(), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        record.fileName = object.value(QStringLiteral("fileName")).toString();
        record.serviceId = object.value(QStringLiteral("serviceId")).toInt();

        seenImportIds.insert(record.importId);
        m_importHistory.append(record);
        if (m_importHistory.size() >= 100)
            break;
    }
}

void ImportPackageUploadWidget::refreshImportHistoryCombo()
{
    if (!m_pImportIdCombo)
        return;

    const QString currentImportId = selectedImportId();
    m_pImportIdCombo->blockSignals(true);
    m_pImportIdCombo->clear();

    for (const ImportHistoryRecord &record : m_importHistory)
    {
        QString uploadTimeText = record.uploadTime.isValid()
            ? record.uploadTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
            : QStringLiteral("未知时间");
        QString itemText = QStringLiteral("%1 | %2").arg(record.importId, uploadTimeText);
        if (!record.fileName.trimmed().isEmpty())
            itemText += QStringLiteral(" | ") + record.fileName.trimmed();
        m_pImportIdCombo->addItem(itemText, record.importId);
    }

    const int index = m_pImportIdCombo->findData(currentImportId);
    if (index >= 0)
        m_pImportIdCombo->setCurrentIndex(index);
    else if (!currentImportId.isEmpty())
        m_pImportIdCombo->setEditText(currentImportId);
    else if (m_pImportIdCombo->count() > 0)
        m_pImportIdCombo->setCurrentIndex(0);

    m_pImportIdCombo->blockSignals(false);
}

void ImportPackageUploadWidget::recordSuccessfulUpload(const QString &zipPath, const QString &importId, int serviceId)
{
    Q_UNUSED(zipPath);
    Q_UNUSED(serviceId);
    const QString trimmedImportId = importId.trimmed();
    if (trimmedImportId.isEmpty())
        return;

    reloadImportHistory();

    const int index = m_pImportIdCombo->findData(trimmedImportId);
    if (index >= 0)
        m_pImportIdCombo->setCurrentIndex(index);
}

QString ImportPackageUploadWidget::historyFilePath() const
{
    return ImportPackageHistory::historyFilePath();
}

void ImportPackageUploadWidget::reloadImportHistory()
{
    ImportPackageHistory::migrateCurrentApplicationHistory();
    loadImportHistory();
    refreshImportHistoryCombo();
}

QString ImportPackageUploadWidget::selectedImportId() const
{
    if (!m_pImportIdCombo)
        return QString();

    const int currentIndex = m_pImportIdCombo->currentIndex();
    if (currentIndex >= 0)
    {
        const QString dataImportId = m_pImportIdCombo->itemData(currentIndex).toString().trimmed();
        if (!dataImportId.isEmpty() && m_pImportIdCombo->currentText() == m_pImportIdCombo->itemText(currentIndex))
            return dataImportId;
    }

    QString text = m_pImportIdCombo->currentText().trimmed();
    const int splitterIndex = text.indexOf(QStringLiteral("|"));
    if (splitterIndex > 0)
        text = text.left(splitterIndex).trimmed();
    return text;
}

QString ImportPackageUploadWidget::selectedImporterApiHost() const
{
    if (!m_pImporterHostCombo)
        return QString();
    return m_pImporterHostCombo->currentData().toString().trimmed();
}

QString ImportPackageUploadWidget::buildImportResultUrl(QString *errorMessage) const
{
    const QString importerApiHost = m_currentQueryImporterApiHost.isEmpty()
        ? selectedImporterApiHost()
        : m_currentQueryImporterApiHost;

    QUrl queryUrl;
    QString reason;
    if (!lemoncofighelper::TryBuildUrl(importerApiHost, lemoncofighelper::kGetImportResultV2Path, queryUrl, reason))
    {
        if (errorMessage)
            *errorMessage = reason + QStringLiteral(" ") + QUrl(importerApiHost).toDisplayString();
        return QString();
    }

    return queryUrl.toDisplayString();
}

void ImportPackageUploadWidget::onResultReplyFinished(QNetworkReply *reply, int querySerial)
{
    if (!reply || reply != m_pResultReply)
        return;

    m_pResultReply = nullptr;
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    if (querySerial != m_querySerial || !m_resultQueryRunning)
        return;

    if (networkError != QNetworkReply::NoError)
    {
        if (networkError == QNetworkReply::AuthenticationRequiredError || statusCode == 401)
        {
            finishResultQuery(QStringLiteral("查询导账结果认证失败，请重新登录后再试，或确认本地导账服务已启动并接收当前登录凭证"));
            return;
        }
        finishResultQuery(QStringLiteral("查询导账结果网络错误：") + networkErrorText);
        return;
    }

    if (statusCode != 200)
    {
        finishResultQuery(QStringLiteral("查询导账结果接口返回 HTTP %1：%2")
            .arg(statusCode)
            .arg(QString::fromUtf8(body).left(500)));
        return;
    }

    handleImportResultResponse(m_currentQueryImportId, body);
}

void ImportPackageUploadWidget::handleImportResultResponse(const QString &importId, const QByteArray &body)
{
    QJsonParseError jsonError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !document.isObject())
    {
        finishResultQuery(QStringLiteral("解析导账结果接口返回失败：") + jsonError.errorString());
        return;
    }

    const QJsonObject rootObject = document.object();
    const int code = rootObject.value(QStringLiteral("Code")).toInt(-1);
    const QString message = rootObject.value(QStringLiteral("Message")).toString();
    if (code != 0)
    {
        finishResultQuery(message.isEmpty()
            ? QStringLiteral("查询导账结果失败，Code=%1").arg(code)
            : message);
        return;
    }

    const QJsonValue rootData = rootObject.value(QStringLiteral("Data"));
    if (rootData.isNull() || rootData.isUndefined())
    {
        scheduleNextResultPoll(QStringLiteral("导账结果尚未返回，10 秒后自动重试"));
        return;
    }

    QJsonValue resultValue = rootData;
    if (rootData.isArray())
    {
        const QJsonArray resultArray = rootData.toArray();
        if (resultArray.isEmpty())
        {
            scheduleNextResultPoll(QStringLiteral("导账结果尚未返回，10 秒后自动重试"));
            return;
        }

        QJsonObject matchedObject;
        for (const QJsonValue &item : resultArray)
        {
            if (!item.isObject())
                continue;
            const QJsonObject itemObject = item.toObject();
            if (itemObject.value(QStringLiteral("Id")).toString() == importId)
            {
                matchedObject = itemObject;
                break;
            }
        }
        if (matchedObject.isEmpty() && resultArray.at(0).isObject())
            matchedObject = resultArray.at(0).toObject();

        if (matchedObject.isEmpty())
        {
            scheduleNextResultPoll(QStringLiteral("导账结果尚未返回，10 秒后自动重试"));
            return;
        }

        resultValue = matchedObject.value(QStringLiteral("Data"));
        if (resultValue.isNull() || resultValue.isUndefined())
        {
            scheduleNextResultPoll(QStringLiteral("导账结果尚未返回，10 秒后自动重试"));
            return;
        }
    }

    QString resultText;
    if (resultValue.isObject())
    {
        resultText = QString::fromUtf8(QJsonDocument(resultValue.toObject()).toJson(QJsonDocument::Indented));
    }
    else if (resultValue.isArray())
    {
        resultText = QString::fromUtf8(QJsonDocument(resultValue.toArray()).toJson(QJsonDocument::Indented));
    }
    else if (resultValue.isString())
    {
        resultText = resultValue.toString();
    }
    else if (resultValue.isBool())
    {
        resultText = resultValue.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    else if (resultValue.isDouble())
    {
        resultText = QString::number(resultValue.toDouble(), 'g', 15);
    }
    else
    {
        resultText = QString::fromUtf8(body);
    }

    finishResultQuery(QStringLiteral("导账结果已返回：") + importId, resultText);
}

void ImportPackageUploadWidget::scheduleNextResultPoll(const QString &message)
{
    if (!m_resultQueryRunning)
        return;

    m_pStatusLabel->setText(message);
    appendLog(QStringLiteral("%1 importId=%2").arg(message, m_currentQueryImportId));
    if (m_pResultPollTimer)
        m_pResultPollTimer->start();
}

void ImportPackageUploadWidget::finishResultQuery(const QString &message, const QString &resultText)
{
    if (m_pResultPollTimer)
        m_pResultPollTimer->stop();

    setQueryRunning(false, message);
    appendLog(message);
    if (!resultText.trimmed().isEmpty())
    {
        appendLog(QStringLiteral("导账结果详情："));
        appendLog(resultText.trimmed());
    }
}

void ImportPackageUploadWidget::appendLog(const QString &message)
{
    if (!m_pLogEdit)
        return;

    if (m_pLogEdit->toPlainText().trimmed().isEmpty())
        m_pLogEdit->setPlainText(message);
    else
        m_pLogEdit->append(message);
}

void ImportPackageUploadWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (hasZipFile(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void ImportPackageUploadWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (hasZipFile(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void ImportPackageUploadWidget::dropEvent(QDropEvent *event)
{
    if (!hasZipFile(event->mimeData()))
    {
        event->ignore();
        return;
    }

    for (const QUrl &url : event->mimeData()->urls())
    {
        if (url.isLocalFile() && isZipFile(url.toLocalFile()))
        {
            event->acceptProposedAction();
            AppLogger::operation(QStringLiteral("拖入上传压缩包 path=%1").arg(QDir::toNativeSeparators(url.toLocalFile())));
            startUpload(url.toLocalFile());
            return;
        }
    }

    event->ignore();
}
