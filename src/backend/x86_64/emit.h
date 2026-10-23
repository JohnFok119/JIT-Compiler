/* x86-64 backend entry points (used by src/api.c). */

#ifndef JIT_X86_EMIT_H
#define JIT_X86_EMIT_H

#include <stddef.h>
#include <stdio.h>
#include "ir/ir.h"
#include "jit.h"

struct x86_code {
	void *mem;                  /* mapped image: code, padding, pool; r-x */
	size_t size;                /* bytes mapped (code + padding + pool) */
	size_t code_size;           /* bytes of instructions */
	char *asm_text;             /* the --emit asm listing */
};

/* Compiles ir (not legacy) into executable memory. 0, or -1 with err set. */
int  x86_compile(const struct ir *, struct x86_code *, struct jit_error *);
void x86_code_free(struct x86_code *);
/* stage "asm" | "hex"; 0 ok, -1 unknown stage */
int  x86_dump(const struct x86_code *, const char *stage, FILE *);

/* execmem.c: mmap RW, copy, mprotect RX (never W and X together). */
void *x86_exec_alloc(const void *image, size_t size);   /* NULL on failure */
void  x86_exec_free(void *mem, size_t size);

#endif /* JIT_X86_EMIT_H */
