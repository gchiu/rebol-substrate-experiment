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
    gv_view *v = gv_view_new(800, 600);
    CHECK(v != NULL);
    int baseline = gv_view_live_nodes(v);

    gv_node_id group = gv_view_add(v, gv_view_root(v), GV_NODE_GROUP);
    gv_view_set_rect(v, group, 10, 10, 300, 200);
    gv_view_set_flags(v, group, 1, 1, 0, 0);
    gv_node_id a = gv_view_add(v, group, GV_NODE_RECT);
    gv_view_set_rect(v, a, 0, 0, 100, 50);
    gv_node_id b = gv_view_add(v, group, GV_NODE_RECT);
    gv_view_set_rect(v, b, 120, 0, 100, 50);
    gv_view_layout(v);

    gv_rect ra = {0}, rb = {0};
    CHECK(gv_view_screen_rect(v, a, &ra) == 0);
    CHECK(gv_view_screen_rect(v, b, &rb) == 0);
    CHECK(ra.x != rb.x);
    CHECK(gv_view_valid(v, a));
    CHECK(gv_view_valid(v, b));

    gv_node_id hit = 0;
    int lx = 0, ly = 0;
    CHECK(gv_view_hit_test(v, ra.x + 10, ra.y + 10, &hit, &lx, &ly) == 1);
    CHECK(hit == a);

    /* Destroy a child; the id becomes invalid and cannot be reused accidentally. */
    gv_view_destroy(v, b);
    CHECK(!gv_view_valid(v, b));
    CHECK(gv_view_node(v, b) == NULL);
    gv_view_destroy(v, b);

    /* Two instances of the same component type are independently addressable. */
    gv_node_id c1 = gv_view_add(v, gv_view_root(v), GV_NODE_GROUP);
    gv_node_id c1a = gv_view_add(v, c1, GV_NODE_RECT);
    gv_view_set_rect(v, c1a, 0, 0, 10, 10);
    gv_node_id c2 = gv_view_add(v, gv_view_root(v), GV_NODE_GROUP);
    gv_node_id c2a = gv_view_add(v, c2, GV_NODE_RECT);
    gv_view_set_rect(v, c2a, 0, 0, 10, 10);
    CHECK(c1 != c2);
    CHECK(c1a != c2a);
    gv_view_destroy(v, c1);
    CHECK(!gv_view_valid(v, c1));
    CHECK(!gv_view_valid(v, c1a));
    CHECK(gv_view_valid(v, c2));
    CHECK(gv_view_valid(v, c2a));
    gv_view_destroy(v, c2);

    /* Repeated create/destroy: capacity is bounded and live nodes return to baseline. */
    int before_loop = gv_view_live_nodes(v);
    for (int i = 0; i < 1000; i++) {
        gv_node_id g = gv_view_add(v, gv_view_root(v), GV_NODE_GROUP);
        CHECK(g != 0);
        gv_node_id x = gv_view_add(v, g, GV_NODE_RECT);
        gv_view_set_rect(v, x, 0, 0, 5, 5);
        gv_view_destroy(v, g);
        CHECK(gv_view_live_nodes(v) == before_loop);
    }
    CHECK(gv_view_node_capacity(v) <= 64);
    printf("address: capacity=%d live=%d\n", gv_view_node_capacity(v),
           gv_view_live_nodes(v));

    /* Stale references and unknown ids are invalid, not recycled aliases. */
    CHECK(!gv_view_valid(v, 9999));
    CHECK(gv_view_node(v, 9999) == NULL);
    CHECK(gv_view_hit_test(v, 0, 0, &hit, &lx, &ly) == 0 || hit != 9999);

    gv_view_free(v);
    if (failures) {
        fprintf(stderr, "FAILED: %d check(s)\n", failures);
        return 1;
    }
    printf("PASS: GV-1A object addressing tests\n");
    return 0;
}