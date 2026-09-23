/* r0_s1_newell_tests.c - Newell (2002) simplified car-following model.
 *
 * Headless verification of demo/shop/demos/newell.glon against real Glon/S1
 * execution (not modelled in C or Python). See GLON-TRAFFIC-MODELS.md sec 2
 * for the design note and sec 8 for the full measured results this pins.
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

int run_r0_s1_newell_tests(void) {
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    M[M1_CURSOR] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    printf("R0-S1 Newell (2002) simplified car-following\n");

    load_file("demo/shop/bootstrap.glon");
    load_file("demo/shop/demos/newell.glon");
    CHECK(r0_s1_ran_cleanly(), "1: demo source loads cleanly");

    /* 2: known one-step result -- mod/hidx hand-checked directly. */
    CHECK(int_val(eval_int("[ newell-mod -9 11 ]")) == 2 &&
          int_val(eval_int("[ newell-mod -10 11 ]")) == 1 &&
          int_val(eval_int("[ newell-mod 0 11 ]")) == 0,
          "2: mod (a-(a/m)*m, wrapped) hand-checked");
    CHECK(int_val(eval_int("[ newell-hidx 0 2 ]")) == 2 &&
          int_val(eval_int("[ newell-hidx 24 10 ]")) == 274,
          "2b: history ring index hand-checked");

    /* 3: initial spacing and speed match the ring geometry, and the seed is a
     * genuine fixed point -- zero-transient uniform flow at vmax for several
     * ticks (see design doc sec 2 for why vmax, not v0/2, is the correct seed
     * for THIS ring's parameters). */
    eval_int("[ newell-init ]");
    CHECK(int_val(eval_int("[ block-at newell-xs 0 ]")) == 0 &&
          int_val(eval_int("[ block-at newell-xs 1 ]")) == 4000 &&
          int_val(eval_int("[ block-at newell-xs 24 ]")) == 96000 &&
          int_val(eval_int("[ block-at newell-vs 0 ]")) == 3000 &&
          int_val(eval_int("[ newell-min-gap ]")) == 3500,
          "3: initial positions/speed match the shared ring geometry");
    for (int t = 0; t < 5; t++) eval_int("[ newell-step ]");
    CHECK(int_val(eval_int("[ newell-mean-v ]")) == 3000 &&
          int_val(eval_int("[ newell-min-v ]")) == 3000 &&
          int_val(eval_int("[ newell-max-v ]")) == 3000 &&
          int_val(eval_int("[ newell-min-gap ]")) == 3500 &&
          r0_s1_ran_cleanly(),
          "4: uniform flow at vmax is an exact fixed point (zero transient)");

    /* 5: the brake produces a real, propagating slowdown. */
    eval_int("[ newell-init ]");
    for (int t = 0; t < 250; t++) eval_int("[ newell-step ]");
    CHECK(int_val(eval_int("[ newell-min-v ]")) == 500 &&
          r0_s1_ran_cleanly(),
          "5: the brake produces a real slowdown (min-v 500)");
    CHECK(int_val(eval_int("[ block-at newell-vs 24 ]")) == 500,
          "6: the slowdown reaches vehicle 24 (immediate follower of the braked vehicle)");

    /* 7: no vehicle ever overlaps (raw gap stays positive) through a long run. */
    eval_int("[ newell-init ]");
    long min_gap_ever = 999999;
    for (int t = 0; t < 600; t++) {
        eval_int("[ newell-step ]");
        long g = int_val(eval_int("[ newell-min-gap ]"));
        if (g < min_gap_ever) min_gap_ever = g;
    }
    CHECK(min_gap_ever > 0 && r0_s1_ran_cleanly(),
          "7: no vehicle ever overlaps over a 600-tick run");

    /* 8: the disturbance PERSISTS rather than damping or growing without
     * bound -- Newell's own predicted behaviour (design doc sec 2): once the
     * braked sub-platoon reaches its own internally-consistent slower
     * equilibrium, there is no mechanism in this minimal law to dissolve it.
     * Pinned exact (deterministic fixed-point model): at tick 600, six
     * vehicles (0, 20..24) are locked at 500 cm/s, the rest at 3000 cm/s. */
    eval_int("[ newell-init ]");
    for (int t = 0; t < 600; t++) eval_int("[ newell-step ]");
    {
        int stuck_ok = 1, fast_ok = 1;
        int stuck[6] = {0, 20, 21, 22, 23, 24};
        for (int k = 0; k < 6; k++) {
            char p[32]; snprintf(p, sizeof p, "[ block-at newell-vs %d ]", stuck[k]);
            if (int_val(eval_int(p)) != 500) stuck_ok = 0;
        }
        for (int i = 1; i < 20; i++) {
            char p[32]; snprintf(p, sizeof p, "[ block-at newell-vs %d ]", i);
            if (int_val(eval_int(p)) != 3000) fast_ok = 0;
        }
        CHECK(stuck_ok && fast_ok,
              "8: disturbance persists as a stable 6-vehicle slow sub-platoon (0,20-24 @ 500; 1-19 @ 3000)");
    }
    CHECK(int_val(eval_int("[ newell-mean-v ]")) == 2400 &&
          int_val(eval_int("[ newell-min-gap ]")) == 3000,
          "9: persistent-platoon observables pinned (mean-v 2400, min-gap 3000)");

    /* 10: determinism. */
    long sig1 = 0, sig2 = 0;
    eval_int("[ newell-init ]");
    for (int t = 0; t < 300; t++) eval_int("[ newell-step ]");
    for (int i = 0; i < 25; i++) {
        char p[64];
        snprintf(p, sizeof p, "[ block-at newell-xs %d ]", i); sig1 += int_val(eval_int(p));
        snprintf(p, sizeof p, "[ block-at newell-vs %d ]", i); sig1 += int_val(eval_int(p));
    }
    eval_int("[ newell-init ]");
    for (int t = 0; t < 300; t++) eval_int("[ newell-step ]");
    for (int i = 0; i < 25; i++) {
        char p[64];
        snprintf(p, sizeof p, "[ block-at newell-xs %d ]", i); sig2 += int_val(eval_int(p));
        snprintf(p, sizeof p, "[ block-at newell-vs %d ]", i); sig2 += int_val(eval_int(p));
    }
    CHECK(sig1 == sig2, "10: two full runs are bit-for-bit deterministic");

    /* 11: canvas view emits the road line + 25 vehicle dots (loader-neutral). */
    eval_int("[ newell-init ]");
    eval_int("[ newell-render ]");
    {
        cell n = M[G1_VIS];
        int has_road = 0, dots = 0;
        for (cell i = 0; i < n && i < G1_VIS_CAP - 1; i++) {
            char c = (char)int_val(M[G1_VIS_DATA + i]);
            if (c == 'L' && i + 2 < n) has_road = 1;
            if (c == 'D') dots++;
        }
        CHECK(has_road && dots == 25, "11: canvas view emits the road line + 25 vehicle dots");
    }

    if (failures == 0) printf("all Newell tests passed\n");
    return failures;
}
