#ifndef CANMGR_H
#define CANMGR_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QTimer>
#include <QElapsedTimer>

class QSocketNotifier;

/*
 * OneTire —— 单个车轮的胎压/胎温（0x105 一个字节一项）
 *
 * 为什么要单独一个结构体：胎压/胎温是**每轮各自有效、各自新鲜**的
 * （B0~B3 里任何一个是 0/0xFF 就只让那一个轮子显示 "--"，不能把四个轮子一起废掉），
 * 面板上也要按轮画在左前/右前/左后/右后四个位置，所以按轮分开存最直观。
 */
struct OneTire {
    int  kpa       = 0;     // 胎压 kPa（原始 LSB = 5 kPa）
    int  tempC     = 0;     // 胎温 ℃（原始值 = 摄氏 + 40）
    bool valid     = false; // 胎压那一字节不是 0 / 0xFF
    bool tempValid = false; // 胎温那一字节不是 0xFF（独立判断：胎压有效不代表胎温有效，
                            // 例如只发了 4 个字节的帧里胎温是"没给"，不能画成 0℃）
    bool fresh     = false; // 0x105 是否在 1 秒内收到过
};

/*
 * CanMgr —— 车机侧的 CAN 接口层（SocketCAN）
 * ============================================================================
 *
 * 数据方向：
 *   下行（电脑 → 板子）：0x100 驾驶状态 / 0x101 车门舱盖 / 0x102 灯光空调反馈
 *                        0x103 里程油量续航 / 0x104 油耗模式故障 / 0x105 胎压胎温
 *   上行（板子 → 电脑）：0x201 空调 / 0x202 车窗 / 0x203 灯光 / 0x204 车辆工作模式
 *   本地（不经总线）    ：写 /dev/mycarctl 1 字节掩码 → M0 → 转向灯 LED / 倒车蜂鸣
 *
 * 全部字节/位定义见 docs/CAN_接口定义与操作_20260929.md（0x100~0x203 与厂商手册一致；
 * 0x103/0x104/0x105/0x204 是**本项目自定义扩展**，见该文档 §3.4 与
 * docs/车辆状态与模式_20261001.md）。
 *
 * 超时语义（2026-09-28 按用户要求；0x103/0x104/0x105 沿用同一条 1 秒规则）：
 *   - 超时 1000 ms，**每组 ID 各自独立**；
 *   - 数值类（车速/档位、里程/油量/续航、油耗、胎压胎温）：**保持上一个值**但标记为
 *     不新鲜（xxxFresh() 变 false → 界面用暗灰显示）；
 *   - 状态类（指示灯/门位/灯光反馈、告警灯位/故障码、工作模式）：清零/置无效
 *     （显示"关"/"无"/"--"），不显示灰色问号；
 *   - 0x100 超时时把 CAN 那份掩码清掉，避免转向灯一直闪、倒车一直响
 *     （本地近光/远光由车内控制页下发，不受影响）；
 *   - 想保持某个状态：电脑端用周期发送（≤500 ms 一帧）。
 *
 * 无硬件也能验证：把 iface 设成 vcan0（VM/板子都可以 ip link add vcan0 type vcan），
 * 或者用演示模式（car-demo-start.sh / SMARTCAR_CAN_DEMO=1）——演示走的是**同一条解析
 * 通路**，所以界面行为与真帧完全一致。
 */
class CanMgr : public QObject
{
    Q_OBJECT

public:
    explicit CanMgr(QObject *parent = nullptr);
    ~CanMgr() override;

    /* 打开接口；iface 为空时按 $SMARTCAR_CAN_IFACE → can0 → vcan0 → 演示 依次尝试 */
    bool open(const QString &iface = QString());
    void close();
    bool isOpen() const { return m_fd >= 0 || m_demo; }
    QString iface() const { return m_iface; }
    QString lastError() const { return m_err; }

    /* ---------------- 下行状态（电脑发来的车况） ---------------- */
    int     speedKmh() const { return m_speed; }        // km/h，0xFF 视为无效
    bool    speedValid() const { return m_speedValid; }
    bool    speedFresh() const { return m_driveFresh; } // false = 超时，界面显示暗灰
    QString gearText() const;                           // "P"/"R"/"N"/"D"/"--"
    bool    gearFresh() const { return m_driveFresh; }
    int     rpmX100() const { return m_rpm; }

    /*
     * ---------------- 0x103 里程/油量/续航（数值类：超时保留最后值但转暗灰）--------
     * 里程一律以**原始 0.1 km 整数**保存（协议就是 0.1 km 一档），需要 km 时用
     * odoKm()/tripKm()；界面自己决定小数位（当前面板显示一位小数）。
     * 为什么不在解析时就直接存 double：0.1 不能用二进制精确表示，反复加减会出现
     * 12345.599999… 这种值，格式化时反而要再补一次四舍五入 —— 存整数最稳。
     */
    int     odoTenths() const { return m_odoTenths; }   // 总里程，0.1 km（0xFFFFFF 视为无效）
    double  odoKm() const { return m_odoTenths / 10.0; }
    bool    odoValid() const { return m_odoValid; }     // 帧里给的是有效值（不是 0xFFFFFF）
    bool    odoFresh() const { return m_odoFresh; }     // false = 超时，界面显示暗灰

    int     tripTenths() const { return m_tripTenths; } // 小计里程，0.1 km（0xFFFF 视为无效）
    double  tripKm() const { return m_tripTenths / 10.0; }
    bool    tripValid() const { return m_tripValid; }
    bool    tripFresh() const { return m_odoFresh; }    // 同一帧（0x103），与总里程同组

    int     fuelPercent() const { return m_fuelPercent; }  // 1..100；**0 也算无效**
    bool    fuelValid() const { return m_fuelValid; }
    bool    fuelFresh() const { return m_odoFresh; }

    int     rangeKm() const { return m_rangeKm; }       // 续航 km（0xFFFF 视为无效）
    bool    rangeValid() const { return m_rangeValid; }
    bool    rangeFresh() const { return m_odoFresh; }

    /*
     * ---------------- 0x104 油耗/工作模式/故障告警 ----------------
     * 油耗同样以**原始 0.1 L/100km 整数**保存，界面用 instFuelL100() 拿 double。
     * 注意"瞬时油耗"没有"0 无效"的约定：0.0 是合法的（松油门滑行真的是 0），
     * 只有 0xFF 才是无效 —— 这一点和油量（0 也算无效）**不一样**，别搞混。
     */
    int     instFuelTenths() const { return m_instFuelTenths; }
    double  instFuelL100() const { return m_instFuelTenths / 10.0; }
    bool    instFuelValid() const { return m_instFuelValid; }
    bool    instFuelFresh() const { return m_faultFresh; }

    int     avgFuelTenths() const { return m_avgFuelTenths; }
    double  avgFuelL100() const { return m_avgFuelTenths / 10.0; }
    bool    avgFuelValid() const { return m_avgFuelValid; }
    bool    avgFuelFresh() const { return m_faultFresh; }

    /* 工作模式：0 经济 1 标准 2 运动 3 雪地；-1 = 无效（含超时后清零） */
    enum DriveMode { ModeEco = 0, ModeNormal = 1, ModeSport = 2, ModeSnow = 3, ModeNone = -1 };
    int     driveMode() const { return m_driveMode; }
    QString driveModeText() const;                      // "经济"/"标准"/"运动"/"雪地"/"--"
    bool    driveModeFresh() const { return m_faultFresh; }   // 与故障告警同一帧（0x104）
    static QString driveModeName(int mode);             // 发送侧/界面共用（无效 → "--"）

    /*
     * 告警灯位（0x104 B3）—— **状态类：超时清零**，所以这里直接与 fresh 相与。
     * 位定义（本项目自定义扩展，不是标准 OBD/J1939）：
     *   bit0 发动机故障 bit1 胎压报警 bit2 油量低 bit3 保养到期
     *   bit4 ABS       bit5 蓄电池   bit6 制动系统 bit7 其它
     */
    enum WarnBit {
        WarnEngine   = 0x01, WarnTire    = 0x02, WarnLowFuel = 0x04, WarnService = 0x08,
        WarnAbs      = 0x10, WarnBattery = 0x20, WarnBrake   = 0x40, WarnOther   = 0x80
    };
    bool    warnActive(int bit) const;
    quint8  warnBits() const { return m_faultFresh ? m_warnBits : 0; }
    int     dtcCount() const { return m_faultFresh ? m_dtcCount : 0; }   // 故障码个数
    int     dtcCode() const  { return m_faultFresh ? m_dtcCode  : 0; }   // 首个故障码（16 位）
    QString dtcText() const;      // "P0301" / "无"（0 = 无故障；高字节是 P 码低 8 位）
    bool    faultFresh() const { return m_faultFresh; }

    /* ---------------- 0x105 胎压/胎温（每轮各自 valid/fresh） ---------------- */
    enum TirePos { TireFL = 0, TireFR = 1, TireRL = 2, TireRR = 3 };   // 左前/右前/左后/右后
    OneTire tire(int pos) const;            // 越界返回一个"无效空轮"（绝不崩）
    static QString tirePosName(int pos);    // "左前"…

    /* 新鲜度（毫秒；-1 = 从没收到过） */
    qint64 msSinceDrive() const;
    qint64 msSinceDoor() const;
    qint64 msSinceLight() const;
    qint64 msSinceOdo() const;
    qint64 msSinceFault() const;
    qint64 msSinceTire() const;

    /* ---------------- 上行控制（车内控制页 → 电脑） ---------------- */
    /* 每帧按手册要求 200 ms 内重发 3 次 */
    bool sendAc(bool on, int tempC, int fan, bool recirc);
    bool sendWindow(int win, int action, int targetPercent);
    bool sendLight(quint8 onMask, quint8 offMask);
    /* 0x204 车辆工作模式切换：0 经济 1 标准 2 运动 3 雪地（B0 = 目标模式，其余 0） */
    bool sendDriveMode(int mode);
    bool sendRaw(quint32 id, const quint8 *data, int dlc);

    bool leftTurn() const  { return m_driveFresh && m_left; }
    bool rightTurn() const { return m_driveFresh && m_right; }
    bool hazard() const    { return m_driveFresh && m_hazard; }
    bool brake() const     { return m_driveFresh && m_brake; }
    bool reverse() const   { return m_driveFresh && m_reverse; }
    bool seatbeltOff() const { return m_driveFresh && m_seatbeltOff; }
    bool handbrake() const { return m_driveFresh && m_handbrake; }

    /* 车门：0 左前 1 右前 2 左后 3 右后 4 后备箱 5 引擎盖 */
    bool doorOpen(int which) const;
    bool carLocked() const { return m_doorFresh && m_locked; }
    bool doorFresh() const { return m_doorFresh; }

    /* 灯光：0 近光 1 远光 2 示宽 3 前雾 4 后雾 5 氛围灯 */
    bool lightOn(int bit) const;
    bool acOn() const       { return m_lightFresh && m_ac; }
    bool recircOn() const   { return m_lightFresh && m_recirc; }
    int  acTemp() const     { return m_acTemp; }        // 0 = 未知
    int  outsideTemp() const{ return m_outsideTemp; }   // 0x3C → 20℃，0 = 未知
    bool lightFresh() const { return m_lightFresh; }

    /* ---------------- 上行控制（车内控制页 → 电脑） ----------------
     * 声明在上面的「上行发送」一节（sendAc/sendWindow/sendLight/sendDriveMode/sendRaw）。
     * ⚠️ 这里曾经被误加了一份重复声明，编译器报
     *    "cannot be overloaded with itself" —— 同一签名在同一作用域不能声明两次。 */

    /* 本地 LED / 蜂鸣（/dev/mycarctl，1 字节掩码；不需要电脑确认）
     * bit0 左转 bit1 右转 bit2 双闪 bit3 倒车 bit4 近光 bit5 远光 */
    bool  writeCarCtl(quint8 mask);
    quint8 carCtlMask() const { return m_mask; }
    bool  carCtlAvailable() const { return m_carCtlOk; }
    /* 车内控制页设置的本地灯光位（近光/远光），与 CAN 那份掩码合并后再写 */
    void setLocalLight(bool low, bool high);

    /* 演示模式：不用电脑/总线也能看界面变化（假帧，走同一条解析通路） */
    void startDemo();
    void stopDemo();
    bool demoRunning() const { return m_demo; }

signals:
    void changed();                                   // 任意状态变化（界面重绘）
    void frameReceived(quint32 id, const QByteArray &data);
    void frameSent(quint32 id, const QByteArray &data);
    void timedOut(int id);                            // 某组 ID 超时（界面据此清零/变灰）
    void log(const QString &msg);

private slots:
    void onReadable();
    void onTick();                                    // 100 ms：超时判定 + 演示推进

private:
    void parseFrame(quint32 id, const quint8 *d, int dlc);
    void applyTimeout();
    void pushCarCtl();
    int  canFrameWrite(quint32 id, const quint8 *data, int dlc);
    static QString hexDump(const quint8 *d, int dlc);

    int      m_fd = -1;
    QSocketNotifier *m_notifier = nullptr;
    QString  m_iface;
    QString  m_err;
    bool     m_demo = false;

    /* 0x100 */
    int  m_speed = 0;       bool m_speedValid = false;
    int  m_gear = -1;       int  m_rpm = 0;
    bool m_left=false, m_right=false, m_brake=false, m_reverse=false;
    bool m_seatbeltOff=false, m_handbrake=false, m_hazard=false;
    /* 0x101 */
    quint8 m_doorBits = 0;  bool m_locked = false;
    /* 0x102 */
    quint8 m_lightBits = 0; bool m_ac=false, m_recirc=false;
    int  m_acTemp = 0, m_outsideTemp = 0;

    /* 0x103 里程/油量/续航（数值：超时保留 + 转暗灰） */
    int  m_odoTenths  = 0;  bool m_odoValid  = false;
    int  m_tripTenths = 0;  bool m_tripValid = false;
    int  m_fuelPercent = 0; bool m_fuelValid = false;     // 0 与 0xFF 都当无效
    int  m_rangeKm     = 0; bool m_rangeValid = false;

    /* 0x104 油耗/工作模式/故障告警（油耗是数值，其余是状态：超时清零） */
    int  m_instFuelTenths = 0; bool m_instFuelValid = false;
    int  m_avgFuelTenths  = 0; bool m_avgFuelValid  = false;
    int  m_driveMode = ModeNone;                          // -1 = 无效
    quint8 m_warnBits = 0;                                // 告警灯位（B3）
    int  m_dtcCount = 0;                                  // 故障码个数（B4）
    int  m_dtcCode  = 0;                                  // 首个故障码（B5~B6，16 位小端）

    /* 0x105 胎压/胎温：四轮各自有效、各自新鲜 */
    OneTire m_tire[4];

    bool m_driveHeard=false, m_doorHeard=false, m_lightHeard=false;
    bool m_odoHeard=false,   m_faultHeard=false, m_tireHeard=false;
    bool m_driveFresh=false, m_doorFresh=false, m_lightFresh=false;
    bool m_odoFresh=false,   m_faultFresh=false, m_tireFresh=false;
    QElapsedTimer m_tDrive, m_tDoor, m_tLight, m_tOdo, m_tFault, m_tTire;
    QElapsedTimer m_maskLog;                      // 本地掩码日志限流（1 秒一次）
    QElapsedTimer m_demoLast;                     // 演示模式上一拍的时刻（用来做"随时间缓慢变化"的量）

    /* 本地掩码 */
    quint8 m_localLight = 0;      // 车内控制页点亮的近光/远光
    quint8 m_mask = 0;            // 最近一次写进 /dev/mycarctl 的值
    bool   m_carCtlOk = false;
    bool   m_carCtlWarned = false;

    QTimer m_tick;
    int    m_demoStep = 0;

    /* 演示模式的长趋势量：里程只增、油量只减（跨"轮"累积，这样演示看起来才像真车） */
    int    m_demoOdoTenths  = 123456;   // 12345.6 km 起
    int    m_demoFuel       = 68;       // 68% 起
    int    m_demoTripTenths = 0;
};

#endif // CANMGR_H
