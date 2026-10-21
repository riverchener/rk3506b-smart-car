#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""真屏抓图通用脚本（kmscap 走 DRM 读内核正在扫的 framebuffer）。

用法：
    python vmctl/pf_shot.py <tag> [page] [waitSec] [--rebuild]

  tag   输出文件名前缀，落 build_out/pfshot/<tag>.png（1280x720 逻辑画面）
  page  应用启动页（desktop/audio/weather/player/hardware/map/sysmon/bluetooth...），
        不传：直接抓当前正在跑的应用（**不动进程**）。
        传了：kill + 用 --page=<page> 重启应用再抓。
  wait  重启后等待秒数，默认 20

⚠️ 刚重启应用那一小段时间，weston 可能给出一张**未初始化的 shm 缓冲**（整屏纯白/纯黑）。
   这是本项目的老朋友，不代表界面坏。所以这里会自动判"整屏单一颜色"并重抓，
   最多 4 次（每次间隔 3 秒）。判据在 Python 侧做（板上不一定有 python）。
"""
import base64
import os
import sys
import time
import paramiko
from collections import Counter
from PIL import Image

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
SDK = "/home/cjh/atk_dlrk3506_linux6.1_release_v1.3.1_20260326"
HOST_BIN = SDK + "/buildroot/output/alientek_rk3506/host/bin"
STAG = SDK + "/buildroot/output/alientek_rk3506/staging"
WORK = "/home/cjh/work/ui2cap"
ADB = "/home/cjh/tools/platform-tools/adb"
APP = "Smart_in-car_entertainment"
BASE = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
OUT = os.path.join(BASE, "build_out", "pfshot")
os.makedirs(OUT, exist_ok=True)

argv = [a for a in sys.argv[1:] if not a.startswith("--")]
tag = argv[0] if len(argv) > 0 else "shot"
page = argv[1] if len(argv) > 1 else ""
wait = int(argv[2]) if len(argv) > 2 else 20
rebuild = "--rebuild" in sys.argv

cli = paramiko.SSHClient()
cli.set_missing_host_key_policy(paramiko.AutoAddPolicy())
cli.connect("192.168.88.130", 22, "cjh", "181305", timeout=25,
            look_for_keys=False, allow_agent=False)


def vm(cmd, timeout=900):
    _, o, e = cli.exec_command(cmd, timeout=timeout)
    return (o.read().decode("utf-8", "replace") + e.read().decode("utf-8", "replace"))


def board(script, timeout=600):
    b64 = base64.b64encode(script.encode("utf-8")).decode("ascii")
    cmd = ("timeout %d %s shell \"echo %s | base64 -d | sh\" </dev/null 2>&1"
           % (timeout, ADB, b64))
    _, o, e = cli.exec_command(cmd, timeout=timeout + 20)
    return (o.read().decode("utf-8", "replace") + e.read().decode("utf-8", "replace"))


print("=== 0) 保证板上有 kmscap ===")
have = vm("%s shell 'ls -l /tmp/kmscap 2>/dev/null' </dev/null 2>&1" % ADB)
if rebuild or "kmscap" not in have:
    sftp = cli.open_sftp()
    sftp.put(os.path.join(os.path.dirname(os.path.abspath(__file__)), "kmscap.c"),
             WORK + "/kmscap.c")
    sftp.close()
    print(vm("cd %s && %s/arm-linux-gcc -O2 -o kmscap kmscap.c -I%s/usr/include 2>&1; ls -l kmscap"
             % (WORK, HOST_BIN, STAG)))
    print(vm("%s push %s/kmscap /tmp/kmscap >/dev/null 2>&1; "
             "%s shell 'chmod 755 /tmp/kmscap' </dev/null" % (ADB, WORK, ADB)))
else:
    print("已有 /tmp/kmscap:", have.strip())

if page:
    print("=== 1) 用 --page=%s 重启应用并等 %ss ===" % (page, wait))
    print(board(r'''
export XDG_RUNTIME_DIR=/var/run
export WAYLAND_DISPLAY=wayland-0
export QT_QPA_PLATFORM=wayland
export QT_QUICK_BACKEND=software
export QT_QPA_FONTDIR=/usr/share/fonts
export LANG=en_US.UTF-8
cd /userdata/smart_car
pkill -9 -f {APP} 2>/dev/null
sleep 3
setsid env SMARTCAR_SELFTEST_MS=600000 /userdata/smart_car/{APP} --page={PAGE} > /tmp/pf_{TAG}.log 2>&1 &
sleep {WAIT}
echo "--- pidof: $(pidof {APP}) ---"
echo "--- 应用日志尾部 ---"
tail -10 /tmp/pf_{TAG}.log
'''.format(APP=APP, PAGE=page, TAG=tag, WAIT=wait), timeout=300))

print("=== 2) 抓真屏（整屏纯色就重抓，最多 4 次）===")
sftp = cli.open_sftp()
saved = None
for attempt in range(1, 5):
    board("/tmp/kmscap /tmp/pf_%s.ppm /dev/dri/card0" % tag, timeout=120)
    vm("%s pull /tmp/pf_%s.ppm %s/ >/dev/null 2>&1" % (ADB, tag, WORK))
    raw = os.path.join(OUT, tag + ".ppm")
    try:
        sftp.get(WORK + "/pf_%s.ppm" % tag, raw)
    except Exception as ex:
        print("  第 %d 次拉取失败：%s" % (attempt, ex))
        continue
    img = Image.open(raw).convert("RGB")
    w, h = img.size
    px = img.transpose(Image.ROTATE_270)
    c = Counter(px.resize((64, 36)).getdata())
    top, cnt = c.most_common(1)[0]
    frac = cnt / float(64 * 36)
    print("  第 %d 次：framebuffer %dx%d，最常见颜色 %s 占比 %.0f%%" % (attempt, w, h, top, frac * 100))
    if frac > 0.97:
        print("  → 判为未初始化的整屏纯色（%.0f%%），3 秒后重抓" % (frac * 100))
        time.sleep(3)
        continue
    px.save(os.path.join(OUT, tag + ".png"), format="PNG")
    img.save(os.path.join(OUT, tag + "_panel.png"), format="PNG")
    saved = os.path.join(OUT, tag + ".png")
    break
sftp.close()
print("已存:", saved if saved else "!! 4 次都是纯色，请检查应用/weston 状态")
cli.close()
