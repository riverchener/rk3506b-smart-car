#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""用 weston-screenshooter 抓真实屏幕（含合成器与壁纸），拉回本地。"""
import os
import sys
import paramiko

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
OUT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build_out", "realshot"))
os.makedirs(OUT, exist_ok=True)

cli = paramiko.SSHClient()
cli.set_missing_host_key_policy(paramiko.AutoAddPolicy())
cli.connect("192.168.88.130", 22, "cjh", "181305", timeout=30,
            look_for_keys=False, allow_agent=False)
ADB = "~/tools/platform-tools/adb"


def board(script, timeout=600):
    cmd = ADB + " shell sh -s <<'EOS'\n" + script + "\nEOS\n"
    _, out, err = cli.exec_command(cmd + " </dev/null", timeout=timeout)
    return (out.read().decode("utf-8", "replace") + err.read().decode("utf-8", "replace")).strip()


print("=== 抓真实屏幕 ===")
print(board("export XDG_RUNTIME_DIR=/var/run; export WAYLAND_DISPLAY=wayland-0; "
            "GUARD=/userdata/wsflash; mkdir -p $GUARD || exit 1; cd $GUARD || exit 1; "
            "case \"$PWD\" in /userdata/*) ;; *) echo FATAL-hardrule3-CWD=$PWD >&2; exit 97 ;; esac; "
            "rm -f $GUARD/real1.png; weston-screenshooter $GUARD/real1.png 2>&1 | tail -3; "
            "ls -l $GUARD/real1.png 2>&1"))

print("=== 切到车辆状态页后再抓一张（顺便看切页是否干净）===")
print(board("export XDG_RUNTIME_DIR=/var/run; export WAYLAND_DISPLAY=wayland-0; "
            "GUARD=/userdata/wsflash; mkdir -p $GUARD || exit 1; cd $GUARD || exit 1; "
            "kill -USR2 1 2>/dev/null; sleep 1; "
            "rm -f $GUARD/real2.png; weston-screenshooter $GUARD/real2.png 2>&1 | tail -2; "
            "ls -l $GUARD/real2.png 2>&1"))

sftp = cli.open_sftp()
for name in ("real1.png", "real2.png"):
    try:
        dst = os.path.join(OUT, name)
        sftp.get("/userdata/wsflash/" + name, dst)
        print("已取回:", dst, os.path.getsize(dst), "B")
    except Exception as e:
        print("取回失败", name, e)
sftp.close()
cli.close()
