#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Board health check after the priority fix."""
import paramiko

HOST, USER, PW = "192.168.137.104", "root", "root"

cli = paramiko.SSHClient()
cli.set_missing_host_key_policy(paramiko.AutoAddPolicy())
cli.connect(HOST, 22, USER, PW, timeout=15, look_for_keys=False, allow_agent=False)


def sh(cmd, timeout=90):
    _, out, err = cli.exec_command(cmd, timeout=timeout)
    return (out.read().decode("utf-8", "replace"),
            err.read().decode("utf-8", "replace"))


print("=== board uptime ===")
o, _ = sh("cat /proc/uptime; uptime")
print(o.strip())

print("\n=== app process ===")
o, _ = sh("ps aux | grep '[S]mart_in-car' | awk '{print $2, $3\"%cpu\", $4\"%mem\", $11}'")
print(o.strip() or "*** APP NOT RUNNING ***")

print("\n=== deployed conf CITYID line ===")
o, _ = sh("grep -n 'SMARTCAR_CITYID' /userdata/smart_car/smartcar.conf")
print(o.strip())

print("\n=== current app log weather/gps lines ===")
o, _ = sh("grep -aE 'weather:|gps:' /var/log/smartcar.log | tail -10")
print(o.strip() or "(weather page not opened in this run yet)")

print("\n=== dmesg: any segfault / oops? ===")
o, _ = sh("dmesg | grep -iE 'segfault|smart_in|oom|killed' | tail -8")
print(o.strip() or "(clean)")

print("\n=== start the app if it is down ===")
o, _ = sh("pidof Smart_in-car_entertainment >/dev/null && echo 'already running' || "
          "{ /etc/init.d/S98smartcar start; echo started; }")
print(o.strip())

cli.close()
