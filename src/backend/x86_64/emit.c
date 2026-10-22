/* IR -> x86-64 machine code + constant pool (backends.md §x86-64 backend).
 *
 * double f(const double *vars): rdi = vars (kept in callee-saved rbx so it
 * survives calls), result in xmm0. Frame:
 *   push rbp; mov rbp,rsp; push rbx; sub rsp,FRAME; mov rbx,rdi
 *   ... body ...
 *   add rsp,FRAME; pop rbx; pop rbp; ret
 * On entry rsp = 8 (mod 16); after the two pushes it is 8 again, so FRAME =
 * 8 (mod 16) keeps rsp 16-byte aligned at every call. Spill slot k is
 * [rbp - 16 - 8k].
 *
 * Operands follow the interpreter's formulas: a is always the destination
 * (minsd/maxsd return the second operand on NaN and on equal values, exactly
 * like (a < b) ? a : b and (a > b) ? a : b). Constants live in the pool and
 * are used as [rip+cN] memory operands or loaded on demand; variables are
 * loaded from [rbx + 8*index] on demand. */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "backend/x86_64/emit.h"
#include "backend/x86_64/encode.h"
#include "backend/x86_64/regalloc.h"
#include "runtime/rt.h"

enum pool_kind { POOL_DOUBLE, POOL_SIGN, POOL_ABS };

struct pool_entry { uint64_t lo, hi; enum pool_kind kind; };
struct fixup { size_t at; int entry; };    /* disp32 at code offset `at` */

struct gen {
	const struct ir *ir;
	struct ra ra;
	struct x86_buf b;
	struct pool_entry *pool; int npool, pool_cap;
	struct fixup *fix; size_t nfix, fix_cap;
	int oom;
};

typedef double (*fn1)(double);
typedef double (*fn2)(double, double);

static uint64_t
dbits(double v)
{
	uint64_t u;

	memcpy(&u, &v, sizeof (u));
	return u;
}

/* Pool entry index (16 bytes each, so every entry is 16-byte aligned). */
static int
pool_entry(struct gen *g, enum pool_kind kind, uint64_t lo, uint64_t hi)
{
	int i;

	for (i = 0; i < g->npool; ++i) {
		if (g->pool[i].kind == kind && g->pool[i].lo == lo) {
			return i;
		}
	}
	if (g->npool == g->pool_cap) {
		int cap = g->pool_cap ? 2 * g->pool_cap : 16;
		struct pool_entry *p = realloc(g->pool, (size_t)cap * sizeof (*p));
		if (!p) {
			g->oom = 1;
			return 0;
		}
		g->pool = p;
		g->pool_cap = cap;
	}
	g->pool[g->npool].kind = kind;
	g->pool[g->npool].lo = lo;
	g->pool[g->npool].hi = hi;
	return g->npool++;
}

static int
pool_double(struct gen *g, double v)
{
	return pool_entry(g, POOL_DOUBLE, dbits(v), 0);
}

/* Records that the instruction just emitted ends with a disp32 to entry e. */
static void
fixup(struct gen *g, int e)
{
	if (g->b.oom || g->oom) {
		return;
	}
	if (g->nfix == g->fix_cap) {
		size_t cap = g->fix_cap ? 2 * g->fix_cap : 32;
		struct fixup *p = realloc(g->fix, cap * sizeof (*p));
		if (!p) {
			g->oom = 1;
			return;
		}
		g->fix = p;
		g->fix_cap = cap;
	}
	g->fix[g->nfix].at = g->b.len - 4;
	g->fix[g->nfix].entry = e;
	++g->nfix;
}

static const char *
label(int e, char *buf, size_t len)
{
	snprintf(buf, len, "c%d", e);
	return buf;
}

static void
sse_pool(struct gen *g, enum x86_sse op, int dst, int e)
{
	char lab[16];

	x86_sse_rip(&g->b, op, dst, 0, label(e, lab, sizeof (lab)));
	fixup(g, e);
}

/* Loads v's value into register r without changing ownership. */
static void
load_into(struct gen *g, int v, int r)
{
	const struct ir_node *x = &g->ir->nodes[v];
	int node = g->b.node;
	char lab[16];

	g->b.node = g->ra.num[v];
	if (IR_CONST == x->op) {
		int e = pool_double(g, x->imm);
		x86_movsd_rip(&g->b, r, 0, label(e, lab, sizeof (lab)));
		fixup(g, e);
	}
	else if (IR_VAR == x->op) {
		x86_movsd_load(&g->b, r, X86_RBX, 8 * x->a);
	}
	else {
		/* invariant: a value out of its register is in its slot */
		x86_movsd_load(&g->b, r, X86_RBP, ra_slot_disp(g->ra.slot[v]));
	}
	g->b.node = node;
}

/* v in some register (loaded into a fresh one if needed). */
static int
ensure(struct gen *g, int v, unsigned pinned)
{
	int r;

	if (0 <= g->ra.reg[v]) {
		return g->ra.reg[v];
	}
	r = ra_alloc(&g->ra, &g->b, pinned);
	load_into(g, v, r);
	ra_bind(&g->ra, v, r);
	return r;
}

/* A register holding a's value that node n may overwrite: a's own register
 * when n is a's last use, else a fresh one with a copied in. */
static int
dest_from(struct gen *g, int n, int a, unsigned pinned)
{
	int ra_ = g->ra.reg[a], r;

	if (0 <= ra_ && g->ra.end[a] == n) {
		ra_unbind(&g->ra, a);
		return ra_;
	}
	if (0 <= ra_) {
		pinned |= 1u << ra_;
	}
	r = ra_alloc(&g->ra, &g->b, pinned);
	if (0 <= ra_) {
		x86_sse_rr(&g->b, X86_MOVAPD, r, ra_);
	}
	else {
		load_into(g, a, r);
	}
	return r;
}

/* Puts v's value in xmm k for a call (ownership is reset by the call). */
static void
place(struct gen *g, int v, int k)
{
	int r = g->ra.reg[v];

	if (r == k) {
		return;
	}
	if (0 <= r) {
		x86_sse_rr(&g->b, X86_MOVAPD, k, r);
	}
	else {
		load_into(g, v, k);
	}
}

static uint64_t
callee(enum ir_op op, const char **name)
{
	fn1 f1 = NULL;
	fn2 f2 = NULL;
	uint64_t u = 0;

	_Static_assert(sizeof (fn1) == sizeof (uint64_t), "64-bit pointers");
	switch (op) {
	case IR_SIN:     f1 = sin;            *name = "sin";            break;
	case IR_COS:     f1 = cos;            *name = "cos";            break;
	case IR_TAN:     f1 = tan;            *name = "tan";            break;
	case IR_EXP:     f1 = exp;            *name = "exp";            break;
	case IR_LOG:     f1 = log;            *name = "log";            break;
	case IR_TANH:    f1 = tanh;           *name = "tanh";           break;
	case IR_SIGMOID: f1 = jit_rt_sigmoid; *name = "jit_rt_sigmoid"; break;
	case IR_POW:     f2 = pow;            *name = "pow";            break;
	default:         *name = "?";                                   break;
	}
	if (f1) {
		memcpy(&u, &f1, sizeof (u));
	}
	else if (f2) {
		memcpy(&u, &f2, sizeof (u));
	}
	return u;
}

/* Every xmm is caller-saved: values live across the call go to their slots;
 * arguments in xmm0 (and xmm1 for pow); the result comes back in xmm0. */
static int
call(struct gen *g, int n, const struct ir_node *x)
{
	const char *name;
	uint64_t addr = callee(x->op, &name);

	ra_save_live_across(&g->ra, &g->b, n);
	if (IR_POW == x->op) {
		int a = x->a, bb = x->b;
		int ra_ = g->ra.reg[a], rb = g->ra.reg[bb];

		if (a == bb) {
			place(g, a, 0);
			x86_sse_rr(&g->b, X86_MOVAPD, 1, 0);
		}
		else if (0 == rb && 1 == ra_) {
			x86_sse_rr(&g->b, X86_MOVAPD, 2, 0);    /* swap */
			x86_sse_rr(&g->b, X86_MOVAPD, 0, 1);
			x86_sse_rr(&g->b, X86_MOVAPD, 1, 2);
		}
		else if (0 == rb) {
			place(g, bb, 1);                        /* a is not in xmm1 */
			place(g, a, 0);
		}
		else {
			place(g, a, 0);                         /* b is not in xmm0 */
			place(g, bb, 1);
		}
	}
	else {
		place(g, x->a, 0);
	}
	x86_mov_rax_imm64(&g->b, addr, name);
	x86_call_rax(&g->b);
	ra_clobber(&g->ra);
	return 0;
}

static enum x86_sse
sse_op(enum ir_op op)
{
	switch (op) {
	case IR_ADD: return X86_ADDSD;
	case IR_SUB: return X86_SUBSD;
	case IR_MUL: return X86_MULSD;
	case IR_DIV: return X86_DIVSD;
	case IR_MIN: return X86_MINSD;
	default:     return X86_MAXSD;
	}
}

static void
node(struct gen *g, int n)
{
	const struct ir_node *x = &g->ir->nodes[n];
	struct ra *ra = &g->ra;
	int a = x->a, b = x->b, r = -1;

	g->b.node = ra->num[n];
	switch (x->op) {
	case IR_CONST:
	case IR_VAR:
		return;             /* no code: loaded where used */
	case IR_NEG:
		r = dest_from(g, n, a, 0);
		sse_pool(g, X86_XORPD, r,
			 pool_entry(g, POOL_SIGN, 0x8000000000000000ull,
				    0x8000000000000000ull));
		break;
	case IR_ABS:
		r = dest_from(g, n, a, 0);
		sse_pool(g, X86_ANDPD, r,
			 pool_entry(g, POOL_ABS, 0x7fffffffffffffffull,
				    0x7fffffffffffffffull));
		break;
	case IR_RELU:           /* maxsd a, 0.0: (a > 0) ? a : 0, NaN -> 0 */
		r = dest_from(g, n, a, 0);
		sse_pool(g, X86_MAXSD, r, pool_double(g, 0.0));
		break;
	case IR_SQRT:
		if (0 <= ra->reg[a]) {
			int src = ra->reg[a];
			if (ra->end[a] == n) {
				ra_unbind(ra, a);
				r = src;
			}
			else {
				r = ra_alloc(ra, &g->b, 1u << src);
			}
			x86_sse_rr(&g->b, X86_SQRTSD, r, src);
		}
		else if (IR_CONST == g->ir->nodes[a].op) {
			r = ra_alloc(ra, &g->b, 0);
			sse_pool(g, X86_SQRTSD, r,
				 pool_double(g, g->ir->nodes[a].imm));
		}
		else {
			r = ra_alloc(ra, &g->b, 0);
			load_into(g, a, r);
			x86_sse_rr(&g->b, X86_SQRTSD, r, r);
		}
		break;
	case IR_STEP: {         /* t = 0; t = (t < a) ? ~0 : 0; t &= 1.0 */
		int src = ensure(g, a, 0);
		r = ra_alloc(ra, &g->b, 1u << src);
		x86_sse_rr(&g->b, X86_XORPD, r, r);
		x86_cmpsd(&g->b, X86_CMP_LT, r, src);
		sse_pool(g, X86_ANDPD, r, pool_double(g, 1.0));
		break;
	}
	case IR_ADD:
	case IR_SUB:
	case IR_MUL:
	case IR_DIV:
	case IR_MIN:
	case IR_MAX: {
		int rb = -1, eb = -1;

		if (IR_CONST == g->ir->nodes[b].op && 0 > ra->reg[b]) {
			eb = pool_double(g, g->ir->nodes[b].imm);
		}
		else {
			rb = ensure(g, b, 0 <= ra->reg[a] ? 1u << ra->reg[a] : 0);
		}
		r = dest_from(g, n, a, 0 <= rb ? 1u << rb : 0);
		if (0 <= rb) {
			x86_sse_rr(&g->b, sse_op(x->op), r, rb);
		}
		else {
			sse_pool(g, sse_op(x->op), r, eb);
		}
		break;
	}
	case IR_SIN:
	case IR_COS:
	case IR_TAN:
	case IR_EXP:
	case IR_LOG:
	case IR_TANH:
	case IR_SIGMOID:
	case IR_POW:
		call(g, n, x);
		r = 0;
		break;
	}
	/* operands whose last use is n die here */
	if (ra->end[a] == n) {
		ra_release(ra, a);
	}
	if (0 <= b && b != a && ra->end[b] == n) {
		ra_release(ra, b);
	}
	ra_bind(ra, n, r);
}

static void
put_hex(FILE *f, const unsigned char *p, size_t n)
{
	size_t i;

	for (i = 0; i < n; ++i) {
		fprintf(f, i ? " %02x" : "%02x", p[i]);
	}
}

static char *
render_asm(const struct gen *g, const unsigned char *image, size_t pool_off)
{
	char *text = NULL, bytes[64];
	size_t size = 0, i;
	FILE *f = open_memstream(&text, &size);
	int e;

	if (!f) {
		return NULL;
	}
	for (i = 0; i < g->b.nlines; ++i) {
		const struct x86_line *l = &g->b.lines[i];
		size_t k, w = 0;

		bytes[0] = '\0';
		for (k = 0; k < l->len && w + 4 < sizeof (bytes); ++k) {
			w += (size_t)snprintf(bytes + w, sizeof (bytes) - w,
					      k ? " %02x" : "%02x",
					      image[l->off + k]);
		}
		fprintf(f, "%04zx  %-30s %s", l->off, bytes, l->text);
		if (0 <= l->node) {
			fprintf(f, "  ; %%%d", l->node);
		}
		fputc('\n', f);
	}
	fprintf(f, "; constant pool\n");
	for (e = 0; e < g->npool; ++e) {
		size_t off = pool_off + 16 * (size_t)e;
		char lab[16], val[32];
		double v;

		fprintf(f, "%04zx  %-4s ", off, label(e, lab, sizeof (lab)));
		put_hex(f, image + off, 16);
		if (POOL_SIGN == g->pool[e].kind) {
			fprintf(f, "  ; sign mask\n");
		}
		else if (POOL_ABS == g->pool[e].kind) {
			fprintf(f, "  ; abs mask\n");
		}
		else {
			memcpy(&v, &g->pool[e].lo, sizeof (v));
			ir_fmt_double(val, sizeof (val), v);
			fprintf(f, "  ; %s\n", val);
		}
	}
	if (fclose(f)) {
		free(text);
		return NULL;
	}
	return text;
}

static void
gen_free(struct gen *g)
{
	ra_free(&g->ra);
	x86_buf_free(&g->b);
	free(g->pool);
	free(g->fix);
}

static int
fail(struct gen *g, struct jit_error *err, const char *msg)
{
	gen_free(g);
	err->pos = -1;
	snprintf(err->msg, sizeof (err->msg), "%s", msg);
	return -1;
}

int
x86_compile(const struct ir *ir, struct x86_code *out, struct jit_error *err)
{
	struct gen g;
	unsigned char *image;
	size_t sub_line, pool_off, size, i;
	int32_t frame;
	int n, e;

	memset(out, 0, sizeof (*out));
	memset(&g, 0, sizeof (g));
	x86_buf_init(&g.b);
	g.ir = ir;
	if (ir->legacy) {
		return fail(&g, err, "legacy mode needs --backend gcc or interp");
	}
	if (0 > ir->root || ir->root >= ir->count) {
		return fail(&g, err, "empty expression");
	}
	if (ra_init(&g.ra, ir)) {
		return fail(&g, err, "out of memory");
	}

	x86_push(&g.b, X86_RBP);
	x86_mov_rbp_rsp(&g.b);
	x86_push(&g.b, X86_RBX);
	sub_line = g.b.nlines;
	x86_sub_rsp(&g.b, 8);   /* patched once the slot count is known */
	x86_mov_rbx_rdi(&g.b);
	for (n = 0; n <= ir->root && !g.b.oom && !g.oom; ++n) {
		if (g.ra.live[n]) {
			node(&g, n);
		}
	}
	g.b.node = g.ra.num[ir->root];
	place(&g, ir->root, 0);
	g.b.node = -1;
	frame = (int32_t)(8 * g.ra.nslots + (g.ra.nslots % 2 ? 0 : 8));
	x86_add_rsp(&g.b, frame);
	x86_pop(&g.b, X86_RBX);
	x86_pop(&g.b, X86_RBP);
	x86_ret(&g.b);
	if (g.b.oom || g.oom) {
		return fail(&g, err, "out of memory");
	}
	/* patch sub rsp,FRAME (48 81 ec imm32) */
	{
		struct x86_line *l = &g.b.lines[sub_line];
		for (i = 0; i < 4; ++i) {
			g.b.code[l->off + 3 + i] = (unsigned char)((uint32_t)frame >> (8 * i));
		}
		snprintf(l->text, sizeof (l->text), "sub rsp,0x%x", (unsigned)frame);
	}

	/* layout: code, zero padding to 16, pool (16 bytes per entry) */
	pool_off = (g.b.len + 15) & ~(size_t)15;
	size = pool_off + 16 * (size_t)g.npool;
	for (i = 0; i < g.nfix; ++i) {
		int64_t disp = (int64_t)(pool_off + 16 * (size_t)g.fix[i].entry) -
			       (int64_t)(g.fix[i].at + 4);
		uint32_t d = (uint32_t)(int32_t)disp;
		int k;
		for (k = 0; k < 4; ++k) {
			g.b.code[g.fix[i].at + (size_t)k] = (unsigned char)(d >> (8 * k));
		}
	}
	if (!(image = calloc(1, size))) {
		return fail(&g, err, "out of memory");
	}
	memcpy(image, g.b.code, g.b.len);
	for (e = 0; e < g.npool; ++e) {
		memcpy(image + pool_off + 16 * (size_t)e, &g.pool[e].lo, 8);
		memcpy(image + pool_off + 16 * (size_t)e + 8, &g.pool[e].hi, 8);
	}
	if (!(out->asm_text = render_asm(&g, image, pool_off))) {
		free(image);
		return fail(&g, err, "out of memory");
	}
	if (!(out->mem = x86_exec_alloc(image, size))) {
		free(image);
		free(out->asm_text);
		out->asm_text = NULL;
		return fail(&g, err, "cannot map executable memory");
	}
	free(image);
	out->size = size;
	out->code_size = g.b.len;
	gen_free(&g);
	return 0;
}

void
x86_code_free(struct x86_code *c)
{
	if (c) {
		x86_exec_free(c->mem, c->size);
		free(c->asm_text);
		memset(c, 0, sizeof (*c));
	}
}

int
x86_dump(const struct x86_code *c, const char *stage, FILE *out)
{
	const unsigned char *p = c->mem;
	size_t i;

	if (!strcmp(stage, "asm")) {
		fputs(c->asm_text, out);
		return 0;
	}
	if (!strcmp(stage, "hex")) {
		for (i = 0; i < c->size; i += 16) {
			put_hex(out, p + i, c->size - i < 16 ? c->size - i : 16);
			fputc('\n', out);
		}
		return 0;
	}
	return -1;
}
