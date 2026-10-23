/* Cross-backend differential fuzzer with failure shrinking.
 *
 * usage: difftest [--seed S] [--n N] [--gcc-every K] [--max-depth D]
 *
 * For every random expression (and for its derivative with respect to its
 * first variable, which is the only way to reach the internal step() op) the
 * reference is the interpreter on the unoptimised IR (-O0). It is compared,
 * bit for bit (any NaN equals any NaN), with: interpreter -O2, x86 -O0,
 * x86 -O2 and, for every K-th expression, the gcc backend (-O2). Four
 * variable vectors per expression, half of the values special (0, -0, inf,
 * nan, huge, tiny...). Values are drawn per variable NAME so that shrinking
 * does not move them.
 *
 * Expression shapes rotate over four modes: plain (depth D, x/y/z), plain
 * with up to 8 variables and reused subtrees, and two "wide" modes (depth
 * 9..13, 8 variables, reused subtrees, no calls / rare calls) that push the
 * x86 register allocator past xmm15 and into spilling. D only sets the
 * depth of the plain modes.
 *
 * On a mismatch the failing expression is shrunk: any subtree is replaced by
 * one of its children, or by 1, 0 or x, while the mismatch persists. Then the
 * seed, both expressions, the inputs and every backend's result (%a) are
 * printed and the exit status is 1.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gen.h"
#include "jit.h"

#define NVEC 4
#define NVAR 8
#define NCFG 5
#define SRC_CAP (1 << 16)
#define MAX_NODES (1 << 16)

static const char *const VNAME[NVAR] = { "x", "y", "z", "a", "b", "c", "d", "f" };
static const char *const CFG[NCFG] = {
	"interp -O0 (reference)", "interp -O2", "x86 -O0", "x86 -O2", "gcc -O2"
};

static const double SPECIAL[] = {
	0.0, -0.0, 1.0, -1.0, 0.5, 2.0, 1e308, 1e-308, -1e-308,
	INFINITY, -INFINITY, NAN
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

/* ---------------------------------------------------------------- tree */

enum nk { N_LEAF, N_NEG, N_BIN, N_F1, N_F2 };

struct tn {
	enum nk k;
	char txt[24];              /* leaf text, operator or function name */
	struct tn *c[2];
};

static struct tn nodes[MAX_NODES];
static int nnodes;

static struct tn *
mk(enum nk k, const char *txt)
{
	struct tn *t;

	if (nnodes >= MAX_NODES) {
		return NULL;
	}
	t = &nodes[nnodes++];
	memset(t, 0, sizeof (*t));
	t->k = k;
	snprintf(t->txt, sizeof (t->txt), "%s", txt);
	return t;
}

static void
skip(const char **p)
{
	while (' ' == **p) {
		++*p;
	}
}

/* Parses exactly the grammar gen.c writes. NULL when it does not match. */
static struct tn *
parse(const char **p)
{
	struct tn *t, *a, *b;
	char tok[24];
	int n = 0;

	skip(p);
	if ('(' == **p) {
		++*p;
		skip(p);
		if ('-' == **p) {
			++*p;
			if (!(a = parse(p)) || ')' != *(*p)++ || !(t = mk(N_NEG, "-"))) {
				return NULL;
			}
			t->c[0] = a;
			return t;
		}
		if (!(a = parse(p))) {
			return NULL;
		}
		skip(p);
		tok[0] = *(*p)++;
		tok[1] = 0;
		if (!strchr("+-*/^", tok[0]) || !(b = parse(p))) {
			return NULL;
		}
		skip(p);
		if (')' != *(*p)++ || !(t = mk(N_BIN, tok))) {
			return NULL;
		}
		t->c[0] = a;
		t->c[1] = b;
		return t;
	}
	if (**p >= '0' && **p <= '9') {
		while ((**p >= '0' && **p <= '9') || '.' == **p) {
			if (n < 22) {
				tok[n++] = **p;
			}
			++*p;
		}
		if (('e' == **p || 'E' == **p) &&
		    (((*p)[1] >= '0' && (*p)[1] <= '9') ||
		     (('-' == (*p)[1] || '+' == (*p)[1]) && (*p)[2] >= '0' && (*p)[2] <= '9'))) {
			tok[n++] = *(*p)++;
			tok[n++] = *(*p)++;
			while (**p >= '0' && **p <= '9') {
				if (n < 22) {
					tok[n++] = **p;
				}
				++*p;
			}
		}
		tok[n] = 0;
		return mk(N_LEAF, tok);
	}
	if ((**p >= 'a' && **p <= 'z') || (**p >= 'A' && **p <= 'Z')) {
		while ((**p >= 'a' && **p <= 'z') || (**p >= 'A' && **p <= 'Z') ||
		       (**p >= '0' && **p <= '9')) {
			if (n < 22) {
				tok[n++] = **p;
			}
			++*p;
		}
		tok[n] = 0;
		if ('(' != **p) {
			return mk(N_LEAF, tok);
		}
		++*p;
		if (!(a = parse(p))) {
			return NULL;
		}
		skip(p);
		if (',' == **p) {
			++*p;
			if (!(b = parse(p)) || (skip(p), ')' != *(*p)++) ||
			    !(t = mk(N_F2, tok))) {
				return NULL;
			}
			t->c[0] = a;
			t->c[1] = b;
			return t;
		}
		if (')' != *(*p)++ || !(t = mk(N_F1, tok))) {
			return NULL;
		}
		t->c[0] = a;
		return t;
	}
	return NULL;
}

/* Appends the tree as source text; returns the new length (0 on overflow). */
static size_t
print(const struct tn *t, char *buf, size_t len, size_t cap)
{
	size_t n;

	if (!len || len + 64 > cap) {
		return 0;
	}
	switch (t->k) {
	case N_LEAF:
		n = (size_t)sprintf(buf + len, "%s", t->txt);
		return len + n;
	case N_NEG:
		len += (size_t)sprintf(buf + len, "(-");
		if (!(len = print(t->c[0], buf, len, cap))) {
			return 0;
		}
		return len + (size_t)sprintf(buf + len, ")");
	case N_BIN:
		len += (size_t)sprintf(buf + len, "(");
		if (!(len = print(t->c[0], buf, len, cap))) {
			return 0;
		}
		len += (size_t)sprintf(buf + len, " %s ", t->txt);
		if (!(len = print(t->c[1], buf, len, cap))) {
			return 0;
		}
		return len + (size_t)sprintf(buf + len, ")");
	case N_F1:
		len += (size_t)sprintf(buf + len, "%s(", t->txt);
		if (!(len = print(t->c[0], buf, len, cap))) {
			return 0;
		}
		return len + (size_t)sprintf(buf + len, ")");
	default:
		len += (size_t)sprintf(buf + len, "%s(", t->txt);
		if (!(len = print(t->c[0], buf, len, cap))) {
			return 0;
		}
		len += (size_t)sprintf(buf + len, ", ");
		if (!(len = print(t->c[1], buf, len, cap))) {
			return 0;
		}
		return len + (size_t)sprintf(buf + len, ")");
	}
}

/* --------------------------------------------------------- evaluation */

struct report {
	int vec;                   /* first mismatching vector */
	double in[NVAR];           /* its inputs, by variable name */
	double r[NCFG];            /* each configuration's result */
	int have[NCFG];
	int nvars;
	const char *first;         /* first variable (the derivative's) */
	char firstname[32];
};

enum { EV_OK = 0, EV_MISMATCH = 1, EV_SKIP = 2, EV_ERROR = 3 };

static long g_evals;           /* function evaluations (main pass only) */
static int g_stats;            /* count coverage in eval_case */
static int g_maxxmm = -1;
static long g_x86_compiles, g_slot_compiles, g_spill_compiles;
static char g_errmsg[256];

static jit_expr *
build(const char *src, int deriv, int level)
{
	struct jit_error err;
	jit_expr *e = jit_parse(src, 0, &err), *d;
	char var[32];

	if (!e) {
		return NULL;
	}
	if (deriv) {
		snprintf(var, sizeof (var), "%s", jit_var_name(e, 0));
		d = jit_diff(e, var, &err);
		jit_free(e);
		if (!(e = d)) {
			return NULL;
		}
	}
	if (level && jit_optimize(e, level)) {
		jit_free(e);
		return NULL;
	}
	return e;
}

static void
scan_x86(const jit_fn *fn)
{
	char *t = NULL;
	size_t s = 0;
	FILE *f = open_memstream(&t, &s);
	const char *q;
	int spill = 0;

	if (!f) {
		return;
	}
	jit_fn_dump(fn, "asm", f);
	fclose(f);
	for (q = t; (q = strstr(q, "xmm")); q += 3) {
		int v = atoi(q + 3);

		if (v > g_maxxmm) {
			g_maxxmm = v;
		}
	}
	if (strstr(t, "[rbp-")) {
		spill = 1;
	}
	++g_x86_compiles;
	g_slot_compiles += spill;
	/* no call in the code: slots are there only because 16 registers ran out */
	g_spill_compiles += spill && !strstr(t, "call ");
	free(t);
}

static int
vindex(const char *name)
{
	int i;

	for (i = 0; i < NVAR; ++i) {
		if (!strcmp(VNAME[i], name)) {
			return i;
		}
	}
	return 0;
}

/*
 * Compiles src (or its derivative w.r.t. its first variable when deriv) on
 * every configuration and compares them on the NVEC named-variable vectors.
 * Fills *rep for the first mismatch.
 */
static int
eval_case(const char *src, int deriv, int usegcc,
	  double named[NVEC][NVAR], struct report *rep)
{
	jit_expr *e0 = NULL, *e2 = NULL, *probe;
	jit_fn *fx0 = NULL, *fx2 = NULL, *fg = NULL;
	struct jit_error err;
	int rc = EV_OK, v, c, k;

	if (!(probe = jit_parse(src, 0, &err))) {
		snprintf(g_errmsg, sizeof (g_errmsg), "parse: %s", err.msg);
		return EV_ERROR;
	}
	k = jit_nvars(probe);
	jit_free(probe);
	if (deriv && k < 1) {
		return EV_SKIP;
	}
	if (!(e0 = build(src, deriv, 0)) || !(e2 = build(src, deriv, 2))) {
		snprintf(g_errmsg, sizeof (g_errmsg), "build (diff/optimize) failed");
		rc = EV_ERROR;
		goto done;
	}
	if (!(fx0 = jit_compile(e0, JIT_X86, &err)) ||
	    !(fx2 = jit_compile(e2, JIT_X86, &err))) {
		snprintf(g_errmsg, sizeof (g_errmsg), "x86 compile: %s", err.msg);
		rc = EV_ERROR;
		goto done;
	}
	if (usegcc && !(fg = jit_compile(e2, JIT_GCC, &err))) {
		snprintf(g_errmsg, sizeof (g_errmsg), "gcc compile: %s", err.msg);
		rc = EV_ERROR;
		goto done;
	}
	if (g_stats) {
		scan_x86(fx0);
		scan_x86(fx2);
	}
	for (v = 0; v < NVEC; ++v) {
		double vars[JIT_MAX_VARS] = { 0 }, r[NCFG];
		int bad = 0;

		for (k = 0; k < jit_nvars(e0); ++k) {
			vars[k] = named[v][vindex(jit_var_name(e0, k))];
		}
		r[0] = jit_interp(e0, vars);
		r[1] = jit_interp(e2, vars);
		r[2] = jit_call(fx0, vars);
		r[3] = jit_call(fx2, vars);
		r[4] = fg ? jit_call(fg, vars) : r[0];
		if (g_stats) {
			g_evals += fg ? 5 : 4;
		}
		for (c = 1; c < NCFG; ++c) {
			if (!same(r[0], r[c])) {
				bad = 1;
			}
		}
		if (bad && EV_OK == rc) {
			rc = EV_MISMATCH;
			if (rep) {
				memset(rep, 0, sizeof (*rep));
				rep->vec = v;
				memcpy(rep->in, named[v], sizeof (rep->in));
				for (c = 0; c < NCFG; ++c) {
					rep->r[c] = r[c];
					rep->have[c] = 4 != c || fg;
				}
				rep->nvars = jit_nvars(e0);
				snprintf(rep->firstname, sizeof (rep->firstname), "%s",
					 rep->nvars ? jit_var_name(e0, 0) : "");
			}
		}
	}
done:
	jit_fn_free(fx0);
	jit_fn_free(fx2);
	jit_fn_free(fg);
	if (e0) {
		jit_free(e0);
	}
	if (e2) {
		jit_free(e2);
	}
	return rc;
}

/* --------------------------------------------------------- shrinking */

struct ctx {
	int deriv, usegcc;
	double named[NVEC][NVAR];
};

static struct ctx g_ctx;
static char g_text[SRC_CAP];

static int
still_fails(const struct tn *root)
{
	if (!print(root, g_text, 1, sizeof (g_text))) {
		return 0;
	}
	/* print() needs a non-zero start; it wrote at offset 1, so use g_text + 1 */
	return EV_MISMATCH == eval_case(g_text + 1, g_ctx.deriv, g_ctx.usegcc,
					g_ctx.named, NULL);
}

static void
collect(struct tn *t, struct tn **list, int *n)
{
	list[(*n)++] = t;
	if (t->c[0]) {
		collect(t->c[0], list, n);
	}
	if (t->c[1]) {
		collect(t->c[1], list, n);
	}
}

static struct tn *list[MAX_NODES];

static void
shrink(struct tn *root)
{
	int progress = 1;

	while (progress) {
		int n = 0, i, j;

		progress = 0;
		collect(root, list, &n);
		for (i = 0; i < n && !progress; ++i) {
			struct tn *t = list[i], saved = *t;
			struct tn cand[5];
			int nc = 0;

			if (t->c[0]) {
				cand[nc++] = *t->c[0];
			}
			if (t->c[1]) {
				cand[nc++] = *t->c[1];
			}
			if (N_LEAF != t->k || (strcmp(t->txt, "1") && strcmp(t->txt, "0") &&
					       strcmp(t->txt, "x"))) {
				static const char *const LEAVES[] = { "1", "0", "x" };

				for (j = 0; j < 3; ++j) {
					memset(&cand[nc], 0, sizeof (cand[nc]));
					cand[nc].k = N_LEAF;
					snprintf(cand[nc].txt, sizeof (cand[nc].txt), "%s",
						 LEAVES[j]);
					++nc;
				}
			}
			for (j = 0; j < nc; ++j) {
				*t = cand[j];
				if (still_fails(root)) {
					progress = 1;
					break;
				}
				*t = saved;
			}
		}
	}
}

static void
show_inputs(const double *in, const struct report *rep)
{
	int i;

	for (i = 0; i < NVAR; ++i) {
		printf("    %s = %.17g (%a)\n", VNAME[i], in[i], in[i]);
	}
	(void)rep;
}

static void
fail_report(uint64_t seed, long idx, const char *orig, int deriv, int usegcc,
	    struct tn *root)
{
	struct report rep;
	char minimal[SRC_CAP];
	int c;

	g_stats = 0;
	shrink(root);
	if (!print(root, minimal, 1, sizeof (minimal))) {
		strcpy(minimal + 1, "(unprintable)");
	}
	printf("difftest: MISMATCH (seed %llu, expression #%ld, %s)\n",
	       (unsigned long long)seed, idx,
	       deriv ? "derivative w.r.t. its first variable" : "value");
	printf("  original (%zu chars): %.2000s%s\n", strlen(orig), orig,
	       strlen(orig) > 2000 ? " ..." : "");
	printf("  minimal : %s\n", minimal + 1);
	if (EV_MISMATCH == eval_case(minimal + 1, deriv, usegcc, g_ctx.named, &rep)) {
		printf("  %s of the minimal expression", deriv ? "derivative" : "value");
		if (deriv) {
			printf(" (w.r.t. %s)", rep.firstname);
		}
		printf(", vector %d, inputs by name:\n", rep.vec);
		show_inputs(rep.in, &rep);
		for (c = 0; c < NCFG; ++c) {
			if (rep.have[c]) {
				printf("    %-24s %a  (%.17g)\n", CFG[c], rep.r[c], rep.r[c]);
			}
		}
	}
	else {
		printf("  (the minimal expression no longer reproduces: shrinker bug)\n");
	}
	printf("difftest: FAILED\n");
}

/* --------------------------------------------------------------- main */

static int
usage(void)
{
	fprintf(stderr, "usage: difftest [--seed S] [--n N] [--gcc-every K] [--max-depth D]\n");
	return 2;
}

int
main(int argc, char *argv[])
{
	uint64_t seed = 20261023ull, rng;
	long n = 5000, gcc_every = 250, idx, nexpr = 0;
	int max_depth = 6, i;
	static char src[SRC_CAP];

	for (i = 1; i < argc; ++i) {
		if (i + 1 >= argc) {
			return usage();
		}
		if (!strcmp(argv[i], "--seed")) {
			seed = strtoull(argv[++i], NULL, 0);
		}
		else if (!strcmp(argv[i], "--n")) {
			n = atol(argv[++i]);
		}
		else if (!strcmp(argv[i], "--gcc-every")) {
			gcc_every = atol(argv[++i]);
		}
		else if (!strcmp(argv[i], "--max-depth")) {
			max_depth = atoi(argv[++i]);
		}
		else {
			return usage();
		}
	}
	rng = seed;
	g_stats = 1;
	for (idx = 0; idx < n; ++idx) {
		struct gen_opts o;
		int mode = (int)(idx % 4), v, k, deriv, usegcc;
		double named[NVEC][NVAR];
		const char *p;
		struct tn *root;

		o.max_depth = max_depth;
		o.smooth = 0;
		o.wide = 0;
		o.reuse = 0;
		o.nvars = 1 + (int)gen_below(&rng, 3);
		if (1 == mode) {
			o.nvars = 1 + (int)gen_below(&rng, 8);
			o.reuse = 1;
		}
		else if (mode >= 2) {
			o.max_depth = 9 + (int)gen_below(&rng, 5);
			o.nvars = 4 + (int)gen_below(&rng, 5);
			o.wide = mode - 1;
			o.reuse = 1;
		}
		while (gen_expr_opts(&rng, src, sizeof (src), &o)) {
			if (--o.max_depth < 2) {
				fprintf(stderr, "difftest: generator cannot fit its output\n");
				return 2;
			}
		}
		for (v = 0; v < NVEC; ++v) {
			for (k = 0; k < NVAR; ++k) {
				named[v][k] = pick(&rng);
			}
		}
		usegcc = gcc_every > 0 && 0 == idx % gcc_every;
		memcpy(g_ctx.named, named, sizeof (named));
		g_ctx.usegcc = usegcc;
		for (deriv = 0; deriv <= 1; ++deriv) {
			struct report rep;
			int rc;

			g_ctx.deriv = deriv;
			g_stats = 1;
			rc = eval_case(src, deriv, usegcc, named, &rep);
			if (EV_ERROR == rc) {
				printf("difftest: ERROR (seed %llu, expression #%ld, %s): %s\n  %.2000s\n",
				       (unsigned long long)seed, idx,
				       deriv ? "derivative" : "value", g_errmsg, src);
				return 1;
			}
			if (EV_MISMATCH == rc) {
				nnodes = 0;
				p = src;
				if (!(root = parse(&p)) || *p) {
					printf("difftest: MISMATCH (seed %llu, expression #%ld) "
					       "but the shrinker cannot parse it:\n  %.2000s\n",
					       (unsigned long long)seed, idx, src);
					return 1;
				}
				fail_report(seed, idx, src, deriv, usegcc, root);
				return 1;
			}
		}
		++nexpr;
	}
	printf("difftest: coverage: max xmm = %d, x86 compiles: %ld, using stack slots: %ld, register-pressure spills (no call in the code): %ld\n",
	       g_maxxmm, g_x86_compiles, g_slot_compiles, g_spill_compiles);
	printf("difftest: %ld expressions, %ld evaluations, 0 mismatches (seed %llu)\n",
	       nexpr, g_evals, (unsigned long long)seed);
	return 0;
}
