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

/* ---- normal mode: double evaluate(const double *v) ---- */

static void
constant(double x, FILE *out)
{
	if (x != x) {
		fprintf(out, "(0.0/0.0)");
	}
	else if (x > 1.7976931348623157e308) {
		fprintf(out, "(1.0/0.0)");
	}
	else if (x < -1.7976931348623157e308) {
		fprintf(out, "(-1.0/0.0)");
	}
	else {
		fprintf(out, "%a", x);       /* hex float: every bit survives */
	}
}

static int
node_normal(const struct ir_node *n, int i, FILE *out)
{
	const char *fn = NULL;

	fprintf(out, "double t%d = ", i);
	switch (n->op) {
	case IR_CONST:
		constant(n->imm, out);
		break;
	case IR_VAR:
		fprintf(out, "v[%d]", n->a);
		break;
	case IR_NEG:     fprintf(out, "-t%d", n->a); break;
	case IR_RELU:    fprintf(out, "(t%d > 0.0) ? t%d : 0.0", n->a, n->a); break;
	case IR_STEP:    fprintf(out, "(t%d > 0.0 ? 1.0 : 0.0)", n->a); break;
	case IR_SIGMOID: fprintf(out, "sigmoidptr(t%d)", n->a); break;
	case IR_ADD:     fprintf(out, "t%d + t%d", n->a, n->b); break;
	case IR_SUB:     fprintf(out, "t%d - t%d", n->a, n->b); break;
	case IR_MUL:     fprintf(out, "t%d * t%d", n->a, n->b); break;
	case IR_DIV:     fprintf(out, "t%d / t%d", n->a, n->b); break;
	case IR_MIN:     fprintf(out, "(t%d < t%d) ? t%d : t%d",
				 n->a, n->b, n->a, n->b); break;
	case IR_MAX:     fprintf(out, "(t%d > t%d) ? t%d : t%d",
				 n->a, n->b, n->a, n->b); break;
	case IR_ABS:     fn = "fabs"; break;
	case IR_SQRT:    fn = "sqrt"; break;
	case IR_SIN:     fn = "sin"; break;
	case IR_COS:     fn = "cos"; break;
	case IR_TAN:     fn = "tan"; break;
	case IR_EXP:     fn = "exp"; break;
	case IR_LOG:     fn = "log"; break;
	case IR_TANH:    fn = "tanh"; break;
	case IR_POW:
		fprintf(out, "pow(t%d, t%d)", n->a, n->b);
		break;
	default:
		return -1;
	}
	if (fn) {
		fprintf(out, "%s(t%d)", fn, n->a);
	}
	fprintf(out, ";\n");
	return 0;
}

int
cgen_normal(const struct ir *ir, FILE *out)
{
	int i, sigmoid = 0;

	if (0 > ir->root || ir->root >= ir->count) {
		return -1;
	}
	for (i = 0; i <= ir->root; ++i) {
		sigmoid |= IR_SIGMOID == ir->nodes[i].op;
	}
	fprintf(out, "#include <math.h>\n");
	fprintf(out, "double evaluate(const double *v) {\n");
	if (sigmoid) {
		/*
		 * Same trick as legacy mode: the module is not linked against
		 * this program, so jit_rt_sigmoid is reached through its
		 * address, printed as an integer and cast back to a function
		 * pointer in the C source.
		 */
		fprintf(out,
			"double (*sigmoidptr)(double) = (double (*)(double))%lu;\n",
			(unsigned long)jit_rt_sigmoid);
	}
	for (i = 0; i <= ir->root; ++i) {
		if (node_normal(&ir->nodes[i], i, out)) {
			return -1;
		}
	}
	fprintf(out, "return t%d;\n", ir->root);
	fprintf(out, "}\n");
	return 0;
}
