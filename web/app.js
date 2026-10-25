// 核-小球生态演化模拟器 · 启动器前端
// 后端端点：GET /status、GET /state（二进制快照）、GET /schema、GET /mods
//           POST /run、/save-config、/set-mods（body 为 key=value 行）、/control
'use strict';

// ---------- 常量 ----------
// 快照 v2 布局（契约 §2，逐字节冻结）：
//   头部 80B → 球 6B×N → 核 32B×M → 蓝细菌节点 24B×P → 蓝细菌连接 8B×Q
// 记录顺序固定，不得自行调换；偏移一律由 HEADER 起算。
const HEADER = 80, BALL_REC = 6, NUCL_REC = 32;
const CYANO_NODE_REC = 24, CYANO_LINK_REC = 8;
const SNAP_VERSION = 2;   // 只接受 v2 快照
const FADE = 14;          // 每渲染帧 alpha 衰减量（255 → ~18 帧 ≈ 0.3s 拖尾）
const FREE_RGB = [158, 158, 158];
// 三类有主球：色相仍来自所属核（谱系），用「明度 + 形状」双重区分。
// 旧版只有 0.32/0.58/0.86 明度差；3~5px 小方块在深色背景上仍难分辨，
// 因此 v2.2.1 起改为 方/菱/十字 三种像素形状（见 ballMask），并把暗色抬亮。
const TYPE_LIGHT = [0.38, 0.62, 0.90];
const TYPE_NAMES = ['护盾', '资源', '侦察'];
const TYPE_MARKS = ['■', '◆', '✚'];       // 图例符号：与 ballMask 的形状一一对应
const OWNER_LUT = 1024;
// 核记录内字段偏移（v2）：28 = hue f32（0..360，遗传自父核 + 小幅扰动）
const NUCL_ID_OFF = 24, NUCL_HUE_OFF = 28;
// 蓝细菌节点记录内字段偏移（v2）
const CY_X = 0, CY_Y = 4, CY_ENERGY = 8, CY_COLONY = 18, CY_FLAGS = 22;
// 蓝细菌连线记录：两个 u32 为**节点数组下标**（非稳定 id），a < b
const CY_LINK_DECIMATE_AT = 4000;   // 超过此连接数 → 每 4 条画 1 条（HUD 标注「连线已抽稀」）
let LINK_THINNING_FLAG = false;     // 本帧是否处于抽稀降级状态（drawCyano 写、HUD 读）

const el = (id) => document.getElementById(id);
const canvas = el('view');
const ctx = canvas.getContext('2d');
// 画布位图尺寸改为跟随 #stage 实际尺寸（CSS 仍是 100% 铺满）：位图宽高比 == 容器宽高比，
// 核/圆才不会被非等比拉伸成椭圆。指针换算继续用 W/rect.width，位图与 CSS 的比例差会被自动吸收。
// 位图总量必须设上限：fadeTrail() 每帧 O(W*H)，超大窗口下不缩位图会把帧率拖死；
// 超限时等比缩小位图、由 CSS 轻微上采样（观感损失可忽略）。常量位于 resizeCanvas 旁。
let W = canvas.width, H = canvas.height;          // 位图尺寸（resizeCanvas() 会改写）
let trail = ctx.createImageData(W, H);            // 轨迹缓冲：fadeTrail → putImageData
let buf = new Uint32Array(trail.data.buffer);     // 同一块内存按 u32 写像素字

// ---------- 颜色 LUT ----------
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
function packRgb(rgb) { return (0xFF << 24) | (rgb[2] << 16) | (rgb[1] << 8) | rgb[0]; }
const ownerColors = new Uint32Array(OWNER_LUT * 3);
for (let i = 0; i < OWNER_LUT; i++) {
  const hue = (i * 137.50776405) % 360;
  for (let t = 0; t < 3; t++) {
    ownerColors[i * 3 + t] = packRgb(hslToRgb(hue, 0.78, TYPE_LIGHT[t]));
  }
}
const freeColor = packRgb(FREE_RGB);

// 材质包调色板（applyPalette 时重建）
let freeColorWord = freeColor;
const typeModeColors = new Uint32Array(3);
// ---- 尺寸参数（材质需求：材质效果默认生效，材质包只作可覆盖层）----
let typeSize = [5, 4, 3];      // Shield, Worker, Scout 的像素边长
let freeSize = 3;              // 自由球的像素边长（自由球按类型仍可用 typeSize，此值为兜底）
let nucleusSize = 1.0;         // 核半径倍率：1.0 = 默认 (3 + 5*frac) / 内芯 2.2
let cyanoNodeSize = 2.0;       // 蓝细菌节点基准半径（像素）
// 节点半径硬夹在 [1.6, 6]：太小会在暗色背景上「看不见」，太大糊成一团（可见性验收项）
const CYANO_R_MIN = 1.6, CYANO_R_MAX = 6;
const CYANO_STROKE_CSS = 'rgba(0,0,0,0.62)';   // 节点 1px 深色描边（先描边再填充）
// ---- 蓝细菌配色（默认绿青，材质包可覆盖）----
let cyanoNodeCss = 'rgb(90,220,170)';
let cyanoLinkRgb = [60, 140, 110];       // 连线基色；材质包 cyano_link 可覆盖
let cyanoLinkAlpha = 0.7;                // 连线默认不透明度 0.7；材质包 cyano_link_alpha(0.35~1) 可覆盖
let cyanoLinkCss = 'rgba(60,140,110,0.7)';
let cyanoColorMode = 'colony';   // colony = 按 colonyId 分色（默认）；flat = 用 cyano_node/cyano_link 整色
let cyanoNodeRgb = [90, 220, 170];       // 材质包 cyano_node（flat 模式生效），提亮后缓存
const clampSize = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
// 把 [s0,s1,s2] 写回模块级 typeSize（就地修改，便于下次无覆盖时保持）
function setTypeSize(idx, v) {
  if (Number.isFinite(v)) typeSize[idx] = clampSize(Math.round(v), 1, 12);
}
function applySizeKeys(p) {
  if (!p) return;
  setTypeSize(0, p.size_ball_shield);
  setTypeSize(1, p.size_ball_worker);
  setTypeSize(2, p.size_ball_scout);
  if (Number.isFinite(p.size_free_ball)) freeSize = clampSize(Math.round(p.size_free_ball), 1, 12);
  if (Number.isFinite(p.size_nucleus)) nucleusSize = clampSize(p.size_nucleus, 0.3, 3);
  if (Number.isFinite(p.cyano_node_size)) cyanoNodeSize = clampSize(p.cyano_node_size, 0.5, 8);
}
// ---- 材质包取色：键名兼容层 ----
// 仓库里并存两套键名约定，必须同时兼容：
//   1) colors.txt 经 parseColors() 解析 → snake_case（ball_shield / free_ball / cyano_node …）；
//   2) 早期由后端 JSON 下发的调色板 → camelCase（ballShield / freeBall / cyanoNode …）。
// 旧代码只读 camelCase，而材质包只产出 snake_case → typeModeColors 恒为 0，
// 0 是「全透明」，stampBalls() 写进像素缓冲后球就整屏隐身（根因见提交历史）。
// 返回合法的 [r,g,b]（0..255 整数）；键缺失或值非法一律返回 null，由调用方决定兜底。
const clampByte = (v) => {
  const n = Math.round(v);
  return n < 0 ? 0 : n > 255 ? 255 : n;   // 越界分量必须夹住：packRgb 用位移打包，超出会串色
};
function pickArr(p, camel, snake) {
  if (!p) return null;
  let v = p[camel];
  if (!Array.isArray(v) || v.length < 3) v = p[snake];
  if (!Array.isArray(v) || v.length < 3) return null;
  const r = v[0], g = v[1], b = v[2];
  if (!Number.isFinite(r) || !Number.isFinite(g) || !Number.isFinite(b)) return null;
  return [clampByte(r), clampByte(g), clampByte(b)];
}
// 低亮度提亮：材质包/聚落色可能比背景还暗（深色球/菌丝在 #05070b 上几乎看不见）。
// 以「最亮通道」为基准等比放大到 minMax —— 色相不变、只抬亮度；已足够亮则原样返回。
// 这是「全元素可见」的兜底：不改变任何颜色键的语义，只保证画面上真的看得见。
function liftRgb(rgb, minMax) {
  const r = clampByte(rgb[0]), g = clampByte(rgb[1]), b = clampByte(rgb[2]);
  const m = Math.max(r, g, b);
  if (m >= minMax || m <= 0) return [r, g, b];
  const k = minMax / m;
  return [clampByte(r * k), clampByte(g * k), clampByte(b * k)];
}
function rgbCss(rgb, alpha) {
  return alpha === undefined
    ? `rgb(${rgb[0]},${rgb[1]},${rgb[2]})`
    : `rgba(${rgb[0]},${rgb[1]},${rgb[2]},${alpha})`;
}
// 打包色字（0xAABBGGRR）→ CSS。图例用实际渲染色，避免手写色块与画面不一致。
function wordCss(w) {
  return `rgb(${w & 255},${(w >>> 8) & 255},${(w >>> 16) & 255})`;
}
function applyPaletteToRenderer(p) {
  palette = p;
  applySizeKeys(p);            // 尺寸键：不依赖着色模式，type_color / owner_hue 都生效
  // 蓝细菌配色每次先回默认值，避免上一个材质包的键泄漏到当前包（切换材质包后颜色必须可预期）
  cyanoNodeRgb = [90, 220, 170];
  cyanoNodeCss = rgbCss(cyanoNodeRgb);
  cyanoLinkRgb = [60, 140, 110];
  cyanoLinkAlpha = 0.7;
  cyanoLinkCss = rgbCss(cyanoLinkRgb, cyanoLinkAlpha);
  cyanoColorMode = 'colony';
  if (!p) {
    freeColorWord = freeColor;
    typeModeColors.fill(0);
    canvas.style.background = '#05070b';
    return;
  }
  // 取色统一走 pickArr：snake_case / camelCase 都能取到，缺键不再静默拿到 undefined。
  // 所有球色都过一遍 liftRgb：材质包写了过暗颜色也不会在画面上消失。
  const free = pickArr(p, 'freeBall', 'free_ball');
  freeColorWord = packRgb(liftRgb(free || FREE_RGB, 96));
  const typeKeys = [['ballShield', 'ball_shield'], ['ballWorker', 'ball_worker'], ['ballScout', 'ball_scout']];
  for (let i = 0; i < typeKeys.length; i++) {
    const rgb = pickArr(p, typeKeys[i][0], typeKeys[i][1]);
    typeModeColors[i] = rgb ? packRgb(liftRgb(rgb, 96)) : 0;
  }
  // 兜底阀：type_color 模式下缺色/解析失败（=0）的类型一律回落默认色（owner_hue LUT 第一组）。
  // 保证材质包即使只写了 mode、颜色键全缺，球也是可见的，而不是 alpha=0 隐身。
  if (p.mode === 'type_color') {
    for (let i = 0; i < typeModeColors.length; i++) {
      if (typeModeColors[i] === 0) typeModeColors[i] = ownerColors[i];
    }
  }
  const cn = pickArr(p, 'cyanoNode', 'cyano_node');
  if (cn) {
    cyanoNodeRgb = liftRgb(cn, 130);
    cyanoNodeCss = rgbCss(cyanoNodeRgb);
  }
  const cl = pickArr(p, 'cyanoLink', 'cyano_link');
  if (cl) cyanoLinkRgb = liftRgb(cl, 110);
  // 可选键：cyano_link_alpha（0.35~1），缺省 0.7 —— 材质包可以把菌丝调更实/更淡，但不能调到看不见
  if (Number.isFinite(p.cyano_link_alpha)) {
    cyanoLinkAlpha = clampSize(p.cyano_link_alpha, 0.35, 1);
  }
  cyanoLinkCss = rgbCss(cyanoLinkRgb, cyanoLinkAlpha);
  if (p.cyano_color_mode) cyanoColorMode = p.cyano_color_mode;
  canvas.style.background = p.background ? `rgb(${p.background.join(',')})` : '#05070b';
  refreshLegend();
}

// ---------- 图例（从真实渲染状态生成，不手写色块） ----------
// 旧图例的问题：色块是硬编码的（有主球固定 #7ec8f0、蓝细菌固定 #5adcaa），
// 实际渲染却按核 hue / colonyId 分色；玩家打开图例看到的颜色与画面不一致。
// 现在色块全部取「当前材质包 + 当前快照」的真实颜色，并标出类型形状。
let legendSig = '';
function legendHue() {
  if (sim && sim.nucleusCount > 0) {
    return sim.dv.getFloat32(sim.nuclOff + NUCL_HUE_OFF, true);
  }
  return 205;   // 菜单态没有快照：用中性蓝作示例，不伪装成真实核
}
function legendTypeCss(t, hue) {
  if (palette && palette.mode === 'type_color' && typeModeColors[t]) {
    return wordCss(typeModeColors[t]);
  }
  return rgbCss(hslToRgb(hue, 0.78, TYPE_LIGHT[t]));
}
// 蓝细菌图例：colony 模式用快照里真实的前 3 个菌落色，flat 模式用材质包整色。
function legendCyano() {
  if (cyanoColorMode === 'flat') {
    return { flat: true, dots: [cyanoNodeCss] };
  }
  const dots = [];
  if (sim && sim.cyanoNodeCount > 0) {
    const seen = new Set();
    for (let i = 0; i < sim.cyanoNodeCount && dots.length < 3; i++) {
      const off = sim.cyanoNodeOff + i * CYANO_NODE_REC;
      const c = sim.dv.getUint16(off + CY_COLONY, true);
      if (seen.has(c)) continue;
      seen.add(c);
      dots.push(rgbCss(liftRgb(hslToRgb((c * 47) % 360, 0.68, 0.55), 150)));
    }
  }
  if (!dots.length) dots.push(cyanoNodeCss);
  return { flat: false, dots };
}
function refreshLegend() {
  const grid = el('legend-grid');
  if (!grid) return;
  const hue = legendHue();
  const typeMode = !!(palette && palette.mode === 'type_color');
  const typeCss = [0, 1, 2].map((t) => legendTypeCss(t, hue));
  const freeCss = wordCss(freeColorWord || freeColor);
  const nucCss = rgbCss(liftRgb(hslToRgb(hue, 0.85, 0.55), 120));
  const cy = legendCyano();
  const sig = [
    hue.toFixed(1), typeMode ? 1 : 0,
    typeCss.join(','), freeCss, nucCss, cy.flat ? 'flat' : cy.dots.join(','),
    cyanoLinkCss, typeSize.join(','), freeSize, cyanoColorMode,
  ].join('|');
  if (sig === legendSig) return;
  legendSig = sig;

  const typeRows = [0, 1, 2].map((t) =>
    `<span class="lgtype"><i class="lgball" style="color:${typeCss[t]}">${TYPE_MARKS[t]}</i>` +
    `${TYPE_NAMES[t]} <span class="lnote">${typeSize[t]}px</span></span>`).join('');
  const colonySw = cy.flat
    ? `<span class="lsw" style="background:${cy.dots[0]}"></span>`
    : `<span class="lsw lsw-colony" style="background:linear-gradient(90deg,${
        cy.dots.map((c, i) => `${c} ${(i * 100 / cy.dots.length).toFixed(1)}% ${((i + 1) * 100 / cy.dots.length).toFixed(1)}%`).join(',')
      })"></span>`;
  grid.innerHTML =
    `<div class="lrow"><span class="lsw" style="background:${freeCss}"></span>自由球 <span class="lnote">${freeSize}px</span></div>` +
    `<div class="lrow"><span class="lsw lsw-ring" style="border-color:${nucCss}"></span>核 <span class="lnote">能量环+暗 halo</span></div>` +
    `<div class="lrow full lg-balltypes">${typeRows}</div>` +
    `<div class="lrow full lnote">${typeMode
      ? '有主球：当前材质包按类型固定配色；形状 = 类型'
      : '有主球：色相 = 所属核（谱系），形状 = 类型'}</div>` +
    `<div class="lrow">${colonySw}蓝细菌节点 <span class="lnote">${cy.flat ? '单色' : '按菌落分色'}</span></div>` +
    `<div class="lrow"><span class="lline" style="background:${cyanoLinkCss}"></span>菌丝连线</div>` +
    `<div class="lrow full lhint">环境：底部快调条 4 轴 ± · 事件卡 / 细调抽屉（细调可展开高级参数，调完自动应用；事件无冷却）</div>` +
    `<div class="lrow full lhint">滚轮缩放（以指针为锚）· 拖拽平移 · 悬停查看核参数 · 双击画布重置视图</div>` +
    `<div class="lrow full lhint">快捷键：<span class="kbd">空格</span> 暂停/继续 · ` +
    `<span class="kbd">N</span> 单步 · <span class="kbd">S</span> 保存 · <span class="kbd">H</span> 折叠状态栏</div>` +
    `<div class="lrow full lhint">快捷键：<span class="kbd">R</span> 重置视图 · ` +
    `<span class="kbd">1-4</span> 切页 · <span class="kbd">+/-</span> 缩放 · ` +
    `<span class="kbd">Esc</span> 返回菜单</div>`;
}

// ---------- 全局状态 ----------
let uiState = 'menu';            // menu | running | paused | finished
let sim = null;
let frame = 0, worldW = 2000, worldH = 2000, status = 0, reason = '';
let scale = 1, ox = 0, oy = 0, viewDirty = true;
let fitScale = 1;                // fitView() 时的 scale = 缩放显示的 100% 基准（见 updateZoomLabel）
let modsData = null;             // { mods: [...], warnings: [...] }
let enabledOrder = [];           // 启用 mod id 的有序列表（= 优先级）
let schemaData = null;           // /schema JSON 数组
let schemaReq = 0;               // 防止并发 loadSchema 旧响应覆盖新响应
let packsData = null;            // /packs JSON 数组
let enabledPacks = [];           // 启用材质包有序列表
let palette = null;              // 应用后的调色板；键名可能是 snake_case（colors.txt）或 camelCase（旧 JSON），读取一律走 pickArr()
let exiting = false;             // 已请求退出：停止轮询并显示退出提示
// ---- 环境调控（v2.2.0）：GET /env 是唯一事实来源（轴/档位/事件/费用全从后端读）----
let envData = null;              // 最近一次 GET /env JSON（null = 尚未取到 / 后端未实现）
let eventsOpen = false;          // 事件卡弹出层是否打开
let fineOpen = false;            // 右侧细调抽屉是否打开
// ---- 存档时间轴 / 世界线分支（v2.3.0）----
let timelineData = null;         // GET /timeline 的最近结果
let timelineOpen = false;        // 时间轴面板是否打开
let timelineBusy = false;        // 正在 seek/branch，防止重复提交
let timelineDragging = false;    // 正在拖动进度条（避免轮询覆盖滑块）
let autoTimeline = false;        // 刚读档：等状态变 paused 后自动打开时间轴
let branchMode = false;          // 参数页正在编辑“从当前帧分支”的参数
let branchFrame = 0;             // 分支起始帧
// 统计兜底：env 里若有峰值/干预/存活帧字段则用之，否则结束结算显示 --（后端预留字段）
let envStats = { peakAlive: null, interventions: null, aliveFrames: null };

// ---------- 渲染自检 ----------
// HUD「渲染自检」折叠行与自动化截图断言共用同一份数据：
//   counts = 快照里的实体数；drawn = 本帧真正画到画布上的数量；culled = 被视口剔除的数量。
// 「实体数 > 0 但绘制数 = 0」是可见性事故的典型信号，HUD 会显示黄色警告。
const simDebug = {
  frame: 0,
  counts: { balls: 0, nuclei: 0, cyanoNodes: 0, cyanoLinks: 0 },
  drawn: { balls: 0, nuclei: 0, cyanoNodes: 0, cyanoLinks: 0 },
  culled: 0,
  thinning: false,
};
try { window.__simDebug = simDebug; } catch (e) { /* 非浏览器环境（Node 打桩）忽略 */ }

// ---------- HTTP 小工具 ----------
async function postBody(path, body) {
  try {
    await fetch(path, { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body });
  } catch (e) { /* 服务器可能已退出 */ }
}
async function postCmd(cmd) {
  try {
    await fetch('/control', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ cmd }),
    });
  } catch (e) { /* ignore */ }
}

// ---------- 标签页 ----------
function switchTab(page) {
  document.querySelectorAll('#tabs button').forEach((x) =>
    x.classList.toggle('active', x.dataset.page === page));
  document.querySelectorAll('.page').forEach((p) =>
    p.classList.toggle('active', p.id === 'page-' + page));
  if (page === 'params' && !schemaData) loadSchema();
  if (page === 'mods') {
    if (!modsData) loadMods();
    if (!packsData) loadPacks();
  }
  if (page === 'saves') loadSaves();
  // 运行页在后台时 #stage 尺寸读不到（display:none），切回来必须重建一次位图尺寸
  if (page === 'run') {
    resizeCanvas();
    if (eventsOpen || fineOpen) layoutEnvPanels();
  }
}
document.querySelectorAll('#tabs button').forEach((b) => {
  b.addEventListener('click', () => switchTab(b.dataset.page));
});

// ---------- 状态机 ----------
function setUiState(s) {
  const prev = uiState;
  uiState = s;
  const badge = el('badge');
  badge.className = s;
  if (s === 'menu') badge.textContent = '未运行';   // 比「菜单」直白：没在跑
  else if (s === 'running') badge.textContent = '运行中';
  else if (s === 'paused') badge.textContent = '已暂停';
  else if (s === 'finished') badge.textContent = '已结束';
  // 控制按钮可用性
  const running = s === 'running' || s === 'paused';
  const simActive = running || s === 'finished';
  el('btn-start').disabled = simActive;
  el('btn-save-start').disabled = simActive;
  el('btn-pause').disabled = !running;
  el('btn-resume').disabled = !running;
  el('btn-step').disabled = !running;
  el('btn-stop').disabled = !running;
  el('btn-save').disabled = !simActive;
  el('btn-timeline').disabled = s !== 'paused';
  el('speed').disabled = !running;
  el('overlay').classList.toggle('show', s === 'finished');
  // 暂停角标：只在画布上叠一个很淡的状态提示，不挡内容（CSS pointer-events:none）
  el('paused-badge').classList.toggle('show', s === 'paused');
  // 空状态只在「未运行」时出现：启动后淡出；结束态用 overlay，两者不同时显示
  el('stage-empty').classList.toggle('show', s === 'menu');
  if (s === 'finished') {
    el('overlay-title').textContent = '模拟已结束';
    updateEndOverlay();
  }
  if (prev !== 'running' && prev !== 'paused' && running) {
    buf.fill(0);           // 新一次运行：清空轨迹
    viewDirty = true;
  }
  if (prev === 'finished' && s === 'menu') {
    buf.fill(0);
  }
  // ---- 环境调控 UI（v2.2.0）：状态切换时收起面板/立即刷新一次 /env ----
  if (prev !== s) {
    if (s === 'menu' || s === 'finished') {
      closeEnvPanels();   // 菜单/结束态不允许余留面板
      closeTimelinePanel();
    }
    if (s === 'menu') {
      envData = null;                        // 新一局从干净状态开始
      timelineData = null;
      autoTimeline = false;
      cancelBranchMode();
    }
    renderEnvUi();     // 先按新状态重置可用性/提示（菜单态不显示上一局的影响力残余）
    fetchEnv();        // 再拉一次 /env（菜单态后端用 pendingCfg_ 构造只读预览）
    refreshLegend();   // 状态切换时立即同步图例（不必等下一次渲染节拍）
  }
}

// 结束结算：峰值核数 / 干预次数 / 存活帧。
// 数据来源优先级：GET /env 的统计字段（后端预留）→ 快照帧号兜底；都没有则显示 --。
function readEnvStats(env) {
  const out = { peakAlive: null, interventions: null, aliveFrames: null };
  const roots = [];
  if (env && typeof env === 'object') {
    roots.push(env, env.stats, env.sim, env.envStats);
  }
  const pickNum = (o, keys) => {
    if (!o || typeof o !== 'object') return null;
    for (const k of keys) {
      const v = o[k];
      if (typeof v === 'number' && Number.isFinite(v)) return v;
    }
    return null;
  };
  for (const r of roots) {
    if (!r) continue;
    if (out.peakAlive == null) out.peakAlive = pickNum(r, ['peakAlive', 'peak_alive', 'peakNuclei', 'peak']);
    if (out.interventions == null) out.interventions = pickNum(r, ['interventions', 'interventionCount', 'interventionsCount']);
    if (out.aliveFrames == null) out.aliveFrames = pickNum(r, ['aliveFrames', 'alive_frames', 'survivedFrames', 'survived_frames']);
  }
  return out;
}
function updateEndOverlay() {
  envStats = readEnvStats(envData);
  const peak = envStats.peakAlive == null ? '--' : Math.round(envStats.peakAlive);
  const iv = envStats.interventions == null ? '--' : Math.round(envStats.interventions);
  // 存活帧：优先用后端统计，其次用最后快照帧号（结束态该帧号即存活帧数）
  const alive = envStats.aliveFrames == null ? frame : Math.round(envStats.aliveFrames);
  const t = el('overlay-text');
  if (t) t.textContent = '结束原因：' + reason + ' · 最终帧 ' + frame;
  const st = el('overlay-stats');
  if (st) st.textContent = '峰值核数 ' + peak + ' · 干预次数 ' + iv + ' · 存活帧 ' + alive;
}

async function pollStatus() {
  if (exiting) return;
  try {
    const res = await fetch('/status', { cache: 'no-store' });
    if (!res.ok) return;
    const text = (await res.text()).trim();
    if (text === 'menu') setUiState('menu');
    else if (text === 'running') setUiState('running');
    else if (text === 'paused') {
      setUiState('paused');
      if (autoTimeline) {
        autoTimeline = false;
        openTimelinePanel();
      }
    }
    else if (text.startsWith('finished:')) {
      reason = text.slice(9);
      setUiState('finished');
    }
  } catch (e) {
    if (!exiting) {
      // 服务器已停止：显示断连提示（提供重试，不提供退出）。
      el('overlay-title').textContent = '连接已断开';
      el('overlay-text').textContent = '模拟进程已停止或退出。若想再次运行请重新启动程序。';
      el('btn-menu').style.display = 'none';
      el('btn-exit').style.display = 'none';
      el('btn-reload').style.display = '';
      el('overlay').classList.add('show');
    }
  }
}

// ---------- 快照轮询与渲染 ----------
async function pollState() {
  if (uiState === 'menu') return;
  try {
    const res = await fetch('/state', { cache: 'no-store' });
    if (!res.ok) return;
    const ab = await res.arrayBuffer();
    const dv = new DataView(ab);
    if (dv.byteLength < HEADER || dv.getUint8(0) !== 0x57 || dv.getUint8(1) !== SNAP_VERSION) return;
    status = dv.getUint8(2);
    frame = dv.getInt32(4, true);
    worldW = dv.getFloat32(8, true);
    worldH = dv.getFloat32(12, true);
    if (viewDirty) fitView();
    const ballCount = dv.getInt32(16, true);
    const nucleusCount = dv.getInt32(20, true);
    const cyanoNodeCount = dv.getInt32(64, true);
    const cyanoLinkCount = dv.getInt32(68, true);
    const cyanoAvgEnergy = dv.getFloat32(72, true);
    const sunLevel = dv.getFloat32(76, true);
    // 记录顺序固定：header → balls → nuclei → cyanoNodes → cyanoLinks
    const cyanoNodeOff = HEADER + ballCount * BALL_REC + nucleusCount * NUCL_REC;
    const cyanoLinkOff = cyanoNodeOff + cyanoNodeCount * CYANO_NODE_REC;
    // 长度自检（防御性）：半包 / 旧快照直接丢弃本帧，避免越界读出 NaN
    if (cyanoLinkOff + cyanoLinkCount * CYANO_LINK_REC > dv.byteLength) return;
    sim = {
      dv,
      ballCount,
      nucleusCount,
      typeCount: [dv.getInt32(24, true), dv.getInt32(28, true), dv.getInt32(32, true)],
      ballOff: HEADER,
      nuclOff: HEADER + ballCount * BALL_REC,
      cyanoNodeCount,
      cyanoLinkCount,
      cyanoAvgEnergy,
      sunLevel,
      cyanoNodeOff,
      cyanoLinkOff,
    };
    const reasonBytes = new Uint8Array(ab, 36, 28);
    let reasonLen = 28;
    while (reasonLen > 0 && reasonBytes[reasonLen - 1] === 0) reasonLen--;
    if (status === 1) {
      reason = new TextDecoder('utf-8').decode(reasonBytes.subarray(0, reasonLen));
      setUiState('finished');
    }
    if (timelineOpen && !timelineBusy && !timelineDragging) {
      setTxt('tl-current', '当前帧 ' + frame);
      renderTimelinePanel();
    }
  } catch (e) { /* 轮询间隙服务器忙：忽略 */ }
}

function fitView() {
  if (!worldW) return;
  scale = Math.min(W / worldW, H / worldH);
  ox = worldW / 2 - W / (2 * scale);
  oy = worldH / 2 - H / (2 * scale);
  viewDirty = false;
  fitScale = scale;        // 100% 基准：之后缩放显示的是「相对适应视图」的倍率
  updateZoomLabel();
}

// ---------- 画布尺寸生命周期 ----------
// #stage 尺寸变了（窗口缩放 / 从别的页切回运行页）就重建位图，保持宽高比 == 容器宽高比。
// 同一尺寸连续调用直接返回（幂等），避免重复重建缓冲造成闪屏与无谓分配。
const CANVAS_MAX_EDGE = 1600;            // 位图单边上限
const CANVAS_MAX_PIXELS = 1600 * 1000;   // 位图总像素上限（≈160 万，fadeTrail 每帧可承受）
function resizeCanvas() {
  const stage = el('stage');
  let cw = stage.clientWidth, ch = stage.clientHeight;
  if (!cw || !ch) {
    // 页面在后台（display:none）时读不到尺寸：退回 getBoundingClientRect，仍为 0 则保持原位图。
    const r = stage.getBoundingClientRect();
    cw = r.width; ch = r.height;
  }
  cw = Math.round(cw); ch = Math.round(ch);
  if (cw < 2 || ch < 2) return;          // 布局尚未完成：不要缩成 2×2
  // 等比系数：同时满足单边上限与总像素上限，取更严的那个（1 = 不需要缩）
  const k = Math.min(1,
    CANVAS_MAX_EDGE / Math.max(cw, ch),
    Math.sqrt(CANVAS_MAX_PIXELS / (cw * ch)));
  const bw = Math.max(2, Math.floor(cw * k));
  const bh = Math.max(2, Math.floor(ch * k));
  if (bw === canvas.width && bh === canvas.height && W === bw && H === bh) return;  // 幂等
  W = bw; H = bh;
  canvas.width = bw; canvas.height = bh;   // 改 width/height 会清空画布并重置 ctx 状态，故随后重建数据
  trail = ctx.createImageData(W, H);
  buf = new Uint32Array(trail.data.buffer);
  buf.fill(0);                             // 新缓冲必须清零，否则残留上一帧的像素
  viewDirty = true;                        // 尺寸变了 → 下次 pollState 重算视图
  fitView();                               // 立即先适配一次，避免这一帧画面比例不对
}
let resizeTimer = 0;
function scheduleResize() {
  clearTimeout(resizeTimer);
  resizeTimer = setTimeout(resizeCanvas, 120);   // 防抖：拖窗口时别每像素重建一次缓冲
}
window.addEventListener('resize', scheduleResize);

// ---------- 缩放 ----------
const ZOOM_MIN = 0.005, ZOOM_MAX = 20, ZOOM_STEP = 1.25;
const clampScale = (s) => Math.min(ZOOM_MAX, Math.max(ZOOM_MIN, s));
// 按钮/键盘缩放以画布中心为锚（滚轮以指针为锚，见 wheel 监听）：先记中心世界坐标，缩放后回推平移
function zoomBy(k) {
  const cx = W / 2, cy = H / 2;
  const wx = cx / scale + ox, wy = cy / scale + oy;
  scale = clampScale(scale * k);
  ox = wx - cx / scale;
  oy = wy - cy / scale;
  updateZoomLabel();
}
// 倍率显示：以 fitView 的 scale 为 100%，避免「世界尺寸不同 → 百分比含义漂移」
function updateZoomLabel() {
  const t = el('st-zoom');
  if (!t) return;
  t.textContent = (fitScale > 0 ? Math.round(scale / fitScale * 100) : 100) + '%';
}
function resetView() {
  fitView();
  updateZoomLabel();
}

function fadeTrail() {
  const n = W * H;
  for (let i = 0; i < n; i++) {
    const w = buf[i];
    const a = (w >>> 24) - FADE;
    buf[i] = a <= 0 ? 0 : (w & 0xFFFFFF) | (a << 24);
  }
}

// 小球像素形状：0=护盾 实心方块，1=资源 菱形，2=侦察 十字。
// 尺寸完全由材质包的 size_ball_* 决定；形状只是在该尺寸内做遮罩，不改变球的物理。
// 为什么不用颜色区分三类：色相承载「所属核/谱系」信息，不能被类型覆盖；
// 小尺寸下明度差又不够可靠，形状是唯一不牺牲谱系色的稳定区分手段。
function ballMask(shape, x, y, size) {
  if (shape === 1) {          // 菱形
    if (size <= 2) return true;   // 2px 菱形会一个像素都覆盖不到：退化为实心，保证可见
    const c = (size - 1) / 2;
    return Math.abs(x - c) + Math.abs(y - c) <= c + 0.01;
  }
  if (shape === 2) {          // 十字
    const c = (size - 1) / 2;
    return Math.abs(x - c) <= 0.51 || Math.abs(y - c) <= 0.51;
  }
  return true;                // 实心方块
}

function stampBalls() {
  const dv = sim.dv, n = sim.ballCount;
  const sxToW = worldW / 65535, syToW = worldH / 65535;
  const typeMode = palette && palette.mode === 'type_color';
  simDebug.counts.balls = n;
  let drawn = 0;
  for (let i = 0; i < n; i++) {
    const off = sim.ballOff + i * BALL_REC;
    const wx = dv.getUint16(off, true) * sxToW;
    const wy = dv.getUint16(off + 2, true) * syToW;
    const meta = dv.getUint16(off + 4, true);
    const px = ((wx - ox) * scale) | 0;
    const py = ((wy - oy) * scale) | 0;
    if (px < -2 || px >= W + 2 || py < -2 || py >= H + 2) continue;
    drawn++;
    const type = meta & 3;
    const ownerBits = meta >> 2;
    let col;
    if (typeMode) {
      col = typeModeColors[type];
    } else if (ownerBits === 0) {
      col = freeColorWord;
    } else {
      col = ownerColors[((ownerBits - 1) % OWNER_LUT) * 3 + type];
    }
    // 兜底：col === 0 即 alpha=0（全透明）。绝不把这种色字写进 buf，否则就是「隐身球」。
    // 正常路径不会命中（packRgb 恒置 alpha=0xFF）；命中说明调色板残缺，退回自由球默认色。
    if (col === 0) col = freeColorWord || freeColor;
    // 材质需求「不同小球大小颜色不同」：尺寸按类型区分（护盾 > 资源 > 侦察）。
    // 锚点仍是左上角 (px,py) —— 改成中心锚点会让弹道观感全变，故不做。
    const size = type < typeSize.length ? typeSize[type] : freeSize;
    const shape = type < 3 ? type : 0;
    if (size < 2) {   // 1px 的极端档位退化成一个像素，保证小球可见
      if (px >= 0 && px < W && py >= 0 && py < H) buf[py * W + px] = col;
      continue;
    }
    for (let yy = 0; yy < size; yy++) {
      const y = py + yy;
      if (y < 0 || y >= H) continue;
      for (let xx = 0; xx < size; xx++) {
        if (!ballMask(shape, xx, yy, size)) continue;
        const x = px + xx;
        if (x >= 0 && x < W) buf[y * W + x] = col;
      }
    }
  }
  simDebug.drawn.balls = drawn;
}

function drawNuclei() {
  const dv = sim.dv, n = sim.nucleusCount;
  simDebug.counts.nuclei = n;
  // 内芯色：材质包 nucleus 键（snake/camel 均可），过 liftRgb 保证不出现「暗核看不见」
  const nucPack = palette ? pickArr(palette, 'nucleus', 'nucleus') : null;
  const nucRgb = nucPack || [255, 255, 255];
  let drawn = 0;
  for (let i = 0; i < n; i++) {
    const off = sim.nuclOff + i * NUCL_REC;
    const wx = dv.getFloat32(off, true), wy = dv.getFloat32(off + 4, true);
    const energy = dv.getFloat32(off + 8, true);
    const threshold = dv.getFloat32(off + 20, true);
    // 颜色来自快照的 hue 字段（遗传自父核 + 小幅扰动）：
    //   「每个核颜色固定」+「繁殖产生的核有少量颜色变异」两个子需求由后端遗传保证。
    const hue = dv.getFloat32(off + NUCL_HUE_OFF, true);
    const sx = (wx - ox) * scale, sy = (wy - oy) * scale;
    if (sx < -30 || sx > W + 30 || sy < -30 || sy > H + 30) continue;
    drawn++;
    const frac = Math.min(1, Math.max(0.15, threshold > 0 ? energy / threshold : 0.15));
    const r = (3 + 5 * frac) * nucleusSize;
    const rgb = liftRgb(hslToRgb(hue, 0.85, 0.55), 120);   // 低亮度核也提亮到可见
    // 能量环外的半透明深色 halo：把核从「球群噪点」里托出来（先画 halo 再画环）
    ctx.beginPath();
    ctx.arc(sx, sy, r + 3, 0, Math.PI * 2);
    ctx.fillStyle = 'rgba(0,0,0,0.38)';
    ctx.fill();
    ctx.beginPath();
    ctx.arc(sx, sy, r, 0, Math.PI * 2);
    ctx.lineWidth = 2;
    ctx.strokeStyle = rgbCss(rgb);
    ctx.stroke();
    ctx.beginPath();
    ctx.arc(sx, sy, 2.2 * nucleusSize, 0, Math.PI * 2);
    ctx.fillStyle = rgbCss(liftRgb(nucRgb, 180));
    ctx.fill();
  }
  simDebug.drawn.nuclei = drawn;
}

// ---------- 蓝细菌（快照 v2 附加段） ----------
// 可见性修复要点（v2.2.0）：
//   连线：默认 rgba(基色, 0.7)、lineWidth 1.5（材质包可用 cyano_link_alpha 覆盖透明度）。
//   节点：半径夹在 [1.6, 6]，先画 1px 深色描边再填充（暗背景上也有轮廓）。
//   flat 模式：进入填充循环前显式设置 fillStyle —— 旧代码只在 colony 分支里设色，
//   首帧 flat 会沿用 ctx 默认的黑色 fillStyle，整片菌落变成「黑块/看不见」。
// 关闭态 cyanoNodeCount === 0 → 首行即 return，零开销。
let cyanoR = new Float32Array(0);
let cyanoXs = new Float32Array(0);
let cyanoYs = new Float32Array(0);
let cyanoCol = new Uint16Array(0);
function drawCyano() {
  const p = sim.cyanoNodeCount | 0;
  const linkCount = sim.cyanoLinkCount | 0;
  simDebug.counts.cyanoNodes = p;
  simDebug.counts.cyanoLinks = linkCount;
  simDebug.drawn.cyanoNodes = 0;
  simDebug.drawn.cyanoLinks = 0;
  if (p <= 0) return;                       // 蓝细菌关闭：立即返回，不做任何工作
  const dv = sim.dv;
  const nodeOff = sim.cyanoNodeOff;

  // ---- 1. 连线（先画，压在节点下面）----
  // 连接记录的两个 u32 是**节点数组下标**（非稳定 id），可 O(1) 直取坐标。
  if (linkCount > 0) {
    // 节点下标 → 屏幕坐标缓存（同一帧内两条连线常共用端点，避免重复解析 DataView）
    const xs = new Float32Array(p), ys = new Float32Array(p);
    for (let i = 0; i < p; i++) {
      const o = nodeOff + i * CYANO_NODE_REC;
      xs[i] = (dv.getFloat32(o + CY_X, true) - ox) * scale;
      ys[i] = (dv.getFloat32(o + CY_Y, true) - oy) * scale;
    }
    const step = linkCount > CY_LINK_DECIMATE_AT ? 4 : 1;   // 抽稀降级，避免每帧几千次 canvas 路径
    LINK_THINNING_FLAG = step > 1;
    ctx.lineWidth = 1.5;
    ctx.strokeStyle = cyanoLinkCss;
    ctx.beginPath();                          // 合并为一条路径，单次 stroke（抽稀与否都更快）
    let drawnLinks = 0;
    for (let i = 0; i < linkCount; i += step) {
      const o = sim.cyanoLinkOff + i * CYANO_LINK_REC;
      const a = dv.getUint32(o, true), b = dv.getUint32(o + 4, true);
      if (a >= p || b >= p) continue;         // 越界下标：跳过，不画（防御性）
      const ax = xs[a], ay = ys[a], bx = xs[b], by = ys[b];
      // 两端都在视口同一侧才剔除；穿过视口的长连线必须保留
      if ((ax < -2 && bx < -2) || (ax > W + 2 && bx > W + 2) ||
          (ay < -2 && by < -2) || (ay > H + 2 && by > H + 2)) continue;
      ctx.moveTo(ax, ay);
      ctx.lineTo(bx, by);
      drawnLinks++;
    }
    ctx.stroke();
    simDebug.drawn.cyanoLinks = drawnLinks;
  }

  // ---- 2. 节点（深色描边 + 实心小圆）----
  const avg = sim.cyanoAvgEnergy || 0;
  const denom = avg * 2 + 1e-6;
  const byColony = cyanoColorMode !== 'flat';   // 默认按聚落分色；材质包可切成整体单色
  if (cyanoR.length < p) {   // 缓冲按需扩容（同帧复用，避免每帧分配）
    cyanoR = new Float32Array(p);
    cyanoXs = new Float32Array(p);
    cyanoYs = new Float32Array(p);
    cyanoCol = new Uint16Array(p);
  }
  // pass 1：计算坐标/半径 + 把所有节点圆并入同一条路径 → 一次 stroke 完成深色描边
  let n = 0;
  ctx.beginPath();
  for (let i = 0; i < p; i++) {
    const o = nodeOff + i * CYANO_NODE_REC;
    if (!(dv.getUint16(o + CY_FLAGS, true) & 1)) continue;   // flags bit0 = alive
    const energy = dv.getFloat32(o + CY_ENERGY, true);
    const rRaw = cyanoNodeSize + 3 * Math.min(1, energy / denom);
    // 半径硬夹：< 1.6px 在暗色背景上会被抗锯齿吃掉，> 6px 会糊成一片
    const r = rRaw < CYANO_R_MIN ? CYANO_R_MIN : rRaw > CYANO_R_MAX ? CYANO_R_MAX : rRaw;
    const sx = (dv.getFloat32(o + CY_X, true) - ox) * scale;
    const sy = (dv.getFloat32(o + CY_Y, true) - oy) * scale;
    if (sx < -8 || sx > W + 8 || sy < -8 || sy > H + 8) continue;
    cyanoXs[n] = sx; cyanoYs[n] = sy; cyanoR[n] = r;
    cyanoCol[n] = dv.getUint16(o + CY_COLONY, true);
    n++;
    ctx.moveTo(sx + r, sy);
    ctx.arc(sx, sy, r, 0, Math.PI * 2);
  }
  if (n > 0) {
    ctx.lineWidth = 1;
    ctx.strokeStyle = CYANO_STROKE_CSS;
    ctx.stroke();
    // pass 2：填充。flat 模式必须**先显式设色**（旧代码的「首帧黑块」根因）
    if (!byColony) ctx.fillStyle = cyanoNodeCss;
    for (let j = 0; j < n; j++) {
      if (byColony) {
        // 聚落分色：47 与 360 互质，配色散；同聚落同色 → 观感上就是一个「菌落」。
        // 再过 liftRgb：深色聚落（低亮度）也保证在暗背景上可见。
        const rgb = liftRgb(hslToRgb((cyanoCol[j] * 47) % 360, 0.68, 0.55), 150);
        ctx.fillStyle = rgbCss(rgb);
      }
      ctx.beginPath();
      ctx.arc(cyanoXs[j], cyanoYs[j], cyanoR[j], 0, Math.PI * 2);
      ctx.fill();
    }
  }
  simDebug.drawn.cyanoNodes = n;
  ctx.fillStyle = cyanoNodeCss;   // 还原，避免污染后续绘制的状态
}

let fpsFrames = 0, fpsT0 = performance.now(), fps = -1;   // fps < 0 = 尚未测出（HUD 显示 --）
function renderLoop(now) {
  if (fpsFrames >= 30) {
    const dt = now - fpsT0;
    // 无头浏览器虚拟时间 / 极端卡顿时 dt 可能为 0：旧代码会算出 Infinity，而 `Infinity | 0 === 0`，
    // HUD 就显示误导性的 FPS 0。这里把「测不出」保留为 -1，绝不把未知伪装成 0。
    fps = dt > 0 ? fpsFrames * 1000 / dt : -1;
    fpsFrames = 0; fpsT0 = now;
  }
  fpsFrames++;
  // 图例跟随「真实渲染状态」：快照核 hue / 材质包配色 / 球尺寸都会变。
  // 每 16 帧刷新一次；refreshLegend 内部用签名去重，只在真的变化时重建 DOM。
  if ((fpsFrames & 15) === 0) refreshLegend();
  if (sim && (uiState === 'running' || uiState === 'paused' || uiState === 'finished')) {
    fadeTrail();
    simDebug.counts.balls = sim.ballCount;
    simDebug.counts.nuclei = sim.nucleusCount;
    simDebug.counts.cyanoNodes = sim.cyanoNodeCount;
    simDebug.counts.cyanoLinks = sim.cyanoLinkCount;
    simDebug.drawn.balls = 0;
    simDebug.drawn.nuclei = 0;
    simDebug.drawn.cyanoNodes = 0;
    simDebug.drawn.cyanoLinks = 0;
    stampBalls();
    ctx.putImageData(trail, 0, 0);
    drawCyano();
    drawNuclei();
  } else {
    simDebug.counts.balls = simDebug.counts.nuclei = 0;
    simDebug.counts.cyanoNodes = simDebug.counts.cyanoLinks = 0;
    simDebug.drawn.balls = simDebug.drawn.nuclei = 0;
    simDebug.drawn.cyanoNodes = simDebug.drawn.cyanoLinks = 0;
    ctx.putImageData(trail, 0, 0);
  }
  // 渲染自检快照：thinning 必须在清标志前捕获（drawCyano 每帧写）
  const dbgCulled = () => {
    const c = simDebug.counts, d = simDebug.drawn;
    const total = c.balls + c.nuclei + c.cyanoNodes + c.cyanoLinks;
    const drew = d.balls + d.nuclei + d.cyanoNodes + d.cyanoLinks;
    return total > drew ? total - drew : 0;
  };
  simDebug.frame = frame;
  simDebug.culled = dbgCulled();
  simDebug.thinning = LINK_THINNING_FLAG;
  LINK_THINNING_FLAG = false;
  updateDebugHud();
  el('st-frame').textContent = frame;
  el('st-nuclei').textContent = sim ? sim.nucleusCount : 0;
  el('st-balls').textContent = sim ? sim.ballCount : 0;
  el('st-shield').textContent = sim ? sim.typeCount[0] : 0;
  el('st-worker').textContent = sim ? sim.typeCount[1] : 0;
  el('st-scout').textContent = sim ? sim.typeCount[2] : 0;
  el('st-fps').textContent = fps < 0 ? '--' : (fps | 0);   // 未知显示 --，不伪装成 0（D2）
  if (sim && sim.nucleusCount > 0) {
    const dv = sim.dv;
    let sum = 0;
    for (let i = 0; i < sim.nucleusCount; i++) {
      sum += dv.getFloat32(sim.nuclOff + i * NUCL_REC + 8, true);
    }
    el('st-energy').textContent = (sum / sim.nucleusCount).toFixed(1);
  } else {
    el('st-energy').textContent = '0';
  }
  // 蓝细菌四项：关闭态也必须显示 0 / 0 / 0 / 0.00，不得省略
  el('st-cyano').textContent = sim ? sim.cyanoNodeCount : 0;
  el('st-cyanolinks').textContent = sim ? sim.cyanoLinkCount : 0;
  el('st-cyanoenergy').textContent = sim ? sim.cyanoAvgEnergy.toFixed(1) : '0';
  el('st-sun').textContent = sim ? sim.sunLevel.toFixed(2) : '0.00';
  el('st-cyano').title = simDebug.thinning ? '连线已抽稀（每 4 条画 1 条）' : '';
  requestAnimationFrame(renderLoop);
}

// HUD「渲染自检」折叠行：把 simDebug 写到 DOM；实体>0 但绘制=0 时黄色警告。
function updateDebugHud() {
  const c = simDebug.counts, d = simDebug.drawn;
  el('dbg-frame').textContent = simDebug.frame;
  el('dbg-counts').textContent =
    c.balls + '/' + c.nuclei + '/' + c.cyanoNodes + '/' + c.cyanoLinks;
  el('dbg-drawn').textContent =
    d.balls + '/' + d.nuclei + '/' + d.cyanoNodes + '/' + d.cyanoLinks;
  el('dbg-culled').textContent = simDebug.culled;
  el('dbg-thinning').textContent = simDebug.thinning ? '是（每 4 条画 1 条）' : '否';
  const warn = el('dbg-warn');
  const total = c.balls + c.nuclei + c.cyanoNodes + c.cyanoLinks;
  const drew = d.balls + d.nuclei + d.cyanoNodes + d.cyanoLinks;
  if (total > 0 && drew === 0) {
    warn.textContent = '⚠ 实体数 ' + total + ' > 0 但绘制数为 0：检查可见性（视口/调色板/渲染路径）';
    warn.classList.add('show');
  } else {
    warn.textContent = '';
    warn.classList.remove('show');
  }
}

// ---------- 视图交互 ----------
el('btn-reset-view').addEventListener('click', resetView);
el('btn-zoom-out').addEventListener('click', () => zoomBy(1 / ZOOM_STEP));
el('btn-zoom-in').addEventListener('click', () => zoomBy(ZOOM_STEP));
canvas.addEventListener('dblclick', resetView);   // 双击画布 = 重置视图（快捷操作，图例里有提示）
canvas.addEventListener('wheel', (e) => {
  e.preventDefault();
  const rect = canvas.getBoundingClientRect();
  // 位图可能被等比缩小（见 CANVAS_MAX_*），W/rect.width 把 CSS 像素换算成位图像素，
  // 因此「以指针为锚」的公式在动态位图下依旧成立，不需要额外 letterbox 修正。
  const mx = (e.clientX - rect.left) * (W / rect.width);
  const my = (e.clientY - rect.top) * (H / rect.height);
  const wx = mx / scale + ox, wy = my / scale + oy;
  const k = Math.pow(1.15, -e.deltaY / 100);
  scale = clampScale(scale * k);
  ox = wx - mx / scale;
  oy = wy - my / scale;
  updateZoomLabel();
}, { passive: false });
let dragging = false, lastX = 0, lastY = 0;
canvas.addEventListener('mousedown', (e) => {
  dragging = true; lastX = e.clientX; lastY = e.clientY;
  canvas.classList.add('dragging');
});
window.addEventListener('mousemove', (e) => {
  if (dragging) {
    const rect = canvas.getBoundingClientRect();
    ox -= (e.clientX - lastX) * (W / rect.width) / scale;
    oy -= (e.clientY - lastY) * (H / rect.height) / scale;
    lastX = e.clientX; lastY = e.clientY;
  }
  showTooltip(e);
});
window.addEventListener('mouseup', () => {
  dragging = false;
  canvas.classList.remove('dragging');
});
function showTooltip(e) {
  if (!sim) return;
  const rect = canvas.getBoundingClientRect();
  const mx = (e.clientX - rect.left) * (W / rect.width);
  const my = (e.clientY - rect.top) * (H / rect.height);
  const dv = sim.dv;
  let best = -1, bestD = 144;
  for (let i = 0; i < sim.nucleusCount; i++) {
    const off = sim.nuclOff + i * NUCL_REC;
    const sx = (dv.getFloat32(off, true) - ox) * scale;
    const sy = (dv.getFloat32(off + 4, true) - oy) * scale;
    const d = (sx - mx) * (sx - mx) + (sy - my) * (sy - my);
    if (d < bestD) { bestD = d; best = i; }
  }
  const tooltip = el('tooltip');
  if (best < 0) { tooltip.style.display = 'none'; return; }
  const off = sim.nuclOff + best * NUCL_REC;
  tooltip.innerHTML =
    `核 #${dv.getInt32(off + NUCL_ID_OFF, true)}<br>` +
    `能量 ${dv.getFloat32(off + 8, true).toFixed(1)} / 阈值 ${dv.getFloat32(off + 20, true).toFixed(1)}<br>` +
    `攻击强度 ${dv.getFloat32(off + 12, true).toFixed(1)} · 最大速度 ${dv.getFloat32(off + 16, true).toFixed(1)}<br>` +
    // 核记录 32B 里没有捕食偏好 / 加速度比字段（契约 §2），这里只显示快照里真实存在的 hue
    `色相 ${dv.getFloat32(off + NUCL_HUE_OFF, true).toFixed(0)}°`;
  tooltip.style.display = 'block';
  tooltip.style.left = (e.clientX - rect.left + 14) + 'px';
  tooltip.style.top = (e.clientY - rect.top + 14) + 'px';
}

// ---------- 键盘快捷键 ----------
// 规则：输入控件聚焦时一律不抢键（否则参数页里打不出数字/空格）；
// 空格在 <button> 聚焦时交给原生 click（否则一次按键会同时触发原生激活 + 我们的暂停/继续）。
function isTypingTarget(t) {
  if (!t) return false;
  const tag = (t.tagName || '').toUpperCase();
  return tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA' || t.isContentEditable === true;
}
function onGlobalKey(e) {
  if (e.ctrlKey || e.metaKey || e.altKey) return;   // 组合键留给浏览器/系统
  const t = e.target;
  if (isTypingTarget(t)) return;
  const tag = t && t.tagName ? t.tagName.toUpperCase() : '';
  const k = e.key;
  // 快捷键与按钮 title 里的提示必须一一对应（自检脚本会核对）
  const active = uiState === 'running' || uiState === 'paused';
  const simActive = active || uiState === 'finished';
  if (k === ' ' || k === 'Spacebar') {
    if (tag === 'BUTTON' || !active || e.repeat) return;
    e.preventDefault();
    postCmd(uiState === 'running' ? 'pause' : 'resume');
    return;
  }
  if (e.repeat) return;
  switch (k) {
    case 'n': case 'N':
      if (!active) return;
      postCmd('step');
      break;
    case 's': case 'S':
      if (!simActive) return;
      saveCurrent();
      break;
    case 'r': case 'R':
      resetView();
      break;
    case '1': switchTab('run'); break;
    case '2': switchTab('params'); break;
    case '3': switchTab('mods'); break;
    case '4': switchTab('saves'); break;
    case 'Escape':
      // 先收面板/分支再回菜单：面板打开时 Esc 只关面板，不打断这一局
      if (eventsOpen || fineOpen) { closeEnvPanels(); break; }
      if (timelineOpen) { closeTimelinePanel(); break; }
      if (branchMode) { cancelBranchMode(); break; }
      if (uiState !== 'menu') postCmd('menu');
      break;
    case 'h': case 'H':
      toggleHud();
      break;
    case '+': case '=':
      zoomBy(ZOOM_STEP);
      break;
    case '-': case '_':
      zoomBy(1 / ZOOM_STEP);
      break;
    default:
      return;
  }
  e.preventDefault();
}
window.addEventListener('keydown', onGlobalKey);

// ---------- 运行页控制 ----------
// 兜底值与 index.html 的快速设置默认值保持一致（v2.2.0 演示配置：balls=300 / nuclei=12）。
// 没有 localStorage 时输入框就是 HTML 默认值；即使用户清空输入框，兜底也不会写回旧的 1000/20。
function quickParams() {
  let body = `seed=${el('q-seed').value || 42}\n` +
             `balls=${el('q-balls').value || 300}\n` +
             `nuclei=${el('q-nuclei').value || 12}\n` +
             `frames=${el('q-frames').value || 100000}\n`;
  // v2.2.0「启用蓝细菌（生产者）」：勾选时把生产者规模写进启动 body。
  // 放在 schema 全量参数之后 → 同名键以这里为准（后端 parseKeyValues 后者覆盖前者）。
  if (el('q-cyano') && el('q-cyano').checked) {
    body += 'cyano_nodes=150\ncyano_colonies=4\n';
  }
  return body;
}
// ---------- 快速设置：localStorage 回填 ----------
// 只记 seed/balls/nuclei/frames 四项快速设置；参数页的全量 schema 仍以后端 env_config.txt
// 为唯一来源，不写进 localStorage，避免两套配置源互相打架。
const QUICK_KEY = 'nukesim.quick';
const QUICK_FIELDS = ['seed', 'balls', 'nuclei', 'frames'];
// 生态开关（v2.2.0）单独存一个键：不打乱上面 nukesim.quick 的既有格式
const ENV_PREFS_KEY = 'nukesim.envprefs';
function loadQuick() {
  try {
    const saved = JSON.parse(localStorage.getItem(QUICK_KEY) || 'null');
    if (!saved) return;
    for (const k of QUICK_FIELDS) {
      const inp = el('q-' + k);
      if (inp && saved[k] !== undefined && saved[k] !== '') inp.value = saved[k];
    }
  } catch (e) { /* 无痕模式/禁用 localStorage：忽略，保留 HTML 默认值 */ }
}
function saveQuick() {
  try {
    const o = {};
    for (const k of QUICK_FIELDS) o[k] = el('q-' + k).value;
    localStorage.setItem(QUICK_KEY, JSON.stringify(o));
  } catch (e) { /* 忽略写入失败 */ }
}
for (const k of QUICK_FIELDS) el('q-' + k).addEventListener('input', saveQuick);
function saveEnvPrefs() {
  try {
    localStorage.setItem(ENV_PREFS_KEY, JSON.stringify({
      cyano: !!(el('q-cyano') && el('q-cyano').checked),
      recomods: !!(el('q-recomods') && el('q-recomods').checked),
      autosave: !!(el('q-autosave') && el('q-autosave').checked),
      saveName: (el('q-save-name') && el('q-save-name').value) || '',
    }));
  } catch (e) { /* 忽略写入失败 */ }
}
function loadEnvPrefs() {
  try {
    const o = JSON.parse(localStorage.getItem(ENV_PREFS_KEY) || 'null');
    if (!o || typeof o !== 'object') return;   // 没有记忆 → 保持 HTML 的默认勾选
    if (typeof o.cyano === 'boolean' && el('q-cyano')) el('q-cyano').checked = o.cyano;
    if (typeof o.recomods === 'boolean' && el('q-recomods')) el('q-recomods').checked = o.recomods;
    if (typeof o.autosave === 'boolean' && el('q-autosave')) el('q-autosave').checked = o.autosave;
    if (typeof o.saveName === 'string' && el('q-save-name')) el('q-save-name').value = o.saveName;
  } catch (e) { /* 无痕模式/禁用 localStorage：保持默认勾选 */ }
}
el('q-cyano').addEventListener('change', saveEnvPrefs);
el('q-recomods').addEventListener('change', saveEnvPrefs);
el('q-autosave').addEventListener('change', saveEnvPrefs);
el('q-save-name').addEventListener('input', saveEnvPrefs);

// ---------- 侧栏折叠 ----------
// 折叠后 stage 变宽，画面立刻变大；关键点是必须 resizeCanvas() 重建位图，
// 否则 canvas 位图仍是旧宽高比，会被 CSS 拉伸变形（D5 的核心）。
const SIDEBAR_KEY = 'nukesim.sidebar';
function applySidebar(collapsed) {
  document.body.classList.toggle('sidebar-collapsed', collapsed);
  const b = el('btn-sidebar');
  if (b) {
    b.classList.toggle('active', collapsed);
    b.title = collapsed ? '展开侧栏' : '折叠侧栏（画布变宽）';
  }
  resizeCanvas();   // 布局宽度变了 → 位图尺寸必须跟着重建
}
function toggleSidebar() {
  const collapsed = !document.body.classList.contains('sidebar-collapsed');
  applySidebar(collapsed);
  try { localStorage.setItem(SIDEBAR_KEY, collapsed ? '1' : '0'); } catch (e) { /* 忽略 */ }
}
function loadSidebar() {
  try {
    if (localStorage.getItem(SIDEBAR_KEY) === '1') applySidebar(true);
  } catch (e) { /* 无痕模式/禁用 localStorage：保持展开 */ }
}
el('btn-sidebar').addEventListener('click', toggleSidebar);

// ---------- 右侧竖排状态栏折叠（v2.3.0） ----------
const HUD_KEY = 'nukesim.hudcollapsed';
function applyHudCollapsed(collapsed) {
  document.body.classList.toggle('hud-collapsed', collapsed);
  const b = el('btn-hud-collapse');
  if (b) b.title = collapsed ? '展开右侧状态栏（H）' : '折叠右侧状态栏（H）';
  if (typeof layoutEnvPanels === 'function' && (eventsOpen || fineOpen)) layoutEnvPanels();
}
function toggleHud() {
  const collapsed = !document.body.classList.contains('hud-collapsed');
  applyHudCollapsed(collapsed);
  try { localStorage.setItem(HUD_KEY, collapsed ? '1' : '0'); } catch (e) { /* 忽略 */ }
}
function loadHud() {
  try {
    if (localStorage.getItem(HUD_KEY) === '1') applyHudCollapsed(true);
  } catch (e) { /* 无痕模式：保持展开 */ }
}
el('btn-hud-collapse').addEventListener('click', toggleHud);
el('hud-tab').addEventListener('click', toggleHud);

// 「推荐生态 mod」：失球宽限 + 核间共生 + 能量溢出再分配（三者互补，单独启用效果很弱）。
// 启动前若当前启用清单与「当前清单 ∪ 推荐项」不同，先 POST /set-mods 再 /run
// （与 Mod 页「保存 Mod 配置」同一后端接口/同一行式 body）。
const RECOMMENDED_MODS = ['ball_loss_relief', 'symbiosis_bond', 'energy_spill'];
async function ensureRecommendedMods() {
  const cb = el('q-recomods');
  if (!cb || !cb.checked) return;
  let info = null;
  try {
    const res = await fetch('/mods', { cache: 'no-store' });
    if (!res.ok) return;                 // 读不到当前清单就不动 mods.list，避免误清空用户配置
    info = await res.json();
  } catch (e) { return; }
  if (!info || !Array.isArray(info.mods)) return;
  modsData = info;
  const known = info.mods.filter((m) => m && m.enabled)
                  .sort((a, b) => a.order - b.order).map((m) => m.id);
  const desired = known.slice();
  for (const id of RECOMMENDED_MODS) {
    if (desired.includes(id)) continue;
    if (info.mods.some((m) => m && m.id === id)) desired.push(id);   // 只追加后端真实存在的 mod
  }
  if (desired.join(',') === known.join(',')) return;                 // 已是目标状态：不重复写
  await postBody('/set-mods', 'mods=' + desired.join(','));
  enabledOrder = desired.slice();
  if (modsData) renderMods();
  showEnvToast('已启用推荐生态 mod（失球宽限+共生+溢出），本局生效', true);
}

// 开局前决定是否保存存档：只有勾选时才把 save_on_start 写进 /run 请求。
// save_name 留空时后端按 seed 自动命名；后端会做文件名安全校验与重名避让。
function startSaveParams() {
  if (!el('q-autosave') || !el('q-autosave').checked) return '';
  let body = 'save_on_start=1\n';
  const name = (el('q-save-name') && el('q-save-name').value || '').replace(/[\r\n]/g, '').trim();
  if (name) body += 'save_name=' + name + '\n';
  return body;
}

el('btn-start').addEventListener('click', async () => {
  closeEnvPanels();
  closeTimelinePanel();
  autoTimeline = false;
  timelineData = null;
  await ensureRecommendedMods();          // 先对齐 mod 清单（失败不阻塞启动）
  const body = collectParams() + quickParams() + startSaveParams();
  await postBody('/run', body);
});
el('btn-pause').addEventListener('click', () => postCmd('pause'));
el('btn-resume').addEventListener('click', () => postCmd('resume'));
el('btn-step').addEventListener('click', () => postCmd('step'));
el('btn-stop').addEventListener('click', () => postCmd('stop'));
el('btn-menu').addEventListener('click', () => postCmd('menu'));
el('btn-reload').addEventListener('click', () => location.reload());
el('speed').addEventListener('change', (e) => postCmd('maxfps:' + e.target.value));

// 启动时把下拉框对齐到 env_config.txt 的 max_fps（后端 maxFps_ 已按它限速）。
// 单独取一次 /schema：只读 max_fps，不写 schemaData —— schemaData 一旦在参数页
// 渲染前就绪，collectParams() 的输入框下标会错位。设置 .value 不触发 change，
// 因此不会回发 maxfps: 指令，与后端保持同一值。
async function syncSpeedFromConfig() {
  try {
    const res = await fetch('/schema', { cache: 'no-store' });
    const d = await res.json();
    const p = d.find((x) => x.key === 'max_fps');
    if (!p) return;
    const v = String(p.cur);
    const sel = el('speed');
    if (![...sel.options].some((o) => o.value === v)) {
      const o = document.createElement('option');
      o.value = v;
      o.textContent = Number(v) > 0 ? v + ' FPS' : '不限速';
      sel.appendChild(o);
    }
    sel.value = v;
  } catch (e) { /* 读取失败则保留 HTML 默认项 */ }
}

// 退出程序：发出指令后立刻给出明确反馈并停止轮询。
async function requestExit() {
  if (exiting) return;
  exiting = true;
  el('overlay-title').textContent = '程序已退出';
  el('overlay-text').textContent = '模拟进程已关闭，现在可以关闭此标签页。';
  el('btn-menu').style.display = 'none';
  el('btn-exit').style.display = 'none';
  el('btn-reload').style.display = '';
  el('overlay').classList.add('show');
  await postCmd('exit');
}
el('btn-exit').addEventListener('click', requestExit);
el('btn-quit').addEventListener('click', () => {
  if (confirm('确定退出程序？未保存的模拟状态将丢失。')) requestExit();
});

// ---------- 参数页 ----------
const GROUP_HINTS = {
  '世界与运行': '地图尺寸、初始规模、运行时长与采样/可视化节奏——决定"这局玩多大、玩多久"。',
  '环境与资源': '能量来源与承载力——决定这个世界的富饶程度：食物越多，生态越繁荣。',
  '小球动力学': '球的轨道/排斥/跟随/失球惩罚——决定球群如何聚散、核能否甩掉球群。',
  '核动力学': '攻击/避让/觅食/代谢/繁殖代价——塑造核的行为，是策略分化的核心旋钮。',
  '遗传参数范围': '开局随机取值的范围 + 进化中变异允许的上下限——共同决定能演化出的策略空间。',
};

async function loadSchema() {
  const token = ++schemaReq;
  try {
    const res = await fetch('/schema', { cache: 'no-store' });
    const data = await res.json();
    if (token !== schemaReq) return;   // 已有更新的请求：丢弃旧响应
    schemaData = data;
  } catch (e) { return; }
  const groups = el('params-groups');
  groups.innerHTML = '';
  const byGroup = new Map();
  for (const d of schemaData) {
    if (!byGroup.has(d.group)) byGroup.set(d.group, []);
    byGroup.get(d.group).push(d);
  }
  for (const [group, items] of byGroup) {
    const g = document.createElement('div');
    g.className = 'pgroup';
    const head = document.createElement('div');
    head.className = 'phead';
    head.textContent = group;
    const hint = document.createElement('div');
    hint.className = 'grouphint';
    hint.textContent = GROUP_HINTS[group] || '';
    const body = document.createElement('div');
    body.className = 'pbody';
    for (const d of items) {
      const item = document.createElement('div');
      item.className = 'pitem';
      const pk = document.createElement('div');
      pk.className = 'pk';
      pk.textContent = d.key;
      const pd = document.createElement('div');
      pd.className = 'pd';
      pd.textContent = d.desc + '（范围 ' + d.min + ' ~ ' + d.max + '）';
      const prow = document.createElement('div');
      prow.className = 'prow';
      let inputs = [];
      if (d.type === 'bool') {
        const c = document.createElement('input');
        c.type = 'checkbox';
        c.checked = d.cur === '1' || d.cur === 'true' || d.cur === 'on';
        inputs = [c];
      } else if (d.type === 'pair') {
        const parts = d.cur.split(',').map((s) => s.trim());
        for (const part of parts) {
          const i = document.createElement('input');
          i.type = 'number';
          i.step = 'any';
          i.className = 'pair';
          i.value = part;
          i.min = d.min;
          i.max = d.max;
          inputs.push(i);
        }
      } else {
        const i = document.createElement('input');
        i.type = d.type === 'int' ? 'number' : 'number';
        i.step = d.type === 'int' ? '1' : 'any';
        i.value = d.cur;
        i.min = d.min;
        i.max = d.max;
        inputs = [i];
      }
      const dfltBtn = document.createElement('button');
      dfltBtn.className = 'dflt';
      dfltBtn.textContent = '默认';
      dfltBtn.addEventListener('click', () => {
        if (d.type === 'bool') {
          inputs[0].checked = d.dflt === '1';
        } else if (d.type === 'pair') {
          const parts = d.dflt.split(',').map((s) => s.trim());
          inputs.forEach((inp, idx) => { inp.value = parts[idx] || ''; });
        } else {
          inputs[0].value = d.dflt;
        }
      });
      for (const i of inputs) prow.appendChild(i);
      prow.appendChild(dfltBtn);
      item.appendChild(pk);
      item.appendChild(pd);
      item.appendChild(prow);
      body.appendChild(item);
    }
    g.appendChild(head);
    g.appendChild(hint);
    g.appendChild(body);
    head.addEventListener('click', () => {
      const hide = body.style.display !== 'none';
      body.style.display = hide ? 'none' : '';
      hint.style.display = hide ? 'none' : '';
    });
    groups.appendChild(g);
  }
}

function collectParams() {
  if (!schemaData) return '';
  const groups = el('params-groups');
  const inputs = groups.querySelectorAll('input');
  const lines = [];
  let idx = 0;
  for (const d of schemaData) {
    if (d.type === 'bool') {
      const c = inputs[idx++];
      lines.push(`${d.key}=${c.checked ? '1' : '0'}`);
    } else if (d.type === 'pair') {
      const a = inputs[idx++], b = inputs[idx++];
      lines.push(`${d.key}=${a.value}, ${b.value}`);
    } else {
      const i = inputs[idx++];
      lines.push(`${d.key}=${i.value}`);
    }
  }
  return lines.join('\n') + '\n';
}

el('btn-save-config').addEventListener('click', async () => {
  await postBody('/save-config', collectParams());
  const badge = el('badge');
  badge.textContent = '已保存';
  setTimeout(() => setUiState(uiState), 1200);
});
// 「先调参后保存并开始」：参数页一次完成写 env_config.txt + 用同一批参数开局。
el('btn-save-start').addEventListener('click', async () => {
  if (uiState !== 'menu') {
    showEnvToast('模拟已在运行；如需重开请先停止并返回菜单', false);
    return;
  }
  if (!schemaData) await loadSchema();
  const body = collectParams();
  if (!body) { showEnvToast('参数尚未加载完成，请稍后重试', false); return; }
  el('btn-save-start').disabled = true;
  try {
    await postBody('/save-config', body);
    await postBody('/run', body);
    closeTimelinePanel();
    timelineData = null;
    cancelBranchMode();
    switchTab('run');
    showEnvToast('参数已保存，并按当前参数开始模拟', true);
  } finally {
    el('btn-save-start').disabled = true;
  }
});
el('btn-restore-defaults').addEventListener('click', () => {
  if (!schemaData) return;
  const groups = el('params-groups');
  const inputs = groups.querySelectorAll('input');
  let idx = 0;
  for (const d of schemaData) {
    if (d.type === 'bool') {
      inputs[idx++].checked = d.dflt === '1';
    } else if (d.type === 'pair') {
      const parts = d.dflt.split(',').map((s) => s.trim());
      inputs[idx++].value = parts[0] || '';
      inputs[idx++].value = parts[1] || '';
    } else {
      inputs[idx++].value = d.dflt;
    }
  }
});

// ---------- Mod 页 ----------
async function loadMods() {
  try {
    const res = await fetch('/mods', { cache: 'no-store' });
    modsData = await res.json();
  } catch (e) { return; }
  enabledOrder = modsData.mods.filter((m) => m.enabled)
                    .sort((a, b) => a.order - b.order).map((m) => m.id);
  renderMods();
}

function renderMods() {
  // 警告区
  const wl = el('warnlist');
  wl.innerHTML = '';
  for (const w of modsData.warnings) {
    const d = document.createElement('div');
    d.className = 'warn ' + (w.type === 'error' ? 'error' : 'warn');
    d.textContent = (w.type === 'error' ? '⚠ 互斥：' : '⚠ 冲突提示：') + w.text;
    wl.appendChild(d);
  }
  // 列表：注册顺序展示全部；启用的显示优先级序号与上下移动按钮
  const list = el('mods-list');
  list.innerHTML = '';
  modsData.mods.forEach((m, idx) => {
    const item = document.createElement('div');
    item.className = 'moditem';
    const cb = document.createElement('input');
    cb.type = 'checkbox';
    cb.checked = enabledOrder.includes(m.id);
    cb.addEventListener('change', () => {
      if (cb.checked) {
        if (!enabledOrder.includes(m.id)) enabledOrder.push(m.id);
      } else {
        enabledOrder = enabledOrder.filter((x) => x !== m.id);
      }
      renderMods();
    });
    const orderSpan = document.createElement('span');
    orderSpan.className = 'order';
    const pos = enabledOrder.indexOf(m.id);
    orderSpan.textContent = pos >= 0 ? '#' + (pos + 1) : '—';
    const info = document.createElement('div');
    info.className = 'minfo';
    const name = document.createElement('div');
    name.className = 'mname';
    name.textContent = m.name + '（' + m.id + '）';
    const desc = document.createElement('div');
    desc.className = 'mdesc';
    desc.textContent = m.desc + (m.touches ? '　影响机制: ' + m.touches : '');
    info.appendChild(name);
    info.appendChild(desc);
    const btns = document.createElement('div');
    btns.className = 'morder-btns';
    const up = document.createElement('button');
    up.textContent = '↑';
    up.disabled = pos <= 0;
    up.addEventListener('click', () => {
      if (pos <= 0) return;
      [enabledOrder[pos - 1], enabledOrder[pos]] = [enabledOrder[pos], enabledOrder[pos - 1]];
      renderMods();
    });
    const down = document.createElement('button');
    down.textContent = '↓';
    down.disabled = pos < 0 || pos >= enabledOrder.length - 1;
    down.addEventListener('click', () => {
      if (pos < 0 || pos >= enabledOrder.length - 1) return;
      [enabledOrder[pos + 1], enabledOrder[pos]] = [enabledOrder[pos], enabledOrder[pos + 1]];
      renderMods();
    });
    btns.appendChild(up);
    btns.appendChild(down);
    item.appendChild(cb);
    item.appendChild(orderSpan);
    item.appendChild(info);
    item.appendChild(btns);
    list.appendChild(item);
  });
  // 重新计算冲突提示的本地近似：与后端一致的口径，保存后以后端为准。
  const enabledSet = enabledOrder;
  for (let i = 0; i < enabledSet.length; i++) {
    for (let j = i + 1; j < enabledSet.length; j++) {
      const a = modsData.mods.find((m) => m.id === enabledSet[i]);
      const b = modsData.mods.find((m) => m.id === enabledSet[j]);
      if (!a || !b) continue;
      const ta = (a.touches || '').split(',').map((s) => s.trim()).filter(Boolean);
      const tb = (b.touches || '').split(',').map((s) => s.trim()).filter(Boolean);
      if (ta.some((x) => tb.includes(x))) {
        const d = document.createElement('div');
        d.className = 'warn warn';
        d.textContent = `⚠ 冲突提示：「${a.name}」与「${b.name}」影响相同机制（${ta.join('/')}），优先级靠前者先执行`;
        wl.appendChild(d);
      }
    }
  }
}

el('btn-save-mods').addEventListener('click', async () => {
  await postBody('/set-mods', 'mods=' + enabledOrder.join(','));
  const badge = el('badge');
  badge.textContent = 'Mod 已保存';
  setTimeout(() => setUiState(uiState), 1200);
  try {
    const res = await fetch('/mods', { cache: 'no-store' });
    modsData = await res.json();
  } catch (e) { /* ignore */ }
  renderMods();
});

// ---------- 环境调控 UI（v2.2.0） ----------
// 设计红线：**前端不复制任何轴/档位/事件/费用常量**，全部从 GET /env 读取渲染。
//   - 快调：点击 chip 的 −/+ → POST `axis.<id>=<目标档位>`
//   - 事件：事件卡 → POST `event.<id>=1`
//   - 细调：每轴「应用」→ POST `fine.<key>=<值>`（可多行一次提交，后端逐条裁决）
// 状态机：菜单/结束态只读（显示「启动后可实时调控」），运行/暂停态可操作。
const ENV_POLL_MS = 1000;          // 运行/暂停态 /env 轮询周期
let envToastTimer = 0;
let quickAxesSig = '';
let eventsSig = '';
let fineSig = '';
let fineInputs = Object.create(null);
let fineDirty = Object.create(null);
let fineGroupUI = Object.create(null);   // axisId -> { axis, est, apply }（费用/改动预览）
let fineExpanded = false;                // 细调抽屉是否展开显示高级参数
let fineAutoTimers = Object.create(null); // axisId -> 自动应用防抖定时器

function envIsActive() { return uiState === 'running' || uiState === 'paused'; }
function setTxt(id, s) { const n = el(id); if (n) n.textContent = s; }
function setCls(id, cls, on) { const n = el(id); if (n) n.classList.toggle(cls, !!on); }

// ---- toast：成功/失败都给出明确文案；失败优先展示后端 rejected 原因 ----
function showEnvToast(text, ok) {
  const t = el('env-toast');
  if (!t) return;
  t.textContent = text || (ok ? '操作成功' : '操作失败');
  t.className = 'show ' + (ok ? 'ok' : 'err');
  if (envToastTimer) clearTimeout(envToastTimer);
  envToastTimer = setTimeout(() => { t.className = ''; envToastTimer = 0; }, 3200);
}

// POST /env 的响应契约（api-dev）：{ok, applied, rejected:[{key,reason}], influence}
// cooldowns 为旧版兼容字段，手动事件已无冷却，前端不再展示。
// 混合请求可能 ok=false 但 applied>0 —— 仍按失败提示，并逐条列出被拒原因。
function envRejectText(json, fallback) {
  if (json && typeof json === 'object') {
    const r = json.rejected;
    if (Array.isArray(r) && r.length) {
      const parts = [];
      for (const x of r) {
        if (x && typeof x === 'object') {
          const k = x.key !== undefined ? x.key : (x.id !== undefined ? x.id : '');
          const why = x.reason || x.err || x.error || '被拒绝';
          parts.push((k ? k + '：' : '') + why);
        } else parts.push(String(x));
      }
      if (json.applied > 0) parts.push('（已应用 ' + json.applied + ' 项）');
      return parts.join('；');
    }
    const m = json.err || json.error || json.message;
    if (typeof m === 'string' && m) return m;
  }
  return fallback || '影响力不足';
}

async function postEnv(body, okText) {
  let json = null, httpOk = true;
  try {
    const res = await fetch('/env', {
      method: 'POST', headers: { 'Content-Type': 'text/plain' }, body,
    });
    httpOk = res.ok !== false;
    try { json = await res.json(); } catch (e) { json = null; }
  } catch (e) {
    showEnvToast('环境指令发送失败（网络错误）', false);
    return false;
  }
  const rejected = json && Array.isArray(json.rejected) ? json.rejected : [];
  const ok = httpOk && rejected.length === 0 && (!json || json.ok === undefined || !!json.ok);
  if (!ok) {
    // 后端未实现 /env（旧版本 404）时不要误报成「影响力不足」
    const msg = (!httpOk && !json)
      ? '环境接口不可用（后端未实现 POST /env，可能不是 v2.2.0）'
      : '操作被拒绝：' + envRejectText(json);
    showEnvToast(msg, false);
    fetchEnv();                     // 拒绝也要刷新：可能部分生效/影响力已被扣
    return false;
  }
  showEnvToast(okText, true);
  fetchEnv();                       // 操作后立即刷新（影响力/档位）
  return true;
}

async function fetchEnv() {
  let d = null;
  try {
    const res = await fetch('/env', { cache: 'no-store' });
    if (!res.ok) return;
    d = await res.json();
  } catch (e) { return; }           // 后端未实现 /env 时静默降级，不阻塞主流程
  if (!d || typeof d !== 'object') return;
  envData = d;
  renderEnvUi();
}

// ---- 总渲染：影响力 / 轴 chip / 提示 / 面板可用性 / 已打开面板同步 ----
function renderEnvUi() {
  if (!el('qb-influence')) return;
  const env = envData;
  const inf = env && env.influence && typeof env.influence === 'object' ? env.influence : null;
  const cur = inf && Number.isFinite(inf.cur) ? inf.cur : null;
  const max = inf && Number.isFinite(inf.max) ? inf.max : null;
  const regen = inf && Number.isFinite(inf.regenPerFrame) ? inf.regenPerFrame : null;
  const active = envIsActive();
  const idle = uiState === 'menu';       // 菜单态没有进行中的影响力池：显示 --，但仍展示四轴预览

  setTxt('qb-influence', (idle || cur == null) ? '--/--' : (Math.floor(cur) + '/' + (max == null ? '--' : Math.round(max))));
  const fill = el('qb-influence-fill');
  if (fill) {
    const pct = (idle || cur == null || !max) ? 0 : Math.max(0, Math.min(100, cur / max * 100));
    fill.style.width = pct.toFixed(1) + '%';
  }

  // 四轴 chip：只在「轴集合 / 档位 / 可用性 / 快调费用」变化时重建，避免每秒轮询抢走按钮焦点。
  // costs 来自 GET /env 的可选字段（api-dev/sim-dev 提供）；缺失时 chip 提示只写「消耗影响力」。
  const costs = env && env.costs && typeof env.costs === 'object' ? env.costs : null;
  const quickCost = costs && Number.isFinite(costs.quick) ? costs.quick : null;
  const axes = env && Array.isArray(env.axes) ? env.axes : [];
  const axesSig = axes.map((a) => a.id + ':' + a.level + ':' + ((a.levels || []).length)).join('|') +
    '|' + (active ? 1 : 0) + '|' + (quickCost == null ? '-' : quickCost);
  if (axesSig !== quickAxesSig) {
    quickAxesSig = axesSig;
    const box = el('qb-axes');
    if (box) {
      box.innerHTML = '';
      for (const a of axes) box.appendChild(buildAxisChip(a, active, quickCost));
    }
  }

  // 底部提示：菜单态「启动后可实时调控」；运行/暂停态显示恢复速度与暂停不回蓝
  const hint = el('qb-hint');
  if (hint) {
    let text = '', show = true;
    if (uiState === 'menu') text = '启动后可实时调控';
    else if (uiState === 'finished') text = '本局已结束';
    else if (!env) text = '环境接口连接中…';
    else {
      const bits = [];
      if (regen != null) bits.push('影响力每帧 +' + regen);
      if (env.paused) bits.push('已暂停（不回蓝）');
      else bits.push('运行中');
      text = bits.join(' · ');
    }
    hint.textContent = text;
    hint.classList.toggle('show', !!show);
  }

  const be = el('btn-events'), bf = el('btn-fine');
  if (be) be.disabled = !active;
  if (bf) bf.disabled = !active;

  if (eventsOpen) renderEventPop();
  if (fineOpen) renderFineDrawer();
  if (eventsOpen || fineOpen) layoutEnvPanels();
  if (uiState === 'finished') updateEndOverlay();   // 结束结算统计随 /env 刷新
}

// ---- 四轴快调 chip ----
function axisById(id) {
  const axes = envData && Array.isArray(envData.axes) ? envData.axes : [];
  for (const a of axes) if (a && a.id === id) return a;
  return null;
}
function buildAxisChip(a, active, quickCost) {
  const wrap = document.createElement('div');
  wrap.className = 'qb-axis' + (active ? '' : ' off');
  const costTxt = quickCost == null ? '消耗影响力（以后端为准）' : '消耗 ' + quickCost + ' 影响力';
  if (a.hint) wrap.title = (a.name || a.id) + '：' + a.hint + '（每次快调' + costTxt + '）';
  const name = document.createElement('span');
  name.className = 'qba-name';
  name.textContent = a.name || a.id;
  const levels = Array.isArray(a.levels) ? a.levels : [];
  const lvIdx = a.level | 0;
  const dec = document.createElement('button');
  dec.className = 'qba-btn';
  dec.textContent = '−';
  dec.title = (a.name || a.id) + ' 降一档 · ' + costTxt;
  dec.disabled = !active || lvIdx <= 0;
  const level = document.createElement('span');
  level.className = 'qba-level';
  level.textContent = levels[lvIdx] && levels[lvIdx].label != null ? levels[lvIdx].label : String(lvIdx);
  level.title = '当前第 ' + (lvIdx + 1) + '/' + (levels.length || 5) + ' 档 · 每次快调' + costTxt;
  const inc = document.createElement('button');
  inc.className = 'qba-btn';
  inc.textContent = '+';
  inc.title = (a.name || a.id) + ' 升一档 · ' + costTxt;
  inc.disabled = !active || lvIdx >= levels.length - 1;
  dec.addEventListener('click', () => envQuickAxis(a.id, -1));
  inc.addEventListener('click', () => envQuickAxis(a.id, +1));
  wrap.appendChild(name);
  wrap.appendChild(dec);
  wrap.appendChild(level);
  wrap.appendChild(inc);
  return wrap;
}
function envQuickAxis(id, delta) {
  if (!envIsActive()) return;
  const a = axisById(id);
  if (!a) return;
  const levels = Array.isArray(a.levels) ? a.levels : [];
  const target = Math.max(0, Math.min(Math.max(0, levels.length - 1), (a.level | 0) + delta));
  if (target === a.level) return;
  const label = levels[target] && levels[target].label != null ? levels[target].label : String(target);
  postEnv('axis.' + a.id + '=' + target + '\n', '快调成功：' + (a.name || a.id) + ' → ' + label);
}

// ---- 事件卡弹出层 ----
function openEvents() {
  if (!envIsActive()) return;
  closeFineDrawer();
  eventsOpen = true;
  eventsSig = '';
  setCls('event-pop', 'show', true);
  renderEventPop();
  layoutEnvPanels();
  fetchEnv();
}
function closeEvents() {
  eventsOpen = false;
  setCls('event-pop', 'show', false);
}
function toggleEvents() { if (eventsOpen) closeEvents(); else openEvents(); }

function renderEventPop() {
  const env = envData;
  const events = env && Array.isArray(env.events) ? env.events : [];
  const inf = env && env.influence && typeof env.influence === 'object' ? env.influence : null;
  const cur = inf && Number.isFinite(inf.cur) ? inf.cur : 0;
  const max = inf && Number.isFinite(inf.max) ? inf.max : null;
  const active = envIsActive();
  // 影响力随时间恢复 → 卡片可用性/说明要跟着刷新；按状态做签名减少无谓重建
  const sig = events.map((e) => [e.id, e.cost, e.name].join(':')).join('|') +
    '|' + Math.floor(cur) + '|' + (active ? 1 : 0);
  if (sig === eventsSig) return;
  eventsSig = sig;
  const box = el('event-list');
  if (!box) return;
  box.innerHTML = '';
  if (!events.length) {
    const d = document.createElement('div');
    d.className = 'hint';
    d.textContent = '环境接口尚未就绪（GET /env 没有事件数据）';
    box.appendChild(d);
  }
  for (const e of events) {
    const cost = Number(e.cost);
    let reason = '';
    if (!active) reason = '启动后可实时调控';
    else if (Number.isFinite(cost) && cur < cost) reason = '影响力不足（需 ' + cost + '，当前 ' + Math.floor(cur) + '）';
    const card = document.createElement('button');
    card.type = 'button';
    card.className = 'ecard';
    card.disabled = !!reason;
    card.title = reason || (e.desc || '');
    const top = document.createElement('div');
    top.className = 'ecard-top';
    const nm = document.createElement('span');
    nm.className = 'ecard-name';
    nm.textContent = e.name || e.id;
    const cst = document.createElement('span');
    cst.className = 'ecard-cost';
    cst.textContent = (Number.isFinite(cost) ? cost : '?') + ' 影响力';
    top.appendChild(nm);
    top.appendChild(cst);
    const ds = document.createElement('div');
    ds.className = 'ecard-desc';
    ds.textContent = e.desc || '';
    const ft = document.createElement('div');
    ft.className = 'ecard-foot';
    ft.textContent = reason || ('点击触发 · 消耗 ' + (Number.isFinite(cost) ? cost : '?') + ' 影响力');
    card.appendChild(top);
    card.appendChild(ds);
    card.appendChild(ft);
    card.addEventListener('click', () => { if (!card.disabled) envTriggerEvent(e); });
    box.appendChild(card);
  }
  setTxt('event-sum', '影响力 ' + Math.floor(cur) + ' / ' + (max == null ? '--' : Math.round(max)));
}

function envTriggerEvent(e) {
  postEnv('event.' + e.id + '=1\n', '事件已触发：' + (e.name || e.id));
}

// ---- 右侧细调抽屉 ----
function closeFineDrawer() {
  fineOpen = false;
  setCls('fine-drawer', 'show', false);
}
// 细调费用：优先读 GET /env 的可选字段 env.costs.fine；后端未提供时只给中性文案
// （契约要求前端不写死 1/3/5/10 常量）。
// 当前后端规则（sim-dev + Lead 确认）：同一 POST /env 里按轴合并提交的多个 fine.* 视为**一批**，
// 整批只扣一次费用；影响不足以支付一批时整批 rejected。UI 文案与预览按「整批」描述。
function fineCostValue() {
  const env = envData;
  const costs = env && env.costs && typeof env.costs === 'object' ? env.costs : null;
  return costs && Number.isFinite(costs.fine) ? costs.fine : null;
}
// 本轴「会被提交」的 fine 键：先按 schema 的 min/max 钳制，再与当前值比较（与 envApplyFine 同一口径）
function fineChangedList(a) {
  const fine = Array.isArray(a.fine) ? a.fine : [];
  const out = [];
  for (const f of fine) {
    const rec = fineInputs[f.key];
    if (!rec) continue;
    let v = Number(rec.num.value);
    if (!Number.isFinite(v)) continue;
    const lo = Number(f.min), hi = Number(f.max);
    if (Number.isFinite(lo) && v < lo) v = lo;
    if (Number.isFinite(hi) && v > hi) v = hi;
    const curN = Number(f.cur);
    if (!Number.isFinite(curN) || Math.abs(v - curN) > 1e-9) out.push({ f: f, v: v });
  }
  return out;
}
function updateFineEstimate(axisId) {
  const ui = fineGroupUI[axisId];
  if (!ui || !ui.est) return;
  const changed = fineChangedList(ui.axis);
  if (!changed.length) {
    ui.est.textContent = '没有改动（数值与当前一致）';
    return;
  }
  const c = fineCostValue();
  ui.est.textContent = '本次提交 ' + changed.length + ' 项' +
    (c == null ? ' · 费用以后端裁决为准' : ' · 整批消耗 ' + c + ' 影响力');
}
// 面板可用空间：右侧状态栏是竖排覆盖层，面板放在状态栏左侧；上界给控制条，
// 下界给底部快调条 / 打开中的时间轴。这样面板不会被右侧状态栏挡住。
function envPanelSpace() {
  const stage = el('stage'), hud = el('hud'), quick = el('quickbar');
  if (!stage) return null;
  const stR = stage.getBoundingClientRect ? stage.getBoundingClientRect() : null;
  const hudR = hud && hud.getBoundingClientRect ? hud.getBoundingClientRect() : null;
  const qbR = quick && quick.getBoundingClientRect ? quick.getBoundingClientRect() : null;
  const tlR = timelineOpen && el('timeline-panel')
    ? el('timeline-panel').getBoundingClientRect() : null;
  const stageH = stR && stR.height ? stR.height : (stage.clientHeight || 0);
  if (!stageH) return null;
  const stageTop = stR ? stR.top : 0;
  const cbR = el('controlbar') && el('controlbar').getBoundingClientRect
    ? el('controlbar').getBoundingClientRect() : null;
  let top = 12;
  if (cbR && cbR.height > 0) top = Math.round(cbR.bottom - stageTop + 8);
  const hudCollapsed = document.body.classList.contains('hud-collapsed');
  const rightOffset = (!hudCollapsed && hudR && hudR.width > 0)
    ? Math.round(hudR.width + 24) : 12;
  let bottom = stageH - 62;
  if (qbR && qbR.height > 0) {
    const b = Math.round(qbR.top - stageTop - 8);
    if (b < bottom) bottom = b;
  }
  if (tlR && tlR.height > 0) {
    const b = Math.round(tlR.top - stageTop - 8);
    if (b < bottom) bottom = b;
  }
  return { top, bottom, stageH, right: rightOffset, avail: Math.max(140, bottom - top) };
}
function layoutEnvPanels() {
  const sp = envPanelSpace();
  if (!sp) return;
  const drawer = el('fine-drawer');
  if (drawer && fineOpen) {
    drawer.style.top = sp.top + 'px';
    drawer.style.right = sp.right + 'px';
    drawer.style.maxHeight = sp.avail + 'px';
  }
  const pop = el('event-pop');
  if (pop) {
    // 事件层底部贴住快调条/时间轴上方；最大高度限制到控制条下沿，超出时内部滚动
    pop.style.right = sp.right + 'px';
    pop.style.bottom = Math.max(8, sp.stageH - sp.bottom) + 'px';
    pop.style.maxHeight = sp.avail + 'px';
  }
}
function openFine() {
  if (!envIsActive()) return;
  closeEvents();
  fineOpen = true;
  fineSig = '';
  setCls('fine-drawer', 'show', true);
  renderFineDrawer();
  layoutEnvPanels();
  fetchEnv();
}
function toggleFine() { if (fineOpen) closeFineDrawer(); else openFine(); }
function closeEnvPanels() { closeEvents(); closeFineDrawer(); }

function renderFineDrawer() {
  if (!fineOpen) return;
  const env = envData;
  const axes = env && Array.isArray(env.axes) ? env.axes : [];
  const inf = env && env.influence && typeof env.influence === 'object' ? env.influence : null;
  const cur = inf && Number.isFinite(inf.cur) ? inf.cur : null;
  const max = inf && Number.isFinite(inf.max) ? inf.max : null;
  const regen = inf && Number.isFinite(inf.regenPerFrame) ? inf.regenPerFrame : null;
  const active = envIsActive();
  setTxt('fine-influence', cur == null ? '影响力 --' : ('影响力 ' + Math.floor(cur) + ' / ' + (max == null ? '--' : Math.round(max))));
  // 费用优先取后端可选字段 env.costs.fine；没有就只说明「消耗影响力」（不写死数值）
  const costs = env && env.costs && typeof env.costs === 'object' ? env.costs : null;
  const fineCost = costs && Number.isFinite(costs.fine) ? costs.fine : null;
  setTxt('fine-cost', fineCost == null
    ? '细调消耗影响力（以服务端裁决为准）'
    : ('每批细调消耗 ' + fineCost + ' 影响力（同轴多个键合并为一批）'));
  let note = '暂停时不回蓝';
  if (regen != null) note = '影响力每帧 +' + regen + ' · ' + (env && env.paused ? '已暂停：不回蓝' : note);
  else if (env && env.paused) note = '已暂停：暂停时不回蓝';
  setTxt('fine-note', note + ' · 松开滑杆/离开数字框后自动应用');

  // 结构签名：轴集合 / fine 键 / min-max / 档位 / 可用性 / 细调费用 / 展开状态
  const sig = axes.map((a) => a.id + ':' + a.level + ':' +
    (Array.isArray(a.fine) ? a.fine.map((f) => f.key + ':' + f.min + ':' + f.max).join(',') : '')).join('|') +
    '|' + (active ? 1 : 0) + '|' + (fineCostValue() == null ? '-' : fineCostValue()) +
    '|' + (fineExpanded ? 1 : 0);
  if (sig !== fineSig) {
    fineSig = sig;
    fineInputs = Object.create(null);
    fineGroupUI = Object.create(null);
    setCls('fine-drawer', 'fine-expanded', fineExpanded);
    setTxt('fine-expand', fineExpanded ? '收起高级' : '展开更多');
    const box = el('fine-list');
    if (box) {
      box.innerHTML = '';
      for (const a of axes) box.appendChild(buildFineGroup(a, active));
      if (!axes.length) {
        const d = document.createElement('div');
        d.className = 'hint';
        d.textContent = '环境接口尚未就绪（GET /env 没有轴数据）';
        box.appendChild(d);
      }
    }
  } else {
    syncFineValues(axes);
  }
}

function syncFineValues(axes) {
  for (const a of axes) {
    // 每轮 /env 都会生成新的轴对象：把预览用的引用也换成最新的，否则费用/改动预览
    // 会拿「上一轮」的 fine.cur 做比较，apply 成功后仍显示“本次提交 N 项”。
    const ui = fineGroupUI[a.id];
    if (ui) ui.axis = a;
    const fine = Array.isArray(a.fine) ? a.fine : [];
    for (const f of fine) {
      const rec = fineInputs[f.key];
      if (!rec) continue;
      rec.cur = f.cur;
      if (!fineDirty[f.key]) {           // 用户没改过的行才跟随服务端当前值
        if (rec.range) rec.range.value = String(f.cur);
        if (rec.num) rec.num.value = String(f.cur);
      }
      if (rec.tag) rec.tag.textContent = '当前 ' + f.cur;
    }
    updateFineEstimate(a.id);            // 服务端 cur 变了，改动项数/费用预览跟着更新
  }
}

function buildFineGroup(a, active) {
  const g = document.createElement('div');
  g.className = 'fine-group';
  const head = document.createElement('div');
  head.className = 'fine-head';
  const nm = document.createElement('span');
  nm.className = 'fine-axis';
  nm.textContent = a.name || a.id;
  const lvl = document.createElement('span');
  lvl.className = 'fine-level';
  const levels = Array.isArray(a.levels) ? a.levels : [];
  lvl.textContent = levels[a.level] && levels[a.level].label != null ? levels[a.level].label : ('档 ' + a.level);
  head.appendChild(nm);
  head.appendChild(lvl);
  g.appendChild(head);
  const fine = Array.isArray(a.fine) ? a.fine : [];
  if (!fine.length) {
    const d = document.createElement('div');
    d.className = 'hint';
    d.textContent = '该轴没有可细调参数';
    g.appendChild(d);
  }
  const addRow = (f) => {
    const lo = Number(f.min), hi = Number(f.max);
    const row = document.createElement('div');
    row.className = 'fine-row';
    const lab = document.createElement('label');
    lab.className = 'fine-label';
    lab.textContent = (f.label || f.key) + (f.unit ? '（' + f.unit + '）' : '');
    const range = document.createElement('input');
    range.type = 'range';
    range.className = 'fine-range';
    range.step = 'any';
    if (Number.isFinite(lo)) range.min = String(lo);
    if (Number.isFinite(hi)) range.max = String(hi);
    range.value = String(f.cur);
    const num = document.createElement('input');
    num.type = 'number';
    num.className = 'fine-num';
    num.step = 'any';
    if (Number.isFinite(lo)) num.min = String(lo);
    if (Number.isFinite(hi)) num.max = String(hi);
    num.value = String(f.cur);
    const tag = document.createElement('span');
    tag.className = 'fine-cur';
    tag.textContent = '当前 ' + f.cur;
    // 滑杆/数字输入双向同步；编辑过的键记为 dirty，轮询刷新时不覆盖用户正在改的值。
    // 改动在「松开滑杆 / 离开数字框（change）」后自动提交 → 调完立即生效。
    const markDirty = (from) => {
      fineDirty[f.key] = true;
      row.classList.add('dirty');
      if (from === range) num.value = range.value;
      else if (from === num && Number.isFinite(Number(num.value))) range.value = num.value;
      updateFineEstimate(a.id);
    };
    const queueAuto = () => {
      if (!active) return;
      if (fineAutoTimers[a.id]) clearTimeout(fineAutoTimers[a.id]);
      fineAutoTimers[a.id] = setTimeout(() => {
        fineAutoTimers[a.id] = 0;
        if (envIsActive()) envApplyFine(a, true);
      }, 320);
    };
    range.addEventListener('input', () => markDirty(range));
    num.addEventListener('input', () => markDirty(num));
    range.addEventListener('change', queueAuto);
    num.addEventListener('change', queueAuto);
    fineInputs[f.key] = { row, range, num, tag, cur: f.cur, min: lo, max: hi };
    row.appendChild(lab);
    row.appendChild(range);
    row.appendChild(num);
    row.appendChild(tag);
    g.appendChild(row);
    if (f.desc) {
      const desc = document.createElement('div');
      desc.className = 'fine-desc';
      desc.textContent = f.desc;
      g.appendChild(desc);
    }
  };
  const basic = fine.filter((f) => !f.advanced);
  const advanced = fine.filter((f) => f.advanced);
  basic.forEach(addRow);
  if (advanced.length) {
    if (fineExpanded) {
      const sep = document.createElement('div');
      sep.className = 'fine-advanced-sep';
      sep.textContent = '高级参数';
      g.appendChild(sep);
      advanced.forEach(addRow);
    } else {
      const tip = document.createElement('div');
      tip.className = 'fine-advanced-tip';
      tip.textContent = '还有 ' + advanced.length + ' 个高级参数：点右上角「展开更多」';
      g.appendChild(tip);
    }
  }
  const est = document.createElement('span');
  est.className = 'fine-est';
  g.appendChild(est);
  const apply = document.createElement('button');
  apply.className = 'fine-apply';
  apply.textContent = '立即应用「' + (a.name || a.id) + '」细调';
  apply.disabled = !active;
  apply.addEventListener('click', () => envApplyFine(a, false));
  g.appendChild(apply);
  fineGroupUI[a.id] = { axis: a, est: est, apply: apply };
  updateFineEstimate(a.id);
  return g;
}

async function envApplyFine(a, auto) {
  if (!envIsActive()) return;
  // 用最新一轮 /env 的轴对象（按钮闭包里的 a 可能来自上一次渲染，fine.cur 已过期）
  const axis = axisById(a.id) || a;
  const fine = Array.isArray(axis.fine) ? axis.fine : [];
  for (const f of fine) {
    const rec = fineInputs[f.key];
    if (rec && !Number.isFinite(Number(rec.num.value))) {
      showEnvToast('「' + (f.label || f.key) + '」不是有效数字', false);
      return;
    }
  }
  const changed = fineChangedList(axis);   // 与费用预览同一口径（已按 min/max 钳制）
  if (!changed.length) {
    if (!auto) showEnvToast('没有改动（数值与当前一致）', false);
    return;
  }
  const lines = changed.map((c) => 'fine.' + c.f.key + '=' + c.v);
  const ok = await postEnv(lines.join('\n') + '\n',
    (auto ? '已立即应用：' : '细调已应用：') + (axis.name || axis.id) +
    '（' + changed.length + ' 项）');
  if (!ok) return;
  // 成功后才清 dirty；失败时保留用户输入，方便重试。
  for (const f of fine) {
    fineDirty[f.key] = false;
    const rec = fineInputs[f.key];
    if (rec && rec.row) rec.row.classList.remove('dirty');
  }
  updateFineEstimate(axis.id);
}

// 面板按钮/关闭按钮/轮询
el('btn-events').addEventListener('click', toggleEvents);
el('btn-fine').addEventListener('click', toggleFine);
el('event-close').addEventListener('click', closeEvents);
el('fine-close').addEventListener('click', closeFineDrawer);
el('fine-expand').addEventListener('click', () => {
  fineExpanded = !fineExpanded;
  fineSig = '';                     // 强制重建：高级参数行要插入/移除
  renderFineDrawer();
  layoutEnvPanels();
});
// 窗口缩放 / HUD「渲染自检」展开收起都会改变可用高度 → 已打开的面板重新限高定位
window.addEventListener('resize', () => { if (eventsOpen || fineOpen) layoutEnvPanels(); });
(function hookHudDebugToggle() {
  const d = el('hud-debug');
  if (d && d.addEventListener) d.addEventListener('toggle', () => { if (eventsOpen || fineOpen) layoutEnvPanels(); });
})();
// /env 轮询：运行/暂停态每秒刷新（影响力恢复、档位都可能变）；
// 打开面板时同样走这条轮询。操作成功/失败后 postEnv/fetchEnv 会立即刷新一次。
setInterval(() => {
  if (exiting) return;
  if (envIsActive()) fetchEnv();
}, ENV_POLL_MS);

// ---------- 启动 ----------
resizeCanvas();          // 先按 #stage 实际尺寸重建位图，否则 1280×720 位图会被 CSS 拉伸成椭圆
updateZoomLabel();
loadSidebar();           // 恢复上次的侧栏折叠状态（会再次触发 resizeCanvas）
loadHud();               // 恢复右侧状态栏的折叠状态
loadQuick();             // 回填上次的快速设置（seed/balls/nuclei/frames）
loadEnvPrefs();          // 回填「启用蓝细菌 / 推荐生态 mod」勾选状态（默认都勾选）
requestAnimationFrame(renderLoop);
setInterval(pollStatus, 400);
setInterval(pollState, 50);
pollStatus();
fetchEnv();              // 菜单态也先取一次：四轴 chip / 事件费用来自后端，菜单态只读展示
loadPacks();  // 启动时应用材质包调色板
refreshLegend();  // 先用默认色填图例；材质包/快照到达后会自动重算
syncSpeedFromConfig();  // 帧率下拉框对齐 env_config.txt 的 max_fps

// ---------- 存档页 ----------
// 保存当前世界为存档：按钮与快捷键 S 共用同一个实现，避免两处逻辑漂移。
async function saveCurrent() {
  const name = prompt('存档名称：', '存档-帧' + frame);
  if (!name) return;
  let json = { ok: false, err: '网络错误' };
  try {
    const res = await fetch('/save', {
      method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: 'name=' + name,
    });
    json = await res.json();
  } catch (e) { /* keep */ }
  if (!json.ok) { alert('保存失败：' + (json.err || '未知错误')); return; }
  // 缩略图：截当前画面回传
  try {
    const dataUrl = canvas.toDataURL('image/png');
    const b64 = dataUrl.slice(dataUrl.indexOf(',') + 1);
    await postBody('/save-thumb', 'name=' + name + '\n' + b64);
  } catch (e) { /* 缩略图失败不阻塞 */ }
  if (timelineOpen) { await fetchTimelineData(); renderTimelinePanel(); }
  alert('已保存存档「' + name + '」，可在「存档」页查看');
}
el('btn-save').addEventListener('click', saveCurrent);

// ---------- 存档时间轴：拖动查看历史帧 / 变动世界线（v2.3.0） ----------
function timelineRange() {
  if (!timelineData || !timelineData.ok) return null;
  const cs = Array.isArray(timelineData.checkpoints) ? timelineData.checkpoints : [];
  const min = Number.isFinite(timelineData.min) ? timelineData.min : (cs.length ? cs[0] : 0);
  const max = Number.isFinite(timelineData.max) ? timelineData.max : (cs.length ? cs[cs.length - 1] : 0);
  if (max < min) return null;
  return { min, max, count: cs.length };
}
function setTimelineBusy(b) {
  timelineBusy = b;
  for (const id of ['tl-range', 'tl-frame', 'btn-tl-view', 'btn-tl-branch']) {
    const n = el(id);
    if (n) n.disabled = b;
  }
}
function closeTimelinePanel() {
  timelineOpen = false;
  timelineDragging = false;
  document.body.classList.remove('timeline-open');
  setCls('timeline-panel', 'show', false);
  if ((eventsOpen || fineOpen) && typeof layoutEnvPanels === 'function') layoutEnvPanels();
}
async function fetchTimelineData() {
  try {
    const res = await fetch('/timeline', { cache: 'no-store' });
    if (!res.ok) return null;
    const d = await res.json();
    if (d && d.ok) timelineData = d;
    return d;
  } catch (e) { return null; }
}
function renderTimelinePanel() {
  if (!timelineData || !timelineData.ok) return;
  const r = timelineRange();
  const range = el('tl-range'), num = el('tl-frame');
  if (!r) {
    if (range) range.disabled = true;
    if (num) num.disabled = true;
    setTxt('tl-current', '当前帧 ' + frame);
    setTxt('tl-hint', '这个存档没有可用的历史检查点。');
    return;
  }
  if (range) {
    range.min = String(r.min);
    range.max = String(r.max);
    range.disabled = timelineBusy || r.min >= r.max;
  }
  if (num) {
    num.min = String(r.min);
    num.max = String(r.max);
    num.disabled = timelineBusy || r.min >= r.max;
  }
  if (!timelineDragging && !timelineBusy) {
    const cur = Math.max(r.min, Math.min(r.max, frame));
    if (range) range.value = String(cur);
    if (num) num.value = String(cur);
  }
  setTxt('tl-source', '来源：' + (timelineData.source || '当前世界'));
  setTxt('tl-current', timelineBusy ? '定位中…' : ('当前帧 ' + frame));
  setTxt('tl-hint', r.min >= r.max
    ? '这个存档只有 1 个检查点；继续运行并保存后会积累更多可查看帧。'
    : ('共 ' + r.count + ' 个检查点（每 200 帧一个）。可查看 ' + r.min + '–' + r.max +
       ' 帧：拖动滑杆松开或输入帧号后点「查看」。定位后按「变动世界线」可从这里开新局。'));
}
async function openTimelinePanel() {
  if (uiState !== 'paused') {
    showEnvToast('时间轴只会在暂停/读档后打开；请先暂停', false);
    return;
  }
  closeEnvPanels();
  const d = await fetchTimelineData();
  if (!d || !d.ok) {
    showEnvToast('时间轴不可用：' + ((d && d.err) || '没有读档或后端未就绪'), false);
    return;
  }
  timelineOpen = true;
  document.body.classList.add('timeline-open');
  setCls('timeline-panel', 'show', true);
  renderTimelinePanel();
  layoutEnvPanels();
}
async function seekTimeline(target, announce) {
  if (timelineBusy) return false;
  const r = timelineRange();
  if (!r) return false;
  target = Math.max(r.min, Math.min(r.max, Math.round(Number(target) || 0)));
  setTimelineBusy(true);
  setTxt('tl-current', '定位中…');
  let json = null;
  try {
    const res = await fetch('/seek', {
      method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: 'frame=' + target,
    });
    json = await res.json();
  } catch (e) { json = null; }
  setTimelineBusy(false);
  if (!json || !json.ok) {
    showEnvToast('定位失败：' + ((json && json.err) || '网络错误'), false);
    if (timelineOpen) renderTimelinePanel();
    return false;
  }
  buf.fill(0);                  // 时间轴回跳后旧轨迹没有意义
  frame = json.frame;
  await pollState();
  await fetchTimelineData();
  if (timelineOpen) renderTimelinePanel();
  if (announce) showEnvToast('已定位到帧 ' + json.frame, true);
  return true;
}
el('btn-timeline').addEventListener('click', openTimelinePanel);
el('btn-timeline-close').addEventListener('click', closeTimelinePanel);
el('tl-range').addEventListener('input', () => {
  timelineDragging = true;
  const v = el('tl-range').value;
  el('tl-frame').value = v;
  setTxt('tl-current', '目标帧 ' + v);
});
el('tl-range').addEventListener('change', () => {
  timelineDragging = false;
  seekTimeline(el('tl-range').value, true);
});
el('tl-frame').addEventListener('change', () => seekTimeline(el('tl-frame').value, true));
el('btn-tl-view').addEventListener('click', () => seekTimeline(el('tl-frame').value, true));

function enterBranchMode(target) {
  branchMode = true;
  branchFrame = target;
  el('branch-frame').textContent = String(target);
  el('branch-banner').classList.add('show');
  closeTimelinePanel();
}
function cancelBranchMode() {
  branchMode = false;
  el('branch-banner').classList.remove('show');
}
el('btn-tl-branch').addEventListener('click', async () => {
  if (timelineBusy) return;
  const r = timelineRange();
  if (!r) return;
  let target = Math.round(Number(el('tl-frame').value));
  if (!Number.isFinite(target)) target = frame;
  target = Math.max(r.min, Math.min(r.max, target));
  if (target !== frame) {
    const ok = await seekTimeline(target, false);
    if (!ok) return;
  }
  enterBranchMode(frame);
  switchTab('params');
  schemaData = null;                  // 强制按“当前世界配置”重载参数页
  await loadSchema();
  showEnvToast('已进入世界线分支：调整参数后点「开始新世界线」', true);
});
el('btn-branch-cancel').addEventListener('click', () => {
  cancelBranchMode();
  switchTab('run');
});
el('btn-branch-start').addEventListener('click', async () => {
  if (!branchMode) return;
  if (!schemaData) await loadSchema();
  const frames = Math.max(1, Math.round(Number(el('branch-frames').value) || 100000));
  const body = collectParams() + 'frames=' + frames + '\n';
  if (!body) { showEnvToast('参数尚未加载完成', false); return; }
  el('btn-branch-start').disabled = true;
  let json = null;
  try {
    const res = await fetch('/branch', {
      method: 'POST', headers: { 'Content-Type': 'text/plain' }, body,
    });
    json = await res.json();
  } catch (e) { json = null; }
  el('btn-branch-start').disabled = false;
  if (!json || !json.ok) {
    showEnvToast('分支失败：' + ((json && json.err) || '网络错误'), false);
    return;
  }
  cancelBranchMode();
  timelineData = null;
  switchTab('run');
  buf.fill(0);
  showEnvToast('世界线已从帧 ' + json.frame + ' 重新开始', true);
  setTimeout(pollStatus, 100);
});


function mkBtn(text) {
  const b = document.createElement('button');
  b.textContent = text;
  return b;
}

async function loadSaves() {
  let saves = [];
  try {
    const res = await fetch('/saves', { cache: 'no-store' });
    saves = await res.json();
  } catch (e) { return; }
  const list = el('saves-list');
  list.innerHTML = '';
  if (!saves.length) {
    const d = document.createElement('div');
    d.className = 'hint';
    d.textContent = '还没有存档。运行模拟时点「保存」按钮即可创建。';
    list.appendChild(d);
    return;
  }
  for (const s of saves) {
    const card = document.createElement('div');
    card.className = 'savecard';
    const url = '/savefile/' + encodeURIComponent(s.name) + '/thumb.png';
    if (s.thumb) {
      const img = document.createElement('img');
      img.src = url;
      img.onerror = () => { img.style.visibility = 'hidden'; };
      card.appendChild(img);
    } else {
      const noimg = document.createElement('div');
      noimg.className = 'noimg';
      noimg.textContent = '无缩略图';
      card.appendChild(noimg);
    }
    const info = document.createElement('div');
    info.className = 'sinfo';
    const nm = document.createElement('div');
    nm.className = 'sname';
    nm.textContent = s.name;
    const meta = document.createElement('div');
    meta.className = 'smeta';
    meta.innerHTML = `保存于 ${s.created}<br>帧 ${s.frame} · 种子 ${s.seed} · 存活核 ${s.alive}（初始 ${s.nuclei}）· 球 ${s.balls}` +
      (s.mods ? `<br>mod: ${s.mods}` : '');
    const btns = document.createElement('div');
    btns.className = 'sbtns';
    const bLoad = mkBtn('载入');
    bLoad.disabled = uiState !== 'menu';
    bLoad.title = bLoad.disabled ? '返回菜单后可载入' : '';
    bLoad.onclick = async () => {
      autoTimeline = true;                  // 读档完成后暂停 → 自动打开时间轴
      await postBody('/load', 'name=' + s.name);
      switchTab('run');
      setTimeout(() => { autoTimeline = false; }, 12000);   // 读档失败时不至于一直挂着
    };
    const bDel = mkBtn('删除');
    bDel.onclick = async () => {
      if (confirm('删除存档「' + s.name + '」？此操作不可恢复。')) {
        await postBody('/delete-save', 'name=' + s.name);
        loadSaves();
      }
    };
    const bRen = mkBtn('重命名');
    bRen.onclick = async () => {
      const nn = prompt('新名称：', s.name);
      if (nn && nn !== s.name) {
        await postBody('/rename-save', 'old=' + s.name + '\nnew=' + nn);
        loadSaves();
      }
    };
    btns.appendChild(bLoad);
    btns.appendChild(bRen);
    btns.appendChild(bDel);
    info.appendChild(nm);
    info.appendChild(meta);
    info.appendChild(btns);
    card.appendChild(info);
    list.appendChild(card);
  }
}
el('btn-refresh-saves').addEventListener('click', loadSaves);

// ---------- 材质包 ----------
el('subtab-mods').addEventListener('click', () => showModSub('mods'));
el('subtab-packs').addEventListener('click', () => showModSub('packs'));
function showModSub(which) {
  el('subtab-mods').classList.toggle('active', which === 'mods');
  el('subtab-packs').classList.toggle('active', which === 'packs');
  el('mods-list').style.display = which === 'mods' ? '' : 'none';
  el('warnlist').style.display = which === 'mods' ? '' : 'none';
  el('packs-list').style.display = which === 'packs' ? '' : 'none';
  el('btn-save-mods').style.display = which === 'mods' ? '' : 'none';
  el('btn-save-packs').style.display = which === 'packs' ? '' : 'none';
}

// colors.txt 解析：颜色键 `key = r, g, b` + 标量键（尺寸 / 透明度）。
// ⚠ 标量分支必须放在 r,g,b 分支**之前**：否则 size_ball_shield = 5 会被
// 当成颜色解析（parts.length < 3）而**静默丢弃**，材质包的尺寸键全部失效。
// cyano_node / cyano_link 这两个颜色键语义是"蓝细菌整体配色"，与按类型分色的 ball_* 不同，
// 故用 cyano_color_mode 开关：colony = 按 colonyId 分色（默认，任务 D），
// flat = 整体单色（此时 cyano_node / cyano_link 生效）。
// v2.2.0 新增可选键 cyano_link_alpha（0.35~1）：连线的实际不透明度，缺省 0.7。
const SCALAR_KEYS = ['size_ball_shield', 'size_ball_worker', 'size_ball_scout',
                     'size_free_ball', 'size_nucleus', 'cyano_node_size', 'cyano_link_alpha'];
const STRING_KEYS = ['cyano_color_mode'];   // 字符串值键（枚举）
function parseColors(text) {
  const out = {};
  for (const line of text.split('\n')) {
    const t = line.trim();
    if (!t || t.startsWith('#')) continue;
    const eq = t.indexOf('=');
    if (eq < 0) continue;
    const key = t.slice(0, eq).trim();
    const val = t.slice(eq + 1).trim();
    if (key === 'mode') { out.mode = val; continue; }
    if (SCALAR_KEYS.includes(key)) {
      const n = parseFloat(val);
      if (!isNaN(n)) out[key] = n;
      continue;
    }
    if (STRING_KEYS.includes(key)) { out[key] = val; continue; }
    const parts = val.split(',').map((s) => parseInt(s.trim(), 10));
    if (parts.length >= 3 && parts.every((n) => !isNaN(n))) out[key] = parts.slice(0, 3);
  }
  return out;
}

async function refreshPalette() {
  // 按 packs.list 顺序叠加（后者覆盖前者）。
  let merged = null;
  for (const name of enabledPacks) {
    try {
      const res = await fetch('/rp/' + encodeURIComponent(name) + '/colors.txt', { cache: 'no-store' });
      if (!res.ok) continue;
      const p = parseColors(await res.text());
      if (!merged) merged = {};
      for (const k in p) merged[k] = p[k];
    } catch (e) { /* 忽略缺失的 colors.txt */ }
  }
  applyPaletteToRenderer(merged);
}

async function loadPacks() {
  try {
    const res = await fetch('/packs', { cache: 'no-store' });
    packsData = await res.json();
  } catch (e) { return; }
  enabledPacks = packsData.filter((p) => p.enabled)
                 .sort((a, b) => a.order - b.order).map((p) => p.name);
  renderPacks();
  await refreshPalette();
}

function renderPacks() {
  const list = el('packs-list');
  list.innerHTML = '';
  if (!packsData || !packsData.length) {
    const d = document.createElement('div');
    d.className = 'hint';
    d.textContent = '没有发现材质包。把材质包文件夹放到 exe 旁的 resourcepacks/ 目录即可（含 pack.txt + colors.txt）。';
    list.appendChild(d);
    return;
  }
  for (const p of packsData) {
    const item = document.createElement('div');
    item.className = 'packitem';
    const cb = document.createElement('input');
    cb.type = 'checkbox';
    cb.checked = enabledPacks.includes(p.name);
    cb.addEventListener('change', () => {
      if (cb.checked) {
        if (!enabledPacks.includes(p.name)) enabledPacks.push(p.name);
      } else {
        enabledPacks = enabledPacks.filter((x) => x !== p.name);
      }
      renderPacks();
      refreshPalette();
    });
    const orderSpan = document.createElement('span');
    orderSpan.className = 'order';
    const pos = enabledPacks.indexOf(p.name);
    orderSpan.textContent = pos >= 0 ? '#' + (pos + 1) : '—';
    const info = document.createElement('div');
    info.className = 'pinfo';
    const name = document.createElement('div');
    name.className = 'pname';
    name.textContent = p.name + (p.version ? ' v' + p.version : '');
    const desc = document.createElement('div');
    desc.className = 'pdesc';
    desc.textContent = (p.desc || '') + (p.author ? '（' + p.author + '）' : '');
    info.appendChild(name);
    info.appendChild(desc);
    const btns = document.createElement('div');
    btns.className = 'porder-btns';
    const up = mkBtn('↑');
    up.disabled = pos <= 0;
    up.onclick = () => {
      if (pos <= 0) return;
      [enabledPacks[pos - 1], enabledPacks[pos]] = [enabledPacks[pos], enabledPacks[pos - 1]];
      renderPacks();
      refreshPalette();
    };
    const down = mkBtn('↓');
    down.disabled = pos < 0 || pos >= enabledPacks.length - 1;
    down.onclick = () => {
      if (pos < 0 || pos >= enabledPacks.length - 1) return;
      [enabledPacks[pos + 1], enabledPacks[pos]] = [enabledPacks[pos], enabledPacks[pos + 1]];
      renderPacks();
      refreshPalette();
    };
    btns.appendChild(up);
    btns.appendChild(down);
    item.appendChild(cb);
    item.appendChild(orderSpan);
    item.appendChild(info);
    item.appendChild(btns);
    list.appendChild(item);
  }
}

el('btn-save-packs').addEventListener('click', async () => {
  await postBody('/set-packs', 'packs=' + enabledPacks.join(','));
  const badge = el('badge');
  badge.textContent = '材质包已保存';
  setTimeout(() => setUiState(uiState), 1200);
});
