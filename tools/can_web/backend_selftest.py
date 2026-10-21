#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
backend_selftest.py —— 测「真后端」这一层（PCAN / python-can）
===============================================================================
为什么单独有这么一个文件：`--sim` 走的是 SimBus，**永远不会执行 PcanBus**。
用户在真机上遇到"能收不能发"，而真后端分支一行都没跑过，所以这里用一个
**假的 `can` 模块**（sys.modules 注入）把 PcanBus 完整跑起来：

  · 构造：python-can ≥4 的 `interface=` 与 ≤3 的 `bustype=` 两条路都要能走通；
  · 通道名 "PCAN_USBBUS1" 要转成 pcan 后端的常量；
  · bus.send 收到的是不是 (arbitration_id=..., is_extended_id=False, data=8 字节)；
  · 发送抛异常时，PcanBus 要把异常原文包好往上抛，**不能吞**；
  · Controller 要把它记进 tx_fail / status.error / 错误日志，且不中断循环；
  · /api/frames 会带 errors 给前端；
  · busy 报错恢复后还能继续发。

这一切都不需要 PCAN 硬件，也不需要真的装 python-can。

    python backend_selftest.py         # 退出码 0 = 全过
"""
from __future__ import annotations

import importlib.util
import os
import sys
import time
import types

HERE = os.path.dirname(os.path.abspath(__file__))
PASS, FAIL = [], []


def check(name, cond, detail=""):
    (PASS if cond else FAIL).append(name)
    print("  %s %s%s" % ("OK  " if cond else "FAIL", name,
                         ("   " + str(detail)[:400]) if (detail and not cond) else ""))


def load_bridge():
    spec = importlib.util.spec_from_file_location("can_bridge_mod",
                                                 os.path.join(HERE, "can_bridge.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# ------------------------------------------------------------------ 假 python-can
class FakeMessage(object):
    def __init__(self, arbitration_id=None, is_extended_id=None, data=None, **kw):
        self.arbitration_id = arbitration_id
        self.is_extended_id = is_extended_id
        self.data = bytearray(data or [])
        self.kw = kw
        self.sent = False
        self.timestamp = time.time()


class FakePcanBusClass(object):
    """假装是 can.interfaces.pcan.PcanBus，只为提供通道常量。"""
    PCAN_USBBUS1 = 0x51
    PCAN_USBBUS2 = 0x52


class FakeRealBus(object):
    """假的 pcan 设备：记录 send/recv，可以指定"下一次 send 抛什么错"。"""

    def __init__(self, **kw):
        self.kw = kw
        self.sent = []
        self.raise_on_send = None
        self.recv_queue = []
        self.shutdown_called = False
        self.recv_timeouts = []

    def send(self, msg, timeout=None):
        if self.raise_on_send is not None:
            err = self.raise_on_send
            self.raise_on_send = None
            raise err
        msg.sent = True
        self.sent.append(msg)

    def recv(self, timeout=None):
        self.recv_timeouts.append(timeout)
        if self.recv_queue:
            return self.recv_queue.pop(0)
        time.sleep(min(timeout or 0, 0.005))
        return None

    def shutdown(self):
        self.shutdown_called = True


def make_fake_can(accept_interface=True):
    mod = types.ModuleType("can")
    mod.__version__ = "4.3.1" if accept_interface else "3.3.3"
    last_bus = {}

    class _Bus(object):
        def __new__(cls, **kw):
            # python-can 4 的签名是 interface=，3 的是 bustype=；
            # 这里模拟"只认其中一种"，用来验证我们的兼容分支真的会兜底
            key = "interface" if accept_interface else "bustype"
            other = "bustype" if accept_interface else "interface"
            if other in kw:
                raise TypeError("__init__() got an unexpected keyword argument '%s'" % other)
            if key not in kw:
                raise TypeError("missing required argument '%s'" % key)
            fake = FakeRealBus(**kw)
            last_bus["bus"] = fake
            return fake

    mod.interface = types.SimpleNamespace(Bus=_Bus)
    mod.Message = FakeMessage
    mod.interfaces = types.SimpleNamespace(
        pcan=types.SimpleNamespace(PcanBus=FakePcanBusClass))
    mod._last_bus = last_bus
    return mod


def run_pcan_tests(cb):
    print("\n[1] PcanBus 构造：python-can 4（interface=）与 3（bustype=）都要通")
    for accept_interface, ver in ((True, "4.3.1"), (False, "3.3.3")):
        fake = make_fake_can(accept_interface=accept_interface)
        saved = sys.modules.get("can")
        sys.modules["can"] = fake
        try:
            bus = cb.PcanBus("PCAN_USBBUS1", 500000)
            kw = fake._last_bus["bus"].kw
            check("python-can %s 构造成功" % ver, True)
            check("python-can %s 传了 bitrate=500000" % ver, kw.get("bitrate") == 500000, kw)
            check("python-can %s 通道名转成了常量 0x51" % ver, kw.get("channel") == 0x51, kw)
            check("python-can %s businfo 可读" % ver, "pcan" in bus.businfo(), bus.businfo())
        except Exception as e:
            check("python-can %s 构造成功" % ver, False, "%s: %s" % (type(e).__name__, e))
        finally:
            if saved is not None:
                sys.modules["can"] = saved
            else:
                sys.modules.pop("can", None)

    print("\n[2] PcanBus.send 真的调用 bus.send，且报文参数正确")
    fake = make_fake_can()
    sys.modules["can"] = fake
    try:
        bus = cb.PcanBus("PCAN_USBBUS1", 500000)
        bus.send(0x100, bytes([0x42, 0x01, 0x03, 0x08, 0, 0, 0, 0]))
        real = fake._last_bus["bus"]
        check("bus.send 被调用了一次", len(real.sent) == 1, len(real.sent))
        m = real.sent[0]
        check("arbitration_id = 0x100", m.arbitration_id == 0x100, m.arbitration_id)
        check("is_extended_id = False（标准帧）", m.is_extended_id is False, m.is_extended_id)
        check("data 是 8 字节且逐字节一致",
              bytes(m.data) == bytes([0x42, 0x01, 0x03, 0x08, 0, 0, 0, 0]), bytes(m.data))
        check("返回值 True", bus.send(0x101, bytes(8)) is True)
    except Exception as e:
        check("PcanBus.send 正常路径", False, "%s: %s" % (type(e).__name__, e))

    print("\n[3] PcanBus.send 出错要原样抛（带 ID/数据/异常类型）")
    fake._last_bus["bus"].raise_on_send = OSError("BusOff: 总线关闭")
    try:
        bus.send(0x102, bytes([1, 2, 3, 0, 0, 0, 0, 0]))
        check("发送失败时抛异常", False, "居然没抛")
    except Exception as e:
        msg = str(e)
        check("发送失败时抛异常", True)
        check("异常里带 ID 0x102", "0x102" in msg, msg)
        check("异常里带数据原文", "01 02 03 00 00 00 00 00" in msg, msg)
        check("异常里带原始错误类型与文本",
              "OSError" in msg and "BusOff" in msg, msg)
    finally:
        sys.modules.pop("can", None)
        if 'bus' in dir():
            pass

    print("\n[4] PcanBus 构造失败时要带上「哪个参数/版本」的信息")
    def boom(**kw):
        raise RuntimeError("PCAN_ERROR_ILLHANDLE: 没有这个设备")
    fake2 = make_fake_can()
    fake2.interface.Bus = boom
    sys.modules["can"] = fake2
    try:
        cb.PcanBus("PCAN_USBBUS1", 500000)
        check("构造失败时抛异常", False, "居然没抛")
    except Exception as e:
        check("构造失败时抛异常", True)
        check("异常里带原始错误文本", "ILLHANDLE" in str(e), str(e))
    finally:
        sys.modules.pop("can", None)


def run_controller_tests(cb):
    print("\n[5] Controller：发送成功/失败分别计数，失败原因留证据")
    calls = {"n": 0}

    class FlakyBus(object):
        name = "fake-flaky"

        def __init__(self):
            self.sent = []
            self.fail_next = 0

        def businfo(self):
            return "fake-flaky @ 500000"

        def recv(self, timeout=0.02):
            time.sleep(min(timeout, 0.005))
            return []

        def send(self, can_id, data):
            calls["n"] += 1
            if self.fail_next > 0:
                self.fail_next -= 1
                raise RuntimeError("总线发送失败 0x%03X：OSError: BusOff" % can_id)
            self.sent.append((can_id, bytes(data)))
            return True

        def close(self):
            pass

    bus = FlakyBus()
    ctl = cb.Controller(bus, "PCAN_USBBUS1", 500000)
    ctl.start()
    try:
        # 前面 [3]/[4] 的小节也在同一个进程里跑过，所以这里一律用"增量"断言
        def base():
            s = ctl.status()
            return s["tx_ok"], s["tx_fail"]

        # 正常发 2 帧
        ok0, fail0 = base()
        ctl.queue_send(0x100, bytes([1, 2, 3, 4, 5, 6, 7, 8]), 2, 30)
        time.sleep(0.4)
        st = ctl.status()
        check("成功 2 帧 → tx_ok +2 / tx_fail 不变",
              st["tx_ok"] - ok0 == 2 and st["tx_fail"] == fail0, st)

        # 让下一帧失败
        ok1, fail1 = base()
        bus.fail_next = 1
        ctl.queue_send(0x101, bytes(8), 1, 0)
        time.sleep(0.4)
        st = ctl.status()
        check("失败 1 帧 → tx_fail +1", st["tx_fail"] - fail1 == 1, st)
        check("失败帧不计入 tx_ok（tx_ok 不变）", st["tx_ok"] == ok1, st)
        check("status.error 有失败原文", "BusOff" in st["error"], st["error"])
        errs = ctl.take_errors()
        check("错误日志里有这条（前端会显示）",
              any("0x101" in e["msg"] and "BusOff" in e["msg"] for e in errs), errs)
        check("错误日志取走后清空", ctl.take_errors() == [])
        check("status.error 仍然留着（顶栏一直红字提醒）", "BusOff" in ctl.status()["error"])

        # 恢复后还能继续发
        ok2, _ = base()
        ctl.clear_error()
        ctl.queue_send(0x102, bytes(8), 1, 0)
        time.sleep(0.3)
        st = ctl.status()
        check("恢复后继续发 → tx_ok +1 且 error 已清",
              st["tx_ok"] - ok2 == 1 and st["error"] == "", st)

        print("\n[6] 突发上限：默认 1 帧、单次最多 10 帧")
        ctl.clear()
        try:
            ctl.queue_send(0x100, bytes(8), 11, 0)
            check("count=11 被拒绝", False, "居然通过了")
        except ValueError as e:
            check("count=11 被拒绝", "最多" in str(e), str(e))
        ctl.clear()
        ctl.queue_send(0x100, bytes(8), 10, 20)
        time.sleep(0.5)
        st = ctl.status()
        check("count=10 全部发出（tx_ok=10）", st["tx_ok"] == 10, st)
        check("发完队列清空（tx_queued=0）", st["tx_queued"] == 0, st)
        # 默认次数就是 1：一次请求 = 一帧（不是 100）
        ctl.clear()
        ctl.queue_send(0x100, bytes(8))            # 不传 count
        time.sleep(0.3)
        check("queue_send 不传 count 时只发 1 帧", ctl.status()["tx_ok"] == 1, ctl.status())

        print("\n[7] 非阻塞：登记动作本身必须立即返回（不 sleep 发 N 次）")
        ctl.clear()
        t0 = time.time()
        ctl.queue_send(0x100, bytes(8), 10, 200)     # 若同步发完要 2 秒
        dt = time.time() - t0
        check("登记 count=10 耗时 < 50 ms（实测 %.1f ms）" % (dt * 1000), dt < 0.05, dt)
        st = ctl.status()
        check("登记后 tx_queued=10（后台慢慢发）", st["tx_queued"] == 10, st)
        t0 = time.time()
        ctl.start_timer(0x101, bytes(8), 100, 0)
        check("登记周期定时器耗时 < 50 ms（实测 %.1f ms）" % ((time.time() - t0) * 1000),
              (time.time() - t0) < 0.05)
        ctl.stop_all()
    finally:
        ctl.shutdown()


def main():
    cb = load_bridge()
    print("=" * 72)
    print(" PcanBus（真后端分支）+ Controller 发送路径自测（不需要 PCAN 硬件）")
    print("=" * 72)
    run_pcan_tests(cb)
    run_controller_tests(cb)
    print("\n" + "=" * 72)
    print("通过 %d 项，失败 %d 项" % (len(PASS), len(FAIL)))
    for f in FAIL:
        print("  FAIL " + f)
    return 1 if FAIL else 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass
    sys.exit(main())
