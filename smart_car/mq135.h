#ifndef MQ135_H
#define MQ135_H

#include <QMainWindow>
#include <QTimer>

/*
 * MQ-135 空气质量的采样节拍（Qt 侧）。
 *
 * 这个类**不做**任何电气/换算工作 —— 真正的读取与判定都在 Hal 里
 * （hal.cpp: Hal::mq135Sample() / Hal::mq135Connected()，见 hal.h 的注释）：
 *   - 原始码值：读 /sys/bus/iio/devices/iio:device0/in_voltage3_raw
 *   - 电压：     raw * in_voltage_scale（本板 1.7578125 mV/LSB）
 *   - 接线判定：连续采样窗口的波形特征（悬空脚 = 死平 / 单调漂移）
 *
 * 这里只负责"每 2 秒踢一次"，让 Hardware::mq135_handler() 去刷新界面。
 * 以前这里还有个 mq135_read(char*)：读失败时返回**上一次的有效值**，
 * 结果界面永远不会显示"读不到"，是一条隐性的伪造数据路径 —— 已经删掉。
 */
class Mq135 : public QMainWindow
{
    Q_OBJECT
public:
    explicit Mq135(QWidget *parent = nullptr);
public slots:
    void timeto_read_mq135data(void);
signals:
    void readyto_read_mq135data();
private:
    QTimer *mq135_timer;
};

#endif // MQ135_H
