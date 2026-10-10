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
    run_source(ENTER "kl: 1"); ticks("fighter-controls", 60);
    check(run_int("block-at rangi F_X") == -180, "E lateral clamp left = -180");
    run_source(ENTER "kr: 1"); ticks("fighter-controls", 60);
    check(run_int("block-at rangi F_X") == 180, "E lateral clamp right = 180");
    run_source(ENTER "ku: 1"); ticks("fighter-controls", 60);
    check(run_int("block-at rangi F_Y") == -120, "E vertical clamp up = -120");
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
    /* K. a gate in a blocked region damages; the gap clears */
    run_source(ENTER "fprog: 318  block-set! rangi F_Y -40  block-set! rangi F_A 0  fighter-gates");
    check(run_int("block-at rangi F_C") == 2, "K obstacle gate damages the player");
    run_source(ENTER "fprog: 318  block-set! rangi F_Y 40  block-set! rangi F_A 0  fighter-gates");
    check(run_int("block-at rangi F_C") == 3, "K gate cleared by being in the gap");
    /* L. invulnerability prevents a burst of hits */
    run_source(ENTER "block-set! rangi F_A 10  fighter-hit");
    check(run_int("block-at rangi F_C") == 3, "L invulnerability prevents multi-hit");

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
    check(run_int("block-at rangi F_E") == 1 && run_int("block-at rangi F_X") == 200, "Q2 failure returns to SKULL_CAVE");
    /* R. a valid aligned bomb in the window destroys the node -> escape */
    run_source(ENTER "fprog: 900  block-set! rangi F_X 0  kg: 1  fighter-run  kg: 0  block-set! rangi F_TARGET 90");
    check(run_int("block-at rangi F_STATE") == 1 && run_int("fbomb") == 0 && run_int("score") == 200,
          "R aligned bomb in window destroys the node");
    /* R2. an unaligned bomb in the window does not destroy the node */
    run_source(ENTER "fprog: 900  block-set! rangi F_X 150  kg: 1  fighter-run  kg: 0");
    check(run_int("block-at rangi F_STATE") == 0, "R2 unaligned bomb misses the node");
    /* S/T. escape ends in success and returns to FOREST */
    run_source(ENTER "fprog: 900  block-set! rangi F_X 0  kg: 1  fighter-run  kg: 0  block-set! rangi F_TARGET 90");
    ticks("fighter-step", 90);
    check(run_int("block-at rangi F_STATE") == 2, "S target hit enters escape then success");
    run_source("fighter-step");
    check(run_int("block-at rangi F_E") == 0 && run_int("fprog") == 0, "T success returns to FOREST");
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
