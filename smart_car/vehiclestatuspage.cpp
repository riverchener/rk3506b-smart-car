#include "vehiclestatuspage.h"
#include "canmgr.h"
#include "carstatuspanel.h"
#include "uiadapter.h"

#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QListWidget>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QPushButton>
#include <QRectF>
#include <QShowEvent>
#include <QStringList>
#include <QVBoxLayout>

namespace {

/* 页面统一科技感 QSS */
const char *kPageQss =
    "QPushButton#hudBack {"
    "  background: #111726; color: #00E5FF; border: 1px solid #00E5FF;"
    "  border-radius: 12px; font-size: 18px; font-weight: bold; padding: 0 12px; }"
    "QPushButton#hudBack:pressed { background: #00E5FF; color: #060A14; }"
    "QPushButton#hudBack:disabled { color: #4E5A70; border-color: #1E2637; }"

    "QLabel#hudTitle { color: #EEF4FF; font-size: 24px; font-weight: bold; }"
    "QLabel#hudSub   { color: #617594; font-size: 15px; font-weight: normal; }"
    "QLabel#hudChip  { color: #94A7C6; font-size: 16px; }"
    "QLabel#hudSec   { color: #00E5FF; font-size: 15px; font-weight: bold; }"
    "QLabel#hudCount { color: #617594; font-size: 15px; }"
    "QLabel#hudLatest{ color: #C9D5E6; font-size: 15px; font-family: monospace; }"

    "QFrame#hudDock {"
    "  background: #0C1222; border: 1px solid #243656; border-radius: 10px; }"
    "QListWidget#hudFrames {"
    "  background: #080D18; border: 1px solid #1A2744; border-radius: 8px;"
    "  color: #B9C7DC; font-size: 15px; outline: none; }"
    "QListWidget#hudFrames::item { padding: 1px 6px; }"
    "QListWidget#hudFrames::item:alternate { background: #0E1626; }";

/* 绘制四角 L-角标 */
void pageCorners(QPainter &p, const QRectF &r, const QColor &c, qreal len, qreal w)
{
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(c, w, Qt::SolidLine, Qt::FlatCap));
    p.drawLine(QPointF(r.left(), r.top()), QPointF(r.left() + len, r.top()));
    p.drawLine(QPointF(r.left(), r.top()), QPointF(r.left(), r.top() + len));
    p.drawLine(QPointF(r.right(), r.top()), QPointF(r.right() - len, r.top()));
    p.drawLine(QPointF(r.right(), r.top()), QPointF(r.right(), r.top() + len));
    p.drawLine(QPointF(r.left(), r.bottom()), QPointF(r.left() + len, r.bottom()));
    p.drawLine(QPointF(r.left(), r.bottom()), QPointF(r.left(), r.bottom() - len));
    p.drawLine(QPointF(r.right(), r.bottom()), QPointF(r.right() - len, r.bottom()));
    p.drawLine(QPointF(r.right(), r.bottom()), QPointF(r.right(), r.bottom() - len));
}

} // namespace

VehicleStatusPage::VehicleStatusPage(CanMgr *can, QWidget *parent)
    : QWidget(parent), m_can(can)
{
    setWindowTitle(QStringLiteral("车辆状态"));
    setStyleSheet(QString::fromUtf8(kPageQss));

    // 1. 顶部控制与状态栏 (高度固定 52)
    m_topBar = new QWidget(this);
    m_topBar->setObjectName(QStringLiteral("hudTopBar"));

    auto *backBtn = new QPushButton(QStringLiteral("← 返回"), m_topBar);
    backBtn->setObjectName(QStringLiteral("hudBack"));
    backBtn->setFixedSize(110, 48);
    backBtn->setFocusPolicy(Qt::NoFocus);
    m_back = backBtn;
    connect(backBtn, &QPushButton::clicked, this, &VehicleStatusPage::back);

    auto *accent = new QFrame(m_topBar);
    accent->setStyleSheet(QStringLiteral("background: #00E5FF; border: none; border-radius: 2px;"));
    m_accent = accent;

    auto *title = new QLabel(QStringLiteral("车辆状态"), m_topBar);
    title->setObjectName(QStringLiteral("hudTitle"));
    auto *sub = new QLabel(QStringLiteral("VEHICLE STATUS HUD"), m_topBar);
    sub->setObjectName(QStringLiteral("hudSub"));

    m_chip = new QLabel(QStringLiteral("CAN:"), m_topBar);
    m_chip->setObjectName(QStringLiteral("hudChip"));
    m_chip->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *top = new QHBoxLayout(m_topBar);
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(12);
    top->addWidget(backBtn);
    top->addSpacing(4);
    top->addWidget(accent);
    top->addWidget(title);
    top->addWidget(sub);
    top->addStretch(1);
    top->addWidget(m_chip);

    // 2. 中部核心自绘一体化全景大面板 (占据绝大部分可视高度)
    m_panel = new CarStatusPanel(m_can, this);

    // 3. 底部极简悬浮 CAN 总线监控流 (高度固定 82px)
    m_frameDock = new QFrame(this);
    m_frameDock->setObjectName(QStringLiteral("hudDock"));

    auto *dockHead = new QWidget(m_frameDock);
    auto *secTitle = new QLabel(QStringLiteral("CAN 实时总线流"), dockHead);
    secTitle->setObjectName(QStringLiteral("hudSec"));
    m_latestFrame = new QLabel(QStringLiteral("等待帧接收..."), dockHead);
    m_latestFrame->setObjectName(QStringLiteral("hudLatest"));
    m_count = new QLabel(QStringLiteral("0 帧"), dockHead);
    m_count->setObjectName(QStringLiteral("hudCount"));
    m_count->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *headLay = new QHBoxLayout(dockHead);
    headLay->setContentsMargins(0, 0, 0, 0);
    headLay->setSpacing(10);
    headLay->addWidget(secTitle);
    headLay->addWidget(m_latestFrame, 1);
    headLay->addWidget(m_count);

    m_frames = new QListWidget(m_frameDock);
    m_frames->setObjectName(QStringLiteral("hudFrames"));
    m_frames->setFocusPolicy(Qt::NoFocus);
    m_frames->setSelectionMode(QAbstractItemView::NoSelection);
    m_frames->setAlternatingRowColors(true);
    m_frames->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *dockLay = new QVBoxLayout(m_frameDock);
    dockLay->setContentsMargins(8, 4, 8, 4);
    dockLay->setSpacing(3);
    dockLay->addWidget(dockHead);
    dockLay->addWidget(m_frames);

    // 全局垂直布局
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 8, 12, 6);
    root->setSpacing(6);
    root->addWidget(m_topBar);
    root->addWidget(m_panel, 1);
    root->addWidget(m_frameDock);

    if (m_can) {
        connect(m_can, &CanMgr::frameReceived, this, &VehicleStatusPage::onFrame);
        connect(m_can, &CanMgr::changed, this, &VehicleStatusPage::refreshChip);
        connect(m_can, &CanMgr::log, this, &VehicleStatusPage::refreshChip);
    }
    refreshChip();

    UiAdapter::fit(this);
    applyFixedHeights();
}

void VehicleStatusPage::applyFixedHeights()
{
    if (m_topBar)    m_topBar->setFixedHeight(50);
    if (m_back)      m_back->setFixedSize(110, 46);
    if (m_accent)    m_accent->setFixedSize(4, 26);
    if (m_frames)    m_frames->setFixedHeight(52);
    if (m_frameDock) m_frameDock->setFixedHeight(82);
}

void VehicleStatusPage::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    applyFixedHeights();
}

void VehicleStatusPage::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const qreal W = width(), H = height();

    // 科技感深蓝渐变底色
    QLinearGradient bg(0, 0, 0, H);
    bg.setColorAt(0.0, QColor(0x0A, 0x10, 0x1E));
    bg.setColorAt(1.0, QColor(0x06, 0x0A, 0x14));
    p.fillRect(rect(), bg);

    // 科技感边框与四角标
    pageCorners(p, QRectF(4, 4, W - 8, H - 8), QColor(0x00, 0xE5, 0xFF, 120), 20, 2.0);
}

void VehicleStatusPage::refreshChip()
{
    if (!m_chip || !m_can)
        return;

    const bool open = m_can->isOpen();
    const bool demo = m_can->demoRunning();
    const QString iface = m_can->iface();
    const qint64 d = m_can->msSinceDrive();

    QString txt;
    QString col;
    if (demo) {
        txt = QStringLiteral("● CAN: 演示模式");
        col = QStringLiteral("#2979FF");
    } else if (open) {
        if (d < 0) {
            txt = QStringLiteral("● CAN: %1 · 监听中").arg(iface);
            col = QStringLiteral("#00E5FF");
        } else if (d < 1000) {
            txt = QStringLiteral("● CAN: %1 · %2ms").arg(iface).arg(d);
            col = QStringLiteral("#00E676");
        } else {
            txt = QStringLiteral("● CAN: %1 · 超时%2s").arg(iface).arg(d / 1000);
            col = QStringLiteral("#FFC400");
        }
    } else {
        txt = QStringLiteral("● CAN: 未连接");
        col = QStringLiteral("#FF1744");
    }

    m_chip->setText(QStringLiteral("<span style='color:%1;'>%2</span>").arg(col, txt));
}

/*
 * 把一帧 CAN 解析成「最近 CAN 帧」列表里的一行文字。
 *
 * ⚠ 字节含义**唯一权威**是 docs/CAN_接口定义与操作_20260929.md §3.1/§3.4
 *   （0x100 见 :148，0x101 见 :165，0x103 见 :271，0x105 见 :300）。
 *   本函数历史上与 canmgr.cpp 的解析**不一致**（2026-10-13 复核）：
 *     0x100 B1/B2 取反、0x101 全车锁取错字节、0x103 按大端、0x105 缺胎温。
 *   现在已逐条对齐文档，并与 canmgr.cpp 的同一 ID 分支保持一致 ——
 *   改这里之前先看文档，再对照 canmgr.cpp，别再让两处各说各话。
 */
QString VehicleStatusPage::decode(quint32 id, const QByteArray &d)
{
    const auto *u = reinterpret_cast<const quint8 *>(d.constData());
    const int n = d.size();
    auto b = [u, n](int i) -> quint8 { return (i >= 0 && i < n) ? u[i] : 0; };

    switch (id) {
    case 0x100: {
        /* B0 车速 / B1 标志位 / B2 档位（文档 §3.1 :152-161） */
        int sp = b(0);
        int gear = b(2);                                  /* B2 = 档位：0=P 1=R 2=N 3=D，其它=无效 */
        const char *gn = (gear == 0 ? "P" : (gear == 1 ? "R" : (gear == 2 ? "N" : (gear == 3 ? "D" : "--"))));
        QStringList st;
        const quint8 fl = b(1);                           /* B1 = 标志位 */
        if (fl & 0x01) st << "左转";
        if (fl & 0x02) st << "右转";
        if (fl & 0x04) st << "刹车";
        if (fl & 0x08) st << "倒车";
        if (fl & 0x10) st << "未系安全带";
        if (fl & 0x20) st << "手刹";
        if (fl & 0x40) st << "双闪";
        return QStringLiteral("车速 %1km/h · 档位 %2 · %3").arg(sp == 0xFF ? QStringLiteral("--") : QString::number(sp), gn, st.isEmpty() ? QStringLiteral("正常") : st.join('/'));
    }
    case 0x101: {
        QStringList doors;
        if (b(0) & 0x01) doors << "左前开";
        if (b(0) & 0x02) doors << "右前开";
        if (b(0) & 0x04) doors << "左后开";
        if (b(0) & 0x08) doors << "右后开";
        if (b(0) & 0x10) doors << "后备箱开";
        if (b(0) & 0x20) doors << "机盖开";
        /* 全车锁 = **B1 bit0**（文档 §3.1 :176），不是 B0 的 bit7 */
        return QStringLiteral("车门: %1 · 锁: %2").arg(doors.isEmpty() ? QStringLiteral("全关") : doors.join('/'), (b(1) & 0x01) ? QStringLiteral("已锁") : QStringLiteral("未锁"));
    }
    case 0x102: {
        QStringList lt;
        if (b(0) & 0x01) lt << "近光";
        if (b(0) & 0x02) lt << "远光";
        if (b(0) & 0x04) lt << "示宽";
        if (b(0) & 0x08) lt << "前雾";
        if (b(0) & 0x10) lt << "后雾";
        return QStringLiteral("灯光: %1 · 空调: %2 %3℃").arg(lt.isEmpty() ? QStringLiteral("关") : lt.join('/'), (b(1) & 0x01) ? QStringLiteral("开") : QStringLiteral("关"), QString::number(b(2)));
    }
    case 0x103: {
        /* 多字节量一律**小端**（文档 §3.4 :275-278）：
         *   B0..B2 总里程 24 位小端 LSB=0.1km（0xFFFFFF 无效）
         *   B3..B4 小计里程 16 位小端 LSB=0.1km（0xFFFF 无效）
         *   B5 油量 %   B6..B7 续航 16 位小端 km（0xFFFF 无效）
         * 解析方式与 canmgr.cpp 的 0x103 分支完全一致。 */
        quint32 odo = quint32(b(0)) | (quint32(b(1)) << 8) | (quint32(b(2)) << 16);
        quint16 trip = quint16(b(3)) | (quint16(b(4)) << 8);
        int fuel = b(5);
        quint16 rng = quint16(b(6)) | (quint16(b(7)) << 8);
        (void)trip;
        return QStringLiteral("总里程: %1km · 油量: %2% · 续航: %3km").arg(
            odo == 0xFFFFFF ? QStringLiteral("--") : QString::asprintf("%.1f", odo / 10.0),
            fuel == 0 || fuel == 0xFF ? QStringLiteral("--") : QString::number(fuel),
            rng == 0xFFFF ? QStringLiteral("--") : QString::number(rng));
    }
    case 0x104: {
        int m = b(2);
        const char *mn = (m == 0 ? "经济" : (m == 1 ? "标准" : (m == 2 ? "运动" : (m == 3 ? "雪地" : "--"))));
        return QStringLiteral("模式: %1 · 告警: 0x%2 · 故障码: %3个").arg(mn, QString::number(b(3), 16).toUpper(), QString::number(b(4)));
    }
    case 0x105: {
        /* B0..B3 四轮胎压（LSB=5kPa；0 或 0xFF 无效）
         * B4..B7 四轮胎温（值 = 摄氏 + 40；0xFF 无效）—— 文档 §3.4 :300-311，原来缺这一半 */
        auto pr = [&](int i) -> QString {
            return (b(i) == 0 || b(i) == 0xFF) ? QStringLiteral("--") : QString::number(int(b(i)) * 5);
        };
        auto tp = [&](int i) -> QString {
            return (b(i) == 0xFF) ? QStringLiteral("--") : QString::number(int(b(i)) - 40);
        };
        QString s = QStringLiteral("胎压: FL %1 / FR %2 / RL %3 / RR %4 kPa · 胎温: FL %5 / FR %6 / RL %7 / RR %8 ℃");
        return s.arg(pr(0)).arg(pr(1)).arg(pr(2)).arg(pr(3))
                .arg(tp(4)).arg(tp(5)).arg(tp(6)).arg(tp(7));
    }
    default:
        return QStringLiteral("DLC=%1 [ %2 ]").arg(n).arg(QString::fromUtf8(d.toHex(' ').toUpper()));
    }
}

void VehicleStatusPage::onFrame(quint32 id, const QByteArray &data)
{
    ++m_n;
    if (m_count)
        m_count->setText(QStringLiteral("%1 帧").arg(m_n));

    const QString timeStr = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString hexStr = QString::fromUtf8(data.toHex().toUpper());
    const QString dec = decode(id, data);
    const QString line = QStringLiteral("%1  0x%2#%3  %4")
                             .arg(timeStr, QString::number(id, 16).toUpper().rightJustified(3, '0'),
                                  hexStr.leftJustified(16, ' '), dec);

    if (m_latestFrame) {
        m_latestFrame->setText(QStringLiteral("0x%1#%2  %3").arg(QString::number(id, 16).toUpper(), hexStr, dec));
    }

    if (m_frames) {
        m_frames->insertItem(0, line);
        while (m_frames->count() > 30) {
            delete m_frames->takeItem(m_frames->count() - 1);
        }
    }
}
