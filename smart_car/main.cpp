#include "mainwindow.h"
#include "uiadapter.h"
#include "ota_version.h"

#include <QApplication>
#include <QTimer>
#include <QColor>
#include <QPalette>

#include <csignal>

/*
 * 全局深色调色板（2026-10-12，「切页白屏」修复的一部分）
 *
 * 应用本来就是深色 HUD（主色 #0A0E17，见 docs/UI设计_科技感_20261002.md），
 * 但以前从没设过应用级 palette：新建的顶层窗口在"页面自己的 background-color
 * 画上去之前"，Qt 是用**调色板的 Window 画刷**铺根窗口那一层的，
 * 而默认调色板是浅灰 —— 在车机上就是切页那一瞬间白闪一下。
 *
 * ⚠ v2（单窗口 + QStackedWidget）之后，应用**只剩 MainWindow 一个顶层窗口**，
 *   页面切来切去都不再新建窗口，所以"新窗口第一帧"这条路径本身已经不存在了。
 *   这个 palette 保留下来当**兜底**：任何还没被页面自己画到的地方
 *   （子控件的背景、临时弹出的对话框、下拉列表等）都是深色底，
 *   万一还有漏画的一帧也不会是白色。
 *
 * 这里把 Window/Base/Button 一并换成深色，并且**同时**把文字色
 * （WindowText/Text/ButtonText）设成浅色，保证按钮、列表、消息框里的字
 * 仍然看得清（只改底色不改字色的话，QMessageBox 这类没有样式表的窗口
 * 会变成"深底 + 黑字"）。
 *
 * 已经写了样式表的控件不受影响：QSS 的优先级高于 palette，
 * 各页面里大量的 "background: transparent" 也不会被破坏。
 */
static void installDarkPalette()
{
    //设置颜色
    const QColor bg(0x0A, 0x0E, 0x17);      // HUD 近黑底
    const QColor bgAlt(0x10, 0x17, 0x25);   // 输入框/列表底
    const QColor fg(0xEE, 0xF4, 0xFF);      // 主文字
    const QColor btn(0x16, 0x20, 0x3A);     // 按钮底（比页面稍亮，看得出边界）

    // 设置调色板
    QPalette p = qApp->palette();
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, fg);
    p.setColor(QPalette::Base, bgAlt);
    p.setColor(QPalette::AlternateBase, bg);
    p.setColor(QPalette::Text, fg);
    p.setColor(QPalette::Button, btn);
    p.setColor(QPalette::ButtonText, fg);
    p.setColor(QPalette::BrightText, QColor(0xFF, 0x6E, 0x6E));
    p.setColor(QPalette::ToolTipBase, bgAlt);
    p.setColor(QPalette::ToolTipText, fg);
    p.setColor(QPalette::Highlight, QColor(0x1F, 0x6F, 0xE0));
    p.setColor(QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
    p.setColor(QPalette::Link, QColor(0x4F, 0xC3, 0xF7));
    p.setColor(QPalette::LinkVisited, QColor(0xB3, 0x9D, 0xDB));
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
    p.setColor(QPalette::PlaceholderText, QColor(0x6B, 0x7A, 0x94));
#endif
    qApp->setPalette(p);
}

/*
 * SIGUSR1 -> 把每个页面各抓一张 PNG 到 /tmp/appshot/。
 *
 * 为什么要这个：板子上 `weston-screenshooter` 报 "unauthorized"（合成器不允许
 * 随便抓屏），网口又可能不通，于是"真机上到底画成什么样"很难取证。
 * 让应用自己 QWidget::grab() 是最稳的一条路：
 *     kill -USR1 $(pidof Smart_in-car_entertainment)
 *     # 然后 adb pull /tmp/appshot/*.png
 * 信号处理函数里不能碰 Qt，所以这里只置标志，由定时器去执行。
 */
static volatile sig_atomic_t g_shotRequested = 0;
static void onShotSignal(int) { g_shotRequested = 1; }

/*
 * 【2026-10-18 部署一致性自检标记】
 *
 * 本串是「IP 实时显示 + 视频页三坑」这次改动**新加的、全仓唯一的**字符串。
 * 用途（验收要求：push 后要能证明板上跑的确实是这次编出来的二进制）：
 *   板端：grep -c 'SC-IPVFIX-20261018-2100' /userdata/smart_car/Smart_in-car_entertainment
 *   日志：grep '构建标记' /var/log/smartcar.log
 * 两处都能命中，才算"文件换对了 + 换上去的进程真的起来了"。
 * 必须被真正引用（下面 qInfo 打一行），否则 -O2 会把没用的 static 变量丢掉。
 */
static const char *kBuildMarker = "SC-IPVFIX-20261018-2100";

/*
 * 【2026-10-18 部署一致性自检标记 —— 音乐海报串图修复】
 *
 * 本串是「每首歌用自己的封面 + 没封面显示胶片占位」这次改动**新加的、全仓唯一**的字符串。
 * 用途（验收：push 后要能证明板上跑的确实是这次编出来的二进制）：
 *   板端：grep -c 'SC-COVERFIX-20261018-2130' /userdata/smart_car/Smart_in-car_entertainment
 *   日志：grep '构建标记' /var/log/smartcar.log
 * 必须被真正引用（下面 qInfo 打一行），否则 -O2 会把没用的 static 变量丢掉。
 */
static const char *kBuildMarkerCover = "SC-COVERFIX-20261018-2130";

/*
 * 【2026-10-18 部署一致性自检标记 —— 音乐页无封面占位改黑胶唱片】
 *
 * 本串是「无封面占位 = 黑胶唱片（透明底、无白边）」这次改动**新加的、全仓唯一**的字符串。
 * 用途（验收：push 后要能证明板上跑的确实是这次编出来的二进制）：
 *   板端：grep -c 'SC-VINYL-20261018-2140' /userdata/smart_car/Smart_in-car_entertainment
 *   日志：grep '黑胶标记' /var/log/smartcar.log
 * 必须被真正引用（下面 qInfo 打一行），否则 -O2 会把没用的 static 变量丢掉。
 */
static const char *kBuildMarkerVinyl = "SC-VINYL-20261018-2140";

/*
 * 【2026-10-18 OTA 演练版本 1.5.0 的部署一致性自检标记】
 *
 * 用途：把"板上跑的这份 ELF 就是 1.5.0 这次编出来的那份"钉死在二进制里，
 * 使 OTA 演练的成功判据有三个**互相独立**的观测点：
 *   ① 文件 md5 = manifest 里的 app_md5
 *   ② ELF 里 SMARTCAR_OTA_VERSION=v1.5.0（由发布脚本生成的 ota_version_local.h 决定，
 *      见 ota_version.h；smartcar-ota verify 比对的正是这个串）
 *   ③ 本标记 SC-OTA-150-20261018（只有 1.5.0 这次构建才有）
 * 板端取证：
 *   grep -c 'SC-OTA-150-20261018' /userdata/smart_car/Smart_in-car_entertainment
 *   grep 'OTA演练标记' /var/log/smartcar.log
 * 必须被真正引用（下面 qInfo 打一行），否则 -O2 会把没用的 static 变量丢掉。
 */
static const char *kBuildMarkerOta150 = "SC-OTA-150-20261018";

/*
 * 【2026-10-18 版本 1.6.0：桌面壁纸换成新图】
 *
 * 1.6.0 的**唯一功能改动**是桌面壁纸（images/1.jpg 换成用户给的 1280x720 JPEG）。
 * 壁纸是 qrc 资源 —— 编译期内嵌，所以"换了图"必须靠重编，而且有**两个独立锚点**：
 *   ① 本标记 SC-OTA-160-20261018（只有 1.6.0 这次构建才有）
 *   ② 壁纸 JPEG 里自带的 COM 段标记 SC-WALLPAPER-20261018-2301
 *      （rcc 对 jpg 是原样内嵌，所以它会原封不动出现在 ELF 里）
 * 板端取证（两条都命中才算"图真的换了"）：
 *   grep -c 'SC-OTA-160-20261018'      /userdata/smart_car/Smart_in-car_entertainment
 *   grep -c 'SC-WALLPAPER-20261018-2301' /userdata/smart_car/Smart_in-car_entertainment
 *   grep 'OTA1.6标记' /var/log/smartcar.log
 */
static const char *kBuildMarkerOta160 = "SC-OTA-160-20261018";

int main(int argc, char *argv[])
{
    // 高分屏/固定像素界面在 720x1280 面板上需要按 DPI 等比缩放
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);

    QApplication a(argc, argv);

    /* 深色底 + 浅色字：新建窗口/子控件的第一帧不会是浅灰/白色（切页白屏的兜底） */
    installDarkPalette();

    /*
     * 启动横幅：把版本号打进 /var/log/smartcar.log。
     *
     * 这是 OTA"升级成功"的**第三重证据**（前两重是文件 md5 和 ELF 里的版本串）：
     * 升级后 restart，日志里会出现新版本的横幅，等于应用自己承认"我是新版本"。
     * 排查线上问题时也能一眼看出板子上跑的是哪次发布。
     */
    qInfo().noquote() << "===== Smart_car 启动 ====="
                      << QString::fromLatin1(SMARTCAR_OTA_VERSION_TAG)//当前系统的正式 OTA 版本号
                      << "构建标记:" << QString::fromLatin1(kBuildMarker)
                      << "封面修复标记:" << QString::fromLatin1(kBuildMarkerCover)
                      << "黑胶标记:" << QString::fromLatin1(kBuildMarkerVinyl)
                      << "OTA演练标记:" << QString::fromLatin1(kBuildMarkerOta150)
                      << "OTA1.6标记:" << QString::fromLatin1(kBuildMarkerOta160);

    MainWindow w;
    w.show();

    signal(SIGUSR1, onShotSignal);
    {
        QTimer *shotTimer = new QTimer(&a);
        QObject::connect(shotTimer, &QTimer::timeout, &w, [&w]() {
            if (g_shotRequested) {
                g_shotRequested = 0;
                qInfo() << "SIGUSR1 -> 开始抓取所有页面";
                w.dumpPages(QStringLiteral("/tmp/appshot"));
            }
        });
        shotTimer->start(500);
    }

    /*
     * 无人值守自检：--page=weather|map|hardware|bluetooth|audio|incar|ota|video|desktop
     * （video 是 2026-10-18 为「视频页」真屏取证补的入口，等价于点桌面上的视频图标）
     * 启动后自动进入指定页面，跑 --selftest-ms 毫秒再自己退出。
     * 配合 QT_QPA_PLATFORM=offscreen 就能在没有人点屏、也不需要接屏幕的情况下
     * 验证"进入页面 -> 定位 -> 取天气"这条链路（日志里能看到实际走了哪条分支）。
     *
     * v2（单窗口 QStackedWidget）：页面不再是顶层窗口，**不能自己 show()** 了，
     * 所以这里统一交给 MainWindow::openPageForTest()，由它 emit 信号 + switchTo()。
     * 切页入口仍然只有 MainWindow::switchTo() 一个（它是 private）。
     * desktop 不需要切（桌面本来就是 stack 的第 0 页，启动就在它上面），
     * 这里只 show() 一下窗口本身（配合 SMARTCAR_CAN_IFACE=demo 看状态栏）。
     */
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (!arg.startsWith("--page="))
            continue;
        const QString page = arg.mid(7);
        int ms = qEnvironmentVariableIntValue("SMARTCAR_SELFTEST_MS");
        if (ms <= 0)
            ms = 30000;
        QTimer::singleShot(50, &w, [&w, page]() {
            if (page == "desktop")
                w.show();                 // 桌面（本来就是初始页）
            else
                w.openPageForTest(page);  // weather/map/hardware/bluetooth/audio/incar/ota
        });
        QTimer::singleShot(ms, &a, &QApplication::quit);
    }

    return a.exec();
}
