#!/bin/bash
# 定版后的整机备份：重新 dump 板子分区 -> 打 update.img（含最新 amp.img）
set -e
SDK=/home/xiaotan/atk_dlrk3506_linux6.1_release_v1.3.1_20260326
P=$SDK/tools/linux/Linux_Pack_Firmware/rockdev
MCUIMG=$SDK/hal/project/rk3506-mcu/Image/amp.img
export PATH=$PATH:/home/xiaotan/tools/platform-tools

TS=$(date +%Y%m%d_%H%M%S)
BK=/home/xiaotan/smartcar_final_$TS
mkdir -p "$BK/parts" "$BK/pack/Image"
echo "### $BK"

part_size() {   # mtd号 -> 字节大小（读 /proc/mtd，格式: "mtd0: 00100000 00020000 \"uboot\""）
    adb shell cat /proc/mtd | awk -v d="mtd$1:" '$1 == d {print $2}' | tr -d '\r'
}

dump() {
    sz=$(part_size $1)          # /proc/mtd 给的是十六进制，如 00400000
    bytes=$(printf '%d' "0x$sz")   # POSIX：不要用 $((16#$sz))，dash 不支持
    echo -n "  mtd$1 -> $2 (分区 $bytes 字节) ... "
    adb exec-out dd if=/dev/mtd$1 bs=1048576 2>/dev/null > "$BK/parts/$2"
    truncate -s "$bytes" "$BK/parts/$2"
    echo "$(stat -c %s "$BK/parts/$2") 字节"
}

echo "=== 1. dump 分区（已按分区大小裁剪）==="
dump 0 uboot.img
dump 1 misc.img
dump 3 recovery.img
dump 4 boot.img
dump 5 rootfs.img
dump 6 amp_on_device.img
dump 7 userdata.img

echo "=== 2. 组装 ==="
cp -f "$P/afptool" "$P/rkImageMaker" "$BK/pack/"
cp -f "$SDK/device/rockchip/.chips/rk3506/parameter-nand-amp.txt" "$BK/pack/Image/parameter.txt"
cp -f "$SDK/u-boot/rk3506_spl_loader_v1.06.111.bin" "$BK/pack/Image/MiniLoaderAll.bin"
for f in uboot boot misc recovery rootfs userdata; do cp -f "$BK/parts/$f.img" "$BK/pack/Image/$f.img"; done
cp -f "$MCUIMG" "$BK/pack/Image/amp.img"

# 干净的 ASCII package-file（含 amp，不含本板没有的 oem）
cat > "$BK/pack/package-file" <<'EOF'
package-file	package-file
bootloader	Image/MiniLoaderAll.bin
parameter	Image/parameter.txt
uboot		Image/uboot.img
boot		Image/boot.img
rootfs		Image/rootfs.img
recovery	Image/recovery.img
userdata	Image/userdata.img
misc		Image/misc.img
amp		Image/amp.img
EOF

echo "=== 3. 打包 ==="
cd "$BK/pack"
./afptool -pack ./ Image/update.img
./rkImageMaker -RK3506 Image/MiniLoaderAll.bin Image/update.img update.img -os_type:androidos
ls -la update.img

echo "=== 4. 清单 ==="
{
    echo "# Smart_car / RK3506B AMP 定版备份 $(date '+%Y-%m-%d %H:%M:%S')"
    echo
    echo "## update.img"
    ls -la "$BK/pack/update.img"; md5sum "$BK/pack/update.img"; sha256sum "$BK/pack/update.img"
    echo
    echo "## amp.img（M0 固件：测距/蜂鸣器/串口日志；DHT 由 A7 推送，M0 本地采样已关）"
    md5sum "$BK/pack/Image/amp.img"
    echo
    echo "## 分区镜像 parts/"
    (cd "$BK/parts" && md5sum *.img)
    echo
    echo "## 当前分工"
    echo "  M0  = HC-SR04 测距 + 蜂鸣器 + UART4 控制台日志（距离/温度/湿度一行打印）"
    echo "  A7  = DHT11 温湿度（sc_sensors.ko 内核 bit-bang，每 2 s 主动采样并 rpmsg 推给 M0）+ 界面/网络/多媒体"
    echo "  详见 docs/模块职责_20260921.md 第 7 章（DHT11 归属结论）"
    echo
    echo "## 还原"
    echo "  A) 整机：RKDevTool 加载 update.img"
    echo "  B) 单分区：flash_erase /dev/mtdN 0 0; nandwrite -p /dev/mtdN x.img"
    echo "     mtd0=uboot mtd1=misc mtd3=recovery mtd4=boot mtd5=rootfs mtd6=amp mtd7=userdata"
    echo "  C) 只刷 M0 固件：写 mtd6 后必须断电冷启动"
} > "$BK/MANIFEST.md"
cat "$BK/MANIFEST.md"
echo "### DONE $BK"
