#pragma once

#include <QtWidgets/QMainWindow>
#include <QPointer>
#include "ui_QtWidgetsApplication1.h"

class GlobalData;
class FunctionSelectWidget;
class DeleteAccountSetWidget;
class BackupAccountSetWidget;
class RepairAccountSetWidget;
class JsonQueryWidget;
class SubjectCodeConflictWidget;
class SubjectCodeSeparatorWidget;
class ImportPackageUploadWidget;
class AllowNullAaTypeWidget;
class EmptySubjectNameWidget;
class ReferenceAccountCodeRepairWidget;
class NonLeafVoucherRepairWidget;
class SubjectMergeWidget;
class Access2003ConverterWidget;
class QStackedWidget;
class QPushButton;
class QLabel;
class QFrame;
class QVBoxLayout;
class QHBoxLayout;
class QResizeEvent;
class QShowEvent;
class QScreen;

class QtWidgetsApplication1 : public QMainWindow
{
    Q_OBJECT

public:
    explicit QtWidgetsApplication1(GlobalData *globalData, QWidget *parent = nullptr);
    ~QtWidgetsApplication1();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onFunctionSelected(int functionId);
    void onBackToMain();
    void onOpenWebsite();
    void onCopyNingMengCookie();

private:
    void updatePageHeader(const QString &title, const QString &subtitle, bool isHome = false);
    void updateResponsiveChrome();
    void trackScreen(QScreen *screen);
    void applyScreenConstraints(QScreen *screen);
    void onAvailableGeometryChanged(const QRect &geometry);
    void onScreenChanged(QScreen *screen);
    void showFunctionSelect();
    void showDeleteAccountSet();
    void showBackupAccountSet();
    void showRepairAccountSet();
    void showVoucherDateRepair();
    void showSiblingNameRepair();
    void showEmptySubjectNameRepair();
    void showJsonQuery();
    void showSubjectCodeConflict();
    void showSubjectCodeSeparator();
    void showImportPackageUpload();
    void showAllowNullAaType();
    void showReferenceAccountCodeRepair();
    void showNonLeafVoucherRepair();
    void showTrimSubjectName();
    void showSubjectMerge();
    void showAccess2003Converter();

    Ui::QtWidgetsApplication1Class ui;
    GlobalData *m_pGlobalData;

    QStackedWidget *m_pStackedWidget;
    FunctionSelectWidget *m_pFunctionSelectWidget;
    DeleteAccountSetWidget *m_pDeleteAccountSetWidget;
    BackupAccountSetWidget *m_pBackupAccountSetWidget;
    RepairAccountSetWidget *m_pRepairAccountSetWidget;
    JsonQueryWidget *m_pJsonQueryWidget;
    SubjectCodeConflictWidget *m_pSubjectCodeConflictWidget;
    SubjectCodeSeparatorWidget *m_pSubjectCodeSeparatorWidget;
    ImportPackageUploadWidget *m_pImportPackageUploadWidget;
    AllowNullAaTypeWidget *m_pAllowNullAaTypeWidget;
    EmptySubjectNameWidget *m_pEmptySubjectNameWidget;
    EmptySubjectNameWidget *m_pTrimSubjectNameWidget;
    ReferenceAccountCodeRepairWidget *m_pReferenceAccountCodeRepairWidget;
    NonLeafVoucherRepairWidget *m_pNonLeafVoucherRepairWidget;
    SubjectMergeWidget *m_pSubjectMergeWidget;
    Access2003ConverterWidget *m_pAccess2003ConverterWidget;
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

