/* demos/kaka/kaka_render_tests.c -- D12A.7 render-binding regression.
 *
 * The reported symptom was "damage to one gameplay tree shows up on another".
 * This harness proves the opposite and locks it in: it loads the real game,
 * installs a tiny test-only overlay that adds three damage routes (one per tree
 * tuple slot), then damages tree 0, tree 1 and tree 2 in turn and reads the
 * Canvas script Glon emits into G1_VIS. For each case it counts foliage circles
 * near each tree's world x and asserts that ONLY the intended tree's instance
 * changed (and did shrink).
 *
 * The overlay is injected here rather than in kaka.glon so the shipped game
 * carries no test hooks and stays inside the fixed loader heap.
 */
#include "r0_s1.h"
#include "r0_s1_g1a.h"
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static char filebuf[1 << 20];
static char wrapbuf[1 << 20];

static int run_source(const char *src) {
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) { fprintf(stderr, "kaka-render-test: parse error (%d)\n", err); return -1; }
    r0_s1_run_persistent(b);
    if (!r0_s1_ran_cleanly()) { fprintf(stderr, "kaka-render-test: run failed\n"); return -1; }
    return 0;
}

static int load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "kaka-render-test: cannot open %s\n", path); return -1; }
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
    return run_source(src);
}

/* Test-only overlay: route `kaka-damageN` damages tree tuple slot N then
 * renders; `home` stays the ordinary reset. */
static const char *OVERLAY =
    "[ route: ["
    "  either = current-route 'kaka-damage0 [ damage-tree 0 60 ] ["
    "  either = current-route 'kaka-damage1 [ damage-tree 1 60 ] ["
    "  either = current-route 'kaka-damage2 [ damage-tree 2 60 ] ["
    "  either = current-route 'kaka-ghost [ r: ts-claim K_RAT  block-set! r F_X 300  block-set! r F_Y 340  block-set! r F_STATE 9  block-set! r F_C 10  block-set! r F_A 0 ] ["
    "  either = current-route 'kaka-treeghost [ r: ts-claim K_RAT  block-set! r F_X 300  block-set! r F_Y 250  block-set! r F_STATE 9  block-set! r F_C 10  block-set! r F_A 0 ] ["
    "  either = current-route 'kaka-stoat [ b: ts-claim K_RAT  block-set! b F_X 300  block-set! b F_Y 250  block-set! b F_STATE 0  block-set! b F_A 1 ] ["
    "  either = current-route 'kaka-possum [ q: ts-claim K_RAT  block-set! q F_X 300  block-set! q F_Y 250  block-set! q F_STATE 0  block-set! q F_A 2 ] ["
    "  either = current-route 'kaka-air [ py: 270  jt: 11 ] ["
    "  either = current-route 'kaka-run [ kr: 1  move-player ] ["
    "  either = current-route 'kaka-hit [ invuln: 10  tick: 1  kr: 0 ] [ reset ]]]]]]]]]]"
    "  tally render ] ]";

static char out[65536];
static char vis[G1_VIS_CAP + 8];

static void snap_vis(void) {
    cell n = M[G1_VIS];
    if (n < 0) n = 0;
    if (n > G1_VIS_CAP) n = G1_VIS_CAP;
    for (cell i = 0; i < n; i++) vis[i] = (char)int_val(M[G1_VIS_DATA + i]);
    vis[n] = 0;
}

/* count foliage/seed circles (`O`) whose x is within +-45 of a tree centre */
static int foliage_near(int cx) {
    int count = 0;
    char *p = vis;
    while ((p = strchr(p, '\n')) != NULL) {
        p++;
        if (p[0] == 'O' && p[1] == ' ') {
            int x = atoi(p + 2);
            if (x >= cx - 45 && x <= cx + 45) count++;
        }
    }
    return count;
}

static int route(const char *tok) {
    int out_len = 0;
    if (r0_s1_g1a_route(tok, out, (int)sizeof out, &out_len) != 0) {
        fprintf(stderr, "kaka-render-test: route '%s' failed\n", tok);
        return -1;
    }
    snap_vis();
    return 0;
}

/* the semantic player draw op `P <pose> <x> <y> <dir>` (host owns the art) */
static int player_op(int *pose, int *x, int *y, int *dir) {
    char *q = vis;
    while ((q = strchr(q, '\n')) != NULL) {
        q++;
        if (q[0] == 'P' && q[1] == ' ')
            if (sscanf(q + 2, "%d %d %d %d", pose, x, y, dir) == 4) return 1;
    }
    return 0;
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
    if (load_file("demos/kaka/kaka-wave.glon") != 0) return 2;
    if (run_source(OVERLAY) != 0) return 2;

    const int X[3] = { 240, 960, 1680 };  /* spawn-tree 0/1/2 world x */
    int base[3], fails = 0;

    if (route("home") != 0) return 2;
    for (int i = 0; i < 3; i++) base[i] = foliage_near(X[i]);
    printf("baseline foliage@[240,720,1200] = [%d,%d,%d]\n", base[0], base[1], base[2]);

    for (int t = 0; t < 3; t++) {
        char tok[32];
        sprintf(tok, "kaka-damage%d", t);
        if (route("home") != 0) return 2;         /* fresh reset */
        if (route(tok) != 0) return 2;            /* damage only tree t */
        int c0 = foliage_near(X[0]), c1 = foliage_near(X[1]), c2 = foliage_near(X[2]);
        printf("damage tree %d -> foliage@[240,720,1200] = [%d,%d,%d]\n", t, c0, c1, c2);
        int c[3] = { c0, c1, c2 };
        for (int i = 0; i < 3; i++) {
            int changed = c[i] != base[i];
            int expected = (i == t);
            if (changed != expected) {
                printf("  FAIL: tree %d changed=%d expected=%d\n", i, changed, expected);
                fails++;
            }
        }
        if (!(c[t] < base[t])) { printf("  FAIL: damaged tree %d did not lose foliage\n", t); fails++; }
    }

    /* a death must emit exactly the semantic event and NO white disc / tuple
       ghost: the host, not the tuple, stages the funeral */
    if (route("home") != 0) return 2;
    if (route("kaka-ghost") != 0) return 2;
    {
        int deaths = 0, white_discs = 0, ghost_sprite = 0;
        char *q = vis;
        while ((q = strchr(q, '\n')) != NULL) {
            q++;
            if (q[0] == 'D' && q[1] == ' ') deaths++;
            else if (q[0] == 'O' && q[1] == ' ') {
                int x, y, r, c;
                if (sscanf(q + 2, "%d %d %d %d", &x, &y, &r, &c) == 4 && c == 15 && r >= 10) white_discs++;
            } else if (q[0] == 'S' && q[1] == ' ' && atoi(q + 2) == 23) {
                ghost_sprite++;
            }
        }
        printf("  ground death: D-events=%d white_discs=%d ghost_sprites=%d\n", deaths, white_discs, ghost_sprite);
        if (deaths < 1) { printf("  FAIL: no semantic death event emitted\n"); fails++; }
        if (white_discs != 0) { printf("  FAIL: death render contains a white disc\n"); fails++; }
        if (ghost_sprite != 0) { printf("  FAIL: tuple-side ghost sprite still drawn\n"); fails++; }
    }

    /* a pest killed up a tree emits the event from its VISIBLE death position */
    if (route("home") != 0) return 2;
    if (route("kaka-treeghost") != 0) return 2;
    {
        int ok = 0;
        char *q = vis;
        while ((q = strchr(q, '\n')) != NULL) {
            q++;
            if (q[0] == 'D' && q[1] == ' ') {
                int sp, id, x, y;
                if (sscanf(q + 2, "%d %d %d %d", &sp, &id, &x, &y) == 4 && sp == 0 && x == 300 && y == 250) ok = 1;
            }
        }
        printf("  tree-death event at (300,250), species 0: %s\n", ok ? "ok" : "missing");
        if (!ok) { printf("  FAIL: tree-death event wrong position/species\n"); fails++; }
    }

    /* the vector pest belly must be the muted tone (16/17), never bright cream 1 */
    if (route("home") != 0) return 2;
    if (route("kaka-stoat") != 0) return 2;
    {
        int belly = 0, bright = 0, whiteE = 0;
        char *q = vis;
        while ((q = strchr(q, '\n')) != NULL) {
            q++;
            if (q[0] == 'E' && q[1] == ' ') {
                int x, y, rx, ry, c;
                if (sscanf(q + 2, "%d %d %d %d %d", &x, &y, &rx, &ry, &c) == 5) {
                    /* scope to the stoat's column (the ranger sits at x=480) */
                    if (x > 240 && x < 360) {
                        if (c == 16 || c == 17) belly++;
                        if (c == 1) bright++;
                        if (c == 15) whiteE++;
                    }
                }
            }
        }
        printf("  stoat render: mutedBelly=%d brightCreamBelly=%d whiteEllipse=%d\n", belly, bright, whiteE);
        if (belly < 1) { printf("  FAIL: stoat belly is not muted\n"); fails++; }
        if (bright != 0) { printf("  FAIL: stoat still has a bright cream belly\n"); fails++; }
        if (whiteE != 0) { printf("  FAIL: stoat render has a white ellipse\n"); fails++; }
    }

    /* possum must read bulky/grey with a long bare pink tail */
    if (route("home") != 0) return 2;
    if (route("kaka-possum") != 0) return 2;
    {
        int greyBody = 0, pinkTail = 0, whiteE = 0;
        char *q = vis;
        while ((q = strchr(q, '\n')) != NULL) {
            q++;
            if (q[0] == 'E' && q[1] == ' ') {
                int x, y, rx, ry, c;
                if (sscanf(q + 2, "%d %d %d %d %d", &x, &y, &rx, &ry, &c) == 5) {
                    if (c == 2 && rx >= 30 && ry >= 14) greyBody++;
                    if (c == 15) whiteE++;
                }
            } else if (q[0] == 'L' && q[1] == ' ') {
                int x1, y1, x2, y2, w, c;
                if (sscanf(q + 2, "%d %d %d %d %d %d", &x1, &y1, &x2, &y2, &w, &c) == 6) {
                    int dx = x1 - x2; if (dx < 0) dx = -dx;
                    if (c == 10 && w >= 5 && dx >= 30) pinkTail++;
                }
            }
        }
        printf("  possum render: bulkyGreyBody=%d longPinkTail=%d whiteEllipse=%d\n", greyBody, pinkTail, whiteE);
        if (greyBody < 1) { printf("  FAIL: possum body is not bulky grey\n"); fails++; }
        if (pinkTail < 1) { printf("  FAIL: possum lacks a long bare pink tail\n"); fails++; }
        if (whiteE != 0) { printf("  FAIL: possum render has a white ellipse\n"); fails++; }
    }

    /* the player is now a semantic pose op the host turns into a cartoon
       sprite: idle / run / jump / hit resolve, facing works, and the jump
       translates the sprite's y (no primitive duplicates) */
    if (route("home") != 0) return 2;
    int gp, gx, gy, gd, gok = player_op(&gp, &gx, &gy, &gd);
    if (route("kaka-run") != 0) return 2;
    int rp, rx, ry, rd, rok = player_op(&rp, &rx, &ry, &rd);
    if (route("home") != 0) return 2;
    if (route("kaka-air") != 0) return 2;
    int ap, ax, ay, ad, aok = player_op(&ap, &ax, &ay, &ad);
    if (route("home") != 0) return 2;
    if (route("kaka-hit") != 0) return 2;
    int hp, hx, hy, hd, hok = player_op(&hp, &hx, &hy, &hd);
    printf("  player idle: ok=%d pose=%d x=%d y=%d dir=%d\n", gok, gp, gx, gy, gd);
    printf("  player run pose=%d | jump pose=%d y=%d | hit pose=%d\n", rp, ap, ay, hp);
    if (!gok || gp != 0) { printf("  FAIL: idle pose not emitted\n"); fails++; }
    if (gx != 480 || gy != 330 || gd != 1) { printf("  FAIL: idle x/y/facing wrong\n"); fails++; }
    if (!rok || !(rp == 1 || rp == 2)) { printf("  FAIL: run pose not emitted\n"); fails++; }
    if (!aok || ap != 3 || ay != 270) { printf("  FAIL: jump pose/translation wrong\n"); fails++; }
    if (!hok || hp != 4) { printf("  FAIL: hit pose not emitted\n"); fails++; }
    /* no old primitive ranger must remain (the host is the only painter) */
    {
        int prim = 0; char *q = vis;
        while ((q = strchr(q, '\n')) != NULL) {
            q++;
            if ((q[0]=='E'||q[0]=='R'||q[0]=='O'||q[0]=='T') && q[1]==' ') {
                int a, b, d, e, c;
                if (q[0]=='E' && sscanf(q+2,"%d %d %d %d %d",&a,&b,&d,&e,&c)==5 && a>430 && a<530) prim++;
            }
        }
        if (prim != 0) { printf("  FAIL: primitive ranger still drawn (%d)\n", prim); fails++; }
    }

    if (fails == 0) { printf("kaka-render-test PASS\n"); return 0; }
    printf("kaka-render-test FAIL (%d)\n", fails);
    return 1;
}
