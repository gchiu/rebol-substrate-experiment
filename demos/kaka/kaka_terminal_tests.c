/* demos/kaka/kaka_terminal_tests.c -- deterministic terminal-state regressions.
 *
 * The reported bug: the HUD said "GAME OVER" while 3 trees and 3 lives
 * remained, and the large splash did not appear. That was two separate flags
 * (`over` for the HUD, `dead` for the splash). This harness drives the REAL
 * game loop and asserts the single authoritative terminal predicate, and that
 * the HUD and splash always agree with it.
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
    if (err) { fprintf(stderr, "terminal-test: parse %d in: %s\n", err, src); return -1; }
    r0_s1_run_persistent(b);
    if (reclaim) M[GC_LOADER_HP] = save;
    if (!r0_s1_ran_cleanly()) { fprintf(stderr, "terminal-test: run failed: %s\n", src); return -1; }
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
static long state(const char *name) { return run_int(name); }

/* render and report whether the HUD says GAME OVER and whether the large
 * splash is present; also the authoritative over flag. */
static char gbuf[1 << 16];
static void render_grab(void) {
    run_source("render");
    cell n = M[G1_OUT]; if (n < 0) n = 0; if (n > G1_OUT_CAP) n = G1_OUT_CAP;
    if (n > (cell)sizeof gbuf - 1) n = (cell)sizeof gbuf - 1;
    for (cell i = 0; i < n; i++) gbuf[i] = (char)int_val(M[G1_OUT_DATA + i]);
    gbuf[n] = 0;
}
static void check_consistent(const char *label) {
    render_grab();
    int hud = strstr(gbuf, "<b>GAME OVER</b>") != NULL;
    int splash = strstr(gbuf, "kaka-gameover") != NULL;
    long over = state("over");
    int ok = (hud == (over == 1)) && (splash == (over == 1));
    printf("  %-34s over=%ld hud=%d splash=%d  %s\n", label, over, hud, splash, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}
static void check_state(const char *label, long want_over, long want_lives, long want_trees) {
    long over = state("over"), lives = state("block-at rangi F_B"), trees = state("block-at tc2 0");
    int ok = (over == want_over) && (lives == want_lives) && (trees == want_trees);
    printf("  %-34s over=%ld block-at rangi F_B=%ld trees=%ld  %s\n", label, over, lives, trees, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}
static int load_file(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) { fprintf(stderr, "terminal-test: open %s\n", path); return -1; }
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

    printf("kaka terminal-state test (one authoritative predicate)\n");

    /* 1. fresh game */
    if (run_source("reset")) return 2;
    check_state("1 fresh", 0, 3, 3);
    check_consistent("1 fresh HUD/splash");
    if (!(state("block-at tc2 1") == 300)) { printf("  FAIL: fresh habitat != 300\n"); fails++; }

    /* 2. one real rat gnaw (the reported bug): run the real loop until habitat
       drops below 300, then it must still NOT be game over. */
    if (run_source("reset")) return 2;
    for (int t = 0; t < 400; t++) {
        if (run_source("on-tick")) return 2;
        if (state("block-at tc2 1") < 300) break;
    }
    printf("  (after real rat gnaw: habitat=%ld)\n", state("block-at tc2 1"));
    check_state("2 rat gnaw, 3 trees alive", 0, 3, 3);
    check_consistent("2 rat gnaw HUD/splash");

    /* 3. repeated non-terminal damage: damage every tree but leave them alive */
    if (run_source("reset")) return 2;
    if (run_source("damage-tree 0 90  damage-tree 1 90  damage-tree 2 90  tally")) return 2;
    check_state("3 trees damaged, all alive", 0, 3, 3);
    check_consistent("3 trees damaged HUD/splash");

    /* 4/5. one tree destroyed, others alive -> still not terminal */
    if (run_source("damage-tree 0 100  tally")) return 2;
    check_state("5 one tree destroyed", 0, 3, 2);
    check_consistent("5 one tree destroyed HUD/splash");

    /* 6. all trees destroyed -> forest terminal + splash */
    if (run_source("damage-tree 1 100  damage-tree 2 100  tally")) return 2;
    check_state("6 all trees destroyed", 1, 3, 0);
    check_consistent("6 all trees destroyed HUD/splash");

    /* 7. lives -> 0 via the real life-loss function -> terminal + splash */
    if (run_source("reset  block-set! rangi F_B  1  lose-life  tally")) return 2;
    check_state("7 final life lost", 1, 0, 3);
    check_consistent("7 final life lost HUD/splash");

    /* 8. a non-terminal hit keeps one life and is NOT game over */
    if (run_source("reset  block-set! rangi F_B  3  lose-life  tally")) return 2;
    check_state("8 non-terminal hit", 0, 2, 3);
    check_consistent("8 non-terminal hit HUD/splash");

    if (fails == 0) { printf("kaka-terminal-test PASS\n"); return 0; }
    printf("kaka-terminal-test FAIL (%d)\n", fails);
    return 1;
}
