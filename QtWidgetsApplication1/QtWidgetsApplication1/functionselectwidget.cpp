#include "functionselectwidget.h"
#include "lemoncofighelper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QLineEdit>
#include <QFrame>
#include <QResizeEvent>

QString FunctionSelectWidget::categoryTitle(int category)
{
    static const QStringList titles = {QStringLiteral("工作台"), QStringLiteral("云端账套"),
        QStringLiteral("科目整理"), QStringLiteral("凭证修复"), QStringLiteral("文件处理"),
        QStringLiteral("查询与会话")};
    return titles.value(category);
}

QString FunctionSelectWidget::categoryDescription(int category)
{
    static const QStringList descriptions = {
        QStringLiteral("让每一次账套处理，都井然有序。"),
        QStringLiteral("管理云端账套，完成备份下载、专业版上传与账套清理。"),
        QStringLiteral("整理科目名称与编码，让层级、格式和对应关系保持一致。"),
        QStringLiteral("集中处理凭证日期、科目引用与辅助核算异常。"),
        QStringLiteral("从备份解包、格式转换到整套修复，准备好你的数据文件。"),
        QStringLiteral("检索 JSON、读取企业微信文档，或获取当前登录会话的快捷凭证。")};
    return descriptions.value(category);
}

int FunctionSelectWidget::categoryForFunction(int id)
{
    switch (id) {
    case 1: case 2: case 10: case 24: return 1;
    case 5: case 6: case 9: case 13: case 16: case 17: return 2;
    case 11: case 12: case 14: case 15: case 23: return 3;
    case 3: case 4: case 18: case 19: case 20: case 21: case 22: case 25: case 26: case 27: return 4;
    case 7: case 8: case 28: return 5;
    default: return 0;
    }
}

FunctionSelectWidget::FunctionSelectWidget(GlobalData *globalData, QWidget *parent)
    : QWidget(parent)
{
    Q_UNUSED(globalData);
    setObjectName("functionSelectPage");
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    m_pages = new QStackedWidget(this);
    m_pages->setObjectName("categoryPages");
    root->addWidget(m_pages);
    const QStringList accents = {"brand", "blue", "violet", "indigo", "brand", "amber"};
    const QStringList icons = {QStringLiteral("总"), QStringLiteral("云"), QStringLiteral("科"),
        QStringLiteral("凭"), QStringLiteral("包"), QStringLiteral("查")};
    for (int category = 0; category <= 5; ++category) {
        auto *scroll = new QScrollArea(m_pages);
        scroll->setObjectName(QString("categoryScroll%1").arg(category));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *canvas = new QWidget(scroll);
        canvas->setObjectName("dashboardCanvas");
        auto *layout = new QVBoxLayout(canvas);
        layout->setContentsMargins(26, 24, 26, 26);
        layout->setSpacing(20);
        auto *hero = new QFrame(canvas);
        hero->setObjectName("dashboardHero");
        auto *heroLayout = new QVBoxLayout(hero);
        heroLayout->setContentsMargins(26, 22, 26, 22);
        heroLayout->setSpacing(10);
        auto *eyebrowRow = new QHBoxLayout;
        auto *eyebrow = new QLabel(category == 0 ? QStringLiteral("LEMON / WORKSPACE")
            : QStringLiteral("TOOLKIT / 0%1").arg(category), hero);
        eyebrow->setObjectName("heroEyebrow");
        eyebrowRow->addWidget(eyebrow);
        eyebrowRow->addStretch();
        const QString environment = lemoncofighelper::CurrentHttpEnvironment();
        auto *env = new QLabel(QStringLiteral("●  %1").arg(environment.toUpper()), hero);
        env->setObjectName("labelCurrentEnvironment");
        env->setProperty("environment", environment.compare("Pro", Qt::CaseInsensitive) == 0 ? "pro" : "test");
        eyebrowRow->addWidget(env);
        heroLayout->addLayout(eyebrowRow);
        auto *title = new QLabel(category == 0 ? QStringLiteral("专注处理，高效完成。") : categoryTitle(category), hero);
        title->setObjectName("labelFuncTitle");
        title->setWordWrap(true);
        heroLayout->addWidget(title);
        auto *description = new QLabel(categoryDescription(category), hero);
        description->setObjectName("labelFuncSubtitle");
        description->setWordWrap(true);
        heroLayout->addWidget(description);
        if (category == 0) {
            auto *summary = new QLabel(QStringLiteral("05  功能分区     /     22  可用工具     /     01  规划中"), hero);
            summary->setObjectName("workspaceSummary");
            summary->setWordWrap(true);
            heroLayout->addWidget(summary);
        }
        layout->addWidget(hero);
        auto *search = new QLineEdit(canvas);
        search->setObjectName(QString("toolSearch%1").arg(category));
        search->setPlaceholderText(category == 0 ? QStringLiteral("搜索功能，例如：备份、科目、JSON…")
            : QStringLiteral("搜索当前分类中的工具…"));
        search->setClearButtonEnabled(true);
        search->setAccessibleName(search->placeholderText());
        layout->addWidget(search);
        CategoryPage page;
        page.search = search;
        page.heading = new QLabel(category == 0 ? QStringLiteral("功能分区 / 按场景开始")
            : QStringLiteral("全部工具"), canvas);
        page.heading->setProperty("sectionTitle", true);
        page.shortcutsHeading = new QLabel(QStringLiteral("快捷入口 / 常用任务"), canvas);
        page.shortcutsHeading->setProperty("sectionTitle", true);
        page.shortcutsHeading->hide();
        page.grid = new QGridLayout;
        page.grid->setSpacing(16);
        layout->addLayout(page.grid);
        page.empty = new QLabel(QStringLiteral("没有匹配的工具，试试其他关键词。"), canvas);
        page.empty->setObjectName("emptySearchState");
        page.empty->setAlignment(Qt::AlignCenter);
        page.empty->setWordWrap(true);
        layout->addWidget(page.empty);
        layout->addStretch();
        m_categories.append(page);
        scroll->setWidget(canvas);
        m_pages->addWidget(scroll);
        connect(search, &QLineEdit::textChanged, this, [this]() { reflowCards(); });
    }

    for (int category = 1; category <= 5; ++category) {
        auto *card = createFunctionButton(categoryTitle(category), categoryDescription(category),
            icons.at(category), QString("categoryCard%1").arg(category), accents.at(category), false,
            QStringLiteral("功能分区"));
        card->setProperty("categoryCard", true);
        card->setParent(m_categories[0].grid->parentWidget());
        card->hide();
        for (auto *label : card->findChildren<QLabel*>())
            if (label->property("cardAction").toBool())
                label->setText(QStringLiteral("进入分区"));
        m_categories[0].cards.append(card);
        connect(card, &QPushButton::clicked, this, [this, category]() { showCategory(category); });
    }

    auto addTool = [this](int id, const QString &title, const QString &description,
        const QString &icon, const QString &name, const QString &accent, bool featured, const QString &badge) {
        Q_UNUSED(featured);
        const int category = categoryForFunction(id);
        // One copy in its category; the dashboard copy is used for shortcuts and global search.
        for (int target : {category, 0}) {
            auto *card = createFunctionButton(title, description, icon,
                target == 0 ? name + "Shortcut" : name, accent, false, badge);
            card->setProperty("functionId", id);
            card->setParent(m_categories[target].grid->parentWidget());
            card->hide();
            card->setProperty("quickAccess", id == 2 || id == 3 || id == 10 || id == 20);
            m_categories[target].cards.append(card);
            connect(card, &QPushButton::clicked, this, [this, id]() { emit functionSelected(id); });
        }
    };
    addTool(1, QStringLiteral("\u5220\u9664\u8d26\u5957"),
        QStringLiteral("\u6279\u91cf\u9009\u62e9\u5e76\u79fb\u9664\u4e0d\u518d\u4f7f\u7528\u7684\u4e91\u7aef\u8d26\u5957"),
        QStringLiteral("\u5220"), "btnDeleteAccount", "danger", true, QStringLiteral("\u9ad8\u98ce\u9669"));
    addTool(2, QStringLiteral("\u5907\u4efd\u4e0b\u8f7d"),
        QStringLiteral("\u5c06\u4e91\u7aef\u8d26\u5957\u4e0b\u8f7d\u5230\u672c\u5730\uff0c\u4fbf\u4e8e\u5f52\u6863\u7559\u5b58"),
        QStringLiteral("\u5907"), "btnBackupAccount", "brand", true, QStringLiteral("\u5e38\u7528"));
    addTool(3, QStringLiteral("\u4fee\u590d\u8d26\u5957"),
        QStringLiteral("\u4f9d\u6b21\u5904\u7406\u79d1\u76ee\u7f16\u7801\u51b2\u7a81\u3001\u540c\u7ea7\u91cd\u540d\u548c\u8f85\u52a9\u6838\u7b97\u975e\u5fc5\u5f55"),
        QStringLiteral("\u4fee"), "btnRepairAccount", "blue", true, QStringLiteral("\u672c\u5730\u5de5\u5177"));
    addTool(13, QStringLiteral("\u7a7a\u79d1\u76ee\u540d\u79f0\u4fee\u590d"),
        QStringLiteral("\u79d1\u76ee\u540d\u79f0\u4e3a\u7a7a\u65f6\uff0c\u81ea\u52a8\u4f7f\u7528\u79d1\u76ee\u7f16\u7801\u586b\u5145"),
        QStringLiteral("\u79d1"), "btnEmptySubjectName", "brand", false, QStringLiteral("\u672c\u5730\u5de5\u5177"));
    addTool(12, QStringLiteral("\u51ed\u8bc1\u65e5\u671f\u5f02\u5e38\u4fee\u590d"),
        QStringLiteral("\u5c06\u4e0d\u5b58\u5728\u7684\u51ed\u8bc1\u65e5\u671f\u81ea\u52a8\u8c03\u6574\u4e3a\u5f53\u6708\u6700\u540e\u4e00\u5929"),
        QStringLiteral("\u65e5"), "btnVoucherDateRepair", "blue", false, QStringLiteral("\u672c\u5730\u5de5\u5177"));
    addTool(4, QStringLiteral("\u622a\u53d6\u6307\u5b9a\u5e74\u5ea6\u6570\u636e"),
        QStringLiteral("\u6309\u4f1a\u8ba1\u5e74\u5ea6\u62c6\u5206\u548c\u5bfc\u51fa\u8d26\u5957\u6570\u636e"),
        QStringLiteral("\u5e74"), "btnExtractYearData", "amber", false, QStringLiteral("\u89c4\u5212\u4e2d"));
    addTool(5, QStringLiteral("\u79d1\u76ee\u7f16\u7801\u51b2\u7a81\u5904\u7406"),
        QStringLiteral("\u5b9a\u4f4d\u91cd\u590d\u79d1\u76ee\u7f16\u7801\u5e76\u5b8c\u6210\u51b2\u7a81\u4fee\u6b63"),
        QStringLiteral("\u7801"), "btnSubjectCodeConflict", "violet", false, QStringLiteral("\u672c\u5730\u5de5\u5177"));
    addTool(6, QStringLiteral("\u540c\u7ea7\u79d1\u76ee\u91cd\u540d\u5904\u7406"),
        QStringLiteral("\u68c0\u67e5\u5e76\u6574\u7406\u540c\u7ea7\u79d1\u76ee\u4e2d\u7684\u540d\u79f0\u91cd\u590d"),
        QStringLiteral("\u540d"), "btnSiblingNameDuplicate", "violet", false, QStringLiteral("\u672c\u5730\u5de5\u5177"));
    addTool(7, QStringLiteral("\u590d\u5236\u767b\u5f55 Cookie"),
        QStringLiteral("\u4e00\u952e\u590d\u5236\u5f53\u524d\u4f1a\u8bdd\u7684\u767b\u5f55\u51ed\u8bc1"),
        QStringLiteral("\u94a5"), "btnCopyNingMengCookie", "slate", false, QStringLiteral("\u5feb\u6377"));
    addTool(8, QStringLiteral("JSON \u6570\u636e\u67e5\u8be2"),
        QStringLiteral("\u4ece\u672c\u5730 JSON \u4e2d\u5feb\u901f\u68c0\u7d22\u4e1a\u52a1\u6570\u636e"),
        QStringLiteral("查"), "btnJsonQuery", "amber", false, QStringLiteral("查询"));
    addTool(9, QStringLiteral("\u79d1\u76ee\u7f16\u7801\u6dfb\u52a0\u5206\u9694\u7b26"),
        QStringLiteral("\u6279\u91cf\u89c4\u8303\u79d1\u76ee\u7f16\u7801\u7684\u5c42\u7ea7\u683c\u5f0f"),
        QStringLiteral("\u5c42"), "btnSubjectCodeSeparator", "indigo", false, QStringLiteral("\u672c\u5730\u5de5\u5177"));
    addTool(10, QStringLiteral("\u4e0a\u4f20\u4e13\u4e1a\u7248\u8d26\u5957"),
        QStringLiteral("\u4e0a\u4f20\u4e13\u4e1a\u7248\u538b\u7f29\u5305\u5e76\u8ddf\u8e2a\u5bfc\u5165\u7ed3\u679c"),
        QStringLiteral("\u4e91"), "btnImportPackageUpload", "blue", false, QStringLiteral("\u4e91\u7aef\u4efb\u52a1"));
    addTool(11, QStringLiteral("\u8f85\u52a9\u6838\u7b97\u8865\u975e\u5fc5\u5f55"),
        QStringLiteral("\u8865\u9f50\u51ed\u8bc1\u8f85\u52a9\u6838\u7b97\u7684\u975e\u5fc5\u5f55\u914d\u7f6e"),
        QStringLiteral("\u6838"), "btnAllowNullAaType", "brand", false, QStringLiteral("\u672c\u5730\u5de5\u5177"));
    addTool(14, QStringLiteral("\u51ed\u8bc1\u4f59\u989d\u79d1\u76ee\u7f16\u7801\u4fee\u590d"),
        QStringLiteral("\u6839\u636e\u79d1\u76ee\u7f16\u7801\u524d\u7f00\u5c42\u7ea7\uff0c\u7edf\u4e00\u51ed\u8bc1\u548c\u4f59\u989d\u7684\u79d1\u76ee\u4f4d\u6570"),
        QStringLiteral("\u8865"), "btnReferenceAccountCodeRepair", "indigo", false, QStringLiteral("\u672c\u5730\u5de5\u5177"));
    addTool(15, QStringLiteral("\u51ed\u8bc1\u975e\u672b\u7ea7\u79d1\u76ee\u8f6c\u6362"),
        QStringLiteral("\u91cd\u7b97\u79d1\u76ee\u5c42\u7ea7\u4e0e\u672b\u7ea7\u6807\u8bb0\uff0c\u786e\u8ba4\u5e76\u8f6c\u6362\u975e\u672b\u7ea7\u51ed\u8bc1"),
        QStringLiteral("\u8f6c"), "btnNonLeafVoucherRepair", "violet", false, QStringLiteral("\u672c\u5730\u5de5\u5177"));
    addTool(16, QStringLiteral("科目名称两侧空格清理"),
        QStringLiteral("去除科目名称左右两侧的空白字符，保留名称中间内容"),
        QStringLiteral("清"), "btnTrimSubjectName", "brand", false, QStringLiteral("本地工具"));
    addTool(17, QStringLiteral("会计科目合并"),
        QStringLiteral("自由选择同父级科目，递归合并子科目、期初余额和凭证引用"),
        QStringLiteral("合"), "btnSubjectMerge", "violet", false, QStringLiteral("本地工具"));
    addTool(18, QStringLiteral("Access 2003 批量转换"),
        QStringLiteral("调用本机 Access 2003，将 AIS、AIY、MDB 批量转换为 Jet 4.0"),
        QStringLiteral("库"), "btnAccess2003Converter", "blue", false, QStringLiteral("本地工具"));
    addTool(20, QStringLiteral("恢复备份包"),
        QStringLiteral("选择用友 T3、U8、畅捷通 T+、金蝶专业版、浪潮、新中大或速达，恢复到本地实例或 211；速达导出 BAK"),
        QStringLiteral("库"), "btnBackupRestore", "blue", false, QStringLiteral("数据库工具"));
    addTool(21, QString::fromUtf8(u8"选择删除数据库"),
        QString::fromUtf8(u8"勾选本地或 211 实例中的数据库进行删除，支持全选"),
        QString::fromUtf8(u8"删"), "btnDatabaseCleanup", "danger", false, QString::fromUtf8(u8"数据库工具"));
    addTool(23, QStringLiteral("凭证编号修复"),
        QStringLiteral("整数小数转整数，非整数按当月最大编号递增，并显示修改明细"),
        QStringLiteral("号"), "btnVoucherGroupNumRepair", "blue", false, QStringLiteral("本地工具"));
    addTool(24, QStringLiteral("科目余额表导账"),
        QStringLiteral("按来源软件上传余额、科目、凭证等原始表格，自动查询导账结果"),
        QStringLiteral("表"), "btnSubjectBalanceImport", "blue", false, QStringLiteral("云端任务"));
    addTool(28, QStringLiteral("企业微信文档读取"),
        QStringLiteral("扫码读取今天及之后的日期工作表，查看内容并导出 JSON"),
        QStringLiteral("文"), "btnWecomDocument", "amber", false, QStringLiteral("在线文档"));
    showCategory(0);
}

int FunctionSelectWidget::currentCategory() const { return m_pages->currentIndex(); }

void FunctionSelectWidget::showCategory(int category)
{
    if (category < 0 || category >= m_categories.size())
        return;
    m_pages->setCurrentIndex(category);
    reflowCards();
    emit categoryChanged(category);
}

void FunctionSelectWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    reflowCards();
}

void FunctionSelectWidget::reflowCards()
{
    if (!m_pages || m_categories.isEmpty())
        return;
    const int category = currentCategory();
    auto &page = m_categories[category];
    const QString query = page.search->text().trimmed();
    const int columns = width() >= 1000 ? 3 : width() >= 610 ? 2 : 1;
    while (auto *item = page.grid->takeAt(0))
        delete item;
    for (int col = 0; col < 3; ++col)
        page.grid->setColumnStretch(col, col < columns ? 1 : 0);
    page.heading->setVisible(query.isEmpty());
    page.shortcutsHeading->hide();
    int row = 0;
    if (query.isEmpty())
        page.grid->addWidget(page.heading, row++, 0, 1, columns);
    int index = 0;
    int column = 0;
    bool shortcutsStarted = false;
    for (auto *card : page.cards) {
        bool visible = query.isEmpty() || card->toolTip().contains(query, Qt::CaseInsensitive);
        if (category == 0) {
            visible = query.isEmpty()
                ? card->property("categoryCard").toBool() || card->property("quickAccess").toBool()
                : !card->property("categoryCard").toBool() && visible;
        }
        card->setVisible(visible);
        if (visible) {
            if (category == 0 && query.isEmpty() && !card->property("categoryCard").toBool()
                && !shortcutsStarted) {
                if (column != 0) { ++row; column = 0; }
                page.shortcutsHeading->show();
                page.grid->addWidget(page.shortcutsHeading, row++, 0, 1, columns);
                shortcutsStarted = true;
            }
            page.grid->addWidget(card, row, column);
            if (++column == columns) { ++row; column = 0; }
            ++index;
        }
    }
    page.empty->setVisible(index == 0);
}

QPushButton* FunctionSelectWidget::createFunctionButton(const QString &title,
                                                        const QString &description,
                                                        const QString &iconText,
                                                        const QString &objectName,
                                                        const QString &accent,
                                                        bool featured,
                                                        const QString &badgeText)
{
    QPushButton *btn = new QPushButton(this);
    btn->setObjectName(objectName);
    btn->setProperty("funcButton", true);
    btn->setProperty("functionCard", true);
    btn->setProperty("featured", featured);
    btn->setProperty("accent", accent);
    btn->setProperty("planned", badgeText == QStringLiteral("\u89c4\u5212\u4e2d"));
    btn->setMinimumSize(featured ? QSize(0, 192) : QSize(0, 192));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    btn->setFocusPolicy(Qt::StrongFocus);
    btn->setAccessibleName(title);
    btn->setToolTip(title + QStringLiteral("\n") + description);

    QVBoxLayout *cardLayout = new QVBoxLayout(btn);
    cardLayout->setContentsMargins(featured ? 18 : 16, featured ? 16 : 14,
                                   featured ? 18 : 16, featured ? 14 : 13);
    cardLayout->setSpacing(featured ? 7 : 5);

    QHBoxLayout *topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(8);
    QLabel *iconLabel = new QLabel(iconText, btn);
    iconLabel->setProperty("cardIcon", true);
    iconLabel->setProperty("accent", accent);
    iconLabel->setFixedSize(featured ? QSize(40, 40) : QSize(36, 36));
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    topRow->addWidget(iconLabel);
    topRow->addStretch();

    if (!badgeText.isEmpty())
    {
        QLabel *badgeLabel = new QLabel(badgeText, btn);
        badgeLabel->setProperty("cardBadge", true);
        badgeLabel->setProperty("accent", accent);
        badgeLabel->setAlignment(Qt::AlignCenter);
        badgeLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        topRow->addWidget(badgeLabel, 0, Qt::AlignTop);
    }
    cardLayout->addLayout(topRow);

    QLabel *titleLabel = new QLabel(title, btn);
    titleLabel->setProperty("cardTitle", true);
    titleLabel->setWordWrap(true);
    titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    cardLayout->addWidget(titleLabel);

    QLabel *descriptionLabel = new QLabel(description, btn);
    descriptionLabel->setProperty("cardDescription", true);
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    cardLayout->addWidget(descriptionLabel);
    cardLayout->addStretch();

    QHBoxLayout *actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(5);
    const bool planned = badgeText == QStringLiteral("\u89c4\u5212\u4e2d");
    QLabel *actionLabel = new QLabel(planned ? QStringLiteral("\u67e5\u770b\u8bf4\u660e")
                                             : QStringLiteral("\u6253\u5f00\u5de5\u5177"), btn);
    actionLabel->setProperty("cardAction", true);
    actionLabel->setProperty("accent", accent);
    actionLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    QLabel *arrowLabel = new QLabel(QStringLiteral("\u2192"), btn);
    arrowLabel->setProperty("cardArrow", true);
    arrowLabel->setProperty("accent", accent);
    arrowLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    actionRow->addWidget(actionLabel);
    actionRow->addWidget(arrowLabel);
    actionRow->addStretch();
    cardLayout->addLayout(actionRow);
    return btn;
}
