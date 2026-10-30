#include "app/HtmlTraceWriter.h"

#include <fstream>
#include <utility>

namespace app {
namespace {
const char* const kHead = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>EcoCore v3 trace</title>
<style>
:root { color-scheme: dark; }
* { box-sizing: border-box; }
body { margin: 0; background: #070b14; color: #d7e0f2; font: 14px/1.4 "Segoe UI", system-ui, sans-serif; }
.wrap { display: flex; gap: 18px; padding: 18px; align-items: flex-start; }
canvas { width: min(900px, 78vw); height: min(900px, 78vw); background: #0b1020; border: 1px solid #23304d; border-radius: 10px; }
.side { min-width: 220px; }
h1 { font-size: 18px; margin: 0 0 10px; letter-spacing: .5px; }
#stats { white-space: pre; font-variant-numeric: tabular-nums; color: #9fb4d8; margin-bottom: 12px; }
input[type=range] { width: 100%; }
button { background: #1d2b4a; color: #e6eeff; border: 1px solid #35507f; border-radius: 7px; padding: 7px 14px; cursor: pointer; margin-top: 10px; }
button:hover { background: #27406b; }
.hint { color: #6f86ab; margin-top: 12px; font-size: 12px; }
</style></head><body>
<div class="wrap">
  <canvas id="c" width="900" height="900"></canvas>
  <div class="side">
    <h1>EcoCore v3</h1>
    <div id="stats"></div>
    <input id="slider" type="range" min="0" max="0" value="0">
    <button id="play">Pause</button>
    <div class="hint">Green: producers. Orange: agents.<br>Brightness scales with energy / capacity.</div>
  </div>
</div>
<script>
const TRACE = )HTML";
const char* const kTail = R"HTML(;
const canvas = document.getElementById('c');
const ctx = canvas.getContext('2d');
const slider = document.getElementById('slider');
const play = document.getElementById('play');
const stats = document.getElementById('stats');
const b = TRACE.bounds;
const worldW = Math.max(1e-9, b.maxX - b.minX);
const worldH = Math.max(1e-9, b.maxY - b.minY);
const scaleX = canvas.width / worldW;
const scaleY = canvas.height / worldH;
let index = 0;
let timer = null;
function px(x) { return (x - b.minX) * scaleX; }
function py(y) { return canvas.height - (y - b.minY) * scaleY; }
function ratio(value, cap) { return Math.max(0, Math.min(1, cap > 0 ? value / cap : 0)); }
function draw() {
  const frame = TRACE.frames[index];
  ctx.fillStyle = '#0b1020';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  if (!frame) { stats.textContent = 'No frames'; return; }
  for (const p of frame.p) {
    const r = ratio(p[2], p[3]);
    ctx.beginPath();
    ctx.fillStyle = 'rgba(72, 220, 128, ' + (0.28 + 0.72 * r) + ')';
    ctx.arc(px(p[0]), py(p[1]), 2 + 5 * r, 0, Math.PI * 2);
    ctx.fill();
  }
  for (const a of frame.a) {
    const r = ratio(a[2], a[3]);
    ctx.beginPath();
    ctx.fillStyle = 'rgba(255, 166, 66, ' + (0.35 + 0.65 * r) + ')';
    ctx.arc(px(a[0]), py(a[1]), 2 + 4 * r, 0, Math.PI * 2);
    ctx.fill();
  }
  stats.textContent =
    'tick    ' + frame.tick + '\n' +
    'producers ' + frame.producers + '\n' +
    'agents    ' + frame.agents + '\n' +
    'energy    ' + frame.totalEnergy.toFixed(2) + '\n' +
    'frame     ' + (index + 1) + ' / ' + TRACE.frames.length;
}
function show(next) {
  index = Math.max(0, Math.min(TRACE.frames.length - 1, next));
  slider.value = String(index);
  draw();
}
function stop() {
  if (timer !== null) { clearInterval(timer); timer = null; }
  play.textContent = 'Play';
}
play.addEventListener('click', () => {
  if (timer !== null) { stop(); return; }
  play.textContent = 'Pause';
  timer = setInterval(() => {
    if (index >= TRACE.frames.length - 1) { stop(); return; }
    show(index + 1);
  }, 80);
});
slider.addEventListener('input', () => { stop(); show(Number(slider.value)); });
if (TRACE.frames.length > 0) {
  slider.max = String(TRACE.frames.length - 1);
  show(0);
  play.textContent = 'Play';
} else {
  stats.textContent = 'No frames captured';
}
</script></body></html>
)HTML";
} // namespace

HtmlTraceWriter::HtmlTraceWriter(std::string path) : path_(std::move(path)) {
    // Fail fast instead of discovering an unwritable destination after a long run.
    std::ofstream probe(path_, std::ios::binary | std::ios::trunc);
    if (!probe) {
        error_ = "cannot open html trace for writing: " + path_;
        ok_ = false;
        return;
    }
    probe.flush();
    if (!probe) {
        error_ = "failed while probing html trace: " + path_;
        ok_ = false;
    }
}

void HtmlTraceWriter::capture(const world::WorldView& view) { buffer_.capture(view); }

bool HtmlTraceWriter::write() {
    if (!ok_) return false;
    const std::string json = buffer_.toJson(std::string(), std::string());
    std::string html;
    html.reserve(json.size() + 8192);
    html += kHead;
    html += json;
    html += kTail;

    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    if (!out) { error_ = "cannot open html trace for writing: " + path_; return false; }
    out.write(html.data(), static_cast<std::streamsize>(html.size()));
    out.flush();
    if (!out) { error_ = "failed while writing html trace: " + path_; return false; }
    error_.clear();
    return true;
}
} // namespace app
