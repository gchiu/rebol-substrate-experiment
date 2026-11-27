#define _POSIX_C_SOURCE 200809L
#include "gv_glon_host.h"
#include "gv_view.h"
#include "r0_s1.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void gc_collect(gv_glon_host *h) {
    char src[160];
    snprintf(src, sizeof src,
             "[ collect: raw [ CALL %ld ARITY 0 EXIT ] collect ]",
             (long)r0_s1_gc_collect_addr());
    if (gv_glon_host_run(h, src) != 0) {
        fprintf(stderr, "measure: forced GC failed\n");
    }
}

static void print_gc(const char *label) {
    printf("%s gc_count=%ld live_cells=%ld live_objs=%ld free_cells=%ld heap_high=%ld\n",
           label, r0_s1_gc_count(), r0_s1_gc_live_cells(),
           r0_s1_gc_live_objs(), r0_s1_gc_free_cells(), r0_s1_heap_high());
}

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : "..";

    gv_view *v = gv_view_new(640, 400);
    if (!v) return 1;
    gv_glon_host *h = gv_glon_host_new(v, NULL);
    if (!h) return 1;
    if (gv_glon_host_load(h, root) != 0) return 1;
    if (gv_glon_host_build(h) != 0) {
        fprintf(stderr, "measure: build failed\n");
        return 1;
    }

    int nodes = gv_view_live_nodes(v);
    printf("nodes=%d capacity=%d sizeof_gv_node=%zu node_array_bytes=%zu\n",
           nodes, gv_view_node_capacity(v), sizeof(gv_node),
           (size_t)nodes * sizeof(gv_node));
    printf("managed_heap_cells=%d\n", (int)(GC_HEAP_LIMIT - GC_HEAP_BASE));

    print_gc("after_build");
    gc_collect(h);
    print_gc("after_gc");

    gv_display_list dl;
    gv_display_list_init(&dl);
    int iterations = 200;
    double t0 = now_ms();
    for (int i = 0; i < iterations; i++) {
        gv_view_layout(v);
        gv_display_list_free(&dl);
        gv_display_list_init(&dl);
        gv_view_build_display_list(v, &dl);
    }
    double t1 = now_ms();
    printf("layout_build iterations=%d total_ms=%.3f us_per_op=%.2f draw_ops=%d\n",
           iterations, t1 - t0, (t1 - t0) * 1000.0 / iterations, dl.count);
    gv_display_list_free(&dl);

    gv_view_destroy(v, gv_view_root(v));
    gc_collect(h);
    print_gc("after_destroy");
    printf("after_destroy live_nodes=%d\n", gv_view_live_nodes(v));

    gv_glon_host_free(h);
    gv_view_free(v);
    return 0;
}