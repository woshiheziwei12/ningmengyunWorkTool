#ifndef ACCESS2003CONVERTERWIDGET_H
#define ACCESS2003CONVERTERWIDGET_H

#include <QStringList>
#include <QWidget>

class GlobalData;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QMimeData;
class QProgressBar;
class QPushButton;
class QTextEdit;

class Access2003ConverterWidget : public QWidget
{
    Q_OBJECT

public:
    explicit Access2003ConverterWidget(GlobalData *globalData, QWidget *parent = nullptr);

signals:
    void backToMain();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onSelectFiles();
    void onSelectDirectory();
    void onSelectOutputDirectory();
    void onClearFiles();
    void onStartConversion();

private:
    void setupUi();
    void addPaths(const QStringList &paths);
    void addDroppedPath(const QString &path);
    void setSuggestedOutputDirectory(const QString &sourceDirectory);
    void refreshFileList();
    void setRunning(bool running);
    bool hasSupportedDrop(const QMimeData *mimeData) const;

    GlobalData *m_pGlobalData;
    QStringList m_sourcePaths;
    bool m_running;
    bool m_outputDirectoryCustomized;
    QLabel *m_pAccessStatusLabel;
    QLabel *m_pSelectionSummaryLabel;
    QLineEdit *m_pOutputDirectoryEdit;
    QLineEdit *m_pPasswordEdit;
    QListWidget *m_pFileList;
    QProgressBar *m_pProgressBar;
    QTextEdit *m_pLogEdit;
    QPushButton *m_pSelectFilesButton;
    QPushButton *m_pSelectDirectoryButton;
    QPushButton *m_pSelectOutputButton;
    QPushButton *m_pClearButton;
    QPushButton *m_pStartButton;
    QPushButton *m_pBackButton;
};

#endif // ACCESS2003CONVERTERWIDGET_H
