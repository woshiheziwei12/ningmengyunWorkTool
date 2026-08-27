#ifndef NONLEAFVOUCHERREPAIRWIDGET_H
#define NONLEAFVOUCHERREPAIRWIDGET_H

#include <QStringList>
#include <QWidget>

class GlobalData;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QLabel;
class QPushButton;
class RepairInputFilePickerWidget;
class RepairPathConfigWidget;
class QTextEdit;

class NonLeafVoucherRepairWidget : public QWidget
{
    Q_OBJECT

public:
    explicit NonLeafVoucherRepairWidget(GlobalData *globalData,
                                        QWidget *parent = nullptr);
    void refreshInputFiles();

signals:
    void backToMain();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onBrowseClicked();
    void onBackClicked();

private:
    void setupUi();
    void processSelectedFiles(const QStringList &filePaths);
    void startProcessFile(const QString &filePath);
    void setRunning(bool running, const QString &message = QString());
    QString workRootDir() const;
    QString outputDir() const;

    GlobalData *m_pGlobalData;
    RepairPathConfigWidget *m_pPathConfig;
    RepairInputFilePickerWidget *m_pInputFilePicker;
    QLabel *m_pDropLabel;
    QLabel *m_pStatusLabel;
    QTextEdit *m_pLogEdit;
    QPushButton *m_pBtnBrowse;
    QPushButton *m_pBtnBack;
    int m_requestId;
};

#endif // NONLEAFVOUCHERREPAIRWIDGET_H
