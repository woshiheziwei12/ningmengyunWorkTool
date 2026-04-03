/****************************************************************************
** Meta object code from reading C++ file 'micromsgloginwidget.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.12.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../micromsgloginwidget.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'micromsgloginwidget.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.12.0. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_MicroMsgLoginWidget_t {
    QByteArrayData data[8];
    char stringdata0[116];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_MicroMsgLoginWidget_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_MicroMsgLoginWidget_t qt_meta_stringdata_MicroMsgLoginWidget = {
    {
QT_MOC_LITERAL(0, 0, 19), // "MicroMsgLoginWidget"
QT_MOC_LITERAL(1, 20, 12), // "AuthCodeSign"
QT_MOC_LITERAL(2, 33, 0), // ""
QT_MOC_LITERAL(3, 34, 16), // "GetWxQrCodeImage"
QT_MOC_LITERAL(4, 51, 13), // "login_success"
QT_MOC_LITERAL(5, 65, 15), // "on_AuthCodeSign"
QT_MOC_LITERAL(6, 81, 19), // "on_GetWxQrCodeImage"
QT_MOC_LITERAL(7, 101, 14) // "on_CheckScaned"

    },
    "MicroMsgLoginWidget\0AuthCodeSign\0\0"
    "GetWxQrCodeImage\0login_success\0"
    "on_AuthCodeSign\0on_GetWxQrCodeImage\0"
    "on_CheckScaned"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_MicroMsgLoginWidget[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       6,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       3,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   44,    2, 0x06 /* Public */,
       3,    0,   45,    2, 0x06 /* Public */,
       4,    0,   46,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       5,    0,   47,    2, 0x08 /* Private */,
       6,    0,   48,    2, 0x08 /* Private */,
       7,    0,   49,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void MicroMsgLoginWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        MicroMsgLoginWidget *_t = static_cast<MicroMsgLoginWidget *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->AuthCodeSign(); break;
        case 1: _t->GetWxQrCodeImage(); break;
        case 2: _t->login_success(); break;
        case 3: _t->on_AuthCodeSign(); break;
        case 4: _t->on_GetWxQrCodeImage(); break;
        case 5: _t->on_CheckScaned(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (MicroMsgLoginWidget::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MicroMsgLoginWidget::AuthCodeSign)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (MicroMsgLoginWidget::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MicroMsgLoginWidget::GetWxQrCodeImage)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (MicroMsgLoginWidget::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MicroMsgLoginWidget::login_success)) {
                *result = 2;
                return;
            }
        }
    }
    Q_UNUSED(_a);
}

QT_INIT_METAOBJECT const QMetaObject MicroMsgLoginWidget::staticMetaObject = { {
    &QWidget::staticMetaObject,
    qt_meta_stringdata_MicroMsgLoginWidget.data,
    qt_meta_data_MicroMsgLoginWidget,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *MicroMsgLoginWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MicroMsgLoginWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_MicroMsgLoginWidget.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int MicroMsgLoginWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 6;
    }
    return _id;
}

// SIGNAL 0
void MicroMsgLoginWidget::AuthCodeSign()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void MicroMsgLoginWidget::GetWxQrCodeImage()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void MicroMsgLoginWidget::login_success()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
