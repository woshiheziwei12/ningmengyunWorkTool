#ifndef VOUCHERGROUPNUMWIDGET_H
#define VOUCHERGROUPNUMWIDGET_H

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

class VoucherGroupNumWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VoucherGroupNumWidget(GlobalData *globalData, QWidget *parent = nullptr);
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
    friend class VoucherGroupNumTestAccess;
    void setupUi();
    void appendLog(const QString &text);
    void processSelectedFiles(const QStringList &zipPaths);
    void processZip(const QString &zipPath);
    void setRunning(bool running, const QString &statusText = QString());
    QString outputDir() const;
    void updateOutputDirLabel();
    QString featureTitle() const;

    GlobalData *m_pGlobalData;
    RepairPathConfigWidget *m_pPathConfig;
    RepairInputFilePickerWidget *m_pInputFilePicker;
    QLabel *m_pDropLabel;
    QLabel *m_pStatusLabel;
    QTextEdit *m_pLogEdit;
    QPushButton *m_pBtnBrowse;
    QPushButton *m_pBtnBack;
    bool m_batchProcessing;
    int m_batchSuccessCount;
    int m_batchFailureCount;
    int m_batchNoChangeCount;
    QStringList m_batchResults;
};

#endif // VOUCHERGROUPNUMWIDGET_H
