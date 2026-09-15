/* tests.c - control-structure demonstrations over the FROZEN S1 primitives.
 *
 * Each test builds a substrate program using only the primitive instruction
 * set (plus the derived-word macros from s1.h) and verifies its behaviour.
 * No test modifies the VM instruction set.
 */
#include "s1.h"
#include <stdio.h>

static int failures = 0;

#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (cond) { printf("    ok: %s\n", msg); }                    \
        else      { printf("    FAIL: %s\n", msg); failures++; }      \
    } while (0)

/* host-visible variable cells (below CODE_BASE; the high level would
 * normally allocate these from the heap). */
enum {
    V_ACC = 16, V_I = 17, V_SUM = 18, V_COUNT = 19,
    V_SAVED_RP = 20, V_SAVED_IP = 21,
    V_S_SP = 22, V_S_RP = 23, V_S_IP = 24, V_TEMP = 25,
    V_MAIN_SP = 26, V_MAIN_RP = 27, V_MAIN_IP = 28,
    V_GEN_SP = 29, V_GEN_RP = 30, V_GEN_IP = 31,
    V_YIELD = 32, V_OUT1 = 33, V_OUT2 = 34, V_OUT3 = 35, V_OUT4 = 36
};

/* generator stack regions (separate from the main stacks) */
enum { GENDS_TOP = 0xC000, GENRS_TOP = 0xF000 };

/* --- shared helpers ------------------------------------------------------ */

/* symmetric coroutine switch: save one side's {SP,RP,IP}, restore the other's.
 * Built entirely from @/! over the memory-mapped registers. */
static cell emit_switch_to_gen(void) {
    asm_lit(REG_SP); asm_fetch(); asm_lit(V_MAIN_SP); asm_store();
    asm_lit(REG_RP); asm_fetch(); asm_lit(V_MAIN_RP); asm_store();
    cell p = asm_lit_fwd(); /* continuation placeholder */
    asm_lit(V_MAIN_IP); asm_store();
    asm_lit(V_GEN_SP); asm_fetch(); asm_lit(REG_SP); asm_store();
    asm_lit(V_GEN_RP); asm_fetch(); asm_lit(REG_RP); asm_store();
    asm_lit(V_GEN_IP); asm_fetch(); asm_lit(REG_IP); asm_store();
    return p;
}
static cell emit_switch_to_main(void) {
    asm_lit(REG_SP); asm_fetch(); asm_lit(V_GEN_SP); asm_store();
    asm_lit(REG_RP); asm_fetch(); asm_lit(V_GEN_RP); asm_store();
    cell p = asm_lit_fwd(); /* resume placeholder */
    asm_lit(V_GEN_IP); asm_store();
    asm_lit(V_MAIN_SP); asm_fetch(); asm_lit(REG_SP); asm_store();
    asm_lit(V_MAIN_RP); asm_fetch(); asm_lit(REG_RP); asm_store();
    asm_lit(V_MAIN_IP); asm_fetch(); asm_lit(REG_IP); asm_store();
    return p;
}

/* --- tests --------------------------------------------------------------- */

/* 1. ordinary call and return */
static void test1_call_return(void) {
    printf("test 1: ordinary call and return\n");
    cell sq = asm_here();                 /* square ( x -- x*x ) */
    asm_dup(); asm_mul(); asm_exit();

    cell main = asm_here();
    asm_lit(5); asm_call(sq); asm_halt();

    s1_reset();
    s1_run(main);
    CHECK(s1_top() == 25, "square(5) == 25");
}

/* 2. conditional */
static void test2_conditional(void) {
    printf("test 2: conditional (7 - 3 > 0 -> 100 else 200)\n");
    cell main = asm_here();
    asm_lit(7); asm_lit(3); asm_sub();     /* 4 (nonzero) */
    cell j = asm_zbranch_fwd();            /* if 0, goto else */
    asm_lit(100);
    cell b = asm_branch_fwd();             /* goto done */
    asm_patch_here(j);                     /* else: */
    asm_lit(200);
    asm_patch_here(b);                     /* done: */
    asm_halt();

    s1_reset();
    s1_run(main);
    CHECK(s1_top() == 100, "took the 'then' branch");
}

/* 3. loop */
static void test3_loop(void) {
    printf("test 3: loop (sum 1..5 == 15)\n");
    cell sum = asm_here();                 /* sum ( n -- total ) */
    asm_lit(0); asm_lit(V_ACC); asm_store();   /* ACC = 0 */
    cell loop = asm_here();
    asm_dup();                             /* n n */
    cell jdone = asm_zbranch_fwd();        /* if n == 0, done */
    asm_dup(); asm_lit(V_ACC); asm_fetch(); asm_add(); asm_lit(V_ACC); asm_store();
    asm_lit(1); asm_sub();                 /* n-- */
    asm_branch(loop);
    asm_patch_here(jdone);                 /* done: (0 on stack) */
    asm_drop();
    asm_lit(V_ACC); asm_fetch();
    asm_exit();

    cell main = asm_here();
    asm_lit(5); asm_call(sum); asm_halt();

    s1_reset();
    s1_run(main);
    CHECK(s1_top() == 15, "sum(1..5) == 15");
}

/* 4. static break and continue */
static void test4_break_continue(void) {
    printf("test 4: static break/continue (sum odds 1..10, break at 7 == 9)\n");
    cell main = asm_here();
    asm_lit(0); asm_lit(V_SUM); asm_store();   /* SUM = 0 */
    asm_lit(1); asm_lit(V_I); asm_store();     /* I = 1 */
    cell loop = asm_here();
    /* if I == 7 -> break (static forward branch to done) */
    asm_lit(V_I); asm_fetch(); asm_lit(7); asm_eq();
    cell jbreak = asm_zbranch_fwd();           /* if I != 7, skip break */
    cell bbreak = asm_branch_fwd();            /* break -> done */
    asm_patch_here(jbreak);
    /* if I even -> continue (skip the add) */
    asm_lit(V_I); asm_fetch(); asm_lit(2); asm_mod();
    cell jcont = asm_zbranch_fwd();            /* if even (0), skip to cont */
    /* odd: SUM += I */
    asm_lit(V_I); asm_fetch(); asm_lit(V_SUM); asm_fetch(); asm_add(); asm_lit(V_SUM); asm_store();
    asm_patch_here(jcont);                     /* cont: */
    asm_lit(V_I); asm_fetch(); asm_lit(1); asm_add(); asm_lit(V_I); asm_store(); /* I++ */
    asm_lit(V_I); asm_fetch(); asm_lit(10); asm_gt(); /* I > 10 ? (1 if yes) */
    asm_zbranch(loop);                         /* if I <= 10, loop again */
    asm_patch_here(bbreak);                    /* done: */
    asm_lit(V_SUM); asm_fetch();
    asm_halt();

    s1_reset();
    s1_run(main);
    CHECK(s1_top() == 9, "sum of odds < 7 (1+3+5) == 9");
}

/* 5. dynamic non-local exit through nested calls */
static void test5_nonlocal_exit(void) {
    printf("test 5: dynamic non-local exit through nested calls\n");

    /* break: restore RP to saved marker, jump to saved IP */
    cell brk = asm_here();
    asm_lit(V_SAVED_RP); asm_fetch(); asm_lit(REG_RP); asm_store();
    asm_lit(V_SAVED_IP); asm_fetch(); asm_lit(REG_IP); asm_store();

    /* decide: when COUNT reaches 1, break out of the whole loop */
    cell decide = asm_here();
    asm_lit(V_COUNT); asm_fetch(); asm_lit(1); asm_eq();
    cell j = asm_zbranch_fwd();
    asm_call(brk);
    asm_patch_here(j);
    asm_exit();

    /* body: CALLs decide (so the break happens two frames deep) */
    cell body = asm_here();
    asm_call(decide);
    asm_exit();

    cell main = asm_here();
    /* install handler: save RP and the non-local exit target */
    asm_lit(REG_RP); asm_fetch(); asm_lit(V_SAVED_RP); asm_store();
    cell p_ip = asm_lit_fwd(); asm_lit(V_SAVED_IP); asm_store(); /* patch later */
    asm_lit(3); asm_lit(V_COUNT); asm_store();    /* COUNT = 3 */
    cell loop = asm_here();
    asm_lit(V_COUNT); asm_fetch();
    cell jdone = asm_zbranch_fwd();               /* if 0 -> normal end */
    asm_lit(V_COUNT); asm_fetch(); asm_lit(1); asm_sub(); asm_lit(V_COUNT); asm_store();
    asm_call(body);
    asm_branch(loop);
    asm_patch_here(jdone);
    asm_lit(999);                                 /* normal path */
    cell bfin = asm_branch_fwd();
    asm_patch_here(p_ip);                         /* SAVED_IP = here (non-local exit) */
    asm_lit(111);
    asm_patch_here(bfin);
    asm_halt();

    s1_reset();
    s1_run(main);
    CHECK(s1_top() == 111, "non-local exit landed on 111 (not 999)");
}

/* 6. CATCH/THROW-style exceptions */
static void test6_catch_throw(void) {
    printf("test 6: CATCH/THROW exceptions\n");

    /* throw ( err -- ): save err, restore SP/RP, re-push err, then jump */
    cell thr = asm_here();
    asm_lit(V_TEMP); asm_store();                 /* M[TEMP] = err */
    asm_lit(V_S_SP); asm_fetch(); asm_lit(REG_SP); asm_store();
    asm_lit(V_S_RP); asm_fetch(); asm_lit(REG_RP); asm_store();
    asm_lit(V_TEMP); asm_fetch();                 /* push err on restored stack */
    asm_lit(V_S_IP); asm_fetch(); asm_lit(REG_IP); asm_store(); /* jump to handler */

    cell main = asm_here();
    /* install handler (catch) with handler continuation = 'handler' */
    asm_lit(REG_SP); asm_fetch(); asm_lit(V_S_SP); asm_store();
    asm_lit(REG_RP); asm_fetch(); asm_lit(V_S_RP); asm_store();
    cell p_ip = asm_lit_fwd(); asm_lit(V_S_IP); asm_store(); /* patch later */
    /* protected body: push junk, then throw 42 */
    asm_lit(7); asm_lit(8); asm_lit(9);            /* junk on stack */
    asm_lit(42); asm_call(thr);
    asm_lit(0); asm_halt();                        /* normal path (never) */
    asm_patch_here(p_ip);                          /* S_IP = here (handler) */
    asm_halt();

    s1_reset();
    s1_run(main);
    CHECK(s1_top() == 42, "caught error code 42");
}

/* 7. save-and-restore suspension */
static void test7_suspend(void) {
    printf("test 7: save-and-restore suspension\n");
    cell main = asm_here();
    asm_lit(10);                                   /* work in progress */
    asm_lit(REG_SP); asm_fetch(); asm_lit(V_S_SP); asm_store(); /* save SP */
    asm_lit(REG_RP); asm_fetch(); asm_lit(V_S_RP); asm_store(); /* save RP */
    cell p_ip = asm_lit_fwd(); asm_lit(V_S_IP); asm_store();   /* save IP */
    asm_halt();                                    /* suspend */
    cell resume = asm_here();
    asm_patch_here(p_ip);                          /* S_IP = resume */
    asm_lit(20);                                   /* resumed work */
    asm_halt();

    s1_reset();
    s1_run(main);
    CHECK(s1_top() == 10, "suspended with 10 on the stack");
    CHECK(s1_mem(V_S_IP) == resume, "saved the resume continuation");

    /* resume: restore SP/RP and jump to the saved IP */
    s1_set_mem(REG_SP, s1_mem(V_S_SP));
    s1_set_mem(REG_RP, s1_mem(V_S_RP));
    s1_run(s1_mem(V_S_IP));
    CHECK(s1_top() == 20, "resumed and pushed 20");
    CHECK(s1_mem(s1_mem(REG_SP) + 1) == 10, "previous value 10 preserved below");
}

/* 8. resumable generator (separate stack regions + pointer swap) */
static void test8_generator(void) {
    printf("test 8: resumable generator\n");

    /* gen: yields a running accumulator, keeping it on its own data stack */
    cell gen = asm_here();
    asm_lit(10);                                  /* accumulator = 10 */
    asm_dup(); asm_lit(V_YIELD); asm_store();     /* yield 10 */
    cell p1 = emit_switch_to_main(); asm_patch_here(p1);
    asm_lit(20); asm_add();                       /* accumulator = 30 */
    asm_dup(); asm_lit(V_YIELD); asm_store();     /* yield 30 */
    cell p2 = emit_switch_to_main(); asm_patch_here(p2);
    asm_lit(30); asm_add();                       /* accumulator = 60 */
    asm_dup(); asm_lit(V_YIELD); asm_store();     /* yield 60 */
    cell p3 = emit_switch_to_main(); asm_patch_here(p3);
    asm_drop();                                   /* discard accumulator */
    asm_lit(0); asm_lit(V_YIELD); asm_store();    /* yield 0 (done) */
    cell p4 = emit_switch_to_main(); asm_patch_here(p4);

    cell main = asm_here();
    /* initialise generator state (separate stack regions) */
    asm_lit(GENDS_TOP); asm_lit(V_GEN_SP); asm_store();
    asm_lit(GENRS_TOP); asm_lit(V_GEN_RP); asm_store();
    asm_lit(gen); asm_lit(V_GEN_IP); asm_store();
    /* first switch into the generator */
    cell s1 = emit_switch_to_gen();
    asm_patch_here(s1);
    asm_lit(V_YIELD); asm_fetch(); asm_lit(V_OUT1); asm_store();
    cell s2 = emit_switch_to_gen();
    asm_patch_here(s2);
    asm_lit(V_YIELD); asm_fetch(); asm_lit(V_OUT2); asm_store();
    cell s3 = emit_switch_to_gen();
    asm_patch_here(s3);
    asm_lit(V_YIELD); asm_fetch(); asm_lit(V_OUT3); asm_store();
    cell s4 = emit_switch_to_gen();
    asm_patch_here(s4);
    asm_lit(V_YIELD); asm_fetch(); asm_lit(V_OUT4); asm_store();
    asm_halt();

    s1_reset();
    s1_run(main);
    CHECK(s1_mem(V_OUT1) == 10, "first yield 10");
    CHECK(s1_mem(V_OUT2) == 30, "second yield 30 (state preserved)");
    CHECK(s1_mem(V_OUT3) == 60, "third yield 60 (state preserved)");
    CHECK(s1_mem(V_OUT4) == 0,  "final yield 0 (done)");
}

/* 9. a new control abstraction: 'times' (bounded repetition using the
 *     return stack as a counter) plus 'do_until' (post-test loop) */
static void test9_new_abstraction(void) {
    printf("test 9: new control abstractions (times, do_until)\n");

    /* times: repeat n times; n kept on the return stack */
    cell main = asm_here();
    asm_lit(0); asm_lit(V_ACC); asm_store();       /* ACC = 0 */
    asm_lit(3); asm_toR();                         /* counter = 3 on R */
    cell tloop = asm_here();
    asm_lit(V_ACC); asm_fetch(); asm_lit(1); asm_add(); asm_lit(V_ACC); asm_store();
    asm_fromR(); asm_lit(1); asm_sub(); asm_dup(); /* counter-- */
    cell jdone = asm_zbranch_fwd();                /* if 0, done */
    asm_toR(); asm_branch(tloop);
    asm_patch_here(jdone);
    asm_drop();                                    /* drop the 0 */
    /* do_until: body first, then test (ACC keeps growing until >= 5) */
    cell dloop = asm_here();
    asm_lit(V_ACC); asm_fetch(); asm_lit(1); asm_add(); asm_lit(V_ACC); asm_store();
    asm_lit(V_ACC); asm_fetch(); asm_lit(5); asm_lt(); /* ACC < 5 ? */
    cell jd = asm_zbranch_fwd();                   /* if not, exit */
    asm_branch(dloop);
    asm_patch_here(jd);
    asm_lit(V_ACC); asm_fetch();
    asm_halt();

    s1_reset();
    s1_run(main);
    /* times(3) gives ACC=3; do_until grows ACC to 5 */
    CHECK(s1_top() == 5, "times(3) + do_until -> 5");
}

int run_tests(void) {
    asm_reset();
    test1_call_return();
    test2_conditional();
    test3_loop();
    test4_break_continue();
    test5_nonlocal_exit();
    test6_catch_throw();
    test7_suspend();
    test8_generator();
    test9_new_abstraction();
    return failures;
}
