#define _POSIX_C_SOURCE 200809L
/* GLON-MEM-1: allocation cost as a function of the number of live blocks.
 *
 *   mem1_alloc_bench_<profile>
 *
 * A bare session (no GUI, no libraries beyond the runtime's core): K live
 * blocks are kept in a chain (`b/keep: reduce [ b/keep i ]`, 32 cells each
 * with the 16-cell header); then 2,000 short-lived blocks are allocated
 * (`reduce [ i i ]`) and timed, against the same loop allocating nothing.  The
 * difference per iteration is the cost of one allocation at that K.  K grows
 * in steps until the heap would be more than half full.  Collections run when
 * they run; their count is reported.
 *
 * r_alloc (r0_s1_runtime.c emit_alloc) scans the heap block by block from
 * GC_HEAP_BASE to REG_HP for a free block before bumping, so its cost is
 * expected to grow with K.  Measurement only: nothing is changed.
 */
#include "r0_s1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static int run(const char *src) {
    int perr = 0;
    cell prog = r0_s1_parse(src, &perr);
    if (perr) { fprintf(stderr, "parse error %d in: %.60s\n", r0_s1_parse_error_kind(), src); return -1; }
    int n = r0_s1_run_persistent(prog);
    return (r0_s1_ran_cleanly() && n >= 0) ? 0 : -1;
}
/* time n iterations of the loop body word (best of 3) */
static double timed(const char *word, int n) {
    char src[256];
    snprintf(src, sizeof src, "[ b/for :%s 0 0 %d ]", word, n);
    double best = 1e30;
    for (int k = 0; k < 3; k++) {
        double t0 = now_ms();
        if (run(src) != 0) return -1;
        double t = now_ms() - t0;
        if (t < best) best = t;
    }
    return best;
}

/* blocks below the frontier, from the headers (size at B+0, flags at B+1) */
static void heap_blocks(long *blocks, long *free_blocks) {
    long b = 0, f = 0;
    for (cell a = GC_HEAP_BASE; a < r0_s1_heap_high(); ) {
        cell size = M[a + GC_HDR_SIZE];
        if (size <= 0) break;
        b++;
        if (M[a + GC_HDR_FLAGS] == 0) f++;
        a += size;
    }
    *blocks = b; *free_blocks = f;
}
static const char *lib =
    "[ b/for: func [f ctx lo hi] [ either > - hi lo 8 [ m: / + lo hi 2  b/for :f ctx lo m  b/for :f ctx m hi ] [ b/for8 :f ctx lo hi ] ]"
    "  b/for8: func [f ctx i hi] [ either < i hi [ f ctx i  b/for8 :f ctx + i 1 hi ] [ 0 ] ]"
    "  b/keep: 0"
    "  b/hold: func [ctx i] [ b/keep: reduce [ b/keep i ] ]"
    "  b/junk: func [ctx i] [ reduce [ i i ] ]"
    "  b/none: func [ctx i] [ + i i ] ]";

int main(void) {
    long managed = (long)(GC_HEAP_LIMIT - GC_HEAP_BASE);
    int n = 500;
    printf("profile: %ld managed cells; each row: a fresh session holding K live blocks, then %d allocations of 32 cells\n", managed, n);
    printf("%8s %12s %10s %12s %12s %14s %6s\n", "live K", "blocks<HP", "free", "loop (ms)", "alloc (ms)", "per alloc (us)", "GCs");
    long steps[] = { 0, 1000, 2000, 4000, 8000, 16000, 24000 };
    for (int s = 0; s < 7; s++) {
        long k = steps[s];
        if (k * 32 * 2 > managed) break;
        r0_s1_init();
        if (run(lib) != 0) { printf("MEM1_ALLOC_BENCH FAIL (setup)\n"); return 1; }
        char src[128];
        snprintf(src, sizeof src, "[ b/for :b/hold 0 0 %ld ]", k);
        if (k && run(src) != 0) { printf("hold failed at %ld\n", k); return 1; }
        long blocks, fb; heap_blocks(&blocks, &fb);
        long gc0 = r0_s1_gc_count();
        double base = timed("b/none", n), with = timed("b/junk", n);
        long gc1 = r0_s1_gc_count();
        printf("%8ld %12ld %10ld %12.2f %12.2f %14.2f %6ld\n", k, blocks, fb, base, with, (with - base) * 1000.0 / n, gc1 - gc0);
    }
    printf("MEM1_ALLOC_BENCH DONE\n");
    return 0;
}
