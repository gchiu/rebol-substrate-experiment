/* r0_s1_invoke_tests.c - dynamic closure invocation.
 *
 * `invoke` is the Glon analogue of Forth EXECUTE: it evaluates ONE expression
 * to obtain a closure VALUE, then hands control to that closure, which consumes
 * its own arity arguments from the SAME caller expression stream -- exactly as
 * if the closure had been reached through an ordinary word. This completes the
 * first-class closure model (create / store / retrieve / invoke).
 *
 * The closure-producing expression is normally a GET-word (`:a`), a factory
 * call, or `block-at`/`select` -- i.e. something that yields a T_CLOSURE value
 * rather than a word that auto-invokes.
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

static void eval(const char *prog) {
    strcpy(src, prog);
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) { N = -1; return; }
    N = r0_s1_run_persistent(b);
}

static int got(int i) { return (i < N) ? (int)int_val(r0_s1_result(i, N)) : 0; }

int run_r0_s1_invoke_tests(void) {
    r0_s1_init();

    printf("R0-S1 dynamic closure invocation (invoke)\n");

    /* shared vocabulary: lambda/does factory, loader-block primitives, a
     * block-set! raw for building a closure table, and a collect raw. */
    {
        char lib[1024];
        snprintf(lib, sizeof lib,
            "[ lambda: func [spec body] [ func :spec :body ] "
            "  does: func [body] [ lambda [] body ] "
            "  block-len: masm 1 [ DUP LIT 16 MOD SUB @ LIT 16 MUL ARITY 1 EXIT ] "
            "  block-at: masm 2 [ LIT SCRATCH_B ! DUP LIT 16 MOD SUB LIT 2 ADD LIT SCRATCH_B @ LIT 16 DIV ADD @ ARITY 1 EXIT ] "
            "  bset: masm 3 [ LIT SCRATCH_C ! LIT SCRATCH_B ! DUP LIT 16 MOD SUB LIT SCRATCH_A ! LIT SCRATCH_C @ LIT SCRATCH_A @ LIT 2 ADD LIT SCRATCH_B @ LIT 16 DIV ADD ! ARITY 0 EXIT ] "
            "  collect: raw [ CALL %ld ARITY 0 EXIT ] ]",
            (long)r0_s1_gc_collect_addr());
        eval(lib);
        CHECK(N == 1 && r0_s1_ran_cleanly(), "library loads cleanly");
    }

    /* 1,2,3: zero/one/two-argument closure reached as a VALUE via GET-word. */
    eval("[ a: does [ 42 ]  invoke :a ]");
    CHECK(N == 1 && got(0) == 42 && r0_s1_ran_cleanly(), "1: zero-arg closure value (42)");

    eval("[ base: 5  f: lambda [n] [ + base n ]  invoke :f 7 ]");
    CHECK(N == 1 && got(0) == 12 && r0_s1_ran_cleanly(), "2: one-arg closure value (5+7=12)");

    eval("[ f: lambda [a b] [ + a b ]  invoke :f 10 20 ]");
    CHECK(N == 1 && got(0) == 30 && r0_s1_ran_cleanly(), "3: two-arg closure value (10+20=30)");

    /* 4: producer expression evaluated exactly once. */
    eval("[ a: does [ 42 ]  cnt: 0  pick: func [] [ cnt: + cnt 1  :a ]  r: invoke pick  values [ r cnt ] ]");
    CHECK(N == 2 && got(0) == 42 && got(1) == 1, "4: producer expression evaluated once (cnt=1)");

    /* 5,6: closure value retrieved from a table via block-at / select. */
    eval("[ t: [ 0 0 ]  a: does [ 42 ]  bset t 0 :a  invoke block-at t 0 ]");
    CHECK(N == 1 && got(0) == 42 && r0_s1_ran_cleanly(), "5: block-at produced closure value (42)");

    eval("[ row: [ 0 0 ]  a: does [ 42 ]  bset row 0 'home  bset row 1 :a  invoke block-at row 1 ]");
    CHECK(N == 1 && got(0) == 42 && r0_s1_ran_cleanly(), "6: select-row closure value (42)");

    /* 7,8: factory-returned closure; captured caller local. */
    eval("[ mk: func [b] [ lambda [n] [ + b n ] ]  invoke mk 100 7 ]");
    CHECK(N == 1 && got(0) == 107 && r0_s1_ran_cleanly(), "7: factory closure, captured local (100+7=107)");

    /* 9: escaped captured context. */
    eval("[ mk: func [b] [ does [ b ] ]  a: mk 42  invoke :a ]");
    CHECK(N == 1 && got(0) == 42 && r0_s1_ran_cleanly(), "9: escaped captured context (42)");

    /* 10: same-site recursion preserves the captured activation (CLOSURE_CTX,
     * not the current activation / BLK_SITE). */
    eval("[ f: func [x carried] [ either = x 99 [ invoke :carried ] [ a: does [ x ]  f 99 :a ] ]  f 7 none ]");
    CHECK(N == 1 && got(0) == 7 && r0_s1_ran_cleanly(), "10: same-site recursion preserves captured activation (7)");

    /* 11: nested invoke. */
    eval("[ a: does [ 42 ]  b: does [ :a ]  invoke b ]");
    CHECK(N == 1 && got(0) == 42 && r0_s1_ran_cleanly(), "11: nested invoke (42)");

    /* 12: expression immediately after invoke stays aligned (not swallowed). */
    eval("[ a: does [ 1 ]  values [ invoke :a  2  3 ] ]");
    CHECK(N == 3 && got(0) == 1 && got(1) == 2 && got(2) == 3,
          "12: following expressions stay aligned (1 2 3)");

    /* 14: forced GC during invoke. */
    eval("[ a: does [ 42 ]  collect  invoke :a ]");
    CHECK(N == 1 && got(0) == 42 && r0_s1_ran_cleanly(), "14: forced GC during invoke (42)");

    /* 13: non-closure invocation fails safely (fail-stop, not a value). */
    {
        eval("[ invoke 42 ]");
        CHECK(!r0_s1_ran_cleanly(), "13: non-closure invocation fails safely");
    }

    if (failures == 0) printf("all invoke tests passed\n");
    return failures;
}
