'use strict';
// 探针：验证 CDP + 截图 + 快照几何 + 像素位置断言在同一条链路上确实可用。
// 用法：node probe.js [--port 19001] [--edge-port 19223]
const path = require('path');
const fs = require('fs');
const { launchEdge, createPage, killTree } = require('./cdp');
const { parseSnapshot, makeGeo, makePixels, PACKS, expectedBallColor, cyanoNodeColor, cyanoLinkComposite } = require('./analysis');
const { decodePng } = require('./png');

const EDGE = 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const ROOT = path.resolve(__dirname, '..', '..', '..');
const ART = path.join(ROOT, 'tests', 'artifacts', 'red_visual');

function arg(name, dflt) {
  const i = process.argv.indexOf('--' + name);
  return i >= 0 ? process.argv[i + 1] : dflt;
}
const PORT = parseInt(arg('port', '19001'), 10);
const EDGE_PORT = parseInt(arg('edge-port', '19223'), 10);

async function post(urlPath, body, ct) {
  const r = await fetch(`http://127.0.0.1:${PORT}${urlPath}`, { method: 'POST', body, headers: { 'Content-Type': ct || 'text/plain' } });
  return r.text();
}

(async () => {
  const prof = path.join(ROOT, '_agent', 'red_visual', `edgeprof_probe`);
  fs.rmSync(prof, { recursive: true, force: true });
  const edge = await launchEdge({ edgePath: EDGE, port: EDGE_PORT, profileDir: prof });
  let page = null;
  try {
    page = await createPage(edge.cdp, `http://127.0.0.1:${PORT}/`);
    await page.setViewport(1400, 900);
    // 停止当前局，回到菜单（Running -> stop -> Finished -> menu）
    const cur = (await (await fetch(`http://127.0.0.1:${PORT}/status`)).text()).trim();
    if (cur !== 'menu') {
      await post('/control', JSON.stringify({ cmd: 'stop' }), 'application/json');
      for (let i = 0; i < 100; i++) {
        const s2 = (await (await fetch(`http://127.0.0.1:${PORT}/status`)).text()).trim();
        if (s2.startsWith('finished')) break;
        await new Promise((r) => setTimeout(r, 100));
      }
      await post('/control', JSON.stringify({ cmd: 'menu' }), 'application/json');
      for (let i = 0; i < 100; i++) {
        if ((await (await fetch(`http://127.0.0.1:${PORT}/status`)).text()).trim() === 'menu') break;
        await new Promise((r) => setTimeout(r, 100));
      }
    }
    await post('/run', 'seed=42\nballs=300\nnuclei=12\nframes=100000\ncyano_nodes=150\ncyano_colonies=4\nmax_fps=30\n');
    for (let i = 0; i < 100; i++) {
      if ((await (await fetch(`http://127.0.0.1:${PORT}/status`)).text()).trim() === 'running') break;
      await new Promise((r) => setTimeout(r, 100));
    }
    await new Promise((r) => setTimeout(r, 2200));
    await post('/control', JSON.stringify({ cmd: 'pause' }), 'application/json');
    await new Promise((r) => setTimeout(r, 500));

    const dom = await page.eval(`(() => {
      const c = document.getElementById('view'), s = document.getElementById('stage');
      const cr = c.getBoundingClientRect(), sr = s.getBoundingClientRect();
      const ui = {};
      for (const id of ['hud','controlbar','quickbar','legend','paused-badge','event-pop','fine-drawer','env-toast','overlay','tooltip']) {
        const e = document.getElementById(id); if (!e) continue;
        const cs = getComputedStyle(e); const r = e.getBoundingClientRect();
        ui[id] = {x:r.x,y:r.y,w:r.width,h:r.height,display:cs.display,opacity:cs.opacity,cls:e.className};
      }
      return {canvas:{w:c.width,h:c.height,rect:{x:cr.x,y:cr.y,w:cr.width,h:cr.height}},
              stage:{rect:{x:sr.x,y:sr.y,w:sr.width,h:sr.height},clientW:s.clientWidth,clientH:s.clientHeight},
              ui, status: document.getElementById('badge').textContent};
    })()`);
    console.log('DOM', JSON.stringify(dom, null, 1));
    const shot = path.join(ART, 'probe_paused_1400x900.png');
    await page.screenshot(shot);
    const buf = Buffer.from(await (await fetch(`http://127.0.0.1:${PORT}/state`)).arrayBuffer());
    const snap = parseSnapshot(buf);
    console.log('SNAP', { frame: snap.frame, balls: snap.ballCount, nuclei: snap.nucleusCount, types: snap.typeCount, cyanoNodes: snap.cyanoNodeCount, links: snap.cyanoLinkCount, avg: snap.cyanoAvgEnergy, world: [snap.worldW, snap.worldH] });
    const geo = makeGeo(snap, dom.canvas.w, dom.canvas.h);
    console.log('GEO', geo.scale, geo.ox, geo.oy);
    const px = makePixels(decodePng(fs.readFileSync(shot)));
    console.log('PNG', px.width, px.height, 'nonBg', px.nonBgRatio([[8,12,20],[5,7,11]]), 'black', px.pureBlackCount());
    const canvasRect = dom.canvas.rect;
    const toDoc = (p) => ({ x: canvasRect.x + p.x * (canvasRect.w / dom.canvas.w), y: canvasRect.y + p.y * (canvasRect.h / dom.canvas.h) });
    const uiRects = Object.entries(dom.ui).filter(([k, v]) => v.display !== 'none' && parseFloat(v.opacity) > 0.05 && v.w > 0).map(([k, v]) => ({ k, x: v.x, y: v.y, w: v.w, h: v.h }));
    const covered = (x, y) => uiRects.some((r) => x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h);
    let packs = [];
    try { packs = await (await fetch(`http://127.0.0.1:${PORT}/packs`)).json(); } catch (e) {}
    const enabled = packs.filter((p) => p.enabled).sort((a, b) => a.order - b.order).map((p) => p.name);
    const pack = enabled.length ? (PACKS[enabled[enabled.length - 1]] || PACKS.none) : PACKS.none;
    console.log('PACKS enabled', enabled, '->', pack && pack.mode);
    // 球
    let ballTotal = 0, ballVis = 0, ballMiss = [];
    for (const b of snap.balls) {
      const sp = geo.pos(b.x, b.y); const d = toDoc(sp);
      if (d.x < canvasRect.x || d.y < canvasRect.y || d.x > canvasRect.x + canvasRect.w || d.y > canvasRect.y + canvasRect.h) continue;
      if (covered(d.x, d.y)) continue;
      ballTotal++;
      const c = px.at(d.x, d.y);
      const exp = expectedBallColor(pack, b);
      if (c && Math.abs(c[0]-exp[0])<=6 && Math.abs(c[1]-exp[1])<=6 && Math.abs(c[2]-exp[2])<=6) ballVis++;
      else if (ballMiss.length < 8) ballMiss.push({d, c, exp, type: b.type, owner: b.owner});
    }
    console.log('BALLS visible', ballVis, '/', ballTotal, ballMiss.slice(0,5));
    // 核
    let nucVis = 0, ringVis = 0, nucMiss = [];
    const NUC = pack ? pack.nucleus : [255,255,255];
    for (const n of snap.nuclei) {
      const sp = geo.pos(n.x, n.y); const d = toDoc(sp);
      if (covered(d.x, d.y)) continue;
      let core = false;
      for (let dy = -2; dy <= 2 && !core; dy++) for (let dx = -2; dx <= 2 && !core; dx++) {
        const c = px.at(d.x + dx, d.y + dy);
        if (c && Math.abs(c[0]-NUC[0])<=14 && Math.abs(c[1]-NUC[1])<=14 && Math.abs(c[2]-NUC[2])<=14) core = true;
      }
      if (core) nucVis++; else nucMiss.push(d);
      const frac = Math.min(1, Math.max(0.15, n.threshold > 0 ? n.energy / n.threshold : 0.15));
      const r = (3 + 5 * frac) * (pack ? pack.nucleusSize : 1);
      let ring = false;
      for (let a = 0; a < 24 && !ring; a++) {
        const ang = a / 24 * Math.PI * 2;
        const c = px.at(d.x + Math.cos(ang) * r, d.y + Math.sin(ang) * r);
        if (!c) continue;
        const mx = Math.max(c[0], c[1], c[2]), mn = Math.min(c[0], c[1], c[2]);
        if (mx >= 80 && (mx - mn) / mx >= 0.35) ring = true;
      }
      if (ring) ringVis++;
    }
    console.log('NUCLEI core', nucVis, '/', snap.nuclei.length, 'ring', ringVis, 'miss', nucMiss.slice(0,5));
    // 蓝细菌节点
    let nodeTotal = 0, nodeVis = 0, nodeMiss = [];
    for (const n of snap.cyanoNodes) {
      if (!(n.flags & 1)) continue;
      const sp = geo.pos(n.x, n.y); const d = toDoc(sp);
      if (covered(d.x, d.y)) continue;
      nodeTotal++;
      const exp = cyanoNodeColor(n.colony);
      const c = px.at(d.x, d.y);
      if (c && Math.abs(c[0]-exp[0])<=14 && Math.abs(c[1]-exp[1])<=14 && Math.abs(c[2]-exp[2])<=14) nodeVis++;
      else if (nodeMiss.length < 8) nodeMiss.push({ d, c, exp, colony: n.colony });
    }
    console.log('CYANO NODES', nodeVis, '/', nodeTotal, nodeMiss.slice(0,5));
    // 连线
    const linkExp = cyanoLinkComposite(pack);
    let linkTotal = 0, linkVis = 0, linkMiss = [];
    for (const [a, b] of snap.cyanoLinks) {
      const na = snap.cyanoNodes[a], nb = snap.cyanoNodes[b];
      if (!na || !nb) continue;
      if (!(na.flags & 1) || !(nb.flags & 1)) continue;
      const pa = geo.pos(na.x, na.y), pb = geo.pos(nb.x, nb.y);
      const dist = Math.hypot(pa.x - pb.x, pa.y - pb.y);
      if (dist < 10) continue;
      const mp = { x: pa.x + (pb.x - pa.x) * 0.5, y: pa.y + (pb.y - pa.y) * 0.5 };
      const d = toDoc(mp);
      if (covered(d.x, d.y)) continue;
      linkTotal++;
      let ok = false;
      for (let oy = -1; oy <= 1 && !ok; oy++) for (let ox = -1; ox <= 1 && !ok; ox++) {
        const c = px.at(d.x + ox, d.y + oy);
        if (c && Math.abs(c[0]-linkExp[0])<=18 && Math.abs(c[1]-linkExp[1])<=18 && Math.abs(c[2]-linkExp[2])<=18) ok = true;
      }
      if (ok) linkVis++; else if (linkMiss.length < 6) linkMiss.push({ d, at: px.at(d.x, d.y), exp: linkExp });
    }
    console.log('CYANO LINKS', linkVis, '/', linkTotal, linkMiss.slice(0,4), 'expected', linkExp);
    // 色簇统计（关闭态误检基线）
    const nodeColors = [...new Set(snap.cyanoNodes.map((n) => JSON.stringify(cyanoNodeColor(n.colony))))].map(JSON.parse);
    for (const nc of nodeColors) console.log('nodeColor', nc, 'count', px.countColor(nc, 6));
    console.log('linkColor count', px.countColor(linkExp, 12));
    console.log('whiteNeutral', px.countBrightNeutral(215, 240, 0.2));
    const errs = await page.collectErrors();
    console.log('ERRORS', JSON.stringify(errs, null, 1));
  } finally {
    try { if (page) await edge.cdp.send('Target.closeTarget', { targetId: page.targetId }); } catch (e) { /* ignore */ }
    killTree(edge.proc.pid);
    edge.cdp.close();
  }
})().catch((e) => { console.error('PROBE FAIL', e); process.exit(1); });
