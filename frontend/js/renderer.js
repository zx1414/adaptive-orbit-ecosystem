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
    background: '233,228,211',
    ball_shield: '169,119,70',
    ball_worker: '71,125,109',
    ball_scout: '109,125,139',
    free_ball: '63,76,60',
    nucleus: '77,92,70'
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
    var fillLandscape = false;
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
      if (W !== w || H !== h || canvas.width !== w || canvas.height !== h) {
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
      cam.fit = Math.min(W / ww, H / wh);
      cam.scale = fillLandscape && W > H ? Math.max(W / ww, H / wh) : cam.fit;
      cam.cx = (b.minX + b.maxX) / 2;
      cam.cy = (b.minY + b.maxY) / 2;
    }

    function resetView() { fitCamera(); }

    function toggleFullMap() {
      fillLandscape = !fillLandscape;
      fitCamera();
      return !fillLandscape;
    }

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
      cam.cy = Math.min(b.maxY + pad, Math.max(b.minY - pad, cam.cy));
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
        var dEx = (dayness - 0.5) * 0.16;                     // >0 白昼，<0 夜晚
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
    var bPos = { x: [], y: [], r: [], id: [] };  // balls
    var lastFrame = null;
    var pulses = [];                       // 施力脉冲池 {id, x, y, dx, dy, age}
    var PULSE_LIFE = 8;                    // 帧数：约 8 帧淡出

    function draw(frame, fixed) {
      // fixed（R7-D 缩略图路径）：直接采用调用方给的尺寸，跳过主画布 resize
      if (fixed) { W = fixed.w; H = fixed.h; fitCamera(); }
      else if (!resize()) return;
      var tick = frame && isFiniteNum(frame.tick) ? frame.tick : 0;
      ctx.clearRect(0, 0, W, H);
      ctx.fillStyle = '#d8d2c1';
      ctx.fillRect(0, 0, W, H);
      if (!trace) return;
      var b = trace.bounds;
      // 地图铺满可用短边，长边余出的区域保留背景。镜头统一使用等比例缩放。
      ctx.save();
      ctx.beginPath();
      ctx.rect(s2x(b.minX), s2y(b.maxY),
        (b.maxX - b.minX) * cam.scale, (b.maxY - b.minY) * cam.scale);
      ctx.clip();
      // R7-D：palette.background 只在关闭昼夜天空时作纯色底。昼夜渐变是 SKY_STOPS
      // 的时间函数，若再把包背景色混进天空，包颜色会被稀释到看不出（取舍：两条路径各司其职）。
      if (dayNight) drawSky(tick, frame && frame.live ? frame.live.sun : null);
      else { ctx.fillStyle = 'rgba(' + palette.background + ',0.9)'; ctx.fillRect(0, 0, W, H); }
      drawGrid(b);
      var zoom = cam.scale / Math.max(1e-9, cam.fit);
      if (frame) {
        drawCyanoNetwork(frame, zoom);
        detectInteractions(frame);
        drawNuclei(frame, zoom);
        drawBalls(frame, zoom);
        updatePulses(frame);
        drawPulses(zoom);
        drawSeamHints(frame);
      }
      ctx.restore();
      drawBounds(b, zoom);
      lastFrame = frame;
    }

    function drawGrid(b) {
      var step = 32;
      var alpha = dayNight ? +(0.05 + 0.06 * (1 - skyDayness)).toFixed(3) : 0.07;
      ctx.strokeStyle = 'rgba(85,91,70,' + (alpha * 0.45) + ')';
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
      ctx.strokeStyle = 'rgba(77,88,68,' + (0.35 + 0.1 * Math.min(1, zoom)) + ')';
      ctx.lineWidth = 1.5 * dpr;
      ctx.strokeRect(s2x(b.minX), s2y(b.maxY),
        (b.maxX - b.minX) * cam.scale, (b.maxY - b.minY) * cam.scale);
    }

    // ---------- 蓝细菌网络（v3：f=连线拓扑，节点色=遗传 hue，先线后点） ----------
    // v3 契约：frame.cyanoLinks=[[idA,idB],...]（TraceParser 已把 f 键归一化到这里）；
    // 节点色相来自遗传数据。显示尺度固定，使同一能量在不同帧的亮度保持一致。
    function drawCyanoNetwork(frame, zoom) {
      var list = frame.cyano;
      var n = list.length;
      if (!n) return;
      var sxArr = new Array(n), syArr = new Array(n), rArr = new Array(n);
      var eScale = 100; // 显示用参照值，悬停显示实际能量
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
        ctx.strokeStyle = 'rgba(78,105,69,' + (0.12 + 0.10 * Math.min(1.6, zoom)).toFixed(3) + ')';
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
          if (cnm2.spore) {
            var sporeSize = Math.max(2.7 * dpr, rArr[jm] * 0.65);
            ctx.fillStyle = '#bd732b';
            ctx.beginPath();
            ctx.moveTo(sxm, sym - sporeSize);
            ctx.lineTo(sxm + sporeSize, sym);
            ctx.lineTo(sxm, sym + sporeSize);
            ctx.lineTo(sxm - sporeSize, sym);
            ctx.closePath();
            ctx.fill();
            continue;
          }
          var huem = ((isFiniteNum(cnm2.hue) ? cnm2.hue : 130) % 360 + 360) % 360;
          var enNm = Math.min(1, (isFiniteNum(cnm2.energy) ? Math.max(0, cnm2.energy) : 0) / eScale);
          ctx.beginPath();
          ctx.fillStyle = 'hsl(' + (95 + (huem % 60) * 0.45).toFixed(0) + ',25%,' + (69 - 37 * Math.sqrt(enNm)).toFixed(1) + '%)';
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
        ctx.fillStyle = 'hsl(' + hue.toFixed(0) + ',62%,' + (27 + 46 * Math.sqrt(Math.min(1, enN2))).toFixed(1) + '%)';
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

    // 球以形状和描边区分类型，内部填充显示所存能量。
    function ballColors() {
      return {
        fill: ['rgb(' + palette.ball_worker + ')', 'rgb(' + palette.ball_scout + ')', 'rgb(' + palette.ball_shield + ')'],
        freeRing: 'rgba(' + palette.free_ball + ',.95)'
      };
    }

    function drawBalls(frame, zoom) {
      var BC = ballColors();
      var r = Math.max(4.5, Math.min(12, 4.8 * Math.pow(zoom, 0.23))) * dpr;
      bPos.x.length = 0; bPos.y.length = 0; bPos.r.length = 0; bPos.id.length = 0;
      for (var i = 0; i < frame.balls.length; i++) {
        var ball = frame.balls[i];
        var sx = s2x(ball.x), sy = s2y(ball.y);
        bPos.x.push(sx); bPos.y.push(sy); bPos.r.push(r); bPos.id.push(ball.id);
        var outer = r * 1.25 + 4 * dpr;
        if (sx < -outer || sx > W + outer || sy < -outer || sy > H + outer) continue;
        var type = ball.type === 1 ? 1 : (ball.type === 2 ? 2 : 0);
        var free = !ball.owner || ball.owner === 0;
        var fraction = isFiniteNum(ball.energy) ? Math.max(0, Math.min(1, ball.energy / 60)) : 0;
        traceShape(sx, sy, r, type);
        ctx.fillStyle = '#eeeadd';
        ctx.fill();
        if (fraction > 0) {
          ctx.save();
          ctx.clip();
          var halfHeight = type === 1 ? r * 1.25 : r;
          var height = 2 * halfHeight * fraction;
          ctx.fillStyle = BC.fill[type];
          ctx.fillRect(sx - r * 1.25, sy + halfHeight - height, 2.5 * r, height);
          ctx.restore();
        }
        ctx.strokeStyle = '#f3efe3';
        ctx.lineWidth = 4 * dpr;
        ctx.stroke();
        ctx.strokeStyle = BC.fill[type];
        ctx.lineWidth = 1.35 * dpr;
        ctx.stroke();
        if (free) {
          ctx.lineWidth = 1.2 * dpr;
          ctx.setLineDash([2.5 * dpr, 2.5 * dpr]);
          ctx.strokeStyle = BC.freeRing;
          ctx.beginPath(); ctx.arc(sx, sy, r * 1.25 + 2 * dpr, 0, TAU); ctx.stroke();
          ctx.setLineDash([]);
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
          var a = TAU * k / 6;
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
        var ratio = (nu.cap > 0 && isFiniteNum(nu.energy))
          ? Math.max(0, Math.min(1, nu.energy / nu.cap))
          : null;
        var r = Math.max(7, Math.min(24, (8 + 3 * (ratio || 0)) * Math.pow(zoom, 0.25))) * dpr;
        nPos.r.push(r);
        var hue = ((isFiniteNum(nu.hue) ? nu.hue : synthHue(nu.id)) % 360 + 360) % 360;
        if (MINIMAL) {
          // R8-D 极简核：平涂 hue 大圆 + 细描边；中央灰点 = 交互闪烁指示
          ctx.beginPath();
          ctx.fillStyle = 'hsla(' + hue.toFixed(0) + ',24%,' + (58 + 5 * ratio).toFixed(1) + '%,1)';
          ctx.arc(sx, sy, r, 0, TAU); ctx.fill();
          ctx.beginPath();
          ctx.strokeStyle = '#405344';
          ctx.lineWidth = 1 * dpr;
          ctx.arc(sx, sy, r, 0, TAU); ctx.stroke();
          // 中央点：交互后 800ms 内灰↔白正弦闪，否则恒灰
          var nowMs = (typeof performance !== 'undefined' && performance.now) ? performance.now() : Date.now();
          var ts = lastInteract[nu.id] || 0;
          var dtMs = nowMs - ts;
          var gray = '#3f5143';
          if (ts > 0 && dtMs < 800) {
            var ph = (Math.sin(dtMs / 800 * Math.PI * 6) + 1) / 2;  // ~3Hz
            var lv = Math.round(50 + 45 * ph);
            gray = 'hsl(0,0%,' + lv + '%)';
          }
          ctx.beginPath();
          ctx.fillStyle = gray;
          ctx.arc(sx, sy, Math.max(1.6, r * 0.30), 0, TAU); ctx.fill();
          if (ratio !== null) drawEnergyRing(sx, sy, r + 3 * dpr, ratio);
          if (nu.feedingTicks > 0) {
            ctx.beginPath();
            ctx.strokeStyle = '#a14b2a';
            ctx.lineWidth = 2 * dpr;
            ctx.arc(sx, sy, r + 7 * dpr, -Math.PI / 2,
                    -Math.PI / 2 + TAU * Math.min(1, nu.feedingTicks / 12));
            ctx.stroke();
          }
          continue;
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
        if (ratio !== null) drawEnergyRing(sx, sy, r + 3 * dpr, ratio);
      }
      ctx.shadowBlur = 0;
    }

    function drawEnergyRing(x, y, radius, ratio) {
      ctx.beginPath();
      ctx.arc(x, y, radius, 0, TAU);
      ctx.strokeStyle = '#f1ede0';
      ctx.lineWidth = 4 * dpr;
      ctx.stroke();
      ctx.strokeStyle = 'rgba(' + palette.nucleus + ',0.30)';
      ctx.lineWidth = 2 * dpr;
      ctx.stroke();
      if (ratio > 0) {
        var start = -Math.PI / 2;
        ctx.beginPath();
        ctx.arc(x, y, radius, start, start + TAU * ratio);
        ctx.strokeStyle = ratio < 0.2 ? '#a35e4d' : (ratio < 0.5 ? '#a67b49' : '#496d51');
        ctx.stroke();
      }
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
    function hitTest(sxCss, syCss) {
      var sx = sxCss * dpr, sy = syCss * dpr;
      if (!trace || !lastFrame) return { kind: 'none' };
      var b = trace.bounds;
      if (sx < s2x(b.minX) || sx > s2x(b.maxX) || sy < s2y(b.maxY) || sy > s2y(b.minY)) {
        return { kind: 'none' };
      }
      var hit = closestAt(bPos, sx, sy, 1.25, 2 * dpr);
      if (hit >= 0) return { kind: 'ball', data: lastFrame.balls[hit] };
      hit = closestAt(nPos, sx, sy, 1, 5 * dpr);
      if (hit >= 0) return { kind: 'nucleus', data: lastFrame.nuclei[hit] };
      var nearest = (5 * dpr) ** 2;
      var cyano = lastFrame.cyano;
      for (var i = 0; i < cyano.length; i++) {
        var dx = s2x(cyano[i].x) - sx, dy = s2y(cyano[i].y) - sy;
        var distance = dx * dx + dy * dy;
        if (distance < nearest) { nearest = distance; hit = i; }
      }
      if (hit >= 0) return { kind: 'cyano', data: cyano[hit] };
      return { kind: 'none' };
    }

    function closestAt(positions, x, y, scale, margin) {
      var best = -1, nearest = Infinity;
      for (var i = 0; i < positions.x.length; i++) {
        if (!positions.r[i]) continue;
        var dx = positions.x[i] - x, dy = positions.y[i] - y;
        var distance = dx * dx + dy * dy;
        var limit = positions.r[i] * scale + margin;
        if (distance <= limit * limit && distance < nearest) {
          nearest = distance;
          best = i;
        }
      }
      return best;
    }

    // 用独立的渲染器生成存档缩略图，避免缩略图改变当前取景和悬停坐标。
    function renderThumb(frame, tcanvas) {
      if (!trace || !frame || !tcanvas || !tcanvas.getContext) return false;
      try {
        var thumb = create(tcanvas);
        thumb.toggleFullMap();
        thumb.setTrace(trace);
        thumb.setMinimal(MINIMAL);
        thumb.draw(frame, { w: tcanvas.width, h: tcanvas.height });
      } catch (e) { return false; }
      return true;
    }

    return {
      setTrace: setTrace,
      draw: draw,
      resize: resize,
      resetView: resetView,
      toggleFullMap: toggleFullMap,
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
