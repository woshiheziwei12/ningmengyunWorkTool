#pragma once

#include <QtWidgets/QMainWindow>
#include <QPointer>
#include <QVector>
#include <QHash>
#include "ui_QtWidgetsApplication1.h"

class GlobalData;
class FunctionSelectWidget;
class DeleteAccountSetWidget;
class BackupAccountSetWidget;
class RepairAccountSetWidget;
class JsonQueryWidget;
class WecomDocumentWidget;
class WecomLocalServer;
class BrowserLoginBridge;
class SubjectCodeConflictWidget;
class SubjectCodeSeparatorWidget;
class ImportPackageUploadWidget;
class SubjectBalanceImportWidget;
class AllowNullAaTypeWidget;
class EmptySubjectNameWidget;
class ReferenceAccountCodeRepairWidget;
class NonLeafVoucherRepairWidget;
class SubjectMergeWidget;
class Access2003ConverterWidget;
class DatabaseCleanupWidget;
class BackupRestoreWidget;
class VoucherGroupNumWidget;
class QStackedWidget;
class QPushButton;
class QLabel;
class QFrame;
class QVBoxLayout;
class QHBoxLayout;
class QResizeEvent;
class QShowEvent;
class QScreen;
class QSystemTrayIcon;
class QCloseEvent;

class QtWidgetsApplication1 : public QMainWindow
{
    Q_OBJECT

public:
    explicit QtWidgetsApplication1(GlobalData *globalData, QWidget *parent = nullptr);
    ~QtWidgetsApplication1();
    bool hasActiveTasks() const;
    void restoreFromTray();
    void updateSessionStatus(const QString &message, bool warning);

signals:
    void switchEnvironmentRequested();
    void refreshSessionRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onFunctionSelected(int functionId);
    void onBackToMain();
    void onOpenWebsite();
    void onCopyNingMengCookie();

private:
    void setupTray();
    void requestEnvironmentSwitch();
    void requestExit();
    QSystemTrayIcon *m_trayIcon = nullptr;
    bool m_wasMaximized = false;
    bool m_trayHintShown = false;
    bool m_sessionWarning = false;
    QString m_environmentName;
    void updatePageHeader(const QString &title, const QString &subtitle, bool isHome = false);
    void updateResponsiveChrome();
    void trackScreen(QScreen *screen);
    void applyScreenConstraints(QScreen *screen);
    void onAvailableGeometryChanged(const QRect &geometry);
    void onScreenChanged(QScreen *screen);
    void showFunctionSelect();
    void showCategory(int category);
    void updateNavigation(int category);
    void prepareToolPage(QWidget *page);
    void addToolPage(QWidget *page);
    void showToolPage(QWidget *page);
    QHash<QWidget*, QWidget*> m_toolContainers;
    QVector<QPushButton*> m_categoryButtons;
    int m_returnCategory = 0;
    void showDeleteAccountSet();
    void showBackupAccountSet();
    void showRepairAccountSet();
    void showVoucherDateRepair();
    void showSiblingNameRepair();
    void showEmptySubjectNameRepair();
    void showJsonQuery();
    void showWecomDocument();
    void ensureWecomDocument();
    void showSubjectCodeConflict();
    void showSubjectCodeSeparator();
    void showImportPackageUpload();
    void showSubjectBalanceImport();
    void showAllowNullAaType();
    void showReferenceAccountCodeRepair();
    void showNonLeafVoucherRepair();
    void showTrimSubjectName();
    void showSubjectMerge();
    void showAccess2003Converter();
    void showDatabaseCleanup();
    void showBackupRestore(int softwareId = 0);
    void showVoucherGroupNumRepair();

    Ui::QtWidgetsApplication1Class ui;
    GlobalData *m_pGlobalData;

    QStackedWidget *m_pStackedWidget;
    FunctionSelectWidget *m_pFunctionSelectWidget;
    DeleteAccountSetWidget *m_pDeleteAccountSetWidget;
    BackupAccountSetWidget *m_pBackupAccountSetWidget;
    RepairAccountSetWidget *m_pRepairAccountSetWidget;
    JsonQueryWidget *m_pJsonQueryWidget;
    WecomDocumentWidget *m_pWecomDocumentWidget = nullptr;
    WecomLocalServer *m_wecomLocalServer = nullptr;
    BrowserLoginBridge *m_browserLoginBridge = nullptr;
    SubjectCodeConflictWidget *m_pSubjectCodeConflictWidget;
    SubjectCodeSeparatorWidget *m_pSubjectCodeSeparatorWidget;
    ImportPackageUploadWidget *m_pImportPackageUploadWidget;
    SubjectBalanceImportWidget *m_pSubjectBalanceImportWidget = nullptr;
    AllowNullAaTypeWidget *m_pAllowNullAaTypeWidget;
    EmptySubjectNameWidget *m_pEmptySubjectNameWidget;
    EmptySubjectNameWidget *m_pTrimSubjectNameWidget;
    ReferenceAccountCodeRepairWidget *m_pReferenceAccountCodeRepairWidget;
    NonLeafVoucherRepairWidget *m_pNonLeafVoucherRepairWidget;
    SubjectMergeWidget *m_pSubjectMergeWidget;
    Access2003ConverterWidget *m_pAccess2003ConverterWidget;
    BackupRestoreWidget *m_pBackupRestoreWidget = nullptr;
    DatabaseCleanupWidget *m_pDatabaseCleanupWidget = nullptr;
    VoucherGroupNumWidget *m_pVoucherGroupNumWidget = nullptr;
    QPushButton *m_pBtnOpenWebsite;
    QPushButton *m_pBtnHome;
    QLabel *m_pHeaderTitle;
    QLabel *m_pHeaderSubtitle;
    QWidget *m_pSideBarWidget;
    QWidget *m_pBrandWidget;
    QLabel *m_pBrandTitle;
    QLabel *m_pBrandSubtitle;
    QLabel *m_pNavCaption;
    QFrame *m_pSessionCard;
    QLabel *m_pSideVersion;
    QVBoxLayout *m_pSideBarLayout;
    QHBoxLayout *m_pBrandLayout;
    QPointer<QScreen> m_pTrackedScreen;
};

