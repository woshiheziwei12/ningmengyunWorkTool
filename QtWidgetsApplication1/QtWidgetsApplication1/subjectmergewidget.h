#ifndef SUBJECTMERGEWIDGET_H
#define SUBJECTMERGEWIDGET_H

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

class SubjectMergeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SubjectMergeWidget(GlobalData *globalData, QWidget *parent = nullptr);
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

#endif // SUBJECTMERGEWIDGET_H
