#include "BackupRestoreWidget.h"
#include "vouchergroupnumwidget.h"
#include "QtWidgetsApplication1.h"
#include "functionselectwidget.h"
#include "deleteaccountsetwidget.h"
#include "backupaccountsetwidget.h"
#include "repairaccountsetwidget.h"
#include "jsonquerywidget.h"
#include "wecomdocumentwidget.h"
#include "subjectcodeconflictwidget.h"
#include "subjectcodeseparatorwidget.h"
#include "importpackageuploadwidget.h"
#include "subjectbalanceimportwidget.h"
#include "allownullaatypewidget.h"
#include "emptysubjectnamewidget.h"
#include "referenceaccountcoderepairwidget.h"
#include "nonleafvoucherrepairwidget.h"
#include "subjectmergewidget.h"
#include "access2003converterwidget.h"
#include "DatabaseCleanupWidget.h"
#include "lemoncofighelper.h"
#include "Common/globaldata.h"
#include "Common/logger.h"
#include "Common/BrowserLoginBridge.h"
#include <QFile>
#include <QStackedWidget>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QPixmap>
#include <QUrl>
#include <QDesktopServices>
#include <QClipboard>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStyle>
#include <QWindow>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QTableView>
#include <QScrollArea>
#include <QTimer>
#include <QSystemTrayIcon>
#include <QCloseEvent>
#include <QMenu>
#include <QPainter>
#include "httprequesthelper.h"

QtWidgetsApplication1::QtWidgetsApplication1(GlobalData *globalData, QWidget *parent)
    : QMainWindow(parent),
      m_pGlobalData(globalData),
      m_pFunctionSelectWidget(nullptr),
      m_pDeleteAccountSetWidget(nullptr),
      m_pBackupAccountSetWidget(nullptr),
      m_pRepairAccountSetWidget(nullptr),
      m_pJsonQueryWidget(nullptr),
      m_pSubjectCodeConflictWidget(nullptr),
      m_pSubjectCodeSeparatorWidget(nullptr),
      m_pImportPackageUploadWidget(nullptr),
      m_pAllowNullAaTypeWidget(nullptr),
      m_pEmptySubjectNameWidget(nullptr),
      m_pTrimSubjectNameWidget(nullptr),
      m_pReferenceAccountCodeRepairWidget(nullptr),
      m_pNonLeafVoucherRepairWidget(nullptr),
      m_pSubjectMergeWidget(nullptr),
      m_pAccess2003ConverterWidget(nullptr),
      m_pBtnOpenWebsite(nullptr),
      m_pBtnHome(nullptr),
      m_pHeaderTitle(nullptr),
      m_pHeaderSubtitle(nullptr),
      m_pSideBarWidget(nullptr),
      m_pBrandWidget(nullptr),
      m_pBrandTitle(nullptr),
      m_pBrandSubtitle(nullptr),
      m_pNavCaption(nullptr),
      m_pSessionCard(nullptr),
      m_pSideVersion(nullptr),
      m_pSideBarLayout(nullptr),
      m_pBrandLayout(nullptr),
      m_pTrackedScreen(nullptr)
{
    ui.setupUi(this);
    ui.menuBar->hide();
    ui.mainToolBar->hide();
    ui.statusBar->hide();

    setWindowTitle(QStringLiteral("\u67e0\u6aac\u4e91\u5de5\u4f5c\u52a9\u624b"));
    const bool production = lemoncofighelper::CurrentHttpEnvironment() == "Pro";
    m_environmentName = production ? QStringLiteral("正式环境") : QStringLiteral("测试环境");
    setWindowTitle(windowTitle() + " · " + m_environmentName);
    setProperty("environment", production ? "pro" : "test");
    setupTray();

    QSize minimumWindowSize(800, 480);
    QSize preferredWindowSize(1240, 780);
    if (QScreen *screen = QGuiApplication::primaryScreen())
    {
        const QRect available = screen->availableGeometry();
        const int safeWidth = qMax(1, available.width() - 24);
        const int safeHeight = qMax(1, available.height() - 24);
        minimumWindowSize.setWidth(qMin(minimumWindowSize.width(), safeWidth));
        minimumWindowSize.setHeight(qMin(minimumWindowSize.height(), safeHeight));
        preferredWindowSize.setWidth(qMin(preferredWindowSize.width(),
                                          qMax(minimumWindowSize.width(), available.width() - 64)));
        preferredWindowSize.setHeight(qMin(preferredWindowSize.height(),
                                           qMax(minimumWindowSize.height(), available.height() - 64)));
    }
    setMinimumSize(minimumWindowSize);
    resize(preferredWindowSize);

    // The workspace theme is local to this window; login keeps its own stylesheet.
    QFile file(":/Resource/qss/workspace.css");
    if (file.open(QFile::ReadOnly))
    {
        QString theme = QString::fromUtf8(file.readAll());
        if (!production) {
            // Replace the shared surface palette so every lazily-created tool inherits its environment.
            theme.replace("#0d1424", "#201810").replace("#131c33", "#382417")
                .replace("#0b1120", "#21160f").replace("#10192b", "#352419")
                .replace("#172238", "#3b2b1e").replace("#1b2940", "#392b21")
                .replace("#151f33", "#2b211a").replace("#1e3554", "#563c25")
                .replace("#222b50", "#423022").replace("#342852", "#493125")
                .replace("#1e293b", "#3b2b20");
        }
        setStyleSheet(theme);
    }

    // 主框架：固定导航侧栏 + 页面内容区。
    QWidget *centralWidget = new QWidget(this);
    centralWidget->setObjectName("mainShell");
    QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_pSideBarWidget = new QWidget(centralWidget);
    QWidget *sideBarWidget = m_pSideBarWidget;
    sideBarWidget->setObjectName("sideBar");
    sideBarWidget->setFixedWidth(226);
    m_pSideBarLayout = new QVBoxLayout(sideBarWidget);
    QVBoxLayout *sideBarLayout = m_pSideBarLayout;
    sideBarLayout->setContentsMargins(20, 24, 20, 20);
    sideBarLayout->setSpacing(10);

    m_pBrandWidget = new QWidget(sideBarWidget);
    QWidget *brandWidget = m_pBrandWidget;
    brandWidget->setObjectName("brandBlock");
    m_pBrandLayout = new QHBoxLayout(brandWidget);
    QHBoxLayout *brandLayout = m_pBrandLayout;
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->setSpacing(11);

    QLabel *brandIcon = new QLabel(brandWidget);
    brandIcon->setObjectName("brandIcon");
    brandIcon->setFixedSize(42, 42);
    brandIcon->setPixmap(QPixmap(":/Resource/Images/logo-reverse.png")
                             .scaled(42, 42, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    brandIcon->setAlignment(Qt::AlignCenter);
    brandLayout->addWidget(brandIcon);

    QVBoxLayout *brandTextLayout = new QVBoxLayout();
    brandTextLayout->setContentsMargins(0, 1, 0, 0);
    brandTextLayout->setSpacing(1);
    m_pBrandTitle = new QLabel(QStringLiteral("\u67e0\u6aac\u4e91\u52a9\u624b"), brandWidget);
    QLabel *brandTitle = m_pBrandTitle;
    brandTitle->setObjectName("brandTitle");
    m_pBrandSubtitle = new QLabel(QStringLiteral("LEMON WORKSPACE"), brandWidget);
    QLabel *brandSubtitle = m_pBrandSubtitle;
    brandSubtitle->setObjectName("brandSubtitle");
    brandTextLayout->addWidget(brandTitle);
    brandTextLayout->addWidget(brandSubtitle);
    brandLayout->addLayout(brandTextLayout, 1);
    sideBarLayout->addWidget(brandWidget);
    sideBarLayout->addSpacing(28);

    m_pNavCaption = new QLabel(QStringLiteral("\u5bfc\u822a"), sideBarWidget);
    QLabel *navCaption = m_pNavCaption;
    navCaption->setObjectName("sideSectionCaption");
    sideBarLayout->addWidget(navCaption);

    m_pBtnHome = new QPushButton(QStringLiteral("\u5de5\u4f5c\u53f0"), sideBarWidget);
    m_pBtnHome->setObjectName("btnSidebarHome");
    m_pBtnHome->setCheckable(true);
    m_pBtnHome->setChecked(true);
    m_pBtnHome->setCursor(Qt::PointingHandCursor);
    sideBarLayout->addWidget(m_pBtnHome);
    const QStringList symbols = {QStringLiteral("云"), QStringLiteral("科"), QStringLiteral("凭"),
        QStringLiteral("包"), QStringLiteral("查")};
    for (int category = 1; category <= 5; ++category) {
        auto *button = new QPushButton(sideBarWidget);
        button->setObjectName(QString("navCategory%1").arg(category));
        button->setProperty("sideNav", true);
        button->setProperty("navSymbol", symbols.at(category - 1));
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(FunctionSelectWidget::categoryTitle(category));
        button->setAccessibleName(button->toolTip());
        m_categoryButtons.append(button);
        sideBarLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, category]() { showCategory(category); });
    }
    sideBarLayout->addStretch();

    m_pSessionCard = new QFrame(sideBarWidget);
    QFrame *sessionCard = m_pSessionCard;
    sessionCard->setObjectName("sideSessionCard");
    QVBoxLayout *sessionLayout = new QVBoxLayout(sessionCard);
    sessionLayout->setContentsMargins(14, 13, 14, 13);
    sessionLayout->setSpacing(6);
    QLabel *sessionCaption = new QLabel(QStringLiteral("\u5f53\u524d\u4f1a\u8bdd"), sessionCard);
    sessionCaption->setObjectName("sessionCaption");
    sessionLayout->addWidget(sessionCaption);

    QWidget *sessionState = new QWidget(sessionCard);
    QHBoxLayout *sessionStateLayout = new QHBoxLayout(sessionState);
    sessionStateLayout->setContentsMargins(0, 0, 0, 0);
    sessionStateLayout->setSpacing(7);
    QLabel *statusDot = new QLabel(sessionState);
    statusDot->setObjectName("sessionStatusDot");
    statusDot->setFixedSize(8, 8);
    QLabel *statusText = new QLabel(QStringLiteral("\u5df2\u5b89\u5168\u8fde\u63a5"), sessionState);
    statusText->setObjectName("sessionStatusText");
    sessionStateLayout->addWidget(statusDot);
    sessionStateLayout->addWidget(statusText);
    sessionStateLayout->addStretch();
    sessionLayout->addWidget(sessionState);

    QString displayName = m_pGlobalData->m_strUserName.trimmed();
    if (displayName.isEmpty())
        displayName = m_pGlobalData->m_strAuthName.trimmed();
    if (displayName.isEmpty())
        displayName = QStringLiteral("\u67e0\u6aac\u4e91\u8d26\u53f7");
    QLabel *accountLabel = new QLabel(displayName, sessionCard);
    accountLabel->setObjectName("sessionAccount");
    accountLabel->setToolTip(displayName);
    sessionLayout->addWidget(accountLabel);
    auto *refreshButton = new QPushButton(QStringLiteral("立即刷新授权"), sessionCard);
    refreshButton->setObjectName("btnRefreshSession");
    connect(refreshButton, &QPushButton::clicked, this, &QtWidgetsApplication1::refreshSessionRequested);
    sessionLayout->addWidget(refreshButton);
    sideBarLayout->addWidget(sessionCard);

    m_pSideVersion = new QLabel(QStringLiteral("DESKTOP TOOL  ·  2026"), sideBarWidget);
    QLabel *versionLabel = m_pSideVersion;
    versionLabel->setObjectName("sideVersion");
    sideBarLayout->addWidget(versionLabel);

    mainLayout->addWidget(sideBarWidget);

    QWidget *contentWidget = new QWidget(centralWidget);
    contentWidget->setObjectName("contentShell");
    QVBoxLayout *contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    QWidget *topBarWidget = new QWidget(contentWidget);
    topBarWidget->setObjectName("topBar");
    QHBoxLayout *topBar = new QHBoxLayout(topBarWidget);
    topBar->setContentsMargins(30, 12, 28, 12);
    topBar->setSpacing(16);

    QVBoxLayout *pageHeaderLayout = new QVBoxLayout();
    pageHeaderLayout->setContentsMargins(0, 0, 0, 0);
    pageHeaderLayout->setSpacing(2);
    m_pHeaderTitle = new QLabel(topBarWidget);
    m_pHeaderTitle->setObjectName("headerTitle");
    m_pHeaderTitle->setWordWrap(true);
    m_pHeaderSubtitle = new QLabel(topBarWidget);
    m_pHeaderSubtitle->setObjectName("headerSubtitle");
    m_pHeaderSubtitle->setWordWrap(true);
    pageHeaderLayout->addWidget(m_pHeaderTitle);
    pageHeaderLayout->addWidget(m_pHeaderSubtitle);
    topBar->addLayout(pageHeaderLayout, 1);

    m_pBtnOpenWebsite = new QPushButton(QStringLiteral("\u6253\u5f00\u67e0\u6aac\u4e91\u5b98\u7f51  \u2197"), topBarWidget);
    m_pBtnOpenWebsite->setObjectName("btnOpenWebsite");
    m_pBtnOpenWebsite->setCursor(Qt::PointingHandCursor);
    auto *environmentButton = new QPushButton(m_environmentName, topBarWidget);
    environmentButton->setObjectName("btnSwitchEnvironment");
    environmentButton->setProperty("environment", production ? "pro" : "test");
    environmentButton->setToolTip(QStringLiteral("使用已保存的账号密码切换环境"));
    auto *environmentMenu = new QMenu(environmentButton);
    auto *switchAction = environmentMenu->addAction(production ? QStringLiteral("切换到测试环境") : QStringLiteral("切换到正式环境"),
        this, &QtWidgetsApplication1::requestEnvironmentSwitch);
    switchAction->setObjectName("actionSwitchEnvironment");
    environmentMenu->addAction(QStringLiteral("退出工具"), this, &QtWidgetsApplication1::requestExit);
    environmentButton->setMenu(environmentMenu);
    topBar->addWidget(environmentButton);
    topBar->addWidget(m_pBtnOpenWebsite);

    contentLayout->addWidget(topBarWidget);

    connect(m_pBtnOpenWebsite, &QPushButton::clicked, this, &QtWidgetsApplication1::onOpenWebsite);
    connect(m_pBtnHome, &QPushButton::clicked, this, &QtWidgetsApplication1::showFunctionSelect);

    // 使用 QStackedWidget 管理页面导航
    m_pStackedWidget = new QStackedWidget(contentWidget);
    m_pStackedWidget->setObjectName("mainStack");
    contentLayout->addWidget(m_pStackedWidget, 1);

    mainLayout->addWidget(contentWidget, 1);

    setCentralWidget(centralWidget);

    // 功能选择页 (index 0)
    m_pFunctionSelectWidget = new FunctionSelectWidget(m_pGlobalData, this);
    m_pStackedWidget->addWidget(m_pFunctionSelectWidget);
    connect(m_pFunctionSelectWidget, &FunctionSelectWidget::functionSelected,
            this, &QtWidgetsApplication1::onFunctionSelected);

    connect(m_pFunctionSelectWidget, &FunctionSelectWidget::categoryChanged, this, [this](int category) {
        m_returnCategory = category;
        updatePageHeader(FunctionSelectWidget::categoryTitle(category),
            FunctionSelectWidget::categoryDescription(category), category == 0);
        updateNavigation(category);
    });
    connect(m_pStackedWidget, &QStackedWidget::currentChanged, this, [this](int) {
        prepareToolPage(m_pStackedWidget->currentWidget());
    });
    updateResponsiveChrome();
    showFunctionSelect();
    m_wecomLocalServer = new WecomLocalServer([this](const QDate &date, WecomLocalServer::Reply reply) {
        if (!isEnabled() || QApplication::activeModalWidget()) {
            reply(WecomLocalServer::error("busy", QStringLiteral("工具正在切换环境或等待对话框操作，请稍后重试。"))); return;
        }
        ensureWecomDocument();
        m_pWecomDocumentWidget->readForAssistant(date, std::move(reply));
    }, this);
    m_wecomLocalServer->start();
}

QtWidgetsApplication1::~QtWidgetsApplication1()
{
    delete m_wecomLocalServer;
}

void QtWidgetsApplication1::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateResponsiveChrome();
}

void QtWidgetsApplication1::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (QWindow *handle = windowHandle())
    {
        connect(handle, &QWindow::screenChanged,
                this, &QtWidgetsApplication1::onScreenChanged,
                Qt::UniqueConnection);
        trackScreen(handle->screen());
    }
}

void QtWidgetsApplication1::trackScreen(QScreen *screen)
{
    if (!screen)
        return;

    if (m_pTrackedScreen != screen)
    {
        if (m_pTrackedScreen)
        {
            disconnect(m_pTrackedScreen.data(), &QScreen::availableGeometryChanged,
                       this, &QtWidgetsApplication1::onAvailableGeometryChanged);
        }
        m_pTrackedScreen = screen;
        connect(screen, &QScreen::availableGeometryChanged,
                this, &QtWidgetsApplication1::onAvailableGeometryChanged,
                Qt::UniqueConnection);
    }
    applyScreenConstraints(screen);
}

void QtWidgetsApplication1::applyScreenConstraints(QScreen *screen)
{
    if (!screen)
        return;

    const QRect available = screen->availableGeometry();
    const int frameWidth = isVisible()
        ? qMax(0, frameGeometry().width() - geometry().width()) : 0;
    const int frameHeight = isVisible()
        ? qMax(0, frameGeometry().height() - geometry().height()) : 0;
    const int safeWidth = qMax(1, available.width() - 24 - frameWidth);
    const int safeHeight = qMax(1, available.height() - 24 - frameHeight);
    const QSize minimumWindowSize(qMin(800, safeWidth), qMin(480, safeHeight));
    setMinimumSize(minimumWindowSize);

    if (!isMaximized() && !isFullScreen())
    {
        const QSize fittedSize(qMin(width(), safeWidth), qMin(height(), safeHeight));
        if (fittedSize != size())
            resize(fittedSize);
    }
}

void QtWidgetsApplication1::onAvailableGeometryChanged(const QRect &geometry)
{
    Q_UNUSED(geometry);
    applyScreenConstraints(m_pTrackedScreen.data());
}

void QtWidgetsApplication1::onScreenChanged(QScreen *screen)
{
    trackScreen(screen);
}

void QtWidgetsApplication1::updateResponsiveChrome()
{
    if (!m_pSideBarWidget || !m_pSideBarLayout || !m_pBrandLayout || !m_pBtnHome)
        return;

    const bool compactSidebar = width() < 1040;
    const bool shortWindow = height() < 680;
    const int sideBarWidth = compactSidebar ? 74 : 226;
    const int contentWidth = qMax(0, width() - sideBarWidth);
    for (int index = 0; index < m_categoryButtons.size(); ++index) {
        auto *button = m_categoryButtons.at(index);
        button->setText(compactSidebar ? button->property("navSymbol").toString()
            : QStringLiteral("%1   %2").arg(button->property("navSymbol").toString(),
                FunctionSelectWidget::categoryTitle(index + 1)));
        if (!button->property("compact").isValid() || button->property("compact").toBool() != compactSidebar) {
            button->setProperty("compact", compactSidebar);
            button->style()->unpolish(button);
            button->style()->polish(button);
        }
    }
    const bool styleChanged = m_pBtnHome->property("compact").toBool() != compactSidebar;

    if (m_pSideBarWidget->width() != sideBarWidth)
        m_pSideBarWidget->setFixedWidth(sideBarWidth);
    m_pSideBarLayout->setContentsMargins(compactSidebar ? 10 : 20,
                                         compactSidebar ? 18 : 24,
                                         compactSidebar ? 10 : 20,
                                         compactSidebar ? 14 : 20);
    m_pBrandLayout->setContentsMargins(compactSidebar ? 6 : 0, 0,
                                       compactSidebar ? 6 : 0, 0);
    m_pBrandLayout->setSpacing(compactSidebar ? 0 : 11);

    if (m_pBrandTitle)
        m_pBrandTitle->setVisible(!compactSidebar);
    if (m_pBrandSubtitle)
        m_pBrandSubtitle->setVisible(!compactSidebar);
    if (m_pNavCaption)
        m_pNavCaption->setVisible(!compactSidebar);
    if (m_pSessionCard)
        m_pSessionCard->setVisible(!compactSidebar && !shortWindow);
    if (m_pSideVersion)
        m_pSideVersion->setVisible(!compactSidebar && !shortWindow);

    m_pBtnHome->setText(compactSidebar ? QStringLiteral("\u9996\u9875")
                                       : QStringLiteral("\u5de5\u4f5c\u53f0"));
    m_pBtnHome->setToolTip(QStringLiteral("\u8fd4\u56de\u5de5\u4f5c\u53f0"));
    m_pBtnHome->setAccessibleName(QStringLiteral("\u5de5\u4f5c\u53f0"));
    m_pBtnHome->setProperty("compact", compactSidebar);
    m_pSideBarWidget->setProperty("compact", compactSidebar);

    if (m_pHeaderSubtitle)
        m_pHeaderSubtitle->setVisible(contentWidth >= 520 && !shortWindow);
    if (m_pBtnOpenWebsite)
        m_pBtnOpenWebsite->setText(contentWidth < 560
            ? QStringLiteral("\u5b98\u7f51  \u2197")
            : QStringLiteral("\u6253\u5f00\u67e0\u6aac\u4e91\u5b98\u7f51  \u2197"));

    if (styleChanged)
    {
        m_pBtnHome->style()->unpolish(m_pBtnHome);
        m_pBtnHome->style()->polish(m_pBtnHome);
        m_pBtnHome->update();
    }
}

void QtWidgetsApplication1::showFunctionSelect()
{
    showCategory(0);
}

void QtWidgetsApplication1::showCategory(int category)
{
    m_pFunctionSelectWidget->showCategory(category);
    m_pStackedWidget->setCurrentWidget(m_pFunctionSelectWidget);
}

void QtWidgetsApplication1::updateNavigation(int category)
{
    m_pBtnHome->setChecked(category == 0);
    for (int index = 0; index < m_categoryButtons.size(); ++index)
        m_categoryButtons.at(index)->setChecked(category == index + 1);
}

void QtWidgetsApplication1::prepareToolPage(QWidget *page)
{
    if (!page || page == m_pFunctionSelectWidget)
        return;
    if (!page->property("workspacePrepared").toBool()) {
        page->setProperty("workspacePrepared", true);
        for (auto *table : page->findChildren<QTableView*>()) {
            table->setAlternatingRowColors(true);
            table->setShowGrid(false);
            table->verticalHeader()->setDefaultSectionSize(38);
        }
        for (auto *button : page->findChildren<QPushButton*>()) {
            button->setCursor(Qt::PointingHandCursor);
            if (button->objectName() == "btnBackToMain" || button->objectName() == "btnResultBack") {
                button->setMaximumWidth(140);
                button->setText(QStringLiteral("← 返回分类"));
            }
        }
        for (auto *label : page->findChildren<QLabel*>())
            if (label->objectName() == "labelRepairDropArea" || label->objectName() == "labelPageTitle")
                label->setWordWrap(true);
    }
}

void QtWidgetsApplication1::addToolPage(QWidget *page)
{
    auto *scroll = new QScrollArea(m_pStackedWidget);
    scroll->setObjectName("toolPageScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    if (page->layout())
        page->layout()->setSizeConstraint(QLayout::SetMinimumSize);
    scroll->setWidget(page);
    m_toolContainers.insert(page, scroll);
    m_pStackedWidget->addWidget(scroll);
}

void QtWidgetsApplication1::showToolPage(QWidget *page)
{
    m_pStackedWidget->setCurrentWidget(m_toolContainers.value(page));
}

void QtWidgetsApplication1::updatePageHeader(const QString &title, const QString &subtitle, bool isHome)
{
    if (m_pHeaderTitle)
        m_pHeaderTitle->setText(title);
    if (m_pHeaderSubtitle)
        m_pHeaderSubtitle->setText(subtitle);
    if (m_pBtnHome)
        m_pBtnHome->setChecked(isHome);
}

void QtWidgetsApplication1::showDeleteAccountSet()
{
    AppLogger::operation(QStringLiteral("显示删除账套页"));
    updatePageHeader(QStringLiteral("\u5220\u9664\u8d26\u5957"),
                     QStringLiteral("\u9009\u62e9\u9700\u8981\u79fb\u9664\u7684\u4e91\u7aef\u8d26\u5957\uff0c\u8bf7\u8c28\u614e\u64cd\u4f5c"));
    if (!m_pDeleteAccountSetWidget)
    {
        m_pDeleteAccountSetWidget = new DeleteAccountSetWidget(m_pGlobalData, this);
        addToolPage(m_pDeleteAccountSetWidget);
        connect(m_pDeleteAccountSetWidget, &DeleteAccountSetWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    showToolPage(m_pDeleteAccountSetWidget);
    m_pDeleteAccountSetWidget->loadAccountSetList();
}

void QtWidgetsApplication1::showBackupAccountSet()
{
    AppLogger::operation(QStringLiteral("显示备份下载账套页"));
    updatePageHeader(QStringLiteral("\u5907\u4efd\u4e0b\u8f7d"),
                     QStringLiteral("\u5c06\u4e91\u7aef\u8d26\u5957\u5b89\u5168\u5f52\u6863\u5230\u672c\u5730"));
    if (!m_pBackupAccountSetWidget)
    {
        m_pBackupAccountSetWidget = new BackupAccountSetWidget(m_pGlobalData, this);
        addToolPage(m_pBackupAccountSetWidget);
        connect(m_pBackupAccountSetWidget, &BackupAccountSetWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    showToolPage(m_pBackupAccountSetWidget);
    m_pBackupAccountSetWidget->loadAccountSetList();
}

void QtWidgetsApplication1::showRepairAccountSet()
{
    AppLogger::operation(QStringLiteral("显示修复账套页"));
    updatePageHeader(QStringLiteral("\u4fee\u590d\u8d26\u5957"),
                     QStringLiteral("\u6309\u987a\u5e8f\u5904\u7406\u79d1\u76ee\u7f16\u7801\u51b2\u7a81\u3001\u540c\u7ea7\u79d1\u76ee\u91cd\u540d\u548c\u8f85\u52a9\u6838\u7b97\u975e\u5fc5\u5f55"));
    if (!m_pRepairAccountSetWidget)
    {
        m_pRepairAccountSetWidget = new RepairAccountSetWidget(m_pGlobalData, this);
        addToolPage(m_pRepairAccountSetWidget);
        connect(m_pRepairAccountSetWidget, &RepairAccountSetWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    m_pRepairAccountSetWidget->setDateRepairOnly(false);
    showToolPage(m_pRepairAccountSetWidget);
    m_pRepairAccountSetWidget->loadAccountSetList();
}

void QtWidgetsApplication1::showVoucherDateRepair()
{
    AppLogger::operation(QStringLiteral("\u663e\u793a\u51ed\u8bc1\u65e5\u671f\u5f02\u5e38\u4fee\u590d\u9875"));
    updatePageHeader(QStringLiteral("\u51ed\u8bc1\u65e5\u671f\u5f02\u5e38\u4fee\u590d"),
                     QStringLiteral("\u5c06\u8d8a\u754c\u51ed\u8bc1\u65e5\u671f\u8c03\u6574\u4e3a\u5f53\u6708\u6700\u540e\u4e00\u5929"));
    if (!m_pRepairAccountSetWidget)
    {
        m_pRepairAccountSetWidget = new RepairAccountSetWidget(m_pGlobalData, this);
        addToolPage(m_pRepairAccountSetWidget);
        connect(m_pRepairAccountSetWidget, &RepairAccountSetWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    m_pRepairAccountSetWidget->setDateRepairOnly(true);
    showToolPage(m_pRepairAccountSetWidget);
    m_pRepairAccountSetWidget->loadAccountSetList();
}

void QtWidgetsApplication1::showSiblingNameRepair()
{
    AppLogger::operation(QStringLiteral("显示同级科目重名处理页"));
    updatePageHeader(QStringLiteral("同级科目重名处理"),
                     QStringLiteral("优先保留一个使用过的科目原名，其余重名科目按编码生成唯一名称"));
    if (!m_pRepairAccountSetWidget)
    {
        m_pRepairAccountSetWidget = new RepairAccountSetWidget(m_pGlobalData, this);
        addToolPage(m_pRepairAccountSetWidget);
        connect(m_pRepairAccountSetWidget, &RepairAccountSetWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    m_pRepairAccountSetWidget->setSiblingNameRepairOnly(true);
    showToolPage(m_pRepairAccountSetWidget);
    m_pRepairAccountSetWidget->loadAccountSetList();
}

void QtWidgetsApplication1::showEmptySubjectNameRepair()
{
    AppLogger::operation(QStringLiteral("\u663e\u793a\u7a7a\u79d1\u76ee\u540d\u79f0\u4fee\u590d\u9875"));
    updatePageHeader(QStringLiteral("\u7a7a\u79d1\u76ee\u540d\u79f0\u4fee\u590d"),
                     QStringLiteral("\u79d1\u76ee\u540d\u79f0\u4e3a\u7a7a\u65f6\uff0c\u4f7f\u7528\u79d1\u76ee\u7f16\u7801\u81ea\u52a8\u586b\u5145"));
    if (!m_pEmptySubjectNameWidget)
    {
        m_pEmptySubjectNameWidget = new EmptySubjectNameWidget(m_pGlobalData, this);
        addToolPage(m_pEmptySubjectNameWidget);
        connect(m_pEmptySubjectNameWidget, &EmptySubjectNameWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    showToolPage(m_pEmptySubjectNameWidget);
    m_pEmptySubjectNameWidget->refreshInputFiles();
}

void QtWidgetsApplication1::showJsonQuery()
{
    AppLogger::operation(QStringLiteral("显示 JSON 查询页"));
    updatePageHeader(QStringLiteral("JSON \u6570\u636e\u67e5\u8be2"),
                     QStringLiteral("\u5feb\u901f\u68c0\u7d22\u548c\u67e5\u770b\u672c\u5730\u8d26\u5957\u6570\u636e"));
    if (!m_pJsonQueryWidget)
    {
        m_pJsonQueryWidget = new JsonQueryWidget(m_pGlobalData, this);
        addToolPage(m_pJsonQueryWidget);
        connect(m_pJsonQueryWidget, &JsonQueryWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    showToolPage(m_pJsonQueryWidget);
    m_pJsonQueryWidget->loadFileList();
}

void QtWidgetsApplication1::showWecomDocument()
{
    updatePageHeader(QStringLiteral("企业微信文档读取"), QStringLiteral("扫码登录，获取今天及之后的工作表并导出 JSON"));
    ensureWecomDocument();
    showToolPage(m_pWecomDocumentWidget);
}
void QtWidgetsApplication1::ensureWecomDocument()
{
    if (!m_pWecomDocumentWidget) {
        m_pWecomDocumentWidget = new WecomDocumentWidget(this);
        addToolPage(m_pWecomDocumentWidget);
        connect(m_pWecomDocumentWidget, &WecomDocumentWidget::backToMain, this, &QtWidgetsApplication1::onBackToMain);
    }
    if (m_wecomLocalServer) m_pWecomDocumentWidget->setAssistantStatus(m_wecomLocalServer->statusText());
}

void QtWidgetsApplication1::showSubjectCodeConflict()
{
    AppLogger::operation(QStringLiteral("\u663e\u793a\u79d1\u76ee\u7f16\u7801\u51b2\u7a81\u5904\u7406\u9875"));
    updatePageHeader(QStringLiteral("\u79d1\u76ee\u7f16\u7801\u51b2\u7a81\u5904\u7406"),
                     QStringLiteral("\u68c0\u67e5\u4f7f\u7528\u5e74\u5ea6\uff0c\u9009\u62e9\u4fdd\u7559\u79d1\u76ee\u5e76\u8f93\u51fa\u5904\u7406\u540e ZIP"));
    if (!m_pSubjectCodeConflictWidget)
    {
        m_pSubjectCodeConflictWidget = new SubjectCodeConflictWidget(m_pGlobalData, this);
        addToolPage(m_pSubjectCodeConflictWidget);
        connect(m_pSubjectCodeConflictWidget, &SubjectCodeConflictWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    showToolPage(m_pSubjectCodeConflictWidget);
    m_pSubjectCodeConflictWidget->loadInputDirectory();
}

void QtWidgetsApplication1::showSubjectCodeSeparator()
{
    AppLogger::operation(QStringLiteral("显示科目编码添加分隔符页"));
    updatePageHeader(QStringLiteral("\u79d1\u76ee\u7f16\u7801\u6dfb\u52a0\u5206\u9694\u7b26"),
                     QStringLiteral("\u6279\u91cf\u89c4\u8303\u79d1\u76ee\u7f16\u7801\u7684\u5c42\u7ea7\u683c\u5f0f"));
    if (!m_pSubjectCodeSeparatorWidget)
    {
        m_pSubjectCodeSeparatorWidget = new SubjectCodeSeparatorWidget(m_pGlobalData, this);
        addToolPage(m_pSubjectCodeSeparatorWidget);
        connect(m_pSubjectCodeSeparatorWidget, &SubjectCodeSeparatorWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    showToolPage(m_pSubjectCodeSeparatorWidget);
    m_pSubjectCodeSeparatorWidget->refreshInputFiles();
}

void QtWidgetsApplication1::showSubjectBalanceImport()
{
    updatePageHeader(QStringLiteral("科目余额表导账"), QStringLiteral("上传原始表格，新建企业专业版账套并跟踪结果"));
    if (!m_pSubjectBalanceImportWidget) {
        m_pSubjectBalanceImportWidget = new SubjectBalanceImportWidget(m_pGlobalData, this);
        addToolPage(m_pSubjectBalanceImportWidget);
        connect(m_pSubjectBalanceImportWidget, &SubjectBalanceImportWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    showToolPage(m_pSubjectBalanceImportWidget);
    m_pSubjectBalanceImportWidget->reloadHistory();
}

void QtWidgetsApplication1::showImportPackageUpload()
{
    AppLogger::operation(QStringLiteral("显示上传压缩包页面"));
    updatePageHeader(QStringLiteral("\u4e0a\u4f20\u4e13\u4e1a\u7248\u8d26\u5957"),
                     QStringLiteral("\u4e0a\u4f20\u6570\u636e\u5305\u5e76\u8ddf\u8e2a\u5bfc\u5165\u8fdb\u5ea6"));
    if (!m_pImportPackageUploadWidget)
    {
        m_pImportPackageUploadWidget = new ImportPackageUploadWidget(m_pGlobalData, this);
        addToolPage(m_pImportPackageUploadWidget);
        connect(m_pImportPackageUploadWidget, &ImportPackageUploadWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    showToolPage(m_pImportPackageUploadWidget);
    m_pImportPackageUploadWidget->reloadImportHistory();
}

void QtWidgetsApplication1::showAllowNullAaType()
{
    AppLogger::operation(QStringLiteral("\u663e\u793a\u51ed\u8bc1\u8f85\u52a9\u6838\u7b97\u8865\u975e\u5fc5\u5f55\u9875\u9762"));
    updatePageHeader(QStringLiteral("\u51ed\u8bc1\u8f85\u52a9\u6838\u7b97"),
                     QStringLiteral("\u8865\u9f50\u8f85\u52a9\u6838\u7b97\u7684\u975e\u5fc5\u5f55\u914d\u7f6e"));
    if (!m_pAllowNullAaTypeWidget)
    {
        m_pAllowNullAaTypeWidget = new AllowNullAaTypeWidget(m_pGlobalData, this);
        addToolPage(m_pAllowNullAaTypeWidget);
        connect(m_pAllowNullAaTypeWidget, &AllowNullAaTypeWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    showToolPage(m_pAllowNullAaTypeWidget);
    m_pAllowNullAaTypeWidget->refreshInputFiles();
}

void QtWidgetsApplication1::showReferenceAccountCodeRepair()
{
    AppLogger::operation(QStringLiteral("\u663e\u793a\u51ed\u8bc1\u4f59\u989d\u79d1\u76ee\u7f16\u7801\u4fee\u590d\u9875"));
    updatePageHeader(QStringLiteral("\u51ed\u8bc1\u4f59\u989d\u79d1\u76ee\u7f16\u7801\u4fee\u590d"),
                     QStringLiteral("\u4ece\u79d1\u76ee\u7f16\u7801\u63a8\u5bfc\u5c42\u7ea7\uff0c\u7edf\u4e00\u51ed\u8bc1\u548c\u4f59\u989d\u7684\u79d1\u76ee\u4f4d\u6570"));
    if (!m_pReferenceAccountCodeRepairWidget)
    {
        m_pReferenceAccountCodeRepairWidget = new ReferenceAccountCodeRepairWidget(m_pGlobalData, this);
        addToolPage(m_pReferenceAccountCodeRepairWidget);
        connect(m_pReferenceAccountCodeRepairWidget, &ReferenceAccountCodeRepairWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    showToolPage(m_pReferenceAccountCodeRepairWidget);
    m_pReferenceAccountCodeRepairWidget->refreshInputFiles();
}

void QtWidgetsApplication1::showNonLeafVoucherRepair()
{
    AppLogger::operation(QStringLiteral("\u663e\u793a\u51ed\u8bc1\u975e\u672b\u7ea7\u79d1\u76ee\u8f6c\u6362\u9875"));
    updatePageHeader(QStringLiteral("\u51ed\u8bc1\u975e\u672b\u7ea7\u79d1\u76ee\u8f6c\u6362"),
                     QStringLiteral("\u91cd\u65b0\u63a8\u5bfc\u79d1\u76ee\u5c42\u7ea7\uff0c\u7531\u7528\u6237\u786e\u8ba4\u975e\u672b\u7ea7\u51ed\u8bc1\u7684\u76ee\u6807\u672b\u7ea7\u79d1\u76ee"));
    if (!m_pNonLeafVoucherRepairWidget)
    {
        m_pNonLeafVoucherRepairWidget = new NonLeafVoucherRepairWidget(m_pGlobalData, this);
        addToolPage(m_pNonLeafVoucherRepairWidget);
        connect(m_pNonLeafVoucherRepairWidget, &NonLeafVoucherRepairWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    showToolPage(m_pNonLeafVoucherRepairWidget);
    m_pNonLeafVoucherRepairWidget->refreshInputFiles();
}

void QtWidgetsApplication1::showTrimSubjectName()
{
    AppLogger::operation(QStringLiteral("显示科目名称两侧空格清理页"));
    updatePageHeader(QStringLiteral("科目名称两侧空格清理"),
                     QStringLiteral("去除 AcctName 左右两侧的空白字符，保留名称中间内容"));
    if (!m_pTrimSubjectNameWidget)
    {
        m_pTrimSubjectNameWidget = new EmptySubjectNameWidget(
            m_pGlobalData, this, EmptySubjectNameWidget::TrimNameWhitespace);
        addToolPage(m_pTrimSubjectNameWidget);
        connect(m_pTrimSubjectNameWidget, &EmptySubjectNameWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    showToolPage(m_pTrimSubjectNameWidget);
    m_pTrimSubjectNameWidget->refreshInputFiles();
}

void QtWidgetsApplication1::showSubjectMerge()
{
    AppLogger::operation(QStringLiteral("显示会计科目合并页"));
    updatePageHeader(QStringLiteral("会计科目合并"),
                     QStringLiteral("选择同父级科目，递归合并子科目、期初余额和凭证科目"));
    if (!m_pSubjectMergeWidget)
    {
        m_pSubjectMergeWidget = new SubjectMergeWidget(m_pGlobalData, this);
        addToolPage(m_pSubjectMergeWidget);
        connect(m_pSubjectMergeWidget, &SubjectMergeWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    showToolPage(m_pSubjectMergeWidget);
    m_pSubjectMergeWidget->refreshInputFiles();
}

void QtWidgetsApplication1::showAccess2003Converter()
{
    AppLogger::operation(QStringLiteral("显示 Access 2003 批量转换页"));
    updatePageHeader(QStringLiteral("Access 2003 批量转换"),
                     QStringLiteral("调用本机 Access 2003，将 AIS、AIY、MDB 批量转换为 Access 2002-2003 格式"));
    if (!m_pAccess2003ConverterWidget)
    {
        m_pAccess2003ConverterWidget = new Access2003ConverterWidget(m_pGlobalData, this);
        addToolPage(m_pAccess2003ConverterWidget);
        connect(m_pAccess2003ConverterWidget, &Access2003ConverterWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    showToolPage(m_pAccess2003ConverterWidget);
}

void QtWidgetsApplication1::showDatabaseCleanup()
{
    updatePageHeader(QString::fromUtf8(u8"选择删除数据库"),
                     QString::fromUtf8(u8"勾选要删除的用户数据库，支持全选，保留系统数据库"));
    if (!m_pDatabaseCleanupWidget) {
        m_pDatabaseCleanupWidget = new DatabaseCleanupWidget(this);
        addToolPage(m_pDatabaseCleanupWidget);
        connect(m_pDatabaseCleanupWidget, &DatabaseCleanupWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    showToolPage(m_pDatabaseCleanupWidget);
}

void QtWidgetsApplication1::showBackupRestore(int softwareId)
{
    updatePageHeader(QStringLiteral("恢复备份包"),
        QStringLiteral("选择备份来源软件，恢复到扫描发现的本地实例或 211"));
    if (!m_pBackupRestoreWidget) {
        m_pBackupRestoreWidget = new BackupRestoreWidget(this);
        addToolPage(m_pBackupRestoreWidget);
        connect(m_pBackupRestoreWidget, &BackupRestoreWidget::backToMain,
            this, &QtWidgetsApplication1::onBackToMain);
    }
    if (softwareId) m_pBackupRestoreWidget->selectSoftware(softwareId);
    showToolPage(m_pBackupRestoreWidget);
}

void QtWidgetsApplication1::onBackToMain()
{
    showCategory(m_returnCategory);
}

void QtWidgetsApplication1::showVoucherGroupNumRepair()
{
    updatePageHeader(QStringLiteral("凭证编号修复"), QStringLiteral("将小数凭证编号转为整数，并展示编号调整明细"));
    if (!m_pVoucherGroupNumWidget) {
        m_pVoucherGroupNumWidget = new VoucherGroupNumWidget(m_pGlobalData, this);
        addToolPage(m_pVoucherGroupNumWidget);
        connect(m_pVoucherGroupNumWidget, &VoucherGroupNumWidget::backToMain,
            this, &QtWidgetsApplication1::onBackToMain);
    }
    showToolPage(m_pVoucherGroupNumWidget);
    m_pVoucherGroupNumWidget->refreshInputFiles();
}

void QtWidgetsApplication1::onFunctionSelected(int functionId)
{
    AppLogger::operation(QStringLiteral("选择功能 functionId=%1").arg(functionId));
    if (functionId != 4 && functionId != 7) {
        m_returnCategory = FunctionSelectWidget::categoryForFunction(functionId);
        updateNavigation(m_returnCategory);
    }
    switch (functionId)
    {
    case 1:
        showDeleteAccountSet();
        break;
    case 2:
        showBackupAccountSet();
        break;
    case 3:
        showRepairAccountSet();
        break;
    case 4:
        QMessageBox::information(this, QStringLiteral("\u622a\u53d6\u8d26\u5957\u7279\u5b9a\u5e74\u4efd\u7684\u6570\u636e"),
                                 QStringLiteral("\u622a\u53d6\u8d26\u5957\u7279\u5b9a\u5e74\u4efd\u7684\u6570\u636e\u529f\u80fd\u5f85\u5b9e\u73b0"));
        break;
    case 5:
        showSubjectCodeConflict();
        break;
    case 6:
        showSiblingNameRepair();
        break;
    case 7:
        onCopyNingMengCookie();
        break;
    case 8:
        showJsonQuery();
        break;
    case 28:
        showWecomDocument();
        break;
    case 9:
        showSubjectCodeSeparator();
        break;
    case 10:
        showImportPackageUpload();
        break;
    case 11:
        showAllowNullAaType();
        break;
    case 12:
        showVoucherDateRepair();
        break;
    case 13:
        showEmptySubjectNameRepair();
        break;
    case 14:
        showReferenceAccountCodeRepair();
        break;
    case 15:
        showNonLeafVoucherRepair();
        break;
    case 16:
        showTrimSubjectName();
        break;
    case 17:
        showSubjectMerge();
        break;
    case 18:
        showAccess2003Converter();
        break;
    case 19:
        showBackupRestore(19);
        break;
    case 20:
        showBackupRestore();
        break;
    case 24:
        showSubjectBalanceImport();
        break;
    case 23:
        showVoucherGroupNumRepair();
        break;
    case 27:
        showBackupRestore(27);
        break;
    case 26:
        showBackupRestore(26);
        break;
    case 25:
        showBackupRestore(25);
        break;
    case 22:
        showBackupRestore(22);
        break;
    case 21:
        showDatabaseCleanup();
        break;
    }
}

void QtWidgetsApplication1::onOpenWebsite()
{
    AppLogger::operation(QStringLiteral("打开柠檬云官网"));
    lemoncofighelper configHelper;
    const QString wbgateUrl = configHelper.GetAccountUrlByKey("wbgateMenuUrl");
    if (wbgateUrl.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("wbgateMenuUrl 未配置"));
        return;
    }

    if (!m_browserLoginBridge) {
        m_browserLoginBridge = new BrowserLoginBridge(this);
        m_browserLoginBridge->finished = [](bool success) {
            if (success) AppLogger::operation(QStringLiteral("浏览器登录同步完成"));
            else AppLogger::operation(QStringLiteral("浏览器登录同步未完成，请检查扩展后重新打开官网"));
        };
    }
    QString error;
    const QUrl handoffUrl = m_browserLoginBridge->start(m_pGlobalData->m_strNingMengCookie, QUrl(wbgateUrl), &error);
    if (handoffUrl.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("打开官网失败"), error);
        return;
    }
    if (!QDesktopServices::openUrl(handoffUrl))
    {
        m_browserLoginBridge->cancel();
        AppLogger::error(QStringLiteral("使用默认浏览器打开柠檬云官网失败"));
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("无法打开柠檬云官网，请检查系统默认浏览器设置"));
    }
    else
    {
        AppLogger::operation(QStringLiteral("已请求默认浏览器连接登录助手"));
    }
}

void QtWidgetsApplication1::onCopyNingMengCookie()
{
    AppLogger::operation(QStringLiteral("复制 ningmengcookie"));
    QString cookie = m_pGlobalData->m_strNingMengCookie;
    if (cookie.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("\u63d0\u793a"),
                             QStringLiteral("ningmengcookie \u4e3a\u7a7a"));
        return;
    }
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(cookie);
    QMessageBox::information(this, QStringLiteral("\u83b7\u53d6ningmengcookie"),
                             QStringLiteral("ningmengcookie \u5df2\u590d\u5236\u5230\u526a\u8d34\u677f\uff1a\n") + cookie);
}


void QtWidgetsApplication1::setupTray()
{
    const bool production = lemoncofighelper::CurrentHttpEnvironment() == "Pro";
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(production ? "#287de0" : "#ed922b"));
        if (production) painter.drawEllipse(QRectF(1, 1, size - 2, size - 2));
        else painter.drawRoundedRect(QRectF(1, 1, size - 2, size - 2), size / 5.0, size / 5.0);
        QFont font("Arial"); font.setBold(true); font.setPixelSize(size * 0.65);
        painter.setFont(font);
        painter.setPen(production ? Qt::white : Qt::black);
        painter.drawText(pixmap.rect(), Qt::AlignCenter, production ? "P" : "T");
        painter.end();
        icon.addPixmap(pixmap);
    }
    setWindowIcon(icon);
    m_trayIcon = new QSystemTrayIcon(icon, this);
    m_trayIcon->setToolTip(QStringLiteral("柠檬云工作助手 · ") + m_environmentName);
    auto *menu = new QMenu(this);
    menu->addAction(QStringLiteral("打开主窗口"), this, &QtWidgetsApplication1::restoreFromTray);
    menu->addAction(QStringLiteral("立即刷新授权"), this, &QtWidgetsApplication1::refreshSessionRequested);
    menu->addAction(production ? QStringLiteral("切换到测试环境") : QStringLiteral("切换到正式环境"),
        this, &QtWidgetsApplication1::requestEnvironmentSwitch);
    menu->addSeparator();
    menu->addAction(QStringLiteral("退出工具"), this, &QtWidgetsApplication1::requestExit);
    m_trayIcon->setContextMenu(menu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) restoreFromTray();
    });
    m_trayIcon->show();
}

void QtWidgetsApplication1::closeEvent(QCloseEvent *event)
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        event->ignore();
        showMinimized();
        return;
    }
    event->ignore();
    m_wasMaximized = isMaximized();
    hide();
    if (!m_trayHintShown) {
        m_trayHintShown = true;
        m_trayIcon->showMessage(QStringLiteral("已在后台运行 · ") + m_environmentName,
            QStringLiteral("点击托盘图标恢复窗口；右键菜单可切换环境或退出。授权将继续自动刷新。"));
    }
}

void QtWidgetsApplication1::restoreFromTray()
{
    if (m_wasMaximized) showMaximized(); else showNormal();
    raise();
    activateWindow();
}

bool QtWidgetsApplication1::hasActiveTasks() const
{
    for (auto *object : findChildren<QObject*>()) {
        for (const auto &name : object->dynamicPropertyNames())
            if (name.startsWith("sessionBusy") && object->property(name.constData()).toBool()) return true;
    }
    for (auto *reply : CHttpRequestHelper::networkManager()->findChildren<QNetworkReply*>())
        if (!reply->isFinished()) return true;
    return false;
}

void QtWidgetsApplication1::requestEnvironmentSwitch()
{
    if (!isEnabled() || QApplication::activeModalWidget()) return;
    if (hasActiveTasks()) {
        restoreFromTray();
        QMessageBox::information(this, QStringLiteral("任务进行中"),
            QStringLiteral("请先完成或停止当前任务，再切换环境。"));
        return;
    }
    emit switchEnvironmentRequested();
}

void QtWidgetsApplication1::requestExit()
{
    if (hasActiveTasks() || QApplication::activeModalWidget()) {
        restoreFromTray();
        QMessageBox::information(this, QStringLiteral("任务进行中"), QStringLiteral("请先完成或停止当前任务，再退出工具。"));
        return;
    }
    qApp->quit();
}

void QtWidgetsApplication1::updateSessionStatus(const QString &message, bool warning)
{
    if (auto *label = findChild<QLabel*>("sessionStatusText")) {
        label->setText(warning ? QStringLiteral("授权需要关注") : QStringLiteral("授权自动续期"));
        label->setToolTip(message);
    }
    if (auto *card = findChild<QFrame*>("sideSessionCard")) card->setToolTip(message);
    if (warning && !m_sessionWarning)
        m_trayIcon->showMessage(QStringLiteral("登录授权 · ") + m_environmentName, message, QSystemTrayIcon::Warning);
    m_sessionWarning = warning;
    m_trayIcon->setToolTip(QStringLiteral("柠檬云工作助手 · ") + m_environmentName + "\n" + message);
}
