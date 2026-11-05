/* agentsim 实时客户端 — 轮询 GET /state 二进制快照 · 零依赖（浏览器原生 API）
 *
 * R4-D：agentsim --serve 后端（R3-B）提供：
 *   GET /status  → {"ok":true,"phase":"running","tick":N}
 *   GET /state   → 小端二进制快照（本文件 parse 的唯一权威格式，与 src/app/main.cpp
 *                  buildStateSnapshot 逐字段对拍；实测 2026-11-01）：
 *
 *   头 80 字节：
 *     0  u8  magic = 0x41 ('A')
 *     1  u8  version = 1
 *     2  u8  状态（0=运行 1=已结束 2=暂停）
 *     3  u8  保留
 *     4  i32 tick
 *     8  f32 世界宽        12 f32 世界高
 *     16 i32 球数          20 i32 核数        24 i32 存活核数
 *     28 i32 自由球数      32 i32 蓝细菌节点数 36 i32 蓝细菌连线数
 *     40 f32 蓝细菌平均能量 44 f32 全局阳光
 *     48 i32 保留 + 52..79 保留 28 字节
 *   记录区（顺序 balls -> nuclei -> cyano -> links，全部小端）：
 *     球   25B：id(u32) owner(u32) x(f32) y(f32) energy(f32) type(u8) radius(f32)
 *     核   v2: 128B = 32B 头(id x y energy cap speed hue alive+pad3) + 24×f32 基因
 *          （v1: 32B 无基因；version 字节区分）
 *     蓝细菌 20B：id(u32) x(f32) y(f32) energy(f32) hue(f32)
 *     连线  8B：idA(u32) idB(u32)
 *
 * parse() 产出与 trace-parser.js 归一化输出**同构**的 frame 对象
 * （对象形态：nuclei/balls/cyano/cyanoLinks/genomes/stats），可直接交给
 * Renderer.draw / Main.updateAll，喂回 TraceParser.parse 也合法（对象形态容错）。
 *
 * 健壮性策略（卡面要求）：
 *   - magic / version 不匹配 → 抛错（绝不静默降级到乱码世界）；
 *   - stride（80 + b*25 + n*32 + c*20 + l*8）与 byteLength 不符 → 警告不崩，
 *     按可读前缀解析（onWarn 回调 / console.warn）；
 *   - 记录区被截断 → 警告 + 尽量多读；负数计数 → 按 0 处理 + 警告。
 */
window.LiveClient = (function () {
  'use strict';

  var LE = true;   // 快照为小端
  var MAX_SANE_ENTITIES = 20000000;  // 计数垃圾值防护（fit 按剩余字节截断兜底）

  // 与 trace-parser.js 的 defaultGenome() 同一套兜底值（/state 不携带基因组，
  // 核 genome 填默认并标 genomeReal=false，绝不造假数据；hue 是真遗传值）。
  function defaultGenome() {
    return {
      affinity: [1 / 3, 1 / 3, 1 / 3],
      orbitRadius: [18, 26, 15],
      absorbPreference: [1, 1, 1],
      repelStrength: [1, 1, 1],
      maxSpeed: 2,
      maxAccel: 0.2,
      energyThreshold: 160,
      mutationRate: 0.05,
      cyanoPreference: 0.6,
      cyanoDigestion: 0.7
    };
  }

  // hue 圆标准差（度）：与 TraceParser.circSdDeg 同式（R = |Σe^{iθ}|/n）。
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

  function normalizeBase(u) {
    if (!u) return '';
    var s = String(u);
    if (s.indexOf('://') < 0 && s.charAt(0) !== '/') s = 'http://' + s;
    while (s.length > 1 && s.charAt(s.length - 1) === '/') s = s.slice(0, -1);
    return s;
  }

  // 跨 realm 安全的 DataView 包装（node vm 冒烟里传入其它 realm 的 ArrayBuffer
  // 时 instanceof 会假阴；DataView 构造器认内部槽，跨 realm 可靠）。
  function toView(data) {
    try { return new DataView(data); } catch (e) { /* 非 ArrayBuffer，继续 */ }
    if (data && typeof data === 'object' && data.buffer) {
      try {
        return new DataView(data.buffer, data.byteOffset || 0, data.byteLength);
      } catch (e2) { /* 同上 */ }
    }
    throw new Error('/state 载荷必须是 ArrayBuffer/TypedArray（实际收到 ' +
      (data === null ? 'null' : typeof data) + '）');
  }

  // ── 二进制快照 → frame（与 trace-parser 归一化输出同构）────────
  function parse(data, opts) {
    var onWarn = (opts && typeof opts.onWarn === 'function') ? opts.onWarn : null;
    function warn(msg) {
      if (onWarn) onWarn(msg);
      else if (typeof console !== 'undefined' && console.warn) console.warn('[live] ' + msg);
    }
    var dv = toView(data);
    if (dv.byteLength < 80) {
      throw new Error('/state 快照过短：' + dv.byteLength + ' 字节 < 头部 80 字节');
    }
    var magic = dv.getUint8(0);
    if (magic !== 0x41) {
      throw new Error('magic 不匹配：期望 0x41 (A)，实际 0x' +
        magic.toString(16) + ' —— 这不是 agentsim /state 快照');
    }
    var version = dv.getUint8(1);
    if (version !== 1 && version !== 2) {
      throw new Error('/state 版本不匹配：期望 1 或 2，实际 ' + version +
        '（请更新前端或后端，两者必须同一版快照格式）');
    }
    var phase = dv.getUint8(2);                       // 0=运行 1=结束 2=暂停
    var tick = dv.getInt32(4, LE);
    var worldW = dv.getFloat32(8, LE);
    var worldH = dv.getFloat32(12, LE);
    var nB = dv.getInt32(16, LE);
    var nN = dv.getInt32(20, LE);
    var nAlive = dv.getInt32(24, LE);
    var nFree = dv.getInt32(28, LE);
    var nC = dv.getInt32(32, LE);
    var nL = dv.getInt32(36, LE);
    var cyanoAvgE = dv.getFloat32(40, LE);
    var sun = dv.getFloat32(44, LE);
    // R11-P1-6：结束原因（头部 52..79 保留区，NUL 结尾 ASCII；v2.3.1 finishReason 对位）
    var reason = '';
    for (var rqi = 52; rqi < 80; ++rqi) {
      var rq = dv.getUint8(rqi);
      if (!rq) break;
      reason += String.fromCharCode(rq);
    }

    // 负数计数（垃圾/截断头部）→ 按 0 + 警告
    var counts = [['球', nB], ['核', nN], ['存活核', nAlive], ['自由球', nFree],
      ['蓝细菌', nC], ['连线', nL]];
    for (var ci = 0; ci < counts.length; ci++) {
      if (counts[ci][1] < 0) {
        warn('头部 ' + counts[ci][0] + ' 计数为负（' + counts[ci][1] + '），按 0 处理');
      }
    }
    if (nB < 0) nB = 0; if (nN < 0) nN = 0; if (nAlive < 0) nAlive = 0;
    if (nFree < 0) nFree = 0; if (nC < 0) nC = 0; if (nL < 0) nL = 0;
    if (nB > MAX_SANE_ENTITIES || nN > MAX_SANE_ENTITIES ||
        nC > MAX_SANE_ENTITIES || nL > MAX_SANE_ENTITIES) {
      warn('头部计数异常巨大（球 ' + nB + ' 核 ' + nN + ' 蓝细菌 ' + nC +
        ' 连线 ' + nL + '），按剩余字节截断解析');
    }

    // stride 自检（卡面公式）：不符 → 警告不崩。R8-A：核步长随 version（v2=128B 含 24 基因）
    var need = 80 + nB * 25 + nN * ((version >= 2) ? 128 : 32) + nC * 20 + nL * 8;
    var strideWarn = (need !== dv.byteLength);
    if (strideWarn) {
      warn('stride 不匹配：头部计数需要 ' + need + ' 字节，实际载荷 ' + dv.byteLength +
        ' 字节 —— 按可读前缀解析（可能丢失尾部实体）');
    }

    var off = 80;
    var balls = [], nuclei = [], cyano = [], cyanoLinks = [];

    // 可读记录数 = min(声明数, 剩余字节能装下的条数)
    function fit(count, stride, name) {
      var remain = dv.byteLength - off;
      var avail = remain > 0 ? Math.floor(remain / stride) : 0;
      var take = Math.min(count, avail);
      if (take < count) {
        warn(name + ' 记录被截断：声明 ' + count + ' 条，仅能读 ' + take + ' 条');
      }
      return take;
    }

    // 球 25B：id(u32) owner(u32) x(f32) y(f32) energy(f32) type(u8) radius(f32)
    var tb = fit(Math.min(nB, MAX_SANE_ENTITIES), 25, '球');
    for (var b = 0; b < tb; b++) {
      if (off + 25 > dv.byteLength) break;
      var type = dv.getUint8(off + 20);
      balls.push({
        id: dv.getUint32(off, LE),
        owner: dv.getUint32(off + 4, LE),
        x: dv.getFloat32(off + 8, LE),
        y: dv.getFloat32(off + 12, LE),
        energy: dv.getFloat32(off + 16, LE),
        radius: dv.getFloat32(off + 21, LE),
        type: (type <= 2) ? type : 0
      });
      off += 25;
    }

    // 核记录：v1=32B（无基因）/ v2=128B（32B 头 + 24×f32 基因，顺序=Genome.h 声明）
    var NUC_REC = (version >= 2) ? 128 : 32;
    var tn = fit(Math.min(nN, MAX_SANE_ENTITIES), NUC_REC, '核');
    for (var n = 0; n < tn; n++) {
      if (off + NUC_REC > dv.byteLength) break;
      var nu = {
        id: dv.getUint32(off, LE),
        x: dv.getFloat32(off + 4, LE),
        y: dv.getFloat32(off + 8, LE),
        energy: dv.getFloat32(off + 12, LE),
        cap: dv.getFloat32(off + 16, LE),
        speed: dv.getFloat32(off + 20, LE),
        alive: dv.getUint8(off + 28) !== 0,
        forceTarget: -1, forceX: 0, forceY: 0,   // /state 无施力字段 → 渲染层安全默认
        hue: dv.getFloat32(off + 24, LE),
        hueReal: true,                            // 后端 genome.hue 是真遗传色相
        genome: defaultGenome(),
        genomeReal: false                         // v1 或解析失败时无假数据
      };
      if (version >= 2) {
        var go = off + 32, ok2 = true, g2 = defaultGenome();  // 基因区在 alive+pad3 之后（+32）
        try {
          for (var t2i = 0; t2i < 3; t2i++) g2.affinity[t2i] = dv.getFloat32(go + 4*t2i, LE);
          for (var t2i = 0; t2i < 3; t2i++) g2.orbitRadius[t2i] = dv.getFloat32(go + 12 + 4*t2i, LE);
          for (var t2i = 0; t2i < 3; t2i++) g2.absorbPreference[t2i] = dv.getFloat32(go + 24 + 4*t2i, LE);
          for (var t2i = 0; t2i < 3; t2i++) g2.repelStrength[t2i] = dv.getFloat32(go + 36 + 4*t2i, LE);
          g2.maxSpeed            = dv.getFloat32(go + 48, LE);
          g2.maxAccel            = dv.getFloat32(go + 52, LE);
          g2.energyThreshold     = dv.getFloat32(go + 56, LE);
          g2.mutationRate        = dv.getFloat32(go + 60, LE);
          g2.cyanoPreference     = dv.getFloat32(go + 64, LE);
          g2.cyanoDigestion      = dv.getFloat32(go + 68, LE);
          g2.nucleusSightRadius  = dv.getFloat32(go + 72, LE);
          g2.nucleusPersonalSpace= dv.getFloat32(go + 76, LE);
          g2.nucleusRepelStrength= dv.getFloat32(go + 80, LE);
          g2.nucleusCohesion     = dv.getFloat32(go + 84, LE);
          g2.forceRange          = dv.getFloat32(go + 88, LE);
          g2.forceCostK          = dv.getFloat32(go + 92, LE);
          for (var k2 in g2) { if (typeof g2[k2] === 'number' && !isFinite(g2[k2])) { ok2 = false; break; } }
          for (var t2i = 0; t2i < 3; t2i++) { if (!g2.affinity.every(isFinite) || !g2.orbitRadius.every(isFinite) || !g2.absorbPreference.every(isFinite) || !g2.repelStrength.every(isFinite)) { ok2 = false; break; } }
        } catch (e2) { ok2 = false; }
        if (ok2) { nu.genome = g2; nu.genomeReal = true; }  // 任一非有限值 → 整体回退默认（无假数据红线）
      }
      nuclei.push(nu);
      off += NUC_REC;
    }

    // 蓝细菌 20B：id(u32) x y energy hue(f32×4)
    var tc = fit(Math.min(nC, MAX_SANE_ENTITIES), 20, '蓝细菌');
    for (var c = 0; c < tc; c++) {
      if (off + 20 > dv.byteLength) break;
      cyano.push({
        id: dv.getUint32(off, LE),
        x: dv.getFloat32(off + 4, LE),
        y: dv.getFloat32(off + 8, LE),
        energy: dv.getFloat32(off + 12, LE),
        alive: true,                              // 快照只含活节点（帧末压缩语义）
        hue: dv.getFloat32(off + 16, LE),
        hueReal: true
      });
      off += 20;
    }

    // 连线 8B：idA(u32) idB(u32)；自环防御性丢弃（与 parseLinks 语义一致）
    var tl = fit(Math.min(nL, MAX_SANE_ENTITIES), 8, '连线');
    for (var l = 0; l < tl; l++) {
      if (off + 8 > dv.byteLength) break;
      var a = dv.getUint32(off, LE), b2 = dv.getUint32(off + 4, LE);
      if (a !== b2) cyanoLinks.push([a, b2]);
      off += 8;
    }

    // 统计：totalEnergy/cyanoEnergy 由实体逐个加总（display-only；账目权威在后端）
    var totalE = 0, cyanoE = 0;
    for (var s1 = 0; s1 < nuclei.length; s1++) totalE += nuclei[s1].energy;
    for (var s2 = 0; s2 < balls.length; s2++) totalE += balls[s2].energy;
    for (var s3 = 0; s3 < cyano.length; s3++) { cyanoE += cyano[s3].energy; totalE += cyano[s3].energy; }

    // 多样性（真 hue）：核与蓝细菌各算圆标准差
    var nucHues = [], cyHues = [];
    for (var h1 = 0; h1 < nuclei.length; h1++) nucHues.push(nuclei[h1].hue);
    for (var h2 = 0; h2 < cyano.length; h2++) cyHues.push(cyano[h2].hue);

    return {
      tick: tick,
      nuclei: nuclei,
      balls: balls,
      cyano: cyano,
      cyanoLinks: cyanoLinks,
      genomes: [],
      hueSd: circSdDeg(nucHues),
      cyanoHueSd: circSdDeg(cyHues),
      stats: {
        nuclei: (nAlive > 0 ? nAlive : nuclei.length),
        cyanoAlive: (cyano.length || nC),
        cyanoEnergy: cyanoE,
        totalEnergy: totalE,
        injected: 0,          // /state 无账本流量字段（审计权威在后端，恒等式不可见）
        dissipated: 0,
        auditOk: true
      },
      live: {
        phase: phase,           // 0=运行 1=已结束 2=暂停
        worldW: worldW,
        worldH: worldH,
        freeBalls: nFree,
        cyanoAvgEnergy: cyanoAvgE,
        sun: sun,
        reason: reason,
        strideWarn: strideWarn,
        bytes: dv.byteLength
      }
    };
  }

  // ── 轮询器状态 ────────────────────────────────────────────────
  var S = {
    active: false, url: '', timer: 0, intervalMs: 33, maxFailures: 4,
    failCount: 0, frames: 0, lastTick: -1, lastError: '', startedAt: 0,
    onFrame: null, onStatus: null, onWarn: null, onError: null
  };

  function status() {
    return {
      active: S.active, url: S.url, frames: S.frames, lastTick: S.lastTick,
      failCount: S.failCount, lastError: S.lastError,
      intervalMs: S.intervalMs, startedAt: S.startedAt
    };
  }

  function emitStatus() {
    if (typeof S.onStatus === 'function') {
      try { S.onStatus(status()); } catch (e) { /* 回调异常不拖垮轮询 */ }
    }
  }
  function emitWarn(msg) {
    if (typeof S.onWarn === 'function') S.onWarn(msg);
    else if (typeof console !== 'undefined' && console.warn) console.warn('[live] ' + msg);
  }
  function emitError(msg) {
    if (typeof S.onError === 'function') S.onError(msg);
    else if (typeof console !== 'undefined' && console.error) console.error('[live] ' + msg);
  }

  // 后端是否可用：GET /status 返回 {ok:true} 即在线。永远 resolve（不 reject）。
  function probe(baseUrl) {
    return new Promise(function (resolve) {
      var done = false;
      function fin(v) { if (!done) { done = true; resolve(v); } }
      if (typeof fetch !== 'function') { fin(false); return; }  // file:// / node stub
      var base = normalizeBase(baseUrl);
      var ctl = (typeof AbortController === 'function') ? new AbortController() : null;
      var timer = setTimeout(function () {
        if (ctl) { try { ctl.abort(); } catch (e) {} }
        fin(false);
      }, 2500);
      var init = ctl ? { method: 'GET', cache: 'no-store', signal: ctl.signal }
                     : { method: 'GET', cache: 'no-store' };
      try {
        fetch(base + '/status', init).then(function (r) {
          clearTimeout(timer);
          if (!r || !r.ok) { fin(false); return null; }
          return r.json().catch(function () { return null; });
        }).then(function (j) {
          fin(!!(j && j.ok === true));
        }).catch(function () {
          clearTimeout(timer); fin(false);
        });
      } catch (e) {
        clearTimeout(timer); fin(false);
      }
    });
  }

  // ── R6-D：行式 REST 小工具（/run /save /load /delete-save /seek /control）──
  // body 形如 'seed=42&nuclei=12&cyano_nodes=48&ticks=300'。成功 resolve 解析后的
  // JSON（非 JSON → {ok:true}）；HTTP 非 2xx（含运行态 seek 409）→ reject。
  function postForm(path, body) {
    if (typeof fetch !== 'function') {
      return Promise.reject(new Error('无 fetch（离线兜底模式）'));
    }
    return fetch(path, {
      method: 'POST', cache: 'no-store',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: String(body || '')
    }).then(function (r) {
      if (!r || !r.ok) throw new Error('POST ' + path + ' → HTTP ' + (r ? r.status : '??'));
      return r.json().catch(function () { return { ok: true }; });
    });
  }

  function getJson(path) {
    if (typeof fetch !== 'function') {
      return Promise.reject(new Error('无 fetch（离线兜底模式）'));
    }
    return fetch(path, { cache: 'no-store' }).then(function (r) {
      if (!r || !r.ok) throw new Error('GET ' + path + ' → HTTP ' + (r ? r.status : '??'));
      return r.json();
    });
  }

  // 拉一帧 /state 并 parse（回放 seek 后取帧 / 分析视图采样共用）
  function fetchState(baseUrl) {
    if (typeof fetch !== 'function') {
      return Promise.reject(new Error('无 fetch（离线兜底模式）'));
    }
    return fetch(normalizeBase(baseUrl) + '/state', { cache: 'no-store' })
      .then(function (r) {
        if (!r || !r.ok) throw new Error('GET /state → HTTP ' + (r ? r.status : '??'));
        return r.arrayBuffer();
      })
      .then(function (buf) { return parse(buf, { onWarn: function () {} }); });
  }

  function pollOnce() {
    if (!S.active) return;
    var init = (typeof fetch === 'function') ? { cache: 'no-store' } : undefined;
    fetch(S.url + '/state', init).then(function (r) {
      if (!r.ok) throw new Error('HTTP ' + r.status);
      return r.arrayBuffer();
    }).then(function (buf) {
      if (!S.active) return;
      var frame = parse(buf, { onWarn: emitWarn });   // magic/版本错会 throw → 走 catch
      S.failCount = 0;
      S.frames++;
      S.lastTick = frame.tick;
      if (typeof S.onFrame === 'function') {
        try { S.onFrame(frame); }
        catch (e) { emitError('onFrame 回调异常：' + (e && e.message || e)); }
      }
      if (S.active) S.timer = setTimeout(pollOnce, S.intervalMs);
    }).catch(function (e) {
      if (!S.active) return;
      S.failCount++;
      S.lastError = String((e && e.message) || e);
      emitError('拉取 /state 失败（连续第 ' + S.failCount + ' 次）：' + S.lastError);
      emitStatus();
      if (S.failCount >= S.maxFailures) {
        var wasUrl = S.url;
        stop();
        emitError('实时源连续失败 ' + S.maxFailures + ' 次，轮询已停止（' + wasUrl + '/state）');
        emitStatus();
      } else {
        S.timer = setTimeout(pollOnce, Math.min(3000, S.intervalMs * S.failCount));
      }
    });
  }

  function start(baseUrl, opts) {
    opts = opts || {};
    stop();
    S.active = true;
    S.url = normalizeBase(baseUrl);
    S.intervalMs = Math.max(16, Math.min(60000, (opts.intervalMs | 0) || 100));
    S.maxFailures = Math.max(1, (opts.maxFailures | 0) || 4);
    S.onFrame = typeof opts.onFrame === 'function' ? opts.onFrame : null;
    S.onStatus = typeof opts.onStatus === 'function' ? opts.onStatus : null;
    S.onWarn = typeof opts.onWarn === 'function' ? opts.onWarn : null;
    S.onError = typeof opts.onError === 'function' ? opts.onError : null;
    S.failCount = 0; S.frames = 0; S.lastTick = -1; S.lastError = '';
    S.startedAt = Date.now();
    S.timer = setTimeout(pollOnce, 0);
    emitStatus();
    return true;
  }

  function stop() {
    var was = S.active;
    S.active = false;
    if (S.timer) { clearTimeout(S.timer); S.timer = 0; }
    return was;
  }

  function isActive() { return S.active; }

  return {
    probe: probe,
    start: start,
    stop: stop,
    isActive: isActive,
    status: status,
    parse: parse,
    postForm: postForm,
    getJson: getJson,
    fetchState: fetchState,
    normalizeBase: normalizeBase,
    circSdDeg: circSdDeg
  };
})();
