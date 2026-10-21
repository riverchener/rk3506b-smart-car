#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Probe SSH access to the RK3506B board and, if it works, run verification."""
import sys

import paramiko

HOST = "192.168.137.104"
USERS = ["root", "cjh", "ubuntu"]
PASSWORDS = ["root", "181305", "", "123456", "toor", "alientek", "orangepi"]

CHECK = (
    "echo '### identity'; uname -a; cat /proc/device-tree/model 2>/dev/null; echo; "
    "echo '### display'; ls /sys/class/drm | grep DSI; "
    "echo '### weston log rotation'; grep -iE 'transform|rotate' /var/log/weston.log 2>/dev/null | tail -5; "
    "echo '### processes'; ps aux 2>/dev/null | grep -E '[w]eston|[S]mart_in-car|[s]ystemui'; "
    "echo '### app log'; tail -20 /var/log/smartcar.log 2>/dev/null; "
    "echo '### init scripts'; ls -la /etc/init.d/S98smartcar /etc/init.d/S99appinit 2>/dev/null; "
    "echo '### module'; lsmod 2>/dev/null | grep -E 'smartcar|sc_sensors'; "
    "echo '### files'; ls /userdata/smart_car 2>/dev/null"
)


def try_login(user, pw):
    cli = paramiko.SSHClient()
    cli.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    try:
        cli.connect(
            HOST, 22, user, pw, timeout=10,
            banner_timeout=15, auth_timeout=15,
            look_for_keys=False, allow_agent=False,
        )
        return cli
    except Exception:
        try:
            cli.close()
        except Exception:
            pass
        return None


def main():
    cli = None
    used = None
    for u in USERS:
        for p in PASSWORDS:
            c = try_login(u, p)
            if c:
                cli, used = c, (u, p)
                break
        if cli:
            break

    if not cli:
        print("NO_SSH_CREDENTIAL_WORKED")
        return 1

    print("SSH OK as %s (password %r)" % used)
    _, out, err = cli.exec_command(CHECK, timeout=60)
    sys.stdout.write(out.read().decode("utf-8", "replace"))
    e = err.read().decode("utf-8", "replace")
    if e.strip():
        sys.stderr.write("\n[stderr]\n" + e)
    cli.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
