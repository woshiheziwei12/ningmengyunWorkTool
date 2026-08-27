#ifndef EMPTYSUBJECTNAMEWIDGET_H
#define EMPTYSUBJECTNAMEWIDGET_H

#include <QString>
#include <QStringList>
#include <QWidget>

class GlobalData;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QLabel;
class QPushButton;
class RepairPathConfigWidget;
class RepairInputFilePickerWidget;
class QTextEdit;

class EmptySubjectNameWidget : public QWidget
{
    Q_OBJECT

public:
    enum RepairMode
    {
        FillEmptyName,
        TrimNameWhitespace
    };

    explicit EmptySubjectNameWidget(GlobalData *globalData,
                                    QWidget *parent = nullptr,
                                    RepairMode repairMode = FillEmptyName);
    void refreshInputFiles();

signals:
    void backToMain();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onBrowseClicked();
    void onBrowseOutputDirClicked();
    void onBackClicked();

private:
    void setupUi();
    void processSelectedFiles(const QStringList &zipPaths);
    void processZip(const QString &zipPath);
    void setRunning(bool running, const QString &statusText = QString());
    QString defaultOutputDir() const;
    QString outputDir() const;
    void updateOutputDirLabel();
    QString featureTitle() const;

    GlobalData *m_pGlobalData;
    RepairMode m_repairMode;
    QLabel *m_pOutputDirLabel;
    RepairPathConfigWidget *m_pPathConfig;
    RepairInputFilePickerWidget *m_pInputFilePicker;
    QLabel *m_pDropLabel;
    QLabel *m_pStatusLabel;
    QTextEdit *m_pLogEdit;
    QPushButton *m_pBtnBrowse;
    QPushButton *m_pBtnOutputDir;
    QPushButton *m_pBtnBack;
    QString m_outputDir;
    bool m_batchProcessing;
    int m_batchSuccessCount;
    int m_batchFailureCount;
    int m_batchNoChangeCount;
    QStringList m_batchResults;
};

#endif // EMPTYSUBJECTNAMEWIDGET_H
