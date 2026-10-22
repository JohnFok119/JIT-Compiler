#include <stdlib.h>
#include <string.h>
#include "front/ast.h"
#include "ir/ir.h"

const struct ast_func ast_funcs[] = {
	{ "sin",     1, IR_SIN },
	{ "cos",     1, IR_COS },
	{ "tan",     1, IR_TAN },
	{ "exp",     1, IR_EXP },
	{ "log",     1, IR_LOG },
	{ "sqrt",    1, IR_SQRT },
	{ "abs",     1, IR_ABS },
	{ "tanh",    1, IR_TANH },
	{ "sigmoid", 1, IR_SIGMOID },
	{ "relu",    1, IR_RELU },
	{ "min",     2, IR_MIN },
	{ "max",     2, IR_MAX },
	{ "pow",     2, IR_POW }
};
const int ast_nfuncs = (int)(sizeof (ast_funcs) / sizeof (ast_funcs[0]));

const struct ast_const ast_consts[] = {
	{ "pi", 3.141592653589793 },
	{ "e",  2.718281828459045 }
};
const int ast_nconsts = (int)(sizeof (ast_consts) / sizeof (ast_consts[0]));

static int
same(const char *a, const char *name, int len)
{
	return (int)strlen(a) == len && !memcmp(a, name, (size_t)len);
}

int
ast_find_func(const char *name, int len)
{
	int i;

	for (i = 0; i < ast_nfuncs; ++i) {
		if (same(ast_funcs[i].name, name, len)) {
			return i;
		}
	}
	return -1;
}

int
ast_find_const(const char *name, int len)
{
	int i;

	for (i = 0; i < ast_nconsts; ++i) {
		if (same(ast_consts[i].name, name, len)) {
			return i;
		}
	}
	return -1;
}

struct ast *
ast_new(enum ast_kind kind, int pos)
{
	struct ast *n = calloc(1, sizeof (*n));

	if (n) {
		n->kind = kind;
		n->pos = pos;
	}
	return n;
}

void
ast_free(struct ast *n)
{
	if (n) {
		ast_free(n->a);
		ast_free(n->b);
		free(n);
	}
}

static const char *
binary_name(enum ast_kind k)
{
	switch (k) {
	case AST_ADD: return "add";
	case AST_SUB: return "sub";
	case AST_MUL: return "mul";
	case AST_DIV: return "div";
	case AST_POW: return "pow";
	default:      return "?";
	}
}

static void
print_node(const struct ast *n, const char (*vars)[32], int depth, FILE *out)
{
	char num[32];

	fprintf(out, "%*s(", 2 * depth, "");
	switch (n->kind) {
	case AST_NUM:
		ir_fmt_double(num, sizeof (num), n->num);
		fprintf(out, "num %s)", num);
		return;
	case AST_VAR:
		fprintf(out, "var %s)", vars[n->var]);
		return;
	case AST_CONST:
		fprintf(out, "const %s)", ast_consts[n->fn].name);
		return;
	case AST_NEG:
		fprintf(out, "neg");
		break;
	case AST_CALL:
		fprintf(out, "call %s", ast_funcs[n->fn].name);
		break;
	default:
		fprintf(out, "%s", binary_name(n->kind));
		break;
	}
	if (n->a) {
		fputc('\n', out);
		print_node(n->a, vars, depth + 1, out);
	}
	if (n->b) {
		fputc('\n', out);
		print_node(n->b, vars, depth + 1, out);
	}
	fputc(')', out);
}

void
ast_print(const struct ast *n, const char (*vars)[32], FILE *out)
{
	print_node(n, vars, 0, out);
	fputc('\n', out);
}
