#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
bridge_selftest.py —— 端到端自测：真的起一个 can_bridge.py（--sim）然后把
所有 API 与 5 个一键预设跑一遍，断言看得到"发出去的帧回显"和"板子上行帧"。

不依赖任何第三方库（urllib 足够），也不需要 PCAN 硬件。

    python bridge_selftest.py            # 用 127.0.0.1:8799 起一个临时实例
    python bridge_selftest.py --port 8765 --no-spawn   # 测已经在跑的那个

退出码 0 = 全部通过。
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = "http://127.0.0.1:%d"

PASS, FAIL = [], []


def check(name, cond, detail=""):
    (PASS if cond else FAIL).append(name)
    print("  %s %s%s" % ("OK  " if cond else "FAIL", name,
                         ("   " + str(detail)) if (detail and not cond) else ""))


def http(method, path, body=None, base=BASE):
    url = base % PORT + path
    data = None
    headers = {}
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=8) as r:
            raw = r.read()
            return r.status, (json.loads(raw.decode("utf-8")) if raw else None)
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", "replace")
        print("      [HTTP %d] %s  ← 请求 %s %s" % (e.code, raw.strip()[:300], method, path))
        try:
            e.parsed = json.loads(raw)
        except Exception:
            e.parsed = None
        raise


def get(path):
    return http("GET", path)


def post(path, body):
    return http("POST", path, body)


def post_expect_400(name, body):
    """要求被 400 拒绝（并给出错误原因）。"""
    try:
        _, j = post("/api/send", body)
        check(name, False, "居然通过了：%s" % (j,))
    except urllib.error.HTTPError as e:
        check(name, e.code == 400, e.code)


def drain(seconds=0.6):
    """轮询几轮，收集这段时间内的所有帧。"""
    out = []
    end = time.time() + seconds
    while time.time() < end:
        _, j = get("/api/frames")
        out.extend(j["frames"])
        time.sleep(0.12)
    return out


def ids(frames):
    return sorted(set(f["id"] for f in frames))


def fmt(f):
    return "0x%03X [%s] %s" % (f["id"], f["dir"], " ".join("%02X" % b for b in f["data"]))


def spawn(port):
    env = dict(os.environ, PYTHONIOENCODING="utf-8")
    p = subprocess.Popen([sys.executable, os.path.join(HERE, "can_bridge.py"),
                          "--sim", "--port", str(port), "--no-selftest"],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    # 等端口起来
    for _ in range(60):
        try:
            get("/api/status")
            return p
        except Exception:
            time.sleep(0.15)
    out = p.stdout.read().decode("utf-8", "replace") if p.stdout else ""
    raise SystemExit("桥接进程没起来：\n" + out)


def main():
    global PORT
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8799)
    ap.add_argument("--no-spawn", action="store_true", help="不自己起进程（测已在跑的那个）")
    args = ap.parse_args()
    PORT = args.port

    proc = None
    if not args.no_spawn:
        proc = spawn(PORT)
    try:
        run_tests()
    finally:
        if proc:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except Exception:
                proc.kill()

    print("\n" + "=" * 70)
    print("通过 %d 项，失败 %d 项" % (len(PASS), len(FAIL)))
    if FAIL:
        for f in FAIL:
            print("  FAIL " + f)
    return 1 if FAIL else 0


def run_tests():
    print("=" * 70)
    print(" can_bridge.py 端到端自测（--sim，无需 PCAN 硬件）")
    print("=" * 70)

    # ---------------- 1. 静态页面 ----------------
    print("\n[1] 静态文件与状态")
    st, html = None, None
    with urllib.request.urlopen(BASE % PORT + "/", timeout=8) as r:
        st, html = r.status, r.read().decode("utf-8")
    check("GET / 返回 200", st == 200, st)
    check("index.html 里有顶栏/接收区/发送区",
          all(k in html for k in ("接收区", "发送区", "清空", "开车时间线")), "缺关键字")
    check("index.html 无外网资源（无 http:// https:// //cdn）",
          ("http://" not in html and "https://" not in html and "//cdn" not in html),
          "页面里有外链")
    for f in ("app.js", "style.css"):
        with urllib.request.urlopen(BASE % PORT + "/" + f, timeout=8) as r:
            body = r.read().decode("utf-8")
        check("GET /%s 返回 200 且非空" % f, r.status == 200 and len(body) > 500, len(body))
        low = body.lower()
        check("%s 无外网资源" % f,
              ("http://" not in low and "https://" not in low and "cdn." not in low), "有外链")
    try:
        http("GET", "/../can_bridge.py")
        check("路径穿越被拒绝", False, "居然读到了上级目录文件")
    except urllib.error.HTTPError as e:
        check("路径穿越被拒绝", e.code in (403, 404), e.code)

    _, s = get("/api/status")
    check("GET /api/status 字段齐全",
          all(k in s for k in ("connected", "channel", "bitrate", "rx", "tx", "error")), s)
    check("status 有新字段 tx_ok / tx_fail / tx_queued / businfo",
          all(k in s for k in ("tx_ok", "tx_fail", "tx_queued", "businfo")), sorted(s))
    check("status.channel 默认 PCAN_USBBUS1", s["channel"] == "PCAN_USBBUS1", s["channel"])
    check("status.bitrate = 500000", s["bitrate"] == 500000, s["bitrate"])
    check("status.connected = True", s["connected"] is True, s)
    check("status.error 为空", s["error"] == "", s["error"])
    check("sim 后端标识", s["backend"] == "sim", s["backend"])

    # ---------------- 2. sim 自动上行帧 ----------------
    print("\n[2] sim 自动上行帧（板子 → 电脑）")
    fr = drain(2.0)
    up = [f for f in fr if f["dir"] == "rx"]
    got = ids(up)
    check("收到上行 0x201/0x202/0x203/0x204",
          all(i in got for i in (0x201, 0x202, 0x203, 0x204)), got)
    check("帧结构 {t,id,data,dir} 齐全",
          all(set(("t", "id", "data", "dir")) <= set(f.keys()) for f in fr), fr[:1])
    check("data 都是 8 字节", all(len(f["data"]) == 8 for f in fr), [f for f in fr if len(f["data"]) != 8][:2])
    check("/api/frames 带 errors 数组（发送/接收错误也同步给前端）",
          isinstance(get("/api/frames")[1].get("errors"), list))

    # ---------------- 3. POST /api/send ----------------
    print("\n[3] POST /api/send（默认 1 帧 / 非阻塞 / 十六进制 id）")
    # ★ 默认次数必须是 1（问题 2：以前一点就灌一串）
    s0 = get("/api/status")[1]
    t0 = time.time()
    _, j = post("/api/send", {"id": 0x100, "data": [1, 2, 3, 0, 0, 0, 0, 0]})
    dt = time.time() - t0
    check("不传 count 时默认只 1 帧（repeats=1）", j.get("repeats") == 1, j)
    check("HTTP 立即返回（登记耗时 < 200 ms，实测 %.0f ms）" % (dt * 1000), dt < 0.2, dt)
    check("响应带回 tx_before 基线", "tx_before" in j, sorted(j))
    fr = drain(0.6)
    n1 = len([f for f in fr if f["id"] == 0x100 and f["dir"] == "tx"])
    check("默认请求只产生 1 条 tx 记录（不是 100）", n1 == 1, n1)
    s1 = get("/api/status")[1]
    check("TX 成功计数 +1", s1["tx_ok"] == s0["tx_ok"] + 1, (s0["tx_ok"], s1["tx_ok"]))

    # ★ 非阻塞：登记 10 帧（周期 50 ms，真要 450 ms 才发完）也必须秒回
    t0 = time.time()
    _, j = post("/api/send", {"id": 0x100, "data": [48, 1, 3, 0, 0, 0, 0, 0], "count": 10, "period_ms": 50})
    dt = time.time() - t0
    check("count=10/period=50 登记耗时 < 200 ms（非阻塞，实测 %.0f ms）" % (dt * 1000), dt < 0.2, dt)
    check("count=10 → repeats=10", j.get("repeats") == 10, j)
    s2 = get("/api/status")[1]
    check("登记完立刻查：tx_queued > 0（后台慢慢发）", s2["tx_queued"] > 0, s2["tx_queued"])
    fr = drain(0.9)
    n100 = len([f for f in fr if f["id"] == 0x100 and f["dir"] == "tx"])
    check("10 帧最终都发出（收到 10 条 tx）", n100 == 10, n100)
    s3 = get("/api/status")[1]
    check("发完 tx_queued 归零", s3["tx_queued"] == 0, s3["tx_queued"])
    check("10 帧全部计入 TX 成功", s3["tx_ok"] == s1["tx_ok"] + 10, (s1["tx_ok"], s3["tx_ok"]))

    # ★ 突发上限 10 帧
    try:
        post("/api/send", {"id": 0x100, "data": [1], "count": 11})
        check("count=11 被 400 拒绝（单次最多 10 帧）", False, "居然通过了")
    except urllib.error.HTTPError as e:
        check("count=11 被 400 拒绝（单次最多 10 帧）", e.code == 400, e.code)
    drain(0.4)

    _, j = post("/api/send", {"id": "0x101", "data": [1, 0, 0, 0, 0, 0, 0, 0]})
    check("十六进制字符串 id 接受", j.get("ok") is True and j["queued"][0]["id"] == 0x101, j)
    _, j = post("/api/send", {"id": "103", "data": [1]})     # 无 0x 前缀 → 按十进制 103
    check("纯数字字符串按十进制（'103' → 103）", j.get("ok") is True and j["queued"][0]["id"] == 103, j)
    _, j = post("/api/send", {"id": "0x104", "data": [0x4B]})  # 含字母 → 16 进制
    check("含字母字符串按十六进制（'0x104' → 260）", j.get("ok") is True and j["queued"][0]["id"] == 0x104, j)
    _, j = post("/api/send", {"id": 0x100, "data": [1]})
    check("data 不足 8 字节自动补 0", j["queued"][0]["data"] == [1, 0, 0, 0, 0, 0, 0, 0], j["queued"])
    drain(0.4)

    # ---------------- 4. 错误处理 ----------------
    print("\n[4] 参数校验")
    for name, body in (("id 超 11 位被拒", {"id": 0x800, "data": [0]}),
                       ("data 超 8 字节被拒", {"id": 0x100, "data": [0] * 9}),
                       ("data 值 > 255 被拒", {"id": 0x100, "data": [999]}),
                       ("缺 id 被拒", {"data": [0]}),
                       ("count=0 且非周期被拒", {"id": 0x100, "data": [0], "count": 0}),
                       ("count 负数被拒", {"id": 0x100, "data": [0], "count": -1})):
        post_expect_400(name, body)
    try:
        req = urllib.request.Request(BASE % PORT + "/api/send", data=b"{not json",
                                     headers={"Content-Type": "application/json"}, method="POST")
        urllib.request.urlopen(req, timeout=5)
        check("坏 JSON 被拒", False, "居然通过了")
    except urllib.error.HTTPError as e:
        check("坏 JSON 被拒", e.code == 400, e.code)

    # ---------------- 5. 预设 1：开车时间线 ----------------
    print("\n[5] 预设 1 —— 开车时间线（约 30 s 一轮，循环）")
    _, j = post("/api/timeline", {"timeline": True, "period_ms": 100})
    check("时间线启动", j.get("ok") is True and j.get("total_ms") == 30000, j)
    fr = drain(3.2)
    got = ids(fr)
    check("时间线同时发 0x100/0x101/0x102", all(i in got for i in (0x100, 0x101, 0x102)), got)
    n100 = len([f for f in fr if f["id"] == 0x100 and f["dir"] == "tx"])
    check("0x100 约 100 ms 一条（3.2 s 内 ≥ 20 条）", n100 >= 20, n100)
    # 第一段是怠速：车速 0
    first = [f for f in fr if f["id"] == 0x100 and f["dir"] == "tx"][:3]
    check("第一段=怠速（B0=00, gear=D(3)）",
          first and all(f["data"][0] == 0 and f["data"][2] == 3 for f in first),
          [f["data"] for f in first])
    # 加速段末尾应该看到 66 km/h（3~6 秒是斜坡）
    time.sleep(3.2)
    fr2 = drain(2.4)
    speeds = [f["data"][0] for f in fr2 if f["id"] == 0x100 and f["dir"] == "tx"]
    check("加速段出现 66 km/h", max(speeds) >= 60 if speeds else False, sorted(set(speeds))[-6:])
    _, j = post("/api/timeline", {"timeline": False})
    check("时间线可停", j.get("timeline") is False, j)
    drain(0.5)
    time.sleep(0.4)
    fr3 = drain(1.2)
    n100b = len([f for f in fr3 if f["id"] == 0x100 and f["dir"] == "tx"])
    check("停后 0x100 不再自动发（1.2 s 内 ≤ 2 条，可能还剩队列）", n100b <= 2, n100b)

    # ---------------- 6. 预设 2：静态车况（3 帧周期） ----------------
    print("\n[6] 预设 2 —— 静态车况（0x100/0x101/0x102 周期 100 ms）")
    _, j = post("/api/send", {"frames": [
        {"id": 0x100, "data": [66, 0x01, 3, 8, 0, 0, 0, 0], "periodic": True, "period_ms": 100, "count": 0},
        {"id": 0x101, "data": [0x01, 0, 0, 0, 0, 0, 0, 0], "periodic": True, "period_ms": 100, "count": 0},
        {"id": 0x102, "data": [0x01, 1, 24, 60, 0, 0, 0, 0], "periodic": True, "period_ms": 100, "count": 0}]})
    check("批量周期帧被接受并返回 token", len(j.get("timers", [])) == 3, j)
    toks = [t["token"] for t in j["timers"]]
    fr = drain(1.6)
    for i in (0x100, 0x101, 0x102):
        n = len([f for f in fr if f["id"] == i and f["dir"] == "tx"])
        check("0x%03X 周期发送（≥8 条/1.6 s）" % i, n >= 8, n)
    d100 = [f["data"] for f in fr if f["id"] == 0x100 and f["dir"] == "tx"][0]
    check("0x100 内容 = 车速66/左转/D/800rpm", d100 == [66, 0x01, 3, 8, 0, 0, 0, 0], d100)
    _, s = get("/api/status")
    check("status.timers 显示 3 个周期定时器", s.get("timers") == 3, s)
    for t in toks:
        post("/api/stop", {"token": t})
    drain(0.5)

    # ---------------- 7. 预设 3：工作模式 ----------------
    print("\n[7] 预设 3 —— 工作模式 0x204（自动重发 3 次）")
    for m, name in ((0, "经济"), (1, "标准"), (2, "运动"), (3, "雪地")):
        _, j = post("/api/send", {"id": 0x204, "data": [m, 0, 0, 0, 0, 0, 0, 0],
                                  "count": 3, "period_ms": 50})
        fr = drain(0.6)
        n = len([f for f in fr if f["id"] == 0x204 and f["dir"] == "tx"])
        d = [f["data"][0] for f in fr if f["id"] == 0x204 and f["dir"] == "tx"]
        check("模式 %s（B0=%d）×3" % (name, m), n == 3 and d == [m] * 3, (n, d))

    # ---------------- 8. 预设 4：里程油耗组 ----------------
    print("\n[8] 预设 4 / 5 —— 里程油耗组 与 故障演示 的字节内容")
    _, j = post("/api/send", {"preset": "mileage"})
    check("预设名 mileage 可用", j.get("ok") is True, j)
    fr = drain(1.6)
    got = ids([f for f in fr if f["dir"] == "tx"])
    check("里程组发出 0x103/0x104/0x105", all(i in got for i in (0x103, 0x104, 0x105)), got)
    d103 = [f["data"] for f in fr if f["id"] == 0x103][0]
    check("0x103 = 总里程1000.0km/小计8.0/油量50%/续航600km",
          d103 == [0x10, 0x27, 0x00, 0x50, 0x00, 50, 0x58, 0x02], d103)
    d105 = [f["data"] for f in fr if f["id"] == 0x105][0]
    check("0x105 = 400/400/395/405 kPa，20/21/20/22℃",
          d105 == [0x50, 0x50, 0x4F, 0x51, 0x3C, 0x3D, 0x3C, 0x3E], d105)
    post("/api/stop", {})

    _, j = post("/api/send", {"preset": "fault"})
    check("预设名 fault 可用", j.get("ok") is True, j)
    fr = drain(1.2)
    d104 = [f["data"] for f in fr if f["id"] == 0x104 and f["dir"] == "tx"][0]
    check("故障演示 0x104 B3=0x03（发动机+胎压告警）", d104[3] == 0x03, d104)
    check("故障演示 0x104 故障码 = 0x0301（P0301，小端 01 03）",
          d104[5] == 0x01 and d104[6] == 0x03 and d104[4] == 1, d104)
    post("/api/stop", {})
    drain(0.4)

    # ---------------- 9. 计数与清空 ----------------
    print("\n[9] 计数 / 清空")
    _, s1 = get("/api/status")
    check("rx/tx 计数都在涨", s1["rx"] > 0 and s1["tx"] > 0, s1)
    _, j = post("/api/send", {"id": 0x100, "data": [1, 0, 0, 0, 0, 0, 0, 0]})
    drain(0.4)
    _, s2 = get("/api/status")
    check("发一帧后 tx 计数 +1", s2["tx"] == s1["tx"] + 1, (s1["tx"], s2["tx"]))
    post("/api/clear", {})
    _, s3 = get("/api/status")
    check("清空后计数归零", s3["rx"] == 0 and s3["tx"] == 0, s3)
    _, j = get("/api/frames")
    check("清空后新帧列表为空", j["frames"] == [], j["frames"][:3])

    # ---------------- 10. GET /api/presets ----------------
    print("\n[10] 其它接口")
    _, j = get("/api/presets")
    check("GET /api/presets 含 4 个模式预设",
          all(k in j for k in ("mode_eco", "mode_normal", "mode_sport", "mode_snow")), list(j))
    _, j = post("/api/send", {"preset": "不存在的预设"})
    check("未知预设名报错但不崩", j.get("ok") is False and "error" in j, j)

    # /api/timer：count=0 = 一直发（前端每行的 ▶）
    _, j = post("/api/timer", {"id": 0x100, "data": [7, 0, 3, 8, 0, 0, 0, 0],
                               "period_ms": 200, "count": 0})
    check("POST /api/timer 支持 count=0（一直发）", j.get("ok") is True and j.get("token"), j)
    fr = drain(1.1)
    n = len([f for f in fr if f["id"] == 0x100 and f["dir"] == "tx"])
    check("/api/timer 周期 200 ms（1.1 s 内 ≥4 条）", n >= 4, n)
    post("/api/stop", {"token": j["token"]})
    drain(0.4)
    fr = drain(1.0)
    n = len([f for f in fr if f["id"] == 0x100 and f["dir"] == "tx"])
    check("单条定时器可停", n <= 1, n)


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass
    sys.exit(main())
