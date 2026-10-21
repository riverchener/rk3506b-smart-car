#include "bluetoothpage.h"
#include "uiadapter.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QDebug>

namespace {
/* 设计画布 1024x600（和项目其它页面一致），UiAdapter 会整体缩放 */
const int kDesignW = 1024;
const int kDesignH = 600;

const char *kStylePage =
    "BluetoothPage { background-color: rgb(18, 22, 30); }"
    "QLabel { color: rgb(230, 236, 245); }"
    "QListWidget {"
    "  background-color: rgb(28, 33, 44);"
    "  border: 1px solid rgb(60, 70, 90);"
    "  border-radius: 6px;"
    "  color: rgb(228, 234, 244);"
    "  font-size: 20px;"
    "  outline: none;"
    "}"
    "QListWidget::item { padding: 14px 10px; border-bottom: 1px solid rgb(44, 52, 68); }"
    "QListWidget::item:selected { background-color: rgb(40, 90, 150); color: white; }"
    "QPushButton {"
    "  background-color: rgb(44, 52, 68);"
    "  color: rgb(230, 236, 245);"
    "  border: 1px solid rgb(70, 82, 105);"
    "  border-radius: 6px;"
    "  font-size: 19px;"
    "  padding: 8px 14px;"
    "}"
    "QPushButton:hover { background-color: rgb(58, 70, 92); }"
    "QPushButton:disabled { color: rgb(110, 118, 132); border-color: rgb(50, 58, 74); }"
    ;

QPushButton *makeButton(const QString &text, int w, int h)
{
    auto *b = new QPushButton(text);
    b->setFixedSize(w, h);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}
}

BluetoothPage::BluetoothPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("BluetoothPage");
    setStyleSheet(QString::fromLatin1(kStylePage));
    setWindowFlag(Qt::FramelessWindowHint);
    setFixedSize(kDesignW, kDesignH);

    m_bt = new BtManager(this);

    /* ---------------- 顶部栏 ---------------- */
    auto *top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(12);

    m_back = makeButton(QStringLiteral("← 返回"), 110, 52);
    m_title = new QLabel(QStringLiteral("蓝牙"));
    m_title->setStyleSheet("font-size: 26px; font-weight: bold;");
    m_stateLabel = new QLabel(QStringLiteral("状态未知"));
    m_stateLabel->setStyleSheet("font-size: 18px; color: rgb(150, 200, 255);");

    m_powerBtn = makeButton(QStringLiteral("打开蓝牙"), 130, 52);
    m_scanBtn = makeButton(QStringLiteral("扫描设备"), 130, 52);
    /*
     * 这里原本还有一个「被手机发现」按钮（discoverable on）。
     * 已删除：车机现在**开机就一直是可被发现的**
     * （bt-boot-setup 里设了 `bluetoothctl discoverable-timeout 0`，
     *  /etc/main.conf 里也写了 DiscoverableTimeout = 0），
     * 所以不需要用户每次手动点一下 —— 手机蓝牙里直接就能找到 Smart_car。
     */

    top->addWidget(m_back);
    top->addWidget(m_title);
    top->addSpacing(8);
    top->addWidget(m_stateLabel);
    top->addStretch(1);
    top->addWidget(m_powerBtn);
    top->addWidget(m_scanBtn);

    /* ---------------- 设备列表 ---------------- */
    m_list = new QListWidget;
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    /* ---------------- 底部操作 ---------------- */
    auto *bottom = new QHBoxLayout;
    bottom->setContentsMargins(0, 0, 0, 0);
    bottom->setSpacing(12);
    m_pairBtn = makeButton(QStringLiteral("配对"), 100, 54);
    m_connBtn = makeButton(QStringLiteral("连接"), 100, 54);
    m_discBtn = makeButton(QStringLiteral("断开"), 100, 54);
    m_forgetBtn = makeButton(QStringLiteral("忘记"), 100, 54);
    bottom->addWidget(m_pairBtn);
    bottom->addWidget(m_connBtn);
    bottom->addWidget(m_discBtn);
    bottom->addWidget(m_forgetBtn);
    bottom->addStretch(1);

    m_hint = new QLabel(QStringLiteral(
        "推荐用法：直接在手机上连（车机开机后一直可被手机发现）。\n"
        "① 手机 设置→蓝牙 → 搜索到 \"Smart_car\" → 配对（会走 Just Works，不会问 PIN 码）；\n"
        "② 手机里点一下 Smart_car 连接，然后放歌，声音就从车机喇叭出来。\n"
        "车机主动连手机：手机要先进入\"可被发现/配对模式\"，再点「扫描设备」→ 选中 → 「配对」。"));
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet("font-size: 16px; color: rgb(170, 180, 196);");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(10);
    root->addLayout(top);
    root->addWidget(m_list, 1);
    root->addLayout(bottom);
    root->addWidget(m_hint);

    /* ---------------- 信号连接 ---------------- */
    connect(m_back, &QPushButton::clicked, this, &BluetoothPage::onBackClicked);
    connect(m_powerBtn, &QPushButton::clicked, this, &BluetoothPage::onPowerClicked);
    connect(m_scanBtn, &QPushButton::clicked, this, &BluetoothPage::onScanClicked);
    connect(m_pairBtn, &QPushButton::clicked, this, &BluetoothPage::onPairClicked);
    connect(m_connBtn, &QPushButton::clicked, this, &BluetoothPage::onConnectClicked);
    connect(m_discBtn, &QPushButton::clicked, this, &BluetoothPage::onDisconnectClicked);
    connect(m_forgetBtn, &QPushButton::clicked, this, &BluetoothPage::onForgetClicked);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &BluetoothPage::onSelectionChanged);

    connect(m_bt, &BtManager::devicesChanged, this, &BluetoothPage::onDevicesChanged);
    connect(m_bt, &BtManager::stateChanged, this, &BluetoothPage::onStateChanged);
    connect(m_bt, &BtManager::errorOccurred, this, &BluetoothPage::onBtError);
    connect(m_bt, &BtManager::infoMessage, this, &BluetoothPage::onBtInfo);

    /* 扫描期间定期刷新列表（bluetoothctl 的扫描是异步发现设备的） */
    m_tick = new QTimer(this);
    m_tick->setInterval(3000);
    connect(m_tick, &QTimer::timeout, this, &BluetoothPage::refreshTick);

    UiAdapter::fit(this);     // 按实际屏幕等比缩放
    updateButtons();
    updateHeader();
}

QString BluetoothPage::selectedAddress() const
{
    auto *it = m_list->currentItem();
    if (!it)
        return QString();
    return it->data(Qt::UserRole).toString();
}

void BluetoothPage::bluetooth_working()
{
    m_entered = true;
    updateHeader();
    setHint(QStringLiteral("正在查询蓝牙状态…"));
    m_bt->refresh();
    if (!m_tick->isActive())
        m_tick->start();
}

void BluetoothPage::triggerScanForTest()
{
    onScanClicked();
}

void BluetoothPage::onBackClicked()
{
    m_tick->stop();
    if (m_bt->scanning())
        m_bt->stopScan();
    emit backout();
}

void BluetoothPage::onScanClicked()
{
    if (m_bt->busy()) {
        setHint(QStringLiteral("正在处理上一条命令，请稍候…"));
        return;
    }
    if (!m_bt->powered()) {
        setHint(QStringLiteral("蓝牙还没打开，先点「打开蓝牙」。"), true);
        return;
    }
    if (m_bt->scanning()) {
        m_bt->stopScan();
        setHint(QStringLiteral("已停止扫描。"));
    } else {
        setHint(QStringLiteral("正在扫描附近蓝牙设备（约 15 秒）…"));
        m_bt->startScan();
    }
    updateHeader();
}

void BluetoothPage::onPowerClicked()
{
    if (m_bt->busy()) {
        setHint(QStringLiteral("正在处理上一条命令，请稍候…"));
        return;
    }
    const bool turnOn = !m_bt->powered();
    setHint(turnOn ? QStringLiteral("正在打开蓝牙…") : QStringLiteral("正在关闭蓝牙…"));
    m_bt->setPowered(turnOn);
}

void BluetoothPage::onPairClicked()
{
    const QString addr = selectedAddress();
    if (addr.isEmpty()) {
        setHint(QStringLiteral("先在列表里选中一个设备。"), true);
        return;
    }
    setHint(QStringLiteral("正在配对 %1 …（手机上如弹出确认请点允许）").arg(addr));
    m_bt->pairDevice(addr);
}

void BluetoothPage::onConnectClicked()
{
    const QString addr = selectedAddress();
    if (addr.isEmpty()) {
        setHint(QStringLiteral("先在列表里选中一个设备。"), true);
        return;
    }
    setHint(QStringLiteral("正在连接 %1 …").arg(addr));
    m_bt->connectDevice(addr);
}

void BluetoothPage::onDisconnectClicked()
{
    const QString addr = selectedAddress();
    if (addr.isEmpty()) {
        setHint(QStringLiteral("先在列表里选中一个设备。"), true);
        return;
    }
    setHint(QStringLiteral("正在断开 %1 …").arg(addr));
    m_bt->disconnectDevice(addr);
}

void BluetoothPage::onForgetClicked()
{
    const QString addr = selectedAddress();
    if (addr.isEmpty()) {
        setHint(QStringLiteral("先在列表里选中一个设备。"), true);
        return;
    }
    setHint(QStringLiteral("已忘记 %1（如需重连请重新配对）").arg(addr));
    m_bt->removeDevice(addr);
}

void BluetoothPage::onDevicesChanged(const QVector<BtDevice> &devices)
{
    m_devices = devices;
    rebuildList();
    updateHeader();

    if (m_devices.isEmpty()) {
        if (m_bt->scanning())
            setHint(QStringLiteral("扫描中…暂时没发现设备（确保对方已开启可被发现）。"));
        else
            setHint(QStringLiteral("没有已发现/已配对的设备。点「扫描设备」搜一下。"));
    }
}

void BluetoothPage::onStateChanged()
{
    updateHeader();
    updateButtons();
}

void BluetoothPage::onSelectionChanged()
{
    updateButtons();
    auto *it = m_list->currentItem();
    if (!it)
        return;
    const QString addr = it->data(Qt::UserRole).toString();
    for (const BtDevice &d : m_devices) {
        if (d.address == addr) {
            QString s = d.displayName() + QStringLiteral("  (") + d.address + QStringLiteral(")");
            if (d.connected)
                s += QStringLiteral("  · 已连接");
            else if (d.paired)
                s += QStringLiteral("  · 已配对");
            if (d.rssi != 0)
                s += QStringLiteral("  · 信号 %1 dBm").arg(d.rssi);
            setHint(s);
            break;
        }
    }
}

void BluetoothPage::onBtError(const QString &msg)
{
    setHint(msg, true);
    updateHeader();
}

void BluetoothPage::onBtInfo(const QString &msg)
{
    setHint(msg, false);
    updateHeader();
}

void BluetoothPage::refreshTick()
{
    /*
     * 只在"页面真的可见"且不在忙时才轮询。
     *
     * 踩过的坑：这里原来只判断 isVisible()，但实测**蓝牙页没打开时也在每 3 秒
     * 跑一次 show+devices**（HCI 抓包看到 12 秒内 8 次 bluetoothctl 开关）。
     * 每次启动 bluetoothctl 都会注册一个 pairing agent，手机正在配对/连接时
     * 被这种反复注册打扰，连接就会被打断（内核日志里 `dir 1` 我方发起断开）。
     * 所以这里再显式要求窗口是 active/可见的，并在 hideEvent 里停掉定时器。
     */
    if (!isVisible() || isHidden() || m_bt->busy())
        return;
    m_bt->refresh();
}

void BluetoothPage::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    /* 离开页面就停止轮询：不要在没有界面时还反复起 bluetoothctl */
    if (m_tick)
        m_tick->stop();
    qInfo() << "蓝牙页：隐藏（停止轮询）";
}

void BluetoothPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_tick && !m_tick->isActive())
        m_tick->start();
}

void BluetoothPage::rebuildList()
{
    const QString keep = selectedAddress();
    m_list->clear();

    for (const BtDevice &d : m_devices) {
        QString right;
        if (d.connected)
            right = QStringLiteral("已连接");
        else if (d.paired)
            right = QStringLiteral("已配对");
        else
            right = QStringLiteral("未配对");

        QString sig;
        if (d.rssi != 0) {
            /* 粗分档，比数字直观 */
            if (d.rssi >= -60)
                sig = QStringLiteral(" ▮▮▮");
            else if (d.rssi >= -75)
                sig = QStringLiteral(" ▮▮");
            else
                sig = QStringLiteral(" ▮");
        }

        auto *it = new QListWidgetItem(
            QStringLiteral("%1\n%2   %3%4")
                .arg(d.displayName(), d.address, right, sig));
        it->setData(Qt::UserRole, d.address);
        if (d.connected)
            it->setForeground(QColor(120, 240, 170));
        else if (d.paired)
            it->setForeground(QColor(150, 200, 255));
        m_list->addItem(it);

        if (d.address == keep)
            m_list->setCurrentItem(it);
    }
    updateButtons();
}

void BluetoothPage::updateHeader()
{
    const QString powerTxt = m_bt->powered() ? QStringLiteral("蓝牙已开")
                                             : QStringLiteral("蓝牙已关");
    QString st;
    if (!m_bt->adapterPresent())
        st = QStringLiteral("没有检测到蓝牙适配器");
    else if (m_bt->scanning())
        st = powerTxt + QStringLiteral(" · 扫描中…");
    else
        st = powerTxt + QStringLiteral(" · 共 %1 个设备").arg(m_devices.size());

    const auto conn = m_bt->connectedDevices();
    if (!conn.isEmpty())
        st += QStringLiteral(" · 已连接: ") + conn.first().displayName();

    m_stateLabel->setText(st);
    m_powerBtn->setText(m_bt->powered() ? QStringLiteral("关闭蓝牙")
                                        : QStringLiteral("打开蓝牙"));
    m_scanBtn->setText(m_bt->scanning() ? QStringLiteral("停止扫描")
                                        : QStringLiteral("扫描设备"));
    updateButtons();
}

void BluetoothPage::updateButtons()
{
    const bool powered = m_bt->powered();
    const bool busy = m_bt->busy();
    const QString addr = selectedAddress();

    const bool hasSel = !addr.isEmpty();
    bool selConnected = false, selPaired = false;
    for (const BtDevice &d : m_devices) {
        if (d.address == addr) {
            selConnected = d.connected;
            selPaired = d.paired;
            break;
        }
    }

    m_scanBtn->setEnabled(powered && !busy);
    m_powerBtn->setEnabled(!busy);
    /* 没配对过的才能"配对"；已连接才能"断开"；连接按钮给未连接的 */
    m_pairBtn->setEnabled(hasSel && powered && !busy && !selPaired);
    m_connBtn->setEnabled(hasSel && powered && !busy && !selConnected);
    m_discBtn->setEnabled(hasSel && powered && !busy && selConnected);
    m_forgetBtn->setEnabled(hasSel && !busy && selPaired);
}

void BluetoothPage::setHint(const QString &text, bool isError)
{
    m_hint->setText(text);
    m_hint->setStyleSheet(isError
                              ? "font-size: 17px; color: rgb(255, 140, 140);"
                              : "font-size: 17px; color: rgb(170, 180, 196);");
}
