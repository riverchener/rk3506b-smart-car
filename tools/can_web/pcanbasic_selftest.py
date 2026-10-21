#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
pcanbasic_selftest.py —— 独立桥接 pcanbasic_bridge.py 的自测（**不需要 PCAN 硬件**）
===============================================================================
做法：用**假的 PCANBasic 模块**注入（写成临时 PCANBasic.py，塞进 PYTHONPATH，
     真桥接进程启动时会 import 到它），然后：

  1. 单元级：PcanBasicBus 的
       · Initialize 的通道常量映射（"PCAN_USBBUS1" → 0x51；数字通道原样）
       · 波特率映射（500000 → PCAN_BAUD_500K，等等）
       · Write 的报文结构（ID / LEN / MSGTYPE / DATA）
       · Read 返回 PCAN_ERROR_QRCVEMPTY 时的空轮询语义（不报错、不 busy）
       · Read/Write 的非 OK 返回码 → 异常文本带原始错误（GetFormattedError 原文）
       · 非阻塞：一次登记 10 帧（周期 200 ms，同步发要 1.8 s）必须 < 100 ms 返回
       · TX 计数精确（成功/失败分开）
  2. 进程级：真起一个 pcanbasic_bridge.py（假 PCANBasic）→ 跑 HTTP，
     与真起一个 can_bridge.py --sim 的实例**逐字段比对 API**（字段名/类型一致），
     并跑同一批发送场景（单帧默认 1 次、批量周期、timeline、stop、timer、presets）。

    python pcanbasic_selftest.py          # 退出码 0 = 全过
"""
from __future__ import annotations

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
PASS, FAIL = [], []


def check(name, cond, detail=""):
    (PASS if cond else FAIL).append(name)
    print("  %s %s%s" % ("OK  " if cond else "FAIL", name,
                         ("   " + str(detail)[:400]) if (detail and not cond) else ""))


# =============================================================== 假 PCANBasic.py
FAKE_PCANBASIC = r'''# -*- coding: utf-8 -*-
"""假的 PCANBasic.py：只实现本项目用到的那点 API，并把每次调用记进日志。"""
import json, os, threading

LOG_PATH = os.environ.get("FAKE_PCANBASIC_LOG", "fake_pcanbasic_log.jsonl")
_LOCK = threading.Lock()


def log(entry):
    with _LOCK:
        with open(LOG_PATH, "a", encoding="utf-8") as fp:
            fp.write(json.dumps(entry, ensure_ascii=False) + "\n")


# --- 测试脚本可以通过这个文件塞"预设行为" ---
def behavior():
    p = os.environ.get("FAKE_PCANBASIC_BEHAVIOR")
    if p and os.path.isfile(p):
        try:
            with open(p, encoding="utf-8") as fp:
                return json.load(fp)
        except Exception:
            return {}
    return {}


class TPCANMsg(object):
    def __init__(self, can_id=0, msgtype=0, length=0, data=None, *a):
        self.ID = can_id
        self.MSGTYPE = msgtype
        self.LEN = length
        self.DATA = list(data) if data else [0] * 8


class PCANBasic(object):
    PCAN_USBBUS1 = 0x51
    PCAN_USBBUS2 = 0x52
    PCAN_USBBUS3 = 0x53

    PCAN_BAUD_1M    = 0x00140000
    PCAN_BAUD_800K  = 0x00160000
    PCAN_BAUD_500K  = 0x001C0000
    PCAN_BAUD_250K  = 0x00140000 + 1
    PCAN_BAUD_125K  = 0x00150000 + 1
    PCAN_BAUD_100K  = 0x00160000 + 1

    PCAN_MESSAGE_STANDARD = 0x00
    PCAN_MESSAGE_EXTENDED = 0x02

    PCAN_ERROR_OK       = 0x00000
    PCAN_ERROR_QRCVEMPTY = 0x00020
    PCAN_ERROR_ILLHANDLE = 0x00001
    PCAN_ERROR_BUSOFF    = 0x00040
    PCAN_ERROR_INITIALIZE = 0x00080

    _queue = []

    @classmethod
    def Initialize(cls, channel, baudrate):
        log({"call": "Initialize", "channel": channel, "baudrate": baudrate})
        b = behavior()
        if b.get("init_error"):
            return cls.PCAN_ERROR_INITIALIZE
        return cls.PCAN_ERROR_OK

    @classmethod
    def Uninitialize(cls, channel):
        log({"call": "Uninitialize", "channel": channel})
        return cls.PCAN_ERROR_OK

    @classmethod
    def Write(cls, channel, msg):
        log({"call": "Write", "channel": channel, "ID": msg.ID, "LEN": msg.LEN,
             "MSGTYPE": msg.MSGTYPE, "DATA": list(msg.DATA)})
        b = behavior()
        n = int(b.get("write_fail_times", 0))
        if n > 0:
            b["write_fail_times"] = n - 1
            p = os.environ.get("FAKE_PCANBASIC_BEHAVIOR")
            with open(p, "w", encoding="utf-8") as fp:
                json.dump(b, fp)
            return cls.PCAN_ERROR_BUSOFF
        return cls.PCAN_ERROR_OK

    @classmethod
    def Read(cls, channel):
        # 假设备：按"喂进来的帧"顺序返回，喂完就一直 QRCVEMPTY
        q = os.environ.get("FAKE_PCANBASIC_READQUEUE")
        frames = []
        if q and os.path.isfile(q):
            try:
                with open(q, encoding="utf-8") as fp:
                    frames = json.load(fp)
            except Exception:
                frames = []
        if frames:
            f = frames.pop(0)
            with open(q, "w", encoding="utf-8") as fp:
                json.dump(frames, fp)
            msg = TPCANMsg(f["id"], 0, len(f["data"]), f["data"])
            log({"call": "Read", "channel": channel, "return": "OK", "ID": f["id"]})
            return (cls.PCAN_ERROR_OK, msg, 0)
        b = behavior()
        if b.get("read_error"):
            log({"call": "Read", "channel": channel, "return": "ERROR"})
            return (cls.PCAN_ERROR_ILLHANDLE, None, 0)
        return (cls.PCAN_ERROR_QRCVEMPTY, None, 0)

    @classmethod
    def GetFormattedError(cls, code):
        return {cls.PCAN_ERROR_BUSOFF: "PCAN_ERROR_BUSOFF: 总线关闭",
                cls.PCAN_ERROR_INITIALIZE: "PCAN_ERROR_INITIALIZE: 驱动/设备初始化失败",
                cls.PCAN_ERROR_ILLHANDLE: "PCAN_ERROR_ILLHANDLE: 句柄无效"}.get(
                    code, "PCAN_ERROR_0x%X" % code)
'''


def make_fake_module_dir(tmpdir):
    d = os.path.join(tmpdir, "fakepcan")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "PCANBasic.py"), "w", encoding="utf-8") as fp:
        fp.write(FAKE_PCANBASIC)
    return d


# =============================================================== HTTP 工具
def http(base, method, path, body=None, timeout=8):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(base + path, data=data, method=method,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            raw = r.read().decode("utf-8")
            return r.status, (json.loads(raw) if raw else None)
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", "replace")
        try:
            return e.code, json.loads(raw)
        except Exception:
            return e.code, {"raw": raw}


def raw(base, path, timeout=8):
    """取原始文本（HTML/CSS/JS 用）。"""
    with urllib.request.urlopen(base + path, timeout=timeout) as r:
        return r.status, r.read().decode("utf-8", "replace")


def wait_up(base, tries=80):
    for _ in range(tries):
        try:
            st, _ = http(base, "GET", "/api/status", timeout=2)
            if st == 200:
                return True
        except Exception:
            pass
        time.sleep(0.15)
    return False


def spawn(script, port, extra_env=None, extra_args=()):
    env = dict(os.environ)
    env["PYTHONIOENCODING"] = "utf-8"
    if extra_env:
        env.update(extra_env)
    args = [sys.executable, os.path.join(HERE, script), "--port", str(port),
            "--no-selftest"] + list(extra_args)
    p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         env=env, cwd=HERE)
    return p


# =============================================================== 1) 单元级
def unit_tests(tmpdir):
    import importlib
    fake_dir = make_fake_module_dir(tmpdir)
    sys.path.insert(0, fake_dir)
    sys.path.insert(0, HERE)

    spec = importlib.util.spec_from_file_location("pb", os.path.join(HERE, "pcanbasic_bridge.py"))
    pb = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pb)

    logpath = os.path.join(tmpdir, "unit_log.jsonl")
    os.environ["FAKE_PCANBASIC_LOG"] = logpath
    os.environ.pop("FAKE_PCANBASIC_BEHAVIOR", None)
    os.environ.pop("FAKE_PCANBASIC_READQUEUE", None)

    def calls():
        if not os.path.isfile(logpath):
            return []
        with open(logpath, encoding="utf-8") as fp:
            return [json.loads(l) for l in fp if l.strip()]

    def reset_log():
        open(logpath, "w").close()

    print("\n[1] Initialize 的通道/波特率常量映射")
    reset_log()
    bus = pb.PcanBasicBus("PCAN_USBBUS1", 500000)
    c = calls()
    init = [x for x in c if x["call"] == "Initialize"]
    check("字符串通道 PCAN_USBBUS1 → Initialize(0x51, PCAN_BAUD_500K)",
          init and init[0]["channel"] == 0x51 and init[0]["baudrate"] == 0x001C0000, c[:2])
    check("businfo 里能看到通道与波特率常量",
          "PCAN_USBBUS1" in bus.businfo() and "PCAN_BAUD_500K" in bus.businfo(), bus.businfo())
    check("name = PCANBasic（前端「后端」显示用）", pb.PcanBasicBus.name == "PCANBasic")

    reset_log()
    bus2 = pb.PcanBasicBus(0x52, 250000)
    c = calls()
    init = [x for x in c if x["call"] == "Initialize"]
    check("数字通道 0x52 原样传给 Initialize", init and init[0]["channel"] == 0x52, c[:2])
    check("250000 → PCAN_BAUD_250K", init and init[0]["baudrate"] == 0x00140000 + 1, c[:2])

    reset_log()
    bus3 = pb.PcanBasicBus("0x53", 1000000)
    init = [x for x in calls() if x["call"] == "Initialize"]
    check("通道写 '0x53' 也认（解析成 0x53）", init and init[0]["channel"] == 0x53, init[:1])
    check("1000000 → PCAN_BAUD_1M", init and init[0]["baudrate"] == 0x00140000, init[:1])

    try:
        pb.PcanBasicBus("PCAN_USBBUS1", 33333)
        check("不支持的波特率给清晰报错", False, "居然通过了")
    except Exception as e:
        check("不支持的波特率给清晰报错",
              "只内置" in str(e) and "33333" in str(e), e)
    try:
        pb.PcanBasicBus("PCAN_NOPE1", 500000)
        check("未知通道名给清晰报错", False, "居然通过了")
    except Exception as e:
        check("未知通道名给清晰报错", "没有通道常量" in str(e), e)

    print("\n[2] Write 的报文结构（ID / LEN / MSGTYPE / DATA）")
    reset_log()
    bus.send(0x100, bytes([0x42, 0x01, 0x03, 0x08, 0, 0, 0, 0]))
    w = [x for x in calls() if x["call"] == "Write"]
    check("Write 被调用一次", len(w) == 1, len(w))
    check("Write.ID = 0x100", w and w[0]["ID"] == 0x100, w[:1])
    check("Write.LEN = 8", w and w[0]["LEN"] == 8, w[:1])
    check("Write.MSGTYPE = PCAN_MESSAGE_STANDARD(0)", w and w[0]["MSGTYPE"] == 0, w[:1])
    check("Write.DATA 逐字节一致",
          w and w[0]["DATA"] == [0x42, 0x01, 0x03, 0x08, 0, 0, 0, 0], w[:1])
    reset_log()
    bus.send(0x201, bytes([1, 24, 3, 1, 0, 0, 0, 0]))
    w = [x for x in calls() if x["call"] == "Write"]
    check("同一个 handle 复用（Initialize 只调过一次）",
          len([x for x in calls() if x["call"] == "Initialize"]) == 0, calls())

    print("\n[3] Read：QRCVEMPTY = 空轮询（正常），有帧就取走")
    t0 = time.time()
    out = bus.recv(0.05)
    dt = time.time() - t0
    check("队列空时返回空列表、不抛异常", out == [], out)
    check("空轮询耗时接近 timeout（不是 busy loop，0.05~0.5 s）", 0.04 <= dt <= 0.6, dt)
    rq = os.path.join(tmpdir, "readqueue.json")
    with open(rq, "w", encoding="utf-8") as fp:
        json.dump([{"id": 0x201, "data": [1, 24, 3, 1, 0, 0, 0, 0]},
                   {"id": 0x100, "data": [66, 1, 3, 8, 0, 0, 0, 0]}], fp)
    os.environ["FAKE_PCANBASIC_READQUEUE"] = rq
    got = bus.recv(0.05)
    check("喂两帧 → 一次 recv 收全，顺序正确",
          got == [(0x201, bytes([1, 24, 3, 1, 0, 0, 0, 0])),
                  (0x100, bytes([66, 1, 3, 8, 0, 0, 0, 0]))], got)
    check("取走后队列空 → 再 recv 返回空", bus.recv(0.02) == [])
    os.environ.pop("FAKE_PCANBASIC_READQUEUE", None)

    print("\n[4] 非 OK 返回码 → 异常文本带原始错误；Write 失败也上抛")
    beh = os.path.join(tmpdir, "behavior.json")
    with open(beh, "w", encoding="utf-8") as fp:
        json.dump({"read_error": True}, fp)
    os.environ["FAKE_PCANBASIC_BEHAVIOR"] = beh
    try:
        bus.recv(0.02)
        check("Read 返回非 OK 时报错", False, "居然没报")
    except Exception as e:
        check("Read 返回非 OK 时报错", True)
        check("错误文本含原始 GetFormattedError 原文",
              "ILLHANDLE" in str(e) and "句柄无效" in str(e), e)
    with open(beh, "w", encoding="utf-8") as fp:
        json.dump({"write_fail_times": 1}, fp)
    try:
        bus.send(0x101, bytes(8))
        check("Write 返回非 OK 时报错", False, "居然没报")
    except Exception as e:
        check("Write 返回非 OK 时报错", True)
        check("错误文本含 ID / 数据 / 原始错误原文",
              "0x101" in str(e) and "00 00 00 00 00 00 00 00" in str(e) and "BUSOFF" in str(e), e)
    with open(beh, "w", encoding="utf-8") as fp:
        json.dump({}, fp)
    os.environ.pop("FAKE_PCANBASIC_BEHAVIOR", None)

    print("\n[5] 上层 Controller：非阻塞 / TX 计数精确 / 上限")
    import can_bridge as cb
    bus4 = pb.PcanBasicBus("PCAN_USBBUS1", 500000)
    ctl = cb.Controller(bus4, "PCAN_USBBUS1", 500000)
    ctl.start()
    try:
        ctl.clear()
        t0 = time.time()
        ctl.queue_send(0x100, bytes(8), 10, 200)      # 同步发要 1.8 s
        dt = time.time() - t0
        check("登记 10 帧（周期 200ms）< 100 ms 返回（实测 %.1f ms）" % (dt * 1000), dt < 0.1, dt)
        check("登记后 tx_queued = 10", ctl.status()["tx_queued"] == 10, ctl.status()["tx_queued"])
        time.sleep(2.2)
        st = ctl.status()
        check("10 帧最终全部发出、tx_ok = 10", st["tx_ok"] == 10, st)
        check("tx_queued 归零", st["tx_queued"] == 0, st)
        check("tx_fail = 0", st["tx_fail"] == 0, st)

        ctl.clear()
        with open(beh, "w", encoding="utf-8") as fp:
            json.dump({"write_fail_times": 1}, fp)
        os.environ["FAKE_PCANBASIC_BEHAVIOR"] = beh
        ctl.queue_send(0x101, bytes(8), 1, 0)
        time.sleep(0.4)
        st = ctl.status()
        check("Write 失败 → tx_fail=1 且不计入 tx_ok", st["tx_fail"] == 1 and st["tx_ok"] == 0, st)
        check("失败原文进 status.error（含 PCANBasic 原文）",
              "BUSOFF" in st["error"], st["error"])
        errs = ctl.take_errors()
        check("错误日志里也有（前端会拉到日志区）",
              any("0x101" in e["msg"] for e in errs), errs)
        os.environ.pop("FAKE_PCANBASIC_BEHAVIOR", None)
        with open(beh, "w", encoding="utf-8") as fp:
            json.dump({}, fp)

        try:
            ctl.queue_send(0x100, bytes(8), 11, 0)
            check("单次突发上限 10 帧仍然生效", False, "居然通过了")
        except ValueError:
            check("单次突发上限 10 帧仍然生效", True)
        ctl.stop_all()
    finally:
        ctl.shutdown()

    print("\n[6] 启动自检用的 send_once_now")
    ctl2 = cb.Controller(bus4, "PCAN_USBBUS1", 500000)
    ok, err = ctl2.send_once_now(0x100, bytes([0xFF, 0, 0, 0, 0, 0, 0, 0]))
    check("send_once_now 返回成功", ok is True and err == "", (ok, err))
    check("自检那一帧计入 tx_ok", ctl2.status()["tx_ok"] == 1, ctl2.status())


# =============================================================== 2) 进程级 + API 对比
STATUS_KEYS = ("connected", "channel", "bitrate", "backend", "businfo", "rx", "tx",
               "tx_ok", "tx_fail", "tx_queued", "timers", "timeline", "error", "version")
FRAME_KEYS = ("t", "id", "data", "dir")


def process_tests(tmpdir):
    fake_dir = make_fake_module_dir(tmpdir)
    env = {"PYTHONPATH": fake_dir + os.pathsep + os.environ.get("PYTHONPATH", "")}
    logpath = os.path.join(tmpdir, "proc_log.jsonl")
    env["FAKE_PCANBASIC_LOG"] = logpath

    pb_port, sim_port = 8931, 8932
    pb_base = "http://127.0.0.1:%d" % pb_port
    sim_base = "http://127.0.0.1:%d" % sim_port

    pb = spawn("pcanbasic_bridge.py", pb_port, env)
    sim = spawn("can_bridge.py", sim_port, None, ("--sim",))
    try:
        print("\n[7] 进程级：pcanbasic_bridge 真起来并服务 HTTP")
        check("PCANBasic 桥接起来了 /api/status 200", wait_up(pb_base))
        check("can_bridge --sim 对照实例也起来", wait_up(sim_base))

        st_pb, st_sim = http(pb_base, "GET", "/api/status")[1], http(sim_base, "GET", "/api/status")[1]
        check("pcanbasic 桥接的 backend = PCANBasic", st_pb["backend"] == "PCANBasic", st_pb["backend"])
        check("对照实例 backend = sim", st_sim["backend"] == "sim", st_sim["backend"])

        miss = [k for k in STATUS_KEYS if k not in st_pb]
        check("/api/status 字段名与 can_bridge 完全一致（%d 个）" % len(STATUS_KEYS),
              not miss, miss)
        extra = [k for k in st_pb if k not in STATUS_KEYS]
        check("/api/status 没有多出字段", not extra, extra)
        types_bad = [k for k in STATUS_KEYS if type(st_pb[k]) is not type(st_sim[k])]
        check("/api/status 各字段类型与对照实例一致", not types_bad, types_bad)

        st, html = raw(pb_base, "/")
        check("GET / 返回同一份前端页面（200，且是 index.html 本体）",
              st == 200 and "接收区" in html and "开车时间线" in html and "app.js" in html,
              (st, html[:120]))
        for f in ("app.js", "style.css"):
            st, body = raw(pb_base, "/" + f)
            check("GET /%s 正常（前端静态文件复用）" % f, st == 200 and len(body) > 500,
                  (st, len(body)))
        check("前端没有外网资源（无 http:// https:// //cdn）",
              all(s not in html for s in ("http://", "https://", "//cdn")), "有外链")

        st, fr = http(pb_base, "GET", "/api/frames")
        check("/api/frames 结构 {frames, errors, status}",
              isinstance(fr.get("frames"), list) and isinstance(fr.get("errors"), list)
              and isinstance(fr.get("status"), dict), sorted(fr))

        print("\n[8] 同一批发送场景，两个桥接行为一致")
        # 单帧不传 count → 1 次
        st, j = http(pb_base, "POST", "/api/send", {"id": 0x100, "data": [1, 2, 3, 0, 0, 0, 0, 0]})
        check("POST /api/send 默认 repeats=1", j.get("repeats") == 1, j)
        check("响应含 tx_before 基线", "tx_before" in j, sorted(j))

        # 批量周期帧
        st, j = http(pb_base, "POST", "/api/send", {"frames": [
            {"id": 0x100, "data": [66, 1, 3, 8, 0, 0, 0, 0], "periodic": True, "period_ms": 100, "count": 0}]})
        check("批量 periodic 返回 token", len(j.get("timers", [])) == 1, j)
        tok = j["timers"][0]["token"]
        time.sleep(0.6)
        st_pb2 = http(pb_base, "GET", "/api/status")[1]
        check("周期帧在跑（timers=1、tx_ok 在涨）",
              st_pb2["timers"] == 1 and st_pb2["tx_ok"] > 1, st_pb2)
        st, j = http(pb_base, "POST", "/api/stop", {"token": tok})
        check("POST /api/stop {token} 有效", j.get("ok") is True, j)

        # 上限
        st, j = http(pb_base, "POST", "/api/send", {"id": 0x100, "data": [1], "count": 11})
        check("count=11 被 400 拒绝（与 can_bridge 一致）", st == 400, (st, j))

        # timeline
        st, j = http(pb_base, "POST", "/api/timeline", {"timeline": True, "period_ms": 100})
        check("POST /api/timeline 开", j.get("ok") is True and j.get("total_ms") == 30000, j)
        time.sleep(0.5)
        ids = set()
        for _ in range(3):
            _, fr = http(pb_base, "GET", "/api/frames")
            ids |= set(f["id"] for f in fr["frames"])
            time.sleep(0.12)
        check("时间线在发 0x100/0x101/0x102", {0x100, 0x101, 0x102} <= ids, sorted(ids))
        st, j = http(pb_base, "POST", "/api/timeline", {"timeline": False})
        check("POST /api/timeline 关", j.get("timeline") is False, j)

        # timer
        st, j = http(pb_base, "POST", "/api/timer", {"id": 0x201, "data": [1, 24, 3, 1], "period_ms": 100, "count": 0})
        check("POST /api/timer 返回 token", bool(j.get("token")), j)
        http(pb_base, "POST", "/api/stop", {})
        check("POST /api/stop {} 全部停发", http(pb_base, "GET", "/api/status")[1]["timers"] == 0)

        # presets
        st, j = http(pb_base, "GET", "/api/presets")
        check("GET /api/presets 与 can_bridge 同一份定义",
              all(k in j for k in ("mileage", "fault", "mode_eco", "mode_sport")), sorted(j))

        # clear
        http(pb_base, "POST", "/api/clear", {})
        st_pb3 = http(pb_base, "GET", "/api/status")[1]
        check("POST /api/clear 计数归零",
              st_pb3["tx_ok"] == 0 and st_pb3["tx_fail"] == 0 and st_pb3["rx"] == 0, st_pb3)

        print("\n[9] 假 PCANBasic 侧：Initialize 参数正确、Write 真的被调用")
        with open(logpath, encoding="utf-8") as fp:
            calls = [json.loads(l) for l in fp if l.strip()]
        init = [c for c in calls if c["call"] == "Initialize"]
        check("进程启动时 Initialize(0x51, PCAN_BAUD_500K)",
              init and init[0]["channel"] == 0x51 and init[0]["baudrate"] == 0x001C0000, init[:1])
        writes = [c for c in calls if c["call"] == "Write"]
        check("Write 真被调用过（帧确实往驱动写了）", len(writes) > 0, len(writes))
        check("Write 的报文结构正确（ID/LEN/MSGTYPE/DATA）",
              all(c["LEN"] == 8 and c["MSGTYPE"] == 0 and len(c["DATA"]) == 8 for c in writes),
              writes[:2])
        check("收到过 0x100 的 Write（发送链路真的走到驱动）",
              any(c["ID"] == 0x100 for c in writes), [c["ID"] for c in writes][:8])

        print("\n[10] 读上行帧也走通（另起一个实例，启动前就把帧喂进假设备队列）")
        rq = os.path.join(tmpdir, "proc_readqueue.json")
        with open(rq, "w", encoding="utf-8") as fp:
            json.dump([{"id": 0x201, "data": [1, 24, 3, 1, 0, 0, 0, 0]},
                       {"id": 0x203, "data": [1, 2, 0, 0, 0, 0, 0, 0]}], fp)
        rd_port = 8933
        rd_base = "http://127.0.0.1:%d" % rd_port
        env2 = dict(env)
        env2["FAKE_PCANBASIC_READQUEUE"] = rq
        rd = spawn("pcanbasic_bridge.py", rd_port, env2)
        try:
            check("带读队列的实例起来了", wait_up(rd_base))
            ok = False
            for _ in range(20):
                st_r = http(rd_base, "GET", "/api/status")[1]
                if st_r["rx"] >= 2:
                    ok = True
                    break
                time.sleep(0.15)
            check("假设备喂的 0x201/0x203 被读到 → rx ≥ 2", ok, st_r)
            got = {}
            for _ in range(4):
                _, fr = http(rd_base, "GET", "/api/frames")
                for f in fr["frames"]:
                    got[f["id"]] = f
                time.sleep(0.1)
            check("读到的帧结构 {t,id,data,dir} 正确、dir=rx",
                  all(set(FRAME_KEYS) <= set(v) and v["dir"] == "rx" for v in got.values()) and got,
                  got)
            check("读到的 0x201 数据逐字节一致",
                  got.get(0x201, {}).get("data") == [1, 24, 3, 1, 0, 0, 0, 0],
                  got.get(0x201))
            st_r2 = http(rd_base, "GET", "/api/status")[1]
            check("读到帧后 error 仍为空（QRCVEMPTY 不当错误）", st_r2["error"] == "", st_r2)
        finally:
            rd.terminate()
            try:
                rd.wait(timeout=5)
            except Exception:
                rd.kill()
    finally:
        for p in (pb, sim):
            p.terminate()
            try:
                p.wait(timeout=5)
            except Exception:
                p.kill()


def main():
    tmpdir = tempfile.mkdtemp(prefix="pcanbasic_selftest_")
    print("=" * 74)
    print(" pcanbasic_bridge.py 自测（假 PCANBasic 模块，不需要 PCAN 硬件）")
    print(" 临时目录：%s" % tmpdir)
    print("=" * 74)
    try:
        unit_tests(tmpdir)
        process_tests(tmpdir)
    finally:
        pass
    print("\n" + "=" * 74)
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
