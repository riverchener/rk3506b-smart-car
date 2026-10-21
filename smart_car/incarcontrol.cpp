#include "incarcontrol.h"
#include "canmgr.h"
#include "uiadapter.h"

#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QPushButton>
#include <QRectF>
#include <QShowEvent>
#include <QStringList>
#include <QVBoxLayout>

namespace {

/* 科技感主色调令牌 */
const char *kC_CYAN   = "#00E5FF";
const char *kC_BLUE   = "#2979FF";
const char *kC_GREEN  = "#00E676";
const char *kC_AMBER  = "#FFC400";
const char *kC_PURPLE = "#D500F9";
const char *kC_RED    = "#FF1744";

/* 按钮样式生成函数 */
QString btnQss(const QString &accent, const QString &tint, int px)
{
    return QStringLiteral(
               "QPushButton {"
               "  background: #111A2E; color: #EEF4FF; border: 1px solid #243656;"
               "  border-radius: 8px; font-size: %3px; font-weight: bold; padding: 2px 6px; }"
               "QPushButton:pressed { background: %1; color: #060A14; border-color: %1; }"
               "QPushButton:checked { background: %2; color: %1; border: 1.5px solid %1; }"
               "QPushButton:disabled { color: #4E5A70; border-color: #1A253A; background: #0C1220; }")
        .arg(accent, tint)
        .arg(px);
}

/* 四角 HUD 角标 */
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

IncarControl::IncarControl(CanMgr *can, QWidget *parent)
    : QWidget(parent), m_can(can)
{
    setWindowTitle(QStringLiteral("车内控制"));

    // 1. 顶栏 (高度固定 50)
    m_topBar = new QWidget(this);
    m_topBar->setObjectName(QStringLiteral("hudTopBar"));

    auto *backBtn = new QPushButton(QStringLiteral("← 返回"), m_topBar);
    backBtn->setStyleSheet(btnQss(kC_CYAN, "#0C2A38", 18));
    backBtn->setFixedSize(110, 46);
    backBtn->setFocusPolicy(Qt::NoFocus);
    m_back = backBtn;
    connect(backBtn, &QPushButton::clicked, this, &IncarControl::back);

    auto *accent = new QFrame(m_topBar);
    accent->setStyleSheet(QStringLiteral("background: #00E5FF; border: none; border-radius: 2px;"));
    m_accent = accent;

    auto *title = mkLabel(QStringLiteral("车内控制"), 24, QStringLiteral("#EEF4FF"));
    auto *sub = mkLabel(QStringLiteral("IN-CAR COCKPIT CONTROL"), 15, QStringLiteral("#617594"));

    m_canChip = mkLabel(QStringLiteral("● CAN: 就绪"), 16, QStringLiteral("#94A7C6"));
    m_canChip->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *topLay = new QHBoxLayout(m_topBar);
    topLay->setContentsMargins(0, 0, 0, 0);
    topLay->setSpacing(12);
    topLay->addWidget(backBtn);
    topLay->addSpacing(4);
    topLay->addWidget(accent);
    topLay->addWidget(title);
    topLay->addWidget(sub);
    topLay->addStretch(1);
    topLay->addWidget(m_canChip);

    // 2. 车辆工作模式全域切换控制条 (0x204)
    m_modeCard = new QFrame(this);
    m_modeCard->setStyleSheet(QStringLiteral("QFrame { background: #0E1628; border: 1px solid #243656; border-radius: 10px; }"));
    
    auto *modeTitle = mkLabel(QStringLiteral("◆ 驾驶模式"), 17, QStringLiteral("#00E5FF"));
    modeTitle->setFixedWidth(100);

    const struct { const char *name; const char *acc; const char *tint; int id; } kModes[4] = {
        { "经济节能 ECO",    kC_GREEN,  "#0C2B1C", CanMgr::ModeEco },
        { "标准舒适 NORMAL", kC_BLUE,   "#0D1F38", CanMgr::ModeNormal },
        { "运动竞速 SPORT",  kC_AMBER,  "#30250A", CanMgr::ModeSport },
        { "全时雪地 SNOW",   kC_PURPLE, "#2E0A36", CanMgr::ModeSnow }
    };

    auto *modeLay = new QHBoxLayout(m_modeCard);
    modeLay->setContentsMargins(14, 6, 14, 6);
    modeLay->setSpacing(12);
    modeLay->addWidget(modeTitle);

    m_modeBtn.resize(4);
    for (int i = 0; i < 4; ++i) {
        auto *btn = mkButton(QString::fromUtf8(kModes[i].name), true, 16, kModes[i].acc, kModes[i].tint);
        btn->setFixedHeight(44);
        m_modeBtn[i] = btn;
        int mid = kModes[i].id;
        connect(btn, &QPushButton::clicked, this, [this, mid]() { onModeClicked(mid); });
        modeLay->addWidget(btn, 1);
    }

    // 3. 中部核心三大控制台 (空调 / 车窗 / 灯光)
    auto *midLay = new QHBoxLayout;
    midLay->setSpacing(12);

    // 3.1 气候环境智能控制台
    auto *acCard = new QFrame(this);
    acCard->setStyleSheet(QStringLiteral("QFrame { background: #0C1322; border: 1px solid #243656; border-radius: 12px; }"));
    auto *acLay = new QVBoxLayout(acCard);
    acLay->setContentsMargins(14, 12, 14, 12);
    acLay->setSpacing(10);

    auto *acHeader = mkLabel(QStringLiteral("▍ 智能空调 (0x201)"), 18, QStringLiteral("#00E5FF"));
    acLay->addWidget(acHeader);

    auto *acBtnLay = new QHBoxLayout;
    acBtnLay->setSpacing(10);
    m_acOn = mkButton(QStringLiteral("制冷 A/C"), true, 16, kC_BLUE, "#0D1E3A");
    m_recirc = mkButton(QStringLiteral("内循环"), true, 16, kC_CYAN, "#0C2A38");
    m_acOn->setFixedHeight(44);
    m_recirc->setFixedHeight(44);
    connect(m_acOn, &QPushButton::clicked, this, &IncarControl::onAcToggled);
    connect(m_recirc, &QPushButton::clicked, this, &IncarControl::onRecircToggled);
    acBtnLay->addWidget(m_acOn);
    acBtnLay->addWidget(m_recirc);
    acLay->addLayout(acBtnLay);

    // 温度调节行
    auto *tLay = new QHBoxLayout;
    tLay->setSpacing(8);
    m_tempDown = mkButton(QStringLiteral("−"), false, 22, kC_CYAN, "#0C2A38");
    m_tempUp = mkButton(QStringLiteral("+"), false, 22, kC_CYAN, "#0C2A38");
    m_tempDown->setFixedSize(48, 44);
    m_tempUp->setFixedSize(48, 44);
    m_tempValue = mkLabel(QStringLiteral("24 ℃"), 24, QStringLiteral("#EEF4FF"));
    m_tempValue->setAlignment(Qt::AlignCenter);
    connect(m_tempDown, &QPushButton::clicked, this, [this]() { onTempStep(-1); });
    connect(m_tempUp, &QPushButton::clicked, this, [this]() { onTempStep(+1); });
    tLay->addWidget(mkLabel(QStringLiteral("温度"), 16, QStringLiteral("#94A7C6")));
    tLay->addWidget(m_tempDown);
    tLay->addWidget(m_tempValue, 1);
    tLay->addWidget(m_tempUp);
    acLay->addLayout(tLay);

    // 风量调节行
    auto *fLay = new QHBoxLayout;
    fLay->setSpacing(8);
    m_fanDown = mkButton(QStringLiteral("−"), false, 22, kC_CYAN, "#0C2A38");
    m_fanUp = mkButton(QStringLiteral("+"), false, 22, kC_CYAN, "#0C2A38");
    m_fanDown->setFixedSize(48, 44);
    m_fanUp->setFixedSize(48, 44);
    m_fanValue = mkLabel(QStringLiteral("3 挡"), 22, QStringLiteral("#00E5FF"));
    m_fanValue->setAlignment(Qt::AlignCenter);
    connect(m_fanDown, &QPushButton::clicked, this, [this]() { onFanStep(-1); });
    connect(m_fanUp, &QPushButton::clicked, this, [this]() { onFanStep(+1); });
    fLay->addWidget(mkLabel(QStringLiteral("风量"), 16, QStringLiteral("#94A7C6")));
    fLay->addWidget(m_fanDown);
    fLay->addWidget(m_fanValue, 1);
    fLay->addWidget(m_fanUp);
    acLay->addLayout(fLay);
    acLay->addStretch(1);

    // 3.2 车窗升降控制矩阵
    auto *winCard = new QFrame(this);
    winCard->setStyleSheet(QStringLiteral("QFrame { background: #0C1322; border: 1px solid #243656; border-radius: 12px; }"));
    auto *winLay = new QVBoxLayout(winCard);
    winLay->setContentsMargins(14, 12, 14, 12);
    winLay->setSpacing(10);

    // 标题行：标题居左，一键全升/全降居右
    auto *winHeaderLay = new QHBoxLayout;
    winHeaderLay->setSpacing(6);
    auto *winHeader = mkLabel(QStringLiteral("▍ 车窗控制 (0x202)"), 18, QStringLiteral("#00E5FF"));
    m_winAllUp = mkButton(QStringLiteral("▲ 全升"), false, 14, kC_GREEN, "#0C2B1C");
    m_winAllDown = mkButton(QStringLiteral("▼ 全降"), false, 14, kC_AMBER, "#30250A");
    m_winAllUp->setFixedHeight(32);
    m_winAllDown->setFixedHeight(32);
    connect(m_winAllUp, &QPushButton::clicked, this, [this]() { onWindow(0xFF, 1); });
    connect(m_winAllDown, &QPushButton::clicked, this, [this]() { onWindow(0xFF, 2); });

    winHeaderLay->addWidget(winHeader);
    winHeaderLay->addStretch(1);
    winHeaderLay->addWidget(m_winAllUp);
    winHeaderLay->addWidget(m_winAllDown);
    winLay->addLayout(winHeaderLay);

    m_winUp.resize(4);
    m_winStop.resize(4);
    m_winDown.resize(4);

    // 2x2 网格紧凑排列：左前/右前/左后/右后
    auto *winGrid = new QGridLayout;
    winGrid->setHorizontalSpacing(10);
    winGrid->setVerticalSpacing(8);

    const QString wNames[4] = { QStringLiteral("左前"), QStringLiteral("右前"), QStringLiteral("左后"), QStringLiteral("右后") };
    for (int i = 0; i < 4; ++i) {
        auto *cell = new QHBoxLayout;
        cell->setSpacing(4);
        auto *lbl = mkLabel(wNames[i], 15, QStringLiteral("#94A7C6"));
        lbl->setFixedWidth(36);
        
        auto *bUp = mkButton(QStringLiteral("升"), false, 14, kC_GREEN, "#0C2B1C");
        auto *bStop = mkButton(QStringLiteral("停"), false, 14, kC_BLUE, "#0D1F38");
        auto *bDown = mkButton(QStringLiteral("降"), false, 14, kC_AMBER, "#30250A");
        bUp->setFixedHeight(32);
        bStop->setFixedHeight(32);
        bDown->setFixedHeight(32);

        m_winUp[i] = bUp;
        m_winStop[i] = bStop;
        m_winDown[i] = bDown;

        connect(bUp, &QPushButton::clicked, this, [this, i]() { onWindow(i, 1); });
        connect(bStop, &QPushButton::clicked, this, [this, i]() { onWindow(i, 0); });
        connect(bDown, &QPushButton::clicked, this, [this, i]() { onWindow(i, 2); });

        cell->addWidget(lbl);
        cell->addWidget(bUp, 1);
        cell->addWidget(bStop, 1);
        cell->addWidget(bDown, 1);

        int row = i / 2;
        int col = i % 2;
        winGrid->addLayout(cell, row, col);
    }
    winLay->addLayout(winGrid);
    winLay->addStretch(1);

    // 3.3 光学照明与本地控制台
    auto *lightCard = new QFrame(this);
    lightCard->setStyleSheet(QStringLiteral("QFrame { background: #0C1322; border: 1px solid #243656; border-radius: 12px; }"));
    auto *lightLay = new QVBoxLayout(lightCard);
    lightLay->setContentsMargins(14, 12, 14, 12);
    lightLay->setSpacing(8);

    auto *lightHeader = mkLabel(QStringLiteral("▍ 灯光照明 (0x203 & 本地)"), 18, QStringLiteral("#00E5FF"));
    lightLay->addWidget(lightHeader);

    m_lightBtn.resize(5);
    const struct { const char *name; const char *acc; const char *tint; int bit; } kLights[5] = {
        { "近光灯 (LOW BEAM)",  kC_GREEN, "#0C2B1C", 0 },
        { "远光灯 (HIGH BEAM)", kC_BLUE,  "#0D1F38", 1 },
        { "示宽灯 (POSITION)",  kC_CYAN,  "#0C2A38", 2 },
        { "前雾灯 (FRONT FOG)", kC_AMBER, "#30250A", 3 },
        { "后雾灯 (REAR FOG)",  kC_AMBER, "#30250A", 4 }
    };

    for (int i = 0; i < 5; ++i) {
        auto *btn = mkButton(QString::fromUtf8(kLights[i].name), true, 16, kLights[i].acc, kLights[i].tint);
        btn->setFixedHeight(40);
        m_lightBtn[i] = btn;
        int lbit = kLights[i].bit;
        connect(btn, &QPushButton::clicked, this, [this, lbit]() { onLightToggled(lbit); });
        lightLay->addWidget(btn);
    }
    lightLay->addStretch(1);

    midLay->addWidget(acCard, 30);
    midLay->addWidget(winCard, 24);
    midLay->addWidget(lightCard, 36);

    // 4. 底部全双工总线交互与反馈确认栏
    m_bottomBar = new QFrame(this);
    m_bottomBar->setStyleSheet(QStringLiteral("QFrame { background: #0E1628; border: 1px solid #243656; border-radius: 10px; }"));
    
    m_sentLabel = mkLabel(QStringLiteral("已下发指令: 无"), 15, QStringLiteral("#94A7C6"));
    m_feedbackLabel = mkLabel(QStringLiteral("0x102 反馈: --"), 15, QStringLiteral("#94A7C6"));
    m_matchLabel = mkLabel(QStringLiteral("✔ 指令一致"), 15, QStringLiteral("#00E676"));
    m_matchLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *botLay = new QHBoxLayout(m_bottomBar);
    botLay->setContentsMargins(14, 6, 14, 6);
    botLay->setSpacing(12);
    botLay->addWidget(m_sentLabel, 2);
    botLay->addWidget(m_feedbackLabel, 2);
    botLay->addWidget(m_matchLabel, 1);

    // 全局垂直组织
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 8, 12, 6);
    root->setSpacing(8);
    root->addWidget(m_topBar);
    root->addWidget(m_modeCard);
    root->addLayout(midLay, 1);
    root->addWidget(m_bottomBar);

    if (m_can) {
        connect(m_can, &CanMgr::changed, this, &IncarControl::refreshFeedback);
        connect(m_can, &CanMgr::log, this, &IncarControl::refreshFeedback);
    }
    refreshFeedback();

    UiAdapter::fit(this);
    applyFixedHeights();
}

void IncarControl::applyFixedHeights()
{
    if (m_topBar)    m_topBar->setFixedHeight(50);
    if (m_back)      m_back->setFixedSize(110, 46);
    if (m_accent)    m_accent->setFixedSize(4, 26);
    if (m_modeCard)  m_modeCard->setFixedHeight(58);
    if (m_bottomBar) m_bottomBar->setFixedHeight(46);
}

void IncarControl::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    applyFixedHeights();
}

void IncarControl::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const qreal W = width(), H = height();

    QLinearGradient bg(0, 0, 0, H);
    bg.setColorAt(0.0, QColor(0x0A, 0x10, 0x1E));
    bg.setColorAt(1.0, QColor(0x06, 0x0A, 0x14));
    p.fillRect(rect(), bg);

    pageCorners(p, QRectF(4, 4, W - 8, H - 8), QColor(0x00, 0xE5, 0xFF, 120), 20, 2.0);
}

QPushButton *IncarControl::mkButton(const QString &text, bool checkable, int px,
                                    const QString &accent, const QString &tint)
{
    auto *btn = new QPushButton(text, this);
    btn->setCheckable(checkable);
    btn->setFocusPolicy(Qt::NoFocus);
    QString acc = accent.isEmpty() ? QString::fromUtf8(kC_CYAN) : accent;
    QString t   = tint.isEmpty()   ? QStringLiteral("#0C2A38")   : tint;
    btn->setStyleSheet(btnQss(acc, t, px));
    return btn;
}

QLabel *IncarControl::mkLabel(const QString &text, int px, const QString &color)
{
    auto *lbl = new QLabel(text, this);
    lbl->setStyleSheet(QStringLiteral("color: %1; font-size: %2px; font-weight: bold;").arg(color).arg(px));
    return lbl;
}

void IncarControl::markSent(const QString &what)
{
    m_lastSent = what;
    m_lastCmd.restart();
    if (m_sentLabel) {
        m_sentLabel->setText(QStringLiteral("已下发指令: %1 (%2)")
                             .arg(what, QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
    }
}

void IncarControl::sendAc()
{
    if (m_can) {
        m_can->sendAc(m_ac, m_temp, m_fan, m_recircOn);
    }
    markSent(QStringLiteral("空调 开=%1 温=%2℃ 风=%3 循=%4")
             .arg(m_ac ? "开" : "关").arg(m_temp).arg(m_fan).arg(m_recircOn ? "内" : "外"));
    refreshFeedback();
}

void IncarControl::onAcToggled()
{
    m_ac = m_acOn->isChecked();
    sendAc();
}

void IncarControl::onTempStep(int delta)
{
    int nt = qBound(16, m_temp + delta, 30);
    if (nt != m_temp) {
        m_temp = nt;
        if (m_tempValue)
            m_tempValue->setText(QStringLiteral("%1 ℃").arg(m_temp));
        sendAc();
    }
}

void IncarControl::onFanStep(int delta)
{
    int nf = qBound(0, m_fan + delta, 7);
    if (nf != m_fan) {
        m_fan = nf;
        if (m_fanValue)
            m_fanValue->setText(QStringLiteral("%1 挡").arg(m_fan));
        sendAc();
    }
}

void IncarControl::onRecircToggled()
{
    m_recircOn = m_recirc->isChecked();
    sendAc();
}

void IncarControl::onWindow(int win, int action)
{
    if (m_can) {
        m_can->sendWindow(win, action, 0);
    }
    const char *actStr = (action == 1 ? "升" : (action == 2 ? "降" : "停"));
    QString winStr = (win == 0xFF) ? QStringLiteral("全车窗") : QStringLiteral("车窗 #%1").arg(win);
    markSent(QStringLiteral("%1 -> %2").arg(winStr, QString::fromUtf8(actStr)));
}

void IncarControl::onLightToggled(int bit)
{
    if (bit < 0 || bit >= m_lightBtn.size())
        return;
    bool on = m_lightBtn[bit]->isChecked();
    quint8 mask = 1 << bit;
    quint8 onMask = on ? mask : 0;
    quint8 offMask = on ? 0 : mask;

    if (on) m_lightOnMask |= mask;
    else    m_lightOnMask &= ~mask;

    if (m_can) {
        m_can->sendLight(onMask, offMask);
    }

    if (bit == 0) m_localLow = on;
    if (bit == 1) m_localHigh = on;
    if (m_can) {
        m_can->setLocalLight(m_localLow, m_localHigh);
    }

    markSent(QStringLiteral("灯光 bit%1 -> %2 (本地响应)").arg(bit).arg(on ? "亮" : "灭"));
    refreshFeedback();
}

void IncarControl::onModeClicked(int mode)
{
    for (int i = 0; i < m_modeBtn.size(); ++i) {
        m_modeBtn[i]->setChecked(i == mode);
    }
    m_mode = mode;
    if (m_can) {
        m_can->sendDriveMode(mode);
    }
    markSent(QStringLiteral("车辆模式 -> %1").arg(CanMgr::driveModeName(mode)));
    refreshFeedback();
}

void IncarControl::refreshFeedback()
{
    if (!m_can)
        return;

    // 1. CAN 状态芯片
    bool open = m_can->isOpen();
    bool demo = m_can->demoRunning();
    QString iface = m_can->iface();
    if (m_canChip) {
        QString txt = demo ? QStringLiteral("● CAN: 演示模式") : (open ? QStringLiteral("● CAN: %1").arg(iface) : QStringLiteral("● CAN: 未连接"));
        QString col = demo ? QStringLiteral("#2979FF") : (open ? QStringLiteral("#00E676") : QStringLiteral("#FF1744"));
        m_canChip->setText(QStringLiteral("<span style='color:%1;'>%2</span>").arg(col, txt));
    }

    // 2. 模式状态同步（若无正在下发未过宽限期的命令，则跟随 0x104 反馈）
    if (!m_lastCmd.isValid() || m_lastCmd.elapsed() > 2000) {
        if (m_can->driveModeFresh()) {
            int curM = m_can->driveMode();
            for (int i = 0; i < m_modeBtn.size(); ++i) {
                m_modeBtn[i]->setChecked(i == curM);
            }
        }
    }

    // 3. 0x102 反馈解析
    bool lFresh = m_can->lightFresh();
    if (m_feedbackLabel) {
        if (lFresh) {
            QString acSt = m_can->acOn() ? QStringLiteral("AC开") : QStringLiteral("AC关");
            QString actT = QStringLiteral("%1℃").arg(m_can->acTemp());
            m_feedbackLabel->setText(QStringLiteral("0x102 反馈: %1 %2 循环=%3")
                                     .arg(acSt, actT, m_can->recircOn() ? "内" : "外"));
        } else {
            m_feedbackLabel->setText(QStringLiteral("0x102 反馈: 超时未收到"));
        }
    }

    // 4. 一致性检查
    if (m_matchLabel) {
        if (!lFresh) {
            m_matchLabel->setText(QStringLiteral("⚠ 暂无反馈"));
            m_matchLabel->setStyleSheet(QStringLiteral("color: #FFC400; font-size: 15px; font-weight: bold;"));
        } else {
            bool acMatch = (m_ac == m_can->acOn());
            if (acMatch) {
                m_matchLabel->setText(QStringLiteral("✔ 指令与反馈一致"));
                m_matchLabel->setStyleSheet(QStringLiteral("color: #00E676; font-size: 15px; font-weight: bold;"));
            } else {
                m_matchLabel->setText(QStringLiteral("⚠ 反馈状态同步中"));
                m_matchLabel->setStyleSheet(QStringLiteral("color: #FFC400; font-size: 15px; font-weight: bold;"));
            }
        }
    }
}
