#include <stdio.h>
#include <string.h>
#include "gen.h"

uint64_t
gen_next(uint64_t *rng)
{
	uint64_t z = (*rng += 0x9e3779b97f4a7c15ull);

	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
	return z ^ (z >> 31);
}

uint64_t
gen_below(uint64_t *rng, uint64_t n)
{
	return gen_next(rng) % n;
}

static const char *const CONSTS[] = {
	"0", "(-0)", "1", "(-1)", "2", "0.5", "3", "1e-300", "1e300",
	"1e999", "(1e999-1e999)", "4", "0.25", "10", "pi", "e"
};
#define NCONSTS (sizeof (CONSTS) / sizeof (CONSTS[0]))

/* Right-hand sides that make the rewrite rules fire often. */
static const char *const HOT[] = {
	"1", "2", "0.5", "(-0)", "0", "4", "0.25", "(-1)", "3", "(-2)",
	"1e300", "1e-300"
};
#define NHOT (sizeof (HOT) / sizeof (HOT[0]))

static const char *const FN1[] = {
	"sin", "cos", "tan", "exp", "log", "sqrt", "abs", "tanh", "sigmoid", "relu"
};
#define NFN1 (sizeof (FN1) / sizeof (FN1[0]))

static const char *const FN2[] = { "min", "max", "pow" };

/* Smooth mode: no abs, relu, min, max (differentiable almost everywhere). */
static const char *const SMOOTH1[] = {
	"sin", "cos", "tan", "exp", "log", "sqrt", "tanh", "sigmoid"
};
#define NSMOOTH1 (sizeof (SMOOTH1) / sizeof (SMOOTH1[0]))

/* Smooth mode also drops the extreme constants (1e300, 1e-300, inf, nan): a
   finite difference through them measures rounding, not the function. */
static const char *const SMOOTH_CONSTS[] = {
	"0", "1", "(-1)", "2", "(-2)", "0.5", "3", "4", "0.25", "10", "pi", "e"
};
#define NSMOOTH_CONSTS (sizeof (SMOOTH_CONSTS) / sizeof (SMOOTH_CONSTS[0]))

#define POOL_N 16
#define POOL_LEN 1024

struct out {
	char *buf;
	size_t cap, len;
	int smooth;
	int wide;                          /* see struct gen_opts */
	int reuse, npool, overflow;
	char pool[POOL_N][POOL_LEN];       /* finished subtrees offered for reuse */
};

static void
put(struct out *o, const char *s)
{
	size_t n = strlen(s);

	if (o->len + n + 1 <= o->cap) {
		memcpy(o->buf + o->len, s, n + 1);
		o->len += n;
	}
	else {
		o->overflow = 1;
	}
}

static void leaf(uint64_t *rng, struct out *o, int nvars);
static void node(uint64_t *rng, struct out *o, int depth, int nvars);

static void
leaf(uint64_t *rng, struct out *o, int nvars)
{
	static const char *const VARS[] = { "x", "y", "z", "a", "b", "c", "d", "f" };

	if (o->reuse && o->npool && gen_below(rng, 3) == 0) {
		put(o, o->pool[gen_below(rng, (uint64_t)o->npool)]);
	}
	else if (gen_below(rng, 3)) {
		put(o, VARS[gen_below(rng, (uint64_t)nvars)]);
	}
	else {
		put(o, o->smooth ? SMOOTH_CONSTS[gen_below(rng, NSMOOTH_CONSTS)]
				 : CONSTS[gen_below(rng, NCONSTS)]);
	}
}

/* Second operand: often a "hot" constant, else a subexpression. */
static void
rhs(uint64_t *rng, struct out *o, int depth, int nvars)
{
	if (gen_below(rng, 3) == 0) {
		put(o, o->smooth ? SMOOTH_CONSTS[gen_below(rng, NSMOOTH_CONSTS)]
				 : HOT[gen_below(rng, NHOT)]);
	}
	else {
		node(rng, o, depth, nvars);
	}
}

static void
node(uint64_t *rng, struct out *o, int depth, int nvars)
{
	static const char *const OPS[] = { "+", "-", "*", "/", "^" };
	static const char *const CHEAP1[] = { "sqrt", "abs", "relu" };
	static const char *const MINMAX[] = { "min", "max" };
	uint64_t k;
	size_t start = o->len;

	if (depth <= 0 || gen_below(rng, o->wide ? 20 : 6) == 0) {
		leaf(rng, o, nvars);
		return;
	}
	k = gen_below(rng, 10);
	if (k < 5) {                       /* binary operator */
		put(o, "(");
		node(rng, o, depth - 1, nvars);
		put(o, " ");
		put(o, OPS[gen_below(rng, o->wide ? 4 : 5)]);
		put(o, " ");
		rhs(rng, o, depth - 1, nvars);
		put(o, ")");
	}
	else if (k < 6) {                  /* unary minus, sometimes doubled */
		put(o, "(-");
		if (gen_below(rng, 3) == 0) {
			put(o, "(-");
			node(rng, o, depth - 1, nvars);
			put(o, ")");
		}
		else {
			node(rng, o, depth - 1, nvars);
		}
		put(o, ")");
	}
	else if (k < 8) {                  /* one-argument function */
		if (o->wide) {
			put(o, (2 == o->wide && gen_below(rng, 6) == 0)
			       ? FN1[gen_below(rng, NFN1)]
			       : CHEAP1[gen_below(rng, 3)]);
		}
		else {
			put(o, o->smooth ? SMOOTH1[gen_below(rng, NSMOOTH1)]
					 : FN1[gen_below(rng, NFN1)]);
		}
		put(o, "(");
		node(rng, o, depth - 1, nvars);
		put(o, ")");
	}
	else {                             /* two-argument function */
		const char *f;

		if (o->wide) {
			f = (2 == o->wide && gen_below(rng, 6) == 0)
			    ? FN2[2] : MINMAX[gen_below(rng, 2)];
		}
		else {
			f = FN2[o->smooth ? 2 : gen_below(rng, 3)];
		}

		put(o, f);
		put(o, "(");
		node(rng, o, depth - 1, nvars);
		put(o, ", ");
		rhs(rng, o, depth - 1, nvars);
		put(o, ")");
	}
	if (o->reuse && !o->overflow && o->len - start < POOL_LEN) {
		/* offer this subtree for reuse (keep or replace a slot) */
		int slot = -1;

		if (o->npool < POOL_N && gen_below(rng, 2)) {
			slot = o->npool++;
		}
		else if (o->npool && gen_below(rng, 4) == 0) {
			slot = (int)gen_below(rng, (uint64_t)o->npool);
		}
		if (0 <= slot) {
			memcpy(o->pool[slot], o->buf + start, o->len - start);
			o->pool[slot][o->len - start] = 0;
		}
	}
}

int
gen_expr_opts(uint64_t *rng, char *buf, size_t cap, const struct gen_opts *op)
{
	static struct out o;               /* large: not on the stack */

	o.buf = buf;
	o.cap = cap;
	o.len = 0;
	o.smooth = op->smooth;
	o.wide = op->wide;
	o.reuse = op->reuse;
	o.npool = 0;
	o.overflow = 0;
	if (cap) {
		buf[0] = 0;
	}
	node(rng, &o, op->max_depth, op->nvars < 1 ? 1 : op->nvars > 8 ? 8 : op->nvars);
	return o.overflow ? -1 : 0;
}

void
gen_expr_ex(uint64_t *rng, char *buf, size_t cap, int max_depth, int nvars,
	    int smooth)
{
	struct out o;

	if (nvars < 1) {
		nvars = 1;
	}
	if (nvars > 3) {
		nvars = 3;
	}
	o.buf = buf;
	o.cap = cap;
	o.len = 0;
	o.smooth = smooth;
	o.wide = o.reuse = o.npool = o.overflow = 0;
	if (cap) {
		buf[0] = 0;
	}
	node(rng, &o, max_depth, nvars);
}

void
gen_expr(uint64_t *rng, char *buf, size_t cap, int max_depth, int nvars)
{
	gen_expr_ex(rng, buf, cap, max_depth, nvars, 0);
}
