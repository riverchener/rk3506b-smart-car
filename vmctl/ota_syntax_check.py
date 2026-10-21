#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""把本地 smartcar-ota 脚本推到 VM，用 dash/sh 做语法检查。"""
import os
import sys
import paramiko

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
HERE = os.path.dirname(os.path.abspath(__file__))
LOCAL = os.path.abspath(os.path.join(HERE, "..", "Linux车载系统", "Smart_car",
                                     "board", "smartcar-ota"))
cli = paramiko.SSHClient()
cli.set_missing_host_key_policy(paramiko.AutoAddPolicy())
cli.connect("192.168.88.130", 22, "cjh", "181305", timeout=30,
            look_for_keys=False, allow_agent=False)
sftp = cli.open_sftp()
sftp.put(LOCAL, "/tmp/smartcar-ota")
sftp.close()
for sh in ("dash", "sh", "bash", "busybox ash"):
    _, out, err = cli.exec_command("%s -n /tmp/smartcar-ota; echo RC=$?" % sh, timeout=60)
    o = out.read().decode("utf-8", "replace") + err.read().decode("utf-8", "replace")
    print("[%s -n] %s" % (sh, o.strip()))
cli.close()
