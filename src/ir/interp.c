/* Reference interpreter: the definition of every expression's meaning. */

#include <math.h>
#include <stdlib.h>
#include "ir/ir.h"
#include "runtime/rt.h"

static double
eval(const struct ir *ir, const struct ir_node *n, const double *v,
     const double *vars)
{
	double a = 0 <= n->a && IR_VAR != n->op ? v[n->a] : 0.0;
	double b = 0 <= n->b ? v[n->b] : 0.0;

	switch (n->op) {
	case IR_CONST:   return n->imm;
	case IR_VAR:     return vars[n->a];
	case IR_NEG:     return -a;
	case IR_ABS:     return fabs(a);
	case IR_SQRT:    return sqrt(a);
	case IR_RELU:    return (a > 0.0) ? a : 0.0;
	case IR_SIN:     return sin(a);
	case IR_COS:     return cos(a);
	case IR_TAN:     return tan(a);
	case IR_EXP:     return exp(a);
	case IR_LOG:     return log(a);
	case IR_TANH:    return tanh(a);
	case IR_SIGMOID: return jit_rt_sigmoid(a);
	case IR_ADD:     return a + b;
	case IR_SUB:     return a - b;
	case IR_MUL:     return a * b;
	case IR_DIV:
		if (ir->legacy) {
			return b ? (a / b) : 0.0;
		}
		return a / b;
	case IR_MIN:     return (a < b) ? a : b;
	case IR_MAX:     return (a > b) ? a : b;
	case IR_POW:     return pow(a, b);
	case IR_STEP:    return (a > 0.0) ? 1.0 : 0.0;
	}
	return NAN;
}

double
ir_interp(const struct ir *ir, const double *vars)
{
	double small[256], *v = small, result;
	int i;

	if (0 > ir->root || ir->root >= ir->count) {
		return NAN;
	}
	if (ir->root + 1 > (int)(sizeof (small) / sizeof (small[0])) &&
	    !(v = malloc((size_t)(ir->root + 1) * sizeof (*v)))) {
		return NAN;
	}
	/* topological order: every operand is computed before its user */
	for (i = 0; i <= ir->root; ++i) {
		v[i] = eval(ir, &ir->nodes[i], v, vars);
	}
	result = v[ir->root];
	if (v != small) {
		free(v);
	}
	return ir->legacy ? jit_rt_sigmoid(result) : result;
}
