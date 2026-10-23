// Smoke test for the in-browser build (make wasm). Zero dependencies, Node >= 18.
//
// For every expression in cases.txt and ten variable vectors it takes the
// wasm module the engine emits (jitw_wasm), instantiates it with the shared imports (web/imports.mjs) as
// the "m" imports, calls f, and compares with the engine's own interpreter
// (jitw_interp). The same for the derivative (which = 1).
//   - no transcendental call in the IR: the results must be bit-identical
//     (NaN matches NaN);
//   - otherwise: relative error <= 1e-12, or both NaN, or the same infinity.
// Run from the repository root: node web/test/smoke.mjs

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import createEngine from '../dist/engine.mjs';
import { mathImports as imports } from '../imports.mjs';

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const lines = readFileSync(here('./cases.txt'), 'utf8')
  .split('\n').map((l) => l.trim()).filter((l) => l && !l.startsWith('#'));

const f64 = new Float64Array(1);
const u64 = new BigUint64Array(f64.buffer);
const bits = (v) => { f64[0] = v; return u64[0]; };

const SPECIAL = [-0, Infinity, NaN, 1e300, -1e-300, 0, 7, -Infinity];
// two plain vectors, then 8 rotations of the special values: variable i gets
// SPECIAL[(i + k) % 8], so every variable meets every special value
const VECTORS = [
  (i) => 0.5 + 0.37 * i,
  (i) => -1.25 + 0.6 * i,
  ...SPECIAL.map((_, k) => (i) => SPECIAL[(i + k) % SPECIAL.length]),
];
const CALLS = /= (sin|cos|tan|exp|log|tanh|sigmoid|pow) /;

function close(a, b) {
  if (Number.isNaN(a) || Number.isNaN(b)) return Number.isNaN(a) && Number.isNaN(b);
  if (a === b) return true;                       // equal, or the same infinity
  if (!Number.isFinite(a) || !Number.isFinite(b)) return false;
  return Math.abs(a - b) <= 1e-12 * Math.max(Math.abs(a), Math.abs(b));
}

const M = await createEngine();
const load = M.cwrap('jitw_load', 'number', ['string', 'number']);
const errorJson = M.cwrap('jitw_error_json', 'string', []);
const stage = M.cwrap('jitw_stage', 'string', ['number', 'string']);
const nvars = M.cwrap('jitw_nvars', 'number', ['number']);
const wasmPtr = M.cwrap('jitw_wasm', 'number', ['number', 'number']);
const wasmLen = M.cwrap('jitw_wasm_len', 'number', ['number', 'number']);
const setVar = M.cwrap('jitw_set_var', null, ['number', 'number', 'number']);
const interp = M.cwrap('jitw_interp', 'number', ['number', 'number']);
const interpSum = M.cwrap('jitw_interp_sum', 'number', ['number', 'number', 'number', 'number']);
const free = M.cwrap('jitw_free', null, ['number']);

let evaluations = 0;
let mismatches = 0;
const complain = (msg) => { mismatches++; console.log('MISMATCH ' + msg); };

for (const src of lines) {
  const h = load(src, 2);
  if (!h) { complain(`${src}: load failed ${errorJson()}`); continue; }

  for (const st of ['asm', 'hex', 'wasm']) {
    if (!stage(h, st)) complain(`${src}: empty ${st} stage`);
  }

  const n = nvars(h);
  const exact = [!CALLS.test(stage(h, 'ir_opt')), !CALLS.test(stage(h, 'diff_ir'))];

  for (const which of [0, 1]) {
    let inst;
    try {
      const ptr = wasmPtr(h, which);
      const len = wasmLen(h, which);
      const bytes = M.HEAPU8.slice(ptr, ptr + len);   // read after the calls: the heap view can be replaced when memory grows
      inst = new WebAssembly.Instance(new WebAssembly.Module(bytes), imports);
    } catch (e) {
      complain(`${src}: f${which ? "'" : ''} does not instantiate: ${e.message}`);
      continue;
    }
    for (let k = 0; k < VECTORS.length; k++) {
      const vars = Array.from({ length: n }, (_, i) => VECTORS[k](i));
      vars.forEach((v, i) => setVar(h, i, v));
      const want = interp(h, which);
      const got = inst.exports.f(...vars);
      evaluations++;
      const ok = exact[which] ? (Number.isNaN(want) ? Number.isNaN(got) : bits(want) === bits(got))
                              : close(want, got);
      if (!ok) complain(`${src} f${which ? "'" : ''} at [${vars}]: interp ${want} wasm ${got}`);
    }
  }

  // one-call interpreter sum over variable 0 == the JS-side sum of jitw_interp
  if (n > 0) {
    const xs = VECTORS.map((v) => v(0));
    const ptr = M._malloc(8 * xs.length);
    new Float64Array(M.HEAPU8.buffer, ptr, xs.length).set(xs);
    for (let i = 1; i < n; i++) setVar(h, i, VECTORS[0](i));
    for (const which of [0, 1]) {
      let want = 0;
      for (const x of xs) { setVar(h, 0, x); want += interp(h, which); }
      const got = interpSum(h, which, ptr, xs.length);
      evaluations++;
      if (!(Object.is(want, got) || (Number.isNaN(want) && Number.isNaN(got)))) {
        complain(`${src} interp_sum f${which ? "'" : ''}: JS sum ${want} engine sum ${got}`);
      }
    }
    M._free(ptr);
  }
  free(h);
}

console.log(`smoke: ${lines.length} cases, ${evaluations} evaluations, ${mismatches} mismatches`);
process.exit(mismatches ? 1 : 0);
