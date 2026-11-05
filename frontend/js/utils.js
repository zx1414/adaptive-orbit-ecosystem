/* agentsim 前端工具集 — DOM/格式化/颜色/转义 · 零依赖
 * 全部为纯函数或薄封装，供各模块复用；不持有任何状态。
 */
window.Utils = (function () {
  'use strict';

  function $(id) { return document.getElementById(id); }

  function clamp(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }

  function fmt(n, d) {
    if (typeof n !== 'number' || !isFinite(n)) return '—';
    if (d == null) d = Math.abs(n) >= 100 ? 0 : 1;
    return n.toFixed(d);
  }

  // 轻量 DOM 构建：el('div', 'card glass', '文字')
  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text != null) e.textContent = text;
    return e;
  }

  function hsl(h, s, l, a) {
    h = ((h % 360) + 360) % 360;
    return (a == null)
      ? 'hsl(' + h.toFixed(1) + ',' + s + '%,' + l + '%)'
      : 'hsla(' + h.toFixed(1) + ',' + s + '%,' + l + '%,' + a + ')';
  }

  function esc(s) {
    return String(s == null ? '' : s)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
  }

  // 存档时间戳：2026-11-01 01:23:45
  function nowStamp() {
    var d = new Date();
    function p2(v) { return (v < 10 ? '0' : '') + v; }
    return d.getFullYear() + '-' + p2(d.getMonth() + 1) + '-' + p2(d.getDate()) +
      ' ' + p2(d.getHours()) + ':' + p2(d.getMinutes()) + ':' + p2(d.getSeconds());
  }

  function uid() {
    return 'u' + Date.now().toString(36) + Math.floor(Math.random() * 1e6).toString(36);
  }

  // 简易 debounce（搜索框/滑杆联动用）
  function debounce(fn, ms) {
    var t = 0;
    return function () {
      var args = arguments, self = this;
      clearTimeout(t);
      t = setTimeout(function () { fn.apply(self, args); }, ms);
    };
  }

  return { $: $, clamp: clamp, fmt: fmt, el: el, hsl: hsl, esc: esc, nowStamp: nowStamp, uid: uid, debounce: debounce };
})();
