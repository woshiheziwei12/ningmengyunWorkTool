#pragma once
#include "LocalSqlInstances.h"
#include "SqlBackupCompatibility.h"
#include "SqlServer211.h"
#include <QComboBox>
#include <QEvent>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace SqlTargetSelector {
inline QString remoteServer() { return SqlServer211::endpoint(); }
inline void updateVersionLabels(QComboBox* combo)
{
    for (int index = 0; index < combo->count(); ++index) {
        const QString server = combo->itemData(index).toString();
        if (!LocalSqlInstances::isLocalEndpoint(server) && server != remoteServer()) continue;
        const QString version = server == remoteServer() ? SqlServer211::version().version : LocalSqlInstances::cachedVersion(server);
        const QString base = server == remoteServer() ? QString::fromUtf8(u8"211 数据库（192.168.1.211）") : LocalSqlInstances::label(server);
        const QString description = version.isEmpty() ? QString::fromUtf8(u8"版本未知，读取时检测")
            : SqlBackupCompatibility::versionName(SqlBackupCompatibility::engineVersion(version)) + QStringLiteral(" / ") + version;
        combo->setItemData(index, version, Qt::UserRole + 1);
        combo->setItemText(index, base + QStringLiteral(" · ") + description);
        combo->setItemData(index, combo->itemText(index), Qt::ToolTipRole);
    }
}
inline bool isRemote(const QComboBox* combo) { return combo->currentData().toString() == remoteServer(); }
inline bool hasTarget(const QComboBox* combo)
{
    return isRemote(combo) || LocalSqlInstances::isLocalEndpoint(combo->currentData().toString());
}

// Keep the exact endpoint across scans; a removed instance must never redirect
// a pending restore/delete to another instance (especially to 211).
inline void populate(QComboBox* combo, const QStringList& locals = LocalSqlInstances::discover())
{
    const bool initial = combo->count() == 0;
    const QString previous = combo->currentData().toString();
    int selected = -1;
    {
        const QSignalBlocker blocker(combo);
        combo->clear();
        QStringList accepted;
        for (const auto& server : locals) {
            if (!LocalSqlInstances::isLocalEndpoint(server) || accepted.contains(server, Qt::CaseInsensitive)) continue;
            accepted.append(server);
            combo->addItem(LocalSqlInstances::label(server), server);
            if (server.compare(previous, Qt::CaseInsensitive) == 0) selected = combo->count() - 1;
        }
        combo->addItem(QString::fromUtf8(u8"211 数据库（192.168.1.211）"), remoteServer());
        if (previous == remoteServer()) selected = combo->count() - 1;
        if (initial && !accepted.isEmpty()) selected = 0;
        if (selected < 0) {
            combo->insertItem(0, accepted.isEmpty()
                ? QString::fromUtf8(u8"未发现本地实例，请重新扫描或选择 211")
                : QString::fromUtf8(u8"请选择本地实例或 211"), QVariant());
            selected = 0;
        }
        combo->setProperty("localInstanceCount", accepted.size());
        combo->setCurrentIndex(selected);
        updateVersionLabels(combo);
    }
    // Rescanning the same target also invalidates previously inspected plans.
    emit combo->currentIndexChanged(selected);
}

class Control : public QWidget
{
public:
    explicit Control(QComboBox* combo) : QWidget(combo->parentWidget()), m_combo(combo)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* row = new QHBoxLayout;
        m_scan = new QPushButton(QString::fromUtf8(u8"扫描本地实例"), this);
        m_scan->setObjectName(combo->objectName() + QStringLiteral("Scan"));
        m_scan->setProperty("grayButton", true);
        row->addWidget(combo, 1);
        row->addWidget(m_scan);
        layout->addLayout(row);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
        combo->setItemDelegate(new QStyledItemDelegate(combo));
        combo->installEventFilter(this);
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { updateStatus(); });
        connect(m_scan, &QPushButton::clicked, this, [this]() {
            if (m_combo->isEnabled()) populate(m_combo);
        });
        updateStatus();
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_combo && event->type() == QEvent::EnabledChange)
            m_scan->setEnabled(m_combo->isEnabled());
        return QWidget::eventFilter(watched, event);
    }
private:
    void updateStatus()
    {
        m_status->setText(QString::fromUtf8(u8"已发现 %1 个本地 SQL Server 实例；仅支持本地实例和 211。扫描不连接数据库。")
            .arg(m_combo->property("localInstanceCount").toInt()));
    }
    QComboBox* m_combo;
    QPushButton* m_scan;
    QLabel* m_status;
};
inline QWidget* control(QComboBox* combo) { return new Control(combo); }
}
