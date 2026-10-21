#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
pcanbasic_bridge.py —— 独立桥接进程（**只用 PEAK 驱动自带的 PCANBasic.py**）
===============================================================================
为什么要单独有一个：
    python-can 的 pcan 后端在 **import 期**就去加载 PEAK 的 PCANBasic.dll，
    加载不到（没装 `python-can[pcan]` extra，或 PCANBasic.dll 不在）就把整个子模块吞掉，
    于是报 `module 'can.interfaces' has no attribute 'pcan'`，桥接起不来。
    而用户机器上 PCAN-View 往往是能用的（PEAK 驱动装了），这时直接调
    PEAK 自带的 PCANBasic.py 就能收发。

设计原则（**完全隔离**）：
  · 本文件是独立进程，**不修改 can_bridge.py**；
  · 只 `import` 它的纯逻辑：Controller（定时器/计数/错误上报）、Handler（HTTP 路由）、
    make_server、preset_frames、VERSION —— 所以两个桥接的 **API 字段与语义天然一致**；
  · 硬件后端换成 PCANBasic（见下面 PcanBasicBus）。

用法（和 can_bridge.py 的参数保持一致）：
    python pcanbasic_bridge.py --channel PCAN_USBBUS1 --bitrate 500000 --port 8765
    浏览器开 http://127.0.0.1:8765/     ← 前端就是同目录的 index.html/app.js/style.css

找 PCANBasic.py 的顺序：
    1) 已经在 sys.path 里（你设了 PYTHONPATH）→ 直接 import
    2) 环境变量 %PCANBASIC% / $PCANBASIC 指到的目录
    3) 本文件所在目录
    4) PEAK 常见安装目录：
       C:\\Program Files\\PEAK-System\\PCAN-Basic\\PCAN-Basic API\\Samples\\Python
       C:\\Program Files (x86)\\PEAK-System\\... （含各种多套一层的写法）
"""

from __future__ import print_function, division

import argparse
import glob
import json
import os
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

# ---- 复用已验证过的纯逻辑（**只读 import，绝不改那个文件**）-------------------
try:
    from can_bridge import (Controller, Handler, make_server, preset_frames,
                            VERSION, hexs)
except ImportError as e:                       # 找不到就把话说清楚
    sys.stderr.write(
        "!! 需要同目录的 can_bridge.py（只 import 它的 Controller/Handler，不会改它）：%s\n" % e)
    raise SystemExit(2)

# =============================================================== 找 PCANBasic.py
PCANBASIC_SEARCH_DIRS = [
    r"C:\Program Files\PEAK-System\PCAN-Basic\PCAN-Basic API\Samples\Python",
    r"C:\Program Files (x86)\PEAK-System\PCAN-Basic\PCAN-Basic API\Samples\Python",
    r"C:\Program Files\PEAK-System\PCAN-Basic\PCAN-Basic API\Python",
    r"C:\Program Files (x86)\PEAK-System\PCAN-Basic\PCAN-Basic API\Python",
    r"C:\Program Files\PEAK-System\PCAN-Basic\PCAN-Basic API",
    r"C:\Program Files (x86)\PEAK-System\PCAN-Basic\PCAN-Basic API",
    r"C:\Program Files\PEAK-System\PCAN-Basic API\Samples\Python",
    r"C:\Program Files (x86)\PEAK-System\PCAN-Basic API\Samples\Python",
    r"C:\Program Files\PEAK-System\PCAN-Basic API",
    r"C:\Program Files (x86)\PEAK-System\PCAN-Basic API",
    r"C:\PEAK-System\PCAN-Basic\Samples\Python",
    "/usr/lib/pcan-basic", "/usr/local/lib/pcan-basic",   # Linux 版也顺手试一下
]


def _rc(v):
    """把 PCANBasic 返回值统一成错误码整数。
    ⚠️ 用户机器上那个版本的 Initialize/Write/Read 返回 (码, 文本) 元组
    （Read 还会再嵌一层），直接与 PCAN_ERROR_OK 比较会把成功判成失败。"""
    while isinstance(v, (tuple, list)) and len(v) > 0:
        v = v[0]
    return v


def candidate_dirs():
    """要往 sys.path 里补的目录（保序去重）。"""
    dirs = []
    env = os.environ.get("PCANBASIC") or os.environ.get("PCANBASIC_DIR")
    if env:
        dirs.append(env)
    dirs.append(HERE)
    dirs.extend(PCANBASIC_SEARCH_DIRS)
    # 常见目录下再往下多找一层（不同版本解压结构不一样）
    extra = []
    for d in list(dirs):
        try:
            extra.extend(sorted(os.path.dirname(p)
                                for p in glob.glob(os.path.join(d, "*", "PCANBasic.py"))))
            extra.extend(sorted(os.path.dirname(p)
                                for p in glob.glob(os.path.join(d, "*", "*", "PCANBasic.py"))))
        except Exception:
            pass
    dirs.extend(extra)
    out = []
    for d in dirs:
        if d and d not in out:
            out.append(d)
    return out


def find_pcanbasic_dirs():
    """在候选目录里实际找到 PCANBasic.py 的那些目录。"""
    return [d for d in candidate_dirs() if os.path.isfile(os.path.join(d, "PCANBasic.py"))]


def import_pcanbasic(log=print):
    """import PCANBasic：先直接试，再把候选目录补进 sys.path 重试。失败抛 ImportError。"""
    tried = []
    try:
        return __import__("PCANBasic"), "sys.path 里已有"
    except Exception as e:
        tried.append("直接 import：%s: %s" % (type(e).__name__, e))
    for d in candidate_dirs():
        if d in sys.path:
            continue
        sys.path.append(d)
        try:
            mod = __import__("PCANBasic")
            log("[后端] 已把 %s 补进 sys.path 并 import PCANBasic" % d)
            return mod, d
        except Exception as e:
            tried.append("从 %s import：%s: %s" % (d, type(e).__name__, e))
    raise ImportError("找不到 PCANBasic.py；试过：\n    " + "\n    ".join(tried))


# =============================================================== PCANBasic 后端
class PcanBasicBus(object):
    """把 PEAK 的 PCANBasic 包装成 Controller 期望的后端接口：
       name / businfo() / recv(timeout) / send(can_id, data) / close()
    —— 与 can_bridge.PcanBus 完全同形，所以 Controller 一行都不用改。"""

    name = "PCANBasic"                 # 前端「后端」这一格会显示这个

    BAUDRATES = {
        1000000: "PCAN_BAUD_1M",
        800000: "PCAN_BAUD_800K",
        500000: "PCAN_BAUD_500K",
        250000: "PCAN_BAUD_250K",
        125000: "PCAN_BAUD_125K",
        100000: "PCAN_BAUD_100K",
        50000: "PCAN_BAUD_50K",
        20000: "PCAN_BAUD_20K",
        10000: "PCAN_BAUD_10K",
    }

    def __init__(self, channel, bitrate, log=print):
        self.mod, where = import_pcanbasic(log)
        cls = getattr(self.mod, "PCANBasic", None)
        if cls is None:
            raise RuntimeError("PCANBasic.py 里没有 PCANBasic 类（版本不对？）")
        # ⚠️ 实测（用户机器上的那版 PCANBasic.py）：
        #   1) Initialize 是**老式签名** (Channel, Btr0Btr1, hwType, ioport, interrupt)，
        #      必须用**实例**调用；在类上直接调会把第一个参数当 self →
        #      "missing 1 required positional argument: 'Btr0Btr1'"。
        #   2) PCAN_* 常量只挂在**模块**上，类/实例上没有 → 这里把常量抄到实例上，
        #      下面统一用 self._api.PCAN_xxx 就不会 AttributeError。
        try:
            inst = cls()
        except Exception:
            inst = cls
        for k in dir(self.mod):
            if k.startswith("PCAN_") and not hasattr(inst, k):
                try:
                    setattr(inst, k, getattr(self.mod, k))
                except Exception:
                    pass
        self._api = inst
        api = inst
        self._lock = threading.Lock()

        # ---- 通道 ----
        self.channel_arg = channel
        if isinstance(channel, int):
            ch = channel
        else:
            s = str(channel).strip()
            if s.lower().startswith("0x") or s.isdigit():
                ch = int(s, 0)
            else:
                # ⚠️ 实测：某些版本的常量只挂在**模块**上、类上没有（反之亦然），
                #    而且值是 ctypes 类型（c_ushort(81)），不是 int —— 两处都要兜住。
                src = api if hasattr(api, s) else (self.mod if hasattr(self.mod, s) else None)
                if src is None:
                    raise RuntimeError(
                        "PCANBasic 里没有通道常量 %s（可用 PCAN_USBBUS1..8 / 0x51.. 这类）" % s)
                ch = getattr(src, s)
        self.handle = ch

        # ---- 波特率 ----
        rate_name = self.BAUDRATES.get(int(bitrate))
        if rate_name is None:
            raise RuntimeError(
                "PCANBasic 只内置这些波特率：%s；%d bit/s 请先用 PCAN-View 设定好"
                % ("/".join("%dK" % (k // 1000) for k in sorted(self.BAUDRATES, reverse=True)),
                   bitrate))
        rate = getattr(api, rate_name, None)
        if rate is None:
            rate = getattr(self.mod, rate_name, None)   # 同通道：常量可能只在模块上
        if rate is None:
            raise RuntimeError("PCANBasic 里没有 %s 常量" % rate_name)

        res = api.Initialize(ch, rate)
        if _rc(res) != api.PCAN_ERROR_OK:
            raise RuntimeError("PCANBasic.Initialize(%s, %s) 失败：%s"
                               % (channel, rate_name, self._fmt_error(res)))
        self.rate_name = rate_name
        self.description = "PCANBasic · PEAK 驱动%s · 通道 %s @ %s" % (
            ("（%s）" % where) if where else "", channel, rate_name)

    # ---- 给 /api/status 用 ----
    def businfo(self):
        return self.description

    def _fmt_error(self, res):
        """⚠️ 实测（用户机器上的 PCANBasic.py，Python 3.12/64 位）：
        这个版本的 Initialize/Write/Read **返回 (错误码, 文本) 元组**，错误文本函数叫
        `GetErrorText` 而**不是** `GetFormattedError`。两处差异都要兜住，否则会把
        "成功"误判成"失败"。"""
        code = _rc(res)
        text = res[1] if isinstance(res, (tuple, list)) and len(res) > 1 else None
        for name in ("GetErrorText", "GetFormattedError"):
            fn = getattr(self._api, name, None)
            if fn is None:
                continue
            try:
                out = fn(code)
                if isinstance(out, (tuple, list)) and len(out) > 1:
                    out = out[1]
                return str(out)
            except Exception:
                continue
        return "错误码 %s%s" % (code, (" (%s)" % text) if text else "")

    def _new_msg(self):
        """TPCANMsg 在不同版本里构造方式不同，几种都兜住。"""
        for args in ((), (0, 0, 0, 0, [0] * 8), (0, 0, 0, 0, 0)):
            try:
                msg = self.mod.TPCANMsg(*args)
                break
            except Exception:
                msg = None
        if msg is None:
            raise RuntimeError("构造 TPCANMsg 失败（PCANBasic.py 版本不兼容？）")
        try:
            if len(msg.DATA) != 8:
                msg.DATA = [0] * 8
        except Exception:
            pass
        return msg

    def recv(self, timeout=0.05):
        """收帧。PCAN_ERROR_QRCVEMPTY = 队列空（正常，不是错误）。
        新版本 Read 返回 3 元组 (res, msg, timestamp)，老版本 2 元组，都兼容。"""
        out = []
        end = time.time() + timeout
        while True:
            with self._lock:
                res = self._api.Read(self.handle)
            if isinstance(res, tuple) and len(res) >= 2:
                code, msg = res[0], res[1]
            else:
                code, msg = res, None
            if _rc(code) == self._api.PCAN_ERROR_OK:
                if msg is not None:
                    out.append((msg.ID, bytes(msg.DATA[:msg.LEN])))
            elif code == self._api.PCAN_ERROR_QRCVEMPTY:
                pass                                  # 没帧了
            else:
                raise RuntimeError("PCANBasic.Read 失败：%s" % self._fmt_error(code))
            if time.time() >= end:
                break
            time.sleep(0.002)
        return out

    def send(self, can_id, data):
        """真发。异常带上 ID/数据原文，交给 Controller 记进 tx_fail / status.error。
        （注意：格式化参数必须与占位符一一对应，否则会把真错盖掉 —— 踩过。）"""
        try:
            msg = self._new_msg()
            msg.ID = can_id
            msg.LEN = len(data)
            msg.MSGTYPE = self._api.PCAN_MESSAGE_STANDARD
            for i, b in enumerate(data):
                msg.DATA[i] = b
            with self._lock:
                res = self._api.Write(self.handle, msg)
        except Exception as e:
            raise RuntimeError("总线发送失败 0x%03X [%s]：%s: %s"
                               % (can_id, " ".join("%02X" % b for b in data),
                                  type(e).__name__, e))
        if _rc(res) != self._api.PCAN_ERROR_OK:
            raise RuntimeError("总线发送失败 0x%03X [%s]：PCANBasic.Write 返回 %s"
                               % (can_id, " ".join("%02X" % b for b in data),
                                  self._fmt_error(res)))
        return True

    def close(self):
        try:
            self._api.Uninitialize(self.handle)
        except Exception:
            pass


# =============================================================== 启动
def print_banner(args, ctl):
    print("=" * 74)
    print(" RK3506B 车机 CAN 调试台 · PCANBasic 专用桥接 %s" % VERSION)
    print("=" * 74)
    print(" 后端   : %s" % ctl.bus.businfo())
    print(" 通道   : %s（PCANBasic 常量 %s）"
          % (args.channel, hex(getattr(ctl.bus.handle, "value", 0))))
    print(" 波特率 : %d bit/s → %s   （板子侧固定 500000，务必一致）"
          % (args.bitrate, ctl.bus.rate_name))
    print(" 界面   : http://127.0.0.1:%d/" % args.port)
    print(" 说明   : 这个桥接只走 PEAK 的 PCANBasic.py，不经过 python-can")
    print(" 排错   : 页面顶栏看 后端/TX 成功/TX 失败；发送失败原文会进页面日志区")
    print(" Ctrl-C 退出")
    print("-" * 74)


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="RK3506B 车机 CAN 调试台 · 只用 PEAK PCANBasic.py 的独立桥接")
    ap.add_argument("--channel", default="PCAN_USBBUS1",
                    help="PCAN 通道常量名（PCAN_USBBUS1）或数值（0x51）")
    ap.add_argument("--bitrate", type=int, default=500000, help="波特率，默认 500000")
    ap.add_argument("--port", type=int, default=8765, help="HTTP 端口，默认 8765")
    ap.add_argument("--host", default="127.0.0.1", help="监听地址，默认只监听本机")
    ap.add_argument("--json-status", action="store_true",
                    help="起好服务后把 /api/status 的一份 JSON 打到 stdout（给自测脚本用）")
    ap.add_argument("--no-selftest", dest="selftest", action="store_false", default=True,
                    help="跳过启动时试发一帧 0x100 的自检")
    args = ap.parse_args(argv)

    try:
        bus = PcanBasicBus(args.channel, args.bitrate)
    except ImportError as e:
        print("!! 找不到 PCANBasic.py：%s" % e, file=sys.stderr)
        print("   · 判据：python -c \"import PCANBasic\" 能成功就说明可用", file=sys.stderr)
        print("   · 失败通常是 PEAK 的 PCAN-Basic 驱动/封装没装全", file=sys.stderr)
        print("   · 检查：dir C:\\Windows\\System32\\PCANBasic.dll", file=sys.stderr)
        print("   · 或把 PCANBasic.py 所在目录加进 PYTHONPATH：", file=sys.stderr)
        print(r"     set PYTHONPATH=C:\Program Files\PEAK-System\PCAN-Basic\PCAN-Basic API\Samples\Python",
              file=sys.stderr)
        print("   · 自动找过的目录：", file=sys.stderr)
        for d in candidate_dirs():
            print("       %s" % d, file=sys.stderr)
        print("   （没有硬件/驱动时想演示前端，请用 can_bridge.py --sim）", file=sys.stderr)
        return 2
    except Exception as e:
        print("!! 打开 PCANBasic 失败：%s: %s" % (type(e).__name__, e), file=sys.stderr)
        print("   逐条检查：", file=sys.stderr)
        print("     1) PEAK 的 PCAN-Basic 驱动装全了吗（System32 里要有 PCANBasic.dll）？", file=sys.stderr)
        print("     2) 分析仪插好了吗、被别的程序（PCAN-View）占用了吗？", file=sys.stderr)
        print("     3) 通道名对吗（%s）？" % args.channel, file=sys.stderr)
        print("     4) 波特率和板子一致吗（500000）？", file=sys.stderr)
        print("   （或者用 can_bridge.py --sim 用假帧先把前端跑起来）", file=sys.stderr)
        return 3

    ctl = Controller(bus, args.channel, args.bitrate)
    ctl.start()
    print_banner(args, ctl)

    # 启动自检：立刻试发一帧 0x100（车速无效），把"能不能发"当场暴露出来
    if args.selftest:
        ok, err = ctl.send_once_now(0x100, bytes([0xFF, 0, 0, 0, 0, 0, 0, 0]))
        if ok:
            print("[自检] 试发 0x100 成功 → 这条总线可以写（TX 成功计数已 +1）")
        else:
            print("[自检] 试发 0x100 失败：%s" % err)
            print("       ↑ 这就是「网页能收不能发」的典型原因：驱动/通道/被占用/总线无 ACK/权限")
        ctl.clear_error()
    print("-" * 74)

    try:
        srv = make_server(ctl, args.port, args.host)
    except OSError as e:
        print("!! 端口 %d 被占用或不可用：%s" % (args.port, e), file=sys.stderr)
        ctl.shutdown()
        return 4
    if args.json_status:
        print("STATUS_JSON " + json.dumps(ctl.status(), ensure_ascii=False), flush=True)
    try:
        srv.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        print("\n[退出] 已停发；板子 1 秒后会按超时语义把指示灯清零、车速档位转暗灰")
    finally:
        try:
            srv.shutdown()
        except Exception:
            pass
        ctl.shutdown()
    return 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass
    sys.exit(main())
