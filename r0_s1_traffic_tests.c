/* r0_s1_traffic_tests.c - the Glon Alpha IDM ring-road traffic simulator.
 *
 * Headless verification that a deterministic microscopic traffic simulation can
 * be expressed entirely in frozen Glon Alpha (no new native, no new opcode, no
 * floating point, no sqrt). The demo (demo/shop/demos/traffic.glon) implements
 * the Intelligent Driver Model in fixed-point integer arithmetic, keeps 25
 * vehicles on a single-lane ring, advances them with ONE synchronous update per
 * `step`, and brakes vehicle 0 for 1.5 s so a stop-and-go wave propagates
 * backwards around the ring.
 *
 * This test loads the real demo source and drives `step` through the ordinary
 * persistent machine, asserting the physical invariants:
 *   1. initial conditions are even spacing at a uniform speed;
 *   2. before the disturbance the ring is in uniform flow (no collisions);
 *   3. the brake produces a real slowdown that propagates (min speed drops);
 *   4. no vehicle ever overlaps (gap never goes negative);
 *   5. speed stays within [0, v0] (the desired-speed cap holds);
 *   6. the whole run is bit-for-bit deterministic.
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

/* Evaluate one Glon form and return its integer result (0 on failure). Like the
 * G1E test harness, the throw-away form's loader cells are reclaimed. */
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

int run_r0_s1_traffic_tests(void) {
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    M[M1_CURSOR] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    printf("R0-S1 IDM traffic simulator (fixed-point, ring road)\n");

    /* The demo relies on block-at from the shared bootstrap (common.glon), so
     * load the bootstrap first, exactly as the launcher does, then the demo. */
    load_file("demo/shop/bootstrap.glon");
    load_file("demo/shop/demos/traffic.glon");
    CHECK(r0_s1_ran_cleanly(), "1: demo source loads cleanly");

    /* ---- IDM acceleration spot checks (hand-computed fixed point) --------- */
    CHECK(int_val(eval_int("[ idm-accel 1500 1500 3500 ]")) == 44,
          "2: IDM at v=vl=1500, gap=3500 -> 44 cm/s^2");
    CHECK(int_val(eval_int("[ idm-accel 3000 3000 3500 ]")) == -180,
          "3: IDM at v0 on short gap brakes (-180 cm/s^2)");
    CHECK(int_val(eval_int("[ idm-accel 2500 1000 1000 ]")) == -51477,
          "4: IDM approaching slower leader brakes hard");

    /* ---- initial conditions ------------------------------------------------ */
    eval_int("[ init ]");
    CHECK(int_val(eval_int("[ block-at xs 0 ]")) == 0 &&
          int_val(eval_int("[ block-at xs 1 ]")) == 4000 &&
          int_val(eval_int("[ block-at xs 24 ]")) == 96000,
          "5: initial positions evenly spaced (0, 4000, ..., 96000)");
    CHECK(int_val(eval_int("[ block-at vs 0 ]")) == 1500 &&
          int_val(eval_int("[ min-gap ]")) == 3500 &&
          int_val(eval_int("[ mean-v ]")) == 1500,
          "6: initial speed 1500 cm/s, uniform gap 3500 cm");

    /* ---- run 200 ticks to settle into uniform flow, then brake ------------- */
    for (int t = 0; t < 200; t++) eval_int("[ step ]");
    long settle_minv = int_val(eval_int("[ min-v ]"));
    long settle_maxv = int_val(eval_int("[ max-v ]"));
    CHECK(settle_minv == settle_maxv,
          "7: after 200 ticks the ring is in uniform flow (min-v == max-v)");
    CHECK(int_val(eval_int("[ min-gap ]")) == 3500,
          "8: uniform flow has no collisions (min-gap 3500)");

    /* ---- run through the disturbance window and observe the wave ----------- */
    long min_gap_ever = 3500, min_v_ever = settle_minv, max_v_ever = settle_maxv;
    for (int t = 0; t < 400; t++) {
        eval_int("[ step ]");
        long mv = int_val(eval_int("[ min-v ]"));
        long mg = int_val(eval_int("[ min-gap ]"));
        long xv = int_val(eval_int("[ max-v ]"));
        if (mg < min_gap_ever) min_gap_ever = mg;
        if (mv < min_v_ever) min_v_ever = mv;
        if (xv > max_v_ever) max_v_ever = xv;
    }
    CHECK(min_v_ever < settle_minv, "9: the brake propagated (min speed dropped)");
    CHECK(min_gap_ever >= 0, "10: no vehicle ever overlapped (gap >= 0)");
    CHECK(min_gap_ever < 3500, "11: the wave compressed the following gap");
    CHECK(max_v_ever <= 3000, "12: speed never exceeds the desired speed v0");

    /* ---- determinism ------------------------------------------------------- */
    long sig1 = 0;
    eval_int("[ init ]");
    for (int t = 0; t < 600; t++) eval_int("[ step ]");
    for (int i = 0; i < 25; i++) {
        char p[64]; snprintf(p, sizeof p, "[ block-at xs %d ]", i);
        sig1 += int_val(eval_int(p));
        snprintf(p, sizeof p, "[ block-at vs %d ]", i);
        sig1 += int_val(eval_int(p));
    }
    long sig2 = 0;
    eval_int("[ init ]");
    for (int t = 0; t < 600; t++) eval_int("[ step ]");
    for (int i = 0; i < 25; i++) {
        char p[64]; snprintf(p, sizeof p, "[ block-at xs %d ]", i);
        sig2 += int_val(eval_int(p));
        snprintf(p, sizeof p, "[ block-at vs %d ]", i);
        sig2 += int_val(eval_int(p));
    }
    CHECK(sig1 == sig2, "13: two full runs are bit-for-bit deterministic");

    /* ---- visualization ----------------------------------------------------- */
    /* The demo's canvas view draws the ring unrolled as a straight road with
     * one dot per vehicle; verify the emitted visual script headlessly. */
    eval_int("[ init ]");
    eval_int("[ render-traffic ]");
    {
        cell n = M[G1_VIS];
        int has_road = 0, dots = 0;
        for (cell i = 0; i < n && i < G1_VIS_CAP - 1; i++) {
            char c = (char)int_val(M[G1_VIS_DATA + i]);
            if (c == 'L' && i + 2 < n) has_road = 1;
            if (c == 'D') dots++;
        }
        CHECK(has_road && dots == 25,
              "14: canvas view emits the road line + 25 vehicle dots");
    }

    if (failures == 0) printf("all traffic-simulator tests passed\n");
    return failures;
}
