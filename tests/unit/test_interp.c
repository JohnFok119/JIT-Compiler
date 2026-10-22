/* Reference interpreter: every op bitwise against the C expression. */

#include "../check.h"
#include "jit.h"
#include "ir/ir.h"
#include "runtime/rt.h"
#include <math.h>

static double
from_bits(uint64_t u)
{
	double d;
	memcpy(&d, &u, sizeof (d));
	return d;
}

/* C reference for each op (a, b already loaded from volatile storage) */
static double
ref(enum ir_op op, double a, double b)
{
	switch (op) {
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
	case IR_SIGMOID: return 1.0 / (1.0 + exp(-a));
	case IR_ADD:     return a + b;
	case IR_SUB:     return a - b;
	case IR_MUL:     return a * b;
	case IR_DIV:     return a / b;
	case IR_MIN:     return (a < b) ? a : b;
	case IR_MAX:     return (a > b) ? a : b;
	case IR_POW:     return pow(a, b);
	case IR_STEP:    return (a > 0.0) ? 1.0 : 0.0;
	default:         return 0.0;
	}
}

/* Evaluates op(var0[, var1]) through the interpreter. */
static double
run(enum ir_op op, double a, double b, int legacy)
{
	struct ir ir;
	double vars[2];
	double r;
	int x, y;

	vars[0] = a;
	vars[1] = b;
	ir_init(&ir);
	ir.legacy = legacy;
	ir.nvars = 2;
	x = ir_var(&ir, 0);
	y = ir_var(&ir, 1);
	ir.root = ir_is_binary(op) ? ir_binary(&ir, op, x, y)
				   : ir_unary(&ir, op, x);
	r = ir_interp(&ir, vars);
	ir_free(&ir);
	return r;
}

static void
test_each_op(void)
{
	static const enum ir_op ops[] = {
		IR_NEG, IR_ABS, IR_SQRT, IR_RELU, IR_SIN, IR_COS, IR_TAN,
		IR_EXP, IR_LOG, IR_TANH, IR_SIGMOID, IR_ADD, IR_SUB, IR_MUL,
		IR_DIV, IR_MIN, IR_MAX, IR_POW, IR_STEP
	};
	volatile double vals[14];
	unsigned i, j, k;
	int bad = 0, total = 0;

	vals[0] = 0.0;
	vals[1] = -0.0;
	vals[2] = 1.0;
	vals[3] = -1.0;
	vals[4] = 0.5;
	vals[5] = -2.5;
	vals[6] = 3.0;
	vals[7] = 1e308;
	vals[8] = -1e-310; /* subnormal */
	vals[9] = INFINITY;
	vals[10] = -INFINITY;
	vals[11] = NAN;
	vals[12] = 0.1;
	vals[13] = 710.0;
	for (k = 0; k < sizeof (ops) / sizeof (ops[0]); ++k) {
		for (i = 0; i < 14; ++i) {
			for (j = 0; j < 14; ++j) {
				double a = vals[i], b = vals[j];
				double got = run(ops[k], a, b, 0);
				double want = ref(ops[k], a, b);
				++total;
				if (memcmp(&got, &want, sizeof (got))) {
					++bad;
					printf("  op %s(%g, %g): %.17g vs %.17g\n",
					       ir_op_name(ops[k]), a, b, got,
					       want);
				}
			}
		}
	}
	CHECK(0 == bad);
	CHECK(19 * 14 * 14 == total);
}

static void
test_nan_rules(void)
{
	double nan = NAN;

	CHECK_EQ_DBL_BITS(run(IR_RELU, nan, 0, 0), 0.0);
	CHECK_EQ_DBL_BITS(run(IR_RELU, -0.0, 0, 0), 0.0);
	CHECK_EQ_DBL_BITS(run(IR_RELU, 2.0, 0, 0), 2.0);
	/* min/max: the second operand wins unless the comparison is true */
	CHECK_EQ_DBL_BITS(run(IR_MIN, nan, 1.0, 0), 1.0);
	CHECK(isnan(run(IR_MIN, 1.0, nan, 0)));
	CHECK_EQ_DBL_BITS(run(IR_MAX, nan, 1.0, 0), 1.0);
	CHECK(isnan(run(IR_MAX, 1.0, nan, 0)));
	CHECK_EQ_DBL_BITS(run(IR_MIN, -0.0, 0.0, 0), 0.0);
	CHECK_EQ_DBL_BITS(run(IR_MIN, 0.0, -0.0, 0), -0.0);
	CHECK_EQ_DBL_BITS(run(IR_MAX, -0.0, 0.0, 0), 0.0);
	CHECK_EQ_DBL_BITS(run(IR_MAX, 0.0, -0.0, 0), -0.0);
	/* NaN payload passes through unchanged where C passes it */
	CHECK_EQ_DBL_BITS(run(IR_ABS, from_bits(0xfff8000000000123ull), 0, 0),
			  from_bits(0x7ff8000000000123ull));
}

static void
test_step(void)
{
	CHECK_EQ_DBL_BITS(run(IR_STEP, NAN, 0, 0), 0.0);
	CHECK_EQ_DBL_BITS(run(IR_STEP, 0.0, 0, 0), 0.0);
	CHECK_EQ_DBL_BITS(run(IR_STEP, -0.0, 0, 0), 0.0);
	CHECK_EQ_DBL_BITS(run(IR_STEP, -1.0, 0, 0), 0.0);
	CHECK_EQ_DBL_BITS(run(IR_STEP, 1e-300, 0, 0), 1.0);
	CHECK_EQ_DBL_BITS(run(IR_STEP, INFINITY, 0, 0), 1.0);
}

static void
test_legacy(void)
{
	volatile double half = 0.5;

	/* x/0 = 0, then sigmoid(0) = 0.5 */
	CHECK_EQ_DBL_BITS(run(IR_DIV, 1.0, 0.0, 1), 0.5);
	CHECK_EQ_DBL_BITS(run(IR_DIV, 1.0, -0.0, 1), 0.5);
	CHECK_EQ_DBL_BITS(run(IR_DIV, 1.0, 2.0, 1), jit_rt_sigmoid(half));
	CHECK_EQ_DBL_BITS(run(IR_ADD, 1.0, -1.0, 1), 0.5);
	/* normal mode keeps IEEE division */
	CHECK(isinf(run(IR_DIV, 1.0, 0.0, 0)));
}

static void
test_api(void)
{
	struct jit_error err;
	double vars[3] = { 2.0, 3.0, 0.25 };
	volatile double x = 2.0, y = 3.0, z = 0.25;
	jit_expr *e = jit_parse("x*y + sin(z)^2 - sigmoid(x/z)", 0, &err);

	CHECK(e != NULL);
	if (!e) return;
	CHECK_EQ_DBL_BITS(jit_interp(e, vars),
			  x * y + pow(sin(z), 2.0) - jit_rt_sigmoid(x / z));
	jit_free(e);

	e = jit_parse("1/0", 1, &err);
	CHECK(e != NULL);
	if (!e) return;
	CHECK_EQ_DBL_BITS(jit_interp(e, NULL), 0.5);
	jit_free(e);
}

int
main(void)
{
	test_each_op();
	test_nan_rules();
	test_step();
	test_legacy();
	test_api();
	return CHECK_REPORT("test_interp");
}
