/* opcost.c - MASM-3 operation-cost harness.
 *
 * Loads the shipped libraries, then a benchmark source that defines `bench`.
 * `[ bench ]` is parsed once and run repeatedly; wall time and S1 event counts
 * are reported. The empty-block cost is measured separately and subtracted by
 * the driver script so per-operation costs exclude session/run overhead.
 *
 * Two builds are used:
 *   opcost       links the shipped s1.c          -> trustworthy wall time
 *   opcost-prof  links the instrumented s1_prof.c -> opcode/HOST counts
 *
 * This file is an experiment; it changes nothing in the runtime or libraries.
 */
#define _GNU_SOURCE
#include "r0_s1.h"
#include "s1.h"
#ifdef USE_S1_PROF
#include "s1_prof.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *s = malloc((size_t)n + 1);
    if (!s) { fclose(f); return NULL; }
    size_t g = fread(s, 1, (size_t)n, f);
    fclose(f);
    s[g] = 0;
    return s;
}

static void strip(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
}

static int load(const char *path) {
    char *s = read_file(path);
    if (!s) { fprintf(stderr, "read %s\n", path); return -1; }
    strip(s);
    int e = 0;
    cell b = r0_s1_parse(s, &e);
    free(s);
    if (e) { fprintf(stderr, "parse %s\n", path); return -1; }
    r0_s1_run_persistent(b);
    if (!r0_s1_ran_cleanly()) { fprintf(stderr, "run %s\n", path); return -1; }
    return 0;
}

int main(int argc, char **argv) {
    const char *bench_path = NULL;
    const char *bench_word = "bench";
    const char *libs[16];
    int nlibs = 0;
    int reps = 1000, rounds = 3;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--reps") && i + 1 < argc) reps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rounds") && i + 1 < argc) rounds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--lib") && i + 1 < argc && nlibs < 16) libs[nlibs++] = argv[++i];
        else if (!strcmp(argv[i], "--word") && i + 1 < argc) bench_word = argv[++i];
        else bench_path = argv[i];
    }
    if (!bench_path) {
        fprintf(stderr, "usage: opcost [--reps N] [--rounds N] BENCH.glon\n");
        return 2;
    }

    r0_s1_init();
    if (load("glon-lib/prelude.glon") ||
        load("glon-lib/strings.glon") ||
        load("glon-lib/strings-ext.glon") ||
        load("glon-lib/file.glon")) return 1;
    for (int i = 0; i < nlibs; i++) if (load(libs[i])) return 1;
    if (load(bench_path)) return 1;

    char call[128];
    snprintf(call, sizeof call, "[ %s DROP ]", bench_word);
    int e = 0;
    cell prog = r0_s1_parse(call, &e);
    if (e) { fprintf(stderr, "parse %s\n", call); return 1; }

    /* warm-up */
    r0_s1_outcome oc;
    r0_s1_session_run_block(prog, &oc);

    long gc0 = r0_s1_gc_count();
    double best = 1e30;
    for (int r = 0; r < rounds; r++) {
        double t0 = now();
        for (int i = 0; i < reps; i++) r0_s1_session_run_block(prog, &oc);
        double dt = now() - t0;
        if (dt < best) best = dt;
    }
    long gc = r0_s1_gc_count() - gc0;

#ifdef USE_S1_PROF
    /* one instrumented run gives exact event counts per invocation */
    s1_prof_reset();
    r0_s1_session_run_block(prog, &oc);
    long ops = 0;
    for (int i = 0; i < 8; i++) ops += s1_prof_op_count[i];
    long hosts = 0;
    for (int i = 0; i < 16; i++) hosts += s1_prof_host_count[i];
#else
    long ops = 0, hosts = 0;
#endif

    printf("name=%s reps=%d seconds_per_call=%.9f gc_per_call=%.6f "
           "opcodes_per_call=%ld hosts_per_call=%ld "
           "live_cells=%ld heap_high=%ld\n",
           bench_path, reps, best / reps, (double)gc / reps,
           ops, hosts, r0_s1_gc_live_cells(), r0_s1_heap_high());
    return r0_s1_ran_cleanly() ? 0 : 1;
}
