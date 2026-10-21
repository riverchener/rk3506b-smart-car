#include "dht11.h"
#include "hal.h"
#include <QDebug>
#include <QTimer>

DHT11::DHT11(QWidget *parent) : QMainWindow(parent)
{
    qInfo() << "DHT11 使用硬件抽象层读取, 数据脚 GPIO ="
            << qEnvironmentVariable("SMARTCAR_DHT11_GPIO", "51(GPIO1_C3)");
    dht11_timer = new QTimer(this);
    /*
     * 200 ms：读数本身由 A7 本地 bit-bang（内核模块 sc_sensors.ko）完成，DHT11 芯片
     * 的转换速度上限是 1 Hz，所以界面 200 ms 去读只是更快地取到"最新一次有效读数"，
     * 真正变化仍约每秒一次——这是器件限制，不是软件问题。
     */
    dht11_timer->setInterval(200);
    connect(dht11_timer, &QTimer::timeout, this, &DHT11::timeto_read_dht11data);
    dht11_timer->start();
}

void DHT11::dht11_read(char *buf)
{
    unsigned char data[4] = {0, 0, 0, 0};
    if (Hal::instance().dht11Read(data, 4)) {
        last[0] = buf[0] = (char)data[0]; // 湿度整数
        last[1] = buf[1] = (char)data[1]; // 湿度小数
        last[2] = buf[2] = (char)data[2]; // 温度整数
        last[3] = buf[3] = (char)data[3]; // 温度小数
        lastOk = true;
        return;
    }
    // 读取失败：保留上一次有效值，避免界面跳变
    if (lastOk) {
        buf[0] = last[0];
        buf[1] = last[1];
        buf[2] = last[2];
        buf[3] = last[3];
        return;
    }
    buf[0] = 0;
    buf[1] = 0;
    buf[2] = 0;
    buf[3] = 0;
}

void DHT11::timeto_read_dht11data()
{
    emit readyto_read_dht11data();
}
