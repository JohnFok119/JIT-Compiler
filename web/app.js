// URL parameters (all optional):
//   ?expr=<formula>  fill the input with this formula (URL-encoded)
//   ?drop=1          press "Drop the ball" once, after the first graph is drawn
//   ?x0=<number>     start the ball at this x (clamped to -4..4) instead of a random x
//
// Live demo page: compile a formula to WebAssembly in the browser, show the
// engine's stages, graph f and f', roll a gradient-descent ball, race the
// compiled code against the interpreter. No network calls, no dependencies.
import createEngine from './engine.mjs';
import { mathImports } from './imports.mjs';

const $ = (id) => document.getElementById(id);
const exprEl = $('expr'), errEl = $('err'), codeEl = $('code'), capEl = $('caption');
const tabsEl = $('tabs'), canvas = $('plot'), slidersEl = $('sliders');
const dropBtn = $('dropBtn'), dropInfo = $('dropInfo'), raceBtn = $('raceBtn');

const XMIN = -4, XMAX = 4;

const STAGES = [
  { id: 'tokens', label: 'tokens', stage: 'tokens', cap: 'tokens: the formula chopped into its smallest pieces, such as numbers, names and symbols.' },
  { id: 'tree', label: 'tree', stage: 'ast', cap: 'tree: how the pieces fit together, like a family tree. Multiplication binds tighter than addition, and so on.' },
  { id: 'steps', label: 'steps (IR)', stage: 'ir', cap: 'steps (IR): the formula as a flat list of simple steps. IR means intermediate representation, a middle language the compiler works in.' },
  { id: 'tidied', label: 'tidied (IR)', stage: 'ir_opt', cap: 'tidied (IR): the same steps after the -O2 clean-up passes, which remove repeated work and fold constants.' },
  { id: 'x86', label: 'x86-64', stage: 'asm', cap: "x86-64: the machine code this engine writes for a laptop processor. Your browser can't run x86, so the page runs the WebAssembly version below instead." },
  { id: 'bytes', label: 'bytes', stage: 'hex', cap: 'bytes: those same x86-64 instructions as raw bytes in hexadecimal, exactly what would sit in memory.' },
  { id: 'wasm', label: 'wasm', stage: 'wasm', cap: 'wasm: the WebAssembly module generated for this formula, as hexadecimal bytes. This is what your browser actually runs.' },
  { id: 'slope', label: 'slope', stage: 'diff', cap: "slope: the derivative f′(x), worked out by the compiler itself. It is how fast the answer changes when x changes." },
];

let M, api;
let cur = null;            // current compiled formula
let tab = 'x86';
let stageCache = {};
let ball = null;           // running gradient descent
let ballTimer = 0;
let sliderVals = {};       // by variable name
let dropOnLoad = false;
let startX = null;         // from ?x0=

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
  errEl.hidden = false;
  errEl.textContent = e.pos >= 0
    ? src + '\n' + ' '.repeat(e.pos) + '^ ' + e.msg
    : e.msg;
}

function compile(src) {
  if (!api) return false;
  stopBall();
  const h = api.load(src, 2);
  if (!h) {
    showError(src, api.errorJson());
    if (tab === "tokens") codeEl.textContent = errEl.textContent;
    return false;                     // keep showing the last good formula
  }
  let f, fp;
  try {
    f = instantiate(h, 0);
    fp = instantiate(h, 1);
  } catch (e) {
    api.free(h);
    errEl.hidden = false;
    errEl.textContent = String(e.message || e);
    return false;
  }
  errEl.hidden = true;
  errEl.textContent = '';
  if (cur) api.free(cur.h);
  const n = api.nvars(h);
  const names = [];
  for (let i = 0; i < n; i++) names.push(api.varName(h, i));
  cur = { h, src, f, fp, n, names, vals: names.map((nm) => (nm in sliderVals ? sliderVals[nm] : 1)) };
  stageCache = {};
  sampled = null;
  buildSliders();
  renderStage();
  resetRace();
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
  codeEl.classList.toggle('wrap', tab === 'bytes' || tab === 'wasm');
  if (!cur) { codeEl.textContent = ''; return; }
  if (!(s.id in stageCache)) {
    const t = api.stage(cur.h, s.stage);
    stageCache[s.id] = t === null || t === undefined || t === ''
      ? '(' + (JSON.parse(api.errorJson()).msg || 'nothing to show') + ')'
      : t;
  }
  codeEl.textContent = stageCache[s.id];
}

// ---------- sliders ----------
function buildSliders() {
  slidersEl.textContent = '';
  for (let i = 1; i < cur.n; i++) {
    const nm = cur.names[i];
    const lab = document.createElement('label');
    const name = document.createElement('span');
    name.textContent = nm + ' =';
    const inp = document.createElement('input');
    inp.type = 'range'; inp.min = '-3'; inp.max = '3'; inp.step = '0.05';
    inp.value = String(cur.vals[i]);
    inp.setAttribute('aria-label', 'value of ' + nm);
    const out = document.createElement('span');
    out.textContent = Number(inp.value).toFixed(2);
    inp.addEventListener('input', () => {
      cur.vals[i] = parseFloat(inp.value);
      sliderVals[nm] = cur.vals[i];
      out.textContent = cur.vals[i].toFixed(2);
      sampled = null;
      stopBall(); ball = null; dropInfo.textContent = '';
      draw();
    });
    lab.append(name, inp, out);
    slidersEl.appendChild(lab);
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

function draw() {
  if (!cur) return;
  const dpr = window.devicePixelRatio || 1;
  const w = canvas.clientWidth || 300;
  const h = Math.round(w * 0.62);
  canvas.style.height = h + 'px';
  canvas.width = Math.round(w * dpr);
  canvas.height = Math.round(h * dpr);
  const c = canvas.getContext('2d');
  c.setTransform(dpr, 0, 0, dpr, 0, 0);
  c.clearRect(0, 0, w, h);

  const { xs, fy, dy } = sample();
  const [ylo, yhi] = yRange(fy, dy);
  const padL = 6, padR = 6, padT = 8, padB = 8;
  const sx = (x) => padL + (x - XMIN) / (XMAX - XMIN) * (w - padL - padR);
  const sy = (y) => padT + (yhi - y) / (yhi - ylo) * (h - padT - padB);
  view = { sx, sy, ylo, yhi, h };

  const navy = css('--navy'), gold = css('--gold'), red = css('--red'), lo = css('--lo'), muted = css('--muted');
  // axes
  c.strokeStyle = lo; c.lineWidth = 1.5;
  c.beginPath();
  if (ylo < 0 && yhi > 0) { c.moveTo(sx(XMIN), sy(0)); c.lineTo(sx(XMAX), sy(0)); }
  c.moveTo(sx(0), padT); c.lineTo(sx(0), h - padB);
  c.stroke();
  c.fillStyle = muted; c.font = '11px ' + css('--mono');
  c.fillText(String(XMIN), sx(XMIN) + 2, h - 12);
  c.fillText(String(XMAX), sx(XMAX) - 12, h - 12);

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
  c.beginPath(); c.rect(0, 0, w, h); c.clip();
  line(dy, gold, 2, [6, 4]);
  line(fy, navy, 2.5, []);
  c.restore();

  // ball and trail
  if (ball) {
    const pt = (x) => {
      const y = F(x);
      return [sx(x), sy(Math.min(Math.max(Number.isFinite(y) ? y : ylo, ylo), yhi))];
    };
    c.strokeStyle = red; c.lineWidth = 1.5; c.setLineDash([2, 4]);
    c.beginPath();
    ball.trail.forEach((x, i) => { const [px, py] = pt(x); if (i) c.lineTo(px, py); else c.moveTo(px, py); });
    c.stroke(); c.setLineDash([]);
    c.fillStyle = red;
    ball.trail.slice(0, -1).forEach((x) => { const [px, py] = pt(x); c.beginPath(); c.arc(px, py, 2, 0, 6.2832); c.fill(); });
    const [bx, by] = pt(ball.x);
    c.beginPath(); c.arc(bx, by, 7, 0, 6.2832); c.fill();
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

function dropTheBall() {
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
  dropInfo.textContent = 'step ' + ball.step + ' · ' + xName() + ' = ' + ball.x.toFixed(5) +
    ' · f = ' + (Number.isFinite(f) ? f.toPrecision(5) : 'n/a') + (ball.done ? ' · ' + ball.why : '');
}

function stepBall() {
  const g = D(ball.x);
  const f0 = F(ball.x);
  if (!Number.isFinite(g) || !Number.isFinite(f0)) return finish('slope is not a number here');
  if (Math.abs(g) < 1e-6) return finish('flat — settled');
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
  if (!improved) return finish('stuck (edge of the range or not a number)');
  ball.step++;
  ball.trail.push(ball.x);
  if (ball.step >= 200) return finish('stopped at 200 steps');
  report();
  draw();
}

function finish(why) {
  stopBall();
  ball.done = true; ball.why = why;
  report();
  draw();
}

// ---------- race ----------
function resetRace() {
  $('bar1').style.width = '0'; $('bar2').style.width = '0';
  $('t1').textContent = ''; $('t2').textContent = '';
  $('speedup').textContent = '';
}

const fmtMs = (ms) => (ms < 10 ? ms.toFixed(2) : ms < 1000 ? ms.toFixed(0) : (ms / 1000).toFixed(2) + ' s') + (ms < 1000 ? ' ms' : '');

function runRace() {
  if (!cur) return;
  raceBtn.disabled = true;
  resetRace();
  $('speedup').textContent = 'running…';
  setTimeout(() => {
    const N = 1000000;
    const xs = new Float64Array(N);
    for (let i = 0; i < N; i++) xs[i] = XMIN + (XMAX - XMIN) * ((i * 0.6180339887) % 1);
    const others = cur.vals.slice(1);
    const f = cur.f, n = cur.n;
    for (let i = 0; i < n; i++) api.setVar(cur.h, i, cur.vals[i]);
    let sink = 0;

    const runWasm = () => {
      let s = 0;
      if (n <= 1) { for (let i = 0; i < N; i++) s += f(xs[i]); }
      else if (n === 2) { const y = others[0]; for (let i = 0; i < N; i++) s += f(xs[i], y); }
      else { const a = cur.vals.slice(); for (let i = 0; i < N; i++) { a[0] = xs[i]; s += f.apply(null, a); } }
      return s;
    };
    // The interpreter runs as one loop inside the engine: copy the inputs into
    // the wasm heap once, call jitw_interp_sum once, free.
    const runInterp = (arr) => {
      const len = arr.length;
      const ptr = api.malloc(8 * len);
      M.HEAPF64.set(arr, ptr / 8);               // read the view after malloc: memory may have grown
      const s = api.interpSum(cur.h, 0, ptr, len);
      api.free_(ptr);
      return s;
    };
    const small = xs.subarray(0, 2000);
    const runWasmSmall = () => { let s = 0; const a = cur.vals.slice(); for (let i = 0; i < small.length; i++) { a[0] = small[i]; s += f.apply(null, a); } return s; };
    sink += runWasmSmall() + runInterp(small);   // warm up both sides
    runWasm();
    let t = performance.now(); sink += runWasm(); const tw = performance.now() - t;
    t = performance.now(); sink += runInterp(xs); const ti = performance.now() - t;
    if (sink === 0.123456) console.log(sink);    // keep the sums alive

    const slow = Math.max(tw, ti, 1e-6);
    $('bar1').style.width = Math.max(1, tw / slow * 100) + '%';
    $('bar2').style.width = Math.max(1, ti / slow * 100) + '%';
    $('t1').textContent = fmtMs(tw);
    $('t2').textContent = fmtMs(ti);
    const x = ti / Math.max(tw, 1e-6);
    $('speedup').textContent = x >= 1
      ? 'The compiled version was ' + (x >= 10 ? x.toFixed(0) : x.toFixed(1)) + '× faster.'
      : 'This time the step-by-step version won (' + (1 / x).toFixed(1) + '× faster). Try a longer formula.';
    raceBtn.disabled = false;
  }, 30);
}

// ---------- theme ----------
function currentTheme() {
  const t = document.documentElement.dataset.theme;
  if (t) return t;
  return window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
}
$('theme').addEventListener('click', () => {
  document.documentElement.dataset.theme = currentTheme() === 'dark' ? 'light' : 'dark';
  draw();
});
window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => draw());

// ---------- wiring ----------
let debounce = 0;
exprEl.addEventListener('input', () => {
  clearTimeout(debounce);
  debounce = setTimeout(() => compile(exprEl.value), 150);
});
for (const chip of document.querySelectorAll('.chip')) {
  chip.addEventListener('click', () => {
    exprEl.value = chip.textContent;
    clearTimeout(debounce);
    compile(exprEl.value);
  });
}
dropBtn.addEventListener('click', dropTheBall);
raceBtn.addEventListener('click', runRace);
let resizeTimer = 0;
window.addEventListener('resize', () => { clearTimeout(resizeTimer); resizeTimer = setTimeout(draw, 80); });

buildTabs();
renderStage();
const params = new URLSearchParams(location.search);
if (params.has('expr')) exprEl.value = params.get('expr');
dropOnLoad = params.get('drop') === '1';
if (params.has('x0') && Number.isFinite(parseFloat(params.get('x0')))) startX = parseFloat(params.get('x0'));

boot().then(() => {
  const ok = compile(exprEl.value);
  if (ok && dropOnLoad) requestAnimationFrame(() => dropTheBall());
}).catch((e) => {
  errEl.hidden = false;
  errEl.textContent = 'The engine could not load (engine.mjs / engine.wasm missing?): ' + (e.message || e);
});
