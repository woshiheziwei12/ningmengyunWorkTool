#include "Common/sessiontaskguard.h"
#include "repairaccountsetwidget.h"
#include "Common/repairinputfileutils.h"
#include "subjectconflictdialog.h"
#include "Common/globaldata.h"
#include "Common/importpackageuploader.h"
#include "Common/logger.h"
#include "Common/repairfilenameutils.h"
#include "Common/repairpathconfigwidget.h"
#include "Common/repairpathsettings.h"

#include <algorithm>
#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QDate>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
bool hasZipFile(const QMimeData *mimeData)
{
    if (!mimeData || !mimeData->hasUrls())
        return false;

    for (const QUrl &url : mimeData->urls())
    {
        if (!url.isLocalFile())
            continue;

        QFileInfo fileInfo(url.toLocalFile());
        if (fileInfo.isFile() && fileInfo.suffix().compare("zip", Qt::CaseInsensitive) == 0)
            return true;
    }

    return false;
}

QString shortDate(const QString &rawDate)
{
    return rawDate.size() >= 10 ? rawDate.left(10) : rawDate;
}

QRegularExpressionMatch jsonDateMatch(const QString &rawDate)
{
    static const QRegularExpression expression(
        QStringLiteral("^(\\d{4})([-./])(\\d{1,2})\\2(\\d{1,2})(.*)$"));
    return expression.match(rawDate);
}

QDate dateFromJsonValue(const QString &rawDate)
{
    const QRegularExpressionMatch match = jsonDateMatch(rawDate);
    if (!match.hasMatch())
        return QDate();

    return QDate(match.captured(1).toInt(),
                 match.captured(3).toInt(),
                 match.captured(4).toInt());
}

bool clampDateToMonthEnd(const QString &rawDate, QString *correctedRawDate, QDate *correctedDate)
{
    const QRegularExpressionMatch match = jsonDateMatch(rawDate);
    if (!match.hasMatch())
        return false;

    const int year = match.captured(1).toInt();
    const int month = match.captured(3).toInt();
    const int day = match.captured(4).toInt();
    const QDate firstDayOfMonth(year, month, 1);
    if (!firstDayOfMonth.isValid())
        return false;

    const int lastDay = firstDayOfMonth.daysInMonth();
    if (day <= lastDay)
        return false;

    if (correctedRawDate)
    {
        *correctedRawDate = rawDate;
        const QString correctedDay = QString::number(lastDay)
            .rightJustified(match.capturedLength(4), QLatin1Char('0'));
        correctedRawDate->replace(match.capturedStart(4), match.capturedLength(4), correctedDay);
    }
    if (correctedDate)
        *correctedDate = QDate(year, month, lastDay);
    return true;
}

QString accountCodeFromJsonValue(const QJsonValue &value)
{
    if (value.isString())
        return value.toString().trimmed();
    if (value.isDouble())
    {
        const double number = value.toDouble();
        const qint64 integer = static_cast<qint64>(number);
        if (qAbs(number - static_cast<double>(integer)) < 0.000001)
            return QString::number(integer);
        return QString::number(number, 'f', 6);
    }
    return value.toVariant().toString().trimmed();
}

QSet<QString> collectUsedAccountCodes(const QJsonObject &root)
{
    QSet<QString> result;
    const auto collect = [&result](const QJsonArray &records) {
        for (const QJsonValue &value : records)
        {
            const QString code = accountCodeFromJsonValue(
                value.toObject().value(QStringLiteral("AcctCode")));
            if (!code.isEmpty())
                result.insert(code);
        }
    };
    collect(root.value(QStringLiteral("GLBalanceList")).toArray());
    collect(root.value(QStringLiteral("GLVoucherList")).toArray());
    return result;
}

QString textFromJsonValue(const QJsonValue &value)
{
    return accountCodeFromJsonValue(value);
}

bool isBlankJsonValue(const QJsonValue &value)
{
    return textFromJsonValue(value).isEmpty();
}

bool anyProjectCodeHasValue(const QJsonObject &object)
{
    for (int i = 1; i <= 6; ++i)
    {
        if (!isBlankJsonValue(object.value(QStringLiteral("ProjCode%1").arg(i))))
            return true;
    }
    return false;
}

bool allProjectCodesBlank(const QJsonObject &object)
{
    for (int i = 1; i <= 6; ++i)
    {
        if (!isBlankJsonValue(object.value(QStringLiteral("ProjCode%1").arg(i))))
            return false;
    }
    return true;
}

bool allowNullAaTypesContains(const QString &allowNullAaTypes, const QString &aaTypeCode)
{
    const QStringList parts = allowNullAaTypes.split(QLatin1Char(','));
    for (const QString &part : parts)
    {
        if (part.trimmed() == aaTypeCode)
            return true;
    }
    return false;
}

bool appendAllowNullAaType(QJsonObject *account, const QString &aaTypeCode)
{
    if (!account || aaTypeCode.isEmpty())
        return false;

    const QString current = textFromJsonValue(
        account->value(QStringLiteral("AllowNullAATypes")));
    if (allowNullAaTypesContains(current, aaTypeCode))
        return false;

    QString nextValue = current;
    if (nextValue.isEmpty())
        nextValue = aaTypeCode;
    else if (nextValue.endsWith(QLatin1Char(',')))
        nextValue += aaTypeCode;
    else
        nextValue += QLatin1Char(',') + aaTypeCode;

    account->insert(QStringLiteral("AllowNullAATypes"), nextValue);
    return true;
}
}

RepairAccountSetWidget::RepairAccountSetWidget(GlobalData *globalData, QWidget *parent)
    : QWidget(parent),
      m_pGlobalData(globalData),
      m_pTitleLabel(nullptr),
      m_pDirLabel(nullptr),
      m_pDropLabel(nullptr),
      m_pPathConfig(nullptr),
      m_pAccountTable(nullptr),
      m_pResultEdit(nullptr),
      m_pBtnSelectAll(nullptr),
      m_pBtnRefresh(nullptr),
      m_pBtnRepair(nullptr),
      m_pBtnBack(nullptr),
      m_dateRepairOnly(false),
      m_siblingNameRepairOnly(false),
      m_pScanTemporaryDir(nullptr)
{
    setAcceptDrops(true);
    setupUi();
}

RepairAccountSetWidget::~RepairAccountSetWidget()
{
    delete m_pScanTemporaryDir;
}

void RepairAccountSetWidget::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    QHBoxLayout *titleLayout = new QHBoxLayout();
    m_pTitleLabel = new QLabel(QStringLiteral("\u4fee\u590d\u8d26\u5957"), this);
    m_pTitleLabel->setObjectName("labelPageTitle");
    titleLayout->addWidget(m_pTitleLabel);
    titleLayout->addStretch();

    m_pBtnSelectAll = new QPushButton(QStringLiteral("\u5168\u9009"), this);
    m_pBtnSelectAll->setObjectName("btnSelectAll");
    m_pBtnSelectAll->setProperty("flatGreen", true);
    m_pBtnSelectAll->setCheckable(true);
    m_pBtnSelectAll->setMaximumWidth(80);
    titleLayout->addWidget(m_pBtnSelectAll);

    m_pBtnRefresh = new QPushButton(QStringLiteral("\u5237\u65b0\u76ee\u5f55"), this);
    m_pBtnRefresh->setObjectName("btnRefreshRepairList");
    m_pBtnRefresh->setMaximumWidth(100);
    m_pBtnRefresh->setMinimumHeight(30);
    titleLayout->addWidget(m_pBtnRefresh);
    layout->addLayout(titleLayout);

    m_pPathConfig = new RepairPathConfigWidget(this);
    m_pPathConfig->setPathsChangedCallback([this]() { loadAccountSetList(); });
    layout->addWidget(m_pPathConfig);

    m_pDirLabel = new QLabel(this);
    m_pDirLabel->setWordWrap(true);
    layout->addWidget(m_pDirLabel);

    m_pDropLabel = new QLabel(
        QStringLiteral("\u5c06 ZIP \u538b\u7f29\u5305\u62d6\u5165\u6b64\u5904\uff0c\u7cfb\u7edf\u4f1a\u81ea\u52a8\u626b\u63cf AccountSet.json"),
        this);
    m_pDropLabel->setObjectName("labelRepairDropArea");
    m_pDropLabel->setAlignment(Qt::AlignCenter);
    m_pDropLabel->setMinimumHeight(74);
    m_pDropLabel->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(m_pDropLabel);

    m_pAccountTable = new QTableWidget(this);
    m_pAccountTable->setObjectName("repairAccountTable");
    m_pAccountTable->setColumnCount(5);
    m_pAccountTable->setHorizontalHeaderLabels({
        QStringLiteral("\u9009\u62e9"),
        QStringLiteral("\u8d26\u5957\u540d\u79f0"),
        QStringLiteral("\u542f\u7528\u65e5\u671f"),
        QStringLiteral("\u6587\u4ef6"),
        QStringLiteral("\u72b6\u6001")
    });
    m_pAccountTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_pAccountTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_pAccountTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_pAccountTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_pAccountTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_pAccountTable->setColumnWidth(0, 50);
    m_pAccountTable->setColumnWidth(2, 120);
    m_pAccountTable->setColumnWidth(4, 110);
    m_pAccountTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pAccountTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pAccountTable->verticalHeader()->setVisible(false);
    m_pAccountTable->verticalHeader()->setDefaultSectionSize(38);
    m_pAccountTable->horizontalHeader()->setHighlightSections(false);
    m_pAccountTable->setAlternatingRowColors(true);
    m_pAccountTable->setShowGrid(false);
    m_pAccountTable->setAcceptDrops(false);
    m_pAccountTable->viewport()->setAcceptDrops(false);
    layout->addWidget(m_pAccountTable);

    m_pResultEdit = new QTextEdit(this);
    m_pResultEdit->setObjectName("repairResultDetails");
    m_pResultEdit->setReadOnly(true);
    m_pResultEdit->setMinimumHeight(150);
    m_pResultEdit->setMaximumHeight(220);
    m_pResultEdit->setPlaceholderText(QStringLiteral("\u4fee\u590d\u5b8c\u6210\u540e\uff0c\u8fd9\u91cc\u4f1a\u663e\u793a\u6bcf\u4e00\u6b65\u7684\u5904\u7406\u6570\u91cf\u548c\u4fee\u6539\u660e\u7ec6\u3002"));
    m_pResultEdit->setVisible(false);
    layout->addWidget(m_pResultEdit);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    m_pBtnBack = new QPushButton(QStringLiteral("\u8fd4\u56de"), this);
    m_pBtnBack->setObjectName("btnBackToMain");
    m_pBtnBack->setProperty("grayButton", true);
    m_pBtnBack->setMaximumWidth(100);
    m_pBtnBack->setMinimumHeight(36);

    m_pBtnRepair = new QPushButton(QStringLiteral("\u4fee\u590d\u9009\u4e2d\u8d26\u5957"), this);
    m_pBtnRepair->setObjectName("btnRepairSelected");
    m_pBtnRepair->setMaximumWidth(150);
    m_pBtnRepair->setMinimumHeight(36);

    btnLayout->addWidget(m_pBtnBack);
    btnLayout->addStretch();
    btnLayout->addWidget(m_pBtnRepair);
    layout->addLayout(btnLayout);

    connect(m_pBtnSelectAll, &QPushButton::toggled, this, &RepairAccountSetWidget::onSelectAllChanged);
    connect(m_pBtnRefresh, &QPushButton::clicked, this, &RepairAccountSetWidget::onRefreshClicked);
    connect(m_pBtnRepair, &QPushButton::clicked, this, &RepairAccountSetWidget::onRepairClicked);
    connect(m_pBtnBack, &QPushButton::clicked, this, &RepairAccountSetWidget::onBackClicked);
}

void RepairAccountSetWidget::setDateRepairOnly(bool dateRepairOnly)
{
    m_dateRepairOnly = dateRepairOnly;
    m_siblingNameRepairOnly = false;
    if (m_pTitleLabel)
        m_pTitleLabel->setText(dateRepairOnly
            ? QStringLiteral("\u51ed\u8bc1\u65e5\u671f\u5f02\u5e38\u4fee\u590d")
            : QStringLiteral("\u4fee\u590d\u8d26\u5957"));
    if (m_pBtnRepair)
        m_pBtnRepair->setText(dateRepairOnly
            ? QStringLiteral("\u4fee\u590d\u9009\u4e2d\u51ed\u8bc1\u65e5\u671f")
            : QStringLiteral("\u4fee\u590d\u9009\u4e2d\u8d26\u5957"));
}

void RepairAccountSetWidget::setSiblingNameRepairOnly(bool siblingNameRepairOnly)
{
    m_siblingNameRepairOnly = siblingNameRepairOnly;
    if (siblingNameRepairOnly)
        m_dateRepairOnly = false;
    if (m_pTitleLabel)
        m_pTitleLabel->setText(siblingNameRepairOnly
            ? QStringLiteral("同级科目重名处理")
            : QStringLiteral("修复账套"));
    if (m_pBtnRepair)
        m_pBtnRepair->setText(siblingNameRepairOnly
            ? QStringLiteral("处理选中账套的重名科目")
            : QStringLiteral("修复选中账套"));
}

void RepairAccountSetWidget::loadAccountSetList()
{
    AppLogger::operation(QStringLiteral("修复账套加载列表"));
    if (m_pResultEdit)
    {
        m_pResultEdit->clear();
        m_pResultEdit->setVisible(false);
    }
    ensureRepairDir();
    scanJsonFiles();
    fillTable();
}

QString RepairAccountSetWidget::repairDir() const
{
    return RepairPathSettings::inputDir();
}

QString RepairAccountSetWidget::repairedDir() const
{
    return RepairPathSettings::outputDir();
}

void RepairAccountSetWidget::ensureRepairDir() const
{
    QDir dir(repairDir());
    if (!dir.exists())
        dir.mkpath(".");
    QDir outputDir(repairedDir());
    if (!outputDir.exists())
        outputDir.mkpath(".");
}

void RepairAccountSetWidget::scanJsonFiles()
{
    SessionTaskGuard sessionTask(this);
    m_accountSetList.clear();

    delete m_pScanTemporaryDir;
    m_pScanTemporaryDir = new QTemporaryDir(
        QDir::tempPath() + QStringLiteral("/LemonRepairScan-XXXXXX"));

    const QString inputRoot = QDir::cleanPath(repairDir()) + QLatin1Char('/');
    const QString outputRoot = QDir::cleanPath(repairedDir()) + QLatin1Char('/');
    const bool outputIsInputSubdirectory = outputRoot != inputRoot
        && outputRoot.startsWith(inputRoot, Qt::CaseInsensitive);

    const auto isInOutputDirectory = [outputIsInputSubdirectory, outputRoot](const QString &path) {
        return outputIsInputSubdirectory
            && QDir::cleanPath(path).startsWith(outputRoot, Qt::CaseInsensitive);
    };

    QDirIterator jsonIterator(
        repairDir(), QStringList() << "*.json", QDir::Files, QDirIterator::Subdirectories);
    while (jsonIterator.hasNext())
    {
        const QString filePath = jsonIterator.next();
        if (isInOutputDirectory(filePath))
            continue;

        RepairAccountSetInfo info;
        QString errorMessage;
        if (!parseAccountSetFile(filePath, info, &errorMessage))
        {
            info.asName = QFileInfo(filePath).completeBaseName();
            info.startDate.clear();
            info.jsonPath = filePath;
            info.status = errorMessage;
            info.valid = false;
        }
        info.sourcePath = filePath;
        m_accountSetList.append(info);
    }

    int zipIndex = 0;
    QDirIterator zipIterator(
        repairDir(), QStringList() << "*.zip", QDir::Files, QDirIterator::Subdirectories);
    while (zipIterator.hasNext())
    {
        const QString zipPath = zipIterator.next();
        if (isInOutputDirectory(zipPath))
            continue;

        RepairAccountSetInfo info;
        info.sourcePath = zipPath;
        info.asName = QFileInfo(zipPath).completeBaseName();
        info.valid = false;

        QString errorMessage;
        if (!m_pScanTemporaryDir || !m_pScanTemporaryDir->isValid())
        {
            errorMessage = QStringLiteral("创建 ZIP 扫描缓存失败");
        }
        else
        {
            if (m_pDropLabel)
            {
                m_pDropLabel->setText(QStringLiteral("正在扫描压缩包：%1")
                    .arg(QFileInfo(zipPath).fileName()));
                QApplication::processEvents();
            }
            const QString destinationPath = QDir(m_pScanTemporaryDir->path())
                .absoluteFilePath(QStringLiteral("zip_%1/AccountSet.json").arg(++zipIndex));
            if (extractAccountSetJsonForScan(zipPath, destinationPath, &errorMessage))
            {
                if (!parseAccountSetFile(destinationPath, info, &errorMessage))
                {
                    info.jsonPath = destinationPath;
                }
            }
        }

        if (!errorMessage.isEmpty())
        {
            info.status = errorMessage;
            info.valid = false;
        }
        m_accountSetList.append(info);
    }
}

bool RepairAccountSetWidget::parseAccountSetFile(const QString &filePath, RepairAccountSetInfo &info, QString *errorMessage) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u6253\u5f00\u5931\u8d25");
        return false;
    }

    QJsonParseError jsonError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("JSON \u89e3\u6790\u5931\u8d25");
        return false;
    }

    const QJsonObject root = doc.object();
    const QJsonObject accountSet = root.value("AccountSet").toObject();
    const QString asName = accountSet.value("AsName").toString();
    const QString startDateValue = accountSet.value("StartDate").toString();

    if (asName.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u672a\u627e\u5230\u8d26\u5957\u540d\u79f0");
        return false;
    }

    info.asName = asName;
    info.startDate = shortDate(startDateValue);
    info.jsonPath = filePath;
    if (m_siblingNameRepairOnly)
    {
        QList<SiblingRenameRecord> duplicateRecords;
        fixSiblingNameDuplicates(
            root.value(QStringLiteral("GLAccountList")).toArray(),
            collectUsedAccountCodes(root),
            duplicateRecords);
        info.valid = !duplicateRecords.isEmpty();
        info.status = info.valid
            ? QStringLiteral("发现 %1 个重名科目，待处理").arg(duplicateRecords.size())
            : QStringLiteral("未发现同级科目重名");
    }
    else
    {
        info.status = QStringLiteral("\u5f85\u4fee\u590d");
        info.valid = true;
    }
    return true;
}

void RepairAccountSetWidget::fillTable()
{
    const QString dirText = QStringLiteral("\u5f53\u524d\u626b\u63cf\u76ee\u5f55\uff1a")
        + QDir::toNativeSeparators(repairDir());
    m_pDirLabel->setText(dirText);
    if (m_pPathConfig)
        m_pPathConfig->refreshPaths();

    m_pAccountTable->setRowCount(m_accountSetList.size());
    QDir rootDir(repairDir());
    int validCount = 0;

    for (int i = 0; i < m_accountSetList.size(); ++i)
    {
        const RepairAccountSetInfo &info = m_accountSetList[i];
        if (info.valid)
            ++validCount;

        QTableWidgetItem *checkItem = new QTableWidgetItem();
        checkItem->setTextAlignment(Qt::AlignCenter);
        checkItem->setCheckState(Qt::Unchecked);
        Qt::ItemFlags itemFlags = info.valid
            ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable)
            : Qt::ItemIsSelectable;
        checkItem->setFlags(itemFlags);
        m_pAccountTable->setItem(i, 0, checkItem);

        m_pAccountTable->setItem(i, 1, new QTableWidgetItem(info.asName));
        m_pAccountTable->setItem(i, 2, new QTableWidgetItem(info.startDate));
        const QString displayPath = info.sourcePath.isEmpty() ? info.jsonPath : info.sourcePath;
        m_pAccountTable->setItem(i, 3, new QTableWidgetItem(
            QDir::toNativeSeparators(rootDir.relativeFilePath(displayPath))));

        QTableWidgetItem *statusItem = new QTableWidgetItem(info.status);
        const bool noSiblingDuplicate = m_siblingNameRepairOnly
            && info.status == QStringLiteral("未发现同级科目重名");
        statusItem->setForeground(info.valid
            ? QColor(110, 224, 177)
            : noSiblingDuplicate ? QColor(156, 171, 194) : QColor(255, 139, 162));
        m_pAccountTable->setItem(i, 4, statusItem);
    }

    m_pBtnSelectAll->setChecked(false);
    m_pBtnSelectAll->setText(QStringLiteral("\u5168\u9009"));
    m_pBtnRepair->setEnabled(validCount > 0);

    int jsonFileCount = 0;
    int zipFileCount = 0;
    for (const RepairAccountSetInfo &info : m_accountSetList)
    {
        const QString suffix = QFileInfo(info.sourcePath).suffix();
        if (suffix.compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0)
            ++zipFileCount;
        else
            ++jsonFileCount;
    }

    const QString scanText = m_dateRepairOnly
        ? QStringLiteral("\u5df2\u626b\u63cf %1 \u4e2a\u6587\u4ef6\uff08JSON %2\uff0cZIP %3\uff09\uff0c\u5176\u4e2d %4 \u4e2a\u53ef\u5904\u7406\u3002"
                         "\u5c06\u81ea\u52a8\u628a 6 \u6708 31 \u65e5\u7b49\u8d8a\u754c\u51ed\u8bc1\u65e5\u671f\u6539\u4e3a\u5f53\u6708\u6700\u540e\u4e00\u5929\u3002")
        : m_siblingNameRepairOnly
            ? QStringLiteral("已扫描 %1 个文件（JSON %2，ZIP %3），其中 %4 个可处理。"
                             "每组优先保留一个使用过的科目原名，其余改为“名称-科目编码”；整组均未使用时随机保留一个。")
            : QStringLiteral("\u5df2\u626b\u63cf %1 \u4e2a\u6587\u4ef6\uff08JSON %2\uff0cZIP %3\uff09\uff0c\u5176\u4e2d %4 \u4e2a\u53ef\u4fee\u590d\u3002"
                             "\u5c06\u6309\u987a\u5e8f\u6267\u884c\uff1a1. \u79d1\u76ee\u7f16\u7801\u51b2\u7a81  2. \u540c\u7ea7\u79d1\u76ee\u91cd\u547d\u540d  "
                             "3. \u8f85\u52a9\u6838\u7b97\u8865\u975e\u5fc5\u5f55  4. \u8be2\u95ee\u662f\u5426\u4e0a\u4f20\u3002");
    m_pDropLabel->setText(scanText.arg(m_accountSetList.size())
        .arg(jsonFileCount)
        .arg(zipFileCount)
        .arg(validCount));
}

bool RepairAccountSetWidget::extractAccountSetJsonForScan(
    const QString &zipPath,
    const QString &destinationPath,
    QString *errorMessage) const
{
    const QFileInfo zipInfo(zipPath);
    if (!zipInfo.exists() || !zipInfo.isFile())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("压缩包不存在");
        return false;
    }

    if (!QDir().mkpath(QFileInfo(destinationPath).absolutePath()))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("创建 ZIP 扫描缓存目录失败");
        return false;
    }

    const QString script =
        "$ErrorActionPreference='Stop';"
        "Add-Type -AssemblyName System.IO.Compression;"
        "Add-Type -AssemblyName System.IO.Compression.FileSystem;"
        "$zip=$env:LEMON_REPAIR_SCAN_ZIP;"
        "$dest=$env:LEMON_REPAIR_SCAN_JSON;"
        "$archive=[System.IO.Compression.ZipFile]::OpenRead($zip);"
        "try {"
        "  $entry=$archive.Entries | Where-Object {"
        "    [System.IO.Path]::GetFileName($_.FullName) -ieq 'AccountSet.json'"
        "  } | Select-Object -First 1;"
        "  if($null -eq $entry) { throw 'ZIP 中未找到 AccountSet.json' }"
        "  if(Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Force }"
        "  [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$dest);"
        "} finally { $archive.Dispose() }";

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LEMON_REPAIR_SCAN_ZIP"), zipInfo.absoluteFilePath());
    environment.insert(QStringLiteral("LEMON_REPAIR_SCAN_JSON"),
                       QFileInfo(destinationPath).absoluteFilePath());
    process.setProcessEnvironment(environment);
    const QStringList arguments = QStringList()
        << QStringLiteral("-NoProfile")
        << QStringLiteral("-ExecutionPolicy")
        << QStringLiteral("Bypass")
        << QStringLiteral("-Command")
        << script;
    process.start(QStringLiteral("powershell.exe"), arguments);

    if (!process.waitForFinished(60000))
    {
        process.kill();
        process.waitForFinished(3000);
        if (errorMessage)
            *errorMessage = QStringLiteral("扫描 ZIP 超时");
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        const QString standardError = QString::fromLocal8Bit(
            process.readAllStandardError()).trimmed();
        if (errorMessage)
            *errorMessage = standardError.isEmpty()
                ? QStringLiteral("读取 ZIP 中的 AccountSet.json 失败")
                : standardError;
        return false;
    }

    if (!QFileInfo::exists(destinationPath))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("ZIP 中的 AccountSet.json 未成功解压");
        return false;
    }
    return true;
}

bool RepairAccountSetWidget::extractZipToRepairDir(const QString &zipPath, QString *errorMessage)
{
    ensureRepairDir();

    QFileInfo zipInfo(zipPath);
    if (!zipInfo.exists() || !zipInfo.isFile())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u538b\u7f29\u5305\u4e0d\u5b58\u5728");
        return false;
    }

    const QString outDir = QDir::cleanPath(repairDir() + "/" + zipInfo.completeBaseName());
    QDir targetDir(outDir);
    if (targetDir.exists())
    {
        const QString rootCanonical = QDir(repairDir()).canonicalPath();
        const QString targetCanonical = targetDir.canonicalPath();
        if (rootCanonical.isEmpty() || targetCanonical.isEmpty()
            || !targetCanonical.startsWith(rootCanonical + "/", Qt::CaseInsensitive))
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("\u76ee\u6807\u76ee\u5f55\u6821\u9a8c\u5931\u8d25");
            return false;
        }
        targetDir.removeRecursively();
    }

    if (!QDir().mkpath(outDir))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u521b\u5efa\u89e3\u538b\u76ee\u5f55\u5931\u8d25");
        return false;
    }

    const QString script =
        "$ErrorActionPreference='Stop';"
        "Add-Type -AssemblyName System.IO.Compression;"
        "Add-Type -AssemblyName System.IO.Compression.FileSystem;"
        "$zip=$env:LEMON_ZIP_PATH;$dest=$env:LEMON_ZIP_DEST;"
        "if([string]::IsNullOrWhiteSpace($zip)) { throw 'ZIP path is empty' }"
        "if([string]::IsNullOrWhiteSpace($dest)) { throw 'Destination path is empty' }"
        "$archive=[System.IO.Compression.ZipFile]::OpenRead($zip);"
        "$count=0;"
        "try {"
        "  foreach($entry in $archive.Entries) {"
        "    if([string]::IsNullOrWhiteSpace($entry.Name)) { continue }"
        "    if(-not $entry.Name.EndsWith('.json',[System.StringComparison]::OrdinalIgnoreCase)) { continue }"
        "    $name=[System.IO.Path]::GetFileName($entry.Name);"
        "    if([string]::IsNullOrWhiteSpace($name)) { continue }"
        "    $target=Join-Path $dest $name;"
        "    if(Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }"
        "    [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$target);"
        "    $count++;"
        "  }"
        "}"
        "finally { $archive.Dispose() }"
        "if($count -eq 0) { throw 'ZIP 中未找到 JSON 文件' }";

    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LEMON_ZIP_PATH", zipInfo.absoluteFilePath());
    env.insert("LEMON_ZIP_DEST", outDir);
    process.setProcessEnvironment(env);
    const QStringList args = QStringList()
        << "-NoProfile"
        << "-ExecutionPolicy"
        << "Bypass"
        << "-Command"
        << script;
    AppLogger::operation(QStringLiteral("修复账套解压 ZIP zip=%1 dest=%2")
        .arg(QDir::toNativeSeparators(zipInfo.absoluteFilePath()), QDir::toNativeSeparators(outDir)));
    AppLogger::command("powershell.exe", args);
    process.start("powershell.exe", args);

    if (!process.waitForFinished(60000))
    {
        process.kill();
        process.waitForFinished(3000);
        if (errorMessage)
            *errorMessage = QStringLiteral("\u89e3\u538b\u8d85\u65f6");
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        const QString stdErr = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (errorMessage)
            *errorMessage = stdErr.isEmpty() ? QStringLiteral("\u89e3\u538b\u5931\u8d25") : stdErr;
        return false;
    }

    return true;
}

QList<SubjectConflict> RepairAccountSetWidget::detectSubjectConflicts(const QJsonArray &accountList) const
{
    // key: AcctCode, value: all distinct names for that code
    QMap<QString, QStringList> codeToNames;

    for (const QJsonValue &val : accountList)
    {
        const QJsonObject acct = val.toObject();
        const QString code = acct.value("AcctCode").toString();
        const QString name = acct.value("AcctName").toString();
        if (code.isEmpty() || name.isEmpty())
            continue;

        QStringList &names = codeToNames[code];
        if (!names.contains(name))
            names.append(name);
    }

    // Keep only codes with conflicting names (count > 1)
    QList<SubjectConflict> conflicts;
    for (auto it = codeToNames.constBegin(); it != codeToNames.constEnd(); ++it)
    {
        if (it.value().size() > 1)
        {
            SubjectConflict conflict;
            conflict.acctCode = it.key();
            conflict.names = it.value();
            conflicts.append(conflict);
        }
    }

    return conflicts;
}

QJsonArray RepairAccountSetWidget::fixSiblingNameDuplicates(
    const QJsonArray &accountList,
    const QSet<QString> &usedAccountCodes,
    QList<SiblingRenameRecord> &records) const
{
    struct AccountVariant {
        QString code;
        QString normalizedName;
        QString category;
        int level = 0;
        int firstIndex = -1;
        QList<int> originalIndices;
    };

    const auto jsonText = [](const QJsonValue &value) {
        return value.toVariant().toString().trimmed();
    };
    const auto normalizeName = [](QString name) {
        name.remove(QLatin1Char('\r'));
        name.remove(QLatin1Char('\n'));
        name.replace(QLatin1Char('('), QChar(0xff08));
        name.replace(QLatin1Char(')'), QChar(0xff09));
        return name.trimmed();
    };

    // 同一科目编码在多个年度可能有多条记录。按“编码 + 规范化名称”归并成
    // 一个逻辑科目，改名时再同步更新这些年度记录，避免把同一科目误判为兄弟科目。
    QList<AccountVariant> variants;
    QMap<QString, int> variantIndexByKey;
    for (int i = 0; i < accountList.size(); ++i)
    {
        const QJsonObject account = accountList.at(i).toObject();
        const QString code = jsonText(account.value(QStringLiteral("AcctCode")));
        const QString name = normalizeName(jsonText(account.value(QStringLiteral("AcctName"))));
        if (code.isEmpty() || name.isEmpty())
            continue;

        const QString variantKey = code + QLatin1Char('\n') + name;
        int variantIndex = variantIndexByKey.value(variantKey, -1);
        if (variantIndex < 0)
        {
            AccountVariant variant;
            variant.code = code;
            variant.normalizedName = name;
            variant.category = jsonText(account.value(QStringLiteral("AcctCate")));
            variant.level = account.value(QStringLiteral("AcctLevel")).toVariant().toInt();
            variant.firstIndex = i;
            variant.originalIndices.append(i);
            variants.append(variant);
            variantIndex = variants.size() - 1;
            variantIndexByKey.insert(variantKey, variantIndex);
        }
        else
        {
            variants[variantIndex].originalIndices.append(i);
        }
    }

    // 老数据可能没有 AcctLevel。此时按科目编码长度推导层级，与参考工具保持一致。
    QList<int> codeLengths;
    for (const AccountVariant &variant : variants)
    {
        if (!codeLengths.contains(variant.code.size()))
            codeLengths.append(variant.code.size());
    }
    std::sort(codeLengths.begin(), codeLengths.end());
    for (AccountVariant &variant : variants)
    {
        if (variant.level <= 0)
            variant.level = codeLengths.indexOf(variant.code.size()) + 1;
    }

    QMap<QString, QList<int>> siblingGroups;
    for (int i = 0; i < variants.size(); ++i)
    {
        const AccountVariant &variant = variants.at(i);
        QString parentCode;
        if (variant.level > 1)
        {
            // 优先找上一级且为编码前缀的科目；异常数据找不到时，再退化为最长前缀。
            for (const AccountVariant &candidate : variants)
            {
                if (candidate.level == variant.level - 1
                    && candidate.code.size() < variant.code.size()
                    && variant.code.startsWith(candidate.code)
                    && candidate.code.size() > parentCode.size())
                {
                    parentCode = candidate.code;
                }
            }
            if (parentCode.isEmpty())
            {
                for (const AccountVariant &candidate : variants)
                {
                    if (candidate.code.size() < variant.code.size()
                        && variant.code.startsWith(candidate.code)
                        && candidate.code.size() > parentCode.size())
                    {
                        parentCode = candidate.code;
                    }
                }
            }
        }

        // 一级科目按科目类别区分兄弟；其他级别必须属于同一父科目。
        const QString parentKey = variant.level <= 1
            ? QStringLiteral("ROOT\n") + variant.category
            : QStringLiteral("PARENT\n") + parentCode;
        const QString groupKey = parentKey + QLatin1Char('\n') + variant.normalizedName;
        siblingGroups[groupKey].append(i);
    }

    QMap<int, QString> renameMap;
    for (auto it = siblingGroups.constBegin(); it != siblingGroups.constEnd(); ++it)
    {
        QSet<QString> distinctCodes;
        for (int variantIndex : it.value())
            distinctCodes.insert(variants.at(variantIndex).code);
        if (distinctCodes.size() <= 1)
            continue;

        QList<int> usedVariants;
        for (int variantIndex : it.value())
        {
            const QString code = variants.at(variantIndex).code;
            bool used = usedAccountCodes.contains(code);
            if (!used)
            {
                for (const QString &usedCode : usedAccountCodes)
                {
                    if (usedCode.size() > code.size() && usedCode.startsWith(code))
                    {
                        used = true;
                        break;
                    }
                }
            }
            if (used)
                usedVariants.append(variantIndex);
        }

        int keptVariantIndex = -1;
        if (!usedVariants.isEmpty())
        {
            // 有使用记录时，稳定地保留科目列表中最先出现的一个使用过的科目。
            keptVariantIndex = *std::min_element(
                usedVariants.constBegin(), usedVariants.constEnd(),
                [&variants](int left, int right) {
                    return variants.at(left).firstIndex < variants.at(right).firstIndex;
                });
        }
        else
        {
            // 整组均未使用时，按要求随机保留一个原名称。
            keptVariantIndex = it.value().at(
                QRandomGenerator::global()->bounded(it.value().size()));
        }

        // 不删除任何科目；除保留项外，其余兄弟科目改为“规范化名称-科目编码”。
        for (int variantIndex : it.value())
        {
            if (variantIndex == keptVariantIndex)
                continue;
            const AccountVariant &variant = variants.at(variantIndex);
            const QString newName = variant.normalizedName
                + QLatin1Char('-') + variant.code;
            for (int originalIndex : variant.originalIndices)
                renameMap.insert(originalIndex, newName);

            SiblingRenameRecord rec;
            rec.acctCode = variant.code;
            rec.originalName = variant.normalizedName;
            rec.newName = newName;
            records.append(rec);
        }
    }

    if (renameMap.isEmpty())
        return accountList;

    QJsonArray result;
    for (int i = 0; i < accountList.size(); ++i)
    {
        QJsonObject obj = accountList[i].toObject();
        if (renameMap.contains(i))
        {
            obj["AcctName"] = renameMap.value(i);
        }
        result.append(obj);
    }
    return result;
}

bool RepairAccountSetWidget::repairAllowNullAaTypes(
    QJsonObject *root,
    RepairPipelineSummary *summary,
    QString *errorMessage) const
{
    if (!root || !summary)
        return false;

    if (!root->value(QStringLiteral("GLAccountList")).isArray()
        || !root->value(QStringLiteral("GLVoucherList")).isArray())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u8f85\u52a9\u6838\u7b97\u4fee\u590d\u9700\u8981 GLAccountList \u548c GLVoucherList");
        return false;
    }

    QJsonArray accountList = root->value(QStringLiteral("GLAccountList")).toArray();
    const QJsonArray voucherList = root->value(QStringLiteral("GLVoucherList")).toArray();
    QMap<QString, int> accountIndexByCode;
    for (int i = 0; i < accountList.size(); ++i)
    {
        const QString code = textFromJsonValue(
            accountList.at(i).toObject().value(QStringLiteral("AcctCode")));
        if (!code.isEmpty() && !accountIndexByCode.contains(code))
            accountIndexByCode.insert(code, i);
    }

    if (accountIndexByCode.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u672a\u627e\u5230 GLAccountList \u79d1\u76ee\u6570\u636e");
        return false;
    }

    QSet<QString> balanceRemovalCandidates;
    QMap<QString, int> detailIndexByCode;
    QStringList missingAccountErrors;
    for (int voucherIndex = 0; voucherIndex < voucherList.size(); ++voucherIndex)
    {
        const QJsonObject voucher = voucherList.at(voucherIndex).toObject();
        ++summary->scannedVoucherCount;
        const QString acctCode = textFromJsonValue(
            voucher.value(QStringLiteral("AcctCode")));
        if (acctCode.isEmpty())
            continue;

        if (!accountIndexByCode.contains(acctCode))
        {
            QString voucherName = textFromJsonValue(voucher.value(QStringLiteral("MakeDate")))
                + textFromJsonValue(voucher.value(QStringLiteral("VchGroup")))
                + textFromJsonValue(voucher.value(QStringLiteral("GroupNum")));
            if (voucherName.isEmpty())
                voucherName = QStringLiteral("\u7b2c %1 \u6761").arg(voucherIndex + 1);
            missingAccountErrors.append(QStringLiteral("\u51ed\u8bc1\u300c%1\u300d\u4e2d\u7684\u79d1\u76ee\u300c%2\u300d\u4e0d\u5b58\u5728")
                .arg(voucherName, acctCode));
            continue;
        }

        const int accountIndex = accountIndexByCode.value(acctCode);
        QJsonObject account = accountList.at(accountIndex).toObject();
        for (int projectIndex = 1; projectIndex <= 6; ++projectIndex)
        {
            const QString aaTypeCode = textFromJsonValue(account.value(
                QStringLiteral("AcctProjTypeCode%1").arg(projectIndex)));
            if (aaTypeCode.isEmpty()
                || !isBlankJsonValue(voucher.value(
                    QStringLiteral("ProjCode%1").arg(projectIndex))))
            {
                continue;
            }

            ++summary->missingAaItemCount;
            balanceRemovalCandidates.insert(acctCode);
            if (!appendAllowNullAaType(&account, aaTypeCode))
                continue;

            ++summary->appendedAaTypeCount;
            int detailIndex = detailIndexByCode.value(acctCode, -1);
            if (detailIndex < 0)
            {
                AllowNullAaRepairRecord record;
                record.acctCode = acctCode;
                record.acctName = textFromJsonValue(
                    account.value(QStringLiteral("AcctName")));
                summary->allowNullAaRecords.append(record);
                detailIndex = summary->allowNullAaRecords.size() - 1;
                detailIndexByCode.insert(acctCode, detailIndex);
            }
            if (!summary->allowNullAaRecords[detailIndex].appendedAaTypes.contains(aaTypeCode))
                summary->allowNullAaRecords[detailIndex].appendedAaTypes.append(aaTypeCode);
        }
        accountList.replace(accountIndex, account);
    }

    if (!missingAccountErrors.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u8f85\u52a9\u6838\u7b97\u68c0\u67e5\u5931\u8d25\uff1a\n")
                + missingAccountErrors.join(QLatin1Char('\n'));
        return false;
    }

    root->insert(QStringLiteral("GLAccountList"), accountList);

    if (!root->value(QStringLiteral("GLBalanceList")).isArray()
        || balanceRemovalCandidates.isEmpty())
    {
        return true;
    }

    const QJsonArray balanceList = root->value(QStringLiteral("GLBalanceList")).toArray();
    QSet<QString> accountsWithProjectBalance;
    for (const QJsonValue &value : balanceList)
    {
        const QJsonObject balance = value.toObject();
        const QString acctCode = textFromJsonValue(
            balance.value(QStringLiteral("AcctCode")));
        if (balanceRemovalCandidates.contains(acctCode)
            && anyProjectCodeHasValue(balance))
        {
            accountsWithProjectBalance.insert(acctCode);
        }
    }

    if (accountsWithProjectBalance.isEmpty())
        return true;

    QMap<QString, int> removedByAccount;
    QJsonArray filteredBalances;
    for (const QJsonValue &value : balanceList)
    {
        const QJsonObject balance = value.toObject();
        const QString acctCode = textFromJsonValue(
            balance.value(QStringLiteral("AcctCode")));
        if (accountsWithProjectBalance.contains(acctCode)
            && allProjectCodesBlank(balance))
        {
            ++summary->removedBalanceCount;
            removedByAccount[acctCode] = removedByAccount.value(acctCode) + 1;
            continue;
        }
        filteredBalances.append(value);
    }
    if (filteredBalances.size() != balanceList.size())
        root->insert(QStringLiteral("GLBalanceList"), filteredBalances);

    for (auto it = removedByAccount.constBegin(); it != removedByAccount.constEnd(); ++it)
    {
        summary->removedBalanceDetails.append(
            QStringLiteral("\u79d1\u76ee %1\uff1a\u5220\u9664 %2 \u6761\u6838\u7b97\u9879\u76ee\u5168\u7a7a\u7684\u671f\u521d\u8bb0\u5f55")
                .arg(it.key())
                .arg(it.value()));
    }
    return true;
}

bool RepairAccountSetWidget::repairJsonFile(const RepairAccountSetInfo &info,
                                            const QMap<QString, QString> &conflictResolutions,
                                            QString *outputPath,
                                            RepairPipelineSummary *pipelineSummary,
                                            int *voucherDateRepairCount,
                                            QString *errorMessage) const
{
    if (voucherDateRepairCount)
        *voucherDateRepairCount = 0;
    RepairPipelineSummary localSummary;
    if (!pipelineSummary)
        pipelineSummary = &localSummary;
    *pipelineSummary = RepairPipelineSummary();
    pipelineSummary->accountSetName = info.asName;

    QFile file(info.jsonPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u6253\u5f00\u5931\u8d25");
        return false;
    }

    QJsonParseError jsonError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("JSON \u89e3\u6790\u5931\u8d25");
        return false;
    }

    QJsonObject root = doc.object();

    // Remove conflicting entries that user did not select to keep
    if (!conflictResolutions.isEmpty())
    {
        QJsonArray accountList = root.value("GLAccountList").toArray();
        QJsonArray filteredAccountList;
        QMap<QString, QSet<QString>> removedNamesByCode;
        QMap<QString, int> removedCountByCode;
        for (const QJsonValue &val : accountList)
        {
            QJsonObject acct = val.toObject();
            const QString code = textFromJsonValue(acct.value("AcctCode"));
            if (conflictResolutions.contains(code))
            {
                const QString keepName = conflictResolutions.value(code);
                const QString currentName = textFromJsonValue(acct.value("AcctName"));
                if (currentName != keepName)
                {
                    removedNamesByCode[code].insert(currentName);
                    removedCountByCode[code] = removedCountByCode.value(code) + 1;
                    continue;
                }
            }
            filteredAccountList.append(acct);
        }
        root["GLAccountList"] = filteredAccountList;

        if (pipelineSummary)
        {
            for (auto it = conflictResolutions.constBegin(); it != conflictResolutions.constEnd(); ++it)
            {
                SubjectConflictRepairRecord record;
                record.acctCode = it.key();
                record.keptName = it.value();
                record.removedNames = removedNamesByCode.value(it.key()).values();
                record.removedNames.sort();
                record.removedRecordCount = removedCountByCode.value(it.key());
                pipelineSummary->removedConflictRecordCount += record.removedRecordCount;
                pipelineSummary->conflictRecords.append(record);
            }
        }
    }

    // The dedicated date-repair entry must not change unrelated account names.
    if (!m_dateRepairOnly)
    {
        QJsonArray currentList = root.value("GLAccountList").toArray();
        QList<SiblingRenameRecord> localRecords;
        QJsonArray fixedList = fixSiblingNameDuplicates(
            currentList, collectUsedAccountCodes(root), localRecords);
        if (!localRecords.isEmpty())
        {
            root["GLAccountList"] = fixedList;
            if (pipelineSummary)
                pipelineSummary->siblingRenameRecords = localRecords;
        }
    }

    if (!m_dateRepairOnly && !m_siblingNameRepairOnly)
    {
        if (!repairAllowNullAaTypes(&root, pipelineSummary, errorMessage))
            return false;
    }

    if (m_dateRepairOnly)
    {
        QJsonArray repairedVoucherList;
        const QJsonArray voucherList = root.value("GLVoucherList").toArray();

        for (const QJsonValue &value : voucherList)
        {
            QJsonObject voucher = value.toObject();
            const QString rawMakeDate = voucher.value("MakeDate").toString();
            QString correctedMakeDate;
            QDate makeDate;
            const bool dateWasCorrected = clampDateToMonthEnd(rawMakeDate, &correctedMakeDate, &makeDate);
            if (dateWasCorrected)
            {
                voucher["MakeDate"] = correctedMakeDate;
                if (voucherDateRepairCount)
                    ++(*voucherDateRepairCount);
            }
            else
            {
                makeDate = dateFromJsonValue(rawMakeDate);
            }
            if (makeDate.isValid() && dateWasCorrected)
            {
                voucher["IntYear"] = makeDate.year();
                voucher["IntPeriod"] = makeDate.month();
            }
            repairedVoucherList.append(voucher);
        }

        if (!voucherList.isEmpty())
            root["GLVoucherList"] = repairedVoucherList;
    }

    QDir outDir(repairedDir());
    if (!outDir.exists() && !outDir.mkpath("."))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u521b\u5efa\u4fee\u590d\u76ee\u5f55\u5931\u8d25");
        return false;
    }

    const QString repairContent = m_dateRepairOnly
        ? QStringLiteral("凭证日期异常修复")
        : m_siblingNameRepairOnly
            ? QStringLiteral("同级科目重名修复")
            : QStringLiteral("账套修复");
    const QString outputBaseName = RepairFileNameUtils::makeBaseName(
        info.asName, QFileInfo(info.jsonPath).completeBaseName(), repairContent);
    const QString outPath = outDir.absoluteFilePath(outputBaseName + QStringLiteral(".json"));
    QSaveFile saveFile(outPath);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u5199\u5165\u4fee\u590d\u6587\u4ef6\u5931\u8d25");
        return false;
    }

    saveFile.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!saveFile.commit())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u4fdd\u5b58\u4fee\u590d\u6587\u4ef6\u5931\u8d25");
        return false;
    }

    if (outputPath)
        *outputPath = outPath;
    if (pipelineSummary)
        pipelineSummary->outputPath = outPath;
    return true;
}

QList<int> RepairAccountSetWidget::selectedRows() const
{
    QList<int> rows;
    for (int i = 0; i < m_pAccountTable->rowCount(); ++i)
    {
        QTableWidgetItem *item = m_pAccountTable->item(i, 0);
        if (item && item->checkState() == Qt::Checked && i < m_accountSetList.size() && m_accountSetList[i].valid)
            rows.append(i);
    }
    return rows;
}

void RepairAccountSetWidget::onSelectAllChanged(bool checked)
{
    for (int i = 0; i < m_pAccountTable->rowCount(); ++i)
    {
        if (i >= m_accountSetList.size() || !m_accountSetList[i].valid)
            continue;

        QTableWidgetItem *item = m_pAccountTable->item(i, 0);
        if (item)
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
    m_pBtnSelectAll->setText(checked ? QStringLiteral("\u53d6\u6d88\u5168\u9009") : QStringLiteral("\u5168\u9009"));
}

void RepairAccountSetWidget::onRefreshClicked()
{
    AppLogger::operation(QStringLiteral("修复账套刷新目录"));
    loadAccountSetList();
}

void RepairAccountSetWidget::runRepairPipeline()
{
    SessionTaskGuard sessionTask(this);
    AppLogger::operation(QStringLiteral("\u4fee\u590d\u8d26\u5957\u56fa\u5b9a\u6d41\u6c34\u7ebf\u5f00\u59cb"));
    const QList<int> rows = selectedRows();
    if (rows.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("\u63d0\u793a"),
                             QStringLiteral("\u8bf7\u5148\u9009\u62e9\u8981\u4fee\u590d\u7684\u8d26\u5957"));
        return;
    }

    QList<RepairPipelineSummary> reports;
    QStringList outputPaths;
    QStringList errors;

    for (int row : rows)
    {
        if (row < 0 || row >= m_accountSetList.size())
            continue;

        const RepairAccountSetInfo &info = m_accountSetList.at(row);
        QTableWidgetItem *statusItem = m_pAccountTable->item(row, 4);
        const auto showProgress = [this, statusItem, &info](const QString &step) {
            const QString text = QStringLiteral("\u300c%1\u300d%2").arg(info.asName, step);
            if (statusItem)
            {
                statusItem->setText(step);
                statusItem->setForeground(QColor(124, 189, 255));
            }
            if (m_pDropLabel)
                m_pDropLabel->setText(text);
            QApplication::processEvents();
        };

        showProgress(QStringLiteral("\u6b65\u9aa4 1/3\uff1a\u68c0\u67e5\u79d1\u76ee\u7f16\u7801\u51b2\u7a81"));
        QFile sourceFile(info.jsonPath);
        if (!sourceFile.open(QIODevice::ReadOnly))
        {
            errors.append(info.asName + QStringLiteral("\uff1a\u6253\u5f00 AccountSet.json \u5931\u8d25"));
            if (statusItem)
                statusItem->setText(QStringLiteral("\u4fee\u590d\u5931\u8d25"));
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument sourceDocument = QJsonDocument::fromJson(sourceFile.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !sourceDocument.isObject())
        {
            errors.append(info.asName + QStringLiteral("\uff1aJSON \u89e3\u6790\u5931\u8d25\uff1a") + parseError.errorString());
            if (statusItem)
                statusItem->setText(QStringLiteral("\u4fee\u590d\u5931\u8d25"));
            continue;
        }

        QMap<QString, QString> conflictResolutions;
        const QList<SubjectConflict> conflicts = detectSubjectConflicts(
            sourceDocument.object().value(QStringLiteral("GLAccountList")).toArray());
        if (!conflicts.isEmpty())
        {
            SubjectConflictDialog dialog(conflicts, info.asName, this);
            if (dialog.exec() != QDialog::Accepted)
            {
                m_accountSetList[row].status = QStringLiteral("\u7528\u6237\u53d6\u6d88");
                if (statusItem)
                {
                    statusItem->setText(m_accountSetList[row].status);
                    statusItem->setForeground(QColor(156, 171, 194));
                }
                continue;
            }
            conflictResolutions = dialog.selectedNames();
        }

        showProgress(QStringLiteral("\u6b65\u9aa4 2/3\uff1a\u5904\u7406\u540c\u7ea7\u79d1\u76ee\u91cd\u540d"));
        showProgress(QStringLiteral("\u6b65\u9aa4 3/3\uff1a\u8f85\u52a9\u6838\u7b97\u8865\u975e\u5fc5\u5f55"));

        QString outputPath;
        QString errorMessage;
        int unusedVoucherDateRepairCount = 0;
        RepairPipelineSummary report;
        if (!repairJsonFile(info,
                            conflictResolutions,
                            &outputPath,
                            &report,
                            &unusedVoucherDateRepairCount,
                            &errorMessage))
        {
            errors.append(info.asName + QStringLiteral("\uff1a") + errorMessage);
            m_accountSetList[row].status = QStringLiteral("\u4fee\u590d\u5931\u8d25");
            if (statusItem)
            {
                statusItem->setText(m_accountSetList[row].status);
                statusItem->setForeground(QColor(255, 139, 162));
            }
            AppLogger::error(QStringLiteral("\u4fee\u590d\u8d26\u5957\u6d41\u6c34\u7ebf\u5931\u8d25 asName=%1 error=%2")
                .arg(info.asName, errorMessage));
            continue;
        }

        reports.append(report);
        outputPaths.append(outputPath);
        m_accountSetList[row].status = QStringLiteral("\u5df2\u4fee\u590d\uff1a\u51b2\u7a81%1/\u91cd\u547d\u540d%2/\u8f85\u52a9%3")
            .arg(report.conflictRecords.size())
            .arg(report.siblingRenameRecords.size())
            .arg(report.appendedAaTypeCount);
        if (statusItem)
        {
            statusItem->setText(m_accountSetList[row].status);
            statusItem->setToolTip(m_accountSetList[row].status);
            statusItem->setForeground(QColor(110, 224, 177));
        }
        AppLogger::operation(QStringLiteral("\u4fee\u590d\u8d26\u5957\u6d41\u6c34\u7ebf\u6210\u529f asName=%1 conflicts=%2 renamed=%3 missingAa=%4 appendedAa=%5 removedBalances=%6 output=%7")
            .arg(info.asName)
            .arg(report.conflictRecords.size())
            .arg(report.siblingRenameRecords.size())
            .arg(report.missingAaItemCount)
            .arg(report.appendedAaTypeCount)
            .arg(report.removedBalanceCount)
            .arg(QDir::toNativeSeparators(outputPath)));
    }

    int totalConflictGroups = 0;
    int totalConflictRows = 0;
    int totalSiblingRenames = 0;
    int totalVouchers = 0;
    int totalMissingAaItems = 0;
    int totalAppendedAaTypes = 0;
    int totalRemovedBalances = 0;
    QStringList details;

    for (const RepairPipelineSummary &report : reports)
    {
        totalConflictGroups += report.conflictRecords.size();
        totalConflictRows += report.removedConflictRecordCount;
        totalSiblingRenames += report.siblingRenameRecords.size();
        totalVouchers += report.scannedVoucherCount;
        totalMissingAaItems += report.missingAaItemCount;
        totalAppendedAaTypes += report.appendedAaTypeCount;
        totalRemovedBalances += report.removedBalanceCount;

        details.append(QStringLiteral("\u3010%1\u3011").arg(report.accountSetName));
        details.append(QStringLiteral("1. \u79d1\u76ee\u7f16\u7801\u51b2\u7a81\uff1a%1 \u7ec4\uff0c\u5220\u9664 %2 \u6761\u51b2\u7a81\u8bb0\u5f55")
            .arg(report.conflictRecords.size())
            .arg(report.removedConflictRecordCount));
        if (report.conflictRecords.isEmpty())
            details.append(QStringLiteral("   \u672a\u53d1\u73b0\u79d1\u76ee\u7f16\u7801\u51b2\u7a81"));
        for (const SubjectConflictRepairRecord &record : report.conflictRecords)
        {
            details.append(QStringLiteral("   %1\uff1a\u4fdd\u7559\u300c%2\u300d\uff0c\u5220\u9664\u300c%3\u300d\uff08%4 \u6761\uff09")
                .arg(record.acctCode,
                     record.keptName,
                     record.removedNames.join(QStringLiteral("\u3001")))
                .arg(record.removedRecordCount));
        }

        details.append(QStringLiteral("2. \u540c\u7ea7\u79d1\u76ee\u91cd\u547d\u540d\uff1a%1 \u4e2a").arg(report.siblingRenameRecords.size()));
        if (report.siblingRenameRecords.isEmpty())
            details.append(QStringLiteral("   \u672a\u53d1\u73b0\u9700\u8981\u91cd\u547d\u540d\u7684\u540c\u7ea7\u79d1\u76ee"));
        for (const SiblingRenameRecord &record : report.siblingRenameRecords)
        {
            details.append(QStringLiteral("   %1\uff1a\u300c%2\u300d -> \u300c%3\u300d")
                .arg(record.acctCode, record.originalName, record.newName));
        }

        details.append(QStringLiteral("3. \u8f85\u52a9\u6838\u7b97\u8865\u975e\u5fc5\u5f55\uff1a\u626b\u63cf %1 \u6761\u51ed\u8bc1\uff0c\u53d1\u73b0\u7f3a\u9879 %2 \u5904\uff0c\u65b0\u589e\u975e\u5fc5\u5f55\u914d\u7f6e %3 \u9879\uff0c\u5220\u9664\u7a7a\u6838\u7b97\u671f\u521d %4 \u6761")
            .arg(report.scannedVoucherCount)
            .arg(report.missingAaItemCount)
            .arg(report.appendedAaTypeCount)
            .arg(report.removedBalanceCount));
        if (report.allowNullAaRecords.isEmpty())
            details.append(QStringLiteral("   \u672a\u65b0\u589e\u8f85\u52a9\u6838\u7b97\u975e\u5fc5\u5f55\u914d\u7f6e"));
        for (const AllowNullAaRepairRecord &record : report.allowNullAaRecords)
        {
            details.append(QStringLiteral("   %1 %2\uff1aAllowNullAATypes \u65b0\u589e [%3]")
                .arg(record.acctCode,
                     record.acctName,
                     record.appendedAaTypes.join(QStringLiteral(", "))));
        }
        for (const QString &balanceDetail : report.removedBalanceDetails)
            details.append(QStringLiteral("   ") + balanceDetail);
        details.append(QStringLiteral("\u8f93\u51fa\uff1a%1").arg(QDir::toNativeSeparators(report.outputPath)));
        details.append(QString());
    }

    if (!errors.isEmpty())
    {
        details.append(QStringLiteral("\u3010\u5904\u7406\u5931\u8d25\u3011"));
        for (const QString &error : errors)
            details.append(QStringLiteral("- ") + error);
    }

    const QString summaryText = QStringLiteral(
        "\u6210\u529f\u5904\u7406 %1 \u4e2a\u8d26\u5957\uff0c\u5931\u8d25 %2 \u4e2a\u3002\n"
        "1. \u79d1\u76ee\u7f16\u7801\u51b2\u7a81\uff1a%3 \u7ec4\uff0c\u5220\u9664 %4 \u6761\n"
        "2. \u540c\u7ea7\u79d1\u76ee\u91cd\u547d\u540d\uff1a%5 \u4e2a\n"
        "3. \u8f85\u52a9\u6838\u7b97\uff1a\u626b\u63cf %6 \u6761\u51ed\u8bc1\uff0c\u53d1\u73b0\u7f3a\u9879 %7 \u5904\uff0c\u65b0\u589e\u975e\u5fc5\u5f55 %8 \u9879\uff0c\u5220\u9664\u7a7a\u671f\u521d %9 \u6761")
        .arg(reports.size())
        .arg(errors.size())
        .arg(totalConflictGroups)
        .arg(totalConflictRows)
        .arg(totalSiblingRenames)
        .arg(totalVouchers)
        .arg(totalMissingAaItems)
        .arg(totalAppendedAaTypes)
        .arg(totalRemovedBalances);

    const QString fullReport = summaryText + QStringLiteral("\n\n") + details.join(QLatin1Char('\n'));
    if (m_pResultEdit)
    {
        m_pResultEdit->setPlainText(fullReport);
        m_pResultEdit->setVisible(true);
    }
    if (m_pDropLabel)
        m_pDropLabel->setText(summaryText);

    if (reports.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("\u4fee\u590d\u5931\u8d25"), fullReport);
        return;
    }

    QMessageBox uploadQuestion(QMessageBox::Question,
                               QStringLiteral("\u4fee\u590d\u5b8c\u6210\uff0c\u662f\u5426\u4e0a\u4f20"),
                               summaryText + QStringLiteral("\n\n\u662f\u5426\u4e0a\u4f20\u4fee\u590d\u540e\u7684\u8d26\u5957\uff1f"),
                               QMessageBox::Yes | QMessageBox::No,
                               this);
    uploadQuestion.setDefaultButton(QMessageBox::No);
    uploadQuestion.setDetailedText(details.join(QLatin1Char('\n')));
    if (uploadQuestion.exec() != QMessageBox::Yes)
        return;

    int uploadSuccess = 0;
    QStringList uploadErrors;
    for (const QString &repairedJsonPath : outputPaths)
    {
        QFile repairedFile(repairedJsonPath);
        if (!repairedFile.open(QIODevice::ReadOnly))
        {
            uploadErrors.append(QFileInfo(repairedJsonPath).fileName() + QStringLiteral("\uff1a\u6253\u5f00\u5931\u8d25"));
            continue;
        }
        const QJsonDocument repairedDocument = QJsonDocument::fromJson(repairedFile.readAll());
        if (!repairedDocument.isObject())
        {
            uploadErrors.append(QFileInfo(repairedJsonPath).fileName() + QStringLiteral("\uff1aJSON \u65e0\u6548"));
            continue;
        }

        const QJsonObject accountSet = repairedDocument.object()
            .value(QStringLiteral("AccountSet")).toObject();
        QString asId = textFromJsonValue(accountSet.value(QStringLiteral("FromAsId")));
        if (asId.isEmpty())
            asId = textFromJsonValue(accountSet.value(QStringLiteral("AsId")));
        const QString asName = accountSet.value(QStringLiteral("AsName")).toString();

        QString zipPath;
        QString zipError;
        if (!zipRepairedFile(repairedJsonPath, &zipPath, &zipError))
        {
            uploadErrors.append(asName + QStringLiteral("\uff1a\u538b\u7f29\u5931\u8d25 - ") + zipError);
            continue;
        }

        QString uploadError;
        if (uploadRepairedFile(asId,
                               asName,
                               QFileInfo(zipPath).completeBaseName(),
                               zipPath,
                               &uploadError))
        {
            ++uploadSuccess;
        }
        else
        {
            uploadErrors.append(asName + QStringLiteral("\uff1a") + uploadError);
        }
    }

    QString uploadResultText;
    if (uploadErrors.isEmpty())
    {
        uploadResultText = QStringLiteral("\u5df2\u6210\u529f\u4e0a\u4f20 %1 \u4e2a\u8d26\u5957\u3002").arg(uploadSuccess);
        QMessageBox::information(this, QStringLiteral("\u4e0a\u4f20\u6210\u529f"), uploadResultText);
    }
    else
    {
        uploadResultText = QStringLiteral("\u4e0a\u4f20\u6210\u529f %1 \u4e2a\uff0c\u5931\u8d25 %2 \u4e2a\uff1a\n%3")
            .arg(uploadSuccess)
            .arg(uploadErrors.size())
            .arg(uploadErrors.join(QLatin1Char('\n')));
        QMessageBox::warning(this, QStringLiteral("\u4e0a\u4f20\u7ed3\u679c"), uploadResultText);
    }
    if (m_pResultEdit)
        m_pResultEdit->append(QStringLiteral("\n\u3010\u4e0a\u4f20\u7ed3\u679c\u3011\n") + uploadResultText);
}

void RepairAccountSetWidget::onRepairClicked()
{
    if (!m_dateRepairOnly && !m_siblingNameRepairOnly)
    {
        runRepairPipeline();
        return;
    }

    AppLogger::operation(QStringLiteral("修复账套点击修复"));
    const QList<int> rows = selectedRows();
    if (rows.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("\u63d0\u793a"),
                             QStringLiteral("\u8bf7\u5148\u9009\u62e9\u8981\u4fee\u590d\u7684\u8d26\u5957"));
        return;
    }

    int successCount = 0;
    int totalVoucherDateRepairCount = 0;
    QStringList outputPaths;
    QStringList errors;
    QList<SiblingRenameRecord> allRenameRecords;

    for (int row : rows)
    {
        if (row < 0 || row >= m_accountSetList.size())
            continue;

        const RepairAccountSetInfo &info = m_accountSetList[row];
        AppLogger::operation(QStringLiteral("修复账套开始处理 asName=%1 file=%2")
            .arg(info.asName, QDir::toNativeSeparators(info.jsonPath)));

        // The comprehensive repair pipeline is handled above. Dedicated pages
        // only execute their single repair operation here.
        QMap<QString, QString> conflictResolutions;

        QString outputPath;
        QString errorMessage;
        RepairPipelineSummary pipelineSummary;
        int voucherDateRepairCount = 0;
        if (repairJsonFile(info,
                           conflictResolutions,
                           &outputPath,
                           &pipelineSummary,
                           &voucherDateRepairCount,
                           &errorMessage))
        {
            AppLogger::operation(QStringLiteral("修复账套处理成功 asName=%1 output=%2 voucherDateRepairs=%3")
                .arg(info.asName, QDir::toNativeSeparators(outputPath))
                .arg(voucherDateRepairCount));
            ++successCount;
            outputPaths.append(QDir::toNativeSeparators(outputPath));
            m_accountSetList[row].status = QStringLiteral("\u5df2\u4fee\u590d");
            allRenameRecords.append(pipelineSummary.siblingRenameRecords);
            totalVoucherDateRepairCount += voucherDateRepairCount;
        }
        else
        {
            AppLogger::error(QStringLiteral("修复账套处理失败 asName=%1 error=%2")
                .arg(info.asName, errorMessage));
            errors.append(info.asName + ": " + errorMessage);
            m_accountSetList[row].status = QStringLiteral("\u4fee\u590d\u5931\u8d25");
        }

        QTableWidgetItem *statusItem = m_pAccountTable->item(row, 4);
        if (statusItem)
        {
            statusItem->setText(m_accountSetList[row].status);
            statusItem->setForeground(m_accountSetList[row].status == QStringLiteral("\u5df2\u4fee\u590d")
                ? QColor(110, 224, 177)
                : QColor(255, 139, 162));
        }
    }

    if (!errors.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("\u90e8\u5206\u8d26\u5957\u4fee\u590d\u5931\u8d25"),
                             errors.join("\n"));
        return;
    }

    if (successCount > 0)
    {
        QString msg = QStringLiteral("\u5df2\u4fee\u590d %1 \u4e2a\u8d26\u5957\uff0c\u6587\u4ef6\u5df2\u8f93\u51fa\u5230\uff1a\n%2")
                          .arg(successCount)
                          .arg(outputPaths.join("\n"));

        if (totalVoucherDateRepairCount > 0)
        {
            msg += QStringLiteral("\n\n\u51ed\u8bc1\u65e5\u671f\u5f02\u5e38\u4fee\u590d\uff1a%1 \u6761\uff08\u5df2\u6539\u4e3a\u5f53\u6708\u6700\u540e\u4e00\u5929\uff09")
                       .arg(totalVoucherDateRepairCount);
        }

        if (!allRenameRecords.isEmpty())
        {
            msg += QStringLiteral("\n\n\u5144\u5f1f\u79d1\u76ee\u540d\u79f0\u91cd\u590d\u4fee\u590d\uff08\u5171 %1 \u9879\uff09\uff1a")
                       .arg(allRenameRecords.size());
            for (const SiblingRenameRecord &rec : allRenameRecords)
            {
                msg += QStringLiteral("\n  %1: %2 -> %3")
                           .arg(rec.acctCode, rec.originalName, rec.newName);
            }
        }

        QMessageBox::information(this,
            m_dateRepairOnly
                ? QStringLiteral("\u51ed\u8bc1\u65e5\u671f\u5f02\u5e38\u4fee\u590d")
                : m_siblingNameRepairOnly
                    ? QStringLiteral("同级科目重名处理")
                    : QStringLiteral("\u4fee\u590d\u8d26\u5957"),
            msg);

        // Ask user whether to upload the repaired files
        QMessageBox::StandardButton uploadBtn = QMessageBox::question(this,
            QStringLiteral("\u4e0a\u4f20\u786e\u8ba4"),
            QStringLiteral("\u662f\u5426\u9700\u8981\u4e0a\u4f20\u4fee\u590d\u540e\u7684\u8d26\u5957\uff1f"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

        if (uploadBtn == QMessageBox::Yes)
        {
            int uploadSuccess = 0;
            QStringList uploadErrors;

            for (int i = 0; i < outputPaths.size(); ++i)
            {
                QString repairedJsonPath = QDir::fromNativeSeparators(outputPaths[i]);

                // Read AsId and AsName from the repaired JSON
                QFile rFile(repairedJsonPath);
                if (!rFile.open(QIODevice::ReadOnly))
                {
                    uploadErrors.append(QFileInfo(repairedJsonPath).fileName() + ": open failed");
                    continue;
                }
                QJsonDocument rDoc = QJsonDocument::fromJson(rFile.readAll());
                rFile.close();
                if (!rDoc.isObject())
                {
                    uploadErrors.append(QFileInfo(repairedJsonPath).fileName() + ": invalid json");
                    continue;
                }
                QJsonObject rRoot = rDoc.object();
                QJsonObject rAccountSet = rRoot.value("AccountSet").toObject();
                QString asId = rAccountSet.value("FromAsId").toString();
                QString asName = rAccountSet.value("AsName").toString();
                if (asId.isEmpty())
                    asId = rAccountSet.value("AsId").toString();

                // Zip the repaired JSON
                QString zipPath;
                QString zipError;
                if (!zipRepairedFile(repairedJsonPath, &zipPath, &zipError))
                {
                    uploadErrors.append(asName + ": zip failed - " + zipError);
                    continue;
                }

                QString zipFileName = QFileInfo(zipPath).completeBaseName();

                // Upload
                QString uploadError;
                if (uploadRepairedFile(asId, asName, zipFileName, zipPath, &uploadError))
                {
                    ++uploadSuccess;
                }
                else
                {
                    uploadErrors.append(asName + ": " + uploadError);
                }
            }

            if (uploadSuccess > 0 && uploadErrors.isEmpty())
            {
                QMessageBox::information(this, QStringLiteral("\u4e0a\u4f20\u6210\u529f"),
                    QStringLiteral("\u5df2\u6210\u529f\u4e0a\u4f20 %1 \u4e2a\u8d26\u5957").arg(uploadSuccess));
            }
            else if (uploadSuccess > 0 && !uploadErrors.isEmpty())
            {
                QMessageBox::warning(this, QStringLiteral("\u90e8\u5206\u4e0a\u4f20\u5931\u8d25"),
                    QStringLiteral("\u6210\u529f %1 \u4e2a\uff0c\u5931\u8d25 %2 \u4e2a\uff1a\n%3")
                        .arg(uploadSuccess).arg(uploadErrors.size()).arg(uploadErrors.join("\n")));
            }
            else
            {
                QMessageBox::critical(this, QStringLiteral("\u4e0a\u4f20\u5931\u8d25"),
                    uploadErrors.join("\n"));
            }
        }
    }
}

void RepairAccountSetWidget::onBackClicked()
{
    AppLogger::operation(QStringLiteral("修复账套返回主界面"));
    emit backToMain();
}

bool RepairAccountSetWidget::zipRepairedFile(const QString &jsonFilePath, QString *zipFilePath, QString *errorMessage) const
{
    QFileInfo jsonInfo(jsonFilePath);
    if (!jsonInfo.exists())
    {
        if (errorMessage)
            *errorMessage = "repaired json file not found";
        return false;
    }

    QString outZip = jsonInfo.absolutePath() + "/" + jsonInfo.completeBaseName() + ".zip";

    const QString script =
        "$ErrorActionPreference='Stop';"
        "Add-Type -AssemblyName System.IO.Compression;"
        "Add-Type -AssemblyName System.IO.Compression.FileSystem;"
        "$src=$env:LEMON_SRC_PATH;$dst=$env:LEMON_DST_PATH;"
        "if([string]::IsNullOrWhiteSpace($src)){throw 'src empty'}"
        "if([string]::IsNullOrWhiteSpace($dst)){throw 'dst empty'}"
        "if(Test-Path -LiteralPath $dst){Remove-Item -LiteralPath $dst -Force}"
        "$zip=[System.IO.Compression.ZipFile]::Open($dst,[System.IO.Compression.ZipArchiveMode]::Create);"
        "try{"
        "  $name='AccountSet.json';"
        "  [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$src,$name)|Out-Null"
        "}finally{$zip.Dispose()}";

    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LEMON_SRC_PATH", jsonInfo.absoluteFilePath());
    env.insert("LEMON_DST_PATH", outZip);
    process.setProcessEnvironment(env);
    const QStringList args = QStringList()
        << "-NoProfile"
        << "-ExecutionPolicy"
        << "Bypass"
        << "-Command"
        << script;
    AppLogger::operation(QStringLiteral("修复账套压缩 JSON json=%1 zip=%2")
        .arg(QDir::toNativeSeparators(jsonInfo.absoluteFilePath()), QDir::toNativeSeparators(outZip)));
    AppLogger::command("powershell.exe", args);
    process.start("powershell.exe", args);

    if (!process.waitForFinished(60000))
    {
        process.kill();
        process.waitForFinished(3000);
        if (errorMessage)
            *errorMessage = "zip creation timed out";
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        QString stdErr = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (errorMessage)
            *errorMessage = stdErr.isEmpty() ? "zip creation failed" : stdErr;
        return false;
    }

    if (zipFilePath)
        *zipFilePath = outZip;
    return true;
}

bool RepairAccountSetWidget::uploadRepairedFile(const QString &asId, const QString &asName, const QString &zipFileName, const QString &zipFilePath, QString *errorMessage)
{
    ImportPackageUploadRequest request;
    request.asId = asId;
    request.asName = asName;
    request.zipFileName = zipFileName;
    request.zipFilePath = zipFilePath;
    request.serviceId = m_pGlobalData ? m_pGlobalData->m_nServiceId : 0;
    request.rewriteProServiceId = true;
    request.serviceType = ImportPackageServiceType::Professional;
    if (m_pGlobalData)
    {
        request.accessToken = m_pGlobalData->m_strNingMengCookie;
        request.ningMengCookie = m_pGlobalData->m_strNingMengCookie;
    }

    const ImportPackageUploadResult result = ImportPackageUploader::upload(request);
    if (!result.ok)
    {
        if (errorMessage)
            *errorMessage = result.errorMessage;
        return false;
    }

    qDebug() << "Upload success for" << asName << "importId:" << result.importId;
    return true;
}

void RepairAccountSetWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (hasZipFile(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void RepairAccountSetWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (hasZipFile(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void RepairAccountSetWidget::dropEvent(QDropEvent *event)
{
    if (!event->possibleActions().testFlag(Qt::CopyAction))
    {
        event->ignore();
        return;
    }
    if (!hasZipFile(event->mimeData()))
    {
        event->ignore();
        return;
    }

    int successCount = 0;
    QStringList errors;
    for (const QUrl &url : event->mimeData()->urls())
    {
        if (!url.isLocalFile())
            continue;

        const QString zipPath = url.toLocalFile();
        AppLogger::operation(QStringLiteral("修复账套拖入 ZIP path=%1").arg(QDir::toNativeSeparators(zipPath)));
        QFileInfo fileInfo(zipPath);
        if (!fileInfo.isFile() || fileInfo.suffix().compare("zip", Qt::CaseInsensitive) != 0)
            continue;

        QString errorMessage;
        const QString inputPath = RepairInputFileUtils::copyToInputDirectory(zipPath, &errorMessage);
        if (!inputPath.isEmpty())
        {
            ++successCount;
        }
        else
        {
            errors.append(fileInfo.fileName() + ": " + errorMessage);
        }
    }

    loadAccountSetList();
    event->setDropAction(Qt::CopyAction);
    event->accept();

    if (!errors.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("部分压缩包导入失败"),
                             errors.join("\n"));
    }
    else if (successCount > 0)
    {
        QMessageBox::information(this, QStringLiteral("\u5df2\u5bfc\u5165"),
                                 QStringLiteral("已留存 %1 个压缩包到共享输入目录并重新扫描。").arg(successCount));
    }
}
