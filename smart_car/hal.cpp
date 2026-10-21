#include "hal.h"

#include <QDebug>
#include <QFile>
#include <QStringList>
#include <QTextStream>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <QDir>

namespace {

// RK3506 GPIO 控制器（来自 rk3506.dtsi）
struct GpioBank {
    unsigned long base;
    unsigned long clk;
    int first;
};

const GpioBank kBank[] = {
    {0xff940000UL, 0xff940000UL, 0},    // gpio0
    {0xff870000UL, 0xff870000UL, 32},   // gpio1
    {0xff1c0000UL, 0xff1c0000UL, 64},   // gpio2
    {0xff1d0000UL, 0xff1d0000UL, 96},   // gpio3
    {0xff1e0000UL, 0xff1e0000UL, 128},  // gpio4
};
const int kBankCount = sizeof(kBank) / sizeof(kBank[0]);

// 每个 bank 的寄存器布局（RK3506 每个 bank 32bit 数据寄存器）
struct BankRegs {
    volatile unsigned int *swport_dr;  // 数据
    volatile unsigned int *swport_ddr; // 方向
    int shift;
    int perReg; // 每个寄存器控制的引脚数（8 或 32）
};

BankRegs g_regs[kBankCount];
bool g_regsReady[kBankCount] = {false, false, false, false, false};
void *g_maps[kBankCount] = {nullptr, nullptr, nullptr, nullptr, nullptr};

inline long long nowNs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

} // namespace

Hal &Hal::instance()
{
    static Hal inst;
    return inst;
}

Hal::Hal()
    : dht11Gpio(qEnvironmentVariableIntValue("SMARTCAR_DHT11_GPIO")),
      sr04TrigGpio(qEnvironmentVariableIntValue("SMARTCAR_SR04_TRIG_GPIO")),
      sr04EchoGpio(qEnvironmentVariableIntValue("SMARTCAR_SR04_ECHO_GPIO")),
      buzzerGpio(qEnvironmentVariableIntValue("SMARTCAR_BUZZER_GPIO")),
      dht11Node(qEnvironmentVariable("SMARTCAR_DHT11_DEVICE", "/dev/mydht11")),
      sr04Node(qEnvironmentVariable("SMARTCAR_SR04_DEVICE", "/dev/mysr04")),
      buzzerNode(qEnvironmentVariable("SMARTCAR_BUZZER_DEVICE", "/dev/mybuzzer")),
      ledPath(qEnvironmentVariable("SMARTCAR_LED_BRIGHTNESS", "/sys/class/leds/work/brightness")),
      mq135Path(qEnvironmentVariable("SMARTCAR_MQ135_ADC",
                                     "/sys/bus/iio/devices/iio:device0/in_voltage3_raw")),
      tempPath(qEnvironmentVariable("SMARTCAR_TEMP_PATH")),
      diskPath(qEnvironmentVariable("SMARTCAR_DISK_PATH", "/userdata")),
      dht11Ok(false), sr04Ok(false), buzzerOk(false), ledOk(false), mq135Ok(false),
      buzzerActiveLowFlag(true),
      dht11SysfsOk(false), sr04SysfsOk(false)
{
    // GPIO 默认值：GPIO1_C3=51, GPIO1_C6=54, GPIO1_C7=55, GPIO1_D0=56（A21）
    if (dht11Gpio <= 0)
        dht11Gpio = 51;
    if (sr04TrigGpio <= 0)
        sr04TrigGpio = 54;
    if (sr04EchoGpio <= 0)
        sr04EchoGpio = 55;
    if (buzzerGpio <= 0)
        buzzerGpio = 56;

    // 蜂鸣器触发极性：1=低电平响（多数有源模块），0=高电平响
    bool okLow = false;
    const int lowFlag = qEnvironmentVariableIntValue("SMARTCAR_BUZZER_ACTIVE_LOW", &okLow);
    buzzerActiveLowFlag = okLow ? (lowFlag != 0) : true;

    /*
     * MQ-135 参数（见 mq135Sample() / mq135Connected() 的说明）：
     *   SMARTCAR_MQ135_ADC_MAX          量程上限，本板 SARADC 是 10bit -> 1023
     *   SMARTCAR_MQ135_ASSUME_CONNECTED 1 = 无条件当作已接（不信启发式判定时用）
     *   SMARTCAR_MQ135_STABLE_SPAN      极差 <= 此值算"读数死平"
     *   SMARTCAR_MQ135_SCALE            每 LSB 毫伏（默认自动读 in_voltage_scale）
     *   SMARTCAR_MQ135_DIVIDER          外部分压比 = AO/ADC（默认 1 = 直连）
     *   SMARTCAR_MQ135_ALARM_RATIO      报警倍数（默认 1.40）
     *   SMARTCAR_MQ135_BASELINE         手工基线 raw（默认 0 = 自动取）
     *   SMARTCAR_MQ135_WARMUP_MIN       预热期分钟（默认 120；手册要求 24~48h，
     *                                   这里只是"先别当真值看"的短周期提示）
     */
    bool okMax = false;
    const int envMax = qEnvironmentVariableIntValue("SMARTCAR_MQ135_ADC_MAX", &okMax);
    if (okMax && envMax > 1)
        mq135AdcMaxValue = envMax;
    bool okAssume = false;
    const int assume = qEnvironmentVariableIntValue("SMARTCAR_MQ135_ASSUME_CONNECTED", &okAssume);
    mq135AssumeConnected = okAssume && assume != 0;
    bool okSpan = false;
    const int span = qEnvironmentVariableIntValue("SMARTCAR_MQ135_STABLE_SPAN", &okSpan);
    if (okSpan && span >= 0)
        mq135StableSpan = span;

    bool okDiv = false;
    const int div = qEnvironmentVariableIntValue("SMARTCAR_MQ135_DIVIDER", &okDiv);
    if (okDiv && div >= 1)
        mq135DividerNum = div;

    bool okRatio = false;
    const double ratio = qEnvironmentVariable("SMARTCAR_MQ135_ALARM_RATIO").toDouble(&okRatio);
    if (okRatio && ratio > 1.0)
        mq135AlarmRatio = ratio;

    bool okBase = false;
    const double base = qEnvironmentVariable("SMARTCAR_MQ135_BASELINE").toDouble(&okBase);
    if (okBase && base > 1.0)
        mq135BaselineRaw = base;

    bool okWarm = false;
    const int warm = qEnvironmentVariableIntValue("SMARTCAR_MQ135_WARMUP_MIN", &okWarm);
    if (okWarm && warm >= 0)
        mq135WarmupMin = warm;

    mq135Uptime.start();

    /*
     * 基线持久化：MQ-135 的 R0（清洁空气基线）本来是"一次标定、长期使用"的量，
     * 但板子经常断电重启，每次重启都重新标定会让"相对基线倍数"失去意义。
     * 所以把基线存到可写分区，下次启动直接读回来。
     * 手工指定的 SMARTCAR_MQ135_BASELINE 优先级最高（会覆盖文件）。
     * 要强制重新标定：删掉文件，或把 SMARTCAR_MQ135_BASELINE 设成一个值。
     */
    mq135BaselineFile = qEnvironmentVariable(
        "SMARTCAR_MQ135_BASELINE_FILE",
        QStringLiteral("/userdata/smart_car/mq135_baseline.conf"));
    if (mq135BaselineRaw <= 0.0) {
        QFile bf(mq135BaselineFile);
        if (bf.open(QIODevice::ReadOnly)) {
            bool okFile = false;
            const double v = QString::fromLatin1(bf.readAll()).trimmed().toDouble(&okFile);
            bf.close();
            if (okFile && v > 1.0) {
                mq135BaselineRaw = v;
                qInfo().noquote() << QString("MQ135: 从 %1 读回清洁空气基线 raw=%2")
                                         .arg(mq135BaselineFile).arg(v, 0, 'f', 1);
            }
        }
    }

    QStringList parts;

    // 优先使用内核模块提供的 /dev 节点，其次用用户态 GPIO 直驱
    if (initDht11())
        parts << QString("DHT11=OK(%1)").arg(dht11Node);
    else if (dht11SysfsOk)
        parts << QString("DHT11=OK(gpio%1)").arg(dht11Gpio);
    else
        parts << "DHT11=UNAVAILABLE";

    if (initSr04())
        parts << QString("SR04=OK(%1)").arg(sr04Node);
    else if (sr04SysfsOk)
        parts << QString("SR04=OK(gpio%1/%2)").arg(sr04TrigGpio).arg(sr04EchoGpio);
    else
        parts << "SR04=UNAVAILABLE";

    if (initBuzzer())
        parts << QString("BUZZER=OK(%1,%2)").arg(buzzerNode,
                                                  buzzerActiveLowFlag ? "低电平响" : "高电平响");
    else if (buzzerOk)
        parts << QString("BUZZER=OK(gpio%1,%2)").arg(buzzerGpio)
                     .arg(buzzerActiveLowFlag ? "低电平响" : "高电平响");
    else
        parts << "BUZZER=UNAVAILABLE";

    if (initLed())
        parts << QString("LED=OK(%1)").arg(ledPath);
    else
        parts << "LED=UNAVAILABLE";

    if (initMq135())
        /* 这里只说"ADC 节点可读 + 换算标度"——节点存在不代表接了模块，
         * 接线状态由 mq135Sample() / mq135Connected() 实测后判定，别在这里写 OK 误导人 */
        parts << QString("MQ135=ADC节点可读(%1, %2mV/LSB%3%4)")
                     .arg(mq135Path)
                     .arg(mq135ScaleMv, 0, 'f', 4)
                     .arg(mq135ScaleFromSysfs ? "" : " 回退值!")
                     .arg(mq135DividerNum > 1 ? QString(", 分压x%1").arg(mq135DividerNum)
                                              : QString(", 未设分压"));
    else
        parts << "MQ135=UNAVAILABLE";

    info = parts.join(' ');
    qInfo().noquote() << "HAL:" << info;
}

Hal::~Hal()
{
    gpioUnexport(dht11Gpio);
    gpioUnexport(sr04TrigGpio);
    gpioUnexport(sr04EchoGpio);
    gpioUnexport(buzzerGpio);
}

// ---------------------------------------------------------------- GPIO sysfs

bool Hal::gpioExport(int gpio)
{
    const QString p = QString("/sys/class/gpio/gpio%1").arg(gpio);
    if (QFile::exists(p))
        return true;
    QFile e("/sys/class/gpio/export");
    if (!e.open(QIODevice::WriteOnly))
        return false;
    e.write(QString::number(gpio).toLatin1());
    e.close();
    for (int i = 0; i < 100; ++i) {
        if (QFile::exists(p))
            return true;
        usleep(2000);
    }
    return QFile::exists(p);
}

void Hal::gpioUnexport(int gpio)
{
    const QString p = QString("/sys/class/gpio/gpio%1").arg(gpio);
    if (!QFile::exists(p))
        return;
    QFile u("/sys/class/gpio/unexport");
    if (u.open(QIODevice::WriteOnly)) {
        u.write(QString::number(gpio).toLatin1());
        u.close();
    }
}

bool Hal::gpioSetPull(int gpio, const char *pull)
{
    // pull: "pullup" / "pulldown" / "none"。内核不支持时忽略。
    QFile f(QString("/sys/class/gpio/gpio%1/pull").arg(gpio));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(pull);
    f.close();
    return true;
}

bool Hal::gpioSetDirection(int gpio, bool out)
{
    QFile f(QString("/sys/class/gpio/gpio%1/direction").arg(gpio));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(out ? "out" : "in");
    f.close();
    return true;
}

bool Hal::gpioWrite(int gpio, int value)
{
    QFile f(QString("/sys/class/gpio/gpio%1/value").arg(gpio));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(value ? "1" : "0");
    f.close();
    return true;
}

int Hal::gpioRead(int gpio)
{
    QFile f(QString("/sys/class/gpio/gpio%1/value").arg(gpio));
    if (!f.open(QIODevice::ReadOnly))
        return -1;
    char c = 0;
    f.read(&c, 1);
    f.close();
    return (c == '1') ? 1 : 0;
}

// ---------------------------------------------------------------- 寄存器直读

bool Hal::gpioUseMmap(int gpio)
{
    for (int b = 0; b < kBankCount; ++b) {
        if (gpio >= kBank[b].first && gpio < kBank[b].first + 32) {
            const int idx = gpio - kBank[b].first;
            if (!g_regsReady[b]) {
                void *m = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED,
                               open("/dev/mem", O_RDWR | O_SYNC), kBank[b].base);
                if (m == MAP_FAILED)
                    return false;
                g_maps[b] = m;
                volatile unsigned int *base = (volatile unsigned int *)m;
                // RK3506：每 8 个引脚一组，0x00 数据 / 0x04 方向（gpio 组）
                g_regs[b].perReg = 8;
                g_regs[b].shift = idx % 8;
                g_regs[b].swport_dr = base + (idx / 8) * 2 + 0;
                g_regs[b].swport_ddr = base + (idx / 8) * 2 + 1;
                g_regsReady[b] = true;
            }
            return true;
        }
    }
    return false;
}

inline int Hal::gpioReadFast(int gpio) const
{
    for (int b = 0; b < kBankCount; ++b) {
        if (gpio >= kBank[b].first && gpio < kBank[b].first + 32) {
            if (!g_regsReady[b])
                return -1;
            return (*g_regs[b].swport_dr >> g_regs[b].shift) & 1;
        }
    }
    return -1;
}

// ---------------------------------------------------------------- 各外设初始化

bool Hal::initDht11()
{
    if (QFile::exists(dht11Node) && QFile(dht11Node).open(QIODevice::ReadOnly)) {
        dht11Ok = true;
        return true;
    }
    if (!gpioExport(dht11Gpio))
        return false;
    // 先使能内部上拉再切成输入，避免悬空时误判为低
    gpioSetPull(dht11Gpio, "pullup");
    if (!gpioSetDirection(dht11Gpio, false))
        return false;
    gpioUseMmap(dht11Gpio);
    usleep(2000);
    // 空闲时数据线应为高电平（外部 4.7k~10k 上拉 + 传感器）
    const int idle = gpioRead(dht11Gpio);
    if (idle != 1) {
        qWarning() << "DHT11 数据线(gpio" << dht11Gpio << ")空闲电平为" << idle
                   << "，请检查上拉电阻与接线（数据线应经 4.7k~10k 上拉到 3.3V）";
        // 仍然继续尝试读取
    }
    dht11SysfsOk = true;
    return false;
}

bool Hal::initSr04()
{
    if (QFile::exists(sr04Node) && QFile(sr04Node).open(QIODevice::ReadOnly)) {
        sr04Ok = true;
        return true;
    }
    if (!gpioExport(sr04TrigGpio) || !gpioExport(sr04EchoGpio))
        return false;
    gpioSetPull(sr04EchoGpio, "pullup");
    if (!gpioSetDirection(sr04TrigGpio, true) || !gpioSetDirection(sr04EchoGpio, false))
        return false;
    gpioWrite(sr04TrigGpio, 0);
    gpioUseMmap(sr04EchoGpio);
    sr04SysfsOk = true;
    return false;
}

bool Hal::initBuzzer()
{
    if (QFile::exists(buzzerNode)) {
        buzzerOk = true;
        // 上电先把蜂鸣器设为“不响”，避免开机一直叫
        int fd = ::open(buzzerNode.toLocal8Bit().constData(), O_WRONLY);
        if (fd >= 0) {
            unsigned char b[2] = {0, 0};
            ::write(fd, b, 2);
            ::close(fd);
        }
        return true;
    }
    if (!gpioExport(buzzerGpio))
        return false;
    if (!gpioSetDirection(buzzerGpio, true))
        return false;
    // 关键：上电立刻输出“静音电平”，否则蜂鸣器可能一通电就响
    gpioWrite(buzzerGpio, buzzerActiveLowFlag ? 1 : 0);
    buzzerOk = true;
    return false;
}

bool Hal::initLed()
{
    QFile f(ledPath);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write("0");
    f.close();
    ledOk = true;
    return true;
}

bool Hal::initMq135()
{
    QFile f(mq135Path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    f.close();
    mq135Ok = true;

    /*
     * 电压标度：IIO 的标准做法是读同目录的 in_voltage_scale（单位 mV/LSB）。
     * 本板实测 1.7578125 mV/LSB = 1.8V / 1024 —— 注意分母是 1024 不是 1023，
     * 所以"raw * 1.7578125mV"才是内核认可的电压；raw/1023*1.8 这种算法会偏。
     * 读不到就回退到 1800mV / 量程，并在 describe() 里标出来（不假装读到了）。
     */
    const QString scalePath = [this]() {
        const QString env = qEnvironmentVariable("SMARTCAR_MQ135_SCALE");
        if (!env.isEmpty())
            return env;
        const int slash = mq135Path.lastIndexOf('/');
        const QString dir = (slash > 0) ? mq135Path.left(slash)
                                        : QStringLiteral("/sys/bus/iio/devices/iio:device0");
        return dir + QStringLiteral("/in_voltage_scale");
    }();

    bool okScale = false;
    double scaleMv = qEnvironmentVariable("SMARTCAR_MQ135_SCALE").toDouble(&okScale);
    if (!okScale || scaleMv <= 0.0)
        scaleMv = 0.0;
    bool fromSysfs = false;
    if (scaleMv <= 0.0) {
        QFile s(scalePath);
        if (s.open(QIODevice::ReadOnly)) {
            const double v = QString::fromLatin1(s.readAll()).trimmed().toDouble(&okScale);
            s.close();
            if (okScale && v > 0.0) {
                scaleMv = v;
                fromSysfs = true;
            }
        }
    }
    if (scaleMv > 0.0) {
        mq135ScaleMv = scaleMv;
        mq135ScaleFromSysfs = fromSysfs;
    } else {
        mq135ScaleMv = 1800.0 / (double)mq135AdcMaxValue;   /* 1.8V 量程 / 码数 */
        mq135ScaleFromSysfs = false;
    }

    qInfo().noquote() << QString("MQ135: 电压标度 %1 mV/LSB（%2：%3），量程 %4，%5")
                             .arg(mq135ScaleMv, 0, 'f', 4)
                             .arg(fromSysfs ? "读自" : "回退")
                             .arg(fromSysfs ? scalePath
                                            : QStringLiteral("vref/量程 = 1800/%1")
                                                  .arg(mq135AdcMaxValue))
                             .arg(mq135AdcMaxValue)
                             .arg(mq135DividerNum > 1
                                      ? QString("外部分压比 x%1（AO 电压 = ADC 电压 x %1）")
                                            .arg(mq135DividerNum)
                                      : QString("未设分压（SMARTCAR_MQ135_DIVIDER=1），"
                                                "AO 直连或已在硬件上分好压"));
    return true;
}

/* ---------------------------------------------------------------- MQ-135 真实读数
 *
 * 这一节是"真驱动"的核心：把 /sys 里的 ADC 原始码值变成
 *   原始值 / ADC 引脚电压 / 换算回 AO 的电压 / 相对清洁空气基线的比值 / 状态。
 *
 * 为什么不再显示"空气质量 50%"：
 *   以前是 raw/1023*100 当成百分比，而悬空 ADC 恰好停在中点，于是没接模块
 *   也显示 ~50%，看着像真数据。raw/满量程 这个量本身也没有任何物理意义
 *   （MQ-135 的有效信息在 Rs/R0 的**比值**里，不在绝对码值里）。
 *
 * 为什么不直接给 PPM：
 *   MQ-135 手册给的是 Rs/R0 与浓度的对数曲线（不同气体不同系数），
 *   R0 必须在已知洁净空气里标定，而且手册自己写明要预热 24~48 小时。
 *   没标定就给 PPM 数字等于编数据，所以这里只给能站得住脚的量：
 *   电压、原始值、相对基线倍数；PPM 一律不显示。
 */
QList<int> Hal::mq135RawSamples(int n)
{
    QList<int> out;
    if (!mq135Ok || n <= 0)
        return out;
    if (n > 512)
        n = 512;

    /*
     * 每次读都重开节点。
     *
     * 为什么要重开：IIO/sysfs 节点是"普通文件"，每次 open+read 才触发一次新的
     * ADC 转换；而且仿真测试时把 SMARTCAR_MQ135_ADC 指到一个**普通文件**
     * （内容是多行 ADC 值）时，只有重开才会每次都从头读——留在同一个 fd 上
     * read 第二次就返回空，会误报"读取失败"（这是实测踩过的坑）。
     */
    int openFail = 0, emptyRead = 0, parseFail = 0;
    for (int i = 0; i < n; ++i) {
        QFile f(mq135Path);
        if (!f.open(QIODevice::ReadOnly)) {
            ++openFail;
            break;
        }
        bool ok = false;
        const QByteArray bytes = f.readAll();
        f.close();
        const QString txt = QString::fromLatin1(bytes).trimmed();
        if (txt.isEmpty())
            ++emptyRead;
        const int v = txt.toInt(&ok);
        if (ok) {
            out.append(v);
            mq135LastValue = v;
        } else {
            ++parseFail;
            if (parseFail <= 2) {
                qWarning().noquote()
                    << QString("MQ135: 解析失败 path=%1 bytes=%2 first=%3")
                           .arg(mq135Path).arg(bytes.size())
                           .arg(QString::fromLatin1(bytes.left(24)));
            }
        }
        /* 悬空脚在两次读之间会自己漂，这里的间隔不是为了"等采样"，
         * 而是让 sysfs 每次走到真正的 ADC 转换 */
        if (i + 1 < n)
            usleep(400);
    }
    if (out.size() < n) {
        qWarning().noquote()
            << QString("MQ135: 采样不完整 path=%1 得到 %2/%3 个（open失败 %4，空读 %5，解析失败 %6）")
                   .arg(mq135Path).arg(out.size()).arg(n).arg(openFail).arg(emptyRead).arg(parseFail);
    }
    return out;
}

Hal::Mq135Sample Hal::mq135Sample()
{
    Mq135Sample s;

    if (!mq135Ok) {
        s.statusCn = QStringLiteral("ADC节点不可用");
        s.detail = QStringLiteral("打开 %1 失败（设备树里没有这个 ADC 通道？）").arg(mq135Path);
        return s;
    }

    /* 20 个样本 x 400us ≈ 8ms：对 2 秒一次的 UI 刷新毫无压力，
     * 但足够看出这根线是"有人驱动的带噪声信号"还是"悬空漂移" */
    const QList<int> v = mq135RawSamples(20);
    if (v.isEmpty()) {
        s.statusCn = QStringLiteral("读取失败");
        s.detail = QStringLiteral("从 %1 读不到合法整数").arg(mq135Path);
        return s;
    }

    s.ok = true;
    s.raw = v.last();                          /* 报最新一次读数 */
    s.vAdc = (double)s.raw * mq135ScaleMv / 1000.0;
    s.vAo = s.vAdc * (double)mq135DividerNum;

    int lo = v.first(), hi = v.first();
    double sum = 0.0;
    for (int x : v) {
        lo = qMin(lo, x);
        hi = qMax(hi, x);
        sum += x;
    }
    const double mean = sum / v.size();
    const int span = hi - lo;

    /* 相邻差值的符号一致性：悬空脚漏电漂移是单调的，真实传感器是随机的 */
    int sameSign = 0, usable = 0, maxStep = 0;
    for (int i = 1; i < v.size(); ++i) {
        const int d = v[i] - v[i - 1];
        const int ad = qAbs(d);
        if (ad > maxStep)
            maxStep = ad;
        if (ad <= 1) {
            ++usable;
            if (d != 0 && (d > 0) == (v[1] >= v[0]))
                ++sameSign;
        }
    }
    const bool monotonicDrift = (span <= 8) && (usable >= 12)
                                && (sameSign * 100 >= usable * 70);

    /*
     * "一直静止"判据：单看一个 20 样本窗口，"死平"和"真实但极度稳定的信号"
     * 是分不开的；但连续几个窗口（每个窗口 2 秒）的均值都一模一样，就只能是
     * 一个被固定住的电平，而不是气体传感器。
     * 反过来，真实传感器即使在洁净空气里，窗口均值也会有几码的缓慢移动。
     */
    mq135WindowMeans.append((int)(mean + 0.5));
    while (mq135WindowMeans.size() > 6)
        mq135WindowMeans.removeFirst();
    int wLo = mq135WindowMeans.first(), wHi = mq135WindowMeans.first();
    for (int w : mq135WindowMeans) {
        wLo = qMin(wLo, w);
        wHi = qMax(wHi, w);
    }
    const bool windowFilled = (mq135WindowMeans.size() >= 4);
    const bool deadStill = windowFilled && ((wHi - wLo) <= qMax(1, mq135StableSpan / 2));

    const int lo5 = mq135AdcMaxValue / 20;              /* 量程 5% */
    const int hi5 = mq135AdcMaxValue - lo5;             /* 量程 95% */
    const bool stuckAtRail = (span <= mq135StableSpan) && (mean <= lo5 || mean >= hi5);
    const bool deadFlat = (span <= mq135StableSpan) && !stuckAtRail;

    if (mq135AssumeConnected) {
        s.connected = true;
        s.detail = QStringLiteral("SMARTCAR_MQ135_ASSUME_CONNECTED=1，无条件当作已接");
    } else if (monotonicDrift) {
        s.connected = false;
        s.detail = QStringLiteral("读数单调缓慢漂移（跨度%1码/最大步进%2码/%3%%同号），"
                                 "典型悬空脚漏电充放电，不是传感器驱动")
                       .arg(span).arg(maxStep).arg(usable ? sameSign * 100 / usable : 0);
    } else if (deadStill) {
        s.connected = false;
        s.detail = QStringLiteral("连续 %1 个采样窗口的均值都不动（%2~%3），"
                                 "被固定住的电平，不是气体传感器在动")
                       .arg(mq135WindowMeans.size()).arg(wLo).arg(wHi);
    } else if (deadFlat) {
        s.connected = false;
        s.detail = QStringLiteral("读数死平且停在量程中段（均值%1/量程%2，极差%3），"
                                 "没有东西在驱动这根线")
                       .arg(mean, 0, 'f', 0).arg(mq135AdcMaxValue).arg(span);
    } else if (stuckAtRail) {
        s.connected = false;
        s.detail = QStringLiteral("读数贴在量程边缘不动（均值%1，极差%2），被拉死到 0 或满量程")
                       .arg(mean, 0, 'f', 0).arg(span);
    } else {
        s.connected = true;
        s.detail = QStringLiteral("有真实随机噪声（极差%1码，均值%2），判定为传感器在驱动")
                       .arg(span).arg(mean, 0, 'f', 0);
    }

    mq135ConnectedCache = s.connected;
    mq135History.append(v.last());
    while (mq135History.size() > 24)
        mq135History.removeFirst();

    /*
     * 基线：优先手工指定（环境变量或上次存下来的），否则用刚判定为"已接"
     * 之后的读数累进平均 —— 对应"上电后在洁净空气里静置"这个真实标定动作。
     * 注意：如果此刻环境本身就不洁净，这个基线会被抬高，之后的报警就要滞后；
     * 所以标定必须在洁净空气里做，或者用 SMARTCAR_MQ135_BASELINE 手工指定。
     *
     * mq135BaselineRaw > 0 说明这个基线是"人给的"或"上次存盘留下的"，
     * 此时只做 2% 的缓慢跟踪，不重新建立；
     * mq135BaselineSaved 记"本次会话是否已落过盘"，保证从文件读回的基线
     * 也能在开机第一次采样时写回一次（只写一次，护 flash）——
     * 以前这里只看 mq135BaselineRaw > 0 就**跳过整个 else 分支**，
     * 于是从文件读回来的基线永远不会被更新，每次重启都退化成"重新标定"，
     * 标定持久化等于没生效。
     */
    if (s.connected) {
        const bool firstTime = (mq135BaselineRaw <= 0.0);
        if (firstTime) {
            mq135BaselineRaw = mean;
            qInfo().noquote()
                << QString("MQ135: 建立清洁空气基线 raw=%1（%2mV）——"
                           "若此刻环境并不洁净，请用 SMARTCAR_MQ135_BASELINE 手工指定")
                       .arg(mean, 0, 'f', 1).arg(s.vAdc * 1000.0, 0, 'f', 0);
        } else {
            /* 2% 的缓慢跟踪：既跟得上传感器老化/温漂，又不会被几分钟的
             * 一次污染（比如对着它呼气）带偏。
             *
             * 但 ASSUME_CONNECTED=1 时 s.connected 是被无条件置真的，
             * 而这根线静置时死平在 raw 8~9 —— 跟踪照跑的话基线会被一路拖向 8
             * （实测：会话内 651.4 → 135.9，且会继续掉），比值随之虚高几十倍，
             * 于是屏上那个数只在开机头几分钟可信。
             * 所以这里加一道门：只有读数**明显高于**当前基线（≥ GATE%）才认为
             * 环境真的变了、允许跟踪；其余情况（含读数低于基线）一律冻结基线。
             * 用乘法/整数比较，避免门限等于基线时"把自己锁住"。 */
            const int gatePct = 300;   /* 只有 raw ≥ 基线 × 3 才跟踪（本板静置 8~9 vs 基线 651 ⇒ 永不跟踪） */
            const bool farAboveNoise =
                ((double)s.raw * 100.0) >= (mq135BaselineRaw * (double)gatePct);
            if (farAboveNoise) {
                mq135BaselineRaw = mq135BaselineRaw * 0.98 + (double)s.raw * 0.02;
            } else if (!mq135BaselineFrozenLogged) {
                /* 只打印一次，不刷日志 */
                mq135BaselineFrozenLogged = true;
                qInfo().noquote()
                    << QString("MQ135: 基线冻结在 raw=%1（读数 %2 未达基线的 %3%%，"
                               "判定为静置/低于基线，不做 2%% 跟踪以防比值虚高）")
                           .arg(mq135BaselineRaw, 0, 'f', 1).arg(s.raw).arg(gatePct);
            }
        }
        /* 每次开机至少把当前基线落一次盘，下次启动直接读回来 ——
         * 只写一次，避免每 2 秒写 flash */
        if (firstTime || !mq135BaselineSaved) {
            mq135BaselineSaved = true;
            if (!mq135BaselineFile.isEmpty()) {
                QFile bf(mq135BaselineFile);
                if (bf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    bf.write(QByteArray::number(mq135BaselineRaw, 'f', 1) + '\n');
                    bf.close();
                    qInfo().noquote() << QString("MQ135: 基线已存入 %1（下次启动直接读回）")
                                             .arg(mq135BaselineFile);
                }
            }
        }
    }
    s.baselineRaw = mq135BaselineRaw;
    s.ratio = mq135BaselineRaw > 0.0 ? (double)s.raw / mq135BaselineRaw : 0.0;

    /* 无明确证据时先不报"已接"：窗口还没攒够、或历史上一直是死值。
     *
     * 但 SMARTCAR_MQ135_ASSUME_CONNECTED=1 的语义就是"无条件当作已接"
     * （见 hal.h 的说明与 hal.cpp 上方那句 detail 文案：不信启发式判定时用），
     * 所以该开关打开时**不得**再用窗口启发式把连接状态打回"未接" ——
     * 实测（MCU 侧 B39 静置死平在 raw 8~9）这里每次都命中 deadStill，
     * 导致静置时不呼气界面一直显示"未接 --"，只有呼气（数值动了）才出数。 */
    if (s.connected && !mq135AssumeConnected && (!windowFilled || deadStill)) {
        s.connected = false;
        s.statusCn = QStringLiteral("未接");
        s.detail = QStringLiteral("判定证据不足（%1 个采样窗口）：%2")
                       .arg(mq135WindowMeans.size()).arg(s.detail);
        return s;
    }

    const qint64 uptimeMin = mq135Uptime.isValid() ? mq135Uptime.elapsed() / 60000 : 0;

    if (!s.connected) {
        s.statusCn = QStringLiteral("未接");
    } else if (uptimeMin < mq135WarmupMin) {
        /* MQ-135 手册：预热 24~48h 才稳定。预热期读数照给，但明确标出来 */
        s.statusCn = QStringLiteral("预热中");
    } else if (s.ratio > 0.0 && s.ratio >= mq135AlarmRatio) {
        s.statusCn = QStringLiteral("超阈值");
        s.alarm = true;
    } else if (s.ratio > 0.0 && s.ratio >= 1.15) {
        s.statusCn = QStringLiteral("偏高");
    } else {
        s.statusCn = QStringLiteral("正常");
    }

    return s;
}

// ---------------------------------------------------------------- DHT11

bool Hal::dht11Read(unsigned char *buf, int bytes)
{
    if (bytes < 4 || buf == nullptr)
        return false;

    // 内核模块优先：内核里做时序，最稳
    if (dht11Ok) {
        int fd = ::open(dht11Node.toLocal8Bit().constData(), O_RDONLY);
        if (fd >= 0) {
            const int n = ::read(fd, buf, 4);
            ::close(fd);
            if (n == 4)
                return true;
        }
        return false;
    }

    if (!dht11SysfsOk)
        return false;

    // 用户态时序实测 p99 抖动只有 0.58us，但偶发（约 0.005%）会有 >30us 的调度停顿。
    // 这里做最多 3 次重试，把偶发失败压到可以忽略。
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (dht11ReadOnce(buf))
            return true;
        usleep(1500);
    }
    return false;
}

bool Hal::dht11ReadOnce(unsigned char *buf)
{
    const int gpio = dht11Gpio;

    // 1) 主机起始信号：拉低 >=18ms，再拉高 20~40us
    if (!gpioSetDirection(gpio, true))
        return false;
    gpioWrite(gpio, 0);
    usleep(20000);
    if (!gpioSetDirection(gpio, false))
        return false;
    // 释放总线（上拉拉高）

    // 2) 等待从机响应：低 80us -> 高 80us
    const long long t0 = nowNs();
    auto waitLevel = [&](int level, long long timeoutUs) -> long long {
        const long long deadline = nowNs() + timeoutUs * 1000LL;
        while (nowNs() < deadline) {
            const int v = gpioReadFast(gpio);
            if (v < 0 ? gpioRead(gpio) == level : v == level)
                return nowNs();
        }
        return -1;
    };

    if (waitLevel(0, 500) < 0)
        return false; // 无响应
    if (waitLevel(1, 500) < 0)
        return false; // 响应低电平超时
    if (waitLevel(0, 500) < 0)
        return false; // 响应高电平超时

    // 3) 读 40 bit
    unsigned char data[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < 40; ++i) {
        // 低电平结束
        if (waitLevel(1, 500) < 0)
            return false;
        const long long rise = nowNs();
        // 高电平结束
        if (waitLevel(0, 500) < 0)
            return false;
        const long long fall = nowNs();
        const long long highUs = (fall - rise) / 1000;
        const int bit = (highUs > 45) ? 1 : 0;
        data[i / 8] = (unsigned char)((data[i / 8] << 1) | bit);
    }

    const unsigned char sum = (unsigned char)(data[0] + data[1] + data[2] + data[3]);
    if (sum != data[4]) {
        Q_UNUSED(t0);
        return false; // 校验失败
    }

    buf[0] = data[0]; // 湿度整数
    buf[1] = data[1]; // 湿度小数
    buf[2] = data[2]; // 温度整数
    buf[3] = data[3]; // 温度小数
    return true;
}

// ---------------------------------------------------------------- SR04

bool Hal::sr04ReadCm(double *cm)
{
    if (!cm)
        return false;

    if (sr04Ok) {
        int fd = ::open(sr04Node.toLocal8Bit().constData(), O_RDONLY);
        if (fd >= 0) {
            int us = 0;
            const int n = ::read(fd, &us, sizeof(us));
            ::close(fd);
            if (n == (int)sizeof(us) && us > 0) {
                *cm = us / 58.0; // 声速 340m/s：距离(cm) = 回波时间(us) / 58
                return true;
            }
        }
        return false;
    }

    if (!sr04SysfsOk)
        return false;

    const int trig = sr04TrigGpio;
    const int echo = sr04EchoGpio;

    // 发 10us 触发脉冲
    gpioWrite(trig, 0);
    usleep(3);
    gpioWrite(trig, 1);
    usleep(12);
    gpioWrite(trig, 0);

    // 等待回波高电平
    const long long deadline = nowNs() + 60000LL * 1000; // 60ms 超时
    while (nowNs() < deadline) {
        const int v = gpioReadFast(echo);
        if (v < 0 ? gpioRead(echo) == 1 : v == 1)
            break;
        if (nowNs() > deadline)
            return false;
    }
    const long long rise = nowNs();
    const long long d2 = rise + 60000LL * 1000;
    while (nowNs() < d2) {
        const int v = gpioReadFast(echo);
        if (v < 0 ? gpioRead(echo) == 0 : v == 0)
            break;
        if (nowNs() > d2)
            return false;
    }
    const long long fall = nowNs();
    const long long us = (fall - rise) / 1000;
    if (us <= 0 || us > 38000)
        return false;
    *cm = us / 58.0;
    return true;
}

// ---------------------------------------------------------------- 蜂鸣器 / LED / ADC

void Hal::buzzerSet(bool on)
{
    if (buzzerOk) {
        int fd = ::open(buzzerNode.toLocal8Bit().constData(), O_WRONLY);
        if (fd >= 0) {
            unsigned char b[2] = {0, 0};
            b[1] = on ? 1 : 0;
            ::write(fd, b, 2);
            ::close(fd);
        }
        return;
    }
    const int level = buzzerActiveLowFlag ? (on ? 0 : 1) : (on ? 1 : 0);
    gpioWrite(buzzerGpio, level);
}

void Hal::ledSet(bool on)
{
    if (!ledOk)
        return;
    QFile f(ledPath);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(on ? "1" : "0");
    f.close();
}

bool Hal::mq135ReadRaw(int *raw)
{
    if (!raw || !mq135Ok)
        return false;
    QFile f(mq135Path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray v = f.readAll().trimmed();
    f.close();
    bool ok = false;
    const int x = v.toInt(&ok);
    if (!ok)
        return false;
    *raw = x;
    mq135LastValue = x;
    return true;
}

/*
 * MQ-135 到底接了没有 —— 见 hal.h 里的详细说明。
 *
 * 关键点：ADC 的 sysfs 节点任何板子上都在，所以"能打开"≠"接了传感器"。
 * 悬空的 ADC 输入会停在量程中段并且几乎不动（本板实测 507~510 / 1023，
 * 另一轮实测是 657~658 一直死平）。
 * 真实气体传感器输出会随空气缓慢漂移，不会长时间死平。
 */
int Hal::mq135AlarmRaw() const
{
    bool ok = false;
    int pct = qEnvironmentVariableIntValue("SMARTCAR_MQ135_ALARM_PCT", &ok);
    if (!ok || pct <= 0 || pct > 100)
        pct = 90;
    return mq135AdcMaxValue * pct / 100;
}

bool Hal::mq135Connected()
{
    if (!mq135Ok)
        return false;
    /* 判定全部收在 mq135Sample() 里（同一套波形判据），这里只做节流：
     * UI 每 2 秒调一次，没必要每次都抓 20 个样本。
     * 注意判定要攒够 4 个采样窗口（UI 侧 2 秒一个 ⇒ 约 8 秒）才成立，
     * 所以刚启动那几秒会报"未接"，这是**故意**的：宁可先说"还没测出来"，
     * 也不显示一个来路不明的数。 */
    if (mq135DecideInit && mq135LastDecide.isValid()
        && mq135LastDecide.elapsed() < 3000)
        return mq135ConnectedCache;

    mq135LastDecide.start();
    mq135DecideInit = true;

    const Mq135Sample s = mq135Sample();
    if (!s.ok)
        return false;

    mq135ConnectedCache = s.connected;
    if (!s.connected)
        qWarning().noquote() << QString("MQ135: 判定为未接（%1）").arg(s.detail);
    else
        qInfo().noquote() << QString("MQ135: 判定为已接（%1）").arg(s.detail);

    return mq135ConnectedCache;
}

// ---------------------------------------------------------------- 系统健康监控

// 伪文件读取（/proc、/sys）统一用 POSIX，见下方定义
static bool readFirstLine(const QString &path, QString *out);
static bool readPseudoFile(const QString &path, QString *out);

// 找温度传感器：优先环境变量，其次 /sys/class/thermal 里第一个有效的
static QString findTempPath()
{
    const QString env = qEnvironmentVariable("SMARTCAR_TEMP_PATH");
    if (!env.isEmpty())
        return env;
    QDir thermal("/sys/class/thermal");
    const QStringList zones = thermal.entryList(QStringList() << "thermal_zone*", QDir::Dirs);
    for (const QString &z : zones) {
        const QString t = "/sys/class/thermal/" + z + "/temp";
        QString v;
        if (readFirstLine(t, &v) && v.toInt() > 0)
            return t;
    }
    // 退路：rockchip 的 svs / adc 温度节点
    const QStringList fallback = {
        "/sys/class/thermal/thermal_zone0/temp",
        "/sys/devices/virtual/thermal/thermal_zone0/temp",
    };
    for (const QString &t : fallback) {
        if (QFile::exists(t))
            return t;
    }
    return QString();
}

static bool readFirstLine(const QString &path, QString *out)
{
    // 注意：不能用 QFile 读 /proc 下的伪文件 —— 它们 stat 出来的 size 是 0，
    // QFile 会认为文件是空的，atEnd() 直接返回 true，一行都读不到。
    // 所以这里统一用 POSIX read()。
    const int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY);
    if (fd < 0)
        return false;
    char buf[512];
    const ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);
    if (n <= 0)
        return false;
    buf[n] = '\0';
    QString s = QString::fromLatin1(buf);
    const int nl = s.indexOf('\n');
    if (nl >= 0)
        s = s.left(nl);
    *out = s.trimmed();
    return true;
}

// 把整个伪文件读成字符串（同样避开 QFile 的 size=0 问题）
static bool readPseudoFile(const QString &path, QString *out)
{
    const int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY);
    if (fd < 0)
        return false;
    QByteArray all;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0)
        all.append(buf, (int)n);
    ::close(fd);
    *out = QString::fromLatin1(all);
    return true;
}

bool Hal::sysInfo(SysInfo *out)
{
    if (!out)
        return false;
    SysInfo &s = *out;
    s.valid = false;

    // ---- CPU ----
    {
        QString stat;
        if (readPseudoFile("/proc/stat", &stat)) {
            const QString first = stat.section('\n', 0, 0);   // "cpu  user nice system idle ..."
            const QStringList p = first.split(' ', QString::SkipEmptyParts);
            if (p.size() >= 8 && p.at(0) == "cpu") {
                const long long user = p.at(1).toLongLong();
                const long long nice = p.at(2).toLongLong();
                const long long sys = p.at(3).toLongLong();
                const long long idle = p.at(4).toLongLong();
                const long long iowait = p.at(5).toLongLong();
                const long long irq = p.at(6).toLongLong();
                const long long softirq = p.at(7).toLongLong();
                const long long steal = p.size() > 8 ? p.at(8).toLongLong() : 0;

                const long long idleAll = idle + iowait;
                const long long total = user + nice + sys + idle + iowait + irq + softirq + steal;

                if (cpuPrevTotal > 0 && total > cpuPrevTotal) {
                    const long long dTotal = total - cpuPrevTotal;
                    const long long dIdle = idleAll - cpuPrevIdle;
                    const long long dUser = (user + nice) - cpuPrevUser;
                    const long long dSys = (sys + irq + softirq + steal) - cpuPrevSys;
                    if (dTotal > 0) {
                        s.cpuPercent = 100.0 * (double)(dTotal - dIdle) / (double)dTotal;
                        if (s.cpuPercent < 0) s.cpuPercent = 0;
                        if (s.cpuPercent > 100) s.cpuPercent = 100;
                        s.cpuUser = 100.0 * (double)dUser / (double)dTotal;
                        s.cpuSys = 100.0 * (double)dSys / (double)dTotal;
                        s.cpuIdle = 100.0 * (double)dIdle / (double)dTotal;
                    }
                }
                cpuPrevTotal = total;
                cpuPrevIdle = idleAll;
                cpuPrevUser = user + nice;
                cpuPrevSys = sys + irq + softirq + steal;
                s.valid = true;
            }
        }
    }

    // ---- 负载 / 运行时间 / 进程数 ----
    {
        QString l;
        if (readFirstLine("/proc/loadavg", &l)) {
            const QStringList p = l.split(' ', QString::SkipEmptyParts);
            if (p.size() >= 3) {
                s.load1 = p.at(0).toDouble();
                s.load5 = p.at(1).toDouble();
                s.load15 = p.at(2).toDouble();
            }
        }
        if (readFirstLine("/proc/uptime", &l)) {
            s.uptimeSec = (long)l.split(' ').first().toDouble();
        }
        QDir proc("/proc");
        int cnt = 0;
        const QStringList entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &e : entries) {
            bool ok = false;
            e.toInt(&ok);
            if (ok)
                cnt++;
        }
        s.procCount = cnt;
    }

    // ---- 内存 ----
    {
        QString mi;
        if (readPseudoFile("/proc/meminfo", &mi)) {
            double memTotal = 0, memAvail = 0, memFree = 0, buffers = 0, cached = 0, shmem = 0;
            double swapTotal = 0, swapFree = 0;
            const QStringList lines = mi.split('\n', QString::SkipEmptyParts);
            for (const QString &line : lines) {
                const QStringList p = line.split(' ', QString::SkipEmptyParts);
                if (p.size() < 2)
                    continue;
                const double kb = p.at(1).toDouble();
                const QString k = p.at(0);
                if (k == "MemTotal:")
                    memTotal = kb;
                else if (k == "MemAvailable:")
                    memAvail = kb;
                else if (k == "MemFree:")
                    memFree = kb;
                else if (k == "Buffers:")
                    buffers = kb;
                else if (k == "Cached:")
                    cached = kb;
                else if (k == "Shmem:")
                    shmem = kb;
                else if (k == "SwapTotal:")
                    swapTotal = kb;
                else if (k == "SwapFree:")
                    swapFree = kb;
            }
            if (memTotal > 0) {
                // 有 MemAvailable 就用它（更准）；没有就按 free+buffers+cached 估算
                double used = (memAvail > 0)
                                  ? (memTotal - memAvail)
                                  : (memTotal - memFree - buffers - cached + shmem);
                if (used < 0)
                    used = 0;
                s.memTotalMb = memTotal / 1024.0;
                s.memUsedMb = used / 1024.0;
                s.memPercent = 100.0 * used / memTotal;
                s.swapTotalMb = swapTotal / 1024.0;
                s.swapUsedMb = (swapTotal - swapFree) / 1024.0;
                s.valid = true;
            }
        }
    }

    // ---- 温度 ----
    {
        if (tempPath.isEmpty())
            tempPath = findTempPath();
        if (!tempPath.isEmpty()) {
            const int fd = ::open(tempPath.toLocal8Bit().constData(), O_RDONLY);
            if (fd >= 0) {
                char buf[64];
                const ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
                ::close(fd);
                if (n > 0) {
                    buf[n] = '\0';
                    const int raw = QString::fromLatin1(buf).trimmed().toInt();
                    if (raw > 0)
                        s.tempC = raw / 1000.0;   // 毫摄氏度 -> 摄氏度
                }
            }
        }
    }

    // ---- 磁盘（默认看 /userdata，也就是应用和素材所在的可写分区）----
    {
        struct statvfs vfs;
        if (::statvfs(diskPath.toLocal8Bit().constData(), &vfs) == 0) {
            const double total = (double)vfs.f_blocks * vfs.f_frsize;
            const double avail = (double)vfs.f_bavail * vfs.f_frsize;
            s.diskTotalMb = total / 1048576.0;
            s.diskUsedMb = (total - avail) / 1048576.0;
            s.diskPercent = total > 0 ? 100.0 * (total - avail) / total : 0;
            s.valid = true;
        }
        if (::statvfs("/", &vfs) == 0) {
            const double total = (double)vfs.f_blocks * vfs.f_frsize;
            const double avail = (double)vfs.f_bavail * vfs.f_frsize;
            s.rootTotalMb = total / 1048576.0;
            s.rootUsedMb = (total - avail) / 1048576.0;
            s.rootPercent = total > 0 ? 100.0 * (total - avail) / total : 0;
        }
    }

    // ---- CPU 核数 ----
    {
        QString ci;
        if (readPseudoFile("/proc/cpuinfo", &ci)) {
            int n = 0;
            const QStringList lines = ci.split('\n');
            for (const QString &line : lines) {
                if (line.startsWith("processor"))
                    n++;
            }
            s.cpuCount = n > 0 ? n : 1;
        }
    }

    *out = s;
    return s.valid;
}
