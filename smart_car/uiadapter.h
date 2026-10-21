#ifndef UIADAPTER_H
#define UIADAPTER_H

/*
 * UiAdapter —— 屏幕自适应缩放
 *
 * 原项目所有页面都是按 1024x600 固定像素画的（Qt Designer 绝对定位 + setFixedSize）。
 * RK3506B + 5.5 寸 MIPI 屏的逻辑分辨率是 1280x720（竖屏 720x1280 经 weston rotate-90 旋转）。
 * 直接显示会出现“窗口比屏幕大、右边被裁掉、按钮点不到”。
 *
 * 做法：把整页当成 1024x600 的设计画布，按屏幕的两个方向分别缩放（sx=1280/1024,
 * sy=720/600），铺满整屏，并递归缩放所有子控件的几何和字号。
 * 两个方向比例很接近（1.25 / 1.20），视觉上基本看不出拉伸，但能真正做到零黑边。
 *
 * 环境变量：
 *   SMARTCAR_DESIGN_W / SMARTCAR_DESIGN_H   设计画布尺寸，默认 1024x600
 *   SMARTCAR_FULLSCREEN=0                   关闭全屏（按设计比例居中，四周留黑边）
 *   SMARTCAR_UI_SCALE=125                   强制统一缩放比例（百分比），此时不做拉伸
 */

#include <QApplication>
#include <QDesktopWidget>
#include <QEvent>
#include <QFont>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QString>
#include <QWidget>
#include <QWindow>

class UiAdapter
{
public:
    static int designWidth()
    {
        bool ok = false;
        int v = qEnvironmentVariableIntValue("SMARTCAR_DESIGN_W", &ok);
        return (ok && v > 0) ? v : 1024;
    }

    static int designHeight()
    {
        bool ok = false;
        int v = qEnvironmentVariableIntValue("SMARTCAR_DESIGN_H", &ok);
        return (ok && v > 0) ? v : 600;
    }

    static bool forcedUniformScale()
    {
        bool ok = false;
        int v = qEnvironmentVariableIntValue("SMARTCAR_UI_SCALE", &ok);
        return ok && v > 0;
    }

    // 按屏幕尺寸算出 x/y 两个方向的缩放比例
    static void scalesFor(int screenW, int screenH, double *sx, double *sy)
    {
        /*
         * 内容**不该比屏幕大**：页面按 designWidth x designHeight 画，
         * 整体乘这个比例；乘出来超过屏幕的话右下角就会被裁掉，而 Qt 不会报任何错。
         *
         * 1280x720 + 1024x600 时装得下的比例是 min(1.25, 1.20) = 1.20。
         * 如果有人在 /userdata/smart_car/smartcar.conf 里写了
         * SMARTCAR_UI_SCALE=125，页面就会变成 1280x750，比屏高多 30px。
         * 所以这里取"用户想要的比例"和"装得下的比例"里较小的那个。
         */
        const double fit = qMin(screenW / (double)designWidth(),
                                screenH / (double)designHeight());

        bool ok = false;
        int forced = qEnvironmentVariableIntValue("SMARTCAR_UI_SCALE", &ok);
        if (ok && forced > 0) {
            double s = forced / 100.0;
            if (fit > 0.05 && s > fit)
                s = fit;              // 夹住，宁可小一点也不能被裁
            *sx = *sy = s;
            return;
        }
        double s = fit;
        if (s <= 0.05)
            s = 1.0;
        *sx = *sy = s;
    }

    static bool fullscreen()
    {
        const QString fs = qEnvironmentVariable("SMARTCAR_FULLSCREEN");
        if (fs == "0" || fs.compare("false", Qt::CaseInsensitive) == 0)
            return false;
        return true;
    }

    static void setAppFontScale(double s)
    {
        QFont f = qApp->font();
        const double base = 9.0; // Qt 默认字号（pt）
        f.setPointSize(qMax(6, qRound(base * s)));   // 同样取整，见 scaleChildren 的说明
        qApp->setFont(f);
    }

    // 让窗口适配当前屏幕
    static void fit(QWidget *w)
    {
        if (!w)
            return;
        arrange(w, screenGeometry(w));
    }

    // 按指定屏幕尺寸适配（离屏渲染诊断用：offscreen 平台屏幕不是 1280x720）
    static void fitInto(QWidget *w, int screenW, int screenH)
    {
        if (!w || screenW <= 0 || screenH <= 0)
            return;
        QRect g(0, 0, screenW, screenH);
        arrange(w, g);
    }

    static QRect screenGeometry(QWidget *w)
    {
        QScreen *scr = (w && w->windowHandle() && w->windowHandle()->screen())
                           ? w->windowHandle()->screen()
                           : QGuiApplication::primaryScreen();
        if (scr)
            return scr->availableGeometry();
        QDesktopWidget *dw = QApplication::desktop();
        return dw->availableGeometry();
    }

private:
    static void arrange(QWidget *w, const QRect &avail)
    {
        if (avail.width() <= 0 || avail.height() <= 0)
            return;

        double sx = 1.0, sy = 1.0;
        scalesFor(avail.width(), avail.height(), &sx, &sy);

        const bool fs = fullscreen() && !forcedUniformScale();
        // 全屏时直接铺满屏幕（1920x720 -> 就是 1280x720）；子控件按下限比例缩放，
        // 用布局管理器的页面（如地图页）会自己铺满。
        const int targetW = fs ? avail.width() : qRound(designWidth() * sx);
        const int targetH = fs ? avail.height() : qRound(designHeight() * sy);

        // 重要：先解除旧尺寸约束，否则 setGeometry 会被夹住
        w->setMinimumSize(0, 0);
        w->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        w->setFixedSize(targetW, targetH);

        /*
         * 【顺序很重要】必须先 scaleChildren()，再 setAppFontScale()。
         *
         * scaleChildren() 是靠 c->font() 反推"设计字号"的（见下面的注释）。
         * 如果先 setAppFontScale()，应用级字体已经被放大成 9*1.2=10.8pt，
         * 那些**没有自己字体**、继承应用字体的控件就会被记成 10.8pt，
         * 再乘一次 1.2 变成 **12.96pt** —— 字号被放大了两遍。
         * 实测证据：应用自己 grab 出来的 geometry.txt 里大量控件是
         *   font=12.96pt fmH=25
         * （正确值应当是 9*1.2 = 10.8pt）。
         * 而这种"非整数、又被放大过"的字号在这块板子的字体引擎上会渲染异常：
         * 天气页温度曲线上的 "31° / 26°" 就被画成了一条条小横杠。
         */
        scaleChildren(w, sx, sy);
        setAppFontScale(qMin(sx, sy));

        if (fs) {
            /*
             * 【v2 适配，2026-10-12】这里的 w 现在**不一定是顶层窗口**：
             * MainWindow 改成"单窗口 + QStackedWidget"之后，各页面
             * （Weather/Player/Audio/Hardware/Map/SysMon/BluetoothPage/
             *   IncarControl/VehicleStatusPage/OtaPage）都变成了 stack 的
             * **子控件**，而 mainwindow.cpp 里仍然会在切页前对它们调
             * UiAdapter::fit()。
             *
             * 对**子控件**执行下面这两句是错的、而且有害：
             *   · setWindowFlags(…|FramelessWindowHint) 会把子控件重新提升成
             *     顶层窗口 —— 那就等于切页时又新建一个 surface，而这正是白屏
             *     （wayland 无缓冲集成 + wl_shm 首帧未初始化）的根因；
             *   · showFullScreen() 同理，而且会把 stack 的布局打乱。
             * 所以这里加一道 isWindow() 闸门：只有真正的顶层窗口才走这套
             * 全屏逻辑（MainWindow 自己、以及 diag/ 里那些独立诊断小程序）。
             * 子控件交给布局/父控件决定尺寸 —— 它们的 setFixedSize() 已经在
             * 上面几行按屏幕算好了。
             */
            if (w->isWindow()) {
                if (w->isVisible()) {
                    w->setWindowFlags(w->windowFlags() | Qt::FramelessWindowHint);
                    w->showFullScreen();
                    w->raise();
                }
            }
        } else {
            const int x = avail.x() + (avail.width() - targetW) / 2;
            const int y = avail.y() + (avail.height() - targetH) / 2;
            w->move(qMax(avail.x(), x), qMax(avail.y(), y));
            if (w->isVisible())
                w->show();
        }
    }

    /*
     * 把子控件按比例缩放到目标尺寸。
     *
     * 【必须是幂等的】——这是本项目最容易踩、后果最明显的一个坑。
     *
     * 页面会在**不止一个时机**调用 fit()：
     *   · 构造函数里一次；
     *   · ScreenAdaptive::ShowRefit 在**每次 Show 事件**里再调一次
     *     （每进一次页面就算一次）。
     * 如果直接拿"当前 geometry × 比例"，那么每调一次就会再乘一遍。
     * 1280x720 时比例是 1.2，真机 wayland 上实测（diag/fitprobe）：
     *     widget_5 = (792,96   409x217)   ← 构造后，正确
     *     fit #1   = (950,115  491x260)   ← ×1.2
     *     fit #2   = (1140,138 589x312)   ← ×1.2
     *     fit #3   = (1368,166 707x374)   ← ×1.2，x 已经超出 1280 屏幕
     * 也就是**每打开一次页面，内容就大一圈**，几轮之后右下角直接被裁掉 ——
     * 用户看到的就是"环境监测/天气预报显示不全、字被切一半"。
     *
     * 正确做法：第一次遇到某个控件时，把它的"设计尺寸/字号"记进动态属性，
     * 以后每次都**从设计值重算**，于是调用多少次结果都一样。
     */
    static void scaleChildren(QWidget *parent, double sx, double sy)
    {
        static const char *kGeomKey = "_smartcar_design_geom";
        static const char *kFontKey = "_smartcar_design_pt";

        const QObjectList kids = parent->children();
        for (int i = 0; i < kids.size(); ++i) {
            QWidget *c = qobject_cast<QWidget *>(kids.at(i));
            if (!c)
                continue;

            QRect g = c->property(kGeomKey).toRect();
            if (!g.isValid()) {
                g = c->geometry();                 // 第一次：记下设计尺寸
                c->setProperty(kGeomKey, g);
            }
            double basePt = c->property(kFontKey).toDouble();
            if (basePt <= 0.0) {
                /*
                 * 反推"设计字号"。
                 *
                 * 关键点：**不能直接信 c->font()**。
                 * 整个应用是分多次 fit() 的（先 MainWindow，再每个页面），
                 * 而 setAppFontScale() 会把应用字体从 9pt 改成 9*1.2≈11pt。
                 * 于是后 fit 的页面里，那些"没有自己字体、继承应用字体"的控件
                 * 读到的已经是 11pt，再乘 1.2 就变成 13pt —— 字号被放大两遍
                 * （实测 geometry.txt 里满屏 font=13pt 就是这个原因，
                 *   天气页温度曲线上的 "31°/26°" 因此被画成小横杠）。
                 *
                 * 所以：字体与应用字体一致 => 它是继承来的 => 设计基准就是
                 * Qt 默认的 9pt；否则才是控件自己的字体。
                 */
                const QFont wf = c->font();
                const QFont af = qApp->font();
                const bool inheritsApp = (wf.pointSizeF() == af.pointSizeF()
                                          && wf.family() == af.family());
                basePt = inheritsApp ? 9.0
                                     : (wf.pointSizeF() > 0 ? wf.pointSizeF() : 9.0);
                c->setProperty(kFontKey, basePt);
            }

            c->setMinimumSize(0, 0);
            c->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

            c->setGeometry(qRound(g.x() * sx), qRound(g.y() * sy),
                           qRound(g.width() * sx), qRound(g.height() * sy));

            QFont f = c->font();
            /* 取整到 1pt：这块板子的字体引擎对"非整数字号"会出现异常度量
             * （天气页曲线上的温度文字被画成小横杠就是这个原因）。
             * 1280x720 时比例是 1.2，取整误差最多 0.5pt，肉眼看不出。 */
            f.setPointSize(qMax(6, qRound(basePt * qMin(sx, sy))));
            c->setFont(f);

            scaleChildren(c, sx, sy);
        }
    }
};

/*
 * 便利基类：调用 fit() 的页面用它。（可选使用）
 * 因为 setFixedSize 之后再被 show() 会重新受最小/最大尺寸约束，这里在 show 时再排一次。
 */
class ScreenAdaptive
{
public:
    static void install(QWidget *w)
    {
        UiAdapter::fit(w);
        if (w)
            w->installEventFilter(new ShowRefit(w));
    }

private:
    class ShowRefit : public QObject
    {
    public:
        explicit ShowRefit(QWidget *w) : QObject(w), m_w(w) {}
    protected:
        bool eventFilter(QObject *obj, QEvent *ev) override
        {
            if (ev->type() == QEvent::Show && m_w)
                UiAdapter::fit(m_w);
            return QObject::eventFilter(obj, ev);
        }
    private:
        QWidget *m_w;
    };
};

#endif // UIADAPTER_H
