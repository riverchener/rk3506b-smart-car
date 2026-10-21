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

## 为什么值得看

### 1. 32 KiB 里塞进一个实时服务

M0 只有 **32 KiB SRAM**，而且**代码、数据、堆、栈全在里面**（不像 A7 代码可以从 DDR 跑）。最终占用：

```
image (text+data+bss)     :  30,192 B
heap  (链接期 ASSERT >=1536):  1,552 B
MSP stack                 :   1,024 B
------------------------------------------
TOTAL                     :  32,768 B  of 32768  -> 0 B spare
```

为此做的取舍：`-Os` 而非 `-O2`、砍掉 unity/coremark/simple-console（它们引用 `printf` 会把 newlib 整套 `vfprintf/dtoa` 拉进来）、自写最小 libc 垫片、**rpmsg ISR 表从 510 项砍到 16 项**、把 2 秒周期的 DHT11 让回 A7。

### 2. 一个 1.5 秒的 `write()`

`rpmsg_sendto()` 在 M0 停止收割接收 vring 时会走完整个 **500 ms 超时**。车控路径里有 3 次阻塞发送 —— 等于 **1.5 秒抱着 mutex**。表现是"板子卡死、转向灯按下去半天没反应"。

实测证据：vring 索引每秒采样一次、共 20 次，**一个 bit 都没动**。

解法：日常通路全部 `rpmsg_trysendto()` 非阻塞，丢帧不阻塞。只有 name-service 的 SUBSCRIBE 保留阻塞（它在 probe/remove 时必须发出去，没有重试路径）。

### 3. 一个从外面完全看不出来的 rpmsg 死锁

A7 侧 `rk_rpmsg_notify()` **单次握手之后只会为奇数队列 id 发通知**。`rpmsg_lite_remote_init()` 里 vring 是交换的。如果 M0 侧漏了 `env_isr(RL_GET_VQ_ID(link_id, 1))`：

- M0 正常起来，正常 announce，日志全绿；
- 但**一帧 A7 数据都不消费**，vring1 的 `used.idx` 永远是 0；
- A7 的 64 个发送描述符永不归还，dmesg 全是 `-12`。

所以 `build_m0_rtt.sh` 在**构建期**就 `grep` 拦住这个回归。

### 4. 切页闪白：一个平台层缺陷导致的架构选择

板子 Qt 的 Wayland 后端**没有缓冲集成**，退回软件渲染：

```
qt.qpa.wayland: Failed to load client buffer integration: "wayland-egl"
```

后果：**每新建/映射顶层窗口，它的第一个 shm 缓冲都是未初始化的内存** → 白盖一帧。怎么调 `show()`/`hide()` 顺序都治不了。

唯一解是让切换过程**根本不产生新 surface**：整个应用只有一个顶层窗口，桌面本身也是 `QStackedWidget` 里的一页，所有页面构造时一次性 `addWidget`。切页就一句 `m_stack->setCurrentWidget(w)`。

### 5. "节点能打开" ≠ "设备存在"

没接 MQ-135 模块，界面却显示"空气质量 50%"徘徊。

根因：ADC 的 sysfs 节点在任何板子上都存在，悬空脚被拉到中点电平 → 量程一半。

正确判据是**抓一串原始值看波形性质**：

| 波形 | 判定 |
|---|---|
| 极差 ≤4 且贴 0/满量程 | 被拉死，**未接** |
| 极差 ≤4 且停在中段 | 悬空，**未接** |
| 极差小但**单调缓慢漂移** | 悬空脚漏电，**未接**（本板实测 718→675，每 2 秒掉 2~3 码） |
| 有真实随机噪声 | **已接** |

### 6. "我怎么知道板子上跑的就是我刚编的"

踩过两次真实事故：

- 板上应用既不是原始版也不是我编的版本 → 根因是另一个进程在**同一个增量构建目录里并发 `make`**；
- 本地与 VM 同名源文件**大小完全相同、内容不同** → 只比大小的同步会全部跳过。

三重防线：

| 防线 | 做法 |
|---|---|
| 构建隔离 | 专用目录 `build_rk3506_ota`，绝不共用 |
| 同步判据 | 用**内容 md5**，不看大小 |
| 版本取证 | 版本串编进 ELF `.rodata`，三重取证 |

**三重取证**是 OTA 的核心：

```bash
# ① 文件 md5
md5sum /userdata/smart_car/Smart_in-car_entertainment

# ② ELF 里的版本串
grep -ao 'SMARTCAR_OTA_VERSION=[0-9A-Za-z._-]*' \
     /userdata/smart_car/Smart_in-car_entertainment

# ③ 最重要：运行中进程的 exe 的版本串
grep -ao 'SMARTCAR_OTA_VERSION=[0-9A-Za-z._-]*' \
     $(readlink /proc/$(pidof Smart_in-car_entertainment)/exe)
```

只有 ③ 有真说服力。①② 都可能"文件换了但进程还是老的"。

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

## 快速开始

### 你需要

| 硬件 | 软件 |
|---|---|
| ATK-DLRK3506B 开发板 | RK3506 SDK（`atk_dlrk3506_linux6.1_release_v1.3.1_20260326`） |
| 5.5" MIPI DSI 屏（720×1280） | Ubuntu 22.04 编译机（交叉编译器只有 x86_64 版） |
| HC-SR04 超声波（ECHO **必须 1k/2k 分压**） | Qt 5.15（buildroot 自带） |
| DHT11（DATA **必须 4.7k~10k 上拉到 3.3V**） | Python 3.8+（PC 侧工具） |
| 有源蜂鸣器（低电平触发） | adb |
| MQ-135（AO 接 SARADC_IN3） | USB-TTL 串口线（看 M0 日志，**必需**） |
| PEAK PCAN-USB（可选） | |

### 编译 A7 应用

```bash
# 在 VM 上
cd ~/work/Smart_car_rk3506
mkdir -p build_rk3506_ota && cd build_rk3506_ota

SDK=~/atk_dlrk3506_linux6.1_release_v1.3.1_20260326
HOST=$SDK/buildroot/output/alientek_rk3506/host/bin

$HOST/qmake .. \
    QMAKE_CC=$HOST/arm-buildroot-linux-gnueabihf-gcc \
    QMAKE_CXX=$HOST/arm-buildroot-linux-gnueabihf-g++ \
    QMAKE_LINK=$HOST/arm-buildroot-linux-gnueabihf-g++

make -j8
# 产物：Smart_in-car_entertainment（ELF 32-bit ARM）
```

> ⚠ **必须显式传三个编译器变量**。SDK 的 qmake 默认可能指向 x86 `gcc`，不传会编出 x86 二进制，推到板子上报 `Exec format error`。

### 编译 M0 固件

```bash
cd ~/work/Smart_car_rk3506/amp/rtthread
./build_m0_rtt.sh
# 产物：out/amp.img（FIT，31,744 B）
```

脚本会打印内存账并**断言不超 32 KiB**，最后校验 payload 真的嵌在 FIT 里。

### 部署

```powershell
# 应用
adb push Smart_in-car_entertainment /userdata/smart_car/
adb shell "chmod +x /userdata/smart_car/Smart_in-car_entertainment"

# 内核模块
adb push sc_sensors.ko /userdata/smart_car/

# 板端脚本（一次装好）
adb push board/S98smartcar          /etc/init.d/
adb push board/S99appinit           /etc/init.d/
adb push board/smartcar-ota         /usr/bin/
adb push board/wifi-ctl board/bt-ctl /usr/bin/
adb push board/smartcar.conf        /userdata/smart_car/

# 重启应用
adb shell "/etc/init.d/S98smartcar restart"
adb shell "tail -f /var/log/smartcar.log"
```

### 烧录 M0 固件到 mtd6

```powershell
python vmctl\flash_mtd6_rtthread.py
```

**6 步硬校验**，任一步不符即中止：本地 md5 → 备份 mtd6 → 校验备份 → 推送+板上 md5 → `flash_erase`/`nandwrite` → 回读校验。

> ⚠ **烧 mtd6 是危险操作**。烧坏了 M0 起不来，A7 还能跑（`S98smartcar` 会降级到 `smartcar_hw.ko`），但超声波/蜂鸣器/转向灯全废。第一次烧之前必须确认备份有效。

### 起 OTA 服务

```powershell
.\Linux车载系统\tools\ota_server.ps1              # 启动（端口 18080）
.\Linux车载系统\tools\ota_server.ps1 -Stop        # 停止
```

四步：tar 发布目录到 VM → `setsid python3 -m http.server 18080` → `adb reverse tcp:18080` → 板端 `wget` 自测。

> 板子 eth0 在 `192.168.137.0/24`，VM 在 `192.168.88.0/24`，**两段不通**。用 `adb reverse` 而不是配路由。

### 发一个版本

```powershell
python Linux车载系统\tools\ota_publish.py publish `
  --version 1.7.0 `
  --binary Linux车载系统\build_out\Smart_in-car_entertainment `
  --notes "新增 xxx" `
  --bump
```

板端：

```bash
smartcar-ota status      # 看版本
smartcar-ota check       # 只比对不装
smartcar-ota update      # 查→下→校验→装→重启→自证（失败自动回滚）
smartcar-ota rollback    # 手动回滚
smartcar-ota verify      # 校验运行进程 == 磁盘文件 == 版本串
```

> **发版前必须改 `Smart_car/ota_version.h` 里的版本号并重编。** 版本号来自 ELF，不是发布命令的参数。`ota_publish.py` 检测到不一致会警告。

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

## 文档

| 文档 | 内容 |
|---|---|
| [开发教程（从 0 到 1）](开发文档/01_开发教程_从0到1.md) | 从点亮第一个窗口到烧录 M0 固件，每步可复刻，含开发难点逐条拆解 |
| [面试手册](开发文档/02_面试手册.md) | 25 个高频问题，每个"30 秒版 + 追问版"，附 20 个必记数字 |
| [流程图与时序图](开发文档/03_流程图与时序图.md) | 15 组 Mermaid 图：架构、启动、rpmsg、CAN、蓝牙、OTA、构建 |
| [常见问题与解决](开发文档/04_项目固有常见问题与解决.md) | 45 条项目固有问题，每条含现象/根因/判据/解决/预防 |
| [文件清单](文件清单_按开发顺序_20261020.md) | 582 个文件的绝对路径、作用、调用方，按人工开发顺序排列 |
| [项目结构](README_项目结构.md) | 目录树、改代码该去哪、产物在哪 |

---

## License

MIT。第三方组件（Rockchip HAL、NXP rpmsg-lite、Qt 例程衍生文件）遵循其原始许可证，详见各文件 SPDX 头。

---

## 致谢

- 正点原子 ATK-DLRK3506B 官方 SDK 提供的 HAL 与 rpmsg 基础代码
- NXP RPMsg-Lite
- Qt 官方 Media Player 示例（`playercontrols` / `playlistmodel` / `videowidget` 的来源）

---

