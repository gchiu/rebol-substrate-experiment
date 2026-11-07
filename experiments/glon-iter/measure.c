/* experiments/glon-iter/measure.c -- GLON-ITER v001 resource measurements on
 * the unchanged 64K runtime. Links the repository's runtime sources; changes
 * nothing in them (the `alloc` mode needs a build with -DR0_S1_PROFILE, a
 * compile flag the runtime already supports).
 *
 *   measure budget  -    -    LIB...   bindings, loader cells and masm code
 *                                      cells per library
 *   measure retain  SETUP STEP LIB...  managed-heap cells kept alive by STEP
 *                                      (STEP binds a global), between two
 *                                      forced collections
 *   measure alloc   SETUP STEP LIB...  cells and allocations per STEP, from the
 *                                      profile counters (K steps, loop cost
 *                                      subtracted)
 *   measure dstack  SETUP STEP LIB...  the data-stack high-water mark of STEP at
 *                                      the deepest probe depth that completes,
 *                                      against the free cells between the end of
 *                                      emitted code and the data-stack top
 *   measure stack   SETUP STEP LIB...  the deepest Glon recursion (probe frames)
 *                                      under which STEP still completes; the
 *                                      lower it is, the more return stack STEP uses
 *
 * Each LIB is a Glon file loaded in order into a fresh machine (prelude and
 * strings must be listed). Comments (;;) are stripped as the CLI does.
 */
#include "r0_s1.h"
#include "m1_layout.h"
#include "s1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char buf[200000];
static const char *libs[32];
static int nlibs;

static int run_src(const char *src) {
    strncpy(buf, src, sizeof buf - 1); buf[sizeof buf - 1] = 0;
    char *r = buf, *w = buf;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
    int err = 0; cell b = r0_s1_parse(buf, &err);
    if (err) return -2;
    r0_s1_run_persistent(b);
    return r0_s1_ran_cleanly() ? 0 : -3;
}
static int load_file(const char *p) {
    static char fb[200000];
    FILE *f = fopen(p, "rb"); if (!f) return -1;
    size_t n = fread(fb, 1, sizeof fb - 1, f); fclose(f); fb[n] = 0;
    return run_src(fb);
}
static const char *base(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }

/* a fresh machine with the libraries loaded, plus m-gc (a forced collection) */
static int fresh(int report) {
    cell me = r0_s1_init(); M[M1_MAIN_ENTRY_CELL] = me; M[M1_CURSOR] = 0; M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++) M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
    cell cap;
    for (int i = 0; i < nlibs; i++) {
        cell c1, c2, l1 = M[GC_LOADER_HP], h1 = asm_here();
        r0_s1_global_usage(&c1, &cap);
        if (load_file(libs[i]) != 0) { printf("load failed: %s\n", libs[i]); return -1; }
        r0_s1_global_usage(&c2, &cap);
        if (report) printf("  %-24s +%3ld bindings  +%5ld loader cells  +%4ld code cells\n", base(libs[i]), (long)(c2 - c1), (long)(M[GC_LOADER_HP] - l1), (long)(asm_here() - h1));
    }
    if (report) {
        cell c; r0_s1_global_usage(&c, &cap);
        printf("  TOTAL %ld/%ld bindings; loader %ld used, %ld free; code ends at %ld\n", (long)c, (long)cap,
               (long)(M[GC_LOADER_HP] - R0S1_HEAP_BASE), (long)(R0S1_HEAP_LIMIT - M[GC_LOADER_HP]), (long)asm_here());
    }
    return run_src("[\nm-gc: masm 1 [ DROP CALL collect LIT 0 ARITY 1 EXIT ]\n]\n");
}
static long live(void) { run_src("m-gc 0\n"); return r0_s1_gc_live_cells(); }

#define DS_TOP 16384L   /* R0S1_DS_INIT: the main data stack grows down from here */
static char prog[100000];
int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: measure MODE SETUP STEP LIB...\n"); return 2; }
    const char *mode = argv[1], *setup = argv[2], *step = argv[3];
    for (int i = 4; i < argc && nlibs < 32; i++) libs[nlibs++] = argv[i];

    if (!strcmp(mode, "budget")) return fresh(1) ? 1 : 0;

    if (!strcmp(mode, "retain")) {
        if (fresh(0) || run_src(setup)) { puts("setup failed"); return 1; }
        long a = live();
        if (run_src(step)) { puts("step failed"); return 1; }
        long b = live();
        printf("  retained by [%s]: %ld cells (live %ld -> %ld of %ld; %ld free)\n", step, b - a, a, b,
               (long)(GC_HEAP_LIMIT - GC_HEAP_BASE), r0_s1_gc_free_cells());
        return 0;
    }

    if (!strcmp(mode, "alloc")) {
#ifndef R0_S1_PROFILE
        puts("build with -DR0_S1_PROFILE"); return 1;
#else
        int k = getenv("K") ? atoi(getenv("K")) : 40; char cmd[64]; long cells[2], objs[2], inv[2];
        if (fresh(0) || run_src(setup)) { puts("setup failed"); return 1; }
        snprintf(prog, sizeof prog,
                 "m-loop: func [n] [ either = n 0 [ 0 ] [ %s  m-loop - n 1 ] ]\n"
                 "m-nop: func [n] [ either = n 0 [ 0 ] [ m-nop - n 1 ] ]\n", step);
        if (run_src(prog)) { puts("loop definition failed"); return 1; }
        for (int pass = 0; pass < 2; pass++) {
            cell c0 = M[PF_ALLOC_CELLS], o0 = M[PF_ALLOCS], i0 = M[PF_CLOSURE];
            snprintf(cmd, sizeof cmd, pass ? "m-loop %d\n" : "m-nop %d\n", k);
            if (run_src(cmd)) { printf("  [%s]: step halted\n", step); return 1; }
            cells[pass] = M[PF_ALLOC_CELLS] - c0; objs[pass] = M[PF_ALLOCS] - o0; inv[pass] = M[PF_CLOSURE] - i0;
        }
        printf("  per [%s]: %.1f cells in %.2f allocations, %.1f closure calls (%d steps)\n", step,
               (cells[1] - cells[0]) / (double)k, (objs[1] - objs[0]) / (double)k, (inv[1] - inv[0]) / (double)k, k);
        return 0;
#endif
    }

    if (!strcmp(mode, "dstack")) {
        int lo = 0, hi = 200, best = -1; long low = DS_TOP;
        for (int pass = 0; pass < 2; pass++) {
            while (pass == 0 && lo <= hi) {
                int mid = (lo + hi) / 2;
                if (fresh(0) || run_src(setup)) { puts("setup failed"); return 1; }
                snprintf(prog, sizeof prog, "m-probe: func [n] [ either = n 0 [ %s  0 ] [ + 0 m-probe - n 1 ] ]\nm-probe %d\n", step, mid);
                if (run_src(prog) == 0) { best = mid; lo = mid + 1; } else hi = mid - 1;
            }
            if (pass == 1) {
                if (fresh(0) || run_src(setup)) { puts("setup failed"); return 1; }
                cell code_end = asm_here();
                for (cell a = code_end; a < DS_TOP - 64; a++) M[a] = (cell)0x5A5A5A5A;
                snprintf(prog, sizeof prog, "m-probe: func [n] [ either = n 0 [ %s  0 ] [ + 0 m-probe - n 1 ] ]\nm-probe %d\n", step, best);
                int rc = run_src(prog);
                for (cell a = code_end; a < DS_TOP; a++) if (M[a] != (cell)0x5A5A5A5A) { low = a; break; }
                printf("  data stack around [%s] at probe depth %d: %ld cells used of %ld free above code (code ends %ld)%s\n",
                       step, best, (long)(DS_TOP - low), (long)(DS_TOP - code_end), (long)code_end, rc ? " RUN FAILED" : "");
            }
        }
        return 0;
    }

    if (!strcmp(mode, "stack")) {
        int lo = 0, hi = 200, best = -1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (fresh(0) || run_src(setup)) { puts("setup failed"); return 1; }
            snprintf(prog, sizeof prog,
                     "m-probe: func [n] [ either = n 0 [ %s  0 ] [ + 0 m-probe - n 1 ] ]\nm-probe %d\n", step, mid);
            if (run_src(prog) == 0) { best = mid; lo = mid + 1; } else hi = mid - 1;
        }
        printf("  max probe depth around [%s]: %d\n", step, best);
        return 0;
    }
    return 2;
}
