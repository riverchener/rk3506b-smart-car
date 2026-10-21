#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Final health check of the Smart_car deployment on the RK3506B board."""
import sys

import paramiko

HOST, USER, PW = "192.168.137.104", "root", "root"

CMDS = [
    ("board", "cat /proc/device-tree/model; echo; uname -sr; nproc; free -h | head -2"),
    ("app alive", "ps aux | grep '[S]mart_in-car' | awk '{print $2, $3\"%cpu\", $4\"%mem\", $11}'"),
    ("app uptime sample (log tail)",
     "tail -6 /var/log/smartcar.log"),
    ("module", "lsmod | grep smartcar; echo '--- device nodes ---'; ls -l /dev/mydht11 /dev/mysr04 /dev/mybuzzer"),
    ("autostart scripts",
     "ls -l /etc/init.d/S98smartcar /etc/init.d/S99appinit; echo '--- S99appinit body ---'; cat /etc/init.d/S99appinit"),
    ("deployed tree", "ls /userdata/smart_car; echo '--- media ---'; ls /userdata/smart_car/myMusic; ls /userdata/smart_car/Videos"),
    ("disk", "df -h /userdata | tail -1"),
    ("weston output", "cat /sys/class/drm/card0-DSI-1/modes"),
]

cli = paramiko.SSHClient()
cli.set_missing_host_key_policy(paramiko.AutoAddPolicy())
cli.connect(HOST, 22, USER, PW, timeout=15, look_for_keys=False, allow_agent=False)

for title, cmd in CMDS:
    _, out, err = cli.exec_command(cmd, timeout=40)
    print("=" * 68)
    print("### " + title)
    print("=" * 68)
    sys.stdout.write(out.read().decode("utf-8", "replace"))
    e = err.read().decode("utf-8", "replace")
    if e.strip():
        print("[stderr] " + e.strip())

cli.close()
