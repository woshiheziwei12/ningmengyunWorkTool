#include "repairinputfilepickerwidget.h"
#include "repairpathsettings.h"

#include <algorithm>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QSizePolicy>
#include <QVBoxLayout>

RepairInputFilePickerWidget::RepairInputFilePickerWidget(
    const QStringList &nameFilters,
    QWidget *parent)
    : QWidget(parent),
      m_nameFilters(nameFilters),
      m_pCountLabel(new QLabel(this)),
      m_pFileList(new QListWidget(this)),
      m_pBtnSelectAll(new QPushButton(QStringLiteral("\u5168\u9009"), this)),
      m_pBtnRefresh(new QPushButton(QStringLiteral("\u5237\u65b0"), this)),
      m_pBtnProcess(new QPushButton(QStringLiteral("\u5904\u7406\u9009\u4e2d"), this))
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    QHBoxLayout *toolbar = new QHBoxLayout();
    QLabel *caption = new QLabel(QStringLiteral("\u8f93\u5165\u76ee\u5f55\u6587\u4ef6"), this);
    toolbar->addWidget(caption);
    toolbar->addWidget(m_pCountLabel);
    toolbar->addStretch();
    m_pBtnSelectAll->setCheckable(true);
    m_pBtnSelectAll->setProperty("flatGreen", true);
    toolbar->addWidget(m_pBtnSelectAll);
    m_pBtnRefresh->setProperty("flatGreen", true);
    m_pBtnProcess->setMaximumWidth(110);
    toolbar->addWidget(m_pBtnRefresh);
    toolbar->addWidget(m_pBtnProcess);
    layout->addLayout(toolbar);

    m_pFileList->setMinimumHeight(92);
    m_pFileList->setMaximumHeight(150);
    m_pFileList->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_pFileList);

    connect(m_pBtnRefresh, &QPushButton::clicked,
            this, [this]() { refreshFiles(); });
    connect(m_pBtnSelectAll, &QPushButton::toggled, this, [this](bool checked) {
        for (int i = 0; i < m_pFileList->count(); ++i)
            m_pFileList->item(i)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        m_pBtnSelectAll->setText(checked
            ? QStringLiteral("\u53d6\u6d88\u5168\u9009")
            : QStringLiteral("\u5168\u9009"));
    });
    connect(m_pBtnProcess, &QPushButton::clicked, this, [this]() {
        const QStringList paths = selectedPaths();
        if (!paths.isEmpty() && m_processCallback)
            m_processCallback(paths);
    });
    connect(m_pFileList, &QListWidget::itemChanged, this, [this]() {
        m_pBtnProcess->setEnabled(isEnabled() && !selectedPaths().isEmpty());
    });
}

void RepairInputFilePickerWidget::refreshFiles()
{
    const QStringList previouslySelected = selectedPaths();
    QSet<QString> previousPaths;
    for (const QString &path : previouslySelected)
        previousPaths.insert(path);
    const QString inputDirectory = QDir::cleanPath(RepairPathSettings::inputDir());
    const QString outputDirectory = QDir::cleanPath(RepairPathSettings::outputDir());
    const QString outputPrefix = outputDirectory + QLatin1Char('/');
    const bool outputInsideInput = outputDirectory.compare(inputDirectory, Qt::CaseInsensitive) != 0
        && outputPrefix.startsWith(inputDirectory + QLatin1Char('/'), Qt::CaseInsensitive);

    QStringList paths;
    QDirIterator iterator(inputDirectory,
                          m_nameFilters,
                          QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        const QString path = QDir::cleanPath(iterator.next());
        if (outputInsideInput
            && (path + QLatin1Char('/')).startsWith(outputPrefix, Qt::CaseInsensitive))
        {
            continue;
        }
        paths.append(QFileInfo(path).absoluteFilePath());
    }
    std::sort(paths.begin(), paths.end(), [](const QString &left, const QString &right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });

    m_pFileList->clear();
    const QDir inputDir(inputDirectory);
    for (const QString &path : paths)
    {
        const QString relativePath = QDir::toNativeSeparators(inputDir.relativeFilePath(path));
        QListWidgetItem *item = new QListWidgetItem(relativePath, m_pFileList);
        item->setData(Qt::UserRole, path);
        item->setToolTip(QDir::toNativeSeparators(path));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(previousPaths.contains(path) ? Qt::Checked : Qt::Unchecked);
    }

    m_pCountLabel->setText(QStringLiteral("%1 \u4e2a").arg(paths.size()));
    m_pBtnSelectAll->blockSignals(true);
    m_pBtnSelectAll->setChecked(false);
    m_pBtnSelectAll->setText(QStringLiteral("\u5168\u9009"));
    m_pBtnSelectAll->blockSignals(false);
    m_pBtnSelectAll->setEnabled(!paths.isEmpty());
    m_pBtnProcess->setEnabled(!selectedPaths().isEmpty());
    m_pFileList->setEnabled(!paths.isEmpty());
    if (m_filesChangedCallback)
        m_filesChangedCallback(paths.size());
}

void RepairInputFilePickerWidget::setRunning(bool running)
{
    m_pFileList->setEnabled(!running && m_pFileList->count() > 0);
    m_pBtnSelectAll->setEnabled(!running && m_pFileList->count() > 0);
    m_pBtnRefresh->setEnabled(!running);
    m_pBtnProcess->setEnabled(!running && !selectedPaths().isEmpty());
}

void RepairInputFilePickerWidget::setProcessCallback(
    const std::function<void(const QStringList &)> &callback)
{
    m_processCallback = callback;
}

void RepairInputFilePickerWidget::setFilesChangedCallback(
    const std::function<void(int)> &callback)
{
    m_filesChangedCallback = callback;
}

QStringList RepairInputFilePickerWidget::selectedPaths() const
{
    QStringList paths;
    for (int i = 0; i < m_pFileList->count(); ++i)
    {
        const QListWidgetItem *item = m_pFileList->item(i);
        if (item->checkState() == Qt::Checked)
            paths.append(item->data(Qt::UserRole).toString());
    }
    return paths;
}

int RepairInputFilePickerWidget::fileCount() const
{
    return m_pFileList->count();
}
