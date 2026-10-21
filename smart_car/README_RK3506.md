# RK3506B 部署补充说明（本目录）

这份 README 补充原项目说明，聚焦 **RK3506B（ATK-DLRK3506B + 5.5 寸 MIPI 屏）** 的部署。

## 快速开始

```bash
# 板子上（adb shell 进去）
cd /userdata/smart_car
./Smart_in-car_entertainment        # 手动跑（正常开机已自启）
./hwtest                            # 外设接线自检
/etc/init.d/S99appinit restart      # 重启应用
tail -f /var/log/smartcar.log       # 看日志
```

## 屏幕

- 面板原生 720×1280 竖屏，通过 weston `transform=rotate-90` 变成 **1280×720 横屏**
- 应用按 1024×600 设计画布等比放大到整屏（`uiadapter.h`），全屏无裁切
- 改缩放：`export SMARTCAR_UI_SCALE=125`（百分比）

## 外设（默认引脚，可改）

| 外设 | 默认 GPIO | 排针位置 | 备注 |
|---|---|---|---|
| DHT11 DATA | 51 (GPIO1_C3) | A28 | 需 4.7k~10k 上拉到 3.3V |
| SR04 TRIG | 54 (GPIO1_C6) | A23 | 直连 |
| SR04 ECHO | 55 (GPIO1_C7) | A24 | **必须 1k/2k 分压** |
| 蜂鸣器 | 56 (GPIO1_B0) | A37 | 高电平响 |
| MQ-135 AO | SARADC_IN3 | B39 | 1.8V 量程，注意分压 |
| 板载 LED | — | 板上 | `/sys/class/leds/work/brightness` |

详细接线图、排错表见 `docs/接线与使用手册.md`。

## 存储布局

`/userdata` 与 `/opt` 是**同一个分区**（255MB），不要把两者互相做软链接。

```
/userdata/smart_car/
├── Smart_in-car_entertainment
├── smartcar_hw.ko
├── hwtest / evinject
├── media_player.db
├── myMusic/    (15 首 mp3 + lrc, 213MB)
├── Videos/     (放得下的视频)
└── docs/
```

**空间不够就往 TF 卡 / U 盘放**，应用会自动搜索 `/mnt/sdcard/Videos`、
`/mnt/udisk/Videos`，或用 `SMARTCAR_VIDEO_DIRS` 指定。

### 把视频同步到 TF 卡（在 VMware 虚拟机里执行）

```bash
export PATH=$PATH:~/tools/platform-tools
# 1) 插好 TF 卡，看板子上挂到哪了
adb shell "ls /mnt/sdcard /mnt/udisk"
# 2) 在卡上建目录并推送（假设挂载点是 /mnt/sdcard）
adb shell "mkdir -p /mnt/sdcard/Videos"
adb push ~/work/Smart_car/Videos/. /mnt/sdcard/Videos/
# 3) 确认应用能找到（重启应用后视频页应该能看到条目）
adb shell "/etc/init.d/S99appinit restart"
```

## 编译

见 `docs/移植说明_RK3506.md` 第 4 节（qmake + SDK buildroot 工具链）。
