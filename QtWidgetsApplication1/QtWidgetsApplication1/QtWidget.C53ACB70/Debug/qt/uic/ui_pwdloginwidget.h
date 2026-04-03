/********************************************************************************
** Form generated from reading UI file 'pwdloginwidget.ui'
**
** Created by: Qt User Interface Compiler version 5.12.0
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_PWDLOGINWIDGET_H
#define UI_PWDLOGINWIDGET_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_PwdLoginWidget
{
public:
    QWidget *verticalWidget;
    QLabel *labelPwdError;
    QLineEdit *editUserName;
    QLabel *label;
    QLineEdit *editPwd;
    QLabel *label_2;
    QPushButton *btnForgetPwd;
    QCheckBox *checkBtnAutoPwd;
    QPushButton *btnPwdLogin;
    QFrame *line;
    QFrame *line_2;
    QLabel *labelRight;

    void setupUi(QWidget *PwdLoginWidget)
    {
        if (PwdLoginWidget->objectName().isEmpty())
            PwdLoginWidget->setObjectName(QString::fromUtf8("PwdLoginWidget"));
        PwdLoginWidget->resize(370, 275);
        PwdLoginWidget->setMinimumSize(QSize(370, 275));
        PwdLoginWidget->setMaximumSize(QSize(370, 275));
        verticalWidget = new QWidget(PwdLoginWidget);
        verticalWidget->setObjectName(QString::fromUtf8("verticalWidget"));
        verticalWidget->setGeometry(QRect(0, 0, 375, 281));
        verticalWidget->setMinimumSize(QSize(375, 0));
        labelPwdError = new QLabel(verticalWidget);
        labelPwdError->setObjectName(QString::fromUtf8("labelPwdError"));
        labelPwdError->setGeometry(QRect(70, 20, 271, 20));
        labelPwdError->setMinimumSize(QSize(0, 20));
        labelPwdError->setMaximumSize(QSize(16777215, 20));
        labelPwdError->setStyleSheet(QString::fromUtf8("font-size:12px;\n"
"font-weight:400;\n"
"color:rgba(255,39,65,1);\n"
"line-height:17px;"));
        editUserName = new QLineEdit(verticalWidget);
        editUserName->setObjectName(QString::fromUtf8("editUserName"));
        editUserName->setGeometry(QRect(100, 48, 180, 30));
        editUserName->setMinimumSize(QSize(180, 30));
        editUserName->setMaximumSize(QSize(180, 35));
        editUserName->setStyleSheet(QString::fromUtf8("font-size:14px;\n"
"font-weight:400;\n"
"color:rgba(0,0,0,0.85);\n"
"line-height:16px;"));
        label = new QLabel(verticalWidget);
        label->setObjectName(QString::fromUtf8("label"));
        label->setGeometry(QRect(70, 50, 28, 28));
        label->setMinimumSize(QSize(28, 28));
        label->setMaximumSize(QSize(28, 28));
        label->setPixmap(QPixmap(QString::fromUtf8(":/Resource/Images/phone-icon.png")));
        label->setAlignment(Qt::AlignCenter);
        editPwd = new QLineEdit(verticalWidget);
        editPwd->setObjectName(QString::fromUtf8("editPwd"));
        editPwd->setGeometry(QRect(100, 109, 180, 30));
        editPwd->setMinimumSize(QSize(180, 30));
        editPwd->setMaximumSize(QSize(180, 30));
        editPwd->setStyleSheet(QString::fromUtf8("font-size:14px;\n"
"font-weight:400;\n"
"color:rgba(0,0,0,0.85);\n"
"line-height:16px;"));
        editPwd->setEchoMode(QLineEdit::Password);
        label_2 = new QLabel(verticalWidget);
        label_2->setObjectName(QString::fromUtf8("label_2"));
        label_2->setGeometry(QRect(70, 110, 28, 28));
        label_2->setMinimumSize(QSize(28, 28));
        label_2->setMaximumSize(QSize(28, 28));
        label_2->setPixmap(QPixmap(QString::fromUtf8(":/Resource/Images/password-icon.png")));
        label_2->setAlignment(Qt::AlignCenter);
        btnForgetPwd = new QPushButton(verticalWidget);
        btnForgetPwd->setObjectName(QString::fromUtf8("btnForgetPwd"));
        btnForgetPwd->setGeometry(QRect(260, 145, 65, 22));
        btnForgetPwd->setCursor(QCursor(Qt::PointingHandCursor));
        btnForgetPwd->setStyleSheet(QString::fromUtf8("font-size:12px;\n"
"font-weight:400;\n"
"color:rgba(1,194,87,1);\n"
"line-height:17px;"));
        btnForgetPwd->setFlat(true);
        checkBtnAutoPwd = new QCheckBox(verticalWidget);
        checkBtnAutoPwd->setObjectName(QString::fromUtf8("checkBtnAutoPwd"));
        checkBtnAutoPwd->setGeometry(QRect(70, 145, 100, 19));
        checkBtnAutoPwd->setStyleSheet(QString::fromUtf8("font-size:12px;\n"
"font-weight:400;\n"
"color:rgba(0,0,0,1);\n"
"line-height:17px;"));
        btnPwdLogin = new QPushButton(verticalWidget);
        btnPwdLogin->setObjectName(QString::fromUtf8("btnPwdLogin"));
        btnPwdLogin->setGeometry(QRect(70, 200, 250, 40));
        btnPwdLogin->setMinimumSize(QSize(250, 40));
        btnPwdLogin->setMaximumSize(QSize(250, 40));
        btnPwdLogin->setAutoDefault(true);
        line = new QFrame(verticalWidget);
        line->setObjectName(QString::fromUtf8("line"));
        line->setGeometry(QRect(70, 80, 250, 1));
        line->setStyleSheet(QString::fromUtf8("background:rgba(0,0,0,0.45);"));
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        line_2 = new QFrame(verticalWidget);
        line_2->setObjectName(QString::fromUtf8("line_2"));
        line_2->setGeometry(QRect(70, 140, 250, 1));
        line_2->setStyleSheet(QString::fromUtf8("background:rgba(0,0,0,0.45);"));
        line_2->setFrameShape(QFrame::HLine);
        line_2->setFrameShadow(QFrame::Sunken);
        labelRight = new QLabel(verticalWidget);
        labelRight->setObjectName(QString::fromUtf8("labelRight"));
        labelRight->setGeometry(QRect(290, 60, 16, 16));
        labelRight->setMinimumSize(QSize(16, 16));
        labelRight->setMaximumSize(QSize(16, 16));
        labelRight->setPixmap(QPixmap(QString::fromUtf8(":/Resource/Images/currect.png")));
        labelRight->setScaledContents(true);

        retranslateUi(PwdLoginWidget);

        QMetaObject::connectSlotsByName(PwdLoginWidget);
    } // setupUi

    void retranslateUi(QWidget *PwdLoginWidget)
    {
        PwdLoginWidget->setWindowTitle(QApplication::translate("PwdLoginWidget", "Form", nullptr));
        labelPwdError->setText(QApplication::translate("PwdLoginWidget", "\347\224\250\346\210\267\345\220\215\346\210\226\345\257\206\347\240\201\351\224\231\350\257\257\357\274\201", nullptr));
        editUserName->setPlaceholderText(QApplication::translate("PwdLoginWidget", "\350\257\267\350\276\223\345\205\245\346\211\213\346\234\272\345\217\267", nullptr));
        label->setText(QString());
        editPwd->setPlaceholderText(QApplication::translate("PwdLoginWidget", "\350\257\267\350\276\223\345\205\245\345\257\206\347\240\201", nullptr));
        label_2->setText(QString());
        btnForgetPwd->setText(QApplication::translate("PwdLoginWidget", "\345\277\230\350\256\260\345\257\206\347\240\201\357\274\237", nullptr));
        checkBtnAutoPwd->setText(QApplication::translate("PwdLoginWidget", "\350\256\260\344\275\217\345\257\206\347\240\201", nullptr));
        btnPwdLogin->setText(QApplication::translate("PwdLoginWidget", "\347\231\273  \345\275\225", nullptr));
        labelRight->setText(QString());
    } // retranslateUi

};

namespace Ui {
    class PwdLoginWidget: public Ui_PwdLoginWidget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_PWDLOGINWIDGET_H
