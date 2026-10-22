#include "front/ast.h"
#include "ir/ir.h"

static int
lower(struct ir *ir, const struct ast *n)
{
	int a, b;

	switch (n->kind) {
	case AST_NUM:
	case AST_CONST:
		return ir_const(ir, n->num);
	case AST_VAR:
		return ir_var(ir, n->var);
	case AST_NEG:
		return ir_unary(ir, IR_NEG, lower(ir, n->a));
	case AST_CALL:
		a = lower(ir, n->a);
		if (2 == ast_funcs[n->fn].arity) {
			b = lower(ir, n->b);
			return ir_binary(ir, (enum ir_op)ast_funcs[n->fn].ir_op,
					 a, b);
		}
		return ir_unary(ir, (enum ir_op)ast_funcs[n->fn].ir_op, a);
	case AST_ADD:
	case AST_SUB:
	case AST_MUL:
	case AST_DIV:
	case AST_POW:
		a = lower(ir, n->a);
		b = lower(ir, n->b);
		return ir_binary(ir,
				 AST_ADD == n->kind ? IR_ADD :
				 AST_SUB == n->kind ? IR_SUB :
				 AST_MUL == n->kind ? IR_MUL :
				 AST_DIV == n->kind ? IR_DIV : IR_POW,
				 a, b);
	}
	return -1;
}

int
ir_lower(struct ir *ir, const struct ast *root)
{
	ir->root = lower(ir, root);
	return 0 > ir->root ? -1 : 0;
}
