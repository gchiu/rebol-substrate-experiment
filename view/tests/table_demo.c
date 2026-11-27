#include "gv_glon_host.h"
#include "gv_view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : "..";
    gv_view *v = gv_view_new(900, 600);
    if (!v) return 1;
    gv_glon_host *h = gv_glon_host_new(v, NULL);
    if (!h || gv_glon_host_load(h, root) != 0 || gv_glon_host_build(h) != 0) {
        fprintf(stderr, "table demo: build failed\n");
        gv_glon_host_free(h);
        gv_view_free(v);
        return 1;
    }

    gv_table *t = NULL;
    if (gv_glon_host_table(h, 19, &t) != 0 || !t) {
        fprintf(stderr, "table demo: table missing\n");
        gv_glon_host_free(h);
        gv_view_free(v);
        return 1;
    }

    printf("rows=%d cols=%d view=%d\n", t->nrows, t->ncols, t->view_count);
    printf("sort: status descending\n");
    gv_glon_host_table_command(h, 19, 1, 2, -1, NULL);
    printf("filter: Lapsed\n");
    gv_glon_host_table_command(h, 19, 2, 0, 0, "Lapsed");
    printf("select first visible\n");
    gv_glon_host_table_command(h, 19, 3, 0, 1, NULL);

    char status[256];
    if (gv_glon_host_table_status(h, 19, status, sizeof status) == 0)
        printf("%s\n", status);

    gv_glon_host_free(h);
    gv_view_free(v);
    printf("PASS: GV-2A table demo\n");
    return 0;
}
