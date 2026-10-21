#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "player.h"
#include "Audio.h"
#include "weather.h"
#include "hardware.h"
#include "map.h"
#include "sysmon.h"
#include "networkwatcher.h"
#include "bluetoothpage.h"
#include "canmgr.h"
#include "carstatuspanel.h"
#include "incarcontrol.h"
#include "vehiclestatuspage.h"
#include "otapage.h"

class QStackedWidget;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    /* 自检用：不点屏直接打开蓝牙页（配合 --page=bluetooth）。
     * scan=true 时顺便触发一次扫描，用来验证"扫描进行中仍能发其它命令"，
     * 因为扫描以前会把命令槽占死（详见 BtManager::startScan 注释）。 */
    void openBluetoothForTest(bool scan = false);

    /*
     * 取证用：把每个页面都渲染成 PNG 存到 dir。
     *
     * 为什么需要它：板子上 `weston-screenshooter` 会报 "unauthorized"（合成器
     * 不允许随便抓屏），网口也可能不通，于是"真机上到底画成什么样"就没法取证。
     * 用 QWidget::grab() 由应用自己抓，是最可靠的一条路：
     *   kill -USR1 $(pidof Smart_in-car_entertainment)   -> /tmp/appshot/*.png
     * 抓到的是**应用窗口的真实渲染结果**（窗口本身就是整屏 1280x720）。
     *
     * v2（单窗口 QStackedWidget）之后：页面不再是顶层窗口，这里改成
     * "依次把 stack 里的每一页切成当前页 -> grab() -> 最后切回原来那页"，
     * 语义和输出文件名都不变（见 .cpp 里的实现与 docs）。
     */
    void dumpPages(const QString &dir);

    /* 自检入口：不经过点屏直接进音乐页（--page=audio 用，方便离线截图核对排版） */
    void openAudioForTest();

    /* 自检入口：不经过点屏直接进「车内控制」页（--page=incar 用） */
    void openIncarForTest();

    /* 自检入口：不经过点屏直接进「系统升级」页（--page=ota 用，OTA 验证要截图取证） */
    void openOtaForTest();

    /*
     * 自检入口（--page=xxx 的统一收口，2026-10-12 v2 新增）：
     *   week/weather/map/hardware/bluetooth/audio/incar/ota/desktop
     *
     * 为什么 v2 必须补这一个：v1 里页面是**顶层窗口**，`emit weather_starting()`
     * 之后页面自己会 show() 上屏，所以 main.cpp 里只 emit 信号也能看到页面；
     * v2 里页面是 QStackedWidget 的**子控件**，绝不允许自己 show()，
     * 必须显式走一次 switchTo()。所有切页都留在 MainWindow 内部完成
     * （switchTo 仍是 private 的唯一入口），main.cpp 只调用这里。
     */
    void openPageForTest(const QString &page);

protected:
    /*
     * 每次回到桌面都把底部图标行的滚动位置归零。
     * 踩过的坑：某个控件拿到焦点时 QScrollArea 会自动滚到它那里，
     * 于是"常态"下看到的不是前 7 个图标，而是被滚到末尾的一屏。
     */
    void showEvent(QShowEvent *e) override;


signals:
    void weather_starting();
    void player_starting();
    void audio_starting();
    void hardware_starting();
    void map_starting();
    void sysmon_starting();
    void incar_starting();


private:
    Ui::MainWindow *ui;
    Player* player;
    Audio* audio;
    Weather* weather;
    Hardware* hardware;
    Map* map;
    SysMon* sysmon;
    BluetoothPage* bluetooth = nullptr;
    class NetworkWatcher *netWatcher = nullptr;

    /* ---------- CAN / 车辆状态（2026-09-29 新增） ---------- */
    CanMgr         *m_can = nullptr;          // SocketCAN 收发 + 超时 + 演示模式
    CarStatusPanel *m_statusPanel = nullptr;  // （桌面右侧面板已按要求搬到独立页面，保留成员备用）
    VehicleStatusPage *m_vstatus = nullptr;   // 「车辆状态」独立页（大面板 + 最近 CAN 帧）
    IncarControl   *m_incar = nullptr;        // 「车内控制」页（空调/车窗/灯光）
    OtaPage        *m_ota = nullptr;          // 「系统升级」页（OTA，2026-10-12 新增）
    class QScrollArea *m_appRow = nullptr;    // 底部可横向滑动的应用行
    int m_rowH = 176;                         // 应用行高度（buildDesktop 里设定）
    void buildDesktop();                      // 桌面改版：图标行 + 状态面板
    /* 底部应用行贴到桌面页底边（构造期 + 首次 showEvent 各算一次，见 .cpp 说明） */
    void layoutDesktopRow();

    /* ==================================================================
     * v2：单窗口 + QStackedWidget（2026-10-12，修「切页白屏」的**真正**根治）
     *
     * 板子上 Qt 的 Wayland 后端**没有缓冲集成**，退回 wl_shm 软件渲染：
     *     qt.qpa.wayland: Failed to load client buffer integration: "wayland-egl"
     *     qt.qpa.wayland: Available client buffer integrations: ()
     * 于是**每新建/映射一个顶层窗口，它的第一个 shm 缓冲都是未初始化的** ——
     * 表现就是"白盖一帧"。v1（先 show 新页、再 hide 旧页）治不了它：
     * 只要切换过程中有窗口 unmap/map，白帧就一定会出现。
     *
     * v2 的办法是**让切换过程根本不产生新 surface**：
     *   · 整个应用只有一个顶层窗口（就是本 MainWindow）；
     *   · 桌面本身也是 stack 里的一页（m_desktopPage）；
     *   · 所有页面在构造函数里**一次性** addWidget 进 m_stack；
     *   · 切页 = m_stack->setCurrentWidget(w)，只是把已存在的子控件换到前面，
     *     不 show()/hide() 任何窗口、不创建/销毁任何 surface。
     * ================================================================== */
    QStackedWidget *m_stack = nullptr;        // 唯一的页面容器（铺满 central 区域）
    QWidget *m_desktopPage = nullptr;         // 桌面（原 ui->centralwidget 被搬进来当一页）

    /* ------------------------------------------------------------------
     * 页面切换的**唯一入口**（对外语义不变，内部改成 QStackedWidget）
     *
     * 所有切页（桌面 -> 页面、页面 -> 桌面、--page=xxx 自检入口）都走它，
     * 调用点一个都不用改。
     *
     * @param w          要切到的页面（桌面传 this：内部映射到 m_desktopPage）
     * @param fullScreen v1 遗留参数，只为不改调用点而保留；
     *                   v2 里页面是 stack 的子控件，**不再调 showFullScreen()**，
     *                   所以它是无害的空操作（见定义处的说明）。
     * ------------------------------------------------------------------ */
    void switchTo(QWidget *w, bool fullScreen = false);
    QWidget *m_currentPage = nullptr;         // 当前在屏上的页面（桌面 = m_desktopPage）

    bool m_alarmOn = false;   // 主页左下角“启动报警器”开关状态
    bool m_wifiOn = false;    // WiFi 开关是否打开（用户意图，与"是否连上"无关）
    bool m_wifiBusy = false;  // 正在开启/关闭中（防止重复点击）
    bool m_netOnline = false; // 当前是否真的连上网（有非 lo 网卡拿到 IPv4）
    // 左上角网络状态文字：已连接网络 / 未发现wifi / 没有网络连接
    void refreshNetStatus();
    // 刷新 WiFi 按钮：灰=关闭、黄=已开启未连上、绿=已连接
    void refreshWifiButton();
    // 查询 WiFi 开关实际状态（后台调 wifi-ctl status）
    bool queryWifiEnabled();
    // 更新左上角"安全警告"模式文字
    void refreshSafeAlarmLabel(bool on);
    // 刷新蓝牙按钮图标：灰=关闭、蓝=已开、绿=已连接设备
    void refreshBtButton();
};
#endif // MAINWINDOW_H
