#pragma once
#include <QWidget>
#include "Common/DatabaseCleaner.h"
#include <atomic>
#include <memory>

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QThread;
class DatabaseCleanupWidget : public QWidget {
    Q_OBJECT
public:
    explicit DatabaseCleanupWidget(QWidget* parent = nullptr);
    ~DatabaseCleanupWidget() override;
signals:
    void backToMain();
    void itemFinished(const QString& name, bool success, const QString& error);
private:
    friend void runDatabaseCleanupWidgetTests();
    void refresh();
    void removeSelected();
    void displayCatalog(const DatabaseCleaner::Catalog& catalog);
    DatabaseCleaner::Catalog selectedCatalog() const;
    void updateSelection();
    void setBusy(bool busy);
    void invalidate();
    QComboBox* m_target;
    QLabel* m_status;
    QTableWidget* m_table;
    QPushButton* m_refresh;
    QPushButton* m_selectAll;
    QLabel* m_selection;
    QPushButton* m_delete;
    QPushButton* m_stop;
    QThread* m_worker = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancel;
    DatabaseCleaner::Catalog m_catalog;
    bool m_loaded = false;
    bool m_busy = false;
};
