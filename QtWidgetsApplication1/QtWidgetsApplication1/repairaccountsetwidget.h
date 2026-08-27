#ifndef REPAIRACCOUNTSETWIDGET_H
#define REPAIRACCOUNTSETWIDGET_H

#include <QList>
#include <QMap>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QWidget>

struct SubjectConflict;

class GlobalData;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QLabel;
class QPushButton;
class RepairPathConfigWidget;
class QTableWidget;
class QTextEdit;
class QTemporaryDir;

struct RepairAccountSetInfo
{
    QString asName;
    QString startDate;
    QString jsonPath;
    QString sourcePath;
    QString status;
    bool valid;
};

// Record of a sibling-duplicate rename
struct SiblingRenameRecord
{
    QString acctCode;
    QString originalName;
    QString newName;
};

struct SubjectConflictRepairRecord
{
    QString acctCode;
    QString keptName;
    QStringList removedNames;
    int removedRecordCount = 0;
};

struct AllowNullAaRepairRecord
{
    QString acctCode;
    QString acctName;
    QStringList appendedAaTypes;
};

struct RepairPipelineSummary
{
    QString accountSetName;
    QString outputPath;
    QList<SubjectConflictRepairRecord> conflictRecords;
    QList<SiblingRenameRecord> siblingRenameRecords;
    QList<AllowNullAaRepairRecord> allowNullAaRecords;
    QStringList removedBalanceDetails;
    int removedConflictRecordCount = 0;
    int scannedVoucherCount = 0;
    int missingAaItemCount = 0;
    int appendedAaTypeCount = 0;
    int removedBalanceCount = 0;
};

class RepairAccountSetWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RepairAccountSetWidget(GlobalData *globalData, QWidget *parent = nullptr);
    ~RepairAccountSetWidget() override;

    void setDateRepairOnly(bool dateRepairOnly);
    void setSiblingNameRepairOnly(bool siblingNameRepairOnly);
    void loadAccountSetList();

signals:
    void backToMain();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onBackClicked();
    void onRefreshClicked();
    void onRepairClicked();
    void onSelectAllChanged(bool checked);

private:
    void setupUi();
    void runRepairPipeline();
    void ensureRepairDir() const;
    void scanJsonFiles();
    void fillTable();
    bool parseAccountSetFile(const QString &filePath, RepairAccountSetInfo &info, QString *errorMessage) const;
    bool extractZipToRepairDir(const QString &zipPath, QString *errorMessage);
    bool extractAccountSetJsonForScan(const QString &zipPath,
                                      const QString &destinationPath,
                                      QString *errorMessage) const;
    bool repairJsonFile(const RepairAccountSetInfo &info,
                        const QMap<QString, QString> &conflictResolutions,
                        QString *outputPath,
                        RepairPipelineSummary *pipelineSummary,
                        int *voucherDateRepairCount,
                        QString *errorMessage) const;
    QList<SubjectConflict> detectSubjectConflicts(const QJsonArray &accountList) const;
    QJsonArray fixSiblingNameDuplicates(const QJsonArray &accountList,
                                        const QSet<QString> &usedAccountCodes,
                                        QList<SiblingRenameRecord> &records) const;
    bool repairAllowNullAaTypes(QJsonObject *root,
                                RepairPipelineSummary *summary,
                                QString *errorMessage) const;
    bool zipRepairedFile(const QString &jsonFilePath, QString *zipFilePath, QString *errorMessage) const;
    bool uploadRepairedFile(const QString &asId, const QString &asName, const QString &zipFileName, const QString &zipFilePath, QString *errorMessage);
    QString repairDir() const;
    QString repairedDir() const;
    QList<int> selectedRows() const;

    GlobalData *m_pGlobalData;
    QLabel *m_pTitleLabel;
    QLabel *m_pDirLabel;
    QLabel *m_pDropLabel;
    RepairPathConfigWidget *m_pPathConfig;
    QTableWidget *m_pAccountTable;
    QTextEdit *m_pResultEdit;
    QPushButton *m_pBtnSelectAll;
    QPushButton *m_pBtnRefresh;
    QPushButton *m_pBtnRepair;
    QPushButton *m_pBtnBack;
    bool m_dateRepairOnly;
    bool m_siblingNameRepairOnly;
    QTemporaryDir *m_pScanTemporaryDir;

    QList<RepairAccountSetInfo> m_accountSetList;
};

#endif // REPAIRACCOUNTSETWIDGET_H
