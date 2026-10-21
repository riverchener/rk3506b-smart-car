/********************************************************************************
** Form generated from reading UI file 'rotatablewidget.ui'
**
** Created by: Qt User Interface Compiler version 5.15.18
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_ROTATABLEWIDGET_H
#define UI_ROTATABLEWIDGET_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_RotatableWidget
{
public:

    void setupUi(QWidget *RotatableWidget)
    {
        if (RotatableWidget->objectName().isEmpty())
            RotatableWidget->setObjectName(QString::fromUtf8("RotatableWidget"));
        RotatableWidget->resize(400, 300);

        retranslateUi(RotatableWidget);

        QMetaObject::connectSlotsByName(RotatableWidget);
    } // setupUi

    void retranslateUi(QWidget *RotatableWidget)
    {
        RotatableWidget->setWindowTitle(QCoreApplication::translate("RotatableWidget", "Form", nullptr));
    } // retranslateUi

};

namespace Ui {
    class RotatableWidget: public Ui_RotatableWidget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_ROTATABLEWIDGET_H
