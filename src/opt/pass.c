/* Optimizer driver: rebuild from the root through the hash-cons builder. */

#include <stdlib.h>
#include <string.h>
#include "api.h"
#include "opt/opt.h"

#define MAX_ROUNDS 8

int
opt_is_const(const struct ir *ir, int n)
{
	return 0 <= n && n < ir->count && IR_CONST == ir->nodes[n].op;
}

int
opt_is_bits(const struct ir *ir, int n, double v)
{
	return opt_is_const(ir, n) &&
	       !memcmp(&ir->nodes[n].imm, &v, sizeof (v));
}

/* Number of nodes reachable from the root. */
static int
reachable(const struct ir *ir, char *live)
{
	int i, n = 0;

	memset(live, 0, (size_t)ir->count);
	live[ir->root] = 1;
	for (i = ir->root; i >= 0; --i) {
		const struct ir_node *nd = &ir->nodes[i];

		if (!live[i]) {
			continue;
		}
		++n;
		if (IR_CONST == nd->op || IR_VAR == nd->op) {
			continue;
		}
		live[nd->a] = 1;
		if (0 <= nd->b) {
			live[nd->b] = 1;
		}
	}
	return n;
}

/* One rebuild of the reachable nodes of `in` into `out`; rules 0 = none,
   1 = fold and identities only (display form), 2 = all. */
static int
rebuild(const struct ir *in, struct ir *out, int rules, int *count, int *fired)
{
	char *live = malloc((size_t)in->count);
	int *map = malloc((size_t)in->count * sizeof (*map));
	int i, bad = 0;

	if (!live || !map) {
		free(live);
		free(map);
		return -1;
	}
	*count = reachable(in, live);
	*fired = 0;
	ir_init(out);
	out->nvars = in->nvars;
	memcpy(out->vars, in->vars, sizeof (out->vars));
	out->legacy = in->legacy;
	for (i = 0; i <= in->root && !bad; ++i) {
		const struct ir_node *n = &in->nodes[i];
		int a = 0 <= n->a && IR_VAR != n->op ? map[n->a] : -1;
		int b = 0 <= n->b ? map[n->b] : -1;
		int r = OPT_NONE;

		if (!live[i]) {
			continue;
		}
		if (IR_CONST == n->op) {
			r = ir_const(out, n->imm);
		}
		else if (IR_VAR == n->op) {
			r = ir_var(out, n->a);
		}
		else {
			if (rules) {
				r = opt_fold(out, n->op, a, b);
				if (OPT_NONE == r && !(1 == rules && IR_MUL == n->op &&
						       (opt_is_bits(out, a, 2.0) ||
							opt_is_bits(out, b, 2.0)))) {
					r = opt_simplify(out, n->op, a, b);
				}
				if (OPT_NONE == r && 2 == rules) {
					r = opt_strength(out, n->op, a, b);
				}
				if (OPT_NONE != r) {
					*fired = 1;
				}
			}
			if (OPT_NONE == r) {
				r = ir_is_binary(n->op)
				    ? ir_binary(out, n->op, a, b)
				    : ir_unary(out, n->op, a);
			}
		}
		if (0 > r) {
			bad = 1;
		}
		map[i] = r;
	}
	if (!bad) {
		out->root = map[in->root];
	}
	free(live);
	free(map);
	if (bad) {
		ir_free(out);
		return -1;
	}
	return 0;
}

static int
optimize(jit_expr *e, int level)
{
	struct ir cur, next;
	int round, count, fired;

	if (0 == level) {
		return 0;
	}
	if (1 != level && 2 != level) {
		return -1;
	}
	if (e->ir.legacy) {
		return 0;       /* legacy semantics are pinned: never optimized */
	}
	cur = e->ir;
	for (round = 0; round < MAX_ROUNDS; ++round) {
		if (rebuild(&cur, &next, level, &count, &fired)) {
			if (cur.nodes != e->ir.nodes) {
				ir_free(&cur);
			}
			return -1;
		}
		if (cur.nodes != e->ir.nodes) {
			ir_free(&cur);
		}
		cur = next;
		/* fixed point: no rule fired in this round */
		if (!fired) {
			break;
		}
	}
	/* final rebuild without rules: drops the dead nodes */
	if (rebuild(&cur, &next, 0, &count, &fired)) {
		ir_free(&cur);
		return -1;
	}
	ir_free(&cur);
	ir_free(&e->ir);
	e->ir = next;
	return 0;
}

int
jit_optimize(jit_expr *e, int level)
{
	return 1 == level ? -1 : optimize(e, level);
}

int
jit_display_form(jit_expr *e)
{
	return optimize(e, 1);
}
