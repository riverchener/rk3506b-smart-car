#ifndef BTMANAGER_H
#define BTMANAGER_H

/*
 * BtManager —— 蓝牙管理（扫描 / 配对 / 连接 / 断开）
 *
 * 实现方式：驱动板子上的 bluetoothctl（BlueZ）子进程，而不是自己用
 * QDBus 去调 org.bluez。原因：
 *   - 这块 buildroot 里 Qt 的 QtDBus 模块不一定编出来了，而 bluetoothctl 肯定在
 *   - bluetoothctl 自己会处理 agent（配对确认）、power on、scan 这些状态机，
 *     比我们手撸 BlueZ D-Bus 接口省事也稳
 * 代价是要解析命令行输出，所以下面的解析对多种输出格式都做了兼容
 * （不同 BlueZ 版本的 `devices` 输出格式不一样）。
 *
 * 所有操作都是异步的（QProcess + 信号），不会卡住界面。
 */

#include <QObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

QT_BEGIN_NAMESPACE
class QProcess;
class QTimer;
QT_END_NAMESPACE

struct BtDevice {
    QString address;        // AA:BB:CC:DD:EE:FF
    QString name;           // 友好名（取 Name / Alias）
    bool paired = false;
    bool trusted = false;
    bool connected = false;
    int rssi = 0;           // 信号强度（dBm，负值，越接近 0 越强；0 表示未知）

    QString displayName() const
    {
        return name.isEmpty() ? address : name;
    }
    bool operator==(const BtDevice &o) const { return address == o.address; }
};

class BtManager : public QObject
{
    Q_OBJECT
public:
    explicit BtManager(QObject *parent = nullptr);

    // ---- 适配器状态 ----
    bool powered() const { return m_powered; }
    bool scanning() const { return m_scanning; }
    bool adapterPresent() const { return m_adapterPresent; }
    QString adapterName() const { return m_adapterName; }
    QString lastError() const { return m_lastError; }

    // ---- 已发现设备（含已配对的历史设备）----
    QVector<BtDevice> devices() const { return m_devices; }
    // 已连接的设备（可能有多个，一般取第一个当音频输出）
    QVector<BtDevice> connectedDevices() const;

    bool busy() const { return m_busy; }

    /*
     * 让本机对手机"可见 + 可配对"（discoverable/pairable on）。
     * 手机想主动连车机时必须先做这个，否则手机搜不到车机。
     */
    void makeDiscoverable();
    bool discoverable() const { return m_discoverable; }

public slots:
    void refresh();                       // 查适配器状态 + 设备列表
    void setPowered(bool on);
    void startScan();
    void stopScan();
    void pairDevice(const QString &address);
    void connectDevice(const QString &address);
    void disconnectDevice(const QString &address);
    void removeDevice(const QString &address);   // 忘记设备

signals:
    void devicesChanged(const QVector<BtDevice> &devices);
    void stateChanged();                  // powered / scanning / 连接 变化
    void errorOccurred(const QString &message);
    void infoMessage(const QString &message);

private slots:
    void onProcessFinished();

private:
    /* 排队等执行的命令（bluetoothctl 或 dbus-send，见 PendingCmd::useDbus） */
    struct PendingCmd {
        QStringList args;
        QString tag;
        int timeoutMs = 0;
        bool useDbus = false;
    };

    void run(const QStringList &args, const QString &tag, int timeoutMs = 0);
    void runDbus(const QStringList &args, const QString &tag, int timeoutMs = 0);
    void startNext();              // 从队列取一条执行
    void applyDeviceList(const QString &rawOutput);
    void runDeviceStateNext();     // 逐个 info 补齐 Paired/Trusted/Connected
    void ensurePowered();
    void ensurePairingMode();      // paurable/discoverable on（让手机能发现车机）
    void ensureAudioRoute();       // 有设备连上就把 bluealsa/bluealsa-aplay 拉起来

    QProcess *m_proc = nullptr;
    QProcess *m_scanProc = nullptr;   // 扫描单独一个进程（不占 m_busy，见 startScan）
    QList<PendingCmd> m_queue;        // 待执行的命令队列
    QString m_tag;
    QString m_lastTargetAddr;         // 最近一次 pair/connect 的目标（用于回查真实状态）
    QString m_verifyKind;             // "pair" / "connect"，决定失败时给的提示
    QStringList m_stateQueue;         // 待补状态的设备地址（见 runDeviceStateNext）
    QString m_stateAddr;              // 正在补状态的地址
    QTimer *m_timeout = nullptr;

    QVector<BtDevice> m_devices;
    bool m_powered = false;
    bool m_scanning = false;
    bool m_adapterPresent = true;
    bool m_busy = false;
    bool m_audioRouted = false;    // 蓝牙音频通道当前是否已建立
    bool m_discoverable = false;   // 适配器当前是否可被搜索
    bool m_pairingModeSet = false; // 本次会话是否已设过 pairable
    QString m_adapterName;
    QString m_lastError;
};

#endif // BTMANAGER_H
