#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""check_board_artifact_paths.py —— 「板端产物落错分区」全仓静态检查器（硬规则 ③ 的守门人）。

用途（纯离线，**不连板子**）：
    python vmctl/check_board_artifact_paths.py                # 打表；有 ERROR 就退出码 1
    python vmctl/check_board_artifact_paths.py --md out.md    # 同时写一份 Markdown 清单

它在仓库里找「**在板子上执行**、且把**截图/抓帧/日志/临时产物**写到 `/userdata` 之外」的代码，
按严重程度分档：

    ERROR  板端产物写到了 **rootfs**（/home /root /etc /usr /opt /var/lib / …）
           ⇒ rootfs 只有 150 MB 且常年 99% 满；写满会让 /etc 写入失败、文件被截断
             （历史事故：libltdl.so.7.3.1 被截成 0 字节 ⇒ 重启可能起不来）。必须改成 /userdata。
    ERROR  板端 **CWD 依赖**型抓屏（`weston-screenshooter` 不带输出参数）
           ⇒ 产物落"当前目录"，而板端 `adb shell` 的 CWD 是 `/` = **rootfs**。
    WARN   板端产物写到 **tmpfs**（/tmp、/var/log、/run，mount 表实测）
           ⇒ **不占 rootfs**，不是"落错分区"；只是重启即失。允许保留。
    HOST   宿主（VM / PC）侧路径 ⇒ 不是问题（/home/cjh 就是 VM 的 home）。**不改**。
    DEPLOY 板端 rootfs 上的**故意**写入（部署/config 本体、或事故回收脚本）⇒ 有理由，备案。
    UNKNOWN 判不出执行侧 ⇒ 明确列出来，**不许猜**。

判定"是不是板端"：① 文件里有板子直连地址 `192.168.137.104` ⇒ BOARD；
② 只有 VM 地址 `192.168.88.130` ⇒ HOST；③ `adb shell` / `board_common` / `b.board(` ⇒ MIXED（逐行判）；
④ 板端 `.sh`（`WS=/userdata`、`$ADB shell`、`Smart_car/board/`）⇒ BOARD；⑤ 其余 ⇒ UNKNOWN。
`adb push A B` 的第一个路径是**宿主侧**、第二个才是板端 —— 这条单独处理。

⚠ 这是**启发式**检查，不是完备静态分析：
  · 变量拼出来的路径（`$WS/x.ppm`、`WORK + "/x.ppm"`）判不了 ⇒ 归 UNKNOWN 或跳过；
  · 注释/文档/URL 会被排除；`2>&1`、`>/dev/null` 不算写操作；
  · 所以它的结论是"代码可读"档，**不能**替代上板 `df`/`ls` 实测。
"""
from __future__ import annotations

import argparse
import io
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from board_paths import classify, _looks_like_artifact  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BOARD_HOST = "192.168.137.104"
VM_HOST = "192.168.88.130"

SCOPE = [
    ("vmctl", ("**/*.py", "**/*.sh", "**/*.c")),
    ("build_out", ("**/*.py", "**/*.ps1", "**/*.sh")),
    (os.path.join("Linux车载系统", "Smart_car"), ("**/*.sh", "**/*.py")),
    (os.path.join("Linux车载系统", "Smart_car", "board"), ("**/*",)),
    (os.path.join("Linux车载系统", "tools"), ("**/*.py", "**/*.ps1", "**/*.sh")),
]

SKIP_DIR_PARTS = (".git", "__pycache__", "_chrome_profile_filetest", "node_modules",
                  "can_web")

#: 工具/政策文件本身不是"板端代码"，里面的示例路径不参与判定
SKIP_FILES = {"vmctl/board_paths.py", "vmctl/check_board_artifact_paths.py"}

#: 自动判不出执行侧、但**人工已核实**的文件（每条都要有理由）
FILE_SIDE = {
    "vmctl/ws_forensic.py": ("host", "比对对象写在注释里：`VM /home/cjh/board_*.bin`，全程 b.vm()"),
    "vmctl/rootfs_reclaim/s1_baseline.sh": ("board", "20261015 事故回收脚本：只读 ls/stat/md5/du"),
    "vmctl/rootfs_reclaim/s2_copy.sh": ("board", "20261015 事故回收脚本：把误落文件**拷到 /userdata**（写的是 /userdata）"),
    "vmctl/rootfs_reclaim/s3_rm_small.sh": ("board", "20261015 事故回收脚本：删误落文件"),
    "vmctl/rootfs_reclaim/s4_rm_big.sh": ("board", "20261015 事故回收脚本：删误落文件"),
    "vmctl/rootfs_reclaim/s5_verify.sh": ("board", "20261015 事故回收脚本：复核 /userdata 副本"),
    "vmctl/rootfs_reclaim/s8_final_counters.sh": ("board", "20261015 事故回收脚本：收尾计数"),
    "build_out/docs/vm_amp_look.sh": ("host", "文件名 vm_ 前缀，跑在 VM 上"),
}

#: 宿主侧同类路径 —— **明确不改**（这就是"未改清单"）
HOST_SIDE_OK = {
    "vmctl/ws_shots.py": "VMWORK=/home/cjh/wsflash 是 VM 上的中转目录（adb pull 的宿主侧目标）；板端那一段是 /userdata/wsflash/",
    "vmctl/ws_fetch_shots.py": "VMROOT/STAGE=/home/cjh/wsflash 是 VM 侧中转（b.vm() 明确跑在 VM 上）；板端是 /userdata/wsflash/",
    "vmctl/ws_get.py": "VM_STAGE=/home/cjh/wsflash/stage 是 VM 侧中转；板端是 /userdata/wsflash/stage",
    "vmctl/ws_build_tools.py": "VMWORK=/home/cjh/wsflash 是 VM 上交叉编译目录；推板目标是 /userdata/wsflash/",
    "vmctl/ws_pull.py": "VMWORK=/home/cjh/wsflash 是 VM 侧中转",
    "vmctl/ws_pullglob.py": "VMWORK=/home/cjh/wsflash 是 VM 侧中转",
    "vmctl/ws_pull_bin.py": "VMWORK=/home/cjh/wsflash 是 VM 侧中转",
    "vmctl/ws_probe2_build.py": "VMWORK=/home/cjh/wsflash 是 VM 侧编译目录",
    "vmctl/ws_vmfind.py": "VMWORK=/home/cjh/wsflash 是 VM 侧目录",
    "vmctl/ws_diff19.py": "VM 侧对照目录",
    "vmctl/ws_cmp_tree.py": "VM 侧源码树比对",
    "vmctl/board_common.py": "VM_HOST=192.168.88.130 与 ADB=/home/cjh/tools/... 都是 VM 侧；板端一律 /userdata",
    "vmctl/taskA_lib.py": "VM_TMP=/home/cjh/boxtool 是 VM 侧中转",
    "vmctl/build_pageshot.py": "SRC/HOSTB 在 VM 上（交叉编译产物）",
    "vmctl/carve_mp3.py": "从 VM 上的 userdata.img 里 carve，输出到 VM 的 /home/cjh/carved",
    "vmctl/ota_deploy_tool.py": "VM 侧备份目录",
    "vmctl/rc_sh_syntax.py": "VM 侧语法检查临时目录",
    "vmctl/kernel_build_start.py": "VM 侧内核构建脚本与日志",
    "vmctl/amp_backup_and_deliver.py": "VM 侧 rollback 目录",
    "vmctl/amp_a7param.py": "VM 侧 upgrade_tool 目录",
    "vmctl/meta_board_watch.py": "adb push 的**源**是 VM 的 /home/cjh/work/ui2cap/（交叉编译产物）；推板目标是 /tmp/kmscap",
    "vmctl/meta_fake_shots.py": "WORK 是 VM 侧工作目录（sf.put/sf.get 走 VM 的 SFTP）；板端产物在 /tmp 与 /userdata/metashot",
    "vmctl/meta_phone_watch.py": "WORK 是 VM 侧工作目录；板端产物在 /userdata/metashot",
    "vmctl/meta_phone_collect.py": "WORK 是 VM 侧工作目录；板端产物在 /userdata/metashot",
    "vmctl/pf_fake_meta_v2.py": "WORK 是 VM 侧工作目录",
    "vmctl/m11_stage_kmscap.py": "WORK 是 VM 侧工作目录（sftp.put 到 VM）；推板目标是 /userdata/kmscap",
    "vmctl/taskA_06_cantool_live.py": "b.vm() 里的 /home/cjh/boxtool 是 VM 侧中转；板端抓图是 /tmp/kmscap /tmp/*.ppm",
    "vmctl/taskA_07_cantool_100.py": "同上：/home/cjh/boxtool 是 VM 侧中转",
    "vmctl/taskA_08_demo_start.py": "同上：/home/cjh/boxtool 是 VM 侧中转",
    "vmctl/taskA_09_demo_proof.py": "同上：/home/cjh/boxtool 是 VM 侧中转",
    "vmctl/taskA_10_demo_visual.py": "同上：/home/cjh/boxtool 是 VM 侧中转",
    "vmctl/r2_mtd6_compare.py": "b.vm() 内的 VM 侧对照文件",
    "vmctl/r9_budget2.py": "VM 侧基线文件",
    "vmctl/r12_cost.py": "VM 侧 M0 构建产物 out/",
    "vmctl/r15_clean.py": "VM 侧 M0 构建产物 out/",
    "vmctl/r26_floor.py": "VM 侧 M0 构建产物 out/",
    "vmctl/r32_flash.py": "VM 侧镜像文件",
    "vmctl/_m0/checks.sh": "VM 侧 RT-Thread 构建日志",
    "vmctl/_m0/delivered.sh": "VM 侧 M0 构建产物",
    "vmctl/_m0/diff_builds.sh": "VM 侧构建日志",
    "vmctl/_m0/final_build.sh": "VM 侧 M0 构建产物",
    "build_out/docs/vm_amp_cmp.sh": "脚本名 vm_：在 VM 上比较 amp 镜像，源与目标都在 VM",
    "build_out/docs/vm_dtb_look.sh": "vm_ 前缀：VM 侧读板子 mtd 到 VM 的 /tmp",
    "build_out/docs/vm_amp_look.sh": "vm_ 前缀：VM 侧查看镜像",
}

#: 板端 rootfs 上**故意**的写（部署/config 本体，不是"临时产物"）
DEPLOY_WRITES_OK = (
    ("/etc/init.d/", "开机脚本部署（项目产物本体，必须写 rootfs 才能开机自启）"),
    ("/etc/xdg/weston/", "weston 配置部署（项目产物本体）"),
    ("/etc/pulse/", "PulseAudio 配置部署（项目产物本体）"),
    ("/etc/smartcar.conf", "项目配置本体"),
    ("/etc/passwd", "pulse 用户（部署动作）"),
    ("/etc/group", "pulse 用户组（部署动作）"),
    ("/etc/shadow", "pulse 口令占位（部署动作）"),
    ("/usr/bin/", "项目工具安装位（部署动作；硬规则②要求 chmod 755）"),
    ("/var/lib/bluetooth/", "写权限探针（0 字节，当场 rm）"),
    ("/var/lib/pulse", "PulseAudio 运行期目录（部署动作）"),
)

#: 板端**故意**引用 rootfs 路径的文件（事故回收脚本等），每条有理由
BOARD_INTENT_OK = {
    "vmctl/rootfs_reclaim/s1_baseline.sh": "20261015 事故回收 · 步骤①：只读基线（ls/stat/md5/du），引用的正是**误落**在 rootfs 的文件",
    "vmctl/rootfs_reclaim/s2_copy.sh": "20261015 事故回收 · 步骤②：把误落文件 cp 到 /userdata/wsflash/shots/ 并核 md5（写的是 /userdata）",
    "vmctl/rootfs_reclaim/s3_rm_small.sh": "20261015 事故回收 · 步骤③：删小的误落文件（用户已批准）",
    "vmctl/rootfs_reclaim/s4_rm_big.sh": "20261015 事故回收 · 步骤④：先只读 dwell 再删大的误落文件（用户已批准）",
    "vmctl/rootfs_reclaim/s5_verify.sh": "20261015 事故回收 · 步骤⑤：复核",
    "vmctl/rootfs_reclaim/s8_final_counters.sh": "20261015 事故回收 · 收尾计数",
    "vmctl/board_leftover_audit2.py": "板端残留体检：**只读**列 rootfs 根目录那 3 张历史 wayland-screenshot（证据留档，不写）",
}

#: 允许的"命令位置"前缀字符（路径前面出现这些才当字面量）。
#: ⚠ 故意**不含** `>`：`<版本>/manifest.json` 这种散文里的尖括号会被误当绝对路径。
PRE_OK = set(' \t"\'=(,|%:[]')
#: 写操作指示（读日志/列目录/比较运算不算）。
#: 重定向要求 `>` 后面紧跟路径/变量，才能把 `len(x) > 1` 这类比较排除掉。
RE_WRITE = re.compile(
    r"(>>?(?=\s*[/A-Za-z\"'$])|\btee\b|\bmkdir\b|\btouch\b|\bcp\b|\bmv\b|\brm\b|\bpush\b|"
    r"\bput\(|\bsave\(|\.write\b|\bkmscap\b|\bkmsprobe\b|\bscreenshooter\b|open\([^)]*[\"']w[\"'])")
RE_ABS = re.compile(r"(/[A-Za-z0-9_.${}%+-][A-Za-z0-9_.${}%+/-]*)")
RE_PLACEHOLDER = re.compile(r"%[-0-9.]*[sdr]|\$\{[A-Za-z_][A-Za-z0-9_]*\}|\$[A-Za-z_][A-Za-z0-9_]*")
#: 注释/叙述行（⚠ 不能把 `"` 当注释起点：Python 里板端命令的续行就是以引号开头的）
RE_COMMENT = re.compile(r"^\s*(#|//|\*|<!--|;)")
#: `"$GUARD"/x*.png` 这种"引号收尾接路径后缀"不是绝对路径字面量
RE_VAR_SUFFIX = re.compile(r"[\"'][^\"']*\$[^\"']*[\"']\s*$")
RE_NARRATIVE = ("报", "unauthorized", "为什么", "说明", "注释", "见 ", "指 ", "例如",
                "strings ", "which ", "command -v", "--help", "-h ", "ls -l /usr/bin",
                "help", "usage")
RE_HAS_CD_USERDATA = re.compile(r"cd\s+[^\n]*?/userdata")
#: `GUARD=/userdata/wsflash ... cd "$GUARD"` 这种"CWD 护栏变量"也算有护栏
RE_GUARD_ASSIGN = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=(/userdata[A-Za-z0-9_./]*)")


def has_cwd_guard(text):
    """文件里有没有把 CWD 钉在 /userdata 的护栏（字面 `cd /userdata` 或 `GUARD=/userdata…` + `cd "$GUARD"`）。"""
    if RE_HAS_CD_USERDATA.search(text):
        return True
    m = RE_GUARD_ASSIGN.search(text)
    if not m:
        return False
    return re.search(r'cd\s+"?\$%s\b' % re.escape(m.group(1)), text) is not None


def iter_files():
    for root_rel, pats in SCOPE:
        root = os.path.join(REPO, root_rel)
        if not os.path.isdir(root):
            continue
        for dirpath, dirnames, filenames in os.walk(root):
            if any(p in dirpath.split(os.sep) for p in SKIP_DIR_PARTS):
                continue
            dirnames[:] = [d for d in dirnames if d not in ("__pycache__", ".git")]
            for fn in filenames:
                rel = os.path.relpath(os.path.join(dirpath, fn), REPO).replace(os.sep, "/")
                if not any(_match(fn, p) for p in pats):
                    continue
                if rel in SKIP_FILES:
                    continue
                yield os.path.join(dirpath, fn), rel


def _match(name, pat):
    if pat == "**/*":
        return True
    if pat.startswith("**/*."):
        return name.endswith(pat[4:])
    if pat.startswith("**/"):
        return name == pat[3:]
    return name == pat


def reach_of(text, rel):
    if rel in FILE_SIDE:
        return FILE_SIDE[rel][0]
    if rel.startswith("vmctl/_m0/"):
        return "host"                                  # RT-Thread/M0 构建分析，跑在 VM 上
    has_board, has_vm = BOARD_HOST in text, VM_HOST in text
    if has_board:
        return "board"
    # ⚠ 先判"是否通过 adb 在驱动板子"，再判 VM：很多脚本 SSH 到 VM 只为拿 adb，
    #   它们的 /home/cjh/... 是 VM 侧、而 adb shell 里的路径是板端 —— 属 MIXED。
    if any(m in text for m in ("adb shell", "%s shell", " shell ", "shell '", 'shell "',
                               "def adb(", "adb(", "adb %s", "adb push", "adb pull",
                               "board_common", "b.board(", "b.adb(", "LocalBoard",
                               "ADB =", "ADB=")):
        return "mixed"
    if has_vm:
        return "host"
    if rel.startswith("Linux车载系统/Smart_car/board/"):
        return "board"
    return "unknown"


def _strip_noise(line):
    return (line.replace("2>&1", " ").replace("1>&2", " ")
            .replace(">/dev/null", " ").replace(">/dev/null2", " "))


def is_write(line):
    return bool(RE_WRITE.search(_strip_noise(line)))


def cwd_hazard(line):
    """`weston-screenshooter` 不带输出参数 ⇒ 产物落 CWD（板端 CWD=/ = rootfs）。"""
    if "weston-screenshooter" not in line:
        return None
    if any(n in line for n in RE_NARRATIVE):
        return None
    code = _strip_noise(line)
    pos = code.index("weston-screenshooter")
    head = code[:pos].rstrip()
    # 只认"命令位置"：行首、或 ; | & ( $( 之后、或 `VAR=value cmd` 形式。
    # 这样 `print("=== weston-screenshooter 直跑 ===")`、`for t in weston-...`、
    # 以及文档里的反引号引用都不会被误判成调用。
    cmd_pos = (head == "" or head.endswith((";", "|", "&", "(", "$("))
               or re.search(r"(?:^|\s)[A-Za-z_][A-Za-z0-9_]*=\S*$", head) is not None)
    if not cmd_pos:
        return None
    seg = code[pos + len("weston-screenshooter"):]
    seg = seg.split("|")[0].split("2>")[0]
    if re.search(r"\s(/|\$GUARD|%s|\{)", seg):      # 已有输出参数（绝对路径/变量）
        return None
    return ("weston-screenshooter 不带输出参数 ⇒ 默认把 wayland-screenshot-<时间戳>.png "
            "写进**当前工作目录**；板端 adb shell 的 CWD 是 `/` = rootfs")


def line_kind(line):
    """从一行里挑"产物路径字面量"；返回 [(path, kind, reason)]。"""
    out = []
    for m in RE_ABS.finditer(line):
        start = m.start()
        prev = line[start - 1] if start else " "
        if prev not in PRE_OK:
            continue
        if prev in "\"'":                      # `A + "/x"`、`"$GUARD"/x` 这种拼接片段判不了 ⇒ 跳过
            head = line[:start - 1].rstrip()
            if head.endswith("+") or head.endswith(",") or RE_VAR_SUFFIX.search(line[:start]):
                continue
        lit = m.group(1).rstrip(".,;:)'\"`")
        if len(lit) < 5 or not _looks_like_artifact(lit):
            continue
        norm = RE_PLACEHOLDER.sub("X", lit)
        d = norm.rsplit("/", 1)[0] + "/"
        probe = d if d != "/" else norm
        kind, reason = classify(probe, side="board")
        out.append((lit, kind, reason))
    return out


def push_source_span(line):
    """`adb push <源> <目标>`：返回源的字符区间（源是**宿主侧**）。"""
    m = re.search(r"\bpush\b\s+(['\"]?)([^\s'\"]+)", line)
    if not m:
        return None
    return (m.start(2), m.end(2))


def scan():
    F = {k: [] for k in ("ERROR", "WARN", "HOST", "DEPLOY", "UNKNOWN")}
    enc_notes, skipped_unknown = [], []
    for full, rel in iter_files():
        raw = open(full, "rb").read()
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError:
            text = raw.decode("gbk", errors="replace")
            enc_notes.append(rel)
        reach = reach_of(text, rel)
        file_has_guard = has_cwd_guard(text)
        for i, line in enumerate(text.splitlines(), 1):
            if RE_COMMENT.match(line):
                continue
            narrative = any(n in line for n in RE_NARRATIVE)
            if "://" in line:                    # URL 路径不是文件系统路径
                continue
            if not narrative and is_write(line):
                hz = cwd_hazard(line)
                if hz:
                    if reach in ("board", "mixed") and not file_has_guard:
                        F["ERROR"].append((rel, i, "weston-screenshooter(无输出参数)", hz, reach))
                    elif reach == "unknown":
                        F["UNKNOWN"].append((rel, i, "weston-screenshooter(无输出参数)",
                                             hz + "；且该文件判不出执行侧", reach))
            if narrative:
                continue
            if not is_write(line):
                continue
            if "sftp." in line:            # sftp 走的是 VM 的 SSH 通道（本仓库约定）
                continue
            # 本仓库的调用约定：vm()/b.vm() 跑在 VM 上，board()/b.board() 跑在板子上。
            line_reach = reach
            if re.search(r"\b(board|b\.board)\(", line) and "vm(" not in line:
                line_reach = "board"
            elif re.search(r"\b(vm|b\.vm)\(", line) and not re.search(r"\b(board|b\.board)\(", line):
                F["HOST"].append((rel, i, "-", "该行走 vm()/b.vm() —— VM 侧命令，不适用硬规则 ③", reach))
                continue
            src = push_source_span(line)
            for lit, kind, reason in line_kind(line):
                pos = line.find(lit)
                if src and src[0] <= pos < src[1]:
                    F["HOST"].append((rel, i, lit,
                                      "adb push 的**源**在宿主侧（"
                                      + HOST_SIDE_OK.get(rel, "VM/PC 上的文件") + "）", line_reach))
                    continue
                if rel in HOST_SIDE_OK or line_reach == "host":
                    F["HOST"].append((rel, i, lit,
                                      HOST_SIDE_OK.get(rel, "宿主（VM/PC）侧路径：" + reason), line_reach))
                    continue
                if kind == "userdata":
                    continue
                if kind == "tmpfs":
                    F["WARN"].append((rel, i, lit, reason, line_reach))
                    continue
                if rel in BOARD_INTENT_OK:
                    F["DEPLOY"].append((rel, i, lit, BOARD_INTENT_OK[rel], line_reach))
                    continue
                if kind == "rootfs" and any(lit.startswith(p) for p, _ in DEPLOY_WRITES_OK):
                    why = next(w for p, w in DEPLOY_WRITES_OK if lit.startswith(p))
                    F["DEPLOY"].append((rel, i, lit, why, line_reach))
                    continue
                if line_reach == "unknown":
                    F["UNKNOWN"].append((rel, i, lit,
                                         "文件既无板子地址也无 VM 地址，判不出执行侧；" + reason, line_reach))
                    continue
                if kind == "unknown":
                    # 执行侧确定是板端，但路径不在已知的 tmpfs/rootfs 清单里 ⇒ 只报"待人工确认"，
                    # 不硬判成 rootfs（**不许猜**）。
                    F["UNKNOWN"].append((rel, i, lit,
                                         "执行侧=板端，但该路径不在已知的 tmpfs/rootfs 清单里，"
                                         "需人工上板 `df <path>` 确认；" + reason, line_reach))
                    continue
                F["ERROR"].append((rel, i, lit, reason, line_reach))
    return F, enc_notes


TITLES = [
    ("ERROR", "🔴 ERROR —— 板端产物写到了 rootfs（必须改成 /userdata）"),
    ("UNKNOWN", "⚪ UNKNOWN —— 判不出板端/宿主，**不许猜**，需人工确认"),
    ("WARN", "🟡 WARN —— 板端产物写到 tmpfs（不占 rootfs，重启即失；允许保留）"),
    ("HOST", "🟢 HOST —— 宿主（VM/PC）侧路径：**不是问题，不改**"),
    ("DEPLOY", "🔵 DEPLOY —— 板端 rootfs 上的**故意**写入 / 事故回收脚本（有理由）"),
]


def render(F, enc_notes, as_md=False):
    buf = io.StringIO()
    for key, title in TITLES:
        rows = F[key]
        buf.write(("\n## " if as_md else "\n=== ") + "%s（%d）\n" % (title, len(rows)))
        if as_md:
            buf.write("\n| 文件 | 行 | 路径/调用 | 判定 | 执行侧 |\n|---|---:|---|---|---|\n")
            for r in rows:
                buf.write("| `%s` | %d | `%s` | %s | %s |\n" % r)
        else:
            for r in rows:
                buf.write("  %-44s :%-4d %-36s %s\n" % (r[0], r[1], r[2], r[3]))
    n_err = len(F["ERROR"])
    buf.write(("\n**" if as_md else "\n") + "verdict: %s（ERROR=%d WARN=%d UNKNOWN=%d HOST=%d DEPLOY=%d）"
              % ("FAIL" if n_err else "PASS", n_err, len(F["WARN"]), len(F["UNKNOWN"]),
                 len(F["HOST"]), len(F["DEPLOY"])) + ("**\n" if as_md else "\n"))
    if enc_notes:
        buf.write("非 UTF-8（按 GBK 解码）文件：%s\n" % ", ".join(sorted(set(enc_notes))))
    return buf.getvalue()


def selftest():
    """自检：确认这套护栏**真的能抓到**事故那一类写法（而不是"什么都放过"）。

    `python vmctl/check_board_artifact_paths.py --selftest` ⇒ 全部 OK 才退出码 0。
    """
    cases = [
        ("classify /home = rootfs", classify("/home/cjh/wsflash/shots/", side="board")[0], "rootfs"),
        ("classify /root = rootfs", classify("/root/", side="board")[0], "rootfs"),
        ("classify /userdata = userdata", classify("/userdata/wsflash/", side="board")[0], "userdata"),
        ("classify /tmp = tmpfs", classify("/tmp/", side="board")[0], "tmpfs"),
        ("classify /home(host) = host", classify("/home/cjh/wsflash/", side="host")[0], "host"),
        ("cwd: 裸 weston-screenshooter", bool(cwd_hazard("weston-screenshooter 2>&1 | tail -3")), True),
        ("cwd: 带 /userdata 参数", bool(cwd_hazard("weston-screenshooter /userdata/wsflash/s.png")), False),
        ("cwd: 文档叙述不算", bool(cwd_hazard('para("weston-screenshooter 报 unauthorized")')), False),
        ("cwd: strings 探针不算", bool(cwd_hazard("strings /usr/bin/weston-screenshooter | head -5")), False),
        ("line: 事故写法命中 rootfs",
         any(k == "rootfs" for _, k, _ in
             line_kind('board("mkdir -p /home/cjh/wsflash/shots; kmscap /home/cjh/wsflash/shots/t_0.ppm")')), True),
        ("line: /tmp 归类 tmpfs",
         any(k == "tmpfs" for _, k, _ in line_kind('board("kmscap /tmp/t_0.ppm")')), True),
        ("line: /userdata 干净",
         [k for _, k, _ in line_kind('board("kmscap /userdata/wsflash/t_0.ppm")')], ["userdata"]),
        ("line: README/注释里的反引号不算",
         line_kind('para("见 `kmscap /home/x/y.ppm`")')[0][1], "rootfs"),
        ("push: 认得出源段", push_source_span("adb push /home/cjh/a /userdata/a") is not None, True),
        ("guard: 字面 cd /userdata", has_cwd_guard("cd /userdata/wsflash"), True),
        ("guard: GUARD 变量 + cd", has_cwd_guard('GUARD=/userdata/wsflash\ncd "$GUARD"'), True),
        ("guard: cd /tmp 不算护栏", has_cwd_guard("cd /tmp"), False),
    ]
    bad = 0
    for name, got, want in cases:
        ok = got == want
        if not ok:
            bad += 1
        print("  %-34s %s  got=%r want=%r" % (name, "OK  " if ok else "FAIL", got, want))
    print("\n自检 %s（失败 %d / %d）" % ("PASS" if not bad else "FAIL", bad, len(cases)))
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--md", help="同时把清单写成 Markdown")
    ap.add_argument("--selftest", action="store_true", help="只跑护栏自检（不扫仓库）")
    args = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if args.selftest:
        return 1 if selftest() else 0
    F, enc = scan()
    sys.stdout.write(render(F, enc))
    if args.md:
        with open(args.md, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(render(F, enc, as_md=True))
        print("已写 %s" % args.md)
    return 1 if F["ERROR"] else 0


if __name__ == "__main__":
    sys.exit(main())
