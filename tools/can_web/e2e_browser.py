#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
e2e_browser.py —— 用「真的浏览器」点一遍前端所有按钮，验证预设与解码文字
===============================================================================
纯标准库实现：
  · 起一个 can_bridge.py --sim（或者用已经在跑的）
  · 用 Chrome/Edge 的 --headless --remote-debugging-port 打开页面
  · 走 CDP（Chrome DevTools Protocol）的 WebSocket，用 Runtime.evaluate 在
    页面里点按钮、读接收区 DOM 的文本、读日志区
  · 断言：点每个预设后，接收区里出现预期 ID，解码文字与协议表一致
  · 顺便截一张全页图（--shot 指定路径）

为什么不用 selenium/playwright：本项目要求零依赖，这个脚本也只用标准库。

    python e2e_browser.py                    # 自动找 Chrome/Edge
    python e2e_browser.py --browser "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe"
    python e2e_browser.py --shot shot.png
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import re
import socket
import struct
import subprocess
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
PASS, FAIL = [], []


def check(name, cond, detail=""):
    (PASS if cond else FAIL).append(name)
    print("  %s %s%s" % ("OK  " if cond else "FAIL", name,
                         ("   " + str(detail)[:400]) if (detail and not cond) else ""))


# ------------------------------------------------------------------ 极简 WebSocket 客户端
class WS(object):
    def __init__(self, url):
        m = re.match(r"ws://([^:/]+):(\d+)(/.*)$", url)
        host, port, path = m.group(1), int(m.group(2)), m.group(3)
        self.sock = socket.create_connection((host, port), timeout=20)
        key = base64.b64encode(os.urandom(16)).decode()
        req = ("GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\n"
               "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
               "Sec-WebSocket-Version: 13\r\n\r\n" % (path, host, port, key))
        self.sock.sendall(req.encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            buf += self.sock.recv(4096)
        if b"101" not in buf.split(b"\r\n")[0]:
            raise RuntimeError("WebSocket 握手失败: %r" % buf[:200])
        self.buf = buf.split(b"\r\n\r\n", 1)[1]
        self.msgid = 0

    def _recv_exact(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise RuntimeError("连接被关闭")
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def send(self, text):
        payload = text.encode("utf-8")
        header = bytearray([0x81])
        n = len(payload)
        if n < 126:
            header.append(0x80 | n)
        elif n < 65536:
            header.append(0x80 | 126)
            header += struct.pack(">H", n)
        else:
            header.append(0x80 | 127)
            header += struct.pack(">Q", n)
        mask = os.urandom(4)
        header += mask
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(bytes(header) + masked)

    def recv(self):
        b0, b1 = self._recv_exact(2)
        length = b1 & 0x7F
        if length == 126:
            length = struct.unpack(">H", self._recv_exact(2))[0]
        elif length == 127:
            length = struct.unpack(">Q", self._recv_exact(8))[0]
        mask = self._recv_exact(4) if (b1 & 0x80) else None
        data = self._recv_exact(length)
        if mask:
            data = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
        return data.decode("utf-8", "replace")

    def call(self, method, params=None):
        self.msgid += 1
        mid = self.msgid
        self.send(json.dumps({"id": mid, "method": method, "params": params or {}}))
        end = time.time() + 25
        while time.time() < end:
            msg = json.loads(self.recv())
            if msg.get("id") == mid:
                if "error" in msg:
                    raise RuntimeError("%s → %s" % (method, msg["error"]))
                return msg.get("result", {})
        raise RuntimeError("CDP 调用超时: " + method)

    def evaluate(self, expr):
        r = self.call("Runtime.evaluate", {"expression": expr, "returnByValue": True,
                                           "awaitPromise": True})
        if r.get("exceptionDetails"):
            raise RuntimeError("页面里执行出错: %s" % json.dumps(r["exceptionDetails"])[:300])
        return r.get("result", {}).get("value")

    def wait_for(self, expr, timeout=15, interval=0.25):
        end = time.time() + timeout
        while time.time() < end:
            try:
                if self.evaluate(expr):
                    return True
            except Exception:
                pass
            time.sleep(interval)
        return False

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass


def find_browser(explicit=None):
    if explicit:
        return explicit
    cands = [
        r"C:\Program Files\Google\Chrome\Application\chrome.exe",
        r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
        os.path.expandvars(r"%LOCALAPPDATA%\Google\Chrome\Application\chrome.exe"),
        r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
        "/usr/bin/google-chrome", "/usr/bin/chromium", "/usr/bin/chromium-browser",
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    ]
    for c in cands:
        if os.path.isfile(c):
            return c
    return None


def http_json(url, timeout=5):
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8"))


def start_bridge(port):
    env = dict(os.environ, PYTHONIOENCODING="utf-8")
    p = subprocess.Popen([sys.executable, os.path.join(HERE, "can_bridge.py"),
                          "--sim", "--port", str(port), "--no-selftest"],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    for _ in range(80):
        try:
            http_json("http://127.0.0.1:%d/api/status" % port)
            return p
        except Exception:
            time.sleep(0.15)
    raise SystemExit("桥接进程没起来")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--browser", default=None)
    ap.add_argument("--port", type=int, default=8790)
    ap.add_argument("--cdp-port", type=int, default=9333)
    ap.add_argument("--shot", default=None, help="截一张全页图到这个路径")
    ap.add_argument("--width", type=int, default=1600, help="浏览器窗口宽（测布局用）")
    ap.add_argument("--height", type=int, default=1000, help="浏览器窗口高")
    ap.add_argument("--no-spawn", action="store_true")
    args = ap.parse_args()

    browser = find_browser(args.browser)
    if not browser:
        print("没找到 Chrome/Edge，跳过浏览器端到端测试。")
        return 0
    print("浏览器: %s" % browser)

    bridge = None if args.no_spawn else start_bridge(args.port)
    profile = os.path.join(HERE, "_chrome_profile")
    proc = subprocess.Popen([
        browser, "--headless=new", "--disable-gpu", "--no-first-run",
        "--no-default-browser-check", "--hide-scrollbars",
        "--window-size=%d,%d" % (args.width, args.height),
        "--remote-debugging-port=%d" % args.cdp_port,
        "--user-data-dir=" + profile, "about:blank"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    try:
        ver = None
        for _ in range(80):
            try:
                ver = http_json("http://127.0.0.1:%d/json/version" % args.cdp_port)
                break
            except Exception:
                time.sleep(0.25)
        if not ver:
            raise SystemExit("浏览器调试端口没起来")
        tab = None
        for t in http_json("http://127.0.0.1:%d/json/list" % args.cdp_port):
            if t.get("type") == "page":
                tab = t
                break
        ws = WS(tab["webSocketDebuggerUrl"])
        ws.call("Page.enable")
        ws.call("Runtime.enable")
        url = "http://127.0.0.1:%d/" % args.port
        ws.call("Page.navigate", {"url": url})
        time.sleep(1.0)
        ok = ws.wait_for("document.readyState === 'complete' && !!document.getElementById('btnTimeline')", 20)
        check("页面加载完成（能拿到 btnTimeline）", ok)

        run_layout_checks(ws, args.width)
        run_checks(ws, url)
        if args.shot:
            full = ws.call("Page.captureScreenshot", {"format": "png", "captureBeyondViewport": True})
            with open(args.shot, "wb") as fp:
                fp.write(base64.b64decode(full["data"]))
            print("  截图: %s" % args.shot)
        ws.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
        if bridge:
            bridge.terminate()
            try:
                bridge.wait(timeout=5)
            except Exception:
                bridge.kill()

    print("\n" + "=" * 70)
    print("浏览器端到端：通过 %d 项，失败 %d 项" % (len(PASS), len(FAIL)))
    for f in FAIL:
        print("  FAIL " + f)
    return 1 if FAIL else 0


def clear_all(ws):
    """先「全部停发」再「清空」：保证接下来看到的是本次预设发出的帧。"""
    ws.evaluate("document.getElementById('btnStopAll').click()")
    time.sleep(0.5)
    ws.evaluate("document.getElementById('btnClear').click()")
    time.sleep(0.9)


def rows(ws):
    """接收区所有行 → [{dir,id,dlc,data,dec}]"""
    return ws.evaluate("""
      Array.prototype.map.call(document.querySelectorAll('#rxTbody tr'), function (tr) {
        var td = tr.querySelectorAll('td');
        return { dir: td[1].textContent, id: td[2].textContent, dlc: td[3].textContent,
                 data: td[4].textContent, dec: td[5].textContent };
      })""")


def logtext(ws):
    return ws.evaluate("document.getElementById('log').textContent")


def run_layout_checks(ws, width):
    """问题 1 的回归：输入框够宽、标签不重叠、卡片不横向溢出。"""
    print("\n[0] 布局：输入框宽度 / 标签不重叠 / 不横向溢出（窗口 %d px）" % width)

    # ① 所有数值输入框至少 72px 宽、box-sizing=border-box、数字右对齐
    info = ws.evaluate("""
      Array.prototype.map.call(
        document.querySelectorAll('.grid-static input[type=number]'),
        function (e) {
          var cs = getComputedStyle(e), r = e.getBoundingClientRect();
          return { id: e.id, w: Math.round(r.width), box: cs.boxSizing,
                   align: cs.textAlign, val: e.value };
        })""")
    check("静态/里程卡片里的数值输入框都 ≥72px（共 %d 个）" % len(info),
          info and all(x["w"] >= 72 for x in info),
          [(x["id"], x["w"]) for x in info if x["w"] < 72])
    check("数值输入框 box-sizing = border-box",
          all(x["box"] == "border-box" for x in info),
          [(x["id"], x["box"]) for x in info if x["box"] != "border-box"][:3])
    check("数值输入框数字右对齐",
          all(x["align"] == "right" for x in info),
          [(x["id"], x["align"]) for x in info if x["align"] != "right"][:3])
    missing = [x["id"] for x in info if not str(x["val"]).strip()]
    check("没有空着看不出数值的输入框（原来胎温/油耗是空的）",
          not missing, missing)

    # ② 关键默认值：胎温 35、瞬时/平均油耗 6.5 / 7.4
    vals = ws.evaluate("""({
        mT0: document.getElementById('mT0').value,
        mT1: document.getElementById('mT1').value,
        mT2: document.getElementById('mT2').value,
        mT3: document.getElementById('mT3').value,
        mInst: document.getElementById('mInst').value,
        mAvg: document.getElementById('mAvg').value })""")
    check("四轮胎温默认都是 35℃（帧里 +40 → 75）",
          all(vals["mT" + str(i)] == "35" for i in range(4)), vals)
    check("瞬时油耗默认 65（=6.5 L/100km）、平均 74（=7.4 L/100km）",
          vals["mInst"] == "65" and vals["mAvg"] == "74", vals)

    # ③ 标签与相邻控件不重叠：每个「标签 + 控件」分组内，标签右边界 ≤ 控件左边界
    overlap = ws.evaluate("""
      (function(){
        var bad = [];
        Array.prototype.forEach.call(document.querySelectorAll('.grid-static .g'), function (g) {
          var lab = g.querySelector('span');
          var inp = g.querySelector('input, select');
          if (!lab || !inp) return;
          var a = lab.getBoundingClientRect(), b = inp.getBoundingClientRect();
          if (a.right > b.left + 0.5) bad.push([lab.textContent, Math.round(a.right - b.left)]);
        });
        return bad; })()""")
    check("标签没有压到相邻输入框（无重叠）", not overlap, overlap[:5])

    # ④ 卡片不横向溢出（滚动宽 ≤ 客户宽 + 1）；发送表允许在自己的 .table-wrap 里横向滚
    ovf = ws.evaluate("""
      (function(){
        var out = [];
        Array.prototype.forEach.call(document.querySelectorAll('.sec'), function (s) {
          if (s.scrollWidth > s.clientWidth + 1) {
            out.push([(s.querySelector('h3') || {}).textContent || '?', s.scrollWidth, s.clientWidth]);
          }
        });
        return out; })()""")
    check("发送区各卡片没有横向溢出（.table-wrap 内部滚动不算）", not ovf, ovf[:4])
    # 发送表自己可横向滚动（而不是把卡片撑破）
    tw = ws.evaluate("""(function(){ var w=document.querySelector('.table-wrap');
        return w ? {sw: w.scrollWidth, cw: w.clientWidth, ox: getComputedStyle(w).overflowX} : null; })()""")
    check("发送表容器 overflow-x 可滚（窄屏时表在里面滚）",
          tw and tw["ox"] in ("auto", "scroll"), tw)

    # ⑤ 顶栏 TX 成功/失败/待发 三个计数都在
    hdr = ws.evaluate("""({
        tx: document.getElementById('tx').textContent,
        fail: document.getElementById('txfail').textContent,
        queued: document.getElementById('txqueued').textContent,
        txLabel: document.querySelector('#tx').parentNode.textContent })""")
    check("顶栏有 TX 成功 / TX 失败 / 待发 三个计数",
          "TX 成功" in hdr["txLabel"] and hdr["fail"] != "" and hdr["queued"] != "", hdr)


def run_checks(ws, url):
    print("\n[1] 页面自检与顶栏")
    lt = logtext(ws)
    check("页面加载即跑了前端解码自检，且全部通过",
          "解码自检" in lt and "全部通过" in lt, lt[-500:])
    check("自检里没有 FAIL", "FAIL" not in lt, [l for l in lt.split("\n") if "FAIL" in l][:3])
    st = ws.evaluate("({c: document.getElementById('conn').textContent,"
                     " ch: document.getElementById('channel').textContent,"
                     " br: document.getElementById('bitrate').textContent,"
                     " bk: document.getElementById('backend').textContent})")
    check("顶栏显示已连接桥接进程", st["c"] == "已连接桥接进程", st)
    check("顶栏显示通道 PCAN_USBBUS1", st["ch"] == "PCAN_USBBUS1", st)
    check("顶栏显示波特率 500 kbit/s", st["br"] == "500 kbit/s", st)
    check("顶栏显示 sim 后端", st["bk"] == "sim 假帧（不碰硬件）", st)
    check("「后端」标签由 backend 字符串映射（另一个桥接 PCANBasic 也能显示）",
          ws.evaluate("backendLabel({backend:'PCANBasic'})") == "PCANBasic（PEAK 驱动）" and
          ws.evaluate("backendLabel({backend:'python-can/pcan'})") == "python-can/pcan" and
          ws.evaluate("backendLabel({})") == "—",
          [ws.evaluate("backendLabel({backend:'PCANBasic'})"),
           ws.evaluate("backendLabel({backend:'python-can/pcan'})"),
           ws.evaluate("backendLabel({})")])

    print("\n[2] 接收区：sim 上行假帧自动出现")
    ok = ws.wait_for("document.querySelectorAll('#rxTbody tr').length >= 3", 12)
    check("接收区出现板子上行帧", ok, len(rows(ws)))
    r = rows(ws)
    check("出现 0x201/0x202/0x203/0x204 中的至少三种",
          len(set(x["id"] for x in r) & set(["0x201", "0x202", "0x203", "0x204"])) >= 3,
          sorted(set(x["id"] for x in r)))
    dec201 = [x["dec"] for x in r if x["id"] == "0x201"]
    check("0x201 解码成中文（空调…）", dec201 and dec201[0].startswith("空调 开 · 温度 24℃"),
          dec201[:1])
    check("DLC 列都是 8", all(x["dlc"] == "8" for x in r), [x for x in r if x["dlc"] != "8"][:2])

    print("\n[3] 预设 1：开车时间线")
    clear_all(ws)
    ws.evaluate("document.getElementById('btnTimeline').click()")
    ok = ws.wait_for("document.getElementById('btnTimeline').textContent.indexOf('停止') >= 0", 8)
    check("按钮变成「停止开车时间线」", ok)
    time.sleep(3.0)
    r = rows(ws)
    ids = set(x["id"] for x in r)
    check("时间线发出 0x100/0x101/0x102", {"0x100", "0x101", "0x102"} <= ids, sorted(ids))
    check("0x100 同时有 TX 与 RX 回显",
          any(x["id"] == "0x100" and x["dir"].startswith("TX") for x in r) and
          any(x["id"] == "0x100" and x["dir"].startswith("RX") for x in r))
    d100 = [x for x in r if x["id"] == "0x100" and x["dir"].startswith("TX")]
    check("第一段是怠速（数据 00 … 03 …）",
          d100 and d100[0]["data"].startswith("00") and d100[0]["data"].split()[2] == "03",
          d100[:1])
    check("解码文字含「车速 … 档位 D」",
          d100 and "档位 D" in d100[0]["dec"] and "车速" in d100[0]["dec"], d100[:1])
    ws.evaluate("document.getElementById('btnTimeline').click()")
    time.sleep(0.5)
    check("再点一次能停下", "开车时间线" in ws.evaluate(
        "document.getElementById('btnTimeline').textContent"))

    print("\n[4] 预设 2：静态车况（周期发送 + 可编辑）")
    clear_all(ws)
    ws.evaluate("""
      var s=document.getElementById('sSpeed'); s.value='66';
      document.getElementById('sGear').value='3';
      document.getElementById('sLeft').checked=true;
      document.getElementById('sBrake').checked=true;
      document.getElementById('sSeatbelt').checked=true;
      document.getElementById('sHandbrake').checked=true;
      document.getElementById('sHazard').checked=true;
      document.getElementById('sReverse').checked=true;
      document.getElementById('sDoor0').checked=true;
      document.getElementById('sDoor4').checked=true;
      document.getElementById('sLocked').checked=false;
      document.getElementById('sLight1').checked=true;
      document.getElementById('sAc').checked=true;
      document.getElementById('sRecirc').checked=true;
      document.getElementById('sAcTemp').value='26';
      document.getElementById('sOutTemp').value='18';
      ['sSpeed','sGear','sLeft','sBrake','sSeatbelt','sHandbrake','sHazard','sReverse',
       'sDoor0','sDoor4','sLocked','sLight1','sAc','sRecirc','sAcTemp','sOutTemp']
        .forEach(function(id){var e=document.getElementById(id);
          e.dispatchEvent(new Event('change',{bubbles:true}));});
    """)
    prev = ws.evaluate("document.getElementById('staticPreview').textContent")
    check("静态车况预览按掩码展开（左转/刹车/倒车/安全带未系/手刹/双闪）",
          all(k in prev for k in ("左转", "刹车", "倒车", "安全带未系", "手刹", "双闪")), prev)
    check("预览 0x101 显示左前门+后备箱开、未上锁",
          "左前门/后备箱" in prev and "未上锁" in prev, prev)
    check("预览 0x102 显示 AC开 26℃ 内循环 车外 18℃",
          "AC开 26℃ 内循环" in prev and "车外 18℃" in prev, prev)

    ws.evaluate("document.getElementById('btnStaticToggle').click()")
    time.sleep(2.0)
    r = rows(ws)
    tx = [x for x in r if x["dir"].startswith("TX")]
    check("静态车况按周期发送 0x100/0x101/0x102",
          {"0x100", "0x101", "0x102"} <= set(x["id"] for x in tx),
          sorted(set(x["id"] for x in tx)))
    n100 = len([x for x in tx if x["id"] == "0x100"])
    check("0x100 至少发了 10 条（100 ms 周期）", n100 >= 10, n100)
    one = [x for x in tx if x["id"] == "0x100"][0]
    # bit0 左转|bit1 右转|bit2 刹车|bit3 倒车|bit4 安全带|bit5 手刹|bit6 双闪 = 0x7F，但右转没勾 → 0x7D
    check("0x100 字节 = 42 7D 03 08（66 km/h、左转/刹车/倒车/安全带/手刹/双闪、D 档、800 rpm）",
          one["data"] == "42 7D 03 08 00 00 00 00", one["data"])
    check("0x101 字节 = 11 00（左前门+后备箱，未上锁）",
          [x for x in tx if x["id"] == "0x101"][0]["data"] == "11 00 00 00 00 00 00 00",
          [x for x in tx if x["id"] == "0x101"][0]["data"])
    check("0x102 字节 = 03 03 1A 3A（近光+远光，AC+内循环，26℃，车外 18℃）",
          [x for x in tx if x["id"] == "0x102"][0]["data"] == "03 03 1A 3A 00 00 00 00",
          [x for x in tx if x["id"] == "0x102"][0]["data"])
    ws.evaluate("document.getElementById('btnStaticToggle').click()")
    time.sleep(0.4)

    print("\n[5] 预设 3：工作模式 0x204（自动重发 3 次）")
    for mode, name in ((0, "经济"), (1, "标准"), (2, "运动"), (3, "雪地")):
        clear_all(ws)
        ws.evaluate("document.querySelector('button[data-mode=\"%d\"]').click()" % mode)
        time.sleep(1.2)
        r = [x for x in rows(ws) if x["id"] == "0x204" and x["dir"].startswith("TX")]
        check("模式 %s：发 0x204 ×3，解码「工作模式 → %s」" % (name, name),
              len(r) == 3 and all(("工作模式 → " + name) in x["dec"] for x in r),
              [(x["data"], x["dec"]) for x in r][:3])
    check("模式状态标签更新", "雪地" in ws.evaluate("document.getElementById('modeState').textContent"),
          ws.evaluate("document.getElementById('modeState').textContent"))

    print("\n[6] 预设 4：里程油耗组（点一下 = 各 1 次；循环要另外点）")
    clear_all(ws)
    ws.evaluate("document.getElementById('btnMileage').click()")
    time.sleep(1.6)
    r = rows(ws)
    got = {}
    for x in r:
        got.setdefault(x["id"], x)
    check("发出 0x103/0x104/0x105", all(k in got for k in ("0x103", "0x104", "0x105")),
          sorted(got.keys()))
    tx103 = [x for x in r if x["id"] == "0x103" and x["dir"].startswith("TX")]
    check("点一下只发 1 次（不是循环灌帧）：0x103 只有 1 条 TX",
          len(tx103) == 1, len(tx103))
    check("日志显示 HTTP 状态与登记结果",
          "HTTP 200" in logtext(ws) and "已登记" in logtext(ws),
          [l for l in logtext(ws).split("\n") if "TX" in l][-3:])
    check("日志显示总线确认（TX 成功 +N 或还在排队）",
          ("总线确认" in logtext(ws) or "还在排队" in logtext(ws)),
          [l for l in logtext(ws).split("\n") if "TX #" in l][-3:])
    check("0x103 数据 = 10 27 00 50 00 32 58 02",
          got.get("0x103", {}).get("data") == "10 27 00 50 00 32 58 02",
          got.get("0x103", {}).get("data"))
    check("0x103 解码 = 总里程 1000.0 km · 小计 8.0 km · 油量 50% · 续航 600 km",
          got.get("0x103", {}).get("dec") == "总里程 1000.0 km · 小计 8.0 km · 油量 50% · 续航 600 km",
          got.get("0x103", {}).get("dec"))
    check("0x104 解码含 6.5 / 7.4 L/100km、模式 标准、告警[-]（页面上填的默认值）",
          all(k in got.get("0x104", {}).get("dec", "") for k in ("6.5", "7.4", "标准", "告警[-]")),
          got.get("0x104", {}).get("dec"))
    check("0x105 解码 = 胎压 400/400/395/405 kPa、胎温 35℃×4（默认值）",
          "400 kPa" in got.get("0x105", {}).get("dec", "") and
          "395 kPa" in got.get("0x105", {}).get("dec", "") and
          got.get("0x105", {}).get("dec", "").count("35℃") == 4,
          got.get("0x105", {}).get("dec"))
    check("0x104 解码里能看到日志行（日志区可核对）",
          "0x104" in logtext(ws), logtext(ws)[-200:])

    print("\n[6b] 循环按钮：点了之后才会持续发")
    clear_all(ws)
    ws.evaluate("document.getElementById('btnMileageLoop').click()")
    time.sleep(1.6)
    n = len([x for x in rows(ws) if x["id"] == "0x103" and x["dir"].startswith("TX")])
    check("里程油耗循环：1.6 s 内 0x103 发了 ≥2 条", n >= 2, n)
    ws.evaluate("document.getElementById('btnStopAll').click()")
    time.sleep(0.5)

    print("\n[7] 预设 5：故障演示（各 1 次；循环另点）")
    clear_all(ws)
    ws.evaluate("document.getElementById('btnFault').click()")
    time.sleep(1.4)
    r = rows(ws)
    f104 = [x for x in r if x["id"] == "0x104"]
    check("发出 0x104", bool(f104), sorted(set(x["id"] for x in r)))
    check("故障演示点一下只发 1 次（不循环）：日志里只有 1 条 0x104 请求",
          logtext(ws).count("0x104 [62 55 01 03 01 01 03 00]") == 1,
          [l for l in logtext(ws).split("\n") if "0x104" in l][-3:])
    check("0x104 B3 = 0x03（发动机+胎压告警位）",
          f104 and f104[0]["data"].split()[3] == "03", f104[:1])
    check("0x104 解码含「告警[发动机/胎压]」「故障 1 个」「故障码 P0301」",
          f104 and all(k in f104[0]["dec"] for k in ("告警[发动机/胎压]", "故障 1 个", "故障码 P0301")),
          f104[:1])

    print("\n[8] 预设 全部停发 / 清空 / 暂停 / CSV")
    ws.evaluate("document.getElementById('btnStopAll').click()")
    time.sleep(0.6)
    clear_all(ws)
    r = rows(ws)
    check("全部停发后清空，接收区在 1 秒内基本不再增长",
          len(r) <= 2, len(r))

    ws.evaluate("document.getElementById('btnPause').click()")
    time.sleep(0.8)
    before = len(rows(ws))
    time.sleep(1.2)
    after = len(rows(ws))
    check("暂停后接收区不再新增行（不碰 DOM）", after == before, (before, after))
    check("暂停期间帧进队列（没丢，等着继续时补上）",
          ws.evaluate("(window.rx && rx.queue.length) || 0") > 0,
          ws.evaluate("(window.rx && rx.queue.length) || 0"))
    ws.evaluate("document.getElementById('btnPause').click()")
    time.sleep(1.2)
    check("继续后又开始新增行", len(rows(ws)) > after, (after, len(rows(ws))))

    csv = ws.evaluate("""
      (function(){ var out=null; var orig=URL.createObjectURL;
        URL.createObjectURL=function(b){ out=b; return orig.call(URL,b); };
        document.getElementById('btnCsv').click();
        URL.createObjectURL=orig;
        return out ? true : false; })()""")
    check("导出 CSV 会生成 blob", csv is True, csv)
    rowsnow = ws.evaluate("document.querySelectorAll('#rxTbody tr').length")
    check("表格行数 ≤ 300（问题 2：上限 500 → 300）", rowsnow <= 300, rowsnow)
    check("「只显示最近 200」勾上后行数 ≤ 200", ws.evaluate("""
      (function(){ var c=document.getElementById('chkRecent');
        c.checked=true; c.dispatchEvent(new Event('change',{bubbles:true}));
        return document.querySelectorAll('#rxTbody tr').length <= 200; })()"""),
      ws.evaluate("document.querySelectorAll('#rxTbody tr').length"))
    check("没有 JS 报错进日志（无 FAIL）",
          "FAIL" not in logtext(ws), [l for l in logtext(ws).split("\n") if "FAIL" in l][:3])

    print("\n[9] 可编辑发送表：默认次数=1，加一行 → 指定 ID/字节/次数 → 发")
    clear_all(ws)
    defcnt = ws.evaluate("""
      Array.prototype.map.call(document.querySelectorAll('#txTbody input.b-cnt'),
                               function(e){ return e.value; })""")
    check("发送表默认「次数」= 1（不是 0/100）",
          defcnt and all(v == "1" for v in defcnt), defcnt)
    ws.evaluate("""
      (function(){
        document.getElementById('btnAddRow').click();
        var tr = document.getElementById('txTbody').lastChild;
        var ins = tr.querySelectorAll('input');
        ins[0].value = '0x204';            // ID
        ins[1].value = '03';               // B0 = 雪地
        var cnt = tr.querySelector('input.b-cnt'); cnt.value = '2';
        tr.querySelectorAll('button')[0].click();   // 「发」
      })()""")
    time.sleep(1.2)
    r = [x for x in rows(ws) if x["id"] == "0x204" and x["dir"].startswith("TX")]
    check("发送表手动行发出 0x204 ×2 且解码为雪地",
          len(r) == 2 and all("工作模式 → 雪地" in x["dec"] for x in r),
          [(x["data"], x["dec"]) for x in r])
    check("日志显示 HTTP 状态 + 总线确认",
          "HTTP 200" in logtext(ws) and ("总线确认" in logtext(ws) or "已登记" in logtext(ws)),
          logtext(ws)[-400:])
    check("顶栏 TX 成功计数在涨", ws.evaluate(
        "parseInt(document.getElementById('tx').textContent, 10) > 0"),
        ws.evaluate("document.getElementById('tx').textContent"))
    check("顶栏 TX 失败计数为 0（sim 下不该有失败）", ws.evaluate(
        "document.getElementById('txfail').textContent") == "0",
        ws.evaluate("document.getElementById('txfail').textContent"))

    print("\n[10] 单次点击的突发上限（前端把次数夹到 10）")
    clear_all(ws)
    ws.evaluate("""
      (function(){
        document.getElementById('btnAddRow').click();
        var tr = document.getElementById('txTbody').lastChild;
        tr.querySelectorAll('input')[0].value = '0x201';
        tr.querySelector('input.b-cnt').value = '99';       // 想灌 99 帧
        tr.querySelectorAll('button')[0].click();
      })()""")
    time.sleep(1.5)
    n = len([x for x in rows(ws) if x["id"] == "0x201" and x["dir"].startswith("TX")])
    check("次数填 99 也只发 10 帧（前端夹紧）", n == 10, n)


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass
    sys.exit(main())
