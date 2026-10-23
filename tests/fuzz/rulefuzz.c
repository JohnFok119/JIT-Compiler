/* Rule-level property test for the optimizer rewrites in src/opt/ (fold is
 * excluded: it only evaluates constants). For every rewrite rule it builds the
 * smallest IR that triggers it, applies the rule, and compares
 *   interp(before) == interp(after) == x86(after)
 * bit for bit (any NaN equals any NaN) over N random doubles: a quarter
 * uniform random bit patterns, a quarter uniform in [-10, 10], a quarter
 * within a few ulps of powers of two, a quarter special values. Whole-expression
 * fuzzing missed pow(x, 2) -> x * x, wrong for ~0.08% of inputs; this does not.
 *
 * usage: rulefuzz [--seed S] [--n N]    (default N = 100000 per rule) */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "api.h"
#include "gen.h"
#include "opt/opt.h"

static const double SPECIAL[] = {
	0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -2.0, 1e308, -1e308, 1e-308,
	-1e-308, 4.9e-324, -4.9e-324, 2.2250738585072014e-308,
	1.7976931348623157e308, INFINITY, -INFINITY, NAN, -NAN
};
#define NSPECIAL (sizeof (SPECIAL) / sizeof (SPECIAL[0]))

static int
same(double a, double b)
{
	if (isnan(a) || isnan(b)) {
		return isnan(a) && isnan(b);
	}
	return !memcmp(&a, &b, sizeof (a));
}

static double
sample(uint64_t *rng, long i)
{
	double d;
	uint64_t u;

	switch (i % 4) {
	case 0:
		u = gen_next(rng);
		memcpy(&d, &u, sizeof (d));
		return d;
	case 1:
		return -10.0 + 20.0 * ((double)(gen_next(rng) >> 11) / 9007199254740992.0);
	case 2:
		d = ldexp(1.0, (int)gen_below(rng, 2047) - 1023);
		memcpy(&u, &d, sizeof (u));
		u += (uint64_t)((int64_t)gen_below(rng, 9) - 4);
		memcpy(&d, &u, sizeof (d));
		return gen_below(rng, 2) ? d : -d;
	default:
		return SPECIAL[gen_below(rng, NSPECIAL)];
	}
}

enum shape { X_OP_C, C_OP_X, NEG_NEG };

struct rule {
	const char *name;
	enum shape shape;
	enum ir_op op;
	double c;
	int strength;          /* the rule lives in opt_strength, else opt_simplify */
};

static const struct rule RULES[] = {
	{ "x * 1 -> x",            X_OP_C, IR_MUL, 1.0, 0 },
	{ "1 * x -> x",            C_OP_X, IR_MUL, 1.0, 0 },
	{ "x / 1 -> x",            X_OP_C, IR_DIV, 1.0, 0 },
	{ "x - (+0) -> x",         X_OP_C, IR_SUB, 0.0, 0 },
	{ "-(-x) -> x",            NEG_NEG, IR_NEG, 0.0, 0 },
	{ "x + (-0) -> x",         X_OP_C, IR_ADD, -0.0, 0 },
	{ "(-0) + x -> x",         C_OP_X, IR_ADD, -0.0, 0 },
	{ "x * 2 -> x + x",        X_OP_C, IR_MUL, 2.0, 0 },
	{ "2 * x -> x + x",        C_OP_X, IR_MUL, 2.0, 0 },
	{ "pow(x, 1) -> x",        X_OP_C, IR_POW, 1.0, 1 },
	{ "x / 2 -> x * 0.5",      X_OP_C, IR_DIV, 2.0, 1 },
	{ "x / 4 -> x * 0.25",     X_OP_C, IR_DIV, 4.0, 1 },
	{ "x / 0.5 -> x * 2",      X_OP_C, IR_DIV, 0.5, 1 },
	{ "x / -8 -> x * -0.125",  X_OP_C, IR_DIV, -8.0, 1 },
	{ "x / 2^10",              X_OP_C, IR_DIV, 1024.0, 1 },
	{ "x / 2^-100",            X_OP_C, IR_DIV, 7.888609052210118e-31, 1 },
	{ "x / 2^1022",            X_OP_C, IR_DIV, 4.4942328371557898e+307, 1 },
	{ "x / 2^-1022",           X_OP_C, IR_DIV, 2.2250738585072014e-308, 1 },
	{ "x / -2^-1000",          X_OP_C, IR_DIV, -9.332636185032189e-302, 1 }
};
#define NRULES (sizeof (RULES) / sizeof (RULES[0]))

static int
run_rule(const struct rule *r, uint64_t *rng, long n)
{
	struct ir ir;
	struct jit_expr e;
	struct jit_error err;
	jit_fn *fn;
	int x, c, before, after;
	long i, bad = 0;

	ir_init(&ir);
	ir.nvars = 1;
	strcpy(ir.vars[0], "x");
	x = ir_var(&ir, 0);
	c = ir_const(&ir, r->c);
	if (NEG_NEG == r->shape) {
		x = ir_unary(&ir, IR_NEG, x);
		before = ir_unary(&ir, IR_NEG, x);
		after = opt_simplify(&ir, IR_NEG, x, -1);
	}
	else {
		int a = X_OP_C == r->shape ? x : c, b = X_OP_C == r->shape ? c : x;

		before = ir_binary(&ir, r->op, a, b);
		after = r->strength ? opt_strength(&ir, r->op, a, b)
				    : opt_simplify(&ir, r->op, a, b);
	}
	if (before < 0 || after < 0) {
		printf("rulefuzz: FAIL rule '%s' did not fire (result %d)\n", r->name, after);
		ir_free(&ir);
		return 1;
	}
	memset(&e, 0, sizeof (e));
	e.ir = ir;
	e.ir.root = after;
	if (!(fn = jit_compile(&e, JIT_X86, &err))) {
		printf("rulefuzz: FAIL x86 compile of '%s': %s\n", r->name, err.msg);
		ir_free(&ir);
		return 1;
	}
	for (i = 0; i < n; ++i) {
		double v[JIT_MAX_VARS] = { 0 }, w, g, h;

		v[0] = sample(rng, i);
		ir.root = before;
		w = ir_interp(&ir, v);
		ir.root = after;
		g = ir_interp(&ir, v);
		h = jit_call(fn, v);
		if (!same(w, g) || !same(w, h)) {
			if (++bad <= 3) {
				printf("rulefuzz: FAIL '%s' at x = %a (%.17g): before %a, after (interp) %a, after (x86) %a\n",
				       r->name, v[0], v[0], w, g, h);
			}
		}
	}
	jit_fn_free(fn);
	ir_free(&ir);
	if (bad) {
		printf("rulefuzz: FAIL '%s': %ld of %ld inputs differ\n", r->name, bad, n);
	}
	return bad != 0;
}

int
main(int argc, char *argv[])
{
	uint64_t seed = 20261024ull, rng;
	long n = 100000;
	size_t k;
	int i, fails = 0;

	for (i = 1; i + 1 < argc; i += 2) {
		if (!strcmp(argv[i], "--seed")) {
			seed = strtoull(argv[i + 1], NULL, 0);
		}
		else if (!strcmp(argv[i], "--n")) {
			n = atol(argv[i + 1]);
		}
		else {
			fprintf(stderr, "usage: rulefuzz [--seed S] [--n N]\n");
			return 2;
		}
	}
	rng = seed;
	for (k = 0; k < NRULES; ++k) {
		fails += run_rule(&RULES[k], &rng, n);
	}
	printf("rulefuzz: %zu rules, %ld inputs each, %d failing rules (seed %llu)\n",
	       NRULES, n, fails, (unsigned long long)seed);
	return fails ? 1 : 0;
}
