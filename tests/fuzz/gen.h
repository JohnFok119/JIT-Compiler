/* Seeded random expression generator (shared by the optimizer fuzzer and the
 * later cross-backend fuzzer). */

#ifndef FUZZ_GEN_H
#define FUZZ_GEN_H

#include <stddef.h>
#include <stdint.h>

/* splitmix64: returns the next 64 random bits and advances *rng. */
uint64_t gen_next(uint64_t *rng);

/* Uniform in [0, n). */
uint64_t gen_below(uint64_t *rng, uint64_t n);

/*
 * Writes the source text of a random expression into buf (NUL terminated,
 * parenthesised everywhere so it always parses): depth <= max_depth, all
 * operators and functions, variables x, y, z (nvars 1..3). The language has
 * no inf/nan literals, so they are spelled 1e999 and (1e999-1e999).
 */
void gen_expr(uint64_t *rng, char *buf, size_t cap, int max_depth, int nvars);

/* As gen_expr; smooth != 0 leaves out abs, relu, min and max. */
void gen_expr_ex(uint64_t *rng, char *buf, size_t cap, int max_depth, int nvars,
		 int smooth);

#endif /* FUZZ_GEN_H */
