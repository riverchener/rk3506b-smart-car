#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""把板端 OTA 工具部署到板子，并把"升级前"的原始应用备份到 PC。

OTA 升级会把 /userdata/smart_car/Smart_in-car_entertainment 换掉。
在换之前**先把原文件留一份到 PC**，这是升级第一原则：
板子上只有一份唯一硬件，出问题必须能拿回原始镜像。
"""
import hashlib
import os
import sys

import paramiko

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
WORKSPACE = os.path.abspath(os.path.join(HERE, "..", "Linux车载系统"))
TOOL = os.path.join(WORKSPACE, "Smart_car", "board", "smartcar-ota")
BAK_DIR = os.path.join(WORKSPACE, "build_out", "app_before_ota")
APP = "/userdata/smart_car/Smart_in-car_entertainment"
ADB = "~/tools/platform-tools/adb"

cli = paramiko.SSHClient()
cli.set_missing_host_key_policy(paramiko.AutoAddPolicy())
cli.connect("192.168.88.130", 22, "cjh", "181305", timeout=30,
            look_for_keys=False, allow_agent=False)


def sh(cmd, timeout=600):
    cmd = cmd.replace("\r\n", "\n").replace("\r", "\n")
    _, out, err = cli.exec_command(cmd, timeout=timeout)
    return (out.read().decode("utf-8", "replace")
            + err.read().decode("utf-8", "replace"))


def adb(cmd, timeout=600):
    return sh("%s %s </dev/null 2>&1" % (ADB, cmd), timeout)


def md5_local(p):
    h = hashlib.md5()
    with open(p, "rb") as fh:
        for b in iter(lambda: fh.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


print("=" * 74)
print("1) 部署板端工具 smartcar-ota")
print("=" * 74)
sftp = cli.open_sftp()
sftp.put(TOOL, "/tmp/smartcar-ota")
sftp.close()
print(adb("push /tmp/smartcar-ota /usr/bin/smartcar-ota"))
# 同时放一份在 /userdata/smart_car（界面会优先用 /usr/bin，找不到就用这个）
print(adb("push /tmp/smartcar-ota /userdata/smart_car/smartcar-ota"))
print(adb("shell 'chmod 755 /usr/bin/smartcar-ota /userdata/smart_car/smartcar-ota; "
          "ls -l /usr/bin/smartcar-ota /userdata/smart_car/smartcar-ota'"))
print(adb("shell 'md5sum /usr/bin/smartcar-ota /userdata/smart_car/smartcar-ota'"))
print("本地工具 md5 =", md5_local(TOOL))

print()
print("=" * 74)
print("2) 备份升级前的原始应用 -> PC")
print("=" * 74)
os.makedirs(BAK_DIR, exist_ok=True)
sh("mkdir -p /home/cjh/ota_origbk")
print(adb("shell 'md5sum %s; ls -l %s; grep -ao SMARTCAR_OTA_VERSION=[0-9A-Za-z._-]* %s "
          "| head -1 || echo \"(原应用没有版本串，属预期)\"'"
          % (APP, APP, APP)))
rc = adb("pull %s %s/orig_Smart_in-car_entertainment" % (APP, "/home/cjh/ota_origbk"))
print(rc)
sftp = cli.open_sftp()
dst = os.path.join(BAK_DIR, "orig_Smart_in-car_entertainment")
try:
    sftp.get("/home/cjh/ota_origbk/orig_Smart_in-car_entertainment", dst)
    print("已备份到: %s" % dst)
    print("  大小: %d B" % os.path.getsize(dst))
    print("  md5 : %s" % md5_local(dst))
except Exception as e:  # noqa: BLE001
    print("!! 备份失败:", e)
sftp.close()

print()
print("=" * 74)
print("3) 板端环境确认")
print("=" * 74)
print(adb("shell 'command -v wget md5sum tar awk; echo ---; "
          "mkdir -p /userdata/ota; df -h /userdata | tail -1'"))
print(adb("shell 'smartcar-ota status'"))
cli.close()
