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
                "0", NULL) == R0S1_OUT_OK ? 0 : -1;
}

/* ---- the failure kinds of B ---------------------------------------------- */
typedef struct { const char *name, *prog; int want; } fail_t;
static char too_large[4096];
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

int main(int argc, char **argv) {
    const char *root = ".";
    for (int i = 1; i < argc; i++) { if (!strcmp(argv[i], "-v")) verbose = 1; else root = argv[i]; }
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

    /* ---- K: known defects (strict XFAIL) ----------------------------------- */
    printf("== K: known defects (expected failures; XPASS means fixed: promote)\n");
    int clean;
    /* K1: a masm word pushes 400 values and pops them again with no closure
     * entry in between, so RV_SPMIN never sees the depth; with masm libraries
     * filling code to the limit the pushes overwrite them. */
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
        XFAIL(!bad, "K1: a run that pushes the data stack 400 deep into loaded code (masm, no closure entry) is not reported clean");
    }
    /* K2: the same with no masm at all: `+ 1` nested 3,600 deep across 36
     * `either` blocks (each block within the 512-value limit). Each pending `+`
     * holds a cell on the data stack and no closure is entered, so the run
     * reaches the runtime's own code and is reported clean. */
    {
        fresh();
        static char w[1 << 16];
        int n = 0;
        for (int l = 0; l < 36; l++) {
            for (int i = 0; i < 100; i++) n += snprintf(w + n, sizeof w - n, "+ 1 ");
            n += snprintf(w + n, sizeof w - n, "either 1 [ ");
        }
        n += snprintf(w + n, sizeof w - n, "1 ");
        for (int l = 0; l < 36; l++) n += snprintf(w + n, sizeof w - n, "] [ 0 ] ");
        unsigned long rt = checksum(256, runtime_code_end());
        int bad = clean_but_code_changed(w, &clean);
        if (verbose) printf("    K2: clean=%d runtime code changed=%d\n", clean, checksum(256, runtime_code_end()) != rt);
        XFAIL(!bad, "K2: ordinary Glon nesting 3,600 deep (no masm) that overwrites the runtime's code is not reported clean");
        fresh();   /* the overwritten session is discarded, not run further */
        static char v[1 << 16];
        n = 0;
        for (int l = 0; l < 2; l++) {
            for (int i = 0; i < 100; i++) n += snprintf(v + n, sizeof v - n, "+ 1 ");
            n += snprintf(v + n, sizeof v - n, "either 1 [ ");
        }
        n += snprintf(v + n, sizeof v - n, "1 ");
        for (int l = 0; l < 2; l++) n += snprintf(v + n, sizeof v - n, "] [ 0 ] ");
        CHECK(sess(v, NULL) == R0S1_OUT_OK && got() == 201, "K2 control: the same form 200 deep computes 201 and is correct");
    }
    /* K3: a source whose first masm block fits and whose second does not is
     * refused, but the first block's code stays; repeated, the refusals use up
     * the whole code space. */
    {
        fresh();
        static char w[8192];
        int n = snprintf(w, sizeof w, "a: masm 0 [ LIT 0 ARITY 1 EXIT ] b: masm 0 [ x: ");
        for (int i = 0; i < 250; i++) n += snprintf(w + n, sizeof w - n, "CALL x ");
        snprintf(w + n, sizeof w - n, "LIT 0 ARITY 1 EXIT ] 0");
        cell h0 = asm_here(); cell g0, g1, cap; r0_s1_global_usage(&g0, &cap);
        int refused = 1; quiet();
        for (int i = 0; i < 200; i++) if (sess(w, NULL) != R0S1_OUT_PARSE_ERROR) refused = 0;
        loud();
        r0_s1_global_usage(&g1, &cap);
        CHECK(refused && g1 == g0, "K3: 200 such sources are each refused (too_large) and bind nothing");
        if (verbose) printf("    K3: code %ld -> %ld (limit %ld)\n", (long)h0, (long)asm_here(), (long)R0S1_CODE_LIMIT);
        XFAIL(asm_here() == h0, "K3: refused sources leave the code end where it was (today they leak 21 cells each, to exhaustion)");
        CHECK(asm_here() <= R0S1_CODE_LIMIT, "K3: the leak never passes R0S1_CODE_LIMIT");
        quiet(); int st = sess("ok: masm 0 [ LIT 5 ARITY 1 EXIT ] ok", NULL); loud();
        if (verbose) printf("    K3: a 21-cell word afterwards: status %d\n", st);
        CHECK(sess("ix-f: func [x] [ + x 1 ] ix-f 41", NULL) == R0S1_OUT_OK && got() == 42,
              "K3: plain Glon still runs after the code space is exhausted");
    }
    /* K4: `masm` is recognised only inside a block (parse_block); a source
     * that is not wrapped in [ ] (the glon CLI parses files as given) loads
     * `k: masm 1 [ ... ]` as plain words and fail-stops when it runs. */
    {
        fresh();
        strcpy(src, "k: masm 1 [ DROP LIT 0 ARITY 1 EXIT ] k 5");
        int err = 0; cell b = r0_s1_parse(src, &err);
        r0_s1_outcome oc;
        quiet(); if (!err) r0_s1_session_run_block(b, &oc); loud();
        int unwrapped_ok = !err && oc.status == R0S1_OUT_OK;
        strcpy(src, "[ k: masm 1 [ DROP LIT 0 ARITY 1 EXIT ] k 5 ]");
        b = r0_s1_parse(src, &err);
        int wrapped_ok = !err && r0_s1_session_run_block(b, &oc) == R0S1_OUT_OK;
        CHECK(wrapped_ok, "K4 control: the wrapped source defines and runs the masm word");
        XFAIL(unwrapped_ok, "K4: an unwrapped source defines a masm word as the wrapped one does (today: machine fail-stop)");
    }

    printf("\nmachine-integrity: %d failure(s), %d expected failure(s), %d unexpected pass(es)\n",
           failures, xfails, xpasses);
    return (failures || xpasses) ? 1 : 0;
}
