#include "QtWidgetsApplication1.h"
#include "functionselectwidget.h"
#include "deleteaccountsetwidget.h"
#include "backupaccountsetwidget.h"
#include "repairaccountsetwidget.h"
#include "jsonquerywidget.h"
#include "subjectcodeconflictwidget.h"
#include "subjectcodeseparatorwidget.h"
#include "importpackageuploadwidget.h"
#include "allownullaatypewidget.h"
#include "emptysubjectnamewidget.h"
#include "referenceaccountcoderepairwidget.h"
#include "nonleafvoucherrepairwidget.h"
#include "subjectmergewidget.h"
#include "access2003converterwidget.h"
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
    setWindowIcon(QIcon(":/Resource/Images/logo-reverse.png"));

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

    // 加载样式表
    QString qss;
    QFile file(":/Resource/qss/" + m_pGlobalData->m_strStyleName);
    if (file.open(QFile::ReadOnly))
    {
        qss = QLatin1String(file.readAll());
        file.close();
    }
    if (!qss.isEmpty())
    {
        setStyleSheet(qss);
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
    m_pHeaderSubtitle = new QLabel(topBarWidget);
    m_pHeaderSubtitle->setObjectName("headerSubtitle");
    pageHeaderLayout->addWidget(m_pHeaderTitle);
    pageHeaderLayout->addWidget(m_pHeaderSubtitle);
    topBar->addLayout(pageHeaderLayout);
    topBar->addStretch();

    m_pBtnOpenWebsite = new QPushButton(QStringLiteral("\u6253\u5f00\u67e0\u6aac\u4e91\u5b98\u7f51  \u2197"), topBarWidget);
    m_pBtnOpenWebsite->setObjectName("btnOpenWebsite");
    m_pBtnOpenWebsite->setCursor(Qt::PointingHandCursor);
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

    updateResponsiveChrome();
    showFunctionSelect();
}

QtWidgetsApplication1::~QtWidgetsApplication1()
{
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

    const bool compactSidebar = width() < 720;
    const bool shortWindow = height() < 430;
    const int sideBarWidth = compactSidebar ? 74 : 226;
    const int contentWidth = qMax(0, width() - sideBarWidth);
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
    AppLogger::operation(QStringLiteral("显示功能选择页"));
    updatePageHeader(QStringLiteral("\u5de5\u4f5c\u53f0"),
                     QStringLiteral("\u96c6\u4e2d\u7ba1\u7406\u8d26\u5957\u4e0e\u6570\u636e\u5904\u7406\u4efb\u52a1"), true);
    m_pStackedWidget->setCurrentWidget(m_pFunctionSelectWidget);
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
        m_pStackedWidget->addWidget(m_pDeleteAccountSetWidget);
        connect(m_pDeleteAccountSetWidget, &DeleteAccountSetWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    m_pStackedWidget->setCurrentWidget(m_pDeleteAccountSetWidget);
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
        m_pStackedWidget->addWidget(m_pBackupAccountSetWidget);
        connect(m_pBackupAccountSetWidget, &BackupAccountSetWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    m_pStackedWidget->setCurrentWidget(m_pBackupAccountSetWidget);
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
        m_pStackedWidget->addWidget(m_pRepairAccountSetWidget);
        connect(m_pRepairAccountSetWidget, &RepairAccountSetWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    m_pRepairAccountSetWidget->setDateRepairOnly(false);
    m_pStackedWidget->setCurrentWidget(m_pRepairAccountSetWidget);
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
        m_pStackedWidget->addWidget(m_pRepairAccountSetWidget);
        connect(m_pRepairAccountSetWidget, &RepairAccountSetWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    m_pRepairAccountSetWidget->setDateRepairOnly(true);
    m_pStackedWidget->setCurrentWidget(m_pRepairAccountSetWidget);
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
        m_pStackedWidget->addWidget(m_pRepairAccountSetWidget);
        connect(m_pRepairAccountSetWidget, &RepairAccountSetWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    m_pRepairAccountSetWidget->setSiblingNameRepairOnly(true);
    m_pStackedWidget->setCurrentWidget(m_pRepairAccountSetWidget);
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
        m_pStackedWidget->addWidget(m_pEmptySubjectNameWidget);
        connect(m_pEmptySubjectNameWidget, &EmptySubjectNameWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    m_pStackedWidget->setCurrentWidget(m_pEmptySubjectNameWidget);
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
        m_pStackedWidget->addWidget(m_pJsonQueryWidget);
        connect(m_pJsonQueryWidget, &JsonQueryWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    m_pStackedWidget->setCurrentWidget(m_pJsonQueryWidget);
    m_pJsonQueryWidget->loadFileList();
}

void QtWidgetsApplication1::showSubjectCodeConflict()
{
    AppLogger::operation(QStringLiteral("\u663e\u793a\u79d1\u76ee\u7f16\u7801\u51b2\u7a81\u5904\u7406\u9875"));
    updatePageHeader(QStringLiteral("\u79d1\u76ee\u7f16\u7801\u51b2\u7a81\u5904\u7406"),
                     QStringLiteral("\u68c0\u67e5\u4f7f\u7528\u5e74\u5ea6\uff0c\u9009\u62e9\u4fdd\u7559\u79d1\u76ee\u5e76\u8f93\u51fa\u5904\u7406\u540e ZIP"));
    if (!m_pSubjectCodeConflictWidget)
    {
        m_pSubjectCodeConflictWidget = new SubjectCodeConflictWidget(m_pGlobalData, this);
        m_pStackedWidget->addWidget(m_pSubjectCodeConflictWidget);
        connect(m_pSubjectCodeConflictWidget, &SubjectCodeConflictWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    m_pStackedWidget->setCurrentWidget(m_pSubjectCodeConflictWidget);
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
        m_pStackedWidget->addWidget(m_pSubjectCodeSeparatorWidget);
        connect(m_pSubjectCodeSeparatorWidget, &SubjectCodeSeparatorWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    m_pStackedWidget->setCurrentWidget(m_pSubjectCodeSeparatorWidget);
    m_pSubjectCodeSeparatorWidget->refreshInputFiles();
}

void QtWidgetsApplication1::showImportPackageUpload()
{
    AppLogger::operation(QStringLiteral("显示上传压缩包页面"));
    updatePageHeader(QStringLiteral("\u4e0a\u4f20\u4e13\u4e1a\u7248\u8d26\u5957"),
                     QStringLiteral("\u4e0a\u4f20\u6570\u636e\u5305\u5e76\u8ddf\u8e2a\u5bfc\u5165\u8fdb\u5ea6"));
    if (!m_pImportPackageUploadWidget)
    {
        m_pImportPackageUploadWidget = new ImportPackageUploadWidget(m_pGlobalData, this);
        m_pStackedWidget->addWidget(m_pImportPackageUploadWidget);
        connect(m_pImportPackageUploadWidget, &ImportPackageUploadWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    m_pStackedWidget->setCurrentWidget(m_pImportPackageUploadWidget);
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
        m_pStackedWidget->addWidget(m_pAllowNullAaTypeWidget);
        connect(m_pAllowNullAaTypeWidget, &AllowNullAaTypeWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    m_pStackedWidget->setCurrentWidget(m_pAllowNullAaTypeWidget);
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
        m_pStackedWidget->addWidget(m_pReferenceAccountCodeRepairWidget);
        connect(m_pReferenceAccountCodeRepairWidget, &ReferenceAccountCodeRepairWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    m_pStackedWidget->setCurrentWidget(m_pReferenceAccountCodeRepairWidget);
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
        m_pStackedWidget->addWidget(m_pNonLeafVoucherRepairWidget);
        connect(m_pNonLeafVoucherRepairWidget, &NonLeafVoucherRepairWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    m_pStackedWidget->setCurrentWidget(m_pNonLeafVoucherRepairWidget);
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
        m_pStackedWidget->addWidget(m_pTrimSubjectNameWidget);
        connect(m_pTrimSubjectNameWidget, &EmptySubjectNameWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    m_pStackedWidget->setCurrentWidget(m_pTrimSubjectNameWidget);
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
        m_pStackedWidget->addWidget(m_pSubjectMergeWidget);
        connect(m_pSubjectMergeWidget, &SubjectMergeWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    m_pStackedWidget->setCurrentWidget(m_pSubjectMergeWidget);
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
        m_pStackedWidget->addWidget(m_pAccess2003ConverterWidget);
        connect(m_pAccess2003ConverterWidget, &Access2003ConverterWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    m_pStackedWidget->setCurrentWidget(m_pAccess2003ConverterWidget);
}

void QtWidgetsApplication1::onBackToMain()
{
    showFunctionSelect();
}

void QtWidgetsApplication1::onFunctionSelected(int functionId)
{
    AppLogger::operation(QStringLiteral("选择功能 functionId=%1").arg(functionId));
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

