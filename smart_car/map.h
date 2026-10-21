#ifndef MAPWIDGET_H
#define MAPWIDGET_H

#include <QDialog>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QMovie>
#include <QSerialPort>
#include <QByteArray>
#include <QPushButton>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QPointF>
#include <QTimer>

/*
 * GPS / 定位页面（支持放大、缩小、拖动平移）
 *
 * 有 GPS 模块  -> 读串口 NMEA（$GPGGA），WGS84 -> GCJ02，按坐标画地图
 * 没有 GPS 模块（或迟迟不定位）-> 用公网 IP 定位（和天气页"让服务端按请求 IP 猜"
 * 一个思路），拿到经纬度后同样画地图；界面上会写清楚当前是哪种来源。
 *
 * 底图：高德公开瓦片（无需 key，国内可直连），256px 瓦片拼接 + 定位点打点。
 *      瓦片按 z/x/y 缓存，平移到去过的地方不会重复下载。
 * 交互：手指拖动平移、+/− 按钮缩放、回到定位；平移范围限制在定位点周围
 *      SMARTCAR_MAP_PAN_MAX_DEG 度（默认 0.45° ≈ 50km），缩放限制在
 *      SMARTCAR_MAP_ZOOM_MIN..MAX（默认 12..18）。
 *
 * 不能用百度静态图：原 AK 已被禁用（api.map.baidu.com 返回
 * {"message":"APP被用户自己禁用","status":"201"}），
 * 而 openstreetmap 系在这里 DNS 都解析不出来（校园网实测 2026-09-22）。
 */
class Map : public QDialog
{
    Q_OBJECT

    // 高德公开瓦片：%1=主机号(1..4) %2=x %3=y %4=z。style=8 是带路网+标注的矢量底图。
    const QString m_tileUrl = "http://webrd0%1.is.autonavi.com/appmaptile"
                              "?lang=zh_cn&size=1&scale=1&style=8&x=%2&y=%3&z=%4";
    // 公网 IP 定位（免 key）。返回 JSON：status/city/regionName/lat/lon/query
    const QString m_ipUrl = "http://ip-api.com/json/?lang=zh-CN"
                            "&fields=status,message,country,regionName,city,district,lat,lon,query";

public:
    explicit Map(QWidget *parent = nullptr);
    ~Map();

    // 实际画地图用的画布尺寸（测试脚本用它判断什么时候有图）
    QSize canvasSize() const { return m_canvas.size(); }
    int zoomLevel() const { return m_zoom; }
    // 视图中心与定位点（GCJ02），日志/自检用
    QPointF viewCenterLngLat() const
    {
        double l = 0.0, t = 0.0;
        worldPixelToLngLat(m_viewPxX, m_viewPxY, m_zoom, &l, &t);
        return QPointF(l, t);
    }
    QPointF locatedLngLat() const { return QPointF(m_locLng, m_locLat); }

private:
    void initUi();
    void initCtrl();
    void initSerialPort();
    void locateByIp();
    void showMapAt(double lngGcj, double latGcj, const QString &source);
    void requestNetwork(const QString &url, int kind, int tx, int ty, int z);
    void applyView(bool fetchMissing);          // 按当前中心/缩放重画；可选择补拉缺失瓦片
    void scheduleFetch();                       // 拖动结束后延迟补瓦片，避免边拖边刷请求
    void panByPixels(double dx, double dy);
    void zoomBy(int delta);
    void clampView();
    void drawMarker(QPainter *p, const QPoint &origin);
    void updateInfoLabel();
    QString parseGpsData(const QString &data);
    static bool wgs84ToGcj02(double lng, double lat, double *outLng, double *outLat);
    static bool networkOnline();
    static QPointF worldPixel(double lng, double lat, int z);
    static void worldPixelToLngLat(double px, double py, int z, double *lng, double *lat);

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private slots:
    void netReplyFinished();
    void onGpsFix(double lngWgs, double latWgs);
    void on_back_clicked();
    void onGpsTimeout();
    void onFetchTimeout();

public slots:
    void map_working();
    void zoomIn();
    void zoomOut();
    void recenter();                            // 回到定位点（并重新跟随 GPS）

signals:
    void backout();

private:
    enum NetKind { NetNone = 0, NetIp = 1, NetTile = 2 };

    QLabel m_iconLabel;
    QLabel m_logoLabel;
    QPushButton* m_back;
    QPushButton* m_zoomInBtn;
    QPushButton* m_zoomOutBtn;
    QPushButton* m_recenterBtn;
    QNetworkAccessManager m_networkAccessManager;

    /* 没有 GPS 时的兜底坐标（"经度,纬度"，WGS84），可用 SMARTCAR_MAP_CENTER 覆盖 */
    QString m_location = qEnvironmentVariable("SMARTCAR_MAP_CENTER", "113.264434,23.129162");
    QString m_source;                 /* 当前坐标来源：GPS / IP / 默认 */
    int m_zoom = qEnvironmentVariableIntValue("SMARTCAR_MAP_ZOOM");
    int m_zoomMin = qEnvironmentVariableIntValue("SMARTCAR_MAP_ZOOM_MIN");
    int m_zoomMax = qEnvironmentVariableIntValue("SMARTCAR_MAP_ZOOM_MAX");
    double m_panMaxDeg = 0.0;         /* 平移范围：离定位点最远多少度 */
    bool m_inited = false;
    bool m_userMoved = false;         /* 用户拖过/缩过：GPS 更新时不再自动跟随 */

    bool m_gpsDeviceOk = false;
    bool m_usingIp = false;
    QTimer m_gpsTimeout;
    QElapsedTimer m_lastFix;

    /* 视图：以"世界像素坐标"为准，平移=加减像素，缩放=像素乘 2 的幂 */
    QImage m_canvas;
    double m_viewPxX = 0.0;           /* 画布中心 */
    double m_viewPxY = 0.0;
    double m_locPxX = 0.0;            /* 定位点在当前 zoom 下的世界像素 */
    double m_locPxY = 0.0;
    double m_locLng = 0.0;            /* 定位点（GCJ02） */
    double m_locLat = 0.0;
    int m_pendingTiles = 0;           /* 本轮还没回来的瓦片数 */
    int m_failedTiles = 0;
    QHash<QString, QImage> m_tileCache;   /* "z/x/y" -> 瓦片 */

    /* 拖动状态 */
    bool m_dragging = false;
    QPoint m_dragLast;
    QTimer m_fetchTimer;              /* 拖动/缩放后的补图延时 */
};

#endif // MAPWIDGET_H
