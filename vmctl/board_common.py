#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""board_common —— 板端文件清点/恢复两个脚本（board_inventory.py / board_restore.py）的共用库。

设计约定（都是本项目踩过的坑，别改）：

  1) 所有板端命令**先 base64 再包成一段 sh 脚本**，用 `adb shell "echo <b64> | base64 -d | sh"`
     执行。绝不把板端命令拼进 PowerShell 内联字符串 —— 那里的 `|` 会被 PowerShell 吃掉。
  2) adb 一律带 `</dev/null`，否则 adb 会去抢 stdin。
  3) 板端文件名**可能带空格**（曲库就是：「等你下课 (with 杨瑞代) - 周杰伦.mp3」）。
     所有 `$f` / `$P` 一律加双引号，md5sum / wc -c 都走引号变量。
  4) **VM 侧命令不要用管道**：本环境下 `paramiko.exec_command("ls | head")` 会静默返回空串
     （实测：带 `|` 的 VM 命令输出恒为空）。VM 命令只用 `;` 串接或重定向。
     板端命令在板子的 sh 里跑，管道是安全的。
  5) 板子只读：本库只生成 `ls/readlink/md5sum/wc/grep/pgrep/ip/lsmod` 这类命令。
"""
from __future__ import annotations

import base64
import fnmatch
import hashlib
import json
import os
import re
import sys

import paramiko

# --------------------------------------------------------------------------- 常量
VM_HOST = "192.168.88.130"
VM_USER = "cjh"
VM_PASSWORD = "181305"
ADB = "/home/cjh/tools/platform-tools/adb"

# ------------------------------------------------- 板端产物路径铁律（硬规则 ③）
#
# 板端的**截图 / 抓帧 / 日志 / 临时产物**一律写 `/userdata/...`；
# **严禁**写 `/home`、`/root`、`/etc`（以及其它 rootfs 目录）。
# 起因：2026-10-13 一次抓屏调试把两块 .ppm 写进 `/home/cjh/wsflash/shots/`（4.73 MiB，在 rootfs 上），
# 把 rootfs 可用量从 1.1 MB 顶到 72 KB；rootfs 只有 150 MB 且常年 99% 满，写满会让 `/etc`
# 写入失败甚至文件被截断（历史事故：libltdl.so.7.3.1 被截成 0 字节）⇒ 重启可能起不来。
# 规则原文：Linux车载系统/Smart_car/docs/板端文件清单与恢复.md「硬规则 ③」。
# 策略与实现：vmctl/board_paths.py；全仓守门人：vmctl/check_board_artifact_paths.py。
#
#: 板端取证/调试产物的推荐落点（沿用仓库既有约定，别新造目录）。
BOARD_ARTIFACT_DIR = "/userdata/wsflash"


def assert_board_artifact(path, allow_tmpfs=False):
    """板端产物路径护栏：不在 `/userdata` 下就**抛异常**（不静默改写到别处）。

    实现与理由都在 `vmctl/board_paths.py`；这里只是让用 `board_common` 的人一眼能找到。
    改完板端脚本请跑：`python vmctl/check_board_artifact_paths.py`（期望 `verdict: PASS`）。
    """
    _here = os.path.dirname(os.path.abspath(__file__))
    if _here not in sys.path:
        sys.path.insert(0, _here)
    from board_paths import assert_board_artifact as _assert
    return _assert(path, side="board", allow_tmpfs=allow_tmpfs)


def board_artifact(name, sub="wsflash"):
    """拼一个合规的板端产物路径 `/userdata/<sub>/<name>`（见 `vmctl/board_paths.py`）。"""
    _here = os.path.dirname(os.path.abspath(__file__))
    if _here not in sys.path:
        sys.path.insert(0, _here)
    from board_paths import board_artifact as _make
    return _make(name, sub=sub)

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(_THIS_DIR)                       # ...\Linux车载系统
SMART_CAR_DIR = os.path.join(REPO_ROOT, "Linux车载系统", "Smart_car")
DEFAULT_MANIFEST = os.path.join(SMART_CAR_DIR, "board", "expected_files.json")

EXIT_OK, EXIT_CRITICAL, EXIT_MINOR, EXIT_TOOL = 0, 1, 2, 3


# --------------------------------------------------------------------------- 连接
class Board(object):
    """一条 SSH 到 VM 的连接；VM 上再用 adb 操作板子。"""

    kind = "vm-ssh"

    def __init__(self, host=VM_HOST, user=VM_USER, password=VM_PASSWORD, adb=ADB, timeout=25):
        # ⚠ 不能叫 self.adb —— 会盖掉下面的 adb() 方法（实测 TypeError: 'str' object is not callable）
        self.host, self.user, self.password, self.adb_bin = host, user, password, adb
        self.cli = paramiko.SSHClient()
        self.cli.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        self.cli.connect(host, 22, user, password, timeout=timeout,
                         look_for_keys=False, allow_agent=False)
        self._sftp = None

    # -- VM 侧（禁止管道！）--------------------------------------------------
    def vm(self, cmd, timeout=300):
        _, out, err = self.cli.exec_command(cmd + " </dev/null", timeout=timeout)
        return (out.read().decode("utf-8", "replace") + err.read().decode("utf-8", "replace"))

    def sftp(self):
        if self._sftp is None:
            self._sftp = self.cli.open_sftp()
        return self._sftp

    # -- 板端 ---------------------------------------------------------------
    def board(self, script, timeout=600):
        """把一段 sh 脚本送到板子上执行（base64，避免任何引号/管道问题）。"""
        b64 = base64.b64encode(script.encode("utf-8")).decode("ascii")
        cmd = ('timeout %d %s shell "echo %s | base64 -d | sh" </dev/null 2>&1'
               % (timeout, self.adb_bin, b64))
        _, out, err = self.cli.exec_command(cmd, timeout=timeout + 30)
        return (out.read().decode("utf-8", "replace") + err.read().decode("utf-8", "replace"))

    def adb(self, args, timeout=600):
        _, out, err = self.cli.exec_command("%s %s </dev/null 2>&1" % (self.adb_bin, args),
                                            timeout=timeout)
        return (out.read().decode("utf-8", "replace") + err.read().decode("utf-8", "replace"))

    def close(self):
        try:
            if self._sftp is not None:
                self._sftp.close()
        except Exception:
            pass
        try:
            self.cli.close()
        except Exception:
            pass


# ------------------------------------------------- 本机直连 adb（VM 不通时的补救）
class LocalBoard(object):
    """和 Board.board() 同形状的板端执行器，但走**本机** adb，不经 VM。

    背景（20261013）：VM 192.168.88.130 中途彻底不可达，而本机
    `D:\\platform-tools-latest-windows\\platform-tools\\adb.exe` 能直接看到
    板子的 USB 直连（`adb devices` -> `35549d1ed33ac33`）。
    约束与 Board 完全一致：base64 + sh，只读。
    """

    kind = "local-adb"

    def __init__(self, adb=None, serial=None, timeout=25):
        import subprocess  # 局部导入，避免无谓依赖
        self._sp = subprocess
        self.adb = adb or os.environ.get(
            "DSH_LOCAL_ADB",
            r"D:\platform-tools-latest-windows\platform-tools\adb.exe")
        self.serial = serial
        self.host = "local-adb" + ((":" + serial) if serial else "")
        if not os.path.exists(self.adb):
            raise RuntimeError("local adb not found: %s" % self.adb)

    def _args(self, extra):
        a = [self.adb]
        if self.serial:
            a += ["-s", self.serial]
        return a + extra

    def _run(self, extra, timeout):
        p = self._sp.run(self._args(extra), capture_output=True, timeout=timeout)
        return ((p.stdout or b"").decode("utf-8", "replace")
                + (p.stderr or b"").decode("utf-8", "replace"))

    def vm(self, cmd, timeout=300):
        raise RuntimeError("LocalBoard 没有 VM 侧；用 Board() 走 VM 通路")

    def board(self, script, timeout=600):
        b64 = base64.b64encode(script.encode("utf-8")).decode("ascii")
        return self._run(["shell", "echo %s | base64 -d | sh" % b64], timeout)

    def adb(self, args, timeout=600):
        return self._run(str(args).split(), timeout)

    def close(self):
        pass


def has_local_board(adb=None, serial=None):
    """本机 adb 是否能看到一块 device 状态的板子。"""
    import subprocess
    adb = adb or os.environ.get(
        "DSH_LOCAL_ADB",
        r"D:\platform-tools-latest-windows\platform-tools\adb.exe")
    if not os.path.exists(adb):
        return False
    try:
        a = [adb] + (["-s", serial] if serial else []) + ["devices"]
        p = subprocess.run(a, capture_output=True, timeout=60)
        txt = (p.stdout or b"").decode("utf-8", "replace")
    except Exception:
        return False
    for line in txt.splitlines()[1:]:
        parts = line.split()
        if len(parts) >= 2 and parts[1] == "device":
            return True
    return False


def open_board(prefer="local", serial=None, timeout=25):
    """按需打开一条板端通路：local（本机 USB adb，默认）或 vm。"""
    if prefer == "vm":
        return Board(timeout=timeout)
    if has_local_board(serial=serial):
        return LocalBoard(serial=serial, timeout=timeout)
    raise RuntimeError("本机 adb 看不到板子，且未指定走 VM（--transport vm）")


# --------------------------------------------------------------------------- 清单
def load_manifest(path=DEFAULT_MANIFEST):
    with open(path, "r", encoding="utf-8") as fp:
        man = json.load(fp)
    man["_path"] = os.path.abspath(path)
    man["_smart_car_dir"] = SMART_CAR_DIR
    for e in man["files"]:
        p = e.get("path", "")
        if "|" in p:
            raise ValueError("清单里的路径不能含 '|'（探针用它做分隔符）：%r" % p)
    return man


def expand_vars(text, man):
    """把 source.path 里的 {vm_app_tree} 之类展开。"""
    for key, val in man.get("paths", {}).items():
        if key.startswith("repo_rel"):
            continue
        text = text.replace("{%s}" % key, val)
    return text


def repo_file(man, rel_path):
    return os.path.join(man["_smart_car_dir"], rel_path.replace("/", os.sep))


def md5_bytes(data):
    return hashlib.md5(data).hexdigest()


def read_local_raw(path):
    with open(path, "rb") as fp:
        return fp.read()


def read_local_normalized(path):
    """读本地文件并把 CRLF 归一成 LF。

    板端**脚本**必须是 LF，否则 sh 会报 `\\r: not found`；而仓库里的文件在 Windows
    上可能被某些工具改成 CRLF。归一化后比较/推送，才能保证幂等收敛。

    ⚠ 但**媒体文件（mp3/lrc/mp4）不要归一化** —— 曲库里的 .lrc 本来就是 CRLF，
      归一化会让「推上去的内容」和清单里记的 md5 对不上，恢复就永远不收敛。
      所以规则是：脚本/配置（restore=push）归一化；dir_set 里的文件（restore=push_file）
      按原始字节处理，除非清单里显式写了 normalize_lf。
    """
    return read_local_raw(path).replace(b"\r\n", b"\n")


def md5_raw(path):
    return md5_bytes(read_local_raw(path))


def local_md5(path, normalize=True):
    return md5_bytes(read_local_normalized(path) if normalize else read_local_raw(path))


# --------------------------------------------------------------------------- 探针
def build_probe_script(man, only_groups=None, do_hash=True):
    """生成一段**只读** sh 脚本，一次性把清单里所有项的状态打成机器可读的行。

    输出格式（`|` 分隔，行首标记）：
        @@F|<idx>|F|md5|size|mode|-      普通文件
        @@F|<idx>|L|linktarget|-|-|-     符号链接
        @@F|<idx>|D|-|-|mode|-           目录
        @@F|<idx>|N|-|-|-|-              不存在
        @@D|<idx>|name|md5|size          dir_set 里的一个文件
        @@C|<idx>|检查输出（换行已替换成空格）
    """
    L = ["#!/bin/sh", "# board_inventory 生成的一次性只读探针；不要写板子。", ""]
    L.append("echo '@@BEGIN'")

    for idx, e in enumerate(man["files"]):
        if only_groups and e.get("group") not in only_groups:
            continue
        kind = e["kind"]
        path = e["path"]

        if kind == "check":
            # ⚠ 整段 cmd 会落在 "$( ... )" 里（双引号内），所以**不要**把单引号
            #   转义成 '\'' —— 双引号内反斜杠对单引号不特殊，转义反而会破坏命令。
            L.append("printf '@@C|%d|%%s\\n' \"$( { %s ; } 2>&1 | tr '\\n' ' ' )\""
                     % (idx, e["cmd"]))
            continue

        if kind == "dir_set":
            L.append("P='%s'" % path.replace("'", "'\\''"))
            L.append("if [ -d \"$P\" ]; then")
            L.append("  for f in \"$P\"/* \"$P\"/.[!.]*; do")
            L.append("    [ -e \"$f\" ] || continue")
            L.append("    printf '@@D|%d|%%s|%%s|%%s\\n' \"$(basename \"$f\")\" "
                     "\"$(md5sum \"$f\" | cut -d' ' -f1)\" \"$(wc -c < \"$f\")\"" % idx)
            L.append("  done")
            L.append("  printf '@@F|%d|D|-|-|%%s|-\\n' \"$(ls -ld \"$P\" | cut -c1-10)\"" % idx)
            L.append("else")
            L.append("  printf '@@F|%d|N|-|-|-|-\\n'" % idx)
            L.append("fi")
            continue

        hash_expr = "\"$(md5sum \"$P\" | cut -d' ' -f1)\"" if do_hash else "'-'"
        L.append("P='%s'" % path.replace("'", "'\\''"))
        L.append("if [ -L \"$P\" ]; then")
        # ⚠ readlink 的结果放**最后一列**（extra），别放 md5 列 —— 否则判定会读错字段
        L.append("  printf '@@F|%d|L|-|-|-|%%s\\n' \"$(readlink \"$P\")\"" % idx)
        L.append("elif [ -d \"$P\" ]; then")
        L.append("  printf '@@F|%d|D|-|-|%%s|-\\n' \"$(ls -ld \"$P\" | cut -c1-10)\"" % idx)
        L.append("elif [ -f \"$P\" ]; then")
        L.append("  printf '@@F|%d|F|%%s|%%s|%%s|-\\n' %s \"$(wc -c < \"$P\")\" "
                 "\"$(ls -ld \"$P\" | cut -c1-10)\"" % (idx, hash_expr))
        L.append("else")
        L.append("  printf '@@F|%d|N|-|-|-|-\\n'" % idx)
        L.append("fi")

    L.append("echo '@@END'")
    return "\n".join(L) + "\n"


def build_extras_script(dirs):
    """列出待查目录里的所有条目名（用来找「多余」）。"""
    L = ["#!/bin/sh"]
    for d in dirs:
        L.append("echo '@@X|%s'" % d)
        L.append("ls -1 '%s' 2>/dev/null" % d.replace("'", "'\\''"))
    return "\n".join(L) + "\n"


def parse_probe(text):
    """把探针输出解析成 {idx: {...}} 与 {idx: [dir_set 文件...]}、{idx: check输出}。"""
    items, dirsets, checks = {}, {}, {}
    cur_dir = None
    in_body = False
    for line in text.splitlines():
        line = line.rstrip("\r")
        if line == "@@BEGIN":
            in_body = True
            continue
        if line == "@@END":
            in_body = False
            continue
        if line.startswith("@@X|"):
            cur_dir = line[4:]
            dirsets.setdefault(("x", cur_dir), [])
            continue
        if not line.startswith("@@"):
            if in_body and cur_dir is not None and line.strip():
                dirsets.setdefault(("x", cur_dir), []).append(line.strip())
            continue
        parts = line.split("|")
        tag = parts[0]
        if tag == "@@F":
            idx = int(parts[1])
            items[idx] = {
                "type": parts[2],
                "md5": parts[3],
                "size": parts[4],
                "mode": parts[5],
                "extra": parts[6] if len(parts) > 6 else "",
            }
        elif tag == "@@D":
            idx = int(parts[1])
            dirsets.setdefault(idx, []).append({
                "name": parts[2],
                "md5": parts[3],
                "size": int(parts[4]) if parts[4].isdigit() else -1,
            })
        elif tag == "@@C":
            # tr '\n' ' ' 会留一个尾空格，必须 strip，否则 '^...$' 永远匹配不上
            checks[int(parts[1])] = "|".join(parts[2:]).strip()
    return items, dirsets, checks


# --------------------------------------------------------------------------- 判定
def entries_of(man, only_groups=None):
    for idx, e in enumerate(man["files"]):
        if only_groups and e.get("group") not in only_groups:
            continue
        yield idx, e


def expected_md5(e, man, warnings):
    """按 md5_policy 算出「期望的 md5」；返回 None 表示不做内容比对。"""
    policy = e.get("md5_policy", "none")
    if policy == "exact":
        return e.get("md5")
    if policy == "source_local":
        rel = e.get("source", {}).get("path", "")
        lp = repo_file(man, rel)
        if not os.path.isfile(lp):
            warnings.append("清单源文件缺失，无法比对：%s（%s）" % (rel, e["path"]))
            return None
        return local_md5(lp)
    if policy == "source_vm":
        vm_path = expand_vars(e.get("source", {}).get("path", ""), man)
        e["_vm_expected_path"] = vm_path
        return None                       # 由调用方（可选）补充 VM 侧 md5
    return None


def evaluate(man, items, dirsets, checks, warnings, do_hash=True,
             vm_md5=None, only_groups=None):
    """把探针结果按清单判成 ok / missing / mismatch / type / manifest_error。"""
    ok, missing, mismatch, typeerr, manifest_err = [], [], [], [], []
    for idx, e in entries_of(man, only_groups):
        kind = e["kind"]
        path = e["path"]
        rec = {"idx": idx, "entry": e}

        if kind == "check":
            got = checks.get(idx)
            if got is None:
                rec["got"] = "(未执行)"
                missing.append(rec)
                continue
            exp = e.get("expect")
            if exp and not re.search(exp, got):
                rec["got"] = got.strip()
                rec["expect"] = exp
                mismatch.append(rec)
            else:
                rec["got"] = got.strip()
                ok.append(rec)
            continue

        it = items.get(idx)
        if it is None:
            rec["got"] = "(未探测)"
            missing.append(rec)
            continue
        rec["got"] = it

        if it["type"] == "N":
            missing.append(rec)
            continue

        if kind == "dir":
            (ok if it["type"] == "D" else typeerr).append(rec)
            continue

        if kind == "dir_set":
            if it["type"] != "D":
                typeerr.append(rec)
                continue
            seen = {f["name"]: f for f in dirsets.get(idx, [])}
            bad = []
            for want in e.get("files", []):
                got = seen.get(want["name"])
                if got is None:
                    bad.append((want["name"], "缺文件"))
                elif do_hash and want.get("md5") and got["md5"] != want["md5"]:
                    bad.append((want["name"], "md5 %s != %s" % (got["md5"], want["md5"])))
                elif want.get("size") and got["size"] != want["size"]:
                    bad.append((want["name"], "大小 %s != %s" % (got["size"], want["size"])))
            rec["bad"] = bad
            rec["found"] = sorted(seen)
            if bad:
                mismatch.append(rec)
            else:
                ok.append(rec)
            continue

        if kind == "symlink":
            if it["type"] != "L" or it["extra"] != e.get("link_target"):
                rec["expect"] = "symlink -> %s" % e.get("link_target")
                typeerr.append(rec)
            else:
                ok.append(rec)
            continue

        if kind == "file_or_symlink":
            if it["type"] == "L":
                if e.get("link_target") and it["extra"] != e["link_target"]:
                    rec["expect"] = "symlink -> %s" % e["link_target"]
                    typeerr.append(rec)
                else:
                    ok.append(rec)
                continue
            # 落到普通文件：按 md5 走下面统一逻辑
        elif kind == "file":
            if it["type"] != "F":
                rec["expect"] = "普通文件"
                typeerr.append(rec)
                continue

        # 内容比对
        policy = e.get("md5_policy", "none")
        if policy == "none" or not do_hash:
            ok.append(rec)
            continue
        if policy == "size":
            if it["size"] in ("", "0"):
                rec["expect"] = "非空文件"
                mismatch.append(rec)
            else:
                ok.append(rec)
            continue

        exp = None
        if policy == "source_vm":
            vp = e.get("_vm_expected_path") or expand_vars(e.get("source", {}).get("path", ""), man)
            exp = (vm_md5 or {}).get(vp)
            if exp is None:
                warnings.append("拿不到 VM 侧 md5，降级成只看存在：%s（%s）" % (vp, path))
                ok.append(rec)
                continue
        else:
            exp = expected_md5(e, man, warnings)
        if exp is None:
            manifest_err.append(rec)
            continue
        rec["expect"] = exp
        if it["md5"] != exp:
            mismatch.append(rec)
        else:
            ok.append(rec)
    return {"ok": ok, "missing": missing, "mismatch": mismatch,
            "typeerr": typeerr, "manifest_err": manifest_err}


def classify_extras(man, raw_extras):
    """raw_extras: [(dir, [names])] -> [(full_path, name)] 不在清单里且未被 allow 命中的。"""
    known = set(e["path"] for e in man["files"])
    for e in man["files"]:
        for f in e.get("files", []) or []:
            known.add(e["path"] + "/" + f["name"])
    allow = man.get("extras_allow", [])
    out = []
    for d, names in raw_extras:
        for n in names:
            full = d.rstrip("/") + "/" + n
            if full in known:
                continue
            if any(fnmatch.fnmatch(n, pat) for pat in allow):
                continue
            out.append((full, n))
    return sorted(out)


# --------------------------------------------------------------------------- 输出
def hr(title=None):
    line = "-" * 78
    if title:
        print("\n" + line)
        print(title)
        print(line)
    else:
        print(line)


def short(s, n=88):
    s = str(s)
    return s if len(s) <= n else s[:n - 3] + "..."


def vm_md5_batch(board, vm_paths, warnings):
    """一次 VM 往返拿多个文件的 md5（注意：VM 侧不能用管道）。"""
    if not vm_paths:
        return {}
    # 不用管道：md5sum 直接输出 "hash  path"，逐行解析。
    cmd = "md5sum " + " ".join("'%s'" % p.replace("'", "'\\''") for p in vm_paths)
    out = board.vm(cmd, timeout=180)
    res = {}
    for line in out.splitlines():
        parts = line.split(None, 1)
        if len(parts) == 2 and re.fullmatch(r"[0-9a-f]{32}", parts[0]):
            res[parts[1].strip()] = parts[0]
    for p in vm_paths:
        if p not in res:
            warnings.append("VM 上没有这个文件或算不出 md5：%s" % p)
    return res


def die(msg, code=EXIT_TOOL):
    sys.stderr.write("ERROR: %s\n" % msg)
    sys.exit(code)
