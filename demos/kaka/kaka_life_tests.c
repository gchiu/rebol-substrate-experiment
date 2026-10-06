/* demos/kaka/kaka_life_tests.c -- rendered-output tests for the Kaka lives /
 * laser-hit / game-over / high-score layer.
 *
 * Loads the real game (prelude + strings + kaka-lib + kaka + kaka-draw) and
 * drives it through the ordinary G1A event bridge, then inspects the HTML Glon
 * renders (the same bytes the browser writes into the DOM). The pure rule
 * assertions (segment intersection, invulnerability, high-score comparison)
 * live in kaka-selftest.glon; this file proves the user-visible result.
 */
#include "r0_s1.h"
#include "r0_s1_g1a.h"
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>

static char filebuf[1 << 20];
static char wrapbuf[1 << 20];
static char out[1 << 16];

static int load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "kaka-life-test: cannot open %s\n", path); return -1; }
    size_t n = fread(filebuf, 1, sizeof filebuf - 1, f);
    fclose(f);
    filebuf[n] = 0;
    char *r = filebuf, *w = filebuf;
    while (*r) { if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; } *w++ = *r++; }
    *w = 0;
    const char *src = filebuf;
    char *p = filebuf;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (*p && *p != '[') {
        size_t len = strlen(filebuf);
        wrapbuf[0] = '['; wrapbuf[1] = ' ';
        memcpy(wrapbuf + 2, filebuf, len);
        wrapbuf[len + 2] = ' '; wrapbuf[len + 3] = ']'; wrapbuf[len + 4] = 0;
        src = wrapbuf;
    }
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) { fprintf(stderr, "kaka-life-test: parse error in %s (%d)\n", path, err); return -2; }
    r0_s1_run_persistent(b);
    if (!r0_s1_ran_cleanly()) { fprintf(stderr, "kaka-life-test: run failed in %s\n", path); return -3; }
    return 0;
}

static int fails = 0;
static void check(int ok, const char *m) {
    if (ok) printf("  ok: %s\n", m);
    else { printf("  FAIL: %s\n", m); fails++; }
}

/* dispatch an event with an optional value; return the rendered HTML */
static const char *ev(const char *tok, const char *val) {
    int n = 0;
    if (r0_s1_g1a_event_value(tok, val, out, (int)sizeof out - 1, &n) != 0) {
        out[0] = 0;
        return out;
    }
    if (n < 0) n = 0;
    if (n > (int)sizeof out - 1) n = (int)sizeof out - 1;
    out[n] = 0;
    return out;
}

int main(void) {
    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    if (load_file("glon-lib/prelude.glon") != 0) return 2;
    if (load_file("glon-lib/strings.glon") != 0) return 2;
    if (load_file("demos/kaka/kaka-lib.glon") != 0) return 2;
    if (load_file("demos/kaka/kaka.glon") != 0) return 2;
    if (load_file("demos/kaka/kaka-draw.glon") != 0) return 2;

    /* A. a new game shows three lives and no game over */
    check(strstr(ev("kaka-start", ""), "Lives: 3") != NULL, "A: HUD shows Lives: 3");
    check(strstr(out, "GAME OVER!!!") == NULL, "A: no game-over splash at start");

    /* L. a higher score is rendered and marked for persistence */
    check(strstr(ev("kaka-debug-score", "777"), "HIGH SCORE: 777") != NULL,
          "L: higher score raises and renders HIGH SCORE: 777");
    check(strstr(out, "id='kaka-hs'") != NULL && strstr(out, ">777<") != NULL,
          "L: the persistence marker carries 777");

    /* K. a lower score does not replace the high score */
    check(strstr(ev("kaka-debug-score", "10"), "HIGH SCORE: 777") != NULL,
          "K: lower score keeps HIGH SCORE: 777");

    /* C/F. three forced hits remove one life each and end in game over */
    check(strstr(ev("kaka-debug-hit", ""), "Lives: 2") != NULL, "C: first hit -> Lives: 2");
    check(strstr(ev("kaka-debug-hit", ""), "Lives: 1") != NULL, "C: second hit -> Lives: 1");
    const char *go = ev("kaka-debug-hit", "");
    check(strstr(go, "GAME OVER!!!") != NULL, "F: final hit -> GAME OVER!!!");
    check(strstr(go, "THE FOREST HAS FALLEN") != NULL, "G: splash subtitle present");
    check(strstr(go, "SCORE:") != NULL, "G: splash shows SCORE");
    check(strstr(go, "HIGH SCORE: 777") != NULL, "G: splash shows HIGH SCORE: 777");
    check(strstr(go, "PRESS R TO PLAY AGAIN") != NULL, "G: splash shows restart prompt");
    check(strstr(go, "data-glon-event='kaka-restart'") != NULL, "G: splash has a touch restart control");
    check(strstr(go, "Lives: 0") != NULL, "F: HUD shows Lives: 0");

    /* H/I. restart restores three lives, clears game over, keeps the high score */
    const char *r = ev("kaka-restart", "");
    check(strstr(r, "Lives: 3") != NULL, "H: restart -> Lives: 3");
    check(strstr(r, "GAME OVER!!!") == NULL, "H: restart clears game over");
    check(strstr(r, "HIGH SCORE: 777") != NULL, "I: restart keeps HIGH SCORE: 777");

    if (fails == 0) { printf("kaka-life-test PASS\n"); return 0; }
    printf("kaka-life-test FAIL (%d)\n", fails);
    return 1;
}
