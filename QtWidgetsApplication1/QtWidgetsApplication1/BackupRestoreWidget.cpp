#include "BackupRestoreWidget.h"
#include "T3RestoreWidget.h"
#include "U8RestoreWidget.h"
#include "TPlusRestoreWidget.h"
#include "KisProRestoreWidget.h"
#include "InspurRestoreWidget.h"
#include "XinZhongDaRestoreWidget.h"
#include "SudaBackupWidget.h"
#include <QComboBox>
#include <QEvent>
#include <QLabel>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <type_traits>

BackupRestoreWidget::BackupRestoreWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("backupRestorePage"));
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(24, 20, 24, 20);
    m_layout->setSpacing(12);
    auto* title = new QLabel(QStringLiteral("恢复备份包"), this);
    title->setObjectName(QStringLiteral("labelPageTitle"));
    m_layout->addWidget(title);
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(QStringLiteral("备份来源软件"), this));
    m_software = new QComboBox(this);
    m_software->setObjectName(QStringLiteral("restoreSoftware"));
    m_software->setItemDelegate(new QStyledItemDelegate(m_software));
    m_software->addItem(QStringLiteral("用友 T3"), 20);
    m_software->addItem(QStringLiteral("用友 U8"), 22);
    m_software->addItem(QStringLiteral("畅捷通 T+"), 25);
    m_software->addItem(QStringLiteral("金蝶 KIS 专业版"), 26);
    m_software->addItem(QStringLiteral("浪潮"), 27);
    m_software->addItem(QStringLiteral("新中大"), 28);
    m_software->addItem(QStringLiteral("速达（解包为 BAK）"), 19);
    row->addWidget(m_software, 1);
    m_layout->addLayout(row);
    auto* hint = new QLabel(QStringLiteral("先选择生成备份的软件，再按下方步骤处理。切换软件会清空当前表单；任务执行期间不可切换。"), this);
    hint->setWordWrap(true);
    m_layout->addWidget(hint);
    connect(m_software, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { changeSoftware(); });
    changeSoftware();
}

bool BackupRestoreWidget::isBusy() const
{
    if (!m_page) return false;
    for (const auto& name : m_page->dynamicPropertyNames())
        if (name.startsWith("sessionBusy") && m_page->property(name.constData()).toBool()) return true;
    return false;
}

bool BackupRestoreWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_page && event->type() == QEvent::DynamicPropertyChange)
        m_software->setEnabled(!isBusy());
    return QWidget::eventFilter(watched, event);
}

void BackupRestoreWidget::selectSoftware(int legacyFunctionId)
{
    if (isBusy()) return;
    const int index = m_software->findData(legacyFunctionId);
    if (index >= 0) m_software->setCurrentIndex(index);
}

void BackupRestoreWidget::changeSoftware()
{
    if (isBusy()) {
        const QSignalBlocker blocker(m_software);
        m_software->setCurrentIndex(m_currentSoftware);
        return;
    }
    if (m_page) {
        m_page->removeEventFilter(this);
        m_layout->removeWidget(m_page);
        delete m_page;
        m_page = nullptr;
    }
    // Preserve each software's parser and control-database rules.
    auto attach = [this](auto* page) {
        m_page = page;
        using Page = std::remove_pointer_t<decltype(page)>;
        connect(page, &Page::backToMain, this, &BackupRestoreWidget::backToMain);
    };
    switch (m_software->currentData().toInt()) {
    case 22: attach(new U8RestoreWidget(this)); break;
    case 25: attach(new TPlusRestoreWidget(this)); break;
    case 26: attach(new KisProRestoreWidget(this)); break;
    case 27: attach(new InspurRestoreWidget(this)); break;
    case 28: attach(new XinZhongDaRestoreWidget(this)); break;
    case 19: attach(new SudaBackupWidget(this)); break;
    default: attach(new T3RestoreWidget(this)); break;
    }
    m_currentSoftware = m_software->currentIndex();
    m_page->layout()->setContentsMargins(0, 0, 0, 0);
    if (auto* title = m_page->findChild<QLabel*>(QStringLiteral("labelPageTitle"))) title->hide();
    m_page->installEventFilter(this);
    m_layout->addWidget(m_page);
}
