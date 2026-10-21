#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
ota_build_and_publish.py —— 一条命令完成"改代码 → 板子上能装的 OTA 版本"
================================================================================
链路：
    Windows 源码  --SFTP-->  VM  ~/work/Smart_car_rk3506/
                              |  arm-buildroot-linux-gnueabihf-g++ 交叉编译
                              v
                            二进制 (2355424 B 量级)
                              |  SFTP 拉回 Windows build_out/
                              v
                     tools/ota_publish.py  打包 + 生成 manifest.json

为什么版本号要**编译进 ELF**：
    OTA 的三重取证都依赖它（文件 md5 / ELF 里的版本串 / 运行进程的 exe 的版本串）。
    如果只是把版本写进文件名，板子上就没法回答"我到底在跑哪个版本"。
    所以这里用 -DSMARTCAR_OTA_VERSION_STR=<版本> 编译，见 Smart_car/ota_version.h。

用法：
    python tools/ota_build_and_publish.py --version 1.1.0 --notes "新增系统升级页"
    python tools/ota_build_and_publish.py --version 1.1.0 --build-only
    python tools/ota_build_and_publish.py --version 1.1.0 --publish-only
    python tools/ota_build_and_publish.py --version 1.1.0 --with-firmware
"""
from __future__ import annotations

import argparse
import os
import posixpath
import sys
import time

import paramiko

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
# HERE = D:\cjh\新建文件夹\Linux车载系统\Linux车载系统\tools
#   WORKSPACE  = ...\Linux车载系统            （源码 + build_out 都在这层）
#   SRC_DIR    = ...\Linux车载系统\Smart_car
#   OUT_DIR    = ...\Linux车载系统\build_out   ← 本工程约定的构建产物目录
#                 （注意：外面还有个同名的 ...\Linux车载系统\build_out 是更早的
#                  一次构建，**不是**本工程当前使用的那个，别拉错文件）
WORKSPACE = os.path.abspath(os.path.join(HERE, ".."))
SRC_DIR = os.path.join(WORKSPACE, "Smart_car")
OUT_DIR = os.path.join(WORKSPACE, "build_out")

HOST, USER, PW = "192.168.88.130", "cjh", "181305"
VM_TREE = "/home/cjh/work/Smart_car_rk3506"
# ⚠ 专用的构建目录，**故意不与别人的 build_rk3506 共用**：
#   2026-10-12 实测踩过 —— 板上出现的应用既不是原始版也不是我编的版本，
#   根因是另一个进程在同一个增量构建目录里并发 make，产物不可预测。
#   单独一个目录后，"我编出来的就是板子上装的"这件事才站得住。
VM_BUILD = VM_TREE + "/build_rk3506_ota"
SDK = "/home/cjh/atk_dlrk3506_linux6.1_release_v1.3.1_20260326"
QMAKE = SDK + "/buildroot/output/alientek_rk3506/host/bin/qmake"
APP = "Smart_in-car_entertainment"

# 只同步这些后缀的源文件；绝不覆盖 VM 上的 Makefile / *.o（增量编译靠它们）
SRC_EXT = {".cpp", ".h", ".ui", ".qrc", ".pro", ".png", ".jpg", ".jpeg",
           ".db", ".ini", ".conf", ".txt", ".qml"}
SKIP_DIRS = {"build_vm", "build_rk3506", "build_gpstest", "build_uidiag",
             "diag", "docs", "__pycache__", ".git"}


def connect():
    cli = paramiko.SSHClient()
    cli.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    cli.connect(HOST, 22, USER, PW, timeout=30, banner_timeout=30,
                auth_timeout=30, look_for_keys=False, allow_agent=False)
    return cli


def run(cli, cmd, timeout=1800):
    cmd = cmd.replace("\r\n", "\n").replace("\r", "\n")
    _, out, err = cli.exec_command(cmd, timeout=timeout)
    o = out.read().decode("utf-8", "replace")
    e = err.read().decode("utf-8", "replace")
    rc = out.channel.recv_exit_status()
    return rc, o, e


def sync_sources(cli, verbose=False, force=False):
    """把 Windows 上的源码同步到 VM。

    ⚠ 用**内容 md5** 判断是否需要上传，不能只看文件大小：
      2026-10-12 实测踩过 —— 本地与 VM 上同名源文件大小完全相同、内容却不同
      （本地是"加过版本串/banner 的版本"，VM 上是另一条线上改过的版本），
      只比大小会全部跳过，结果编译出来的二进制里根本没有新代码。
      代价是多读一遍文件算 md5，但**不会漏同步**，这个交换是值得的。
    """
    sftp = cli.open_sftp()
    sent = skipped = 0
    changed: list[str] = []
    cand = 0
    if not os.path.isdir(SRC_DIR):
        raise SystemExit("源码目录不存在（路径算错了？）: %s" % SRC_DIR)
    try:
        run(cli, "mkdir -p %s" % VM_TREE)
        # 先给 VM 上现有源码留一份快照：万一本地树比 VM 旧，还能倒回去
        stamp = time.strftime("%Y%m%d_%H%M%S")
        run(cli, "mkdir -p ~/ota_src_backup && cd %s && "
                 "tar -czf ~/ota_src_backup/src_%s.tar.gz --exclude=build_* "
                 "--exclude=diag --exclude=docs *.cpp *.h *.ui *.qrc *.pro 2>/dev/null; "
                 "ls -l ~/ota_src_backup/src_%s.tar.gz"
             % (VM_TREE, stamp, stamp))
        print("  （VM 源码快照: ~/ota_src_backup/src_%s.tar.gz）" % stamp)

        # 远端文件 md5 表
        rc, out, _ = run(
            cli, "cd %s && find . -maxdepth 3 -type f "
                 "\\( -name '*.cpp' -o -name '*.h' -o -name '*.ui' -o -name '*.qrc' "
                 "-o -name '*.pro' -o -name '*.png' -o -name '*.db' \\) "
                 "-exec md5sum {} + 2>/dev/null" % VM_TREE)
        remote = {}
        for ln in out.splitlines():
            parts = ln.split(None, 1)
            if len(parts) == 2:
                remote[parts[1].strip().lstrip("./")] = parts[0]

        import hashlib

        for root, dirs, files in os.walk(SRC_DIR):
            dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
            for fn in files:
                ext = os.path.splitext(fn)[1].lower()
                if ext not in SRC_EXT:
                    continue
                cand += 1
                local = os.path.join(root, fn)
                rel = os.path.relpath(local, SRC_DIR).replace("\\", "/")
                h = hashlib.md5(open(local, "rb").read()).hexdigest()
                if not force and remote.get(rel) == h:
                    skipped += 1
                    continue
                rdir = posixpath.dirname(posixpath.join(VM_TREE, rel))
                run(cli, "mkdir -p '%s'" % rdir)
                sftp.put(local, posixpath.join(VM_TREE, rel))
                sent += 1
                changed.append(rel)
                if verbose:
                    print("   -> %s (%d B)" % (rel, os.path.getsize(local)))
    finally:
        sftp.close()
    print("源码同步: 上传 %d 个, 跳过(内容相同) %d 个 (候选 %d 个)"
          % (sent, skipped, cand))
    if changed and not verbose:
        print("  本次覆盖的源文件: %s%s"
              % (", ".join(changed[:12]), " …" if len(changed) > 12 else ""))
    return sent


def build(cli, version):
    """写版本头 → qmake → make。返回 (ok, 日志)"""
    ver = version.lstrip("v")
    print("=" * 74)
    print("交叉编译 版本=%s" % ver)
    print("=" * 74)

    # ---- 1) 在 VM 上生成 ota_version_local.h（编译期版本号的唯一来源）
    # 用 base64 传内容，彻底避免多层 shell 转义（见 ota_version.h 顶部注释）
    content = (
        "/* 本文件由 tools/ota_build_and_publish.py 自动生成，请勿手工修改。\n"
        " * 生成时间: %s\n"
        " * 它把发布版本号固化进 ELF，供 OTA 升级前后取证。 */\n"
        "#ifndef OTA_VERSION_LOCAL_H\n"
        "#define OTA_VERSION_LOCAL_H\n"
        "#define SMARTCAR_OTA_VERSION_STR \"v%s\"\n"
        "#endif\n" % (time.strftime("%Y-%m-%d %H:%M:%S"), ver))
    import base64
    b64 = base64.b64encode(content.encode("utf-8")).decode("ascii")
    rc, o, e = run(cli, "echo %s | base64 -d > %s/ota_version_local.h && cat %s/ota_version_local.h"
                   % (b64, VM_TREE, VM_TREE))
    print("$ 生成 ota_version_local.h  [rc=%d]" % rc)
    print(o.strip())
    if rc != 0 or ("v" + ver) not in o:
        return False, o + e

    # ---- 2) qmake + make
    cmds = [
        ("qmake -v", "%s -v" % QMAKE),
        ("qmake 生成 Makefile", "mkdir -p %s && cd %s && %s -o Makefile ../%s.pro"
         % (VM_BUILD, VM_BUILD, QMAKE, APP)),
        # ⚠ 不要写 `make ... | tail`：管道的退出码是 tail 的，编译失败也返回 0，
        #   上一版就是这么"编译失败却以为成功"的。这里让 make 自己决定 rc。
        ("make -j$(nproc)", "cd %s && make -j$(nproc) 2>&1" % VM_BUILD),
    ]
    allog = ""
    for label, c in cmds:
        t0 = time.time()
        print("\n$ %s" % label)
        rc, o, e = run(cli, c)
        dt = time.time() - t0
        txt = (o + e)
        allog += txt
        print(txt[-6000:] if len(txt) > 6000 else txt)
        print("[rc=%d, %.1fs]" % (rc, dt))
        if rc != 0:
            return False, allog

    # ---- 3) 复核版本串真的编进去了
    rc, o, e = run(cli, "grep -ao 'SMARTCAR_OTA_VERSION=[0-9A-Za-z._-]*' "
                        "%s/%s | head -1; md5sum %s/%s; ls -l %s/%s"
                   % (VM_TREE, APP, VM_TREE, APP, VM_TREE, APP))
    print("\n编译产物自检:")
    print(o.strip())
    if ("SMARTCAR_OTA_VERSION=v" + ver) not in o:
        print("!! 失败: 产物里没找到期望的版本串 SMARTCAR_OTA_VERSION=v%s" % ver)
        return False, allog
    return True, o


def pull_binary(cli):
    os.makedirs(OUT_DIR, exist_ok=True)
    dst = os.path.join(OUT_DIR, APP)
    sftp = cli.open_sftp()
    try:
        sftp.get("%s/%s" % (VM_TREE, APP), dst)
    finally:
        sftp.close()
    print("已拉回: %s (%d B)" % (dst, os.path.getsize(dst)))
    return dst


def publish(binary, version, notes, with_firmware, dry_run=False):
    sys.path.insert(0, HERE)
    import ota_publish  # noqa: E402

    class A:
        pass
    a = A()
    a.out = ota_publish.DEFAULT_OUT
    a.version = version
    a.bump = False
    a.binary = binary
    a.notes = notes
    a.extra = []
    a.with_firmware = with_firmware
    a.fw_keys = []
    a.dry_run = dry_run
    return ota_publish.cmd_publish(a)


def main():
    ap = argparse.ArgumentParser(description="构建并发布 Smart_car OTA 版本")
    ap.add_argument("--version", required=True, help="版本号，如 1.1.0")
    ap.add_argument("--notes", default="", help="发布说明")
    ap.add_argument("--with-firmware", action="store_true",
                    help="发布时带上固件镜像条目")
    ap.add_argument("--build-only", action="store_true")
    ap.add_argument("--publish-only", action="store_true",
                    help="跳过编译，直接拿 build_out 里现有二进制发布")
    ap.add_argument("--no-sync", action="store_true", help="跳过源码同步")
    ap.add_argument("--force-sync", action="store_true",
                    help="无条件把本地源码全部覆盖到 VM（忽略 md5 比对）")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()

    cli = connect()
    try:
        if not a.publish_only:
            if not a.no_sync:
                print("== 1) 同步源码 Windows -> VM ==")
                sync_sources(cli, a.verbose, a.force_sync)
            print("== 2) 交叉编译 ==")
            ok, _ = build(cli, a.version)
            if not ok:
                print("!! 编译失败，未发布。")
                return 2
        print("== 3) 取回二进制 ==")
        binary = pull_binary(cli)
    finally:
        cli.close()

    print("== 4) 打包 + 生成 manifest ==")
    return publish(binary, a.version, a.notes, a.with_firmware, a.dry_run)


if __name__ == "__main__":
    sys.exit(main())
