/* agentsim 演示数据生成器 — 确定性（mulberry32 + 固定消耗序）· 零依赖
 *
 * 本轮后端（HTTP /state 实时快照）尚未接入：这里用种子化程序生成与"真实契约 V3"
 * 逐字段一致的演示轨迹（300 帧：核元组 n /球元组 b /蓝细菌连线拓扑 f /六项蓝细菌元组 c（含遗传色相）/ 每核基因组导出 g）。
 * 后端接上后：把 main.js 里 DemoData.generate(...) 的调用换成 fetch/WebSocket
 * 提供的帧流即可，TraceParser / Renderer / 各 UI 模块无需改动。
 *
 * ── 确定性纪律（同 seed 双跑逐字节一致）────────────────────────────
 *  唯一随机源 rng = mulberry32((seed ^ 0x9E3779B9) >>> 0)，高斯 = Box-Muller(2×rng)。
 *  初始化消耗序（固定，不得增删）：
 *    核 i 递增 → [x, y, energy, lineageHue, 24 个 genome 高斯
 *      （affinity×3 → orbitRadius×3 → absorbPreference×3 → repelStrength×3 →
 *        nucleusSightRadius → nucleusPersonalSpace → nucleusRepelStrength → nucleusCohesion →
 *        forceRange → forceCostK → maxSpeed → maxAccel → energyThreshold → mutationRate →
 *        cyanoPreference → cyanoDigestion）]
 *    → 球（每核 k=2+floor(rng×3) 个 → [typePick, angle, radiusJitter]）
 *    → 自由球（6 个 → [x, y, angle, speed]）→ 蓝细菌节点（[x, y, energy, hue]）
 *  每帧消耗序（固定）：核 wander 随机游走(每核 1 次) → 核繁殖判定(每核 1 次)。
 *    其余动态（蓝细菌光合/传输/繁殖、球轨道、施力模式）全部是确定性函数，不耗随机。
 * ────────────────────────────────────────────────────────────────
 * 能量审计：injected = 初始全場存量 + 累计光合注入；dissipated = 累计代谢/施力耗散；
 * sumLiving = 核+球+自由球+蓝细菌 当前存量 → 恒等式 injected - dissipated == sumLiving
 * 按构造成立（fp 误差 < 0.5），auditOk 恒为 true，与主项目审计范式一致。
 */
window.DemoData = (function () {
  'use strict';

  var TAU = Math.PI * 2;
  var WORLD = 256;

  // ── 随机源 ────────────────────────────────────────────────────
  function mulberry32(a) {
    return function () {
      a |= 0; a = (a + 0x6D2B79F5) | 0;
      var t = Math.imul(a ^ (a >>> 15), 1 | a);
      t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
  }

  function makeRng(seed) {
    var rng = mulberry32(((seed | 0) ^ 0x9E3779B9) >>> 0);
    var spare = null;
    function gauss() { // Box-Muller；固定消耗 2 个均匀数（缓存备用值也不跨调用边界复用）
      var u = 0, v = 0;
      while (u === 0) u = rng();
      while (v === 0) v = rng();
      return Math.sqrt(-2 * Math.log(u)) * Math.cos(TAU * v);
    }
    return { uni: rng, gauss: gauss };
  }

  function wrap(v) { return v - Math.floor(v / WORLD) * WORLD; }

  // 环面最近位移：从 a 指向 b 的最短向量（分量为 [-W/2, W/2]）
  function torusDelta(ax, ay, bx, by) {
    var dx = bx - ax, dy = by - ay;
    if (dx > WORLD / 2) dx -= WORLD; else if (dx < -WORLD / 2) dx += WORLD;
    if (dy > WORLD / 2) dy -= WORLD; else if (dy < -WORLD / 2) dy += WORLD;
    return { dx: dx, dy: dy };
  }

  // ── 基因组（V2 契约：24 个参数，无战斗字段）──────────────────
  // 各参数 Init 范围取自 V2-SPEC-AMEND-1 §二.1/§二.2 与 V2-SPEC §A
  function genGenome(rng, lineage) {
    var g = {};
    var a1 = Math.abs(rng.gauss()) + 0.25, a2 = Math.abs(rng.gauss()) + 0.25, a3 = Math.abs(rng.gauss()) + 0.25;
    var asum = a1 + a2 + a3;
    g.affinity = [a1 / asum, a2 / asum, a3 / asum];              // 归一化和为 1
    g.orbitRadius = [11 + rng.uni() * 18, 16 + rng.uni() * 22, 9 + rng.uni() * 14];
    g.absorbPreference = [0.6 + rng.uni() * 0.9, 0.6 + rng.uni() * 0.9, 0.6 + rng.uni() * 0.9];
    g.repelStrength = [0.5 + rng.uni() * 2.0, 0.5 + rng.uni() * 2.0, 0.5 + rng.uni() * 2.0];
    g.nucleusSightRadius = 40 + rng.uni() * 260;                 // Init 40~300
    g.nucleusPersonalSpace = 15 + rng.uni() * 65;                // Init 15~80
    g.nucleusRepelStrength = 0.5 + rng.uni() * 5.5;              // Init 0.5~6.0
    g.nucleusCohesion = rng.uni() * 3.0;                         // Init 0~3.0
    g.forceRange = 30 + rng.uni() * 120;                         // Init 30~150
    g.forceCostK = 0.005 + rng.uni() * 0.045;                    // Init 0.005~0.05
    g.maxSpeed = 1.0 + rng.uni() * 1.2;
    g.maxAccel = 0.08 + rng.uni() * 0.12;
    g.energyThreshold = 110 + rng.uni() * 50;
    g.mutationRate = 0.02 + rng.uni() * 0.10;
    g.cyanoPreference = 0.3 + rng.uni() * 0.7;
    g.cyanoDigestion = 0.5 + rng.uni() * 0.4;
    if (lineage) { // 子代 = 父代 + 高斯抖动（×mutationRate），再 clamp
      var c = {};
      var mr = lineage.mutationRate;
      for (var k in lineage) {
        if (!Object.prototype.hasOwnProperty.call(lineage, k)) continue;
        var v = lineage[k];
        if (Array.isArray(v)) {
          c[k] = v.map(function (x) { return x * (1 + rng.gauss() * mr * 0.5); });
        } else if (typeof v === 'number') {
          c[k] = v * (1 + rng.gauss() * mr);
        } else c[k] = v;
      }
      c.affinity = lineage.affinity.map(function (x) { return Math.max(0.02, x * (1 + rng.gauss() * mr * 0.5)); });
      var s = c.affinity[0] + c.affinity[1] + c.affinity[2];
      c.affinity = c.affinity.map(function (x) { return x / s; });
      g = clampGenome(c);
    }
    return g;
  }

  var GCLAMP = {
    nucleusSightRadius: [10, 600], nucleusPersonalSpace: [5, 200],
    nucleusRepelStrength: [0.1, 20], nucleusCohesion: [0, 10],
    forceRange: [10, 400], forceCostK: [0.0001, 1.0],
    maxSpeed: [0.2, 4], maxAccel: [0.01, 1], energyThreshold: [60, 400],
    mutationRate: [0, 0.5], cyanoPreference: [0, 1], cyanoDigestion: [0.1, 1]
  };

  function clampGenome(g) {
    for (var k in GCLAMP) {
      if (typeof g[k] === 'number') g[k] = Math.min(GCLAMP[k][1], Math.max(GCLAMP[k][0], g[k]));
    }
    for (var i = 0; i < 3; i++) {
      g.orbitRadius[i] = Math.min(48, Math.max(5, g.orbitRadius[i]));
      g.absorbPreference[i] = Math.min(3, Math.max(0.1, g.absorbPreference[i]));
      g.repelStrength[i] = Math.min(8, Math.max(0.05, g.repelStrength[i]));
    }
    return g;
  }

  // ── v3 基因组扁平化（Genome.h 声明序，与主项目 --trace-genome 严格同序）──
  // [id, affinity×3, orbitRadius×3, absorbPreference×3, repelStrength×3,
  //  maxSpeed, maxAccel, energyThreshold, mutationRate, cyanoPreference,
  //  cyanoDigestion, nucleusSightRadius, nucleusPersonalSpace, nucleusRepelStrength,
  //  nucleusCohesion, forceRange, forceCostK, hue] → 26 个数/核。
  function flattenGenome(n) {
    var g = n.genome;
    return [n.id,
      g.affinity[0], g.affinity[1], g.affinity[2],
      g.orbitRadius[0], g.orbitRadius[1], g.orbitRadius[2],
      g.absorbPreference[0], g.absorbPreference[1], g.absorbPreference[2],
      g.repelStrength[0], g.repelStrength[1], g.repelStrength[2],
      g.maxSpeed, g.maxAccel, g.energyThreshold, g.mutationRate,
      g.cyanoPreference, g.cyanoDigestion,
      g.nucleusSightRadius, g.nucleusPersonalSpace,
      g.nucleusRepelStrength, g.nucleusCohesion,
      g.forceRange, g.forceCostK,
      n.hue];
  }

  // ── 生成器主入口 ──────────────────────────────────────────────
  // opts: { seed, nuclei, cyano, frames, autosave }
  function generate(opts) {
    opts = opts || {};
    var seed = (typeof opts.seed === 'number' && isFinite(opts.seed)) ? (opts.seed | 0) : 42;
    var N0 = Math.min(24, Math.max(1, opts.nuclei | 0 || 12));
    var M0 = Math.min(120, Math.max(4, opts.cyano | 0 || 48));
    var T = Math.min(600, Math.max(20, opts.frames | 0 || 300));
    var rng = makeRng(seed);

    var nextNucleusId = 1, nextBallId = 1, nextCyanoId = 1;

    // ---- 初始化：核（消耗序见文件头）----
    var lineages = [];   // 血统基色：同血统 hue 相近 → 谱系聚集肉眼可见
    var nuclei = [];
    for (var i = 0; i < N0; i++) {
      var x = rng.uni() * WORLD, y = rng.uni() * WORLD;
      var energy = 100 + rng.uni() * 60;
      var lineageHue = (i < 4)
        ? i * 90 + rng.uni() * 24                    // 前 4 个是血统始祖：基色铺满色环
        : lineages[(rng.uni() * lineages.length) | 0] + rng.gauss() * 14;
      var hue = ((lineageHue % 360) + 360) % 360;
      lineages.push(hue);
      var n = {
        id: nextNucleusId++, x: x, y: y, vx: 0, vy: 0,
        energy: energy, hue: hue, cap: 240, alive: true,
        wander: rng.uni() * TAU,
        genome: genGenome(rng, null),
        forceTarget: -1, forceX: 0, forceY: 0
      };
      nuclei.push(n);
    }

    // ---- 初始化：球（Resource 0 / Scout 1 / Shield 2）----
    var balls = [];
    var TYPE_SPEED = [1.3, 3.2, 0.9]; // 各类型 maxSpeed 上限（Scout 最高）
    for (var ni = 0; ni < nuclei.length; ni++) {
      var owner = nuclei[ni];
      var count = 2 + Math.floor(rng.uni() * 3); // 2~4
      for (var bi = 0; bi < count; bi++) {
        var pick = rng.uni();
        var type = pick < 0.45 ? 0 : (pick < 0.75 ? 1 : 2);
        var ang = rng.uni() * TAU;
        var orb = owner.genome.orbitRadius[type];
        balls.push({
          id: nextBallId++, owner: owner.id,
          x: wrap(owner.x + Math.cos(ang) * orb), y: wrap(owner.y + Math.sin(ang) * orb),
          vx: 0, vy: 0, energy: 6 + rng.uni() * 8,
          radius: type === 0 ? 3.2 : (type === 1 ? 2.6 : 3.6),
          type: type, angle: ang, freeGrace: 0
        });
      }
    }

    // ---- 初始化：自由球 ----
    for (var fi = 0; fi < 6; fi++) {
      var fx = rng.uni() * WORLD, fy = rng.uni() * WORLD;
      var fang = rng.uni() * TAU, fsp = 0.2 + rng.uni() * 0.4;
      balls.push({
        id: nextBallId++, owner: 0,
        x: fx, y: fy, vx: Math.cos(fang) * fsp, vy: Math.sin(fang) * fsp,
        energy: 8 + rng.uni() * 10, radius: 3.0, type: Math.floor(rng.uni() * 3),
        angle: fang, freeGrace: 0
      });
    }

    // ---- 初始化：蓝细菌 ----
    var cyano = [];
    for (var ci = 0; ci < M0; ci++) {
      cyano.push({
        id: nextCyanoId++, x: rng.uni() * WORLD, y: rng.uni() * WORLD,
        energy: 6 + rng.uni() * 8, hue: 128 + rng.gauss() * 16, alive: true
      });
    }

    var cyanoLinks = [];   // [idA, idB]；每 8 帧重算，帧间共享同一数组（省分配）
    var sunPeriod = 200;   // 与渲染器昼夜周期一致
    var injected = 0, dissipated = 0;
    for (var e0 = 0; e0 < nuclei.length; e0++) injected += nuclei[e0].energy;
    for (var e1 = 0; e1 < balls.length; e1++) injected += balls[e1].energy;
    for (var e2 = 0; e2 < cyano.length; e2++) injected += cyano[e2].energy;
    var initialStock = injected;

    recomputeLinks();

    function recomputeLinks() {
      var aliveN = [];
      for (var i = 0; i < cyano.length; i++) if (cyano[i].alive) aliveN.push(cyano[i]);
      var pairs = [];
      var MAXL = 3, LD = 20;
      for (var a = 0; a < aliveN.length; a++) {
        var na = aliveN[a], links = 0;
        // 就近配对：按距离平方排序的前 maxLinks 个
        var cands = [];
        for (var b = 0; b < aliveN.length; b++) {
          if (a === b) continue;
          var nb = aliveN[b];
          var d = torusDelta(na.x, na.y, nb.x, nb.y);
          var d2 = d.dx * d.dx + d.dy * d.dy;
          if (d2 <= LD * LD) cands.push([d2, nb.id]);
        }
        cands.sort(function (p, q) { return p[0] - q[0]; });
        for (var c = 0; c < cands.length && links < MAXL; c++) {
          var lo = Math.min(na.id, cands[c][1]), hi = Math.max(na.id, cands[c][1]);
          var dup = false;
          for (var pi = 0; pi < pairs.length; pi++) {
            if (pairs[pi][0] === lo && pairs[pi][1] === hi) { dup = true; break; }
          }
          if (!dup) { pairs.push([lo, hi]); links++; }
        }
      }
      cyanoLinks.length = 0;
      for (var pp = 0; pp < pairs.length; pp++) cyanoLinks.push(pairs[pp]);
    }

    // ---- 每帧模拟 ----
    function step(tick) {
      var sun = 0.5 + 0.5 * Math.sin(TAU * tick / sunPeriod);

      // 1) 蓝细菌光合（唯一 injectPrimary）+ 代谢
      for (var i = 0; i < cyano.length; i++) {
        var cn = cyano[i];
        if (!cn.alive) continue;
        var photo = 0.85 * sun * (1 - cn.energy / 26);
        if (photo > 0) { cn.energy += photo; injected += photo; }
        var cmet = Math.min(0.02, Math.max(0, cn.energy));
        cn.energy -= cmet; dissipated += cmet;
        if (cn.energy < 0.5) { cn.alive = false; dissipated += Math.max(0, cn.energy); cn.energy = 0; continue; }
        // 繁殖：能量足够 → 子节点出现在父节点附近（肉眼可见），色相抖动
        if (cn.energy > 16 && cyano.length < 96 && ((tick + cn.id) % 7 === 0)) {
          var off = 2.5 + ((cn.id * 13 + tick) % 30) / 10;
          var oa = ((cn.id * 47 + tick * 3) % 360) * TAU / 360;
          cyano.push({
            id: nextCyanoId++, x: wrap(cn.x + Math.cos(oa) * off), y: wrap(cn.y + Math.sin(oa) * off),
            energy: cn.energy * 0.38, hue: cn.hue + Math.sin(tick * 0.7 + cn.id) * 7, alive: true
          });
          cn.energy *= 0.62;
        }
      }
      // 2) 连线能量传输：高 → 低（deadband 0.5）
      var byId = {};
      for (var li = 0; li < cyano.length; li++) byId[cyano[li].id] = cyano[li];
      for (var lk = 0; lk < cyanoLinks.length; lk++) {
        var pa = byId[cyanoLinks[lk][0]], pb = byId[cyanoLinks[lk][1]];
        if (!pa || !pb || !pa.alive || !pb.alive) continue;
        var diff = pa.energy - pb.energy;
        if (Math.abs(diff) <= 0.5) continue;
        var flow = 0.08 * diff * 0.5;
        pa.energy -= flow; pb.energy += flow;
      }
      if (tick % 8 === 0) recomputeLinks();
      // 生产者托底：蓝细菌近乎灭绝时从"种子库"补一株（确定性位置，能量走 injectPrimary）
      var cyanoAliveNow = 0;
      for (var ca = 0; ca < cyano.length; ca++) if (cyano[ca].alive) cyanoAliveNow++;
      if (cyanoAliveNow < 5 && cyano.length < 96) {
        cyano.push({
          id: nextCyanoId++, x: (tick * 37) % WORLD, y: (tick * 53) % WORLD,
          energy: 6, hue: 128 + Math.sin(tick * 0.3) * 10, alive: true
        });
        injected += 6;
      }

      // 3) 核移动：聚拢 + 斥力 + wander + 觅食偏向（无战斗）
      for (var ni = 0; ni < nuclei.length; ni++) {
        var n = nuclei[ni];
        var g = n.genome;
        // wander（每帧每核恰好 1 次 rng —— 固定消耗序）
        n.wander += (rng.uni() * 2 - 1) * 0.55;
        var ax = Math.cos(n.wander) * 0.03, ay = Math.sin(n.wander) * 0.03;
        var sx = 0, sy = 0, cnt = 0, cx = 0, cy2 = 0;
        for (var nj = 0; nj < nuclei.length; nj++) {
          if (nj === ni) continue;
          var o = nuclei[nj];
          var dd = torusDelta(n.x, n.y, o.x, o.y);
          var dist = Math.sqrt(dd.dx * dd.dx + dd.dy * dd.dy) || 1e-6;
          if (dist < g.nucleusSightRadius) { // 聚拢
            cx += dd.dx; cy2 += dd.dy; cnt++;
          }
          if (dist < g.nucleusPersonalSpace) { // 个人空间斥力
            var push = g.nucleusRepelStrength * (1 - dist / g.nucleusPersonalSpace) * 0.05;
            ax -= dd.dx / dist * push; ay -= dd.dy / dist * push;
          }
        }
        if (cnt > 0 && g.nucleusCohesion > 0) {
          ax += cx / cnt * 0.0016 * g.nucleusCohesion;
          ay += cy2 / cnt * 0.0016 * g.nucleusCohesion;
        }
        // 觅食：偏向最近的蓝细菌（cyanoPreference 越高追得越紧）
        var bestD = 1e9, bt = null;
        for (var ck = 0; ck < cyano.length; ck++) {
          var cn2 = cyano[ck];
          if (!cn2.alive || cn2.energy < 2) continue;
          var dc = torusDelta(n.x, n.y, cn2.x, cn2.y);
          var dd2 = dc.dx * dc.dx + dc.dy * dc.dy;
          if (dd2 < bestD) { bestD = dd2; bt = { dx: dc.dx, dy: dc.dy, node: cn2 }; }
        }
        if (bt && bestD < 90 * 90) {
          var bd = Math.sqrt(bestD) || 1e-6;
          var seek = 0.012 * g.cyanoPreference;
          ax += bt.dx / bd * seek; ay += bt.dy / bd * seek;
        }
        // 积分 + 限速 + wrap
        n.vx = (n.vx + ax) * 0.96; n.vy = (n.vy + ay) * 0.96;
        var sp = Math.sqrt(n.vx * n.vx + n.vy * n.vy);
        if (sp > g.maxSpeed) { n.vx = n.vx / sp * g.maxSpeed; n.vy = n.vy / sp * g.maxSpeed; }
        n.speed = Math.min(sp, g.maxSpeed);
        n.x = wrap(n.x + n.vx); n.y = wrap(n.y + n.vy);
      }

      // 4) 施力：确定性模式挑核 → 点名范围内最近的球（自由球优先），代价走 dissipated
      for (var fi2 = 0; fi2 < nuclei.length; fi2++) {
        var fn = nuclei[fi2];
        fn.forceTarget = -1; fn.forceX = 0; fn.forceY = 0;
        if ((tick * 7 + fi2 * 13) % 97 >= 4) continue;
        var fg = fn.genome;
        var target = null, tDist = 1e9;
        for (var bk = 0; bk < balls.length; bk++) {
          var bb = balls[bk];
          if (bb.owner === fn.id) continue;
          var dfb = torusDelta(fn.x, fn.y, bb.x, bb.y);
          var d3 = Math.sqrt(dfb.dx * dfb.dx + dfb.dy * dfb.dy);
          var bias = bb.owner === 0 ? 0.7 : 1.0; // 自由球优先
          if (d3 * bias < fg.forceRange && d3 * bias < tDist) { tDist = d3 * bias; target = { ball: bb, dx: dfb.dx, dy: dfb.dy, dist: d3 }; }
        }
        if (!target) continue;
        var mag = 42;
        var ux = target.dx / (target.dist || 1e-6), uy = target.dy / (target.dist || 1e-6);
        // 施力方向 = 远离核 + 少量切向（看得见的"拨动"）
        var fvx = (ux * 0.85 - uy * 0.5), fvy = (uy * 0.85 + ux * 0.5);
        var fl = Math.sqrt(fvx * fvx + fvy * fvy) || 1e-6;
        fvx = fvx / fl * mag; fvy = fvy / fl * mag;
        var cost = fg.forceCostK * mag * mag;
        if (fn.energy > 30 + cost) {
          fn.energy -= cost; dissipated += cost;
          target.ball.vx += fvx * 0.055; target.ball.vy += fvy * 0.055;
          fn.forceTarget = target.ball.id;
          fn.forceX = fvx * 0.055; fn.forceY = fvy * 0.055; // 实际加速度（渲染箭头方向）
        }
      }

      // 5) 球动力学：轨道弹簧 + 切向环绕 + 阻尼 + 类型限速 + wrap
      for (var si = 0; si < balls.length; si++) {
        var ball = balls[si];
        if (ball.owner !== 0) {
          var on = null;
          for (var q = 0; q < nuclei.length; q++) if (nuclei[q].id === ball.owner) { on = nuclei[q]; break; }
          if (!on) { ball.owner = 0; } // 主人死了 → 自由球
          else {
            var orb2 = on.genome.orbitRadius[ball.type];
            ball.angle += (ball.type === 1 ? 0.10 : (ball.type === 0 ? 0.055 : 0.035));
            var wantX = wrap(on.x + Math.cos(ball.angle) * orb2);
            var wantY = wrap(on.y + Math.sin(ball.angle) * orb2);
            var dv = torusDelta(ball.x, ball.y, wantX, wantY);
            ball.vx += dv.dx * 0.02; ball.vy += dv.dy * 0.02;
            // 主人能量缓慢喂给 Resource 球（零和）
            if (ball.type === 0 && on.energy > on.cap * 0.5 && ball.energy < 16) {
              on.energy -= 0.03; ball.energy += 0.03;
            }
          }
        }
        ball.vx *= 0.90; ball.vy *= 0.90;
        var bs = Math.sqrt(ball.vx * ball.vx + ball.vy * ball.vy);
        var cap2 = TYPE_SPEED[ball.type];
        if (bs > cap2) { ball.vx = ball.vx / bs * cap2; ball.vy = ball.vy / bs * cap2; }
        ball.x = wrap(ball.x + ball.vx); ball.y = wrap(ball.y + ball.vy);
        var bdec = Math.min(0.02, Math.max(0, ball.energy));
        ball.energy -= bdec; dissipated += bdec;
      }
      // 球死亡（能量耗尽 → 腐朽消失，残能按耗散记账）；倒序遍历便于删除
      for (var bdi = balls.length - 1; bdi >= 0; bdi--) {
        if (balls[bdi].energy <= 0.2) {
          dissipated += Math.max(0, balls[bdi].energy);
          balls.splice(bdi, 1);
        }
      }

      // 6) 核捕食蓝细菌（进食 = 活→活转移，零和，无账本项）
      for (var gi = 0; gi < nuclei.length; gi++) {
        var gn = nuclei[gi];
        for (var gj = 0; gj < cyano.length; gj++) {
          var gc = cyano[gj];
          if (!gc.alive || gc.energy < 1.5) continue;
          var dg = torusDelta(gn.x, gn.y, gc.x, gc.y);
          var d4 = dg.dx * dg.dx + dg.dy * dg.dy;
          if (d4 < 100) { // grazeRadius 10
            var bite = Math.min(gc.energy, 4.0);
            var raw = bite * gn.genome.cyanoDigestion * gn.genome.cyanoPreference;
            var gain = Math.min(raw, gn.cap - gn.energy);
            if (gain > 0) {
              gc.energy -= bite; gn.energy += gain;
              var waste = bite - gain;             // 消化损耗：活→活转移的差额，按耗散记账
              if (waste > 0) dissipated += waste;
            }
            if (gc.energy < 0.5) { gc.alive = false; dissipated += Math.max(0, gc.energy); gc.energy = 0; }
          }
        }
        // 代谢（守恒：永不创造能量；种群<=2 时在 1 点休眠不饿死）
        if (nuclei.length > 2) {
          var met = Math.min(0.18, Math.max(0, gn.energy));
          gn.energy -= met; dissipated += met;
        } else if (gn.energy > 1) {
          gn.energy -= 0.18; dissipated += 0.18;
        }
      }

      // 7) 核繁殖（子代 hue = 父 hue + 高斯 → 谱系色团）+ 死亡（每帧各 1 次 rng）
      var born = null;
      for (var ri = 0; ri < nuclei.length; ri++) {
        var rn = nuclei[ri];
        if (rn.energy > rn.genome.energyThreshold && nuclei.length < 22 && rng.uni() < 0.6) {
          var ch = {
            id: nextNucleusId++,
            x: wrap(rn.x + Math.cos(rn.wander) * 7), y: wrap(rn.y + Math.sin(rn.wander) * 7),
            vx: 0, vy: 0, energy: rn.energy * 0.42,
            hue: ((rn.hue + rng.gauss() * 6) % 360 + 360) % 360,
            cap: rn.cap * 0.85, alive: true, wander: rng.uni() * TAU,
            genome: genGenome(rng, rn.genome),
            forceTarget: -1, forceX: 0, forceY: 0
          };
          rn.energy *= 0.58;
          born = ch;
          break; // 每帧最多一胎（保持消耗序简单）
        }
      }
      for (var di = nuclei.length - 1; di >= 0; di--) {
        if (nuclei[di].energy <= 8 && nuclei.length > 2) {
          dissipated += Math.max(0, nuclei[di].energy); // 遗体分解 → 按耗散记账
          nuclei.splice(di, 1); // 饿死 → 其球在球循环里转自由
        }
      }
      if (born) nuclei.push(born);
    }

    // ---- 主循环：跑 T 帧，录快照 ----
    var frames = [];
    for (var t = 1; t <= T; t++) {
      step(t);
      var sumLiving = 0, cyanoAlive = 0, cyanoEnergy = 0;
      var snapN = [], snapB = [], snapC = [], snapG = [];
      for (var a1 = 0; a1 < nuclei.length; a1++) {
        var nn = nuclei[a1];
        sumLiving += nn.energy;
        // v3 核元组（7 项 + 3 项施力扩展位；主项目契约只写前 7 项，解析器兼容两者）
        snapN.push([nn.id, nn.x, nn.y, nn.energy, nn.cap, nn.speed || 0, nn.alive,
          nn.forceTarget, nn.forceX, nn.forceY]);
        snapG.push(flattenGenome(nn));   // g 键：Genome.h 声明序 26 数
      }
      for (var b1 = 0; b1 < balls.length; b1++) {
        var sb = balls[b1];
        sumLiving += sb.energy;
        snapB.push([sb.id, sb.owner, sb.x, sb.y, sb.energy, sb.radius, sb.type]);
      }
      // v3：容器里没有死节点（帧末压缩）——活节点才进快照
      for (var c1 = 0; c1 < cyano.length; c1++) {
        var sc = cyano[c1];
        if (!sc.alive) continue;
        sumLiving += sc.energy; cyanoAlive++; cyanoEnergy += sc.energy;
        snapC.push([sc.id, sc.x, sc.y, sc.energy, true, sc.hue]);  // 6 项：含遗传色相
      }
      var auditDiff = injected - dissipated - sumLiving;
      frames.push({
        tick: t,
        n: snapN,
        b: snapB,
        f: cyanoLinks,          // v3：f = 蓝细菌连线拓扑 [[idA,idB],...]
        c: snapC,
        g: snapG,               // v3+：基因组导出（对应主项目 --trace-genome）
        stats: {
          nuclei: snapN.length, cyanoAlive: cyanoAlive, cyanoEnergy: cyanoEnergy,
          totalEnergy: sumLiving, injected: injected, dissipated: dissipated,
          auditOk: Math.abs(auditDiff) < 0.5
        }
      });
    }

    var series = [];
    for (var s2 = 0; s2 < frames.length; s2++) {
      var st = frames[s2].stats;
      series.push({
        tick: frames[s2].tick, nuclei: st.nuclei, cyanoAlive: st.cyanoAlive,
        cyanoEnergy: st.cyanoEnergy, totalEnergy: st.totalEnergy,
        injected: st.injected, dissipated: st.dissipated, auditOk: st.auditOk
      });
    }

    return {
      schemaVersion: 3,
      meta: {
        seed: seed, ticks: T, world: { width: WORLD, height: WORLD },
        policy: 'demo', generator: 'agentsim-frontend-r3d', contractVersion: 3,
        framesCaptured: frames.length, framesTruncated: false,
        genome: true, nuclei0: N0, cyano0: M0
      },
      bounds: { minX: 0, minY: 0, maxX: WORLD, maxY: WORLD },
      frames: frames,
      series: series
    };
  }

  return { generate: generate, WORLD: WORLD };
})();
