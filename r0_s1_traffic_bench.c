/* r0_s1_traffic_bench.c - load-once benchmark for the Glon Alpha IDM traffic
 * simulator (post-Alpha application; no runtime/language change).
 *
 * This measures the ACTUAL simulation cost, separated from re-parse overhead:
 * the bootstrap + demo are loaded once, the initialisation runs once, and then
 * a pre-parsed `advance 25` block is executed repeatedly through the existing
 * public persistent-run entry point (r0_s1_run_persistent). The `advance` batch
 * (25 synchronous ticks) stays under the frozen return-stack depth limit while
 * amortising the per-call host entry over 25 ticks.
 *
 * The make-built runtime object (r0_s1_runtime.o) is compiled -O0; for a fair
 * read of interpreter cost vs C optimisation, build with both the shipped -O0
 * object and a directly-compiled -O2 runtime (see the Makefile target).
 *
 * Usage: ./traffic-bench [ticks] [batch]
 */

#define _POSIX_C_SOURCE 199309L
#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static char src[65536];

static void strip_comments(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
}

static void load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    size_t n = fread(src, 1, sizeof src - 1, f); fclose(f); src[n] = 0;
    strip_comments(src);
    int err = 0; cell b = r0_s1_parse(src, &err);
    if (!err) r0_s1_run_persistent(b);
}

static double now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
    long ticks = argc > 1 ? atol(argv[1]) : 3000;
    long batch = argc > 2 ? atol(argv[2]) : 25;

    cell me = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = me;
    M[M1_CURSOR] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    double t0 = now_ms();
    load_file("demo/shop/bootstrap.glon");
    load_file("demo/shop/demos/traffic.glon");
    double t1 = now_ms();

    int err = 0;
    char form[64];
    if (batch == 1) strcpy(form, "[ step ]");
    else snprintf(form, sizeof form, "[ advance %ld ]", batch);
    strcpy(src, form);
    cell bb = r0_s1_parse(src, &err);
    strcpy(src, "[ init ]");
    cell bi = r0_s1_parse(src, &err);
    r0_s1_run_persistent(bi);
    double t2 = now_ms();

    long loops = ticks / batch;
    for (long i = 0; i < loops; i++) r0_s1_run_persistent(bb);
    double t3 = now_ms();

    long total_ticks = loops * batch;
    long updates = total_ticks * 25;
    double sim_ms = t3 - t2;

    printf("scenario: 25 vehicles, ring 1000 m, IDM, dt = 0.1 s\n");
    printf("batch          = %ld ticks per host call\n", batch);
    printf("host calls     = %ld\n", loops);
    printf("total ticks    = %ld  (%.0f simulated seconds)\n", total_ticks, total_ticks * 0.1);
    printf("vehicle updates= %ld\n", updates);
    printf("source load    = %.2f ms\n", t1 - t0);
    printf("init           = %.2f ms\n", t2 - t1);
    printf("simulation     = %.2f ms\n", sim_ms);
    printf("ms/tick        = %.3f\n", sim_ms / total_ticks);
    printf("updates/sec    = %.0f\n", updates / (sim_ms / 1000.0));
    printf("sim_sec/wall   = %.3f\n", (total_ticks * 0.1) / (sim_ms / 1000.0));
    return 0;
}
