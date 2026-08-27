#ifndef REPAIRPATHCONFIGWIDGET_H
#define REPAIRPATHCONFIGWIDGET_H

#include <QWidget>
#include <functional>

class QLabel;
class QShowEvent;

class RepairPathConfigWidget : public QWidget
{
public:
    explicit RepairPathConfigWidget(QWidget *parent = nullptr);

    void refreshPaths();
    void setPathsChangedCallback(const std::function<void()> &callback);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void selectInputDir();
    void selectOutputDir();

    QLabel *m_pInputLabel;
    QLabel *m_pOutputLabel;
    std::function<void()> m_pathsChangedCallback;
};

#endif // REPAIRPATHCONFIGWIDGET_H
