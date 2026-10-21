#include "mq135.h"
#include "hal.h"
#include <QDebug>

/*
 * MQ-135 采样节拍器。
 *
 * 只做一件事：每 2 秒发一个 readyto_read_mq135data()，由 Hardware::mq135_handler()
 * 调 Hal::mq135Sample() 拿"原始值 / 电压 / 基线比值 / 状态"。
 *
 * 2 秒这个周期不是随手定的：Hal::mq135Sample() 每次要连抓 20 个 ADC 码值
 * （≈8ms）来判断这根线是"有人驱动的带噪声信号"还是"悬空漂移"，
 * 而且判定要连续攒 4 个采样窗口才成立 —— 2 秒一次正好 = 8 秒出首次结论。
 */
Mq135::Mq135(QWidget *parent) : QMainWindow(parent)
{
    qInfo() << "MQ135 使用 ADC 节点:"
            << qEnvironmentVariable("SMARTCAR_MQ135_ADC",
                                    "/sys/bus/iio/devices/iio:device0/in_voltage3_raw");

    mq135_timer = new QTimer(this);
    mq135_timer->setInterval(2000);
    connect(mq135_timer, &QTimer::timeout, this, &Mq135::timeto_read_mq135data);
    mq135_timer->start();
}

void Mq135::timeto_read_mq135data()
{
    emit readyto_read_mq135data();
}
