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

static int word_int(gv_glon_host *h, const char *word) {
    char buf[128];
    if (gv_glon_host_text(h, word, buf, sizeof buf) != 0) return -1;
    return atoi(buf);
}

static int word_has(gv_glon_host *h, const char *word, const char *needle) {
    char buf[512];
    if (gv_glon_host_text(h, word, buf, sizeof buf) != 0) return 0;
    return strstr(buf, needle) != NULL;
}

int main(void) {
    gv_view *v = gv_view_new(640, 400);
    CHECK(v != NULL);
    if (!v) return 1;
    int baseline = gv_view_live_nodes(v);

    gv_glon_host *h = gv_glon_host_new(v, NULL);
    CHECK(h != NULL);
    if (!h) {
        gv_view_free(v);
        return 1;
    }
    CHECK(gv_glon_host_load(h, "..") == 0);
    if (gv_glon_host_error(h)) {
        fprintf(stderr, "glon host load failed\n");
        return 1;
    }
    CHECK(gv_glon_host_build(h) == 0);
    CHECK(gv_glon_host_bind(h) == 0);
    CHECK(!gv_glon_host_error(h));
    int built = gv_view_live_nodes(v);
    CHECK(built > baseline + 5);
    printf("glon build: live=%d\n", built);

    int button = word_int(h, "v/button");
    int check = word_int(h, "v/check");
    int field = word_int(h, "v/focus");
    int scene_button = word_int(h, "v/scene-button");
    int canvas = word_int(h, "v/canvas");
    CHECK(button > 1);
    CHECK(check > 1);
    CHECK(field > 1);
    CHECK(scene_button > 1);
    CHECK(canvas > 1);

    CHECK(word_has(h, "v/status", "Ready"));

    gv_node_id canvas_view = gv_glon_host_view_node(h, canvas);
    gv_node *cn = gv_view_node(v, canvas_view);
    CHECK(cn != NULL);
    CHECK(cn && cn->type == GV_NODE_CUSTOM);
    CHECK(cn && cn->draw_list.count > 0);

    gv_node_id sv = gv_glon_host_view_node(h, scene_button);
    CHECK(sv > 0);
    gv_rect sr = {0, 0, 0, 0};
    CHECK(gv_view_screen_rect(v, sv, &sr) == 0);
    gv_node_id hit = 0;
    int lx = 0, ly = 0;
    CHECK(gv_view_hit_test(v, sr.x + sr.w / 2, sr.y + sr.h / 2, &hit, &lx, &ly) == 1);
    CHECK(gv_glon_host_glon_id(h, hit) == scene_button);

    CHECK(gv_glon_host_click(h, button) == 0);
    CHECK(word_has(h, "v/status", "Run pressed"));

    CHECK(gv_glon_host_char(h, "A") == 0);
    CHECK(word_has(h, "v/field-text", "A"));
    CHECK(gv_glon_host_char(h, "B") == 0);
    CHECK(word_has(h, "v/field-text", "AB"));
    CHECK(gv_glon_host_backspace(h) == 0);
    CHECK(!word_has(h, "v/field-text", "AB"));
    CHECK(word_has(h, "v/field-text", "A"));

    CHECK(gv_glon_host_click(h, check) == 0);
    CHECK(word_has(h, "v/status", "Enabled"));

    CHECK(gv_glon_host_click(h, scene_button) == 0);
    CHECK(word_has(h, "v/status", "View button pressed"));

    gv_table *table = NULL;
    CHECK(gv_glon_host_table(h, 19, &table) == 0);
    CHECK(table != NULL);
    if (table) {
        CHECK(table->nrows == 300);
        CHECK(table->ncols == 4);
        gv_node_id tv = gv_glon_host_view_node(h, 19);
        gv_node *tn = gv_view_node(v, tv);
        CHECK(tn && tn->type == GV_NODE_CUSTOM);
        CHECK(tn && tn->draw_list.count > 0);
        CHECK(gv_glon_host_table_command(h, 19, 2, 0, 0, "Lapsed") == 0);
        CHECK(table->view_count > 0 && table->view_count < 300);
        CHECK(gv_glon_host_table_command(h, 19, 3, 0, 1, NULL) == 0);
        CHECK(gv_table_is_selected(table, 0));
        char status[256];
        CHECK(gv_glon_host_table_status(h, 19, status, sizeof status) == 0);
        CHECK(strstr(status, "rows=") != NULL);
        int before_dir = table->sort_dir;
        CHECK(gv_glon_host_tick(h) == 0);
        CHECK(word_has(h, "v/status", "Tick"));
        CHECK(table->sort_dir != before_dir);
    }

    CHECK(gv_glon_host_click(h, 9999) == 0);
    CHECK(!gv_glon_host_error(h));

    CHECK(gv_glon_host_run(h, "[ demo/build") == -1);
    CHECK(gv_glon_host_error(h));

    gv_glon_host_free(h);
    gv_view_free(v);
    if (failures) {
        fprintf(stderr, "FAILED: %d check(s)\n", failures);
        return 1;
    }
    printf("PASS: GV-1 Glon/VID integration tests\n");
    return 0;
}