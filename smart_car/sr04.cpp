#include "sr04.h"
#include "hal.h"
#include <QDebug>
#include <QTimer>

Sr04::Sr04(QWidget *parent) : QMainWindow(parent)
{
    qInfo() << "SR04 使用硬件抽象层读取, TRIG/ECHO GPIO ="
            << qEnvironmentVariable("SMARTCAR_SR04_TRIG_GPIO", "54(GPIO1_C6)")
            << "/" << qEnvironmentVariable("SMARTCAR_SR04_ECHO_GPIO", "55(GPIO1_C7)");
    sr04_timer = new QTimer(this);
    /*
     * 200 ms：测距界面的真正瓶颈在这里。M0 每 100 ms 采一次距离并通过 rpmsg 推上来
     * （节点实测约 20 Hz 变化），但界面原来 2 s 才读一次，所以看起来"更新很慢"。
     * 改成 200 ms 后实测界面读取频率 0.6 Hz -> 5 Hz。板子上已部署的二进制做的
     * 是同一处改动（见 refs/amp/patch_app_timer.py），重新编译本源码结果一致。
     */
    sr04_timer->setInterval(200);
    connect(sr04_timer, &QTimer::timeout, this, &Sr04::timeto_read_sr04data);
    sr04_timer->start();
}

void Sr04::sr04_read(int *buf)
{
    // 单位：微秒（沿用原工程含义）
    double cm = 0.0;
    if (Hal::instance().sr04ReadCm(&cm) && cm > 0.0) {
        lastUs = (int)(cm * 58.0);
        lastOk = true;
        *buf = lastUs;
        return;
    }
    if (lastOk) {
        *buf = lastUs;
        return;
    }
    *buf = 0;
}

void Sr04::timeto_read_sr04data()
{
    emit readyto_read_sr04data();
}
