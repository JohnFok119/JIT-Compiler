/* Linear-scan register allocator state, driven node by node by emit.c. */

#include <stdlib.h>
#include <string.h>
#include "backend/x86_64/regalloc.h"

int
ra_init(struct ra *ra, const struct ir *ir)
{
	int i, next = 0;

	memset(ra, 0, sizeof (*ra));
	ra->ir = ir;
	ra->n = ir->root + 1;
	ra->live = calloc((size_t)ra->n, 1);
	ra->stored = calloc((size_t)ra->n, 1);
	ra->num = malloc((size_t)ra->n * sizeof (int));
	ra->end = malloc((size_t)ra->n * sizeof (int));
	ra->reg = malloc((size_t)ra->n * sizeof (int));
	ra->slot = malloc((size_t)ra->n * sizeof (int));
	ra->free_slots = malloc((size_t)ra->n * sizeof (int));
	if (!ra->live || !ra->stored || !ra->num || !ra->end || !ra->reg ||
	    !ra->slot || !ra->free_slots) {
		ra_free(ra);
		return -1;
	}
	for (i = 0; i < RA_NREGS; ++i) {
		ra->owner[i] = -1;
	}
	/* reachability: operands always have smaller indices */
	ra->live[ir->root] = 1;
	for (i = ir->root; i >= 0; --i) {
		const struct ir_node *n = &ir->nodes[i];
		if (!ra->live[i] || IR_CONST == n->op || IR_VAR == n->op) {
			continue;
		}
		ra->live[n->a] = 1;
		if (0 <= n->b) {
			ra->live[n->b] = 1;
		}
	}
	for (i = 0; i < ra->n; ++i) {
		ra->num[i] = ra->live[i] ? next++ : -1;
		ra->end[i] = i;
		ra->reg[i] = -1;
		ra->slot[i] = -1;
	}
	/* live interval [i, last use] */
	for (i = 0; i < ra->n; ++i) {
		const struct ir_node *n = &ir->nodes[i];
		if (!ra->live[i] || IR_CONST == n->op || IR_VAR == n->op) {
			continue;
		}
		ra->end[n->a] = i;
		if (0 <= n->b) {
			ra->end[n->b] = i;
		}
	}
	return 0;
}

void
ra_free(struct ra *ra)
{
	free(ra->live);
	free(ra->stored);
	free(ra->num);
	free(ra->end);
	free(ra->reg);
	free(ra->slot);
	free(ra->free_slots);
	memset(ra, 0, sizeof (*ra));
}

int
ra_remat(const struct ra *ra, int v)
{
	enum ir_op op = ra->ir->nodes[v].op;

	return IR_CONST == op || IR_VAR == op;
}

/* Slot k sits below the saved rbx at [rbp - 8]. */
int32_t
ra_slot_disp(int slot)
{
	return -16 - 8 * slot;
}

static void
store(struct ra *ra, struct x86_buf *b, int v)
{
	int node = b->node;

	if (ra_remat(ra, v) || ra->stored[v]) {
		return;
	}
	if (0 > ra->slot[v]) {
		ra->slot[v] = ra->nfree ? ra->free_slots[--ra->nfree]
				       : ra->nslots++;
	}
	b->node = ra->num[v];
	x86_movsd_store(b, X86_RBP, ra_slot_disp(ra->slot[v]), ra->reg[v]);
	b->node = node;
	ra->stored[v] = 1;
}

int
ra_alloc(struct ra *ra, struct x86_buf *b, unsigned pinned)
{
	int r, victim = -1;

	for (r = 0; r < RA_NREGS; ++r) {
		if (0 > ra->owner[r]) {
			return r;
		}
	}
	/* none free: spill the active value with the furthest end */
	for (r = 0; r < RA_NREGS; ++r) {
		if (pinned & (1u << r)) {
			continue;
		}
		if (0 > victim ||
		    ra->end[ra->owner[r]] > ra->end[ra->owner[victim]]) {
			victim = r;
		}
	}
	store(ra, b, ra->owner[victim]);
	ra_unbind(ra, ra->owner[victim]);
	return victim;
}

void
ra_bind(struct ra *ra, int v, int reg)
{
	ra->owner[reg] = v;
	ra->reg[v] = reg;
}

void
ra_unbind(struct ra *ra, int v)
{
	if (0 <= ra->reg[v]) {
		ra->owner[ra->reg[v]] = -1;
		ra->reg[v] = -1;
	}
}

void
ra_release(struct ra *ra, int v)
{
	ra_unbind(ra, v);
	if (0 <= ra->slot[v]) {
		ra->free_slots[ra->nfree++] = ra->slot[v];
		ra->slot[v] = -1;
		ra->stored[v] = 0;
	}
}

void
ra_save_live_across(struct ra *ra, struct x86_buf *b, int pos)
{
	int r;

	for (r = 0; r < RA_NREGS; ++r) {
		int v = ra->owner[r];
		if (0 <= v && ra->end[v] > pos) {
			store(ra, b, v);
		}
	}
}

void
ra_clobber(struct ra *ra)
{
	int r;

	for (r = 0; r < RA_NREGS; ++r) {
		if (0 <= ra->owner[r]) {
			ra->reg[ra->owner[r]] = -1;
			ra->owner[r] = -1;
		}
	}
}
