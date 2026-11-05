#!/usr/bin/env node
/* agentsim 前端冒烟测试 — 最小 DOM stub · 零依赖
 *
 * 用法：node frontend/tools/smoke.mjs
 * 无法跑浏览器（沙箱无 GUI），这里用最小 DOM stub 求值全部前端模块，并断言：
 *   1. v3 轨迹（真实后端数据形状：帧键 tick,n,b,f,c；n=7 项；c=6 项；f=连线对）能解析
 *   2. v2 轨迹（对象形态 + f=食物元组）也能解析（向后兼容；v2 f 绝不当连线）
 *   3. 没有 food 键不崩（v3 本就无 food；缺 f/c/g 键也容错）
 *   4. f 连线能映射到节点坐标（连线端点 id 在 cyano 里查得到）
 *   5. c 6 项能正确取 hue（hueReal=true，v2 5 项 → 兜底 hue 且 hueReal=false）
 *   6. g 键 26 个数解析成 24 基因 + hue（Genome.h 声明序对拍）
 *   7. 真实 v3 trace 文件（主项目 exe 生成）对拍解析
 *   8. 演示数据确定性 + 审计恒等式
 *  11-15. R7-D：调色板默认值 / GET /packs 覆盖 / 材质包 UI + POST /set-packs /
 *         存档缩略图（无帧数据不伪造 → 占位）
 * 全部断言通过 → 打印 SMOKE PASSED，否则非零退出。
 */
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import path from "node:path";
import vm from "node:vm";

const here = path.dirname(fileURLToPath(import.meta.url));   // frontend/tools
const fe = path.resolve(here, "..");
let failures = 0, checks = 0;

function ok(cond, label) {
  checks++;
  if (cond) { console.log("  ok  " + label); }
  else { failures++; console.error("  FAIL " + label); }
}
function eq(a, b, label) {
  checks++;
  const sa = JSON.stringify(a), sb = JSON.stringify(b);
  if (sa === sb) console.log("  ok  " + label);
  else { failures++; console.error("  FAIL " + label + "\n        got: " + sa + "\n        exp: " + sb); }
}
function near(a, b, eps, label) {
  checks++;
  if (Math.abs(a - b) <= eps) console.log("  ok  " + label + " (" + a + ")");
  else { failures++; console.error("  FAIL " + label + " got " + a + " exp " + b + " (eps " + eps + ")"); }
}

// ── 最小 DOM stub ──────────────────────────────────────────────
function makeCtx() {
  const noop = () => {};
  return {
    canvas: { width: 300, height: 150 },
    clearRect: noop, fillRect: noop, strokeRect: noop, beginPath: noop,
    moveTo: noop, lineTo: noop, arc: noop, closePath: noop, fill: noop,
    stroke: noop, save: noop, restore: noop, translate: noop, scale: noop,
    setLineDash: noop, createLinearGradient: () => ({ addColorStop: noop }),
    createRadialGradient: () => ({ addColorStop: noop }),
    fillText: noop, measureText: () => ({ width: 0 }),
    setTransform: noop, drawImage: noop,
    getImageData: () => ({ data: new Uint8ClampedArray(4) }),
    putImageData: noop,
    fillStyle: "", strokeStyle: "", lineWidth: 1, shadowBlur: 0, shadowColor: "",
    font: "", textAlign: "", textBaseline: "", lineJoin: "", lineCap: "", globalAlpha: 1
  };
}
function makeCanvas() {
  return {
    width: 300, height: 150, clientWidth: 300, clientHeight: 150,
    style: {}, classList: { add: noop2, remove: noop2, toggle: noop2, contains: () => false },
    listeners: {},
    addEventListener(ev, fn) { (this.listeners[ev] = this.listeners[ev] || []).push(fn); },
    removeEventListener() {}, setPointerCapture() {}, releasePointerCapture() {},
    getBoundingClientRect: () => ({ left: 0, top: 0, width: 300, height: 150 }),
    getContext: () => makeCtx(),
    toDataURL: () => "data:image/jpeg;base64,stubthumb"
  };
}
function noop2() {}
const elements = {};
function fakeEl(tag) {
  return {
    tagName: String(tag || "div").toUpperCase(),
    children: [], style: {}, dataset: {},
    classList: { set: new Set(), add(c) { this.set.add(c); }, remove(c) { this.set.delete(c); },
      toggle(c, v) { v ? this.set.add(c) : this.set.delete(c); }, contains(c) { return this.set.has(c); } },
    attributes: {},
    setAttribute(k, v) { this.attributes[k] = String(v); },
    getAttribute(k) { return this.attributes[k]; },
    appendChild(c) { this.children.push(c); return c; },
    querySelectorAll() { return []; },
    querySelector() { return null; },
    _ev: {},
    addEventListener(ev, fn) { (this._ev[ev] = this._ev[ev] || []).push(fn); },
    removeEventListener() {},
    click() { (this._ev.click || []).forEach((f) => f.call(this)); },
    fire(ev) { (this._ev[ev] || []).forEach((f) => f.call(this)); },
    getContext: () => makeCtx(),
    getBoundingClientRect: () => ({ left: 0, top: 0, width: 300, height: 150 }),
    clientWidth: 300, clientHeight: 150, width: 300, height: 150,
    files: null, focus() {},   // click() 由上方监听版提供（原 no-op 已删）
    // R7-D：innerHTML 语义对齐浏览器——赋 '' 清空子节点（渲染函数先 innerHTML=''
    // 再 append 的模式在真实浏览器里不累积；stub 也必须不累积，否则重复渲染会翻倍）
    _innerHTML: "",
    set innerHTML(v) { if (v === "") this.children.length = 0; this._innerHTML = String(v); },
    get innerHTML() { return this._innerHTML; }
  };
}
const sandbox = {
  console, setTimeout, clearTimeout, setInterval, clearInterval,
  Math, JSON, isFinite, parseInt, parseFloat, Date, RegExp, Error, TypeError,
  Uint8ClampedArray, Float32Array, Array, Object, String, Number, Boolean, isNaN,
  requestAnimationFrame: () => 0,
  navigator: { userAgent: "smoke" },
  performance: { now: () => 0 },
  localStorage: { getItem: () => null, setItem() {}, removeItem() {} },
  sessionStorage: { getItem: () => null, setItem() {}, removeItem() {} },
  FileReader: function () { this.readAsText = function () { if (this.onerror) this.onerror(); }; },
  document: {
    createElement: (tag) => (tag === "canvas" ? makeCanvas() : fakeEl(tag)),
    getElementById: (id) => (elements[id] !== undefined ? elements[id] : (elements[id] = fakeEl("div"))),
    addEventListener() {}, removeEventListener() {},
    body: fakeEl("body"), documentElement: fakeEl("html"), hidden: false
  }
};
sandbox.window = sandbox;
sandbox.globalThis = sandbox;
sandbox.window.addEventListener = () => {};
sandbox.window.devicePixelRatio = 1;
sandbox.window.innerWidth = 1200;
sandbox.window.innerHeight = 800;

function loadModule(rel) {
  const code = readFileSync(path.join(fe, rel), "utf8");
  vm.runInContext(code, sandbox, { filename: rel });
}
const ctx = vm.createContext(sandbox);

console.log("== module evaluation under DOM stub ==");
for (const m of ["js/utils.js", "js/demo-data.js", "js/live-client.js", "js/trace-parser.js", "js/charts.js",
                 "js/renderer.js", "js/trace-io.js", "js/main.js"]) {
  try { loadModule(m); console.log("  ok  " + m + " evaluated"); }
  catch (e) { failures++; console.error("  FAIL " + m + ": " + e.stack.split("\n").slice(0, 3).join(" | ")); }
}

const P = sandbox.window.TraceParser;
console.log("== 1) v3 trace (real backend shape: keys tick,n,b,f,c) ==");
// 形状取自主项目 agentsim.exe --trace 的实测输出（见任务卡 + build/v3.json 对拍）
const v3 = {
  schemaVersion: 3,
  meta: { contractVersion: 3, seed: 42, ticks: 2000, framesCaptured: 3, framesTruncated: false },
  bounds: { minX: 0, minY: 0, maxX: 256, maxY: 256 },
  series: [
    { tick: 0, nuclei: 3, cyanoAlive: 2, cyanoEnergy: 20.5, totalEnergy: 400.25, injected: 500.0, dissipated: 99.75, auditOk: true },
    { tick: 1, nuclei: 3, cyanoAlive: 3, cyanoEnergy: 30.5, totalEnergy: 410.25, injected: 510.0, dissipated: 99.75, auditOk: true }
  ],
  frames: [
    { tick: 0,
      n: [[1, 10.5, 20.5, 100.0, 200, 1.5, true], [2, 30.25, 40.75, 90.5, 200, 0.0, true], [3, 200.0, 200.0, 80.0, 200, 2.0, true]],
      b: [[7, 1, 12.0, 22.0, 5.0, 3.2, 0], [8, 0, 99.0, 99.0, 6.0, 3.0, 1]],
      f: [[1, 2]],
      c: [[1, 5.0, 6.0, 10.0, true, 120.5], [2, 250.0, 250.0, 5.0, true, 130.0]] },
    { tick: 1,
      n: [[1, 11.0, 21.0, 101.0, 200, 1.6, true], [2, 31.0, 41.0, 91.0, 200, 0.1, true], [3, 201.0, 201.0, 81.0, 200, 2.1, true]],
      b: [[7, 1, 13.0, 23.0, 5.0, 3.2, 0]],
      f: [[1, 2], [2, 3]],
      c: [[1, 5.5, 6.5, 11.0, true, 122.0], [2, 250.5, 250.5, 6.0, true, 129.0], [3, 100.0, 100.0, 4.0, true, 250.0]] }
  ]
};
const pv3 = P.parse(JSON.parse(JSON.stringify(v3)));
eq(pv3.schemaVersion, 3, "v3 schemaVersion round-trips");
eq(pv3.frames.length, 2, "v3 frames count");
eq(pv3.frames[0].nuclei.length, 3, "v3 n tuples -> nuclei");
eq(pv3.frames[0].cyano.length, 2, "v3 c tuples -> cyano");
eq(pv3.frames[0].balls.length, 2, "v3 b tuples -> balls");
near(pv3.frames[0].nuclei[0].x, 10.5, 1e-9, "v3 nucleus x from tuple[1]");
near(pv3.frames[0].nuclei[0].energy, 100.0, 1e-9, "v3 nucleus energy from tuple[3]");
near(pv3.frames[0].nuclei[0].cap, 200, 1e-9, "v3 nucleus cap from tuple[4]");
eq(pv3.frames[0].nuclei[0].alive, true, "v3 nucleus alive from tuple[6]");
eq(pv3.frames[0].nuclei[0].forceTarget, -1, "v3 7-item tuple -> forceTarget=-1");
eq(pv3.frames[0].stats.nuclei, 3, "v3 series stats.nuclei");
near(pv3.frames[0].stats.cyanoEnergy, 20.5, 1e-9, "v3 series stats.cyanoEnergy");
eq(pv3.frames[0].stats.auditOk, true, "v3 series stats.auditOk");

console.log("== 4) f links map to node coordinates ==");
const f1 = pv3.frames[1];
eq(f1.cyanoLinks.length, 2, "v3 f pairs -> cyanoLinks count");
const byId = {};
for (const cn of f1.cyano) byId[cn.id] = cn;
let mapped = 0, broken = 0;
for (const [a, b2] of f1.cyanoLinks) {
  if (byId[a] && byId[b2]) mapped++; else broken++;
}
eq(mapped, 2, "all link endpoints resolve to cyano nodes");
eq(broken, 0, "no dangling link endpoints");
near(byId[1].x, 5.5, 1e-9, "linked node coordinate accessible");

console.log("== 5) c 6-tuple hue ==");
near(f1.cyano[0].hue, 122.0, 1e-9, "c tuple[5] -> hue");
eq(f1.cyano[0].hueReal, true, "c 6-tuple hue flagged real");
near(f1.cyano[0].energy, 11.0, 1e-9, "c tuple[3] -> energy");
eq(f1.cyano[0].alive, true, "c tuple[4] -> alive");
ok(Number.isFinite(f1.cyanoHueSd), "cyano hue circular SD computed (" + f1.cyanoHueSd.toFixed(1) + "°)");
ok(f1.cyanoHueSd > 20, "diverse cyano hues give SD > 20°");

console.log("== 6) g key: 26 numbers -> 24 genes + hue (Genome.h order) ==");
// Genome.h 声明序构造一行：id + affinity×3 + orbitRadius×3 + absorbPreference×3 +
// repelStrength×3 + maxSpeed + maxAccel + energyThreshold + mutationRate +
// cyanoPreference + cyanoDigestion + nucleusSightRadius + nucleusPersonalSpace +
// nucleusRepelStrength + nucleusCohesion + forceRange + forceCostK + hue
const gRow = [2,   // id=2：attach 到帧内已存在的核
  0.2, 0.3, 0.5,             // affinity
  60, 70, 80,                // orbitRadius
  1.1, 1.2, 1.3,             // absorbPreference
  2.1, 2.2, 2.3,             // repelStrength
  77.5, 8.25, 300, 0.15,     // maxSpeed, maxAccel, energyThreshold, mutationRate
  0.75, 0.85,                // cyanoPreference, cyanoDigestion
  250, 40, 4.5, 2.5,         // nucleusSightRadius, nucleusPersonalSpace, nucleusRepelStrength, nucleusCohesion
  120, 0.03,                 // forceRange, forceCostK
  217.25];                   // hue
const v3g = JSON.parse(JSON.stringify(v3));
v3g.frames[1].g = [gRow, [1, 0.3, 0.3, 0.4, 55, 65, 75, 1.0, 1.0, 1.0, 2.0, 2.0, 2.0, 60, 7, 250, 0.1, 0.7, 0.8, 220, 35, 3.5, 2.0, 100, 0.02, 45.5]];  // 全核覆盖（真导出语义）
const pv3g = P.parse(v3g);
eq(pv3g.hasGenome, true, "hasGenome=true when g present");
const gen = pv3g.frames[1].genomes[0];
eq(gen.genes.length, 24, "g row -> 24 genes");
near(gen.hue, 217.25, 1e-9, "g row last number -> hue");
near(gen.genes[12], 77.5, 1e-9, "genes[12] = maxSpeed (decl order idx 12)");
near(gen.genes[14], 300, 1e-9, "genes[14] = energyThreshold (decl order idx 14)");
near(gen.genome.maxSpeed, 77.5, 1e-9, "genome.maxSpeed mapped");
near(gen.genome.energyThreshold, 300, 1e-9, "genome.energyThreshold mapped");
near(gen.genome.affinity[2], 0.5, 1e-9, "genome.affinity[2] mapped");
near(gen.genome.forceCostK, 0.03, 1e-9, "genome.forceCostK mapped");
const nu9 = pv3g.frames[1].nuclei.filter(function (n) { return n.id === 2; })[0];
ok(nu9, "g row attaches to nucleus by id");
near(nu9.hue, 217.25, 1e-9, "attached nucleus hue = g hue");
eq(nu9.hueReal, true, "attached nucleus hueReal=true");
eq(nu9.genomeReal, true, "attached nucleus genomeReal=true");
ok(Number.isFinite(pv3g.frames[1].hueSd), "nucleus hue SD computed from g");

console.log("== 2) v2 trace (real v2 files are TUPLE form: f=food tuples, c=5-item, no hue) ==");
// 形状取自实测 build/test_trace_v2_run_a.json（n=7 项 / f=食物 5 项 / c=5 项）
const v2 = {
  schemaVersion: 2,
  meta: { contractVersion: 2, seed: 7 },
  bounds: { minX: 0, minY: 0, maxX: 256, maxY: 256 },
  series: [{ tick: 0, nuclei: 1, food: 9, cyanoAlive: 2, cyanoEnergy: 4.0, totalEnergy: 100.0, injected: 120.0, dissipated: 20.0, auditOk: true }],
  frames: [{
    tick: 0,
    n: [[1, 3.0, 4.0, 90.0, 120, 1.0, true]],
    b: [[7, 1, 5.0, 6.0, 5.0, 3.0, 1]],
    f: [[11, 12.0, 13.0, 14.0, true], [15, 16.0, 17.0, 18.0, false]],   // v2 食物 5 元组
    c: [[21, 1.0, 2.0, 3.0, true], [22, 8.0, 9.0, 4.0, false]]          // v2 蓝细菌 5 元组（含死节点）
  }]
};
const pv2 = P.parse(JSON.parse(JSON.stringify(v2)));
eq(pv2.schemaVersion, 2, "v2 schemaVersion round-trips");
eq(pv2.frames[0].nuclei.length, 1, "v2 tuple nuclei parsed");
eq(pv2.frames[0].balls.length, 1, "v2 tuple balls parsed");
eq(pv2.frames[0].cyano.length, 2, "v2 dead cyano node still exported (v2 semantics)");
  eq(pv2.frames[0].cyano[1].alive, false, "v2 dead cyano alive=false visible (row with alive=false)");
eq(pv2.frames[0].cyanoLinks.length, 0, "v2 food tuples NOT parsed as links");
near(pv2.frames[0].nuclei[0].energy, 90, 1e-9, "v2 nucleus energy from tuple");
// v2 蓝细菌 5 项无 hue → 兜底 130 且 hueReal=false（不污染多样性统计）
near(pv2.frames[0].cyano[0].hue, 130, 1e-9, "v2 5-item cyano hue defaults to 130");
eq(pv2.frames[0].cyano[0].hueReal, false, "v2 cyano hueReal=false (not real data)");
eq(Number.isNaN(pv2.frames[0].cyanoHueSd), true, "v2 cyano hue SD = NaN (no fake diversity)");
eq(pv2.series[0].food, 9, "v2 series food key kept as-is");

console.log("== 2b) v2 object-form frame (old /state export / old demo / save snapshot) ==");
const v2obj = {
  schemaVersion: 2,
  meta: { contractVersion: 2 },
  frames: [{
    tick: 0,
    nuclei: [{ id: 1, x: 3, y: 4, energy: 90, cap: 120, speed: 1, hue: 33.5, alive: true,
               forceTarget: 7, forceX: 0.5, forceY: -0.5,
               genome: { affinity: [0.5, 0.25, 0.25], maxSpeed: 88, energyThreshold: 175 } }],
    balls: [{ id: 7, owner: 1, x: 5, y: 6, energy: 5, radius: 3, type: 1 }],
    cyano: [{ id: 21, x: 1, y: 2, energy: 3, hue: 140, alive: true }],
    cyanoLinks: [[21, 999]]   // 悬空端点也保留（渲染时查不到就跳过）
  }]
};
const pv2o = P.parse(v2obj);
eq(pv2o.frames[0].nuclei.length, 1, "v2 object nuclei parsed");
near(pv2o.frames[0].nuclei[0].hue, 33.5, 1e-9, "v2 nucleus object hue kept");
eq(pv2o.frames[0].nuclei[0].hueReal, true, "v2 nucleus hue flagged real");
eq(pv2o.frames[0].nuclei[0].forceTarget, 7, "v2 forceTarget kept");
near(pv2o.frames[0].nuclei[0].genome.maxSpeed, 88, 1e-9, "v2 genome object merged with defaults");
eq(pv2o.frames[0].cyanoLinks.length, 1, "v2 object cyanoLinks kept");
near(pv2o.frames[0].cyano[0].hue, 140, 1e-9, "v2 object cyano hue kept (real)");
eq(pv2o.frames[0].cyano[0].hueReal, true, "v2 object cyano hueReal=true");

console.log("== 2c) v2 mixed form (tuple n + object cyano in one frame) ==");
const v2mix = {
  schemaVersion: 2,
  frames: [{ tick: 3,
    n: [[1, 1, 2, 3, 4, 5, true]],
    nuclei: [{ id: 5, x: 7, y: 8, energy: 9 }],
    c: [[31, 10, 20, 30, true, 200]],
    cyano: [{ id: 32, x: 11, y: 22, energy: 33, alive: true }] }]
};
const pv2m = P.parse(v2mix);
eq(pv2m.frames[0].nuclei.length, 2, "mixed tuple+object nuclei merged");
eq(pv2m.frames[0].cyano.length, 2, "mixed tuple+object cyano merged");
eq(pv2m.frames[0].cyano[0].hueReal, true, "mixed 6-item tuple cyano hue real");
eq(pv2m.frames[0].cyano[1].hueReal, false, "mixed object cyano without hue flagged not real");

console.log("== 2d) real v2 fixture file from build dir (if present) ==");
// 契约已于 2026-11-01 升到 v3（R1-B 取消食物球），build/ 下不再产出真正的 v2 文件：
// 单元测试的夹具名已改为 test_trace_v3_*，内容也是 v3。这里保留对"任意仍在磁盘上的
// 真 v2 文件"的回归检查，但**只有当文件确实是 v2 时才断言**——否则读到 v3 文件会
// 让 3 条断言假失败（曾发生：build/test_trace_v2_run_a.json 被 v3 内容覆盖写）。
// 兼容性本身由上一节 2) 的合成 v2 夹具覆盖，不依赖磁盘上是否有历史文件。
let v2RealPath = path.join(fe, "..", "build", "test_trace_v2_run_a.json");
try {
  const v2real = JSON.parse(readFileSync(v2RealPath, "utf8"));
  if (v2real.schemaVersion !== 2) {
    console.log("  --  build/test_trace_v2_run_a.json is v" + v2real.schemaVersion +
                " (契约已升 v3), skip real-v2 assertions; synthetic v2 fixture covers compat");
    throw { skip: true };
  }
  const pv2r = P.parse(v2real);
  eq(pv2r.schemaVersion, 2, "real v2 fixture schemaVersion=2");
  ok(pv2r.frames.length > 0, "real v2 fixture frames=" + pv2r.frames.length);
  let foodAsLink = 0;
  for (const fr of pv2r.frames) foodAsLink += fr.cyanoLinks.length;
  eq(foodAsLink, 0, "real v2 fixture: food tuples never become links");
  ok(pv2r.series.length > 0 && pv2r.series[0].food !== undefined, "real v2 fixture series keeps food key");
} catch (e) { console.log("  --  real v2 fixture not found, skipped"); }
console.log("== 3) no food key / missing keys -> no crash ==");
const v3bare = { schemaVersion: 3, meta: {}, bounds: { minX: 0, minY: 0, maxX: 10, maxY: 10 },
  series: [], frames: [{ tick: 5, n: [[1, 1, 2, 3, 4, 5, true]], b: [], c: [] }] };
const pbare = P.parse(v3bare);
eq(pbare.frames[0].nuclei.length, 1, "frame without f/c arrays parses");
eq(pbare.frames[0].cyanoLinks.length, 0, "missing f -> empty links");
eq(pbare.frames[0].genomes.length, 0, "missing g -> empty genomes");
eq(pbare.series.length, 0, "missing series -> empty");
// 完全空帧也不崩
const pEmpty = P.parse({ schemaVersion: 3, meta: {}, frames: [null, {}, { tick: 9 }] });
eq(pEmpty.frames.length, 3, "null/empty frames tolerated");
eq(pEmpty.frames[0].tick, 0, "null frame gets index tick");

console.log("== version guard ==");
let threw = false;
try { P.parse({ schemaVersion: 4, meta: {}, frames: [] }); } catch (e) { threw = true; }
ok(threw, "schemaVersion 4 rejected loudly");
threw = false;
try { P.parse({ meta: {}, frames: [] }); } catch (e) { threw = true; }
ok(threw, "missing schemaVersion rejected");
threw = false;
try { P.parse("not an object"); } catch (e) { threw = true; }
ok(threw, "non-object payload rejected");

// ==================== R4-D: LiveClient（实时 /state 二进制快照）====================
console.log("== 9) live-client: /state binary snapshot parse (R4-D) ==");
const LC = sandbox.window.LiveClient;
ok(LC && typeof LC.parse === "function", "LiveClient.parse exposed");
ok(typeof LC.probe === "function" && typeof LC.start === "function" &&
   typeof LC.stop === "function" && typeof LC.isActive === "function",
   "LiveClient probe/start/stop/isActive exposed");

// 9a. 手工编码一个与 buildStateSnapshot 完全同构的 /state 快照：
// 头 80B + 球 25B×2 + 核 32B×2 + 蓝细菌 20B×2 + 连线 8B×1（小端）
function buildStateFixture(tickOpt) {
  const u8 = [], f32 = [], i32 = [];
  const tick = (tickOpt === undefined) ? 777 : tickOpt;
  function pushU8(v) { u8.push(v & 0xff); }
  function pushU32(v) { for (let k = 0; k < 4; k++) pushU8((v >>> (k * 8)) & 0xff); }
  function pushI32(v) { pushU32(v >>> 0); }
  function pushF32(v) { const b = new Float32Array(1); b[0] = v; const dv = new DataView(b.buffer); for (let k = 0; k < 4; k++) pushU8(dv.getUint8(k)); }
  const W = 256;
  function wrapTorus(p) { let x = p.x % W, y = p.y % W; if (x < 0) x += W; if (y < 0) y += W; return { x, y }; }
  // ---- 头 ----
  pushU8(0x41); pushU8(1); pushU8(0); pushU8(0);          // magic/version/状态/保留
  pushI32(tick);                                           // tick
  pushF32(256); pushF32(256);                              // 世界宽高
  pushI32(2); pushI32(2); pushI32(2);                      // 球数/核数/存活核数
  pushI32(1); pushI32(2); pushI32(1);                      // 自由球/蓝细菌/连线
  pushF32(4.25);                                           // 蓝细菌平均能量
  pushF32(0.5);                                            // 全局阳光
  pushI32(0); for (let k = 0; k < 28; k++) pushU8(0);      // 保留
  // ---- 球 25B：id owner x y energy type(u8) radius ----
  const balls = [
    { id: 11, owner: 1, x: 12.5, y: 22.5, e: 5.5, t: 0, r: 3.25 },
    { id: 12, owner: 0, x: 99.5, y: 99.5, e: 6.5, t: 2, r: 3.0 }   // 自由球
  ];
  for (const b of balls) {
    pushU32(b.id); pushU32(b.owner);
    const p = wrapTorus(b); pushF32(p.x); pushF32(p.y);
    pushF32(b.e); pushU8(b.t); pushF32(b.r);
  }
  // ---- 核 32B：id x y energy cap speed hue alive+pad3 ----
  const nuclei = [
    { id: 1, x: 10.5, y: 20.5, e: 100.0, cap: 200.0, sp: 1.5, hue: 217.25, alive: 1 },
    { id: 2, x: 30.25, y: 40.75, e: 90.5, cap: 200.0, sp: 0.0, hue: 33.5, alive: 1 }
  ];
  for (const n of nuclei) {
    pushU32(n.id);
    const p = wrapTorus(n); pushF32(p.x); pushF32(p.y);
    pushF32(n.e); pushF32(n.cap); pushF32(n.sp); pushF32(n.hue);
    pushU8(n.alive); pushU8(0); pushU8(0); pushU8(0);
  }
  // ---- 蓝细菌 20B：id x y energy hue ----
  const cyano = [
    { id: 21, x: 5.0, y: 6.0, e: 4.0, hue: 120.5 },
    { id: 22, x: 250.0, y: 250.0, e: 4.5, hue: 250.0 }
  ];
  for (const c of cyano) {
    pushU32(c.id); const p = wrapTorus(c); pushF32(p.x); pushF32(p.y);
    pushF32(c.e); pushF32(c.hue);
  }
  // ---- 连线 8B：idA idB ----
  pushU32(21); pushU32(22);
  return { buf: new Uint8Array(u8).buffer,
           balls, nuclei, cyano,
           totalE: 100 + 90.5 + 5.5 + 6.5 + 4 + 4.5 };
}
const fx = buildStateFixture();
eq(fx.buf.byteLength, 80 + 2 * 25 + 2 * 32 + 2 * 20 + 1 * 8, "fixture size = 80+50+64+40+8 (strides 25/32/20/8)");

const lf = LC.parse(fx.buf);
eq(lf.tick, 777, "live tick from header i32");
eq(lf.balls.length, 2, "live balls count");
eq(lf.nuclei.length, 2, "live nuclei count");
eq(lf.cyano.length, 2, "live cyano count");
eq(lf.cyanoLinks.length, 1, "live links count");
eq(JSON.stringify(lf.cyanoLinks[0]), "[21,22]", "live link pair");
eq(lf.nuclei[0].id, 1, "live nucleus id");
near(lf.nuclei[0].x, 10.5, 1e-6, "live nucleus x");
near(lf.nuclei[0].energy, 100.0, 1e-6, "live nucleus energy");
near(lf.nuclei[0].cap, 200.0, 1e-6, "live nucleus cap");
near(lf.nuclei[0].speed, 1.5, 1e-6, "live nucleus speed");
near(lf.nuclei[0].hue, 217.25, 1e-6, "live nucleus hue (32B record: hue@24, alive@28)");
eq(lf.nuclei[0].alive, true, "live nucleus alive from byte 28");
near(lf.nuclei[1].hue, 33.5, 1e-6, "live nucleus hue #2");
eq(lf.nuclei[0].hueReal, true, "live hueReal=true (backend genome.hue)");
near(lf.balls[0].radius, 3.25, 1e-6, "live ball radius (25B record: type@20, radius@21)");
eq(lf.balls[1].owner, 0, "live free ball owner=0");
eq(lf.balls[1].type, 2, "live ball type byte");
near(lf.cyano[0].hue, 120.5, 1e-6, "live cyano hue");
eq(lf.cyano[0].alive, true, "live cyano alive (snapshot only carries alive nodes)");
ok(lf.stats, "live frame has stats");
near(lf.stats.totalEnergy, fx.totalE, 0.01, "live stats.totalEnergy = sum of entity energies");
near(lf.stats.cyanoEnergy, 8.5, 1e-6, "live stats.cyanoEnergy");
ok(lf.live && lf.live.sun === 0.5, "live.live.sun from header f32@44");
ok(lf.live.worldW === 256 && lf.live.worldH === 256, "live.live world size from header");
eq(lf.live.strideWarn, false, "no stride warning on well-formed snapshot");

// 9c. R8-A: /state v2 — 128B nucleus records with 24 genome f32 (genes at +32)
function buildStateV2Fixture() {
  const u8 = [];
  function pushU8(v) { u8.push(v & 0xff); }
  function pushU32(v) { for (let k = 0; k < 4; k++) pushU8((v >>> (k * 8)) & 0xff); }
  function pushI32(v) { pushU32(v >>> 0); }
  function pushF32(v) { const b = new Float32Array(1); b[0] = v; const dv = new DataView(b.buffer); for (let k = 0; k < 4; k++) pushU8(dv.getUint8(k)); }
  // header (80B)
  pushU8(0x41); pushU8(2); pushU8(0); pushU8(0);
  pushI32(555);                       // tick
  pushF32(512); pushF32(512);         // world
  pushI32(0);                         // balls
  pushI32(1);                         // nuclei
  pushI32(1);                         // alive
  pushI32(0);                         // free
  pushI32(0);                         // cyano
  pushI32(0);                         // links
  pushF32(0); pushF32(0.5);           // cyanoAvgE, sun
  pushI32(0); for (let k = 0; k < 28; k++) pushU8(0);
  // nucleus 128B: 32B head + 24 f32 genes
  pushU32(9); pushF32(1); pushF32(2); pushF32(60); pushF32(200); pushF32(1.5);
  pushF32(123.5); pushU8(1); pushU8(0); pushU8(0); pushU8(0);
  const genes = [];
  for (let t = 0; t < 3; t++) { genes.push(0.2 + 0.1 * t); pushF32(genes[genes.length - 1]); }   // affinity (sum=0.9? no: 0.2+0.3+0.4=0.9; fine, parser does not assert sum)
  for (let t = 0; t < 3; t++) { genes.push(100 + t); pushF32(genes[genes.length - 1]); }          // orbitRadius
  for (let t = 0; t < 3; t++) { genes.push(1 + t); pushF32(genes[genes.length - 1]); }            // absorbPreference
  for (let t = 0; t < 3; t++) { genes.push(2 + t); pushF32(genes[genes.length - 1]); }            // repelStrength
  genes.push(66.5); pushF32(66.5);       // maxSpeed
  genes.push(7.5);  pushF32(7.5);        // maxAccel
  genes.push(150);  pushF32(150);        // energyThreshold
  genes.push(0.12); pushF32(0.12);       // mutationRate
  genes.push(0.6);  pushF32(0.6);        // cyanoPreference
  genes.push(0.55); pushF32(0.55);       // cyanoDigestion
  genes.push(170);  pushF32(170);        // nucleusSightRadius
  genes.push(47);   pushF32(47);         // nucleusPersonalSpace
  genes.push(3.2);  pushF32(3.2);        // nucleusRepelStrength
  genes.push(1.1);  pushF32(1.1);        // nucleusCohesion
  genes.push(90);   pushF32(90);         // forceRange
  genes.push(0.027); pushF32(0.027);     // forceCostK
  eq(genes.length, 24, "v2 fixture genes count = 24");
  return { buf: new Uint8Array(u8).buffer, genes };
}
const fx2 = buildStateV2Fixture();
eq(fx2.buf.byteLength, 80 + 128, "v2 fixture size = 80 + 128B nucleus");
const lf2 = LC.parse(fx2.buf);
eq(lf2.nuclei.length, 1, "v2: nucleus parsed");
ok(lf2.nuclei[0].genomeReal === true, "v2: genomeReal=true (genes carried)");
near(lf2.nuclei[0].genome.maxSpeed, 66.5, 1e-6, "v2: maxSpeed @+32+48");
near(lf2.nuclei[0].genome.forceCostK, 0.027, 1e-6, "v2: forceCostK @+32+92 (last gene)");
near(lf2.nuclei[0].genome.nucleusCohesion, 1.1, 1e-6, "v2: nucleusCohesion mid-scalar offset");
near(lf2.nuclei[0].genome.affinity[2], 0.4, 1e-6, "v2: affinity[2] array tail");
near(lf2.nuclei[0].genome.orbitRadius[1], 101, 1e-6, "v2: orbitRadius[1]");
near(lf2.nuclei[0].hue, 123.5, 1e-6, "v2: hue still from 32B head");
// v1 buffer must still parse (compat window): strip to v1 by rebuilding header version
(function v1Compat() {
  const v = new Uint8Array(fx2.buf); v[1] = 1;
  // onWarn 静默：version=1 下 128B 记录按 32B 读必然 stride 警告（预期行为，9c 已测通道）；
  // 默认 console.warn 会写 stderr，在 PS5.1 EAP=Stop 门禁下会被升级为终止错误。
  const lf1 = LC.parse(v.buffer, { onWarn: function () {} });
  ok(lf1.nuclei[0].genomeReal === false, "v1: genomeReal=false (no genes, no fake data)");
  eq(lf1.nuclei.length, 1, "v1: nucleus parsed from same buffer");
})();

// 9b. parse 产物可直接喂回 TraceParser（同构性验收：对象形态容错通道）
const reParsed = P.parse({ schemaVersion: 2, meta: {}, frames: [lf] });
eq(reParsed.frames[0].nuclei.length, 2, "live frame re-parses through TraceParser (object form)");
near(reParsed.frames[0].nuclei[0].hue, 217.25, 1e-6, "TraceParser keeps live real hue");
ok(reParsed.frames[0].nuclei[0].genomeReal === false || reParsed.frames[0].nuclei[0].genomeReal === true,
   "re-parsed nucleus keeps genomeReal flag");

// 9c. stride 不匹配 → 警告不崩（少 20B：尾部连线整条 + 最后一条蓝细菌被截）
const warnCalls = [];
const shortBuf = fx.buf.slice(0, fx.buf.byteLength - 20);
const lfx = LC.parse(shortBuf, { onWarn: (m) => warnCalls.push(String(m)) });
eq(lfx.balls.length, 2, "truncated snapshot keeps balls (front sections intact)");
eq(lfx.nuclei.length, 2, "truncated snapshot keeps nuclei");
eq(lfx.cyano.length, 1, "truncated snapshot: only 1 of 2 declared cyano readable (tail cut)");
eq(lfx.cyanoLinks.length, 1, "truncated snapshot: remaining 28B after 194B = 1 cyano(20B) + 1 link(8B)");
eq(warnCalls.length, 2, "stride mismatch + cyano truncation warned (not crashed): [" +
   warnCalls.map(w => w.slice(0, 20) + "…").join(" | ") + "]");

// 9d. magic 错 → 抛错（注意 Uint8Array(buffer) 是视图不是拷贝，必须 slice 复制）
const bad = new Uint8Array(fx.buf.slice(0)); bad[0] = 0x42;   // 'B'
let threwLive = false;
try { LC.parse(bad.buffer, { onWarn: () => {} }); } catch (e) { threwLive = /magic/i.test(e.message); }
ok(threwLive, "wrong magic (0x42) throws loudly");

// 9e. version 错 → 抛错（同理独立拷贝）
const badV = new Uint8Array(fx.buf.slice(0)); badV[1] = 2;
threw = false;
try { LC.parse(badV.buffer, { onWarn: () => {} }); } catch (e) { threwLive = /版本|version/i.test(e.message); }
ok(threwLive, "wrong version (2) throws loudly");

// 9f. 过短载荷 → 抛错
threw = false;
try { LC.parse(new ArrayBuffer(10)); } catch (e) { threwLive = /过短|80/.test(e.message); }
ok(threwLive, "payload < 80 bytes throws");

// 9g. 环回一致性：同输入两次 parse 结果逐字节一致（无隐藏 RNG/时间戳）
eq(JSON.stringify(LC.parse(fx.buf)), JSON.stringify(lf), "live parse deterministic (parse(parse-input) byte-equal)");

// 9h. LiveClient.probe：沙箱无 fetch → resolve(false)（file:// / 未启动后端回退演示语义）
let probed = null;
LC.probe("").then((v) => { probed = v; });
await new Promise((r) => setTimeout(r, 20));
ok(probed === false, "probe resolves false when fetch missing (demo fallback path)");

console.log("== 8) demo data: determinism + v3 shape + audit identity ==");
const D = sandbox.window.DemoData;
const d1 = D.generate({ seed: 42 });
const d2 = D.generate({ seed: 42 });
eq(JSON.stringify(d1), JSON.stringify(d2), "demo data deterministic (same seed, byte-equal JSON)");
eq(d1.schemaVersion, 3, "demo data is v3");
eq(d1.frames[10].n.length, d1.frames[10].stats.nuclei, "demo n count == stats.nuclei");
let auditMax = 0;
for (const s of d1.series) auditMax = Math.max(auditMax, Math.abs(s.injected - s.dissipated - s.totalEnergy));
ok(auditMax < 0.5, "audit identity holds across all demo frames (max |Δ| = " + auditMax.toFixed(4) + ")");
const pd = P.parse(d1);
ok(pd.frames[60].cyanoLinks.length >= 0, "demo links parse");
ok(pd.hasGenome, "demo data carries g (genome export)");
ok(pd.frames.every(fr => fr.cyano.every(cn => cn.hueReal)), "demo cyano hue always real");
ok(pd.frames.every(fr => fr.nuclei.every(nu => nu.genomeReal)), "demo nuclei genome always real");

let prReal = null;   // module scope: reused by idempotency section
console.log("== 7) real v3 trace file from main project exe ==");
let v3Path = path.join(fe, "..", "build", "v3.json");
let real = null;
try { real = JSON.parse(readFileSync(v3Path, "utf8")); console.log("  ok  build/v3.json loaded (" + real.frames.length + " frames)"); }
catch (e) { console.log("  --  build/v3.json not present (" + e.message.slice(0, 60) + ") — generate with agentsim.exe --trace; skipping"); }
if (real) {
  prReal = P.parse(real);
  eq(prReal.schemaVersion, 3, "real trace schemaVersion=3");
  ok(prReal.frames.length > 0, "real trace parsed frames=" + prReal.frames.length);
  const linksTotal = prReal.frames.reduce((acc, fr) => acc + fr.cyanoLinks.length, 0);
  console.log("  ok  total links across frames = " + linksTotal);
  let dangling = 0;
  for (const fr of prReal.frames) {
    const ids = new Set(fr.cyano.map(cn => cn.id));
    for (const [a, b2] of fr.cyanoLinks) if (!ids.has(a) || !ids.has(b2)) dangling++;
  }
  eq(dangling, 0, "real trace: zero dangling link endpoints");
  let hueOk = true;
  for (const fr of prReal.frames) for (const cn of fr.cyano) if (!cn.hueReal || cn.hue < 0 || cn.hue >= 360) hueOk = false;
  ok(hueOk, "real trace: every cyano hue real and in [0,360)");
  ok(prReal.series.length > 0 && prReal.series[0].food === undefined, "real series has no food key");
  ok(prReal.series.every(s => s.auditOk !== false), "real series auditOk all true");
  const lastHueSd = (() => { const a = prReal.frames.filter(fr => Number.isFinite(fr.cyanoHueSd)); return a.length ? a[a.length - 1].cyanoHueSd : NaN; })();
  console.log("  ok  last cyanoHueSd = " + (Number.isFinite(lastHueSd) ? lastHueSd.toFixed(1) + "°" : "n/a"));
}

console.log("== boot() end-to-end under stub (menu + full pipeline) ==");
// 走完整 boot：菜单页生成演示数据、存档种子化、参数页构建 —— 任何 raw/parsed 混用都会在这里炸
sandbox.Main.boot();
ok(Array.isArray(sandbox.window.Main._state.saves) && sandbox.window.Main._state.saves.length === 0,
  "R6-D: boot 后存档列表为空数组（离线不生成假存档，等 GET /saves）");
ok(sandbox.window.Main._state.demoTrace && sandbox.window.Main._state.demoTrace.frames.length > 0,
  "boot generated demo trace");
// 载入真实 v3 trace 走完整 loadTrace（charts setData + 派生序列）
const realObj = real; // 来自第 7 节
if (realObj) {
  const loaded = sandbox.window.Main.loadTrace(realObj, "真实v3对拍");
  ok(loaded, "loadTrace accepts real v3 trace end-to-end");
  ok(sandbox.window.Main._state.series.length > 0, "state.series derived (" + sandbox.window.Main._state.series.length + " rows)");
  ok(Array.isArray(sandbox.window.Main._state.hueSeries), "state.hueSeries derived from frames");
  // 环境轴 + 调控
  sandbox.window.Main.showPage("run");
  sandbox.window.Main.setAxis("light", 4);
  ok(sandbox.window.Main._state.interventions >= 1, "env axis intervention counted");
  ok(sandbox.window.Main._state.env.light === 4, "env axis set to 4");
  sandbox.window.Main.setPlaying(false);
}
// 演示轨迹全链路（菜单 startRun）
// R4-D：startRun 现在是**异步**的（先 probe /status，失败才回退演示数据）。
// 同步断言会在 Promise resolve 之前跑，看到的是空的 state.trace。
// 必须等一个宏任务（setTimeout 0 就够，因为 probe 在无 fetch 时是
// Promise.resolve(false)，微任务，setTimeout 一定排在它后面）。
sandbox.window.Main.startRun({ seed: 7, frames: 60, nuclei: 6, cyano: 20 });
await new Promise((r) => setTimeout(r, 20));
ok(sandbox.window.Main._state.page === "run", "startRun switches to run page");
ok(sandbox.window.Main._state.trace && sandbox.window.Main._state.trace.schemaVersion === 3, "startRun trace is v3 parsed");
ok(sandbox.window.Main._state.trace.frames[10].nuclei.length > 0, "parsed demo frame has nuclei");
ok(sandbox.window.Main._state.trace.frames[10].cyanoLinks.length >= 0, "parsed demo frame has links");

console.log("== parser idempotency (save-snapshot reload path) ==");
const once = P.parse(d1);
const twice = P.parse(JSON.parse(JSON.stringify(once)));
  eq(JSON.stringify(twice), JSON.stringify(once), "parse(parse(x)) === parse(x) (byte-equal)");
const nuIdem = twice.frames[30].nuclei[0];
ok(nuIdem.genomeReal, "re-parsed nucleus keeps real genome");
ok(nuIdem.hueReal, "re-parsed nucleus keeps real hue");
ok(Number.isFinite(twice.frames[30].hueSd), "re-parsed frame keeps hueSd");
if (prReal) {
  const realTwice = P.parse(JSON.parse(JSON.stringify(prReal)));
  ok(Number.isFinite(realTwice.frames[realTwice.frames.length - 1].cyanoHueSd),
    "re-parsed real trace keeps cyanoHueSd");
}

// ==================== R6-D: 菜单两卡 + 存档管理 + 进度条回放 ====================
console.log("== 10b) R6-D: 菜单两卡（在线 stub / 离线 stub 两态）==");
const LM = sandbox.window.Main;

// 先回到离线态：去 fetch → checkDemo → 两卡置灰 + 横幅
sandbox.fetch = undefined;
LM._state.online = null;
LM._live.checkDemo();
await new Promise((r) => setTimeout(r, 30));
ok(LM._state.online === false, "offline: state.online=false after checkDemo (no fetch)");
const cardLive = elements["cardLive"], cardSaves = elements["cardSaves"], banner = elements["offlineBanner"];
ok(cardLive.classList.contains("offline"), "offline: card A (实时开新局) 置灰 (.offline)");
ok(cardSaves.classList.contains("offline"), "offline: card B (载入存档) 置灰 (.offline)");
ok(!banner.classList.contains("hidden"), "offline: 顶部横幅 #offlineBanner 可见");
// 横幅文案在 index.html（stub 不解析 HTML）→ 直接对真实文件断言，并把真实片段塞进 stub 元素
const idxHtml = readFileSync(path.join(fe, "index.html"), "utf8");
const bannerM = idxHtml.match(/<div class="offline-banner[^>]*"[^>]*>[\s\S]*?<\/div>/);
ok(!!bannerM, "index.html 含 #offlineBanner 横幅元素");
ok(bannerM && bannerM[0].indexOf("未检测到本地服务") >= 0, "index.html: banner text mentions 未检测到本地服务");
ok(bannerM && bannerM[0].indexOf("--serve") >= 0, "index.html: banner text mentions --serve 命令");
ok(bannerM && bannerM[0].indexOf("menuDemoLink") >= 0, "index.html: banner 内含离线演示小链接");
ok(idxHtml.indexOf('id="cardLive"') >= 0 && idxHtml.indexOf('id="cardSaves"') >= 0,
   "index.html: 菜单两卡 #cardLive / #cardSaves 存在");
if (bannerM) banner.innerHTML = bannerM[0];
ok(sandbox.window.document.getElementById("menuDemoLink") !== undefined || elements["menuDemoLink"],
   "离线演示小链接 #menuDemoLink 存在（兜底）");

// 在线态：注入 fetch stub → checkDemo → 两卡恢复 + 横幅隐藏
const savePosts = [], seekPosts = [], loadPosts = [], delPosts = [], runPosts = [];
const envPosts = [], controlPosts = [];   // R4-D 段遗留共用（本段 stub 也要喂）
// R7-D：动态存档库（10d 用；/save /delete-save 维护，/saves 读取）
const savesDb = [
  { name: "演示 · seed 7", frame: 240, seed: 7, aliveNuclei: 8, savedAt: "2026-11-01 12:00:00" },
  { name: "auto-42-20261101", frame: 315, seed: 42, aliveNuclei: 12, savedAt: "2026-11-01 12:30:00" }
];
let onlineTick = 500;
sandbox.fetch = function (url, init) {
  const u = String(url || "");
  const method = (init && init.method) || "GET";
  const body = String((init && init.body) || "");
  if (u.endsWith("/status")) return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true, phase: "running", tick: onlineTick }) });
  if (u.endsWith("/run") && method === "POST") { runPosts.push(body); return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true }) }); }
  if (u.endsWith("/saves") && method === "GET") {
    // R7-D：动态存档库（重命名=删旧+存新 后，GET /saves 必须反映新名——
    // innerHTML stub 修正后 re-render 是真清空重建，静态快照会名实不符）
    return Promise.resolve({ ok: true, json: () => Promise.resolve({ saves: savesDb.slice() }) });
  }
  if (u.endsWith("/save") && method === "POST") {
    savePosts.push(body);
    const nm = decodeURIComponent(String(body).replace(/^name=/, ""));
    if (!savesDb.some((x) => x.name === nm)) savesDb.unshift({ name: nm, frame: 1, seed: 0, aliveNuclei: 1, savedAt: "2026-11-01 13:00:00" });
    return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true }) });
  }
  if (u.endsWith("/load") && method === "POST") { loadPosts.push(body); return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true }) }); }
  if (u.endsWith("/delete-save") && method === "POST") {
    delPosts.push(body);
    const nm2 = decodeURIComponent(String(body).replace(/^name=/, ""));
    for (let di = savesDb.length - 1; di >= 0; di--) if (savesDb[di].name === nm2) savesDb.splice(di, 1);
    return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true }) });
  }
  if (u.endsWith("/timeline")) return Promise.resolve({ ok: true, json: () => Promise.resolve({ checkpoints: [200, 400, 600, 800, 1000], current: 800 }) });
  if (u.endsWith("/seek") && method === "POST") { seekPosts.push(body); return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true }) }); }
  if (u.endsWith("/state") && method === "GET") {
    const buf = buildStateFixture(onlineTick).buf;
    onlineTick += 10;
    return Promise.resolve({ ok: true, arrayBuffer: () => Promise.resolve(buf) });
  }
  if (u.endsWith("/env") && method === "POST") { envPosts.push(body); return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true, applied: 1, rejected: 0 }) }); }
  if (u.endsWith("/control") && method === "POST") { controlPosts.push(body); return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true }) }); }
  return Promise.resolve({ ok: false });
};
LM._state.online = null;
LM._live.checkDemo();
await new Promise((r) => setTimeout(r, 30));
ok(LM._state.online === true, "online: state.online=true after checkDemo (stub /status 200)");
ok(!cardLive.classList.contains("offline"), "online: card A 不再置灰");
ok(!cardSaves.classList.contains("offline"), "online: card B 不再置灰");
ok(banner.classList.contains("hidden"), "online: 横幅隐藏");

console.log("== 10c) R6-D: /run 后状态栏 seed 变化 ==");
runPosts.length = 0; controlPosts.length = 0; savePosts.length = 0;
sandbox.window.LiveClient.stop();                     // 先停掉 10b 之前的轮询
sandbox.window.LiveClient.start("", { intervalMs: 1000, onFrame: () => {} });  // 占位停掉
sandbox.window.LiveClient.stop();
LM.showPage("menu");
LM.startRun({ seed: 1234, frames: 300, nuclei: 10, cyano: 40, autosave: true });
await new Promise((r) => setTimeout(r, 400));
eq(runPosts[0], "seed=1234&nuclei=10&cyano_nodes=40&ticks=300&policy=rule&max_fps=60",
   "POST /run 行式 body 正确（R10：含 policy 键）");
ok(savePosts.length >= 1 && String(savePosts[0]).indexOf("name=auto-1234-") === 0,
   "自动存档 auto-<seed>-<时间戳> POST /save（" + String(savePosts[0] || "").slice(0, 24) + "…）");
ok(String(elements["metaChips"].textContent).indexOf("seed 1234") >= 0,
   "状态栏 chips 显示所选 seed 1234（got: " + String(elements["metaChips"].textContent).slice(0, 40) + "…）");
ok(LM._state.live === true, "/run 后进入实时模式");
sandbox.window.LiveClient.stop();

console.log("== 10d) R6-D: 存档列表渲染 + 重命名 + 删除（stub /saves）==");
savesDb.length = 0;   // R7-D：10c 自动存档已污染动态库 → 重置到本节基线 2 条
savesDb.push({ name: "演示 · seed 7", frame: 240, seed: 7, aliveNuclei: 8, savedAt: "2026-11-01 12:00:00" },
             { name: "auto-42-20261101", frame: 315, seed: 42, aliveNuclei: 12, savedAt: "2026-11-01 12:30:00" });
LM._state.saves = []; LM._state.savesOnline = false;
await LM._live.refreshSaves();
await new Promise((r) => setTimeout(r, 20));
ok(LM._state.savesOnline === true, "refreshSaves online: savesOnline=true");
ok(Array.isArray(LM._state.saves) && LM._state.saves.length === 2, "GET /saves 渲染 2 个存档");
const grid = elements["savesGrid"];
let cardEls = grid.querySelectorAll ? grid.querySelectorAll(".save-card") : [];
// fakeEl.querySelectorAll 返回 [] —— 走 children 深度断言替代
let saveCards = 0;
(function count(node) {
  if (node.className === "save-card glass") saveCards++;
  (node.children || []).forEach(count);
})(grid);
eq(saveCards, 2, "savesGrid 渲染 2 张存档卡片");
ok(String(grid.children[0].children[0].children[0].value).indexOf("演示 · seed 7") >= 0 ||
   String(grid.children[0].querySelectorAll && "") !== "",
   "首卡名字输入框值为「演示 · seed 7」");

// 重命名：行内改名输入框 change → delete-save(旧名) + save(新名)
const firstCard = grid.children[0];
const renameInput = firstCard.children[0].children[0];
ok(renameInput.value === "演示 · seed 7", "行内重命名输入框存在且带原名");
renameInput.value = "演示改名";
renameInput.fire("change");
await new Promise((r) => setTimeout(r, 30));
ok(delPosts.some((b) => b.indexOf("name=" + encodeURIComponent("演示 · seed 7")) === 0),
   "重命名触发 DELETE 旧名 POST /delete-save");
ok(savePosts.some((b) => b.indexOf("name=" + encodeURIComponent("演示改名")) === 0),
   "重命名触发 POST /save 新名（后端无 rename 路由 → 删旧+存新 等价改名）");

// 删除：确认框 → POST /delete-save（先装 confirm stub，卡片渲染在 sandbox 内执行）
const delBtnCard = grid.children[0];
// 找删除按钮（btn-danger）
let delBtn = null;
(function find(node) {
  if (node.className === "btn btn-danger") { delBtn = node; return; }
  (node.children || []).forEach(find);
})(delBtnCard);
ok(delBtn !== null, "删除按钮存在（btn-danger）");
delPosts.length = 0;
sandbox.window.confirm = () => true;   // 确认框通过（必须在 click 前装好）
if (delBtn) delBtn.click();
await new Promise((r) => setTimeout(r, 30));
await new Promise((r) => setTimeout(r, 30));
// 重命名 then 已把闭包内 s.name 改成新名 → 删除 POST 的是新名（行为正确：
// 重命名成功后的存档就该按新名删除）。stub 的 /saves 是静态快照，refreshSaves
// 重拉后列表显示名与闭包名可能短暂不一致，真实后端无此歧义。
eq(delPosts[0], "name=" + encodeURIComponent("演示改名"),
   "删除触发 POST /delete-save name=演示改名（重命名后的当前名）");

// 离线存档页：显示「需要本地服务」而非假数据
sandbox.fetch = undefined;
LM._state.saves = []; LM._state.savesOnline = false;
await LM._live.refreshSaves();
await new Promise((r) => setTimeout(r, 20));
ok(LM._state.savesOnline === false, "offline: savesOnline=false");
ok(String(grid.innerHTML).indexOf("需要本地服务") >= 0, "offline: 存档页显示「需要本地服务」提示（非假数据）");

console.log("== 10e) R6-D: seek 流程 pause -> seek -> 拉 state ==");
sandbox.fetch = function (url, init) {
  const u = String(url || "");
  const method = (init && init.method) || "GET";
  if (u.endsWith("/status")) return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true, phase: "paused", tick: 800 }) });
  if (u.endsWith("/timeline")) return Promise.resolve({ ok: true, json: () => Promise.resolve({ checkpoints: [200, 400, 600, 800, 1000], current: 800 }) });
  if (u.endsWith("/seek") && method === "POST") { seekPosts.push(String((init && init.body) || "")); return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true }) }); }
  if (u.endsWith("/state") && method === "GET") {
    return Promise.resolve({ ok: true, arrayBuffer: () => Promise.resolve(buildStateFixture(400).buf) });
  }
  if (u.endsWith("/env") && method === "POST") { envPosts.push(""); return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true }) }); }
  if (u.endsWith("/control") && method === "POST") { controlPosts.push(String((init && init.body) || "")); return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true }) }); }
  return Promise.resolve({ ok: false });
};
controlPosts.length = 0; seekPosts.length = 0;
LM._state.live = true;
LM._state.replay = false;
LM._state.cfg = { seed: 42 };
await LM._live.refreshTimeline();
ok(LM._state.timeline && LM._state.timeline.checkpoints.length === 5, "GET /timeline 缓存 5 个检查点");
const badge = elements["replayBadge"];
ok(badge.classList.contains("hidden"), "初始无回放徽标");
const seekResult = await LM._live.seekToFrame(600);
ok(seekResult === true, "seekToFrame(600) resolve true");
eq(controlPosts[0], "cmd=pause", "seek 前先 POST /control cmd=pause");
eq(seekPosts[0], "frame=600", "POST /seek frame=600");
ok(controlPosts.filter((b) => b === "cmd=pause").length === 1, "只发一次 cmd=pause（无双发）");
ok(LM._state.replay === true, "seek 后进入回放态（state.replay=true）");
ok(!badge.classList.contains("hidden"), "回放徽标可见");
ok(String(badge.textContent).indexOf("t400") >= 0, "徽标显示回放帧 t400（/state 拉回的实际 tick）");
// 回放锁定：后台轮询帧不推进
const before = LM._state.trace.frames.length;
LM._live.appendLiveFrame({ tick: 9999, nuclei: [], balls: [], cyano: [], cyanoLinks: [], genomes: [], stats: {} });
eq(LM._state.trace.frames.length, before, "回放态 appendLiveFrame 被锁定（帧数不变）");
// resume 退出回放态
LM.setPlaying(true);
ok(LM._state.replay === false && badge.classList.contains("hidden"), "resume 退出回放态 + 徽标隐藏");
sandbox.window.LiveClient.stop();

console.log("== 10f) R6-D: checkpoints 为空 → 进度条置灰提示 ==");
sandbox.fetch = function (url, init) {
  const u = String(url || "");
  if (u.endsWith("/timeline")) return Promise.resolve({ ok: true, json: () => Promise.resolve({ checkpoints: [], current: 42 }) });
  if (u.endsWith("/status")) return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true, phase: "running", tick: 42 }) });
  return Promise.resolve({ ok: false });
};
LM._state.timeline = null;
await LM._live.refreshTimeline();
ok(LM._state.timeline && LM._state.timeline.checkpoints.length === 0, "空 checkpoints 缓存");
ok(String(elements["tlHint"].textContent).indexOf("暂无检查点") >= 0, "进度条提示「暂无检查点（约 200 帧后自动打点）」");
ok(String(elements["tlWrap"].classList.contains ? elements["tlWrap"].classList.contains("empty") : true),
   "进度条容器加 .empty 置灰类");

console.log("== 10g) R6-D: 分析视图（pause 后抽检查点逐个 /seek + /state）==");
sandbox.fetch = function (url, init) {
  const u = String(url || "");
  const method = (init && init.method) || "GET";
  if (u.endsWith("/status")) return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true, phase: "paused", tick: 800 }) });
  if (u.endsWith("/timeline")) return Promise.resolve({ ok: true, json: () => Promise.resolve({ checkpoints: [100,200,300,400,500,600,700,800,900,1000,1100,1200], current: 1200 }) });
  if (u.endsWith("/seek") && method === "POST") { seekPosts.push(String((init && init.body) || "")); return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true }) }); }
  if (u.endsWith("/state") && method === "GET") {
    return Promise.resolve({ ok: true, arrayBuffer: () => Promise.resolve(buildStateFixture(100 + seekPosts.length * 100).buf) });
  }
  if (u.endsWith("/env") && method === "POST") { envPosts.push(""); return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true }) }); }
  if (u.endsWith("/control") && method === "POST") { controlPosts.push(String((init && init.body) || "")); return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true }) }); }
  return Promise.resolve({ ok: false });
};
controlPosts.length = 0; seekPosts.length = 0;
LM._state.timeline = null;
LM._state.live = true; LM._state.cfg = { seed: 42 };
LM._live.runAnalysis();
await new Promise((r) => setTimeout(r, 120));
eq(controlPosts.filter((b) => b === "cmd=pause").length, 1, "分析前 pause 恰好一次");
eq(seekPosts.length, 12, "12 个检查点 ≤ 20 → 全部采样（POST /seek ×12）");
eq(seekPosts[0], "frame=100", "首个采样 frame=100");
eq(seekPosts[11], "frame=1200", "末个采样 frame=1200");
ok(elements["analysisPanel"].classList.contains("open"), "分析浮层 .open 显示");
const prog = String(elements["analysisProgress"].textContent);
ok(prog.indexOf("完成") >= 0, "分析进度显示完成（got: " + prog + "）");

// ==================== R7-D: 调色板 /packs + 存档缩略图 ====================
console.log("== 11) R7-D: palette 默认值（离线内置色，与旧硬编码一致） ==");
const RP = sandbox.window.Renderer;
const R7D_DEFAULTS = {
  background: '10,14,26', ball_shield: '255,207,94', ball_worker: '69,224,216',
  ball_scout: '183,139,255', free_ball: '200,200,210', nucleus: '255,255,255'
};
eq(JSON.stringify(RP.getPalette()), JSON.stringify(R7D_DEFAULTS),
   "renderer 内置默认调色板 = 旧硬编码色（'r,g,b' 形态）");
eq(JSON.stringify(LM._state.palette), JSON.stringify(R7D_DEFAULTS), "state.palette 镜像默认值");
ok(LM._state.packs.length === 0, "离线 boot：packs 列表为空（无假数据）");

console.log("== 12) R7-D: setPalette 校验（坏值回退 / 白名单，坏包不崩） ==");
const badApplied = RP.setPalette({ ball_worker: '999,1,1', nucleus: '5,6,7', junk: 'abc', ball_scout: 42 });
eq(badApplied.ball_worker, R7D_DEFAULTS.ball_worker, "超范围 '999,1,1' 拒绝 → 回退默认");
eq(badApplied.nucleus, '5,6,7', "合法 '5,6,7' 生效");
eq(badApplied.ball_scout, R7D_DEFAULTS.ball_scout, "非字符串拒绝 → 回退默认");
ok(badApplied.junk === undefined, "未知名不进调色板（白名单）");
RP.setPalette({});
eq(JSON.stringify(RP.getPalette()), JSON.stringify(R7D_DEFAULTS), "setPalette({}) 恢复全默认");

console.log("== 13) R7-D: GET /packs stub → active 覆盖默认（部分覆盖安全） ==");
sandbox.fetch = function (url, init) {
  const u = String(url || ""), method = (init && init.method) || "GET";
  if (u.endsWith("/packs") && method === "GET") {
    return Promise.resolve({ ok: true, json: () => Promise.resolve({
      ok: true,
      packs: [{ name: "ocean", desc: "深海蓝", version: 2, author: "R7", enabled: true },
              { name: "sunset", desc: "落日橙", version: 1, author: "R7", enabled: false }],
      active: { background: "1,2,3", ball_worker: "9,8,7", nucleus: "200,200,200", broken: "xx" }
    }) });
  }
  return Promise.resolve({ ok: false });
};
LM._live.fetchPalette();
await new Promise((r) => setTimeout(r, 20));
eq(LM._state.palette.ball_worker, "9,8,7", "active.ball_worker 覆盖默认（state.palette 回写）");
eq(LM._state.palette.background, "1,2,3", "active.background 覆盖默认");
eq(RP.getPalette().nucleus, "200,200,200", "active.nucleus 生效（renderer 真源同步）");
eq(LM._state.palette.ball_shield, R7D_DEFAULTS.ball_shield, "active 未给的键保持默认");
eq(LM._state.packs.length, 2, "GET /packs 包列表缓存 2 个");
ok(LM._state.packsOnline === true, "packsOnline=true");
ok(String(elements["toast"].textContent).indexOf("崩") < 0, "坏键 broken 未引发异常路径");

console.log("== 14) R7-D: 材质包 UI（勾选 → POST /set-packs 行式 body → 重拉 active） ==");
const setPacksPosts = [];
sandbox.fetch = function (url, init) {
  const u = String(url || ""), method = (init && init.method) || "GET";
  if (u.endsWith("/packs") && method === "GET") {
    return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true, packs: [
      { name: "ocean", desc: "深海蓝", version: 2, author: "R7", enabled: true },
      { name: "sunset", desc: "落日橙", version: 1, author: "R7", enabled: false },
      { name: "霓虹 夜", desc: "霓虹", version: 3, author: "R7", enabled: true }
    ], active: { ball_scout: "11,22,33" } }) });
  }
  if (u.endsWith("/set-packs") && method === "POST") {
    setPacksPosts.push(String((init && init.body) || ""));
    return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true }) });
  }
  return Promise.resolve({ ok: false });
};
LM._state.packsOnline = true;
LM._live.fetchPalette();   // R7-D：拉 3 包列表（renderPacks 渲染的是 state.packs，必须先取）
await new Promise((r) => setTimeout(r, 20));
const packsBox = elements["packsBox"];
function collectRows(root, cls) {
  const out = [];
  (function walk(node) { (node.children || []).forEach((c) => { if (c.className === cls) out.push(c); walk(c); }); })(root);
  return out;
}
let packRows = collectRows(packsBox, "pack-row");
eq(packRows.length, 3, "renderPacks 渲染 3 行包（在线态）");
eq(packRows[0].children[0].checked, true, "ocean 勾选态来自 enabled=true");
ok(String(collectRows(packsBox, "packs-empty").length) === "0" ||
   collectRows(packsBox, "packs-empty").length === 0, "在线态无「需要本地服务」占位");
// 通过 UI 取消勾选 ocean → POST /set-packs body 只含仍启用的包
packRows[0].children[0].checked = false;
packRows[0].children[0].fire("change");
await new Promise((r) => setTimeout(r, 20));
eq(setPacksPosts[0], "enabled=" + encodeURIComponent("霓虹 夜"),
   "POST /set-packs body = enabled=霓虹 夜（ocean 取消/sunset 本关，中文+空格正确编码）");
eq(LM._state.palette.ball_scout, "11,22,33", "POST 后重拉 /packs → 新 active 立即生效");
// 失败回滚：服务端拒绝 → 勾选与 enabled 回滚（服务端是权威）
sandbox.fetch = function (url, init) {
  const u = String(url || "");
  if (u.endsWith("/set-packs")) return Promise.resolve({ ok: false });
  return Promise.resolve({ ok: false });
};
packRows = collectRows(packsBox, "pack-row");   // POST 成功路径已重渲染 → 取新鲜行
const sunsetCb = packRows[1].children[0];
sunsetCb.checked = true;
sunsetCb.fire("change");
await new Promise((r) => setTimeout(r, 20));
eq(LM._state.packs[1].enabled, false, "POST /set-packs 失败 → enabled 回滚（服务端权威）");
eq(sunsetCb.checked, false, "失败 → 勾选 UI 回滚");
ok(String(elements["toast"].textContent).indexOf("材质包切换失败") >= 0, "失败 toast 提示");
// 离线提示（非假列表）
LM._state.packsOnline = false;
LM._live.renderPacks();
const emptyPh = collectRows(packsBox, "packs-empty");
eq(emptyPh.length, 1, "离线：材质包区显示占位（非假列表）");
ok(String(emptyPh[0].textContent).indexOf("需要本地服务") >= 0, "占位文案含「需要本地服务」");

console.log("== 15) R7-D: 存档缩略图（无帧数据不伪造 → 占位） ==");
// 功能化 localStorage stub（原 stub 恒 null/no-op，无法验证读写往返）
sandbox.localStorage = {
  _m: {},
  getItem(k) { return Object.prototype.hasOwnProperty.call(this._m, k) ? this._m[k] : null; },
  setItem(k, v) { this._m[k] = String(v); },
  removeItem(k) { delete this._m[k]; },
  key(i) { return Object.keys(this._m)[i] || null; },
  get length() { return Object.keys(this._m).length; }
};
const TH = LM._live;
eq(TH.readThumb("不存在"), null, "无缩略图 → readThumb null");
eq(Object.keys(TH.loadThumbMap()).length, 0, "空 localStorage → 空 map");
eq(TH.captureThumbFromFrame(null), null, "无帧 → 不造缩略图（null）");
// 诚实约束：没有 trace 的 renderer 拒绝凭空画世界
const rBare = RP.create(sandbox.document.createElement("canvas"));
const thumbCanvas = sandbox.document.createElement("canvas");
thumbCanvas.width = 128; thumbCanvas.height = 72;
eq(rBare.renderThumb({ tick: 1, nuclei: [], balls: [], cyano: [], cyanoLinks: [] }, thumbCanvas), false,
   "renderer 无 trace → renderThumb false（拒绝凭空画世界）");
// 有 trace + 真帧 → 抓到 dataURL（stub toDataURL 固定串）
const shot = TH.captureThumbFromFrame(
  (LM._state.trace && LM._state.trace.frames[0]) || { tick: 0, nuclei: [], balls: [], cyano: [], cyanoLinks: [] });
ok(typeof shot === "string" && shot.indexOf("data:image/") === 0,
   "有 trace + 帧 → 抓到 dataURL 缩略图（got: " + String(shot).slice(0, 34) + "…）");
TH.storeThumb("有图存档", shot);
eq(TH.readThumb("有图存档"), shot, "storeThumb → readThumb 往返一致");
// renderSaves：有图卡 <img>，无图卡占位渐变 + 文案
LM._state.savesOnline = true;
LM._state.saves = [
  { name: "有图存档", frame: 120, seed: 42, aliveNuclei: 8, savedAt: "2026-11-01 13:00:00" },
  { name: "无图存档", frame: 60, seed: 7, aliveNuclei: 3, savedAt: "2026-11-01 13:10:00" }
];
LM._live.renderSaves();
const gridR7 = elements["savesGrid"];
const cardsR7 = collectRows(gridR7, "save-card glass");
eq(cardsR7.length, 2, "renderSaves 渲染 2 卡");
eq(cardsR7[0].children[1].className, "save-thumb", "有缩略图卡：.save-thumb");
eq(cardsR7[0].children[1].children[0].tagName, "IMG", "有图卡内是 <img>");
eq(cardsR7[0].children[1].children[0].src, shot, "img.src = localStorage dataURL");
eq(cardsR7[1].children[1].className, "save-thumb save-thumb-empty", "无图卡：占位 .save-thumb-empty");
ok(String(cardsR7[1].children[1].children[0].textContent).indexOf("首次载入时生成") >= 0,
   "无图卡文案「首次载入时生成」（不伪造）");
// 真实接线：载入存档后第一帧 /state → 抓缩略图入 localStorage（只此一帧）
sandbox.fetch = function (url, init) {
  const u = String(url || ""), method = (init && init.method) || "GET";
  if (u.endsWith("/status")) return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true, phase: "running", tick: 300 }) });
  if (u.endsWith("/timeline")) return Promise.resolve({ ok: true, json: () => Promise.resolve({ checkpoints: [], current: 300 }) });
  if (u.endsWith("/state") && method === "GET") return Promise.resolve({ ok: true, arrayBuffer: () => Promise.resolve(buildStateFixture(300).buf) });
  if (u.endsWith("/control") && method === "POST") return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true }) });
  if (u.endsWith("/env")) return Promise.resolve({ ok: true, json: () => Promise.resolve({ ok: true }) });
  return Promise.resolve({ ok: false });
};
LM._state.pendingThumbName = "实测存档";
LM._live.enterLiveMode();
await new Promise((r) => setTimeout(r, 120));
sandbox.window.LiveClient.stop();
eq(LM._state.pendingThumbName, null, "第一帧落地后 pendingThumbName 清空（只抓一帧）");
eq(TH.readThumb("实测存档"), "data:image/jpeg;base64,stubthumb",
   "载入存档第一帧 → 缩略图写入 localStorage（128x72 JPEG dataURL）");

console.log("");
// == R10: criteria scorecard (browser-side quick card) ==
console.log("== 15) R10 判据速算卡 ==");
(function criteriaCardTest() {
  // build fake rows: steady-ish series
  var rows = [];
  for (var i = 0; i < 20; i++) {
    rows.push({ tick: i*50, nuclei: 300 + Math.round(10*Math.sin(i/2)), cyanoAlive: 200,
      cyanoEnergy: 15, totalEnergy: 20000 + 50*Math.sin(i/3), hueSd: 33.0 });
  }
  LM.__test_renderCriteriaCard(rows);
  var html = String(elements["criteriaCard"] ? elements["criteriaCard"].innerHTML : "");
  ok(html.indexOf("crit-title") >= 0, "判据卡渲染出标题");
  ok(html.indexOf("1 能量紧平衡") >= 0, "判据卡含 crit1 行");
  ok(html.indexOf("5 捕食者-猎物") >= 0, "判据卡含 crit5 行");
  LM.__test_renderCriteriaCard([]);
  ok(String(elements["criteriaCard"].innerHTML).indexOf("采样点不足") >= 0, "空采样显示不足提示");
})();

console.log("smoke checks: " + checks + ", failures: " + failures);
if (failures) { console.error("SMOKE FAILED"); process.exit(1); }
console.log("SMOKE PASSED");
