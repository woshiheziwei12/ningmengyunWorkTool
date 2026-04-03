/****************************************************************************
** Meta object code from reading C++ file 'functionselectwidget.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.12.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../functionselectwidget.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'functionselectwidget.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.12.0. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_FunctionSelectWidget_t {
    QByteArrayData data[10];
    char stringdata0[233];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_FunctionSelectWidget_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_FunctionSelectWidget_t qt_meta_stringdata_FunctionSelectWidget = {
    {
QT_MOC_LITERAL(0, 0, 20), // "FunctionSelectWidget"
QT_MOC_LITERAL(1, 21, 16), // "functionSelected"
QT_MOC_LITERAL(2, 38, 0), // ""
QT_MOC_LITERAL(3, 39, 10), // "functionId"
QT_MOC_LITERAL(4, 50, 27), // "on_btnDeleteAccount_clicked"
QT_MOC_LITERAL(5, 78, 27), // "on_btnBackupAccount_clicked"
QT_MOC_LITERAL(6, 106, 27), // "on_btnRepairAccount_clicked"
QT_MOC_LITERAL(7, 134, 29), // "on_btnExtractYearData_clicked"
QT_MOC_LITERAL(8, 164, 33), // "on_btnSubjectCodeConflict_cli..."
QT_MOC_LITERAL(9, 198, 34) // "on_btnSiblingNameDuplicate_cl..."

    },
    "FunctionSelectWidget\0functionSelected\0"
    "\0functionId\0on_btnDeleteAccount_clicked\0"
    "on_btnBackupAccount_clicked\0"
    "on_btnRepairAccount_clicked\0"
    "on_btnExtractYearData_clicked\0"
    "on_btnSubjectCodeConflict_clicked\0"
    "on_btnSiblingNameDuplicate_clicked"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_FunctionSelectWidget[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   49,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       4,    0,   52,    2, 0x08 /* Private */,
       5,    0,   53,    2, 0x08 /* Private */,
       6,    0,   54,    2, 0x08 /* Private */,
       7,    0,   55,    2, 0x08 /* Private */,
       8,    0,   56,    2, 0x08 /* Private */,
       9,    0,   57,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::Int,    3,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void FunctionSelectWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        FunctionSelectWidget *_t = static_cast<FunctionSelectWidget *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->functionSelected((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 1: _t->on_btnDeleteAccount_clicked(); break;
        case 2: _t->on_btnBackupAccount_clicked(); break;
        case 3: _t->on_btnRepairAccount_clicked(); break;
        case 4: _t->on_btnExtractYearData_clicked(); break;
        case 5: _t->on_btnSubjectCodeConflict_clicked(); break;
        case 6: _t->on_btnSiblingNameDuplicate_clicked(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (FunctionSelectWidget::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&FunctionSelectWidget::functionSelected)) {
                *result = 0;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject FunctionSelectWidget::staticMetaObject = { {
    &QWidget::staticMetaObject,
    qt_meta_stringdata_FunctionSelectWidget.data,
    qt_meta_data_FunctionSelectWidget,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *FunctionSelectWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *FunctionSelectWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_FunctionSelectWidget.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int FunctionSelectWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void FunctionSelectWidget::functionSelected(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
