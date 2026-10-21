#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
pc_can_sim.py —— 电脑侧 CAN 模拟器（车况信号由电脑发给车机，并显示车机回传的控制结果）
====================================================================================
用途（对应车机 CAN 功能的两端）：
  · 下行：按 0x100 / 0x101 / 0x102 周期性发车况（车速/转向/刹车/倒车/安全带/门/灯光/空调），
          车机桌面右侧的车辆状态灯会跟着变（转向闪、门开摆出、安全带断开、倒车…）
  · 上行：收车机的 0x201 空调 / 0x202 车窗 / 0x203 灯光，解码打印
          （你在车机「车内控制」页点一下，这里就会显示出来）

两种驱动后端，自动选：
  1) python-can（推荐）：  pip install "python-can[pcan]"
  2) PEAK 官方 PCANBasic：装了 PEAK 驱动后自带的 PCANBasic.py（免 pip）

用法：
  python pc_can_sim.py --scenario drive            # 跑"开车"时间线（默认）
  python pc_can_sim.py --scenario static --speed 66 --gear D --left
  python pc_can_sim.py --raw 0x100 42 01 03 00 00 00 00 00    # 手发一帧（十进制或0x十六进制）
  python pc_can_sim.py --listen                     # 只听不说（看车机发的 0x201/0x202/0x203）
  python pc_can_sim.py --channel PCAN_USBBUS1 --bitrate 500000

⚠️ 波特率必须 500 kBit/s；采样点用标准 87.5% 即可（车机侧是 87.0%，
   一般能通；若出现错误帧，在 PCAN-View 里自定义 Bit Timing）。
"""
from __future__ import annotations

import argparse
import struct
import sys
import time

# ----------------------------------------------------------------- 协议解码
GEARS = {0: "P", 1: "R", 2: "N", 3: "D"}


def decode_0x201(d):
    on = d[0] == 1
    return "空调：{}  温度 {}℃  风量 {}  {}".format(
        "开" if on else "关", d[1], d[2], "内循环" if d[3] & 1 else "外循环")


def decode_0x202(d):
    win = {0: "全部", 1: "左前", 2: "右前", 3: "左后", 4: "右后"}.get(d[0], "?%d" % d[0])
    act = {0: "停", 1: "升", 2: "降"}.get(d[1], "?%d" % d[1])
    return "车窗：{} {}  目标 {}%".format(win, act, d[2])


def decode_0x203(d):
    bits = ["近光", "远光", "示宽", "前雾", "后雾", "氛围灯"]
    on = [bits[i] for i in range(6) if d[0] & (1 << i)]
    off = [bits[i] for i in range(6) if d[1] & (1 << i)]
    return "灯光：开[{}] 关[{}]".format("/".join(on) or "-", "/".join(off) or "-")


UPLINK = {0x201: decode_0x201, 0x202: decode_0x202, 0x203: decode_0x203}


def describe_tx(can_id, data):
    if can_id == 0x100:
        b1 = data[1]
        fl = []
        for bit, name in ((0, "左转"), (1, "右转"), (2, "刹车"), (3, "倒车"),
                          (4, "安全带未系"), (5, "手刹"), (6, "双闪")):
            if b1 & (1 << bit):
                fl.append(name)
        sp = "无效" if data[0] == 0xFF else "%d km/h" % data[0]
        return "0x100 车速={} 档位={} 转速={}rpm {}".format(
            sp, GEARS.get(data[2], "?"), data[3] * 100,
            (" ".join(fl)) if fl else "(全无)")
    if can_id == 0x101:
        doors = ["左前", "右前", "左后", "右后门", "后备箱", "引擎盖"]
        op = [doors[i] for i in range(6) if data[0] & (1 << i)]
        return "0x101 开[{}] {}".format("/".join(op) or "-",
                                        "已锁" if data[1] & 1 else "未锁")
    if can_id == 0x102:
        bits = ["近光", "远光", "示宽", "前雾", "后雾", "氛围灯"]
        on = [bits[i] for i in range(6) if data[0] & (1 << i)]
        return "0x102 灯[{}] {} {}℃ 车外{}℃".format(
            "/".join(on) or "-", "AC开" if data[1] & 1 else "AC关", data[2],
            (data[3] - 40) if data[3] else 0)
    return "0x%03X %s" % (can_id, " ".join("%02X" % b for b in data))


# ----------------------------------------------------------------- 后端
class Backend(object):
    name = "?"

    def send(self, can_id, data):
        raise NotImplementedError

    def recv(self, timeout=0.0):
        """返回 [(id, bytes), ...]，没有就返回空列表。"""
        raise NotImplementedError

    def close(self):
        pass


class PyCanBackend(Backend):
    name = "python-can"

    def __init__(self, channel, bitrate):
        import can                                    # noqa
        self._can = can
        self.bus = can.interface.Bus(channel=channel, bustype="pcan", bitrate=bitrate)
        self.buf = can.BufferedReader()
        self.notifier = can.Notifier(self.bus, [self.buf], timeout=0.2)

    def send(self, can_id, data):
        msg = self._can.Message(arbitration_id=can_id, is_extended_id=False,
                                data=bytes(data))
        self.bus.send(msg)

    def recv(self, timeout=0.0):
        out = []
        end = time.time() + timeout
        while True:
            msg = self.buf.get_message(timeout=0.05)
            if msg is not None:
                out.append((msg.arbitration_id, bytes(msg.data)))
            if time.time() >= end:
                break
        return out

    def close(self):
        try:
            self.notifier.stop()
            self.bus.shutdown()
        except Exception:
            pass


class PcanBasicBackend(Backend):
    name = "PCANBasic"

    def __init__(self, channel, bitrate):
        from PCANBasic import PCANBasic, TPCANMsg, TPCANType, TPCANHandle, TPCANBaudrate  # noqa
        self._m = PCANBasic
        self._msg = TPCANMsg
        ch = getattr(PCANBasic, channel) if isinstance(channel, str) else channel
        rate = {500000: TPCANBaudrate.PCAN_BAUD_500K,
                250000: TPCANBaudrate.PCAN_BAUD_250K,
                1000000: TPCANBaudrate.PCAN_BAUD_1M}.get(bitrate)
        if rate is None:
            raise SystemExit("PCANBasic 后端只内置 500K/250K/1M，其它波特率请用 PCAN-View 改")
        res = self._m.Initialize(ch, rate)
        if res != self._m.PCAN_ERROR_OK:
            raise SystemExit("PCAN 初始化失败: %s（驱动装了？分析仪插好了？）"
                             % self._m.GetFormattedError(res))
        self.handle = ch

    def send(self, can_id, data):
        msg = self._msg()
        msg.ID = can_id
        msg.LEN = len(data)
        msg.MSGTYPE = self._m.PCAN_MESSAGE_STANDARD
        for i, b in enumerate(data):
            msg.DATA[i] = b
        res = self._m.Write(self.handle, msg)
        if res != self._m.PCAN_ERROR_OK:
            print("!! 发送失败:", self._m.GetFormattedError(res))

    def recv(self, timeout=0.0):
        out = []
        end = time.time() + timeout
        while True:
            res, msg, _ts = self._m.Read(self.handle)
            if res == self._m.PCAN_ERROR_OK:
                out.append((msg.ID, bytes(msg.DATA[:msg.LEN])))
            elif res != self._m.PCAN_ERROR_QRCVEMPTY:
                break
            if time.time() >= end:
                break
            time.sleep(0.01)
        return out

    def close(self):
        self._m.Uninitialize(self.handle)


def open_backend(channel, bitrate):
    try:
        b = PyCanBackend(channel, bitrate)
        print("[后端] python-can / pcan，通道 %s，%d bit/s" % (channel, bitrate))
        return b
    except Exception as e:
        print("[后端] python-can 不可用（%s），改试 PEAK 官方 PCANBasic…" % e)
    try:
        b = PcanBasicBackend(channel, bitrate)
        print("[后端] PCANBasic，通道 %s，%d bit/s" % (channel, bitrate))
        return b
    except Exception as e:
        raise SystemExit(
            "两个后端都不可用：%s\n"
            "   · pip install \"python-can[pcan]\"\n"
            "   · 或装 PEAK 驱动（自带 PCANBasic.py）并把它的目录加进 PYTHONPATH" % e)


# ----------------------------------------------------------------- 发帧
def build_0x100(speed, gear, left=False, right=False, brake=False, reverse=False,
                seatbelt_off=False, handbrake=False, hazard=False, rpm_x100=8):
    flags = (int(left) | (int(right) << 1) | (int(brake) << 2) | (int(reverse) << 3)
             | (int(seatbelt_off) << 4) | (int(handbrake) << 5) | (int(hazard) << 6))
    g = {"P": 0, "R": 1, "N": 2, "D": 3}.get(gear, 3)
    return bytes([0xFF if speed is None else int(speed), flags, g, rpm_x100, 0, 0, 0, 0])


def build_0x101(doors=0, locked=True):
    return bytes([doors, 1 if locked else 0, 0, 0, 0, 0, 0, 0])


def build_0x102(lights=0, ac=False, recirc=False, ac_temp=24, outside=20):
    return bytes([lights, (1 if ac else 0) | (2 if recirc else 0),
                  ac_temp, (outside + 40) & 0xFF, 0, 0, 0, 0])


def pump(bus, send_list, period_ms, listen=True, seconds=None):
    """周期性把 send_list 里的帧发出去，同时打印车机回传。"""
    t0 = time.time()
    n = 0
    try:
        while True:
            for can_id, data in send_list:
                bus.send(can_id, data)
            n += 1
            if n == 1 or n % 10 == 0:
                for can_id, data in send_list:
                    print("  TX", describe_tx(can_id, data))
            if listen:
                for can_id, data in bus.recv(0.02):
                    fn = UPLINK.get(can_id)
                    if fn:
                        print("  RX ← 车机 %s" % fn(data))
                    elif can_id & 0x80000000:
                        print("  RX ← 错误帧 0x%X" % can_id)
            if seconds and (time.time() - t0) >= seconds:
                break
            time.sleep(period_ms / 1000.0)
    except KeyboardInterrupt:
        print("\n[停止] 已停发；车机 1 秒后按超时语义把指示灯清零、车速档位转暗灰")


# ----------------------------------------------------------------- 场景
def scenario_drive(bus, period_ms, listen, loop_s):
    """模拟一段真实开车过程（和车机内置演示的时间线一致，便于两端对照）。"""
    print("[场景] 开车时间线：怠速 → 加速到 66 → 左转 → 刹车 → 安全带未系 → 左前门开 → 倒车 → 停车")
    steps = [
        (10, dict(speed=0, gear="D"), 0, False, 0),
        (60, dict(speed=None, gear="D"), 0, False, 0),      # speed=None 由下面按比例填
        (30, dict(speed=66, gear="D", left=True), 0, False, 0),
        (30, dict(speed=40, gear="D", brake=True), 0, False, 0),
        (60, dict(speed=20, gear="D", seatbelt_off=True, right=True), 1, False, 0),
        (30, dict(speed=0, gear="R", reverse=True), 0, False, 0),   # reverse=True 让蜂鸣响
        (40, dict(speed=0, gear="P", handbrake=True), 0, False, 0),
    ]
    t0 = time.time()
    idx, tick = 0, 0
    try:
        while True:
            n, kw, doors, locked, lights = steps[idx]
            if "speed" in kw and kw["speed"] is None:      # 加速段：0 → 66
                kw = dict(kw)
                kw["speed"] = int(66 * tick / float(n))
            send = [(0x100, build_0x100(**kw)),
                    (0x101, build_0x101(doors=doors, locked=locked)),
                    (0x102, build_0x102(lights=lights, ac=True, ac_temp=24, outside=20))]
            for can_id, data in send:
                bus.send(can_id, data)
            if tick % 10 == 0:
                print("  TX", describe_tx(0x100, send[0][1]))
            if listen:
                for can_id, data in bus.recv(0.02):
                    fn = UPLINK.get(can_id)
                    if fn:
                        print("  RX ← 车机 %s" % fn(data))
            tick += 1
            if tick >= n:
                idx = (idx + 1) % len(steps)
                tick = 0
                print("[场景] 进入第 %d 段" % (idx + 1))
            if loop_s and (time.time() - t0) >= loop_s:
                break
            time.sleep(period_ms / 1000.0)
    except KeyboardInterrupt:
        print("\n[停止]")


def parse_raw(args):
    if not args.raw:
        return None
    can_id = int(args.raw[0], 0)
    data = [int(x, 0) & 0xFF for x in args.raw[1:9]]
    while len(data) < 8:
        data.append(0)
    return can_id, bytes(data)


def main():
    ap = argparse.ArgumentParser(description="车机 CAN 模拟器（电脑侧）")
    ap.add_argument("--channel", default="PCAN_USBBUS1",
                    help="python-can 用 pcan 通道名如 PCAN_USBBUS1；PCANBasic 用同名常量")
    ap.add_argument("--bitrate", type=int, default=500000)
    ap.add_argument("--period", type=int, default=100,
                    help="发送周期 ms（手册要求保持状态类报文 ≤500 ms，默认 100）")
    ap.add_argument("--scenario", default="drive", choices=["drive", "static"])
    ap.add_argument("--seconds", type=int, default=0, help="跑多少秒后退出（0=一直跑）")
    ap.add_argument("--listen", action="store_true", help="只听不发（看车机 0x201/0x202/0x203）")
    ap.add_argument("--raw", nargs="+", help="手发一帧：--raw 0x100 42 01 03 0 0 0 0 0")
    # static 场景参数
    ap.add_argument("--speed", type=int, default=0)
    ap.add_argument("--gear", default="D", choices=["P", "R", "N", "D"])
    ap.add_argument("--left", action="store_true")
    ap.add_argument("--right", action="store_true")
    ap.add_argument("--brake", action="store_true")
    ap.add_argument("--reverse", action="store_true")
    ap.add_argument("--seatbelt-off", action="store_true")
    ap.add_argument("--handbrake", action="store_true")
    ap.add_argument("--hazard", action="store_true")
    ap.add_argument("--door", type=int, default=0, help="门位掩码 bit0..5 左前/右前/左后/右后/后备箱/引擎盖")
    ap.add_argument("--unlocked", action="store_true")
    ap.add_argument("--lights", type=int, default=0, help="灯光掩码 bit0..5 近光/远光/示宽/前雾/后雾/氛围灯")
    ap.add_argument("--ac", action="store_true")
    args = ap.parse_args()

    bus = open_backend(args.channel, args.bitrate)
    try:
        raw = parse_raw(args)
        if raw:
            bus.send(*raw)
            print("  TX", describe_tx(raw[0], raw[1]))
            time.sleep(0.3)
            for can_id, data in bus.recv(0.5):
                fn = UPLINK.get(can_id)
                print("  RX ← 车机 %s" % (fn(data) if fn else "0x%03X" % can_id))
            return
        if args.listen:
            print("[只听模式] 等车机发 0x201/0x202/0x203（在车机上点「车内控制」）… Ctrl-C 退出")
            pump(bus, [], args.period, listen=True, seconds=args.seconds or None)
            return
        if args.scenario == "drive":
            scenario_drive(bus, args.period, True, args.seconds)
            return
        # static：按参数发固定车况
        send = [(0x100, build_0x100(args.speed, args.gear, args.left, args.right,
                                    args.brake, args.reverse, args.seatbelt_off,
                                    args.handbrake, args.hazard)),
                (0x101, build_0x101(args.door, not args.unlocked)),
                (0x102, build_0x102(args.lights, args.ac, False, 24, 20))]
        print("[静态场景] 周期 %d ms（车速 %s 档位 %s）" % (args.period, args.speed, args.gear))
        pump(bus, send, args.period, listen=True, seconds=args.seconds or None)
    finally:
        bus.close()


if __name__ == "__main__":
    main()
