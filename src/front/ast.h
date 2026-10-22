/* Abstract syntax tree of the v2 expression language. */

#ifndef JIT_FRONT_AST_H
#define JIT_FRONT_AST_H

#include <stdio.h>
#include "jit.h"

enum ast_kind {
	AST_NUM,    /* num */
	AST_VAR,    /* var = variable index */
	AST_CONST,  /* fn = constant id, num = its value */
	AST_NEG,    /* a */
	AST_ADD, AST_SUB, AST_MUL, AST_DIV, AST_POW,  /* a, b ('^') */
	AST_CALL    /* fn = function id, a [, b] */
};

struct ast {
	enum ast_kind kind;
	int pos;    /* byte offset of the token that made this node */
	int var;
	int fn;
	double num;
	struct ast *a, *b;
};

/* Built-in functions and constants. */
struct ast_func {
	const char *name;
	int arity;
	int ir_op;  /* enum ir_op */
};

struct ast_const {
	const char *name;
	double value;
};

extern const struct ast_func ast_funcs[];
extern const int ast_nfuncs;
extern const struct ast_const ast_consts[];
extern const int ast_nconsts;

/* Index into ast_funcs / ast_consts, or -1. name need not be terminated. */
int ast_find_func(const char *name, int len);
int ast_find_const(const char *name, int len);

struct ast *ast_new(enum ast_kind kind, int pos);
void ast_free(struct ast *);

/* Indented S-expression, two spaces per level, trailing newline. */
void ast_print(const struct ast *, const char (*vars)[32], FILE *out);

#endif /* JIT_FRONT_AST_H */
