/********************************************************************************
** Form generated from reading UI file 'hardware.ui'
**
** Created by: Qt User Interface Compiler version 5.15.18
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_HARDWARE_H
#define UI_HARDWARE_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_Hardware
{
public:
    QWidget *centralwidget;
    QWidget *widget;
    QLabel *Templabel;
    QLabel *Temp_label;
    QLabel *label;
    QLabel *Temp_label_2;
    QWidget *widget_2;
    QLabel *label_2;
    QLabel *Temp_label_4;
    QLabel *Humilabel;
    QLabel *Humi_label;
    QWidget *widget_3;
    QLabel *label_3;
    QLabel *Temp_label_5;
    QLabel *Sr04labal;
    QLabel *Sr04_label;
    QWidget *widget_4;
    QLabel *buzzer;
    QLabel *led;
    QWidget *widget_5;
    QLabel *label_5;
    QLabel *Temp_label_7;
    QLabel *Humilabel_3;
    QLabel *fumes_label;
    QPushButton *back1;
    QMenuBar *menubar;
    QStatusBar *statusbar;

    void setupUi(QMainWindow *Hardware)
    {
        if (Hardware->objectName().isEmpty())
            Hardware->setObjectName(QString::fromUtf8("Hardware"));
        Hardware->resize(1024, 600);
        Hardware->setStyleSheet(QString::fromUtf8("background-color: rgb(220, 255, 251);\n"
"\n"
""));
        centralwidget = new QWidget(Hardware);
        centralwidget->setObjectName(QString::fromUtf8("centralwidget"));
        widget = new QWidget(centralwidget);
        widget->setObjectName(QString::fromUtf8("widget"));
        widget->setGeometry(QRect(20, 80, 291, 181));
        widget->setStyleSheet(QString::fromUtf8("border-radius: 30px;\n"
"background-color: rgb(255, 255, 255);\n"
"\n"
"\n"
"\n"
"\n"
"\n"
""));
        Templabel = new QLabel(widget);
        Templabel->setObjectName(QString::fromUtf8("Templabel"));
        Templabel->setGeometry(QRect(30, 140, 51, 21));
        Templabel->setStyleSheet(QString::fromUtf8("color: rgb(85, 170, 255);\n"
"font: 12pt \"Agency FB\";"));
        Temp_label = new QLabel(widget);
        Temp_label->setObjectName(QString::fromUtf8("Temp_label"));
        Temp_label->setGeometry(QRect(100, 30, 81, 121));
        Temp_label->setStyleSheet(QString::fromUtf8("color: rgb(85, 170, 255);\n"
"font: 48pt \"Agency FB\";"));
        label = new QLabel(widget);
        label->setObjectName(QString::fromUtf8("label"));
        label->setGeometry(QRect(10, 20, 91, 111));
        label->setStyleSheet(QString::fromUtf8("\n"
"image: url(:/images/temp.png);"));
        Temp_label_2 = new QLabel(widget);
        Temp_label_2->setObjectName(QString::fromUtf8("Temp_label_2"));
        Temp_label_2->setGeometry(QRect(180, 30, 61, 121));
        Temp_label_2->setStyleSheet(QString::fromUtf8("color: rgb(85, 170, 255);\n"
"font: 48pt \"Agency FB\";"));
        widget_2 = new QWidget(centralwidget);
        widget_2->setObjectName(QString::fromUtf8("widget_2"));
        widget_2->setGeometry(QRect(340, 80, 291, 181));
        widget_2->setStyleSheet(QString::fromUtf8("border-radius: 30px;\n"
"background-color: rgb(255, 255, 255);\n"
"\n"
"\n"
""));
        label_2 = new QLabel(widget_2);
        label_2->setObjectName(QString::fromUtf8("label_2"));
        label_2->setGeometry(QRect(0, 20, 91, 111));
        label_2->setStyleSheet(QString::fromUtf8("image: url(:/images/humi.png);"));
        Temp_label_4 = new QLabel(widget_2);
        Temp_label_4->setObjectName(QString::fromUtf8("Temp_label_4"));
        Temp_label_4->setGeometry(QRect(200, 30, 61, 121));
        Temp_label_4->setStyleSheet(QString::fromUtf8("color: rgb(85, 170, 255);\n"
"font: 48pt \"Agency FB\";"));
        Humilabel = new QLabel(widget_2);
        Humilabel->setObjectName(QString::fromUtf8("Humilabel"));
        Humilabel->setGeometry(QRect(30, 140, 41, 21));
        Humilabel->setStyleSheet(QString::fromUtf8("color: rgb(85, 170, 255);\n"
"font: 12pt \"Agency FB\";"));
        Humi_label = new QLabel(widget_2);
        Humi_label->setObjectName(QString::fromUtf8("Humi_label"));
        Humi_label->setGeometry(QRect(110, 30, 81, 121));
        Humi_label->setStyleSheet(QString::fromUtf8("color: rgb(85, 170, 255);\n"
"font: 48pt \"Agency FB\";"));
        widget_3 = new QWidget(centralwidget);
        widget_3->setObjectName(QString::fromUtf8("widget_3"));
        widget_3->setGeometry(QRect(30, 320, 451, 181));
        widget_3->setStyleSheet(QString::fromUtf8("border-radius: 30px;\n"
"background-color: rgb(255, 255, 255);\n"
"\n"
"\n"
""));
        label_3 = new QLabel(widget_3);
        label_3->setObjectName(QString::fromUtf8("label_3"));
        label_3->setGeometry(QRect(31, 20, 91, 111));
        label_3->setStyleSheet(QString::fromUtf8("\n"
"image: url(:/images/car.png);"));
        Temp_label_5 = new QLabel(widget_3);
        Temp_label_5->setObjectName(QString::fromUtf8("Temp_label_5"));
        Temp_label_5->setGeometry(QRect(350, 30, 91, 121));
        Temp_label_5->setStyleSheet(QString::fromUtf8("color: rgb(85, 170, 255);\n"
"font: 48pt \"Agency FB\";"));
        Sr04labal = new QLabel(widget_3);
        Sr04labal->setObjectName(QString::fromUtf8("Sr04labal"));
        Sr04labal->setGeometry(QRect(60, 140, 41, 21));
        Sr04labal->setStyleSheet(QString::fromUtf8("color: rgb(85, 170, 255);\n"
"font: 12pt \"Agency FB\";"));
        Sr04_label = new QLabel(widget_3);
        Sr04_label->setObjectName(QString::fromUtf8("Sr04_label"));
        Sr04_label->setGeometry(QRect(160, 50, 171, 91));
        Sr04_label->setStyleSheet(QString::fromUtf8("color: rgb(85, 170, 255);\n"
"font: 48pt \"Agency FB\";"));
        widget_4 = new QWidget(centralwidget);
        widget_4->setObjectName(QString::fromUtf8("widget_4"));
        widget_4->setGeometry(QRect(520, 320, 451, 181));
        widget_4->setStyleSheet(QString::fromUtf8("border-radius: 30px;\n"
"background-color: rgb(255, 255, 255);\n"
"\n"
"\n"
""));
        buzzer = new QLabel(widget_4);
        buzzer->setObjectName(QString::fromUtf8("buzzer"));
        buzzer->setGeometry(QRect(80, 30, 120, 120));
        buzzer->setStyleSheet(QString::fromUtf8("image: url(:/images/buzzer.png);"));
        led = new QLabel(widget_4);
        led->setObjectName(QString::fromUtf8("led"));
        led->setGeometry(QRect(260, 30, 120, 120));
        led->setStyleSheet(QString::fromUtf8("image: url(:/images/led.png);"));
        widget_5 = new QWidget(centralwidget);
        widget_5->setObjectName(QString::fromUtf8("widget_5"));
        widget_5->setGeometry(QRect(660, 80, 341, 181));
        widget_5->setStyleSheet(QString::fromUtf8("border-radius: 30px;\n"
"background-color: rgb(255, 255, 255);\n"
"\n"
"\n"
""));
        label_5 = new QLabel(widget_5);
        label_5->setObjectName(QString::fromUtf8("label_5"));
        label_5->setGeometry(QRect(20, 20, 90, 110));
        label_5->setStyleSheet(QString::fromUtf8("image: url(:/images/air.png);"));
        Temp_label_7 = new QLabel(widget_5);
        Temp_label_7->setObjectName(QString::fromUtf8("Temp_label_7"));
        Temp_label_7->setGeometry(QRect(270, 30, 61, 121));
        Temp_label_7->setStyleSheet(QString::fromUtf8("color: rgb(85, 170, 255);\n"
"font: 48pt \"Agency FB\";"));
        Humilabel_3 = new QLabel(widget_5);
        Humilabel_3->setObjectName(QString::fromUtf8("Humilabel_3"));
        Humilabel_3->setGeometry(QRect(20, 140, 81, 21));
        Humilabel_3->setStyleSheet(QString::fromUtf8("\n"
"color: rgb(85, 255, 0);\n"
"font: 12pt \"Agency FB\";"));
        fumes_label = new QLabel(widget_5);
        fumes_label->setObjectName(QString::fromUtf8("fumes_label"));
        fumes_label->setGeometry(QRect(130, 30, 131, 121));
        fumes_label->setStyleSheet(QString::fromUtf8("color: rgb(85, 170, 255);\n"
"font: 48pt \"Agency FB\";"));
        back1 = new QPushButton(centralwidget);
        back1->setObjectName(QString::fromUtf8("back1"));
        back1->setGeometry(QRect(20, 10, 31, 28));
        back1->setStyleSheet(QString::fromUtf8("QPushButton {\n"
" background-color:transparent;\n"
" border-radius:10px;\n"
" border-image: url(:/images/back1.png);\n"
"}"));
        Hardware->setCentralWidget(centralwidget);
        menubar = new QMenuBar(Hardware);
        menubar->setObjectName(QString::fromUtf8("menubar"));
        menubar->setGeometry(QRect(0, 0, 1024, 26));
        Hardware->setMenuBar(menubar);
        statusbar = new QStatusBar(Hardware);
        statusbar->setObjectName(QString::fromUtf8("statusbar"));
        Hardware->setStatusBar(statusbar);

        retranslateUi(Hardware);

        QMetaObject::connectSlotsByName(Hardware);
    } // setupUi

    void retranslateUi(QMainWindow *Hardware)
    {
        Hardware->setWindowTitle(QCoreApplication::translate("Hardware", "MainWindow", nullptr));
        Templabel->setText(QCoreApplication::translate("Hardware", " \346\270\251\345\272\246", nullptr));
        Temp_label->setText(QCoreApplication::translate("Hardware", "25", nullptr));
        label->setText(QString());
        Temp_label_2->setText(QCoreApplication::translate("Hardware", "\342\204\203", nullptr));
        label_2->setText(QString());
        Temp_label_4->setText(QCoreApplication::translate("Hardware", "%", nullptr));
        Humilabel->setText(QCoreApplication::translate("Hardware", "\346\271\277\345\272\246", nullptr));
        Humi_label->setText(QCoreApplication::translate("Hardware", "50", nullptr));
        label_3->setText(QString());
        Temp_label_5->setText(QCoreApplication::translate("Hardware", "cm", nullptr));
        Sr04labal->setText(QCoreApplication::translate("Hardware", "\350\267\235\347\246\273", nullptr));
        Sr04_label->setText(QCoreApplication::translate("Hardware", "25.51", nullptr));
        buzzer->setText(QString());
        led->setText(QString());
        label_5->setText(QString());
        Temp_label_7->setText(QCoreApplication::translate("Hardware", "%", nullptr));
        Humilabel_3->setText(QCoreApplication::translate("Hardware", "\347\251\272\346\260\224\350\264\250\351\207\217", nullptr));
        fumes_label->setText(QCoreApplication::translate("Hardware", "50", nullptr));
        back1->setText(QString());
    } // retranslateUi

};

namespace Ui {
    class Hardware: public Ui_Hardware {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_HARDWARE_H
