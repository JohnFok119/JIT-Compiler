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

struct out {
	char *buf;
	size_t cap, len;
};

static void
put(struct out *o, const char *s)
{
	size_t n = strlen(s);

	if (o->len + n + 1 <= o->cap) {
		memcpy(o->buf + o->len, s, n + 1);
		o->len += n;
	}
}

static void leaf(uint64_t *rng, struct out *o, int nvars);
static void node(uint64_t *rng, struct out *o, int depth, int nvars);

static void
leaf(uint64_t *rng, struct out *o, int nvars)
{
	static const char *const VARS[] = { "x", "y", "z" };

	if (gen_below(rng, 3)) {
		put(o, VARS[gen_below(rng, (uint64_t)nvars)]);
	}
	else {
		put(o, CONSTS[gen_below(rng, NCONSTS)]);
	}
}

/* Second operand: often a "hot" constant, else a subexpression. */
static void
rhs(uint64_t *rng, struct out *o, int depth, int nvars)
{
	if (gen_below(rng, 3) == 0) {
		put(o, HOT[gen_below(rng, NHOT)]);
	}
	else {
		node(rng, o, depth, nvars);
	}
}

static void
node(uint64_t *rng, struct out *o, int depth, int nvars)
{
	static const char *const OPS[] = { "+", "-", "*", "/", "^" };
	uint64_t k;

	if (depth <= 0 || gen_below(rng, 6) == 0) {
		leaf(rng, o, nvars);
		return;
	}
	k = gen_below(rng, 10);
	if (k < 5) {                       /* binary operator */
		put(o, "(");
		node(rng, o, depth - 1, nvars);
		put(o, " ");
		put(o, OPS[gen_below(rng, 5)]);
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
		put(o, FN1[gen_below(rng, NFN1)]);
		put(o, "(");
		node(rng, o, depth - 1, nvars);
		put(o, ")");
	}
	else {                             /* two-argument function */
		const char *f = FN2[gen_below(rng, 3)];

		put(o, f);
		put(o, "(");
		node(rng, o, depth - 1, nvars);
		put(o, ", ");
		rhs(rng, o, depth - 1, nvars);
		put(o, ")");
	}
}

void
gen_expr(uint64_t *rng, char *buf, size_t cap, int max_depth, int nvars)
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
	if (cap) {
		buf[0] = 0;
	}
	node(rng, &o, max_depth, nvars);
}
