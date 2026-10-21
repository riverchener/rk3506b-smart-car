#ifndef INCARCONTROL_H
#define INCARCONTROL_H

#include <QElapsedTimer>
#include <QVector>
#include <QWidget>

class CanMgr;
class QPushButton;
class QLabel;
class QFrame;
class QShowEvent;

/* ============================================================================
 * IncarControl —— 车内控制（车载科技感驾驶舱中枢交互控制台）
 * ============================================================================
 * 2026-10-03 重塑架构：
 * 1. 顶部：工作模式全域切换 (0x204 经济/标准/运动/雪地，带科技流光呼吸与状态反馈跟随)；
 * 2. 中部核心三大控制台：
 *    - 气候智能控制台 (0x201)：大号温控 HUD 数显盘、风量阶梯指示、制冷与内外循环强力触控；
 *    - 车窗多维升降矩阵 (0x202)：四门车窗空间拟真矩阵布局 (FL/FR/RL/RR) + 一键全升/全降；
 *    - 光学全景照明矩阵 (0x203 + /dev/mycarctl)：近光/远光/示宽/前雾/后雾，本地硬件瞬间响应；
 * 3. 底部：全双工双向总线交互反馈条（指令下发状态、0x102 确认反馈、指令一致性校验）。
 * 4. 字段、CAN 交互与掩码控制 100% 保持，字号与布局完全满足 ≥15px 规范。
 * ==========================================================================*/
class IncarControl : public QWidget
{
    Q_OBJECT

public:
    explicit IncarControl(CanMgr *can, QWidget *parent = nullptr);

signals:
    void back();

protected:
    void paintEvent(QPaintEvent *e) override;
    void showEvent(QShowEvent *e) override;

private slots:
    void onAcToggled();
    void onTempStep(int delta);
    void onFanStep(int delta);
    void onRecircToggled();
    void onWindow(int win, int action);
    void onLightToggled(int bit);
    void onModeClicked(int mode);
    void refreshFeedback();

private:
    QPushButton *mkButton(const QString &text, bool checkable = false, int px = 18,
                           const QString &accent = QString(),
                           const QString &tint = QString());
    QLabel      *mkLabel(const QString &text, int px = 16, const QString &color = "#EEF4FF");
    void         applyFixedHeights();
    void         sendAc();
    void         markSent(const QString &what);

    CanMgr *m_can = nullptr;

    /* 空调控件 */
    QPushButton *m_acOn = nullptr;
    QPushButton *m_recirc = nullptr;
    QPushButton *m_tempUp = nullptr;
    QPushButton *m_tempDown = nullptr;
    QPushButton *m_fanUp = nullptr;
    QPushButton *m_fanDown = nullptr;
    QLabel      *m_tempValue = nullptr;
    QLabel      *m_fanValue = nullptr;
    int          m_temp = 24;
    int          m_fan = 3;
    bool         m_ac = false;
    bool         m_recircOn = false;

    /* 车窗控件 */
    QVector<QPushButton *> m_winUp;
    QVector<QPushButton *> m_winStop;
    QVector<QPushButton *> m_winDown;
    QPushButton *m_winAllUp = nullptr;
    QPushButton *m_winAllDown = nullptr;

    /* 灯光控件 */
    QVector<QPushButton *> m_lightBtn;
    quint8 m_lightOnMask = 0;
    bool   m_localLow = false;
    bool   m_localHigh = false;

    /* 模式控制 */
    QVector<QPushButton *> m_modeBtn;
    int    m_mode = -1;

    /* 底部反馈栏 */
    QLabel *m_sentLabel = nullptr;
    QLabel *m_feedbackLabel = nullptr;
    QLabel *m_matchLabel = nullptr;
    QLabel *m_canChip = nullptr;

    QWidget     *m_topBar = nullptr;
    QWidget     *m_accent = nullptr;
    QFrame      *m_modeCard = nullptr;
    QFrame      *m_bottomBar = nullptr;
    QPushButton *m_back = nullptr;

    QString m_lastSent;
    QElapsedTimer m_lastCmd;
};

#endif // INCARCONTROL_H
