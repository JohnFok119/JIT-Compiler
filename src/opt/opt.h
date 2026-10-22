/* Optimizer internals (backends.md, Optimizer). Every rule function takes a
 * node (op, a, b) whose operands already live in `out` and returns the node
 * index it rewrites to, OPT_NONE when it does not apply, or -1 on out of
 * memory. */

#ifndef JIT_OPT_H
#define JIT_OPT_H

#include "ir/ir.h"

#define OPT_NONE (-2)

int opt_fold(struct ir *out, enum ir_op op, int a, int b);      /* rule 1 */
int opt_simplify(struct ir *out, enum ir_op op, int a, int b);  /* rules 2, 5 */
int opt_strength(struct ir *out, enum ir_op op, int a, int b);  /* rules 3, 4 */

/* Shared helpers: is node n an IR_CONST with exactly this bit pattern / value. */
int opt_is_const(const struct ir *, int n);
int opt_is_bits(const struct ir *, int n, double v);   /* bit-identical to v */

#endif /* JIT_OPT_H */
