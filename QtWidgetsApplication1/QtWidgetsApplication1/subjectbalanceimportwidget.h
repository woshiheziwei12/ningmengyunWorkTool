#pragma once

#include "Common/subjectbalanceimport.h"
#include <QWidget>

class GlobalData;
class QComboBox;
class QDateEdit;
class QCheckBox;
class QLineEdit;
class QLabel;
class QFormLayout;
class QPushButton;
class QTextEdit;
class QTableWidget;

class SubjectBalanceImportWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SubjectBalanceImportWidget(GlobalData *globalData, QWidget *parent = nullptr,
                                       const QString &historyFilePath = QString());
    void reloadHistory();
public slots:
    void selectSourceFile(const QString &key, const QString &path);
signals:
    void backToMain();
private:
    void changeSoftware();
    void submit();
    void resumeQuery();
    void addAuxiliary();
    void updateBusy(bool busy);
    void saveRecord();
    BalanceImportSession session() const;
    QString historyPath() const;

    GlobalData *m_global;
    QString m_historyFilePath;
    SubjectBalanceImportClient *m_client;
    QList<BalanceImportSoftware> m_catalog;
    QWidget *m_form;
    QComboBox *m_software, *m_standard, *m_tax, *m_host, *m_history;
    QLineEdit *m_name;
    QDateEdit *m_month;
    QCheckBox *m_zero;
    QFormLayout *m_fileLayout;
    QMap<QString, QLineEdit*> m_fileInputs;
    QString m_generatedAccountName;
    QTableWidget *m_auxiliary;
    QPushButton *m_addAuxiliary, *m_submit, *m_resume, *m_stop;
    QLabel *m_softwareNotes, *m_status, *m_fileHint;
    QTextEdit *m_result;
    QJsonObject m_record;
};
