#ifndef NETWORKWATCHER_H
#define NETWORKWATCHER_H

/*
 * NetworkWatcher —— 监听 /etc/init.d/S47wifi 写的连接状态文件，
 * 一旦检测到“网络连接成功”，就在应用最上层弹一个提示框。
 *
 * 状态文件由 S47wifi 写入：
 *   /var/run/smartcar-net.status
 *     连接成功 -> 三行：OK / SSID / IP
 *     连接断开 -> 两行：DOWN / 原因
 *     连接失败 -> 两行：FAIL / 原因（应用不弹窗）
 *
 * 应用读取后会删掉该文件，保证每次事件只弹一次。
 */

#include <QObject>
#include <QString>
#include <QTimer>

class NetworkWatcher : public QObject
{
    Q_OBJECT
public:
    explicit NetworkWatcher(QObject *parent = nullptr);

    // 状态文件路径（可用环境变量覆盖，方便测试）
    static QString statusFile();

    /*
     * 【2026-10-18 ①「IP 实时显示」】现查当前网卡的 IPv4；没有可用地址返回空串。
     *
     * 为什么要它：状态文件里的 IP 是"连上那一刻"的一次性快照（写文件的一方
     * 只在连接成功时写一次，见 board/S47wifi:207-213、board/wifi-ctl:146-148），
     * DHCP 续租 / 换热点之后屏上就还是旧值。诊断与实测见
     * build_out/docs/IP显示与视频页_只读诊断_20261018.md §2。
     *
     * 规则：只认 wlan* / eth*（wlan* 优先），排除 lo/docker/veth/br-/virbr/usb，
     * 跳过 169.254.*（没拿到 DHCP 时的 link-local 自配地址）。
     * 主窗口左上角状态文字与"连上网络"弹窗**共用这一处实现**，避免两处逻辑漂移。
     */
    static QString currentIPv4();

    // 立即检查一次（用于启动后先看一眼当前状态）
    void checkNow() { check(); }

signals:
    void networkConnected(const QString &ssid, const QString &ip);
    void networkDisconnected(const QString &reason);

private slots:
    void check();

private:
    void showConnectedPopup(const QString &ssid, const QString &ip);
    void showDisconnectedPopup(const QString &reason);

    QTimer *m_timer;
};

#endif // NETWORKWATCHER_H
