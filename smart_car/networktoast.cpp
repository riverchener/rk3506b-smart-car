#include "networktoast.h"

#include <QApplication>
#include <QDebug>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

/*
 * 【2026-10-18 ②(c) 同类问题】提示框也要钳进屏幕。
 *
 * 真屏实测（诊断报告 §2.4 末尾 / 图 _ipvideo/04_ip_toast_real_rot.png）：
 * 提示框的可见区是 x=402..1279、y=225..719 —— 宽度 480 是对的（居中位置也算对了），
 * 但**高度被拉得比屏幕还高**，于是"确 定"按钮落在屏外，用户只能等 6 秒自动关；
 * 内容在窗口里还是垂直居中的（内容到 y≈478 才出现）—— 说明窗口确实比内容高得多。
 *
 * 两个措施一起上：
 *   1) setFixedSize(钳进屏幕的尺寸)：min==max，compositor 想拉大也拉不动；
 *   2) 根布局末尾加 addStretch(1)：万一还是被拉高，内容贴顶显示，按钮不会被推到屏外。
 */
static QRect toastAvailableGeometry()
{
    QScreen *scr = QGuiApplication::primaryScreen();
    QRect g = scr ? scr->availableGeometry() : QRect();
    if (g.width() <= 0 || g.height() <= 0)
        g = QRect(0, 0, 1280, 720);            // 兜底：本板逻辑分辨率 1280x720
    return g;
}

NetworkToast::NetworkToast(QWidget *parent, const QString &iconPath,
                           const QString &title, const QString &detail)
    : QDialog(parent), m_icon(iconPath), m_title(title), m_detail(detail)
{
    buildUi(iconPath, title, detail);
}

NetworkToast::NetworkToast(QWidget *parent, const QString &ssid, const QString &ip)
    : QDialog(parent)
{
    buildUi(":/images/info.png", QStringLiteral("已成功连接网络！"),
            QString("WiFi：%1\nIP：%2").arg(ssid, ip));
}

void NetworkToast::buildUi(const QString &iconPath, const QString &title, const QString &detail)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog | Qt::WindowStaysOnTopHint);
    setStyleSheet(
        "QDialog { background-color: rgb(240, 250, 252); border: 2px solid rgb(170, 225, 232);"
        "          border-radius: 18px; }"
        "QLabel  { color: rgb(18, 62, 82); background: transparent; border: none; }"
        "QPushButton { background-color: rgb(0, 150, 200); color: white; border: none;"
        "              border-radius: 10px; padding: 10px 32px; }"
        "QPushButton:pressed { background-color: rgb(0, 110, 150); }");

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(30, 24, 30, 22);
    root->setSpacing(16);

    QHBoxLayout *row = new QHBoxLayout;
    row->setSpacing(20);

    QLabel *icon = new QLabel(this);
    icon->setFixedSize(76, 76);
    icon->setScaledContents(true);
    icon->setPixmap(QPixmap(iconPath));
    icon->setAlignment(Qt::AlignCenter);
    row->addWidget(icon);

    // 主标题 + 可选明细，合成一个 QLabel（换行）
    QString text = title;
    if (!detail.isEmpty())
        text += "\n\n" + detail;
    QLabel *label = new QLabel(text, this);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QFont tf = label->font();
    tf.setPixelSize(28);
    tf.setBold(true);
    label->setFont(tf);
    row->addWidget(label, 1);
    root->addLayout(row);

    QPushButton *ok = new QPushButton(QStringLiteral("确 定"), this);
    QFont bf = ok->font();
    bf.setPixelSize(24);
    ok->setFont(bf);
    ok->setCursor(Qt::PointingHandCursor);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);

    QHBoxLayout *btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    btnRow->addWidget(ok);
    btnRow->addStretch(1);
    root->addLayout(btnRow);
    // 兜底：窗口万一被拉高，内容贴顶，按钮不会被推出屏外
    root->addStretch(1);

    adjustSize();
    if (width() < 480)
        resize(480, height());

    // 【2026-10-18 ②(c)】钉进屏幕：min==max，别让 compositor 再拉大
    const QRect avail = toastAvailableGeometry();
    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    setFixedSize(qMin(avail.width(), qMax(480, width())),
                 qMin(avail.height(), qMax(160, height())));
}

void NetworkToast::centerOnScreen()
{
    const QRect g = toastAvailableGeometry();
    int x = g.x() + (g.width() - width()) / 2;
    int y = g.y() + (g.height() - height()) / 2;
    // 钳住：窗口比屏大时，至少保证左上角（标题/图标）在屏内可见
    if (x < g.x())
        x = g.x();
    if (y < g.y())
        y = g.y();
    move(x, y);
}

void NetworkToast::showMessage(QWidget *parent, const QString &iconPath,
                              const QString &title, const QString &detail,
                              int autoCloseMs)
{
    NetworkToast *t = new NetworkToast(parent, iconPath, title, detail);
    t->setAttribute(Qt::WA_DeleteOnClose, true);
    t->centerOnScreen();
    // 非阻塞浮层：不打断应用主循环；点确定或超时后自动关闭
    t->show();
    t->raise();
    if (autoCloseMs > 0)
        QTimer::singleShot(autoCloseMs, t, [t]() { if (t) t->close(); });
    qInfo().noquote() << "已弹出提示:" << title << detail;
}

void NetworkToast::showToast(QWidget *parent, const QString &ssid, const QString &ip)
{
    showMessage(parent, ":/images/info.png", QStringLiteral("已成功连接网络！"),
                QString("WiFi：%1\nIP：%2").arg(ssid, ip), 6000);
}

void NetworkToast::showDisconnected(QWidget *parent, const QString &reason)
{
    showMessage(parent, ":/images/error.png", QStringLiteral("网络已断开！"),
                reason.isEmpty() ? QString() : reason, 8000);
}

void NetworkToast::showModal(QWidget *parent, const QString &ssid, const QString &ip)
{
    NetworkToast t(parent, ssid, ip);
    t.centerOnScreen();
    t.exec();
}
