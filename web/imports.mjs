// The "m" imports every module from the engine expects. Shared by the Node
// smoke test and the browser page.
//
// Raw Math.pow is not C pow. C (and the engine's interpreter) define
// pow(1, y) = 1 for every y including NaN, pow(x, +-0) = 1 for every x
// including NaN, and pow(-1, +-inf) = 1. Math.pow returns NaN for pow(1, NaN),
// pow(-1, +-inf) and pow(1, +-inf). cpow patches those cases (x === 1 or y === 0
// also covers a NaN partner) so the wasm result matches C.
export function cpow(x, y) {
  if (x === 1 || y === 0) return 1;
  if (x === -1 && (y === Infinity || y === -Infinity)) return 1;
  return Math.pow(x, y);
}

export const mathImports = {
  m: { sin: Math.sin, cos: Math.cos, tan: Math.tan, exp: Math.exp,
       log: Math.log, tanh: Math.tanh, pow: cpow },
};
