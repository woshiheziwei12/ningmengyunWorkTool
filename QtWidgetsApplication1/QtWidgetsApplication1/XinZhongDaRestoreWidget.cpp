#include "Common/SqlServerCredentials.h"
#include "XinZhongDaRestoreWidget.h"
#include "Common/RestorePreferences.h"
#include "Common/SqlTargetSelector.h"
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

XinZhongDaRestoreWidget::XinZhongDaRestoreWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("xinZhongDaRestorePage");
    setAcceptDrops(true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(10);
    auto* title = new QLabel(QString::fromUtf8(u8"恢复新中大备份包"), this);
    title->setObjectName("labelPageTitle");
    layout->addWidget(title);
    auto* intro = new QLabel(
        QString::fromUtf8(
            u8"选择或拖入账套备份目录，支持年度目录及其下的月度备份。也可选择 ZWDATA.VAL；压缩包请先解压。"),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* source = new QHBoxLayout;
    m_source = new QLineEdit(this);
    m_source->setObjectName("xzdSource");
    m_source->setAcceptDrops(false);
    m_browse = new QPushButton(QString::fromUtf8(u8"选择目录"), this);
    m_browse->setObjectName("xzdBrowseDirectory");
    m_file = new QPushButton(QString::fromUtf8(u8"选择 VAL"), this);
    m_file->setObjectName("xzdBrowseMetadata");
    source->addWidget(m_source, 1);
    source->addWidget(m_browse);
    source->addWidget(m_file);
    layout->addLayout(source);
    m_read = new QPushButton(QString::fromUtf8(u8"1. 读取账套和年度"), this);
    m_read->setObjectName("xzdAnalyze");
    m_read->setProperty("flatGreen", true);
    layout->addWidget(m_read);
    m_summary = new QLabel(this);
    m_summary->setObjectName("xzdSummary");
    m_summary->setTextFormat(Qt::PlainText);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);
    auto* hint = new QLabel(QString::fromUtf8(u8"同一年度的月度备份通常是累计快照，每年只恢复一个。默认按凭证日期、明细"
                                              u8"数量推荐；请核对所选备份，不会把月度快照叠加。"),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    m_table = new QTableWidget(0, 3, this);
    m_table->setObjectName("xzdYears");
    m_table->setHorizontalHeaderLabels(
        { QString::fromUtf8(u8"恢复"), QString::fromUtf8(u8"年度"), QString::fromUtf8(u8"所用备份快照（可切换）") });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setMinimumSectionSize(72);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(46);
    m_table->setMinimumHeight(160);
    layout->addWidget(m_table);
    auto* form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_code = new QLineEdit(this);
    m_code->setObjectName("xzdTargetCode");
    m_code->setMaxLength(12);
    m_code->setPlaceholderText(QString::fromUtf8(u8"1—12 位字母或数字，重名时请修改"));
    m_target = new QComboBox(this);
    m_target->setObjectName("xzdTargetServer");
    SqlTargetSelector::populate(m_target);
    m_driver = new QComboBox(this);
    m_driver->setObjectName("xzdDriver");
    m_driver->addItems(T3BackupRestorer::availableDrivers());
    form->addRow(QString::fromUtf8(u8"目标账套号"), m_code);
    form->addRow(QString::fromUtf8(u8"恢复实例"), SqlTargetSelector::control(m_target));
    form->addRow(QString::fromUtf8(u8"ODBC 驱动"), m_driver);
    layout->addLayout(form);
    auto* note = new QLabel(
        QString::fromUtf8(u8"账套名称优先取备份内名称，为空时使用所选目录名。按年度创建 USER 业务库并登记 "
                          u8"PUBDATA，供新中大银色快车 SE 采集使用；已有库和账套号拒绝覆盖。未审核凭证保留原状态。"),
        this);
    note->setWordWrap(true);
    layout->addWidget(note);
    m_log = new QPlainTextEdit(this);
    m_log->setObjectName("xzdRestoreLog");
    m_log->setReadOnly(true);
    m_log->setMinimumHeight(110);
    layout->addWidget(m_log);
    auto* actions = new QHBoxLayout;
    m_back = new QPushButton(QString::fromUtf8(u8"返回"), this);
    m_back->setObjectName("xzdBack");
    m_stop = new QPushButton(QString::fromUtf8(u8"取消"), this);
    m_stop->setObjectName("xzdCancel");
    m_restore = new QPushButton(QString::fromUtf8(u8"2. 恢复所选年度"), this);
    m_restore->setObjectName("xzdRestore");
    m_restore->setProperty("flatGreen", true);
    actions->addWidget(m_back);
    actions->addStretch();
    actions->addWidget(m_stop);
    actions->addWidget(m_restore);
    layout->addLayout(actions);
    connect(m_browse, &QPushButton::clicked, this, [this]() {
        const auto path = QFileDialog::getExistingDirectory(
            this, QString::fromUtf8(u8"选择新中大账套备份目录"), RestorePreferences::backupDirectory());
        if (!path.isEmpty()) {
            m_source->setText(path);
            RestorePreferences::setBackupDirectory(path);
        }
    });
    connect(m_file, &QPushButton::clicked, this, [this]() {
        const auto path = QFileDialog::getOpenFileName(this, QString::fromUtf8(u8"选择账套标识文件"),
            RestorePreferences::backupDirectory(), "ZWDATA.VAL (ZWDATA.VAL zwdata.val)");
        if (!path.isEmpty()) {
            m_source->setText(path);
            RestorePreferences::setBackupDirectory(QFileInfo(path).absolutePath());
        }
    });
    connect(m_source, &QLineEdit::textChanged, this, &XinZhongDaRestoreWidget::invalidate);
    connect(m_read, &QPushButton::clicked, this, &XinZhongDaRestoreWidget::readBackup);
    connect(m_restore, &QPushButton::clicked, this, &XinZhongDaRestoreWidget::restoreBackup);
    connect(m_stop, &QPushButton::clicked, this, [this]() {
        if (m_cancel)
            m_cancel->store(true);
        m_stop->setEnabled(false);
    });
    connect(m_back, &QPushButton::clicked, this, &XinZhongDaRestoreWidget::backToMain);
    connect(this, &XinZhongDaRestoreWidget::operationLog, m_log, &QPlainTextEdit::appendPlainText);
    invalidate();
}
XinZhongDaRestoreWidget::~XinZhongDaRestoreWidget()
{
    if (m_worker) {
        m_cancel->store(true);
        m_worker->wait();
    }
}
void XinZhongDaRestoreWidget::setBusy(bool busy)
{
    setProperty("sessionBusyXzdRestore", busy);
    setAcceptDrops(!busy);
    for (QWidget* w :
        QList<QWidget*> { m_source, m_code, m_target, m_driver, m_read, m_browse, m_file, m_back, m_table })
        w->setEnabled(!busy);
    m_stop->setEnabled(busy);
    m_restore->setEnabled(!busy && !m_catalog.isEmpty());
}
void XinZhongDaRestoreWidget::invalidate()
{
    if (m_worker)
        return;
    m_catalog.clear();
    m_table->setRowCount(0);
    m_code->clear();
    m_summary->setText(QString::fromUtf8(u8"读取后显示账套名称、年度和备份快照。"));
    setBusy(false);
}
void XinZhongDaRestoreWidget::readBackup()
{
    if (m_worker)
        return;
    invalidate();
    const auto source = m_source->text();
    const auto catalog = std::make_shared<QVector<XinZhongDaBackupAnalysis>>();
    const auto error = std::make_shared<QString>();
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto stop = m_cancel;
    setBusy(true);
    m_worker = QThread::create([this, source, catalog, error, stop]() {
        *catalog = XinZhongDaBackupRestorer::discover(
            source, error.get(), [stop]() { return stop->load(); },
            [this](const QString& text) { emit operationLog(text); });
    });
    m_worker->setParent(this);
    connect(m_worker, &QThread::finished, this, [this, catalog, error]() {
        m_worker = nullptr;
        m_catalog = *catalog;
        if (!error->isEmpty())
            m_log->appendPlainText(*error);
        if (!m_catalog.isEmpty()) {
            const auto first = m_catalog.first();
            m_code->setText(first.accountCode);
            m_summary->setText(QString::fromUtf8(u8"账套：%1　原账套号：%2　名称来源：%3　共 %4 个备份快照")
                    .arg(first.accountName, first.accountCode,
                        first.nameFromBackup ? QString::fromUtf8(u8"备份内容") : QString::fromUtf8(u8"所选目录名"))
                    .arg(m_catalog.size()));
            QMap<int, int> rows;
            for (int i = 0; i < m_catalog.size(); ++i) {
                const auto& a = m_catalog[i];
                int row;
                if (!rows.contains(a.year)) {
                    row = m_table->rowCount();
                    rows.insert(a.year, row);
                    m_table->insertRow(row);
                    auto* check = new QTableWidgetItem;
                    check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
                    check->setCheckState(Qt::Checked);
                    m_table->setItem(row, 0, check);
                    auto* year = new QTableWidgetItem(QString::number(a.year));
                    year->setFlags(Qt::ItemIsEnabled);
                    m_table->setItem(row, 1, year);
                    auto* combo = new QComboBox(m_table);
                    combo->setObjectName("xzdSnapshot" + QString::number(a.year));
                    combo->setItemDelegate(new QStyledItemDelegate(combo));
                    m_table->setCellWidget(row, 2, combo);
                } else
                    row = rows.value(a.year);
                auto* combo = qobject_cast<QComboBox*>(m_table->cellWidget(row, 2));
                combo->addItem(QString::fromUtf8(u8"%1　%2 条 / 已审核 %3　末笔 %4")
                                   .arg(QFileInfo(a.directory).fileName())
                                   .arg(a.voucherRows)
                                   .arg(a.approvedVoucherRows)
                                   .arg(a.lastVoucherDate.left(10)),
                    i);
                combo->setItemData(combo->count() - 1, a.directory, Qt::ToolTipRole);
            }
        }
        setBusy(false);
        emit operationFinished(error->isEmpty());
    });
    connect(m_worker, &QThread::finished, m_worker, &QObject::deleteLater);
    m_worker->start();
}
void XinZhongDaRestoreWidget::restoreBackup()
{
    if (m_worker)
        return;
    QVector<XinZhongDaBackupAnalysis> selected;
    for (int row = 0; row < m_table->rowCount(); ++row)
        if (m_table->item(row, 0)->checkState() == Qt::Checked) {
            auto* combo = qobject_cast<QComboBox*>(m_table->cellWidget(row, 2));
            const int index = combo->currentData().toInt();
            if (index >= 0 && index < m_catalog.size())
                selected.append(m_catalog[index]);
        }
    if (selected.isEmpty() || !SqlTargetSelector::hasTarget(m_target)) {
        m_log->appendPlainText(QString::fromUtf8(u8"请选择年度和恢复实例。"));
        return;
    }
    T3SqlConnection options;
    options.server = m_target->currentData().toString();
    options.driver = m_driver->currentText();
    options.windowsAuthentication = !SqlTargetSelector::isRemote(m_target);
    if (!options.windowsAuthentication) {
        SqlServerCredentials::apply211(options);
    }
    const QString code = m_code->text().trimmed();
    if (XinZhongDaBackupRestorer::targetDatabase(code, selected.first().year).isEmpty()) {
        m_log->appendPlainText(QString::fromUtf8(u8"目标账套号须为 1—12 位字母或数字。"));
        return;
    }
    const auto result = std::make_shared<XinZhongDaRestoreResult>();
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto stop = m_cancel;
    setBusy(true);
    m_worker = QThread::create([this, options, selected, code, result, stop]() {
        *result = XinZhongDaBackupRestorer::restoreMany(
            options, selected, code, [stop]() { return stop->load(); },
            [this](const QString& text) { emit operationLog(text); });
    });
    m_worker->setParent(this);
    connect(m_worker, &QThread::finished, this, [this, result]() {
        m_worker = nullptr;
        if (!result->success)
            m_log->appendPlainText(result->error);
        setBusy(false);
        if (result->success)
            m_restore->setEnabled(false);
        emit operationFinished(result->success);
    });
    connect(m_worker, &QThread::finished, m_worker, &QObject::deleteLater);
    m_worker->start();
}
QString XinZhongDaRestoreWidget::droppedPath(const QMimeData* data) const
{
    if (!data || data->urls().size() != 1 || !data->urls().first().isLocalFile())
        return {};
    QString error;
    const auto path = data->urls().first().toLocalFile();
    return XinZhongDaBackupRestorer::resolveDirectory(path, &error).isEmpty() ? QString() : path;
}
void XinZhongDaRestoreWidget::dragEnterEvent(QDragEnterEvent* e)
{
    if (!m_worker && !droppedPath(e->mimeData()).isEmpty())
        e->acceptProposedAction();
    else
        e->ignore();
}
void XinZhongDaRestoreWidget::dropEvent(QDropEvent* e)
{
    if (m_worker)
        return;
    QString path = droppedPath(e->mimeData());
    if (path.isEmpty())
        return;
    if (QFileInfo(path).isFile()) {
        QString error;
        path = XinZhongDaBackupRestorer::retainDroppedBackup(path, &error);
        if (path.isEmpty()) {
            m_log->appendPlainText(error);
            return;
        }
    }
    m_source->setText(path);
    RestorePreferences::setBackupDirectory(QFileInfo(path).isDir() ? path : QFileInfo(path).absolutePath());
    e->acceptProposedAction();
}
