#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ir/ir.h"

void
ir_fmt_double(char *buf, size_t len, double v)
{
	int prec;

	if (isnan(v)) {
		snprintf(buf, len, "nan");
		return;
	}
	for (prec = 1; prec <= 17; ++prec) {
		double back;

		snprintf(buf, len, "%.*g", prec, v);
		back = strtod(buf, NULL);
		if (!memcmp(&back, &v, sizeof (v))) {
			break;
		}
	}
	/* normal numbers keep the plain notation of %.15g (100, not 1e+02) */
	if (prec < 15 && FP_NORMAL == fpclassify(v)) {
		snprintf(buf, len, "%.15g", v);
	}
}

void
ir_print(const struct ir *ir, FILE *out)
{
	int *num, i, next = 0;
	char *live;
	char buf[32];

	if (0 > ir->root || ir->root >= ir->count) {
		return;
	}
	num = malloc((size_t)(ir->root + 1) * sizeof (*num));
	live = calloc((size_t)(ir->root + 1), 1);
	if (!num || !live) {
		free(num);
		free(live);
		fprintf(out, "; out of memory\n");
		return;
	}
	/* mark reachable nodes: operands always have smaller indices */
	live[ir->root] = 1;
	for (i = ir->root; i >= 0; --i) {
		const struct ir_node *n = &ir->nodes[i];
		if (!live[i] || IR_CONST == n->op || IR_VAR == n->op) {
			continue;
		}
		live[n->a] = 1;
		if (0 <= n->b) {
			live[n->b] = 1;
		}
	}
	for (i = 0; i <= ir->root; ++i) {
		const struct ir_node *n = &ir->nodes[i];

		if (!live[i]) {
			continue;
		}
		num[i] = next++;
		fprintf(out, "%%%d = ", num[i]);
		if (IR_CONST == n->op) {
			ir_fmt_double(buf, sizeof (buf), n->imm);
			fprintf(out, "const %s\n", buf);
		}
		else if (IR_VAR == n->op) {
			fprintf(out, "var %s\n", ir->vars[n->a]);
		}
		else if (ir_is_binary(n->op)) {
			fprintf(out, "%s %%%d, %%%d\n", ir_op_name(n->op),
				num[n->a], num[n->b]);
		}
		else {
			fprintf(out, "%s %%%d\n", ir_op_name(n->op),
				num[n->a]);
		}
	}
	fprintf(out, "ret %%%d\n", num[ir->root]);
	free(num);
	free(live);
}
