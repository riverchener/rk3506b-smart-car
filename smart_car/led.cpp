#include "led.h"
#include "hal.h"
#include <QDebug>

Led::Led(QWidget *parent) : QMainWindow(parent)
{
    qInfo() << "板载 LED 使用:" << qEnvironmentVariable("SMARTCAR_LED_BRIGHTNESS",
                                                         "/sys/class/leds/work/brightness");
}

int Led::led_write(char* led_buf)
{
    if (!led_buf)
        return -1;
    // led_buf[1] == 0 表示点亮（沿用原工程语义）
    Hal::instance().ledSet(led_buf[1] == 0);
    return 1;
}
