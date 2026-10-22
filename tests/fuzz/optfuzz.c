/* Optimizer fuzzer: interpreter on unoptimized vs optimized IR must be
 * bit-identical (NaN == NaN). usage: optfuzz [seed [count]] */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gen.h"
#include "jit.h"

static const double SPECIAL[] = {
	0.0, -0.0, 1.0, -1.0, 0.5, 1e308, -1e-308, INFINITY, -INFINITY, NAN
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
pick(uint64_t *rng)
{
	if (gen_below(rng, 2)) {
		return SPECIAL[gen_below(rng, NSPECIAL)];
	}
	return ((double)gen_below(rng, 2000001) - 1000000.0) / 100000.0;
}

int
main(int argc, char *argv[])
{
	uint64_t seed = argc > 1 ? strtoull(argv[1], NULL, 0) : 20261022ull;
	long count = argc > 2 ? atol(argv[2]) : 20000;
	uint64_t rng = seed;
	long n, failures = 0, compared = 0;
	char src[8192];

	printf("optfuzz: seed %llu, %ld expressions x 5 vectors\n",
	       (unsigned long long)seed, count);
	for (n = 0; n < count; ++n) {
		struct jit_error err;
		jit_expr *a, *b;
		int nv = 1 + (int)gen_below(&rng, 3), v, k;

		gen_expr(&rng, src, sizeof (src), 6, nv);
		a = jit_parse(src, 0, &err);
		b = jit_parse(src, 0, &err);
		if (!a || !b) {
			printf("FAIL generator produced unparsable source: %s\n  %s\n",
			       err.msg, src);
			return 1;
		}
		if (jit_optimize(b, 2)) {
			printf("FAIL jit_optimize failed: %s\n", src);
			return 1;
		}
		for (v = 0; v < 5; ++v) {
			double vars[JIT_MAX_VARS] = { 0 }, r0, r1;

			for (k = 0; k < jit_nvars(a); ++k) {
				vars[k] = pick(&rng);
			}
			r0 = jit_interp(a, vars);
			r1 = jit_interp(b, vars);
			++compared;
			if (!same(r0, r1)) {
				++failures;
				printf("FAIL %s\n", src);
				for (k = 0; k < jit_nvars(a); ++k) {
					printf("  %s = %.17g\n", jit_var_name(a, k), vars[k]);
				}
				printf("  unoptimized %.17g, optimized %.17g\n", r0, r1);
				if (failures >= 10) {
					return 1;
				}
			}
		}
		jit_free(a);
		jit_free(b);
	}
	printf("%s optfuzz: %ld expressions, %ld comparisons, %ld mismatches\n",
	       failures ? "FAIL" : "PASS", count, compared, failures);
	return failures ? 1 : 0;
}
