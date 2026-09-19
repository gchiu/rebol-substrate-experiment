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
        "ST_BREAK", "ST_RETURN", "ST_THROW", "RESULT_RETURN",
        "ds[", "cstack[",
        "eval_subexpr", "eval_block", "apply",
        "generator", "yield", "continuation", "resume"
    };
    int nfiles = 2, nbad = 13, violations = 0;
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

/* ==================== examples validation (test wiring) ===================
 * Reads the example files under examples/, strips `;;` comments, and runs the
 * first [ ... ] program in each, checking the documented result. This is
 * test-only; the runtime gains no file-loading ability. */

static char *read_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;
    static char buf[1 << 16];
    size_t n = fread(buf, 1, sizeof buf - 1, fp);
    buf[n] = 0;
    fclose(fp);
    return buf;
}

static void strip_comments(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
}

static void check_example(const char *path, int n, const cell *vals, const char *what) {
    char *src = read_file(path);
    if (!src) { printf("  (examples: cannot open %s; skipping)\n", path); return; }
    strip_comments(src);
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) { printf("  FAIL: %s (parse error)\n", what); failures++; return; }
    int N = r0_s1_run(b);
    int ok = (N == n);
    if (ok) for (int i = 0; i < n; i++) if (r0_s1_result(i, N) != vals[i]) ok = 0;
    if (ok) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s (N=%d want %d)\n", what, N, n); failures++; }
}

static void validate_examples(void) {
    { cell v[2] = { mk_int(42), mk_int(15) };
      check_example("examples/higher-order.r0", 2, v, "example: higher-order"); }
    { cell v[2] = { mk_int(10), mk_int(20) };
      check_example("examples/multiple-results.r0", 2, v, "example: multiple-results"); }
    { cell v[2] = { mk_int(5), mk_int(13) };
      check_example("examples/lexical-scope.r0", 2, v, "example: lexical-scope"); }
    { cell v[2] = { mk_int(42), mk_int(42) };
      check_example("examples/nested-escape.r0", 2, v, "example: nested-escape"); }
    { cell v[3] = { mk_int(-1), mk_int(0), mk_int(1) };
      check_example("examples/raw-branch.r0", 3, v, "example: raw-branch"); }
    { cell v[2] = { mk_int(10), mk_int(20) };
      check_example("examples/raw-memory.r0", 2, v, "example: raw-memory"); }
    { cell v[3] = { mk_int(5), mk_int(5), mk_int(5) };
      check_example("examples/raw-first-class.r0", 3, v, "example: raw-first-class"); }
    { cell v[2] = { mk_int(42), mk_int(0) };
      check_example("examples/return-through-unaware.r0", 2, v, "example: return-through-unaware"); }
    { cell v[5] = { mk_int(42), mk_int(42), mk_int(42), mk_int(100), mk_int(200) };
      check_example("examples/mini-library.r0", 5, v, "example: mini-library"); }
}

/* ===================== Phase 4B: user-defined escape =====================
 * with-escape / escape are written in ordinary R0 SOURCE plus two generic
 * `raw` fragments. The evaluator knows nothing about them (it only knows RAW
 * values are callable, from Phase 4A). The fragments use only the symbolic
 * RAW ABI (REG_*, RV_*, FRAME_*, SCRATCH_*), no hard-wired addresses. The
 * escape library is re-assembled into each test source. */

static const char *ESC_LIB =
    " frame-here: raw [ LIT RV_FRAME @ ARITY 1 EXIT ] "         /* read RV_FRAME */
    " restore: raw 2 [ "
    "   LIT SCRATCH_A ! LIT SCRATCH_B ! "                       /* save result, frame */
    "   LIT SCRATCH_B @ LIT FRAME_SAVED_CTX ADD @ LIT RV_CTX ! "
    "   LIT SCRATCH_B @ LIT FRAME_SAVED_CUR ADD @ LIT RV_CUR ! "
    "   LIT SCRATCH_B @ LIT FRAME_SAVED_END ADD @ LIT RV_END ! "
    "   LIT SCRATCH_B @ LIT FRAME_SAVED_BLK ADD @ LIT RV_BLK ! "
    "   LIT SCRATCH_B @ @ LIT RV_FRAME ! "                      /* RV_FRAME <- frame.prev */
    "   LIT SCRATCH_B @ LIT FRAME_SAVED_RP ADD @ LIT REG_RP ! "
    "   LIT SCRATCH_B @ LIT FRAME_SAVED_IP ADD @ >R "           /* stage frame.ip on R */
    "   LIT SCRATCH_B @ LIT FRAME_SAVED_SP ADD @ LIT REG_SP ! "
    "   LIT SCRATCH_A @ LIT 16 "                                /* push [result, count] */
    "   EXIT ] "
    " with-escape: func [body] [ "
    "   target: frame-here "
    "   escape: func [r] [ restore target r ] "
    "   body :escape ] ";

static char esc_buf[4096];
static int run_esc(const char *body, int *N) {
    r0_s1_init();   /* fresh code region + global context per escape program */
    snprintf(esc_buf, sizeof esc_buf, "[ %s %s ]", ESC_LIB, body);
    return run_src(esc_buf, N);
}
static void esc_expect1(const char *body, cell v, const char *what) {
    int N; run_esc(body, &N);
    int ok = (N == 1 && r0_s1_result(0, 1) == v);
    if (ok) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s (N=%d got %ld want %ld)\n", what, N,
                  (long)(N == 1 ? r0_s1_result(0, 1) : -999), (long)v); failures++; }
}
static void esc_expectN(const char *body, int n, const cell *vals, const char *what) {
    int N; run_esc(body, &N);
    int ok = (N == n);
    if (ok) for (int i = 0; i < n; i++) if (r0_s1_result(i, N) != vals[i]) ok = 0;
    if (ok) printf("  ok: %s\n", what);
    else { printf("  FAIL: %s (N=%d want %d)\n", what, N, n); failures++; }
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

    printf("R0-S1 phase 3A: non-local definitional RETURN\n");
    expect1("[ f: func [] [ return 42  99 ]  f ]",
            mk_int(42), "A: simple return 42 (99 never evaluated)");

    { /* B: return value through one unaware helper */
      cell v[2] = { mk_int(42), mk_int(0) };
      expectN("[ side: 0  "
              "helper: func [b] [ do b  side: 1 ]  "
              "outer: func [] [ helper [ return 42 ]  99 ]  "
              "values [outer side] ]",
              2, v, "B: return through helper (side effect skipped)"); }

    { /* C/D: deep unaware chain f->g->h->do, side effects must not run */
      int N;
      run_src("[ counter: 0  "
              "h: func [b] [ do b  counter: + counter 1000 ]  "
              "g: func [b] [ h b  counter: + counter 100 ]  "
              "f: func [b] [ g b  counter: + counter 10 ]  "
              "outer: func [] [ f [ return 42 ]  counter: + counter 1 ]  "
              "values [outer counter] ]", &N);
      int ok = (N == 2 && r0_s1_result(0, 2) == mk_int(42) && r0_s1_result(1, 2) == mk_int(0));
      CHECK(ok, "C: deep chain return, counter == 0 (all epilogues bypassed)");
      CHECK(r0_s1_rp_end() == r0_s1_rp_start(), "C: final RP == baseline");
      printf("    [deep chain] rp %ld->%ld (min %ld)  sp %ld->%ld (min %ld)\n",
             (long)r0_s1_rp_start(), (long)r0_s1_rp_end(), (long)r0_s1_rp_min(),
             (long)r0_s1_sp_start(), (long)r0_s1_sp_end(), (long)r0_s1_sp_min());
      /* E: ordinary call control - same chain without RETURN */
      expect1("[ counter: 0  "
              "h: func [b] [ do b  counter: + counter 1000 ]  "
              "g: func [b] [ h b  counter: + counter 100 ]  "
              "f: func [b] [ g b  counter: + counter 10 ]  "
              "outer: func [] [ f [ 42 ]  counter: + counter 1 ]  "
              "outer  counter ]",
              mk_int(1111), "E: same chain without RETURN -> counter == 1111"); }

    { /* F: recursion selects the innermost live activation */
      expect1("[ f: func [n] [ either >= n 3 [ return n ] [ + 1 f + n 1 ] ]  f 0 ]",
              mk_int(6), "F: recursive return targets innermost activation (f 0 == 6)"); }

    expect_arity("[ f: func [] [ return values [] ]  f ]",
                 0, "G: zero-result return (values [])");

    { cell v[2] = { mk_int(10), mk_int(20) };
      expectN("[ f: func [] [ return values [10 20] ]  f ]", 2, v,
              "H: multiple-result return (10 20)"); }

    { /* I: stack cleanliness */
      int N;
      run_src("[ f: func [] [ return 42 ]  f ]", &N);
      CHECK(N == 1 && r0_s1_sp_end() == r0_s1_sp_start() - 2,
            "I: after return, SP holds exactly the result set");
      CHECK(r0_s1_rp_end() == r0_s1_rp_start(), "I: RP restored to baseline");
      for (int i = 0; i < 5; i++) run_src("[ f: func [] [ return 42 ]  f ]", &N);
      CHECK(r0_s1_rp_end() == r0_s1_rp_start() && r0_s1_sp_end() == r0_s1_sp_start() - 2,
            "I: repeated return does not leak SP or RP"); }

    { /* J: control is not a value - the same unaware function for both paths */
      cell v[3] = { mk_int(7), mk_int(1), mk_int(42) };
      expectN("[ counter: 0  "
              "innocent: func [b] [ x: do b  counter: + counter 1  x ]  "
              "outer: func [] [ innocent [ return 42 ]  99 ]  "
              "n: innocent [ 7 ]  "
              "r: outer  "
              "values [n counter r] ]",
              3, v, "J: innocent: normal increments counter, return does not"); }

    printf("R0-S1 phase 3A: instrumentation\n");
    instrument("[ f: func [] [ return 42 ]  f ]", "simple return");

    printf("R0-S1 phase 4A: first-class RAW S1 trapdoor\n");
    expect1("[ c42: raw [ INT 42 ARITY 1 EXIT ]  c42 ]",
            mk_int(42), "A: raw constant 42 (zero args)");
    expect1("[ add2: raw 2 [ LIT 16 DIV >R LIT 16 DIV R> ADD LIT 16 MUL ARITY 1 EXIT ]  add2 3 4 ]",
            mk_int(7), "B: raw 2-arg sum 3+4 == 7");
    { cell v[3] = { mk_int(5), mk_int(5), mk_int(5) };
      expectN("[ c5: raw [ INT 5 ARITY 1 EXIT ]  values [ c5 c5 c5 ] ]", 3, v,
              "C: raw invoked repeatedly via values -> 5 5 5"); }
    expect1("[ apply: func [g] [ g ]  c5: raw [ INT 5 ARITY 1 EXIT ]  apply c5 ]",
            mk_int(5), "D: raw passed as arg and invoked == 5");
    expect1("[ add2: raw 2 [ LIT 16 DIV >R LIT 16 DIV R> ADD LIT 16 MUL ARITY 1 EXIT ]  "
            "f: func [n] [ add2 n 10 ]  f 5 ]",
            mk_int(15), "E: raw invoked from inside a func body == 15");
    { cell v[2] = { mk_int(10), mk_int(20) };
      expectN("[ two: raw [ INT 10 INT 20 ARITY 2 EXIT ]  two ]", 2, v,
              "F: raw multiple results -> 10 20"); }
    expect_arity("[ zero: raw [ ARITY 0 EXIT ]  zero ]", 0,
                 "G: raw zero results");
    { cell v[2] = { mk_int(1), mk_int(2) };
      expectN("[ t: raw 1 [ LIT 16 DIV LIT 0 EQ ZBRANCH Lelse INT 1 ARITY 1 EXIT "
              "Lelse: INT 2 ARITY 1 EXIT ]  values [ t 0  t 5 ] ]", 2, v,
              "H: raw ZBRANCH/labels -> 1 2"); }
    expect1("[ sw: raw [ INT 42 LIT SCRATCH_A ! LIT SCRATCH_A @ ARITY 1 EXIT ]  sw ]",
            mk_int(42), "I: raw @/! store+fetch 42");
    { int N; run_src("[ c42: raw [ INT 42 ARITY 1 EXIT ]  c42 ]", &N);
      CHECK(N == 1 && r0_s1_sp_end() == r0_s1_sp_start() - 2
            && r0_s1_rp_end() == r0_s1_rp_start(),
            "J: raw invocation leaves exactly the result set (no SP/RP leak)"); }
    { int N; run_src("[ c5: raw [ INT 5 ARITY 1 EXIT ]  :c5 ]", &N);
      CHECK(N == 1 && r0_tag(r0_s1_result(0, 1)) == T_RAW,
            "K: raw is first-class (get-word :c5 returns the raw value, tag T_RAW)"); }

    printf("R0-S1 phase 4A: instrumentation\n");
    instrument("[ add2: raw 2 [ LIT 16 DIV >R LIT 16 DIV R> ADD LIT 16 MUL ARITY 1 EXIT ]  "
                "f: func [n] [ add2 n 10 ]  f 5 ]", "raw in func");

    printf("R0-S1 phase 4B: user-defined first-class escape (R0 + RAW)\n");
    esc_expect1(" x: with-escape func [escape] [ escape 42  99 ]  x ",
                mk_int(42), "A: simple escape 42 (99 never evaluated)");
    { cell v[2] = { mk_int(42), mk_int(0) };
      esc_expectN(" counter: 0  "
                  "innocent: func [e] [ deeper :e  counter: + counter 1 ]  "
                  "deeper: func [e] [ e 42  counter: + counter 10 ]  "
                  "x: with-escape func [e] [ innocent :e  counter: + counter 100  99 ]  "
                  "values [x counter] ",
                  2, v, "B: escape through unaware innocent/deeper (counter == 0)"); }
    { cell v[2] = { mk_int(99), mk_int(111) };
      esc_expectN(" id: func [r] [ r ]  counter: 0  "
                  "innocent: func [e] [ deeper :e  counter: + counter 1 ]  "
                  "deeper: func [e] [ e 42  counter: + counter 10 ]  "
                  "x: with-escape func [e] [ innocent :id  counter: + counter 100  99 ]  "
                  "values [x counter] ",
                  2, v, "C: ordinary callable (no escape) runs all side effects (111)"); }
    { cell v[2] = { mk_int(42), mk_int(0) };
      esc_expectN(" pass2: func [g] [ g 42  counter: + counter 10 ]  "
                  "pass1: func [g] [ pass2 :g  counter: + counter 1 ]  counter: 0  "
                  "x: with-escape func [e] [ e2: :e  pass1 :e2 ]  "
                  "values [x counter] ",
                  2, v, "D: escape assigned and passed through two args (42, 0)"); }
    esc_expect1(" r: with-escape func [e1] [ s: with-escape func [e2] [ e2 42 ]  s ]  r ",
                mk_int(42), "E1: inner escape targets inner with-escape");
    esc_expect1(" r: with-escape func [e1] [ s: with-escape func [e2] [ e1 42 ]  99 ]  r ",
                mk_int(42), "E2: outer escape crosses inner with-escape");
    { cell v[3] = { mk_int(1), mk_int(2), mk_int(3) };
      esc_expectN(" f: func [n] [ with-escape func [e] [ e n ] ]  values [ f 1  f 2  f 3 ] ",
                  3, v, "F: repeated with-escape invocation -> 1 2 3"); }
    { int N; run_esc(" f: func [n] [ with-escape func [e] [ e n ] ]  values [ f 1  f 2  f 3 ] ", &N);
      CHECK(N == 3 && r0_s1_rp_end() == r0_s1_rp_start(),
            "F: repeated escape leaves no RP leakage"); }
    esc_expect1(" x: with-escape func [e] [ 42 ]  x ",
                mk_int(42), "G: with-escape body that never escapes returns normally");
    { int N;
      run_esc(" counter: 0  "
              "innocent: func [e] [ deeper :e  counter: + counter 1 ]  "
              "deeper: func [e] [ e 42  counter: + counter 10 ]  "
              "x: with-escape func [e] [ innocent :e  counter: + counter 100  99 ]  "
              "values [x counter] ", &N);
      CHECK(N == 2 && r0_s1_rp_end() == r0_s1_rp_start()
            && r0_s1_sp_end() == r0_s1_sp_start() - 3,
            "H: escape stack evidence (SP == result set, RP == baseline)");
      printf("    [escape chain] sp %ld->%ld (min %ld)  rp %ld->%ld (min %ld)\n",
             (long)r0_s1_sp_start(), (long)r0_s1_sp_end(), (long)r0_s1_sp_min(),
             (long)r0_s1_rp_start(), (long)r0_s1_rp_end(), (long)r0_s1_rp_min()); }

    printf("audit\n");
    audit_no_c_evaluator();

    printf("examples\n");
    r0_s1_init();   /* reset loader heap/code for a fresh group of programs */
    validate_examples();

    printf("R0-S1 code emitted: %ld cells\n", (long)r0_s1_code_size());
    if (failures == 0) printf("all R0-S1 tests passed\n");
    return failures;
}
