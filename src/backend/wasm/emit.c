/* IR -> WebAssembly module (see emit.h for the layout).
 *
 * Opcodes used: local.get 20, local.set 21, call 10, f64.const 44, select 1B,
 * f64.lt 63, f64.gt 64, f64.abs 99, f64.neg 9A, f64.sqrt 9F, f64.add A0,
 * f64.sub A1, f64.mul A2, f64.div A3, f64.convert_i32_u B8, end 0B.
 *
 * min(a,b) = select(a, b, a < b) and max(a,b) = select(a, b, a > b) are the
 * interpreter's (a < b) ? a : b and (a > b) ? a : b; relu(a) = select(a, 0,
 * a > 0); step(a) = convert_i32_u(a > 0). NaN follows the same formulas.
 *
 * jit_call on a wasm jit_fn does not run anything: it returns NaN. The bytes
 * are meant for WebAssembly.instantiate in a browser or Node. */

#include <stdlib.h>
#include <string.h>
#include "backend/wasm/emit.h"

enum {
	OP_END = 0x0B, OP_CALL = 0x10, OP_LOCAL_GET = 0x20, OP_LOCAL_SET = 0x21,
	OP_SELECT = 0x1B, OP_F64_CONST = 0x44,
	OP_F64_LT = 0x63, OP_F64_GT = 0x64,
	OP_F64_ABS = 0x99, OP_F64_NEG = 0x9A, OP_F64_SQRT = 0x9F,
	OP_F64_ADD = 0xA0, OP_F64_SUB = 0xA1, OP_F64_MUL = 0xA2, OP_F64_DIV = 0xA3,
	OP_F64_CONVERT_I32_U = 0xB8,
	T_F64 = 0x7C, T_FUNC = 0x60
};

enum { SEC_TYPE = 1, SEC_IMPORT = 2, SEC_FUNC = 3, SEC_EXPORT = 7, SEC_CODE = 10 };

/* imports in module order */
enum { I_SIN, I_COS, I_TAN, I_EXP, I_LOG, I_TANH, I_POW, N_IMPORTS };
static const char *const IMPORT_NAME[N_IMPORTS] = {
	"sin", "cos", "tan", "exp", "log", "tanh", "pow"
};

size_t
wasm_leb_u(unsigned char *out, uint64_t v)
{
	size_t n = 0;

	do {
		unsigned char b = (unsigned char)(v & 0x7f);

		v >>= 7;
		out[n++] = v ? (unsigned char)(b | 0x80) : b;
	} while (v);
	return n;
}

size_t
wasm_leb_s(unsigned char *out, int64_t v)
{
	size_t n = 0;
	int more = 1;

	while (more) {
		unsigned char b = (unsigned char)(v & 0x7f);

		v >>= 7;        /* arithmetic shift of a negative value (gcc, emcc) */
		if ((!v && !(b & 0x40)) || (-1 == v && (b & 0x40))) {
			more = 0;
		}
		else {
			b |= 0x80;
		}
		out[n++] = b;
	}
	return n;
}

struct buf {
	unsigned char *p;
	size_t len, cap;
	int oom;
};

static void
put_bytes(struct buf *b, const void *src, size_t n)
{
	if (b->oom) {
		return;
	}
	if (b->len + n > b->cap) {
		size_t cap = b->cap ? b->cap : 64;
		unsigned char *p;

		while (cap < b->len + n) {
			cap *= 2;
		}
		if (!(p = realloc(b->p, cap))) {
			b->oom = 1;
			return;
		}
		b->p = p;
		b->cap = cap;
	}
	memcpy(b->p + b->len, src, n);
	b->len += n;
}

static void
put_u8(struct buf *b, unsigned v)
{
	unsigned char c = (unsigned char)v;

	put_bytes(b, &c, 1);
}

static void
put_leb(struct buf *b, uint64_t v)
{
	unsigned char t[10];

	put_bytes(b, t, wasm_leb_u(t, v));
}

static void
put_f64(struct buf *b, double v)
{
	unsigned char t[8];
	uint64_t u;
	int i;

	memcpy(&u, &v, sizeof (u));
	for (i = 0; i < 8; ++i) {       /* little endian, whatever the host is */
		t[i] = (unsigned char)(u >> (8 * i));
	}
	put_bytes(b, t, 8);
}

static void
put_name(struct buf *b, const char *s)
{
	size_t n = strlen(s);

	put_leb(b, n);
	put_bytes(b, s, n);
}

/* id, byte count, payload */
static void
put_section(struct buf *out, unsigned id, const struct buf *payload)
{
	put_u8(out, id);
	put_leb(out, payload->len);
	put_bytes(out, payload->p, payload->len);
}

static int
import_of(enum ir_op op)
{
	switch (op) {
	case IR_SIN:     return I_SIN;
	case IR_COS:     return I_COS;
	case IR_TAN:     return I_TAN;
	case IR_EXP:     return I_EXP;
	case IR_LOG:     return I_LOG;
	case IR_TANH:    return I_TANH;
	case IR_SIGMOID: return I_EXP;
	case IR_POW:     return I_POW;
	default:         return -1;
	}
}

static void
get(struct buf *b, unsigned local)
{
	put_u8(b, OP_LOCAL_GET);
	put_leb(b, local);
}

static void
zero(struct buf *b)
{
	put_u8(b, OP_F64_CONST);
	put_f64(b, 0.0);
}

/* Pushes the value of one node (its operands are already in their locals). */
static void
emit_node(struct buf *b, const struct ir_node *x, const unsigned *loc,
	  const int *imp)
{
	switch (x->op) {
	case IR_CONST:
		put_u8(b, OP_F64_CONST);
		put_f64(b, x->imm);
		return;
	case IR_VAR:
		get(b, (unsigned)x->a);
		return;
	case IR_NEG:  get(b, loc[x->a]); put_u8(b, OP_F64_NEG);  return;
	case IR_ABS:  get(b, loc[x->a]); put_u8(b, OP_F64_ABS);  return;
	case IR_SQRT: get(b, loc[x->a]); put_u8(b, OP_F64_SQRT); return;
	case IR_RELU:
		get(b, loc[x->a]);
		zero(b);
		get(b, loc[x->a]);
		zero(b);
		put_u8(b, OP_F64_GT);
		put_u8(b, OP_SELECT);
		return;
	case IR_STEP:
		get(b, loc[x->a]);
		zero(b);
		put_u8(b, OP_F64_GT);
		put_u8(b, OP_F64_CONVERT_I32_U);
		return;
	case IR_SIGMOID:
		/* 1.0 / (1.0 + exp(-a)), in exactly that order */
		put_u8(b, OP_F64_CONST); put_f64(b, 1.0);
		put_u8(b, OP_F64_CONST); put_f64(b, 1.0);
		get(b, loc[x->a]);
		put_u8(b, OP_F64_NEG);
		put_u8(b, OP_CALL); put_leb(b, (uint64_t)imp[I_EXP]);
		put_u8(b, OP_F64_ADD);
		put_u8(b, OP_F64_DIV);
		return;
	case IR_SIN: case IR_COS: case IR_TAN: case IR_EXP: case IR_LOG:
	case IR_TANH:
		get(b, loc[x->a]);
		put_u8(b, OP_CALL); put_leb(b, (uint64_t)imp[import_of(x->op)]);
		return;
	case IR_POW:
		get(b, loc[x->a]);
		get(b, loc[x->b]);
		put_u8(b, OP_CALL); put_leb(b, (uint64_t)imp[I_POW]);
		return;
	case IR_MIN: case IR_MAX:
		get(b, loc[x->a]);
		get(b, loc[x->b]);
		get(b, loc[x->a]);
		get(b, loc[x->b]);
		put_u8(b, IR_MIN == x->op ? OP_F64_LT : OP_F64_GT);
		put_u8(b, OP_SELECT);
		return;
	case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV:
		get(b, loc[x->a]);
		get(b, loc[x->b]);
		put_u8(b, IR_ADD == x->op ? OP_F64_ADD : IR_SUB == x->op ?
		       OP_F64_SUB : IR_MUL == x->op ? OP_F64_MUL : OP_F64_DIV);
		return;
	}
}

int
wasm_emit(const struct ir *ir, unsigned char **out, size_t *len,
	  struct jit_error *err)
{
	static const unsigned char HEADER[8] = {
		0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00
	};
	struct buf mod = { 0 }, sec = { 0 }, body = { 0 };
	unsigned char *reach = NULL;
	unsigned *loc = NULL;
	int imp[N_IMPORTS], used[N_IMPORTS] = { 0 };
	int i, k, nimp = 0, nreach = 0;
	const char *msg = "out of memory";

	if (ir->legacy) {
		msg = "legacy mode needs --backend gcc or interp";
		goto fail;
	}
	reach = calloc((size_t)ir->count + 1, 1);
	loc = malloc(((size_t)ir->count + 1) * sizeof (*loc));
	if (!reach || !loc) {
		goto fail;
	}
	reach[ir->root] = 1;
	for (i = ir->root; i >= 0; --i) {
		const struct ir_node *x = &ir->nodes[i];

		if (!reach[i]) {
			continue;
		}
		if (0 <= x->a && IR_VAR != x->op) {
			reach[x->a] = 1;
		}
		if (0 <= x->b) {
			reach[x->b] = 1;
		}
	}
	for (i = 0; i <= ir->root; ++i) {
		if (reach[i]) {
			loc[i] = (unsigned)(ir->nvars + nreach++);
			if (0 <= import_of(ir->nodes[i].op)) {
				used[import_of(ir->nodes[i].op)] = 1;
			}
		}
	}
	if (nreach > WASM_MAX_NODES) {
		msg = "expression too large for the wasm backend";
		goto fail;
	}
	for (k = 0; k < N_IMPORTS; ++k) {
		imp[k] = used[k] ? nimp++ : -1;
	}

	put_bytes(&mod, HEADER, sizeof (HEADER));

	/* type section */
	put_leb(&sec, 3);
	put_u8(&sec, T_FUNC); put_leb(&sec, (uint64_t)ir->nvars);
	for (i = 0; i < ir->nvars; ++i) {
		put_u8(&sec, T_F64);
	}
	put_leb(&sec, 1); put_u8(&sec, T_F64);
	put_u8(&sec, T_FUNC); put_leb(&sec, 1); put_u8(&sec, T_F64);
	put_leb(&sec, 1); put_u8(&sec, T_F64);
	put_u8(&sec, T_FUNC); put_leb(&sec, 2); put_u8(&sec, T_F64);
	put_u8(&sec, T_F64); put_leb(&sec, 1); put_u8(&sec, T_F64);
	put_section(&mod, SEC_TYPE, &sec);

	/* import section (omitted when nothing is imported) */
	if (nimp) {
		sec.len = 0;
		put_leb(&sec, (uint64_t)nimp);
		for (k = 0; k < N_IMPORTS; ++k) {
			if (used[k]) {
				put_name(&sec, "m");
				put_name(&sec, IMPORT_NAME[k]);
				put_u8(&sec, 0x00);                 /* function */
				put_leb(&sec, I_POW == k ? 2 : 1);  /* type index */
			}
		}
		put_section(&mod, SEC_IMPORT, &sec);
	}

	/* function section: one function of type 0 */
	sec.len = 0;
	put_leb(&sec, 1);
	put_leb(&sec, 0);
	put_section(&mod, SEC_FUNC, &sec);

	/* export section: "f" = the function after the imports */
	sec.len = 0;
	put_leb(&sec, 1);
	put_name(&sec, "f");
	put_u8(&sec, 0x00);
	put_leb(&sec, (uint64_t)nimp);
	put_section(&mod, SEC_EXPORT, &sec);

	/* code section */
	if (nreach) {
		put_leb(&body, 1);                  /* one run of locals */
		put_leb(&body, (uint64_t)nreach);
		put_u8(&body, T_F64);
	}
	else {
		put_leb(&body, 0);
	}
	for (i = 0; i <= ir->root; ++i) {
		if (reach[i]) {
			emit_node(&body, &ir->nodes[i], loc, imp);
			put_u8(&body, OP_LOCAL_SET);
			put_leb(&body, loc[i]);
		}
	}
	get(&body, loc[ir->root]);
	put_u8(&body, OP_END);
	sec.len = 0;
	put_leb(&sec, 1);
	put_leb(&sec, body.len);
	put_bytes(&sec, body.p, body.len);
	put_section(&mod, SEC_CODE, &sec);

	if (mod.oom || sec.oom || body.oom) {
		goto fail;
	}
	free(reach);
	free(loc);
	free(sec.p);
	free(body.p);
	*out = mod.p;
	*len = mod.len;
	return 0;
fail:
	free(reach);
	free(loc);
	free(sec.p);
	free(body.p);
	free(mod.p);
	err->pos = -1;
	snprintf(err->msg, sizeof (err->msg), "%s", msg);
	return -1;
}

static const char *
section_name(unsigned id, char *buf, size_t cap)
{
	switch (id) {
	case SEC_TYPE:   return "type section";
	case SEC_IMPORT: return "import section";
	case SEC_FUNC:   return "function section";
	case SEC_EXPORT: return "export section";
	case SEC_CODE:   return "code section";
	default:
		snprintf(buf, cap, "section %u", id);
		return buf;
	}
}

static void
hex_lines(FILE *out, const unsigned char *p, size_t n)
{
	size_t i;

	for (i = 0; i < n; ++i) {
		fprintf(out, "%s%02x", 0 == i % 16 ? (i ? "\n" : "") : " ", p[i]);
	}
	if (n) {
		fputc('\n', out);
	}
}

int
wasm_dump(const unsigned char *code, size_t len, const char *stage, FILE *out)
{
	size_t at = 8;

	if (strcmp(stage, "wasm")) {
		return -1;
	}
	fputs("; header (magic, version)\n", out);
	hex_lines(out, code, len < 8 ? len : 8);
	while (at < len) {
		size_t start = at, size = 0;
		int shift = 0;
		char nb[24];

		++at;                           /* section id */
		while (at < len) {              /* payload size, LEB128 */
			unsigned char b = code[at++];

			size |= (size_t)(b & 0x7f) << shift;
			shift += 7;
			if (!(b & 0x80)) {
				break;
			}
		}
		if (at + size > len) {
			size = len - at;        /* malformed: dump what is there */
		}
		fprintf(out, "; %s\n", section_name(code[start], nb, sizeof (nb)));
		hex_lines(out, code + start, at + size - start);
		at += size;
	}
	return 0;
}
