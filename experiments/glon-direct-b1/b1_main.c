/* B1: tests and measurement for the direct Glon subset compiler. */
#include "b1_compiler.h"
#include "r0_s1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef B1_COUNT
#include "s1_count.h"
#endif

#define FIB_CALLS 8361L          /* activations of naive fib 18 */

static int failures;

static void check(const char *what, int ok, const char *detail) {
    if (ok) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s%s%s\n", what, detail ? ": " : "", detail ? detail : ""); failures++; }
}

static int run_source(const char *src, long *value, b1_code *code_out) {
    b1_code code;
    char err[256];
    if (!b1_compile(src, &code, err, sizeof err)) {
        if (code_out) code_out->code_cells = -1;
        return 0;
    }
    s1_reset();
#ifdef B1_COUNT
    b1_reset_counts();
#endif
    s1_run(code.entry);
    *value = int_val(s1_top());
    if (code_out) *code_out = code;
    return 1;
}

static void positive(const char *name, const char *src, long want) {
    long got = 0;
    b1_code code;
    int ok = run_source(src, &got, &code);
    char detail[256];
    snprintf(detail, sizeof detail, "wanted %ld, got %ld, compile %s",
             want, got, ok ? "ok" : "failed");
    check(name, ok && got == want, detail);
}

static void negative(const char *name, const char *src) {
    b1_code code;
    char err[256];
    int ok = b1_compile(src, &code, err, sizeof err);
    check(name, !ok, ok ? "compiler accepted it" : NULL);
}


static long fib_ref(int n) {
    long a = 0, b = 1;
    for (int i = 0; i < n; i++) { long t = a + b; a = b; b = t; }
    return a;
}

static void fib_sweep(void) {
    int ok = 1;
    for (int n = 0; n <= 18; n++) {
        char src[256]; long got = 0;
        snprintf(src, sizeof src,
                 "fib: func [n] [ either < n 2 [ n ] [ + fib - n 1 fib - n 2 ] ] fib %d", n);
        if (!run_source(src, &got, NULL) || got != fib_ref(n)) {
            printf("  fib %d: got %ld, wanted %ld\n", n, got, fib_ref(n));
            ok = 0;
        }
    }
    check("fib 0..18 match the reference", ok, NULL);
}

static void tests(void) {
    printf("B1 direct-compiler tests\n");

    positive("identity", "id: func [x] [ x ] id 42", 42);
    positive("two arguments", "add: func [x y] [ + x y ] add 20 22", 42);
    positive("subtraction", "sub: func [x y] [ - x y ] sub 50 8", 42);
    positive("multiplication with tags", "mul: func [x y] [ * x y ] mul 6 7", 42);
    positive("less-than true", "lt: func [x y] [ < x y ] lt 1 2", 1);
    positive("less-than false", "lt: func [x y] [ < x y ] lt 2 1", 0);
    positive("equality", "eq: func [x y] [ = x y ] eq 7 7", 1);
    positive("either then", "choose: func [x] [ either < x 2 [ 10 ] [ 20 ] ] choose 1", 10);
    positive("either else", "choose: func [x] [ either < x 2 [ 10 ] [ 20 ] ] choose 5", 20);
    positive("forward reference", "a: func [] [ b ] b: func [] [ 42 ] a", 42);
    positive("negative literal", "id: func [x] [ x ] id -3", -3);
    positive("recursive fib 5", "fib: func [n] [ either < n 2 [ n ] [ + fib - n 1 fib - n 2 ] ] fib 5", 5);
    positive("recursive fib 10", "fib: func [n] [ either < n 2 [ n ] [ + fib - n 1 fib - n 2 ] ] fib 10", 55);
    positive("recursive fib 18",
             "fib: func [n] [ either < n 2 [ n ] [ + fib - n 1 fib - n 2 ] ] fib 18", 2584);
    positive("two-parameter recursion",
             "sum: func [n acc] [ either = n 0 [ acc ] [ sum - n 1 + acc n ] ] sum 9 0", 45);
    positive("greater-than", "gt: func [x y] [ > x y ] gt 3 2", 1);
    positive("less-or-equal", "le: func [x y] [ <= x y ] le 2 2", 1);
    positive("equality false", "eq: func [x y] [ = x y ] eq 7 8", 0);
    fib_sweep();

    negative("unknown word", "f: func [x] [ y ] f 1");
    negative("missing argument", "f: func [x] [ x ] f");
    negative("nested func unsupported", "f: func [x] [ g: func [] [ 1 ] x ] f 1");
    negative("if without else unsupported", "f: func [x] [ if x [ 1 ] ] f 1");
    negative("division unsupported", "f: func [x] [ / x 2 ] f 1");
    negative("set-word local unsupported", "f: func [x] [ x: 1 ] f 1");
    negative("block value unsupported", "f: func [x] [ [ x ] ] f 1");
    negative("string unsupported", "f: func [x] [ \"hi\" ] f 1");
    negative("empty body", "f: func [x] [] f 1");
    negative("duplicate function", "f: func [x] [ x ] f: func [x] [ x ] f 1");
    negative("unterminated body", "f: func [x] [ x f 1");
    negative("too many parameters",
             "f: func [a b c d e f g h i] [ a ] f 1 2 3 4 5 6 7 8 9");

    printf("\nB1 tests: %d failure(s)\n", failures);
}

#ifdef B1_COUNT
static void measure(void) {
    const char *src =
        "fib: func [n] [ either < n 2 [ n ] [ + fib - n 1 fib - n 2 ] ] fib 18";
    b1_code code;
    char err[256];
    if (!b1_compile(src, &code, err, sizeof err)) {
        printf("compile failed: %s\n", err);
        return;
    }
    s1_reset();
    b1_reset_counts();
    s1_run(code.entry);
    long result = int_val(s1_top());
    long total = b1_insns;
    long body = total - 15;          /* top entry: LIT, CALL expansion, HALT */
    long ds = 16384 - b1_min_sp;
    long rs = 24576 - b1_min_rp;
    printf("B1 compiled direct execution\n");
    printf("  result: %ld\n", result);
    printf("  functions: %d\n", code.nfuncs);
    printf("  generated code cells: %ld\n", code.code_cells);
    printf("  executed S1 instructions: %ld\n", total);
    printf("  instructions per fib activation (total / %ld): %.1f\n",
           FIB_CALLS, (double)total / (double)FIB_CALLS);
    printf("  instructions per fib activation (body, top entry removed): %.1f\n",
           (double)body / (double)FIB_CALLS);
    printf("  data-stack high-water: %ld cells\n", ds);
    printf("  return-stack high-water: %ld cells\n", rs);
    printf("  runtime-state cells used: 0 (no heap, loader, GC or RV cells)\n");
}
#endif

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--measure")) {
#ifdef B1_COUNT
        measure();
#else
        printf("measurement mode needs the counter build (build/b1-measure)\n");
#endif
        return 0;
    }
    tests();
    return failures ? 1 : 0;
}
