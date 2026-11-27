#define _POSIX_C_SOURCE 200809L
#include "gv_table.h"
#include "gv_view.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

int main(int argc, char **argv) {
    int rows = argc > 1 ? atoi(argv[1]) : 300;
    if (rows < 0) return 1;

    gv_view *v = gv_view_new(800, 600);
    gv_node_id root = gv_view_root(v);
    gv_node_id node = gv_view_add(v, root, GV_NODE_CUSTOM);
    gv_view_set_rect(v, node, 0, 0, 800, 600);
    if (!v || !node) return 1;

    double t0 = now_ms();
    gv_table *t = gv_table_create(1, rows);
    double t1 = now_ms();
    if (!t) return 1;
    int ops = gv_table_render(v, node, t, (gv_rect){0, 0, 800, 600});
    double t2 = now_ms();
    gv_table_sort(t, 2, -1);
    double t3 = now_ms();
    gv_table_filter(t, "Lapsed");
    double t4 = now_ms();
    int filtered = t->view_count;
    gv_table_filter(t, "");
    gv_table_select(t, 0, 1);
    gv_table_render(v, node, t, (gv_rect){0, 0, 800, 600});
    double t5 = now_ms();

    printf("rows=%d create_ms=%.3f render_ops=%d render_ms=%.3f sort_ms=%.3f filter_ms=%.3f filtered=%d rerender_ms=%.3f\n",
           rows, t1 - t0, ops, t2 - t1, t3 - t2, t4 - t3, filtered, t5 - t4);
    printf("visible_rows=%d view_count=%d\n", gv_table_visible_rows(t, 600), t->view_count);
    gv_table_free(t);
    gv_view_free(v);
    return 0;
}
