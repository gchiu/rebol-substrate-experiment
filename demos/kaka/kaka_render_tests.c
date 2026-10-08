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
    "  either = current-route 'kaka-air [ py: 270 ] [ reset ]]]]]]]]"
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

/* collect the ranger's primitives (the only actor near cx) from the canvas
   script: count, vertical extent and which palette entries appear. */
static void player_stats(int cx, int *count, int *ymin, int *ymax,
                         unsigned *colours, int *maxLight) {
    *count = 0; *ymin = 99999; *ymax = -99999; *colours = 0; *maxLight = 0;
    char *q = vis;
    while ((q = strchr(q, '\n')) != NULL) {
        q++;
        if (q[1] != ' ') continue;
        char op = q[0];
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0, c = -1, ok = 0, a, b, d, e, f, g;
        if (op == 'E' && sscanf(q + 2, "%d %d %d %d %d", &a, &b, &d, &e, &c) == 5) {
            x0 = a - d; x1 = a + d; y0 = b - e; y1 = b + e; ok = 1;
        } else if (op == 'O' && sscanf(q + 2, "%d %d %d %d", &a, &b, &d, &c) == 4) {
            x0 = a - d; x1 = a + d; y0 = b - d; y1 = b + d; ok = 1;
        } else if (op == 'R' && sscanf(q + 2, "%d %d %d %d %d", &a, &b, &d, &e, &c) == 5) {
            x0 = a; y0 = b; x1 = a + d; y1 = b + e; ok = 1;
        } else if (op == 'T' && sscanf(q + 2, "%d %d %d %d %d %d %d", &a, &b, &d, &e, &f, &g, &c) == 7) {
            x0 = x1 = a; y0 = y1 = b;
            if (d < x0) x0 = d;
            if (d > x1) x1 = d;
            if (f < x0) x0 = f;
            if (f > x1) x1 = f;
            if (e < y0) y0 = e;
            if (e > y1) y1 = e;
            if (g < y0) y0 = g;
            if (g > y1) y1 = g;
            ok = 1;
        }
        if (!ok) continue;
        if (x1 < cx - 45 || x0 > cx + 45) continue;
        (*count)++;
        if (y0 < *ymin) *ymin = y0;
        if (y1 > *ymax) *ymax = y1;
        if (c >= 0 && c < 32) *colours |= (1u << c);
        if ((c == 1 || c == 15)) {
            int ext = x1 - x0; if (y1 - y0 > ext) ext = y1 - y0;
            if (ext > *maxLight) *maxLight = ext;
        }
    }
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

    /* the ranger must be several distinct primitives, human-sized, and the
       WHOLE figure must ride the jump (no component left at the basline) */
    if (route("home") != 0) return 2;
    int gN, gy0, gy1, gMaxLight; unsigned gCol;
    player_stats(480, &gN, &gy0, &gy1, &gCol, &gMaxLight);
    if (route("kaka-air") != 0) return 2;
    int aN, ay0, ay1, aMaxLight; unsigned aCol;
    player_stats(480, &aN, &ay0, &ay1, &aCol, &aMaxLight);
    int ndist = 0; for (int b = 0; b < 32; b++) if (gCol & (1u << b)) ndist++;
    printf("  player grounded n=%d y[%d..%d] h=%d distinctColours=%d maxLightPrim=%d\n",
           gN, gy0, gy1, gy1 - gy0, ndist, gMaxLight);
    printf("  player airborne n=%d y[%d..%d] dyMin=%d dyMax=%d\n",
           aN, ay0, ay1, ay0 - gy0, ay1 - gy1);
    if (gN < 6) { printf("  FAIL: ranger not composed of the full sprite (n<6)\n"); fails++; }
    if (gy1 - gy0 < 60) { printf("  FAIL: ranger not human-sized\n"); fails++; }
    if (ndist < 4) { printf("  FAIL: ranger palette too flat (white blob?)\n"); fails++; }
    if (gMaxLight > 22) { printf("  FAIL: a huge cream/white primitive dominates the ranger\n"); fails++; }
    if (aN != gN) { printf("  FAIL: jump changed the ranger primitive count\n"); fails++; }
    if ((ay0 - gy0) != (ay1 - gy1) || (ay0 - gy0) > -50) {
        printf("  FAIL: jump did not translate the whole ranger together\n"); fails++;
    }

    if (fails == 0) { printf("kaka-render-test PASS\n"); return 0; }
    printf("kaka-render-test FAIL (%d)\n", fails);
    return 1;
}
