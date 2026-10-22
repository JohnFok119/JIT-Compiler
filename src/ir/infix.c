/* Infix printer: the expression in the language's own syntax, with the
 * fewest parentheses that keep the tree's structure. Shared subexpressions
 * are printed inline (repeated text). Not parseable back in two cases:
 * IR_STEP prints as step(...), and non-finite constants print as 1e999,
 * -1e999 and (1e999-1e999). */

#include <math.h>
#include "ir/ir.h"

/* Precedence levels; an operand needs at least the level its slot demands. */
enum { L_ADD = 1, L_MUL = 2, L_NEG = 3, L_POW = 4, L_ATOM = 5 };

static int
const_level(double v)
{
	if (isnan(v)) {
		return L_ATOM;          /* printed with its own parentheses */
	}
	return signbit(v) ? L_NEG : L_ATOM;
}

static int
level(const struct ir *ir, int n)
{
	switch (ir->nodes[n].op) {
	case IR_CONST: return const_level(ir->nodes[n].imm);
	case IR_ADD: case IR_SUB: return L_ADD;
	case IR_MUL: case IR_DIV: return L_MUL;
	case IR_NEG: return L_NEG;
	case IR_POW: return L_POW;
	default: return L_ATOM;
	}
}

static void print(const struct ir *, int, FILE *);

/* Prints node n; parenthesised when its level is below `need`. */
static void
operand(const struct ir *ir, int n, int need, FILE *out)
{
	int paren = level(ir, n) < need;

	if (paren) {
		fputc('(', out);
	}
	print(ir, n, out);
	if (paren) {
		fputc(')', out);
	}
}

static void
print(const struct ir *ir, int n, FILE *out)
{
	const struct ir_node *nd = &ir->nodes[n];
	const char *sym = NULL;
	char buf[40];

	switch (nd->op) {
	case IR_CONST:
		if (isnan(nd->imm)) {
			fputs("(1e999-1e999)", out);
		}
		else if (isinf(nd->imm)) {
			fputs(nd->imm < 0 ? "-1e999" : "1e999", out);
		}
		else {
			ir_fmt_double(buf, sizeof (buf), nd->imm);
			fputs(buf, out);
		}
		return;
	case IR_VAR:
		fputs(ir->vars[nd->a], out);
		return;
	case IR_NEG:
		fputc('-', out);
		operand(ir, nd->a, L_NEG, out);
		return;
	case IR_ADD: sym = " + "; break;
	case IR_SUB: sym = " - "; break;
	case IR_MUL: sym = " * "; break;
	case IR_DIV: sym = " / "; break;
	case IR_POW: sym = "^"; break;
	default: break;
	}
	if (sym) {
		/* left-assoc: right operand one level tighter; ^ takes an atom
		   on the left and a unary on the right */
		int lv = level(ir, n);
		int ln = IR_POW == nd->op ? L_ATOM : lv;
		int rn = IR_POW == nd->op ? L_NEG : lv + 1;

		operand(ir, nd->a, ln, out);
		fputs(sym, out);
		operand(ir, nd->b, rn, out);
		return;
	}
	/* function call */
	fprintf(out, "%s(", ir_op_name(nd->op));
	print(ir, nd->a, out);
	if (ir_is_binary(nd->op)) {
		fputs(", ", out);
		print(ir, nd->b, out);
	}
	fputc(')', out);
}

void
ir_infix(const struct ir *ir, FILE *out)
{
	print(ir, ir->root, out);
	fputc('\n', out);
}
