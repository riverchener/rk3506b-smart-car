#ifndef APPICONS_H
#define APPICONS_H

#include <QPixmap>

/*
 * appicons.h —— 桌面底部图标行的**简约线性图标**（2026-10-01）
 * ============================================================================
 * 为什么不用原来那些 PNG（Music.png / Video1.png / Weather.png / gps.png …）：
 *   它们风格各异（吉他、彩色场记板、蓝水滴…），一排看过去很花。
 *   这里统一成极简风：**细白描边 + 圆头线帽 + 极淡的圆角底**，
 *   底只是为了让图标在深色壁纸上也看得清，不是实心色块。
 *
 * 用法： QPixmap pm = smartcarGlyphIcon(AppIcon::Music, 112);
 *        btn->setIcon(QIcon(pm)); btn->setIconSize(pm.size());
 *        （控件样式表要把 border-image 去掉，见 MainWindow::buildDesktop 的 addItem）
 */
namespace AppIcon {
enum Kind {
    CarAC = 0,      // 车内控制：车形 + 雪花
    Gauge,          // 车辆状态：仪表盘 + 指针
    Music,          // 音乐播放器：音符
    Video,          // 视频播放器：播放三角
    Weather,        // 天气预报：太阳 + 云
    Gps,            // GPS：定位针
    Env,            // 环境检测：水滴
    Sysmon,         // 系统监控：显示器 + 脉搏线
    Bell,           // 启动报警器：铃铛（响时用红色）
    Wifi,           // 连接 WiFi：扇形信号
    Bluetooth,      // 蓝牙：B 符文
    Update,         // 系统升级：向下箭头 + 双向循环弧（2026-10-12 新增，OTA）
    Count
};
}

/* 画一个图标；size 是正方形边长（默认 112，和图标行一致）。
 * line 可在需要状态色时传（如 WiFi 已连接=绿、报警中=红），不传就是统一的近白色。 */
QPixmap smartcarGlyphIcon(int kind, int size = 112, const QColor &line = QColor());

#endif // APPICONS_H
