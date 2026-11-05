/* agentsim 轨迹契约解析与校验 — v3 为主 · 容错 v2（元组/对象两形态）· 零依赖
 *
 * 契约 v3（docs/TRACE-CONTRACT.md v3，R1-B 蓝细菌取代食物）：
 *   frame = { tick, n, b, f, c, g? }   （定长元组形态）
 *     n: [[id,x,y,energy,cap,speed,alive],...]                       7 项
 *     b: [[id,owner,x,y,energy,radius,type],...]                     7 项
 *     f: [[idA,idB],...]   蓝细菌连线拓扑（v3 语义；v2 里 f 是食物元组）
 *     c: [[id,x,y,energy,alive,hue],...]                             6 项（hue ∈ [0,360)）
 *     g: [[id, 24 基因, hue],...]  --trace-genome 导出，26 个数/核，基因顺序 =
 *        Genome.h 声明序：affinity[3], orbitRadius[3], absorbPreference[3],
 *        repelStrength[3], maxSpeed, maxAccel, energyThreshold, mutationRate,
 *        cyanoPreference, cyanoDigestion, nucleusSightRadius, nucleusPersonalSpace,
 *        nucleusRepelStrength, nucleusCohesion, forceRange, forceCostK，末位 hue。
 *     容器里没有死节点（帧末压缩）；真 v3 帧无 stats 键 → 从 series 同 tick 行回填。
 *     series 行删除 food 键，键序 tick, nuclei, cyanoAlive, cyanoEnergy,
 *     totalEnergy, injected, dissipated, auditOk。
 *
 * 容错 v2（实测 build/test_trace_v2_*.json：**元组形态**）：
 *   n/b 与 v3 同序；f = 食物元组 5 项 [id,x,y,energy,alive]（**绝不当连线**——
 *   parseLinks 只收长度恰为 2 的整数对，5 项食物行天然被拒）；c = 5 项
 *   [id,x,y,energy,alive]（无 hue → 兜底 130 且 hueReal=false，不进多样性统计）；
 *   死节点在容器里（v2 语义：前端可见死亡），原样保留 alive 标志。
 * 容错 v2（对象形态，旧 /state 导出 / 前端旧演示数据 / 存档快照）：
 *   nuclei/balls/cyano 为对象 + cyanoLinks 键。
 *   两形态在同一帧里混合出现时也兼容（各自解析后合并）。
 *
 * 归一化输出（统一对象形态，渲染器/主控只吃这一种）：
 *   frame.nuclei[i] = {id,x,y,energy,cap,speed,alive,forceTarget,forceX,forceY,
 *                      hue,hueReal,genome,genomeReal}
 *   frame.cyano[i]  = {id,x,y,energy,alive,hue,hueReal}
 *   frame.cyanoLinks = [[idA,idB],...]（来自 v3 f / v2 cyanoLinks）
 *   frame.genomes    = [{id,genes[24],hue,genome}]（无 g 时为空数组）
 *   frame.hueSd / frame.cyanoHueSd：hue 圆标准差（度）。只用「真实遗传 hue」计算
 *      （核 hue 来自 g 或 v2 对象 hue；v3 无 g 时核 hue 是按 id 黄金角合成的兜底色，
 *      标记 hueReal=false，不参与多样性统计——绝不做假多样性数据）。
 *
 * 校验策略：宽松净化（剔除坏字段/坏实体，绝不抛到白屏）；坏 tick 按下标补齐；
 * schemaVersion 不是 2/3 → 明确报错（不静默降级）。trace.hasGenome 告诉调用方
 * g 是否存在过（v3 的 g 键不出现 = 没开 --trace-genome，与"这一帧没有核"可区分）。
 */
window.TraceParser = (function () {
  'use strict';

  function fail(msg) { throw new Error(msg); }
  function isNum(v) { return typeof v === 'number' && isFinite(v); }
  function num(v, def) { return isNum(v) ? v : def; }
  function boolish(v) { return v !== false && v !== 0; }

  // 24 基因默认值（解析缺字段时的兜底；与 V2 时代前端默认一致）
  function defaultGenome() {
    return {
      affinity: [1 / 3, 1 / 3, 1 / 3],
      orbitRadius: [18, 26, 15],
      absorbPreference: [1, 1, 1],
      repelStrength: [1, 1, 1],
      nucleusSightRadius: 120,
      nucleusPersonalSpace: 30,
      nucleusRepelStrength: 2,
      nucleusCohesion: 1,
      forceRange: 80,
      forceCostK: 0.02,
      maxSpeed: 2,
      maxAccel: 0.2,
      energyThreshold: 160,
      mutationRate: 0.05,
      cyanoPreference: 0.6,
      cyanoDigestion: 0.7
    };
  }

  function sanitizeGenome(gRaw) {
    // 键插入顺序固定 = genomeFromFlat / Genome.h 声明序——保证 parse(parse(x))
    // 逐字节幂等（存档快照回灌二次解析时 JSON.stringify 完全一致）。
    var d = defaultGenome();
    var valid = gRaw && typeof gRaw === 'object' && !Array.isArray(gRaw);
    function arr3(key, def) {
      var a = valid && Array.isArray(gRaw[key]) ? gRaw[key] : [];
      return [num(a[0], def), num(a[1], def), num(a[2], def)];
    }
    function pick(key) { return valid ? num(gRaw[key], d[key]) : d[key]; }
    return {
      affinity: arr3('affinity', 1 / 3),
      orbitRadius: arr3('orbitRadius', 18),
      absorbPreference: arr3('absorbPreference', 1),
      repelStrength: arr3('repelStrength', 1),
      maxSpeed: pick('maxSpeed'),
      maxAccel: pick('maxAccel'),
      energyThreshold: pick('energyThreshold'),
      mutationRate: pick('mutationRate'),
      cyanoPreference: pick('cyanoPreference'),
      cyanoDigestion: pick('cyanoDigestion'),
      nucleusSightRadius: pick('nucleusSightRadius'),
      nucleusPersonalSpace: pick('nucleusPersonalSpace'),
      nucleusRepelStrength: pick('nucleusRepelStrength'),
      nucleusCohesion: pick('nucleusCohesion'),
      forceRange: pick('forceRange'),
      forceCostK: pick('forceCostK')
    };
  }

  // v3：g 行 = [id, 基因0..基因23, hue]（26 个数）→ 基因组对象。
  // 顺序 = Genome.h 声明序（主项目 main.cpp/TraceWriter 同序，两端不许各自重排）。
  function genomeFromFlat(t) {
    return {
      affinity: [t[1], t[2], t[3]],
      orbitRadius: [t[4], t[5], t[6]],
      absorbPreference: [t[7], t[8], t[9]],
      repelStrength: [t[10], t[11], t[12]],
      maxSpeed: t[13],
      maxAccel: t[14],
      energyThreshold: t[15],
      mutationRate: t[16],
      cyanoPreference: t[17],
      cyanoDigestion: t[18],
      nucleusSightRadius: t[19],
      nucleusPersonalSpace: t[20],
      nucleusRepelStrength: t[21],
      nucleusCohesion: t[22],
      forceRange: t[23],
      forceCostK: t[24]
    };
  }

  function parseGenomeRow(raw) {
    if (!Array.isArray(raw) || raw.length < 26) return null;
    for (var i = 0; i < 26; i++) if (!isNum(raw[i])) return null;
    return {
      id: raw[0] | 0,
      genes: raw.slice(1, 25),
      hue: raw[25],
      genome: genomeFromFlat(raw)
    };
  }

  // ---------- 核 ----------
  // v3/v2 元组：[id,x,y,energy,cap,speed,alive]（7 项）。
  // 宽容：≥8 项时追加位读作 forceTarget/forceX/forceY（演示数据用；真后端 7 项）。
  function parseTupleNucleus(t, fallbackId) {
    if (!Array.isArray(t) || t.length < 7) return null;
    if (!isNum(t[1]) || !isNum(t[2])) return null;
    return {
      id: isNum(t[0]) ? (t[0] | 0) : fallbackId,
      x: t[1], y: t[2],
      energy: num(t[3], 50),
      cap: num(t[4], 120),
      speed: num(t[5], 0),
      alive: boolish(t[6]),
      forceTarget: (t.length > 7 && isNum(t[7]) && t[7] >= 0) ? (t[7] | 0) : -1,
      forceX: t.length > 8 ? num(t[8], 0) : 0,
      forceY: t.length > 9 ? num(t[9], 0) : 0,
      hue: 210,            // 兜底合成基色；attachGenomes/对象 hue 可覆盖
      hueReal: false,
      genome: defaultGenome(),
      genomeReal: false
    };
  }

  // v2 对象形态（旧 /state 导出 / 前端旧演示数据）
  function parseObjNucleus(raw, fallbackId) {
    if (!raw || typeof raw !== 'object' || Array.isArray(raw)) return null;
    if (!isNum(raw.x) || !isNum(raw.y)) return null;
    var hasHue = isNum(raw.hue);
    return {
      id: isNum(raw.id) ? raw.id : fallbackId,
      x: raw.x, y: raw.y,
      energy: num(raw.energy, 50),
      cap: num(raw.cap, 120),
      speed: num(raw.speed, 0),
      alive: raw.alive !== false,
      forceTarget: isNum(raw.forceTarget) ? (raw.forceTarget | 0) : -1,
      forceX: num(raw.forceX, 0),
      forceY: num(raw.forceY, 0),
      hue: hasHue ? raw.hue : 210,
      hueReal: hasHue,
      genome: sanitizeGenome(raw.genome),
      genomeReal: !!(raw.genome && typeof raw.genome === 'object' && !Array.isArray(raw.genome))
    };
  }

  // ---------- 球 ----------
  function parseTupleBall(t, fallbackId) {
    if (!Array.isArray(t) || t.length < 7) return null;
    if (!isNum(t[2]) || !isNum(t[3])) return null;
    var type = num(t[6], 0) | 0;
    return {
      id: isNum(t[0]) ? (t[0] | 0) : fallbackId,
      owner: num(t[1], 0) | 0,
      x: t[2], y: t[3],
      energy: num(t[4], 5),
      radius: num(t[5], 3),
      type: (type >= 0 && type <= 2) ? type : 0
    };
  }

  function parseObjBall(raw, fallbackId) {
    if (!raw || typeof raw !== 'object' || Array.isArray(raw)) return null;
    if (!isNum(raw.x) || !isNum(raw.y)) return null;
    var type = num(raw.type, 0) | 0;
    return {
      id: isNum(raw.id) ? raw.id : fallbackId,
      owner: num(raw.owner, 0) | 0,
      x: raw.x, y: raw.y,
      energy: num(raw.energy, 5),
      radius: num(raw.radius, 3),
      type: (type >= 0 && type <= 2) ? type : 0
    };
  }

  // ---------- 蓝细菌 ----------
  // v3：6 项 [id,x,y,energy,alive,hue]；v2：5 项（无 hue → 默认 130，hueReal=false）
  function parseTupleCyano(t, fallbackId) {
    if (!Array.isArray(t) || t.length < 5) return null;
    if (!isNum(t[1]) || !isNum(t[2])) return null;
    var hasHue = t.length >= 6 && isNum(t[5]);
    return {
      id: isNum(t[0]) ? (t[0] | 0) : fallbackId,
      x: t[1], y: t[2],
      energy: num(t[3], 5),
      alive: boolish(t[4]),
      hue: hasHue ? t[5] : 130,
      hueReal: hasHue
    };
  }

  function parseObjCyano(raw, fallbackId) {
    if (!raw || typeof raw !== 'object' || Array.isArray(raw)) return null;
    if (!isNum(raw.x) || !isNum(raw.y)) return null;
    var hasHue = isNum(raw.hue);
    return {
      id: isNum(raw.id) ? raw.id : fallbackId,
      x: raw.x, y: raw.y,
      energy: num(raw.energy, 5),
      alive: raw.alive !== false,
      hue: hasHue ? raw.hue : 130,
      hueReal: hasHue
    };
  }

  // ---------- 连线 ----------
  // 严格只收 [idA,idB] 整数对：v3 的 f 与 v2 的 cyanoLinks 都走这里；
  // 长度 != 2 的行（v2 食物 5 元组等）绝不进来，自环丢弃。
  function parseLinks(raw, dropped) {
    var out = [];
    if (!Array.isArray(raw)) return out;
    for (var i = 0; i < raw.length; i++) {
      var l = raw[i];
      if (!Array.isArray(l) || l.length !== 2) { if (dropped) dropped.links++; continue; }
      if (!isNum(l[0]) || !isNum(l[1])) { if (dropped) dropped.links++; continue; }
      if (l[0] === l[1]) { if (dropped) dropped.links++; continue; }
      out.push([l[0] | 0, l[1] | 0]);
    }
    return out;
  }

  // ---------- hue 圆标准差（度）----------
  // R = |Σ e^{iθ}| / n；SD = sqrt(-2 ln R)（弧度）→ 度。R≥1 → 0；R≈0 → 封顶 180。
  function circSdDeg(hues) {
    var n = hues.length;
    if (n < 2) return NaN;
    var sx = 0, sy = 0;
    for (var i = 0; i < n; i++) {
      var a = hues[i] * Math.PI / 180;
      sx += Math.cos(a); sy += Math.sin(a);
    }
    var R = Math.sqrt(sx * sx + sy * sy) / n;
    if (R >= 1) return 0;
    if (R < 1e-9) return 180;
    return Math.min(360, Math.max(0, Math.sqrt(-2 * Math.log(R)) * 180 / Math.PI));
  }

  // 把 g 行按 id 贴回核（基因组 + 真实 hue）
  function attachGenomes(frame, genomes) {
    var byId = {};
    for (var i = 0; i < genomes.length; i++) byId[genomes[i].id] = genomes[i];
    for (var j = 0; j < frame.nuclei.length; j++) {
      var nu = frame.nuclei[j];
      var gr = byId[nu.id];
      if (gr) {
        nu.genome = gr.genome;
        nu.genomeReal = true;
        nu.hue = gr.hue;
        nu.hueReal = true;
      }
    }
  }

  function emptyFrame(tick) {
    return {
      tick: tick,
      nuclei: [], balls: [], cyano: [], cyanoLinks: [], genomes: [],
      hueSd: NaN, cyanoHueSd: NaN,
      stats: { nuclei: 0, cyanoAlive: 0, cyanoEnergy: 0, totalEnergy: 0,
        injected: 0, dissipated: 0, auditOk: true }
    };
  }

  function parseFrame(f, idx, docVersion, dropped, seriesRow) {
    if (!f || typeof f !== 'object' || Array.isArray(f)) return emptyFrame(idx);
    var tick = isNum(f.tick) ? f.tick : idx;

    // 实体抽取：元组形态（n/b/c 键）与对象形态（nuclei/balls/cyano 键）**并存兼容**——
    // 各自解析后合并（真 v2 文件是元组形态；旧 /state 导出与旧演示数据是对象形态；
    // 存档快照可能混合）。两种键都缺 → 空帧。
    var nuclei = [], balls = [], cyano = [];
    var tArr = Array.isArray(f.n) ? f.n : null;
    var oArr = Array.isArray(f.nuclei) ? f.nuclei : null;

    if (tArr) {
      for (var i = 0; i < tArr.length; i++) {
        var pn = parseTupleNucleus(tArr[i], i + 1);
        if (pn) nuclei.push(pn); else dropped.nuclei++;
      }
    }
    if (oArr) {
      for (var i2 = 0; i2 < oArr.length; i2++) {
        var pn2 = parseObjNucleus(oArr[i2], i2 + 1);
        if (pn2) nuclei.push(pn2); else dropped.nuclei++;
      }
    }

    var tB = Array.isArray(f.b) ? f.b : null;
    var oB = Array.isArray(f.balls) ? f.balls : null;
    if (tB) {
      for (var b = 0; b < tB.length; b++) {
        var pb = parseTupleBall(tB[b], b + 1);
        if (pb) balls.push(pb); else dropped.balls++;
      }
    }
    if (oB) {
      for (var b2 = 0; b2 < oB.length; b2++) {
        var pb2 = parseObjBall(oB[b2], b2 + 1);
        if (pb2) balls.push(pb2); else dropped.balls++;
      }
    }

    var tC = Array.isArray(f.c) ? f.c : null;
    var oC = Array.isArray(f.cyano) ? f.cyano : null;
    if (tC) {
      for (var c = 0; c < tC.length; c++) {
        var pc = parseTupleCyano(tC[c], c + 1);
        if (pc) cyano.push(pc); else dropped.cyano++;
      }
    }
    if (oC) {
      for (var c2 = 0; c2 < oC.length; c2++) {
        var pc2 = parseObjCyano(oC[c2], c2 + 1);
        if (pc2) cyano.push(pc2); else dropped.cyano++;
      }
    }

    // 连线来源（按版本定向，另一形态兜底；5 项食物元组会被 parseLinks 天然拒绝）：
    //   v3 → f 键 = 拓扑对；v2 → cyanoLinks 键（键序 tick,n,b,f,c 与 v3 相同，
    //   但 v2 的 f 是食物元组，绝不能进连线）。双向兜底覆盖误标版本的文件。
    var links = [];
    if (docVersion >= 3) {
      links = parseLinks(f.f, dropped);
      if (!links.length && Array.isArray(f.cyanoLinks)) links = parseLinks(f.cyanoLinks, dropped);
    } else {
      links = parseLinks(f.cyanoLinks, dropped);
      if (!links.length && Array.isArray(f.f)) links = parseLinks(f.f, dropped); // v3 形状误标 v2 时兜底
    }

    // 基因组导出（可选键；行内 26 个数，缺一剔除）
    var genomes = [];
    if (Array.isArray(f.g)) {
      for (var g = 0; g < f.g.length; g++) {
        var gr = parseGenomeRow(f.g[g]);
        if (gr) genomes.push(gr); else dropped.genomes++;
      }
    }
    // 幂等容错：输入已是本 parser 的归一化输出（存档快照回灌 loadTrace → parse）时，
    // 帧上的 genomes 规范行（{id,genes,hue,genome}）同样要收——否则二次解析丢基因组。
    if (!Array.isArray(f.g) && Array.isArray(f.genomes)) {
      for (var g2 = 0; g2 < f.genomes.length; g2++) {
        var grow = f.genomes[g2];
        if (grow && typeof grow === 'object' && !Array.isArray(grow) && isNum(grow.id) &&
            grow.genome && typeof grow.genome === 'object') {
          genomes.push({
            id: grow.id | 0,
            genes: Array.isArray(grow.genes) ? grow.genes.slice(0, 24) : [],
            hue: num(grow.hue, 210),
            genome: sanitizeGenome(grow.genome)
          });
        }
      }
    }

    var frame = {
      tick: tick,
      nuclei: nuclei,
      balls: balls,
      cyano: cyano,
      cyanoLinks: links,
      genomes: genomes,
      hueSd: NaN,
      cyanoHueSd: NaN,
      stats: null
    };
    attachGenomes(frame, genomes);

    // 多样性（圆标准差）：只用真实遗传 hue——核：g 或 v2 对象 hue；蓝细菌：v3 第 6 项。
    var realNucHues = [];
    for (var h = 0; h < nuclei.length; h++) if (nuclei[h].hueReal) realNucHues.push(nuclei[h].hue);
    if (realNucHues.length >= 2) frame.hueSd = circSdDeg(realNucHues);
    else if (genomes.length >= 2) frame.hueSd = circSdDeg(genomes.map(function (x) { return x.hue; }));
    var realCyHues = [];
    for (var h2 = 0; h2 < cyano.length; h2++) if (cyano[h2].hueReal) realCyHues.push(cyano[h2].hue);
    if (realCyHues.length >= 2) frame.cyanoHueSd = circSdDeg(realCyHues);

    // 帧内 stats：真 v3 契约帧里没有 stats 键 → 用 series 的同 tick 行回填（series 每帧
    // 一条、永不降采样）。帧自带 stats（演示数据/存档快照）时优先用帧的。
    var st = (f.stats && typeof f.stats === 'object' && !Array.isArray(f.stats)) ? f.stats
      : (seriesRow && typeof seriesRow === 'object' ? seriesRow : {});
    frame.stats = {
      nuclei: num(st.nuclei, nuclei.length) | 0,
      cyanoAlive: num(st.cyanoAlive, cyano.length) | 0,
      cyanoEnergy: num(st.cyanoEnergy, 0),
      totalEnergy: num(st.totalEnergy, 0),
      injected: num(st.injected, 0),
      dissipated: num(st.dissipated, 0),
      auditOk: st.auditOk !== false
    };
    return frame;
  }

  function deriveBounds(frames) {
    var minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
    function touch(x, y) {
      if (!isNum(x) || !isNum(y)) return;
      if (x < minX) minX = x;
      if (x > maxX) maxX = x;
      if (y < minY) minY = y;
      if (y > maxY) maxY = y;
    }
    for (var i = 0; i < frames.length; i++) {
      var f = frames[i];
      for (var a = 0; a < f.nuclei.length; a++) touch(f.nuclei[a].x, f.nuclei[a].y);
      for (var b = 0; b < f.balls.length; b++) touch(f.balls[b].x, f.balls[b].y);
      for (var c = 0; c < f.cyano.length; c++) touch(f.cyano[c].x, f.cyano[c].y);
    }
    if (minX === Infinity) return { minX: 0, minY: 0, maxX: 256, maxY: 256 };
    return { minX: minX - 8, minY: minY - 8, maxX: maxX + 8, maxY: maxY + 8 };
  }

  function parse(json) {
    if (!json || typeof json !== 'object' || Array.isArray(json)) {
      fail('轨迹必须是 JSON 对象（实际收到 ' + (json === null ? 'null' : typeof json) + '）');
    }
    // 版本判定：schemaVersion 优先，meta.contractVersion 兜底；只认 2 / 3。
    var sv = (typeof json.schemaVersion === 'number' && isFinite(json.schemaVersion))
      ? json.schemaVersion
      : (json.meta && typeof json.meta.contractVersion === 'number' && isFinite(json.meta.contractVersion)
        ? json.meta.contractVersion : NaN);
    var docVersion = sv | 0;
    if (docVersion !== 2 && docVersion !== 3) {
      fail('契约版本不受支持：schemaVersion 必须为 3（v3：蓝细菌连线拓扑 + 遗传色相）或 2（旧版，容错读入），实际为 ' +
        JSON.stringify(json.schemaVersion == null ? (json.meta ? json.meta.contractVersion : null) : json.schemaVersion) +
        '。请用匹配版本的模拟器重新导出轨迹。');
    }
    if (json.frames != null && !Array.isArray(json.frames)) {
      fail('frames 字段必须是数组（允许空数组，但不接受 ' + typeof json.frames + ' 类型）');
    }

    // series 先提取：供帧缺 stats 时按 tick 回填（v3 契约帧无 stats 键）
    var seriesRows = Array.isArray(json.series) ? json.series : [];
    var seriesByTick = {};
    for (var sr = 0; sr < seriesRows.length; sr++) {
      var srow = seriesRows[sr];
      if (srow && typeof srow === 'object' && !Array.isArray(srow) && isNum(srow.tick)) {
        seriesByTick[srow.tick] = srow;
      }
    }

    var dropped = { nuclei: 0, balls: 0, cyano: 0, links: 0, genomes: 0 };
    var framesRaw = Array.isArray(json.frames) ? json.frames : [];
    var frames = new Array(framesRaw.length);
    var hasGenome = false;
    for (var i = 0; i < framesRaw.length; i++) {
      frames[i] = parseFrame(framesRaw[i], i, docVersion, dropped,
        seriesByTick[isNum(framesRaw[i] && framesRaw[i].tick) ? framesRaw[i].tick : i]);
      if (frames[i].genomes.length) hasGenome = true;
    }

    var series = seriesRows.filter(function (s) { return s && typeof s === 'object' && !Array.isArray(s); });

    var b = json.bounds;
    var bounds = (b && isNum(b.minX) && isNum(b.minY) && isNum(b.maxX) && isNum(b.maxY) &&
      b.maxX > b.minX && b.maxY > b.minY)
      ? { minX: b.minX, minY: b.minY, maxX: b.maxX, maxY: b.maxY }
      : deriveBounds(frames);

    var totalDropped = dropped.nuclei + dropped.balls + dropped.cyano + dropped.links + dropped.genomes;
    return {
      schemaVersion: docVersion,
      meta: (json.meta && typeof json.meta === 'object' && !Array.isArray(json.meta)) ? json.meta : {},
      bounds: bounds,
      series: series,
      frames: frames,
      hasGenome: hasGenome,
      stats: {
        frames: frames.length,
        droppedTuples: totalDropped,
        detail: dropped
      }
    };
  }

  return { parse: parse, circSdDeg: circSdDeg };
})();
