/* experiments/glon-mem/measure.c -- storage overhead and operation cost of the
 * GLON-MEM v001 prototype, on the unchanged runtime (native). Measurement only:
 * it defines one masm probe, m-gc (a full collection), to read live cells.
 *
 *   build/measure ROOT     ROOT = the repository root
 */
#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char buf[1 << 17], wb[(1 << 17) + 16];
static int N;
static int run(const char *s) {
    snprintf(wb, sizeof wb, "[ %s ]", s);
    int e = 0; cell b = r0_s1_parse(wb, &e); if (e) return -2;
    N = r0_s1_run_persistent(b);
    return r0_s1_ran_cleanly() ? 0 : -3;
}
static int file(const char *root, const char *rel) {
    char p[4096]; snprintf(p, sizeof p, "%s/%s", root, rel);
    FILE *f = fopen(p, "rb"); if (!f) { fprintf(stderr, "cannot open %s\n", p); exit(2); }
    size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0;
    char *r = buf, *w = buf;
    while (*r) { if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; } *w++ = *r++; }
    *w = 0;
    int e = 0; cell b = r0_s1_parse(buf, &e); if (e) return -2;
    r0_s1_run_persistent(b);
    return r0_s1_ran_cleanly() ? 0 : -3;
}
static long live(void) { cell h = M[GC_LOADER_HP]; run("m-gc 0"); M[GC_LOADER_HP] = h; return r0_s1_gc_live_cells(); }
static double secs(const char *s) {
    cell h = M[GC_LOADER_HP];
    clock_t t0 = clock(); int rc = run(s); double d = (double)(clock() - t0) / CLOCKS_PER_SEC;
    M[GC_LOADER_HP] = h;
    if (rc) { fprintf(stderr, "run failed: %s\n", s); exit(1); }
    return d;
}

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : "../..";
    cell me = r0_s1_init(); M[M1_MAIN_ENTRY_CELL] = me; M[M1_CURSOR] = 0; M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++) M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
    const char *libs[] = { "jupyter/prelude.glon", "demo/shop/strings.glon", "demo/shop/strings-ext.glon", NULL };
    for (int i = 0; libs[i]; i++) if (file(root, libs[i])) { fprintf(stderr, "load %s\n", libs[i]); return 1; }
    run("m-gc: masm 1 [ DROP CALL collect LIT 0 ARITY 1 EXIT ]");
    cell code0 = asm_here(), ld0 = M[GC_LOADER_HP], b0, cap;
    r0_s1_global_usage(&b0, &cap);
    long live0 = live();
    if (file(root, "experiments/glon-mem/mem.glon")) { fprintf(stderr, "load mem.glon\n"); return 1; }
    cell b1; r0_s1_global_usage(&b1, &cap);
    long live1 = live();
    printf("code ends at %ld before mem.glon, %ld after (data stack top 16384)\n", (long)code0, (long)asm_here());
    printf("library: %ld bindings, %ld loader cells, %ld code cells, %ld managed cells live\n",
           (long)(b1 - b0), (long)(M[GC_LOADER_HP] - ld0), (long)(asm_here() - code0), live1 - live0);

    printf("\nstorage (cells, header included), region vs STRING! of n bytes:\n");
    int sizes[] = { 1, 16, 100, 1000, 3000, 6000 };
    for (int i = 0; i < 6; i++) {
        char s[160];
        long a = live();
        snprintf(s, sizeof s, "x: mem/alloc %d", sizes[i]); run(s);
        long b = live(); run("x: none");
        long c = live();
        snprintf(s, sizeof s, "y: s/slice mk-string [ 0 ] 0 0  y: none"); run(s);
        long str = 16 + ((sizes[i] + 1 + 15) / 16) * 16;
        printf("  n = %5d: region %5ld (measured %5ld), STRING! %5ld, ratio %.2f\n", sizes[i],
               16 + ((4 + (sizes[i] + 2) / 3 + 15) / 16) * 16L, b - a, str,
               (double)str / (double)(16 + ((4 + (sizes[i] + 2) / 3 + 15) / 16) * 16));
        (void)c;
    }

    /* per-operation cost: K operations in a balanced loop, less the empty loop */
    run("r: mem/alloc 4000  s: s/slice \"0123456789abcdef0123456789abcdef\" 0 32  q: mem/alloc 4000 "
        "z: 0 "
        "lp0: func [a b] [ either = b + a 1 [ z ] [ lp0 a / + a b 2 lp0 / + a b 2 b ] ] "
        "lpb: func [a b] [ either = b + a 1 [ mem/byte r a ] [ lpb a / + a b 2 lpb / + a b 2 b ] ] "
        "lps: func [a b] [ either = b + a 1 [ mem/set-byte r a 7 ] [ lps a / + a b 2 lps / + a b 2 b ] ] "
        "lpc: func [a b] [ either = b + a 1 [ mem/cell r a ] [ lpc a / + a b 2 lpc / + a b 2 b ] ] "
        "lpd: func [a b] [ either = b + a 1 [ mem/set-cell r a -5 ] [ lpd a / + a b 2 lpd / + a b 2 b ] ] "
        "lpa: func [a b] [ either = b + a 1 [ s/at s 5 ] [ lpa a / + a b 2 lpa / + a b 2 b ] ] "
        "lpr: func [a b] [ either = b + a 1 [ mem/acc 0 0 r a mem/w ] [ lpr a / + a b 2 lpr / + a b 2 b ] ]");
    int K = 3000;
    char s[160];
    snprintf(s, sizeof s, "lp0 0 %d", K); double t0 = secs(s);
    struct { const char *name, *fn; } ops[] = {
        { "mem/byte (checked)", "lpb" }, { "mem/set-byte (checked)", "lps" },
        { "mem/cell (checked, 4 bytes)", "lpc" }, { "mem/set-cell (checked)", "lpd" },
        { "mem/acc (validated masm, no SIN!)", "lpr" }, { "s/at on a STRING! (checked)", "lpa" } };
    printf("\nper operation (native -O1; %d ops each, loop overhead %.1f us/op subtracted):\n", K, t0 * 1e6 / K);
    for (int i = 0; i < 6; i++) {
        snprintf(s, sizeof s, "%s 0 %d", ops[i].fn, K);
        double t = secs(s);
        printf("  %-32s %7.1f us\n", ops[i].name, (t - t0) * 1e6 / K);
    }
    double tm = secs("mem/move r 0 q 0 3000");
    double tmo = secs("mem/move r 0 r 1 3000");
    double ts = secs("w: mem/to-string r 0 256  w: none");
    double tf = secs("mem/from-string q 0 s/slice \"0123456789abcdef0123456789abcdef\" 0 32");
    printf("  %-32s %7.2f us per byte (3,000 bytes)\n", "mem/move, between regions", tm * 1e6 / 3000);
    printf("  %-32s %7.2f us per byte (3,000 bytes)\n", "mem/move, overlapping (backward)", tmo * 1e6 / 3000);
    printf("  %-32s %7.1f us per byte (256 bytes)\n", "mem/to-string", ts * 1e6 / 256);
    printf("  %-32s %7.1f us per byte (32 bytes)\n", "mem/from-string", tf * 1e6 / 32);
    double ta = secs("x: mem/alloc 3000  x: none");
    printf("  %-32s %7.1f us (3,000 bytes, zero-filled)\n", "mem/alloc", ta * 1e6);
    return 0;
}
