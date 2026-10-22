/* Optimizer: one test per rule, the cases that must NOT fire, bitwise results. */

#include "../check.h"
#include "api.h"
#include "ir/ir.h"
#include <math.h>
#include <stdlib.h>

static const double INPUTS[] = {
	0.0, -0.0, 1.0, -1.0, 0.5, 1e308, -1e-308, INFINITY, -INFINITY, NAN
};
#define NINPUTS (sizeof (INPUTS) / sizeof (INPUTS[0]))

/* IR text of src after optimizing at `level` (caller frees). */
static char *
ir_text(const char *src, int level)
{
	struct jit_error err;
	jit_expr *e = jit_parse(src, 0, &err);
	char *text = NULL;
	size_t size = 0;
	FILE *f;

	if (!e || jit_optimize(e, level)) {
		jit_free(e);
		return strdup("PARSE/OPT ERROR");
	}
	f = open_memstream(&text, &size);
	jit_dump(e, "ir", f);
	fclose(f);
	jit_free(e);
	return text;
}

static int
same(double a, double b)
{
	uint64_t x, y;

	if (isnan(a) || isnan(b)) {
		return isnan(a) && isnan(b);
	}
	memcpy(&x, &a, sizeof (x));
	memcpy(&y, &b, sizeof (y));
	return x == y;
}

/* Optimized result is bit-identical to unoptimized on every input (x and y). */
static void
check_bits(const char *src)
{
	struct jit_error err;
	jit_expr *a = jit_parse(src, 0, &err);
	jit_expr *b = jit_parse(src, 0, &err);
	size_t i, j;
	int ok = 1;

	CHECK(a && b && 0 == jit_optimize(b, 2));
	if (!a || !b) {
		jit_free(a);
		jit_free(b);
		return;
	}
	for (i = 0; i < NINPUTS; ++i) {
		for (j = 0; j < NINPUTS; ++j) {
			double v[JIT_MAX_VARS] = { INPUTS[i], INPUTS[j] };

			if (!same(jit_interp(a, v), jit_interp(b, v))) {
				ok = 0;
				printf("  mismatch: %s at %.17g, %.17g\n", src,
				       INPUTS[i], INPUTS[j]);
			}
		}
	}
	CHECK(ok);
	jit_free(a);
	jit_free(b);
}

/* Optimized IR text of src equals want; results stay bit-identical. */
static void
rule(const char *src, const char *want)
{
	char *got = ir_text(src, 2);

	CHECK_STR(got, want);
	free(got);
	check_bits(src);
}

/* Optimizing leaves the IR exactly as at -O0 (the rule must not fire). */
static void
no_change(const char *src)
{
	char *o0 = ir_text(src, 0);
	char *o2 = ir_text(src, 2);

	CHECK_STR(o2, o0);
	free(o0);
	free(o2);
	check_bits(src);
}

#define X1 "%0 = var x\nret %0\n"

static void
test_fold(void)
{
	rule("1+2*3", "%0 = const 7\nret %0\n");
	rule("sin(0)", "%0 = const 0\nret %0\n");
	rule("pow(2, 10)", "%0 = const 1024\nret %0\n");
	rule("1/0", "%0 = const inf\nret %0\n");
	rule("(1e999-1e999)", "%0 = const nan\nret %0\n");
	rule("-(0)", "%0 = const -0\nret %0\n");
	rule("sigmoid(0)", "%0 = const 0.5\nret %0\n");
	rule("max(1, 2) + min(3, 4)", "%0 = const 5\nret %0\n");
	rule("x * (2+3)", "%0 = var x\n%1 = const 5\n%2 = mul %0, %1\nret %2\n");
}

static void
test_identities(void)
{
	rule("x*1", X1);
	rule("1*x", X1);
	rule("x/1", X1);
	rule("x-0", X1);
	rule("-(-x)", X1);
	rule("x+(-0)", X1);
	rule("(-0)+x", X1);
	rule("x+(0-0)*5", "%0 = var x\n%1 = const 0\n%2 = add %0, %1\nret %2\n");
	/* nested: the fixed point keeps going */
	rule("(x*1)*1/1", X1);
	rule("-(-(-(-x)))", X1);
}

static void
test_strength(void)
{
	rule("x*2", "%0 = var x\n%1 = add %0, %0\nret %1\n");
	rule("2*x", "%0 = var x\n%1 = add %0, %0\nret %1\n");
	rule("pow(x, 1)", X1);
	rule("x/4", "%0 = var x\n%1 = const 0.25\n%2 = mul %0, %1\nret %2\n");
	rule("x/(-0.5)", "%0 = var x\n%1 = const -2\n%2 = mul %0, %1\nret %2\n");
	/* x/0.5 -> x*2 -> x+x across rounds */
	rule("x/0.5", "%0 = var x\n%1 = add %0, %0\nret %1\n");
	rule("x/2", "%0 = var x\n%1 = const 0.5\n%2 = mul %0, %1\nret %2\n");
}

/* Regression: glibc 2.31 pow(z, 2) != z * z at this z; -O2 must keep pow. */
static void
test_pow_two_regression(void)
{
	struct jit_error err;
	jit_expr *a = jit_parse("pow(z, 2)", 0, &err);
	jit_expr *b = jit_parse("pow(z, 2)", 0, &err);
	double v[JIT_MAX_VARS] = { 1.0175903746940147 };

	CHECK(a && b && 0 == jit_optimize(b, 2));
	if (a && b) {
		CHECK_EQ_DBL_BITS(jit_interp(a, v), jit_interp(b, v));
	}
	jit_free(a);
	jit_free(b);
}

static void
test_must_not_fire(void)
{
	/* -0 - -0 = +0; only the constant (-0) folds */
	rule("x - (-0)", "%0 = var x\n%1 = const -0\n%2 = sub %0, %1\nret %2\n");
	no_change("x + 0");        /* -0 + +0 = +0 */
	no_change("0 + x");
	no_change("x * 0");        /* inf*0 = nan, -1*0 = -0 */
	no_change("0 * x");
	no_change("pow(x, 2)");    /* glibc pow(z, 2) is not always z * z */
	no_change("x^2");
	no_change("pow(x, 3)");    /* repeated multiplication is not bit-identical */
	no_change("x^3");
	no_change("x / 3");        /* not a power of two */
	no_change("x / 0");        /* 1/0 = inf, not a normal reciprocal */
	rule("x / (-0)", "%0 = var x\n%1 = const -0\n%2 = div %0, %1\nret %2\n");
	no_change("pow(x, 0.5)");  /* sqrt differs for -0 and -inf */
	no_change("pow(x, 0)");
	no_change("x / 1e308");    /* 1/c is a normal number but c is not a power of two */
	no_change("x * 3");
	no_change("0 - x");
	no_change("x - y");
	no_change("x * y");
	/* c normal but 1/c subnormal: 2^1023 -> 2^-1023 */
	no_change("x / 8.98846567431158e307");
	/* c subnormal */
	no_change("x / 5e-324");
}

static void
test_dce_and_cse(void)
{
	/* dead nodes vanish; shared nodes stay shared */
	rule("x*1 + sin(x)*1", "%0 = var x\n%1 = sin %0\n%2 = add %0, %1\nret %2\n");
	rule("(x*1)*(x*1)", "%0 = var x\n%1 = mul %0, %0\nret %1\n");
	rule("sin(x)*2 - sin(x)*2", "%0 = var x\n%1 = sin %0\n%2 = add %1, %1\n%3 = sub %2, %2\nret %3\n");
}

static void
test_levels(void)
{
	struct jit_error err;
	jit_expr *e = jit_parse("x*1", 0, &err);
	jit_expr *l = jit_parse("1/2", 1, &err);
	char *t;

	CHECK(0 == jit_optimize(e, 0));
	t = ir_text("x*1", 0);
	CHECK_STR(t, "%0 = var x\n%1 = const 1\n%2 = mul %0, %1\nret %2\n");
	free(t);
	CHECK(-1 == jit_optimize(e, 1));
	CHECK(-1 == jit_optimize(e, 3));
	/* legacy expressions are never rewritten */
	CHECK(0 == jit_optimize(l, 2));
	{
		FILE *f;
		char *text = NULL;
		size_t size = 0;

		f = open_memstream(&text, &size);
		jit_dump(l, "ir", f);
		fclose(f);
		CHECK_STR(text, "%0 = const 1\n%1 = const 2\n%2 = div %0, %1\nret %2\n");
		free(text);
	}
	jit_free(e);
	jit_free(l);
}

static void
test_formatter(void)
{
	char buf[32];

	ir_fmt_double(buf, sizeof (buf), 1e-320);
	CHECK_STR(buf, "1e-320");
	ir_fmt_double(buf, sizeof (buf), 100.0);
	CHECK_STR(buf, "100");
	ir_fmt_double(buf, sizeof (buf), 0.1);
	CHECK_STR(buf, "0.1");
	ir_fmt_double(buf, sizeof (buf), -0.0);
	CHECK_STR(buf, "-0");
	ir_fmt_double(buf, sizeof (buf), -INFINITY);
	CHECK_STR(buf, "-inf");
	ir_fmt_double(buf, sizeof (buf), 5e-324);
	CHECK_STR(buf, "5e-324");
}

int
main(void)
{
	test_fold();
	test_identities();
	test_strength();
	test_must_not_fire();
	test_pow_two_regression();
	test_dce_and_cse();
	test_levels();
	test_formatter();
	return CHECK_REPORT("test_opt");
}
