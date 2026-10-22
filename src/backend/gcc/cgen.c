#include "backend/gcc/cgen.h"
#include "runtime/rt.h"

static int
node(const struct ir_node *n, int i, FILE *out)
{
	switch (n->op) {
	case IR_CONST:
		fprintf(out, "double t%d = %.17g;\n", i, n->imm);
		return 0;
	case IR_NEG:
		fprintf(out, "double t%d = - t%d;\n", i, n->a);
		return 0;
	case IR_ADD:
		fprintf(out, "double t%d = t%d + t%d;\n", i, n->a, n->b);
		return 0;
	case IR_SUB:
		fprintf(out, "double t%d = t%d - t%d;\n", i, n->a, n->b);
		return 0;
	case IR_MUL:
		fprintf(out, "double t%d = t%d * t%d;\n", i, n->a, n->b);
		return 0;
	case IR_DIV:
		fprintf(out, "double t%d = t%d ? (t%d / t%d) : 0.0;\n",
			i, n->b, n->a, n->b);
		return 0;
	default:
		return -1;
	}
}

int
cgen_legacy(const struct ir *ir, FILE *out)
{
	int i;

	if (0 > ir->root || ir->root >= ir->count) {
		return -1;
	}
	fprintf(out, "double evaluate(void) {\n");
	for (i = 0; i <= ir->root; ++i) {
		if (node(&ir->nodes[i], i, out)) {
			return -1;
		}
	}
	fprintf(out, "double result = t%d;\n", ir->root);
	/*
	 * The generated module cannot link against this program, so it calls
	 * jit_rt_sigmoid through its address: the function pointer is printed
	 * as an integer and cast back to double (*)(double) in the C source.
	 */
	fprintf(out,
		"double (*sigmoidptr)(double) = (double (*)(double))%lu;\n",
		(unsigned long)jit_rt_sigmoid);
	fprintf(out, "return sigmoidptr(result);\n");
	fprintf(out, "}\n");
	return 0;
}
