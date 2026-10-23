/* IR builder: hash-consing, topological order, variable numbering. */

#include "../check.h"
#include "api.h"
#include "ir/ir.h"
#include <math.h>
#include <stdlib.h>

static double
from_bits(uint64_t u)
{
	double d;
	memcpy(&d, &u, sizeof (d));
	return d;
}

static void
test_const_dedup(void)
{
	struct ir ir;
	int a, b, z, nz, n1, n2, n1b;

	ir_init(&ir);
	a = ir_const(&ir, 1.5);
	b = ir_const(&ir, 1.5);
	CHECK(0 <= a);
	CHECK(a == b);
	z = ir_const(&ir, 0.0);
	nz = ir_const(&ir, -0.0);
	CHECK(z != nz);
	CHECK(z == ir_const(&ir, 0.0));
	CHECK(nz == ir_const(&ir, -0.0));
	/* NaNs keep their bits: same payload dedups, different payloads not */
	n1 = ir_const(&ir, from_bits(0x7ff8000000000001ull));
	n2 = ir_const(&ir, from_bits(0x7ff8000000000002ull));
	n1b = ir_const(&ir, from_bits(0x7ff8000000000001ull));
	CHECK(n1 != n2);
	CHECK(n1 == n1b);
	CHECK(ir_const(&ir, from_bits(0xfff8000000000000ull)) !=
	      ir_const(&ir, from_bits(0x7ff8000000000000ull)));
	CHECK(IR_CONST == ir.nodes[a].op);
	CHECK(-1 == ir.nodes[a].a && -1 == ir.nodes[a].b);
	CHECK_EQ_DBL_BITS(ir.nodes[nz].imm, -0.0);
	CHECK(7 == ir.count);
	ir_free(&ir);
}

static void
test_node_dedup(void)
{
	struct ir ir;
	int x, y, s1, s2, ab, ba, sub, c;

	ir_init(&ir);
	x = ir_var(&ir, 0);
	y = ir_var(&ir, 1);
	CHECK(x != y);
	CHECK(x == ir_var(&ir, 0));
	CHECK(IR_VAR == ir.nodes[x].op && 0 == ir.nodes[x].a);
	s1 = ir_unary(&ir, IR_SIN, x);
	s2 = ir_unary(&ir, IR_SIN, x);
	CHECK(s1 == s2);
	CHECK(-1 == ir.nodes[s1].b);
	CHECK(ir_unary(&ir, IR_COS, x) != s1);
	ab = ir_binary(&ir, IR_ADD, x, y);
	ba = ir_binary(&ir, IR_ADD, y, x);
	CHECK(ab != ba); /* no canonical reordering */
	CHECK(ab == ir_binary(&ir, IR_ADD, x, y));
	sub = ir_binary(&ir, IR_SUB, x, y);
	CHECK(sub != ab);
	c = ir.count;
	/* errors propagate, nothing is added */
	CHECK(-1 == ir_unary(&ir, IR_NEG, -1));
	CHECK(-1 == ir_binary(&ir, IR_MUL, x, -1));
	CHECK(-1 == ir_binary(&ir, IR_MUL, x, 999));
	CHECK(-1 == ir_unary(&ir, IR_ADD, x));
	CHECK(-1 == ir_binary(&ir, IR_SIN, x, y));
	CHECK(c == ir.count);
	/* IR_STEP is a unary op */
	CHECK(0 <= ir_unary(&ir, IR_STEP, x));
	ir_free(&ir);
}

static void
test_table_growth(void)
{
	struct ir ir;
	int i, ok = 1;

	ir_init(&ir);
	for (i = 0; i < 5000; ++i) {
		if (i != ir_const(&ir, (double)i)) {
			ok = 0;
		}
	}
	for (i = 0; i < 5000; ++i) {
		if (i != ir_const(&ir, (double)i)) {
			ok = 0;
		}
	}
	CHECK(ok);
	CHECK(5000 == ir.count);
	ir_free(&ir);
}

static void
test_topological(void)
{
	struct jit_error err;
	jit_expr *e = jit_parse(
		"sin(x)*cos(y) + max(x, -y)^2 - relu(x/y) + pow(e, x) * pi",
		0, &err);
	int i, ok = 1;

	CHECK(e != NULL);
	if (!e) return;
	for (i = 0; i < e->ir.count; ++i) {
		const struct ir_node *n = &e->ir.nodes[i];
		if (IR_CONST == n->op || IR_VAR == n->op) {
			continue;
		}
		if (n->a >= i || n->b >= i || 0 > n->a) {
			ok = 0;
		}
		if (ir_is_binary(n->op) && 0 > n->b) {
			ok = 0;
		}
	}
	CHECK(ok);
	CHECK(e->ir.root == e->ir.count - 1);
	jit_free(e);
}

static void
test_var_numbering(void)
{
	struct jit_error err;
	jit_expr *e = jit_parse("y + x*y + z + x", 0, &err);
	int i, seen = 0;

	CHECK(e != NULL);
	if (!e) return;
	CHECK(3 == jit_nvars(e));
	CHECK_STR(jit_var_name(e, 0), "y");
	CHECK_STR(jit_var_name(e, 1), "x");
	CHECK_STR(jit_var_name(e, 2), "z");
	CHECK(NULL == jit_var_name(e, 3));
	/* first IR node is var y = index 0 */
	CHECK(IR_VAR == e->ir.nodes[0].op && 0 == e->ir.nodes[0].a);
	for (i = 0; i < e->ir.count; ++i) {
		seen += IR_VAR == e->ir.nodes[i].op;
	}
	CHECK(3 == seen); /* one node per variable */
	jit_free(e);
}

static void
test_cse(void)
{
	struct jit_error err;
	jit_expr *e = jit_parse("sin(x)*sin(x) + 3", 0, &err);
	const struct ir_node *mul;
	int i, sins = 0;

	CHECK(e != NULL);
	if (!e) return;
	for (i = 0; i < e->ir.count; ++i) {
		sins += IR_SIN == e->ir.nodes[i].op;
	}
	CHECK(1 == sins);
	mul = &e->ir.nodes[e->ir.nodes[e->ir.root].a];
	CHECK(IR_MUL == mul->op && mul->a == mul->b);
	CHECK(0 == e->ir.legacy);
	jit_free(e);

	e = jit_parse("1-1", 1, &err);
	CHECK(e != NULL);
	if (!e) return;
	CHECK(1 == e->ir.legacy);
	CHECK(2 == e->ir.count); /* const 1 shared */
	jit_free(e);
}

int
main(void)
{
	test_const_dedup();
	test_node_dedup();
	test_table_growth();
	test_topological();
	test_var_numbering();
	test_cse();
	return CHECK_REPORT("test_ir");
}
