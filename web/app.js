// URL parameters (all optional):
//   ?expr=<formula>  fill the input with this formula (URL-encoded); default 0.5*x^2 + sin(2*x)
//   ?drop=1          press "Find the lowest point" once, after the first graph is drawn
//   ?x0=<number>     start the descent at this x (clamped to -4..4) instead of a random x
//   ?race=1          open "Under the hood" and run the speed test once: after the descent
//                    settles when drop=1, else right after the first compile
//   ?open=1          open "Under the hood" (so does the #under-the-hood fragment)
//
// Demo page: compile a formula to WebAssembly in the browser, graph f and f',
// show its derivative and value, roll a gradient-descent ball to the lowest
// point, and expose the engine's stages and a speed test behind "Under the
// hood". No network calls, no dependencies.
import createEngine from './engine.mjs';
import { mathImports } from './imports.mjs';

const $ = (id) => document.getElementById(id);
const exprEl = $('expr'), errBox = $('err'), errMsg = $('errMsg'), errCaret = $('errCaret');
const codeEl = $('code'), capEl = $('caption'), tabsEl = $('tabs'), canvas = $('plot');
const dropBtn = $('dropBtn'), dropInfo = $('dropInfo'), raceBtn = $('raceBtn'), raceOut = $('raceOut');
const atEl = $('at'), hood = $('under-the-hood');

const XMIN = -4, XMAX = 4;
const RACE_FALLBACK = '((3*x - 2)*x + 5)*x - 1';

const STAGES = [
  { id: 'steps', label: 'Steps', stage: 'ir_opt', cap: "The formula as a flat list of simple steps, after clean-up: repeated work is merged and constants are folded. This is the compiler's middle language." },
  { id: 'x86', label: 'Machine code (x86-64)', stage: 'asm', cap: "The x86-64 machine code this engine writes for a laptop processor. Your browser can't run it, so this page runs the WebAssembly version." },
  { id: 'wasm', label: 'WebAssembly', stage: 'wasm', cap: 'The WebAssembly module generated for this formula, as hexadecimal bytes. This is what your browser actually runs.' },
  { id: 'tokens', label: 'Tokens', stage: 'tokens', cap: 'The formula chopped into its smallest pieces: numbers, names and symbols.' },
  { id: 'tree', label: 'Tree', stage: 'ast', cap: 'How the pieces fit together, like a family tree. Multiplication binds tighter than addition, and so on.' },
];

let M, api;
let cur = null;            // current compiled formula
let tab = 'steps';
let stageCache = {};
let ball = null;           // running gradient descent
let ballTimer = 0;
let sliderVals = {};       // by variable name
let atX = 1;               // the "At x =" value
let dropOnLoad = false;
let startX = null;         // from ?x0=
let raceOnLoad = false;    // from ?race=1; cleared once the race has run

const minus = (s) => s.replace(/-/g, '−');
const fmt = (v, d) => (Number.isFinite(v) ? minus(v.toFixed(d)) : 'n/a');
const fmtSig = (v) => (Number.isFinite(v) ? minus(String(Number(v.toPrecision(6)))) : 'not a number');

// ---------- engine ----------
async function boot() {
  M = await createEngine();
  api = {
    load: M.cwrap('jitw_load', 'number', ['string', 'number']),
    errorJson: M.cwrap('jitw_error_json', 'string', []),
    stage: M.cwrap('jitw_stage', 'string', ['number', 'string']),
    nvars: M.cwrap('jitw_nvars', 'number', ['number']),
    varName: M.cwrap('jitw_var_name', 'string', ['number', 'number']),
    wasmPtr: M.cwrap('jitw_wasm', 'number', ['number', 'number']),
    wasmLen: M.cwrap('jitw_wasm_len', 'number', ['number', 'number']),
    setVar: M.cwrap('jitw_set_var', null, ['number', 'number', 'number']),
    interp: M.cwrap('jitw_interp', 'number', ['number', 'number']),
    free: M.cwrap('jitw_free', null, ['number']),
    interpSum: M.cwrap('jitw_interp_sum', 'number', ['number', 'number', 'number', 'number']),
    malloc: M.cwrap('malloc', 'number', ['number']),
    free_: M.cwrap('free', null, ['number']),
  };
}

function instantiate(h, which) {
  const ptr = api.wasmPtr(h, which);
  const len = api.wasmLen(h, which);
  if (!ptr || !len) throw new Error(JSON.parse(api.errorJson()).msg || 'could not generate WebAssembly');
  const bytes = M.HEAPU8.slice(ptr, ptr + len);   // copy after the calls: the heap view can be replaced
  return new WebAssembly.Instance(new WebAssembly.Module(bytes), mathImports).exports.f;
}

function showError(src, json) {
  let e;
  try { e = JSON.parse(json); } catch { e = { pos: -1, msg: 'unknown error' }; }
  errBox.hidden = false;
  errMsg.textContent = "This formula can't be read: " + e.msg + '.' + (cur ? ' The graph still shows the last working formula.' : '');
  errCaret.textContent = e.pos >= 0 ? src + '\n' + ' '.repeat(e.pos) + '^' : '';
  errCaret.hidden = e.pos < 0;
}

function clearError() {
  errBox.hidden = true;
  errMsg.textContent = '';
  errCaret.textContent = '';
}

function compile(src) {
  if (!api) return false;
  stopBall();
  const t0 = performance.now();
  const h = api.load(src, 2);
  const ms = performance.now() - t0;
  if (!h) {
    showError(src, api.errorJson());
    return false;                     // keep showing the last good formula
  }
  let f, fp;
  try {
    f = instantiate(h, 0);
    fp = instantiate(h, 1);
  } catch (e) {
    api.free(h);
    errBox.hidden = false;
    errMsg.textContent = String(e.message || e);
    errCaret.textContent = '';
    errCaret.hidden = true;
    return false;
  }
  clearError();
  if (cur) api.free(cur.h);
  const n = api.nvars(h);
  const names = [];
  for (let i = 0; i < n; i++) names.push(api.varName(h, i));
  cur = { h, src, f, fp, n, names, ms, vals: names.map((nm) => (nm in sliderVals ? sliderVals[nm] : 1)) };
  stageCache = {};
  sampled = null;
  buildSliders();
  renderStage();
  renderResults();
  raceOut.textContent = '';
  dropInfo.textContent = '';
  ball = null;
  draw();
  return true;
}

// ---------- evaluation ----------
function args(x) {
  const a = cur.vals.slice();
  if (cur.n > 0) a[0] = x;
  return a;
}
const F = (x) => cur.f(...args(x));
const D = (x) => cur.fp(...args(x));
const xName = () => (cur.n > 0 ? cur.names[0] : 'x');

// ---------- results list ----------
function stageText(id) {
  const s = STAGES.find((t) => t.id === id);
  if (!(id in stageCache)) {
    const t = api.stage(cur.h, s.stage);
    stageCache[id] = t === null || t === undefined || t === ''
      ? '(' + (JSON.parse(api.errorJson()).msg || 'nothing to show') + ')'
      : t;
  }
  return stageCache[id];
}

function renderResults() {
  const x = xName();
  $('atLabel').textContent = 'At ' + x + ' =';
  const d = api.stage(cur.h, 'diff');
  $('deriv').textContent = d === null || d === undefined || d === '' ? 'not available' : d.trim();
  const asm = stageText('x86');
  const lines = asm.split('\n');               // instruction lines first, then a "; constant pool" section
  const end = lines.findIndex((l) => l.startsWith(';'));
  const insns = lines.slice(0, end < 0 ? lines.length : end).filter((l) => /^[0-9a-f]{4}\s/.test(l)).length;
  const bytes = api.wasmLen(cur.h, 0);
  $('compiled').textContent = insns + ' x86-64 instructions · ' + bytes + ' bytes of WebAssembly · compiled in ' +
    (cur.ms < 10 ? cur.ms.toFixed(1) : cur.ms.toFixed(0)) + ' ms · checked against the reference interpreter';
  renderValues();
}

function renderValues() {
  if (!cur) return;
  for (let i = 0; i < cur.n; i++) api.setVar(cur.h, i, i === 0 ? atX : cur.vals[i]);
  $('fval').textContent = 'f = ' + fmtSig(api.interp(cur.h, 0));
  $('dval').textContent = 'f′ = ' + fmtSig(api.interp(cur.h, 1));
}

// ---------- stage tabs ----------
function buildTabs() {
  tabsEl.textContent = '';
  for (const s of STAGES) {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'tab';
    b.id = 'tab-' + s.id;
    b.setAttribute('role', 'tab');
    b.setAttribute('aria-controls', 'code');
    b.textContent = s.label;
    b.addEventListener('click', () => selectTab(s.id));
    b.addEventListener('keydown', (e) => {
      const i = STAGES.findIndex((t) => t.id === tab);
      let j = -1;
      if (e.key === 'ArrowRight') j = (i + 1) % STAGES.length;
      else if (e.key === 'ArrowLeft') j = (i + STAGES.length - 1) % STAGES.length;
      if (j >= 0) { e.preventDefault(); selectTab(STAGES[j].id); $('tab-' + STAGES[j].id).focus(); }
    });
    tabsEl.appendChild(b);
  }
}

function selectTab(id) {
  tab = id;
  renderStage();
}

function renderStage() {
  for (const s of STAGES) {
    const b = $('tab-' + s.id);
    b.setAttribute('aria-selected', String(s.id === tab));
    b.tabIndex = s.id === tab ? 0 : -1;
  }
  const s = STAGES.find((t) => t.id === tab);
  capEl.textContent = s.cap;
  codeEl.setAttribute('aria-labelledby', 'tab-' + tab);
  codeEl.classList.toggle('wrap', tab === 'wasm');
  codeEl.textContent = cur ? stageText(tab) : '';
}

// ---------- sliders (one row per variable after the first) ----------
function buildSliders() {
  for (const r of document.querySelectorAll('.sliderrow')) r.remove();
  const before = $('compiledRow');
  for (let i = 1; i < cur.n; i++) {
    const nm = cur.names[i];
    const row = document.createElement('div');
    row.className = 'row sliderrow';
    const dt = document.createElement('dt');
    const lab = document.createElement('label');
    lab.htmlFor = 'slider-' + i;
    lab.textContent = nm + ' =';
    dt.appendChild(lab);
    const dd = document.createElement('dd');
    const inp = document.createElement('input');
    inp.id = 'slider-' + i;
    inp.type = 'range'; inp.min = '-3'; inp.max = '3'; inp.step = '0.05';
    inp.value = String(cur.vals[i]);
    const out = document.createElement('span');
    out.className = 'mono sliderval';
    out.textContent = fmt(cur.vals[i], 2);
    inp.addEventListener('input', () => {
      cur.vals[i] = parseFloat(inp.value);
      sliderVals[nm] = cur.vals[i];
      out.textContent = fmt(cur.vals[i], 2);
      sampled = null;
      stopBall(); ball = null; dropInfo.textContent = '';
      renderValues();
      draw();
    });
    dd.append(inp, out);
    row.append(dt, dd);
    before.parentNode.insertBefore(row, before);
  }
}

// ---------- graph ----------
const css = (v) => getComputedStyle(document.documentElement).getPropertyValue(v).trim();
let view = null;

let sampled = null;       // cached samples; reset when the formula or a slider changes
function sample() {
  if (sampled) return sampled;
  const N = 400, xs = [], fy = [], dy = [];
  for (let i = 0; i <= N; i++) {
    const x = XMIN + (XMAX - XMIN) * i / N;
    xs.push(x); fy.push(F(x)); dy.push(D(x));
  }
  return (sampled = { xs, fy, dy });
}

function yRange(fy, dy) {
  const v = fy.concat(dy).filter(Number.isFinite).sort((a, b) => a - b);
  if (v.length === 0) return [-1, 1];
  // clip the extreme 2% each side so poles and blow-ups do not flatten the rest
  let lo = v[Math.floor(v.length * 0.02)], hi = v[Math.ceil(v.length * 0.98) - 1];
  if (!(hi > lo)) { lo -= 1; hi += 1; }
  const pad = (hi - lo) * 0.1;
  return [lo - pad, hi + pad];
}

// 1, 2 or 5 times a power of ten, giving about `want` ticks over the span
function niceStep(span, want) {
  const raw = span / want, p = Math.pow(10, Math.floor(Math.log10(raw))), m = raw / p;
  return (m < 1.5 ? 1 : m < 3.5 ? 2 : m < 7.5 ? 5 : 10) * p;
}

function tickLabel(v, step) {
  const dec = step >= 1 ? 0 : Math.min(6, Math.ceil(-Math.log10(step)));
  return minus((Math.abs(v) < step * 1e-9 ? 0 : v).toFixed(dec));
}

function draw() {
  if (!cur) return;
  const dpr = window.devicePixelRatio || 1;
  const w = canvas.clientWidth || 300;
  const h = Math.round(w * 0.75);
  canvas.style.height = h + 'px';
  canvas.width = Math.round(w * dpr);
  canvas.height = Math.round(h * dpr);
  const c = canvas.getContext('2d');
  c.setTransform(dpr, 0, 0, dpr, 0, 0);
  c.clearRect(0, 0, w, h);

  const { xs, fy, dy } = sample();
  const [ylo, yhi] = yRange(fy, dy);
  const padL = 48, padR = 16, padT = 16, padB = 32;
  const sx = (x) => padL + (x - XMIN) / (XMAX - XMIN) * (w - padL - padR);
  const sy = (y) => padT + (yhi - y) / (yhi - ylo) * (h - padT - padB);
  view = { sx, sy, ylo, yhi, h };

  const navy = css('--navy'), gold = css('--gold'), red = css('--red'), lo = css('--lo'), muted = css('--muted');
  const mono = css('--mono');

  // light grid, axes, tick labels on both axes
  c.font = '14px ' + mono; c.fillStyle = muted;
  c.strokeStyle = lo; c.lineWidth = 1;
  c.beginPath();
  for (let x = XMIN; x <= XMAX; x++) { c.moveTo(Math.round(sx(x)) + 0.5, padT); c.lineTo(Math.round(sx(x)) + 0.5, h - padB); }
  const ystep = niceStep(yhi - ylo, 6);
  const yticks = [];
  for (let v = Math.ceil(ylo / ystep) * ystep; v <= yhi + ystep * 1e-9; v += ystep) yticks.push(v);
  for (const v of yticks) { const py = Math.round(sy(v)) + 0.5; c.moveTo(padL, py); c.lineTo(w - padR, py); }
  c.stroke();
  c.textAlign = 'center'; c.textBaseline = 'top';
  for (let x = XMIN; x <= XMAX; x++) c.fillText(minus(String(x)), sx(x), h - padB + 8);
  c.textAlign = 'right'; c.textBaseline = 'middle';
  for (const v of yticks) c.fillText(tickLabel(v, ystep), padL - 8, sy(v));
  c.strokeStyle = muted; c.lineWidth = 1;
  c.beginPath();
  if (ylo < 0 && yhi > 0) { c.moveTo(padL, Math.round(sy(0)) + 0.5); c.lineTo(w - padR, Math.round(sy(0)) + 0.5); }
  c.moveTo(Math.round(sx(0)) + 0.5, padT); c.lineTo(Math.round(sx(0)) + 0.5, h - padB);
  c.stroke();

  const line = (ys, color, width, dash) => {
    c.strokeStyle = color; c.lineWidth = width; c.setLineDash(dash);
    c.beginPath();
    let pen = false;
    for (let i = 0; i < xs.length; i++) {
      const y = ys[i];
      if (!Number.isFinite(y) || y < ylo - (yhi - ylo) || y > yhi + (yhi - ylo)) { pen = false; continue; }
      if (pen) c.lineTo(sx(xs[i]), sy(y)); else { c.moveTo(sx(xs[i]), sy(y)); pen = true; }
    }
    c.stroke(); c.setLineDash([]);
  };
  c.save();
  c.beginPath(); c.rect(padL, padT, w - padL - padR, h - padT - padB); c.clip();
  line(dy, gold, 1.5, [6, 4]);
  line(fy, navy, 2, []);
  c.restore();

  // ball and trail
  if (ball) {
    const pt = (x) => {
      const y = F(x);
      return [sx(x), sy(Math.min(Math.max(Number.isFinite(y) ? y : ylo, ylo), yhi))];
    };
    c.strokeStyle = red; c.lineWidth = 1; c.setLineDash([2, 4]);
    c.beginPath();
    ball.trail.forEach((x, i) => { const [px, py] = pt(x); if (i) c.lineTo(px, py); else c.moveTo(px, py); });
    c.stroke(); c.setLineDash([]);
    c.fillStyle = red;
    ball.trail.slice(0, -1).forEach((x) => { const [px, py] = pt(x); c.beginPath(); c.arc(px, py, 2, 0, 6.2832); c.fill(); });
    const [bx, by] = pt(ball.x);
    c.beginPath(); c.arc(bx, by, 6, 0, 6.2832); c.fill();
  }
  const others = cur.n > 1 ? ' with ' + cur.names.slice(1).map((nm, k) => nm + ' = ' + cur.vals[k + 1].toFixed(2)).join(', ') : '';
  canvas.setAttribute('aria-label',
    'Graph of f(' + xName() + ') = ' + cur.src + ' as a solid line and its slope as a dashed line, x from ' + XMIN + ' to ' + XMAX + others + '.');
}

// ---------- gradient descent ----------
function stopBall() {
  if (ballTimer) { clearInterval(ballTimer); ballTimer = 0; }
  dropBtn.disabled = false;
}

function findLowest() {
  if (!cur) return;
  stopBall();
  ball = { x: startX !== null ? Math.min(XMAX, Math.max(XMIN, startX)) : XMIN + 0.5 + Math.random() * (XMAX - XMIN - 1), lr: 0.1, step: 0, trail: [], done: false };
  ball.trail.push(ball.x);
  dropBtn.disabled = true;
  ballTimer = setInterval(stepBall, 1000 / 30);
  report();
  draw();
}

function report() {
  const f = F(ball.x);
  const here = xName() + ' = ' + fmt(ball.x, 3) + ' (f = ' + fmt(f, 3) + ')';
  const steps = ball.step + (ball.step === 1 ? ' step' : ' steps');
  if (!ball.done) { dropInfo.textContent = 'Searching: ' + here + ' after ' + steps + '.'; return; }
  dropInfo.textContent = ball.settled
    ? 'Lowest point near ' + here + ' after ' + steps + '. This is gradient descent, the method neural networks use to learn.'
    : 'Stopped near ' + here + ' after ' + steps + ': ' + ball.why + '. This is gradient descent, the method neural networks use to learn.';
}

function stepBall() {
  const g = D(ball.x);
  const f0 = F(ball.x);
  if (!Number.isFinite(g) || !Number.isFinite(f0)) return finish('the slope is not a number here');
  if (Math.abs(g) < 1e-6) return finish('', true);
  let improved = false;
  // halve the step size until f no longer goes up
  for (let k = 0; k < 40; k++) {
    const nx = Math.min(XMAX, Math.max(XMIN, ball.x - ball.lr * g));
    const f1 = F(nx);
    if (Number.isFinite(f1) && f1 <= f0) {
      if (nx !== ball.x) { improved = true; ball.x = nx; }
      break;
    }
    ball.lr /= 2;
  }
  if (!improved) return finish('it reached the edge of the graph or a value that is not a number');
  ball.step++;
  ball.trail.push(ball.x);
  if (ball.step >= 200) return finish('it hit the limit of 200 steps');
  report();
  draw();
}

function finish(why, settled) {
  stopBall();
  ball.done = true; ball.why = why; ball.settled = !!settled;
  if (raceOnLoad) { raceOnLoad = false; runRace(); }
  report();
  draw();
}

// ---------- speed test ----------
const fmtMs = (ms) => (ms < 10 ? ms.toFixed(1) : ms < 1000 ? ms.toFixed(0) : (ms / 1000).toFixed(2) + ' s') + (ms < 1000 ? ' ms' : '');

function runRace() {
  if (!cur) return;
  raceBtn.disabled = true;
  raceOut.textContent = 'Running…';
  setTimeout(() => {
    // Always race the fixed plain-arithmetic formula, whatever is in the box.
    const temp = api.load(RACE_FALLBACK, 2);
    if (!temp) { raceOut.textContent = 'The speed test could not start.'; raceBtn.disabled = false; return; }
    const r = { h: temp, f: instantiate(temp, 0), n: api.nvars(temp), vals: [0] };
    const N = 1000000;
    const xs = new Float64Array(N);
    for (let i = 0; i < N; i++) xs[i] = XMIN + (XMAX - XMIN) * ((i * 0.6180339887) % 1);
    const others = r.vals.slice(1);
    const f = r.f, n = r.n;
    for (let i = 0; i < n; i++) api.setVar(r.h, i, r.vals[i]);
    let sink = 0;

    const runWasm = () => {
      let s = 0;
      if (n <= 1) { for (let i = 0; i < N; i++) s += f(xs[i]); }
      else if (n === 2) { const y = others[0]; for (let i = 0; i < N; i++) s += f(xs[i], y); }
      else { const a = r.vals.slice(); for (let i = 0; i < N; i++) { a[0] = xs[i]; s += f.apply(null, a); } }
      return s;
    };
    // The interpreter runs as one loop inside the engine: copy the inputs into
    // the wasm heap once, call jitw_interp_sum once, free.
    const runInterp = (arr) => {
      const len = arr.length;
      const ptr = api.malloc(8 * len);
      M.HEAPF64.set(arr, ptr / 8);               // read the view after malloc: memory may have grown
      const s = api.interpSum(r.h, 0, ptr, len);
      api.free_(ptr);
      return s;
    };
    const small = xs.subarray(0, 2000);
    const runWasmSmall = () => { let s = 0; const a = r.vals.slice(); for (let i = 0; i < small.length; i++) { a[0] = small[i]; s += f.apply(null, a); } return s; };
    sink += runWasmSmall() + runInterp(small);   // warm up both sides
    runWasm();
    let t = performance.now(); sink += runWasm(); const tw = performance.now() - t;
    t = performance.now(); sink += runInterp(xs); const ti = performance.now() - t;
    if (sink === 0.123456) console.log(sink);    // keep the sums alive
    api.free(temp);

    const x = ti / Math.max(tw, 1e-6);
    raceOut.textContent = 'Compiled: ' + fmtMs(tw) + ' · Step by step: ' + fmtMs(ti) + ' · ' +
      (x >= 1 ? (x >= 10 ? x.toFixed(0) : x.toFixed(1)) + '× faster' : 'step by step was ' + (1 / x).toFixed(1) + '× faster this time');
    raceBtn.disabled = false;
  }, 30);
}

// ---------- theme ----------
function currentTheme() {
  const t = document.documentElement.dataset.theme;
  if (t) return t;
  return window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
}
function syncThemeButton() { $('theme').textContent = currentTheme() === 'dark' ? 'Light mode' : 'Dark mode'; }
$('theme').addEventListener('click', () => {
  document.documentElement.dataset.theme = currentTheme() === 'dark' ? 'light' : 'dark';
  syncThemeButton();
  draw();
});
window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => { syncThemeButton(); draw(); });
syncThemeButton();

// ---------- wiring ----------
let debounce = 0;
exprEl.addEventListener('input', () => {
  clearTimeout(debounce);
  debounce = setTimeout(() => compile(exprEl.value), 150);
});
for (const ex of document.querySelectorAll('.ex')) {
  ex.addEventListener('click', () => {
    exprEl.value = ex.textContent;
    clearTimeout(debounce);
    compile(exprEl.value);
  });
}
atEl.addEventListener('input', () => {
  const v = parseFloat(atEl.value);
  if (Number.isFinite(v)) { atX = v; renderValues(); }
});
dropBtn.addEventListener('click', findLowest);
raceBtn.addEventListener('click', runRace);
let resizeTimer = 0;
window.addEventListener('resize', () => { clearTimeout(resizeTimer); resizeTimer = setTimeout(draw, 80); });

buildTabs();
renderStage();
const params = new URLSearchParams(location.search);
if (params.has('expr')) exprEl.value = params.get('expr');
dropOnLoad = params.get('drop') === '1';
raceOnLoad = params.get('race') === '1';
if (params.has('x0') && Number.isFinite(parseFloat(params.get('x0')))) startX = parseFloat(params.get('x0'));
if (params.get('open') === '1' || params.get('race') === '1' || location.hash === '#under-the-hood') hood.open = true;

boot().then(() => {
  const ok = compile(exprEl.value);
  if (ok && dropOnLoad) requestAnimationFrame(() => findLowest());
  else if (ok && raceOnLoad) { raceOnLoad = false; runRace(); }
}).catch((e) => {
  errBox.hidden = false;
  errMsg.textContent = 'The engine could not load (engine.mjs / engine.wasm missing?): ' + (e.message || e);
});
