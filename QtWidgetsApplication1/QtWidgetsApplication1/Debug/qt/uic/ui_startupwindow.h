/********************************************************************************
** Form generated from reading UI file 'startupwindow.ui'
**
** Created by: Qt User Interface Compiler version 5.12.0
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_STARTUPWINDOW_H
#define UI_STARTUPWINDOW_H

#include <QtCore/QVariant>
#include <QtGui/QIcon>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_StartUpWindow
{
public:
    QHBoxLayout *horizontalLayout_8;
    QWidget *widgetLeft;
    QHBoxLayout *horizontalLayout_4;
    QVBoxLayout *verticalLayout_6;
    QVBoxLayout *verticalLayout;
    QHBoxLayout *horizontalLayout_6;
    QLabel *label_8;
    QHBoxLayout *horizontalLayout_7;
    QLabel *label;
    QHBoxLayout *horizontalLayout_2;
    QLabel *label_11;
    QVBoxLayout *verticalLayout_2;
    QLabel *label_7;
    QHBoxLayout *horizontalLayout;
    QLabel *label_10;
    QVBoxLayout *verticalLayout_3;
    QLabel *label_4;
    QHBoxLayout *horizontalLayout_3;
    QLabel *label_9;
    QVBoxLayout *verticalLayout_4;
    QLabel *label_2;
    QSpacerItem *verticalSpacer_3;
    QWidget *widgetRight;
    QHBoxLayout *horizontalLayout_5;
    QVBoxLayout *verticalLayout_7;
    QWidget *widgetTitle;
    QHBoxLayout *horizontalLayout_9;
    QSpacerItem *horizontalSpacer;
    QWidget *widgetSize;
    QHBoxLayout *horizontalLayout_10;
    QPushButton *btnMin;
    QPushButton *btnExit;
    QHBoxLayout *layouLoginMenu;
    QSpacerItem *horizontalSpacer_2;
    QPushButton *btnMicroMsgMode;
    QFrame *line_2;
    QPushButton *btnPwdMode;
    QFrame *line;
    QPushButton *btnSmsMode;
    QSpacerItem *horizontalSpacer_3;
    QVBoxLayout *verticalLayout_5;
    QSpacerItem *verticalSpacer_2;
    QHBoxLayout *loginWidget;
    QSpacerItem *verticalSpacer;
    QHBoxLayout *registorLayout;
    QLabel *labelFlag;
    QPushButton *btnRegistor;

    void setupUi(QDialog *StartUpWindow)
    {
        if (StartUpWindow->objectName().isEmpty())
            StartUpWindow->setObjectName(QString::fromUtf8("StartUpWindow"));
        StartUpWindow->setWindowModality(Qt::WindowModal);
        StartUpWindow->resize(622, 420);
        QSizePolicy sizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(StartUpWindow->sizePolicy().hasHeightForWidth());
        StartUpWindow->setSizePolicy(sizePolicy);
        StartUpWindow->setMinimumSize(QSize(620, 418));
        StartUpWindow->setMaximumSize(QSize(622, 420));
        QIcon icon;
        icon.addFile(QString::fromUtf8(":/Resource/Images/logo-reverse.png"), QSize(), QIcon::Normal, QIcon::Off);
        StartUpWindow->setWindowIcon(icon);
        StartUpWindow->setStyleSheet(QString::fromUtf8("background:rgba(255,255,255,1);\n"
"border:1px solid rgba(228,228,228,1);"));
        horizontalLayout_8 = new QHBoxLayout(StartUpWindow);
        horizontalLayout_8->setSpacing(0);
        horizontalLayout_8->setContentsMargins(0, 0, 0, 0);
        horizontalLayout_8->setObjectName(QString::fromUtf8("horizontalLayout_8"));
        horizontalLayout_8->setContentsMargins(1, 1, 1, 1);
        widgetLeft = new QWidget(StartUpWindow);
        widgetLeft->setObjectName(QString::fromUtf8("widgetLeft"));
        widgetLeft->setEnabled(true);
        QSizePolicy sizePolicy1(QSizePolicy::Preferred, QSizePolicy::Preferred);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(widgetLeft->sizePolicy().hasHeightForWidth());
        widgetLeft->setSizePolicy(sizePolicy1);
        widgetLeft->setMinimumSize(QSize(237, 418));
        widgetLeft->setMaximumSize(QSize(237, 418));
        widgetLeft->setStyleSheet(QString::fromUtf8("border-image:url(:Resource/Images/main_left_bgimg.png);\n"
""));
        horizontalLayout_4 = new QHBoxLayout(widgetLeft);
        horizontalLayout_4->setSpacing(6);
        horizontalLayout_4->setContentsMargins(0, 0, 0, 0);
        horizontalLayout_4->setObjectName(QString::fromUtf8("horizontalLayout_4"));
        horizontalLayout_4->setContentsMargins(0, 0, 0, 0);
        verticalLayout_6 = new QVBoxLayout();
        verticalLayout_6->setSpacing(0);
        verticalLayout_6->setObjectName(QString::fromUtf8("verticalLayout_6"));
        verticalLayout_6->setSizeConstraint(QLayout::SetDefaultConstraint);
        verticalLayout_6->setContentsMargins(0, 40, 0, 0);
        verticalLayout = new QVBoxLayout();
        verticalLayout->setSpacing(6);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        horizontalLayout_6 = new QHBoxLayout();
        horizontalLayout_6->setSpacing(6);
        horizontalLayout_6->setObjectName(QString::fromUtf8("horizontalLayout_6"));
        label_8 = new QLabel(widgetLeft);
        label_8->setObjectName(QString::fromUtf8("label_8"));
        label_8->setMinimumSize(QSize(70, 68));
        label_8->setMaximumSize(QSize(70, 68));
        label_8->setStyleSheet(QString::fromUtf8("font-size:12px;\n"
"font-family:PingFangHK-Light;\n"
"font-weight:300;\n"
"color:rgba(255,255,255,0.85);\n"
"border-image:transparent;\n"
"background:transparent;\n"
"border:0px solid rgba(228,228,228,1);"));
        label_8->setPixmap(QPixmap(QString::fromUtf8(":/Resource/Images/logo.png")));
        label_8->setScaledContents(false);
        label_8->setAlignment(Qt::AlignCenter);

        horizontalLayout_6->addWidget(label_8);


        verticalLayout->addLayout(horizontalLayout_6);

        horizontalLayout_7 = new QHBoxLayout();
        horizontalLayout_7->setSpacing(6);
        horizontalLayout_7->setObjectName(QString::fromUtf8("horizontalLayout_7"));
        label = new QLabel(widgetLeft);
        label->setObjectName(QString::fromUtf8("label"));
        label->setMinimumSize(QSize(0, 35));
        label->setMaximumSize(QSize(210, 35));
        label->setLayoutDirection(Qt::LeftToRight);
        label->setStyleSheet(QString::fromUtf8("font-size:25px;\n"
"font-family:PingFangHK-Light;\n"
"font-weight:500;\n"
"color:rgba(255,255,255,0.85);\n"
"border-image:transparent;\n"
"background:transparent;\n"
"border:0px solid rgba(228,228,228,1);"));
        label->setAlignment(Qt::AlignCenter);
        label->setMargin(0);

        horizontalLayout_7->addWidget(label);


        verticalLayout->addLayout(horizontalLayout_7);


        verticalLayout_6->addLayout(verticalLayout);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setSpacing(8);
        horizontalLayout_2->setObjectName(QString::fromUtf8("horizontalLayout_2"));
        horizontalLayout_2->setContentsMargins(24, 40, -1, -1);
        label_11 = new QLabel(widgetLeft);
        label_11->setObjectName(QString::fromUtf8("label_11"));
        label_11->setMinimumSize(QSize(32, 32));
        label_11->setMaximumSize(QSize(32, 32));
        label_11->setStyleSheet(QString::fromUtf8("font-size:12px;\n"
"font-family:PingFangHK-Light;\n"
"font-weight:300;\n"
"color:rgba(255,255,255,0.85);\n"
"border-image:transparent;\n"
"background:transparent;\n"
"border:0px solid rgba(228,228,228,1);"));
        label_11->setPixmap(QPixmap(QString::fromUtf8(":/Resource/Images/icon1.png")));
        label_11->setAlignment(Qt::AlignCenter);

        horizontalLayout_2->addWidget(label_11);

        verticalLayout_2 = new QVBoxLayout();
        verticalLayout_2->setSpacing(0);
        verticalLayout_2->setObjectName(QString::fromUtf8("verticalLayout_2"));
        label_7 = new QLabel(widgetLeft);
        label_7->setObjectName(QString::fromUtf8("label_7"));
        label_7->setMinimumSize(QSize(0, 20));
        label_7->setMaximumSize(QSize(200, 20));
        label_7->setStyleSheet(QString::fromUtf8("font-size:14px;\n"
"font-weight:400;\n"
"line-height:20px;\n"
"color:rgba(255,255,255,1);\n"
"border-image:none;\n"
"background:none;\n"
"border:none;"));

        verticalLayout_2->addWidget(label_7);


        horizontalLayout_2->addLayout(verticalLayout_2);


        verticalLayout_6->addLayout(horizontalLayout_2);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setSpacing(8);
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        horizontalLayout->setContentsMargins(24, 40, -1, -1);
        label_10 = new QLabel(widgetLeft);
        label_10->setObjectName(QString::fromUtf8("label_10"));
        label_10->setMinimumSize(QSize(32, 32));
        label_10->setMaximumSize(QSize(32, 32));
        label_10->setStyleSheet(QString::fromUtf8("font-size:12px;\n"
"font-family:PingFangHK-Light;\n"
"font-weight:300;\n"
"color:rgba(255,255,255,0.85);\n"
"border-image:transparent;\n"
"background:transparent;\n"
"border:0px solid rgba(228,228,228,1);"));
        label_10->setPixmap(QPixmap(QString::fromUtf8(":/Resource/Images/icon2.png")));
        label_10->setAlignment(Qt::AlignCenter);

        horizontalLayout->addWidget(label_10);

        verticalLayout_3 = new QVBoxLayout();
        verticalLayout_3->setSpacing(0);
        verticalLayout_3->setObjectName(QString::fromUtf8("verticalLayout_3"));
        label_4 = new QLabel(widgetLeft);
        label_4->setObjectName(QString::fromUtf8("label_4"));
        sizePolicy.setHeightForWidth(label_4->sizePolicy().hasHeightForWidth());
        label_4->setSizePolicy(sizePolicy);
        label_4->setMinimumSize(QSize(0, 20));
        label_4->setMaximumSize(QSize(200, 20));
        label_4->setStyleSheet(QString::fromUtf8("font-size:14px;\n"
"font-weight:400;\n"
"line-height:20px;\n"
"color:rgba(255,255,255,1);\n"
"border-image:none;\n"
"background:none;\n"
"border:none;"));

        verticalLayout_3->addWidget(label_4);


        horizontalLayout->addLayout(verticalLayout_3);


        verticalLayout_6->addLayout(horizontalLayout);

        horizontalLayout_3 = new QHBoxLayout();
        horizontalLayout_3->setSpacing(8);
        horizontalLayout_3->setObjectName(QString::fromUtf8("horizontalLayout_3"));
        horizontalLayout_3->setContentsMargins(24, 40, -1, -1);
        label_9 = new QLabel(widgetLeft);
        label_9->setObjectName(QString::fromUtf8("label_9"));
        label_9->setMinimumSize(QSize(23, 32));
        label_9->setMaximumSize(QSize(32, 32));
        label_9->setStyleSheet(QString::fromUtf8("font-size:12px;\n"
"font-family:PingFangHK-Light;\n"
"font-weight:300;\n"
"color:rgba(255,255,255,0.85);\n"
"border-image:transparent;\n"
"background:transparent;\n"
"border:0px solid rgba(228,228,228,1);"));
        label_9->setPixmap(QPixmap(QString::fromUtf8(":/Resource/Images/icon3.png")));
        label_9->setAlignment(Qt::AlignCenter);

        horizontalLayout_3->addWidget(label_9);

        verticalLayout_4 = new QVBoxLayout();
        verticalLayout_4->setSpacing(0);
        verticalLayout_4->setObjectName(QString::fromUtf8("verticalLayout_4"));
        label_2 = new QLabel(widgetLeft);
        label_2->setObjectName(QString::fromUtf8("label_2"));
        label_2->setMinimumSize(QSize(0, 20));
        label_2->setMaximumSize(QSize(200, 20));
        label_2->setStyleSheet(QString::fromUtf8("font-size:14px;\n"
"font-weight:400;\n"
"line-height:20px;\n"
"color:rgba(255,255,255,1);\n"
"border-image:none;\n"
"background:none;\n"
"border:none;"));

        verticalLayout_4->addWidget(label_2);


        horizontalLayout_3->addLayout(verticalLayout_4);


        verticalLayout_6->addLayout(horizontalLayout_3);

        verticalSpacer_3 = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout_6->addItem(verticalSpacer_3);


        horizontalLayout_4->addLayout(verticalLayout_6);


        horizontalLayout_8->addWidget(widgetLeft);

        widgetRight = new QWidget(StartUpWindow);
        widgetRight->setObjectName(QString::fromUtf8("widgetRight"));
        widgetRight->setMinimumSize(QSize(383, 418));
        widgetRight->setMaximumSize(QSize(383, 418));
        widgetRight->setStyleSheet(QString::fromUtf8("border:none;"));
        horizontalLayout_5 = new QHBoxLayout(widgetRight);
        horizontalLayout_5->setSpacing(0);
        horizontalLayout_5->setContentsMargins(0, 0, 0, 0);
        horizontalLayout_5->setObjectName(QString::fromUtf8("horizontalLayout_5"));
        horizontalLayout_5->setSizeConstraint(QLayout::SetDefaultConstraint);
        horizontalLayout_5->setContentsMargins(0, 0, 0, 0);
        verticalLayout_7 = new QVBoxLayout();
        verticalLayout_7->setSpacing(0);
        verticalLayout_7->setObjectName(QString::fromUtf8("verticalLayout_7"));
        verticalLayout_7->setContentsMargins(-1, 0, -1, 5);
        widgetTitle = new QWidget(widgetRight);
        widgetTitle->setObjectName(QString::fromUtf8("widgetTitle"));
        widgetTitle->setEnabled(true);
        sizePolicy.setHeightForWidth(widgetTitle->sizePolicy().hasHeightForWidth());
        widgetTitle->setSizePolicy(sizePolicy);
        widgetTitle->setMinimumSize(QSize(0, 40));
        widgetTitle->setMaximumSize(QSize(475, 40));
        horizontalLayout_9 = new QHBoxLayout(widgetTitle);
        horizontalLayout_9->setSpacing(0);
        horizontalLayout_9->setContentsMargins(0, 0, 0, 0);
        horizontalLayout_9->setObjectName(QString::fromUtf8("horizontalLayout_9"));
        horizontalLayout_9->setContentsMargins(0, 0, 1, 0);
        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_9->addItem(horizontalSpacer);

        widgetSize = new QWidget(widgetTitle);
        widgetSize->setObjectName(QString::fromUtf8("widgetSize"));
        widgetSize->setMinimumSize(QSize(50, 40));
        widgetSize->setMaximumSize(QSize(80, 40));
        horizontalLayout_10 = new QHBoxLayout(widgetSize);
        horizontalLayout_10->setSpacing(0);
        horizontalLayout_10->setContentsMargins(0, 0, 0, 0);
        horizontalLayout_10->setObjectName(QString::fromUtf8("horizontalLayout_10"));
        horizontalLayout_10->setContentsMargins(0, 0, 1, 0);
        btnMin = new QPushButton(widgetSize);
        btnMin->setObjectName(QString::fromUtf8("btnMin"));
        btnMin->setMinimumSize(QSize(35, 35));
        btnMin->setMaximumSize(QSize(35, 35));
        btnMin->setAutoDefault(false);

        horizontalLayout_10->addWidget(btnMin);

        btnExit = new QPushButton(widgetSize);
        btnExit->setObjectName(QString::fromUtf8("btnExit"));
        btnExit->setMinimumSize(QSize(35, 35));
        btnExit->setMaximumSize(QSize(35, 35));
        btnExit->setAutoDefault(false);

        horizontalLayout_10->addWidget(btnExit);


        horizontalLayout_9->addWidget(widgetSize, 0, Qt::AlignRight|Qt::AlignTop);


        verticalLayout_7->addWidget(widgetTitle);

        layouLoginMenu = new QHBoxLayout();
        layouLoginMenu->setSpacing(12);
        layouLoginMenu->setObjectName(QString::fromUtf8("layouLoginMenu"));
        layouLoginMenu->setContentsMargins(-1, 30, -1, -1);
        horizontalSpacer_2 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        layouLoginMenu->addItem(horizontalSpacer_2);

        btnMicroMsgMode = new QPushButton(widgetRight);
        btnMicroMsgMode->setObjectName(QString::fromUtf8("btnMicroMsgMode"));
        btnMicroMsgMode->setMinimumSize(QSize(96, 22));
        btnMicroMsgMode->setMaximumSize(QSize(96, 22));
        btnMicroMsgMode->setCheckable(true);
        btnMicroMsgMode->setChecked(true);
        btnMicroMsgMode->setAutoDefault(false);

        layouLoginMenu->addWidget(btnMicroMsgMode);

        line_2 = new QFrame(widgetRight);
        line_2->setObjectName(QString::fromUtf8("line_2"));
        line_2->setMinimumSize(QSize(1, 18));
        line_2->setMaximumSize(QSize(1, 18));
        line_2->setStyleSheet(QString::fromUtf8("background:rgba(208,208,208,1);\n"
"border:1px solid rgba(151,151,151,1);"));
        line_2->setFrameShape(QFrame::VLine);
        line_2->setFrameShadow(QFrame::Sunken);

        layouLoginMenu->addWidget(line_2);

        btnPwdMode = new QPushButton(widgetRight);
        btnPwdMode->setObjectName(QString::fromUtf8("btnPwdMode"));
        btnPwdMode->setMinimumSize(QSize(64, 22));
        btnPwdMode->setMaximumSize(QSize(64, 22));
        btnPwdMode->setCheckable(true);
        btnPwdMode->setAutoDefault(false);

        layouLoginMenu->addWidget(btnPwdMode);

        line = new QFrame(widgetRight);
        line->setObjectName(QString::fromUtf8("line"));
        line->setMinimumSize(QSize(1, 18));
        line->setMaximumSize(QSize(1, 18));
        line->setStyleSheet(QString::fromUtf8("background:rgba(208,208,208,1);\n"
"border:1px solid rgba(151,151,151,1);"));
        line->setFrameShape(QFrame::VLine);
        line->setFrameShadow(QFrame::Sunken);

        layouLoginMenu->addWidget(line);

        btnSmsMode = new QPushButton(widgetRight);
        btnSmsMode->setObjectName(QString::fromUtf8("btnSmsMode"));
        btnSmsMode->setMinimumSize(QSize(80, 22));
        btnSmsMode->setMaximumSize(QSize(80, 22));
        btnSmsMode->setCheckable(true);
        btnSmsMode->setAutoDefault(false);

        layouLoginMenu->addWidget(btnSmsMode);

        horizontalSpacer_3 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        layouLoginMenu->addItem(horizontalSpacer_3);


        verticalLayout_7->addLayout(layouLoginMenu);

        verticalLayout_5 = new QVBoxLayout();
        verticalLayout_5->setSpacing(6);
        verticalLayout_5->setObjectName(QString::fromUtf8("verticalLayout_5"));
        verticalSpacer_2 = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout_5->addItem(verticalSpacer_2);

        loginWidget = new QHBoxLayout();
        loginWidget->setSpacing(6);
        loginWidget->setObjectName(QString::fromUtf8("loginWidget"));

        verticalLayout_5->addLayout(loginWidget);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout_5->addItem(verticalSpacer);

        verticalLayout_5->setStretch(0, 10);
        verticalLayout_5->setStretch(2, 30);

        verticalLayout_7->addLayout(verticalLayout_5);

        registorLayout = new QHBoxLayout();
        registorLayout->setSpacing(6);
        registorLayout->setObjectName(QString::fromUtf8("registorLayout"));
        registorLayout->setContentsMargins(0, -1, 25, 10);
        labelFlag = new QLabel(widgetRight);
        labelFlag->setObjectName(QString::fromUtf8("labelFlag"));
        labelFlag->setStyleSheet(QString::fromUtf8("border:none;"));

        registorLayout->addWidget(labelFlag);

        btnRegistor = new QPushButton(widgetRight);
        btnRegistor->setObjectName(QString::fromUtf8("btnRegistor"));
        btnRegistor->setMaximumSize(QSize(80, 25));
        btnRegistor->setCursor(QCursor(Qt::PointingHandCursor));
        btnRegistor->setLayoutDirection(Qt::LeftToRight);
        btnRegistor->setStyleSheet(QString::fromUtf8("font-weight:400;\n"
"color:rgba(0,0,0,0.85);\n"
"line-height:20px;"));
        btnRegistor->setIconSize(QSize(0, 0));
        btnRegistor->setAutoDefault(false);
        btnRegistor->setFlat(true);

        registorLayout->addWidget(btnRegistor, 0, Qt::AlignRight);


        verticalLayout_7->addLayout(registorLayout);

        verticalLayout_7->setStretch(0, 40);
        verticalLayout_7->setStretch(1, 70);
        verticalLayout_7->setStretch(2, 495);
        verticalLayout_7->setStretch(3, 60);

        horizontalLayout_5->addLayout(verticalLayout_7);


        horizontalLayout_8->addWidget(widgetRight);

        horizontalLayout_8->setStretch(0, 300);
        horizontalLayout_8->setStretch(1, 475);
        QWidget::setTabOrder(btnRegistor, btnMicroMsgMode);
        QWidget::setTabOrder(btnMicroMsgMode, btnPwdMode);
        QWidget::setTabOrder(btnPwdMode, btnSmsMode);
        QWidget::setTabOrder(btnSmsMode, btnMin);
        QWidget::setTabOrder(btnMin, btnExit);

        retranslateUi(StartUpWindow);

        QMetaObject::connectSlotsByName(StartUpWindow);
    } // setupUi

    void retranslateUi(QDialog *StartUpWindow)
    {
        StartUpWindow->setWindowTitle(QApplication::translate("StartUpWindow", "\347\231\273\345\275\225", nullptr));
        label_8->setText(QString());
        label->setText(QApplication::translate("StartUpWindow", "\346\237\240\346\252\254\344\272\221\345\257\274\350\264\246\345\267\245\345\205\267", nullptr));
        label_11->setText(QString());
        label_7->setText(QApplication::translate("StartUpWindow", "\344\270\200\351\224\256\346\223\215\344\275\234\357\274\214\345\277\253\351\200\237\350\277\201\347\247\273", nullptr));
        label_10->setText(QString());
        label_4->setText(QApplication::translate("StartUpWindow", "\346\223\215\344\275\234\347\225\214\351\235\242\347\256\200\345\215\225\346\230\223\347\224\250", nullptr));
        label_9->setText(QString());
        label_2->setText(QApplication::translate("StartUpWindow", "\345\205\250\346\265\201\347\250\213\350\207\252\345\212\250\345\214\226\346\223\215\344\275\234", nullptr));
        btnMin->setText(QString());
        btnExit->setText(QString());
        btnMicroMsgMode->setText(QApplication::translate("StartUpWindow", "\345\276\256\344\277\241\346\211\253\347\240\201\347\231\273\345\275\225", nullptr));
        btnPwdMode->setText(QApplication::translate("StartUpWindow", "\345\257\206\347\240\201\347\231\273\345\275\225", nullptr));
        btnSmsMode->setText(QApplication::translate("StartUpWindow", "\351\252\214\350\257\201\347\240\201\347\231\273\345\275\225", nullptr));
        labelFlag->setText(QString());
        btnRegistor->setText(QApplication::translate("StartUpWindow", "\350\264\246\345\217\267\346\263\250\345\206\214>", nullptr));
    } // retranslateUi

};

namespace Ui {
    class StartUpWindow: public Ui_StartUpWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_STARTUPWINDOW_H
