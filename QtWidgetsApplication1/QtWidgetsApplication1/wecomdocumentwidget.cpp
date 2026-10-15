#include "wecomdocumentwidget.h"
#include "Common/WecomBrowser.h"
#include "Common/WecomDocumentReader.h"
#include <QAbstractTableModel>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QFileDialog>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QShortcut>
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

WecomDocumentWidget::WecomDocumentWidget(QWidget *parent) : QWidget(parent)
{
    setObjectName("wecomDocumentPage");
    auto *layout = new QVBoxLayout(this);
    auto *back = new QPushButton(QStringLiteral("返回分类"), this);
    back->setObjectName("btnWecomBack"); connect(back, &QPushButton::clicked, this, &WecomDocumentWidget::backToMain);
    layout->addWidget(back, 0, Qt::AlignLeft);
    auto *intro = new QLabel(QStringLiteral("读取“2026-运营支持组项目进度表”中今天及之后的日期工作表。扫码登录后点击获取，结果可导出为 JSON。"), this);
    intro->setWordWrap(true); layout->addWidget(intro);
    m_url = new QLineEdit(WecomDocument::defaultUrl().toString(), this);
    m_url->setObjectName("wecomDocumentUrl"); m_url->setPlaceholderText(QStringLiteral("粘贴这份文档的完整分享链接"));
    layout->addWidget(m_url);
    auto *buttons = new QHBoxLayout;
    m_login = new QPushButton(QStringLiteral("扫码登录"), this); m_login->setObjectName("btnWecomLogin");
    m_read = new QPushButton(QStringLiteral("获取最新数据"), this); m_read->setObjectName("btnWecomRead");
    m_cancel = new QPushButton(QStringLiteral("停止获取"), this); m_cancel->setEnabled(false);
    m_export = new QPushButton(QStringLiteral("导出 JSON"), this); m_export->setObjectName("btnWecomExport"); m_export->setEnabled(false);
    for (auto *button : {m_login, m_read, m_cancel, m_export}) buttons->addWidget(button);
    buttons->addStretch(); layout->addLayout(buttons);
    m_status = new QLabel(QStringLiteral("尚未获取数据。筛选日期按电脑本地日期计算。"), this);
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
        m_url->setEnabled(!busy); m_cancel->setEnabled(busy); m_export->setEnabled(!busy && m_snapshot.fetchedAt.isValid());
    });
    connect(m_reader, &WecomDocumentReader::progress, m_status, &QLabel::setText);
    connect(m_reader, &WecomDocumentReader::failed, this, [this](const QString &message) {
        m_loginDialog->hide(); m_status->setText(message);
        finishAssistant(WecomLocalServer::error("read_failed", message));
    });
    connect(m_reader, &WecomDocumentReader::finished, this, &WecomDocumentWidget::showSnapshot);
    connect(m_reader, &WecomDocumentReader::finished, this, [this](const WecomDocument::Snapshot &snapshot) {
        finishAssistant(WecomLocalServer::success(snapshot.toJson()));
    });
    connect(m_login, &QPushButton::clicked, this, [this]() { clearResults(); m_reader->signIn(QUrl(m_url->text().trimmed())); });
    connect(m_read, &QPushButton::clicked, this, [this]() { clearResults(); m_reader->read(QUrl(m_url->text().trimmed())); });
    connect(m_cancel, &QPushButton::clicked, m_reader, &WecomDocumentReader::cancel);
    connect(m_export, &QPushButton::clicked, this, &WecomDocumentWidget::exportJson);
    connect(m_url, &QLineEdit::textChanged, this, &WecomDocumentWidget::clearResults);
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
void WecomDocumentWidget::readForAssistant(const QDate &fromDate, WecomLocalServer::Reply reply)
{
    if (m_reader->isBusy() || m_assistantReply) {
        reply(WecomLocalServer::error("busy", QStringLiteral("工具正在登录或读取文档，请等待完成后重试。"))); return;
    }
    m_assistantReply = std::move(reply); clearResults();
    m_status->setText(QStringLiteral("助手已请求读取最新文档…"));
    m_reader->read(QUrl(m_url->text().trimmed()), fromDate);
}
void WecomDocumentWidget::clearResults()
{
    while (m_tabs->count()) { auto *page = m_tabs->widget(0); m_tabs->removeTab(0); delete page; }
    m_snapshot = {}; m_export->setEnabled(false);
}
void WecomDocumentWidget::showSnapshot(const WecomDocument::Snapshot &snapshot)
{
    clearResults(); m_snapshot = snapshot; m_loginDialog->hide();
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
    if (snapshot.sheets.isEmpty()) m_status->setText(QStringLiteral("获取完成：没有 %1 及之后的日期工作表。").arg(snapshot.fromDate.toString(Qt::ISODate)));
    else m_status->setText(QStringLiteral("%1：成功读取 %2/%3 个工作表。筛选日期 %4；获取时间 %5。%6")
        .arg(snapshot.cancelled ? QStringLiteral("已停止") : (loaded == snapshot.sheets.size() ? QStringLiteral("获取完成") : QStringLiteral("部分获取成功")))
        .arg(loaded).arg(snapshot.sheets.size()).arg(snapshot.fromDate.toString(Qt::ISODate))
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
