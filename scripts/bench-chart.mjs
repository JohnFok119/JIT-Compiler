// Reads docs/bench/results.csv, writes docs/bench/chart.svg. No dependencies.
// Run from the repository root: node scripts/bench-chart.mjs
import { readFileSync, writeFileSync } from 'node:fs';

const CSV = 'docs/bench/results.csv';
const SVG = 'docs/bench/chart.svg';
const BACKENDS = [
  ['x86-O2', '#1f2a6b', 'x86 -O2'],
  ['x86-O0', '#8e949e', 'x86 -O0'],
  ['gcc', '#b8262c', 'gcc'],
  ['interp-O2', '#c9a449', 'interp -O2'],
];

const lines = readFileSync(CSV, 'utf8').trim().split(/\r?\n/).slice(1);
const rows = lines.map((l) => {
  const [expr, backend, compile, ns] = l.split(',');
  return { expr, backend, compile: Number(compile), ns: Number(ns) };
});
const exprs = [...new Set(rows.map((r) => r.expr))];
const get = (e, b) => rows.find((r) => r.expr === e && r.backend === b);

const W = 900, LEFT = 70, RIGHT = 90, BAR = 14, GAP = 3, GROUP_GAP = 14;
const PANEL_TOP = 64, GROUP_H = BACKENDS.length * (BAR + GAP) - GAP;
const PANEL_H = PANEL_TOP + exprs.length * (GROUP_H + GROUP_GAP) + 34;
const H = 2 * PANEL_H + 50;
const esc = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;');
const fmt = (v) => (v >= 100 ? v.toFixed(0) : v >= 10 ? v.toFixed(1) : v.toFixed(2));

let out = '';
out += `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}" font-family="Helvetica, Arial, sans-serif">\n`;
out += `<rect width="${W}" height="${H}" rx="10" fill="#ffffff" stroke="#d0d7de"/>\n`;

// legend
BACKENDS.forEach(([, colour, label], i) => {
  const x = LEFT + i * 150;
  out += `<rect x="${x}" y="16" width="14" height="14" fill="${colour}"/><text x="${x + 20}" y="28" font-size="13" fill="#24292f">${esc(label)}</text>\n`;
});

function panel(top, title, unit, key) {
  const vals = rows.map((r) => r[key]).filter((v) => v > 0);
  const lo = Math.pow(10, Math.floor(Math.log10(Math.min(...vals))));
  const hi = Math.pow(10, Math.ceil(Math.log10(Math.max(...vals))));
  const span = Math.log10(hi / lo);
  const plotW = W - LEFT - RIGHT;
  const len = (v) => Math.max(1, (Math.log10(Math.max(v, lo) / lo) / span) * plotW);
  const bottom = top + PANEL_TOP - 18 + exprs.length * (GROUP_H + GROUP_GAP);
  let s = `<text x="${LEFT}" y="${top + 24}" font-size="15" font-weight="bold" fill="#24292f">${esc(title)}</text>\n`;
  for (let d = Math.round(Math.log10(lo)); d <= Math.round(Math.log10(hi)); d++) {
    const x = LEFT + ((d - Math.log10(lo)) / span) * plotW;
    s += `<line x1="${x}" y1="${top + PANEL_TOP - 18}" x2="${x}" y2="${bottom}" stroke="#d0d7de" stroke-width="1"/>`;
    s += `<text x="${x}" y="${bottom + 14}" font-size="11" fill="#57606a" text-anchor="middle">${esc(Math.pow(10, d))}</text>\n`;
  }
  s += `<text x="${LEFT + plotW / 2}" y="${bottom + 28}" font-size="11" fill="#57606a" text-anchor="middle">${esc(unit)} (log scale)</text>\n`;
  exprs.forEach((e, gi) => {
    const y0 = top + PANEL_TOP - 12 + gi * (GROUP_H + GROUP_GAP);
    s += `<text x="${LEFT - 8}" y="${y0 + GROUP_H / 2 + 4}" font-size="13" fill="#24292f" text-anchor="end">${esc(e)}</text>\n`;
    BACKENDS.forEach(([b, colour], bi) => {
      const r = get(e, b);
      if (!r) return;
      const y = y0 + bi * (BAR + GAP);
      const l = len(r[key]);
      s += `<rect x="${LEFT}" y="${y}" width="${l.toFixed(1)}" height="${BAR}" fill="${colour}"/>`;
      s += `<text x="${(LEFT + l + 5).toFixed(1)}" y="${y + BAR - 3}" font-size="11" fill="#24292f">${fmt(r[key])}</text>\n`;
    });
  });
  return s;
}

out += panel(34, 'Compile time (median of 15 compiles)', 'microseconds', 'compile');
out += panel(34 + PANEL_H, 'Time per evaluation (median of 11 runs)', 'nanoseconds', 'ns');
out += '</svg>\n';
writeFileSync(SVG, out);
console.log(`wrote ${SVG}`);
