#ifndef B3_MACHINE_H
#define B3_MACHINE_H

#include "s1.h"

/* Experimental opcodes added in B2. The original seven are unchanged. */
enum {
    B3_OP_CALL = 8,       /* CALL  a : R.push(return), IP = a          */
    B3_OP_RET,            /* RET      : IP = R.pop()                   */
    B3_OP_TO_R,           /* >R       : R.push(DS.pop())               */
    B3_OP_FROM_R,         /* R>       : DS.push(R.pop())               */
    B3_OP_R_FETCH,        /* R@       : DS.push(R.peek())              */
    B3_OP_BRANCH          /* BRANCH a : IP = a                         */
};

/* Native emitters. CALL and BRANCH are two-cell instructions. */
cell b3_native_call_fwd(void);
cell b3_native_branch_fwd(void);
void b3_native_ret(void);
void b3_native_toR(void);
void b3_native_fromR(void);
void b3_native_fetchR(void);

/* Measurement counters in the B2 machine copy (semantics unchanged). */
extern long b3_insns;
extern long b3_min_sp;
extern long b3_min_rp;
extern long b9_op_count[6];
extern long b11_total_ops;
extern long b11_calls;
extern long b11_rets;
extern long b11_to_r;
extern long b11_from_r;
extern long b11_fetch_r;
void b11_reset_counts(void);
extern int b3_bad_opcode;
void b3_reset_counts(void);

#endif
