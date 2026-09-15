/* r0_tests.c - automated tests for R0, mapping to the architecture acceptance
 * tests A..M plus additional implementation tests. Built above frozen S1. */
#include "r0.h"
#include <stdio.h>

static int failures = 0;
static cell g_ctx;

#define CHECK(c, m)                                                   \
    do { if (c) { printf("    ok: %s\n", m); }                        \
         else    { printf("    FAIL: %s\n", m); failures++; } } while (0)

/* parse + eval a program in the current global context; returns arity (-1 on
 * error / uncaught transfer). */
static int ev(const char *src) {
    int err = 0;
    cell b = r0_parse(src, &err);
    if (err) return -1;
    return r0_eval(b, g_ctx);
}

/* run a program, expect arity n and exactly the values vals[] */
static void expect(const char *src, int n, const cell *vals, const char *what) {
    int N = ev(src);
    if (N < 0) { CHECK(0, what); printf("        (eval failed)\n"); return; }
    if (N != n) { CHECK(0, what); printf("        (arity %d != %d)\n", N, n); return; }
    int ok = 1;
    for (int i = 0; i < n; i++) if (r0_result(i) != vals[i]) ok = 0;
    CHECK(ok, what);
    if (!ok) { printf("        got:"); for (int i = 0; i < n; i++) printf(" %ld", (long)r0_result(i)); printf("\n"); }
}

static void expect1(const char *src, cell v, const char *what) {
    cell vals[1]; vals[0] = v; expect(src, 1, vals, what);
}

/* --- A..M acceptance tests -------------------------------------------- */

static void test_A_no_result(void) {
    printf("A. no ordinary result\n");
    int N = ev("[ print 1 ]");
    CHECK(N == 0, "print yields arity 0");
}

static void test_B_none(void) {
    printf("B. one NONE result\n");
    expect1("[ none ]", R0_NONE, "literal none -> NONE");
    expect1("[ f: func [] []  f ]", R0_NONE, "empty body -> NONE");
}

static void test_C_values(void) {
    printf("C. multiple results via values\n");
    cell vals[2] = { mk_int(10), mk_int(20) };
    expect("[ values [10 20] ]", 2, vals, "values [10 20] -> two results");
}

static void test_D_block(void) {
    printf("D. one literal block\n");
    int N = ev("[ [10 20] ]");
    CHECK(N == 1 && r0_tag(r0_result(0)) == TAG_BLOCK, "block value (tag BLOCK)");
}

static void test_E_break_crossing(void) {
    printf("E. BREAK crossing unaware intermediate functions\n");
    expect1("[ i: 0  h: func [] [ i: + i 1  break ]  g: func [] [ h ]  loop 100 [ g ]  i ]",
            mk_int(1), "break from h through g exits the loop (i == 1)");
}

static void test_F_return_crossing(void) {
    printf("F. definitional RETURN crossing an unaware helper\n");
    cell vals[2] = { mk_int(10), mk_int(20) };
    expect("[ f: func [] [ run: func [body] [ do body ]  run [ return values [10 20] ] ]  f ]",
           2, vals, "return through `run` targets f, two results");
}

static void test_G_multi_return_survives(void) {
    printf("G. multiple results surviving non-local RETURN\n");
    cell vals[3] = { mk_int(7), mk_int(8), mk_int(9) };
    expect("[ f: func [] [ run: func [body] [ do body ]  run [ return values [7 8 9] ] ]  f ]",
           3, vals, "return values [7 8 9] survives the unwind");
}

static void test_H_nested_loop_break(void) {
    printf("H. nested loops: BREAK selects only the inner loop\n");
    cell vals[2] = { mk_int(2), mk_int(5) };
    expect("[ x: 0  loop 2 [ y: 0  loop 100 [ if >= y 5 [ break ]  y: + y 1 ]  x: + x 1 ]  values [x y] ]",
           2, vals, "outer runs twice (x=2), inner breaks at y=5");
}

static void test_I_return_in_loop(void) {
    printf("I. RETURN inside inner loop targets enclosing function\n");
    expect1("[ f: func [] [ i: 0  loop 100 [ i: + i 1  if >= i 1 [ return 42 ] ]  0 ]  f ]",
            mk_int(42), "return 42 from inside the loop (not 0)");
}

static void test_J_throw_crossing(void) {
    printf("J. THROW crosses functions unaware of THROW\n");
    expect1("[ h: func [] [ throw 99 ]  g: func [] [ h ]  catch [ g ] ]",
            mk_int(99), "throw 99 reaches catch through g");
}

static void test_K_user_control(void) {
    printf("K. user-defined control abstraction (while)\n");
    expect1("[ while: func [cond body] [ if do cond [ do body  while cond body ] ]  "
            "i: 0  while [< i 3] [ i: + i 1 ]  i ]",
            mk_int(3), "user-defined while counts i to 3");
}

static void test_L_dialect(void) {
    printf("L. tiny dialect (sum-list folds a block as data)\n");
    expect1("[ sum-list: func [data] [ acc: 0  i: 1  loop len data [ acc: + acc pick data i  i: + i 1 ]  acc ]  "
            "sum-list [1 2 3 4 5] ]",
            mk_int(15), "sum-list [1 2 3 4 5] -> 15 (not 5)");
}

static void test_M_trapdoor(void) {
    printf("M. RAW trapdoor: generator (make-gen / next) suspends and resumes\n");
    cell vals[4] = { mk_int(1), mk_int(2), mk_int(3), R0_NONE };
    expect("[ g: make-gen  a: next g  b: next g  c: next g  d: next g  values [a b c d] ]",
           4, vals, "generator yields 1,2,3 then NONE");
}

/* --- additional implementation tests ----------------------------------- */

static void test_recursion(void) {
    printf("recursion: factorial\n");
    expect1("[ fact: func [n] [ if <= n 1 [ return 1 ]  * n fact - n 1 ]  fact 5 ]",
            mk_int(120), "fact 5 == 120");
}

static void test_nested_closures(void) {
    printf("nested closures\n");
    expect1("[ outer: func [x] [ func [y] [ + x y ] ]  add3: outer 3  add3 4 ]",
            mk_int(7), "outer 3 applied to 4 == 7");
}

static void test_make_counter(void) {
    printf("captured binding mutation (make-counter)\n");
    cell vals[2] = { mk_int(15), mk_int(16) };
    expect("[ make-counter: func [start] [ func [delta] [ start: + start delta  start ] ]  "
            "c: make-counter 10  a: c 5  b: c 1  values [a b] ]",
           2, vals, "c 5 == 15 then c 1 == 16");
}

static void test_zero_arg(void) {
    printf("zero-argument function\n");
    expect1("[ f: func [] [ 42 ]  f ]", mk_int(42), "zero-arg f == 42");
}

static void test_arg_cleanup(void) {
    printf("argument cleanup after normal calls\n");
    int N = ev("[ + 1 2 ]");
    CHECK(N == 1 && r0_ds_depth() == 2, "stack depth after [+ 1 2] is exactly result set");
    N = ev("[ f: func [a b] [ a ]  f 1 2 ]");
    CHECK(N == 1 && r0_ds_depth() == 2, "no leftover args after f 1 2");
}

static void test_return_cleanup(void) {
    printf("argument/temporary cleanup after non-local RETURN\n");
    int N = ev("[ f: func [] [ run: func [body] [ do body ]  run [ return values [1 2] ] ]  f ]");
    CHECK(N == 2 && r0_ds_depth() == 3, "depth == result set only after non-local return");
}

static void test_no_stale_frames(void) {
    printf("repeated calls leave no stale SP/RP/control frames\n");
    ev("[ f: func [n] [ if <= n 0 [ 0 ] [ + n f - n 1 ] ]  f 4 ]");
    CHECK(r0_cs_depth() == 0, "control stack empty after recursion");
    int before = r0_cs_max();
    for (int i = 0; i < 5; i++) ev("[ f: func [] [ 7 ]  f ]");
    CHECK(r0_cs_max() == before, "csp_max does not grow on repeated calls");
}

static void test_missing_targets(void) {
    printf("missing BREAK/RETURN/THROW targets -> controlled errors\n");
    int N = ev("[ break ]");
    CHECK(N == -1, "break outside loop is an error");
    N = ev("[ throw 1 ]");
    CHECK(N == -1, "throw without catch is an error");
    int err = 0;
    r0_parse("[ return 1 ]", &err);
    CHECK(err == 2, "return outside function is a parse error");
}

static void test_native_boundary(void) {
    printf("NATIVE/HOST result boundary\n");
    int N = ev("[ print 42 ]");
    CHECK(N == 0, "print produces arity 0 through the trampoline");
    N = ev("[ + 2 3 ]");
    CHECK(N == 1 && r0_tag(r0_result(0)) == TAG_INT && r0_result(0) == mk_int(5),
          "add produces exactly one tagged INT result");
}

static void test_values_empty(void) {
    printf("VALUES [] -> zero results\n");
    int N = ev("[ values [] ]");
    CHECK(N == 0, "values [] yields arity 0");
}

static void test_return_no_arg(void) {
    printf("return with explicit none\n");
    expect1("[ f: func [] [ return none ]  f ]", R0_NONE, "return none -> NONE");
}

int run_r0_tests(void) {
    g_ctx = r0_init();

    test_A_no_result();
    test_B_none();
    test_C_values();
    test_D_block();
    test_E_break_crossing();
    test_F_return_crossing();
    test_G_multi_return_survives();
    test_H_nested_loop_break();
    test_I_return_in_loop();
    test_J_throw_crossing();
    test_K_user_control();
    test_L_dialect();
    test_M_trapdoor();

    test_recursion();
    test_nested_closures();
    test_make_counter();
    test_zero_arg();
    test_arg_cleanup();
    test_return_cleanup();
    test_no_stale_frames();
    test_missing_targets();
    test_native_boundary();
    test_values_empty();
    test_return_no_arg();

    printf("R0 heap used: %ld cells; data-stack max %d; control-stack max %d; symbols %d\n",
           r0_heap_used(), r0_ds_max(), r0_cs_max(), r0_symbol_count());
    return failures;
}
