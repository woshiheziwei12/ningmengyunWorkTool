#include "Common/repairinputfileutils.h"
#include "emptysubjectnamewidget.h"
#include "Common/globaldata.h"
#include "Common/logger.h"
#include "Common/repairfilenameutils.h"
#include "Common/repairinputfilepickerwidget.h"
#include "Common/repairpathconfigwidget.h"
#include "Common/repairpathsettings.h"

#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
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
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
bool isZipFile(const QString &filePath)
{
    const QFileInfo info(filePath);
    return info.isFile() && info.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0;
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

bool runPowerShell(const QString &script,
                   const QProcessEnvironment &environment,
                   QString *errorMessage)
{
    QProcess process;
    process.setProcessEnvironment(environment);
    const QStringList arguments = QStringList()
        << QStringLiteral("-NoProfile")
        << QStringLiteral("-ExecutionPolicy")
        << QStringLiteral("Bypass")
        << QStringLiteral("-Command")
        << script;
    AppLogger::command(QStringLiteral("powershell.exe"), arguments);
    process.start(QStringLiteral("powershell.exe"), arguments);

    if (!process.waitForFinished(60000))
    {
        process.kill();
        process.waitForFinished(3000);
        if (errorMessage)
            *errorMessage = QStringLiteral("处理 ZIP 超时");
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        const QString standardError = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (errorMessage)
            *errorMessage = standardError.isEmpty() ? QStringLiteral("处理 ZIP 失败") : standardError;
        return false;
    }
    return true;
}

bool extractAccountSetJson(const QString &zipPath,
                           const QString &jsonPath,
                           QString *errorMessage)
{
    const QString script = QStringLiteral(
        "$ErrorActionPreference='Stop';"
        "Add-Type -AssemblyName System.IO.Compression;"
        "Add-Type -AssemblyName System.IO.Compression.FileSystem;"
        "$zip=$env:LEMON_EMPTY_NAME_ZIP;"
        "$dest=$env:LEMON_EMPTY_NAME_JSON;"
        "$archive=[System.IO.Compression.ZipFile]::OpenRead($zip);"
        "try {"
        "  $entry=$archive.Entries | Where-Object { [System.IO.Path]::GetFileName($_.FullName) -ieq 'AccountSet.json' } | Select-Object -First 1;"
        "  if($null -eq $entry){throw 'AccountSet.json not found in ZIP'}"
        "  if(Test-Path -LiteralPath $dest){Remove-Item -LiteralPath $dest -Force}"
        "  [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$dest);"
        "} finally { $archive.Dispose() }");

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LEMON_EMPTY_NAME_ZIP"), QFileInfo(zipPath).absoluteFilePath());
    environment.insert(QStringLiteral("LEMON_EMPTY_NAME_JSON"), QFileInfo(jsonPath).absoluteFilePath());
    return runPowerShell(script, environment, errorMessage);
}

bool replaceAccountSetJson(const QString &zipPath,
                           const QString &jsonPath,
                           QString *errorMessage)
{
    const QString script = QStringLiteral(
        "$ErrorActionPreference='Stop';"
        "Add-Type -AssemblyName System.IO.Compression;"
        "Add-Type -AssemblyName System.IO.Compression.FileSystem;"
        "$zipPath=$env:LEMON_EMPTY_NAME_ZIP;"
        "$src=$env:LEMON_EMPTY_NAME_JSON;"
        "$archive=[System.IO.Compression.ZipFile]::Open($zipPath,[System.IO.Compression.ZipArchiveMode]::Update);"
        "try {"
        "  $entry=$archive.Entries | Where-Object { [System.IO.Path]::GetFileName($_.FullName) -ieq 'AccountSet.json' } | Select-Object -First 1;"
        "  if($null -eq $entry){throw 'AccountSet.json not found in ZIP'}"
        "  $entryName=$entry.FullName;"
        "  $entry.Delete();"
        "  [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$src,$entryName,[System.IO.Compression.CompressionLevel]::Optimal) | Out-Null;"
        "} finally { $archive.Dispose() }");

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LEMON_EMPTY_NAME_ZIP"), QFileInfo(zipPath).absoluteFilePath());
    environment.insert(QStringLiteral("LEMON_EMPTY_NAME_JSON"), QFileInfo(jsonPath).absoluteFilePath());
    return runPowerShell(script, environment, errorMessage);
}

QString uniqueOutputPath(const QString &directoryPath, const QString &baseName)
{
    QDir directory(directoryPath);
    QString candidate = directory.filePath(baseName + QStringLiteral(".zip"));
    for (int suffix = 2; QFileInfo::exists(candidate); ++suffix)
    {
        candidate = directory.filePath(baseName + QStringLiteral("_%1.zip").arg(suffix));
    }
    return candidate;
}
}

EmptySubjectNameWidget::EmptySubjectNameWidget(GlobalData *globalData,
                                               QWidget *parent,
                                               RepairMode repairMode)
    : QWidget(parent),
      m_pGlobalData(globalData),
      m_repairMode(repairMode),
      m_pOutputDirLabel(nullptr),
      m_pPathConfig(nullptr),
      m_pInputFilePicker(nullptr),
      m_pDropLabel(nullptr),
      m_pStatusLabel(nullptr),
      m_pLogEdit(nullptr),
      m_pBtnBrowse(nullptr),
      m_pBtnOutputDir(nullptr),
      m_pBtnBack(nullptr),
      m_batchProcessing(false),
      m_batchSuccessCount(0),
      m_batchFailureCount(0),
      m_batchNoChangeCount(0)
{
    setAcceptDrops(true);
    setupUi();
    updateOutputDirLabel();
    refreshInputFiles();
}

void EmptySubjectNameWidget::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    QLabel *titleLabel = new QLabel(featureTitle(), this);
    titleLabel->setObjectName(QStringLiteral("labelPageTitle"));
    layout->addWidget(titleLabel);

    const QString description = m_repairMode == TrimNameWhitespace
        ? QStringLiteral("去除科目列表中 AcctName 左右两侧的空格、制表符和换行符。名称中间的内容不会修改。")
        : QStringLiteral("检查数据包中的科目列表；科目名称为空、缺失或仅包含空格时，使用该科目编码填充。已有科目名称不会修改。");
    QLabel *descriptionLabel = new QLabel(description, this);
    descriptionLabel->setWordWrap(true);
    layout->addWidget(descriptionLabel);

    m_pPathConfig = new RepairPathConfigWidget(this);
    m_pPathConfig->setPathsChangedCallback([this]() { refreshInputFiles(); });
    layout->addWidget(m_pPathConfig);

    m_pInputFilePicker = new RepairInputFilePickerWidget(
        QStringList() << QStringLiteral("*.zip"), this);
    m_pInputFilePicker->setProcessCallback(
        [this](const QStringList &paths) { processSelectedFiles(paths); });
    m_pInputFilePicker->setFilesChangedCallback([this](int count) {
        if (!m_batchProcessing && m_pStatusLabel)
            m_pStatusLabel->setText(QStringLiteral("\u5df2\u626b\u63cf\u8f93\u5165\u76ee\u5f55\uff0c\u53d1\u73b0 %1 \u4e2a ZIP").arg(count));
    });
    layout->addWidget(m_pInputFilePicker);

    m_pDropLabel = new QLabel(
        QStringLiteral("将账套 ZIP 压缩包拖入此处，系统会自动修复并生成新的 ZIP"), this);
    m_pDropLabel->setObjectName(QStringLiteral("labelRepairDropArea"));
    m_pDropLabel->setAlignment(Qt::AlignCenter);
    m_pDropLabel->setMinimumHeight(90);
    m_pDropLabel->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(m_pDropLabel);

    m_pStatusLabel = new QLabel(QStringLiteral("等待选择数据包"), this);
    m_pStatusLabel->setWordWrap(true);
    layout->addWidget(m_pStatusLabel);

    m_pLogEdit = new QTextEdit(this);
    m_pLogEdit->setReadOnly(true);
    m_pLogEdit->setPlaceholderText(QStringLiteral("处理结果会显示在这里"));
    layout->addWidget(m_pLogEdit, 1);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    m_pBtnBack = new QPushButton(QStringLiteral("返回"), this);
    m_pBtnBack->setProperty("grayButton", true);
    m_pBtnBrowse = new QPushButton(QStringLiteral("选择 ZIP 并修复"), this);
    buttonLayout->addWidget(m_pBtnBack);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_pBtnBrowse);
    layout->addLayout(buttonLayout);

    connect(m_pBtnBack, &QPushButton::clicked, this, &EmptySubjectNameWidget::onBackClicked);
    connect(m_pBtnBrowse, &QPushButton::clicked, this, &EmptySubjectNameWidget::onBrowseClicked);
}

QString EmptySubjectNameWidget::defaultOutputDir() const
{
    return RepairPathSettings::outputDir();
}

QString EmptySubjectNameWidget::outputDir() const
{
    return RepairPathSettings::outputDir();
}

QString EmptySubjectNameWidget::featureTitle() const
{
    return m_repairMode == TrimNameWhitespace
        ? QStringLiteral("科目名称两侧空格清理")
        : QStringLiteral("空科目名称修复");
}

void EmptySubjectNameWidget::updateOutputDirLabel()
{
    if (m_pPathConfig)
        m_pPathConfig->refreshPaths();
}

void EmptySubjectNameWidget::setRunning(bool running, const QString &statusText)
{
    setProperty("sessionBusysetRunning", running);
    m_pBtnBrowse->setEnabled(!running);
    if (m_pPathConfig)
        m_pPathConfig->setEnabled(!running);
    if (m_pInputFilePicker)
        m_pInputFilePicker->setRunning(running);
    m_pBtnBack->setEnabled(!running);
    if (!statusText.isEmpty())
        m_pStatusLabel->setText(statusText);
    QApplication::processEvents();
}

void EmptySubjectNameWidget::refreshInputFiles()
{
    if (m_pPathConfig)
        m_pPathConfig->refreshPaths();
    if (m_pInputFilePicker)
        m_pInputFilePicker->refreshFiles();
}

void EmptySubjectNameWidget::processSelectedFiles(const QStringList &zipPaths)
{
    if (zipPaths.isEmpty())
        return;

    m_batchProcessing = true;
    m_batchSuccessCount = 0;
    m_batchFailureCount = 0;
    m_batchNoChangeCount = 0;
    m_batchResults.clear();
    m_pLogEdit->clear();
    setRunning(true, QStringLiteral("\u6b63\u5728\u6309\u987a\u5e8f\u5904\u7406 %1 \u4e2a ZIP...").arg(zipPaths.size()));
    for (int i = 0; i < zipPaths.size(); ++i)
    {
        m_pLogEdit->append(QStringLiteral("\n===== [%1/%2] %3 =====")
            .arg(i + 1)
            .arg(zipPaths.size())
            .arg(QFileInfo(zipPaths.at(i)).fileName()));
        processZip(zipPaths.at(i));
    }
    m_batchProcessing = false;
    setRunning(false, QStringLiteral("\u6279\u91cf\u5904\u7406\u5b8c\u6210\uff1a\u6210\u529f %1\uff0c\u65e0\u9700\u4fee\u590d %2\uff0c\u5931\u8d25 %3")
        .arg(m_batchSuccessCount)
        .arg(m_batchNoChangeCount)
        .arg(m_batchFailureCount));
    const QString summary = QStringLiteral("\u6279\u91cf\u5904\u7406\u5b8c\u6210\uff1a\n\u6210\u529f\uff1a%1\n\u65e0\u9700\u4fee\u590d\uff1a%2\n\u5931\u8d25\uff1a%3\n\n%4")
        .arg(m_batchSuccessCount)
        .arg(m_batchNoChangeCount)
        .arg(m_batchFailureCount)
        .arg(m_batchResults.join(QLatin1Char('\n')));
    QMessageBox::information(this, featureTitle() + QStringLiteral("批量处理"), summary);
}

void EmptySubjectNameWidget::processZip(const QString &zipPath)
{
    if (!isZipFile(zipPath))
    {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请选择有效的 ZIP 压缩包"));
        return;
    }

    setRunning(true, QStringLiteral("正在处理：%1").arg(QFileInfo(zipPath).fileName()));
    if (!m_batchProcessing)
        m_pLogEdit->clear();
    m_pLogEdit->append(QStringLiteral("输入：%1").arg(QDir::toNativeSeparators(zipPath)));
    AppLogger::operation(QStringLiteral("%1开始 zip=%2")
        .arg(featureTitle(), QDir::toNativeSeparators(zipPath)));

    QString errorMessage;
    QTemporaryDir temporaryDir;
    if (!temporaryDir.isValid())
    {
        errorMessage = QStringLiteral("创建临时目录失败");
    }

    const QString jsonPath = temporaryDir.filePath(QStringLiteral("AccountSet.json"));
    if (errorMessage.isEmpty())
        extractAccountSetJson(zipPath, jsonPath, &errorMessage);

    QJsonDocument document;
    if (errorMessage.isEmpty())
    {
        QFile jsonFile(jsonPath);
        if (!jsonFile.open(QIODevice::ReadOnly))
        {
            errorMessage = QStringLiteral("读取 AccountSet.json 失败：%1").arg(jsonFile.errorString());
        }
        else
        {
            QJsonParseError parseError;
            document = QJsonDocument::fromJson(jsonFile.readAll(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject())
                errorMessage = QStringLiteral("AccountSet.json 解析失败：%1").arg(parseError.errorString());
        }
    }

    int repairCount = 0;
    QStringList repairedCodes;
    QStringList repairDetails;
    QString accountSetName;
    if (errorMessage.isEmpty())
    {
        QJsonObject root = document.object();
        accountSetName = root.value(QStringLiteral("AccountSet"))
            .toObject()
            .value(QStringLiteral("AsName"))
            .toString()
            .trimmed();
        if (!root.value(QStringLiteral("GLAccountList")).isArray())
        {
            errorMessage = QStringLiteral("未找到 GLAccountList 科目列表");
        }
        else
        {
            const QJsonArray accountList = root.value(QStringLiteral("GLAccountList")).toArray();
            QJsonArray repairedAccountList;
            for (const QJsonValue &value : accountList)
            {
                if (!value.isObject())
                {
                    repairedAccountList.append(value);
                    continue;
                }
                QJsonObject account = value.toObject();
                const QString accountName = account.value(QStringLiteral("AcctName")).toVariant().toString();
                const QString accountCode = account.value(QStringLiteral("AcctCode")).toVariant().toString();
                const QString trimmedName = accountName.trimmed();
                if (m_repairMode == TrimNameWhitespace && accountName != trimmedName)
                {
                    account[QStringLiteral("AcctName")] = trimmedName;
                    ++repairCount;
                    repairedCodes.append(accountCode);
                    repairDetails.append(QStringLiteral("%1：[%2] -> [%3]")
                        .arg(accountCode, accountName, trimmedName));
                }
                else if (m_repairMode == FillEmptyName
                         && trimmedName.isEmpty()
                         && !accountCode.trimmed().isEmpty())
                {
                    account[QStringLiteral("AcctName")] = accountCode;
                    ++repairCount;
                    repairedCodes.append(accountCode);
                    repairDetails.append(QStringLiteral("%1：空名称 -> %1").arg(accountCode));
                }
                repairedAccountList.append(account);
            }
            root[QStringLiteral("GLAccountList")] = repairedAccountList;
            document.setObject(root);
        }
    }

    if (errorMessage.isEmpty() && repairCount == 0)
    {
        if (m_batchProcessing)
        {
            ++m_batchNoChangeCount;
            m_batchResults.append(QFileInfo(zipPath).fileName() + QStringLiteral("：无需修复"));
        }
        else
        {
            setRunning(false, m_repairMode == TrimNameWhitespace
                ? QStringLiteral("未发现科目名称两侧有空白字符，无需生成新文件")
                : QStringLiteral("未发现科目名称为空的数据，无需生成新文件"));
        }
        m_pLogEdit->append(QStringLiteral("未发现需要修复的科目。"));
        if (!m_batchProcessing)
            QMessageBox::information(this, featureTitle(),
                m_repairMode == TrimNameWhitespace
                    ? QStringLiteral("未发现科目名称两侧有空白字符。")
                    : QStringLiteral("未发现科目名称为空的数据。"));
        return;
    }

    if (errorMessage.isEmpty())
    {
        QSaveFile outputJson(jsonPath);
        if (!outputJson.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            errorMessage = QStringLiteral("写入 AccountSet.json 失败：%1").arg(outputJson.errorString());
        }
        else
        {
            outputJson.write(document.toJson(QJsonDocument::Compact));
            if (!outputJson.commit())
                errorMessage = QStringLiteral("保存 AccountSet.json 失败：%1").arg(outputJson.errorString());
        }
    }

    QString outputZipPath;
    if (errorMessage.isEmpty())
    {
        QDir outputDirectory(outputDir());
        if (!outputDirectory.exists() && !outputDirectory.mkpath(QStringLiteral(".")))
        {
            errorMessage = QStringLiteral("创建输出目录失败");
        }
        else
        {
            const QString outputBaseName = RepairFileNameUtils::makeBaseName(
                accountSetName,
                QFileInfo(zipPath).completeBaseName(),
                featureTitle());
            outputZipPath = uniqueOutputPath(outputDirectory.absolutePath(), outputBaseName);
            if (!QFile::copy(zipPath, outputZipPath))
                errorMessage = QStringLiteral("复制原始 ZIP 到输出目录失败");
        }
    }

    if (errorMessage.isEmpty() && !replaceAccountSetJson(outputZipPath, jsonPath, &errorMessage))
    {
        QFile::remove(outputZipPath);
        outputZipPath.clear();
    }

    if (!errorMessage.isEmpty())
    {
        AppLogger::error(QStringLiteral("%1失败 zip=%2 error=%3")
            .arg(featureTitle(), QDir::toNativeSeparators(zipPath), errorMessage));
        if (m_batchProcessing)
        {
            ++m_batchFailureCount;
            m_batchResults.append(QFileInfo(zipPath).fileName() + QStringLiteral("：失败 - ") + errorMessage);
        }
        else
        {
            setRunning(false, QStringLiteral("处理失败：%1").arg(errorMessage));
        }
        m_pLogEdit->append(QStringLiteral("失败：%1").arg(errorMessage));
        if (!m_batchProcessing)
            QMessageBox::critical(this, featureTitle() + QStringLiteral("失败"), errorMessage);
        return;
    }

    m_pLogEdit->append(QStringLiteral("已修复 %1 个科目：%2")
        .arg(repairCount)
        .arg(repairedCodes.join(QStringLiteral("、"))));
    if (!repairDetails.isEmpty())
        m_pLogEdit->append(QStringLiteral("修改明细：\n%1").arg(repairDetails.join(QLatin1Char('\n'))));
    m_pLogEdit->append(QStringLiteral("输出：%1").arg(QDir::toNativeSeparators(outputZipPath)));
    if (m_batchProcessing)
    {
        ++m_batchSuccessCount;
        m_batchResults.append(QStringLiteral("%1：修复 %2 个，输出 %3")
            .arg(QFileInfo(zipPath).fileName())
            .arg(repairCount)
            .arg(QDir::toNativeSeparators(outputZipPath)));
    }
    else
    {
        setRunning(false, QStringLiteral("处理完成：已处理 %1 个科目名称").arg(repairCount));
    }
    AppLogger::operation(QStringLiteral("%1成功 count=%2 output=%3")
        .arg(featureTitle())
        .arg(repairCount)
        .arg(QDir::toNativeSeparators(outputZipPath)));
    if (!m_batchProcessing)
        QMessageBox::information(this, featureTitle() + QStringLiteral("完成"),
            QStringLiteral("已处理 %1 个科目名称。\n\n输出文件：\n%2")
                .arg(repairCount)
                .arg(QDir::toNativeSeparators(outputZipPath)));
}

void EmptySubjectNameWidget::onBrowseClicked()
{
    const QString zipPath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择账套 ZIP"),
        RepairPathSettings::inputDir(),
        QStringLiteral("ZIP 压缩包 (*.zip)"));
    if (!zipPath.isEmpty())
        processZip(zipPath);
}

void EmptySubjectNameWidget::onBrowseOutputDirClicked()
{
    const QString directoryPath = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择输出目录"), outputDir());
    if (!directoryPath.isEmpty())
    {
        RepairPathSettings::setOutputDir(directoryPath);
        updateOutputDirLabel();
    }
}

void EmptySubjectNameWidget::onBackClicked()
{
    emit backToMain();
}

void EmptySubjectNameWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (hasZipFile(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void EmptySubjectNameWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (hasZipFile(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void EmptySubjectNameWidget::dropEvent(QDropEvent *event)
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

    for (const QUrl &url : event->mimeData()->urls())
    {
        if (url.isLocalFile() && isZipFile(url.toLocalFile()))
        {
            const QString inputPath = RepairInputFileUtils::copyDroppedFile(url.toLocalFile(), this);
            if (inputPath.isEmpty())
            {
                event->ignore();
                return;
            }
            refreshInputFiles();
            event->setDropAction(Qt::CopyAction);
            event->accept();
            processZip(inputPath);
            return;
        }
    }
    event->ignore();
}
