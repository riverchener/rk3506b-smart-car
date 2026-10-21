#ifndef CARSTATUSPANEL_H
#define CARSTATUSPANEL_H

#include <QColor>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>

class QPainter;
class CanMgr;

/* ============================================================================
 * CarStatusPanel —— 「车辆状态」页的核心自绘大面板（Tesla Model 3/Y 极简数字座舱）
 *
 * 2026-10-03 Tesla 风格重塑（v3）：
 * 1. 彻底移除科幻青蓝 HUD、网格、扫描线、四角 L-括号和霓虹光晕；
 * 2. 转向 Tesla Model 3/Y 的黑白灰高级扁平商用风格（深黑底色 #0D0D0F，卡片 #18181A，
 *    1px 边框 #28282B，圆角 8~10px）；
 * 3. 排版主导：白色等宽大号主数字，中灰 #8E8E93 次要信息，小号大写英文副标题；
 * 4. 语义色克制：红=制动/告警/门开，琥珀=转向/警告，蓝=远光/信息，绿=正常/就绪/经济；
 * 5. 视觉中心：Tesla 风格深灰色金属漆整车模型，红宝石尾灯、纯白前大灯光束、四门开闭联动；
 * 6. 右侧信息列集成 2×2 紧凑胎压胎温矩阵，提升信息秩序感。
 *
 * 字段与数据访问 100% 遵照 canmgr.h，严格遵循 1 秒超时语义。
 * ==========================================================================*/
class CarStatusPanel : public QWidget
{
    Q_OBJECT

public:
    explicit CarStatusPanel(CanMgr *can, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *e) override;

private slots:
    void onBlink();

private:
    /* 逻辑画布基准尺寸（1000x520） */
    static const int LW = 1000;
    static const int LH = 520;

    void drawBackdrop(QPainter &p, const QRectF &r);
    void drawTopStatusLine(QPainter &p);
    void drawLeftDashboard(QPainter &p);      // 左侧驾驶信息：大号车速、PRND 胶囊、主动安全状态
    void drawRightTelemetry(QPainter &p);     // 右侧车辆遥测：行程能量、2x2胎压网格、故障诊断
    void drawRealisticCar(QPainter &p, const QRectF &carArea); // 视觉中心：Tesla 风格写实车模

    /* Tesla 风格车模内部细分绘制部件 */
    void drawCarShadow(QPainter &p, const QRectF &body);
    void drawTiresAndBrakes(QPainter &p, const QRectF &body);
    void drawHeadlightBeams(QPainter &p, const QRectF &body);
    void drawCarBodyShell(QPainter &p, const QRectF &body);
    void drawCockpitAndGlass(QPainter &p, const QRectF &body);
    void drawDoorsAndFlaps(QPainter &p, const QRectF &body);
    void drawCarLamps(QPainter &p, const QRectF &body);

    /* 辅助绘制函数 */
    void drawCard(QPainter &p, const QRectF &r, const QColor &bg = QColor(0x18, 0x18, 0x1B),
                  const QColor &border = QColor(0x28, 0x28, 0x2C), qreal radius = 8.0);
    void drawTabularNum(QPainter &p, const QRectF &r, const QString &text, int px,
                        const QColor &c, int align = Qt::AlignCenter, bool bold = true);
    void drawLabel(QPainter &p, const QRectF &r, const QString &s, int px,
                   const QColor &c, int align = Qt::AlignLeft | Qt::AlignVCenter,
                   bool bold = false, qreal tracking = 0.0);

    CanMgr *m_can = nullptr;
    QTimer  m_blink;
    bool    m_blinkOn = true;
};

#endif // CARSTATUSPANEL_H
