#include "DatabaseCleanupWidget.h"
#include "Common/SqlTargetSelector.h"
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>
#include <exception>

DatabaseCleanupWidget::DatabaseCleanupWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName("databaseCleanupPage");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);
    auto* title = new QLabel(QString::fromUtf8(u8"选择删除数据库"), this);
    title->setObjectName("labelPageTitle");
    layout->addWidget(title);
    auto* intro = new QLabel(QString::fromUtf8(u8"读取清单后勾选要删除的数据库，可逐项选择或全选。连接账号和密码已内置，无需输入。"), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    m_target = new QComboBox(this);
    m_target->setObjectName("cleanupTarget");
    SqlTargetSelector::populate(m_target);
    layout->addWidget(SqlTargetSelector::control(m_target));
    auto* note = new QLabel(QString::fromUtf8(u8"保留 master、tempdb、model、msdb、Resource 及复制分发数据库。删除会断开目标库连接并回滚未提交事务，数据只能通过已有备份恢复。"), this);
    note->setWordWrap(true);
    layout->addWidget(note);
    m_refresh = new QPushButton(QString::fromUtf8(u8"读取 / 刷新数据库清单"), this);
    m_refresh->setObjectName("cleanupRefresh");
    m_refresh->setProperty("grayButton", true);
    layout->addWidget(m_refresh);
    m_status = new QLabel(this);
    m_status->setObjectName("cleanupStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    auto* selectionRow = new QHBoxLayout;
    m_selectAll = new QPushButton(QString::fromUtf8(u8"全选"), this);
    m_selectAll->setObjectName("cleanupSelectAll");
    m_selectAll->setProperty("grayButton", true);
    m_selection = new QLabel(this);
    m_selection->setObjectName("cleanupSelectionCount");
    selectionRow->addWidget(m_selectAll);
    selectionRow->addWidget(m_selection);
    selectionRow->addStretch();
    layout->addLayout(selectionRow);
    m_table = new QTableWidget(0, 4, this);
    m_table->setObjectName("cleanupDatabases");
    m_table->setHorizontalHeaderLabels({QString::fromUtf8(u8"选择 / 数据库"), QString::fromUtf8(u8"状态"),
        QString::fromUtf8(u8"类型"), QString::fromUtf8(u8"处理结果")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->hide();
    m_table->setMinimumHeight(240);
    layout->addWidget(m_table, 1);
    auto* actions = new QHBoxLayout;
    auto* back = new QPushButton(QString::fromUtf8(u8"返回"), this);
    back->setObjectName("btnBackToMain");
    back->setProperty("grayButton", true);
    actions->addWidget(back);
    actions->addStretch();
    m_stop = new QPushButton(QString::fromUtf8(u8"停止后续删除"), this);
    m_stop->setObjectName("cleanupStop");
    m_stop->setProperty("grayButton", true);
    m_delete = new QPushButton(QString::fromUtf8(u8"删除所选数据库"), this);
    m_delete->setObjectName("cleanupDelete");
    m_delete->setProperty("dangerButton", true);
    actions->addWidget(m_stop);
    actions->addWidget(m_delete);
    layout->addLayout(actions);
    connect(back, &QPushButton::clicked, this, &DatabaseCleanupWidget::backToMain);
    connect(m_target, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { invalidate(); });
    connect(m_refresh, &QPushButton::clicked, this, &DatabaseCleanupWidget::refresh);
    connect(m_delete, &QPushButton::clicked, this, &DatabaseCleanupWidget::removeSelected);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (item->column() == 0) updateSelection();
    });
    connect(m_selectAll, &QPushButton::clicked, this, [this]() {
        if (m_busy || !m_loaded) return;
        const bool allChecked = selectedCatalog().databases.size() == m_catalog.databases.size();
        {
            const QSignalBlocker blocker(m_table);
            for (int row = 0; row < m_table->rowCount(); ++row)
                m_table->item(row, 0)->setCheckState(allChecked ? Qt::Unchecked : Qt::Checked);
        }
        updateSelection();
    });
    connect(m_stop, &QPushButton::clicked, this, [this]() {
        if (m_cancel) m_cancel->store(true);
        m_stop->setEnabled(false);
        m_status->setText(QString::fromUtf8(u8"已请求停止，当前数据库操作结束后停止后续删除。"));
    });
    connect(this, &DatabaseCleanupWidget::itemFinished, this, [this](const QString& name, bool success, const QString& error) {
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (m_table->item(row, 0)->text() != name) continue;
            const QString text = success ? QString::fromUtf8(u8"已删除") : QString::fromUtf8(u8"失败：%1").arg(error);
            m_table->setItem(row, 3, new QTableWidgetItem(text));
            m_table->item(row, 3)->setToolTip(text);
        }
    });
    invalidate();
}
DatabaseCleanupWidget::~DatabaseCleanupWidget()
{
    if (m_worker) {
        if (m_cancel) m_cancel->store(true);
        m_worker->wait();
    }
}
void DatabaseCleanupWidget::setBusy(bool busy)
{
    setProperty("sessionBusysetBusy", busy);
    m_busy = busy;
    m_target->setEnabled(!busy);
    m_refresh->setEnabled(!busy && SqlTargetSelector::hasTarget(m_target));
    {
        const QSignalBlocker blocker(m_table);
        for (int row = 0; row < m_table->rowCount(); ++row) {
            auto* item = m_table->item(row, 0);
            if (item) item->setFlags((!busy && m_loaded)
                ? item->flags() | Qt::ItemIsUserCheckable : item->flags() & ~Qt::ItemIsUserCheckable);
        }
    }
    updateSelection();
    m_stop->setEnabled(busy && static_cast<bool>(m_cancel));
}
DatabaseCleaner::Catalog DatabaseCleanupWidget::selectedCatalog() const
{
    auto selected = m_catalog;
    selected.databases.clear();
    for (int row = 0; row < m_table->rowCount() && row < m_catalog.databases.size(); ++row) {
        const auto* item = m_table->item(row, 0);
        if (item && item->checkState() == Qt::Checked) selected.databases.append(m_catalog.databases.at(row));
    }
    return selected;
}
void DatabaseCleanupWidget::updateSelection()
{
    const int count = selectedCatalog().databases.size();
    const int total = m_catalog.databases.size();
    m_selection->setText(QString::fromUtf8(u8"已选择 %1 / %2 个数据库").arg(count).arg(total));
    m_selectAll->setText(count > 0 && count == total ? QString::fromUtf8(u8"取消全选") : QString::fromUtf8(u8"全选"));
    m_selectAll->setEnabled(!m_busy && m_loaded && total > 0);
    m_delete->setEnabled(!m_busy && m_loaded && count > 0);
    if (!m_busy && m_loaded) {
        for (int row = 0; row < m_table->rowCount(); ++row) {
            auto* item = m_table->item(row, 0);
            auto* result = m_table->item(row, 3);
            if (item && result) result->setText(item->checkState() == Qt::Checked
                ? QString::fromUtf8(u8"已选择") : QString::fromUtf8(u8"未选择"));
        }
    }
}
void DatabaseCleanupWidget::displayCatalog(const DatabaseCleaner::Catalog& catalog)
{
    const QSignalBlocker blocker(m_table);
    m_catalog = catalog;
    m_loaded = catalog.error.isEmpty();
    m_table->setRowCount(0);
    m_table->setRowCount(catalog.databases.size());
    for (int row = 0; row < catalog.databases.size(); ++row) {
        const auto& database = catalog.databases.at(row);
        const QStringList values = {database.name, database.state, database.sourceId
            ? QString::fromUtf8(u8"数据库快照") : QString::fromUtf8(u8"用户数据库"), QString::fromUtf8(u8"未选择")};
        for (int col = 0; col < values.size(); ++col) {
            auto* item = new QTableWidgetItem(values.at(col));
            item->setToolTip(values.at(col));
            if (col == 0) item->setCheckState(Qt::Unchecked);
            m_table->setItem(row, col, item);
        }
    }
    m_status->setText(!m_loaded ? QString::fromUtf8(u8"读取失败：%1").arg(catalog.error)
        : QString::fromUtf8(u8"实例：%1；共 %2 个非系统数据库%3").arg(catalog.server).arg(catalog.databases.size())
            .arg(catalog.databases.isEmpty() ? QString::fromUtf8(u8"，无需删除。") : QString::fromUtf8(u8"，请勾选要删除的数据库。")));
    setBusy(false);
}
void DatabaseCleanupWidget::invalidate()
{
    m_loaded = false;
    m_catalog = {};
    m_table->setRowCount(0);
    m_status->setText(QString::fromUtf8(u8"请读取所选实例的数据库清单。"));
    setBusy(false);
}
void DatabaseCleanupWidget::refresh()
{
    if (m_worker || !SqlTargetSelector::hasTarget(m_target)) return;
    invalidate();
    const auto target = SqlTargetSelector::isRemote(m_target) ? DatabaseCleaner::Target::Server211 : DatabaseCleaner::Target::Local;
    const QString localInstance = target == DatabaseCleaner::Target::Local ? m_target->currentData().toString() : QStringLiteral(".");
    auto result = std::make_shared<DatabaseCleaner::Catalog>();
    m_cancel.reset();
    setBusy(true);
    m_status->setText(QString::fromUtf8(u8"正在连接并读取数据库清单……"));
    m_worker = QThread::create([target, localInstance, result]() {
        try { *result = DatabaseCleaner::inspect(target, localInstance); }
        catch (const std::exception&) { result->error = QString::fromUtf8(u8"读取数据库清单时发生异常。"); }
    });
    connect(m_worker, &QThread::finished, this, [this, result]() {
        m_worker->deleteLater();
        m_worker = nullptr;
        displayCatalog(*result);
    });
    m_worker->start();
}
void DatabaseCleanupWidget::removeSelected()
{
    if (m_worker || !m_loaded || m_catalog.databases.isEmpty()) return;
    const auto confirmed = selectedCatalog();
    if (confirmed.databases.isEmpty()) return;
    QMessageBox dialog(QMessageBox::Warning, QString::fromUtf8(u8"确认删除数据库"),
        QString::fromUtf8(u8"目标：%1\n实际实例：%2\n\n将删除勾选的 %3 个数据库，断开连接并回滚未提交事务。此操作不可撤销。").arg(
            DatabaseCleaner::targetLabel(confirmed.target, confirmed.localInstance), confirmed.server).arg(confirmed.databases.size()),
        QMessageBox::NoButton, this);
    dialog.setTextFormat(Qt::PlainText);
    QStringList names;
    for (const auto& database : confirmed.databases) names.append(database.name);
    dialog.setDetailedText(names.join('\n'));
    auto* remove = dialog.addButton(QString::fromUtf8(u8"确认删除所选"), QMessageBox::DestructiveRole);
    remove->setProperty("dangerButton", true);
    auto* cancel = dialog.addButton(QString::fromUtf8(u8"取消"), QMessageBox::RejectRole);
    dialog.setDefaultButton(cancel);
    dialog.setEscapeButton(cancel);
    dialog.exec();
    if (dialog.clickedButton() != remove) return;
    for (int row = 0; row < m_table->rowCount(); ++row)
        m_table->item(row, 3)->setText(m_table->item(row, 0)->checkState() == Qt::Checked
            ? QString::fromUtf8(u8"待删除") : QString::fromUtf8(u8"未选择，保留"));
    m_loaded = false;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto stop = m_cancel;
    auto result = std::make_shared<DatabaseCleaner::Result>();
    setBusy(true);
    m_status->setText(QString::fromUtf8(u8"正在核对清单并删除数据库……"));
    m_worker = QThread::create([this, confirmed, result, stop]() {
        try {
            *result = DatabaseCleaner::removeSelected(confirmed, [stop]() { return stop->load(); },
                [this](const DatabaseCleaner::ItemResult& item) { emit itemFinished(item.name, item.success, item.error); });
        } catch (const std::exception&) { result->error = QString::fromUtf8(u8"清理发生异常，请刷新清单核实当前状态。"); }
    });
    connect(m_worker, &QThread::finished, this, [this, result]() {
        m_worker->deleteLater();
        m_worker = nullptr;
        m_cancel.reset();
        int success = 0;
        for (const auto& item : result->items) if (item.success) ++success;
        for (int row = 0; row < m_table->rowCount(); ++row)
            if (m_table->item(row, 3)->text() == QString::fromUtf8(u8"待删除"))
                m_table->item(row, 3)->setText(QString::fromUtf8(u8"未执行"));
        QString status = QString::fromUtf8(u8"%1：成功 %2 个，失败 %3 个。").arg(result->stopped
            ? QString::fromUtf8(u8"已停止") : QString::fromUtf8(u8"处理结束")).arg(success).arg(result->items.size() - success);
        if (!result->error.isEmpty()) status += '\n' + result->error;
        else status += QString::fromUtf8(u8"实例中剩余 %1 个非系统数据库。").arg(result->remaining.size());
        status += QString::fromUtf8(u8"再次操作前请刷新清单。");
        m_status->setText(status);
        setBusy(false);
    });
    m_worker->start();
}
