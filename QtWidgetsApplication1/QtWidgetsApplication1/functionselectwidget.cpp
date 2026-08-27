#include "functionselectwidget.h"
#include "Common/globaldata.h"
#include "lemoncofighelper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QFrame>
#include <QSizePolicy>
#include <QVariant>
#include <QResizeEvent>

FunctionSelectWidget::FunctionSelectWidget(GlobalData *globalData, QWidget *parent)
    : QWidget(parent),
      m_pGlobalData(globalData),
      m_pPrimaryGrid(nullptr),
      m_pToolsGrid(nullptr),
      m_pDashboardLayout(nullptr),
      m_pHeroLayout(nullptr),
      m_pMetricCard(nullptr),
      m_primaryColumnCount(0),
      m_toolColumnCount(0)
{
    setupUi();
}

void FunctionSelectWidget::setupUi()
{
    setObjectName("functionSelectPage");
    QVBoxLayout *pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setObjectName("dashboardScroll");
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->viewport()->setObjectName("dashboardViewport");

    QWidget *dashboardCanvas = new QWidget(scrollArea);
    dashboardCanvas->setObjectName("dashboardCanvas");
    QVBoxLayout *mainLayout = new QVBoxLayout(dashboardCanvas);
    m_pDashboardLayout = mainLayout;
    mainLayout->setContentsMargins(32, 28, 32, 34);
    mainLayout->setSpacing(0);
    mainLayout->setSizeConstraint(QLayout::SetMinimumSize);

    // 欢迎区：突出任务入口数量和当前可用状态。
    QFrame *hero = new QFrame(dashboardCanvas);
    hero->setObjectName("dashboardHero");
    QHBoxLayout *heroLayout = new QHBoxLayout(hero);
    m_pHeroLayout = heroLayout;
    heroLayout->setContentsMargins(28, 22, 22, 22);
    heroLayout->setSpacing(24);

    QVBoxLayout *heroTextLayout = new QVBoxLayout();
    heroTextLayout->setContentsMargins(0, 0, 0, 0);
    heroTextLayout->setSpacing(6);
    QHBoxLayout *eyebrowLayout = new QHBoxLayout();
    eyebrowLayout->setContentsMargins(0, 0, 0, 0);
    eyebrowLayout->setSpacing(10);
    QLabel *eyebrowLabel = new QLabel(QStringLiteral("LEMON CLOUD  ·  OPERATIONS"), hero);
    eyebrowLabel->setObjectName("heroEyebrow");
    eyebrowLayout->addWidget(eyebrowLabel);
    eyebrowLayout->addStretch();

    const bool productionEnvironment = lemoncofighelper::CurrentHttpEnvironment()
        .compare(QStringLiteral("Pro"), Qt::CaseInsensitive) == 0;
    QLabel *environmentLabel = new QLabel(
        productionEnvironment
            ? QStringLiteral("\u5f53\u524d\u73af\u5883\uff1aPRO")
            : QStringLiteral("\u5f53\u524d\u73af\u5883\uff1aTEST"),
        hero);
    environmentLabel->setObjectName("labelCurrentEnvironment");
    environmentLabel->setProperty("environment", productionEnvironment ? "pro" : "test");
    eyebrowLayout->addWidget(environmentLabel, 0, Qt::AlignVCenter);
    heroTextLayout->addLayout(eyebrowLayout);

    QLabel *titleLabel = new QLabel(QStringLiteral("\u4eca\u5929\u60f3\u5904\u7406\u4ec0\u4e48\uff1f"), hero);
    titleLabel->setObjectName("labelFuncTitle");
    titleLabel->setWordWrap(true);
    heroTextLayout->addWidget(titleLabel);

    QLabel *subtitleLabel = new QLabel(
        QStringLiteral("\u4ece\u5e38\u7528\u4efb\u52a1\u5f00\u59cb\uff0c\u6216\u4f7f\u7528\u6570\u636e\u5de5\u5177\u5b8c\u6210\u66f4\u7ec6\u81f4\u7684\u8d26\u5957\u5904\u7406\u3002"), hero);
    subtitleLabel->setObjectName("labelFuncSubtitle");
    subtitleLabel->setWordWrap(true);
    heroTextLayout->addWidget(subtitleLabel);
    heroTextLayout->addStretch();
    heroLayout->addLayout(heroTextLayout, 1);

    QFrame *metricCard = new QFrame(hero);
    m_pMetricCard = metricCard;
    metricCard->setObjectName("heroMetricCard");
    metricCard->setFixedWidth(184);
    QVBoxLayout *metricLayout = new QVBoxLayout(metricCard);
    metricLayout->setContentsMargins(18, 14, 18, 14);
    metricLayout->setSpacing(2);
    QLabel *metricValue = new QLabel(QStringLiteral("18"), metricCard);
    metricValue->setObjectName("heroMetricValue");
    QLabel *metricTitle = new QLabel(QStringLiteral("\u9879\u4efb\u52a1\u5165\u53e3"), metricCard);
    metricTitle->setObjectName("heroMetricTitle");
    QLabel *metricStatus = new QLabel(QStringLiteral("\u25cf  17 \u9879\u5df2\u5c31\u7eea"), metricCard);
    metricStatus->setObjectName("heroMetricStatus");
    metricLayout->addWidget(metricValue);
    metricLayout->addWidget(metricTitle);
    metricLayout->addStretch();
    metricLayout->addWidget(metricStatus);
    heroLayout->addWidget(metricCard);

    mainLayout->addWidget(hero);
    mainLayout->addSpacing(26);

    auto createSectionHeader = [dashboardCanvas](const QString &title,
                                                  const QString &subtitle,
                                                  const QString &countText) {
        QWidget *header = new QWidget(dashboardCanvas);
        header->setProperty("sectionHeader", true);
        QHBoxLayout *layout = new QHBoxLayout(header);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);

        QVBoxLayout *textLayout = new QVBoxLayout();
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(2);
        QLabel *titleLabel = new QLabel(title, header);
        titleLabel->setProperty("sectionTitle", true);
        QLabel *subtitleLabel = new QLabel(subtitle, header);
        subtitleLabel->setProperty("sectionSubtitle", true);
        subtitleLabel->setWordWrap(true);
        textLayout->addWidget(titleLabel);
        textLayout->addWidget(subtitleLabel);
        layout->addLayout(textLayout);
        layout->addStretch();

        QLabel *countLabel = new QLabel(countText, header);
        countLabel->setProperty("sectionCount", true);
        layout->addWidget(countLabel, 0, Qt::AlignVCenter);
        return header;
    };

    mainLayout->addWidget(createSectionHeader(QStringLiteral("\u5e38\u7528\u4efb\u52a1"),
                                               QStringLiteral("\u4e91\u7aef\u8d26\u5957\u7684\u6838\u5fc3\u7ba1\u7406\u64cd\u4f5c"),
                                               QStringLiteral("3 \u9879")));
    mainLayout->addSpacing(12);

    QPushButton *btn1 = createFunctionButton(
        QStringLiteral("\u5220\u9664\u8d26\u5957"),
        QStringLiteral("\u6279\u91cf\u9009\u62e9\u5e76\u79fb\u9664\u4e0d\u518d\u4f7f\u7528\u7684\u4e91\u7aef\u8d26\u5957"),
        QStringLiteral("\u5220"), "btnDeleteAccount", "danger", true, QStringLiteral("\u9ad8\u98ce\u9669"));
    QPushButton *btn2 = createFunctionButton(
        QStringLiteral("\u5907\u4efd\u4e0b\u8f7d"),
        QStringLiteral("\u5c06\u4e91\u7aef\u8d26\u5957\u4e0b\u8f7d\u5230\u672c\u5730\uff0c\u4fbf\u4e8e\u5f52\u6863\u7559\u5b58"),
        QStringLiteral("\u5907"), "btnBackupAccount", "brand", true, QStringLiteral("\u5e38\u7528"));
    QPushButton *btn3 = createFunctionButton(
        QStringLiteral("\u4fee\u590d\u8d26\u5957"),
        QStringLiteral("\u4f9d\u6b21\u5904\u7406\u79d1\u76ee\u7f16\u7801\u51b2\u7a81\u3001\u540c\u7ea7\u91cd\u540d\u548c\u8f85\u52a9\u6838\u7b97\u975e\u5fc5\u5f55"),
        QStringLiteral("\u4fee"), "btnRepairAccount", "blue", true, QStringLiteral("\u672c\u5730\u5de5\u5177"));

    m_pPrimaryGrid = new QGridLayout();
    m_pPrimaryGrid->setContentsMargins(0, 0, 0, 0);
    m_pPrimaryGrid->setHorizontalSpacing(14);
    m_pPrimaryGrid->setVerticalSpacing(14);
    m_primaryButtons << btn1 << btn2 << btn3;
    mainLayout->addLayout(m_pPrimaryGrid);
    mainLayout->addSpacing(28);

    mainLayout->addWidget(createSectionHeader(QStringLiteral("\u6570\u636e\u4e0e\u7ef4\u62a4\u5de5\u5177"),
                                               QStringLiteral("\u8986\u76d6\u6570\u636e\u6574\u7406\u3001\u68c0\u7d22\u548c\u5bfc\u5165\u7684\u4e13\u9879\u80fd\u529b"),
                                               QStringLiteral("15 \u9879")));
    mainLayout->addSpacing(12);

    QPushButton *btn13 = createFunctionButton(
        QStringLiteral("\u7a7a\u79d1\u76ee\u540d\u79f0\u4fee\u590d"),
        QStringLiteral("\u79d1\u76ee\u540d\u79f0\u4e3a\u7a7a\u65f6\uff0c\u81ea\u52a8\u4f7f\u7528\u79d1\u76ee\u7f16\u7801\u586b\u5145"),
        QStringLiteral("\u79d1"), "btnEmptySubjectName", "brand", false, QStringLiteral("\u672c\u5730\u5de5\u5177"));
    QPushButton *btn12 = createFunctionButton(
        QStringLiteral("\u51ed\u8bc1\u65e5\u671f\u5f02\u5e38\u4fee\u590d"),
        QStringLiteral("\u5c06\u4e0d\u5b58\u5728\u7684\u51ed\u8bc1\u65e5\u671f\u81ea\u52a8\u8c03\u6574\u4e3a\u5f53\u6708\u6700\u540e\u4e00\u5929"),
        QStringLiteral("\u65e5"), "btnVoucherDateRepair", "blue", false, QStringLiteral("\u672c\u5730\u5de5\u5177"));
    QPushButton *btn4 = createFunctionButton(
        QStringLiteral("\u622a\u53d6\u6307\u5b9a\u5e74\u5ea6\u6570\u636e"),
        QStringLiteral("\u6309\u4f1a\u8ba1\u5e74\u5ea6\u62c6\u5206\u548c\u5bfc\u51fa\u8d26\u5957\u6570\u636e"),
        QStringLiteral("\u5e74"), "btnExtractYearData", "amber", false, QStringLiteral("\u89c4\u5212\u4e2d"));
    QPushButton *btn5 = createFunctionButton(
        QStringLiteral("\u79d1\u76ee\u7f16\u7801\u51b2\u7a81\u5904\u7406"),
        QStringLiteral("\u5b9a\u4f4d\u91cd\u590d\u79d1\u76ee\u7f16\u7801\u5e76\u5b8c\u6210\u51b2\u7a81\u4fee\u6b63"),
        QStringLiteral("\u7801"), "btnSubjectCodeConflict", "violet", false, QStringLiteral("\u672c\u5730\u5de5\u5177"));
    QPushButton *btn6 = createFunctionButton(
        QStringLiteral("\u540c\u7ea7\u79d1\u76ee\u91cd\u540d\u5904\u7406"),
        QStringLiteral("\u68c0\u67e5\u5e76\u6574\u7406\u540c\u7ea7\u79d1\u76ee\u4e2d\u7684\u540d\u79f0\u91cd\u590d"),
        QStringLiteral("\u540d"), "btnSiblingNameDuplicate", "violet", false, QStringLiteral("\u672c\u5730\u5de5\u5177"));
    QPushButton *btn7 = createFunctionButton(
        QStringLiteral("\u590d\u5236\u767b\u5f55 Cookie"),
        QStringLiteral("\u4e00\u952e\u590d\u5236\u5f53\u524d\u4f1a\u8bdd\u7684\u767b\u5f55\u51ed\u8bc1"),
        QStringLiteral("\u94a5"), "btnCopyNingMengCookie", "slate", false, QStringLiteral("\u5feb\u6377"));
    QPushButton *btn8 = createFunctionButton(
        QStringLiteral("JSON \u6570\u636e\u67e5\u8be2"),
        QStringLiteral("\u4ece\u672c\u5730 JSON \u4e2d\u5feb\u901f\u68c0\u7d22\u4e1a\u52a1\u6570\u636e"),
        QStringLiteral("查"), "btnJsonQuery", "amber", false, QStringLiteral("查询"));
    QPushButton *btn9 = createFunctionButton(
        QStringLiteral("\u79d1\u76ee\u7f16\u7801\u6dfb\u52a0\u5206\u9694\u7b26"),
        QStringLiteral("\u6279\u91cf\u89c4\u8303\u79d1\u76ee\u7f16\u7801\u7684\u5c42\u7ea7\u683c\u5f0f"),
        QStringLiteral("\u5c42"), "btnSubjectCodeSeparator", "indigo", false, QStringLiteral("\u672c\u5730\u5de5\u5177"));
    QPushButton *btn10 = createFunctionButton(
        QStringLiteral("\u4e0a\u4f20\u4e13\u4e1a\u7248\u8d26\u5957"),
        QStringLiteral("\u4e0a\u4f20\u4e13\u4e1a\u7248\u538b\u7f29\u5305\u5e76\u8ddf\u8e2a\u5bfc\u5165\u7ed3\u679c"),
        QStringLiteral("\u4e91"), "btnImportPackageUpload", "blue", false, QStringLiteral("\u4e91\u7aef\u4efb\u52a1"));
    QPushButton *btn11 = createFunctionButton(
        QStringLiteral("\u8f85\u52a9\u6838\u7b97\u8865\u975e\u5fc5\u5f55"),
        QStringLiteral("\u8865\u9f50\u51ed\u8bc1\u8f85\u52a9\u6838\u7b97\u7684\u975e\u5fc5\u5f55\u914d\u7f6e"),
        QStringLiteral("\u6838"), "btnAllowNullAaType", "brand", false, QStringLiteral("\u672c\u5730\u5de5\u5177"));
    QPushButton *btn14 = createFunctionButton(
        QStringLiteral("\u51ed\u8bc1\u4f59\u989d\u79d1\u76ee\u7f16\u7801\u4fee\u590d"),
        QStringLiteral("\u6839\u636e\u79d1\u76ee\u7f16\u7801\u524d\u7f00\u5c42\u7ea7\uff0c\u7edf\u4e00\u51ed\u8bc1\u548c\u4f59\u989d\u7684\u79d1\u76ee\u4f4d\u6570"),
        QStringLiteral("\u8865"), "btnReferenceAccountCodeRepair", "indigo", false, QStringLiteral("\u672c\u5730\u5de5\u5177"));
    QPushButton *btn15 = createFunctionButton(
        QStringLiteral("\u51ed\u8bc1\u975e\u672b\u7ea7\u79d1\u76ee\u8f6c\u6362"),
        QStringLiteral("\u91cd\u7b97\u79d1\u76ee\u5c42\u7ea7\u4e0e\u672b\u7ea7\u6807\u8bb0\uff0c\u786e\u8ba4\u5e76\u8f6c\u6362\u975e\u672b\u7ea7\u51ed\u8bc1"),
        QStringLiteral("\u8f6c"), "btnNonLeafVoucherRepair", "violet", false, QStringLiteral("\u672c\u5730\u5de5\u5177"));
    QPushButton *btn16 = createFunctionButton(
        QStringLiteral("科目名称两侧空格清理"),
        QStringLiteral("去除科目名称左右两侧的空白字符，保留名称中间内容"),
        QStringLiteral("清"), "btnTrimSubjectName", "brand", false, QStringLiteral("本地工具"));
    QPushButton *btn17 = createFunctionButton(
        QStringLiteral("会计科目合并"),
        QStringLiteral("自由选择同父级科目，递归合并子科目、期初余额和凭证引用"),
        QStringLiteral("合"), "btnSubjectMerge", "violet", false, QStringLiteral("本地工具"));
    QPushButton *btn18 = createFunctionButton(
        QStringLiteral("Access 2003 批量转换"),
        QStringLiteral("调用本机 Access 2003，将 AIS、AIY、MDB 批量转换为 Jet 4.0"),
        QStringLiteral("库"), "btnAccess2003Converter", "blue", false, QStringLiteral("本地工具"));

    connect(btn1, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnDeleteAccount_clicked);
    connect(btn2, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnBackupAccount_clicked);
    connect(btn3, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnRepairAccount_clicked);
    connect(btn4, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnExtractYearData_clicked);
    connect(btn5, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnSubjectCodeConflict_clicked);
    connect(btn6, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnSiblingNameDuplicate_clicked);
    connect(btn7, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnCopyNingMengCookie_clicked);
    connect(btn8, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnJsonQuery_clicked);
    connect(btn9, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnSubjectCodeSeparator_clicked);
    connect(btn10, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnImportPackageUpload_clicked);
    connect(btn11, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnAllowNullAaType_clicked);
    connect(btn12, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnVoucherDateRepair_clicked);
    connect(btn13, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnEmptySubjectName_clicked);
    connect(btn14, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnReferenceAccountCodeRepair_clicked);
    connect(btn15, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnNonLeafVoucherRepair_clicked);
    connect(btn16, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnTrimSubjectName_clicked);
    connect(btn17, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnSubjectMerge_clicked);
    connect(btn18, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnAccess2003Converter_clicked);

    m_pToolsGrid = new QGridLayout();
    m_pToolsGrid->setContentsMargins(0, 0, 0, 0);
    m_pToolsGrid->setHorizontalSpacing(14);
    m_pToolsGrid->setVerticalSpacing(14);
    m_toolButtons << btn18 << btn17 << btn16 << btn15 << btn14 << btn13 << btn12 << btn4 << btn5
                  << btn6 << btn7 << btn8 << btn9 << btn10 << btn11;
    mainLayout->addLayout(m_pToolsGrid);
    mainLayout->addStretch();

    reflowCards(width());
    scrollArea->setWidget(dashboardCanvas);
    pageLayout->addWidget(scrollArea);
}

void FunctionSelectWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    reflowCards(event->size().width());
}

void FunctionSelectWidget::reflowCards(int pageWidth)
{
    const int horizontalMargin = pageWidth < 520 ? 14
                               : pageWidth < 760 ? 22
                                                 : 32;
    const int cardAreaWidth = qMax(0, pageWidth - horizontalMargin * 2 - 14);
    const bool stackHero = cardAreaWidth < 440;

    if (m_pDashboardLayout)
        m_pDashboardLayout->setContentsMargins(horizontalMargin,
                                               pageWidth < 520 ? 18 : 28,
                                               horizontalMargin,
                                               pageWidth < 520 ? 22 : 34);
    if (m_pHeroLayout)
    {
        m_pHeroLayout->setDirection(stackHero ? QBoxLayout::TopToBottom
                                              : QBoxLayout::LeftToRight);
        m_pHeroLayout->setContentsMargins(pageWidth < 520 ? 16 : 28,
                                          pageWidth < 520 ? 16 : 22,
                                          pageWidth < 520 ? 16 : 22,
                                          pageWidth < 520 ? 16 : 22);
        m_pHeroLayout->setSpacing(stackHero ? 12 : 24);
    }
    if (m_pMetricCard)
    {
        if (stackHero)
        {
            m_pMetricCard->setMinimumWidth(0);
            m_pMetricCard->setMaximumWidth(QWIDGETSIZE_MAX);
            m_pMetricCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        }
        else
        {
            m_pMetricCard->setFixedWidth(184);
        }
    }

    const int primaryColumns = cardAreaWidth >= 680 ? 3
                             : cardAreaWidth >= 430 ? 2
                                                    : 1;
    const int toolColumns = cardAreaWidth >= 760 ? 4
                          : cardAreaWidth >= 540 ? 3
                          : cardAreaWidth >= 350 ? 2
                                                 : 1;

    if (primaryColumns != m_primaryColumnCount)
    {
        reflowGrid(m_pPrimaryGrid, m_primaryButtons, primaryColumns);
        m_primaryColumnCount = primaryColumns;
    }
    if (toolColumns != m_toolColumnCount)
    {
        reflowGrid(m_pToolsGrid, m_toolButtons, toolColumns);
        m_toolColumnCount = toolColumns;
    }
}

void FunctionSelectWidget::reflowGrid(QGridLayout *grid,
                                      const QVector<QPushButton*> &buttons,
                                      int columns)
{
    if (!grid || columns < 1)
        return;

    while (QLayoutItem *item = grid->takeAt(0))
        delete item;

    for (int index = 0; index < buttons.size(); ++index)
        grid->addWidget(buttons.at(index), index / columns, index % columns);

    for (int column = 0; column < 4; ++column)
        grid->setColumnStretch(column, column < columns ? 1 : 0);
    grid->invalidate();
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
    btn->setMinimumSize(featured ? QSize(0, 170) : QSize(0, 156));
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

void FunctionSelectWidget::on_btnDeleteAccount_clicked()
{
    emit functionSelected(1);
}

void FunctionSelectWidget::on_btnBackupAccount_clicked()
{
    emit functionSelected(2);
}

void FunctionSelectWidget::on_btnRepairAccount_clicked()
{
    emit functionSelected(3);
}

void FunctionSelectWidget::on_btnExtractYearData_clicked()
{
    emit functionSelected(4);
}

void FunctionSelectWidget::on_btnSubjectCodeConflict_clicked()
{
    emit functionSelected(5);
}

void FunctionSelectWidget::on_btnSiblingNameDuplicate_clicked()
{
    emit functionSelected(6);
}

void FunctionSelectWidget::on_btnCopyNingMengCookie_clicked()
{
    emit functionSelected(7);
}

void FunctionSelectWidget::on_btnJsonQuery_clicked()
{
    emit functionSelected(8);
}

void FunctionSelectWidget::on_btnSubjectCodeSeparator_clicked()
{
    emit functionSelected(9);
}

void FunctionSelectWidget::on_btnImportPackageUpload_clicked()
{
    emit functionSelected(10);
}

void FunctionSelectWidget::on_btnAllowNullAaType_clicked()
{
    emit functionSelected(11);
}

void FunctionSelectWidget::on_btnVoucherDateRepair_clicked()
{
    emit functionSelected(12);
}

void FunctionSelectWidget::on_btnEmptySubjectName_clicked()
{
    emit functionSelected(13);
}

void FunctionSelectWidget::on_btnReferenceAccountCodeRepair_clicked()
{
    emit functionSelected(14);
}

void FunctionSelectWidget::on_btnNonLeafVoucherRepair_clicked()
{
    emit functionSelected(15);
}

void FunctionSelectWidget::on_btnTrimSubjectName_clicked()
{
    emit functionSelected(16);
}

void FunctionSelectWidget::on_btnSubjectMerge_clicked()
{
    emit functionSelected(17);
}

void FunctionSelectWidget::on_btnAccess2003Converter_clicked()
{
    emit functionSelected(18);
}
