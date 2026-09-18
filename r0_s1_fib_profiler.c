/* r0_s1_fib_profiler.c - FIB-PROFILE-P1: profile naive recursive Fibonacci.
 *
 * A standalone driver (not part of the frozen test suite). It runs the
 * ordinary GLON/R0 naive Fibonacci and reports:
 *   PART A - a high-level event trace of fib 5 (machine-generated, one line
 *            per event, decoded in FIB-PROFILE-P1.md);
 *   PART B - architectural event counts for fib 10/15/20/25/(30);
 *   PART C - wall-clock timing (monotonic, evaluation only).
 *
 * The counters are emitted only under -DR0_S1_PROFILE (see r0_s1_runtime.c);
 * a baseline build prints zero counts and is used for PART C timing only.
 *
 * NOTE: the managed heap is reset (HP -> 32768) by every r0_s1_run, so a
 * closure does not survive across runs. Every measurement therefore defines
 * fib AND invokes it in a single run. The definition cost (one closure
 * allocation + one binding, zero native/closures/lookups) is a small constant
 * included in each run; the fib-activation count is unaffected.
 *
 * This file never modifies the frozen substrate (s1.c/s1.h/tests.c/
 * adversarial.c/claims.c); all instrumentation lives above it.
 */
#define _POSIX_C_SOURCE 199309L
#include "r0_s1.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* The benchmark function definition (no leading '['), exactly as specified. */
static const char *FIB_DEF =
    "fib: func [n] [ either <= n 1 [ n ] [ + fib - n 1 fib - n 2 ] ]";

static char prog_buf[4096];

/* build "[ <FIB_DEF> fib n ]" */
static const char *build_full(int n) {
    snprintf(prog_buf, sizeof prog_buf, "[ %s fib %d ]", FIB_DEF, n);
    return prog_buf;
}

#ifdef R0_S1_PROFILE
/* parse+run a program string; returns result arity (or -1 on parse error). */
static int run_prog(const char *prog) {
    int err = 0;
    cell b = r0_s1_parse(prog, &err);
    if (err) { fprintf(stderr, "parse error in: %s\n", prog); return -1; }
    return r0_s1_run(b);
}
#endif

#ifdef R0_S1_PROFILE
/* Fibonacci + call counts (closed-form, for independent validation). */
static long long fib_value(int n) {
    long long a = 0, b = 1;
    for (int i = 0; i < n; i++) { long long t = a + b; a = b; b = t; }
    return a;
}
static long long fib_calls(int n) { return 2 * fib_value(n + 1) - 1; }

/* ---- PART B: event counts ---------------------------------------------- */
static void profile_fib(int n) {
    static const long long expect[] = { 0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55,
        89, 144, 233, 377, 610, 987, 1597, 2584, 4181, 6765, 10946, 17711,
        28657, 46368, 75025, 121393, 196418, 317811, 514229, 832040 };

    M[GC_COLLECT_CNT] = 0;
    int N = run_prog(build_full(n));

    cell result = (N == 1) ? r0_s1_result(0, 1) : -1;
    long long want = (n <= 30) ? expect[n] : -1;
    int correct = (N == 1 && result == mk_int((cell)want));

    r0_s1_pf_stats s;
    r0_s1_pf_read(&s);

    printf("fib %2d: result=%ld (%s)  calls=%ld (%s)\n",
           n, (long)(N == 1 ? int_val(result) : -1), correct ? "ok" : "WRONG",
           (long)s.closure, s.closure == fib_calls(n) ? "ok" : "WRONG");
    printf("  closure-invoc=%ld  subexpr=%ld  blkeval-iters=%ld\n",
           (long)s.closure, (long)s.subexpr, (long)s.blkeval);
    printf("  lookups=%ld  slots-examined=%ld  parent-hops=%ld  (avg slots/lookup=%.2f)\n",
           (long)s.lookup, (long)s.lk_slots, (long)s.lk_parent,
           s.lookup ? (double)s.lk_slots / (double)s.lookup : 0.0);
    printf("  lex-direct=%ld (local=%ld parent=%ld)\n",
           (long)s.lex_direct, (long)s.lex_local, (long)s.lex_parent);
    printf("  hash-probes=%ld hits=%ld misses=%ld collisions=%ld fallback=%ld fallback-slots=%ld\n",
           (long)s.hash_probes, (long)s.hash_hits, (long)s.hash_misses,
           (long)s.hash_collisions, (long)s.hash_fallback, (long)s.hash_fallback_slots);
    printf("  natives=%ld (<=%ld -%ld +%ld either%ld other%ld)\n",
           (long)s.native, (long)s.nat_le, (long)s.nat_sub, (long)s.nat_add,
           (long)s.nat_either, (long)s.nat_other);
    printf("  allocs=%ld cells=%ld (ctx-created=%ld heap-ctx=%ld frame=%ld)  raw=%ld\n",
           (long)s.allocs, (long)s.alloc_cells, (long)s.alloc_ctx,
           (long)s.promote, (long)s.alloc_frame, (long)s.raw);
    printf("  GC: cycles=%ld live-objs=%ld free-blocks=%ld free-cells=%ld reclaimed=%ld\n",
           (long)r0_s1_gc_count(), (long)r0_s1_gc_live_objs(),
           (long)r0_s1_gc_free_blocks(), (long)r0_s1_gc_free_cells(),
           (long)r0_s1_gc_reclaimed());
    printf("  heap-high=%ld  max-depth=%ld  SP(init=%ld min=%ld) RP(init=%ld min=%ld)\n",
           (long)r0_s1_heap_high(), (long)s.max_depth,
           (long)r0_s1_sp_start(), (long)r0_s1_sp_min(),
           (long)r0_s1_rp_start(), (long)r0_s1_rp_min());
    printf("  HOST-calls(arithmetic natives)=%ld\n", (long)r0_s1_host_calls());
}

/* ---- PART A: trace fib 5 ------------------------------------------------ */
static void trace_fib(int n) {
    printf("\n[TRACE-BEGIN fib %d]\n", n);
    fflush(stdout);
    M[PF_TRACE] = 1;
    int N = run_prog(build_full(n));
    M[PF_TRACE] = 0;
    fflush(stdout);
    printf("[TRACE-END fib %d]  (N=%d result=%lld)\n",
           n, N, (long long)(N == 1 ? int_val(r0_s1_result(0, 1)) : -1));
}
#endif /* R0_S1_PROFILE */

/* ---- PART C: wall-clock timing (both builds) ----------------------------- */
static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void time_fib(int n, int reps) {
    int err = 0;
    cell b = r0_s1_parse(build_full(n), &err);
    if (err) { printf("fib %2d: parse error\n", n); return; }

    r0_s1_run(b);                    /* warm up */

    static double ts[256];
    int got = 0;
    for (int r = 0; r < reps && got < 256; r++) {
        double t0 = now_sec();
        r0_s1_run(b);
        double t1 = now_sec();
        ts[got++] = t1 - t0;
    }
    /* median */
    for (int i = 0; i < got; i++)
        for (int j = i + 1; j < got; j++)
            if (ts[j] < ts[i]) { double t = ts[i]; ts[i] = ts[j]; ts[j] = t; }
    double med = ts[got / 2];
    printf("fib %2d: %d reps, median %.6f s  (min %.6f, max %.6f)\n",
           n, got, med, ts[0], ts[got - 1]);
}

int main(void) {
#ifdef R0_S1_PROFILE
    printf("FIB-PROFILE-P1: INSTRUMENTED build (counters + trace)\n");
#else
    printf("FIB-PROFILE-P1: BASELINE build (timing only; counters unavailable)\n");
#endif

    r0_s1_init();

#ifdef R0_S1_PROFILE
    printf("\n--- PART A: trace (fib 5) ---\n");
    trace_fib(5);

    printf("\n--- PART B: event counts ---\n");
    profile_fib(10);
    profile_fib(15);
    profile_fib(20);
    profile_fib(25);
    /* fib 30 omitted: fib 25 already takes ~25 s (baseline); fib 30 is ~11x
     * that (~270 s) and is optional per the brief. Uncomment to run it. */
    /* profile_fib(30); */
#endif

    printf("\n--- PART C: wall-clock (evaluation only, define+invoke) ---\n");
    time_fib(10, 100);
    time_fib(15, 50);
    time_fib(20, 20);
    time_fib(25, 5);
    /* time_fib(30, 3);   -- omitted: see note above */

    return 0;
}
