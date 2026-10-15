#include "subjectbalanceimportwidget.h"
#include "Common/globaldata.h"
#include "Common/importerhostselector.h"
#include "Common/subjectbalancefiles.h"

#include <QtWidgets>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QLockFile>
#include <QStandardPaths>
#include <algorithm>

namespace {
class SpacedComboDelegate : public QStyledItemDelegate
{
public:
    explicit SpacedComboDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(size.height(), qMax(36, option.fontMetrics.height() + 16)));
        return size;
    }
};

void spaceComboItems(QAbstractItemView *view)
{
    view->setItemDelegate(new SpacedComboDelegate(view));
    view->setTextElideMode(Qt::ElideRight);
    if (auto *list = qobject_cast<QListView*>(view)) {
        list->setSpacing(2);
        list->setUniformItemSizes(true);
    }
}

void appendPlain(QTextEdit *edit, const QString &text)
{
    QTextCursor cursor = edit->textCursor();
    cursor.movePosition(QTextCursor::End);
    if (!edit->document()->isEmpty()) cursor.insertText("\n");
    cursor.insertText(text);
    edit->setTextCursor(cursor);
}

QJsonArray readRecords(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).array();
}

const QList<QPair<int, QString>> standards = {
    {1, QStringLiteral("小企业会计准则")}, {2, QStringLiteral("企业会计准则")},
    {3, QStringLiteral("民间非营利组织会计制度")}, {4, QStringLiteral("农民专业合作社会计制度")},
    {5, QStringLiteral("农民专业合作社会计制度（2023）")}, {6, QStringLiteral("工会会计制度")},
    {7, QStringLiteral("农村集体经济组织会计制度")}, {9, QStringLiteral("民间非营利组织会计制度（2026）")}
};
}

SubjectBalanceImportWidget::SubjectBalanceImportWidget(GlobalData *globalData, QWidget *parent, const QString &historyFilePath)
    : QWidget(parent), m_global(globalData), m_historyFilePath(historyFilePath), m_client(new SubjectBalanceImportClient(this))
{
    setObjectName("subjectBalanceImportPage");
    m_client->setTokenProvider([this]() {
        if (!m_global) return QString();
        return m_global->m_strNingMengCookie.isEmpty() ? m_global->m_userToken.m_strAccessToken : m_global->m_strNingMengCookie;
    });
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(12);
    auto *title = new QLabel(QStringLiteral("科目余额表导账"), this);
    title->setObjectName("labelPageTitle");
    root->addWidget(title);
    auto *intro = new QLabel(QStringLiteral("选择来源软件及原始表格，在当前套餐下新建企业专业版账套。"), this);
    intro->setWordWrap(true);
    root->addWidget(intro);
    m_form = new QWidget(this);
    auto *form = new QFormLayout(m_form);
    form->setContentsMargins(0, 0, 0, 0);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_software = new QComboBox(m_form);
    m_software->setObjectName("balanceSoftware");
    m_software->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_software->setMinimumContentsLength(16);
    QString catalogError;
    m_catalog = SubjectBalanceImport::softwareCatalog(&catalogError);
    m_software->addItem(QStringLiteral("请选择来源软件"), -1);
    for (const auto &software : m_catalog) {
        m_software->addItem(software.name, software.id);
        m_software->setItemData(m_software->count() - 1, software.name, Qt::ToolTipRole);
    }
    m_software->setEditable(true);
    m_software->setInsertPolicy(QComboBox::NoInsert);
    m_software->setMaxVisibleItems(10);
    m_software->lineEdit()->setPlaceholderText(QStringLiteral("输入关键词搜索，如：用友、金蝶、T3"));
    m_software->lineEdit()->setClearButtonEnabled(true);
    auto *completer = new QCompleter(m_software->model(), m_software);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setMaxVisibleItems(10);
    auto *suggestions = new QListView(m_software);
    suggestions->setObjectName("balanceSoftwareSuggestions");
    completer->setPopup(suggestions);
    spaceComboItems(suggestions);
    m_software->setCompleter(completer);
    m_software->setCurrentIndex(-1);
    connect(m_software->lineEdit(), &QLineEdit::textEdited, this, [this, completer](const QString &text) {
        // Typed text is a query, never an implicit choice of the previously selected software.
        if (m_software->currentIndex() >= 0 && text != m_software->itemText(m_software->currentIndex())) {
            m_software->setCurrentIndex(-1);
            m_software->setEditText(text);
        }
        // QCompleter owns a separate popup window; explicitly carry over the workspace theme.
        const QString theme = window()->styleSheet();
        if (completer->popup()->styleSheet() != theme) completer->popup()->setStyleSheet(theme);
        completer->setCompletionPrefix(text);
        if (text.isEmpty()) completer->popup()->hide();
        else completer->complete();
    });
    form->addRow(QStringLiteral("来源软件 *"), m_software);
    m_name = new QLineEdit(m_form);
    m_name->setObjectName("balanceAccountName");
    m_name->setPlaceholderText(QStringLiteral("填写新账套名称"));
    form->addRow(QStringLiteral("账套名称 *"), m_name);
    m_month = new QDateEdit(QDate::currentDate(), m_form);
    m_month->setObjectName("balanceStartMonth");
    m_month->setDisplayFormat("yyyy-MM");
    m_month->setDateRange(QDate(1900, 1, 1), QDate(9999, 12, 31));
    form->addRow(QStringLiteral("启用年月 *"), m_month);
    auto *periodHint = new QLabel(QStringLiteral("若凭证存在更早月份，服务端将使用该月份作为实际启用年月。"), m_form);
    periodHint->setWordWrap(true);
    form->addRow(QString(), periodHint);
    m_standard = new QComboBox(m_form);
    m_standard->setObjectName("balanceStandard");
    form->addRow(QStringLiteral("会计准则 *"), m_standard);
    m_tax = new QComboBox(m_form);
    m_tax->setObjectName("balanceTaxType");
    m_tax->addItem(QStringLiteral("请选择纳税人类型"), 0);
    m_tax->addItem(QStringLiteral("小规模纳税人"), 1);
    m_tax->addItem(QStringLiteral("一般纳税人"), 2);
    m_tax->setCurrentIndex(m_tax->findData(2));
    form->addRow(QStringLiteral("纳税人类型 *"), m_tax);
    m_host = new QComboBox(m_form);
    m_host->setObjectName("balanceImporterHost");
    m_host->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_host->setMinimumContentsLength(16);
    populateProfessionalImporterHosts(m_host);
    form->addRow(QStringLiteral("导账域名 *"), m_host);
    m_zero = new QCheckBox(QStringLiteral("导入零余额科目（包括金额为空的余额表行）"), m_form);
    m_zero->setObjectName("balanceImportZero");
    form->addRow(QString(), m_zero);
    m_softwareNotes = new QLabel(m_form);
    m_softwareNotes->setObjectName("balanceSoftwareNotes");
    m_softwareNotes->setTextFormat(Qt::PlainText);
    m_softwareNotes->setWordWrap(true);
    form->addRow(QString(), m_softwareNotes);
    m_fileHint = new QLabel(m_form);
    m_fileHint->setObjectName("balanceFileHint");
    m_fileHint->setTextFormat(Qt::PlainText);
    m_fileHint->setWordWrap(true);
    form->addRow(QString(), m_fileHint);
    auto *files = new QWidget(m_form);
    m_fileLayout = new QFormLayout(files);
    m_fileLayout->setContentsMargins(0, 0, 0, 0);
    m_fileLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_fileLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->addRow(files);
    m_auxiliary = new QTableWidget(0, 3, m_form);
    m_auxiliary->setObjectName("balanceAuxiliaryFiles");
    m_auxiliary->setHorizontalHeaderLabels({QStringLiteral("辅助类别（如客户）"), QStringLiteral("原始表格"), QStringLiteral("操作")});
    m_auxiliary->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_auxiliary->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_auxiliary->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_auxiliary->setColumnWidth(0, 190);
    m_auxiliary->setColumnWidth(2, 86);
    m_auxiliary->verticalHeader()->setDefaultSectionSize(48);
    m_auxiliary->setMinimumHeight(110);
    m_auxiliary->setMaximumHeight(190);
    form->addRow(m_auxiliary);
    m_addAuxiliary = new QPushButton(QStringLiteral("添加辅助核算附件"), m_form);
    m_addAuxiliary->setMaximumWidth(220);
    form->addRow(QString(), m_addAuxiliary);
    connect(m_addAuxiliary, &QPushButton::clicked, this, &SubjectBalanceImportWidget::addAuxiliary);
    root->addWidget(m_form);
    auto *actions = new QHBoxLayout;
    m_submit = new QPushButton(QStringLiteral("导账"), this);
    m_submit->setObjectName("balanceSubmit");
    m_submit->setProperty("flatGreen", true);
    m_stop = new QPushButton(QStringLiteral("停止查询"), this);
    m_stop->setObjectName("balanceStopQuery");
    m_stop->setEnabled(false);
    auto *back = new QPushButton(QStringLiteral("返回"), this);
    actions->addWidget(m_submit);
    actions->addWidget(m_stop);
    actions->addStretch();
    actions->addWidget(back);
    root->addLayout(actions);
    m_status = new QLabel(this);
    m_status->setObjectName("balanceStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_status);
    auto *historyRow = new QHBoxLayout;
    m_history = new QComboBox(this);
    m_history->setObjectName("balanceHistory");
    m_history->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_history->setMinimumContentsLength(16);
    m_resume = new QPushButton(QStringLiteral("查询历史任务"), this);
    m_resume->setObjectName("balanceResumeQuery");
    historyRow->addWidget(m_history, 1);
    historyRow->addWidget(m_resume);
    root->addLayout(historyRow);
    m_result = new QTextEdit(this);
    m_result->setObjectName("balanceResult");
    m_result->setReadOnly(true);
    m_result->setMinimumHeight(150);
    root->addWidget(m_result);
    connect(m_software, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SubjectBalanceImportWidget::changeSoftware);
    connect(m_submit, &QPushButton::clicked, this, &SubjectBalanceImportWidget::submit);
    connect(m_resume, &QPushButton::clicked, this, &SubjectBalanceImportWidget::resumeQuery);
    connect(m_stop, &QPushButton::clicked, m_client, &SubjectBalanceImportClient::stopQuery);
    connect(back, &QPushButton::clicked, this, &SubjectBalanceImportWidget::backToMain);
    connect(m_client, &SubjectBalanceImportClient::busyChanged, this, &SubjectBalanceImportWidget::updateBusy);
    connect(m_client, &SubjectBalanceImportClient::progress, this, [this](const QString &message) {
        m_status->setText(message);
    });
    connect(m_client, &SubjectBalanceImportClient::accepted, this, [this](const QString &id) {
        m_record["importId"] = id;
        m_record["status"] = 0;
        m_record["submittedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        m_stop->setEnabled(true);
        m_result->setPlainText(QStringLiteral("任务编号：%1\n账套：%2\n提交域名：%3")
            .arg(id, m_record.value("accountName").toString(), m_record.value("host").toString()));
        saveRecord();
    });
    connect(m_client, &SubjectBalanceImportClient::failed, this, [this](const QString &message, bool uncertain) {
        m_status->setText(message);
        appendPlain(m_result, message);
        if (uncertain) appendPlain(m_result, QStringLiteral("提交结果不确定，服务端可能已创建任务。请先在导账历史中核实，避免重复新建账套；本工具不会自动重新提交。"));
    });
    connect(m_client, &SubjectBalanceImportClient::outcome, this,
        [this](int status, const QString &error, const QString &warnings, const QString &month, const QString &accountId) {
        const QString text = status == 1 ? QStringLiteral("导账成功") : status == 2 ? QStringLiteral("部分导入，请检查明细") : QStringLiteral("导账失败");
        m_status->setText(text);
        appendPlain(m_result, text);
        if (!month.isEmpty()) appendPlain(m_result, QStringLiteral("实际启用年月：") + month);
        if (!accountId.isEmpty()) appendPlain(m_result, QStringLiteral("账套编号：") + accountId);
        if (!error.isEmpty()) appendPlain(m_result, QStringLiteral("错误明细：\n") + error);
        if (!warnings.isEmpty()) appendPlain(m_result, QStringLiteral("导入提醒：\n") + warnings);
        m_record["status"] = status;
        m_record["actualStartMonth"] = month;
        saveRecord();
    });
    for (auto *combo : {m_software, m_standard, m_tax, m_host, m_history}) {
        spaceComboItems(combo->view());
        combo->setMaxVisibleItems(10);
    }
    changeSoftware();
    reloadHistory();
    if (!catalogError.isEmpty()) { m_status->setText(catalogError); m_submit->setEnabled(false); }
}

void SubjectBalanceImportWidget::changeSoftware()
{
    while (m_fileLayout->rowCount()) m_fileLayout->removeRow(0);
    m_fileInputs.clear();
    m_fileHint->clear();
    m_auxiliary->setRowCount(0);
    m_auxiliary->hide();
    m_addAuxiliary->hide();
    m_softwareNotes->clear();
    const int priorStandard = m_standard->currentData().toInt();
    m_standard->clear();
    m_standard->addItem(QStringLiteral("请选择会计准则"), 0);
    const BalanceImportSoftware *selected = nullptr;
    for (const auto &software : m_catalog)
        if (software.id == m_software->currentData().toInt()) { selected = &software; break; }
    for (const auto &standard : standards)
        if (!selected || selected->accountingStandards.isEmpty() || selected->accountingStandards.contains(standard.first))
            m_standard->addItem(standard.second, standard.first);
    m_standard->setCurrentIndex(qMax(0, m_standard->findData(priorStandard > 0 ? priorStandard : 1)));
    const QString defaultName = selected ? selected->name + QStringLiteral("账套") : QString();
    if (m_name->text().trimmed().isEmpty() || m_name->text() == m_generatedAccountName)
        m_name->setText(defaultName);
    m_generatedAccountName = defaultName;
    if (!selected) return;
    m_softwareNotes->setText(selected->notes);
    m_addAuxiliary->setVisible(selected->supportsAuxiliaryFiles);
    auto orderedFiles = selected->files;
    const QStringList fileOrder{"subject", "trialbalance", "voucher"};
    std::stable_sort(orderedFiles.begin(), orderedFiles.end(), [&fileOrder](const BalanceImportFileSlot &a, const BalanceImportFileSlot &b) {
        const int first = fileOrder.indexOf(a.key), second = fileOrder.indexOf(b.key);
        return (first < 0 ? fileOrder.size() : first) < (second < 0 ? fileOrder.size() : second);
    });
    for (const auto &slot : orderedFiles) {
        auto *row = new QWidget(m_form);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *path = new QLineEdit(row);
        path->setObjectName("balanceFile_" + slot.key);
        path->setReadOnly(true);
        path->setPlaceholderText(slot.extensions.join(" / "));
        path->setMinimumWidth(80);
        m_fileInputs.insert(slot.key, path);
        auto *browse = new QPushButton(QStringLiteral("选择"), row);
        auto *clear = new QPushButton(QStringLiteral("清除"), row);
        layout->addWidget(path, 1);
        layout->addWidget(browse);
        layout->addWidget(clear);
        connect(clear, &QPushButton::clicked, path, &QLineEdit::clear);
        connect(browse, &QPushButton::clicked, this, [this, path, slot]() {
            QStringList patterns;
            for (const auto &extension : slot.extensions) patterns.append("*." + extension);
            const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("选择") + slot.label, path->text(),
                QStringLiteral("原始表格 (%1)").arg(patterns.join(' ')));
            if (!file.isEmpty()) selectSourceFile(slot.key, file);
        });
        m_fileLayout->addRow(slot.label + (slot.required ? QStringLiteral(" *") : QStringLiteral("（可选）")), row);
        if (!slot.notes.isEmpty()) {
            auto *note = new QLabel(slot.notes, m_form);
            note->setTextFormat(Qt::PlainText);
            note->setWordWrap(true);
            m_fileLayout->addRow(QString(), note);
        }
    }
}

void SubjectBalanceImportWidget::selectSourceFile(const QString &key, const QString &path)
{
    if (m_client->busy() || !m_fileInputs.contains(key) || !QFileInfo(path).isFile()) return;
    const BalanceImportSoftware *selected = nullptr;
    for (const auto &software : m_catalog)
        if (software.id == m_software->currentData().toInt()) { selected = &software; break; }
    if (!selected) return;
    const QString directory = QFileInfo(path).absolutePath();
    QMap<QString, QString> existing;
    for (auto it = m_fileInputs.begin(); it != m_fileInputs.end(); ++it) {
        auto *edit = it.value();
        if (edit->property("autoSuggested").toBool() && QFileInfo(edit->text()).absolutePath() != directory) {
            edit->clear();
            edit->setProperty("autoSuggested", false);
        }
        if (!edit->text().isEmpty()) existing.insert(it.key(), edit->text());
    }
    const QString absolute = QFileInfo(path).absoluteFilePath();
    m_fileInputs[key]->setText(absolute);
    m_fileInputs[key]->setToolTip(absolute);
    m_fileInputs[key]->setProperty("autoSuggested", false);
    existing[key] = absolute;
    const auto suggestions = suggestBalanceFiles(*selected, absolute, existing);
    QStringList filled;
    for (const auto &slot : selected->files) {
        if (suggestions.matches.contains(slot.key)) {
            const auto file = suggestions.matches.value(slot.key);
            m_fileInputs[slot.key]->setText(file);
            m_fileInputs[slot.key]->setToolTip(QStringLiteral("按文件名自动匹配，请核对：\n") + file);
            m_fileInputs[slot.key]->setProperty("autoSuggested", true);
            filled.append(slot.label);
        }
    }
    QStringList messages;
    if (!filled.isEmpty()) messages.append(QStringLiteral("已按同目录文件名补充：%1。导账前请核对。").arg(filled.join(QStringLiteral("、"))));
    if (messages.isEmpty()) messages.append(QStringLiteral("已选择文件；同目录未发现可补充的明确匹配。"));
    m_fileHint->setText(messages.join("\n"));
}

void SubjectBalanceImportWidget::addAuxiliary()
{
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("选择辅助核算附件"), {}, "Excel (*.xls *.xlsx)");
    if (file.isEmpty()) return;
    const int row = m_auxiliary->rowCount();
    m_auxiliary->show();
    m_auxiliary->insertRow(row);
    m_auxiliary->setItem(row, 0, new QTableWidgetItem);
    auto *path = new QTableWidgetItem(file);
    path->setFlags(path->flags() & ~Qt::ItemIsEditable);
    path->setToolTip(file);
    m_auxiliary->setItem(row, 1, path);
    auto *remove = new QPushButton(QStringLiteral("移除"), m_auxiliary);
    m_auxiliary->setCellWidget(row, 2, remove);
    connect(remove, &QPushButton::clicked, this, [this, remove]() {
        for (int i = 0; i < m_auxiliary->rowCount(); ++i)
            if (m_auxiliary->cellWidget(i, 2) == remove) {
                m_auxiliary->removeRow(i);
                if (!m_auxiliary->rowCount()) m_auxiliary->hide();
                break;
            }
    });
    m_auxiliary->editItem(m_auxiliary->item(row, 0));
}

BalanceImportSession SubjectBalanceImportWidget::session() const
{
    BalanceImportSession result;
    result.host = m_host->currentData().toString();
    if (m_global) {
        result.token = m_global->m_strNingMengCookie.trimmed();
        if (result.token.isEmpty()) result.token = m_global->m_userToken.m_strAccessToken.trimmed();
        result.serviceId = m_global->m_nServiceId;
        result.owner = m_global->m_strSN;
        if (result.owner.isEmpty() && m_global->m_nUserId > 0) result.owner = "user:" + QString::number(m_global->m_nUserId);
    }
    return result;
}

void SubjectBalanceImportWidget::submit()
{
    if (m_software->currentIndex() <= 0 || m_software->currentText() != m_software->itemText(m_software->currentIndex())) {
        m_status->setText(QStringLiteral("请从搜索结果或下拉列表选择来源软件"));
        m_software->setFocus();
        return;
    }
    if (m_client->busy()) return;
    BalanceImportInput input;
    input.software = m_software->currentData().toInt();
    input.accountName = m_name->text().trimmed();
    input.startMonth = m_month->date();
    input.accountingStandard = m_standard->currentData().toInt();
    input.taxType = m_tax->currentData().toInt();
    input.importZero = m_zero->isChecked();
    for (auto it = m_fileInputs.constBegin(); it != m_fileInputs.constEnd(); ++it)
        if (!it.value()->text().isEmpty()) input.files.insert(it.key(), it.value()->text());
    for (int row = 0; row < m_auxiliary->rowCount(); ++row) {
        const QString key = m_auxiliary->item(row, 0)->text().trimmed();
        if (key.isEmpty() || input.files.contains(key) || m_fileInputs.contains(key)) {
            m_status->setText(QStringLiteral("请填写不重复的辅助类别名称，不能使用余额表、科目表或凭证表的字段名")); return;
        }
        input.files.insert(key, m_auxiliary->item(row, 1)->text());
    }
    const auto current = session();
    const QString error = SubjectBalanceImport::validate(input, current);
    if (!error.isEmpty()) { m_status->setText(error); return; }
    if (current.owner.isEmpty()) { m_status->setText(QStringLiteral("当前登录用户信息不完整，请重新登录后导账")); return; }
    m_record = QJsonObject{{"owner", current.owner}, {"serviceId", current.serviceId}, {"host", current.host},
        {"software", input.software}, {"softwareName", m_software->currentText()}, {"accountName", input.accountName}};
    m_result->clear();
    m_client->submit(input, current);
}

void SubjectBalanceImportWidget::updateBusy(bool busy)
{
    setProperty("sessionBusyupdateBusy", busy);
    m_form->setEnabled(!busy);
    m_submit->setEnabled(!busy && !m_catalog.isEmpty());
    m_history->setEnabled(!busy);
    m_resume->setEnabled(!busy && m_history->count() > 0);
    if (!busy) m_stop->setEnabled(false);
}

QString SubjectBalanceImportWidget::historyPath() const
{
    if (!m_historyFilePath.isEmpty()) return m_historyFilePath;
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath("LemonCloud/LemonWorkTool/subject_balance_import_history.json");
}

void SubjectBalanceImportWidget::reloadHistory()
{
    const auto current = session();
    m_history->clear();
    if (!current.owner.isEmpty()) for (const auto &entry : readRecords(historyPath())) {
        const auto record = entry.toObject();
        if (record.value("owner").toString() != current.owner || record.value("serviceId").toInt() != current.serviceId) continue;
        const QString id = record.value("importId").toString();
        if (id.isEmpty()) continue;
        m_history->addItem(QStringLiteral("%1 | %2 | %3 | %4").arg(record.value("submittedAt").toString(),
            record.value("accountName").toString(), record.value("host").toString(), id), record.toVariantMap());
        m_history->setItemData(m_history->count() - 1, m_history->itemText(m_history->count() - 1), Qt::ToolTipRole);
    }
    if (m_history->count() == 0) m_history->setToolTip(QStringLiteral("当前登录用户和套餐暂无导账记录"));
    updateBusy(m_client->busy());
}

void SubjectBalanceImportWidget::saveRecord()
{
    const QString path = historyPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QLockFile lock(path + ".lock");
    if (!lock.tryLock(100)) {
        appendPlain(m_result, QStringLiteral("历史记录正在被使用，保存失败；请保留上方任务编号。")); return;
    }
    QJsonArray records;
    records.append(m_record);
    for (const auto &entry : readRecords(path)) {
        const auto old = entry.toObject();
        if (old.value("importId") == m_record.value("importId") && old.value("host") == m_record.value("host")
            && old.value("owner") == m_record.value("owner")) continue;
        if (records.size() >= 100) break;
        records.append(entry);
    }
    QSaveFile file(path);
    const auto bytes = QJsonDocument(records).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        appendPlain(m_result, QStringLiteral("保存历史记录失败，请保留上方任务编号。"));
    reloadHistory();
}

void SubjectBalanceImportWidget::resumeQuery()
{
    if (m_client->busy() || m_history->currentIndex() < 0) return;
    auto current = session();
    m_record = QJsonObject::fromVariantMap(m_history->currentData().toMap());
    if (m_record.value("owner").toString() != current.owner || m_record.value("serviceId").toInt() != current.serviceId) {
        m_status->setText(QStringLiteral("请切换到提交此任务时的登录账号和套餐")); return;
    }
    current.host = m_record.value("host").toString();
    const QString id = m_record.value("importId").toString();
    m_result->setPlainText(QStringLiteral("任务编号：%1\n提交域名：%2").arg(id, current.host));
    m_client->query(id, current);
    m_stop->setEnabled(m_client->busy());
}
