#include "functionselectwidget.h"
#include "Common/globaldata.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVariant>
#include <QMessageBox>

FunctionSelectWidget::FunctionSelectWidget(GlobalData *globalData, QWidget *parent)
    : QWidget(parent),
      m_pGlobalData(globalData)
{
    setupUi();
}

void FunctionSelectWidget::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(40, 30, 40, 40);
    mainLayout->setSpacing(0);

    // --- 标题区域 ---
    QLabel *titleLabel = new QLabel(QStringLiteral("\u67e0\u6aac\u4e91\u5de5\u4f5c\u4f7f\u7528\u5de5\u5177"));
    titleLabel->setObjectName("labelFuncTitle");
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    mainLayout->addSpacing(30);

    // --- 功能按钮网格 ---
    QGridLayout *gridLayout = new QGridLayout();
    gridLayout->setSpacing(20);
    gridLayout->setContentsMargins(20, 0, 20, 0);

    QPushButton *btn1 = createFunctionButton(QStringLiteral("\u5220\u9664\u8d26\u5957"), "btnDeleteAccount");
    QPushButton *btn2 = createFunctionButton(QStringLiteral("\u5907\u4efd\u4e0b\u8f7d\u8d26\u5957"), "btnBackupAccount");
    QPushButton *btn3 = createFunctionButton(QStringLiteral("\u4fee\u590d\u8d26\u5957"), "btnRepairAccount");
    QPushButton *btn4 = createFunctionButton(QStringLiteral("\u622a\u53d6\u8d26\u5957\u7279\u5b9a\u5e74\u4efd\u7684\u6570\u636e"), "btnExtractYearData");
    QPushButton *btn5 = createFunctionButton(QStringLiteral("\u79d1\u76ee\u7f16\u7801\u51b2\u7a81\u5904\u7406"), "btnSubjectCodeConflict");
    QPushButton *btn6 = createFunctionButton(QStringLiteral("\u5144\u5f1f\u79d1\u76ee\u540d\u79f0\u91cd\u590d\u5904\u7406"), "btnSiblingNameDuplicate");

    connect(btn1, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnDeleteAccount_clicked);
    connect(btn2, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnBackupAccount_clicked);
    connect(btn3, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnRepairAccount_clicked);
    connect(btn4, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnExtractYearData_clicked);
    connect(btn5, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnSubjectCodeConflict_clicked);
    connect(btn6, &QPushButton::clicked, this, &FunctionSelectWidget::on_btnSiblingNameDuplicate_clicked);

    // 3列 x 2行 布局
    gridLayout->addWidget(btn1, 0, 0);
    gridLayout->addWidget(btn2, 0, 1);
    gridLayout->addWidget(btn3, 0, 2);
    gridLayout->addWidget(btn4, 1, 0);
    gridLayout->addWidget(btn5, 1, 1);
    gridLayout->addWidget(btn6, 1, 2);

    mainLayout->addLayout(gridLayout);
    mainLayout->addStretch();
}

QPushButton* FunctionSelectWidget::createFunctionButton(const QString &text, const QString &objectName)
{
    QPushButton *btn = new QPushButton(text, this);
    btn->setObjectName(objectName);
    btn->setProperty("funcButton", true);
    btn->setMinimumSize(160, 100);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return btn;
}

void FunctionSelectWidget::on_btnDeleteAccount_clicked()
{
    emit functionSelected(1);
}

void FunctionSelectWidget::on_btnBackupAccount_clicked()
{
    emit functionSelected(2);
}

void FunctionSelectWidget::on_btnRepairAccount_clicked()
{
    emit functionSelected(3);
}

void FunctionSelectWidget::on_btnExtractYearData_clicked()
{
    emit functionSelected(4);
}

void FunctionSelectWidget::on_btnSubjectCodeConflict_clicked()
{
    emit functionSelected(5);
}

void FunctionSelectWidget::on_btnSiblingNameDuplicate_clicked()
{
    emit functionSelected(6);
}
