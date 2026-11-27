#define _POSIX_C_SOURCE 200809L
/* GLON-MEM-5: the collector with the mark-aware closure check against the
 * old kind-only check, on a closure-heavy workload.
 *
 *   mem5_closure_diff_<profile>[_walk]
 *
 * The _walk build is compiled with -DR0S1_GC_CLOSURE_WALK_ALWAYS: the
 * collector exactly as before GLON-MEM-5.  The same deterministic workload
 * runs in both:
 *   - 100 top-level functions (so the global context is retraced and its
 *     closures met already marked, as in the applications);
 *   - a table of closures, each step one of: a new closure (c/mkc x: a closure
 *     capturing a managed context); a wrapper around the closure in another
 *     slot (c/wrap: a closure whose only reference to the inner one is its own
 *     captured context -- nested, up to 4 deep); a copy of another slot (a
 *     shared reference); or a data block of garbage;
 *   - exhaustion: a chain of closures grown until the run fails;
 *   - recovery: the chain dropped, the steps resumed.
 * After every chunk: the heap layout (every block's address, size and flags)
 * hashed from the headers, the frontier, the collection count, the last
 * collection's live objects and cells, and a checksum of every closure in the
 * table invoked (the retained values; it also allocates, identically in both
 * builds).  The two builds must print the same lines. */
#include "r0_s1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int run(const char *src) {
    int perr = 0;
    cell prog = r0_s1_parse(src, &perr);
    if (perr) { fprintf(stderr, "parse error %d in: %.60s\n", r0_s1_parse_error_kind(), src); return -2; }
    int n = r0_s1_run_persistent(prog);
    return (r0_s1_ran_cleanly() && n >= 0) ? 0 : -1;
}
static long run_int(const char *src, int *rc) {
    int perr = 0;
    cell prog = r0_s1_parse(src, &perr);
    if (perr) { *rc = -2; return 0; }
    int n = r0_s1_run_persistent(prog);
    *rc = (r0_s1_ran_cleanly() && n >= 1) ? 0 : -1;
    return *rc ? 0 : (long)int_val(r0_s1_result(n - 1, n));
}
static unsigned long long layout(long *blocks, long *free_blocks) {
    unsigned long long hsh = 1469598103934665603ULL;
    long b = 0, f = 0;
    for (cell a = GC_HEAP_BASE; a < M[REG_HP]; ) {
        cell size = M[a + GC_HDR_SIZE], flags = M[a + GC_HDR_FLAGS];
        if (size <= 0) { hsh ^= 0xdeadULL; break; }
        cell v[3] = { a, size, flags };
        for (int k = 0; k < 3; k++) { hsh ^= (unsigned long long)v[k]; hsh *= 1099511628211ULL; }
        b++; if (flags == 0) f++;
        a += size;
    }
    *blocks = b; *free_blocks = f;
    return hsh;
}
static int checksum_ok = 1;
static void report(const char *phase, int chunk, int rc, int with_sum) {
    long b, f;
    unsigned long long hsh = layout(&b, &f);
    long sum = 0; int src = 0;
    if (with_sum) { sum = run_int("[ c/acc: 0  c/for :c/sum 0 0 c/n  c/acc ]", &src); if (src) checksum_ok = 0; }
    printf("%-8s %3d rc %2d  HP %8ld  GCs %4ld  live %6ld objs %8ld cells  blocks %6ld free %5ld  layout %016llx  sum %ld%s\n",
           phase, chunk, rc, (long)(M[REG_HP] - GC_HEAP_BASE), r0_s1_gc_count(), (long)M[GC_LIVE_OBJS],
           (long)M[GC_LIVE_CELLS], b, f, hsh, sum, with_sum && src ? " (checksum run failed)" : "");
}
int main(void) {
    r0_s1_init();
    long managed = (long)(GC_HEAP_LIMIT - GC_HEAP_BASE);
    int nslots = managed > 100000 ? 64 : 12;
    int steps = managed > 100000 ? 2000 : 150;
    if (run("[ set-block: masm 3 [ LIT SCRATCH_C ! LIT SCRATCH_B ! DUP LIT 16 MOD SUB LIT SCRATCH_A ! "
            "LIT SCRATCH_C @ LIT SCRATCH_A @ LIT 2 ADD LIT SCRATCH_B @ LIT 16 DIV ADD ! ARITY 0 EXIT ] "
            "  bat: masm 2 [ LIT SCRATCH_B ! DUP LIT 16 MOD SUB LIT 2 ADD LIT SCRATCH_B @ LIT 16 DIV ADD @ ARITY 1 EXIT ] ]") != 0) {
        printf("MEM5_CLOSURE_DIFF FAIL (block helpers)\n"); return 1;
    }
    /* 100 top-level functions, in sources of 50 */
    for (int lo = 0; lo < 100; lo += 50) {
        static char src[8192]; int k = sprintf(src, "[");
        for (int i = lo; i < lo + 50; i++) k += sprintf(src + k, " g%d: func [x] [ + x %d ]", i, i);
        sprintf(src + k, " ]");
        if (run(src) != 0) { printf("MEM5_CLOSURE_DIFF FAIL (functions)\n"); return 1; }
    }
    static char lib[4096];
    char zeros[512] = ""; for (int i = 0; i < nslots; i++) strcat(zeros, " 0");
    snprintf(lib, sizeof lib,
        "[ c/for: func [f ctx lo hi] [ either > - hi lo 8 [ m: / + lo hi 2  c/for :f ctx lo m  c/for :f ctx m hi ] [ c/for8 :f ctx lo hi ] ]"
        "  c/for8: func [f ctx i hi] [ either < i hi [ f ctx i  c/for8 :f ctx + i 1 hi ] [ 0 ] ]"
        "  c/seed: 4242"
        "  c/rand: func [n] [ c/seed: - + * c/seed 75 74 * / + * c/seed 75 74 65537 65537  - c/seed * / c/seed n n ]"
        "  c/n: %d"
        "  c/slots: reduce [%s ]"
        "  c/depth: reduce [%s ]"
        "  c/junk: [ 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 ]"
        "  c/mkc: func [x] [ func [y] [ + x y ] ]"
        "  c/wrap: func [g x] [ func [y] [ + invoke :g y x ] ]"
        "  c/init: func [ctx i] [ set-block c/slots i c/mkc i  set-block c/depth i 1 ]"
        "  c/new: func [k] [ set-block c/slots k c/mkc c/rand 1000  set-block c/depth k 1 ]"
        "  c/step: func [ctx i] [ k: c/rand c/n  j: c/rand c/n  r: c/rand 4 "
        "    either = r 0 [ c/new k ] [ either = r 1 [ "
        "      either < bat c/depth j 4 [ set-block c/slots k c/wrap bat c/slots j c/rand 100  set-block c/depth k + bat c/depth j 1 ] [ c/new k ] ] [ "
        "      either = r 2 [ set-block c/slots k bat c/slots j  set-block c/depth k bat c/depth j ] [ reduce c/junk ] ] ] ]"
        "  c/acc: 0"
        "  c/sum: func [ctx i] [ c/acc: + c/acc invoke bat c/slots i i ]"
        "  c/chain: 0"
        "  c/grow: func [ctx i] [ c/chain: reduce [ c/chain c/mkc i ] ] ]",
        nslots, zeros, zeros);
    if (run(lib) != 0) { printf("MEM5_CLOSURE_DIFF FAIL (setup)\n"); return 1; }
    char src[128];
    snprintf(src, sizeof src, "[ c/for :c/init 0 0 c/n ]");
    report("init", 0, run(src), 1);
    for (int c = 1; c <= 8; c++) {
        snprintf(src, sizeof src, "[ c/for :c/step 0 0 %d ]", steps);
        report("churn", c, run(src), 1);
    }
    int rc = 0, c;
    for (c = 1; c <= 400 && rc == 0; c++) {
        snprintf(src, sizeof src, "[ c/for :c/grow 0 0 %d ]", steps / 4);
        rc = run(src);
        if (rc != 0 || c % 25 == 0) report("exhaust", c, rc, 0);
    }
    printf("exhaustion: %s after %d chunks (halt reason %d)\n", rc ? "failed" : "did not fail", c - 1, r0_s1_halt_reason());
    report("recover", 0, run("[ c/chain: 0 ]"), 1);
    for (int k = 1; k <= 3; k++) {
        snprintf(src, sizeof src, "[ c/for :c/step 0 0 %d ]", steps);
        report("recover", k, run(src), 1);
    }
    printf("MEM5_CLOSURE_DIFF %s\n", checksum_ok ? "DONE" : "FAIL (a checksum run failed)");
    return checksum_ok ? 0 : 1;
}
