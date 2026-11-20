#include "BakRestoreWidget.h"
#include "Common/RestorePreferences.h"
#include "Common/SqlRestoreRouting.h"
#include "Common/SqlTargetSelector.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>

namespace {
QLineEdit* namedEdit(QWidget* parent, const char* name, const QString& placeholder)
{
    auto* edit = new QLineEdit(parent);
    edit->setObjectName(QString::fromLatin1(name));
    edit->setPlaceholderText(placeholder);
    return edit;
}
QTableWidgetItem* textItem(const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    item->setToolTip(text);
    return item;
}
QString entryError(const BakBackupEntry& entry)
{
    if (!entry.readError.isEmpty()) return entry.readError;
    if (!entry.analysis.error.isEmpty()) return entry.analysis.error;
    if (entry.backupType != 1) return QStringLiteral("仅支持恢复完整数据库备份");
    if (!entry.analysis.success) return QStringLiteral("尚未通过目标实例校验");
    return QStringLiteral("版本兼容，可选择恢复");
}
}

BakRestoreWidget::BakRestoreWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("bakRestorePage"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    auto* title = new QLabel(QStringLiteral("BAK 文件恢复"), this);
    title->setObjectName(QStringLiteral("labelPageTitle"));
    layout->addWidget(title);
    auto* intro = new QLabel(QStringLiteral("选择一个 BAK 文件或目录，读取每个备份的数据库名称和 SQL Server 版本，再勾选要恢复的备份。目录扫描包含子目录，不进入目录链接。"), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* sourceRow = new QHBoxLayout;
    m_source = namedEdit(this, "bakSource", QStringLiteral("BAK 文件或包含 BAK 的目录"));
    m_fileButton = new QPushButton(QStringLiteral("选择 BAK 文件"), this);
    m_fileButton->setObjectName(QStringLiteral("bakChooseFile"));
    m_directoryButton = new QPushButton(QStringLiteral("选择目录"), this);
    m_directoryButton->setObjectName(QStringLiteral("bakChooseDirectory"));
    sourceRow->addWidget(m_source, 1);
    sourceRow->addWidget(m_fileButton);
    sourceRow->addWidget(m_directoryButton);
    layout->addLayout(sourceRow);

    auto* stagingRow = new QHBoxLayout;
    stagingRow->addWidget(new QLabel(QStringLiteral("备份暂存"), this));
    m_staging = namedEdit(this, "bakStaging", QStringLiteral("本机暂存目录；远程使用双方可访问的 UNC 共享目录"));
    RestorePreferences::bindStaging(m_staging);
    m_stagingButton = new QPushButton(QStringLiteral("选择暂存目录"), this);
    stagingRow->addWidget(m_staging, 1);
    stagingRow->addWidget(m_stagingButton);
    layout->addLayout(stagingRow);
    auto* stagingHint = new QLabel(QStringLiteral("本机优先直接读取源文件；无法访问时使用暂存目录。远程恢复需要可写入且 SQL Server 服务可读取的共享目录。修改暂存目录后可重新校验目标。"), this);
    stagingHint->setWordWrap(true);
    layout->addWidget(stagingHint);
    m_readButton = new QPushButton(QStringLiteral("1. 读取 BAK 清单与版本"), this);
    m_readButton->setObjectName(QStringLiteral("bakRead"));
    m_readButton->setProperty("flatGreen", true);
    layout->addWidget(m_readButton);

    auto* targetBox = new QGroupBox(QStringLiteral("2. 确认恢复实例"), this);
    auto* form = new QFormLayout(targetBox);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_target = new QComboBox(this);
    m_target->setObjectName(QStringLiteral("bakTargetServer"));
    SqlTargetSelector::populate(m_target);
    form->addRow(QStringLiteral("恢复到"), SqlTargetSelector::control(m_target));
    m_auth = new QLabel(this);
    form->addRow(QStringLiteral("验证方式"), m_auth);
    m_driver = new QComboBox(this);
    m_driver->setObjectName(QStringLiteral("bakDriver"));
    m_driver->addItems(T3BackupRestorer::availableDrivers());
    form->addRow(QStringLiteral("ODBC 驱动"), m_driver);
    m_trust = new QCheckBox(QStringLiteral("信任服务器证书"), this);
    m_trust->setChecked(true);
    form->addRow(m_trust);
    m_testButton = new QPushButton(QStringLiteral("测试连接 / 校验所选实例"), this);
    m_testButton->setObjectName(QStringLiteral("bakTestConnection"));
    form->addRow(m_testButton);
    layout->addWidget(targetBox);
    m_summary = new QLabel(QStringLiteral("读取只检查备份信息，不创建或恢复数据库。来源版本未知或与目标不兼容的备份不可恢复。"), this);
    m_summary->setObjectName(QStringLiteral("bakSummary"));
    m_summary->setWordWrap(true);
    m_summary->setTextFormat(Qt::PlainText);
    layout->addWidget(m_summary);

    auto* selectionRow = new QHBoxLayout;
    m_selectAllButton = new QPushButton(QStringLiteral("全选可恢复项"), this);
    m_selectAllButton->setObjectName(QStringLiteral("bakSelectAll"));
    m_selectionSummary = new QLabel(this);
    m_selectionSummary->setObjectName(QStringLiteral("bakSelectionSummary"));
    m_selectionSummary->setWordWrap(true);
    selectionRow->addWidget(m_selectAllButton);
    selectionRow->addWidget(m_selectionSummary, 1);
    layout->addLayout(selectionRow);
    m_table = new QTableWidget(0, ColumnCount, this);
    m_table->setObjectName(QStringLiteral("bakEntries"));
    m_table->setHorizontalHeaderLabels({QStringLiteral("恢复"), QStringLiteral("BAK 文件 / 备份集"),
        QStringLiteral("原数据库"), QStringLiteral("来源 SQL Server 版本"), QStringLiteral("内部版本"),
        QStringLiteral("兼容级别"), QStringLiteral("备份类型"), QStringLiteral("备份时间"),
        QStringLiteral("目标数据库名（可修改）"), QStringLiteral("状态 / 目标兼容性")});
    const int widths[] = {60, 240, 205, 250, 85, 85, 110, 165, 230, 320};
    for (int column = 0; column < ColumnCount; ++column) m_table->setColumnWidth(column, widths[column]);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setDefaultSectionSize(38);
    m_table->setMinimumHeight(240);
    m_table->setWordWrap(false);
    m_table->setAlternatingRowColors(true);
    layout->addWidget(m_table);

    auto* paths = new QFormLayout;
    paths->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_dataDirectory = namedEdit(this, "bakDataDirectory", QStringLiteral("校验目标后自动填写服务器默认数据目录"));
    m_logDirectory = namedEdit(this, "bakLogDirectory", QStringLiteral("SQL Server 所在服务器上的日志目录"));
    paths->addRow(QStringLiteral("服务器数据目录"), m_dataDirectory);
    paths->addRow(QStringLiteral("服务器日志目录"), m_logDirectory);
    layout->addLayout(paths);
    auto* note = new QLabel(QStringLiteral("按原始备份内容恢复数据库，不添加软件账套登记。同名数据库拒绝覆盖；可修改目标库名。读取失败、差异备份和日志备份保留在清单中，仅完整数据库备份可勾选。"), this);
    note->setWordWrap(true);
    layout->addWidget(note);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    layout->addWidget(m_progress);
    m_log = new QPlainTextEdit(this);
    m_log->setObjectName(QStringLiteral("bakRestoreLog"));
    m_log->setReadOnly(true);
    m_log->setMinimumHeight(140);
    layout->addWidget(m_log);
    auto* actions = new QHBoxLayout;
    m_backButton = new QPushButton(QStringLiteral("返回"), this);
    m_stopButton = new QPushButton(QStringLiteral("取消读取"), this);
    m_stopButton->setObjectName(QStringLiteral("bakStop"));
    m_restoreButton = new QPushButton(QStringLiteral("3. 恢复所选数据库"), this);
    m_restoreButton->setObjectName(QStringLiteral("bakRestore"));
    m_restoreButton->setProperty("flatGreen", true);
    actions->addWidget(m_backButton);
    actions->addStretch();
    actions->addWidget(m_stopButton);
    actions->addWidget(m_restoreButton);
    layout->addLayout(actions);
    for (auto* button : {m_fileButton, m_directoryButton, m_stagingButton, m_testButton,
            m_selectAllButton, m_backButton, m_stopButton}) button->setProperty("grayButton", true);

    connect(this, &BakRestoreWidget::operationLog, m_log, &QPlainTextEdit::appendPlainText);
    connect(m_fileButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择 BAK 文件"),
            RestorePreferences::backupDirectory(), QStringLiteral("SQL Server 备份 (*.bak *.BAK)"));
        if (!path.isEmpty()) {
            RestorePreferences::setBackupDirectory(QFileInfo(path).absolutePath());
            m_source->setText(QDir::toNativeSeparators(path));
        }
    });
    connect(m_directoryButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择包含 BAK 的目录"), RestorePreferences::backupDirectory());
        if (!path.isEmpty()) {
            RestorePreferences::setBackupDirectory(path);
            m_source->setText(QDir::toNativeSeparators(path));
        }
    });
    connect(m_stagingButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择备份暂存目录"), m_staging->text());
        if (!path.isEmpty()) m_staging->setText(QDir::toNativeSeparators(path));
    });
    connect(m_source, &QLineEdit::textChanged, this, &BakRestoreWidget::invalidateAnalysis);
    connect(m_readButton, &QPushButton::clicked, this, &BakRestoreWidget::readBackups);
    connect(m_testButton, &QPushButton::clicked, this, &BakRestoreWidget::testConnection);
    connect(m_target, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &BakRestoreWidget::targetChanged);
    connect(m_driver, &QComboBox::currentTextChanged, this, &BakRestoreWidget::targetChanged);
    connect(m_trust, &QCheckBox::toggled, this, &BakRestoreWidget::targetChanged);
    connect(m_table, &QTableWidget::itemChanged, this, [this]() { updateSelectionState(); });
    connect(m_dataDirectory, &QLineEdit::textChanged, this, &BakRestoreWidget::updateSelectionState);
    connect(m_logDirectory, &QLineEdit::textChanged, this, &BakRestoreWidget::updateSelectionState);
    connect(m_selectAllButton, &QPushButton::clicked, this, [this]() {
        bool allChecked = true;
        for (int row = 0; row < m_table->rowCount(); ++row)
            if (eligible(row) && m_table->item(row, Selected)->checkState() != Qt::Checked) allChecked = false;
        const QSignalBlocker blocker(m_table);
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (allChecked) m_table->item(row, Selected)->setCheckState(Qt::Unchecked);
            else if (eligible(row)) m_table->item(row, Selected)->setCheckState(Qt::Checked);
        }
        updateSelectionState();
    });
    connect(m_restoreButton, &QPushButton::clicked, this, &BakRestoreWidget::restoreSelected);
    connect(m_backButton, &QPushButton::clicked, this, &BakRestoreWidget::backToMain);
    connect(m_stopButton, &QPushButton::clicked, this, [this]() {
        if (m_stop) m_stop->store(true);
        m_stopButton->setEnabled(false);
        m_log->appendPlainText(QStringLiteral("已请求停止；正在恢复的数据库会完成后再停止后续项。"));
    });
    targetChanged();
    setBusy(false);
}

BakRestoreWidget::~BakRestoreWidget()
{
    if (m_worker) {
        if (m_stop) m_stop->store(true);
        m_worker->wait();
    }
}

T3SqlConnection BakRestoreWidget::connectionOptions() const
{
    T3SqlConnection options;
    options.server = m_target->currentData().toString();
    options.windowsAuthentication = !SqlTargetSelector::isRemote(m_target);
    if (!options.windowsAuthentication) SqlServerCredentials::apply211(options);
    options.driver = m_driver->currentText();
    options.trustServerCertificate = m_trust->isChecked();
    return options;
}

bool BakRestoreWidget::eligible(int row) const
{
    if (row < 0 || row >= m_batch.entries.size()) return false;
    const auto& entry = m_batch.entries.at(row);
    return entry.analysis.success && BakBackupRestorer::isRestorable(entry);
}

QVector<BakRestoreSelection> BakRestoreWidget::selections() const
{
    QVector<BakRestoreSelection> result;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const auto* check = m_table->item(row, Selected);
        const auto* name = m_table->item(row, TargetDatabase);
        if (check && name && check->checkState() == Qt::Checked) result.append({row, name->text()});
    }
    return result;
}

void BakRestoreWidget::invalidateAnalysis()
{
    if (m_busy) return;
    m_batch = BakBackupBatch();
    m_table->setRowCount(0);
    m_dataDirectory->clear();
    m_logDirectory->clear();
    m_summary->setText(QStringLiteral("点击读取，逐个确认 BAK 来源版本。未读取前不会恢复数据库。"));
    m_progress->setValue(0);
    updateSelectionState();
}

void BakRestoreWidget::setBusy(bool busy, bool restoring)
{
    m_busy = busy;
    setProperty("sessionBusyBakRestore", busy);
    for (QWidget* widget : QList<QWidget*>{m_source, m_fileButton, m_directoryButton, m_staging,
            m_stagingButton, m_readButton, m_target, m_driver, m_trust, m_table,
            m_dataDirectory, m_logDirectory, m_backButton}) widget->setEnabled(!busy);
    m_testButton->setEnabled(!busy && SqlTargetSelector::hasTarget(m_target));
    m_stopButton->setEnabled(busy);
    m_stopButton->setText(restoring ? QStringLiteral("停止后续恢复") : QStringLiteral("取消读取 / 校验"));
    m_progress->setRange(0, busy ? 0 : 1);
    updateSelectionState();
}

void BakRestoreWidget::updateSelectionState()
{
    if (!m_table) return;
    int available = 0, checkedAvailable = 0;
    const QSignalBlocker blocker(m_table);
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto* check = m_table->item(row, Selected);
        if (check) {
            // An invalid selection may be cleared, but an invalid unchecked row cannot be selected.
            check->setFlags(eligible(row) || check->checkState() == Qt::Checked
                ? Qt::ItemIsEnabled | Qt::ItemIsUserCheckable : Qt::NoItemFlags);
        }
        if (eligible(row)) {
            ++available;
            if (check && check->checkState() == Qt::Checked) ++checkedAvailable;
        }
    }
    const auto selected = selections();
    const QString validation = selected.isEmpty() ? QString() : BakBackupRestorer::validateSelections(m_batch, selected);
    m_selectAllButton->setEnabled(!m_busy && (available > 0 || !selected.isEmpty()));
    m_selectAllButton->setText((available > 0 && checkedAvailable == available) || (available == 0 && !selected.isEmpty())
        ? QStringLiteral("取消全选") : QStringLiteral("全选可恢复项"));
    QString summary = QStringLiteral("共 %1 项，可恢复 %2 项，已选 %3 项").arg(m_batch.entries.size()).arg(available).arg(selected.size());
    if (!validation.isEmpty()) summary += QStringLiteral("；") + validation;
    m_selectionSummary->setText(summary);
    m_restoreButton->setEnabled(!m_busy && !selected.isEmpty() && validation.isEmpty()
        && checkedAvailable == selected.size() && SqlTargetSelector::hasTarget(m_target)
        && !m_dataDirectory->text().trimmed().isEmpty() && !m_logDirectory->text().trimmed().isEmpty());
}

void BakRestoreWidget::renderEntries(bool preserveSelection)
{
    QVector<Qt::CheckState> checks;
    QStringList names;
    if (preserveSelection) {
        for (int row = 0; row < m_table->rowCount(); ++row) {
            checks.append(m_table->item(row, Selected) ? m_table->item(row, Selected)->checkState() : Qt::Unchecked);
            names.append(m_table->item(row, TargetDatabase) ? m_table->item(row, TargetDatabase)->text() : QString());
        }
    }
    const QSignalBlocker blocker(m_table);
    m_table->setRowCount(m_batch.entries.size());
    const QFileInfo source(m_source->text().trimmed());
    const QDir sourceDirectory(source.isDir() ? source.absoluteFilePath() : source.absolutePath());
    QSet<QString> files;
    int readCount = 0;
    for (int row = 0; row < m_batch.entries.size(); ++row) {
        const auto& entry = m_batch.entries.at(row);
        files.insert(entry.sourcePath);
        const bool known = entry.readError.isEmpty() && entry.analysis.sets.size() == 1;
        if (known) ++readCount;
        const T3BackupSet backup = entry.analysis.sets.isEmpty() ? T3BackupSet() : entry.analysis.sets.first();
        auto* check = new QTableWidgetItem;
        check->setFlags(eligible(row) ? Qt::ItemIsEnabled | Qt::ItemIsUserCheckable : Qt::NoItemFlags);
        // Keep the user's selection across target checks, including temporarily incompatible rows.
        check->setCheckState(row < checks.size() ? checks.at(row) : Qt::Unchecked);
        m_table->setItem(row, Selected, check);
        QString fileLabel = QDir::toNativeSeparators(sourceDirectory.relativeFilePath(entry.sourcePath));
        if (backup.position > 0) fileLabel += QStringLiteral("  [#%1]").arg(backup.position);
        m_table->setItem(row, File, textItem(fileLabel));
        m_table->item(row, File)->setToolTip(QDir::toNativeSeparators(entry.sourcePath));
        m_table->setItem(row, Database, textItem(backup.databaseName.isEmpty() ? QStringLiteral("未读取") : backup.databaseName));
        m_table->setItem(row, SourceVersion, textItem(BakBackupRestorer::sourceVersionText(entry)));
        m_table->setItem(row, InternalVersion, textItem(backup.databaseVersion > 0 ? QString::number(backup.databaseVersion) : QStringLiteral("未知")));
        m_table->setItem(row, CompatibilityLevel, textItem(entry.compatibilityLevel > 0 ? QString::number(entry.compatibilityLevel) : QStringLiteral("未知")));
        m_table->setItem(row, BackupType, textItem(entry.backupTypeDescription.isEmpty() ? QStringLiteral("未知") : entry.backupTypeDescription));
        m_table->setItem(row, BackupDate, textItem(backup.backupDate.isEmpty() ? QStringLiteral("未知") : backup.backupDate));
        auto* target = new QTableWidgetItem(row < names.size() ? names.at(row) : backup.databaseName);
        target->setFlags(known && entry.backupType == 1 ? Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable
            : Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        target->setToolTip(target->text());
        m_table->setItem(row, TargetDatabase, target);
        m_table->setItem(row, Status, textItem(entryError(entry)));
    }
    QString summary = QStringLiteral("已列出 %1 个 BAK 文件、%2 个条目，已读取 %3 个备份集的版本。").arg(files.size()).arg(m_batch.entries.size()).arg(readCount);
    if (!m_batch.readerServer.isEmpty()) summary += QStringLiteral("读取实例：%1。恢复目标：%2。").arg(m_batch.readerServer, m_target->currentData().toString());
    summary += QStringLiteral("兼容级别不等于来源 SQL Server 版本，不能据此降级恢复。");
    if (!m_batch.error.isEmpty()) summary += QStringLiteral("\n") + m_batch.error;
    m_summary->setText(summary);
    updateSelectionState();
}

void BakRestoreWidget::updateTargetPaths()
{
    m_dataDirectory->clear();
    m_logDirectory->clear();
    for (const auto& entry : m_batch.entries) {
        if (entry.analysis.success) {
            m_dataDirectory->setText(entry.analysis.server.dataDirectory);
            m_logDirectory->setText(entry.analysis.server.logDirectory);
            break;
        }
    }
}

void BakRestoreWidget::targetChanged()
{
    if (m_busy) return;
    m_auth->setText(SqlTargetSelector::isRemote(m_target) ? QStringLiteral("本机配置的 SQL Server 账号") : QStringLiteral("Windows 身份验证"));
    m_testButton->setEnabled(SqlTargetSelector::hasTarget(m_target));
    m_dataDirectory->clear();
    m_logDirectory->clear();
    if (!m_batch.entries.isEmpty()) testConnection();
    else updateSelectionState();
}

void BakRestoreWidget::testConnection()
{
    if (m_busy) return;
    const auto options = connectionOptions();
    const auto original = m_batch;
    const QString staging = m_staging->text().trimmed();
    if (!SqlTargetSelector::hasTarget(m_target)) {
        for (auto& entry : m_batch.entries) {
            entry.analysis.success = false;
            entry.analysis.error = QStringLiteral("请选择恢复实例，已读取的备份信息保留。");
        }
        renderEntries(true);
        updateTargetPaths();
        return;
    }
    const auto result = std::make_shared<BakBackupBatch>();
    const auto server = std::make_shared<T3ServerInfo>();
    const auto error = std::make_shared<QString>();
    m_stop = std::make_shared<std::atomic_bool>(false);
    const auto stop = m_stop;
    setBusy(true);
    m_log->appendPlainText(QStringLiteral("正在校验恢复实例，保留清单、勾选与目标库名……"));
    m_worker = QThread::create([this, options, original, staging, result, server, error, stop]() {
        try {
            if (original.entries.isEmpty()) T3BackupRestorer::testConnection(options, server.get(), error.get());
            else *result = BakBackupRestorer::validateTarget(original, options, staging,
                [stop]() { return stop->load(); }, [this](const QString& text) { emit operationLog(text); });
        } catch (...) { *error = QStringLiteral("目标校验发生异常，原清单已保留。"); }
    });
    m_worker->setParent(this);
    connect(m_worker, &QThread::finished, this, [this, original, result, server, error, stop]() {
        m_worker = nullptr;
        if (!original.entries.isEmpty()) {
            m_batch = error->isEmpty() ? *result : original;
            if (!error->isEmpty() || stop->load()) {
                m_batch.error = stop->load() ? QStringLiteral("已取消目标校验，清单已保留。") : *error;
                for (auto& entry : m_batch.entries) { entry.analysis.success = false; entry.analysis.error = m_batch.error; }
            }
            renderEntries(true);
            updateTargetPaths();
        } else if (error->isEmpty() && !stop->load()) {
            m_dataDirectory->setText(server->dataDirectory);
            m_logDirectory->setText(server->logDirectory);
            m_log->appendPlainText(QStringLiteral("连接成功：%1 / SQL Server %2").arg(server->name, server->version));
        }
        if (!error->isEmpty()) m_log->appendPlainText(*error);
        SqlTargetSelector::updateVersionLabels(m_target);
        setBusy(false);
        emit operationFinished(error->isEmpty() && !stop->load() && (original.entries.isEmpty() || !m_dataDirectory->text().isEmpty()));
    });
    connect(m_worker, &QThread::finished, m_worker, &QObject::deleteLater);
    m_worker->start();
}

void BakRestoreWidget::readBackups()
{
    if (m_busy) return;
    const QFileInfo sourceInfo(m_source->text().trimmed());
    if (!sourceInfo.exists()) { m_log->appendPlainText(QStringLiteral("请选择存在的 BAK 文件或目录。")); return; }
    RestorePreferences::setBackupDirectory(sourceInfo.isDir() ? sourceInfo.absoluteFilePath() : sourceInfo.absolutePath());
    const QString source = sourceInfo.absoluteFilePath();
    const QString staging = m_staging->text().trimmed();
    auto options = connectionOptions();
    QStringList endpoints;
    for (int index = 0; index < m_target->count(); ++index) endpoints.append(m_target->itemData(index).toString());
    const auto candidates = SqlRestoreRouting::candidates(options, endpoints);
    const auto result = std::make_shared<BakBackupBatch>();
    const auto selectedTarget = std::make_shared<QString>(options.server);
    m_stop = std::make_shared<std::atomic_bool>(false);
    const auto stop = m_stop;
    invalidateAnalysis();
    m_log->clear();
    setBusy(true);
    m_worker = QThread::create([this, candidates, source, staging, options, result, selectedTarget, stop]() mutable {
        try {
            const auto cancel = [stop]() { return stop->load(); };
            const auto progress = [this](const QString& text) { emit operationLog(text); };
            *result = BakBackupRestorer::read(candidates, source, staging, cancel, progress);
            if (cancel() || result->entries.isEmpty()) return;
            if (options.server.isEmpty()) {
                for (const auto& candidate : candidates) {
                    if (candidate.server == result->readerServer) { options = candidate; *selectedTarget = candidate.server; break; }
                }
            }
            if (!options.server.isEmpty()) *result = BakBackupRestorer::validateTarget(*result, options, staging, cancel, progress);
        } catch (...) { result->error = QStringLiteral("读取备份发生异常，请检查目录、文件及 SQL Server 连接。"); }
    });
    m_worker->setParent(this);
    connect(m_worker, &QThread::finished, this, [this, result, selectedTarget, stop]() {
        m_worker = nullptr;
        m_batch = *result;
        if (m_target->currentData().toString().isEmpty() && !selectedTarget->isEmpty()) {
            const int index = m_target->findData(*selectedTarget);
            if (index >= 0) { const QSignalBlocker blocker(m_target); m_target->setCurrentIndex(index); }
        }
        m_auth->setText(SqlTargetSelector::isRemote(m_target) ? QStringLiteral("本机配置的 SQL Server 账号") : QStringLiteral("Windows 身份验证"));
        if (stop->load()) {
            m_batch.error = QStringLiteral("已取消读取；已取得的信息保留，请重新读取后恢复。");
            for (auto& entry : m_batch.entries) entry.analysis.success = false;
        }
        SqlTargetSelector::updateVersionLabels(m_target);
        renderEntries(false);
        updateTargetPaths();
        if (!m_batch.error.isEmpty()) m_log->appendPlainText(m_batch.error);
        setBusy(false);
        m_progress->setValue(!stop->load() && !m_batch.entries.isEmpty() ? 1 : 0);
        emit operationFinished(!stop->load() && !m_batch.entries.isEmpty() && m_batch.error.isEmpty());
    });
    connect(m_worker, &QThread::finished, m_worker, &QObject::deleteLater);
    m_worker->start();
}

void BakRestoreWidget::restoreSelected()
{
    if (m_busy) return;
    const auto selected = selections();
    const QString validation = BakBackupRestorer::validateSelections(m_batch, selected);
    if (!validation.isEmpty()) { m_log->appendPlainText(validation); return; }
    const QString data = m_dataDirectory->text().trimmed(), logs = m_logDirectory->text().trimmed();
    if (data.isEmpty() || logs.isEmpty()) { m_log->appendPlainText(QStringLiteral("请填写服务器上的数据和日志目录。")); return; }
    const auto options = connectionOptions();
    const auto batch = m_batch;
    const auto result = std::make_shared<T3RestoreResult>();
    m_stop = std::make_shared<std::atomic_bool>(false);
    const auto stop = m_stop;
    setBusy(true, true);
    m_worker = QThread::create([this, options, batch, selected, data, logs, result, stop]() {
        try {
            *result = BakBackupRestorer::restore(options, batch, selected, data, logs,
                [stop]() { return stop->load(); }, [this](const QString& text) { emit operationLog(text); });
        } catch (...) { result->error = QStringLiteral("恢复发生异常，请检查目标数据库状态；已恢复的数据库予以保留。"); }
    });
    m_worker->setParent(this);
    connect(m_worker, &QThread::finished, this, [this, selected, result]() {
        m_worker = nullptr;
        int success = 0;
        for (int index = 0; index < result->items.size(); ++index) {
            const auto& item = result->items.at(index);
            if (item.success) ++success;
            m_log->appendPlainText(QStringLiteral("%1：%2").arg(item.database,
                item.success ? QStringLiteral("恢复成功") : item.error));
            if (index < selected.size()) {
                const int row = selected.at(index).entryIndex;
                if (row >= 0 && row < m_batch.entries.size()) {
                    m_batch.entries[row].analysis.success = false;
                    m_batch.entries[row].analysis.error = item.success ? QStringLiteral("已恢复，同名数据库不能再次覆盖")
                        : (item.databaseRestored ? QStringLiteral("数据库已恢复并保留，请检查：") : QStringLiteral("恢复失败："))
                            + (item.error.isEmpty() ? QStringLiteral("请检查服务器状态后重新校验目标。") : item.error);
                }
            }
        }
        if (!result->error.isEmpty()) m_log->appendPlainText(result->error);
        const bool ok = result->error.isEmpty() && !result->stopped && success == selected.size();
        m_log->appendPlainText(QStringLiteral("恢复结束：成功 %1 / 选择 %2。%3").arg(success).arg(selected.size())
            .arg(result->stopped ? QStringLiteral("已停止后续恢复。") : QString()));
        renderEntries(true);
        setBusy(false);
        m_progress->setValue(ok ? 1 : 0);
        emit operationFinished(ok);
    });
    connect(m_worker, &QThread::finished, m_worker, &QObject::deleteLater);
    m_worker->start();
}
