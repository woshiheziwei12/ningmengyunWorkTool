#include "Common/RestorePreferences.h"
#include <QLabel>
#include "SudaBackupWidget.h"
#include "Common/SudaBackupUnpacker.h"
#include "Common/repairpathconfigwidget.h"
#include "Common/repairpathsettings.h"
#include "Common/repairinputfileutils.h"

#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QShowEvent>
#include <QTextCodec>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include <exception>

namespace
{
QString droppedFile(const QMimeData* data)
{
    if (!data->hasUrls() || data->urls().size() != 1)
        return QString();
    const QUrl url = data->urls().first();
    return url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()
        ? QFileInfo(url.toLocalFile()).absoluteFilePath() : QString();
}
}

SudaBackupWidget::SudaBackupWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("sudaBackupPage"));
    setAcceptDrops(true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);
    auto* title = new QLabel(QStringLiteral("破解速达备份包"), this);
    title->setObjectName(QStringLiteral("labelPageTitle"));
    layout->addWidget(title);
    auto* description = new QLabel(QStringLiteral("选择或拖入一个速达备份包（支持传统备份及 Pro 的 .SDB 备份），解包导出 .bak 文件。"), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto* versionNote = new QLabel(QStringLiteral("导出文件保留原数据库版本，恢复时请使用兼容的 SQL Server。"), this);
    versionNote->setWordWrap(true);
    layout->addWidget(versionNote);

    m_pathConfig = new RepairPathConfigWidget(this);
    m_pathConfig->setPathsChangedCallback([this]() { refreshSharedPaths(); });
    layout->addWidget(m_pathConfig);

    auto* backupRow = new QHBoxLayout;
    m_backupDirectory = new QLabel(this);m_backupDirectory->setWordWrap(true);
    m_backupDirectoryButton = new QPushButton(QStringLiteral("选择备份包目录"), this);
    m_backupDirectoryButton->setProperty("grayButton", true);
    backupRow->addWidget(m_backupDirectory,1);backupRow->addWidget(m_backupDirectoryButton);
    layout->addLayout(backupRow);
    connect(m_backupDirectoryButton,&QPushButton::clicked,this,[this](){
        const QString path=QFileDialog::getExistingDirectory(this,QStringLiteral("选择备份包目录"),RestorePreferences::backupDirectory());
        if(!path.isEmpty()){RestorePreferences::setBackupDirectory(path);refreshSharedPaths();}
    });
    auto* sourceRow = new QHBoxLayout();
    sourceRow->addWidget(new QLabel(QStringLiteral("备份文件"), this));
    m_sourceEdit = new QLineEdit(this);
    m_sourceEdit->setObjectName(QStringLiteral("sudaSourcePath"));
    m_sourceEdit->setAcceptDrops(false);
    m_sourceEdit->setPlaceholderText(QStringLiteral("选择、粘贴路径或拖入一个速达备份文件"));
    m_sourceButton = new QPushButton(QStringLiteral("选择文件"), this);
    m_sourceButton->setProperty("grayButton", true);
    sourceRow->addWidget(m_sourceEdit, 1);
    sourceRow->addWidget(m_sourceButton);
    layout->addLayout(sourceRow);

    auto* outputRow = new QHBoxLayout();
    outputRow->addWidget(new QLabel(QStringLiteral("输出文件"), this));
    m_outputEdit = new QLineEdit(this);
    m_outputEdit->setObjectName(QStringLiteral("sudaOutputPath"));
    m_outputEdit->setAcceptDrops(false);
    m_outputEdit->setPlaceholderText(QStringLiteral("默认保存到共享输出目录：原文件名_解包.bak"));
    m_outputButton = new QPushButton(QStringLiteral("另存为"), this);
    m_outputButton->setProperty("grayButton", true);
    outputRow->addWidget(m_outputEdit, 1);
    outputRow->addWidget(m_outputButton);
    layout->addLayout(outputRow);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    layout->addWidget(m_progress);
    m_log = new QPlainTextEdit(this);
    m_log->setObjectName(QStringLiteral("sudaUnpackLog"));
    m_log->setReadOnly(true);
    m_log->setAcceptDrops(false);
    m_log->viewport()->setAcceptDrops(false);
    m_log->setPlaceholderText(QStringLiteral("解包结果会显示在这里"));
    layout->addWidget(m_log, 1);

    auto* actions = new QHBoxLayout();
    m_backButton = new QPushButton(QStringLiteral("返回"), this);
    m_backButton->setProperty("grayButton", true);
    m_openButton = new QPushButton(QStringLiteral("打开输出目录"), this);
    m_openButton->setEnabled(false);
    m_openButton->setProperty("grayButton", true);
    m_cancelButton = new QPushButton(QStringLiteral("取消"), this);
    m_cancelButton->setEnabled(false);
    m_cancelButton->setObjectName(QStringLiteral("sudaCancelButton"));
    m_cancelButton->setProperty("grayButton", true);
    m_startButton = new QPushButton(QStringLiteral("开始破解"), this);
    m_startButton->setObjectName(QStringLiteral("sudaStartButton"));
    m_startButton->setProperty("flatGreen", true);
    actions->addWidget(m_backButton);
    actions->addStretch();
    actions->addWidget(m_openButton);
    actions->addWidget(m_cancelButton);
    actions->addWidget(m_startButton);
    layout->addLayout(actions);

    connect(m_sourceButton, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("选择速达备份包"),
            RestorePreferences::backupDirectory(), QStringLiteral("速达备份 (*.bak *.BAK *.sdb *.SDB *.sdq *.SDQ);;所有文件 (*)"));
        if (!file.isEmpty())
        {
            RestorePreferences::setBackupDirectory(QFileInfo(file).absolutePath());refreshSharedPaths();
            m_outputCustomized = false;
            m_sourceEdit->setText(QDir::toNativeSeparators(file));
            updateOutputPath();
        }
    });
    connect(m_sourceEdit, &QLineEdit::textChanged, this, [this]() { updateOutputPath(); });
    connect(m_outputEdit, &QLineEdit::textEdited, this, [this]() { m_outputCustomized = true; });
    connect(m_outputButton, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getSaveFileName(this, QStringLiteral("选择输出文件（使用新文件名）"),
            m_outputEdit->text().isEmpty() ? RepairPathSettings::outputDir() : m_outputEdit->text(),
            QStringLiteral("SQL Server 备份 (*.bak)"), nullptr, QFileDialog::DontConfirmOverwrite);
        if (!file.isEmpty())
        {
            RepairPathSettings::setOutputDir(QFileInfo(file).absolutePath());
            refreshSharedPaths();
            m_outputCustomized = true;
            m_outputEdit->setText(QDir::toNativeSeparators(file));
        }
    });
    connect(m_startButton, &QPushButton::clicked, this, &SudaBackupWidget::startUnpack);
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        if (m_cancel)
            m_cancel->store(true);
        m_cancelButton->setEnabled(false);
        m_log->appendPlainText(QStringLiteral("正在取消..."));
    });
    connect(m_backButton, &QPushButton::clicked, this, &SudaBackupWidget::backToMain);
    connect(m_openButton, &QPushButton::clicked, this, [this]() {
        if (!m_completedPath.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_completedPath).absolutePath()));
    });
    refreshSharedPaths();
}

void SudaBackupWidget::refreshSharedPaths()
{
    m_backupDirectory->setText(QStringLiteral("备份包目录：%1").arg(QDir::toNativeSeparators(RestorePreferences::backupDirectory())));
    m_pathConfig->refreshPaths();
    if (m_worker)
        return;
    const QString outputDir = RepairPathSettings::outputDir();
    if (m_sharedOutputDir != outputDir)
    {
        m_sharedOutputDir = outputDir;
        m_outputCustomized = false;
    }
    updateOutputPath();
}

void SudaBackupWidget::updateOutputPath()
{
    if (m_outputCustomized)
        return;
    const QString source = m_sourceEdit->text().trimmed();
    m_outputEdit->setText(source.isEmpty() ? QString() : QDir::toNativeSeparators(
        QDir(RepairPathSettings::outputDir()).filePath(
            QFileInfo(source).completeBaseName() + QStringLiteral("_解包.bak"))));
}

void SudaBackupWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refreshSharedPaths();
}

void SudaBackupWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (!m_worker && !droppedFile(event->mimeData()).isEmpty())
        event->acceptProposedAction();
    else
        event->ignore();
}

void SudaBackupWidget::dropEvent(QDropEvent* event)
{
    if (!event->possibleActions().testFlag(Qt::CopyAction))
    {
        event->ignore();
        return;
    }
    const QString file = droppedFile(event->mimeData());
    if (m_worker || file.isEmpty())
    {
        event->ignore();
        return;
    }
    const QString inputPath = RepairInputFileUtils::copyDroppedFile(file, this);
    if (inputPath.isEmpty())
    {
        event->ignore();
        return;
    }
    m_outputCustomized = false;
    m_sourceEdit->setText(QDir::toNativeSeparators(inputPath));
    updateOutputPath();
    event->setDropAction(Qt::CopyAction);
    event->accept();
}

SudaBackupWidget::~SudaBackupWidget()
{
    if (m_worker)
    {
        m_cancel->store(true);
        m_worker->wait();
    }
}

void SudaBackupWidget::setRunning(bool running)
{
    setProperty("sessionBusysetRunning", running);
    m_pathConfig->setEnabled(!running);
    m_backupDirectoryButton->setEnabled(!running);
    m_sourceEdit->setEnabled(!running);
    m_outputEdit->setEnabled(!running);
    m_sourceButton->setEnabled(!running);
    m_outputButton->setEnabled(!running);
    m_startButton->setEnabled(!running);
    m_cancelButton->setEnabled(running);
    m_backButton->setEnabled(!running);
    m_openButton->setEnabled(!running && !m_completedPath.isEmpty());
    m_progress->setRange(0, running ? 0 : 1);
}

void SudaBackupWidget::startUnpack()
{
    if (m_worker)
        return;
    m_log->clear();
    m_completedPath.clear();
    m_openButton->setEnabled(false);
    m_progress->setValue(0);
    const QString source = m_sourceEdit->text().trimmed();
    QString output = m_outputEdit->text().trimmed();
    if (!QFileInfo(source).isFile() || output.isEmpty())
    {
        m_log->appendPlainText(QStringLiteral("请选择有效的备份文件和输出文件路径。"));
        return;
    }
    if (!output.endsWith(QStringLiteral(".bak"), Qt::CaseInsensitive))
        output += QStringLiteral(".bak");
    output = QFileInfo(output).absoluteFilePath();
    m_outputEdit->setText(QDir::toNativeSeparators(output));
    if (QFileInfo::exists(output))
    {
        m_log->appendPlainText(QStringLiteral("输出文件已存在，请选择新的文件名。"));
        return;
    }
    if (!QDir().mkpath(QFileInfo(output).absolutePath()))
    {
        m_log->appendPlainText(QStringLiteral("无法创建输出目录，请检查路径和目录权限。"));
        return;
    }
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_cancel;
    const auto result = std::make_shared<SudaUnpackResult>();
    setRunning(true);
    m_log->appendPlainText(QStringLiteral("正在解包：%1").arg(QDir::toNativeSeparators(source)));
    m_worker = QThread::create([source, output, cancel, result]() {
        try
        {
            *result = SudaBackupUnpacker::unpack(source.toStdWString(), output.toStdWString(),
                16ULL * 1024 * 1024 * 1024, [cancel]() { return cancel->load(); });
        }
        catch (const std::exception&)
        {
            result->error = "解包失败：系统资源不足或发生异常。";
        }
        catch (...)
        {
            result->error = "解包失败：发生未预期的异常。";
        }
    });
    m_worker->setParent(this);
    connect(m_worker, &QThread::finished, this, [this, result, output]() {
        m_worker = nullptr;
        if (result->success)
            m_completedPath = output;
        setRunning(false);
        m_progress->setValue(result->success ? 1 : 0);
        if (result->success)
        {
            auto* codec = QTextCodec::codecForName("GBK");
            const QString account = codec ? codec->toUnicode(result->accountName.data(),
                static_cast<int>(result->accountName.size())) : QString();
            m_log->appendPlainText(QStringLiteral("解包成功\n账套：%1\n输出：%2\n大小：%3 字节\n可使用兼容原数据库版本的 SQL Server 还原。")
                .arg(account, QDir::toNativeSeparators(output)).arg(result->outputBytes));
        }
        else
        {
            m_log->appendPlainText(QString::fromUtf8(result->error.c_str()));
        }
        emit unpackFinished(result->success, output);
    });
    connect(m_worker, &QThread::finished, m_worker, &QObject::deleteLater);
    m_worker->start();
}
