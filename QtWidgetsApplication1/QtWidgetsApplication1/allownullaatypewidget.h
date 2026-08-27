#ifndef ALLOWNULLAATYPEWIDGET_H
#define ALLOWNULLAATYPEWIDGET_H

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

class AllowNullAaTypeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AllowNullAaTypeWidget(GlobalData *globalData, QWidget *parent = nullptr);
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
    void processNextBatchFile();
    void startProcessZip(const QString &zipPath);
    void setRunning(bool running, const QString &message = QString());
    QString workRootDir() const;
    QString defaultOutputDir() const;
    QString outputDir() const;
    void updateOutputDirLabel();

    GlobalData *m_pGlobalData;
    QLabel *m_pDirLabel;
    RepairPathConfigWidget *m_pPathConfig;
    RepairInputFilePickerWidget *m_pInputFilePicker;
    QLabel *m_pDropLabel;
    QLabel *m_pStatusLabel;
    QTextEdit *m_pLogEdit;
    QPushButton *m_pBtnBrowse;
    QPushButton *m_pBtnOutputDir;
    QPushButton *m_pBtnBack;
    QString m_outputDir;
    int m_requestId;
    bool m_batchProcessing;
    int m_batchSuccessCount;
    int m_batchFailureCount;
    QStringList m_pendingBatchPaths;
    QStringList m_batchResults;
};

#endif // ALLOWNULLAATYPEWIDGET_H
