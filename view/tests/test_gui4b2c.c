/* test_gui4b2c.c -- GUI-4B2C: scrolling, clipping and widget lifecycle.
 *
 * Input is driven through the interactive runner's own path: SDL events are
 * pushed into the SDL backend (dummy video driver), read back with
 * poll_event and routed with gv_glon_host_dispatch, exactly as glon_demo
 * does.  Geometry checks use the View (screen / visible rectangles, hit
 * tests), not screenshots.
 *
 * Usage: test_gui4b2c ROOT CASE
 *   wheel nested clip activate edit gc collide rebuild stale resize
 *   present perf */
#include "gv_glon_host.h"
#include "gv_view.h"
#include "gv_backend.h"
#include "r0_s1.h"
#include <SDL.h>
#include <cairo.h>
#include <time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static gv_view *v;
static gv_backend *b;
static gv_glon_host *h;

#define K_TAB 9
#define K_PGUP 1073741899
#define K_PGDN 1073741902

static long gint(const char *expr) {
    char buf[96] = "";
    if (gv_glon_host_text(h, expr, buf, sizeof buf) != 0) return -999999;
    return atol(buf);
}
static void gstr(const char *expr, char *out, int cap) {
    out[0] = 0;
    gv_glon_host_text(h, expr, out, cap);
}
/* run Glon source "[ ... ]" and deliver the View commands it queued */
static int run(const char *src) {
    char buf[1024];
    size_t n = strlen(src);
    if (n < 2 || n + 32 > sizeof buf || src[n - 1] != ']') return gv_glon_host_run(h, src);
    snprintf(buf, sizeof buf, "%.*s widget/flush ]", (int)(n - 1), src);
    if (gv_glon_host_run(h, buf) != 0 || gv_glon_host_deliver(h) != 0) return -1;
    return gv_glon_host_bind(h);       /* answers queries, as after an event */
}
static int node_of(const char *key) {
    char e[96]; snprintf(e, sizeof e, "gui3/find-node-by-key '%s", key); return (int)gint(e);
}
static int left(void)  { return (int)gint("block-at gui3/app 1"); }
static int right(void) { return (int)gint("block-at gui3/app 2"); }
static int inner(void) { return (int)gint("block-at gui3/app 3"); }
/* viewport node of scroll region which (1 left, 2 right, 3 inner) */
static int vp_of(int which) {
    char e[96]; snprintf(e, sizeof e, "block-at gui3/scroll-record block-at gui3/app %d 1", which); return (int)gint(e);
}
static int off(int which, int k) {
    char e[96]; snprintf(e, sizeof e, "t/off %d %d", which, k); return (int)gint(e);
}
static void set_off(int which, int y) {
    char e[128]; snprintf(e, sizeof e, "[ gui3/scroll-set block-at gui3/app %d 0 %d ]", which, y);
    CHECK(run(e) == 0);
}
static void last_key(char *out, int cap) { gstr("block-at gui3/app 5", out, cap); }
static int hits(void) { return (int)gint("block-at gui3/app 6"); }

/* screen / visible rectangle of a Glon node */
static int rect_of(int gid, gv_rect *r, int visible) {
    gv_node_id n = gv_glon_host_view_node(h, gid);
    if (!n) return -1;
    if (v->layout_dirty) gv_view_layout(v);
    return visible ? gv_view_visible_rect(v, n, r) : gv_view_screen_rect(v, n, r);
}

/* ---- runner input: push SDL events, poll them back, dispatch ---------- */
static int pump(void) {
    gv_event ev;
    int rc = 0, got;
    while ((got = b->poll_event(b, &ev)) > 0)
        if (gv_glon_host_dispatch(h, &ev) != 0) rc = -1;
    return rc;
}
static int sdl_wheel(int x, int y, int sdl_dy) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = SDL_MOUSEWHEEL;
    e.wheel.y = sdl_dy;
    e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    SDL_PushEvent(&e);
    /* SDL 2.0.20 has no wheel position; the backend asks SDL_GetMouseState,
     * which pushed events do not move, so the position is set here */
    gv_event ev;
    int rc = 0;
    while (b->poll_event(b, &ev) > 0) {
        if (ev.type == GV_EVENT_WHEEL) { ev.x = x; ev.y = y; }
        if (gv_glon_host_dispatch(h, &ev) != 0) rc = -1;
    }
    return rc;
}
static int sdl_click(int x, int y) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT;
    e.button.x = x; e.button.y = y; e.button.state = SDL_PRESSED;
    SDL_PushEvent(&e);
    e.type = SDL_MOUSEBUTTONUP; e.button.state = SDL_RELEASED;
    SDL_PushEvent(&e);
    return pump();
}
static int sdl_key(int sym) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = SDL_KEYDOWN; e.key.keysym.sym = sym; e.key.state = SDL_PRESSED;
    SDL_PushEvent(&e);
    return pump();
}
static int sdl_text(const char *s) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = SDL_TEXTINPUT;
    snprintf(e.text.text, sizeof e.text.text, "%s", s);
    SDL_PushEvent(&e);
    return pump();
}
static void centre(const gv_rect *r, int *x, int *y) { *x = r->x + r->w / 2; *y = r->y + r->h / 2; }

static int setup(const char *root) {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    v = gv_view_new(700, 480);
    b = gv_backend_sdl();
    gv_backend_config cfg; memset(&cfg, 0, sizeof cfg);
    cfg.width = 700; cfg.height = 480;
    if (!v || !b || b->init(b, &cfg) != 0 || b->create_window(b, 700, 480, "b2c") != 0) return -1;
    h = gv_glon_host_new(v, b);
    if (!h || gv_glon_host_load_gui3_app(h, root, "view/tests/fixtures/gui4b2c_app.glon") != 0) return -1;
    if (gv_glon_host_build_app(h, NULL) != 0 || gv_glon_host_bind(h) != 0 || gv_glon_host_error(h)) return -1;
    gv_view_layout(v);
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    return 0;
}

/* 1: positive and negative deltas, physical wheel direction */
static void case_wheel(void) {
    gv_rect r; int x, y;
    CHECK(rect_of(vp_of(1), &r, 1) == 0);
    x = r.x + r.w - 4; y = r.y + 12;      /* right of the nested region */
    set_off(1, 100);
    CHECK(gv_glon_host_wheel(h, x, y, 0, -1) == 0);
    CHECK(off(1, 1) == 76);                       /* negative delta */
    CHECK(gv_glon_host_wheel(h, x, y, 0, 2) == 0);
    CHECK(off(1, 1) == 124);                      /* positive delta */
    CHECK(sdl_wheel(x, y, 1) == 0);               /* physical wheel up */
    CHECK(off(1, 1) == 100);
    CHECK(sdl_wheel(x, y, -1) == 0);              /* physical wheel down */
    CHECK(off(1, 1) == 124);
    set_off(1, 10);
    CHECK(sdl_wheel(x, y, 1) == 0);               /* clamps at the start */
    CHECK(off(1, 1) == 0);
    CHECK(off(2, 1) == 0);                        /* other region untouched */
    printf("wheel: left %d after clamp; right %d\n", off(1, 1), off(2, 1));
}

/* 2: nested boundary propagation in both directions */
static void case_nested(void) {
    gv_rect r; int x, y;
    CHECK(rect_of(inner(), &r, 1) == 0);
    centre(&r, &x, &y);
    int inner_max = off(3, 3);
    CHECK(inner_max > 0);
    set_off(3, 24);
    CHECK(gv_glon_host_wheel(h, x, y, 0, 1) == 0);
    CHECK(off(3, 1) == 48 && off(1, 1) == 0);      /* inner consumes */
    set_off(3, inner_max);
    CHECK(gv_glon_host_wheel(h, x, y, 0, 1) == 0);
    CHECK(off(3, 1) == inner_max && off(1, 1) == 24); /* lower boundary -> outer */
    set_off(3, 0);
    CHECK(rect_of(inner(), &r, 1) == 0);
    centre(&r, &x, &y);
    CHECK(gv_glon_host_wheel(h, x, y, 0, -1) == 0);
    CHECK(off(3, 1) == 0 && off(1, 1) == 0);       /* upper boundary -> outer */
    set_off(1, 0);
    CHECK(gv_glon_host_wheel(h, x, y, 0, -1) == 0);
    CHECK(off(3, 1) == 0 && off(1, 1) == 0);       /* both at the start: no-op */
    /* keyboard: PageDown / PageUp on the focused outer region */
    CHECK(run("[ gui3/focus-set block-at gui3/app 1 ]") == 0);
    CHECK(sdl_key(K_PGDN) == 0);
    int vh = off(1, 1);
    CHECK(vh > 0);
    CHECK(sdl_key(K_PGUP) == 0);
    CHECK(off(1, 1) == 0);
    printf("nested: inner max %d; page step %d\n", inner_max, vh);
}

/* 3: nothing clipped or scrolled out receives pointer or wheel input */
static void case_clip(void) {
    gv_rect lr, ir, br;
    CHECK(rect_of(left(), &lr, 1) == 0);
    int f2 = node_of("f2");
    CHECK(f2 > 0);
    /* field f2 lies below the left viewport: its screen rectangle is inside
     * the window but it is not visible */
    CHECK(rect_of(f2, &br, 0) == 0);
    CHECK(rect_of(f2, &br, 1) != 0);
    int x = br.x + br.w / 2, y = br.y + br.h / 2;
    int gid = -1;
    CHECK(y > lr.y + lr.h && y < 480);
    CHECK(gv_glon_host_pointer_at(h, 1, x, y, &gid) == 0);
    CHECK(gid != f2);
    CHECK(gv_glon_host_pointer_at(h, 2, x, y, NULL) == 0);
    CHECK(gint("gui3/current-node") != f2);
    CHECK(hits() == 0);
    /* scroll the inner region partly above the left viewport: points above
     * the viewport's top edge are inside the inner viewport's screen
     * rectangle but must not reach it */
    set_off(1, 200);
    CHECK(rect_of(inner(), &ir, 0) == 0);
    CHECK(ir.y < lr.y);
    int wy = lr.y - 4, wx = ir.x + 20;
    if (wy > ir.y) {
        int i0 = off(3, 1), l0 = off(1, 1);
        CHECK(gv_glon_host_wheel(h, wx, wy, 0, 1) == 0);
        CHECK(off(3, 1) == i0 && off(1, 1) == l0);
        CHECK(gv_glon_host_pointer_at(h, 1, wx, wy, &gid) == 0);
        CHECK(gid != node_of("inner-hit"));
    }
    /* the visible part of the inner viewport is clipped to the outer one */
    gv_rect iv;
    CHECK(rect_of(inner(), &iv, 1) == 0);
    CHECK(iv.y >= lr.y && iv.y + iv.h <= lr.y + lr.h);
    /* a point on the left region but not on any control activates nothing */
    CHECK(gv_glon_host_pointer_at(h, 1, lr.x + lr.w - 20, lr.y + 2, &gid) == 0);
    CHECK(gv_glon_host_pointer_at(h, 2, lr.x + lr.w - 20, lr.y + 2, NULL) == 0);
    CHECK(hits() == 0);
    printf("clip: inner screen y %d, left viewport y %d, inner visible %d+%d\n", ir.y, lr.y, iv.y, iv.h);
}

/* 4: visible scrolled controls activate exactly where they are drawn */
static void case_activate(void) {
    char k[64];
    int lefthit = node_of("left-hit"), righthit = node_of("right-hit");
    gv_rect lr, br; int x, y;
    CHECK(rect_of(vp_of(1), &lr, 1) == 0);
    x = lr.x + lr.w - 4; y = lr.y + 12;                                 /* clear of the nested region */
    for (int i = 0; i < 20; i++) CHECK(sdl_wheel(x, y, -1) == 0);   /* to the end */
    CHECK(off(1, 1) == off(1, 3));
    CHECK(rect_of(lefthit, &br, 1) == 0);
    centre(&br, &x, &y);
    int gid = 0;
    gv_node_id hn = 0; int lx, ly;
    CHECK(gv_view_hit_test(v, x, y, &hn, &lx, &ly));
    CHECK(sdl_click(x, y) == 0);
    last_key(k, sizeof k);
    CHECK(strstr(k, "left-hit") != NULL);
    CHECK(hits() == 1);                            /* one click, one activation */
    /* right region: scroll with the wheel, then click its button */
    CHECK(rect_of(right(), &lr, 1) == 0);
    centre(&lr, &x, &y);
    for (int i = 0; i < 20; i++) CHECK(sdl_wheel(x, y, -1) == 0);
    CHECK(rect_of(righthit, &br, 1) == 0);
    centre(&br, &x, &y);
    CHECK(gv_glon_host_pointer_at(h, 1, x, y, &gid) == 0);
    CHECK(gv_glon_host_pointer_at(h, 2, x, y, NULL) == 0);
    last_key(k, sizeof k);
    CHECK(strstr(k, "right-hit") != NULL);
    /* nested control after scrolling both regions */
    set_off(1, 150);
    int ih = node_of("inner-hit");
    for (int i = 0; i < 4 && rect_of(ih, &br, 1) != 0; i++) set_off(1, 150 + 20 * i);
    if (rect_of(ih, &br, 1) == 0) {
        centre(&br, &x, &y);
        CHECK(sdl_click(x, y) == 0);
        last_key(k, sizeof k);
        CHECK(strstr(k, "inner-hit") != NULL);
    } else CHECK(!"inner-hit never visible");
    CHECK(hits() == 3);
    printf("activate: hits %d, last %s\n", hits(), k);
}

/* 5: focus and editing inside a scrolled region; Tab reaches clipped fields */
static void case_edit(void) {
    char t[96];
    gv_rect lr, fr; int x, y;
    CHECK(rect_of(left(), &lr, 1) == 0);
    set_off(1, off(1, 3));
    int f2 = node_of("f2");
    CHECK(rect_of(f2, &fr, 1) == 0);
    centre(&fr, &x, &y);
    CHECK(sdl_click(x, y) == 0);
    CHECK(gint("gui3/current-node") == f2);
    CHECK(sdl_text("X") == 0);
    gstr("block-at gui3/state gui3/find-node-by-key 'f2 5", t, sizeof t);
    CHECK(strstr(t, "gammaX") != NULL);
    CHECK(sdl_key(8) == 0);
    gstr("block-at gui3/state gui3/find-node-by-key 'f2 5", t, sizeof t);
    CHECK(strstr(t, "gamma") != NULL && strstr(t, "gammaX") == NULL);
    /* f0 is scrolled out: focusing it brings it into view */
    int f0 = node_of("f0");
    CHECK(rect_of(f0, &fr, 1) != 0);
    char e[96]; snprintf(e, sizeof e, "[ gui3/focus-set %d ]", f0);
    CHECK(run(e) == 0);
    CHECK(rect_of(f0, &fr, 1) == 0);
    CHECK(fr.h >= 20);
    CHECK(sdl_text("Y") == 0);
    gstr("block-at gui3/state gui3/find-node-by-key 'f0 5", t, sizeof t);
    CHECK(strstr(t, "alphaY") != NULL);
    /* runner Tab order: focus moves on and stays visible */
    int moved = 0;
    for (int i = 0; i < 6; i++) {
        CHECK(sdl_key(K_TAB) == 0);
        int f = (int)gint("gui3/current-node");
        if (f > 0 && f != f0) moved = 1;
        if (f > 0 && gint("either = block-at gui3/state gui3/current-node 3 'field' [ 1 ] [ 0 ]") == 1)
            CHECK(rect_of(f, &fr, 1) == 0);
    }
    CHECK(moved);
    printf("edit: f2 typed/backspaced after scroll; f0 focused into view\n");
}

/* 6: scroll state survives forced collections */
static void case_gc(void) {
    set_off(1, 133); set_off(2, 77); set_off(3, 41);
    CHECK(run("[ gc/collect loop: func [n] [ either > n 0 [ reduce [ n n n n ] loop - n 1 ] [ 0 ] ] loop 60 gc/collect gc/collect ]") == 0);
    CHECK(off(1, 1) == 133 && off(2, 1) == 77 && off(3, 1) == 41);
    gv_rect r; int x, y;
    CHECK(rect_of(vp_of(1), &r, 1) == 0);
    x = r.x + r.w - 4; y = r.y + 12;
    CHECK(sdl_wheel(x, y, 1) == 0);
    CHECK(off(1, 1) == 109);
    CHECK(run("[ gc/collect ]") == 0);
    CHECK(off(1, 1) == 109 && off(1, 3) > 0);
    printf("gc: offsets 133/77/41 kept across 3 collections; live cells %ld\n", (long)r0_s1_gc_live_cells());
}

/* 7: SC-1R component handles never collide with scroll records */
static void case_collide(void) {
    set_off(1, 90); set_off(2, 60); set_off(3, 30);
    long made = 0;
    for (int i = 0; i < 40; i++) {
        made = gint("either = sc/new-field \"x\" 0 [ 0 ] [ 1 ]") ? made + 1 : made;
        CHECK(run("[ sc/new-field \"y\" sc/new-field \"z\" sc/new-field \"w\" sc/new-field \"u\" sc/new-field \"t\" sc/new-field \"s\" sc/new-field \"r\" sc/new-field \"q\" sc/new-field \"p\" ]") == 0);
    }
    CHECK(off(1, 1) == 90 && off(2, 1) == 60 && off(3, 1) == 30);
    CHECK(gint("sc-next-id") <= 384);                /* capacity reached, no overrun */
    CHECK(gint("either = sc/new-field \"over\" 0 [ 1 ] [ 0 ]") == 1);
    CHECK(run("[ gc/collect ]") == 0);
    CHECK(off(1, 1) == 90 && off(3, 1) == 30);
    printf("collide: %ld + components allocated; sc-next-id %ld; offsets kept\n", made, gint("sc-next-id"));
}

/* 8 + 9: repeated rebuilds (theme switching) do not accumulate ids or records */
static void case_rebuild(void) {
    char k[64];
    set_off(1, 120); set_off(2, 50); set_off(3, 20);
    long id0 = -1, states0 = -1, scrolls0 = -1;
    int theme = node_of("theme");
    for (int i = 0; i < 30; i++) {
        gv_rect r; int x, y;
        theme = node_of("theme");
        CHECK(rect_of(theme, &r, 1) == 0);
        centre(&r, &x, &y);
        if (sdl_click(x, y) != 0 || gv_glon_host_error(h)) { CHECK(!"theme click failed"); break; }
        if (i == 1) { id0 = gint("view/id"); states0 = gint("t/states"); scrolls0 = gint("t/scrolls"); }
    }
    long id1 = gint("view/id"), states1 = gint("t/states"), scrolls1 = gint("t/scrolls");
    CHECK(id1 == id0 && states1 == states0 && scrolls1 == scrolls0);
    CHECK(scrolls1 == 3);
    CHECK(off(1, 1) == 120 && off(2, 1) == 50 && off(3, 1) == 20);
    gstr("block-at gui3/app 0", k, sizeof k);
    CHECK(strstr(k, "dark") != NULL);              /* 30 toggles */
    int views = 0;
    for (int i = 1; i <= v->count; i++) if (gv_view_node(v, i)) views++;
    /* controls of the latest build still work */
    gv_rect r; int x, y;
    set_off(1, off(1, 3));
    CHECK(rect_of(node_of("left-hit"), &r, 1) == 0);
    centre(&r, &x, &y);
    CHECK(sdl_click(x, y) == 0);
    last_key(k, sizeof k);
    CHECK(strstr(k, "left-hit") != NULL);
    printf("rebuild: 30 theme switches; view/id %ld -> %ld; faces %ld -> %ld; scroll records %ld; live View nodes %d\n",
           id0, id1, states0, states1, scrolls1, views);
}

/* 10: no stale events after destruction */
static void case_stale(void) {
    int lft = left(), lefthit = node_of("left-hit"), f0 = node_of("f0");
    gv_rect br; int x, y;
    set_off(1, off(1, 3));
    CHECK(rect_of(lefthit, &br, 1) == 0);
    centre(&br, &x, &y);
    char e[96]; snprintf(e, sizeof e, "[ gui3/focus-set %d ]", f0);
    CHECK(run(e) == 0);
    snprintf(e, sizeof e, "[ gui3/destroy-face %d ]", lft);
    CHECK(run(e) == 0);
    CHECK(gv_glon_host_view_node(h, lft) == 0);
    CHECK(gv_glon_host_view_node(h, lefthit) == 0);       /* whole subtree released */
    CHECK(gv_glon_host_view_node(h, f0) == 0);
    CHECK(gint("gui3/current-node") == 0);               /* focus dropped */
    int gid = -1;
    CHECK(gv_glon_host_pointer_at(h, 1, x, y, &gid) == 0);
    CHECK(gid != lefthit);
    CHECK(gv_glon_host_pointer_at(h, 2, x, y, NULL) == 0);
    CHECK(hits() == 0);
    /* events naming the dead ids directly are inert */
    CHECK(gv_glon_host_click(h, lefthit) == 0);
    CHECK(gv_glon_host_focus(h, f0) == 0);
    CHECK(sdl_text("Z") == 0);
    CHECK(sdl_key(K_PGDN) == 0);
    CHECK(gv_glon_host_wheel(h, x, y, 0, 1) == 0);
    CHECK(hits() == 0 && !gv_glon_host_error(h));
    CHECK(off(1, 1) == 0 && off(1, 3) == 0);             /* stale handle is inert */
    CHECK(gint("t/scrolls") == 1);                       /* left + inner released */
    printf("stale: destroyed left region; %ld scroll record left; no event reached it\n", gint("t/scrolls"));
}

/* resize: geometry, clipping and targeting agree after repeated resizes */
static void case_resize(void) {
    char k[64];
    set_off(1, 140);
    int sizes[][2] = { {600, 420}, {900, 600}, {500, 360}, {700, 480} };
    for (int i = 0; i < 4; i++) {
        SDL_Event e; memset(&e, 0, sizeof e);
        gv_event ev; memset(&ev, 0, sizeof ev);
        ev.type = GV_EVENT_RESIZE; ev.width = sizes[i][0]; ev.height = sizes[i][1];
        CHECK(gv_glon_host_dispatch(h, &ev) == 0);
        CHECK(gv_glon_host_resize(h, sizes[i][0], sizes[i][1]) == 0);
        CHECK(off(1, 1) == 140);
        gv_rect r; int x, y;
        if (rect_of(left(), &r, 1) == 0) {
            centre(&r, &x, &y);
            CHECK(gv_glon_host_wheel(h, x, y, 0, 1) == 0);
            CHECK(gv_glon_host_wheel(h, x, y, 0, -1) == 0);
            CHECK(off(1, 1) == 140);
        }
    }
    set_off(1, off(1, 3));
    gv_rect r; int x, y;
    CHECK(rect_of(node_of("left-hit"), &r, 1) == 0);
    centre(&r, &x, &y);
    CHECK(sdl_click(x, y) == 0);
    last_key(k, sizeof k);
    CHECK(strstr(k, "left-hit") != NULL);
    printf("resize: 4 resizes, offset kept, activation after resize ok\n");
}

static int render(void) {
    const gv_display_list *dl = gv_glon_host_display(h);
    if (!dl || b->begin_frame(b) != 0 || b->draw(b, dl) != 0 || b->end_frame(b) != 0) return -1;
    return 0;
}
static SDL_Window *find_window(void) {
    for (Uint32 id = 1; id < 16; id++) { SDL_Window *w = SDL_GetWindowFromID(id); if (w) return w; }
    return NULL;
}
/* pixels presented in the window (what the user sees) against the frame the
 * renderer drew (the PNG path reads the renderer's surface directly) */
static long present_mismatch(const char *png, int *sample) {
    SDL_Window *w = find_window();
    SDL_Surface *ws = w ? SDL_GetWindowSurface(w) : NULL;
    if (!ws || gv_backend_sdl_save_png(b, png) != 0) return -1;
    cairo_surface_t *cs = cairo_image_surface_create_from_png(png);
    if (cairo_surface_status(cs) != CAIRO_STATUS_SUCCESS) return -1;
    cairo_surface_flush(cs);
    unsigned char *d = cairo_image_surface_get_data(cs);
    int stride = cairo_image_surface_get_stride(cs);
    int W = cairo_image_surface_get_width(cs), H = cairo_image_surface_get_height(cs);
    long bad = 0;
    SDL_LockSurface(ws);
    for (int y = 0; y < H && y < ws->h; y++)
        for (int x = 0; x < W && x < ws->w; x++) {
            Uint32 px = 0;
            memcpy(&px, (Uint8 *)ws->pixels + y * ws->pitch + x * ws->format->BytesPerPixel, ws->format->BytesPerPixel);
            Uint8 r, g, bb;
            SDL_GetRGB(px, ws->format, &r, &g, &bb);
            Uint32 c = *(Uint32 *)(d + y * stride + x * 4);
            int cr = (c >> 16) & 255, cg = (c >> 8) & 255, cb = c & 255;
            if (abs(cr - r) > 1 || abs(cg - g) > 1 || abs(cb - bb) > 1) {
                if (!bad && sample) { sample[0] = r; sample[1] = g; sample[2] = bb; sample[3] = cr; sample[4] = cg; sample[5] = cb; }
                bad++;
            }
        }
    SDL_UnlockSurface(ws);
    cairo_surface_destroy(cs);
    return bad;
}

/* what is shown in the window equals the rendered frame, also after
 * scrolling (no ghosting of earlier frames) */
static void case_present(void) {
    int s[6] = {0};
    CHECK(render() == 0);
    long bad = present_mismatch("/tmp/b2c-present0.png", s);
    if (bad) fprintf(stderr, "first mismatch: window %d,%d,%d frame %d,%d,%d\n", s[0], s[1], s[2], s[3], s[4], s[5]);
    CHECK(bad == 0);
    gv_rect r;
    CHECK(rect_of(vp_of(1), &r, 1) == 0);
    for (int i = 0; i < 6; i++) {
        CHECK(sdl_wheel(r.x + r.w - 4, r.y + 12, -1) == 0);
        CHECK(render() == 0);
    }
    bad = present_mismatch("/tmp/b2c-present1.png", s);
    if (bad) fprintf(stderr, "after scrolling: window %d,%d,%d frame %d,%d,%d\n", s[0], s[1], s[2], s[3], s[4], s[5]);
    CHECK(bad == 0);
    printf("present: window pixels equal the rendered frame before and after scrolling\n");
}

static double ms_now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}
/* per-interaction cost through the runner path */
static void case_perf(void) {
    gv_rect a, c, vr; int ax, ay, cx, cy;
    CHECK(rect_of(node_of("theme"), &a, 1) == 0);
    CHECK(rect_of(node_of("top"), &c, 1) == 0);
    CHECK(rect_of(vp_of(2), &vr, 1) == 0);
    centre(&a, &ax, &ay); centre(&c, &cx, &cy);
    double q0 = ms_now();
    for (int i = 0; i < 10; i++) CHECK(gv_glon_host_tick(h) == 0);
    double q1 = ms_now();
    for (int i = 0; i < 10; i++) CHECK(gv_glon_host_run(h, "[ 0 ]") == 0);
    double q2 = ms_now();
    for (int i = 0; i < 10; i++) CHECK(gv_glon_host_bind(h) == 0);
    double q3 = ms_now();
    char e[128]; snprintf(e, sizeof e, "[ gui3/scroll-set %d 0 %d ]", right(), 40);
    for (int i = 0; i < 10; i++) { e[0] = '['; CHECK(gv_glon_host_run(h, e) == 0); }
    double q4 = ms_now();
    snprintf(e, sizeof e, "[ gui3/scroll-find-vp %d ]", vp_of(2));
    for (int i = 0; i < 10; i++) CHECK(gv_glon_host_run(h, e) == 0);
    double q5 = ms_now();
    const char *mb[] = { "[ s/from-int 12345 ]", "[ s/+ \"abc\" \"defgh\" ]", "[ view/rect 5 0 0 10 10 ]",
                         "[ view/viewport 5 0 0 ]", "[ gui3/scroll-record 5 ]", "[ parse-int \"12345\" ]",
                         "[ block-at gui3/scroll-state 5 1 ]", "[ gui3/state 5 ]",
                         "[ gui3/refresh gui3/find-node-by-key 'top widget/flush ]",
                         "[ gui3/find-node-by-key 'top ]",
                         "[ style/get 'button 'bg ]", "[ style/get 'button 'hover/fg ]",
                         "[ widget/flush ]", NULL };
    for (int k = 0; mb[k]; k++) {
        double m0 = ms_now();
        for (int i = 0; i < 20; i++) gv_glon_host_run(h, mb[k]);
        printf("  %-40s %.3f ms\n", mb[k], (ms_now() - m0) / 20);
    }
    printf("fixed costs (ms): tick event %.1f, empty run %.2f, bind %.2f, scroll-set %.1f, find-vp %.2f\n",
           (q1 - q0) / 10, (q2 - q1) / 10, (q3 - q2) / 10, (q4 - q3) / 10, (q5 - q4) / 10);
    double t0 = ms_now();
    for (int i = 0; i < 10; i++) {
        CHECK(gv_glon_host_pointer_at(h, 0, ax, ay, NULL) == 0);
        CHECK(gv_glon_host_pointer_at(h, 0, cx, cy, NULL) == 0);
    }
    double t1 = ms_now();
    for (int i = 0; i < 20; i++) CHECK(gv_glon_host_pointer_at(h, 0, cx + (i % 5), cy, NULL) == 0);
    double t2 = ms_now();
    for (int i = 0; i < 10; i++) CHECK(gv_glon_host_wheel(h, vr.x + 20, vr.y + 20, 0, i < 5 ? 1 : -1) == 0);
    double t3 = ms_now();
    for (int i = 0; i < 5; i++) CHECK(sdl_click(cx, cy) == 0);
    double t4 = ms_now();
    for (int i = 0; i < 10; i++) CHECK(sdl_key(K_TAB) == 0);
    double t5 = ms_now();
    for (int i = 0; i < 10; i++) CHECK(render() == 0);
    double t6 = ms_now();
    /* a burst of 50 motion samples and 10 wheel notches, coalesced */
    gv_event evs[64]; int n = 0;
    for (int i = 0; i < 50; i++) { memset(&evs[n], 0, sizeof evs[n]); evs[n].type = GV_EVENT_POINTER_MOVE; evs[n].x = ax + i % 7; evs[n].y = ay; n++; }
    for (int i = 0; i < 10; i++) { memset(&evs[n], 0, sizeof evs[n]); evs[n].type = GV_EVENT_WHEEL; evs[n].x = vr.x + 20; evs[n].y = vr.y + 20; evs[n].mods = 1; n++; }
    int y0 = off(2, 1);
    int m = gv_glon_host_coalesce(evs, n);
    double t7 = ms_now();
    for (int i = 0; i < m; i++) CHECK(gv_glon_host_dispatch(h, &evs[i]) == 0);
    double t8 = ms_now();
    CHECK(m == 2);
    CHECK(off(2, 1) == (y0 + 240 > off(2, 3) ? off(2, 3) : y0 + 240));
    printf("perf (ms per event): hover change %.1f, move within a node %.2f, wheel %.1f, click %.1f, Tab %.1f, frame %.1f; burst of 60 -> %d events, %.1f ms\n",
           (t1 - t0) / 20, (t2 - t1) / 20, (t3 - t2) / 10, (t4 - t3) / 5, (t5 - t4) / 10, (t6 - t5) / 10, m, t8 - t7);
}

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : "..";
    const char *which = argc > 2 ? argv[2] : "wheel";
    if (setup(root) != 0) { fprintf(stderr, "setup failed\n"); return 2; }
    if (!strcmp(which, "wheel")) case_wheel();
    else if (!strcmp(which, "nested")) case_nested();
    else if (!strcmp(which, "clip")) case_clip();
    else if (!strcmp(which, "activate")) case_activate();
    else if (!strcmp(which, "edit")) case_edit();
    else if (!strcmp(which, "gc")) case_gc();
    else if (!strcmp(which, "collide")) case_collide();
    else if (!strcmp(which, "rebuild")) case_rebuild();
    else if (!strcmp(which, "stale")) case_stale();
    else if (!strcmp(which, "resize")) case_resize();
    else if (!strcmp(which, "present")) case_present();
    else if (!strcmp(which, "perf")) case_perf();
    else { fprintf(stderr, "unknown case %s\n", which); return 2; }
    CHECK(!gv_glon_host_error(h));
    if (failures) { fprintf(stderr, "test_gui4b2c %s: %d failure(s)\n", which, failures); return 1; }
    printf("test_gui4b2c %s: ok\n", which);
    return 0;
}
