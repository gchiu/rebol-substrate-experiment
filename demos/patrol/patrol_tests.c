/* demos/patrol/patrol_tests.c -- deterministic Glon Patrol rule tests.
 *
 * Loads the real Glon game and dispatches its `patrol-selftest` event; the
 * game runs its own rule assertions in Glon and reports PASS/FAIL, so the C
 * side only reads the result text.  All game rules live in patrol.glon.
 */
#include "r0_s1.h"
#include "r0_s1_g1a.h"
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static char filebuf[1 << 20];
static char wrapbuf[1 << 20];

static int load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "patrol-test: cannot open %s\n", path); return -1; }
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
    if (err) { fprintf(stderr, "patrol-test: parse error in %s (%d)\n", path, err); return -2; }
    r0_s1_run_persistent(b);
    if (!r0_s1_ran_cleanly()) { fprintf(stderr, "patrol-test: run failed in %s\n", path); return -3; }
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
    if (load_file("demos/patrol/patrol.glon") != 0) return 2;

    static char out[65536];
    int out_len = 0;
    if (r0_s1_g1a_event_value("patrol-start", "", out, (int)sizeof out, &out_len) != 0) {
        printf("patrol-test: start dispatch failed\n");
        return 1;
    }
    if (out_len < (int)sizeof out) out[out_len] = 0;
    printf("start render: %s\n", out);
    if (r0_s1_g1a_event_value("patrol-selftest", "", out, (int)sizeof out, &out_len) != 0) {
        printf("patrol-test: dispatch failed\n");
        return 1;
    }
    if (out_len < (int)sizeof out) out[out_len] = 0;

    /* Read the game's own output buffer (it emits the result via e-*). */
    char gout[65536];
    cell n = M[G1_OUT];
    if (n < 0) n = 0;
    if (n > (cell)sizeof gout - 1) n = (cell)sizeof gout - 1;
    for (cell i = 0; i < n; i++) gout[i] = (char)int_val(M[G1_OUT_DATA + i]);
    gout[n] = 0;

    printf("render: %s\n", out);
    printf("emitted: %s\n", gout);
    /* rough per-tick cost (native, -O0): each tick is one G1A dispatch */
    clock_t t0 = clock();
    for (int i = 0; i < 2000; i++)
        r0_s1_g1a_event_value("patrol-tick", "", out, (int)sizeof out, &out_len);
    clock_t t1 = clock();
    double ms = 1000.0 * (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
    printf("perf: 2000 ticks in %.1f ms (%.3f ms/tick, native -O0)\n", ms, ms / 2000.0);

    if (strstr(gout, "PATROL-SELFTEST PASS")) { printf("patrol-test PASS\n"); return 0; }
    printf("patrol-test FAIL\n");
    return 1;
}
