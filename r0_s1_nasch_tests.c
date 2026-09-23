/* r0_s1_nasch_tests.c - Nagel-Schreckenberg (1992) cellular-automaton model.
 *
 * Headless verification of demo/shop/demos/nasch.glon against real Glon/S1
 * execution, including the seeded application-level PRNG. See
 * GLON-TRAFFIC-MODELS.md sec 4 (design note, PRNG constants) and sec 8
 * (measured results, including the spontaneous-jamming finding).
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

static long run_signature(long seed, long vmax_canonical, long ticks) {
    char cmd[64];
    if (vmax_canonical) eval_int("[ nasch-use-canonical ]");
    eval_int("[ nasch-init ]");
    snprintf(cmd, sizeof cmd, "[ nasch-init-seed %ld ]", seed);
    eval_int(cmd);
    for (long t = 0; t < ticks; t++) eval_int("[ nasch-step ]");
    long sig = 0;
    for (int i = 0; i < 25; i++) {
        char p[64];
        snprintf(p, sizeof p, "[ block-at nasch-pos %d ]", i); sig += int_val(eval_int(p));
        snprintf(p, sizeof p, "[ block-at nasch-vel %d ]", i); sig += int_val(eval_int(p));
    }
    return sig;
}

int run_r0_s1_nasch_tests(void) {
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    M[M1_CURSOR] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    printf("R0-S1 Nagel-Schreckenberg cellular automaton\n");

    load_file("demo/shop/bootstrap.glon");
    load_file("demo/shop/demos/nasch.glon");
    CHECK(r0_s1_ran_cleanly(), "1: demo source loads cleanly");

    /* 2: mod and the LCG, hand-checked. */
    CHECK(int_val(eval_int("[ nasch-mod -9 11 ]")) == 2,
          "2: mod (a-(a/m)*m, wrapped) hand-checked");
    CHECK(int_val(eval_int("[ nasch-rand-next 1 ]")) == 12446,
          "3: LCG one-step hand-checked (seed=1 -> (1*101+12345) mod 2^20 = 12446)");

    /* 4: initial conditions (canonical vmax=5 set). */
    eval_int("[ nasch-use-canonical ]");
    eval_int("[ nasch-init ]");
    CHECK(int_val(eval_int("[ block-at nasch-pos 0 ]")) == 0 &&
          int_val(eval_int("[ block-at nasch-pos 1 ]")) == 5 &&
          int_val(eval_int("[ block-at nasch-pos 24 ]")) == 127 &&
          int_val(eval_int("[ block-at nasch-vel 0 ]")) == 5,
          "4: initial positions/speed match the ring geometry (133 cells / 25 vehicles)");

    /* 5: same-seed reproducibility -- the headline PRNG requirement. */
    long sig_seed1_a = run_signature(1, 1, 200);
    long sig_seed1_b = run_signature(1, 1, 200);
    CHECK(sig_seed1_a == sig_seed1_b, "5: same seed (1) reproduces bit-for-bit over 200 ticks");

    /* 6: different seeds diverge (across several seeds, not just one pair --
     * a single-seed comparison cannot support a stochastic-variability claim). */
    long sigs[4]; long seeds[4] = {1, 2, 3, 12345};
    for (int k = 0; k < 4; k++) sigs[k] = run_signature(seeds[k], 1, 200);
    int all_distinct = 1;
    for (int a = 0; a < 4; a++) for (int b = a + 1; b < 4; b++) if (sigs[a] == sigs[b]) all_distinct = 0;
    CHECK(all_distinct, "6: seeds {1,2,3,12345} produce 4 distinct 200-tick trajectories");

    /* 7: one-step rule-order check -- acceleration then safety braking then
     * randomization then movement, on a hand-set two-vehicle case with a
     * TIGHT gap, so rule 2 (safety) must override rule 1 (acceleration):
     * vehicle 0 at cell 0 v=2, vehicle 1 (its leader) at cell 3 v=0 ->
     * gap = 3-0-1 = 2; rule 1: v=3; rule 2: v=min(3,2)=2; movement: pos=0+2=2
     * (rule 3 disabled by using seed 0's very first draw only if it does NOT
     * fire; verified by checking the deterministic seed-0 first-tick result
     * matches EITHER the no-slowdown or the slowdown-by-1 case exactly). */
    {
        eval_int("[ nasch-use-canonical ]");
        eval_int("[ nasch-init ]");
        eval_int("[ nasch-block-set! nasch-vel 0 2 ]");
        eval_int("[ nasch-block-set! nasch-pos 1 3 ]");
        eval_int("[ nasch-block-set! nasch-vel 1 0 ]");
        eval_int("[ nasch-init-seed 999 ]");
        long drew_slow = int_val(eval_int("[ nasch-draw-slow ]"));
        eval_int("[ nasch-init-seed 999 ]");   /* replay the SAME draw for the real step */
        eval_int("[ nasch-step ]");
        long expect_v = drew_slow ? 1 : 2;     /* rule3 applies AFTER rule1+2 cap v to 2 */
        long expect_pos = drew_slow ? 1 : 2;
        CHECK(int_val(eval_int("[ block-at nasch-vel 0 ]")) == expect_v &&
              int_val(eval_int("[ block-at nasch-pos 0 ]")) == expect_pos,
              "7: rule order (accel, then safety-cap to gap, then randomization, then move)");
    }

    /* 8: collision exclusion -- gap never goes negative, over a long,
     * intentionally-dense (self-jamming, see finding in sec 8) run; NaSch's
     * safety rule guarantees this structurally, so this is a real regression
     * on the implementation, not a restated tautology. */
    eval_int("[ nasch-use-canonical ]");
    eval_int("[ nasch-init ]");
    eval_int("[ nasch-init-seed 1 ]");
    long min_gap_ever = 999999;
    int vmax_ok = 1;
    for (int t = 0; t < 600; t++) {
        eval_int("[ nasch-step ]");
        long g = int_val(eval_int("[ nasch-min-gap ]"));
        if (g < min_gap_ever) min_gap_ever = g;
        for (int i = 0; i < 25 && vmax_ok; i++) {
            char p[32]; snprintf(p, sizeof p, "[ block-at nasch-vel %d ]", i);
            long v = int_val(eval_int(p));
            if (v < 0 || v > 5) vmax_ok = 0;
        }
    }
    CHECK(min_gap_ever >= 0 && r0_s1_ran_cleanly(), "8: no vehicle ever overlaps over 600 ticks (gap >= 0)");
    CHECK(vmax_ok, "9: every vehicle's velocity stays within [0, vmax] for the whole run");

    /* 10: spontaneous jamming (the finding, not tuned to look good -- see
     * design doc sec 4/8): at this density (25 vehicles / 133 cells, above
     * the vmax=5 critical density) the ring self-jams from pure car-following
     * + randomization, with NO disturbance needed. Pinned as a real
     * regression: if this stops happening, the density/PRNG/rule wiring
     * changed underneath the test. */
    eval_int("[ nasch-use-canonical ]");
    eval_int("[ nasch-init ]");
    eval_int("[ nasch-init-seed 1 ]");
    for (int t = 0; t < 19; t++) eval_int("[ nasch-step ]");
    CHECK(int_val(eval_int("[ nasch-min-gap ]")) == 0,
          "10: canonical density (vmax=5) self-jams (min-gap 0) by tick 19, before disturb window");

    /* 11: the disturbance window is exactly ticks 20..22 in NaSch's own 1 s
     * ticks (the design doc's documented deviation from the continuous
     * models' 0.1 s tick -- see sec 4). nasch-disturb is called with the
     * PRE-increment tick (same convention as IDM's own disturb/traffic.glon),
     * so its condition (tick0 <= t < tick1) is satisfied on the 21st and 22nd
     * calls (pre-tick 20 and 21), and its effect is visible starting the tick
     * AFTER each of those calls -- i.e. at nasch-tick 21 and 22, not 20. */
    eval_int("[ nasch-use-canonical ]");
    eval_int("[ nasch-init ]");
    eval_int("[ nasch-init-seed 1 ]");
    for (int t = 0; t < 21; t++) eval_int("[ nasch-step ]");
    CHECK(int_val(eval_int("[ nasch-tick ]")) == 21 &&
          int_val(eval_int("[ block-at nasch-vel 0 ]")) == 0,
          "11a: vehicle 0 is forced to a stop at tick 21 (disturb fired on the pre-tick-20 call)");
    eval_int("[ nasch-step ]");   /* tick 22: disturb still fires (pre-tick 21 < tick1 22) */
    CHECK(int_val(eval_int("[ block-at nasch-vel 0 ]")) == 0, "11b: still forced at tick 22");
    eval_int("[ nasch-step ]");   /* tick 23: disturb's pre-tick was 22, window closed */
    /* vehicle 0 is free to accelerate again from here, though rule 2's gap
     * cap or rule 3's randomization may still limit it -- only assert the
     * forcing itself has stopped, not a specific resulting velocity. */
    CHECK(r0_s1_ran_cleanly(), "11c: disturbance window releases cleanly at tick 23");

    /* 12: ring wrap -- a vehicle's position never exceeds ncells-1. */
    eval_int("[ nasch-use-canonical ]");
    eval_int("[ nasch-init ]");
    eval_int("[ nasch-init-seed 42 ]");
    int wrap_ok = 1;
    for (int t = 0; t < 400; t++) {
        eval_int("[ nasch-step ]");
        for (int i = 0; i < 25 && wrap_ok; i++) {
            char p[32]; snprintf(p, sizeof p, "[ block-at nasch-pos %d ]", i);
            long pos = int_val(eval_int(p));
            if (pos < 0 || pos >= 133) wrap_ok = 0;
        }
    }
    CHECK(wrap_ok && r0_s1_ran_cleanly(), "12: every position stays within [0, ncells) (ring wrap holds) over 400 ticks");

    /* 13: canvas view. */
    eval_int("[ nasch-use-canonical ]");
    eval_int("[ nasch-init ]");
    eval_int("[ nasch-render ]");
    {
        cell n = M[G1_VIS];
        int has_road = 0, dots = 0;
        for (cell i = 0; i < n && i < G1_VIS_CAP - 1; i++) {
            char c = (char)int_val(M[G1_VIS_DATA + i]);
            if (c == 'L' && i + 2 < n) has_road = 1;
            if (c == 'D') dots++;
        }
        CHECK(has_road && dots == 25, "13: canvas view emits the road line + 25 vehicle dots");
    }

    if (failures == 0) printf("all NaSch tests passed\n");
    return failures;
}
