#define _POSIX_C_SOURCE 200809L
#include "gv_glon_host.h"
#include "gv_view.h"
#include "r0_s1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : "..";
    gv_view *v = gv_view_new(640, 400);
    if (!v) return 1;
    gv_glon_host *h = gv_glon_host_new(v, NULL);
    if (!h || gv_glon_host_load(h, root) != 0 || gv_glon_host_build(h) != 0) {
        return 1;
    }

    printf("start heap_high=%ld gc_count=%ld live_cells=%ld\n",
           r0_s1_heap_high(), r0_s1_gc_count(), r0_s1_gc_live_cells());

    int chars = argc > 2 ? atoi(argv[2]) : 1000;
    int gc_each = argc > 3 && strcmp(argv[3], "gc") == 0;
    cell collect_prog = 0;
    if (gc_each) {
        char src[160];
        snprintf(src, sizeof src,
                 "[ collect: raw [ CALL %ld ARITY 0 EXIT ] collect ]",
                 (long)r0_s1_gc_collect_addr());
        int perr = 0;
        collect_prog = r0_s1_parse(src, &perr);
        if (perr) return 1;
    }
    int ok = 0;
    double t0 = now_ms();
    for (int i = 0; i < chars; i++) {
        if (gc_each) r0_s1_run_persistent(collect_prog);
        if (gv_glon_host_char(h, "a") != 0) {
            printf("failed_at=%d\n", i);
            break;
        }
        ok++;
    }
    double t1 = now_ms();

    char buf[131072];
    size_t len = 0;
    if (gv_glon_host_text(h, "v/field-text", buf, sizeof buf) == 0) {
        len = strlen(buf);
    }
    printf("chars_requested=%d chars_ok=%d total_ms=%.3f us_per_char=%.2f text_len=%zu\n",
           chars, ok, t1 - t0, ok ? (t1 - t0) * 1000.0 / ok : 0.0, len);
    printf("end heap_high=%ld gc_count=%ld live_cells=%ld live_objs=%ld\n",
           r0_s1_heap_high(), r0_s1_gc_count(), r0_s1_gc_live_cells(),
           r0_s1_gc_live_objs());

    gv_glon_host_free(h);
    gv_view_free(v);
    return 0;
}