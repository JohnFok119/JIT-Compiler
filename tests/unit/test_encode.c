/* Encoder: emits every instruction form over low (xmm0-7) and high
 * (xmm8-15) registers into build/enc.bin and the encoder's own listing into
 * build/enc.lst; tests/check-encoding.sh then disassembles enc.bin with
 * objdump and compares it line by line with enc.lst. */

#include "../check.h"
#include "backend/x86_64/encode.h"
#include <stdlib.h>

static const int32_t DISPS[] = { 0, 8, 0x38, 0x1000, -16, -0x88, -0x1000 };
#define NDISPS (sizeof (DISPS) / sizeof (DISPS[0]))
static const int32_t RIP_DISPS[] = { 0, 0x10, 0x7f, 0x80, 0x12345 };
#define NRIP (sizeof (RIP_DISPS) / sizeof (RIP_DISPS[0]))

int
main(void)
{
	struct x86_buf b;
	enum x86_sse op;
	size_t i, k, total = 0;
	int d, s, p;
	FILE *bin, *lst;

	x86_buf_init(&b);
	for (op = X86_MOVAPD; op <= X86_ANDPD; ++op) {
		for (d = 0; d < 16; ++d) {
			for (s = 0; s < 16; ++s) {
				x86_sse_rr(&b, op, d, s);
			}
			for (k = 0; k < NRIP; ++k) {
				x86_sse_rip(&b, op, d, RIP_DISPS[k], NULL);
			}
		}
	}
	for (d = 0; d < 16; ++d) {
		for (k = 0; k < NDISPS; ++k) {
			x86_movsd_load(&b, d, X86_RBX, DISPS[k]);
			x86_movsd_load(&b, d, X86_RBP, DISPS[k]);
			x86_movsd_store(&b, X86_RBX, DISPS[k], d);
			x86_movsd_store(&b, X86_RBP, DISPS[k], d);
		}
		for (k = 0; k < NRIP; ++k) {
			x86_movsd_rip(&b, d, RIP_DISPS[k], NULL);
		}
		for (s = 0; s < 16; ++s) {
			for (p = X86_CMP_EQ; p <= X86_CMP_ORD; ++p) {
				x86_cmpsd(&b, (enum x86_cmp)p, d, s);
			}
		}
	}
	x86_push(&b, X86_RBP);
	x86_push(&b, X86_RBX);
	x86_pop(&b, X86_RBX);
	x86_pop(&b, X86_RBP);
	x86_mov_rbp_rsp(&b);
	x86_mov_rbx_rdi(&b);
	x86_sub_rsp(&b, 8);
	x86_sub_rsp(&b, 0x88);
	x86_sub_rsp(&b, 0x12348);
	x86_add_rsp(&b, 8);
	x86_add_rsp(&b, 0x88);
	x86_add_rsp(&b, 0x12348);
	x86_mov_rax_imm64(&b, 0x123456789abcdef0ull, NULL);
	x86_mov_rax_imm64(&b, 0x00007f0012345678ull, NULL);
	x86_call_rax(&b);
	x86_ret(&b);

	CHECK(!b.oom);
	/* lines tile the buffer exactly, in order */
	for (i = 0; i < b.nlines; ++i) {
		CHECK(b.lines[i].off == total);
		CHECK(b.lines[i].len >= 1 && b.lines[i].len <= 15);
		total += b.lines[i].len;
	}
	CHECK(total == b.len);

	/* spot checks against hand-encoded bytes (SDM) */
	{
		struct x86_buf t;
		static const unsigned char want[] = {
			0x66, 0x0f, 0x28, 0xc1,                 /* movapd xmm0,xmm1 */
			0x66, 0x45, 0x0f, 0x28, 0xc7,           /* movapd xmm8,xmm15 */
			0xf2, 0x41, 0x0f, 0x58, 0xc1,           /* addsd xmm0,xmm9 */
			0xf2, 0x44, 0x0f, 0x10, 0x15, 0x10, 0, 0, 0, /* movsd xmm10,[rip+0x10] */
			0xf2, 0x0f, 0xc2, 0xc1, 0x01,           /* cmpltsd xmm0,xmm1 */
			0x48, 0xb8, 1, 2, 3, 4, 5, 6, 7, 8,     /* movabs rax,... */
			0xff, 0xd0, 0xc3                        /* call rax; ret */
		};
		x86_buf_init(&t);
		x86_sse_rr(&t, X86_MOVAPD, 0, 1);
		x86_sse_rr(&t, X86_MOVAPD, 8, 15);
		x86_sse_rr(&t, X86_ADDSD, 0, 9);
		x86_movsd_rip(&t, 10, 0x10, NULL);
		x86_cmpsd(&t, X86_CMP_LT, 0, 1);
		x86_mov_rax_imm64(&t, 0x0807060504030201ull, NULL);
		x86_call_rax(&t);
		x86_ret(&t);
		CHECK(t.len == sizeof (want) && !memcmp(t.code, want, sizeof (want)));
		CHECK_STR(t.lines[3].text, "movsd xmm10,QWORD PTR [rip+0x10]");
		x86_buf_free(&t);
	}

	bin = fopen("build/enc.bin", "wb");
	lst = fopen("build/enc.lst", "w");
	CHECK(bin && lst);
	if (bin && lst) {
		CHECK(b.len == fwrite(b.code, 1, b.len, bin));
		for (i = 0; i < b.nlines; ++i) {
			fprintf(lst, "%s\n", b.lines[i].text);
		}
	}
	if (bin) {
		fclose(bin);
	}
	if (lst) {
		fclose(lst);
	}
	printf("test_encode: %zu instructions, %zu bytes -> build/enc.bin\n",
	       b.nlines, b.len);
	x86_buf_free(&b);
	return CHECK_REPORT("test_encode.c");
}
