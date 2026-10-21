#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "player.h"
#include "Audio.h"
#include "weather.h"
#include "uiadapter.h"
#include "networkwatcher.h"
#include "canmgr.h"
#include "carstatuspanel.h"
#include "incarcontrol.h"
#include "appicons.h"

#include <QStackedWidget>   /* v2：单窗口方案的核心容器（2026-10-12） */
#include <QStackedLayout>
#include <QThread>
#include <QTimer>
#include <QClipboard>       /* dumpPages：临时占住剪贴板，阻止 grab() 报警告 */
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QApplication>
#include <QNetworkInterface>
#include <QNetworkAddressEntry>
#include <QAbstractSocket>
#include <QProcess>
#include <QScrollArea>
#include <QScroller>
#include <QScrollBar>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QColor>

namespace {

/*
 * 给页面铺一层深色底（HUD 主色 #0A0E17）。
 *
 * v2（单窗口 QStackedWidget）之后这段**退化成兜底**，但一行都不删：
 *
 *   1. 根治白屏的是"切页不再产生新 surface"（见 MainWindow::switchTo 的说明）。
 *      只要窗口不再 unmap/map，也就不存在"新窗口第一帧用调色板 Window 画刷
 *      铺底"这件事。所以本函数不再是修复的主体。
 *   2. 但它仍然有用：把"页面自己的 background-color 画上去之前"那一层
 *      （QWidget::paintBackground 的 DrawAsRoot 分支）从默认浅灰改成近黑，
 *      万一还有别的地方会露出未初始化/未绘制的一帧，至少是深色的。
 *
 * 只动 Window 一个角色：
 *   Base/Text/Button/ButtonText 一概不碰 —— 免得把列表、输入框、按钮的
 *   文字变成看不清的颜色；Audio/player 等页里大量的
 *   "background: transparent" 样式表也完全不受影响（QSS 优先级高于 palette）。
 */
void applyDarkBackdrop(QWidget *w)
{
    if (!w)
        return;
    w->setAutoFillBackground(true);
    QPalette p = w->palette();
    p.setColor(QPalette::Window, QColor(0x0A, 0x0E, 0x17));
    w->setPalette(p);
}

/*
 * v2：把一个页面"改造成可以放进 QStackedWidget 的普通子控件"。
 *
 *   setWindowFlags(Qt::Widget) —— 剥掉顶窗标志。这一步**不能省**：
 *     · Weather / BluetoothPage 的构造函数里设了 setWindowFlag(Qt::FramelessWindowHint)；
 *     · Map 本身是 QDialog（天然带 Qt::Dialog）；
 *     · Hardware 是 QMainWindow。
 *   留着这些标志，addWidget() 之后仍有被当成独立窗口的可能 —— 那样切页时
 *   又会新建 surface，"未初始化的 shm 首帧"（= 白屏根因）就跟着回来了。
 *   Qt::Widget = 清掉全部窗口标志，回到普通子控件。
 *
 * 深色底（applyDarkBackdrop）保留，作用退化成兜底，见上面说明。
 * 尺寸不在这里动：各页面构造函数里已经 UiAdapter::fit() 过，都是 1280x720。
 */
QWidget *preparePageWidget(QWidget *w)
{
    if (!w)                          // 空指针保护：上游可能传入尚未构造完成的页面
        return nullptr;
    w->setWindowFlags(Qt::Widget);   // 清除 FramelessWindowHint / Dialog / Window 等顶窗标志，降级为普通子控件，防止被 QStackedWidget 当成独立窗口
    applyDarkBackdrop(w);            // 设置深色背景作为兜底，避免首帧未绘制时露出白屏
    return w;                        // 返回同一指针，方便在 addWidget(preparePageWidget(page)) 中链式调用
}

}   // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    /*
     * v2：**这里不再调用 UiAdapter::fit(this)**（v1 有过一句，已删），改成显式
     * 设定 MainWindow 自己的几何。原因：
     *
     *   ① UiAdapter::fit() 是"按 1024x600 设计画布缩放 + **给顶层窗口**加
     *      Qt::FramelessWindowHint + showFullScreen()"。本窗口是唯一顶层窗口，
     *      这三件事在 v2 里要么已由 main.cpp 的 show()/QPA 全屏完成，
     *      要么会干扰 QStackedWidget 的布局，所以不再借它来做。
     *   ② 它还会**递归缩放 MainWindow 的所有子控件**（scaleChildren）。
     *      mainwindow.ui 里的坐标本来就是 1280x720 真实像素
     *      （buildDesktop() 的 1280/693/176 算式就是按这个来的），
     *      再乘一次 1.2 属于重复缩放：比如系统监控图标按钮会从 150 变成 180。
     *      桌面这一页的排版由 buildDesktop() 直接按 1280x720 摆，不需要缩放层。
     *
     * 其余页面（weather/player/audio/hardware/map/sysmon/bluetooth/incar/
     * vstatus/ota）**照旧**在各自构造函数里 UiAdapter::fit()，尺寸都是 1280x720。
     *
     * 全屏：窗口铺满 1280x720，与 stack / 各页面同尺寸，页面切来切去没有位移。
     * 这里**自己**把 v1 里 UiAdapter::fit() 干的那几件事做掉（去掉旧的最大尺寸
     * 1024x600、钉成 1280x720、无边框、置到 0,0），等到 main.cpp 的 show()
     * 一次映射上屏；后续切页只换 stack 的当前页，不会再碰窗口本身。
     */
    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    setFixedSize(1280, 720);
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);   // 车机：不要标题栏/边框
    move(0, 0);

    /*
     * ★★★ 真因修复 #1（2026-10-12 晚，实测证据见 docs/页面显示为窄列_根因与修复.md）★★★
     *
     * mainwindow.ui 里 Qt Designer 给 QMainWindow 默认塞了一个 QStatusBar。
     * QMainWindow 会把它**算进窗口高度**：真机上实测（当时用临时 SIGUSR2
     * 几何打印量的，定位完已删掉那段代码）
     *
     *     window:  frame=0,0 1280x720  client=0,0 1280x720
     *     screen:  geometry=0,0 1280x720 available=0,0 1280x720
     *     stack:   pos=0,0 size=**1280x693**      ← 少了 27px
     *     page[1] Weather: size=1280x720          ← 比 stack 高 27px
     *
     * 27px 正好是 QStatusBar 的高度。于是：
     *   · 所有页面都按 1280x720 定死尺寸（各页构造函数里的 UiAdapter::fit()），
     *     而 stack 只有 1280x693 ⇒ 每页底部 27px 被裁掉（返回键所在那一行）；
     *   · 桌面页被显式设成 1280x693，于是"首页看着正常、别的页缺一截"，
     *     正好对应工位上"除了首页其他页面都不对"的观感。
     *
     * QStatusBar 在本应用里**完全没用**（全项目没有一句 statusBar()->showMessage()；
     * player.cpp 里那个是它自己的 QStatusBar 实例，与这里无关），
     * menuBar 同理（mainwindow.ui 的 menubar 本来就高 0/没菜单）。
     * 所以直接把两者移出布局，把整整 720px 还给 central 区域。
     * 用 delete 而不是 hide()：hide() 之后 QMainWindow 仍可能把它算进高度。
     */
    delete statusBar();
    delete menuBar();

    /* 当前在屏上的页面：启动时就是桌面。
     * v2 里桌面不是 this 而是 m_desktopPage（stack 的第 0 页），
     * 真正的赋值在下面 preparePage 之后。 */
    m_currentPage = nullptr;

    // 监听 WiFi 连接结果：连上/断开会更新左上角的小字，并弹提示框
    netWatcher = new NetworkWatcher(this);
    connect(netWatcher, &NetworkWatcher::networkConnected, this,
            [this](const QString &ssid, const QString &ip) {
                qInfo().noquote() << "已成功连接网络！" << ssid << ip;
                refreshNetStatus();
            });
    connect(netWatcher, &NetworkWatcher::networkDisconnected, this,
            [this](const QString &reason) {
                qInfo().noquote() << "网络已断开！" << reason;
                refreshNetStatus();
            });
    // 左上角状态文字 + WiFi 按钮初始化
    // WiFi 开关的初始状态向板子实际状态查询，避免应用重启后显示错
    m_wifiOn = queryWifiEnabled();
    refreshNetStatus();
    refreshWifiButton();
    {
        QTimer *netTimer = new QTimer(this);
        netTimer->setInterval(2000);
        connect(netTimer, &QTimer::timeout, this, &MainWindow::refreshNetStatus);
        netTimer->start();
    }


    /*
     * ==================================================================
     *  v2 页面容器：**整个应用只有一个顶层窗口**（本 MainWindow），
     *  所有页面（含桌面自己）都是 m_stack 里的普通子控件。
     *  切页 = setCurrentWidget()，不再有窗口 unmap/map，也就不再产生
     *  "未初始化的 shm 缓冲" —— 这是白屏的真正根治（见 switchTo 的说明）。
     * ==================================================================
     */
    m_stack = new QStackedWidget(this);

    /*
     * ★★★ 真因修复 #5（2026-10-12 深夜，"音乐播放器页 / 蓝牙页坏掉"）★★★
     *
     * 现象（我用 DRM 直接读内核扫描输出，拍到的真屏，见 docs）：
     *   进"音乐播放器"页后，整页几乎还是**桌面壁纸**——
     *   把同一会话里 desktop 与 audio 两页的真屏逐点比较：
     *       右上角区域   与桌面完全相同 100.0%（最大通道差 = 0）
     *       中央大片区域 与桌面完全相同  92.3%
     *   也就是"音乐播放器页把桌面壁纸透出来了"，所以看着像坏掉/花掉。
     *   蓝牙页同理（列表区域那一大片是壁纸）。
     *
     * 根因：壁纸是写在 **MainWindow 的样式表**里的
     *   mainwindow.ui:24  QMainWindow { border-image: url(:/images/1.jpg); }
     * 在 v2（单窗口 + QStackedWidget）里，所有页面都是 MainWindow 的子控件，
     * 于是这层壁纸**铺满了整个窗口**，而页面自己不一定把背景画满：
     *   · 音乐播放器页（Audio.ui 根 `QWidget{background-color:#222233}`）在真机上
     *     并没有把这层底色画出来（应用自己 grab() 是 #222233，真屏却是壁纸 ——
     *     两条渲染路径不一致，QSS 那层底色在合成路径上没生效）；
     *   · 蓝牙页的背景也没盖住列表区域。
     *   · 而 SysMon / IncarControl / Weather 自己画了不透明底，所以它们看着正常。
     *
     * 改法（只动 mainwindow.cpp）：**把壁纸的所有权从 MainWindow 收到桌面页**。
     *   · 用 MainWindow 自己的样式表覆盖掉 .ui 里那条规则（子控件样式表优先于父的，
     *     所以这句一定赢），让 MainWindow 只做一堵"深色不透明底"；
     *   · 壁纸改由**桌面页**自己画 —— 桌面页是透明的，把它换成同一张图。
     * 这样：桌面页照样是壁纸；其它页背后不再是壁纸，而是深色底，
     * 于是"页面没画满背景"最多露出深色底，不会再出现"整页是壁纸"的错觉。
     *
     * 注：**不删** mainwindow.ui 里那条规则（.ui 一个字都没改），
     * 只是被这里覆盖；要彻底删掉它也可以，效果一样。
     */
    /* 用**带对象名限定**的选择器覆盖 .ui 里那条（子控件样式表优先于父的，
     * 这句一定赢）。限定 #MainWindow 是为了**不要顺手改到子控件**：
     * 每条页面自己的样式表优先级更高，但页面自己没说到的地方不应该被这里影响。 */
    setObjectName(QStringLiteral("MainWindow"));
    setStyleSheet(QStringLiteral(
        "QMainWindow#MainWindow { border-image: none; background-color: #0A0E17; }"));

    /* 桌面：把 .ui 里那个 centralwidget 原样搬进来当一页用。
     * 这样 ui->xxx（时钟、图标按钮、底部应用行）**一个都不用改**，
     * 它们仍然通过 ui->centralwidget 这一条父链挂在 stack 上。 */
    m_desktopPage = new QWidget;
    m_desktopPage->setObjectName(QStringLiteral("desktopPage"));
    /* 桌面页自己贴壁纸（原来靠 MainWindow 的 border-image 透出来，见上面 #5）。
     * 只给这一页设，不用后代选择器 —— 免得把应用行、列表那些控件的背景也改掉。 */
    m_desktopPage->setStyleSheet(QStringLiteral(
        "QWidget#desktopPage { border-image: url(:/images/1.jpg); }"));
    ui->centralwidget->setParent(m_desktopPage);
    m_desktopPage->resize(1280, 720);
    /*
     * ★★★ 真因修复 #2 · 这里以前是 ui->centralwidget->setGeometry(0, 0, 1280, 693) ★★★
     *
     * 那个 693 是"减去 statusbar 27px 之后 central 区域的高度"，本身没错，
     * 但它把桌面页和 centralwidget 一起钉在了 1280x693，而 stack 里其它页面
     * （各自 UiAdapter::fit() 过）是 1280x720 —— 于是"桌面这一页比别的页矮 27px"，
     * 用户看到的就是"首页会漏一行 / 别的页缺一截"。
     * 现在 statusbar 已经删掉（见构造函数开头），central 区域就是完整的 720，
     * 所以桌面页这一层**不再写死尺寸**：让它和别的页一样，由 stack 决定
     * （stack 铺满 1280x720，每页就被摆成 1280x720）。
     *
     * centralwidget 仍然要挂到桌面页下面（这样 ui->xxx 一个都不用改），
     * 尺寸交给 Qt 的父子布局：它与父同尺寸。
     */
    ui->centralwidget->move(0, 0);

    // 创建工作对象
    weather = new Weather;
    player = new Player;
    audio = new Audio;
    hardware = new Hardware;
    map = new Map;
    sysmon = new SysMon;

    /*
     * 页面统一处理：preparePageWidget()（见文件上方，剥顶窗标志 + 深色底兜底）
     * + 尺寸兜底 + addWidget()（一次性全部加进来，之后再也不增删页面）。
     *
     * ★★★ 真因修复 #3（SysMon 就是用户看到的"窄列"）★★★
     *
     * 实测（临时 SIGUSR2 几何打印，定位完已删）：
     *
     *     page[1] Weather : size=1280x720   min=1280x720  ← 构造函数里 fit() 过
     *     ...
     *     page[6] SysMon  : size=**640x480** min=0x0      ← 从来没被摆过！
     *
     * `sysmon.cpp` 的构造函数里**没有** UiAdapter::fit(this)，也没有任何
     * setFixedSize —— 它就是 QWidget 的默认尺寸 640x480。而 QStackedWidget
     * 只把"变成 current 的那一页"摆一次，它已经可见、尺寸也没变化需要，
     * 于是没有任何人把它拉到 1280x720。结果：进系统监控页时只有左上角
     * 640x480 那块画了内容（SysMon 是纯 paintEvent 相对绘制 + resizeEvent 重算），
     * 其余露出窗口底色 —— 这就是"只有左侧一条窄列显示内容"的直接来源。
     *
     * 修法（只动 mainwindow.cpp，sysmon.cpp 一个字不改）：
     * 凡是"没有任何尺寸约束"的页，说明它自己不管尺寸，这里就替它摆成目标尺寸。
     *
     * ⚠ 注意**不要**对它调 UiAdapter::fit()：fit() 是给"按 1024x600 绝对坐标画"
     * 的页面用的（它会 scaleChildren ×1.2 并 setFixedSize）；SysMon 是按
     * width()/height() 相对绘制的自适应页，套上 fit() 反而会把它钉死并重复缩放。
     * 这里只 resize，不加约束 —— 这样它依旧随窗口自适应。
     */
    auto preparePage = [this](QWidget *w) {
        if (QWidget *const p = preparePageWidget(w)) {
            /*
             * 兜底（真因修复 #5 的第二半）：给每一页铺一层**不透明深色**底。
             *
             * 为什么必须兜：真屏实测过"音乐播放器页整页透出桌面壁纸"
             * （右上角与桌面逐点完全相同 100%）。壁纸现在只画在桌面页上了，
             * 所以最坏情况也只会露出 MainWindow 的深色底；这里再给每一页
             * 明确铺一层深色（QPalette::Window + autoFillBackground），
             * 让"页面自己没画到的地方"也不会是别的东西透出来。
             *
             * 注意：页面自己写了样式表（background-color / paintEvent 自绘）的地方
             * **优先级更高**，这一层不会盖掉它们的观感；桌面页除外（它要露壁纸，
             * 而且它是主动设了 border-image 的那一页）。
             */
            if (p != m_desktopPage) {
                p->setAutoFillBackground(true);
                QPalette pal = p->palette();
                pal.setColor(QPalette::Window, QColor(0x0A, 0x0E, 0x17));
                p->setPalette(pal);
            }

            const bool unconstrained =
                (p->minimumSize() == QSize(0, 0)
                 && p->maximumSize() == QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
            if (unconstrained) {
                const QSize before = p->size();
                p->resize(1280, 720);
                qInfo().noquote() << QString("页尺寸兜底: %1 没有任何尺寸约束(原来是 %2x%3)，"
                                             "按目标 1280x720 摆")
                                         .arg(p->metaObject()->className())
                                         .arg(before.width()).arg(before.height());
            }
            m_stack->addWidget(p);
            /* addWidget 之后 reparent 到 stack；再确认一次尺寸（对已定死的页是空操作） */
            if (unconstrained)
                p->resize(1280, 720);
        }
    };

    preparePage(m_desktopPage);                 // 索引 0 = 桌面（启动时显示它）
    preparePage(weather);
    preparePage(player);
    preparePage(audio);
    preparePage(hardware);
    preparePage(map);
    preparePage(sysmon);

    /* 桌面这一页的可见性由 stack 管；currentPage 记录给切页日志/诊断用 */
    m_currentPage = m_desktopPage;

    /* stack 就位：铺满窗口、显式显示第 0 页（桌面）、并接管 QMainWindow 的
     * central widget 位置。放在页面都 addWidget 完之后做，避免中途换 current
     * 把布局算两遍。setCentralWidget() 会把 stack 从"普通子控件"提升为
     * central widget，并按 central 区域的可用空间铺满整个窗口。
     *
     * ★ 真因修复 #1 的收尾：statusbar 已经在构造函数最开头 delete 掉，
     * 所以这里的 central 区域已经是完整的 1280x720（原来是 1280x693）。
     * 上板后的正确值：stack / centralwidget / desktopPage 都是 1280x720，
     * 底部应用行 y=544（= 720 - 176）；见下方那句 "布局就位" 日志。 */
    m_stack->setGeometry(0, 0, 1280, 720);
    m_stack->setCurrentIndex(0);                // 桌面
    setCentralWidget(m_stack);
    m_stack->raise();

    /* 桌面页与 centralwidget 显式钉成 1280x720（与 stack、各页面同尺寸）。
     * 这里是"最终目标尺寸"而不是"当前 stack 尺寸"——避免拿到还没定型的值。 */
    m_desktopPage->resize(1280, 720);
    ui->centralwidget->resize(1280, 720);
    qInfo().noquote() << QString("布局就位: stack=%1x%2 desktopPage=%3x%4 centralwidget=%5x%6")
                             .arg(m_stack->width()).arg(m_stack->height())
                             .arg(m_desktopPage->width()).arg(m_desktopPage->height())
                             .arg(ui->centralwidget->width()).arg(ui->centralwidget->height());

    // 图标是 QLabel 画的，别让它挡住按钮点击
    if (ui->sysmonIcon)
        ui->sysmonIcon->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    // 当信号被触发执行相应的工作函数
    connect(this,&MainWindow::weather_starting,weather,&Weather::weather_working);

    /*
     * 网络连上后**自动刷新天气**。
     *
     * 以前天气只在"进天气页"那一刻取数（weather_working 由 weather_starting 触发），
     * 于是：开机时没网 -> 页面显示的是 .ui 里的占位数据（"昨天 01/20"），
     * 之后你把 WiFi 连上，页面也**不会自己更新**，必须退出再进一次才有数据 ——
     * 用户看到的就是"已经连上热点了怎么还是显示昨天的天气"。
     *
     * 这里挂在网络状态回调上：一旦检测到联网成功就重新走一遍取数流程。
     * 页面没打开时也会取，相当于顺手预热（打开时数据通常已经在了）。
     */
    connect(netWatcher, &NetworkWatcher::networkConnected, this,
            [this](const QString &, const QString &) {
                if (!weather)
                    return;
                qInfo() << "weather: 检测到网络已连接 -> 自动刷新天气";
                weather->weather_working();
            });
    connect(this,&MainWindow::player_starting,player,&Player::player_working);
    connect(this,&MainWindow::audio_starting,audio,&Audio::audio_working);
    connect(this,&MainWindow::hardware_starting,hardware,&Hardware::hardware_working);
    connect(this,&MainWindow::map_starting,map,&Map::map_working);
    // 系统监控页：一次采样定型即可，切回来时重新采样
    connect(this,&MainWindow::sysmon_starting,this,[=](){ sysmon->refreshNow(); });
    connect(sysmon,&SysMon::backout,this,[=](){
        switchTo(this);
    });
    // 页面对象都是 QWidget，必须在 GUI 主线程中运行。
    connect(ui->pushButton_6,&QPushButton::clicked,this,[=](){
       emit sysmon_starting();
       UiAdapter::fit(sysmon);
       switchTo(sysmon);
    });
    connect(ui->pushButton_3,&QPushButton::clicked,this,[=](){
       emit weather_starting();
       switchTo(weather);
    });
    connect(ui->pushButton,&QPushButton::clicked,this,[=](){
        emit player_starting();
        switchTo(player);
    });
    connect(ui->pushButton_2,&QPushButton::clicked,this,[=](){
       emit audio_starting();
       switchTo(audio);
    });
    connect(ui->pushButton_4,&QPushButton::clicked,this,[=](){
       emit hardware_starting();
       switchTo(hardware);
    });
    connect(ui->pushButton_5,&QPushButton::clicked,this,[=](){
       emit map_starting();
       switchTo(map);
    });

    // 环境检测页的"后台保持近距离警告"开关 → 主页左上角显示当前安全警告模式
    connect(hardware, &Hardware::backgroundAlarmChanged, this,
            [this](bool on) { refreshSafeAlarmLabel(on); });
    // 启动时先按页面里保存的状态显示一次
    refreshSafeAlarmLabel(hardware->backgroundAlarm());

    // 左下角「启动报警器」：点一次开始滴滴滴（图标变红），再点一次恢复正常（图标变紫）
    connect(ui->alarmBtn,&QPushButton::clicked,this,[=](){
        m_alarmOn = !m_alarmOn;
        hardware->setManualAlarm(m_alarmOn);
        /* 图标统一成简约线性风（见 appicons.cpp），状态只看文字：启动/停止报警器 */
        ui->alarmBtn->setIcon(QIcon(smartcarGlyphIcon(AppIcon::Bell, 112)));
        ui->alarmBtn->setIconSize(QSize(108, 108));
        ui->alarmBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: transparent; border: none; }"));
        ui->alarmLabel->setText(m_alarmOn ? QStringLiteral("停止报警器")
                                          : QStringLiteral("启动报警器"));
        qInfo() << "主页报警器按钮:" << (m_alarmOn ? "开" : "关");
    });

    // 左下角「连接 WiFi」：这是【WiFi 开关】，跟"是否连上"是两件事。
    //   灰=关闭、黄=已开启但没连上（未发现wifi）、绿=已连接
    // 点一下 = 开（会尝试连已保存的网络，连不上也保持"开"）
    // 再点一下 = 关
    connect(ui->wifiBtn,&QPushButton::clicked,this,[=](){
        if (m_wifiBusy) {
            qInfo() << "WiFi 正在处理中，忽略本次点击";
            return;
        }
        m_wifiOn = !m_wifiOn;
        m_wifiBusy = true;
        refreshWifiButton();

        const QString action = m_wifiOn ? QStringLiteral("enable") : QStringLiteral("disable");
        qInfo().noquote() << "主页 WiFi 开关:" << action;

        // 异步执行，别卡住界面（关联最多要几十秒）
        QProcess *proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, proc](int code, QProcess::ExitStatus) {
                    const QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
                    qInfo().noquote() << QString("wifi-ctl 结束 code=%1 out=[%2]")
                                             .arg(code).arg(out);
                    m_wifiBusy = false;
                    // 失败（比如网卡/配置缺失）才回退开关；"连不上"不算失败
                    if (code != 0 && out.startsWith("FAIL")) {
                        m_wifiOn = false;
                        qInfo() << "WiFi 开启失败，开关回退为关闭";
                    }
                    refreshWifiButton();
                    refreshNetStatus();
                    proc->deleteLater();
                });
        proc->start(QStringLiteral("/userdata/smart_car/wifi-ctl"), QStringList{action});
    });

    // 左下角「蓝牙」：点一下进入蓝牙界面（扫描/配对/连接）。
    // 图标颜色反映实际状态：灰=关闭、蓝=已开、绿=已连接设备。
    bluetooth = new BluetoothPage;
    /* v2：不再让它当顶层窗口（它构造函数里有 setWindowFlag(FramelessWindowHint)），
     * 交给 preparePage 剥标志 + 深色底 + addWidget 进 stack。 */
    preparePage(bluetooth);
    connect(ui->btBtn, &QPushButton::clicked, this, [=]() {
        bluetooth->bluetooth_working();
        switchTo(bluetooth);
    });
    connect(bluetooth, &BluetoothPage::backout, this, [=]() {
        refreshBtButton();
        switchTo(this);
    });
    // 蓝牙状态变化时同步主页图标
    connect(bluetooth->manager(), &BtManager::stateChanged,
            this, &MainWindow::refreshBtButton);
    connect(bluetooth->manager(), &BtManager::devicesChanged,
            this, [this](const QVector<BtDevice> &) { refreshBtButton(); });
    refreshBtButton();

    connect(this->weather,&Weather::back,[=](){
        switchTo(this);
    });
    connect(this->player,&Player::back,[=](){
        player->clearConnections();
        switchTo(this);
    });
    connect(this->audio,&Audio::back,[=](){
        switchTo(this);
    });
    connect(this->hardware,&Hardware::back1,[=](){
        switchTo(this);
    });
    connect(this->map,&Map::backout,[=](){
        switchTo(this);
    });
    // 获取当前的日期时间
    QDateTime *datatime=new QDateTime(QDateTime::currentDateTime());
    QString str = datatime->toString("yyyy年MM月dd日 ddd"); //设置显示格式
    ui->datelable->setText(str); // 设置显示
    // 获取当前时间
    QTime currentTime = QTime::currentTime();
    ui->timelable->setText(QString(currentTime.toString("HH:mm:ss")));
    // 设置定时器 每一秒触发并重新设置时间（日期也一起刷新，跨天/校准时间后也同步）
    QTimer *myTimer = new QTimer(this);
    myTimer->start(1000);
    connect(myTimer,&QTimer::timeout,this,[=](){
        const QDateTime now = QDateTime::currentDateTime();
        ui->timelable->setText(now.time().toString("HH:mm:ss"));
        ui->datelable->setText(now.toString("yyyy年MM月dd日 ddd"));
    });

    /* ---------------- CAN 接口层 + 桌面改版（2026-09-29） ----------------
     * 顺序很重要：先建 CanMgr（界面要读它的状态），再改桌面。 */
    m_can = new CanMgr(this);
    connect(m_can, &CanMgr::log, this, [](const QString &s) { qInfo().noquote() << "CAN:" << s; });
    if (!m_can->open())                       // 默认 can0 → vcan0；都不在就保持未就绪
        qWarning().noquote() << "CAN: 接口未就绪（" << m_can->lastError() << ")，桌面状态栏会显示 CAN 未就绪";

    m_incar = new IncarControl(m_can);        // v2：stack 里的一页（原来是自己一个顶层窗口）
    preparePage(m_incar);                     // 剥顶窗标志 + 深色底 + addWidget（不再需要 hide()）
    connect(m_incar, &IncarControl::back, this, [this]() {
        switchTo(this);
    });

    /* 「车辆状态」独立页：按用户要求，桌面右侧不再显示面板，改成从图标行进入 */
    m_vstatus = new VehicleStatusPage(m_can);
    preparePage(m_vstatus);
    connect(m_vstatus, &VehicleStatusPage::back, this, [this]() {
        switchTo(this);
    });

    /* 「系统升级」独立页（2026-10-12 新增，OTA）：
     * 页面本身不含 OTA 逻辑，只是板端 /usr/bin/smartcar-ota 的遥控入口。
     * 详见 otapage.h 顶部说明。 */
    m_ota = new OtaPage;
    preparePage(m_ota);
    connect(m_ota, &OtaPage::backout, this, [this]() {
        switchTo(this);
    });

    buildDesktop();
}

/*
 * ============================================================================
 *  页面切换的唯一入口 —— 修「切页白屏」的 v2（2026-10-12）
 * ============================================================================
 * 【v1 为什么没治住】v1 把顺序改成"先 show 新页、再 hide 旧页"，保证切换
 * 过程中任何时刻都至少有一个顶层窗口是 mapped 的。但真机上白屏依旧，日志里
 * 抓到的是这两行（硬证据）：
 *
 *     qt.qpa.wayland: Failed to load client buffer integration: "wayland-egl"
 *     qt.qpa.wayland: Available client buffer integrations: ()
 *
 * 也就是本板 Qt 的 Wayland 后端**没有缓冲集成**，退回了 wl_shm 软件渲染。
 * 这种情况下**每新建/映射一个顶层窗口，它第一次上屏送出的 shm 缓冲是未初始化
 * 的**（全白）。所以只要切换过程中存在 unmap/map，白帧就躲不掉 ——
 * "show/hide 的先后顺序"根本治不了它 ✗。
 *
 * 【v2 怎么治】让切换过程**根本不产生新 surface**：
 *   · 全应用只有一个顶层窗口（MainWindow），它的 surface 只在启动时建一次；
 *   · 桌面和所有页面都是 m_stack（QStackedWidget）里的普通子控件，在构造函数里
 *     一次性 addWidget 完毕（见构造函数里的 preparePage）；
 *   · 切页只做一件事：m_stack->setCurrentWidget(w)。
 *     Qt 只是把旧页 setVisible(false)、新页 setVisible(true)，
 *     两者都是**已有窗口的子控件**，走的是同一个表面（同一个 QWindow），
 *     不创建、不销毁、不重新映射任何 surface ⇒ 没有"未初始化的第一帧"。
 *
 * 顺带的好处：不再需要 raise()/activateWindow()（Wayland 下合成器可能直接
 * 忽略它们），也不存在"两个页面同时盖在屏上/退不出页面"这类状态错乱。
 *
 * 调用点：**一个都没改**（对外语义不变：桌面仍然传 this）。
 * 桌面 = this 会被映射到 m_desktopPage（stack 的第 0 页）。
 *
 * @param fullScreen v1 遗留参数，只为不改调用点而保留。v2 里页面是 stack 的
 *                   子控件，**不再调用 showFullScreen()**（那会把子控件重新
 *                   提升成顶层窗口 = 又新建 surface），所以它是空操作。
 *                   唯一调用点 openOtaForTest() 传 true 也照样工作：
 *                   OtaPage 自己 setFixedSize(1280,720)，在 stack 里就是满屏。
 */
void MainWindow::switchTo(QWidget *w, bool fullScreen)
{
    if (!w)
        return;

    (void)fullScreen;                 // v1 遗留参数，v2 里没有意义（见上方说明）

    if (!m_stack)                     // 理论上不会发生（构造函数里就建好了）
        return;

    /* 对外语义不变：桌面一直是用 this 表示的，这里翻译成 stack 里的桌面页。
     * 这样 23 个 switchTo(this) 调用点一行都不用改。 */
    if (w == this)
        w = m_desktopPage;
    if (!w)
        return;

    /* 页面必须都在 stack 里（构造函数里一次性加完），否则说明调用点传错了。
     * 自检入口（--page=xxx）也都是走这些页面，不会有例外。 */
    if (m_stack->indexOf(w) < 0) {
        qWarning().noquote() << "切页: 目标不在 QStackedWidget 里，忽略 ->"
                             << w->metaObject()->className();
        return;
    }

    /* 留一行日志：真机上排查"切页了几次、切到哪"时，这行就是证据 */
    if (m_currentPage != w) {
        qInfo().noquote() << "切页（QStackedWidget 单窗口，无新 surface）:"
                          << (m_currentPage ? m_currentPage->metaObject()->className() : "?")
                          << "->" << w->metaObject()->className();
    }

    /* 唯一的动作：换当前页。
     * 不要再对页面写 show()/hide()/showFullScreen() —— 可见性由 stack 管，
     * 手写只会破坏"stack 里永远有一页是 current"的性质（见 docs v2 一节）。 */
    m_stack->setCurrentWidget(w);

    m_currentPage = w;
}

// 刷新 WiFi 按钮的图标与文字（三态）
//   已连接   -> 绿色 + “断开 WiFi”
//   已开启未连上 -> 黄色 + “未发现wifi”（再点可尝试重连）
//   已关闭   -> 灰色 + “连接 WiFi”
void MainWindow::refreshWifiButton()
{
    if (!ui->wifiBtn || !ui->wifiLabel)
        return;

    if (m_wifiBusy) {
        ui->wifiLabel->setText(m_wifiOn ? QStringLiteral("连接中…")
                                        : QStringLiteral("关闭中…"));
        return;
    }

    const bool online = m_wifiOn && m_netOnline;
    if (!m_wifiOn) {
        ui->wifiLabel->setText(QStringLiteral("连接 WiFi"));
    } else if (online) {
        ui->wifiLabel->setText(QStringLiteral("断开 WiFi"));
    } else {
        ui->wifiLabel->setText(QStringLiteral("未发现wifi"));
    }
    /* 图标统一用简约线性风（appicons.cpp 的 Wifi 图），状态靠上面的文字表达 */
}

/*
 * 桌面改版（用户 2026-09-29 要求）
 * ============================================================================
 *   · 六个应用图标**缩小**成一排，放进底部**可横向滑动**的条里；
 *     「车内控制」排第一个（手册第 7 节），末尾跟设置类的 报警器 / WiFi / 蓝牙
 *   · 车辆状态灯区放桌面**右侧、应用行上方**（CarStatusPanel）
 *   · 状态图标收到 CAN 信号会改变形态（转向闪 / 门开摆出 / 安全带断开 / 灯束不同）
 * ========================================================================== */
void MainWindow::buildDesktop()
{
    QWidget *cw = ui->centralwidget;
    /*
     * ⚠️ 这里**不能**用 cw->width()/height()：
     *   UiAdapter 的设计画布是 1000×600，.ui 里的坐标都会在 fit() 时被 ×1.2 缩放；
     *   而构造函数里读完 fit() 之后，centralwidget 的尺寸可能还没定型
     *   （实测这里是 120 宽），拿它当基准会把右侧面板算到屏幕外
     *   （geometry.txt 里出现过 CarStatusPanel rel=-536,8）。
     * 本应用是满屏 1280×720（SMARTCAR_FULLSCREEN=1）。
     *
     * ★ 真因修复 #4（2026-10-12 晚）：这里以前写 H = 693。
     * 693 = 720 - 27（QStatusBar 的高度）—— 现在 statusbar 已经删掉，
     * central 区域/stack/每页都是完整的 1280x720，所以 H 改成"中央区域的实际高度"
     * （运行时取，取不到才退回 720）。否则底部应用行会被摆到 y=517 而不是 y=544，
     * 屏幕最下面 27px 又是空白/透出底色。
     */
    const int W = 1280;
    const int rowH = 176;
    /*
     * 底部应用行的 y 位置：桌面页高度 - rowH。桌面页 = stack = central 区域 = 720
     * （statusbar 已删）。这里**不在构造期算**，而是留到 layoutDesktopRow() 里
     * 按当时的真实高度算 —— 用户在 showEvent 之后还会再算一次，保证就算
     * 构造期 central 区域的高度还没定型（实测它有时还是 .ui 里的旧值），
     * 最终摆的位置也是对的。这是 v1 那个 H=693 写死的替代方案。
     */
    m_rowH = rowH;

    /*
     * 车辆状态面板已按要求搬到独立页面（VehicleStatusPage，从底部图标行进入）：
     * 桌面右侧不再显示面板，这块空间空出来给壁纸/时钟。
     * （原实现：m_statusPanel = new CarStatusPanel(m_can, cw);
     *             setGeometry(W - 656, 8, 648, H - rowH - 22);）
     */

    /* ---------------- 底部可滑动应用行 ---------------- */
    m_appRow = new QScrollArea(cw);
    /*
     * ★ 真因修复 #4：位置**不再**用写死的 y（原来是 0, 693-176, 1280, 176）。
     * 具体 y 交给 layoutDesktopRow()（构造期 + 首次 showEvent 各算一次），
     * 它按"桌面页的真实高度 - rowH"来摆，statusbar 删掉之后就是 720-176=544。
     */
    m_appRow->setGeometry(0, 0, W, rowH);
    m_appRow->setFrameShape(QFrame::NoFrame);
    m_appRow->setWidgetResizable(true);
    m_appRow->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_appRow->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_appRow->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollArea > QWidget > QWidget { background: transparent; }"));
    auto *rowHost = new QWidget;
    rowHost->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *lay = new QHBoxLayout(rowHost);
    /*
     * 常态下**刚好显示 7 个完整图标**、不出现被切一半的：
     *   左边距 17 + 7 × 178 + 右边距 17 = 1280（整屏宽）
     * 第 8 个图标正好从 x=1280 开始 → 常态下完全看不见，手指一滑才滚进来。
     * 因此这里间距设 0、每个 item 固定 178 宽（图标在 item 内居中）。
     */
    lay->setContentsMargins(17, 4, 17, 4);
    lay->setSpacing(0);
    m_appRow->setWidget(rowHost);
    /* 手指按住拖动就能横向滑动（触摸屏要的就是这个，不依赖滚动条） */
    QScroller::grabGesture(m_appRow->viewport(), QScroller::LeftMouseButtonGesture);
    m_appRow->show();
    m_appRow->raise();

    /* 把一个「按钮 + 文字」塞进行里（按钮统一缩到 96×96） */
    auto addItem = [&](QPushButton *btn, QLabel *lab, const QPixmap &icon = QPixmap()) {
        if (!btn)
            return;
        auto *item = new QWidget(rowHost);
        item->setStyleSheet(QStringLiteral("background: transparent;"));
        item->setFixedWidth(178);                  // 7 个刚好占满一屏，见上面的算式
        auto *v = new QVBoxLayout(item);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(1);
        btn->setParent(item);
        btn->setFocusPolicy(Qt::NoFocus);          // 免得常态下图标上挂一个焦点框
        btn->setMinimumSize(112, 112);
        btn->setMaximumSize(112, 112);
        if (!icon.isNull()) {                     // 系统监控的图原本画在单独的 QLabel 上
            btn->setIcon(QIcon(icon));
            btn->setIconSize(QSize(108, 108));
            btn->setStyleSheet(QStringLiteral(
                "QPushButton { background: transparent; border: none; }"
                "QPushButton:hover { background: rgba(255,255,255,40); }"));
        }
        if (lab) {
            lab->setParent(item);
            lab->setAlignment(Qt::AlignCenter);
            lab->setFixedHeight(28);
            lab->setStyleSheet(QStringLiteral(
                "color:#ffffff; font-size:18px; background:transparent;"));
        }
        v->addWidget(btn, 0, Qt::AlignHCenter);
        if (lab)
            v->addWidget(lab);
        lay->addWidget(item);
    };

    /* ① 车内控制（手册要求排第一个）——简约线性图标（见 appicons.cpp） */
    auto *incarBtn = new QPushButton;
    auto *incarLabel = new QLabel(QStringLiteral("车内控制"));
    addItem(incarBtn, incarLabel, smartcarGlyphIcon(AppIcon::CarAC, 112));
    connect(incarBtn, &QPushButton::clicked, this, [this]() {
        emit incar_starting();
        UiAdapter::fit(m_incar);
        switchTo(m_incar);
    });

    /* ② 车辆状态（独立页：大面板 + 最近 CAN 帧列表） */
    auto *vstatBtn = new QPushButton;
    auto *vstatLabel = new QLabel(QStringLiteral("车辆状态"));
    addItem(vstatBtn, vstatLabel, smartcarGlyphIcon(AppIcon::Gauge, 112));
    connect(vstatBtn, &QPushButton::clicked, this, [this]() {
        UiAdapter::fit(m_vstatus);
        switchTo(m_vstatus);
    });

    /* ③ 六个原有应用：也换成同一套线性图标（原来是各不相同的彩色 PNG） */
    addItem(ui->pushButton_2, ui->label_2, smartcarGlyphIcon(AppIcon::Music));    // 音乐播放器
    addItem(ui->pushButton,   ui->label,   smartcarGlyphIcon(AppIcon::Video));    // 视频播放器
    addItem(ui->pushButton_3, ui->label_3, smartcarGlyphIcon(AppIcon::Weather));  // 天气预报
    addItem(ui->pushButton_5, ui->label_5, smartcarGlyphIcon(AppIcon::Gps));      // GPS定位
    addItem(ui->pushButton_4, ui->label_4, smartcarGlyphIcon(AppIcon::Env));      // 环境检测
    addItem(ui->pushButton_6, ui->label_6, smartcarGlyphIcon(AppIcon::Sysmon));   // 系统监控
    if (ui->sysmonIcon)
        ui->sysmonIcon->hide();                        // 它的图已经不用了

    /*
     * 设置类：报警器 / WiFi / 蓝牙（原来在左下角，一起收进这一行）
     * 2026-10-01 按要求去掉了「设置」文字与竖线分隔符；它们排在 7 个主图标之后，
     * 常态下看不见，手指横向滑动才滚进来。
     */
    addItem(ui->alarmBtn, ui->alarmLabel, smartcarGlyphIcon(AppIcon::Bell));       // 启动报警器
    addItem(ui->wifiBtn,  ui->wifiLabel,  smartcarGlyphIcon(AppIcon::Wifi));       // 连接 WiFi
    addItem(ui->btBtn,    ui->btLabel,    smartcarGlyphIcon(AppIcon::Bluetooth));  // 蓝牙

    /* 系统升级（OTA）：放在设置类图标的最后，常态下同样需要横向滑动才看到。
     * 这样不会挤掉原来"刚好 7 个"的排版，也不影响任何已有入口。 */
    auto *otaBtn = new QPushButton;
    auto *otaLabel = new QLabel(QStringLiteral("系统升级"));
    addItem(otaBtn, otaLabel, smartcarGlyphIcon(AppIcon::Update));
    connect(otaBtn, &QPushButton::clicked, this, [this]() {
        openOtaForTest();
    });

    /* 底部应用行贴底：构造期先摆一次（尺寸已定型就直接到位），
     * 没定型也不要紧 —— 首次 showEvent 还会再摆一次（见 layoutDesktopRow）。 */
    layoutDesktopRow();
}

void MainWindow::openOtaForTest()
{
    if (!m_ota)
        return;
    /* ⚠ 这里**不调用** UiAdapter::fit(m_ota)：
     *   fit() 是按 1024x600 设计画布 ×1.2 缩放的，而本页用布局管理器
     *   按 1280x720 真实坐标自适应并已 setFixedSize(1280,720)。
     *   再套一层 fit() 只会把两者搞混（子控件几何被布局改回去，白忙一场）。
     *
     * v2：第二个参数（fullScreen）现在是空操作 —— 页面是 QStackedWidget 的
     * 子控件，不能再 showFullScreen()（那会把子控件重新提升成顶层窗口，
     * 又新建一个 surface，白屏就回来了）。OtaPage 自己就是 1280x720 满屏，
     * 在 stack 里显示的效果与原来的 fullscreen 一致。 */
    m_ota->refreshStatus();
    switchTo(m_ota);
}

/*
 * 自检入口统一收口（--page=xxx，见 mainwindow.h 的说明）。
 *
 * v1 时 main.cpp 里只 `emit xxx_starting()` 就够了：页面是顶层窗口，收到信号
 * 做数据准备时会自己 show() 上屏。v2 里页面是 stack 的子控件、**不能自己
 * show()**，所以必须在这里显式切一次页 —— 否则 `--page=weather` 这类自检
 * 就只会在后台取数，屏幕上什么也不变（自检截图会全变成桌面）。
 *
 * 每个分支都保持"先 emit（页面准备数据）再切页"的原有顺序。
 * 只接桌面以外的页；桌面用不到（它是初始页）。
 */
void MainWindow::openPageForTest(const QString &page)
{
    if (page == "weather") {
        emit weather_starting();
        switchTo(weather);
    } else if (page == "map") {
        emit map_starting();
        switchTo(map);
    } else if (page == "hardware") {
        emit hardware_starting();
        switchTo(hardware);
    } else if (page == "bluetooth") {
        /* scan=true（原来 main.cpp 就是传 true）：顺带验证扫描期间命令槽不卡 */
        openBluetoothForTest(true);
    } else if (page == "audio") {
        openAudioForTest();
    } else if (page == "incar") {
        openIncarForTest();
    } else if (page == "ota") {
        openOtaForTest();
    } else if (page == "sysmon") {
        /* SysMon 以前没有自检入口（它的构造里没 fit()，是本次"窄列"的两大真因之一），
         * 补上，方便上板用 --page=sysmon 直接核对它是否满屏。 */
        emit sysmon_starting();
        UiAdapter::fit(sysmon);
        switchTo(sysmon);
    } else if (page == "video" || page == "player") {
        /* 【2026-10-18 ②视频页】补一个视频页自检入口：
         * 桌面图标点进去要经过 evinject 触摸注入，坐标依赖当时的桌面布局；
         * 拍"打开对话框几何 / 过滤 / 播放列表"这类真屏证据时，用
         * `--page=video` 起步最稳（与 pushButton 的点击路径完全一致：先 emit 再切页）。 */
        emit player_starting();
        switchTo(player);
    } else {
        qWarning().noquote() << "自检: 未知页面 --page=" << page
                             << "（支持 weather/map/hardware/bluetooth/audio/incar/ota/sysmon/video/desktop）";
    }
}

void MainWindow::openIncarForTest()
{
    if (!m_incar)
        return;
    emit incar_starting();
    UiAdapter::fit(m_incar);
    switchTo(m_incar);
}

/*
 * 底部应用行（QScrollArea）的位置：贴在**桌面页的底部**。
 *
 * 为什么单独抽出来、还允许调用两次（构造期 + 首次 showEvent）：
 *   · 构造期 centralwidget 的高度可能还是 mainwindow.ui 里的旧值（实测出现过
 *     120 宽这种"没定型"的值），用它算 y 会把应用行摆歪；
 *   · 而 showEvent 时窗口已经按 1280x720 映射完，桌面页的高度就是最终值。
 * 两次都以"**当时**桌面页高度 - rowH"计算，所以哪次算都对；m_rowH 是 176，
 * statusbar 删掉后正常值就是 720-176 = 544（v2 之前是 693-176 = 517，
 * 屏幕最下面 27px 因此永远空着 —— 这就是用户看到的"少一截"）。
 *
 * 只在"桌面页高度已经合理（>= 400）"时才动它，避免拿构造期的脏值去摆。
 */
void MainWindow::layoutDesktopRow()
{
    if (!m_appRow)
        return;
    QWidget *const page = m_desktopPage ? m_desktopPage : ui->centralwidget;
    if (!page)
        return;
    const int H = page->height();
    if (H < 400)
        return;                     // 尺寸还没定型，等下一次（showEvent）再摆
    const int y = H - m_rowH;
    if (m_appRow->geometry() != QRect(0, y, 1280, m_rowH)) {
        m_appRow->setGeometry(0, y, 1280, m_rowH);
        qInfo().noquote() << QString("桌面应用行: 页高=%1 -> y=%2 h=%3").arg(H).arg(y).arg(m_rowH);
    }
}

/*
 * 回到桌面时把底部图标行滚回最左边。
 * 不加这个的话，焦点落到后面某个按钮上时 QScrollArea 会自动把它滚进可视区，
 * 用户看到的"常态"就不是前 7 个图标了（实测踩到：截图里显示的是第 5–11 个）。
 *
 * 2026-10-12 追加：顺手把底部应用行按"窗口真正定型之后"的桌面页高度重摆一次
 * （见 layoutDesktopRow() 的说明），保证它永远贴屏幕底边。
 */
void MainWindow::showEvent(QShowEvent *e)
{
    QMainWindow::showEvent(e);
    layoutDesktopRow();
    if (m_appRow && m_appRow->horizontalScrollBar())
        m_appRow->horizontalScrollBar()->setValue(0);
}

/*
 * 把每个页面各抓一张 PNG（应用自己 grab()，不依赖合成器/网络）。
 * 见 mainwindow.h 的说明；由 main.cpp 的 SIGUSR1 处理程序触发。
 *
 * v2 改动：页面不再是顶层窗口，所以**不能**再用 `w->show()` 把它们弄上屏
 * （它们也不该被 show）。改成"依次把 stack 的 currentWidget 换成它 -> grab()
 * -> 最后切回原来那一页"：
 *   · 换成当前页会让 stack 按正常路径给它布局（尺寸/事件都正确）；
 *   · QWidget::grab() 抓的是该控件自己渲染出来的内容，隐藏与否都能抓，
 *     所以即使某一页没被切到，grab() 一样有效（这里仍按上面顺序抓，稳一点）；
 *   · 输出文件名与 geometry.txt 的格式**完全不变**，诊断脚本不用改。
 *
 * 除 PNG 外还写一份 geometry.txt：每个控件的**绝对位置/尺寸/字号/字体度量**。
 * 排查"文字被裁一半"这类问题时，光看图片看不出到底是"控件太小"还是
 * "字号被样式表放大"，把数字打出来最省事。
 */
static void dumpWidgets(QTextStream &ts, QWidget *root, QWidget *w, int depth)
{
    const QPoint abs = w->mapTo(root, QPoint(0, 0));
    ts << QString(depth * 2, QLatin1Char(' '))
       << w->metaObject()->className() << " \"" << w->objectName() << "\""
       << " rel=" << w->x() << "," << w->y() << " " << w->width() << "x" << w->height()
       << " abs=" << abs.x() << "," << abs.y()
       << " bottom=" << (abs.y() + w->height())
       << " font=" << w->font().pointSizeF() << "pt"
       << " fmH=" << w->fontMetrics().height()
       << " css=" << (!w->styleSheet().isEmpty() ? "yes" : "no")
       << "\n";
    const QObjectList kids = w->children();
    for (QObject *o : kids) {
        QWidget *c = qobject_cast<QWidget *>(o);
        if (c)
            dumpWidgets(ts, root, c, depth + 1);
    }
}

void MainWindow::dumpPages(const QString &dir)
{
    QDir().mkpath(dir);

    /* 先记住用户当前在看哪一页：抓完要切回去，诊断不能改变屏幕内容 */
    QWidget *const restore = m_stack->currentWidget();

    /* ① 桌面这张图：v1 里它就是顶层窗口本身，现在等价于"桌面那一页"。
     *    先把 stack 切回桌面再抓，语义和以前一样（抓的是应用真实渲染结果）。 */
    m_stack->setCurrentWidget(m_desktopPage);

    struct Item { const char *name; QWidget *w; };
    const Item items[] = {
        { "01_main",      m_desktopPage },
        { "02_hardware",  hardware },
        { "03_weather",   weather },
        { "04_audio",     audio },
        { "05_player",    player },
        { "06_map",       map },
        { "07_sysmon",    sysmon },
        { "08_bluetooth", bluetooth },
        { "09_incar",     m_incar },
        { "10_vstatus",   m_vstatus },
        { "11_ota",       m_ota },
    };

    QFile gf(dir + QStringLiteral("/geometry.txt"));
    const bool gfOk = gf.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream ts(gfOk ? &gf : nullptr);

    /* 注：QWidget::grab() 不涉及剪贴板，不要写 clipboard()->open()/close()（Qt 没有这两个 API）。 */

    for (const Item &it : items) {
        if (!it.w)
            continue;

        /* ② 把这一页变成当前页：stack 会按正常路径让它可见并布局完
         *    （showEvent 之类都会跑到），这一刻抓到的就是它真实的样子。 */
        m_stack->setCurrentWidget(it.w);
        QApplication::processEvents();
        QApplication::processEvents();

        const QPixmap pm = it.w->grab();
        const QString path = QStringLiteral("%1/app_%2.png")
                                 .arg(dir, QString::fromLatin1(it.name));
        if (pm.save(path))
            qInfo().noquote() << "shot saved" << path << pm.width() << "x" << pm.height();
        else
            qWarning() << "shot FAILED" << path;

        if (gfOk)
            dumpWidgets(ts, it.w, it.w, 0);
    }

    /* ③ 抓完切回原来的页面（诊断不能改变用户看到的东西） */
    if (restore)
        m_stack->setCurrentWidget(restore);

    if (gfOk) {
        gf.close();
        qInfo() << "geometry dump ->" << (dir + "/geometry.txt");
    }
}

/* 自检入口：不经过点击直接进指定页（--page=xxx 用） */
void MainWindow::openAudioForTest(){
    if (!audio)
        return;
    emit audio_starting();
    switchTo(audio);
}

/* 自检入口：不经过点击直接进蓝牙页（--page=bluetooth 用） */
void MainWindow::openBluetoothForTest(bool scan){
    if (!bluetooth)
        return;
    bluetooth->bluetooth_working();
    switchTo(bluetooth);
    if (scan) {
        /* 等 refresh 把适配器状态读回来（powered）再开扫 */
        QTimer::singleShot(2500, bluetooth, [this]() {
            bluetooth->triggerScanForTest();
        });
        /* 扫描进行中再插一条 devices 刷新，验证命令槽没被扫描占死 */
        QTimer::singleShot(6000, bluetooth, [this]() {
            bluetooth->manager()->refresh();
        });
    }
}

// 蓝牙按钮图标：灰=蓝牙关闭、蓝=已开、绿=已连接设备（标签文字同步）
void MainWindow::refreshBtButton()
{
    if (!ui->btBtn || !ui->btLabel)
        return;

    const BtManager *bt = bluetooth ? bluetooth->manager() : nullptr;
    if (!bt) {
        ui->btLabel->setText(QStringLiteral("蓝牙"));
        return;
    }

    const bool connected = !bt->connectedDevices().isEmpty();
    const bool on = bt->powered();

    /* 图标统一简约线性风（appicons.cpp 的 Bluetooth 图），状态看文字 */
    if (connected) {
        ui->btLabel->setText(bt->connectedDevices().first().displayName());
    } else if (on) {
        ui->btLabel->setText(QStringLiteral("蓝牙已开"));
    } else {
        ui->btLabel->setText(QStringLiteral("蓝牙"));
    }
}

// 查询 WiFi 开关的真实状态（给启动时用，避免应用重启后状态显示错）
bool MainWindow::queryWifiEnabled(){
    QProcess p;
    p.start(QStringLiteral("/userdata/smart_car/wifi-ctl"), QStringList{QStringLiteral("status")});
    if (!p.waitForFinished(3000))
        return false;
    const QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    const int rc = p.exitCode();
    // rc: 0=已连接  1=已开启  2=已关闭
    return (rc == 0 || rc == 1) || out.startsWith("connected") || out == "enabled";
}

// 左上角"安全警告"模式文字：
//   开启（后台保持近距离警告）→ 绿色 "安全警告开启中"
//   关闭                        → 淡红 "安全警告关闭"
void MainWindow::refreshSafeAlarmLabel(bool on)
{
    if (!ui->safeAlarmLabel)
        return;

    if (on) {
        ui->safeAlarmLabel->setText(QStringLiteral("安全警告开启中"));
        ui->safeAlarmLabel->setStyleSheet(
            "font: 11pt \"Agency FB\"; color: rgb(140, 230, 170); background: transparent;");
    } else {
        ui->safeAlarmLabel->setText(QStringLiteral("安全警告关闭"));
        ui->safeAlarmLabel->setStyleSheet(
            "font: 11pt \"Agency FB\"; color: rgba(255, 180, 180, 200); background: transparent;");
    }
    qInfo().noquote() << "安全警告模式:" << (on ? "开启中" : "关闭");
}

// 左上角网络状态小字：
//   已连接   -> 绿色 “已连接网络 <当前IPv4>”
//   WiFi 开着但没连上 -> 黄色 “未发现wifi”
//   WiFi 关闭  -> 淡红 “没有网络连接”
void MainWindow::refreshNetStatus()
{
    if (!ui->netStatusLabel)
        return;

    /*
     * 【2026-10-18 ①「IP 实时显示」】
     *
     * 以前这个函数只判"有没有 IPv4"（online 布尔），界面上**根本不显示地址**；
     * 全应用唯一显示 IP 的地方是"连上网络"那一刻的弹窗，而它的值来自
     * /var/run/smartcar-net.status 第 3 行 —— 一次性事件快照
     *（networkwatcher.cpp:44-51，诊断报告 §2.2）。所以 DHCP 换地址以后，
     * 屏上还是旧 IP，用户看到的"不准"就是这么来的。
     *
     * 现在把**当前网卡 IPv4** 直接拼到这块既有的状态文字上：
     *   · 取值走 NetworkWatcher::currentIPv4()（wlan* 优先，其次 eth*，
     *     排除 lo/docker/veth/br-/virbr/usb，跳过 169.254.*）——
     *     弹窗与这里共用同一处实现，不会漂移；
     *   · 本函数由 QTimer 每 2000ms 调一次（:175-180），所以显示**本身就是实时的**：
     *     地址一变，2 秒内文字跟着变，无需重启应用。
     *   · 公网出口 IP（天气页"按 IP 定位"用的那个）**不在**这里，也不动。
     *
     * 取舍说明（为什么选"拼进既有 label"而不是新加控件）：
     *   · 这块 label 本来就在屏上、位置/字体/配色都是调好的（mainwindow.ui:45-62，
     *     291x30 @ (42,56)，按 1.2 放大后 349x36，文案长度够放下一个 IPv4）；
     *   · 新加控件要动 .ui、要重新走 UiAdapter::fit() 的缩放记录，风险更大；
     *   · 用户要的是"看到当前 IP 是什么"，和"已连接网络"是同一句话。
     */
    const QString ipv4 = NetworkWatcher::currentIPv4();
    const bool online = !ipv4.isEmpty();

    static int lastState = -1;   // -1 未知 / 0 断网 / 1 有网
    static QString lastIp;       // 上一次显示的地址（地址变了也要留一条日志，便于取证）
    const int now = online ? 1 : 0;
    if (now != lastState || ipv4 != lastIp) {
        lastState = now;
        lastIp = ipv4;
        qInfo().noquote() << "网络状态:" << (online ? "已连接网络" : "没有网络连接")
                          << (online ? ipv4 : QString());
    }

    // 记录真实联网状态（供 WiFi 按钮决定黄/绿）
    m_netOnline = online;

    // 左上角小字三态：已连接 / WiFi开着但没连上 / WiFi关闭
    if (online) {
        ui->netStatusLabel->setText(QStringLiteral("已连接网络 ") + ipv4);
        ui->netStatusLabel->setStyleSheet(
            "font: 11pt \"Agency FB\"; color: rgb(140, 230, 170); background: transparent;");
    } else if (m_wifiOn) {
        ui->netStatusLabel->setText(QStringLiteral("未发现wifi"));
        ui->netStatusLabel->setStyleSheet(
            "font: 11pt \"Agency FB\"; color: rgb(255, 205, 70); background: transparent;");
    } else {
        ui->netStatusLabel->setText(QStringLiteral("没有网络连接"));
        ui->netStatusLabel->setStyleSheet(
            "font: 11pt \"Agency FB\"; color: rgba(255, 180, 180, 190); background: transparent;");
    }

    // 只更新按钮外观，【不】用联网状态去改开关（开关由用户点击控制）
    refreshWifiButton();
}

/*
 * 析构：v2 里**所有页面（含桌面页）都是 m_stack 的子控件**，
 * m_stack 又是 MainWindow 的子控件 —— Qt 的父子对象机制已经会按顺序删掉它们，
 * 所以这里**不能再**手工 delete map/hardware/audio/player/weather/sysmon
 * （v1 时它们是无父对象的顶层窗口，才需要手工删；现在手工删就是二次释放）。
 *
 * 顺序说明：MainWindow 析构时先走本函数体（delete ui 只是释放 Ui::MainWindow
 * 这个 POD 结构，里面存的是指针，不拥有控件），随后 ~QMainWindow/~QWidget/~QObject
 * 删除子对象树：m_stack -> desktopPage/weather/player/... 全部随之释放。
 */
MainWindow::~MainWindow()
{
    delete ui;
}