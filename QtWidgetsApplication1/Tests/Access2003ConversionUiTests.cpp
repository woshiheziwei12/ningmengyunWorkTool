#include "access2003converterwidget.h"
#include "Common/repairpathsettings.h"
#include <QtWidgets>
#include <cstdio>

// This isolated UI executable must not change the user's shared input settings.
namespace { QString inputDirectory,outputDirectory; }
QString RepairPathSettings::inputDir() { return inputDirectory; }
QString RepairPathSettings::outputDir() { return outputDirectory; }
void RepairPathSettings::setInputDir(const QString& path) { inputDirectory=path; }
void RepairPathSettings::setOutputDir(const QString& path) { outputDirectory=path; }

int runRecoveryUiTest(const QString& source,const QString& output)
{
    const auto fail=[](const char* step){fprintf(stderr,"UI_FAIL=%s\n",step);return 1;};
    QDir().mkpath(output);
    inputDirectory=output+"/input";outputDirectory=output+"/recovered";
    Access2003ConverterWidget widget(nullptr);
    widget.setAttribute(Qt::WA_DontShowOnScreen);
    widget.resize(1000,820);widget.show();QApplication::processEvents();
    auto combo=widget.findChild<QComboBox*>("access2003ConversionMode");
    QLineEdit* password=nullptr;
    for(auto edit:widget.findChildren<QLineEdit*>()) if(edit->echoMode()==QLineEdit::Password) password=edit;
    if(!combo || !password || combo->currentIndex()!=0 || password->isEnabled()) return fail("initial mode");
    combo->setCurrentIndex(1);
    if(!password->isEnabled()) return fail("legacy password mode");
    combo->setCurrentIndex(0);
    QMimeData mime;mime.setUrls({QUrl::fromLocalFile(source)});
    QDragEnterEvent enter(QPoint(30,30),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&widget,&enter);
    QDropEvent drop(QPointF(30,30),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&widget,&drop);
    if(!enter.isAccepted() || !drop.isAccepted()) return fail("drop accepted");
    auto files=widget.findChild<QListWidget*>("access2003FileList");
    if(!files || files->count()!=1) return fail("selected file count");
    for(auto edit:widget.findChildren<QLineEdit*>()) if(edit->echoMode()==QLineEdit::Normal) {
        edit->setText(outputDirectory);
        QMetaObject::invokeMethod(edit,"textEdited",Q_ARG(QString,outputDirectory));
    }
    QTimer dialogs;
    QObject::connect(&dialogs,&QTimer::timeout,[](){
        for(auto window:QApplication::topLevelWidgets()) if(auto box=qobject_cast<QMessageBox*>(window)) box->accept();
    });
    dialogs.start(20);
    QMetaObject::invokeMethod(&widget,"onStartConversion");
    if(combo->isEnabled() || !widget.property("sessionBusysetRunning").toBool()) return fail("busy state");
    QElapsedTimer timer;timer.start();
    while(widget.property("sessionBusysetRunning").toBool() && timer.elapsed()<45000) {
        QApplication::processEvents();QThread::msleep(10);
    }
    QApplication::processEvents();
    const auto log=widget.findChild<QTextEdit*>("access2003ConversionLog")->toPlainText();
    const bool captured=widget.grab().save(output+"/recovery-ui.png");
    const bool passed=combo->isEnabled() && !password->isEnabled()
        && log.contains(QStringLiteral("逐字段校验通过")) && log.contains(QStringLiteral("成功 1，失败 0"))
        && QDir(outputDirectory).entryList({"*.ais"},QDir::Files).size()==1
        && captured;
    fprintf(stdout,"UI_RECOVERY=%s modeEnabled=%d passwordEnabled=%d outputs=%d captured=%d\n%s\n",passed?"PASS":"FAIL",
        combo->isEnabled(),password->isEnabled(),QDir(outputDirectory).entryList({"*.ais"},QDir::Files).size(),captured,log.toUtf8().constData());
    return passed?0:1;
}
