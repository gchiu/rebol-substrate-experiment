/* B3: calling-convention comparison and wall-clock benchmarks. */
#include "bench_util.h"
#include "b3_compiler.h"
#include "b3_machine.h"
#include "r0_s1.h"

#include <stdio.h>
#include <string.h>

#define FIB_CALLS 8361L
#define SUM_CALLS 501L

typedef struct { const char *name; unsigned flags; b3_convention conv; } Config;

static const Config configs[] = {
    { "A: none (B1 lowering)", 0, B3_CONV_RETSTACK },
    { "A: call+ret+>r+r>", B3_NATIVE_CALL | B3_NATIVE_RET | B3_NATIVE_TO_R | B3_NATIVE_FROM_R,
      B3_CONV_RETSTACK },
    { "A: call+ret+>r+r>+r@", B3_NATIVE_CALL | B3_NATIVE_RET | B3_NATIVE_TO_R |
      B3_NATIVE_FROM_R | B3_NATIVE_RFETCH, B3_CONV_RETSTACK },
    { "A: all six", B3_NATIVE_CALL | B3_NATIVE_RET | B3_NATIVE_TO_R |
      B3_NATIVE_FROM_R | B3_NATIVE_RFETCH | B3_NATIVE_BRANCH, B3_CONV_RETSTACK },
    { "B: none (B1 lowering)", 0, B3_CONV_DATAARGS },
    { "B: call+ret+>r+r>", B3_NATIVE_CALL | B3_NATIVE_RET | B3_NATIVE_TO_R | B3_NATIVE_FROM_R,
      B3_CONV_DATAARGS },
    { "B: call+ret+>r+r>+r@", B3_NATIVE_CALL | B3_NATIVE_RET | B3_NATIVE_TO_R |
      B3_NATIVE_FROM_R | B3_NATIVE_RFETCH, B3_CONV_DATAARGS },
    { "B: all six", B3_NATIVE_CALL | B3_NATIVE_RET | B3_NATIVE_TO_R |
      B3_NATIVE_FROM_R | B3_NATIVE_RFETCH | B3_NATIVE_BRANCH, B3_CONV_DATAARGS }
};
#define NCONFIG ((int)(sizeof configs / sizeof configs[0]))

static const char *FIB_SRC =
    "fib: func [n] [ either < n 2 [ n ] [ + fib - n 1 fib - n 2 ] ] fib 18";
static const char *SUM_SRC =
    "sum: func [n acc] [ either = n 0 [ acc ] [ sum - n 1 + acc n ] ] sum 500 0";

static int failures;

static void check(const char *what, int ok, const char *detail) {
    if (ok) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s%s%s\n", what, detail ? ": " : "", detail ? detail : ""); failures++; }
}

static int run_source(const char *src, const Config *cfg, long *value, b3_code *code_out,
                      int check_balance) {
    b3_code code;
    char err[256];
    if (!b3_compile(src, cfg->flags, cfg->conv, &code, err, sizeof err)) return 0;
    s1_reset();
#ifdef B3_COUNT
    b3_reset_counts();
#endif
    s1_run(code.entry);
    *value = int_val(s1_top());
    if (code_out) *code_out = code;
    return !check_balance || (M[REG_SP] == 16384 - 1 && M[REG_RP] == 24576);
}

static void positive_all(const char *name, const char *src, long want) {
    int ok = 1;
    char detail[256] = "";
    for (int i = 0; i < NCONFIG; i++) {
        long got = 0;
        if (!run_source(src, &configs[i], &got, NULL, 1) || got != want) {
            ok = 0;
            snprintf(detail, sizeof detail, "config '%s': wanted %ld, got %ld (or stack imbalance)",
                     configs[i].name, want, got);
            break;
        }
    }
    check(name, ok, detail[0] ? detail : NULL);
}

static void negative_all(const char *name, const char *src) {
    int ok = 1;
    for (int i = 0; i < NCONFIG; i++) {
        b3_code code;
        char err[256];
        if (b3_compile(src, configs[i].flags, configs[i].conv, &code, err, sizeof err)) {
            ok = 0; break;
        }
    }
    check(name, ok, ok ? NULL : "compiler accepted it in some configuration");
}

static long fib_ref(int n) {
    long a = 0, b = 1;
    for (int i = 0; i < n; i++) { long t = a + b; a = b; b = t; }
    return a;
}

static void fib_sweep(void) {
    int ok = 1;
    char detail[256] = "";
    for (int c = 0; c < NCONFIG && ok; c++) {
        for (int n = 0; n <= 18; n++) {
            char src[256]; long got = 0;
            snprintf(src, sizeof src,
                     "fib: func [n] [ either < n 2 [ n ] [ + fib - n 1 fib - n 2 ] ] fib %d", n);
            if (!run_source(src, &configs[c], &got, NULL, 1) || got != fib_ref(n)) {
                ok = 0;
                snprintf(detail, sizeof detail, "config '%s', fib %d: got %ld, wanted %ld",
                         configs[c].name, n, got, fib_ref(n));
                break;
            }
        }
    }
    check("fib 0..18 in every configuration, stack-balanced", ok, detail[0] ? detail : NULL);
}

static void invalid_opcode_test(void) {
    b3_code code;
    char err[256];
    if (!b3_compile("id: func [x] [ x ] id 1", 0, B3_CONV_RETSTACK,
                    &code, err, sizeof err)) {
        check("invalid opcode stops the machine", 0, "compile failed");
        return;
    }
    cell saved = M[code.entry];
    M[code.entry] = 99;
    s1_reset();
    b3_bad_opcode = 0;
    s1_run(code.entry);
    int ok = b3_bad_opcode != 0;
    M[code.entry] = saved;
    check("invalid opcode stops the machine", ok, ok ? NULL : "bad opcode was not reported");
}

static void tests(void) {
    printf("B3 tests (8 configurations: A/B x four primitive sets)\n");

    positive_all("identity", "id: func [x] [ x ] id 42", 42);
    positive_all("two arguments", "add: func [x y] [ + x y ] add 20 22", 42);
    positive_all("subtraction", "sub: func [x y] [ - x y ] sub 50 8", 42);
    positive_all("multiplication", "mul: func [x y] [ * x y ] mul 6 7", 42);
    positive_all("less-than true", "lt: func [x y] [ < x y ] lt 1 2", 1);
    positive_all("less-than false", "lt: func [x y] [ < x y ] lt 2 1", 0);
    positive_all("equality", "eq: func [x y] [ = x y ] eq 7 7", 1);
    positive_all("either then", "choose: func [x] [ either < x 2 [ 10 ] [ 20 ] ] choose 1", 10);
    positive_all("either else", "choose: func [x] [ either < x 2 [ 10 ] [ 20 ] ] choose 5", 20);
    positive_all("forward reference", "a: func [] [ b ] b: func [] [ 42 ] a", 42);
    positive_all("nested calls", "a: func [x] [ + x 1 ] b: func [x] [ a a x ] b 5", 7);
    positive_all("negative literal", "id: func [x] [ x ] id -3", -3);
    positive_all("recursive fib 5", "fib: func [n] [ either < n 2 [ n ] [ + fib - n 1 fib - n 2 ] ] fib 5", 5);
    positive_all("recursive fib 10", "fib: func [n] [ either < n 2 [ n ] [ + fib - n 1 fib - n 2 ] ] fib 10", 55);
    positive_all("recursive fib 18",
                 "fib: func [n] [ either < n 2 [ n ] [ + fib - n 1 fib - n 2 ] ] fib 18", 2584);
    positive_all("two-parameter recursion",
                 "sum: func [n acc] [ either = n 0 [ acc ] [ sum - n 1 + acc n ] ] sum 9 0", 45);
    positive_all("sum 500", SUM_SRC, 125250);
    positive_all("greater-than", "gt: func [x y] [ > x y ] gt 3 2", 1);
    positive_all("less-or-equal", "le: func [x y] [ <= x y ] le 2 2", 1);
    fib_sweep();
    invalid_opcode_test();

    negative_all("unknown word", "f: func [x] [ y ] f 1");
    negative_all("missing argument", "f: func [x] [ x ] f");
    negative_all("nested func unsupported", "f: func [x] [ g: func [] [ 1 ] x ] f 1");
    negative_all("if without else unsupported", "f: func [x] [ if x [ 1 ] ] f 1");
    negative_all("division unsupported", "f: func [x] [ / x 2 ] f 1");
    negative_all("set-word local unsupported", "f: func [x] [ x: 1 ] f 1");
    negative_all("block value unsupported", "f: func [x] [ [ x ] ] f 1");
    negative_all("string unsupported", "f: func [x] [ \"hi\" ] f 1");
    negative_all("empty body", "f: func [x] [] f 1");
    negative_all("duplicate function", "f: func [x] [ x ] f: func [x] [ x ] f 1");
    negative_all("unterminated body", "f: func [x] [ x f 1");
    negative_all("too many parameters", "f: func [a b c d e f g h i] [ a ] f 1 2 3 4 5 6 7 8 9");

    printf("\nB3 tests: %d failure(s)\n", failures);
}

#ifdef B3_COUNT
static void measure_one(const char *label, const char *src, long calls) {
    printf("\n%s\n", label);
    for (int i = 0; i < NCONFIG; i++) {
        b3_code code;
        char err[256];
        if (!b3_compile(src, configs[i].flags, configs[i].conv, &code, err, sizeof err)) {
            printf("  %-24s compile failed: %s\n", configs[i].name, err);
            continue;
        }
        s1_reset();
        b3_reset_counts();
        s1_run(code.entry);
        printf("  %-24s result %-7ld total %-10ld per-call %-8.2f cells %-4ld ds %-4ld rs %-4ld\n",
               configs[i].name, int_val(s1_top()), b3_insns, (double)b3_insns / (double)calls,
               code.code_cells, 16384 - b3_min_sp, 24576 - b3_min_rp);
    }
}

static void measure(void) {
    measure_one("fib 18 (8,361 activations)", FIB_SRC, FIB_CALLS);
    measure_one("sum 500 0 (501 activations, two parameters)", SUM_SRC, SUM_CALLS);
}
#else

typedef struct { cell entry; } RunCtx;
static void run_ctx(void *v) {
    RunCtx *c = v;
    s1_reset();
    s1_run(c->entry);
}

typedef struct { const char *src; unsigned flags; b3_convention conv; } CompileCtx;
static void compile_ctx(void *v) {
    CompileCtx *c = v;
    b3_code code;
    char err[256];
    (void)b3_compile(c->src, c->flags, c->conv, &code, err, sizeof err);
}

static void bench_one(const char *label, const char *src, long expected, long calls) {
    printf("\n%s\n", label);
    for (int i = 0; i < NCONFIG; i++) {
        b3_code code;
        char err[256];
        long got = 0;
        if (!b3_compile(src, configs[i].flags, configs[i].conv, &code, err, sizeof err) ||
            !run_source(src, &configs[i], &got, &code, 1) || got != expected) {
            printf("  %-24s setup/verify failed\n", configs[i].name);
            continue;
        }
        RunCtx rc = { code.entry };
        bench_stats bs = bench_measure(run_ctx, &rc, 0.05, 9);
        CompileCtx cc = { src, configs[i].flags, configs[i].conv };
        bench_stats cs = bench_measure(compile_ctx, &cc, 0.03, 7);
        printf("  %-24s run %9.2f us [min %8.2f max %8.2f spread %5.1f%%]  compile %8.2f us  iters %ld\n",
               configs[i].name, bs.median * 1e6, bs.min * 1e6, bs.max * 1e6,
               bs.spread, cs.median * 1e6, bs.iters);
        (void)calls;
    }
}

static void bench(void) {
    bench_one("fib 18 (8,361 activations)", FIB_SRC, 2584, FIB_CALLS);
    bench_one("sum 500 0 (501 activations, two parameters)", SUM_SRC, 125250, SUM_CALLS);

    b3_code code;
    char err[256];
    if (b3_compile("0", 0, B3_CONV_RETSTACK, &code, err, sizeof err)) {
        RunCtx rc = { code.entry };
        bench_stats bs = bench_measure(run_ctx, &rc, 0.05, 9);
        printf("\nempty program (LIT 0; HALT), reset included: %.2f us median\n", bs.median * 1e6);
    }
}
#endif

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--measure")) {
#ifdef B3_COUNT
        measure();
#else
        printf("measure mode needs b3-count (built with B3_COUNT)\n");
#endif
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--bench")) {
#ifndef B3_COUNT
        bench();
#else
        printf("bench mode needs the non-counter b3 binary\n");
#endif
        return 0;
    }
    tests();
    return failures ? 1 : 0;
}
