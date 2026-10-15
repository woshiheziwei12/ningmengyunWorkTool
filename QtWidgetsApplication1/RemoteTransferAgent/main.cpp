#include "Common/RemoteFileTransfer.h"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QThread>
#include <QVBoxLayout>
#include <atomic>
#include <memory>

class AgentWindow: public QWidget
{
public:
    QSystemTrayIcon* tray=nullptr;
    bool exiting=false;
    void closeEvent(QCloseEvent* event) override
    {
        if(!exiting&&tray&&QSystemTrayIcon::isSystemTrayAvailable()){hide();event->ignore();}
        else QWidget::closeEvent(event);
    }
};
int main(int argc,char** argv)
{
    QApplication app(argc,argv);
    app.setApplicationName("LemonTransferAgent");
    const auto args=app.arguments();const int verifyIndex=args.indexOf("--verify-ui");
    AgentWindow window;if(verifyIndex>=0)window.setAttribute(Qt::WA_DontShowOnScreen);window.setWindowTitle(QString::fromUtf8(u8"柠檬云 · 远端文件传输助手"));window.resize(680,430);
    auto* layout=new QVBoxLayout(&window);
    auto* intro=new QLabel(QString::fromUtf8(u8"接收共享目录中的传输任务，在本机解压。仅处理文件，不执行命令，也不需要 SQL 账号。"));intro->setWordWrap(true);layout->addWidget(intro);
    auto* row=new QHBoxLayout();auto* directory=new QLineEdit();directory->setText("C:\\hzw");
    const int index=args.indexOf("--root");if(index>=0&&index+1<args.size())directory->setText(args.at(index+1));
    row->addWidget(new QLabel(QString::fromUtf8(u8"共享对应的本地目录")));row->addWidget(directory,1);
    auto* choose=new QPushButton(QString::fromUtf8(u8"选择"));row->addWidget(choose);layout->addLayout(row);
    auto* status=new QLabel(QString::fromUtf8(u8"未启动"));layout->addWidget(status);
    auto* log=new QPlainTextEdit();log->setReadOnly(true);log->setMaximumBlockCount(1000);layout->addWidget(log,1);
    auto* buttons=new QHBoxLayout();auto* start=new QPushButton(QString::fromUtf8(u8"启动助手"));auto* stop=new QPushButton(QString::fromUtf8(u8"停止助手"));auto* quit=new QPushButton(QString::fromUtf8(u8"退出"));
    buttons->addWidget(start);buttons->addWidget(stop);buttons->addStretch();buttons->addWidget(quit);layout->addLayout(buttons);
    auto* note=new QLabel(QString::fromUtf8(u8"启动后关闭窗口会最小化到托盘；电脑注销或重启后需重新启动助手。"));note->setWordWrap(true);layout->addWidget(note);
    QSystemTrayIcon tray(app.style()->standardIcon(QStyle::SP_DriveNetIcon),&window);window.tray=&tray;tray.setToolTip(window.windowTitle());
    QMenu menu;menu.addAction(QString::fromUtf8(u8"显示窗口"),&window,[&](){window.showNormal();window.raise();});menu.addAction(QString::fromUtf8(u8"退出"),&app,&QApplication::quit);tray.setContextMenu(&menu);tray.show();
    QObject::connect(&tray,&QSystemTrayIcon::activated,&window,[&](QSystemTrayIcon::ActivationReason reason){if(reason==QSystemTrayIcon::DoubleClick)window.showNormal();});
    QThread* worker=nullptr;std::shared_ptr<std::atomic_bool> cancellation;
    const auto setRunning=[&](bool running){start->setEnabled(!running);stop->setEnabled(running);directory->setEnabled(!running);choose->setEnabled(!running);};setRunning(false);
    QObject::connect(choose,&QPushButton::clicked,&window,[&](){const auto path=QFileDialog::getExistingDirectory(&window,QString::fromUtf8(u8"共享本地目录"),directory->text());if(!path.isEmpty())directory->setText(QDir::toNativeSeparators(path));});
    QObject::connect(start,&QPushButton::clicked,&window,[&](){
        if(worker)return;
        const QString root=directory->text().trimmed();cancellation=std::make_shared<std::atomic_bool>(false);const auto cancel=cancellation;
        setRunning(true);status->setText(QString::fromUtf8(u8"启动中"));
        worker=QThread::create([&,root,cancel](){
            const auto report=[&](const QString& message){QMetaObject::invokeMethod(&window,[&,message](){log->appendPlainText(message);status->setText(message);},Qt::QueuedConnection);};
            try{RemoteFileTransfer::serve(root,[cancel](){return cancel->load();},report);}
            catch(...){report(QString::fromUtf8(u8"助手发生异常，已停止。"));}
        });
        QObject::connect(worker,&QThread::finished,&window,[&](){worker=nullptr;setRunning(false);});
        QObject::connect(worker,&QThread::finished,worker,&QObject::deleteLater);worker->start();
    });
    QObject::connect(stop,&QPushButton::clicked,&window,[&](){if(cancellation)cancellation->store(true);stop->setEnabled(false);});
    QObject::connect(quit,&QPushButton::clicked,&app,&QApplication::quit);
    QObject::connect(&app,&QApplication::aboutToQuit,&window,[&](){window.exiting=true;if(worker){cancellation->store(true);worker->wait();delete worker;worker=nullptr;}});
    window.show();
    if(verifyIndex>=0&&verifyIndex+1<args.size()) {
        QApplication::processEvents();QJsonArray captions;
        for(auto* button:window.findChildren<QPushButton*>())captions.append(button->text());
        QString backendError;
        RemoteFileTransfer::serve(QDir::tempPath()+"/missing-"+QUuid::createUuid().toString(),{},[&](const QString& message){backendError=message;});
        QFile output(args.at(verifyIndex+1));if(!output.open(QIODevice::WriteOnly))return 2;
        output.write(QJsonDocument(QJsonObject{{"title",window.windowTitle()},{"buttons",captions},{"backendError",backendError}}).toJson());
        window.grab().save(args.at(verifyIndex+1)+".png");return 0;
    }
    start->click();return app.exec();
}
