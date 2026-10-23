#include "opt/opt.h"

/* x * 1 and 1 * x: exact in IEEE (NaN stays NaN, signed zero kept). */
static int
mul_one(struct ir *out, int a, int b)
{
	if (opt_is_bits(out, b, 1.0)) {
		return a;
	}
	if (opt_is_bits(out, a, 1.0)) {
		return b;
	}
	return OPT_NONE;
}

/* x / 1: exact. */
static int
div_one(struct ir *out, int a, int b)
{
	return opt_is_bits(out, b, 1.0) ? a : OPT_NONE;
}

/* x - (+0): exact, even for x = -0 (-0 - +0 = -0). x - (-0) is NOT: -0 - -0 = +0. */
static int
sub_zero(struct ir *out, int a, int b)
{
	return opt_is_bits(out, b, 0.0) ? a : OPT_NONE;
}

/* -(-x): negation only flips the sign bit, twice is the identity. */
static int
neg_neg(struct ir *out, int a)
{
	return IR_NEG == out->nodes[a].op ? out->nodes[a].a : OPT_NONE;
}

/* x + (-0) and (-0) + x: exact, even for x = -0 (-0 + -0 = -0). x + (+0) is NOT: -0 + +0 = +0. */
static int
add_neg_zero(struct ir *out, int a, int b)
{
	if (opt_is_bits(out, b, -0.0)) {
		return a;
	}
	if (opt_is_bits(out, a, -0.0)) {
		return b;
	}
	return OPT_NONE;
}

/* x * 2 and 2 * x: x + x is the same single rounding (doubling is exact). */
static int
mul_two(struct ir *out, int a, int b)
{
	if (opt_is_bits(out, b, 2.0)) {
		return ir_binary(out, IR_ADD, a, a);
	}
	if (opt_is_bits(out, a, 2.0)) {
		return ir_binary(out, IR_ADD, b, b);
	}
	return OPT_NONE;
}

int
opt_simplify(struct ir *out, enum ir_op op, int a, int b)
{
	int r = OPT_NONE;

	switch (op) {
	case IR_MUL:
		r = mul_one(out, a, b);
		if (OPT_NONE == r) {
			r = mul_two(out, a, b);
		}
		return r;
	case IR_DIV:
		return div_one(out, a, b);
	case IR_SUB:
		return sub_zero(out, a, b);
	case IR_NEG:
		return neg_neg(out, a);
	case IR_ADD:
		return add_neg_zero(out, a, b);
	default:
		return OPT_NONE;
	}
}
