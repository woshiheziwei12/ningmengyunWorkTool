#include "startupwindow.h"
#include "ui_startupwindow.h"
#include "pwdloginwidget.h"
#include "smsloginwidget.h"
#include "micromsgloginwidget.h"
#include "lemoncofighelper.h"
#include <QMouseEvent>
#include <QDesktopServices>
#include <QSslSocket>

StartUpWindow::StartUpWindow(GlobalData *globalData, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::StartUpWindow),
    mDrag(false),
    m_pMicroMsgImageWidget(nullptr),
    m_pPwdLoginForm(nullptr),
    m_pSmsLoginForm(nullptr)
{
    ui->setupUi(this);
    ui->btnSmsMode->setChecked(false);
    ui->btnPwdMode->setChecked(false);
    ui->btnMicroMsgMode->setChecked(true);

    m_pGlobalData = globalData;

    // SSL 诊断日志
    qDebug() << "SSL support:" << QSslSocket::supportsSsl();
    qDebug() << "SSL library build version:" << QSslSocket::sslLibraryBuildVersionString();
    qDebug() << "SSL library runtime version:" << QSslSocket::sslLibraryVersionString();

    m_loginMothed = WeiXinSaoMa;
    lemoncofighelper configHelper;
    registerUrl = configHelper.GetAccountUrlByKey(lemoncofighelper::kRegisterUrlKey);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);

    mLocation = this->geometry();

    ui->widgetTitle->installEventFilter(this);
    ui->labelFlag->setVisible(false);

    QString qss;
    QFile file(":/Resource/qss/" + m_pGlobalData->m_strStyleName);
    if (file.open(QFile::ReadOnly)) {
        qss = QLatin1String(file.readAll());
        file.close();
    }
    if (!qss.isEmpty())
    {
        this->setStyleSheet(qss);
    }

    m_pMicroMsgImageWidget = new MicroMsgLoginWidget(m_pGlobalData, this);
    connect(m_pMicroMsgImageWidget, &MicroMsgLoginWidget::login_success, this, &StartUpWindow::on_login_success);
    changeLoginWidget(m_pMicroMsgImageWidget);
}

StartUpWindow::~StartUpWindow()
{
    delete ui;
}

void StartUpWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    {
        if (m_loginMothed == UserAndName && m_pPwdLoginForm)
        {
            emit m_pPwdLoginForm->tologin();
        }
        else if (m_loginMothed == PhoneAndSms && m_pSmsLoginForm)
        {
            emit m_pSmsLoginForm->tologin();
        }
    }
}

bool StartUpWindow::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == ui->widgetTitle)
    {
        if (e->type() == QEvent::MouseButtonDblClick)
        {
            return true;
        }
    }
    return QObject::eventFilter(obj, e);
}

void StartUpWindow::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
    {
        mDrag = true;
        mDragPos = e->globalPos() - pos();
        e->accept();
    }
}

void StartUpWindow::mouseMoveEvent(QMouseEvent *e)
{
    if (mDrag && (e->buttons() == Qt::LeftButton))
    {
        move(e->globalPos() - mDragPos);
        e->accept();
    }
}

void StartUpWindow::mouseReleaseEvent(QMouseEvent *)
{
    mDrag = false;
}

void StartUpWindow::on_btnMin_clicked()
{
    showMinimized();
}

void StartUpWindow::on_btnExit_clicked()
{
    qApp->exit();
}

void StartUpWindow::on_btnPwdMode_clicked()
{
    if (m_pPwdLoginForm == nullptr)
    {
        m_pPwdLoginForm = new PwdLoginWidget(m_pGlobalData, this);
        connect(m_pPwdLoginForm, &PwdLoginWidget::login_success, this, &StartUpWindow::on_login_success);
    }

    changeLoginWidget(m_pPwdLoginForm);
    ui->btnSmsMode->setChecked(false);
    ui->btnMicroMsgMode->setChecked(false);
    m_loginMothed = UserAndName;
}

void StartUpWindow::on_btnSmsMode_clicked()
{
    if (m_pSmsLoginForm == nullptr)
    {
        m_pSmsLoginForm = new SmsLoginWidget(m_pGlobalData, this);
        connect(m_pSmsLoginForm, &SmsLoginWidget::login_success, this, &StartUpWindow::on_login_success);
    }

    changeLoginWidget(m_pSmsLoginForm);
    ui->btnPwdMode->setChecked(false);
    ui->btnMicroMsgMode->setChecked(false);
    m_loginMothed = PhoneAndSms;
}

void StartUpWindow::on_btnMicroMsgMode_clicked()
{
    changeLoginWidget(m_pMicroMsgImageWidget);
    ui->btnPwdMode->setChecked(false);
    ui->btnSmsMode->setChecked(false);
    m_loginMothed = WeiXinSaoMa;
}

void StartUpWindow::closeEvent(QCloseEvent *event)
{
    event->accept();
}

void StartUpWindow::on_login_success()
{
    this->close();
}

void StartUpWindow::changeLoginWidget(QWidget *pLoginWidget)
{
    // 先隐藏当前布局中的所有 widget
    for (int i = 0; i < ui->loginWidget->count(); ++i)
    {
        QLayoutItem *item = ui->loginWidget->itemAt(i);
        if (item && item->widget())
            item->widget()->hide();
    }

    // 如果 widget 尚未在布局中，则添加
    if (ui->loginWidget->indexOf(pLoginWidget) < 0)
    {
        ui->loginWidget->addWidget(pLoginWidget);
    }
    pLoginWidget->show();
}

void StartUpWindow::on_btnRegistor_clicked()
{
    QDesktopServices::openUrl(QUrl(registerUrl));
}
