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
static void px300(void) { run_source("px: 300  pf: 1  krock: 1  rock-cd: 0"); }
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
    if (load_file("demos/kaka/kaka-wave.glon")) return 2;

    printf("kaka D12B wave/climb/cover test\n");

    /* 1. a new game has no wave; a spawned pest enters off the RIGHT edge */
    run_source("reset");
    check(run_int("wave") == 0 && run_int("n-rat") == 0, "1 fresh: no wave, no pests");
    run_source("spawn-pest");
    check(run_int("block-at ts-t 3 F_X") == run_int("+ + cam view-w 90"), "1 pest spawns off the right edge");
    check(run_int("block-at ts-t 3 F_X") > run_int("px"), "1 pest is ahead (right) of the ranger");
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
    run_source("px: 2000  rat-step ts-t 3");
    check(run_int("block-at ts-t 3 F_X") < x, "7 still left after ranger moves right");
    x = run_int("block-at ts-t 3 F_X");
    run_source("px: 100  rat-step ts-t 3");
    check(run_int("block-at ts-t 3 F_X") < x, "7 still left after ranger moves left");

    /* 8 (D). the ranger advances far beyond the old ~1440 world */
    run_source("reset  kr: 1");
    for (int i = 0; i < 250; i++) run_source("move-player");
    check(run_int("px") > 2400, "8 ranger advances beyond the old world");

    /* 9 (E). camera leaves more visible space ahead than behind */
    run_source("reset  view-w: 1000  px: 3000  move-player");
    check(run_int("- px cam") < run_int("- view-w - px cam"), "9 more space ahead than behind");

    /* 10 (F). one rock kills a 1-hp rat and scores 20 */
    run_source("reset  px: 300  pf: 1  krock: 1  rock-cd: 0");
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
    run_source("rock-cd: 0  fire-rock  rock-step ts-t 4");
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
        char msg[64]; snprintf(msg, sizeof msg, "14 powered kaka zaps a %s", names[sp]);
        check(run_int("ts-count K_RAT") == 0, msg);
    }

    /* 15 (J/M/Q). restart clears progression/pests/projectiles + viewport-only */
    run_source("reset");
    run_source("spawn-pest");
    long px0 = run_int("px"), d0 = run_int("dist"), nx0 = run_int("next-tree-x");
    run_source("px: 9000  dist: 9000  next-tree-x: 10000");
    run_source("view-w: 1200  move-player");
    check(run_int("px") == 9000, "15 viewport change does not move the ranger");
    run_source("reset");
    check(run_int("px") == 480 && run_int("dist") == 480 && run_int("next-tree-x") == 2400,
          "15 restart resets progression");
    check(run_int("ts-count K_RAT") == 0 && run_int("ts-count K_ROCK") == 0 && run_int("ts-count K_BERRY") == 0,
          "15 restart clears pests/projectiles");
    (void)px0; (void)d0; (void)nx0;

    /* 16 (N/O/P). 20+ sector transitions keep tuple occupancy bounded */
    run_source("reset  kr: 1");
    long maxTotal = 0;
    for (int i = 0; i < 1600; i++) {
        run_source("move-player  spawn-timers  move-all 0  tally");
        if (i % 20 == 0) {
            long tot = run_int("+ + + + ts-count K_RAT ts-count K_KAKA ts-count K_ROCK ts-count K_BERRY ts-count K_TREE");
            if (tot > maxTotal) maxTotal = tot;
        }
    }
    long sectors = (run_int("px") - 480) / 720;
    check(sectors >= 20, "16 advanced through 20+ sectors");
    check(run_int("next-tree-x") >= 12000, "16 rolling forest regenerated ahead");
    check(run_int("trees-live") <= 6, "16 active tree count bounded");
    check(maxTotal <= 30, "16 tuple occupancy bounded (not growing with distance)");

    if (fails == 0) { printf("kaka-wave-test PASS\n"); return 0; }
    printf("kaka-wave-test FAIL (%d)\n", fails);
    return 1;
}
