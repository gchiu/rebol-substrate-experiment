#include "gv_table.h"
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

int main(void) {
    gv_table *t = gv_table_create(1, 300);
    CHECK(t != NULL);
    CHECK(t->nrows == 300);
    CHECK(t->view_count == 300);
    CHECK(t->ncols == 4);
    CHECK(strcmp(gv_table_get_cell(t, 0, 0), "Member 1") == 0);

    CHECK(gv_table_set_cell(t, 0, 0, "Edited") == 0);
    CHECK(strcmp(gv_table_get_cell(t, 0, 0), "Edited") == 0);

    gv_table *u = gv_table_create(2, 0);
    CHECK(u != NULL);
    CHECK(gv_table_set_columns(u, "Name:180:0;Age:60:2;City:140:0;Note:200:0") == 0);
    CHECK(u->ncols == 4);
    CHECK(u->cols[0].width == 180);
    CHECK(u->cols[1].width == 60);
    CHECK(u->cols[2].width == 140);
    CHECK(u->cols[3].width == 200);
    CHECK(gv_table_load_rows(u, "Alice|30|Wellington;Bob|25|Auckland") == 0);
    CHECK(strcmp(gv_table_get_cell(u, 0, 1), "30") == 0);
    CHECK(gv_table_load_rows(u, "A\\|B|C\\;D|E\\nF|π") == 0);
    CHECK(strcmp(gv_table_get_cell(u, 0, 0), "A|B") == 0);
    CHECK(strcmp(gv_table_get_cell(u, 0, 1), "C;D") == 0);
    CHECK(strcmp(gv_table_get_cell(u, 0, 2), "E\nF") == 0);
    CHECK(strcmp(gv_table_get_cell(u, 0, 3), "π") == 0);
    gv_table_free(u);

    gv_table_sort(t, 2, 1);
    CHECK(strcmp(gv_table_get_cell(t, 0, 2), "Active") == 0);
    gv_table_sort(t, 2, -1);
    CHECK(strcmp(gv_table_get_cell(t, 0, 2), "Lapsed") == 0);

    gv_table_filter(t, "Lapsed");
    CHECK(t->view_count < 300);
    CHECK(t->view_count > 0);
    gv_table_filter(t, "");
    CHECK(t->view_count == 300);

    CHECK(gv_table_select(t, 0, 1) == 0);
    CHECK(gv_table_is_selected(t, 0) == 1);
    int selected_id = gv_table_row_id(t, 0);
    CHECK(selected_id > 0);
    CHECK(gv_table_select(t, 1, 1) == 0);
    CHECK(gv_table_is_selected(t, 1) == 1);

    gv_table_sort(t, 0, 1);
    CHECK(gv_table_is_selected_id(t, selected_id));
    gv_table_filter(t, "Member");
    CHECK(gv_table_is_selected_id(t, selected_id));
    gv_table_filter(t, "");

    const char *cells[4] = {"New", "Village", "Active", "42"};
    CHECK(gv_table_insert_row(t, 0, cells, 4) == 0);
    CHECK(t->nrows == 301);
    int found_new = -1;
    for (int r = 0; r < t->view_count; r++) {
        if (strcmp(gv_table_get_cell(t, r, 0), "New") == 0) { found_new = r; break; }
    }
    CHECK(found_new >= 0);
    CHECK(gv_table_delete_row(t, found_new) == 0);
    CHECK(t->nrows == 300);

    CHECK(gv_table_row_at(t, 0) < 0);
    CHECK(gv_table_row_at(t, 28) == 0);
    CHECK(gv_table_visible_rows(t, 480) > 0);

    CHECK(gv_table_focus(t, 1, 1) == 0);
    CHECK(gv_table_focus_row(t) == 1);
    CHECK(gv_table_focus_col(t) == 1);
    CHECK(gv_table_move_focus(t, 1, 1) == 0);
    CHECK(gv_table_focus_row(t) == 2);
    CHECK(gv_table_focus_col(t) == 2);

    CHECK(gv_table_begin_edit(t, 2, 2) == 0);
    CHECK(gv_table_set_edit_text(t, "Validated") == 0);
    CHECK(gv_table_commit_edit(t) == 0);
    CHECK(strcmp(gv_table_get_cell(t, 2, 2), "Validated") == 0);

    char before_readonly[128];
    snprintf(before_readonly, sizeof before_readonly, "%s", gv_table_get_cell(t, 3, 0));
    CHECK(gv_table_set_readonly(t, 3, 0, 1) == 0);
    CHECK(gv_table_set_cell(t, 3, 0, "Nope") == -1);
    CHECK(strcmp(gv_table_get_cell(t, 3, 0), before_readonly) == 0);

    CHECK(gv_table_resize_column(t, 0, 220) == 0);
    CHECK(t->cols[0].width == 220);

    gv_view *v = gv_view_new(640, 400);
    gv_node_id root = gv_view_root(v);
    gv_node_id node = gv_view_add(v, root, GV_NODE_CUSTOM);
    gv_view_set_rect(v, node, 10, 10, 600, 300);
    gv_node_id renderer = gv_view_add(v, root, GV_NODE_CUSTOM);
    gv_view_set_rect(v, renderer, 0, 0, 16, 16);
    gv_display_list dl;
    gv_display_list_init(&dl);
    gv_cmd cmd;
    memset(&cmd, 0, sizeof cmd);
    cmd.op = GV_DRAW_RECT;
    cmd.rect.x = 0; cmd.rect.y = 0; cmd.rect.w = 12; cmd.rect.h = 12;
    cmd.color = (gv_color){70, 180, 110, 255};
    CHECK(gv_display_list_add(&dl, &cmd) == 0);
    gv_view_set_draw_list(v, renderer, &dl);
    gv_display_list_free(&dl);
    CHECK(gv_table_set_cell_renderer(t, 0, 0, renderer) == 0);
    int ops = gv_table_render(v, node, t, (gv_rect){0, 0, 600, 300});
    CHECK(ops > 0);
    CHECK(gv_view_node(v, node)->draw_list.count > 0);

    int nodes_before = gv_view_stats_nodes_created(v);
    int layouts_before = gv_view_stats_layouts(v);
    gv_table_row *row_before = &t->rows[0];
    CHECK(gv_table_set_cell_inplace(t, 0, 1, "Incremental") == 0);
    CHECK(gv_table_render(v, node, t, (gv_rect){0, 0, 600, 300}) > 0);
    CHECK(gv_view_stats_nodes_created(v) == nodes_before);
    CHECK(gv_view_stats_layouts(v) == layouts_before);
    CHECK(&t->rows[0] == row_before);
    CHECK(strcmp(gv_table_get_cell(t, 0, 1), "Incremental") == 0);

    char status[256];
    CHECK(gv_table_status(t, status, sizeof status) == 0);
    printf("status: %s\n", status);
    printf("render ops=%d nodes=%d\n", ops, gv_view_live_nodes(v));

    gv_view_free(v);
    gv_table_free(t);

    for (int i = 0; i < 1000; i++) {
        gv_table *x = gv_table_create(1, 10);
        CHECK(x != NULL);
        gv_table_free(x);
    }

    if (failures) {
        fprintf(stderr, "FAILED: %d check(s)\n", failures);
        return 1;
    }
    printf("PASS: GV-2A table unit tests\n");
    return 0;
}
