/********************************************************************************
** Form generated from reading UI file 'micromsgloginwidget.ui'
**
** Created by: Qt User Interface Compiler version 5.12.0
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MICROMSGLOGINWIDGET_H
#define UI_MICROMSGLOGINWIDGET_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MicroMsgLoginWidget
{
public:
    QLabel *labelError;
    QLabel *labelErWeiMa;

    void setupUi(QWidget *MicroMsgLoginWidget)
    {
        if (MicroMsgLoginWidget->objectName().isEmpty())
            MicroMsgLoginWidget->setObjectName(QString::fromUtf8("MicroMsgLoginWidget"));
        MicroMsgLoginWidget->resize(270, 270);
        MicroMsgLoginWidget->setMinimumSize(QSize(270, 270));
        MicroMsgLoginWidget->setMaximumSize(QSize(270, 270));
        labelError = new QLabel(MicroMsgLoginWidget);
        labelError->setObjectName(QString::fromUtf8("labelError"));
        labelError->setGeometry(QRect(5, 100, 260, 60));
        labelError->setMinimumSize(QSize(260, 60));
        labelError->setMaximumSize(QSize(260, 60));
        labelError->setStyleSheet(QString::fromUtf8("background:#CC000000;\n"
"color:white;\n"
"border-radius:4px;"));
        labelError->setAlignment(Qt::AlignCenter);
        labelErWeiMa = new QLabel(MicroMsgLoginWidget);
        labelErWeiMa->setObjectName(QString::fromUtf8("labelErWeiMa"));
        labelErWeiMa->setGeometry(QRect(35, 35, 200, 200));
        labelErWeiMa->setMinimumSize(QSize(200, 200));
        labelErWeiMa->setMaximumSize(QSize(200, 200));
        labelErWeiMa->setScaledContents(true);
        labelErWeiMa->setAlignment(Qt::AlignCenter);
        labelErWeiMa->raise();
        labelError->raise();

        retranslateUi(MicroMsgLoginWidget);

        QMetaObject::connectSlotsByName(MicroMsgLoginWidget);
    } // setupUi

    void retranslateUi(QWidget *MicroMsgLoginWidget)
    {
        MicroMsgLoginWidget->setWindowTitle(QApplication::translate("MicroMsgLoginWidget", "Form", nullptr));
        labelError->setText(QApplication::translate("MicroMsgLoginWidget", "\345\276\256\344\277\241\346\234\252\347\273\221\345\256\232\357\274\214\350\257\267\347\224\250\345\205\266\344\273\226\346\226\271\345\274\217\347\231\273\345\275\225\345\223\246~", nullptr));
        labelErWeiMa->setText(QString());
    } // retranslateUi

};

namespace Ui {
    class MicroMsgLoginWidget: public Ui_MicroMsgLoginWidget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MICROMSGLOGINWIDGET_H
