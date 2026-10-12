/* demos/kaka/kaka_fighter_tests.c -- D12S.12 helmet-route (fighter) tests.
 *
 * Loads the real game + kaka-fighter.glon (NOT the self-test module: the two
 * together exceed the loader heap, so the fighter gets its own harness). Drives
 * the mission functions directly and asserts the checkpoint contract: entry /
 * identity, four-way steering + bounds, the laser (still / steering / boosting),
 * enemy-bolt + obstacle damage, invulnerability, boost, the ONE bomb, the
 * target window, success -> escape -> forest, failure -> cave, the parked
 * forest, and reset coherence.
 */
#include "r0_s1.h"
#include "r0_s1_g1a.h"
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>

#define SEL "reset  view-w: 1000  block-set! rangi F_E 1  block-set! rangi F_X 1120 " \
            "block-set! rangi F_Y 182  block-set! rangi F_VY 0  block-set! rangi F_A 0 " \
            "block-set! rangi F_TARGET 0  krock: 1  rangi-step  krock: 0"
#define ENTER "reset  block-set! rangi F_E 2  fighter-enter "
#define FOE0 "foe: ts-claim K_FOE  block-set! foe F_X 0  block-set! foe F_Y 0  block-set! foe F_C 200 " \
             "block-set! foe F_A 1  block-set! foe F_D 99  block-set! foe F_TARGET 0  block-set! foe F_VY 0 "

static char filebuf[1 << 20], wrapbuf[1 << 20];
static int fails = 0;

static int run_source_impl(const char *src, int reclaim) {
    cell save = M[GC_LOADER_HP];
    int err = 0; cell b = r0_s1_parse(src, &err);
    if (err) { fprintf(stderr, "fighter-test: parse %d in: %s\n", err, src); return -1; }
    r0_s1_run_persistent(b);
    if (reclaim) M[GC_LOADER_HP] = save;
    if (!r0_s1_ran_cleanly()) { fprintf(stderr, "fighter-test: run failed: %s\n", src); return -1; }
    return 0;
}
static int run_source(const char *src) { return run_source_impl(src, 1); }
static long run_int(const char *src) {
    cell save = M[GC_LOADER_HP];
    int err = 0; cell b = r0_s1_parse(src, &err);
    if (err) return -1;
    int n = r0_s1_run_persistent(b);
    M[GC_LOADER_HP] = save;
    if (!r0_s1_ran_cleanly() || n < 1) return -1;
    cell v = r0_s1_result(0, n);
    return (v & 15) == T_INT ? (long)int_val(v) : -1;
}
static int ticks(const char *src, int n) { for (int i = 0; i < n; i++) if (run_source(src)) return -1; return 0; }
static void check(int ok, const char *m) { printf("  %-46s %s\n", m, ok ? "ok" : "FAIL"); if (!ok) fails++; }
static int load_file(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) { fprintf(stderr, "fighter-test: open %s\n", path); return -1; }
    size_t n = fread(filebuf, 1, sizeof filebuf - 1, f); fclose(f); filebuf[n] = 0;
    /* the browser WASM copies each source (comments included) into a 16 KiB
       srcbuf (standalone/glon.c) and glon_load fails at >= 16384 bytes -- which
       stops the whole game booting; this native loader has no such limit */
    if (n >= 16384) { printf("  %-46s FAIL\n", path); fprintf(stderr, "fighter-test: %s is %zu bytes (>= 16384 WASM srcbuf)\n", path, n); fails++; }
    char *r = filebuf, *w = filebuf;
    while (*r) { if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; } *w++ = *r++; }
    *w = 0;
    const char *src = filebuf; char *p = filebuf;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (*p && *p != '[') { size_t len = strlen(filebuf); wrapbuf[0]='['; wrapbuf[1]=' '; memcpy(wrapbuf+2, filebuf, len); wrapbuf[len+2]=' '; wrapbuf[len+3]=']'; wrapbuf[len+4]=0; src=wrapbuf; }
    return run_source_impl(src, 0);
}

int main(void) {
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me; M[M1_CURSOR] = 0; M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++) M[M1_TASK_TABLE + i*M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    if (load_file("glon-lib/prelude.glon")) return 2;
    if (load_file("glon-lib/strings.glon")) return 2;
    if (load_file("demos/kaka/kaka-lib.glon")) return 2;
    if (load_file("demos/kaka/kaka.glon")) return 2;
    if (load_file("demos/kaka/kaka-draw.glon")) return 2;
    if (load_file("demos/kaka/kaka-rangi.glon")) return 2;
    if (load_file("demos/kaka/kaka-wave.glon")) return 2;
    if (load_file("demos/kaka/kaka-fighter.glon")) return 2;

    printf("kaka D12S.12 helmet-route (fighter) tests\n");

    /* A. helmet selection enters fighter mode (cave relic choice -> F_E 2) */
    run_source(SEL);
    check(run_int("block-at rangi F_E") == 2, "A helmet selection enters fighter mode");
    /* B. same Rangi actor id persists across the transition */
    check(run_int("block-at rangi F_ID") == 1, "B same Rangi actor id persists");
    run_source("fighter-step");   /* first tick arms the mission */
    check(run_int("fprog") == 1 && run_int("block-at rangi F_C") == 3 && run_int("fbomb") == 1,
          "B2 mission armed: fprog=1 shield=3 bomb=1");

    /* C. LEFT/RIGHT steer laterally */
    run_source(ENTER "kl: 1  fighter-controls");
    long lx = run_int("block-at rangi F_X");
    run_source(ENTER "kr: 1  fighter-controls");
    long rx = run_int("block-at rangi F_X");
    check(lx < 0 && rx > 0, "C LEFT/RIGHT steer laterally");
    /* D. UP/DOWN steer vertically */
    run_source(ENTER "ku: 1  fighter-controls");
    long uy = run_int("block-at rangi F_Y");
    run_source(ENTER "kd: 1  fighter-controls");
    long dy = run_int("block-at rangi F_Y");
    check(uy < 0 && dy > 0, "D UP/DOWN steer vertically");
    /* E. bounded on every edge */
    run_source(ENTER "kl: 1"); ticks("fighter-controls", 160);
    check(run_int("block-at rangi F_X") == -700, "E lateral clamp left = -700");
    run_source(ENTER "kr: 1"); ticks("fighter-controls", 160);
    check(run_int("block-at rangi F_X") == 700, "E lateral clamp right = 700");
    run_source(ENTER "ku: 1"); ticks("fighter-controls", 160);
    check(run_int("block-at rangi F_Y") == -420, "E vertical clamp up = -420 (above the rim)");
    run_source(ENTER "kd: 1"); ticks("fighter-controls", 60);
    check(run_int("block-at rangi F_Y") == 120, "E vertical clamp down = 120");

    /* F. ROCK fires while stationary and destroys an aligned foe */
    run_source(ENTER FOE0 "krock: 1  fighter-run");
    check(run_int("ts-count K_FOE") == 0 && run_int("score") == 15, "F ROCK destroys an aligned foe when still");
    /* G. ROCK fires while steering */
    run_source(ENTER FOE0 "kl: 1  krock: 1  fighter-run");
    check(run_int("ts-count K_FOE") == 0, "G ROCK fires while steering");
    /* H. ROCK fires while boosting */
    run_source(ENTER FOE0 "kf: 1  krock: 1  fighter-run");
    check(run_int("ts-count K_FOE") == 0 && run_int("block-at rangi F_D") > 0, "H ROCK fires while boosting");
    /* I. a foe outside the cone is not hit (aim matters) */
    run_source(ENTER FOE0 "block-set! foe F_X 175  krock: 1  fighter-run");
    check(run_int("ts-count K_FOE") == 1, "I ROCK misses a foe outside the cone");

    /* J. an enemy bolt damages the player (one shield step) */
    run_source(ENTER FOE0 "block-set! foe F_C 20  block-set! foe F_E 0  block-set! foe F_F 0 "
              "block-set! foe F_VY 20  block-set! foe F_TARGET 5  block-set! rangi F_A 0  fighter-foes 0");
    check(run_int("block-at rangi F_C") == 2, "J enemy bolt damages the player");
    /* J2. a bolt on its own (foe far away, bolt about to arrive) -> reason 5 */
    run_source(ENTER FOE0 "block-set! foe F_C 200  block-set! foe F_E 0  block-set! foe F_F 0 "
              "block-set! foe F_VY 15  block-set! foe F_TARGET 5  block-set! rangi F_A 0  fighter-foes 0");
    check(run_int("block-at rangi F_C") == 2 && run_int("fwhy") == 5, "J2 bolt hit records reason 5 (fighter fire)");
    /* J3. a ram needs lateral AND altitude overlap; it records reason 4 */
    run_source(ENTER FOE0 "block-set! foe F_C 20  block-set! rangi F_A 0  fighter-foes 0");
    check(run_int("block-at rangi F_C") == 2 && run_int("fwhy") == 4, "J3 an overlapping foe rams (reason 4)");
    run_source(ENTER FOE0 "block-set! foe F_C 20  block-set! rangi F_Y 100  block-set! rangi F_A 0  fighter-foes 0");
    check(run_int("block-at rangi F_C") == 3, "J4 a foe passing well below does not ram");
    /* J5. wall scrapes record which wall (fprog 660 centre 150, 420 centre -122) */
    run_source(ENTER "fprog: 660  block-set! rangi F_X 0  block-set! rangi F_A 0  fighter-run");
    long wl = run_int("fwhy");
    run_source(ENTER "fprog: 420  block-set! rangi F_X 0  block-set! rangi F_A 0  fighter-run");
    long wr = run_int("fwhy");
    check(wl == 1 && wr == 2, "J5 wall scrapes record LEFT (1) / RIGHT (2) wall");
    /* K. a gate in a blocked region damages; the gap clears */
    run_source(ENTER "fprog: 318  block-set! rangi F_Y -40  block-set! rangi F_A 0  fighter-gates");
    check(run_int("block-at rangi F_C") == 2 && run_int("fwhy") == 3, "K obstacle gate damages the player (reason 3)");
    run_source(ENTER "fprog: 318  block-set! rangi F_Y 40  block-set! rangi F_A 0  fighter-gates");
    check(run_int("block-at rangi F_C") == 3, "K gate cleared by being in the gap");
    /* K2. every gate pattern: blocked side hits, open side clears
       (518 = pattern 1, 718 = pattern 2 with centre 108, 1118 = pattern 4) */
    run_source(ENTER "fprog: 518  block-set! rangi F_Y 40  block-set! rangi F_A 0  fighter-gates");
    long k1h = run_int("block-at rangi F_C");
    run_source(ENTER "fprog: 518  block-set! rangi F_Y -40  block-set! rangi F_A 0  fighter-gates");
    long k1o = run_int("block-at rangi F_C");
    run_source(ENTER "fprog: 718  block-set! rangi F_X 60  block-set! rangi F_A 0  fighter-gates");
    long k2h = run_int("block-at rangi F_C");
    run_source(ENTER "fprog: 718  block-set! rangi F_X 150  block-set! rangi F_A 0  fighter-gates");
    long k2o = run_int("block-at rangi F_C");
    run_source(ENTER "fprog: 1118  block-set! rangi F_Y 80  block-set! rangi F_A 0  fighter-gates");
    long k4h = run_int("block-at rangi F_C");
    run_source(ENTER "fprog: 1118  block-set! rangi F_Y 0  block-set! rangi F_A 0  fighter-gates");
    long k4o = run_int("block-at rangi F_C");
    check(k1h == 2 && k1o == 3 && k2h == 2 && k2o == 3 && k4h == 2 && k4o == 3,
          "K2 gate patterns 1/2/4 block and open correctly");
    /* L. invulnerability prevents a burst of hits */
    run_source(ENTER "block-set! rangi F_A 10  fighter-hit 3");
    check(run_int("block-at rangi F_C") == 3, "L invulnerability prevents multi-hit");

    /* M0. debrief stats: shots fired, fighters destroyed, boosts used */
    run_source(ENTER FOE0 "krock: 1  fighter-run  krock: 0");
    check(run_int("fshots") == 1 && run_int("fkills") == 1, "M0 a shot and a kill are counted");
    run_source(ENTER "kf: 1"); ticks("fighter-controls", 5);
    run_source("kf: 0"); ticks("fighter-run", 30);
    run_source("kf: 1  fighter-controls  kf: 0");
    check(run_int("fboosts") == 2, "M0b each boost press counts once");

    /* M. JUMP/boost starts the boost */
    run_source(ENTER "kf: 1  fighter-controls");
    check(run_int("block-at rangi F_D") == 24, "M boost starts");
    /* N. boost expires on its own */
    run_source("kf: 0"); ticks("fighter-run", 30);
    check(run_int("block-at rangi F_D") == 0, "N boost expires");

    /* O. the ONE bomb starts available */
    run_source(ENTER);
    check(run_int("fbomb") == 1, "O one bomb available at mission start");
    /* P. the bomb can only be used once */
    run_source("kf: 0  kg: 1  fighter-run  kg: 0  kg: 1  fighter-run");
    check(run_int("fbomb") == 0, "P bomb can only be used once");
    /* Q. an early/missed bomb => the node passes unopposed => failure -> cave */
    run_source(ENTER "fprog: 100  kg: 1  fighter-run  kg: 0");
    run_source("fprog: 1300  fighter-run");
    check(run_int("fbomb") == 0 && run_int("block-at rangi F_STATE") == 3, "Q early bomb use causes later failure");
    run_source("fighter-step");
    check(run_int("block-at rangi F_STATE") == 5 && run_int("block-at rangi F_E") == 2, "Q1 failure freezes into the FAILED debrief");
    /* the debrief holds, then needs FIRE released and pressed again */
    run_source("krock: 1"); ticks("fighter-step", 40);
    check(run_int("block-at rangi F_STATE") == 5, "Q1b a held FIRE cannot skip the debrief");
    run_source("krock: 0  fighter-step  krock: 1  fighter-step  krock: 0");
    check(run_int("block-at rangi F_E") == 1 && run_int("block-at rangi F_X") == 200, "Q2 failure debrief -> FIRE -> SKULL_CAVE");
    /* R. a valid aligned bomb in the window destroys the node -> escape
       (fprog 1020 is a canyon crossover: centre 0) */
    run_source(ENTER "fprog: 1020  block-set! rangi F_X 0  kg: 1  fighter-run  kg: 0  block-set! rangi F_TARGET 90");
    check(run_int("block-at rangi F_STATE") == 1 && run_int("fbomb") == 0 && run_int("score") == 200,
          "R aligned bomb in window destroys the node");
    /* R2. an unaligned bomb in the window does not destroy the node */
    run_source(ENTER "fprog: 1020  block-set! rangi F_X 150  kg: 1  fighter-run  kg: 0");
    check(run_int("block-at rangi F_STATE") == 0, "R2 unaligned bomb misses the node");
    /* S/T. escape ends in success and returns to FOREST */
    run_source(ENTER "fprog: 1020  block-set! rangi F_X 0  kg: 1  fighter-run  kg: 0  block-set! rangi F_TARGET 90");
    ticks("fighter-step", 90);
    check(run_int("block-at rangi F_STATE") == 2, "S target hit enters escape then success");
    run_source("fighter-step");
    check(run_int("block-at rangi F_STATE") == 4 && run_int("block-at rangi F_E") == 2, "S2 success freezes into the COMPLETE debrief");
    long dprog = run_int("fprog");
    ticks("fighter-step", 30);
    check(run_int("fprog") == dprog && run_int("block-at rangi F_STATE") == 4, "S3 gameplay is frozen during the debrief");
    run_source("kf: 1  fighter-step  kf: 0");
    check(run_int("block-at rangi F_E") == 0 && run_int("fprog") == 0, "T success debrief -> FIRE -> FOREST");
    run_source("step  tally");
    check(run_int("over") == 0 && run_int("block-at tc2 0") > 0, "T2 the forest after success has trees and is not over");
    /* V. same actor id persists on the way back */
    check(run_int("block-at rangi F_ID") == 1, "V same Rangi actor persists on return");

    /* W. the forest is parked while the fighter is active */
    run_source("reset  view-w: 640  spawn-pest");
    long wx0 = run_int("block-at ts-t 3 F_X");
    run_source("block-set! rangi F_E 2  fighter-enter  step  step  step");
    check(run_int("block-at ts-t 3 F_X") == wx0 && wx0 > 640 && run_int("block-at rangi F_E") == 2,
          "W forest actors do not move while the fighter is parked");
    /* X. reset clears the fighter back to a fresh FOREST */
    run_source("reset");
    check(run_int("block-at rangi F_E") == 0 && run_int("fprog") == 0 && run_int("fbomb") == 0
          && run_int("ts-count K_FOE") == 0, "X reset returns a coherent fresh game");

    /* ---- open trench: climb out over the Death Star surface ---------------
       rim / surface at F_Y -150; walls + surface solid only below the rim */
    run_source(ENTER "fprog: 660  block-set! rangi F_X 0  block-set! rangi F_Y -200  block-set! rangi F_A 0  fighter-run");
    check(run_int("block-at rangi F_C") == 3, "O1 above the rim the ship crosses over the wall");
    run_source(ENTER "fprog: 660  block-set! rangi F_X 0  block-set! rangi F_Y -140  block-set! rangi F_A 0  fighter-run");
    check(run_int("block-at rangi F_C") == 2 && run_int("fwhy") == 7 && run_int("block-at rangi F_Y") == -160,
          "O2 coming down onto the surface hits it and pops back above");
    run_source(ENTER "fprog: 318  block-set! rangi F_Y -200  block-set! rangi F_A 0  fighter-gates");
    check(run_int("block-at rangi F_C") == 3, "O3 gates cannot hit above the rim");
    run_source(ENTER "fprog: 1020  block-set! rangi F_X 0  block-set! rangi F_Y -200  kg: 1  fighter-run  kg: 0");
    check(run_int("block-at rangi F_STATE") == 0, "O4 the bomb only counts inside the trench");
    run_source(ENTER "fprog: 1020  block-set! rangi F_X 500  block-set! rangi F_Y -300  block-set! rangi F_A 9999");
    ticks("fighter-run", 70);
    long st70 = run_int("block-at rangi F_STATE");
    ticks("fighter-run", 8);
    check(st70 == 0 && run_int("block-at rangi F_STATE") == 3 && run_int("fwhy") == 6,
          "O5 straying > 400 from the trench for 3 s abandons the run");
    run_source(ENTER "fprog: 1020  block-set! rangi F_X 500  block-set! rangi F_Y -300  block-set! rangi F_A 9999");
    ticks("fighter-run", 60);
    run_source("block-set! rangi F_X 0  block-set! rangi F_Y 0  fighter-run");
    check(run_int("fdbr") == 0 && run_int("block-at rangi F_STATE") == 0, "O6 returning to the trench resets the stray clock");

    /* ---- re-entry assist (fprog 660: trench centre 150) --------------------- */
    run_source(ENTER "fprog: 660  block-set! rangi F_X 0  block-set! rangi F_Y -300  block-set! rangi F_A 0  kd: 1");
    ticks("fighter-run", 18);
    long ax = run_int("block-at rangi F_X"), ayy = run_int("block-at rangi F_Y");
    ticks("fighter-run", 20);
    run_source("kd: 0");
    check(ayy < -150 && ax > 130 && ax < 170 && run_int("block-at rangi F_Y") > -150 && run_int("block-at rangi F_C") == 3,
          "A1 holding DOWN above the trench is captured and drops into it");
    run_source(ENTER "fprog: 660  block-set! rangi F_X 0  block-set! rangi F_Y -300  block-set! rangi F_A 9999");
    ticks("fighter-run", 10);
    check(run_int("block-at rangi F_X") == 0, "A2 no pull without DOWN");
    run_source(ENTER "fprog: 660  block-set! rangi F_X 500  block-set! rangi F_Y -400  block-set! rangi F_A 9999  kd: 1");
    ticks("fighter-run", 5);
    run_source("kd: 0");
    check(run_int("block-at rangi F_X") == 500, "A3 no pull outside the 340 capture zone");

    /* ---- winding canyon (D12S.23) -----------------------------------------
       canyon centre = can-x fprog; half-width 110; collision/render share it.
       straight to 60, then a 32-step sine (wavelength 480) x amplitude ramp
       40->150: +67@180, -122@420, +150@660, -145@918, +106@1080. */
    check(run_int("can-x 30") == 0 && run_int("can-x 180") > 0 && run_int("can-x 420") < 0,
          "N1 canyon opens straight, then bends both ways");
    /* exact values: kaka-host.js canX() mirrors can-x and must match these */
    check(run_int("can-x 100") == 24 && run_int("can-x 180") == 67 && run_int("can-x 420") == -122 &&
          run_int("can-x 660") == 150 && run_int("can-x 918") == -145 && run_int("can-x 1080") == 106,
          "N1b can-x matches the host render mirror");
    /* fprog 660 -> centre 150: holding a fixed world x leaves the corridor */
    run_source(ENTER "fprog: 660  block-set! rangi F_X 0  block-set! rangi F_A 0  fighter-run");
    check(run_int("block-at rangi F_C") == 2, "N2 fixed X hits the canyon wall at a bend");
    check(run_int("block-at rangi F_X") == 50, "N2b a wall scrape bounces the ship just inside the wall");
    run_source(ENTER "fprog: 660  block-set! rangi F_X 150  block-set! rangi F_A 0  fighter-run");
    check(run_int("block-at rangi F_C") == 3, "N3 following the bend clears the wall");
    /* fprog 918 -> centre -145, pattern 3 blocks the right half */
    run_source(ENTER "fprog: 918  block-set! rangi F_X -180  block-set! rangi F_A 0  fighter-gates");
    check(run_int("block-at rangi F_C") == 3, "N4 gate follows the local canyon centre");
    run_source(ENTER "fprog: 918  block-set! rangi F_X 120  block-set! rangi F_A 0  fighter-gates");
    check(run_int("block-at rangi F_C") == 2, "N5 gate blocked region is canyon-relative");
    /* fprog 1080 -> centre 106: the node is acquired at the canyon centre */
    run_source(ENTER "fprog: 1080  block-set! rangi F_X 106  kg: 1  fighter-run  kg: 0");
    check(run_int("block-at rangi F_STATE") == 1 && run_int("fbomb") == 0,
          "N6 node follows the canyon centre");

    /* cave relic interaction: reaching a relic (on the rope OR on the pedestal)
       is enough; ROCK confirms. Zone: 130 < F_Y < 260, |F_X - relic| < 64. */
    run_source("reset  block-set! rangi F_E 1  block-set! rangi F_X 1120  block-set! rangi F_Y 182  block-set! rangi F_A 1  krock: 1  rangi-step");
    check(run_int("block-at rangi F_E") == 2, "relic: helmet rope at relic height -> selects helmet");
    run_source("reset  block-set! rangi F_E 1  block-set! rangi F_X 1300  block-set! rangi F_Y 182  block-set! rangi F_A 1  krock: 1  rangi-step");
    check(run_int("block-at rangi F_E") == 3, "relic: phone rope at relic height -> selects phone");
    run_source("reset  block-set! rangi F_E 1  block-set! rangi F_X 1480  block-set! rangi F_Y 182  block-set! rangi F_A 1  krock: 1  rangi-step");
    check(run_int("block-at rangi F_E") == 4, "relic: key rope at relic height -> selects key");
    run_source("reset  block-set! rangi F_E 1  block-set! rangi F_X 1120  block-set! rangi F_Y 182  block-set! rangi F_A 0  krock: 1  rangi-step");
    check(run_int("block-at rangi F_E") == 2, "relic: on the pedestal beside the helmet -> selects");
    run_source("reset  block-set! rangi F_E 1  block-set! rangi F_X 1120  block-set! rangi F_Y 352  block-set! rangi F_A 0  krock: 1  rangi-step");
    check(run_int("block-at rangi F_E") == 1, "relic: cave floor -> does not select");
    run_source("reset  block-set! rangi F_E 1  block-set! rangi F_X 1120  block-set! rangi F_Y 300  block-set! rangi F_A 0  krock: 1  rangi-step");
    check(run_int("block-at rangi F_E") == 1, "relic: below the band -> does not select");
    run_source("reset  block-set! rangi F_E 1  block-set! rangi F_X 1200  block-set! rangi F_Y 182  block-set! rangi F_A 1  krock: 1  rangi-step");
    check(run_int("block-at rangi F_E") == 1, "relic: outside +/-64 -> does not select");
    run_source("reset  block-set! rangi F_E 1  block-set! rangi F_X 1480  block-set! rangi F_Y 182  block-set! rangi F_A 1  krock: 1  rangi-step");
    check(run_int("block-at rangi F_E") == 4 && run_int("block-at rangi F_C") == 3,
          "relic: key zone not overridden by a neighbouring relic");

    if (fails == 0) { printf("kaka-fighter-test PASS\n"); return 0; }
    printf("kaka-fighter-test FAIL (%d)\n", fails);
    return 1;
}
