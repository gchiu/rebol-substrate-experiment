/* r0_s1_tests.c - R0-S1 tests (phase 1 + phase 2: functions/closures).
 * Each test: parse source -> load -> s1_run -> inspect results + machine state.
 * Plus a mechanical audit that the runtime does not implement evaluation in C.
 */
#include "r0_s1.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static int run_src(const char *src, int *N) {
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) { *N = -1; return 0; }
    *N = r0_s1_run(b);
    return 0;
}

static void expect1(const char *src, cell v, const char *what) {
    int N; run_src(src, &N);
    int ok = (N == 1 && r0_s1_result(0, 1) == v);
    if (ok) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s (N=%d got %ld want %ld)\n", what, N,
                  (long)(N == 1 ? r0_s1_result(0, 1) : -999), (long)v); failures++; }
}

static void expectN(const char *src, int n, const cell *vals, const char *what) {
    int N; run_src(src, &N);
    int ok = (N == n);
    if (ok) for (int i = 0; i < n; i++) if (r0_s1_result(i, N) != vals[i]) ok = 0;
    if (ok) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s (N=%d want %d)\n", what, N, n); failures++; }
}

static void expect_arity(const char *src, int wantN, const char *what) {
    int N; run_src(src, &N);
    if (N == wantN) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s (N=%d want %d)\n", what, N, wantN); failures++; }
}

/* print SP/RP instrumentation for one run */
static void instrument(const char *src, const char *label) {
    int N; run_src(src, &N);
    printf("  [%s] sp %ld->%ld (min %ld)  rp %ld->%ld (min %ld)  N=%d\n",
           label,
           (long)r0_s1_sp_start(), (long)r0_s1_sp_end(), (long)r0_s1_sp_min(),
           (long)r0_s1_rp_start(), (long)r0_s1_rp_end(), (long)r0_s1_rp_min(), N);
}

/* mechanical audit: the runtime must not implement evaluation in C */
static void audit_no_c_evaluator(void) {
    static const char *files[] = { "r0_s1_runtime.c", "r0_s1.h" };
    static const char *forbidden[] = {
        "ST_BREAK", "ST_RETURN", "ST_THROW",
        "ds[", "cstack[",
        "eval_subexpr", "eval_block", "apply"
    };
    int nfiles = 2, nbad = 8, violations = 0;
    for (int f = 0; f < nfiles; f++) {
        FILE *fp = fopen(files[f], "r");
        if (!fp) { printf("  (audit: cannot open %s; skipping)\n", files[f]); continue; }
        static char buf[1 << 20];
        size_t n = fread(buf, 1, sizeof buf - 1, fp);
        buf[n] = 0; fclose(fp);
        for (int b = 0; b < nbad; b++)
            if (strstr(buf, forbidden[b])) {
                printf("  FAIL: audit found forbidden pattern '%s' in %s\n", forbidden[b], files[f]);
                violations++;
            }
    }
    CHECK(violations == 0, "no C evaluator / C stack / status-enum patterns in runtime files");
}

int run_r0_s1_tests(void) {
    r0_s1_init();

    printf("R0-S1 phase 1: minimal evaluator on S1\n");
    expect1("[ 42 ]", mk_int(42), "phase1: [ 42 ] -> one INT 42");
    expect1("[ none ]", R0_NONE, "phase1: [ none ] -> one NONE");
    expect1("[ + 2 3 ]", mk_int(5), "phase1: [ + 2 3 ] -> one INT 5");
    expect1("[ x: 10  + x 5 ]", mk_int(15), "phase1: [ x: 10 + x 5 ] -> one INT 15");
    expect_arity("[ print 42 ]", 0, "phase1: [ print 42 ] -> zero results");
    expect1("[ 10 20 ]", mk_int(20), "phase1: [ 10 20 ] -> one result 20");

    printf("R0-S1 phase 2: functions, closures, recursion\n");
    expect1("[ add: func [a b] [ + a b ]  add 2 3 ]", mk_int(5), "A: add 2 3 == 5");
    expect1("[ f: func [] [ 42 ]  f ]", mk_int(42), "B: zero-arg f == 42");
    expect1("[ outer: func [x] [ func [y] [ + x y ] ]  add3: outer 3  add3 4 ]",
            mk_int(7), "C: lexical capture outer 3 applied to 4 == 7");
    { cell v[2] = { mk_int(15), mk_int(16) };
      expectN("[ make-counter: func [start] [ func [delta] [ start: + start delta  start ] ]  "
              "c: make-counter 10  a: c 5  b: c 1  values [a b] ]",
              2, v, "D: make-counter -> 15 16"); }
    expect1("[ fact: func [n] [ either <= n 1 [ 1 ] [ * n fact - n 1 ] ]  fact 5 ]",
            mk_int(120), "E: factorial 5 == 120");
    { int N; run_src("[ h: func [] [ 7 ]  g: func [] [ h ]  f: func [] [ g ]  f ]", &N);
      CHECK(N == 1 && r0_s1_result(0, 1) == mk_int(7), "F: f->g->h == 7");
      CHECK(r0_s1_rp_end() == r0_s1_rp_start(), "F: RP returned to baseline"); }
    { int N; run_src("[ add: func [a b] [ + a b ]  add 1 2  add 3 4  add 5 6 ]", &N);
      CHECK(N == 1 && r0_s1_result(0, 1) == mk_int(11), "G: repeated add calls -> 11");
      CHECK(r0_s1_sp_end() == r0_s1_sp_start() - 2 && r0_s1_rp_end() == r0_s1_rp_start(),
            "G: no SP/RP leakage"); }
    { int N; run_src("[ f: func [a b] [ + a b ]  f 1 2 ]", &N);
      CHECK(N == 1 && r0_s1_sp_end() == r0_s1_sp_start() - 2,
            "H: only the result set remains after a 2-arg call"); }
    expect1("[ add: func [a b] [ + a b ]  add 1 add 2 3 ]",
            mk_int(6), "I: nested application add 1 (add 2 3) == 6");
    { cell v[2] = { mk_int(10), mk_int(20) };
      expectN("[ values [10 20] ]", 2, v, "J: values [10 20] -> two results"); }
    expect_arity("[ values [] ]", 0, "K: values [] -> zero results");

    printf("R0-S1 phase 2: instrumentation\n");
    instrument("[ fact: func [n] [ either <= n 1 [ 1 ] [ * n fact - n 1 ] ]  fact 5 ]",
               "factorial 5");
    instrument("[ h: func [] [ 7 ]  g: func [] [ h ]  f: func [] [ g ]  f ]",
               "f->g->h chain");

    printf("audit\n");
    audit_no_c_evaluator();

    printf("R0-S1 code emitted: %ld cells\n", (long)r0_s1_code_size());
    if (failures == 0) printf("all R0-S1 tests passed\n");
    return failures;
}
