#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""全新安装：应用 + S98smartcar + S49can（新 rootfs 场景，容忍旧目录残留）。
   流程：清理残留 → 推应用 → 推板端脚本 → 起服务 → 验证"""
import os
import sys
import paramiko

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HOST, USER, PW = "192.168.88.130", "cjh", "181305"
TREE = "/home/cjh/work/Smart_car_rk3506"
BOARD = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                     "Linux车载系统", "Smart_car", "board")
BOARD = os.path.abspath(BOARD)
APP_DIR = "/userdata/smart_car"
APP = "Smart_in-car_entertainment"

cli = paramiko.SSHClient()
cli.set_missing_host_key_policy(paramiko.AutoAddPolicy())
cli.connect(HOST, 22, USER, PW, timeout=30, look_for_keys=False, allow_agent=False)
ADB = "~/tools/platform-tools/adb"


def sh(cmd, timeout=600):
    _, out, err = cli.exec_command(cmd + " </dev/null", timeout=timeout)
    return (out.read().decode("utf-8", "replace") + err.read().decode("utf-8", "replace"))


def adb(cmd, timeout=600):
    return sh(ADB + " " + cmd, timeout)


print("=== 0) 板上现状：/userdata/smart_car 与两个 init 脚本 ===")
print(adb("shell 'ls -la %s 2>/dev/null; echo ---; ls -l /etc/init.d/S98smartcar /etc/init.d/S49can 2>/dev/null'" % APP_DIR))

print("=== 1) 清理残留（若是同名目录先删）===")
print(adb("shell 'if [ -d %s/%s ]; then rm -rf %s/%s && echo removed-dir; else echo no-dir; fi'" % (APP_DIR, APP, APP_DIR, APP)))

print("=== 2) 推应用（VM 构建产物 → 板子）===")
print(adb("shell 'mkdir -p %s'" % APP_DIR))
print(adb("push %s/%s %s/" % (TREE, APP, APP_DIR)))
print(adb("shell 'chmod 755 %s/%s; ls -l %s/'" % (APP_DIR, APP, APP_DIR)))

print("=== 3) 推板端脚本（S98smartcar / S49can / 其它 *.sh、car-demo-*）===")
sftp = cli.open_sftp()
local_scripts = []
for name in sorted(os.listdir(BOARD)):
    # ⚠ 只挑**要装到板子上的可执行脚本**。以前的正则 (car-demo|smartcar) 会连带
    #   car-demo-start.sh、smartcar-ota、smartcar.conf、smartcar-cand.c 一起
    #   被塞进 /usr/bin（源码和配置丢进 bin 目录），也漏掉了新加的 smartcar-ota。
    #   这里明确分两类：
    #     S*        -> /etc/init.d/
    #     其余无扩展名或 .sh 的可执行工具 -> /usr/bin/
    if name.startswith("S") and not name.endswith(".sh"):
        local_scripts.append(name)
    elif name in ("car-demo-start.sh", "car-demo-stop.sh",
                  "bt-ctl", "bt-agent", "bt-boot-setup", "bt-connect",
                  "bt-keystore-init", "wifi-ctl", "smartcar-ota"):
        local_scripts.append(name)
print("本地发现:", local_scripts)
tmpdir = "/tmp/board_scripts"
sh("mkdir -p " + tmpdir)
for name in local_scripts:
    # ⚠ Windows 上 os.path.join 会用反斜杠，绝不能直接当远端 POSIX 路径
    sftp.put(os.path.join(BOARD, name), "%s/%s" % (tmpdir, name))
    print("  已上传到 VM:", name)
print(sh("ls -l " + tmpdir))
for name in local_scripts:
    target = "/etc/init.d/" + name if name.startswith("S") else "/usr/bin/" + name
    print(adb("push %s/%s %s" % (tmpdir, name, target)))
    adb("shell 'chmod 755 %s'" % target)
print(adb("shell 'ls -l /etc/init.d/ | grep -iE \"smart|can\"; echo ---; ls -l /usr/bin/ | grep -iE \"car-demo|smartcar\"'"))
sftp.close()

print("=== 3.5) 加载 A7 驱动模块（sc_sensors.ko，提供 /dev/mycarctl 等）===")
print(sh("ls -l %s/*.ko 2>/dev/null" % TREE))
print(adb("shell 'lsmod | grep -iE \"sc_|smartcar|carctl\"'"))
print(adb("push %s/sc_sensors.ko /tmp/ 2>&1" % TREE))
print(adb("shell 'insmod /tmp/sc_sensors.ko 2>&1; sleep 1; ls -l /dev/mycarctl /dev/mysr04 /dev/mydht11 /dev/mybuzzer 2>&1'" )) 


print("=== 4) 起服务（先看 S98smartcar 是否可用）===")
print(adb("shell 'test -x /etc/init.d/S98smartcar && echo has-S98smartcar || echo NO-S98smartcar'"))
print(adb("shell '/etc/init.d/S98smartcar restart 2>&1 | tail -20'"))
print("=== 5) 验证：进程 / 日志 / 设备 / can0 ===")
print(adb("shell 'ps aux | grep -i Smart_in-car | grep -v grep'"))
print(adb("shell 'tail -15 /var/log/smartcar.log 2>/dev/null || tail -15 /userdata/smartcar-boot.log 2>/dev/null'"))
print(adb("shell 'ls -l /dev/mycarctl /dev/mysr04 /dev/mydht11 /dev/mybuzzer 2>&1; ip -br link show can0'"))
cli.close()
