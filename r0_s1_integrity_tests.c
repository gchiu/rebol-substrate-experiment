/* r0_s1_integrity_tests.c - machine-integrity tests (make machine-integrity).
 *
 * The functional suite (./s1) checks what programs compute. These checks are
 * about the machine itself: that runs, failures and collections leave the
 * 64K-cell memory in a state the next run can trust. See
 * MACHINE-INTEGRITY-AUDIT.md for the inventory, the invariants and the
 * coverage matrix this file was written against.
 *
 *   R  the two boundary regressions, re-run here (gc_interior, masm_bound)
 *   A  determinism: fresh sessions and identical workloads give identical
 *      memory; re-initialisation is a total reset
 *   B  recovery: every failure kind, repeated, returns the session to the same
 *      state (code end, code, loader heap, globals, live heap after a
 *      collection) and the next run is correct
 *   C  the runtime's own code is never written by any of the above
 *   K  KNOWN DEFECTS, as strict expected failures. Each check states the
 *      invariant that SHOULD hold; it does not hold today, so it reports XFAIL
 *      and does not count as a failure. If a repair makes it hold, it reports
 *      XPASS and DOES count as a failure, so that the check is moved into the
 *      ordinary sections deliberately rather than passing unnoticed.
 *
 * Everything runs in this process's own copy of the runtime memory; the K
 * checks that overwrite code never execute the overwritten code (each is
 * followed by a fresh initialisation). The masm label/reference buffer
 * overflow (K5) is host C undefined behaviour and is exercised only in the
 * separate AddressSanitizer probe (integrity/asan_probe.c), never here.
 *
 *   machine-integrity-test [-v]     -v: do not silence expected stderr
 */

#include "r0_s1.h"
#include "m1_layout.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int run_r0_s1_gc_interior_tests(void);
int run_r0_s1_masm_bound_tests(void);

static int failures = 0, xfails = 0, xpasses = 0, verbose = 0;
static char src[1 << 17];
static int N;

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)
/* a known defect: `c` is the invariant that should hold */
#define XFAIL(c, m) do { if (c) { printf("  XPASS (known defect now fixed: promote this check): %s\n", m); xpasses++; } \
                         else { printf("  xfail (known defect): %s\n", m); xfails++; } } while (0)

/* ---- quiet stderr while a failure is provoked on purpose ----------------- */
static int saved_err = -1;
static void quiet(void) {
    if (verbose) return;
    fflush(stderr); saved_err = dup(2);
    int fd = open("/dev/null", O_WRONLY);
    if (fd >= 0) { dup2(fd, 2); close(fd); }
}
static void loud(void) {
    if (saved_err < 0) return;
    fflush(stderr); dup2(saved_err, 2); close(saved_err); saved_err = -1;
}

/* ---- session helpers ----------------------------------------------------- */
static void fresh(void) {
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me; M[M1_CURSOR] = 0; M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
}
/* a wrapped program through the session facade; answers the outcome status */
static int sess(const char *prog, r0_s1_outcome *oc) {
    r0_s1_outcome tmp; if (!oc) oc = &tmp;
    r0_s1_session_run(prog, (unsigned)strlen(prog), oc);
    N = oc->status == R0S1_OUT_OK ? oc->count : -1;
    return oc->status;
}
static long got(void) { return (N >= 1) ? (long)int_val(r0_s1_result(N - 1, N)) : -999; }
static int load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { printf("  (cannot open %s)\n", path); return -3; }
    size_t n = fread(src, 1, sizeof src - 1, f); fclose(f); src[n] = 0;
    char *r = src, *w = src;
    while (*r) { if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; } *w++ = *r++; }
    *w = 0;
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) return -1;
    r0_s1_run_persistent(b);
    return r0_s1_ran_cleanly() ? 0 : -2;
}
static unsigned long checksum(cell a, cell b) {
    unsigned long h = 1469598103UL;
    for (cell c = a; c < b; c++) h = (h ^ (unsigned long)M[c]) * 1099511628211UL;
    return h;
}
static cell runtime_code_end(void) { return 256 + r0_s1_code_size(); }

/* ---- the observable session state ---------------------------------------- */
typedef struct {
    cell here, loader_hp, globals, heap_high;
    unsigned long code;          /* checksum of all emitted code [256, here) */
    long live_cells, live_objs;  /* after a full collection */
} state_t;
static state_t state(void) {
    state_t s;
    sess("ix/gc 0", NULL);       /* a full collection (ix/gc is defined in setup) */
    s.here = asm_here(); s.loader_hp = M[GC_LOADER_HP];
    cell cap; r0_s1_global_usage(&s.globals, &cap);
    s.heap_high = (cell)r0_s1_heap_high();
    s.code = checksum(256, s.here);
    s.live_cells = r0_s1_gc_live_cells(); s.live_objs = r0_s1_gc_live_objs();
    return s;
}
/* the loader heap is a bump region that keeps every parsed cell (by design,
 * reported as LOADER_EXHAUSTED when full), so it is compared separately */
static int same(state_t a, state_t b) {
    int ok = a.here == b.here && a.globals == b.globals
        && a.code == b.code && a.live_cells == b.live_cells && a.live_objs == b.live_objs;
    if (!ok && verbose)
        printf("    differs: here %ld/%ld globals %ld/%ld code %d live %ld/%ld objs %ld/%ld\n",
               (long)a.here, (long)b.here, (long)a.globals, (long)b.globals, a.code != b.code,
               a.live_cells, b.live_cells, a.live_objs, b.live_objs);
    return ok;
}
static void show(const char *what, state_t s) {
    if (verbose) printf("    %s: here %ld lhp %ld globals %ld heap_high %ld live %ld/%ld\n", what, (long)s.here,
                        (long)s.loader_hp, (long)s.globals, (long)s.heap_high, s.live_cells, s.live_objs);
}

/* a session with the shipped libraries and the helpers B needs */
static int setup(const char *root) {
    char p[4096];
    fresh();
    const char *libs[] = { "jupyter/prelude.glon", "demo/shop/strings.glon", "demo/shop/strings-ext.glon", NULL };
    for (int i = 0; libs[i]; i++) {
        snprintf(p, sizeof p, "%s/%s", root, libs[i]);
        if (load(p)) return -1;
    }
    return sess("ix/gc: masm 1 [ DROP CALL collect LIT 0 ARITY 1 EXIT ] "
                "ix/ok: func [a b] [ * a b ] "
                "ix/inf: func [n] [ + 1 ix/inf n ] "
                "ix/dbl: func [s n] [ either = n 0 [ s/length s ] [ ix/dbl s/+ s s - n 1 ] ] "
                "ix/mk: func [n] [ func [] [ n: + n 1 ] ] "
                /* each level holds 50 pending `+` arguments on the data stack:
                 * 80 levels need ~4,000 cells, more than the data stack has */
                "ix/deep: func [n] [ either = n 0 [ 0 ] [ "
                "+ 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 "
                "+ 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 "
                "ix/deep - n 1 ] ] "
                "0", NULL) == R0S1_OUT_OK ? 0 : -1;
}

/* ---- the failure kinds of B ---------------------------------------------- */
typedef struct { const char *name, *prog; int want; } fail_t;
static char too_large[4096];
static char deep_nest[1 << 16];     /* `+ 1` nested 3,600 deep (filled in main) */
static fail_t fails[] = {
    { "parse error (syntax)",          "y: [ oops",                                      R0S1_OUT_PARSE_ERROR },
    { "parse error (too large)",       too_large,                                        R0S1_OUT_PARSE_ERROR },
    { "uncaught SIN!",                 "raise create-sin 'ix 'oops 7",                    R0S1_OUT_UNCAUGHT_SIN },
    { "unset word (HALT / machine)",   "ix-never-defined 1",                             R0S1_OUT_HALT },
    { "managed-heap exhaustion",       "ix/dbl \"abcdefgh\" 12",                         -1 },
    { "return-stack exhaustion",       "ix/inf 0",                                       -1 },
    { "context full (17 bindings)",    "ix/f17: func [] [ a1: 1 a2: 2 a3: 3 a4: 4 a5: 5 a6: 6 a7: 7 a8: 8 "
                                       "a9: 9 a10: 10 a11: 11 a12: 12 a13: 13 a14: 14 a15: 15 a16: 16 a17: 17 ] ix/f17",
                                                                                         R0S1_OUT_RESOURCE_ERROR },
    { "data-stack exhaustion (recursion, 50 pending per level)", "ix/deep 80",        R0S1_OUT_HALT },
    { "judge-caught SIN! (control)",   "judge [ raise create-sin 'ix 'oops 7 ] 0",        R0S1_OUT_OK },
    { "closure churn (control)",       "c: ix/mk 0  c c c  c: 0  0",                      R0S1_OUT_OK },
};
#define NFAILS ((int)(sizeof fails / sizeof fails[0]))

/* ---- K: the known defects ------------------------------------------------- */
static int word_src(char *out, size_t cap, const char *name, long cells) {
    long e = cells - 21, b = 0;
    while (e - 4 * b >= 0 && (e - 4 * b) % 3 != 0) b++;
    if (e < 0 || e - 4 * b < 0) return -1;
    long a = (e - 4 * b) / 3;
    int n = snprintf(out, cap, "%s: masm 0 [ ", name);
    for (long i = 0; i < a; i++) n += snprintf(out + n, cap - (size_t)n, "LIT 0 DROP ");
    for (long i = 0; i < b; i++) n += snprintf(out + n, cap - (size_t)n, "LIT 0 @ DROP ");
    n += snprintf(out + n, cap - (size_t)n, "LIT 0 ARITY 1 EXIT ] 0");
    return 0;
}
static int representable(long e) { return e == 0 || e == 3 || e == 4 || e >= 6; }
/* masm filler words up to exactly `limit` */
static int fill_to(cell limit) {
    static char w[1 << 15];
    for (int k = 0;; k++) {
        long room = (long)limit - (long)asm_here();
        if (room == 0) return 0;
        long size;
        if (room > 420) size = 400;
        else if (representable(room - 21)) size = room;
        else {
            size = 0;
            for (long s = 24; s < 40; s++)
                if (representable(s - 21) && representable(room - s - 21)) { size = s; break; }
            if (!size) return -1;
        }
        char name[32]; snprintf(name, sizeof name, "fill-%d", k);
        word_src(w, sizeof w, name, size);
        cell before = asm_here();
        if (sess(w, NULL) != R0S1_OUT_OK || asm_here() - before != size) return -1;
    }
}
/* run `prog`; answers 1 iff it was reported clean while emitted code changed */
static int clean_but_code_changed(const char *prog, int *clean) {
    cell here = asm_here(); unsigned long s0 = checksum(256, here);
    r0_s1_outcome oc; quiet(); sess(prog, &oc); loud();
    *clean = oc.status == R0S1_OUT_OK;
    return *clean && checksum(256, here) != s0;
}

/* `+ 1` nested `depth` deep: 100 per block, each further 100 inside an
 * `either 1 [ ... ] [ 0 ]` (a block holds at most 512 values); computes depth + 1 */
static void build_nest(char *out, size_t cap, int depth) {
    int n = 0, open = 0;
    for (int i = 0; i < depth; i++) {
        n += snprintf(out + n, cap - (size_t)n, "+ 1 ");
        if (i % 100 == 99 && i + 1 < depth) { n += snprintf(out + n, cap - (size_t)n, "either 1 [ "); open++; }
    }
    n += snprintf(out + n, cap - (size_t)n, "1 ");
    while (open--) n += snprintf(out + n, cap - (size_t)n, "] [ 0 ] ");
}
/* run `prog` with the free data stack [here, DS_INIT) poisoned; answers the
 * status and sets *lowest to the lowest cell the run wrote there. The
 * programs here bind nothing that refers to their source, so the loader-heap
 * mark is restored afterwards (every parsed cell is otherwise kept, and the
 * bisection below parses thousands of values per run). */
static int run_poisoned(const char *prog, cell *lowest, r0_s1_outcome *oc) {
    const cell P = (cell)0x5A5A5A5;
    cell here = asm_here(), lhp = M[GC_LOADER_HP];
    for (cell c = here; c < R0S1_DS_INIT; c++) M[c] = P;
    quiet(); sess(prog, oc); loud();
    M[GC_LOADER_HP] = lhp;
    *lowest = R0S1_DS_INIT;
    for (cell c = here; c < R0S1_DS_INIT; c++) if (M[c] != P) { *lowest = c; break; }
    return oc->status;
}
/* the deepest `+ 1` nesting that runs, by bisection; checks the boundary */
static void depth_boundary(const char *where) {
    static char w[1 << 16];
    r0_s1_outcome oc; cell low;
    cell here = asm_here(); unsigned long code = checksum(256, here);
    int lo = 1, hi = 4000, runs = 0, other = 0;   /* lo runs; hi does not */
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        build_nest(w, sizeof w, mid);
        int st = run_poisoned(w, &low, &oc); runs++;
        if (st == R0S1_OUT_OK) lo = mid;
        else { hi = mid; if (!(st == R0S1_OUT_HALT && r0_s1_halt_reason() == R0S1_HALT_DS_EXHAUSTED)) other++; }
    }
    char m[256];
    snprintf(m, sizeof m, "S: %s: every bisection run either completes or fail-stops as data-stack exhaustion", where);
    CHECK(other == 0, m);
    build_nest(w, sizeof w, lo);
    int ok = run_poisoned(w, &low, &oc) == R0S1_OUT_OK && got() == lo + 1;
    snprintf(m, sizeof m, "S: %s: the deepest nesting that runs (%d) computes %d, writing nothing below the end of code "
             "(lowest cell written %ld, code ends at %ld)", where, lo, lo + 1, (long)low, (long)here);
    CHECK(ok && low >= here, m);
    build_nest(w, sizeof w, hi);
    run_poisoned(w, &low, &oc);
    snprintf(m, sizeof m, "S: %s: one level deeper (%d) fail-stops as data-stack exhaustion, writing nothing below the end of code",
             where, hi);
    CHECK(oc.status == R0S1_OUT_HALT && r0_s1_halt_reason() == R0S1_HALT_DS_EXHAUSTED && low >= here, m);
    snprintf(m, sizeof m, "S: %s: no code changed by the bisection's %d runs", where, runs);
    CHECK(checksum(256, here) == code, m);
    if (verbose) printf("    %s: room %ld cells, deepest nesting %d\n", where, (long)(R0S1_DS_INIT - here), lo);
}

static void section_S(const char *root) {
    printf("== S: stack boundaries (data-stack guard, R0S1_DS_GUARD = %ld)\n", (long)R0S1_DS_GUARD);
    r0_s1_outcome oc; cell low;
    static char w[1 << 16];

    /* the K2 reproduction: ordinary Glon, no masm, 3,600 deep */
    setup(root);
    cell here = asm_here(); unsigned long code = checksum(256, here);
    run_poisoned(deep_nest, &low, &oc);
    CHECK(oc.status == R0S1_OUT_HALT && oc.detail == R0S1_DETAIL_MACHINE && r0_s1_halt_reason() == R0S1_HALT_DS_EXHAUSTED,
          "S1: `+ 1` nested 3,600 deep (no masm) fail-stops as data-stack exhaustion (HALT / machine)");
    CHECK(low >= here && checksum(256, here) == code,
          "S1: ... before writing below the end of code: the runtime and libraries are unchanged");
    CHECK(sess("ix/ok 6 7", NULL) == R0S1_OUT_OK && got() == 42 && !r0_s1_stack_sentry_fired(),
          "S1: the session continues (6 * 7) with no stack-sentry report");

    /* the exact boundary, with the libraries' code and with code full */
    depth_boundary("libraries loaded");
    setup(root);
    CHECK(fill_to(R0S1_CODE_LIMIT) == 0 && asm_here() == R0S1_CODE_LIMIT, "S2: code filled to R0S1_CODE_LIMIT with masm words");
    depth_boundary("code full");
    CHECK(sess("fill-0", NULL) == R0S1_OUT_OK && got() == 0, "S2: a library word still runs");

    /* values that accumulate on the data stack: `values` returns every
     * value of its block (a top-level block keeps only its last) */
    int n = snprintf(w, sizeof w, "0 1 2 ");
    for (int i = 0; i < 400; i++) n += snprintf(w + n, sizeof w - (size_t)n, "%d ", i);
    here = asm_here(); code = checksum(256, here);
    CHECK(run_poisoned(w, &low, &oc) == R0S1_OUT_OK && oc.count == 1 && low >= here,
          "S3: with code full, a top-level block of 400 values runs (only the last value is kept)");
    n = snprintf(w, sizeof w, "values [ ");
    for (int i = 0; i < 400; i++) n += snprintf(w + n, sizeof w - (size_t)n, "%d ", i);
    snprintf(w + n, sizeof w - (size_t)n, "]");
    run_poisoned(w, &low, &oc);
    CHECK(oc.status == R0S1_OUT_HALT && r0_s1_halt_reason() == R0S1_HALT_DS_EXHAUSTED && low >= here
          && checksum(256, here) == code,
          "S3: with code full, `values` of 400 values (all kept on the data stack) fail-stops before reaching code");
    n = snprintf(w, sizeof w, "values [ ");
    for (int i = 0; i < 100; i++) n += snprintf(w + n, sizeof w - (size_t)n, "%d ", i);
    snprintf(w + n, sizeof w - (size_t)n, "]");
    CHECK(run_poisoned(w, &low, &oc) == R0S1_OUT_OK && oc.count == 100 && low >= here,
          "S3: control: `values` of 100 values fits (100 values returned)");
    n = snprintf(w, sizeof w, "b: reduce [ ");
    for (int i = 0; i < 400; i++) n += snprintf(w + n, sizeof w - (size_t)n, "%d ", i);
    snprintf(w + n, sizeof w - (size_t)n, "] 0");
    CHECK(run_poisoned(w, &low, &oc) == R0S1_OUT_OK && low >= here,
          "S4: with code full, `reduce` of 400 values still runs (it does not accumulate on the data stack)");

    /* the guard and the sentry together: closure arguments (the sentry samples
     * at closure entry; the guard stops the growth first) */
    sess("ix/g15: func [a b c d e f h i j k l m n o p] [ 0 ] 0", NULL);
    here = asm_here(); code = checksum(256, here);
    n = 0;
    for (int d = 0; d < 30; d++) n += snprintf(w + n, sizeof w - (size_t)n, "ix/g15 1 1 1 1 1 1 1 1 1 1 1 1 1 1 ");
    snprintf(w + n, sizeof w - (size_t)n, "0");
    run_poisoned(w, &low, &oc);
    CHECK(oc.status == R0S1_OUT_HALT && r0_s1_halt_reason() == R0S1_HALT_DS_EXHAUSTED && low >= here
          && checksum(256, here) == code && !r0_s1_stack_sentry_fired(),
          "S5: with code full, 30 nested 15-argument calls fail-stop before reaching code (no sentry needed)");

    /* recursion whose activations hold data-stack values (B8's failure kind) */
    setup(root);
    here = asm_here(); code = checksum(256, here);
    run_poisoned("ix/deep 80", &low, &oc);
    CHECK(oc.status == R0S1_OUT_HALT && r0_s1_halt_reason() == R0S1_HALT_DS_EXHAUSTED && low >= here
          && checksum(256, here) == code,
          "S6: recursion holding 50 pending values per level fail-stops on the data stack (not the return stack), before reaching code");
    CHECK(run_poisoned("ix/deep 20", &low, &oc) == R0S1_OUT_OK && got() == 1000,
          "S6: control: 20 levels (1,000 pending values) compute 1000");

    /* a collection deep in the stack: the collector's own data-stack use stays
     * inside the guard (it was measured at no more than 16 cells) */
    setup(root);
    CHECK(fill_to(R0S1_CODE_LIMIT) == 0, "S8 setup: code filled to R0S1_CODE_LIMIT");
    sess("ix/junk: func [n] [ either = n 0 [ 0 ] [ s/length s/+ s/from-int n \"x\"  ix/junk - n 1 ] ] 0", NULL);
    here = asm_here(); code = checksum(256, here);
    {
        /* 150-deep nesting whose innermost value allocates and collects */
        int k = 0;
        for (int i = 0; i < 150; i++) k += snprintf(w + k, sizeof w - (size_t)k, "+ 1 ");
        snprintf(w + k, sizeof w - (size_t)k, "+ ix/junk 30 ix/gc 0");
    }
    CHECK(run_poisoned(w, &low, &oc) == R0S1_OUT_OK && got() == 150 + 0 && low >= here && checksum(256, here) == code
          && r0_s1_gc_count() > 0,
          "S8: with code full, a collection 150 values deep runs and writes nothing below the end of code");

    /* underflow: a masm word that declares a result it did not leave pops a
     * cell from above the data stack (the return stack's bottom cell), and
     * the next push would overwrite it (examples/raw-memory.r0 did this until
     * 2026-11-10). The guard fail-stops at the next evaluation step. */
    setup(root);
    sess("ix/sink: masm 1 [ LIT SCRATCH_A ! ARITY 1 EXIT ] 0", NULL);
    const cell P = (cell)0x5A5A5A5;
    M[R0S1_DS_INIT] = P;
    quiet(); sess("ix/sink 10  ix/sink 20  0", &oc); loud();
    CHECK(oc.status == R0S1_OUT_HALT && r0_s1_halt_reason() == R0S1_HALT_DS_UNDERFLOW,
          "S7: a top-level pop past the data stack's top fail-stops as data-stack underflow");
    CHECK(M[R0S1_DS_INIT] == P, "S7: ... before the next push writes the return stack's bottom cell");
    CHECK(sess("ix/ok 6 7", NULL) == R0S1_OUT_OK && got() == 42, "S7: the session continues (6 * 7)");
}

/* ---- L: failed loads emit and keep nothing --------------------------------
 * masm blocks are emitted only when the whole source has parsed (K3, repaired
 * 2026-11-10): before, a source whose first block fitted and whose second did
 * not was refused but left the first block's code behind, and repeated
 * refusals exhausted the code space. */
typedef struct { cell here, lhp, globals; unsigned long code; } load_state_t;
static load_state_t load_state(void) {
    load_state_t t; cell cap;
    t.here = asm_here(); t.lhp = M[GC_LOADER_HP]; r0_s1_global_usage(&t.globals, &cap);
    t.code = checksum(256, t.here);
    return t;
}
static int load_same(load_state_t a, load_state_t b) {
    return a.here == b.here && a.lhp == b.lhp && a.globals == b.globals && a.code == b.code;
}
static void section_L(const char *root) {
    printf("== L: failed-load recovery\n");
    static char w[1 << 15];
    setup(root);
    const char *ok_block = "lx-a: masm 0 [ LIT 0 ARITY 1 EXIT ] ";

    /* the K3 reproduction: the second block can never fit */
    int n = snprintf(w, sizeof w, "%slx-b: masm 0 [ x: ", ok_block);
    for (int i = 0; i < 250; i++) n += snprintf(w + n, sizeof w - (size_t)n, "CALL x ");
    snprintf(w + n, sizeof w - (size_t)n, "LIT 0 ARITY 1 EXIT ] 0");
    load_state_t t0 = load_state();
    int refused = 1;
    quiet(); for (int i = 0; i < 200; i++) if (sess(w, NULL) != R0S1_OUT_PARSE_ERROR) refused = 0; loud();
    CHECK(refused, "L1: 200 sources whose second masm block cannot fit are each refused (too_large)");
    CHECK(load_same(load_state(), t0),
          "L1: ... and leave code end, code, loader heap and bindings exactly as they were");
    CHECK(sess("lx-ok: masm 0 [ INT 5 ARITY 1 EXIT ] lx-ok", NULL) == R0S1_OUT_OK && got() == 5,
          "L1: a masm word still loads and runs afterwards");

    /* every other way a source with masm can fail after its first block */
    struct { const char *name; const char *tail; int kind; } f[] = {
        { "a syntax error",                         "y: [ oops",                               R0S1_PARSE_SYNTAX },
        { "a 600-value block",                      NULL,                                      R0S1_PARSE_TOO_LARGE },
        { "a masm block with 65 labels",            NULL,                                      R0S1_PARSE_TOO_LARGE },
        { "a masm block inside a func body, then a syntax error",
                                                    "lx-f: func [] [ k: masm 0 [ LIT 1 ARITY 1 EXIT ] k ] y: [ oops",
                                                                                               R0S1_PARSE_SYNTAX },
    };
    for (int k = 0; k < 4; k++) {
        setup(root);   /* independent of L1 (which, unrepaired, fills the code space) */
        n = snprintf(w, sizeof w, "%s", ok_block);
        if (k == 1) { n += snprintf(w + n, sizeof w - (size_t)n, "b: [ ");
                      for (int i = 0; i < 600; i++) n += snprintf(w + n, sizeof w - (size_t)n, "1 ");
                      snprintf(w + n, sizeof w - (size_t)n, "] 0"); }
        else if (k == 2) { n += snprintf(w + n, sizeof w - (size_t)n, "lx-l: masm 0 [ ");
                      for (int i = 0; i < 65; i++) n += snprintf(w + n, sizeof w - (size_t)n, "L%d: ", i);
                      snprintf(w + n, sizeof w - (size_t)n, "LIT 0 ARITY 1 EXIT ] 0"); }
        else snprintf(w + n, sizeof w - (size_t)n, "%s", f[k].tail);
        load_state_t t = load_state();
        r0_s1_outcome oc; quiet(); sess(w, &oc); loud();
        int kind_ok = oc.status == R0S1_OUT_PARSE_ERROR && r0_s1_parse_error_kind() == f[k].kind;
        char m[256];
        snprintf(m, sizeof m, "L%d: a masm block followed by %s: refused, no code emitted, nothing kept", k + 2, f[k].name);
        CHECK(kind_ok && load_same(load_state(), t), m);
    }

    /* a source that parses: its blocks land where their callables say, with
     * exactly the code the same blocks get when loaded one per source */
    const char *blocks[] = {
        "lx-p: masm 1 [ LIT 16 ADD ARITY 1 EXIT ] ",
        "lx-q: masm 0 [ LIT 1 L1: DUP LIT 5 LT ZBRANCH L2 LIT 1 ADD BRANCH L1 L2: LIT 16 MUL ARITY 1 EXIT ] ",
        "lx-r: masm 2 [ MUL LIT 16 DIV ARITY 1 EXIT ] ",
    };
    setup(root);
    cell h0 = asm_here();
    n = 0;
    for (int i = 0; i < 3; i++) n += snprintf(w + n, sizeof w - (size_t)n, "%s", blocks[i]);
    snprintf(w + n, sizeof w - (size_t)n, "+ + lx-p 1 lx-q lx-r 6 7");
    int multi = sess(w, NULL) == R0S1_OUT_OK && got() == 2 + 5 + 42;
    cell h1 = asm_here(); unsigned long sum_multi = checksum(h0, h1);
    setup(root);
    for (int i = 0; i < 3; i++) { snprintf(w, sizeof w, "%s 0", blocks[i]); sess(w, NULL); }
    int single = asm_here() == h1 && checksum(h0, asm_here()) == sum_multi;
    CHECK(multi, "L6: three masm blocks in one source run correctly (2 + 5 + 42)");
    CHECK(single, "L6: they emit exactly the code the same blocks emit loaded one per source, at the same addresses");
}

/* ---- U: unwrapped sources ---------------------------------------------------
 * A source file may be wrapped in one outer [ ] or not (./glon and WASM
 * glon_load parse files as given). Until 2026-11-10 (K4) masm was recognised
 * only inside a block, so an unwrapped `k: masm 1 [ ... ]` loaded as plain
 * words and fail-stopped when run. */
static int parse_run(const char *text, r0_s1_outcome *oc) {
    static char b[1 << 17];
    snprintf(b, sizeof b, "%s", text);
    int err = 0;
    cell prog = r0_s1_parse(b, &err);
    if (err) { r0_s1_session_parse_error(oc); return oc->status; }
    quiet(); r0_s1_session_run_block(prog, oc); loud();
    N = oc->status == R0S1_OUT_OK ? oc->count : -1;
    return oc->status;
}
/* a library file as text (comments stripped), and the same without its outer [ ] */
static int read_lib(const char *root, const char *rel, char *wrapped, char *unwrapped, size_t cap) {
    char path[4096]; snprintf(path, sizeof path, "%s/%s", root, rel);
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    size_t n = fread(wrapped, 1, cap - 1, f); fclose(f); wrapped[n] = 0;
    char *r = wrapped, *w = wrapped;
    while (*r) { if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; } *w++ = *r++; }
    *w = 0;
    char *open = strchr(wrapped, '['), *close = strrchr(wrapped, ']');
    if (!open || !close || close < open) return -1;
    for (char *p = wrapped; p < open; p++) if (*p != ' ' && *p != '\n' && *p != '\t' && *p != '\r') return -1;
    size_t len = (size_t)(close - open - 1);
    memcpy(unwrapped, open + 1, len); unwrapped[len] = 0;
    return 0;
}
static void section_U(const char *root) {
    printf("== U: unwrapped sources\n");
    r0_s1_outcome oc;
    fresh();
    cell h0 = asm_here();
    int ok = parse_run("k: masm 1 [ DROP INT 7 ARITY 1 EXIT ]  k 5", &oc) == R0S1_OUT_OK && got() == 7;
    cell h1 = asm_here(); unsigned long sum_u = checksum(h0, h1);
    CHECK(ok, "U1: an unwrapped source defines and runs a masm word (k 5 -> 7)");
    fresh();
    parse_run("[ k: masm 1 [ DROP INT 7 ARITY 1 EXIT ]  k 5 ]", &oc);
    CHECK(asm_here() == h1 && checksum(h0, h1) == sum_u, "U1: it emits the same code as the wrapped source");

    /* the shipped masm libraries, loaded unwrapped: the same code */
    static char wrapped[1 << 17], unwrapped[1 << 17];
    const char *libs[] = { "jupyter/prelude.glon", "demo/shop/strings.glon", "demo/shop/strings-ext.glon",
                           "experiments/glon-mem/mem.glon" };
    int same_code = 1, all_ok = 1;
    unsigned long sums[2]; cell ends[2];
    for (int form = 0; form < 2; form++) {
        fresh();
        for (int i = 0; i < 4; i++) {
            if (read_lib(root, libs[i], wrapped, unwrapped, sizeof wrapped)) { all_ok = 0; continue; }
            if (parse_run(form ? unwrapped : wrapped, &oc) != R0S1_OUT_OK) all_ok = 0;
        }
        ends[form] = asm_here(); sums[form] = checksum(256, asm_here());
    }
    if (ends[0] != ends[1] || sums[0] != sums[1]) same_code = 0;
    CHECK(all_ok, "U2: prelude, strings, strings-ext and mem/ load unwrapped");
    CHECK(same_code, "U2: ... emitting exactly the code they emit wrapped");
    CHECK(sess("mm-r: mem/alloc 4  mem/set-byte mm-r 2 99  mem/byte mm-r 2", NULL) == R0S1_OUT_OK && got() == 99,
          "U2: the unwrapped mem/ library works");

    /* refusals are the same unwrapped */
    fresh();
    static char w[1 << 15];
    int n = snprintf(w, sizeof w, "k: masm 0 [ ");
    for (int i = 0; i < 65; i++) n += snprintf(w + n, sizeof w - (size_t)n, "L%d: ", i);
    snprintf(w + n, sizeof w - (size_t)n, "LIT 0 ARITY 1 EXIT ]");
    load_state_t t = load_state();
    CHECK(parse_run(w, &oc) == R0S1_OUT_PARSE_ERROR && r0_s1_parse_error_kind() == R0S1_PARSE_TOO_LARGE
          && load_same(load_state(), t), "U3: an unwrapped block with 65 labels is refused, emitting nothing");
    n = snprintf(w, sizeof w, "a: masm 0 [ LIT 0 ARITY 1 EXIT ] b: masm 0 [ x: ");
    for (int i = 0; i < 250; i++) n += snprintf(w + n, sizeof w - (size_t)n, "CALL x ");
    snprintf(w + n, sizeof w - (size_t)n, "LIT 0 ARITY 1 EXIT ]");
    t = load_state();
    CHECK(parse_run(w, &oc) == R0S1_OUT_PARSE_ERROR && load_same(load_state(), t),
          "U3: an unwrapped source whose second block cannot fit is refused, emitting nothing");
}

/* ---- M: assembler limits ----------------------------------------------------
 * A masm block holds at most 511 tokens, 64 labels and 128 label references,
 * and its code must end at or below R0S1_CODE_LIMIT; anything larger is
 * refused as too_large with nothing emitted (masm_bound covers the code
 * limit; integrity/asan-probe.sh repeats the table limits under
 * AddressSanitizer). */
static int masm_parse(const char *body, int arity, cell *emitted) {
    static char w[1 << 15];
    snprintf(w, sizeof w, "mm-w: masm %d [ %s ] 0", arity, body);
    cell h = asm_here(); r0_s1_outcome oc;
    quiet(); sess(w, &oc); loud();
    *emitted = asm_here() - h;
    return oc.status;
}
static void section_M(void) {
    printf("== M: assembler limits\n");
    static char b[1 << 15];
    cell e; int n;
    /* tokens: 167 x "LIT 0 DROP" (501) + "INT 1 ARITY 1 EXIT" (5), padded with labels */
    for (int total = 511; total <= 512; total++) {
        fresh();
        n = 0;
        int labels = total - 167 * 3 - 5;
        for (int i = 0; i < 167; i++) n += snprintf(b + n, sizeof b - (size_t)n, "LIT 0 DROP ");
        for (int i = 0; i < labels; i++) n += snprintf(b + n, sizeof b - (size_t)n, "P%d: ", i);
        snprintf(b + n, sizeof b - (size_t)n, "INT 1 ARITY 1 EXIT");
        int st = masm_parse(b, 0, &e);
        if (total == 511)
            CHECK(st == R0S1_OUT_OK && e == 167 * 3 + 21 && sess("mm-w", NULL) == R0S1_OUT_OK && got() == 1,
                  "M1: a block of 511 tokens (the maximum) assembles and runs");
        else
            CHECK(st == R0S1_OUT_PARSE_ERROR && r0_s1_parse_error_kind() == R0S1_PARSE_TOO_LARGE && e == 0,
                  "M1: a block of 512 tokens is refused (too_large), emitting nothing");
    }
    struct { int labels, refs, call; int ok; const char *what; } t[] = {
        { 64, 0, 0, 1, "M2: 64 labels assemble" },
        { 65, 0, 0, 0, "M2: 65 labels are refused, emitting nothing" },
        { 0, 128, 0, 1, "M2: 128 BRANCH label references assemble" },
        { 0, 129, 0, 0, "M2: 129 BRANCH label references are refused, emitting nothing" },
        { 0, 128, 1, 1, "M2: 128 CALL label references assemble" },
        { 0, 129, 1, 0, "M2: 129 CALL label references are refused, emitting nothing" },
        { 0, 129, 2, 1, "M2: 129 CALLs of a named runtime entry (collect) are not label references: they assemble" },
    };
    for (int k = 0; k < 7; k++) {
        fresh();
        n = 0;
        /* `labels` labels in all, counting the T: the references jump to */
        for (int i = 0; i + 1 < t[k].labels; i++) n += snprintf(b + n, sizeof b - (size_t)n, "L%d: ", i);
        const char *ref = t[k].call == 0 ? "BRANCH T " : t[k].call == 1 ? "CALL T " : "CALL collect ";
        for (int i = 0; i < t[k].refs; i++) n += snprintf(b + n, sizeof b - (size_t)n, "%s", ref);
        snprintf(b + n, sizeof b - (size_t)n, "T: INT 1 ARITY 1 EXIT");
        int st = masm_parse(b, 0, &e);
        int ok = t[k].ok ? (st == R0S1_OUT_OK && e > 0)
                         : (st == R0S1_OUT_PARSE_ERROR && r0_s1_parse_error_kind() == R0S1_PARSE_TOO_LARGE && e == 0);
        CHECK(ok, t[k].what);
    }
    /* every form's emitted size, against the frozen s1.c expansions (an
     * independent table: LIT/INT/ARITY/HOST ops 2, ZBRANCH 2, BRANCH 5, >R and
     * R> 14, EXIT 17, CALL 21, DUP DROP @ ! 1, a label 0) */
    struct { const char *form; long cells; } f[] = {
        { "LIT 7", 2 }, { "INT 7", 2 }, { "ARITY 1", 2 }, { "HOST 0", 2 }, { "NONE", 2 },
        { "ADD", 2 }, { "SUB", 2 }, { "MUL", 2 }, { "DIV", 2 }, { "MOD", 2 }, { "EQ", 2 }, { "NE", 2 },
        { "LT", 2 }, { "GT", 2 }, { "LE", 2 }, { "GE", 2 }, { "PRINT", 2 }, { "ALLOC", 2 },
        { "ZBRANCH X", 2 }, { "ZBRANCH 300", 2 }, { "BRANCH X", 5 }, { "BRANCH 300", 5 },
        { ">R", 14 }, { "R>", 14 }, { "EXIT", 17 }, { "CALL X", 21 }, { "CALL 300", 21 }, { "CALL alloc", 21 },
        { "CALL collect", 21 }, { "CALL lookup", 21 }, { "CALL ESC_ANY", 21 }, { "CALL ESC_TRANSPORT", 21 },
        { "DUP", 1 }, { "DROP", 1 }, { "@", 1 }, { "!", 1 }, { "Y:", 0 },
    };
    int sizes_ok = 1; long sum = 0;
    n = 0;
    for (unsigned k = 0; k < sizeof f / sizeof f[0]; k++) {
        fresh();
        /* every block must end in a transfer: a fixed 21-cell tail */
        snprintf(b, sizeof b, "X: %s INT 0 ARITY 1 EXIT", f[k].form);
        if (masm_parse(b, 0, &e) != R0S1_OUT_OK || e - 21 != f[k].cells) {
            sizes_ok = 0; printf("    M3: '%s' emitted %ld, expected %ld\n", f[k].form, (long)e - 21, f[k].cells);
        }
        sum += f[k].cells;
    }
    CHECK(sizes_ok, "M3: every masm form emits exactly its frozen S1 expansion size (37 forms)");
    fresh();
    n = snprintf(b, sizeof b, "X: ");
    for (unsigned k = 0; k < sizeof f / sizeof f[0]; k++)
        if (strcmp(f[k].form, "Y:")) n += snprintf(b + n, sizeof b - (size_t)n, "%s ", f[k].form);
    snprintf(b + n, sizeof b - (size_t)n, "INT 0 ARITY 1 EXIT");
    CHECK(masm_parse(b, 0, &e) == R0S1_OUT_OK && e - 21 == sum, "M3: and all of them together in one block emit the sum");
}

/* ---- V: masm well-formedness (K6; repaired 2026-11-10) ----------------------
 * Until the repair, invalid masm was accepted with a warning on stderr:
 * undefined labels were patched as address 0, unresolved operands, unknown
 * mnemonics and operand-less instructions were dropped, duplicate labels and
 * out-of-range HOST ids accepted. Now each is refused as a syntax error
 * before anything is emitted, recorded, bound or kept. No refused block is
 * ever run here. */
static void section_V(const char *root) {
    printf("== V: masm well-formedness\n");
    struct { const char *body; const char *what; } bad[] = {
        { "BRANCH nowhere INT 1 ARITY 1 EXIT", "an undefined label (BRANCH)" },
        { "INT 0 ZBRANCH nowhere INT 1 ARITY 1 EXIT", "an undefined label (ZBRANCH)" },
        { "CALL nowhere INT 1 ARITY 1 EXIT",   "an undefined label (CALL)" },
        { "LIT nosuch INT 1 ARITY 1 EXIT",     "an unresolved operand (LIT)" },
        { "HOST nosuch INT 1 ARITY 1 EXIT",    "an unresolved operand (HOST)" },
        { "FROB INT 1 ARITY 1 EXIT",           "an unknown mnemonic" },
        { "lit 1 ARITY 1 EXIT",                "a mnemonic in the wrong case" },
        { "L: L: INT 1 ARITY 1 EXIT",          "a label defined twice" },
        { "HOST 99 INT 1 ARITY 1 EXIT",        "a HOST id outside S1's closed set (99)" },
        { "HOST -1 INT 1 ARITY 1 EXIT",        "a negative HOST id" },
        { "INT 1 ARITY 1 EXIT LIT",            "an instruction without its operand at the end" },
        { "INT 1 ARITY -1 EXIT",               "a negative ARITY" },
        { "5 INT 1 ARITY 1 EXIT",              "a bare number in instruction position" },
        { "\"text\" INT 1 ARITY 1 EXIT",     "a string in instruction position" },
        { "[ x ] INT 1 ARITY 1 EXIT",          "a nested block in instruction position" },
        { "BRANCH L: INT 1 ARITY 1 EXIT",      "a branch whose operand is a label definition" },
    };
    int nbad = (int)(sizeof bad / sizeof bad[0]);
    setup(root);
    for (int k = 0; k < nbad; k++) {
        load_state_t t = load_state();
        cell e; int st = masm_parse(bad[k].body, 0, &e);
        char m[256];
        snprintf(m, sizeof m, "V%d: a masm block with %s is refused as a syntax error, emitting, binding and keeping nothing",
                 k + 1, bad[k].what);
        CHECK(st == R0S1_OUT_PARSE_ERROR && r0_s1_parse_error_kind() == R0S1_PARSE_SYNTAX
              && e == 0 && load_same(load_state(), t), m);
    }
    CHECK(sess("ix/ok 6 7", NULL) == R0S1_OUT_OK && got() == 42
          && sess("v-ok: masm 0 [ INT 9 ARITY 1 EXIT ] v-ok", NULL) == R0S1_OUT_OK && got() == 9,
          "V: after all the refusals, Glon and a new masm word run (42, 9)");

    /* valid forms the rules must keep accepting */
    struct { const char *body; int arity; const char *call; long want; const char *what; } good[] = {
        { "INT 5 ARITY 1 EXIT", 0, "vg", 5, "a block ending in EXIT" },
        { "INT 7 ARITY 1 EXIT L: INT 0 BRANCH L", 0, "vg", 7, "code after EXIT ending in BRANCH (unreachable)" },
        { "LIT 1 L1: DUP LIT 5 LT ZBRANCH L2 LIT 1 ADD BRANCH L1 L2: LIT 16 MUL ARITY 1 EXIT", 0, "vg", 5,
          "labels, a loop and forward and backward branches" },
        { "DROP CALL collect INT 3 ARITY 1 EXIT", 1, "vg 0", 3, "CALL of a named runtime entry" },
        { "DUP LIT 0 ADD DROP INT 4 ARITY 1 EXIT", 1, "vg 1", 4, "HOST forms and stack words" },
        { "INT 6 ARITY 1 EXIT", 0, "vg", 6, "ARITY 1 EXIT with an INT result" },
    };
    for (unsigned k = 0; k < sizeof good / sizeof good[0]; k++) {
        static char w[1024];
        fresh();
        snprintf(w, sizeof w, "vg: masm %d [ %s ] %s", good[k].arity, good[k].body, good[k].call);
        char m[256]; snprintf(m, sizeof m, "V: still accepted and correct: %s (%ld)", good[k].what, good[k].want);
        CHECK(sess(w, NULL) == R0S1_OUT_OK && got() == good[k].want, m);
    }
    /* an indirect jump as the last instruction (the M1 scheduler idiom) and a
     * numeric branch target (an absolute address) assemble; not run */
    fresh();
    cell e;
    CHECK(masm_parse("LIT SCRATCH_A @ LIT REG_IP !", 0, &e) == R0S1_OUT_OK,
          "V: still accepted: a block ending in an indirect jump (LIT REG_IP !)");
    CHECK(masm_parse("INT 0 ZBRANCH 300 INT 1 ARITY 1 EXIT", 0, &e) == R0S1_OUT_OK,
          "V: still accepted: a numeric (absolute) branch target");
    /* the libraries that contain masm all still load (also the scan of every
     * masm block in the repository, MACHINE-INTEGRITY-AUDIT.md) */
    CHECK(setup(root) == 0, "V: prelude, strings and strings-ext still load");
}

int main(int argc, char **argv) {
    const char *root = ".";
    for (int i = 1; i < argc; i++) { if (!strcmp(argv[i], "-v")) verbose = 1; else root = argv[i]; }
    build_nest(deep_nest, sizeof deep_nest, 3600);
    {   int n = snprintf(too_large, sizeof too_large, "b: [ ");
        for (int i = 0; i < 600; i++) n += snprintf(too_large + n, sizeof too_large - n, "1 ");
        snprintf(too_large + n, sizeof too_large - n, "] 0"); }

    /* ---- R ---------------------------------------------------------------- */
    printf("== R: boundary regressions (re-run)\n");
    failures += run_r0_s1_gc_interior_tests();
    failures += run_r0_s1_masm_bound_tests();

    /* ---- A: determinism and reset ------------------------------------------ */
    printf("== A: determinism and reset\n");
    static cell snap0[MEM_CELLS], snap1[MEM_CELLS];
    fresh(); memcpy(snap0, M, sizeof snap0);
    fresh();
    CHECK(memcmp(snap0, M, sizeof snap0) == 0, "A1: two fresh initialisations give identical memory (all 64K cells)");
    const char *work = "c: ix/mk 10  c c  t: ix/dbl \"ab\" 6  s: s/+ \"x\" s/from-int 12345  ix/gc 0  judge [ raise create-sin 'ix 'w 1 ] 0";
    int a2 = setup(root) == 0 && sess(work, NULL) == R0S1_OUT_OK;
    memcpy(snap1, M, sizeof snap1);
    a2 = a2 && setup(root) == 0 && sess(work, NULL) == R0S1_OUT_OK;
    CHECK(a2 && memcmp(snap1, M, sizeof snap1) == 0,
          "A2: the same libraries and workload (closures, strings, a collection, a caught SIN!) in two sessions give identical memory");
    /* r0_s1_init is NOT a memory clear: dead cells (below SP and RP, above
     * HP, above the loader heap pointer, unused slack) keep what the previous
     * session left. The invariant is that nothing reads them: a session on
     * any prior memory behaves, and ends, exactly as on zeroed memory. */
    {
        unsigned long h[4];
        const char *fills[] = { "zeroed", "poisoned", "pointer-like integers", "a previous session's residue" };
        for (int f = 0; f < 4; f++) {
            if (f == 0) memset(M, 0, sizeof(cell) * MEM_CELLS);
            if (f == 1) for (cell c = 0; c < MEM_CELLS; c++) M[c] = (cell)(0x5A5A5A5 + c * 7);
            if (f == 2) for (cell c = 0; c < MEM_CELLS; c++) M[c] = (cell)16 * (2048 + c % 452);
            if (f == 3) { setup(root); quiet(); sess(work, NULL); sess("ix/inf 0", NULL); sess("ix/dbl \"abcdefgh\" 12", NULL); loud(); }
            unsigned long x = 1469598103UL;
            if (setup(root) != 0) x = 0;
            const char *w[] = { work, "c c", "+ t 0", "s/at s 3", "ix/ok 6 7",
                                "reduce [ 1 + 2 3 ]", "f: func [n] [ either < n 2 [ n ] [ + f - n 1 f - n 2 ] ] f 15" };
            for (int i = 0; i < 7; i++) {
                r0_s1_outcome oc; char out[256] = "";
                quiet(); sess(w[i], &oc); loud();
                if (oc.status == R0S1_OUT_OK) r0_s1_mold(r0_s1_result(oc.count - 1, oc.count), out, sizeof out);
                x = (x ^ (unsigned long)oc.status) * 1099511628211UL;
                for (char *p = out; *p; p++) x = (x ^ (unsigned char)*p) * 1099511628211UL;
                if (verbose) printf("    %s: %s -> %d %s\n", fills[f], w[i], oc.status, out);
            }
            state_t s = state();
            x = (x ^ s.code) * 1099511628211UL;
            x = (x ^ (unsigned long)(s.here + 3 * s.loader_hp + 5 * s.globals + 7 * s.live_cells + 11 * s.live_objs)) * 1099511628211UL;
            h[f] = x;
        }
        CHECK(h[0] && h[0] == h[1] && h[0] == h[2] && h[0] == h[3],
              "A3: a session on poisoned, pointer-like or previously used memory gives the same results and ends in the same state as on zeroed memory");
    }
    fresh();
    CHECK(memcmp(snap0 + 256, M + 256, sizeof(cell) * (size_t)r0_s1_code_size()) == 0,
          "A4: re-initialisation after all of that emits the same runtime code");

    /* ---- B: every failure kind, repeated, returns to the same state -------- */
    printf("== B: recovery to a fixed state after repeated failures\n");
    CHECK(setup(root) == 0, "B0: libraries and helpers load");
    CHECK(sess("ix/ok 6 7", NULL) == R0S1_OUT_OK && got() == 42, "B0: the probe program runs (6 * 7)");
    unsigned long rt0 = checksum(256, runtime_code_end());
    for (int k = 0; k < NFAILS; k++) {
        /* a fresh session per kind (the loader heap keeps every parsed cell);
         * the first run is a warm-up: it may intern symbols, which persist */
        setup(root);
        quiet(); sess(fails[k].prog, NULL); loud();
        state_t base = state(); show(fails[k].name, base);
        int status_ok = 1, after_ok = 1, state_ok = 1, sentry = 0, bounded = 1, lhp_steady = 1;
        int last = -2; cell delta0 = -1;
        for (int round = 0; round < 10; round++) {
            r0_s1_outcome oc;
            cell lhp = M[GC_LOADER_HP];
            quiet(); last = sess(fails[k].prog, &oc); loud();
            if (fails[k].want >= 0 ? last != fails[k].want : last == R0S1_OUT_OK) status_ok = 0;
            if (oc.detail == R0S1_DETAIL_STACK_SENTRY) sentry++;
            if (r0_s1_heap_high() > 40000) bounded = 0;
            if (sess("ix/ok 6 7", NULL) != R0S1_OUT_OK || got() != 42) after_ok = 0;
            if (!same(state(), base)) state_ok = 0;
            cell delta = M[GC_LOADER_HP] - lhp;
            if (round == 0) delta0 = delta; else if (delta != delta0) lhp_steady = 0;
        }
        if (verbose) printf("    loader heap per round: %ld cells\n", (long)delta0);
        char m[256];
        snprintf(m, sizeof m, "B%d: %s x10: classified as expected (%s)", k + 1, fails[k].name,
                 r0_s1_outcome_status_name(last));
        CHECK(status_ok, m);
        snprintf(m, sizeof m, "B%d: %s x10: the next run is correct every time", k + 1, fails[k].name);
        CHECK(after_ok, m);
        snprintf(m, sizeof m, "B%d: %s x10: code end, code, globals and the live heap after a collection return to the baseline",
                 k + 1, fails[k].name);
        CHECK(state_ok, m);
        snprintf(m, sizeof m, "B%d: %s x10: the loader heap grows by the same amount every round (no hidden retention)",
                 k + 1, fails[k].name);
        CHECK(lhp_steady, m);
        snprintf(m, sizeof m, "B%d: %s x10: heap frontier stays below the loader heap; no stack-sentry report",
                 k + 1, fails[k].name);
        CHECK(bounded && sentry == 0, m);
    }

    /* ---- C: the runtime's code is never written ---------------------------- */
    printf("== C: runtime code immutability\n");
    CHECK(checksum(256, runtime_code_end()) == rt0,
          "C1: the runtime's own code [256, code end) is unchanged by section B (every failure kind, repeated)");
    fresh();
    CHECK(memcmp(snap0 + 256, M + 256, sizeof(cell) * (size_t)r0_s1_code_size()) == 0,
          "C2: and equals the code a fresh initialisation emits");

    section_S(root);
    section_L(root);
    section_U(root);
    section_M();
    section_V(root);

    /* ---- K: known defects (strict XFAIL) ----------------------------------- */
    printf("== K: known defects (expected failures; XPASS means fixed: promote)\n");
    int clean;
    /* K1: a masm word pushes 400 values and pops them again. masm runs raw S1
     * instructions between evaluation steps, and frozen S1 executes LIT, DUP,
     * @ and HOST pushes with no bound, so neither the data-stack guard (at
     * each evaluation step) nor the post-run sentry (final SP and RV_SPMIN,
     * sampled at closure entry) can see a push-and-pop inside one masm word.
     * With masm libraries filling code to the limit, the pushes overwrite
     * them. Preventing it needs a bound in S1 itself (MACHINE-INTEGRITY-AUDIT.md,
     * "K1"): this stays an expected failure until that change is approved. */
    {
        setup(root);
        static char w[4096];
        snprintf(w, sizeof w,
            "ix/exc: masm 0 [ LIT 400 LIT SCRATCH_A ! "
            "  push: LIT 7 LIT SCRATCH_A @ LIT 1 SUB DUP LIT SCRATCH_A ! ZBRANCH pd BRANCH push "
            "  pd: LIT 400 LIT SCRATCH_A ! "
            "  pop: DROP LIT SCRATCH_A @ LIT 1 SUB DUP LIT SCRATCH_A ! ZBRANCH od BRANCH pop "
            "  od: LIT 0 ARITY 1 EXIT ] 0");
        int ok = sess(w, NULL) == R0S1_OUT_OK && fill_to(R0S1_CODE_LIMIT) == 0;
        CHECK(ok && asm_here() == R0S1_CODE_LIMIT, "K1 setup: code filled to R0S1_CODE_LIMIT with masm words");
        int bad = clean_but_code_changed("ix/exc", &clean);
        XFAIL(!bad, "K1: a masm word that pushes the data stack 400 deep into loaded code is stopped or not reported clean (needs a frozen-S1 change)");
    }
    /* K7: an empty masm block emits nothing, and its callable's entry is the
     * end of code: calling it runs whatever follows. Checked without calling it. */
    {
        fresh();
        cell e; int st = masm_parse("", 0, &e);
        XFAIL(st == R0S1_OUT_PARSE_ERROR && e == 0,
              "K7: an empty masm block is refused (today: its callable enters the end of code)");
    }

    printf("\nmachine-integrity: %d failure(s), %d expected failure(s), %d unexpected pass(es)\n",
           failures, xfails, xpasses);
    return (failures || xpasses) ? 1 : 0;
}
