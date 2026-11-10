/* B2: tests and measurements for native primitives on the B1 subset. */
#include "b2_compiler.h"
#include "b2_machine.h"
#include "r0_s1.h"

#include <stdio.h>
#include <string.h>

#define FIB_CALLS 8361L
#define SUM_CALLS 201L

typedef struct { const char *name; unsigned flags; } Config;

static const Config configs[] = {
    { "none (B1 lowering)", 0 },
    { "call",              B2_NATIVE_CALL },
    { "ret",               B2_NATIVE_RET },
    { ">r",                B2_NATIVE_TO_R },
    { "r>",                B2_NATIVE_FROM_R },
    { ">r+r>",             B2_NATIVE_TO_R | B2_NATIVE_FROM_R },
    { "r@",                B2_NATIVE_RFETCH },
    { "branch",            B2_NATIVE_BRANCH },
    { "call+ret",          B2_NATIVE_CALL | B2_NATIVE_RET },
    { "call+ret+branch",   B2_NATIVE_CALL | B2_NATIVE_RET | B2_NATIVE_BRANCH },
    { "call+ret+>r+r>",    B2_NATIVE_CALL | B2_NATIVE_RET |
                           B2_NATIVE_TO_R | B2_NATIVE_FROM_R },
    { "call+ret+>r+r>+r@", B2_NATIVE_CALL | B2_NATIVE_RET |
                           B2_NATIVE_TO_R | B2_NATIVE_FROM_R | B2_NATIVE_RFETCH },
    { "all",               B2_NATIVE_CALL | B2_NATIVE_RET |
                           B2_NATIVE_TO_R | B2_NATIVE_FROM_R |
                           B2_NATIVE_RFETCH | B2_NATIVE_BRANCH }
};
#define NCONFIG ((int)(sizeof configs / sizeof configs[0]))

static int failures;

static void check(const char *what, int ok, const char *detail) {
    if (ok) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s%s%s\n", what, detail ? ": " : "", detail ? detail : ""); failures++; }
}

static int run_source(const char *src, unsigned flags, long *value, b2_code *code_out) {
    b2_code code;
    char err[256];
    if (!b2_compile(src, flags, &code, err, sizeof err)) {
        if (code_out) code_out->code_cells = -1;
        return 0;
    }
    s1_reset();
    b2_reset_counts();
    s1_run(code.entry);
    *value = int_val(s1_top());
    if (code_out) *code_out = code;
    return 1;
}

static void positive_all(const char *name, const char *src, long want) {
    int ok = 1;
    char detail[256] = "";
    for (int i = 0; i < NCONFIG; i++) {
        long got = 0;
        if (!run_source(src, configs[i].flags, &got, NULL) || got != want) {
            ok = 0;
            snprintf(detail, sizeof detail, "config '%s': wanted %ld, got %ld",
                     configs[i].name, want, got);
            break;
        }
    }
    check(name, ok, detail[0] ? detail : NULL);
}

static void negative_all(const char *name, const char *src) {
    int ok = 1;
    for (int i = 0; i < NCONFIG; i++) {
        b2_code code;
        char err[256];
        if (b2_compile(src, configs[i].flags, &code, err, sizeof err)) { ok = 0; break; }
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
            if (!run_source(src, configs[c].flags, &got, NULL) || got != fib_ref(n)) {
                ok = 0;
                snprintf(detail, sizeof detail, "config '%s', fib %d: got %ld, wanted %ld",
                         configs[c].name, n, got, fib_ref(n));
                break;
            }
        }
    }
    check("fib 0..18 in every configuration", ok, detail[0] ? detail : NULL);
}

static void invalid_opcode_test(void) {
    b2_code code;
    char err[256];
    if (!b2_compile("id: func [x] [ x ] id 1", 0, &code, err, sizeof err)) {
        check("invalid opcode stops the machine", 0, "compile failed");
        return;
    }
    cell saved = M[code.entry];
    M[code.entry] = 99;
    s1_reset();
    b2_reset_counts();
    s1_run(code.entry);
    int ok = b2_bad_opcode != 0;
    M[code.entry] = saved;
    check("invalid opcode stops the machine", ok, ok ? NULL : "bad opcode was not reported");
}

static void tests(void) {
    printf("B2 native-primitive tests (every configuration)\n");

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
    positive_all("negative literal", "id: func [x] [ x ] id -3", -3);
    positive_all("recursive fib 5", "fib: func [n] [ either < n 2 [ n ] [ + fib - n 1 fib - n 2 ] ] fib 5", 5);
    positive_all("recursive fib 10", "fib: func [n] [ either < n 2 [ n ] [ + fib - n 1 fib - n 2 ] ] fib 10", 55);
    positive_all("recursive fib 18",
                 "fib: func [n] [ either < n 2 [ n ] [ + fib - n 1 fib - n 2 ] ] fib 18", 2584);
    positive_all("two-parameter recursion",
                 "sum: func [n acc] [ either = n 0 [ acc ] [ sum - n 1 + acc n ] ] sum 9 0", 45);
    positive_all("greater-than", "gt: func [x y] [ > x y ] gt 3 2", 1);
    positive_all("less-or-equal", "le: func [x y] [ <= x y ] le 2 2", 1);
    positive_all("equality false", "eq: func [x y] [ = x y ] eq 7 8", 0);
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

    printf("\nB2 tests: %d failure(s)\n", failures);
}

static void measure_one(const char *label, const char *src, long calls) {
    printf("\n%s\n", label);
    for (int i = 0; i < NCONFIG; i++) {
        b2_code code;
        char err[256];
        if (!b2_compile(src, configs[i].flags, &code, err, sizeof err)) {
            printf("  %-22s compile failed: %s\n", configs[i].name, err);
            continue;
        }
        s1_reset();
        b2_reset_counts();
        s1_run(code.entry);
        long result = int_val(s1_top());
        printf("  %-22s result %-6ld total %-10ld per-call %-8.2f cells %-4ld ds %-3ld rs %-3ld\n",
               configs[i].name, result, b2_insns, (double)b2_insns / (double)calls,
               code.code_cells, 16384 - b2_min_sp, 24576 - b2_min_rp);
    }
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--measure")) {
        measure_one("fib 18 (8,361 activations)",
                    "fib: func [n] [ either < n 2 [ n ] [ + fib - n 1 fib - n 2 ] ] fib 18",
                    FIB_CALLS);
        measure_one("sum 200 0 (201 activations, two parameters)",
                    "sum: func [n acc] [ either = n 0 [ acc ] [ sum - n 1 + acc n ] ] sum 200 0",
                    SUM_CALLS);
        return 0;
    }
    tests();
    return failures ? 1 : 0;
}
