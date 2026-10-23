/* Symbolic differentiation (backends.md, Derivatives): one pass over the
 * nodes in topological order. For each node i, p[i] is a copy of the node
 * itself in the new IR and d[i] its derivative; both go through the
 * hash-cons builder, so shared sub-derivatives stay shared. */

#include <stdlib.h>
#include <string.h>
#include "api.h"

/*
 * Builders with the textbook zero/one rules applied as the derivative is
 * constructed (a term whose derivative is 0 is dropped, a factor 1 is
 * omitted). This defines what the derivative is; it is not an optimizer
 * rewrite, so it may drop 0 * x. A -1 operand (out of memory) propagates.
 */
static int
is_val(const struct ir *o, int n, double v)
{
	return 0 <= n && IR_CONST == o->nodes[n].op && o->nodes[n].imm == v;
}

static int
mk_bin(struct ir *o, enum ir_op op, int x, int y)
{
	if (0 > x || 0 > y) {
		return -1;
	}
	switch (op) {
	case IR_MUL:
		if (is_val(o, x, 0.0) || is_val(o, y, 0.0)) {
			return ir_const(o, 0.0);
		}
		if (is_val(o, x, 1.0)) {
			return y;
		}
		if (is_val(o, y, 1.0)) {
			return x;
		}
		break;
	case IR_ADD:
		if (is_val(o, x, 0.0)) {
			return y;
		}
		if (is_val(o, y, 0.0)) {
			return x;
		}
		break;
	case IR_SUB:
		if (is_val(o, y, 0.0)) {
			return x;
		}
		if (is_val(o, x, 0.0)) {
			return ir_unary(o, IR_NEG, y);
		}
		break;
	case IR_DIV:
		if (is_val(o, x, 0.0)) {
			return ir_const(o, 0.0);
		}
		if (is_val(o, y, 1.0)) {
			return x;
		}
		break;
	case IR_POW:
		if (is_val(o, y, 1.0)) {
			return x;
		}
		if (is_val(o, y, 0.0)) {
			return ir_const(o, 1.0);
		}
		break;
	default:
		break;
	}
	return ir_binary(o, op, x, y);
}

static int
mk_un(struct ir *o, enum ir_op op, int x)
{
	if (0 > x) {
		return -1;
	}
	if (IR_NEG == op && is_val(o, x, 0.0)) {
		return ir_const(o, 0.0);
	}
	return ir_unary(o, op, x);
}

#define C(v)        ir_const(o, (v))
#define U(op, x)    mk_un(o, (op), (x))
#define B(op, x, y) mk_bin(o, (op), (x), (y))

/* Derivative of node n (operands already done); -1 on out of memory. */
static int
diff_node(struct ir *o, const struct ir *in, const struct ir_node *n,
	  const int *p, const int *d, int self, int target)
{
	int a = 0 <= n->a && IR_VAR != n->op ? p[n->a] : -1;
	int b = 0 <= n->b ? p[n->b] : -1;
	int da = 0 <= n->a && IR_VAR != n->op ? d[n->a] : -1;
	int db = 0 <= n->b ? d[n->b] : -1;
	int t;

	switch (n->op) {
	case IR_CONST: return C(0.0);
	case IR_VAR:   return C(n->a == target ? 1.0 : 0.0);
	case IR_STEP:  return C(0.0);
	case IR_NEG:   return U(IR_NEG, da);
	case IR_ADD:   return B(IR_ADD, da, db);
	case IR_SUB:   return B(IR_SUB, da, db);
	case IR_MUL:   return B(IR_ADD, B(IR_MUL, da, b), B(IR_MUL, a, db));
	case IR_DIV:
		if (is_val(o, db, 0.0)) {           /* a' / b */
			return B(IR_DIV, da, b);
		}
		if (is_val(o, da, 0.0)) {           /* -(a * b') / (b * b) */
			return B(IR_DIV, U(IR_NEG, B(IR_MUL, a, db)),
				 B(IR_MUL, b, b));
		}
		return B(IR_DIV, B(IR_SUB, B(IR_MUL, da, b), B(IR_MUL, a, db)),
			 B(IR_MUL, b, b));
	case IR_POW:
		if (IR_CONST == in->nodes[n->b].op) {
			/* b * pow(a, b-1) * a' */
			return B(IR_MUL,
				 B(IR_MUL, b,
				   B(IR_POW, a, C(in->nodes[n->b].imm - 1.0))),
				 da);
		}
		/* pow(a,b) * (b' * log(a) + b * a' / a) */
		return B(IR_MUL, self,
			 B(IR_ADD, B(IR_MUL, db, U(IR_LOG, a)),
			   B(IR_DIV, B(IR_MUL, b, da), a)));
	case IR_SIN:   return B(IR_MUL, da, U(IR_COS, a));
	case IR_COS:   return B(IR_MUL, U(IR_NEG, U(IR_SIN, a)), da);
	case IR_TAN:
		t = U(IR_COS, a);
		return B(IR_DIV, da, B(IR_MUL, t, t));
	case IR_EXP:   return B(IR_MUL, da, self);
	case IR_LOG:   return B(IR_DIV, da, a);
	case IR_SQRT:  return B(IR_DIV, da, B(IR_MUL, C(2.0), self));
	case IR_TANH:
		return B(IR_MUL, da, B(IR_SUB, C(1.0), B(IR_MUL, self, self)));
	case IR_SIGMOID:
		return B(IR_MUL, B(IR_MUL, da, self), B(IR_SUB, C(1.0), self));
	case IR_RELU:  return B(IR_MUL, da, U(IR_STEP, a));
	case IR_ABS:
		return B(IR_MUL, da,
			 B(IR_SUB, U(IR_STEP, a), U(IR_STEP, U(IR_NEG, a))));
	case IR_MIN:   /* a < b, ties -> b: step(b - a) */
		t = U(IR_STEP, B(IR_SUB, b, a));
		return B(IR_ADD, B(IR_MUL, da, t),
			 B(IR_MUL, db, B(IR_SUB, C(1.0), t)));
	case IR_MAX:   /* a > b, ties -> b: step(a - b) */
		t = U(IR_STEP, B(IR_SUB, a, b));
		return B(IR_ADD, B(IR_MUL, da, t),
			 B(IR_MUL, db, B(IR_SUB, C(1.0), t)));
	}
	return -1;
}

static jit_expr *
fail(jit_expr *e, struct jit_error *err, const char *msg)
{
	jit_free(e);
	err->pos = -1;
	snprintf(err->msg, sizeof (err->msg), "%s", msg);
	return NULL;
}

jit_expr *
jit_diff(const jit_expr *e, const char *var, struct jit_error *err)
{
	const struct ir *in = &e->ir;
	jit_expr *r;
	struct ir *o;
	int *p = NULL, *d = NULL;
	int i, target = -1, bad = 0;

	if (in->legacy) {
		return fail(NULL, err, "cannot differentiate in legacy mode");
	}
	if (!(r = calloc(1, sizeof (*r)))) {
		return fail(NULL, err, "out of memory");
	}
	o = &r->ir;
	ir_init(o);
	o->nvars = in->nvars;
	memcpy(o->vars, in->vars, sizeof (o->vars));
	for (i = 0; i < in->nvars; ++i) {
		if (!strcmp(in->vars[i], var)) {
			target = i;
		}
	}
	if (0 > target) {               /* unknown variable: the constant 0 */
		o->root = ir_const(o, 0.0);
		if (0 > o->root) {
			return fail(r, err, "out of memory");
		}
		return r;
	}
	p = malloc((size_t)in->count * sizeof (*p));
	d = malloc((size_t)in->count * sizeof (*d));
	if (!p || !d) {
		free(p);
		free(d);
		return fail(r, err, "out of memory");
	}
	for (i = 0; i <= in->root && !bad; ++i) {
		const struct ir_node *n = &in->nodes[i];

		if (IR_CONST == n->op) {
			p[i] = ir_const(o, n->imm);
		}
		else if (IR_VAR == n->op) {
			p[i] = ir_var(o, n->a);
		}
		else if (ir_is_binary(n->op)) {
			p[i] = ir_binary(o, n->op, p[n->a], p[n->b]);
		}
		else {
			p[i] = ir_unary(o, n->op, p[n->a]);
		}
		d[i] = 0 <= p[i] ? diff_node(o, in, n, p, d, p[i], target) : -1;
		bad = 0 > p[i] || 0 > d[i];
	}
	if (!bad) {
		o->root = d[in->root];
	}
	free(p);
	free(d);
	if (bad || jit_display_form(r)) {
		return fail(r, err, "out of memory");
	}
	return r;
}
