/* r0_s1_reduce_tests.c - REDUCE: evaluated-block construction.
 *
 * `reduce` is the Glon analogue of Rebol REDUCE: it evaluates each expression
 * in a source block left-to-right, reduces each to a single value, and collects
 * the values into a freshly allocated managed block [count, site_id, v...]. The
 * result is ordinary transportable data: block-at/bset/block-len operate on it
 * unchanged, and closures stored in it keep their captured ancestry (so a
 * factory can build a closure table with REDUCE, not bset surgery).
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

int run_r0_s1_reduce_tests(void) {
    r0_s1_init();

    printf("R0-S1 evaluated-block construction (reduce)\n");

    /* shared vocabulary: lambda/does factory, loader-block primitives, a
     * block-set! raw, and a collect raw (same library as the invoke tests). */
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

    /* 1: empty block reduces to an empty block. */
    eval("[ block-len reduce [] ]");
    CHECK(N == 1 && got(0) == 0 && r0_s1_ran_cleanly(), "1: empty source -> empty block");

    /* 2: simple self-evaluating values. */
    eval("[ block-len reduce [1 2 3] ]");
    CHECK(N == 1 && got(0) == 3 && r0_s1_ran_cleanly(), "2: simple values [1 2 3]");

    /* 3: multi-cell expressions; one reduced value per expression. */
    eval("[ values [ block-len reduce [1 + 2 3 4]  block-at reduce [1 + 2 3 4] 1 ] ]");
    CHECK(N == 2 && got(0) == 3 && got(1) == 5 && r0_s1_ran_cleanly(),
          "3: multi-cell expressions ([1 + 2 3 4] -> [1 5 4])");

    /* 4: literal words and nested blocks stay literal (not auto-executed). */
    eval("[ t: reduce [1 'home [1 2]]  values [ block-len t  block-at t 0  block-len block-at t 2 ] ]");
    CHECK(N == 3 && got(0) == 3 && got(1) == 1 && got(2) == 2 && r0_s1_ran_cleanly(),
          "4: literals stay literal (word + nested block are values)");
    eval("[ block-at reduce [1 'home [1 2]] 1 ]");
    CHECK(N == 1 && r0_tag(r0_s1_result(0, N)) == T_WORD && r0_s1_ran_cleanly(),
          "4a: quoted word stays a word value (not auto-executed)");

    /* 5: NONE is a storable value. */
    eval("[ values [ block-len reduce [none]  block-at reduce [none] 0 ] ]");
    CHECK(N == 2 && got(0) == 1 && r0_s1_ran_cleanly(),
          "5: NONE is a storable value");

    /* 6: a nested source block is a value, not auto-executed. */
    eval("[ block-len block-at reduce [ [1 2 3] ] 0 ]");
    CHECK(N == 1 && got(0) == 3 && r0_s1_ran_cleanly(),
          "6: nested source block reduces to a block value");

    /* 7: a closure expression stores the closure, not its result. */
    eval("[ t: reduce [ does [42] ]  invoke block-at t 0 ]");
    CHECK(N == 1 && got(0) == 42 && r0_s1_ran_cleanly(),
          "7: closure expression stores the closure (invoke -> 42)");

    /* 8: closure captures the caller's parameter. */
    eval("[ f: func [x] [ invoke block-at reduce [ does [x] ] 0 ]  f 7 ]");
    CHECK(N == 1 && got(0) == 7 && r0_s1_ran_cleanly(),
          "8: closure captures caller parameter (7)");

    /* 9: closure captures the caller's set-word local. */
    eval("[ g: func [] [ y: 50 invoke block-at reduce [ does [y] ] 0 ]  g ]");
    CHECK(N == 1 && got(0) == 50 && r0_s1_ran_cleanly(),
          "9: closure captures caller set-word local (50)");

    /* 10: escaped closure stored in a REDUCE result survives the factory return. */
    eval("[ f: func [x] [ reduce [ does [x] ] ]  t: f 42  invoke block-at t 0 ]");
    CHECK(N == 1 && got(0) == 42 && r0_s1_ran_cleanly(),
          "10: escaped closure survives creator return (42)");

    /* 11: block-at retrieves the correct tagged values in order. */
    eval("[ t: reduce [10 20 30]  values [ block-at t 0  block-at t 1  block-at t 2 ] ]");
    CHECK(N == 3 && got(0) == 10 && got(1) == 20 && got(2) == 30 && r0_s1_ran_cleanly(),
          "11: block-at retrieves the reduced values in order");

    /* 12: bset can mutate a REDUCE-created block in place. */
    eval("[ t: reduce [1 2 3]  bset t 1 99  block-at t 1 ]");
    CHECK(N == 1 && got(0) == 99 && r0_s1_ran_cleanly(),
          "12: bset mutates a REDUCE-created block");

    /* 13: nested REDUCE (a REDUCE result stored inside another). */
    eval("[ t: reduce [1 reduce [2 3] 4]  values [ block-len t  block-len block-at t 1 ] ]");
    CHECK(N == 2 && got(0) == 3 && got(1) == 2 && r0_s1_ran_cleanly(),
          "13: nested REDUCE (inner block is a value)");

    /* 14: expressions run left-to-right with side effects in order. */
    eval("[ c: 0  t: reduce [ c: + c 1  c: + c 10  c: + c 100 ] "
         "  values [ block-at t 0  block-at t 1  block-at t 2  c ] ]");
    CHECK(N == 4 && got(0) == 1 && got(1) == 11 && got(2) == 111 && got(3) == 111,
          "14: left-to-right order + side effects ([1 11 111])");

    /* 16: forced GC during construction (closures trigger the collector). */
    eval("[ collect block-len reduce [ does [1] does [2] does [3] ] ]");
    CHECK(N == 1 && got(0) == 3 && r0_s1_ran_cleanly(),
          "16: forced GC during construction (3 closures survive)");

    /* 17: GC after the result survives and stays intact. */
    eval("[ f: func [x] [ reduce [ does [x] ] ]  t: f 42  collect  invoke block-at t 0 ]");
    CHECK(N == 1 && got(0) == 42 && r0_s1_ran_cleanly(),
          "17: GC after result survives (closure still 42)");

    /* failure: reduce of a non-block fail-stops safely. */
    eval("[ reduce 42 ]");
    CHECK(!r0_s1_ran_cleanly(), "non-block input fail-stops safely");

    if (failures == 0) printf("all reduce tests passed\n");
    return failures;
}
