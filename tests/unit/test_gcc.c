/* GCC backend: every result bit-identical to the interpreter (NaN: both
 * NaN) — golden expressions, every op/function on special inputs, legacy
 * mode, the generated C, gcc failure path, temp-dir cleanup, snapshots. */

#include "../check.h"
#include "api.h"
#include "backend/gcc/cgen.h"
#include "backend/gcc/loader.h"
#include "ir/ir.h"
#include <dirent.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

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

/* gcc vs interpreter over the input grid (see test_x86.c). One CHECK. */
static int
compare(const char *what, jit_expr *e, const jit_fn *fn)
{
	int nv = jit_nvars(e), i, j, k, ok = 1;
	int ni = nv >= 1 ? NIN : 1, nj = nv >= 2 ? NIN : 1;

	for (i = 0; i < ni && ok; ++i) {
		for (j = 0; j < nj && ok; ++j) {
			double v[JIT_MAX_VARS] = { 0 }, want, got;

			for (k = 0; k < nv; ++k) {
				v[k] = INPUTS[(0 == k ? i : 1 == k ? j : i + j + k) % NIN];
			}
			want = jit_interp(e, v);
			got = jit_call(fn, v);
			if (!same(want, got)) {
				ok = 0;
				printf("  mismatch %s:", what);
				for (k = 0; k < nv; ++k) {
					printf(" %s=%.17g", jit_var_name(e, k), v[k]);
				}
				printf(" -> interp %.17g, gcc %.17g\n", want, got);
			}
		}
	}
	CHECK(ok);
	return ok;
}

static void
check_src(const char *src, int legacy, int level)
{
	struct jit_error err;
	jit_expr *e = jit_parse(src, legacy, &err);
	jit_fn *fn;

	CHECK(e != NULL);
	if (!e) {
		printf("  parse error in %s: %s\n", src, err.msg);
		return;
	}
	CHECK(0 == jit_optimize(e, level));
	fn = jit_compile(e, JIT_GCC, &err);
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

static const char *OPS[] = {
	"-x", "+x", "abs(x)", "sqrt(x)", "relu(x)", "sin(x)", "cos(x)",
	"tan(x)", "exp(x)", "log(x)", "tanh(x)", "sigmoid(x)",
	"x + y", "x - y", "x * y", "x / y", "min(x, y)", "max(x, y)",
	"pow(x, y)", "x ^ y",
	"x + x", "x - x", "x * x", "x / x", "min(x, x)", "max(x, x)", "pow(x, x)",
	"x + 2", "2 - x", "x * 0.5", "1 / x", "x / 0", "min(x, 0)", "max(0, x)",
	"min(x, -0)", "max(x, 1e308)", "min(1e-308, x)", "pow(x, 2)",
	"pow(2, x)", "pow(x, 0.5)", "sqrt(2)", "sqrt(x) + sqrt(4)", "abs(-3)",
	"relu(-x)", "sin(1)", "-(-x)", "x - 0", "x * 1", "x * 2", "x / 4",
	"3", "x", "pi * e", "1e999", "1e999 - 1e999", "-1e999 + x", "0.1 + x",
	"sin(cos(exp(x)) + log(sqrt(x)))",
	"pow(sin(x), cos(y)) * pow(cos(y), sin(x)) + tanh(pow(x, y))",
	"x * y + sin(x) * (x - y) + cos(y) / (x + y) + sigmoid(x * y)",
	"sigmoid(x)^2 + relu(-x)",
	"a*b + c/d - min(e, f) + max(g, h) + pow(a, h) - sin(c)*g + sqrt(abs(d - e))",
};

static const char *LEGACY[] = {
	"1", "100", "10/4", "-3", "--3", "+5", "2*(3+4)/0.5", "1/0", "0/0",
	"(1+2)*3-4/5", "1-1", "1e-9/3",
};

/* Each gcc compile costs a process spawn: compare every distinct
 * (mode, expression) of the golden cases once. */
static int
seen_before(const char *expr, int legacy)
{
	static char seen[400][96];
	static int n;
	char key[96];
	int i;

	snprintf(key, sizeof (key), "%d%s", legacy, expr);
	for (i = 0; i < n; ++i) {
		if (!strcmp(seen[i], key)) {
			return 1;
		}
	}
	if (n < 400) {
		snprintf(seen[n++], sizeof (seen[0]), "%s", key);
	}
	return 0;
}

/* Every golden expression that parses (options skipped). */
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
			else if (!strcmp(line, "-O0") || !strcmp(line, "-O2") ||
				 !strcmp(line, "--help") || !strcmp(line, "--version")) {
				continue;
			}
			else {
				expr = strdup(line);
				break;
			}
		}
		fclose(f);
		if (!expr || !(e = jit_parse(expr, legacy, &err))) {
			free(expr);
			continue;
		}
		jit_free(e);
		if (!seen_before(expr, legacy)) {
			check_src(expr, legacy, legacy ? 0 : 2);
			++count;
		}
		free(expr);
	}
	closedir(d);
	printf("  %d golden expressions compared\n", count);
	CHECK(count >= 50);
}

static char *
source_of(const jit_fn *fn)
{
	char *text = NULL;
	size_t size = 0;
	FILE *f = open_memstream(&text, &size);

	CHECK(0 == jit_fn_dump(fn, "c", f));
	fclose(f);
	return text;
}

static void
check_cgen(void)
{
	struct jit_error err;
	jit_expr *e;
	jit_fn *fn;
	char *text;

	e = jit_parse("x * 0.1 + min(x, 2) - relu(x) + sigmoid(x)", 0, &err);
	fn = jit_compile(e, JIT_GCC, &err);
	CHECK(fn != NULL);
	text = fn ? source_of(fn) : NULL;
	CHECK(text && strstr(text, "double evaluate(const double *v) {"));
	CHECK(text && strstr(text, "#include <math.h>"));
	CHECK(text && strstr(text, "0x1.999999999999ap-4"));         /* %a constant */
	CHECK(text && strstr(text, "double (*sigmoidptr)(double) = (double (*)(double))"));
	CHECK(text && strstr(text, ") ? t0 : t"));                     /* ternaries */
	CHECK(-1 == (fn ? jit_fn_dump(fn, "asm", stdout) : -1));
	CHECK(fn && jit_entry(fn) != NULL);
	free(text);
	jit_fn_free(fn);
	jit_free(e);

	/* non-finite constants (core.md Q9) */
	e = jit_parse("x + 1e999 - 1e999 * 1e999", 0, &err);
	fn = jit_compile(e, JIT_GCC, &err);
	text = fn ? source_of(fn) : NULL;
	CHECK(text && strstr(text, "(1.0/0.0)"));
	free(text);
	jit_fn_free(fn);
	jit_free(e);

	/* IR_STEP is internal: build it by hand */
	e = jit_parse("x + y", 0, &err);
	e->ir.root = ir_unary(&e->ir, IR_STEP, ir_var(&e->ir, 0));
	fn = jit_compile(e, JIT_GCC, &err);
	CHECK(fn != NULL);
	text = fn ? source_of(fn) : NULL;
	CHECK(text && strstr(text, "(t0 > 0.0 ? 1.0 : 0.0)"));
	if (fn) {
		static const double want[NIN] = { 0, 0, 1, 0, 1, 1, 1, 1, 1, 0, 0 };
		int i;

		for (i = 0; i < NIN; ++i) {
			double v[2] = { INPUTS[i], 0.0 };

			CHECK(same(want[i], jit_call(fn, v)));
		}
	}
	free(text);
	jit_fn_free(fn);
	jit_free(e);

	/* legacy: the original program, byte for byte */
	e = jit_parse("10/4", 1, &err);
	fn = jit_compile(e, JIT_GCC, &err);
	text = fn ? source_of(fn) : NULL;
	CHECK(text && !strncmp(text,
			       "double evaluate(void) {\n"
			       "double t0 = 10;\n"
			       "double t1 = 4;\n"
			       "double t2 = t1 ? (t0 / t1) : 0.0;\n"
			       "double result = t2;\n"
			       "double (*sigmoidptr)(double) = (double (*)(double))",
			       sizeof ("double evaluate(void) {\n"
				       "double t0 = 10;\n"
				       "double t1 = 4;\n"
				       "double t2 = t1 ? (t0 / t1) : 0.0;\n"
				       "double result = t2;\n"
				       "double (*sigmoidptr)(double) = (double (*)(double))") - 1));
	CHECK(text && strstr(text, "return sigmoidptr(result);\n}\n"));
	CHECK(fn && same(jit_call(fn, NULL), jit_interp(e, NULL)));
	free(text);
	jit_fn_free(fn);
	jit_free(e);
}

static int
exists(const char *path)
{
	struct stat st;

	return 0 == stat(path, &st);
}

/* The loader on its own: a broken C file gives gcc's first line. */
static void
check_loader_error(void)
{
	char dir[] = "/tmp/jit-XXXXXX", path[64], msg[128];
	FILE *f;

	CHECK(mkdtemp(dir) != NULL);
	snprintf(path, sizeof (path), "%s/expr.c", dir);
	f = fopen(path, "w");
	CHECK(f != NULL);
	if (f) {
		fputs("double evaluate(const double *v) { return v[0] +; }\n"
		      "second line of nonsense\n", f);
		fclose(f);
	}
	CHECK(-1 == jitc_compile(dir, msg, sizeof (msg)));
	printf("  gcc said: %s\n", msg);
	CHECK(msg[0] != '\0');
	CHECK(NULL == strchr(msg, '\n'));
	CHECK(NULL != strstr(msg, "expr.c"));
	snprintf(path, sizeof (path), "%s/expr.so", dir);
	CHECK(!exists(path));
	CHECK(NULL == jitc_open(path));

	/* a good file compiles, loads and finds its symbol */
	snprintf(path, sizeof (path), "%s/expr.c", dir);
	f = fopen(path, "w");
	if (f) {
		fputs("double evaluate(const double *v) { return v[0] + 1.0; }\n", f);
		fclose(f);
	}
	CHECK(0 == jitc_compile(dir, msg, sizeof (msg)));
	snprintf(path, sizeof (path), "%s/expr.so", dir);
	CHECK(exists(path));
	{
		struct jitc *so = jitc_open(path);

		CHECK(so != NULL);
		CHECK(so && jitc_lookup(so, "evaluate") != 0);
		CHECK(so && jitc_lookup(so, "nothing") == 0);
		jitc_close(so);
	}
	jitc_close(NULL);
	snprintf(path, sizeof (path), "%s/expr.so", dir);
	unlink(path);
	snprintf(path, sizeof (path), "%s/expr.c", dir);
	unlink(path);
	CHECK(0 == rmdir(dir));
}

/* gcc_compile reports a gcc failure through jit_error and leaves no files. */
static void
check_compile_error(void)
{
	struct ir ir;
	struct gcc_code g;
	struct jit_error err;
	DIR *d;
	struct dirent *de;
	int before = 0, after = 0;

	/* count /tmp/jit-* before and after */
	for (d = opendir("/tmp"); d && (de = readdir(d));) {
		before += !strncmp(de->d_name, "jit-", 4);
	}
	if (d) {
		closedir(d);
	}
	/* an op the emitter cannot produce: out-of-range enum value */
	ir_init(&ir);
	ir.root = ir_const(&ir, 1.0);
	ir.nodes[ir.root].op = (enum ir_op)99;
	CHECK(-1 == gcc_compile(&ir, &g, &err));
	CHECK_STR(err.msg, "cannot generate C for this expression");
	CHECK(-1 == err.pos);
	ir_free(&ir);

	/* a variable index so large gcc still compiles it: use a bad node
	   shape instead — a NaN-free root out of range is a generator error */
	ir_init(&ir);
	ir.root = 5;
	CHECK(-1 == gcc_compile(&ir, &g, &err));
	ir_free(&ir);

	for (d = opendir("/tmp"); d && (de = readdir(d));) {
		after += !strncmp(de->d_name, "jit-", 4);
	}
	if (d) {
		closedir(d);
	}
	CHECK(before == after);
}

/* The temp dir exists while the function lives and is gone after free; the
   function survives jit_free(expr). */
static void
check_lifetime(void)
{
	struct jit_error err;
	jit_expr *e = jit_parse("x * 3 + 1", 0, &err);
	struct ir *ir = &e->ir;
	struct gcc_code g;
	char path[64];
	jit_fn *fn;
	double v = 2.0;

	CHECK(0 == gcc_compile(ir, &g, &err));
	CHECK(0 == strncmp(g.dir, "/tmp/jit-", 9));
	snprintf(path, sizeof (path), "%s/expr.so", g.dir);
	CHECK(exists(path));
	snprintf(path, sizeof (path), "%s", g.dir);
	CHECK(same(7.0, gcc_call(&g, &v)));
	gcc_code_free(&g);
	CHECK(!exists(path));

	fn = jit_compile(e, JIT_GCC, &err);
	jit_free(e);
	CHECK(fn && same(7.0, jit_call(fn, &v)));
	CHECK(fn && same(7.0, jit_entry(fn)(&v)));
	jit_fn_free(fn);
	jit_fn_free(NULL);
}

int
main(void)
{
	size_t i;

	for (i = 0; i < sizeof (OPS) / sizeof (OPS[0]); ++i) {
		check_src(OPS[i], 0, 2);
	}
	for (i = 0; i < sizeof (LEGACY) / sizeof (LEGACY[0]); ++i) {
		check_src(LEGACY[i], 1, 0);
	}
	check_goldens();
	check_cgen();
	check_loader_error();
	check_compile_error();
	check_lifetime();
	return CHECK_REPORT("test_gcc.c");
}
