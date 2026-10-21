#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
ota_publish.py —— Smart_car 车机 OTA 发布工具（PC 端）
================================================================================
把"一个构建产物"发布成一个**可被板端 smartcar-ota 消费的 OTA 版本**。

它做三件事：
  1. 打应用包：把 Smart_in-car_entertainment（可附带 sc_sensors.ko 等）打成一个
     **确定性 gzip** 的 tar.gz —— 同样的输入必然得到同样的 md5。
     这一点很重要：OTA 的版本比对依赖 md5，如果每次打包 md5 都变，
     "同一个版本"就没法复现，出了问题也没法二分定位。
  2. 生成 manifest.json：版本号 / 文件名 / 大小 / md5 / notes / 固件条目。
  3. 维护一个发布目录（默认 tools/ota_release/），把 <版本>/manifest.json +
     包 + 固件镜像摆在 HTTP 服务的根下，使 URL 结构为：

        http://<host>:<port>/ota/manifest.json      <- 板端默认指向它
        http://<host>:<port>/ota/app-<版本>.tar.gz
        http://<host>:<port>/ota/amp_mtd6_<版本>.img

用法
----
    # 发布一个应用版本（版本号自己给）
    python tools/ota_publish.py publish --version 1.1.0 \
        --binary Linux车载系统/Smart_car/build_vm/Smart_in-car_entertainment \
        --notes "新增系统升级页" --bump

    # 只看会发布什么（不写文件）
    python tools/ota_publish.py publish --version 1.1.0 --binary ... --dry-run

    # 带固件条目（默认指向 build_out/flash 下的 mtd4/mtd6 镜像）
    python tools/ota_publish.py publish --version 1.2.0 --binary ... --with-firmware

    # 看当前已发布版本
    python tools/ota_publish.py list

    # 校验一个发布目录（md5 与实际文件是否一致）
    python tools/ota_publish.py verify

注意：本工具**不碰板子**，只产出文件。板端安装由 board/smartcar-ota 负责。
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
# HERE = D:\cjh\新建文件夹\Linux车载系统\Linux车载系统\tools
#   WORKSPACE     = ...\Linux车载系统          （源码根）
#   SMART_CAR_DIR = ...\Linux车载系统\Smart_car
#   FLASH_DIR     = ...\build_out\flash        ← 固件镜像在**外面那层** build_out 下
#   （外面还有一个 ...\Linux车载系统\build_out 是更早的构建，别混用）
WORKSPACE = os.path.abspath(os.path.join(HERE, ".."))
SMART_CAR_DIR = os.path.join(WORKSPACE, "Smart_car")
OUTER_WORKSPACE = os.path.abspath(os.path.join(WORKSPACE, ".."))
FLASH_DIR = os.path.join(OUTER_WORKSPACE, "build_out", "flash")
DEFAULT_OUT = os.path.join(HERE, "ota_release")
APP_NAME = "Smart_in-car_entertainment"
DEFAULT_BINARY = os.path.join(WORKSPACE, "build_out", APP_NAME)

# 默认的固件源：build_out/flash 下这两份是 2026-09-29 烧录验证过的镜像
DEFAULT_FW = {
    "boot_mtd4": {
        "name": "CAN 内核 (mtd4)",
        "partition": "mtd4",
        "path": os.path.join(FLASH_DIR, "can_boot_zboot_20260929.img"),
        "size": 6153728,
        "md5": "72640b6afbcda169b0373a63267b209b",
    },
    "amp_mtd6": {
        "name": "M0/AMP 固件 (mtd6)",
        "partition": "mtd6",
        "path": os.path.join(FLASH_DIR, "can_amp_m0_20260929.img"),
        "size": 33280,
        "md5": "41ec6a3993b0fda1a9c2ad5225048c4a",
    },
}


# --------------------------------------------------------------------- 工具
def md5_file(path: str, chunk: int = 1 << 20) -> str:
    h = hashlib.md5()
    with open(path, "rb") as fh:
        while True:
            b = fh.read(chunk)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def read_version(path: str) -> str | None:
    """从 ELF 里抠出 SMARTCAR_OTA_VERSION=xxx（板端用同一套规则）。"""
    if not os.path.isfile(path):
        return None
    needle = b"SMARTCAR_OTA_VERSION="
    with open(path, "rb") as fh:
        data = fh.read()
    i = data.find(needle)
    if i < 0:
        return None
    j = i + len(needle)
    k = j
    while k < len(data) and (data[k:k + 1].isalnum() or data[k:k + 1] in b"._-"):
        k += 1
    return data[j:k].decode("ascii", "replace") or None


def parse_ver(v: str) -> tuple:
    parts = []
    for p in v.strip().lstrip("v").split("."):
        try:
            parts.append(int(p))
        except ValueError:
            parts.append(0)
    while len(parts) < 4:
        parts.append(0)
    return tuple(parts[:4])


def bump_patch(v: str) -> str:
    p = list(parse_ver(v))
    p[2] += 1
    return "%d.%d.%d" % (p[0], p[1], p[2])


def human(n: int) -> str:
    return "%.2f MB" % (n / 1048576.0) if n >= 1048576 else "%d B" % n


# --------------------------------------------------------------------- 打包
def make_app_tarball(binary: str, out_path: str, version: str,
                     extra_files: list[str] | None = None) -> dict:
    """把应用二进制（+可选附加文件）打成确定性 tar.gz。

    确定性 = gzip mtime 固定 + tar 里每个成员的 mtime/uid/gid/uname 固定。
    这样"同样的输入 -> 同样的 md5"，版本号与内容才能真正一一对应。
    """
    members: list[tuple[str, str]] = [(binary, APP_NAME)]
    for f in (extra_files or []):
        members.append((f, os.path.basename(f)))

    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w") as tar:
        for src, arcname in members:
            if not os.path.isfile(src):
                raise SystemExit("附件不存在: %s" % src)
            ti = tarfile.TarInfo(arcname)
            st = os.stat(src)
            ti.size = st.st_size
            ti.mtime = 0
            ti.mode = 0o755
            ti.uid = ti.gid = 0
            ti.uname = ti.gname = "root"
            with open(src, "rb") as fh:
                tar.addfile(ti, fh)
    raw = buf.getvalue()
    # gzip 头里的 mtime 置 0，否则每次打包 md5 都不同
    with open(out_path, "wb") as fh:
        with gzip.GzipFile(filename="", mode="wb", fileobj=fh, mtime=0) as gz:
            gz.write(raw)
    return {
        "file": os.path.basename(out_path),
        "size": os.path.getsize(out_path),
        "md5": md5_file(out_path),
        "members": [m[1] for m in members],
    }


def build_firmware_entries(keys: list[str]) -> list[dict]:
    entries = []
    for k in keys:
        spec = DEFAULT_FW.get(k)
        if not spec:
            raise SystemExit("未知固件条目 %s（可选：%s）"
                             % (k, ", ".join(DEFAULT_FW)))
        p = spec["path"]
        if not os.path.isfile(p):
            raise SystemExit("固件镜像不存在: %s" % p)
        real_md5 = md5_file(p)
        real_size = os.path.getsize(p)
        if real_md5 != spec["md5"]:
            print("!! 警告: %s 的 md5 与记录值不符\n   记录 %s\n   实际 %s"
                  % (p, spec["md5"], real_md5))
        entries.append({
            "name": spec["name"],
            "partition": spec["partition"],
            "image": os.path.basename(p),
            "size": real_size,
            "md5": real_md5,
        })
    return entries


# --------------------------------------------------------------------- 发布
def cmd_publish(a) -> int:
    out_dir = os.path.abspath(a.out)
    ota_dir = os.path.join(out_dir, "ota")
    os.makedirs(ota_dir, exist_ok=True)

    version = a.version.lstrip("v")
    if a.bump:
        # 自动 +1 patch：方便连续做两次端到端验证（v1.0.1 -> v1.0.2）
        existing = read_version(a.binary) or "1.0.0"
        version = bump_patch(existing)
        print("--bump: %s -> %s" % (existing, version))

    if not os.path.isfile(a.binary):
        raise SystemExit("二进制不存在: %s" % a.binary)
    bin_md5 = md5_file(a.binary)
    bin_size = os.path.getsize(a.binary)
    bin_ver_raw = read_version(a.binary)
    # ELF 里写的是 "v1.1.0"，manifest 的版本号是 "1.1.0"：比之前先归一化掉前缀 v
    bin_ver = bin_ver_raw.lstrip("v") if bin_ver_raw else None

    print("=" * 74)
    print("OTA 发布")
    print("=" * 74)
    print("  源二进制   : %s" % a.binary)
    print("    大小     : %d B (%s)" % (bin_size, human(bin_size)))
    print("    md5      : %s" % bin_md5)
    print("    ELF 版本 : %s" % (bin_ver_raw or "**未找到 SMARTCAR_OTA_VERSION= 串**"))
    print("  发布版本号 : %s" % version)
    if bin_ver and bin_ver != version:
        print("  ⚠ 注意: 二进制里的版本串(v%s) 与发布版本号(%s) 不一致。" % (bin_ver, version))
        print("     板端会把 ELF 里的串当作实际版本，建议用 --version %s 重编。" % bin_ver)
    elif not bin_ver:
        print("  ⚠ 注意: 二进制里没有版本串！板端将无法用版本号判断升级，")
        print("     只能退化成 md5 比较。请确认编译时包含了 ota_version.h。")

    tarball = os.path.join(ota_dir, "app-%s.tar.gz" % version)
    if a.dry_run:
        print("  [dry-run] 将生成 %s" % tarball)
        info = {"file": os.path.basename(tarball), "size": -1, "md5": "(dry-run)"}
    else:
        info = make_app_tarball(a.binary, tarball, version, a.extra)
        print("  应用包     : %s" % tarball)
        print("    大小     : %d B (%s)" % (info["size"], human(info["size"])))
        print("    md5      : %s" % info["md5"])
        print("    包含     : %s" % ", ".join(info["members"]))
        # 压缩率参考（板子只有 240MB /userdata，包越小越安全）
        print("    压缩率   : %.1f%%" % (100.0 * info["size"] / max(bin_size, 1)))

    firmware = []
    if a.with_firmware:
        fw = build_firmware_entries(a.fw_keys or list(DEFAULT_FW))
        for e in fw:
            src = os.path.join(FLASH_DIR, e["image"])
            dst = os.path.join(ota_dir, e["image"])
            if not a.dry_run:
                shutil.copy2(src, dst)
            print("  固件镜像   : %s (%s) md5=%s"
                  % (e["image"], human(e["size"]), e["md5"]))
            firmware.append(e)

    manifest = {
        "schema": 1,
        "product": "Smart_car RK3506B 车机",
        "version": version,
        "released_at": time.strftime("%Y-%m-%d %H:%M:%S"),
        "notes": a.notes or "",
        "app_file": info["file"],
        "app_size": info["size"],
        "app_md5": info["md5"],
        "app_elf_version": bin_ver or "",
        "app_source_md5": bin_md5,
        "firmware": firmware,
    }
    mpath = os.path.join(ota_dir, "manifest.json")
    if a.dry_run:
        print("\n[dry-run] manifest 内容：")
        print(json.dumps(manifest, ensure_ascii=False, indent=2))
        return 0

    with open(mpath, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(manifest, fh, ensure_ascii=False, indent=2)
        fh.write("\n")
    with open(os.path.join(ota_dir, "..", "VERSION"), "w",
              encoding="utf-8", newline="\n") as fh:
        fh.write(version + "\n")

    print("\n  manifest   : %s" % mpath)
    print("  发布目录   : %s" % out_dir)
    print("""
下一步（二选一）：

  A) 真机 OTA（推荐，本次验证用的就是这条）：
       powershell -File tools\\ota_server.ps1 -Port 18080
       # 板子上：
       smartcar-ota check
       smartcar-ota update

  B) 离线升级（不联网）：
       adb push %s /userdata/ota/pkg/ -r
       smartcar-ota local /userdata/ota/pkg
""" % ota_dir)
    return 0


def cmd_list(a) -> int:
    ota_dir = os.path.join(os.path.abspath(a.out), "ota")
    mpath = os.path.join(ota_dir, "manifest.json")
    if not os.path.isfile(mpath):
        print("还没有发布过（%s 不存在）" % mpath)
        return 1
    with open(mpath, encoding="utf-8") as fh:
        m = json.load(fh)
    print("当前发布目录: %s" % ota_dir)
    print("  版本      : %s" % m["version"])
    print("  时间      : %s" % m["released_at"])
    print("  说明      : %s" % m.get("notes", ""))
    print("  应用包    : %s  %d B  md5=%s"
          % (m["app_file"], m["app_size"], m["app_md5"]))
    for e in m.get("firmware", []):
        print("  固件      : %s -> %s  %d B  md5=%s"
              % (e["image"], e["partition"], e["size"], e["md5"]))
    for f in sorted(os.listdir(ota_dir)):
        p = os.path.join(ota_dir, f)
        print("    %-34s %10d B" % (f, os.path.getsize(p)))
    return 0


def cmd_verify(a) -> int:
    ota_dir = os.path.join(os.path.abspath(a.out), "ota")
    mpath = os.path.join(ota_dir, "manifest.json")
    if not os.path.isfile(mpath):
        print("manifest 不存在: %s" % mpath)
        return 1
    with open(mpath, encoding="utf-8") as fh:
        m = json.load(fh)
    bad = 0
    for label, fn, size, expect in (
            ("应用包", m["app_file"], m["app_size"], m["app_md5"]),):
        p = os.path.join(ota_dir, fn)
        if not os.path.isfile(p):
            print("✘ %s 缺失: %s" % (label, p)); bad += 1; continue
        got = md5_file(p)
        gs = os.path.getsize(p)
        ok = (got == expect and gs == size)
        print("%s %s  size=%d(期望%d)  md5=%s(期望%s)"
              % ("✔" if ok else "✘", fn, gs, size, got, expect))
        bad += 0 if ok else 1
    for e in m.get("firmware", []):
        p = os.path.join(ota_dir, e["image"])
        if not os.path.isfile(p):
            print("✘ 固件缺失: %s" % p); bad += 1; continue
        got = md5_file(p)
        ok = (got == e["md5"] and os.path.getsize(p) == e["size"])
        print("%s %s  size=%d(期望%d)  md5=%s(期望%s)"
              % ("✔" if ok else "✘", e["image"], os.path.getsize(p),
                 e["size"], got, e["md5"]))
        bad += 0 if ok else 1
    print("结论: %s" % ("全部一致" if bad == 0 else "%d 项不一致" % bad))
    return 0 if bad == 0 else 1


def cmd_info(a) -> int:
    p = a.binary
    if not os.path.isfile(p):
        raise SystemExit("文件不存在: %s" % p)
    print("文件   : %s" % p)
    print("大小   : %d B (%s)" % (os.path.getsize(p), human(os.path.getsize(p))))
    print("md5    : %s" % md5_file(p))
    print("ELF版本: %s" % (read_version(p) or "(无)"))
    try:
        out = subprocess.run(["file", p], capture_output=True, text=True)
        print("file   : %s" % out.stdout.strip())
    except FileNotFoundError:
        pass
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Smart_car 车机 OTA 发布工具",
        formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument("--out", default=DEFAULT_OUT,
                    help="发布目录（默认 tools/ota_release）")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("publish", help="发布一个版本")
    p.add_argument("--version", default="1.0.0", help="版本号，如 1.1.0")
    p.add_argument("--bump", action="store_true",
                   help="忽略 --version，按二进制里的版本串 patch+1")
    p.add_argument("--binary", required=True, help="应用二进制路径")
    p.add_argument("--notes", default="", help="发布说明（会进 manifest）")
    p.add_argument("--extra", action="append", default=[],
                   help="附加文件（如 sc_sensors.ko），可多次")
    p.add_argument("--with-firmware", action="store_true",
                   help="同时带上默认固件镜像（mtd4/mtd6）")
    p.add_argument("--fw", action="append", dest="fw_keys", default=[],
                   help="指定固件条目 key：boot_mtd4 / amp_mtd6")
    p.add_argument("--dry-run", action="store_true", help="只打印不写文件")
    p.set_defaults(func=cmd_publish)

    l = sub.add_parser("list", help="看当前发布内容")
    l.set_defaults(func=cmd_list)

    v = sub.add_parser("verify", help="校验发布目录里 md5 与文件是否一致")
    v.set_defaults(func=cmd_verify)

    i = sub.add_parser("info", help="看某个二进制的版本/md5")
    i.add_argument("binary")
    i.set_defaults(func=cmd_info)

    a = ap.parse_args()
    return a.func(a)


if __name__ == "__main__":
    sys.exit(main())
