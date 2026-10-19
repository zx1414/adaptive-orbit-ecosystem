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
const TYPE_LIGHT = [0.32, 0.58, 0.86];
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
// ---- 蓝细菌配色（默认绿青，材质包可覆盖）----
let cyanoNodeCss = 'rgb(90,220,170)';
let cyanoLinkCss = 'rgba(60,140,110,0.35)';
let cyanoColorMode = 'colony';   // colony = 按 colonyId 分色（默认）；flat = 用 cyano_node/cyano_link 整色
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
// 0 是「全透明」，stampBalls() 写进像素缓冲后球就整屏隐身（详见 _agent/qa/ROOT_CAUSE.md）。
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
function applyPaletteToRenderer(p) {
  palette = p;
  applySizeKeys(p);            // 尺寸键：不依赖着色模式，type_color / owner_hue 都生效
  if (!p) {
    freeColorWord = freeColor;
    typeModeColors.fill(0);
    canvas.style.background = '#05070b';
    return;
  }
  // 取色统一走 pickArr：snake_case / camelCase 都能取到，缺键不再静默拿到 undefined。
  const free = pickArr(p, 'freeBall', 'free_ball');
  freeColorWord = packRgb(free || FREE_RGB);
  const typeKeys = [['ballShield', 'ball_shield'], ['ballWorker', 'ball_worker'], ['ballScout', 'ball_scout']];
  for (let i = 0; i < typeKeys.length; i++) {
    const rgb = pickArr(p, typeKeys[i][0], typeKeys[i][1]);
    typeModeColors[i] = rgb ? packRgb(rgb) : 0;
  }
  // 兜底阀：type_color 模式下缺色/解析失败（=0）的类型一律回落默认色（owner_hue LUT 第一组）。
  // 保证材质包即使只写了 mode、颜色键全缺，球也是可见的，而不是 alpha=0 隐身。
  if (p.mode === 'type_color') {
    for (let i = 0; i < typeModeColors.length; i++) {
      if (typeModeColors[i] === 0) typeModeColors[i] = ownerColors[i];
    }
  }
  const cn = pickArr(p, 'cyanoNode', 'cyano_node');
  if (cn) cyanoNodeCss = `rgb(${cn.join(',')})`;
  const cl = pickArr(p, 'cyanoLink', 'cyano_link');
  if (cl) cyanoLinkCss = `rgba(${cl.join(',')},0.35)`;
  if (p.cyano_color_mode) cyanoColorMode = p.cyano_color_mode;
  canvas.style.background = p.background ? `rgb(${p.background.join(',')})` : '#05070b';
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
let packsData = null;            // /packs JSON 数组
let enabledPacks = [];           // 启用材质包有序列表
let palette = null;              // 应用后的调色板；键名可能是 snake_case（colors.txt）或 camelCase（旧 JSON），读取一律走 pickArr()
let exiting = false;             // 已请求退出：停止轮询并显示退出提示

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
  if (page === 'run') resizeCanvas();
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
  el('btn-pause').disabled = !running;
  el('btn-resume').disabled = !running;
  el('btn-step').disabled = !running;
  el('btn-stop').disabled = !running;
  el('btn-save').disabled = !simActive;
  el('speed').disabled = !running;
  el('overlay').classList.toggle('show', s === 'finished');
  // 暂停角标：只在画布上叠一个很淡的状态提示，不挡内容（CSS pointer-events:none）
  el('paused-badge').classList.toggle('show', s === 'paused');
  // 空状态只在「未运行」时出现：启动后淡出；结束态用 overlay，两者不同时显示
  el('stage-empty').classList.toggle('show', s === 'menu');
  if (s === 'finished') {
    el('overlay-title').textContent = '模拟已结束';
    el('overlay-text').textContent = '结束原因：' + reason + ' · 最终帧 ' + frame;
  }
  if (prev !== 'running' && prev !== 'paused' && running) {
    buf.fill(0);           // 新一次运行：清空轨迹
    viewDirty = true;
  }
  if (prev === 'finished' && s === 'menu') {
    buf.fill(0);
  }
}

async function pollStatus() {
  if (exiting) return;
  try {
    const res = await fetch('/status', { cache: 'no-store' });
    if (!res.ok) return;
    const text = (await res.text()).trim();
    if (text === 'menu') setUiState('menu');
    else if (text === 'running') setUiState('running');
    else if (text === 'paused') setUiState('paused');
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

function stampBalls() {
  const dv = sim.dv, n = sim.ballCount;
  const sxToW = worldW / 65535, syToW = worldH / 65535;
  const typeMode = palette && palette.mode === 'type_color';
  for (let i = 0; i < n; i++) {
    const off = sim.ballOff + i * BALL_REC;
    const wx = dv.getUint16(off, true) * sxToW;
    const wy = dv.getUint16(off + 2, true) * syToW;
    const meta = dv.getUint16(off + 4, true);
    const px = ((wx - ox) * scale) | 0;
    const py = ((wy - oy) * scale) | 0;
    if (px < -2 || px >= W + 2 || py < -2 || py >= H + 2) continue;
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
    if (size < 2) {   // 1px 的极端档位退化成一个像素，保证小球可见
      if (px >= 0 && px < W && py >= 0 && py < H) buf[py * W + px] = col;
      continue;
    }
    for (let yy = 0; yy < size; yy++) {
      const y = py + yy;
      if (y < 0 || y >= H) continue;
      for (let xx = 0; xx < size; xx++) {
        const x = px + xx;
        if (x < 0 || x >= W) continue;
        buf[y * W + x] = col;
      }
    }
  }
}

function drawNuclei() {
  const dv = sim.dv, n = sim.nucleusCount;
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
    const frac = Math.min(1, Math.max(0.15, threshold > 0 ? energy / threshold : 0.15));
    const rgb = hslToRgb(hue, 0.85, 0.55);
    ctx.beginPath();
    ctx.arc(sx, sy, (3 + 5 * frac) * nucleusSize, 0, Math.PI * 2);
    ctx.lineWidth = 2;
    ctx.strokeStyle = `rgb(${rgb[0]},${rgb[1]},${rgb[2]})`;
    ctx.stroke();
    ctx.beginPath();
    ctx.arc(sx, sy, 2.2 * nucleusSize, 0, Math.PI * 2);
    ctx.fillStyle = palette && palette.nucleus ? `rgb(${palette.nucleus.join(',')})` : '#ffffff';
    ctx.fill();
  }
}

// ---------- 蓝细菌（快照 v2 附加段） ----------
// 关闭态 cyanoNodeCount === 0 → 首行即 return，零开销。
function drawCyano() {
  const p = sim.cyanoNodeCount | 0;
  if (p <= 0) return;                       // 蓝细菌关闭：立即返回，不做任何工作
  const dv = sim.dv;
  const nodeOff = sim.cyanoNodeOff;
  const linkCount = sim.cyanoLinkCount | 0;

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
    if (step > 1) {
      ctx.beginPath();                      // 抽稀时合并为一条子路径，单次 stroke
      for (let i = 0; i < linkCount; i += step) {
        const o = sim.cyanoLinkOff + i * CYANO_LINK_REC;
        const a = dv.getUint32(o, true), b = dv.getUint32(o + 4, true);
        if (a >= p || b >= p) continue;      // 越界下标：跳过，不画（防御性）
        ctx.moveTo(xs[a], ys[a]);
        ctx.lineTo(xs[b], ys[b]);
      }
      ctx.lineWidth = 1;
      ctx.strokeStyle = cyanoLinkCss;
      ctx.stroke();
    } else {
      ctx.lineWidth = 1;
      ctx.strokeStyle = cyanoLinkCss;
      for (let i = 0; i < linkCount; i++) {
        const o = sim.cyanoLinkOff + i * CYANO_LINK_REC;
        const a = dv.getUint32(o, true), b = dv.getUint32(o + 4, true);
        if (a >= p || b >= p) continue;
        ctx.beginPath();
        ctx.moveTo(xs[a], ys[a]);
        ctx.lineTo(xs[b], ys[b]);
        ctx.stroke();
      }
    }
  }

  // ---- 2. 节点（实心小圆）----
  const avg = sim.cyanoAvgEnergy || 0;
  const denom = avg * 2 + 1e-6;
  const byColony = cyanoColorMode !== 'flat';   // 默认按聚落分色；材质包可切成整体单色
  for (let i = 0; i < p; i++) {
    const o = nodeOff + i * CYANO_NODE_REC;
    if (!(dv.getUint16(o + CY_FLAGS, true) & 1)) continue;   // flags bit0 = alive
    const energy = dv.getFloat32(o + CY_ENERGY, true);
    const r = cyanoNodeSize + 3 * Math.min(1, energy / denom);
    const sx = (dv.getFloat32(o + CY_X, true) - ox) * scale;
    const sy = (dv.getFloat32(o + CY_Y, true) - oy) * scale;
    if (sx < -8 || sx > W + 8 || sy < -8 || sy > H + 8) continue;
    if (byColony) {
      // 聚落分色：47 与 360 互质，配色散；同聚落同色 → 观感上就是一个「菌落」
      const rgb = hslToRgb((dv.getUint16(o + CY_COLONY, true) * 47) % 360, 0.65, 0.55);
      ctx.fillStyle = `rgb(${rgb[0]},${rgb[1]},${rgb[2]})`;
    }
    ctx.beginPath();
    ctx.arc(sx, sy, r, 0, Math.PI * 2);
    ctx.fill();
  }
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
  if (sim && (uiState === 'running' || uiState === 'paused' || uiState === 'finished')) {
    fadeTrail();
    stampBalls();
    ctx.putImageData(trail, 0, 0);
    drawCyano();
    drawNuclei();
  } else {
    ctx.putImageData(trail, 0, 0);
  }
  LINK_THINNING_FLAG = false;
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
  el('st-cyano').title = LINK_THINNING_FLAG ? '连线已抽稀（每 4 条画 1 条）' : '';
  requestAnimationFrame(renderLoop);
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
      if (uiState !== 'menu') postCmd('menu');
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
function quickParams() {
  return `seed=${el('q-seed').value || 42}\n` +
         `balls=${el('q-balls').value || 1000}\n` +
         `nuclei=${el('q-nuclei').value || 20}\n` +
         `frames=${el('q-frames').value || 100000}\n`;
}
// ---------- 快速设置：localStorage 回填 ----------
// 只记 seed/balls/nuclei/frames 四项快速设置；参数页的全量 schema 仍以后端 env_config.txt
// 为唯一来源，不写进 localStorage，避免两套配置源互相打架。
const QUICK_KEY = 'nukesim.quick';
const QUICK_FIELDS = ['seed', 'balls', 'nuclei', 'frames'];
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

el('btn-start').addEventListener('click', async () => {
  const body = collectParams() + quickParams();
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
  try {
    const res = await fetch('/schema', { cache: 'no-store' });
    schemaData = await res.json();
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

// ---------- 启动 ----------
resizeCanvas();          // 先按 #stage 实际尺寸重建位图，否则 1280×720 位图会被 CSS 拉伸成椭圆
updateZoomLabel();
loadSidebar();           // 恢复上次的侧栏折叠状态（会再次触发 resizeCanvas）
loadQuick();             // 回填上次的快速设置（seed/balls/nuclei/frames）
requestAnimationFrame(renderLoop);
setInterval(pollStatus, 400);
setInterval(pollState, 50);
pollStatus();
loadPacks();  // 启动时应用材质包调色板
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
  alert('已保存存档「' + name + '」，可在「存档」页查看');
}
el('btn-save').addEventListener('click', saveCurrent);

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
      await postBody('/load', 'name=' + s.name);
      switchTab('run');
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

// colors.txt 解析：颜色键 `key = r, g, b` + 标量键（尺寸）。
// ⚠ 标量分支必须放在 r,g,b 分支**之前**：否则 size_ball_shield = 5 会被
// 当成颜色解析（parts.length < 3）而**静默丢弃**，材质包的尺寸键全部失效。
// cyano_node / cyano_link 这两个颜色键语义是"蓝细菌整体配色"，与按类型分色的 ball_* 不同，
// 故用 cyano_color_mode 开关：colony = 按 colonyId 分色（默认，任务 D），
// flat = 整体单色（此时 cyano_node / cyano_link 生效）。
const SCALAR_KEYS = ['size_ball_shield', 'size_ball_worker', 'size_ball_scout',
                     'size_free_ball', 'size_nucleus', 'cyano_node_size'];
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
