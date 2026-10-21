#!/usr/bin/env node
/* ============================================================================
 * frontend_selftest.js —— 在 Node 里跑 app.js 的「解码/编帧自检」，不需要浏览器
 *
 * 为什么：前端解码逻辑必须与《CAN 接口定义》逐字节一致，而在 CI/命令行里
 * 没法点网页。这个脚本把 app.js 里的纯逻辑函数（无 DOM 依赖的那一截）抽出来
 * 在 vm 里执行，再调用 selfTest()，要求返回 true。
 *
 *   用法：  node frontend_selftest.js       （在 tools/can_web/ 下）
 *   退出码：0 = 全部通过，1 = 有失败
 * ========================================================================= */
'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const APP = path.join(__dirname, 'app.js');
const src = fs.readFileSync(APP, 'utf8');

/* 取出「常量 → 解码 → 编帧」这一段；它上不碰 DOM、下不碰 fetch/Node。
   末端以「后端 API」注释块为界。 */
const BEGIN = '/* ------------------------------------------------------------------ 常量 */';
const END = '/* ================================================================ 后端 API */';
const i = src.indexOf(BEGIN);
const j = src.indexOf(END);
if (i < 0 || j < 0 || j <= i) {
  console.error('无法在 app.js 里定位逻辑段落（常量/BACKEND 标记变了？）');
  process.exit(1);
}
let logic = src.slice(i, j);

/* selfTest 里只用到 log()，这里换成收集器。 */
const FAILS = [];
const sandbox = {
  console: { log: (...a) => console.log(...a) },
  log: (msg, cls) => { console.log((cls ? '[' + cls + '] ' : '') + msg); if (cls === 'e') { FAILS.push(msg); } },
  result: null
};
vm.createContext(sandbox);

/* selfTest 在 app.js 的后半段（纯逻辑，只依赖 log），一并取出来。 */
const sBegin = src.indexOf('function selfTest() {');
const sEnd = src.indexOf('/* ================================================================ 主流程 */');
if (sBegin < 0 || sEnd < 0) {
  console.error('无法在 app.js 里定位 selfTest()');
  process.exit(1);
}
const selfTestSrc = src.slice(sBegin, sEnd);

try {
  vm.runInContext(logic + '\n' + selfTestSrc + '\nresult = { selfTest: selfTest, decodeFrame: decodeFrame };',
                  sandbox, { filename: APP });
} catch (e) {
  console.error('执行前端逻辑出错：', e && e.stack || e);
  process.exit(1);
}

console.log('');
const ok = sandbox.result.selfTest();

/* 额外：逐字节核对 README/协议表里的 6 条"金标准"（独立于 app.js 内那张表） */
const GOLD = [
  [0x100, [66, 0x01, 3, 8, 0, 0, 0, 0], '车速 66 km/h · 档位 D · 转速 800 rpm · 左转'],
  [0x101, [0x01, 0x00, 0, 0, 0, 0, 0, 0], '开门：左前门 · 未上锁'],
  [0x102, [0x03, 0x03, 24, 60, 0, 0, 0, 0], 'AC开 24℃ 内循环 · 车外 20℃'],
  [0x103, [0x10, 0x27, 0x00, 0x50, 0x00, 50, 0x58, 0x02],
    '总里程 1000.0 km · 小计 8.0 km · 油量 50% · 续航 600 km'],
  [0x104, [0x62, 0x55, 1, 0x03, 1, 0x01, 0x03, 0],
    '瞬时油耗 9.8 L/100km · 平均 8.5 L/100km · 模式 标准 · 告警[发动机/胎压] · 故障 1 个 · 故障码 P0301'],
  [0x105, [0x50, 0x50, 0x4F, 0x51, 0x3C, 0x3D, 0x3C, 0x3E],
    '胎压 左前 400 kPa / 右前 400 kPa / 左后 395 kPa / 右后 405 kPa · 胎温 左前 20℃ / 右前 21℃ / 左后 20℃ / 右后 22℃']
];
let goldBad = 0;
console.log('\n—— 金标准复核（独立清单）——');
GOLD.forEach(function (g) {
  const got = sandbox.result.decodeFrame(g[0], g[1]);
  if (got === g[2]) { console.log('OK   0x' + g[0].toString(16).toUpperCase()); }
  else { goldBad++; console.log('FAIL 0x' + g[0].toString(16).toUpperCase() + '\n  期望 ' + g[2] + '\n  实得 ' + got); }
});

const allOk = ok && goldBad === 0 && FAILS.length === 0;
console.log('\n==> 前端自检 ' + (allOk ? '通过（退出码 0）' : '失败（退出码 1）'));
process.exit(allOk ? 0 : 1);
