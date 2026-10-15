#include "repairpathconfigwidget.h"
#include "repairpathsettings.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QVBoxLayout>

namespace
{
const char kInputDirKey[] = "repair/sharedInputDir";
const char kOutputDirKey[] = "repair/sharedOutputDir";

QString defaultInputDir()
{
    return QDir::cleanPath(qApp->applicationDirPath()
                           + QStringLiteral("/RepairAccountSetPackages"));
}

QString defaultOutputDir()
{
    return QDir::cleanPath(defaultInputDir() + QStringLiteral("/Repaired"));
}

QString cleanConfiguredPath(const QString &path, const QString &fallback)
{
    const QString cleaned = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
    return cleaned.isEmpty() || cleaned == QStringLiteral(".") ? fallback : cleaned;
}
}

QString RepairPathSettings::inputDir()
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                       QStringLiteral("LemonCloud"), QStringLiteral("LemonWorkTool"));
    return cleanConfiguredPath(settings.value(QLatin1String(kInputDirKey)).toString(),
                               defaultInputDir());
}

QString RepairPathSettings::outputDir()
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                       QStringLiteral("LemonCloud"), QStringLiteral("LemonWorkTool"));
    return cleanConfiguredPath(settings.value(QLatin1String(kOutputDirKey)).toString(),
                               defaultOutputDir());
}

void RepairPathSettings::setInputDir(const QString &path)
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                       QStringLiteral("LemonCloud"), QStringLiteral("LemonWorkTool"));
    settings.setValue(QLatin1String(kInputDirKey), cleanConfiguredPath(path, defaultInputDir()));
    settings.sync();
}

void RepairPathSettings::setOutputDir(const QString &path)
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                       QStringLiteral("LemonCloud"), QStringLiteral("LemonWorkTool"));
    settings.setValue(QLatin1String(kOutputDirKey), cleanConfiguredPath(path, defaultOutputDir()));
    settings.sync();
}

RepairPathConfigWidget::RepairPathConfigWidget(QWidget *parent)
    : QWidget(parent),
      m_pInputLabel(new QLabel(this)),
      m_pOutputLabel(new QLabel(this))
{
    setObjectName(QStringLiteral("sharedPathCard"));
    setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 10, 12, 10);
    rootLayout->setSpacing(6);

    QHBoxLayout *inputLayout = new QHBoxLayout();
    m_pInputLabel->setWordWrap(true);
    inputLayout->addWidget(m_pInputLabel, 1);
    QPushButton *inputButton = new QPushButton(QStringLiteral("选择输入目录"), this);
    inputButton->setMaximumWidth(120);
    inputLayout->addWidget(inputButton);
    rootLayout->addLayout(inputLayout);

    QHBoxLayout *outputLayout = new QHBoxLayout();
    m_pOutputLabel->setWordWrap(true);
    outputLayout->addWidget(m_pOutputLabel, 1);
    QPushButton *outputButton = new QPushButton(QStringLiteral("选择输出目录"), this);
    outputButton->setMaximumWidth(120);
    outputLayout->addWidget(outputButton);
    rootLayout->addLayout(outputLayout);

    connect(inputButton, &QPushButton::clicked, this, [this]() { selectInputDir(); });
    connect(outputButton, &QPushButton::clicked, this, [this]() { selectOutputDir(); });

    refreshPaths();
}

void RepairPathConfigWidget::refreshPaths()
{
    m_pInputLabel->setText(QStringLiteral("共享输入目录：%1")
        .arg(QDir::toNativeSeparators(RepairPathSettings::inputDir())));
    m_pOutputLabel->setText(QStringLiteral("共享输出目录：%1")
        .arg(QDir::toNativeSeparators(RepairPathSettings::outputDir())));
}

void RepairPathConfigWidget::setPathsChangedCallback(const std::function<void()> &callback)
{
    m_pathsChangedCallback = callback;
}

void RepairPathConfigWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshPaths();
}

void RepairPathConfigWidget::selectInputDir()
{
    QDir().mkpath(RepairPathSettings::inputDir());
    const QString path = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择所有修复功能共用的输入目录"),
        RepairPathSettings::inputDir(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (path.isEmpty())
        return;

    RepairPathSettings::setInputDir(path);
    refreshPaths();
    if (m_pathsChangedCallback)
        m_pathsChangedCallback();
}

void RepairPathConfigWidget::selectOutputDir()
{
    QDir().mkpath(RepairPathSettings::outputDir());
    const QString path = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择所有修复功能共用的输出目录"),
        RepairPathSettings::outputDir(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (path.isEmpty())
        return;

    RepairPathSettings::setOutputDir(path);
    refreshPaths();
    if (m_pathsChangedCallback)
        m_pathsChangedCallback();
}
