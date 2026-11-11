#ifndef B3_BENCH_UTIL_H
#define B3_BENCH_UTIL_H

#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

typedef void (*bench_fn)(void *ctx);

typedef struct {
    double median;   /* seconds per call */
    double min;
    double max;
    double spread;   /* (max - min) / median, percent */
    long iters;      /* calls per timed batch */
} bench_stats;

static inline double bench_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static inline int bench_cmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Auto-calibrate the batch until it takes at least target seconds, then run
 * trials batches. Reports seconds per call, not total batch time. */
static inline bench_stats bench_measure(bench_fn fn, void *ctx, double target, int trials) {
    bench_stats st = {0, 0, 0, 0, 1};
    if (trials < 1) trials = 1;
    if (trials > 32) trials = 32;

    for (;;) {
        double t0 = bench_now();
        for (long i = 0; i < st.iters; i++) fn(ctx);
        double elapsed = bench_now() - t0;
        if (elapsed >= target || st.iters >= 100000000L) break;
        double scale = elapsed > 0 ? (target / elapsed) * 1.5 : 2.0;
        if (scale < 2.0) scale = 2.0;
        if (scale > 8.0) scale = 8.0;
        long next = (long)((double)st.iters * scale);
        if (next <= st.iters) next = st.iters + 1;
        st.iters = next;
    }

    double times[32];
    for (int t = 0; t < trials; t++) {
        double t0 = bench_now();
        for (long i = 0; i < st.iters; i++) fn(ctx);
        times[t] = (bench_now() - t0) / (double)st.iters;
    }
    qsort(times, (size_t)trials, sizeof(double), bench_cmp);
    st.min = times[0];
    st.max = times[trials - 1];
    st.median = (trials & 1) ? times[trials / 2]
                             : (times[trials / 2 - 1] + times[trials / 2]) / 2.0;
    st.spread = st.median > 0 ? 100.0 * (st.max - st.min) / st.median : 0.0;
    return st;
}

#endif
