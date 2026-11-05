/* agentsim 折线图 V2 — 纯 canvas 手绘 · 零依赖
 * 用法：
 *   var chart = Charts.create(canvasEl, {});
 *   var scatter = Charts.createScatter(canvasEl, {});  // 基因散点图（v3 g 键）
 *   chart.setData(series, keys);   // keys: [{key,color,label}] 数组（V2 食物曲线已移除）
 *   chart.setCursor(frameIndex);
 *   chart.resize();
 * 手绘职责：参考线 → 面积（仅第一行）→ 折线 → 游标 → min/max/当前值标注。
 * 同一图内多行共享一个量程；非有限值断线（不连跨）。
 */
window.Charts = (function () {
  'use strict';
  var TAU = Math.PI * 2;
  var DIM = 'rgba(143,163,200,0.85)';
  var PALETTE = ['#5ad1ff', '#48dc80', '#ffa656', '#ff6b81', '#c792ff'];

  function fmtNum(v) {
    var a = Math.abs(v);
    if (a >= 10000) return (v / 1000).toFixed(1) + 'k';
    if (a >= 100) return v.toFixed(0);
    if (a >= 10) return v.toFixed(1);
    return v.toFixed(2);
  }

  function hexA(hex, a) {
    var h = String(hex || '#ffffff').replace('#', '');
    if (h.length === 3) h = h[0] + h[0] + h[1] + h[1] + h[2] + h[2];
    var n = parseInt(h, 16) || 0;
    return 'rgba(' + ((n >> 16) & 255) + ',' + ((n >> 8) & 255) + ',' + (n & 255) + ',' + a + ')';
  }

  function create(canvasEl, opts) {
    opts = opts || {};
    var ctx = canvasEl.getContext('2d');
    var dpr = Math.max(1, window.devicePixelRatio || 1);
    var rows = [];    // [{key,color,label,values[]}]
    var cursor = -1;

    function resize() {
      var rect = canvasEl.getBoundingClientRect ? canvasEl.getBoundingClientRect() : null;
      var cw = (rect && rect.width) || canvasEl.clientWidth || canvasEl.width || 300;
      var ch = (rect && rect.height) || canvasEl.clientHeight || canvasEl.height || 80;
      var w = Math.max(1, Math.round(cw * dpr));
      var h = Math.max(1, Math.round(ch * dpr));
      if (canvasEl.width !== w || canvasEl.height !== h) { canvasEl.width = w; canvasEl.height = h; }
      draw();
      return true;
    }

    function setData(series, key) {
      var list = Array.isArray(series) ? series : [];
      var keys = Array.isArray(key) ? key : [key];
      rows = [];
      for (var i = 0; i < keys.length; i++) {
        var k = keys[i];
        var spec = (k && typeof k === 'object') ? k : { key: k };
        var name = spec.key || ('v' + i);
        var vals = new Array(list.length);
        for (var j = 0; j < list.length; j++) {
          var v = list[j] ? list[j][name] : undefined;
          vals[j] = (typeof v === 'number' && isFinite(v)) ? v : NaN;
        }
        rows.push({ key: name, color: spec.color || PALETTE[i % PALETTE.length],
          label: spec.label || name, values: vals });
      }
      draw();
    }

    function setCursor(i) {
      var v = (typeof i === 'number' && isFinite(i)) ? Math.round(i) : -1;
      if (v !== cursor) { cursor = v; draw(); }
    }

    function extent() {
      var mn = Infinity, mx = -Infinity, count = 0;
      for (var r = 0; r < rows.length; r++) {
        var vals = rows[r].values;
        for (var i = 0; i < vals.length; i++) {
          var v = vals[i];
          if (typeof v !== 'number' || !isFinite(v)) continue;
          if (v < mn) mn = v;
          if (v > mx) mx = v;
          count++;
        }
      }
      if (!count) return null;
      if (mx - mn < 1e-9) { mn -= 1; mx += 1; }
      var pad = (mx - mn) * 0.12;
      return { min: mn - pad, max: mx + pad };
    }

    function draw() {
      var W = canvasEl.width, H = canvasEl.height;
      if (!W || !H) return;
      ctx.clearRect(0, 0, W, H);
      var padL = 8 * dpr, padR = 10 * dpr, padT = 9 * dpr, padB = 7 * dpr;
      var iw = W - padL - padR, ih = H - padT - padB;

      ctx.strokeStyle = 'rgba(122,152,220,0.14)';
      ctx.lineWidth = 1;
      ctx.beginPath();
      for (var g = 0; g <= 2; g++) {
        var gy = Math.round(padT + ih * g / 2) + 0.5;
        ctx.moveTo(padL, gy); ctx.lineTo(padL + iw, gy);
      }
      ctx.stroke();

      if (iw < 4 || ih < 4) return;
      var ext = extent();
      if (!ext) {
        ctx.fillStyle = DIM;
        ctx.font = (10 * dpr) + 'px Consolas,monospace';
        ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
        ctx.fillText('暂无数据', padL + iw / 2, padT + ih / 2);
        return;
      }

      var N = rows[0].values.length;
      function xAt(i) { return padL + (N <= 1 ? iw / 2 : iw * i / (N - 1)); }
      function yAt(v) { return padT + ih * (1 - (v - ext.min) / (ext.max - ext.min)); }

      for (var r = 0; r < rows.length; r++) {
        var vals = rows[r].values, color = rows[r].color, started = false;
        if (r === 0) {
          ctx.beginPath();
          var firstX = null, lastX = null;
          for (var i2 = 0; i2 < vals.length; i2++) {
            if (!isFinite(vals[i2])) continue;
            var xx = xAt(i2), yy = yAt(vals[i2]);
            if (!started) { ctx.moveTo(xx, padT + ih); ctx.lineTo(xx, yy); firstX = xx; started = true; }
            else ctx.lineTo(xx, yy);
            lastX = xx;
          }
          if (started) {
            ctx.lineTo(lastX, padT + ih); ctx.lineTo(firstX, padT + ih); ctx.closePath();
            ctx.fillStyle = hexA(color, 0.13);
            ctx.fill();
          }
        }
        started = false;
        ctx.beginPath();
        for (var i3 = 0; i3 < vals.length; i3++) {
          if (!isFinite(vals[i3])) { started = false; continue; }
          var x3 = xAt(i3), y3 = yAt(vals[i3]);
          if (!started) { ctx.moveTo(x3, y3); started = true; }
          else ctx.lineTo(x3, y3);
        }
        ctx.strokeStyle = color;
        ctx.lineWidth = 1.6 * dpr;
        ctx.lineJoin = 'round'; ctx.lineCap = 'round';
        ctx.stroke();
      }

      if (cursor >= 0 && N > 0 && cursor < N) {
        var cx = xAt(cursor);
        ctx.strokeStyle = 'rgba(90,209,255,0.55)';
        ctx.lineWidth = 1;
        if (ctx.setLineDash) ctx.setLineDash([3 * dpr, 3 * dpr]);
        ctx.beginPath(); ctx.moveTo(cx, padT); ctx.lineTo(cx, padT + ih); ctx.stroke();
        if (ctx.setLineDash) ctx.setLineDash([]);
        for (var r2 = 0; r2 < rows.length; r2++) {
          var cv = rows[r2].values[cursor];
          if (!isFinite(cv)) continue;
          ctx.beginPath();
          ctx.fillStyle = rows[r2].color;
          ctx.arc(cx, yAt(cv), 2.4 * dpr, 0, TAU);
          ctx.fill();
        }
      }

      ctx.font = (9.5 * dpr) + 'px Consolas,monospace';
      ctx.fillStyle = DIM;
      ctx.textAlign = 'right';
      ctx.textBaseline = 'top';
      ctx.fillText(fmtNum(ext.max), padL + iw, 1 * dpr);
      ctx.textBaseline = 'alphabetic';
      ctx.fillText(fmtNum(ext.min), padL + iw, Math.min(H - 1, padT + ih + 8 * dpr));
      var cur = rows[0].values[cursor];
      if (typeof cur === 'number' && isFinite(cur)) {
        ctx.fillStyle = rows[0].color;
        ctx.textAlign = 'left';
        ctx.textBaseline = 'top';
        ctx.fillText(fmtNum(cur), padL, 1 * dpr);
      }
    }

    resize();
    return { setData: setData, setCursor: setCursor, resize: resize, draw: draw };
  }

  // ── 基因散点图（生态可视化：X=maxSpeed，Y=energyThreshold，点=核，颜色=hue）──
  // 数据来自 v3 帧的 g 键（Genome.h 声明序 26 数）或解析归一化的 nucleus.genome。
  // 目测验收：种群随演化分化出「快而饿 / 慢而富」的策略簇，且簇色（血统）内聚。
  function createScatter(canvasEl, opts) {
    opts = opts || {};
    var ctx = canvasEl.getContext('2d');
    var dpr = Math.max(1, window.devicePixelRatio || 1);
    var pts = [];          // [{x, y, hue, id, genome}]
    var hover = -1;
    var meta = { xKey: 'maxSpeed', yKey: 'energyThreshold' };

    function resize() {
      var rect = canvasEl.getBoundingClientRect ? canvasEl.getBoundingClientRect() : null;
      var cw = (rect && rect.width) || canvasEl.clientWidth || canvasEl.width || 260;
      var ch = (rect && rect.height) || canvasEl.clientHeight || canvasEl.height || 140;
      var w = Math.max(1, Math.round(cw * dpr));
      var h = Math.max(1, Math.round(ch * dpr));
      if (canvasEl.width !== w || canvasEl.height !== h) { canvasEl.width = w; canvasEl.height = h; }
      draw();
      return true;
    }

    function setData(points, metaOpt) {
      var list = Array.isArray(points) ? points : [];
      pts = [];
      for (var i = 0; i < list.length; i++) {
        var p = list[i];
        if (!p) continue;
        if (typeof p.x !== 'number' || !isFinite(p.x)) continue;
        if (typeof p.y !== 'number' || !isFinite(p.y)) continue;
        pts.push(p);
      }
      if (metaOpt && typeof metaOpt === 'object') meta = metaOpt;
      hover = -1;
      draw();
    }

    function extent(key) {
      var mn = Infinity, mx = -Infinity;
      for (var i = 0; i < pts.length; i++) {
        var v = pts[i][key];
        if (typeof v !== 'number' || !isFinite(v)) continue;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
      }
      if (mn === Infinity) return null;
      if (mx - mn < 1e-9) { mn -= 1; mx += 1; }
      var pad = (mx - mn) * 0.14;
      return { min: mn - pad, max: mx + pad };
    }

    function draw() {
      var W = canvasEl.width, H = canvasEl.height;
      if (!W || !H) return;
      ctx.clearRect(0, 0, W, H);
      var padL = 30 * dpr, padR = 8 * dpr, padT = 8 * dpr, padB = 17 * dpr;
      var iw = W - padL - padR, ih = H - padT - padB;
      var DIM = 'rgba(143,163,200,0.85)';
      ctx.strokeStyle = 'rgba(122,152,220,0.20)';
      ctx.lineWidth = 1;
      ctx.strokeRect(padL + 0.5, padT + 0.5, Math.max(1, iw), Math.max(1, ih));
      if (iw < 8 || ih < 8) return;
      var ex = extent('x'), ey = extent('y');
      ctx.font = (9 * dpr) + 'px Consolas,monospace';
      ctx.fillStyle = DIM;
      if (!ex || !ey) {
        ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
        ctx.fillText('暂无数据（需 g 键 / 基因组导出）', padL + iw / 2, padT + ih / 2);
        return;
      }
      function px(v) { return padL + iw * (v - ex.min) / (ex.max - ex.min); }
      function py(v) { return padT + ih * (1 - (v - ey.min) / (ey.max - ey.min)); }
      // 网格 + 轴标注
      ctx.strokeStyle = 'rgba(122,152,220,0.12)';
      ctx.beginPath();
      for (var g = 1; g < 3; g++) {
        var gx = padL + iw * g / 3; ctx.moveTo(gx, padT); ctx.lineTo(gx, padT + ih);
        var gy = padT + ih * g / 3; ctx.moveTo(padL, gy); ctx.lineTo(padL + iw, gy);
      }
      ctx.stroke();
      ctx.textAlign = 'right'; ctx.textBaseline = 'middle';
      ctx.fillText(fmtNum(ey.max), padL - 3 * dpr, padT + 4 * dpr);
      ctx.fillText(fmtNum(ey.min), padL - 3 * dpr, padT + ih - 3 * dpr);
      ctx.textAlign = 'left'; ctx.textBaseline = 'alphabetic';
      ctx.fillText(' ' + meta.xKey + ' →', padL + iw - 56 * dpr, H - 4 * dpr);
      ctx.textAlign = 'right';
      ctx.fillText(fmtNum(ex.max), padL + iw, H - 4 * dpr);
      // 点：颜色 = hue（谱系），大小随 energy（有则放大）
      for (var i = 0; i < pts.length; i++) {
        var p = pts[i];
        var hue = ((typeof p.hue === 'number' && isFinite(p.hue) ? p.hue : 210) % 360 + 360) % 360;
        var sx = px(p.x), sy = py(p.y);
        var rr = (2.6 + 1.6 * Math.min(1, (typeof p.energy === 'number' ? p.energy : 60) / 200)) * dpr;
        ctx.shadowBlur = (hover === i ? 10 : 5) * dpr;
        ctx.shadowColor = 'hsla(' + hue.toFixed(0) + ',75%,60%,.9)';
        ctx.fillStyle = 'hsl(' + hue.toFixed(0) + ',72%,' + (hover === i ? 70 : 58) + '%)';
        ctx.beginPath();
        ctx.arc(sx, sy, rr, 0, TAU);
        ctx.fill();
        ctx.shadowBlur = 0;
      }
    }

    // 悬停：返回最近点（CSS 像素），供 tooltip 展示全部 24 基因
    function hitTest(mxCss, myCss) {
      var W = canvasEl.width, H = canvasEl.height;
      if (!W || !H || !pts.length) return -1;
      var mx = mxCss * dpr, my = myCss * dpr;
      var padL = 30 * dpr, padR = 8 * dpr, padT = 8 * dpr, padB = 17 * dpr;
      var iw = W - padL - padR, ih = H - padT - padB;
      var ex = extent('x'), ey = extent('y');
      if (!ex || !ey) return -1;
      var best = -1, bestD = 12 * dpr * 12 * dpr;
      for (var i = 0; i < pts.length; i++) {
        var sx = padL + iw * (pts[i].x - ex.min) / (ex.max - ex.min);
        var sy = padT + ih * (1 - (pts[i].y - ey.min) / (ey.max - ey.min));
        var dx = sx - mx, dy = sy - my;
        var d2 = dx * dx + dy * dy;
        if (d2 < bestD) { bestD = d2; best = i; }
      }
      if (best !== hover) { hover = best; draw(); }
      return best;
    }

    function clearHover() { if (hover !== -1) { hover = -1; draw(); } }

    resize();
    return { setData: setData, resize: resize, draw: draw, hitTest: hitTest, clearHover: clearHover, pts: function () { return pts; } };
  }

  return { create: create, createScatter: createScatter };
})();
