#include "carstatuspanel.h"
#include "canmgr.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRadialGradient>
#include <QRectF>
#include <QStringList>
#include <QtMath>

/* ============================================================================
 * HUD 科技感设计规范与色彩令牌（2026-10-03 科幻座舱重塑版）
 * ==========================================================================*/
namespace {

/* 核心色彩令牌 */
const QColor kBgDark     (0x06, 0x0A, 0x14); // #060A14 极深座舱黑蓝
const QColor kPanelBg    (0x0C, 0x12, 0x22); // #0C1222 面板基底
const QColor kCardBg     (0x10, 0x17, 0x2A); // #10172A 卡片背景 (半透使用)
const QColor kCardBgHi   (0x16, 0x22, 0x3D); // #16223D 卡片高亮底
const QColor kGridLine   (0x1A, 0x27, 0x44); // #1A2744 坐标网格
const QColor kBorder     (0x24, 0x36, 0x56); // #243656 常规边框
const QColor kBorderHi   (0x3B, 0x53, 0x7E); // #3B537E 高亮边框

/* 文本层级令牌 */
const QColor kTextHi     (0xEE, 0xF4, 0xFF); // #EEF4FF 核心高亮文字 (对比度 > 15:1)
const QColor kTextMid    (0x94, 0xA7, 0xC6); // #94A7C6 次级标签/说明
const QColor kTextMute   (0x61, 0x75, 0x94); // #617594 弱化标签/单位
const QColor kTextStale  (0x75, 0x86, 0x9E); // #75869E 超时数值暗灰色 (保留最后值)

/* 状态语义霓虹令牌 */
const QColor kCyan       (0x00, 0xE5, 0xFF); // #00E5FF 主交互/HUD青色/近光
const QColor kBlue       (0x29, 0x79, 0xFF); // #2979FF 远光/标准模式/信息
const QColor kGreen      (0x00, 0xE6, 0x76); // #00E676 正常/已锁/经济模式/转向
const QColor kAmber      (0xFF, 0xC4, 0x00); // #FFC400 警告/运动模式/雾灯
const QColor kRed        (0xFF, 0x17, 0x44); // #FF1744 危险/故障/刹车/门开/胎压异常
const QColor kPurple     (0xD5, 0x00, 0xF9); // #D500F9 雪地模式

/* 辅助透明度生成 */
inline QColor rgba(const QColor &c, int a) {
    return QColor(c.red(), c.green(), c.blue(), qBound(0, a, 255));
}

/* 绘制 HUD 卡片背景与科技边角 */
void drawHudCard(QPainter &p, const QRectF &r, const QColor &accent = kBorder,
                 bool highlight = false, qreal radius = 8.0)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // 卡片背景填充（深邃渐变）
    QLinearGradient bg(r.topLeft(), r.bottomRight());
    bg.setColorAt(0.0, rgba(highlight ? kCardBgHi : kCardBg, 220));
    bg.setColorAt(1.0, rgba(kPanelBg, 240));
    p.setBrush(bg);
    p.setPen(QPen(rgba(highlight ? accent : kBorder, highlight ? 180 : 120), 1.2));
    p.drawRoundedRect(r, radius, radius);

    // 科技光晕描边（若高亮）
    if (highlight) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(rgba(accent, 60), 2.5));
        p.drawRoundedRect(r.adjusted(-1, -1, 1, 1), radius + 1, radius + 1);
    }

    // 四角 HUD L-角标
    const qreal L = qMin(r.width(), r.height()) * 0.18;
    const qreal capL = qBound(6.0, L, 14.0);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(rgba(highlight ? accent : kBorderHi, highlight ? 240 : 150), 1.8, Qt::SolidLine, Qt::SquareCap));
    
    // 左上
    p.drawLine(QPointF(r.left() + 2, r.top() + 2 + capL), QPointF(r.left() + 2, r.top() + 2));
    p.drawLine(QPointF(r.left() + 2, r.top() + 2), QPointF(r.left() + 2 + capL, r.top() + 2));
    // 右上
    p.drawLine(QPointF(r.right() - 2 - capL, r.top() + 2), QPointF(r.right() - 2, r.top() + 2));
    p.drawLine(QPointF(r.right() - 2, r.top() + 2), QPointF(r.right() - 2, r.top() + 2 + capL));
    // 左下
    p.drawLine(QPointF(r.left() + 2, r.bottom() - 2 - capL), QPointF(r.left() + 2, r.bottom() - 2));
    p.drawLine(QPointF(r.left() + 2, r.bottom() - 2), QPointF(r.left() + 2 + capL, r.bottom() - 2));
    // 右下
    p.drawLine(QPointF(r.right() - 2 - capL, r.bottom() - 2), QPointF(r.right() - 2, r.bottom() - 2));
    p.drawLine(QPointF(r.right() - 2, r.bottom() - 2), QPointF(r.right() - 2, r.bottom() - 2 - capL));

    p.restore();
}

/* 绘制 HUD 等宽防抖数字 */
void drawHudTabularNum(QPainter &p, const QRectF &r, const QString &text, int px,
                       const QColor &c, int align = Qt::AlignCenter, bool bold = true)
{
    QFont f = p.font();
    f.setPixelSize(px);
    f.setBold(bold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0.0);
    p.setFont(f);
    p.setPen(c);

    const QString s = text.isEmpty() ? QStringLiteral("--") : text;
    const QFontMetricsF fm(f);
    const qreal charW = qMax<qreal>(1.0, fm.horizontalAdvance(QLatin1Char('0')));
    const int len = s.length();

    qreal startX = r.left();
    if (align & Qt::AlignRight) {
        startX = r.right() - len * charW;
    } else if (align & Qt::AlignHCenter) {
        startX = r.left() + (r.width() - len * charW) / 2.0;
    }

    for (int i = 0; i < len; ++i) {
        p.drawText(QRectF(startX + i * charW, r.top(), charW, r.height()),
                   Qt::AlignCenter, s.mid(i, 1));
    }
}

/* 绘制 HUD 文本 */
void drawHudLabel(QPainter &p, const QRectF &r, const QString &s, int px,
                  const QColor &c, int align = Qt::AlignLeft | Qt::AlignVCenter,
                  bool bold = false, qreal tracking = 0.0)
{
    QFont f = p.font();
    f.setPixelSize(px);
    f.setBold(bold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, tracking);
    p.setFont(f);
    p.setPen(c);
    p.drawText(r, align, s);
}

void drawTireHudCards(CarStatusPanel *panel, QPainter &p, const QRectF &carArea, CanMgr *m_can, bool m_blinkOn)
{
    p.save();
    const qreal cx = carArea.center().x();
    const qreal cy = carArea.center().y();

    // 四卡位置坐标 (紧密布局在车模四角)
    const QRectF tireCards[4] = {
        QRectF(cx - 195, cy - 170, 95, 80), // 左前 FL
        QRectF(cx + 100, cy - 170, 95, 80), // 右前 FR
        QRectF(cx - 195, cy + 90,  95, 80), // 左后 RL
        QRectF(cx + 100, cy + 90,  95, 80)  // 右后 RR
    };

    const QString posNames[4] = { QStringLiteral("左前轮 FL"), QStringLiteral("右前轮 FR"), QStringLiteral("左后轮 RL"), QStringLiteral("右后轮 RR") };

    for (int i = 0; i < 4; ++i) {
        QRectF tr = tireCards[i];
        CanMgr::TirePos tpos = static_cast<CanMgr::TirePos>(i);
        OneTire tInfo = m_can ? m_can->tire(tpos) : OneTire();

        bool hasWarn = m_can && m_can->faultFresh() && m_can->warnActive(CanMgr::WarnTire);
        bool abnormal = hasWarn || (tInfo.valid && (tInfo.kpa < 180 || tInfo.kpa > 280));

        // 卡片底与高亮
        QColor accent = !tInfo.valid ? kBorder : (!tInfo.fresh ? kTextStale : (abnormal ? kRed : kGreen));
        drawHudCard(p, tr, accent, abnormal && m_blinkOn, 6);

        // 轮位名称
        drawHudLabel(p, QRectF(tr.left() + 6, tr.top() + 4, tr.width() - 12, 18), posNames[i], 15, kTextMid, Qt::AlignLeft);

        // 胎压数值 (kPa -> Bar 转换显示更直观，如 2.3 bar / 230 kPa)
        QString kpaStr = tInfo.valid ? QString::number(tInfo.kpa) : QStringLiteral("--");
        QColor numCol = !tInfo.valid ? kTextMute : (!tInfo.fresh ? kTextStale : (abnormal ? kRed : kTextHi));
        drawHudTabularNum(p, QRectF(tr.left() + 6, tr.top() + 24, 55, 26), kpaStr, 18, numCol, Qt::AlignLeft | Qt::AlignVCenter, true);
        drawHudLabel(p, QRectF(tr.left() + 62, tr.top() + 28, 30, 20), QStringLiteral("kPa"), 15, kTextMute);

        // 胎温数值
        QString tempStr = (tInfo.valid && tInfo.tempValid) ? QString("%1℃").arg(tInfo.tempC) : QStringLiteral("--℃");
        drawHudLabel(p, QRectF(tr.left() + 6, tr.top() + 52, tr.width() - 12, 22), QStringLiteral("温度: %1").arg(tempStr), 15, !tInfo.valid ? kTextMute : (!tInfo.fresh ? kTextStale : kTextMid));

        // 连线：从卡片引向对应车轮
        p.setPen(QPen(rgba(accent, 120), 1.0, Qt::DashLine));
        qreal targetX = (i % 2 == 0) ? cx - 68.0 : cx + 68.0;
        qreal targetY = (i < 2) ? cy - 115.0 : cy + 105.0;
        p.drawLine(QPointF((i % 2 == 0) ? tr.right() : tr.left(), tr.center().y()), QPointF(targetX, targetY));
    }

    p.restore();
}

} // namespace

CarStatusPanel::CarStatusPanel(CanMgr *can, QWidget *parent)
    : QWidget(parent), m_can(can)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAttribute(Qt::WA_Hover, false);
    setFocusPolicy(Qt::NoFocus);

    // 1.5 Hz 闪烁定时器 (333ms 翻转周期，满足车载标准转向灯与警示闪烁节奏)
    connect(&m_blink, &QTimer::timeout, this, &CarStatusPanel::onBlink);
    m_blink.start(333);

    if (m_can) {
        connect(m_can, &CanMgr::changed, this, static_cast<void (QWidget::*)()>(&QWidget::update));
        connect(m_can, &CanMgr::timedOut, this, [this](int) { update(); });
    }
}

void CarStatusPanel::onBlink()
{
    m_blinkOn = !m_blinkOn;
    // 当存在闪烁信号（转向灯/双闪/胎压报警/严重故障）时触发局部刷新
    if (m_can && (m_can->leftTurn() || m_can->rightTurn() || m_can->hazard() ||
                  m_can->warnActive(CanMgr::WarnTire) || m_can->warnActive(CanMgr::WarnEngine))) {
        update();
    }
}

void CarStatusPanel::drawCard(QPainter &p, const QRectF &r, const QColor &bg,
                              const QColor &border, qreal radius)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(bg);
    p.setPen(border.isValid() ? QPen(border, 1.0) : Qt::NoPen);
    p.drawRoundedRect(r, radius, radius);
    p.restore();
}

void CarStatusPanel::drawTabularNum(QPainter &p, const QRectF &r, const QString &text, int px,
                                    const QColor &c, int align, bool bold)
{
    drawHudTabularNum(p, r, text, px, c, align, bold);
}

void CarStatusPanel::drawLabel(QPainter &p, const QRectF &r, const QString &s, int px,
                               const QColor &c, int align, bool bold, qreal tracking)
{
    drawHudLabel(p, r, s, px, c, align, bold, tracking);
}

void CarStatusPanel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const qreal cw = width();
    const qreal ch = height();
    if (cw <= 10 || ch <= 10)
        return;

    // 全局等比缩放矩阵（基准 1000x520 逻辑画布）
    const qreal sx = cw / qreal(LW);
    const qreal sy = ch / qreal(LH);
    const qreal s = qMin(sx, sy);

    p.save();
    // 居中偏移对齐
    const qreal ox = (cw - LW * s) / 2.0;
    const qreal oy = (ch - LH * s) / 2.0;
    p.translate(ox, oy);
    p.scale(s, s);

    const QRectF logicalRect(0, 0, LW, LH);

    // 1. 绘制科幻座舱网格底纹与全景光晕
    drawBackdrop(p, logicalRect);

    // 2. 绘制顶部座舱模式与环境 HUD 条
    drawTopStatusLine(p);

    // 3. 绘制左侧主驾驶 HUD 仪表群 (x: 20 ~ 290)
    drawLeftDashboard(p);

    // 4. 绘制右侧综合车况与故障遥测群 (x: 710 ~ 980)
    drawRightTelemetry(p);

    // 5. 绘制视觉中心：高精矢量写实车模 (中心 x: 300 ~ 700)
    const QRectF carCenterArea(300, 45, 400, 465);
    drawRealisticCar(p, carCenterArea);

    // 6. 绘制环绕车轮的四轮实时胎压胎温 HUD 卡片
    drawTireHudCards(this, p, carCenterArea, m_can, m_blinkOn);

    p.restore();
}

/* ----------------------------------------------------------------------------
 * 1. 绘制全景科技底纹网格
 * --------------------------------------------------------------------------*/
void CarStatusPanel::drawBackdrop(QPainter &p, const QRectF &r)
{
    p.save();
    // 底色填充
    QLinearGradient bgGrad(r.topLeft(), r.bottomLeft());
    bgGrad.setColorAt(0.0, QColor(0x0A, 0x10, 0x20));
    bgGrad.setColorAt(0.5, QColor(0x06, 0x0A, 0x14));
    bgGrad.setColorAt(1.0, QColor(0x04, 0x07, 0x0F));
    p.fillRect(r, bgGrad);

    // 科技感网格（关 AA 保持像素级锐利）
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(rgba(kGridLine, 70), 1, Qt::DotLine));
    const int step = 20;
    for (int x = step; x < r.width(); x += step) {
        p.drawLine(QPointF(x, 0), QPointF(x, r.height()));
    }
    for (int y = step; y < r.height(); y += step) {
        p.drawLine(QPointF(0, y), QPointF(r.width(), y));
    }

    // 中心车模停泊台光晕 (椭圆发光座)
    p.setRenderHint(QPainter::Antialiasing, true);
    QRadialGradient spot(r.center().x(), r.center().y() + 15, 230);
    spot.setColorAt(0.0, rgba(kCyan, 35));
    spot.setColorAt(0.6, rgba(kBlue, 15));
    spot.setColorAt(1.0, rgba(kBgDark, 0));
    p.setBrush(spot);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(r.center().x(), r.center().y() + 15), 230, 180);

    p.restore();
}

/* ----------------------------------------------------------------------------
 * 2. 顶部座舱状态条（工作模式、车锁、网络/环境概览）
 * --------------------------------------------------------------------------*/
void CarStatusPanel::drawTopStatusLine(QPainter &p)
{
    p.save();
    const QRectF bar(20, 8, 960, 32);

    // 背景细长槽
    p.setBrush(rgba(kCardBg, 160));
    p.setPen(QPen(rgba(kBorder, 140), 1.0));
    p.drawRoundedRect(bar, 6, 6);

    // 左侧：工作模式标识
    QString modeStr = QStringLiteral("驾驶模式: --");
    QColor modeCol = kTextMute;
    if (m_can && m_can->driveModeFresh()) {
        int m = m_can->driveMode();
        switch (m) {
        case CanMgr::ModeEco:    modeStr = QStringLiteral("◆ 驾驶模式: 经济节能 (ECO)"); modeCol = kGreen; break;
        case CanMgr::ModeNormal: modeStr = QStringLiteral("◆ 驾驶模式: 标准舒适 (NORMAL)"); modeCol = kBlue; break;
        case CanMgr::ModeSport:  modeStr = QStringLiteral("◆ 驾驶模式: 运动竞速 (SPORT)"); modeCol = kAmber; break;
        case CanMgr::ModeSnow:   modeStr = QStringLiteral("◆ 驾驶模式: 全时雪地 (SNOW)"); modeCol = kPurple; break;
        default: modeStr = QStringLiteral("◆ 驾驶模式: 未配置"); modeCol = kTextMid; break;
        }
    }
    drawHudLabel(p, QRectF(32, 8, 300, 32), modeStr, 15, modeCol, Qt::AlignLeft | Qt::AlignVCenter, true);

    // 中间：中央电子防盗与四门安全锁
    bool locked = m_can && m_can->carLocked();
    bool doorFresh = m_can && m_can->doorFresh();
    QString lockStr = !doorFresh ? QStringLiteral("中央安全锁: --") :
                      (locked ? QStringLiteral("🔒 中央安全锁: 已设防锁定") : QStringLiteral("🔓 中央安全锁: 解锁状态"));
    QColor lockCol = !doorFresh ? kTextStale : (locked ? kGreen : kAmber);
    drawHudLabel(p, QRectF(380, 8, 240, 32), lockStr, 15, lockCol, Qt::AlignCenter, true);

    // 右侧：车外环境温度
    int outT = m_can ? m_can->outsideTemp() : 0;
    bool lightFresh = m_can && m_can->lightFresh();
    QString envStr = QStringLiteral("车外环境: -- ℃");
    QColor envCol = kTextMute;
    if (lightFresh && outT != 0) {
        envStr = QStringLiteral("车外气温: %1 ℃").arg(outT);
        envCol = kTextHi;
    } else if (lightFresh) {
        envStr = QStringLiteral("车外气温: -- ℃");
    }
    drawHudLabel(p, QRectF(680, 8, 285, 32), envStr, 15, envCol, Qt::AlignRight | Qt::AlignVCenter, false);

    p.restore();
}

/* ----------------------------------------------------------------------------
 * 3. 左侧 HUD 仪表群：车速大数、档位条、驾驶状态指示阵列
 * --------------------------------------------------------------------------*/
void CarStatusPanel::drawLeftDashboard(QPainter &p)
{
    p.save();
    // 1. 车速与能量表主卡片 (20, 48, 270, 240)
    const QRectF spCard(20, 48, 270, 240);
    drawHudCard(p, spCard, kCyan, true, 10);

    drawHudLabel(p, QRectF(35, 58, 140, 22), QStringLiteral("VEHICLE SPEED"), 15, kCyan, Qt::AlignLeft, true, 1.2);
    drawHudLabel(p, QRectF(180, 58, 95, 22), QStringLiteral("瞬时车速"), 15, kTextMid, Qt::AlignRight);

    // 车速大数展示
    int sp = m_can ? m_can->speedKmh() : 0;
    bool spValid = m_can && m_can->speedValid();
    bool spFresh = m_can && m_can->speedFresh();
    QColor spCol = !spValid ? kTextMute : (!spFresh ? kTextStale : kTextHi);
    QString spStr = spValid ? QString::number(sp) : QStringLiteral("--");

    drawHudTabularNum(p, QRectF(30, 82, 160, 68), spStr, 56, spCol, Qt::AlignCenter, true);
    drawHudLabel(p, QRectF(195, 114, 80, 26), QStringLiteral("km/h"), 18, kCyan, Qt::AlignLeft | Qt::AlignVCenter, true);

    // 车速分段刻度动态能量条 (0 ~ 160 km/h)
    const QRectF barArea(35, 158, 240, 14);
    const int segCount = 20;
    const qreal frac = spValid ? qBound(0.0, sp / 160.0, 1.0) : 0.0;
    const int litSegs = qRound(frac * segCount);
    const qreal segW = (barArea.width() - (segCount - 1) * 3.0) / segCount;

    for (int i = 0; i < segCount; ++i) {
        QRectF segR(barArea.left() + i * (segW + 3.0), barArea.top(), segW, barArea.height());
        if (i < litSegs) {
            // 能量颜色渐变：低速青 -> 中速蓝 -> 高速琥珀 -> 超速红
            QColor sc = (i > 16) ? kRed : ((i > 12) ? kAmber : kCyan);
            p.setBrush(!spFresh ? kTextStale : sc);
        } else {
            p.setBrush(rgba(kGridLine, 140));
        }
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(segR, 2, 2);
    }

    // 档位选择条 (P R N D)
    const QString gears[4] = { QStringLiteral("P"), QStringLiteral("R"), QStringLiteral("N"), QStringLiteral("D") };
    QString curGear = m_can ? m_can->gearText() : QStringLiteral("--");
    bool gearFresh = m_can && m_can->gearFresh();

    const qreal gw = 54.0;
    const qreal gx0 = 35.0;
    for (int i = 0; i < 4; ++i) {
        QRectF gr(gx0 + i * (gw + 8.0), 184, gw, 36);
        bool match = (curGear == gears[i]);
        QColor gc = match ? (gears[i] == "R" ? kRed : (gears[i] == "P" ? kAmber : kCyan)) : kBorder;
        
        p.setBrush(match ? rgba(gc, 50) : rgba(kCardBg, 180));
        p.setPen(QPen(match ? gc : rgba(kBorder, 150), match ? 1.8 : 1.0));
        p.drawRoundedRect(gr, 6, 6);

        QColor tc = !gearFresh ? kTextStale : (match ? gc : kTextMute);
        drawHudLabel(p, gr, gears[i], 20, tc, Qt::AlignCenter, true);
    }

    // 2. 驾驶安全与动态指示灯矩阵卡片 (20, 298, 270, 212)
    const QRectF lampCard(20, 298, 270, 212);
    drawHudCard(p, lampCard, kBorderHi, false, 10);
    drawHudLabel(p, QRectF(35, 308, 240, 22), QStringLiteral("ACTIVE SAFETY 动态安全"), 15, kTextMid, Qt::AlignLeft, true);

    // 4 组关键行指示 (刹车系统 / 倒车雷达 / 安全带 / 驻车电子手刹)
    bool brakeOn    = m_can && m_can->brake();
    bool reverseOn  = m_can && m_can->reverse();
    bool beltOff    = m_can && m_can->seatbeltOff();
    bool handbrake  = m_can && m_can->handbrake();
    bool driveFresh = m_can && m_can->speedFresh();

    auto drawIndicatorRow = [&](qreal y, const QString &icon, const QString &name, bool active, const QColor &actCol, const QString &actText, const QString &inactText) {
        QRectF rowR(32, y, 246, 36);
        bool on = driveFresh && active;
        p.setBrush(on ? rgba(actCol, 40) : rgba(kPanelBg, 160));
        p.setPen(QPen(on ? actCol : rgba(kBorder, 100), on ? 1.4 : 0.8));
        p.drawRoundedRect(rowR, 5, 5);

        // 图标徽标
        drawHudLabel(p, QRectF(rowR.left() + 8, rowR.top(), 30, 36), icon, 17, on ? actCol : kTextMute, Qt::AlignCenter, true);
        // 名称
        drawHudLabel(p, QRectF(rowR.left() + 42, rowR.top(), 110, 36), name, 15, on ? kTextHi : kTextMid, Qt::AlignLeft | Qt::AlignVCenter);
        // 状态文字
        QString stText = !driveFresh ? QStringLiteral("--") : (active ? actText : inactText);
        QColor stCol = !driveFresh ? kTextStale : (active ? actCol : kTextMute);
        drawHudLabel(p, QRectF(rowR.right() - 85, rowR.top(), 75, 36), stText, 15, stCol, Qt::AlignRight | Qt::AlignVCenter, true);
    };

    drawIndicatorRow(336, QStringLiteral("⯄"), QStringLiteral("行车制动"), brakeOn, kRed, QStringLiteral("BRAKE 制动"), QStringLiteral("正常"));
    drawIndicatorRow(378, QStringLiteral("⮌"), QStringLiteral("倒车系统"), reverseOn, kCyan, QStringLiteral("R 倒车中"), QStringLiteral("就绪"));
    drawIndicatorRow(420, QStringLiteral("⚠"), QStringLiteral("主驾安全带"), beltOff, kRed, (m_blinkOn ? QStringLiteral("未系告警") : QStringLiteral("未系")), QStringLiteral("已系扣"));
    drawIndicatorRow(462, QStringLiteral("Ⓟ"), QStringLiteral("电子驻车手刹"), handbrake, kAmber, QStringLiteral("PARK 驻车"), QStringLiteral("已释放"));

    p.restore();
}

/* ----------------------------------------------------------------------------
 * 4. 右侧 HUD 车况遥测群：里程/油耗/续航/全车故障诊断
 * --------------------------------------------------------------------------*/
void CarStatusPanel::drawRightTelemetry(QPainter &p)
{
    p.save();
    // 1. 行程与能量续航卡片 (710, 48, 270, 240)
    const QRectF odoCard(710, 48, 270, 240);
    drawHudCard(p, odoCard, kBlue, true, 10);

    drawHudLabel(p, QRectF(725, 58, 140, 22), QStringLiteral("TELEMETRY"), 15, kBlue, Qt::AlignLeft, true, 1.2);
    drawHudLabel(p, QRectF(860, 58, 105, 22), QStringLiteral("行车遥测"), 15, kTextMid, Qt::AlignRight);

    // 总里程 ODO & 小计 TRIP
    double odoVal = m_can ? m_can->odoKm() : 0.0;
    bool odoValid = m_can && m_can->odoValid();
    bool odoFresh = m_can && m_can->odoFresh();
    QString odoStr = odoValid ? QString::asprintf("%.1f", odoVal) : QStringLiteral("--.-");
    QColor odoCol = !odoValid ? kTextMute : (!odoFresh ? kTextStale : kTextHi);

    drawHudLabel(p, QRectF(725, 84, 80, 20), QStringLiteral("总里程 ODO"), 15, kTextMute);
    drawHudTabularNum(p, QRectF(725, 104, 160, 32), odoStr, 24, odoCol, Qt::AlignLeft | Qt::AlignVCenter, true);
    drawHudLabel(p, QRectF(890, 112, 40, 20), QStringLiteral("km"), 15, kTextMid);

    double tripVal = m_can ? m_can->tripKm() : 0.0;
    bool tripValid = m_can && m_can->tripValid();
    QString tripStr = tripValid ? QString::asprintf("%.1f", tripVal) : QStringLiteral("--.-");
    drawHudLabel(p, QRectF(725, 138, 80, 20), QStringLiteral("小计 TRIP"), 15, kTextMute);
    drawHudTabularNum(p, QRectF(725, 156, 160, 26), tripStr, 20, !tripValid ? kTextMute : (!odoFresh ? kTextStale : kTextMid), Qt::AlignLeft | Qt::AlignVCenter);
    drawHudLabel(p, QRectF(890, 160, 40, 20), QStringLiteral("km"), 15, kTextMute);

    // 剩余油量与估算续航里程
    int fuelP = m_can ? m_can->fuelPercent() : 0;
    bool fuelValid = m_can && m_can->fuelValid();
    int rangeV = m_can ? m_can->rangeKm() : 0;
    bool rangeValid = m_can && m_can->rangeValid();

    const QRectF fuelBox(725, 188, 240, 46);
    p.setBrush(rgba(kPanelBg, 200));
    p.setPen(QPen(rgba(kBorder, 150), 1.0));
    p.drawRoundedRect(fuelBox, 6, 6);

    // 油量分段指示
    drawHudLabel(p, QRectF(735, 192, 50, 18), QStringLiteral("油量"), 15, kTextMute);
    QString fStr = fuelValid ? QString("%1%").arg(fuelP) : QStringLiteral("--%");
    QColor fCol = !fuelValid ? kTextMute : (!odoFresh ? kTextStale : (fuelP < 15 ? kRed : kGreen));
    drawHudLabel(p, QRectF(785, 192, 50, 18), fStr, 15, fCol, Qt::AlignLeft | Qt::AlignVCenter, true);

    // 续航
    drawHudLabel(p, QRectF(845, 192, 50, 18), QStringLiteral("续航"), 15, kTextMute);
    QString rStr = rangeValid ? QString::number(rangeV) : QStringLiteral("--");
    drawHudLabel(p, QRectF(890, 192, 65, 18), QString("%1 km").arg(rStr), 15, !rangeValid ? kTextMute : (!odoFresh ? kTextStale : kCyan), Qt::AlignRight | Qt::AlignVCenter, true);

    // 油量条
    const QRectF fBar(735, 214, 220, 8);
    p.setBrush(rgba(kGridLine, 150));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(fBar, 2, 2);
    if (fuelValid && fuelP > 0) {
        qreal fw = fBar.width() * qBound(0.0, fuelP / 100.0, 1.0);
        p.setBrush(!odoFresh ? kTextStale : (fuelP < 15 ? kRed : kGreen));
        p.drawRoundedRect(QRectF(fBar.left(), fBar.top(), fw, fBar.height()), 2, 2);
    }

    // 2. 动力诊断与全车告警灯阵卡片 (710, 298, 270, 212)
    const QRectF diagCard(710, 298, 270, 212);
    drawHudCard(p, diagCard, kBorderHi, false, 10);
    drawHudLabel(p, QRectF(725, 308, 240, 22), QStringLiteral("DIAGNOSTICS 系统诊断"), 15, kTextMid, Qt::AlignLeft, true);

    // 故障码状态条
    int dtcCount = m_can ? m_can->dtcCount() : 0;
    QString dtcText = m_can ? m_can->dtcText() : QStringLiteral("无");
    bool faultFresh = m_can && m_can->faultFresh();

    const QRectF dtcBox(725, 334, 240, 36);
    bool hasDtc = faultFresh && (dtcCount > 0);
    p.setBrush(hasDtc ? rgba(kRed, 35) : rgba(kPanelBg, 180));
    p.setPen(QPen(hasDtc ? kRed : rgba(kBorder, 120), hasDtc ? 1.5 : 1.0));
    p.drawRoundedRect(dtcBox, 5, 5);

    drawHudLabel(p, QRectF(735, 334, 100, 36), QStringLiteral("故障诊断码"), 15, hasDtc ? kRed : kTextMid);
    QString dtcShow = !faultFresh ? QStringLiteral("--") : (hasDtc ? QString("%1 (%2项)").arg(dtcText).arg(dtcCount) : QStringLiteral("✔ 无故障"));
    drawHudLabel(p, QRectF(835, 334, 120, 36), dtcShow, 15, !faultFresh ? kTextStale : (hasDtc ? kRed : kGreen), Qt::AlignRight | Qt::AlignVCenter, true);

    // 8 位告警灯网格 (2行 x 4列)
    const struct { CanMgr::WarnBit bit; const char *name; } kDiagWarns[8] = {
        { CanMgr::WarnEngine,   "发动机" },
        { CanMgr::WarnTire,     "胎压" },
        { CanMgr::WarnLowFuel,  "低油量" },
        { CanMgr::WarnService,  "保养" },
        { CanMgr::WarnAbs,      "ABS" },
        { CanMgr::WarnBattery,  "蓄电池" },
        { CanMgr::WarnBrake,    "制动系" },
        { CanMgr::WarnOther,    "其它" }
    };

    const qreal wgw = 56.0;
    const qreal wgh = 34.0;
    for (int i = 0; i < 8; ++i) {
        int row = i / 4;
        int col = i % 4;
        QRectF wr(725 + col * (wgw + 5.0), 378 + row * (wgh + 6.0), wgw, wgh);
        bool act = faultFresh && m_can->warnActive(kDiagWarns[i].bit);
        
        QColor wc = act ? (m_blinkOn ? kRed : rgba(kRed, 140)) : kTextMute;
        p.setBrush(act ? rgba(kRed, 45) : rgba(kPanelBg, 160));
        p.setPen(QPen(act ? wc : rgba(kBorder, 80), act ? 1.4 : 0.8));
        p.drawRoundedRect(wr, 4, 4);

        drawHudLabel(p, wr, QString::fromUtf8(kDiagWarns[i].name), 15, wc, Qt::AlignCenter, act);
    }

    drawHudLabel(p, QRectF(725, 465, 240, 35), QStringLiteral("瞬时油耗: %1  平均: %2").arg(
                     m_can && m_can->instFuelValid() ? QString::asprintf("%.1f L", m_can->instFuelL100()) : QStringLiteral("--.-"),
                     m_can && m_can->avgFuelValid() ? QString::asprintf("%.1f L", m_can->avgFuelL100()) : QStringLiteral("--.-")),
                 15, kTextMid, Qt::AlignCenter);

    p.restore();
}

/* ----------------------------------------------------------------------------
 * 5. 视觉中心：写实科技座舱矢量车模 (QPainter 极致拟真建模)
 * --------------------------------------------------------------------------*/
void CarStatusPanel::drawRealisticCar(QPainter &p, const QRectF &carArea)
{
    p.save();
    // 车模基准几何尺寸 (俯视流线型轿跑，长 380px，宽 160px，居中放置)
    const qreal carW = 160.0;
    const qreal carH = 380.0;
    const qreal cx = carArea.center().x();
    const qreal cy = carArea.center().y();
    const QRectF bodyRect(cx - carW / 2.0, cy - carH / 2.0, carW, carH);

    // 1. 车身投影与科幻底盘环境光晕
    drawCarShadow(p, bodyRect);

    // 2. 车轮、高光刹车盘与红色制动卡钳 (带四轮胎压告警反馈)
    drawTiresAndBrakes(p, bodyRect);

    // 3. 前大灯真实光学矩阵光束 (近光短斜 / 远光长直，遵照协议 50%/100% 亮度)
    drawHeadlightBeams(p, bodyRect);

    // 4. 金属漆双层高光外壳、前后保杠、流线腰线
    drawCarBodyShell(p, bodyRect);

    // 5. 黑色全景天幕玻璃、挡风反光渐变、座舱内部视效
    drawCockpitAndGlass(p, bodyRect);

    // 6. 四车门向外旋开动态机械铰链与危险警示光晕、前后机盖
    drawDoorsAndFlaps(p, bodyRect);

    // 7. 前后灯罩光效（LED 日行灯、远近光发光体、贯穿式红色尾灯、转向灯 1.5Hz 闪烁）
    drawCarLamps(p, bodyRect);

    p.restore();
}

/* 5.1 投影与底盘氛围光晕 */
void CarStatusPanel::drawCarShadow(QPainter &p, const QRectF &b)
{
    // 底盘氛围灯 (根据协议灯光 bit5 氛围灯联动)
    bool ambientOn = m_can && m_can->lightFresh() && m_can->lightOn(5);
    QRadialGradient ambGlow(b.center(), b.width() * 0.9);
    ambGlow.setColorAt(0.0, ambientOn ? rgba(kCyan, 70) : rgba(kBlue, 20));
    ambGlow.setColorAt(0.7, ambientOn ? rgba(kCyan, 20) : rgba(kBgDark, 10));
    ambGlow.setColorAt(1.0, rgba(kBgDark, 0));
    p.setBrush(ambGlow);
    p.setPen(Qt::NoPen);
    p.drawEllipse(b.center(), b.width() * 0.95, b.height() * 0.52);

    // 车身深色硬阴影
    QPainterPath shadowPath;
    shadowPath.addRoundedRect(b.adjusted(-4, -6, 4, 8), 38, 38);
    p.fillPath(shadowPath, rgba(QColor(0x02, 0x04, 0x08), 180));
}

/* 5.2 轮胎与轮毂 */
void CarStatusPanel::drawTiresAndBrakes(QPainter &p, const QRectF &b)
{
    // 四轮相对坐标 (左前/右前/左后/右后)
    const qreal tw = 26.0;
    const qreal th = 60.0;
    const qreal xOffset = b.width() / 2.0 - 12.0;
    const qreal yF = b.top() + 75.0;
    const qreal yR = b.bottom() - 85.0;

    const QPointF wheelCenters[4] = {
        QPointF(b.center().x() - xOffset, yF), // FL
        QPointF(b.center().x() + xOffset, yF), // FR
        QPointF(b.center().x() - xOffset, yR), // RL
        QPointF(b.center().x() + xOffset, yR)  // RR
    };

    for (int i = 0; i < 4; ++i) {
        QPointF cp = wheelCenters[i];
        QRectF tr(cp.x() - tw / 2.0, cp.y() - th / 2.0, tw, th);

        // 胎压状态获取
        CanMgr::TirePos tpos = static_cast<CanMgr::TirePos>(i);
        OneTire tireInfo = m_can ? m_can->tire(tpos) : OneTire();
        bool hasWarn = m_can && m_can->faultFresh() && m_can->warnActive(CanMgr::WarnTire);
        bool tireWarn = hasWarn || (tireInfo.valid && (tireInfo.kpa < 180 || tireInfo.kpa > 280));

        // 若异常，轮胎外圈发光
        if (tireWarn && m_blinkOn) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(rgba(kRed, 180), 3.0));
            p.drawRoundedRect(tr.adjusted(-3, -3, 3, 3), 6, 6);
        }

        // 橡胶轮胎（深灰黑带有胎纹质感）
        QLinearGradient tireGrad(tr.topLeft(), tr.topRight());
        tireGrad.setColorAt(0.0, QColor(0x15, 0x18, 0x20));
        tireGrad.setColorAt(0.3, QColor(0x28, 0x2D, 0x38));
        tireGrad.setColorAt(0.7, QColor(0x28, 0x2D, 0x38));
        tireGrad.setColorAt(1.0, QColor(0x15, 0x18, 0x20));
        p.setBrush(tireGrad);
        p.setPen(QPen(tireWarn ? kRed : QColor(0x35, 0x3F, 0x50), 1.2));
        p.drawRoundedRect(tr, 5, 5);

        // 轮胎胎纹沟槽
        p.setPen(QPen(QColor(0x12, 0x15, 0x1C), 1.0));
        for (qreal y = tr.top() + 6; y < tr.bottom() - 4; y += 8) {
            p.drawLine(QPointF(tr.left() + 2, y), QPointF(tr.right() - 2, y));
        }

        // 高光金属刹车盘
        QRectF discR(cp.x() - 7, cp.y() - 15, 14, 30);
        QLinearGradient discGrad(discR.topLeft(), discR.bottomRight());
        discGrad.setColorAt(0.0, QColor(0xC0, 0xD0, 0xE0));
        discGrad.setColorAt(1.0, QColor(0x60, 0x70, 0x85));
        p.setBrush(discGrad);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(discR, 3, 3);

        // 红色运动制动卡钳
        QRectF calR(cp.x() - 8, cp.y() - 12, 6, 12);
        if (i % 2 == 1) calR.moveLeft(cp.x() + 2);
        p.setBrush(kRed);
        p.drawRoundedRect(calR, 2, 2);
    }
}

/* 5.3 大灯光学矩阵光束 */
void CarStatusPanel::drawHeadlightBeams(QPainter &p, const QRectF &b)
{
    bool fresh = m_can && m_can->lightFresh();
    bool lowBeam  = fresh && m_can->lightOn(0);
    bool highBeam = fresh && m_can->lightOn(1);

    if (!lowBeam && !highBeam)
        return;

    const qreal lx = b.center().x() - 48.0;
    const qreal rx = b.center().x() + 48.0;
    const qreal y0 = b.top() + 15.0;

    // 协议约定：近光 50% 亮度短斜，远光 100% 亮度长直
    if (highBeam) {
        // 远光：长直宽束、100% 全亮白蓝渐变
        QLinearGradient hBeam(0, y0, 0, y0 - 180);
        hBeam.setColorAt(0.0, rgba(QColor(0xFF, 0xFF, 0xFF), 230));
        hBeam.setColorAt(0.2, rgba(kBlue, 180));
        hBeam.setColorAt(0.8, rgba(kCyan, 60));
        hBeam.setColorAt(1.0, rgba(kCyan, 0));

        QPainterPath beamPath;
        beamPath.moveTo(lx - 12, y0);
        beamPath.lineTo(lx - 55, y0 - 180);
        beamPath.lineTo(rx + 55, y0 - 180);
        beamPath.lineTo(rx + 12, y0);
        beamPath.closeSubpath();

        p.fillPath(beamPath, hBeam);
    } else if (lowBeam) {
        // 近光：短斜扇形、50% 半亮青绿渐变
        QLinearGradient lBeam(0, y0, 0, y0 - 100);
        lBeam.setColorAt(0.0, rgba(kCyan, 150));
        lBeam.setColorAt(0.4, rgba(kCyan, 80));
        lBeam.setColorAt(1.0, rgba(kCyan, 0));

        // 左侧扇形
        QPainterPath leftBeam;
        leftBeam.moveTo(lx - 6, y0);
        leftBeam.lineTo(lx - 45, y0 - 95);
        leftBeam.lineTo(lx + 15, y0 - 95);
        leftBeam.lineTo(lx + 6, y0);
        leftBeam.closeSubpath();
        p.fillPath(leftBeam, lBeam);

        // 右侧扇形
        QPainterPath rightBeam;
        rightBeam.moveTo(rx - 6, y0);
        rightBeam.lineTo(rx - 15, y0 - 95);
        rightBeam.lineTo(rx + 45, y0 - 95);
        rightBeam.lineTo(rx + 6, y0);
        rightBeam.closeSubpath();
        p.fillPath(rightBeam, lBeam);
    }
}

/* 5.4 车身金属外壳与流线型腰线 */
void CarStatusPanel::drawCarBodyShell(QPainter &p, const QRectF &b)
{
    // 主流线型轿跑车身轮廓 (贝塞尔高精曲线)
    QPainterPath shell;
    const qreal cx = b.center().x();
    const qreal l = b.left() + 8.0;
    const qreal r = b.right() - 8.0;
    const qreal t = b.top() + 10.0;
    const qreal bot = b.bottom() - 10.0;

    shell.moveTo(cx, t);
    // 车头流线 (弧形向下过渡到前大灯角)
    shell.cubicTo(cx + 42, t, r - 12, t + 12, r - 6, t + 45);
    // 前轮拱凹凸造型
    shell.cubicTo(r + 2, t + 65, r + 2, t + 95, r - 4, t + 115);
    // 侧门流线型内收与后轮拱宽体
    shell.cubicTo(r - 12, t + 170, r - 12, t + 240, r + 4, t + 285);
    shell.cubicTo(r + 6, t + 305, r + 2, t + 335, r - 15, t + 355);
    // 车尾流线与贯穿式尾翼
    shell.cubicTo(r - 30, bot, cx + 35, bot, cx, bot);
    shell.cubicTo(cx - 35, bot, l + 30, bot, l + 15, t + 355);
    shell.cubicTo(l - 2, t + 335, l - 6, t + 305, l - 4, t + 285);
    shell.cubicTo(l + 12, t + 240, l + 12, t + 170, l + 4, t + 115);
    shell.cubicTo(l - 2, t + 95, l - 2, t + 65, l + 6, t + 45);
    shell.cubicTo(l + 12, t + 12, cx - 42, t, cx, t);
    shell.closeSubpath();

    // 双层高光深邃金属漆渐变 (科技深空灰蓝)
    QLinearGradient paintGrad(b.topLeft(), b.bottomRight());
    paintGrad.setColorAt(0.0, QColor(0x28, 0x36, 0x4E));
    paintGrad.setColorAt(0.3, QColor(0x1C, 0x25, 0x38));
    paintGrad.setColorAt(0.5, QColor(0x2A, 0x3A, 0x54));
    paintGrad.setColorAt(0.8, QColor(0x16, 0x1E, 0x2E));
    paintGrad.setColorAt(1.0, QColor(0x22, 0x30, 0x46));

    p.setBrush(paintGrad);
    p.setPen(QPen(QColor(0x4F, 0x6E, 0x9C), 1.5));
    p.drawPath(shell);

    // 车顶与引擎盖立体特征高光筋线
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(rgba(QColor(0x8A, 0xBA, 0xF5), 140), 1.2));
    // 引擎盖左导流线
    QPainterPath hoodL;
    hoodL.moveTo(cx - 24, t + 15);
    hoodL.cubicTo(cx - 28, t + 45, cx - 42, t + 75, cx - 48, t + 95);
    p.drawPath(hoodL);
    // 引擎盖右导流线
    QPainterPath hoodR;
    hoodR.moveTo(cx + 24, t + 15);
    hoodR.cubicTo(cx + 28, t + 45, cx + 42, t + 75, cx + 48, t + 95);
    p.drawPath(hoodR);

    // 引擎盖前沿镀铬 LOGO 饰条
    QRectF logoR(cx - 10, t + 16, 20, 4);
    p.setBrush(QColor(0xD0, 0xE4, 0xFF));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(logoR, 2, 2);

    // 后视镜 (带有集成式 LED 转向灯)
    bool leftTurn  = m_can && m_can->leftTurn();
    bool rightTurn = m_can && m_can->rightTurn();
    bool hazard    = m_can && m_can->hazard();

    auto drawMirror = [&](qreal mx, qreal my, bool isRight, bool turnOn) {
        QRectF mr(mx - 8, my - 6, 16, 12);
        p.setBrush(QColor(0x1F, 0x29, 0x3D));
        p.setPen(QPen(QColor(0x4F, 0x6E, 0x9C), 1.0));
        p.drawRoundedRect(mr, 3, 3);
        // 转向灯光条
        if (turnOn && m_blinkOn) {
            p.setBrush(kAmber);
            p.setPen(QPen(rgba(kAmber, 180), 2.0));
            p.drawRoundedRect(QRectF(isRight ? mr.right() - 4 : mr.left(), mr.top() + 2, 4, mr.height() - 4), 1, 1);
        }
    };
    drawMirror(l - 4, t + 105, false, leftTurn || hazard);
    drawMirror(r + 4, t + 105, true, rightTurn || hazard);
}

/* 5.5 黑色全景天幕玻璃与座舱 */
void CarStatusPanel::drawCockpitAndGlass(QPainter &p, const QRectF &b)
{
    const qreal cx = b.center().x();
    const qreal t = b.top();

    // 全景黑曜玻璃座舱舱盖
    QPainterPath glass;
    glass.moveTo(cx - 38, t + 105);
    glass.lineTo(cx + 38, t + 105);
    glass.cubicTo(cx + 46, t + 175, cx + 45, t + 265, cx + 36, t + 315);
    glass.lineTo(cx - 36, t + 315);
    glass.cubicTo(cx - 45, t + 265, cx - 46, t + 175, cx - 38, t + 105);
    glass.closeSubpath();

    // 玻璃全景反光渐变 (极具通透质感)
    QLinearGradient glassGrad(b.topLeft(), b.bottomRight());
    glassGrad.setColorAt(0.0, QColor(0x08, 0x0E, 0x1A));
    glassGrad.setColorAt(0.3, QColor(0x14, 0x22, 0x36));
    glassGrad.setColorAt(0.6, QColor(0x0A, 0x12, 0x22));
    glassGrad.setColorAt(1.0, QColor(0x06, 0x0B, 0x14));

    p.setBrush(glassGrad);
    p.setPen(QPen(QColor(0x35, 0x4A, 0x6A), 1.2));
    p.drawPath(glass);

    // 前挡风玻璃光影高光反射条 (对角斜切高光)
    QPainterPath glare;
    glare.moveTo(cx - 34, t + 110);
    glare.lineTo(cx - 10, t + 110);
    glare.lineTo(cx - 28, t + 160);
    glare.lineTo(cx - 36, t + 160);
    glare.closeSubpath();
    p.fillPath(glare, rgba(QColor(0x9E, 0xCB, 0xFF), 55));

    // 后挡风除霜发热丝与高位刹车灯基座
    p.setPen(QPen(rgba(QColor(0x40, 0x58, 0x7A), 80), 0.8));
    for (qreal gy = t + 275; gy < t + 305; gy += 7) {
        p.drawLine(QPointF(cx - 30, gy), QPointF(cx + 30, gy));
    }
}

/* 5.6 四车门向外旋开与前后盖板 */
void CarStatusPanel::drawDoorsAndFlaps(QPainter &p, const QRectF &b)
{
    const qreal cx = b.center().x();
    const qreal l = b.left() + 10.0;
    const qreal r = b.right() - 10.0;
    const qreal t = b.top();

    bool doorFresh = m_can && m_can->doorFresh();
    // 门位：0 左前 1 右前 2 左后 3 右后 4 后备箱 5 引擎盖
    bool dFL = doorFresh && m_can->doorOpen(0);
    bool dFR = doorFresh && m_can->doorOpen(1);
    bool dRL = doorFresh && m_can->doorOpen(2);
    bool dRR = doorFresh && m_can->doorOpen(3);
    bool dTrunk = doorFresh && m_can->doorOpen(4);
    bool dHood  = doorFresh && m_can->doorOpen(5);

    auto drawSingleDoor = [&](qreal pivotX, qreal pivotY, qreal endX, qreal endY, bool open, bool isRight) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);

        if (open) {
            // 车门向外旋开：平移到旋转铰链点，旋转 26 度
            p.translate(pivotX, pivotY);
            p.rotate(isRight ? 26.0 : -26.0);
            p.translate(-pivotX, -pivotY);

            // 门开危险高亮光晕与实色描边
            p.setBrush(rgba(kRed, 70));
            p.setPen(QPen(kRed, 2.0));
        } else {
            p.setBrush(rgba(kCardBg, 140));
            p.setPen(QPen(rgba(kBorderHi, 120), 1.0));
        }

        // 门板几何轮廓
        QRectF doorBody(isRight ? pivotX - 4 : pivotX - 10, pivotY, 14, endY - pivotY);
        p.drawRoundedRect(doorBody, 3, 3);

        if (open) {
            // 门边安全警示红灯
            p.setBrush(kRed);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(isRight ? doorBody.right() : doorBody.left(), doorBody.bottom() - 4), 3, 3);
        }
        p.restore();
    };

    // 绘制四车门 (左前 / 右前 / 左后 / 右后)
    drawSingleDoor(l, t + 115, l, t + 185, dFL, false);
    drawSingleDoor(r, t + 115, r, t + 185, dFR, true);
    drawSingleDoor(l, t + 195, l, t + 265, dRL, false);
    drawSingleDoor(r, t + 195, r, t + 265, dRR, true);

    // 引擎盖与后备箱状态高亮
    if (dHood) {
        QRectF hoodR(cx - 30, t + 30, 60, 45);
        p.setBrush(rgba(kRed, 60));
        p.setPen(QPen(kRed, 2.0, Qt::DashLine));
        p.drawRoundedRect(hoodR, 8, 8);
        drawHudLabel(p, hoodR, QStringLiteral("前机舱开启"), 15, kRed, Qt::AlignCenter, true);
    }
    if (dTrunk) {
        QRectF trunkR(cx - 30, t + 325, 60, 35);
        p.setBrush(rgba(kRed, 60));
        p.setPen(QPen(kRed, 2.0, Qt::DashLine));
        p.drawRoundedRect(trunkR, 6, 6);
        drawHudLabel(p, trunkR, QStringLiteral("后备箱开启"), 15, kRed, Qt::AlignCenter, true);
    }
}

/* 5.7 车灯光效与转向灯闪烁 */
void CarStatusPanel::drawCarLamps(QPainter &p, const QRectF &b)
{
    const qreal cx = b.center().x();
    const qreal t = b.top();
    const qreal bot = b.bottom();

    bool lightFresh = m_can && m_can->lightFresh();
    bool driveFresh = m_can && m_can->speedFresh();

    bool lowBeam   = lightFresh && m_can->lightOn(0);
    bool highBeam  = lightFresh && m_can->lightOn(1);
    bool posLight  = lightFresh && m_can->lightOn(2);
    bool fFog      = lightFresh && m_can->lightOn(3);
    bool rFog      = lightFresh && m_can->lightOn(4);

    bool leftTurn  = driveFresh && m_can->leftTurn();
    bool rightTurn = driveFresh && m_can->rightTurn();
    bool hazard    = driveFresh && m_can->hazard();
    bool brakeOn   = driveFresh && m_can->brake();
    bool reverseOn = driveFresh && m_can->reverse();

    // 1. 前大灯组 (L/R 鹰眼几何灯罩)
    auto drawHeadlightCluster = [&](qreal hx, bool isRight, bool turnOn) {
        QRectF lr(hx - 15, t + 26, 30, 16);
        p.setBrush(QColor(0x10, 0x16, 0x22));
        p.setPen(QPen(QColor(0x40, 0x55, 0x75), 1.0));
        p.drawRoundedRect(lr, 4, 4);

        // LED 日行灯与示宽灯
        if (posLight || lowBeam || highBeam) {
            p.setBrush(highBeam ? QColor(0xFF, 0xFF, 0xFF) : kCyan);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(QRectF(lr.left() + 3, lr.top() + 3, lr.width() - 6, 3), 1, 1);
        }

        // 远近光透镜发光体
        if (highBeam) {
            p.setBrush(QColor(0xFF, 0xFF, 0xFF));
            p.drawEllipse(QPointF(lr.center().x(), lr.center().y() + 2), 5, 5);
        } else if (lowBeam) {
            p.setBrush(kCyan);
            p.drawEllipse(QPointF(lr.center().x(), lr.center().y() + 2), 4, 4);
        }

        // 转向灯 (1.5 Hz 闪烁)
        if (turnOn && m_blinkOn) {
            p.setBrush(kAmber);
            p.drawRoundedRect(QRectF(isRight ? lr.right() - 8 : lr.left() + 2, lr.top() + 2, 6, lr.height() - 4), 2, 2);
        }
    };
    drawHeadlightCluster(cx - 48, false, leftTurn || hazard);
    drawHeadlightCluster(cx + 48, true, rightTurn || hazard);

    // 2. 前雾灯
    if (fFog) {
        p.setBrush(kAmber);
        p.setPen(QPen(rgba(kAmber, 160), 2.0));
        p.drawEllipse(QPointF(cx - 52, t + 16), 4, 4);
        p.drawEllipse(QPointF(cx + 52, t + 16), 4, 4);
    }

    // 3. 贯穿式尾灯、制动刹车灯与倒车灯
    QRectF tailBar(cx - 54, bot - 22, 108, 8);
    // 示宽/常亮红光
    QColor tailCol = brakeOn ? kRed : (posLight ? rgba(kRed, 180) : QColor(0x35, 0x10, 0x15));
    p.setBrush(tailCol);
    p.setPen(QPen(brakeOn ? rgba(kRed, 220) : rgba(kRed, 80), brakeOn ? 2.0 : 1.0));
    p.drawRoundedRect(tailBar, 3, 3);

    if (brakeOn) {
        // 高位刹车灯
        QRectF highBrake(cx - 16, t + 312, 32, 4);
        p.setBrush(kRed);
        p.setPen(QPen(rgba(kRed, 200), 2.0));
        p.drawRoundedRect(highBrake, 1, 1);

        // 尾部刹车扩散光晕
        QRadialGradient brakeGlow(QPointF(cx, bot - 15), 65);
        brakeGlow.setColorAt(0.0, rgba(kRed, 140));
        brakeGlow.setColorAt(1.0, rgba(kRed, 0));
        p.setBrush(brakeGlow);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(cx, bot - 15), 65, 30);
    }

    // 倒车灯 (亮白色)
    if (reverseOn) {
        p.setBrush(QColor(0xFF, 0xFF, 0xFF));
        p.setPen(QPen(rgba(QColor(0xFF, 0xFF, 0xFF), 180), 2.0));
        p.drawRoundedRect(QRectF(cx - 36, bot - 21, 14, 6), 2, 2);
        p.drawRoundedRect(QRectF(cx + 22, bot - 21, 14, 6), 2, 2);
    }

    // 后雾灯
    if (rFog) {
        p.setBrush(kAmber);
        p.drawRoundedRect(QRectF(cx - 8, bot - 21, 16, 6), 2, 2);
    }

    // 后部转向灯闪烁
    if ((leftTurn || hazard) && m_blinkOn) {
        p.setBrush(kAmber);
        p.drawRoundedRect(QRectF(tailBar.left() - 4, tailBar.top() - 1, 10, tailBar.height() + 2), 2, 2);
    }
    if ((rightTurn || hazard) && m_blinkOn) {
        p.setBrush(kAmber);
        p.drawRoundedRect(QRectF(tailBar.right() - 6, tailBar.top() - 1, 10, tailBar.height() + 2), 2, 2);
    }
}
