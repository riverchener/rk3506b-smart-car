/********************************************************************************
** Form generated from reading UI file 'Audio.ui'
**
** Created by: Qt User Interface Compiler version 5.15.18
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_AUDIO_H
#define UI_AUDIO_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_Audio
{
public:
    QWidget *widget_2;
    QWidget *widget;
    QWidget *widget_3;
    QWidget *layoutWidget;
    QHBoxLayout *horizontalLayout_6;
    QSpacerItem *horizontalSpacer_16;
    QVBoxLayout *verticalLayout;
    QHBoxLayout *horizontalLayout_2;
    QPushButton *pushButton;
    QSpacerItem *horizontalSpacer_6;
    QLabel *label_5;
    QListWidget *listWidget;
    QLabel *label_4;
    QSpacerItem *verticalSpacer_2;
    QVBoxLayout *verticalLayout_3;
    QSpacerItem *verticalSpacer;
    QHBoxLayout *horizontalLayout_5;
    QSpacerItem *horizontalSpacer_12;
    QVBoxLayout *verticalLayout_2;
    QSpacerItem *verticalSpacer_3;
    QSlider *verticalSlider;
    QLabel *label;
    QSpacerItem *verticalSpacer_4;
    QSpacerItem *horizontalSpacer_14;
    QHBoxLayout *horizontalLayout_3;
    QSpacerItem *horizontalSpacer_10;
    QSlider *horizontalSlider;
    QSpacerItem *horizontalSpacer_11;
    QHBoxLayout *horizontalLayout_4;
    QSpacerItem *horizontalSpacer_7;
    QLabel *label_2;
    QSpacerItem *horizontalSpacer_8;
    QLabel *label_3;
    QSpacerItem *horizontalSpacer_9;
    QSpacerItem *verticalSpacer_5;
    QHBoxLayout *horizontalLayout;
    QSpacerItem *horizontalSpacer_5;
    QPushButton *btn_loop;
    QSpacerItem *horizontalSpacer;
    QPushButton *btn_pre;
    QSpacerItem *horizontalSpacer_2;
    QPushButton *btn_play;
    QSpacerItem *horizontalSpacer_3;
    QPushButton *btn_next;
    QSpacerItem *horizontalSpacer_4;
    QPushButton *pushButton_2;
    QSpacerItem *horizontalSpacer_15;
    QSpacerItem *verticalSpacer_6;

    void setupUi(QWidget *Audio)
    {
        if (Audio->objectName().isEmpty())
            Audio->setObjectName(QString::fromUtf8("Audio"));
        Audio->resize(1000, 600);
        Audio->setMinimumSize(QSize(1000, 600));
        Audio->setMaximumSize(QSize(1000, 600));
        Audio->setStyleSheet(QString::fromUtf8("QWidget {\n"
"background-color:#222233;\n"
"}"));
        widget_2 = new QWidget(Audio);
        widget_2->setObjectName(QString::fromUtf8("widget_2"));
        widget_2->setGeometry(QRect(740, 20, 150, 160));
        widget_2->setStyleSheet(QString::fromUtf8("QWidget {\n"
"border:none;\n"
"background-color: transparent;\n"
"}\n"
""));
        widget = new QWidget(Audio);
        widget->setObjectName(QString::fromUtf8("widget"));
        widget->setGeometry(QRect(650, 120, 240, 240));
        widget->setMinimumSize(QSize(240, 240));
        widget->setMaximumSize(QSize(240, 240));
        widget->setStyleSheet(QString::fromUtf8("QWidget {\n"
"border:none;\n"
"border-radius:120px;\n"
"	background-color: rgb(32, 74, 135);\n"
"}\n"
""));
        widget_3 = new QWidget(widget);
        widget_3->setObjectName(QString::fromUtf8("widget_3"));
        widget_3->setGeometry(QRect(0, 0, 240, 240));
        widget_3->setMinimumSize(QSize(240, 240));
        widget_3->setMaximumSize(QSize(240, 240));
        widget_3->setStyleSheet(QString::fromUtf8("QWidget {\n"
"border:none;\n"
"border-radius:120px;\n"
"background-color: transparent;\n"
"\n"
"}\n"
""));
        layoutWidget = new QWidget(Audio);
        layoutWidget->setObjectName(QString::fromUtf8("layoutWidget"));
        layoutWidget->setGeometry(QRect(0, 0, 1001, 601));
        horizontalLayout_6 = new QHBoxLayout(layoutWidget);
        horizontalLayout_6->setSpacing(6);
        horizontalLayout_6->setContentsMargins(11, 11, 11, 11);
        horizontalLayout_6->setObjectName(QString::fromUtf8("horizontalLayout_6"));
        horizontalLayout_6->setContentsMargins(0, 0, 0, 0);
        horizontalSpacer_16 = new QSpacerItem(13, 20, QSizePolicy::Fixed, QSizePolicy::Minimum);

        horizontalLayout_6->addItem(horizontalSpacer_16);

        verticalLayout = new QVBoxLayout();
        verticalLayout->setSpacing(10);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setSpacing(6);
        horizontalLayout_2->setObjectName(QString::fromUtf8("horizontalLayout_2"));
        pushButton = new QPushButton(layoutWidget);
        pushButton->setObjectName(QString::fromUtf8("pushButton"));
        pushButton->setMinimumSize(QSize(40, 40));
        pushButton->setMaximumSize(QSize(40, 40));
        pushButton->setStyleSheet(QString::fromUtf8("QPushButton {\n"
"border:none;\n"
"background-color:transparent;\n"
"border-radius:10px;\n"
"	border-image: url(:/images/return.png);\n"
"}\n"
"\n"
"QPushButton::pressed {\n"
"	border-image: url(:/images/return2.png);\n"
"}"));

        horizontalLayout_2->addWidget(pushButton);

        horizontalSpacer_6 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_2->addItem(horizontalSpacer_6);

        label_5 = new QLabel(layoutWidget);
        label_5->setObjectName(QString::fromUtf8("label_5"));
        label_5->setMinimumSize(QSize(100, 40));
        label_5->setMaximumSize(QSize(100, 40));
        label_5->setStyleSheet(QString::fromUtf8("QLabel {\n"
"border:none;\n"
"background-color:transparent;\n"
"font-size:20px;\n"
"	color: rgb(59, 150, 240);\n"
"}"));

        horizontalLayout_2->addWidget(label_5);


        verticalLayout->addLayout(horizontalLayout_2);

        listWidget = new QListWidget(layoutWidget);
        listWidget->setObjectName(QString::fromUtf8("listWidget"));
        listWidget->setStyleSheet(QString::fromUtf8("QListWidget {\n"
"border:none;\n"
"background-color:transparent;\n"
"}\n"
"QListWidget::verticalScrollBar {\n"
"            border:none;\n"
"            background:transparent;\n"
"        }\n"
"        QListWidget::item {\n"
"            color:white;\n"
"            height:60px;\n"
"        }\n"
"        QListWidget::verticalScrollBar:handle {\n"
"            border: none;\n"
"            background-color:#aaaaaa;\n"
"            width: 20px; /* \350\256\276\347\275\256\346\273\232\345\212\250\346\235\241\345\256\275\345\272\246 */\n"
"        }\n"
"        QListWidget::verticalScrollBar:add-page,\n"
"        QListWidget::verticalScrollBar:sub-page {\n"
"            height: 0px; /* \351\232\220\350\227\217\346\214\211\351\222\256 */\n"
"            border: none;\n"
"            background: none;\n"
"        }\n"
"\n"
""));
        listWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        verticalLayout->addWidget(listWidget);

        label_4 = new QLabel(layoutWidget);
        label_4->setObjectName(QString::fromUtf8("label_4"));
        label_4->setMinimumSize(QSize(0, 80));
        label_4->setMaximumSize(QSize(16777215, 80));
        QFont font;
        font.setPointSize(13);
        label_4->setFont(font);
        label_4->setStyleSheet(QString::fromUtf8("QLabel {\n"
"	color: rgb(59, 150, 240);\n"
"border:2px solid white;\n"
"border-radius:20px;\n"
"background-color:transparent;\n"
"}"));
        label_4->setAlignment(Qt::AlignCenter);

        verticalLayout->addWidget(label_4);

        verticalSpacer_2 = new QSpacerItem(20, 25, QSizePolicy::Minimum, QSizePolicy::Fixed);

        verticalLayout->addItem(verticalSpacer_2);


        horizontalLayout_6->addLayout(verticalLayout);

        verticalLayout_3 = new QVBoxLayout();
        verticalLayout_3->setSpacing(6);
        verticalLayout_3->setObjectName(QString::fromUtf8("verticalLayout_3"));
        verticalSpacer = new QSpacerItem(20, 20, QSizePolicy::Minimum, QSizePolicy::Fixed);

        verticalLayout_3->addItem(verticalSpacer);

        horizontalLayout_5 = new QHBoxLayout();
        horizontalLayout_5->setSpacing(6);
        horizontalLayout_5->setObjectName(QString::fromUtf8("horizontalLayout_5"));
        horizontalSpacer_12 = new QSpacerItem(100, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_5->addItem(horizontalSpacer_12);

        verticalLayout_2 = new QVBoxLayout();
        verticalLayout_2->setSpacing(6);
        verticalLayout_2->setObjectName(QString::fromUtf8("verticalLayout_2"));
        verticalSpacer_3 = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout_2->addItem(verticalSpacer_3);

        verticalSlider = new QSlider(layoutWidget);
        verticalSlider->setObjectName(QString::fromUtf8("verticalSlider"));
        verticalSlider->setMinimumSize(QSize(30, 300));
        verticalSlider->setMaximumSize(QSize(30, 300));
        verticalSlider->setSliderPosition(50);
        verticalSlider->setOrientation(Qt::Vertical);

        verticalLayout_2->addWidget(verticalSlider);

        label = new QLabel(layoutWidget);
        label->setObjectName(QString::fromUtf8("label"));
        label->setMinimumSize(QSize(24, 24));
        label->setMaximumSize(QSize(24, 24));
        label->setStyleSheet(QString::fromUtf8("QLabel {\n"
"border:none;\n"
"background-color:transparent;\n"
"	border-image: url(:/images/btn_volume2.png);\n"
"}"));

        verticalLayout_2->addWidget(label);

        verticalSpacer_4 = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout_2->addItem(verticalSpacer_4);


        horizontalLayout_5->addLayout(verticalLayout_2);

        horizontalSpacer_14 = new QSpacerItem(13, 20, QSizePolicy::Fixed, QSizePolicy::Minimum);

        horizontalLayout_5->addItem(horizontalSpacer_14);


        verticalLayout_3->addLayout(horizontalLayout_5);

        horizontalLayout_3 = new QHBoxLayout();
        horizontalLayout_3->setSpacing(6);
        horizontalLayout_3->setObjectName(QString::fromUtf8("horizontalLayout_3"));
        horizontalSpacer_10 = new QSpacerItem(40, 20, QSizePolicy::Fixed, QSizePolicy::Minimum);

        horizontalLayout_3->addItem(horizontalSpacer_10);

        horizontalSlider = new QSlider(layoutWidget);
        horizontalSlider->setObjectName(QString::fromUtf8("horizontalSlider"));
        horizontalSlider->setMinimumSize(QSize(350, 15));
        horizontalSlider->setOrientation(Qt::Horizontal);

        horizontalLayout_3->addWidget(horizontalSlider);

        horizontalSpacer_11 = new QSpacerItem(40, 20, QSizePolicy::Fixed, QSizePolicy::Minimum);

        horizontalLayout_3->addItem(horizontalSpacer_11);


        verticalLayout_3->addLayout(horizontalLayout_3);

        horizontalLayout_4 = new QHBoxLayout();
        horizontalLayout_4->setSpacing(6);
        horizontalLayout_4->setObjectName(QString::fromUtf8("horizontalLayout_4"));
        horizontalSpacer_7 = new QSpacerItem(40, 20, QSizePolicy::Fixed, QSizePolicy::Minimum);

        horizontalLayout_4->addItem(horizontalSpacer_7);

        label_2 = new QLabel(layoutWidget);
        label_2->setObjectName(QString::fromUtf8("label_2"));
        label_2->setStyleSheet(QString::fromUtf8("QLabel {\n"
"color:white;\n"
"border:none;\n"
"background-color:transparent;\n"
"}"));
        label_2->setAlignment(Qt::AlignCenter);

        horizontalLayout_4->addWidget(label_2);

        horizontalSpacer_8 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_4->addItem(horizontalSpacer_8);

        label_3 = new QLabel(layoutWidget);
        label_3->setObjectName(QString::fromUtf8("label_3"));
        label_3->setStyleSheet(QString::fromUtf8("QLabel {\n"
"color:white;\n"
"border:none;\n"
"background-color:transparent;\n"
"}"));
        label_3->setAlignment(Qt::AlignCenter);

        horizontalLayout_4->addWidget(label_3);

        horizontalSpacer_9 = new QSpacerItem(40, 20, QSizePolicy::Fixed, QSizePolicy::Minimum);

        horizontalLayout_4->addItem(horizontalSpacer_9);


        verticalLayout_3->addLayout(horizontalLayout_4);

        verticalSpacer_5 = new QSpacerItem(17, 18, QSizePolicy::Minimum, QSizePolicy::Fixed);

        verticalLayout_3->addItem(verticalSpacer_5);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setSpacing(6);
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        horizontalSpacer_5 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer_5);

        btn_loop = new QPushButton(layoutWidget);
        btn_loop->setObjectName(QString::fromUtf8("btn_loop"));
        btn_loop->setMinimumSize(QSize(24, 24));
        btn_loop->setMaximumSize(QSize(24, 24));
        btn_loop->setStyleSheet(QString::fromUtf8("QPushButton {\n"
"border:none;\n"
"background-color:transparent;\n"
"border-radius:15px;\n"
"border-image: url(:/images/btn_listcycle1.png);\n"
"}\n"
"\n"
"QPushButton::pressed {\n"
"border:none;\n"
"background-color:transparent;\n"
"border-image: url(:/images/btn_listcycle2.png);\n"
"}\n"
"\n"
"QPushButton::checked {\n"
"border:none;\n"
"background-color:transparent;\n"
"	border-image: url(:/images/single_cycle.png);\n"
"}\n"
"\n"
"QPushButton::checked:pressed {\n"
"border:none;\n"
"background-color:transparent;\n"
"	border-image: url(:/images/single_cycle2.png);\n"
"}"));
        btn_loop->setCheckable(true);

        horizontalLayout->addWidget(btn_loop);

        horizontalSpacer = new QSpacerItem(20, 20, QSizePolicy::Fixed, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer);

        btn_pre = new QPushButton(layoutWidget);
        btn_pre->setObjectName(QString::fromUtf8("btn_pre"));
        btn_pre->setMinimumSize(QSize(60, 60));
        btn_pre->setStyleSheet(QString::fromUtf8("QPushButton {\n"
"border:none;\n"
"background-color:transparent;\n"
"border-radius:15px;\n"
"	border-image: url(:/images/btn_previous1.png);\n"
"}\n"
"\n"
"QPushButton::pressed {\n"
"	border-image: url(:/images/btn_previous2.png);\n"
"}\n"
""));

        horizontalLayout->addWidget(btn_pre);

        horizontalSpacer_2 = new QSpacerItem(20, 20, QSizePolicy::Fixed, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer_2);

        btn_play = new QPushButton(layoutWidget);
        btn_play->setObjectName(QString::fromUtf8("btn_play"));
        btn_play->setMinimumSize(QSize(60, 60));
        btn_play->setStyleSheet(QString::fromUtf8("QPushButton {\n"
"border:none;\n"
"background-color:transparent;\n"
"	border-image: url(:/images/btn_play1.png);\n"
"}	\n"
"\n"
"QPushButton::pressed {\n"
"border:none;\n"
"background-color:transparent;\n"
"	border-image: url(:/images/btn_play2.png);\n"
"}\n"
"\n"
"QPushButton::checked {\n"
"border:none;\n"
"background-color:transparent;\n"
"border-image: url(:/images/btn_pause1.png);\n"
"}\n"
"\n"
"QPushButton::checked:pressed {\n"
"border:none;\n"
"background-color:transparent;\n"
"	border-image: url(:/images/btn_pause2.png);\n"
"}"));
        btn_play->setCheckable(true);

        horizontalLayout->addWidget(btn_play);

        horizontalSpacer_3 = new QSpacerItem(20, 20, QSizePolicy::Fixed, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer_3);

        btn_next = new QPushButton(layoutWidget);
        btn_next->setObjectName(QString::fromUtf8("btn_next"));
        btn_next->setMinimumSize(QSize(60, 60));
        btn_next->setStyleSheet(QString::fromUtf8("QPushButton {\n"
"border:none;\n"
"background-color:transparent;\n"
"border-radius:15px;\n"
"	border-image: url(:/images/btn_next1.png);\n"
"}\n"
"\n"
"QPushButton::pressed {\n"
"	border-image: url(:/images/btn_next2.png);\n"
"}\n"
""));

        horizontalLayout->addWidget(btn_next);

        horizontalSpacer_4 = new QSpacerItem(20, 28, QSizePolicy::Fixed, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer_4);

        pushButton_2 = new QPushButton(layoutWidget);
        pushButton_2->setObjectName(QString::fromUtf8("pushButton_2"));
        pushButton_2->setMinimumSize(QSize(24, 24));
        pushButton_2->setMaximumSize(QSize(24, 24));
        pushButton_2->setStyleSheet(QString::fromUtf8("QPushButton {\n"
"border:none;\n"
"background-color:transparent;\n"
"border-radius:15px;\n"
"	border-image: url(:/images/btn_favorite_no.png);\n"
"}\n"
"\n"
"QPushButton::checked {\n"
"border:none;\n"
"background-color:transparent;\n"
"border-radius:15px;\n"
"	border-image: url(:/images/btn_favorite_yes.png);\n"
"}"));
        pushButton_2->setCheckable(true);

        horizontalLayout->addWidget(pushButton_2);

        horizontalSpacer_15 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer_15);


        verticalLayout_3->addLayout(horizontalLayout);

        verticalSpacer_6 = new QSpacerItem(17, 18, QSizePolicy::Minimum, QSizePolicy::Fixed);

        verticalLayout_3->addItem(verticalSpacer_6);


        horizontalLayout_6->addLayout(verticalLayout_3);

        horizontalLayout_6->setStretch(1, 3);
        horizontalLayout_6->setStretch(2, 2);
        layoutWidget->raise();
        widget->raise();
        widget_2->raise();

        retranslateUi(Audio);

        QMetaObject::connectSlotsByName(Audio);
    } // setupUi

    void retranslateUi(QWidget *Audio)
    {
        Audio->setWindowTitle(QCoreApplication::translate("Audio", "Widget", nullptr));
        pushButton->setText(QString());
        label_5->setText(QCoreApplication::translate("Audio", "\351\237\263\344\271\220\346\222\255\346\224\276\345\231\250", nullptr));
        label_4->setText(QString());
        label->setText(QString());
        label_2->setText(QCoreApplication::translate("Audio", "00:00", nullptr));
        label_3->setText(QCoreApplication::translate("Audio", "00:00", nullptr));
        btn_loop->setText(QString());
        btn_pre->setText(QString());
        btn_play->setText(QString());
        btn_next->setText(QString());
        pushButton_2->setText(QString());
    } // retranslateUi

};

namespace Ui {
    class Audio: public Ui_Audio {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_AUDIO_H
