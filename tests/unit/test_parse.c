/* Parser: precedence/associativity table and the position of every error. */

#include "../check.h"
#include "jit.h"
#include <stdlib.h>

static char *
dump(const char *src, const char *stage)
{
	struct jit_error err;
	jit_expr *e = jit_parse(src, 0, &err);
	char *buf = NULL;
	size_t len = 0;
	FILE *f;

	if (!e) {
		return NULL;
	}
	f = open_memstream(&buf, &len);
	if (f) {
		jit_dump(e, stage, f);
		fclose(f);
	}
	jit_free(e);
	return buf;
}

static void
test_values(void)
{
	static const struct { const char *src; double want; } T[] = {
		{ "1+2*3",     7 },   { "2*3+1",     7 },
		{ "(1+2)*3",   9 },   { "8-3-2",     3 },
		{ "8/4/2",     1 },   { "1-2+3",     2 },
		{ "2^3^2",     512 }, { "(2^3)^2",   64 },
		{ "-2^2",      -4 },  { "(-2)^2",    4 },
		{ "2^-1",      0.5 }, { "2^-1^2",    0.5 },
		{ "2*3^2",     18 },  { "2^2*3",     12 },
		{ "-2*3",      -6 },  { "2*-3",      -6 },
		{ "--3",       3 },   { "+5",        5 },
		{ "-(2)^2",    -4 },  { "2^+3",      8 },
		{ "min(1+2, 2*2)", 3 }, { "max(1, 2)^2", 4 },
		{ " 1\t+\n1 ", 2 }
	};
	struct jit_error err;
	unsigned i;

	for (i = 0; i < sizeof (T) / sizeof (T[0]); ++i) {
		jit_expr *e = jit_parse(T[i].src, 0, &err);
		CHECK(e != NULL);
		if (!e) {
			printf("  parse failed: %s (%s)\n", T[i].src, err.msg);
			continue;
		}
		if (T[i].want != jit_interp(e, NULL)) {
			printf("  value of %s: %.17g\n", T[i].src,
			       jit_interp(e, NULL));
		}
		CHECK(T[i].want == jit_interp(e, NULL));
		jit_free(e);
	}
}

static void
test_shapes(void)
{
	static const struct { const char *src, *ast; } T[] = {
		{ "1-2-3",
		  "(sub\n  (sub\n    (num 1)\n    (num 2))\n  (num 3))\n" },
		{ "2^3^2",
		  "(pow\n  (num 2)\n  (pow\n    (num 3)\n    (num 2)))\n" },
		{ "-x^2",
		  "(neg\n  (pow\n    (var x)\n    (num 2)))\n" },
		{ "+x", "(var x)\n" },
		{ "pow(e, 1/2)",
		  "(call pow\n  (const e)\n  (div\n    (num 1)\n    (num 2)))\n" },
		{ "1.5e3", "(num 1500)\n" }
	};
	unsigned i;

	for (i = 0; i < sizeof (T) / sizeof (T[0]); ++i) {
		char *got = dump(T[i].src, "ast");
		CHECK_STR(got, T[i].ast);
		free(got);
	}
}

static void
test_errors(void)
{
	static const struct {
		const char *src;
		int legacy;
		const char *msg;
		int pos;
	} T[] = {
		{ "1 $", 0, "unexpected character '$'", 2 },
		{ "1 # 2 $", 0, "unexpected character '#'", 2 },
		{ "1.2.3", 0, "malformed number", 0 },
		{ "x+1..2", 0, "malformed number", 2 },
		{ ".", 0, "malformed number", 0 },
		{ "1e5.", 0, "malformed number", 0 },
		{ "*2", 0, "expected a number, name or '('", 0 },
		{ "1+", 0, "expected a number, name or '('", 2 },
		{ "()", 0, "expected a number, name or '('", 1 },
		{ "1+*3", 0, "expected a number, name or '('", 2 },
		{ "(1+2", 0, "expected ')'", 4 },
		{ "min(1,2", 0, "expected ')'", 7 },
		{ "sin(1 2)", 0, "expected ')'", 6 },
		{ "1 2", 0, "unexpected '2' after expression", 2 },
		{ "(1))", 0, "unexpected ')' after expression", 3 },
		{ "2e", 0, "unexpected 'e' after expression", 1 },
		{ "x yz", 0, "unexpected 'yz' after expression", 2 },
		{ "foo(1)", 0, "unknown function 'foo'", 0 },
		{ "1+bar(2)", 0, "unknown function 'bar'", 2 },
		{ "sin(1,2)", 0, "'sin' takes 1 argument", 0 },
		{ "min(1)", 0, "'min' takes 2 arguments", 0 },
		{ "pow(1,2,3)", 0, "'pow' takes 2 arguments", 0 },
		{ "1+sin()", 0, "'sin' takes 1 argument", 2 },
		{ "sin + 1", 0, "'sin' is a function", 0 },
		{ "2*max", 0, "'max' is a function", 2 },
		{ "pi(2)", 0, "'pi' is a constant", 0 },
		{ "2*e(1)", 0, "'e' is a constant", 2 },
		{ "a+b+c+d+f+g+h+i+j", 0, "too many variables (max 8)", 16 },
		{ "", 0, "empty expression", 0 },
		{ "  \t\n", 0, "empty expression", 4 },
		{ "X", 0, "names are lowercase", 0 },
		{ "x+sinX(1)", 0, "names are lowercase", 2 },
		{ "abcdefghijabcdefghijabcdefghijab", 0,
		  "name too long (max 31 characters)", 0 },
		{ "1+x", 1, "legacy mode supports numbers and + - * / only", 2 },
		{ "2^2", 1, "legacy mode supports numbers and + - * / only", 1 },
		{ "sin(1)", 1, "legacy mode supports numbers and + - * / only", 0 },
		{ "pi", 1, "legacy mode supports numbers and + - * / only", 0 },
		{ "1 2 $", 0, "unexpected '2' after expression", 2 },
		{ "( $", 0, "unexpected character '$'", 2 }
	};
	unsigned i;

	for (i = 0; i < sizeof (T) / sizeof (T[0]); ++i) {
		struct jit_error err;
		jit_expr *e;

		memset(&err, 0, sizeof (err));
		e = jit_parse(T[i].src, T[i].legacy, &err);
		CHECK(e == NULL);
		jit_free(e);
		CHECK_STR(err.msg, T[i].msg);
		CHECK(T[i].pos == err.pos);
		if (T[i].pos != err.pos) {
			printf("  pos of \"%s\": %d\n", T[i].src, err.pos);
		}
	}
}

static void
test_accepted(void)
{
	static const char *const ok[] = {
		"a+b+c+d+f+g+h+i+a",                 /* 8 distinct */
		"abcdefghijabcdefghijabcdefghija",   /* 31 chars */
		"_x + x_1",
		"1E5 + .5 + 2. + 1e-3 + 1e+3",
		"--legacy-is-not-an-option-here"
	};
	struct jit_error err;
	unsigned i;

	for (i = 0; i < sizeof (ok) / sizeof (ok[0]); ++i) {
		jit_expr *e = jit_parse(ok[i], 0, &err);
		CHECK(e != NULL);
		if (!e) {
			printf("  rejected %s: %s\n", ok[i], err.msg);
		}
		jit_free(e);
	}
}

static void
test_dump_stages(void)
{
	struct jit_error err;
	jit_expr *e = jit_parse("1", 0, &err);
	char *t;

	CHECK(e != NULL);
	if (!e) return;
	CHECK(-1 == jit_dump(e, "bogus", stdout));
	jit_free(e);
	t = dump("2*(x)", "tokens");
	CHECK_STR(t, "0 number 2\n1 op *\n2 lparen (\n3 name x\n"
		     "4 rparen )\n5 end\n");
	free(t);
}

/* Nesting cap (256): groups, calls, unary chains and ^ chains. */
static char *
nested(const char *open, const char *close, const char *core, int n)
{
	size_t lo = strlen(open), lc = strlen(close), lk = strlen(core);
	char *s = malloc((size_t)n * (lo + lc) + lk + 1), *p = s;
	int i;

	for (i = 0; i < n; ++i, p += lo) {
		memcpy(p, open, lo);
	}
	memcpy(p, core, lk);
	p += lk;
	for (i = 0; i < n; ++i, p += lc) {
		memcpy(p, close, lc);
	}
	*p = '\0';
	return s;
}

static void
test_depth(void)
{
	/* per = nesting levels one repetition of open/close costs */
	static const struct { const char *open, *close, *core; int per; } K[] = {
		{ "(", ")", "x", 1 }, { "sin(", ")", "x", 1 }, { "-", "", "x", 1 },
		{ "+", "", "x", 1 }, { "(-sin(", "))", "x", 3 },
	};
	struct jit_error err;
	unsigned i;

	for (i = 0; i < sizeof (K) / sizeof (K[0]); ++i) {
		char *ok = nested(K[i].open, K[i].close, K[i].core,
				  256 / K[i].per);
		char *bad = nested(K[i].open, K[i].close, K[i].core,
				   300 / K[i].per + 1);
		jit_expr *e;

		e = jit_parse(ok, 0, &err);
		CHECK(e != NULL);
		if (e) {
			jit_expr *d = jit_diff(e, "x", &err);

			CHECK(0 == jit_optimize(e, 2));
			CHECK(d != NULL);
			jit_free(d);
			jit_free(e);
		}
		CHECK(NULL == jit_parse(bad, 0, &err));
		CHECK_STR(err.msg, "expression nests too deeply");
		CHECK(err.pos >= 0);
		free(ok);
		free(bad);
	}

	/* exact boundary and error position for plain groups */
	{
		char *ok = nested("(", ")", "1", 256), *bad = nested("(", ")", "1", 257);
		jit_expr *e = jit_parse(ok, 0, &err);

		CHECK(e != NULL);
		jit_free(e);
		CHECK(NULL == jit_parse(bad, 0, &err));
		CHECK_STR(err.msg, "expression nests too deeply");
		CHECK(256 == err.pos);
		free(ok);
		free(bad);
	}

	/* a ^ chain: 2^2^2^... */
	{
		char *s = malloc(2 * 400 + 2), *p = s;
		int n;

		for (n = 0; n < 300; ++n) {
			*p++ = '2';
			*p++ = '^';
		}
		*p++ = '2';
		*p = '\0';
		CHECK(NULL == jit_parse(s, 0, &err));
		CHECK_STR(err.msg, "expression nests too deeply");
		free(s);
	}
}

int
main(void)
{
	test_values();
	test_shapes();
	test_errors();
	test_accepted();
	test_dump_stages();
	test_depth();
	return CHECK_REPORT("test_parse");
}
