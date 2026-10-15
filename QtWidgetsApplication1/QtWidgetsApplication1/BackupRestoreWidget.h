#pragma once
#include <QWidget>

class QComboBox;
class QVBoxLayout;

class BackupRestoreWidget : public QWidget
{
    Q_OBJECT
public:
    explicit BackupRestoreWidget(QWidget* parent = nullptr);
    void selectSoftware(int legacyFunctionId);
signals:
    void backToMain();
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void changeSoftware();
    bool isBusy() const;
    QComboBox* m_software;
    QVBoxLayout* m_layout;
    QWidget* m_page = nullptr;
    int m_currentSoftware = -1;
};
