#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""board_restore —— 按期望清单**幂等补齐**板端文件（默认 dry-run）

原则：
  · 先比对 md5，**只推缺的/不符的**，绝不无脑全量覆盖；
  · **默认 dry-run**，加 `--run` 才真的写板子；
  · 每一步都打印「将做什么」，`--run` 时做完再重新清点一遍自证；
  · 幂等：可以对同一块板反复跑，第二次什么都不会做。

⚠ 单写者规则：板子同一时刻只能有一个写入者。跑 `--run` 之前先确认没有别的
  agent 正在部署（本项目并行排查时踩过这个坑）。

⚠ 本脚本**不碰** mtd6 / M0 固件 / OTA 服务器；只写 /etc、/usr、/userdata 下的清单条目。

用法：
    python vmctl/board_restore.py                    # dry-run，列出要补什么
    python vmctl/board_restore.py --only-critical     # 只处理关键项
    python vmctl/board_restore.py --run               # 真的补
"""
from __future__ import annotations

import argparse
import base64
import os
import posixpath
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from board_common import (  # noqa: E402
    ADB, DEFAULT_MANIFEST, EXIT_CRITICAL, EXIT_OK,
    Board, build_probe_script, die, evaluate, expand_vars, hr, load_manifest,
    local_md5, parse_probe, read_local_normalized, read_local_raw, repo_file, short,
    vm_md5_batch,
)

INLINE_LIMIT = 256 * 1024        # 小于这个的文本文件直接 base64 写进去，不用 adb push
VM_TMP = "/tmp/smartcar_restore"


def shq(s):
    """VM(bash) 侧的单引号转义。"""
    return "'" + str(s).replace("'", "'\\''") + "'"


def parse_args(argv):
    ap = argparse.ArgumentParser(description="按期望清单幂等补齐板端文件（默认 dry-run）")
    ap.add_argument("--manifest", default=DEFAULT_MANIFEST)
    ap.add_argument("--host", default="192.168.88.130")
    ap.add_argument("--user", default="cjh")
    ap.add_argument("--password", default="181305")
    ap.add_argument("--adb", default=ADB)
    ap.add_argument("--group", action="append", default=None)
    ap.add_argument("--only-critical", action="store_true", help="只补齐关键项")
    ap.add_argument("--skip-optional", action="store_true",
                    help="跳过非关键项（等价于 --only-critical，但保留提示）")
    ap.add_argument("--force", action="store_true", help="即使已一致也重推一次")
    ap.add_argument("--run", action="store_true", help="真的写板子（默认只打印）")
    return ap.parse_args(argv)


# --------------------------------------------------------------------------- 计划
def plan_actions(man, res, args):
    """把「缺失 / 不一致」翻成可执行动作，按安全顺序排好。"""
    pending = []
    for bucket in ("missing", "mismatch", "typeerr"):
        pending.extend(res[bucket])
    if args.force:
        pending.extend(res["ok"])

    seen = set()
    actions = []
    for r in pending:
        e = r["entry"]
        if e["path"] in seen:
            continue
        seen.add(e["path"])
        if args.group and e.get("group") not in args.group:
            continue
        if (args.only_critical or args.skip_optional) and not e.get("critical"):
            continue
        actions.append(make_action(man, e))
        # dir_set：每个缺失/不符的文件单独一个动作（--force 时全部重推）
        if e["kind"] == "dir_set" and e.get("restore") == "push":
            bad = {n for n, _ in r.get("bad", [])} if r.get("bad") else None
            found = set(r.get("found", []) or [])
            for f in e.get("files", []):
                if not args.force:
                    if bad is not None and f["name"] not in bad:
                        continue
                    if bad is None and f["name"] in found:
                        continue
                actions.append({
                    "op": "push_file",
                    "entry": e,
                    "local": repo_file(man, e["source"]["path"] + "/" + f["name"]),
                    "target": posixpath.join(e["path"], f["name"]),
                    "expect_md5": f.get("md5"),
                    "label": "%s/%s" % (e["path"], f["name"]),
                })

    order = {"mkdir": 0, "write": 1, "push": 1, "push_file": 1, "push_vm": 1,
             "symlink": 2, "run_cmd": 3, "manual": 4}
    actions.sort(key=lambda a: order.get(a["op"], 9))
    return actions


def make_action(man, e):
    op = e.get("restore", "manual")
    a = {"op": op, "entry": e, "label": e["path"], "target": e["path"]}
    if op == "push":
        # dir_set 的「目录」本身只需要存在，文件由 push_file 逐个推
        if e["kind"] == "dir_set":
            a["op"] = "mkdir"
            return a
        a["local"] = repo_file(man, e["source"]["path"])
        a["expect_md5"] = local_md5(a["local"]) if os.path.isfile(a["local"]) else None
    elif op == "push_vm":
        a["vm"] = expand_vars(e["source"]["path"], man)
    elif op == "write":
        a["content"] = e["source"].get("content", "")
    elif op == "symlink":
        a["link_target"] = e.get("link_target")
    elif op == "run_cmd":
        a["cmd"] = e.get("restore_cmd", "")
    return a


def want_normalize(a):
    """要不要把 CRLF 归一成 LF（见 board_common.read_local_normalized 的说明）。"""
    e = a.get("entry", {})
    if "normalize_lf" in e:
        return bool(e["normalize_lf"])
    return a.get("op") != "push_file"


# --------------------------------------------------------------------------- 执行
def do_mkdir(bd, a, run):
    return bd.board("mkdir -p %s && echo MKDIR-OK" % shq_board(a["target"]))


def shq_board(s):
    """板子 sh 里用的单引号转义。"""
    return "'" + str(s).replace("'", "'\\''") + "'"


def do_write(bd, a, run):
    blob = base64.b64encode(a["content"].encode("utf-8")).decode("ascii")
    chunks = [blob[i:i + 4000] for i in range(0, len(blob), 4000)]
    script = ["set -e", "mkdir -p %s" % shq_board(posixpath.dirname(a["target"])),
              ": > /tmp/.sc_restore_b64"]
    for c in chunks:
        script.append("printf '%%s' %s >> /tmp/.sc_restore_b64" % shq_board(c))
    script.append("base64 -d /tmp/.sc_restore_b64 > %s" % shq_board(a["target"]))
    script.append("rm -f /tmp/.sc_restore_b64")
    mode = a["entry"].get("mode")
    if mode:
        script.append("chmod %s %s" % (mode, shq_board(a["target"])))
    script.append("echo WRITE-OK")
    return bd.board("\n".join(script))


def do_symlink(bd, a, run):
    return bd.board("mkdir -p %s; ln -sfn %s %s && echo LNK-OK"
                    % (shq_board(posixpath.dirname(a["target"])),
                       shq_board(a["link_target"]), shq_board(a["target"])))


def do_run_cmd(bd, a, run):
    return bd.board(a["cmd"] + " 2>&1; echo RUNDONE=$?")


def do_push(bd, a, run, is_vm_source):
    """把文件放到板子上：小的走 base64，大的走 adb push。"""
    target = a["target"]
    mode = a["entry"].get("mode") or "0644"

    if not is_vm_source:
        local = a["local"]
        if not os.path.isfile(local):
            return None, "本地源文件不存在: %s" % local
        data = read_local_normalized(local) if want_normalize(a) else read_local_raw(local)
        if len(data) <= INLINE_LIMIT:
            blob = base64.b64encode(data).decode("ascii")
            script = ["set -e",
                      "mkdir -p %s" % shq_board(posixpath.dirname(target)),
                      ": > /tmp/.sc_restore_b64"]
            for i in range(0, len(blob), 4000):
                script.append("printf '%%s' %s >> /tmp/.sc_restore_b64"
                              % shq_board(blob[i:i + 4000]))
            script += ["base64 -d /tmp/.sc_restore_b64 > %s" % shq_board(target),
                       "rm -f /tmp/.sc_restore_b64",
                       "chmod %s %s" % (mode, shq_board(target)),
                       "echo PUSH-INLINE-OK"]
            return bd.board("\n".join(script)), None
        # 大的：先把**归一化后**的内容写成 VM 上的临时文件（绝不动本地源文件！），再 adb push
        bd.vm("mkdir -p %s" % shq(VM_TMP))
        tmpdir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build_out",
                              "_restore_tmp")
        tmpdir = os.path.abspath(tmpdir)
        os.makedirs(tmpdir, exist_ok=True)
        tmp_local = os.path.join(tmpdir, os.path.basename(local))
        with open(tmp_local, "wb") as fp:
            fp.write(data)
        tmpname = os.path.basename(local).replace(" ", "_")
        vm_tmp = "%s/%s" % (VM_TMP, tmpname)
        bd.sftp().put(tmp_local, vm_tmp)
        src_vm = vm_tmp
    else:
        src_vm = a["vm"]
        out = bd.vm("test -f %s && echo YES || echo NO" % shq(src_vm))
        if "YES" not in out:
            return None, "VM 上没有这个文件: %s" % src_vm

    script = ["mkdir -p %s" % shq_board(posixpath.dirname(target))]
    out = bd.adb("push %s %s" % (shq(src_vm), shq(target)))
    script.append("chmod %s %s" % (mode, shq_board(target)))
    return out + bd.board("\n".join(script + ["echo PUSH-OK"])), None


def apply_action(bd, a, run):
    """run=False 时只打印，返回 (ok, 说明)。"""
    op = a["op"]
    tgt = a.get("target", a.get("label"))

    if op == "mkdir":
        if not run:
            return True, "mkdir -p %s" % tgt
        return "MKDIR-OK" in do_mkdir(bd, a, run), "mkdir -p %s" % tgt

    if op == "write":
        n = len(a["content"])
        if not run:
            return True, "写入内置内容 %d 字节 -> %s" % (n, tgt)
        out = do_write(bd, a, run)
        return "WRITE-OK" in out, "写入 %d 字节 -> %s" % (n, tgt)

    if op == "symlink":
        if not run:
            return True, "ln -sfn %s %s" % (a["link_target"], tgt)
        out = do_symlink(bd, a, run)
        return "LNK-OK" in out, "ln -sfn %s %s" % (a["link_target"], tgt)

    if op == "run_cmd":
        if not run:
            return True, "执行: %s" % a["cmd"]
        out = do_run_cmd(bd, a, run)
        return "RUNDONE=0" in out, "执行: %s" % a["cmd"]

    if op in ("push", "push_file", "push_vm"):
        is_vm = op == "push_vm"
        src = a.get("vm") if is_vm else a.get("local")
        if not run:
            extra = ""
            if not is_vm and src and not os.path.isfile(src):
                extra = "   ⚠ 本地源不存在！"
            return True, "推 %s -> %s%s" % (src, tgt, extra)
        out, err = do_push(bd, a, run, is_vm)
        if err:
            return False, "%s   (%s)" % (err, tgt)
        return ("PUSH-OK" in out or "PUSH-INLINE-OK" in out), "推 %s -> %s" % (src, tgt)

    if op == "manual":
        hint = a["entry"].get("manual_hint", "")
        return None, "需人工处理: %s\n           %s" % (tgt, short(hint, 150))

    return None, "未知动作 %s（%s）" % (op, tgt)


# --------------------------------------------------------------------------- 主流程
def preflight_space(bd, actions):
    """写 rootfs 前先看剩余空间。

    ★ 本板 rootfs 常年 99% 满（实测只剩 ~1.1MB）。往 /etc、/usr 里写文件时如果
      空间不够，会出现"文件写了但截断"这种最难查的状态。所以先算清楚再说。
    """
    root_actions = []
    for a in actions:
        tgt = a.get("target", "")
        if not tgt.startswith("/userdata"):
            root_actions.append(a)
    if not root_actions:
        return
    out = bd.board("df -k / | tail -1 | awk '{print $4}'", timeout=60)
    free_kb = None
    for tok in out.split():
        if tok.isdigit():
            free_kb = int(tok)
    need = 0
    for a in root_actions:
        try:
            if a.get("local") and os.path.isfile(a["local"]):
                need += os.path.getsize(a["local"])
            elif a.get("vm"):
                r = bd.vm("stat -c%%s %s 2>/dev/null || echo 0" % shq(a["vm"]))
                need += int(r.strip().splitlines()[-1] or 0) if r.strip() else 0
            elif a.get("op") == "write":
                need += len(a.get("content", ""))
        except Exception:
            pass
    print("  rootfs 剩余: %s  本次计划写入 rootfs 的项: %d 个，约 %d 字节"
          % ("%s KB" % free_kb if free_kb is not None else "?", len(root_actions), need))
    if free_kb is not None and need and need > (free_kb - 1024) * 1024:
        print("  ⚠ 空间可能不够（留 1MB 余量）！先清理或改用 /userdata + 软链方案。")
        for a in root_actions:
            print("     - %s" % a.get("target"))


def probe_and_eval(bd, man, args, warnings):
    probe = build_probe_script(man, only_groups=args.group, do_hash=True)
    text = bd.board(probe, timeout=900)
    items, dirsets, checks = parse_probe(text)
    if not items and not checks:
        die("探针没有返回任何结果（板子没连上？adb 不通？）\n%s" % text[-1500:])
    vm_paths = []
    for e in man["files"]:
        if args.group and e.get("group") not in args.group:
            continue
        if e.get("md5_policy") == "source_vm" and e.get("kind") == "file":
            vm_paths.append(expand_vars(e["source"]["path"], man))
    vm_md5 = vm_md5_batch(bd, vm_paths, warnings) if vm_paths else {}
    return evaluate(man, items, dirsets, checks, warnings, do_hash=True,
                    vm_md5=vm_md5, only_groups=args.group)


def report(res, title):
    hr(title)
    n = 0
    for bucket in ("missing", "mismatch", "typeerr"):
        for r in res[bucket]:
            e = r["entry"]
            n += 1
            print("  [%s/%s] %s"
                  % ("关键" if e.get("critical") else "非关键", e.get("group", "?"), e["path"]))
            if bucket == "dir_set" or e["kind"] == "dir_set":
                for name, why in r.get("bad", []) or []:
                    print("        - %s : %s" % (name, why))
    if n == 0:
        print("  (无)")
    return n


def main(argv=None):
    args = parse_args(argv if argv is not None else sys.argv[1:])
    man = load_manifest(args.manifest)
    warnings = []
    bd = Board(args.host, args.user, args.password, args.adb)
    try:
        print("清单: %s" % man["_path"])
        print("模式: %s" % ("★ 真写（--run）" if args.run else "dry-run（只打印，不碰板子）"))
        print("目标: %s@%s" % (args.user, args.host))

        before = probe_and_eval(bd, man, args, warnings)
        report(before, "清点结果（补齐前）")

        actions = plan_actions(man, before, args)
        hr("恢复计划（%d 步）" % len(actions))
        if not actions:
            print("  无需补齐 —— 板子已与清单一致。")
            return EXIT_OK

        if args.run:
            print("  ⚠ 单写者规则：确认此刻没有别的 agent 在部署这块板子。")
            print("  ⚠ 本脚本只写 /etc、/usr、/userdata 下的清单条目，不碰 mtd6/M0/OTA。")
            preflight_space(bd, actions)
            print("")

        fails, manuals = [], []
        for i, a in enumerate(actions, 1):
            ok, desc = apply_action(bd, a, args.run)
            tag = "  " if not args.run else ("OK" if ok else ("!!" if ok is False else "--"))
            print("  %2d. [%s] %s" % (i, tag, desc))
            if args.run:
                if ok is False:
                    fails.append(desc)
                elif ok is None and a["op"] == "manual":
                    manuals.append(a["entry"])

        if not args.run:
            hr("dry-run 结束：上面就是要做的事")
            print("  真正执行： python vmctl/board_restore.py --run")
            print("  只想补关键项： python vmctl/board_restore.py --run --only-critical")
            if [e for e in (a["entry"] for a in actions) if e.get("restore") == "manual"]:
                print("\n  其中「需人工」的项不能自动补，见下面的恢复后待办。")

        if args.run:
            hr("补齐后自证：重新清点一遍")
            time.sleep(2)
            after = probe_and_eval(bd, man, args, warnings)
            left = report(after, "清点结果（补齐后）")
            if fails:
                print("\n  失败 %d 项:" % len(fails))
                for f in fails:
                    print("    - %s" % f)
            print("\n  剩余待处理: %d 项" % left)
            if not left and not fails:
                print("  ✓ 已与清单一致")
                return EXIT_OK
            return EXIT_CRITICAL

    finally:
        bd.close()

    # ---------------------------------------------------------------- 恢复后待办
    hr("恢复后必须做的事")
    step = 0
    need_restart = sorted({a["entry"].get("reload", "") for a in actions
                           if a["entry"].get("reload")})
    need_restart = [r for r in need_restart if r and not r.startswith("无")]
    step += 1
    if need_restart:
        print("  %d) 让改动生效（按需）:" % step)
        for r in need_restart:
            print("     - %s" % r)
    else:
        print("  %d) 本次没有需要重启的服务。" % step)

    step += 1
    print("  %d) 冷启动条件（硬限制，adb reboot 不算）:" % step)
    for item in man.get("cold_boot_required_for", []):
        print("     - %s" % item)

    checked = [e for e in (a["entry"] for a in actions) if e.get("restore") == "manual"]
    if checked:
        step += 1
        print("  %d) 需要人工的项（脚本不会自动做）:" % step)
        for e in checked:
            print("     - %s" % e["path"])
            print("       %s" % e.get("manual_hint", ""))

    step += 1
    print("  %d) 跑一次清点收尾: python vmctl/board_inventory.py" % step)
    return EXIT_CRITICAL if actions else EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
