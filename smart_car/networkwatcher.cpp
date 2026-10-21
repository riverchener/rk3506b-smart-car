#include "networkwatcher.h"
#include "networktoast.h"

#include <QAbstractSocket>
#include <QDebug>
#include <QFile>
#include <QHostAddress>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QTextStream>

QString NetworkWatcher::statusFile()
{
    return qEnvironmentVariable("SMARTCAR_NET_STATUS", "/var/run/smartcar-net.status");
}

/*
 * 【2026-10-18 ①】现查当前网卡 IPv4（详见 networkwatcher.h 的说明）。
 *
 * 为什么不能继续用状态文件：写文件的两个脚本（board/S47wifi:207-213、
 * board/wifi-ctl:146-148）都只在"连接成功那一刻"写一次，没有定时刷新；
 * 而本板真正管 WiFi 的是 connman 那一套（S47wifi_smart_community），
 * 它根本不写这个文件 ⇒ 屏上的 IP 只可能是旧值/残留值。
 *
 * 实测基线（2026-10-18）：wlan0 = 10.69.97.76/24，eth0/eth1 NO-CARRIER DOWN，
 * 没有 usb0，也没有 docker/veth ⇒ 规则取 wlan* 优先足够。
 */
QString NetworkWatcher::currentIPv4()
{
    QString wlanIp, ethIp;

    const QList<QNetworkInterface> ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &ni : ifaces) {
        const QNetworkInterface::InterfaceFlags f = ni.flags();
        if (!(f & QNetworkInterface::IsUp) || !(f & QNetworkInterface::IsRunning))
            continue;
        if (f & QNetworkInterface::IsLoopBack)
            continue;

        const QString name = ni.name();
        const bool isWlan = name.startsWith(QLatin1String("wlan"));
        const bool isEth = name.startsWith(QLatin1String("eth"));
        // 只认无线/以太网；docker/veth/br-/virbr/usb0 之类一律不参与
        if (!isWlan && !isEth)
            continue;

        for (const QNetworkAddressEntry &ae : ni.addressEntries()) {
            if (ae.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            const QString cand = ae.ip().toString();
            if (cand.startsWith(QLatin1String("169.254.")))
                continue;                       // link-local：没拿到 DHCP，不算可用
            if (isWlan) {
                if (wlanIp.isEmpty())
                    wlanIp = cand;              // wlan 优先
            } else if (ethIp.isEmpty()) {
                ethIp = cand;                   // 其次 eth
            }
        }
    }

    if (!wlanIp.isEmpty())
        return wlanIp;
    return ethIp;
}

NetworkWatcher::NetworkWatcher(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(2000); // 每 2 秒看一次
    connect(m_timer, &QTimer::timeout, this, &NetworkWatcher::check);
    m_timer->start();
}

void NetworkWatcher::check()
{
    const QString path = statusFile();
    QFile f(path);
    if (!f.exists())
        return;
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    QStringList lines;
    {
        QTextStream ts(&f);
        ts.setCodec("UTF-8");          // 中文 SSID 要按 UTF-8 读
        while (!ts.atEnd())
            lines << ts.readLine();
    }
    f.close();
    // 先删掉，避免重复弹
    QFile::remove(path);

    // S47wifi 写的状态文件：
    //   连接成功 -> 三行：OK / SSID / IP
    //   连接断开 -> 两行：DOWN / 原因
    if (!lines.isEmpty() && lines.at(0).trimmed() == "OK" && lines.size() >= 3) {
        const QString ssid = lines.at(1);
        /*
         * 【2026-10-18 ①】第 3 行不再原样回显。
         *
         * 它是"写文件那一刻"的快照：DHCP 续租 / 换热点之后就是旧值；而且对内容
         * 零校验（诊断 §2.4 做过受控实验：注入 192.168.88.130，屏上就照搬
         * 192.168.88.130，与 wlan0 的 10.69.97.76 不符）。
         * 现在改成**现查网卡**，状态文件里的值只在现查拿不到时兜底。
         */
        const QString fileIp = lines.at(2);
        const QString liveIp = currentIPv4();
        const QString ip = liveIp.isEmpty() ? fileIp : liveIp;
        qInfo().noquote() << "network: 连接成功" << ssid << ip
                          << (liveIp.isEmpty() ? "(来自状态文件)" : "(网卡实时值)");
        emit networkConnected(ssid, ip);
        showConnectedPopup(ssid, ip);
        return;
    }

    if (!lines.isEmpty() && lines.at(0).trimmed() == "DOWN") {
        const QString reason = lines.size() >= 2 ? lines.at(1) : QString();
        qInfo().noquote() << "network: 已断开" << reason;
        emit networkDisconnected(reason);
        showDisconnectedPopup(reason);
        return;
    }

    // 兼容旧的单行格式：OK:SSID:IP
    const QString line = lines.join(' ').trimmed();
    const QStringList parts = line.split(':');
    if (parts.size() >= 3 && parts.at(0) == "OK") {
        const QString ssid = parts.at(1);
        const QString liveIp = currentIPv4();       // 同上：现查优先，文件值兜底
        const QString ip = liveIp.isEmpty() ? parts.mid(2).join(':') : liveIp;
        qInfo().noquote() << "network: 连接成功" << ssid << ip;
        emit networkConnected(ssid, ip);
        showConnectedPopup(ssid, ip);
        return;
    }

    qInfo().noquote() << "network: 状态文件 =" << line << "（不提示）";
}

void NetworkWatcher::showDisconnectedPopup(const QString &reason)
{
    NetworkToast::showDisconnected(qobject_cast<QWidget *>(parent()), reason);
}

void NetworkWatcher::showConnectedPopup(const QString &ssid, const QString &ip)
{
    // 交给 NetworkToast：非阻塞浮层，6 秒自动关，也可点“确定”
    NetworkToast::showToast(qobject_cast<QWidget *>(parent()), ssid, ip);
}
