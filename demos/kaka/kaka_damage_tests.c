/* demos/kaka/kaka_damage_tests.c -- deterministic end-to-end damage-path test.
 *
 * The reported symptom was "one attacker damages one tree, but a neighbouring
 * tree also loses health". The existing kaka_render_tests.c proves only that
 * direct synthetic damage renders in isolation; this harness drives the REAL
 * gameplay machinery instead:
 *
 *     attacker -> targeting/attack state -> damage event -> target tuple
 *              -> F_A decrement -> (render)
 *
 * via move-all (the same dispatch the game uses), with exactly one damaging
 * actor and controlled state. For each tree and each damaging actor kind it
 * asserts that ONLY the selected tree's F_A changes. It also checks retargeting
 * (the old target must stop) and the real spawn->sweep->swoop->eat path.
 *
 * The overlay is injected by this test (not shipped in the game) and only
 * defines setup/step helpers; no game rule is changed.
 */
#include "r0_s1.h"
#include "r0_s1_g1a.h"
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>

static char filebuf[1 << 20], wrapbuf[1 << 20];
static int fails = 0;

static int run_source_impl(const char *src, int reclaim) {
    cell save = M[GC_LOADER_HP];
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) { fprintf(stderr, "kaka-damage-test: parse %d in: %s\n", err, src); return -1; }
    r0_s1_run_persistent(b);
    if (reclaim) M[GC_LOADER_HP] = save;   /* transient call, like g1a_dispatch */
    if (!r0_s1_ran_cleanly()) { fprintf(stderr, "kaka-damage-test: run failed: %s\n", src); return -1; }
    return 0;
}
static int run_source(const char *src) { return run_source_impl(src, 1); }

/* parse+run a source and return its single integer result */
static long run_int(const char *src) {
    cell save = M[GC_LOADER_HP];
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) return -1;
    int n = r0_s1_run_persistent(b);
    M[GC_LOADER_HP] = save;
    if (!r0_s1_ran_cleanly() || n < 1) return -1;
    cell v = r0_s1_result(0, n);
    return (v & 15) == T_INT ? (long)int_val(v) : -1;
}
static long fa(int i) { char b[32]; sprintf(b, "block-at ts-t %d F_A", i); return run_int(b); }

static int load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "kaka-damage-test: cannot open %s\n", path); return -1; }
    size_t n = fread(filebuf, 1, sizeof filebuf - 1, f);
    fclose(f);
    filebuf[n] = 0;
    char *r = filebuf, *w = filebuf;
    while (*r) { if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; } *w++ = *r++; }
    *w = 0;
    const char *src = filebuf;
    char *p = filebuf;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (*p && *p != '[') {
        size_t len = strlen(filebuf);
        wrapbuf[0] = '['; wrapbuf[1] = ' ';
        memcpy(wrapbuf + 2, filebuf, len);
        wrapbuf[len + 2] = ' '; wrapbuf[len + 3] = ']'; wrapbuf[len + 4] = 0;
        src = wrapbuf;
    }
    return run_source_impl(src, 0);
}

static const char *OVERLAY =
"["
"  gk: 0  gr: 0"
"  mk-kaka: func [ti] ["
"    reset"
"    k: ts-claim K_KAKA"
"    block-set! k F_X block-at ts-t ti F_X"
"    block-set! k F_Y 250"
"    block-set! k F_STATE KS_EAT"
"    block-set! k F_TARGET ti"
"    block-set! k F_A 0"
"    gk: k"
"  ]"
"  mk-rat: func [ti] ["
"    reset"
"    r: ts-claim K_RAT"
"    block-set! r F_X block-at ts-t ti F_X"
"    block-set! r F_Y 345"
"    block-set! r F_A 0"
"    block-set! r F_B 0"
"    gr: r"
"  ]"
"  step-all: func [] [ move-all 0 ]"
"  retarget: func [ti] ["
"    block-set! gk F_TARGET ti"
"    block-set! gk F_X block-at ts-t ti F_X"
"    block-set! gk F_STATE KS_EAT"
"    block-set! gk F_A 0"
"  ]"
"  full-spawn: func [] [ reset  spawn-kaka ]"
"]";

static void ticks(int n) { for (int t = 0; t < n; t++) run_source("step-all"); }

/* assert only tree `t` changed (a drop), others identical to before */
static void check_isolated(const char *label, int t, long b0, long b1, long b2) {
    long a0 = fa(0), a1 = fa(1), a2 = fa(2);
    long after[3] = { a0, a1, a2 }, before[3] = { b0, b1, b2 };
    int ok = 1;
    for (int i = 0; i < 3; i++) {
        if (i == t) { if (!(after[i] < before[i])) ok = 0; }
        else        { if (after[i] != before[i])    ok = 0; }
    }
    printf("  %-28s t%d: [%ld,%ld,%ld] -> [%ld,%ld,%ld]  %s\n",
           label, t, b0, b1, b2, a0, a1, a2, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

/* assert exactly one tree changed (the id-distributed picker may choose any) */
static void check_exactly_one(const char *label, long b0, long b1, long b2) {
    long a0 = fa(0), a1 = fa(1), a2 = fa(2);
    int drops = (a0 < b0) + (a1 < b1) + (a2 < b2);
    int same  = (a0 == b0) + (a1 == b1) + (a2 == b2);
    int ok = (drops == 1 && same == 2);
    printf("  %-28s     [%ld,%ld,%ld] -> [%ld,%ld,%ld]  %s\n",
           label, b0, b1, b2, a0, a1, a2, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

int main(void) {
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    if (load_file("glon-lib/prelude.glon")) return 2;
    if (load_file("glon-lib/strings.glon")) return 2;
    if (load_file("demos/kaka/kaka-lib.glon")) return 2;
    if (load_file("demos/kaka/kaka.glon")) return 2;
    if (load_file("demos/kaka/kaka-draw.glon")) return 2;
    if (load_file("demos/kaka/kaka-rangi.glon")) return 2;
    if (load_file("demos/kaka/kaka-wave.glon")) return 2;
    if (run_source_impl(OVERLAY, 0)) return 2;

    printf("kaka damage-path test (one real attacker -> only its tree changes)\n");

    /* A. kaka-eat, one kaka targeting each tree */
    for (int ti = 0; ti < 3; ti++) {
        char b[32]; sprintf(b, "mk-kaka %d", ti); if (run_source(b)) return 2;
        long b0 = fa(0), b1 = fa(1), b2 = fa(2);
        ticks(130);
        check_isolated("kaka-eat", ti, b0, b1, b2);
    }
    /* B. rat-gnaw, one stationary rat at each tree */
    for (int ti = 0; ti < 3; ti++) {
        char b[32]; sprintf(b, "mk-rat %d", ti); if (run_source(b)) return 2;
        long b0 = fa(0), b1 = fa(1), b2 = fa(2);
        ticks(120);
        check_isolated("rat-gnaw", ti, b0, b1, b2);
    }
    /* C. retarget: attack tree0, then switch to tree1; tree0 must stop */
    {
        if (run_source("mk-kaka 0")) return 2;
        ticks(110);
        long m0 = fa(0), m1 = fa(1), m2 = fa(2);
        if (run_source("retarget 1")) return 2;
        ticks(130);
        long a0 = fa(0), a1 = fa(1), a2 = fa(2);
        int ok = (a0 == m0) && (a1 < m1) && (a2 == m2);
        printf("  %-28s     [%ld,%ld,%ld] -> [%ld,%ld,%ld]  %s\n",
               "retarget t0->t1", m0, m1, m2, a0, a1, a2, ok ? "ok" : "FAIL");
        if (!ok) fails++;
    }
    /* D. full real spawn -> sweep -> swoop -> eat (target id-distributed) */
    {
        if (run_source("full-spawn")) return 2;
        long b0 = fa(0), b1 = fa(1), b2 = fa(2);
        ticks(400);
        check_exactly_one("full spawn/attack", b0, b1, b2);
    }

    if (fails == 0) { printf("kaka-damage-test PASS\n"); return 0; }
    printf("kaka-damage-test FAIL (%d)\n", fails);
    return 1;
}
