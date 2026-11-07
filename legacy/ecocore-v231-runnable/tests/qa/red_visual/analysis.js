'use strict';
// 快照解析 + 颜色/像素断言工具。
// 快照格式（web/app.js HEADER/BALL_REC/NUCL_REC/CYANO_*）：
//   header 80B: magic(1) version(1) status(1) | frame i32@4 | worldW f32@8 worldH f32@12
//               ballCount i32@16 nucleusCount i32@20 typeCount[3] i32@24/28/32
//               reason[28] @36 | cyanoNodeCount i32@64 cyanoLinkCount i32@68
//               cyanoAvgEnergy f32@72 sunLevel f32@76
const HEADER = 80, BALL_REC = 6, NUCL_REC = 32, CY_NODE_REC = 24, CY_LINK_REC = 8;

function parseSnapshot(buf) {
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  const snap = {
    magic: dv.getUint8(0), version: dv.getUint8(1), status: dv.getUint8(2),
    frame: dv.getInt32(4, true),
    worldW: dv.getFloat32(8, true), worldH: dv.getFloat32(12, true),
    ballCount: dv.getInt32(16, true), nucleusCount: dv.getInt32(20, true),
    typeCount: [dv.getInt32(24, true), dv.getInt32(28, true), dv.getInt32(32, true)],
    cyanoNodeCount: dv.getInt32(64, true), cyanoLinkCount: dv.getInt32(68, true),
    cyanoAvgEnergy: dv.getFloat32(72, true), sunLevel: dv.getFloat32(76, true),
    balls: [], nuclei: [], cyanoNodes: [], cyanoLinks: [],
  };
  const reasonBytes = new Uint8Array(buf.buffer, buf.byteOffset + 36, 28);
  let rl = 28;
  while (rl > 0 && reasonBytes[rl - 1] === 0) rl--;
  snap.reason = Buffer.from(reasonBytes.subarray(0, rl)).toString('utf8');
  let off = HEADER;
  for (let i = 0; i < snap.ballCount; i++, off += BALL_REC) {
    const x = dv.getUint16(off, true) * (snap.worldW / 65535);
    const y = dv.getUint16(off + 2, true) * (snap.worldH / 65535);
    const meta = dv.getUint16(off + 4, true);
    snap.balls.push({ x, y, type: meta & 3, owner: meta >> 2 });
  }
  snap.nuclOff = off;
  for (let i = 0; i < snap.nucleusCount; i++, off += NUCL_REC) {
    snap.nuclei.push({
      x: dv.getFloat32(off, true), y: dv.getFloat32(off + 4, true),
      energy: dv.getFloat32(off + 8, true), threshold: dv.getFloat32(off + 20, true),
      hue: dv.getFloat32(off + 28, true),
    });
  }
  snap.cyanoNodeOff = off;
  for (let i = 0; i < snap.cyanoNodeCount; i++, off += CY_NODE_REC) {
    snap.cyanoNodes.push({
      x: dv.getFloat32(off, true), y: dv.getFloat32(off + 4, true),
      energy: dv.getFloat32(off + 8, true),
      colony: dv.getUint16(off + 18, true), flags: dv.getUint16(off + 22, true),
    });
  }
  snap.cyanoLinkOff = off;
  for (let i = 0; i < snap.cyanoLinkCount; i++, off += CY_LINK_REC) {
    snap.cyanoLinks.push([dv.getUint32(off, true), dv.getUint32(off + 4, true)]);
  }
  return snap;
}

// ---------- 颜色（与 web/app.js 完全同构） ----------
const FREE_RGB = [158, 158, 158];
const TYPE_LIGHT = [0.32, 0.58, 0.86];
const OWNER_LUT = 1024;

function hslToRgb(h, s, l) {
  h = ((h % 360) + 360) % 360;
  const c = (1 - Math.abs(2 * l - 1)) * s;
  const x = c * (1 - Math.abs((h / 60) % 2 - 1));
  const m = l - c / 2;
  let r = 0, g = 0, b = 0;
  if (h < 60) { r = c; g = x; } else if (h < 120) { r = x; g = c; }
  else if (h < 180) { g = c; b = x; } else if (h < 240) { g = x; b = c; }
  else if (h < 300) { r = x; b = c; } else { r = c; b = x; }
  return [(r + m) * 255 | 0, (g + m) * 255 | 0, (b + m) * 255 | 0];
}
const clampByte = (v) => { const n = Math.round(v); return n < 0 ? 0 : n > 255 ? 255 : n; };
function liftRgb(rgb, minMax) {
  const r = clampByte(rgb[0]), g = clampByte(rgb[1]), b = clampByte(rgb[2]);
  const m = Math.max(r, g, b);
  if (m >= minMax || m <= 0) return [r, g, b];
  const k = minMax / m;
  return [clampByte(r * k), clampByte(g * k), clampByte(b * k)];
}
function ownerColor(ownerBits, type) {
  const idx = (ownerBits - 1) % OWNER_LUT;
  return hslToRgb((idx * 137.50776405) % 360, 0.78, TYPE_LIGHT[type]);
}
function cyanoNodeColor(colony) {
  return liftRgb(hslToRgb((colony * 47) % 360, 0.68, 0.55), 150);
}
// 连线颜色 = rgba(link,alpha) 叠在画布 CSS 背景上（快照/栅格截图会与背景合成）
function cyanoLinkComposite(pack) {
  const alpha = pack && pack.cyanoLinkAlpha != null ? pack.cyanoLinkAlpha : 0.7;
  const link = (pack && pack.cyanoLinkRgb) || [60, 140, 110];
  const bg = (pack && pack.background) || [5, 7, 11];
  return [0, 1, 2].map((i) => Math.round(alpha * link[i] + (1 - alpha) * bg[i]));
}
// 材质包调色板（和 applyPaletteToRenderer 的可见结果同构；只包含本测试用到的键）
const PACKS = {
  none: null,
  classic_flat: {
    mode: 'type_color', background: [8, 12, 20],
    types: [[100, 180, 255], [120, 220, 130], [255, 190, 90]],
    free: liftRgb([190, 190, 200], 96),
    nucleus: liftRgb([255, 245, 220], 180),
    nucleusSize: 1.0,
    ballSizes: [5, 4, 3], freeSize: 3,
    cyanoColorMode: 'colony', cyanoNodeRGB: liftRgb([90, 220, 170], 130),
    cyanoLinkRgb: [60, 140, 110], cyanoLinkAlpha: 0.7, cyanoNodeSize: 2.0,
  },
  big_beads: {
    mode: 'type_color', background: [8, 12, 20],
    types: [[100, 180, 255], [120, 220, 130], [255, 190, 90]],
    free: liftRgb([190, 190, 200], 96),
    nucleus: liftRgb([255, 245, 220], 180),
    nucleusSize: 0.8,
    ballSizes: [5, 4, 3], freeSize: 3,
    cyanoColorMode: 'colony', cyanoNodeRGB: liftRgb([90, 220, 170], 130),
    cyanoLinkRgb: [60, 140, 110], cyanoLinkAlpha: 0.85, cyanoNodeSize: 2.0,
  },
};
function expectedBallColor(pack, ball) {
  if (pack && pack.mode === 'type_color') return pack.types[ball.type] || pack.types[0];
  if (ball.owner === 0) return FREE_RGB;
  return ownerColor(ball.owner, ball.type);
}

// ---------- 几何：世界 -> 画布位图坐标（与 fitView 同构） ----------
function makeGeo(snap, canvasW, canvasH) {
  const scale = Math.min(canvasW / snap.worldW, canvasH / snap.worldH);
  const ox = snap.worldW / 2 - canvasW / (2 * scale);
  const oy = snap.worldH / 2 - canvasH / (2 * scale);
  return {
    scale, ox, oy, canvasW, canvasH,
    pos(wx, wy) { return { x: (wx - ox) * scale, y: (wy - oy) * scale }; },
  };
}

// ---------- 像素工具 ----------
function makePixels(decoded) {
  const { width, height, rgba } = decoded;
  return {
    width, height, rgba,
    at(x, y) {
      x |= 0; y |= 0;
      if (x < 0 || y < 0 || x >= width || y >= height) return null;
      const i = (y * width + x) * 4;
      return [rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]];
    },
    dist(c, t) { return Math.abs(c[0] - t[0]) + Math.abs(c[1] - t[1]) + Math.abs(c[2] - t[2]); },
    countColor(target, tol) {
      let n = 0;
      for (let i = 0; i < width * height; i++) {
        if (Math.abs(rgba[i * 4] - target[0]) <= tol &&
            Math.abs(rgba[i * 4 + 1] - target[1]) <= tol &&
            Math.abs(rgba[i * 4 + 2] - target[2]) <= tol) n++;
      }
      return n;
    },
    countColorInRegion(target, tol, x0, y0, x1, y1) {
      let n = 0;
      const xa = Math.max(0, x0 | 0), ya = Math.max(0, y0 | 0);
      const xb = Math.min(width - 1, x1 | 0), yb = Math.min(height - 1, y1 | 0);
      for (let y = ya; y <= yb; y++) {
        for (let x = xa; x <= xb; x++) {
          const i = (y * width + x) * 4;
          if (Math.abs(rgba[i] - target[0]) <= tol &&
              Math.abs(rgba[i + 1] - target[1]) <= tol &&
              Math.abs(rgba[i + 2] - target[2]) <= tol) n++;
        }
      }
      return n;
    },
    countSaturated(x0, y0, x1, y1, minSat = 0.5, minV = 0.35) {
      let n = 0;
      for (let y = Math.max(0, y0 | 0); y <= Math.min(height - 1, y1 | 0); y++) {
        for (let x = Math.max(0, x0 | 0); x <= Math.min(width - 1, x1 | 0); x++) {
          const i = (y * width + x) * 4;
          const r = rgba[i], g = rgba[i + 1], b = rgba[i + 2];
          const mx = Math.max(r, g, b), mn = Math.min(r, g, b);
          if (mx <= 0) continue;
          if ((mx - mn) / mx >= minSat && mx / 255 >= minV) n++;
        }
      }
      return n;
    },
    countBrightNeutral(minC, minMax, maxSat) {
      // 核白芯：三通道都亮、接近中性
      let n = 0;
      for (let i = 0; i < width * height; i++) {
        const r = rgba[i * 4], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];
        const mn = Math.min(r, g, b), mx = Math.max(r, g, b);
        if (mn >= minC && mx >= minMax && (mx - mn) / mx <= maxSat) n++;
      }
      return n;
    },
    nonBgRatio(bgs, tol = 12) {
      let n = 0;
      for (let i = 0; i < width * height; i++) {
        const r = rgba[i * 4], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];
        let isBg = false;
        for (const bg of bgs) {
          if (Math.abs(r - bg[0]) <= tol && Math.abs(g - bg[1]) <= tol && Math.abs(b - bg[2]) <= tol) { isBg = true; break; }
        }
        if (!isBg) n++;
      }
      return n / (width * height);
    },
    pureBlackCount() {
      let n = 0;
      for (let i = 0; i < width * height; i++) {
        if (rgba[i * 4] === 0 && rgba[i * 4 + 1] === 0 && rgba[i * 4 + 2] === 0) n++;
      }
      return n;
    },
    luma(c) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; },
  };
}

function rectContains(r, x, y) {
  return r && x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}
function rectsIntersect(a, b) {
  if (!a || !b || a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0) return false;
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
function intersectArea(a, b) {
  const x = Math.max(a.x, b.x), y = Math.max(a.y, b.y);
  const x2 = Math.min(a.x + a.w, b.x + b.w), y2 = Math.min(a.y + a.h, b.y + b.h);
  if (x2 <= x || y2 <= y) return 0;
  return (x2 - x) * (y2 - y);
}

module.exports = {
  parseSnapshot, HEADER, BALL_REC, NUCL_REC, CY_NODE_REC, CY_LINK_REC,
  hslToRgb, liftRgb, ownerColor, cyanoNodeColor, cyanoLinkComposite, PACKS,
  expectedBallColor, makeGeo, makePixels, rectContains, rectsIntersect, intersectArea,
};
