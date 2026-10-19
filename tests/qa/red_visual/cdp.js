'use strict';
// 极简 CDP 客户端（Node 22+ 内置 WebSocket）：
//  - launchEdge(): 拉起 headless Edge 并连到 browser WebSocket
//  - page: Target.createTarget + attach(flatten) 后得到的页面会话
//  - eval/screenshot/setViewport/click/key 等便利方法
const { spawn, spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

class CDP {
  constructor(wsUrl) {
    this.wsUrl = wsUrl;
    this.ws = null;
    this.nextId = 1;
    this.pending = new Map();
    this.events = [];
    this.eventHandlers = [];
  }
  connect() {
    return new Promise((resolve, reject) => {
      const ws = new WebSocket(this.wsUrl);
      this.ws = ws;
      const timer = setTimeout(() => reject(new Error('WebSocket connect timeout')), 10000);
      ws.addEventListener('open', () => { clearTimeout(timer); resolve(this); });
      ws.addEventListener('error', (e) => { clearTimeout(timer); reject(new Error('WebSocket error')); });
      ws.addEventListener('message', (ev) => {
        let msg;
        try { msg = JSON.parse(typeof ev.data === 'string' ? ev.data : Buffer.from(ev.data).toString('utf8')); }
        catch (e) { return; }
        if (msg.id !== undefined && this.pending.has(msg.id)) {
          const p = this.pending.get(msg.id);
          this.pending.delete(msg.id);
          if (msg.error) p.reject(new Error(`${p.method}: ${msg.error.message || JSON.stringify(msg.error)}`));
          else p.resolve(msg.result);
          return;
        }
        this.events.push(msg);
        for (const h of this.eventHandlers) { try { h(msg); } catch (e) { /* ignore */ } }
      });
      ws.addEventListener('close', () => {
        for (const p of this.pending.values()) p.reject(new Error('CDP closed'));
        this.pending.clear();
      });
    });
  }
  send(method, params = {}, sessionId) {
    const id = this.nextId++;
    const msg = { id, method, params };
    if (sessionId) msg.sessionId = sessionId;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject, method });
      try { this.ws.send(JSON.stringify(msg)); }
      catch (e) { this.pending.delete(id); reject(e); }
    });
  }
  onEvent(fn) { this.eventHandlers.push(fn); }
  close() { try { this.ws.close(); } catch (e) { /* ignore */ } }
}

async function waitHttp(url, timeoutMs = 15000) {
  const t0 = Date.now();
  while (Date.now() - t0 < timeoutMs) {
    try {
      const r = await fetch(url, { method: 'GET' });
      if (r.ok) return r;
    } catch (e) { /* retry */ }
    await sleep(120);
  }
  throw new Error('HTTP not ready: ' + url);
}

let edgeSeq = 0;
async function launchEdge({ edgePath, port, profileDir, windowSize, extraArgs = [] }) {
  fs.mkdirSync(profileDir, { recursive: true });
  const size = windowSize || { width: 1600, height: 1000 };
  const args = [
    '--headless=new', '--disable-gpu', '--no-first-run', '--no-default-browser-check',
    '--hide-scrollbars', '--disable-extensions', '--disable-sync', '--disable-background-networking',
    '--disable-component-update', '--disable-default-apps', '--mute-audio',
    '--remote-allow-origins=*',
    `--remote-debugging-port=${port}`,
    `--user-data-dir=${profileDir}`,
    `--window-size=${size.width},${size.height}`,
    'about:blank',
    ...extraArgs,
  ];
  const proc = spawn(edgePath, args, { stdio: 'ignore', windowsHide: true });
  const version = await waitHttp(`http://127.0.0.1:${port}/json/version`).then((r) => r.json());
  const cdp = new CDP(version.webSocketDebuggerUrl);
  await cdp.connect();
  return { proc, cdp, version, port };
}

function killTree(pid) {
  try { spawnSync('taskkill', ['/PID', String(pid), '/T', '/F'], { stdio: 'ignore', windowsHide: true }); }
  catch (e) { /* ignore */ }
}

const INIT_SCRIPT = `
window.__errs = [];
window.addEventListener('error', function (e) {
  try { window.__errs.push('error: ' + (e.message || '') + ' @' + (e.filename || '') + ':' + (e.lineno || 0)); } catch (x) {}
});
window.addEventListener('unhandledrejection', function (e) {
  try { window.__errs.push('unhandledrejection: ' + String((e.reason && (e.reason.stack || e.reason.message)) || e.reason)); } catch (x) {}
});
`;

async function createPage(browserCdp, url) {
  const { targetId } = await browserCdp.send('Target.createTarget', { url: 'about:blank' });
  const { sessionId } = await browserCdp.send('Target.attachToTarget', { targetId, flatten: true });
  const page = new PageClient(browserCdp, sessionId, targetId);
  await page.init();
  if (url) await page.navigate(url);
  return page;
}

class PageClient {
  constructor(cdp, sessionId, targetId) {
    this.cdp = cdp; this.sessionId = sessionId; this.targetId = targetId;
    this.errors = []; this.logErrors = [];
  }
  send(method, params = {}) { return this.cdp.send(method, params, this.sessionId); }
  async init() {
    await this.send('Page.enable');
    await this.send('Runtime.enable');
    await this.send('Log.enable');
    await this.send('Page.addScriptToEvaluateOnNewDocument', { source: INIT_SCRIPT });
    this.cdp.onEvent((msg) => {
      if (msg.sessionId !== this.sessionId) return;
      if (msg.method === 'Runtime.exceptionThrown') {
        const d = msg.params.exceptionDetails || {};
        this.errors.push('exceptionThrown: ' + (d.text || '') + ' ' + ((d.exception && d.exception.description) || ''));
      } else if (msg.method === 'Log.entryAdded') {
        const e = msg.params.entry || {};
        if (e.level === 'error') this.logErrors.push(e.source + ': ' + (e.text || ''));
      }
    });
  }
  async navigate(url) {
    this._lastUrl = url;
    await this.send('Page.navigate', { url });
    await this.waitFor(`document.readyState === 'complete' && !!document.getElementById('view')`, 15000);
    // 等应用启动（renderLoop 已跑过、env 已取到、packs 已应用）
    await this.waitFor(`!!window.__simDebug && document.querySelectorAll('#qb-axes .qb-axis').length === 4`, 15000);
    await sleep(350);
  }
  async reload() {
    await this.send('Page.reload', { ignoreCache: true });
    await this.waitFor(`document.readyState === 'complete' && !!window.__simDebug`, 15000);
    await this.waitFor(`document.querySelectorAll('#qb-axes .qb-axis').length === 4`, 15000);
    await sleep(350);
  }
  async waitFor(expression, timeoutMs = 5000, intervalMs = 60) {
    const t0 = Date.now();
    let lastErr = null;
    while (Date.now() - t0 < timeoutMs) {
      try {
        const v = await this.eval(expression);
        if (v) return v;
      } catch (e) { lastErr = e; }
      await sleep(intervalMs);
    }
    throw new Error('waitFor timeout: ' + expression + (lastErr ? ' (' + lastErr.message + ')' : ''));
  }
  async eval(expression, opts = {}) {
    const r = await this.send('Runtime.evaluate', {
      expression, returnByValue: true, awaitPromise: true,
      userGesture: opts.userGesture !== false, includeCommandLineAPI: false,
    });
    if (r.exceptionDetails) {
      const d = r.exceptionDetails;
      throw new Error('JS exception: ' + (d.text || '') + ' ' + ((d.exception && d.exception.description) || ''));
    }
    return r.result ? r.result.value : undefined;
  }
  async setViewport(width, height) {
    await this.send('Emulation.setDeviceMetricsOverride', {
      width, height, deviceScaleFactor: 1, mobile: false,
      screenWidth: width, screenHeight: height,
    });
    await sleep(450); // resize 防抖 120ms + 重绘
    await this.eval('new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)))');
    await sleep(120);
  }
  async screenshot(file) {
    const r = await this.send('Page.captureScreenshot', { format: 'png', fromSurface: true, captureBeyondViewport: false });
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, Buffer.from(r.data, 'base64'));
    return file;
  }
  async click(selector) {
    const ok = await this.eval(`(() => { const e = document.querySelector(${JSON.stringify(selector)}); if (!e) return 'missing'; e.click(); return 'ok'; })()`);
    if (ok !== 'ok') throw new Error('click target missing: ' + selector);
    await sleep(180);
  }
  async mouseClick(x, y) {
    await this.send('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', clickCount: 1 });
    await this.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'left', clickCount: 1 });
    await sleep(150);
  }
  async key(keyName, code, keyCode, text) {
    const base = { key: keyName, code: code || undefined, windowsVirtualKeyCode: keyCode, nativeVirtualKeyCode: keyCode };
    await this.send('Input.dispatchKeyEvent', { type: 'keyDown', ...base, text: text || undefined });
    await this.send('Input.dispatchKeyEvent', { type: 'keyUp', ...base });
    await sleep(160);
  }
  async getRect(selector) {
    return this.eval(`(() => { const e = document.querySelector(${JSON.stringify(selector)}); if (!e) return null;
      const r = e.getBoundingClientRect(); return {x:r.x,y:r.y,w:r.width,h:r.height}; })()`);
  }
  async collectErrors() {
    let winErrs = [];
    try { winErrs = await this.eval('window.__errs || []'); } catch (e) { /* ignore */ }
    return { windowErrors: winErrs, exceptions: this.errors.slice(), logErrors: this.logErrors.slice() };
  }
}

module.exports = { CDP, launchEdge, createPage, killTree, sleep, waitHttp };
