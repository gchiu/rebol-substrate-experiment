/* bench_gui_p2.c -- GUI-P2: interactive cost of the Glon GUI, measured
 * through the SDL runner's path.
 *
 * SDL events go into the SDL backend (dummy video driver unless SDL_VIDEODRIVER
 * says otherwise), are read with poll_event, coalesced and routed with
 * gv_glon_host_dispatch, and a frame is drawn and presented, exactly as
 * glon_demo does.  Each operation reports wall time (ms, incl. one frame),
 * collections and, in a -DR0_S1_PROFILE build, allocation calls and cells.
 *
 * Usage: bench_gui_p2 ROOT gallery|gui3 [REPEAT]
 *   gallery  the WG-3 widget gallery (glon_demo --vocab-vid)
 *   gui3     the GUI-3 scroll demo (glon_demo --gui3)
 */
#include "gv_glon_host.h"
#include "gv_view.h"
#include "gv_backend.h"
#include "r0_s1.h"
#include <SDL.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static gv_view *v;
static gv_backend *b;
static gv_glon_host *h;
static int W = 1200, H = 700;

static double ms_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; }

/* time inside the runner's dispatch and inside drawing (all pumps) */
static double t_dispatch, t_draw;
typedef struct { double t, disp, draw; long gc, allocs, cells, steps; } mark;
static mark mark_now(void) {
    mark m;
    m.disp = t_dispatch; m.draw = t_draw;
    gv_glon_host_pf_totals(&m.allocs, &m.cells);
    m.steps = gv_glon_host_pf_steps();
    m.t = ms_now(); m.gc = r0_s1_gc_count();
    return m;
}
static void report(const char *what, mark a, int n) {
    mark z = mark_now();
    if (n < 1) n = 1;
    printf("%-26s %9.1f ms  gc %5.1f  allocs %9.0f  cells %10.0f   (per op, n=%d)  dispatch %.1f draw %.1f steps %.0f\n", what,
           (z.t - a.t) / n, (double)(z.gc - a.gc) / n, (double)(z.allocs - a.allocs) / n,
           (double)(z.cells - a.cells) / n, n, (z.disp - a.disp) / n, (z.draw - a.draw) / n,
           (double)(z.steps - a.steps) / n);
    fflush(stdout);
}

static int render(void) {
    const gv_display_list *dl = gv_glon_host_display(h);
    if (!dl || b->begin_frame(b) != 0 || b->draw(b, dl) != 0 || b->end_frame(b) != 0) return -1;
    return 0;
}
static int pump(void) {
    gv_event evs[256];
    int n = 0, rc = 0;
    while (n < 256 && b->poll_event(b, &evs[n]) > 0) n++;
    n = gv_glon_host_coalesce(evs, n);
    double t0 = ms_now();
    for (int i = 0; i < n; i++) if (gv_glon_host_dispatch(h, &evs[i]) != 0) rc = -1;
    double t1 = ms_now();
    if (n > 0 && render() != 0) rc = -1;
    t_dispatch += t1 - t0;
    t_draw += ms_now() - t1;
    return rc;
}
static void push_button(int type, int x, int y) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = type; e.button.button = SDL_BUTTON_LEFT; e.button.x = x; e.button.y = y;
    e.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
    SDL_PushEvent(&e);
}
static void push_motion(int x, int y) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = SDL_MOUSEMOTION; e.motion.x = x; e.motion.y = y;
    SDL_PushEvent(&e);
}
static int click(int x, int y) { push_motion(x, y); push_button(SDL_MOUSEBUTTONDOWN, x, y); push_button(SDL_MOUSEBUTTONUP, x, y); return pump(); }
static int key(int sym) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = SDL_KEYDOWN; e.key.keysym.sym = sym; e.key.state = SDL_PRESSED;
    SDL_PushEvent(&e);
    return pump();
}
static int text(const char *s) {
    for (; *s; s++) {
        SDL_Event e; memset(&e, 0, sizeof e);
        e.type = SDL_TEXTINPUT; e.text.text[0] = *s;
        SDL_PushEvent(&e);
    }
    return pump();
}
static SDL_Window *find_window(void) {
    for (Uint32 id = 1; id < 16; id++) { SDL_Window *w = SDL_GetWindowFromID(id); if (w) return w; }
    return NULL;
}
/* resize through the platform window, as a user drag does */
static int resize_window(int w, int hh) {
    SDL_SetWindowSize(find_window(), w, hh);
    return pump();
}
static gv_node_id text_node(const char *t) {
    for (int i = 1; i <= v->count; i++) {
        gv_node *n = gv_view_node(v, i);
        if (n && n->type == GV_NODE_TEXT && n->text && !strcmp(n->text, t)) return (gv_node_id)i;
    }
    return 0;
}
static int click_text(const char *t) {
    gv_node_id n = text_node(t);
    gv_rect r;
    if (!n) { fprintf(stderr, "no text %s\n", t); return -1; }
    if (v->layout_dirty) gv_view_layout(v);
    if (gv_view_visible_rect(v, n, &r) != 0) { fprintf(stderr, "text %s not visible\n", t); return -1; }
    return click(r.x + r.w / 2, r.y + r.h / 2);
}
static int run(const char *src) {
    if (gv_glon_host_run(h, src) != 0) return -1;
    return gv_glon_host_deliver(h);
}

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : "..";
    const char *app = argc > 2 ? argv[2] : "gallery";
    int rep = argc > 3 && strcmp(argv[2], "expr") ? atoi(argv[3]) : 1;
    int gallery = strcmp(app, "gui3") != 0;
    if (!gallery) { W = 700; H = 480; }
    if (!getenv("SDL_VIDEODRIVER")) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    printf("bench_gui_p2 %s, video driver %s\n", app, getenv("SDL_VIDEODRIVER"));

    mark m = mark_now();
    v = gv_view_new(W, H);
    b = gv_backend_sdl();
    gv_backend_config cfg; memset(&cfg, 0, sizeof cfg);
    cfg.width = W; cfg.height = H; cfg.title = "p2";
    if (!v || !b || b->init(b, &cfg) != 0 || b->create_window(b, W, H, "p2") != 0) return 2;
    h = gv_glon_host_new(v, b);
    int rc = gallery ? gv_glon_host_load_machine_vid_app(h, root, "view/glon/widget_gallery.glon")
                     : gv_glon_host_load_gui3_app(h, root, "view/glon/gui4_scroll_demo.glon");
    if (!h || rc != 0) return 2;
    report("load", m, 1);
    {
        cell gbase = 0, gcap = 0;
        r0_s1_global_usage(&gbase, &gcap);
        printf("code space: %ld of %d cells used; globals %ld of %ld\n", (long)r0_s1_code_size(),
               (int)R0S1_CODE_LIMIT, (long)gbase, (long)gcap);
    }
    m = mark_now();
    if (gv_glon_host_build_app(h, NULL) != 0 || gv_glon_host_bind(h) != 0 || gv_glon_host_error(h)) return 2;
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    if (render() != 0) return 2;
    report("first build + frame", m, 1);
    printf("view nodes %d, heap high %ld\n", gv_view_live_nodes(v), r0_s1_heap_high());
    /* BENCH_GC_FIRST=1: collect once after the first build, to separate the
     * cost of the code from the state the first build leaves in the heap */
    if (getenv("BENCH_GC_FIRST")) { run("[ gc/collect ]"); printf("collected after the first build\n"); }
    m = mark_now();
    for (int i = 0; i < 10; i++) render();
    report("frame only (draw+present)", m, 10);

    /* expr mode: bench_gui_p2 ROOT expr "[ ... ]" ...  -- per-expression cost */
    if (!strcmp(app, "expr")) {
        for (int k = 3; k < argc; k++) {
            m = mark_now();
            for (int i = 0; i < 20; i++) if (run(argv[k]) != 0) return 3;
            report(argv[k], m, 20);
        }
        return 0;
    }
    for (int r = 0; r < rep; r++) {
        if (gallery) {
            m = mark_now();
            for (int i = 0; i < 5; i++) run("[ vid/button gallery/theme gallery/id 37 0 0 100 30 \"B\" widget/flush ]");
            report("vid/button", m, 5);
            m = mark_now();
            for (int i = 0; i < 5; i++) run("[ vid/label gallery/theme gallery/id 37 0 0 100 30 \"L\" widget/flush ]");
            report("vid/label", m, 5);
            m = mark_now();
            for (int i = 0; i < 20; i++) run("[ view/rect 5 1 2 3 4 widget/flush ]");
            report("view/rect command", m, 20);
            m = mark_now();
            for (int i = 0; i < 6; i++) if (click_text(i & 1 ? "Field" : "Radio") != 0) return 3;
            report("preview switch", m, 6);
            m = mark_now();
            if (click_text("Type here") != 0) return 3;
            report("focus field", m, 1);
            m = mark_now();
            for (int i = 0; i < 10; i++) if (text("x") != 0) return 3;
            report("keystroke", m, 10);
            m = mark_now();
            for (int i = 0; i < 6; i++) if (key(9) != 0) return 3;
            report("Tab", m, 6);
            gv_node_id a = text_node("Radio"), c = text_node("Panel");
            gv_rect ra, rc2;
            gv_view_visible_rect(v, a, &ra); gv_view_visible_rect(v, c, &rc2);
            m = mark_now();
            for (int i = 0; i < 10; i++) { push_motion(ra.x + 5, ra.y + 5); pump(); push_motion(rc2.x + 5, rc2.y + 5); pump(); }
            report("hover change", m, 20);
            m = mark_now();
            for (int i = 0; i < 2; i++) if (click_text("Theme") != 0) return 3;
            report("theme switch", m, 2);
        } else {
            m = mark_now();
            for (int i = 0; i < 6; i++) if (key(9) != 0) return 3;
            report("Tab", m, 6);
            m = mark_now();
            for (int i = 0; i < 10; i++) {
                SDL_Event e; memset(&e, 0, sizeof e);
                e.type = SDL_MOUSEWHEEL; e.wheel.y = (i & 1) ? 1 : -1;
                push_motion(200, 200); SDL_PushEvent(&e); pump();
            }
            report("wheel step", m, 10);
            m = mark_now();
            for (int i = 0; i < 10; i++) { push_motion(60, 100); pump(); push_motion(60, 160); pump(); }
            report("pointer move", m, 20);
        }
        int sizes[][2] = { {W - 200, H - 100}, {W + 200, H + 100}, {W, H} };
        m = mark_now();
        for (int i = 0; i < 3; i++) if (resize_window(sizes[i][0], sizes[i][1]) != 0) return 3;
        report("resize (one step)", m, 3);
        m = mark_now();
        for (int i = 0; i < 20; i++) SDL_SetWindowSize(find_window(), W - 10 * i, H - 5 * i);
        if (pump() != 0) return 3;
        if (resize_window(W, H) != 0) return 3;
        report("drag: 20 sizes, 2 frames", m, 1);
    }
    m = mark_now();
    run("[ gc/collect ]");
    report("forced gc", m, 1);
    printf("final: view nodes %d, view/id ", gv_view_live_nodes(v));
    char buf[64] = "";
    gv_glon_host_text(h, "view/id", buf, sizeof buf);
    printf("%s, host error %d\n", buf, gv_glon_host_error(h));
    return gv_glon_host_error(h) ? 1 : 0;
}
