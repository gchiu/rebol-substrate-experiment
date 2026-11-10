/* B3 reference harness for B1's compressed Glon on frozen S1. */
#include "bench_util.h"
#include "../glon-direct-b1/b1_compiler.h"
#include "r0_s1.h"
#ifdef WITH_COUNTS
#include "../glon-direct-b1/s1_count.h"
#endif

#include <stdio.h>
#include <string.h>

#define FIB_CALLS 8361L
#define SUM_CALLS 501L

static const char *FIB_SRC =
    "fib: func [n] [ either < n 2 [ n ] [ + fib - n 1 fib - n 2 ] ] fib 18";
static const char *SUM_SRC =
    "sum: func [n acc] [ either = n 0 [ acc ] [ sum - n 1 + acc n ] ] sum 500 0";

static int run_source(const char *src, long *value, b1_code *code_out) {
    b1_code code;
    char err[256];
    if (!b1_compile(src, &code, err, sizeof err)) return 0;
    s1_reset();
#ifdef WITH_COUNTS
    b1_reset_counts();
#endif
    s1_run(code.entry);
    *value = int_val(s1_top());
    if (code_out) *code_out = code;
    return M[REG_SP] == 16384 - 1 && M[REG_RP] == 24576;
}

#ifdef WITH_COUNTS
static void measure_one(const char *label, const char *src, long expected, long calls) {
    b1_code code;
    long got = 0;
    if (!run_source(src, &got, &code) || got != expected) {
        printf("%s: setup/verify failed (got %ld, wanted %ld)\n", label, got, expected);
        return;
    }
    printf("%s\n", label);
    printf("  result %ld, total %ld, per-call %.2f, cells %ld, ds %ld, rs %ld\n",
           got, b1_insns, (double)b1_insns / (double)calls, code.code_cells,
           16384 - b1_min_sp, 24576 - b1_min_rp);
}

static void measure(void) {
    measure_one("B1 fib 18 (8,361 activations)", FIB_SRC, 2584, FIB_CALLS);
    measure_one("B1 sum 500 0 (501 activations, two parameters)", SUM_SRC, 125250, SUM_CALLS);
}
#else

typedef struct { cell entry; } RunCtx;
static void run_ctx(void *v) {
    RunCtx *c = v;
    s1_reset();
    s1_run(c->entry);
}

typedef struct { const char *src; } CompileCtx;
static void compile_ctx(void *v) {
    CompileCtx *c = v;
    b1_code code;
    char err[256];
    (void)b1_compile(c->src, &code, err, sizeof err);
}

static void bench_one(const char *label, const char *src, long expected) {
    b1_code code;
    long got = 0;
    if (!run_source(src, &got, &code) || got != expected) {
        printf("%s: setup/verify failed (got %ld, wanted %ld)\n", label, got, expected);
        return;
    }
    RunCtx rc = { code.entry };
    bench_stats bs = bench_measure(run_ctx, &rc, 0.05, 9);
    CompileCtx cc = { src };
    bench_stats cs = bench_measure(compile_ctx, &cc, 0.03, 7);
    printf("%s\n", label);
    printf("  run %9.2f us [min %8.2f max %8.2f spread %5.1f%%]  compile %8.2f us  iters %ld\n",
           bs.median * 1e6, bs.min * 1e6, bs.max * 1e6, bs.spread, cs.median * 1e6, bs.iters);
}

static void bench(void) {
    bench_one("B1 fib 18 (8,361 activations)", FIB_SRC, 2584);
    bench_one("B1 sum 500 0 (501 activations, two parameters)", SUM_SRC, 125250);

    b1_code code;
    char err[256];
    if (b1_compile("0", &code, err, sizeof err)) {
        RunCtx rc = { code.entry };
        bench_stats bs = bench_measure(run_ctx, &rc, 0.05, 9);
        printf("\nB1 empty program (LIT 0; HALT), reset included: %.2f us median\n", bs.median * 1e6);
    }
}
#endif

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--measure")) {
#ifdef WITH_COUNTS
        measure();
#else
        printf("measure mode needs b1-ref-count\n");
#endif
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--bench")) {
#ifndef WITH_COUNTS
        bench();
#else
        printf("bench mode needs the non-counter b1-ref\n");
#endif
        return 0;
    }
    printf("usage: b1-ref --measure | --bench\n");
    return 1;
}
