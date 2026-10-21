#include "buzzer.h"
#include "hal.h"
#include <QDebug>

Buzzer::Buzzer(QWidget *parent) : QMainWindow(parent)
{
    // 上电先确保静音，避免“一通电就响”
    Hal::instance().buzzerSet(false);
    qInfo() << "蜂鸣器使用硬件抽象层控制, GPIO ="
            << qEnvironmentVariable("SMARTCAR_BUZZER_GPIO", "56(A37)")
            << " 触发电平:"
            << (Hal::instance().buzzerActiveLow() ? "低电平响" : "高电平响")
            << "(改 /etc/smartcar.conf 里的 SMARTCAR_BUZZER_ACTIVE_LOW)";
}

int Buzzer::buzzer_write(char buf)
{
    // 原有语义：0 = 距离过近要报警（响）；其他值 = 静音
    Hal::instance().buzzerSet(buf == 0);
    return 1;
}
