/* adversarial.c - combinations of effects over the FROZEN S1 primitives.
 *
 * tests.c exercises one control behaviour at a time. These tests exercise
 * COMBINATIONS: a break firing several calls deep inside a user-defined loop,
 * that loop also carrying multiple returned values, nested scopes where break
 * and a non-local return target different dynamic levels, transfers crossing
 * functions that are unaware of them, one function with four distinct outcomes,
 * and two independent mechanisms active at once.
 *
 * Everything below is built from the frozen primitive set (LIT, DUP, DROP, @,
 * !, 0BRANCH, HOST) plus the derived-word macros in s1.h. No VM change.
 */
#include "s1.h"
#include <stdio.h>

static int failures = 0;

#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (cond) { printf("    ok: %s\n", msg); }                    \
        else      { printf("    FAIL: %s\n", msg); failures++; }      \
    } while (0)

/* host-visible variable cells for these tests (disjoint from tests.c) */
enum {
    A_BRK_RP = 64, A_BRK_IP = 65,
    A_RET_RP = 66, A_RET_IP = 67,
    A_C_SP   = 68, A_C_RP   = 69, A_C_IP   = 70,
    A_TMP    = 71,
    A_SEL    = 72, A_SHAPE  = 73,
    A_I      = 74, A_ACC    = 75,
    A_R1     = 76, A_R2     = 77
};

/* --- shared emitters (all derive over @/! of the memory-mapped regs) ------ */

/* Install a non-local frame: save the current RP and a (to-be-patched) exit IP
 * into rp_cell/ip_cell. Returns the placeholder operand address to patch. */
static cell emit_install_frame(cell rp_cell, cell ip_cell) {
    asm_lit(REG_RP); asm_fetch(); asm_lit(rp_cell); asm_store();
    cell p = asm_lit_fwd(); asm_lit(ip_cell); asm_store();
    return p;
}

/* Non-local jump used by break and non-local return: restore RP to the saved
 * marker and jump to the saved exit IP. (break/return share this mechanically;
 * they differ only in which cells and what value, if any, is left.) */
static void emit_restore_rp_jump(cell rp_cell, cell ip_cell) {
    asm_lit(rp_cell); asm_fetch(); asm_lit(REG_RP); asm_store();
    asm_lit(ip_cell); asm_fetch(); asm_lit(REG_IP); asm_store();
}

/* THROW err: stash err, restore {SP,RP}, re-push err, jump to saved IP. */
static void emit_do_throw(cell sp_cell, cell rp_cell, cell ip_cell, cell err) {
    asm_lit(err); asm_lit(A_TMP); asm_store();
    asm_lit(sp_cell); asm_fetch(); asm_lit(REG_SP); asm_store();
    asm_lit(rp_cell); asm_fetch(); asm_lit(REG_RP); asm_store();
    asm_lit(A_TMP); asm_fetch();
    asm_lit(ip_cell); asm_fetch(); asm_lit(REG_IP); asm_store();
}

/* Install a CATCH handler: save {SP,RP} and a (patched) IP. */
static cell emit_install_catch(cell sp_cell, cell rp_cell, cell ip_cell) {
    asm_lit(REG_SP); asm_fetch(); asm_lit(sp_cell); asm_store();
    asm_lit(REG_RP); asm_fetch(); asm_lit(rp_cell); asm_store();
    cell p = asm_lit_fwd(); asm_lit(ip_cell); asm_store();
    return p;
}

/* --- tests --------------------------------------------------------------- */

/* ADV 1: user-defined loop, body calls a 4-deep chain, break from the leaf. */
static void adv1(void) {
    printf("adv 1: break from 4 nested calls inside a user-defined loop\n");

    cell leaf = asm_here();               /* f4: ACC++; if ACC>=5 break */
    asm_lit(A_ACC); asm_fetch(); asm_lit(1); asm_add(); asm_lit(A_ACC); asm_store();
    asm_lit(A_ACC); asm_fetch(); asm_lit(5); asm_ge();
    cell jleaf = asm_zbranch_fwd();
    emit_restore_rp_jump(A_BRK_RP, A_BRK_IP);
    asm_patch_here(jleaf);
    asm_exit();

    cell f3 = asm_here(); asm_call(leaf); asm_exit();
    cell f2 = asm_here(); asm_call(f3);   asm_exit();
    cell f1 = asm_here(); asm_call(f2);   asm_exit();

    cell loopfn = asm_here();
    cell p_exit = emit_install_frame(A_BRK_RP, A_BRK_IP);
    asm_lit(0); asm_lit(A_ACC); asm_store();
    asm_lit(0); asm_lit(A_I); asm_store();
    cell lloop = asm_here();
    asm_call(f1);
    asm_lit(A_I); asm_fetch(); asm_lit(1); asm_add(); asm_lit(A_I); asm_store();
    asm_lit(A_I); asm_fetch(); asm_lit(10); asm_lt();
    cell jend = asm_zbranch_fwd();        /* I>=10 -> normal end */
    asm_branch(lloop);
    asm_patch_here(jend);
    asm_lit(999);                         /* normal path */
    cell bfin = asm_branch_fwd();
    asm_patch_here(p_exit);               /* break lands here */
    asm_lit(A_ACC); asm_fetch();
    asm_patch_here(bfin);
    asm_exit();

    cell main = asm_here();
    asm_call(loopfn);
    asm_halt();

    s1_reset();
    s1_run(main);
    CHECK(s1_top() == 5, "break from 4-deep nested call exits loop with ACC=5");
}

/* ADV 2: the same loop + a function that returns two ordinary values. */
static void adv2(void) {
    printf("adv 2: break loop + a function returning two ordinary values\n");

    cell two = asm_here();                /* ( -- I I*2 ) */
    asm_lit(A_I); asm_fetch(); asm_dup(); asm_lit(2); asm_mul();
    asm_exit();

    cell f3 = asm_here(); asm_call(two); asm_exit();
    cell f2 = asm_here(); asm_call(f3);  asm_exit();
    cell f1 = asm_here(); asm_call(f2);  asm_exit();

    cell guard = asm_here();              /* break if ACC>=8 */
    asm_lit(A_ACC); asm_fetch(); asm_lit(8); asm_ge();
    cell jg = asm_zbranch_fwd();
    emit_restore_rp_jump(A_BRK_RP, A_BRK_IP);
    asm_patch_here(jg);
    asm_exit();

    cell loopfn = asm_here();
    cell p_exit = emit_install_frame(A_BRK_RP, A_BRK_IP);
    asm_lit(0); asm_lit(A_ACC); asm_store();
    asm_lit(0); asm_lit(A_I); asm_store();
    cell lloop = asm_here();
    asm_call(f1);                         /* leaves I, I*2 */
    /* consume both: ACC += I + I*2 */
    asm_lit(A_TMP); asm_store();          /* tmp = I*2 */
    asm_lit(A_ACC); asm_fetch(); asm_add(); /* I + ACC */
    asm_lit(A_TMP); asm_fetch(); asm_add(); /* + I*2 */
    asm_lit(A_ACC); asm_store();
    asm_call(guard);
    asm_lit(A_I); asm_fetch(); asm_lit(1); asm_add(); asm_lit(A_I); asm_store();
    asm_lit(A_I); asm_fetch(); asm_lit(6); asm_lt();
    cell jend = asm_zbranch_fwd();
    asm_branch(lloop);
    asm_patch_here(jend);
    asm_lit(999);
    cell bfin = asm_branch_fwd();
    asm_patch_here(p_exit);
    asm_lit(A_ACC); asm_fetch();
    asm_patch_here(bfin);
    asm_exit();

    cell main = asm_here();
    asm_call(loopfn);
    asm_halt();

    s1_reset();
    s1_run(main);
    /* contribution per iteration = I + I*2 = 3I; break when ACC>=8, at I=2: ACC=9 */
    CHECK(s1_top() == 9, "break at ACC=9 (loop also carried two values)");
}

/* ADV 3: nested control structures; break targets the inner loop, a non-local
 * return targets the outer function. */
static void adv3(void) {
    printf("adv 3: break (inner loop) + non-local return (outer function)\n");

    cell do_brk = asm_here();
    emit_restore_rp_jump(A_BRK_RP, A_BRK_IP);
    cell do_ret = asm_here();
    emit_restore_rp_jump(A_RET_RP, A_RET_IP);

    cell leaf = asm_here();               /* break when I==2 */
    asm_lit(A_I); asm_fetch(); asm_lit(2); asm_eq();
    cell jl = asm_zbranch_fwd();
    asm_call(do_brk);
    asm_patch_here(jl);
    asm_exit();

    cell trigger = asm_here();            /* non-local return from outer */
    asm_call(do_ret);
    asm_exit();

    cell inner = asm_here();
    cell p_bexit = emit_install_frame(A_BRK_RP, A_BRK_IP);
    asm_lit(0); asm_lit(A_I); asm_store();
    cell iloop = asm_here();
    asm_lit(A_I); asm_fetch(); asm_lit(1); asm_add(); asm_lit(A_I); asm_store();
    asm_call(leaf);
    asm_lit(A_I); asm_fetch(); asm_lit(3); asm_lt();
    cell jend = asm_zbranch_fwd();        /* I>=3 -> exit loop */
    asm_branch(iloop);
    asm_patch_here(jend);
    asm_patch_here(p_bexit);              /* break lands here too */
    asm_exit();                           /* inner returns normally */

    cell outer = asm_here();
    cell p_rexit = emit_install_frame(A_RET_RP, A_RET_IP);
    asm_call(inner);
    asm_call(trigger);                    /* non-local return from outer */
    asm_lit(999);                         /* normal path (skipped) */
    cell bfin = asm_branch_fwd();
    asm_patch_here(p_rexit);              /* non-local return lands here */
    asm_lit(111);
    asm_patch_here(bfin);
    asm_exit();

    cell main = asm_here();
    asm_call(outer);
    asm_halt();

    s1_reset();
    s1_run(main);
    CHECK(s1_top() == 111, "break stopped at inner loop; return unwound outer -> 111");
}

/* ADV 4: exception crossing functions that are completely unaware of it. */
static void adv4(void) {
    printf("adv 4: exception crossing three unaware functions\n");

    cell leaf = asm_here();
    asm_lit(6); asm_lit(7); asm_add();    /* partial result 13, then throw */
    emit_do_throw(A_C_SP, A_C_RP, A_C_IP, 42);
    asm_exit();                           /* unreachable */

    /* ordinary functions: call the next, return. No knowledge of the throw. */
    cell f3 = asm_here(); asm_call(leaf); asm_exit();
    cell f2 = asm_here(); asm_call(f3);   asm_exit();
    cell f1 = asm_here(); asm_call(f2);   asm_exit();

    cell main = asm_here();
    cell p_cip = emit_install_catch(A_C_SP, A_C_RP, A_C_IP);
    asm_call(f1);
    asm_lit(0); asm_halt();               /* normal path (skipped) */
    asm_patch_here(p_cip);                /* handler: err already on stack */
    asm_halt();

    s1_reset();
    s1_run(main);
    CHECK(s1_top() == 42, "throw reached handler across f1/f2/f3 (partial 13 discarded)");
}

/* ADV 5: one function with four outcomes (value / no result / two values /
 * non-local transfer), with unaware intermediate functions, no per-case
 * propagation. */
static void adv5(void) {
    printf("adv 5: one function, four outcomes, unaware intermediaries\n");

    /* produce(sel): returns [count] or [v1 ... vn count] on the stack.
     *   sel=0 -> count 0 (no result)
     *   sel=1 -> value 42, count 1
     *   sel=2 -> values 10,20, count 2
     *   sel=3 -> break (never returns) */
    cell produce = asm_here();
    asm_lit(A_SEL); asm_fetch();
    asm_dup(); asm_lit(3); asm_eq();
    cell jb3 = asm_zbranch_fwd();
    asm_drop();
    emit_restore_rp_jump(A_BRK_RP, A_BRK_IP);   /* break */
    asm_patch_here(jb3);
    asm_dup(); asm_lit(0); asm_eq();
    cell j0 = asm_zbranch_fwd();
    asm_drop();
    asm_lit(0);                                   /* count 0 */
    cell b0 = asm_branch_fwd();
    asm_patch_here(j0);
    asm_dup(); asm_lit(1); asm_eq();
    cell j1 = asm_zbranch_fwd();
    asm_drop();
    asm_lit(42); asm_lit(1);                      /* value + count 1 */
    cell b1 = asm_branch_fwd();
    asm_patch_here(j1);
    asm_drop();
    asm_lit(20); asm_lit(10); asm_lit(2);         /* two values + count 2 */
    asm_patch_here(b0);
    asm_patch_here(b1);
    asm_exit();

    /* unaware pass-through functions */
    cell h = asm_here(); asm_call(produce); asm_exit();
    cell g = asm_here(); asm_call(h);      asm_exit();

    cell main = asm_here();
    cell p_bexit = emit_install_frame(A_BRK_RP, A_BRK_IP);
    asm_call(g);                              /* leaves [values.. count] */
    asm_lit(A_SHAPE); asm_store();            /* count -> A_SHAPE */
    /* shape==0 -> no values */
    asm_lit(A_SHAPE); asm_fetch(); asm_lit(0); asm_eq();
    cell jn0 = asm_zbranch_fwd();
    asm_lit(888); asm_lit(A_R1); asm_store();
    asm_lit(888); asm_lit(A_R2); asm_store();
    cell bdone = asm_branch_fwd();
    asm_patch_here(jn0);
    /* shape==1 -> one value */
    asm_lit(A_SHAPE); asm_fetch(); asm_lit(1); asm_eq();
    cell jn1 = asm_zbranch_fwd();
    asm_lit(A_R1); asm_store();
    asm_lit(777); asm_lit(A_R2); asm_store();
    cell bdone1 = asm_branch_fwd();
    asm_patch_here(jn1);
    /* shape==2 -> two values */
    asm_lit(A_R1); asm_store();
    asm_lit(A_R2); asm_store();
    asm_patch_here(bdone1);
    asm_patch_here(bdone);
    asm_halt();
    asm_patch_here(p_bexit);                  /* break lands here */
    asm_lit(444); asm_lit(A_R1); asm_store();
    asm_halt();

    cell exp_shapes[4] = {0, 1, 2, -1};
    cell exp_r1[4]     = {888, 42, 10, 444};
    cell exp_r2[4]     = {888, 777, 20, 0};
    for (cell s = 0; s < 4; s++) {
        s1_reset();
        s1_set_mem(A_SHAPE, -1);
        s1_set_mem(A_R1, 0);
        s1_set_mem(A_R2, 0);
        s1_set_mem(A_SEL, s);
        s1_run(main);
        int ok = s1_mem(A_SHAPE) == exp_shapes[s]
              && s1_mem(A_R1) == exp_r1[s]
              && s1_mem(A_R2) == exp_r2[s];
        char buf[64];
        snprintf(buf, sizeof buf,
                 "sel=%ld -> shape=%ld r1=%ld r2=%ld",
                 (long)s, (long)s1_mem(A_SHAPE), (long)s1_mem(A_R1), (long)s1_mem(A_R2));
        CHECK(ok, buf);
    }
}

/* ADV 6: two independent mechanisms (CATCH/THROW + break) active at once,
 * routing to different enclosing scopes. */
static void adv6(void) {
    printf("adv 6: CATCH/THROW and break active simultaneously\n");

    /* leaf: sel==1 -> throw 777 (to catch), else -> break (to loop) */
    cell leaf = asm_here();
    asm_lit(A_SEL); asm_fetch(); asm_lit(1); asm_eq();
    cell jth = asm_zbranch_fwd();
    emit_do_throw(A_C_SP, A_C_RP, A_C_IP, 777);
    asm_patch_here(jth);
    emit_restore_rp_jump(A_BRK_RP, A_BRK_IP);   /* break */
    asm_exit();                                 /* unreachable */

    cell main = asm_here();
    cell p_cip = emit_install_catch(A_C_SP, A_C_RP, A_C_IP);
    cell p_bexit = emit_install_frame(A_BRK_RP, A_BRK_IP);
    asm_lit(0); asm_lit(A_I); asm_store();
    cell loop = asm_here();
    asm_lit(A_I); asm_fetch(); asm_lit(1); asm_add(); asm_lit(A_I); asm_store();
    asm_call(leaf);
    asm_lit(A_I); asm_fetch(); asm_lit(5); asm_lt();
    cell jend = asm_zbranch_fwd();
    asm_branch(loop);
    asm_patch_here(jend);
    asm_lit(999); asm_halt();                 /* normal end (skipped) */
    asm_patch_here(p_bexit);                  /* break lands here */
    asm_lit(100); asm_halt();
    asm_patch_here(p_cip);                    /* throw handler */
    asm_halt();                               /* err already on stack */

    s1_reset();
    s1_set_mem(A_SEL, 0);
    s1_run(main);
    CHECK(s1_top() == 100, "break routed to the loop, not the catch");

    s1_reset();
    s1_set_mem(A_SEL, 1);
    s1_run(main);
    CHECK(s1_top() == 777, "throw routed to the catch, not the loop");
}

int run_adversarial_tests(void) {
    asm_reset();
    adv1();
    adv2();
    adv3();
    adv4();
    adv5();
    adv6();
    return failures;
}
