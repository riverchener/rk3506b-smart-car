/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 5.15.18
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QWidget *centralwidget;
    QPushButton *pushButton;
    QPushButton *pushButton_2;
    QPushButton *pushButton_3;
    QPushButton *pushButton_4;
    QPushButton *pushButton_5;
    QLabel *timelable;
    QLabel *datelable;
    QLabel *label;
    QLabel *label_2;
    QLabel *label_3;
    QLabel *label_4;
    QLabel *label_5;
    QMenuBar *menubar;
    QStatusBar *statusbar;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName(QString::fromUtf8("MainWindow"));
        MainWindow->resize(1024, 600);
        MainWindow->setMaximumSize(QSize(1024, 600));
        MainWindow->setStyleSheet(QString::fromUtf8("\n"
"QMainWindow { border-image: url(:/images/1.jpg); }"));
        centralwidget = new QWidget(MainWindow);
        centralwidget->setObjectName(QString::fromUtf8("centralwidget"));
        pushButton = new QPushButton(centralwidget);
        pushButton->setObjectName(QString::fromUtf8("pushButton"));
        pushButton->setGeometry(QRect(40, 300, 261, 201));
        pushButton->setStyleSheet(QString::fromUtf8("QPushButton { border-image: url(:/images/Video1.png) }\n"
""));
        pushButton_2 = new QPushButton(centralwidget);
        pushButton_2->setObjectName(QString::fromUtf8("pushButton_2"));
        pushButton_2->setGeometry(QRect(360, 290, 280, 260));
        pushButton_2->setStyleSheet(QString::fromUtf8("QPushButton { border-image: url(:/images/Music.png) }"));
        pushButton_3 = new QPushButton(centralwidget);
        pushButton_3->setObjectName(QString::fromUtf8("pushButton_3"));
        pushButton_3->setGeometry(QRect(710, 290, 280, 260));
        pushButton_3->setStyleSheet(QString::fromUtf8("QPushButton { border-image: url(:/images/Weather.png) }"));
        pushButton_4 = new QPushButton(centralwidget);
        pushButton_4->setObjectName(QString::fromUtf8("pushButton_4"));
        pushButton_4->setGeometry(QRect(710, 30, 251, 211));
        pushButton_4->setStyleSheet(QString::fromUtf8("QPushButton { border-image: url(:/images/environment.png) }"));
        pushButton_5 = new QPushButton(centralwidget);
        pushButton_5->setObjectName(QString::fromUtf8("pushButton_5"));
        pushButton_5->setGeometry(QRect(360, 10, 280, 260));
        pushButton_5->setStyleSheet(QString::fromUtf8("QPushButton { border-image: url(:/images/gps.png) }"));
        timelable = new QLabel(centralwidget);
        timelable->setObjectName(QString::fromUtf8("timelable"));
        timelable->setGeometry(QRect(130, 80, 161, 51));
        timelable->setStyleSheet(QString::fromUtf8("font: 75 20pt \"Agency FB\";\n"
"color: rgb(255, 255, 255);"));
        datelable = new QLabel(centralwidget);
        datelable->setObjectName(QString::fromUtf8("datelable"));
        datelable->setGeometry(QRect(20, 160, 321, 51));
        datelable->setStyleSheet(QString::fromUtf8("font: 75 20pt \"Agency FB\";\n"
"color: rgb(255, 255, 255);\n"
""));
        label = new QLabel(centralwidget);
        label->setObjectName(QString::fromUtf8("label"));
        label->setGeometry(QRect(120, 520, 91, 21));
        label->setStyleSheet(QString::fromUtf8("font: 10pt \"Agency FB\";\n"
"color: rgb(255, 255, 255);"));
        label_2 = new QLabel(centralwidget);
        label_2->setObjectName(QString::fromUtf8("label_2"));
        label_2->setGeometry(QRect(450, 520, 91, 20));
        label_2->setStyleSheet(QString::fromUtf8("font: 10pt \"Agency FB\";\n"
"color: rgb(255, 255, 255);"));
        label_3 = new QLabel(centralwidget);
        label_3->setObjectName(QString::fromUtf8("label_3"));
        label_3->setGeometry(QRect(820, 520, 91, 20));
        label_3->setStyleSheet(QString::fromUtf8("font: 10pt \"Agency FB\";\n"
"color: rgb(255, 255, 255);"));
        label_4 = new QLabel(centralwidget);
        label_4->setObjectName(QString::fromUtf8("label_4"));
        label_4->setGeometry(QRect(800, 260, 91, 20));
        label_4->setStyleSheet(QString::fromUtf8("font: 10pt \"Agency FB\";\n"
"color: rgb(255, 255, 255);"));
        label_5 = new QLabel(centralwidget);
        label_5->setObjectName(QString::fromUtf8("label_5"));
        label_5->setGeometry(QRect(480, 260, 71, 20));
        label_5->setStyleSheet(QString::fromUtf8("color: rgb(255, 255, 255);\n"
"font: 10pt \"Algerian\";"));
        MainWindow->setCentralWidget(centralwidget);
        menubar = new QMenuBar(MainWindow);
        menubar->setObjectName(QString::fromUtf8("menubar"));
        menubar->setGeometry(QRect(0, 0, 1024, 26));
        MainWindow->setMenuBar(menubar);
        statusbar = new QStatusBar(MainWindow);
        statusbar->setObjectName(QString::fromUtf8("statusbar"));
        MainWindow->setStatusBar(statusbar);

        retranslateUi(MainWindow);

        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QCoreApplication::translate("MainWindow", "MainWindow", nullptr));
        pushButton->setText(QString());
        pushButton_2->setText(QString());
        pushButton_3->setText(QString());
        pushButton_4->setText(QString());
        pushButton_5->setText(QString());
        timelable->setText(QString());
        datelable->setText(QString());
        label->setText(QCoreApplication::translate("MainWindow", "\350\247\206\351\242\221\346\222\255\346\224\276\345\231\250", nullptr));
        label_2->setText(QCoreApplication::translate("MainWindow", "\351\237\263\344\271\220\346\222\255\346\224\276\345\231\250", nullptr));
        label_3->setText(QCoreApplication::translate("MainWindow", "\345\244\251\346\260\224\351\242\204\346\212\245", nullptr));
        label_4->setText(QCoreApplication::translate("MainWindow", "\347\216\257\345\242\203\346\243\200\346\265\213", nullptr));
        label_5->setText(QCoreApplication::translate("MainWindow", "GPS\345\256\232\344\275\215", nullptr));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
