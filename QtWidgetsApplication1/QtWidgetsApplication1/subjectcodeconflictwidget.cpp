#include "subjectcodeconflictwidget.h"

#include "Common/globaldata.h"
#include "Common/logger.h"
#include "Common/repairfilenameutils.h"
#include "Common/repairpathconfigwidget.h"
#include "Common/repairpathsettings.h"

#include <algorithm>
#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTemporaryDir>
#include <QUrl>
#include <QVBoxLayout>
#include <QVector>

namespace
{
QString normalizedSubjectName(QString name)
{
    name.replace(QLatin1Char('\r'), QString());
    name.replace(QLatin1Char('\n'), QString());
    return name.trimmed();
}

QString normalizedSubjectCode(const QJsonValue &value)
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
    return QString();
}

int validYear(int year)
{
    return year >= 1900 && year <= 2999 ? year : 0;
}

int yearFromDateValue(const QJsonValue &value)
{
    const QString dateText = value.toVariant().toString().trimmed();
    const QRegularExpressionMatch match =
        QRegularExpression(QStringLiteral("^(\\d{4})")).match(dateText);
    if (match.hasMatch())
    {
        const int year = validYear(match.captured(1).toInt());
        if (year > 0)
            return year;
    }
    return 0;
}

int yearFromRecord(const QJsonObject &record)
{
    const int dateYear =
        yearFromDateValue(record.value(QStringLiteral("MakeDate")));
    if (dateYear > 0)
        return dateYear;
    return validYear(record.value(QStringLiteral("IntYear")).toVariant().toInt());
}

QList<int> sortedYears(const QSet<int> &years)
{
    QList<int> result = years.values();
    std::sort(result.begin(), result.end());
    return result;
}

bool isSupportedFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile())
        return false;
    const QString suffix = info.suffix();
    return suffix.compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0;
}
}

SubjectCodeConflictWidget::SubjectCodeConflictWidget(GlobalData *globalData, QWidget *parent)
    : QWidget(parent),
      m_pGlobalData(globalData),
      m_pOutputLabel(nullptr),
      m_pPathConfig(nullptr),
      m_pDropLabel(nullptr),
      m_pTable(nullptr),
      m_pBtnAddFiles(nullptr),
      m_pBtnOutputDir(nullptr),
      m_pBtnSelectAll(nullptr),
      m_pBtnClear(nullptr),
      m_pBtnView(nullptr),
      m_pBtnProcess(nullptr),
      m_pBtnBack(nullptr),
      m_pTemporaryDir(new QTemporaryDir()),
      m_requestId(0)
{
    setAcceptDrops(true);
    m_pTemporaryDir->setAutoRemove(true);
    setupUi();
    updateOutputLabel();
}

SubjectCodeConflictWidget::~SubjectCodeConflictWidget()
{
    delete m_pTemporaryDir;
}

void SubjectCodeConflictWidget::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    QHBoxLayout *titleLayout = new QHBoxLayout();
    QLabel *title = new QLabel(QStringLiteral("\u79d1\u76ee\u7f16\u7801\u51b2\u7a81\u5904\u7406"), this);
    title->setObjectName(QStringLiteral("labelPageTitle"));
    titleLayout->addWidget(title);
    titleLayout->addStretch();

    m_pBtnSelectAll = new QPushButton(QStringLiteral("\u5168\u9009"), this);
    m_pBtnSelectAll->setCheckable(true);
    m_pBtnSelectAll->setProperty("flatGreen", true);
    m_pBtnSelectAll->setMaximumWidth(80);
    titleLayout->addWidget(m_pBtnSelectAll);

    m_pBtnAddFiles = new QPushButton(QStringLiteral("\u6dfb\u52a0\u6587\u4ef6"), this);
    m_pBtnAddFiles->setMaximumWidth(100);
    titleLayout->addWidget(m_pBtnAddFiles);

    m_pBtnClear = new QPushButton(QStringLiteral("\u6e05\u7a7a\u5217\u8868"), this);
    m_pBtnClear->setMaximumWidth(100);
    titleLayout->addWidget(m_pBtnClear);
    layout->addLayout(titleLayout);

    m_pPathConfig = new RepairPathConfigWidget(this);
    m_pPathConfig->setPathsChangedCallback([this]() { loadInputDirectory(); });
    layout->addWidget(m_pPathConfig);

    m_pDropLabel = new QLabel(
        QStringLiteral("\u5c06 ZIP \u6216 JSON \u62d6\u5165\u6b64\u5904\uff0c\u7cfb\u7edf\u4f1a\u7acb\u5373\u68c0\u67e5\u79d1\u76ee\u7f16\u7801\u51b2\u7a81"),
        this);
    m_pDropLabel->setObjectName(QStringLiteral("labelRepairDropArea"));
    m_pDropLabel->setAlignment(Qt::AlignCenter);
    m_pDropLabel->setMinimumHeight(70);
    m_pDropLabel->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(m_pDropLabel);

    m_pTable = new QTableWidget(this);
    m_pTable->setColumnCount(6);
    m_pTable->setHorizontalHeaderLabels({
        QStringLiteral("\u9009\u62e9"),
        QStringLiteral("\u8d26\u5957\u540d\u79f0"),
        QStringLiteral("\u542f\u7528\u65e5\u671f"),
        QStringLiteral("\u51b2\u7a81\u6570"),
        QStringLiteral("\u6587\u4ef6"),
        QStringLiteral("\u72b6\u6001")
    });
    m_pTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_pTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_pTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_pTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_pTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_pTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    m_pTable->setColumnWidth(0, 55);
    m_pTable->setColumnWidth(2, 115);
    m_pTable->setColumnWidth(3, 80);
    m_pTable->setColumnWidth(5, 120);
    m_pTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pTable->verticalHeader()->setVisible(false);
    m_pTable->verticalHeader()->setDefaultSectionSize(38);
    m_pTable->setAlternatingRowColors(true);
    m_pTable->setShowGrid(false);
    layout->addWidget(m_pTable, 1);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    m_pBtnBack = new QPushButton(QStringLiteral("\u8fd4\u56de"), this);
    m_pBtnBack->setProperty("grayButton", true);
    m_pBtnBack->setMaximumWidth(100);
    m_pBtnBack->setMinimumHeight(36);
    buttonLayout->addWidget(m_pBtnBack);
    buttonLayout->addStretch();

    m_pBtnView = new QPushButton(QStringLiteral("\u67e5\u770b/\u590d\u5236\u51b2\u7a81"), this);
    m_pBtnView->setMaximumWidth(140);
    m_pBtnView->setMinimumHeight(36);
    buttonLayout->addWidget(m_pBtnView);

    m_pBtnProcess = new QPushButton(QStringLiteral("\u5904\u7406\u9009\u4e2d\u8d26\u5957"), this);
    m_pBtnProcess->setMaximumWidth(160);
    m_pBtnProcess->setMinimumHeight(36);
    buttonLayout->addWidget(m_pBtnProcess);
    layout->addLayout(buttonLayout);

    connect(m_pBtnAddFiles, &QPushButton::clicked,
            this, &SubjectCodeConflictWidget::onAddFilesClicked);
    connect(m_pBtnSelectAll, &QPushButton::toggled,
            this, &SubjectCodeConflictWidget::onSelectAllChanged);
    connect(m_pBtnClear, &QPushButton::clicked,
            this, &SubjectCodeConflictWidget::onClearClicked);
    connect(m_pBtnView, &QPushButton::clicked,
            this, &SubjectCodeConflictWidget::onViewClicked);
    connect(m_pBtnProcess, &QPushButton::clicked,
            this, &SubjectCodeConflictWidget::onProcessClicked);
    connect(m_pBtnBack, &QPushButton::clicked,
            this, &SubjectCodeConflictWidget::onBackClicked);
    connect(m_pTable, &QTableWidget::cellDoubleClicked, this,
            [this](int, int) { onViewClicked(); });
}

QString SubjectCodeConflictWidget::defaultOutputDir() const
{
    return RepairPathSettings::outputDir();
}

QString SubjectCodeConflictWidget::outputDir() const
{
    return RepairPathSettings::outputDir();
}

void SubjectCodeConflictWidget::updateOutputLabel()
{
    if (m_pPathConfig)
        m_pPathConfig->refreshPaths();
}

void SubjectCodeConflictWidget::loadInputDirectory()
{
    const QString inputDirectory = QDir::cleanPath(RepairPathSettings::inputDir());
    QDir().mkpath(inputDirectory);

    m_sources.clear();
    delete m_pTemporaryDir;
    m_pTemporaryDir = new QTemporaryDir();
    m_pTemporaryDir->setAutoRemove(true);
    m_requestId = 0;

    const QString inputRoot = inputDirectory + QLatin1Char('/');
    const QString outputRoot = QDir::cleanPath(RepairPathSettings::outputDir())
        + QLatin1Char('/');
    const bool outputIsInputSubdirectory = outputRoot != inputRoot
        && outputRoot.startsWith(inputRoot, Qt::CaseInsensitive);

    QStringList paths;
    QDirIterator iterator(inputDirectory,
                          QStringList() << QStringLiteral("*.zip")
                                        << QStringLiteral("*.json"),
                          QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        const QString path = iterator.next();
        if (outputIsInputSubdirectory
            && QDir::cleanPath(path).startsWith(outputRoot, Qt::CaseInsensitive))
        {
            continue;
        }
        paths.append(path);
    }
    std::sort(paths.begin(), paths.end(), [](const QString &left, const QString &right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });

    for (int index = 0; index < paths.size(); ++index)
    {
        m_pDropLabel->setText(QStringLiteral("正在扫描共享输入目录（%1/%2）：%3")
            .arg(index + 1)
            .arg(paths.size())
            .arg(QFileInfo(paths.at(index)).fileName()));
        QApplication::processEvents();
        addInputPath(paths.at(index), false);
    }
    refreshTable();
    AppLogger::operation(QStringLiteral("科目编码冲突已扫描共享输入目录 dir=%1 files=%2 accountSets=%3")
        .arg(QDir::toNativeSeparators(inputDirectory))
        .arg(paths.size())
        .arg(m_sources.size()));
}

QString SubjectCodeConflictWidget::copyIntoInputDirectory(
    const QString &path,
    QString *errorMessage) const
{
    const QFileInfo sourceInfo(path);
    if (!sourceInfo.exists() || !sourceInfo.isFile() || !isSupportedFile(path))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("文件不存在或格式不支持");
        return QString();
    }

    const QString inputDirectory = QDir::cleanPath(RepairPathSettings::inputDir());
    if (!QDir().mkpath(inputDirectory))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("创建共享输入目录失败");
        return QString();
    }

    const QString sourcePath = QDir::cleanPath(sourceInfo.absoluteFilePath());
    const QString inputRoot = inputDirectory + QLatin1Char('/');
    if (sourcePath.startsWith(inputRoot, Qt::CaseInsensitive))
        return sourcePath;

    QDir inputDir(inputDirectory);
    QString targetPath = inputDir.absoluteFilePath(sourceInfo.fileName());
    int suffixNumber = 2;
    while (QFileInfo::exists(targetPath))
    {
        const QString suffix = sourceInfo.suffix();
        const QString fileName = sourceInfo.completeBaseName()
            + QStringLiteral("_%1").arg(suffixNumber++)
            + (suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix);
        targetPath = inputDir.absoluteFilePath(fileName);
    }

    if (!QFile::copy(sourcePath, targetPath))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("复制文件到共享输入目录失败");
        return QString();
    }
    AppLogger::operation(QStringLiteral("科目编码冲突导入文件 source=%1 target=%2")
        .arg(QDir::toNativeSeparators(sourcePath), QDir::toNativeSeparators(targetPath)));
    return targetPath;
}

void SubjectCodeConflictWidget::onAddFilesClicked()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("\u9009\u62e9\u8d26\u5957\u6587\u4ef6"),
        RepairPathSettings::inputDir(),
        QStringLiteral("\u8d26\u5957\u6587\u4ef6 (*.zip *.json);;ZIP (*.zip);;JSON (*.json)"));
    QStringList errors;
    for (const QString &path : paths)
    {
        QString errorMessage;
        if (copyIntoInputDirectory(path, &errorMessage).isEmpty())
            errors.append(QFileInfo(path).fileName() + QStringLiteral("：") + errorMessage);
    }
    loadInputDirectory();
    if (!errors.isEmpty())
        QMessageBox::warning(this, QStringLiteral("部分文件导入失败"), errors.join(QLatin1Char('\n')));
}

void SubjectCodeConflictWidget::onSelectOutputDirClicked()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("\u9009\u62e9\u5904\u7406\u540e ZIP \u8f93\u51fa\u76ee\u5f55"),
        outputDir());
    if (!dir.isEmpty())
    {
        RepairPathSettings::setOutputDir(dir);
        updateOutputLabel();
    }
}

void SubjectCodeConflictWidget::addInputPath(const QString &path, bool showErrors)
{
    if (!isSupportedFile(path))
        return;

    const QFileInfo info(path);
    if (info.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0)
    {
        addJsonSource(info.absoluteFilePath(), info.absoluteFilePath(), info.completeBaseName());
        return;
    }

    QStringList jsonPaths;
    QString errorMessage;
    if (!extractZip(info.absoluteFilePath(), &jsonPaths, &errorMessage))
    {
        SubjectConflictSource source;
        source.sourcePath = info.absoluteFilePath();
        source.packageBaseName = info.completeBaseName();
        source.accountSetName = info.completeBaseName();
        source.status = QStringLiteral("加载失败");
        source.errorMessage = errorMessage;
        m_sources.append(source);
        if (showErrors)
        {
            QMessageBox::warning(this, QStringLiteral("\u6dfb\u52a0\u5931\u8d25"),
                                 QStringLiteral("%1\n%2")
                                     .arg(QDir::toNativeSeparators(path), errorMessage));
        }
        return;
    }

    for (int i = 0; i < jsonPaths.size(); ++i)
    {
        QString baseName = info.completeBaseName();
        if (jsonPaths.size() > 1)
            baseName += QStringLiteral("_") + QFileInfo(jsonPaths[i]).completeBaseName();
        addJsonSource(jsonPaths[i], info.absoluteFilePath(), baseName);
    }
}

void SubjectCodeConflictWidget::addJsonSource(const QString &jsonPath,
                                               const QString &sourcePath,
                                               const QString &packageBaseName)
{
    const QString absoluteJsonPath = QFileInfo(jsonPath).absoluteFilePath();
    for (const SubjectConflictSource &existing : m_sources)
    {
        if (existing.jsonPath.compare(absoluteJsonPath, Qt::CaseInsensitive) == 0)
            return;
    }

    SubjectConflictSource source;
    source.sourcePath = QFileInfo(sourcePath).absoluteFilePath();
    source.jsonPath = absoluteJsonPath;
    source.packageBaseName = packageBaseName;

    QFile file(absoluteJsonPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        source.status = QStringLiteral("\u6253\u5f00\u5931\u8d25");
        source.errorMessage = file.errorString();
        m_sources.append(source);
        return;
    }

    QJsonParseError parseError;
    source.document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !source.document.isObject())
    {
        source.status = QStringLiteral("JSON \u65e0\u6548");
        source.errorMessage = parseError.errorString();
        m_sources.append(source);
        return;
    }

    const QJsonObject root = source.document.object();
    if (!root.value(QStringLiteral("GLAccountList")).isArray())
    {
        source.status = QStringLiteral("\u7f3a\u5c11\u79d1\u76ee\u5217\u8868");
        source.errorMessage = QStringLiteral("\u672a\u627e\u5230 GLAccountList");
        m_sources.append(source);
        return;
    }

    const QJsonObject accountSet = root.value(QStringLiteral("AccountSet")).toObject();
    source.accountSetName = accountSet.value(QStringLiteral("AsName")).toString().trimmed();
    if (source.accountSetName.isEmpty())
        source.accountSetName = QFileInfo(sourcePath).completeBaseName();
    source.startDate = accountSet.value(QStringLiteral("StartDate")).toVariant().toString();
    source.conflicts = analyzeDocument(source.document);
    source.valid = true;
    source.status = source.conflicts.isEmpty()
        ? QStringLiteral("\u65e0\u51b2\u7a81")
        : QStringLiteral("\u5f85\u5904\u7406");
    m_sources.append(source);

    AppLogger::operation(QStringLiteral("\u79d1\u76ee\u7f16\u7801\u51b2\u7a81\u626b\u63cf asName=%1 conflicts=%2 file=%3")
        .arg(source.accountSetName)
        .arg(source.conflicts.size())
        .arg(QDir::toNativeSeparators(source.sourcePath)));
}

QList<SubjectConflict> SubjectCodeConflictWidget::analyzeDocument(
    const QJsonDocument &document) const
{
    QList<SubjectConflict> result;
    if (!document.isObject())
        return result;

    const QJsonObject root = document.object();
    const QJsonArray accounts = root.value(QStringLiteral("GLAccountList")).toArray();

    QMap<QString, QStringList> namesByCode;
    QMap<QString, QMap<QString, QSet<int>>> yearsByCodeAndName;
    for (const QJsonValue &value : accounts)
    {
        const QJsonObject account = value.toObject();
        const QString code = normalizedSubjectCode(account.value(QStringLiteral("AcctCode")));
        const QString name = normalizedSubjectName(
            account.value(QStringLiteral("AcctName")).toString());
        if (code.isEmpty() || name.isEmpty())
            continue;

        if (!namesByCode[code].contains(name))
            namesByCode[code].append(name);
        const int year = validYear(
            account.value(QStringLiteral("AccountYear")).toVariant().toInt());
        if (year > 0)
            yearsByCodeAndName[code][name].insert(year);
    }

    for (auto it = namesByCode.constBegin(); it != namesByCode.constEnd(); ++it)
    {
        if (it.value().size() <= 1)
            continue;
        SubjectConflict conflict;
        conflict.acctCode = it.key();
        conflict.names = it.value();
        conflict.accountYearDataAvailable = true;
        conflict.usageDataAvailable = true;
        for (const QString &name : conflict.names)
            conflict.accountYearsByName.insert(
                name, sortedYears(yearsByCodeAndName[conflict.acctCode][name]));
        result.append(conflict);
    }

    if (result.isEmpty())
        return result;

    QVector<QSet<int>> voucherYears(result.size());
    QVector<QSet<int>> openingBalanceYears(result.size());
    QVector<QSet<int>> descendantVoucherYears(result.size());
    QVector<QSet<int>> descendantOpeningBalanceYears(result.size());

    const QJsonArray vouchers = root.value(QStringLiteral("GLVoucherList")).toArray();
    for (const QJsonValue &value : vouchers)
    {
        const QJsonObject voucher = value.toObject();
        const QString code = normalizedSubjectCode(voucher.value(QStringLiteral("AcctCode")));
        const int year = yearFromRecord(voucher);
        for (int i = 0; i < result.size(); ++i)
        {
            if (code == result[i].acctCode)
            {
                if (year > 0)
                    voucherYears[i].insert(year);
            }
            else if (code.size() > result[i].acctCode.size()
                     && code.startsWith(result[i].acctCode))
            {
                if (year > 0)
                    descendantVoucherYears[i].insert(year);
            }
        }
    }

    const QJsonArray openingBalances =
        root.value(QStringLiteral("GLBalanceList")).toArray();
    const int openingBalanceYear = yearFromDateValue(
        root.value(QStringLiteral("AccountSet"))
            .toObject()
            .value(QStringLiteral("StartDate")));
    for (const QJsonValue &value : openingBalances)
    {
        const QJsonObject openingBalance = value.toObject();
        const QString code = normalizedSubjectCode(
            openingBalance.value(QStringLiteral("AcctCode")));
        for (int i = 0; i < result.size(); ++i)
        {
            if (code == result[i].acctCode)
            {
                if (openingBalanceYear > 0)
                    openingBalanceYears[i].insert(openingBalanceYear);
                else
                    result[i].hasOpeningBalanceWithUnknownYear = true;
            }
            else if (code.size() > result[i].acctCode.size()
                     && code.startsWith(result[i].acctCode))
            {
                if (openingBalanceYear > 0)
                    descendantOpeningBalanceYears[i].insert(openingBalanceYear);
                else
                    result[i].hasDescendantOpeningBalanceWithUnknownYear = true;
            }
        }
    }

    for (int i = 0; i < result.size(); ++i)
    {
        result[i].voucherYears = sortedYears(voucherYears[i]);
        result[i].openingBalanceYears = sortedYears(openingBalanceYears[i]);
        result[i].descendantVoucherYears = sortedYears(descendantVoucherYears[i]);
        result[i].descendantOpeningBalanceYears =
            sortedYears(descendantOpeningBalanceYears[i]);
    }
    return result;
}

void SubjectCodeConflictWidget::refreshTable()
{
    m_pTable->setRowCount(m_sources.size());
    for (int row = 0; row < m_sources.size(); ++row)
    {
        const SubjectConflictSource &source = m_sources[row];
        QTableWidgetItem *checkItem = new QTableWidgetItem();
        checkItem->setTextAlignment(Qt::AlignCenter);
        checkItem->setCheckState(Qt::Unchecked);
        Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (source.valid && !source.conflicts.isEmpty())
            flags |= Qt::ItemIsUserCheckable;
        checkItem->setFlags(flags);
        m_pTable->setItem(row, 0, checkItem);
        m_pTable->setItem(row, 1, new QTableWidgetItem(source.accountSetName));
        m_pTable->setItem(row, 2, new QTableWidgetItem(source.startDate));

        QTableWidgetItem *countItem =
            new QTableWidgetItem(QString::number(source.conflicts.size()));
        countItem->setTextAlignment(Qt::AlignCenter);
        m_pTable->setItem(row, 3, countItem);

        QTableWidgetItem *fileItem =
            new QTableWidgetItem(QDir::toNativeSeparators(source.sourcePath));
        fileItem->setToolTip(fileItem->text());
        m_pTable->setItem(row, 4, fileItem);

        QTableWidgetItem *statusItem = new QTableWidgetItem(source.status);
        if (!source.valid)
        {
            statusItem->setForeground(QColor(220, 53, 69));
            statusItem->setToolTip(source.errorMessage);
        }
        else if (source.conflicts.isEmpty())
        {
            statusItem->setForeground(QColor(1, 130, 72));
        }
        else if (source.status == QStringLiteral("\u5df2\u5904\u7406"))
        {
            statusItem->setForeground(QColor(1, 130, 72));
            statusItem->setToolTip(QDir::toNativeSeparators(source.outputZipPath));
        }
        m_pTable->setItem(row, 5, statusItem);
    }

    m_pDropLabel->setText(m_sources.isEmpty()
        ? QStringLiteral("\u5c06 ZIP \u6216 JSON \u62d6\u5165\u6b64\u5904\uff0c\u7cfb\u7edf\u4f1a\u7acb\u5373\u68c0\u67e5\u79d1\u76ee\u7f16\u7801\u51b2\u7a81")
        : QStringLiteral("\u5df2\u52a0\u8f7d %1 \u4e2a\u8d26\u5957\uff0c\u53ef\u52fe\u9009\u6709\u51b2\u7a81\u7684\u8d26\u5957\u8fdb\u884c\u5904\u7406")
              .arg(m_sources.size()));
    m_pBtnSelectAll->blockSignals(true);
    m_pBtnSelectAll->setChecked(false);
    m_pBtnSelectAll->setText(QStringLiteral("\u5168\u9009"));
    m_pBtnSelectAll->blockSignals(false);
}

QList<int> SubjectCodeConflictWidget::selectedRows() const
{
    QList<int> rows;
    for (int row = 0; row < m_pTable->rowCount(); ++row)
    {
        const QTableWidgetItem *item = m_pTable->item(row, 0);
        if (item && item->checkState() == Qt::Checked
            && row < m_sources.size()
            && m_sources[row].valid
            && !m_sources[row].conflicts.isEmpty())
        {
            rows.append(row);
        }
    }
    return rows;
}

void SubjectCodeConflictWidget::onSelectAllChanged(bool checked)
{
    for (int row = 0; row < m_pTable->rowCount(); ++row)
    {
        if (row >= m_sources.size() || !m_sources[row].valid
            || m_sources[row].conflicts.isEmpty())
        {
            continue;
        }
        QTableWidgetItem *item = m_pTable->item(row, 0);
        if (item)
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
    m_pBtnSelectAll->setText(
        checked ? QStringLiteral("\u53d6\u6d88\u5168\u9009") : QStringLiteral("\u5168\u9009"));
}

void SubjectCodeConflictWidget::onClearClicked()
{
    m_sources.clear();
    refreshTable();
}

void SubjectCodeConflictWidget::onViewClicked()
{
    const int row = m_pTable->currentRow();
    if (row < 0 || row >= m_sources.size())
    {
        QMessageBox::information(this, QStringLiteral("\u63d0\u793a"),
                                 QStringLiteral("\u8bf7\u5148\u9009\u4e2d\u4e00\u4e2a\u8d26\u5957"));
        return;
    }
    const SubjectConflictSource &source = m_sources[row];
    if (!source.valid)
    {
        QMessageBox::warning(this, QStringLiteral("\u6587\u4ef6\u65e0\u6548"),
                             source.errorMessage);
        return;
    }
    if (source.conflicts.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("\u68c0\u67e5\u7ed3\u679c"),
                                 QStringLiteral("\u8be5\u8d26\u5957\u6ca1\u6709\u79d1\u76ee\u7f16\u7801\u51b2\u7a81"));
        return;
    }
    SubjectConflictDialog dialog(source.conflicts, source.accountSetName, this);
    dialog.exec();
}

void SubjectCodeConflictWidget::onProcessClicked()
{
    const QList<int> rows = selectedRows();
    if (rows.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("\u63d0\u793a"),
                             QStringLiteral("\u8bf7\u5148\u52fe\u9009\u8981\u5904\u7406\u7684\u8d26\u5957"));
        return;
    }

    QStringList outputs;
    QStringList errors;
    for (int row : rows)
    {
        SubjectConflictSource &source = m_sources[row];
        SubjectConflictDialog dialog(source.conflicts, source.accountSetName, this);
        if (dialog.exec() != QDialog::Accepted)
        {
            source.status = QStringLiteral("\u7528\u6237\u53d6\u6d88");
            continue;
        }

        QString outputZipPath;
        QString errorMessage;
        if (processSource(source, dialog.selectedNames(), &outputZipPath, &errorMessage))
        {
            source.status = QStringLiteral("\u5df2\u5904\u7406");
            source.outputZipPath = outputZipPath;
            outputs.append(QDir::toNativeSeparators(outputZipPath));
        }
        else
        {
            source.status = QStringLiteral("\u5904\u7406\u5931\u8d25");
            source.errorMessage = errorMessage;
            errors.append(QStringLiteral("%1\uff1a%2").arg(source.accountSetName, errorMessage));
        }
    }
    refreshTable();

    if (!outputs.isEmpty())
    {
        QMessageBox::information(
            this,
            QStringLiteral("\u5904\u7406\u5b8c\u6210"),
            QStringLiteral("\u5df2\u751f\u6210 %1 \u4e2a\u5904\u7406\u540e\u538b\u7f29\u5305\uff1a\n%2")
                .arg(outputs.size())
                .arg(outputs.join(QStringLiteral("\n"))));
    }
    if (!errors.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("\u90e8\u5206\u5904\u7406\u5931\u8d25"),
                             errors.join(QStringLiteral("\n")));
    }
}

bool SubjectCodeConflictWidget::processSource(
    const SubjectConflictSource &source,
    const QMap<QString, QString> &selectedNames,
    QString *outputZipPath,
    QString *errorMessage)
{
    if (!source.document.isObject())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("JSON \u65e0\u6548");
        return false;
    }
    if (selectedNames.size() != source.conflicts.size())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u5c1a\u6709\u51b2\u7a81\u672a\u9009\u62e9\u4fdd\u7559\u79d1\u76ee");
        return false;
    }

    QJsonObject root = source.document.object();
    const int voucherCount = root.value(QStringLiteral("GLVoucherList")).toArray().size();
    const int openingBalanceCount =
        root.value(QStringLiteral("GLBalanceList")).toArray().size();
    const QJsonArray accounts = root.value(QStringLiteral("GLAccountList")).toArray();
    QJsonArray filteredAccounts;
    for (const QJsonValue &value : accounts)
    {
        const QJsonObject account = value.toObject();
        const QString code = normalizedSubjectCode(account.value(QStringLiteral("AcctCode")));
        if (selectedNames.contains(code))
        {
            const QString name = normalizedSubjectName(
                account.value(QStringLiteral("AcctName")).toString());
            if (name != selectedNames.value(code))
                continue;
        }
        filteredAccounts.append(account);
    }
    root.insert(QStringLiteral("GLAccountList"), filteredAccounts);

    const QJsonDocument processedDocument(root);
    if (!analyzeDocument(processedDocument).isEmpty())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u5904\u7406\u540e\u4ecd\u5b58\u5728\u79d1\u76ee\u7f16\u7801\u51b2\u7a81");
        return false;
    }
    if (root.value(QStringLiteral("GLVoucherList")).toArray().size() != voucherCount
        || root.value(QStringLiteral("GLBalanceList")).toArray().size()
            != openingBalanceCount)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u51ed\u8bc1\u6216\u671f\u521d\u6570\u636e\u6821\u9a8c\u5931\u8d25");
        return false;
    }

    const QString stageDir = QDir::cleanPath(
        m_pTemporaryDir->path() + QStringLiteral("/output_%1").arg(++m_requestId));
    if (!QDir().mkpath(stageDir))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u521b\u5efa\u4e34\u65f6\u76ee\u5f55\u5931\u8d25");
        return false;
    }

    const QString jsonName = QStringLiteral("AccountSet.json");
    const QString stageJsonPath = QDir(stageDir).absoluteFilePath(jsonName);
    QSaveFile outputFile(stageJsonPath);
    if (!outputFile.open(QIODevice::WriteOnly)
        || outputFile.write(processedDocument.toJson(QJsonDocument::Compact)) < 0
        || !outputFile.commit())
    {
        QDir(stageDir).removeRecursively();
        if (errorMessage)
            *errorMessage = QStringLiteral("\u5199\u5165\u4e34\u65f6 JSON \u5931\u8d25");
        return false;
    }

    QDir destination(outputDir());
    if (!destination.exists() && !QDir().mkpath(destination.absolutePath()))
    {
        QDir(stageDir).removeRecursively();
        if (errorMessage)
            *errorMessage = QStringLiteral("\u521b\u5efa\u8f93\u51fa\u76ee\u5f55\u5931\u8d25");
        return false;
    }

    const QString outputBaseName = RepairFileNameUtils::makeBaseName(
        source.accountSetName,
        source.packageBaseName,
        QStringLiteral("科目编码冲突修复"));
    QString zipName = outputBaseName + QStringLiteral(".zip");
    QString zipPath = destination.absoluteFilePath(zipName);
    if (QFileInfo::exists(zipPath))
    {
        zipName = outputBaseName
            + QStringLiteral("_%1.zip")
                  .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmmss")));
        zipPath = destination.absoluteFilePath(zipName);
    }

    const bool zipped = createZipWithSingleJson(stageJsonPath, zipPath, errorMessage);
    QDir(stageDir).removeRecursively();
    if (!zipped)
    {
        if (QFileInfo::exists(zipPath))
            QFile::remove(zipPath);
        return false;
    }

    if (!QFileInfo::exists(zipPath) || QFileInfo(zipPath).size() <= 0)
    {
        if (QFileInfo::exists(zipPath))
            QFile::remove(zipPath);
        if (errorMessage)
            *errorMessage = QStringLiteral("\u8f93\u51fa ZIP \u6821\u9a8c\u5931\u8d25");
        return false;
    }

    if (outputZipPath)
        *outputZipPath = zipPath;
    AppLogger::operation(QStringLiteral("\u79d1\u76ee\u7f16\u7801\u51b2\u7a81\u5904\u7406\u6210\u529f asName=%1 output=%2")
        .arg(source.accountSetName, QDir::toNativeSeparators(zipPath)));
    return true;
}

bool SubjectCodeConflictWidget::extractZip(
    const QString &zipPath,
    QStringList *jsonPaths,
    QString *errorMessage)
{
    if (!m_pTemporaryDir->isValid())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u4e34\u65f6\u76ee\u5f55\u65e0\u6548");
        return false;
    }

    const QString targetDir = QDir::cleanPath(
        m_pTemporaryDir->path() + QStringLiteral("/input_%1").arg(++m_requestId));
    if (!QDir().mkpath(targetDir))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("\u521b\u5efa\u4e34\u65f6\u89e3\u538b\u76ee\u5f55\u5931\u8d25");
        return false;
    }

    const QString script =
        QStringLiteral(
            "$ErrorActionPreference='Stop';"
            "Add-Type -AssemblyName System.IO.Compression;"
            "Add-Type -AssemblyName System.IO.Compression.FileSystem;"
            "$zip=$env:LEMON_ZIP_PATH;$dest=$env:LEMON_ZIP_DEST;"
            "$archive=[System.IO.Compression.ZipFile]::OpenRead($zip);"
            "try{"
            " $entry=$archive.Entries | Where-Object {"
            "  [System.IO.Path]::GetFileName($_.FullName) -ieq 'AccountSet.json'"
            " } | Select-Object -First 1;"
            " if($null -eq $entry){throw 'ZIP 中未找到 AccountSet.json'}"
            " $target=Join-Path $dest 'AccountSet.json';"
            " [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$target);"
            "}finally{$archive.Dispose()}"
            );

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LEMON_ZIP_PATH"), QFileInfo(zipPath).absoluteFilePath());
    environment.insert(QStringLiteral("LEMON_ZIP_DEST"), targetDir);
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
        QDir(targetDir).removeRecursively();
        if (errorMessage)
            *errorMessage = QStringLiteral("\u89e3\u538b\u8d85\u65f6");
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        const QString standardError =
            QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        QDir(targetDir).removeRecursively();
        if (errorMessage)
            *errorMessage = standardError.isEmpty()
                ? QStringLiteral("\u89e3\u538b\u5931\u8d25")
                : standardError;
        return false;
    }

    QStringList found;
    QDirIterator iterator(targetDir, QStringList() << QStringLiteral("*.json"),
                          QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext())
        found.append(iterator.next());
    if (found.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("ZIP \u4e2d\u672a\u627e\u5230 JSON");
        return false;
    }
    if (jsonPaths)
        *jsonPaths = found;
    return true;
}

bool SubjectCodeConflictWidget::createZipWithSingleJson(
    const QString &jsonPath,
    const QString &zipPath,
    QString *errorMessage) const
{
    const QString script =
        QStringLiteral(
            "$ErrorActionPreference='Stop';"
            "Add-Type -AssemblyName System.IO.Compression;"
            "Add-Type -AssemblyName System.IO.Compression.FileSystem;"
            "$src=$env:LEMON_SRC_PATH;$dst=$env:LEMON_DST_PATH;"
            "if(Test-Path -LiteralPath $dst){throw 'Output ZIP already exists'}"
            "$zip=[System.IO.Compression.ZipFile]::Open($dst,[System.IO.Compression.ZipArchiveMode]::Create);"
            "try{"
            " $name=[System.IO.Path]::GetFileName($src);"
            " [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$src,$name)|Out-Null"
            "}finally{$zip.Dispose()}"
            "$check=[System.IO.Compression.ZipFile]::OpenRead($dst);"
            "try{if($check.Entries.Count -ne 1){throw 'ZIP entry validation failed'}}"
            "finally{$check.Dispose()}");

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LEMON_SRC_PATH"), QFileInfo(jsonPath).absoluteFilePath());
    environment.insert(QStringLiteral("LEMON_DST_PATH"), QFileInfo(zipPath).absoluteFilePath());
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
            *errorMessage = QStringLiteral("\u751f\u6210 ZIP \u8d85\u65f6");
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        const QString standardError =
            QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (errorMessage)
            *errorMessage = standardError.isEmpty()
                ? QStringLiteral("\u751f\u6210 ZIP \u5931\u8d25")
                : standardError;
        return false;
    }
    return true;
}

void SubjectCodeConflictWidget::onBackClicked()
{
    emit backToMain();
}

void SubjectCodeConflictWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData() && event->mimeData()->hasUrls())
    {
        for (const QUrl &url : event->mimeData()->urls())
        {
            if (url.isLocalFile() && isSupportedFile(url.toLocalFile()))
            {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void SubjectCodeConflictWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData() && event->mimeData()->hasUrls())
    {
        for (const QUrl &url : event->mimeData()->urls())
        {
            if (url.isLocalFile() && isSupportedFile(url.toLocalFile()))
            {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void SubjectCodeConflictWidget::dropEvent(QDropEvent *event)
{
    if (!event->mimeData() || !event->mimeData()->hasUrls())
        return;
    QStringList errors;
    for (const QUrl &url : event->mimeData()->urls())
    {
        if (!url.isLocalFile() || !isSupportedFile(url.toLocalFile()))
            continue;
        QString errorMessage;
        if (copyIntoInputDirectory(url.toLocalFile(), &errorMessage).isEmpty())
        {
            errors.append(QFileInfo(url.toLocalFile()).fileName()
                          + QStringLiteral("：") + errorMessage);
        }
    }
    loadInputDirectory();
    event->acceptProposedAction();
    if (!errors.isEmpty())
        QMessageBox::warning(this, QStringLiteral("部分文件导入失败"), errors.join(QLatin1Char('\n')));
}
