#include "gv_view.h"
#include "gv_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static gv_node_id add_rect(gv_view *v, gv_node_id parent, int x, int y, int w,
                           int h, gv_color c) {
    gv_node_id id = gv_view_add(v, parent, GV_NODE_RECT);
    gv_view_set_rect(v, id, x, y, w, h);
    gv_view_set_colors(v, id, c, (gv_color){255, 255, 255, 255});
    return id;
}

static gv_node_id add_text(gv_view *v, gv_node_id parent, int x, int y,
                           const char *text, double size, gv_color c) {
    gv_node_id id = gv_view_add(v, parent, GV_NODE_TEXT);
    gv_view_set_rect(v, id, x, y, 0, 0);
    gv_view_set_text(v, id, text);
    gv_view_set_font(v, id, size);
    gv_view_set_colors(v, id, (gv_color){0, 0, 0, 0}, c);
    return id;
}

int main(void) {
    gv_view *v = gv_view_new(640, 400);
    if (!v) return 1;
    gv_node_id root = gv_view_root(v);
    gv_view_set_layout(v, root, GV_LAYOUT_V, GV_ALIGN_START, 12, 16);

    gv_color bg = {28, 31, 36, 255};
    gv_color panel = {42, 48, 58, 255};
    gv_color accent = {70, 130, 200, 255};
    gv_color field = {24, 26, 30, 255};
    gv_color text = {235, 238, 242, 255};
    gv_color muted = {160, 168, 178, 255};

    gv_view_set_colors(v, root, bg, text);
    add_text(v, root, 0, 0, "Glon/View GV-1 core", 20.0, text);

    gv_node_id row = gv_view_add(v, root, GV_NODE_GROUP);
    gv_view_set_layout(v, row, GV_LAYOUT_H, GV_ALIGN_START, 12, 0);
    gv_view_set_fill(v, row, 1, 0);

    gv_node_id form = gv_view_add(v, row, GV_NODE_GROUP);
    gv_view_set_layout(v, form, GV_LAYOUT_V, GV_ALIGN_START, 8, 12);
    gv_view_set_fill(v, form, 1, 1);
    gv_view_set_colors(v, form, panel, text);

    add_text(v, form, 0, 0, "Name", 13.0, muted);
    gv_node_id field_box = add_rect(v, form, 0, 0, 100, 28, field);
    gv_view_set_fill(v, field_box, 1, 0);
    add_text(v, form, 0, 0, "Glon operator", 13.0, text);

    gv_node_id controls = gv_view_add(v, row, GV_NODE_GROUP);
    gv_view_set_layout(v, controls, GV_LAYOUT_V, GV_ALIGN_START, 8, 12);
    gv_view_set_rect(v, controls, 0, 0, 220, 0);
    gv_view_set_colors(v, controls, panel, text);

    gv_node_id button = add_rect(v, controls, 0, 0, 100, 30, accent);
    gv_view_set_fill(v, button, 1, 0);
    add_text(v, controls, 0, 0, "Run", 13.0, text);

    gv_node_id check = add_rect(v, controls, 0, 0, 18, 18, accent);
    add_text(v, controls, 26, 0, "Enabled", 13.0, text);

    gv_node_id status = add_text(v, controls, 0, 0, "Ready", 13.0, muted);
    gv_view_set_rect(v, status, 0, 0, 0, 0);
    (void)check;

    gv_view_layout(v);

    gv_display_list dl;
    gv_display_list_init(&dl);
    gv_view_build_display_list(v, &dl);

    gv_backend_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.width = 640;
    cfg.height = 400;
    cfg.title = "Glon/View GV-1 core";
    cfg.headless = 1;

    gv_backend *b = gv_backend_sdl();
    if (!b || b->init(b, &cfg) != 0 || b->create_window(b, 640, 400, cfg.title) != 0) {
        fprintf(stderr, "SDL/Cairo unavailable\n");
        gv_view_free(v);
        return 1;
    }
    int rc = 1;
    if (b->begin_frame(b) == 0 && b->draw(b, &dl) == 0 && b->end_frame(b) == 0) {
        rc = gv_backend_sdl_save_png(b, "evidence/gv1-view.png");
    }
    printf("nodes_created=%d layouts=%d draw_ops=%d live=%d\n",
           gv_view_stats_nodes_created(v), gv_view_stats_layouts(v),
           gv_view_stats_draw_ops(v), gv_view_live_nodes(v));
    gv_display_list_free(&dl);
    b->destroy(b);
    gv_view_free(v);
    if (rc != 0) {
        fprintf(stderr, "failed to write evidence/gv1-view.png\n");
        return 1;
    }
    printf("wrote evidence/gv1-view.png\n");
    return 0;
}