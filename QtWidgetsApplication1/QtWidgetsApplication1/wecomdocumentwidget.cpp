#include "wecomdocumentwidget.h"
#include "Common/WecomBrowser.h"
#include "Common/WecomDocumentReader.h"
#include "Common/comboboxpopup.h"
#include <QAbstractTableModel>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTableView>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace {
// Sparse model: empty worksheet rows stay in their original positions without allocating items.
class SheetModel final : public QAbstractTableModel
{
public:
    SheetModel(const WecomDocument::Sheet &sheet, QObject *parent) : QAbstractTableModel(parent), m_sheet(sheet) {
        for (int i = 0; i < m_sheet.cells.size(); ++i) m_cells.insert(key(m_sheet.cells[i].row, m_sheet.cells[i].column), i);
    }
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_sheet.rowCount; }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_sheet.columnCount; }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid()) return {};
        const auto found = m_cells.constFind(key(index.row(), index.column()));
        if (found == m_cells.cend()) return {};
        const auto &cell = m_sheet.cells[*found];
        if (role == Qt::DisplayRole) return cell.text;
        if (role == Qt::ToolTipRole) return cell.links.isEmpty() ? cell.text : cell.text + "\n" + cell.links.join("\n");
        if (role == Qt::UserRole) return cell.links;
        return {};
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (role != Qt::DisplayRole) return {};
        if (orientation == Qt::Vertical) return section + 1;
        QString name;
        for (int value = section + 1; value; value = (value - 1) / 26) name.prepend(QChar('A' + (value - 1) % 26));
        return name;
    }
private:
    static quint64 key(int row, int column) { return (quint64(row) << 32) | quint32(column); }
    WecomDocument::Sheet m_sheet;
    QHash<quint64, int> m_cells;
};
}

WecomDocumentWidget::WecomDocumentWidget(QWidget *parent, const QString &catalogPath)
    : QWidget(parent), m_catalog(catalogPath)
{
    setObjectName("wecomDocumentPage");
    auto *layout = new QVBoxLayout(this);
    auto *back = new QPushButton(QStringLiteral("返回分类"), this);
    back->setObjectName("btnWecomBack"); connect(back, &QPushButton::clicked, this, &WecomDocumentWidget::backToMain);
    layout->addWidget(back, 0, Qt::AlignLeft);
    auto *documentControls = new QHBoxLayout;
    m_documents = new QComboBox(this);
    m_documents->setObjectName("wecomDocumentSelector");
    m_documents->setAccessibleName(QStringLiteral("企业微信表格"));
    m_documents->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_documents->setMaxVisibleItems(8);
    spaceComboItems(m_documents->view());
    m_manualAccess = new QPushButton(QStringLiteral("手动访问"), this);
    m_manualAccess->setObjectName("btnWecomManualAccess");
    m_addDocument = new QPushButton(QStringLiteral("添加到列表"), this);
    m_addDocument->setObjectName("btnWecomAddDocument");
    documentControls->addWidget(m_documents, 1);
    documentControls->addWidget(m_manualAccess);
    documentControls->addWidget(m_addDocument);
    layout->addLayout(documentControls);
    const QString catalogError = m_catalog.load();
    populateDocuments();
    auto *buttons = new QHBoxLayout;
    m_login = new QPushButton(QStringLiteral("扫码登录"), this); m_login->setObjectName("btnWecomLogin");
    m_read = new QPushButton(QStringLiteral("获取最新数据"), this); m_read->setObjectName("btnWecomRead");
    m_cancel = new QPushButton(QStringLiteral("停止获取"), this); m_cancel->setEnabled(false);
    m_export = new QPushButton(QStringLiteral("导出 JSON"), this); m_export->setObjectName("btnWecomExport"); m_export->setEnabled(false);
    for (auto *button : {m_login, m_read, m_cancel, m_export}) buttons->addWidget(button);
    buttons->addStretch(); layout->addLayout(buttons);
    m_status = new QLabel(catalogError.isEmpty() ? QStringLiteral("尚未获取数据。") : catalogError, this);
    m_status->setObjectName("wecomReadStatus"); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
    m_tabs = new QTabWidget(this); m_tabs->setObjectName("wecomSheetTabs"); m_tabs->setMinimumHeight(360); layout->addWidget(m_tabs, 1);

    m_loginDialog = new QDialog(this); m_loginDialog->setWindowTitle(QStringLiteral("企业微信文档 · 扫码登录"));
    m_loginDialog->setWindowModality(Qt::WindowModal); m_loginDialog->resize(1000, 720);
    auto *loginLayout = new QVBoxLayout(m_loginDialog);
    auto *help = new QLabel(QStringLiteral("请在企业微信官方页面完成扫码。识别到目标文档后窗口会自动关闭。"), m_loginDialog);
    help->setWordWrap(true); loginLayout->addWidget(help);
    m_browser = new WecomBrowser(m_loginDialog); loginLayout->addWidget(m_browser, 1);
    auto *sessionStatus = new QLabel(m_browser->hasSavedSession()
        ? QStringLiteral("发现本地登录记录，获取数据时会验证是否有效。")
        : QStringLiteral("首次扫码后自动加密保存登录；重开可复用，授权有效期由企业微信决定。"), this);
    sessionStatus->setObjectName("wecomSessionStatus"); sessionStatus->setWordWrap(true); sessionStatus->setTextFormat(Qt::PlainText);
    layout->insertWidget(layout->indexOf(m_tabs), sessionStatus);
    m_assistantStatus = new QLabel(QStringLiteral("本机读取接口随工作台启动。"), this);
    m_assistantStatus->setObjectName("wecomAssistantStatus"); m_assistantStatus->setWordWrap(true); m_assistantStatus->setTextFormat(Qt::PlainText);
    layout->insertWidget(layout->indexOf(m_tabs), m_assistantStatus);
    connect(m_browser, &WecomBrowser::sessionStatus, sessionStatus, &QLabel::setText);
    m_reader = new WecomDocumentReader(m_browser, this);
    connect(m_reader, &WecomDocumentReader::loginRequired, m_loginDialog, &QDialog::show);
    connect(m_reader, &WecomDocumentReader::loginRequired, this, [this]() {
        finishAssistant(WecomLocalServer::error("login_required", QStringLiteral("请在工具的企业微信文档页面完成登录；完成后重新发起读取。")));
    });
    connect(m_reader, &WecomDocumentReader::authenticated, m_loginDialog, &QDialog::hide);
    connect(m_loginDialog, &QDialog::rejected, m_reader, &WecomDocumentReader::cancel);
    connect(m_reader, &WecomDocumentReader::busyChanged, this, [this](bool busy) {
        setProperty("sessionBusyWecomRead", busy); m_login->setEnabled(!busy); m_read->setEnabled(!busy);
        m_documents->setEnabled(!busy);
        m_manualAccess->setEnabled(!busy);
        m_addDocument->setEnabled(!busy);
        m_cancel->setEnabled(busy);
        m_export->setEnabled(!busy && m_snapshot.fetchedAt.isValid());
    });
    connect(m_reader, &WecomDocumentReader::progress, this, [this](const QString &message) {
        (m_showReadInPage ? m_status : m_assistantStatus)->setText(message);
    });
    connect(m_reader, &WecomDocumentReader::failed, this, [this](const QString &message) {
        m_loginDialog->hide();
        (m_showReadInPage ? m_status : m_assistantStatus)->setText(message);
        finishAssistant(WecomLocalServer::error("read_failed", message));
    });
    connect(m_reader, &WecomDocumentReader::finished, this, [this](const WecomDocument::Snapshot &snapshot) {
        if (m_showReadInPage) showSnapshot(snapshot);
        else m_assistantStatus->setText(QStringLiteral("本机助手读取已结束。"));
        if (m_assistantReply) finishAssistant(WecomLocalServer::success(snapshot.toJson()));
    });
    connect(m_login, &QPushButton::clicked, this, [this]() {
        m_showReadInPage = true;
        clearResults();
        m_reader->signIn(selectedDocument());
    });
    connect(m_read, &QPushButton::clicked, this, [this]() {
        m_showReadInPage = true;
        clearResults();
        m_reader->read(selectedDocument());
    });
    connect(m_cancel, &QPushButton::clicked, m_reader, &WecomDocumentReader::cancel);
    connect(m_export, &QPushButton::clicked, this, &WecomDocumentWidget::exportJson);
    connect(m_manualAccess, &QPushButton::clicked, this, [this]() { openDocumentDialog(false); });
    connect(m_addDocument, &QPushButton::clicked, this, [this]() { openDocumentDialog(true); });
    connect(m_documents, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
        clearResults();
        m_status->setText(selectedDocument().selection == WecomDocument::SheetSelection::LatestDate
            ? QStringLiteral("获取数据时只读取页签日期最新的一页。")
            : QStringLiteral("已切换表格，请获取最新数据。"));
    });
}
WecomDocumentWidget::~WecomDocumentWidget()
{
    finishAssistant(WecomLocalServer::error("tool_closing", QStringLiteral("工具正在关闭，请重新打开后重试。")));
    delete m_reader; delete m_loginDialog;
}
void WecomDocumentWidget::setAssistantStatus(const QString &message) { m_assistantStatus->setText(message); }
void WecomDocumentWidget::finishAssistant(const QJsonObject &response)
{
    auto reply = std::move(m_assistantReply); m_assistantReply = {};
    if (reply) reply(response);
}
QJsonObject WecomDocumentWidget::assistantDocuments() const
{
    auto catalog = m_catalog;
    const QString error = catalog.load();
    if (!error.isEmpty()) return WecomLocalServer::error("catalog_error", error);
    return WecomLocalServer::success(catalog.documentList());
}

void WecomDocumentWidget::readForAssistant(const QDate &fromDate, const QString &documentId, const QUrl &url,
                                          WecomLocalServer::Reply reply)
{
    if (m_reader->isBusy() || m_assistantReply) {
        reply(WecomLocalServer::error("busy", QStringLiteral("工具正在登录或读取文档，请等待完成后重试。"))); return;
    }
    auto catalog = m_catalog;
    const QString error = catalog.load();
    if (!error.isEmpty() && (!url.isEmpty() || !documentId.isEmpty())) {
        reply(WecomLocalServer::error("catalog_error", error));
        return;
    }
    const auto document = !error.isEmpty()
        ? WecomDocument::Document{QString(), WecomDocument::defaultUrl()}
        : (url.isEmpty() ? catalog.entryForRead(documentId) : catalog.entryForUrl(url));
    if (document.url.isEmpty()) {
        reply(WecomLocalServer::error("document_not_found", QStringLiteral("当前表格清单中没有此文档。")));
        return;
    }
    m_assistantReply = std::move(reply);
    m_showReadInPage = WecomDocument::documentIdFromUrl(selectedDocumentUrl()) == WecomDocument::documentIdFromUrl(document.url);
    if (m_showReadInPage) clearResults();
    (m_showReadInPage ? m_status : m_assistantStatus)->setText(QStringLiteral("助手已请求读取企业微信表格…"));
    m_reader->read(document, fromDate);
}

void WecomDocumentWidget::populateDocuments(const QUrl &selected)
{
    const QSignalBlocker blockSelection(m_documents);
    m_documents->clear();
    m_temporaryIndex = -1;
    for (const auto &entry : m_catalog.entries()) m_documents->addItem(entry.name, entry.url);
    const int index = m_documents->findData(selected);
    if (index >= 0) m_documents->setCurrentIndex(index);
}

QUrl WecomDocumentWidget::selectedDocumentUrl() const
{
    return m_documents->currentData().toUrl();
}

WecomDocument::Document WecomDocumentWidget::selectedDocument() const
{
    return m_catalog.entryForUrl(selectedDocumentUrl());
}

void WecomDocumentWidget::openDocumentDialog(bool saveToCatalog)
{
    QDialog dialog(this);
    dialog.setWindowTitle(saveToCatalog ? QStringLiteral("添加到列表") : QStringLiteral("手动访问"));
    dialog.setMinimumWidth(480);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *urlEditor = new QLineEdit(&dialog);
    urlEditor->setObjectName("wecomDocumentLinkInput");
    urlEditor->setPlaceholderText(QStringLiteral("粘贴企业微信表格链接"));
    auto *nameEditor = new QLineEdit(&dialog);
    nameEditor->setObjectName("wecomDocumentNameInput");
    if (saveToCatalog) {
        m_catalog.load();
        nameEditor->setPlaceholderText(QStringLiteral("下拉框中显示的名称"));
        form->addRow(QStringLiteral("表格名称"), nameEditor);
        if (m_temporaryIndex >= 0 && m_documents->currentIndex() == m_temporaryIndex) {
            urlEditor->setText(selectedDocumentUrl().toString(QUrl::FullyEncoded));
            nameEditor->setText(m_snapshot.title);
        }
    } else {
        nameEditor->hide();
    }
    form->addRow(QStringLiteral("表格链接"), urlEditor);
    layout->addLayout(form);
    auto *message = new QLabel(&dialog);
    message->setWordWrap(true);
    message->setTextFormat(Qt::PlainText);
    message->hide();
    layout->addWidget(message);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(saveToCatalog ? QStringLiteral("保存") : QStringLiteral("访问"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QUrl url;
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        url = QUrl(urlEditor->text().trimmed(), QUrl::StrictMode);
        QString error;
        if (WecomDocument::validateUrl(url, &error) && saveToCatalog) {
            const QString name = nameEditor->text().trimmed();
            if (name.isEmpty()) {
                error = QStringLiteral("请填写表格名称。");
            } else {
                const bool replace = !m_catalog.urlForId(WecomDocument::documentIdFromUrl(url)).isEmpty();
                if (replace && QMessageBox::question(&dialog, QStringLiteral("更新表格绑定"),
                    QStringLiteral("这张表格已在列表中。要更新它的名称和链接吗？"),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
                m_catalog.save({name, url, WecomDocument::SheetSelection::All}, replace, &error);
            }
        }
        if (error.isEmpty()) {
            dialog.accept();
        } else {
            message->setText(error);
            message->show();
        }
    });
    if (dialog.exec() != QDialog::Accepted) return;
    if (saveToCatalog) {
        populateDocuments(url);
        clearResults();
        m_status->setText(QStringLiteral("已保存表格，可以获取最新数据。"));
    } else {
        accessTemporaryDocument(url);
    }
}

void WecomDocumentWidget::accessTemporaryDocument(const QUrl &url)
{
    const QString label = QStringLiteral("临时访问");
    if (m_temporaryIndex < 0) {
        m_temporaryIndex = m_documents->count();
        m_documents->addItem(label, url);
    } else {
        m_documents->setItemText(m_temporaryIndex, label);
        m_documents->setItemData(m_temporaryIndex, url);
    }
    m_documents->setCurrentIndex(m_temporaryIndex);
    m_showReadInPage = true;
    clearResults();
    m_reader->read(selectedDocument());
}
void WecomDocumentWidget::clearResults()
{
    while (m_tabs->count()) { auto *page = m_tabs->widget(0); m_tabs->removeTab(0); delete page; }
    m_snapshot = {}; m_export->setEnabled(false);
}
void WecomDocumentWidget::showSnapshot(const WecomDocument::Snapshot &snapshot)
{
    clearResults(); m_snapshot = snapshot; m_loginDialog->hide();
    if (m_temporaryIndex >= 0 && m_documents->currentIndex() == m_temporaryIndex && !snapshot.title.isEmpty()) {
        m_documents->setItemText(m_temporaryIndex, snapshot.title);
    }
    int loaded = 0;
    for (const auto &sheet : snapshot.sheets) {
        auto *page = new QWidget(m_tabs); auto *layout = new QVBoxLayout(page);
        auto *status = new QLabel(sheet.loaded ? QStringLiteral("%1 行 × %2 列 · %3 个有值单元格；悬停查看完整文本和链接，Ctrl+C 复制选区。").arg(sheet.rowCount).arg(sheet.columnCount).arg(sheet.cells.size()) : sheet.error, page);
        status->setTextFormat(Qt::PlainText); status->setWordWrap(true); layout->addWidget(status);
        if (sheet.loaded) {
            ++loaded;
            auto *table = new QTableView(page); table->setModel(new SheetModel(sheet, table));
            table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setAlternatingRowColors(true);
            table->setSelectionMode(QAbstractItemView::ContiguousSelection); table->horizontalHeader()->setDefaultSectionSize(160);
            table->verticalHeader()->setDefaultSectionSize(32); layout->addWidget(table);
            auto *copy = new QShortcut(QKeySequence::Copy, table); copy->setContext(Qt::WidgetWithChildrenShortcut);
            connect(copy, &QShortcut::activated, table, [table]() {
                auto selection = table->selectionModel()->selection(); if (selection.isEmpty()) return;
                const auto range = selection.first(); QStringList rows;
                if (qint64(range.height()) * range.width() > 100000) return;
                for (int row = range.top(); row <= range.bottom(); ++row) {
                    QStringList cells;
                    for (int col = range.left(); col <= range.right(); ++col) cells.append(table->model()->index(row, col).data().toString());
                    rows.append(cells.join('\t'));
                }
                QApplication::clipboard()->setText(rows.join('\n'));
            });
        }
        m_tabs->addTab(page, sheet.name + (sheet.loaded ? QString() : QStringLiteral("（未完成）")));
    }
    m_export->setEnabled(true);
    const bool latest = snapshot.selection == WecomDocument::SheetSelection::LatestDate;
    const bool all = snapshot.selection == WecomDocument::SheetSelection::All;
    if (snapshot.sheets.isEmpty()) {
        QString message = QStringLiteral("获取完成：没有工作表。");
        if (latest) message = QStringLiteral("获取完成：没有可从名称识别日期的工作表。");
        else if (!all) message = QStringLiteral("获取完成：没有 %1 及之后的日期工作表。").arg(snapshot.fromDate.toString(Qt::ISODate));
        m_status->setText(message);
        return;
    }
    QString filter = QStringLiteral("全部页签");
    if (latest) filter = QStringLiteral("最新页签日期 %1").arg(snapshot.sheets.first().date.toString(Qt::ISODate));
    else if (!all) filter = QStringLiteral("筛选日期 %1").arg(snapshot.fromDate.toString(Qt::ISODate));
    m_status->setText(QStringLiteral("%1：成功读取 %2/%3 个工作表。%4；获取时间 %5。%6")
        .arg(snapshot.cancelled ? QStringLiteral("已停止") : (loaded == snapshot.sheets.size() ? QStringLiteral("获取完成") : QStringLiteral("部分获取成功")))
        .arg(loaded).arg(snapshot.sheets.size()).arg(filter)
        .arg(snapshot.fetchedAt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss"))
        .arg(loaded == snapshot.sheets.size() ? QString() : QStringLiteral("导出结果会保留未完成状态及原因。")));
}
void WecomDocumentWidget::exportJson()
{
    if (!m_snapshot.fetchedAt.isValid()) return;
    const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出企业微信文档数据"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/企业微信文档-" + m_snapshot.fetchedAt.toLocalTime().toString("yyyyMMdd-HHmmss") + ".json", "JSON (*.json)");
    if (path.isEmpty()) return;
    QSaveFile file(path); const QByteArray data = QJsonDocument(m_snapshot.toJson()).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), file.errorString()); return;
    }
    m_status->setText(QStringLiteral("JSON 已导出：%1").arg(path));
}
