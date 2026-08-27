#ifndef SUBJECTCODECONFLICTWIDGET_H
#define SUBJECTCODECONFLICTWIDGET_H

#include "subjectconflictdialog.h"

#include <QJsonDocument>
#include <QList>
#include <QString>
#include <QWidget>

class GlobalData;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QLabel;
class QPushButton;
class RepairPathConfigWidget;
class QTableWidget;
class QTemporaryDir;

struct SubjectConflictSource
{
    QString sourcePath;
    QString jsonPath;
    QString packageBaseName;
    QString accountSetName;
    QString startDate;
    QString status;
    QString errorMessage;
    QString outputZipPath;
    QJsonDocument document;
    QList<SubjectConflict> conflicts;
    bool valid = false;
};

class SubjectCodeConflictWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SubjectCodeConflictWidget(GlobalData *globalData, QWidget *parent = nullptr);
    ~SubjectCodeConflictWidget() override;
    void loadInputDirectory();

signals:
    void backToMain();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onAddFilesClicked();
    void onSelectOutputDirClicked();
    void onSelectAllChanged(bool checked);
    void onClearClicked();
    void onViewClicked();
    void onProcessClicked();
    void onBackClicked();

private:
    void setupUi();
    void addInputPath(const QString &path, bool showErrors = true);
    QString copyIntoInputDirectory(const QString &path, QString *errorMessage) const;
    void addJsonSource(const QString &jsonPath,
                       const QString &sourcePath,
                       const QString &packageBaseName);
    void refreshTable();
    QList<int> selectedRows() const;
    QList<SubjectConflict> analyzeDocument(const QJsonDocument &document) const;
    bool extractZip(const QString &zipPath, QStringList *jsonPaths, QString *errorMessage);
    bool processSource(const SubjectConflictSource &source,
                       const QMap<QString, QString> &selectedNames,
                       QString *outputZipPath,
                       QString *errorMessage);
    bool createZipWithSingleJson(const QString &jsonPath,
                                 const QString &zipPath,
                                 QString *errorMessage) const;
    QString defaultOutputDir() const;
    QString outputDir() const;
    void updateOutputLabel();

    GlobalData *m_pGlobalData;
    QLabel *m_pOutputLabel;
    RepairPathConfigWidget *m_pPathConfig;
    QLabel *m_pDropLabel;
    QTableWidget *m_pTable;
    QPushButton *m_pBtnAddFiles;
    QPushButton *m_pBtnOutputDir;
    QPushButton *m_pBtnSelectAll;
    QPushButton *m_pBtnClear;
    QPushButton *m_pBtnView;
    QPushButton *m_pBtnProcess;
    QPushButton *m_pBtnBack;
    QTemporaryDir *m_pTemporaryDir;
    QList<SubjectConflictSource> m_sources;
    QString m_outputDir;
    int m_requestId;
};

#endif // SUBJECTCODECONFLICTWIDGET_H
