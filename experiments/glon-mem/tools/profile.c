/* tools/profile.c -- per-definition cost and per-layer timing for a Glon
 * memory library. Measurement only (defines a masm m-gc probe).
 *   profile ROOT DEFDIR NDEFS [EXPR | !SETUP ...]
 * loads prelude, strings, strings-ext, then DEFDIR/00.glon .. NN.glon one at a
 * time (printing code, loader, binding and live-managed cells per definition),
 * then times each EXPR: a Glon expression evaluated K times in a balanced
 * loop, less the empty loop. */
#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static char buf[1 << 17], wb[(1 << 17) + 64];
static int run(const char *s) {
    snprintf(wb, sizeof wb, "[ %s ]", s);
    int e = 0; cell b = r0_s1_parse(wb, &e); if (e) return -2;
    r0_s1_run_persistent(b); return r0_s1_ran_cleanly() ? 0 : -3;
}
static int file(const char *p) {
    FILE *f = fopen(p, "rb"); if (!f) { fprintf(stderr, "cannot open %s\n", p); exit(2); }
    size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0;
    char *r = buf, *w = buf;
    while (*r) { if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; } *w++ = *r++; }
    *w = 0;
    int e = 0; cell b = r0_s1_parse(buf, &e); if (e) return -2;
    r0_s1_run_persistent(b); return r0_s1_ran_cleanly() ? 0 : -3;
}
static long live(void) { cell h = M[GC_LOADER_HP]; run("m-gc 0"); M[GC_LOADER_HP] = h; return r0_s1_gc_live_cells(); }
static double alloc_cells;   /* managed cells allocated by the last timed run */
static double timed(const char *s) {
    cell h = M[GC_LOADER_HP];
#ifdef R0_S1_PROFILE
    cell a0 = M[PF_ALLOC_CELLS];
#endif
    clock_t t0 = clock(); int rc = run(s);
#ifdef R0_S1_PROFILE
    alloc_cells = (double)M[PF_ALLOC_CELLS]; (void)a0;   /* the run resets the counters */
#endif
    double d = (double)(clock() - t0) / CLOCKS_PER_SEC; M[GC_LOADER_HP] = h;
    if (rc) { fprintf(stderr, "failed: %s\n", s); exit(1); }
    return d;
}
int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: profile ROOT DEFDIR NDEFS [EXPR...]\n"); return 2; }
    char p[4096];
    cell me = r0_s1_init(); M[M1_MAIN_ENTRY_CELL] = me; M[M1_CURSOR] = 0; M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++) M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
    const char *libs[] = { "jupyter/prelude.glon", "demo/shop/strings.glon", "demo/shop/strings-ext.glon", NULL };
    for (int i = 0; libs[i]; i++) { snprintf(p, sizeof p, "%s/%s", argv[1], libs[i]); if (file(p)) return 1; }
    run("m-gc: masm 1 [ DROP CALL collect LIT 0 ARITY 1 EXIT ]");
    int nd = atoi(argv[3]);
    long tc = 0, tl = 0, tb = 0, tm = 0;
    cell code_start = asm_here();
    printf("%-4s %6s %7s %5s %7s\n", "def", "code", "loader", "bind", "managed");
    for (int i = 0; i < nd; i++) {
        snprintf(p, sizeof p, "%s/%02d.glon", argv[2], i);
        cell c0 = asm_here(), l0 = M[GC_LOADER_HP], b0, b1, cap; r0_s1_global_usage(&b0, &cap);
        long m0 = live();
        if (file(p)) { fprintf(stderr, "load failed %s\n", p); return 1; }
        long m1 = live(); r0_s1_global_usage(&b1, &cap);
        printf("%02d   %6ld %7ld %5ld %7ld\n", i, (long)(asm_here() - c0), (long)(M[GC_LOADER_HP] - l0), (long)(b1 - b0), m1 - m0);
        tc += asm_here() - c0; tl += M[GC_LOADER_HP] - l0; tb += b1 - b0; tm += m1 - m0;
    }
    printf("TOTAL %5ld %7ld %5ld %7ld   code %ld..%ld, data-stack room %ld\n", tc, tl, tb, tm,
           (long)code_start, (long)asm_here(), 16384L - (long)asm_here());
    run("pz: 0 lp0: func [a b] [ either = b + a 1 [ pz ] [ lp0 a / + a b 2 lp0 / + a b 2 b ] ]");
    int K = 3000; char s[2048];
    snprintf(s, sizeof s, "lp0 0 %d", K); double t0 = timed(s); double a_loop = alloc_cells / K; (void)a_loop;
    printf("\nloop overhead %.1f us/op; per-op cost below has it subtracted (K=%d, median of 3)\n", t0 * 1e6 / K, K);
    for (int i = 4; i < argc; i++) {
        if (argv[i][0] == '!') { if (run(argv[i] + 1)) { fprintf(stderr, "setup failed %s\n", argv[i]); return 1; } continue; }
        /* "K=n:EXPR" times EXPR n times instead of K (slow bulk operations) */
        const char *e = argv[i]; int k1 = K;
        if (!strncmp(e, "K=", 2)) { k1 = atoi(e + 2); e = strchr(e, ':') + 1; }
        snprintf(s, sizeof s, "lpx: func [a b] [ either = b + a 1 [ %s ] [ lpx a / + a b 2 lpx / + a b 2 b ] ]", e);
        if (run(s)) { fprintf(stderr, "bad expr %s\n", e); return 1; }
        snprintf(s, sizeof s, "lpx 0 %d", k1);
        double t[3]; for (int k = 0; k < 3; k++) t[k] = timed(s);
        for (int a = 0; a < 3; a++) for (int b = a + 1; b < 3; b++) if (t[b] < t[a]) { double x = t[a]; t[a] = t[b]; t[b] = x; }
#ifdef R0_S1_PROFILE
        printf("  %-60s %10.1f managed cells allocated per op (n=%d)\n", e, alloc_cells / k1 - a_loop, k1);
#else
        printf("  %-60s %10.1f us  (min %.1f, max %.1f, n=%d)\n", e, (t[1] - t0 * k1 / K) * 1e6 / k1,
               (t[0] - t0 * k1 / K) * 1e6 / k1, (t[2] - t0 * k1 / K) * 1e6 / k1, k1);
#endif
    }
    return 0;
}
