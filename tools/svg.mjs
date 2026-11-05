// svg.mjs — 零依赖内联 SVG 绘图工具（analyze.mjs 专用，也可独立复用）。
// 只用字符串拼接生成 SVG；不含任何随机数（报告必须可复现）。
// 颜色选用 GitHub 深浅两版主题都可读的中饱和色；文字/坐标轴用中性灰。

/** 数据系列调色板（按序取用，可循环）。 */
export const PALETTE = [
  '#58a6ff', '#3fb950', '#d29922', '#f778ba', '#bc8cff',
  '#39c5cf', '#ff7b72', '#7ee787', '#ffa657', '#79c0ff',
  '#d2a8ff', '#56d364',
];

/** hue(度) → CSS hsl 颜色。 */
export function hsl(hdeg, s = 85, l = 60) {
  const h = ((Number(hdeg) % 360) + 360) % 360;
  return 'hsl(' + h.toFixed(1) + ',' + s + '%,' + l + '%)';
}

/** 数值 → 紧凑字符串（去尾零；非有限值给占位）。 */
export function fmt(v, digits = 4) {
  if (!Number.isFinite(v)) return '—';
  const s = Number(v.toFixed(digits)).toString();
  return s;
}

/** XML 文本转义。 */
export function esc(s) {
  return String(s)
    .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

function niceNum(range, round) {
  const exp = Math.floor(Math.log10(range));
  const f = range / Math.pow(10, exp);
  let nf;
  if (round) nf = f < 1.5 ? 1 : f < 3 ? 2 : f < 7 ? 5 : 10;
  else nf = f <= 1 ? 1 : f <= 2 ? 2 : f <= 5 ? 5 : 10;
  return nf * Math.pow(10, exp);
}

/** 好看的坐标刻度（Heckbert 算法）。max<=min 或非有限时返回退化刻度。 */
export function niceTicks(min, max, count = 5) {
  if (!Number.isFinite(min) || !Number.isFinite(max) || !(max > min)) {
    if (Number.isFinite(min) && min === max) {
      const d = Math.abs(min) > 0 ? Math.abs(min) * 0.1 : 1;
      return [min - d, min, min + d];
    }
    return [0, 1];
  }
  const range = niceNum(max - min, false);
  const step = niceNum(range / Math.max(1, count - 1), true);
  const t0 = Math.floor(min / step) * step;
  const ticks = [];
  for (let t = t0; t <= max + step * 0.5; t += step) {
    ticks.push(Number(t.toPrecision(12)));
    if (ticks.length > 64) break; // 防御：异常输入时不要炸出几千条刻度
  }
  return ticks;
}

const AXIS = '#8b949e';
const GRID = '#8b949e';
const TEXT = '#8b949e';

/** 迷你折线（表格内用）。values: number[]；全空/单点时给占位框。 */
export function sparkline(values, opts = {}) {
  const w = opts.width ?? 120;
  const h = opts.height ?? 22;
  const color = opts.color ?? '#58a6ff';
  const pad = 2;
  const vs = (values ?? []).filter(Number.isFinite);
  if (vs.length < 2) {
    return '<svg xmlns="http://www.w3.org/2000/svg" width="' + w + '" height="' + h + '">' +
      '<rect x="0.5" y="0.5" width="' + (w - 1) + '" height="' + (h - 1) +
      '" fill="none" stroke="' + GRID + '" stroke-opacity="0.3"/></svg>';
  }
  let min = Infinity, max = -Infinity;
  for (const v of vs) { if (v < min) min = v; if (v > max) max = v; }
  if (max === min) { max += 1; min -= 1; }
  const pts = vs.map((v, i) => {
    const x = pad + (i / (vs.length - 1)) * (w - 2 * pad);
    const y = h - pad - ((v - min) / (max - min)) * (h - 2 * pad);
    return x.toFixed(1) + ',' + y.toFixed(1);
  }).join(' ');
  return '<svg xmlns="http://www.w3.org/2000/svg" width="' + w + '" height="' + h + '">' +
    '<polyline points="' + pts + '" fill="none" stroke="' + color + '" stroke-width="1.5"/>' +
    '</svg>';
}

function downsample(points, maxPoints) {
  if (!maxPoints || points.length <= maxPoints) return points;
  const stride = Math.ceil(points.length / maxPoints);
  const out = [];
  for (let i = 0; i < points.length; i += stride) out.push(points[i]);
  const last = points[points.length - 1];
  if (out[out.length - 1] !== last) out.push(last);
  return out;
}

/**
 * 多序列折线图（可选第二 y 轴）。
 * series: [{ name, color?, points:[[x,y],...], axis?: 'left'|'right' }]
 * 返回 <svg> 字符串。
 */
export function multiLineChart(spec) {
  const w = spec.width ?? 640;
  const h = spec.height ?? 240;
  const showLegend = spec.legend !== false;
  const series = (spec.series ?? []).filter(s => s.points && s.points.length > 0);
  if (series.length === 0) {
    return '<svg xmlns="http://www.w3.org/2000/svg" width="' + w + '" height="' + h + '">' +
      '<text x="8" y="' + (h / 2) + '" fill="' + TEXT + '" font-size="12">no data</text></svg>';
  }
  const hasRight = series.some(s => s.axis === 'right');
  const padL = 52, padR = hasRight ? 56 : 14, padT = 24, padB = 30;
  const legendH = showLegend ? Math.ceil(series.length / 3) * 14 + 4 : 0;
  const plotW = w - padL - padR;
  const plotH = h - padT - padB - legendH;
  if (plotW <= 10 || plotH <= 10) {
    return '<svg xmlns="http://www.w3.org/2000/svg" width="' + w + '" height="' + h + '"></svg>';
  }

  // 域
  let xMin = Infinity, xMax = -Infinity;
  const yDomains = { left: [Infinity, -Infinity], right: [Infinity, -Infinity] };
  const prepared = series.map(s => {
    const pts = downsample(s.points, spec.maxPoints ?? 2000);
    const axis = s.axis === 'right' ? 'right' : 'left';
    for (const [x, y] of pts) {
      if (!Number.isFinite(x) || !Number.isFinite(y)) continue;
      if (x < xMin) xMin = x; if (x > xMax) xMax = x;
      const d = yDomains[axis];
      if (y < d[0]) d[0] = y; if (y > d[1]) d[1] = y;
    }
    return { name: s.name, color: s.color ?? PALETTE[0], axis, pts };
  }).filter(s => s.pts.length > 0);
  if (xMax === xMin) { xMax = xMin + 1; }
  for (const k of Object.keys(yDomains)) {
    const d = yDomains[k];
    if (d[0] === Infinity) { d[0] = 0; d[1] = 1; }
    if (d[1] === d[0]) { d[1] = d[0] + 1; }
  }
  const lx0 = spec.xMin ?? xMin, lx1 = spec.xMax ?? xMax;
  const scaleX = (x) => padL + ((x - lx0) / (lx1 - lx0)) * plotW;
  const scaleL = (y) => padT + plotH - ((y - yDomains.left[0]) / (yDomains.left[1] - yDomains.left[0])) * plotH;
  const scaleR = (y) => padT + plotH - ((y - yDomains.right[0]) / (yDomains.right[1] - yDomains.right[0])) * plotH;
  const scale = (axis) => (axis === 'right' ? scaleR : scaleL);

  const parts = [];
  parts.push('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ' + w + ' ' + h +
    '" width="' + w + '" height="' + h + '" font-family="monospace" font-size="10">');
  if (spec.title) {
    parts.push('<text x="' + padL + '" y="14" fill="' + TEXT + '">' + esc(spec.title) + '</text>');
  }
  // 网格 + 左轴刻度
  const ytL = niceTicks(yDomains.left[0], yDomains.left[1], 5);
  for (const t of ytL) {
    const y = scaleL(t);
    if (y < padT - 0.5 || y > padT + plotH + 0.5) continue;
    parts.push('<line x1="' + padL + '" y1="' + y.toFixed(1) + '" x2="' + (padL + plotW) +
      '" y2="' + y.toFixed(1) + '" stroke="' + GRID + '" stroke-opacity="0.18"/>');
    parts.push('<text x="' + (padL - 4) + '" y="' + (y + 3).toFixed(1) +
      '" fill="' + TEXT + '" text-anchor="end">' + esc(fmt(t, 3)) + '</text>');
  }
  // 右轴刻度
  if (hasRight) {
    const ytR = niceTicks(yDomains.right[0], yDomains.right[1], 5);
    for (const t of ytR) {
      const y = scaleR(t);
      if (y < padT - 0.5 || y > padT + plotH + 0.5) continue;
      parts.push('<text x="' + (padL + plotW + 4) + '" y="' + (y + 3).toFixed(1) +
        '" fill="' + TEXT + '">' + esc(fmt(t, 3)) + '</text>');
    }
  }
  // x 刻度
  const xt = niceTicks(lx0, lx1, 6);
  for (const t of xt) {
    const x = scaleX(t);
    if (x < padL - 0.5 || x > padL + plotW + 0.5) continue;
    parts.push('<line x1="' + x.toFixed(1) + '" y1="' + padT + '" x2="' + x.toFixed(1) +
      '" y2="' + (padT + plotH) + '" stroke="' + GRID + '" stroke-opacity="0.12"/>');
    parts.push('<text x="' + x.toFixed(1) + '" y="' + (padT + plotH + 12) +
      '" fill="' + TEXT + '" text-anchor="middle">' + esc(fmt(t, 3)) + '</text>');
  }
  // 边框
  parts.push('<rect x="' + padL + '" y="' + padT + '" width="' + plotW + '" height="' + plotH +
    '" fill="none" stroke="' + AXIS + '" stroke-opacity="0.5"/>');
  // 序列
  for (const s of prepared) {
    const pts = s.pts.filter(([x, y]) => Number.isFinite(x) && Number.isFinite(y));
    if (pts.length === 0) continue;
    const str = pts.map(([x, y]) =>
      scaleX(x).toFixed(1) + ',' + scale(s.axis)(y).toFixed(1)).join(' ');
    parts.push('<polyline points="' + str + '" fill="none" stroke="' + s.color +
      '" stroke-width="' + (s.axis === 'right' ? 1.4 : 1.7) +
      '" stroke-opacity="0.95" stroke-linejoin="round"/>');
  }
  // 轴标题
  if (spec.yLabel) {
    parts.push('<text x="' + (padL + 4) + '" y="' + (padT - 6) + '" fill="' + TEXT + '">' +
      esc(spec.yLabel) + '</text>');
  }
  if (hasRight && spec.y2Label) {
    parts.push('<text x="' + (padL + plotW - 4) + '" y="' + (padT - 6) +
      '" fill="' + TEXT + '" text-anchor="end">' + esc(spec.y2Label) + '</text>');
  }
  if (spec.xLabel) {
    parts.push('<text x="' + (padL + plotW) + '" y="' + (h - 4) + '" fill="' + TEXT +
      '" text-anchor="end">' + esc(spec.xLabel) + '</text>');
  }
  // 图例
  if (showLegend) {
    prepared.forEach((s, i) => {
      const col = i % 3, row = Math.floor(i / 3);
      const x = padL + 4 + col * (plotW / 3);
      const y = padT + plotH + legendH + 2 + row * 14;
      const tag = s.axis === 'right' ? '（右轴）' : '';
      parts.push('<rect x="' + x + '" y="' + (y - 8) + '" width="9" height="9" fill="' + s.color + '"/>');
      parts.push('<text x="' + (x + 13) + '" y="' + y + '" fill="' + TEXT + '">' +
        esc(s.name + tag) + '</text>');
    });
  }
  parts.push('</svg>');
  return parts.join('');
}

/**
 * 散点图。points: [{x, y, color?, radius?, opacity?}]
 */
export function scatterChart(spec) {
  const w = spec.width ?? 520;
  const h = spec.height ?? 380;
  const points = (spec.points ?? []).filter(p => Number.isFinite(p.x) && Number.isFinite(p.y));
  if (points.length === 0) {
    return '<svg xmlns="http://www.w3.org/2000/svg" width="' + w + '" height="' + h + '">' +
      '<text x="8" y="' + (h / 2) + '" fill="' + TEXT + '" font-size="12">no data</text></svg>';
  }
  const padL = 52, padR = 14, padT = 24, padB = 30;
  const plotW = w - padL - padR, plotH = h - padT - padB;
  let xMin = Infinity, xMax = -Infinity, yMin = Infinity, yMax = -Infinity;
  for (const p of points) {
    if (p.x < xMin) xMin = p.x; if (p.x > xMax) xMax = p.x;
    if (p.y < yMin) yMin = p.y; if (p.y > yMax) yMax = p.y;
  }
  if (xMax === xMin) xMax = xMin + 1;
  if (yMax === yMin) yMax = yMin + 1;
  const scaleX = (x) => padL + ((x - xMin) / (xMax - xMin)) * plotW;
  const scaleY = (y) => padT + plotH - ((y - yMin) / (yMax - yMin)) * plotH;
  const parts = [];
  parts.push('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ' + w + ' ' + h +
    '" width="' + w + '" height="' + h + '" font-family="monospace" font-size="10">');
  if (spec.title) {
    parts.push('<text x="' + padL + '" y="14" fill="' + TEXT + '">' + esc(spec.title) + '</text>');
  }
  for (const t of niceTicks(yMin, yMax, 5)) {
    const y = scaleY(t);
    parts.push('<text x="' + (padL - 4) + '" y="' + (y + 3).toFixed(1) + '" fill="' + TEXT +
      '" text-anchor="end">' + esc(fmt(t, 3)) + '</text>');
  }
  for (const t of niceTicks(xMin, xMax, 6)) {
    const x = scaleX(t);
    parts.push('<text x="' + x.toFixed(1) + '" y="' + (padT + plotH + 12) + '" fill="' + TEXT +
      '" text-anchor="middle">' + esc(fmt(t, 3)) + '</text>');
  }
  parts.push('<rect x="' + padL + '" y="' + padT + '" width="' + plotW + '" height="' + plotH +
    '" fill="none" stroke="' + AXIS + '" stroke-opacity="0.5"/>');
  for (const p of points) {
    const r = p.radius ?? 3;
    parts.push('<circle cx="' + scaleX(p.x).toFixed(1) + '" cy="' + scaleY(p.y).toFixed(1) +
      '" r="' + r + '" fill="' + (p.color ?? '#58a6ff') + '" fill-opacity="' + (p.opacity ?? 0.85) + '"/>');
  }
  if (spec.xLabel) {
    parts.push('<text x="' + (padL + plotW) + '" y="' + (h - 4) + '" fill="' + TEXT +
      '" text-anchor="end">' + esc(spec.xLabel) + '</text>');
  }
  if (spec.yLabel) {
    parts.push('<text x="' + (padL + 4) + '" y="' + (padT - 6) + '" fill="' + TEXT + '">' +
      esc(spec.yLabel) + '</text>');
  }
  parts.push('</svg>');
  return parts.join('');
}

/** 单序列小折线（互相关等），带零线。 */
export function singleLineChart(values, opts = {}) {
  const pts = [];
  values.forEach((v, i) => { if (Number.isFinite(v)) pts.push([i, v]); });
  return multiLineChart({
    width: opts.width ?? 520, height: opts.height ?? 160,
    title: opts.title ?? '', xLabel: opts.xLabel ?? '', yLabel: opts.yLabel ?? '',
    series: [{ name: opts.name ?? '', points: pts, color: opts.color ?? '#58a6ff' }],
    legend: false, maxPoints: 800,
  });
}
