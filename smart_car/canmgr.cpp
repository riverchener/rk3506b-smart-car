#include "canmgr.h"

#include <QDebug>
#include <QFile>
#include <QSocketNotifier>
#include <QStringList>
#include <QtMath>          // 演示模式的 qSin/qRound（油耗、胎压的细微波动）

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <linux/can.h>
#include <linux/can/raw.h>

namespace {
const int   kTimeoutMs   = 1000;                    // 手册规定的超时
const int   kTickMs      = 100;
const char *kCarCtlDev   = "/dev/mycarctl";
const char *kDemoFlag    = "/tmp/smartcar_can_demo";

/* 掩码位（与 /dev/mycarctl 定义一致） */
const quint8 kBitLeft = 0x01, kBitRight = 0x02, kBitHazard = 0x04, kBitReverse = 0x08;
const quint8 kBitLow = 0x10, kBitHigh = 0x20;

QString errnoText()
{
    return QString::fromLocal8Bit(strerror(errno));
}
} // namespace

CanMgr::CanMgr(QObject *parent) : QObject(parent)
{
    m_tick.setInterval(kTickMs);
    connect(&m_tick, &QTimer::timeout, this, &CanMgr::onTick);
    m_tick.start();                                  // 一直跑：超时判定 + 演示/标记文件监视
    m_maskLog.start();                               // 掩码日志限流用
}

CanMgr::~CanMgr()
{
    close();
}

QString CanMgr::hexDump(const quint8 *d, int dlc)
{
    QString s;
    for (int i = 0; i < dlc; ++i)
        s += QStringLiteral("%1").arg(d[i], 2, 16, QLatin1Char('0')).toUpper()
             + (i + 1 < dlc ? QLatin1Char(' ') : QLatin1Char(' '));
    return s.trimmed();
}

bool CanMgr::open(const QString &ifaceReq)
{
    close();
    m_err.clear();

    QString want = ifaceReq.trimmed();
    if (want.isEmpty())
        want = qEnvironmentVariable("SMARTCAR_CAN_IFACE").trimmed();

    /* 演示模式：显式要求、或者标记文件在（car-demo-start.sh 会创建它） */
    if (want.compare(QLatin1String("demo"), Qt::CaseInsensitive) == 0
            || (want.isEmpty() && QFile::exists(kDemoFlag))) {
        startDemo();
        return true;
    }

    QStringList list;
    if (!want.isEmpty())
        list << want;                                 // 指定了就用指定的（不存在就报错，别偷偷降级）
    else
        list << QStringLiteral("can0") << QStringLiteral("vcan0");

    for (const QString &name : list) {
        const int fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (fd < 0) {
            m_err = QStringLiteral("socket(PF_CAN) 失败：") + errnoText();
            continue;
        }
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, name.toLocal8Bit().constData(), IFNAMSIZ - 1);
        if (::ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
            m_err = QStringLiteral("接口 %1 不存在（ioctl SIOCGIFINDEX：%2）")
                        .arg(name, errnoText());
            ::close(fd);
            continue;
        }
        struct sockaddr_can addr;
        memset(&addr, 0, sizeof(addr));
        addr.can_family  = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        if (::bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
            m_err = QStringLiteral("bind %1 失败：%2").arg(name, errnoText());
            ::close(fd);
            continue;
        }
        const int fl = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, fl | O_NONBLOCK);

        m_fd = fd;
        m_iface = name;
        m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        /* ⚠️ QSocketNotifier 的信号是 activated()，不是 readyRead()（那是 QLocalSocket 的）；
         * 而且它带一个 QPrivateSignal 参数，QOverload 会匹配不上（编译报 no matching
         * function），所以这里用旧式 SIGNAL/SLOT 写法 —— 它忽略 QPrivateSignal，最稳。 */
        connect(m_notifier, SIGNAL(activated(int)), this, SLOT(onReadable()));
        qInfo().noquote() << "CAN: 已打开" << name;
        emit log(QStringLiteral("CAN 接口 %1 已打开").arg(name));
        return true;
    }

    qWarning().noquote() << "CAN: 打不开接口 ->" << m_err;
    emit log(QStringLiteral("CAN 打不开：") + m_err);
    return false;
}

void CanMgr::close()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_iface.clear();
}

/* ---------------------------------------------------------------- 收帧 */
void CanMgr::onReadable()
{
    if (m_fd < 0 || !m_notifier)
        return;
    for (;;) {
        struct can_frame f;
        const ssize_t n = ::read(m_fd, &f, sizeof(f));
        if (n < 0) {
            /*
             * 接口被 down/up 过（例如 `/etc/init.d/S49can restart`、插拔收发器导致
             * 控制器复位）：SocketCAN 的 socket 会一直返回 ENETDOWN/ENODEV，
             * 继续 read 没有意义 —— 必须重新 open。实测踩到过：
             * 日志里出现 "CAN: read 失败 Network is down" 之后状态栏就再也不更新了。
             */
            if (errno == ENETDOWN || errno == ENODEV) {
                const QString iface = m_iface;
                emit log(QStringLiteral("CAN 接口 %1 掉线（%2），1 秒后自动重连")
                             .arg(iface, errnoText()));
                close();
                QTimer::singleShot(1000, this, [this, iface]() {
                    if (!open(iface))
                        emit log(QStringLiteral("CAN 重连失败：%1").arg(lastError()));
                    else
                        emit log(QStringLiteral("CAN 已重连 %1").arg(iface));
                });
                return;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                qWarning().noquote() << "CAN: read 失败" << errnoText();
            return;
        }
        if (n < static_cast<ssize_t>(sizeof(struct can_frame)))
            return;
        if (f.can_id & CAN_ERR_FLAG) {                // 总线错误帧：只记日志
            qWarning().noquote() << "CAN: 错误帧 id=" << QString::number(f.can_id, 16);
            continue;
        }
        const quint32 id = f.can_id & CAN_EFF_MASK;
        parseFrame(id, f.data, f.can_dlc);
    }
}

void CanMgr::parseFrame(quint32 id, const quint8 *d, int dlc)
{
    bool touched = false;

    switch (id) {
    case 0x100: {
        m_driveHeard = true;
        m_tDrive.restart();
        const bool firstOrStale = !m_driveFresh;
        m_driveFresh = true;

        if (dlc >= 1) {
            if (d[0] == 0xFF) {
                m_speedValid = false;                 // 明确"无效"
            } else {
                m_speed = d[0];
                m_speedValid = true;
            }
        }
        if (dlc >= 2) {
            const quint8 b = d[1];
            m_left        = b & 0x01;
            m_right       = b & 0x02;
            m_brake       = b & 0x04;
            m_reverse     = b & 0x08;
            m_seatbeltOff = b & 0x10;
            m_handbrake   = b & 0x20;
            m_hazard      = b & 0x40;
        }
        if (dlc >= 3) {
            const quint8 g = d[2];
            m_gear = (g <= 3) ? g : -1;               // 0P 1R 2N 3D，其它=无效
        }
        if (dlc >= 4)
            m_rpm = d[3];
        if (firstOrStale)
            emit log(QStringLiteral("CAN RX 0x100 恢复"));
        pushCarCtl();                                 // 转向/倒车 → M0（LED/蜂鸣）
        touched = true;
        break;
    }
    case 0x101: {
        m_doorHeard = true;
        m_tDoor.restart();
        m_doorFresh = true;
        if (dlc >= 1)
            m_doorBits = d[0];
        if (dlc >= 2)
            m_locked = d[1] & 0x01;
        touched = true;
        break;
    }
    case 0x102: {
        m_lightHeard = true;
        m_tLight.restart();
        m_lightFresh = true;
        if (dlc >= 1)
            m_lightBits = d[0];
        if (dlc >= 2) {
            m_ac     = d[1] & 0x01;
            m_recirc = d[1] & 0x02;
        }
        if (dlc >= 3)
            m_acTemp = (d[2] >= 16 && d[2] <= 30) ? d[2] : 0;
        if (dlc >= 4)
            m_outsideTemp = (d[3] == 0) ? 0 : (static_cast<int>(d[3]) - 40);
        touched = true;
        break;
    }

    /*
     * 0x103 里程 / 油量 / 续航（自定义扩展，逐字节定义见文档 §3.4）
     *   B0..B2 总里程 24 位小端 LSB=0.1km（0xFFFFFF 无效）
     *   B3..B4 小计里程 16 位小端 LSB=0.1km（0xFFFF 无效）
     *   B5     剩余油量 1..100；**0 也算无效**（0 是"没数据"最常见的默认填充，
     *          若当成"油量 0"就会误报油量低告警 —— 这是踩过的坑，别改回去）
     *   B6..B7 续航 16 位小端 km（0xFFFF 无效）
     * 全部是"数值类"：解析后只更新值，超时时保留最后值（见 applyTimeout）。
     */
    case 0x103: {
        m_odoHeard = true;
        m_tOdo.restart();
        const bool firstOrStale = !m_odoFresh;
        m_odoFresh = true;
        if (dlc >= 3) {
            const quint32 raw = static_cast<quint32>(d[0])
                                | (static_cast<quint32>(d[1]) << 8)
                                | (static_cast<quint32>(d[2]) << 16);
            if (raw == 0xFFFFFFu) {
                m_odoValid = false;                    // 明确"无效"，界面显示 --
            } else {
                m_odoTenths = static_cast<int>(raw);
                m_odoValid  = true;
            }
        }
        if (dlc >= 5) {
            const quint32 raw = static_cast<quint32>(d[3])
                                | (static_cast<quint32>(d[4]) << 8);
            if (raw == 0xFFFFu) {
                m_tripValid = false;
            } else {
                m_tripTenths = static_cast<int>(raw);
                m_tripValid  = true;
            }
        }
        if (dlc >= 6) {
            const quint8 v = d[5];
            if (v == 0 || v == 0xFF) {
                m_fuelValid = false;
            } else {
                m_fuelPercent = qMin<int>(v, 100);
                m_fuelValid   = true;
            }
        }
        if (dlc >= 8) {
            const quint32 raw = static_cast<quint32>(d[6])
                                | (static_cast<quint32>(d[7]) << 8);
            if (raw == 0xFFFFu) {
                m_rangeValid = false;
            } else {
                m_rangeKm    = static_cast<int>(raw);
                m_rangeValid = true;
            }
        }
        if (firstOrStale)
            emit log(QStringLiteral("CAN RX 0x103 恢复（里程/油量/续航）"));
        touched = true;
        break;
    }

    /*
     * 0x104 油耗 / 工作模式 / 故障告警（自定义扩展，见文档 §3.4）
     *   B0 瞬时油耗 LSB=0.1 L/100km（0xFF 无效；0.0 是合法值，别当无效）
     *   B1 平均油耗 同上
     *   B2 工作模式 0 经济 1 标准 2 运动 3 雪地（其它/0xFF 无效）
     *   B3 告警灯位：bit0 发动机 bit1 胎压 bit2 油量低 bit3 保养
     *                bit4 ABS   bit5 蓄电池 bit6 制动系统 bit7 其它
     *   B4 故障码个数（0 = 无）
     *   B5..B6 首个故障码 16 位小端（P0301 → 0x0301；0 = 无）
     *   B7 备用，固定 0
     * 油耗是数值类（超时保留变灰），模式/告警/故障码是状态类（超时清零）。
     */
    case 0x104: {
        m_faultHeard = true;
        m_tFault.restart();
        const bool firstOrStale = !m_faultFresh;
        m_faultFresh = true;
        if (dlc >= 1) {
            m_instFuelValid  = (d[0] != 0xFF);
            m_instFuelTenths = m_instFuelValid ? d[0] : 0;
        }
        if (dlc >= 2) {
            m_avgFuelValid  = (d[1] != 0xFF);
            m_avgFuelTenths = m_avgFuelValid ? d[1] : 0;
        }
        if (dlc >= 3)
            m_driveMode = (d[2] <= 3) ? static_cast<int>(d[2]) : ModeNone;
        if (dlc >= 4)
            m_warnBits = d[3];
        if (dlc >= 5)
            m_dtcCount = d[4];
        if (dlc >= 7) {
            const int code = static_cast<int>(d[5]) | (static_cast<int>(d[6]) << 8);
            m_dtcCode = code;
        }
        if (firstOrStale)
            emit log(QStringLiteral("CAN RX 0x104 恢复（油耗/模式/故障告警）"));
        touched = true;
        break;
    }

    /*
     * 0x105 胎压 / 胎温（自定义扩展，见文档 §3.4）
     *   B0..B3 左前/右前/左后/右后 胎压 LSB=5 kPa（230 kPa → 46；0 与 0xFF 无效）
     *   B4..B7 左前/右前/左后/右后 胎温 = 摄氏 + 40（35℃ → 75；0xFF 无效）
     * 每轮各自判定有效：只发 2 个字节时另外两轮保持 "--"，不会牵连。
     */
    case 0x105: {
        m_tireHeard = true;
        m_tTire.restart();
        const bool firstOrStale = !m_tireFresh;
        m_tireFresh = true;
        for (int i = 0; i < 4; ++i) {
            OneTire &t = m_tire[i];
            t.fresh = true;
            if (dlc >= 4) {
                const quint8 raw = d[i];
                if (raw == 0 || raw == 0xFF) {
                    t.valid = false;                        // 胎压无效（0 也当无效）
                } else {
                    t.kpa   = static_cast<int>(raw) * 5;    // LSB = 5 kPa
                    t.valid = true;
                }
            }
            if (dlc >= 8) {
                const quint8 raw = d[4 + i];
                if (raw == 0xFF) {
                    t.tempValid = false;                    // 胎温无效 → 只把温度显示成 --
                } else {
                    t.tempC     = static_cast<int>(raw) - 40;   // 值 = 摄氏 + 40
                    t.tempValid = true;
                }
            }
        }
        if (firstOrStale)
            emit log(QStringLiteral("CAN RX 0x105 恢复（胎压/胎温）"));
        touched = true;
        break;
    }

    default:
        /* 其它 ID（含电脑回显的 0x201~0x204）只上报，不改状态 */
        break;
    }

    emit frameReceived(id, QByteArray(reinterpret_cast<const char *>(d), dlc));
    if (touched)
        emit changed();
}

/* ---------------------------------------------------------------- 超时 */
void CanMgr::applyTimeout()
{
    bool touched = false;

    if (m_driveHeard && m_driveFresh && m_tDrive.elapsed() > kTimeoutMs) {
        m_driveFresh = false;
        /* 指示灯清零（手册：超时后显示"关"，不留灰问号）；
         * 车速/档位保留最后值，只是变"不新鲜"（界面用暗灰显示）。 */
        m_left = m_right = m_brake = m_reverse = false;
        m_seatbeltOff = m_handbrake = m_hazard = false;
        emit timedOut(0x100);
        emit log(QStringLiteral("CAN 0x100 超时：转向/刹车/倒车等已清零，车速档位转暗灰"));
        pushCarCtl();                                 // CAN 那份掩码清掉 → LED 停闪、蜂鸣停
        touched = true;
    }
    if (m_doorHeard && m_doorFresh && m_tDoor.elapsed() > kTimeoutMs) {
        m_doorFresh = false;
        m_doorBits = 0;
        m_locked = false;
        emit timedOut(0x101);
        touched = true;
    }
    if (m_lightHeard && m_lightFresh && m_tLight.elapsed() > kTimeoutMs) {
        m_lightFresh = false;
        m_lightBits = 0;
        m_ac = m_recirc = false;
        m_acTemp = m_outsideTemp = 0;
        emit timedOut(0x102);
        touched = true;
    }

    /*
     * 0x103（数值类）：**只把 fresh 置 false，值一律保留**。
     * 界面据此把里程/油量/续航画成暗灰（和车速同样的处理），
     * 这样"电脑暂时停发"不会让里程突然变成 0 —— 里程跳 0 比显示旧值危险得多。
     */
    if (m_odoHeard && m_odoFresh && m_tOdo.elapsed() > kTimeoutMs) {
        m_odoFresh = false;
        /* 这里**故意不改 valid**：valid 表示"帧里那一项本身有没有给有效值"
         * （例如油量 0 也是无效），fresh 才表示"帧还新不新"。
         * 两个标志都留着，界面才能区分三种情况：
         *   !heard        → 从没收到过     → 显示 "--"
         *   valid&&!fresh → 有值但已超时   → 显示数值但用暗灰
         *   valid&&fresh  → 正常           → 亮白
         */
        emit timedOut(0x103);
        emit log(QStringLiteral("CAN 0x103 超时：里程/油量/续航保留最后值并转暗灰"));
        touched = true;
    }

    /*
     * 0x104 一组里混了两类：
     *   · 油耗（B0/B1）是数值类 → 保留值，只转暗灰；
     *   · 工作模式（B2）、告警灯位（B3）、故障码（B4~B6）是状态类 → 清零/置无效。
     * 状态类必须清零：电脑停发之后还挂着"发动机故障 P0301"是误报，
     * 比什么都不显示更糟（和指示灯超时清零同一个道理）。
     */
    if (m_faultHeard && m_faultFresh && m_tFault.elapsed() > kTimeoutMs) {
        m_faultFresh = false;
        m_driveMode = ModeNone;
        m_warnBits = 0;
        m_dtcCount = 0;
        m_dtcCode = 0;
        emit timedOut(0x104);
        emit log(QStringLiteral("CAN 0x104 超时：工作模式/告警/故障码清零，油耗转暗灰"));
        touched = true;
    }

    /* 0x105（数值类）：四轮保留最后值，只把 fresh 置 false → 面板画暗灰 */
    if (m_tireHeard && m_tireFresh && m_tTire.elapsed() > kTimeoutMs) {
        m_tireFresh = false;
        for (int i = 0; i < 4; ++i)
            m_tire[i].fresh = false;          // 值保留（画暗灰），valid 不动（同 0x103 的理由）
        emit timedOut(0x105);
        emit log(QStringLiteral("CAN 0x105 超时：四轮胎压胎温保留最后值并转暗灰"));
        touched = true;
    }
    if (touched)
        emit changed();
}

/* ---------------------------------------------------------------- 本地掩码 */
void CanMgr::pushCarCtl()
{
    quint8 mask = m_localLight;
    if (m_driveFresh) {
        if (m_left)    mask |= kBitLeft;
        if (m_right)   mask |= kBitRight;
        if (m_hazard)  mask |= kBitHazard;
        if (m_reverse) mask |= kBitReverse;
    }
    if (mask != m_mask)
        writeCarCtl(mask);
}

bool CanMgr::writeCarCtl(quint8 mask)
{
    const int fd = ::open(kCarCtlDev, O_WRONLY);
    if (fd < 0) {
        if (!m_carCtlWarned) {
            m_carCtlWarned = true;
            qWarning().noquote() << "CAN: 打不开" << kCarCtlDev << errnoText()
                                 << "（M0 没起来？热重启后写会 EIO，需要断电冷启动）";
            emit log(QStringLiteral("本地 LED/蜂鸣不可用（%1：%2）")
                         .arg(QString::fromLatin1(kCarCtlDev), errnoText()));
        }
        m_carCtlOk = false;
        return false;
    }
    const ssize_t w = ::write(fd, &mask, 1);
    ::close(fd);
    if (w != 1) {
        if (!m_carCtlWarned) {
            m_carCtlWarned = true;
            emit log(QStringLiteral("写 %1 失败：%2").arg(QString::fromLatin1(kCarCtlDev), errnoText()));
        }
        m_carCtlOk = false;
        return false;
    }
    m_carCtlOk = true;
    m_mask = mask;
    m_carCtlWarned = false;
    /* 现场核对用：把"CAN 帧 → 本地掩码 → M0 执行"这条链路留痕。
     * 限流 1 秒一次，正常开车（转向灯 1.5Hz 变化）也不会刷屏。 */
    if (m_maskLog.elapsed() > 1000) {
        m_maskLog.restart();
        emit log(QStringLiteral("本地掩码 → 0x%1（转向闪/LED/倒车蜂鸣由 M0 执行）")
                     .arg(mask, 2, 16, QLatin1Char('0')).toUpper());
    }
    return true;
}

void CanMgr::setLocalLight(bool low, bool high)
{
    quint8 m = 0;
    if (low)  m |= kBitLow;
    if (high) m |= kBitHigh;
    if (m == m_localLight)
        return;
    m_localLight = m;
    pushCarCtl();
}

/* ---------------------------------------------------------------- 发帧 */
int CanMgr::canFrameWrite(quint32 id, const quint8 *data, int dlc)
{
    if (m_fd < 0) {
        emit log(QStringLiteral("CAN 未打开，发帧失败"));
        return -1;
    }
    struct can_frame f;
    memset(&f, 0, sizeof(f));
    f.can_id  = id & CAN_SFF_MASK;                    // 标准帧（11 位）
    f.can_dlc = static_cast<__u8>(qBound(0, dlc, 8));
    if (data)
        memcpy(f.data, data, f.can_dlc);
    const ssize_t n = ::write(m_fd, &f, sizeof(f));
    if (n != sizeof(f)) {
        emit log(QStringLiteral("发帧 0x%1 失败：%2")
                     .arg(id, 0, 16).arg(errnoText()));
        return -1;
    }
    emit frameSent(id, QByteArray(reinterpret_cast<const char *>(f.data), f.can_dlc));
    return 0;
}

bool CanMgr::sendRaw(quint32 id, const quint8 *data, int dlc)
{
    return canFrameWrite(id, data, dlc) == 0;
}

/* 手册要求：每次点击 200 ms 内重发 3 次（蓝牙/CAN 都可能丢一帧） */
static void repeat3(CanMgr *self, quint32 id, const quint8 *data, int dlc)
{
    QByteArray copy(reinterpret_cast<const char *>(data), dlc);
    self->sendRaw(id, data, dlc);
    QTimer::singleShot(70, self, [self, id, copy]() {
        self->sendRaw(id, reinterpret_cast<const quint8 *>(copy.constData()), copy.size());
    });
    QTimer::singleShot(140, self, [self, id, copy]() {
        self->sendRaw(id, reinterpret_cast<const quint8 *>(copy.constData()), copy.size());
    });
}

bool CanMgr::sendAc(bool on, int tempC, int fan, bool recirc)
{
    quint8 d[8] = {0};
    d[0] = on ? 0x01 : 0x00;
    d[1] = static_cast<quint8>(qBound(16, tempC, 30));
    d[2] = static_cast<quint8>(qBound(0, fan, 7));
    d[3] = recirc ? 0x01 : 0x00;
    repeat3(this, 0x201, d, 8);
    emit log(QStringLiteral("发送 0x201 空调：%1 温度%2℃ 风量%3 %4")
                 .arg(on ? "开" : "关").arg(d[1]).arg(d[2]).arg(recirc ? "内循环" : "外循环"));
    return true;
}

bool CanMgr::sendWindow(int win, int action, int targetPercent)
{
    quint8 d[8] = {0};
    d[0] = static_cast<quint8>(win);                  // 0 全部 1 左前 2 右前 3 左后 4 右后
    d[1] = static_cast<quint8>(action);               // 0 停 1 升 2 降
    d[2] = static_cast<quint8>(qBound(0, targetPercent, 100));
    repeat3(this, 0x202, d, 8);
    emit log(QStringLiteral("发送 0x202 车窗：窗位%1 动作%2 目标%3%")
                 .arg(win).arg(action).arg(d[2]));
    return true;
}

bool CanMgr::sendLight(quint8 onMask, quint8 offMask)
{
    quint8 d[8] = {0};
    d[0] = onMask;
    d[1] = offMask;
    repeat3(this, 0x203, d, 8);
    emit log(QStringLiteral("发送 0x203 灯光：开掩码 0x%1 关掩码 0x%2")
                 .arg(onMask, 2, 16, QLatin1Char('0'))
                 .arg(offMask, 2, 16, QLatin1Char('0')).toUpper());
    return true;
}

/*
 * 0x204 车辆工作模式切换（自定义扩展，见文档 §3.4）
 * 只用 B0 = 目标模式（0 经济 1 标准 2 运动 3 雪地），其余字节固定 0。
 * 发送方式沿用现有约定：一次点击 200 ms 内重发 3 次（repeat3）。
 * 为什么发送侧要 clamp（而不是把非法值原样发出去）：B0 只有 4 个合法取值，
 * 发 0x7 这种值电脑端无从解释；宁可夹到最近的合法模式，日志里也写明目标模式。
 */
bool CanMgr::sendDriveMode(int mode)
{
    const int m = qBound(static_cast<int>(ModeEco), mode, static_cast<int>(ModeSnow));
    quint8 d[8] = {0};
    d[0] = static_cast<quint8>(m);
    repeat3(this, 0x204, d, 8);
    emit log(QStringLiteral("发送 0x204 工作模式：%1").arg(driveModeName(m)));
    return true;
}

/* 模式文本（发送日志 / 面板 / 车内控制页共用同一份，避免三处各写一遍） */
QString CanMgr::driveModeName(int mode)
{
    switch (mode) {
    case ModeEco:    return QStringLiteral("经济");
    case ModeNormal: return QStringLiteral("标准");
    case ModeSport:  return QStringLiteral("运动");
    case ModeSnow:   return QStringLiteral("雪地");
    default:         return QStringLiteral("--");
    }
}

QString CanMgr::driveModeText() const
{
    if (!m_faultFresh)                       // 0x104 超时 → 状态类清零并显示 --
        return QStringLiteral("--");
    return driveModeName(m_driveMode);
}

QString CanMgr::tirePosName(int pos)
{
    switch (pos) {
    case TireFL: return QStringLiteral("左前");
    case TireFR: return QStringLiteral("右前");
    case TireRL: return QStringLiteral("左后");
    case TireRR: return QStringLiteral("右后");
    default:     return QStringLiteral("--");
    }
}

/*
 * 故障码显示成 P0301 这种形式（OBD-II 的 P 码写法）：
 * 帧里传的是 16 位码值，**高字节是 P 码低 8 位** —— 例如 P0301 传 0x0301；
 * 所以显示时按高字节补出 P0xxx / P1xxx…（0x03 → "P03"，0x0301 → "P0301"）。
 * 这是本项目自定义的口径，不是标准 OBD 编码（标准 DTC 还要区分 P/C/B/U 与首位数字）。
 */
QString CanMgr::dtcText() const
{
    const int code = dtcCode();
    if (code <= 0)
        return QStringLiteral("无");
    return QStringLiteral("P%1").arg(code & 0xFFFF, 4, 16, QLatin1Char('0')).toUpper();
}

bool CanMgr::warnActive(int bit) const
{
    if (!m_faultFresh)
        return false;
    return (m_warnBits & static_cast<quint8>(bit)) != 0;
}

OneTire CanMgr::tire(int pos) const
{
    OneTire t;
    if (pos < 0 || pos > 3)
        return t;                             // 越界：返回"无效空轮"，绝不越界读数组
    t = m_tire[pos];
    t.fresh = t.fresh && m_tireFresh;          // 组超时 → 四轮一起变暗灰
    return t;
}

/* ---------------------------------------------------------------- 状态查询 */
QString CanMgr::gearText() const
{
    switch (m_gear) {
    case 0: return QStringLiteral("P");
    case 1: return QStringLiteral("R");
    case 2: return QStringLiteral("N");
    case 3: return QStringLiteral("D");
    default: return QStringLiteral("--");
    }
}

bool CanMgr::doorOpen(int which) const
{
    if (!m_doorFresh || which < 0 || which > 5)
        return false;
    return m_doorBits & (1u << which);
}

bool CanMgr::lightOn(int bit) const
{
    if (!m_lightFresh || bit < 0 || bit > 5)
        return false;
    return m_lightBits & (1u << bit);
}

qint64 CanMgr::msSinceDrive() const { return m_driveHeard ? m_tDrive.elapsed() : -1; }
qint64 CanMgr::msSinceDoor()  const { return m_doorHeard  ? m_tDoor.elapsed()  : -1; }
qint64 CanMgr::msSinceLight() const { return m_lightHeard ? m_tLight.elapsed() : -1; }
qint64 CanMgr::msSinceOdo()   const { return m_odoHeard   ? m_tOdo.elapsed()   : -1; }
qint64 CanMgr::msSinceFault() const { return m_faultHeard ? m_tFault.elapsed() : -1; }
qint64 CanMgr::msSinceTire()  const { return m_tireHeard  ? m_tTire.elapsed()  : -1; }

/* ---------------------------------------------------------------- 演示模式 */
void CanMgr::startDemo()
{
    if (m_demo)
        return;
    m_demo = true;
    m_demoStep = 0;
    /* 演示的长趋势量每次都从同一个起点开始（便于反复观察、截图对比）：
     * 总里程 12345.6 km、小计 0.0 km、油量 68% */
    m_demoOdoTenths  = 123456;
    m_demoTripTenths = 0;
    m_demoFuel       = 68;
    m_demoLast.restart();
    m_iface = QStringLiteral("演示(假帧)");
    qInfo().noquote() << "CAN: 进入演示模式（假帧，走同一条解析通路）";
    emit log(QStringLiteral("CAN 演示模式：无需电脑/总线"));
    emit changed();
}

void CanMgr::stopDemo()
{
    if (!m_demo)
        return;
    m_demo = false;
    qInfo().noquote() << "CAN: 退出演示模式（停喂 1 秒后状态会自动变灰/清零）";
    emit log(QStringLiteral("CAN 演示已停止"));
    emit changed();
}

void CanMgr::onTick()
{
    /* 标记文件（car-demo-start.sh / car-demo-stop.sh）随时生效，不用重启 app */
    const bool flag = QFile::exists(kDemoFlag);
    if (flag && !m_demo && m_fd < 0)
        startDemo();
    else if (!flag && m_demo && qEnvironmentVariableIsEmpty("SMARTCAR_CAN_DEMO"))
        stopDemo();

    if (m_demo) {
        /* 时间线（循环演示，便于反复观察）：
         * 怠速 → 车速爬到 66 → 左转 → 刹车 → 安全带未系 → 左前门开 → 倒车 → 回到怠速 */
        ++m_demoStep;
        const int t = m_demoStep % 260;               // 26 秒一轮
        quint8 d100[8] = {0};
        int speed = 0;
        if (t < 10) {
            speed = 0;
        } else if (t < 70) {
            speed = (t - 10) * 66 / 60;               // 6 秒爬到 66
        } else if (t < 200) {
            speed = 66;
        } else {
            speed = qMax(0, 66 - (t - 200) * 66 / 40); // 慢慢滑到 0
        }
        d100[0] = static_cast<quint8>(speed);
        quint8 flags = 0;
        if (t >= 80 && t < 110)  flags |= 0x01;       // 左转
        if (t >= 110 && t < 140) flags |= 0x04;       // 刹车
        if (t >= 140 && t < 200) flags |= 0x10;       // 安全带未系
        if (t >= 170 && t < 200) flags |= 0x02;       // 右转（连演示右灯）
        if (t >= 200)            flags |= 0x08;       // 倒车
        d100[1] = flags;
        d100[2] = (t >= 200) ? 0x01 : 0x03;           // R 挡 / D 挡
        d100[3] = 8;                                  // 转速 800
        parseFrame(0x100, d100, 8);

        quint8 d101[8] = {0};
        if (t >= 170 && t < 200)                      // 左前门开
            d101[0] = 0x01;
        parseFrame(0x101, d101, 8);

        quint8 d102[8] = {0};
        d102[0] = (t >= 110 && t < 200) ? 0x01 : 0x00;  // 近光
        d102[1] = 0x01;                                  // AC 开
        d102[2] = 24;                                    // 24℃
        d102[3] = 60;                                    // 车外 20℃
        parseFrame(0x102, d102, 8);

        /*
         * 0x103 里程/油量/续航（演示）
         * 里程要"随风速缓慢增长"，油量要"缓慢下降"，所以这两个量**跨轮累积**
         * （m_demoOdoTenths / m_demoFuel 是成员，不会因为时间线回到起点而复位）。
         * 里程按 dt 积分：0.1 km 一档，速度 66 km/h 时 100 ms 只走 0.0018 km，
         * 取整后大部分拍子上里程是不动的 —— 这正是真车的样子（里程表本来就跳得慢）。
         */
        {
            const qint64 dtMs = qMax<qint64>(0, m_demoLast.restart());
            m_demoOdoTenths  += static_cast<int>(speed * dtMs / 3600);   // km/h·ms → 0.1km 档
            m_demoTripTenths += static_cast<int>(speed * dtMs / 3600);
            if (m_demoTripTenths > 9999)                       // 小计里程 0xFFFF 上限，别溢出
                m_demoTripTenths = 9999;

            quint8 d103[8] = {0};
            const int odo = m_demoOdoTenths & 0xFFFFFF;        // 24 位小端
            d103[0] = static_cast<quint8>(odo & 0xFF);
            d103[1] = static_cast<quint8>((odo >> 8) & 0xFF);
            d103[2] = static_cast<quint8>((odo >> 16) & 0xFF);
            d103[3] = static_cast<quint8>(m_demoTripTenths & 0xFF);
            d103[4] = static_cast<quint8>((m_demoTripTenths >> 8) & 0xFF);
            d103[5] = static_cast<quint8>(qBound(0, m_demoFuel, 100));
            /* 续航按 8 km/1% 估算（68% → 544 km，看起来合理） */
            const int range = qBound(0, m_demoFuel * 8, 0xFFFE);
            d103[6] = static_cast<quint8>(range & 0xFF);
            d103[7] = static_cast<quint8>((range >> 8) & 0xFF);
            parseFrame(0x103, d103, 8);

            /* 油量缓慢下降：约每 5 秒掉 1%（一轮 26 秒掉 5%，肉眼能看出条在缩） */
            if (m_demoStep % 50 == 0 && m_demoFuel > 8)
                --m_demoFuel;
        }

        /*
         * 0x104 油耗/模式/故障告警（演示）
         *   模式：一轮里轮换 经济 → 标准 → 运动 → 雪地（各占 1/4 时间）
         *   油耗：怠速高（滑行/停车时瞬时油耗反而显示得高）、巡航低，来回小幅波动
         *   故障：t∈[205,240) 出现"胎压报警 + 发动机故障 + 故障码 P0301"，其余时间无故障
         */
        {
            quint8 d104[8] = {0};
            /* 瞬时油耗：0.1 L/100km，怠速 12.0、巡航 6.5，按 sin 做小幅波动 */
            const double inst = (speed < 5 ? 12.0 : 6.5 + 1.5 * qSin(m_demoStep / 7.0));
            const double avg  = 7.4 + 0.3 * qSin(m_demoStep / 23.0);
            d104[0] = static_cast<quint8>(qBound(0, qRound(inst * 10), 0xFE));
            d104[1] = static_cast<quint8>(qBound(0, qRound(avg * 10), 0xFE));
            d104[2] = static_cast<quint8>((t / 65) % 4);       // 0 经济 1 标准 2 运动 3 雪地
            if (t >= 205 && t < 240) {
                d104[3] = 0x02 | 0x01;                         // bit1 胎压报警 + bit0 发动机故障
                d104[4] = 1;                                   // 故障码个数 1
                d104[5] = 0x01;                                // 0x0301（小端低字节）→ 显示 P0301
                d104[6] = 0x03;
            }
            d104[7] = 0x00;                                    // B7 备用固定 0
            parseFrame(0x104, d104, 8);
        }

        /*
         * 0x105 胎压/胎温（演示）
         *   胎压基准 230 kPa（值 46），随车速轻微升高（胎温也升）；左后再刻意低 15 kPa
         *   让"超范围变红"这个效果一定能看到。
         *   t∈[205,240) 时右前抬到 285 kPa（超 280 上限）配合 0x104 的胎压报警。
         */
        {
            quint8 d105[8] = {0};
            const int warm   = qMin(12, speed / 6);            // 跑热了胎压涨一点
            const int base   = 46 + warm / 2;
            const int wobble = static_cast<int>(qSin(m_demoStep / 5.0) * 2);
            const int kpa[4] = { base, base + 1, base - 3, base + wobble };  // 左后 (-15kPa)
            for (int i = 0; i < 4; ++i) {
                int v = kpa[i];
                if (t >= 205 && t < 240 && i == TireFR)
                    v = 57;                                    // 285 kPa → 超上限报警
                d105[i] = static_cast<quint8>(qBound(1, v, 0xFE));
            }
            for (int i = 0; i < 4; ++i) {
                const int tempC = 26 + warm / 2 + ((t / 20 + i) % 3);  // 26~34℃ 缓慢变化
                d105[4 + i] = static_cast<quint8>(qBound(0, tempC + 40, 0xFE));
            }
            parseFrame(0x105, d105, 8);
        }
        return;                                       // 演示时不判超时（一直在喂）
    }

    applyTimeout();
}
