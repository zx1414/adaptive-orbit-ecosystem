'use strict';
const fs = require('fs');
const { decodePng } = require('./png');
const file = process.argv[2];
const img = decodePng(fs.readFileSync(file));
console.log('size', img.width, img.height);
// histogram of exact colors (top 30)
const hist = new Map();
const { rgba, width, height } = img;
for (let i = 0; i < width * height; i++) {
  const r = rgba[i * 4], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2], a = rgba[i * 4 + 3];
  if (a < 250) continue;
  const k = (r << 16) | (g << 8) | b;
  hist.set(k, (hist.get(k) || 0) + 1);
}
const top = [...hist.entries()].sort((a, b) => b[1] - a[1]).slice(0, 25);
for (const [k, n] of top) {
  const r = (k >> 16) & 255, g = (k >> 8) & 255, b = k & 255;
  console.log(`#${r.toString(16).padStart(2, '0')}${g.toString(16).padStart(2, '0')}${b.toString(16).padStart(2, '0')} rgb(${r},${g},${b}) ${n}`);
}
console.log('unique colors', hist.size);
