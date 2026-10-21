#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
ota_server.py —— OTA 静态 HTTP 服务 + adb reverse 隧道（在 VM 上操作）
================================================================================
为什么需要隧道：
    板子 eth0 在 192.168.137.0/24（Windows ICS 网段），rootfs 里也没有任何指向
    VM(192.168.88.130) 的路由，"板子直接访问 VM 的 IP"是不通的。
    用 adb 自带的隧道不需要改任何网络配置：

        adb reverse tcp:18080 tcp:18080

    板子上的 adbd 于是在 127.0.0.1:18080 监听，连接经 USB 回到 VM 的 18080。
    实测（2026-10-12）：板端 `wget http://127.0.0.1:18080/ota/manifest.json`
    返回 821 B 正确内容。

踩过的坑（都写在这里，免得下次再踩）：
  1. `nohup python3 -m http.server &` 在 SSH 非交互会话里**会被收掉**，
     SSH 一断服务就没了。必须 `setsid ... < /dev/null &` 脱离会话。
  2. VM 上**没有 curl**（只有 wget），健康检查一律用 wget。
  3. 千万不要在内联命令里写 `$?` / `$(...)`：它会依次穿过
     PowerShell -> python -> ssh -> sh，实测被吃掉，回显成 "rc=True" 这种鬼东西。
     要判断成功与否，就看"文件字节数/内容对不对"。
  4. `pkill -f 'http.server'` 会匹配到**自己这条 bash -c 命令行**，
     于是把自己的会话杀掉、输出全丢（本次"== 2) 起 HTTP 服务 =="后一片空白就是这个）。
     正确做法：pgrep -f 拿 PID，再从命令行里排除掉自己 (`grep -v $$`)。

用法：
    python vmctl/ota_server.py start  [--port 18080]
    python vmctl/ota_server.py stop   [--port 18080]
    python vmctl/ota_server.py status [--port 18080]
"""
import argparse
import os
import sys

import paramiko

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
WORKSPACE = os.path.abspath(os.path.join(HERE, "..", "Linux车载系统"))
RELEASE = os.path.join(WORKSPACE, "tools", "ota_release")
ADB = "~/tools/platform-tools/adb"
VM_DIR = "/home/cjh/otasrv"


def conn():
    cli = paramiko.SSHClient()
    cli.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    cli.connect("192.168.88.130", 22, "cjh", "181305", timeout=30,
                look_for_keys=False, allow_agent=False)
    return cli


def run(cli, cmd, timeout=900):
    cmd = cmd.replace("\r\n", "\n").replace("\r", "\n")
    _, out, err = cli.exec_command(cmd, timeout=timeout)
    o = out.read().decode("utf-8", "replace")
    e = err.read().decode("utf-8", "replace")
    rc = out.channel.recv_exit_status()
    return o + e, rc


def adb(cli, cmd, timeout=900):
    return run(cli, "%s %s </dev/null 2>&1" % (ADB, cmd), timeout)[0]


def kill_server(cli, port):
    """按 PID 精确杀掉 http.server，不会误伤自己这条 ssh 命令。"""
    return run(cli, "for p in $(pgrep -f 'http.server %d'); do "
                    "  [ \"$p\" = \"$$\" ] && continue; "
                    "  kill \"$p\" 2>/dev/null; done; sleep 1; echo killed"
               % port)[0]


def server_up(cli, port):
    """起服务。用 nohup ... & 会被 SSH 收掉，必须 setsid。"""
    script = (
        "cd %s || exit 1\n"
        "setsid python3 -m http.server %d --bind 0.0.0.0 --directory %s "
        "> /tmp/otasrv.log 2>&1 < /dev/null &\n"
        "sleep 3\n"
        "echo '--- pgrep ---'\n"
        "pgrep -f 'http.server %d' | while read -r p; do "
        "  tr '\\0' ' ' < /proc/$p/cmdline; echo; done\n"
        "echo '--- listen ---'\n"
        "ss -ltn 2>/dev/null | grep ':%d' || echo '!! 没在监听'\n"
        "echo '--- VM 本地取 manifest ---'\n"
        "wget -q -O /tmp/_m.json http://127.0.0.1:%d/ota/manifest.json\n"
        "wc -c /tmp/_m.json\n"
        "head -4 /tmp/_m.json\n"
        "echo '--- server log ---'\n"
        "cat /tmp/otasrv.log\n" % (VM_DIR, port, VM_DIR, port, port, port))
    return run(cli, script)[0]


def upload_release(cli, sftp):
    ota = os.path.join(RELEASE, "ota")
    if not os.path.isdir(ota):
        raise SystemExit("发布目录不存在: %s（先跑 tools/ota_publish.py）" % ota)
    run(cli, "rm -rf %s && mkdir -p %s/ota" % (VM_DIR, VM_DIR))
    n = 0
    for fn in sorted(os.listdir(ota)):
        lp = os.path.join(ota, fn)
        if os.path.isfile(lp):
            sftp.put(lp, "%s/ota/%s" % (VM_DIR, fn))
            print("  上传 %s (%d B)" % (fn, os.path.getsize(lp)))
            n += 1
    vp = os.path.join(RELEASE, "VERSION")
    if os.path.isfile(vp):
        sftp.put(vp, "%s/VERSION" % VM_DIR)
    print("  共 %d 个文件" % n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("action", choices=["start", "stop", "status"])
    ap.add_argument("--port", type=int, default=18080)
    a = ap.parse_args()
    cli = conn()
    try:
        if a.action == "stop":
            print(kill_server(cli, a.port))
            print(adb(cli, "reverse --remove tcp:%d" % a.port))
            return 0

        if a.action == "status":
            print("--- VM http.server ---")
            print(run(cli, "pgrep -f 'http.server' | while read -r p; do "
                           "tr '\\0' ' ' < /proc/$p/cmdline; echo; done "
                           "|| echo '(none)'")[0])
            print("--- listen ---")
            print(run(cli, "ss -ltn 2>/dev/null | grep ':%d' || echo '(none)'"
                      % a.port)[0])
            print("--- adb reverse ---")
            print(adb(cli, "reverse --list"))
            print("--- 板端 wget 自测（看字节数是不是 821 左右）---")
            print(adb(cli, "shell 'wget -q -O /tmp/_m.json "
                           "http://127.0.0.1:%d/ota/manifest.json; "
                           "wc -c /tmp/_m.json; head -3 /tmp/_m.json'" % a.port))
            return 0

        print("== 1) 上传发布目录到 VM ==")
        sftp = cli.open_sftp()
        try:
            upload_release(cli, sftp)
        finally:
            sftp.close()
        print(run(cli, "ls -l %s/ota/" % VM_DIR)[0])

        print("== 2) 停旧服务 ==")
        print(kill_server(cli, a.port))

        print("== 3) 起 HTTP 服务 (端口 %d) ==" % a.port)
        print(server_up(cli, a.port))

        print("== 4) adb reverse 隧道 ==")
        print(adb(cli, "reverse tcp:%d tcp:%d" % (a.port, a.port))
              or "(no output = ok)")
        print(adb(cli, "reverse --list"))

        print("== 5) 板端自测（板子 -> VM 拉 manifest）==")
        print(adb(cli, "shell 'wget -q -O /tmp/_m.json "
                       "http://127.0.0.1:%d/ota/manifest.json; "
                       "wc -c /tmp/_m.json; head -3 /tmp/_m.json'" % a.port))
        return 0
    finally:
        cli.close()


if __name__ == "__main__":
    sys.exit(main())
