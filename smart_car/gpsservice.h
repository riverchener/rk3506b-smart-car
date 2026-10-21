#ifndef GPSSERVICE_H
#define GPSSERVICE_H

/*
 * GpsService —— 全局唯一的 GPS 串口读取服务
 *
 * 为什么要有这个类：地图页和天气页都需要定位，但串口是**独占**资源，
 * 两个页面各开一次 /dev/ttyS2 会互相打架（读到的数据会错乱、丢帧）。
 * 所以由这个单例独占串口，谁来问就发给谁。
 *
 * 对外提供：
 *   - fixReady(lngWgs, latWgs)  收到一帧有效定位（NMEA 是 WGS84 原始值）
 *   - hasFix() / lngWgs() / latWgs()  当前是否已定位、坐标是多少
 *   - deviceAvailable()         串口是否打开成功（= 有没有 GPS 模块）
 *   - start()                   懒启动；没有 GPS 模块时返回 false，不会反复重试
 *
 * 注意：地图页用 GCJ02（高德底图要纠偏），天气页做逆地理编码用 WGS84。
 * 这里只发 **WGS84 原始值**，需要 GCJ02 的调用方自己纠偏（Map::wgs84ToGcj02）。
 */

#include <QObject>
#include <QString>
#include <QByteArray>

QT_BEGIN_NAMESPACE
class QSerialPort;
QT_END_NAMESPACE

class GpsService : public QObject
{
    Q_OBJECT
public:
    static GpsService &instance();

    bool start();                      /* 已启动则直接返回上次结果 */
    bool deviceAvailable() const { return m_deviceOk; }
    bool started() const { return m_started; }

    bool hasFix() const { return m_hasFix; }
    double lngWgs() const { return m_lngWgs; }
    double latWgs() const { return m_latWgs; }

    /* NMEA $GPGGA/$GNGGA -> "经度,纬度"（WGS84，保留 6 位）；无效返回空串 */
    static QString parseNmeaGga(const QString &line);

    /*
     * 把一行 NMEA 喂给服务处理（等价于从串口读到这一行）。
     * 生产路径就是串口 -> feedLine()，公开出来是为了：
     *   - 自检工具能在没有真实 GPS 模块时验证"解析 + 纠偏 + 逆地理编码"整条链路
     *   - 单元测试可以确定性地构造数据
     * 返回 true 表示这一行解析出了有效定位并已发出 fixReady。
     */
    bool feedLine(const QString &line);

signals:
    void fixReady(double lngWgs, double latWgs);

private slots:
    void onReadyRead();

private:
    explicit GpsService(QObject *parent = nullptr);
    ~GpsService() override;
    GpsService(const GpsService &) = delete;
    GpsService &operator=(const GpsService &) = delete;

    QSerialPort *m_port = nullptr;
    QByteArray m_buf;
    bool m_started = false;
    bool m_deviceOk = false;
    bool m_hasFix = false;
    double m_lngWgs = 0.0;
    double m_latWgs = 0.0;
};

#endif // GPSSERVICE_H
