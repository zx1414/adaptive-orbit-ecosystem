// 核-小球生态演化模拟器 · 启动器前端
// 后端端点：GET /status、GET /state（二进制快照）、GET /schema、GET /mods
//           POST /run、/save-config、/set-mods（body 为 key=value 行）、/control
'use strict';

// ---------- 常量 ----------
const HEADER = 64, BALL_REC = 6, NUCL_REC = 28;
const FADE = 14;          // 每渲染帧 alpha 衰减量（255 → ~18 帧 ≈ 0.3s 拖尾）
const FREE_RGB = [158, 158, 158];
const TYPE_LIGHT = [0.42, 0.60, 0.76];
const OWNER_LUT = 1024;

const el = (id) => document.getElementById(id);
const canvas = el('view');
const ctx = canvas.getContext('2d');
const W = canvas.width, H = canvas.height;
const trail = ctx.createImageData(W, H);
const buf = new Uint32Array(trail.data.buffer);

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
function applyPaletteToRenderer(p) {
  palette = p;
  if (!p) {
    freeColorWord = freeColor;
    canvas.style.background = '#05070b';
    return;
  }
  freeColorWord = packRgb(p.freeBall || FREE_RGB);
  if (p.ballShield) typeModeColors[0] = packRgb(p.ballShield);
  if (p.ballWorker) typeModeColors[1] = packRgb(p.ballWorker);
  if (p.ballScout) typeModeColors[2] = packRgb(p.ballScout);
  canvas.style.background = p.background ? `rgb(${p.background.join(',')})` : '#05070b';
}

// ---------- 全局状态 ----------
let uiState = 'menu';            // menu | running | paused | finished
let sim = null;
let frame = 0, worldW = 2000, worldH = 2000, status = 0, reason = '';
let scale = 1, ox = 0, oy = 0, viewDirty = true;
let modsData = null;             // { mods: [...], warnings: [...] }
let enabledOrder = [];           // 启用 mod id 的有序列表（= 优先级）
let schemaData = null;           // /schema JSON 数组
let packsData = null;            // /packs JSON 数组
let enabledPacks = [];           // 启用材质包有序列表
let palette = null;              // 应用后的调色板 {mode, background, ballShield, ballWorker, ballScout, freeBall, nucleus}
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
  if (s === 'menu') badge.textContent = '菜单';
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
    if (dv.byteLength < HEADER || dv.getUint8(0) !== 0x57 || dv.getUint8(1) !== 1) return;
    status = dv.getUint8(2);
    frame = dv.getInt32(4, true);
    worldW = dv.getFloat32(8, true);
    worldH = dv.getFloat32(12, true);
    if (viewDirty) fitView();
    const ballCount = dv.getInt32(16, true);
    sim = {
      dv,
      ballCount,
      nucleusCount: dv.getInt32(20, true),
      typeCount: [dv.getInt32(24, true), dv.getInt32(28, true), dv.getInt32(32, true)],
      ballOff: HEADER,
      nuclOff: HEADER + ballCount * BALL_REC,
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
    for (let yy = 0; yy < 2; yy++) {
      const y = py + yy;
      if (y < 0 || y >= H) continue;
      for (let xx = 0; xx < 2; xx++) {
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
    const id = dv.getInt32(off + 24, true);
    const sx = (wx - ox) * scale, sy = (wy - oy) * scale;
    if (sx < -30 || sx > W + 30 || sy < -30 || sy > H + 30) continue;
    const frac = Math.min(1, Math.max(0.15, threshold > 0 ? energy / threshold : 0.15));
    const rgb = hslToRgb((id * 137.50776405) % 360, 0.85, 0.55);
    ctx.beginPath();
    ctx.arc(sx, sy, 4 + 7 * frac, 0, Math.PI * 2);
    ctx.lineWidth = 2;
    ctx.strokeStyle = `rgb(${rgb[0]},${rgb[1]},${rgb[2]})`;
    ctx.stroke();
    ctx.beginPath();
    ctx.arc(sx, sy, 3.2, 0, Math.PI * 2);
    ctx.fillStyle = palette && palette.nucleus ? `rgb(${palette.nucleus.join(',')})` : '#ffffff';
    ctx.fill();
  }
}

let fpsFrames = 0, fpsT0 = performance.now(), fps = 0;
function renderLoop(now) {
  if (fpsFrames >= 30) {
    fps = fpsFrames * 1000 / (now - fpsT0);
    fpsFrames = 0; fpsT0 = now;
  }
  fpsFrames++;
  if (sim && (uiState === 'running' || uiState === 'paused' || uiState === 'finished')) {
    fadeTrail();
    stampBalls();
    ctx.putImageData(trail, 0, 0);
    drawNuclei();
  } else {
    ctx.putImageData(trail, 0, 0);
  }
  el('st-frame').textContent = frame;
  el('st-nuclei').textContent = sim ? sim.nucleusCount : 0;
  el('st-balls').textContent = sim ? sim.ballCount : 0;
  el('st-shield').textContent = sim ? sim.typeCount[0] : 0;
  el('st-worker').textContent = sim ? sim.typeCount[1] : 0;
  el('st-scout').textContent = sim ? sim.typeCount[2] : 0;
  el('st-fps').textContent = fps | 0;
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
  requestAnimationFrame(renderLoop);
}

// ---------- 视图交互 ----------
el('btn-reset-view').addEventListener('click', fitView);
canvas.addEventListener('wheel', (e) => {
  e.preventDefault();
  const rect = canvas.getBoundingClientRect();
  const mx = (e.clientX - rect.left) * (W / rect.width);
  const my = (e.clientY - rect.top) * (H / rect.height);
  const wx = mx / scale + ox, wy = my / scale + oy;
  const k = Math.pow(1.15, -e.deltaY / 100);
  scale = Math.min(20, Math.max(0.005, scale * k));
  ox = wx - mx / scale;
  oy = wy - my / scale;
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
    `核 #${dv.getInt32(off + 24, true)}<br>` +
    `能量 ${dv.getFloat32(off + 8, true).toFixed(1)} / 阈值 ${dv.getFloat32(off + 20, true).toFixed(1)}<br>` +
    `攻击强度 ${dv.getFloat32(off + 12, true).toFixed(1)} · 最大速度 ${dv.getFloat32(off + 16, true).toFixed(1)}`;
  tooltip.style.display = 'block';
  tooltip.style.left = (e.clientX - rect.left + 14) + 'px';
  tooltip.style.top = (e.clientY - rect.top + 14) + 'px';
}

// ---------- 运行页控制 ----------
function quickParams() {
  return `seed=${el('q-seed').value || 42}\n` +
         `balls=${el('q-balls').value || 1000}\n` +
         `nuclei=${el('q-nuclei').value || 20}\n` +
         `frames=${el('q-frames').value || 100000}\n`;
}
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
requestAnimationFrame(renderLoop);
setInterval(pollStatus, 400);
setInterval(pollState, 50);
pollStatus();
loadPacks();  // 启动时应用材质包调色板

// ---------- 存档页 ----------
el('btn-save').addEventListener('click', async () => {
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
