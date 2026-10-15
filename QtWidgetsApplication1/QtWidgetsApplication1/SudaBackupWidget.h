#pragma once

#include <QWidget>
#include <atomic>
#include <memory>

class QLineEdit;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QProgressBar;
class QThread;
class QDragEnterEvent;
class QDropEvent;
class QShowEvent;
class RepairPathConfigWidget;

class SudaBackupWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SudaBackupWidget(QWidget* parent = nullptr);
    ~SudaBackupWidget() override;

signals:
    void backToMain();
    void unpackFinished(bool success, const QString& destination);

protected:
    void showEvent(QShowEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void refreshSharedPaths();
    void updateOutputPath();
    void startUnpack();
    void setRunning(bool running);
    RepairPathConfigWidget* m_pathConfig;
    QLabel* m_backupDirectory;
    QPushButton* m_backupDirectoryButton;
    QLineEdit* m_sourceEdit;
    QLineEdit* m_outputEdit;
    QPushButton* m_sourceButton;
    QPushButton* m_outputButton;
    QPushButton* m_startButton;
    QPushButton* m_cancelButton;
    QPushButton* m_backButton;
    QPushButton* m_openButton;
    QPlainTextEdit* m_log;
    QProgressBar* m_progress;
    QThread* m_worker = nullptr;
    bool m_outputCustomized = false;
    QString m_sharedOutputDir;
    QString m_completedPath;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
