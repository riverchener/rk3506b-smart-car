/* ============================================================================
 * app.js —— RK3506B 车机 CAN 调试台前端（纯原生 JS，无框架、无外网资源）
 *
 *   接收：每 250 ms 轮询 GET /api/frames，解码后滚动进表格（最多 500 行）
 *   发送：POST /api/send（单帧 count/period_ms 重发、批量、周期定时器、开车时间线）
 *
 *   解码/编帧严格按《CAN 接口定义》：
 *     下行 0x100 驾驶状态 / 0x101 车门舱盖 / 0x102 灯光空调
 *          0x103 里程油耗 / 0x104 油耗告警故障码 / 0x105 胎压胎温
 *     上行 0x201 空调 / 0x202 车窗 / 0x203 灯光 / 0x204 工作模式
 * ========================================================================= */

/* ------------------------------------------------------------------ 常量 */
var GEAR_NAMES  = ['P', 'R', 'N', 'D'];                       // 0/1/2/3
var MODE_NAMES  = ['经济', '标准', '运动', '雪地'];             // 0/1/2/3
var LIGHT_BITS  = ['近光', '远光', '示宽', '前雾', '后雾', '氛围灯'];
var DOOR_BITS   = ['左前门', '右前门', '左后门', '右后门', '后备箱', '引擎盖'];
var FLAG_BITS   = ['左转', '右转', '刹车', '倒车', '安全带未系', '手刹', '双闪'];
var WARN_BITS   = ['发动机', '胎压', '油量低', '保养', 'ABS', '蓄电池', '制动', '其它'];
var WIN_POS     = { 0: '全部', 1: '左前', 2: '右前', 3: '左后', 4: '右后' };
var WIN_ACT     = { 0: '停', 1: '升', 2: '降' };
var DOWN_IDS    = [0x100, 0x101, 0x102, 0x103, 0x104, 0x105];
var UP_IDS      = [0x201, 0x202, 0x203, 0x204];
var MAX_ROWS    = 300;       // 接收区最多保留（DOM 行数上限）
var RECENT_ROWS = 200;       // 勾「只显示最近 200」时的上限
var MAX_BURST   = 10;        // 单次请求最多发几帧（与桥接 Controller.MAX_BURST 一致）
var POLL_MS     = 250;

/* ------------------------------------------------------------------ 小工具 */
function $(id) { return document.getElementById(id); }

function hx(n, w) {                       // 定宽大写十六进制
  var s = (n >>> 0).toString(16).toUpperCase();
  while (s.length < (w || 2)) { s = '0' + s; }
  return s;
}
function byteHex(b) { return hx(b & 0xFF, 2); }
function dataHex(d) {                      // "10 27 00 ..."（不足 8 补 00）
  var out = [];
  for (var i = 0; i < 8; i++) { out.push(byteHex(i < d.length ? d[i] : 0)); }
  return out.join(' ');
}
function le16(d, i) { return ((d[i] & 0xFF) | ((d[i + 1] & 0xFF) << 8)) >>> 0; }
function le24(d, i) {
  return ((d[i] & 0xFF) | ((d[i + 1] & 0xFF) << 8) | ((d[i + 2] & 0xFF) << 16)) >>> 0;
}
function bitsOf(mask, names, sep) {        // 位掩码 → "近光/远光"
  var out = [];
  for (var i = 0; i < names.length; i++) { if (mask & (1 << i)) { out.push(names[i]); } }
  return out.length ? out.join(sep || '/') : '-';
}
function num(v) { var n = parseInt(v, 10); return isNaN(n) ? 0 : n; }
function clamp(n, lo, hi) { n = num(n); return n < lo ? lo : (n > hi ? hi : n); }
function pad(n, w) { var s = String(n); while (s.length < w) { s = ' ' + s; } return s; }
function clockOf(ms) {                     // 本地时间 HH:MM:SS.mmm
  var d = new Date(ms || 0);
  return pad(d.getHours(), 2) + ':' + pad(d.getMinutes(), 2) + ':' + pad(d.getSeconds(), 2) +
         '.' + pad(d.getMilliseconds(), 3);
}
function toHexStr(d) {                     // [66,1,3,8,0,0,0,0] → "42 01 03 08 00 00 00 00"
  var out = [];
  for (var i = 0; i < (d ? d.length : 0); i++) { out.push(byteHex(d[i])); }
  return out.join(' ');
}

/* ================================================================ 解码核心 */
/* 每个解码函数返回中文文字；未知 ID 返回 "0xXXX 数据" 样式。 */

function decode0x100(d) {
  var speed = d[0] === 0xFF ? '无效' : (d[0] + ' km/h');
  var gear = d[2] === 0xFF ? '无效' : (GEAR_NAMES[d[2]] !== undefined ? GEAR_NAMES[d[2]] : '?' + d[2]);
  var spins = '';
  for (var i = 0; i < FLAG_BITS.length; i++) {
    if (d[1] & (1 << i)) { spins += (spins ? ' ' : '') + FLAG_BITS[i]; }
  }
  return '车速 ' + speed + ' · 档位 ' + gear + ' · 转速 ' + (d[3] * 100) + ' rpm' +
         (spins ? ' · ' + spins : ' · 无标志位');
}

function decode0x101(d) {
  return '开门：' + bitsOf(d[0], DOOR_BITS) + ' · ' + ((d[1] & 1) ? '全车已锁' : '未上锁');
}

function decode0x102(d) {
  var out = (d[1] & 1) ? 'AC开' : 'AC关';
  out += ' ' + d[2] + '℃';
  out += ' ' + ((d[1] & 2) ? '内循环' : '外循环');
  out += ' · 车外 ' + (d[3] - 40) + '℃';
  if (d.length > 4 && (d[4] || d[5] || d[6] || d[7])) {
    out += ' · 保留 ' + byteHex(d[4]) + ' ' + byteHex(d[5]) + ' ' + byteHex(d[6]) + ' ' + byteHex(d[7]);
  }
  return out;
}

function decode0x103(d) {
  var total = le24(d, 0);
  var trip = le16(d, 3);
  var fuel = d[5];
  var range = le16(d, 6);
  return '总里程 ' + (total === 0xFFFFFF ? '无效' : (total / 10).toFixed(1) + ' km') +
         ' · 小计 ' + (trip / 10).toFixed(1) + ' km' +
         ' · 油量 ' + (fuel === 0 || fuel === 0xFF ? '无效' : fuel + '%') +
         ' · 续航 ' + range + ' km';
}

function decode0x104(d) {
  var warn = bitsOf(d[3], WARN_BITS);
  var dtc = '';
  if (d[4] > 0) {
    /* 板子口径（docs/车辆状态与模式_20261001.md §2.2）：帧里传 16 位码值，
       高字节 = P 码低 8 位，界面就是"把 4 位十六进制码值前面加个 P"：
       0x0301 → P0301（不是标准 OBD 的 P/C/B/U 编码，别改成标准算法）。 */
    var raw = le16(d, 5);
    dtc = ' · 故障码 P' + hx(raw & 0xFFFF, 4);
  }
  return '瞬时油耗 ' + (d[0] / 10).toFixed(1) + ' L/100km' +
         ' · 平均 ' + (d[1] / 10).toFixed(1) + ' L/100km' +
         ' · 模式 ' + (MODE_NAMES[d[2]] || '?' + d[2]) +
         ' · 告警[' + warn + ']' +
         ' · 故障 ' + d[4] + ' 个' + dtc;
}

function decode0x105(d) {
  var order = ['左前', '右前', '左后', '右后'];
  var p = [], t = [];
  for (var i = 0; i < 4; i++) {
    p.push(order[i] + ' ' + (d[i] === 0 || d[i] === 0xFF ? '无效' : (d[i] * 5) + ' kPa'));
    t.push(order[i] + ' ' + (d[4 + i] === 0xFF ? '无效' : (d[4 + i] - 40) + '℃'));
  }
  return '胎压 ' + p.join(' / ') + ' · 胎温 ' + t.join(' / ');
}

function decode0x201(d) {
  var sw = d[0] === 0 ? '关' : (d[0] === 1 ? '开' : '?' + d[0]);
  return '空调 ' + sw + ' · 温度 ' + d[1] + '℃ · 风量 ' + d[2] +
         ' · ' + ((d[3] & 1) ? '内循环' : '外循环');
}

function decode0x202(d) {
  var pos = WIN_POS[d[0]] !== undefined ? WIN_POS[d[0]] : '?' + d[0];
  var act = WIN_ACT[d[1]] !== undefined ? WIN_ACT[d[1]] : '?' + d[1];
  return '车窗 ' + pos + ' ' + act + ' · 目标 ' + d[2] + '%';
}

function decode0x203(d) {
  return '灯光 开[' + bitsOf(d[0], LIGHT_BITS) + '] 关[' + bitsOf(d[1], LIGHT_BITS) + ']';
}

function decode0x204(d) {
  return '工作模式 → ' + (MODE_NAMES[d[0]] !== undefined ? MODE_NAMES[d[0]] : '?' + d[0]);
}

function decodeFrame(id, d) {
  switch (id) {
    case 0x100: return decode0x100(d);
    case 0x101: return decode0x101(d);
    case 0x102: return decode0x102(d);
    case 0x103: return decode0x103(d);
    case 0x104: return decode0x104(d);
    case 0x105: return decode0x105(d);
    case 0x201: return decode0x201(d);
    case 0x202: return decode0x202(d);
    case 0x203: return decode0x203(d);
    case 0x204: return decode0x204(d);
    default:    return '（未定义 ID）' + dataHex(d);
  }
}
function isKnown(id) { return DOWN_IDS.indexOf(id) >= 0 || UP_IDS.indexOf(id) >= 0; }

/* ================================================================ 编帧 */
function build0x100(o) {
  var f = 0;
  if (o.left) f |= 1;
  if (o.right) f |= 2;
  if (o.brake) f |= 4;
  if (o.reverse) f |= 8;
  if (o.seatbelt) f |= 16;
  if (o.handbrake) f |= 32;
  if (o.hazard) f |= 64;
  return [o.speedInvalid ? 0xFF : clamp(o.speed, 0, 254), f, clamp(o.gear, 0, 3), clamp(o.rpm, 0, 255), 0, 0, 0, 0];
}
function build0x101(o) {
  var m = 0;
  for (var i = 0; i < 6; i++) { if (o['door' + i]) m |= (1 << i); }
  return [m, o.locked ? 1 : 0, 0, 0, 0, 0, 0, 0];
}
function build0x102(o) {
  var m = 0;
  for (var i = 0; i < 6; i++) { if (o['light' + i]) m |= (1 << i); }
  var b1 = (o.ac ? 1 : 0) | (o.recirc ? 2 : 0);
  return [m, b1, clamp(o.acTemp, 16, 30), (clamp(o.outTemp, -40, 215) + 40) & 0xFF, 0, 0, 0, 0];
}
function build0x103(o) {
  var tot = clamp(o.total, 0, 0xFFFFFF);
  var trip = clamp(o.trip, 0, 0xFFFF);
  var rng = clamp(o.range, 0, 0xFFFF);
  return [tot & 0xFF, (tot >> 8) & 0xFF, (tot >> 16) & 0xFF,
          trip & 0xFF, (trip >> 8) & 0xFF, clamp(o.fuel, 0, 255),
          rng & 0xFF, (rng >> 8) & 0xFF];
}
function build0x104(o) {
  var warn = 0;
  for (var i = 0; i < 8; i++) { if (o['warn' + i]) warn |= (1 << i); }
  var dtc = clamp(o.dtc, 0, 0xFFFF);
  var n = o.faultCount === undefined || o.faultCount === null ? (warn ? 1 : 0) : clamp(o.faultCount, 0, 255);
  return [clamp(o.inst, 0, 255), clamp(o.avg, 0, 255), clamp(o.mode, 0, 3), warn, n,
          dtc & 0xFF, (dtc >> 8) & 0xFF, 0];
}
function build0x105(o) {
  return [clamp(o.p0, 0, 255), clamp(o.p1, 0, 255), clamp(o.p2, 0, 255), clamp(o.p3, 0, 255),
          (clamp(o.t0, 0, 215) + 40) & 0xFF, (clamp(o.t1, 0, 215) + 40) & 0xFF,
          (clamp(o.t2, 0, 215) + 40) & 0xFF, (clamp(o.t3, 0, 215) + 40) & 0xFF];
}

/* ================================================================ 后端 API */
var api = {
  pending: 0,
  /* POST：把 HTTP 状态码和响应体原样交回调用方，不抛异常也不吞错误 ——
     "发没发出去"必须能一眼看见（问题 3）。 */
  post: function (path, body) {
    return fetch(path, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body || {})
    }).then(function (r) {
      return r.text().then(function (t) {
        var j = null;
        try { j = JSON.parse(t); } catch (e) { j = null; }
        return { status: r.status, ok: r.ok, body: j, text: j ? null : t };
      });
    });
  },
  /* POST 但只关心成功与否（停发、清空这类） */
  postOk: function (path, body) {
    return api.post(path, body).then(function (r) {
      if (!r.ok || !r.body || r.body.ok === false) {
        throw new Error('HTTP ' + r.status + ' ' +
                        ((r.body && r.body.error) || r.text || ''));
      }
      return r.body;
    });
  },
  get: function (path, timeoutMs) {
    if (!timeoutMs) {
      return fetch(path, { cache: 'no-store' }).then(function (r) { return r.json(); });
    }
    return new Promise(function (resolve, reject) {
      var timer = setTimeout(function () { reject(new Error('请求超时 ' + timeoutMs + ' ms')); }, timeoutMs);
      fetch(path, { cache: 'no-store' }).then(function (r) { return r.json(); })
        .then(function (j) { clearTimeout(timer); resolve(j); })
        .catch(function (e) { clearTimeout(timer); reject(e); });
    });
  }
};

function log(msg, cls) {
  var box = $('log');
  var line = document.createElement('div');
  if (cls) { line.className = cls; }
  line.textContent = clockOf(Date.now()) + '  ' + msg;
  box.appendChild(line);
  while (box.childNodes.length > 200) { box.removeChild(box.firstChild); }
  box.scrollTop = box.scrollHeight;
}

/* ================================================================ 接收区
 * 渲染策略（防卡顿）：
 *   · /api/frames 只给"新增帧"，所以本来就是增量；
 *   · 新行先攒进 queue，一个 requestAnimationFrame 里用 DocumentFragment 一次性插入；
 *   · 暂停时**只入队、绝不碰 DOM**（队列有上限，不会无限涨）；
 *   · 表格最多 MAX_ROWS 行（勾「只显示最近 200」则 200 行）。
 */
var rx = {
  paused: false,
  rows: 0,
  queue: [],          // 待渲染的行数据
  csv: [],            // 导出用（暂停期间的也在里面）
  tbody: null,
  raf: 0
};

function rowCap() {
  return ($('chkRecent') && $('chkRecent').checked) ? Math.min(RECENT_ROWS, MAX_ROWS) : MAX_ROWS;
}

function rowVisible(id) {
  var d = $('chkDown').checked, u = $('chkUp').checked;
  if (!d && !u) { return true; }
  if (d && DOWN_IDS.indexOf(id) >= 0) { return true; }
  if (u && UP_IDS.indexOf(id) >= 0) { return true; }
  return false;
}

/* 建一行 <tr>（不插入 DOM） */
function makeRow(f) {
  var d = f.data || [];
  var id = f.id;
  var tr = document.createElement('tr');
  tr.className = (f.dir === 'tx' ? 'tx' : 'rx') + (isKnown(id) ? '' : ' unknown');
  var tds = [
    ['d-time', clockOf(f.t)],
    ['d-dir', f.dir === 'tx' ? 'TX →' : 'RX ←'],
    ['d-id', '0x' + hx(id, 3)],
    ['d-dlc', String(d.length)],
    ['d-data', dataHex(d)],
    ['dec', decodeFrame(id, d)]
  ];
  for (var i = 0; i < tds.length; i++) {
    var td = document.createElement('td');
    td.className = tds[i][0];
    td.textContent = tds[i][1];
    tr.appendChild(td);
  }
  return tr;
}

/* 一帧一批：插入 → 裁剪 → 记数（整批只在一次 rAF 里做一次） */
function flushRows() {
  rx.raf = 0;
  if (rx.paused || !rx.queue.length) { return; }   // 暂停：DOM 一概不动
  var batch = rx.queue;
  rx.queue = [];
  var frag = document.createDocumentFragment();
  for (var i = 0; i < batch.length; i++) { frag.appendChild(makeRow(batch[i])); }
  rx.tbody.appendChild(frag);                      // 一次插入，只触发一次重排
  rx.rows += batch.length;
  var cap = rowCap();
  while (rx.rows > cap && rx.tbody.firstChild) {
    rx.tbody.removeChild(rx.tbody.firstChild);
    rx.rows--;
  }
  $('rows').textContent = String(rx.rows);
  if ($('chkAutoScroll').checked) {
    var body = $('rxBody');
    body.scrollTop = body.scrollHeight;
  }
}

function scheduleFlush() {
  if (rx.raf || rx.paused) { return; }
  if (typeof requestAnimationFrame === 'function') {
    rx.raf = requestAnimationFrame(flushRows);
  } else {
    rx.raf = setTimeout(flushRows, 16);
  }
}

function handleFrames(list) {
  var queued = 0;
  for (var i = 0; i < list.length; i++) {
    var f = list[i];
    rx.csv.push(f);
    if (rowVisible(f.id)) {
      if (rx.queue.length < 3000) { rx.queue.push(f); queued++; }   // 上限：暂停久了也不撑爆内存
    }
  }
  if (rx.csv.length > 20000) { rx.csv = rx.csv.slice(-20000); }
  if (queued) { scheduleFlush(); }
}

/* 暂停切换：继续时把攒下的行一次刷出来 */
function setPaused(p) {
  rx.paused = p;
  $('btnPause').textContent = p ? '继续' : '暂停';
  $('btnPause').className = p ? 'on' : '';
  var n = rx.queue.length;
  $('rxState').textContent = p ? ('已暂停（' + n + ' 帧待显示）') : '接收中';
  $('rxState').className = p ? 'badge' : 'badge live';
  if (!p) { scheduleFlush(); }
  log(p ? '接收区：暂停显示（不再碰 DOM，帧仍记进导出）' : '接收区：继续（补上暂停期间的帧）');
}

function poll() {
  if (api.pending > 0) { return; }
  api.pending++;
  api.get('/api/frames', 4000).then(function (j) {
    api.pending--;
    setConn(true, '');
    handleFrames(j.frames || []);
    (j.errors || []).forEach(function (e) { log('⚠ 桥接：' + e.msg, 'e'); });
    if (j.status) { showStatus(j.status); }
  }).catch(function (e) {
    api.pending--;
    setConn(false, '轮询失败：' + e.message + '（桥接进程还在跑吗？）');
  });
}

function backendLabel(s) {
  /* 「后端」这一格显示人话。两个桥接共用这个前端：
       can_bridge.py        → sim / python-can/pcan
       pcanbasic_bridge.py  → PCANBasic（PEAK 驱动，不走 python-can） */
  var b = s.backend || '';
  if (b === 'sim') { return 'sim 假帧（不碰硬件）'; }
  if (b === 'PCANBasic') { return 'PCANBasic（PEAK 驱动）'; }
  return b || '—';
}

function showStatus(s) {
  $('channel').textContent = s.channel || '—';
  $('bitrate').textContent = s.bitrate ? (s.bitrate / 1000) + ' kbit/s' : '—';
  $('backend').textContent = backendLabel(s);
  $('backend').title = s.businfo || '';
  $('rx').textContent = String(s.rx || 0);
  $('tx').textContent = String(s.tx_ok !== undefined ? s.tx_ok : (s.tx || 0));
  var fail = s.tx_fail || 0;
  $('txfail').textContent = String(fail);
  $('txfail').className = 'v count-fail' + (fail ? '' : ' zero');
  $('txqueued').textContent = String(s.tx_queued || 0);
  lastStatus = s;
  $('ver').textContent = 'v' + (s.version || '?') + ' · 桥接进程';
  $('err').textContent = s.error ? ('⚠ ' + s.error) : '';
  $('err').title = s.businfo || '';
}

function setConn(ok, errText) {
  var dot = $('dot');
  dot.className = ok ? 'on' : 'off';
  $('conn').textContent = ok ? '已连接桥接进程' : '未连接';
  if (!ok) {
    $('backend').textContent = '—';
    if (errText) { $('err').textContent = '⚠ ' + errText; }
  }
}

/* 桥接状态快照（发送后用来核对"到底发出去没有"） */
var lastStatus = null;

function exportCsv() {
  if (!rx.csv.length) { log('没有可导出的帧', 'w'); return; }
  var lines = ['time,dir,id,dlc,data,decoded'];
  for (var i = 0; i < rx.csv.length; i++) {
    var f = rx.csv[i], d = f.data || [];
    var dec = decodeFrame(f.id, d).replace(/"/g, '""');
    lines.push([clockOf(f.t), f.dir, '0x' + hx(f.id, 3), d.length,
                '"' + dataHex(d) + '"', '"' + dec + '"'].join(','));
  }
  var blob = new Blob(['\ufeff' + lines.join('\r\n') + '\r\n'],
                      { type: 'text/csv;charset=utf-8' });
  var a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = 'can_frames_' + new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19) + '.csv';
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  setTimeout(function () { URL.revokeObjectURL(a.href); }, 3000);
  log('已导出 ' + rx.csv.length + ' 帧到 CSV');
}

/* ================================================================ 定时器台账 */
var timers = {};        // token → 说明
var _staticPresetToken = null;

function resetStaticButton() {
  staticOn = false;
  _staticPresetToken = null;
  $('btnStaticToggle').textContent = '▶ 静态车况（周期发）';
  $('btnStaticToggle').className = 'primary';
}

function addTimer(token, label) {
  timers[token] = label;
  renderTimers();
}
function delTimer(token) {
  delete timers[token];
  renderTimers();
}
function renderTimers() {
  var box = $('activeTimers');
  box.innerHTML = '';
  var keys = Object.keys(timers);
  if (!keys.length) { return; }
  var span = document.createElement('span');
  span.className = 'hint';
  span.textContent = '正在周期发送：';
  box.appendChild(span);
  keys.forEach(function (t) {
    var b = document.createElement('button');
    b.className = 'tiny on';
    b.textContent = timers[t] + ' ■';
    b.title = '点击停止这一组';
    b.onclick = function () {
      api.postOk('/api/stop', { token: t }).then(function () {
        var label = timers[t];
        delTimer(t);
        presetTokens = presetTokens.filter(function (x) { return x !== t; });
        if (String(label).indexOf('时间线') >= 0) { /* 时间线不走定时器 */ }
        if (_staticPresetToken === t) { resetStaticButton(); }
        log('已停 ' + label);
      }).catch(function (e) { log('停止失败：' + e.message, 'e'); });
    };
    box.appendChild(b);
  });
}

/* 发一组帧：frames = [{id,data,count,period_ms,periodic}]
 *
 * 日志策略（问题 3 的关键排查手段）：把**请求体、HTTP 状态码、响应体**
 * 都写进日志区，让"到底发出去没有"一眼可见；随后 600 ms 再回读一次
 * /api/status，用 TX 成功/TX 失败 的增量确认帧真的写进总线了。
 */
function sendFrames(frames, note) {
  var label = note || ('发 ' + frames.length + ' 条');
  var brief = frames.map(function (f) {
    return '0x' + hx(f.id, 3) + ' [' + toHexStr(f.data) + ']' +
           (f.periodic ? ' 周期' + (f.period_ms || 100) + 'ms'
                       : ' ×' + (f.count === undefined ? 1 : f.count));
  }).join(' + ');
  var reqId = ++txSeq;
  log('→ TX 请求 #' + reqId + '：' + brief, 'a');
  return api.post('/api/send', { frames: frames }).then(function (res) {
    var j = res.body || {};
    if (res.ok && j.ok) {
      var n = j.repeats || 0;
      log('✓ TX #' + reqId + ' HTTP ' + res.status + ' 已登记 ' + frames.length + ' 条' +
          (n ? '（共 ' + n + ' 次发送）' : '') +
          (j.tx_before !== undefined ? ' · 发送前 TX 成功=' + j.tx_before : ''), 'a');
      (j.timers || []).forEach(function (t) {
        addTimer(t.token, '0x' + hx(t.id, 3) + ' 周期');
      });
      (j.rejected || []).forEach(function (r) {
        log('✗ TX #' + reqId + ' 有一条被拒：' + r.error, 'e');
      });
      verifySend(reqId, j.tx_before, frames, j.periodic_any);
      return j;
    }
    log('✗ TX #' + reqId + ' HTTP ' + res.status + ' 被拒：' +
        (j.error || res.text || '（无响应体）'), 'e');
    return null;
  }).catch(function (e) {
    log('✗ TX #' + reqId + ' 请求失败（桥接进程没响应？）：' + e.message, 'e');
    return null;
  });
}

/* 600 ms 后回读状态：TX 成功数涨了 = 帧真的写进总线了 */
function verifySend(reqId, txBefore, frames, anyPeriodic) {
  var want = frames.reduce(function (a, f) {
    return a + (f.periodic ? (f.count > 0 ? f.count : 1) : (f.count === undefined ? 1 : f.count));
  }, 0);
  setTimeout(function () {
    api.get('/api/status?t=' + Date.now(), 4000).then(function (s) {
      var nowOk = (s.tx_ok !== undefined ? s.tx_ok : s.tx) || 0;
      var nowFail = s.tx_fail || 0;
      var delta = (txBefore === undefined || txBefore === null) ? null : (nowOk - txBefore);
      if (s.error) {
        log('✗ TX #' + reqId + ' 总线写入报错：' + s.error, 'e');
      } else if (delta !== null && delta > 0) {
        log('✓ TX #' + reqId + ' 总线确认：TX 成功 +' + delta +
            '（累计 ' + nowOk + (nowFail ? '，累计失败 ' + nowFail : '') + '）');
      } else if (s.tx_queued > 0 || anyPeriodic) {
        log('· TX #' + reqId + ' 还在排队（待发 ' + (s.tx_queued || 0) + ' 帧），下一拍就发');
      } else {
        log('⚠ TX #' + reqId + ' 已登记但 TX 成功数没涨（累计 ' + nowOk +
            '，失败 ' + nowFail + '）；若失败>0 看上面的总线错误原文', 'w');
      }
    }).catch(function (e) {
      log('⚠ TX #' + reqId + ' 回读状态失败：' + e.message, 'w');
    });
  }, 600);
}
var txSeq = 0;

/* 停掉某组：先把整块停掉再重发——本项目里同一时刻只有一组"预设"在跑（见 stopPresetGroup） */
var presetTokens = [];      // 上一次预设留下的定时器

function stopPresetGroup() {
  if (!presetTokens.length) { return Promise.resolve(); }
  var ps = presetTokens.map(function (t) {
    return api.postOk('/api/stop', { token: t }).then(function () { delTimer(t); });
  });
  presetTokens = [];
  return Promise.all(ps);
}

function rememberPreset(j) {
  presetTokens = (j && j.timers ? j.timers.map(function (t) { return t.token; }) : []);
}

/* ================================================================ 预设 1：开车时间线 */
var timelineOn = false;

function toggleTimeline() {
  if (timelineOn) {
    api.postOk('/api/timeline', { timeline: false }).then(function () {
      timelineOn = false;
      $('btnTimeline').textContent = '▶ 开车时间线（30 s 循环）';
      $('btnTimeline').className = 'primary';
      log('开车时间线：停');
    }).catch(function (e) { log('停止时间线失败：' + e.message, 'e'); });
    return;
  }
  stopPresetGroup().then(function () {
    return api.postOk('/api/timeline', { timeline: true, period_ms: 100 });
  }).then(function (j) {
    timelineOn = true;
    $('btnTimeline').textContent = '■ 停止开车时间线';
    $('btnTimeline').className = 'danger';
    log('开车时间线：启动（怠速→加速到 66→左转→刹车→安全带未系→左前门开→倒车→停车，' +
        Math.round((j.total_ms || 30000) / 1000) + ' 秒一轮，循环）', 'a');
  }).catch(function (e) { log('启动时间线失败：' + e.message, 'e'); });
}

/* ================================================================ 预设 2：静态车况 */
var staticOn = false;

function readStatic() {
  var o = {
    speed: $('sSpeed').value, speedInvalid: $('sSpeedInvalid').checked,
    gear: $('sGear').value, rpm: $('sRpm').value,
    left: $('sLeft').checked, right: $('sRight').checked, brake: $('sBrake').checked,
    reverse: $('sReverse').checked, seatbelt: $('sSeatbelt').checked,
    handbrake: $('sHandbrake').checked, hazard: $('sHazard').checked,
    locked: $('sLocked').checked,
    ac: $('sAc').checked, recirc: $('sRecirc').checked,
    acTemp: $('sAcTemp').value, outTemp: $('sOutTemp').value
  };
  for (var i = 0; i < 6; i++) {
    o['door' + i] = $('sDoor' + i).checked;
    o['light' + i] = $('sLight' + i).checked;
  }
  return o;
}

function staticFrames(o) {
  return [
    { id: 0x100, data: build0x100(o), periodic: true, count: 0 },
    { id: 0x101, data: build0x101(o), periodic: true, count: 0 },
    { id: 0x102, data: build0x102(o), periodic: true, count: 0 }
  ];
}

function refreshStaticPreview() {
  var o = readStatic();
  var per = clamp($('sPeriod').value, 20, 5000);
  var f = staticFrames(o);
  $('staticPreview').textContent =
    '0x100 ' + dataHex(f[0].data) + '  |  ' + decode0x100(f[0].data) + '\n' +
    '0x101 ' + dataHex(f[1].data) + '  |  ' + decode0x101(f[1].data) + '\n' +
    '0x102 ' + dataHex(f[2].data) + '  |  ' + decode0x102(f[2].data) + '\n' +
    '周期 ' + per + ' ms';
}

function toggleStatic() {
  if (staticOn) {
    stopPresetGroup().then(function () {
      resetStaticButton();
      log('静态车况：停');
    });
    return;
  }
  var o = readStatic();
  var per = clamp($('sPeriod').value, 20, 5000);
  var frames = staticFrames(o).map(function (f) {
    f.period_ms = per; f.count = 0; return f;
  });
  stopTimelineQuiet().then(function () { return stopPresetGroup(); }).then(function () {
    return sendFrames(frames, '静态车况（' + per + ' ms）');
  }).then(function (j) {
    if (!j) { return; }
    staticOn = true;
    rememberPreset(j);
    _staticPresetToken = j.timers && j.timers.length ? j.timers[0].token : null;
    $('btnStaticToggle').textContent = '■ 停止静态车况';
    $('btnStaticToggle').className = 'danger';
  });
}

/* 静态车况启动前，把开车时间线先关掉（两者都发 0x100 会互相打架） */
function stopTimelineQuiet() {
  if (!timelineOn) { return Promise.resolve(); }
  return api.postOk('/api/timeline', { timeline: false }).then(function () {
    timelineOn = false;
    $('btnTimeline').textContent = '▶ 开车时间线（30 s 循环）';
    $('btnTimeline').className = 'primary';
  });
}

/* ================================================================ 预设 3：工作模式 0x204 */
function sendMode(m) {
  var names = ['经济', '标准', '运动', '雪地'];
  sendFrames([{ id: 0x204, data: [m, 0, 0, 0, 0, 0, 0, 0], count: 3, period_ms: 50 }],
             '工作模式 → ' + names[m]).then(function () {
    $('modeState').textContent = '模式 ' + names[m] + '（已发 0x204 ×3）';
    $('modeState').className = 'badge live';
  });
}

/* ================================================================ 预设 4：里程油耗组 */
function mileageFrames() {
  return [
    { id: 0x103, data: build0x103({
        total: $('mTotal').value, trip: $('mTrip').value,
        fuel: $('mFuel').value, range: $('mRange').value }), periodic: true, count: 0 },
    { id: 0x104, data: build0x104({
        inst: $('mInst').value, avg: $('mAvg').value, mode: $('mMode').value,
        dtc: 0, faultCount: 0 }), periodic: true, count: 0 },
    { id: 0x105, data: build0x105({
        p0: $('mP0').value, p1: $('mP1').value, p2: $('mP2').value, p3: $('mP3').value,
        t0: $('mT0').value, t1: $('mT1').value, t2: $('mT2').value, t3: $('mT3').value }),
      periodic: true, count: 0 }
  ];
}

/* 一键 = 三条样例各发 1 次（点一下不会灌总线） */
function sendMileageGroup() {
  stopPresetGroup().then(function () {
    var frames = mileageFrames().map(function (f) {
      f.periodic = false; f.count = 1; delete f.period_ms;
      return f;
    });
    return sendFrames(frames, '里程油耗组（每条 1 次）');
  }).then(function (j) {
    if (!j) { return; }
    var fr = mileageFrames();
    log(' 0x103 ' + dataHex(fr[0].data) + ' → ' + decode0x103(fr[0].data));
    log(' 0x104 ' + dataHex(fr[1].data) + ' → ' + decode0x104(fr[1].data));
    log(' 0x105 ' + dataHex(fr[2].data) + ' → ' + decode0x105(fr[2].data));
  });
}

/* ▶ 按上面数值周期发送：让板子状态页持续有数（明确是"周期"动作） */
function sendMileageLoop() {
  var per = clamp($('mPeriod').value, 50, 5000);
  var frames = mileageFrames().map(function (f) { f.period_ms = per; return f; });
  stopPresetGroup().then(function () {
    return sendFrames(frames, '里程油耗组循环（' + per + ' ms）');
  }).then(function (j) {
    if (!j) { return; }
    rememberPreset(j);
    log(' 0x103 ' + dataHex(frames[0].data) + ' → ' + decode0x103(frames[0].data));
    log(' 0x104 ' + dataHex(frames[1].data) + ' → ' + decode0x104(frames[1].data));
    log(' 0x105 ' + dataHex(frames[2].data) + ' → ' + decode0x105(frames[2].data));
  });
}

/* ================================================================ 预设 5：故障演示 */
/* 一键 = 两条告警帧各发 1 次；循环是另一个按钮 */
function sendFaultDemo() {
  var d104 = build0x104({ inst: 98, avg: 85, mode: 1, warn0: true, warn1: true,
                          dtc: 0x0301, faultCount: 1 });
  var d105 = build0x105({ p0: 48, p1: 44, p2: 80, p3: 80, t0: 20, t1: 21, t2: 20, t3: 22 });
  stopPresetGroup().then(function () {
    return sendFrames([{ id: 0x104, data: d104, count: 1 },
                       { id: 0x105, data: d105, count: 1 }],
                      '故障演示（发动机故障 + 胎压报警 + P0301）');
  }).then(function (j) {
    if (!j) { return; }
    log(' 0x104 ' + dataHex(d104) + ' → ' + decode0x104(d104), 'w');
    log(' 0x105 ' + dataHex(d105) + ' → ' + decode0x105(d105), 'w');
  });
}

function sendFaultLoop() {
  var d104 = build0x104({ inst: 98, avg: 85, mode: 1, warn0: true, warn1: true,
                          dtc: 0x0301, faultCount: 1 });
  var d105 = build0x105({ p0: 48, p1: 44, p2: 80, p3: 80, t0: 20, t1: 21, t2: 20, t3: 22 });
  var frames = [
    { id: 0x104, data: d104, periodic: true, count: 0, period_ms: 500 },
    { id: 0x105, data: d105, periodic: true, count: 0, period_ms: 500 }
  ];
  stopPresetGroup().then(function () {
    return sendFrames(frames, '故障演示循环（500 ms）');
  }).then(function (j) {
    if (!j) { return; }
    rememberPreset(j);
    log(' 0x104 ' + dataHex(d104) + ' → ' + decode0x104(d104), 'w');
    log(' 0x105 ' + dataHex(d105) + ' → ' + decode0x105(d105), 'w');
  });
}

/* ================================================================ 可编辑发送表 */
var rowSeq = 0;
var rowState = {};      // rowId → {token, running}

function addEditRow(id, data, period, count) {
  var rid = 'r' + (++rowSeq);
  var tr = document.createElement('tr');
  tr.id = rid;
  rowState[rid] = { token: null, running: false };

  function cell(child) { var td = document.createElement('td'); if (child) { td.appendChild(child); } return td; }

  var inpId = document.createElement('input');
  inpId.type = 'text'; inpId.className = 'b-id';
  inpId.value = (id === undefined ? '0x100' : '0x' + hx(id, 3));
  tr.appendChild(cell(inpId));

  var byteInputs = [];
  for (var i = 0; i < 8; i++) {
    var bi = document.createElement('input');
    bi.type = 'text'; bi.className = 'b-byte';
    bi.value = byteHex(data && data[i] !== undefined ? data[i] : 0);
    byteInputs.push(bi);
    tr.appendChild(cell(bi));
  }

  var inPer = document.createElement('input');
  inPer.type = 'number'; inPer.className = 'b-per'; inPer.min = '10'; inPer.max = '600000';
  inPer.value = String(period === undefined ? 100 : period);
  tr.appendChild(cell(inPer));

  var inCnt = document.createElement('input');
  inCnt.type = 'number'; inCnt.className = 'b-cnt'; inCnt.min = '0'; inCnt.max = String(MAX_BURST);
  inCnt.value = String(count === undefined ? 1 : count);   // 默认 1 帧，不再默认灌一串
  inCnt.title = '「发」按这个次数发（默认 1，单次最多 ' + MAX_BURST + ' 帧）；0 = 配合 ▶ 表示一直发';
  tr.appendChild(cell(inCnt));

  var en = document.createElement('input');
  en.type = 'checkbox'; en.checked = true; en.title = '使能（「启动全部」时只发勾上的行）';
  var tdEn = cell(en); tdEn.className = 'mid';
  tr.appendChild(tdEn);

  var tdBtn = document.createElement('td');
  tdBtn.className = 'ops';
  var bSend = document.createElement('button');
  bSend.className = 'tiny'; bSend.textContent = '发';
  bSend.title = '按「次数」发这一帧（默认 1 帧，最多 ' + MAX_BURST + ' 帧）；HTTP 结果与总线确认写在日志区';
  var bStart = document.createElement('button');
  bStart.className = 'tiny primary'; bStart.textContent = '▶';
  bStart.title = '按周期一直发（让板子持续收到）';
  var bStop = document.createElement('button');
  bStop.className = 'tiny'; bStop.textContent = '■'; bStop.title = '停止本行周期发送';
  var bDel = document.createElement('button');
  bDel.className = 'tiny danger'; bDel.textContent = '✕'; bDel.title = '删除本行';
  [bSend, bStart, bStop, bDel].forEach(function (b) { tdBtn.appendChild(b); });
  tr.appendChild(tdBtn);

  var tdDec = document.createElement('td');
  tdDec.className = 'mid';
  var bDec = document.createElement('button');
  bDec.className = 'tiny'; bDec.textContent = '解码'; bDec.title = '按当前 ID/数据解码';
  tdDec.appendChild(bDec);
  tr.appendChild(tdDec);

  $('txTbody').appendChild(tr);

  function readId() {
    var s = inpId.value.trim();
    if (/^0x/i.test(s)) { return parseInt(s, 16); }
    if (/[a-f]/i.test(s)) { return parseInt(s, 16); }
    return parseInt(s, 10);
  }
  function readData() {
    return byteInputs.map(function (b) {
      var s = b.value.trim();
      var v = /^0x/i.test(s) ? parseInt(s, 16) : (/[a-f]/i.test(s) ? parseInt(s, 16) : parseInt(s, 10));
      return (isNaN(v) ? 0 : (v & 0xFF));
    });
  }
  function guard() {
    var id = readId();
    if (isNaN(id) || id < 0 || id > 0x7FF) {
      log('ID 不合法（要 0x000~0x7FF）：' + inpId.value, 'e');
      return null;
    }
    return { id: id, data: readData() };
  }

  bSend.onclick = function () {
    var g = guard(); if (!g) { return; }
    var c = clamp(inCnt.value, 1, MAX_BURST);      // 默认 1 帧，单次最多 MAX_BURST
    var per = clamp(inPer.value, 0, 600000);
    sendFrames([{ id: g.id, data: g.data, count: c, period_ms: c > 1 ? per : 0 }],
               '发 0x' + hx(g.id, 3) + ' ×' + c);
  };
  bStart.onclick = function () {
    var g = guard(); if (!g) { return; }
    var per = clamp(inPer.value, 10, 600000);
    var cnt = clamp(inCnt.value, 0, MAX_BURST);   // 0 = 一直发（周期）
    api.postOk('/api/timer', { id: g.id, data: g.data, period_ms: per, count: cnt })
      .then(function (j) {
        rowState[rid].token = j.token;
        rowState[rid].running = true;
        tr.className = 'running';
        addTimer(j.token, '0x' + hx(g.id, 3) + ' 行');
        log('行启动 0x' + hx(g.id, 3) + ' 周期 ' + per + ' ms' + (cnt ? ' 共 ' + cnt + ' 次' : ' 一直发'), 'a');
      }).catch(function (e) { log('启动失败：' + e.message, 'e'); });
  };
  bStop.onclick = function () {
    var st = rowState[rid];
    var done = function () {
      st.running = false; st.token = null; tr.className = '';
      log('行停止 0x' + hx(readId() || 0, 3));
    };
    if (st.token) {
      api.postOk('/api/stop', { token: st.token }).then(function () { delTimer(st.token); done(); })
        .catch(function (e) { log('停止失败：' + e.message, 'e'); });
    } else { done(); }
  };
  bDel.onclick = function () {
    if (rowState[rid].token) { bStop.onclick(); }
    delete rowState[rid];
    if (tr.parentNode) { tr.parentNode.removeChild(tr); }
  };
  bDec.onclick = function () {
    var g = guard(); if (!g) { return; }
    log('解码 0x' + hx(g.id, 3) + ' ' + dataHex(g.data) + ' → ' + decodeFrame(g.id, g.data));
    $('log').scrollTop = $('log').scrollHeight;
  };

  [inpId].concat(byteInputs).forEach(function (el) {
    el.addEventListener('change', function () { /* 留口子：改完不自动发 */ });
  });
  return rid;
}

function fillRowFromStatic() {
  var o = readStatic();
  while ($('txTbody').firstChild) { $('txTbody').removeChild($('txTbody').firstChild); }
  rowState = {};
  var per = clamp($('sPeriod').value, 20, 5000);
  addEditRow(0x100, build0x100(o), per, 1);
  addEditRow(0x101, build0x101(o), per, 1);
  addEditRow(0x102, build0x102(o), per, 1);
  log('已用静态车况填入发送表（点每行的 ▶ 按周期发）', 'a');
}

function startAllRows() {
  var rows = $('txTbody').childNodes;
  for (var i = 0; i < rows.length; i++) {
    var tr = rows[i];
    if (!tr.querySelector) { continue; }
    var en = tr.querySelector('input[type=checkbox]');
    if (!en || !en.checked) { continue; }
    var btns = tr.querySelectorAll('button');
    if (btns.length >= 2) { btns[1].click(); }      // btns[1] = ▶
  }
}
function stopAllRows() {
  Object.keys(rowState).forEach(function (rid) {
    var st = rowState[rid];
    if (st.token) {
      api.postOk('/api/stop', { token: st.token }).then(function () { delTimer(st.token); });
      st.token = null; st.running = false;
      var tr = $(rid); if (tr) { tr.className = ''; }
    }
  });
  api.postOk('/api/stop', {}).then(function () {
    presetTokens = []; timelineOn = false;
    timers = {};
    _staticPresetToken = null;
    $('btnTimeline').textContent = '▶ 开车时间线（30 s 循环）';
    $('btnTimeline').className = 'primary';
    $('btnStaticToggle').textContent = '▶ 静态车况（周期发）';
    $('btnStaticToggle').className = 'primary';
    renderTimers();
    log('全部停发（周期定时器已清空）', 'w');
  }).catch(function (e) { log('停止失败：' + e.message, 'e'); });
}

/* ================================================================ 绑定 */
function bind() {
  rx.tbody = $('rxTbody');

  $('btnClear').onclick = function () {
    api.postOk('/api/clear', {}).then(function () {
      rx.tbody.innerHTML = '';
      rx.rows = 0; rx.csv = []; rx.queue = [];
      $('rows').textContent = '0';
      $('rx').textContent = '0'; $('tx').textContent = '0';
      $('txfail').textContent = '0'; $('txfail').className = 'v count-fail zero';
      $('txqueued').textContent = '0';
      log('已清空接收区与计数');
    }).catch(function (e) { log('清空失败：' + e.message, 'e'); });
  };

  $('btnPause').onclick = function () { setPaused(!rx.paused); };

  $('btnCsv').onclick = exportCsv;
  $('chkDown').onchange = $('chkUp').onchange = function () { /* 只影响后续行 */ };
  $('chkRecent').onchange = function () {
    var cap = rowCap();
    while (rx.rows > cap && rx.tbody.firstChild) {
      rx.tbody.removeChild(rx.tbody.firstChild);
      rx.rows--;
    }
    $('rows').textContent = String(rx.rows);
    $('rowsCap').textContent = '/' + cap;
    log('接收区显示上限：' + cap + ' 行');
  };

  $('btnTimeline').onclick = toggleTimeline;
  $('btnStaticToggle').onclick = toggleStatic;
  $('btnMileage').onclick = sendMileageGroup;
  $('btnMileageLoop').onclick = sendMileageLoop;
  $('btnFault').onclick = sendFaultDemo;
  $('btnFaultLoop').onclick = sendFaultLoop;
  $('btnStopAll').onclick = stopAllRows;
  $('btnMileageEdit').onclick = sendMileageLoop;

  Array.prototype.forEach.call(document.querySelectorAll('button[data-mode]'), function (b) {
    b.onclick = function () { sendMode(parseInt(b.getAttribute('data-mode'), 10)); };
  });

  ['sSpeed', 'sGear', 'sRpm', 'sLeft', 'sRight', 'sBrake', 'sReverse', 'sSeatbelt',
   'sHandbrake', 'sHazard', 'sLocked', 'sAc', 'sRecirc', 'sAcTemp', 'sOutTemp',
   'sSpeedInvalid', 'sPeriod',
   'sDoor0', 'sDoor1', 'sDoor2', 'sDoor3', 'sDoor4', 'sDoor5',
   'sLight0', 'sLight1', 'sLight2', 'sLight3', 'sLight4', 'sLight5'].forEach(function (id) {
    var el = $(id);
    if (el) { el.addEventListener('change', refreshStaticPreview); el.addEventListener('input', refreshStaticPreview); }
  });

  $('btnAddRow').onclick = function () { addEditRow(0x100, [0, 0, 0, 0, 0, 0, 0, 0], 100, 1); };
  $('btnEchoStatic').onclick = fillRowFromStatic;
  $('btnStartAll').onclick = startAllRows;
  $('btnStopAllRows').onclick = stopAllRows;

  refreshStaticPreview();
  $('rowsCap').textContent = '/' + rowCap();
  // 默认先摆三行常规帧，方便直接点 ▶（次数默认 1，不会一点就灌总线）
  addEditRow(0x100, build0x100(readStatic()), 100, 1);
  addEditRow(0x101, build0x101(readStatic()), 100, 1);
  addEditRow(0x102, build0x102(readStatic()), 100, 1);
}

/* ================================================================ 启动自检 */
function selfTest() {
  /* 无硬件也能验证解码：把协议表里的样例逐条过一遍，结果打到日志。
     与 README 的"自测"一节对应。 */
  var cases = [
    ['0x100', 0x100, [66, 0x01, 3, 8, 0, 0, 0, 0], '车速 66 km/h · 档位 D · 转速 800 rpm · 左转'],
    ['0x100', 0x100, [0xFF, 0x08, 1, 0, 0, 0, 0, 0], '车速 无效 · 档位 R · 转速 0 rpm · 倒车'],
    ['0x100', 0x100, [0, 0x20, 0, 0, 0, 0, 0, 0], '车速 0 km/h · 档位 P · 转速 0 rpm · 手刹'],
    ['0x101', 0x101, [0x01, 0, 0, 0, 0, 0, 0, 0], '开门：左前门 · 未上锁'],
    ['0x101', 0x101, [0x20, 1, 0, 0, 0, 0, 0, 0], '开门：引擎盖 · 全车已锁'],
    ['0x102', 0x102, [0x03, 0x03, 24, 60, 0, 0, 0, 0], 'AC开 24℃ 内循环 · 车外 20℃'],
    ['0x103', 0x103, [0x10, 0x27, 0x00, 0x50, 0x00, 50, 0x58, 0x02],
      '总里程 1000.0 km · 小计 8.0 km · 油量 50% · 续航 600 km'],
    ['0x103', 0x103, [0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0],
      '总里程 无效 · 小计 0.0 km · 油量 无效 · 续航 0 km'],
    ['0x104', 0x104, [0x4B, 0x41, 1, 0, 0, 0, 0, 0],
      '瞬时油耗 7.5 L/100km · 平均 6.5 L/100km · 模式 标准 · 告警[-] · 故障 0 个'],
    ['0x104', 0x104, [0x62, 0x55, 1, 0x03, 1, 0x01, 0x03, 0],
      '瞬时油耗 9.8 L/100km · 平均 8.5 L/100km · 模式 标准 · 告警[发动机/胎压] · 故障 1 个 · 故障码 P0301'],
    ['0x105', 0x105, [0x50, 0x50, 0x4F, 0x51, 0x3C, 0x3D, 0x3C, 0x3E],
      '胎压 左前 400 kPa / 右前 400 kPa / 左后 395 kPa / 右后 405 kPa · 胎温 左前 20℃ / 右前 21℃ / 左后 20℃ / 右后 22℃'],

    ['0x201', 0x201, [0, 16, 0, 0, 0, 0, 0, 0], '空调 关 · 温度 16℃ · 风量 0 · 外循环'],
    ['0x201', 0x201, [1, 24, 3, 1, 0, 0, 0, 0], '空调 开 · 温度 24℃ · 风量 3 · 内循环'],
    ['0x202', 0x202, [1, 1, 100, 0, 0, 0, 0, 0], '车窗 左前 升 · 目标 100%'],
    ['0x202', 0x202, [0, 2, 0, 0, 0, 0, 0, 0], '车窗 全部 降 · 目标 0%'],
    ['0x202', 0x202, [4, 0, 55, 0, 0, 0, 0, 0], '车窗 右后 停 · 目标 55%'],
    ['0x203', 0x203, [0x01, 0x02, 0, 0, 0, 0, 0, 0], '灯光 开[近光] 关[远光]'],
    ['0x203', 0x203, [0x15, 0x0A, 0, 0, 0, 0, 0, 0], '灯光 开[近光/示宽/后雾] 关[远光/前雾]'],
    ['0x204', 0x204, [2, 0, 0, 0, 0, 0, 0, 0], '工作模式 → 运动'],
    ['0x104', 0x104, [0x32, 0x2D, 3, 0x20, 0, 0, 0, 0],
      '瞬时油耗 5.0 L/100km · 平均 4.5 L/100km · 模式 雪地 · 告警[蓄电池] · 故障 0 个'],
    ['0x104', 0x104, [0x32, 0x2D, 3, 0x10, 0, 0, 0, 0],
      '瞬时油耗 5.0 L/100km · 平均 4.5 L/100km · 模式 雪地 · 告警[ABS] · 故障 0 个'],
    // 板子文档里的原始用例（docs/车辆状态与模式_20261001.md §5）
    ['0x103', 0x103, [0xC0, 0x37, 0x12, 0x7B, 0x00, 68, 0x20, 0x02],
      '总里程 119392.0 km · 小计 12.3 km · 油量 68% · 续航 544 km'],
    ['0x104', 0x104, [0x41, 0x4A, 2, 0x03, 1, 0x01, 0x03, 0],
      '瞬时油耗 6.5 L/100km · 平均 7.4 L/100km · 模式 运动 · 告警[发动机/胎压] · 故障 1 个 · 故障码 P0301'],
    ['0x105', 0x105, [46, 47, 43, 46, 75, 76, 74, 75],
      '胎压 左前 230 kPa / 右前 235 kPa / 左后 215 kPa / 右后 230 kPa · 胎温 左前 35℃ / 右前 36℃ / 左后 34℃ / 右后 35℃'],
    ['0x105', 0x105, [0, 0xFF, 0x50, 0x50, 0xFF, 0x3C, 0x3C, 0x3C],
      '胎压 左前 无效 / 右前 无效 / 左后 400 kPa / 右后 400 kPa · 胎温 左前 无效 / 右前 20℃ / 左后 20℃ / 右后 20℃'],
    ['0x999', 0x999, [1, 2, 3, 0, 0, 0, 0, 0], '（未定义 ID）01 02 03 00 00 00 00 00'],
    ['0x100', 0x100, [42, 0x40, 3, 8, 0, 0, 0, 0], '车速 42 km/h · 档位 D · 转速 800 rpm · 双闪']
  ];
  var bad = 0;
  log('—— 前端解码自检（' + cases.length + ' 条协议样例）——', 'a');
  cases.forEach(function (c) {
    var got = decodeFrame(c[1], c[2]);
    if (got === c[3]) {
      log('OK   ' + c[0] + ' ' + dataHex(c[2]) + ' → ' + got);
    } else {
      bad++;
      log('FAIL ' + c[0] + ' ' + dataHex(c[2]) + '\n     期望: ' + c[3] + '\n     实得: ' + got, 'e');
    }
  });
  log(bad ? ('解码自检：' + bad + ' 条不符！') : ('解码自检：' + cases.length + ' 条全部通过'), bad ? 'e' : 'a');

  /* 编帧自检：编出来的字节要能解回原值 */
  var enc = [
    ['0x100 左转+D+66+800rpm', build0x100({ speed: 66, gear: 3, rpm: 8, left: true }),
      [66, 0x01, 3, 8, 0, 0, 0, 0]],
    ['0x101 左前门开+未锁', build0x101({ door0: true, locked: false }), [0x01, 0, 0, 0, 0, 0, 0, 0]],
    ['0x102 近光+AC+24℃+车外20℃', build0x102({ light0: true, ac: true, acTemp: 24, outTemp: 20 }),
      [0x01, 1, 24, 60, 0, 0, 0, 0]],
    ['0x103 10000km/800km小计/50%/600km', build0x103({ total: 100000, trip: 800, fuel: 50, range: 600 }),
      [0xA0, 0x86, 0x01, 0x20, 0x03, 50, 0x58, 0x02]],
    ['0x104 P0301+发动机胎压告警', build0x104({ inst: 98, avg: 85, mode: 1, warn0: true, warn1: true, dtc: 0x0301 }),
      [98, 85, 1, 0x03, 1, 0x01, 0x03, 0]],
    ['0x105 400kPa/20℃', build0x105({ p0: 80, p1: 80, p2: 80, p3: 80, t0: 20, t1: 20, t2: 20, t3: 20 }),
      [80, 80, 80, 80, 60, 60, 60, 60]],
    ['0x100 车速无效', build0x100({ speedInvalid: true, gear: 3 }), [0xFF, 0, 3, 0, 0, 0, 0, 0]],
    ['0x102 车外 −20℃', build0x102({ outTemp: -20 }), [0, 0, 16, 20, 0, 0, 0, 0]],
    ['0x103 24 位小端 0x123456', build0x103({ total: 0x123456, trip: 0x0102, fuel: 0xFF, range: 0x0304 }),
      [0x56, 0x34, 0x12, 0x02, 0x01, 0xFF, 0x04, 0x03]]
  ];
  enc.forEach(function (c) {
    var ok = c[1].length === 8 && c[1].every(function (v, i) { return (v & 0xFF) === c[2][i]; });
    if (ok) { log('OK   编帧 ' + c[0] + ' → ' + dataHex(c[1])); }
    else { bad++; log('FAIL 编帧 ' + c[0] + ' 得 ' + dataHex(c[1]) + ' 期望 ' + dataHex(c[2]), 'e'); }
  });
  log('—— 自检结束：' + (bad ? bad + ' 项失败' : '全部通过') + ' ——', bad ? 'e' : 'a');
  return bad === 0;
}

/* ================================================================ 主流程 */
function main() {
  bind();
  selfTest();
  setInterval(poll, POLL_MS);
  poll();
  log('提示：先在下面点「一键预设」，接收区会看到自己发出去的帧（sim 回显）与板子上行帧。');
}

if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', main);
} else {
  main();
}
