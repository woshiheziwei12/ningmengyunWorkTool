#pragma once
#include "Common/TPlusBackupRestorer.h"
#include <QWidget>
#include <atomic>
#include <memory>

class QComboBox;
class QLineEdit;
class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QPlainTextEdit;
class QProgressBar;
class QThread;
class QDragEnterEvent;
class QDropEvent;
class QShowEvent;
class QMimeData;

class TPlusRestoreWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TPlusRestoreWidget(QWidget* parent=nullptr);
    ~TPlusRestoreWidget() override;
signals:
    void backToMain();
    void operationLog(const QString& message);
    void operationFinished(bool success);
protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void showEvent(QShowEvent* event) override;
private:
    friend void runSqlInstanceTests();
    T3SqlConnection connectionOptions() const;
    void refreshSharedInput();
    void invalidateAnalysis();
    void setBusy(bool busy, bool restoring=false);
    void targetChanged();
    void testConnection();
    void analyzeBackup();
    void restoreSelected();
    QString droppedSource(const QMimeData* data) const;
    QComboBox* m_target;
    QComboBox* m_driver;
    QLabel* m_authDescription;
    QLabel* m_compatibility;
    QCheckBox* m_trust;
    QCheckBox* m_remoteAgent;
    QLabel* m_sharedInput;
    QLineEdit* m_source;
    QLineEdit* m_staging;
    QLineEdit* m_dataDirectory;
    QLineEdit* m_logDirectory;
    QPushButton* m_inputButton;
    QPushButton* m_sourceButton;
    QPushButton* m_stageButton;
    QPushButton* m_testButton;
    QPushButton* m_analyzeButton;
    QPushButton* m_restoreButton;
    QPushButton* m_stopButton;
    QPushButton* m_backButton;
    QTableWidget* m_table;
    QPlainTextEdit* m_log;
    QProgressBar* m_progress;
    TPlusBackupAnalysis m_analysis;
    QThread* m_worker=nullptr;
    std::shared_ptr<std::atomic_bool> m_stop;
};
