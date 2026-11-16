# RK3506B 多核异构车载系统

> 一块芯片里跑两个系统：**3× Cortex-A7 上的 Linux 负责体验，1× Cortex-M0 上的 RT-Thread 负责微秒级实时**。
> 11 个页面的 Qt5 车载界面 + CAN 总线车况 + 蓝牙音箱/AVRCP 控制 + 可自证可回滚的 OTA。

![Platform](https://img.shields.io/badge/Platform-RK3506B-0b7285)
![Linux](https://img.shields.io/badge/Linux-6.1-1f6feb)
![Qt](https://img.shields.io/badge/Qt-5.15-41cd52)
![RT-Thread](https://img.shields.io/badge/RT--Thread-M0%20%C2%B7%2032KiB-e63946)
![C++](https://img.shields.io/badge/C%2B%2B-33k%20lines-00599c)
![License](https://img.shields.io/badge/License-MIT-yellow)

---

## 这是什么

一个完整的车载娱乐终端，从驱动到界面到 OTA 全部自己做。目标板是**正点原子 ATK-DLRK3506B**（瑞芯微 RK3506B），5.5 寸 MIPI 触摸屏。

它不是"把桌面程序搬到板子上"。它要解决的核心问题是：

> **Linux 的调度抖动是毫秒级的，而超声波测距需要微秒级精度。怎么办？**

答案是**异构（AMP）**：把实时外设交给同一颗芯片里的 Cortex-M0，A7 只负责体验。两个核之间用 **rpmsg** 通信。

```
┌─────────────────────────────────────────────────────────────────────┐
│                    Qt5 应用（A7 · Linux 6.1）                        │
│   MainWindow · 唯一顶层窗口 · QStackedWidget 管 11 个页面             │
│   桌面 音乐 视频 天气 地图 环境 监控 车况 车内控制 蓝牙 系统升级        │
├─────────────────────────────────────────────────────────────────────┤
│   Hal 单例（hal.cpp · 1,247 行）—— 所有硬件的唯一入口                 │
├─────────────────────────────────────────────────────────────────────┤
│   sc_sensors.ko（内核模块 · ~1,050 行）                              │
│   ├─ rpmsg 客户端 ↔ M0                                               │
│   └─ DHT11 bit-bang（A7 本地）                                       │
├─────────────────────────────────────────────────────────────────────┤
│   rpmsg（vring 共享内存 + mailbox 中断）· EPT 0x4004                 │
├─────────────────────────────────────────────────────────────────────┤
│           Cortex-M0 · RT-Thread · 32 KiB 用得零余量                   │
│   sc_rpmsg.c · sc_sr04.c · sc_carctl.c · UART4 @ 1,500,000 bps      │
└─────────────────────────────────────────────────────────────────────┘
         ↕ CAN 500k          ↕ 蓝牙 A2DP+AVRCP      ↕ WiFi      ↕ OTA/HTTP
```

---



## 功能

| 页面 | 内容 |
|---|---|
| **桌面** | 12 个 `QPainter` 代码画的线性图标（不是 PNG，可用状态色），状态栏显示网络/IP/时间 |
| **音乐** | 旋转黑胶唱片、三行同步歌词、本地曲库扫描、播放列表、收藏、倍速。`Audio.cpp` 2,297 行 |
| **视频** | GStreamer 播放、`QVideoProbe`、SQLite 播放记录 |
| **天气** | 实时 + 逐时曲线 + 7 日预报 + 生活指数。城市来源优先级：GPS > IP 定位 > 配置兜底 |
| **地图** | 瓦片下载缓存 + 定位标记，纯 `paintEvent` 绘制 |
| **环境检测** | 温湿度 / 超声波距离 / MQ-135 空气质量（含接线真实性判定） |
| **系统监控** | CPU / 内存 / 芯片温度 / 磁盘 / 负载 / 进程数，自绘深色卡片 |
| **车辆状态** | 写实车模自绘 + 环绕 HUD 遥测 + 最近 CAN 帧。`carstatuspanel.cpp` 1,084 行 |
| **车内控制** | 空调温度/风量/风向、车窗、灯光、工作模式（走 CAN 上行帧） |
| **蓝牙** | 扫描/配对/连接、当音箱出声、AVRCP 控制手机、显示手机歌词 |
| **系统升级** | OTA 操作面板（磁盘版本 / 运行版本 / 检查更新 / 回滚 / 日志） |

### CAN 协议

| 方向 | ID | 内容 |
|---|---|---|
| 下行 | `0x100` | 车速 · 档位 · 转向 · 刹车 · 倒车 · 安全带 · 手刹 · 双闪 |
| | `0x101` | 车门 · 舱盖 · 锁止 |
| | `0x102` | 灯光反馈 · 空调 · 循环 · 内外温度 |
| | `0x103` | 总里程 · 小计里程 · 油量 · 续航 |
| | `0x104` | 瞬时/平均油耗 · 工作模式 · 故障码 |
| | `0x105` | 四轮胎压 · 胎温 |
| 上行 | `0x201`~`0x204` | 空调 / 车窗 / 灯光 / 工作模式 设定 |

**超时语义**（1,000 ms，每组独立）：
- **数值类** → 保留上一个值，标记不新鲜，界面暗灰（突然显示 0 会让司机以为车熄火了）
- **状态类** → 清零置无效，显示"关"/"无"/`--`
- `0x100` 超时 → 额外清掉 CAN 掩码（否则转向灯一直闪、倒车一直响）

### rpmsg 协议

```c
struct sc_frame {
    u16 cmd; u16 seq;
    u32 status;
    u32 a, b, c;
    u32 t;
} __packed;
```

13 条命令：`PING` / `TIME` / `JITTER` / `BUZZER` / `SR04` / `DHT11` / `LOOP` / `INFO` / `SUBSCRIBE` / `LATEST` / `TONE` / `PUSH_DHT` / `CARCTL`。主动推送 `0x8081 SC_EVT_SAMPLE`。

### OTA

**四条设计原则**：原子替换 · 升级前留 slot · 升级后自证 · 失败自动回滚。

| 特性 | 实现 |
|---|---|
| 原子替换 | 同分区 `.new` + `mv`（`rename()` 同文件系统内原子） |
| 确定性打包 | gzip `mtime=0` + tar 成员 mtime/uid/gid/uname 全固定 → 同输入必同 md5 |
| 板端零依赖 | 31.5 KB POSIX sh，只用 busybox（wget/md5sum/tar/sed） |
| 版本比对 | 版本串编进 ELF，三重取证 |
| 固件层三级闸 | `check` / `dryrun` / `flash --yes`（默认拒绝） |
| 实测 | 7 次真实升级（1.1.0→1.6.0）+ 1 次回滚演练 + 2 次坏包演练 |

---

## 硬件接线

| 外设 | 引脚 | GPIO | 排针 | 注意 |
|---|---|---|---|---|
| DHT11 DATA | GPIO1_C4 | **52** | **A25** | **必须 4.7k~10k 上拉到 3.3V**。AMP 下不能用 51：51=GPIO1_C3=A28 被 M0 的 UART4_RX 占用 |
| SR04 TRIG | GPIO1_C6 | 54 | A23 | 直连 |
| SR04 ECHO | GPIO1_C7 | 55 | A24 | ⚠ **必须 1k/2k 分压**（模块输出 5V，板子 IO 是 1.8V） |
| 蜂鸣器 | GPIO1_D0 | 56 | A21 | 低电平触发，`SMARTCAR_BUZZER_ACTIVE_LOW=1` |
| MQ-135 AO | SARADC_IN3 | — | B39 | 1.8V 量程，注意分压 |
| 转向灯 L / R | GPIO1_C0 / C1 | 48 / 49 | A29 / A30 | 由 M0 驱动 |
| CAN | RM_IO30/31 | — | — | 500 kbit/s，采样点 87% |
| 屏 | MIPI DSI | — | — | 原生 720×1280 竖屏，Weston `rotate-90` 转 1280×720 |
| M0 调试串口 | UART4 | — | — | **1,500,000 8N1**（不是 115200） |


---

## 代码规模

| 文件 | 行数 | 说明 |
|---|---|---|
| `Audio.cpp` | 2,297 | 音乐页全部逻辑（占应用源码约 1/6） |
| `hal.cpp` | 1,247 | 硬件抽象层实现 |
| `mainwindow.cpp` | 1,217 | 主窗口 + 11 页调度 |
| `carstatuspanel.cpp` | 1,084 | 写实车模自绘 + HUD |
| `btmedia.cpp` | 913 | AVRCP 元数据 / 封面 / 歌词 |
| `canmgr.cpp` | 899 | CAN 收发 + 超时 + 演示模式 |
| `player.cpp` | 782 | 视频播放器 |
| `weather.cpp` | 724 | 天气业务 |
| `btmanager.cpp` | 723 | 蓝牙扫描/配对/连接 |
| `otapage.cpp` | 607 | OTA 面板 |
| `amp/sc_sensors.c` | ~1,050 | 内核模块 |
| `amp/rtthread/applications/sc_rpmsg.c` | 17 KB | M0 rpmsg 主体 |
| `board/smartcar-ota` | 31.5 KB | 板端 OTA 客户端（POSIX sh） |

A7 应用约 **33 个 .cpp / 35 个 .h**，总计三万余行。

---

## 依赖

| 组件 | 版本 | 用途 |
|---|---|---|
| Linux | 6.1（RK 厂商内核） | A7 系统，含 CAN / rpmsg / IIO |
| Qt | 5.15 | core gui network xml multimedia multimediawidgets widgets sql serialport |
| RT-Thread | 4.x | M0 实时系统 |
| rpmsg-lite | NXP | 核间通信（含本仓库新写的 RT-Thread 环境层） |
| BlueZ | 5.77 | 蓝牙，`MediaControl1` / `MediaPlayer1`（标 Deprecated 但可用） |
| bluealsa | — | A2DP Sink + `bluealsa-aplay` |
| GStreamer | — | Qt Multimedia 后端 |
| Weston | — | Wayland 合成器（`rotate-90` + `panel-position=none`） |

---

## 已知限制

| 限制 | 原因 |
|---|---|
| 蓝牙音源**不能拖动进度** | AVRCP 没有 seek 命令（协议限制，`seekSupported()` 恒 false） |
| 蓝牙音源歌词偶有不准 | AVRCP 元数据语义随 App/播放形态变化，按字段形态判定，拿不准时显示中性占位 |
| M0 固件零内存余量 | 32 KiB 全部占满，加一行 `printf` 就会链接失败 |
| 根文件系统只剩 ~1.4 MB | 一切运行时产物必须落 `/userdata` |
| 屏幕只能是横的 | 依赖 Weston `transform=rotate-90` |

---



## License

MIT。第三方组件（Rockchip HAL、NXP rpmsg-lite、Qt 例程衍生文件）遵循其原始许可证，详见各文件 SPDX 头。

---

## 致谢

- 正点原子 ATK-DLRK3506B 官方 SDK 提供的 HAL 与 rpmsg 基础代码
- NXP RPMsg-Lite
- Qt 官方 Media Player 示例（`playercontrols` / `playlistmodel` / `videowidget` 的来源）

---

