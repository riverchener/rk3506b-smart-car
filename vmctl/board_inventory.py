#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""board_inventory —— 板端文件清点（**纯只读**）

拿 Smart_car/board/expected_files.json 当唯一事实来源，连板子逐项核对，输出三张表：

    缺失 / 内容不一致（md5 不符或类型不符） / 多余（可选）

有缺失或不符合时退出码非 0：

    0 = 全部符合
    1 = 有**关键**项缺失或不符合
    2 = 只有非关键项有问题
    3 = 工具/连接/清单本身有问题

只做 ls / readlink / md5sum / wc / grep / pgrep / ip / lsmod 这类只读操作，
**绝不写板子**（单写者规则：同一时刻只允许一个 agent 动板子）。

用法：
    python vmctl/board_inventory.py
    python vmctl/board_inventory.py --group init --group bt
    python vmctl/board_inventory.py --json build_out/inventory.json --no-extras
"""
from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from board_common import (  # noqa: E402
    ADB, DEFAULT_MANIFEST, EXIT_CRITICAL, EXIT_MINOR, EXIT_OK, EXIT_TOOL,
    Board, build_extras_script, build_probe_script, classify_extras, die,
    evaluate, expand_vars, hr, load_manifest, parse_probe, short, vm_md5_batch,
)

EXTRAS_DIRS = ["/userdata/smart_car", "/etc/init.d", "/etc/bluetooth", "/userdata/ota"]


def parse_args(argv):
    ap = argparse.ArgumentParser(description="板端文件清点（只读）")
    ap.add_argument("--manifest", default=DEFAULT_MANIFEST, help="期望清单 JSON")
    ap.add_argument("--host", default="192.168.88.130")
    ap.add_argument("--user", default="cjh")
    ap.add_argument("--password", default="181305")
    ap.add_argument("--adb", default=ADB)
    ap.add_argument("--group", action="append", default=None,
                    help="只看这些分组（可重复）")
    ap.add_argument("--no-hash", action="store_true",
                    help="跳过 md5（只判存在），快很多")
    ap.add_argument("--no-extras", action="store_true", help="不扫「多余」")
    ap.add_argument("--json", default=None, help="把完整结果写成 JSON")
    ap.add_argument("--quiet", action="store_true", help="只打印结论")
    return ap.parse_args(argv)


def main(argv=None):
    args = parse_args(argv if argv is not None else sys.argv[1:])

    man = load_manifest(args.manifest)
    groups = args.group
    do_hash = not args.no_hash

    warnings = []
    board = Board(args.host, args.user, args.password, args.adb)
    try:
        if not args.quiet:
            print("清单: %s" % man["_path"])
            print("快照: %s    条目: %d" % (man.get("snapshot", "?"), len(man["files"])))
            print("目标: %s@%s -> adb %s" % (args.user, args.host, args.adb))

        probe = build_probe_script(man, only_groups=groups, do_hash=do_hash)
        text = board.board(probe, timeout=900)
        items, dirsets, checks = parse_probe(text)
        if not items and not checks:
            die("探针没有返回任何结果（板子没连上？adb 不通？）\n---- 原始输出 ----\n%s"
                % text[-2000:])

        # source_vm 的期望 md5：一次 VM 往返批量取
        vm_paths = []
        for idx, e in enumerate(man["files"]):
            if groups and e.get("group") not in groups:
                continue
            if e.get("kind") == "file" and e.get("md5_policy") == "source_vm":
                vm_paths.append(expand_vars(e["source"]["path"], man))
        vm_md5 = vm_md5_batch(board, vm_paths, warnings) if vm_paths else {}

        res = evaluate(man, items, dirsets, checks, warnings,
                       do_hash=do_hash, vm_md5=vm_md5, only_groups=groups)

        extras = []
        if not args.no_extras:
            ex_text = board.board(build_extras_script(EXTRAS_DIRS), timeout=180)
            raws = {}
            cur = None
            for line in ex_text.splitlines():
                if line.startswith("@@X|"):
                    cur = line[4:]
                    raws.setdefault(cur, [])
                elif cur and line.strip():
                    raws[cur].append(line.strip())
            extras = classify_extras(man, list(raws.items()))
    finally:
        board.close()

    crit_bad = lambda lst: [r for r in lst if r["entry"].get("critical")]  # noqa: E731

    # ---------------------------------------------------------------- 速览
    problems = (crit_bad(res["missing"]) + crit_bad(res["mismatch"])
                + crit_bad(res["typeerr"]))
    hr("速览：关键项问题（丢了会影响功能）")
    if not problems:
        print("  (无)")
    for r in problems:
        e = r["entry"]
        kind = ("缺失" if r in res["missing"]
                else "内容不符" if r in res["mismatch"] else "类型不符")
        print("  ✗ [%s] %s   —— %s" % (kind, e["path"], short(e.get("why", ""), 60)))
    if res["manifest_err"]:
        print("  ! 清单自身有问题，无法判定：%s"
              % ", ".join(r["entry"]["path"] for r in res["manifest_err"]))

    # ---------------------------------------------------------------- 三张表
    if not args.quiet:
        hr("① 缺失（清单里有、板子上没有）")
        if not res["missing"]:
            print("(无)")
        for r in res["missing"]:
            e = r["entry"]
            print("  [%s/%s] %s" % ("关键" if e.get("critical") else "非关键",
                                    e.get("group", "?"), e["path"]))
            print("        影响: %s" % short(e.get("why", ""), 100))
            got = r.get("got")
            if e["kind"] == "check":
                print("        检查: %s -> %s" % (short(e["cmd"], 70), got))
            if e.get("status_at_snapshot", "").startswith("MISSING"):
                print("        快照时就已经缺（不是这次新丢的）")
            print("        恢复: %s" % describe_restore(e, man))

        hr("② 内容不一致（md5 不符 / 类型不符）")
        if not res["mismatch"] and not res["typeerr"]:
            print("(无)")
        for r in res["mismatch"]:
            e = r["entry"]
            print("  [%s] %s" % ("关键" if e.get("critical") else "非关键", e["path"]))
            if e["kind"] == "dir_set":
                for name, why in r.get("bad", []):
                    print("        - %s : %s" % (name, why))
            elif e["kind"] == "check":
                print("        期望匹配: %s" % r.get("expect"))
                print("        实际输出: %s" % short(r.get("got"), 100))
            else:
                g = r["got"]
                print("        期望 md5: %s" % r.get("expect"))
                print("        实际 md5: %s  (%s 字节)"
                      % (g["md5"], g["size"]))
            print("        恢复: %s" % describe_restore(e, man))
        for r in res["typeerr"]:
            e = r["entry"]
            g = r.get("got", {})
            print("  [%s] %s" % ("关键" if e.get("critical") else "非关键", e["path"]))
            print("        期望: %s   实际类型: %s"
                  % (r.get("expect", e["kind"]), g.get("type") if isinstance(g, dict) else g))
            print("        恢复: %s" % describe_restore(e, man))

        hr("③ 多余（板上有、清单里没有；仅供参考，不影响退出码）")
        if args.no_extras:
            print("(已跳过 --no-extras)")
        elif not extras:
            print("(无)")
        for full, name in extras:
            print("  %s" % full)
        if extras:
            print("\n  说明：这些多半是运行期产物（*.log / *.bak / 缓存）或旧项目残留。")
            print("  要长期忽略，把名字加进清单的 extras_allow。")

        if res["manifest_err"]:
            hr("④ 清单自身的问题（无法比对）")
            for r in res["manifest_err"]:
                print("  %s" % r["entry"]["path"])
        if warnings:
            hr("提示")
            for w in warnings:
                print("  - %s" % w)

    # ---------------------------------------------------------------- 汇总
    def counts(lst):
        c = len([r for r in lst if r["entry"].get("critical")])
        return len(lst), c

    m, mc = counts(res["missing"])
    x, xc = counts(res["mismatch"])
    t, tc = counts(res["typeerr"])
    hr("汇总")
    total = len([e for e in man["files"] if not groups or e.get("group") in groups])
    print("  条目总数      : %d" % total)
    print("  符合          : %d" % len(res["ok"]))
    print("  缺失          : %d （其中关键 %d）" % (m, mc))
    print("  内容不一致    : %d （其中关键 %d）" % (x, xc))
    print("  类型不符      : %d （其中关键 %d）" % (t, tc))
    print("  多余(可选)    : %d" % len(extras))

    per_group = {}
    for name, lst in (("缺失", res["missing"]), ("不一致", res["mismatch"]),
                      ("类型不符", res["typeerr"])):
        for r in lst:
            g = r["entry"].get("group", "?")
            per_group.setdefault(g, []).append(name)
    if per_group:
        print("  按分组:")
        for g in sorted(per_group):
            print("    %-10s %s" % (g, " ".join(sorted(set(per_group[g])))))

    if args.json:
        out = {
            "manifest": man["_path"], "snapshot": man.get("snapshot"),
            "ok": [r["entry"]["path"] for r in res["ok"]],
            "missing": [{"path": r["entry"]["path"], "critical": bool(r["entry"].get("critical")),
                         "group": r["entry"].get("group")} for r in res["missing"]],
            "mismatch": [{"path": r["entry"]["path"],
                          "critical": bool(r["entry"].get("critical")),
                          "group": r["entry"].get("group"),
                          "expected": r.get("expect"),
                          "got": r.get("got")} for r in res["mismatch"]],
            "typeerr": [{"path": r["entry"]["path"], "expect": r.get("expect")}
                        for r in res["typeerr"]],
            "extras": [f for f, _ in extras],
            "warnings": warnings,
        }
        with open(args.json, "w", encoding="utf-8") as fp:
            json.dump(out, fp, ensure_ascii=False, indent=2)
        print("  结果已写入: %s" % args.json)

    if crit_bad(res["missing"]) or crit_bad(res["mismatch"]) or crit_bad(res["typeerr"]):
        print("\n结论: 有【关键】项缺失或不符合 -> 退出码 1")
        print("      补齐（默认 dry-run，看清再动手）: python vmctl/board_restore.py")
        return EXIT_CRITICAL
    if res["missing"] or res["mismatch"] or res["typeerr"]:
        print("\n结论: 只有非关键项有问题 -> 退出码 2")
        return EXIT_MINOR
    if res["manifest_err"]:
        print("\n结论: 清单自身有问题（无法判定）-> 退出码 3")
        return EXIT_TOOL
    print("\n结论: 全部符合 -> 退出码 0")
    return EXIT_OK


def describe_restore(e, man=None):
    r = e.get("restore", "?")
    src = e.get("source", {}).get("path", "?")
    if man is not None and src != "?":
        src = expand_vars(src, man)
    if r == "push":
        return "从仓库推 %s -> %s" % (e["source"]["path"], e["path"])
    if r == "push_vm":
        return "从 VM 推 %s -> %s" % (src, e["path"])
    if r == "symlink":
        return "建符号链接 %s -> %s" % (e["path"], e.get("link_target"))
    if r == "mkdir":
        return "mkdir -p %s" % e["path"]
    if r == "write":
        return "写入清单内置内容（%d 字节）" % len(e.get("source", {}).get("content", ""))
    if r == "manual":
        return "需人工：%s" % short(e.get("manual_hint", "（清单里没写提示）"), 90)
    return r


if __name__ == "__main__":
    sys.exit(main())
