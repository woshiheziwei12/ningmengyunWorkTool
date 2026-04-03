#include "QtWidgetsApplication1.h"
#include "functionselectwidget.h"
#include "deleteaccountsetwidget.h"
#include "backupaccountsetwidget.h"
#include "Common/globaldata.h"
#include <QFile>
#include <QStackedWidget>
#include <QMessageBox>

QtWidgetsApplication1::QtWidgetsApplication1(GlobalData *globalData, QWidget *parent)
    : QMainWindow(parent),
      m_pGlobalData(globalData),
      m_pFunctionSelectWidget(nullptr),
      m_pDeleteAccountSetWidget(nullptr),
      m_pBackupAccountSetWidget(nullptr)
{
    ui.setupUi(this);
    setWindowTitle(QStringLiteral("\u67e0\u6aac\u4e91\u5de5\u4f5c\u4f7f\u7528\u5de5\u5177"));
    setWindowIcon(QIcon(":/Resource/Images/logo-reverse.png"));
    resize(700, 500);

    // 加载样式表
    QString qss;
    QFile file(":/Resource/qss/" + m_pGlobalData->m_strStyleName);
    if (file.open(QFile::ReadOnly))
    {
        qss = QLatin1String(file.readAll());
        file.close();
    }
    if (!qss.isEmpty())
    {
        setStyleSheet(qss);
    }

    // 使用 QStackedWidget 管理页面导航
    m_pStackedWidget = new QStackedWidget(this);
    setCentralWidget(m_pStackedWidget);

    // 功能选择页 (index 0)
    m_pFunctionSelectWidget = new FunctionSelectWidget(m_pGlobalData, this);
    m_pStackedWidget->addWidget(m_pFunctionSelectWidget);
    connect(m_pFunctionSelectWidget, &FunctionSelectWidget::functionSelected,
            this, &QtWidgetsApplication1::onFunctionSelected);

    showFunctionSelect();
}

QtWidgetsApplication1::~QtWidgetsApplication1()
{
}

void QtWidgetsApplication1::showFunctionSelect()
{
    m_pStackedWidget->setCurrentWidget(m_pFunctionSelectWidget);
}

void QtWidgetsApplication1::showDeleteAccountSet()
{
    if (!m_pDeleteAccountSetWidget)
    {
        m_pDeleteAccountSetWidget = new DeleteAccountSetWidget(m_pGlobalData, this);
        m_pStackedWidget->addWidget(m_pDeleteAccountSetWidget);
        connect(m_pDeleteAccountSetWidget, &DeleteAccountSetWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    m_pStackedWidget->setCurrentWidget(m_pDeleteAccountSetWidget);
    m_pDeleteAccountSetWidget->loadAccountSetList();
}

void QtWidgetsApplication1::showBackupAccountSet()
{
    if (!m_pBackupAccountSetWidget)
    {
        m_pBackupAccountSetWidget = new BackupAccountSetWidget(m_pGlobalData, this);
        m_pStackedWidget->addWidget(m_pBackupAccountSetWidget);
        connect(m_pBackupAccountSetWidget, &BackupAccountSetWidget::backToMain,
                this, &QtWidgetsApplication1::onBackToMain);
    }
    m_pStackedWidget->setCurrentWidget(m_pBackupAccountSetWidget);
    m_pBackupAccountSetWidget->loadAccountSetList();
}

void QtWidgetsApplication1::onBackToMain()
{
    showFunctionSelect();
}

void QtWidgetsApplication1::onFunctionSelected(int functionId)
{
    switch (functionId)
    {
    case 1:
        showDeleteAccountSet();
        break;
    case 2:
        showBackupAccountSet();
        break;
    case 3:
        QMessageBox::information(this, QStringLiteral("\u4fee\u590d\u8d26\u5957"),
                                 QStringLiteral("\u4fee\u590d\u8d26\u5957\u529f\u80fd\u5f85\u5b9e\u73b0"));
        break;
    case 4:
        QMessageBox::information(this, QStringLiteral("\u622a\u53d6\u8d26\u5957\u7279\u5b9a\u5e74\u4efd\u7684\u6570\u636e"),
                                 QStringLiteral("\u622a\u53d6\u8d26\u5957\u7279\u5b9a\u5e74\u4efd\u7684\u6570\u636e\u529f\u80fd\u5f85\u5b9e\u73b0"));
        break;
    case 5:
        QMessageBox::information(this, QStringLiteral("\u79d1\u76ee\u7f16\u7801\u51b2\u7a81\u5904\u7406"),
                                 QStringLiteral("\u79d1\u76ee\u7f16\u7801\u51b2\u7a81\u5904\u7406\u529f\u80fd\u5f85\u5b9e\u73b0"));
        break;
    case 6:
        QMessageBox::information(this, QStringLiteral("\u5144\u5f1f\u79d1\u76ee\u540d\u79f0\u91cd\u590d\u5904\u7406"),
                                 QStringLiteral("\u5144\u5f1f\u79d1\u76ee\u540d\u79f0\u91cd\u590d\u5904\u7406\u529f\u80fd\u5f85\u5b9e\u73b0"));
        break;
    }
}

