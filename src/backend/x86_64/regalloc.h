/* Linear-scan register allocation over the reachable, topologically ordered
 * IR (backends.md §x86-64): value i lives on [i, last use]; 16 xmm
 * registers; when none is free the active value with the furthest end is
 * spilled to a stack slot [rbp - off] and reloaded before its next use.
 * Calls clobber every xmm, so values live across a call are stored to their
 * slots first. IR_CONST / IR_VAR values are rematerialised (reloaded from the
 * constant pool / vars array) instead of being stored. */

#ifndef JIT_X86_REGALLOC_H
#define JIT_X86_REGALLOC_H

#include "backend/x86_64/encode.h"
#include "ir/ir.h"

#define RA_NREGS 16

struct ra {
	const struct ir *ir;
	int n;                      /* ir->root + 1 */
	char *live;                 /* reachable from the root */
	int *num;                   /* dense number (as ir_print), -1 if dead */
	int *end;                   /* index of the last user; root: itself */
	int *reg;                   /* current register or -1 */
	int *slot;                  /* stack slot or -1 */
	char *stored;               /* the slot holds the value */
	int owner[RA_NREGS];        /* value held by each register, -1 = free */
	int *free_slots; int nfree; /* slots of dead values, for reuse */
	int nslots;                 /* slots ever used (frame size) */
};

int  ra_init(struct ra *, const struct ir *);   /* 0, or -1 out of memory */
void ra_free(struct ra *);

int     ra_remat(const struct ra *, int v);      /* IR_CONST or IR_VAR */
int32_t ra_slot_disp(int slot);                  /* rbp-relative offset */

/*
 * A free register; if none, spills the value with the furthest end that is
 * not in `pinned` (bit mask of registers), storing it to its slot unless it
 * is rematerialisable or already stored.
 */
int  ra_alloc(struct ra *, struct x86_buf *, unsigned pinned);
void ra_bind(struct ra *, int v, int reg);       /* v now lives in reg */
void ra_unbind(struct ra *, int v);              /* v leaves its register */
void ra_release(struct ra *, int v);             /* v is dead: free reg + slot */
/* Stores every register value with end > pos (live across the call at pos). */
void ra_save_live_across(struct ra *, struct x86_buf *, int pos);
void ra_clobber(struct ra *);                    /* after a call: all xmm lost */

#endif /* JIT_X86_REGALLOC_H */
