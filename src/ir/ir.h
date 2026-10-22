/* Hash-consed SSA intermediate representation (core.md §IR). */

#ifndef JIT_IR_H
#define JIT_IR_H

#include <stddef.h>
#include <stdio.h>
#include "jit.h"

enum ir_op {
	IR_CONST, IR_VAR,                         /* leaves: imm / a = var index */
	IR_NEG, IR_ABS, IR_SQRT, IR_RELU,         /* unary, operand a */
	IR_SIN, IR_COS, IR_TAN, IR_EXP, IR_LOG, IR_TANH, IR_SIGMOID,   /* unary calls, operand a */
	IR_ADD, IR_SUB, IR_MUL, IR_DIV, IR_MIN, IR_MAX, IR_POW,        /* binary, operands a, b */
	IR_STEP                                   /* internal (derivatives only, not in the language): (a > 0.0) ? 1.0 : 0.0, NaN -> 0 */
};

/* Unused operands are -1; imm is 0.0 except for IR_CONST. */
struct ir_node { enum ir_op op; int a, b; double imm; };

struct ir {
	struct ir_node *nodes; int count, cap;    /* topological: operands always have smaller indices */
	int root;
	int nvars; char vars[JIT_MAX_VARS][32];
	int legacy;
	/* hash-cons table: open addressing over (op, a, b, bit pattern of imm) */
	int *table; int table_cap;
};

void ir_init(struct ir *);
void ir_free(struct ir *);

/*
 * Builders: return the node index, or -1 on out of memory. An operand of
 * -1 propagates (returns -1), so errors can be checked once at the end.
 */
int ir_const(struct ir *, double v);          /* dedups by bit pattern (0.0 and -0.0 distinct; all NaNs keep their bits) */
int ir_var(struct ir *, int index);
int ir_unary(struct ir *, enum ir_op, int a);
int ir_binary(struct ir *, enum ir_op, int a, int b);   /* no canonical reordering of operands */

int ir_is_unary(enum ir_op);
int ir_is_binary(enum ir_op);
const char *ir_op_name(enum ir_op);

/* Lowers an AST into ir (post-order, left to right); sets ir->root. 0 or -1 (out of memory). */
struct ast;
int ir_lower(struct ir *, const struct ast *);

/* Value of node ir->root given the variable values (honours ir->legacy). */
double ir_interp(const struct ir *, const double *vars);

/* Reachable nodes from root, renumbered densely, then "ret %N". */
void ir_print(const struct ir *, FILE *);

/*
 * Shortest of %.15g, %.16g, %.17g that round-trips; "nan" for every NaN,
 * "inf" / "-inf", "-0". len >= 32 is always enough.
 */
void ir_fmt_double(char *buf, size_t len, double v);

#endif /* JIT_IR_H */
