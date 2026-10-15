#include "Common/SqlServerCredentials.h"
#include "Common/RestorePreferences.h"
#include "Common/SqlRestoreRouting.h"
#include "TPlusRestoreWidget.h"
#include "Common/repairinputfileutils.h"
#include "Common/SqlTargetSelector.h"
#include "Common/repairpathsettings.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QShowEvent>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include <exception>

static QLineEdit* tplusEdit(QWidget* parent,const char* name,const QString& placeholder={})
{
    auto* edit=new QLineEdit(parent);edit->setObjectName(QLatin1String(name));edit->setPlaceholderText(placeholder);edit->setAcceptDrops(false);return edit;
}
TPlusRestoreWidget::TPlusRestoreWidget(QWidget* parent):QWidget(parent)
{
    setObjectName(QStringLiteral("tplusRestorePage"));setAcceptDrops(true);
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(24,20,24,20);layout->setSpacing(12);
    auto* title=new QLabel(QString::fromUtf8(u8"恢复T+备份包到数据库"),this);title->setObjectName("labelPageTitle");layout->addWidget(title);
    auto* intro=new QLabel(QString::fromUtf8(u8"选择或拖入 T+ ZIP 备份包。自动解包业务库，并从 AdditionalXml.xml 恢复供导账工具采集使用的控制库信息。"),this);
    intro->setWordWrap(true);layout->addWidget(intro);
    auto* sharedRow=new QHBoxLayout();m_sharedInput=new QLabel(this);m_sharedInput->setObjectName("tplusSharedInput");m_sharedInput->setWordWrap(true);
    m_inputButton=new QPushButton(QString::fromUtf8(u8"选择备份包目录"),this);sharedRow->addWidget(m_sharedInput,1);sharedRow->addWidget(m_inputButton);layout->addLayout(sharedRow);

    auto* connectionBox=new QGroupBox(QString::fromUtf8(u8"2. 确认恢复实例（读取后自动匹配，可改选）"),this);auto* form=new QFormLayout(connectionBox);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_target=new QComboBox(this);m_target->setObjectName("tplusTargetServer");
    SqlTargetSelector::populate(m_target);
    m_authDescription=new QLabel(QString::fromUtf8(u8"Windows 身份验证"),this);
    m_driver=new QComboBox(this);m_driver->setObjectName("tplusDriver");m_driver->addItems(TPlusBackupRestorer::availableDrivers());
    m_trust=new QCheckBox(QString::fromUtf8(u8"信任服务器证书（适用于本机或自签名证书）"),this);m_trust->setChecked(true);
    form->addRow(QString::fromUtf8(u8"恢复到"),SqlTargetSelector::control(m_target));form->addRow(QString::fromUtf8(u8"验证方式"),m_authDescription);
    form->addRow(QString::fromUtf8(u8"ODBC 驱动"),m_driver);form->addRow(m_trust);
    m_testButton=new QPushButton(QString::fromUtf8(u8"测试连接"),this);m_testButton->setObjectName("tplusTestConnection");form->addRow(m_testButton);
    m_compatibility=new QLabel(QString::fromUtf8(u8"读取备份后显示来源版本、兼容版本和自动选择的实例。"),this);
    m_compatibility->setObjectName("tplusCompatibility");m_compatibility->setWordWrap(true);m_compatibility->setTextFormat(Qt::PlainText);

    auto* sourceRow=new QHBoxLayout();m_source=tplusEdit(this,"tplusSource",QString::fromUtf8(u8"选择或拖入备份包"));m_sourceButton=new QPushButton(QString::fromUtf8(u8"选择备份"),this);
    sourceRow->addWidget(new QLabel(QString::fromUtf8(u8"备份包"),this));sourceRow->addWidget(m_source,1);sourceRow->addWidget(m_sourceButton);layout->addLayout(sourceRow);
    auto* stagingRow=new QHBoxLayout();m_staging=tplusEdit(this,"tplusStaging",QString::fromUtf8(u8"本机留空使用 D:\\Backup；远程填写 \\\\服务器\\共享目录"));m_stageButton=new QPushButton(QString::fromUtf8(u8"选择暂存目录"),this);
    m_staging->setText(QDir::toNativeSeparators(TPlusBackupRestorer::defaultLocalStagingDirectory()));
    stagingRow->addWidget(new QLabel(QString::fromUtf8(u8"备份暂存"),this));stagingRow->addWidget(m_staging,1);stagingRow->addWidget(m_stageButton);layout->addLayout(stagingRow);
    auto* stagingHint=new QLabel(QString::fromUtf8(u8"所有实例共用此暂存目录；修改用于下次读取。暂存目录需允许当前 Windows 用户写入、SQL Server 服务账号读取。远程服务器请使用双方可访问的 UNC 共享目录；SQL 登录密码不用于访问共享文件夹。"),this);
    stagingHint->setWordWrap(true);layout->addWidget(stagingHint);
    m_remoteAgent=new QCheckBox(QString::fromUtf8(u8"使用远端传输助手（原生压缩备份通常无需再次压缩）"),this);
    m_remoteAgent->setObjectName("tplusRemoteAgent");m_remoteAgent->setChecked(false);layout->addWidget(m_remoteAgent);
    m_analyzeButton=new QPushButton(QString::fromUtf8(u8"1. 读取 T+ 数据库清单"),this);m_analyzeButton->setObjectName("tplusAnalyze");m_analyzeButton->setProperty("flatGreen",true);layout->addWidget(m_analyzeButton);
    layout->addWidget(m_compatibility);layout->addWidget(connectionBox);
    m_table=new QTableWidget(0,4,this);m_table->setObjectName("tplusBackupSets");m_table->setHorizontalHeaderLabels({QString::fromUtf8(u8"恢复"),QString::fromUtf8(u8"原数据库 / 类型"),QString::fromUtf8(u8"备份时间"),QString::fromUtf8(u8"目标数据库名（可修改）")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);m_table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Fixed);m_table->setColumnWidth(0,72);
    m_table->setMinimumHeight(190);m_table->setAcceptDrops(false);m_table->viewport()->setAcceptDrops(false);layout->addWidget(m_table);
    auto* paths=new QFormLayout();paths->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_dataDirectory=tplusEdit(this,"tplusDataDirectory",QString::fromUtf8(u8"读取备份后自动填入服务器默认数据目录"));m_logDirectory=tplusEdit(this,"tplusLogDirectory",QString::fromUtf8(u8"服务器上的目录，不是本机上传路径"));
    paths->addRow(QString::fromUtf8(u8"服务器数据目录"),m_dataDirectory);paths->addRow(QString::fromUtf8(u8"服务器日志目录"),m_logDirectory);layout->addLayout(paths);
    auto* note=new QLabel(QString::fromUtf8(u8"恢复业务库后自动创建或复用 UFTSystem 控制库并登记账套名称、启用年月和版本。重名时修改目标库名（保留 UFTData 前缀）；同名库拒绝覆盖，已有控制表不改结构。控制库用于导账采集，不用于启动 T+ 软件。"),this);note->setWordWrap(true);layout->addWidget(note);
    m_progress=new QProgressBar(this);m_progress->setRange(0,1);m_progress->setValue(0);layout->addWidget(m_progress);
    m_log=new QPlainTextEdit(this);m_log->setObjectName("tplusRestoreLog");m_log->setReadOnly(true);m_log->setAcceptDrops(false);m_log->viewport()->setAcceptDrops(false);m_log->setMinimumHeight(160);layout->addWidget(m_log);
    auto* actions=new QHBoxLayout();m_backButton=new QPushButton(QString::fromUtf8(u8"返回"),this);m_stopButton=new QPushButton(QString::fromUtf8(u8"取消读取"),this);
    m_stopButton->setObjectName("tplusStop");m_restoreButton=new QPushButton(QString::fromUtf8(u8"3. 恢复所选数据库"),this);m_restoreButton->setObjectName("tplusRestore");m_restoreButton->setProperty("flatGreen",true);
    actions->addWidget(m_backButton);actions->addStretch();actions->addWidget(m_stopButton);actions->addWidget(m_restoreButton);layout->addLayout(actions);
    for(auto* button:{m_inputButton,m_sourceButton,m_stageButton,m_testButton,m_backButton,m_stopButton})button->setProperty("grayButton",true);
    connect(this,&TPlusRestoreWidget::operationLog,m_log,&QPlainTextEdit::appendPlainText);
    connect(m_target,QOverload<int>::of(&QComboBox::currentIndexChanged),this,&TPlusRestoreWidget::targetChanged);
    connect(m_source,&QLineEdit::textChanged,this,[this](){invalidateAnalysis();});
    connect(m_driver,&QComboBox::currentTextChanged,this,&TPlusRestoreWidget::targetChanged);
    connect(m_trust,&QCheckBox::toggled,this,&TPlusRestoreWidget::targetChanged);
    connect(m_inputButton,&QPushButton::clicked,this,[this](){
        const QString path=QFileDialog::getExistingDirectory(this,QString::fromUtf8(u8"选择备份包目录"),RestorePreferences::backupDirectory());
        if(!path.isEmpty()){RestorePreferences::setBackupDirectory(path);refreshSharedInput();}
    });
    connect(m_sourceButton,&QPushButton::clicked,this,[this](){
        const QString path=QFileDialog::getOpenFileName(this,QString::fromUtf8(u8"选择 T+ 备份"),RestorePreferences::backupDirectory(),QString::fromUtf8(u8"T+ 备份 (*.zip *.ZIP)"));
        if(!path.isEmpty()){m_source->setText(QDir::toNativeSeparators(path));RestorePreferences::setBackupDirectory(QFileInfo(path).absolutePath());refreshSharedInput();}
    });
    connect(m_stageButton,&QPushButton::clicked,this,[this](){
        const QString path=QFileDialog::getExistingDirectory(this,QString::fromUtf8(u8"选择 SQL Server 可读取的备份暂存目录"),m_staging->text());
        if(!path.isEmpty())m_staging->setText(QDir::toNativeSeparators(path));
    });
    connect(m_testButton,&QPushButton::clicked,this,&TPlusRestoreWidget::testConnection);
    connect(m_analyzeButton,&QPushButton::clicked,this,&TPlusRestoreWidget::analyzeBackup);
    connect(m_restoreButton,&QPushButton::clicked,this,&TPlusRestoreWidget::restoreSelected);
    connect(m_stopButton,&QPushButton::clicked,this,[this](){if(m_stop)m_stop->store(true);m_stopButton->setEnabled(false);m_log->appendPlainText(QString::fromUtf8(u8"已请求停止；正在执行的数据库恢复会完成后停止后续任务。"));});
    connect(m_backButton,&QPushButton::clicked,this,&TPlusRestoreWidget::backToMain);
    RestorePreferences::bindStaging(m_staging);
    connect(m_source,&QLineEdit::editingFinished,this,[this](){const QFileInfo file(m_source->text().trimmed());if(file.isFile()){RestorePreferences::setBackupDirectory(file.absolutePath());refreshSharedInput();}});
    refreshSharedInput();setBusy(false);
}
TPlusRestoreWidget::~TPlusRestoreWidget()
{
    if(m_worker){m_stop->store(true);m_worker->wait();}
}
void TPlusRestoreWidget::refreshSharedInput()
{m_sharedInput->setText(QString::fromUtf8(u8"备份包目录：%1").arg(QDir::toNativeSeparators(RestorePreferences::backupDirectory())));}
void TPlusRestoreWidget::showEvent(QShowEvent* event){QWidget::showEvent(event);refreshSharedInput();}
T3SqlConnection TPlusRestoreWidget::connectionOptions() const
{
    T3SqlConnection options;options.server=m_target->currentData().toString();options.windowsAuthentication=!SqlTargetSelector::isRemote(m_target);
    if(!options.windowsAuthentication){SqlServerCredentials::apply211(options);}
    options.driver=m_driver->currentText();options.trustServerCertificate=m_trust->isChecked();return options;
}
void TPlusRestoreWidget::invalidateAnalysis()
{
    if(m_worker)return;
    m_compatibility->setText(QString::fromUtf8(u8"读取备份后显示来源版本、兼容版本和自动选择的实例。"));
    m_analysis=TPlusBackupAnalysis();m_table->setRowCount(0);m_restoreButton->setEnabled(false);
}
void TPlusRestoreWidget::setBusy(bool busy,bool restoring)
{
    setProperty("sessionBusysetBusy", busy);
    for(QWidget* widget:QList<QWidget*>{m_target,m_driver,m_source,m_staging,m_trust,
        m_inputButton,m_sourceButton,m_stageButton,m_testButton,m_analyzeButton,m_table,m_dataDirectory,m_logDirectory})widget->setEnabled(!busy);
    m_testButton->setEnabled(!busy&&SqlTargetSelector::hasTarget(m_target));
    m_analyzeButton->setEnabled(!busy);
    m_remoteAgent->setEnabled(!busy&&SqlTargetSelector::isRemote(m_target));
    m_restoreButton->setEnabled(!busy&&m_analysis.success);m_stopButton->setEnabled(busy);m_backButton->setEnabled(!busy);
    m_stopButton->setText(restoring?QString::fromUtf8(u8"停止后续恢复"):QString::fromUtf8(u8"取消读取"));
    m_progress->setRange(0,busy?0:1);setAcceptDrops(!busy);
}
void TPlusRestoreWidget::targetChanged()
{
    if(m_worker)return;
    m_analysis.success=false;
    m_dataDirectory->clear();m_logDirectory->clear();
    m_authDescription->setText(SqlTargetSelector::isRemote(m_target)?QString::fromUtf8(u8"内置 SQL Server 账号（sa）"):QString::fromUtf8(u8"Windows 身份验证"));
    setBusy(false);
    if(!m_analysis.preparedPath.isEmpty()&&!m_analysis.sets.isEmpty())testConnection();
}
void TPlusRestoreWidget::testConnection()
{
    if(m_worker)return;
    const auto options=connectionOptions();const auto original=static_cast<const T3BackupAnalysis&>(m_analysis);
    const bool reuse=!original.preparedPath.isEmpty()&&!original.sets.isEmpty();
    const auto result=std::make_shared<T3BackupAnalysis>();
    m_stop=std::make_shared<std::atomic_bool>(false);const auto stop=m_stop;
    if(reuse)m_analysis.success=false;
    setBusy(true);m_log->appendPlainText(reuse?QString::fromUtf8(u8"正在校验目标实例，保留备份清单，不重新读取备份包..."):QString::fromUtf8(u8"正在测试连接..."));
    m_worker=QThread::create([options,original,result,reuse,stop](){
        try{
            if(reuse)*result=SqlRestoreRouting::validateTarget(original,options,[stop](){return stop->load();});
            else result->success=T3BackupRestorer::testConnection(options,&result->server,&result->error);
        }
        catch(...){result->success=false;result->error=QString::fromUtf8(u8"目标实例校验发生异常，备份清单已保留。");}
    });
    m_worker->setParent(this);
    connect(m_worker,&QThread::finished,this,[this,result,reuse,stop](){
        m_worker=nullptr;
        if(stop->load()){result->success=false;result->error=QString::fromUtf8(u8"已取消目标校验，备份清单已保留。");}
        if(reuse){
            m_analysis.success=result->success;m_analysis.error=result->error;
            m_analysis.server=result->server;m_analysis.targetServer=result->targetServer;
            m_analysis.compatibilityMessage=result->compatibilityMessage;
            m_compatibility->setText(result->success?result->compatibilityMessage:SqlBackupCompatibility::summary(m_analysis.sets)+"\n"+result->error);
            if(result->success){m_dataDirectory->setText(result->server.dataDirectory);m_logDirectory->setText(result->server.logDirectory);}
            else{m_dataDirectory->clear();m_logDirectory->clear();}
            SqlTargetSelector::updateVersionLabels(m_target);
        }
        setBusy(false);
        m_log->appendPlainText(result->success?QString::fromUtf8(u8"连接成功：%1 / SQL Server %2").arg(result->server.name,result->server.version):QString::fromUtf8(u8"目标校验失败：%1").arg(result->error));emit operationFinished(result->success);
    });
    connect(m_worker,&QThread::finished,m_worker,&QObject::deleteLater);m_worker->start();
}
void TPlusRestoreWidget::analyzeBackup()
{
    if(m_worker)return;invalidateAnalysis();
    QString error;const QString source=TPlusBackupRestorer::resolveSource(m_source->text().trimmed(),&error);
    if(source.isEmpty()){m_log->appendPlainText(error);return;}
    const auto options=connectionOptions();const QString staging=m_staging->text().trimmed();const bool useAgent=SqlTargetSelector::isRemote(m_target)&&m_remoteAgent->isChecked();
    QStringList locals;for(int i=0;i<m_target->count();++i)locals.append(m_target->itemData(i).toString());
    const auto candidates=SqlRestoreRouting::candidates(options,locals);
    const auto versions=SqlRestoreRouting::cachedVersions(candidates);
    QMap<QString,QString> stages;for(const auto& candidate:candidates)stages.insert(candidate.server,staging);
    const auto result=std::make_shared<TPlusBackupAnalysis>();m_stop=std::make_shared<std::atomic_bool>(false);const auto stop=m_stop;
    m_log->clear();setBusy(true);
    m_worker=QThread::create([this,candidates,versions,source,stages,result,stop,useAgent](){
        try{*result=SqlRestoreRouting::read<TPlusBackupAnalysis>(candidates,source,stages,useAgent,TPlusBackupRestorer::analyze,[stop](){return stop->load();},[this](const QString& message){emit operationLog(message);},T3BackupRestorer::testConnection,versions);}
        catch(...){result->error=QString::fromUtf8(u8"读取备份失败：发生未预期的异常。");}
    });m_worker->setParent(this);
    connect(m_worker,&QThread::finished,this,[this,result](){
        m_worker=nullptr;SqlTargetSelector::updateVersionLabels(m_target);
        if(result->success && m_target->currentData().toString()!=result->targetServer){
            const int index=m_target->findData(result->targetServer);
            if(index>=0)m_target->setCurrentIndex(index);
            else{result->success=false;result->error=QString::fromUtf8(u8"匹配实例已不在列表中，请重新扫描并读取。");}
        }
        m_analysis=*result;m_compatibility->setText(result->compatibilityMessage);
        if(result->success)
        {
            m_dataDirectory->setText(result->server.dataDirectory);m_logDirectory->setText(result->server.logDirectory);
            m_table->setRowCount(result->sets.size());
            for(int row=0;row<result->sets.size();++row)
            {
                const auto& set=result->sets.at(row);auto* check=new QTableWidgetItem();check->setFlags(Qt::ItemIsEnabled|Qt::ItemIsUserCheckable);check->setCheckState(Qt::Checked);m_table->setItem(row,0,check);
                for(int col=1;col<=3;++col)
                {auto* item=new QTableWidgetItem(col==2?set.backupDate:set.databaseName);if(col!=3)item->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable);m_table->setItem(row,col,item);}
            }
            m_log->appendPlainText(QString::fromUtf8(u8"已读取 %1 个 T+ 业务库备份。请核对目标库名和目录；恢复时自动登记 UFTSystem 控制库。\n暂存文件：%2").arg(result->sets.size()).arg(QDir::toNativeSeparators(result->preparedPath)));
        }
        else m_log->appendPlainText(QString::fromUtf8(u8"读取失败：%1").arg(result->error));
        setBusy(false);m_progress->setValue(result->success?1:0);emit operationFinished(result->success);
    });connect(m_worker,&QThread::finished,m_worker,&QObject::deleteLater);m_worker->start();
}
void TPlusRestoreWidget::restoreSelected()
{
    if(m_worker||!m_analysis.success)return;
    QVector<T3RestoreSelection> selected;
    for(int row=0;row<m_table->rowCount();++row)
        if(m_table->item(row,0)&&m_table->item(row,0)->checkState()==Qt::Checked&&m_table->item(row,3))
            selected.append({m_analysis.sets.at(row),m_table->item(row,3)->text()});
    const QString error=TPlusBackupRestorer::validateSelections(selected);if(!error.isEmpty()){m_log->appendPlainText(error);return;}
    const auto options=connectionOptions();const auto analysis=m_analysis;const QString data=m_dataDirectory->text().trimmed(),logs=m_logDirectory->text().trimmed();
    if(data.isEmpty()||logs.isEmpty()){m_log->appendPlainText(QString::fromUtf8(u8"请填写服务器上的数据目录和日志目录。"));return;}
    auto result=std::make_shared<T3RestoreResult>();m_stop=std::make_shared<std::atomic_bool>(false);const auto stop=m_stop;setBusy(true,true);
    m_worker=QThread::create([this,options,analysis,selected,data,logs,result,stop](){
        try{*result=TPlusBackupRestorer::restore(options,analysis,selected,data,logs,[stop](){return stop->load();},[this](const QString& message){emit operationLog(message);});}
        catch(...){result->error=QString::fromUtf8(u8"恢复发生异常。请检查服务器数据库状态，已完成的数据库予以保留。");}
    });m_worker->setParent(this);
    connect(m_worker,&QThread::finished,this,[this,result,selected](){
        m_worker=nullptr;setBusy(false);int count=0;for(const auto& item:result->items)if(item.success)++count;
        const bool ok=result->error.isEmpty()&&!result->stopped&&count==selected.size();m_progress->setValue(ok?1:0);
        m_log->appendPlainText(QString::fromUtf8(u8"恢复结束：完整成功 %1 / 选择 %2。%3").arg(count).arg(selected.size()).arg(result->stopped?QString::fromUtf8(u8"已停止后续任务。"):QString()));
        if(!ok){ QStringList restored; for(const auto& item:result->items)if(item.databaseRestored)restored.append(item.database); if(!restored.isEmpty())m_log->appendPlainText(QString::fromUtf8(u8"已恢复并保留的数据库：%1；整组登记尚未完成，请查看上方日志。").arg(restored.join(", "))); }
        if(!result->error.isEmpty())m_log->appendPlainText(result->error);
        emit operationFinished(ok);
    });connect(m_worker,&QThread::finished,m_worker,&QObject::deleteLater);m_worker->start();
}
QString TPlusRestoreWidget::droppedSource(const QMimeData* data) const
{
    if(!data||!data->hasUrls()||data->urls().size()!=1||!data->urls().first().isLocalFile())return {};
    QString error;return TPlusBackupRestorer::resolveSource(data->urls().first().toLocalFile(),&error);
}
void TPlusRestoreWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if(!m_worker&&event->possibleActions().testFlag(Qt::CopyAction)&&!droppedSource(event->mimeData()).isEmpty())event->acceptProposedAction();else event->ignore();
}
void TPlusRestoreWidget::dropEvent(QDropEvent* event)
{
    if(m_worker||!event->possibleActions().testFlag(Qt::CopyAction)){event->ignore();return;}
    const QString source=droppedSource(event->mimeData());if(source.isEmpty()){event->ignore();return;}
    const QString retained=RepairInputFileUtils::copyDroppedFile(source,this);if(retained.isEmpty()){event->ignore();return;}
    m_source->setText(QDir::toNativeSeparators(retained));event->setDropAction(Qt::CopyAction);event->accept();
}
