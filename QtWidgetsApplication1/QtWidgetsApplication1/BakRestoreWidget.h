#pragma once

#include "Common/BakBackupRestorer.h"
#include <QWidget>
#include <atomic>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QThread;

class BakRestoreWidget : public QWidget
{
    Q_OBJECT
public:
    explicit BakRestoreWidget(QWidget* parent = nullptr);
    ~BakRestoreWidget() override;
signals:
    void backToMain();
    void operationLog(const QString& message);
    void operationFinished(bool success);
private:
    friend void runBakRestoreTests();
    enum Column { Selected, File, Database, SourceVersion, InternalVersion,
        CompatibilityLevel, BackupType, BackupDate, TargetDatabase, Status, ColumnCount };
    T3SqlConnection connectionOptions() const;
    QVector<BakRestoreSelection> selections() const;
    bool eligible(int row) const;
    void invalidateAnalysis();
    void setBusy(bool busy, bool restoring = false);
    void updateSelectionState();
    void renderEntries(bool preserveSelection);
    void updateTargetPaths();
    void targetChanged();
    void testConnection();
    void readBackups();
    void restoreSelected();
    QComboBox* m_target;
    QComboBox* m_driver;
    QCheckBox* m_trust;
    QLabel* m_auth;
    QLabel* m_summary;
    QLabel* m_selectionSummary;
    QLineEdit* m_source;
    QLineEdit* m_staging;
    QLineEdit* m_dataDirectory;
    QLineEdit* m_logDirectory;
    QPushButton* m_fileButton;
    QPushButton* m_directoryButton;
    QPushButton* m_stagingButton;
    QPushButton* m_readButton;
    QPushButton* m_testButton;
    QPushButton* m_selectAllButton;
    QPushButton* m_restoreButton;
    QPushButton* m_stopButton;
    QPushButton* m_backButton;
    QTableWidget* m_table;
    QPlainTextEdit* m_log;
    QProgressBar* m_progress;
    BakBackupBatch m_batch;
    QThread* m_worker = nullptr;
    std::shared_ptr<std::atomic_bool> m_stop;
    bool m_busy = false;
};
