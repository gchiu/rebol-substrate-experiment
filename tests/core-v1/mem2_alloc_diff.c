#define _POSIX_C_SOURCE 200809L
/* GLON-MEM-2: allocation decisions with the free-block hint against the old
 * scan from GC_HEAP_BASE.
 *
 *   mem2_alloc_diff_<profile>[_base]
 *
 * The same deterministic workload runs in both builds (the _base build is
 * compiled with -DR0S1_ALLOC_SCAN_FROM_BASE: the scan exactly as before):
 *   1. churn: a 64-slot table of live blocks; each step puts a new block of a
 *      pseudo-random size (1, 20, 40, 60, 150 or 400 values: 32 to 432
 *      cells; the last two added by GLON-MEM-3) in a
 *      pseudo-random slot, so the old one becomes garbage -- collections,
 *      reuse of freed blocks and fragmentation by mixed sizes;
 *   2. exhaustion: a chain kept growing until the run fails (out of memory);
 *   3. recovery: the chain dropped, the churn resumed (a collection frees it).
 * After every chunk the heap is walked from its headers and its layout --
 * every block's address, size and flags -- hashed, with the frontier and the
 * collection count.  First-fit is deterministic, so the two builds must print
 * the same lines.  The hint build also checks its invariant at every chunk:
 * no free block below GC_FREE_HINT, and GC_FREE_HINT <= REG_HP.
 */
#include "r0_s1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int run(const char *src) {
    int perr = 0;
    cell prog = r0_s1_parse(src, &perr);
    if (perr) { fprintf(stderr, "parse error %d in: %.60s\n", r0_s1_parse_error_kind(), src); return -2; }
    int n = r0_s1_run_persistent(prog);
    return (r0_s1_ran_cleanly() && n >= 0) ? 0 : -1;
}
/* the layout's hash; blocks, free blocks; the invariant (hint build only) */
static unsigned long long layout(long *blocks, long *free_blocks, int *inv_ok) {
    unsigned long long hsh = 1469598103934665603ULL;
    long b = 0, f = 0;
    cell first_free = 0;
    for (cell a = GC_HEAP_BASE; a < M[REG_HP]; ) {
        cell size = M[a + GC_HDR_SIZE], flags = M[a + GC_HDR_FLAGS];
        if (size <= 0) { hsh ^= 0xdeadULL; break; }
        cell v[3] = { a, size, flags };
        for (int k = 0; k < 3; k++) { hsh ^= (unsigned long long)v[k]; hsh *= 1099511628211ULL; }
        b++;
        if (flags == 0) { f++; if (!first_free) first_free = a; }
        a += size;
    }
    *blocks = b; *free_blocks = f;
#ifndef R0S1_ALLOC_SCAN_FROM_BASE
    cell hint = M[GC_FREE_HINT];
    int ok = hint <= M[REG_HP] && (hint < GC_HEAP_BASE || !first_free || first_free >= hint);
    /* (a hint below the base is the "scan from the base" fallback: valid) */
    *inv_ok = ok;
#else
    *inv_ok = 1;
#endif
    return hsh;
}
static void report(const char *phase, int chunk, int rc) {
    long b, f; int ok;
    unsigned long long hsh = layout(&b, &f, &ok);
    printf("%-10s %3d rc %2d  HP %8ld  GCs %4ld  blocks %6ld free %5ld  layout %016llx\n", phase, chunk, rc,
           (long)(M[REG_HP] - GC_HEAP_BASE), r0_s1_gc_count(), b, f, hsh);
    if (!ok) { fprintf(stderr, "  INVARIANT BROKEN at %s %d: hint %ld HP %ld\n", phase, chunk, (long)M[GC_FREE_HINT], (long)M[REG_HP]); failures++; }
}

int main(void) {
    r0_s1_init();
    const char *lib =
        "[ w/for: func [f ctx lo hi] [ either > - hi lo 8 [ m: / + lo hi 2  w/for :f ctx lo m  w/for :f ctx m hi ] [ w/for8 :f ctx lo hi ] ]"
        "  w/for8: func [f ctx i hi] [ either < i hi [ f ctx i  w/for8 :f ctx + i 1 hi ] [ 0 ] ]"
        "  w/seed: 12345"
        "  w/rand: func [n] [ w/seed: - + * w/seed 75 74 * / + * w/seed 75 74 65537 65537  - w/seed * / w/seed n n ]"
        "  w/t0: [ 0 ]"
        "  w/t1: [ 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 ]"
        "  w/t2: [ 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 ]"
        "  w/t3: [ 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0"
        "          0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 ]"
        "  w/slots: reduce [ 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0"
        "                    0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 ]"
        "  w/make: func [s] [ either = s 0 [ reduce w/t0 ] [ either = s 1 [ reduce w/t1 ] [ either = s 2 [ reduce w/t2 ] [ either = s 3 [ reduce w/t3 ] [ w/large s ] ] ] ] ]"
        "  w/step: func [ctx i] [ k: w/rand 64  set-block w/slots k w/make w/rand w/sizes ]"
        "  w/chain: 0"
        "  w/grow: func [ctx i] [ w/chain: reduce [ w/chain w/make 3 ] ] ]";
    /* the block store view_lib.glon defines (vid/set-block!), here under its own name */
    if (run("[ set-block: masm 3 [ LIT SCRATCH_C ! LIT SCRATCH_B ! DUP LIT 16 MOD SUB LIT SCRATCH_A ! "
            "LIT SCRATCH_C @ LIT SCRATCH_A @ LIT 2 ADD LIT SCRATCH_B @ LIT 16 DIV ADD ! ARITY 0 EXIT ] ]") != 0) {
        printf("MEM2_ALLOC_DIFF FAIL (set-block)\n"); return 1;
    }
    if (run(lib) != 0) { printf("MEM2_ALLOC_DIFF FAIL (setup)\n"); return 1; }
    /* GLON-MEM-3 (D): large blocks too -- 150 and 400 values (176 and 432
     * cells) -- so allocations after a collection include requests larger
     * than most free blocks.  The templates are made here (a literal of 400
     * zeros is long); w/sizes: how many sizes the churn picks from (6) */
    {
        static char big[4200];
        int n = sprintf(big, "[ w/sizes: 6  w/t4: [");
        for (int k = 0; k < 150; k++) n += sprintf(big + n, " 0");
        n += sprintf(big + n, " ]  w/t5: [");
        for (int k = 0; k < 400; k++) n += sprintf(big + n, " 0");
        sprintf(big + n, " ]  w/large: func [s] [ either = s 4 [ reduce w/t4 ] [ reduce w/t5 ] ] ]");
        if (run(big) != 0) { printf("MEM2_ALLOC_DIFF FAIL (large templates)\n"); return 1; }
    }
    long managed = (long)(GC_HEAP_LIMIT - GC_HEAP_BASE);
    int steps = managed > 100000 ? 4000 : 400;
    report("start", 0, 0);
    /* 1. churn */
    for (int c = 1; c <= 8; c++) {
        char src[128]; snprintf(src, sizeof src, "[ w/for :w/step 0 0 %d ]", steps);
        report("churn", c, run(src));
    }
    /* 2. exhaustion: grow until a run fails */
    int rc = 0, c = 0;
    for (c = 1; c <= 400 && rc == 0; c++) {
        char src[128]; snprintf(src, sizeof src, "[ w/for :w/grow 0 0 %d ]", steps / 4);
        rc = run(src);
        if (rc != 0 || c % 25 == 0) report("exhaust", c, rc);
    }
    printf("exhaustion: %s after %d chunks (halt reason %d)\n", rc ? "failed" : "did not fail", c - 1, r0_s1_halt_reason());
    /* 3. recovery: drop the chain, churn again */
    int r1 = run("[ w/chain: 0 ]");
    report("recover", 0, r1);
    for (int k = 1; k <= 3; k++) {
        char src[128]; snprintf(src, sizeof src, "[ w/for :w/step 0 0 %d ]", steps);
        report("recover", k, run(src));
    }
    printf("MEM2_ALLOC_DIFF %s (%d invariant failures)\n", failures ? "FAIL" : "DONE", failures);
    return failures ? 1 : 0;
}
