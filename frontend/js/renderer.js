/* agentsim 世界渲染器 V2 — 色相核/三类球形状/蓝细菌网络/施力脉冲/昼夜天空 · 零依赖
 *
 * V2 视觉契约（用户指令）：
 *   - 核：圆形，填充 hsl(hue,72%,58%)，外发光同色，半径随 energy 增长，必须看得出移动
 *   - 绝对不画核→球的连线（用户明确禁止）；不保留任何历史位置（R1-AMEND-1）
 *   - 球：Resource=实心圆(青) · Scout=菱形(紫) · Shield=六边形(金)；自由球虚线圈区分
 *   - 蓝细菌：发光小圆，颜色 = c 元组遗传 hue，亮度/半径随 energy；连线拓扑来自
 *     v3 f 键（parser 归一化为 cyanoLinks），半透明绿线单 path 批处理、在节点下层；
 *     新节点从父节点附近出现（相邻帧位置即体现）
 *   - 施力可视化：帧数据 forceTarget 指向的球上画脉冲环 + 短箭头（方向=实际加速度），
 *     约 8 帧淡出；箭头画在球上，不是核→球连线
 *   - 昼夜天空 + 网格保留；食物球渲染整体移除（食物已取消）
 * 性能：单 path 批量画同类蓝细菌/连线；脉冲环用固定池按 age 增量刷新；
 *       悬停命中用上一帧的屏幕坐标做 O(n) 距离测试（无 DOM 开销）。
 * R7-D：实体颜色由调色板驱动（默认 = 旧硬编码色；GET /packs active 可整体换肤，
 *       setPalette 后下一帧立即生效）；核填充/发光仍走遗传 hue（谱系可分优先）。
 */
window.Renderer = (function () {
  'use strict';
  var TAU = Math.PI * 2;

  // ── R7-D 调色板：默认值与 /packs active 同构（'r,g,b' 字符串）──────────
  // 内置默认 = 历史硬编码色（青球 #45e0d8 / 紫球 #b78bff / 金球 #ffcf5e…）。
  // main.js boot 时 GET /packs 拿 active 调 setPalette() 整体替换；离线/404
  // 保持本默认，file:// 双击不崩、正常渲染（本卡底线）。
  var DEFAULT_PALETTE = {
    background: '10,14,26',
    ball_shield: '255,207,94',
    ball_worker: '69,224,216',
    ball_scout: '183,139,255',
    free_ball: '200,200,210',
    nucleus: '255,255,255'
  };
  var palette = DEFAULT_PALETTE;

  function clampRgbStr(s) {
    if (typeof s !== 'string') return null;
    var m = s.match(/^\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d{1,3})\s*$/);
    if (!m) return null;
    var r = +m[1], g = +m[2], b = +m[3];
    if (r > 255 || g > 255 || b > 255) return null;
    return r + ',' + g + ',' + b;
  }

  // 校验并应用调色板：非法/缺失键逐项回退默认（坏包不崩、不渲染出黑块），
  // 未知名不进调色板（白名单）。返回生效后的调色板（main.js 以此回写 state.palette）。
  function setPalette(p) {
    var out = {};
    for (var k in DEFAULT_PALETTE) out[k] = clampRgbStr(p && p[k]) || DEFAULT_PALETTE[k];
    palette = out;
    return palette;
  }
  function getPalette() { return palette; }

  function isFiniteNum(v) { return typeof v === 'number' && isFinite(v); }

  function create(canvas) {
    var ctx = canvas.getContext('2d');
    var trace = null, W = 0, H = 0, dpr = 1;
    // R8-D minimal materials: default ON (palette recolors, style fixed)
    var MINIMAL = true;
    function setMinimal(v) { MINIMAL = !!v; }
    // interaction blink: nucleus id -> last interact ts (ms). Frontend approximation (T54).
    var lastInteract = {};
    var cam = { scale: 1, cx: 128, cy: 128, fit: 1 };
    var dayNight = true;
    var skyDayness = 0;
    var DAYNIGHT_PERIOD = 200;
    var SKY_STOPS = [
      [0.00, 232, 62, 4.5],
      [0.18, 248, 50, 6.5],
      [0.30, 24, 55, 11],
      [0.50, 210, 58, 13.5],
      [0.68, 18, 52, 10.5],
      [0.82, 268, 46, 6.5],
      [1.00, 232, 62, 4.5]
    ];

    function resize() {
      var rect = canvas.getBoundingClientRect ? canvas.getBoundingClientRect() : null;
      var cw = rect ? rect.width : (canvas.clientWidth || canvas.width || 0);
      var ch = rect ? rect.height : (canvas.clientHeight || canvas.height || 0);
      if (cw < 2 || ch < 2) return false;
      dpr = Math.max(1, window.devicePixelRatio || 1);
      var w = Math.round(cw * dpr), h = Math.round(ch * dpr);
      if (canvas.width !== w || canvas.height !== h) {
        canvas.width = w; canvas.height = h; W = w; H = h;
        if (trace) fitCamera();
      }
      return true;
    }

    function setTrace(next) { trace = next; if (next) fitCamera(); }

    function fitCamera() {
      if (!trace || W < 2) return;
      var b = trace.bounds;
      var ww = Math.max(1e-9, b.maxX - b.minX), wh = Math.max(1e-9, b.maxY - b.minY);
      cam.fit = Math.min(W / ww, H / wh) * 0.92;
      cam.scale = cam.fit;
      cam.cx = (b.minX + b.maxX) / 2;
      cam.cy = (b.minY + b.maxY) / 2;
    }

    function resetView() { fitCamera(); }

    function s2x(x) { return (x - cam.cx) * cam.scale + W / 2; }
    function s2y(y) { return H / 2 - (y - cam.cy) * cam.scale; }
    function screenToWorld(sx, sy) {
      return { x: (sx - W / 2) / cam.scale + cam.cx, y: (H / 2 - sy) / cam.scale + cam.cy };
    }

    function zoomAt(sx, sy, factor) {
      var wx = (sx - W / 2) / cam.scale + cam.cx;
      var wy = (H / 2 - sy) / cam.scale + cam.cy;
      var next = Math.min(cam.fit * 48, Math.max(cam.fit * 0.3, cam.scale * factor));
      if (next === cam.scale) return;
      cam.scale = next;
      cam.cx += wx - ((sx - W / 2) / cam.scale + cam.cx);
      cam.cy += wy - ((H / 2 - sy) / cam.scale + cam.cy);
      clampPan();
    }

    function panBy(dxPx, dyPx) {
      cam.cx -= dxPx / cam.scale; cam.cy += dyPx / cam.scale;
      clampPan();
    }

    function clampPan() {
      if (!trace) return;
      var b = trace.bounds;
      var pad = Math.min(64, (b.maxX - b.minX) * 0.25);
      cam.cx = Math.min(b.maxX + pad, Math.max(b.minX - pad, cam.cx));
      cam.cy = Math.min(b.maxX + pad, Math.max(b.minY - pad, cam.cy));
    }

    // ---------- 昼夜天空 ----------
    function setDayNight(on) { dayNight = !!on; }

    function skyAt(phase) {
      var p = ((phase % 1) + 1) % 1;
      for (var i = 1; i < SKY_STOPS.length; i++) {
        if (p <= SKY_STOPS[i][0]) {
          var a = SKY_STOPS[i - 1], b = SKY_STOPS[i];
          var t = (p - a[0]) / Math.max(1e-9, b[0] - a[0]);
          return [a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t, a[3] + (b[3] - a[3]) * t];
        }
      }
      return [232, 62, 4.5];
    }

    // R11-P0-4：sun 实时驱动明暗。实时模式（frame.live.sun 到达）以 /state 头的
    // sun 为唯一光照源（与后端昼夜周期 628 帧严格同步）；离线轨迹无 live.sun，
    // 回退到前端时间相位（DAYNIGHT_PERIOD）。MINIMAL 分支叠加日罩/夜幕平涂
    //（零渐变保持极简风），非极简分支实时时用 sun 推导天色亮度（渐变 + 光晕保留）。
    function drawSky(tick, sun) {
      var live = isFiniteNum(sun);
      var p = (((tick || 0) / DAYNIGHT_PERIOD) % 1 + 1) % 1;
      var dayness = live ? Math.max(0, Math.min(1, sun)) : 0.5 - 0.5 * Math.cos(p * TAU);
      skyDayness = dayness;
      if (MINIMAL) {
        // R8-D 极简：纯色平涂（palette.background）+ R11 日罩/夜幕半透明平涂
        var bg = palette.background || '10,14,26';
        ctx.fillStyle = 'rgb(' + bg + ')';
        ctx.fillRect(0, 0, W, H);
        var dEx = dayness - 0.5;                     // >0 白昼，<0 夜晚
        if (dEx > 0.003) {
          ctx.fillStyle = 'rgba(255,240,200,' + (0.26 * dEx * 2).toFixed(3) + ')';
          ctx.fillRect(0, 0, W, H);
        } else if (dEx < -0.003) {
          ctx.fillStyle = 'rgba(0,0,28,' + (0.30 * -dEx * 2).toFixed(3) + ')';
          ctx.fillRect(0, 0, W, H);
        }
        return;
      }
      var h, s, l;
      if (live) { h = 215; s = 45; l = 4 + 22 * dayness; }
      else { var hsl = skyAt(p); h = Math.round(hsl[0]); s = Math.round(hsl[1]); l = hsl[2]; }
      var g = ctx.createLinearGradient(0, 0, 0, H);
      g.addColorStop(0, 'hsl(' + h + ',' + s + '%,' + (l + 3).toFixed(1) + '%)');
      g.addColorStop(1, 'hsl(' + ((h + 24) % 360) + ',' + s + '%,' + Math.max(1.5, l - 2).toFixed(1) + '%)');
      ctx.fillStyle = g;
      ctx.fillRect(0, 0, W, H);
      var gx = W * (live ? 0.5 : (0.18 + 0.64 * p));
      var gy = H * (live ? 0.28 : (0.72 - 0.55 * Math.sin(p * TAU)));
      var gr = Math.min(W, H) * (0.30 + 0.20 * dayness);
      var hue = 220 - 180 * dayness;
      var rg = ctx.createRadialGradient(gx, gy, 0, gx, gy, gr);
      rg.addColorStop(0, 'hsla(' + hue.toFixed(0) + ',80%,65%,' + (0.10 + 0.14 * dayness).toFixed(3) + ')');
      rg.addColorStop(1, 'hsla(' + hue.toFixed(0) + ',80%,65%,0)');
      ctx.fillStyle = rg;
      ctx.fillRect(0, 0, W, H);
    }

    // ---------- 主绘制 ----------
    // 上一帧的实体屏幕坐标（悬停命中 + 施力脉冲锚点复用；无 DOM/无历史轨迹）
    var nPos = { x: [], y: [], r: [] };   // nuclei
    var bPos = { x: [], y: [], id: [] };  // balls
    var lastFrame = null;
    var pulses = [];                       // 施力脉冲池 {id, x, y, dx, dy, age}
    var PULSE_LIFE = 8;                    // 帧数：约 8 帧淡出

    function draw(frame, fixed) {
      // fixed（R7-D 缩略图路径）：直接采用调用方给的尺寸，跳过主画布 resize
      if (fixed) { W = fixed.w; H = fixed.h; }
      else if (!resize()) return;
      var tick = frame && isFiniteNum(frame.tick) ? frame.tick : 0;
      ctx.clearRect(0, 0, W, H);
      // R7-D：palette.background 只在关闭昼夜天空时作纯色底。昼夜渐变是 SKY_STOPS
      // 的时间函数，若再把包背景色混进天空，包颜色会被稀释到看不出（取舍：两条路径各司其职）。
      if (dayNight) drawSky(tick, frame && frame.live ? frame.live.sun : null);
      else { ctx.fillStyle = 'rgba(' + palette.background + ',0.9)'; ctx.fillRect(0, 0, W, H); }
      if (!trace) return;
      var b = trace.bounds;
      drawGrid(b);
      if (!frame) return;
      var zoom = cam.scale / Math.max(1e-9, cam.fit);
      drawBounds(b, zoom);
      drawCyanoNetwork(frame, zoom);
      drawBalls(frame, zoom);
      detectInteractions(frame);   // R8-D：邻近判定先于核绘制（灰点闪烁数据源）
      drawNuclei(frame, zoom);
      updatePulses(frame);
      drawPulses(zoom);
      drawSeamHints(frame);
      lastFrame = frame;
    }

    function drawGrid(b) {
      var step = 32;
      var alpha = dayNight ? +(0.05 + 0.06 * (1 - skyDayness)).toFixed(3) : 0.07;
      ctx.strokeStyle = 'rgba(122,152,220,' + alpha + ')';
      ctx.lineWidth = 1;
      ctx.beginPath();
      for (var gx = b.minX; gx <= b.maxX + 0.01; gx += step) {
        var sx = s2x(gx); ctx.moveTo(sx, s2y(b.minY)); ctx.lineTo(sx, s2y(b.maxY));
      }
      for (var gy = b.minY; gy <= b.maxY + 0.01; gy += step) {
        var sy = s2y(gy); ctx.moveTo(s2x(b.minX), sy); ctx.lineTo(s2x(b.maxX), sy);
      }
      ctx.stroke();
    }

    function drawBounds(b, zoom) {
      ctx.strokeStyle = 'rgba(90,209,255,' + (0.22 + 0.1 * Math.min(1, zoom)) + ')';
      ctx.lineWidth = 1.5 * dpr;
      ctx.strokeRect(s2x(b.minX), s2y(b.maxY),
        (b.maxX - b.minX) * cam.scale, (b.maxY - b.minY) * cam.scale);
    }

    // ---------- 蓝细菌网络（v3：f=连线拓扑，节点色=遗传 hue，先线后点） ----------
    // v3 契约：frame.cyanoLinks=[[idA,idB],...]（TraceParser 已把 f 键归一化到这里）；
    // 节点 hue 来自 c 元组第 6 项（B 链路遗传色相）；亮度/半径随 energy（按帧内最大能量归一，
    // 兼容不同能量量纲的轨迹）。连线永远画在节点下层；节点必须在线的上层（用户验收点）。
    function drawCyanoNetwork(frame, zoom) {
      var list = frame.cyano;
      var n = list.length;
      if (!n) return;
      var sxArr = new Array(n), syArr = new Array(n), rArr = new Array(n);
      var eMax = 0;
      for (var e = 0; e < n; e++) {
        var en0 = list[e].energy;
        if (isFiniteNum(en0) && en0 > eMax) eMax = en0;
      }
      var eScale = Math.max(8, eMax);        // 帧内相对亮度归一（对不同量纲的轨迹都成立）
      var rBase = Math.max(1.6, 3.0 * Math.pow(zoom, 0.32)) * dpr;
      for (var i = 0; i < n; i++) {
        var c = list[i];
        var sx = s2x(c.x), sy = s2y(c.y);
        sxArr[i] = sx; syArr[i] = sy;
        var en = isFiniteNum(c.energy) ? Math.max(0, c.energy) : 0;
        var enN = Math.min(1.35, en / eScale);
        rArr[i] = rBase * (0.6 + 0.5 * Math.sqrt(enN));   // 半径随 energy（√ 压扁差距）
      }
      // ① 半透明绿连线：单 path 批处理。R11-P0-2：环面 wrap——后端拓扑本就是
      // 环面最短距，跨界对按 ±world 平移拆成两段短边（A→B' 主段 + B→A' 镜像
      // 段），左边出去右边进来。R11-P1-7：>4000 条自动 4 取 1 抽稀（对齐原版）。
      var links = frame.cyanoLinks;
      if (links && links.length) {
        var byId = {};
        for (var m = 0; m < n; m++) byId[list[m].id] = m;
        var bnd = trace.bounds;
        var ww2 = Math.max(1, bnd.maxX - bnd.minX), wh2 = Math.max(1, bnd.maxY - bnd.minY);
        var decStep = links.length > 4000 ? 4 : 1;
        ctx.strokeStyle = 'rgba(72,220,128,' + (0.10 + 0.10 * Math.min(1.6, zoom)).toFixed(3) + ')';
        ctx.lineWidth = Math.max(0.6, 1.0 * dpr * Math.pow(zoom, 0.2));
        ctx.beginPath();
        for (var li = 0; li < links.length; li += decStep) {
          var pair = links[li];
          var ia = byId[pair[0]], ib = byId[pair[1]];
          if (ia == null || ib == null) continue;   // 跨帧引用缺失 → 跳过
          var x1w = list[ia].x, y1w = list[ia].y, x2w = list[ib].x, y2w = list[ib].y;
          var dxw = x2w - x1w;
          if (dxw > ww2 * 0.5) dxw -= ww2; else if (dxw < -ww2 * 0.5) dxw += ww2;
          var dyw = y2w - y1w;
          if (dyw > wh2 * 0.5) dyw -= wh2; else if (dyw < -wh2 * 0.5) dyw += wh2;
          // 主段 A→B'（B 平移到 A 的环面邻域）
          var ax = s2x(x1w), ay = s2y(y1w), bx = s2x(x1w + dxw), by = s2y(y1w + dyw);
          if (!((ax < -40 && bx < -40) || (ax > W + 40 && bx > W + 40) ||
                (ay < -40 && by < -40) || (ay > H + 40 && by > H + 40))) {
            ctx.moveTo(ax, ay); ctx.lineTo(bx, by);
          }
          // 跨界时补镜像段 B→A'（另一侧接缝可见：左边出去右边进来）
          if (dxw !== x2w - x1w || dyw !== y2w - y1w) {
            var cx3 = s2x(x2w), cy3 = s2y(y2w), dx3 = s2x(x2w - dxw), dy3 = s2y(y2w - dyw);
            if (!((cx3 < -40 && dx3 < -40) || (cx3 > W + 40 && dx3 > W + 40) ||
                  (cy3 < -40 && dy3 < -40) || (cy3 > H + 40 && dy3 > H + 40))) {
              ctx.moveTo(cx3, cy3); ctx.lineTo(dx3, dy3);
            }
          }
        }
        ctx.stroke();
      }
      // ② 节点：颜色 = c 元组 hue（遗传色相），画在连线上层
      // R8-D 极简：平涂小圆点（复用上方 sxArr/rArr），无 shadowBlur 无高光芯
      if (MINIMAL) {
        for (var jm = 0; jm < n; jm++) {
          var sxm = sxArr[jm], sym = syArr[jm];
          if (sxm < -8 || sxm > W + 8 || sym < -8 || sym > H + 8) continue;
          var cnm2 = list[jm];
          var huem = ((isFiniteNum(cnm2.hue) ? cnm2.hue : 130) % 360 + 360) % 360;
          var enNm = Math.min(1, (isFiniteNum(cnm2.energy) ? Math.max(0, cnm2.energy) : 0) / eScale);
          ctx.beginPath();
          ctx.fillStyle = 'hsl(' + huem.toFixed(0) + ',45%,' + (50 + 16 * enNm).toFixed(1) + '%)';
          ctx.arc(sxm, sym, Math.max(1.4, rArr[jm] * 0.8), 0, TAU);
          ctx.fill();
        }
        return;
      }
      ctx.shadowBlur = 7 * dpr;
      for (var j = 0; j < n; j++) {
        var sx2 = sxArr[j], sy2 = syArr[j];
        if (sx2 < -10 || sx2 > W + 10 || sy2 < -10 || sy2 > H + 10) continue;
        var cn = list[j];
        var hue = ((isFiniteNum(cn.hue) ? cn.hue : 130) % 360 + 360) % 360;
        var en2 = isFiniteNum(cn.energy) ? Math.max(0, cn.energy) : 0;
        var enN2 = Math.min(1.35, en2 / eScale);
        var rr = rArr[j];
        ctx.shadowColor = 'hsla(' + hue.toFixed(0) + ',70%,55%,.8)';
        ctx.fillStyle = 'hsl(' + hue.toFixed(0) + ',62%,' + (44 + 16 * enN2).toFixed(1) + '%)';
        ctx.beginPath();
        ctx.moveTo(sx2 + rr, sy2);
        ctx.arc(sx2, sy2, rr, 0, TAU);
        ctx.fill();
        // 高能节点提亮芯（亮度随 energy）
        ctx.shadowBlur = 0;
        ctx.fillStyle = 'hsla(' + hue.toFixed(0) + ',85%,' + (70 + 14 * enN2).toFixed(1) + '%,' + (0.30 + 0.35 * enN2).toFixed(3) + ')';
        ctx.beginPath();
        ctx.arc(sx2 - rr * 0.22, sy2 - rr * 0.22, rr * 0.48, 0, TAU);
        ctx.fill();
        if (j < n - 1) ctx.shadowBlur = 7 * dpr;
      }
      ctx.shadowBlur = 0;
    }

    // ---------- 球：Resource=实心圆 · Scout=菱形 · Shield=六边形（颜色全走调色板） ----------
    // R7-D: 调色板改为 GET /packs 动态获取（本卡实现）。每帧从 palette 现拼颜色
    // 字符串（3 类球 + 自由球虚线圈），换包后下一帧立即生效，无需刷新/重建缓存。
    function ballColors() {
      return {
        fill: ['rgb(' + palette.ball_worker + ')', 'rgb(' + palette.ball_scout + ')', 'rgb(' + palette.ball_shield + ')'],
        glow: ['rgba(' + palette.ball_worker + ',.85)', 'rgba(' + palette.ball_scout + ',.9)', 'rgba(' + palette.ball_shield + ',.85)'],
        freeRing: 'rgba(' + palette.free_ball + ',.95)'
      };
    }

    function drawBalls(frame, zoom) {
      var BC = ballColors();   // R7-D：本帧调色板颜色（下一帧重取 → 换包即时生效）
      var rBase = Math.max(1.5, 2.6 * Math.pow(zoom, 0.3)) * dpr;
      bPos.x.length = 0; bPos.y.length = 0; bPos.id.length = 0;
      for (var i = 0; i < frame.balls.length; i++) {
        var ball = frame.balls[i];
        var sx = s2x(ball.x), sy = s2y(ball.y);
        bPos.x.push(sx); bPos.y.push(sy); bPos.id.push(ball.id);
        if (sx < -14 || sx > W + 14 || sy < -14 || sy > H + 14) continue;
        var type = ball.type === 1 ? 1 : (ball.type === 2 ? 2 : 0);
        var free = !ball.owner || ball.owner === 0;
        // 体型随球能轻微缩放（energy 40 → 1.2×封顶）
        var r = rBase * (0.85 + 0.35 * Math.min(1, (isFiniteNum(ball.energy) ? ball.energy : 0) / 40));
        if (free) {
          // 自由球：空心形状 + 虚线外圈，无辉光，与从属球一眼区分
          ctx.lineWidth = 1.1 * dpr;
          ctx.strokeStyle = BC.fill[type];
          traceShape(sx, sy, r, type); ctx.stroke();
          ctx.setLineDash([2.5 * dpr, 2.5 * dpr]);
          ctx.strokeStyle = BC.freeRing;
          ctx.beginPath(); ctx.arc(sx, sy, r + 2.8 * dpr, 0, TAU); ctx.stroke();
          ctx.setLineDash([]);
        } else if (MINIMAL) {
          // R8-D 极简 + R10 修正（用户反馈：全白看不出类型）：平涂类型色，无辉光
          ctx.beginPath();
          ctx.fillStyle = BC.fill[type];
          ctx.arc(sx, sy, r, 0, TAU); ctx.fill();
        } else {
          ctx.shadowBlur = 7 * dpr;
          ctx.shadowColor = BC.glow[type];
          ctx.fillStyle = BC.fill[type];
          traceShape(sx, sy, r, type); ctx.fill();
          ctx.shadowBlur = 0;
        }
      }
      ctx.shadowBlur = 0;
    }

    // 按类型走形状路径（beginPath 由调用方控制）
    function traceShape(sx, sy, r, type) {
      ctx.beginPath();
      if (type === 0) {           // Resource：实心圆
        ctx.arc(sx, sy, r, 0, TAU);
      } else if (type === 1) {    // Scout：菱形
        ctx.moveTo(sx, sy - r * 1.25);
        ctx.lineTo(sx + r * 1.25, sy);
        ctx.lineTo(sx, sy + r * 1.25);
        ctx.lineTo(sx - r * 1.25, sy);
        ctx.closePath();
      } else {                    // Shield：六边形
        for (var k = 0; k < 6; k++) {
          var a = TAU * k / 6 - Math.PI / 6;
          var px = sx + Math.cos(a) * r * 1.15, py = sy + Math.sin(a) * r * 1.15;
          if (k === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py);
        }
        ctx.closePath();
      }
    }

    // ---------- 核：颜色 = 遗传 hue（g 键 / v2 对象 hue / 按 id 兜底合成），半径随 energy ----------
    // 兜底合成：轨迹无 g 键且核无 hue（真 v3 后端在 R2-A 接线前）时，用黄金角按 id
    // 确定性铺开色相——谱系可分（同 id 恒同色），但绝不计入多样性统计（hueReal=false）。
    var GOLDEN = 137.50776405003785;   // 黄金角（度）
    function synthHue(id) { return (id * GOLDEN) % 360; }

    // R8-D：帧级邻近判定（前端近似）：核距任一蓝细菌 <14（cyanoPredationRadius 默认）
    // 或距任一球 <24（senseRadius 近似）→ 记 lastInteract。环面 wrap 距离取 min(dx, W-dx)。
    function detectInteractions(frame) {
      var nowMs = (typeof performance !== 'undefined' && performance.now) ? performance.now() : Date.now();
      var wW = (trace && trace.bounds) ? Math.max(1, trace.bounds.maxX) : 512;
      var wH = (trace && trace.bounds) ? Math.max(1, trace.bounds.maxY) : 512;
      var cy = frame.cyano || [], bl = frame.balls || [];
      for (var i = 0; i < frame.nuclei.length; i++) {
        var nu = frame.nuclei[i];
        var hit2 = false;
        for (var a = 0; a < cy.length && !hit2; a++) {
          var dx = Math.abs(nu.x - cy[a].x); dx = Math.min(dx, wW - dx);
          var dy = Math.abs(nu.y - cy[a].y); dy = Math.min(dy, wH - dy);
          if (dx*dx + dy*dy < 196) hit2 = true;          // 14^2
        }
        for (var b3 = 0; b3 < bl.length && !hit2; b3++) {
          var dx2 = Math.abs(nu.x - bl[b3].x); dx2 = Math.min(dx2, wW - dx2);
          var dy2 = Math.abs(nu.y - bl[b3].y); dy2 = Math.min(dy2, wH - dy2);
          if (dx2*dx2 + dy2*dy2 < 576) hit2 = true;      // 24^2
        }
        if (hit2) lastInteract[nu.id] = nowMs;
      }
      // 防止 map 无界增长：清理 10s 无交互的死核记录
      if (frame.nuclei.length > 0) {
        for (var k in lastInteract) {
          var aliveSeen = false;
          for (var z = 0; z < frame.nuclei.length; z++) if (String(frame.nuclei[z].id) === k) { aliveSeen = true; break; }
          if (!aliveSeen && nowMs - lastInteract[k] > 10000) delete lastInteract[k];
        }
      }
    }

    function drawNuclei(frame, zoom) {
      var glow = Math.min(1, 0.35 + 0.4 * zoom);
      nPos.x.length = 0; nPos.y.length = 0; nPos.r.length = 0;
      for (var i = 0; i < frame.nuclei.length; i++) {
        var nu = frame.nuclei[i];
        var sx = s2x(nu.x), sy = s2y(nu.y);
        nPos.x.push(sx); nPos.y.push(sy);
        if (sx < -30 || sx > W + 30 || sy < -30 || sy > H + 30) { nPos.r.push(0); continue; }
        var ratio = (nu.cap > 0)
          ? Math.max(0.12, Math.min(1, nu.energy / nu.cap))
          : 0.5;
        var r = (5.0 + 6.0 * ratio) * Math.pow(zoom, 0.28) * dpr;
        if (MINIMAL) r *= 1.6;   // R8-D：核比球大（用户反馈 2.5 太大 → 1.6）
        nPos.r.push(r);
        var hue = ((isFiniteNum(nu.hue) ? nu.hue : synthHue(nu.id)) % 360 + 360) % 360;
        if (MINIMAL) {
          // R8-D 极简核：平涂 hue 大圆 + 细描边；中央灰点 = 交互闪烁指示
          ctx.beginPath();
          ctx.fillStyle = 'hsla(' + hue.toFixed(0) + ',52%,' + (42 + 14 * ratio).toFixed(1) + '%,0.92)';
          ctx.arc(sx, sy, r, 0, TAU); ctx.fill();
          ctx.beginPath();
          ctx.strokeStyle = 'hsla(' + hue.toFixed(0) + ',60%,70%,0.5)';
          ctx.lineWidth = 1 * dpr;
          ctx.arc(sx, sy, r, 0, TAU); ctx.stroke();
          // 中央点：交互后 800ms 内灰↔白正弦闪，否则恒灰
          var nowMs = (typeof performance !== 'undefined' && performance.now) ? performance.now() : Date.now();
          var ts = lastInteract[nu.id] || 0;
          var dtMs = nowMs - ts;
          var gray = '#808080';
          if (ts > 0 && dtMs < 800) {
            var ph = (Math.sin(dtMs / 800 * Math.PI * 6) + 1) / 2;  // ~3Hz
            var lv = Math.round(50 + 45 * ph);
            gray = 'hsl(0,0%,' + lv + '%)';
          }
          ctx.beginPath();
          ctx.fillStyle = gray;
          ctx.arc(sx, sy, Math.max(1.6, r * 0.30), 0, TAU); ctx.fill();
          continue;   // 极简分支不吃能量环/亮斑
        }
        // 外发光（同色）
        ctx.shadowBlur = 18 * dpr * glow;
        ctx.shadowColor = 'hsla(' + hue.toFixed(0) + ',72%,58%,.9)';
        ctx.beginPath();
        ctx.fillStyle = 'hsla(' + hue.toFixed(0) + ',72%,' + (50 + 10 * ratio).toFixed(1) + '%,0.95)';
        ctx.arc(sx, sy, r, 0, TAU); ctx.fill();
        ctx.shadowBlur = 0;
        // 内核亮斑
        ctx.beginPath();
        ctx.fillStyle = 'hsla(' + hue.toFixed(0) + ',85%,' + (78 + 8 * ratio).toFixed(1) + '%,' + (0.55 + 0.4 * ratio).toFixed(3) + ')';
        ctx.arc(sx - r * 0.25, sy - r * 0.25, r * 0.42, 0, TAU); ctx.fill();
        // 能量环（随 energy/cap 增长）。R7-D：描边用 palette.nucleus（包作者的风格层）；
        // 核本体填充/外发光/亮斑仍走遗传 hue——hue 是"这是哪个谱系"的第一识别通道，
        // 不交给材质包覆盖，否则谱系可分性（用户验收点）会被换包破坏。
        ctx.beginPath();
        ctx.strokeStyle = 'rgba(' + palette.nucleus + ',' + (0.35 + 0.3 * ratio).toFixed(3) + ')';
        ctx.lineWidth = 1.2 * dpr;
        ctx.arc(sx, sy, r + 3 * dpr, -Math.PI / 2, -Math.PI / 2 + TAU * ratio);
        ctx.stroke();
      }
      ctx.shadowBlur = 0;
    }

    // ---------- 施力脉冲：被施力的球上画脉冲环 + 短箭头（≈8 帧淡出） ----------
    // 数据来源：帧里 nucleus.forceTarget + forceX/forceY（实际加速度方向）。
    // 脉冲锚点在球上（不是核→球连线）；球在环面 wrap 时锚点跟随最近帧位置。
    function updatePulses(frame) {
      for (var i = 0; i < frame.nuclei.length; i++) {
        var nu = frame.nuclei[i];
        var tid = nu.forceTarget;
        if (tid != null && tid >= 0 && isFiniteNum(nu.forceX) && isFiniteNum(nu.forceY) &&
            (nu.forceX !== 0 || nu.forceY !== 0)) {
          var anchor = findBallPos(tid);
          if (anchor) {
            pulses.push({ x: anchor.x, y: anchor.y, dx: nu.forceX, dy: nu.forceY, age: 0 });
          }
        }
      }
      for (var p = pulses.length - 1; p >= 0; p--) {
        pulses[p].age++;
        if (pulses[p].age > PULSE_LIFE) pulses.splice(p, 1);
      }
      if (pulses.length > 96) pulses.splice(0, pulses.length - 96); // 兜底防暴涨
    }

    function findBallPos(ballId) {
      for (var i = 0; i < bPos.id.length; i++) if (bPos.id[i] === ballId) return { x: bPos.x[i], y: bPos.y[i] };
      return null;
    }

    function drawPulses(zoom) {
      for (var i = 0; i < pulses.length; i++) {
        var pu = pulses[i];
        var t = pu.age / PULSE_LIFE;                    // 0 → 1
        var alpha = (1 - t) * 0.85;
        var rr = (4 + 11 * t) * Math.pow(zoom, 0.3) * dpr;
        // 脉冲环
        ctx.strokeStyle = 'rgba(255,236,170,' + alpha.toFixed(3) + ')';
        ctx.lineWidth = 1.6 * dpr * (1 - t * 0.5);
        ctx.beginPath(); ctx.arc(pu.x, pu.y, rr, 0, TAU); ctx.stroke();
        // 短箭头（方向 = 实际加速度，画在球上）
        var mag = Math.sqrt(pu.dx * pu.dx + pu.dy * pu.dy);
        if (mag > 1e-6) {
          var ux = pu.dx / mag, uy = -pu.dy / mag;      // 世界 y 向上 → 屏幕 y 向下
          var len = (7 + 5 * (1 - t)) * dpr;
          var cx = pu.x, cy2 = pu.y;
          var ex = cx + ux * len, ey = cy2 + uy * len;
          var ang = Math.atan2(uy, ux);
          ctx.strokeStyle = 'rgba(255,244,200,' + alpha.toFixed(3) + ')';
          ctx.fillStyle = 'rgba(255,244,200,' + alpha.toFixed(3) + ')';
          ctx.lineWidth = 1.5 * dpr;
          ctx.beginPath(); ctx.moveTo(cx, cy2); ctx.lineTo(ex, ey); ctx.stroke();
          var head = 3.2 * dpr;
          ctx.beginPath();
          ctx.moveTo(ex + Math.cos(ang) * head, ey + Math.sin(ang) * head);
          ctx.lineTo(ex + Math.cos(ang + 2.5) * head, ey + Math.sin(ang + 2.5) * head);
          ctx.lineTo(ex + Math.cos(ang - 2.5) * head, ey + Math.sin(ang - 2.5) * head);
          ctx.closePath(); ctx.fill();
        }
      }
    }

    // ---------- 环面接缝提示 ----------
    var SEAM_MARGIN = 5;
    var SEAM_COLOR = 'rgba(255,196,86,.9)';

    function drawSeamArrow(sx, sy, dxs, dys) {
      var len = 9 * dpr, head = 3.6 * dpr;
      var ex = sx + dxs * len, ey = sy + dys * len;
      var ang = Math.atan2(dys, dxs);
      ctx.strokeStyle = SEAM_COLOR;
      ctx.fillStyle = SEAM_COLOR;
      ctx.lineWidth = 1.4 * dpr;
      ctx.beginPath();
      ctx.moveTo(sx - dxs * len * 0.25, sy - dys * len * 0.25);
      ctx.lineTo(ex, ey);
      ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(ex + Math.cos(ang) * head, ey + Math.sin(ang) * head);
      ctx.lineTo(ex + Math.cos(ang + 2.5) * head, ey + Math.sin(ang + 2.5) * head);
      ctx.lineTo(ex + Math.cos(ang - 2.5) * head, ey + Math.sin(ang - 2.5) * head);
      ctx.closePath();
      ctx.fill();
    }

    function drawSeamHints(frame) {
      var b = trace.bounds;
      function hint(x, y) {
        if (!isFiniteNum(x) || !isFiniteNum(y)) return;
        var sx = s2x(x), sy = s2y(y);
        if (sx < -20 || sx > W + 20 || sy < -20 || sy > H + 20) return;
        if (x - b.minX < SEAM_MARGIN) drawSeamArrow(sx - 6 * dpr, sy, -1, 0);
        if (b.maxX - x < SEAM_MARGIN) drawSeamArrow(sx + 6 * dpr, sy, 1, 0);
        if (y - b.minY < SEAM_MARGIN) drawSeamArrow(sx, sy + 6 * dpr, 0, 1);
        if (b.maxY - y < SEAM_MARGIN) drawSeamArrow(sx, sy - 6 * dpr, 0, -1);
      }
      for (var i = 0; i < frame.nuclei.length; i++) hint(frame.nuclei[i].x, frame.nuclei[i].y);
      for (var j = 0; j < frame.balls.length; j++) hint(frame.balls[j].x, frame.balls[j].y);
    }

    // ---------- 悬停命中：O(n) 距离测试（用上一帧屏幕坐标） ----------
    // 返回 { kind:'nucleus', index, data } 或 { kind:'none' }
    function hitTest(sxCss, syCss) {
      var sx = sxCss * dpr, sy = syCss * dpr;
      var best = -1, bestD = 26 * dpr * dpr; // 命中半径 ~26 设备像素
      for (var i = 0; i < nPos.x.length; i++) {
        if (!nPos.r[i]) continue;
        var dx = nPos.x[i] - sx, dy = nPos.y[i] - sy;
        var d2 = dx * dx + dy * dy;
        var lim = Math.max(26 * dpr, nPos.r[i] + 8 * dpr);
        if (d2 <= lim * lim && d2 < bestD) { bestD = d2; best = i; }
      }
      if (best >= 0 && lastFrame && lastFrame.nuclei[best]) {
        return { kind: 'nucleus', index: best, data: lastFrame.nuclei[best] };
      }
      return { kind: 'none' };
    }

    // ---------- R7-D 存档缩略图：把某帧一次性画进 128x72 离屏画布 ----------
    // 复用主渲染管线（调色板/天空/球/核/蓝细菌全部一致），换 ctx + 独立相机后
    // 完整还原，主画布视角与状态不受影响。bPos/nPos/pulses/lastFrame 会被短暂
    // 污染——一次性操作，下一次真实 draw 全量重建（悬停命中不受可感知影响）。
    function renderThumb(frame, tcanvas) {
      if (!trace || !frame || !tcanvas || !tcanvas.getContext) return false;
      var saved = { ctx: ctx, W: W, H: H, dayNight: dayNight,
        scale: cam.scale, cx: cam.cx, cy: cam.cy, fit: cam.fit };
      ctx = tcanvas.getContext('2d');
      W = tcanvas.width; H = tcanvas.height;
      dayNight = true;
      var b = trace.bounds;
      var ww = Math.max(1e-9, b.maxX - b.minX), wh = Math.max(1e-9, b.maxY - b.minY);
      cam.fit = Math.min(W / ww, H / wh) * 0.92;
      cam.scale = cam.fit;
      cam.cx = (b.minX + b.maxX) / 2;
      cam.cy = (b.minY + b.maxY) / 2;
      try { draw(frame, { w: W, h: H }); } catch (e) { return false; }   // 缩略图失败不拖垮主流程
      ctx = saved.ctx; W = saved.W; H = saved.H; dayNight = saved.dayNight;
      cam.scale = saved.scale; cam.cx = saved.cx; cam.cy = saved.cy; cam.fit = saved.fit;
      return true;
    }

    return {
      setTrace: setTrace,
      draw: draw,
      resize: resize,
      resetView: resetView,
      setDayNight: setDayNight,
      renderThumb: renderThumb,
      panBy: function (dxCss, dyCss) { panBy(dxCss * dpr, dyCss * dpr); },
      zoomAt: function (sxCss, syCss, factor) { zoomAt(sxCss * dpr, syCss * dpr, factor); },
      hitTest: hitTest,
      screenToWorld: function (sxCss, syCss) { return screenToWorld(sxCss * dpr, syCss * dpr); },
      camera: function () { return { scale: cam.scale, fit: cam.fit, cx: cam.cx, cy: cam.cy }; },
      setMinimal: function (v) { setMinimal(v); }   // R8-D：极简材质开关（默认开）
    };
  }
  return {
    create: create,
    setPalette: setPalette,     // R7-D：模块级（调色板全局一份，所有实例共用）
    getPalette: getPalette
  };
})();
