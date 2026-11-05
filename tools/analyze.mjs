#!/usr/bin/env node
// analyze.mjs — 生态判据分析器（任务卡 R2-D）
//
// 用法:
//   node tools/analyze.mjs <trace.json> [--out report.md] [--json [path]]
//                          [--baseline <trace2.json>] [--genome <genome.json>]
//                          [--out-genome <path>] [--help] [--version]
//
// 输入: agentsim --trace 导出的 JSON（docs/TRACE-CONTRACT.md；v2/v3 都能读，主路径 v3，
//        v2 差异: series 多一个 food 键、帧内 c 元组 5 项、无 g 键——都按"缺则跳过"容错）。
// 优雅降级: 本轮后端还没有 genome 导出，trace 里没有 genome 字段时，
// 判据 3/4/6 输出 SKIP（不是 FAIL），并在报告里明确写"需要主代理接线 --trace-genome"。
//
// 判据定义: docs/GOAL-ECOLOGY.md §一（6 条硬判据）。
// 依赖: 零。只用 node 内置模块 + 同目录 svg.mjs（也是零依赖）。
//
// 退出码: 0 = 无 FAIL（SKIP 不影响）; 1 = 至少一条判据 FAIL; 2 = 用法/IO/解析错误。
//
// genome 数据两种来源（按优先级）:
//   1. 显式 --genome <file> 或 trace 同名旁车文件 <trace>.genome.json
//      （格式: {schemaVersion:1, genomeTrace:true, samples:[{tick, genomes:[{hue, params:[24]}]}]}）
//   2. trace frames 内嵌 genome 键（v3 契约主路径，候选键名 'g' / 'genomes' / 'genome'）：
//      行契约 = 26 个数 [id, p0..p23, hue]（hue 在最后）；
//      p 顺序 = src/core/Genome.h 字段声明序：
//        affinity0..2, orbitRadius0..2, absorbPreference0..2, repelStrength0..2,
//        maxSpeed, maxAccel, energyThreshold, mutationRate, cyanoPreference, cyanoDigestion,
//        nucleusSightRadius, nucleusPersonalSpace, nucleusRepelStrength, nucleusCohesion,
//        forceRange, forceCostK
//      兼容形态: [hue, p0..p23]（25 项，早期草案）与对象 {id?, hue, params:[24]}。
//   内嵌 genome 出现时自动写旁车文件（--out-genome 可指定路径），供二次分析免重跑。

import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';

import {
  multiLineChart, singleLineChart, scatterChart, sparkline, hsl, fmt, esc,
} from './svg.mjs';

const VERSION = '0.2.0';
const EXIT_OK = 0;
const EXIT_FAIL = 1;
const EXIT_USAGE = 2;

// ---------------------------------------------------------------------------
// 基因组契约（顺序 = src/core/Genome.h 字段声明序，不得重排）
// ---------------------------------------------------------------------------
const GENE_NAMES = [
  'affinity_resource', 'affinity_scout', 'affinity_shield',
  'orbit_radius_resource', 'orbit_radius_scout', 'orbit_radius_shield',
  'absorb_preference_resource', 'absorb_preference_scout', 'absorb_preference_shield',
  'repel_strength_resource', 'repel_strength_scout', 'repel_strength_shield',
  'max_speed', 'max_accel', 'energy_threshold', 'mutation_rate',
  'cyano_preference', 'cyano_digestion',
  'nucleus_sight_radius', 'nucleus_personal_space',
  'nucleus_repel_strength', 'nucleus_cohesion',
  'force_range', 'force_cost_k',
];
const IDX = {
  maxSpeed: 12, maxAccel: 13, energyThreshold: 14, mutationRate: 15,
  cyanoPreference: 16, cyanoDigestion: 17,
};
const GENOME_KEYS = ['g', 'genomes', 'genome']; // 内嵌 genome 候选键名
const CRITERIA_COUNT = 6;
const KMEANS_K = 3;

// ---------------------------------------------------------------------------
// 统计工具（全部是总体口径 population 口径）
// ---------------------------------------------------------------------------
function mean(a) {
  if (!a || a.length === 0) return NaN;
  let s = 0;
  for (const v of a) s += v;
  return s / a.length;
}

function pstd(a) {
  const n = a ? a.length : 0;
  if (n < 2) return n === 1 ? 0 : NaN;
  const m = mean(a);
  let s = 0;
  for (const v of a) s += (v - m) * (v - m);
  return Math.sqrt(s / n);
}

function pearson(a, b) {
  const n = Math.min(a.length, b.length);
  if (n < 2) return NaN;
  const ma = mean(a), mb = mean(b);
  let sab = 0, sa = 0, sb = 0;
  for (let i = 0; i < n; i++) {
    const da = a[i] - ma, db = b[i] - mb;
    sab += da * db; sa += da * da; sb += db * db;
  }
  if (sa <= 0 || sb <= 0) return NaN;
  return sab / Math.sqrt(sa * sb);
}

// 最小二乘斜率（x = 0..n-1，单位 = 每 tick）
function linregSlope(ys) {
  const n = ys.length;
  if (n < 2) return NaN;
  const mx = (n - 1) / 2;
  const my = mean(ys);
  let num = 0, den = 0;
  for (let i = 0; i < n; i++) {
    num += (i - mx) * (ys[i] - my);
    den += (i - mx) * (i - mx);
  }
  if (den <= 0) return NaN;
  return num / den;
}

// 圆统计: hue 的圆标准差（度）。R = |mean(e^{iθ})|，circStd = sqrt(-2 ln R)（弧度）。
function circularStdDeg(anglesDeg) {
  const n = anglesDeg.length;
  if (n < 2) return 0;
  let sc = 0, ss = 0;
  for (const d of anglesDeg) {
    const r = (d * Math.PI) / 180;
    sc += Math.cos(r); ss += Math.sin(r);
  }
  const R = Math.sqrt(sc * sc + ss * ss) / n;
  const Rc = Math.min(1, Math.max(1e-12, R));
  return Math.sqrt(-2 * Math.log(Rc)) * (180 / Math.PI);
}

// 互相关: c[lag] = corr(a[0..n-lag-1], b[lag..n-1])。lag=0 即普通皮尔逊。
function crossCorr(a, b, maxLag) {
  const n = Math.min(a.length, b.length);
  const out = [];
  for (let lag = 0; lag <= maxLag && lag < n; lag++) {
    const la = a.slice(0, n - lag);
    const lb = b.slice(lag, n);
    out.push(pearson(la, lb));
  }
  return out;
}

// 在 corr 序列里找第一个符号过零（检查区间 [fromLag, 序列末尾)）
function findZeroCrossing(cs, fromLag) {
  const sign = (v) => (v > 0 ? 1 : v < 0 ? -1 : 0);
  for (let i = Math.max(1, fromLag); i < cs.length; i++) {
    const s0 = sign(cs[i - 1]), s1 = sign(cs[i]);
    if ((s0 !== 0 && s1 !== 0 && s0 !== s1) || (s0 !== 0 && s1 === 0)) return i;
  }
  return null;
}

// 末 20% 窗口（至少 1 个元素）
function tailSlice(a) {
  const n = a.length;
  const start = Math.min(n - 1, Math.floor(n * 0.8));
  return a.slice(start);
}

function cvOf(a) {
  const m = mean(a);
  if (!Number.isFinite(m) || Math.abs(m) < 1e-12) return NaN;
  return pstd(a) / m;
}

function pctStr(x, digits = 2) {
  if (!Number.isFinite(x)) return '—';
  return (x * 100).toFixed(digits) + '%';
}

// ---------------------------------------------------------------------------
// CLI
// ---------------------------------------------------------------------------
function usageError(msg) {
  process.stderr.write('analyze: error: ' + msg + '\n');
  process.stderr.write('运行 node tools/analyze.mjs --help 查看用法。\n');
  process.exit(EXIT_USAGE);
}

function printHelp() {
  const lines = [
    'analyze.mjs v' + VERSION + ' — agentsim 生态判据分析器（任务卡 R2-D）',
    '',
    '用法:',
    '  node tools/analyze.mjs <trace.json> [options]',
    '',
    '参数:',
    '  trace.json                 agentsim --trace 导出的 JSON（schemaVersion 2/3）',
    '  --out <report.md>          输出 markdown 报告路径（缺省: 不写文件，只打印摘要）',
    '  --json [path]              同时输出机器可读 JSON（无 path 时打印到 stdout）',
    '  --baseline <trace2.json>   判据 6 用: 提供对照运行的 trace（如 light 轴调满 vs 默认）',
    '  --genome <genome.json>     显式指定 genome 旁车文件（缺省尝试 <trace>.genome.json',
    '                             再尝试 trace frames 内嵌 g/genomes/genome 键）',
    '  --out-genome <path>        内嵌 genome 存在时，旁车文件写出路径（缺省自动命名）',
    '  --help                     本帮助',
    '  --version                  版本号',
    '',
    '6 条判据（定义见 docs/GOAL-ECOLOGY.md §一）:',
    '  1 能量紧平衡   |slope(totalEnergy)| * ticks / mean(total) < 0.15',
    '  2 种群波动     末 20% tick 核数 CV ∈ [0.05, 0.6] 且末态核数 > 0',
    '  3 基因多样性   24 参数中 >= 18 个末态 std > 初态 std * 0.3        [需 genome]',
    '  4 hue 发散     hue 圆标准差 > 30 度                                [需 genome]',
    '  5 捕食者-猎物  corr(nuclei, cyano) < -0.2 或互相关滞后 1..200 过零',
    '  6 调控有效     对比 baseline: cyanoPreference / maxSpeed 变化 > 10%  [需 genome+baseline]',
    '',
    '降级规则: trace 没有 genome 数据时判据 3/4/6 为 SKIP（不是 FAIL）。',
    '退出码: 0 = 无 FAIL；1 = 有 FAIL；2 = 用法/IO/解析错误。',
  ];
  process.stdout.write(lines.join('\n') + '\n');
}

function parseArgs(argv) {
  const opts = {
    trace: null, out: null, jsonPath: null, jsonStdout: false,
    baseline: null, genome: null, outGenome: null, help: false, version: false,
  };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    const takesValue = () => i + 1 < argv.length && !argv[i + 1].startsWith('-');
    const value = () => argv[++i];
    if (a === '--help' || a === '-h') { opts.help = true; continue; }
    if (a === '--version') { opts.version = true; continue; }
    if (a === '--out') { opts.out = takesValue() ? value() : usageError('--out 缺少路径参数'); continue; }
    if (a === '--json') {
      opts.jsonStdout = true;
      if (takesValue()) opts.jsonPath = value();
      continue;
    }
    if (a === '--baseline') { opts.baseline = takesValue() ? value() : usageError('--baseline 缺少路径参数'); continue; }
    if (a === '--genome') { opts.genome = takesValue() ? value() : usageError('--genome 缺少路径参数'); continue; }
    if (a === '--out-genome') { opts.outGenome = takesValue() ? value() : usageError('--out-genome 缺少路径参数'); continue; }
    if (a.startsWith('--')) usageError('未知选项: ' + a);
    if (opts.trace) usageError('多余的位置参数: ' + a + '（trace 文件只能有一个）');
    opts.trace = a;
  }
  return opts;
}

// ---------------------------------------------------------------------------
// trace 加载与 genome 提取
// ---------------------------------------------------------------------------
function loadTrace(p, label) {
  if (!p) usageError('缺少 ' + (label || 'trace') + ' 文件参数');
  let raw;
  try {
    raw = fs.readFileSync(p, 'utf8');
  } catch (e) {
    usageError('无法读取 ' + (label || 'trace') + ' 文件: ' + p + ' (' + (e.code || e.message) + ')');
  }
  let t;
  try {
    t = JSON.parse(raw);
  } catch (e) {
    usageError((label || 'trace') + ' 不是合法 JSON: ' + e.message);
  }
  if (!t || typeof t !== 'object' || Array.isArray(t)) {
    usageError((label || 'trace') + ' 顶层结构不是 JSON 对象');
  }
  if (t.schemaVersion !== 2 && t.schemaVersion !== 3) {
    process.stderr.write('analyze: warning: 期望 schemaVersion 2 或 3，实际 ' +
      JSON.stringify(t.schemaVersion) + '；继续解析，缺失字段对应判据会 SKIP\n');
  }
  if (!Array.isArray(t.series) || t.series.length === 0) {
    usageError((label || 'trace') + ' 缺少 series 数据（需要至少 1 行）');
  }
  return t;
}

// 解析一行 genome（三种形态，见文件头注释）
function parseGenomeRow(row) {
  if (Array.isArray(row)) {
    if (row.length === 26) {
      // v3 契约主路径: [id, p0..p23, hue] —— hue 在最后。
      if (!row.every(Number.isFinite)) return null;
      return { id: row[0], hue: row[25], params: row.slice(1, 25) };
    }
    if (row.length === 25) {
      // 早期草案兼容: [hue, p0..p23]。
      if (!row.every(Number.isFinite)) return null;
      return { id: null, hue: row[0], params: row.slice(1) };
    }
    return null;
  }
  if (row && typeof row === 'object') {
    if (!Number.isFinite(row.hue)) return null;
    if (Array.isArray(row.params) && row.params.length === 24 &&
        row.params.every(Number.isFinite)) {
      return { id: Number.isFinite(row.id) ? row.id : null, hue: row.hue, params: row.params.slice() };
    }
    const params = new Array(24).fill(NaN);
    for (let i = 0; i < 24; i++) {
      const v = row[GENE_NAMES[i]];
      if (!Number.isFinite(v)) return null;
      params[i] = v;
    }
    return { id: Number.isFinite(row.id) ? row.id : null, hue: row.hue, params };
  }
  return null;
}

// 从 trace.frames 内嵌键提取 genome 样本
function extractGenomeSamples(trace) {
  const samples = [];
  let usedKey = null;
  for (const fr of (trace.frames ?? [])) {
    if (!fr || typeof fr !== 'object') continue;
    const key = GENOME_KEYS.find((k) => fr[k] != null);
    if (!key) continue;
    const rows = Array.isArray(fr[key]) ? fr[key] : [fr[key]];
    const gs = [];
    for (const row of rows) {
      const g = parseGenomeRow(row);
      if (g) gs.push(g);
    }
    if (gs.length > 0) {
      usedKey = key;
      samples.push({ tick: Number.isFinite(fr.tick) ? fr.tick : samples.length, genomes: gs });
    }
  }
  return { found: samples.length > 0, source: usedKey ? 'embedded:' + usedKey : null, samples };
}

// genome 旁车文件: 读
function loadGenomeExternal(p) {
  let raw;
  try {
    raw = fs.readFileSync(p, 'utf8');
  } catch (e) {
    usageError('无法读取 genome 文件: ' + p + ' (' + (e.code || e.message) + ')');
  }
  let obj;
  try {
    obj = JSON.parse(raw);
  } catch (e) {
    usageError('genome 文件不是合法 JSON: ' + p + ': ' + e.message);
  }
  const sampleList = Array.isArray(obj) ? obj : (Array.isArray(obj.samples) ? obj.samples : null);
  if (!sampleList) usageError('genome 文件缺少 samples 数组: ' + p);
  const samples = [];
  for (const s of sampleList) {
    if (!s || typeof s !== 'object') continue;
    const rows = Array.isArray(s.genomes) ? s.genomes : (Array.isArray(s) ? s : []);
    const gs = [];
    for (const row of rows) {
      const g = parseGenomeRow(row);
      if (g) gs.push(g);
    }
    if (gs.length > 0) samples.push({ tick: Number.isFinite(s.tick) ? s.tick : samples.length, genomes: gs });
  }
  return { found: samples.length > 0, source: 'file:' + p, samples };
}

// genome 旁车文件: 写（内嵌 genome 出现时自动归档，供二次分析免重跑）
function writeGenomeExternal(samples, meta, p) {
  const doc = {
    schemaVersion: 1,
    genomeTrace: true,
    generator: 'analyze.mjs ' + VERSION,
    meta: {
      seed: meta.seed ?? null,
      ticks: meta.ticks ?? null,
      source: meta.source ?? null,
      samples: samples.length,
      geneNames: GENE_NAMES,
      note: 'params 顺序 = src/core/Genome.h 字段声明序；hue 单位 = 度 [0,360)',
    },
    samples: samples.map((s) => ({
      tick: s.tick,
      genomes: s.genomes.map((g) => ({ id: g.id, hue: g.hue, params: g.params })),
    })),
  };
  fs.mkdirSync(path.dirname(path.resolve(p)), { recursive: true });
  fs.writeFileSync(p, JSON.stringify(doc), 'utf8');
}

// trace 同名旁车约定: t.json -> t.genome.json
function sidecarPath(tracePath) {
  return tracePath.replace(/\.json$/i, '') + '.genome.json';
}

// ---------------------------------------------------------------------------
// genome 统计
// ---------------------------------------------------------------------------
function kmeans(pts, k) {
  const n = pts.length;
  if (n < k || k < 1) return null;
  const dim = pts[0].length;
  const d2 = (a, b) => {
    let s = 0;
    for (let i = 0; i < dim; i++) { const d = a[i] - b[i]; s += d * d; }
    return s;
  };
  // 确定性初始化: 全局质心最远点起步，随后每次取离已有中心最远的点（最远点遍历）
  const centroid = new Array(dim).fill(0);
  for (const p of pts) for (let i = 0; i < dim; i++) centroid[i] += p[i] / n;
  let first = 0, best = -1;
  for (let i = 0; i < n; i++) {
    const dd = d2(pts[i], centroid);
    if (dd > best) { best = dd; first = i; }
  }
  const centers = [pts[first].slice()];
  while (centers.length < k) {
    let idx = -1, far = -1;
    for (let i = 0; i < n; i++) {
      let mn = Infinity;
      for (const c of centers) mn = Math.min(mn, d2(pts[i], c));
      if (mn > far) { far = mn; idx = i; }
    }
    if (idx < 0) break;
    centers.push(pts[idx].slice());
  }
  // Lloyd 迭代（空簇保持旧中心；200 轮上限；分配不变即收敛）
  let assign = new Array(n).fill(-1);
  for (let iter = 0; iter < 200; iter++) {
    let changed = false;
    for (let i = 0; i < n; i++) {
      let ni = 0, nd = Infinity;
      for (let c = 0; c < centers.length; c++) {
        const dd = d2(pts[i], centers[c]);
        if (dd < nd) { nd = dd; ni = c; }
      }
      if (assign[i] !== ni) { assign[i] = ni; changed = true; }
    }
    if (iter > 0 && !changed) break;
    const sums = centers.map(() => new Array(dim).fill(0));
    const counts = centers.map(() => 0);
    for (let i = 0; i < n; i++) {
      counts[assign[i]]++;
      for (let j = 0; j < dim; j++) sums[assign[i]][j] += pts[i][j];
    }
    for (let c = 0; c < centers.length; c++) {
      if (counts[c] > 0) {
        for (let j = 0; j < dim; j++) centers[c][j] = sums[c][j] / counts[c];
      }
    }
  }
  const sizes = centers.map((_, c) => assign.filter((a) => a === c).length);
  return { assign, centers, sizes };
}

function buildGenomeStats(genome, primaryTickFirst, primaryTickLast) {
  const samples = genome.samples;
  const nS = samples.length;
  const stdSeries = [];
  const meanSeries = [];
  for (let g = 0; g < 24; g++) { stdSeries.push([]); meanSeries.push([]); }
  const hueCircSeries = [];
  for (const s of samples) {
    const pop = s.genomes;
    for (let g = 0; g < 24; g++) {
      const col = pop.map((x) => x.params[g]);
      stdSeries[g].push(pstd(col));
      meanSeries[g].push(mean(col));
    }
    hueCircSeries.push(circularStdDeg(pop.map((x) => x.hue)));
  }
  const genes = [];
  let keptCount = 0;
  for (let g = 0; g < 24; g++) {
    const s0 = stdSeries[g][0];
    const sEnd = stdSeries[g][nS - 1];
    const kept = (s0 > 0 && sEnd > s0 * 0.3) || (s0 === 0 && sEnd > 1e-12);
    if (kept) keptCount++;
    genes.push({ name: GENE_NAMES[g], std0: s0, stdEnd: sEnd, kept, meanEnd: meanSeries[g][nS - 1] });
  }
  const finalSample = samples[nS - 1];
  const scatter = finalSample.genomes.map((g) => ({
    x: g.params[IDX.maxSpeed], y: g.params[IDX.energyThreshold],
    hue: g.hue, color: hsl(g.hue), opacity: 0.85, radius: 3.5,
  }));
  // 主基因选择: maxSpeed + energyThreshold 恒用；cyanoPreference 末态有差异时加入
  const dims = [IDX.maxSpeed, IDX.energyThreshold];
  if (pstd(finalSample.genomes.map((g) => g.params[IDX.cyanoPreference])) > 1e-9) {
    dims.push(IDX.cyanoPreference);
  }
  let clusters = null;
  if (finalSample.genomes.length >= KMEANS_K * 2) {
    const scale = dims.map((d) => {
      const s = pstd(finalSample.genomes.map((g) => g.params[d]));
      return s > 1e-12 ? s : 1;
    });
    const pts = finalSample.genomes.map((g) =>
      dims.map((d, i) => g.params[d] / scale[i]));
    const km = kmeans(pts, Math.min(KMEANS_K, finalSample.genomes.length));
    if (km) {
      const members = km.centers.map((_, c) =>
        finalSample.genomes.filter((_, i) => km.assign[i] === c));
      clusters = {
        dims: dims.map((d) => GENE_NAMES[d]),
        sizes: km.sizes,
        centers: members.map((mem) => ({
          maxSpeed: mean(mem.map((g) => g.params[IDX.maxSpeed])),
          energyThreshold: mean(mem.map((g) => g.params[IDX.energyThreshold])),
          cyanoPreference: dims.includes(IDX.cyanoPreference)
            ? mean(mem.map((g) => g.params[IDX.cyanoPreference])) : null,
          hueCirc: circularStdDeg(mem.map((g) => g.hue)),
        })),
      };
    }
  }
  return {
    nSamples: nS,
    tickFirst: samples[0].tick,
    tickLast: samples[nS - 1].tick,
    populationMax: Math.max(...samples.map((s) => s.genomes.length)),
    stdSeries, meanSeries, hueCircSeries, genes, keptCount, scatter, clusters,
    hueEnd: hueCircSeries[nS - 1],
    hueMax: Math.max(...hueCircSeries),
    hueInit: hueCircSeries[0],
  };
}

// 判据 6 用: 某基因的"末 20% 样本 × 种群均值"的均值
function geneTailMean(gs, geneIdx) {
  const perSample = gs.samples.map((s) => mean(s.genomes.map((g) => g.params[geneIdx])));
  return mean(tailSlice(perSample));
}

// ---------------------------------------------------------------------------
// 判据
// ---------------------------------------------------------------------------
function judgeAll(ctx) {
  const rows = ctx.series;
  const n = rows.length;
  const nuclei = rows.map((r) => r.nuclei);
  const total = rows.map((r) => r.totalEnergy);
  const cyano = rows.map((r) => (Number.isFinite(r.cyanoAlive) ? r.cyanoAlive : 0));
  const firstTick = rows[0].tick ?? 0;
  const lastTick = rows[n - 1].tick ?? (n - 1);
  const spanTicks = Math.max(1, lastTick - firstTick + 1);
  const c = [];

  // ---- 判据 1: 能量紧平衡（GOAL 修订 1：末 30% 窗口 + 未稳态 DEFER）----
  {
    const slope = linregSlope(total);
    const mTotal = mean(total);
    const metricFull = Number.isFinite(slope) && mTotal > 1e-9
      ? Math.abs(slope) * spanTicks / mTotal : NaN;
    // 修订窗口：末 30%
    const w0 = Math.floor(n * 0.7);
    const wTotal = total.slice(w0), wNuclei = nuclei.slice(w0);
    const wTicks = (rows[n-1].tick ?? n-1) - (rows[w0].tick ?? w0);
    const wSlope = linregSlope(wTotal);
    const wMean = mean(wTotal);
    const metric = Number.isFinite(wSlope) && wMean > 1e-9
      ? Math.abs(wSlope) * Math.max(1, wTicks) / wMean : NaN;
    // 稳态前提：末 30% 核数趋势 < 5%/kt（相对窗口均值）
    const nSlope = linregSlope(wNuclei);
    const nMean = Math.max(1e-9, mean(wNuclei));
    const nTrendPct = Number.isFinite(nSlope) ? Math.abs(nSlope) * 1000 / nMean : NaN;
    const steady = Number.isFinite(nTrendPct) && nTrendPct < 0.05;
    let status, why;
    if (!Number.isFinite(metric)) { status = 'SKIP'; why = '能量序列不可用'; }
    else if (!steady) { status = 'DEFER'; why = '未稳态（末窗种群趋势 ' + (nTrendPct*100).toFixed(1) + '%/kt ≥ 5%）——量殖民期是噪音'; }
    else { status = metric < 0.15 ? 'PASS' : 'FAIL'; why = ''; }
    c.push({
      id: 1, name: '能量紧平衡', status,
      value: Number.isFinite(metric) ? metric.toFixed(4) : '—',
      threshold: '末30% |slope|*ticks/mean < 0.15 且种群趋势 <5%/kt',
      detail: '窗口 slope=' + fmt(wSlope, 6) + '/tick, 窗口均值=' + fmt(wMean, 2) +
        ', 全窗=' + (Number.isFinite(metricFull) ? metricFull.toFixed(4) : '—') +
        (why ? '；' + why : ''),
    });
  }

  // ---- 判据 2: 种群波动 ----
  {
    const finalNuclei = nuclei[n - 1];
    const tail = tailSlice(nuclei);
    const cv = cvOf(tail);
    let status, why;
    if (finalNuclei <= 0) { status = 'FAIL'; why = '灭绝（末态核数=0）'; }
    else if (!Number.isFinite(cv)) { status = 'FAIL'; why = '末窗口均值≈0'; }
    else if (cv < 0.05) { status = 'FAIL'; why = '波动不足（锁死/平台）'; }
    else if (cv > 0.6) { status = 'FAIL'; why = '波动过激（崩溃区间）'; }
    else { status = 'PASS'; why = ''; }
    c.push({
      id: 2, name: '种群波动', status,
      value: (Number.isFinite(cv) ? 'cv=' + cv.toFixed(4) : 'cv=—') + ', final=' + finalNuclei,
      threshold: 'cv ∈ [0.05, 0.6] 且 final > 0',
      detail: '末 20% 窗口=' + tail.length + ' tick, mean=' + fmt(mean(tail), 2) +
        (why ? '; ' + why : ''),
    });
  }

  // ---- 判据 5: 捕食者-猎物 ----（先算，保证表序固定仍可在汇总时重排）
  {
    const hasCyano = cyano.some((v) => v > 0);
    if (!hasCyano) {
      c.push({
        id: 5, name: '捕食者-猎物', status: 'SKIP', value: '—',
        threshold: 'r < -0.2 或滞后 1..200 过零',
        detail: 'trace 无蓝细菌序列（cyanoAlive 全 0/缺失，v1 文件？）',
      });
    } else {
      const finalCyano = cyano[cyano.length - 1];
      if (finalCyano <= 0) {
        c.push({
          id: 5, name: '捕食者-猎物', status: 'FAIL', value: '灭绝',
          threshold: 'r < -0.2 或滞后 1..200 过零',
          detail: '蓝细菌末态=0（第一营养级断裂）',
        });
      } else {
        // GOAL 修订 1：殖民期捕食者与猎物必然同向增长——稳态前提与判据 1 同源
        const w0 = Math.floor(n * 0.7);
        const wNuc5 = nuclei.slice(w0);
        const nSlope5 = linregSlope(wNuc5);
        const nMean5 = Math.max(1e-9, mean(wNuc5));
        const nTrend5 = Number.isFinite(nSlope5) ? Math.abs(nSlope5) * 1000 / nMean5 : NaN;
        const steady5 = Number.isFinite(nTrend5) && nTrend5 < 0.05;
        if (!steady5) {
          c.push({
            id: 5, name: '捕食者-猎物', status: 'DEFER', value: 'r=' + fmt(pearson(nuclei, cyano), 4),
            threshold: 'r < -0.2 或滞后 1..200 过零',
            detail: '未稳态（末窗种群趋势 ' + (nTrend5*100).toFixed(1) + '%/kt ≥ 5%）——共同增长趋势主导，相关系数无生态含义',
          });
          return c;   // 判据 6 需 genome+baseline（本轮皆无），提前收表安全
        }
        // GOAL 修订 1：相关在末 30% 稳态窗口内计算——殖民期共增长会污染全窗相关
        const wN = nuclei.slice(w0), wC = cyano.slice(w0);
        const r = pearson(wN, wC);
        const maxLag = Math.min(200, Math.floor(wN.length / 4));
        const cNL = crossCorr(wN, wC, maxLag); // 核领先
        const cLN = crossCorr(wC, wN, maxLag); // 蓝细菌领先
        const zNL = findZeroCrossing(cNL, 1);
        const zLN = findZeroCrossing(cLN, 1);
        let status, why;
        if (Number.isFinite(r) && r < -0.2) { status = 'PASS'; why = '负相关'; }
        else if (zNL !== null || zLN !== null) {
          status = 'PASS';
          why = '互相关滞后过零（捕食者-猎物振荡）@lag=' + (zNL !== null ? zNL : zLN) +
            (zNL !== null && zLN !== null ? '/' + zLN : '') + '（核领先/蓝细菌领先）';
        } else { status = 'FAIL'; why = '无显著负相关也无相位滞后'; }
        const finiteCorrs = cNL.filter(Number.isFinite).map(Math.abs);
        c.push({
          id: 5, name: '捕食者-猎物', status,
          value: 'r=' + (Number.isFinite(r) ? r.toFixed(4) : '—') +
            (zNL !== null || zLN !== null ? ', 过零lag=' + (zNL !== null ? zNL : zLN) : ''),
          threshold: 'r < -0.2 或滞后 1..200 过零',
          detail: why + '; max|corr(lag)|=' +
            (finiteCorrs.length ? fmt(Math.max(...finiteCorrs), 4) : '—') +
            ', maxLag=' + maxLag + ', cyano 末态=' + finalCyano,
        });
        ctx.xcorr = { cNL, cLN, r, maxLag };
      }
    }
  }

  // ---- 判据 3: 基因多样性 ----
  if (!ctx.genome || !ctx.genome.found) {
    c.push({
      id: 3, name: '基因多样性', status: 'SKIP', value: '—',
      threshold: '24 参数中 >= 18 个末态 std > 初态 std * 0.3',
      detail: 'trace 无 genome 字段（需要主代理接线 --trace-genome；本轮无法计算）',
    });
  } else {
    const gs = ctx.genomeStats;
    c.push({
      id: 3, name: '基因多样性', status: gs.keptCount >= 18 ? 'PASS' : 'FAIL',
      value: gs.keptCount + '/24',
      threshold: '>= 18 个参数末态 std > 初态 std * 0.3',
      detail: '未保持: ' + (gs.genes.filter((g) => !g.kept).map((g) => g.name).join(', ') || '无') +
        '; 样本=' + gs.nSamples + ' 段（tick ' + gs.tickFirst + '..' + gs.tickLast + '）',
    });
  }

  // ---- 判据 4: hue 发散 ----
  if (!ctx.genome || !ctx.genome.found) {
    c.push({
      id: 4, name: 'hue 发散', status: 'SKIP', value: '—',
      threshold: 'hue 圆标准差 > 30°',
      detail: 'trace 无 genome 字段（需要主代理接线 --trace-genome；本轮无法计算）',
    });
  } else {
    const gs = ctx.genomeStats;
    const ok = gs.hueEnd > 30;
    c.push({
      id: 4, name: 'hue 发散', status: ok ? 'PASS' : 'FAIL',
      value: '末态=' + gs.hueEnd.toFixed(1) + '°',
      threshold: '> 30°',
      detail: '初始=' + gs.hueInit.toFixed(1) + '°, max=' + gs.hueMax.toFixed(1) + '°' +
        (gs.clusters ? '; k-means 簇大小=' + gs.clusters.sizes.join('/') : '; 样本太少未聚类'),
    });
  }

  // ---- 判据 6: 调控有效 ----
  if (!ctx.baseline) {
    c.push({
      id: 6, name: '调控有效', status: 'SKIP', value: '—',
      threshold: 'cyanoPreference 或 maxSpeed 平均值变化 > 10%',
      detail: '未提供 --baseline <trace2>（判据 6 需要两次运行对比）',
    });
  } else if (!ctx.genome || !ctx.genome.found || !ctx.baselineGenome || !ctx.baselineGenome.found) {
    c.push({
      id: 6, name: '调控有效', status: 'SKIP', value: '—',
      threshold: 'cyanoPreference 或 maxSpeed 平均值变化 > 10%',
      detail: '两次运行都需要 genome 数据（需要主代理接线 --trace-genome）',
    });
  } else {
    const cmp = (geneIdx) => {
      const a = geneTailMean(ctx.genome, geneIdx);
      const b = geneTailMean(ctx.baselineGenome, geneIdx);
      const denom = Math.max(Math.abs(a), Math.abs(b), 1e-9);
      return { a, b, rel: Math.abs(a - b) / denom, abs: Math.abs(a - b) };
    };
    const cp = cmp(IDX.cyanoPreference);
    const ms = cmp(IDX.maxSpeed);
    const ok = (cp.rel > 0.10 && cp.abs > 1e-12) || (ms.rel > 0.10 && ms.abs > 1e-12);
    c.push({
      id: 6, name: '调控有效', status: ok ? 'PASS' : 'FAIL',
      value: 'cyanoPreference ' + pctStr(cp.rel) + ', maxSpeed ' + pctStr(ms.rel),
      threshold: '任一 > 10%（末 20% 样本均值，本组 vs 基线）',
      detail: 'cyanoPreference: ' + fmt(cp.a, 4) + ' vs ' + fmt(cp.b, 4) +
        '; maxSpeed: ' + fmt(ms.a, 4) + ' vs ' + fmt(ms.b, 4),
    });
  }

  c.sort((x, y) => x.id - y.id);
  return c;
}

// ---------------------------------------------------------------------------
// series 解析容错（v2 键都该有；v1 文件缺 cyano* 时按 0 处理 → 判据 5 会 SKIP）
// ---------------------------------------------------------------------------
function parseSeries(rows) {
  return rows.map((r, i) => ({
    tick: Number.isFinite(r.tick) ? r.tick : i,
    nuclei: Number.isFinite(r.nuclei) ? r.nuclei : NaN,
    food: Number.isFinite(r.food) ? r.food : 0,
    cyanoAlive: Number.isFinite(r.cyanoAlive) ? r.cyanoAlive : 0,
    totalEnergy: Number.isFinite(r.totalEnergy) ? r.totalEnergy : NaN,
    injected: Number.isFinite(r.injected) ? r.injected : NaN,
    dissipated: Number.isFinite(r.dissipated) ? r.dissipated : NaN,
    auditOk: r.auditOk !== false,
  }));
}

// ---------------------------------------------------------------------------
// 报告
// ---------------------------------------------------------------------------
function statusMark(s) {
  return s === 'PASS' ? '**PASS**' : s === 'FAIL' ? '**FAIL**' : 'SKIP';
}

function buildReport(ctx) {
  const o = [];
  const rows = ctx.series;
  const n = rows.length;
  const meta = ctx.trace.meta ?? {};
  const nuclei = rows.map((r) => r.nuclei);
  const total = rows.map((r) => r.totalEnergy);
  const cyano = rows.map((r) => r.cyanoAlive);
  const auditBad = rows.filter((r) => !r.auditOk).length;
  const counts = { PASS: 0, FAIL: 0, SKIP: 0 };
  for (const cr of ctx.criteria) counts[cr.status]++;

  o.push('# 生态判据分析报告');
  o.push('');
  o.push('- 输入: ' + ctx.tracePath + '（schemaVersion ' + JSON.stringify(ctx.trace.schemaVersion) +
    ', seed ' + JSON.stringify(meta.seed) + ', ticks ' + JSON.stringify(meta.ticks) +
    ', policy ' + JSON.stringify(meta.policy) + ', series ' + n + ' 行, frames ' +
    (ctx.trace.frames ? ctx.trace.frames.length : 0) + '）');
  o.push('- 基线: ' + (ctx.baselinePath ?? '无（判据 6 SKIP）'));
  o.push('- genome 数据: ' + (ctx.genome && ctx.genome.found
    ? '有（' + ctx.genome.source + '，' + ctx.genomeStats.nSamples + ' 段样本）'
    : '**未找到** → 判据 3/4/6 SKIP'));
  if (!ctx.genome || !ctx.genome.found) {
    o.push('');
    o.push('> **需要主代理接线: --trace-genome**（trace 里没有 genome 字段，');
    o.push('> 判据 3 基因多样性 / 判据 4 hue 发散 / 判据 6 调控对比无法计算。）');
  }
  o.push('');
  o.push('## 一、判据总表');
  o.push('');
  o.push('| # | 判据 | 结果 | 实测值 | 阈值 | 说明 |');
  o.push('|---|------|------|--------|------|------|');
  for (const cr of ctx.criteria) {
    o.push('| ' + cr.id + ' | ' + cr.name + ' | ' + statusMark(cr.status) + ' | ' +
      esc(String(cr.value)) + ' | ' + esc(cr.threshold) + ' | ' + esc(cr.detail) + ' |');
  }
  o.push('');
  o.push('**总结: ' + counts.PASS + ' PASS / ' + counts.SKIP + ' SKIP / ' + counts.FAIL + ' FAIL**' +
    (counts.SKIP > 0 ? '（SKIP = 数据缺失，不是生态失败）' : ''));
  o.push('');

  // 二、时间序列
  const ticks = rows.map((r) => r.tick);
  o.push('## 二、时间序列（核数 / 蓝细菌数 / 总能量）');
  o.push('');
  o.push(multiLineChart({
    title: 'population & energy',
    xLabel: 'tick', yLabel: 'count', y2Label: 'totalEnergy',
    series: [
      { name: 'nuclei', points: ticks.map((t, i) => [t, nuclei[i]]), color: '#58a6ff' },
      { name: 'cyanoAlive', points: ticks.map((t, i) => [t, cyano[i]]), color: '#3fb950' },
      { name: 'totalEnergy', points: ticks.map((t, i) => [t, total[i]]), color: '#d29922', axis: 'right' },
    ],
  }));
  o.push('');
  o.push('- 核数: 初 ' + nuclei[0] + ' → 峰 ' + Math.max(...nuclei) + ' → 末 ' + nuclei[n - 1]);
  o.push('- 蓝细菌: 初 ' + cyano[0] + ' → 峰 ' + Math.max(...cyano) + ' → 末 ' + cyano[cyano.length - 1]);
  o.push('- 总能量: 初 ' + fmt(total[0], 2) + ' → 峰 ' + fmt(Math.max(...total), 2) +
    ' → 末 ' + fmt(total[total.length - 1], 2));
  o.push('');

  // 三、能量收支
  const injected = rows.map((r) => r.injected);
  const dissipated = rows.map((r) => r.dissipated);
  const slope = linregSlope(total);
  o.push('## 三、能量收支');
  o.push('');
  o.push('| 量 | 值 |');
  o.push('|----|----|');
  o.push('| 总能量线性斜率 | ' + fmt(slope, 6) + ' /tick |');
  o.push('| 总能量均值 | ' + fmt(mean(total), 2) + ' |');
  o.push('| 累计注入 (injected) | ' + fmt(injected[injected.length - 1] - injected[0], 2) + ' |');
  o.push('| 累计耗散 (dissipated) | ' + fmt(dissipated[dissipated.length - 1] - dissipated[0], 2) + ' |');
  o.push('| 净变化 (注入-耗散) | ' + fmt((injected[injected.length - 1] - injected[0]) -
    (dissipated[dissipated.length - 1] - dissipated[0]), 2) + ' |');
  o.push('| auditOk 违规帧 | ' + auditBad + ' / ' + n + ' |');
  o.push('');

  // 四、种群波动
  const tailN = tailSlice(nuclei);
  o.push('## 四、种群波动（判据 2 细节）');
  o.push('');
  o.push('- 末 20% 窗口: ' + tailN.length + ' tick（tick ' +
    rows[n - tailN.length].tick + '..' + rows[n - 1].tick + '）');
  o.push('- 窗口均值 ' + fmt(mean(tailN), 3) + ', 总体标准差 ' + fmt(pstd(tailN), 3) +
    ', CV = ' + (Number.isFinite(cvOf(tailN)) ? cvOf(tailN).toFixed(4) : '—'));
  o.push('');
  o.push(multiLineChart({
    title: 'nuclei (last 20% window)', xLabel: 'tick', yLabel: 'nuclei',
    width: 520, height: 180, legend: false,
    series: [{ name: 'nuclei', points: rows.slice(n - tailN.length).map((r) => [r.tick, r.nuclei]) }],
  }));
  o.push('');

  // 五、捕食者-猎物
  if (ctx.xcorr) {
    o.push('## 五、捕食者-猎物相位（判据 5 细节）');
    o.push('');
    o.push('- 皮尔逊 r(nuclei, cyano) = ' +
      (Number.isFinite(ctx.xcorr.r) ? ctx.xcorr.r.toFixed(4) : '—'));
    o.push('- 互相关过零滞后: 核领先 ' + (findZeroCrossing(ctx.xcorr.cNL, 1) ?? '无') +
      '，蓝细菌领先 ' + (findZeroCrossing(ctx.xcorr.cLN, 1) ?? '无') + '（检查范围 lag 1..' + ctx.xcorr.maxLag + '）');
    o.push('');
    o.push(singleLineChart(ctx.xcorr.cNL, {
      title: 'cross-correlation: nuclei leads cyano (lag 0..' + ctx.xcorr.maxLag + ')',
      xLabel: 'lag', yLabel: 'corr', width: 520, height: 170,
    }));
    o.push('');
  }

  // 六、基因多样性
  if (ctx.genome && ctx.genome.found) {
    const gs = ctx.genomeStats;
    o.push('## 六、基因多样性（判据 3 细节）');
    o.push('');
    o.push('保持率 = 末态 std / 初态 std；判据: 末态 std > 初态 std × 0.3');
    o.push('');
    o.push('| # | 参数 | 初态 std | 末态 std | 保持率 | 判定 | 末态均值 | std 轨迹 |');
    o.push('|---|------|----------|----------|--------|------|----------|----------|');
    gs.genes.forEach((g, i) => {
      const ratio = g.std0 > 0 ? g.stdEnd / g.std0 : NaN;
      o.push('| ' + i + ' | ' + g.name + ' | ' + fmt(g.std0, 4) + ' | ' + fmt(g.stdEnd, 4) +
        ' | ' + (Number.isFinite(ratio) ? fmt(ratio, 3) : '—') + ' | ' + (g.kept ? 'OK' : '塌缩') +
        ' | ' + fmt(g.meanEnd, 3) + ' | ' + sparkline(gs.stdSeries[i]) + ' |');
    });
    o.push('');
    o.push('**保持数: ' + gs.keptCount + ' / 24**（判据阈值 >= 18）');
    o.push('');
    o.push('### hue 圆标准差（判据 4 细节）');
    o.push('');
    o.push('- 初始 ' + gs.hueInit.toFixed(1) + '° → 末态 ' + gs.hueEnd.toFixed(1) + '°（max ' +
      gs.hueMax.toFixed(1) + '°），判据阈值 30°');
    o.push('');
    o.push(multiLineChart({
      title: 'hue circular std over time', xLabel: 'sample', yLabel: 'deg', legend: false,
      width: 520, height: 180,
      series: [{ name: 'hue circ std', points: gs.hueCircSeries.map((v, i) => [i, v]), color: '#f778ba' }],
    }));
    o.push('');
  } else {
    o.push('## 六、基因多样性（判据 3）与 hue 发散（判据 4）');
    o.push('');
    o.push('**SKIP** — trace 无 genome 字段（v3 契约的帧键 g 未出现）。');
    o.push('需要主代理在 C++ 侧加 --trace-genome 导出：每帧每核 g=[[id, p0..p23, hue],...]');
    o.push('（26 个数，参数顺序 = src/core/Genome.h 声明序，hue 在最后），本工具即可自动计算。');
    o.push('');
  }

  // 七、末态散点 + 聚类
  if (ctx.genome && ctx.genome.found) {
    const gs = ctx.genomeStats;
    o.push('## 七、末态散点（maxSpeed × energyThreshold，点色 = hue）');
    o.push('');
    o.push(scatterChart({ title: 'final population strategies', xLabel: 'maxSpeed', yLabel: 'energyThreshold', points: gs.scatter }));
    o.push('');
    o.push('## 八、策略聚类（k-means k=' + KMEANS_K + '，主基因: ' +
      (gs.clusters ? gs.clusters.dims.join(', ') : '—') + '）');
    o.push('');
    if (gs.clusters) {
      o.push('| 簇 | 大小 | 中心 maxSpeed | 中心 energyThreshold | 中心 cyanoPreference | 簇内 hue 圆标准差 |');
      o.push('|----|------|----------------|----------------------|----------------------|-------------------|');
      gs.clusters.centers.forEach((cen, i) => {
        o.push('| ' + i + ' | ' + gs.clusters.sizes[i] + ' | ' + fmt(cen.maxSpeed, 3) +
          ' | ' + fmt(cen.energyThreshold, 3) + ' | ' +
          (cen.cyanoPreference !== null ? fmt(cen.cyanoPreference, 4) : '—') +
          ' | ' + cen.hueCirc.toFixed(1) + '° |');
      });
      o.push('');
      o.push('簇大小分布越均匀、中心差距越大 → 策略分化越明显（判据 4b 的人工检查材料）。');
    } else {
      o.push('样本太少（< ' + (KMEANS_K * 2) + ' 个核），未聚类。');
    }
    o.push('');
  }

  // 九、判据 6
  if (ctx.baseline) {
    o.push('## 九、调控有效性对比（判据 6，本组 vs 基线 ' + ctx.baselinePath + '）');
    o.push('');
    if (ctx.genome && ctx.genome.found && ctx.baselineGenome && ctx.baselineGenome.found) {
      const cpA = geneTailMean(ctx.genome, IDX.cyanoPreference);
      const cpB = geneTailMean(ctx.baselineGenome, IDX.cyanoPreference);
      const msA = geneTailMean(ctx.genome, IDX.maxSpeed);
      const msB = geneTailMean(ctx.baselineGenome, IDX.maxSpeed);
      o.push('| 基因 | 本组（末20%均值） | 基线 | 相对变化 |');
      o.push('|------|-------------------|------|----------|');
      o.push('| cyanoPreference | ' + fmt(cpA, 4) + ' | ' + fmt(cpB, 4) + ' | ' +
        pctStr(Math.abs(cpA - cpB) / Math.max(Math.abs(cpA), Math.abs(cpB), 1e-9)) + ' |');
      o.push('| maxSpeed | ' + fmt(msA, 4) + ' | ' + fmt(msB, 4) + ' | ' +
        pctStr(Math.abs(msA - msB) / Math.max(Math.abs(msA), Math.abs(msB), 1e-9)) + ' |');
    } else {
      o.push('**SKIP** — 两次运行都需要 genome 数据（需要主代理接线 --trace-genome）。');
    }
    o.push('');
  }

  o.push('## 附: 方法说明');
  o.push('');
  o.push('- 能量斜率 = 全序列最小二乘回归（单位: 每 tick）；紧平衡指标 = \\|slope\\| × ticks / mean(total)。');
  o.push('- CV = 总体标准差 / 均值，窗口 = 序列末 20%。');
  o.push('- hue 圆标准差 = sqrt(-2 ln R)，R = \\|mean(e^{iθ})\\|（圆统计，0°/360° 视为同一点）。');
  o.push('- 互相关 = corr(nuclei[t], cyano[t+lag])，lag 0..' + Math.min(200, Math.floor(n / 4)) +
    '，两个领先方向都检查过零。');
  o.push('- k-means: 确定性最远点初始化 + Lloyd 迭代，主基因标准化（除以末态总体 std）后欧氏距离。');
  o.push('- 判据定义: docs/GOAL-ECOLOGY.md §一；工具版本 analyze.mjs v' + VERSION + '。');
  o.push('');
  return o.join('\n');
}

// ---------------------------------------------------------------------------
// JSON 输出
// ---------------------------------------------------------------------------
function buildJson(ctx) {
  const rows = ctx.series;
  const counts = { PASS: 0, FAIL: 0, SKIP: 0 };
  for (const cr of ctx.criteria) counts[cr.status]++;
  return {
    tool: 'analyze.mjs', version: VERSION,
    input: ctx.tracePath,
    meta: ctx.trace.meta ?? null,
    baseline: ctx.baselinePath,
    genomeFound: !!(ctx.genome && ctx.genome.found),
    genomeSource: ctx.genome ? ctx.genome.source : null,
    series: {
      rows: rows.length,
      tickFirst: rows[0].tick, tickLast: rows[rows.length - 1].tick,
      nucleiFinal: rows[rows.length - 1].nuclei,
      nucleiPeak: Math.max(...rows.map((r) => r.nuclei)),
      cyanoFinal: rows[rows.length - 1].cyanoAlive,
      totalEnergyFinal: rows[rows.length - 1].totalEnergy,
      totalEnergyMean: mean(rows.map((r) => r.totalEnergy)),
      energySlope: linregSlope(rows.map((r) => r.totalEnergy)),
      nucleiCvTail: cvOf(tailSlice(rows.map((r) => r.nuclei))),
      auditBadRows: rows.filter((r) => !r.auditOk).length,
    },
    genome: ctx.genomeStats ? {
      samples: ctx.genomeStats.nSamples,
      keptGenes: ctx.genomeStats.keptCount,
      genes: ctx.genomeStats.genes,
      hueCircStd: { init: ctx.genomeStats.hueInit, end: ctx.genomeStats.hueEnd, max: ctx.genomeStats.hueMax },
      clusters: ctx.genomeStats.clusters,
    } : null,
    criteria: ctx.criteria,
    summary: counts,
  };
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
function main() {
  const argv = process.argv.slice(2);
  if (argv.includes('--version')) {
    process.stdout.write('analyze.mjs ' + VERSION + '\n');
    process.exit(EXIT_OK);
  }
  const opts = parseArgs(argv);
  if (opts.help || !opts.trace) {
    printHelp();
    process.exit(opts.help ? EXIT_OK : EXIT_USAGE);
  }

  const trace = loadTrace(opts.trace, 'trace');
  const series = parseSeries(trace.series);
  const ctx = { tracePath: opts.trace, trace, series, baselinePath: null, baseline: null, baselineGenome: null, genome: null, genomeStats: null, xcorr: null };

  // genome 获取顺序: 显式 --genome > 旁车文件 > trace 内嵌
  let genomeSource = null;
  if (opts.genome) {
    ctx.genome = loadGenomeExternal(opts.genome);
    genomeSource = 'explicit';
  } else {
    const sidecar = sidecarPath(opts.trace);
    if (fs.existsSync(sidecar)) {
      ctx.genome = loadGenomeExternal(sidecar);
      genomeSource = 'sidecar';
    } else {
      ctx.genome = extractGenomeSamples(trace);
    }
  }
  if (ctx.genome.found) {
    ctx.genomeStats = buildGenomeStats(ctx.genome);
    // 内嵌 genome 自动归档旁车文件（显式/旁车来源不回写）
    if (genomeSource === null || genomeSource === 'embedded') {
      const outG = opts.outGenome ?? sidecarPath(opts.trace);
      try {
        writeGenomeExternal(ctx.genome.samples, {
          seed: trace.meta?.seed, ticks: trace.meta?.ticks, source: opts.trace,
        }, outG);
        process.stderr.write('analyze: genome 旁车已写出: ' + outG + '\n');
      } catch (e) {
        process.stderr.write('analyze: warning: genome 旁车写出失败: ' + e.message + '\n');
      }
    }
  }

  if (opts.baseline) {
    ctx.baselinePath = opts.baseline;
    ctx.baseline = loadTrace(opts.baseline, 'baseline');
    // 基线 genome: 基线旁车 > 基线内嵌
    const bSidecar = sidecarPath(opts.baseline);
    if (fs.existsSync(bSidecar)) ctx.baselineGenome = loadGenomeExternal(bSidecar);
    else ctx.baselineGenome = extractGenomeSamples(ctx.baseline);
  }

  ctx.criteria = judgeAll(ctx);

  // stdout 摘要（ASCII，避免 Windows 控制台代码页问题）
  const counts = { PASS: 0, FAIL: 0, SKIP: 0 };
  for (const cr of ctx.criteria) counts[cr.status]++;
  const line = (cr) => {
    const pad = (s, w) => (s.length >= w ? s : s + ' '.repeat(w - s.length));
    return '  ' + cr.id + ' ' + pad(cr.name, 10) + ' ' + pad('[' + cr.status + ']', 8) + ' ' + cr.value;
  };
  process.stdout.write('analyze: ' + opts.trace + '  seed=' + JSON.stringify(ctx.trace.meta?.seed) +
    ' series=' + series.length + '\n');
  for (const cr of ctx.criteria) process.stdout.write(line(cr) + '\n');
  process.stdout.write('  summary: ' + counts.PASS + ' PASS / ' + counts.SKIP + ' SKIP / ' +
    counts.FAIL + ' FAIL\n');

  if (opts.out) {
    try {
      fs.mkdirSync(path.dirname(path.resolve(opts.out)), { recursive: true });
      fs.writeFileSync(opts.out, buildReport(ctx), 'utf8');
      process.stdout.write('  report: ' + opts.out + '\n');
    } catch (e) {
      usageError('无法写出报告: ' + opts.out + ': ' + e.message);
    }
  }
  const jsonDoc = JSON.stringify(buildJson(ctx), null, 2) + '\n';
  if (opts.jsonPath) {
    try {
      fs.mkdirSync(path.dirname(path.resolve(opts.jsonPath)), { recursive: true });
      fs.writeFileSync(opts.jsonPath, jsonDoc, 'utf8');
      process.stdout.write('  json: ' + opts.jsonPath + '\n');
    } catch (e) {
      usageError('无法写出 JSON: ' + opts.jsonPath + ': ' + e.message);
    }
  } else if (opts.jsonStdout) {
    process.stdout.write(jsonDoc);
  }

  process.exit(counts.FAIL > 0 ? EXIT_FAIL : EXIT_OK);
}

main();
