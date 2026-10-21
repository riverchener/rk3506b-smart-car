#include "map.h"
#include "gpsservice.h"

#include "commonhelper.h"
#include "uiadapter.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QAbstractSocket>
#include <QSslConfiguration>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPixmap>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QSerialPortInfo>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>
#include <cmath>

namespace {

const double kPi = 3.14159265358979323846;
const double kEarthAxis = 6378245.0;              /* 克拉索夫斯基椭球长半轴 */
const double kEccentricity = 0.00669342162296594323;
const int kTileSize = 256;
const int kMaxCachedTiles = 400;                  /* 缓存上限，超了整表清掉（简单够用） */

/* GCJ02 偏移量计算（火星坐标），国内底图必须做这个纠偏 */
double transformLat(double x, double y)
{
    double ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y
                 + 0.2 * std::sqrt(std::fabs(x));
    ret += (20.0 * std::sin(6.0 * x * kPi) + 20.0 * std::sin(2.0 * x * kPi)) * 2.0 / 3.0;
    ret += (20.0 * std::sin(y * kPi) + 40.0 * std::sin(y / 3.0 * kPi)) * 2.0 / 3.0;
    ret += (160.0 * std::sin(y / 12.0 * kPi) + 320.0 * std::sin(y * kPi / 30.0)) * 2.0 / 3.0;
    return ret;
}

double transformLon(double x, double y)
{
    double ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y
                 + 0.1 * std::sqrt(std::fabs(x));
    ret += (20.0 * std::sin(6.0 * x * kPi) + 20.0 * std::sin(2.0 * x * kPi)) * 2.0 / 3.0;
    ret += (20.0 * std::sin(x * kPi) + 40.0 * std::sin(x / 3.0 * kPi)) * 2.0 / 3.0;
    ret += (150.0 * std::sin(x / 12.0 * kPi) + 300.0 * std::sin(x / 30.0 * kPi)) * 2.0 / 3.0;
    return ret;
}

bool outOfChina(double lng, double lat)
{
    return (lng < 72.004 || lng > 137.8347 || lat < 0.8293 || lat > 55.8271);
}

const char *kButtonQss =
    "QPushButton { background: rgba(255,255,255,0.88); border: 2px solid #1F4E79;"
    " border-radius: 8px; color: #1F4E79; font-size: 22px; font-weight: bold; }"
    "QPushButton:pressed { background: #1F4E79; color: white; }";

}  // namespace

Map::Map(QWidget *parent) : QDialog(parent)
{
    if (m_zoom < 3 || m_zoom > 18)
        m_zoom = 15;                 /* 15 级大约 1 屏 4~5 公里，看位置刚好 */
    if (m_zoomMin < 3 || m_zoomMin > 18)
        m_zoomMin = 12;              /* 再往外拉就没意义了（省/国范围） */
    if (m_zoomMax <= m_zoomMin || m_zoomMax > 18)
        m_zoomMax = 18;              /* 18 级是街道/门牌级别 */
    if (m_zoom < m_zoomMin)
        m_zoom = m_zoomMin;
    if (m_zoom > m_zoomMax)
        m_zoom = m_zoomMax;
    if (m_panMaxDeg <= 0.0) {
        bool ok = false;
        const double env = qEnvironmentVariable("SMARTCAR_MAP_PAN_MAX_DEG").toDouble(&ok);
        m_panMaxDeg = (ok && env > 0.0) ? env : 0.45;      /* 0.45° ≈ 50km，够逛一圈城市 */
    }
    initUi();
}

Map::~Map()
{
    /* GPS 串口由全局 GpsService 持有，在地图页析构时**不能**关它 ——
     * 天气页可能还要用同一个定位（两个页面共用一个串口）。 */
}

void Map::initUi()
{
    auto pMovie = new QMovie(this);
    pMovie->setFileName(":/images/loading.gif");
    pMovie->start();
    UiAdapter::fit(this);
    m_logoLabel.setObjectName("logoLabel");
    m_logoLabel.setText(QString("正在获取定位…"));    m_back = new QPushButton(tr("返回"), this);
    m_iconLabel.setMovie(pMovie);
    m_iconLabel.setAlignment(Qt::AlignCenter);
    m_iconLabel.setObjectName("mapCanvasLabel");   /* 自检脚本按名字找它注入拖动事件 */
    m_iconLabel.installEventFilter(this);     /* 拖动平移靠它（见 eventFilter） */
    m_logoLabel.setAlignment(Qt::AlignRight);

    /* 缩放 + 回到定位：贴在地图上，手指点得着 */
    m_zoomOutBtn = new QPushButton(QStringLiteral("−"), this);
    m_zoomInBtn = new QPushButton(QStringLiteral("+"), this);
    m_recenterBtn = new QPushButton(tr("回到定位"), this);
    for (QPushButton *b : { m_zoomOutBtn, m_zoomInBtn, m_recenterBtn }) {
        b->setStyleSheet(QString::fromLatin1(kButtonQss));
        b->setMinimumSize(72, 56);
        b->setFocusPolicy(Qt::NoFocus);
    }
    m_recenterBtn->setMinimumWidth(150);
    /* 返回键做高一点：原来只有一行字的高度，手指不好点 */
    CommonHelper::makeBackButtonBig(m_back, 200, 62, 22);
    connect(m_zoomOutBtn, &QPushButton::clicked, this, &Map::zoomOut);
    connect(m_zoomInBtn, &QPushButton::clicked, this, &Map::zoomIn);
    connect(m_recenterBtn, &QPushButton::clicked, this, &Map::recenter);
    connect(m_back, &QPushButton::clicked, this, &Map::on_back_clicked);

    auto *pZoomRow = new QHBoxLayout;
    pZoomRow->addStretch();
    pZoomRow->addWidget(m_zoomOutBtn);
    pZoomRow->addWidget(m_zoomInBtn);
    pZoomRow->addWidget(m_recenterBtn);
    pZoomRow->addSpacing(12);

    auto *pVLayout = new QVBoxLayout;
    pVLayout->addStretch();
    pVLayout->addWidget(&m_logoLabel);
    pVLayout->addLayout(pZoomRow);
    pVLayout->addWidget(m_back);
    m_iconLabel.setLayout(pVLayout);

    auto *pMainLayout = new QVBoxLayout;
    pMainLayout->addWidget(&m_iconLabel);
    pMainLayout->setMargin(0);
    setLayout(pMainLayout);
    setObjectName("map_widget");
    CommonHelper::setStyleSheet(":/style/default.qss", this);

    /* 拖动/缩放停下来 250ms 再补缺失的瓦片，避免边拖边发请求 */
    m_fetchTimer.setSingleShot(true);
    m_fetchTimer.setInterval(250);
    connect(&m_fetchTimer, &QTimer::timeout, this, &Map::onFetchTimeout);
}

void Map::initCtrl()
{
    if (m_inited)
        return;
    m_inited = true;

    /* 等 8 秒还没有 GPS 定位就退到 IP 定位（模块没插/天线没信号都会走这条） */
    m_gpsTimeout.setSingleShot(true);
    m_gpsTimeout.setInterval(qEnvironmentVariableIntValue("SMARTCAR_GPS_TIMEOUT_MS") > 0
                             ? qEnvironmentVariableIntValue("SMARTCAR_GPS_TIMEOUT_MS") : 8000);
    connect(&m_gpsTimeout, &QTimer::timeout, this, &Map::onGpsTimeout);

    initSerialPort();

    if (m_gpsDeviceOk) {
        m_gpsTimeout.start();
        return;                       /* 先等 GPS，等不到再 IP 定位 */
    }

    /* 没有 GPS 模块：直接按公网 IP 定位（和天气页同一个思路） */
    locateByIp();
}

// 串口初始化：交给全局唯一的 GpsService（地图页和天气页共用，避免抢串口）
void Map::initSerialPort()
{
    GpsService &gps = GpsService::instance();
    connect(&gps, &GpsService::fixReady, this, &Map::onGpsFix);
    m_gpsDeviceOk = gps.start();
    if (!m_gpsDeviceOk)
        qWarning() << "map: 没打开 GPS 串口，按无 GPS 模块处理";
}

/* 有没有网：遍历网卡找一个非回环 IPv4（WiFi 拿到 DHCP 就会出现） */
bool Map::networkOnline()
{
    const QList<QHostAddress> addrs = QNetworkInterface::allAddresses();
    for (const QHostAddress &a : addrs) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol
            && !a.isLoopback()
            && !a.isLinkLocal()) {
            return true;
        }
    }
    return false;
}

/*
 * 公网 IP 定位。用的是"服务器看请求来源 IP"这一招 —— 和天气页不给城市名时
 * 服务端按 IP 猜城市是同一个思路，不需要任何 key，也不需要用户填城市。
 */
void Map::locateByIp()
{
    m_usingIp = true;

    if (!networkOnline()) {
        /* 没有 GPS 模块也没网：退回配置里的默认坐标，并说清楚 */
        const QStringList parts = m_location.split(',');
        if (parts.count() == 2) {
            double lng = 0.0, lat = 0.0;
            wgs84ToGcj02(parts.at(0).toDouble(), parts.at(1).toDouble(), &lng, &lat);
            showMapAt(lng, lat, QString("无 GPS 模块，且当前无网络 → 显示默认坐标"));
        } else {
            m_logoLabel.setText("无 GPS 模块，且当前无网络，无法定位");
        }
        return;
    }

    m_logoLabel.setText("未检测到 GPS 模块，正在按网络 IP 定位…");
    qInfo().noquote() << "map: 无 GPS 模块，改用 IP 定位" << m_ipUrl;
    requestNetwork(m_ipUrl, NetIp, 0, 0, 0);
}

/* 世界像素 <-> 经纬度（Web Mercator，256px 瓦片） */
QPointF Map::worldPixel(double lng, double lat, int z)
{
    const double world = std::pow(2.0, z) * kTileSize;
    const double latRad = lat * kPi / 180.0;
    const double x = (lng + 180.0) / 360.0 * world;
    double s = std::sin(latRad);
    s = qBound(-0.9999, s, 0.9999);
    const double y = (0.5 - std::log((1 + s) / (1 - s)) / (4 * kPi)) * world;
    return QPointF(x, y);
}

void Map::worldPixelToLngLat(double px, double py, int z, double *lng, double *lat)
{
    const double world = std::pow(2.0, z) * kTileSize;
    *lng = px / world * 360.0 - 180.0;
    const double n = kPi - 2.0 * kPi * py / world;
    *lat = 180.0 / kPi * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
}

/* 拿到坐标后的统一入口：source 会显示在界面上，便于区分 GPS / IP / 默认。
 * 传入的是纠偏后的 GCJ02（和高德底图一致），界面显示的也是它。 */
void Map::showMapAt(double lngGcj, double latGcj, const QString &source)
{
    m_locLng = lngGcj;
    m_locLat = latGcj;
    m_source = source;

    const QPointF p = worldPixel(m_locLng, m_locLat, m_zoom);
    m_locPxX = p.x();
    m_locPxY = p.y();

    /* 用户自己拖过/缩过就不要再被"定位更新"拽回去；没动过则跟随定位 */
    if (!m_userMoved || m_canvas.isNull()) {
        m_viewPxX = m_locPxX;
        m_viewPxY = m_locPxY;
    }

    qInfo().noquote() << QString("map: %1 -> 定位 %2,%3 zoom=%4")
                         .arg(source)
                         .arg(lngGcj, 0, 'f', 6)
                         .arg(latGcj, 0, 'f', 6)
                         .arg(m_zoom);

    applyView(true);
}

/* 根据 m_viewPxX/Y + m_zoom 重画，并按需补拉缺失的瓦片 */
void Map::applyView(bool fetchMissing)
{
    QSize size = m_iconLabel.size();
    if (size.width() < 128 || size.height() < 128) {
        const QSize cs = m_canvas.size();
        size = (cs.width() >= 128 && cs.height() >= 128) ? cs : QSize(1024, 600);
    }

    if (m_canvas.size() != size)
        m_canvas = QImage(size, QImage::Format_RGB32);
    m_canvas.fill(QColor(0xE8, 0xEC, 0xF0));
    /* 有图之后就不需要 loading 动画了：直接贴 pixmap（QLabel 会替换掉 movie 的显示） */

    const int originX = static_cast<int>(std::floor(m_viewPxX - size.width() / 2.0));
    const int originY = static_cast<int>(std::floor(m_viewPxY - size.height() / 2.0));
    const int x0 = originX / kTileSize;
    const int y0 = originY / kTileSize;
    const int x1 = (originX + size.width() - 1) / kTileSize;
    const int y1 = (originY + size.height() - 1) / kTileSize;
    const int n = 1 << m_zoom;

    int missing = 0;
    QPainter p(&m_canvas);
    for (int y = y0; y <= y1; ++y) {
        if (y < 0 || y >= n)
            continue;
        for (int x = x0; x <= x1; ++x) {
            if (x < 0 || x >= n)
                continue;
            const QString key = QStringLiteral("%1/%2/%3").arg(m_zoom).arg(x).arg(y);
            const QImage tile = m_tileCache.value(key);
            if (!tile.isNull()) {
                p.drawImage(QPoint(x * kTileSize - originX, y * kTileSize - originY), tile);
            } else {
                missing++;
                if (fetchMissing) {
                    const int host = 1 + ((x + y) & 0x3);   /* 4 台主机轮换 */
                    requestNetwork(m_tileUrl.arg(host).arg(x).arg(y).arg(m_zoom),
                                   NetTile, x, y, m_zoom);
                }
            }
        }
    }
    drawMarker(&p, QPoint(originX, originY));
    p.end();

    m_iconLabel.setPixmap(QPixmap::fromImage(m_canvas));
    updateInfoLabel();

    if (fetchMissing) {
        m_pendingTiles = missing;
        m_failedTiles = 0;
        if (missing == 0)
            qInfo().noquote() << QString("map: 视口全部命中缓存（z=%1 中心像素 %2,%3）")
                                 .arg(m_zoom).arg(int(m_viewPxX)).arg(int(m_viewPxY));
    }
}

void Map::scheduleFetch()
{
    m_fetchTimer.start();
}

void Map::onFetchTimeout()
{
    applyView(true);
}

void Map::drawMarker(QPainter *p, const QPoint &origin)
{
    const QPoint c(int(m_locPxX - origin.x()), int(m_locPxY - origin.y()));
    /* 定位点跑出画布太多就不画（免得边缘出现半个点） */
    if (c.x() < -30 || c.y() < -30 || c.x() > m_canvas.width() + 30 || c.y() > m_canvas.height() + 30)
        return;

    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(QPen(QColor(0x1F, 0x4E, 0x79), 2));
    p->drawLine(c.x() - 18, c.y(), c.x() - 6, c.y());
    p->drawLine(c.x() + 6, c.y(), c.x() + 18, c.y());
    p->drawLine(c.x(), c.y() - 18, c.x(), c.y() - 6);
    p->drawLine(c.x(), c.y() + 6, c.x(), c.y() + 18);
    p->setPen(QPen(Qt::white, 3));
    p->setBrush(QColor(0xE5, 0x39, 0x35));
    p->drawEllipse(c, 10, 10);
}

void Map::updateInfoLabel()
{
    double cLng = 0.0, cLat = 0.0;
    worldPixelToLngLat(m_viewPxX, m_viewPxY, m_zoom, &cLng, &cLat);
    m_logoLabel.setText(QString("%1 | z%2 中心 %3,%4 | 拖动平移")
                        .arg(m_source)
                        .arg(m_zoom)
                        .arg(cLng, 0, 'f', 5)
                        .arg(cLat, 0, 'f', 5));
}

void Map::requestNetwork(const QString &url, int kind, int tx, int ty, int z)
{
    QNetworkRequest networkRequest{ QUrl(url) };
    /* 高德瓦片是 http；这里保留"不校验证书"的宽松设置，方便以后换 https 源 */
    QSslConfiguration config;
    config.setPeerVerifyMode(QSslSocket::VerifyNone);
    networkRequest.setSslConfiguration(config);

    QNetworkReply *reply = m_networkAccessManager.get(networkRequest);
    reply->setProperty("kind", kind);
    reply->setProperty("tx", tx);
    reply->setProperty("ty", ty);
    reply->setProperty("tz", z);
    connect(reply, &QNetworkReply::finished, this, &Map::netReplyFinished);
}

void Map::netReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;
    const int kind = reply->property("kind").toInt();
    const QByteArray data = reply->readAll();
    const bool ok = (reply->error() == QNetworkReply::NoError);
    const int tx = reply->property("tx").toInt();
    const int ty = reply->property("ty").toInt();
    const int tz = reply->property("tz").toInt();
    reply->deleteLater();

    /* ---- 瓦片 ---- */
    if (kind == NetTile) {
        if (!ok) {
            m_failedTiles++;
            qWarning() << "map: tile failed" << tz << tx << ty << reply->errorString();
        } else {
            QImage tile;
            if (tile.loadFromData(data)) {
                if (m_tileCache.size() > kMaxCachedTiles)
                    m_tileCache.clear();
                m_tileCache.insert(QStringLiteral("%1/%2/%3").arg(tz).arg(tx).arg(ty), tile);
            } else {
                m_failedTiles++;
            }
        }
        if (m_pendingTiles > 0)
            m_pendingTiles--;
        /* 新到的瓦片只贴上去，不再发新请求（拖动中不会有雪崩式请求） */
        if (m_pendingTiles == 0)
            applyView(false);
        if (m_pendingTiles == 0 && m_failedTiles > 0)
            qWarning().noquote() << QString("map: 本轮有 %1 张瓦片失败").arg(m_failedTiles);
        return;
    }

    /* ---- IP 定位 ---- */
    if (kind == NetIp) {
        if (!ok) {
            qWarning() << "map: IP 定位请求失败" << reply->errorString();
            m_logoLabel.setText("IP 定位请求失败（网络不通？）");
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(data).object();
        if (obj.value("status").toString() != "success") {
            qWarning().noquote() << "map: IP 定位返回异常" << QString::fromUtf8(data).left(200);
            m_logoLabel.setText("IP 定位服务返回异常");
            return;
        }
        const double lat = obj.value("lat").toDouble();
        const double lon = obj.value("lon").toDouble();
        if (lat == 0.0 && lon == 0.0) {
            m_logoLabel.setText("IP 定位没有拿到经纬度");
            return;
        }
        QString place = obj.value("city").toString();
        const QString region = obj.value("regionName").toString();
        if (!region.isEmpty() && !place.startsWith(region))
            place = region + " " + place;
        const QString ip = obj.value("query").toString();

        double gLng = lon, gLat = lat;
        wgs84ToGcj02(lon, lat, &gLng, &gLat);
        m_userMoved = false;
        showMapAt(gLng, gLat, QString("IP 定位 %1 IP %2").arg(place, ip));
        return;
    }
}

/* ---------- 交互：拖动平移 / 缩放 ---------- */

void Map::panByPixels(double dx, double dy)
{
    if (m_canvas.isNull())
        return;
    m_viewPxX += dx;
    m_viewPxY += dy;
    clampView();
    m_userMoved = true;
    applyView(false);          /* 先用已有瓦片平移，松手后再补图 */
}

void Map::clampView()
{
    /* 限制：视图中心不能离定位点超过 m_panMaxDeg 度（纬度再夹到墨卡托有效范围） */
    double lng = 0.0, lat = 0.0;
    worldPixelToLngLat(m_viewPxX, m_viewPxY, m_zoom, &lng, &lat);
    const double cl = qBound(m_locLng - m_panMaxDeg, lng, m_locLng + m_panMaxDeg);
    const double ca = qBound(qMax(-85.0, m_locLat - m_panMaxDeg), lat,
                             qMin(85.0, m_locLat + m_panMaxDeg));
    if (cl == lng && ca == lat)
        return;
    const QPointF p = worldPixel(cl, ca, m_zoom);
    m_viewPxX = p.x();
    m_viewPxY = p.y();
}

void Map::zoomBy(int delta)
{
    if (m_canvas.isNull() && m_viewPxX == 0.0 && m_viewPxY == 0.0)
        return;
    const int nz = qBound(m_zoomMin, m_zoom + delta, m_zoomMax);
    if (nz == m_zoom)
        return;

    /* 缩放：世界像素按 2^(dz) 缩放，中心点保持不变 */
    const double f = std::pow(2.0, nz - m_zoom);
    m_viewPxX *= f;
    m_viewPxY *= f;
    m_locPxX *= f;
    m_locPxY *= f;
    m_zoom = nz;
    m_userMoved = true;

    applyView(true);
    qInfo().noquote() << QString("map: 缩放 -> z%1").arg(m_zoom);
}

void Map::zoomIn()
{
    zoomBy(+1);
}

void Map::zoomOut()
{
    zoomBy(-1);
}

void Map::recenter()
{
    const QPointF p = worldPixel(m_locLng, m_locLat, m_zoom);
    m_locPxX = p.x();
    m_locPxY = p.y();
    m_viewPxX = m_locPxX;
    m_viewPxY = m_locPxY;
    m_userMoved = false;
    applyView(true);
}

/*
 * 拖动平移。地图是画在 m_iconLabel 上的，而 QLabel 会把鼠标事件自己吃掉
 * （它不调用基类实现，事件不会被传给父窗口），所以用事件过滤器在 label 这一层截住。
 * 缩放按钮是 label 的子控件，事件先给按钮，不受影响。
 */
bool Map::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj != &m_iconLabel)
        return QDialog::eventFilter(obj, ev);

    switch (ev->type()) {
    case QEvent::MouseButtonPress: {
        auto *me = static_cast<QMouseEvent *>(ev);
        if (me->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragLast = me->pos();
            return true;
        }
        break;
    }
    case QEvent::MouseMove: {
        auto *me = static_cast<QMouseEvent *>(ev);
        if (m_dragging) {
            const QPoint d = me->pos() - m_dragLast;
            m_dragLast = me->pos();
            /* 手指往右拖 = 地图往右走 = 视图中心往左移 */
            panByPixels(-d.x(), -d.y());
            return true;
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        auto *me = static_cast<QMouseEvent *>(ev);
        if (m_dragging && me->button() == Qt::LeftButton) {
            m_dragging = false;
            scheduleFetch();          /* 停手 250ms 后把缺的瓦片补齐 */
            return true;
        }
        break;
    }
    case QEvent::Wheel: {
        /* 鼠标滚轮也能缩放（桌面调试方便；触摸屏走 +/− 按钮） */
        auto *we = static_cast<QWheelEvent *>(ev);
        const int steps = we->angleDelta().y() / 120;
        if (steps != 0) {
            zoomBy(steps > 0 ? +1 : -1);
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QDialog::eventFilter(obj, ev);
}

void Map::mousePressEvent(QMouseEvent *e)
{
    /* label 上的事件被 eventFilter 截住了；这里是空白区域的兜底 */
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragLast = e->pos();
        e->accept();
        return;
    }
    QDialog::mousePressEvent(e);
}

void Map::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging) {
        const QPoint d = e->pos() - m_dragLast;
        m_dragLast = e->pos();
        panByPixels(-d.x(), -d.y());
        e->accept();
        return;
    }
    QDialog::mouseMoveEvent(e);
}

void Map::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_dragging && e->button() == Qt::LeftButton) {
        m_dragging = false;
        scheduleFetch();
        e->accept();
        return;
    }
    QDialog::mouseReleaseEvent(e);
}

void Map::wheelEvent(QWheelEvent *e)
{
    const int steps = e->angleDelta().y() / 120;
    if (steps != 0) {
        zoomBy(steps > 0 ? +1 : -1);
        e->accept();
        return;
    }
    QDialog::wheelEvent(e);
}

void Map::resizeEvent(QResizeEvent *e)
{
    QDialog::resizeEvent(e);
    if (!m_canvas.isNull())
        applyView(true);
}

void Map::onGpsTimeout()
{
    if (m_usingIp || m_gpsDeviceOk == false)
        return;
    qWarning() << "map: GPS 超时未定位，改用 IP 定位";
    locateByIp();
}

/*
 * GPS 定位抵达（来自全局 GpsService，WGS84 原始值）。
 * NMEA 是 WGS84，高德底图是 GCJ02，必须纠偏，否则会差几百米。
 */
void Map::onGpsFix(double lngWgs, double latWgs)
{
    if (m_usingIp)
        return;                       /* 已经退到 IP 定位，就别被 GPS 抢回去了 */
    m_gpsTimeout.stop();
    m_location = QString::number(lngWgs, 'f', 6) + ',' + QString::number(latWgs, 'f', 6);

    double gLng = 0.0, gLat = 0.0;
    wgs84ToGcj02(lngWgs, latWgs, &gLng, &gLat);

    /* GPS 是 1Hz 连续输出，别每来一帧就重画一次 */
    const bool moved = (std::fabs(gLng - m_locLng) > 0.00005
                        || std::fabs(gLat - m_locLat) > 0.00005);
    if (!m_lastFix.isValid() || m_lastFix.elapsed() > 5000 || moved) {
        m_lastFix.start();
        showMapAt(gLng, gLat, QString("GPS 定位"));
    }
}

void Map::map_working()
{
    setModal(true);
    show();
    /* show() 之后布局才算完，尺寸才有效；等一拍再拉地图 */
    QTimer::singleShot(30, this, &Map::initCtrl);
}

void Map::on_back_clicked()
{
    emit backout();
}

/*
 * 解析GPS数据（保留此函数是为了兼容旧调用/自检脚本）。
 * 真正的实现已经收敛到 GpsService::parseNmeaGga，那边支持 GPGGA/GNGGA/GBGGA/BDGGA
 * 并且会检查定位质量字段（旧实现只认 $GPGGA、且不查 quality，
 * 会把"未定位"的 0,0 当成有效坐标）。
 */
QString Map::parseGpsData(const QString &data)
{
    return GpsService::parseNmeaGga(data);
}

/* WGS84 -> GCJ02（国内底图纠偏）。坐标系外的点原样返回。 */
bool Map::wgs84ToGcj02(double lng, double lat, double *outLng, double *outLat)
{
    if (outOfChina(lng, lat)) {
        *outLng = lng;
        *outLat = lat;
        return false;
    }
    double dLat = transformLat(lng - 105.0, lat - 35.0);
    double dLon = transformLon(lng - 105.0, lat - 35.0);
    const double radLat = lat / 180.0 * kPi;
    double magic = std::sin(radLat);
    magic = 1 - kEccentricity * magic * magic;
    const double sqrtMagic = std::sqrt(magic);
    dLat = (dLat * 180.0) / ((kEarthAxis * (1 - kEccentricity)) / (magic * sqrtMagic) * kPi);
    dLon = (dLon * 180.0) / (kEarthAxis / sqrtMagic * std::cos(radLat) * kPi);
    *outLat = lat + dLat;
    *outLng = lng + dLon;
    return true;
}
