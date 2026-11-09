/* experiments/glon-masm/measure.c -- code density and cost of masm forms.
 *
 * Loads prelude, strings and strings-ext, then each definition line of
 * variants.glon on its own (wrapped in [ ]), recording its source tokens and
 * the code cells it emitted. Then, for each variant called on one argument:
 *   - time per call: a balanced Glon loop of K calls, median of 3, less the
 *     same loop around a constant;
 *   - with -DCOUNT (linking the instrumented S1 copy s1_prof.c instead of the
 *     frozen s1.c): the exact number of S1 instructions executed per call,
 *     the same subtraction. Wall time on this machine varies by several us
 *     per call between runs; instruction counts do not vary.
 * The runtime is the repository's, unchanged.
 *   measure ROOT VARIANTS
 */
#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef COUNT
#include "s1_prof.h"     /* the instrumented S1 copy (s1_prof.c), not frozen s1.c */
#endif
static double last_ops;  /* S1 instructions executed by the last timed run (COUNT build) */

static char buf[1 << 17], wb[(1 << 17) + 16];
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
static double timed(const char *s) {
    cell h = M[GC_LOADER_HP];
#ifdef COUNT
    s1_prof_reset();
#endif
    clock_t t0 = clock(); int rc = run(s);
    double d = (double)(clock() - t0) / CLOCKS_PER_SEC; M[GC_LOADER_HP] = h;
#ifdef COUNT
    last_ops = 0; for (int i = 0; i < 8; i++) last_ops += (double)s1_prof_op_count[i];
#endif
    if (rc) { fprintf(stderr, "failed: %s\n", s); exit(1); }
    return d;
}
static double median3(const char *s) {
    double t[3]; for (int k = 0; k < 3; k++) t[k] = timed(s);
    for (int a = 0; a < 3; a++) for (int b = a + 1; b < 3; b++) if (t[b] < t[a]) { double x = t[a]; t[a] = t[b]; t[b] = x; }
    return t[1];
}
static int masm_tokens(const char *line) {
    const char *a = strstr(line, " masm "); if (!a) return 0;
    a = strchr(a, '['); if (!a) return 0;
    int d = 0, n = 0, in = 0;
    for (const char *p = a; *p; p++) {
        if (*p == '[') { d++; continue; }
        if (*p == ']') { if (--d == 0) break; continue; }
        if (*p == ' ' || *p == '\n' || *p == '\t') in = 0; else if (!in) { in = 1; n++; }
    }
    return n;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: measure ROOT VARIANTS\n"); return 2; }
    char p[4096];
    cell me = r0_s1_init(); M[M1_MAIN_ENTRY_CELL] = me; M[M1_CURSOR] = 0; M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++) M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
    const char *libs[] = { "jupyter/prelude.glon", "demo/shop/strings.glon", "demo/shop/strings-ext.glon", NULL };
    for (int i = 0; libs[i]; i++) { snprintf(p, sizeof p, "%s/%s", argv[1], libs[i]); if (file(p)) return 1; }

    /* definitions, one line each */
    FILE *f = fopen(argv[2], "rb"); if (!f) return 2;
    static char line[16384]; char names[64][32]; int nn = 0;
    printf("%-9s %6s %6s\n", "word", "tokens", "cells");
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "x/", 2) != 0) continue;
        char *colon = strchr(line, ':'); if (!colon) continue;
        cell c0 = asm_here();
        if (run(line)) { fprintf(stderr, "load failed: %s", line); return 1; }
        int nl = (int)(colon - line); if (nl > 31) nl = 31;
        memcpy(names[nn], line, (size_t)nl); names[nn][nl] = 0;
        printf("%-9s %6d %6ld\n", names[nn], masm_tokens(line), (long)(asm_here() - c0));
        nn++;
    }
    fclose(f);

    run("pz: 0 lp0: func [a b] [ either = b + a 1 [ pz ] [ lp0 a / + a b 2 lp0 / + a b 2 b ] ]");
    int K = argc > 3 ? atoi(argv[3]) : 3000; char s[4096];
    snprintf(s, sizeof s, "lp0 0 %d", K); double t0 = median3(s); double ops0 = last_ops;
    printf("\nper call (native -O1, K=%d, median of 3; loop overhead %.1f us subtracted)\n", K, t0 * 1e6 / K);
    printf("%-9s %9s %12s\n", "word", "us/call", "S1 instr/call");
    for (int i = 0; i < nn; i++) {
        const char *arg = (!strcmp(names[i], "x/xc") || !strcmp(names[i], "x/two")) ? "12345 :x/u" : "12345";
        snprintf(s, sizeof s, "lpx: func [a b] [ either = b + a 1 [ %s %s ] [ lpx a / + a b 2 lpx / + a b 2 b ] ]", names[i], arg);
        if (run(s)) return 1;
        snprintf(s, sizeof s, "lpx 0 %d", K);
        double t = median3(s);
#ifdef COUNT
        printf("%-9s %9.2f %12.1f\n", names[i], (t - t0) * 1e6 / K, (last_ops - ops0) / K);
#else
        printf("%-9s %9.2f %12s\n", names[i], (t - t0) * 1e6 / K, "-");
#endif
    }
    printf("\ncode ends at %ld (data-stack top 16384); the runtime's own code is %ld cells\n",
           (long)asm_here(), (long)r0_s1_code_size());
    return 0;
}
