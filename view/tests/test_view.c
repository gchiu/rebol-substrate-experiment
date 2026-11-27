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

static gv_node_id add_rect(gv_view *v, gv_node_id parent, int x, int y, int w,
                           int h, gv_color c) {
    gv_node_id id = gv_view_add(v, parent, GV_NODE_RECT);
    gv_view_set_rect(v, id, x, y, w, h);
    gv_view_set_colors(v, id, c, (gv_color){255, 255, 255, 255});
    return id;
}

static void test_horizontal_layout(void) {
    gv_view *v = gv_view_new(800, 600);
    CHECK(v != NULL);
    gv_node_id root = gv_view_root(v);
    gv_view_set_layout(v, root, GV_LAYOUT_H, GV_ALIGN_START, 8, 10);

    gv_node_id a = add_rect(v, root, 0, 0, 100, 40, (gv_color){200, 50, 50, 255});
    gv_node_id b = add_rect(v, root, 0, 0, 50, 40, (gv_color){50, 200, 50, 255});
    gv_view_set_fill(v, b, 1, 0);
    gv_node_id t = gv_view_add(v, root, GV_NODE_TEXT);
    gv_view_set_text(v, t, "Hello");
    gv_view_set_font(v, t, 12.0);

    gv_view_layout(v);

    gv_rect ra = {0, 0, 0, 0}, rb = {0, 0, 0, 0}, rt = {0, 0, 0, 0};
    CHECK(gv_view_screen_rect(v, a, &ra) == 0);
    CHECK(gv_view_screen_rect(v, b, &rb) == 0);
    CHECK(gv_view_screen_rect(v, t, &rt) == 0);
    CHECK(ra.x == 10 && ra.y == 10 && ra.w == 100 && ra.h == 40);
    CHECK(rb.x == 118 && rb.y == 10 && rb.w == 581 && rb.h == 40);
    CHECK(rt.x == 707 && rt.w == 33);
    printf("h-layout: A=%d,%d %dx%d B=%d,%d %dx%d T=%d,%d %dx%d\n",
           ra.x, ra.y, ra.w, ra.h, rb.x, rb.y, rb.w, rb.h,
           rt.x, rt.y, rt.w, rt.h);
    gv_view_free(v);
}

static void test_vertical_layout(void) {
    gv_view *v = gv_view_new(800, 600);
    gv_node_id root = gv_view_root(v);
    gv_view_set_layout(v, root, GV_LAYOUT_V, GV_ALIGN_START, 5, 10);

    gv_node_id a = add_rect(v, root, 0, 0, 200, 30, (gv_color){200, 50, 50, 255});
    gv_node_id b = add_rect(v, root, 0, 0, 100, 20, (gv_color){50, 200, 50, 255});
    gv_view_set_fill(v, b, 0, 1);
    gv_node_id c = add_rect(v, root, 0, 0, 50, 20, (gv_color){50, 50, 200, 255});

    gv_view_layout(v);
    gv_rect ra = {0}, rb = {0}, rc = {0};
    CHECK(gv_view_screen_rect(v, a, &ra) == 0);
    CHECK(gv_view_screen_rect(v, b, &rb) == 0);
    CHECK(gv_view_screen_rect(v, c, &rc) == 0);
    CHECK(ra.y == 10 && ra.h == 30);
    CHECK(rb.y == 45 && rb.h == 500);
    CHECK(rc.y == 550 && rc.h == 20);
    gv_view_free(v);
}

static void test_transform_hit(void) {
    gv_view *v = gv_view_new(800, 600);
    gv_node_id root = gv_view_root(v);
    gv_view_set_layout(v, root, GV_LAYOUT_NONE, GV_ALIGN_START, 0, 0);
    gv_node_id r = add_rect(v, root, 100, 100, 100, 50,
                            (gv_color){80, 120, 200, 255});
    gv_view_set_transform(v, r, (gv_transform){1, 0, 0, 1, 50, 25});
    gv_view_set_transform(v, r, (gv_transform){2, 0, 0, 2, 50, 25});
    gv_view_layout(v);

    gv_rect rr = {0};
    CHECK(gv_view_screen_rect(v, r, &rr) == 0);
    CHECK(rr.x == 150 && rr.y == 125 && rr.w == 200 && rr.h == 100);

    gv_node_id hit = 0;
    int lx = 0, ly = 0;
    CHECK(gv_view_hit_test(v, 200, 150, &hit, &lx, &ly) == 1);
    CHECK(hit == r);
    CHECK(lx == 25 && ly == 13);
    CHECK(gv_view_hit_test(v, 140, 120, &hit, &lx, &ly) == 0);
    gv_view_free(v);
}

static void test_rotation_hit(void) {
    gv_view *v = gv_view_new(800, 600);
    gv_node_id root = gv_view_root(v);
    gv_view_set_layout(v, root, GV_LAYOUT_NONE, GV_ALIGN_START, 0, 0);
    gv_node_id r = add_rect(v, root, 200, 200, 100, 100,
                            (gv_color){200, 120, 40, 255});
    gv_view_set_transform(v, r, (gv_transform){0, 1, -1, 0, 0, 0});
    gv_view_layout(v);

    gv_rect rr = {0};
    CHECK(gv_view_screen_rect(v, r, &rr) == 0);
    CHECK(rr.x == 100 && rr.y == 200 && rr.w == 100 && rr.h == 100);

    gv_node_id hit = 0;
    int lx = 0, ly = 0;
    CHECK(gv_view_hit_test(v, 150, 250, &hit, &lx, &ly) == 1);
    CHECK(hit == r);
    CHECK(lx == 50 && ly == 50);
    CHECK(gv_view_hit_test(v, 190, 190, &hit, &lx, &ly) == 0);
    gv_view_free(v);
}

static void test_clip(void) {
    gv_view *v = gv_view_new(800, 600);
    gv_node_id root = gv_view_root(v);
    gv_view_set_layout(v, root, GV_LAYOUT_NONE, GV_ALIGN_START, 0, 0);
    gv_node_id clip = gv_view_add(v, root, GV_NODE_CLIP);
    gv_view_set_rect(v, clip, 100, 100, 100, 100);
    gv_view_set_flags(v, clip, 1, 1, 0, 0);
    gv_node_id child = add_rect(v, clip, 0, 0, 200, 200,
                                (gv_color){40, 180, 90, 255});
    gv_view_layout(v);

    gv_node_id hit = 0;
    int lx = 0, ly = 0;
    CHECK(gv_view_hit_test(v, 150, 150, &hit, &lx, &ly) == 1);
    CHECK(hit == child);
    CHECK(lx == 50 && ly == 50);
    CHECK(gv_view_hit_test(v, 250, 250, &hit, &lx, &ly) == 0);
    gv_view_free(v);
}

static void test_focus_tab(void) {
    gv_view *v = gv_view_new(800, 600);
    gv_node_id root = gv_view_root(v);
    gv_view_set_layout(v, root, GV_LAYOUT_H, GV_ALIGN_START, 10, 10);
    gv_node_id a = add_rect(v, root, 0, 0, 60, 30, (gv_color){90, 90, 90, 255});
    gv_node_id b = add_rect(v, root, 0, 0, 60, 30, (gv_color){90, 90, 90, 255});
    gv_node_id c = add_rect(v, root, 0, 0, 60, 30, (gv_color){90, 90, 90, 255});
    gv_view_set_flags(v, a, 1, 1, 1, 1);
    gv_view_set_flags(v, b, 1, 1, 1, 1);
    gv_view_set_flags(v, c, 1, 1, 1, 1);
    gv_view_layout(v);

    CHECK(gv_view_focus(v, a) == 0);
    CHECK(gv_view_focus_id(v) == a);
    CHECK(gv_view_focus_next(v, 0) == b);
    CHECK(gv_view_focus_next(v, 0) == c);
    CHECK(gv_view_focus_next(v, 0) == a);
    CHECK(gv_view_focus_next(v, 1) == c);
    gv_view_free(v);
}

static void test_invalid_refs(void) {
    gv_view *v = gv_view_new(400, 300);
    CHECK(gv_view_add(v, 9999, GV_NODE_RECT) == 0);
    gv_view_destroy(v, 9999);
    gv_view_set_text(v, 9999, "x");
    gv_view_set_rect(v, 9999, 0, 0, 1, 1);
    CHECK(gv_view_focus(v, 9999) == -1);
    gv_node_id id = 0;
    int lx = 0, ly = 0;
    gv_view_hit_test(v, 10, 10, &id, &lx, &ly);
    CHECK(gv_view_add(v, 0, GV_NODE_RECT) == 0);
    CHECK(gv_view_add(v, 0, GV_NODE_WINDOW) == 0);

    gv_node_id p = gv_view_add(v, gv_view_root(v), GV_NODE_GROUP);
    CHECK(p != 0);
    CHECK(gv_view_set_parent(v, p, p) == -1);

    gv_view_destroy(v, gv_view_root(v));
    CHECK(gv_view_root(v) == 0);
    gv_view_free(v);
}

static void test_destroy_parent(void) {
    gv_view *v = gv_view_new(400, 300);
    gv_node_id root = gv_view_root(v);
    gv_node_id group = gv_view_add(v, root, GV_NODE_GROUP);
    add_rect(v, group, 0, 0, 10, 10, (gv_color){1, 2, 3, 255});
    add_rect(v, group, 0, 0, 10, 10, (gv_color){1, 2, 3, 255});
    CHECK(gv_view_live_nodes(v) == 4);
    gv_view_destroy(v, group);
    CHECK(gv_view_live_nodes(v) == 1);
    CHECK(!gv_view_valid(v, group));
    gv_view_free(v);
}

static void test_repeat_create_destroy(void) {
    gv_view *v = gv_view_new(400, 300);
    gv_node_id root = gv_view_root(v);
    int baseline = gv_view_live_nodes(v);
    for (int i = 0; i < 1000; i++) {
        gv_node_id g = gv_view_add(v, root, GV_NODE_GROUP);
        CHECK(g != 0);
        for (int j = 0; j < 3; j++) {
            gv_node_id r = add_rect(v, g, 0, 0, 10, 10,
                                    (gv_color){10, 20, 30, 255});
            CHECK(r != 0);
        }
        gv_view_destroy(v, g);
        CHECK(gv_view_live_nodes(v) == baseline);
    }
    CHECK(gv_view_node_capacity(v) <= 64);
    printf("repeat: capacity=%d live=%d\n", gv_view_node_capacity(v),
           gv_view_live_nodes(v));
    gv_view_free(v);
}

static void test_display_list(void) {
    gv_view *v = gv_view_new(400, 300);
    gv_node_id root = gv_view_root(v);
    gv_view_set_layout(v, root, GV_LAYOUT_NONE, GV_ALIGN_START, 0, 0);
    gv_node_id r = add_rect(v, root, 10, 10, 100, 50,
                            (gv_color){200, 20, 20, 255});
    gv_view_layout(v);

    gv_display_list dl;
    gv_display_list_init(&dl);
    gv_view_build_display_list(v, &dl);
    int visible_ops = dl.count;
    CHECK(visible_ops > 0);
    gv_display_list_free(&dl);

    gv_view_set_flags(v, r, 0, 1, 0, 1);
    gv_view_layout(v);
    gv_display_list_init(&dl);
    gv_view_build_display_list(v, &dl);
    CHECK(dl.count < visible_ops);
    gv_display_list_free(&dl);

    gv_node_id custom = gv_view_add(v, root, GV_NODE_CUSTOM);
    gv_view_set_rect(v, custom, 0, 0, 20, 20);
    gv_display_list local;
    gv_display_list_init(&local);
    gv_cmd c;
    memset(&c, 0, sizeof c);
    c.op = GV_DRAW_RECT;
    c.rect.x = 0;
    c.rect.y = 0;
    c.rect.w = 20;
    c.rect.h = 20;
    c.color = (gv_color){10, 200, 10, 255};
    gv_display_list_add(&local, &c);
    gv_view_set_draw_list(v, custom, &local);
    gv_display_list_free(&local);
    gv_display_list_init(&dl);
    gv_view_build_display_list(v, &dl);
    CHECK(dl.count > 0);
    gv_display_list_free(&dl);

    gv_view_free(v);
}

static void test_viewport(void) {
    gv_view *v = gv_view_new(800, 600);
    gv_node_id root = gv_view_root(v);
    gv_view_set_layout(v, root, GV_LAYOUT_NONE, GV_ALIGN_START, 0, 0);
    gv_node_id vp = gv_view_add(v, root, GV_NODE_VIEWPORT);
    gv_view_set_rect(v, vp, 100, 100, 200, 200);
    gv_view_set_flags(v, vp, 1, 1, 0, 0);
    gv_node_id child = add_rect(v, vp, 0, 0, 50, 50,
                                (gv_color){30, 120, 220, 255});
    gv_view_set_viewport(v, vp, 10, 20, 2.0);
    gv_view_layout(v);

    gv_rect vr = {0}, cr = {0};
    CHECK(gv_view_screen_rect(v, vp, &vr) == 0);
    CHECK(vr.x == 100 && vr.y == 100 && vr.w == 200 && vr.h == 200);
    CHECK(gv_view_screen_rect(v, child, &cr) == 0);
    CHECK(cr.x == 80 && cr.y == 60 && cr.w == 100 && cr.h == 100);

    gv_node_id hit = 0;
    int lx = 0, ly = 0;
    CHECK(gv_view_hit_test(v, 120, 100, &hit, &lx, &ly) == 1);
    CHECK(hit == child);
    CHECK(lx == 20 && ly == 20);
    CHECK(gv_view_hit_test(v, 300, 300, &hit, &lx, &ly) == 0);
    gv_view_free(v);
}

static int g_count = 0;

static int handler_stop(gv_view *v, gv_node_id id, const gv_event *ev, int lx,
                        int ly, void *user) {
    (void)v; (void)id; (void)ev; (void)lx; (void)ly; (void)user;
    g_count++;
    return 0;
}

static int handler_pass(gv_view *v, gv_node_id id, const gv_event *ev, int lx,
                        int ly, void *user) {
    (void)v; (void)id; (void)ev; (void)lx; (void)ly; (void)user;
    g_count++;
    return 1;
}

static void test_dispatch(void) {
    gv_view *v = gv_view_new(400, 300);
    gv_node_id root = gv_view_root(v);
    gv_view_set_layout(v, root, GV_LAYOUT_NONE, GV_ALIGN_START, 0, 0);
    gv_node_id r = add_rect(v, root, 0, 0, 60, 30,
                            (gv_color){100, 100, 100, 255});
    gv_view_set_flags(v, r, 1, 1, 1, 1);
    gv_view_set_event(v, r, 7);
    gv_view_layout(v);

    gv_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = GV_EVENT_POINTER_DOWN;
    ev.x = 10;
    ev.y = 10;
    g_count = 0;
    CHECK(gv_view_dispatch(v, &ev, handler_stop, NULL) == 1);
    CHECK(g_count == 1);

    g_count = 0;
    CHECK(gv_view_dispatch(v, &ev, handler_pass, NULL) == 1);
    CHECK(g_count == 2);

    CHECK(gv_view_focus(v, r) == 0);
    memset(&ev, 0, sizeof ev);
    ev.type = GV_EVENT_KEY_DOWN;
    ev.key = 65;
    g_count = 0;
    CHECK(gv_view_dispatch(v, &ev, handler_stop, NULL) == 1);
    CHECK(g_count == 1);
    gv_view_free(v);
}

static void test_resize(void) {
    gv_view *v = gv_view_new(800, 600);
    gv_node_id root = gv_view_root(v);
    gv_view_set_layout(v, root, GV_LAYOUT_H, GV_ALIGN_START, 8, 10);
    (void)add_rect(v, root, 0, 0, 100, 40, (gv_color){1, 2, 3, 255});
    gv_node_id b = add_rect(v, root, 0, 0, 50, 40, (gv_color){1, 2, 3, 255});
    gv_view_set_fill(v, b, 1, 0);
    gv_view_layout(v);
    gv_rect rb = {0};
    CHECK(gv_view_screen_rect(v, b, &rb) == 0);
    CHECK(rb.w == 622);
    gv_view_resize(v, 400, 300);
    gv_view_layout(v);
    CHECK(gv_view_screen_rect(v, b, &rb) == 0);
    CHECK(rb.w == 222);
    gv_view_free(v);
}

int main(void) {
    test_horizontal_layout();
    test_vertical_layout();
    test_transform_hit();
    test_rotation_hit();
    test_clip();
    test_focus_tab();
    test_invalid_refs();
    test_destroy_parent();
    test_repeat_create_destroy();
    test_display_list();
    test_viewport();
    test_dispatch();
    test_resize();
    if (failures) {
        fprintf(stderr, "FAILED: %d check(s)\n", failures);
        return 1;
    }
    printf("PASS: GV-1 View core tests\n");
    return 0;
}