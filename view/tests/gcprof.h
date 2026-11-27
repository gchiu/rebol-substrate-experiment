/* gcprof.h -- GLON-MEM-4 collector diagnostics (diagnostic build only:
 * -DR0S1_GC_PROF).  Nothing here changes the emitted code or the heap's
 * contents; nothing in S1 is touched.
 *
 *   1. IP sampler.  A CLOCK_MONOTONIC POSIX timer raises SIGPROF every
 *      GCP_PERIOD_US; the handler records the S1 instruction pointer
 *      (M[REG_IP]) and a timestamp.  The runtime records, in C, the S1
 *      address range of each collector routine, each inlined object-start
 *      walk and each collector phase (r0s1_gcprof_*, r0_s1_runtime.c), so a
 *      sample is attributed to the routine whose code was executing.
 *
 *   2. Mark replay.  A read-only C re-implementation of r_collect's mark
 *      phase (the same roots, dispatch, traces and object-start check, in the
 *      same order), marking in a private bitmap, never in the headers.  It
 *      counts the work the real collector does: mark_value calls, context
 *      traces, closure candidates, object-start walk steps, worklist pushes
 *      and pops, and the sweep's live/dead/free blocks and coalesces.  Its
 *      result is checked against the real collection's GC_LIVE_OBJS and
 *      GC_LIVE_CELLS.
 *
 *   3. A collection on demand.  A 20-cell stub (CALL r_collect; HALT,
 *      assembled by hand exactly as s1.c's asm_call would emit it) is written
 *      just past the end of emitted code -- inside the 64-cell guard below
 *      the data-stack floor (RV_DSLIM), which the data stack never reaches --
 *      run with s1_run, and the cells restored.  The code pointer does not
 *      move. */
#ifndef GCPROF_H
#define GCPROF_H
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "m1_layout.h"

extern const char *r0s1_gcprof_name[];
extern cell r0s1_gcprof_lo[], r0s1_gcprof_hi[];
extern int r0s1_gcprof_n;
/* the runtime's own M1 addresses (they differ from this file's m1_layout.h
 * in the 1M/4M builds; GLON-MEM-4.md) */
extern const cell r0s1_gcprof_m1_task_table, r0s1_gcprof_m1_arena_base;
#define GCP_TASK_TABLE r0s1_gcprof_m1_task_table
#define GCP_ARENA_BASE r0s1_gcprof_m1_arena_base
#define R0S1_GCPROF_MAX 64   /* as r0_s1_runtime.c */

/* ---- 1. the sampler ---------------------------------------------------- */
#ifndef GCP_PERIOD_US
#define GCP_PERIOD_US 50
#endif
#define GCP_MAX_SAMPLES (1 << 21)   /* 2M samples: 100 s at 50 us */
static cell *gcp_ip;
static double *gcp_t;
static volatile long gcp_n;
static timer_t gcp_timer;
static double gcp_now_us(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}
static void gcp_handler(int sig) {
    (void)sig;
    long k = gcp_n;
    if (k < GCP_MAX_SAMPLES) { gcp_ip[k] = M[REG_IP]; gcp_t[k] = gcp_now_us(); gcp_n = k + 1; }
}
static void gcp_start(void) {
    if (!gcp_ip) { gcp_ip = malloc(sizeof(cell) * GCP_MAX_SAMPLES); gcp_t = malloc(sizeof(double) * GCP_MAX_SAMPLES); }
    gcp_n = 0;
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_handler = gcp_handler; sa.sa_flags = SA_RESTART; sigemptyset(&sa.sa_mask);
    sigaction(SIGPROF, &sa, NULL);
    struct sigevent sev; memset(&sev, 0, sizeof sev);
    sev.sigev_notify = SIGEV_SIGNAL; sev.sigev_signo = SIGPROF;
    timer_create(CLOCK_MONOTONIC, &sev, &gcp_timer);
    struct itimerspec it; memset(&it, 0, sizeof it);
    it.it_interval.tv_nsec = GCP_PERIOD_US * 1000L; it.it_value.tv_nsec = GCP_PERIOD_US * 1000L;
    timer_settime(gcp_timer, 0, &it, NULL);
}
static void gcp_stop(void) {
    struct itimerspec it; memset(&it, 0, sizeof it);
    timer_settime(gcp_timer, 0, &it, NULL);
    timer_delete(gcp_timer);
    signal(SIGPROF, SIG_IGN);
}
/* the innermost recorded range holding ip (walks sit inside routines), or -1 */
static int gcp_range_of(cell ip) {
    int best = -1; cell bw = 0;
    for (int r = 0; r < r0s1_gcprof_n; r++)
        if (ip >= r0s1_gcprof_lo[r] && ip < r0s1_gcprof_hi[r]) {
            cell w = r0s1_gcprof_hi[r] - r0s1_gcprof_lo[r];
            if (best < 0 || w < bw) { best = r; bw = w; }
        }
    return best;
}
static int gcp_find(const char *name) {
    for (int r = 0; r < r0s1_gcprof_n; r++) if (!strcmp(r0s1_gcprof_name[r], name)) return r;
    return -1;
}
/* is ip collector code?  (every routine below is reached only from r_collect;
 * r_alloc's own range is excluded) */
static int gcp_is_gc(int r) {
    return r >= 0 && strcmp(r0s1_gcprof_name[r], "alloc") != 0;
}
typedef struct { double total_ms, roots_ms, drain_ms, sweep_ms; long gc_samples; } gcp_summary;
/* report the samples taken between gcp_start and gcp_stop; one collection */
static gcp_summary gcp_report(const char *label) {
    gcp_summary s; memset(&s, 0, sizeof s);
    long n = gcp_n;
    int r_drain = gcp_find("collect: drain loop"), r_sweep = gcp_find("collect: sweep");
    long hits[R0S1_GCPROF_MAX + 1]; memset(hits, 0, sizeof hits);
    long ph_hits[3][R0S1_GCPROF_MAX + 1]; memset(ph_hits, 0, sizeof ph_hits);
    double t_first = -1, t_last = -1, t_drain = -1, t_sweep = -1;
    for (long k = 0; k < n; k++) {
        int r = gcp_range_of(gcp_ip[k]);
        if (!gcp_is_gc(r)) continue;
        if (t_first < 0) t_first = gcp_t[k];
        t_last = gcp_t[k];
        if (r == r_drain && t_drain < 0) t_drain = gcp_t[k];
        if (r == r_sweep && t_sweep < 0) t_sweep = gcp_t[k];
        hits[r]++; s.gc_samples++;
    }
    /* phases by time: roots until the drain loop first runs, the drain until
     * the sweep first runs, then the sweep */
    for (long k = 0; k < n; k++) {
        int r = gcp_range_of(gcp_ip[k]);
        if (!gcp_is_gc(r)) continue;
        int ph = (t_sweep >= 0 && gcp_t[k] >= t_sweep) ? 2 : (t_drain >= 0 && gcp_t[k] >= t_drain) ? 1 : 0;
        ph_hits[ph][r]++;
    }
    double per = GCP_PERIOD_US / 1000.0;   /* ms per sample */
    long ph_tot[3] = {0, 0, 0};
    for (int ph = 0; ph < 3; ph++) for (int r = 0; r < r0s1_gcprof_n; r++) ph_tot[ph] += ph_hits[ph][r];
    s.total_ms = s.gc_samples * per;
    s.roots_ms = ph_tot[0] * per; s.drain_ms = ph_tot[1] * per; s.sweep_ms = ph_tot[2] * per;
    printf("  [%s] %ld samples (%d us) in all; %ld in collector code = %.0f ms; first-to-last collector sample %.0f ms\n",
           label, n, GCP_PERIOD_US, s.gc_samples, s.total_ms, t_first >= 0 ? (t_last - t_first) / 1000.0 : 0.0);
    const char *phn[3] = { "roots (before the drain)", "drain (worklist)", "sweep" };
    for (int ph = 0; ph < 3; ph++) {
        if (!ph_tot[ph]) { printf("    phase %-26s     0 ms\n", phn[ph]); continue; }
        printf("    phase %-26s %6.0f ms (%4.1f%%):", phn[ph], ph_tot[ph] * per, 100.0 * ph_tot[ph] / (s.gc_samples ? s.gc_samples : 1));
        for (int r = 0; r < r0s1_gcprof_n; r++)
            if (ph_hits[ph][r] * 200 >= ph_tot[ph])   /* >= 0.5% of the phase */
                printf(" %s %.1f%%", r0s1_gcprof_name[r], 100.0 * ph_hits[ph][r] / ph_tot[ph]);
        printf("\n");
    }
    int r_walk_total = 0; long walk_hits = 0;
    for (int r = 0; r < r0s1_gcprof_n; r++)
        if (!strcmp(r0s1_gcprof_name[r], "object-start walk")) { walk_hits += hits[r]; r_walk_total++; }
    printf("    the object-start walk loops (%d inlined copies): %.0f ms, %.1f%% of the collection\n",
           r_walk_total, walk_hits * per, 100.0 * walk_hits / (s.gc_samples ? s.gc_samples : 1));
    return s;
}

/* INTENT-7: every sample, by routine (the evaluator's and the collector's):
 * the share of the samples whose IP is in each recorded range (innermost),
 * outside every range, or 0 (no S1 running: the host zeroes the IP after each
 * run in the diagnostic build) */
static void gcp_report_all(const char *label) {
    long n = gcp_n, hits[R0S1_GCPROF_MAX + 1], none = 0, other = 0;
    memset(hits, 0, sizeof hits);
    for (long k = 0; k < n; k++) {
        if (gcp_ip[k] == 0) { none++; continue; }
        int r = gcp_range_of(gcp_ip[k]);
        if (r < 0) other++; else hits[r]++;
    }
    long s1 = n - none;
    double per = GCP_PERIOD_US / 1000.0;
    printf("  [%s] %ld samples (%.0f ms): S1 running %ld (%.0f ms), not running %ld (%.0f ms)\n", label, n, n * per, s1, s1 * per, none, none * per);
    for (int pass = 0; pass < r0s1_gcprof_n; pass++) {
        int best = -1;
        for (int r = 0; r < r0s1_gcprof_n; r++) if (hits[r] > 0 && (best < 0 || hits[r] > hits[best])) best = r;
        if (best < 0 || hits[best] * 200 < s1) break;     /* down to 0.5% of the S1 time */
        printf("    %-22s %5.1f%% of S1 time (%.0f ms)\n", r0s1_gcprof_name[best], 100.0 * hits[best] / (s1 ? s1 : 1), hits[best] * per);
        hits[best] = 0;
    }
    if (other) printf("    %-22s %5.1f%%\n", "(other S1 code)", 100.0 * other / (s1 ? s1 : 1));
}

/* ---- 2. the mark replay -------------------------------------------------- */
typedef struct {
    long mv_calls, mv_ctx, mv_closure, mv_user, mv_string, mv_block, mv_other;
    long ctx_traces_managed, ctx_traces_stack, ctx_traces_loader, ctx_values_traced;
    long loader_ctx_values;            /* values visited while tracing loader contexts */
    long closure_candidates, closure_walks, walk_steps, walk_hits_marked;
    long walks_unmarked, walk_steps_unmarked;
    long walks_skipped;                         /* GLON-MEM-5: candidates skipped before the walk (marked) */   /* the walks left if a marked-looking header skipped the walk */
    long pushes, already_marked, pops, max_wl;
    long pops_ctx, pops_closure, pops_user, pops_string, pops_block;
    long frames;
    long live_objs, live_cells;
    /* the sweep, from the layout before the collection */
    long blocks, free_before, dead, coalesces, free_runs_after, free_cells_after;
    long frontier;
    long corrupt;
} gcp_counts;
static gcp_counts gcpc;
static long gcp_bad_tag[16]; static cell gcp_bad_ex[16];
static void gcp_bad(cell v) { int t = (int)(((v % 16) + 16) % 16); if (!gcp_bad_tag[t]) gcp_bad_ex[t] = v; gcp_bad_tag[t]++; gcpc.corrupt++; }
static unsigned char *gcp_mark;        /* one byte per 16 managed cells */
static cell *gcp_starts; static long gcp_nstarts;   /* block starts below the frontier */
static cell *gcp_wl; static long gcp_wl_sp;
static void gcp_mv(cell v);
static int gcp_no_loader;   /* gcp_reach: do not follow loader-heap contexts (the global one) */
static long gcp_walk_steps(cell h) {   /* headers the walk reads to reach h */
    long lo = 0, hi = gcp_nstarts;     /* first start >= h */
    while (lo < hi) { long mid = (lo + hi) / 2; if (gcp_starts[mid] < h) lo = mid + 1; else hi = mid; }
    return lo;
}
static int gcp_is_start(cell h) {
    long i = gcp_walk_steps(h); return i < gcp_nstarts && gcp_starts[i] == h;
}
static void gcp_push(cell p) {
    long bit = (p - GC_HEAP_BASE) / 16;
    if (gcp_mark[bit]) { gcpc.already_marked++; return; }
    gcp_mark[bit] = 1; gcpc.pushes++;
    gcp_wl[gcp_wl_sp++] = p; if (gcp_wl_sp > gcpc.max_wl) gcpc.max_wl = gcp_wl_sp;
}
static void gcp_closure_inline(cell p) {   /* emit_mark_closure_inline */
    gcpc.closure_candidates++;
    if (!(p >= GC_HEAP_BASE && p < GC_HEAP_LIMIT)) return;
    if (p % 16 != 0) return;
    cell flags = M[p - (GC_HDR_STRIDE - GC_HDR_FLAGS)];
    if ((flags / 4) % 8 != GC_KIND_CLOSURE) return;
    cell h = p - GC_HDR_STRIDE;
#ifndef R0S1_GC_CLOSURE_WALK_ALWAYS
    /* GLON-MEM-5: the check skips a candidate whose would-be flags carry the
     * mark bit.  For an object start that is the replay's own mark; for any
     * other p the word is not a header, so marking never changes it */
    {
        int looks_marked = gcp_is_start(h) ? gcp_mark[(p - GC_HEAP_BASE) / 16] : (int)((flags / 2) % 2);
        if (looks_marked) { gcpc.walks_skipped++; return; }
    }
#endif
    if (!(h < M[REG_HP])) return;
    gcpc.closure_walks++;
    long steps = gcp_walk_steps(h);
    gcpc.walk_steps += steps;
    /* the walk's candidate looks marked when the header it would read has the
     * mark bit: in the real collector only object starts can be marked */
    if (!(gcp_is_start(h) && gcp_mark[(p - GC_HEAP_BASE) / 16])) { gcpc.walks_unmarked++; gcpc.walk_steps_unmarked += steps; }
    if (!gcp_is_start(h)) return;
    if (gcp_mark[(p - GC_HEAP_BASE) / 16]) gcpc.walk_hits_marked++;
    gcp_push(p);
}
static cell gcp_ldr_ctx[64]; static long gcp_ldr_ctx_hits[64]; static int gcp_nldr;
static void gcp_note_loader_ctx(cell p) {
    for (int i = 0; i < gcp_nldr; i++) if (gcp_ldr_ctx[i] == p) { gcp_ldr_ctx_hits[i]++; return; }
    if (gcp_nldr < 64) { gcp_ldr_ctx[gcp_nldr] = p; gcp_ldr_ctx_hits[gcp_nldr++] = 1; }
}
static void gcp_trace_ctx(cell p, int loader) {
    if (loader) gcp_note_loader_ctx(p);
    gcp_mv(M[p + CTX_PARENT]);
    cell count = M[p + CTX_COUNT];
    for (cell i = 0; i < count; i++) {
        gcpc.ctx_values_traced++; if (loader) gcpc.loader_ctx_values++;
        gcp_mv(M[p + CTX_DATA + 1 + 2 * i]);
    }
}
static void gcp_mark_frame(cell p) {
    if (p == 0) return;
    gcpc.frames++;
    gcp_mark_frame(M[p + FRAME_PREV]);
    gcp_mv(M[p + FRAME_CTX]);
}
static void gcp_mv(cell v) {
    gcpc.mv_calls++;
    cell tag = v % 16;
    if (tag == T_CONTEXT) {
        gcpc.mv_ctx++;
        cell p = v - T_CONTEXT;
        if (p >= GC_HEAP_BASE && p < GC_HEAP_LIMIT) { gcp_push(p); return; }
        if (p >= R0S1_DS_INIT && p < R0S1_RS_INIT) { gcpc.ctx_traces_stack++; gcp_trace_ctx(p, 0); return; }
        if (p >= GCP_ARENA_BASE && p < GCP_ARENA_BASE + M1_MAX_TASKS * M1_TASK_CELLS) { gcpc.ctx_traces_stack++; gcp_trace_ctx(p, 0); return; }
        if (p >= R0S1_HEAP_BASE && p < R0S1_HEAP_LIMIT) { if (gcp_no_loader) return; gcpc.ctx_traces_loader++; gcp_trace_ctx(p, 1); return; }
        gcp_bad(v); return;
    }
    if (tag == T_CLOSURE) { gcpc.mv_closure++; gcp_closure_inline(v - T_CLOSURE); return; }
    if (tag == T_USER || tag == T_ERROR) {
        gcpc.mv_user++;
        cell p = v - tag;
        if (p >= GC_HEAP_BASE && p < GC_HEAP_LIMIT) { gcp_push(p); return; }
        gcp_bad(v); return;
    }
    if (tag == T_STRING) {
        gcpc.mv_string++;
        cell p = v - T_STRING;
        if (p >= GC_HEAP_BASE && p < GC_HEAP_LIMIT) { gcp_push(p); return; }
        gcp_bad(v); return;
    }
    if (tag == T_BLOCK) {
        gcpc.mv_block++;
        cell p = v - T_BLOCK;
        if (p == G1_OUT) return;
        if (p < GC_HEAP_BASE) { gcp_bad(v); return; }
        if (p < GC_HEAP_LIMIT) { gcp_push(p); return; }   /* (header validated by the real collector) */
        if (p < R0S1_HEAP_LIMIT) return;
        gcp_bad(v); return;
    }
    gcpc.mv_other++;
}
static void gcp_scan_values(cell lo, cell hi) { for (cell a = lo; a < hi; a++) gcp_mv(M[a]); }
static void gcp_scan_closures(cell lo, cell hi) { for (cell a = lo; a < hi; a++) gcp_closure_inline(M[a]); }
/* replay the mark phase of a collection run now (SP/RP as they are) */
static void gcp_replay(void) {
    memset(&gcpc, 0, sizeof gcpc); memset(gcp_bad_tag, 0, sizeof gcp_bad_tag); gcp_nldr = 0;
    long nbits = (GC_HEAP_LIMIT - GC_HEAP_BASE) / 16 + 1;
    if (!gcp_mark) { gcp_mark = malloc(nbits); gcp_starts = malloc(sizeof(cell) * nbits); gcp_wl = malloc(sizeof(cell) * nbits); }
    memset(gcp_mark, 0, nbits);
    gcp_nstarts = 0; gcp_wl_sp = 0;
    for (cell a = GC_HEAP_BASE; a < M[REG_HP]; ) {
        cell size = M[a + GC_HDR_SIZE]; if (size <= 0) break;
        gcp_starts[gcp_nstarts++] = a; a += size;
    }
    gcpc.frontier = M[REG_HP] - GC_HEAP_BASE;
    /* roots, in r_collect's order */
    gcp_mv(M[GC_GLOBAL_CTX]); gcp_mv(M[RV_CTX]); gcp_mv(M[RV_CHILD]);
    gcp_closure_inline(M[RV_CLOSURE]); gcp_mark_frame(M[RV_FRAME]);
    gcp_scan_values(BUILTIN_BASE, BUILTIN_BASE + 16);
    cell sp0 = M[REG_SP], rp0 = M[REG_RP];
    if (sp0 > R0S1_DS_INIT) { printf("  (replay: a task world is active; not replayed)\n"); gcpc.corrupt++; }
    else { gcp_scan_values(sp0, R0S1_DS_INIT); gcp_scan_closures(rp0, R0S1_RS_INIT); }
    for (int t = 0; t < M1_MAX_TASKS; t++) {
        cell rec = GCP_TASK_TABLE + t * M1_TASK_REC_SIZE;
        if (M[rec + TREC_STATE] != TASK_RUNNABLE) continue;
        gcp_mv(M[rec + TREC_CTX]); gcp_mark_frame(M[rec + TREC_FRAME]);
        cell base = GCP_ARENA_BASE + t * M1_TASK_CELLS;
        gcp_scan_values(M[rec + TREC_SP], base + M1_DS_OFF);
        gcp_scan_closures(M[rec + TREC_RP], base + M1_RS_OFF);
    }
    /* drain */
    while (gcp_wl_sp > 0) {
        cell p = gcp_wl[--gcp_wl_sp]; gcpc.pops++;
        cell kind = (M[p - (GC_HDR_STRIDE - GC_HDR_FLAGS)] / 4) % 8;
        if (kind == GC_KIND_CTX) { gcpc.pops_ctx++; gcpc.ctx_traces_managed++; gcp_trace_ctx(p, 0); }
        else if (kind == GC_KIND_CLOSURE) { gcpc.pops_closure++; gcp_mv(M[p + CLOSURE_SPEC]); gcp_mv(M[p + CLOSURE_BODY]); gcp_mv(M[p + CLOSURE_CTX]); }
        else if (kind == GC_KIND_USER) { gcpc.pops_user++; cell count = M[p + 1]; gcp_mv(M[p]); for (cell i = 0; i < count; i++) gcp_mv(M[p + 2 + i]); }
        else if (kind == GC_KIND_STRING) { gcpc.pops_string++; }
        else if (kind == GC_KIND_BLOCK) { gcpc.pops_block++; cell len = M[p]; for (cell i = 0; i < len; i++) gcp_mv(M[p + BLK_DATA + i]); }
    }
    /* the sweep, predicted from the layout and the replay's marks */
    int prev_free = 0;
    for (long i = 0; i < gcp_nstarts; i++) {
        cell a = gcp_starts[i], size = M[a + GC_HDR_SIZE], flags = M[a + GC_HDR_FLAGS];
        gcpc.blocks++;
        int live = (flags % 2) && gcp_mark[(a + GC_HDR_STRIDE - GC_HEAP_BASE) / 16];
        if (live) { gcpc.live_objs++; gcpc.live_cells += size; prev_free = 0; continue; }
        if (flags % 2) gcpc.dead++; else gcpc.free_before++;
        gcpc.free_cells_after += size;
        if (prev_free) gcpc.coalesces++; else { gcpc.free_runs_after++; prev_free = 1; }
    }
}
/* GLON-INST-1: the managed cells reachable from one value alone (the
 * replay's marking, from that root only, not following the loader-heap
 * contexts -- the global context -- that every closure captures), for
 * attributing retained memory */
static long gcp_reach(cell v) {
    long nbits = (GC_HEAP_LIMIT - GC_HEAP_BASE) / 16 + 1;
    if (!gcp_mark) { gcp_mark = malloc(nbits); gcp_starts = malloc(sizeof(cell) * nbits); gcp_wl = malloc(sizeof(cell) * nbits); }
    memset(gcp_mark, 0, nbits); gcp_nstarts = 0; gcp_wl_sp = 0;
    for (cell a = GC_HEAP_BASE; a < M[REG_HP]; ) { cell size = M[a + GC_HDR_SIZE]; if (size <= 0) break; gcp_starts[gcp_nstarts++] = a; a += size; }
    gcp_no_loader = 1;
    gcp_mv(v);
    while (gcp_wl_sp > 0) {
        cell p = gcp_wl[--gcp_wl_sp];
        cell kind = (M[p - (GC_HDR_STRIDE - GC_HDR_FLAGS)] / 4) % 8;
        if (kind == GC_KIND_CTX) gcp_trace_ctx(p, 0);
        else if (kind == GC_KIND_CLOSURE) { gcp_mv(M[p + CLOSURE_SPEC]); gcp_mv(M[p + CLOSURE_BODY]); gcp_mv(M[p + CLOSURE_CTX]); }
        else if (kind == GC_KIND_USER) { cell count = M[p + 1]; gcp_mv(M[p]); for (cell i = 0; i < count; i++) gcp_mv(M[p + 2 + i]); }
        else if (kind == GC_KIND_BLOCK) { cell len = M[p]; for (cell i = 0; i < len; i++) gcp_mv(M[p + BLK_DATA + i]); }
    }
    gcp_no_loader = 0;
    long cells = 0;
    for (long i = 0; i < gcp_nstarts; i++) {
        cell a = gcp_starts[i];
        if ((M[a + GC_HDR_FLAGS] % 2) && gcp_mark[(a + GC_HDR_STRIDE - GC_HEAP_BASE) / 16]) cells += M[a + GC_HDR_SIZE];
    }
    return cells;
}
static void gcp_print_counts(const char *label) {
    gcp_counts *c = &gcpc;
    printf("  [%s] replay: heap %ld blocks below the frontier (%ld cells): %ld live (%ld cells), %ld dead, %ld already free\n",
           label, c->blocks, c->frontier, c->live_objs, c->live_cells, c->dead, c->free_before);
    printf("    sweep: %ld free runs after it (%ld cells), %ld coalesces; the frontier is not moved\n",
           c->free_runs_after, c->free_cells_after, c->coalesces);
    printf("    mark_value calls %ld: context %ld, closure %ld, user/error %ld, string %ld, block %ld, other %ld\n",
           c->mv_calls, c->mv_ctx, c->mv_closure, c->mv_user, c->mv_string, c->mv_block, c->mv_other);
    printf("    context traces: managed %ld (via the worklist), stack %ld, loader %ld (never marked: traced at every reference); values traced %ld, %ld of them in loader contexts\n",
           c->ctx_traces_managed, c->ctx_traces_stack, c->ctx_traces_loader, c->ctx_values_traced, c->loader_ctx_values);
    printf("    closure candidates %ld; object-start walks %ld (%ld of them for a closure already marked), walk steps %ld (%.0f per walk)\n",
           c->closure_candidates, c->closure_walks, c->walk_hits_marked, c->walk_steps,
           c->closure_walks ? (double)c->walk_steps / c->closure_walks : 0.0);
    printf("    distinct loader contexts traced: %d;", gcp_nldr);
    for (int i = 0; i < gcp_nldr && i < 6; i++)
        printf(" %ld (%ld bindings%s) x%ld", (long)gcp_ldr_ctx[i], (long)M[gcp_ldr_ctx[i] + CTX_COUNT],
               gcp_ldr_ctx[i] + T_CONTEXT == M[GC_GLOBAL_CTX] ? ", the global context" : "", gcp_ldr_ctx_hits[i]);
    printf("\n");
    printf("    walks skipped as already marked (GLON-MEM-5 check): %ld%s\n", c->walks_skipped,
#ifdef R0S1_GC_CLOSURE_WALK_ALWAYS
           " (this build: the kind-only check, none skipped)"
#else
           ""
#endif
           );
    printf("    if a marked-looking candidate skipped the walk: %ld walks, %ld steps (%.3f%% of the steps)\n",
           c->walks_unmarked, c->walk_steps_unmarked, c->walk_steps ? 100.0 * c->walk_steps_unmarked / c->walk_steps : 0.0);
    printf("    worklist: %ld pushes, %ld already-marked hits, %ld pops (ctx %ld, closure %ld, user %ld, string %ld, block %ld), deepest %ld; frames %ld\n",
           c->pushes, c->already_marked, c->pops, c->pops_ctx, c->pops_closure, c->pops_user, c->pops_string, c->pops_block, c->max_wl, c->frames);
    if (c->corrupt) {
        printf("    (replay: %ld values the collector would treat as corrupt:", c->corrupt);
        for (int t = 0; t < 16; t++) if (gcp_bad_tag[t]) printf(" tag %d x%ld (e.g. %ld)", t, gcp_bad_tag[t], (long)gcp_bad_ex[t]);
        printf("; GC_HEAP %ld..%ld, loader %ld..%ld)\n", (long)GC_HEAP_BASE, (long)GC_HEAP_LIMIT, (long)R0S1_HEAP_BASE, (long)R0S1_HEAP_LIMIT);
    }
}

/* ---- 3. a collection on demand ------------------------------------------- */
static void gcp_collect_now(void) {
    cell a = asm_here(), n = 0, save[24];
    for (int i = 0; i < 24; i++) save[i] = M[a + i];
    cell cont_at;
    M[a + n++] = OP_LIT; cont_at = a + n; M[a + n++] = 0;          /* LIT cont */
    /* >R, as s1.c's asm_toR emits it */
    M[a + n++] = OP_LIT; M[a + n++] = REG_RP; M[a + n++] = OP_FETCH;
    M[a + n++] = OP_LIT; M[a + n++] = 1; M[a + n++] = OP_HOST; M[a + n++] = HOST_SUB;
    M[a + n++] = OP_LIT; M[a + n++] = REG_RP; M[a + n++] = OP_STORE;
    M[a + n++] = OP_LIT; M[a + n++] = REG_RP; M[a + n++] = OP_FETCH; M[a + n++] = OP_STORE;
    /* ip = r_collect */
    M[a + n++] = OP_LIT; M[a + n++] = r0_s1_gc_collect_addr(); M[a + n++] = OP_LIT; M[a + n++] = REG_IP; M[a + n++] = OP_STORE;
    M[cont_at] = a + n;
    M[a + n++] = OP_HALT;
    if (a + n > M[RV_DSLIM]) { printf("  (gcp_collect_now: no room below the data-stack floor)\n"); for (int i = 0; i < 24; i++) M[a + i] = save[i]; return; }
    s1_run(a);
    for (int i = 0; i < 24; i++) M[a + i] = save[i];
}
/* a hash of the code region [256, R0S1_CODE_LIMIT): the same in the normal
 * and diagnostic builds of one profile, at the same point of a session */
static unsigned long gcp_code_hash(void) {
    unsigned long h = 1469598103934665603UL;
    for (cell a = 256; a < R0S1_CODE_LIMIT; a++) { h ^= (unsigned long)M[a]; h *= 1099511628211UL; }
    return h;
}
#endif
