/* --bench: compile latency and evaluation speed of every backend. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "jit.h"
#include "bench.h"

#define COMPILES 15
#define RUNS 11
#define NVEC 1024       /* pre-generated variable vectors (power of two) */

static volatile double sink;

static double
now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static int
cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;

	return (x > y) - (x < y);
}

static double
median(double *v, int n)
{
	qsort(v, (size_t)n, sizeof (double), cmp_double);
	return v[n / 2];
}

/* Deterministic values in [0.1, 2.1): positive so that ^, log and sqrt stay real. */
static void
fill_vectors(double *vec, int nvars)
{
	unsigned long long s = 0x9E3779B97F4A7C15ULL;
	int i;

	for (i = 0; i < NVEC * nvars; ++i) {
		s ^= s << 13;
		s ^= s >> 7;
		s ^= s << 17;
		vec[i] = 0.1 + 2.0 * (double)(s >> 11) / 9007199254740992.0;
	}
}

struct row {
	const char *name;
	enum jit_backend backend;
	int opt;
};

int
bench_run(const char *src, long n)
{
	static const struct row ROWS[] = {
		{ "interp-O2", JIT_INTERP, 2 },
		{ "x86-O0", JIT_X86, 0 },
		{ "x86-O2", JIT_X86, 2 },
		{ "gcc", JIT_GCC, 2 },
	};
	struct jit_error err;
	jit_expr *probe;
	double *vec;
	int nvars, r, i, c;

	if (n < 1) {
		fprintf(stderr, "error: bench count must be at least 1\n");
		return 1;
	}
	if (!(probe = jit_parse(src, 0, &err))) {
		fprintf(stderr, "error: %s\n", err.msg);
		return 1;
	}
	nvars = jit_nvars(probe);
	jit_free(probe);
	if (!(vec = malloc(sizeof (double) * (size_t)(NVEC * (nvars ? nvars : 1))))) {
		fprintf(stderr, "error: out of memory\n");
		return 1;
	}
	fill_vectors(vec, nvars ? nvars : 1);

	printf("%-10s %12s %14s\n", "backend", "compile_us", "ns_per_eval");
	for (r = 0; r < 4; ++r) {
		double ctimes[COMPILES], rtimes[RUNS];
		jit_fn *fn = NULL;
		jit_native entry;

		/* compile: optimize (if any) + jit_compile; the parse is not timed */
		for (c = 0; c < COMPILES; ++c) {
			jit_expr *e = jit_parse(src, 0, &err);
			double t0, t1;

			if (!e) {
				free(vec);
				return 1;
			}
			jit_fn_free(fn);
			fn = NULL;
			t0 = now_ns();
			if (ROWS[r].opt && jit_optimize(e, ROWS[r].opt)) {
				fprintf(stderr, "error: out of memory\n");
				free(vec);
				return 1;
			}
			fn = jit_compile(e, ROWS[r].backend, &err);
			t1 = now_ns();
			jit_free(e);
			if (!fn) {
				fprintf(stderr, "error: %s: %s\n", ROWS[r].name, err.msg);
				free(vec);
				return 1;
			}
			ctimes[c] = (t1 - t0) / 1e3;
		}

		entry = jit_entry(fn);
		for (i = 0; i < RUNS + 1; ++i) {     /* run 0 is a warm-up, not kept */
			double acc = 0, t0, t1;
			long k;

			t0 = now_ns();
			if (entry) {
				for (k = 0; k < n; ++k) {
					acc += entry(vec + (k & (NVEC - 1)) * nvars);
				}
			}
			else {
				for (k = 0; k < n; ++k) {
					acc += jit_call(fn, vec + (k & (NVEC - 1)) * nvars);
				}
			}
			t1 = now_ns();
			sink = acc;
			if (i) {
				rtimes[i - 1] = (t1 - t0) / (double)n;
			}
		}
		printf("%-10s %12.1f %14.2f\n", ROWS[r].name, median(ctimes, COMPILES),
		       median(rtimes, RUNS));
		jit_fn_free(fn);
	}
	free(vec);
	return 0;
}
