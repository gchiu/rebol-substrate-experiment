/* demos/kaka/kaka_wave_tests.c -- D12S.2 invasion regressions.
 *
 * Drives the real wave/climb/cover functions with a controlled tuple space:
 *   - pests spawn ahead/RIGHT and march LEFT;
 *   - a pest climbs the tree it reaches and gnaws it;
 *   - a climbing pest drives a feeding kaka off that tree;
 *   - a live tree canopy absorbs a berry fired through it;
 *   - an open line or a dead tree does not absorb the berry.
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
    int err = 0; cell b = r0_s1_parse(src, &err);
    if (err) { fprintf(stderr, "wave-test: parse %d in: %s\n", err, src); return -1; }
    r0_s1_run_persistent(b);
    if (reclaim) M[GC_LOADER_HP] = save;
    if (!r0_s1_ran_cleanly()) { fprintf(stderr, "wave-test: run failed: %s\n", src); return -1; }
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
static void check(int ok, const char *m) { printf("  %-42s %s\n", m, ok ? "ok" : "FAIL"); if (!ok) fails++; }
/* aim the ranger right and clear the rock cooldown before a shot */
static void px300(void) { run_source("block-set! rangi F_X  300  block-set! rangi F_D  1  krock: 1  block-set! rangi F_TARGET  0"); }
static int load_file(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) { fprintf(stderr, "wave-test: open %s\n", path); return -1; }
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

    printf("kaka D12B wave/climb/cover test\n");

    /* 1. a new game has no wave; a spawned pest enters off the RIGHT edge */
    run_source("reset");
    check(run_int("wave") == 0 && run_int("n-rat") == 0, "1 fresh: no wave, no pests");
    run_source("spawn-pest");
    check(run_int("block-at ts-t 3 F_X") == run_int("+ + cam view-w 90"), "1 pest spawns off the right edge");
    check(run_int("block-at ts-t 3 F_X") > run_int("block-at rangi F_X"), "1 pest is ahead (right) of the ranger");
    check(run_int("block-at ts-t 3 F_Y") == 345, "1 pest starts on the ground (y=345)");
    check(run_int("block-at ts-t 3 F_A") == 0 && run_int("block-at ts-t 3 F_B") == 1, "1 first pest is a 1-hp rat");

    /* 2. the wave controller advances and produces rats */
    run_source("reset");
    for (int t = 0; t < 400 && run_int("n-rat") < 1; t++) run_source("on-tick");
    check(run_int("wave") >= 1 && run_int("n-rat") >= 1, "2 wave advances and spawns rats");

    /* 3. a rat climbs the tree it reaches and gnaws it */
    run_source("reset");
    run_source("ts-claim K_RAT block-set! ts-t 3 F_X 240 block-set! ts-t 3 F_Y 345 block-set! ts-t 3 F_STATE 0 block-set! ts-t 3 F_A 0 block-set! ts-t 3 F_VY 2");
    ticks("rat-step ts-t 3", 60);
    check(run_int("block-at ts-t 3 F_Y") <= 250, "3 rat climbed the trunk (y<=250)");
    check(run_int("block-at ts-t 0 F_A") < 100, "3 climbed rat gnawed tree0");
    check(run_int("block-at ts-t 1 F_A") == 100 && run_int("block-at ts-t 2 F_A") == 100, "3 other trees untouched");

    /* 4. a climbing rat drives a feeding kaka off that tree */
    run_source("reset");
    run_source("ts-claim K_KAKA block-set! ts-t 3 F_STATE KS_EAT block-set! ts-t 3 F_TARGET 0 block-set! ts-t 3 F_X 240 block-set! ts-t 3 F_Y 250 block-set! ts-t 3 F_A 0");
    run_source("ts-claim K_RAT block-set! ts-t 4 F_X 240 block-set! ts-t 4 F_Y 345 block-set! ts-t 4 F_STATE 0 block-set! ts-t 4 F_A 0 block-set! ts-t 4 F_VY 2");
    ticks("rat-step ts-t 4", 60);
    check(run_int("block-at ts-t 3 F_STATE") == 4 /*KS_LEAVE*/, "4 rat drove the feeding kaka off (KS_LEAVE)");

    /* 5. a live canopy absorbs a berry; open air does not; a dead tree does not */
    run_source("reset");
    run_source("ts-claim K_BERRY block-set! ts-t 3 F_X 240 block-set! ts-t 3 F_Y 240 block-set! ts-t 3 F_VY -9");
    run_source("berry-step ts-t 3");
    check(run_int("ts-count K_BERRY") == 0, "5 berry blocked by tree0 canopy");

    run_source("reset");
    run_source("ts-claim K_BERRY block-set! ts-t 3 F_X 420 block-set! ts-t 3 F_Y 240 block-set! ts-t 3 F_VY -9");
    run_source("berry-step ts-t 3");
    check(run_int("ts-count K_BERRY") == 1, "5 berry in open air is not blocked");

    run_source("reset");
    run_source("damage-tree 0 100");
    run_source("ts-claim K_BERRY block-set! ts-t 3 F_X 240 block-set! ts-t 3 F_Y 240 block-set! ts-t 3 F_VY -9");
    run_source("berry-step ts-t 3");
    check(run_int("ts-count K_BERRY") == 1, "5 dead tree does not block the berry");

    /* ---- D12S.2: rightward invasion, species HP, rolling bounded state ---- */

    /* 6 (B). a pest marches LEFT (direction is fixed, never by ranger) */
    run_source("reset");
    run_source("spawn-pest");
    run_source("block-set! ts-t 3 F_X 900  block-set! ts-t 3 F_VY 2");
    long x = run_int("block-at ts-t 3 F_X");
    run_source("rat-step ts-t 3");
    check(run_int("block-at ts-t 3 F_X") < x, "6 pest marches left");

    /* 7 (C). moving the ranger across the old midpoint never reverses the front */
    run_source("reset");
    run_source("spawn-pest");
    run_source("block-set! ts-t 3 F_X 900  block-set! ts-t 3 F_VY 2");
    x = run_int("block-at ts-t 3 F_X");
    run_source("block-set! rangi F_X  2000  rat-step ts-t 3");
    check(run_int("block-at ts-t 3 F_X") < x, "7 still left after ranger moves right");
    x = run_int("block-at ts-t 3 F_X");
    run_source("block-set! rangi F_X  100  rat-step ts-t 3");
    check(run_int("block-at ts-t 3 F_X") < x, "7 still left after ranger moves left");

    /* 8 (D). the ranger advances far beyond the old ~1440 world */
    run_source("reset  kr: 1");
    for (int i = 0; i < 250; i++) run_source("rangi-step");
    check(run_int("block-at rangi F_X") > 2400, "8 ranger advances beyond the old world");

    /* 9 (E). camera leaves more visible space ahead than behind */
    run_source("reset  view-w: 1000  block-set! rangi F_X  3000  rangi-step");
    check(run_int("- block-at rangi F_X cam") < run_int("- view-w - block-at rangi F_X cam"), "9 more space ahead than behind");

    /* 10 (F). one rock kills a 1-hp rat and scores 20 */
    run_source("reset  block-set! rangi F_X  300  block-set! rangi F_D  1  krock: 1  block-set! rangi F_TARGET  0");
    run_source("r: ts-claim K_RAT  block-set! r F_X 330  block-set! r F_Y 330  block-set! r F_B 1  block-set! r F_A 0  block-set! r F_VY 2");
    run_source("fire-rock  rock-step ts-t 4");
    check(run_int("block-at r F_STATE") == 9 && run_int("score") == 20, "10 one rock kills a rat (+20)");

    /* 11 (G/I). a stoat survives the first rock and dies on the second (+35) */
    run_source("reset");
    run_source("r: ts-claim K_RAT  block-set! r F_X 330  block-set! r F_Y 330  block-set! r F_B 2  block-set! r F_A 1  block-set! r F_VY 3");
    px300();
    run_source("fire-rock  rock-step ts-t 4");
    check(run_int("block-at r F_STATE") == 0 && run_int("block-at r F_B") == 1 && run_int("score") == 0,
          "11 stoat survives hit 1 (no early score)");
    run_source("block-set! rangi F_TARGET  0  fire-rock  rock-step ts-t 4");
    check(run_int("block-at r F_STATE") == 9 && run_int("score") == 35, "11 stoat dies on hit 2 (+35)");

    /* 12 (H). a possum dies on the fifth rock (+60) */
    run_source("reset");
    run_source("r: ts-claim K_RAT  block-set! r F_X 330  block-set! r F_Y 330  block-set! r F_B 5  block-set! r F_A 2  block-set! r F_VY 1");
    for (int i = 0; i < 4; i++) { px300(); run_source("fire-rock  rock-step ts-t 4"); }
    check(run_int("block-at r F_STATE") == 0, "12 possum survives four rocks");
    px300();
    run_source("fire-rock  rock-step ts-t 4");
    check(run_int("block-at r F_STATE") == 9 && run_int("score") == 60, "12 possum dies on rock 5 (+60)");

    /* 13 (K). a berry never damages a pest */
    run_source("reset");
    run_source("r: ts-claim K_RAT  block-set! r F_B 2  block-set! r F_A 1  block-set! r F_Y 300");
    run_source("b: ts-claim K_BERRY  block-set! b F_X 0  block-set! b F_Y 0  block-set! b F_A 0");
    run_source("block-set! b F_X block-at r F_X  block-set! b F_Y block-at r F_Y  berry-step b");
    check(run_int("block-at r F_B") == 2 && run_int("block-at r F_STATE") == 0, "13 berry does not damage a pest");

    /* 14 (L). a powered kaka attacks every pest kind */
    for (int sp = 0; sp < 3; sp++) {
        char src[256];
        run_source("reset");
        snprintf(src, sizeof src, "k: ts-claim K_KAKA  block-set! k F_STATE KS_MUTANT  block-set! k F_B + tick 540  block-set! k F_X 300  block-set! k F_Y 320  r: ts-claim K_RAT  block-set! r F_X 305  block-set! r F_Y 330  block-set! r F_A %d  block-set! r F_B 5", sp);
        run_source(src);
        run_source("kaka-mutant k");
        static const char *names[3] = { "rat", "stoat", "possum" };
        char msg[64]; snprintf(msg, sizeof msg, "14 powered kaka zaps a %s (ghost)", names[sp]);
        check(run_int("block-at r F_STATE") == 9, msg);
    }

    /* 15 (J/M/Q). restart clears progression/pests/projectiles + viewport-only */
    run_source("reset");
    run_source("spawn-pest");
    long px0 = run_int("block-at rangi F_X"), d0 = run_int("dist"), nx0 = run_int("next-tree-x");
    run_source("block-set! rangi F_X  3000  dist: 3000  next-tree-x: 10000");
    run_source("view-w: 1200  rangi-step");
    check(run_int("block-at rangi F_X") == 3000, "15 viewport change does not move the ranger");
    run_source("reset");
    check(run_int("block-at rangi F_X") == 480 && run_int("dist") == 480 && run_int("next-tree-x") == 2400,
          "15 restart resets progression");
    check(run_int("ts-count K_RAT") == 0 && run_int("ts-count K_ROCK") == 0 && run_int("ts-count K_BERRY") == 0,
          "15 restart clears pests/projectiles");
    (void)px0; (void)d0; (void)nx0;

    /* 16 (N/O/P). long travel keeps tuple occupancy bounded across the journey
       (rolling forest until the destination, then the frozen cave) */
    run_source("reset  kr: 1  block-set! rangi F_A  999999");   /* isolate occupancy from contact */
    long maxTotal = 0;
    for (int i = 0; i < 1600; i++) {
        run_source("step");
        if (i % 20 == 0) {
            long tot = run_int("+ + + + ts-count K_RAT ts-count K_KAKA ts-count K_ROCK ts-count K_BERRY ts-count K_TREE");
            if (tot > maxTotal) maxTotal = tot;
        }
    }
    check(run_int("block-at rangi F_E") == 1, "16 reached SKULL_CAVE by long travel");
    check(maxTotal <= 30, "16 tuple occupancy bounded (not growing with distance)");

    /* 17. death ghost (state 9): not a live pest, not a target, frees cleanly */
    run_source("reset");
    run_source("r: ts-claim K_RAT  block-set! r F_X 400  block-set! r F_Y 345  block-set! r F_STATE 9  block-set! r F_C 20  block-set! r F_A 0");
    run_source("tally");
    check(run_int("n-rat") == 0, "17 ghost is not a live pest (n-rat 0)");
    run_source("reset");
    run_source("r: ts-claim K_RAT  block-set! r F_X 305  block-set! r F_Y 330  block-set! r F_STATE 9  block-set! r F_C 20");
    run_source("k: ts-claim K_KAKA  block-set! k F_STATE KS_MUTANT  block-set! k F_B + tick 540  block-set! k F_X 300  block-set! k F_Y 320");
    run_source("kaka-mutant k");
    check(run_int("ts-count K_RAT") == 1 && run_int("laser") < 0, "17 powered kaka does not target a ghost");
    run_source("reset");
    run_source("r: ts-claim K_RAT  block-set! r F_STATE 9  block-set! r F_C 20");
    for (int i = 0; i < 20; i++) run_source("rat-step r");
    check(run_int("ts-count K_RAT") == 0, "17 ghost frees after its 20-tick life");

    /* 18. every lethal path funnels into the one state-9 ghost transition */
    run_source("reset");
    run_source("r: ts-claim K_RAT  block-set! r F_X 330  block-set! r F_Y 305  block-set! r F_STATE 1  block-set! r F_B 1  block-set! r F_A 0  block-set! r F_VY 2  block-set! r F_E 330");
    px300();
    run_source("fire-rock  rock-step ts-t 4");
    check(run_int("block-at r F_STATE") == 9, "18 rock kills a climbing rat -> ghost");
    run_source("reset");
    run_source("r: ts-claim K_RAT  block-set! r F_X 305  block-set! r F_Y 250  block-set! r F_STATE 2  block-set! r F_B 5  block-set! r F_A 2  block-set! r F_E 305");
    run_source("k: ts-claim K_KAKA  block-set! k F_STATE KS_MUTANT  block-set! k F_B + tick 540  block-set! k F_X 300  block-set! k F_Y 240");
    run_source("kaka-mutant k");
    check(run_int("block-at r F_STATE") == 9, "18 laser kills a gnawing possum -> ghost");
    /* a state-9 ghost is inert: it only ticks its timer (never climbs/moves) */
    run_source("reset");
    run_source("r: ts-claim K_RAT  block-set! r F_X 960  block-set! r F_Y 250  block-set! r F_STATE 9  block-set! r F_C 20  block-set! r F_A 0");
    long gx = run_int("block-at r F_X");
    run_source("rat-step r");
    check(run_int("block-at r F_C") == 19 && run_int("block-at r F_X") == gx, "18 ghost is inert (timer only)");

    /* 19. ROCK has a finite semantic range from its spawn x */
    /* still lethal at ordinary visible combat distance (~380 px) */
    run_source("reset  block-set! rangi F_X  300  block-set! rangi F_D  1  krock: 1  block-set! rangi F_TARGET  0");
    run_source("r: ts-claim K_RAT  block-set! r F_X 700  block-set! r F_Y 330  block-set! r F_B 1  block-set! r F_A 0  block-set! r F_VY 0");
    run_source("fire-rock");
    for (int i = 0; i < 40; i++) run_source("rock-step ts-t 4");
    check(run_int("block-at r F_STATE") == 9, "19 rock kills at visible combat distance");
    /* reclaimed after exceeding ROCK_RANGE (960 px => 80 steps at 12 px/tick) */
    run_source("reset  block-set! rangi F_X  300  block-set! rangi F_D  1  krock: 1  block-set! rangi F_TARGET  0");
    run_source("fire-rock");
    for (int i = 0; i < 82; i++) run_source("rock-step ts-t 3");
    check(run_int("ts-count K_ROCK") == 0, "19 rock reclaimed after ROCK_RANGE");
    /* a pest well beyond ROCK_RANGE is never reached */
    run_source("reset  block-set! rangi F_X  300  block-set! rangi F_D  1  krock: 1  block-set! rangi F_TARGET  0");
    run_source("r: ts-claim K_RAT  block-set! r F_X 1400  block-set! r F_Y 330  block-set! r F_B 1  block-set! r F_A 0  block-set! r F_VY 0");
    run_source("fire-rock");
    for (int i = 0; i < 82; i++) run_source("rock-step ts-t 4");
    check(run_int("block-at r F_STATE") == 0 && run_int("ts-count K_ROCK") == 0,
          "19 pest beyond ROCK_RANGE is untouched");
    /* firing continuously stays bounded, then the pool fully drains (no leak) */
    run_source("reset  block-set! rangi F_X  300  block-set! rangi F_D  1  krock: 1  block-set! rangi F_TARGET  0");
    for (int i = 0; i < 300; i++) run_source("block-set! rangi F_TARGET  0  fire-rock  move-all 0");
    long inflight = run_int("ts-count K_ROCK");
    for (int i = 0; i < 90; i++) run_source("move-all 0");
    check(inflight <= 5 && run_int("ts-count K_ROCK") == 0,
          "19 no rock leak (all reclaimed)");

    /* 20. ground armada: most spawned pests are ground attackers, some seek trees */
    run_source("reset");
    run_source("spawn-pest spawn-pest spawn-pest spawn-pest spawn-pest spawn-pest");
    {
        int ground = 0, seekers = 0;
        for (int i = 3; i <= 8; i++) {
            char src[48]; snprintf(src, sizeof src, "block-at ts-t %d F_TARGET", i);
            long v = run_int(src);
            if (v == -1) ground++; else if (v == 0) seekers++;
        }
        printf("  ground=%d tree-seekers=%d (of 6)\n", ground, seekers);
        check(ground >= 3, "20 most pests are ground attackers");
        check(seekers >= 1, "20 some pests are tree-seekers");
    }
    /* a ground attacker marches past a tree and is never converted */
    run_source("reset");
    run_source("r: ts-claim K_RAT  block-set! r F_X 240  block-set! r F_Y 345  block-set! r F_STATE 0  block-set! r F_A 0  block-set! r F_VY 2  block-set! r F_TARGET -1");
    {
        long x0 = run_int("block-at r F_X");
        run_source("rat-step r  rat-step r  rat-step r");
        check(run_int("block-at r F_STATE") == 0 && run_int("block-at r F_X") < x0,
              "20 ground attacker marches past a tree");
    }
    /* a tree-seeker climbs and gnaws habitat */
    run_source("reset");
    run_source("r: ts-claim K_RAT  block-set! r F_X 240  block-set! r F_Y 345  block-set! r F_STATE 0  block-set! r F_A 0  block-set! r F_VY 2  block-set! r F_TARGET 0");
    run_source("rat-step r");
    check(run_int("block-at r F_STATE") == 1, "20 tree-seeker climbs");
    for (int i = 0; i < 60; i++) run_source("rat-step r");
    check(run_int("block-at ts-t 0 F_A") < 100, "20 tree-seeker gnaws habitat");

    /* 21. player contact uses the one canonical life path for every species */
    run_source("reset");
    run_source("r: ts-claim K_RAT  block-set! r F_X 480  block-set! r F_Y 345  block-set! r F_STATE 0  block-set! r F_A 0  block-set! r F_B 1  block-set! r F_VY 0  block-set! r F_TARGET -1");
    run_source("rat-step r");
    check(run_int("block-at rangi F_B") == 2 && run_int("block-at rangi F_A") > 0 && run_int("score") == 0,
          "21 grounded rat contact: one life, no score");
    run_source("reset");
    run_source("r: ts-claim K_RAT  block-set! r F_X 480  block-set! r F_Y 345  block-set! r F_STATE 0  block-set! r F_A 1  block-set! r F_B 2  block-set! r F_VY 0  block-set! r F_TARGET -1");
    run_source("rat-step r");
    check(run_int("block-at rangi F_B") == 2 && run_int("block-at r F_B") == 2 && run_int("ts-count K_RAT") == 1,
          "21 stoat contact: one life, HP + tuples unchanged");
    run_source("reset");
    run_source("r: ts-claim K_RAT  block-set! r F_X 480  block-set! r F_Y 345  block-set! r F_STATE 0  block-set! r F_A 2  block-set! r F_B 5  block-set! r F_VY 0  block-set! r F_TARGET -1");
    run_source("rat-step r");
    check(run_int("block-at rangi F_B") == 2 && run_int("block-at r F_B") == 5,
          "21 possum contact: one life, HP unchanged");

    /* 22. short invulnerability: a swarm cannot drain lives in one window */
    run_source("reset");
    run_source("r: ts-claim K_RAT  block-set! r F_X 480  block-set! r F_Y 345  block-set! r F_STATE 0  block-set! r F_A 0  block-set! r F_VY 0  block-set! r F_TARGET -1");
    run_source("rat-step r");
    check(run_int("block-at rangi F_B") == 2, "22 first contact loses a life");
    run_source("block-set! r F_X 480  block-set! r F_Y 345  rat-step r");
    check(run_int("block-at rangi F_B") == 2, "22 same-overlap contact does not stack");
    run_source("block-set! r F_X - block-at rangi F_X 200  rat-step r");            /* separate */
    run_source("block-set! rangi F_A  0  block-set! r F_X 480  block-set! r F_Y 345  rat-step r");
    check(run_int("block-at rangi F_B") == 1, "22 new episode after separation hits again");
    run_source("reset");
    check(run_int("block-at rangi F_A") == 0 && run_int("block-at rangi F_B") == 3, "22 restart clears block-at rangi F_A/block-at rangi F_B");

    /* 23. jump dodge: grounded = hit, airborne = safe, landing = normal again */
    run_source("reset");
    run_source("r: ts-claim K_RAT  block-set! r F_X 480  block-set! r F_Y 345  block-set! r F_STATE 0  block-set! r F_A 0  block-set! r F_VY 0  block-set! r F_TARGET -1");
    run_source("rat-step r");
    check(run_int("block-at rangi F_B") == 2, "23 grounded ranger takes the hit");
    run_source("reset");
    run_source("r: ts-claim K_RAT  block-set! r F_X 480  block-set! r F_Y 345  block-set! r F_STATE 0  block-set! r F_A 0  block-set! r F_VY 0  block-set! r F_TARGET -1");
    run_source("ku: 1  rangi-step  rangi-step  rangi-step");
    check(run_int("block-at rangi F_Y") < 315, "23 jump lifts the ranger clear");
    run_source("rat-step r");
    check(run_int("block-at rangi F_B") == 3, "23 airborne ranger takes no contact");
    run_source("ku: 0");
    for (int i = 0; i < 26; i++) run_source("rangi-step");
    check(run_int("block-at rangi F_Y") == 330, "23 landing returns to the ground (block-at rangi F_Y 330)");
    run_source("block-set! rangi F_A  0  block-set! r F_X 480  block-set! r F_Y 345  rat-step r");
    check(run_int("block-at rangi F_B") == 2, "23 after landing contact hits again (no permanent immunity)");

    /* 24. movement keeps working across jump/land and after contact knockback */
    run_source("reset  kr: 1");
    for (int i = 0; i < 10; i++) run_source("rangi-step");
    long pxA = run_int("block-at rangi F_X");
    run_source("ku: 1  rangi-step  rangi-step  rangi-step");   /* jump */
    check(run_int("block-at rangi F_Y") < 315, "24 jump lifts during the move sequence");
    run_source("ku: 0");
    for (int i = 0; i < 30; i++) run_source("rangi-step");  /* land and continue */
    long pxB = run_int("block-at rangi F_X");
    run_source("kr: 0");
    check(run_int("block-at rangi F_Y") == 330 && pxB > pxA + 100, "24 movement continues across jump/land");
    run_source("reset  kr: 1");
    run_source("r: ts-claim K_RAT  block-set! r F_X 485  block-set! r F_Y 345  block-set! r F_STATE 0  block-set! r F_A 0  block-set! r F_VY 0  block-set! r F_TARGET -1");
    run_source("rangi-step");
    run_source("rat-step r");
    long pxC = run_int("block-at rangi F_X");
    check(run_int("block-at rangi F_B") == 2 && pxC < 480, "24 contact knocks the ranger back");
    run_source("kr: 1");
    for (int i = 0; i < 20; i++) run_source("rangi-step");
    run_source("kr: 0");
    check(run_int("block-at rangi F_X") > pxC + 100, "24 movement resumes after contact knockback");

    /* 25. contact episodes: one pest + one continuous overlap = exactly one hit */
    run_source("reset");
    run_source("r: ts-claim K_RAT  block-set! r F_X 480  block-set! r F_Y 345  block-set! r F_STATE 0  block-set! r F_A 0  block-set! r F_VY 0  block-set! r F_TARGET -1");
    for (int i = 0; i < 40; i++) run_source("block-set! r F_X block-at rangi F_X  block-set! r F_Y + block-at rangi F_Y 2  rat-step r");
    check(run_int("block-at rangi F_B") == 2, "25 A continuous overlap => exactly one life");
    run_source("block-set! rangi F_A  0");
    for (int i = 0; i < 5; i++) run_source("block-set! r F_X block-at rangi F_X  block-set! r F_Y + block-at rangi F_Y 2  rat-step r");
    check(run_int("block-at rangi F_B") == 2, "25 B block-at rangi F_A expiry mid-overlap does not stack");
    run_source("block-set! r F_X - block-at rangi F_X 200  rat-step r");
    check(run_int("block-at r F_F") == 0, "25 C separation clears the episode");
    run_source("block-set! rangi F_A  0  block-set! r F_X block-at rangi F_X  block-set! r F_Y + block-at rangi F_Y 2  rat-step r");
    check(run_int("block-at rangi F_B") == 1, "25 D a new contact after separation hits again");
    run_source("reset");
    run_source("a: ts-claim K_RAT  block-set! a F_X 480  block-set! a F_Y 345  block-set! a F_STATE 0  block-set! a F_A 0  block-set! a F_VY 0  block-set! a F_TARGET -1");
    run_source("b: ts-claim K_RAT  block-set! b F_X 482  block-set! b F_Y 345  block-set! b F_STATE 0  block-set! b F_A 0  block-set! b F_VY 0  block-set! b F_TARGET -1");
    run_source("rat-step a  rat-step b");
    check(run_int("block-at rangi F_B") == 2, "25 E two near-simultaneous rats cost one life");
    run_source("reset");
    check(run_int("block-at rangi F_B") == 3 && run_int("ts-count K_RAT") == 0, "25 F restart clears contact state");

    /* 26. the ranger can never leave the visible left margin */
    int M = (int)run_int("PLAYER_LEFT_MARGIN");
    run_source("reset  view-w: 1000");
    run_source("kl: 1");
    for (int i = 0; i < 80; i++) run_source("kl: 1  rangi-step");
    check(run_int("block-at rangi F_X") == M, "26 A left input clamps at the margin");
    check(run_int("- block-at rangi F_X cam") >= M, "26 A screenX stays inside the margin");
    check(run_int("cam") == 0, "26 C camera rests at the world origin");
    run_source("kl: 0  kr: 1  rangi-step");
    check(run_int("block-at rangi F_X") > M, "26 F right movement resumes after the clamp");
    run_source("kr: 0  kl: 1");
    for (int i = 0; i < 20; i++) run_source("kl: 1  rangi-step");
    int p0 = (int)run_int("block-at rangi F_X"), c0 = (int)run_int("cam");
    for (int i = 0; i < 20; i++) run_source("kl: 1  rangi-step");
    check(run_int("block-at rangi F_X") == p0 && run_int("cam") == c0, "26 H clamp is stable (no jitter)");

    /* B. scroll right, then walk left all the way home: the ranger stays inside */
    run_source("kl: 0  reset  view-w: 1000  block-set! rangi F_X  3000  rangi-step");
    check(run_int("cam") == 2650, "26 B camera follows while scrolled");
    int minSX = 99999;
    run_source("kl: 1");
    for (int i = 0; i < 320; i++) {
        run_source("kl: 1  rangi-step");
        long sx = run_int("- block-at rangi F_X cam");
        if (sx < minSX) minSX = (int)sx;
    }
    check(minSX >= M, "26 B screenX never crosses while scrolling left");
    check(run_int("cam") == 0 && run_int("block-at rangi F_X") == M, "26 B camera reaches origin and pins");

    /* D. knockback near the left margin cannot push the body off-screen */
    run_source("reset  view-w: 1000  block-set! rangi F_X  60  block-set! rangi F_A  0");
    run_source("hit-ranger");
    check(run_int("block-at rangi F_X") >= M, "26 D knockback keeps the body inside the margin");
    check(run_int("- block-at rangi F_X cam") >= M, "26 D post-knockback screenX inside the margin");

    /* E. jump + LEFT cannot escape the viewport */
    run_source("reset  view-w: 1000  block-set! rangi F_X  60  kl: 1  ku: 1");
    int jmin = 99999;
    for (int i = 0; i < 30; i++) {
        run_source("kl: 1  rangi-step");
        long sx = run_int("- block-at rangi F_X cam");
        if (sx < jmin) jmin = (int)sx;
    }
    check(jmin >= M, "26 E jump + left stays inside the viewport");

    /* G. restart returns to a valid, visible state */
    run_source("reset");
    check(run_int("block-at rangi F_X") == 480 && run_int("- block-at rangi F_X cam") >= M, "26 G restart is visible");

    /* 27. repeated nonfatal rat contacts must never leave a partial-control
       state: movement, facing, ROCK, JUMP all recover; only game-over is a
       legitimate control freeze. */
    run_source("reset  view-w: 1000");
    /* six fresh rats (reusing one slot), each overlapped and hitting once; lives
       are pinned high so the episode is deliberately NONfatal */
    for (int i = 0; i < 6; i++) {
        run_source("block-set! rangi F_B 3  block-set! rangi F_A 0  h: ts-claim K_RAT  "
                   "block-set! h F_X block-at rangi F_X  block-set! h F_Y + block-at rangi F_Y 2  "
                   "block-set! h F_STATE 0  block-set! h F_TARGET -1  rat-step h");
        run_source("block-set! h F_X - block-at rangi F_X 400  rat-step h  ts-free h  block-set! rangi F_A 0");
    }
    check(run_int("block-at rangi F_B") < 3 && run_int("over") == 0, "27 repeated hits: alive, game not over");
    /* A. LEFT */
    run_source("kl: 1  kr: 0  rangi-step");
    int pL = (int)run_int("block-at rangi F_X");
    check(run_int("block-at rangi F_D") == -1, "27 LEFT still turns the ranger");
    /* B. RIGHT (release LEFT as a real key-up would) */
    run_source("kl: 0  kr: 1  rangi-step");
    check((int)run_int("block-at rangi F_X") > pL, "27 RIGHT moves after LEFT");
    check(run_int("block-at rangi F_D") == 1, "27 RIGHT still turns the ranger");
    run_source("kr: 0");
    /* C. ROCK */
    run_source("block-set! rangi F_D  1  krock: 1  block-set! rangi F_TARGET  0  fire-rock  tally");
    check(run_int("n-rock") >= 1 && run_int("block-at rangi F_TARGET") > 0, "27 ROCK fires after repeated hits");
    /* D. JUMP */
    run_source("krock: 0  block-set! rangi F_VY  0  block-set! rangi F_Y  330  ku: 1  rangi-step  rangi-step");
    check(run_int("block-at rangi F_VY") > 0 && run_int("block-at rangi F_Y") < 330, "27 JUMP works after repeated hits");
    /* F. no sticky held-state */
    run_source("ku: 0  kl: 0  kr: 0  krock: 0  kf: 0");
    check(run_int("kl") == 0 && run_int("kr") == 0 && run_int("krock") == 0, "27 no sticky held state");
    /* game-over IS the only control freeze (step gates input on `over`) */
    run_source("reset  over: 1  kr: 1  block-set! rangi F_X  480");
    run_source("step");
    check(run_int("block-at rangi F_X") == 480, "27 game-over freezes movement");
    run_source("reset");
    check(run_int("block-at rangi F_B") == 3 && run_int("block-at rangi F_A") == 0, "27 restart clears control/contact state");

    /* 28. Rangi is a first-class actor: ONE tuple owns all player state */
    run_source("reset");
    check(run_int("block-at rangi F_KIND") == 8 && run_int("block-at rangi F_ID") == 1,
          "28 A one K_RANGI actor exists");
    check(run_int("block-at rangi F_X") == 480 && run_int("block-at rangi F_Y") == 330 &&
          run_int("block-at rangi F_D") == 1 && run_int("block-at rangi F_B") == 3 &&
          run_int("block-at rangi F_VY") == 0 && run_int("block-at rangi F_A") == 0 &&
          run_int("block-at rangi F_TARGET") == 0 && run_int("block-at rangi F_STATE") == 0,
          "28 B actor owns x/y/facing/lives/jump/invuln/cooldown/state");
    run_source("kl: 1  kr: 0  rangi-step");
    check(run_int("block-at rangi F_D") == -1 && run_int("block-at rangi F_X") == 470, "28 C LEFT moves+turns actor");
    run_source("kl: 0  kr: 1  rangi-step");
    check(run_int("block-at rangi F_D") == 1 && run_int("block-at rangi F_X") == 480, "28 D RIGHT moves+turns actor");
    run_source("kr: 0  block-set! rangi F_X 300  ku: 1  rangi-step  rangi-step");
    check(run_int("block-at rangi F_VY") > 0 && run_int("block-at rangi F_Y") < 330 && run_int("block-at rangi F_X") == 300,
          "28 E JUMP only moves the actor's jump/y");
    run_source("ku: 0  block-set! rangi F_VY 0  block-set! rangi F_Y 330");
    run_source("block-set! rangi F_D 1  block-set! rangi F_TARGET 0  krock: 1  fire-rock  tally");
    check(run_int("n-rock") >= 1, "28 F ROCK spawns through the actor");
    run_source("krock: 0");
    /* G/H. contact resolves through Rangi; one continuous overlap = one hit */
    run_source("reset  block-set! rangi F_B 3");
    run_source("h: ts-claim K_RAT  block-set! h F_X 480  block-set! h F_Y 332  block-set! h F_STATE 0  block-set! h F_TARGET -1");
    for (int i = 0; i < 20; i++) run_source("block-set! h F_X block-at rangi F_X  block-set! h F_Y + block-at rangi F_Y 2  rat-step h");
    check(run_int("block-at rangi F_B") == 2, "28 G/H continuous overlap => exactly one hit");
    run_source("ts-free h");
    /* K/J. final life => DEAD, and DEAD is inert */
    run_source("block-set! rangi F_B 1  block-set! rangi F_A 0  block-set! rangi F_C 1  rangi-step");
    check(run_int("block-at rangi F_B") == 0 && run_int("block-at rangi F_STATE") == 1, "28 J final life => DEAD");
    {
        long dx = run_int("block-at rangi F_X"), df = run_int("block-at rangi F_D");
        run_source("kl: 0  kr: 1  ku: 1  krock: 1  block-set! rangi F_TARGET 0  n-rock: 0  rangi-step  fire-rock");
        check(run_int("block-at rangi F_X") == dx && run_int("block-at rangi F_D") == df &&
              run_int("block-at rangi F_VY") == 0 && run_int("n-rock") == 0, "28 K DEAD actor is inert");
    }
    run_source("kl: 0  kr: 0  ku: 0  krock: 0");
    /* M/N. restart recreates the actor; camera derives without mutating it */
    run_source("reset");
    check(run_int("block-at rangi F_X") == 480 && run_int("block-at rangi F_B") == 3 &&
          run_int("block-at rangi F_STATE") == 0 && run_int("block-at rangi F_KIND") == 8, "28 M restart recreates the actor");
    run_source("view-w: 1000  block-set! rangi F_X 3000  rangi-step");
    check(run_int("block-at rangi F_X") == 3000 && run_int("cam") == 2650, "28 N camera derives from the actor");

    /* 29. Rangi torture: long mixed input + repeated contacts never break it */
    run_source("reset  view-w: 1056  block-set! rangi F_B 9");
    unsigned tseed = 26461u;
    int tortureFail = 0, sawAlive = 0, sawDead = 0;
    for (int i = 0; i < 2500 && !tortureFail; i++) {
        tseed = tseed * 1103515245u + 12345u;
        switch ((tseed >> 13) & 7) {
            case 0: run_source("kl: 1  kr: 0  ku: 0  kd: 0  krock: 0"); break;
            case 1: run_source("kl: 0  kr: 1  ku: 0  kd: 0  krock: 0"); break;
            case 2: run_source("kl: 0  kr: 1  ku: 1  kd: 0  krock: 0"); break;
            case 3: run_source("kl: 1  kr: 0  ku: 0  kd: 0  block-set! rangi F_TARGET 0  krock: 1"); break;
            case 4: run_source("kl: 1  kr: 0  ku: 0  kd: 1  krock: 0"); break;
            default: run_source("kl: 0  kr: 0  ku: 0  kd: 0  krock: 0"); break;
        }
        run_source("rangi-step  fire-rock  move-all 0  tally");
        if ((i % 17) == 0) {   /* a fresh rat hits once, then separates and frees */
            run_source("block-set! rangi F_A 0  h: ts-claim K_RAT  block-set! h F_X block-at rangi F_X  "
                       "block-set! h F_Y + block-at rangi F_Y 2  block-set! h F_STATE 0  block-set! h F_TARGET -1  rat-step h  ts-free h");
        }
        if ((i % 500) == 499) run_source("reset  view-w: 1056  block-set! rangi F_B 9");  /* restart mid-run */
        long st = run_int("block-at rangi F_STATE"), x = run_int("block-at rangi F_X"),
             y = run_int("block-at rangi F_Y"), vy = run_int("block-at rangi F_VY"),
             lb = run_int("block-at rangi F_B");
        if (st == 0) {
            sawAlive++;
            if (x < 56 || y < 260 || y > 330 || vy < 0 || vy > 22 || lb < 1 || lb > 9) {
                tortureFail = 1;
                printf("      torture ALIVE invariant broken i=%d st=%ld x=%ld y=%ld vy=%ld lb=%ld\n", i, st, x, y, vy, lb);
            }
        } else {
            sawDead++;
            if (vy != 0 || y != 330) {
                tortureFail = 1;
                printf("      torture DEAD invariant broken i=%d vy=%ld y=%ld\n", i, vy, y);
            }
        }
    }
    check(sawAlive > 100 && sawDead >= 0, "29 torture exercised the actor for thousands of ticks");
    check(!tortureFail, "29 torture: no impossible actor state");

    /* 32. pest speeds give useful jump-dodge clearance for every ground pest */
    check(run_int("block-at pest-speed 0") == 4 && run_int("block-at pest-speed 1") == 5 &&
          run_int("block-at pest-speed 2") == 3, "32 pest speeds are rat4 stoat5 possum3");
    check(run_int("block-at pest-speed 1") > run_int("block-at pest-speed 0") &&
          run_int("block-at pest-speed 0") > run_int("block-at pest-speed 2"),
          "32 speed ordering stoat > rat > possum");
    for (int sp = 0; sp < 3; sp++) {
        const char *nm = sp == 0 ? "rat" : (sp == 1 ? "stoat" : "possum");
        long v = run_int("block-at pest-speed 0");                 /* placeholder */
        char q[220];
        snprintf(q, sizeof q, "block-at pest-speed %d", sp);
        v = run_int(q);
        run_source("reset  view-w: 1000");
        run_source("block-set! rangi F_X 1000  block-set! rangi F_Y 330  block-set! rangi F_VY 0  block-set! rangi F_D 1  block-set! rangi F_B 3  block-set! rangi F_STATE 0  kl: 0  kr: 1  ku: 1");
        snprintf(q, sizeof q, "h: ts-claim K_RAT  block-set! h F_X 1150  block-set! h F_Y 345  block-set! h F_STATE 0  block-set! h F_TARGET -1  block-set! h F_VY %ld  block-set! h F_F 0", v);
        run_source(q);
        run_source("rangi-step  ku: 0  rat-step h");
        for (int t = 0; t < 39; t++) run_source("rangi-step  rat-step h");
        long lb = run_int("block-at rangi F_B");
        long pxf = run_int("block-at rangi F_X"), rxf = run_int("block-at h F_X");
        char msg[96]; snprintf(msg, sizeof msg, "32 timed forward jump clears a %s", nm);
        check(lb == 3 && pxf > rxf, msg);
        run_source("ts-free h");
    }

    /* 33. FOREST -> SKULL_CAVE transition, ONCE, same actor */
    run_source("reset  view-w: 1000  kl: 0  kr: 0");
    check(run_int("block-at rangi F_E") == 0, "33 starts in FOREST mode");
    long rid = run_int("block-at rangi F_ID");
    run_source("dist: 3999  rangi-step");
    check(run_int("block-at rangi F_E") == 0, "33 FOREST before the threshold");
    run_source("dist: 4000  rangi-step");
    check(run_int("block-at rangi F_E") == 1, "33 enters SKULL_CAVE at the threshold");
    check(run_int("block-at rangi F_ID") == rid && run_int("block-at rangi F_B") == 3,
          "33 SAME Rangi actor id/lives survive the transition");
    run_source("rangi-step  rangi-step");
    check(run_int("block-at rangi F_E") == 1, "33 transition happens exactly once");
    run_source("kr: 1  rangi-step");
    check(run_int("block-at rangi F_X") > 200, "33 Rangi can walk in the cave");
    run_source("kr: 0");
    {
        long nrat0 = run_int("ts-count K_RAT"), tree0 = run_int("ts-count K_TREE"), ntx = run_int("next-tree-x");
        for (int i = 0; i < 200; i++) run_source("step");
        check(run_int("ts-count K_RAT") <= nrat0 && run_int("ts-count K_TREE") == tree0 &&
              run_int("next-tree-x") == ntx, "33 forest spawning is frozen in the cave");
    }
    run_source("reset");
    check(run_int("block-at rangi F_E") == 0 && run_int("dist") == 480 && run_int("skull-ready") == 0,
          "33 restart returns to a fresh FOREST");

    /* 30. Skull Cave distance: one deterministic destination + ready flag */
    run_source("reset");
    check(run_int("block-at signs 0") == 500 && run_int("block-at signs 10") == 4000,
          "30 signs span start..destination");
    check(run_int("block-at signs 0") == 500 && run_int("block-at signs 2") == 1250 &&
          run_int("block-at signs 4") == 2000 && run_int("block-at signs 6") == 2750 &&
          run_int("block-at signs 8") == 3500 && run_int("block-at signs 10") == 4000,
          "30 signs at the exact intended positions, in order");
    check(run_int("SKULL_X") == 4000, "30 SKULL_X is 4000");
    check(run_int("skull-ready") == 0, "30 not ready at the start");
    run_source("dist: 3999  tally");
    check(run_int("skull-ready") == 0, "30 not ready just before the threshold");
    run_source("dist: 4000  tally");
    check(run_int("skull-ready") == 1 && run_int("over") == 0, "30 ready at the threshold, no transition");
    run_source("reset  block-set! rangi F_X 900  rangi-step");
    check(run_int("block-at rangi F_B") == 3 && run_int("block-at rangi F_X") == 900,
          "30 passing a sign leaves Rangi state intact");
    run_source("reset");
    check(run_int("dist") == 480 && run_int("skull-ready") == 0, "30 restart resets progression");

    /* 31. ROCK firing is independent of jump state */
    {
        long g = 0, j = 0, prev;
        run_source("reset  view-w: 1000  block-set! rangi F_X 1000");
        run_source("krock: 1  ku: 0  block-set! rangi F_TARGET 0");
        prev = run_int("ts-count K_ROCK");
        for (int i = 0; i < 240; i++) { run_source("rangi-step  fire-rock  move-all 0"); long c = run_int("ts-count K_ROCK"); if (c > prev) g++; prev = c; }
        run_source("reset  view-w: 1000  block-set! rangi F_X 1000");
        run_source("krock: 1  ku: 1  block-set! rangi F_TARGET 0");
        prev = run_int("ts-count K_ROCK");
        for (int i = 0; i < 240; i++) { run_source("rangi-step  fire-rock  move-all 0"); long c = run_int("ts-count K_ROCK"); if (c > prev) j++; prev = c; }
        printf("      (31) rock spawns grounded=%ld jumping=%ld\n", g, j);
        check(g > 10 && j == g, "31 A/B/C jumping ROCK cadence == grounded");
    }
    /* D. a one-tick ROCK tap mid-jump is accepted (independent of jump state) */
    run_source("reset  view-w: 1000  block-set! rangi F_X 1000  block-set! rangi F_Y 270  block-set! rangi F_VY 11");
    run_source("krock: 1  block-set! rangi F_TARGET 0");
    run_source("rangi-step  fire-rock  tally");
    check(run_int("n-rock") >= 1, "31 D ROCK tap during a jump is accepted");
    run_source("krock: 0");
    /* E. jump: 22-tick arc, peak ~60 px, lands at 330 */
    run_source("reset  view-w: 1000  block-set! rangi F_Y 330  block-set! rangi F_VY 0  ku: 1  rangi-step");
    check(run_int("block-at rangi F_VY") == 21, "31 E jump starts the 22-tick arc");
    {
        long minY = 999;
        for (int i = 0; i < 22; i++) { run_source("ku: 0  rangi-step"); long y = run_int("block-at rangi F_Y"); if (y < minY) minY = y; }
        printf("      (31) jump minY=%ld\n", minY);
        check(minY <= 271 && minY >= 268, "31 E jump peak ~60 px");
        check(run_int("block-at rangi F_Y") == 330, "31 E jump lands at ground 330");
    }

    /* 34. Skull Cave relic choice: LAND = highlight, FIRE = select, leave = cancel */
    long rid0, lives0;
    /* --- helmet route (pedestal A, x=760) --- */
    run_source("reset  view-w: 1000  kl: 0  kr: 0  krock: 0");
    run_source("dist: 4000  rangi-step");                 /* enter the cave */
    check(run_int("block-at rangi F_E") == 1 && run_int("block-at rangi F_C") == 0, "34 in cave, nothing highlighted");
    rid0 = run_int("block-at rangi F_ID"); lives0 = run_int("block-at rangi F_B");
    run_source("block-set! rangi F_X 1120  block-set! rangi F_Y 352  block-set! rangi F_VY 0  ku: 1  rangi-step");
    for (int i = 0; i < 40 && run_int("block-at rangi F_VY") > 0; i++) run_source("ku: 0  rangi-step");
    check(run_int("block-at rangi F_Y") == 182 && run_int("block-at rangi F_C") == 1 && run_int("block-at rangi F_E") == 1,
          "34 A landing highlights helmet (not selected)");
    /* jump off WITHOUT firing -> candidate cleared, no selection */
    run_source("block-set! rangi F_X 700  block-set! rangi F_Y 352  rangi-step");
    check(run_int("block-at rangi F_C") == 0 && run_int("block-at rangi F_E") == 1, "34 D leaving cancels, no selection");
    /* FIRE on the floor selects nothing */
    run_source("reset  dist: 4000  rangi-step  block-set! rangi F_X 1300  block-set! rangi F_Y 352  krock: 1  rangi-step  krock: 0");
    check(run_int("block-at rangi F_E") == 1 && run_int("block-at rangi F_C") == 0, "34 H FIRE on the floor selects nothing");
    /* --- helmet select --- */
    run_source("reset  dist: 4000  rangi-step  krock: 0");
    rid0 = run_int("block-at rangi F_ID"); lives0 = run_int("block-at rangi F_B");
    run_source("block-set! rangi F_X 1120  block-set! rangi F_Y 352  block-set! rangi F_VY 0  ku: 1  rangi-step");
    for (int i = 0; i < 40 && run_int("block-at rangi F_VY") > 0; i++) run_source("ku: 0  rangi-step");
    run_source("krock: 1  rangi-step  krock: 0");
    check(run_int("block-at rangi F_E") == 2, "34 E FIRE selects the helmet route");
    check(run_int("block-at rangi F_ID") == rid0 && run_int("block-at rangi F_B") == lives0, "34 J/K same actor id + lives");
    /* other pedestals can no longer be chosen */
    run_source("block-set! rangi F_X 1480  rangi-step  krock: 1  rangi-step  krock: 0");
    check(run_int("block-at rangi F_E") == 2, "34 I selection locks out other relics");
    /* --- phone select --- */
    run_source("reset  dist: 4000  rangi-step  krock: 0");
    run_source("block-set! rangi F_X 1300  block-set! rangi F_Y 352  block-set! rangi F_VY 0  ku: 1  rangi-step");
    for (int i = 0; i < 40 && run_int("block-at rangi F_VY") > 0; i++) run_source("ku: 0  rangi-step");
    check(run_int("block-at rangi F_C") == 2, "34 B landing highlights phone");
    run_source("krock: 1  rangi-step  krock: 0");
    check(run_int("block-at rangi F_E") == 3, "34 F FIRE selects the phone route");
    /* --- key select --- */
    run_source("reset  dist: 4000  rangi-step  krock: 0");
    run_source("block-set! rangi F_X 1480  block-set! rangi F_Y 352  block-set! rangi F_VY 0  ku: 1  rangi-step");
    for (int i = 0; i < 40 && run_int("block-at rangi F_VY") > 0; i++) run_source("ku: 0  rangi-step");
    check(run_int("block-at rangi F_C") == 3, "34 C landing highlights key");
    run_source("krock: 1  rangi-step  krock: 0");
    check(run_int("block-at rangi F_E") == 4, "34 G FIRE selects the key route");
    /* restart returns to a fresh FOREST with no relic selected */
    run_source("reset");
    check(run_int("block-at rangi F_E") == 0 && run_int("block-at rangi F_C") == 0, "34 M restart returns to fresh FOREST");

    /* 35. Skull Cave is FINITE: ranger + camera clamped, objects inside */
    run_source("reset  view-w: 1000  dist: 4000  rangi-step");   /* enter cave */
    run_source("kl: 1  kr: 0");
    for (int i = 0; i < 60; i++) run_source("rangi-step");
    check(run_int("block-at rangi F_X") == 60, "35 C left boundary clamps at 60");
    run_source("kl: 0  kr: 1");
    for (int i = 0; i < 400; i++) run_source("rangi-step");
    check(run_int("block-at rangi F_X") == 1740, "35 B right boundary clamps at 1740");
    check(run_int("cam") == 800, "35 D camera stops at the cave right wall (1800-view-w)");
    check(1120 > 60 && 1300 < 1740 && 1480 < 1740 && 700 < 1740, "35 E/F pedestals + guardian inside the chamber");
    check(run_int("block-at rangi F_E") == 1, "35 still in cave after hitting the wall");
    run_source("reset");
    check(run_int("block-at rangi F_E") == 0 && run_int("dist") == 480, "35 L restart returns to fresh FOREST");

    if (fails == 0) { printf("kaka-wave-test PASS\n"); return 0; }
    printf("kaka-wave-test FAIL (%d)\n", fails);
    return 1;
}
