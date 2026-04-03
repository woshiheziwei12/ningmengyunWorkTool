/********************************************************************************
** Form generated from reading UI file 'smsloginwidget.ui'
**
** Created by: Qt User Interface Compiler version 5.12.0
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_SMSLOGINWIDGET_H
#define UI_SMSLOGINWIDGET_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFrame>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_SmsLoginWidget
{
public:
    QLabel *label;
    QLineEdit *editPhone;
    QLabel *label_2;
    QPushButton *btnSendSms;
    QLineEdit *editSmsNo;
    QPushButton *btnSmsLogin;
    QLabel *label_3;
    QLabel *labelLoginError;
    QFrame *line;
    QFrame *line_2;
    QLabel *labelRight;

    void setupUi(QWidget *SmsLoginWidget)
    {
        if (SmsLoginWidget->objectName().isEmpty())
            SmsLoginWidget->setObjectName(QString::fromUtf8("SmsLoginWidget"));
        SmsLoginWidget->resize(370, 275);
        SmsLoginWidget->setMinimumSize(QSize(370, 275));
        SmsLoginWidget->setMaximumSize(QSize(370, 275));
        label = new QLabel(SmsLoginWidget);
        label->setObjectName(QString::fromUtf8("label"));
        label->setGeometry(QRect(70, 50, 28, 28));
        label->setMinimumSize(QSize(28, 28));
        label->setMaximumSize(QSize(28, 28));
        label->setPixmap(QPixmap(QString::fromUtf8(":/Resource/Images/phone-icon.png")));
        label->setAlignment(Qt::AlignCenter);
        editPhone = new QLineEdit(SmsLoginWidget);
        editPhone->setObjectName(QString::fromUtf8("editPhone"));
        editPhone->setGeometry(QRect(100, 48, 200, 30));
        editPhone->setMinimumSize(QSize(200, 30));
        editPhone->setMaximumSize(QSize(200, 30));
        editPhone->setStyleSheet(QString::fromUtf8("font-size:14px;\n"
"font-weight:400;\n"
"color:rgba(0,0,0,0.85);\n"
"line-height:16px;"));
        label_2 = new QLabel(SmsLoginWidget);
        label_2->setObjectName(QString::fromUtf8("label_2"));
        label_2->setGeometry(QRect(70, 110, 28, 28));
        label_2->setMinimumSize(QSize(28, 28));
        label_2->setMaximumSize(QSize(28, 28));
        label_2->setPixmap(QPixmap(QString::fromUtf8(":/Resource/Images/qrcode-icon.png")));
        label_2->setAlignment(Qt::AlignCenter);
        btnSendSms = new QPushButton(SmsLoginWidget);
        btnSendSms->setObjectName(QString::fromUtf8("btnSendSms"));
        btnSendSms->setGeometry(QRect(240, 115, 80, 25));
        btnSendSms->setMinimumSize(QSize(80, 25));
        btnSendSms->setMaximumSize(QSize(80, 25));
        btnSendSms->setCursor(QCursor(Qt::PointingHandCursor));
        btnSendSms->setStyleSheet(QString::fromUtf8("font-size:13px;\n"
"font-family:PingFangHK-Regular;\n"
"font-weight:400;\n"
"color:rgba(1,194,87,1);\n"
"line-height:17px;"));
        btnSendSms->setFlat(true);
        editSmsNo = new QLineEdit(SmsLoginWidget);
        editSmsNo->setObjectName(QString::fromUtf8("editSmsNo"));
        editSmsNo->setGeometry(QRect(100, 109, 108, 30));
        editSmsNo->setMaximumSize(QSize(110, 40));
        editSmsNo->setStyleSheet(QString::fromUtf8("font-size:14px;\n"
"font-weight:400;\n"
"color:rgba(0,0,0,0.85);\n"
"line-height:16px;"));
        btnSmsLogin = new QPushButton(SmsLoginWidget);
        btnSmsLogin->setObjectName(QString::fromUtf8("btnSmsLogin"));
        btnSmsLogin->setGeometry(QRect(70, 200, 250, 40));
        btnSmsLogin->setMinimumSize(QSize(250, 40));
        btnSmsLogin->setMaximumSize(QSize(250, 40));
        btnSmsLogin->setAutoDefault(true);
        label_3 = new QLabel(SmsLoginWidget);
        label_3->setObjectName(QString::fromUtf8("label_3"));
        label_3->setGeometry(QRect(120, 250, 141, 20));
        label_3->setStyleSheet(QString::fromUtf8("font-size:12px;\n"
"font-family:PingFangHK-Regular;\n"
"font-weight:400;\n"
"color:rgba(0,0,0,0.45);\n"
"line-height:17px;"));
        label_3->setAlignment(Qt::AlignCenter);
        labelLoginError = new QLabel(SmsLoginWidget);
        labelLoginError->setObjectName(QString::fromUtf8("labelLoginError"));
        labelLoginError->setGeometry(QRect(70, 15, 300, 21));
        labelLoginError->setStyleSheet(QString::fromUtf8("font-size:12px;\n"
"font-weight:400;\n"
"color:rgba(255,39,65,1);\n"
"line-height:17px;"));
        line = new QFrame(SmsLoginWidget);
        line->setObjectName(QString::fromUtf8("line"));
        line->setGeometry(QRect(70, 80, 250, 1));
        line->setStyleSheet(QString::fromUtf8("background:rgba(0,0,0,0.45);"));
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        line_2 = new QFrame(SmsLoginWidget);
        line_2->setObjectName(QString::fromUtf8("line_2"));
        line_2->setGeometry(QRect(70, 140, 250, 1));
        line_2->setStyleSheet(QString::fromUtf8("background:rgba(0,0,0,0.45);"));
        line_2->setFrameShape(QFrame::HLine);
        line_2->setFrameShadow(QFrame::Sunken);
        labelRight = new QLabel(SmsLoginWidget);
        labelRight->setObjectName(QString::fromUtf8("labelRight"));
        labelRight->setGeometry(QRect(300, 60, 16, 16));
        labelRight->setMinimumSize(QSize(16, 16));
        labelRight->setMaximumSize(QSize(16, 16));
        labelRight->setPixmap(QPixmap(QString::fromUtf8(":/Resource/Images/currect.png")));
        labelRight->setScaledContents(true);

        retranslateUi(SmsLoginWidget);

        QMetaObject::connectSlotsByName(SmsLoginWidget);
    } // setupUi

    void retranslateUi(QWidget *SmsLoginWidget)
    {
        SmsLoginWidget->setWindowTitle(QApplication::translate("SmsLoginWidget", "Form", nullptr));
        label->setText(QString());
        editPhone->setPlaceholderText(QApplication::translate("SmsLoginWidget", "\350\257\267\350\276\223\345\205\245\346\211\213\346\234\272\345\217\267", nullptr));
        label_2->setText(QString());
        btnSendSms->setText(QApplication::translate("SmsLoginWidget", "\345\217\221\351\200\201\351\252\214\350\257\201\347\240\201", nullptr));
        editSmsNo->setPlaceholderText(QApplication::translate("SmsLoginWidget", "\350\257\267\350\276\223\345\205\245\351\252\214\350\257\201\347\240\201", nullptr));
        btnSmsLogin->setText(QApplication::translate("SmsLoginWidget", "\347\231\273  \345\275\225", nullptr));
        label_3->setText(QApplication::translate("SmsLoginWidget", "\346\226\260\347\224\250\346\210\267\344\271\237\345\217\257\347\233\264\346\216\245\347\231\273\345\275\225\345\223\246", nullptr));
        labelLoginError->setText(QString());
        labelRight->setText(QString());
    } // retranslateUi

};

namespace Ui {
    class SmsLoginWidget: public Ui_SmsLoginWidget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_SMSLOGINWIDGET_H
