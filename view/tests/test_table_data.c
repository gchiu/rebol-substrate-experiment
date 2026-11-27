#include "gv_glon_host.h"
#include "gv_view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : "..";
    gv_view *v = gv_view_new(760, 300);
    CHECK(v != NULL);
    gv_glon_host *h = gv_glon_host_new(v, NULL);
    CHECK(h != NULL);
    CHECK(gv_glon_host_load_vid_app(h, root, "view/glon/table_data.glon") == 0);
    CHECK(!gv_glon_host_error(h));
    CHECK(gv_glon_host_build_app(h, NULL) == 0);
    CHECK(!gv_glon_host_error(h));

    char ids[64];
    CHECK(gv_glon_host_text(h, "v/table1", ids, sizeof ids) == 0);
    int table_id = atoi(ids);
    CHECK(table_id > 1);
    gv_table *t = NULL;
    CHECK(gv_glon_host_table(h, table_id, &t) == 0);
    CHECK(t != NULL);
    if (t) {
        CHECK(t->nrows == 3);
        CHECK(t->ncols == 3);
        CHECK(t->cols[0].width == 160);
        CHECK(t->cols[1].width == 320);
        CHECK(t->cols[2].width == 140);
        CHECK(strcmp(gv_table_get_cell(t, 0, 0), "Alice") == 0);
        CHECK(strcmp(gv_table_get_cell(t, 0, 1), "semi;colon") == 0);
        CHECK(strcmp(gv_table_get_cell(t, 0, 2), "π") == 0);
        CHECK(strcmp(gv_table_get_cell(t, 1, 1), "pipe|char") == 0);
        CHECK(strcmp(gv_table_get_cell(t, 1, 2), "λ") == 0);
        CHECK(strcmp(gv_table_get_cell(t, 2, 1), "line\nbreak") == 0);
        CHECK(strcmp(gv_table_get_cell(t, 2, 2), "漢字") == 0);
    }

    gv_glon_host_free(h);
    gv_view_free(v);
    if (failures) {
        fprintf(stderr, "FAILED: %d check(s)\n", failures);
        return 1;
    }
    printf("PASS: GV-2 structured table data tests\n");
    return 0;
}
