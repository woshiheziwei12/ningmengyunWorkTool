/****************************************************************************
** Meta object code from reading C++ file 'smsloginwidget.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.12.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../smsloginwidget.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'smsloginwidget.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.12.0. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_SmsLoginWidget_t {
    QByteArrayData data[9];
    char stringdata0[129];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_SmsLoginWidget_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_SmsLoginWidget_t qt_meta_stringdata_SmsLoginWidget = {
    {
QT_MOC_LITERAL(0, 0, 14), // "SmsLoginWidget"
QT_MOC_LITERAL(1, 15, 12), // "AuthCodeSign"
QT_MOC_LITERAL(2, 28, 0), // ""
QT_MOC_LITERAL(3, 29, 8), // "authCode"
QT_MOC_LITERAL(4, 38, 13), // "login_success"
QT_MOC_LITERAL(5, 52, 7), // "tologin"
QT_MOC_LITERAL(6, 60, 22), // "on_btnSmsLogin_clicked"
QT_MOC_LITERAL(7, 83, 21), // "on_btnSendSms_clicked"
QT_MOC_LITERAL(8, 105, 23) // "on_AuthCodeSign_Reveive"

    },
    "SmsLoginWidget\0AuthCodeSign\0\0authCode\0"
    "login_success\0tologin\0on_btnSmsLogin_clicked\0"
    "on_btnSendSms_clicked\0on_AuthCodeSign_Reveive"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_SmsLoginWidget[] = {

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
       1,    1,   44,    2, 0x06 /* Public */,
       4,    0,   47,    2, 0x06 /* Public */,
       5,    0,   48,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       6,    0,   49,    2, 0x08 /* Private */,
       7,    0,   50,    2, 0x08 /* Private */,
       8,    1,   51,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    3,

       0        // eod
};

void SmsLoginWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        SmsLoginWidget *_t = static_cast<SmsLoginWidget *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->AuthCodeSign((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 1: _t->login_success(); break;
        case 2: _t->tologin(); break;
        case 3: _t->on_btnSmsLogin_clicked(); break;
        case 4: _t->on_btnSendSms_clicked(); break;
        case 5: _t->on_AuthCodeSign_Reveive((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (SmsLoginWidget::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SmsLoginWidget::AuthCodeSign)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (SmsLoginWidget::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SmsLoginWidget::login_success)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (SmsLoginWidget::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SmsLoginWidget::tologin)) {
                *result = 2;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject SmsLoginWidget::staticMetaObject = { {
    &QWidget::staticMetaObject,
    qt_meta_stringdata_SmsLoginWidget.data,
    qt_meta_data_SmsLoginWidget,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *SmsLoginWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SmsLoginWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_SmsLoginWidget.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int SmsLoginWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
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
void SmsLoginWidget::AuthCodeSign(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void SmsLoginWidget::login_success()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void SmsLoginWidget::tologin()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
