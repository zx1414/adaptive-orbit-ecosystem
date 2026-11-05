/* agentsim 前端主控 — 单页应用（菜单/运行/参数/存档）· 零依赖
 *
 * 页面结构：#page-menu / #page-run / #page-params / #page-saves，showPage() 切换（无刷新）。
 * 运行页：全屏 canvas + 底部环境调控条（四轴五档/事件卡/细调抽屉）+
 *         右侧可折叠状态栏（统计+曲线）+ 播放控制 + 时间轴。
 *
 * R1-AMEND-1 落实：
 *   - 调控免费无限：状态栏只显示"已干预 N 次"计数。
 *   - 无战斗 UI：悬停 tooltip 展示 24 个基因组参数（社会/施力/生存/捕食），无战斗字段。
 *   - 每帧只画当前实体位置；施力脉冲由 Renderer 负责（数据来自 frame.nuclei[i].forceTarget）。
 *
 * R4-D 实时后端已接线（live-client.js 轮询 GET /state 二进制快照，与演示数据双源）：
 *   startRun(cfg)  → 先探测 GET /status：在线接实时流（onLiveFrame 滚动追加帧），
 *                    失败（file:// / 后端未启动）回退 DemoData.generate
 *   环境调控 setAxis/fire/applyPreset/cyano → 实时模式 POST /env（axis./event./cyano= 行式）
 *   播放控制       → 实时模式 POST /control（cmd=pause/resume/step；旧后端无此路由
 *                    时 toast 降级说明，仅本地暂停轮询——不崩）
 */
window.Main = (function () {
  'use strict';

  var $ = function (id) { return document.getElementById(id); };
  var U = window.Utils || { $: $, fmt: function (n) { return String(n); }, hsl: function (h, s, l) { return 'hsl(' + h + ',' + s + '%,' + l + '%)'; } };

  var addWin = (typeof window !== 'undefined' && window.addEventListener)
    ? function (ev, fn, opt) { window.addEventListener(ev, fn, opt); }
    : function () {};
  var raf = (typeof window !== 'undefined' && window.requestAnimationFrame)
    ? function (cb) { return window.requestAnimationFrame(cb); }
    : function () { return 0; };

  // 安全绑定：元素缺失时静默跳过（保证 node DOM-stub 冒烟可干净求值）
  function on(id, ev, fn, opt) {
    var e = $(id);
    if (e && e.addEventListener) e.addEventListener(ev, fn, opt);
  }

  // ── 全局状态 ─────────────────────────────────────────────────
  var state = {
    page: 'menu',
    trace: null,          // TraceParser.parse 的产物
    series: [],           // 展示用序列（补派生键 auditDelta）
    idx: 0,
    playing: false, loop: true, speed: 1, accum: 0, lastTs: 0,
    FPS_BASE: 24,
    cinema: false,
    hoverIndex: -1,
    pinnedCards: {},        // R8-D: nucleusId -> true (pinned param cards)
    pinnedSeq: 0,           // dom id counter
    frameCount: 0,
    env: { light: 2, nutrient: 2, pressure: 2, producer: 2 },  // 四轴，0..4 五档
    interventions: 0,     // "已干预 N 次"计数（调控免费，无预算）
    dirty: false,         // 有未提交到"真实后端"的调控（演示轮恒 true after ops）
    params: {},           // 参数页当前值（32 键）
    paramsEdited: {},
    cfg: null,            // 菜单页配置
    demoTrace: null,
    live: false,          // R4-D：实时模式（LiveClient 轮询 /state）
    liveWorld: null,      // 实时世界尺寸缓存（变化时才重 fitCamera）
    envSegs: {},          // axisId → {seg, name}
    chart: {},            // pop/cyano/energy/audit = 折线；scatter = 基因散点；hue = 多样性
    online: null,         // R6-D：菜单探测结果 true=后端在线 false=离线 null=探测中
    timeline: null,       // R6-D：GET /timeline 缓存 {checkpoints:[frame...], current}
    seeking: false,       // R6-D：seek 在途（拖动节流）
    replay: false,        // R6-D：回放态（徽标 + 屏蔽实时帧落地）
    // R7-D 调色板：renderer DEFAULT_PALETTE 的镜像（'r,g,b' 字符串）。真源在
    // renderer 内部；boot 时 GET /packs → active 覆盖默认，离线/404 保持内置默认。
    palette: {
      background: '10,14,26',
      ball_shield: '255,207,94',
      ball_worker: '69,224,216',
      ball_scout: '183,139,255',
      free_ball: '200,200,210',
      nucleus: '255,255,255'
    },
    packs: [],            // R7-D：GET /packs 的包列表缓存（[] = 未拉到/离线）
    packsOnline: null,    // R7-D：材质包区在线态 true/false/null（探测中）
  };

  var renderer = null, canvas = null;

  // R4-D：曲线键规格（loadTrace 与实时追加共用，避免两处漂移）
  var CHART_KEYS = {
    pop: [
      { key: 'nuclei', color: '#5ad1ff', label: '核' },
      { key: 'cyanoAlive', color: '#48dc80', label: '蓝细菌' }
    ],
    cyano: [{ key: 'cyanoEnergy', color: '#48dc80', label: '蓝细菌能量' }],
    energy: [{ key: 'totalEnergy', color: '#ffa656', label: '总能量' }],
    audit: [{ key: 'auditDelta', color: '#ff6b81', label: '|审计差值|' }],
    hue: [
      { key: 'hueSd', color: '#c792ff', label: '核 hue 圆标准差' },
      { key: 'cyanoHueSd', color: '#48dc80', label: '蓝细菌 hue 圆标准差' }
    ]
  };

  // ── 通用 ─────────────────────────────────────────────────────
  function toast(msg) {
    var elToast = $('toast');
    if (!elToast) return;
    elToast.textContent = msg;
    elToast.classList.add('on');
    clearTimeout(toast._t);
    toast._t = setTimeout(function () { elToast.classList.remove('on'); }, 2400);
  }

  var PAGE_IDS = ['menu', 'run', 'params', 'saves'];
  function showPage(name) {
    state.page = name;
    for (var i = 0; i < PAGE_IDS.length; i++) {
      var el = $('page-' + PAGE_IDS[i]);
      if (el) el.classList.toggle('active', PAGE_IDS[i] === name);
    }
    var nav = $('navBtns');
    if (nav) {
      var btns = nav.querySelectorAll('[data-page]');
      for (var b = 0; b < btns.length; b++) {
        btns[b].classList.toggle('active', btns[b].getAttribute('data-page') === name);
      }
    }
    if (name === 'run' && renderer) {
      renderer.resize();
      resizeCharts();
      updateAll(true);
    }
    if (name === 'saves') refreshSaves();   // R6-D：每次进存档页重读 GET /saves
    // R7-D：进参数页时若在线但还没拉到包列表（boot 时后端未起），补拉一次
    if (name === 'params' && state.packsOnline !== true &&
        window.LiveClient && window.LiveClient.getJson && typeof fetch === 'function') {
      window.LiveClient.getJson('/packs').then(function (j) {
        if (applyPaletteActive(j && j.active)) {
          state.packs = Array.isArray(j.packs) ? j.packs : [];
          state.packsOnline = true;
          renderPacks();
        }
      }).catch(function () {});
    }
  }

  function resizeCharts() {
    var c = state.chart;
    if (c.pop) c.pop.resize();
    if (c.cyano) c.cyano.resize();
    if (c.energy) c.energy.resize();
    if (c.audit) c.audit.resize();
    if (c.scatter) c.scatter.resize();
    if (c.hue) c.hue.resize();
  }

  // ── 轨迹载入（实时后端接线点：换成 ws/HTTP 帧流即可）─────────
  function loadTrace(raw, label) {
    var t;
    try {
      if (!window.TraceParser || !window.TraceParser.parse) throw new Error('TraceParser 模块未加载');
      t = window.TraceParser.parse(raw);
    } catch (err) {
      toast('轨迹校验失败：' + err.message);
      return false;
    }
    state.trace = t;
    state.idx = 0; state.accum = 0; state.lastTs = 0; state.hoverIndex = -1;
  if (state.pinnedCards) { for (var pid in state.pinnedCards) unpinCard(pid); }  // R8-D 换局清卡
    var tip = $('tip');
    if (tip) tip.classList.remove('on');
    renderer.setTrace(t);
    var last = Math.max(0, t.frames.length - 1);
    var tl = $('timeline');
    if (tl) { tl.max = String(last); tl.value = '0'; }
    var tlMax = $('tlMax');
    if (tlMax) tlMax.textContent = '/ t' + last;
    var m = t.meta || {};
    var chips = $('metaChips');
    if (chips) {
      chips.textContent = (m.contractVersion ? ('契约 v' + m.contractVersion + ' · ') : '契约 v' + t.schemaVersion + ' · ') + 'seed ' + (m.seed == null ? '?' : m.seed) +
        ' · ' + t.frames.length + ' 帧 · ' +
        (m.world ? m.world.width + '×' + m.world.height : '?') +
        (t.hasGenome ? ' · 基因组导出' : '') +
        ' · ' + label;
    }
    // 派生序列：|审计差值|（injected - dissipated - totalEnergy，应恒为 ~0）
    state.series = (t.series || []).map(function (s) {
      var d = 0;
      if (typeof s.injected === 'number' && typeof s.dissipated === 'number' && typeof s.totalEnergy === 'number') {
        d = Math.abs(s.injected - s.dissipated - s.totalEnergy);
      }
      var o = {};
      for (var k in s) if (Object.prototype.hasOwnProperty.call(s, k)) o[k] = s[k];
      o.auditDelta = d;
      return o;
    });
    if (state.chart.pop) state.chart.pop.setData(state.series, CHART_KEYS.pop);
    if (state.chart.cyano) state.chart.cyano.setData(state.series, CHART_KEYS.cyano);
    if (state.chart.energy) state.chart.energy.setData(state.series, CHART_KEYS.energy);
    if (state.chart.audit) state.chart.audit.setData(state.series, CHART_KEYS.audit);
    // 多样性曲线数据源 = frames（hueSd 按帧计算；series 每 tick 一条而帧被降采样，
    // 用 frames 派生序列才能与时间轴游标同下标对齐）
    state.hueSeries = (t.frames || []).map(function (fr) {
      return { tick: fr.tick, hueSd: fr.hueSd, cyanoHueSd: fr.cyanoHueSd };
    });
    if (state.chart.hue) state.chart.hue.setData(state.hueSeries, CHART_KEYS.hue);
    var dr = t.stats && t.stats.droppedTuples ? '，剔除异常元组 ' + t.stats.droppedTuples : '';
    toast('已载入：' + label + '（' + t.frames.length + ' 帧' + dr + '）');
    state.frameCount = t.frames.length;
    updateAll(true);
    return true;
  }

  // ── 帧更新 ───────────────────────────────────────────────────
  function fmt(n, d) { return U.fmt(n, d); }

  function updateAll(force, keepTimeline) {
    if (!state.trace) return;
    var frame = state.trace.frames[state.idx];
    if (!frame) return;
    renderer.draw(frame);
    if (!keepTimeline) {   // R6-D：seek 在途时轮询回调不得覆盖游标/帧号
      var tl = $('timeline');
      if (tl) tl.value = String(state.idx);
      var tlCur = $('tlCur');
      if (tlCur) tlCur.textContent = 't ' + frame.tick;
    }
    updateMetrics(frame);
    updateHud(frame);
    if (state.chart.pop) state.chart.pop.setCursor(state.idx);
    if (state.chart.cyano) state.chart.cyano.setCursor(state.idx);
    if (state.chart.energy) state.chart.energy.setCursor(state.idx);
    if (state.chart.audit) state.chart.audit.setCursor(state.idx);
    if (state.chart.hue) state.chart.hue.setCursor(state.idx);
    updateScatter(frame);
    if (state.hoverIndex >= 0) refreshHover(frame, force);
    refreshPinnedCards(frame);   // R8-D：钉住卡每帧跟随数据（死亡自动解钉）
  }

  // R11-P1-6：结束原因（extinct/max_ticks 来自 /state 头 52..79 保留区）
  var REASON_TXT = { extinct: '灭绝', max_ticks: '到达帧上限' };
  function END_REASON_METRIC(frame) {
    var r = frame && frame.live && frame.live.reason;
    if (!r || r === 'running' || r === 'paused') return '';
    return '<div class="metric wide"><div class="k">结束原因</div><div class="v bad">' +
      (REASON_TXT[r] || r) + '</div></div>';
  }

  function updateMetrics(frame) {
    var box = $('metrics');
    if (!box) return;
    var s = frame.stats || {};
    var ok = s.auditOk !== false;
    var hueSdStr = (typeof frame.hueSd === 'number' && isFinite(frame.hueSd)) ? frame.hueSd.toFixed(1) + '°' : '—（无 g）';
    box.innerHTML =
      '<div class="metric"><div class="k">核数</div><div class="v cy">' + (s.nuclei != null ? s.nuclei : frame.nuclei.length) + '</div></div>' +
      '<div class="metric"><div class="k">蓝细菌</div><div class="v ok">' + (s.cyanoAlive != null ? s.cyanoAlive : frame.cyano.length) + '</div></div>' +
      '<div class="metric"><div class="k">总能量</div><div class="v am">' + fmt(s.totalEnergy, 1) + '</div></div>' +
      '<div class="metric"><div class="k">帧号</div><div class="v cy">' + frame.tick + '</div></div>' +
      '<div class="metric wide"><div class="k">累计注入 / 耗散</div><div class="v">' + fmt(s.injected, 1) + ' / ' + fmt(s.dissipated, 1) + '</div></div>' +
      '<div class="metric wide"><div class="k">蓝细菌连线</div><div class="v cy">' + (frame.cyanoLinks ? frame.cyanoLinks.length : 0) + '</div></div>' +
      '<div class="metric wide"><div class="k">谱系多样性（hue 圆标准差）</div><div class="v">' + hueSdStr + '</div></div>' +
      '<div class="metric wide"><div class="k">已干预（免费）</div><div class="v">' + state.interventions + '</div></div>' +
      '<div class="metric wide"><div class="k">能量审计</div><div class="v ' + (ok ? 'ok' : 'bad') + '">' + (ok ? 'PASS' : 'FAIL') + '</div></div>' +
      '<div class="metric wide"><div class="k">数据源</div><div class="v ' + (state.live ? 'ok' : '') + '">' +
      (state.live ? (state.replay ? '实时 /state · 回放' : '实时 /state') : '离线演示') + '</div></div>';
    if (box) box.innerHTML += END_REASON_METRIC(frame);
    var st = frame.live;
    if (box) box.classList.toggle('replaying', !!(state.live && st && st.replay));
  }

  function updateHud(frame) {
    var hud = $('hud');
    if (!hud) return;
    var frees = 0;
    for (var i = 0; i < frame.balls.length; i++) {
      if (!frame.balls[i].owner) frees++;
    }
    var linkN = frame.cyanoLinks ? frame.cyanoLinks.length : 0;
    var linkTxt = '（连线 ' + linkN + (linkN > 4000 ? '，抽稀显示' : '') + '）';
    var sunTxt = (frame.live && isFinite(frame.live.sun)) ? ' · 阳光 ' + frame.live.sun.toFixed(2) : '';
    hud.textContent = 't=' + frame.tick +
      ' · 核 ' + frame.nuclei.length +
      ' · 球 ' + frame.balls.length + '（自由 ' + frees + '）' +
      ' · 蓝细菌 ' + frame.cyano.length + linkTxt + sunTxt +
      ' · ΔE ' + fmt(Math.abs((frame.stats.injected || 0) - (frame.stats.dissipated || 0) - (frame.stats.totalEnergy || 0)), 2);
  }

  // ── 播放控制 ─────────────────────────────────────────────────
  function setPlaying(on, opts) {
    state.playing = on;
    if (on) { state.replay = false; setReplayBadge(false); }   // R6-D：resume 恢复实时，退回放态
    if (!(opts && opts.silent) && state.live) {
      postControl(on ? 'resume' : 'pause');   // 单一分发点：setPlaying 负责 POST /control
    }
    var b = $('btnPlay');
    if (b) {
      b.textContent = on ? '⏸' : '▶';
      b.title = on ? '暂停（空格）' : '播放（空格）';
    }
  }

  function stepTo(i) {
    if (!state.trace) return;
    var max = state.trace.frames.length - 1;
    var next = Math.max(0, Math.min(max, i));
    if (next === state.idx && !state.playing) return;
    state.idx = next;
    updateAll();
  }

  function tick(ts) {
    raf(tick);
    if (!state.trace || state.page !== 'run') { state.lastTs = 0; return; }
    if (state.live) { state.lastTs = 0; return; }   // 实时模式：帧由 LiveClient 轮询驱动
    if (state.playing) {
      if (!state.lastTs) state.lastTs = ts;
      state.accum += (ts - state.lastTs) / 1000 * state.speed * state.FPS_BASE;
      state.lastTs = ts;
      if (state.accum >= 1) {
        var adv = Math.floor(state.accum);
        state.accum -= adv;
        var max = state.trace.frames.length - 1;
        var next = state.idx + adv;
        if (next > max) {
          if (state.loop) { state.idx = next % (max + 1); updateAll(); }
          else { state.idx = max; setPlaying(false); updateAll(); }
        } else { state.idx = next; updateAll(); }
      }
    } else { state.lastTs = 0; }
  }

  function setSpeed(v) {
    state.speed = v;
    var seg = $('speedSeg');
    if (seg) {
      var btns = seg.querySelectorAll('button[data-v]');
      for (var i = 0; i < btns.length; i++) {
        btns[i].classList.toggle('active', parseFloat(btns[i].getAttribute('data-v')) === v);
      }
    }
    toast('速度 ' + v + '×');
  }

  // ── 基因散点图：X=maxSpeed，Y=energyThreshold，点=核，颜色=hue ──
  // 数据来自帧的 g 键（trace.hasGenome）或解析归一化的 nucleus.genome（v2 对象/演示数据）。
  // 生态含义：演化分化出「快而饿 / 慢而富」策略簇 → 谱系色团在基因空间可见（GOAL-ECOLOGY §判据4）。
  function updateScatter(frame) {
    var sc = state.chart.scatter;
    if (!sc || !frame) return;
    var pts = [];
    var genomes = frame.genomes || [];
    if (genomes.length) {
      // v3 g 键：直接读扁平基因（GIDX 与 Genome.h 声明序一致）
      for (var i = 0; i < genomes.length; i++) {
        var gr = genomes[i];
        pts.push({
          x: gr.genome.maxSpeed,
          y: gr.genome.energyThreshold,
          hue: gr.hue,
          energy: (frame.nuclei[i] ? frame.nuclei[i].energy : null),
          id: gr.id,
          genome: gr.genome,
          genomeReal: true
        });
      }
    } else {
      // 回退：只用解析时确认真实的基因组（v2 对象 hue/genome、演示数据）。
      // 无 g 的真 v3 轨迹核 genome 是默认兜底值（genomeReal=false）——画出来是
      // 「所有点挤在一团」的假分群，宁可不画（面板显示“暂无数据”）也不造假数据。
      for (var j = 0; j < frame.nuclei.length; j++) {
        var nu = frame.nuclei[j];
        if (!nu.genome || !nu.genomeReal) continue;
        pts.push({
          x: nu.genome.maxSpeed,
          y: nu.genome.energyThreshold,
          hue: nu.hue,
          energy: nu.energy,
          id: nu.id,
          genome: nu.genome,
          genomeReal: true
        });
      }
    }
    sc.setData(pts, { xKey: 'maxSpeed', yKey: 'energyThreshold' });
  }

  // 散点图悬停：命中即复用核 tooltip（24 基因全量展示）
  function bindScatterHover(canvas, chart) {
    if (!canvas) return;
    canvas.addEventListener('pointermove', function (e) {
      var rect = canvas.getBoundingClientRect();
      var hit = chart.hitTest(e.clientX - rect.left, e.clientY - rect.top);
      var tip = $('tip');
      if (hit >= 0 && state.trace) {
        var p = chart.pts()[hit];
        var frame = state.trace.frames[state.idx];
        var nu = null;
        for (var i = 0; i < frame.nuclei.length; i++) if (frame.nuclei[i].id === p.id) { nu = frame.nuclei[i]; break; }
        if (tip && nu) {
          tip.innerHTML = renderTooltip(nu);
          tip.classList.add('on');
          tip.style.left = Math.min((window.innerWidth || 1200) - 340, e.clientX + 14) + 'px';
          tip.style.top = (e.clientY + 12) + 'px';
        }
      } else if (tip) tip.classList.remove('on');
    });
    canvas.addEventListener('pointerleave', function () {
      chart.clearHover();
      var tip = $('tip');
      if (tip) tip.classList.remove('on');
    });
  }

  // ── 悬停 tooltip：核的全部基因组参数（无战斗字段）────────────
  function refreshHover(frame, keepVisible) {
    var tip = $('tip');
    if (!tip) return;
    if (state.hoverIndex < 0) { tip.classList.remove('on'); return; }
    var nu = frame.nuclei[state.hoverIndex];
    if (!nu) {
      if (!keepVisible) { state.hoverIndex = -1; tip.classList.remove('on'); }
      return;
    }
    tip.innerHTML = renderTooltip(nu);
    tip.classList.add('on');
    positionTip(tip, state.hoverX, state.hoverY);
  }

  function positionTip(tip, x, y) {
    var vw = window.innerWidth || 1200, vh = window.innerHeight || 800;
    var w = tip.offsetWidth || 320, h = tip.offsetHeight || 260;
    var left = x + 16, top = y + 14;
    if (left + w > vw - 10) left = x - w - 14;
    if (top + h > vh - 10) top = Math.max(10, vh - h - 10);
    if (left < 10) left = 10;
    tip.style.left = left + 'px';
    tip.style.top = top + 'px';
  }

  var TYPE_NAMES = ['资源', '侦察', '护盾'];

  // ── R8-D 参数卡钉住：单击核 → 持久挂载（多卡并存、×关闭、死亡自动解钉）──
  function ensureCardLayer() {
    var layer = $('pinnedCards');
    if (!layer) {
      layer = document.createElement('div');
      layer.id = 'pinnedCards';
      document.body.appendChild(layer);
    }
    return layer;
  }
  function pinCard(nu) {
    if (!nu || state.pinnedCards[nu.id]) return;
    state.pinnedCards[nu.id] = true;
    var layer = ensureCardLayer();
    var card = document.createElement('div');
    card.className = 'pcard';
    card.id = 'pcard_' + nu.id;
    card.innerHTML = '<div class="pcard-head"><span class="pcard-title">核 #' + nu.id + '</span>' +
      '<button class="pcard-x" title="关闭">×</button></div>' +
      '<div class="pcard-body">' + renderTooltip(nu) + '</div>';
    layer.appendChild(card);
    var x = card.querySelector('.pcard-x');
    if (x) x.addEventListener('click', function () { unpinCard(nu.id); });
    toast('已钉住核 #' + nu.id + ' 参数卡（×关闭）');
  }
  function unpinCard(id) {
    delete state.pinnedCards[id];
    var el = document.getElementById('pcard_' + id);
    if (el && el.parentNode) el.parentNode.removeChild(el);
  }
  function refreshPinnedCards(frame) {
    if (!frame) return;
    var alive = {};
    for (var i = 0; i < frame.nuclei.length; i++) alive[frame.nuclei[i].id] = frame.nuclei[i];
    for (var id in state.pinnedCards) {
      if (!alive[id]) { unpinCard(id); continue; }   // 核死亡自动解钉
      var body = document.querySelector('#pcard_' + id + ' .pcard-body');
      if (body) body.innerHTML = renderTooltip(alive[id]);   // 每帧刷新（含 /state v2 基因）
    }
  }

  function renderTooltip(nu) {
    var g = nu.genome || {};
    var rows = '';
    function row(k, v, cls) {
      return '<div class="trow"><span class="tk">' + k + '</span><span class="tv ' + (cls || '') + '">' + v + '</span></div>';
    }
    function sec(t) { return '<div class="tsec">' + t + '</div>'; }
    function arr(name, key, digits) {
      var a = g[key] || [0, 0, 0];
      var out = sec(name);
      for (var i = 0; i < 3; i++) {
        out += row(TYPE_NAMES[i], (typeof a[i] === 'number' ? a[i].toFixed(digits) : '—'));
      }
      return out;
    }
    var hue = ((typeof nu.hue === 'number' && isFinite(nu.hue)) ? nu.hue : 210);
    hue = ((hue % 360) + 360) % 360;
    var ft = (typeof nu.forceTarget === 'number' && nu.forceTarget >= 0) ? ('球 #' + nu.forceTarget) : '本帧无';

    rows += sec('帧态');
    rows += row('核 id', nu.id);
    rows += row('能量 / 上限', fmt(nu.energy, 1) + ' / ' + fmt(nu.cap, 0));
    rows += row('速度', fmt(nu.speed, 2) + ' px/帧');
    rows += row('施力目标', ft);

    rows += arr('类型偏好 affinity', 'affinity', 3);
    rows += arr('轨道半径 orbitRadius', 'orbitRadius', 1);
    rows += arr('吸收倍率 absorbPreference', 'absorbPreference', 2);
    rows += arr('近距排斥 repelStrength', 'repelStrength', 2);

    rows += sec('社会参数（对其它核）');
    rows += row('察觉半径 nucleusSightRadius', fmt(g.nucleusSightRadius, 0));
    rows += row('个人空间 nucleusPersonalSpace', fmt(g.nucleusPersonalSpace, 0));
    rows += row('斥力强度 nucleusRepelStrength', fmt(g.nucleusRepelStrength, 2));
    rows += row('聚拢倾向 nucleusCohesion', fmt(g.nucleusCohesion, 2));

    rows += sec('施力能力');
    rows += row('控制范围 forceRange', fmt(g.forceRange, 0));
    rows += row('代价系数 forceCostK', fmt(g.forceCostK, 4));

    rows += sec('生存');
    rows += row('极速 maxSpeed', fmt(g.maxSpeed, 2));
    rows += row('最大加速度 maxAccel', fmt(g.maxAccel, 3));
    rows += row('繁殖阈值 energyThreshold', fmt(g.energyThreshold, 0));
    rows += row('变异率 mutationRate', fmt(g.mutationRate, 3));

    rows += sec('捕食（蓝细菌 = 食物）');
    rows += row('捕食偏好 cyanoPreference', fmt(g.cyanoPreference, 2));
    rows += row('消化效率 cyanoDigestion', fmt(g.cyanoDigestion, 2));

    rows += sec('遗传色相');
    rows += '<div class="trow"><span class="tk">hue' +
      (nu.hueReal === false ? '（合成兜底色，非遗传）' : '（真遗传）') + '</span><span class="tv">' +
      '<i class="hueswatch" style="background:' + U.hsl(hue, 72, 58) + '"></i>' + hue.toFixed(1) + '°</span></div>';
    rows += '<div class="thint">提示：点按该核可跟随视角；社会/施力/捕食参数即行为层实时读取的基因。</div>';
    return rows;
  }

  // ── 运行页布局切换（折叠/影院）───────────────────────────────
  function setStatusCollapsed(on) {
    if (document.body) document.body.classList.toggle('statushidden', on);
    var b = $('btnStatus');
    if (b) b.classList.toggle('active', on);
    renderer.resize();
    resizeCharts();
    updateAll(true);
  }
  function setToolbarHidden(on) {
    if (document.body) document.body.classList.toggle('toolhidden', on);
    var b = $('btnToolbar');
    if (b) b.classList.toggle('active', on);
    renderer.resize();
    updateAll(true);
  }
  function setCinema(on) {
    state.cinema = on;
    if (document.body) document.body.classList.toggle('cinema', on);
    renderer.resize();
    resizeCharts();
    updateAll(true);
    toast(on ? '影院模式：F / Esc 退出' : '已退出影院模式');
    raf(function () { renderer.resize(); updateAll(true); });
  }

  // ── 环境调控条（全部免费）───────────────────────────────────
  var AXES = [
    { id: 'light', label: '光照' },
    { id: 'nutrient', label: '蓝细菌' },
    { id: 'pressure', label: '环境压力' },
    { id: 'producer', label: '生产者' }
  ];
  var LEVELS = ['很低', '偏低', '中性', '偏高', '很高'];
  var AXIS_DESC = {
    light: '阳光模型档位：sunBase / sunAmplitude',
    nutrient: '蓝细菌光合与连线传输档位',
    pressure: '代谢与衰减档位：metabolismCost / decayRate',
    producer: '生产者存量档位：初始节点与繁殖成本'
  };

  // ── R7-D 调色板消费 + 材质包 UI ─────────────────────────────
  // 数据流：boot → GET /packs → active → Renderer.setPalette（逐键校验回退）
  // → state.palette 回写 → 下一帧 draw 立即用新色（无需刷新）。
  // 离线 / 404 / 坏 JSON → 保持内置默认色，file:// 双击不崩（本卡底线）。
  function applyPaletteActive(active) {
    if (!active || typeof active !== 'object') return false;
    if (!window.Renderer || !Renderer.setPalette) return false;
    var eff = window.Renderer.setPalette(active);   // 非法键逐项回退默认（坏包不黑屏）
    for (var k in eff) state.palette[k] = eff[k];
    return true;
  }

  function fetchPalette() {
    var LCX = window.LiveClient;
    if (!LCX || typeof fetch !== 'function' || !LCX.getJson) return;   // 离线：默认色
    LCX.getJson('/packs').then(function (j) {
      if (applyPaletteActive(j && j.active)) {
        state.packs = Array.isArray(j.packs) ? j.packs : [];
        state.packsOnline = true;
        renderPacks();   // 参数页若已构建则刷新勾选态
      }
    }).catch(function () { /* 404/网络失败：默认色，静默（R7-A 未上线也正常） */ });
  }

  // 勾选变化 → POST /set-packs（行式 enabled=p1,p2）→ 重拉 active → 立即重渲染
  function postSetPacks() {
    var LCX = window.LiveClient;
    if (!LCX || !LCX.postForm) return Promise.reject(new Error('无本地服务'));
    var enabled = [];
    for (var i = 0; i < state.packs.length; i++) {
      if (state.packs[i].enabled) enabled.push(state.packs[i].name);
    }
    var body = 'enabled=' + enabled.map(function (n) { return encodeURIComponent(n); }).join(',');
    return LCX.postForm('/set-packs', body).then(function () {
      return LCX.getJson('/packs');
    }).then(function (j) {
      if (applyPaletteActive(j && j.active)) {
        state.packs = Array.isArray(j.packs) ? j.packs : state.packs;
        renderPacks();
      }
      // 下一帧渲染即用新调色板；回放/暂停时也强制画一帧，立刻看到换肤
      if (state.trace) updateAll(true);
    });
  }

  // 参数页「材质包」区：在线渲染包列表（勾选=enabled），离线显示「需要本地服务」
  function renderPacks() {
    var box = $('packsBox');
    if (!box) return;
    box.innerHTML = '';
    if (state.packsOnline !== true) {
      var p = document.createElement('div');
      p.className = 'packs-empty';
      p.textContent = '🎨 材质包需要本地服务：运行 build\agentsim.exe --serve 8080 后可用（离线用内置默认色）';
      box.appendChild(p);
      return;
    }
    var list = document.createElement('div');
    list.className = 'packs-list';
    for (var i = 0; i < state.packs.length; i++) {
      (function (pk) {
        var row = document.createElement('label');
        row.className = 'pack-row';
        var cb = document.createElement('input');
        cb.type = 'checkbox';
        cb.checked = !!pk.enabled;
        cb.setAttribute('aria-label', '启用材质包 ' + pk.name);
        cb.addEventListener('change', function () {
          pk.enabled = cb.checked;
          postSetPacks().catch(function (e) {
            pk.enabled = !cb.checked;   // 失败回滚勾选（服务端是权威）
            cb.checked = pk.enabled;
            toast('材质包切换失败：' + (e.message || e));
          });
        });
        var txt = document.createElement('span');
        txt.className = 'pack-text';
        var nm = document.createElement('span');
        nm.className = 'pack-name';
        nm.textContent = pk.name + (pk.version ? ' v' + pk.version : '');
        var ds = document.createElement('span');
        ds.className = 'pack-desc';
        ds.textContent = (pk.desc || '') + (pk.author ? ' · ' + pk.author : '');
        txt.appendChild(nm);
        txt.appendChild(ds);
        row.appendChild(cb);
        row.appendChild(txt);
        list.appendChild(row);
      })(state.packs[i]);
    }
    if (!state.packs.length) {
      var empty = document.createElement('div');
      empty.className = 'packs-empty';
      empty.textContent = '后端未注册任何材质包';
      list.appendChild(empty);
    }
    box.appendChild(list);
  }

  function buildEnv() {
    var wrapEl = $('envAxes');
    if (!wrapEl) return;
    wrapEl.innerHTML = '';
    for (var i = 0; i < AXES.length; i++) {
      (function (axis) {
        var box = U.el ? U.el('div', 'env-axis') : document.createElement('div');
        box.className = 'env-axis';
        var head = document.createElement('div');
        head.className = 'env-head';
        var name = document.createElement('span');
        name.className = 'env-name';
        name.textContent = axis.label;
        var lvName = document.createElement('span');
        lvName.className = 'env-lv mono';
        lvName.textContent = LEVELS[state.env[axis.id]];
        head.appendChild(name);
        head.appendChild(lvName);
        var seg = document.createElement('div');
        seg.className = 'seg env-seg';
        for (var lv = 0; lv < 5; lv++) {
          (function (l) {
            var btn = document.createElement('button');
            btn.type = 'button';
            btn.textContent = String(l + 1);
            btn.title = axis.label + ' ' + LEVELS[l];
            btn.classList.toggle('active', l === state.env[axis.id]);
            btn.addEventListener('click', function () { setAxis(axis.id, l); });
            seg.appendChild(btn);
          })(lv);
        }
        box.appendChild(head);
        box.appendChild(seg);
        wrapEl.appendChild(box);
        state.envSegs[axis.id] = { seg: seg, name: lvName, label: axis.label };
      })(AXES[i]);
    }
  }

  function setAxis(axisId, lv, opts) {
    lv = Math.max(0, Math.min(4, lv | 0));
    if (state.env[axisId] === lv) return;
    state.env[axisId] = lv;
    var rec = state.envSegs[axisId];
    if (rec) {
      var btns = rec.seg.querySelectorAll('button');
      for (var i = 0; i < btns.length; i++) btns[i].classList.toggle('active', i === lv);
      rec.name.textContent = LEVELS[lv];
    }
    if (!opts || !opts.silent) {
      state.interventions++;
      state.dirty = true;
      var label = rec ? rec.label : axisId;
      toast(label + ' → ' + LEVELS[lv] + '（免费 · 立即生效）');
      updateAll(true);
      if (state.live) postEnvLines(['axis.' + (LIVE_AXIS_ID[axisId] || axisId) + '=' + lv]);
    }
  }

  function describeEnv() {
    var dirty = state.dirty ? ' · 已改动*' : '';
    var parts = [];
    for (var i = 0; i < AXES.length; i++) {
      parts.push(AXES[i].label + ' ' + LEVELS[state.env[AXES[i].id]]);
    }
    var el = $('envDesc');
    if (el) el.textContent = parts.join(' · ') + dirty;
    var el2 = $('envHint');
    if (el2) el2.textContent = '换档免费无限制 · ' + AXIS_DESC.pressure;
  }

  var EVENTS = [
    { id: 'btnWinter', name: '寒冬', desc: '光照骤降 · 代谢消耗上升' },
    { id: 'btnBloom', name: '藻华', desc: '光合倍增 · 蓝细菌爆发' },
    { id: 'btnHarvest', name: '收获', desc: '立即注入一批能量' },
    { id: 'btnPlague', name: '瘟疫', desc: '蓝细菌批量凋亡' }
  ];
  var PRESETS = [
    { id: 'btnPresetStable', name: '稳定态', lv: [2, 2, 2, 2] },
    { id: 'btnPresetFrenzy', name: '疯长态', lv: [4, 4, 0, 4] },
    { id: 'btnPresetBarren', name: '荒芜态', lv: [0, 1, 4, 1] }
  ];

  function fire(kind, name) {
    state.interventions++;
    state.dirty = true;
    var liveId = LIVE_EVENT_ID[kind];
    if (state.live && liveId) {
      postEnvLines(['event.' + liveId + '=1']);
      toast('已触发「' + name + '」→ POST /env event.' + liveId + '=1');
    } else {
      toast('已触发「' + name + '」（免费 · 无冷却）· 本轮演示不改变轨迹数据');
    }
    describeEnv();
    updateAll(true);
  }

  function applyPreset(lv, name) {
    for (var i = 0; i < AXES.length; i++) setAxis(AXES[i].id, lv[i], { silent: true });
    state.interventions++;
    state.dirty = true;
    if (state.live) {
      var lines = [];
      for (var j = 0; j < AXES.length; j++) {
        lines.push('axis.' + (LIVE_AXIS_ID[AXES[j].id] || AXES[j].id) + '=' + lv[j]);
      }
      postEnvLines(lines);
      toast('已套用预设「' + name + '」（免费）→ POST /env 四轴档位');
    } else {
      toast('已套用预设「' + name + '」（免费）');
    }
    describeEnv();
    updateAll(true);
  }

  // ── 参数页（假 schema：32 键 ≥ 30）───────────────────────────
  var paramDefs = [
    ['核 · 生存', [
      ['maxSpeed', '极速 maxSpeed', 0.2, 4, 2.0, 0.05],
      ['maxAccel', '最大加速度 maxAccel', 0.01, 1, 0.25, 0.01],
      ['energyThreshold', '繁殖阈值 energyThreshold', 60, 400, 160, 5],
      ['mutationRate', '变异率 mutationRate', 0, 0.5, 0.06, 0.01]
    ]],
    ['核 · 社会参数', [
      ['nucleusSightRadius', '察觉半径 nucleusSightRadius', 10, 600, 120, 5],
      ['nucleusPersonalSpace', '个人空间 nucleusPersonalSpace', 5, 200, 30, 1],
      ['nucleusRepelStrength', '斥力强度 nucleusRepelStrength', 0.1, 20, 2, 0.1],
      ['nucleusCohesion', '聚拢倾向 nucleusCohesion', 0, 10, 1, 0.1]
    ]],
    ['核 · 施力能力', [
      ['forceRange', '控制范围 forceRange', 10, 400, 80, 5],
      ['forceCostK', '代价系数 forceCostK', 0.0001, 1, 0.02, 0.001]
    ]],
    ['核 · 球偏好', [
      ['orbitRadius0', '轨道半径·资源 orbitRadius[0]', 5, 48, 18, 0.5],
      ['orbitRadius1', '轨道半径·侦察 orbitRadius[1]', 5, 48, 26, 0.5],
      ['orbitRadius2', '轨道半径·护盾 orbitRadius[2]', 5, 48, 15, 0.5],
      ['absorbPreference0', '吸收倍率·资源 absorbPreference[0]', 0.1, 3, 1, 0.05],
      ['absorbPreference1', '吸收倍率·侦察 absorbPreference[1]', 0.1, 3, 1, 0.05],
      ['absorbPreference2', '吸收倍率·护盾 absorbPreference[2]', 0.1, 3, 1, 0.05],
      ['repelStrength0', '近距排斥·资源 repelStrength[0]', 0.05, 8, 1, 0.05],
      ['repelStrength1', '近距排斥·侦察 repelStrength[1]', 0.05, 8, 1, 0.05],
      ['repelStrength2', '近距排斥·护盾 repelStrength[2]', 0.05, 8, 1, 0.05]
    ]],
    ['捕食（蓝细菌 = 食物）', [
      ['cyanoPreference', '捕食偏好 cyanoPreference', 0, 1, 0.6, 0.01],
      ['cyanoDigestion', '消化效率 cyanoDigestion', 0.1, 1, 0.7, 0.01],
      ['cyanoGrazeRadius', '捕食半径 cyanoGrazeRadius', 2, 30, 9, 0.5]
    ]],
    ['蓝细菌', [
      ['cyanoPhotoRate', '光合速率 cyanoPhotoRate', 0, 2, 0.55, 0.01],
      ['cyanoTransferRate', '传输速率 cyanoTransferRate', 0, 0.5, 0.08, 0.01],
      ['cyanoLinkDistance', '连线距离 cyanoLinkDistance', 6, 60, 20, 1],
      ['cyanoMaxLinks', '最大连线 cyanoMaxLinks', 0, 6, 3, 1],
      ['cyanoReproCost', '繁殖成本 cyanoReproCost', 2, 40, 9.6, 0.2],
      ['cyanoBallSpawnCost', '产球成本 cyanoBallSpawnCost', 5, 60, 18, 1],
      ['minNodeSeparation', '节点最小间距 minNodeSeparation', 2, 40, 6, 0.5]
    ]],
    ['世界与阳光', [
      ['sunBase', '基础光照 sunBase', 0, 1, 0.5, 0.01],
      ['sunAmplitude', '光照振幅 sunAmplitude', 0, 1, 0.5, 0.01],
      ['sunPeriod', '昼夜周期 sunPeriod', 40, 600, 200, 10],
      ['metabolismCost', '代谢消耗 metabolismCost', 0, 2, 0.22, 0.01],
      ['ballDamping', '球阻尼 ballDamping', 0.5, 1, 0.9, 0.01],
      ['decayRate', '自由球衰减 decayRate', 0, 0.5, 0.02, 0.01],
      ['worldWidth', '世界宽 worldWidth', 64, 512, 256, 32],
      ['worldHeight', '世界高 worldHeight', 64, 512, 256, 32],
      ['newbornGraceFrames', '新生保护 newbornGraceFrames', 0, 600, 0, 10]
    ]]
  ];

  function paramValue(def) {
    var v = state.params[def[0]];
    return (typeof v === 'number') ? v : def[4];
  }

  function buildParams() {
    var body = $('paramBody');
    if (!body) return;
    body.innerHTML = '';
    for (var gi = 0; gi < paramDefs.length; gi++) {
      var group = paramDefs[gi];
      var h = document.createElement('h3');
      h.className = 'group-title';
      h.textContent = group[0];
      body.appendChild(h);
      var grid = document.createElement('div');
      grid.className = 'param-grid';
      for (var pi = 0; pi < group[1].length; pi++) {
        (function (def) {
          var key = def[0];
          if (typeof state.params[key] !== 'number') state.params[key] = def[4];
          var rowEl = document.createElement('div');
          rowEl.className = 'param-row';
          rowEl.setAttribute('data-key', key);
          var lab = document.createElement('span');
          lab.className = 'param-label';
          lab.textContent = def[1];
          var val = document.createElement('span');
          val.className = 'param-val mono';
          val.textContent = String(def[4]);
          var input = document.createElement('input');
          input.type = 'range';
          input.min = String(def[2]);
          input.max = String(def[3]);
          input.step = String(def[5]);
          input.value = String(def[4]);
          input.setAttribute('data-key', key);
          input.setAttribute('aria-label', def[1]);
          var labWrap = document.createElement('label');
          labWrap.className = 'param-top';
          labWrap.appendChild(lab);
          labWrap.appendChild(val);
          rowEl.appendChild(labWrap);
          rowEl.appendChild(input);
          grid.appendChild(rowEl);
          input.addEventListener('input', function () {
            applyParam(key, parseFloat(input.value), val, lab);
          });
        })(group[1][pi]);
      }
      body.appendChild(grid);
    }
    // R7-D：参数页「材质包」区（在 32 参数滑杆之后追加）
    var pkTitle = document.createElement('h3');
    pkTitle.className = 'group-title';
    pkTitle.textContent = '材质包 Resource Packs';
    body.appendChild(pkTitle);
    var pkBox = document.createElement('div');
    pkBox.id = 'packsBox';
    pkBox.className = 'packs-box';
    body.appendChild(pkBox);
    renderPacks();
  }

  function applyParam(key, v, valEl, labEl) {
    var def = findParamDef(key);
    if (!def || typeof v !== 'number' || !isFinite(v)) return;
    var clamped = Math.max(def[2], Math.min(def[3], v));
    state.params[key] = clamped;
    state.paramsEdited[key] = true;
    if (valEl) valEl.textContent = String(clamped);
    if (labEl) labEl.classList.add('edited');
    var resetBtn = $('btnParamReset');
    if (resetBtn) resetBtn.disabled = false;
    state.dirty = true;
  }

  function findParamDef(key) {
    for (var gi = 0; gi < paramDefs.length; gi++) {
      var group = paramDefs[gi][1];
      for (var pi = 0; pi < group.length; pi++) {
        if (group[pi][0] === key) return group[pi];
      }
    }
    return null;
  }

  function syncParamInputs() {
    var body = $('paramBody');
    if (!body) return;
    var inputs = body.querySelectorAll('input[type=range]');
    for (var i = 0; i < inputs.length; i++) {
      var key = inputs[i].getAttribute('data-key');
      var def = findParamDef(key);
      if (!def) continue;
      var v = paramValue(def);
      inputs[i].value = String(v);
      var rowEl = inputs[i].parentNode;
      var valEl = rowEl ? rowEl.querySelector('.param-val') : null;
      var labEl = rowEl ? rowEl.querySelector('.param-label') : null;
      if (valEl) valEl.textContent = String(v);
      if (labEl) labEl.classList.toggle('edited', !!state.paramsEdited[key]);
    }
    var resetBtn = $('btnParamReset');
    if (resetBtn) resetBtn.disabled = Object.keys(state.paramsEdited).length === 0;
  }

  function filterParams(q) {
    var body = $('paramBody');
    if (!body) return;
    var rows = body.querySelectorAll('.param-row');
    var needle = String(q || '').trim().toLowerCase();
    for (var i = 0; i < rows.length; i++) {
      var key = rows[i].getAttribute('data-key') || '';
      rows[i].classList.toggle('hidden', needle !== '' && key.toLowerCase().indexOf(needle) < 0);
    }
    var groups = body.querySelectorAll('.group-title');
    for (var g = 0; g < groups.length; g++) groups[g].classList.remove('hidden');
  }

  // ── R7-D 存档缩略图 ─────────────────────────────────────────
  // localStorage 键：agentsim.thumb.v1:<saveName> → dataURL（128x72 JPEG）。
  // 诚实约束：缩略图只能来自真实渲染过的帧（载入后的第一帧 /state）。
  // 没有帧数据的存档绝不伪造 → 显示占位渐变「首次载入时生成」。
  var THUMB_W = 128, THUMB_H = 72;
  var THUMB_KEY_PREFIX = 'agentsim.thumb.v1:';

  function thumbKey(name) { return THUMB_KEY_PREFIX + String(name); }

  function loadThumbMap() {
    var map = {};
    try {
      for (var i = 0; i < localStorage.length; i++) {
        var k = localStorage.key(i);
        if (k && k.indexOf(THUMB_KEY_PREFIX) === 0) {
          var v = localStorage.getItem(k);
          if (typeof v === 'string' && v.indexOf('data:image/') === 0) map[k.slice(THUMB_KEY_PREFIX.length)] = v;
        }
      }
    } catch (e) { /* file:// 某些浏览器禁 localStorage → 全部走占位 */ }
    return map;
  }

  function readThumb(name) {
    try {
      var v = localStorage.getItem(thumbKey(name));
      return (typeof v === 'string' && v.indexOf('data:image/') === 0) ? v : null;
    } catch (e) { return null; }
  }

  function storeThumb(name, dataUrl) {
    try { localStorage.setItem(thumbKey(name), dataUrl); } catch (e) { /* 配额/隐私模式：静默 */ }
  }

  // 用 renderer.renderThumb 把 frame 画进 128x72 离屏画布 → dataURL。
  // 任一步失败（无帧/无 renderer/画布异常）返回 null —— 调用方显示占位，不伪造。
  function captureThumbFromFrame(frame) {
    if (!frame || !frame.nuclei || !renderer || !renderer.renderThumb) return null;
    var c = document.createElement('canvas');
    if (!c || !c.getContext) return null;
    c.width = THUMB_W; c.height = THUMB_H;
    try {
      if (!renderer.renderThumb(frame, c)) return null;
      var url = c.toDataURL('image/jpeg', 0.72);
      return (typeof url === 'string' && url.indexOf('data:image/') === 0) ? url : null;
    } catch (e) { return null; }
  }

  // 载入存档后第一帧落地 → 抓缩略图存 localStorage（键=存档名）。
  // 名字未知时（POST /load 只有 name 参数，此刻就在 state.pendingThumbName）不抓。
  function captureThumbForLive(frame) {
    var name = state.pendingThumbName;
    if (!name || !frame) return;
    state.pendingThumbName = null;   // 只抓第一帧，之后正常轮询不再进这里
    var url = captureThumbFromFrame(frame);
    if (url) { storeThumb(name, url); if (state.page === 'saves') renderSaves(); }
  }

  // ── R6-D 存档页（真后端 saves\ 文件夹；localStorage 假数据已废）──────
  function renderSaves() {
    var grid = $('savesGrid');
    if (!grid) return;
    grid.innerHTML = '';
    var saves = state.saves || [];
    if (!saves.length) {
      grid.innerHTML = '<div class="saves-empty">还没有存档：菜单「① 实时开新局」勾选自动存档，或运行页点「⬤ 存为快照」。</div>';
      return;
    }
    var thumbs = loadThumbMap();   // R7-D：name → dataURL（无缩略图 → 占位）
    for (var i = 0; i < saves.length; i++) {
      (function (s) {
        var card = document.createElement('div');
        card.className = 'save-card glass';
        var head = document.createElement('div');
        head.className = 'save-head';
        var nameInput = document.createElement('input');
        nameInput.type = 'text';
        nameInput.value = s.name;
        nameInput.className = 'save-rename mono';
        nameInput.setAttribute('aria-label', '重命名存档（回车确认）');
        nameInput.addEventListener('change', function () {
          var nn = nameInput.value.trim();
          if (!nn || nn === s.name) { nameInput.value = s.name; return; }
          var LCX = window.LiveClient;
          if (state.savesOnline && LCX && LCX.postForm) {
            // 后端 /saves 无 rename 路由 → 删除旧名 + 存新名（同文件夹等价改名）
            LCX.postForm('/delete-save', 'name=' + encodeURIComponent(s.name))
              .then(function () { return LCX.postForm('/save', 'name=' + encodeURIComponent(nn)); })
              .then(function () { s.name = nn; toast('已重命名为「' + nn + '」'); refreshSaves(); })
              .catch(function (e) { nameInput.value = s.name; toast('重命名失败：' + (e.message || e)); });
          } else {
            s.name = nn;   // 本地兜底列表
            toast('已重命名为「' + nn + '」');
          }
        });
        head.appendChild(nameInput);
        // R7-D 缩略图：localStorage 有真实帧快照 → <img>；没有 → 占位渐变
        // +「首次载入时生成」。绝不伪造（诚实约束：无帧数据不画假世界）。
        var thumbUrl = thumbs[s.name] || null;
        var thumb = document.createElement('div');
        thumb.className = thumbUrl ? 'save-thumb' : 'save-thumb save-thumb-empty';
        if (thumbUrl) {
          var img = document.createElement('img');
          img.src = thumbUrl;
          img.alt = s.name + ' 缩略图';
          img.width = THUMB_W; img.height = THUMB_H;
          thumb.appendChild(img);
        } else {
          var ph = document.createElement('span');
          ph.className = 'save-thumb-ph mono';
          ph.textContent = '首次载入时生成';
          thumb.appendChild(ph);
        }
        var meta = document.createElement('div');
        meta.className = 'save-meta';
        meta.innerHTML =
          '<div class="save-info mono">帧 ' + U.esc(String(s.frame)) +
          ' · seed ' + U.esc(String(s.seed)) +
          ' · 存活核 ' + U.esc(String(s.aliveNuclei)) +
          (s.savedAt ? ' · ' + U.esc(s.savedAt) : '') + '</div>';
        var actions = document.createElement('div');
        actions.className = 'save-actions';
        var loadB = document.createElement('button');
        loadB.type = 'button';
        loadB.className = 'btn btn-primary';
        loadB.textContent = '载入';
        loadB.title = 'POST /load 载入并存档帧继续';
        loadB.addEventListener('click', function () {
          var LCX = window.LiveClient;
          if (!state.savesOnline || !LCX || !LCX.postForm) { toast('载入需要本地服务（离线兜底无存档文件）'); return; }
          loadB.disabled = true;
          state.pendingThumbName = s.name;   // R7-D：第一帧落地时抓缩略图
          LCX.postForm('/load', 'name=' + encodeURIComponent(s.name))
            .then(function () { return enterLiveMode(); })
            .then(function () { toast('已载入「' + s.name + '」· 从存档帧继续'); })
            .catch(function (e) { loadB.disabled = false; toast('载入失败：' + (e.message || e)); });
        });
        var delB = document.createElement('button');
        delB.type = 'button';
        delB.className = 'btn btn-danger';
        delB.textContent = '删除';
        delB.addEventListener('click', function () {
          if (!window.confirm('确定删除存档「' + s.name + '」？此操作不可恢复。')) return;
          var LCX = window.LiveClient;
          if (!state.savesOnline || !LCX || !LCX.postForm) {
            state.saves = (state.saves || []).filter(function (x) { return x !== s; });
            renderSaves();
            toast('已删除「' + s.name + '」');
            return;
          }
          LCX.postForm('/delete-save', 'name=' + encodeURIComponent(s.name))
            .then(function () { toast('已删除「' + s.name + '」'); return refreshSaves(); })
            .catch(function (e) { toast('删除失败：' + (e.message || e)); });
        });
        var anB = document.createElement('button');
        anB.type = 'button';
        anB.className = 'btn';
        anB.textContent = '📈 分析';
        anB.title = '跳分析视图：暂停后抽 20 个检查点采样出四曲线';
        anB.addEventListener('click', function () {
          if (!state.savesOnline) { toast('分析需要本地服务（离线兜底无存档数据）'); return; }
          var loadTarget = s;
          var LCX = window.LiveClient;
          LCX.postForm('/load', 'name=' + encodeURIComponent(loadTarget.name))
            .then(function () { return enterLiveMode(); })
            .then(function () {
              showPage('run');
              runAnalysis();
            })
            .catch(function (e) { toast('分析载入失败：' + (e.message || e)); });
        });
        actions.appendChild(loadB);
        actions.appendChild(anB);
        actions.appendChild(delB);
        card.appendChild(head);
        card.appendChild(thumb);   // R7-D：缩略图卡在标题与元信息之间
        card.appendChild(meta);
        card.appendChild(actions);
        grid.appendChild(card);
      })(saves[i]);
    }
  }

  // GET /saves → 渲染；离线 → 「需要本地服务」提示（无假数据）
  function refreshSaves() {
    var grid = $('savesGrid');
    var LCX = window.LiveClient;
    if (!LCX || typeof fetch !== 'function' || !LCX.getJson) {
      state.saves = [];
      state.savesOnline = false;
      if (grid) grid.innerHTML = '<div class="saves-empty">🗄 需要本地服务：运行 <code>build\\agentsim.exe --serve 8080</code> 后刷新，才能读取和管理 saves\\ 文件夹的存档。</div>';
      return;
    }
    if (grid && !grid.innerHTML) grid.innerHTML = '<div class="saves-empty">正在读取存档…</div>';
    LCX.getJson('/saves').then(function (j) {
      var arr = (j && Array.isArray(j.saves)) ? j.saves : [];
      state.saves = arr;
      state.savesOnline = true;
      renderSaves();
    }).catch(function (e) {
      state.saves = [];
      state.savesOnline = false;
      if (grid) grid.innerHTML = '<div class="saves-empty">🗄 需要本地服务（GET /saves 失败：' +
        U.esc(String(e && e.message || e)) + '）。运行 <code>build\\agentsim.exe --serve 8080</code> 后重试。</div>';
    });
  }

  // ⬤ 存为快照：在线 POST /save（auto-<seed>-<时间戳> 命名建议可改名）；离线 toast
  function makeSaveFromCurrent() {
    if (!state.trace) { toast('还没有可存的世界：先「开始模拟」'); return; }
    var LCX = window.LiveClient;
    if (state.live && LCX && LCX.postForm) {
      var f = state.trace.frames[state.idx] || state.trace.frames[0];
      var base = (state.cfg && typeof state.cfg.seed !== 'undefined') ? state.cfg.seed : 'live';
      var name = 'auto-' + base + '-' + U.nowStamp().replace(/[-: ]/g, '');
      LCX.postForm('/save', 'name=' + encodeURIComponent(name))
        .then(function () {
          toast('已保存「' + name + '」→ saves\\ 文件夹（POST /save）' + (f ? '（t' + f.tick + '）' : ''));
          if (state.page === 'saves') refreshSaves();
        })
        .catch(function (e) { toast('POST /save 失败：' + (e.message || e)); });
      return;
    }
    toast('离线兜底不写真实存档：需要本地服务（POST /save）');
  }

  // ── 快速存档（断点续跑，sessionStorage）──────────────────────
  function pushQuicksave() {
    if (!state.trace) return;
    try {
      sessionStorage.setItem('agentsim.quicksave.v2', JSON.stringify({ tick: state.idx, seed: (state.trace.meta || {}).seed }));
    } catch (e) {}
  }
  function peekQuicksave() {
    try {
      var raw = sessionStorage.getItem('agentsim.quicksave.v2');
      if (raw) return JSON.parse(raw);
    } catch (e) {}
    return null;
  }

  // ── 菜单页 ───────────────────────────────────────────────────
  function readMenuCfg() {
    function intOf(id, def, lo, hi) {
      var e = $(id);
      var v = e ? parseInt(e.value, 10) : NaN;
      if (typeof v !== 'number' || !isFinite(v)) v = def;
      return Math.max(lo, Math.min(hi, v));
    }
    var seedRaw = $('menuSeed') ? $('menuSeed').value : '';
    var seed = String(seedRaw).trim() === '' ? 42 : (isFinite(+seedRaw) ? (+seedRaw | 0) : 42);
    return {
      seed: seed,
      nuclei: intOf('menuNuclei', 12, 1, 24),
      cyano: intOf('menuCyano', 48, 4, 120),
      frames: intOf('menuFrames', 300, 20, 100000),
      autosave: !(!$('menuAutosave') || !$('menuAutosave').checked),
      policy: ($('menuPolicy') && $('menuPolicy').value) ? $('menuPolicy').value : 'rule',
      fps: intOf('menuFps', 60, 0, 240)
    };
  }

  // ── R4-D 实时数据源（live-client.js）────────────────────────
  // 前端轴 id → 后端 EnvControl 轴 id（EnvControl.cpp：light/food/pressure/producer）
  var LIVE_AXIS_ID = { light: 'light', nutrient: 'food', pressure: 'pressure', producer: 'producer' };
  // 前端事件卡按钮 id → 后端事件 id（EnvControl.cpp：harvest/winter/plague/bloom）
  var LIVE_EVENT_ID = { btnWinter: 'winter', btnBloom: 'bloom', btnHarvest: 'harvest', btnPlague: 'plague' };
  var liveCtl = { warnedNoControl: false, lastPaintTick: -1 };

  // POST /env 行式协议（HttpServer handleEnvPost：每行一条 key=value）
  function postEnvLines(lines) {
    var body = lines.join('\n');
    try {
      fetch('/env', { method: 'POST', cache: 'no-store',
        headers: { 'Content-Type': 'text/plain' }, body: body })
        .then(function (r) { return r.ok ? r.json() : { ok: false, error: 'HTTP ' + r.status }; })
        .then(function (j) {
          if (!j) return;
          if (j.ok) {
            if (j.rejected) toast('POST /env：部分被拒 ' + j.rejected + ' 条' + (j.error ? '（' + j.error + '）' : ''));
          } else {
            toast('POST /env 失败：' + (j.error || ('rejected=' + j.rejected)));
          }
        })
        .catch(function (e) { toast('POST /env 网络失败：' + (e.message || e)); });
    } catch (e) { toast('POST /env 失败：' + (e.message || e)); }
  }

  // POST /control（cmd=pause|resume|step）。旧后端无此路由（404/405）→
  // 只提示一次，本地照常暂停（轮询继续、画面停在最后帧）。
  function postControl(cmd, opts) {
    var silent = !!(opts && opts.silent);
    try {
      fetch('/control', { method: 'POST', cache: 'no-store', body: 'cmd=' + cmd })
        .then(function (r) {
          if (!r.ok) throw new Error('HTTP ' + r.status);
          return r.json().catch(function () { return null; });
        })
        .then(function (j) {
          if (j && j.ok === false) throw new Error(j.error || 'rejected');
        })
        .catch(function () {
          if (!silent && !liveCtl.warnedNoControl) {
            liveCtl.warnedNoControl = true;
            toast('后端无 /control 路由：仅本地暂停回放（模拟仍在推进）');
          }
        });
    } catch (e) { /* file:// 等环境静默 */ }
  }

  // 实时帧落地：滚动窗口（最多 600 帧），charts 增量推进，时间轴=窗口末。
  function appendLiveFrame(frame) {
    if (state.replay) return;   // R6-D 回放锁定：后台轮询帧不推进画面/游标
    if (!state.trace) return;
    var t = state.trace;
    var i, dedup = [];
    for (i = 0; i < t.frames.length; i++) {
      if (t.frames[i].tick !== frame.tick) dedup.push(t.frames[i]);
    }
    dedup.push(frame);
    if (dedup.length > 600) dedup = dedup.slice(dedup.length - 600);
    t.frames = dedup;
    state.series = t.series = dedup.map(function (f) {
      var s = f.stats || {};
      var d = 0;
      if (typeof s.injected === 'number' && typeof s.dissipated === 'number' && typeof s.totalEnergy === 'number') {
        d = Math.abs(s.injected - s.dissipated - s.totalEnergy);
      }
      return { tick: f.tick, nuclei: s.nuclei, cyanoAlive: s.cyanoAlive,
        cyanoEnergy: s.cyanoEnergy, totalEnergy: s.totalEnergy,
        injected: s.injected, dissipated: s.dissipated, auditOk: s.auditOk,
        auditDelta: d };
    });
    state.hueSeries = dedup.map(function (f) {
      return { tick: f.tick, hueSd: f.hueSd, cyanoHueSd: f.cyanoHueSd };
    });
    var lb = state.liveWorld || { w: 256, h: 256 };
    t.bounds = { minX: 0, minY: 0, maxX: lb.w, maxY: lb.h };  // 环面世界固定框，尺寸随 /state 头
    var first = (t.meta.liveFirst !== true);
    t.meta.liveFirst = true;
    if (first) {
      var lw = state.liveWorld || {};
      t.meta.world = { width: lw.w || 256, height: lw.h || 256 };
    }
    state.idx = dedup.length - 1;
    if (state.chart.pop) state.chart.pop.setData(state.series, CHART_KEYS.pop);
    if (state.chart.cyano) state.chart.cyano.setData(state.series, CHART_KEYS.cyano);
    if (state.chart.energy) state.chart.energy.setData(state.series, CHART_KEYS.energy);
    if (state.chart.audit) state.chart.audit.setData(state.series, CHART_KEYS.audit);
    if (state.chart.hue) state.chart.hue.setData(state.hueSeries, CHART_KEYS.hue);
    var tl = $('timeline');
    if (tl) { tl.max = String(dedup.length - 1); tl.value = String(state.idx); }
    var tlMax = $('tlMax');
    if (tlMax) tlMax.textContent = '/ t' + frame.tick;
    if (first && renderer) renderer.resetView();   // 首帧按世界满幅取景（resetView = fitCamera）
    liveCtl.lastPaintTick = frame.tick;
    updateAll(true);
  }

  function enterLiveMode() {
    if (!window.LiveClient || !renderer) return;
    state.live = true;
    state.replay = false; state.timeline = null; state.seeking = false;
    liveCtl.lastPaintTick = -1;
    state.trace = {
      schemaVersion: 3,
      meta: { contractVersion: 3, seed: 'live', world: { width: 256, height: 256 }, policy: 'live' },
      bounds: { minX: 0, minY: 0, maxX: 256, maxY: 256 },
      frames: [], series: [], hasGenome: false,
      stats: { frames: 0, droppedTuples: 0, detail: { nuclei: 0, balls: 0, cyano: 0, links: 0, genomes: 0 } }
    };
    state.idx = 0; state.accum = 0; state.lastTs = 0; state.hoverIndex = -1;
    state.interventions = 0; state.dirty = false;
    state.series = []; state.hueSeries = [];
    renderer.setTrace(state.trace);
    var chips = $('metaChips');
    if (chips) {
      var sd = (state.cfg && typeof state.cfg.seed !== 'undefined') ? state.cfg.seed : '—';
      chips.textContent = '实时模式 · seed ' + sd + ' · 数据源 agentsim --serve · POST /env 调控直通';
    }
    var mc2 = $('menuChips');
    if (mc2) mc2.textContent = '实时后端已连接（GET /status 探测成功）— 开跑即看实时模拟';
    showPage('run');
    setPlaying(true);
    refreshTimeline();   // R6-D：拉 GET /timeline 画检查点刻度 + 灰显判定
    window.LiveClient.start('', {
      intervalMs: 33, maxFailures: 6,
      onFrame: function (frame) {
        // R7-D：载入存档后的第一帧 → 渲染 128x72 缩略图存 localStorage（只此一帧）
        if (state.pendingThumbName) captureThumbForLive(frame);
        if (state.liveWorld && frame.live && frame.live.worldW) {
          if (state.liveWorld.w !== frame.live.worldW || state.liveWorld.h !== frame.live.worldH) {
            state.liveWorld = { w: frame.live.worldW, h: frame.live.worldH };
          }
        } else if (frame.live && frame.live.worldW && !state.liveWorld) {
          state.liveWorld = { w: frame.live.worldW, h: frame.live.worldH };
        }
        appendLiveFrame(frame);
      },
      onWarn: function (msg) { console.warn('[live] ' + msg); },
      onError: function (msg) { console.warn('[live] ' + msg); }
    });
    var st = window.LiveClient.status();
    toast('实时模式：轮询 /state @' + st.intervalMs + 'ms · 调控直通 POST /env');
    describeEnv();
  }

  function startRun(cfg) {
    state.cfg = cfg;
    // R4-D 双数据源：探测 /status 在线 → POST /run 开新局 + 实时轮询；失败
    // （file:// 双击打开 / 后端没起）→ 回退演示数据。probe 永不 reject。
    var live = (window.LiveClient && window.LiveClient.probe) ? window.LiveClient.probe('') : Promise.resolve(false);
    live.then(function (up) {
      if (up) {
        // R6-D：卡A「开始」= POST /run（行式 seed=&nuclei=&cyano_nodes=&ticks=）。
        // 旧后端无 /run（404）→ 仅提示，仍进实时轮询/本地兜底，不崩。
        var LCX = window.LiveClient;
        LCX.postForm('/run', 'seed=' + encodeURIComponent(cfg.seed) +
          '&nuclei=' + cfg.nuclei + '&cyano_nodes=' + cfg.cyano + '&ticks=' + cfg.frames +
          '&policy=' + encodeURIComponent(cfg.policy || 'rule') +
          '&max_fps=' + (typeof cfg.fps === 'number' ? cfg.fps : 60))
          .then(function () {
            if (!cfg.autosave) return;
            var autoName = 'auto-' + cfg.seed + '-' + U.nowStamp().replace(/[-: ]/g, '');
            return LCX.postForm('/save', 'name=' + encodeURIComponent(autoName))
              .then(function () { toast('已自动存档「' + autoName + '」（POST /save）'); });
          })
          .catch(function (e) { toast('POST /run|save 失败：' + (e.message || e)); });
        enterLiveMode();
        return;
      }
      var trace;
      try {
        // R6-D: demo 降级为无后端兜底（不再是菜单一等公民）。
        trace = window.DemoData.generate(cfg);
      } catch (e) {
        toast('演示数据生成失败：' + e.message);
        return;
      }
      if (!loadTrace(trace, '演示 seed ' + cfg.seed)) return;
      state.interventions = 0;
      state.dirty = false;
      showPage('run');
      setPlaying(true);
      toast('开跑：seed ' + cfg.seed + ' · 演示数据（后端未连接）· 调控免费');
    });
  }

  // R6-D：回放态徽标（画布右上角；resume 即隐藏）
  function setReplayBadge(on, text) {
    var b = $('replayBadge');
    if (!b) return;
    b.classList.toggle('hidden', !on);
    if (on) b.textContent = text || '⏸ 回放态 · 播放键恢复实时';
  }

  // ── R6-D 进度条回放：GET /timeline → 刻度 + pause→seek→拉帧 ──────────
  function refreshTimeline() {
    var LCX = window.LiveClient;
    if (!LCX || !LCX.getJson) return Promise.resolve(null);
    return LCX.getJson('/timeline').then(function (j) {
      var cps = (j && Array.isArray(j.checkpoints)) ? j.checkpoints : [];
      var cur = (j && typeof j.current === 'number') ? j.current : 0;
      state.timeline = { checkpoints: cps, current: cur };
      renderTimeline();
      return state.timeline;
    }).catch(function () {
      // 无 /timeline 路由或离线：进度条按本地窗口帧处理（旧行为）
      if (state.timeline === null) state.timeline = { checkpoints: [], current: 0 };
      renderTimeline();
      return state.timeline;
    });
  }

  // 把 checkpoints 画成进度条刻度；空则灰显 + 提示（未攒够 200 帧）
  function renderTimeline() {
    var tl = $('timeline'), wrapEl = $('tlWrap'), ticks = $('tlTicks'), hint = $('tlHint');
    var cps = state.timeline ? state.timeline.checkpoints : [];
    var frames = (state.trace && state.trace.frames) ? state.trace.frames : [];
    var lastIdx = Math.max(0, frames.length - 1);
    if (tl) tl.max = String(lastIdx);
    var tlMax = $('tlMax');
    if (tlMax) tlMax.textContent = '/ t' + (frames.length ? frames[lastIdx].tick : 0);
    if (ticks) ticks.innerHTML = '';
    var empty = !cps.length;
    if (wrapEl) wrapEl.classList.toggle('empty', empty);
    if (hint) {
      hint.classList.toggle('hidden', !empty || !state.live);
      hint.textContent = '暂无检查点（约 200 帧后自动打点），进度条暂不可回放';
    }
    if (empty || !ticks || !cps.length) return;
    var maxF = cps[cps.length - 1];
    for (var i = 0; i < cps.length; i++) {
      var ic = document.createElement('i');
      var pct = maxF > 0 ? (cps[i] / maxF) * 100 : 0;
      ic.style.left = pct.toFixed(2) + '%';
      ic.title = '检查点 t' + cps[i];
      ticks.appendChild(ic);
    }
  }

  function tickOfIdx(i) {
    if (!state.trace || !state.trace.frames || !state.trace.frames[i]) return NaN;
    return state.trace.frames[i].tick;
  }
  function curTick() {
    return (state.timeline && typeof state.timeline.current === 'number')
      ? state.timeline.current : (tickOfIdx(state.idx) || 0);
  }

  // 回放核心：pause → POST /seek frame=N → 拉 /state 渲染。
  // 运行态 seek（409）会被后端拒绝，所以先确保暂停。
  function seekToFrame(f) {
    var LCX = window.LiveClient;
    if (!LCX || !LCX.postForm || !LCX.fetchState) return Promise.resolve(false);
    if (typeof f !== 'number' || !isFinite(f)) return Promise.resolve(false);
    f = Math.round(f);
    var cps = state.timeline ? state.timeline.checkpoints : [];
    if (cps.length && f < cps[0]) f = cps[0];          // 检查点只从攒够处开始
    if (cps.length && f > cps[cps.length - 1]) f = cps[cps.length - 1];
    if (state.seeking) return Promise.resolve(false);   // 拖动节流：在途直接丢
    state.seeking = true;
    setPlaying(false, { silent: true });   // 本地暂停（cmd=pause 由下面链路显式发）
    return LCX.postForm('/control', 'cmd=pause').catch(function () {})
      .then(function () { return LCX.postForm('/seek', 'frame=' + f); })
      .then(function () { return LCX.fetchState(''); })
      .then(function (frame) {
        state.replay = true;                            // 回放徽标 + 屏蔽轮询帧
        var frames = state.trace ? state.trace.frames : null;
        var hit = -1;
        if (frames) {
          for (var i = frames.length - 1; i >= 0; i--) {
            if (frames[i].tick === frame.tick) { hit = i; break; }
          }
          if (hit >= 0) {
            frames[hit] = frame;                          // 窗口内已有该 tick：原位替换
          } else {
            if (frames.length >= 600) frames.shift();     // 窗口外：挤掉最老一帧贴入
            frames.push(frame);
            hit = frames.length - 1;
          }
        }
        var t = state.trace;
        t.series = frames.map(function (fr) {
          var s = fr.stats || {};
          var d = 0;
          if (typeof s.injected === 'number' && typeof s.dissipated === 'number' && typeof s.totalEnergy === 'number') {
            d = Math.abs(s.injected - s.dissipated - s.totalEnergy);
          }
          return { tick: fr.tick, nuclei: s.nuclei, cyanoAlive: s.cyanoAlive,
            cyanoEnergy: s.cyanoEnergy, totalEnergy: s.totalEnergy,
            injected: s.injected, dissipated: s.dissipated, auditOk: s.auditOk,
            auditDelta: d };
        });
        state.hueSeries = frames.map(function (fr) {
          return { tick: fr.tick, hueSd: fr.hueSd, cyanoHueSd: fr.cyanoHueSd };
        });
        state.idx = Math.max(0, (frames ? frames.length - 1 : 0));
        if (state.chart.pop) state.chart.pop.setData(state.series, CHART_KEYS.pop);
        if (state.chart.cyano) state.chart.cyano.setData(state.series, CHART_KEYS.cyano);
        if (state.chart.energy) state.chart.energy.setData(state.series, CHART_KEYS.energy);
        if (state.chart.audit) state.chart.audit.setData(state.series, CHART_KEYS.audit);
        if (state.chart.hue) state.chart.hue.setData(state.hueSeries, CHART_KEYS.hue);
        var tl = $('timeline');
        if (tl) tl.value = String(state.idx);
        updateAll(true, true);
        var tlCur = $('tlCur');
        if (tlCur) tlCur.textContent = 't ' + frame.tick + '（回放）';
        if (state.timeline) state.timeline.current = frame.tick;
        setReplayBadge(true, '⏸ 回放 t' + frame.tick + ' · 播放键恢复实时');
        return true;
      })
      .catch(function (e) {
        toast('回放 seek 失败：' + (e.message || e));
        return false;
      })
      .then(function (v) { state.seeking = false; return v; });
  }

  // ── R6-D 分析视图：暂停后对 checkpoints 抽 ≤20 个采样点逐个 /seek + /state ──
  function runAnalysis() {
    var LCX = window.LiveClient;
    var panel = $('analysisPanel');
    if (!panel) return;
    function close(msg) {
      panel.classList.remove('open');
      if (msg) toast(msg);
    }
    if (!LCX || !LCX.postForm) { close('分析需要本地服务（file:// 离线兜底无数据）'); return; }
    panel.classList.add('open');
    if (renderer) renderer.resize();
    var cbs = ['anPop', 'anCyano', 'anEnergy', 'anHue'];
    for (var ci = 0; ci < cbs.length; ci++) {
      if (state.chart[cbs[ci]]) state.chart[cbs[ci]].resize();
      if (state.chart[cbs[ci]]) state.chart[cbs[ci]].setData([], []);
    }
    var meta = $('analysisMeta'), prog = $('analysisProgress');
    setPlaying(false, { silent: true });   // 纯本地暂停徽标（不重复 POST）
    LCX.postForm('/control', 'cmd=pause').then(function () {   // await 保证 pause 先于 seek 落地
      return refreshTimeline();
    }).then(function (tl) {
      var cps = (tl && tl.checkpoints) ? tl.checkpoints.slice() : [];
      if (!cps.length) { close('没有可用检查点（约 200 帧后自动打点），无法分析'); return null; }
      // 均匀抽 ≤20 个
      var picks = [];
      if (cps.length <= 20) picks = cps;
      else {
        for (var i = 0; i < 20; i++) picks.push(cps[Math.round(i * (cps.length - 1) / 19)]);
      }
      if (meta) meta.textContent = '抽 ' + picks.length + '/' + cps.length + ' 个检查点采样';
      var rows = [];
      var idx = 0;
      function next() {
        if (idx >= picks.length) {
          if (state.chart.anPop) state.chart.anPop.setData(rows, CHART_KEYS.pop);
          if (state.chart.anCyano) state.chart.anCyano.setData(rows, CHART_KEYS.cyano);
          if (state.chart.anEnergy) state.chart.anEnergy.setData(rows, CHART_KEYS.energy);
          if (state.chart.anHue) state.chart.anHue.setData(rows, CHART_KEYS.hue);
          // R10 框架：六判据速算卡（GOAL-ECOLOGY 修订 1 口径，浏览器端复算）
          renderCriteriaCard(rows);
          if (prog) prog.textContent = '完成（' + rows.length + ' 点）';
          toast('分析完成：' + rows.length + ' 个采样点');
          return;
        }
        var f = picks[idx];
        if (prog) prog.textContent = '采样 ' + (idx + 1) + '/' + picks.length + '（t' + f + '）';
        return LCX.postForm('/seek', 'frame=' + f)
          .then(function () { return LCX.fetchState(''); })
          .then(function (fr) {
            var s = fr.stats || {};
            rows.push({ tick: fr.tick, nuclei: s.nuclei, cyanoAlive: s.cyanoAlive,
              cyanoEnergy: s.cyanoEnergy, totalEnergy: s.totalEnergy,
              hueSd: fr.hueSd, cyanoHueSd: fr.cyanoHueSd });
            idx++;
            return next();
          })
          .catch(function (e) {
            close('分析中断：' + (e.message || e));
            return null;
          });
      }
      return next();
    }).catch(function (e) { close('分析失败：' + (e.message || e)); });
  }

  // ── R10 判据速算卡：GOAL-ECOLOGY 修订 1 口径（末 30% 窗口 + DEFER）────────
  // 与 tools/analyze.mjs 同公式，但只消费分析视图已采样的 rows（≈20 点）——
  // 采样密度低，结果定性（速算卡），权威定量仍以 analyze.mjs 对全量 trace 为准。
  function renderCriteriaCard(rows) {
    var box = $('criteriaCard');
    if (!box) return;
    if (!rows || rows.length < 6) { box.innerHTML = '<div class="crit-line">采样点不足，无法速算</div>'; return; }
    function slope(y) {
      var n = y.length, mx = 0, my = 0;
      for (var i = 0; i < n; i++) { mx += i; my += y[i]; }
      mx /= n; my /= n;
      var num = 0, den = 0;
      for (var j = 0; j < n; j++) { num += (j - mx) * (y[j] - my); den += (j - mx) * (j - mx); }
      return den > 0 ? num / den : 0;
    }
    function mean(a) { var s = 0; for (var i = 0; i < a.length; i++) s += a[i]; return s / a.length; }
    function pearson(a, b) {
      var ma = mean(a), mb = mean(b), num = 0, da = 0, db = 0;
      for (var i = 0; i < a.length; i++) { num += (a[i]-ma)*(b[i]-mb); da += (a[i]-ma)*(a[i]-ma); db += (b[i]-mb)*(b[i]-mb); }
      return (da > 0 && db > 0) ? num / Math.sqrt(da * db) : NaN;
    }
    var w0 = Math.floor(rows.length * 0.7);
    var w = rows.slice(w0);
    var totalE = w.map(function (r) { return r.totalEnergy || 0; });
    var nucs = w.map(function (r) { return r.nuclei || 0; });
    var cyas = w.map(function (r) { return r.cyanoAlive || 0; });
    var ticks = w.map(function (r) { return r.tick || 0; });
    var span = Math.max(1, ticks[ticks.length-1] - ticks[0]);
    // crit1
    var eSlope = slope(totalE), eMean = Math.max(1e-9, mean(totalE));
    var c1 = Math.abs(eSlope) * span / eMean;
    var nSlope = Math.abs(slope(nucs)) * span / Math.max(1e-9, mean(nucs));
    var c1s = nSlope >= 0.05 ? 'DEFER' : (c1 < 0.15 ? 'PASS' : 'FAIL');
    // crit2 (tail 20% cv)
    var t0 = Math.floor(rows.length * 0.8);
    var tailN = rows.slice(t0).map(function (r) { return r.nuclei || 0; });
    var m = mean(tailN);
    var cv = m > 0 ? Math.sqrt(tailN.reduce(function (a, v) { var d = v - m; return a + d*d; }, 0) / tailN.length) / m : NaN;
    var c2s = !isFinite(cv) ? 'FAIL' : (cv >= 0.05 && cv <= 0.6 ? 'PASS' : 'FAIL');
    // crit5
    var r5 = pearson(nucs, cyas);
    var c5s = nSlope >= 0.05 ? 'DEFER' : (!isFinite(r5) ? 'SKIP' : (r5 < -0.2 ? 'PASS' : 'FAIL'));
    // crit4 (hue from rows.hueSd — linear sd proxy; note analyze uses circular)
    var hueSdLast = rows[rows.length-1].hueSd;
    var c4s = (typeof hueSdLast === 'number' && hueSdLast > 30) ? 'PASS' : 'FAIL';
    function line(name, st, val) {
      var cls = st === 'PASS' ? 'crit-pass' : (st === 'FAIL' ? 'crit-fail' : 'crit-defer');
      return '<div class="crit-line"><span class="' + cls + '">' + st + '</span> ' + name +
             ' <span class="mono">' + val + '</span></div>';
    }
    box.innerHTML =
      '<div class="crit-title">六判据速算（末 30% 窗口 · 定性）</div>' +
      line('1 能量紧平衡', c1s, c1.toFixed(3)) +
      line('2 种群波动', c2s, 'cv=' + (isFinite(cv) ? cv.toFixed(3) : '—')) +
      line('4 hue 发散', c4s, 'sd≈' + (typeof hueSdLast === 'number' ? hueSdLast.toFixed(1) + '°' : '—') + '（线性近似）') +
      line('5 捕食者-猎物', c5s, 'r=' + (isFinite(r5) ? r5.toFixed(3) : '—'));
  }

  function closeAnalysis() {
    var p = $('analysisPanel');
    if (p) p.classList.remove('open');
  }

  function resetRun() {
    if (!state.live) { toast('离线兜底没有真实模拟在跑，无需复位'); return; }   // R6-D
    if (!state.cfg) state.cfg = readMenuCfg();
    startRun(state.cfg);
    for (var i = 0; i < AXES.length; i++) setAxis(AXES[i].id, 2, { silent: true });
    state.interventions = 0;
    state.dirty = false;
    describeEnv();
    toast(state.live
      ? '已复位：实时模式由后端持续推帧，本地时间轴已清零'
      : '已复位（同一 seed 重放，结果逐字节一致）');
  }

  function resumeRun() {
    var qs = peekQuicksave();
    if (!qs) { toast('没有断点存档'); return; }
    if (!state.demoTrace) {
      try { state.demoTrace = window.DemoData.generate({ seed: qs.seed || 42 }); }
      catch (e) { toast('续跑失败：' + e.message); return; }
    }
    if (loadTrace(state.demoTrace, '续跑 seed ' + (qs.seed || 42))) {
      showPage('run');
      stepTo(qs.tick || 0);
      setPlaying(true);
    }
  }

  function checkDemo() {
    var chips = $('menuChips');
    try {
      state.demoTrace = window.DemoData.generate({ seed: 42 });
    } catch (e) {
      if (chips) chips.textContent = '演示数据生成失败：' + e.message;
    }
    // R6-D：探测本地服务。在线 → 两卡可用 + 文案；离线 → 横幅 + 两卡置灰 +
    // 不起眼的「离线演示」小链接兜底（demo 不再是菜单一等公民）。
    function applyOnline(up) {
      state.online = up;
      var banner = $('offlineBanner');
      var a = $('cardLive'), b = $('cardSaves');
      if (banner) banner.classList.toggle('hidden', !!up);
      if (a) a.classList.toggle('offline', !up);
      if (b) b.classList.toggle('offline', !up);
      if (chips) {
        chips.textContent = up
          ? '本地服务在线（agentsim --serve）— 选「实时开新局」或「载入存档」'
          : '未检测到本地服务 — 两卡暂不可用，仅留离线演示兜底';
      }
    }
    applyOnline(false);   // 先按离线渲染（探测中置灰，避免闪出可点按钮）
    if (window.LiveClient && window.LiveClient.probe) {
      window.LiveClient.probe('').then(function (up) {
        if (!up) return;                 // 探测失败维持离线渲染
        if (state.online === true) return;
        applyOnline(true);
        if (window.LiveClient.getJson) {
          window.LiveClient.getJson('/saves').then(function (j) {
            var n = (j && Array.isArray(j.saves)) ? j.saves.length : 0;
            if (chips && state.online) {
              chips.textContent = '本地服务在线 · saves\\ 已有 ' + n + ' 个存档 — 选「实时开新局」或「载入存档」';
            }
          }).catch(function () {});
        }
      });
    }
    var resumeBtn = $('menuResume');
    if (resumeBtn) resumeBtn.classList.toggle('hidden', true);
  }

  // ── 画布交互：缩放/平移/悬停/跟随 ────────────────────────────
  function bindCanvas() {
    if (!canvas) return;
    var dragging = false, lastX = 0, lastY = 0, movedPx = 0;
    canvas.addEventListener('pointerdown', function (e) {
      dragging = true; lastX = e.clientX; lastY = e.clientY; movedPx = 0;
      canvas.classList.add('dragging');
      try { canvas.setPointerCapture(e.pointerId); } catch (err) {}
    });
    canvas.addEventListener('pointermove', function (e) {
      var rect = canvas.getBoundingClientRect();
      var lx = e.clientX - rect.left, ly = e.clientY - rect.top;
      if (dragging) {
        var dx = e.clientX - lastX, dy = e.clientY - lastY;
        movedPx += Math.abs(dx) + Math.abs(dy);
        renderer.panBy(dx, dy);
        lastX = e.clientX; lastY = e.clientY;
        if (state.trace) updateAll();
        return;
      }
      state.hoverX = lx; state.hoverY = ly;
      var hit = renderer.hitTest(lx, ly);
      if (hit.kind === 'nucleus') {
        if (hit.index !== state.hoverIndex) {
          state.hoverIndex = hit.index;
          refreshHover(state.trace.frames[state.idx], false);
        } else if (state.hoverIndex >= 0) {
          var tip = $('tip');
          if (tip && tip.classList.contains('on')) positionTip(tip, lx, ly);
        }
      } else if (!dragging && state.hoverIndex >= 0) {
        state.hoverIndex = -1;
        var tip2 = $('tip');
        if (tip2) tip2.classList.remove('on');
      }
    });
    canvas.addEventListener('pointerup', function (e) {
      var wasDrag = movedPx > 6;
      dragging = false;
      canvas.classList.remove('dragging');
      try { canvas.releasePointerCapture(e.pointerId); } catch (err) {}
      if (!wasDrag && state.hoverIndex >= 0 && state.trace) {
        var nu = state.trace.frames[state.idx].nuclei[state.hoverIndex];
        if (nu) {
          if (state.pinnedCards[nu.id]) { unpinCard(nu.id); }   // 再点同核 = 取消钉住
          else pinCard(nu);
          state.followNucleus = (state.followNucleus === nu.id) ? -1 : nu.id;
          toast(state.followNucleus >= 0
            ? ('跟随核 #' + nu.id + '（hue ' + nu.hue.toFixed(0) + '°）— 再次点按取消')
            : '已取消跟随');
        }
      }
    });
    canvas.addEventListener('pointerleave', function () {
      state.hoverIndex = -1;
      var tip = $('tip');
      if (tip) tip.classList.remove('on');
    });
    canvas.addEventListener('wheel', function (e) {
      e.preventDefault();
      var rect = canvas.getBoundingClientRect();
      renderer.zoomAt(e.clientX - rect.left, e.clientY - rect.top, e.deltaY < 0 ? 1.12 : 1 / 1.12);
      if (state.trace) updateAll();
    }, { passive: false });
  }

  // ── 绑定 ─────────────────────────────────────────────────────
  function bindAll() {
    // 顶栏
    on('brandHome', 'click', function () { showPage('menu'); });
    var nav = $('navBtns');
    if (nav) {
      var btns = nav.querySelectorAll('[data-page]');
      for (var i = 0; i < btns.length; i++) {
        (function (b) {
          b.addEventListener('click', function () { showPage(b.getAttribute('data-page')); });
        })(btns[i]);
      }
    }
    // 菜单页（R6-D：两卡 + 离线演示小链接）
    on('menuStart', 'click', function () {
      if (state.online === false) { toast('未检测到本地服务：请先 build\\agentsim.exe --serve 8080'); return; }
      startRun(readMenuCfg());
    });
    on('menuLoad', 'click', function () {
      if (state.online === false) { toast('未检测到本地服务：存档管理需要 build\\agentsim.exe --serve 8080'); return; }
      showPage('saves');
    });
    on('menuDemoLink', 'click', function () { startRun(readMenuCfg()); });   // 离线兜底
    on('menuParams', 'click', function () { showPage('params'); });
    // 回放：进度条拖动/输入帧号 → pause → /seek → 拉 /state 渲染
    on('btnAnalyze', 'click', runAnalysis);
    on('btnAnalysisClose', 'click', closeAnalysis);
    var tlBox = $('timeline');
    if (tlBox) {
      tlBox.addEventListener('input', function () {
        if (state.live && state.timeline && state.timeline.checkpoints.length) {
          var i = parseInt(tlBox.value, 10) || 0;
          var fr = state.trace && state.trace.frames ? state.trace.frames[i] : null;
          var target = fr ? fr.tick : i;
          seekToFrame(target);
        } else {
          stepTo(parseInt(tlBox.value, 10));   // 离线兜底：本地窗口游标
        }
      });
    }
    // 播放控制
    on('btnPlay', 'click', function () { setPlaying(!state.playing); });   // POST /control 由 setPlaying 分发
    on('btnFirst', 'click', function () { setPlaying(false); stepTo(0); });
    on('btnPrev', 'click', function () { setPlaying(false); stepTo(state.idx - 1); });
    on('btnNext', 'click', function () {
      setPlaying(false);
      stepTo(state.idx + 1);
      if (state.live) postControl('step');
    });   // R6-D：pause 已由 setPlaying(false) 分发，这里只补 step
    on('btnLast', 'click', function () { setPlaying(false); stepTo(state.trace ? state.trace.frames.length - 1 : 0); });
    on('btnLoop', 'click', function () {
      state.loop = !state.loop;
      this.classList.toggle('active', state.loop);
      toast(state.loop ? '循环播放：开' : '循环播放：关');
    });
    on('btnStops', 'click', function () {
      setPlaying(false);
      toast(state.live ? '已暂停：POST /control cmd=pause' : '已全部停止：回放暂停 · 本轮演示无实时模拟在跑');
    });
    var seg = $('speedSeg');
    if (seg) {
      seg.addEventListener('click', function (e) {
        var b = e.target.closest ? e.target.closest('button[data-v]') : null;
        if (!b) return;
        setSpeed(parseFloat(b.getAttribute('data-v')));
      });
    }
    // 时间轴（R6-D：input 分流在上方；此处只管拖动时暂停/松手恢复）
    var tl = $('timeline');
    if (tl) {
      var wasPlaying = false;
      tl.addEventListener('pointerdown', function () { wasPlaying = state.playing; setPlaying(false); });
      tl.addEventListener('change', function () { if (wasPlaying && !state.live) setPlaying(true); });
    }
    on('btnTimelineLeft', 'click', function () { setPlaying(false); stepTo(state.idx - 10); });
    on('btnTimelineRight', 'click', function () { setPlaying(false); stepTo(state.idx + 10); });
    // 状态栏折叠 / 调控条折叠
    on('btnStatus', 'click', function () {
      setStatusCollapsed(!(document.body && document.body.classList.contains('statushidden')));
    });
    on('btnToolbar', 'click', function () {
      setToolbarHidden(!(document.body && document.body.classList.contains('toolhidden')));
    });
    // 环境调控条
    buildEnv();
    for (var ev2 = 0; ev2 < EVENTS.length; ev2++) {
      (function (ev) {
        on(ev.id, 'click', function () { fire(ev.id, ev.name); });
      })(EVENTS[ev2]);
    }
    for (var pr = 0; pr < PRESETS.length; pr++) {
      (function (p) {
        on(p.id, 'click', function () { applyPreset(p.lv, p.name); });
      })(PRESETS[pr]);
    }
    on('btnCyanoToggle', 'click', function () {
      state.interventions++;
      state.dirty = true;
      var turningOn = this.textContent.indexOf('关') >= 0;
      this.textContent = turningOn ? '🧪 蓝细菌：开' : '🧪 蓝细菌：关';
      if (state.live) postEnvLines(['cyano=' + (turningOn ? 'on' : 'off')]);
      toast('蓝细菌开关已切换（免费）' + (state.live ? ' → POST /env cyano=' + (turningOn ? 'on' : 'off') : ' · 本轮演示不改变轨迹数据'));
    });
    on('btnToggleDrawer', 'click', function () {
      var d = $('drawer');
      if (d) d.classList.toggle('open');
      var b = $('btnToggleDrawer');
      if (b) b.classList.toggle('active', d && d.classList.contains('open'));
    });
    on('btnRunParams', 'click', function () { showPage('params'); });
    on('btnNewSave', 'click', makeSaveFromCurrent);
    on('btnStopRun', 'click', function () { setPlaying(false); pushQuicksave(); toast('已暂停并存断点（sessionStorage）'); });
    on('btnResetRun', 'click', resetRun);
    // 参数页
    buildParams();
    on('btnParamReset', 'click', function () {
      state.params = {}; state.paramsEdited = {};
      buildParams();
      syncParamInputs();
      toast('已恢复默认参数（演示 schema）');
    });
    on('btnParamDefaults', 'click', function () {
      toast('已写入默认（演示轮为空操作：真实后端接 POST /save-config）');
    });
    on('btnParamBack', 'click', function () { showPage('run'); });
    on('paramSearch', 'input', function () { filterParams(this.value); });
    // 存档页（R6-D：真后端 saves，无 localStorage 种子假数据）
    state.saves = [];
    on('btnSavesRefresh', 'click', function () { refreshSaves(); toast('已刷新存档列表'); });
    on('btnSavesBack', 'click', function () { showPage('menu'); });
    on('savesSearch', 'input', function () {
      var needle = String(this.value || '').trim().toLowerCase();
      var grid = $('savesGrid');
      if (!grid) return;
      var cards = grid.querySelectorAll('.save-card');
      for (var i = 0; i < cards.length; i++) {
        var nameEl = cards[i].querySelector('.save-name');
        var name = nameEl ? nameEl.textContent.toLowerCase() : '';
        cards[i].classList.toggle('hidden', needle !== '' && name.indexOf(needle) < 0);
      }
    });
    // 文件载入（轨迹 JSON）
    if (window.TraceIO) {
      window.TraceIO.bindHandlers({
        fileInput: $('fileInput'),
        loadBtn: $('loadBtn'),
        dropCover: $('dropCover'),
        onTrace: function (obj, name) { if (loadTrace(obj, name)) showPage('run'); },
        onError: function (msg) { toast(msg); }
      });
    }
    // 昼夜开关
    on('dayNightToggle', 'change', function () {
      renderer.setDayNight(!!$('dayNightToggle').checked);
      updateAll(true);
      toast($('dayNightToggle').checked ? '昼夜天空：开' : '昼夜天空：关');
    });
    // 画布
    bindCanvas();
    if (state.chart.scatter && $('chartScatter')) bindScatterHover($('chartScatter'), state.chart.scatter);
    // 快捷键
    addWin('keydown', function (e) {
      var tag = e.target && e.target.tagName;
      var k = e.key;
      if (tag === 'INPUT' && e.target.type !== 'range' && e.target.type !== 'checkbox') return;
      if (tag === 'INPUT' && (k === 'ArrowLeft' || k === 'ArrowRight' || k === 'Home' || k === 'End')) return;
      if (k === ' ') {
        e.preventDefault();
        if (state.page === 'run') setPlaying(!state.playing);   // POST /control 由 setPlaying 分发
      }
      else if (k === 'ArrowLeft' && state.page === 'run') { setPlaying(false); stepTo(state.idx - (e.shiftKey ? 10 : 1)); }
      else if (k === 'ArrowRight' && state.page === 'run') { setPlaying(false); stepTo(state.idx + (e.shiftKey ? 10 : 1)); }
      else if (k === 'Home' && state.page === 'run') { setPlaying(false); stepTo(0); }
      else if (k === 'End' && state.page === 'run') { setPlaying(false); stepTo(state.trace ? state.trace.frames.length - 1 : 0); }
      else if (k === 'r' || k === 'R') { renderer.resetView(); updateAll(true); toast('视角已复位'); }
      else if (k === 'l' || k === 'L') { var lb = $('btnLoop'); if (lb) lb.click(); }
      else if (k === 'f' || k === 'F') { setCinema(!state.cinema); }
      else if (k === 'h' || k === 'H') {
        setStatusCollapsed(!(document.body && document.body.classList.contains('statushidden')));
      }
      else if (k === 'Escape') {
        if (state.cinema) setCinema(false);
        else if (state.page === 'params') showPage('run');
        else if (state.page === 'saves') showPage('menu');
      }
    });
    addWin('resize', function () {
      renderer.resize();
      resizeCharts();
      if (state.trace && state.page === 'run') updateAll(true);
    });
  }

  // ── 启动 ─────────────────────────────────────────────────────
  function boot() {
    canvas = $('stage');
    if (!canvas || !window.Renderer) return;
    renderer = window.Renderer.create(canvas);
    if (window.Charts) {
      state.chart.pop = window.Charts.create($('chartPop'), {});
      state.chart.cyano = window.Charts.create($('chartCyano'), {});
      state.chart.energy = window.Charts.create($('chartEnergy'), {});
      state.chart.audit = window.Charts.create($('chartAudit'), {});
      if (window.Charts.createScatter) state.chart.scatter = window.Charts.createScatter($('chartScatter'), {});
      state.chart.hue = window.Charts.create($('chartHue'), {});
      // R6-D：分析视图四曲线
      state.chart.anPop = window.Charts.create($('chartAnPop'), {});
      state.chart.anCyano = window.Charts.create($('chartAnCyano'), {});
      state.chart.anEnergy = window.Charts.create($('chartAnEnergy'), {});
      state.chart.anHue = window.Charts.create($('chartAnHue'), {});
    }
    bindAll();
    checkDemo();
    fetchPalette();   // R7-D：GET /packs → active 覆盖默认色（失败保持默认，不崩）
    // R4-D：开机探测一次后端；在线则拉 GET /env 把四轴档位同步到调控条，
    // 并在顶栏挂常驻数据源徽标。失败静默（演示模式不受影响）。
    if (window.LiveClient && window.LiveClient.probe) {
      window.LiveClient.probe('').then(function (up) {
        if (!up) return;
        state.liveDetected = true;
        syncEnvFromServer();
        if (document.body && !document.getElementById('liveSrcPill')) {
          var pill = document.createElement('span');
          pill.id = 'liveSrcPill';
          pill.className = 'live-pill mono';
          pill.textContent = '● 实时';
          pill.title = '检测到 agentsim --serve 实时后端';
          var brand = document.querySelector('.brand-text');
          if (brand) brand.appendChild(pill);
        }
      });
    }
    showPage('menu');
    raf(tick);
  }

  // R4-D：GET /env → 同步四轴档位（后端是权威；level ∈ [0,4]）
  function syncEnvFromServer() {
    try {
      fetch('/env', { cache: 'no-store' }).then(function (r) {
        return r.ok ? r.json() : null;
      }).then(function (j) {
        if (!j || !Array.isArray(j.axes)) return;
        for (var i = 0; i < j.axes.length; i++) {
          var a = j.axes[i];
          var front = null;
          for (var k in LIVE_AXIS_ID) if (LIVE_AXIS_ID[k] === a.id) front = k;
          if (front && typeof a.level === 'number') setAxis(front, Math.max(0, Math.min(4, a.level | 0)), { silent: true });
        }
        describeEnv();
      }).catch(function () {});
    } catch (e) { /* 静默 */ }
  }

  return {
    boot: boot,
    showPage: showPage,
    startRun: startRun,
    loadTrace: loadTrace,
    setAxis: setAxis,
    setPlaying: setPlaying,
    _state: state,
    _live: {
      postEnvLines: postEnvLines,
      postControl: postControl,
      appendLiveFrame: appendLiveFrame,
      enterLiveMode: enterLiveMode,
      syncEnvFromServer: syncEnvFromServer,
      refreshSaves: refreshSaves,
      renderSaves: renderSaves,
      checkDemo: checkDemo,
      refreshTimeline: refreshTimeline,
      renderTimeline: renderTimeline,
      seekToFrame: seekToFrame,
      runAnalysis: runAnalysis,
      fetchPalette: fetchPalette,
      applyPaletteActive: applyPaletteActive,
      renderPacks: renderPacks,
      postSetPacks: postSetPacks,
      captureThumbFromFrame: captureThumbFromFrame,
      loadThumbMap: loadThumbMap,
      readThumb: readThumb,
      storeThumb: storeThumb,
      __test_renderCriteriaCard: renderCriteriaCard
    },
    __test_renderCriteriaCard: renderCriteriaCard
  };
})();
