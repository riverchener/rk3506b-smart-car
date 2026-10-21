#ifndef WEATHER_H
#define WEATHER_H

#include <QLabel>
#include <QMenu>
#include <QNetworkReply>
#include <QTimer>
#include <QWidget>
#include "citycodeutils.h"
#include "day.h"
QT_BEGIN_NAMESPACE
namespace Ui { class Weather; }
QT_END_NAMESPACE

class Weather : public QWidget
{
    Q_OBJECT

public:
    Day days[7];
    QList<QLabel *> mDateList;
    QList<QLabel *> mWeekList;
    QList<QLabel *> mIconList;
    QList<QLabel *> mWeaTypeList;
    QList<QLabel *> mAirqList;
    QList<QLabel *> mFxList;
    QList<QLabel *> mFlList;
    Weather(QWidget *parent = nullptr);
    ~Weather();
    void weather_working();
protected:
    void mousePressEvent(QMouseEvent *event);
    //void mouseMoveEvent(QMouseEvent *event);
    bool eventFilter(QObject *watched,QEvent *event);

public slots:
    void readHttpRply(QNetworkReply *reply);
private slots:
    //void on_lineEditCity_returnPressed();

    void on_back_clicked();

signals:
    void back();
private:
    Ui::Weather *ui;
    QMenu *menuQuit;
    QPoint mOffset; // 偏移值
    QNetworkReply *reply;
    void parseWeatherJsonData(QByteArray rawData);
    QString strUrl;
    CityCodeUtils cityCodeUtils;
    QMap<QString,QString> mTypeMap;
    QNetworkAccessManager *manager = nullptr;
    void parseWeatherJsonDataNew(QByteArray rawData);
    void updateUI();
    void drawTempLineHigh();
    void drawTempLineLow();

    /*
     * 城市来源优先级（天气要跟着定位走）：
     *   GPS 逆地理编码 > 本机 IP 定位 > 配置兜底(CITYID/CITY) > 服务端按 IP 猜
     */
    void resolveCityByGps();                     /* 等 GPS 定位，拿到就逆地理编码 */
    void resolveCityByIp();
    bool requestConfiguredCity();                /* 定位全失败时才用的最后兜底 */
    static bool networkOnline();                /* 有没有网（非回环 IPv4） */
    void reverseGeocode(double lngWgs, double latWgs);   /* 坐标 -> 城市名 */
    void requestWeather(const QString &cityParam);
    static QString normalizeCityName(const QString &city);
    QString m_cityParam;            /* 本次用的 &city= / &cityid= 参数 */
    QString m_cityFromIp;           /* ip-api 归一化后的城市名 */
    bool m_usedFallback = false;    /* 已经退回"服务端按 IP 猜" */

    /* GPS：等超时还拿不到定位就退到 IP 定位（没插模块/室内没星都走这条） */
    QTimer *m_gpsWait = nullptr;
    qint64 m_gpsElapsedMs = 0;
    int m_gpsWaitMs = 0;
};
#endif // WIDGET_H
