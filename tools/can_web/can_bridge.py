#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
can_bridge.py —— RK3506B 车机 CAN 调试台「本地桥接进程」
===============================================================================
浏览器不能直接开 PCAN，所以由本进程持有 PCAN-USB，向上给纯静态网页提供
一个极简 HTTP API：

  GET  /                     → index.html（同目录静态文件）
  GET  /api/frames           → 自上次轮询以来的新帧（JSON 数组）
                               [{"t": 毫秒,"id": 256,"data":[...8 字节...],"dir":"rx"|"tx"}, ...]
  POST /api/send             → 发帧
       {"id":256,"data":[48,1,3,0,0,0,0,0],"count":3,"period_ms":50}
       · count / period_ms 可选（默认 1 次、周期 0）
       · id 接受十进制（256）或 0x 十六进制（"0x100" / "100" 也行，字符串）
       · 批量：{"frames":[{"id":...,"data":[...],"count":..,"period_ms":..}, ...]}
       · 开车时间线：{"timeline":true,"period_ms":100} / {"timeline":false} 停
  GET  /api/status           → {"connected":true,"channel":"PCAN_USBBUS1",
                               "bitrate":500000,"rx":12,"tx":34,"error":""}
  POST /api/clear            → 清计数与缓存（前端"清空"）

依赖：**只用 Python 标准库**（http.server / threading / json / argparse）。
     真要连 PCAN-USB 时才需要 python-can：pip install "python-can[pcan]"
     没有硬件：加 --sim，用内置假帧把整套前端跑起来。

用法：
  python can_bridge.py --channel PCAN_USBBUS1 --bitrate 500000 --port 8765
  python can_bridge.py --sim                       # 无硬件演示
  python can_bridge.py --sim --sim-echo            # 发出的帧回显到接收区（默认开）
  python can_bridge.py --sim --no-sim-periodic     # 不回显、不自动发板子周期帧
  浏览器打开 http://127.0.0.1:8765/
"""

from __future__ import division, print_function

import argparse
import json
import mimetypes
import os
import random
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

VERSION = "1.0.0"
HERE = os.path.dirname(os.path.abspath(__file__))

# =============================================================== 协议常量
DOWNLINK_IDS = (0x100, 0x101, 0x102, 0x103, 0x104, 0x105)
UPLINK_IDS = (0x201, 0x202, 0x203, 0x204)

GEARS = {0: "P", 1: "R", 2: "N", 3: "D"}
WORK_MODES = {0: "经济", 1: "标准", 2: "运动", 3: "雪地"}
LIGHT_BITS = ("近光", "远光", "示宽", "前雾", "后雾", "氛围灯")
DOOR_BITS = ("左前门", "右前门", "左后门", "右后门", "后备箱", "引擎盖")
WARN_BITS = ("发动机", "胎压", "油量低", "保养", "ABS", "蓄电池", "制动", "其它")


def hexs(can_id):
    return "0x%03X" % can_id


def _hexbytes(data):
    return " ".join("%02X" % b for b in data)


# =============================================================== 后端
class SimBus(object):
    """--sim：不碰硬件。发出的帧延迟 1~4 ms 回显成 rx（模拟板子/自身回读），
    并且每秒自动发一组板子上行帧（0x201/0x202/0x203/0x204），让接收区有东西看。"""

    name = "sim"

    def __init__(self, echo=True, periodic=True):
        self.echo = echo
        self.periodic = periodic
        self.pending = []                 # 待回显：[(到期时刻, id, bytes)]
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._t0 = time.time()
        self._thread = None
        if periodic:
            self._thread = threading.Thread(target=self._periodic_loop, daemon=True)
            self._thread.start()

    # ---- 后端接口 ----
    def recv(self, timeout=0.05):
        end = time.time() + timeout
        while not self._stop.is_set() and time.time() < end:
            time.sleep(0.005)
        out = []
        now = time.time()
        with self._lock:
            keep = []
            for due, can_id, data in self.pending:
                if due <= now:
                    out.append((can_id, data))
                else:
                    keep.append((due, can_id, data))
            self.pending = keep
        return out

    def send(self, can_id, data):
        if self.echo:
            with self._lock:
                if len(self.pending) > 500:
                    self.pending = self.pending[-500:]
                self.pending.append((time.time() + 0.001 + random.random() * 0.003,
                                     can_id, bytes(data)))
        return True

    def close(self):
        self._stop.set()

    # ---- 板子周期上行（1 Hz 各一条）----
    def _periodic_loop(self):
        seq = [
            (0x201, lambda i: bytes([1, 24, 3 + (i % 3), 1, 0, 0, 0, 0])),      # 空调开 24℃ 风量 3~5 内循环
            (0x202, lambda i: bytes([1 + (i % 4), 1 + (i % 2), 50 + (i * 7) % 51, 0, 0, 0, 0, 0])),
            (0x203, lambda i: bytes([1 + (i % 2), 2, 0, 0, 0, 0, 0, 0])),        # 开近光/关远光…
            (0x204, lambda i: bytes([i % 4, 0, 0, 0, 0, 0, 0, 0])),              # 工作模式
        ]
        time.sleep(0.6)
        i = 0
        while not self._stop.is_set():
            can_id, mk = seq[i % len(seq)]
            with self._lock:
                self.pending.append((time.time(), can_id, mk(i // len(seq))))
            i += 1
            self._stop.wait(0.6)


class PcanBus(object):
    """真实硬件：python-can 的 pcan 后端（PCAN-USB）。

    ⚠️ 这一支只有在真接 PCAN 时才会被执行（--sim 走 SimBus），所以异常**一律
    原样往上抛**，由 Controller 记进 status.error / 错误日志，绝不吞掉。
    """

    name = "python-can/pcan"

    def __init__(self, channel, bitrate):
        import can  # noqa: 延迟导入，--sim 时完全不需要 python-can
        self._can = can
        self._lock = threading.Lock()
        appver = getattr(can, "__version__", "?")
        ch = channel
        # PCAN_USBBUS1 → can.interfaces.pcan.PcanBus 里对应 PCAN_USBBUS1 常量
        if isinstance(ch, str) and hasattr(can.interfaces.pcan.PcanBus, ch):
            try:
                ch = getattr(can.interfaces.pcan.PcanBus, ch)
            except Exception:
                ch = channel
        # python-can ≥ 4 用 interface=，≤ 3 用 bustype=；两个都试，报错要看得见
        attempts = [
            ("interface=", dict(interface="pcan", channel=ch, bitrate=bitrate)),
            ("bustype=", dict(bustype="pcan", channel=ch, bitrate=bitrate)),
        ]
        last = None
        for tag, kw in attempts:
            try:
                self.bus = can.interface.Bus(**kw)
                self.kw = tag + "pcan"
                break
            except TypeError as e:          # 参数名不被这个版本接受 → 换下一个
                last = e
                continue
            except Exception as e:          # 硬件/驱动/占用 → 原样抛给上层
                raise RuntimeError("python-can Bus(%s) 打不开：%s: %s"
                                   % (tag + "pcan", type(e).__name__, e))
        else:
            raise RuntimeError("python-can Bus() 参数不兼容（%s）；版本 %s"
                               % (last, appver))
        self.description = "python-can/pcan · %s · python-can %s · 通道 %s @ %d" % (
            self.kw, appver, channel, bitrate)

    def businfo(self):
        return self.description

    def recv(self, timeout=0.05):
        """一次调用里最多收一小会儿；异常原样抛出（Controller 记录）。"""
        out = []
        end = time.time() + timeout
        while True:
            with self._lock:
                msg = self.bus.recv(timeout=0.01)
            if msg is not None:
                out.append((msg.arbitration_id, bytes(msg.data)))
            if time.time() >= end:
                break
        return out

    def send(self, can_id, data):
        """真的写总线。任何异常都带上类型与原文，交给调用方记录。"""
        msg = self._can.Message(arbitration_id=can_id, is_extended_id=False,
                                data=bytes(data))
        try:
            with self._lock:
                self.bus.send(msg)
        except Exception as e:
            # 注意：格式化参数必须和数据里的 % 占位符一一对应，
            # 否则会抛 ValueError，把真正的总线错误盖掉（踩过）。
            raise RuntimeError("总线发送失败 0x%03X [%s]：%s: %s"
                               % (can_id, " ".join("%02X" % b for b in data),
                                  type(e).__name__, e))
        return True

    def close(self):
        try:
            self.bus.shutdown()
        except Exception:
            pass


# =============================================================== 总线控制
class Controller(object):
    """持有后端 + 发帧队列 + 周期定时器 + 开车时间线；供 HTTP 线程调用。

    ⚠️ 所有发送都只在这里"登记"，由 _loop 后台线程按周期真正写总线；
       HTTP 线程永远不 sleep、不阻塞，请求立即返回。
    """

    # 一次点击/一个请求最多往队列里塞多少帧（防止误点 100 次把电脑灌死）
    MAX_BURST = 10
    # 错误日志最多留多少条
    MAX_ERRORS = 20

    def __init__(self, bus, channel, bitrate, max_pending=5000):
        self.bus = bus
        self.channel = channel
        self.bitrate = bitrate
        self.max_pending = max_pending
        self.lock = threading.Lock()
        self.pending = []                 # 待前端轮询的帧
        self.counts = {"rx": 0, "tx": 0}
        self.tx_fail = 0                  # 发送失败计数（顶栏单独显示）
        self.error = ""                   # 最近一次错误（顶栏红字）
        self.errors = []                  # 错误日志 [{t, msg}]，前端拉走
        self.send_queue = []              # 待发：[id, data, left, period_ms, next_t]
        self.timers = []                  # 周期定时器：[token, id, data, period_ms, next_t, left]
        self._timer_seq = 0
        self.timeline = None              # {"t0":..,"period_ms":..,"count":..,"sent":..}
        self._stop = threading.Event()
        self._thread = None

    # ---------------- 帧入队（给前端轮询） ----------------
    def _record(self, can_id, data, direction):
        with self.lock:
            self.counts[direction] += 1
            self.pending.append({"t": int(time.time() * 1000), "id": can_id,
                                 "data": list(data), "dir": direction})
            if len(self.pending) > self.max_pending:
                self.pending = self.pending[-self.max_pending:]

    def _note_error(self, msg):
        """记一条错误：顶栏 error + 错误日志（前端拉到日志区显示）。"""
        msg = str(msg)
        with self.lock:
            self.error = msg
            self.errors.append({"t": int(time.time() * 1000), "msg": msg})
            if len(self.errors) > self.MAX_ERRORS:
                self.errors = self.errors[-self.MAX_ERRORS:]
            return msg

    def send_once_now(self, can_id, data):
        """同步发一帧（只给启动自检用，**不在 HTTP 路径上**）。
        返回 (ok, 错误文本)。"""
        try:
            self.bus.send(can_id, bytes(data))
            self._record(can_id, bytes(data), "tx")
            return True, ""
        except Exception as e:
            with self.lock:
                self.tx_fail += 1
            return False, self._note_error(
                "发送失败 0x%03X [%s]：%s"
                % (can_id, " ".join("%02X" % b for b in data), e))

    def clear_error(self):
        with self.lock:
            self.error = ""

    # ---------------- HTTP 调用的动作（全部只登记，不阻塞） ----------------
    def queue_send(self, can_id, data, count=1, period_ms=0):
        """登记 count 次发送（period_ms>0 时按周期摊开）。
        第一次**下一拍就发**（≤2 ms），HTTP 立即返回。
        单次请求的突发上限 MAX_BURST，超了直接报错，不让用户误灌总线。"""
        count = int(count)
        if count > self.MAX_BURST:
            raise ValueError("一次最多发 %d 帧（收到 %d）；要连续发请用「使能周期发送」或把次数拆开"
                             % (self.MAX_BURST, count))
        count = max(0, count)
        period_ms = max(0, min(int(period_ms), 600000))
        if count <= 0:
            return 0
        with self.lock:
            self.send_queue.append([can_id, bytes(data), count, period_ms, 0.0])
        return count

    def start_timer(self, can_id, data, period_ms, count=0):
        """周期重发。count<=0 表示一直发，直到 stop_timer/stop_all。
        同样只登记；第一次下一拍就发。"""
        period_ms = max(1, min(int(period_ms), 600000))
        with self.lock:
            self._timer_seq += 1
            token = "t%d" % self._timer_seq
            self.timers.append([token, can_id, bytes(data), period_ms, 0.0, int(count)])
        return token

    def stop_timer(self, token):
        with self.lock:
            self.timers = [t for t in self.timers if t[0] != token]

    def stop_all(self):
        with self.lock:
            self.timers = []
            self.send_queue = []
            self.timeline = None

    def start_timeline(self, period_ms=100, count=0):
        with self.lock:
            self.timeline = {"t0": time.time(), "period_ms": max(10, int(period_ms)),
                             "count": int(count), "sent": 0, "next_t": time.time()}
        return True

    def stop_timeline(self):
        with self.lock:
            self.timeline = None
        return True

    def clear(self):
        with self.lock:
            self.pending = []
            self.counts = {"rx": 0, "tx": 0}
            self.tx_fail = 0
            self.errors = []
            self.error = ""

    def status(self):
        with self.lock:
            bus_name = getattr(self.bus, "name", "?")
            info = self.bus.businfo() if hasattr(self.bus, "businfo") else bus_name
            return {"connected": self.bus is not None,
                    "channel": self.channel,
                    "bitrate": self.bitrate,
                    "backend": bus_name,
                    "businfo": info,
                    "rx": self.counts["rx"],
                    "tx": self.counts["tx"],            # = tx_ok（成功写总线的帧数）
                    "tx_ok": self.counts["tx"],
                    "tx_fail": self.tx_fail,
                    "tx_queued": sum(i[2] for i in self.send_queue) +
                                 sum((t[5] if t[5] > 0 else 0) for t in self.timers),
                    "timers": len(self.timers),
                    "timeline": bool(self.timeline),
                    "error": self.error,
                    "version": VERSION}

    def take_frames(self):
        with self.lock:
            out = self.pending
            self.pending = []
        return out

    def take_errors(self):
        with self.lock:
            out = self.errors
            self.errors = []
        return out

    def tx_ok(self):
        with self.lock:
            return self.counts["tx"]

    # ---------------- 主循环 ----------------
    def start(self):
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def _loop(self):
        drive_step = 0
        while not self._stop.is_set():
            now = time.time()
            # 1) 收
            try:
                for can_id, data in self.bus.recv(0.02):
                    self._record(can_id, data, "rx")
            except Exception as e:                       # 硬件拔了/驱动出错
                self._note_error("接收失败: %s: %s" % (type(e).__name__, e))
                time.sleep(0.5)
            # 2) 发（队列 + 周期定时器）
            due = []
            with self.lock:
                keep = []
                for item in self.send_queue:
                    if item[4] <= now:
                        due.append((item[0], item[1]))
                        item[2] -= 1
                        if item[2] > 0:
                            item[4] = now + item[3] / 1000.0
                            keep.append(item)
                    else:
                        keep.append(item)
                self.send_queue = keep
                tkeep = []
                for t in self.timers:
                    if t[4] <= now:
                        due.append((t[1], t[2]))
                        t[4] = now + t[3] / 1000.0        # 排下一次
                        left = t[5]
                        if left < 0:                      # 负数 = 一直发
                            pass
                        elif left == 0:                   # 0 = 一直发
                            t[5] = -1
                        else:
                            t[5] = left - 1               # 有限次：递减到 0
                        if t[5] != 0:                     # 还剩次数（或 -1 无限）就保留
                            tkeep.append(t)
                    else:
                        tkeep.append(t)
                self.timers = tkeep
            # 3) 开车时间线
            with self.lock:
                tl = self.timeline
                if tl is not None and tl["next_t"] <= now:
                    for can_id, data in timeline_frames(now - tl["t0"], drive_step):
                        due.append((can_id, data))
                    drive_step += 1
                    tl["sent"] += 1
                    tl["next_t"] = now + tl["period_ms"] / 1000.0
                    if tl["count"] and tl["sent"] >= tl["count"]:
                        self.timeline = None
            # 4) 真正写总线（唯一调用 bus.send 的地方；异常全部留下证据）
            for can_id, data in due:
                try:
                    self.bus.send(can_id, data)
                    self._record(can_id, data, "tx")     # 这里会 counts["tx"] += 1（= TX 成功数）
                except Exception as e:
                    with self.lock:
                        self.tx_fail += 1
                    self._note_error("发送失败 0x%03X [%s]：%s"
                                     % (can_id, " ".join("%02X" % b for b in data), e))
            time.sleep(0.002)

    def shutdown(self):
        self._stop.set()
        try:
            self.bus.close()
        except Exception:
            pass


# =============================================================== 开车时间线
# 约 30 秒一轮：怠速 → 加速到 66 → 左转 → 刹车 → 安全带未系 → 左前门开 → 倒车 → 停车
TIMELINE = [
    # (持续 ms, 说明, 0x100 参数, 0x101 门掩码/是否上锁, 0x102 灯光/AC)
    (5000, "怠速", dict(speed=0, gear="D", rpm=8), 0, True, dict(lights=0x01, ac=True, temp=24)),
    (3000, "加速到 66", dict(speed="ramp66", gear="D", rpm="ramp"), 0, True, dict(lights=0x01, ac=True, temp=24)),
    (3000, "左转（66 km/h）", dict(speed=66, gear="D", rpm=18, left=True), 0, True, dict(lights=0x01, ac=True, temp=24)),
    (3000, "刹车（66→40）", dict(speed="ramp40", gear="D", rpm="ramp", brake=True), 0, True, dict(lights=0x03, ac=True, temp=24)),
    (6000, "安全带未系 + 右转（20 km/h）", dict(speed=20, gear="D", rpm=12, seatbelt_off=True, right=True),
     0, True, dict(lights=0x01, ac=True, temp=24)),
    (3000, "左前门开", dict(speed=20, gear="D", rpm=10, right=True), 0x01, False, dict(lights=0x01, ac=True, temp=24)),
    (3000, "倒车", dict(speed=0, gear="R", reverse=True), 0x09, False, dict(lights=0x01, ac=True, temp=24)),
    (4000, "停车（手刹）", dict(speed=0, gear="P", handbrake=True, hazard=True), 0x00, True,
     dict(lights=0x01, ac=False, temp=24)),
]
TIMELINE_TOTAL_MS = sum(p[0] for p in TIMELINE)


def build_0x100(speed=0xFF, flags=0, gear=0, rpm=0):
    return bytes([0xFF if speed is None else (speed & 0xFF), flags & 0xFF,
                  gear & 0xFF, rpm & 0xFF, 0, 0, 0, 0])


def build_0x101(doors=0, locked=True):
    return bytes([doors & 0xFF, 1 if locked else 0, 0, 0, 0, 0, 0, 0])


def build_0x102(lights=0, ac=False, recirc=False, temp=24, outside=20):
    return bytes([lights & 0xFF,
                  (1 if ac else 0) | (2 if recirc else 0),
                  temp & 0xFF, (outside + 40) & 0xFF, 0, 0, 0, 0])


def timeline_frames(elapsed_s, step):
    """给定时间线内已过去的秒数与步号，返回该步要发的 3 帧。"""
    ms = elapsed_s * 1000.0
    phase_ms = ms % TIMELINE_TOTAL_MS
    acc = 0
    seg = TIMELINE[0]
    for item in TIMELINE:
        if phase_ms < acc + item[0]:
            seg = item
            local = (phase_ms - acc) / float(item[0])
            break
        acc += item[0]
    else:
        seg = TIMELINE[-1]
        local = 1.0

    _dur, _name, d100, doors, locked, d102 = seg
    d100 = dict(d100)
    sp = d100.get("speed", 0)
    if sp == "ramp66":
        sp = int(round(66 * local))
    elif sp == "ramp40":
        sp = int(round(66 - 26 * local))
    rpm = d100.get("rpm", 8)
    if rpm == "ramp":
        rpm = int(round(8 + 20 * local))
    flags = 0
    for i, key in enumerate(("left", "right", "brake", "reverse",
                             "seatbelt_off", "handbrake", "hazard")):
        if d100.get(key):
            flags |= 1 << i
    g = {"P": 0, "R": 1, "N": 2, "D": 3}.get(d100.get("gear", "D"), 3)
    return [(0x100, build_0x100(sp, flags, g, rpm)),
            (0x101, build_0x101(doors, locked)),
            (0x102, build_0x102(d102["lights"], d102["ac"], d102.get("recirc", False),
                                 d102["temp"], d102.get("outside", 20)))]


def preset_frames():
    """前端"一键预设"用的样例帧组（后备：不想用前端也能直接 POST 这些）。
    返回 {名字: [帧, ...]}，每帧 {"id":..,"data":[..],"count":..,"period_ms":..}。"""
    return {
        # 4) 里程油耗组：一条 0x103 + 一条 0x104 + 一条 0x105，500 ms 循环
        "mileage": [
            {"id": 0x103, "data": [0x10, 0x27, 0x00, 0x50, 0x00, 50, 0x58, 0x02],
             "period_ms": 500},                                   # 1000.0km / 小计8.0 / 50% / 600km
            {"id": 0x104, "data": [0x4B, 0x41, 1, 0x00, 0x00, 0x00, 0x00, 0x00],
             "period_ms": 500},                                   # 7.5 / 6.5 L/100km 标准模式 无告警
            {"id": 0x105, "data": [0x50, 0x50, 0x4F, 0x51, 0x3C, 0x3D, 0x3C, 0x3E],
             "period_ms": 500},                                   # 胎压 400/400/395/405 kPa，胎温 20/21/20/22℃
        ],
        # 5) 故障演示：发动机故障 + 胎压报警 + 故障码 P0301
        "fault": [
            {"id": 0x104, "data": [0x62, 0x55, 1, 0x03, 0x01, 0x01, 0x03, 0x00],
             "period_ms": 500},                                   # 告警 bit0|bit1（bit0=1,bit1=1→0x03）
            {"id": 0x105, "data": [0x30, 0x2C, 0x50, 0x50, 0x3C, 0x3D, 0x3C, 0x3E],
             "period_ms": 500},                                   # 左前 240kPa 偏低
        ],
        # 3) 工作模式：经济/标准/运动/雪地
        "mode_eco": [{"id": 0x204, "data": [0, 0, 0, 0, 0, 0, 0, 0], "count": 3, "period_ms": 50}],
        "mode_normal": [{"id": 0x204, "data": [1, 0, 0, 0, 0, 0, 0, 0], "count": 3, "period_ms": 50}],
        "mode_sport": [{"id": 0x204, "data": [2, 0, 0, 0, 0, 0, 0, 0], "count": 3, "period_ms": 50}],
        "mode_snow": [{"id": 0x204, "data": [3, 0, 0, 0, 0, 0, 0, 0], "count": 3, "period_ms": 50}],
    }


# =============================================================== 参数解析
def parse_int(value, field="值"):
    """宽松数字：256 / "256" / "0x100" 都收。
    字符串以 0x 开头 → 16 进制；含 A~F → 16 进制；否则 10 进制。"""
    if isinstance(value, bool):
        raise ValueError("%s 不能是布尔值" % field)
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        return int(value)
    s = str(value).strip()
    if not s:
        raise ValueError("%s 为空" % field)
    neg = s.startswith("-")
    body = s[1:] if neg else s
    try:
        if body.lower().startswith("0x"):
            n = int(body, 16)
        elif any(c in "abcdefABCDEF" for c in body):
            n = int(body, 16)
        else:
            n = int(body, 10)
    except ValueError:
        raise ValueError("%s 不是合法数字: %r" % (field, value))
    return -n if neg else n


def parse_frame(obj, allow_infinite=False):
    """校验并规整一条发送请求。返回 (id, bytes, count, period_ms)。
    allow_infinite=True 时 count=0 表示"周期发送、一直发"（周期定时器用）。"""
    if not isinstance(obj, dict):
        raise ValueError("帧必须是对象")
    if "id" not in obj:
        raise ValueError("缺少 id")
    can_id = parse_int(obj["id"], "id")
    if not (0 <= can_id <= 0x7FF):
        raise ValueError("id 必须是标准帧 11 位（0x000~0x7FF）：%r" % obj["id"])
    data = obj.get("data", [])
    if not isinstance(data, (list, tuple)):
        raise ValueError("data 必须是数组")
    if len(data) > 8:
        raise ValueError("data 最多 8 字节")
    out = []
    for i, b in enumerate(data):
        v = parse_int(b, "data[%d]" % i)
        if not (0 <= v <= 255):
            raise ValueError("data[%d] 必须在 0~255：%r" % (i, b))
        out.append(v)
    while len(out) < 8:
        out.append(0)
    count = parse_int(obj.get("count", 1), "count")
    period_ms = parse_int(obj.get("period_ms", 0), "period_ms")
    if count < 0:
        raise ValueError("count 不能为负")
    if count == 0 and not allow_infinite:
        raise ValueError("count 至少 1（周期发送请加 \"periodic\": true，count=0 表示一直发）")
    if count > 100000:
        raise ValueError("count 太大（≤100000）")
    if period_ms < 0:
        raise ValueError("period_ms 不能为负")
    return can_id, bytes(out), count, period_ms


# =============================================================== HTTP
class Handler(BaseHTTPRequestHandler):
    server_version = "can_web/" + VERSION
    protocol_version = "HTTP/1.1"
    ctl = None            # 由 make_server 注入
    static_root = HERE

    # ---- 工具 ----
    def _json(self, obj, code=200):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _text(self, text, code=200, ctype="text/plain; charset=utf-8"):
        body = text.encode("utf-8") if isinstance(text, str) else text
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _read_body(self):
        try:
            n = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            n = 0
        if n <= 0:
            return {}
        raw = self.rfile.read(n)
        if not raw:
            return {}
        try:
            return json.loads(raw.decode("utf-8"))
        except Exception as e:
            raise ValueError("body 不是合法 JSON: %s" % e)

    def _static(self, path):
        # 只允许同目录下的普通文件，杜绝 ../ 穿越
        rel = path.lstrip("/") or "index.html"
        full = os.path.normpath(os.path.join(self.static_root, rel))
        if not full.startswith(os.path.normpath(self.static_root) + os.sep):
            return self._text("403 forbidden", 403)
        if not os.path.isfile(full):
            return self._text("404 not found: %s" % rel, 404)
        ctype = mimetypes.guess_type(full)[0] or "application/octet-stream"
        if ctype.startswith("text/") or ctype in ("application/javascript", "application/json"):
            ctype += "; charset=utf-8"
        with open(full, "rb") as fp:
            body = fp.read()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):        # 安静点：只记 API 错误
        pass

    def log_error(self, fmt, *args):
        pass

    # ---- 路由 ----
    def do_GET(self):
        try:
            self._route_get()
        except (BrokenPipeError, ConnectionResetError):
            pass
        except Exception as e:
            try:
                self._json({"error": "%s: %s" % (type(e).__name__, e)}, 500)
            except Exception:
                pass

    def do_POST(self):
        try:
            self._route_post()
        except (BrokenPipeError, ConnectionResetError):
            pass
        except Exception as e:
            try:
                self._json({"ok": False, "error": "%s: %s" % (type(e).__name__, e)}, 500)
            except Exception:
                pass

    def _route_get(self):
        path = self.path.split("?", 1)[0]
        if path in ("/", "/index.html"):
            return self._static("index.html")
        if path == "/api/status":
            return self._json(self.ctl.status())
        if path == "/api/frames":
            return self._json({"frames": self.ctl.take_frames(),
                               "errors": self.ctl.take_errors(),      # 发送/接收错误也一起给前端
                               "status": self.ctl.status()})
        if path == "/api/presets":
            return self._json(preset_frames())
        if path == "/favicon.ico":
            return self._text("", 204)
        return self._static(path)

    def _route_post(self):
        path = self.path.split("?", 1)[0]
        try:
            body = self._read_body()
        except ValueError as e:
            return self._json({"ok": False, "error": str(e)}, 400)
        try:
            if path == "/api/send":
                return self._json(self._api_send(body))
            if path == "/api/clear":
                self.ctl.clear()
                return self._json({"ok": True, "status": self.ctl.status()})
            if path == "/api/timeline":
                if body.get("timeline", True):
                    self.ctl.start_timeline(body.get("period_ms", 100), body.get("count", 0))
                    return self._json({"ok": True, "timeline": True,
                                       "total_ms": TIMELINE_TOTAL_MS,
                                       "status": self.ctl.status()})
                self.ctl.stop_timeline()
                return self._json({"ok": True, "timeline": False,
                                   "status": self.ctl.status()})
            if path == "/api/stop":
                token = body.get("token")
                if token:
                    self.ctl.stop_timer(token)
                else:
                    self.ctl.stop_all()
                return self._json({"ok": True, "status": self.ctl.status()})
            if path == "/api/timer":
                can_id, data, count, period_ms = parse_frame(body, allow_infinite=True)
                token = self.ctl.start_timer(can_id, data, period_ms or 100, count)
                return self._json({"ok": True, "token": token, "id": can_id,
                                   "status": self.ctl.status()})
            return self._json({"ok": False, "error": "未知接口 %s" % path}, 404)
        except ValueError as e:
            return self._json({"ok": False, "error": str(e)}, 400)

    def _api_send(self, body):
        # 开车时间线开关
        if "timeline" in body and "id" not in body and "frames" not in body:
            if body["timeline"]:
                self.ctl.start_timeline(body.get("period_ms", 100), body.get("count", 0))
                return {"ok": True, "timeline": True, "total_ms": TIMELINE_TOTAL_MS,
                        "status": self.ctl.status()}
            self.ctl.stop_timeline()
            return {"ok": True, "timeline": False, "status": self.ctl.status()}
        # 预设名
        if "preset" in body and "id" not in body and "frames" not in body:
            allp = preset_frames()
            name = str(body["preset"])
            if name not in allp:
                return {"ok": False, "error": "未知预设 %s（可选：%s）"
                        % (name, ", ".join(sorted(allp)))}
            body = dict(body)
            body["frames"] = allp[name]
        items = body["frames"] if isinstance(body.get("frames"), list) else [body]
        if not isinstance(items, list) or not items:
            raise ValueError("没有要发的帧（给 id/data，或用 frames 数组）")
        batched = isinstance(body.get("frames"), list)
        if not batched and not isinstance(items[0], dict):
            raise ValueError("帧必须是对象：{\"id\":256,\"data\":[...]}")
        queued, tokens, rejected = [], [], []
        for it in items:
            periodic = bool(isinstance(it, dict) and it.get("periodic"))
            try:
                can_id, data, count, period_ms = parse_frame(it, allow_infinite=periodic)
            except ValueError as e:
                if not batched:
                    raise                        # 单帧请求：参数错就直接 400，别静默丢弃
                rejected.append({"req": it, "error": str(e)})
                continue
            if periodic:                                 # 周期发送 → 注册定时器
                tok = self.ctl.start_timer(can_id, data, period_ms or 100, count)
                tokens.append({"token": tok, "id": can_id})
            else:
                self.ctl.queue_send(can_id, data, count, period_ms)   # 只登记，不阻塞
            queued.append({"id": can_id, "data": list(data),
                           "count": count, "period_ms": period_ms})
        if not queued:
            return {"ok": False, "rejected": rejected,
                    "error": rejected[0]["error"] if rejected else "没有可发的帧",
                    "status": self.ctl.status()}
        return {"ok": True, "queued": queued, "timers": tokens,
                "repeats": sum(q["count"] for q in queued),
                "rejected": rejected,
                # 前端拿这个当基线，之后对比 /api/status 就能说清"发出去没有"
                "tx_before": self.ctl.tx_ok(),
                "status": self.ctl.status()}


# =============================================================== 启动
def make_server(ctl, port, host):
    Handler.ctl = ctl
    srv = ThreadingHTTPServer((host, port), Handler)
    srv.daemon_threads = True
    return srv


def print_banner(args, ctl):
    print("=" * 74)
    print(" RK3506B 车机 CAN 调试台 · 本地桥接 %s" % VERSION)
    print("=" * 74)
    if args.sim:
        print(" 后端   : --sim 内置假帧（不碰 PCAN 硬件）")
        print("          回显发出的帧 : %s" % ("开" if args.sim_echo else "关"))
        print("          板子周期上行 : %s" % ("开（约 1.7 Hz）" if args.sim_periodic else "关"))
        print("          ⚠ 这个模式不会真的碰总线：只用来演示/自测前端")
    else:
        print(" 后端   : %s" % (ctl.bus.businfo() if hasattr(ctl.bus, "businfo")
                                 else getattr(ctl.bus, "name", "?")))
    print(" 通道   : %s" % args.channel)
    print(" 波特率 : %d bit/s   （板子侧固定 500000，务必一致）" % args.bitrate)
    print(" 界面   : http://127.0.0.1:%d/" % args.port)
    for can_id in DOWNLINK_IDS:
        print("   下行 %s 由前端发送" % hexs(can_id))
    print("   上行 %s 会显示在接收区" % " ".join(hexs(i) for i in UPLINK_IDS))
    print(" 排错   : 页面顶栏看 后端/TX 成功/TX 失败；发送失败原文会进页面日志区")
    print(" Ctrl-C 退出")
    print("-" * 74)


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="RK3506B 车机 CAN 调试台本地桥接（纯标准库 HTTP + python-can/pcan）")
    ap.add_argument("--channel", default="PCAN_USBBUS1",
                    help="python-can pcan 通道名，如 PCAN_USBBUS1 / PCAN_USBBUS2")
    ap.add_argument("--bitrate", type=int, default=500000, help="波特率，默认 500000")
    ap.add_argument("--port", type=int, default=8765, help="HTTP 端口，默认 8765")
    ap.add_argument("--host", default="127.0.0.1", help="监听地址，默认只监听本机")
    ap.add_argument("--sim", action="store_true",
                    help="用内置假帧（没有 PCAN 硬件时也能演示前端）")
    ap.add_argument("--sim-echo", dest="sim_echo", action="store_true", default=True,
                    help="sim：发出的帧回显到接收区（默认开）")
    ap.add_argument("--no-sim-echo", dest="sim_echo", action="store_false")
    ap.add_argument("--sim-periodic", dest="sim_periodic", action="store_true", default=True,
                    help="sim：每秒自动发板子上行帧 0x201~0x204（默认开）")
    ap.add_argument("--no-sim-periodic", dest="sim_periodic", action="store_false")
    ap.add_argument("--json-status", action="store_true",
                    help="起好服务后把 /api/status 的一份 JSON 打到 stdout（给自测脚本判断参数用）")
    ap.add_argument("--no-selftest", dest="selftest", action="store_false", default=True,
                    help="跳过启动时试发一帧 0x100 的自检")
    args = ap.parse_args(argv)

    # 1) 建后端
    if args.sim:
        bus = SimBus(echo=args.sim_echo, periodic=args.sim_periodic)
    else:
        try:
            bus = PcanBus(args.channel, args.bitrate)
        except ImportError:
            print("!! 没有 python-can。请先：pip install \"python-can[pcan]\"", file=sys.stderr)
            print("   （或者加 --sim 用假帧先把前端跑起来）", file=sys.stderr)
            return 2
        except Exception as e:
            print("!! 打开 PCAN 失败：%s" % e, file=sys.stderr)
            print("   逐条检查：", file=sys.stderr)
            print("     1) pip install \"python-can[pcan]\" 装了吗？", file=sys.stderr)
            print("     2) PEAK 官方驱动装了吗（设备管理器里要显示 PCAN-USB）？", file=sys.stderr)
            print("     3) 分析仪插好了吗、被别的程序（PCAN-View）占用了吗？", file=sys.stderr)
            print("     4) 通道名对吗（%s）？" % args.channel, file=sys.stderr)
            print("   （或者加 --sim 用假帧先把前端跑起来）", file=sys.stderr)
            return 3

    ctl = Controller(bus, args.channel, args.bitrate)
    ctl.start()
    print_banner(args, ctl)

    # 启动自检：立刻试发一帧 0x100（车速无效），把"能不能发"当场暴露出来。
    # 只走 Controller.send_once_now，不经过 HTTP，也不影响前端逻辑。
    if args.selftest:
        ok, err = ctl.send_once_now(0x100, bytes([0xFF, 0, 0, 0, 0, 0, 0, 0]))
        if ok:
            print("[自检] 试发 0x100 成功 → 这条总线可以写（TX 成功计数已 +1）")
        else:
            print("[自检] 试发 0x100 失败：%s" % err)
            print("       ↑ 这就是「网页能收不能发」的典型原因：驱动/通道/被占用/总线无 ACK/权限")
        ctl.clear_error()          # 自检的错误只提示一次，不要一直挂顶栏
    print("-" * 74)

    try:
        srv = make_server(ctl, args.port, args.host)
    except OSError as e:
        print("!! 端口 %d 被占用或不可用：%s" % (args.port, e), file=sys.stderr)
        ctl.shutdown()
        return 4
    if args.json_status:
        # 给自测脚本用：证明"参数确实传进来了"（尤其是 --sim 与波特率）
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
        sys.stdout.reconfigure(encoding="utf-8")   # Windows 控制台中文
    except Exception:
        pass
    sys.exit(main())
