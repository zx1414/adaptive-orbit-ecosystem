'use strict';
// 栈内 stage 管理：复制 v2.2.0 产物到 _agent/red_visual/stage，起/停 simulator.exe（端口自选，避开他人）。
// 只写 _agent/**（本任务临时目录），不碰产品文件。
const { spawn, spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const net = require('net');

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function repoRoot() {
  return path.resolve(__dirname, '..', '..', '..');
}

const STAGE_REL = path.join('_agent', 'red_visual', 'stage');
const EXE_REL = path.join('_agent', 'integration', 'build', 'simulator.exe');

function resetStage({ root, log } = {}) {
  root = root || repoRoot();
  const stage = path.join(root, STAGE_REL);
  fs.mkdirSync(stage, { recursive: true });
  fs.copyFileSync(path.join(root, EXE_REL), path.join(stage, 'simulator.exe'));
  fs.cpSync(path.join(root, 'web'), path.join(stage, 'web'), { recursive: true });
  fs.cpSync(path.join(root, 'resourcepacks'), path.join(stage, 'resourcepacks'), { recursive: true });
  fs.copyFileSync(path.join(root, 'env_config.txt'), path.join(stage, 'env_config.txt'));
  fs.copyFileSync(path.join(root, 'packs.list'), path.join(stage, 'packs.list'));
  // 清掉之前测试可能生成的存档/CSV，保证每次复跑一致
  for (const p of ['saves', 'mods.list']) {
    try { fs.rmSync(path.join(stage, p), { recursive: true, force: true }); } catch (e) { /* ignore */ }
  }
  if (log) log('stage 重置完成: ' + stage);
  return stage;
}

function portFree(port) {
  return new Promise((resolve) => {
    const srv = net.createServer();
    srv.once('error', () => resolve(false));
    srv.once('listening', () => srv.close(() => resolve(true)));
    srv.listen(port, '127.0.0.1');
  });
}

async function findPort(start) {
  for (let p = start; p < start + 40; p++) {
    if (await portFree(p)) return p;
  }
  throw new Error('no free port from ' + start);
}

class StageServer {
  constructor({ root, port, stage }) {
    this.root = root; this.port = port; this.stage = stage; this.proc = null;
    this.stdout = path.join(stage, 'server.out.log');
    this.stderr = path.join(stage, 'server.err.log');
  }
  get url() { return `http://127.0.0.1:${this.port}`; }
  async start() {
    for (const f of [this.stdout, this.stderr]) fs.writeFileSync(f, '');
    const out = fs.openSync(this.stdout, 'a');
    const err = fs.openSync(this.stderr, 'a');
    this.proc = spawn(path.join(this.stage, 'simulator.exe'),
      ['--serve', '--no-browser', '--port', String(this.port)],
      { cwd: this.stage, stdio: ['ignore', out, err], windowsHide: true });
    this.proc.on('exit', () => { this.exited = true; });
    const t0 = Date.now();
    while (Date.now() - t0 < 20000) {
      try {
        const r = await fetch(this.url + '/', { cache: 'no-store' });
        if (r.ok) return this;
      } catch (e) { /* retry */ }
      await sleep(150);
    }
    throw new Error('simulator 未能在 20s 内监听 ' + this.port + '\n' + this.tailLog());
  }
  tailLog() {
    try {
      return fs.readFileSync(this.stdout, 'utf8').split('\n').slice(-15).join('\n');
    } catch (e) { return ''; }
  }
  async stop() {
    if (!this.proc) return;
    if (this.exited) { this.proc = null; return; }
    try { this.proc.kill(); } catch (e) { /* ignore */ }
    const pid = this.proc.pid;
    for (let i = 0; i < 40 && !this.exited; i++) await sleep(50);
    if (!this.exited) killPidTree(pid);
    await sleep(200);
    this.proc = null;
  }
  async http(method, urlPath, body, headers) {
    const opts = { method, cache: 'no-store' };
    if (body !== undefined) { opts.body = body; opts.headers = headers || { 'Content-Type': 'text/plain' }; }
    const r = await fetch(this.url + urlPath, opts);
    return r;
  }
  async text(urlPath) { return (await this.http('GET', urlPath)).text(); }
  async json(urlPath) { return JSON.parse(await this.text(urlPath)); }
  async run(bodyLines) {
    const body = bodyLines.join('\n') + '\n';
    const r = await this.http('POST', '/run', body);
    if (!r.ok) throw new Error('/run HTTP ' + r.status);
    return body;
  }
  async control(cmd) {
    const r = await this.http('POST', '/control', JSON.stringify({ cmd }), { 'Content-Type': 'application/json' });
    return r.text();
  }
  async envPost(body) {
    const r = await this.http('POST', '/env', body);
    let json = null;
    try { json = await r.json(); } catch (e) { /* ignore */ }
    return { status: r.status, json };
  }
  async status() { return (await this.text('/status')).trim(); }
  async waitStatus(expected, timeoutMs = 12000) {
    const t0 = Date.now();
    while (Date.now() - t0 < timeoutMs) {
      const s = await this.status();
      if (Array.isArray(expected) ? expected.includes(s) : s === expected) return s;
      await sleep(100);
    }
    throw new Error('status 未到 ' + expected + '（当前 ' + (await this.status()) + '）');
  }
  async stateBuffer() {
    const r = await this.http('GET', '/state');
    return Buffer.from(await r.arrayBuffer());
  }
}

function killPidTree(pid) {
  try { spawnSync('taskkill', ['/PID', String(pid), '/T', '/F'], { stdio: 'ignore', windowsHide: true }); }
  catch (e) { /* ignore */ }
}

module.exports = { repoRoot, resetStage, StageServer, findPort, portFree, killPidTree, STAGE_REL, sleep };
