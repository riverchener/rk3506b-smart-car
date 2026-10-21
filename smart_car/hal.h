#ifndef HAL_H
#define HAL_H

/*
 * 车载系统硬件抽象层（ATK-DLRK3506B / RK3506B）
 *
 * 所有设备路径 / GPIO 编号都支持用环境变量覆盖，方便现场改接线而不改代码：
 *   SMARTCAR_DHT11_DEVICE   DHT11 内核驱动节点（默认 /dev/mydht11）
 *   SMARTCAR_SR04_DEVICE    SR04  内核驱动节点（默认 /dev/mysr04）
 *   SMARTCAR_BUZZER_DEVICE  蜂鸣器内核驱动节点（默认 /dev/mybuzzer）
 *   SMARTCAR_DHT11_GPIO     DHT11 数据脚 GPIO 全局编号（默认 51 = GPIO1_C3，底板 A 排针 28 脚）
 *   SMARTCAR_SR04_TRIG_GPIO 超声波 TRIG GPIO（默认 54 = GPIO1_C6，A 排针 31 脚）
 *   SMARTCAR_SR04_ECHO_GPIO 超声波 ECHO GPIO（默认 55 = GPIO1_C7，A 排针 33 脚）
 *   SMARTCAR_BUZZER_GPIO    蜂鸣器 GPIO（默认 32 = GPIO1_A0，A 排针 12 脚）
 *   SMARTCAR_BUZZER_ACTIVE_LOW  1=低电平触发（IO 低=响，默认），0=高电平触发
 *   SMARTCAR_BUZZER_SELFTEST    1=开机响一声自检（默认 1，可用来确认接线是否接通）
 *   SMARTCAR_LED_BRIGHTNESS 板载 LED（默认 /sys/class/leds/work/brightness）
 *   SMARTCAR_MQ135_ADC      MQ135 ADC 节点（默认 iio in_voltage3_raw）
 *   SMARTCAR_MQ135_SCALE    每 LSB 多少 mV（默认自动从同级 in_voltage_scale 读，本板 1.7578125）
 *   SMARTCAR_MQ135_DIVIDER  外部分压比 = AO 电压 / ADC 引脚电压（默认 1.0 = 直连）
 *   SMARTCAR_MQ135_ADC_MAX  量程上限（默认 1023，本板 10bit）
 *   SMARTCAR_MQ135_ALARM_RATIO 报警阈值 = 相对基线倍数（默认 1.40）
 *   SMARTCAR_MQ135_BASELINE 手工基线 raw 值（默认 0 = 用上电后清洁空气窗口自动取）
 *   SMARTCAR_MQ135_WARMUP_MIN 预热期分钟数，期间报"预热中"（默认 120）
 *                           MQ-135 手册要求 24~48h 才真正稳定，120 分钟只是
 *                           "先别把它当真值看"的短周期提示，想严格点就调大
 */

#include <QElapsedTimer>
#include <QList>
#include <QString>

class Hal
{
public:
    static Hal &instance();

    // ---- 温湿度 ----
    bool dht11Available() const { return dht11Ok; }
    // 返回 true 表示数据有效；buf = [湿度整数, 湿度小数, 温度整数, 温度小数]
    bool dht11Read(unsigned char *buf, int bytes);

    // ---- 超声波 ----
    // 返回 true 表示测距成功，cm 为距离（厘米）
    bool sr04ReadCm(double *cm);

    // ---- 蜂鸣器 ----
    // on=true 表示报警。默认按常见有源蜂鸣器模块处理：低电平触发。
    // 若你的模块是高电平触发，在 /etc/smartcar.conf 里设 SMARTCAR_BUZZER_ACTIVE_LOW=0
    void buzzerSet(bool on);
    bool buzzerActiveLow() const { return buzzerActiveLowFlag; }
    int buzzerPin() const { return buzzerGpio; }

    // ---- 板载 LED ----
    void ledSet(bool on);

    // ---- MQ135（ADC 原始值 + 真实电压；本板 10bit 0~1023 / vref 1.8V / 1.7578125mV 每 LSB） ----
    bool mq135ReadRaw(int *raw);
    bool mq135Available() const { return mq135Ok; }

    /*
     * 一次完整的真实读数：原始值 + 电压 + 基线比值 + 状态。
     *
     * 电压换算（依据见 docs/MQ135真实驱动实现与验证_20261012.md）：
     *   vAdc = raw * (in_voltage_scale / 1000)      // IIO 的标准换算，本板 scale=1.7578125mV
     *   vAo  = vAdc * divider                        // divider = 外部 1k/2k 分压的反比
     * 没有用 vref/1024 硬算 —— in_voltage_scale 是内核按 vref-supply 算好的，直接读它最稳。
     */
    struct Mq135Sample {
        bool ok = false;          // ADC 节点能读、且读到合法整数
        int raw = 0;              // ADC 原始码值（10bit：0~1023）
        double vAdc = 0.0;        // ADC 引脚电压 = raw * scale（V）
        double vAo = 0.0;         // 换算回传感器 AO 的电压 = vAdc * divider（V）
        double baselineRaw = 0.0; // 清洁空气基线 raw，0 = 还没建立
        double ratio = 0.0;       // raw / 基线（与 MQ-135 手册的 Rs/R0 同性质）
        QString statusCn;         // 未接 / 预热中 / 正常 / 偏高 / 超阈值 / ADC节点不可用
        bool connected = false;   // 有真实传感器在驱动这根线
        bool alarm = false;       // ratio >= 报警倍数
        QString detail;           // 判定依据（写日志用）
    };
    Mq135Sample mq135Sample();

    int mq135AdcMax() const { return mq135AdcMaxValue; }   /* 量程上限（10bit=1023） */
    int mq135LastRaw() const { return mq135LastValue; }
    double mq135MvPerLsb() const { return mq135ScaleMv; }  /* 每 LSB 毫伏（读 in_voltage_scale） */
    int mq135Divider() const { return mq135DividerNum; }   /* 外部分压比 numerator（1 = 直连） */
    QList<int> mq135RawSamples(int n);                     /* 直接抓 n 个原始值（现场排查用） */

    /*
     * MQ-135 到底接了没有。
     *
     * 为什么需要这个：ADC 的 sysfs 节点（in_voltage3_raw）在**任何**板子上都存在，
     * 所以"节点能打开"根本不能证明接了传感器 —— 以前 initMq135() 就是这么判的，
     * 结果永远报 OK，界面上把悬空 ADC 的中点电平显示成"空气质量 50%"。
     *
     * 判据（抓一串原始值看波形性质）：
     *   - 极差 <= SMARTCAR_MQ135_STABLE_SPAN（默认 4）且贴 0 / 贴满量程 => 被拉死，未接
     *   - 极差 <= STABLE_SPAN 且停在中段                             => 悬空 / 被拉死，未接
     *   - 极差小、但序列单调缓慢漂移（相邻差值几乎都同号且 <= 1 个码）
     *     => 悬空脚漏电缓慢充放电，未接（本板实测正是如此：718→675，每 2 秒掉 2~3 个码）
     *   - 有真实上下跳动的随机噪声 => 已接（真实传感器输出必然带噪声）
     *
     * 说人话：这是启发式判定，不是电气测量。真接了模块、环境又极度稳定导致读数
     * 死平的情况会被误判成未接 —— 那种情况把 SMARTCAR_MQ135_ASSUME_CONNECTED=1
     * 设上即可（无条件当作已接）。
     */
    bool mq135Connected();
    /*
     * "空气差"报警阈值（raw），按量程比例算，默认 90%，可用 SMARTCAR_MQ135_ALARM_PCT 调。
     * 说明：**这个函数目前没有被界面用到** —— UI 的报警用的是 mq135Sample() 里的
     * "相对清洁空气基线倍数"（ratio >= SMARTCAR_MQ135_ALARM_RATIO，默认 1.40），
     * 更贴近 MQ-135 的物理特性（它输出的是 Rs 的变化，不是绝对电平）。
     * 这个按量程百分比的阈值作为"没有基线时"的兜底留在这里，供排查/脚本使用。
     */
    int mq135AlarmRaw() const;
    bool dht11ProbeOk() const { return dht11SysfsOk; }
    bool sr04ProbeOk() const { return sr04SysfsOk; }
    bool buzzerAvailable() const { return buzzerOk; }

    // ---- 系统健康监控（CPU / 内存 / 温度 / 磁盘 / 负载 / 运行时间） ----
    struct SysInfo {
        double cpuPercent = -1;   // 整机 CPU 使用率 %（两次采样差值算法）
        double cpuUser = 0, cpuSys = 0, cpuIdle = 0;  // 累计占比 %
        double memTotalMb = 0, memUsedMb = 0, memPercent = 0;
        double swapTotalMb = 0, swapUsedMb = 0;
        double tempC = -1;        // 芯片温度（thermal zone）
        double diskTotalMb = 0, diskUsedMb = 0, diskPercent = 0;  // /userdata
        double rootTotalMb = 0, rootUsedMb = 0, rootPercent = 0;  // / (只读系统)
        double load1 = 0, load5 = 0, load15 = 0;
        int cpuCount = 0;
        long uptimeSec = 0;
        int procCount = 0;
        bool valid = false;
    };
    // 每次调用都会更新 CPU 差值，建议 1 秒调用一次
    bool sysInfo(SysInfo *out);
    QString tempSource() const { return tempPath; }

    QString describe() const { return info; }

private:
    Hal();
    ~Hal();
    Hal(const Hal &);
    Hal &operator=(const Hal &);

    bool initDht11();
    bool dht11ReadOnce(unsigned char *buf);
    bool initSr04();
    bool initBuzzer();
    bool initLed();
    bool initMq135();

    // GPIO sysfs 辅助
    bool gpioExport(int gpio);
    bool gpioSetDirection(int gpio, bool out);
    bool gpioSetPull(int gpio, const char *pull);
    bool gpioWrite(int gpio, int value);
    int gpioRead(int gpio);
    void gpioUnexport(int gpio);

    // 直接寄存器访问（us 级时序用），失败返回 false
    bool gpioUseMmap(int gpio);
    inline int gpioReadFast(int gpio) const;

    int dht11Gpio;
    int sr04TrigGpio;
    int sr04EchoGpio;
    int buzzerGpio;

    QString dht11Node;
    QString sr04Node;
    QString buzzerNode;
    QString ledPath;
    QString mq135Path;
    QString tempPath;      // 温度传感器节点
    QString diskPath;      // 监控的可写分区（默认 /userdata）
    // CPU 差值采样缓存
    long long cpuPrevTotal = 0;
    long long cpuPrevIdle = 0;
    long long cpuPrevUser = 0;
    long long cpuPrevSys = 0;

    bool dht11Ok;
    bool sr04Ok;
    bool buzzerOk;
    bool ledOk;
    bool mq135Ok;
    bool buzzerActiveLowFlag;

    bool dht11SysfsOk;
    bool sr04SysfsOk;

    /* MQ-135 真实读数用（见 mq135Sample() / mq135Connected() 的说明） */
    int mq135AdcMaxValue = 1023;   /* 量程上限：本板 10bit=1023，可用环境变量覆盖 */
    int mq135LastValue = 0;
    double mq135ScaleMv = 1.7578125;  /* 每 LSB 毫伏；initMq135() 时从 in_voltage_scale 读 */
    bool mq135ScaleFromSysfs = false; /* true = 上面的值是真从 sysfs 读到的，不是回退值 */
    int mq135DividerNum = 1;          /* 外部分压比（AO/ADC），1 = 直连 */
    double mq135AlarmRatio = 1.40;    /* 报警倍数（相对清洁空气基线） */
    double mq135BaselineRaw = 0.0;    /* 基线 raw；0 = 未建立 */
    int mq135WarmupMin = 120;         /* 预热期（分钟），期间状态显示"预热中" */
    QElapsedTimer mq135Uptime;        /* 用于预热期计时 */
    bool mq135ConnectedCache = false;
    bool mq135AssumeConnected = false;
    int mq135StableSpan = 4;       /* 极差 <= 这个值算"死平" */
    QList<int> mq135History;       /* 最近若干次读数（mq135Connected 的窗口） */
    QList<int> mq135WindowMeans;   /* 最近几个采样窗口的均值（判"是否一直静止"） */
    QElapsedTimer mq135LastDecide; /* 多久重新判定一次 */
    bool mq135DecideInit = false;
    QString mq135BaselineFile;     /* 基线持久化路径（重启后不用重新标定） */
    bool mq135BaselineSaved = false; /* 本次会话是否已把基线落盘（只写一次，护 flash） */
    bool mq135BaselineFrozenLogged = false; /* "基线冻结"这条日志只打一次，免得刷日志 */

    QString info;
};

#endif // HAL_H
