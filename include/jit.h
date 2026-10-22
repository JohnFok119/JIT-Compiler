/* Public API of the JIT expression engine. */

#ifndef JIT_H
#define JIT_H

#include <stdio.h>

#define JIT_MAX_VARS 8

typedef struct jit_expr jit_expr;

struct jit_error {
	int pos;       /* 0-based byte offset in the source; -1 = no position */
	char msg[128];
};

enum jit_backend { JIT_INTERP, JIT_X86, JIT_GCC, JIT_WASM };

/* Parses src (legacy != 0: original assignment semantics). NULL on error. */
jit_expr   *jit_parse(const char *src, int legacy, struct jit_error *err);
void        jit_free(jit_expr *);
int         jit_nvars(const jit_expr *);
const char *jit_var_name(const jit_expr *, int i);
double      jit_interp(const jit_expr *, const double *vars);
/* level 0: nothing; 2: all optimizer passes (no-op on legacy expressions). 0 ok, -1 bad level or out of memory (expression unchanged). */
int         jit_optimize(jit_expr *, int level);
/* stage: "tokens" | "ast" | "ir"; 0 ok, -1 unknown stage */
int         jit_dump(const jit_expr *, const char *stage, FILE *out);

/* Compiled functions. jit_compile takes a snapshot: the jit_fn stays valid
 * after jit_free(expr). */
typedef struct jit_fn jit_fn;
typedef double (*jit_native)(const double *vars);
jit_fn   *jit_compile(const jit_expr *, enum jit_backend, struct jit_error *err); /* NULL on error, err->pos = -1 */
double    jit_call(const jit_fn *, const double *vars);
jit_native jit_entry(const jit_fn *);          /* raw pointer for tight loops (benchmarks); NULL for JIT_INTERP */
void      jit_fn_free(jit_fn *);
int       jit_fn_dump(const jit_fn *, const char *stage, FILE *out);   /* "asm" | "hex" (x86) | "c" (gcc); 0 ok, -1 unknown stage */
size_t    jit_fn_code_size(const jit_fn *);    /* bytes mapped (code + constant pool); 0 for JIT_INTERP */

#endif /* JIT_H */
