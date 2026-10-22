/* Private definition of jit_expr, shared by the engine's own modules. */

#ifndef JIT_API_H
#define JIT_API_H

#include "jit.h"
#include "front/ast.h"
#include "front/lexer.h"
#include "ir/ir.h"

struct jit_expr {
	char *src;                /* copy of the source text */
	struct token_list toks;
	struct ast *ast;
	struct ir ir;             /* ir.vars / ir.nvars = the variable table */
};

#endif /* JIT_API_H */
