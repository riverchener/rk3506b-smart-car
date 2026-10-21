#include "weather.h"
#include "ui_weather.h"
#include "uiadapter.h"
#include "commonhelper.h"
#include "gpsservice.h"

#include <QMouseEvent>
#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QAbstractSocket>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPainter>
#include <QDate>

namespace {
/* 天气接口（城市名/编码二选一，都会显式传，避免服务端按自己的 IP 库乱猜） */
const char *kWeatherUrl = "http://v1.yiketianqi.com/api?unescape=1&version=v9"
                          "&appid=71755637&appsecret=cxCm8AxT";
/* IP 定位接口：和 app/map.cpp 里地图页用的是同一个，保证两页城市一致 */
const char *kIpLocateUrl = "http://ip-api.com/json/?lang=zh-CN"
                           "&fields=status,message,country,regionName,city,district,lat,lon,query";
/*
 * 逆地理编码（坐标 -> 城市名）。
 * 注意：本板 Qt **没有 TLS 插件**（/usr/lib/qt/plugins 下没有 tls/ 目录，
 * 所以 HTTPS 请求会直接失败），因此必须用 http。
 * BigDataCloud 这个免 key 接口实测在板上可用，localityLanguage=zh 直接返回中文：
 *   {"city":"广州市","locality":"越秀區","principalSubdivision":"广东省", ...}
 */
const char *kReverseGeocodeUrl =
    "http://api.bigdatacloud.net/data/reverse-geocode-client"
    "?localityLanguage=zh&latitude=%1&longitude=%2";
enum ReplyKind { kReplyOther = 0, kReplyIpLocate = 1, kReplyWeather = 2, kReplyReverseGeo = 3 };
}

Weather::Weather(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Weather)
{
    ui->setupUi(this);
    // 设置字体格式
    this->setFont(QFont("Airal",15));
    // 固定窗口 防止拖拉
    UiAdapter::fit(this); // 按屏幕分辨率自适应缩放
    setWindowFlag(Qt::FramelessWindowHint);
    // 新建菜单组件
    menuQuit = new QMenu(this);
    // 设置菜单项文字颜色
    menuQuit->setStyleSheet("QMenu::item {color:white}");
    // 返回键放大（原本 .ui 里只有 20x30，手指点不到；图标是 border-image，会跟着放大）
    CommonHelper::makeBackButtonBig(ui->back, 64, 64);
    // 为菜单组件添加一个动作
    QAction *closeAct = new QAction(QIcon(":/images/close.png"),tr("退出"),this);
    menuQuit->addAction(closeAct);

    // 用户选择菜单项并点击后回调函数，退出程序
    connect(menuQuit,&QMenu::triggered,this,[=]{
        this->close();
    });



    mWeekList << ui->labelday0 << ui->labelday1
              << ui->labelday2 << ui->labelday3
              << ui->labelday4 << ui->labelday5;
    mDateList << ui->labelDate0 << ui->labelDate1
              << ui->labelDate2 << ui->labelDate3
              << ui->labelDate4 << ui->labelDate5;
    mIconList << ui->labelWeaterIcon0 << ui->labelWeaterIcon1
              << ui->labelWeaterIcon2 << ui->labelWeaterIcon3
              << ui->labelWeaterIcon4 << ui->labelWeaterIcon5;
    mWeaTypeList << ui->lbweatherTypeDate0 << ui->lbweatherTypeDate1
                 << ui->lbweatherTypeDate2 << ui->lbweatherTypeDate3
                 << ui->lbweatherTypeDate4 << ui->lbweatherTypeDate5;
    mAirqList << ui->labelairq0 << ui->labelairq1 << ui->labelairq2
              << ui->labelairq3 << ui->labelairq4 << ui->labelairq5;
    mFxList << ui->labelFX0 << ui->labelFX1 << ui->labelFX2
            << ui->labelFX3 << ui->labelFX4 << ui->labelFX5;
    mFlList << ui->labelFL0 << ui->labelFL1 << ui->labelFL2
            << ui->labelFL3 << ui->labelFL4 << ui->labelFL5;


    //根据keys,设置icon的路径
    mTypeMap.insert("暴雪",":/images/type/BaoXue.png");
    mTypeMap.insert("暴雨",":/images/type/BaoYu.png");
    mTypeMap.insert("暴雨到大暴雨",":/images/type/BaoYuDaoDaBaoYu.png");
    mTypeMap.insert("大暴雨",":/images/type/DaBaoYu.png");
    mTypeMap.insert("大暴雨到特大暴雨",":/images/type/DaBaoYuDaoTeDaBaoYu.png");
    mTypeMap.insert("大到暴雪",":/images/type/DaDaoBaoXue.png");
    mTypeMap.insert("大雪",":/images/type/DaXue.png");
    mTypeMap.insert("大雨",":/images/type/DaYu.png");
    mTypeMap.insert("冻雨",":/images/type/DongYu.png");
    mTypeMap.insert("多云",":/images/type/DuoYun.png");
    mTypeMap.insert("浮沉",":/images/type/FuChen.png");
    mTypeMap.insert("雷阵雨",":/images/type/LeiZhenYu.png");
    mTypeMap.insert("雷阵雨伴有冰雹",":/images/type/LeiZhenYuBanYouBingBao.png");
    mTypeMap.insert("霾",":/images/type/Mai.png");
    mTypeMap.insert("强沙尘暴",":/images/type/QiangShaChenBao.png");
    mTypeMap.insert("晴",":/images/type/Qing.png");
    mTypeMap.insert("沙尘暴",":/images/type/ShaChenBao.png");
    mTypeMap.insert("特大暴雨",":/images/type/TeDaBaoYu.png");
    mTypeMap.insert("undefined",":/images/type/undefined.png");
    mTypeMap.insert("雾",":/images/type/Wu.png");
    mTypeMap.insert("小到中雪",":/images/type/XiaoDaoZhongXue.png");
    mTypeMap.insert("小到中雨",":/images/type/XiaoDaoZhongYu.png");
    mTypeMap.insert("小雪",":/images/type/XiaoXue.png");
    mTypeMap.insert("小雨",":/images/type/XiaoYu.png");
    mTypeMap.insert("雪",":/images/type/Xue.png");
    mTypeMap.insert("扬沙",":/images/type/YangSha.png");
    mTypeMap.insert("阴",":/images/type/Yin.png");
    mTypeMap.insert("雨",":/images/type/Yu.png");
    mTypeMap.insert("雨夹雪",":/images/type/YuJiaXue.png");
    mTypeMap.insert("阵雪",":/images/type/ZhenXue.png");
    mTypeMap.insert("阵雨",":/images/type/ZhenYu.png");
    mTypeMap.insert("中到大雪",":/images/type/ZhongDaoDaXue.png");
    mTypeMap.insert("中到大雨",":/images/type/ZhongDaoDaYu.png");
    mTypeMap.insert("中雪",":/images/type/ZhongXue.png");
    mTypeMap.insert("中雨",":/images/type/ZhongYu.png");
    // 安装事件过滤器
    ui->widget0404->installEventFilter(this);
    ui->widget0405->installEventFilter(this);



}
void Weather::weather_working()
{
    // 网络管理器只建一次（以前每次进天气页都 new + connect，会漏 manager 且重复处理回复）
    if (!manager) {
        manager = new QNetworkAccessManager(this);
        connect(manager, &QNetworkAccessManager::finished, this, &Weather::readHttpRply);
    }

    /*
     * 城市来源优先级（要求：天气跟着定位走）：
     *   1) GPS 定位 —— 有模块且已定到位，逆地理编码拿城市名
     *   2) 本机公网 IP 定位 —— 没 GPS 模块 / 室内收不到星时的兜底
     *   3) SMARTCAR_CITYID / SMARTCAR_CITY —— **最后**兜底（定位全失败时才用）
     *   4) 服务端按 IP 猜 —— 连 ip-api 都失败时的最后兜底
     *
     * 注意：CITYID/CITY 以前是**最高**优先级，而 smartcar.conf 里默认就写着
     * SMARTCAR_CITYID=101280101（广州），结果 GPS 和 IP 两条路都被它短路了，
     * 天气永远显示广州 —— 所以现在降级成"最后兜底"，不再是首选。
     */
    m_usedFallback = false;
    m_cityParam.clear();

    resolveCityByGps();
}

/* 有没有网：遍历网卡找一个非回环 IPv4（和地图页同一个判据） */
bool Weather::networkOnline()
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

/* 定位全失败时的最后兜底：用配置里钉死的城市 */
bool Weather::requestConfiguredCity()
{
    const QString cityId = qEnvironmentVariable("SMARTCAR_CITYID");
    if (!cityId.isEmpty()) {
        qWarning().noquote() << "weather: 定位不可用 -> 用配置兜底的 SMARTCAR_CITYID" << cityId;
        requestWeather("&cityid=" + cityId);
        return true;
    }
    const QString cityName = qEnvironmentVariable("SMARTCAR_CITY");
    if (!cityName.isEmpty()) {
        qWarning().noquote() << "weather: 定位不可用 -> 用配置兜底的 SMARTCAR_CITY" << cityName;
        requestWeather("&city=" + QString::fromUtf8(
                           QUrl::toPercentEncoding(normalizeCityName(cityName))));
        return true;
    }
    return false;
}

/*
 * 等 GPS 定位。已经定到位就立刻用，否则每 500ms 看一次，
 * 最多等 SMARTCAR_GPS_TIMEOUT_MS（默认 8 秒，和地图页一致），超时退 IP 定位。
 * 没插 GPS 模块时 GpsService 打不开串口，这里第一拍就会走 IP 分支，不会白等。
 */
void Weather::resolveCityByGps()
{
    GpsService &gps = GpsService::instance();
    const bool deviceOk = gps.start();

    if (deviceOk && gps.hasFix()) {
        qInfo().noquote() << QString("weather: 已有 GPS 定位 %1,%2 -> 逆地理编码")
                                 .arg(gps.lngWgs(), 0, 'f', 6).arg(gps.latWgs(), 0, 'f', 6);
        reverseGeocode(gps.lngWgs(), gps.latWgs());
        return;
    }

    if (!deviceOk) {
        qInfo().noquote() << "weather: 未检测到 GPS 模块 -> 按本机 IP 定位";
        resolveCityByIp();
        return;
    }

    if (!m_gpsWait) {
        m_gpsWait = new QTimer(this);
        m_gpsWait->setInterval(500);
        connect(m_gpsWait, &QTimer::timeout, this, [this]() {
            GpsService &g = GpsService::instance();
            if (g.hasFix()) {
                m_gpsWait->stop();
                qInfo().noquote() << QString("weather: GPS 已定位 %1,%2 -> 逆地理编码")
                                         .arg(g.lngWgs(), 0, 'f', 6).arg(g.latWgs(), 0, 'f', 6);
                reverseGeocode(g.lngWgs(), g.latWgs());
                return;
            }
            m_gpsElapsedMs += m_gpsWait->interval();
            if (m_gpsElapsedMs >= m_gpsWaitMs) {
                m_gpsWait->stop();
                qWarning().noquote() << "weather: 等待 GPS 定位超时 -> 退回 IP 定位";
                resolveCityByIp();
            }
        });
    }

    m_gpsElapsedMs = 0;
    m_gpsWaitMs = qEnvironmentVariableIntValue("SMARTCAR_GPS_TIMEOUT_MS");
    if (m_gpsWaitMs <= 0)
        m_gpsWaitMs = 8000;
    qInfo().noquote() << "weather: 有 GPS 模块，等待定位（最多" << m_gpsWaitMs << "ms）";
    m_gpsWait->start();
}

/* 坐标 -> 城市名（免 key 的 HTTP 逆地理编码），失败就退 IP 定位 */
void Weather::reverseGeocode(double lngWgs, double latWgs)
{
    const QString url = QString::fromLatin1(kReverseGeocodeUrl)
                            .arg(QString::number(latWgs, 'f', 6),
                                 QString::number(lngWgs, 'f', 6));
    qInfo().noquote() << "weather: 逆地理编码" << url;
    QNetworkReply *r = manager->get(QNetworkRequest(QUrl(url)));
    r->setProperty("kind", kReplyReverseGeo);
}

/* 问 ip-api 拿城市，再按城市名查天气（GPS 不可用时的兜底） */
void Weather::resolveCityByIp()
{
    QNetworkRequest req{ QUrl(QString::fromLatin1(kIpLocateUrl)) };
    QNetworkReply *r = manager->get(req);
    r->setProperty("kind", kReplyIpLocate);
    qInfo().noquote() << "weather: IP 定位" << kIpLocateUrl;
}

void Weather::requestWeather(const QString &cityParam)
{
    m_cityParam = cityParam;
    strUrl = QString::fromLatin1(kWeatherUrl) + cityParam;
    qInfo().noquote() << "weather: 请求" << strUrl;

    QNetworkReply *r = manager->get(QNetworkRequest(QUrl(strUrl)));
    r->setProperty("kind", kReplyWeather);
}

/*
 * yiketianqi 只认"广州"这种短名，传"广州市"会直接返回
 * {"errcode":100,"errmsg":"city不存在"}（实测），所以要去掉行政区后缀。
 */
QString Weather::normalizeCityName(const QString &city)
{
    QString c = city.trimmed();
    static const char *kSuffixes[] = { "特别行政区", "维吾尔自治区", "回族自治区",
                                       "壮族自治区", "自治区", "傣族自治州", "自治州",
                                       "地区", "盟", "市", "县", "区" };
    for (const char *s : kSuffixes) {
        const QString suf = QString::fromUtf8(s);
        if (c.endsWith(suf) && c.size() > suf.size()) {
            c.chop(suf.size());
            break;
        }
    }
    return c;
}

// 读取数据
void Weather::readHttpRply(QNetworkReply *reply)
{
    if (!reply)
        return;
    const int kind = reply->property("kind").toInt();
    const int resCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    const bool netOk = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();

    /* ---- 第 0 步：GPS 坐标 -> 城市名（逆地理编码）---- */
    if (kind == kReplyReverseGeo) {
        const QJsonObject o = QJsonDocument::fromJson(data).object();
        /* BigDataCloud 的字段：city=地级市（"广州市"）, locality=区县（"越秀區"）,
         * principalSubdivision=省（"广东省"）。取 city，取不到再用 locality。 */
        QString city = o.value("city").toString();
        if (city.isEmpty())
            city = o.value("locality").toString();
        const QString province = o.value("principalSubdivision").toString();

        if (netOk && !city.isEmpty()) {
            const QString shortName = normalizeCityName(city);
            qInfo().noquote() << QString("weather: GPS 逆地理编码 -> %1 %2（城市名 '%3'）")
                                     .arg(province, city, shortName);
            requestWeather("&city=" + QString::fromUtf8(QUrl::toPercentEncoding(shortName)));
        } else {
            qWarning().noquote() << "weather: GPS 逆地理编码失败，退回 IP 定位"
                                 << (netOk ? QString::fromUtf8(data).left(120)
                                           : reply->errorString());
            if (networkOnline())
                resolveCityByIp();
            else
                requestConfiguredCity();
        }
        return;
    }

    /* ---- 第一步：IP 定位（和地图页同一个接口，保证两页城市一致）---- */
    if (kind == kReplyIpLocate) {
        const QJsonObject o = QJsonDocument::fromJson(data).object();
        const QString city = o.value("city").toString();
        const QString region = o.value("regionName").toString();
        const QString ip = o.value("query").toString();
        if (netOk && o.value("status").toString() == "success" && !city.isEmpty()) {
            m_cityFromIp = normalizeCityName(city);
            qInfo().noquote() << QString("weather: IP 定位 %1 %2（IP %3）-> 城市名 '%4'")
                                 .arg(region, city, ip, m_cityFromIp);
            requestWeather("&city=" + QString::fromUtf8(QUrl::toPercentEncoding(m_cityFromIp)));
        } else {
            qWarning().noquote() << "weather: IP 定位失败"
                                 << (netOk ? QString::fromUtf8(data).left(120) : reply->errorString());
            if (networkOnline()) {
                /* 有网但 ip-api 挂了：让服务端按 IP 自己猜，通常也能猜对 */
                qWarning() << "weather: 退回让服务端按 IP 猜";
                requestWeather(QString());
            } else {
                requestConfiguredCity();
            }
        }
        return;
    }

    /* ---- 第二步：天气数据 ---- */
    if (netOk && resCode == 200 && !data.isEmpty()) {
        const QJsonObject root = QJsonDocument::fromJson(data).object();
        if (root.contains("errcode")) {
            qWarning().noquote() << "weather: 接口报错" << QString::fromUtf8(data).left(200);
            if (!m_usedFallback && !m_cityParam.isEmpty()) {
                m_usedFallback = true;
                qWarning() << "weather: 城市名不被接受，退回让服务端按 IP 猜";
                requestWeather(QString());
                return;
            }
            QMessageBox mes;
            mes.setWindowTitle("错误");
            mes.setText(QString("天气接口返回错误：%1").arg(root.value("errmsg").toString()));
            mes.setStandardButtons(QMessageBox::Ok);
            mes.exec();
            return;
        }
        parseWeatherJsonDataNew(data);
        qDebug() << QString::fromUtf8(data);
        return;
    }

    qWarning() << "weather: 请求失败" << resCode << reply->errorString();
    QMessageBox mes;
    mes.setWindowTitle("错误");
    mes.setText("网络请求失败");
    mes.setStyleSheet("QPushButton {color:red}");
    mes.setStandardButtons(QMessageBox::Ok);
    mes.exec();
}
Weather::~Weather()
{
    delete ui;
}



// 重写鼠标点击
void Weather::mousePressEvent(QMouseEvent *event)
{
    // 捕获右键被按下
    if(event->button() == Qt::RightButton){
        //qDebug() << "right Mouse clicked!";
        menuQuit->exec(QCursor::pos());
    }
    // 捕获左键被按下

//    if(event->button() == Qt::LeftButton){
//        // qDebug() << "left Mouse clicked!";
//        // 鼠标当前位置
//        // qDebug() << event->globalPos();
//        // 窗口当前位置
//        // qDebug() << this->pos();
//        // 窗口新位置 event->globalPos() - this->pos()
//        mOffset = event->globalPos() - this->pos();
//    }
}

// 鼠标左键按下后的移动，导致事件被调用，设置窗口的新位置
//void Widget::mouseMoveEvent(QMouseEvent *event)
//{
//    this->move(event->globalPos() - mOffset);
//}

// 绘制温度曲线变化
bool Weather::eventFilter(QObject *watched, QEvent *event)
{
    if(watched == ui->widget0404 && event->type() == QEvent::Paint){
        drawTempLineHigh();
        return true;
    }
    if(watched == ui->widget0405 && event->type() == QEvent::Paint){
        drawTempLineLow();
        return true;
    }
    return QWidget::eventFilter(watched,event);
}
// 解析json数据 单天数据
void Weather::parseWeatherJsonData(QByteArray rawData)
{
    QJsonDocument jsonObj = QJsonDocument::fromJson(rawData);
    if(!jsonObj.isNull() && jsonObj.isObject()){
        QJsonObject objRoot = jsonObj.object();
        // 解析当前日期
        QString date = objRoot["date"].toString();
        QString week = objRoot["week"].toString();
        ui->labelCurrentDate->setText(date + " " + week);
        // 解析城市名词
        QString cityName = objRoot["city"].toString();
        ui->labelCity->setText(cityName + "市");
        // 解析当前温度
        QString currentTemp = objRoot["tem"].toString();
        ui->labelTmp->setText(currentTemp + "℃");
        ui->labelTempRange->setText(objRoot["tem2"].toString() + "~" + objRoot["tem1"].toString());
        // 解析天气类型
        ui->labelweatherType->setText(objRoot["wea"].toString());
        ui->labelWeatherIcon->setPixmap(mTypeMap[objRoot["wea"].toString()]);
        // 解析感冒指数
        ui->labelGanmao->setText(objRoot["air_tips"].toString());
        // 解析风向
        ui->labelFXType->setText(objRoot["win"].toString());
        // 解析风力
        ui->labelFXType_2->setText(objRoot["win_speed"].toString());
        // 解析PM2.5
        ui->labelPM25Data->setText(objRoot["air_pm25"].toString());
        // 解析湿度
        ui->labelShiduData->setText(objRoot["humidity"].toString());
        // 解析空气质量
        ui->labelairData->setText(objRoot["air_level"].toString());
    }
}

// 解析json数据 七天数据
void Weather::parseWeatherJsonDataNew(QByteArray rawData)
{
    // 将JSON作为UTF-8编码的JSON文档进行解析，并基于它创建一个 QJsonDocument
    QJsonDocument jsonDoc = QJsonDocument::fromJson(rawData);
    // 解析对象
    if(!jsonDoc.isNull() && jsonDoc.isObject()){
        QJsonObject jsonRoot = jsonDoc.object();
        days[0].mCity = jsonRoot["city"].toString();
        days[0].mPm25 = jsonRoot["aqi"].toObject()["pm25"].toString();
        // 解析数组
        if(jsonRoot.contains("data") && jsonRoot["data"].isArray()){
            // 转换类型为QT表示JSON数组的数据类型
            QJsonArray weaArray = jsonRoot["data"].toArray();
            for(int i = 0; i < weaArray.size(); i++){
                // 转换类型为QT表示JSON对象的数据类型
                QJsonObject obj = weaArray[i].toObject();
                days[i].mDate = obj["date"].toString();
                days[i].mWeek = obj["week"].toString();
                days[i].mWeathType = obj["wea"].toString();
                days[i].mTemp = obj["tem"].toString();
                days[i].mTempLow = obj["tem2"].toString();
                days[i].mTempHigh = obj["tem1"].toString();
                days[i].mFx = obj["win"].toArray()[0].toString();
                days[i].mFl = obj["win_speed"].toString();
                days[i].mAirq = obj["air_level"].toString();
                days[i].mTips = obj["air_tips"].toString();
                days[i].mHu = obj["humidity"].toString();

            }
        }
    }
    // 更新界面
    updateUI();
}

void Weather::updateUI()
{
    QPixmap pixmap;

    /*
     * 先找出"今天"在 days[] 里的下标。
     *
     * 踩过的坑（用户现象："怎么还是昨天"）：接口在凌晨返回的第一条**还是昨天**。
     * 实测 update_time = 2026-09-24 00:20，而 data[0].date = 2026-09-23。
     * 原来代码无条件用 days[0] 当"今天"：
     *   · 顶部大温度/城市/空气质量显示的都是**昨天**的天气；
     *   · 下面六天列还把 data[0] 硬标成"今天"，于是页面写着
     *     "今天 09-23"，而设备当天其实是 09-24 —— 看起来就永远差一天。
     * 现在按日期差来找，找不到才退回 0。
     */
    const QDate today = QDate::currentDate();
    int todayIdx = 0;
    for (int i = 0; i < 6; ++i) {
        const QDate d = QDate::fromString(days[i].mDate, QStringLiteral("yyyy-MM-dd"));
        if (d.isValid() && d == today) {
            todayIdx = i;
            break;
        }
    }
    qInfo().noquote() << QString("weather: 今天=%1，接口第一条=%2 -> 用 days[%3] 作为\"今天\"")
                             .arg(today.toString("yyyy-MM-dd"), days[0].mDate)
                             .arg(todayIdx);

    // 解析当前日期
    ui->labelCurrentDate->setText(days[todayIdx].mDate + " " + days[todayIdx].mWeek);
    // 解析城市名词
    ui->labelCity->setText(days[todayIdx].mCity + "市");
    // 解析当前温度
    ui->labelTmp->setText(days[todayIdx].mTemp + "℃");
    ui->labelTempRange->setText(
                days[todayIdx].mTempLow + "℃" + "~"  + days[todayIdx].mTempHigh +  "℃");
    // 解析天气类型
    ui->labelweatherType->setText(days[todayIdx].mWeathType);
    ui->labelWeatherIcon->setPixmap(mTypeMap[days[todayIdx].mWeathType]);
    // 解析感冒指数
    ui->labelGanmao->setText(days[todayIdx].mTips);
    // 解析风向
    ui->labelFXType->setText(days[todayIdx].mFx);
    // 解析风力
    ui->labelFXType_2->setText(days[todayIdx].mFl);
    // 解析PM2.5
    ui->labelPM25Data->setText(days[todayIdx].mPm25);
    // 解析湿度
    ui->labelShiduData->setText(days[todayIdx].mHu);
    // 解析空气质量
    ui->labelairData->setText(days[todayIdx].mAirq);
    ui->lbweatherTypeDate3->setText(days[3].mWeathType);
    ui->lbweatherTypeDate0->setText(days[0].mWeathType);
    for(int i = 0; i < 6; i++){

        /*
         * "今天/明天/后天"按**日期差**算，不要按下标写死。
         * diff = 该列日期 - 设备当天日期。
         */
        const QDate d = QDate::fromString(days[i].mDate, QStringLiteral("yyyy-MM-dd"));
        QString dayLabel = days[i].mWeek;
        if (d.isValid()) {
            switch (today.daysTo(d)) {
            case  0: dayLabel = QStringLiteral("今天"); break;
            case  1: dayLabel = QStringLiteral("明天"); break;
            case  2: dayLabel = QStringLiteral("后天"); break;
            case -1: dayLabel = QStringLiteral("昨天"); break;
            case -2: dayLabel = QStringLiteral("前天"); break;
            default: break;      /* 更远的用接口给的星期几 */
            }
        }
        mWeekList[i]->setText(dayLabel);
        QStringList dayList = days[i].mDate.split("-");
        mDateList[i]->setText(dayList.at(1) + "-" + dayList.at(2));
        // 缩放图片大小和label大小能匹配
        int index = days[i].mWeathType.indexOf("转");
        if(index != -1){
            pixmap = mTypeMap[days[i].mWeathType.left(index)];
        }else{
            pixmap = mTypeMap[days[i].mWeathType];
        }
        pixmap = pixmap.scaled(mIconList[i]->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation);
        mIconList[i]->setMaximumHeight(50);
        mIconList[i]->setMaximumWidth(ui->widget02->width()/6.5);
        mIconList[i]->setPixmap(pixmap);
        mWeaTypeList[i]->setText(days[i].mWeathType);
        QString airQ = days[i].mAirq;
        mAirqList[i]->setText(airQ);
        if(airQ == "优"){
            mAirqList[i]->setStyleSheet("background-color:rgb(150, 213, 32);border-radius: 7px;color: rgb(230, 230, 230)");
        }
        if(airQ == "良"){
            mAirqList[i]->setStyleSheet("background-color:rgb(255, 170, 127);border-radius: 7px;color: rgb(230, 230, 230)");
        }
        if(airQ == "轻度污染"){
            mAirqList[i]->setStyleSheet("background-color:rgb(170, 200, 129);border-radius: 7px;color: rgb(230, 230, 230)");
        }
        if(airQ == "中度污染"){
            mAirqList[i]->setStyleSheet("background-color:rgb(255, 17, 17);border-radius: 7px;color: rgb(230, 230, 230)");
        }
        if(airQ == "重度污染"){
            mAirqList[i]->setStyleSheet("background-color:rgb(153, 0, 0);border-radius: 7px;color: rgb(230, 230, 230)");
        }

        mFxList[i]->setText(days[i].mFx);
        index = days[i].mFl.indexOf("转");
        if(index != -1){
            mFlList[i]->setText(days[i].mFl.left(index));
        }else{
            mFlList[i]->setText(days[i].mFl);
        }


    }
   update();
}

// 绘制最高温度曲线
void Weather::drawTempLineHigh()
{
    QPainter painter(ui->widget0404);
    painter.setRenderHint(QPainter::Antialiasing,true);
    painter.setBrush(Qt::yellow);
    painter.setPen(Qt::yellow);

    /*
     * 曲线上的温度文字要单独指定字号，并且把基线夹在色带内。
     *
     * 为什么：这个控件只有 38px 高（geometry.txt 实测 widget0404 = 1211x38），
     * 而它的字号是 18pt（行高 35px，几乎占满整条带子）。
     * 原来文字画在 `points[i].y() - 15`，而 points[i].y() 大约在 20~34，
     * 于是基线只有 5~19 —— 字形主体在基线**上方**，直接被画到控件顶边以外，
     * 只剩一条小横杠（实测就是这样：温度数字看不见，只有短横线）。
     * 现在：字号压到 10pt，并把基线限制在 [11, height-2] 之间，保证整行都在带内。
     */
    const int bandH = ui->widget0404->height();
    QFont tf = painter.font();
    tf.setPointSize(10);
    painter.setFont(tf);

    int ave;
    int sum = 0;
    int offSet = 0;
    int middle = ui->widget0404->height()*0.7;
    for (int i = 0; i < 6; i++) {
        sum += days[i].mTempHigh.toInt();
    }
    ave = sum / 6;

    // 定义6个点
    QPoint points[6];
    for (int i = 0; i < 6; i++) {
          // 以mAirqList为参照算出 x 位置
          points[i].setX(mAirqList[i]->x() + mAirqList[i]->width()/2);
          // 算出偏移量 （最高温度 - 平均温度） * 2
          offSet = (days[i].mTempHigh.toInt() - ave)*2;
          // 算出 Y 位置 中值 - 偏移量
          points[i].setY(middle - offSet);
          // 画出6个点
          painter.drawEllipse(QPoint(points[i]),3,3);
          // 画出当天温度（基线夹在色带内，避免被上边缘裁掉）
          const int ty = qBound(11, points[i].y() - 15, qMax(11, bandH - 2));
          painter.drawText(points[i].x() - 15, ty, days[i].mTempHigh + "°");
    }
    for (int i = 0; i < 5; i++) {
        // 点与点之间连线
        painter.drawLine(points[i],points[i+1]);
    }

}
// 绘制最低温度曲线
void Weather::drawTempLineLow()
{
    QPainter painter(ui->widget0405);
    // 抗锯齿
    painter.setRenderHint(QPainter::Antialiasing,true);
    // 设置画刷
    painter.setBrush(QColor(70,192,203));
    // 设置画笔
    painter.setPen(QColor(70,192,203));

    /* 同 drawTempLineHigh：色带只有 38px 高，而控件字号 18pt，
     * 必须单独给小字号并把基线夹进带内，否则温度数字会被上边缘裁成小横杠。 */
    QFont tf = painter.font();
    tf.setPointSize(10);
    painter.setFont(tf);

    int ave;
    int sum = 0;
    int offSet = 0;
    int middle = ui->widget0405->height()*0.7;
    for (int i = 0; i < 6; i++) {
        sum += days[i].mTempLow.toInt();
    }
    ave = sum / 6;

    // 定义6个点
    QPoint points[6];
    for (int i = 0; i < 6; i++) {
          points[i].setX(mAirqList[i]->x() + mAirqList[i]->width()/2);
          offSet = (days[i].mTempLow.toInt() - ave)*2;
          points[i].setY(middle - offSet);
          // 画出6个点
          painter.drawEllipse(QPoint(points[i]),3,3);
          // 画出当天温度（同上：字号压小 + 基线夹在色带内）
          const int ty = qBound(11, points[i].y() - 15,
                                qMax(11, ui->widget0405->height() - 2));
          painter.drawText(points[i].x() - 15, ty, days[i].mTempLow + "°");
    }
    for (int i = 0; i < 5; i++) {
        // 画出两点之间的线
        painter.drawLine(points[i],points[i+1]);
    }
}


// 读取数据
void Weather::on_back_clicked()
{
    emit this->back();
}
