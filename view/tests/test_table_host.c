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

static int has_text(gv_view *v, gv_node_id id, const char *needle) {
    gv_node *n = gv_view_node(v, id);
    if (!n) return 0;
    if (n->text && strstr(n->text, needle)) return 1;
    for (gv_node_id c = n->first_child; c; ) {
        gv_node *cn = gv_view_node(v, c);
        gv_node_id next = cn ? cn->next_sibling : 0;
        if (has_text(v, c, needle)) return 1;
        c = next;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : "..";
    gv_view *v = gv_view_new(900, 420);
    CHECK(v != NULL);
    gv_glon_host *h = gv_glon_host_new(v, NULL);
    CHECK(h != NULL);
    CHECK(gv_glon_host_load_app(h, root, "view/glon/table_demo.glon") == 0);
    if (gv_glon_host_error(h)) {
        fprintf(stderr, "table host load failed\n");
        return 1;
    }
    CHECK(gv_glon_host_build_app(h, NULL) == 0);
    CHECK(!gv_glon_host_error(h));
    char id1s[64], id2s[64];
    CHECK(gv_glon_host_text(h, "v/table1", id1s, sizeof id1s) == 0);
    CHECK(gv_glon_host_text(h, "v/table2", id2s, sizeof id2s) == 0);
    int id1 = atoi(id1s);
    int id2 = atoi(id2s);
    CHECK(id1 > 1);
    CHECK(id2 > 1);
    gv_node *root_node = gv_view_node(v, gv_view_root(v));
    CHECK(root_node && root_node->w == 900);
    gv_rect sr2 = {0};
    CHECK(gv_view_screen_rect(v, gv_glon_host_view_node(h, id2), &sr2) == 0);
    CHECK(sr2.x + sr2.w <= 900);

    gv_table *t1 = NULL, *t2 = NULL;
    CHECK(gv_glon_host_table(h, id1, &t1) == 0);
    CHECK(gv_glon_host_table(h, id2, &t2) == 0);
    CHECK(t1 && t2);
    if (!t1 || !t2) return 1;
    CHECK(t1->nrows == 5);
    CHECK(t2->nrows == 5);
    CHECK(t1->ncols == 4);
    CHECK(t1 != t2);
    CHECK(gv_view_node(v, gv_glon_host_view_node(h, id1))->type == GV_NODE_CUSTOM);
    CHECK(gv_view_node(v, gv_glon_host_view_node(h, id2))->type == GV_NODE_CUSTOM);

    char btn[64];
    CHECK(gv_glon_host_text(h, "btn1", btn, sizeof btn) == 0);
    gv_node_id btn_node = gv_glon_host_view_node(h, atoi(btn));
    CHECK(btn_node > 0);
    CHECK(has_text(v, btn_node, "Add"));
    CHECK(has_text(v, gv_glon_host_view_node(h, atoi(btn) + 200), "Add"));

    gv_rect sr = {0, 0, 0, 0};
    CHECK(gv_view_screen_rect(v, gv_glon_host_view_node(h, id1), &sr) == 0);
    CHECK(gv_glon_host_click_xy(h, id1, sr.x + 40,
                                sr.y + t1->header_h + t1->row_h + 5) == 0);
    CHECK(gv_table_focus_row(t1) == 1);
    CHECK(gv_table_focus_col(t1) == 0);
    int row_id1 = gv_table_row_id(t1, 1);
    CHECK(row_id1 > 0);

    int nodes_before = gv_view_stats_nodes_created(v);
    int layouts_before = gv_view_stats_layouts(v);
    CHECK(gv_table_set_cell_inplace(t1, 0, 0, "Incremental") == 0);
    CHECK(gv_view_stats_nodes_created(v) == nodes_before);
    CHECK(gv_view_stats_layouts(v) == layouts_before);
    CHECK(strcmp(gv_table_get_cell(t1, 0, 0), "Incremental") == 0);

    CHECK(gv_glon_host_key(h, 1025) == 0);
    CHECK(gv_table_focus_row(t1) == 2);

    char before[128];
    snprintf(before, sizeof before, "%s", gv_table_get_cell(t1, 2, 0));
    CHECK(gv_glon_host_key(h, 13) == 0);
    CHECK(gv_glon_host_char_event(h, "X") == 0);
    CHECK(gv_glon_host_key(h, 13) == 0);
    CHECK(strcmp(gv_table_get_cell(t1, 2, 0), "X") == 0);

    int rows_before = t1->nrows;
    CHECK(gv_glon_host_key(h, 65) == 0);
    CHECK(t1->nrows == rows_before + 1);
    CHECK(gv_glon_host_key(h, 68) == 0);
    CHECK(t1->nrows == rows_before);

    CHECK(gv_glon_host_key(h, 83) == 0);
    CHECK(t1->sort_col == 0);
    CHECK(gv_table_is_selected_id(t1, row_id1));

    char status[256];
    CHECK(gv_glon_host_table_status(h, id1, status, sizeof status) == 0);
    CHECK(strstr(status, "rows=") != NULL);

    gv_glon_host_free(h);
    gv_view_free(v);
    if (failures) {
        fprintf(stderr, "FAILED: %d check(s)\n", failures);
        return 1;
    }
    printf("PASS: GV-2 host-backed table integration tests\n");
    return 0;
}
