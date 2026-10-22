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
/* stage: "tokens" | "ast" | "ir"; 0 ok, -1 unknown stage */
int         jit_dump(const jit_expr *, const char *stage, FILE *out);

#endif /* JIT_H */
