/* demos/kaka/kaka_wave_tests.c -- D12B gameplay regressions.
 *
 * Drives the real wave/climb/cover functions with a controlled tuple space:
 *   - rats spawn off the left edge and march right;
 *   - a rat climbs the tree it reaches and gnaws it;
 *   - a climbing rat drives a feeding kaka off that tree;
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

    /* 1. a new game has no wave; a spawned rat appears off the left edge */
    run_source("reset");
    check(run_int("wave") == 0 && run_int("n-rat") == 0, "1 fresh: no wave, no rats");
    run_source("spawn-rat");
    check(run_int("block-at ts-t 3 F_X") == -24, "1 rat spawns off the left edge (x=-24)");
    check(run_int("block-at ts-t 3 F_Y") == 345, "1 rat starts on the ground (y=345)");

    /* 2. the wave controller advances and produces rats */
    run_source("reset");
    for (int t = 0; t < 400 && run_int("n-rat") < 1; t++) run_source("on-tick");
    check(run_int("wave") >= 1 && run_int("n-rat") >= 1, "2 wave advances and spawns rats");

    /* 3. a rat climbs the tree it reaches and gnaws it */
    run_source("reset");
    run_source("ts-claim K_RAT block-set! ts-t 3 F_X 240 block-set! ts-t 3 F_Y 345 block-set! ts-t 3 F_STATE 0 block-set! ts-t 3 F_A 1 block-set! ts-t 3 F_D 1");
    ticks("rat-step ts-t 3", 60);
    check(run_int("block-at ts-t 3 F_Y") <= 250, "3 rat climbed the trunk (y<=250)");
    check(run_int("block-at ts-t 0 F_A") < 100, "3 climbed rat gnawed tree0");
    check(run_int("block-at ts-t 1 F_A") == 100 && run_int("block-at ts-t 2 F_A") == 100, "3 other trees untouched");

    /* 4. a climbing rat drives a feeding kaka off that tree */
    run_source("reset");
    run_source("ts-claim K_KAKA block-set! ts-t 3 F_STATE KS_EAT block-set! ts-t 3 F_TARGET 0 block-set! ts-t 3 F_X 240 block-set! ts-t 3 F_Y 250 block-set! ts-t 3 F_A 0");
    run_source("ts-claim K_RAT block-set! ts-t 4 F_X 240 block-set! ts-t 4 F_Y 345 block-set! ts-t 4 F_STATE 0 block-set! ts-t 4 F_A 1 block-set! ts-t 4 F_D 1");
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

    if (fails == 0) { printf("kaka-wave-test PASS\n"); return 0; }
    printf("kaka-wave-test FAIL (%d)\n", fails);
    return 1;
}
