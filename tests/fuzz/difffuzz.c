/* Derivative fuzzer. For random smooth-only expressions (no abs, relu, min,
 * max) at several points:
 *   1. jit_diff's result, evaluated by the interpreter and by the x86
 *      backend, and again after jit_optimize(.., 2), must be bit-identical
 *      (NaN == NaN);
 *   2. it must match the central difference (f(x+h) - f(x-h)) / 2h, with
 *      h = 1e-6 * max(1, |x|): |d - num| / max(1, |d|, |num|) < 1e-5.
 * Points are skipped from check 2 (and counted) when the numeric slope is not
 * trustworthy: a value or the derivative is not finite, |x| > 1e6, the slope
 * at step 256h disagrees with the one at h (relative to the larger slope:
 * curvature, a pole or an oscillation too close); the slope exceeds 1e10 or the
 * function moves by more than 10% of its size over the 256h window (too steep
 * to difference); the function looks exactly flat at both step sizes; an
 * intermediate value is subnormal or not finite; or the rounding noise of the
 * difference quotient (largest intermediate value times epsilon over 2h) is
 * itself above the tolerance.
 * usage: difffuzz [seed [count]] */

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "api.h"
#include "gen.h"
#include "jit.h"

#define NPOINTS 4
#define TOL 1e-5

static int
same(double a, double b)
{
	if (isnan(a) || isnan(b)) {
		return isnan(a) && isnan(b);
	}
	return !memcmp(&a, &b, sizeof (a));
}

static double
pick(uint64_t *rng)
{
	double u = (double)gen_below(rng, 1000001) / 1000000.0;

	/* half the points positive, so log and sqrt are often defined */
	return gen_below(rng, 2) ? 0.1 + 3.9 * u : -3.0 + 6.0 * u;
}

static double
at(const jit_expr *f, const double *base, int var, double x)
{
	double v[JIT_MAX_VARS];

	memcpy(v, base, sizeof (v));
	v[var] = x;
	return jit_interp(f, v);
}

/* Largest |value| over every node of f at x (+inf when some node is not finite
   or is subnormal: precision is lost there). */
static double
max_node(const jit_expr *f, const double *base, int var, double x)
{
	const struct ir *ir = &f->ir;
	double v[JIT_MAX_VARS], m = 0.0, *val;
	int i;

	memcpy(v, base, sizeof (v));
	v[var] = x;
	if (!(val = malloc((size_t)ir->count * sizeof (*val)))) {
		return INFINITY;
	}
	for (i = 0; i < ir->count; ++i) {
		const struct ir_node *nd = &ir->nodes[i];

		if (IR_CONST == nd->op) {
			val[i] = nd->imm;
		}
		else if (IR_VAR == nd->op) {
			val[i] = v[nd->a];
		}
		else {
			val[i] = ir_apply(0, nd->op, val[nd->a],
					  0 <= nd->b ? val[nd->b] : 0.0);
		}
		if (!isfinite(val[i])) {
			m = INFINITY;
		}
		else if (0.0 != val[i] && fabs(val[i]) < DBL_MIN) {
			m = INFINITY;
		}
		else if (fabs(val[i]) > m) {
			m = fabs(val[i]);
		}
	}
	free(val);
	return m;
}

int
main(int argc, char *argv[])
{
	uint64_t seed = argc > 1 ? strtoull(argv[1], NULL, 0) : 20261024ull;
	long count = argc > 2 ? atol(argv[2]) : 5000;
	uint64_t rng = seed;
	long n, mismatches = 0, bad_slope = 0, checked = 0, points = 0;
	long skip_nonfinite = 0, skip_big = 0, skip_unstable = 0, skip_noise = 0;
	char src[8192];

	printf("difffuzz: seed %llu, %ld smooth expressions x %d points\n",
	       (unsigned long long)seed, count, NPOINTS);
	for (n = 0; n < count; ++n) {
		int nv = 1 + (int)gen_below(&rng, 3), var, p, k;
		struct jit_error err;
		jit_expr *f, *d, *d2;
		jit_fn *fn, *fn2;

		gen_expr_ex(&rng, src, sizeof (src), 5, nv, 1);
		if (!(f = jit_parse(src, 0, &err))) {
			printf("FAIL generator produced unparsable source: %s\n  %s\n",
			       err.msg, src);
			return 1;
		}
		if (!jit_nvars(f)) {
			jit_free(f);        /* constant: nothing to differentiate by */
			continue;
		}
		var = (int)gen_below(&rng, (uint64_t)jit_nvars(f));
		if (!(d = jit_diff(f, jit_var_name(f, var), &err))) {
			printf("FAIL jit_diff: %s\n  %s\n", err.msg, src);
			return 1;
		}
		if (!(d2 = jit_diff(f, jit_var_name(f, var), &err)) ||
		    jit_optimize(d2, 2)) {
			printf("FAIL jit_diff / -O2 of the derivative: %s\n", src);
			return 1;
		}
		if (!(fn2 = jit_compile(d2, JIT_X86, &err)) ||
		    !(fn = jit_compile(d, JIT_X86, &err))) {
			printf("FAIL jit_compile of the derivative: %s\n  %s\n",
			       err.msg, src);
			return 1;
		}
		for (p = 0; p < NPOINTS; ++p) {
			double vals[JIT_MAX_VARS] = { 0 };
			double dv2, dx2, x, h, dv, dx, f1, f2, g1, g2, num, num2, err1, noise, f0, scale;

			for (k = 0; k < jit_nvars(f); ++k) {
				vals[k] = pick(&rng);
			}
			x = vals[var];
			++points;
			dv = jit_interp(d, vals);
			dx = jit_call(fn, vals);
			dv2 = jit_interp(d2, vals);
			dx2 = jit_call(fn2, vals);
			if (!same(dv, dx) || !same(dv, dv2) || !same(dv, dx2)) {
				++mismatches;
				printf("FAIL interp/x86/-O2 differ on d/d%s of %s at x=%.17g: "
				       "%.17g, %.17g, %.17g, %.17g\n",
				       jit_var_name(f, var), src, x, dv, dx, dv2, dx2);
			}
			if (fabs(x) > 1e6) {
				++skip_big;
				continue;
			}
			h = 1e-6 * (fabs(x) > 1.0 ? fabs(x) : 1.0);
			f1 = at(f, vals, var, x + h);
			f2 = at(f, vals, var, x - h);
			g1 = at(f, vals, var, x + 256.0 * h);
			g2 = at(f, vals, var, x - 256.0 * h);
			num = (f1 - f2) / (2.0 * h);
			num2 = (g1 - g2) / (512.0 * h);
			if (!isfinite(dv) || !isfinite(num) || !isfinite(num2)) {
				++skip_nonfinite;
				continue;
			}
			f0 = jit_interp(f, vals);
			scale = fmax(fmax(fabs(num), fabs(num2)), fmax(fabs(dv), 1e-6));
			if (fabs(num - num2) > TOL * scale ||
			    fabs(dv) > 1e10 ||
			    fabs(dv) * 256.0 * h > 0.1 * fmax(1.0, fabs(f0))) {
				++skip_unstable;
				continue;
			}
			if (f1 == f2 && g1 == g2 && fabs(dv) >= TOL) {
				++skip_noise;   /* flat at this resolution: a huge term absorbs x */
				continue;
			}
			if (isinf(max_node(d, vals, var, x))) {
				++skip_noise;   /* subnormal or overflowing intermediate */
				continue;
			}
			noise = 4.5e-16 * (max_node(f, vals, var, x + h) +
					   max_node(f, vals, var, x - h)) / (2.0 * h);
			if (noise > 0.1 * TOL * fmax(1.0, fabs(dv))) {
				++skip_noise;
				continue;
			}
			++checked;
			err1 = fabs(dv - num) /
			       fmax(1.0, fmax(fabs(dv), fabs(num)));
			if (!(err1 < TOL)) {
				++bad_slope;
				printf("FAIL d/d%s of %s at x=%.17g: derivative %.17g, "
				       "numeric %.17g\n", jit_var_name(f, var), src, x,
				       dv, num);
				if (bad_slope >= 10) {
					return 1;
				}
			}
		}
		jit_fn_free(fn);
		jit_fn_free(fn2);
		jit_free(d2);
		jit_free(d);
		jit_free(f);
	}
	printf("%s difffuzz: %ld points, %ld slope-checked, skipped %ld non-finite, "
	       "%ld |x|>1e6, %ld unstable slope, %ld rounding noise; "
	       "%ld slope failures, %ld interp/x86 mismatches\n",
	       bad_slope || mismatches ? "FAIL" : "PASS", points, checked,
	       skip_nonfinite, skip_big, skip_unstable, skip_noise,
	       bad_slope, mismatches);
	return bad_slope || mismatches ? 1 : 0;
}
