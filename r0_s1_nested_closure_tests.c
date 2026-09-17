/* r0_s1_nested_closure_tests.c - regression: a closure invocation nested inside
 * another closure's argument list must not corrupt the outer invocation's
 * argument count / loop state.
 *
 * BUG (fixed): emit_invoke_closure used the global RV_ARITY cell as its
 * argument-loop bound. A nested closure invocation (during argument evaluation)
 * overwrote RV_ARITY and restored it only after the outer loop, so the outer
 * argument loop could terminate early and shift arguments (the first argument
 * became 0 and the real argument values slid one position).
 *
 * These tests use ordinary closures/natives only: no datatypes, no genealogy,
 * no rendering, no M3 machinery. They FAIL against the unfixed evaluator.
 */

#include "r0_s1.h"
#include <stdio.h>

static int failures = 0;

static int run_src(const char *src, int *N) {
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) { *N = -1; return 0; }
    *N = r0_s1_run(b);
    return 0;
}

static void expectN(const char *src, int n, const cell *vals, const char *what) {
    int N; run_src(src, &N);
    int ok = (N == n);
    if (ok) for (int i = 0; i < n; i++) if (r0_s1_result(i, N) != vals[i]) ok = 0;
    if (ok) printf("  ok: %s\n", what);
    else {
        printf("  FAIL: %s (N=%d want %d; got", what, N, n);
        for (int i = 0; i < N && i < n; i++) printf(" %ld", (long)r0_s1_result(i, N));
        printf(")\n");
        failures++;
    }
}

int run_r0_s1_nested_closure_tests(void) {
    r0_s1_init();

    printf("R0-S1 regression: nested closure argument evaluation\n");

    /* A: an identity closure used directly as another closure's argument. */
    { cell v[2] = { mk_int(100), mk_int(115) };
      expectN("[ g: func [x] [ x ]  f: func [a b] [ values [a b] ]  f g 100 115 ]",
              2, v, "A: f (g 100) 115 -> [100 115]"); }

    /* B: a closure call nested inside a native, inside another closure. */
    { cell v[2] = { mk_int(130), mk_int(115) };
      expectN("[ g: func [x] [ x ]  f: func [a b] [ values [a b] ]  f + g 100 30 115 ]",
              2, v, "B: f (+ (g 100) 30) 115 -> [130 115]"); }

    /* C: two closure calls as the two arguments of a closure. */
    { cell v[2] = { mk_int(100), mk_int(200) };
      expectN("[ g: func [x] [ x ]  f: func [a b] [ values [a b] ]  f g 100 g 200 ]",
              2, v, "C: f (g 100) (g 200) -> [100 200]"); }

    /* D: a three-argument closure with a nested closure call in the middle. */
    { cell v[3] = { mk_int(7), mk_int(42), mk_int(9) };
      expectN("[ g: func [x] [ x ]  f: func [a b c] [ values [a b c] ]  f 7 g 42 9 ]",
              3, v, "D: f 7 (g 42) 9 -> [7 42 9]"); }

    /* E: nesting does not corrupt the outer call's return depth (RP balanced). */
    { int N;
      run_src("[ g: func [x] [ x ]  f: func [a b] [ values [a b] ]  f + g 100 30 115 ]", &N);
      int ok = (N == 2 && r0_s1_result(0, 2) == mk_int(130) && r0_s1_result(1, 2) == mk_int(115));
      if (ok) printf("  ok: E: nested arg eval leaves RP balanced\n");
      else { printf("  FAIL: E (N=%d)\n", N); failures++; } }

    if (failures == 0) printf("all nested-closure regression tests passed\n");
    return failures;
}
