#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Deploy the rebuilt app: VM -> Windows -> board, then restart and verify."""
import os
import stat
import time

import paramiko

VM = ("192.168.88.130", "cjh", "181305")
BRD = ("192.168.137.104", "root", "root")
SRC = "/home/cjh/work/Smart_car_rk3506/Smart_in-car_entertainment"
TMP = r"D:\cjh\新建文件夹\Linux车载系统\build_out\Smart_in-car_entertainment"
DST = "/userdata/smart_car/Smart_in-car_entertainment"


def conn(host, user, pw):
    c = paramiko.SSHClient()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    c.connect(host, 22, user, pw, timeout=20, look_for_keys=False, allow_agent=False)
    return c


print("=== 1) pull the binary from the VM ===")
vm = conn(*VM)
sftp = vm.open_sftp()
os.makedirs(os.path.dirname(TMP), exist_ok=True)
sftp.get(SRC, TMP)
st = os.stat(TMP)
print("  %s  (%d bytes)" % (TMP, st.st_size))
sftp.close()
vm.close()

print("\n=== 2) stop the app on the board ===")
brd = conn(*BRD)
_, out, _ = brd.exec_command("/etc/init.d/S98smartcar stop 2>&1 | tail -2; sleep 2; "
                             "ps -eo pid,args | grep '[S]mart_in-car' || echo 'app stopped'")
print(out.read().decode("utf-8", "replace").strip())

print("\n=== 3) back up the old binary and upload the new one ===")
b = brd.open_sftp()
try:
    b.stat(DST + ".bak")
    print("  backup already exists, keeping it")
except IOError:
    try:
        b.stat(DST)
        b.rename(DST, DST + ".bak")
        print("  old binary -> %s.bak" % DST)
    except IOError:
        print("  (no existing binary)")
b.put(TMP, DST + ".new")
try:
    b.remove(DST)          # rename() refuses to clobber
except IOError:
    pass
b.rename(DST + ".new", DST)
b.chmod(DST, 0o755)
print("  uploaded, mode set")
b.close()

_, o, _ = brd.exec_command("ls -l %s* 2>&1; md5sum %s %s.bak" % (DST, DST, DST))
print(o.read().decode("utf-8", "replace").strip())

print("\n=== 4) local md5 for comparison ===")
import hashlib
with open(TMP, "rb") as f:
    print("  windows md5:", hashlib.md5(f.read()).hexdigest())

print("\n=== 5) restart the app ===")
_, o, _ = brd.exec_command("/etc/init.d/S98smartcar start 2>&1 | tail -3; sleep 15; "
                          "pgrep -af '[S]mart_in-car' | head -2")
print(o.read().decode("utf-8", "replace").strip())

print("\n=== 6) verify the fix is live in the running binary path ===")
_, o, _ = brd.exec_command("ls -l /userdata/smart_car/Smart_in-car_entertainment; "
                          "echo '--- app log tail ---'; "
                          "tail -12 /userdata/smart_car/smartcar.log 2>/dev/null || "
                          "tail -12 /var/log/smartcar.log 2>/dev/null || echo '(no app log)'")
print(o.read().decode("utf-8", "replace").strip())

brd.close()
print("\nDONE")
