#include <math.h>
#include "opt/opt.h"

/* pow(x, 1): glibc pow(x, 1) returns x exactly. */
static int
pow_one(struct ir *out, int a, int b)
{
	return opt_is_bits(out, b, 1.0) ? a : OPT_NONE;
}

/* pow(x, 2): one correctly rounded product either way (fuzzer-verified on glibc). */
static int
pow_two(struct ir *out, int a, int b)
{
	return opt_is_bits(out, b, 2.0) ? ir_binary(out, IR_MUL, a, a) : OPT_NONE;
}

/* x / c, c = +-2^k with c and 1/c normal: both are the same exact scaling, one rounding. */
static int
div_pow2(struct ir *out, int a, int b)
{
	double c, inv;
	int e;

	if (!opt_is_const(out, b)) {
		return OPT_NONE;
	}
	c = out->nodes[b].imm;
	if (!isnormal(c) || 0.5 != frexp(fabs(c), &e)) {
		return OPT_NONE;
	}
	inv = 1.0 / c;
	if (!isnormal(inv)) {
		return OPT_NONE;
	}
	return ir_binary(out, IR_MUL, a, ir_const(out, inv));
}

int
opt_strength(struct ir *out, enum ir_op op, int a, int b)
{
	int r;

	if (IR_POW == op) {
		r = pow_one(out, a, b);
		return OPT_NONE == r ? pow_two(out, a, b) : r;
	}
	if (IR_DIV == op) {
		return div_pow2(out, a, b);
	}
	return OPT_NONE;
}
