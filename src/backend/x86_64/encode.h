/* x86-64 instruction encoder: a growing byte buffer plus one Intel-syntax
 * listing line per instruction (text in objdump's format). */

#ifndef JIT_X86_ENCODE_H
#define JIT_X86_ENCODE_H

#include <stddef.h>
#include <stdint.h>

/* General registers the backend uses as memory bases / push-pop operands. */
enum x86_gpr { X86_RBX = 3, X86_RBP = 5 };

/* SSE2 two-operand forms (dst op= src). */
enum x86_sse {
	X86_MOVAPD,                 /* 66 0F 28 (register copy) */
	X86_ADDSD, X86_SUBSD, X86_MULSD, X86_DIVSD,
	X86_MINSD, X86_MAXSD, X86_SQRTSD,
	X86_XORPD, X86_ANDPD        /* 66 0F 57 / 66 0F 54 */
};

/* cmpsd predicates (imm8). */
enum x86_cmp { X86_CMP_EQ, X86_CMP_LT, X86_CMP_LE, X86_CMP_UNORD,
	       X86_CMP_NEQ, X86_CMP_NLT, X86_CMP_NLE, X86_CMP_ORD };

struct x86_line {
	size_t off, len;            /* the instruction's bytes in buf->code */
	int node;                   /* IR node number for "; %N", -1 = none */
	char text[64];              /* Intel syntax, objdump spelling */
};

struct x86_buf {
	unsigned char *code; size_t len, cap;
	struct x86_line *lines; size_t nlines, lines_cap;
	int node;                   /* comment given to the next lines */
	int oom;                    /* set once an allocation failed */
};

void x86_buf_init(struct x86_buf *);
void x86_buf_free(struct x86_buf *);

/*
 * xmm registers are 0..15. A rip-relative operand prints as
 * [rip+LABEL] when label != NULL, else [rip+0xDISP] like objdump.
 */
void x86_sse_rr(struct x86_buf *, enum x86_sse, int dst, int src);
void x86_sse_rip(struct x86_buf *, enum x86_sse, int dst, int32_t disp,
		 const char *label);
void x86_movsd_load(struct x86_buf *, int dst, enum x86_gpr base,
		    int32_t disp);                          /* movsd xmm,[base+disp32] */
void x86_movsd_store(struct x86_buf *, enum x86_gpr base, int32_t disp,
		     int src);                              /* movsd [base+disp32],xmm */
void x86_movsd_rip(struct x86_buf *, int dst, int32_t disp,
		   const char *label);                      /* movsd xmm,[rip+disp32] */
void x86_cmpsd(struct x86_buf *, enum x86_cmp, int dst, int src);
void x86_push(struct x86_buf *, enum x86_gpr);
void x86_pop(struct x86_buf *, enum x86_gpr);
void x86_mov_rbp_rsp(struct x86_buf *);
void x86_mov_rbx_rdi(struct x86_buf *);
void x86_sub_rsp(struct x86_buf *, int32_t imm);
void x86_add_rsp(struct x86_buf *, int32_t imm);
/* label != NULL prints "movabs rax,<label>" (symbolic, deterministic). */
void x86_mov_rax_imm64(struct x86_buf *, uint64_t imm, const char *label);
void x86_call_rax(struct x86_buf *);
void x86_ret(struct x86_buf *);

#endif /* JIT_X86_ENCODE_H */
