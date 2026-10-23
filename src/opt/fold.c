#include "opt/opt.h"

/* Rule 1: all operands constant -> the interpreter's own function, same libm call. */
int
opt_fold(struct ir *out, enum ir_op op, int a, int b)
{
	double x, y = 0.0;

	if (!opt_is_const(out, a) || (ir_is_binary(op) && !opt_is_const(out, b))) {
		return OPT_NONE;
	}
	x = out->nodes[a].imm;
	if (ir_is_binary(op)) {
		y = out->nodes[b].imm;
	}
	return ir_const(out, ir_apply(0, op, x, y));
}
