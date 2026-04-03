#ifndef FUNCTIONSELECTWIDGET_H
#define FUNCTIONSELECTWIDGET_H

#include <QWidget>

class QPushButton;
class GlobalData;

class FunctionSelectWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FunctionSelectWidget(GlobalData *globalData, QWidget *parent = nullptr);

signals:
    void functionSelected(int functionId);

private slots:
    void on_btnDeleteAccount_clicked();
    void on_btnBackupAccount_clicked();
    void on_btnRepairAccount_clicked();
    void on_btnExtractYearData_clicked();
    void on_btnSubjectCodeConflict_clicked();
    void on_btnSiblingNameDuplicate_clicked();

private:
    void setupUi();
    QPushButton* createFunctionButton(const QString &text, const QString &objectName);

    GlobalData *m_pGlobalData;
};

#endif // FUNCTIONSELECTWIDGET_H
