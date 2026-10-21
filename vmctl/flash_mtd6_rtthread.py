#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""烧录 RT-Thread M0 固件到 mtd6。

顺序（任一步不符即中止）：备份现有 mtd6 → 校验备份 → 推送 amp.img → 板上校验 md5
→ flash_erase → nandwrite → 回读校验 → 给出冷启动后的验证清单。
"""
import os
import sys
import hashlib
import paramiko

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

WANT_MD5 = "7cd7b9e5046ef45d2d65f3bbb87713ce"      # amp.img（RT-Thread）
LOCAL_IMG = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                                         "Linux车载系统", "Smart_car", "amp", "rtthread", "out", "amp.img"))
REMOTE_IMG = "/home/cjh/amp_rtt.img"

cli = paramiko.SSHClient()
cli.set_missing_host_key_policy(paramiko.AutoAddPolicy())
cli.connect("192.168.88.130", 22, "cjh", "181305", timeout=30,
            look_for_keys=False, allow_agent=False)
ADB = "~/tools/platform-tools/adb"


def sh(cmd, timeout=900):
    _, out, err = cli.exec_command(cmd + " </dev/null", timeout=timeout)
    return (out.read().decode("utf-8", "replace") + err.read().decode("utf-8", "replace")).strip()


def adb(script, timeout=900):
    cmd = ADB + " shell sh -s <<'EOS'\n" + script + "\nEOS\n"
    _, out, err = cli.exec_command(cmd + " </dev/null", timeout=timeout)
    return (out.read().decode("utf-8", "replace") + err.read().decode("utf-8", "replace")).strip()


def adb_raw(args, timeout=900):
    """给 push / pull 这类 adb 自身子命令用（不能被包进板子 shell ✗）"""
    _, out, err = cli.exec_command(ADB + " " + args + " </dev/null", timeout=timeout)
    return (out.read().decode("utf-8", "replace") + err.read().decode("utf-8", "replace")).strip()


def die(msg):
    print("!! 中止：" + msg)
    cli.close()
    sys.exit(1)


# 0) 本地镜像先自检
if not os.path.exists(LOCAL_IMG):
    die("本地镜像不存在： " + LOCAL_IMG)
md5_local = hashlib.md5(open(LOCAL_IMG, "rb").read()).hexdigest()
print("本地 amp.img: %d B  md5=%s" % (os.path.getsize(LOCAL_IMG), md5_local))
if md5_local != WANT_MD5:
    die("本地镜像 md5 与预期不符（期望 %s）" % WANT_MD5)

# 1) 备份现有 mtd6
print("\n=== 1) 备份现有 mtd6（1 MiB）到 /userdata/board_disable/mtd6_before_rtt.bin ===")
print(adb("mkdir -p /userdata/board_disable; "
          "nanddump -f /userdata/board_disable/mtd6_before_rtt.bin /dev/mtd6 2>&1 | tail -3; "
          "ls -l /userdata/board_disable/mtd6_before_rtt.bin; "
          "md5sum /userdata/board_disable/mtd6_before_rtt.bin"))
bak = adb("md5sum /userdata/board_disable/mtd6_before_rtt.bin | cut -d' ' -f1")
print("备份 md5 =", bak)
if len(bak) != 32:
    die("备份文件没有生成成功")

# 2) 推送镜像并校验
print("\n=== 2) 推送 amp.img 到板子并校验 md5 ===")
sftp = cli.open_sftp()
sftp.put(LOCAL_IMG, REMOTE_IMG)
sftp.close()
print(sh("md5sum %s; ls -l %s" % (REMOTE_IMG, REMOTE_IMG)))
print(adb_raw("push %s /userdata/amp_rtt.img" % REMOTE_IMG))
out = adb("md5sum /userdata/amp_rtt.img; ls -l /userdata/amp_rtt.img")
print(out)
if WANT_MD5 not in out:
    die("板上的 amp.img md5 不符，未写 flash")

# 3) 擦写 mtd6
print("\n=== 3) 擦除并写入 /dev/mtd6 ===")
print(adb("echo '--- 写入前 mtd6 前 16 字节:'; nanddump -l 16 -f - /dev/mtd6 2>/dev/null | tail -2"))
print(adb("flash_erase /dev/mtd6 0 0 2>&1 | tail -2"))
print(adb("nandwrite -p /dev/mtd6 /userdata/amp_rtt.img 2>&1 | tail -3"))

# 4) 回读校验
print("\n=== 4) 回读校验（取前 31232 字节与镜像比对）===")
print(adb("nanddump -l %d -f /tmp/mtd6_readback.bin /dev/mtd6 2>/dev/null; "
          "dd if=/tmp/mtd6_readback.bin bs=31232 count=1 2>/dev/null | md5sum; "
          "dd if=/userdata/amp_rtt.img bs=31232 count=1 2>/dev/null | md5sum" % 31232))

# 5) 冷启动后的验证清单
print("\n=== 5) 写入完成。冷启动后（断电重启）请让主代理跑这些验证 ===")
print(adb("echo 'devmem 0x03e0639c 期望 0x414D5042'; devmem 0x03e0639c; "
          "echo 'devmem 0x03e063a0 期望 6'; devmem 0x03e063a0; "
          "echo '--- 当前（未冷启动）M0/rpmsg 状态 ---'; cat /sys/bus/rpmsg/devices/*/name 2>/dev/null; "
          "ls -l /dev/mycarctl /dev/mysr04 /dev/mydht11 /dev/mybuzzer 2>&1"))
cli.close()
print("\n>>> 现在请【断电冷启动】（热重启不会重载 M0）")
