# `amp/rtthread/` — M0 固件的 RT-Thread 版

这是 RK3506B 车机 **Cortex-M0** 固件的 RT-Thread 移植，替换原来的裸机
`amp/sc_amp.c` + `rpmsg_env_bm.c`。对外行为（rpmsg 协议、`/dev/mycarctl`、`/dev/mysr04`、
`/dev/mybuzzer` 语义、UART4 日志）与裸机版一致。

完整说明见 **[`../../docs/M0_RTThread移植说明.md`](../../docs/M0_RTThread移植说明.md)**，
这里只放最短路径。

## 构建

```bash
# VM 上，RK_SDK_DIR 不设则用默认 SDK 路径
cd ~/work/Smart_car_rk3506/amp/rtthread
./build_m0_rtt.sh
```

产物在 `out/`：

| 文件 | 大小 | md5 |
|---|---|---|
| `mcu.bin`（M0 裸固件） | 26 416 B | `cd1795fb6e65b2e51a28b38e4972e36b` |
| `amp.img`（FIT，load 0xfff84000） | 31 232 B | `7cd7b9e5046ef45d2d65f3bbb87713ce` |

内存（32 KiB 全部用完，见文档 §6）：

```
image (.text 25716 + exidx/zerotbl 24 + .data 676 + .bss 3776) = 30192 B
heap  (链接期 ASSERT >= 1536)               =  1552 B
MSP stack                                   =  1024 B
                                        total 32768 B
```

## 目录

| 路径 | 内容 |
|---|---|
| `rtconfig.py` `rtconfig.h` `Kconfig` `.config` | 工具链与裁剪（tick 8000 Hz / 8 优先级 / 只留 sem+mutex+mailbox） |
| `SConstruct` `SConscript` | scons 入口（`has_libcpu=False` = 包含 `libcpu/arm/cortex-m0`） |
| `gcc_link.ld.S` | M0 的 32 KiB 地图 + rpmsg 共享区 + 堆下限断言（修正了厂商脚本的 MSP 起点 bug） |
| `hal_conf.h` | 厂商 HAL 配置（`HAL_DBG` 改走 `rt_kprintf`） |
| `board/evb/board.c` | `rt_hw_board_init()`、UART4 控制台、SysTick 8 kHz、中断钩子 |
| `applications/` | 应用层：`sc_hw.c` `sc_carctl.c` `sc_sr04.c` `sc_rpmsg.c` `main.c` `sc_libc.c` + rpmsg-lite 的 RT-Thread 环境/平台层 |
| `Image/amp_mcu_rtt.its` | FIT 描述（`compile { sys="rtt"; core="mcu" }`） |
| `build_m0_rtt.sh` | 安装到 SDK → scons → 内存预算 → 打 `amp.img` → md5 |

`applications/sc_carctl.h` **不是**本目录的源文件：构建时从 `../sc_carctl.h` 复制，
保证固件与 `../test_carctl_host.c` 的时序测试共用同一份实现。

## 烧录（只刷 M0）

```sh
flash_erase /dev/mtd6 0 0
nandwrite -p /dev/mtd6 /userdata/amp_rtt.img      # mtd6 = amp 分区
# 必须断电冷启动（板上无 remoteproc，热重启不会重启 M0）
devmem 0x03e0639c      # 期望 0x414D5042
devmem 0x03e063a0      # 期望 0x00000006
```
