#!/usr/bin/env node
// Bundle EcoScope into one self-contained HTML (double-click to open).
// Usage: node frontend/tools/bundle.mjs
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const fe = path.resolve(here, '..');
const html = fs.readFileSync(path.join(fe, 'index.html'), 'utf8');

let out = html.replace(
  /[ \t]*<link rel="stylesheet" href="assets\/styles.css">/,
  () => '<style>\n' + fs.readFileSync(path.join(fe, 'assets/styles.css'), 'utf8') + '\n</style>'
);

out = out.replace(/<script src="(js\/[^"]+)"><\/script>/g, (m, src) => {
  const code = fs.readFileSync(path.join(fe, src), 'utf8').replace(/<\/script/gi, '<\\/script');
  return '<script>\n' + code + '\n</script>';
});

if (/src="|stylesheet" href="/.test(out)) {
  console.error('bundle failed: unresolved external references remain');
  process.exit(1);
}
const dest = path.join(fe, 'EcoScope.html');
fs.writeFileSync(dest, out, 'utf8');
console.log('wrote', dest, (fs.statSync(dest).size / 1024).toFixed(1) + ' KiB');
