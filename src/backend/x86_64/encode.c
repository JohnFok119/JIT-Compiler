/* x86-64 encoder. Every function appends the bytes of exactly one
 * instruction and one listing line spelled the way objdump -M intel prints
 * it (tests/check-encoding.sh compares the two). */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "backend/x86_64/encode.h"

static const struct {
	const char *name;
	unsigned char prefix, opcode;
	const char *ptr;            /* memory operand size */
} SSE[] = {
	[X86_MOVAPD] = { "movapd", 0x66, 0x28, "XMMWORD" },
	[X86_ADDSD]  = { "addsd",  0xf2, 0x58, "QWORD" },
	[X86_SUBSD]  = { "subsd",  0xf2, 0x5c, "QWORD" },
	[X86_MULSD]  = { "mulsd",  0xf2, 0x59, "QWORD" },
	[X86_DIVSD]  = { "divsd",  0xf2, 0x5e, "QWORD" },
	[X86_MINSD]  = { "minsd",  0xf2, 0x5d, "QWORD" },
	[X86_MAXSD]  = { "maxsd",  0xf2, 0x5f, "QWORD" },
	[X86_SQRTSD] = { "sqrtsd", 0xf2, 0x51, "QWORD" },
	[X86_XORPD]  = { "xorpd",  0x66, 0x57, "XMMWORD" },
	[X86_ANDPD]  = { "andpd",  0x66, 0x54, "XMMWORD" },
};

static const char *CMP[] = {
	"cmpeqsd", "cmpltsd", "cmplesd", "cmpunordsd",
	"cmpneqsd", "cmpnltsd", "cmpnlesd", "cmpordsd"
};

static const char *
gpr_name(enum x86_gpr r)
{
	return X86_RBX == r ? "rbx" : "rbp";
}

void
x86_buf_init(struct x86_buf *b)
{
	memset(b, 0, sizeof (*b));
	b->node = -1;
}

void
x86_buf_free(struct x86_buf *b)
{
	free(b->code);
	free(b->lines);
	x86_buf_init(b);
}

static void
put(struct x86_buf *b, unsigned char byte)
{
	if (b->oom) {
		return;
	}
	if (b->len == b->cap) {
		size_t cap = b->cap ? 2 * b->cap : 256;
		unsigned char *p = realloc(b->code, cap);
		if (!p) {
			b->oom = 1;
			return;
		}
		b->code = p;
		b->cap = cap;
	}
	b->code[b->len++] = byte;
}

static void
put32(struct x86_buf *b, uint32_t v)
{
	int i;

	for (i = 0; i < 4; ++i) {
		put(b, (unsigned char)(v >> (8 * i)));
	}
}

/* Closes the instruction that started at off with a listing line. */
static void
line(struct x86_buf *b, size_t off, const char *fmt, ...)
{
	struct x86_line *l;
	va_list ap;

	if (b->oom) {
		return;
	}
	if (b->nlines == b->lines_cap) {
		size_t cap = b->lines_cap ? 2 * b->lines_cap : 64;
		struct x86_line *p = realloc(b->lines, cap * sizeof (*p));
		if (!p) {
			b->oom = 1;
			return;
		}
		b->lines = p;
		b->lines_cap = cap;
	}
	l = &b->lines[b->nlines++];
	l->off = off;
	l->len = b->len - off;
	l->node = b->node;
	va_start(ap, fmt);
	vsnprintf(l->text, sizeof (l->text), fmt, ap);
	va_end(ap);
}

/* Mandatory prefix, REX (only when an extended register is used), 0F op. */
static void
sse_head(struct x86_buf *b, unsigned char prefix, unsigned char opcode,
	 int reg, int rm)
{
	unsigned char rex = (unsigned char)(0x40 | ((reg >> 3) & 1) << 2 |
					    ((rm >> 3) & 1));

	put(b, prefix);
	if (0x40 != rex) {
		put(b, rex);
	}
	put(b, 0x0f);
	put(b, opcode);
}

static void
modrm(struct x86_buf *b, int mod, int reg, int rm)
{
	put(b, (unsigned char)(mod << 6 | (reg & 7) << 3 | (rm & 7)));
}

/* "+0x10" / "-0x10" as objdump prints a base+disp32 displacement. */
static void
fmt_disp(char *buf, size_t len, int32_t disp)
{
	if (0 > disp) {
		snprintf(buf, len, "-0x%x", (unsigned)-(int64_t)disp);
	}
	else {
		snprintf(buf, len, "+0x%x", (unsigned)disp);
	}
}

static void
fmt_rip(char *buf, size_t len, int32_t disp, const char *label)
{
	if (label) {
		snprintf(buf, len, "[rip+%s]", label);
	}
	else {
		snprintf(buf, len, "[rip+0x%x]", (unsigned)(uint32_t)disp);
	}
}

void
x86_sse_rr(struct x86_buf *b, enum x86_sse op, int dst, int src)
{
	size_t off = b->len;

	sse_head(b, SSE[op].prefix, SSE[op].opcode, dst, src);
	modrm(b, 3, dst, src);
	line(b, off, "%s xmm%d,xmm%d", SSE[op].name, dst, src);
}

void
x86_sse_rip(struct x86_buf *b, enum x86_sse op, int dst, int32_t disp,
	    const char *label)
{
	size_t off = b->len;
	char mem[48];

	sse_head(b, SSE[op].prefix, SSE[op].opcode, dst, 0);
	modrm(b, 0, dst, 5);
	put32(b, (uint32_t)disp);
	fmt_rip(mem, sizeof (mem), disp, label);
	line(b, off, "%s xmm%d,%s PTR %s", SSE[op].name, dst, SSE[op].ptr, mem);
}

void
x86_movsd_load(struct x86_buf *b, int dst, enum x86_gpr base, int32_t disp)
{
	size_t off = b->len;
	char d[16];

	sse_head(b, 0xf2, 0x10, dst, (int)base);
	modrm(b, 2, dst, (int)base);
	put32(b, (uint32_t)disp);
	fmt_disp(d, sizeof (d), disp);
	line(b, off, "movsd xmm%d,QWORD PTR [%s%s]", dst, gpr_name(base), d);
}

void
x86_movsd_store(struct x86_buf *b, enum x86_gpr base, int32_t disp, int src)
{
	size_t off = b->len;
	char d[16];

	sse_head(b, 0xf2, 0x11, src, (int)base);
	modrm(b, 2, src, (int)base);
	put32(b, (uint32_t)disp);
	fmt_disp(d, sizeof (d), disp);
	line(b, off, "movsd QWORD PTR [%s%s],xmm%d", gpr_name(base), d, src);
}

void
x86_movsd_rip(struct x86_buf *b, int dst, int32_t disp, const char *label)
{
	size_t off = b->len;
	char mem[48];

	sse_head(b, 0xf2, 0x10, dst, 0);
	modrm(b, 0, dst, 5);
	put32(b, (uint32_t)disp);
	fmt_rip(mem, sizeof (mem), disp, label);
	line(b, off, "movsd xmm%d,QWORD PTR %s", dst, mem);
}

void
x86_cmpsd(struct x86_buf *b, enum x86_cmp pred, int dst, int src)
{
	size_t off = b->len;

	sse_head(b, 0xf2, 0xc2, dst, src);
	modrm(b, 3, dst, src);
	put(b, (unsigned char)pred);
	line(b, off, "%s xmm%d,xmm%d", CMP[pred], dst, src);
}

void
x86_push(struct x86_buf *b, enum x86_gpr r)
{
	size_t off = b->len;

	put(b, (unsigned char)(0x50 + r));
	line(b, off, "push %s", gpr_name(r));
}

void
x86_pop(struct x86_buf *b, enum x86_gpr r)
{
	size_t off = b->len;

	put(b, (unsigned char)(0x58 + r));
	line(b, off, "pop %s", gpr_name(r));
}

void
x86_mov_rbp_rsp(struct x86_buf *b)
{
	size_t off = b->len;

	put(b, 0x48);               /* REX.W 89 /r: mov r/m64, r64 */
	put(b, 0x89);
	modrm(b, 3, 4, 5);          /* reg = rsp, rm = rbp */
	line(b, off, "mov rbp,rsp");
}

void
x86_mov_rbx_rdi(struct x86_buf *b)
{
	size_t off = b->len;

	put(b, 0x48);
	put(b, 0x89);
	modrm(b, 3, 7, 3);          /* reg = rdi, rm = rbx */
	line(b, off, "mov rbx,rdi");
}

void
x86_sub_rsp(struct x86_buf *b, int32_t imm)
{
	size_t off = b->len;

	put(b, 0x48);               /* REX.W 81 /5 id */
	put(b, 0x81);
	modrm(b, 3, 5, 4);
	put32(b, (uint32_t)imm);
	line(b, off, "sub rsp,0x%x", (unsigned)imm);
}

void
x86_add_rsp(struct x86_buf *b, int32_t imm)
{
	size_t off = b->len;

	put(b, 0x48);               /* REX.W 81 /0 id */
	put(b, 0x81);
	modrm(b, 3, 0, 4);
	put32(b, (uint32_t)imm);
	line(b, off, "add rsp,0x%x", (unsigned)imm);
}

void
x86_mov_rax_imm64(struct x86_buf *b, uint64_t imm, const char *label)
{
	size_t off = b->len;
	int i;

	put(b, 0x48);               /* REX.W B8+r io */
	put(b, 0xb8);
	for (i = 0; i < 8; ++i) {
		put(b, (unsigned char)(imm >> (8 * i)));
	}
	if (label) {
		line(b, off, "movabs rax,<%s>", label);
	}
	else {
		line(b, off, "movabs rax,0x%llx", (unsigned long long)imm);
	}
}

void
x86_call_rax(struct x86_buf *b)
{
	size_t off = b->len;

	put(b, 0xff);               /* FF /2 */
	modrm(b, 3, 2, 0);
	line(b, off, "call rax");
}

void
x86_ret(struct x86_buf *b)
{
	size_t off = b->len;

	put(b, 0xc3);
	line(b, off, "ret");
}
