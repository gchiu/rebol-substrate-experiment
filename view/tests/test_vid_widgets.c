#define _POSIX_C_SOURCE 200809L
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

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : "..";

    gv_view *v = gv_view_new(1000, 650);
    CHECK(v != NULL);
    int baseline = gv_view_live_nodes(v);
    gv_glon_host *h = gv_glon_host_new(v, NULL);
    CHECK(h != NULL);
    CHECK(gv_glon_host_load_vid_app(h, root, "view/glon/membership.glon") == 0);
    CHECK(!gv_glon_host_error(h));
    CHECK(gv_glon_host_build_app(h, NULL) == 0);
    CHECK(!gv_glon_host_error(h));
    CHECK(gv_view_live_nodes(v) > baseline + 10);

    int table_id = word_int(h, "v/table-id");
    CHECK(table_id > 1);
    gv_table *t = NULL;
    CHECK(gv_glon_host_table(h, table_id, &t) == 0);
    CHECK(t != NULL);
    if (t) {
        CHECK(t->nrows == 5);
        CHECK(t->ncols == 8);
        CHECK(strcmp(gv_table_get_cell(t, 0, 0), "Alice") == 0);
        CHECK(strcmp(gv_table_get_cell(t, 0, 1), "阿明") == 0);
        CHECK(strcmp(gv_table_get_cell(t, 1, 1), "小波") == 0);
    }

    gv_glon_host_free(h);
    gv_view_free(v);

    v = gv_view_new(1000, 650);
    baseline = gv_view_live_nodes(v);
    h = gv_glon_host_new(v, NULL);
    CHECK(gv_glon_host_load_vid_app(h, root, "view/glon/examples/example4.glon") == 0);
    CHECK(!gv_glon_host_error(h));
    CHECK(gv_glon_host_build_app(h, NULL) == 0);
    CHECK(!gv_glon_host_error(h));
    CHECK(gv_view_live_nodes(v) > baseline + 5);

    CHECK(gv_glon_host_char(h, "Alice") == 0);
    CHECK(word_has(h, "v/name-text", "Alice"));
    int clear_node = word_int(h, "v/clear-node");
    CHECK(clear_node > 1);
    CHECK(gv_glon_host_click(h, clear_node) == 0);
    CHECK(word_has(h, "v/name-text", ""));

    gv_glon_host_free(h);
    gv_view_free(v);

    if (failures) {
        fprintf(stderr, "FAILED: %d check(s)\n", failures);
        return 1;
    }
    printf("PASS: GV-2 VID widget and membership integration tests\n");
    return 0;
}
