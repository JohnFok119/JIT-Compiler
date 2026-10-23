/* WebAssembly backend: LEB128, the exact bytes of a small module, the module
 * structure (sections, imports), and a tiny wasm stack machine that runs the
 * emitted bytes and must match the interpreter bit for bit (NaN: both NaN). */

#include "../check.h"
#include "api.h"
#include "backend/wasm/emit.h"
#include "ir/ir.h"
#include <math.h>
#include <stdlib.h>

static int
same(double a, double b)
{
	if (isnan(a) || isnan(b)) {
		return isnan(a) && isnan(b);
	}
	return !memcmp(&a, &b, sizeof (a));
}

static void
check_leb(void)
{
	static const struct { uint64_t v; unsigned char b[10]; size_t n; } U[] = {
		{ 0, { 0x00 }, 1 }, { 1, { 0x01 }, 1 }, { 127, { 0x7f }, 1 },
		{ 128, { 0x80, 0x01 }, 2 }, { 255, { 0xff, 0x01 }, 2 },
		{ 16383, { 0xff, 0x7f }, 2 }, { 16384, { 0x80, 0x80, 0x01 }, 3 },
		{ 624485, { 0xe5, 0x8e, 0x26 }, 3 },
		{ 0xffffffffu, { 0xff, 0xff, 0xff, 0xff, 0x0f }, 5 },
		{ UINT64_MAX, { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01 }, 10 },
	};
	static const struct { int64_t v; unsigned char b[10]; size_t n; } S[] = {
		{ 0, { 0x00 }, 1 }, { 1, { 0x01 }, 1 }, { -1, { 0x7f }, 1 },
		{ 63, { 0x3f }, 1 }, { 64, { 0xc0, 0x00 }, 2 },
		{ -64, { 0x40 }, 1 }, { -65, { 0xbf, 0x7f }, 2 },
		{ 127, { 0xff, 0x00 }, 2 }, { -128, { 0x80, 0x7f }, 2 },
		{ -123456, { 0xc0, 0xbb, 0x78 }, 3 },
		{ INT64_MIN, { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7f }, 10 },
		{ INT64_MAX, { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 }, 10 },
	};
	unsigned char out[16];
	size_t i, n;

	for (i = 0; i < sizeof (U) / sizeof (U[0]); ++i) {
		n = wasm_leb_u(out, U[i].v);
		CHECK(n == U[i].n && !memcmp(out, U[i].b, n));
	}
	for (i = 0; i < sizeof (S) / sizeof (S[0]); ++i) {
		n = wasm_leb_s(out, S[i].v);
		CHECK(n == S[i].n && !memcmp(out, S[i].b, n));
	}
}

/* Section walk: sets *at and *size to the payload of section id, 0 if absent.
 * Returns -1 when the sections do not tile the module exactly. */
static int
find_section(const unsigned char *m, size_t len, unsigned id,
	     size_t *at, size_t *size)
{
	size_t p = 8;
	int found = 0, last = 0;

	*at = *size = 0;
	while (p < len) {
		unsigned sid = m[p++];
		size_t sz = 0;
		int shift = 0;

		while (p < len) {
			unsigned char b = m[p++];

			sz |= (size_t)(b & 0x7f) << shift;
			shift += 7;
			if (!(b & 0x80)) {
				break;
			}
		}
		if (p + sz > len || (int)sid <= last) {
			return -1;
		}
		last = (int)sid;
		if (sid == id) {
			*at = p;
			*size = sz;
			found = 1;
		}
		p += sz;
	}
	return p == len ? found : -1;
}

static uint64_t
rd_leb(const unsigned char *m, size_t *p)
{
	uint64_t v = 0;
	int shift = 0;

	for (;;) {
		unsigned char b = m[(*p)++];

		v |= (uint64_t)(b & 0x7f) << shift;
		shift += 7;
		if (!(b & 0x80)) {
			return v;
		}
	}
}

/* Import names in module order; returns the count. */
static int
read_imports(const unsigned char *m, size_t len, char names[8][8], int types[8])
{
	size_t at, size, p;
	uint64_t n, i;

	if (1 != find_section(m, len, 2, &at, &size)) {
		return 0;
	}
	p = at;
	n = rd_leb(m, &p);
	for (i = 0; i < n && i < 8; ++i) {
		size_t l = (size_t)rd_leb(m, &p);

		p += l;                         /* module name "m" */
		l = (size_t)rd_leb(m, &p);
		memcpy(names[i], m + p, l);
		names[i][l] = '\0';
		p += l;
		++p;                            /* kind: function */
		types[i] = (int)rd_leb(m, &p);
	}
	return (int)n;
}

/* The wasm stack machine: just the opcodes the emitter uses. */
static double
run(const unsigned char *m, size_t len, int nvars, const double *vars)
{
	char names[8][8];
	int types[8], nimp = read_imports(m, len, names, types);
	double L[512], S[64];
	size_t at, size, p;
	int sp = 0, i;
	uint64_t nl = 0, runs, r;

	if (1 != find_section(m, len, 10, &at, &size)) {
		return -12345.0;
	}
	for (i = 0; i < 512; ++i) {
		L[i] = 0.0;
	}
	for (i = 0; i < nvars; ++i) {
		L[i] = vars[i];
	}
	p = at;
	rd_leb(m, &p);                          /* function count */
	rd_leb(m, &p);                          /* body size */
	runs = rd_leb(m, &p);
	for (r = 0; r < runs; ++r) {
		nl += rd_leb(m, &p);
		++p;                            /* f64 */
	}
	if (nl + (uint64_t)nvars > 512) {
		return -12345.0;
	}
	for (;;) {
		unsigned op = m[p++];
		double a, b, c;

		switch (op) {
		case 0x0B: return S[0];
		case 0x20: S[sp++] = L[rd_leb(m, &p)]; break;
		case 0x21: L[rd_leb(m, &p)] = S[--sp]; break;
		case 0x44: {
			uint64_t u = 0;

			for (i = 7; i >= 0; --i) {
				u = (u << 8) | m[p + (size_t)i];
			}
			p += 8;
			memcpy(&a, &u, sizeof (a));
			S[sp++] = a;
			break;
		}
		case 0x1B: c = S[--sp]; b = S[--sp]; a = S[--sp]; S[sp++] = c != 0.0 ? a : b; break;
		case 0x63: b = S[--sp]; a = S[--sp]; S[sp++] = a < b; break;
		case 0x64: b = S[--sp]; a = S[--sp]; S[sp++] = a > b; break;
		case 0x99: S[sp - 1] = fabs(S[sp - 1]); break;
		case 0x9A: S[sp - 1] = -S[sp - 1]; break;
		case 0x9F: S[sp - 1] = sqrt(S[sp - 1]); break;
		case 0xA0: b = S[--sp]; S[sp - 1] += b; break;
		case 0xA1: b = S[--sp]; S[sp - 1] -= b; break;
		case 0xA2: b = S[--sp]; S[sp - 1] *= b; break;
		case 0xA3: b = S[--sp]; S[sp - 1] /= b; break;
		case 0xB8: break;               /* i32 0/1 is already 0.0/1.0 here */
		case 0x10: {
			uint64_t f = rd_leb(m, &p);
			const char *nm;

			if ((int)f >= nimp) {
				return -12345.0;
			}
			nm = names[f];
			if (!strcmp(nm, "pow")) {
				b = S[--sp];
				S[sp - 1] = pow(S[sp - 1], b);
			}
			else {
				a = S[sp - 1];
				S[sp - 1] = !strcmp(nm, "sin") ? sin(a) :
					    !strcmp(nm, "cos") ? cos(a) :
					    !strcmp(nm, "tan") ? tan(a) :
					    !strcmp(nm, "exp") ? exp(a) :
					    !strcmp(nm, "log") ? log(a) : tanh(a);
			}
			break;
		}
		default:
			return -12345.0;
		}
	}
}

static const double INPUTS[] = {
	0.0, -0.0, 1.0, -1.0, 0.5, 2.0, 1e308, 1e-308, INFINITY, -INFINITY, NAN
};
#define NIN ((int)(sizeof (INPUTS) / sizeof (INPUTS[0])))

/* Wasm bytes run by the stack machine vs the interpreter on a grid. */
static void
compare(const char *what, const jit_expr *e)
{
	unsigned char *m = NULL;
	size_t len = 0;
	struct jit_error err;
	int nv = e->ir.nvars, i, j, ok = 1;

	if (wasm_emit(&e->ir, &m, &len, &err)) {
		printf("  wasm_emit failed for %s: %s\n", what, err.msg);
		CHECK(0);
		return;
	}
	CHECK(len > 8 && !memcmp(m, "\0asm\1\0\0\0", 8));
	CHECK(0 <= find_section(m, len, 10, &(size_t){ 0 }, &(size_t){ 0 }));
	for (i = 0; i < (nv >= 1 ? NIN : 1) && ok; ++i) {
		for (j = 0; j < (nv >= 2 ? NIN : 1) && ok; ++j) {
			double v[JIT_MAX_VARS] = { 0 }, want, got;
			int k;

			v[0] = INPUTS[i];
			v[1] = INPUTS[j];
			for (k = 2; k < JIT_MAX_VARS; ++k) {
				v[k] = 0.25 * k;
			}
			want = ir_interp(&e->ir, v);
			got = run(m, len, nv, v);
			if (!same(want, got)) {
				printf("  MISMATCH %s at %g %g: interp %.17g wasm %.17g\n",
				       what, v[0], v[1], want, got);
				ok = 0;
			}
		}
	}
	CHECK(ok);
	free(m);
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

/* Derivatives bring in the internal step() op. */
static const char *DIFFS[] = {
	"relu(x) * abs(y) + min(x, y) - max(x, 2)", "abs(x)", "sigmoid(x) * x",
	"min(x * x, y) + tanh(x)", "x ^ 3 + sin(x) / x",
};

static void
check_modules(void)
{
	static const unsigned char X_PLUS_1[] = {
		0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
		0x01, 0x11, 0x03, 0x60, 0x01, 0x7c, 0x01, 0x7c, 0x60, 0x01, 0x7c,
		0x01, 0x7c, 0x60, 0x02, 0x7c, 0x7c, 0x01, 0x7c,
		0x03, 0x02, 0x01, 0x00,
		0x07, 0x05, 0x01, 0x01, 0x66, 0x00, 0x00,
		0x0a, 0x1e, 0x01, 0x1c, 0x01, 0x03, 0x7c, 0x20, 0x00, 0x21, 0x01,
		0x44, 0, 0, 0, 0, 0, 0, 0xf0, 0x3f, 0x21, 0x02,
		0x20, 0x01, 0x20, 0x02, 0xa0, 0x21, 0x03, 0x20, 0x03, 0x0b
	};
	struct jit_error err;
	unsigned char *m;
	size_t len, at, size;
	char names[8][8];
	int types[8], n;
	jit_expr *e;

	e = jit_parse("x + 1", 0, &err);
	CHECK(0 == wasm_emit(&e->ir, &m, &len, &err));
	CHECK(len == sizeof (X_PLUS_1) && !memcmp(m, X_PLUS_1, len));
	CHECK(0 == read_imports(m, len, names, types));
	free(m);
	jit_free(e);

	/* no variables: type 0 is () -> f64 */
	e = jit_parse("2 * 3", 0, &err);
	CHECK(0 == wasm_emit(&e->ir, &m, &len, &err));
	CHECK(0x03 == m[10] && 0x60 == m[11] && 0x00 == m[12]);
	CHECK(1 == find_section(m, len, 1, &at, &size));
	free(m);
	jit_free(e);

	/* imports: only the ones used, in the fixed order, pow has type 2 */
	e = jit_parse("pow(sin(x), y) + tanh(x) + log(x)", 0, &err);
	CHECK(0 == wasm_emit(&e->ir, &m, &len, &err));
	n = read_imports(m, len, names, types);
	CHECK(4 == n);
	CHECK_STR(names[0], "sin");
	CHECK_STR(names[1], "log");
	CHECK_STR(names[2], "tanh");
	CHECK_STR(names[3], "pow");
	CHECK(1 == types[0] && 1 == types[1] && 1 == types[2] && 2 == types[3]);
	free(m);
	jit_free(e);

	/* sigmoid goes through the imported exp, and nothing else */
	e = jit_parse("sigmoid(x)", 0, &err);
	CHECK(0 == wasm_emit(&e->ir, &m, &len, &err));
	n = read_imports(m, len, names, types);
	CHECK(1 == n);
	CHECK_STR(names[0], "exp");
	free(m);
	jit_free(e);

	/* legacy is refused */
	e = jit_parse("1 / 2", 1, &err);
	CHECK(-1 == wasm_emit(&e->ir, &m, &len, &err));
	CHECK_STR(err.msg, "legacy mode needs --backend gcc or interp");
	CHECK(-1 == err.pos);
	jit_free(e);
}

/* A chain of adds over distinct constants: 2 nodes per step, 1 for the start. */
static int
chain_emit(int steps, struct jit_error *err)
{
	struct ir ir;
	unsigned char *m = NULL;
	size_t len = 0;
	int i, acc, rc;

	ir_init(&ir);
	acc = ir_const(&ir, 0.0);
	for (i = 1; i <= steps; ++i) {
		acc = ir_binary(&ir, IR_ADD, acc, ir_const(&ir, (double)i));
	}
	ir.root = acc;
	rc = wasm_emit(&ir, &m, &len, err);
	free(m);
	ir_free(&ir);
	return rc;
}

static void
check_limit(void)
{
	struct jit_error err;

	CHECK(0 == chain_emit((WASM_MAX_NODES - 1) / 2, &err));        /* 49999 nodes */
	CHECK(-1 == chain_emit(WASM_MAX_NODES / 2, &err));             /* 50001 nodes */
	CHECK_STR(err.msg, "expression too large for the wasm backend");
	CHECK(-1 == err.pos);
}

static void
check_api(void)
{
	struct jit_error err;
	jit_expr *e = jit_parse("x * 2 + sin(y)", 0, &err);
	jit_fn *fn = jit_compile(e, JIT_WASM, &err);
	double v[2] = { 1.0, 2.0 };
	char *text = NULL;
	size_t size = 0, lines = 0, i;
	FILE *f;
	int maxw = 0, w = 0, headers = 0;

	CHECK(NULL != fn);
	CHECK(isnan(jit_call(fn, v)));
	CHECK(NULL == jit_entry(fn));
	CHECK(0 == jit_fn_code_size(fn));
	CHECK(-1 == jit_fn_dump(fn, "asm", stdout));
	CHECK(-1 == jit_fn_dump(fn, "hex", stdout));
	f = open_memstream(&text, &size);
	CHECK(0 == jit_fn_dump(fn, "wasm", f));
	fclose(f);
	CHECK(text && size > 0 && '\n' == text[size - 1]);
	CHECK(text && !strncmp(text, "; header (magic, version)\n00 61 73 6d 01 00 00 00\n", 49));
	CHECK(text && strstr(text, "; type section\n") && strstr(text, "; import section\n") &&
	      strstr(text, "; function section\n") && strstr(text, "; export section\n") &&
	      strstr(text, "; code section\n"));
	for (i = 0; text && i < size; ++i) {
		if ('\n' == text[i]) {
			++lines;
			maxw = w > maxw ? w : maxw;
			w = 0;
		}
		else {
			++w;
		}
		headers += ';' == text[i];
	}
	CHECK(lines > 6 && 6 == headers);
	CHECK(maxw <= 16 * 3 - 1);          /* 16 bytes per line at most */
	free(text);
	jit_fn_free(fn);
	jit_free(e);

	/* the snapshot outlives the expression */
	e = jit_parse("x", 0, &err);
	fn = jit_compile(e, JIT_WASM, &err);
	jit_free(e);
	f = fopen("/dev/null", "w");
	CHECK(f && 0 == jit_fn_dump(fn, "wasm", f));
	CHECK(-1 == jit_fn_dump(fn, "c", f));
	if (f) {
		fclose(f);
	}
	jit_fn_free(fn);
}

int
main(void)
{
	struct jit_error err;
	size_t i;

	check_leb();
	check_modules();
	for (i = 0; i < sizeof (OPS) / sizeof (OPS[0]); ++i) {
		jit_expr *e = jit_parse(OPS[i], 0, &err);

		CHECK(NULL != e);
		if (e) {
			compare(OPS[i], e);
			jit_optimize(e, 2);
			compare(OPS[i], e);
			jit_free(e);
		}
	}
	for (i = 0; i < sizeof (DIFFS) / sizeof (DIFFS[0]); ++i) {
		jit_expr *e = jit_parse(DIFFS[i], 0, &err), *d;

		CHECK(NULL != e);
		d = e ? jit_diff(e, "x", &err) : NULL;
		CHECK(NULL != d);
		if (d) {
			compare(DIFFS[i], d);
			jit_optimize(d, 2);
			compare(DIFFS[i], d);
		}
		jit_free(d);
		jit_free(e);
	}
	check_limit();
	check_api();
	return CHECK_REPORT("test_wasm.c");
}
