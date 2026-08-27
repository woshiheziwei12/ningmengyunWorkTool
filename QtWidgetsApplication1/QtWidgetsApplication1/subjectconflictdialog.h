#ifndef SUBJECTCONFLICTDIALOG_H
#define SUBJECTCONFLICTDIALOG_H

#include <QDialog>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

class QTableWidget;
class QPushButton;
class QLabel;
class QButtonGroup;

// Subject code conflict: same AcctCode with different AcctName values
struct SubjectConflict
{
    QString acctCode;
    QStringList names;
    QMap<QString, QList<int>> accountYearsByName;
    QList<int> voucherYears;
    QList<int> openingBalanceYears;
    QList<int> descendantVoucherYears;
    QList<int> descendantOpeningBalanceYears;
    bool hasOpeningBalanceWithUnknownYear = false;
    bool hasDescendantOpeningBalanceWithUnknownYear = false;
    bool accountYearDataAvailable = false;
    bool usageDataAvailable = false;

    SubjectConflict() {}
};

class SubjectConflictDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SubjectConflictDialog(const QList<SubjectConflict> &conflicts,
                                   const QString &asName,
                                   QWidget *parent = nullptr);

    // Returns user selection: AcctCode -> chosen AcctName to KEEP
    QMap<QString, QString> selectedNames() const;

private slots:
    void onConfirmClicked();
    void onCancelClicked();
    void onSelectAllFirst();
    void onSelectAllLast();
    void onCopyClicked();
    void onCopyAllClicked();

private:
    void setupUi();
    void fillTable();
    QString copyText(int conflictIndex = -1) const;

    QList<SubjectConflict> m_conflicts;
    QString m_asName;
    QTableWidget *m_pTable;
    QPushButton *m_pBtnConfirm;
    QPushButton *m_pBtnCancel;
    QPushButton *m_pBtnSelectFirst;
    QPushButton *m_pBtnSelectLast;
    QPushButton *m_pBtnCopy;
    QPushButton *m_pBtnCopyAll;
    QLabel *m_pTitleLabel;

    // One QButtonGroup per conflict
    QList<QButtonGroup *> m_buttonGroups;
};

#endif // SUBJECTCONFLICTDIALOG_H
