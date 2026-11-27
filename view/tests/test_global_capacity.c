#define _POSIX_C_SOURCE 200809L
/* M-1: profile global-namespace capacity test.
 *
 * Loads the complete GUI-3 toolkit, declares independent application globals,
 * checks values/Symbols/GC/GUI operation, then fills to the configured global
 * context capacity and checks a deterministic one-over CONTEXT_FULL failure.
 */
#include "gv_glon_host.h"
#include "gv_view.h"
#include "gv_backend.h"
#include "r0_s1.h"
#include "m1_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static int run(gv_glon_host *h, const char *src) {
    if (gv_glon_host_run(h, src) != 0) {
        fprintf(stderr, "M1: run failed: %s (halt=%d)\n", src, r0_s1_halt_reason());
        return -1;
    }
    return 0;
}

static int int_expr(gv_glon_host *h, const char *name, long *out) {
    char src[96];
    snprintf(src, sizeof src, "[ %s ]", name);
    if (run(h, src) != 0) return -1;
    cell v = r0_s1_result(0, 1);
    if ((v & 15) != T_INT) return -1;
    *out = (long)int_val(v);
    return 0;
}

static int bare_run(const char *src) {
    int perr = 0;
    cell prog = r0_s1_parse(src, &perr);
    if (perr) return -1;
    int n = r0_s1_run_persistent(prog);
    return (r0_s1_ran_cleanly() && n >= 0) ? 0 : -1;
}

static int bare_declare_range(const char *prefix, int start, int count) {
    char src[8192];
    int n = 0;
    n += snprintf(src + n, sizeof src - (size_t)n, "[ ");
    for (int i = 0; i < count && n < (int)sizeof src - 96; i++)
        n += snprintf(src + n, sizeof src - (size_t)n, "%s%d: %d ", prefix, start + i, start + i);
    n += snprintf(src + n, sizeof src - (size_t)n, "]");
    return bare_run(src);
}

static int bare_int_expr(const char *name, long *out) {
    char src[96];
    snprintf(src, sizeof src, "[ %s ]", name);
    if (bare_run(src) != 0) return -1;
    cell v = r0_s1_result(0, 1);
    if ((v & 15) != T_INT) return -1;
    *out = (long)int_val(v);
    return 0;
}

static int declare_range(gv_glon_host *h, const char *prefix, int start, int count) {
    char src[8192];
    int n = 0;
    n += snprintf(src + n, sizeof src - (size_t)n, "[ ");
    for (int i = 0; i < count && n < (int)sizeof src - 96; i++) {
        n += snprintf(src + n, sizeof src - (size_t)n,
                      "%s%d: %d ", prefix, start + i, start + i);
    }
    n += snprintf(src + n, sizeof src - (size_t)n, "]");
    return run(h, src);
}

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : ".";
    int add = argc > 2 ? atoi(argv[2]) : 1000;
    int fill = argc > 3 ? atoi(argv[3]) : 0;

    int bare = argc > 4 && strcmp(argv[4], "bare") == 0;
    if (bare) {
        r0_s1_init();
        cell base = 0, cap = 0;
        r0_s1_global_usage(&base, &cap);
        if (cap != R0S1_GLOBAL_CAP || base + add > cap) {
            fprintf(stderr, "M1: bare capacity base=%ld add=%d cap=%ld\n",
                    (long)base, add, (long)cap);
            return 1;
        }
        double t0 = now_ms();
        for (int i = 0; i < add; i += 16) {
            int n = add - i < 16 ? add - i : 16;
            if (bare_declare_range("app-g", i, n) != 0) return 1;
        }
        double t1 = now_ms();
        long v0 = 0, vm = 0, vl = 0;
        char last[32];
        snprintf(last, sizeof last, "app-g%d", add - 1);
        if (add <= 0 || bare_int_expr("app-g0", &v0) != 0 ||
            bare_int_expr(last, &vm) != 0 || bare_int_expr("app-g3", &vl) != 0 ||
            v0 != 0 || vm != add - 1 || (add > 3 && vl != 3)) {
            fprintf(stderr, "M1: bare application lookup failed\n");
            return 1;
        }
        /* The bare profile has no prelude `gc/collect` helper.  GC is covered
         * by the toolkit profiles (and by the runtime's own collector tests). */
        cell used = 0, cap2 = 0;
        r0_s1_global_usage(&used, &cap2);
        if (fill) {
            int remaining = (int)(cap2 - used);
            for (int i = 0; i < remaining; i += 16) {
                int n = remaining - i < 16 ? remaining - i : 16;
                if (bare_declare_range("fill-g", i, n) != 0) return 1;
            }
            r0_s1_global_usage(&used, &cap2);
            if (used != cap2 || bare_run("[ overflow-one: 1 ]") == 0 ||
                r0_s1_halt_reason() != R0S1_HALT_CONTEXT_FULL) {
                fprintf(stderr, "M1: bare one-over failure not deterministic\n");
                return 1;
            }
            cell after = 0, cap3 = 0;
            r0_s1_global_usage(&after, &cap3);
            if (after != cap3) return 1;
        }
        printf("M1_GLOBAL_CAPACITY_BARE PASS cap=%ld base=%ld added=%d fill=%d "
               "heap_high=%ld gc=%ld live_cells=%ld elapsed_ms=%.1f\n",
               (long)cap, (long)base, add, fill, r0_s1_heap_high(), r0_s1_gc_count(),
               r0_s1_gc_live_cells(), t1 - t0);
        return 0;
    }

    gv_view *v = gv_view_new(800, 500);
    if (!v) return 1;
    gv_backend_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.width = 800;
    cfg.height = 500;
    cfg.headless = 1;
    gv_backend *b = gv_backend_headless();
    if (!b || b->init(b, &cfg) != 0 || b->create_window(b, 800, 500, "M-1") != 0) return 1;
    gv_glon_host *h = gv_glon_host_new(v, b);
    if (!h || gv_glon_host_load_gui3_app(h, root, "view/glon/gui3_demo.glon") != 0 ||
        gv_glon_host_build_app(h, NULL) != 0 || gv_glon_host_bind(h) != 0) {
        fprintf(stderr, "M1: GUI toolkit load failed\n");
        return 1;
    }

    cell base = 0, cap = 0;
    r0_s1_global_usage(&base, &cap);
    if (cap != R0S1_GLOBAL_CAP) {
        fprintf(stderr, "M1: cap %ld does not match R0S1_GLOBAL_CAP %d\n",
                (long)cap, R0S1_GLOBAL_CAP);
        return 1;
    }
    if (base + add > cap) {
        fprintf(stderr, "M1: base %ld + add %d exceeds cap %ld\n",
                (long)base, add, (long)cap);
        return 1;
    }

    double t0 = now_ms();
    for (int i = 0; i < add; i += 16) {
        int n = add - i < 16 ? add - i : 16;
        if (declare_range(h, "app-g", i, n) != 0) return 1;
    }
    double t1 = now_ms();

    /* independent names and values */
    long v0 = 0, vm = 0, vl = 0;
    char last[32];
    snprintf(last, sizeof last, "app-g%d", add - 1);
    if (add <= 0 || int_expr(h, "app-g0", &v0) != 0 ||
        int_expr(h, last, &vm) != 0 || int_expr(h, "app-g3", &vl) != 0) {
        fprintf(stderr, "M1: application lookup failed\n");
        return 1;
    }
    if (v0 != 0 || vm != add - 1 || (add > 3 && vl != 3)) {
        fprintf(stderr, "M1: application values wrong (%ld %ld %ld)\n", v0, vm, vl);
        return 1;
    }

    /* GUI must still accept events after application bindings exist. */
    if (gv_glon_host_char_event(h, "Z") != 0 || gv_glon_host_error(h)) {
        fprintf(stderr, "M1: GUI event failed after application globals\n");
        return 1;
    }

    /* GC must retain application values. */
    if (run(h, "gc/collect") != 0 || int_expr(h, last, &vm) != 0 || vm != add - 1) {
        fprintf(stderr, "M1: application values did not survive GC\n");
        return 1;
    }

    cell used = 0, cap2 = 0;
    r0_s1_global_usage(&used, &cap2);
    if (fill) {
        int remaining = (int)(cap2 - used);
        for (int i = 0; i < remaining; i += 16) {
            int n = remaining - i < 16 ? remaining - i : 16;
            if (declare_range(h, "fill-g", i, n) != 0) return 1;
        }
        r0_s1_global_usage(&used, &cap2);
        if (used != cap2) {
            fprintf(stderr, "M1: fill ended at %ld/%ld, not full\n", (long)used, (long)cap2);
            return 1;
        }
        if (gv_glon_host_run(h, "[ overflow-one: 1 ]") == 0) {
            fprintf(stderr, "M1: one-over binding unexpectedly succeeded\n");
            return 1;
        }
        if (r0_s1_halt_reason() != R0S1_HALT_CONTEXT_FULL) {
            fprintf(stderr, "M1: one-over halt=%d, expected CONTEXT_FULL\n", r0_s1_halt_reason());
            return 1;
        }
        cell after = 0, cap3 = 0;
        r0_s1_global_usage(&after, &cap3);
        if (after != cap3) {
            fprintf(stderr, "M1: one-over changed binding count %ld/%ld\n", (long)after, (long)cap3);
            return 1;
        }
    }

    printf("M1_GLOBAL_CAPACITY PASS cap=%ld base=%ld added=%d fill=%d "
           "heap_high=%ld gc=%ld live_cells=%ld elapsed_ms=%.1f\n",
           (long)cap, (long)base, add, fill, r0_s1_heap_high(), r0_s1_gc_count(),
           r0_s1_gc_live_cells(), t1 - t0);
    return 0;
}
