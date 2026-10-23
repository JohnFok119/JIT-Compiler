#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ir/ir.h"

void
ir_init(struct ir *ir)
{
	memset(ir, 0, sizeof (*ir));
	ir->root = -1;
}

void
ir_free(struct ir *ir)
{
	if (ir) {
		free(ir->nodes);
		free(ir->table);
		ir_init(ir);
	}
}

static uint64_t
bits(double v)
{
	uint64_t u;

	memcpy(&u, &v, sizeof (u));
	return u;
}

static uint64_t
hash(const struct ir_node *n)
{
	uint64_t h = 0xcbf29ce484222325ull;

	h = (h ^ (uint64_t)n->op) * 0x100000001b3ull;
	h = (h ^ (uint64_t)(uint32_t)n->a) * 0x100000001b3ull;
	h = (h ^ (uint64_t)(uint32_t)n->b) * 0x100000001b3ull;
	h = (h ^ bits(n->imm)) * 0x100000001b3ull;
	return h ^ (h >> 29);
}

static int
equal(const struct ir_node *x, const struct ir_node *y)
{
	return x->op == y->op && x->a == y->a && x->b == y->b &&
	       bits(x->imm) == bits(y->imm);
}

/* Rebuilds the table with cap slots (power of two). */
static int
rehash(struct ir *ir, int cap)
{
	int *table;
	int i;

	if (!(table = malloc((size_t)cap * sizeof (*table)))) {
		return -1;
	}
	for (i = 0; i < cap; ++i) {
		table[i] = -1;
	}
	for (i = 0; i < ir->count; ++i) {
		uint64_t s = hash(&ir->nodes[i]) & (uint64_t)(cap - 1);
		while (0 <= table[s]) {
			s = (s + 1) & (uint64_t)(cap - 1);
		}
		table[s] = i;
	}
	free(ir->table);
	ir->table = table;
	ir->table_cap = cap;
	return 0;
}

static int
intern(struct ir *ir, enum ir_op op, int a, int b, double imm)
{
	struct ir_node n;
	uint64_t s;

	memset(&n, 0, sizeof (n));
	n.op = op;
	n.a = a;
	n.b = b;
	n.imm = imm;
	/* keep the load factor at most 1/2 */
	if (2 * (ir->count + 1) > ir->table_cap) {
		if (rehash(ir, ir->table_cap ? 2 * ir->table_cap : 64)) {
			return -1;
		}
	}
	s = hash(&n) & (uint64_t)(ir->table_cap - 1);
	while (0 <= ir->table[s]) {
		if (equal(&ir->nodes[ir->table[s]], &n)) {
			return ir->table[s];
		}
		s = (s + 1) & (uint64_t)(ir->table_cap - 1);
	}
	if (ir->count == ir->cap) {
		int cap = ir->cap ? 2 * ir->cap : 32;
		struct ir_node *v = realloc(ir->nodes,
					    (size_t)cap * sizeof (*v));
		if (!v) {
			return -1;
		}
		ir->nodes = v;
		ir->cap = cap;
	}
	ir->nodes[ir->count] = n;
	ir->table[s] = ir->count;
	return ir->count++;
}

int
ir_const(struct ir *ir, double v)
{
	return intern(ir, IR_CONST, -1, -1, v);
}

int
ir_var(struct ir *ir, int index)
{
	if (0 > index) {
		return -1;
	}
	return intern(ir, IR_VAR, index, -1, 0.0);
}

int
ir_is_unary(enum ir_op op)
{
	return (op >= IR_NEG && op <= IR_SIGMOID) || IR_STEP == op;
}

int
ir_is_binary(enum ir_op op)
{
	return op >= IR_ADD && op <= IR_POW;
}

int
ir_unary(struct ir *ir, enum ir_op op, int a)
{
	if (0 > a || a >= ir->count || !ir_is_unary(op)) {
		return -1;
	}
	return intern(ir, op, a, -1, 0.0);
}

int
ir_binary(struct ir *ir, enum ir_op op, int a, int b)
{
	if (0 > a || 0 > b || a >= ir->count || b >= ir->count ||
	    !ir_is_binary(op)) {
		return -1;
	}
	return intern(ir, op, a, b, 0.0);
}

const char *
ir_op_name(enum ir_op op)
{
	switch (op) {
	case IR_CONST:   return "const";
	case IR_VAR:     return "var";
	case IR_NEG:     return "neg";
	case IR_ABS:     return "abs";
	case IR_SQRT:    return "sqrt";
	case IR_RELU:    return "relu";
	case IR_SIN:     return "sin";
	case IR_COS:     return "cos";
	case IR_TAN:     return "tan";
	case IR_EXP:     return "exp";
	case IR_LOG:     return "log";
	case IR_TANH:    return "tanh";
	case IR_SIGMOID: return "sigmoid";
	case IR_ADD:     return "add";
	case IR_SUB:     return "sub";
	case IR_MUL:     return "mul";
	case IR_DIV:     return "div";
	case IR_MIN:     return "min";
	case IR_MAX:     return "max";
	case IR_POW:     return "pow";
	case IR_STEP:    return "step";
	}
	return "?";
}
