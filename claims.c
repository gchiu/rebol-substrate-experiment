/* claims.c - tests of the designer's claims (the "isotope/antiform" argument)
 * against the FROZEN S1 primitive set.
 *
 * This file does not modify, extend, reinterpret, or add primitives to S1.
 * It builds substrate programs out of the same frozen primitives (LIT, DUP,
 * DROP, @, !, 0BRANCH, HOST) plus the derived macros in s1.h, and asks whether
 * the behaviours the designer says require a richer value/effect model are
 * actually *mechanically* out of reach of S1, or merely *unsafe/unergonomic*.
 *
 * The centrepiece is the designer's own falsifiable challenge: a loop wrapper
 * (FOR-BOTH) that can both be broken out of and return multiple values, with
 * the two mechanisms composing without either knowing about the other.
 */
#include "s1.h"
#include <stdio.h>

static int failures = 0;

#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (cond) { printf("    ok: %s\n", msg); }                    \
        else      { printf("    FAIL: %s\n", msg); failures++; }      \
    } while (0)

/* host-visible variable cells (disjoint from tests.c: 16..36 and
 * adversarial.c: 64..77). */
enum {
    C_BRK_RP = 128, C_BRK_IP = 129,
    C_C_SP   = 130, C_C_RP   = 131, C_C_IP   = 132,
    C_TMP    = 133,
    C_SEL    = 134,
    C_I      = 135, C_ACC    = 136,
    C_N      = 137, C_TARGET = 138, C_NVAL   = 139,
    C_RAN    = 140, C_SP0    = 141, C_ARITY  = 142
};

/* --- shared emitters (identical to the ones in adversarial.c) ------------ */

static cell emit_install_frame(cell rp_cell, cell ip_cell) {
    asm_lit(REG_RP); asm_fetch(); asm_lit(rp_cell); asm_store();
    cell p = asm_lit_fwd(); asm_lit(ip_cell); asm_store();
    return p;
}

static void emit_restore_rp_jump(cell rp_cell, cell ip_cell) {
    asm_lit(rp_cell); asm_fetch(); asm_lit(REG_RP); asm_store();
    asm_lit(ip_cell); asm_fetch(); asm_lit(REG_IP); asm_store();
}

static void emit_do_throw(cell sp_cell, cell rp_cell, cell ip_cell, cell err) {
    asm_lit(err); asm_lit(C_TMP); asm_store();
    asm_lit(sp_cell); asm_fetch(); asm_lit(REG_SP); asm_store();
    asm_lit(rp_cell); asm_fetch(); asm_lit(REG_RP); asm_store();
    asm_lit(C_TMP); asm_fetch();
    asm_lit(ip_cell); asm_fetch(); asm_lit(REG_IP); asm_store();
}

static cell emit_install_catch(cell sp_cell, cell rp_cell, cell ip_cell) {
    asm_lit(REG_SP); asm_fetch(); asm_lit(sp_cell); asm_store();
    asm_lit(REG_RP); asm_fetch(); asm_lit(rp_cell); asm_store();
    cell p = asm_lit_fwd(); asm_lit(ip_cell); asm_store();
    return p;
}

/* --- tests --------------------------------------------------------------- */

/* CLAIM 1: the designer's falsifiable challenge.
 *
 * "write one function that hits two of [the distinctions] at once - a loop
 * wrapper that can both be broken out of and return multiple values - and show
 * the two mechanisms composing without either one knowing about the other."
 *
 * for-both ( N -- i acc ) iterates i over 0..N; each iteration adds i into an
 * accumulator. A leaf (reached through an *unaware* intermediate `body`) breaks
 * when acc >= target. Either way - break or exhaustion - for-both returns TWO
 * values (i, acc). The break restores only RP/IP; the two values ride the data
 * stack. The two mechanisms never touch each other's channel. */
static void claim1_for_both(void) {
    printf("claim 1: for-both loop wrapper that breaks AND returns two values\n");

    /* leaf: acc += i; if acc >= target, break (non-local). */
    cell leaf = asm_here();
    asm_lit(C_ACC); asm_fetch(); asm_lit(C_I); asm_fetch(); asm_add();
    asm_lit(C_ACC); asm_store();
    asm_lit(C_ACC); asm_fetch(); asm_lit(C_TARGET); asm_fetch(); asm_ge();
    cell jleaf = asm_zbranch_fwd();
    emit_restore_rp_jump(C_BRK_RP, C_BRK_IP);   /* break */
    asm_patch_here(jleaf);
    asm_exit();

    /* body: unaware of break; CALL leaf and return. */
    cell body = asm_here(); asm_call(leaf); asm_exit();

    /* for-both ( N -- i acc ): install break frame, loop, return two values. */
    cell for_both = asm_here();
    asm_lit(C_N); asm_store();                       /* C_N = N (pop arg) */
    cell p_exit = emit_install_frame(C_BRK_RP, C_BRK_IP);
    asm_lit(0); asm_lit(C_I); asm_store();
    asm_lit(0); asm_lit(C_ACC); asm_store();
    cell lloop = asm_here();
    asm_lit(C_I); asm_fetch(); asm_lit(C_N); asm_fetch(); asm_lt();
    cell jend = asm_zbranch_fwd();                   /* i >= N -> normal exit */
    asm_call(body);
    asm_lit(C_I); asm_fetch(); asm_lit(1); asm_add(); asm_lit(C_I); asm_store();
    asm_branch(lloop);
    asm_patch_here(jend);                            /* normal exit lands here */
    asm_patch_here(p_exit);                          /* break exit: same point */
    asm_lit(C_I); asm_fetch();                       /* return TWO values */
    asm_lit(C_ACC); asm_fetch();
    asm_exit();

    cell main = asm_here();
    asm_lit(C_NVAL); asm_fetch();                    /* N from a cell */
    asm_call(for_both);
    asm_halt();

    /* break case: target 10, N 100 -> break at i=4 (acc reaches 10). */
    s1_reset();
    s1_set_mem(C_TARGET, 10);
    s1_set_mem(C_NVAL, 100);
    s1_run(main);
    cell i = s1_mem(s1_mem(REG_SP) + 1);             /* below top */
    cell acc = s1_top();
    CHECK(i == 4 && acc == 10, "break at acc=10 -> two values (i=4, acc=10)");

    /* exhaustion case: target 1000, N 5 -> runs to completion. */
    s1_reset();
    s1_set_mem(C_TARGET, 1000);
    s1_set_mem(C_NVAL, 5);
    s1_run(main);
    i = s1_mem(s1_mem(REG_SP) + 1);
    acc = s1_top();
    CHECK(i == 5 && acc == 10, "exhaustion -> two values (i=5, acc=10)");
}

/* CLAIM 2: the central structural claim - "a control/effect signal must travel
 * as an ordinary value in order to cross code that is unaware of it."
 *
 * Refutation: a break fires inside `leaf` and crosses f1/f2/f3, all unaware
 * pass-throughs. The break is a pure register jump (RP/IP) - it carries NO
 * value. A value (42) sitting on the data stack is untouched: SP does not
 * move, so nothing was pushed or popped as a "signal". */
static void claim2_signal_not_a_value(void) {
    printf("claim 2: a break crosses unaware code as a jump, not a value\n");

    cell leaf = asm_here();
    emit_restore_rp_jump(C_BRK_RP, C_BRK_IP);        /* break; never returns */

    cell f3 = asm_here(); asm_call(leaf); asm_exit();
    cell f2 = asm_here(); asm_call(f3);   asm_exit();
    cell f1 = asm_here(); asm_call(f2);   asm_exit();

    cell main = asm_here();
    asm_lit(42);                                     /* value in flight */
    asm_lit(REG_SP); asm_fetch(); asm_lit(C_SP0); asm_store(); /* snapshot SP */
    cell p_exit = emit_install_frame(C_BRK_RP, C_BRK_IP);
    asm_call(f1);                                    /* break fires inside */
    asm_lit(999); asm_halt();                        /* normal path (skipped) */
    asm_patch_here(p_exit);                          /* break lands here */
    asm_halt();

    s1_reset();
    s1_run(main);
    CHECK(s1_mem(C_SP0) == s1_mem(REG_SP),
          "break moved RP/IP but not SP (no signal value was pushed)");
    CHECK(s1_top() == 42,
          "the value 42 in flight survived the break untouched");
}

/* CLAIM 3: distinction "failed vs returned something falsey" (one of the eight).
 *
 * A normal return of the falsey value 0 is a VALUE left on the stack by an
 * EXIT; a failure (42) is a non-local throw (a jump restoring SP/RP). S1
 * distinguishes them mechanically by control path, not by tagging a value. */
static void claim3_failed_vs_falsey(void) {
    printf("claim 3: failed (throw) vs falsey return (0) are distinct\n");

    cell compute = asm_here();
    asm_lit(C_SEL); asm_fetch();
    cell jfail = asm_zbranch_fwd();                  /* sel==0 -> normal path */
    emit_do_throw(C_C_SP, C_C_RP, C_C_IP, 42);       /* sel!=0 -> throw 42 */
    asm_patch_here(jfail);
    asm_lit(0); asm_exit();                          /* normal: falsey 0 */

    cell main = asm_here();
    cell p_cip = emit_install_catch(C_C_SP, C_C_RP, C_C_IP);
    asm_call(compute);
    asm_lit(1); asm_lit(C_RAN); asm_store();         /* normal path marker */
    cell bfin = asm_branch_fwd();
    asm_patch_here(p_cip);                           /* handler: 42 on stack */
    asm_lit(0); asm_lit(C_RAN); asm_store();         /* failed path marker */
    asm_patch_here(bfin);
    asm_halt();

    s1_reset(); s1_set_mem(C_SEL, 0); s1_run(main);
    CHECK(s1_top() == 0 && s1_mem(C_RAN) == 1,
          "falsey 0 is a value on the normal path");

    s1_reset(); s1_set_mem(C_SEL, 1); s1_run(main);
    CHECK(s1_top() == 42 && s1_mem(C_RAN) == 0,
          "failure 42 is a jump, not the falsey value");
}

/* CLAIM 4: the value/arity distinctions ("no value vs NONE", "several values
 * vs one block", "spliced vs appended"). S1 CAN observe arity by reading SP
 * before and after a call - the machine state is transparent. But the
 * observation is manual and unenforced: the machine cannot tell a block
 * reference from a scalar, because they are the same word type. */
static void claim4_arity_observable_not_enforced(void) {
    printf("claim 4: arity (0/1/several) is SP-observable, not enforced\n");

    /* fn: returns C_SEL values. sel=0 -> none; 1 -> one (42); 2 -> two. */
    cell fn = asm_here();
    asm_lit(C_SEL); asm_fetch();
    asm_dup(); asm_lit(0); asm_eq();
    cell j0 = asm_zbranch_fwd();                     /* sel==0 -> push nothing */
    asm_drop();
    cell b0 = asm_branch_fwd();
    asm_patch_here(j0);
    asm_dup(); asm_lit(1); asm_eq();
    cell j1 = asm_zbranch_fwd();                     /* sel==1 -> push 42 */
    asm_drop();
    asm_lit(42);
    cell b1 = asm_branch_fwd();
    asm_patch_here(j1);
    asm_drop();
    asm_lit(10); asm_lit(20);                        /* sel==2 -> two values */
    asm_patch_here(b0);
    asm_patch_here(b1);
    asm_exit();

    cell main = asm_here();
    asm_lit(REG_SP); asm_fetch(); asm_lit(C_SP0); asm_store(); /* SP before */
    asm_call(fn);
    /* read "after" FIRST, while the stack is still empty of our own
     * bookkeeping values: the @ of SP must not be preceded by a push, or the
     * push itself decrements the SP we are trying to measure. */
    asm_lit(REG_SP); asm_fetch(); asm_lit(C_TMP); asm_store(); /* after -> C_TMP */
    asm_lit(C_SP0); asm_fetch();                     /* before */
    asm_lit(C_TMP); asm_fetch();                     /* after  */
    asm_sub();                                       /* before - after = arity */
    asm_lit(C_ARITY); asm_store();
    asm_halt();

    cell exp_arity[3] = {0, 1, 2};
    for (cell s = 0; s < 3; s++) {
        s1_reset();
        s1_set_mem(C_SEL, s);
        s1_run(main);
        char buf[64];
        snprintf(buf, sizeof buf, "sel=%ld -> observed arity %ld",
                 (long)s, (long)s1_mem(C_ARITY));
        CHECK(s1_mem(C_ARITY) == exp_arity[s], buf);
    }
}

int run_claim_tests(void) {
    asm_reset();
    claim1_for_both();
    claim2_signal_not_a_value();
    claim3_failed_vs_falsey();
    claim4_arity_observable_not_enforced();
    return failures;
}
