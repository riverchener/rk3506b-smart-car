# RK3506B 车机 CAN 调试台（纯网页前端 + 本地桥接）

在电脑浏览器里**收**和**发** PEAK PCAN-USB 上的 CAN 帧，用来调试 RK3506B 车机：

* 车机「车辆状态」页 → 电脑发 `0x100 / 0x101 / 0x102 / 0x103 / 0x104 / 0x105`（车况、里程、油耗、胎压、告警）
* 车机「车内控制」页 → 电脑收 `0x201 / 0x202 / 0x203 / 0x204`（空调、车窗、灯光、工作模式）

**为什么要有桥接进程**：浏览器不能直接打开 PCAN，所以由 `can_bridge.py` 持有分析仪，
用一个极简的本地 HTTP 服务把「收发帧」暴露给网页。前端是**纯静态** HTML/CSS/JS
（不上框架、不打包、**不引用任何外网资源**，板子/电脑没外网也能用）。

```
┌──────────────┐   HTTP 127.0.0.1:8765   ┌───────────────┐   PCAN-USB   ┌──────────┐
│  浏览器网页   │ ◄─────────────────────► │ can_bridge.py │ ◄──────────► │  RK3506B │
│ index.html   │  /api/frames /api/send  │  python-can   │  500 kbit/s  │  can0    │
└──────────────┘                         └───────────────┘              └──────────┘
```

---

## 1. 装什么

```bat
:: 想真的连 PCAN-USB：装 python-can 的 pcan 后端（会带上 PCAN-Basic 的 Python 封装）
pip install "python-can[pcan]"

:: 顺带装 PEAK 官方驱动（Windows），装完设备管理器里能看到 PCAN-USB
::   https://www.peak-system.com/  → PCAN-USB 驱动
```

* **前端和桥接的 HTTP 服务本身零依赖**：只用 Python 标准库（`http.server` 等）。
  只有真的要开 PCAN 硬件时才需要 `python-can`。
* 没有硬件？用 `--sim`，不需要 `python-can`，`python` 装好就能跑（见第 3 节）。

## 2. 怎么跑（有 PCAN-USB）

```bat
cd Linux车载系统\tools\can_web

:: ① 起桥接进程（默认通道 PCAN_USBBUS1、500 kbit/s、端口 8765）
python can_bridge.py --channel PCAN_USBBUS1 --bitrate 500000 --port 8765

:: ② 浏览器打开（能看到 "已连接桥接进程" 绿灯就对了）
start http://127.0.0.1:8765/
```

也可以在资源管理器里双击 `start_can_web.bat`（sim 演示）/ `start_can_web_real.bat`（接硬件）。

> **`python-can` 的 pcan 打不开时**（报 `module 'can.interfaces' has no attribute 'pcan'`）：
> 走 **`start_can_web_pcanbasic.bat`** 或见第 9 节的「PCANBasic 专用桥接」一节 ——
> 那是一个**独立**的桥接 `pcanbasic_bridge.py`，直接调 PEAK 驱动自带的 `PCANBasic.py`，
> 完全不经过 python-can，前端页面还是同一份。

启动时会**试发一帧 `0x100`**（车速无效）做自检，并把结果直接打在控制台：

```
[自检] 试发 0x100 成功 → 这条总线可以写（TX 成功计数已 +1）
[自检] 试发 0x100 失败：总线发送失败 0x100 [FF 00 …]：OSError: …
       ↑ 这就是「网页能收不能发」的典型原因：驱动/通道/被占用/总线无 ACK/权限
```

命令行参数：

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `--channel` | `PCAN_USBBUS1` | pcan 通道名（`PCAN_USBBUS1`…`PCAN_USBBUS8`），会转成 python-can 的常量 |
| `--bitrate` | `500000` | 波特率。**必须和板子一致（500 k）** |
| `--port` | `8765` | HTTP 端口 |
| `--host` | `127.0.0.1` | 监听地址（默认只本机可访问） |
| `--sim` | 关 | 用内置假帧，不碰 PCAN 硬件（**排障时要确认没加这个**） |
| `--no-selftest` | — | 跳过启动时试发一帧的自检 |
| `--json-status` | — | 起好服务后往 stdout 打一份 `/api/status` 的 JSON（给脚本判断参数用） |
| `--no-sim-echo` | — | sim 下**不**回显自己发出的帧 |
| `--no-sim-periodic` | — | sim 下**不**自动发 0x201~0x204 上行帧 |

## 3. 没有硬件：`--sim` 怎么用

```bat
python can_bridge.py --sim
:: 浏览器开 http://127.0.0.1:8765/
```

`--sim` 做两件事，让整套前端（含所有按钮、解码、计数、CSV）都能演示和自测：

1. **回显**：网页发出去的每一帧，1~4 ms 后作为 `rx` 出现在接收区
   （等于"自己发、自己收"，用来确认编帧/链路/前端闭环）。
2. **假上行**：每 0.6 s 自动发一条板子上行帧（`0x201`→`0x202`→`0x203`→`0x204` 轮着来），
   数值每次略有变化（风量 3/4/5、窗位、目标百分比…），所以接收区一直有东西在滚。

不需要 `python-can`，也不需要 PCAN 驱动。

## 4. 前端长什么样、怎么点

**顶栏**：连接状态（每 250 ms 轮询 `/api/status`）、通道、波特率、后端（`sim 假帧（不碰硬件）` /
`python-can/pcan`）、**RX**、**TX 成功**、**TX 失败**、**待发**（后台定时器里还没发完的帧数）、
表中行数（上限 300）、**清空**。点一下发送，**TX 成功**就该 +1；如果 **TX 失败** 涨了，
顶栏右侧会出现红色错误原文，日志区也有完整的那一条。

**接收区**（左）：
* 滚动表格：`时间 / 方向(RX← TX→) / ID / DLC / 数据(8 字节 HEX) / 解码文字`
* 最多保留 **300 行**（勾「只显示最近 200」再降到 200）；`自动滚动` 可关
* **暂停/继续**：暂停时**完全不碰 DOM**（帧进队列、仍计入导出），避免几千行把浏览器拖死
* 渲染是增量的：新行先攒起来，在**一个 `requestAnimationFrame` 里用 `DocumentFragment` 批量插入**，
  一次重排搞定（不会一行一次 layout）
* **导出 CSV**（UTF-8 BOM，Excel 直接开）
* 过滤：`只看下行 100~105` / `只看上行 201~204`

**发送区**（右）：
* **一键预设**（见第 6 节）。**所有"发一次"的按钮默认只发 1 帧**，不会点一下就往总线灌一串。
* **静态车况**：车速（含"无效(0xFF)"勾选）/档位/转速/左转/右转/刹车/倒车/安全带未系/手刹/双闪，
  车门舱盖 6 位 + 全车已锁，灯光 6 位 + AC/内循环/空调温度/车外温度 + **周期**；
  下面一行实时显示这组帧的**字节与解码**（所见即所发）。「▶ 静态车况（周期发）」才按周期持续发。
* **里程/油耗/胎压组**：可编辑总里程、小计、油量、续航、瞬时/平均油耗、模式、四轮胎压胎温。
  「发 里程油耗组（各 1 次）」= 每条一次；「▶ 里程油耗组（循环）」= 按上面的周期持续发。
  默认值：瞬时油耗 **6.5 L/100km**、平均 **7.4 L/100km**、四轮胎温 **35℃**（帧里传 75）。
* **发送表**（可编辑多行）：每行 `ID(hex) / B0..B7 / 周期 ms / 次数 / 使能 / 发 ▶ ■ ✕ / 解码`
  * **「次数」默认 1**；`发`＝按次数发一次（**单次最多 10 帧**，填 99 也会被夹到 10）
  * `▶`＝按周期一直发（次数填 0 表示不限次数），`■`＝停这一行
  * `+ 加一行`、`← 用静态车况填表`、`▶ 启动全部已使能`、`■ 停止全部`
* 底部**日志区**：每次发送都会打印
  * `→ TX 请求 #N：0x100 [FF 08 03 00 …] ×3`（**请求体**，发之前就记）
  * `✓ TX #N HTTP 200 已登记 1 条（共 3 次发送）· 发送前 TX 成功=12`（**HTTP 状态码 + 响应要点**）
  * `✓ TX #N 总线确认：TX 成功 +3（累计 15）` ← 这句才是"真的写进总线了"的铁证
  * 失败时：`✗ TX #N HTTP 400 被拒：…` 或 `✗ TX #N 总线写入报错：总线发送失败 0x100 [..]：CanError: …`
  * 桥接侧捕获到的错误也会被拉到日志区（`⚠ 桥接：…`），不是只藏在状态里

**ID 写法**：十进制 `256`、十六进制 `0x100`、或含 A~F 的 `100`(→0x100) 都接受。

**布局**：数值输入框统一最小 72px、`box-sizing: border-box`、**数字右对齐**、
`font-variant-numeric: tabular-nums`；标签与控件用 grid 的 `column-gap` 排，不用绝对定位/负 margin，
所以不会重叠。窗口 ≤1200px 时接收区/发送区自动**上下叠放**；发送表太宽时只在它自己的容器里横向滚动，
卡片不会被撑破。

## 5. HTTP API（前端就用这几个）

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/` | `index.html`（同目录静态文件；`../` 穿越被拒绝） |
| GET | `/api/frames` | 自上次轮询以来的**新帧**：`{"frames":[{"t":毫秒,"id":256,"data":[48,1,3,0,0,0,0,0],"dir":"rx"},…],"errors":[{"t":…,"msg":"发送失败…"}],"status":{…}}` |
| POST | `/api/send` | 见下 |
| GET | `/api/status` | `{"connected":true,"channel":"PCAN_USBBUS1","bitrate":500000,"backend":"python-can/pcan","businfo":"interface=pcan · python-can 4.3.1 · 通道 PCAN_USBBUS1 @ 500000","rx":12,"tx":34,"tx_ok":34,"tx_fail":0,"tx_queued":0,"timers":0,"timeline":false,"error":"","version":"1.0.0"}` |
| POST | `/api/clear` | 清空帧缓存、RX/TX 计数与错误日志 |
| POST | `/api/stop` | `{}`＝全部停发；`{"token":"t1"}`＝停某一组周期发送 |
| GET | `/api/presets` | 一键预设的样例帧组（JSON） |

`POST /api/send` 的几种写法：

```jsonc
// 1) 单帧（题目要求的格式）：count / period_ms 可选，默认 1 次
{"id": 256, "data": [48,1,3,0,0,0,0,0], "count": 3, "period_ms": 50}
{"id": "0x100", "data": [66,1,3,8,0,0,0,0]}          // id 也收 0x 十六进制字符串

// 2) 批量
{"frames": [ {"id":0x100,"data":[…],"periodic":true,"period_ms":100,"count":0}, … ]}

// 3) 周期发送：periodic=true + count=0 表示"一直发"（等价 POST /api/timer）
// 4) 开车时间线开关
{"timeline": true, "period_ms": 100}      // 开
{"timeline": false}                       // 关
// 5) 直接点名一个内置预设
{"preset": "mileage"}
```

校验（都会返回 `400` + `{"ok":false,"error":"…"}`）：
ID 必须 ≤ `0x7FF`；`data` ≤ 8 字节且每字节 0~255；`count` 1~**10**（周期发送可为 0 = 一直发）；
`period_ms` ≥ 0；body 必须是合法 JSON。**单帧请求参数错就直接 400**；批量 `frames` 里逐条校验，
合法的照发、非法的放在响应的 `rejected` 里并在日志区显示。

`data` 不足 8 字节会自动补 `00`；`id` 十进制/十六进制都行。

**发送永远不阻塞**：`/api/send` 只把帧"登记"到后台定时器（`_loop` 线程按周期真正写总线），
HTTP 立即返回，响应里带 `tx_before`（发送前的 TX 成功数）和 `status` 快照，
前端 600 ms 后再回读一次 `/api/status` 比对增量，确认帧真的进了总线。

## 6. 一键预设

| 按钮 | 行为 |
| --- | --- |
| **▶ 开车时间线（30 s 循环）** | 怠速 5 s → 加速到 66（3 s 斜坡）→ 左转 66（3 s）→ 刹车 66→40（3 s）→ 安全带未系+右转 20（6 s）→ 左前门开+未锁（3 s）→ 倒车+后备箱（3 s）→ 停车手刹+双闪（4 s）；**每 100 ms 同时发 `0x100/0x101/0x102`**，一轮正好 30 s，不停循环；再点一次停止。 |
| **▶ 静态车况（周期发）** | 把「静态车况」那一堆勾选项编成 `0x100/0x101/0x102` 三帧，按你设的周期一直发。 |
| **发 里程油耗组（各 1 次）** | `0x103` + `0x104` + `0x105` **各发 1 次**（点一下不会持续灌总线）。 |
| **▶ 里程油耗组（循环）** | 同三条，按上面的周期（默认 500 ms）持续发，让板子状态页一直有数。 |
| **发 故障演示（各 1 次）** | `0x104`（发动机故障 bit0 + 胎压报警 bit1 + 故障码 P0301）+ `0x105`（左前胎压偏低）各 1 次。 |
| **▶ 故障循环** | 上两条按 500 ms 循环。 |
| **工作模式：经济/标准/运动/雪地** | 发 `0x204`（B0=0/1/2/3）**自动重发 3 次**（间隔 50 ms，协议要求）。 |
| **■ 全部停发** | 停掉所有周期定时器与时间线。 |

预设的样例字节（和前端"一键预设"一致，也可以直接 `POST /api/send {"preset":"…"}`）：

| 预设 | 帧 |
| --- | --- |
| `mileage` | `0x103 = 10 27 00 50 00 32 58 02`（总里程 1000.0 km / 小计 8.0 km / 油量 50% / 续航 600 km）<br>`0x104 = 4B 41 01 00 00 00 00 00`（7.5 / 6.5 L/100km、标准模式、无告警）<br>`0x105 = 50 50 4F 51 3C 3D 3C 3E`（400/400/395/405 kPa，20/21/20/22℃） |
| `fault` | `0x104 = 62 55 01 03 01 01 03 00`（9.8 / 8.5 L/100km、标准、告警=发动机+胎压、1 个故障码 P0301）<br>`0x105 = 30 2C 50 50 3C 3D 3C 3E`（左前 240 kPa 偏低） |
| `mode_eco` / `mode_normal` / `mode_sport` / `mode_snow` | `0x204` B0 = `00` / `01` / `02` / `03`，重发 3 次 |

## 7. 协议（严格按《CAN 接口定义》实现，逐字节）

下行（电脑 → 板子，全部标准帧、DLC=8）：

| ID | 字节 | 含义 |
| --- | --- | --- |
| `0x100` | B0 | 车速 km/h（`0xFF` = 无效） |
| | B1 | bit0 左转 bit1 右转 bit2 刹车 bit3 倒车 bit4 安全带未系 bit5 手刹 bit6 双闪 |
| | B2 | 档位 `00`P / `01`R / `02`N / `03`D |
| | B3 | 转速 ×100 rpm |
| `0x101` | B0 | bit0..bit5 = 左前门/右前门/左后门/右后门/后备箱/引擎盖 **开** |
| | B1 | bit0 全车已锁 |
| `0x102` | B0 | bit0 近光 bit1 远光 bit2 示宽 bit3 前雾 bit4 后雾 bit5 氛围灯 |
| | B1 | bit0 AC bit1 内循环 |
| | B2 | 空调温度 16~30 ℃ |
| | B3 | 车外温度 = **B3 − 40**（`0x3C`=60 → 20℃） |
| `0x103` | B0..B2 | 总里程 **24 位小端**，LSB 0.1 km（`0xFFFFFF` = 无效） |
| | B3..B4 | 小计里程 **16 位小端**，0.1 km |
| | B5 | 油量 %（`0` / `0xFF` = 无效） |
| | B6..B7 | 续航 **16 位小端** km |
| `0x104` | B0 | 瞬时油耗 0.1 L/100km |
| | B1 | 平均油耗 0.1 L/100km |
| | B2 | 模式 `0`经济 `1`标准 `2`运动 `3`雪地 |
| | B3 | 告警 bit0 发动机 bit1 胎压 bit2 油量低 bit3 保养 bit4 ABS bit5 蓄电池 bit6 制动 bit7 其它 |
| | B4 | 故障码个数 |
| | B5..B6 | 首个故障码 16 位小端（`0x0301` → **P0301**） |
| `0x105` | B0..B3 | 左前/右前/左后/右后 **胎压 ×5 kPa**（`0` / `0xFF` = 无效） |
| | B4..B7 | 同顺序 **胎温 = ℃ + 40**（`0xFF` = 无效） |

> **故障码口径**：按板子侧 `docs/车辆状态与模式_20261001.md` §2.2 —— 帧里传的是 16 位码值，
> **高字节是 P 码的低 8 位**，界面显示就是「把 4 位十六进制码值前面加个 `P`」：
> `0x0301` → `P0301`。这是本项目自定义口径，**不是**标准 OBD 的 P/C/B/U 首位编码
> （标准算法只在 `0x0301` 这种"高两位为 0"的码值上恰好同结果）。前端按板子口径实现。

上行（板子 → 电脑）：

| ID | 字节 | 含义 |
| --- | --- | --- |
| `0x201` | B0 | `00` 关 / `01` 开；B1 温度 16~30；B2 风量 0~7；B3 bit0 内循环 |
| `0x202` | B0 | 窗位 `0`全部 `1`左前 `2`右前 `3`左后 `4`右后；B1 `0`停 `1`升 `2`降；B2 目标 % |
| `0x203` | B0 | 要打开的灯位掩码（位定义同 `0x102` B0）；B1 要关闭的灯位掩码 |
| `0x204` | B0 | 目标工作模式 `0`经济 `1`标准 `2`运动 `3`雪地 |

帧格式：**标准帧（11 位 ID）、数据帧、DLC = 8**。

## 8. 我是怎么自测的

一共五层，全部**不接硬件**就能跑。下面每一条都实际跑过、结果贴在这里。

> ⚠️ **说清楚一个前提**：第 8.2 层用假 `can` 模块把 `PcanBus`（真后端分支）**代码路径**跑通了，
> 第 8.5 层用假 `PCANBasic` 模块把 PCANBasic 后端的**代码路径**跑通了，
> 但**没有任何一层是在真实 PCAN-USB + 真板子上跑过的** —— 开发这台机器上没有分析仪。
> "总线电气层能不能通"必须你在真机上验（见第 9 节排查顺序）。

### 8.1 前端解码/编帧自检（浏览器里 0 依赖，打开页面自动跑一次）

`app.js::selfTest()` 在页面加载时把协议表里的样例逐条解码，并把结果打进**日志区**：
解码 27 条 + 编帧 10 条，全部 `OK` 才显示「自检结束：全部通过」。
任何一条不符会打印 `FAIL 期望… 实得…`（红字）。
其中 3 条直接取自板子侧文档 `docs/车辆状态与模式_20261001.md` §5 的原始用例
（`0x103 C0 37 12 7B 00 44 20 02`、`0x104 41 4A 02 03 01 01 03 00`、`0x105 2E 2F 2B 2E 4B 4C 4A 4B`），
用来保证电脑侧解码与板子侧解析**逐字节互认**（已核对 `canmgr.cpp` 的 `parseFrame()`）。

```bat
node frontend_selftest.js
```

**实测：通过（退出码 0）**，节选：

```
OK   0x100 42 01 03 08 00 00 00 00 → 车速 66 km/h · 档位 D · 转速 800 rpm · 左转
OK   0x103 10 27 00 50 00 32 58 02 → 总里程 1000.0 km · 小计 8.0 km · 油量 50% · 续航 600 km
OK   0x104 62 55 01 03 01 01 03 00 → 瞬时油耗 9.8 L/100km · 平均 8.5 L/100km · 模式 标准 · 告警[发动机/胎压] · 故障 1 个 · 故障码 P0301
OK   0x105 50 50 4F 51 3C 3D 3C 3E → 胎压 左前 400 kPa / 右前 400 kPa / 左后 395 kPa / 右后 405 kPa · 胎温 左前 20℃ / 右前 21℃ / 左后 20℃ / 右后 22℃
==> 前端自检 通过（退出码 0）
```

### 8.2 真后端分支（PcanBus，用假 `can` 模块，**不需要 PCAN 硬件**）

```bat
python backend_selftest.py
```

`--sim` 走 `SimBus`，**永远不会执行 `PcanBus`**，而"能收不能发"恰恰只可能出在这一支上，
所以单独用自注入的假 `can` 模块把它跑起来（`sys.modules["can"] = fake`）：

**实测：通过 34 项，失败 0 项。** 覆盖：

* 构造：python-can **4（`interface=`）与 3（`bustype=`）两条路都走通**（哪个版本的参数名都能用，不会因为参数名不对直接崩）
* 通道名 `"PCAN_USBBUS1"` → 转成 pcan 后端的常量 `0x51`；`bitrate=500000` 确实传下去
* `send()` 确实调用了 `bus.send`，且报文是 `arbitration_id=0x100 / is_extended_id=False / data 8 字节逐字节一致`
* `send()` 抛异常时**原样上抛**，异常文本里带 **ID + 数据 + 原始异常类型与原文**（`0x102`、`01 02 03 …`、`OSError: BusOff`）
* `Controller`：成功帧计 `tx_ok`、失败帧计 `tx_fail` 且**不计入 tx_ok**；失败原文进 `status.error` 与错误日志；
  错误被 `take_errors()` 取走后 `status.error` 仍留着（顶栏一直红字）；恢复后能继续发
* **突发上限**：`queue_send(count=11)` 直接抛错；`count=10` 全部发出；不传 count 只发 1 帧
* **非阻塞**：登记 `count=10`（周期 200 ms，同步发要 2 秒）耗时 **< 50 ms**，登记后 `tx_queued=10`

（这层里还揪出一个真 bug：`PcanBus.send` 的错误格式化串占位符数量不匹配，会抛
`ValueError: not enough arguments for format string` 把真正的 `CanError` 盖掉 —— 已修。）

### 8.3 桥接进程 + 全部 API + 预设（`--sim`，标准库 http 客户端）

```bat
python bridge_selftest.py
```

**实测：通过 75 项，失败 0 项。** 覆盖：

* 静态文件、`/api/status` 字段（含新增 `tx_ok/tx_fail/tx_queued/businfo`）、路径穿越被拒、
  **三个前端文件里都没有 `http://`/`https://`/`cdn.`**
* `--sim` 自动上行 `0x201/0x202/0x203/0x204`；帧结构 `{t,id,data,dir}` 与 8 字节 data；
  `/api/frames` 带 `errors` 数组
* **默认次数 = 1**：不传 `count` 时 `repeats=1`，接收区只出现 **1 条** tx 记录（不是 100）
* **非阻塞**：`count=10 / period_ms=50`（要 450 ms 才发完）HTTP 登记耗时 **实测 1~11 ms**；
  登记后立刻查 `tx_queued > 0`，随后 10 帧全部发出、`tx_queued` 归零、TX 成功 +10
* **突发上限**：`count=11` 被 400 拒绝
* `POST /api/send`：十进制 `256`、`0x101`、纯数字 `"103"`、含字母 `"0x104"`、data 自动补 0
* 参数校验 7 项（ID 超 11 位 / data 超 8 字节 / 字节 >255 / 缺 id / count=0 且非周期 / count 负数 / 坏 JSON）都被 400 拒绝
* **时间线**：同时发 3 个 ID、100 ms 节拍、第一段是怠速（B0=00、档位 D）、加速段出现 66 km/h、可停、停后不再自动发
* **静态车况**：三帧各按 100 ms 周期跑（1.6 s ≥ 8 条）、字节 = `42 01 03 08…`、`status.timers == 3`
* **工作模式**：4 个模式各 `0x204` ×3，B0 依次 0/1/2/3
* **里程油耗组 / 故障演示**：字节与上表一致（`0x104` B3=0x03、B4=1、B5..B6=`01 03`）
* 计数：发一帧 TX 成功 +1；`/api/clear` 后计数归零、帧列表清空
* `POST /api/timer`（count=0 一直发 / 可单条停）

### 8.4 真浏览器点一遍所有按钮（Chrome/Edge headless + CDP，仍是纯标准库）

```bat
python e2e_browser.py --width 1280 --shot shot.png
```

它用 Chrome 的 `--headless --remote-debugging-port` 打开页面，通过 CDP 的 `Runtime.evaluate`
**真的去 `click()` 每一个按钮**，再读 DOM 文本/几何信息做断言。

**实测：1280 宽通过 68 项、1600 宽通过 68 项，都 0 失败。**

布局回归（问题 1，1280 宽下量真实 DOM）：

| 检查 | 结果 |
| --- | --- |
| 卡片里 17 个数值输入框宽度 | 全部 ≥72px、`box-sizing: border-box`、`text-align: right` |
| 空着看不出数值的输入框 | 无（胎温默认 35℃×4、瞬时油耗 65=6.5、平均 74=7.4） |
| 标签 vs 相邻控件几何重叠 | 无（逐个比对 `label.right ≤ input.left`） |
| 发送区各卡片横向溢出 | 无（发送表在自己的 `.table-wrap` 里横向滚） |
| 顶栏 TX 成功 / TX 失败 / 待发 | 三个计数都在 |

功能回归（`--sim` 下点过的按钮与实际所见）：

| 点的东西 | 看到什么 |
| --- | --- |
| 页面加载 | 顶栏「已连接桥接进程 / PCAN_USBBUS1 / 500 kbit/s / sim 假帧（不碰硬件）」；日志「解码自检（27 条）…全部通过」；接收区滚出 `0x201 空调 开 · 温度 24℃ · 风量 3 · 内循环`、`0x203 灯光 开[近光] 关[远光]` |
| **开车时间线** | `0x100/0x101/0x102` 同时出现，每条都有 `TX →` 和 `RX ←`（sim 回显）；首条 `00 … 03 …` 解码「车速 0 km/h · 档位 D」；再点即停 |
| **静态车况** | 勾一堆位后预览 `0x100 42 7D 03 08` / `0x101 11 00` / `0x102 03 03 1A 3A`，点按钮后按 100 ms 滚动，字节与预览一致 |
| **工作模式 ×4** | 各 3 条 `0x204`，解码「工作模式 → 经济/标准/运动/雪地」 |
| **发 里程油耗组（各 1 次）** | 三条各 **1 条 TX**（不是循环灌帧）；`0x103`「总里程 1000.0 km…」、`0x104`「瞬时 6.5 · 平均 7.4 · 标准 · 告警[-]」、`0x105`「400/400/395/405 kPa · 胎温 35℃×4」 |
| **▶ 里程油耗组（循环）** | 1.6 s 内 `0x103` 出现 ≥2 条 |
| **发 故障演示（各 1 次）** | 日志里只有 1 条 `0x104 [62 55 01 03 01 01 03 00]` 请求；解码含「告警[发动机/胎压] · 故障 1 个 · 故障码 P0301」 |
| **日志区** | 出现 `HTTP 200 已登记` 与 `总线确认：TX 成功 +N`（问题 3 的排查手段） |
| **暂停/继续** | 暂停 1.2 s 内**行数不变**，且队列里攒着帧（`rx.queue.length > 0`）；继续后补上 |
| **导出 CSV** | 生成了 Blob；表 ≤300 行；勾「只显示最近 200」后 ≤200 行 |
| **全部停发 + 清空** | 清空后 1 s 内不再有帧进来 |
| **发送表** | 默认「次数」= 1；填 `0x204 / B0=03 / 次数=2` 点「发」→ 2 条「工作模式 → 雪地」；填 99 只发 10 帧；顶栏 TX 成功 > 0、TX 失败 = 0 |

跑完这些之后，`_chrome_profile*/`、`__pycache__/`、`*.png` 之类的临时文件已删掉。

### 8.5 PCANBasic 专用桥接（独立进程，用假 `PCANBasic` 模块，**不需要 PCAN 硬件**）

```bat
python pcanbasic_selftest.py
```

**实测：通过 68 项，失败 0 项。** 覆盖：

* **常量映射**：`"PCAN_USBBUS1"` → `Initialize(0x51, PCAN_BAUD_500K)`；数字通道 `0x52` 原样传；
  `"0x53"` 也认；`250000`/`1000000` → 对应 `PCAN_BAUD_250K`/`PCAN_BAUD_1M`；
  不支持的波特率、未知通道名都给**清晰报错**
* **`Write` 报文结构**：`ID=0x100`、`LEN=8`、`MSGTYPE=PCAN_MESSAGE_STANDARD(0)`、`DATA` 逐字节一致
* **`Read` 语义**：返回 `PCAN_ERROR_QRCVEMPTY` 时**当空轮询**（返回空、不报错、不 busy）；
  喂两帧能一次收全且顺序正确；返回其它错误码时异常文本含 `GetFormattedError` **原文**
* **异常上抛**：`Write` 失败 → 异常里带 ID + 数据原文 + `PCAN_ERROR_BUSOFF: 总线关闭`
* **非阻塞**：登记 10 帧（周期 200 ms，同步发要 1.8 s）实测 **< 1 ms** 返回，`tx_queued=10`；
  随后 10 帧全部发出、`tx_ok=10`、`tx_queued` 归零
* **TX 计数精确**：`Write` 失败 1 次 → `tx_fail=1` 且**不计入** `tx_ok`，原文进 `status.error` 与错误日志
* **API 与 `can_bridge.py` 逐字段一致**：真起两个进程（`pcanbasic_bridge.py` + `can_bridge.py --sim`），
  比对 `/api/status` 的 **14 个字段**（名字、有没有多余字段、类型）完全一致；
  `/api/frames` 结构 `{frames, errors, status}` 一致；`GET /`+`app.js`+`style.css` 复用同一份前端
* **同一批场景两边行为一致**：默认 `repeats=1`、`tx_before` 基线、批量 `periodic` 返回 token 且真的在按周期发、
  `count=11` → 400、`/api/timeline` 开关、`/api/timer`、`/api/stop`、`/api/presets`、`/api/clear` 计数归零
* **读通路**：另起一个实例并在启动前把帧喂进假设备队列 → 桥接真的读出 `0x201/0x203`，
  `dir=rx`、数据逐字节一致、`error` 仍为空（`QRCVEMPTY` 不当错误）

## 9. 真机第一次用的排查顺序（"网页能收不能发"就照这个走）

按顺序来，**每一步都能在页面上看到证据**：

1. **桥接是不是不小心用了 `--sim`？**
   看页面顶栏「后端」：写 **`sim 假帧（不碰硬件）`** 就是假帧模式 —— 这时收发都只在电脑内部转圈，
   板子当然收不到。看桥接窗口的启动横幅，`--sim` 那行下面还有一句红字警告。
   重启：`python can_bridge.py --channel PCAN_USBBUS1 --bitrate 500000`（**不要带 `--sim`**）。
2. **顶栏「后端」是不是 `pcan`？**
   真后端会显示 `python-can/pcan`，鼠标悬停在红色错误区能看到 `businfo`（形如
   `interface=pcan · python-can 4.3.1 · 通道 PCAN_USBBUS1 @ 500000`）。
   如果 python-can 装的是 3.x，桥接会自动改用 `bustype=` 参数（两条路都试过，见 8.2）。
3. **点一下发送，顶栏「TX 成功」有没有涨？**
   * 涨了、但 `TX 失败` 也涨 → 看顶栏红字与日志区的 `总线写入报错：…`（原文就是 python-can 的异常）。
   * 涨了、也没失败，但板子状态页不动 → 总线通了、帧发出去了，问题在板子侧（下一步）。
   * 完全没涨 → 看日志区是不是 `HTTP 400 被拒`（参数被拒，比如一次填了 11 帧以上）或
     `请求失败（桥接进程没响应？）`（桥接挂了/端口变了）。
   * 也可以用命令行旁证：`curl -X POST http://127.0.0.1:8765/api/send -H "Content-Type: application/json" -d "{\"id\":256,\"data\":[66,1,3,8,0,0,0,0]}"`
4. **板子侧 `cat /proc/net/can/stats` 的 RXF 有没有涨？**
   在板子上先 `ip -details link show can0` 确认是 500000、`state ERROR-ACTIVE`（不是 BUS-OFF），
   然后一边在网页上点发送、一边看：
   ```sh
   adb shell "while true; do grep -E 'RXF|TXF|bus-errors' /proc/net/can/stats; sleep 1; done"
   ```
   * **RXF 涨** → 板子收到帧了；状态页还不动就是板子 app/固件没解析这个 ID。
   * **RXF 不涨、PCAN 侧 TX 成功涨** → 线/终端电阻/共地问题（第 9 节下面的常见问题），
     或波特率不一致、CANH/CANL 接反。
   * **TXF/bus-errors 涨、PCAN 侧 TX 失败** → 没有 ACK（对端没上线）或总线错误；PCAN-View 里能看到错误帧。
5. **还不行就把桥接的 stdout 和页面日志区整段留起来**：启动横幅会打印通道/波特率/后端，
   启动时还会**试发一帧 0x100** 并直接告诉你成功还是失败（"这条总线可以写"/"试发失败：…"）。
   想跳过这个试发用 `--no-selftest`。

### 9.1 `python-can` 的 pcan 打不开 → 改走 PCANBasic 专用桥接

**典型报错**（你已经遇到过）：

```
!! 打开 PCAN 失败: module 'can.interfaces' has no attribute 'pcan'
```

这是 python-can 的已知行为：`can/interfaces/pcan` 在 **import 期**就去加载 PEAK 驱动提供的
`PCANBasic.dll`，加载不到（没装 `python-can[pcan]` extra，或 PCANBasic.dll 不在）就把整个子模块吞掉。
**而 PEAK 的驱动本身可能是好的**（PCAN-View 能用），这时不要跟 python-can 较劲，
直接用它自带的那套 Python 封装。

**怎么做**：双击 `start_can_web_pcanbasic.bat`，或者手动：

```bat
cd Linux车载系统\tools\can_web
python pcanbasic_bridge.py --channel PCAN_USBBUS1 --bitrate 500000 --port 8765
start http://127.0.0.1:8765/
```

它是**独立的进程**（`pcanbasic_bridge.py`），**完全不经过 python-can**：
直接 `import PCANBasic`，用 `Initialize / Write / Read / Uninitialize` 收发。
前端还是同一份 `index.html/app.js/style.css`，API 字段与 `can_bridge.py` **逐字段一致**
（`/api/frames`、`/api/send`、`/api/status`、`/api/clear`、`/api/stop`、`/api/timer`、`/api/presets`），
所以两个桥接可以互换，页面顶栏的「后端」会显示 **`PCANBasic（PEAK 驱动）`**。

**判据（先做这三步，一眼知道能不能用）**：

```bat
:: ① 能不能 import 到 PEAK 的封装？成功会打印 PCANBasic OK 81（0x51 = PCAN_USBBUS1）
python -c "import PCANBasic; print('PCANBasic OK', PCANBasic.PCAN_USBBUS1)"

:: ② 底层 DLL 在不在？
dir C:\Windows\System32\PCANBasic.dll

:: ③ 真连一下（会打印启动横幅 + 试发一帧 0x100 的结果）
python pcanbasic_bridge.py --channel PCAN_USBBUS1 --bitrate 500000
```

* **①成功** → 这个桥接就能用，直接开浏览器即可。
* **①失败、②不存在** → **PEAK 驱动其实没装全**（PCAN-View 能用也可能是绿色版 / 另一台机器装的）。
  去装 PEAK 的 **PCAN-Basic** 驱动，装完 ② 那个 DLL 会出现，① 也就成功了。
* **①失败、②存在** → 驱动装了但 Python 封装（`PCANBasic.py`）不在 `sys.path`。
  桥接会**自动**去这些常见目录找（找不到才报错，报错里会把找过的目录全列出来）：
  `%PCANBASIC%`、本目录、`C:\Program Files\PEAK-System\PCAN-Basic\PCAN-Basic API\Samples\Python`、
  `C:\Program Files (x86)\...`（含多层子目录的写法）。也可以自己设：
  ```bat
  set PYTHONPATH=C:\Program Files\PEAK-System\PCAN-Basic\PCAN-Basic API\Samples\Python
  python pcanbasic_bridge.py --channel PCAN_USBBUS1 --bitrate 500000
  ```
* 波特率只内置 `1M/800K/500K/250K/125K/100K/50K/20K/10K`（映射到 `PCAN_BAUD_*`），
  传别的值会直接告诉你"请先用 PCAN-View 设定"。通道名支持 `PCAN_USBBUS1` 这种常量名，也支持 `0x51` 数字。

## 10. 常见问题

* **波特率必须 500 k**：板子侧 `can0` 固定 500000、采样点 87.0%。桥接默认已是 500000；
  PCAN 标准 500k 档是 87.5%，一般能通；**若总线上有错误帧**，在 PCAN-View 里用自定义 Bit Timing 把采样点调到 87.0%。
* **必须共地**：PCAN-USB 和板子（SN65HVD230 收发模块）一定要 GND 相连，CANH↔CANH、CANL↔CANL（H/L 接反了会一直报错或完全收不到，可以对调试一下）。
* **120 Ω 两端各一个**：总线两端各一个终端电阻（板子端 1 个、PCAN 端 1 个）。短台架可以只用一个，但长线/多节点必须两端都有。
* **只有板子一个节点时"发不出去"/全是错误帧**：CAN 需要至少一个节点 ACK。接上 PCAN 就好了，或用 `candump`/`cantool` 在板子上再看一眼。
* **点一下发送，板子没反应**：先看顶栏 TX 成功有没有涨（见第 9 节）。别用「发」去灌连续帧 ——
  要让板子持续收到请点 `▶`（周期发送），或者先把「次数」调大再点「发」（单次最多 10 帧）。
* **发很多帧电脑卡**：现在单次最多 10 帧、接收区最多 300 行、暂停时不碰 DOM、渲染走 rAF 批处理；
  如果还卡，勾上「只显示最近 200」，或点「暂停」。
* **`--sim` 下收不到自己发出的帧**：检查是否加了 `--no-sim-echo`（默认是回显的）。
* **打不开网页 / 顶栏红灯**：确认 `can_bridge.py` 还在跑；端口被占用换 `--port 8800`；只监听本机（`--host 0.0.0.0` 才能局域网访问，注意安全）。
* **`pip install python-can` 后仍报 pcan 打不开**：装 PEAK 官方 PCAN-USB 驱动；设备管理器里应显示 `PCAN-USB`（只装驱动前可能显示 `XCAN-USB`）；确认通道号（`PCAN_USBBUS1` 是第一路）；确认没有别的程序（PCAN-View）正占着设备。
* **报 `module 'can.interfaces' has no attribute 'pcan'`**：见 **9.1** —— 换 `python pcanbasic_bridge.py`
  （或双击 `start_can_web_pcanbasic.bat`）绕开 python-can。先验 `python -c "import PCANBasic"` 和
  `dir C:\Windows\System32\PCANBasic.dll`。
* **接收区一直刷新数据但板子状态页不动**：板子只解析它已实现的 ID；`0x103/0x104/0x105/0x204` 是新增报文，若板子固件还是旧版就看不到效果（电脑侧本身收发是正常的，接收区会显示 TX/RX）。
* **一帧数据不足 8 字节**：桥接会自动补 `00`（`0x100` 只给 4 个字节也能发）。

---

## 11. 文件清单

| 文件 | 作用 |
| --- | --- |
| `can_bridge.py` | **本地桥接进程**：`http.server` 提供 `/`、`/api/frames`、`/api/send`、`/api/status`（外加 `/api/clear`、`/api/stop`、`/api/timer`、`/api/presets`）；持有 PCAN（`python-can` 的 `pcan` 后端，兼容 3.x/4.x 参数名）或 `--sim` 假后端；**只登记不阻塞**，由后台线程按周期真正写总线；TX 成功/失败分开计数、错误原文上报；启动时试发一帧自检。零第三方依赖（真硬件才需要 `python-can`）。 |
| `pcanbasic_bridge.py` | **PCANBasic 专用桥接**（独立进程，见 §9.1）：绕开 python-can，直接调 PEAK 驱动自带的 `PCANBasic.py`（自动搜常见安装目录）；**只 import** `can_bridge.py` 的 `Controller/Handler/make_server` 等纯逻辑，所以 API 与它逐字段一致、前端同一份。`--channel/--bitrate/--port` 参数同名。 |
| `index.html` | 前端页面结构：顶栏（RX/TX 成功/TX 失败/待发）、接收区、发送区（预设 / 静态车况 / 里程油耗组 / 可编辑发送表 / 日志）。 |
| `app.js` | 前端全部逻辑：轮询与增量渲染（rAF + DocumentFragment）、**逐字节解码/编帧**、一键预设、发送表、CSV 导出、暂停/过滤、发送结果核对（HTTP 状态 + TX 增量）、后端标签映射（`sim`/`PCANBasic`/`python-can/pcan`）、页面自检 `selfTest()`。 |
| `style.css` | 深色样式（无任何外网字体/图片，纯 CSS）：输入框最小 72px / border-box / 数字右对齐 / tabular-nums，grid 列间距布局不重叠，窄屏上下叠放。 |
| `start_can_web.bat` | 双击就跑：`--sim` 演示模式 + 自动开浏览器（并提示手动地址）。 |
| `start_can_web_real.bat` | 双击就跑：真实 PCAN（`PCAN_USBBUS1`、500 k，可改）+ 自动开浏览器。 |
| `start_can_web_pcanbasic.bat` | 双击就跑 **PCANBasic 专用桥接**（会先跑 `import PCANBasic` 预检并提示 `dir ...\PCANBasic.dll`）+ 自动开浏览器。 |
| `frontend_selftest.js` | 命令行跑 `app.js` 的解码/编帧自检（`node frontend_selftest.js`）。 |
| `backend_selftest.py` | 用假 `can` 模块跑 **PcanBus 真后端分支** + Controller 发送/失败/上限/非阻塞（`python backend_selftest.py`）。 |
| `pcanbasic_selftest.py` | 用假 `PCANBasic` 模块跑 PCANBasic 后端 + 两个桥接的 API 逐字段对比（`python pcanbasic_selftest.py`）。 |
| `bridge_selftest.py` | 命令行端到端测桥接进程、全部 API 与预设（`python bridge_selftest.py`）。 |
| `e2e_browser.py` | 用真 Chrome/Edge（headless + CDP）把前端所有按钮点一遍并断言，含布局几何检查（`python e2e_browser.py --width 1280`）。 |
