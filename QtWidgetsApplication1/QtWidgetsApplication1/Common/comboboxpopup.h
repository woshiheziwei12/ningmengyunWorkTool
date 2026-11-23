#pragma once

#include <QListView>
#include <QStyledItemDelegate>

// Gives styled combo box options enough room for their text and selection padding.
inline void spaceComboItems(QAbstractItemView *view)
{
    class SpacedComboDelegate final : public QStyledItemDelegate
    {
    public:
        explicit SpacedComboDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
        QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
        {
            QSize size = QStyledItemDelegate::sizeHint(option, index);
            size.setHeight(qMax(size.height(), qMax(36, option.fontMetrics.height() + 16)));
            return size;
        }
    };

    view->setItemDelegate(new SpacedComboDelegate(view));
    view->setTextElideMode(Qt::ElideRight);
    if (auto *list = qobject_cast<QListView*>(view)) {
        list->setSpacing(2);
        list->setUniformItemSizes(true);
    }
}
