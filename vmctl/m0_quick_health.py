#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""只读体检：本次开机 M0 是否起来过（rpmsg 设备 / -512 / mysr04 / 运行期自增 extra）。"""
import sys
import paramiko

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
cli = paramiko.SSHClient()
cli.set_missing_host_key_policy(paramiko.AutoAddPolicy())
cli.connect("192.168.88.130", 22, "cjh", "181305", timeout=30,
            look_for_keys=False, allow_agent=False)
ADB = "~/tools/platform-tools/adb"


def board(script, timeout=600):
    cmd = ADB + " shell sh -s <<'EOS'\n" + script + "\nEOS\n"
    _, out, err = cli.exec_command(cmd + " </dev/null", timeout=timeout)
    return (out.read().decode("utf-8", "replace") + err.read().decode("utf-8", "replace")).strip()


print("=== 1) 本次开机时长（判断是否刚冷启动过）===")
print(board("uptime; cat /proc/uptime"))

print("=== 2) rpmsg 设备是否出现（出现= M0 起来过并 announce 过）===")
print(board("ls /sys/bus/rpmsg/devices/ 2>&1; echo ---; for d in /sys/bus/rpmsg/devices/*; do echo -n \"$d name=\"; cat $d/name 2>/dev/null; done"))

print("=== 3) dmesg：M0 服务订阅行 / -512 / tx buffer 超时（按时间排序）===")
print(board("dmesg | grep -aiE 'M0 AMP|rpmsg-smartcar|tx buffer|512|sc_sensors' | tail -14"))

print("=== 4) 探针：magic / step / extra（只有 extra 运行期自增）===")
print(board("echo -n 'magic(0x03e0639c)='; devmem 0x03e0639c; "
            "echo -n 'step (0x03e063a0)='; devmem 0x03e063a0; "
            "echo -n 'extra(0x03e063a4)='; devmem 0x03e063a4; sleep 3; "
            "echo -n 'extra 3 秒后     ='; devmem 0x03e063a4"))

print("=== 5) /dev/mysr04 直接读（3 次）===")
print(board("for i in 1 2 3; do echo -n \"read$i: \"; dd if=/dev/mysr04 bs=4 count=1 2>&1 | od -An -td4; sleep 0.5; done"))

print("=== 6) 驱动最近是否有 SR04 事件/超时日志 ===")
print(board("dmesg | grep -aiE 'sr04|dht11' | tail -8"))

print("=== 7) 应用与页面状态 ===")
print(board("ps -o pid,etime,args -e 2>/dev/null | grep -i Smart_in-car | grep -v grep; "
            "grep -aE 'sr04|SR04|超声波|距离' /var/log/smartcar.log 2>/dev/null | tail -6"))
cli.close()
