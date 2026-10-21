#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""只编译（不推板）：把源码推到 VM 工程树，qmake + make，打印错误。"""
import os
import sys
import paramiko

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

cli = paramiko.SSHClient()
cli.set_missing_host_key_policy(paramiko.AutoAddPolicy())
cli.connect("192.168.88.130", 22, "cjh", "181305", timeout=25,
            look_for_keys=False, allow_agent=False)

SDK = "/home/cjh/atk_dlrk3506_linux6.1_release_v1.3.1_20260326"
HOST = SDK + "/buildroot/output/alientek_rk3506/host/bin"
TREE = "/home/cjh/work/Smart_car_rk3506"
LOCAL = r"D:\cjh\新建文件夹\Linux车载系统\Linux车载系统\Smart_car"
FILES = ("btmedia.h", "btmedia.cpp", "Audio.h", "Audio.cpp",
         "mainwindow.h", "mainwindow.cpp", "main.cpp",
         "canmgr.h", "canmgr.cpp", "carstatuspanel.h", "carstatuspanel.cpp",
         "incarcontrol.h", "incarcontrol.cpp",
         "vehiclestatuspage.h", "vehiclestatuspage.cpp",
         "appicons.h", "appicons.cpp",
         "Smart_in-car_entertainment.pro")


def all_sources():
    """直接把源码目录里所有 .h/.cpp/.pro/.ui/.qrc 都推上去 ——
    这样就不用每次新增文件都回来改这份清单（吃过这个亏：忘了加 appicons.*，
    结果 VM 上编译报 mainwindow.cpp 找不到头文件）。"""
    import glob as _glob
    out = []
    for pat in ("*.h", "*.cpp", "*.pro", "*.ui", "*.qrc"):
        out += [os.path.basename(p) for p in _glob.glob(os.path.join(LOCAL, pat))]
    return sorted(set(out))


def run(cmd, timeout=3600):
    _, out, err = cli.exec_command(cmd, timeout=timeout)
    return (out.read().decode("utf-8", "replace") + err.read().decode("utf-8", "replace"))


sftp = cli.open_sftp()
pushed = 0
for f in all_sources():
    p = os.path.join(LOCAL, f)
    if not os.path.exists(p):
        continue
    sftp.put(p, "%s/%s" % (TREE, f))
    pushed += 1
sftp.close()
print("已推送 %d 个源文件" % pushed)

print(run(r'''cd __T__/build_rk3506 && __H__/qmake .. >/tmp/q6.log 2>&1; echo QMAKE=$?
make -j8 >/tmp/m6.log 2>&1; echo MAKE=$?
echo '--- 错误行 ---'
grep -nE 'error:|错误' /tmp/m6.log | head -40
echo '--- 警告里与本次新文件相关的 ---'
grep -nE 'canmgr|carstatuspanel|incarcontrol|mainwindow' /tmp/m6.log | grep -iE 'warning|error' | head -20
ls -l __T__/Smart_in-car_entertainment'''.replace("__T__", TREE).replace("__H__", HOST)))
cli.close()
