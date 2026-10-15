#pragma once
#include <QWidget>
#include <QVector>

class GlobalData;
class QPushButton;
class QGridLayout;
class QStackedWidget;
class QLineEdit;
class QLabel;

// Shared category IDs keep the dashboard and persistent navigation consistent.
class FunctionSelectWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FunctionSelectWidget(GlobalData *globalData, QWidget *parent = nullptr);
    void showCategory(int category);
    int currentCategory() const;
    static QString categoryTitle(int category);
    static QString categoryDescription(int category);
    static int categoryForFunction(int functionId);
signals:
    void functionSelected(int functionId);
    void categoryChanged(int category);
protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    struct CategoryPage {
        QGridLayout *grid = nullptr;
        QLineEdit *search = nullptr;
        QLabel *empty = nullptr;
        QLabel *heading = nullptr;
        QLabel *shortcutsHeading = nullptr;
        QVector<QPushButton*> cards;
    };
    void reflowCards();
    QPushButton *createFunctionButton(const QString &title, const QString &description,
        const QString &iconText, const QString &objectName, const QString &accent,
        bool featured, const QString &badgeText = QString());
    QStackedWidget *m_pages = nullptr;
    QVector<CategoryPage> m_categories;
};
