/* WebAssembly backend: IR -> one fresh wasm module per expression. */

#ifndef JIT_WASM_EMIT_H
#define JIT_WASM_EMIT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "ir/ir.h"
#include "jit.h"

/*
 * Module layout (all hand-built, no tools):
 *   type    0: (f64 x nvars) -> f64   1: (f64) -> f64   2: (f64, f64) -> f64
 *   import  "m" "sin" "cos" "tan" "exp" "log" "tanh" (type 1), "m" "pow"
 *           (type 2) -- only those the expression uses, in that order
 *   func    one function, type 0
 *   export  "f"
 *   code    one f64 local per reachable IR node, computed in topological
 *           order with local.set; the result is local.get of the root.
 * sigmoid(a) = 1.0 / (1.0 + exp(-a)) through the imported exp.
 *
 * Not for legacy expressions. On success *out is a malloc'd buffer (free it)
 * and the return value is 0; otherwise -1 with err set (err->pos = -1).
 */
int wasm_emit(const struct ir *, unsigned char **out, size_t *len,
	      struct jit_error *);

/* V8 allows 50000 locals in a function and every reachable node takes one:
 * wasm_emit fails with "expression too large for the wasm backend" beyond this. */
#define WASM_MAX_NODES 50000

/* LEB128: write v at out (at most 10 bytes), return the byte count. */
size_t wasm_leb_u(unsigned char *out, uint64_t v);
size_t wasm_leb_s(unsigned char *out, int64_t v);

/* stage "wasm": hex, 16 bytes per line, a "; section" comment line before the
 * bytes of the header and of each section. 0 ok, -1 unknown stage. */
int wasm_dump(const unsigned char *code, size_t len, const char *stage, FILE *);

#endif /* JIT_WASM_EMIT_H */
