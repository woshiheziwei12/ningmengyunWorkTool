#ifndef FUNCTIONSELECTWIDGET_H
#define FUNCTIONSELECTWIDGET_H

#include <QWidget>
#include <QString>
#include <QVector>

class QPushButton;
class GlobalData;
class QGridLayout;
class QResizeEvent;
class QBoxLayout;
class QVBoxLayout;
class QFrame;

class FunctionSelectWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FunctionSelectWidget(GlobalData *globalData, QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;

signals:
    void functionSelected(int functionId);

private slots:
    void on_btnDeleteAccount_clicked();
    void on_btnBackupAccount_clicked();
    void on_btnRepairAccount_clicked();
    void on_btnExtractYearData_clicked();
    void on_btnSubjectCodeConflict_clicked();
    void on_btnSiblingNameDuplicate_clicked();
    void on_btnCopyNingMengCookie_clicked();
    void on_btnJsonQuery_clicked();
    void on_btnSubjectCodeSeparator_clicked();
    void on_btnImportPackageUpload_clicked();
    void on_btnAllowNullAaType_clicked();
    void on_btnVoucherDateRepair_clicked();
    void on_btnEmptySubjectName_clicked();
    void on_btnReferenceAccountCodeRepair_clicked();
    void on_btnNonLeafVoucherRepair_clicked();
    void on_btnTrimSubjectName_clicked();
    void on_btnSubjectMerge_clicked();
    void on_btnAccess2003Converter_clicked();

private:
    void setupUi();
    void reflowCards(int pageWidth);
    void reflowGrid(QGridLayout *grid,
                    const QVector<QPushButton*> &buttons,
                    int columns);
    QPushButton* createFunctionButton(const QString &title,
                                      const QString &description,
                                      const QString &iconText,
                                      const QString &objectName,
                                      const QString &accent,
                                      bool featured,
                                      const QString &badgeText = QString());

    GlobalData *m_pGlobalData;
    QGridLayout *m_pPrimaryGrid;
    QGridLayout *m_pToolsGrid;
    QVBoxLayout *m_pDashboardLayout;
    QBoxLayout *m_pHeroLayout;
    QFrame *m_pMetricCard;
    QVector<QPushButton*> m_primaryButtons;
    QVector<QPushButton*> m_toolButtons;
    int m_primaryColumnCount;
    int m_toolColumnCount;
};

#endif // FUNCTIONSELECTWIDGET_H
