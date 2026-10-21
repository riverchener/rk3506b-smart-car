#include "gpsservice.h"

#include <QDebug>
#include <QFileInfo>
#include <QSerialPort>
#include <QSerialPortInfo>

GpsService &GpsService::instance()
{
    static GpsService s;
    return s;
}

GpsService::GpsService(QObject *parent)
    : QObject(parent)
{
}

GpsService::~GpsService()
{
    if (m_port) {
        if (m_port->isOpen())
            m_port->close();
        delete m_port;
        m_port = nullptr;
    }
}

/*
 * 打开 GPS 串口。Port/baud 可用环境变量覆盖：
 *   SMARTCAR_GPS_PORT（默认 /dev/ttyS2）  SMARTCAR_GPS_BAUD（默认 9600）
 * 打开失败说明没插模块 —— 记住这个结论，返回 false，不再反复重试。
 */
bool GpsService::start()
{
    if (m_started)
        return m_deviceOk;
    m_started = true;

    const QString requestedPort = qEnvironmentVariable("SMARTCAR_GPS_PORT", "/dev/ttyS2");
    QString selectedPort = requestedPort;
    if (!QFileInfo::exists(selectedPort)) {
        for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
            if (info.systemLocation() == requestedPort || info.portName() == requestedPort) {
                selectedPort = info.systemLocation();
                break;
            }
        }
    }

    m_port = new QSerialPort(this);
    m_port->setPortName(selectedPort);
    if (!m_port->open(QIODevice::ReadWrite)) {
        qWarning().noquote() << "gps: 串口打开失败（按无 GPS 模块处理）" << selectedPort
                             << "可用 SMARTCAR_GPS_PORT 指定实际设备节点";
        m_deviceOk = false;
        return false;
    }

    bool baudOk = false;
    int baud = qEnvironmentVariableIntValue("SMARTCAR_GPS_BAUD", &baudOk);
    if (!baudOk || baud <= 0)
        baud = 9600;
    m_port->setBaudRate(baud, QSerialPort::AllDirections);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setFlowControl(QSerialPort::NoFlowControl);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setStopBits(QSerialPort::OneStop);
    connect(m_port, &QSerialPort::readyRead, this, &GpsService::onReadyRead);

    m_deviceOk = true;
    qInfo().noquote() << "gps: 串口已打开" << selectedPort << "baud" << baud;
    return true;
}

/*
 * 串口可能一次给半行，也可能一次给好几行 —— 所以先攒进缓冲，按 '\n' 切完整行。
 * （旧实现直接 readLine()，半行会被丢掉，这也是偶发丢定位的一个来源。）
 */
void GpsService::onReadyRead()
{
    if (!m_port)
        return;
    m_buf.append(m_port->readAll());

    int nl;
    while ((nl = m_buf.indexOf('\n')) >= 0) {
        QByteArray lineBytes = m_buf.left(nl);
        m_buf.remove(0, nl + 1);
        if (lineBytes.endsWith('\r'))
            lineBytes.chop(1);
        feedLine(QString::fromLatin1(lineBytes));
    }

    /* 防止没有换行的垃圾数据把缓冲撑爆 */
    if (m_buf.size() > 4096)
        m_buf.clear();
}

bool GpsService::feedLine(const QString &line)
{
    const QString loc = parseNmeaGga(line);
    if (loc.isEmpty())
        return false;

    const QStringList parts = loc.split(',');
    if (parts.count() != 2)
        return false;
    bool okLng = false, okLat = false;
    const double lng = parts.at(0).toDouble(&okLng);
    const double lat = parts.at(1).toDouble(&okLat);
    if (!okLng || !okLat)
        return false;

    m_hasFix = true;
    m_lngWgs = lng;
    m_latWgs = lat;
    emit fixReady(lng, lat);
    return true;
}

/*
 * 解析 $GPGGA / $GNGGA / $GBGGA / $BDGGA：
 * 字段 2=纬度 ddmm.mmmm，3=N/S，4=经度 dddmm.mmmm，5=E/W，6=定位质量
 * 返回 "经度,纬度"（WGS84 十进制度，6 位小数）；无效/未定位返回空串。
 */
QString GpsService::parseNmeaGga(const QString &line)
{
    const QString s = line.trimmed();
    /* 只认 GGA，且必须是定位有效的语句 */
    if (!(s.startsWith("$GPGGA") || s.startsWith("$GNGGA")
          || s.startsWith("$GBGGA") || s.startsWith("$BDGGA")))
        return QString();
    if (s.contains(",,,,,"))
        return QString();

    const QStringList f = s.split(',');
    if (f.count() < 10)
        return QString();

    /* 定位质量 0 = 未定位，此时经纬度可能是 0 或者空，必须丢掉 */
    bool okQ = false;
    const int quality = f.at(6).toInt(&okQ);
    if (okQ && quality == 0)
        return QString();

    bool okLat = false, okLng = false;
    const double rawLat = f.at(2).toDouble(&okLat);
    const double rawLng = f.at(4).toDouble(&okLng);
    if (!okLat || !okLng || rawLat == 0.0 || rawLng == 0.0)
        return QString();

    /* ddmm.mmmm / dddmm.mmmm -> 十进制度 */
    const int latDeg = static_cast<int>(rawLat / 100);
    const double lat = latDeg + (rawLat / 100 - latDeg) * 100.0 / 60.0;

    const int lngDeg = static_cast<int>(rawLng / 100);
    const double lng = lngDeg + (rawLng / 100 - lngDeg) * 100.0 / 60.0;

    const QString ns = f.at(3).trimmed().toUpper();
    const QString ew = f.at(5).trimmed().toUpper();
    const double signedLat = (ns == "S") ? -lat : lat;
    const double signedLng = (ew == "W") ? -lng : lng;

    return QString::number(signedLng, 'f', 6) + ',' + QString::number(signedLat, 'f', 6);
}
