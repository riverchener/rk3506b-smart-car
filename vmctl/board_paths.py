#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""board_paths —— 「板端产物落盘铁律」的唯一实现（见 docs 硬规则 ③）。

铁律（2026-10-15 定，起因见 build_out/docs/rootfs空间告急_只读排查_20261015.md）：

    板端的**截图 / 抓帧 / 日志 / 临时产物**一律写 `/userdata/...`；
    **严禁**写 `/home`、`/root`、`/etc`、`/usr`、`/opt`、`/var/lib`、`/`（都在 rootfs 上）。

为什么（实测，不是推测）：

  * rootfs = `ubi0:rootfs`（UBIFS），`df` 容量只有 **150 MB**，常年 99% 满；
    2026-10-15 实测可用量只剩 **1.1 MB**，`ubinfo` 里 `available_eraseblocks = 0`
    （证据：build_out/docs/rootfs空间告急_只读排查_20261015.md §2）。
  * rootfs 写满之后 `/etc` 写入会失败，历史事故里 `/usr/lib/libltdl.so.7.3.1`
    被截成 **0 字节**；`S50systemui restart` 变 `Permission denied`，
    **冷启动 `rcS` 会失败 ⇒ 板子可能起不来**。
  * 反面案例（本次）：2026-10-13 21:24 一次抓屏调试把两块 `.ppm` 写进
    `/home/cjh/wsflash/shots/`（**2 764 816 B + 2 195 456 B = 4.73 MiB**，都在 rootfs 上）
    ⇒ `df` 可用量从 **1.1 MB 掉到 72 KB**，逼近"写 `/etc` 会失败"的红线。

`/tmp` 与 `/var/log` **实测是 tmpfs**（mount 表：build_out/docs/_audit_raw/ALL_audit_raw.txt:17-18
`tmpfs on /var/log` / `tmpfs on /tmp`），**不占 rootfs** ⇒ 写它们**不是**"落错分区"，
只是**重启即失**。所以本模块把它们单独归一类（WARN），不算违规；
但需要长期留档的取证产物仍然应该写 `/userdata`。

⚠ 本模块**只判断"板端"的路径**。同一串字符 `/home/cjh/...` 在 VM/宿主侧是**正常**的
（那是 VM 的 home），在板子上才是 rootfs。所以所有函数都要求显式声明 `side`
（`"board"` / `"host"`），**不允许默认瞎猜**。

用法：
    from board_paths import board_artifact, assert_board_artifact, guard_shell

    p = board_artifact("t_0.ppm")            # -> /userdata/wsflash/t_0.ppm
    assert_board_artifact(p)                 # 不在 /userdata 下就抛异常
    script = "OUT=%s\n%s\n" % (p, guard_shell("$OUT"))   # 板端 sh 里内联断言

自检（全仓静态检查）：
    python vmctl/check_board_artifact_paths.py
"""
from __future__ import annotations

# ---------------------------------------------------------------- 常量（唯一真相）
USERDATA = "/userdata"

#: 板端取证/调试产物的默认落点。沿用仓库既有约定（不要新造目录）：
#:   wsflash —— 抓屏工具与逐帧调试产物（vmctl/ws_build_tools.py、ws_s*.sh、ws_s25_cleanup.sh 都用它）
#:   metashot —— 蓝牙元数据取证（vmctl/meta_board_watch.py）
ARTIFACT_DIR = USERDATA + "/wsflash"

#: 板端**可写且持久**的位置（允许清单）。
ALLOWED_PREFIXES = (USERDATA + "/",)

#: 板端 tmpfs：写了不占 rootfs，但**重启即失**（WARN，不是违规）。
TMPFS_PREFIXES = (
    "/tmp/", "/var/log/", "/var/run/", "/run/", "/dev/shm/", "/dev/", "/proc/", "/sys/",
)

#: 板端 rootfs：严禁写产物（ERROR）。这些目录 2026-10-15 实测都在 rootfs 上
#: （证据：rootfs空间告急_只读排查_20261015.md §1 挂载表 + §3 空间构成表）。
ROOTFS_PREFIXES = (
    "/home/", "/root/", "/etc/", "/usr/", "/opt/", "/bin/", "/sbin/", "/lib/", "/lib32/",
    "/info/", "/oem/", "/data/", "/media/", "/mnt/",
    "/var/lib/", "/var/www/", "/var/empty/", "/var/spool/", "/var/cache/", "/var/lock/",
)

#: 宿主（VM / PC）侧：这些路径出现在宿主侧命令里是**正常**的，不是问题。
HOST_PREFIXES = ("/home/cjh/", "/tmp/", "/var/tmp/")


class BoardArtifactPathError(ValueError):
    """板端产物路径违反了硬规则 ③（不在 /userdata 下）。"""


def classify(path, side="board"):
    """给一个板端/宿主路径分类。

    返回 `(kind, reason)`，`kind` 取：
        `userdata`  /userdata 下（合规）
        `tmpfs`     板端 tmpfs（不是 rootfs；重启即失）
        `rootfs`    板端 rootfs（**违规**：会吃掉 150 MB 根文件系统）
        `relative`  相对路径 / 没有前导 `/`（**违规**：最终落在"当前工作目录"，
                    而板端 `adb shell` 的 CWD 是 `/`，也就是 rootfs）
        `host`      宿主侧（不管）
        `unknown`   说不清（必须人工判断，**不许猜**）
    """
    if side not in ("board", "host"):
        raise ValueError("side 只能是 'board' 或 'host'，收到 %r" % (side,))
    if path is None or path == "":
        return "unknown", "空路径"

    p = str(path)
    # 变量/占位符（$WS、%s、{tag}）没法静态判断，交给调用方
    if not p.startswith("/"):
        return "relative", "没有前导 `/`：会落到 CWD（板端 adb shell 的 CWD 实测是 `/` = rootfs）"

    if side == "host":
        return "host", "宿主（VM/PC）侧路径，不适用硬规则 ③"

    if p == USERDATA or p.startswith(USERDATA + "/"):
        return "userdata", "在 /userdata 下（合规）"
    for pre in TMPFS_PREFIXES:
        if p.startswith(pre):
            return "tmpfs", "%s 是 tmpfs（实测），不占 rootfs；但重启即失" % pre.rstrip("/")
    if p == "/":
        return "rootfs", "rootfs 根目录（板端 CWD 就在这里）"
    for pre in ROOTFS_PREFIXES:
        if p.startswith(pre):
            return "rootfs", "%s 在 rootfs 上（150 MB，常年 99%% 满）" % pre.rstrip("/")
    return "unknown", "既不在 /userdata，也不在已知的 tmpfs/rootfs 清单里 —— 请人工上板 `df <path>` 确认"


def assert_board_artifact(path, side="board", allow_tmpfs=False):
    """板端产物路径护栏：不合规就**抛异常**，绝不静默写到别处。

    `allow_tmpfs=True` 时容忍 tmpfs（用于"确实是 scratch、掉了没关系"的场合）。
    """
    kind, reason = classify(path, side=side)
    if kind == "userdata":
        return str(path)
    if kind == "tmpfs" and allow_tmpfs:
        return str(path)
    raise BoardArtifactPathError(
        "硬规则③：板端截图/抓帧/日志/临时产物必须写 %s（实际 %r —— %s）。"
        "原因：rootfs 只有 150 MB 且常年 99%% 满，写满会导致 /etc 写入失败甚至文件被截断"
        "（历史事故 libltdl.so.7.3.1 被截成 0 字节），重启可能起不来。"
        % (USERDATA, path, reason))


def board_artifact(name, sub="wsflash"):
    """拼一个合规的板端产物路径：`/userdata/<sub>/<name>`。"""
    name = str(name).lstrip("/")
    if not name:
        raise ValueError("name 不能为空")
    if ".." in name.split("/"):
        raise ValueError("name 里不能有 ..：%r" % (name,))
    return "%s/%s/%s" % (USERDATA, str(sub).strip("/"), name)


def guard_shell(path_expr="$OUT", allow_tmpfs=False):
    """生成一段**板端 sh** 断言，贴在 heredoc 里用（busybox ash 语法）。

    例：
        OUT=/userdata/wsflash/t_0.ppm
        <guard_shell("$OUT")>
        /userdata/wsflash/kmscap "$OUT" /dev/dri/card0

    不在 /userdata 下就 `exit 97`（**拒绝写**并报错，而不是静默写到 rootfs）。
    """
    pats = ["%s/*" % USERDATA]
    if allow_tmpfs:
        pats += ["%s*" % p for p in TMPFS_PREFIXES]
    return (
        'case "%s" in\n'
        '  %s) ;;\n'
        '  *) echo "FATAL(硬规则③): 板端产物必须写 %s（实际=%s）；'
        'rootfs 只有 150MB，写满会让 /etc 写入失败甚至文件被截断" >&2; exit 97 ;;\n'
        'esac'
    ) % (path_expr, "|".join(pats), USERDATA, path_expr)


def check_script(script):
    """粗筛一段**板端 sh/python 片段**里的产物路径；返回 `[(path, kind, reason)]`。

    这是给人和检查脚本用的"手电筒"，**不是**完备的静态分析：
    只认 `/...` 形式的字面量，且只回报"看起来像产物"的那些（扩展名/关键词）。
    """
    import re
    findings = []
    for m in re.finditer(r"/[A-Za-z0-9_.${}%\-/]*", script or ""):
        lit = m.group(0).rstrip("./")
        if not lit or lit == "/":
            continue
        if not _looks_like_artifact(lit):
            continue
        kind, reason = classify(lit, side="board")
        findings.append((lit, kind, reason))
    return findings


ARTIFACT_EXTS = (
    ".ppm", ".png", ".jpg", ".jpeg", ".bmp", ".gif", ".webp",
    ".log", ".txt", ".bin", ".img", ".json", ".csv", ".dump", ".trace",
)
ARTIFACT_KEYWORDS = (
    "kmscap", "kmsprobe", "screenshot", "shot", "shots", "appshot", "metashot",
    "wsflash", "frame", "rframes", "grab",
)


def _looks_like_artifact(path):
    low = path.lower()
    if low.endswith(ARTIFACT_EXTS):
        return True
    return any(k in low for k in ARTIFACT_KEYWORDS)


if __name__ == "__main__":       # 手工自测
    import sys
    for a in sys.argv[1:] or ["/home/cjh/wsflash/shots/t_0.ppm", "/userdata/wsflash/t_0.ppm",
                              "/tmp/kms.ppm", "t_0.ppm", "/"]:
        k, r = classify(a, side="board")
        print("%-42s %-9s %s" % (a, k, r))
