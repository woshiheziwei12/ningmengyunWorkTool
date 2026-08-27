#include "subjectmergewidget.h"

#include "Common/globaldata.h"
#include "Common/repairfilenameutils.h"
#include "Common/repairinputfilepickerwidget.h"
#include "Common/repairpathconfigwidget.h"
#include "Common/repairpathsettings.h"
#include "Common/subjectmergeengine.h"

#include <algorithm>
#include <functional>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QSharedPointer>
#include <QSignalBlocker>
#include <QTextEdit>
#include <QThread>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
struct SubjectMergeAnalysis
{
    bool ok = false;
    QString inputPath;
    QString jsonPath;
    QString accountSetName;
    QString errorMessage;
    QJsonObject root;
    QList<SubjectMergeNode> nodes;
};

struct SubjectMergeFinalization
{
    bool ok = false;
    QString outputZipPath;
    QString mappingPath;
    QString errorMessage;
};

struct MergeDialogSnapshot
{
    QJsonObject root;
    QMap<QString, QString> mapping;
    QStringList logs;
    int changedVoucherCount = 0;
    int changedBalanceCount = 0;
    int reversedOpeningCount = 0;
};

bool supportedInput(const QString &path)
{
    const QFileInfo info(path);
    return info.isFile()
        && (info.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0
            || info.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0);
}

bool hasSupportedInput(const QMimeData *mimeData)
{
    if (!mimeData || !mimeData->hasUrls())
        return false;
    for (const QUrl &url : mimeData->urls())
    {
        if (url.isLocalFile() && supportedInput(url.toLocalFile()))
            return true;
    }
    return false;
}

QString codeFromValue(const QJsonValue &value)
{
    if (value.isString())
        return value.toString().trimmed();
    if (value.isDouble())
        return QString::number(static_cast<qint64>(value.toDouble()));
    return QString();
}

bool runPowerShell(const QString &script,
                   const QProcessEnvironment &environment,
                   QString *errorMessage)
{
    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral("powershell.exe"),
                  QStringList() << QStringLiteral("-NoProfile")
                                << QStringLiteral("-ExecutionPolicy")
                                << QStringLiteral("Bypass")
                                << QStringLiteral("-Command")
                                << script);
    if (!process.waitForFinished(120000))
    {
        process.kill();
        process.waitForFinished(3000);
        if (errorMessage)
            *errorMessage = QStringLiteral("处理 ZIP 文件超时");
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        const QString error = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (errorMessage)
            *errorMessage = error.isEmpty() ? QStringLiteral("处理 ZIP 文件失败") : error;
        return false;
    }
    return true;
}

bool extractAccountSetJson(const QString &zipPath,
                           const QString &jsonPath,
                           QString *errorMessage)
{
    const QString script =
        QStringLiteral("$ErrorActionPreference='Stop';")
        + QStringLiteral("Add-Type -AssemblyName System.IO.Compression;")
        + QStringLiteral("Add-Type -AssemblyName System.IO.Compression.FileSystem;")
        + QStringLiteral("$archive=[System.IO.Compression.ZipFile]::OpenRead($env:LEMON_MERGE_ZIP_IN);")
        + QStringLiteral("try{$entry=$archive.Entries|Where-Object{[System.IO.Path]::GetFileName($_.FullName)-ieq 'AccountSet.json'}|Select-Object -First 1;")
        + QStringLiteral("if($null-eq $entry){throw 'AccountSet.json not found in ZIP'};")
        + QStringLiteral("[System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$env:LEMON_MERGE_JSON_OUT,$true)}finally{$archive.Dispose()}");
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LEMON_MERGE_ZIP_IN"), QFileInfo(zipPath).absoluteFilePath());
    environment.insert(QStringLiteral("LEMON_MERGE_JSON_OUT"), QFileInfo(jsonPath).absoluteFilePath());
    return runPowerShell(script, environment, errorMessage);
}

bool createZip(const QString &directory,
               const QString &zipPath,
               QString *errorMessage)
{
    const QString script =
        QStringLiteral("$ErrorActionPreference='Stop';")
        + QStringLiteral("Add-Type -AssemblyName System.IO.Compression;")
        + QStringLiteral("Add-Type -AssemblyName System.IO.Compression.FileSystem;")
        + QStringLiteral("[System.IO.Compression.ZipFile]::CreateFromDirectory($env:LEMON_MERGE_PACKAGE_DIR,$env:LEMON_MERGE_ZIP_OUT,[System.IO.Compression.CompressionLevel]::Optimal,$false)");
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LEMON_MERGE_PACKAGE_DIR"),
                       QFileInfo(directory).absoluteFilePath());
    environment.insert(QStringLiteral("LEMON_MERGE_ZIP_OUT"),
                       QFileInfo(zipPath).absoluteFilePath());
    return runPowerShell(script, environment, errorMessage);
}

QString uniqueFilePath(const QString &directory,
                       const QString &baseName,
                       const QString &suffix)
{
    QString path = QDir(directory).absoluteFilePath(baseName + suffix);
    int index = 1;
    while (QFile::exists(path))
        path = QDir(directory).absoluteFilePath(
            baseName + QStringLiteral("_%1").arg(index++) + suffix);
    return path;
}

SubjectMergeAnalysis analyzeInput(const QString &inputPath,
                                  const QString &workRoot)
{
    SubjectMergeAnalysis result;
    result.inputPath = inputPath;
    if (!supportedInput(inputPath))
    {
        result.errorMessage = QStringLiteral("请选择存在的 AccountSet.json 或 ZIP 文件");
        return result;
    }

    const QFileInfo inputInfo(inputPath);
    const QString jobName = RepairFileNameUtils::safePart(inputInfo.completeBaseName())
        + QLatin1Char('_')
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmsszzz"));
    const QString packageDir = QDir(workRoot).absoluteFilePath(jobName + QStringLiteral("/package"));
    if (!QDir().mkpath(packageDir))
    {
        result.errorMessage = QStringLiteral("创建科目合并工作目录失败");
        return result;
    }
    result.jsonPath = QDir(packageDir).absoluteFilePath(QStringLiteral("AccountSet.json"));

    if (inputInfo.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0)
    {
        if (!extractAccountSetJson(inputPath, result.jsonPath, &result.errorMessage))
            return result;
    }
    else if (!QFile::copy(inputInfo.absoluteFilePath(), result.jsonPath))
    {
        result.errorMessage = QStringLiteral("复制 AccountSet.json 到工作目录失败");
        return result;
    }

    QFile file(result.jsonPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        result.errorMessage = QStringLiteral("读取 AccountSet.json 失败：") + file.errorString();
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        result.errorMessage = QStringLiteral("解析 AccountSet.json 失败：") + parseError.errorString();
        return result;
    }
    result.root = document.object();
    if (!SubjectMergeEngine::buildTree(result.root, &result.nodes, &result.errorMessage))
        return result;
    result.accountSetName = result.root.value(QStringLiteral("AccountSet")).toObject()
        .value(QStringLiteral("AsName")).toString().trimmed();
    result.ok = true;
    return result;
}

QString csvEscape(QString text)
{
    text.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + text + QLatin1Char('"');
}

bool writeMapping(const QString &path,
                  const QJsonObject &originalRoot,
                  const QJsonObject &mergedRoot,
                  const QMap<QString, QString> &mapping,
                  QString *errorMessage)
{
    QMap<QString, QString> originalNames;
    QMap<QString, QString> mergedNames;
    for (const QJsonValue &value : originalRoot.value(QStringLiteral("GLAccountList")).toArray())
    {
        const QJsonObject account = value.toObject();
        originalNames.insert(codeFromValue(account.value(QStringLiteral("AcctCode"))),
                             account.value(QStringLiteral("AcctName")).toString());
    }
    for (const QJsonValue &value : mergedRoot.value(QStringLiteral("GLAccountList")).toArray())
    {
        const QJsonObject account = value.toObject();
        mergedNames.insert(codeFromValue(account.value(QStringLiteral("AcctCode"))),
                           account.value(QStringLiteral("AcctName")).toString());
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("创建科目合并映射清单失败：") + file.errorString();
        return false;
    }
    file.write("\xEF\xBB\xBF");
    file.write("SourceCode,SourceName,TargetCode,TargetName\n");
    for (auto it = mapping.constBegin(); it != mapping.constEnd(); ++it)
    {
        if (it.key() == it.value())
            continue;
        const QString line = QStringLiteral("%1,%2,%3,%4\n")
            .arg(csvEscape(it.key()), csvEscape(originalNames.value(it.key())),
                 csvEscape(it.value()), csvEscape(mergedNames.value(it.value())));
        if (file.write(line.toUtf8()) < 0)
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("写入科目合并映射清单失败：") + file.errorString();
            return false;
        }
    }
    if (!file.commit())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("保存科目合并映射清单失败：") + file.errorString();
        return false;
    }
    return true;
}

SubjectMergeFinalization finalizeOutput(const SubjectMergeAnalysis &analysis,
                                        const QJsonObject &mergedRoot,
                                        const QMap<QString, QString> &mapping,
                                        const QString &outputRoot)
{
    SubjectMergeFinalization result;
    QSaveFile jsonFile(analysis.jsonPath);
    if (!jsonFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        result.errorMessage = QStringLiteral("写入合并后的 AccountSet.json 失败：")
            + jsonFile.errorString();
        return result;
    }
    const QByteArray content = QJsonDocument(mergedRoot).toJson(QJsonDocument::Compact);
    if (jsonFile.write(content) != content.size() || !jsonFile.commit())
    {
        result.errorMessage = QStringLiteral("保存合并后的 AccountSet.json 失败：")
            + jsonFile.errorString();
        return result;
    }
    if (!QDir().mkpath(outputRoot))
    {
        result.errorMessage = QStringLiteral("创建输出目录失败");
        return result;
    }

    const QString baseName = RepairFileNameUtils::makeBaseName(
        analysis.accountSetName,
        QFileInfo(analysis.inputPath).completeBaseName(),
        QStringLiteral("科目合并"));
    result.outputZipPath = uniqueFilePath(outputRoot, baseName, QStringLiteral(".zip"));
    result.mappingPath = uniqueFilePath(outputRoot,
                                        baseName + QStringLiteral("_科目映射"),
                                        QStringLiteral(".csv"));
    if (!writeMapping(result.mappingPath, analysis.root, mergedRoot, mapping,
                      &result.errorMessage))
    {
        return result;
    }
    if (!createZip(QFileInfo(analysis.jsonPath).absolutePath(),
                   result.outputZipPath, &result.errorMessage))
    {
        return result;
    }
    result.ok = true;
    return result;
}

class SubjectMergeDialog : public QDialog
{
public:
    explicit SubjectMergeDialog(const SubjectMergeAnalysis &analysis,
                                QWidget *parent = nullptr)
        : QDialog(parent),
          m_root(analysis.root),
          m_originalAccountCount(analysis.nodes.size()),
          m_filter(new QLineEdit(this)),
          m_tree(new QTreeWidget(this)),
          m_selectionLabel(new QLabel(this)),
          m_targetCombo(new QComboBox(this)),
          m_newCode(new QLineEdit(this)),
          m_newName(new QLineEdit(this)),
          m_directionCombo(new QComboBox(this)),
          m_mergeButton(new QPushButton(QStringLiteral("合并勾选科目"), this)),
          m_undoButton(new QPushButton(QStringLiteral("撤销上一次合并"), this)),
          m_history(new QTextEdit(this)),
          m_buttonBox(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                          Qt::Horizontal, this))
    {
        setWindowTitle(QStringLiteral("选择需要合并的会计科目"));
        resize(1060, 760);
        setMinimumSize(780, 580);

        QVBoxLayout *layout = new QVBoxLayout(this);
        QLabel *description = new QLabel(
            QStringLiteral("勾选至少两个父级相同的科目，选择现有目标或新建目标；"
                           "合并后可继续勾选其他科目，最后统一生成新数据包。"), this);
        description->setWordWrap(true);
        layout->addWidget(description);

        m_filter->setPlaceholderText(QStringLiteral("按科目编码或名称搜索，父级会自动保留显示"));
        m_filter->setClearButtonEnabled(true);
        layout->addWidget(m_filter);

        m_tree->setColumnCount(9);
        m_tree->setHeaderLabels(QStringList()
            << QStringLiteral("科目编码") << QStringLiteral("科目名称")
            << QStringLiteral("父级编码") << QStringLiteral("余额方向")
            << QStringLiteral("层级") << QStringLiteral("子树科目数")
            << QStringLiteral("本科目期初") << QStringLiteral("子树期初")
            << QStringLiteral("子树凭证"));
        m_tree->setRootIsDecorated(true);
        m_tree->setAlternatingRowColors(true);
        m_tree->setUniformRowHeights(true);
        m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        m_tree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        m_tree->header()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
        m_tree->header()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
        m_tree->header()->setSectionResizeMode(7, QHeaderView::ResizeToContents);
        m_tree->header()->setSectionResizeMode(8, QHeaderView::ResizeToContents);
        layout->addWidget(m_tree, 1);

        m_selectionLabel->setWordWrap(true);
        layout->addWidget(m_selectionLabel);

        QFormLayout *form = new QFormLayout();
        form->addRow(QStringLiteral("目标科目："), m_targetCombo);
        form->addRow(QStringLiteral("新科目编码："), m_newCode);
        form->addRow(QStringLiteral("新科目名称："), m_newName);
        m_directionCombo->addItem(QStringLiteral("借方"), 1);
        m_directionCombo->addItem(QStringLiteral("贷方"), -1);
        form->addRow(QStringLiteral("新科目方向："), m_directionCombo);
        layout->addLayout(form);

        QHBoxLayout *actions = new QHBoxLayout();
        actions->addWidget(m_mergeButton);
        actions->addWidget(m_undoButton);
        actions->addStretch();
        layout->addLayout(actions);

        m_history->setReadOnly(true);
        m_history->setMaximumHeight(140);
        m_history->setPlaceholderText(QStringLiteral("合并记录会显示在这里"));
        layout->addWidget(m_history);

        m_buttonBox->button(QDialogButtonBox::Ok)->setText(QStringLiteral("生成合并后的 ZIP"));
        m_buttonBox->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        layout->addWidget(m_buttonBox);

        connect(m_filter, &QLineEdit::textChanged, this,
                [this](const QString &text) { filterTree(text); });
        connect(m_tree, &QTreeWidget::itemChanged, this,
                [this](QTreeWidgetItem *item, int column) { onItemChanged(item, column); });
        connect(m_targetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int) { updateTargetControls(); });
        connect(m_mergeButton, &QPushButton::clicked, this,
                [this]() { applyMerge(); });
        connect(m_undoButton, &QPushButton::clicked, this,
                [this]() { undoMerge(); });
        connect(m_buttonBox, &QDialogButtonBox::accepted, this,
                [this]() { acceptIfMerged(); });
        connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

        rebuildTree();
    }

    QJsonObject mergedRoot() const { return m_root; }
    QMap<QString, QString> mapping() const { return m_mapping; }
    QStringList logs() const { return m_logs; }
    int changedVoucherCount() const { return m_changedVoucherCount; }
    int changedBalanceCount() const { return m_changedBalanceCount; }
    int reversedOpeningCount() const { return m_reversedOpeningCount; }
    int operationCount() const { return m_snapshots.size(); }

private:
    void rebuildTree()
    {
        QString error;
        QList<SubjectMergeNode> nodes;
        if (!SubjectMergeEngine::buildTree(m_root, &nodes, &error))
        {
            QMessageBox::warning(this, QStringLiteral("科目层级识别失败"), error);
            return;
        }
        m_nodes = nodes;
        m_nodeByCode.clear();
        QMap<QString, int> balanceCounts;
        QMap<QString, int> voucherCounts;
        QMap<QString, QStringList> childrenByParent;
        for (const SubjectMergeNode &node : nodes)
            childrenByParent[node.parentCode].append(node.code);
        for (const QJsonValue &value : m_root.value(QStringLiteral("GLBalanceList")).toArray())
        {
            const QString code = codeFromValue(value.toObject().value(QStringLiteral("AcctCode")));
            balanceCounts.insert(code, balanceCounts.value(code) + 1);
        }
        for (const QJsonValue &value : m_root.value(QStringLiteral("GLVoucherList")).toArray())
        {
            const QString code = codeFromValue(value.toObject().value(QStringLiteral("AcctCode")));
            voucherCounts.insert(code, voucherCounts.value(code) + 1);
        }

        m_subtreeSubjectCounts.clear();
        m_subtreeBalanceCounts.clear();
        m_subtreeVoucherCounts.clear();
        std::function<void(const QString &)> countSubtree =
            [this, &childrenByParent, &balanceCounts, &voucherCounts, &countSubtree](const QString &code) {
                int subjectCount = 1;
                int balanceCount = balanceCounts.value(code);
                int voucherCount = voucherCounts.value(code);
                for (const QString &childCode : childrenByParent.value(code))
                {
                    countSubtree(childCode);
                    subjectCount += m_subtreeSubjectCounts.value(childCode);
                    balanceCount += m_subtreeBalanceCounts.value(childCode);
                    voucherCount += m_subtreeVoucherCounts.value(childCode);
                }
                m_subtreeSubjectCounts.insert(code, subjectCount);
                m_subtreeBalanceCounts.insert(code, balanceCount);
                m_subtreeVoucherCounts.insert(code, voucherCount);
            };
        for (const QString &rootCode : childrenByParent.value(QString()))
            countSubtree(rootCode);

        const QSignalBlocker blocker(m_tree);
        m_tree->clear();
        QMap<QString, QTreeWidgetItem *> items;
        for (const SubjectMergeNode &node : nodes)
        {
            QTreeWidgetItem *item = nullptr;
            if (!node.parentCode.isEmpty() && items.contains(node.parentCode))
                item = new QTreeWidgetItem(items.value(node.parentCode));
            else
                item = new QTreeWidgetItem(m_tree);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(0, Qt::Unchecked);
            item->setText(0, node.code);
            item->setText(1, node.name);
            item->setText(2, node.parentCode.isEmpty()
                ? QStringLiteral("一级科目") : node.parentCode);
            item->setText(3, node.direction > 0 ? QStringLiteral("借") : QStringLiteral("贷"));
            item->setText(4, QString::number(node.level));
            item->setText(5, QString::number(m_subtreeSubjectCounts.value(node.code)));
            item->setText(6, QString::number(balanceCounts.value(node.code)));
            item->setText(7, QString::number(m_subtreeBalanceCounts.value(node.code)));
            item->setText(8, QString::number(m_subtreeVoucherCounts.value(node.code)));
            item->setData(0, Qt::UserRole, node.code);
            item->setToolTip(1, node.isLeaf
                ? QStringLiteral("末级科目") : QStringLiteral("非末级科目，可递归合并子科目"));
            items.insert(node.code, item);
            m_nodeByCode.insert(node.code, node);
        }
        m_tree->expandToDepth(0);
        updateSelection();
        filterTree(m_filter->text());
    }

    QStringList checkedCodes() const
    {
        QStringList result;
        QTreeWidgetItemIterator iterator(m_tree);
        while (*iterator)
        {
            if ((*iterator)->checkState(0) == Qt::Checked)
                result.append((*iterator)->data(0, Qt::UserRole).toString());
            ++iterator;
        }
        return result;
    }

    void onItemChanged(QTreeWidgetItem *item, int column)
    {
        if (!item || column != 0 || item->checkState(0) != Qt::Checked)
        {
            updateSelection();
            return;
        }
        const QString code = item->data(0, Qt::UserRole).toString();
        const QString parentCode = m_nodeByCode.value(code).parentCode;
        const QStringList codes = checkedCodes();
        for (const QString &selected : codes)
        {
            if (m_nodeByCode.value(selected).parentCode != parentCode)
            {
                const QSignalBlocker blocker(m_tree);
                item->setCheckState(0, Qt::Unchecked);
                QMessageBox::information(this, QStringLiteral("父级不同"),
                    QStringLiteral("只有父级相同的科目可以一起合并；一级科目之间可以合并。"));
                break;
            }
        }
        updateSelection();
    }

    void updateSelection()
    {
        const QStringList codes = checkedCodes();
        QString parent = QStringLiteral("尚未选择");
        if (!codes.isEmpty())
        {
            parent = m_nodeByCode.value(codes.first()).parentCode;
            if (parent.isEmpty())
                parent = QStringLiteral("一级科目（虚拟根节点）");
        }
        int subtreeSubjectCount = 0;
        int subtreeBalanceCount = 0;
        int subtreeVoucherCount = 0;
        for (const QString &code : codes)
        {
            subtreeSubjectCount += m_subtreeSubjectCounts.value(code);
            subtreeBalanceCount += m_subtreeBalanceCounts.value(code);
            subtreeVoucherCount += m_subtreeVoucherCounts.value(code);
        }
        m_selectionLabel->setText(
            QStringLiteral("已勾选 %1 个科目，父级：%2；涉及子树科目 %3 个、期初 %4 条、凭证 %5 条；当前共 %6 个科目，原始 %7 个")
                .arg(codes.size()).arg(parent).arg(subtreeSubjectCount)
                .arg(subtreeBalanceCount).arg(subtreeVoucherCount)
                .arg(m_nodes.size()).arg(m_originalAccountCount));

        const QString previous = m_targetCombo->currentData().toString();
        {
            const QSignalBlocker blocker(m_targetCombo);
            m_targetCombo->clear();
            for (const QString &code : codes)
            {
                const SubjectMergeNode node = m_nodeByCode.value(code);
                m_targetCombo->addItem(QStringLiteral("保留 %1  %2（%3方）")
                    .arg(node.code, node.name, node.direction > 0
                        ? QStringLiteral("借") : QStringLiteral("贷")), code);
            }
            m_targetCombo->addItem(QStringLiteral("新建同父级目标科目"),
                                   QStringLiteral("__new__"));
            const int previousIndex = m_targetCombo->findData(previous);
            if (previousIndex >= 0)
                m_targetCombo->setCurrentIndex(previousIndex);
        }
        m_mergeButton->setEnabled(codes.size() >= 2);
        m_undoButton->setEnabled(!m_snapshots.isEmpty());
        m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!m_snapshots.isEmpty());
        updateTargetControls();
    }

    void updateTargetControls()
    {
        const bool createNew = m_targetCombo->currentData().toString()
            == QStringLiteral("__new__");
        m_newCode->setEnabled(createNew);
        m_newName->setEnabled(createNew);
        m_directionCombo->setEnabled(createNew);
        if (!createNew)
            return;

        const QStringList codes = checkedCodes();
        if (codes.isEmpty())
            return;
        const SubjectMergeNode node = m_nodeByCode.value(codes.first());
        if (m_newCode->text().trimmed().isEmpty())
            m_newCode->setText(SubjectMergeEngine::suggestChildCode(m_nodes, node.parentCode));
        const int directionIndex = m_directionCombo->findData(node.direction);
        if (directionIndex >= 0)
            m_directionCombo->setCurrentIndex(directionIndex);
    }

    void filterTree(const QString &text)
    {
        const QString filter = text.trimmed();
        std::function<bool(QTreeWidgetItem *)> visit =
            [&visit, &filter](QTreeWidgetItem *item) {
                bool childVisible = false;
                for (int index = 0; index < item->childCount(); ++index)
                    childVisible = visit(item->child(index)) || childVisible;
                const bool matches = filter.isEmpty()
                    || item->text(0).contains(filter, Qt::CaseInsensitive)
                    || item->text(1).contains(filter, Qt::CaseInsensitive)
                    || item->checkState(0) == Qt::Checked;
                const bool visible = matches || childVisible;
                item->setHidden(!visible);
                if (!filter.isEmpty() && childVisible)
                    item->setExpanded(true);
                return visible;
            };
        for (int index = 0; index < m_tree->topLevelItemCount(); ++index)
            visit(m_tree->topLevelItem(index));
    }

    void applyMerge()
    {
        SubjectMergeRequest request;
        request.sourceCodes = checkedCodes();
        request.createTarget = m_targetCombo->currentData().toString()
            == QStringLiteral("__new__");
        request.targetCode = request.createTarget
            ? m_newCode->text().trimmed() : m_targetCombo->currentData().toString();
        request.targetName = m_newName->text().trimmed();
        request.targetDirection = m_directionCombo->currentData().toInt();

        SubjectMergeResult result;
        QString error;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool merged = SubjectMergeEngine::merge(m_root, request, &result, &error);
        QApplication::restoreOverrideCursor();
        if (!merged)
        {
            QMessageBox::warning(this, QStringLiteral("科目合并失败"), error);
            return;
        }

        const QString impact = QStringLiteral(
            "本次合并将产生以下影响：\n\n"
            "删除科目：%1 个\n"
            "新增承接科目：%2 个\n"
            "期初科目编码调整：%3 条\n"
            "凭证科目编码调整：%4 条\n"
            "期初金额因借贷方向相反而取反：%5 条\n\n"
            "“编码调整”只表示 AcctCode 变化，不等于金额变化。是否继续？")
                .arg(result.removedAccountCount)
                .arg(result.createdAccountCount)
                .arg(result.changedBalanceCount)
                .arg(result.changedVoucherCount)
                .arg(result.reversedOpeningCount);
        if (QMessageBox::question(this, QStringLiteral("确认科目合并影响"), impact,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes)
        {
            return;
        }

        MergeDialogSnapshot snapshot;
        snapshot.root = m_root;
        snapshot.mapping = m_mapping;
        snapshot.logs = m_logs;
        snapshot.changedVoucherCount = m_changedVoucherCount;
        snapshot.changedBalanceCount = m_changedBalanceCount;
        snapshot.reversedOpeningCount = m_reversedOpeningCount;
        m_snapshots.append(snapshot);

        for (auto it = m_mapping.begin(); it != m_mapping.end(); ++it)
        {
            if (result.codeMapping.contains(it.value()))
                it.value() = result.codeMapping.value(it.value());
        }
        for (auto it = result.codeMapping.constBegin(); it != result.codeMapping.constEnd(); ++it)
        {
            if (!m_mapping.contains(it.key()))
                m_mapping.insert(it.key(), it.value());
        }
        m_root = result.root;
        m_logs.append(result.logs);
        m_changedVoucherCount += result.changedVoucherCount;
        m_changedBalanceCount += result.changedBalanceCount;
        m_reversedOpeningCount += result.reversedOpeningCount;
        m_newCode->clear();
        m_newName->clear();
        m_history->setPlainText(m_logs.join(QStringLiteral("\n")));
        rebuildTree();
    }

    void undoMerge()
    {
        if (m_snapshots.isEmpty())
            return;
        const MergeDialogSnapshot snapshot = m_snapshots.takeLast();
        m_root = snapshot.root;
        m_mapping = snapshot.mapping;
        m_logs = snapshot.logs;
        m_changedVoucherCount = snapshot.changedVoucherCount;
        m_changedBalanceCount = snapshot.changedBalanceCount;
        m_reversedOpeningCount = snapshot.reversedOpeningCount;
        m_history->setPlainText(m_logs.join(QStringLiteral("\n")));
        rebuildTree();
    }

    void acceptIfMerged()
    {
        if (m_snapshots.isEmpty())
        {
            QMessageBox::information(this, QStringLiteral("尚未合并"),
                                     QStringLiteral("请至少完成一次科目合并后再生成数据包。"));
            return;
        }
        accept();
    }

    QJsonObject m_root;
    int m_originalAccountCount;
    QList<SubjectMergeNode> m_nodes;
    QMap<QString, SubjectMergeNode> m_nodeByCode;
    QMap<QString, int> m_subtreeSubjectCounts;
    QMap<QString, int> m_subtreeBalanceCounts;
    QMap<QString, int> m_subtreeVoucherCounts;
    QMap<QString, QString> m_mapping;
    QStringList m_logs;
    QList<MergeDialogSnapshot> m_snapshots;
    int m_changedVoucherCount = 0;
    int m_changedBalanceCount = 0;
    int m_reversedOpeningCount = 0;
    QLineEdit *m_filter;
    QTreeWidget *m_tree;
    QLabel *m_selectionLabel;
    QComboBox *m_targetCombo;
    QLineEdit *m_newCode;
    QLineEdit *m_newName;
    QComboBox *m_directionCombo;
    QPushButton *m_mergeButton;
    QPushButton *m_undoButton;
    QTextEdit *m_history;
    QDialogButtonBox *m_buttonBox;
};
}

SubjectMergeWidget::SubjectMergeWidget(GlobalData *globalData, QWidget *parent)
    : QWidget(parent),
      m_pGlobalData(globalData),
      m_pPathConfig(nullptr),
      m_pInputFilePicker(nullptr),
      m_pDropLabel(nullptr),
      m_pStatusLabel(nullptr),
      m_pLogEdit(nullptr),
      m_pBtnBrowse(nullptr),
      m_pBtnBack(nullptr),
      m_requestId(0)
{
    setAcceptDrops(true);
    setupUi();
}

void SubjectMergeWidget::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    QHBoxLayout *titleLayout = new QHBoxLayout();
    QLabel *title = new QLabel(QStringLiteral("会计科目合并"), this);
    title->setObjectName(QStringLiteral("labelPageTitle"));
    titleLayout->addWidget(title);
    titleLayout->addStretch();
    m_pBtnBrowse = new QPushButton(QStringLiteral("选择文件"), this);
    m_pBtnBrowse->setMaximumWidth(100);
    titleLayout->addWidget(m_pBtnBrowse);
    layout->addLayout(titleLayout);

    QLabel *description = new QLabel(
        QStringLiteral("按科目编码推导真实父子层级，手动勾选同父级科目并选择合并目标；"
                       "支持一级科目、同名子科目递归合并、末级与非末级混合、"
                       "期初借贷方向换算和凭证科目迁移。"), this);
    description->setWordWrap(true);
    layout->addWidget(description);

    m_pPathConfig = new RepairPathConfigWidget(this);
    m_pPathConfig->setPathsChangedCallback([this]() { refreshInputFiles(); });
    layout->addWidget(m_pPathConfig);

    m_pInputFilePicker = new RepairInputFilePickerWidget(
        QStringList() << QStringLiteral("*.zip") << QStringLiteral("*.json"), this);
    m_pInputFilePicker->setProcessCallback(
        [this](const QStringList &paths) { processSelectedFiles(paths); });
    m_pInputFilePicker->setFilesChangedCallback([this](int count) {
        if (m_pStatusLabel)
            m_pStatusLabel->setText(QStringLiteral("已扫描到 %1 个 JSON/ZIP；科目合并需要逐个文件确认")
                                    .arg(count));
    });
    layout->addWidget(m_pInputFilePicker);

    m_pDropLabel = new QLabel(
        QStringLiteral("将 AccountSet.json 或 ZIP 拖入此处，随后勾选要合并的会计科目"), this);
    m_pDropLabel->setObjectName(QStringLiteral("labelRepairDropArea"));
    m_pDropLabel->setAlignment(Qt::AlignCenter);
    m_pDropLabel->setMinimumHeight(72);
    m_pDropLabel->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(m_pDropLabel);

    m_pStatusLabel = new QLabel(QStringLiteral("待处理"), this);
    m_pStatusLabel->setWordWrap(true);
    layout->addWidget(m_pStatusLabel);
    m_pLogEdit = new QTextEdit(this);
    m_pLogEdit->setReadOnly(true);
    m_pLogEdit->setMinimumHeight(180);
    layout->addWidget(m_pLogEdit, 1);

    QHBoxLayout *buttons = new QHBoxLayout();
    m_pBtnBack = new QPushButton(QStringLiteral("返回"), this);
    m_pBtnBack->setObjectName(QStringLiteral("btnBackToMain"));
    m_pBtnBack->setProperty("grayButton", true);
    m_pBtnBack->setMaximumWidth(100);
    m_pBtnBack->setMinimumHeight(36);
    buttons->addWidget(m_pBtnBack);
    buttons->addStretch();
    layout->addLayout(buttons);

    connect(m_pBtnBrowse, &QPushButton::clicked, this, &SubjectMergeWidget::onBrowseClicked);
    connect(m_pBtnBack, &QPushButton::clicked, this, &SubjectMergeWidget::onBackClicked);
}

QString SubjectMergeWidget::workRootDir() const
{
    return QDir::cleanPath(qApp->applicationDirPath()
                           + QStringLiteral("/SubjectMergePackages"));
}

void SubjectMergeWidget::setRunning(bool running, const QString &message)
{
    m_pBtnBrowse->setEnabled(!running);
    m_pBtnBack->setEnabled(!running);
    m_pPathConfig->setEnabled(!running);
    m_pInputFilePicker->setRunning(running);
    setAcceptDrops(!running);
    if (!message.isEmpty())
        m_pStatusLabel->setText(message);
}

void SubjectMergeWidget::refreshInputFiles()
{
    m_pPathConfig->refreshPaths();
    m_pInputFilePicker->refreshFiles();
}

void SubjectMergeWidget::processSelectedFiles(const QStringList &filePaths)
{
    if (filePaths.size() != 1)
    {
        QMessageBox::information(this, QStringLiteral("请逐个处理"),
            QStringLiteral("科目合并需要逐个账套手动确认，请每次只选择一个文件。"));
        return;
    }
    startProcessFile(filePaths.first());
}

void SubjectMergeWidget::startProcessFile(const QString &filePath)
{
    const int requestId = ++m_requestId;
    const QString inputPath = QFileInfo(filePath).absoluteFilePath();
    const QString rootPath = workRootDir();
    setRunning(true, QStringLiteral("正在读取账套并根据科目编码推导层级..."));
    m_pLogEdit->setPlainText(QStringLiteral("正在处理：")
                             + QDir::toNativeSeparators(inputPath));

    QSharedPointer<SubjectMergeAnalysis> analysis(new SubjectMergeAnalysis);
    QThread *thread = QThread::create([analysis, inputPath, rootPath]() {
        *analysis = analyzeInput(inputPath, rootPath);
    });
    connect(thread, &QThread::finished, this, [this, analysis, requestId]() {
        if (requestId != m_requestId)
            return;
        if (!analysis->ok)
        {
            setRunning(false, QStringLiteral("读取失败：") + analysis->errorMessage);
            m_pLogEdit->append(analysis->errorMessage);
            QMessageBox::warning(this, QStringLiteral("科目合并"), analysis->errorMessage);
            return;
        }

        setRunning(false, QStringLiteral("已识别 %1 个科目，请勾选需要合并的科目")
                   .arg(analysis->nodes.size()));
        m_pLogEdit->append(QStringLiteral("账套：%1；科目：%2；期初：%3；凭证：%4")
            .arg(analysis->accountSetName)
            .arg(analysis->nodes.size())
            .arg(analysis->root.value(QStringLiteral("GLBalanceList")).toArray().size())
            .arg(analysis->root.value(QStringLiteral("GLVoucherList")).toArray().size()));

        SubjectMergeDialog dialog(*analysis, this);
        if (dialog.exec() != QDialog::Accepted)
        {
            m_pStatusLabel->setText(QStringLiteral("已取消，未生成输出文件"));
            return;
        }

        const QJsonObject mergedRoot = dialog.mergedRoot();
        const QMap<QString, QString> mapping = dialog.mapping();
        const QStringList logs = dialog.logs();
        const int operationCount = dialog.operationCount();
        const int voucherCount = dialog.changedVoucherCount();
        const int balanceCount = dialog.changedBalanceCount();
        const int reversedCount = dialog.reversedOpeningCount();
        const QString outputPath = RepairPathSettings::outputDir();
        setRunning(true, QStringLiteral("正在生成科目合并 ZIP 和映射清单..."));

        QSharedPointer<SubjectMergeFinalization> finalized(new SubjectMergeFinalization);
        QThread *finalThread = QThread::create(
            [finalized, analysis, mergedRoot, mapping, outputPath]() {
                *finalized = finalizeOutput(*analysis, mergedRoot, mapping, outputPath);
            });
        connect(finalThread, &QThread::finished, this,
                [this, finalized, logs, operationCount, voucherCount, balanceCount,
                 reversedCount, requestId]() {
            if (requestId != m_requestId)
                return;
            if (!finalized->ok)
            {
                setRunning(false, QStringLiteral("生成失败：") + finalized->errorMessage);
                m_pLogEdit->append(finalized->errorMessage);
                QMessageBox::warning(this, QStringLiteral("科目合并失败"),
                                     finalized->errorMessage);
                return;
            }
            setRunning(false, QStringLiteral("处理完成：完成 %1 次科目合并")
                       .arg(operationCount));
            m_pLogEdit->append(logs.join(QStringLiteral("\n")));
            m_pLogEdit->append(QStringLiteral("凭证科目编码调整 %1 条，期初科目编码调整 %2 条，期初金额取反 %3 条")
                .arg(voucherCount).arg(balanceCount).arg(reversedCount));
            m_pLogEdit->append(QStringLiteral("输出 ZIP：")
                + QDir::toNativeSeparators(finalized->outputZipPath));
            m_pLogEdit->append(QStringLiteral("科目映射：")
                + QDir::toNativeSeparators(finalized->mappingPath));
            QMessageBox::information(this, QStringLiteral("科目合并完成"),
                QStringLiteral("已完成 %1 次科目合并。\n\n输出 ZIP：\n%2\n\n科目映射：\n%3")
                    .arg(operationCount)
                    .arg(QDir::toNativeSeparators(finalized->outputZipPath),
                         QDir::toNativeSeparators(finalized->mappingPath)));
        });
        connect(finalThread, &QThread::finished, finalThread, &QObject::deleteLater);
        finalThread->start();
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void SubjectMergeWidget::onBrowseClicked()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 AccountSet.json 或 ZIP"),
        RepairPathSettings::inputDir(),
        QStringLiteral("账套数据 (*.json *.zip);;JSON (*.json);;ZIP (*.zip)"));
    if (!path.isEmpty())
        startProcessFile(path);
}

void SubjectMergeWidget::onBackClicked()
{
    emit backToMain();
}

void SubjectMergeWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (hasSupportedInput(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void SubjectMergeWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (hasSupportedInput(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void SubjectMergeWidget::dropEvent(QDropEvent *event)
{
    if (!hasSupportedInput(event->mimeData()))
    {
        event->ignore();
        return;
    }
    for (const QUrl &url : event->mimeData()->urls())
    {
        if (url.isLocalFile() && supportedInput(url.toLocalFile()))
        {
            event->acceptProposedAction();
            startProcessFile(url.toLocalFile());
            return;
        }
    }
    event->ignore();
}
