/********************************************************************************
** Form generated from reading UI file 'weather.ui'
**
** Created by: Qt User Interface Compiler version 5.15.18
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_WEATHER_H
#define UI_WEATHER_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_Weather
{
public:
    QWidget *layoutWidget;
    QVBoxLayout *verticalLayout_7;
    QWidget *widget01;
    QHBoxLayout *horizontalLayout_6;
    QPushButton *back;
    QSpacerItem *horizontalSpacer_2;
    QLabel *labelCurrentDate;
    QWidget *widget02;
    QHBoxLayout *horizontalLayout_5;
    QLabel *labelWeatherIcon;
    QGridLayout *gridLayout_2;
    QLabel *labelweatherType;
    QLabel *labelTempRange;
    QLabel *labelTmp;
    QSpacerItem *horizontalSpacer;
    QLabel *labelCity;
    QWidget *widget03;
    QVBoxLayout *verticalLayout;
    QLabel *labelGanmao;
    QWidget *widget0301;
    QGridLayout *gridLayout;
    QHBoxLayout *horizontalLayout;
    QLabel *labelFX;
    QVBoxLayout *verticalLayout_2;
    QLabel *labelFXType;
    QLabel *labelFXType_2;
    QHBoxLayout *horizontalLayout_2;
    QLabel *labelFX_2;
    QVBoxLayout *verticalLayout_3;
    QLabel *labelPM25;
    QLabel *labelPM25Data;
    QHBoxLayout *horizontalLayout_3;
    QLabel *labelShidu;
    QVBoxLayout *verticalLayout_4;
    QLabel *labelshidu;
    QLabel *labelShiduData;
    QHBoxLayout *horizontalLayout_4;
    QLabel *labelFX_4;
    QVBoxLayout *verticalLayout_5;
    QLabel *labelair;
    QLabel *labelairData;
    QWidget *widget04;
    QVBoxLayout *verticalLayout_6;
    QWidget *widget0401;
    QGridLayout *gridLayout_3;
    QLabel *labelday0;
    QLabel *labelday1;
    QLabel *labelday2;
    QLabel *labelday3;
    QLabel *labelday4;
    QLabel *labelday5;
    QLabel *labelDate0;
    QLabel *labelDate1;
    QLabel *labelDate2;
    QLabel *labelDate3;
    QLabel *labelDate4;
    QLabel *labelDate5;
    QWidget *widget0402;
    QGridLayout *gridLayout_4;
    QLabel *lbweatherTypeDate4;
    QLabel *lbweatherTypeDate5;
    QLabel *labelWeaterIcon1;
    QLabel *labelWeaterIcon5;
    QLabel *labelWeaterIcon0;
    QLabel *lbweatherTypeDate3;
    QLabel *labelWeaterIcon4;
    QLabel *lbweatherTypeDate1;
    QLabel *labelWeaterIcon3;
    QLabel *lbweatherTypeDate2;
    QLabel *labelWeaterIcon2;
    QLabel *lbweatherTypeDate0;
    QWidget *widget0403;
    QHBoxLayout *horizontalLayout_7;
    QLabel *labelairq0;
    QLabel *labelairq1;
    QLabel *labelairq2;
    QLabel *labelairq3;
    QLabel *labelairq4;
    QLabel *labelairq5;
    QGridLayout *gridLayout_6;
    QWidget *widget0404;
    QWidget *widget0405;
    QWidget *widget0406;
    QGridLayout *gridLayout_5;
    QLabel *labelFX5;
    QLabel *labelFX3;
    QLabel *labelFL5;
    QLabel *labelFX0;
    QLabel *labelFX1;
    QLabel *labelFX2;
    QLabel *labelFL0;
    QLabel *labelFL3;
    QLabel *labelFL2;
    QLabel *labelFL4;
    QLabel *labelFX4;
    QLabel *labelFL1;

    void setupUi(QWidget *Weather)
    {
        if (Weather->objectName().isEmpty())
            Weather->setObjectName(QString::fromUtf8("Weather"));
        Weather->resize(1024, 600);
        Weather->setStyleSheet(QString::fromUtf8("background-color: rgb(0, 0, 0);"));
        layoutWidget = new QWidget(Weather);
        layoutWidget->setObjectName(QString::fromUtf8("layoutWidget"));
        layoutWidget->setGeometry(QRect(0, 0, 1021, 601));
        verticalLayout_7 = new QVBoxLayout(layoutWidget);
        verticalLayout_7->setSpacing(2);
        verticalLayout_7->setObjectName(QString::fromUtf8("verticalLayout_7"));
        verticalLayout_7->setContentsMargins(0, 0, 0, 0);
        widget01 = new QWidget(layoutWidget);
        widget01->setObjectName(QString::fromUtf8("widget01"));
        widget01->setMaximumSize(QSize(16777215, 50));
        horizontalLayout_6 = new QHBoxLayout(widget01);
        horizontalLayout_6->setObjectName(QString::fromUtf8("horizontalLayout_6"));
        horizontalLayout_6->setContentsMargins(-1, 8, -1, 2);
        back = new QPushButton(widget01);
        back->setObjectName(QString::fromUtf8("back"));
        back->setMinimumSize(QSize(20, 30));
        back->setStyleSheet(QString::fromUtf8("QPushButton { border-image: url(:/images/back.png) }"));

        horizontalLayout_6->addWidget(back);

        horizontalSpacer_2 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_6->addItem(horizontalSpacer_2);

        labelCurrentDate = new QLabel(widget01);
        labelCurrentDate->setObjectName(QString::fromUtf8("labelCurrentDate"));
        labelCurrentDate->setMaximumSize(QSize(16777215, 16777215));
        QFont font;
        font.setFamily(QString::fromUtf8("Courier New"));
        font.setPointSize(13);
        labelCurrentDate->setFont(font);
        labelCurrentDate->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);"));

        horizontalLayout_6->addWidget(labelCurrentDate);


        verticalLayout_7->addWidget(widget01);

        widget02 = new QWidget(layoutWidget);
        widget02->setObjectName(QString::fromUtf8("widget02"));
        widget02->setMaximumSize(QSize(16777215, 80));
        widget02->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);"));
        horizontalLayout_5 = new QHBoxLayout(widget02);
        horizontalLayout_5->setObjectName(QString::fromUtf8("horizontalLayout_5"));
        horizontalLayout_5->setContentsMargins(-1, 4, -1, 4);
        labelWeatherIcon = new QLabel(widget02);
        labelWeatherIcon->setObjectName(QString::fromUtf8("labelWeatherIcon"));
        labelWeatherIcon->setMinimumSize(QSize(90, 90));
        labelWeatherIcon->setMaximumSize(QSize(90, 90));
        labelWeatherIcon->setPixmap(QPixmap(QString::fromUtf8(":/res/type/Qing.png")));
        labelWeatherIcon->setScaledContents(true);

        horizontalLayout_5->addWidget(labelWeatherIcon);

        gridLayout_2 = new QGridLayout();
        gridLayout_2->setObjectName(QString::fromUtf8("gridLayout_2"));
        labelweatherType = new QLabel(widget02);
        labelweatherType->setObjectName(QString::fromUtf8("labelweatherType"));
        QFont font1;
        font1.setPointSize(12);
        labelweatherType->setFont(font1);

        gridLayout_2->addWidget(labelweatherType, 1, 0, 1, 1);

        labelTempRange = new QLabel(widget02);
        labelTempRange->setObjectName(QString::fromUtf8("labelTempRange"));
        labelTempRange->setFont(font1);

        gridLayout_2->addWidget(labelTempRange, 1, 1, 1, 1);

        labelTmp = new QLabel(widget02);
        labelTmp->setObjectName(QString::fromUtf8("labelTmp"));
        QFont font2;
        font2.setFamily(QString::fromUtf8("Arial"));
        font2.setPointSize(33);
        labelTmp->setFont(font2);

        gridLayout_2->addWidget(labelTmp, 0, 0, 1, 2);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        gridLayout_2->addItem(horizontalSpacer, 1, 2, 1, 1);

        labelCity = new QLabel(widget02);
        labelCity->setObjectName(QString::fromUtf8("labelCity"));
        QFont font3;
        font3.setFamily(QString::fromUtf8("Courier New"));
        font3.setPointSize(14);
        labelCity->setFont(font3);

        gridLayout_2->addWidget(labelCity, 0, 3, 1, 1);


        horizontalLayout_5->addLayout(gridLayout_2);


        verticalLayout_7->addWidget(widget02);

        widget03 = new QWidget(layoutWidget);
        widget03->setObjectName(QString::fromUtf8("widget03"));
        widget03->setStyleSheet(QString::fromUtf8("color:rgb(230,230,230);"));
        verticalLayout = new QVBoxLayout(widget03);
        verticalLayout->setSpacing(4);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        verticalLayout->setContentsMargins(-1, 4, -1, 4);
        labelGanmao = new QLabel(widget03);
        labelGanmao->setObjectName(QString::fromUtf8("labelGanmao"));

        verticalLayout->addWidget(labelGanmao);

        widget0301 = new QWidget(widget03);
        widget0301->setObjectName(QString::fromUtf8("widget0301"));
        widget0301->setMaximumSize(QSize(16777215, 100));
        widget0301->setStyleSheet(QString::fromUtf8("background-color: rgb(203, 107, 39);\n"
"border-radius: 20px;"));
        gridLayout = new QGridLayout(widget0301);
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        gridLayout->setHorizontalSpacing(0);
        gridLayout->setVerticalSpacing(8);
        gridLayout->setContentsMargins(35, 0, 10, 0);
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setSpacing(0);
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        horizontalLayout->setContentsMargins(-1, 2, -1, -1);
        labelFX = new QLabel(widget0301);
        labelFX->setObjectName(QString::fromUtf8("labelFX"));
        labelFX->setMinimumSize(QSize(35, 35));
        labelFX->setMaximumSize(QSize(35, 35));
        labelFX->setFrameShadow(QFrame::Raised);
        labelFX->setPixmap(QPixmap(QString::fromUtf8(":/res/wind.png")));
        labelFX->setScaledContents(true);

        horizontalLayout->addWidget(labelFX);

        verticalLayout_2 = new QVBoxLayout();
        verticalLayout_2->setSpacing(7);
        verticalLayout_2->setObjectName(QString::fromUtf8("verticalLayout_2"));
        verticalLayout_2->setContentsMargins(-1, -1, -1, 0);
        labelFXType = new QLabel(widget0301);
        labelFXType->setObjectName(QString::fromUtf8("labelFXType"));
        labelFXType->setAlignment(Qt::AlignCenter);

        verticalLayout_2->addWidget(labelFXType);

        labelFXType_2 = new QLabel(widget0301);
        labelFXType_2->setObjectName(QString::fromUtf8("labelFXType_2"));
        labelFXType_2->setAlignment(Qt::AlignCenter);

        verticalLayout_2->addWidget(labelFXType_2);


        horizontalLayout->addLayout(verticalLayout_2);


        gridLayout->addLayout(horizontalLayout, 0, 0, 1, 1);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName(QString::fromUtf8("horizontalLayout_2"));
        labelFX_2 = new QLabel(widget0301);
        labelFX_2->setObjectName(QString::fromUtf8("labelFX_2"));
        labelFX_2->setMinimumSize(QSize(35, 35));
        labelFX_2->setMaximumSize(QSize(35, 35));
        labelFX_2->setFrameShadow(QFrame::Raised);
        labelFX_2->setPixmap(QPixmap(QString::fromUtf8(":/res/pm25.png")));
        labelFX_2->setScaledContents(true);

        horizontalLayout_2->addWidget(labelFX_2);

        verticalLayout_3 = new QVBoxLayout();
        verticalLayout_3->setObjectName(QString::fromUtf8("verticalLayout_3"));
        labelPM25 = new QLabel(widget0301);
        labelPM25->setObjectName(QString::fromUtf8("labelPM25"));
        labelPM25->setAlignment(Qt::AlignCenter);

        verticalLayout_3->addWidget(labelPM25);

        labelPM25Data = new QLabel(widget0301);
        labelPM25Data->setObjectName(QString::fromUtf8("labelPM25Data"));
        labelPM25Data->setAlignment(Qt::AlignCenter);

        verticalLayout_3->addWidget(labelPM25Data);


        horizontalLayout_2->addLayout(verticalLayout_3);


        gridLayout->addLayout(horizontalLayout_2, 0, 1, 1, 1);

        horizontalLayout_3 = new QHBoxLayout();
        horizontalLayout_3->setObjectName(QString::fromUtf8("horizontalLayout_3"));
        labelShidu = new QLabel(widget0301);
        labelShidu->setObjectName(QString::fromUtf8("labelShidu"));
        labelShidu->setMinimumSize(QSize(35, 35));
        labelShidu->setMaximumSize(QSize(35, 35));
        labelShidu->setFrameShadow(QFrame::Raised);
        labelShidu->setPixmap(QPixmap(QString::fromUtf8(":/res/humidity.png")));
        labelShidu->setScaledContents(true);

        horizontalLayout_3->addWidget(labelShidu);

        verticalLayout_4 = new QVBoxLayout();
        verticalLayout_4->setObjectName(QString::fromUtf8("verticalLayout_4"));
        labelshidu = new QLabel(widget0301);
        labelshidu->setObjectName(QString::fromUtf8("labelshidu"));
        labelshidu->setAlignment(Qt::AlignCenter);

        verticalLayout_4->addWidget(labelshidu);

        labelShiduData = new QLabel(widget0301);
        labelShiduData->setObjectName(QString::fromUtf8("labelShiduData"));
        labelShiduData->setAlignment(Qt::AlignCenter);

        verticalLayout_4->addWidget(labelShiduData);


        horizontalLayout_3->addLayout(verticalLayout_4);


        gridLayout->addLayout(horizontalLayout_3, 1, 0, 1, 1);

        horizontalLayout_4 = new QHBoxLayout();
        horizontalLayout_4->setObjectName(QString::fromUtf8("horizontalLayout_4"));
        labelFX_4 = new QLabel(widget0301);
        labelFX_4->setObjectName(QString::fromUtf8("labelFX_4"));
        labelFX_4->setMinimumSize(QSize(35, 35));
        labelFX_4->setMaximumSize(QSize(35, 35));
        labelFX_4->setFrameShadow(QFrame::Raised);
        labelFX_4->setPixmap(QPixmap(QString::fromUtf8(":/res/airQi.png")));
        labelFX_4->setScaledContents(true);

        horizontalLayout_4->addWidget(labelFX_4);

        verticalLayout_5 = new QVBoxLayout();
        verticalLayout_5->setObjectName(QString::fromUtf8("verticalLayout_5"));
        labelair = new QLabel(widget0301);
        labelair->setObjectName(QString::fromUtf8("labelair"));
        labelair->setAlignment(Qt::AlignCenter);

        verticalLayout_5->addWidget(labelair);

        labelairData = new QLabel(widget0301);
        labelairData->setObjectName(QString::fromUtf8("labelairData"));
        labelairData->setAlignment(Qt::AlignCenter);

        verticalLayout_5->addWidget(labelairData);


        horizontalLayout_4->addLayout(verticalLayout_5);


        gridLayout->addLayout(horizontalLayout_4, 1, 1, 1, 1);


        verticalLayout->addWidget(widget0301);

        verticalLayout->setStretch(0, 2);
        verticalLayout->setStretch(1, 3);

        verticalLayout_7->addWidget(widget03);

        widget04 = new QWidget(layoutWidget);
        widget04->setObjectName(QString::fromUtf8("widget04"));
        verticalLayout_6 = new QVBoxLayout(widget04);
        verticalLayout_6->setSpacing(0);
        verticalLayout_6->setObjectName(QString::fromUtf8("verticalLayout_6"));
        verticalLayout_6->setContentsMargins(-1, 0, -1, 4);
        widget0401 = new QWidget(widget04);
        widget0401->setObjectName(QString::fromUtf8("widget0401"));
        widget0401->setMaximumSize(QSize(16777215, 50));
        gridLayout_3 = new QGridLayout(widget0401);
        gridLayout_3->setObjectName(QString::fromUtf8("gridLayout_3"));
        gridLayout_3->setHorizontalSpacing(8);
        gridLayout_3->setVerticalSpacing(0);
        gridLayout_3->setContentsMargins(0, 0, 0, 4);
        labelday0 = new QLabel(widget0401);
        labelday0->setObjectName(QString::fromUtf8("labelday0"));
        labelday0->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(70, 192, 203);\n"
"border-radius: 7px;\n"
"border-bottom-right-radius:0px;\n"
"border-bottom-left-radius:0px;"));
        labelday0->setAlignment(Qt::AlignBottom|Qt::AlignHCenter);

        gridLayout_3->addWidget(labelday0, 0, 0, 1, 1);

        labelday1 = new QLabel(widget0401);
        labelday1->setObjectName(QString::fromUtf8("labelday1"));
        labelday1->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(70, 192, 203);\n"
"\n"
"border-radius: 7px;\n"
"border-bottom-right-radius:0px;\n"
"border-bottom-left-radius:0px;"));
        labelday1->setAlignment(Qt::AlignBottom|Qt::AlignHCenter);

        gridLayout_3->addWidget(labelday1, 0, 1, 1, 1);

        labelday2 = new QLabel(widget0401);
        labelday2->setObjectName(QString::fromUtf8("labelday2"));
        labelday2->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(70, 192, 203);\n"
"\n"
"border-radius: 7px;\n"
"border-bottom-right-radius:0px;\n"
"border-bottom-left-radius:0px;"));
        labelday2->setAlignment(Qt::AlignBottom|Qt::AlignHCenter);

        gridLayout_3->addWidget(labelday2, 0, 2, 1, 1);

        labelday3 = new QLabel(widget0401);
        labelday3->setObjectName(QString::fromUtf8("labelday3"));
        labelday3->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(70, 192, 203);\n"
"\n"
"border-radius: 7px;\n"
"border-bottom-right-radius:0px;\n"
"border-bottom-left-radius:0px;"));
        labelday3->setAlignment(Qt::AlignBottom|Qt::AlignHCenter);

        gridLayout_3->addWidget(labelday3, 0, 3, 1, 1);

        labelday4 = new QLabel(widget0401);
        labelday4->setObjectName(QString::fromUtf8("labelday4"));
        labelday4->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(70, 192, 203);\n"
"\n"
"border-radius: 7px;\n"
"border-bottom-right-radius:0px;\n"
"border-bottom-left-radius:0px;"));
        labelday4->setAlignment(Qt::AlignBottom|Qt::AlignHCenter);

        gridLayout_3->addWidget(labelday4, 0, 4, 1, 1);

        labelday5 = new QLabel(widget0401);
        labelday5->setObjectName(QString::fromUtf8("labelday5"));
        labelday5->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(70, 192, 203);\n"
"\n"
"border-radius: 7px;\n"
"border-bottom-right-radius:0px;\n"
"border-bottom-left-radius:0px;"));
        labelday5->setAlignment(Qt::AlignBottom|Qt::AlignHCenter);

        gridLayout_3->addWidget(labelday5, 0, 5, 1, 1);

        labelDate0 = new QLabel(widget0401);
        labelDate0->setObjectName(QString::fromUtf8("labelDate0"));
        labelDate0->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(70, 192, 203);\n"
"\n"
"border-radius: 7px;\n"
"border-top-left-radius:0px;\n"
"border-top-right-radius:0px;"));
        labelDate0->setAlignment(Qt::AlignCenter);

        gridLayout_3->addWidget(labelDate0, 1, 0, 1, 1);

        labelDate1 = new QLabel(widget0401);
        labelDate1->setObjectName(QString::fromUtf8("labelDate1"));
        labelDate1->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(70, 192, 203);\n"
"\n"
"border-radius: 7px;\n"
"border-top-left-radius:0px;\n"
"border-top-right-radius:0px;"));
        labelDate1->setAlignment(Qt::AlignCenter);

        gridLayout_3->addWidget(labelDate1, 1, 1, 1, 1);

        labelDate2 = new QLabel(widget0401);
        labelDate2->setObjectName(QString::fromUtf8("labelDate2"));
        labelDate2->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(70, 192, 203);\n"
"\n"
"border-radius: 7px;\n"
"border-top-left-radius:0px;\n"
"border-top-right-radius:0px;"));
        labelDate2->setAlignment(Qt::AlignCenter);

        gridLayout_3->addWidget(labelDate2, 1, 2, 1, 1);

        labelDate3 = new QLabel(widget0401);
        labelDate3->setObjectName(QString::fromUtf8("labelDate3"));
        labelDate3->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(70, 192, 203);\n"
"\n"
"border-radius: 7px;\n"
"border-top-left-radius:0px;\n"
"border-top-right-radius:0px;"));
        labelDate3->setAlignment(Qt::AlignCenter);

        gridLayout_3->addWidget(labelDate3, 1, 3, 1, 1);

        labelDate4 = new QLabel(widget0401);
        labelDate4->setObjectName(QString::fromUtf8("labelDate4"));
        labelDate4->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(70, 192, 203);\n"
"\n"
"border-radius: 7px;\n"
"border-top-left-radius:0px;\n"
"border-top-right-radius:0px;"));
        labelDate4->setAlignment(Qt::AlignCenter);

        gridLayout_3->addWidget(labelDate4, 1, 4, 1, 1);

        labelDate5 = new QLabel(widget0401);
        labelDate5->setObjectName(QString::fromUtf8("labelDate5"));
        labelDate5->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(70, 192, 203);\n"
"\n"
"border-radius: 7px;\n"
"border-top-left-radius:0px;\n"
"border-top-right-radius:0px;"));
        labelDate5->setAlignment(Qt::AlignCenter);

        gridLayout_3->addWidget(labelDate5, 1, 5, 1, 1);


        verticalLayout_6->addWidget(widget0401);

        widget0402 = new QWidget(widget04);
        widget0402->setObjectName(QString::fromUtf8("widget0402"));
        widget0402->setMaximumSize(QSize(16777215, 60));
        gridLayout_4 = new QGridLayout(widget0402);
        gridLayout_4->setObjectName(QString::fromUtf8("gridLayout_4"));
        gridLayout_4->setHorizontalSpacing(8);
        gridLayout_4->setVerticalSpacing(0);
        gridLayout_4->setContentsMargins(0, 0, 0, 4);
        lbweatherTypeDate4 = new QLabel(widget0402);
        lbweatherTypeDate4->setObjectName(QString::fromUtf8("lbweatherTypeDate4"));
        lbweatherTypeDate4->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-top-left-radius:0px;\n"
"border-top-right-radius:0px;"));
        lbweatherTypeDate4->setAlignment(Qt::AlignCenter);

        gridLayout_4->addWidget(lbweatherTypeDate4, 1, 4, 1, 1);

        lbweatherTypeDate5 = new QLabel(widget0402);
        lbweatherTypeDate5->setObjectName(QString::fromUtf8("lbweatherTypeDate5"));
        lbweatherTypeDate5->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-top-left-radius:0px;\n"
"border-top-right-radius:0px;"));
        lbweatherTypeDate5->setAlignment(Qt::AlignCenter);

        gridLayout_4->addWidget(lbweatherTypeDate5, 1, 5, 1, 1);

        labelWeaterIcon1 = new QLabel(widget0402);
        labelWeaterIcon1->setObjectName(QString::fromUtf8("labelWeaterIcon1"));
        labelWeaterIcon1->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-bottom-right-radius:0px;\n"
"border-bottom-left-radius:0px;"));
        labelWeaterIcon1->setPixmap(QPixmap(QString::fromUtf8(":/res/type/DuoYun.png")));
        labelWeaterIcon1->setScaledContents(false);
        labelWeaterIcon1->setAlignment(Qt::AlignCenter);

        gridLayout_4->addWidget(labelWeaterIcon1, 0, 1, 1, 1);

        labelWeaterIcon5 = new QLabel(widget0402);
        labelWeaterIcon5->setObjectName(QString::fromUtf8("labelWeaterIcon5"));
        labelWeaterIcon5->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-bottom-right-radius:0px;\n"
"border-bottom-left-radius:0px;"));
        labelWeaterIcon5->setPixmap(QPixmap(QString::fromUtf8(":/res/type/DuoYun.png")));
        labelWeaterIcon5->setScaledContents(false);
        labelWeaterIcon5->setAlignment(Qt::AlignCenter);

        gridLayout_4->addWidget(labelWeaterIcon5, 0, 5, 1, 1);

        labelWeaterIcon0 = new QLabel(widget0402);
        labelWeaterIcon0->setObjectName(QString::fromUtf8("labelWeaterIcon0"));
        labelWeaterIcon0->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-bottom-right-radius:0px;\n"
"border-bottom-left-radius:0px;"));
        labelWeaterIcon0->setPixmap(QPixmap(QString::fromUtf8(":/res/type/DuoYun.png")));
        labelWeaterIcon0->setScaledContents(false);
        labelWeaterIcon0->setAlignment(Qt::AlignCenter);

        gridLayout_4->addWidget(labelWeaterIcon0, 0, 0, 1, 1);

        lbweatherTypeDate3 = new QLabel(widget0402);
        lbweatherTypeDate3->setObjectName(QString::fromUtf8("lbweatherTypeDate3"));
        lbweatherTypeDate3->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-top-left-radius:0px;\n"
"border-top-right-radius:0px;"));
        lbweatherTypeDate3->setAlignment(Qt::AlignCenter);

        gridLayout_4->addWidget(lbweatherTypeDate3, 1, 3, 1, 1);

        labelWeaterIcon4 = new QLabel(widget0402);
        labelWeaterIcon4->setObjectName(QString::fromUtf8("labelWeaterIcon4"));
        labelWeaterIcon4->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-bottom-right-radius:0px;\n"
"border-bottom-left-radius:0px;"));
        labelWeaterIcon4->setPixmap(QPixmap(QString::fromUtf8(":/res/type/DuoYun.png")));
        labelWeaterIcon4->setScaledContents(false);
        labelWeaterIcon4->setAlignment(Qt::AlignCenter);

        gridLayout_4->addWidget(labelWeaterIcon4, 0, 4, 1, 1);

        lbweatherTypeDate1 = new QLabel(widget0402);
        lbweatherTypeDate1->setObjectName(QString::fromUtf8("lbweatherTypeDate1"));
        lbweatherTypeDate1->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-top-left-radius:0px;\n"
"border-top-right-radius:0px;"));
        lbweatherTypeDate1->setAlignment(Qt::AlignCenter);

        gridLayout_4->addWidget(lbweatherTypeDate1, 1, 1, 1, 1);

        labelWeaterIcon3 = new QLabel(widget0402);
        labelWeaterIcon3->setObjectName(QString::fromUtf8("labelWeaterIcon3"));
        labelWeaterIcon3->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-bottom-right-radius:0px;\n"
"border-bottom-left-radius:0px;"));
        labelWeaterIcon3->setPixmap(QPixmap(QString::fromUtf8(":/res/type/DuoYun.png")));
        labelWeaterIcon3->setScaledContents(false);
        labelWeaterIcon3->setAlignment(Qt::AlignCenter);

        gridLayout_4->addWidget(labelWeaterIcon3, 0, 3, 1, 1);

        lbweatherTypeDate2 = new QLabel(widget0402);
        lbweatherTypeDate2->setObjectName(QString::fromUtf8("lbweatherTypeDate2"));
        lbweatherTypeDate2->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-top-left-radius:0px;\n"
"border-top-right-radius:0px;"));
        lbweatherTypeDate2->setAlignment(Qt::AlignCenter);

        gridLayout_4->addWidget(lbweatherTypeDate2, 1, 2, 1, 1);

        labelWeaterIcon2 = new QLabel(widget0402);
        labelWeaterIcon2->setObjectName(QString::fromUtf8("labelWeaterIcon2"));
        labelWeaterIcon2->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-bottom-right-radius:0px;\n"
"border-bottom-left-radius:0px;"));
        labelWeaterIcon2->setPixmap(QPixmap(QString::fromUtf8(":/res/type/DuoYun.png")));
        labelWeaterIcon2->setScaledContents(false);
        labelWeaterIcon2->setAlignment(Qt::AlignCenter);

        gridLayout_4->addWidget(labelWeaterIcon2, 0, 2, 1, 1);

        lbweatherTypeDate0 = new QLabel(widget0402);
        lbweatherTypeDate0->setObjectName(QString::fromUtf8("lbweatherTypeDate0"));
        lbweatherTypeDate0->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-top-left-radius:0px;\n"
"border-top-right-radius:0px;"));
        lbweatherTypeDate0->setAlignment(Qt::AlignCenter);

        gridLayout_4->addWidget(lbweatherTypeDate0, 1, 0, 1, 1);


        verticalLayout_6->addWidget(widget0402);

        widget0403 = new QWidget(widget04);
        widget0403->setObjectName(QString::fromUtf8("widget0403"));
        widget0403->setMaximumSize(QSize(16777215, 40));
        horizontalLayout_7 = new QHBoxLayout(widget0403);
        horizontalLayout_7->setSpacing(8);
        horizontalLayout_7->setObjectName(QString::fromUtf8("horizontalLayout_7"));
        horizontalLayout_7->setContentsMargins(0, 0, 0, 4);
        labelairq0 = new QLabel(widget0403);
        labelairq0->setObjectName(QString::fromUtf8("labelairq0"));
        labelairq0->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(150, 213, 32);\n"
"border-radius: 7px;"));
        labelairq0->setAlignment(Qt::AlignCenter);

        horizontalLayout_7->addWidget(labelairq0);

        labelairq1 = new QLabel(widget0403);
        labelairq1->setObjectName(QString::fromUtf8("labelairq1"));
        labelairq1->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(150, 213, 32);\n"
"\n"
"border-radius: 7px;"));
        labelairq1->setAlignment(Qt::AlignCenter);

        horizontalLayout_7->addWidget(labelairq1);

        labelairq2 = new QLabel(widget0403);
        labelairq2->setObjectName(QString::fromUtf8("labelairq2"));
        labelairq2->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(150, 213, 32);\n"
"border-radius: 7px;"));
        labelairq2->setAlignment(Qt::AlignCenter);

        horizontalLayout_7->addWidget(labelairq2);

        labelairq3 = new QLabel(widget0403);
        labelairq3->setObjectName(QString::fromUtf8("labelairq3"));
        labelairq3->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(150, 213, 32);\n"
"border-radius: 7px;"));
        labelairq3->setAlignment(Qt::AlignCenter);

        horizontalLayout_7->addWidget(labelairq3);

        labelairq4 = new QLabel(widget0403);
        labelairq4->setObjectName(QString::fromUtf8("labelairq4"));
        labelairq4->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(150, 213, 32);\n"
"border-radius: 7px;"));
        labelairq4->setAlignment(Qt::AlignCenter);

        horizontalLayout_7->addWidget(labelairq4);

        labelairq5 = new QLabel(widget0403);
        labelairq5->setObjectName(QString::fromUtf8("labelairq5"));
        labelairq5->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(150, 213, 32);\n"
"border-radius: 7px;"));
        labelairq5->setAlignment(Qt::AlignCenter);

        horizontalLayout_7->addWidget(labelairq5);


        verticalLayout_6->addWidget(widget0403);

        gridLayout_6 = new QGridLayout();
        gridLayout_6->setSpacing(0);
        gridLayout_6->setObjectName(QString::fromUtf8("gridLayout_6"));
        widget0404 = new QWidget(widget04);
        widget0404->setObjectName(QString::fromUtf8("widget0404"));
        widget0404->setMinimumSize(QSize(0, 80));
        widget0404->setMaximumSize(QSize(16777215, 80));
        widget0404->setStyleSheet(QString::fromUtf8("\n"
"background-color: rgb(135, 135, 135);\n"
"border-radius: 16px;\n"
"border-bottom-right-radius:0px;\n"
"border-bottom-left-radius:0px;"));

        gridLayout_6->addWidget(widget0404, 0, 0, 1, 1);

        widget0405 = new QWidget(widget04);
        widget0405->setObjectName(QString::fromUtf8("widget0405"));
        widget0405->setMinimumSize(QSize(0, 80));
        widget0405->setMaximumSize(QSize(16777215, 80));
        widget0405->setStyleSheet(QString::fromUtf8("background-color: rgb(135, 135, 135);\n"
"border-radius: 16px;\n"
"border-top-left-radius:0px;\n"
"border-top-right-radius:0px;"));

        gridLayout_6->addWidget(widget0405, 1, 0, 1, 1);


        verticalLayout_6->addLayout(gridLayout_6);

        widget0406 = new QWidget(widget04);
        widget0406->setObjectName(QString::fromUtf8("widget0406"));
        widget0406->setMaximumSize(QSize(16777215, 50));
        gridLayout_5 = new QGridLayout(widget0406);
        gridLayout_5->setObjectName(QString::fromUtf8("gridLayout_5"));
        gridLayout_5->setHorizontalSpacing(8);
        gridLayout_5->setVerticalSpacing(0);
        gridLayout_5->setContentsMargins(0, 4, 0, 0);
        labelFX5 = new QLabel(widget0406);
        labelFX5->setObjectName(QString::fromUtf8("labelFX5"));
        labelFX5->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-bottom-right-radius:0px;\n"
"border-bottom-left-radius:0px;"));
        labelFX5->setScaledContents(false);
        labelFX5->setAlignment(Qt::AlignCenter);

        gridLayout_5->addWidget(labelFX5, 1, 5, 1, 1);

        labelFX3 = new QLabel(widget0406);
        labelFX3->setObjectName(QString::fromUtf8("labelFX3"));
        labelFX3->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-bottom-right-radius:0px;\n"
"border-bottom-left-radius:0px;"));
        labelFX3->setScaledContents(false);
        labelFX3->setAlignment(Qt::AlignCenter);

        gridLayout_5->addWidget(labelFX3, 1, 3, 1, 1);

        labelFL5 = new QLabel(widget0406);
        labelFL5->setObjectName(QString::fromUtf8("labelFL5"));
        labelFL5->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-top-left-radius:0px;\n"
"border-top-right-radius:0px;"));
        labelFL5->setAlignment(Qt::AlignCenter);

        gridLayout_5->addWidget(labelFL5, 2, 5, 1, 1);

        labelFX0 = new QLabel(widget0406);
        labelFX0->setObjectName(QString::fromUtf8("labelFX0"));
        labelFX0->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-bottom-right-radius:0px;\n"
"border-bottom-left-radius:0px;"));
        labelFX0->setScaledContents(false);
        labelFX0->setAlignment(Qt::AlignCenter);

        gridLayout_5->addWidget(labelFX0, 1, 0, 1, 1);

        labelFX1 = new QLabel(widget0406);
        labelFX1->setObjectName(QString::fromUtf8("labelFX1"));
        labelFX1->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-bottom-right-radius:0px;\n"
"border-bottom-left-radius:0px;"));
        labelFX1->setScaledContents(false);
        labelFX1->setAlignment(Qt::AlignCenter);

        gridLayout_5->addWidget(labelFX1, 1, 1, 1, 1);

        labelFX2 = new QLabel(widget0406);
        labelFX2->setObjectName(QString::fromUtf8("labelFX2"));
        labelFX2->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-bottom-right-radius:0px;\n"
"border-bottom-left-radius:0px;"));
        labelFX2->setScaledContents(false);
        labelFX2->setAlignment(Qt::AlignCenter);

        gridLayout_5->addWidget(labelFX2, 1, 2, 1, 1);

        labelFL0 = new QLabel(widget0406);
        labelFL0->setObjectName(QString::fromUtf8("labelFL0"));
        labelFL0->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-top-left-radius:0px;\n"
"border-top-right-radius:0px;"));
        labelFL0->setAlignment(Qt::AlignCenter);

        gridLayout_5->addWidget(labelFL0, 2, 0, 1, 1);

        labelFL3 = new QLabel(widget0406);
        labelFL3->setObjectName(QString::fromUtf8("labelFL3"));
        labelFL3->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-top-left-radius:0px;\n"
"border-top-right-radius:0px;"));
        labelFL3->setAlignment(Qt::AlignCenter);

        gridLayout_5->addWidget(labelFL3, 2, 3, 1, 1);

        labelFL2 = new QLabel(widget0406);
        labelFL2->setObjectName(QString::fromUtf8("labelFL2"));
        labelFL2->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-top-left-radius:0px;\n"
"border-top-right-radius:0px;"));
        labelFL2->setAlignment(Qt::AlignCenter);

        gridLayout_5->addWidget(labelFL2, 2, 2, 1, 1);

        labelFL4 = new QLabel(widget0406);
        labelFL4->setObjectName(QString::fromUtf8("labelFL4"));
        labelFL4->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-top-left-radius:0px;\n"
"border-top-right-radius:0px;"));
        labelFL4->setAlignment(Qt::AlignCenter);

        gridLayout_5->addWidget(labelFL4, 2, 4, 1, 1);

        labelFX4 = new QLabel(widget0406);
        labelFX4->setObjectName(QString::fromUtf8("labelFX4"));
        labelFX4->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-bottom-right-radius:0px;\n"
"border-bottom-left-radius:0px;"));
        labelFX4->setScaledContents(false);
        labelFX4->setAlignment(Qt::AlignCenter);

        gridLayout_5->addWidget(labelFX4, 1, 4, 1, 1);

        labelFL1 = new QLabel(widget0406);
        labelFL1->setObjectName(QString::fromUtf8("labelFL1"));
        labelFL1->setStyleSheet(QString::fromUtf8("color: rgb(230, 230, 230);\n"
"background-color: rgb(74, 74, 74);\n"
"border-radius: 7px;\n"
"border-top-left-radius:0px;\n"
"border-top-right-radius:0px;"));
        labelFL1->setAlignment(Qt::AlignCenter);

        gridLayout_5->addWidget(labelFL1, 2, 1, 1, 1);


        verticalLayout_6->addWidget(widget0406);


        verticalLayout_7->addWidget(widget04);

        verticalLayout_7->setStretch(1, 2);
        verticalLayout_7->setStretch(2, 3);
        verticalLayout_7->setStretch(3, 12);

        retranslateUi(Weather);

        QMetaObject::connectSlotsByName(Weather);
    } // setupUi

    void retranslateUi(QWidget *Weather)
    {
        Weather->setWindowTitle(QCoreApplication::translate("Weather", "Widget", nullptr));
        back->setText(QString());
        labelCurrentDate->setText(QCoreApplication::translate("Weather", "2024/09/16 \346\230\237\346\234\237\344\270\200", nullptr));
        labelWeatherIcon->setText(QString());
        labelweatherType->setText(QCoreApplication::translate("Weather", "\346\231\264\345\244\251", nullptr));
        labelTempRange->setText(QCoreApplication::translate("Weather", "20-26\342\204\203", nullptr));
        labelTmp->setText(QCoreApplication::translate("Weather", "23", nullptr));
        labelCity->setText(QCoreApplication::translate("Weather", "\346\267\261\345\234\263\345\270\202  ", nullptr));
        labelGanmao->setText(QCoreApplication::translate("Weather", "\346\204\237\345\206\222\346\214\207\346\225\260:\345\220\204\347\261\273\344\272\272\347\276\244\345\217\257\350\207\252\347\224\261\346\264\273\345\212\250", nullptr));
        labelFX->setText(QString());
        labelFXType->setText(QCoreApplication::translate("Weather", "\344\270\234\345\215\227\351\243\216", nullptr));
        labelFXType_2->setText(QCoreApplication::translate("Weather", "2\347\272\247", nullptr));
        labelFX_2->setText(QString());
        labelPM25->setText(QCoreApplication::translate("Weather", "PM2.5", nullptr));
        labelPM25Data->setText(QCoreApplication::translate("Weather", "24", nullptr));
        labelShidu->setText(QString());
        labelshidu->setText(QCoreApplication::translate("Weather", "\346\271\277\345\272\246", nullptr));
        labelShiduData->setText(QCoreApplication::translate("Weather", "85%", nullptr));
        labelFX_4->setText(QString());
        labelair->setText(QCoreApplication::translate("Weather", "\347\251\272\346\260\224\350\264\250\351\207\217", nullptr));
        labelairData->setText(QCoreApplication::translate("Weather", "\344\274\230", nullptr));
        labelday0->setText(QCoreApplication::translate("Weather", "\346\230\250\345\244\251", nullptr));
        labelday1->setText(QCoreApplication::translate("Weather", "\346\230\250\345\244\251", nullptr));
        labelday2->setText(QCoreApplication::translate("Weather", "\346\230\250\345\244\251", nullptr));
        labelday3->setText(QCoreApplication::translate("Weather", "\346\230\250\345\244\251", nullptr));
        labelday4->setText(QCoreApplication::translate("Weather", "\346\230\250\345\244\251", nullptr));
        labelday5->setText(QCoreApplication::translate("Weather", "\346\230\250\345\244\251", nullptr));
        labelDate0->setText(QCoreApplication::translate("Weather", "01/20", nullptr));
        labelDate1->setText(QCoreApplication::translate("Weather", "01/20", nullptr));
        labelDate2->setText(QCoreApplication::translate("Weather", "01/20", nullptr));
        labelDate3->setText(QCoreApplication::translate("Weather", "01/20", nullptr));
        labelDate4->setText(QCoreApplication::translate("Weather", "01/20", nullptr));
        labelDate5->setText(QCoreApplication::translate("Weather", "01/20", nullptr));
        lbweatherTypeDate4->setText(QCoreApplication::translate("Weather", "\345\244\232\344\272\221", nullptr));
        lbweatherTypeDate5->setText(QCoreApplication::translate("Weather", "\345\244\232\344\272\221", nullptr));
        labelWeaterIcon1->setText(QString());
        labelWeaterIcon5->setText(QString());
        labelWeaterIcon0->setText(QString());
        lbweatherTypeDate3->setText(QCoreApplication::translate("Weather", "\345\244\232\344\272\221", nullptr));
        labelWeaterIcon4->setText(QString());
        lbweatherTypeDate1->setText(QCoreApplication::translate("Weather", "\345\244\232\344\272\221", nullptr));
        labelWeaterIcon3->setText(QString());
        lbweatherTypeDate2->setText(QCoreApplication::translate("Weather", "\345\244\232\344\272\221", nullptr));
        labelWeaterIcon2->setText(QString());
        lbweatherTypeDate0->setText(QCoreApplication::translate("Weather", "\345\244\232\344\272\221", nullptr));
        labelairq0->setText(QCoreApplication::translate("Weather", "\344\274\230", nullptr));
        labelairq1->setText(QCoreApplication::translate("Weather", "\350\211\257", nullptr));
        labelairq2->setText(QCoreApplication::translate("Weather", "\344\274\230", nullptr));
        labelairq3->setText(QCoreApplication::translate("Weather", "\344\274\230", nullptr));
        labelairq4->setText(QCoreApplication::translate("Weather", "\344\274\230", nullptr));
        labelairq5->setText(QCoreApplication::translate("Weather", "\344\274\230", nullptr));
        labelFX5->setText(QCoreApplication::translate("Weather", "\344\270\234\345\215\227\351\243\216", nullptr));
        labelFX3->setText(QCoreApplication::translate("Weather", "\344\270\234\345\215\227\351\243\216", nullptr));
        labelFL5->setText(QCoreApplication::translate("Weather", "1\347\272\247", nullptr));
        labelFX0->setText(QCoreApplication::translate("Weather", "\344\270\234\345\215\227\351\243\216", nullptr));
        labelFX1->setText(QCoreApplication::translate("Weather", "\344\270\234\345\215\227\351\243\216", nullptr));
        labelFX2->setText(QCoreApplication::translate("Weather", "\344\270\234\345\215\227\351\243\216", nullptr));
        labelFL0->setText(QCoreApplication::translate("Weather", "1\347\272\247", nullptr));
        labelFL3->setText(QCoreApplication::translate("Weather", "1\347\272\247", nullptr));
        labelFL2->setText(QCoreApplication::translate("Weather", "1\347\272\247", nullptr));
        labelFL4->setText(QCoreApplication::translate("Weather", "1\347\272\247", nullptr));
        labelFX4->setText(QCoreApplication::translate("Weather", "\344\270\234\345\215\227\351\243\216", nullptr));
        labelFL1->setText(QCoreApplication::translate("Weather", "1\347\272\247", nullptr));
    } // retranslateUi

};

namespace Ui {
    class Weather: public Ui_Weather {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_WEATHER_H
