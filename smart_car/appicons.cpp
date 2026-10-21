#include "appicons.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace {
const QColor kLine(233, 237, 245);        // 线条：接近白，不刺眼
const QColor kDim(150, 158, 178);         // 次要线条
const QColor kBgFill(255, 255, 255, 14);  // 极淡的底
const QColor kBgEdge(255, 255, 255, 34);

/* 统一的极简底：淡圆角方块（只是保证在花壁纸上也看得清，不是色块） */
void drawBase(QPainter &p)
{
    p.setPen(QPen(kBgEdge, 2));
    p.setBrush(kBgFill);
    p.drawRoundedRect(QRectF(5, 5, 102, 102), 26, 26);
}

/* 统一笔触：细、白、圆头圆角 */
void usePen(QPainter &p, const QColor &c = kLine, qreal w = 6.0)
{
    p.setPen(QPen(c, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
}
} // namespace

QPixmap smartcarGlyphIcon(int kind, int size, const QColor &lineIn)
{
    (void)lineIn;      /* 目前统一近白（简约风）；留着参数是为了以后要给状态色时不用改调用方 */
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(size / 112.0, size / 112.0);   // 全部按 112 画布设计
    drawBase(p);

    switch (kind) {
    case AppIcon::CarAC: {
        /* 车：车顶弧 + 车身 + 两个轮子；上方一颗小雪花表示空调 */
        usePen(p, kLine, 6);
        QPainterPath body;
        body.moveTo(26, 74);
        body.lineTo(26, 62);
        body.quadTo(30, 48, 46, 46);
        body.lineTo(66, 46);
        body.quadTo(82, 48, 86, 62);
        body.lineTo(86, 74);
        p.drawPath(body);
        p.drawLine(QPointF(26, 66), QPointF(86, 66));
        p.setBrush(kLine);
        p.drawEllipse(QPointF(38, 78), 6, 6);
        p.drawEllipse(QPointF(74, 78), 6, 6);
        p.setBrush(Qt::NoBrush);
        usePen(p, kDim, 4);                 // 雪花（比车身弱一点）
        for (int i = 0; i < 3; ++i) {
            p.save();
            p.translate(56, 26);
            p.rotate(60 * i);
            p.drawLine(QPointF(-11, 0), QPointF(11, 0));
            p.restore();
        }
        break;
    }
    case AppIcon::Gauge: {
        /* 仪表盘：下半开口的弧 + 指针 + 中心点 */
        usePen(p, kLine, 6);
        p.drawArc(QRectF(20, 26, 72, 72), 200 * 16, 140 * 16);
        usePen(p, kLine, 6);
        p.drawLine(QPointF(56, 62), QPointF(74, 44));
        p.setBrush(kLine);
        p.drawEllipse(QPointF(56, 62), 5, 5);
        p.setBrush(Qt::NoBrush);
        usePen(p, kDim, 4);                 // 刻度
        p.drawLine(QPointF(26, 70), QPointF(33, 73));
        p.drawLine(QPointF(86, 70), QPointF(79, 73));
        break;
    }
    case AppIcon::Music: {
        /* 音符：一个符头 + 竖杆 + 旗 */
        usePen(p, kLine, 6);
        p.drawLine(QPointF(68, 32), QPointF(68, 70));
        p.drawLine(QPointF(68, 32), QPointF(84, 38));
        p.setBrush(kLine);
        p.drawEllipse(QPointF(58, 72), 11, 9);
        p.setBrush(Qt::NoBrush);
        usePen(p, kLine, 6);
        p.drawLine(QPointF(36, 44), QPointF(36, 78));
        p.setBrush(kLine);
        p.drawEllipse(QPointF(28, 78), 9, 7);
        p.setBrush(Qt::NoBrush);
        break;
    }
    case AppIcon::Video: {
        /* 播放：圆角外框 + 三角 */
        usePen(p, kLine, 6);
        p.drawRoundedRect(QRectF(22, 32, 68, 48), 12, 12);
        p.setBrush(kLine);
        QPainterPath tri;
        tri.moveTo(48, 44);
        tri.lineTo(70, 56);
        tri.lineTo(48, 68);
        tri.closeSubpath();
        p.drawPath(tri);
        p.setBrush(Qt::NoBrush);
        break;
    }
    case AppIcon::Weather: {
        /* 太阳 + 云 */
        usePen(p, kDim, 4);
        p.drawEllipse(QPointF(40, 40), 12, 12);
        for (int i = 0; i < 8; ++i) {
            p.save();
            p.translate(40, 40);
            p.rotate(45 * i);
            p.drawLine(QPointF(16, 0), QPointF(21, 0));
            p.restore();
        }
        usePen(p, kLine, 6);
        QPainterPath cloud;
        cloud.moveTo(34, 76);
        cloud.quadTo(24, 76, 24, 68);
        cloud.quadTo(24, 60, 34, 60);
        cloud.quadTo(38, 48, 52, 48);
        cloud.quadTo(66, 48, 70, 60);
        cloud.quadTo(86, 60, 86, 68);
        cloud.quadTo(86, 76, 74, 76);
        cloud.closeSubpath();
        p.drawPath(cloud);
        break;
    }
    case AppIcon::Gps: {
        /* 定位针：水滴轮廓 + 中心孔 */
        usePen(p, kLine, 6);
        QPainterPath pin;
        pin.moveTo(56, 90);
        pin.quadTo(28, 62, 28, 46);
        pin.arcTo(QRectF(28, 20, 56, 56), 180, -180);
        pin.quadTo(84, 62, 56, 90);
        p.drawPath(pin);
        p.setBrush(kLine);
        p.drawEllipse(QPointF(56, 48), 8, 8);
        p.setBrush(Qt::NoBrush);
        break;
    }
    case AppIcon::Env: {
        /* 水滴 + 内层小弧（水质） */
        usePen(p, kLine, 6);
        QPainterPath drop;
        drop.moveTo(56, 24);
        drop.quadTo(84, 56, 84, 68);
        drop.arcTo(QRectF(28, 48, 56, 44), 0, -180);
        drop.quadTo(28, 56, 56, 24);
        p.drawPath(drop);
        usePen(p, kDim, 4);
        p.drawArc(QRectF(40, 60, 32, 22), 200 * 16, 140 * 16);
        break;
    }
    case AppIcon::Sysmon: {
        /* 显示器 + 脉搏线 */
        usePen(p, kLine, 6);
        p.drawRoundedRect(QRectF(20, 30, 72, 46), 10, 10);
        p.drawLine(QPointF(46, 82), QPointF(66, 82));
        p.drawLine(QPointF(56, 76), QPointF(56, 82));
        usePen(p, kLine, 5);
        QPainterPath ecg;
        ecg.moveTo(28, 54);
        ecg.lineTo(40, 54);
        ecg.lineTo(46, 44);
        ecg.lineTo(54, 64);
        ecg.lineTo(60, 54);
        ecg.lineTo(84, 54);
        p.drawPath(ecg);
        break;
    }
    case AppIcon::Bell: {
        /* 铃铛 + 顶钮 + 底摆 */
        usePen(p, kLine, 6);
        QPainterPath bell;
        bell.moveTo(32, 70);
        bell.quadTo(36, 62, 36, 50);
        bell.quadTo(36, 36, 56, 36);
        bell.quadTo(76, 36, 76, 50);
        bell.quadTo(76, 62, 80, 70);
        bell.closeSubpath();
        p.drawPath(bell);
        p.drawLine(QPointF(27, 70), QPointF(85, 70));
        p.drawLine(QPointF(56, 30), QPointF(56, 36));
        usePen(p, kLine, 5);
        p.drawArc(QRectF(48, 76, 16, 12), 180 * 16, 180 * 16);
        break;
    }
    case AppIcon::Wifi: {
        /* 扇形信号：三段弧 + 一个点 */
        usePen(p, kLine, 6);
        p.drawArc(QRectF(22, 34, 68, 68), 35 * 16, 110 * 16);
        usePen(p, kLine, 6);
        p.drawArc(QRectF(36, 48, 40, 40), 35 * 16, 110 * 16);
        p.setBrush(kLine);
        p.drawEllipse(QPointF(56, 74), 6, 6);
        p.setBrush(Qt::NoBrush);
        break;
    }
    case AppIcon::Update: {
        /* 系统升级：向下的箭头（下载/刷入）+ 一圈带缺口的弧（OTA 循环） */
        usePen(p, kDim, 5);
        /* 缺口留在正下方，示意"从上面下来" */
        p.drawArc(QRectF(20, 20, 72, 72), 200 * 16, 140 * 16);
        usePen(p, kLine, 6);
        p.drawLine(QPointF(56, 36), QPointF(56, 70));   // 竖杆
        p.drawLine(QPointF(42, 58), QPointF(56, 72));   // 左斜
        p.drawLine(QPointF(70, 58), QPointF(56, 72));   // 右斜
        p.drawLine(QPointF(40, 84), QPointF(72, 84));   // 落地的横线
        break;
    }
    case AppIcon::Bluetooth:
    default: {
        /* 蓝牙符文：竖线 + 两个折线 */
        usePen(p, kLine, 6);
        p.drawLine(QPointF(56, 26), QPointF(56, 86));
        p.drawLine(QPointF(56, 26), QPointF(74, 42));
        p.drawLine(QPointF(74, 42), QPointF(40, 68));
        p.drawLine(QPointF(56, 86), QPointF(74, 70));
        p.drawLine(QPointF(74, 70), QPointF(40, 44));
        break;
    }
    }
    return pm;
}
