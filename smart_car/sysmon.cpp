#include "sysmon.h"
#include "uiadapter.h"

#include <QDebug>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

namespace {
const QColor kBg1(18, 26, 38);
const QColor kBg2(10, 16, 26);
const QColor kCard(255, 255, 255, 22);
const QColor kCardBorder(255, 255, 255, 46);
const QColor kTextMain(235, 244, 252);
const QColor kTextDim(150, 172, 192);
const QColor kAccentCpu(64, 158, 255);
const QColor kAccentMem(82, 196, 26);
const QColor kAccentTemp(250, 173, 20);
const QColor kAccentDisk(146, 84, 222);
const QColor kOk(82, 196, 26);
const QColor kWarn(250, 173, 20);
const QColor kBad(245, 74, 74);
}

SysMon::SysMon(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("sysmon_page");
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &SysMon::tick);
    m_timer->start();
    tick();
}

void SysMon::refreshNow()
{
    tick();
}

void SysMon::tick()
{
    Hal::SysInfo info;
    if (Hal::instance().sysInfo(&info)) {
        m_info = info;
        if (m_cpuSamples < 3)
            m_cpuSamples++;   // 头几次 CPU 差值不准，先显示“采样中”
    }
    update();
}

QColor SysMon::usageColor(double percent)
{
    if (percent >= 85.0)
        return kBad;
    if (percent >= 65.0)
        return kWarn;
    return kOk;
}

void SysMon::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateBackRect();
}

void SysMon::updateBackRect()
{
    // 返回按钮固定在左上角（尺寸随页面缩放，所以每次 resize 都重算）
    const int s = qMax(24, height() / 22);
    m_backRect = QRect(width() / 60, height() / 26, s, s);
}

void SysMon::mousePressEvent(QMouseEvent *event)
{
    if (m_backRect.adjusted(-10, -10, 10, 10).contains(event->pos())) {
        emit backout();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void SysMon::drawCard(QPainter &p, const QRect &r, const QString &title,
                      const QString &value, const QString &sub,
                      double percent, const QColor &accent, bool showBar)
{
    // 每张卡片完全独立绘制：save/restore + 显式重设画笔画刷字体，
    // 避免任何状态残留导致某张卡片画不出来。
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const int radius = qMax(6, r.height() / 8);

    // 卡片底
    p.setPen(Qt::NoPen);
    p.setBrush(kCard);
    p.drawRoundedRect(r, radius, radius);

    // 边框
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(kCardBorder, 1.5));
    p.drawRoundedRect(r, radius, radius);

    // 左侧彩色竖条
    const int barW = qMax(4, r.width() / 90);
    const QRect bar(r.left() + 2, r.top() + r.height() * 18 / 100,
                    barW, r.height() * 64 / 100);
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawRoundedRect(bar, barW / 2, barW / 2);

    const int padL = bar.right() + r.width() / 22;
    const int textW = r.right() - padL - r.width() / 24;
    if (textW <= 10) {
        p.restore();
        return;
    }

    // 标题
    p.setBrush(Qt::NoBrush);
    p.setPen(kTextDim);
    {
        QFont f = font();
        f.setPixelSize(qMax(11, r.height() / 8));
        f.setBold(false);
        p.setFont(f);
        p.drawText(QRect(padL, r.top() + r.height() / 18, textW, r.height() / 5),
                   Qt::AlignLeft | Qt::AlignVCenter, title);
    }

    // 大数值（字号按宽度收敛，避免被切）
    p.setPen(kTextMain);
    {
        QFont f = font();
        int size = qMax(18, r.height() / 3);
        f.setPixelSize(size);
        f.setBold(true);
        while (size > 12 && QFontMetrics(f).horizontalAdvance(value) > textW)
            f.setPixelSize(--size);
        p.setFont(f);
        p.drawText(QRect(padL, r.top() + r.height() * 28 / 100, textW, r.height() * 34 / 100),
                   Qt::AlignLeft | Qt::AlignVCenter, value);
    }

    // 副标题
    if (!sub.isEmpty()) {
        p.setPen(kTextDim);
        QFont f = font();
        f.setPixelSize(qMax(10, r.height() / 10));
        f.setBold(false);
        p.setFont(f);
        const int subTop = r.top() + r.height() * 63 / 100;
        const int subBottom = showBar ? (r.bottom() - r.height() / 8) : (r.bottom() - r.height() / 14);
        p.drawText(QRect(padL, subTop, textW, qMax(12, subBottom - subTop)),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(f).elidedText(sub, Qt::ElideRight, textW));
    }

    // 进度条
    if (showBar && percent >= 0) {
        const int bh = qMax(6, r.height() / 14);
        const QRect track(padL, r.bottom() - bh - r.height() / 12, textW, bh);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 38));
        p.drawRoundedRect(track, bh / 2, bh / 2);

        double f = percent / 100.0;
        if (f > 1.0) f = 1.0;
        if (f < 0.0) f = 0.0;
        if (f > 0.001) {
            const QRect fill(track.left(), track.top(), qMax(bh, (int)(track.width() * f)), bh);
            p.setBrush(usageColor(percent));
            p.drawRoundedRect(fill, bh / 2, bh / 2);
        }
    }

    p.restore();
}

void SysMon::drawInfoTile(QPainter &p, const QRect &r, const QString &label, const QString &value)
{
    QPainterPath path;
    path.addRoundedRect(r, r.height() / 6.0, r.height() / 6.0);
    p.fillPath(path, kCard);
    p.setPen(QPen(kCardBorder, 1.2));
    p.drawPath(path);

    const int padX = r.width() / 14;
    const int textW = r.width() - 2 * padX;

    QFont fl = p.font();
    fl.setPixelSize(qMax(10, r.height() / 6));
    fl.setBold(false);
    p.setFont(fl);
    p.setPen(kTextDim);
    QFontMetrics flm(fl);
    p.drawText(QRect(r.left() + padX, r.top() + r.height() / 12, textW, r.height() / 3),
               Qt::AlignLeft | Qt::AlignVCenter,
               flm.elidedText(label, Qt::ElideRight, textW));

    // 数值也按宽度自适应，避免“0天 1小时”这种被切掉
    QString v = value;
    QFont fv = p.font();
    int vs = qMax(14, r.height() / 3);
    fv.setPixelSize(vs);
    fv.setBold(true);
    QFontMetrics fvm(fv);
    for (; vs > 10; --vs) {
        fv.setPixelSize(vs);
        fvm = QFontMetrics(fv);
        if (fvm.horizontalAdvance(v) <= textW)
            break;
    }
    p.setFont(fv);
    p.setPen(kTextMain);
    p.drawText(QRect(r.left() + padX, r.top() + r.height() / 2, textW, r.height() * 2 / 5),
               Qt::AlignLeft | Qt::AlignVCenter, v);
}

void SysMon::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    updateBackRect();   // 保证返回按钮位置跟当前尺寸一致
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const int W = width();
    const int H = height();

    // 背景渐变
    QLinearGradient bg(0, 0, 0, H);
    bg.setColorAt(0.0, kBg1);
    bg.setColorAt(1.0, kBg2);
    p.fillRect(rect(), bg);

    // 顶部高光
    QLinearGradient top(0, 0, W, 0);
    top.setColorAt(0.0, QColor(64, 158, 255, 70));
    top.setColorAt(0.5, QColor(146, 84, 222, 40));
    top.setColorAt(1.0, QColor(82, 196, 26, 30));
    p.fillRect(QRect(0, 0, W, qMax(3, H / 180)), top);

    // ---- 上边距/标题 ----
    const int marginX = W / 28;
    const int topY = H / 22;
    const int backS = qMax(24, H / 22);

    // 返回箭头
    {
        QPen pen(kTextMain, qMax(2, backS / 9), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        const int cx = m_backRect.center().x(), cy = m_backRect.center().y();
        const int a = backS / 4;
        p.drawLine(cx - a / 2, cy - a, cx - a * 3 / 2, cy);
        p.drawLine(cx - a * 3 / 2, cy, cx - a / 2, cy + a);
        p.drawLine(cx - a * 3 / 2, cy, cx + a, cy);
    }

    // 标题
    QFont ft = p.font();
    ft.setPixelSize(qMax(20, H / 16));
    ft.setBold(true);
    p.setFont(ft);
    p.setPen(kTextMain);
    p.drawText(QRect(marginX + backS + W / 80, topY - H / 60, W / 2, H / 12),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("系统健康监控"));

    // 右上角实时状态
    {
        QFont fs = p.font();
        fs.setPixelSize(qMax(12, H / 34));
        fs.setBold(false);
        p.setFont(fs);
        p.setPen(kTextDim);
        const QString st = m_info.valid ? QStringLiteral("实时刷新中 · 1s") : QStringLiteral("数据不可用");
        p.drawText(QRect(W / 2, topY - H / 60, W / 2 - marginX, H / 12),
                   Qt::AlignRight | Qt::AlignVCenter, st);
    }

    // ---- 四张大卡片（2x2）----
    const int gridTop = topY + H / 11;
    const int gapX = W / 60;
    const int gapY = H / 40;
    const int cardW = (W - 2 * marginX - gapX) / 2;
    const int tileH = qMax(56, H / 8);                    // 底部四块小信息
    const int tileTop = H - marginX - tileH;
    const int cardH = qMax(90, (tileTop - gridTop - gapY - gapY) / 2);

    const double cpu = (m_cpuSamples >= 2 && m_info.cpuPercent >= 0) ? m_info.cpuPercent : -1;
    QString cpuValue = cpu < 0 ? QStringLiteral("采样中")
                               : QString::number(cpu, 'f', 1) + "%";
    QString cpuSub = QString("%1 核 · 用户%2% 系统%3%")
                         .arg(m_info.cpuCount)
                         .arg(m_info.cpuUser, 0, 'f', 0)
                         .arg(m_info.cpuSys, 0, 'f', 0);

    QString memValue = m_info.memPercent > 0
                           ? QString::number(m_info.memPercent, 'f', 1) + "%"
                           : QStringLiteral("--");
    QString memSub = QString("已用 %1 / %2 MB")
                         .arg(m_info.memUsedMb, 0, 'f', 0)
                         .arg(m_info.memTotalMb, 0, 'f', 0);

    QString tempValue = m_info.tempC > 0 ? QString::number(m_info.tempC, 'f', 1) + " °C"
                                         : QStringLiteral("--");
    QString tempSub = m_info.tempC > 0 ? QStringLiteral("RK3506B 芯片温度") : QStringLiteral("无温度传感器");

    QString diskValue = QString::number(m_info.diskPercent, 'f', 1) + "%";
    QString diskSub = QString("已用 %1 / %2 MB")
                          .arg(m_info.diskUsedMb, 0, 'f', 0)
                          .arg(m_info.diskTotalMb, 0, 'f', 0);

    drawCard(p, QRect(marginX, gridTop, cardW, cardH),
             QStringLiteral("CPU 使用率"), cpuValue, cpuSub,
             cpu, kAccentCpu, true);

    drawCard(p, QRect(marginX + cardW + gapX, gridTop, cardW, cardH),
             QStringLiteral("内存使用率"), memValue, memSub,
             m_info.memPercent, kAccentMem, true);

    drawCard(p, QRect(marginX, gridTop + cardH + gapY, cardW, cardH),
             QStringLiteral("芯片温度"), tempValue, tempSub,
             m_info.tempC > 0 ? qMin(100.0, m_info.tempC) : -1, kAccentTemp, true);

    drawCard(p, QRect(marginX + cardW + gapX, gridTop + cardH + gapY, cardW, cardH),
             QStringLiteral("磁盘使用率 (/userdata)"), diskValue, diskSub,
             m_info.diskPercent, kAccentDisk, true);

    // ---- 底部四块小信息 ----
    const int tileGap = W / 80;
    const int tileW = (W - 2 * marginX - 3 * tileGap) / 4;
    if (tileH > 20) {
        const int up = (int)m_info.uptimeSec;
        QString upStr = up >= 86400
                            ? QString("%1天%2时").arg(up / 86400).arg((up % 86400) / 3600)
                            : QString("%1时%2分").arg(up / 3600).arg((up % 3600) / 60);
        QString loadStr = QString("%1 / %2 / %3")
                              .arg(m_info.load1, 0, 'f', 2)
                              .arg(m_info.load5, 0, 'f', 2)
                              .arg(m_info.load15, 0, 'f', 2);
        QString swapStr = m_info.swapTotalMb > 0
                              ? QString("%1/%2M")
                                    .arg(m_info.swapUsedMb, 0, 'f', 0)
                                    .arg(m_info.swapTotalMb, 0, 'f', 0)
                              : QStringLiteral("无");
        QString rootStr = QString("%1% · %2M空闲")
                              .arg(m_info.rootPercent, 0, 'f', 0)
                              .arg(m_info.rootTotalMb - m_info.rootUsedMb, 0, 'f', 0);

        drawInfoTile(p, QRect(marginX, tileTop, tileW, tileH), QStringLiteral("运行时间"), upStr);
        drawInfoTile(p, QRect(marginX + tileW + tileGap, tileTop, tileW, tileH),
                     QStringLiteral("负载 1/5/15"), loadStr);
        drawInfoTile(p, QRect(marginX + 2 * (tileW + tileGap), tileTop, tileW, tileH),
                     QStringLiteral("进程数 / SWAP"), QString("%1 · %2").arg(m_info.procCount).arg(swapStr));
        drawInfoTile(p, QRect(marginX + 3 * (tileW + tileGap), tileTop, tileW, tileH),
                     QStringLiteral("系统分区 /"), rootStr);
    }
}
