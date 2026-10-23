/* IR -> C source for the GCC backend. */

#ifndef JIT_BACKEND_GCC_CGEN_H
#define JIT_BACKEND_GCC_CGEN_H

#include <stdio.h>
#include "ir/ir.h"

/*
 * Writes the original assignment's program for a legacy IR:
 * `double evaluate(void)`, one `double tN = ...;` per node, the result
 * passed through jit_rt_sigmoid via the function-pointer-as-integer trick.
 * Supports const, neg, add, sub, mul, div. Returns 0, or -1 if the IR uses
 * any other op.
 */
int cgen_legacy(const struct ir *, FILE *out);

/*
 * Writes `double evaluate(const double *v)` for a normal-mode IR: constants
 * as %a hex floats (non-finite ones as (1.0/0.0), (-1.0/0.0), (0.0/0.0)),
 * libm calls through <math.h>, sigmoid through the host pointer trick.
 * Returns 0, or -1 for an invalid IR.
 */
int cgen_normal(const struct ir *, FILE *out);

#endif /* JIT_BACKEND_GCC_CGEN_H */
