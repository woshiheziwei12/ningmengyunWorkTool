#pragma once
#include "Common/XinZhongDaBackupRestorer.h"
#include <QWidget>
#include <atomic>
#include <memory>
class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QTableWidget;
class QThread;
class QMimeData;
class XinZhongDaRestoreWidget : public QWidget {
    Q_OBJECT
public:
    explicit XinZhongDaRestoreWidget(QWidget* parent = nullptr);
    ~XinZhongDaRestoreWidget() override;
signals:
    void backToMain();
    void operationLog(const QString& message);
    void operationFinished(bool success);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void readBackup();
    void restoreBackup();
    void setBusy(bool busy);
    void invalidate();
    QString droppedPath(const QMimeData* data) const;
    QLineEdit* m_source;
    QLineEdit* m_code;
    QComboBox* m_target;
    QComboBox* m_driver;
    QLabel* m_summary;
    QTableWidget* m_table;
    QPlainTextEdit* m_log;
    QPushButton* m_read;
    QPushButton* m_restore;
    QPushButton* m_stop;
    QPushButton* m_browse;
    QPushButton* m_file;
    QPushButton* m_back;
    QThread* m_worker = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QVector<XinZhongDaBackupAnalysis> m_catalog;
};
