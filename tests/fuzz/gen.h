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

/*
 * Full options. nvars 1..8 (names x y z a b c d f). wide: 0 = normal mix of
 * operators and calls; 1 = no calls at all (only + - * /, neg, sqrt, abs,
 * relu, min, max: the register allocator never has to save anything across a
 * call); 2 = as 1 but with a call (sin.., pow) about one node in six. wide
 * modes also make leaves rarer, so trees are bushy. reuse != 0 records some
 * finished subtrees and pastes the same text again later, so hash-consing
 * (CSE) keeps those values live over long ranges. Returns 0, or -1 when the
 * text did not fit in cap (buf is then truncated and must not be used).
 */
struct gen_opts {
	int max_depth;
	int nvars;
	int smooth;
	int wide;
	int reuse;
};
int gen_expr_opts(uint64_t *rng, char *buf, size_t cap, const struct gen_opts *o);

#endif /* FUZZ_GEN_H */
