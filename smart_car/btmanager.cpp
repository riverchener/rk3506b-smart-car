#include "btmanager.h"

#include <QDebug>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

namespace {
const char *kBluetoothctl = "bluetoothctl";

/*
 * 配对 agent 的能力声明。**这块板子没有屏幕、没有键盘**，所以只能声明
 * NoInputNoOutput（= 无输入无输出）。
 *
 * 为什么不能用 KeyboardDisplay：
 * KeyboardDisplay 相当于告诉手机"我能显示 6 位通行密钥，也有键盘能输入"。
 * 安卓手机本身也是 KeyboardDisplay，两边一配对，协商出来的方式就是
 * **Passkey Entry（通行密钥输入）** —— 需要有人把板子上的 6 位数字念出来、
 * 再敲进手机（或者反过来）。板子既显示不了也敲不了，这一步必然失败，
 * 手机上看到的就是"PIN 码或通行密钥不正确"。
 *
 * 用 NoInputNoOutput，协商结果是 **Just Works**：免交互，直接配对完成。
 * 蓝牙音箱、耳机全都是这么做的。
 *
 * 注：早期怀疑"NoInputNoOutput 导致 PIN 码报错"是误判，真正的原因是
 * ① 蓝牙页面每 3 秒注册/注销一次 agent（配对进行到一半被打断）；
 * ② 可达性 180 秒自动超时关闭，手机根本搜不到；
 * ③ 手机上残留旧配对记录、板子侧却没有对应 link key。
 */
const char *kAgentCapability = "NoInputNoOutput";

/*
 * 这个板子上的 BlueZ 被改过：org.bluez.Device1 的 Connect / Disconnect
 * **多了一个字符串参数**（ADDR_TYPE），原生 BlueZ 是不带参数的。
 *
 * 麻烦就在这里：bluetoothctl 是按"无参数"去调 Connect 的，于是**永远失败**：
 *   Failed to connect: org.freedesktop.DBus.Error.UnknownMethod
 *   Method "Connect" with signature "" on interface "org.bluez.Device1" doesn't exist
 * 反过来 `bluetoothctl pair` 能用，是因为 Device1.Pair 确实没有参数。
 *
 * 所以连接/断开**不能**走 bluetoothctl，必须直接 dbus-send，
 * 并且把那个参数补上（实测传空串即可）：
 *   dbus-send --system --print-reply --dest=org.bluez \
 *     /org/bluez/hci0/dev_XX_XX_.. org.bluez.Device1.Connect string:
 *
 * 注意 dbus-send 里 `string:` 后面就是空串（命令行上的 "" 会被 shell 吃掉）。
 */
QString deviceDbusPath(const QString &address)
{
    /* /org/bluez/hci0/dev_FC_5B_8C_F7_D7_B3 —— 大写、冒号换下划线 */
    return QStringLiteral("/org/bluez/hci0/dev_")
           + QString(address).replace(QLatin1Char(':'), QLatin1Char('_')).toUpper();
}

/* bluetoothctl 的输出里可能有 ANSI 颜色转义，删掉再解析 */
QString stripAnsi(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("\x1b\\[[0-9;]*[A-Za-z]"));
    QString out = s;
    out.remove(re);
    return out;
}
}

BtManager::BtManager(QObject *parent)
    : QObject(parent)
{
}

QVector<BtDevice> BtManager::connectedDevices() const
{
    QVector<BtDevice> v;
    for (const BtDevice &d : m_devices) {
        if (d.connected)
            v.append(d);
    }
    return v;
}

/*
 * 跑一条 bluetoothctl 命令。tag 用来在回调里区分是谁发起的。
 * 注意：bluetoothctl 有些命令（scan on）不会自己退出，所以带 timeout 的调用
 * 我们用 `bluetoothctl --timeout N <cmd>`（BlueZ 5.5x 支持）来保证会返回；
 * 不支持 --timeout 的版本会立刻报错，这时退回普通调用。
 */
void BtManager::run(const QStringList &args, const QString &tag, int timeoutMs)
{
    /*
     * 用**队列**而不是"忙就丢弃"。
     *
     * 早期版本在 m_proc 未结束时直接 return 把命令丢掉，结果像
     * "先 pairable on 再 discoverable on" 这种连续操作，第二条被静默丢弃：
     * 实测 Pairable 变成 yes 了、Discoverable 还是 no，用户就觉得"切不动/连不上"。
     * 现在排队，前一条结束自动跑下一条。
     */
    PendingCmd cmd;
    cmd.args = args;
    cmd.tag = tag;
    cmd.timeoutMs = timeoutMs;
    m_queue.append(cmd);
    if (!m_proc)
        startNext();
}

/* 同上，但走 dbus-send（见 deviceDbusPath 的说明） */
void BtManager::runDbus(const QStringList &args, const QString &tag, int timeoutMs)
{
    PendingCmd cmd;
    cmd.args = args;
    cmd.tag = tag;
    cmd.timeoutMs = timeoutMs;
    cmd.useDbus = true;
    m_queue.append(cmd);
    if (!m_proc)
        startNext();
}

void BtManager::startNext()
{
    if (m_proc || m_queue.isEmpty())
        return;

    const PendingCmd c = m_queue.takeFirst();
    m_tag = c.tag;
    m_busy = true;
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);

    QStringList real = c.args;
    if (!c.useDbus) {
        if (c.timeoutMs > 0) {
            /* bluetoothctl --timeout <秒> <命令>：让 scan on 这类不会自己退出的命令
             * 过一会儿自动结束，否则 QProcess 永远不 finished、界面会卡在"忙" */
            real.prepend(QString::number(c.timeoutMs / 1000));
            real.prepend(QString::fromLatin1("--timeout"));
        }
        /*
         * **只有需要配对交互的命令才带 agent。**
         *
         * 踩过的坑：原来所有命令都带 --agent，而每次启动 bluetoothctl 都会向 BlueZ
         * 注册一个 pairing agent、进程结束再注销。界面轮询每 3 秒跑一次 show/devices，
         * 就等于每 3 秒注册+注销一次 agent —— 手机正在配对/连接时被这种打扰，
         * 连接会被打断（内核日志表现为我方发起断开，dir 1）。
         *
         * 只读命令（show / devices / info）根本不需要 agent，去掉。
         * 需要交互的（pair / connect / scan）才带。
         */
        const bool needsAgent = c.tag == QLatin1String("pair");
        if (needsAgent) {
            /*
             * capability 必须是 NoInputNoOutput（见文件开头 kAgentCapability 的说明）：
             * 板子无屏无键盘，声明成 KeyboardDisplay 会协商出 Passkey Entry，
             * 手机就会报"PIN 码或通行密钥不正确"。NoInputNoOutput 走 Just Works。
             */
            real.prepend(QString::fromLatin1(kAgentCapability));
            real.prepend(QString::fromLatin1("--agent"));
        }
    }

    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &BtManager::onProcessFinished);

    const QString program = c.useDbus ? QStringLiteral("dbus-send")
                                      : QString::fromLatin1(kBluetoothctl);
    qInfo().noquote() << "bt: 执行" << program << real.join(' ')
                      << (m_queue.isEmpty() ? QString()
                                            : QString("(队列剩 %1)").arg(m_queue.size()));
    m_proc->start(program, real);

    /* 兜底：防止 bluetoothctl 卡死导致队列永远堵住 */
    if (!m_timeout) {
        m_timeout = new QTimer(this);
        m_timeout->setSingleShot(true);
        connect(m_timeout, &QTimer::timeout, this, [this]() {
            if (m_proc) {
                qWarning() << "bt: 命令超时，强杀 bluetoothctl";
                m_proc->kill();
            }
        });
    }
    m_timeout->start(c.timeoutMs > 0 ? (c.timeoutMs + 5000) : 15000);
}

void BtManager::onProcessFinished()
{
    if (!m_proc)
        return;
    if (m_timeout)
        m_timeout->stop();

    /* 记住还有没有排队命令；处理完这条后要接着跑 */
    const bool moreQueued = !m_queue.isEmpty();

    /* 队列里还有命令的话，本条处理完就接着跑下一条（各处 return 前都会调到）。 */
    struct QueueStep {
        BtManager *self;
        bool more;
        ~QueueStep() { if (more) self->startNext(); }
    } queueStep{ this, moreQueued };

    const QString raw = stripAnsi(QString::fromUtf8(m_proc->readAll()));
    const QString tag = m_tag;
    const int code = m_proc->exitCode();
    m_proc->deleteLater();
    m_proc = nullptr;
    m_busy = false;

    qInfo().noquote() << QString("bt: [%1] rc=%2 输出=%3")
                             .arg(tag).arg(code)
                             .arg(raw.trimmed().left(600));

    if (tag == QLatin1String("show")) {
        /* Controller 信息：Powered / Name / Discoverable / Pairable */
        const QRegularExpression rePower(QStringLiteral("Powered:\\s*(yes|no)"),
                                         QRegularExpression::CaseInsensitiveOption);
        const auto m = rePower.match(raw);
        if (m.hasMatch()) {
            m_powered = (m.captured(1).toLower() == QLatin1String("yes"));
            m_adapterPresent = true;
        } else if (raw.contains(QLatin1String("No default controller"),
                                Qt::CaseInsensitive)) {
            m_adapterPresent = false;
            m_powered = false;
        }
        const QRegularExpression reName(QStringLiteral("^\\s*Name:\\s*(.+)$"),
                                        QRegularExpression::MultilineOption);
        const auto mn = reName.match(raw);
        if (mn.hasMatch())
            m_adapterName = mn.captured(1).trimmed();

        const QRegularExpression reDisc(QStringLiteral("Discoverable:\\s*(yes|no)"),
                                        QRegularExpression::CaseInsensitiveOption);
        const auto md = reDisc.match(raw);
        m_discoverable = md.hasMatch()
                         && md.captured(1).toLower() == QLatin1String("yes");

        emit stateChanged();

        /*
         * 开机/进入页面时确保：
         *   1) 蓝牙已开
         *   2) pairable on   —— 手机主动配对时我方要允许
         *   3) discoverable on —— 手机才**看得到**这台车机
         * 板子出厂默认这两项都是 no，不设的话手机根本搜不到车机，
         * 手机主动发起的配对也容易被拒。
         */
        ensurePowered();
        ensurePairingMode();

        /* 顺手把设备列表也拉一次 */
        run({ QStringLiteral("devices") }, QStringLiteral("devices"));
        return;
    }

    if (tag == QLatin1String("devstate")) {
        /*
         * `info <地址>` 的返回里才有真实状态，把它填回列表，
         * 解决"手机已连上但车机显示未配对"。
         */
        static const QRegularExpression reYes(
            QStringLiteral("(Paired|Bonded|Trusted|Connected):\\s*(yes|no)"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression reName(
            QStringLiteral("^\\s*(?:Name|Alias):\\s*(.+)$"),
            QRegularExpression::MultilineOption);

        const auto nm = reName.match(raw);
        const QString infoName = nm.hasMatch() ? nm.captured(1).trimmed() : QString();

        auto it = reYes.globalMatch(raw);
        while (it.hasNext()) {
            const auto m = it.next();
            const QString key = m.captured(1).toLower();
            const bool yes = (m.captured(2).compare("yes", Qt::CaseInsensitive) == 0);
            for (BtDevice &d : m_devices) {
                if (d.address.compare(m_stateAddr, Qt::CaseInsensitive) != 0)
                    continue;
                if (key == QLatin1String("paired") || key == QLatin1String("bonded"))
                    d.paired = d.paired || yes;
                else if (key == QLatin1String("trusted"))
                    d.trusted = yes;
                else if (key == QLatin1String("connected"))
                    d.connected = yes;
                break;
            }
        }
        if (!infoName.isEmpty()) {
            for (BtDevice &d : m_devices) {
                if (d.address.compare(m_stateAddr, Qt::CaseInsensitive) == 0
                    && d.name.isEmpty()) {
                    d.name = infoName;
                    break;
                }
            }
        }

        emit devicesChanged(m_devices);
        emit stateChanged();

        if (m_stateQueue.isEmpty()) {
            /* 状态齐了，这时候判断"有没有设备连着"才准确。
             * 注意**不要**在这里再 run(devices)：devices 又会排一批 info，
             * 那样会 devices -> devstate -> devices 无限循环。
             * 刷新交给 3 秒一次的轮询（refresh()）就够了。 */
            ensureAudioRoute();
        } else {
            runDeviceStateNext();
        }
        return;
    }

    if (tag == QLatin1String("devices")) {
        applyDeviceList(raw);
        emit devicesChanged(m_devices);
        emit stateChanged();
        ensureAudioRoute();
        return;
    }

    if (tag == QLatin1String("pair") || tag == QLatin1String("connect")) {
        /*
         * 用**真实状态**判断成败，而不是猜输出文字。
         *
         * 踩过的坑：`bluetoothctl pair` **无论成功失败都返回 rc=0**，输出只有
         * "Attempting to pair with XX:..." 这一行。原来代码在输出里找
         * "successful" ——永远找不到，于是既不报成功也不报失败，
         * 用户按了配对"毫无反应"。现在改成回查 `info` 的真实字段
         * （Paired/Bonded/Connected），这才是权威判据。
         */
        m_verifyKind = tag;
        const QString addr = m_lastTargetAddr;
        if (addr.isEmpty()) {
            emit errorOccurred(QStringLiteral("内部错误：没有目标设备地址"));
            run({ QStringLiteral("devices") }, QStringLiteral("devices"));
            return;
        }
        run({ QStringLiteral("info"), addr }, QStringLiteral("verify"));
        return;
    }

    if (tag == QLatin1String("verify")) {
        /* info 的输出里读真实状态（contains(QRegularExpression) 没有大小写参数，
         * 所以把 CaseInsensitiveOption 放进正则本身） */
        static const QRegularExpression rePaired(
            QStringLiteral("Paired:\\s*yes"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression reConnected(
            QStringLiteral("Connected:\\s*yes"), QRegularExpression::CaseInsensitiveOption);
        const bool paired = raw.contains(rePaired);
        const bool connected = raw.contains(reConnected);
        const QString addr = m_lastTargetAddr;
        const QString what = m_verifyKind;

        if (what == QLatin1String("pair")) {
            if (paired) {
                emit infoMessage(QStringLiteral("配对成功，接下来点「连接」"));
                /*
                 * 配对成功后立刻把设备标为 trusted。
                 *
                 * 手机主动连过来时（A2DP 音频就是从手机连到车机），BlueZ 会先向
                 * agent 发一次「是否授权该服务」的询问；如果没人应答，BlueZ
                 * 直接拒绝这条连接，表现就是"配对上了但一放歌就断"。
                 * trust 之后 BlueZ 直接放行，不再问。
                 */
                run({ QStringLiteral("trust"), addr }, QStringLiteral("trust"));
            } else {
                emit errorOccurred(QStringLiteral(
                    "配对失败。请依次确认：① 手机上先「忘记此设备」再重配；"
                    "② 手机贴近车机再试；③ 车机蓝牙页面保持打开。"));
            }
        } else if (what == QLatin1String("connect")) {
            if (connected) {
                emit infoMessage(QStringLiteral("已连接，手机放歌即可从车机喇叭出声"));
            } else if (paired) {
                emit errorOccurred(QStringLiteral(
                    "连接失败：已配对但没连上。手机放首歌试试（A2DP 通常是放歌时才建立）；"
                    "仍不行请把 /userdata/bluealsa.log 发我"));
            } else {
                emit errorOccurred(QStringLiteral("连接失败：该设备还没配对，请先点「配对」"));
            }
        }
        run({ QStringLiteral("devices") }, QStringLiteral("devices"));
        return;
    }

    if (tag == QLatin1String("disconnect") || tag == QLatin1String("remove")
        || tag == QLatin1String("power")) {
        run({ QStringLiteral("devices") }, QStringLiteral("devices"));
        return;
    }

    emit stateChanged();
}

/*
 * 解析 `bluetoothctl devices` 的输出。
 *
 * 兼容两种格式：
 *   新式 D-Bus 风格（BlueZ 5.6x+）：
 *     Device AA:BB:CC:DD:EE:FF
 *         Name: XXX
 *         Alias: XXX
 *         Paired: yes
 *         Trusted: yes
 *         Connected: yes
 *         RSSI: -60
 *   旧式单行：
 *     Device AA:BB:CC:DD:EE:FF XXX
 */
void BtManager::applyDeviceList(const QString &rawOutput)
{
    QVector<BtDevice> found;

    /* 按 "Device XX:XX:..." 切块 */
    static const QRegularExpression reHead(
        QStringLiteral("Device\\s+([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5})\\s*(.*)"));
    static const QRegularExpression reProp(
        QStringLiteral("^\\s*(Name|Alias|Paired|Trusted|Connected|RSSI):\\s*(.*)$"),
        QRegularExpression::MultilineOption);

    const QStringList lines = rawOutput.split('\n');
    BtDevice cur;
    bool has = false;

    auto flush = [&]() {
        if (!has)
            return;
        /* 去重：同一个地址只留一个（后出现的属性更全） */
        bool merged = false;
        for (BtDevice &d : found) {
            if (d.address.compare(cur.address, Qt::CaseInsensitive) == 0) {
                if (!cur.name.isEmpty())
                    d.name = cur.name;
                d.paired = d.paired || cur.paired;
                d.trusted = d.trusted || cur.trusted;
                d.connected = d.connected || cur.connected;
                if (cur.rssi != 0)
                    d.rssi = cur.rssi;
                merged = true;
                break;
            }
        }
        if (!merged)
            found.append(cur);
        cur = BtDevice();
        has = false;
    };

    for (const QString &line : lines) {
        const auto hm = reHead.match(line);
        if (hm.hasMatch()) {
            flush();
            cur.address = hm.captured(1).toUpper();
            cur.name = hm.captured(2).trimmed();
            has = true;
            continue;
        }
        if (!has)
            continue;
        const auto pm = reProp.match(line);
        if (pm.hasMatch()) {
            const QString key = pm.captured(1);
            const QString val = pm.captured(2).trimmed();
            if (key == QLatin1String("Name") || key == QLatin1String("Alias")) {
                if (!val.isEmpty())
                    cur.name = val;
            } else if (key == QLatin1String("Paired")) {
                cur.paired = (val.compare("yes", Qt::CaseInsensitive) == 0);
            } else if (key == QLatin1String("Trusted")) {
                cur.trusted = (val.compare("yes", Qt::CaseInsensitive) == 0);
            } else if (key == QLatin1String("Connected")) {
                cur.connected = (val.compare("yes", Qt::CaseInsensitive) == 0);
            } else if (key == QLatin1String("RSSI")) {
                bool ok = false;
                const int v = val.toInt(&ok);
                if (ok)
                    cur.rssi = v;
            }
        }
    }
    flush();

    m_devices = found;
    m_lastError.clear();

    /*
     * 【重要】`bluetoothctl devices` 的输出**只有一行地址+名字**，不带状态：
     *
     *     Device FC:5B:8C:F7:D7:B3 cj&Redmi K70 Ultra
     *
     * 上面那个属性解析分支（Paired:/Trusted:/Connected:）在 5.77 上**永远匹配不到**，
     * 于是 paired/connected 一直是 false —— 表现就是
     * **"手机明明已经连上了，车机界面还显示未配对"**（用户实测反馈）。
     *
     * 真实状态只能靠 `info <地址>` 拿。所以这里给每个设备排一条 info，
     * 由 devstate 分支把结果填回 m_devices。
     */
    m_stateQueue.clear();
    for (const BtDevice &d : m_devices)
        m_stateQueue.append(d.address);
    runDeviceStateNext();
}

/* 从队列里取下一个地址，跑 `bluetoothctl info <地址>` */
void BtManager::runDeviceStateNext()
{
    if (m_stateQueue.isEmpty())
        return;
    m_stateAddr = m_stateQueue.takeFirst();
    run({ QStringLiteral("info"), m_stateAddr }, QStringLiteral("devstate"));
}

void BtManager::refresh()
{
    /* show 拿到 Powered，回调里会自动接着拉 devices */
    run({ QStringLiteral("show") }, QStringLiteral("show"));
}

void BtManager::setPowered(bool on)
{
    run({ QStringLiteral("power"), on ? QStringLiteral("on") : QStringLiteral("off") },
        QStringLiteral("power"));
}

void BtManager::startScan()
{
    /*
     * 扫描必须"发射后不管"：
     * `bluetoothctl --timeout N scan on` 在 N 秒内**不会退出**（它要一直保持发现状态），
     * 如果按普通同步命令处理，QProcess 不 finished -> m_busy 一直是 true ->
     * 之后所有 pair/connect 都被"上一条命令还没结束"挡掉。
     * （实测踩过：界面点扫描后再点配对就完全没反应。）
     *
     * 所以这里单独走一条不占 m_busy 的路径：起进程后立刻返回，
     * 到点由 finished 回调刷新一次设备列表。
     */
    if (m_scanProc) {
        qInfo() << "bt: 扫描已在进行中";
        return;
    }

    m_scanning = true;
    emit stateChanged();

    /*
     * 扫描之前先把自己设为可配对 + 可发现。
     * 双向都放开，用户从车机找手机、或从手机找车机都能成。
     */
    if (!m_powered)
        setPowered(true);
    run({ QStringLiteral("pairable"), QStringLiteral("on") }, QStringLiteral("pairable"));
    run({ QStringLiteral("discoverable"), QStringLiteral("on") }, QStringLiteral("discoverable"));

    m_scanProc = new QProcess(this);
    m_scanProc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_scanProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
                const QString raw = stripAnsi(QString::fromUtf8(m_scanProc->readAll()));
                qInfo().noquote() << "bt: [scan] 结束 输出=" << raw.trimmed().right(300);
                m_scanProc->deleteLater();
                m_scanProc = nullptr;
                m_scanning = false;
                emit stateChanged();
                /* 扫描结束把发现到的设备读出来 */
                run({ QStringLiteral("devices") }, QStringLiteral("devices"));
            });

    /* 20 秒够扫到附近的设备了 */
    qInfo() << "bt: 开始扫描（20s）";
    m_scanProc->start(QString::fromLatin1(kBluetoothctl),
                      { QStringLiteral("--agent"), QString::fromLatin1(kAgentCapability),
                        QStringLiteral("--timeout"), QStringLiteral("20"),
                        QStringLiteral("scan"), QStringLiteral("on") });
}

void BtManager::stopScan()
{
    if (!m_scanProc) {
        m_scanning = false;
        emit stateChanged();
        return;
    }
    /* 直接杀掉扫描进程即可（比再起一条 scan off 更干脆，也避免又占一次命令槽） */
    qInfo() << "bt: 手动停止扫描";
    m_scanProc->kill();
}

void BtManager::pairDevice(const QString &address)
{
    m_lastTargetAddr = address;
    run({ QStringLiteral("pair"), address }, QStringLiteral("pair"), 30000);
}

void BtManager::connectDevice(const QString &address)
{
    m_lastTargetAddr = address;
    /*
     * **不能**用 `bluetoothctl connect <addr>`：这个板子的 BlueZ 给
     * Device1.Connect 加了一个字符串参数（ADDR_TYPE），而 bluetoothctl 是按
     * 无参数去调的，结果永远报 UnknownMethod（详见 deviceDbusPath 的注释）。
     * 直接 dbus-send，参数传空串。
     */
    runDbus({ QStringLiteral("--system"), QStringLiteral("--print-reply"),
              QStringLiteral("--dest=org.bluez"), deviceDbusPath(address),
              QStringLiteral("org.bluez.Device1.Connect"), QStringLiteral("string:") },
            QStringLiteral("connect"), 30000);
}

void BtManager::disconnectDevice(const QString &address)
{
    /* Disconnect 同样带参数，理由见上 */
    runDbus({ QStringLiteral("--system"), QStringLiteral("--print-reply"),
              QStringLiteral("--dest=org.bluez"), deviceDbusPath(address),
              QStringLiteral("org.bluez.Device1.Disconnect"), QStringLiteral("string:") },
            QStringLiteral("disconnect"), 10000);
}

void BtManager::removeDevice(const QString &address)
{
    run({ QStringLiteral("remove"), address }, QStringLiteral("remove"), 10000);
}

void BtManager::ensurePowered()
{
    if (m_adapterPresent && !m_powered)
        setPowered(true);
}

/*
 * 让车机对别的设备"可见 + 可配对"。
 *
 * 为什么必须做：板子出厂时 Pairable / Discoverable 都是 no
 * （实测 `bluetoothctl show` -> Pairable: no / Discoverable: no）。
 * 后果是：
 *   - 手机蓝牙列表里**看不到**这台车机（除非车机先主动发起配对）
 *   - 手机主动发起的配对会被拒
 * 所以进蓝牙页/开机时都设一遍。pairable 是持久的，discoverable 会超时，
 * 所以进页面时重新设一次。
 */
void BtManager::ensurePairingMode()
{
    if (!m_adapterPresent || !m_powered)
        return;
    if (m_pairingModeSet)
        return;
    m_pairingModeSet = true;
    /*
     * 一次性把"能被手机发现并配对"要的东西都排进队列：
     *   alias        —— 手机搜到的名字。出厂默认是 "BlueZ 5.77"，
     *                   用户根本认不出哪台是车机，所以起个名字
     *   pairable on  —— 允许对方配对（持久）
     *   discoverable on —— 对手机可见（默认 180 秒超时，所以进页面时还会再刷一次）
     * 注意：这三条是**排队**执行的（run() 内部有队列），不会互相覆盖。
     */
    qInfo() << "bt: 设置名称/可配对/可被发现（让手机能找到并配对这台车机）";
    const QString alias = qEnvironmentVariable("SMARTCAR_BT_NAME",
                                               QStringLiteral("Smart_car"));
    run({ QStringLiteral("system-alias"), alias }, QStringLiteral("alias"));
    run({ QStringLiteral("pairable"), QStringLiteral("on") }, QStringLiteral("pairable"));
    run({ QStringLiteral("discoverable"), QStringLiteral("on") }, QStringLiteral("discoverable"));
}

/*
 * 让车机能被手机搜到。
 *
 * 界面上的「被手机发现」按钮已经删掉了：现在**开机就一直是可被发现的**
 * （bt-boot-setup 里设 `discoverable-timeout 0`，main.conf 里也写了
 *  DiscoverableTimeout = 0）。这个函数保留给"进蓝牙页时再刷一次"用，
 * 算是兜底（比如适配器被重开过）。
 */
void BtManager::makeDiscoverable()
{
    if (!m_adapterPresent) {
        emit errorOccurred(QStringLiteral("没有检测到蓝牙适配器"));
        return;
    }
    if (!m_powered)
        setPowered(true);
    m_pairingModeSet = true;
    qInfo() << "bt: 让本机可被搜索（discoverable on）";
    run({ QStringLiteral("pairable"), QStringLiteral("on") }, QStringLiteral("pairable"));
    run({ QStringLiteral("discoverable"), QStringLiteral("on") }, QStringLiteral("discoverable"));
}

/*
 * 确保蓝牙音频通道在跑：
 *   bluealsa（A2DP Sink 端点）+ bluealsa-aplay（把收到的音频播到本地声卡）
 *
 * 【只开，永远不关】
 * bluealsa 注册的 A2DP Sink 端点**就是"车机是个音箱"这个身份本身**。
 * 一旦 audio-off 把 bluealsa 关掉，手机侧就看不到 Audio Sink 了，
 * 也就不会再连过来放歌。所以任何情况下都不该关。
 *
 * 踩过的坑（必须记下来，否则很容易再犯）：
 * 上一版按"当前有没有设备连着"在 on/off 之间切 —— 结果
 *   ① `devices` 处理器里调它时，设备状态**还没补齐**（全是 false）；
 *   ② `devstate` 补齐后再调一次，就变成 true；
 * 同一个轮询周期里于是 on -> off 来回切，日志里就是每秒一次的
 * "蓝牙音频已停止 / bluealsa 已启动" 抖动（实测 43 次 bt-ctl 调用），
 * 而且抖动期间 bluealsa 有一半时间是关的，反而把链路打断。
 *
 * 现在：幂等、只开、一个进程生命周期最多调一次。
 */
void BtManager::ensureAudioRoute()
{
    if (m_audioRouted)
        return;                       /* 已经确保过了，别再起进程 */
    m_audioRouted = true;

    auto *p = new QProcess(this);
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, p](int, QProcess::ExitStatus) {
                const QString out = QString::fromUtf8(p->readAll()).trimmed();
                qInfo().noquote() << "bt: bt-ctl audio-on ->" << out;
                if (!out.startsWith(QLatin1String("OK")))
                    emit errorOccurred(QStringLiteral("蓝牙音频启动失败：") + out);
                p->deleteLater();
            });
    qInfo().noquote() << "bt: 确保蓝牙音频通道在跑（bt-ctl audio-on）";
    p->start(QStringLiteral("/userdata/smart_car/bt-ctl"), QStringList{QStringLiteral("audio-on")});
}
