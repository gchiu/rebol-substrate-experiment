/* r0_s1_p6b_bench.c - FIB-OPT-P6B: compiler-optimisation control experiment.
 *
 * A standalone timing driver (not part of the frozen test suite). It runs the
 * exact naive recursive Fibonacci benchmark used by P5/P6 and reports:
 *   - the result of fib 10/15/20/25 (correctness check: 55/610/6765/75025);
 *   - wall-clock timing (monotonic, evaluation only) for fib 20 and fib 25,
 *     with a warmup and 7 timed repetitions each, printing every raw time.
 *
 * Built once per optimisation level with identical flags except -O:
 *
 *     cc -std=c17 -Wall -Wextra -O<level> -o fib-p6b-bench \
 *         r0_s1_p6b_bench.c r0_s1_runtime.c s1.c
 *
 * This is the NON-counting runtime (no -DR0_S1_PROFILE). The frozen s1.c is
 * untouched. This file never modifies the frozen substrate (s1.c/s1.h/
 * tests.c/adversarial.c/claims.c).
 */
#define _POSIX_C_SOURCE 199309L
#include "r0_s1.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* The benchmark function definition (no leading '['), exactly as P5/P6. */
static const char *FIB_DEF =
    "fib: func [n] [ either <= n 1 [ n ] [ + fib - n 1 fib - n 2 ] ]";

static char prog_buf[4096];
static const char *build_full(int n) {
    snprintf(prog_buf, sizeof prog_buf, "[ %s fib %d ]", FIB_DEF, n);
    return prog_buf;
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* Verify fib 10/15/20/25 == 55/610/6765/75025 (one run each). */
static void verify(void) {
    static const int ns[] = { 10, 15, 20, 25 };
    static const long expect[] = { 55, 610, 6765, 75025 };
    printf("results:");
    for (int i = 0; i < 4; i++) {
        int err = 0;
        cell b = r0_s1_parse(build_full(ns[i]), &err);
        if (err) { printf(" fib%d=PARSE-ERR", ns[i]); continue; }
        int N = r0_s1_run(b);
        long got = (N == 1) ? (long)int_val(r0_s1_result(0, 1)) : -1;
        printf(" fib%d=%ld%s", ns[i], got, got == expect[i] ? "(ok)" : "(WRONG)");
    }
    printf("\n");
}

/* Warmup + `reps` timed runs; print every raw time, median, min, max. */
static void bench(int n, int reps) {
    int err = 0;
    cell b = r0_s1_parse(build_full(n), &err);
    if (err) { printf("fib %d parse error\n", n); return; }

    r0_s1_run(b); /* warm up */

    double ts[64];
    int got = 0;
    for (int r = 0; r < reps; r++) {
        double t0 = now_sec();
        r0_s1_run(b);
        double t1 = now_sec();
        ts[got++] = t1 - t0;
    }
    double s[64];
    for (int i = 0; i < got; i++) s[i] = ts[i];
    for (int i = 0; i < got; i++)
        for (int j = i + 1; j < got; j++)
            if (s[j] < s[i]) { double t = s[i]; s[i] = s[j]; s[j] = t; }

    printf("fib %2d raw:", n);
    for (int i = 0; i < got; i++) printf(" %.6f", ts[i]);
    printf("\n");
    printf("fib %2d median %.6f s  (min %.6f, max %.6f)\n",
           n, s[got / 2], s[0], s[got - 1]);
}

int main(void) {
    printf("FIB-OPT-P6B: compiler-optimisation control (non-counting runtime)\n");
    r0_s1_init();
    verify();
    bench(20, 7);
    bench(25, 7);
    return 0;
}
