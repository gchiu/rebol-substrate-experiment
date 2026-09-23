/* r0_s1_ovm_tests.c - FVDM with a piecewise-linear optimal-velocity function.
 *
 * Headless verification of demo/shop/demos/ovm.glon against real Glon/S1
 * execution. See GLON-TRAFFIC-MODELS.md sec 1 (why this model, not Gipps or
 * canonical tanh-OVM) and sec 3 (design note) / sec 8 (measured results).
 */
#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
static char src[65536];

#define CHECK(c, m) do { if (c) printf("  ok: %s\n", m); \
                         else { printf("  FAIL: %s\n", m); failures++; } } while (0)

static void strip_comments(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
}
static void load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    size_t n = fread(src, 1, sizeof src - 1, f); fclose(f); src[n] = 0;
    strip_comments(src);
    int err = 0; cell b = r0_s1_parse(src, &err);
    if (err) return;
    r0_s1_run_persistent(b);
}
static cell eval_int(const char *prog) {
    strcpy(src, prog);
    int err = 0;
    cell save_lhp = M[GC_LOADER_HP];
    cell b = r0_s1_parse(src, &err);
    if (err) { M[GC_LOADER_HP] = save_lhp; return 0; }
    int N = r0_s1_run_persistent(b);
    M[GC_LOADER_HP] = save_lhp;
    if (!r0_s1_ran_cleanly() || N != 1) return 0;
    return r0_s1_result(0, 1);
}

int run_r0_s1_ovm_tests(void) {
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    M[M1_CURSOR] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    printf("R0-S1 FVDM / piecewise-linear OVM\n");

    load_file("demo/shop/bootstrap.glon");
    load_file("demo/shop/demos/ovm.glon");
    CHECK(r0_s1_ran_cleanly(), "1: demo source loads cleanly");

    /* 2: optimal-velocity function, hand-checked at each piece. */
    CHECK(int_val(eval_int("[ ovm-V 0 ]")) == 0 &&
          int_val(eval_int("[ ovm-V 200 ]")) == 0 &&
          int_val(eval_int("[ ovm-V 2100 ]")) == 1500 &&
          int_val(eval_int("[ ovm-V 4000 ]")) == 3000 &&
          int_val(eval_int("[ ovm-V 5000 ]")) == 3000,
          "2: V(h) piecewise-linear, hand-checked at hc/midpoint/hmax/above");

    /* 3: known one-step acceleration, hand-computed:
     * V(3500) = (3500-200)*3000/(4000-200) = 2605 (truncated);
     * term1 = 5*(2605-1500)/10 = 552; term2 = 5*(1500-1500)/10 = 0. */
    CHECK(int_val(eval_int("[ ovm-accel 1500 1500 3500 ]")) == 552,
          "3: idm-style hand-computed one-step accel (v=vl=1500, h=3500 -> 552)");

    /* 4: initial conditions match the shared ring geometry (same as IDM). */
    eval_int("[ ovm-init ]");
    CHECK(int_val(eval_int("[ block-at ovm-xs 0 ]")) == 0 &&
          int_val(eval_int("[ block-at ovm-xs 1 ]")) == 4000 &&
          int_val(eval_int("[ block-at ovm-xs 24 ]")) == 96000 &&
          int_val(eval_int("[ block-at ovm-vs 0 ]")) == 1500 &&
          int_val(eval_int("[ ovm-min-gap ]")) == 3500,
          "4: initial positions/speed match the shared ring geometry");

    /* 5: relaxation is smooth (not an instant snap), and symmetric (uniform
     * ring -> every vehicle identical) before any disturbance. */
    for (int t = 0; t < 5; t++) eval_int("[ ovm-step ]");
    CHECK(int_val(eval_int("[ ovm-mean-v ]")) == int_val(eval_int("[ ovm-min-v ]")) &&
          int_val(eval_int("[ ovm-min-v ]")) == int_val(eval_int("[ ovm-max-v ]")) &&
          int_val(eval_int("[ ovm-mean-v ]")) > 1500 &&
          int_val(eval_int("[ ovm-mean-v ]")) < 2000 &&
          r0_s1_ran_cleanly(),
          "5: symmetric ring relaxes smoothly toward V(3500) (no snap, no split)");

    /* 6: safe-gap / collision invariant -- overlap must fail loudly, exactly
     * like IDM's, never be silently clamped (design doc sec 3: this model
     * reuses IDM's own fail-stop convention). */
    eval_int("[ ovm-init ]");
    eval_int("[ ovm-block-set! ovm-xs 1 200 ]");
    eval_int("[ ovm-step ]");
    CHECK(!r0_s1_ran_cleanly(), "6: overlapping vehicles (raw gap < 0) fail-stop");

    /* 7: the moderate parameter set damps the disturbance (pinned exact). */
    eval_int("[ ovm-init ]");
    eval_int("[ ovm-use-moderate ]");
    for (int t = 0; t < 600; t++) eval_int("[ ovm-step ]");
    CHECK(int_val(eval_int("[ ovm-mean-v ]")) == 2608 &&
          int_val(eval_int("[ ovm-min-v ]")) == 2202 &&
          int_val(eval_int("[ ovm-wave-amplitude ]")) == 761 &&
          r0_s1_ran_cleanly(),
          "7: moderate set damps the disturbance by tick 600 (pinned)");

    /* 8: no vehicle ever overlaps under either parameter set, over a full run. */
    eval_int("[ ovm-init ]");
    eval_int("[ ovm-use-moderate ]");
    long min_gap_ever = 999999;
    for (int t = 0; t < 600; t++) {
        eval_int("[ ovm-step ]");
        long g = int_val(eval_int("[ ovm-min-gap ]"));
        if (g < min_gap_ever) min_gap_ever = g;
    }
    CHECK(min_gap_ever > 0 && r0_s1_ran_cleanly(), "8: moderate set: no overlap over 600 ticks");

    eval_int("[ ovm-init ]");
    eval_int("[ ovm-use-unstable ]");
    long min_gap_ever2 = 999999;
    for (int t = 0; t < 600; t++) {
        eval_int("[ ovm-step ]");
        long g = int_val(eval_int("[ ovm-min-gap ]"));
        if (g < min_gap_ever2) min_gap_ever2 = g;
    }
    CHECK(min_gap_ever2 > 0 && r0_s1_ran_cleanly(), "9: low-a0 (near-instability) set: no overlap over 600 ticks");

    /* 10: determinism. */
    long sig1 = 0, sig2 = 0;
    eval_int("[ ovm-init ]");
    eval_int("[ ovm-use-moderate ]");
    for (int t = 0; t < 300; t++) eval_int("[ ovm-step ]");
    for (int i = 0; i < 25; i++) {
        char p[64];
        snprintf(p, sizeof p, "[ block-at ovm-xs %d ]", i); sig1 += int_val(eval_int(p));
        snprintf(p, sizeof p, "[ block-at ovm-vs %d ]", i); sig1 += int_val(eval_int(p));
    }
    eval_int("[ ovm-init ]");
    eval_int("[ ovm-use-moderate ]");
    for (int t = 0; t < 300; t++) eval_int("[ ovm-step ]");
    for (int i = 0; i < 25; i++) {
        char p[64];
        snprintf(p, sizeof p, "[ block-at ovm-xs %d ]", i); sig2 += int_val(eval_int(p));
        snprintf(p, sizeof p, "[ block-at ovm-vs %d ]", i); sig2 += int_val(eval_int(p));
    }
    CHECK(sig1 == sig2, "11: two full runs are bit-for-bit deterministic");

    /* 12: canvas view. */
    eval_int("[ ovm-init ]");
    eval_int("[ ovm-render ]");
    {
        cell n = M[G1_VIS];
        int has_road = 0, dots = 0;
        for (cell i = 0; i < n && i < G1_VIS_CAP - 1; i++) {
            char c = (char)int_val(M[G1_VIS_DATA + i]);
            if (c == 'L' && i + 2 < n) has_road = 1;
            if (c == 'D') dots++;
        }
        CHECK(has_road && dots == 25, "12: canvas view emits the road line + 25 vehicle dots");
    }

    if (failures == 0) printf("all FVDM/OVM tests passed\n");
    return failures;
}
