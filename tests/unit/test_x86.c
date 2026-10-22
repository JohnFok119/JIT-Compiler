/* x86-64 backend: every result bit-identical to the interpreter (NaN: both
 * NaN) — golden expressions, every op/function on special inputs, spills,
 * nested calls, sigmoid, 8 variables, IR_STEP, W^X, snapshot semantics. */

#include "../check.h"
#include "api.h"
#include "ir/ir.h"
#include "runtime/rt.h"
#include <dirent.h>
#include <math.h>
#include <stdlib.h>

static const double INPUTS[] = {
	0.0, -0.0, 1.0, -1.0, 0.5, 2.0, 1e308, 1e-308, INFINITY, -INFINITY, NAN
};
#define NIN ((int)(sizeof (INPUTS) / sizeof (INPUTS[0])))

static int
same(double a, double b)
{
	if (isnan(a) || isnan(b)) {
		return isnan(a) && isnan(b);
	}
	return !memcmp(&a, &b, sizeof (a));
}

/* x86 vs interpreter over the input grid: var0 x var1 over INPUTS, the
 * other variables rotate through INPUTS. One CHECK per call. */
static int
compare(const char *what, jit_expr *e, const jit_fn *fn)
{
	int nv = jit_nvars(e), i, j, k, ok = 1;
	int ni = nv >= 1 ? NIN : 1, nj = nv >= 2 ? NIN : 1;

	for (i = 0; i < ni && ok; ++i) {
		for (j = 0; j < nj && ok; ++j) {
			double v[JIT_MAX_VARS] = { 0 }, want, got, raw;

			for (k = 0; k < nv; ++k) {
				v[k] = INPUTS[(0 == k ? i : 1 == k ? j : i + j + k) % NIN];
			}
			want = jit_interp(e, v);
			got = jit_call(fn, v);
			raw = jit_entry(fn)(v);
			if (!same(want, got) || !same(want, raw)) {
				ok = 0;
				printf("  mismatch %s:", what);
				for (k = 0; k < nv; ++k) {
					printf(" %s=%.17g", jit_var_name(e, k), v[k]);
				}
				printf(" -> interp %.17g, x86 %.17g / %.17g\n",
				       want, got, raw);
			}
		}
	}
	CHECK(ok);
	return ok;
}

/* The listing of fn as a string (caller frees). */
static char *
listing(const jit_fn *fn)
{
	char *text = NULL;
	size_t size = 0;
	FILE *f = open_memstream(&text, &size);

	jit_fn_dump(fn, "asm", f);
	fclose(f);
	return text;
}

static void
check_src(const char *src, int level)
{
	struct jit_error err;
	jit_expr *e = jit_parse(src, 0, &err);
	jit_fn *fn;

	CHECK(e != NULL);
	if (!e) {
		printf("  parse error in %s: %s\n", src, err.msg);
		return;
	}
	CHECK(0 == jit_optimize(e, level));
	fn = jit_compile(e, JIT_X86, &err);
	CHECK(fn != NULL);
	if (fn) {
		compare(src, e, fn);
		jit_fn_free(fn);
	}
	else {
		printf("  compile error in %s: %s\n", src, err.msg);
	}
	jit_free(e);
}

static void
check_both(const char *src)
{
	check_src(src, 0);
	check_src(src, 2);
}

static const char *OPS[] = {
	/* every op and function, variables only */
	"-x", "+x", "abs(x)", "sqrt(x)", "relu(x)", "sin(x)", "cos(x)",
	"tan(x)", "exp(x)", "log(x)", "tanh(x)", "sigmoid(x)",
	"x + y", "x - y", "x * y", "x / y", "min(x, y)", "max(x, y)",
	"pow(x, y)", "x ^ y",
	/* the same value on both sides */
	"x + x", "x - x", "x * x", "x / x", "min(x, x)", "max(x, x)", "pow(x, x)",
	/* constants as memory operands and as loaded first operands */
	"x + 2", "2 - x", "x * 0.5", "1 / x", "x / 0", "min(x, 0)", "max(0, x)",
	"min(x, -0)", "max(x, 1e308)", "min(1e-308, x)", "pow(x, 2)",
	"pow(2, x)", "pow(x, 0.5)", "sqrt(2)", "sqrt(x) + sqrt(4)", "abs(-3)",
	"relu(-x)", "sin(1)", "-(-x)", "x - 0", "x * 1", "x * 2", "x / 4",
	"3", "x", "pi * e", "1e999", "1e999 - 1e999",
	/* nested calls, values live across calls */
	"sin(cos(exp(x)) + log(sqrt(x)))",
	"pow(sin(x), cos(y)) * pow(cos(y), sin(x)) + tanh(pow(x, y))",
	"x * y + sin(x) * (x - y) + cos(y) / (x + y) + sigmoid(x * y)",
	"pow(pow(x, y), pow(y, x))", "pow(y, x) + pow(x, y) - pow(x, x)",
	"sigmoid(x)^2 + relu(-x)",
	/* 8 variables */
	"a*b + c/d - min(e, f) + max(g, h) + pow(a, h) - sin(c)*g + sqrt(abs(d - e))",
};

/* Every golden expression that parses in normal mode (options skipped). */
static void
check_goldens(void)
{
	DIR *d = opendir("tests/golden");
	struct dirent *de;
	int count = 0;

	CHECK(d != NULL);
	if (!d) {
		return;
	}
	while ((de = readdir(d))) {
		char path[512], line[1024], *expr = NULL;
		size_t len = strlen(de->d_name);
		int legacy = 0, skip_next = 0;
		struct jit_error err;
		jit_expr *e;
		FILE *f;

		if (len < 6 || strcmp(de->d_name + len - 5, ".args")) {
			continue;
		}
		snprintf(path, sizeof (path), "tests/golden/%s", de->d_name);
		if (!(f = fopen(path, "r"))) {
			continue;
		}
		while (fgets(line, sizeof (line), f)) {
			line[strcspn(line, "\n")] = '\0';
			if (skip_next) {
				skip_next = 0;
			}
			else if (!strcmp(line, "--legacy")) {
				legacy = 1;
			}
			else if (!strcmp(line, "--emit") || !strcmp(line, "--backend")) {
				skip_next = 1;
			}
			else if (!strcmp(line, "-O0") || !strcmp(line, "-O2")) {
				continue;
			}
			else {
				expr = strdup(line);
				break;
			}
		}
		fclose(f);
		if (!expr || legacy || !(e = jit_parse(expr, 0, &err))) {
			free(expr);
			continue;
		}
		jit_free(e);
		check_both(expr);
		++count;
		free(expr);
	}
	closedir(d);
	printf("  %d golden expressions compared\n", count);
	CHECK(count >= 60);
}

/* sum over i of (x+i)*(y-i) (or sin(x+i)*cos(y-i)), all 24 products built
 * before the adds so they are live at the same time. */
static void
check_spills(int with_calls)
{
	struct jit_error err;
	jit_expr *e = jit_parse("x + y", 0, &err);
	struct ir *ir = &e->ir;
	int x = ir_var(ir, 0), y = ir_var(ir, 1), p[24], i, sum;
	jit_fn *fn;
	char *text;

	for (i = 0; i < 24; ++i) {
		int c = ir_const(ir, i + 1.0);
		int l = ir_binary(ir, IR_ADD, x, c), r = ir_binary(ir, IR_SUB, y, c);
		if (with_calls) {
			l = ir_unary(ir, IR_SIN, l);
			r = ir_unary(ir, IR_COS, r);
		}
		p[i] = ir_binary(ir, IR_MUL, l, r);
	}
	sum = p[0];
	for (i = 1; i < 24; ++i) {
		sum = ir_binary(ir, IR_ADD, sum, p[i]);
	}
	CHECK(0 <= sum);
	ir->root = sum;
	fn = jit_compile(e, JIT_X86, &err);
	CHECK(fn != NULL);
	if (fn) {
		text = listing(fn);
		CHECK(text && strstr(text, "movsd QWORD PTR [rbp-"));    /* spilled */
		free(text);
		compare(with_calls ? "spills with calls" : "spills", e, fn);
		jit_fn_free(fn);
	}
	jit_free(e);
}

/* (x+1)*((x+2)*(...*(x+24))) at -O0: every x+i is live until the end. */
static void
check_deep(void)
{
	char src[1024];
	size_t w = 0;
	int i;

	for (i = 1; i <= 24; ++i) {
		w += (size_t)snprintf(src + w, sizeof (src) - w, "%s(x+%d)",
				      1 == i ? "" : "*(", i);
	}
	for (i = 2; i <= 24; ++i) {
		src[w++] = ')';
	}
	src[w] = '\0';
	check_both(src);
}

/* IR_STEP is internal: build it by hand. */
static void
check_step(void)
{
	static const double want[NIN] = { 0, 0, 1, 0, 1, 1, 1, 1, 1, 0, 0 };
	struct jit_error err;
	jit_expr *e = jit_parse("x + y", 0, &err);
	struct ir *ir = &e->ir;
	int x = ir_var(ir, 0), y = ir_var(ir, 1), i;
	jit_fn *fn;

	ir->root = ir_unary(ir, IR_STEP, x);
	fn = jit_compile(e, JIT_X86, &err);
	CHECK(fn != NULL);
	if (fn) {
		for (i = 0; i < NIN; ++i) {
			double v[2] = { INPUTS[i], 0.0 };
			CHECK_EQ_DBL_BITS(jit_call(fn, v), want[i]);
		}
		compare("step(x)", e, fn);
		jit_fn_free(fn);
	}
	/* step(x - y) * x + step(y): operand live after the step */
	ir->root = ir_binary(ir, IR_ADD,
			     ir_binary(ir, IR_MUL,
				       ir_unary(ir, IR_STEP,
						ir_binary(ir, IR_SUB, x, y)), x),
			     ir_unary(ir, IR_STEP, y));
	fn = jit_compile(e, JIT_X86, &err);
	CHECK(fn != NULL);
	if (fn) {
		compare("step(x - y) * x + step(y)", e, fn);
		jit_fn_free(fn);
	}
	jit_free(e);
}

static void
check_sigmoid(void)
{
	struct jit_error err;
	jit_expr *e = jit_parse("sigmoid(x)", 0, &err);
	jit_fn *fn = jit_compile(e, JIT_X86, &err);
	char *text;
	int i;

	CHECK(fn != NULL);
	if (fn) {
		text = listing(fn);
		CHECK(text && strstr(text, "movabs rax,<jit_rt_sigmoid>"));
		free(text);
		for (i = 0; i < NIN; ++i) {
			double v[1] = { INPUTS[i] };
			double got = jit_call(fn, v), want = jit_rt_sigmoid(INPUTS[i]);
			CHECK(same(got, want));
		}
		jit_fn_free(fn);
	}
	jit_free(e);
}

/* The code page is r-x, never rwx. */
static void
check_wx(void)
{
	struct jit_error err;
	jit_expr *e = jit_parse("x * 2 + sin(x)", 0, &err);
	jit_fn *fn = jit_compile(e, JIT_X86, &err);
	jit_native f = jit_entry(fn);
	unsigned long addr;
	char line[512];
	int found = 0, rwx = 0;
	FILE *maps;

	memcpy(&addr, &f, sizeof (addr));
	maps = fopen("/proc/self/maps", "r");
	CHECK(maps != NULL);
	while (maps && fgets(line, sizeof (line), maps)) {
		unsigned long lo, hi;
		char perms[8];

		if (3 != sscanf(line, "%lx-%lx %7s", &lo, &hi, perms)) {
			continue;
		}
		if (lo <= addr && addr < hi) {
			found = 1;
			CHECK_STR(perms, "r-xp");
		}
		if (!strncmp(perms, "rwx", 3)) {
			rwx = 1;
		}
	}
	if (maps) {
		fclose(maps);
	}
	CHECK(found);
	CHECK(!rwx);
	jit_fn_free(fn);
	jit_free(e);
}

/* API: snapshot, errors, dumps, sizes. */
static void
check_api(void)
{
	struct jit_error err;
	jit_expr *e = jit_parse("x * y - 3", 0, &err);
	jit_fn *fx = jit_compile(e, JIT_X86, &err);
	jit_fn *fi = jit_compile(e, JIT_INTERP, &err);
	double v[2] = { 2.0, 5.0 };
	FILE *null = fopen("/dev/null", "w");

	jit_free(e);    /* both stay valid */
	CHECK(fx && fi);
	CHECK_EQ_DBL_BITS(jit_call(fx, v), 7.0);
	CHECK_EQ_DBL_BITS(jit_call(fi, v), 7.0);
	CHECK(NULL == jit_entry(fi));
	CHECK(jit_fn_code_size(fx) > 0 && 0 == jit_fn_code_size(fx) % 16);
	CHECK(0 == jit_fn_code_size(fi));
	CHECK(0 == jit_fn_dump(fx, "asm", null));
	CHECK(0 == jit_fn_dump(fx, "hex", null));
	CHECK(-1 == jit_fn_dump(fx, "c", null));
	CHECK(-1 == jit_fn_dump(fi, "asm", null));
	fclose(null);
	jit_fn_free(fx);
	jit_fn_free(fi);

	e = jit_parse("1 / 2", 1, &err);
	CHECK(NULL == jit_compile(e, JIT_X86, &err));
	CHECK_STR(err.msg, "legacy mode needs --backend gcc or interp");
	CHECK(-1 == err.pos);
	fi = jit_compile(e, JIT_INTERP, &err);
	CHECK(fi && same(jit_call(fi, NULL), jit_interp(e, NULL)));
	jit_fn_free(fi);
	jit_free(e);

	e = jit_parse("x", 0, &err);
	CHECK(NULL == jit_compile(e, JIT_WASM, &err));
	CHECK_STR(err.msg, "wasm backend arrives in a later version");
	jit_free(e);
}

int
main(void)
{
	size_t i;

	for (i = 0; i < sizeof (OPS) / sizeof (OPS[0]); ++i) {
		check_both(OPS[i]);
	}
	check_goldens();
	check_spills(0);
	check_spills(1);
	check_deep();
	check_step();
	check_sigmoid();
	check_wx();
	check_api();
	return CHECK_REPORT("test_x86.c");
}
