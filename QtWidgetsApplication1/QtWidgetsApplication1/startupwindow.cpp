#include "startupwindow.h"
#include "ui_startupwindow.h"
#include "pwdloginwidget.h"
#include "lemoncofighelper.h"
#include <QMouseEvent>
#include <QDesktopServices>
#include <QSslSocket>
#include <QTimer>

StartUpWindow::StartUpWindow(GlobalData *globalData, QWidget *parent, const QString &credentialsDirectory) :
    QDialog(parent),
    ui(new Ui::StartUpWindow),
    mDrag(false),
    m_pPwdLoginForm(nullptr)
{
    ui->setupUi(this);
    ui->loginEnvironment->addItem(QStringLiteral("正式环境"), "Pro");
    ui->loginEnvironment->addItem(QStringLiteral("测试环境"), "Dev2");
    const bool production = lemoncofighelper::CurrentHttpEnvironment().compare("Pro", Qt::CaseInsensitive) == 0;
    ui->loginEnvironment->setCurrentIndex(production ? 0 : 1);
    lemoncofighelper::SetHttpEnvironment(ui->loginEnvironment->currentData().toString());

    m_pGlobalData = globalData;

    // SSL 诊断日志
    qDebug() << "SSL support:" << QSslSocket::supportsSsl();
    qDebug() << "SSL library build version:" << QSslSocket::sslLibraryBuildVersionString();
    qDebug() << "SSL library runtime version:" << QSslSocket::sslLibraryVersionString();

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

    m_pPwdLoginForm = new PwdLoginWidget(m_pGlobalData, this, credentialsDirectory);
    ui->loginWidget->addWidget(m_pPwdLoginForm);
    connect(m_pPwdLoginForm, &PwdLoginWidget::login_success, this, &StartUpWindow::on_login_success);
    connect(m_pPwdLoginForm, &PwdLoginWidget::loginBusyChanged, this, [this](bool busy) {
        ui->loginEnvironment->setEnabled(!busy && !m_environmentLocked);
        ui->btnRegistor->setEnabled(!busy);
    });
    connect(ui->loginEnvironment, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        m_pPwdLoginForm->setEnvironment(ui->loginEnvironment->currentData().toString());
    });
}

StartUpWindow::~StartUpWindow()
{
    delete ui;
}

void StartUpWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        emit m_pPwdLoginForm->tologin();
        return;
    }
    QDialog::keyPressEvent(event);
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
    reject();
}

void StartUpWindow::prepareEnvironmentSwitch()
{
    m_environmentLocked = true;
    ui->loginEnvironment->setEnabled(false);
    if (m_pPwdLoginForm->hasSavedPassword())
        QTimer::singleShot(0, m_pPwdLoginForm, [this]() { emit m_pPwdLoginForm->tologin(); });
}

void StartUpWindow::closeEvent(QCloseEvent *event)
{
    event->accept();
}

void StartUpWindow::on_login_success()
{
    this->close();
}

void StartUpWindow::on_btnRegistor_clicked()
{
    lemoncofighelper config;
    QDesktopServices::openUrl(QUrl(config.GetAccountUrlByKey(lemoncofighelper::kRegisterUrlKey)));
}
