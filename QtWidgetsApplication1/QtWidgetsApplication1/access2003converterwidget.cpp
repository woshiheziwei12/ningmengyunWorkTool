#include "access2003converterwidget.h"

#include "Common/access2003converter.h"
#include "Common/logger.h"

#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSharedPointer>
#include <QSet>
#include <QTextEdit>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
bool isSupportedFile(const QString &path)
{
    const QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    return info.isFile() && (suffix == QStringLiteral("ais")
                             || suffix == QStringLiteral("aiy")
                             || suffix == QStringLiteral("mdb"));
}

QString canonicalKey(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical).toLower();
}
}

Access2003ConverterWidget::Access2003ConverterWidget(GlobalData *globalData, QWidget *parent)
    : QWidget(parent),
      m_pGlobalData(globalData),
      m_running(false),
      m_outputDirectoryCustomized(false),
      m_pAccessStatusLabel(nullptr),
      m_pSelectionSummaryLabel(nullptr),
      m_pOutputDirectoryEdit(nullptr),
      m_pPasswordEdit(nullptr),
      m_pFileList(nullptr),
      m_pProgressBar(nullptr),
      m_pLogEdit(nullptr),
      m_pSelectFilesButton(nullptr),
      m_pSelectDirectoryButton(nullptr),
      m_pSelectOutputButton(nullptr),
      m_pClearButton(nullptr),
      m_pStartButton(nullptr),
      m_pBackButton(nullptr)
{
    Q_UNUSED(m_pGlobalData)
    setAcceptDrops(true);
    setupUi();
}

void Access2003ConverterWidget::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    QFrame *noticeFrame = new QFrame(this);
    noticeFrame->setObjectName(QStringLiteral("access2003Notice"));
    QVBoxLayout *noticeLayout = new QVBoxLayout(noticeFrame);
    noticeLayout->setContentsMargins(16, 13, 16, 13);
    noticeLayout->setSpacing(5);
    QLabel *noticeTitle = new QLabel(QStringLiteral("调用本机 Access 2003 转换为 Jet 4.0"), noticeFrame);
    noticeTitle->setObjectName(QStringLiteral("labelPageTitle"));
    noticeLayout->addWidget(noticeTitle);
    QLabel *noticeText = new QLabel(
        QStringLiteral("支持 AIS、AIY、MDB；源文件不会被覆盖。转换结果统一命名为：源文件名_03转换后.ais。"),
        noticeFrame);
    noticeText->setWordWrap(true);
    noticeLayout->addWidget(noticeText);
    m_pAccessStatusLabel = new QLabel(noticeFrame);
    m_pAccessStatusLabel->setWordWrap(true);
    QString registrationName;
    const bool registered = Access2003Converter::isAccess2003Registered(&registrationName);
    m_pAccessStatusLabel->setText(registered
        ? QStringLiteral("● 已检测到 %1（Access 2003）").arg(registrationName)
        : QStringLiteral("● 未检测到 Access.Application.11，请先安装或修复 Access 2003"));
    m_pAccessStatusLabel->setProperty("state", registered ? "ready" : "error");
    noticeLayout->addWidget(m_pAccessStatusLabel);
    layout->addWidget(noticeFrame);

    QHBoxLayout *selectLayout = new QHBoxLayout();
    selectLayout->setSpacing(8);
    m_pSelectFilesButton = new QPushButton(QStringLiteral("选择文件"), this);
    m_pSelectFilesButton->setProperty("flatGreen", true);
    m_pSelectDirectoryButton = new QPushButton(QStringLiteral("选择文件夹（含子目录）"), this);
    m_pSelectDirectoryButton->setProperty("flatGreen", true);
    m_pClearButton = new QPushButton(QStringLiteral("清空"), this);
    m_pClearButton->setProperty("grayButton", true);
    selectLayout->addWidget(m_pSelectFilesButton);
    selectLayout->addWidget(m_pSelectDirectoryButton);
    selectLayout->addStretch();
    selectLayout->addWidget(m_pClearButton);
    layout->addLayout(selectLayout);

    m_pSelectionSummaryLabel = new QLabel(QStringLiteral("尚未选择数据库文件，也可以直接拖入文件或文件夹"), this);
    m_pSelectionSummaryLabel->setWordWrap(true);
    layout->addWidget(m_pSelectionSummaryLabel);

    m_pFileList = new QListWidget(this);
    m_pFileList->setObjectName(QStringLiteral("access2003FileList"));
    m_pFileList->setAlternatingRowColors(true);
    m_pFileList->setMinimumHeight(150);
    layout->addWidget(m_pFileList, 1);

    QHBoxLayout *outputLayout = new QHBoxLayout();
    outputLayout->setSpacing(8);
    QLabel *outputLabel = new QLabel(QStringLiteral("输出目录"), this);
    m_pOutputDirectoryEdit = new QLineEdit(this);
    m_pOutputDirectoryEdit->setPlaceholderText(QStringLiteral("请选择输出目录"));
    m_pSelectOutputButton = new QPushButton(QStringLiteral("浏览..."), this);
    m_pSelectOutputButton->setProperty("grayButton", true);
    outputLayout->addWidget(outputLabel);
    outputLayout->addWidget(m_pOutputDirectoryEdit, 1);
    outputLayout->addWidget(m_pSelectOutputButton);
    layout->addLayout(outputLayout);

    QHBoxLayout *passwordLayout = new QHBoxLayout();
    passwordLayout->setSpacing(8);
    QLabel *passwordLabel = new QLabel(QStringLiteral("数据库/Access 密码"), this);
    m_pPasswordEdit = new QLineEdit(this);
    m_pPasswordEdit->setEchoMode(QLineEdit::Password);
    m_pPasswordEdit->setPlaceholderText(QStringLiteral("留空时自动使用内置默认密码；手工输入可覆盖默认值"));
    m_pPasswordEdit->setClearButtonEnabled(true);
    passwordLayout->addWidget(passwordLabel);
    passwordLayout->addWidget(m_pPasswordEdit, 1);
    layout->addLayout(passwordLayout);

    m_pProgressBar = new QProgressBar(this);
    m_pProgressBar->setRange(0, 1);
    m_pProgressBar->setValue(0);
    m_pProgressBar->setTextVisible(true);
    layout->addWidget(m_pProgressBar);

    m_pLogEdit = new QTextEdit(this);
    m_pLogEdit->setObjectName(QStringLiteral("access2003ConversionLog"));
    m_pLogEdit->setReadOnly(true);
    m_pLogEdit->setMinimumHeight(140);
    layout->addWidget(m_pLogEdit, 1);

    QHBoxLayout *actionLayout = new QHBoxLayout();
    m_pBackButton = new QPushButton(QStringLiteral("返回"), this);
    m_pBackButton->setProperty("grayButton", true);
    m_pStartButton = new QPushButton(QStringLiteral("开始批量转换"), this);
    m_pStartButton->setProperty("flatGreen", true);
    m_pStartButton->setEnabled(false);
    actionLayout->addWidget(m_pBackButton);
    actionLayout->addStretch();
    actionLayout->addWidget(m_pStartButton);
    layout->addLayout(actionLayout);

    connect(m_pSelectFilesButton, &QPushButton::clicked,
            this, &Access2003ConverterWidget::onSelectFiles);
    connect(m_pSelectDirectoryButton, &QPushButton::clicked,
            this, &Access2003ConverterWidget::onSelectDirectory);
    connect(m_pSelectOutputButton, &QPushButton::clicked,
            this, &Access2003ConverterWidget::onSelectOutputDirectory);
    connect(m_pClearButton, &QPushButton::clicked,
            this, &Access2003ConverterWidget::onClearFiles);
    connect(m_pStartButton, &QPushButton::clicked,
            this, &Access2003ConverterWidget::onStartConversion);
    connect(m_pBackButton, &QPushButton::clicked, this, [this]() { emit backToMain(); });
    connect(m_pOutputDirectoryEdit, &QLineEdit::textEdited, this, [this]() {
        m_outputDirectoryCustomized = true;
        m_pStartButton->setEnabled(!m_running && !m_sourcePaths.isEmpty()
                                   && !m_pOutputDirectoryEdit->text().trimmed().isEmpty());
    });
}

void Access2003ConverterWidget::onSelectFiles()
{
    const QString startDirectory = m_sourcePaths.isEmpty()
        ? QDir::homePath() : QFileInfo(m_sourcePaths.constFirst()).absolutePath();
    const QStringList paths = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("选择 Access/金蝶数据库文件"),
        startDirectory,
        QStringLiteral("Access/金蝶数据库 (*.ais *.AIS *.aiy *.AIY *.mdb *.MDB)"));
    if (paths.isEmpty())
        return;
    addPaths(paths);
    setSuggestedOutputDirectory(QFileInfo(paths.constFirst()).absolutePath());
}

void Access2003ConverterWidget::onSelectDirectory()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择包含 AIS/AIY/MDB 的文件夹"),
        m_sourcePaths.isEmpty() ? QDir::homePath() : QFileInfo(m_sourcePaths.constFirst()).absolutePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (directory.isEmpty())
        return;

    const QStringList paths = Access2003Converter::collectDatabaseFiles(directory, true);
    if (paths.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("未找到文件"),
                                 QStringLiteral("该目录及其子目录中没有 AIS、AIY 或 MDB 文件。"));
        return;
    }
    addPaths(paths);
    setSuggestedOutputDirectory(directory);
}

void Access2003ConverterWidget::onSelectOutputDirectory()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择转换结果输出目录"),
        m_pOutputDirectoryEdit->text().trimmed().isEmpty()
            ? QDir::homePath() : m_pOutputDirectoryEdit->text().trimmed(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (directory.isEmpty())
        return;
    m_outputDirectoryCustomized = true;
    m_pOutputDirectoryEdit->setText(QDir::toNativeSeparators(QDir::cleanPath(directory)));
    m_pStartButton->setEnabled(!m_running && !m_sourcePaths.isEmpty());
}

void Access2003ConverterWidget::onClearFiles()
{
    if (m_running)
        return;
    m_sourcePaths.clear();
    refreshFileList();
    m_pLogEdit->clear();
    m_pProgressBar->setRange(0, 1);
    m_pProgressBar->setValue(0);
}

void Access2003ConverterWidget::addPaths(const QStringList &paths)
{
    QSet<QString> existing;
    for (const QString &path : m_sourcePaths)
        existing.insert(canonicalKey(path));

    for (const QString &path : paths)
    {
        if (!isSupportedFile(path))
            continue;
        const QString key = canonicalKey(path);
        if (!existing.contains(key))
        {
            existing.insert(key);
            m_sourcePaths.append(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
        }
    }
    m_sourcePaths.sort(Qt::CaseInsensitive);
    refreshFileList();
}

void Access2003ConverterWidget::addDroppedPath(const QString &path)
{
    const QFileInfo info(path);
    if (info.isDir())
    {
        addPaths(Access2003Converter::collectDatabaseFiles(path, true));
        setSuggestedOutputDirectory(info.absoluteFilePath());
    }
    else if (isSupportedFile(path))
    {
        addPaths(QStringList() << path);
        setSuggestedOutputDirectory(info.absolutePath());
    }
}

void Access2003ConverterWidget::setSuggestedOutputDirectory(const QString &sourceDirectory)
{
    if (m_outputDirectoryCustomized || sourceDirectory.trimmed().isEmpty())
        return;
    const QString outputPath = QDir(sourceDirectory).filePath(QStringLiteral("Access2003转换结果"));
    m_pOutputDirectoryEdit->setText(QDir::toNativeSeparators(QDir::cleanPath(outputPath)));
    m_pStartButton->setEnabled(!m_running && !m_sourcePaths.isEmpty());
}

void Access2003ConverterWidget::refreshFileList()
{
    m_pFileList->clear();
    for (const QString &path : m_sourcePaths)
    {
        QListWidgetItem *item = new QListWidgetItem(
            QStringLiteral("%1  →  %2")
                .arg(QDir::toNativeSeparators(path),
                     Access2003Converter::suggestedOutputFileName(path)),
            m_pFileList);
        item->setToolTip(QDir::toNativeSeparators(path));
    }
    m_pSelectionSummaryLabel->setText(m_sourcePaths.isEmpty()
        ? QStringLiteral("尚未选择数据库文件，也可以直接拖入文件或文件夹")
        : QStringLiteral("已选择 %1 个文件；转换过程会按列表顺序执行").arg(m_sourcePaths.size()));
    m_pStartButton->setEnabled(!m_running && !m_sourcePaths.isEmpty()
                               && !m_pOutputDirectoryEdit->text().trimmed().isEmpty());
}

void Access2003ConverterWidget::setRunning(bool running)
{
    m_running = running;
    m_pSelectFilesButton->setEnabled(!running);
    m_pSelectDirectoryButton->setEnabled(!running);
    m_pSelectOutputButton->setEnabled(!running);
    m_pOutputDirectoryEdit->setEnabled(!running);
    m_pPasswordEdit->setEnabled(!running);
    m_pClearButton->setEnabled(!running);
    m_pBackButton->setEnabled(!running);
    setAcceptDrops(!running);
    m_pStartButton->setEnabled(!running && !m_sourcePaths.isEmpty()
                               && !m_pOutputDirectoryEdit->text().trimmed().isEmpty());
}

void Access2003ConverterWidget::onStartConversion()
{
    if (m_running || m_sourcePaths.isEmpty())
        return;
    const QString outputDirectory = QDir::cleanPath(m_pOutputDirectoryEdit->text().trimmed());
    if (outputDirectory.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("缺少输出目录"), QStringLiteral("请先选择输出目录。"));
        return;
    }
    if (!Access2003Converter::isAccess2003Registered())
    {
        QMessageBox::warning(this, QStringLiteral("未检测到 Access 2003"),
                             QStringLiteral("没有找到 Access.Application.11。请确认 Access 2003 已正确安装。"));
        return;
    }

    const QStringList sourcePaths = m_sourcePaths;
    QString password = m_pPasswordEdit->text();
    m_pPasswordEdit->clear();
    QSharedPointer<Access2003BatchResult> result(new Access2003BatchResult);
    setRunning(true);
    m_pProgressBar->setRange(0, 0);
    m_pLogEdit->clear();
    m_pLogEdit->append(QStringLiteral("准备调用 Access 2003，批量转换 %1 个文件...").arg(sourcePaths.size()));
    m_pLogEdit->append(QStringLiteral("输出目录：%1").arg(QDir::toNativeSeparators(outputDirectory)));
    AppLogger::operation(QStringLiteral("启动 Access 2003 批量转换 count=%1 output=%2")
        .arg(sourcePaths.size())
        .arg(QDir::toNativeSeparators(outputDirectory)));

    QThread *thread = QThread::create([result, sourcePaths, outputDirectory, password]() mutable {
        *result = Access2003Converter::convertFiles(sourcePaths, outputDirectory, password);
        password.fill(QChar('\0'));
        password.clear();
    });
    connect(thread, &QThread::finished, this, [this, result, outputDirectory]() {
        setRunning(false);
        m_pProgressBar->setRange(0, qMax(1, result->items.size()));
        m_pProgressBar->setValue(result->items.size());

        if (!result->accessStarted)
        {
            m_pLogEdit->append(QStringLiteral("失败：") + result->startupError);
            m_pSelectionSummaryLabel->setText(QStringLiteral("转换未启动：") + result->startupError);
            QMessageBox::warning(this, QStringLiteral("Access 2003 启动失败"), result->startupError);
            return;
        }

        const QString versionText = result->accessVersion.isEmpty()
            ? QStringLiteral("11.x") : result->accessVersion;
        m_pLogEdit->append(QStringLiteral("Access 版本：%1").arg(versionText));
        int successCount = 0;
        int failureCount = 0;
        for (const Access2003ConversionItem &item : result->items)
        {
            if (item.success)
            {
                ++successCount;
                m_pLogEdit->append(QStringLiteral("成功：%1\n  → %2%3")
                    .arg(QFileInfo(item.sourcePath).fileName(),
                         QDir::toNativeSeparators(item.destinationPath),
                         item.copiedBecauseAlreadyCompatible
                            ? QStringLiteral("（源文件已是 Jet 4.0，安全复制）") : QString()));
            }
            else
            {
                ++failureCount;
                m_pLogEdit->append(QStringLiteral("失败：%1\n  %2")
                    .arg(QFileInfo(item.sourcePath).fileName(), item.errorMessage));
            }
        }

        const QString summary = QStringLiteral("批量转换完成：成功 %1，失败 %2")
            .arg(successCount).arg(failureCount);
        m_pSelectionSummaryLabel->setText(summary);
        m_pLogEdit->append(QStringLiteral("\n%1\n结果目录：%2")
            .arg(summary, QDir::toNativeSeparators(outputDirectory)));
        AppLogger::operation(QStringLiteral("Access 2003 批量转换结束 success=%1 failure=%2 output=%3")
            .arg(successCount).arg(failureCount)
            .arg(QDir::toNativeSeparators(outputDirectory)));
        QMessageBox::information(this, QStringLiteral("Access 2003 批量转换"),
            QStringLiteral("%1\n\n结果目录：\n%2")
                .arg(summary, QDir::toNativeSeparators(outputDirectory)));
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

bool Access2003ConverterWidget::hasSupportedDrop(const QMimeData *mimeData) const
{
    if (!mimeData || !mimeData->hasUrls())
        return false;
    for (const QUrl &url : mimeData->urls())
    {
        if (!url.isLocalFile())
            continue;
        const QFileInfo info(url.toLocalFile());
        if (info.isDir() || isSupportedFile(info.absoluteFilePath()))
            return true;
    }
    return false;
}

void Access2003ConverterWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (!m_running && hasSupportedDrop(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void Access2003ConverterWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (!m_running && hasSupportedDrop(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void Access2003ConverterWidget::dropEvent(QDropEvent *event)
{
    if (m_running || !hasSupportedDrop(event->mimeData()))
    {
        event->ignore();
        return;
    }
    for (const QUrl &url : event->mimeData()->urls())
    {
        if (url.isLocalFile())
            addDroppedPath(url.toLocalFile());
    }
    event->acceptProposedAction();
}
