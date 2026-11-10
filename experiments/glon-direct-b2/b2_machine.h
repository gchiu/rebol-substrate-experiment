#ifndef B2_MACHINE_H
#define B2_MACHINE_H

#include "s1.h"

/* Experimental opcodes added in B2. The original seven are unchanged. */
enum {
    B2_OP_CALL = 8,       /* CALL  a : R.push(return), IP = a          */
    B2_OP_RET,            /* RET      : IP = R.pop()                   */
    B2_OP_TO_R,           /* >R       : R.push(DS.pop())               */
    B2_OP_FROM_R,         /* R>       : DS.push(R.pop())               */
    B2_OP_R_FETCH,        /* R@       : DS.push(R.peek())              */
    B2_OP_BRANCH          /* BRANCH a : IP = a                         */
};

/* Native emitters. CALL and BRANCH are two-cell instructions. */
cell b2_native_call_fwd(void);
cell b2_native_branch_fwd(void);
void b2_native_ret(void);
void b2_native_toR(void);
void b2_native_fromR(void);
void b2_native_fetchR(void);

/* Measurement counters in the B2 machine copy (semantics unchanged). */
extern long b2_insns;
extern long b2_min_sp;
extern long b2_min_rp;
extern int b2_bad_opcode;
void b2_reset_counts(void);

#endif
