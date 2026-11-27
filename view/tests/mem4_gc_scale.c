#define _POSIX_C_SOURCE 200809L
/* GLON-MEM-4: how one collection's cost scales, in bare sessions.
 *
 *   mem4_gc_scale_4m        (diagnostic build: -DR0S1_GC_PROF; tests/gcprof.h)
 *
 * Each row is a fresh session with N top-level functions (N closures bound in
 * the global context) and K live filler blocks (a chain), made in one of two
 * orders: the functions first (their closures low in the heap, the filler
 * above them) or the filler first (the closures above K blocks).  Then one
 * collection is run by itself (the gcprof stub), timed, and its work counted
 * by the mark replay (checked against the collector's own live counts).
 *
 * The expectation from intent6-gcprof (GLON-MEM-4.md): the collector retraces
 * the global context (a loader context, never marked) once per closure it
 * traces, and pays the object-start walk -- O(blocks below the closure) --
 * for every closure value it meets, marked or not.  So the cost should grow
 * about as N x N x (blocks below the closures), not with K above them.
 * Measurement only. */
#include "r0_s1.h"
#include "s1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gcprof.h"

static int run(const char *src) {
    int perr = 0;
    cell prog = r0_s1_parse(src, &perr);
    if (perr) { fprintf(stderr, "parse error %d in: %.60s\n", r0_s1_parse_error_kind(), src); return -1; }
    int n = r0_s1_run_persistent(prog);
    return (r0_s1_ran_cleanly() && n >= 0) ? 0 : -1;
}
static const char *lib =
    "[ b/for: func [f ctx lo hi] [ either > - hi lo 8 [ m: / + lo hi 2  b/for :f ctx lo m  b/for :f ctx m hi ] [ b/for8 :f ctx lo hi ] ]"
    "  b/for8: func [f ctx i hi] [ either < i hi [ f ctx i  b/for8 :f ctx + i 1 hi ] [ 0 ] ]"
    "  b/keep: 0"
    "  b/hold: func [ctx i] [ b/keep: reduce [ b/keep i ] ] ]";
static int define_funcs(int n) {   /* in sources of at most 50 (one source's literal limit) */
    static char src[1 << 16];
    for (int lo = 0; lo < n; lo += 50) {
        int k = sprintf(src, "[");
        for (int i = lo; i < n && i < lo + 50; i++) k += sprintf(src + k, " f%d: func [x] [ + x %d ]", i, i);
        sprintf(src + k, " ]");
        if (run(src) != 0) return -1;
    }
    return 0;
}
static int filler(long k) {
    char src[96]; snprintf(src, sizeof src, "[ b/for :b/hold 0 0 %ld ]", k);
    return k ? run(src) : 0;
}
static int row(int n, long k, int funcs_first) {
    r0_s1_init();
    if (run(lib) != 0) return -1;
    if (funcs_first) { if (define_funcs(n) || filler(k)) return -1; }
    else { if (filler(k) || define_funcs(n)) return -1; }
    gcp_replay();
    long g0 = r0_s1_gc_count();
    double t0 = gcp_now_us();
    gcp_collect_now();
    double t1 = gcp_now_us();
    int ok = r0_s1_gc_count() == g0 + 1 && M[GC_LIVE_OBJS] == gcpc.live_objs && M[GC_LIVE_CELLS] == gcpc.live_cells;
    printf("%5d %6ld %-13s %7ld %6ld %8ld %9ld %8ld %11ld %7.0f %9.2f %s\n", n, k, funcs_first ? "funcs, filler" : "filler, funcs",
           gcpc.blocks, gcpc.live_objs, gcpc.ctx_traces_loader, gcpc.loader_ctx_values, gcpc.closure_walks, gcpc.walk_steps,
           gcpc.closure_walks ? (double)gcpc.walk_steps / gcpc.closure_walks : 0.0, (t1 - t0) / 1000.0, ok ? "ok" : "MISMATCH");
    return ok ? 0 : 1;
}
int main(void) {
    printf("profile: %ld managed cells; one collection per row, by itself\n", (long)(GC_HEAP_LIMIT - GC_HEAP_BASE));
    printf("%5s %6s %-13s %7s %6s %8s %9s %8s %11s %7s %9s\n", "N fn", "K", "made in order", "blocks", "live",
           "ldr ctx", "ldr vals", "walks", "walk steps", "steps/w", "GC (ms)");
    int bad = 0;
    int ns[] = { 50, 100, 200, 400 };
    for (int i = 0; i < 4; i++) bad |= row(ns[i], 0, 1);          /* N alone */
    for (int i = 1; i < 4; i++) bad |= row(100, i * 2000, 1);      /* filler above the closures */
    for (int i = 1; i < 4; i++) bad |= row(100, i * 2000, 0);      /* filler below the closures */
    printf(bad ? "MEM4_GC_SCALE FAIL\n" : "MEM4_GC_SCALE DONE\n");
    return bad;
}
