#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""board_restore_selftest —— **离线**自检：不连板子，证明恢复脚本"会补什么、怎么补"。

做法：把 board_restore 里的 Board 换成假的（只记录、不发送），然后对每个恢复动作
跑一遍 `apply_action(..., run=True)`，把真正会发给 VM/板子的命令原样打印出来；
对走 base64 内联写入的动作，还把捕获到的 payload 解码回来跟本地源文件比 md5 ——
证明「推过去的内容就是仓库里那一份」。

用法：
    python vmctl/board_restore_selftest.py
    python vmctl/board_restore_selftest.py --only-critical
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.stdout.reconfigure(encoding="utf-8", errors="replace")

import board_restore as BR  # noqa: E402
from board_common import (  # noqa: E402
    DEFAULT_MANIFEST, evaluate, hr, load_manifest, local_md5, read_local_normalized,
    repo_file,
)


class FakeSFTP(object):
    def __init__(self, log):
        self.log = log

    def put(self, local, remote):
        self.log.append(("sftp.put", "%s -> %s" % (local, remote)))


class FakeBoard(object):
    """只记录命令，什么都不发。"""

    def __init__(self):
        self.log = []

    def vm(self, cmd, timeout=300):
        self.log.append(("vm", cmd))
        return "YES\n"                      # 让 test -f 走"存在"分支

    def board(self, script, timeout=600):
        self.log.append(("board", script))
        return "MKDIR-OK\nWRITE-OK\nPUSH-INLINE-OK\nPUSH-OK\nLNK-OK\nRUNDONE=0\n"

    def adb(self, args, timeout=600):
        self.log.append(("adb", args))
        return "1 file pushed\n"

    def sftp(self):
        return FakeSFTP(self.log)

    def close(self):
        pass


def fake_results(man):
    """造一份「全缺」的结果，逼所有恢复动作都进计划（离线，不依赖真实板子）。"""
    res = {"ok": [], "missing": [], "mismatch": [], "typeerr": [], "manifest_err": []}
    for e in man["files"]:
        res["missing"].append({"idx": 0, "entry": e, "got": {}})
    return res


def extract_inline_payload(script):
    """把内联写入脚本里 base64 分片拼回来解码。"""
    chunks = re.findall(r"printf '%s' '([A-Za-z0-9+/=]+)'", script)
    if not chunks:
        return None
    try:
        return base64.b64decode("".join(chunks))
    except Exception:
        return None


def main(argv=None):
    ap = argparse.ArgumentParser(description="board_restore 离线自检")
    ap.add_argument("--manifest", default=DEFAULT_MANIFEST)
    ap.add_argument("--only-critical", action="store_true")
    ap.add_argument("--group", action="append", default=None)
    args = ap.parse_args(argv if argv is not None else sys.argv[1:])

    man = load_manifest(args.manifest)
    ns = argparse.Namespace(force=False, group=args.group,
                            only_critical=args.only_critical, skip_optional=False)
    res = fake_results(man)
    if args.only_critical:
        res["missing"] = [r for r in res["missing"] if r["entry"].get("critical")]

    actions = BR.plan_actions(man, res, ns)
    hr("离线自检：假设清单里的每一项都缺，恢复脚本会发这些命令（共 %d 步）" % len(actions))

    problems = []
    payload_checks = []
    for i, a in enumerate(actions, 1):
        bd = FakeBoard()
        ok, desc = BR.apply_action(bd, a, run=True)
        print("\n%2d. [%s] %s" % (i, "OK" if ok else ("!!" if ok is False else "--"), desc))
        for kind, text in bd.log:
            if kind == "board":
                first = text.splitlines()[0] if text.splitlines() else ""
                print("      board<< %s%s" % (first, "   …（%d 行脚本）" % len(text.splitlines())
                                              if len(text.splitlines()) > 1 else ""))
                payload = extract_inline_payload(text)
                if payload is not None and a.get("local") and os.path.isfile(a["local"]):
                    got = hashlib.md5(payload).hexdigest()
                    want = local_md5(a["local"], normalize=BR.want_normalize(a))
                    same = (got == want)
                    payload_checks.append((a["target"], same, want, got))
                    print("      ↳ 内联 payload 解码后 md5 %s（本地 %s）%s"
                          % (got, want, "✓ 一致" if same else "✗ 不一致"))
            else:
                print("      %-10s %s" % (kind, text.replace("\n", " ; ")[:150]))
        if ok is False:
            problems.append(desc)

    hr("自检结论")
    print("  动作数          : %d" % len(actions))
    print("  会失败的动作    : %d" % len(problems))
    for p in problems:
        print("    - %s" % p)
    print("  内联 payload 校验: %d 项" % len(payload_checks))
    bad = [c for c in payload_checks if not c[1]]
    for tgt, same, want, got in payload_checks:
        print("    %s %s" % ("✓" if same else "✗", tgt))
    if bad:
        print("  !! 有 payload 与本地源不一致，恢复脚本有问题")
        return 1
    print("\n  说明：以上只是命令文本，**没有连板子也没有写板子**。")
    print("  真写请用: python vmctl/board_restore.py --run")
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
