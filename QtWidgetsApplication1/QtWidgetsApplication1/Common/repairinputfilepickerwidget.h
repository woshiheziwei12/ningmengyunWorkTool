#ifndef REPAIRINPUTFILEPICKERWIDGET_H
#define REPAIRINPUTFILEPICKERWIDGET_H

#include <functional>
#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;

class RepairInputFilePickerWidget : public QWidget
{
public:
    explicit RepairInputFilePickerWidget(const QStringList &nameFilters,
                                         QWidget *parent = nullptr);

    void refreshFiles();
    void setRunning(bool running);
    void setProcessCallback(const std::function<void(const QStringList &)> &callback);
    void setFilesChangedCallback(const std::function<void(int)> &callback);
    QStringList selectedPaths() const;
    int fileCount() const;

private:
    QStringList m_nameFilters;
    QLabel *m_pCountLabel;
    QListWidget *m_pFileList;
    QPushButton *m_pBtnSelectAll;
    QPushButton *m_pBtnRefresh;
    QPushButton *m_pBtnProcess;
    std::function<void(const QStringList &)> m_processCallback;
    std::function<void(int)> m_filesChangedCallback;
};

#endif // REPAIRINPUTFILEPICKERWIDGET_H
