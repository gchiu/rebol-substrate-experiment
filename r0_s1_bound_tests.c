/* r0_s1_bound_tests.c - the Alpha bound-block safety law.
 *
 * A bare block with T_BOUND (lexical) references is valid only while executing
 * under an activation whose FRAME_SITE matches the block's BLK_SITE. A
 * travelling bound block (passed into another function, or do'd after its
 * creator returned) would otherwise silently resolve T_BOUND against an
 * unrelated activation, so it must fail-stop. Closures (which capture the real
 * context identity in CLOSURE_CTX) are the portable form and are unaffected.
 */

#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
static char src[65536];
static int N;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static void strip_comments(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
}

static void eval(const char *prog) {
    strcpy(src, prog);
    int err = 0;
    cell save_lhp = M[GC_LOADER_HP];
    cell b = r0_s1_parse(src, &err);
    if (err) { M[GC_LOADER_HP] = save_lhp; N = -1; return; }
    N = r0_s1_run_persistent(b);
    M[GC_LOADER_HP] = save_lhp;
}

static int got(int i) { return (i < N) ? (int)int_val(r0_s1_result(i, N)) : 0; }

int run_r0_s1_bound_tests(void) {
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    M[M1_CURSOR] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    printf("R0-S1 bound-block safety (bare blocks are not portable)\n");

    {
        FILE *f = fopen("demo/shop/bootstrap.glon", "rb");
        size_t n = fread(src, 1, sizeof src - 1, f); fclose(f); src[n] = 0;
        strip_comments(src);
        int err = 0; cell b = r0_s1_parse(src, &err);
        if (!err) r0_s1_run_persistent(b);
    }

    /* 1: inline do [x] within the owning activation succeeds. */
    eval("[ f: func [x] [ do [x] ]  f 7 ]");
    CHECK(N == 1 && got(0) == 7 && r0_s1_ran_cleanly(), "1: inline do [x] within owning activation");

    /* ordinary function-body T_BOUND execution (implicit). */
    eval("[ f: func [x] [ + x 1 ]  f 41 ]");
    CHECK(N == 1 && got(0) == 42 && r0_s1_ran_cleanly(), "2: ordinary function-body T_BOUND");

    /* 3/4/5: closure capture + escape + invoke remain correct. */
    eval("[ g: func [x] [ does [x] ]  h: g 7  h ]");
    CHECK(N == 1 && got(0) == 7 && r0_s1_ran_cleanly(), "3: escaped closure after creator return");

    eval("[ g: func [x] [ does [x] ]  h: g 7  invoke :h ]");
    CHECK(N == 1 && got(0) == 7 && r0_s1_ran_cleanly(), "5: invoke of captured closure");

    /* 6: cross-function do of a bound block fails loudly. */
    eval("[ exec: func [blk] [ do blk ]  f: func [x] [ exec [x] ]  f 7 ]");
    CHECK(!r0_s1_ran_cleanly(), "6: cross-function do of bound block fails loudly");

    /* 8: returned bare bound block after dead origin fails loudly. */
    eval("[ mk: func [x] [ [x] ]  b: mk 7  do b ]");
    CHECK(!r0_s1_ran_cleanly(), "8: returned bare bound block after dead origin fails");

    /* 9: global block do remains valid. */
    eval("[ do [1 2 3] ]");
    CHECK(N == 1 && got(0) == 3 && r0_s1_ran_cleanly(), "9: global block do remains valid");

    /* 10: bare block with no T_BOUND remains valid. */
    eval("[ do [ + 1 2 ] ]");
    CHECK(N == 1 && got(0) == 3 && r0_s1_ran_cleanly(), "10: bare block with no T_BOUND remains valid");

    /* 11: a nested bound block is inert data while the outer block is do'd. */
    eval("[ f: func [x] [ do [ [x] ] ]  f 7 ]");
    CHECK(N == 1 && r0_s1_ran_cleanly(), "11: nested bound block is inert as data");

    /* 12: executing that nested bound block under an unrelated function fails. */
    eval("[ exec: func [blk] [ do blk ]  f: func [x] [ exec block-at [ [x] ] 0 ]  f 7 ]");
    CHECK(!r0_s1_ran_cleanly(), "12: executing the nested bound block cross-function fails");

    /* ---- documented same-site recursion behaviour -------------------------
     * Two live activations of ONE site share FRAME_SITE, so a travelling bare
     * bound block resolves against the INNERMOST matching activation. This is
     * the Alpha-documented limitation, not a bug; pin it so it cannot drift. */
    eval("[ f: func [x depth carried] [ "
         "    either = depth 0 [ b: [x]  f 99 1 b ] [ do carried ] ] "
         "  f 7 0 none ]");
    CHECK(N == 1 && got(0) == 99 && r0_s1_ran_cleanly(),
          "13: same-site recursion resolves bare bound block against innermost (99)");

    /* The closure control: the OUTER activation captures x=7; the inner same-site
     * activation x=99 invokes it, and the closure still sees 7. */
    eval("[ does: func [body] [ func [] :body ] "
         "  f: func [x depth carried] [ "
         "    either = depth 0 [ b: does [x]  f 99 1 b ] [ carried ] ] "
         "  f 7 0 none ]");
    CHECK(N == 1 && got(0) == 7 && r0_s1_ran_cleanly(),
          "14: closure control captures the outer activation (7) across same-site recursion");

    /* ---- return-stack invocation boundary ---------------------------------
     * One invocation reserves at most 96 cells (context 64 + frame 16 + mkctx
     * call 1 + alignment padding <=15); the guard fail-stops cleanly rather than
     * corrupting. countdown recurses ~96 cells/level, so depth 83 is the deepest
     * clean level and 84 must fail-stop with the stack sentry NOT firing. */
    eval("[ countdown: func [n] [ either < n 0 [ n ] [ countdown - n 1 ] ]  countdown 83 ]");
    CHECK(r0_s1_ran_cleanly() && !r0_s1_stack_sentry_fired() &&
          r0_s1_rp_min() >= R0S1_DS_INIT,
          "15: deepest clean recursion (83) returns correctly, RP stays >= DS_INIT");
    eval("[ countdown: func [n] [ either < n 0 [ n ] [ countdown - n 1 ] ]  countdown 84 ]");
    CHECK(!r0_s1_ran_cleanly() && !r0_s1_stack_sentry_fired(),
          "16: one level deeper (84) fail-stops at the guard, not via the sentry");

    if (failures == 0) printf("all bound-block safety tests passed\n");
    return failures;
}
