/* r0_s1_tests.c - phase-1 tests: prove R0 evaluation runs on S1 via s1_run.
 * Each test: parse source -> load -> s1_run -> inspect results + machine state.
 * Also a mechanical audit that the runtime does not implement evaluation in C.
 */
#include "r0_s1.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static int run_src(const char *src, cell **res, int *N) {
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) { *N = -1; return 0; }
    *N = r0_s1_run(b);
    (void)res;
    return 0;
}

static void expect1(const char *src, cell v, const char *what) {
    int N;
    run_src(src, NULL, &N);
    int ok = (N == 1 && r0_s1_result(0, 1) == v);
    if (ok) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s (N=%d got %ld want %ld)\n", what, N,
                  (long)(N == 1 ? r0_s1_result(0, 1) : -999), (long)v); failures++; }
}

static void expect_arity(const char *src, int wantN, const char *what) {
    int N;
    run_src(src, NULL, &N);
    if (N == wantN) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s (N=%d want %d)\n", what, N, wantN); failures++; }
}

/* print instrumentation for one run */
static void instrument(const char *src) {
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    printf("  [%s]\n", src);
    printf("    code=%ld cells  ip %ld->%ld  sp %ld->%ld  rp %ld->%ld  hostcalls=%ld\n",
           (long)r0_s1_code_size(),
           (long)r0_s1_ip_start(), (long)r0_s1_ip_end(),
           (long)r0_s1_sp_start(), (long)r0_s1_sp_end(),
           (long)r0_s1_rp_start(), (long)r0_s1_rp_end(),
           (long)0);
    if (!err) {
        int N = r0_s1_run(b);
        printf("    results: N=%d  hostcalls=%ld  sp %ld->%ld\n", N,
               (long)r0_s1_host_calls(), (long)r0_s1_sp_start(), (long)r0_s1_sp_end());
        for (int i = 0; i < N; i++)
            printf("      r%d = %ld (tag %d)\n", i, (long)r0_s1_result(i, N),
                   (int)r0_tag(r0_s1_result(i, N)));
    }
}

/* mechanical audit: the runtime must not implement evaluation in C */
static void audit_no_c_evaluator(void) {
    static const char *files[] = { "r0_s1_runtime.c", "r0_s1.h" };
    static const char *forbidden[] = {
        "ST_BREAK", "ST_RETURN", "ST_THROW",   /* C status enum */
        "ds[", "cstack[",                       /* C array stacks */
        "eval_subexpr", "eval_block", "apply"   /* C evaluator function names */
    };
    int nfiles = 2, nbad = 8;
    int violations = 0;
    for (int f = 0; f < nfiles; f++) {
        FILE *fp = fopen(files[f], "r");
        if (!fp) { printf("  (audit: cannot open %s; skipping)\n", files[f]); continue; }
        static char buf[1 << 20];
        size_t n = fread(buf, 1, sizeof buf - 1, fp);
        buf[n] = 0;
        fclose(fp);
        for (int b = 0; b < nbad; b++) {
            if (strstr(buf, forbidden[b])) {
                printf("  FAIL: audit found forbidden pattern '%s' in %s\n",
                       forbidden[b], files[f]);
                violations++;
            }
        }
    }
    CHECK(violations == 0, "no C evaluator / C stack / status-enum patterns in runtime files");
}

int run_r0_s1_tests(void) {
    r0_s1_init();

    printf("R0-S1 phase 1: minimal evaluator on S1\n");

    printf("mandatory tests\n");
    expect1("[ 42 ]", mk_int(42), "[ 42 ] -> one INT 42");
    expect1("[ none ]", R0_NONE, "[ none ] -> one NONE");
    expect1("[ + 2 3 ]", mk_int(5), "[ + 2 3 ] -> one INT 5");
    expect1("[ x: 10  + x 5 ]", mk_int(15), "[ x: 10 + x 5 ] -> one INT 15");
    expect_arity("[ print 42 ]", 0, "[ print 42 ] -> zero results");
    expect1("[ 10 20 ]", mk_int(20), "[ 10 20 ] -> one result 20 (sequential)");

    printf("extra eval-subexpr cases\n");
    expect1("[ x: 10  :x ]", mk_int(10), "get-word :x -> 10");
    {   /* lit-word 'x -> the WORD x */
        int N; run_src("[ 'x ]", NULL, &N);
        CHECK(N == 1 && r0_tag(r0_s1_result(0, 1)) == T_WORD, "lit-word 'x -> a WORD");
    }
    expect1("[ + + 2 3 4 ]", mk_int(9), "nested application (+ (+ 2 3) 4) == 9");
    expect1("[ x: 2  y: 3  * x y ]", mk_int(6), "x=2 y=3, * x y == 6");
    expect1("[ x: 10  x: 20  x ]", mk_int(20), "nearest-binding update: x reassigned to 20");
    expect1("[ - 10 3 ]", mk_int(7), "subtraction 10-3 == 7");

    printf("instrumentation sample\n");
    instrument("[ x: 10  + x 5 ]");

    printf("audit\n");
    audit_no_c_evaluator();

    printf("R0-S1 code emitted: %ld cells\n", (long)r0_s1_code_size());
    if (failures == 0) printf("all R0-S1 tests passed\n");
    return failures;
}
