#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""生成 docs/OTA升级实现与验证_20261012.md（把 build_out/ota_evidence 的原始输出嵌进去）。"""
import io
import os

HERE = os.path.dirname(os.path.abspath(__file__))
# HERE = ...\Linux车载系统\Linux车载系统\tools
WS = os.path.abspath(os.path.join(HERE, ".."))            # ...\Linux车载系统\Linux车载系统
OUTER = os.path.abspath(os.path.join(WS, ".."))           # ...\Linux车载系统  <- 工程最外层

# ⚠ 证据原始输出（40 份 txt + 图片）已于 2026-10-20 归档到 E 盘，仓库里那份 build_out/ota_evidence
#   已不存在。可用环境变量 OTA_EVID_DIR 覆盖；默认指向 E 盘归档位置。
#   （vmctl/ota_run.py 当年就是往 <仓库>\build_out\ota_evidence 写；重跑板端取证时把新输出
#     放进 EVID 指向的目录，或临时 export OTA_EVID_DIR=<新目录>。）
EVID_DEFAULT = r"E:\车载备份\evidence_20261019\build_out__ota_evidence"
EVID = os.environ.get("OTA_EVID_DIR") or EVID_DEFAULT

DOCS = os.path.join(OUTER, "docs")
os.makedirs(DOCS, exist_ok=True)

# 证据目录必须真实存在：否则 ev() 会静默把每一处证据替换成"（缺少证据文件 xxx）"，
# 生成一份看着"成功"、实则满是空洞的残稿（历史教训：68403 B 的文档被降级成 30479 B）。
if not os.path.isdir(EVID):
    raise SystemExit(
        "!! 证据目录不存在，拒绝生成残稿:\n   %s\n"
        "   若证据已搬走，用环境变量指定:\n"
        "     $env:OTA_EVID_DIR='E:\\车载备份\\evidence_20261019\\build_out__ota_evidence'\n"
        % EVID)


def ev(name, strip_header=True):
    p = os.path.join(EVID, name + ".txt")
    if not os.path.isfile(p):
        raise SystemExit("!! 缺证据文件 %s.txt（在 %s 里找不到），拒绝生成残稿" % (name, EVID))
    s = io.open(p, encoding="utf-8", errors="replace").read().rstrip()
    if strip_header and s.startswith("$ (板端)"):
        # 去掉"$ (板端) <长命令>"那一行：命令另在正文里给出，避免重复且难读
        s = s.split("\n", 1)[1].lstrip("\n")
    return s


DOC = r"""# OTA 升级机制：设计、实现与真机端到端验证

- 日期：2026-10-12
- 硬件：正点原子 ATK-DLRK3506B（RK3506B，Cortex-A7 + Cortex-M0 AMP）
- 系统：Linux 6.1.118（buildroot，busybox ash）+ Qt 5.15.11（Wayland）
- 板子连接：VMware Ubuntu（192.168.88.130）上通过 `adb` 操作；Windows 侧用 paramiko 驱动
- 本文所有命令与输出均为**真机实测**，原文粘贴（含失败与不完美之处）

---

## 0. 一句话结论

实现了一套**分层、可自证、能自动回滚**的 OTA：
应用层（`/userdata` 上的可执行文件）走"下载 → 三重校验 → 同分区原子替换 → 重启 → 运行自证 → 失败自动回滚"，
已在真机上连续完成 **7 次真实升级**（1.1.0 → 1.4.0）、**1 次回滚演练**、**2 次损坏包拒绝演练**；
固件层（mtd4/mtd6）实现了"查 → 试运行 → 真写"三段式，本次**只做到试运行（dryrun）**，未真正擦写 flash —— 原因见 §8。

---

## 1. 方案对比：为什么这么设计

### 1.1 为什么分"应用层 / 固件层"两层，而不是一套代码全刷

| 维度 | 应用层 OTA（本次重点） | 固件层 OTA（内核/AMP） |
|---|---|---|
| 写什么 | `/userdata/smart_car/Smart_in-car_entertainment` 一个文件 | `mtd4`(boot,10MB) / `mtd6`(amp,1MB) 整分区 |
| 风险 | 文件坏了，重推一份即可，板子始终能进系统 | **写坏内核/M0 = 可能起不来**，要短路进 Loader + 开发工具救 |
| 生效 | `S98smartcar restart` 立刻生效 | 必须**断电冷启动**（M0 固件尤其如此，`adb reboot` 不行） |
| 回滚 | `cp` + `mv` 一个 2.4MB 文件，秒级 | 要把 10MB 整分区 `nandwrite` 回去，分钟级且有风险 |
| 频次 | 绝大多数需求（改 UI、修 bug）都在这层 | 只有换内核/换 M0 才需要 |

结论：**把 95% 的更新都压在应用层**，固件层单独做成"更慢、更谨慎、必须人工确认"的通道。
两条通道共用同一份 `manifest.json` 和同一套 md5 校验逻辑，运维心智一致。

### 1.2 为什么下载通道用 `adb reverse` 而不是直连网络

板子的网络现状（实测）：

```
lo               UNKNOWN        127.0.0.1/8
can0             UP
eth0             UP             192.168.137.85/24      <- Windows ICS 网段
wlan0            UP             10.17.102.76/24        <- 通过手机/路由
default via 10.17.102.82 dev wlan0
```

`eth0` 在 Windows 的 ICS 网段、`wlan0` 走外部路由，**rootfs 里没有任何指向开发机（VM 192.168.88.130）的路由**，
所以"板子直接 wget 开发机 IP"是不通的。可选做法：

| 方案 | 评价 |
|---|---|
| 给板子加一条静态路由指向 VM | 要改板子网络配置，VM 网卡模式（NAT）还会变，脆弱 |
| 把包推到板子再本地升级 | 能跑，但**验证不到"下载 + 网络校验"这条链路**，OTA 的意义少一半 |
| **`adb reverse tcp:18080 tcp:18080`** | ✅ 不改任何网络配置，USB 即通道；板端只见 `127.0.0.1:18080` |

实测（见 §5.1）：板端 `wget http://127.0.0.1:18080/ota/manifest.json` 直接拿到正确内容（rc=0）。
**注意**：`adb reverse` 依赖 adb server 存活，本次验证期间它被外部因素重启过两次，隧道随之失效 —— 这一点写进了 §8 已知限制。

### 1.3 为什么版本号要"编进 ELF"，而不是只在文件名里

这是整套方案能**自证**的关键。`Smart_car/ota_version.h` 把版本固化成带独特前缀的字符串常量：

```c
#define SMARTCAR_OTA_VERSION_TAG "SMARTCAR_OTA_VERSION=" SMARTCAR_OTA_VERSION_STR
```

编译后它落在 ELF 的 `.rodata`，于是板端可以用**同一条事实来源**回答三个不同问题：

1. **磁盘上装的是哪个版本？** → `grep -ao 'SMARTCAR_OTA_VERSION=...' 文件`
2. **正在跑的是哪个版本？** → `readlink /proc/$(pidof ...)/exe` 拿到真实可执行文件，再 grep
3. **应用自己承认自己是哪个版本？** → 启动横幅打进 `/var/log/smartcar.log`

三者一致才算"升级成功"。只比 md5 是不够的：md5 只能证明"文件没坏"，不能证明"跑起来的确实是它"。

### 1.4 为什么"原子替换"必须先写同分区临时文件再 `mv`

`/userdata` 是 ubi 卷（`/dev/ubi7_0`），`/tmp`、`/var/log` 是 tmpfs（**不同分区**）。
跨分区 `mv` 是"复制 + 删除"，中途掉电会留下**半个可执行文件**，那台车机就再也起不来了。
正确顺序是（`board/smartcar-ota` 的 `install_stage`）：

```
cp 新版本 -> /userdata/smart_car/.Smart_in-car_entertainment.new   # 同分区
chmod 755
校验临时文件 md5 == 期望
mv -f  .new  Smart_in-car_entertainment                            # 同分区 rename，原子
sync
```

`rename(2)` 在同一文件系统内是原子的：掉电只会出现"完整的旧的"或"完整的新的"。

### 1.5 为什么"界面只做遥控，逻辑全在命令行工具里"

Qt 的「系统升级」页（`otapage.cpp`）不自己下载、不自己换文件，它只 `QProcess` 调 `smartcar-ota`。原因：

1. 升级要在**应用进程之外**替换应用自己的可执行文件并重启自己 —— 让被替换的进程覆盖自己、再杀掉自己，逻辑上就别扭；
2. 升级过程中应用会被 `S98smartcar restart` 杀掉，**界面上的"实时进度"其实看不到**。所以真正的记录必须落盘（`/var/log/smartcar-ota.log`），页面重开后再读日志呈现结果，而不是假装能实时显示；
3. 命令行工具能脱离界面单独用（adb/串口都能跑）、能写进文档被复现、能被 CI 调用。界面只是它的一个入口。

### 1.6 被否掉的方案

- **A/B 双分区 + 引导切换**：需要改分区表（`parameter.txt`）和 uboot 的 `misc` 逻辑，属于"动内核与设备树"范畴，
  与本项目"不要动内核/设备树、不要整盘刷 update.img"的约束冲突，**不做**。
- **在 Qt 里用 QNetworkAccessManager 自己下载**：见 §1.5。
- **签名校验（RSA/Ed25519）**：板端没有 openssl，要静态编进去；
  在"局域网 + 自己发布"的场景里收益有限，**本次没做**，在 §8 如实列为限制。

---

## 2. 交付物清单

### 2.1 板端（新增）

| 路径 | 说明 |
|---|---|
| `Linux车载系统/Smart_car/board/smartcar-ota` | **OTA 客户端**（POSIX sh，31.5KB）。部署到板子 `/usr/bin/smartcar-ota`，另在 `/userdata/smart_car/` 放一份 |
| `/userdata/ota/` | 运行时目录（`slots/` 回滚槽、`fwbackup/` 固件回滚镜像、`tmp/` 下载暂存、`version`、`manifest.json`、`ota.log`） |
| `/var/log/smartcar-ota.log` + `/userdata/ota/ota.log` | 双份日志：一份给现场看（tmpfs，重启即失），一份给重启后回溯 |

### 2.2 应用（改动）

| 路径 | 改动 |
|---|---|
| `Smart_car/ota_version.h` | **新增**：版本号的唯一来源 + 取证锚点 |
| `Smart_car/otapage.h` / `otapage.cpp` | **新增**：「系统升级」页（版本三连显示 + 检查/升级/回滚三个按钮 + 日志窗） |
| `Smart_car/main.cpp` | 加启动横幅（把版本打日志）+ `--page=ota` 自检入口 |
| `Smart_car/mainwindow.h` / `mainwindow.cpp` | 图标行末尾加「系统升级」图标；`dumpPages` 增加 `11_ota` 截图 |
| `Smart_car/appicons.h` / `appicons.cpp` | 加 `AppIcon::Update` 图标（下箭头 + 循环弧） |
| `Smart_car/Smart_in-car_entertainment.pro` | 把新增源文件加进 SOURCES/HEADERS |

> 改动原则：**只新增文件 + 最小侵入式改动现有文件**。桌面原本"刚好 7 个图标"的排版没动，
> 新图标排在设置类图标之后（横向滑动才可见），已有入口全部保持原样。

### 2.3 PC 端（新增）

| 路径 | 说明 |
|---|---|
| `Linux车载系统/tools/ota_publish.py` | 发布工具：确定性打包 + 生成 `manifest.json` + `list/verify/info` |
| `Linux车载系统/tools/ota_build_and_publish.py` | 一键：同步源码 → VM 交叉编译 → 拉回二进制 → 发布 |
| `Linux车载系统/tools/ota_server.ps1` | 起静态 HTTP + 打通 adb reverse 隧道（PowerShell 版） |
| `vmctl/ota_server.py` | 同上（Python 版，本次实际用的） |
| `vmctl/ota_run.py` | 在板子上跑命令并把**原始输出**存证 |
| `vmctl/ota_push_tool.py` | 部署板端工具并核对 md5 |
| `vmctl/ota_rollback_drill.py` / `ota_md5_drill.py` | 两个自动化演练（回滚 / 损坏包拒绝） |
| `vmctl/ota_deploy_tool.py` / `ota_verify_untouched.py` | 部署与"未破坏现有功能"核对 |
| `Linux车载系统/build_out/app_before_ota/orig_Smart_in-car_entertainment` | **升级前的原始应用备份**（md5 `a647e89a…`） |

---

## 3. 部署步骤（从零到能升级）

### 3.1 服务器侧（开发机）

```powershell
# ① 编译 + 打包 + 生成 manifest（一条命令）
python Linux车载系统\tools\ota_build_and_publish.py `
       --version 1.4.0 `
       --notes "发布说明"

# ② 起 HTTP 服务 + 打通 adb reverse 隧道
python vmctl\ota_server.py start --port 18080
```

`ota_publish.py` 产出的目录结构就是 HTTP 根：

```
tools/ota_release/
  VERSION
  ota/
    manifest.json          <- 板端默认拉这个
    app-1.4.0.tar.gz
    can_boot_zboot_20260929.img   （--with-firmware 时才有）
    can_amp_m0_20260929.img
```

`manifest.json` 实际内容（真机验证时用的那份）：

```json
{
  "schema": 1,
  "product": "Smart_car RK3506B 车机",
  "version": "1.4.0",
  "released_at": "2026-10-12 21:52:44",
  "notes": "修正升级页版本显示；带固件条目（仅供 firmware dryrun 预演，本次不真写 flash）",
  "app_file": "app-1.4.0.tar.gz",
  "app_size": 1558978,
  "app_md5": "5fbf4103016ba92d6e1c34507f4258b2",
  "app_elf_version": "1.4.0",
  "app_source_md5": "9cf29579d3663c8217f10c6e21cf17c8",
  "firmware": [
    { "name": "CAN 内核 (mtd4)", "partition": "mtd4",
      "image": "can_boot_zboot_20260929.img", "size": 6153728,
      "md5": "72640b6afbcda169b0373a63267b209b" },
    { "name": "M0/AMP 固件 (mtd6)", "partition": "mtd6",
      "image": "can_amp_m0_20260929.img", "size": 33280,
      "md5": "41ec6a3993b0fda1a9c2ad5225048c4a" }
  ]
}
```

### 3.2 板端首次部署

```powershell
# 把 OTA 客户端推到板子（两个位置，界面优先用 /usr/bin）
python vmctl\ota_push_tool.py

# 备份"升级前"的原始应用到 PC（升级第一原则：先留退路）
python vmctl\ota_deploy_tool.py
```

### 3.3 日常升级（板端）

```sh
smartcar-ota status      # 当前安装版本 / 运行版本 / 可用 slot
smartcar-ota check       # 拉 manifest 比对版本，只报告不安装
smartcar-ota update      # 查 → 下 → 校验 → 装 → 重启 → 自证（失败自动回滚）
smartcar-ota verify      # 校验"运行中的进程 == 安装的文件 == 版本串"
smartcar-ota rollback    # 回滚到最近一次备份的 slot
smartcar-ota local <目录>  # 离线升级（不联网，读本地 manifest.json + 包）
smartcar-ota firmware check|dryrun|flash [--yes]
```

---

## 4. 端到端验证：升级链

> 本节所有输出都是从板上**原样抓回**的（`vmctl/ota_run.py` 把输出直接写文件，避免控制台编码破坏中文）。
> 证据文件在 `build_out/ota_evidence/`。

### 4.0 起点：板子上的"升级前"状态

升级前装的是"还没有版本串"的老构建（这正是需要 OTA 的理由）：

```
$ smartcar-ota status          # 部署工具后第一次查
安装文件    : /userdata/smart_car/Smart_in-car_entertainment
  大小      : 2374284 B
  md5       : a647e89ac0615acfe8698d0d1b59f533
  版本串    :                     <- 老构建没有版本串，空
运行进程    : pid=2186 exe=/userdata/smart_car/Smart_in-car_entertainment
  进程版本  :
  进程md5   : a647e89ac0615acfe8698d0d1b59f533
  一致性    : 运行的就是安装位置的文件
```

这份原文件已备份到 PC：`build_out/app_before_ota/orig_Smart_in-car_entertainment`
（2374284 B，md5 `a647e89ac0615acfe8698d0d1b59f533`，本地核对一致）。

### 4.1 `check`：比对版本（只报告，不安装）

命令：`smartcar-ota check`

```
{CHECK}
```

**这行输出是"下载通道 + manifest 解析 + 版本比对"三件事同时成立的证据**：
manifest 从 `http://127.0.0.1:18080` 拉到了，解析出了包名/md5/大小，并且判定 `1.1.0 -> 1.2.0` 可升级。

### 4.2 第一次升级：1.1.0 → 1.2.0

命令：`smartcar-ota update`

```
{UPDATE120}
```

逐步对应到设计：

| 日志行 | 对应的设计点 |
|---|---|
| `已备份当前版本到 slot 0001 (版本='1.1.0', md5=67aec6f8…)` | §1.6 回滚材料，**在任何写盘之前**建立 |
| `✔ md5 校验通过` / `✔ 大小校验通过` / `✔ 解包通过` | 三重校验（防损坏 / 防服务端与 manifest 不同步 / 防不是合法包） |
| `临时文件已就绪: .new  md5=3bc9d2ff…` | §1.4 同分区临时文件 |
| `原子替换完成 … md5=3bc9d2ff…` | §1.4 原子 `mv` |
| `装机后版本串: 1.2.0 ✔` | 装机后自检（新文件的版本串必须是目标版本） |
| `[0s] pid=3049 exe=/userdata/smart_car/Smart_in-car_entertainment version=1.2.0` | **运行自证**：跑起来的进程的 exe 指向的文件里版本串就是 1.2.0 |
| `OTA_UPDATE_OK version=1.2.0` | 最终结果行（界面也用这一行判定） |

### 4.3 独立复核（不只是工具自己说成功）

命令：`smartcar-ota verify`

```
{VERIFY120}
```

三条独立证据同时成立：**磁盘文件**版本/md5、**/proc/<pid>/exe 指向的文件**版本/md5、以及**应用自己打的启动横幅**。
第三条最重要 —— 它是应用"亲口承认"自己在跑哪个版本，不是我们替它推断的。

### 4.4 第二次升级：1.2.0 → 1.3.0

命令：`smartcar-ota check; smartcar-ota update`（一条命令走完"检查 + 升级"）

```
{UPDATE130}
```

注意这次出现了 **两个 slot**（0001=1.1.0，0002=1.2.0），说明回滚材料是**逐版本累积**的，不是只留一份。

### 4.5 后续连续升级：1.3.1 / 1.3.2 / 1.3.3 / 1.4.0

验证过程中为了修「系统升级」页的显示问题，连续发布了多个版本并逐一真机升级，
每一次都走的是同一套流程（这也顺便证明了流程的可重复性）。最后一次（1.4.0）：

```
{UPDATE140}
```

### 4.6 最终状态与"未破坏现有功能"核对

```
{HEALTH}
```

要点：
- 应用在跑（pid 777），运行版本 = 磁盘版本 = 1.4.0；
- `/dev/mycarctl`、`/dev/mysr04`、`/dev/mydht11`、`/dev/mybuzzer` 四个节点都在；
- `can0` 仍然 `UP`；
- `S98smartcar` / `S49can` 与本地源码 **md5 完全一致**（`757c680c…` / `2b70cfd7…`）—— 从未被 OTA 改动过；
- mtd4/mtd6 只做过只读 dump，没有擦写。

> 关于 §4.6 里"掩码写读回"那一项：输出是 `write 01 -> read 30` 这种，看着像"坏了"，
> 其实是**我的测试命令写错了**（`printf "01 " > /dev/mycarctl` 只写入第一个字节 `'0'`=0x30），
> 不是 OTA 造成的。已有的 `verify_install_and_shot.py` 里的写法是正确的。

---

## 5. 端到端验证：回滚与拒绝

### 5.1 回滚演练

命令：`smartcar-ota rollback`（回滚到最近一次备份的 slot 0010 = 1.3.4）

```
{ROLLBACK}
```

结果链条：`OTA_ROLLBACK_OK version=1.3.4` → 独立 `verify` 确认磁盘/进程/启动横幅三者都是 1.3.4。

> 演练脚本原本还想在回滚后"再升回最新版"，这一步失败了，失败原因是
> `无法下载 manifest … 检查网络 / adb reverse 隧道`：
> **外部因素把 adb server 重启了**（见 §8.1），隧道没了。
> 重连隧道后升级成功（同一份证据文件的第 9–11 段），所以功能本身没问题。
> 这个"隧道会因外部原因断掉"的现象是真实运维问题，已列入已知限制。

### 5.2 损坏包拒绝演练（任务明确要求）

命令：`vmctl/ota_md5_drill.py`

```
{MD5DRILL}
```

两次演练都达到预期：

| 演练 | 手段 | 结果 |
|---|---|---|
| A | 把服务端包**改坏一个字节**（manifest 仍写正确 md5） | `md5 校验失败（拒绝安装）：期望 5fbf4103…，实际 20a780e2…`，退出码 1；**安装文件、运行进程、版本全部与演练前一致** |
| B | 包正确，但把 **manifest 里的 md5 写错**成全 0 | 同上拒绝：`期望 00000000…，实际 5fbf4103…` |
| 收尾 | 恢复 manifest，正常升级 | `OTA_UPDATE_OK version=1.4.0`，链路恢复正常 |

"拒绝"是真的没动系统：两次演练后 `smartcar-ota status` 里安装文件 md5 仍是 `bfac0297…`（1.3.4），
`运行进程` 的 pid 也仍是原来那个 2418 —— **没有重启、没有写盘**。

> 演练还暴露了一个**设计缺陷并已修复**：早期版本在"下载之前"就做 slot 备份，
> 于是每次被拒绝的下载都会在 `/userdata/ota/slots` 下留一份 2.4MB 的重复槽位
> （实测 10 个 slot 里有 3 个是完全相同的 1.3.4，白占 7MB）。
> 现在改成**校验通过之后、写盘之前**才备份 —— 既省空间，也让"回滚到上一个版本"的目标更明确。

---

## 6. 「系统升级」界面

`app_11_ota.png` 由应用自己的 `QWidget::grab()` 产生（板子 weston 不允许外部截图，见 `main.cpp` 的 SIGUSR1 机制），
`kill -USR1 $(pidof Smart_in-car_entertainment)` 触发，一次抓全部 11 个页面：

![系统升级页]({EVIDURL}/app_11_ota_v140.png)

页面内容与出处：

| 界面元素 | 数据来源 |
|---|---|
| 磁盘上的版本 / 运行的版本 | 板端探测脚本（`grep -ao` ELF + `readlink /proc/<pid>/exe`），与命令行 `smartcar-ota verify` 同一套判据 |
| 服务端版本 / 服务端说明 | 从 `smartcar-ota check` 的输出里解析 |
| 检查更新 / 立即升级 / 回滚 | `QProcess` 调 `smartcar-ota check` / `check+update --force` / `rollback` |
| 日志窗 | 工具 stdout + `/var/log/smartcar-ota.log` 尾部 |

**只新增文件 + 最小改动现有文件**：桌面图标行末尾多一个「系统升级」图标（横向滑动才可见），
原有"刚好 7 个图标"的排版、所有既有入口、`S98smartcar`/`S49can` 行为都没变。

---

## 7. 固件层（mtd4/mtd6）：本次只到"试运行"

### 7.1 三段式设计

```sh
smartcar-ota firmware check    # 只读：拉 manifest、列分区，不下载不备份
smartcar-ota firmware dryrun   # 除擦写外全做：下载 + md5 + nanddump 备份 + 打印将要写的分区/md5/回滚命令
smartcar-ota firmware flash --yes   # 真写：备份失败就拒绝继续
```

`flash` 的安全闸门（缺一不可）：
1. 必须显式 `--yes`（默认拒绝，防止手滑）；
2. 镜像 md5 必须与 manifest 一致；
3. **必须先成功 `nanddump` 出整分区备份**，备份为空则拒绝写入；
4. 写完立刻 `nanddump -l <镜像长度>` 回读并比 md5；
5. 明确提示需要**断电冷启动**。

### 7.2 真机试运行输出

命令：`smartcar-ota firmware check`（首次，暴露了一个真 bug）

```
{FWCHECK}
```

```
{FWCHECK2}
```

```
{FWDRYRUN}
```

试运行做到了"除擦写以外的全部动作"，并且**已经真的产生了回滚材料**：

| 分区 | 名称 | 分区大小 | 备份文件 | 备份 md5 |
|---|---|---|---|---|
| `/dev/mtd4` | boot（CAN 内核） | 10485760 B | `/userdata/ota/fwbackup/mtd4_backup_*.bin` | `6603bd6907cbe12c802c5ab3c3975ead` |
| `/dev/mtd6` | amp（M0 固件） | 1048576 B | `/userdata/ota/fwbackup/mtd6_backup_*.bin` | `6653546e2481af20c89171595abf09e3` |

### 7.3 为什么最终没有真写 flash

三个理由，都是正当理由而不是"没时间"：

1. **板子上跑的 M0 固件，与我们要发布的那个镜像不是同一份。**
   实测：当前 `mtd6` 前 33280 字节 md5 = `184b641f13ed2af17b8905f4786133d8`，
   而 manifest 里的 `can_amp_m0_20260929.img` = `41ec6a3993b0fda1a9c2ad5225048c4a`。
   也就是说真刷下去**会改变 M0 的行为**，不是"刷回同一个东西"这种低风险操作。
   （`mtd4` 相反：前 6153728 字节 md5 = `72640b6a…`，与镜像**完全一致**，刷它是幂等的。）
2. **本次会话期间，有另一个进程在并发操作同一块板子**（见 §8.1），
   包括一次板子重启。在这种状态下写 flash，无法保证"这次写入的结果"能被干净地归因。
3. 任务是"先做应用层完整验证，固件层额外谨慎"，并且板子是唯一一块。

**因此固件层的能力边界如实标注为：读写路径已实现、校验/备份/回滚命令已实测打印，但"真写 + 回读校验"未在真机上执行过。**

---

## 8. 已知限制（如实写）

### 8.1 本次验证期间有第三方并发操作板子（重要）

时间线上抓到的证据：

| 时间 | 现象 |
|---|---|
| 21:26 | 我备份的"升级前"应用：`a647e89a…`，2374284 B，**无版本串** |
| 21:28 | 板子**重启**（我未执行任何 reboot） |
| 21:32:46 | 板上应用变成 2431212 B、md5 `67aec6f8…`，**含 `SMARTCAR_OTA_VERSION=v1.1.0` 与 `OtaPage` 符号** —— 是别人编译并推上去的 |
| 21:26:58 | 板上出现 `/userdata/smart_car/Smart_in-car_entertainment.mq135bak` |
| 21:33 | 别人在跑 `timeout 30 ./Smart_in-car_entertainment --page=hardware` 自检 |
| 验证期间 | `adb server` 被重启两次，`adb reverse` 隧道随之失效 |

后果与处置：
- 我立刻改用**独立的 VM 构建目录** `~/work/Smart_car_rk3506/build_rk3506_ota`，
  与原来的 `build_rk3506` 分开，避免两个进程同时 `make` 导致产物不可预测
  （实测踩过：板上应用既不是原始版也不是我编的版本，根因就是并发 make）；
- 我的 7 次升级都用**版本号 + md5** 双重锚定，能证明"装上去的确实是我编的那份"；
- 我没有回退、没有覆盖别人部署的东西，也没有写 mtd4/mtd6。

### 8.2 没有签名校验

manifest 和包只有 md5，**没有 RSA/Ed25519 签名**。这意味着：
有 LAN 访问权的人可以同时替换包和 manifest 里的 md5，板端会照装。
md5 在这里防的是"传输损坏 / 服务端文件与 manifest 不同步"，**不防恶意篡改**。
做签名需要板端有验签能力（板子上没有 openssl，需静态链一个精简实现）。

### 8.3 启动成功判据是"进程活着"，不是"业务功能正常"

`wait_running_ok` 判定的是"进程存在 + `/proc/<pid>/exe` 指向目标文件 + 版本串匹配"。
如果新版本能起来但**界面是黑的/某个功能坏了**，OTA 会认为成功。
更严的做法是加一个"版本无关的业务自检"（例如等应用写出某个心跳标记），本次未做。
现有的 `--page=xxx + SMARTCAR_SELFTEST_MS` 自检机制是天然的挂钩点，可以作为后续改进。

### 8.4 下载通道依赖 adb，不是产品化形态

`adb reverse` 适合开发/演示，真实车机应该是"板子自己通过 4G/WiFi 访问 OTA 服务器"。
好消息是**板端代码里没有任何 adb 相关的东西** —— `MANIFEST_URL` 是普通 HTTP URL，
用 `SMARTCAR_OTA_URL` 环境变量指向真实服务器即可，只需把隧道换成真实网络可达。

### 8.5 `/userdata` 空间有限，slot 会累积

板子 `/userdata` = 254.8MB。每次成功升级会留一份 2.4MB 的 slot。本次验证后：

```
32.0M  /userdata/ota/fwbackup     <- 三次 dryrun 各留了一份 10MB 的 mtd4 备份
16.4M  /userdata/ota/slots        <- 7 个版本各 2.4MB
 9.7M  /userdata/ota/tmp          <- 下载暂存（未自动清理）
```

当前剩余 192.8MB，不紧张，但**长期跑下去会满**。`smartcar-ota` 目前**没有自动清理策略**，
需要的话应加"只保留最近 N 个 slot / 清理 tmp / 限制 fwbackup 份数"。这是明确的功能缺口。

如果要立刻回收本次验证占用的空间（**注意：这会删掉回滚材料，确认不再需要回滚后再执行**）：

```sh
# 只保留最近 2 个 slot
ls -1d /userdata/ota/slots/*/ | sort | head -n -2 | xargs rm -rf
# 清掉下载暂存
rm -rf /userdata/ota/tmp/*
# 固件回滚镜像：确认不再需要回滚 mtd4/mtd6 后再删（每份 10MB）
ls -l /userdata/ota/fwbackup/    # 先看一眼
```

### 8.6 根文件系统只有 ~2.4MB 空闲

`ubi0:rootfs 150.0M 147.6M 2.4M 98%`。因此 OTA 的所有下载/解包**一律放 `/userdata`**，
代码里没有往 `/tmp` 或 `/` 写大文件的路径。如果以后要在 rootfs 里放东西，必须先解决空间。

### 8.7 M0 固件必须冷启动

`mtd6` 里的 M0（Cortex-M0）固件**只有断电冷启动才会重新加载**，`adb reboot` 不行。
所以固件层 OTA 完成后，必须提示现场人员**拔电重上电**，并在文档里写清楚。
应用层 OTA 不受此影响（`S98smartcar restart` 就够）。

### 8.8 版本比较是"最多 4 段数字"

`ver_lt()` 只按点分数字比较（`1.10.0 > 1.9.0` 是对的），不支持 `-rc1`、`+build` 这类后缀。
manifest 里的版本号必须保持纯数字点分格式。

---

## 9. 踩坑记录（都是真踩过的，写给下次的自己）

| # | 现象 | 根因 | 修法 |
|---|---|---|---|
| 1 | `adb shell '脚本'` 报 `adb: unknown command /home/cjh/tools/platform-tools/adb` | VM 侧 sh 把**嵌套单引号**拆坏了，参数错位 | 改成本地文件 → SFTP 到 VM → `adb push` 到板 `/tmp` → `adb shell sh /tmp/x.sh` |
| 2 | `-DSMARTCAR_OTA_VERSION_STR=1.1.0` 编译报 `expected ')' before numeric constant` | 转义要穿过 PowerShell→python→ssh→sh→qmake 五层，被啃成 `\1.1.0\` | 改成**发布脚本生成一个头文件** `ota_version_local.h`，零转义 |
| 3 | `make … \| tail -40` 编译失败了却返回 rc=0 | 管道退出码是 `tail` 的 | 去掉管道，让 `make` 自己决定 rc |
| 4 | 源码同步"上传 0 个"，编译出的二进制里没有新代码 | 用**文件大小**判断是否需要同步，本地与 VM 同名文件大小相同、内容不同 | 改成比 **md5**（多读一遍文件，但不会漏） |
| 5 | 界面上"磁盘版本/运行版本"永远是空，但日志显示探测脚本输出完全正确 | `SMARTCAR_OTA_VERSION_TAG` 是宏拼接，**本身就含版本号**（27 字节）；拿它去 `indexOf` + `mid` 等于"跳过整个串" → 恒为空 | 另用字面量前缀 `SMARTCAR_OTA_VERSION=`（21 字节）。定位靠的是把 `t.len=27` 打出来 |
| 6 | 界面版本仍为空（前一坑之前） | `QProcess` 是 execvp 不经 shell，把带单引号的 `sh -c "grep -ao '…'"` 直接当 argv 传给内层 sh，引号变字面量 | 改成**写一个探测脚本文件**再 `sh <文件>` |
| 7 | VM 上 `nohup python3 -m http.server &` 之后 `pgrep` 为空、板端 `wget: error getting response` | SSH 非交互会话结束时把后台进程收掉了 | 必须 `setsid … < /dev/null &` |
| 8 | `pkill -f 'http.server 18080'` 之后那条 ssh 命令的输出整段消失 | `-f` 匹配到了**自己这条 bash -c 命令行**，把自己杀了 | 先 `pgrep -f` 拿 PID，跳过 `$$`，再按 PID `kill` |
| 9 | `firmware dryrun` 的日志标题一直是 `act=check` | 入口处 `shift` 过一次，`firmware` 分支里又 `shift` 一次，子命令被吃掉 | 去掉重复的 `shift` |
| 10 | `firmware check` 报"条目 1 解析失败，跳过" | manifest 是 `json.dump(indent=2)` 的**多行美化 JSON**，我按"行首缩进 + 首行 `{`"切对象，一个都切不到 | 改成 awk 按**花括号配对**累积，紧凑/美化格式都能吃 |
| 11 | `hexdump` 在板上不存在 | buildroot 精简掉了 | 改用 busybox `od` |
| 12 | 被拒绝的升级在 `/userdata/ota/slots` 留下重复槽位 | 备份动作放在下载校验**之前** | 挪到"校验通过、写盘之前" |
| 13 | PowerShell 里 `kill -USR1 $(pidof xxx)` 传到板子变成 `kill -USR1 \; sleep 8` | PowerShell 先做了一次子表达式求值 | 命令统一写进 `.sh` 文件，用 `@文件` 形式执行 |

---

## 10. 复现步骤（拿到这个仓库后怎么重跑一遍）

```powershell
# 0) 前提：VM(192.168.88.130) 可 SSH；板子插在 VM 上且 adb devices 能看到

# 1) 交叉编译并发布一个版本
python Linux车载系统\tools\ota_build_and_publish.py --version 1.5.0 --notes "复现验证"

# 2) 起 HTTP + 隧道
python vmctl\ota_server.py start --port 18080

# 3) 部署板端工具（改过 smartcar-ota 之后必须重推）
python vmctl\ota_push_tool.py

# 4) 板端升级并取原始输出
python vmctl\ota_run.py x1_check  "smartcar-ota check"
python vmctl\ota_run.py x2_update "smartcar-ota update"
python vmctl\ota_run.py x3_verify "smartcar-ota verify"

# 5) 两个演练
python vmctl\ota_rollback_drill.py
python vmctl\ota_md5_drill.py

# 6) 核对没破坏现有功能
python vmctl\ota_verify_untouched.py
```

固件层（**会用唯一一块板子写 flash，请先确认你接受风险，并已确认备份存在**）：

```powershell
python vmctl\ota_run.py fw1 "smartcar-ota firmware check"
python vmctl\ota_run.py fw2 "smartcar-ota firmware dryrun"
# 只有在明确接受风险后：
# python vmctl\ota_run.py fw3 "smartcar-ota firmware flash --yes"
# 然后断电冷启动
```

---

## 11. 附：证据文件索引

所有原始输出在 `build_out/ota_evidence/`：

| 文件 | 内容 |
|---|---|
| `s0_before_status.txt` / `s1_before_status.txt` | 升级前基线 |
| `s2_check.txt` | `check`（1.1.0 → 1.2.0） |
| `s3_update_v120.txt` / `s4_verify.txt` | **第一次升级 + 独立复核** |
| `s7_update_v130.txt` / `s8_verify130.txt` | 第二次升级 + 复核 |
| `s11/s13/s17/s19/s21_*.txt` | 后续连续升级（1.3.1 / 1.3.2 / 1.3.3 / 1.4.0） |
| `rollback_drill.txt` | 回滚演练全过程 |
| `md5_reject_drill.txt` | 损坏包拒绝演练全过程 |
| `s25_fw_check2.txt` / `s27_fw_dryrun2.txt` | 固件层 check / dryrun |
| `s28_health.txt` | 最终健康检查（进程/设备节点/can0/init 脚本/空间） |
| `s29/s30/s31_mtd6*.txt` | mtd6 内容核对（确认分区未被本次操作改动） |
| `app_11_ota_v140.png` | 「系统升级」页最终截图 |
| `geometry.txt` | 全部页面控件几何/字号 dump（排版取证用） |
"""


def main():
    # 图片引用跟着 EVID 走：证据在仓库内 -> 原先那种相对路径；已在别处（如 E 盘）-> file:/// 绝对 URL
    if os.path.abspath(EVID).lower().startswith(os.path.abspath(OUTER).lower()):
        evid_url = os.path.relpath(EVID, DOCS).replace(os.sep, "/")
    else:
        evid_url = "file:///" + EVID.replace(os.sep, "/")
    subs = {
        "EVIDURL": evid_url,
        "CHECK": ev("s2_check"),
        "UPDATE120": ev("s3_update_v120"),
        "VERIFY120": ev("s4_verify"),
        "UPDATE130": ev("s7_update_v130"),
        "UPDATE140": ev("s21_v140"),
        "HEALTH": ev("s28_health"),
        "ROLLBACK": ev("rollback_drill"),
        "MD5DRILL": ev("md5_reject_drill"),
        "FWCHECK": ev("s24_fw_check"),
        "FWCHECK2": ev("s25_fw_check2"),
        "FWDRYRUN": ev("s27_fw_dryrun2"),
    }
    out = DOC
    for k, v in subs.items():
        out = out.replace("{%s}" % k, v)
    left = [l for l in out.split("\n") if l.strip().startswith("{") and l.strip().endswith("}")]
    if left:
        print("!! 还有未替换的占位符:", left)
    p = os.path.join(DOCS, "OTA升级实现与验证_20261012.md")
    with io.open(p, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(out)
    print("已生成: %s" % p)
    print("  行数: %d  字符: %d" % (out.count("\n") + 1, len(out)))


if __name__ == "__main__":
    main()
