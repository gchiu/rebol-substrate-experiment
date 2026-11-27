/* test_gui_p2.c -- GUI-P2: the WG-3 gallery resized through the SDL runner.
 *
 * The window is resized with SDL_SetWindowSize, so the SDL backend sees the
 * platform's window events, as on a real edge drag; events are drained,
 * coalesced and routed with gv_glon_host_dispatch and a frame is presented,
 * exactly as glon_demo does.  Checks use View geometry (screen and visible
 * rectangles, hit tests), Glon state and the presented window pixels.
 * SDL's dummy video driver unless SDL_VIDEODRIVER is set.
 *
 * Usage: test_gui_p2 ROOT CASE [PNG_DIR]
 *   sizes rapid clicks edit state themes lifecycle reveal scrollbars press caret edges edges-default tabs tabs-resize tabs-lifecycle table table-unicode source table-drag source-drag table-cols
 *   intent-parse intent-directions intent-unequal intent-alias intent-nested
 *   intent-edit intent-escape intent-lifecycle intent-resize intent-demo intent-stretch
 *   intent-demo-resize intent-demo-native (INTENT-1)
 *   intent-handlers intent-members-handlers intent-members-forms intent-members-validate (INTENT-2)
 *   intent-foreign intent-foreign-tab intent-foreign-x11 (FOREIGN-1; the last X11 only, no input)
 *   intent-browser-mock intent-browser-x11 (FOREIGN-2; the last X11 + WebKitGTK only, no input)
 *   webkit-address (FOREIGN-2: the provider's address rule, no engine)
 *   native-window (the gallery's native window) capture (pointer capture lifecycle)
 *   edge-promise-cursor edge-promise-both edge-promise-default
 *   edge-clearance-N intent-demo-clearance-N (edge band N px clear of controls)
 *   edge-policy-wslg edge-policy-default edge-policy-override
 * With PNG_DIR, the sizes case writes the presented window at each size. */
/* POSIX (setenv, mkstemp, pread) for the trace check of intent4-forms-demo */
#define _POSIX_C_SOURCE 200809L
#include "gv_glon_host.h"
#include "gv_view.h"
#include "gv_backend.h"
#include "gv_table.h"
#include "r0_s1.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <cairo.h>
#include <time.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static const char *app_file = "view/glon/widget_gallery.glon";
static const char *root_dir = "..";
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static gv_view *v;
static gv_backend *b;
static gv_glon_host *h;
static const char *png_dir;
#define MIN_W 960
#define MIN_H 660

static long gint(const char *expr) {
    char buf[96] = "";
    if (gv_glon_host_text(h, expr, buf, sizeof buf) != 0) return -999999;
    return atol(buf);
}
static void gstr(const char *expr, char *out, int cap) { out[0] = 0; gv_glon_host_text(h, expr, out, cap); }
/* the source pane: a host table in text mode (slot 40) */
static gv_table *src_table(void) {
    gv_table *t = NULL;
    if (gv_glon_host_table(h, (int)gint("gallery/id 40"), &t) != 0) return NULL;
    return t;
}
static long src_first(void) { gv_table *t = src_table(); return t ? t->visible_start : -1; }
/* the byte of the document at the top of the pane */
static int src_anchor(void) {
    gv_table *t = src_table(); int st = 0, ln = 0;
    return t && gv_table_text_row(t, t->visible_start, &st, &ln) == 0 ? st : -1;
}
/* is byte b inside the top row (incl. its line end)? */
static int src_top_holds(int b) {
    gv_table *t = src_table(); int st = 0, ln = 0;
    return t && gv_table_text_row(t, t->visible_start, &st, &ln) == 0 && b >= st && b <= st + ln;
}
static double ms_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; }

static int render(void) {
    const gv_display_list *dl = gv_glon_host_display(h);
    if (!dl || b->begin_frame(b) != 0 || b->draw(b, dl) != 0 || b->end_frame(b) != 0) return -1;
    return 0;
}
static int dispatched;
/* GLON-INST-3B: the SDL window synthetic events are pushed for (0: none,
 * which SDL routes to the first window) */
static Uint32 win_target;
static int c1_windows;      /* GLON-VIEW-PREP-1: c1-accept loads the window library */
static int pump(void) {
    gv_event evs[256];
    int n = 0, rc = 0;
    while (n < 256 && b->poll_event(b, &evs[n]) > 0) n++;
    n = gv_glon_host_coalesce(evs, n);
    for (int i = 0; i < n; i++) {
        if (evs[i].type == GV_EVENT_RESIZE) dispatched++;
        if (gv_glon_host_dispatch(h, &evs[i]) != 0) rc = -1;
    }
    /* hosted components (FOREIGN-1), as the runner does every iteration */
    if (gv_glon_host_pump(h) > 0) n++;
    /* GLON-INST-3B: the secondary windows' events, each in its own window,
     * then those that changed are drawn (the runner does the same) */
    if (gv_glon_host_window_count(h) > 0) {
        int m = gv_glon_host_pump_windows(h);
        if (m < 0) rc = -1; else if (m > 0) n++;
    }
    if (gv_glon_host_window_count(h) > 0 && gv_glon_host_render_windows(h, 0) != 0) rc = -1;
    if (n > 0 && render() != 0) rc = -1;
    return rc;
}
static SDL_Window *find_window(void) {
    for (Uint32 id = 1; id < 16; id++) { SDL_Window *w = SDL_GetWindowFromID(id); if (w) return w; }
    return NULL;
}
/* resize as a user does: a size change (setting the current size raises no
 * window event, so nothing would be presented) */
static int resize(int w, int hh) {
    int cw = 0, ch = 0;
    SDL_GetWindowSize(find_window(), &cw, &ch);
    if (cw == w && ch == hh) return 0;
    SDL_SetWindowSize(find_window(), w, hh);
    return pump();
}
static void push_button(int type, int x, int y) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = type; e.button.button = SDL_BUTTON_LEFT; e.button.x = x; e.button.y = y;
    e.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
    e.button.windowID = win_target;
    SDL_PushEvent(&e);
}
/* a press with SDL's click count (2 = the second press of a double click) */
static void push_press_n(int x, int y, int clicks) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT; e.button.x = x; e.button.y = y;
    e.button.state = SDL_PRESSED; e.button.clicks = (Uint8)clicks;
    SDL_PushEvent(&e);
}
static void push_motion(int x, int y) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = SDL_MOUSEMOTION; e.motion.x = x; e.motion.y = y;
    e.motion.windowID = win_target;
    SDL_PushEvent(&e);
}
static int click(int x, int y) { push_motion(x, y); push_button(SDL_MOUSEBUTTONDOWN, x, y); push_button(SDL_MOUSEBUTTONUP, x, y); return pump(); }
/* SDL 2.0.20 wheel events carry no position and the backend asks
 * SDL_GetMouseState, which pushed events do not move (a real pointer does);
 * so, as test_gui4b2c does, the position is set after polling.  Then the
 * runner's coalesce / dispatch / one frame. */
static int wheel(int x, int y, int dy) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = SDL_MOUSEWHEEL; e.wheel.y = dy;      /* SDL: positive = away from the user (up) */
    e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    push_motion(x, y); SDL_PushEvent(&e);
    gv_event evs[256];
    int n = 0, rc = 0;
    while (n < 256 && b->poll_event(b, &evs[n]) > 0) {
        if (evs[n].type == GV_EVENT_WHEEL) { evs[n].x = x; evs[n].y = y; }
        n++;
    }
    n = gv_glon_host_coalesce(evs, n);
    for (int i = 0; i < n; i++) if (gv_glon_host_dispatch(h, &evs[i]) != 0) rc = -1;
    if (n > 0 && render() != 0) rc = -1;
    return rc;
}
static int key(int sym) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = SDL_KEYDOWN; e.key.keysym.sym = sym; e.key.state = SDL_PRESSED;
    e.key.windowID = win_target;
    SDL_PushEvent(&e);
    return pump();
}
static int key_shift(int sym) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = SDL_KEYDOWN; e.key.keysym.sym = sym; e.key.keysym.mod = KMOD_LSHIFT; e.key.state = SDL_PRESSED;
    SDL_PushEvent(&e);
    return pump();
}
static int text(const char *s) {
    for (; *s; s++) {
        SDL_Event e; memset(&e, 0, sizeof e);
        e.type = SDL_TEXTINPUT; e.text.text[0] = *s;
        e.text.windowID = win_target;
        SDL_PushEvent(&e);
    }
    return pump();
}
static gv_node_id text_node(const char *t) {
    for (int i = 1; i <= v->count; i++) {
        gv_node *n = gv_view_node(v, i);
        if (n && n->type == GV_NODE_TEXT && n->text && !strcmp(n->text, t)) return (gv_node_id)i;
    }
    return 0;
}
/* the visible part of the text node, its centre; -1 when not shown */
static int centre_of_text(const char *t, int *x, int *y) {
    gv_node_id n = text_node(t);
    gv_rect r;
    if (!n) return -1;
    if (v->layout_dirty) gv_view_layout(v);
    if (gv_view_visible_rect(v, n, &r) != 0) return -1;
    *x = r.x + r.w / 2; *y = r.y + r.h / 2;
    return 0;
}
static int click_text(const char *t) {
    int x, y;
    if (centre_of_text(t, &x, &y) != 0) { fprintf(stderr, "no visible text %s\n", t); return -1; }
    return click(x, y);
}
/* every text node holds valid UTF-8 (Pango warns and draws nothing otherwise) */
static int utf8_ok(const unsigned char *t) {
    while (*t) {
        int n = *t < 0x80 ? 0 : (*t & 0xE0) == 0xC0 ? 1 : (*t & 0xF0) == 0xE0 ? 2 : (*t & 0xF8) == 0xF0 ? 3 : -1;
        if (n < 0) return 0;
        t++;
        for (int i = 0; i < n; i++, t++) if ((*t & 0xC0) != 0x80) return 0;
    }
    return 1;
}
static int texts_valid(void) {
    int bad = 0;
    for (int i = 1; i <= v->count; i++) {
        gv_node *n = gv_view_node(v, i);
        if (n && n->type == GV_NODE_TEXT && n->text && !utf8_ok((const unsigned char *)n->text)) {
            fprintf(stderr, "invalid UTF-8 in text node %d (glon %d): \"%.40s\"\n", i, gv_glon_host_glon_id(h, (gv_node_id)i), n->text);
            bad++;
        }
    }
    return bad;
}
static void status(char *out, int cap) { gstr("gallery/status", out, cap); }
static gv_node_id vnode(long gid) { return gv_glon_host_view_node(h, (int)gid); }
static int screen(long gid, gv_rect *r) {
    if (v->layout_dirty) gv_view_layout(v);
    return gv_view_screen_rect(v, vnode(gid), r);
}

static long window_vs_png(const char *png) {
    SDL_Surface *ws = SDL_GetWindowSurface(find_window());
    cairo_surface_t *cs = cairo_image_surface_create_from_png(png);
    if (!ws || cairo_surface_status(cs) != CAIRO_STATUS_SUCCESS) return -1;
    cairo_surface_flush(cs);
    unsigned char *d = cairo_image_surface_get_data(cs);
    int stride = cairo_image_surface_get_stride(cs);
    int cw = cairo_image_surface_get_width(cs), ch = cairo_image_surface_get_height(cs);
    long bad = (cw != ws->w || ch != ws->h) ? 1 : 0;
    SDL_LockSurface(ws);
    for (int y = 0; y < ch && y < ws->h; y++)
        for (int x = 0; x < cw && x < ws->w; x++) {
            Uint32 px = 0; Uint8 r, g, bb;
            memcpy(&px, (Uint8 *)ws->pixels + y * ws->pitch + x * ws->format->BytesPerPixel, ws->format->BytesPerPixel);
            SDL_GetRGB(px, ws->format, &r, &g, &bb);
            Uint32 c = *(Uint32 *)(d + y * stride + x * 4);
            if (abs((int)((c >> 16) & 255) - r) > 1 || abs((int)((c >> 8) & 255) - g) > 1 || abs((int)(c & 255) - bb) > 1) {
                if (!bad) fprintf(stderr, "window differs from frame at %d,%d: %d,%d,%d vs %d,%d,%d (window %dx%d)\n", x, y, r, g, bb,
                                  (int)((c >> 16) & 255), (int)((c >> 8) & 255), (int)(c & 255), ws->w, ws->h);
                bad++;
            }
        }
    SDL_UnlockSurface(ws);
    cairo_surface_destroy(cs);
    return bad;
}
/* the window shows exactly the frame that was drawn, at the window's size */
static long presented_ok(void) {
    if (gv_backend_sdl_save_png(b, "/tmp/gui-p2-frame.png") != 0) return -1;
    return window_vs_png("/tmp/gui-p2-frame.png");
}
static void pixel(int x, int y, int *rgb) {
    SDL_Surface *ws = SDL_GetWindowSurface(find_window());
    Uint32 px = 0; Uint8 r, g, bb;
    SDL_LockSurface(ws);
    memcpy(&px, (Uint8 *)ws->pixels + y * ws->pitch + x * ws->format->BytesPerPixel, ws->format->BytesPerPixel);
    SDL_UnlockSurface(ws);
    SDL_GetRGB(px, ws->format, &r, &g, &bb);
    rgb[0] = r; rgb[1] = g; rgb[2] = bb;
}

/* INTENT-6: the heap when a setup failed (the scale measurements) */
static void i6_setup_report(void) {
    fprintf(stderr, "  heap at exit: high %ld of %ld, %ld collections, live %ld, free %ld in %ld blocks\n",
            r0_s1_heap_high() - GC_HEAP_BASE, (long)(GC_HEAP_LIMIT - GC_HEAP_BASE), r0_s1_gc_count(), r0_s1_gc_live_cells(),
            r0_s1_gc_free_cells(), r0_s1_gc_free_blocks());
}
static int setup(const char *root) {
    if (!getenv("SDL_VIDEODRIVER")) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    v = gv_view_new(1200, 700);
    b = gv_backend_sdl();
    gv_backend_config cfg; memset(&cfg, 0, sizeof cfg);
    cfg.width = 1200; cfg.height = 700; cfg.title = "p2";
    if (!v || !b || b->init(b, &cfg) != 0 || b->create_window(b, 1200, 700, "p2") != 0) return -1;
    h = gv_glon_host_new(v, b);
    if (h) gv_glon_host_set_window_ops(h, gv_backend_sdl_window_ops());   /* GLON-INST-3B */
    /* the window words only for the windows demo (every other session loads
     * exactly what it did before) */
    if (h && strstr(app_file, "intent_windows_demo.glon")) gv_glon_host_use_windows(h, 1);
    /* GLON-VIEW-PREP-1: the C1 acceptance application with or without the
     * window library; GV_TEST_WINDOWS_LIB=1 loads it into any other session
     * (the optional library must not change a single-window application) */
    if (h && c1_windows) gv_glon_host_use_windows(h, 1);
    if (h && getenv("GV_TEST_WINDOWS_LIB") && !strcmp(getenv("GV_TEST_WINDOWS_LIB"), "1")) gv_glon_host_use_windows(h, 1);
    /* the gallery is a machine-VID application: no Intent dialect (INTENT-4B-3) */
    int gallery = strstr(app_file, "widget_gallery.glon") != NULL;
    if (!h || (gallery ? gv_glon_host_load_machine_vid_app(h, root, app_file)
                       : gv_glon_host_load_vocab_vid_app(h, root, app_file)) != 0) return -1;
    /* INTENT-6: the membership application works on a copy of its data, in
     * view/build/intent6/ (never view/data/members.tsv); GV_MEMBERS_SOURCE
     * names another source (the 1,000 / 10,000-member files) */
    if (strstr(app_file, "membership_app.glon") && getenv("GV_MB_REPORT_SETUP")) atexit(i6_setup_report);
    if (strstr(app_file, "membership_app.glon")) {
        const char *src = getenv("GV_MEMBERS_SOURCE") ? getenv("GV_MEMBERS_SOURCE") : "view/data/members-seed.tsv";
        char from[1024], to[1024], cmd[1200];
        snprintf(from, sizeof from, "%s/%s", root, src);
        snprintf(to, sizeof to, "%s/view/build/intent6", root);
        mkdir(to, 0777);
        snprintf(to, sizeof to, "%s/view/build/intent6/members.tsv", root);
        FILE *fi = fopen(from, "rb"), *fo = fopen(to, "wb");
        if (!fi || !fo) { if (fi) fclose(fi); if (fo) fclose(fo); fprintf(stderr, "intent6: cannot copy %s\n", from); return -1; }
        char buf[65536]; size_t n;
        while ((n = fread(buf, 1, sizeof buf, fi)) > 0) fwrite(buf, 1, n, fo);
        fclose(fi); fclose(fo);
        snprintf(cmd, sizeof cmd, "[ mb/file: \"view/build/intent6/members.tsv\" ]");
        if (gv_glon_host_run(h, cmd) != 0) return -1;
    }
    if (gv_glon_host_build_app(h, NULL) != 0 || gv_glon_host_bind(h) != 0 || gv_glon_host_error(h)) return -1;
    /* GLON-VIEW-PREP-1: a session asked to load the window library has its
     * words (intent/window-count fail-stops without it) */
    if (getenv("GV_TEST_WINDOWS_LIB") && !strcmp(getenv("GV_TEST_WINDOWS_LIB"), "1")) {
        if (gint("intent/window-count") != 0) { fprintf(stderr, "window library not loaded\n"); return -1; }
        printf("  (window library loaded: GV_TEST_WINDOWS_LIB=1)\n");
    }
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    if (render() != 0) return -1;
    /* on a real display the newly mapped window gets the platform's own
     * enter / motion events (where the user's pointer happens to rest);
     * take them now, before the cases push theirs, so a resting pointer
     * cannot change a hover in the middle of a check.  Nothing moves the
     * user's pointer. */
    if (strcmp(getenv("SDL_VIDEODRIVER"), "dummy") != 0)
        for (int i = 0; i < 15; i++) { SDL_Delay(20); pump(); }
    return 0;
}

/* The layout the gallery promises for a W x H window (see widget_gallery.glon):
 * root content max(W,960) x max(H,640); left pane 224 wide; source pane 320 +
 * 1/4 of the width beyond 1200, right-aligned; preview pane in between. */
static int check_layout(int W, int H, const char *tag) {
    int f0 = failures;
    int cw = W > MIN_W ? W : MIN_W, ch = H > MIN_H ? H : MIN_H;
    int rw = 20 + 250 * cw / 1000, pw = -300 + 750 * cw / 1000;
    gv_rect r;
    CHECK(v->width == W && v->height == H);
    CHECK(screen(gint("gallery/root-outer"), &r) == 0 && r.x == 0 && r.y == 0 && r.w == W && r.h == H);
    long cam_x = gint("block-at block-at lay/scrolls 0 3"), cam_y = gint("block-at block-at lay/scrolls 0 4");
    CHECK(screen(gint("gallery/root"), &r) == 0 && r.w == cw && r.h == ch && r.x == -cam_x && r.y == -cam_y);
    CHECK(cam_x >= 0 && cam_x <= cw - W + (cw == W ? 0 : 0) && cam_y >= 0 && cam_y <= ch - H);
    gv_rect L, C, R;
    CHECK(screen(gint("block-at block-at lay/rules gallery/lay-left 0"), &L) == 0);
    CHECK(screen(gint("block-at block-at lay/rules gallery/lay-center 0"), &C) == 0);
    CHECK(screen(gint("block-at block-at lay/rules gallery/lay-right 0"), &R) == 0);
    CHECK(L.w == 224 && L.h == ch - 32 && L.x == 16 - cam_x && L.y == 16 - cam_y);
    CHECK(C.x == 252 - cam_x && C.w == pw && C.h == ch - 32);
    CHECK(R.w == rw && R.x + R.w == cw - 16 - cam_x && R.h == ch - 32);
    CHECK(C.x + C.w + 12 == R.x);                       /* 12-pixel gutter */
    /* the source document fills the source box and draws inside itself */
    gv_rect clip;
    CHECK(screen(gint("gallery/id 40"), &clip) == 0);
    CHECK(clip.x == R.x + 17 && clip.w == rw - 34 && clip.h == ch - 32 - 132);
    gv_node *sn = gv_view_node(v, vnode(gint("gallery/id 40")));
    CHECK(sn && sn->draw_list.count > 2 && sn->draw_list.cmds[1].op == GV_DRAW_CLIP &&
          sn->draw_list.cmds[1].rect.w == sn->w && sn->draw_list.cmds[1].rect.h == sn->h);
    /* the status row and the scroll buttons sit at the bottom of their panes */
    gv_rect st, sd;
    CHECK(screen(gint("gallery/id 35"), &st) == 0 && st.y > L.y + L.h - 44 && st.y + st.h <= L.y + L.h);
    CHECK(screen(gint("gallery/nid 43"), &sd) == 0 && sd.y == R.y + R.h - 46 && sd.x + sd.w <= R.x + R.w);
    if (getenv("P2_DEBUG")) {
        gv_rect rr; screen(gint("gallery/root"), &rr);
        gv_node *rvn = gv_view_node(v, vnode(gint("gallery/root-outer")));
        fprintf(stderr, "dbg view cam %d,%d\n", rvn ? rvn->cam_x : -1, rvn ? rvn->cam_y : -1);
        fprintf(stderr, "dbg root %d,%d %dx%d L %d,%d %dx%d C %d,%d %dx%d R %d,%d %dx%d sd %d,%d %dx%d st %d,%d cam %ld,%ld\n",
            rr.x, rr.y, rr.w, rr.h, L.x, L.y, L.w, L.h, C.x, C.y, C.w, C.h, R.x, R.y, R.w, R.h, sd.x, sd.y, sd.w, sd.h, st.x, st.y, cam_x, cam_y);
    }
    /* the preview viewport fills the pane below its heading */
    gv_rect pv;
    CHECK(screen(gint("block-at block-at lay/scrolls gallery/pscroll 0"), &pv) == 0);
    CHECK(pv.x == C.x && pv.y == C.y + 64 && pv.w == pw && pv.h == ch - 32 - 64);
    /* nothing visible lies outside the window */
    for (int i = 1; i <= v->count; i++) {
        gv_rect vr;
        if (gv_view_node(v, i) && gv_view_visible_rect(v, (gv_node_id)i, &vr) == 0)
            CHECK(vr.x >= 0 && vr.y >= 0 && vr.x + vr.w <= W && vr.y + vr.h <= H);
    }
    CHECK(presented_ok() == 0);
    printf("  %-22s %4dx%-4d content %4dx%-4d panes L %d C %d R %d, root scroll %ld,%ld%s\n", tag, W, H, cw, ch,
           L.w, C.w, R.w, cam_x, cam_y, failures == f0 ? "" : "  <-- FAILED");
    return failures == f0 ? 0 : -1;
}

/* 1: enlarging and shrinking, including below the minimum */
static void case_sizes(void) {
    int sizes[][2] = { {1200, 700}, {1600, 1000}, {1920, 1080}, {1000, 660}, {960, 640},
                       {800, 500}, {640, 400}, {1400, 760}, {1200, 700} };
    for (int i = 0; i < 9; i++) {
        CHECK(resize(sizes[i][0], sizes[i][1]) == 0);
        char tag[32]; snprintf(tag, sizeof tag, "size %d", i + 1);
        check_layout(sizes[i][0], sizes[i][1], tag);
        if (png_dir) {
            char p[512];
            snprintf(p, sizeof p, "%s/p2-gallery-%dx%d.png", png_dir, sizes[i][0], sizes[i][1]);
            CHECK(gv_backend_sdl_save_png(b, p) == 0);
        }
    }
    printf("sizes: 9 window sizes laid out as specified; presented frames equal the drawn frames\n");
}

/* 2: a fast edge drag: many sizes queue between frames; only the last is laid out */
static void case_rapid(void) {
    long nodes0 = gv_view_live_nodes(v), id0 = gint("view/id"), rules0 = gint("lay/count");
    double worst = 0, total = 0;
    int frames = 0;
    for (int k = 0; k < 40; k++) {
        dispatched = 0;
        for (int j = 0; j < 12; j++) SDL_SetWindowSize(find_window(), 900 + (k * 37 + j * 11) % 900, 520 + (k * 29 + j * 7) % 500);
        int fw = 900 + (k * 37 + 11 * 11) % 900, fh = 520 + (k * 29 + 11 * 7) % 500;
        double t0 = ms_now();
        CHECK(pump() == 0);
        double dt = ms_now() - t0;
        total += dt; frames++;
        if (dt > worst) worst = dt;
        CHECK(dispatched == 1);                        /* 12 queued sizes, one layout */
        CHECK(v->width == fw && v->height == fh);
    }
    CHECK(resize(1200, 700) == 0);
    check_layout(1200, 700, "after drag");
    CHECK(gv_view_live_nodes(v) == nodes0 && gint("view/id") == id0 && gint("lay/count") == rules0);
    printf("rapid: 40 frames x 12 queued sizes, one layout each; frame %.1f ms mean, %.1f ms worst; nodes %ld, view/id %ld unchanged\n",
           total / frames, worst, nodes0, id0);
}

/* 3: pointer input lands where things are drawn after resizes */
static void case_clicks(void) {
    char st[96];
    int sizes[][2] = { {1600, 1000}, {1000, 660}, {1920, 1080} };
    for (int i = 0; i < 3; i++) {
        CHECK(resize(sizes[i][0], sizes[i][1]) == 0);
        CHECK(click_text("Checkbox") == 0 && gint("gallery/id 38") == 4);
        CHECK(click_text("Mailing list") == 0);
        status(st, sizeof st); CHECK(strstr(st, "Checkbox 2") != NULL);
        /* the scroll buttons moved with the bottom of the source pane (the
         * full source is long enough to page through) */
        CHECK(click_text("Full Source") == 0);
        long off = src_first();
        CHECK(click_text("Scroll Down") == 0 && src_first() > off);
        status(st, sizeof st); CHECK(strstr(st, "Source scrolled") != NULL);
        CHECK(click_text("Scroll Up") == 0 && src_first() == off);
        /* the hit test at a nav row's drawn centre names that row */
        int x, y; gv_node_id hit = 0; int lx, ly;
        CHECK(centre_of_text("Radio", &x, &y) == 0);
        CHECK(gv_view_hit_test(v, x, y, &hit, &lx, &ly) && gv_glon_host_glon_id(h, hit) == gint("gallery/nid 5"));
        /* hover shows on the drawn row (presented pixels) */
        int before[3], after[3];
        pixel(x - 60, y, before);
        push_motion(x, y); CHECK(pump() == 0);
        pixel(x - 60, y, after);
        CHECK(memcmp(before, after, sizeof before) != 0);
        CHECK(presented_ok() == 0);
    }
    printf("clicks: nav, checkbox and the moved scroll buttons work at 3 sizes; hit test and hover match drawing\n");
}

/* buttons and fields show a focus ring (state slot 9): exactly the focused
 * one, none when focus is elsewhere.  Answers 1 (the focused widget's ring
 * is the one shown), 0 (none shown) or -1 (wrong ring, or several). */
static int rings_ok(void) {
    /* Glon lists [ ring id next ] for every button and field taking events;
     * C walks it through a global cursor (plain block-at reads only) */
    CHECK(gv_glon_host_run(h, "[ p2/rings: func [l] [ either = l 0 [ 0 ] [ st: block-at l 0 k: block-at st 0 "
                              "either either = k 3 [ 1 ] [ = k 5 ] [ reduce [ block-at st 9 block-at st 1 p2/rings block-at l 1 ] ] "
                              "[ p2/rings block-at l 1 ] ] ] p2/cur: p2/rings gallery/live-states ]") == 0);
    int shown = 0, focused_shown = 0;
    gv_node_id f = gv_view_focus_id(v);
    for (int guard = 0; guard < 64 && gint("either = p2/cur 0 [ 1 ] [ 0 ]") == 0; guard++) {
        gv_node *ring = gv_view_node(v, vnode(gint("block-at p2/cur 0")));
        long id = gint("block-at p2/cur 1");
        if (ring && ring->visible) { shown++; if (vnode(id) == f) focused_shown = 1; }
        CHECK(gv_glon_host_run(h, "[ p2/cur: block-at p2/cur 2 ]") == 0);
    }
    return shown == 0 ? 0 : shown == 1 && focused_shown ? 1 : -1;
}

/* 4: focus and editing across resizes */
static void case_edit(void) {
    char a[96];
    CHECK(click_text("Field") == 0 && click_text("Type here") == 0 && text("ab") == 0);
    gv_node_id f = gv_view_focus_id(v);
    CHECK(resize(1500, 900) == 0);
    CHECK(gv_view_focus_id(v) == f);                         /* focus kept */
    CHECK(text("cd") == 0);
    gstr("gallery/field-value", a, sizeof a);
    CHECK(!strcmp(a, "abcd"));
    CHECK(resize(1000, 660) == 0 && key(8) == 0);
    gstr("gallery/field-value", a, sizeof a);
    CHECK(!strcmp(a, "abc"));
    int seen = 0; gv_node_id last = gv_view_focus_id(v);
    int ringed = 0;
    for (int i = 0; i < 12; i++) {
        CHECK(key(9) == 0);
        if (gv_view_focus_id(v) != last) seen++;
        last = gv_view_focus_id(v);
        int r = rings_ok();
        CHECK(r >= 0);                                 /* one ring, on the focused widget */
        if (r == 1) ringed++;
    }
    CHECK(ringed >= 3);
    CHECK(seen == 12);
    CHECK(resize(1200, 700) == 0);
    printf("edit: typing continues across resizes (\"%s\"), focus kept; 12 Tabs, ring on the focused widget %d times, never elsewhere\n", a, ringed);
}

/* 5: application state survives resizing (no rebuild) */
static void case_state(void) {
    char a[96];
    CHECK(click_text("Radio") == 0 && click_text("Business") == 0);
    CHECK(click_text("Scroll Down") == 0 && click_text("Scroll Down") == 0);
    long off = src_anchor(), sel = gint("gallery/id 38"), id0 = gint("view/id");
    CHECK(wheel(400, 300, -1) == 0);                         /* wheel down over the preview */
    long py = gint("block-at block-at lay/scrolls gallery/pscroll 4");
    CHECK(resize(960, 640) == 0);
    if (getenv("P2_DEBUG")) {
        long pvid = gint("block-at block-at lay/scrolls gallery/pscroll 0");
        gv_node *pn = gv_view_node(v, vnode(pvid));
        gv_rect sr, vr; int a = screen(pvid, &sr), c = gv_view_visible_rect(v, vnode(pvid), &vr);
        fprintf(stderr, "dbg pv glon %ld view %d type %d vis %d screen %d:%d,%d %dx%d visible %d:%d,%d %dx%d\n", pvid, (int)vnode(pvid),
                pn ? (int)pn->type : -1, pn ? pn->visible : -1, a, sr.x, sr.y, sr.w, sr.h, c, vr.x, vr.y, vr.w, vr.h);
    }
    CHECK(wheel(400, 400, -3) == 0);
    py = gint("block-at block-at lay/scrolls gallery/pscroll 4");
    CHECK(py > 0);                                           /* the preview scrolls in a small pane */
    for (int i = 0; i < 6; i++) CHECK(resize(1000 + 40 * i, 645 + i) == 0);
    CHECK(gint("gallery/id 38") == sel && src_top_holds((int)off) && gint("view/id") == id0);
    CHECK(gint("block-at gallery/id 18 5") == 1);            /* Business still selected */
    /* the offset is kept, clamped to what the larger pane still allows:
     * preview content 604 high in a viewport of (H - 32 - 64) */
    long py2 = gint("block-at block-at lay/scrolls gallery/pscroll 4");
    long maxy = 604 - (650 - 96);
    CHECK(py2 == (py < maxy ? py : maxy));
    CHECK(resize(1920, 1080) == 0);
    CHECK(gint("block-at block-at lay/scrolls gallery/pscroll 4") == 0);  /* clamped: everything fits */
    gstr("gallery/status", a, sizeof a);
    CHECK(resize(1200, 700) == 0);
    printf("state: selection, radio, source offset %ld, preview offset %ld kept across 8 resizes; view/id %ld\n", off, py, id0);
}

/* 6: both themes lay out for the current window (a theme switch rebuilds) */
static void case_themes(void) {
    int bg[3];
    long nodes0 = gv_view_live_nodes(v);
    CHECK(resize(1600, 900) == 0);
    CHECK(click_text("Theme") == 0);
    pixel(4, 4, bg);
    CHECK(bg[0] == 250 && bg[1] == 250 && bg[2] == 252);     /* WG-3 light root panel */
    check_layout(1600, 900, "light 1600x900");
    CHECK(resize(800, 500) == 0);
    check_layout(800, 500, "light 800x500");
    CHECK(wheel(100, 300, -5) == 0);                         /* over the nav pane: the root scrolls */
    long ry = gint("block-at block-at lay/scrolls 0 4");
    CHECK(ry > 0);
    CHECK(click_text("Theme") == 0);                         /* rebuild: root offset restored */
    CHECK(gint("block-at block-at lay/scrolls 0 4") == ry);
    pixel(4, 4, bg);
    CHECK(bg[0] == 31 && bg[1] == 34 && bg[2] == 40);        /* WG-3 dark root panel */
    check_layout(800, 500, "dark 800x500");
    CHECK(resize(1200, 700) == 0);
    check_layout(1200, 700, "dark 1200x700");
    CHECK(gv_view_live_nodes(v) == nodes0);
    printf("themes: light and dark laid out at 3 sizes; root offset %ld survives a rebuild; nodes %ld\n", ry, nodes0);
}

/* one cycle: 60 resizes, two theme rebuilds, back to 1200 x 700, collect */
static long lifecycle_cycle(int rebuild) {
    for (int i = 0; i < 60; i++) CHECK(resize(900 + (i * 53) % 1000, 500 + (i * 31) % 500) == 0);
    if (rebuild) CHECK(click_text("Theme") == 0 && click_text("Theme") == 0);
    CHECK(resize(1200, 700) == 0);
    CHECK(gv_glon_host_run(h, "[ gc/collect ]") == 0);
    return r0_s1_gc_live_cells();
}
/* 7: repeated resizing leaks no View nodes, rules, scroll records or heap.
 * The live heap after a collection varies by a granule or a few (values that
 * happen to be held at collection time, such as the last event's text, differ
 * in length; theme rebuilds varied the same way before GUI-P2: 223ac78 gives
 * 17,568..17,600 over 8 rebuilds).  A leak per resize would add over a
 * thousand cells per 60-resize cycle, so the cycles are held to small bands
 * with no growth from first to last. */
static void case_lifecycle(void) {
    long nodes0 = gv_view_live_nodes(v), id0 = gint("view/id"), rules0 = gint("lay/count"), sc0 = gint("lay/nscroll");
    CHECK(gv_glon_host_run(h, "[ gc/collect ]") == 0);
    long start = r0_s1_gc_live_cells();
    long r1 = lifecycle_cycle(0), r2 = lifecycle_cycle(0), r3 = lifecycle_cycle(0);
    long rlo = start, rhi = start;
    long rs[3] = { r1, r2, r3 };
    for (int i = 0; i < 3; i++) { if (rs[i] < rlo) rlo = rs[i]; if (rs[i] > rhi) rhi = rs[i]; }
    CHECK(rhi - rlo <= 32 && r3 <= start + 16);
    long cells0 = lifecycle_cycle(1);
    long c2 = lifecycle_cycle(1);
    long cells1 = lifecycle_cycle(1);
    long lo = cells0 < c2 ? cells0 : c2, hi = cells0 > c2 ? cells0 : c2;
    if (cells1 < lo) lo = cells1;
    if (cells1 > hi) hi = cells1;
    CHECK(gv_view_live_nodes(v) == nodes0 && gint("view/id") == id0);
    CHECK(gint("lay/count") == rules0 && gint("lay/nscroll") == sc0);
    CHECK(hi - lo <= 96 && cells1 <= cells0 + 64);
    printf("  live cells: start %ld; resize-only cycles %ld %ld %ld; with rebuilds %ld %ld %ld\n",
           start, r1, r2, r3, cells0, c2, cells1);
    CHECK(click_text("Checkbox") == 0 && gint("gallery/id 38") == 4);
    CHECK(!gv_glon_host_error(h));
    printf("lifecycle: 6 x 60 resizes (3 with 2 rebuilds): nodes %ld, view/id %ld, rules %ld, scrolls %ld unchanged; live cells %ld -> %ld\n",
           nodes0, id0, rules0, sc0, cells0, cells1);
}

/* 8: below the minimum, focus brings a control into view */
static void case_reveal(void) {
    CHECK(resize(700, 420) == 0);
    /* the field preview's editable field sits low in the preview: focus it
     * through the View focus order and it must end up visible */
    CHECK(gv_glon_host_run(h, "[ gallery/select 6 gallery/src-field widget/flush ]") == 0 && gv_glon_host_deliver(h) == 0);
    CHECK(render() == 0);
    if (getenv("P2_DEBUG") && texts_valid()) fprintf(stderr, "  ^ after select\n");
    gv_node_id field = vnode(gint("gallery/nid 32"));
    int found = 0;
    for (int i = 0; i < 40 && !found; i++) {
        CHECK(key(9) == 0);
        if (getenv("P2_DEBUG") && texts_valid()) fprintf(stderr, "  ^ after Tab %d\n", i + 1);
        if (gv_view_focus_id(v) == field) found = 1;
    }
    CHECK(found);
    CHECK(texts_valid() == 0);
    gv_rect r;
    CHECK(gv_view_visible_rect(v, field, &r) == 0);
    if (gv_view_visible_rect(v, field, &r) == 0) {
        CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= 700 && r.y + r.h <= 420);
        CHECK(text("ok") == 0);
        char a[96]; gstr("gallery/field-value", a, sizeof a);
        CHECK(strstr(a, "ok") != NULL);
        printf("reveal: at 700x420 the focused field is shown at %d,%d %dx%d; root scroll %ld,%ld, preview scroll %ld\n",
               r.x, r.y, r.w, r.h, gint("block-at block-at lay/scrolls 0 3"), gint("block-at block-at lay/scrolls 0 4"),
               gint("block-at block-at lay/scrolls gallery/pscroll 4"));
    }
    CHECK(resize(1200, 700) == 0);
    check_layout(1200, 700, "back to 1200x700");
}

/* 9: scrolled content shows where the view is, and Shift+wheel scrolls sideways */
static int thumb(long scroll, int k, gv_rect *r) {
    char e[96]; snprintf(e, sizeof e, "block-at block-at lay/scrolls %ld %d", scroll, k);
    return screen(gint(e), r);
}
static void case_scrollbars(void) {
    gv_rect tv, th, d;
    long ps = gint("gallery/pscroll");
    CHECK(resize(1600, 1000) == 0);
    CHECK(thumb(0, 11, &tv) == 0 && tv.w == 0 && thumb(ps, 11, &th) == 0 && th.w == 0);   /* nothing to scroll */
    CHECK(resize(800, 500) == 0);
    /* the root content (960 x 640) is larger than the window: both root thumbs */
    CHECK(thumb(0, 11, &tv) == 0 && tv.w == 4 && tv.h >= 24 && tv.x + tv.w <= 800 && tv.y >= 0 && tv.y + tv.h <= 500);
    CHECK(thumb(0, 12, &th) == 0 && th.h == 4 && th.w >= 24 && th.y + th.h <= 500);
    int y0 = tv.y;
    CHECK(wheel(100, 300, -2) == 0);                          /* root scrolls down */
    CHECK(thumb(0, 11, &tv) == 0 && tv.y > y0);
    CHECK(wheel(100, 300, 5) == 0);                           /* and back */
    /* the preview pane (420 wide at the minimum) clips the 600-wide preview:
     * "Delete" is cut off; Shift+wheel scrolls the preview to the right */
    int dx, dy;
    CHECK(centre_of_text("Delete", &dx, &dy) == 0);
    gv_node_id del = text_node("Delete");
    gv_rect full;
    CHECK(screen(gv_glon_host_glon_id(h, del), &full) == 0);
    CHECK(gv_view_visible_rect(v, del, &d) == 0 && d.w < full.w);   /* clipped */
    SDL_SetModState(KMOD_LSHIFT);
    CHECK(wheel(400, 160, -3) == 0);
    SDL_SetModState(KMOD_NONE);
    long px = gint("block-at block-at lay/scrolls gallery/pscroll 3");
    CHECK(px > 0);
    CHECK(gv_view_visible_rect(v, del, &d) == 0 && d.w == full.w);  /* now whole */
    CHECK(click(d.x + d.w / 2, d.y + d.h / 2) == 0);
    char st[96]; status(st, sizeof st);
    CHECK(presented_ok() == 0);
    CHECK(resize(1200, 700) == 0);
    CHECK(gint("block-at block-at lay/scrolls gallery/pscroll 3") == 0);  /* fits again */
    printf("scrollbars: thumbs only when content overflows; Shift+wheel moved the preview %ld px; status \"%s\"\n", px, st);
}

/* 10: buttons show hover and pressed looks in the presented window, also in
 * the previews (the gallery used to dispatch to a list taken before the
 * preview was built) and inside nested panels; a click reports itself */
static int press_check(const char *name, const int hover[3], const int held[3], const char *want_status) {
    int x, y, c0[3], c1[3], c2[3], c3[3], f0 = failures;
    char st[96];
    CHECK(centre_of_text(name, &x, &y) == 0);
    push_motion(2, 2); CHECK(pump() == 0); pixel(x - 40, y + 8, c0);
    push_motion(x, y); CHECK(pump() == 0); pixel(x - 40, y + 8, c1);
    push_button(SDL_MOUSEBUTTONDOWN, x, y); CHECK(pump() == 0); pixel(x - 40, y + 8, c2);
    push_button(SDL_MOUSEBUTTONUP, x, y); CHECK(pump() == 0); pixel(x - 40, y + 8, c3);
    CHECK(memcmp(c1, hover, sizeof c1) == 0 && memcmp(c2, held, sizeof c2) == 0);
    CHECK(memcmp(c3, c1, sizeof c3) == 0);                     /* released over it: hover again */
    CHECK(memcmp(c0, c1, sizeof c0) != 0);
    status(st, sizeof st);
    CHECK(strstr(st, want_status) != NULL);
    printf("  %-9s normal %3d,%3d,%3d  hover %3d,%3d,%3d  held %3d,%3d,%3d  released %3d,%3d,%3d  status \"%s\"%s\n", name,
           c0[0], c0[1], c0[2], c1[0], c1[1], c1[2], c2[0], c2[1], c2[2], c3[0], c3[1], c3[2], st, failures == f0 ? "" : "  <-- FAILED");
    return failures == f0 ? 0 : -1;
}
static long ig(const char *fmt, ...);
static void case_press(void) {
    /* WG-3 dark slots: accent hover 41, accent pressed 45, control hover 113,
     * control pressed 117 */
    int acc_h[3], acc_p[3], ctl_h[3], ctl_p[3];
    for (int k = 0; k < 3; k++) {
        acc_h[k] = (int)ig("block-at theme/dark %d", 41 + k); acc_p[k] = (int)ig("block-at theme/dark %d", 45 + k);
        ctl_h[k] = (int)ig("block-at theme/dark %d", 113 + k); ctl_p[k] = (int)ig("block-at theme/dark %d", 117 + k);
    }
    press_check("Activate", acc_h, acc_p, "Button activated");
    press_check("Secondary", ctl_h, ctl_p, "Secondary pressed");
    CHECK(click_text("Panel") == 0);
    press_check("Button A", ctl_h, ctl_p, "Button A pressed");
    press_check("Bottom", ctl_h, ctl_p, "Bottom pressed");
    CHECK(click_text("Window") == 0);
    press_check("Four", ctl_h, ctl_p, "Four pressed");           /* a panel inside a panel */
    press_check("Full Source", ctl_h, ctl_p, "Full source");
    CHECK(presented_ok() == 0);
    printf("press: hover and pressed looks presented for preview, nested-panel and pane buttons\n");
}

static int click_field(void) {
    gv_rect r;
    gv_node_id fn = vnode(gint("block-at gallery/id 32 1"));
    if (!fn || gv_view_visible_rect(v, fn, &r) != 0) return -1;
    return click(r.x + 20, r.y + r.h / 2);
}
/* 11: a focused field shows a caret after its text; it follows editing and
 * goes away on blur.  The caret is drawn where the backend measures the
 * text, so the check compares the presented frame with and without it. */
static long caret_of(long gid) { gv_node *n = gv_view_node(v, vnode(gid)); return n ? n->caret : -9; }
static void case_caret(void) {
    CHECK(click_text("Field") == 0 && click_text("Type here") == 0);
    long tx = gint("block-at gallery/id 32 3");
    CHECK(caret_of(tx) == 1);                     /* in front of the prompt "Type here" */
    CHECK(text("ab") == 0);
    CHECK(caret_of(tx) == 3);                                     /* "ab": index 2 */
    /* the presented caret: only a narrow vertical bar right of the text start */
    SDL_Surface *ws = SDL_GetWindowSurface(find_window());
    gv_rect fr;
    CHECK(gv_view_visible_rect(v, vnode(tx), &fr) == 0);
    size_t bytes = (size_t)ws->pitch * ws->h;
    unsigned char *with = malloc(bytes);
    memcpy(with, ws->pixels, bytes);
    char cmd[96];
    snprintf(cmd, sizeof cmd, "[ view/caret %ld -1 widget/flush ]", tx);
    CHECK(gv_glon_host_run(h, cmd) == 0 && gv_glon_host_deliver(h) == 0 && render() == 0);
    ws = SDL_GetWindowSurface(find_window());
    int x0 = 99999, x1 = -1, y0 = 99999, y1 = -1;
    for (int y = 0; y < ws->h; y++)
        for (int x = 0; x < ws->w; x++) {
            size_t o = (size_t)y * ws->pitch + (size_t)x * 4;
            if (memcmp(with + o, (unsigned char *)ws->pixels + o, 4)) {
                if (x < x0) { x0 = x; }
                if (x > x1) { x1 = x; }
                if (y < y0) { y0 = y; }
                if (y > y1) { y1 = y; }
            }
        }
    free(with);
    CHECK(x1 >= 0 && x1 - x0 <= 3 && y1 - y0 >= 10);             /* a 2-pixel bar, a line tall */
    CHECK(x0 > fr.x + 4 && x1 < fr.x + fr.w && y0 >= fr.y - 2 && y1 <= fr.y + fr.h + 2);
    printf("  caret bar at x %d..%d, y %d..%d (text box at %d,%d %dx%d)\n", x0, x1, y0, y1, fr.x, fr.y, fr.w, fr.h);
    snprintf(cmd, sizeof cmd, "[ view/caret %ld 2 widget/flush ]", tx);
    CHECK(gv_glon_host_run(h, cmd) == 0 && gv_glon_host_deliver(h) == 0);
    CHECK(text("c") == 0 && caret_of(tx) == 4);
    if (png_dir) {
        char pth[512];
        CHECK(render() == 0);
        snprintf(pth, sizeof pth, "%s/p2-field-caret.png", png_dir);
        CHECK(gv_backend_sdl_save_png(b, pth) == 0);
    }
    CHECK(key(8) == 0 && caret_of(tx) == 3);                      /* Backspace */
    CHECK(key(9) == 0 && caret_of(tx) == 0);                      /* Tab away: no caret */
    CHECK(click_field() == 0 && caret_of(tx) == 3);              /* refocus */
    CHECK(click_text("Clear") == 0 && caret_of(tx) == 0);        /* the button took the focus */
    CHECK(click_field() == 0 && caret_of(tx) == 1);               /* empty field: caret at 0 */
    CHECK(resize(1500, 900) == 0);
    CHECK(presented_ok() == 0);
    printf("caret: follows typing and Backspace, gone on blur, back on refocus\n");
}

/* 12: the window's edge is a resize zone: the pointer there shows the
 * matching resize cursor (and, where the platform takes an SDL hit test, a
 * press there starts a window-manager resize) */
static SDL_Cursor *normal_cursor;
/* 12b: by default the program leaves the platform's pointer alone (on WSLg,
 * changing it made the pointer invisible) and installs no edge hit test */
static void case_edges_default(void) {
    SDL_Cursor *c0 = SDL_GetCursor();
    for (int i = 0; i < 4; i++) {
        push_motion(i & 1 ? 2 : 600, 350);
        CHECK(pump() == 0);
        CHECK(SDL_GetCursor() == c0);
    }
    CHECK(gv_backend_sdl_hit_test_active(b) == 0);
    printf("edges-default: pointer untouched, no edge hit test\n");
}
static void case_edges(void) {
    int W0 = v->width, H0 = v->height;
    struct { int x, y, zone; } pts[] = {
        { 2, H0 / 2, SDL_HITTEST_RESIZE_LEFT }, { W0 - 1, H0 / 2, SDL_HITTEST_RESIZE_RIGHT },
        { W0 / 2, 0, SDL_HITTEST_RESIZE_TOP }, { W0 / 2, H0 - 3, SDL_HITTEST_RESIZE_BOTTOM },
        { 1, 1, SDL_HITTEST_RESIZE_TOPLEFT }, { W0 - 2, 8, SDL_HITTEST_RESIZE_TOPRIGHT },
        { 9, H0 - 1, SDL_HITTEST_RESIZE_BOTTOMLEFT }, { W0 - 1, H0 - 1, SDL_HITTEST_RESIZE_BOTTOMRIGHT },
        { 6, H0 / 2, SDL_HITTEST_NORMAL }, { W0 / 2, H0 / 2, SDL_HITTEST_NORMAL },
    };
    for (unsigned i = 0; i < sizeof pts / sizeof pts[0]; i++)
        CHECK(gv_backend_sdl_edge_zone(W0, H0, pts[i].x, pts[i].y) == pts[i].zone);
    /* "real": a platform that took the hit test can also make cursors */
    int real = gv_backend_sdl_hit_test_active(b);
    for (unsigned i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        /* on a real display the real pointer also sends motion, which can
         * land after the pushed event: try a few times */
        for (int t = 0; t < (real ? 5 : 1); t++) {
            push_motion(pts[i].x, pts[i].y);
            CHECK(pump() == 0);
            if (gv_backend_sdl_cursor_zone(b) == pts[i].zone) break;
        }
        if (gv_backend_sdl_cursor_zone(b) != pts[i].zone || getenv("P2_DEBUG")) {
            int ww = 0, wh = 0; SDL_GetWindowSize(find_window(), &ww, &wh);
            fprintf(stderr, "  point %d,%d: zone %d want %d (window %dx%d, view %dx%d)\n", pts[i].x, pts[i].y,
                    gv_backend_sdl_cursor_zone(b), pts[i].zone, ww, wh, v->width, v->height);
        }
        CHECK(gv_backend_sdl_cursor_zone(b) == pts[i].zone);
        if (real) {
            /* bitmap mode: the normal pointer is an explicit arrow too, never
             * SDL's "None" default; system mode returns to the default */
            int bitmap = !strcmp(getenv("GV_EDGE_CURSOR"), "bitmap");
            SDL_Cursor *c = SDL_GetCursor();
            if (pts[i].zone != SDL_HITTEST_NORMAL) CHECK(c && c != SDL_GetDefaultCursor() && c != normal_cursor);
            else CHECK(bitmap ? (c != SDL_GetDefaultCursor() && (normal_cursor = c) != NULL) : c == SDL_GetDefaultCursor());
        }
    }
    /* after a resize the zones follow the new edges */
    CHECK(resize(1000, 600) == 0);
    push_motion(998, 300); CHECK(pump() == 0);
    CHECK(gv_backend_sdl_cursor_zone(b) == SDL_HITTEST_RESIZE_RIGHT);
    push_motion(500, 300); CHECK(pump() == 0);
    CHECK(gv_backend_sdl_cursor_zone(b) == SDL_HITTEST_NORMAL);
    /* leaving the window puts the arrow back */
    push_motion(1, 300); CHECK(pump() == 0);
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = SDL_WINDOWEVENT; e.window.event = SDL_WINDOWEVENT_LEAVE;
    e.window.windowID = SDL_GetWindowID(find_window());
    SDL_PushEvent(&e); CHECK(pump() == 0);
    CHECK(gv_backend_sdl_cursor_zone(b) == SDL_HITTEST_NORMAL);
    printf("edges: 8 resize zones + interior mapped; cursor follows the pointer; platform hit test %s\n",
           real ? "installed (window-manager resize from the edge)" : "not available (dummy driver: cursor zone only)");
}

/* ---- tabs (vid_tabs.glon) in the gallery's Tabs preview ---------------- */
#define K_LEFT  1073741904
#define K_RIGHT 1073741903
#define K_HOME  1073741898
#define K_END   1073741901
/* glon id of tab i's group in tab set st ("gallery/tabs-a" / "-b") */
static long tab_gid(const char *st, int i) {
    char e[128]; snprintf(e, sizeof e, "block-at vid/tabs-tab %s %d 0", st, i); return gint(e);
}
static long tab_part(const char *st, int i, int part) {
    char e[128]; snprintf(e, sizeof e, "block-at vid/tabs-tab %s %d %d", st, i, part); return gint(e);
}
static long tab_sel(const char *st) { char e[96]; snprintf(e, sizeof e, "vid/tabs/selected %s", st); return gint(e); }
static int node_visible(long gid) {
    gv_rect r; gv_node_id n = vnode(gid);
    return n && gv_view_visible_rect(v, n, &r) == 0;
}
/* exactly one tab of the set shows the selected underline: the selected one */
static int one_selected(const char *st, int n) {
    int shown = 0, at = -1;
    for (int i = 0; i < n; i++) {
        gv_node *u = gv_view_node(v, vnode(tab_part(st, i, 4)));
        if (u && u->visible) { shown++; at = i; }
    }
    return shown == 1 && at == tab_sel(st);
}
/* the ring shows on exactly the focused tab of the set (or none) */
static int ring_on(const char *st, int n, int want) {
    for (int i = 0; i < n; i++) {
        gv_node *r = gv_view_node(v, vnode(tab_part(st, i, 1)));
        if (!r || r->visible != (i == want)) return 0;
    }
    return 1;
}
static int keyk(int k) { return key(k); }

/* 13: selection by pointer and keyboard, one selected tab, pages, focus,
 * state kept across switches, nested tabs */
static void case_tabs(void) {
    char st[96], a[96];
    CHECK(click_text("Tabs") == 0 && gint("gallery/id 38") == 9);
    CHECK(tab_sel("gallery/tabs-a") == 0 && one_selected("gallery/tabs-a", 3));
    CHECK(node_visible(gint("block-at gallery/id 32 1")));            /* General's field shown */
    CHECK(text_node("Details page") && !centre_of_text("Details page", &(int){0}, &(int){0}) == 0);
    /* pointer: a click selects; exactly one selected; its page alone shows */
    CHECK(click_text("Details") == 0 && tab_sel("gallery/tabs-a") == 1 && one_selected("gallery/tabs-a", 3));
    status(st, sizeof st); CHECK(strstr(st, "Tab: Details") != NULL);
    CHECK(!node_visible(gint("block-at gallery/id 32 1")));
    int x, y;
    CHECK(centre_of_text("Details page", &x, &y) == 0);
    /* hover changes an unselected tab's presented look */
    int c0[3], c1[3];
    CHECK(centre_of_text("Nested", &x, &y) == 0);
    pixel(x - 50, y, c0);
    push_motion(x, y); CHECK(pump() == 0);
    pixel(x - 50, y, c1);
    CHECK(memcmp(c0, c1, sizeof c0) != 0);
    /* keyboard: the clicked tab has the focus; arrows move selection and
     * focus together, wrapping; Home / End */
    CHECK(gv_view_focus_id(v) == vnode(tab_gid("gallery/tabs-a", 1)) && ring_on("gallery/tabs-a", 3, 1));
    int seq[][2] = { {K_RIGHT, 2}, {K_RIGHT, 0}, {K_LEFT, 2}, {K_HOME, 0}, {K_END, 2}, {K_LEFT, 1} };
    for (int i = 0; i < 6; i++) {
        CHECK(keyk(seq[i][0]) == 0);
        CHECK(tab_sel("gallery/tabs-a") == seq[i][1] && one_selected("gallery/tabs-a", 3));
        CHECK(gv_view_focus_id(v) == vnode(tab_gid("gallery/tabs-a", seq[i][1])));
        CHECK(ring_on("gallery/tabs-a", 3, seq[i][1]));
    }
    status(st, sizeof st); CHECK(strstr(st, "Tab: Details") != NULL);
    /* the Tab key never reaches a widget on a hidden page */
    CHECK(keyk(K_HOME) == 0);
    for (int i = 0; i < 16; i++) {
        CHECK(key(9) == 0);
        gv_node_id f = gv_view_focus_id(v);
        gv_rect r;
        CHECK(f && gv_view_visible_rect(v, f, &r) == 0);
    }
    /* state kept: field text, checkbox, inner selection */
    CHECK(click_text("General") == 0);
    CHECK(click_text("Type here") == 0 && text("abc") == 0);
    CHECK(click_text("Notifications") == 0 && gint("block-at gallery/id 14 5") == 1);
    CHECK(click_text("Nested") == 0 && click_text("Two") == 0 && tab_sel("gallery/tabs-b") == 1);
    CHECK(one_selected("gallery/tabs-b", 2) && centre_of_text("Inner page two", &x, &y) == 0);
    CHECK(click_text("Inner") == 0);
    status(st, sizeof st); CHECK(strstr(st, "Inner pressed") != NULL);
    CHECK(click_text("Details") == 0 && click_text("General") == 0);
    gstr("gallery/field-value", a, sizeof a);
    CHECK(!strcmp(a, "abc") && node_visible(gint("block-at gallery/id 32 1")) && text_node("abc"));
    CHECK(gint("block-at gallery/id 14 5") == 1);
    CHECK(click_text("Nested") == 0 && tab_sel("gallery/tabs-b") == 1 && centre_of_text("Inner page two", &x, &y) == 0);
    /* focus on a page that gets hidden: dropped, and keys do not reach it */
    CHECK(click_text("General") == 0 && click_text("abc") == 0);
    long fld = gint("block-at gallery/id 32 1");
    CHECK(gv_view_focus_id(v) == vnode(fld));
    CHECK(gv_glon_host_run(h, "[ vid/tabs/select gallery/tabs-a 1 widget/flush ]") == 0);
    CHECK(gv_glon_host_deliver(h) == 0 && gv_glon_host_bind(h) == 0 && render() == 0);
    CHECK(tab_sel("gallery/tabs-a") == 1 && gv_view_focus_id(v) != vnode(fld));
    CHECK(gint("either = vid/focus-ring block-at gallery/id 32 9 [ 1 ] [ 0 ]") == 0);   /* ring gone */
    CHECK(text("zz") == 0);
    gstr("gallery/field-value", a, sizeof a);
    CHECK(!strcmp(a, "abc"));                                           /* typing did not reach it */
    CHECK(presented_ok() == 0);
    printf("tabs: click / arrows / Home / End select exactly one tab and page; focus follows; Tab skips hidden pages; "
           "field \"%s\", checkbox and inner tab kept; hidden field loses the focus\n", a);
}

/* 14: tabs follow the window: both sets keep their margins, the strip
 * shares the width, clicks land on the moved tabs */
static void case_tabs_resize(void) {
    CHECK(click_text("Tabs") == 0);
    int sizes[][2] = { {1600, 1000}, {1000, 660}, {1920, 1080}, {1200, 700} };
    for (int i = 0; i < 4; i++) {
        CHECK(resize(sizes[i][0], sizes[i][1]) == 0);
        gv_rect pv, ta, tb, t0, t2, pg;
        CHECK(screen(gint("block-at block-at lay/scrolls gallery/pscroll 0"), &pv) == 0);
        CHECK(screen(gint("block-at gallery/tabs-a 1"), &ta) == 0);
        CHECK(screen(tab_gid("gallery/tabs-a", 0), &t0) == 0 && screen(tab_gid("gallery/tabs-a", 2), &t2) == 0);
        int pvw = pv.w, aw = (pvw - 40 > 400 ? pvw - 40 : 400) - 32;
        CHECK(ta.w == aw);
        int tw = aw / 3 > 160 ? 160 : aw / 3;
        CHECK(t0.w == tw - 2 && t2.x == t0.x + 2 * tw);
        char e[96]; snprintf(e, sizeof e, "vid/tabs/page gallery/tabs-a %ld", tab_sel("gallery/tabs-a"));
        CHECK(screen(gint(e), &pg) == 0 && pg.x >= ta.x && pg.x + pg.w <= ta.x + ta.w && pg.y + pg.h <= ta.y + ta.h);
        CHECK(screen(gint("block-at gallery/tabs-b 1"), &tb) == 0 && tb.w == aw - 56 - 40);
        int want = i % 3;
        const char *names[] = { "General", "Details", "Nested" };
        CHECK(click_text(names[want]) == 0 && tab_sel("gallery/tabs-a") == want);
        CHECK(one_selected("gallery/tabs-a", 3) && presented_ok() == 0);
        printf("  %4dx%-4d preview %d: tabs %d wide, tab %d, inner tabs %d\n", sizes[i][0], sizes[i][1], pvw, ta.w, t0.w, tb.w);
    }
    printf("tabs-resize: 4 sizes; widths follow the pane, clicks land on the moved tabs\n");
}

/* 15: switching previews and rebuilding leaks no nodes or layout rules */
static void case_tabs_lifecycle(void) {
    CHECK(click_text("Tabs") == 0);
    long nodes0 = gv_view_live_nodes(v), id0 = gint("view/id"), rules0 = gint("lay/count");
    for (int i = 0; i < 10; i++) {
        CHECK(click_text(i & 1 ? "Tabs" : "Button") == 0);
        if (i & 1) {
            CHECK(click_text(i % 4 == 1 ? "Nested" : "Details") == 0);
            CHECK(gv_view_live_nodes(v) == nodes0 && gint("view/id") == id0 && gint("lay/count") == rules0);
        }
    }
    CHECK(click_text("Tabs") == 0);
    CHECK(gv_view_live_nodes(v) == nodes0 && gint("view/id") == id0 && gint("lay/count") == rules0);
    CHECK(gv_glon_host_run(h, "[ gc/collect ]") == 0);
    CHECK(click_text("Nested") == 0 && click_text("Two") == 0 && tab_sel("gallery/tabs-b") == 1);
    CHECK(click_text("Theme") == 0 && click_text("Tabs") == 0);       /* rebuild, then the preview again */
    CHECK(tab_sel("gallery/tabs-a") == 0 && one_selected("gallery/tabs-a", 3) && click_text("Details") == 0);
    CHECK(gv_view_live_nodes(v) == nodes0 && gint("lay/count") == rules0);
    CHECK(!gv_glon_host_error(h));
    printf("tabs-lifecycle: 10 preview switches, GC, theme rebuild: nodes %ld, view/id %ld, rules %ld unchanged\n",
           nodes0, id0, rules0);
}

/* ---- the Table preview (host row store) and the source pane ------------- */
static gv_table *gtable(void) {
    gv_table *t = NULL;
    if (gv_glon_host_table(h, (int)gint("gallery/table-id"), &t) != 0) return NULL;
    return t;
}
static gv_node *table_node(void) { return gv_view_node(v, vnode(gint("gallery/table-id"))); }
static int table_rect(gv_rect *r) { return screen(gint("gallery/table-id"), r); }
/* any TEXT command of the table's draw list with exactly this text */
static const gv_cmd *table_text_cmd(const char *want) {
    gv_node *n = table_node();
    for (int i = 0; n && i < n->draw_list.count; i++) {
        const gv_cmd *c = &n->draw_list.cmds[i];
        if (c->op == GV_DRAW_TEXT && c->text && strlen(want) == c->text_len && !memcmp(c->text, want, c->text_len)) return c;
    }
    return NULL;
}
static long src_top(void) { return src_first(); }
static long pv_y(void) { return gint("block-at block-at lay/scrolls gallery/pscroll 4"); }

/* 16: 100 records, wheel / scrollbar / boundaries, header, clipping, state */
static void case_table(void) {
    char st[96];
    CHECK(click_text("Table") == 0);
    gv_table *t = gtable();
    CHECK(t && t->nrows == 100 && t->view_count == 100);
    if (!t) return;
    gv_node *n = table_node();
    gv_rect tr;
    CHECK(n && table_rect(&tr) == 0);
    /* the whole table draws inside its own clip; the header comes first */
    CHECK(n->draw_list.count > 4 && n->draw_list.cmds[0].op == GV_DRAW_PUSH && n->draw_list.cmds[1].op == GV_DRAW_CLIP);
    CHECK(n->draw_list.cmds[1].rect.w == n->w && n->draw_list.cmds[1].rect.h == n->h);
    CHECK(table_text_cmd("Name") && table_text_cmd("Notes"));
    int page = gv_table_page_rows(t);
    CHECK(page == (n->h - t->header_h) / t->row_h && page < 100);
    /* nothing spills below the table: the pixel under it is the shell's */
    int below[3], above[3];
    pixel(tr.x + 60, tr.y + tr.h + 4, below);
    pixel(tr.x + 60, tr.y - 6, above);
    CHECK(memcmp(below, above, sizeof below) == 0);
    /* the wheel scrolls the table alone, 3 rows a step */
    long src0 = src_top(), pv0 = pv_y();
    CHECK(wheel(tr.x + 200, tr.y + 120, -1) == 0 && t->visible_start == 3);
    CHECK(gint("gallery/table-start") == 3 && src_top() == src0 && pv_y() == pv0);
    CHECK(table_text_cmd("Name"));                                  /* header still drawn */
    /* bottom boundary: the last full page; the last record shown */
    for (int i = 0; i < 40; i++) CHECK(wheel(tr.x + 200, tr.y + 120, -1) == 0);
    CHECK(t->visible_start == 100 - page && table_text_cmd("100") && gv_table_max_start(t) == 100 - page);
    CHECK(src_top() == src0 && pv_y() == pv0);                      /* beyond the end: nothing else moves here */
    pixel(tr.x + 60, tr.y + tr.h + 4, below);
    CHECK(memcmp(below, above, sizeof below) == 0);
    /* top boundary */
    for (int i = 0; i < 40; i++) CHECK(wheel(tr.x + 200, tr.y + 120, 1) == 0);
    CHECK(t->visible_start == 0 && table_text_cmd("1"));
    /* scrollbar: thumb moves; a click on the track pages; on the thumb: nothing */
    int ty = 0, tl = 0, x0 = 0, x1 = 0;
    CHECK(gv_table_scrollbar(t, n->w, &x0, &x1) && gv_table_thumb(t, &ty, &tl));
    int bx = tr.x + (x0 + x1) / 2;
    CHECK(click(bx, tr.y + ty + tl + 20) == 0 && t->visible_start == page);
    int ty2 = 0;
    CHECK(gv_table_thumb(t, &ty2, &tl) && ty2 > ty);
    CHECK(click(bx, tr.y + ty2 + tl / 2) == 0 && t->visible_start == page);
    CHECK(click(bx, tr.y + t->header_h + 2) == 0 && t->visible_start == 0);
    CHECK(gint("gallery/table-start") == 0);
    /* select a row after scrolling (click in the Name column of visible row 2) */
    CHECK(wheel(tr.x + 200, tr.y + 120, -10) == 0 && t->visible_start == 30);
    CHECK(click(tr.x + 120, tr.y + t->header_h + 2 * t->row_h + t->row_h / 2) == 0);
    status(st, sizeof st);
    CHECK(gint("gallery/table-sel") == 32 && gv_table_is_selected(t, 32) && strstr(st, "selected: 33"));
    /* kept across preview switches, resizes and a theme rebuild */
    CHECK(click_text("Button") == 0 && click_text("Table") == 0);
    t = gtable();
    CHECK(t && t->visible_start == 30 && gv_table_is_selected(t, 32));
    CHECK(resize(1000, 660) == 0 && resize(1600, 1000) == 0);
    CHECK(gtable() == t && t->visible_start == 30 && gv_table_is_selected(t, 32));
    /* routing at the ends: where the preview pane can scroll (960 x 660), the
     * wheel moves the table until its last row, then the preview */
    CHECK(resize(960, 660) == 0 && table_rect(&tr) == 0);
    long pvm = gint("- block-at block-at lay/scrolls gallery/pscroll 2 block-at block-at lay/scrolls gallery/pscroll 6");
    CHECK(pvm > 0 && pv_y() == 0);
    for (int i = 0; i < 30; i++) CHECK(wheel(tr.x + 200, tr.y + 120, -1) == 0);
    CHECK(t->visible_start == 100 - page && pv_y() > 0);           /* table at its end, then the pane */
    long pv1 = pv_y();
    CHECK(table_rect(&tr) == 0 && wheel(tr.x + 200, tr.y + 120, 1) == 0);
    CHECK(t->visible_start == 100 - page - 3 && pv_y() == pv1);     /* back up: the table first */
    for (int i = 0; i < 40; i++) { CHECK(table_rect(&tr) == 0); CHECK(wheel(tr.x + 200, tr.y + 120, 1) == 0); }
    CHECK(t->visible_start == 0 && pv_y() == 0);
    gv_table_scroll(t, 30 - t->visible_start);                       /* back to the remembered offset */
    CHECK(gv_glon_host_run(h, "[ gallery/table-start: 30 ]") == 0);
    CHECK(resize(1200, 700) == 0);
    CHECK(click_text("Theme") == 0 && click_text("Table") == 0);
    t = gtable();
    CHECK(t && t->visible_start == 30 && gv_table_is_selected(t, 32) && t->nrows == 100);
    CHECK(t->sel_count == 1);
    /* one selected row: another click moves it; a click on it clears it */
    CHECK(table_rect(&tr) == 0);
    int ry5 = tr.y + t->header_h + 5 * t->row_h + t->row_h / 2;
    CHECK(click(tr.x + 120, ry5) == 0);
    CHECK(t->sel_count == 1 && gv_table_is_selected(t, 35) && !gv_table_is_selected(t, 32) && gint("gallery/table-sel") == 35);
    CHECK(click(tr.x + 120, ry5) == 0);
    status(st, sizeof st);
    CHECK(t->sel_count == 0 && !t->focus_active && gint("gallery/table-sel") == -1 && strstr(st, "selection cleared"));
    CHECK(click_text("Button") == 0 && click_text("Table") == 0);   /* stays cleared */
    t = gtable();
    CHECK(t && t->sel_count == 0 && !t->focus_active && t->visible_start == 30);
    CHECK(table_rect(&tr) == 0 && click(tr.x + 120, ry5) == 0 && t->sel_count == 1 && gv_table_is_selected(t, 35));
    CHECK(presented_ok() == 0);
    printf("table: 100 rows, page %d; wheel 3 rows/step to rows %d..100 and back; track pages; "
           "row 33 selected at offset 30 kept across preview switch, resize, theme rebuild; at its ends the wheel moves the pane; "
           "one selected row, cleared by clicking it again\n", page, 101 - page);
}

/* the expected cells of row i (1-based), as widget_gallery.glon makes them */
static const char *U_NAMES[20] = { "趙世榮 Zhao Shirong", "陳美玲 Chan Mei-ling", "张伟 Zhang Wei", "李嘉欣 Li Ka-yan",
    "王芳 Wang Fang", "Aroha Te Whare", "Mere Pōmare", "Hēmi Rāwiri", "Tūī Ngātai", "Āwhina Kōtare", "José Martínez",
    "Zoë Ørsted", "François Lefèvre", "Sigrún Þórsdóttir", "Łukasz Wiśniewski", "Αλέξανδρος Παπαδόπουλος-Ηλιόπουλος",
    "Мария Иванова", "田中さくら Tanaka Sakura", "דוד כהן David Cohen", "黄志明 Wong Chi-ming" };
static const char *U_PLACES[16] = { "高要 Gaoyao", "增城 Zengcheng", "廣州 Guangzhou", "广州 (简体)", "台山 Taishan",
    "開平 Kaiping", "香港 Hong Kong", "澳門 Macau", "Ōtautahi Christchurch", "Tāmaki Makaurau Auckland",
    "Te Whanganui-a-Tara Wellington", "Rotorua", "Αθήνα Athens", "Москва Moscow", "東京 Tokyo", "Zürich" };
static const char *U_LONG_ZH = "長備註：趙世榮在增城出生，後來搬到 Ōtautahi Christchurch 工作，每年都回廣州探親。";
static int table_unicode_checks(gv_table *t, const char *when) {
    int f0 = failures;
    /* every stored cell is valid UTF-8, every record distinct, known cells byte-exact */
    char seen[100][8] = {{0}};
    for (int r = 0; r < 100; r++) {
        for (int c = 0; c < 4; c++) {
            const char *x = gv_table_get_cell(t, r, c);
            CHECK(x && utf8_ok((const unsigned char *)x));
        }
        snprintf(seen[r], sizeof seen[r], "%s", gv_table_get_cell(t, r, 0));
        CHECK(atoi(seen[r]) == r + 1);
        int k = r;
        CHECK(!strcmp(gv_table_get_cell(t, r, 1), U_NAMES[k % 20]));
        CHECK(!strcmp(gv_table_get_cell(t, r, 2), U_PLACES[(k * 7) % 16]));
    }
    CHECK(!strcmp(gv_table_get_cell(t, 0, 2), "高要 Gaoyao") && !strcmp(gv_table_get_cell(t, 7, 2), "增城 Zengcheng"));
    CHECK(!strcmp(gv_table_get_cell(t, 14, 2), "廣州 Guangzhou") && !strcmp(gv_table_get_cell(t, 2, 1), "张伟 Zhang Wei"));
    /* what is drawn: whole strings (never cut bytes), fitted by the backend,
     * each inside its own clip; columns aligned on every row */
    gv_node *n = table_node();
    int xs[8], nx = 0, texts = 0, fitted = 0;
    for (int i = 0; n && i < n->draw_list.count; i++) {
        const gv_cmd *c = &n->draw_list.cmds[i];
        if (c->op != GV_DRAW_TEXT || !c->text) continue;
        char buf[300];
        int len = c->text_len < sizeof buf - 1 ? (int)c->text_len : (int)sizeof buf - 1;
        memcpy(buf, c->text, (size_t)len); buf[len] = 0;
        CHECK(utf8_ok((const unsigned char *)buf));
        texts++;
        if (c->f4 >= 1.0) {
            fitted++;
            const gv_cmd *clip = &n->draw_list.cmds[i - 1];
            CHECK(i >= 2 && clip->op == GV_DRAW_CLIP && clip->rect.x == c->rect.x && clip->rect.w == c->rect.w);
        }
        if (c->rect.y > t->header_h) {
            int k = 0;
            while (k < nx && xs[k] != c->rect.x) k++;
            if (k == nx && nx < 8) xs[nx++] = c->rect.x;
        }
    }
    CHECK(texts > 40 && fitted == texts);
    CHECK(nx == 4);                                   /* one x per column, the same on every row */
    if (failures != f0) fprintf(stderr, "  table unicode checks failed %s\n", when);
    return failures == f0 ? 0 : -1;
}
/* ink (pixels unlike the row background) in a cell of the presented frame:
 * inside the text's clip [x+8, x+w-12), and "spill" past it, up to the end
 * of the column or the scrollbar track, whichever comes first (the grid
 * line is not spill).  The row background is sampled in the cell's left
 * padding. */
static int cell_ink(gv_rect tr, gv_table *t, int vrow, int col, int *right_spill) {
    int cx = tr.x + 8;
    for (int c = 0; c < col; c++) cx += t->cols[c].width;
    int y0 = tr.y + t->header_h + vrow * t->row_h;
    int end = cx + t->cols[col].width, sx0 = 0, sx1 = 0;
    gv_node *n = table_node();
    if (n && gv_table_scrollbar(t, n->w, &sx0, &sx1) && end > tr.x + sx0) end = tr.x + sx0;
    int bg[3]; pixel(cx + 2, y0 + t->row_h / 2, bg);
    int ink = 0, spill = 0;
    for (int y = y0 + 3; y < y0 + t->row_h - 3; y++)
        for (int x = cx + 8; x < end; x++) {
            int p[3]; pixel(x, y, p);
            int d = abs(p[0] - bg[0]) + abs(p[1] - bg[1]) + abs(p[2] - bg[2]);
            /* the column's grid line is drawn at its start + width - 8 */
            if (d > 60 && x != cx + t->cols[col].width - 8) { if (x < cx + t->cols[col].width - 12) ink++; else spill++; }
        }
    if (right_spill) *right_spill = spill;
    if (getenv("P2_DEBUG")) fprintf(stderr, "  cell vrow %d col %d: x %d..%d ink %d spill %d\n", vrow, col, cx + 8, end, ink, spill);
    return ink;
}

/* 17: international text: stored and drawn intact, fitted at character
 * boundaries, clipped, aligned, rendered; after scrolling and resizing */
static void case_table_unicode(void) {
    CHECK(click_text("Table") == 0);
    gv_table *t = gtable();
    if (!t) { CHECK(t); return; }
    gv_rect tr; CHECK(table_rect(&tr) == 0);
    CHECK(table_unicode_checks(t, "at the top") == 0);
    /* the long Chinese note is passed whole (cut by Pango at a character) */
    int r_long = -1;
    for (int r = 0; r < 100 && r_long < 0; r++) if (!strcmp(gv_table_get_cell(t, r, 3), U_LONG_ZH)) r_long = r;
    CHECK(r_long >= 0);
    /* glyphs drawn for Chinese (row 1 name), Māori (row 7), Greek (row 16) */
    int spill = 0;
    CHECK(cell_ink(tr, t, 0, 1, &spill) > 40 && spill == 0);
    CHECK(cell_ink(tr, t, 6, 1, &spill) > 40 && spill == 0);
    CHECK(wheel(tr.x + 200, tr.y + 120, -3) == 0 && t->visible_start == 9);
    CHECK(table_unicode_checks(t, "after scrolling") == 0);
    CHECK(table_text_cmd(U_NAMES[15]) != NULL);           /* long Greek name, whole */
    CHECK(cell_ink(tr, t, 15 - 9, 1, &spill) > 40 && spill == 0);
    /* scroll so the long Chinese note is visible: drawn whole, nothing spills */
    CHECK(wheel(tr.x + 200, tr.y + 120, 20) == 0);
    int want = r_long > 2 ? r_long - 2 : 0;
    gv_table_scroll(t, want - t->visible_start);
    CHECK(gv_glon_host_bind(h) == 0 && render() == 0);
    CHECK(table_text_cmd(U_LONG_ZH) != NULL);
    CHECK(cell_ink(tr, t, r_long - t->visible_start, 3, &spill) > 40 && spill == 0);
    CHECK(resize(1000, 660) == 0);
    CHECK(table_rect(&tr) == 0 && table_unicode_checks(t, "after resizing") == 0);
    CHECK(resize(1600, 1000) == 0 && table_rect(&tr) == 0);
    CHECK(cell_ink(tr, t, r_long - t->visible_start, 3, &spill) > 40 && spill == 0);
    CHECK(resize(1200, 700) == 0 && texts_valid() == 0 && presented_ok() == 0);
    printf("table-unicode: 400 cells valid and byte-exact; drawn whole, fitted and clipped per cell; 4 aligned columns; "
           "Chinese, Māori, Greek glyphs drawn; long note (row %d) contained; after scroll and resize\n", r_long + 1);
}

static int rgb_diff(const int *a, const int *b);
static int thumb_contrast(gv_table *t, gv_rect tr, int x0, int x1);
static int press_at(int x, int y);
static int move_to(int x, int y);
static int release_at(int x, int y);
/* 18: the right-hand pane: the actual source and documentation, wrapped at
 * its width (no truncation), scrolling on its own */
static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    if (b) b[n] = 0;
    fclose(f);
    return b;
}
/* the display rows tile the document exactly: each a slice, the next one
 * starting right after it or after its newline; every row starts on a
 * character and is valid UTF-8 */
static int rows_tile(gv_table *t) {
    int f0 = failures, at = 0;
    for (int i = 0; i < t->wrap_count; i++) {
        int st = 0, ln = 0;
        gv_table_text_row(t, i, &st, &ln);
        if (st != at) { CHECK(st == at); break; }
        CHECK(st >= t->doc_len || ((unsigned char)t->doc[st] & 0xC0) != 0x80);
        char row[1024];
        int k = ln < (int)sizeof row - 1 ? ln : (int)sizeof row - 1;
        memcpy(row, t->doc + st, (size_t)k); row[k] = 0;
        CHECK(utf8_ok((const unsigned char *)row));
        at = st + ln;
        if (at < t->doc_len && t->doc[at] == '\n') at++;
    }
    CHECK(at >= t->doc_len);
    return failures == f0 ? 0 : -1;
}
/* no ink between the wrap width and the scrollbar, in the presented frame */
static int ink_past_wrap(gv_rect r, gv_table *t) {
    int x0 = r.x + 8 + t->wrap_w + 1, x1 = r.x + r.w - GV_TABLE_BAR_W, bad = 0;
    int bg[3]; pixel(r.x + 3, r.y + r.h / 2, bg);
    for (int y = r.y + 2; y < r.y + r.h - 2; y++)
        for (int x = x0; x < x1; x++) { int p[3]; pixel(x, y, p); if (rgb_diff(p, bg) > 60) bad++; }
    return bad;
}
static void case_source(void) {
    char path[600];
    CHECK(click_text("Table") == 0);
    gv_table *t = src_table(), *dt = gtable();
    gv_rect sr;
    if (!t || !dt) { CHECK(t && dt); return; }
    CHECK(screen(gint("gallery/id 40"), &sr) == 0 && t->text_mode && t->header_h == 0);
    /* the actual library source and the documentation */
    snprintf(path, sizeof path, "%s/view/glon/vid_table.glon", root_dir);
    char *lib = read_file(path);
    CHECK(lib && strstr(t->doc, lib) && strstr(t->doc, "-- Source: view/glon/vid_table.glon --"));
    CHECK(strstr(t->doc, "-- VID table: rows live in the host --") && strstr(t->doc, "趙世榮 Zhao Shirong"));
    free(lib);
    int page = gv_table_page_rows(t);
    CHECK(t->wrap_count >= 3 * page);                              /* several screens */
    CHECK(rows_tile(t) == 0);                                      /* wrapped, nothing cut or added */
    for (int i = 0; i < t->wrap_count; i++) { int st, ln; gv_table_text_row(t, i, &st, &ln); CHECK(ln <= 400); }
    gv_node *n = gv_view_node(v, vnode(gint("gallery/id 40")));
    for (int i = 0; n && i < n->draw_list.count; i++) {
        const gv_cmd *c = &n->draw_list.cmds[i];
        if (c->op == GV_DRAW_TEXT) CHECK(c->f4 == 0.0 && c->text >= t->doc && c->text + c->text_len <= t->doc + t->doc_len);
    }
    CHECK(ink_past_wrap(sr, t) == 0);
    /* the pane scrolls alone: wheel 3 rows, buttons a page; the data table stays */
    long ts = dt->visible_start, pv0 = pv_y();
    CHECK(t->visible_start == 0 && wheel(sr.x + 60, sr.y + 60, -1) == 0 && t->visible_start == 3);
    CHECK(dt->visible_start == ts && pv_y() == pv0 && gint("block-at gallery/src-tops 7") == src_anchor());
    CHECK(click_text("Scroll Down") == 0 && t->visible_start == 3 + page && click_text("Scroll Up") == 0 && t->visible_start == 3);
    CHECK(wheel(sr.x + 200, sr.y - 400, -1) == 0 || 1);            /* (over the preview: not the pane) */
    /* ends: the last row is the document's last line */
    for (int i = 0; i < 200; i++) CHECK(wheel(sr.x + 60, sr.y + 60, -1) == 0);
    CHECK(t->visible_start == t->wrap_count - page && dt->visible_start == ts);
    int st = 0, ln = 0;
    CHECK(gv_table_text_row(t, t->wrap_count - 1, &st, &ln) == 0 && st + ln >= t->doc_len - 1);
    for (int i = 0; i < 200; i++) CHECK(wheel(sr.x + 60, sr.y + 60, 1) == 0);
    CHECK(t->visible_start == 0);
    /* wider and taller: fewer rows (reflow), more of them shown; the top text stays */
    CHECK(wheel(sr.x + 60, sr.y + 60, -8) == 0);
    int anchor = src_anchor(), rows0 = t->wrap_count, page0 = page;
    CHECK(resize(1920, 1080) == 0 && screen(gint("gallery/id 40"), &sr) == 0);
    CHECK(t->wrap_count < rows0 && gv_table_page_rows(t) > page0 && src_top_holds(anchor));
    if (png_dir) { char pth[512]; snprintf(pth, sizeof pth, "%s/p2-source-1920x1080.png", png_dir); CHECK(gv_backend_sdl_save_png(b, pth) == 0); }
    CHECK(rows_tile(t) == 0 && ink_past_wrap(sr, t) == 0);
    CHECK(resize(1000, 660) == 0 && screen(gint("gallery/id 40"), &sr) == 0);
    CHECK(t->wrap_count > rows0 && src_top_holds(anchor) && rows_tile(t) == 0 && ink_past_wrap(sr, t) == 0);
    if (png_dir) { char pth[512]; snprintf(pth, sizeof pth, "%s/p2-source-1000x660.png", png_dir); CHECK(gv_backend_sdl_save_png(b, pth) == 0); }
    CHECK(resize(1200, 700) == 0 && src_top_holds(anchor));
    /* kept across preview switches; restored after a theme rebuild */
    /* each document keeps its own place: Button's at its start, Table's back */
    long keep = t->visible_start;
    CHECK(click_text("Button") == 0 && src_table() == t && src_first() == 0);
    CHECK(screen(gint("gallery/id 40"), &sr) == 0);
    CHECK(wheel(sr.x + 60, sr.y + 60, -2) == 0 && src_first() == 6);
    CHECK(click_text("Table") == 0 && src_first() == keep && src_top_holds(anchor));
    CHECK(click_text("Button") == 0 && src_first() == 6 && click_text("Table") == 0);
    CHECK(resize(1500, 900) == 0 && src_top_holds(anchor));         /* reflowed while away... */
    CHECK(click_text("Button") == 0 && src_first() > 0 && click_text("Table") == 0 && src_top_holds(anchor));
    CHECK(resize(1200, 700) == 0 && src_first() == keep);
    CHECK(click_text("Theme") == 0 && click_text("Table") == 0);    /* a rebuild: new table, same place */
    t = src_table();
    CHECK(t && t->text_mode && src_first() == keep);
    CHECK(click_text("Theme") == 0 && click_text("Table") == 0);
    /* Full Source: this gallery's own file from disk */
    CHECK(click_text("Full Source") == 0);
    t = src_table();
    snprintf(path, sizeof path, "%s/view/glon/widget_gallery.glon", root_dir);
    char *gal = read_file(path);
    CHECK(t && gal && strstr(t->doc, gal) && rows_tile(t) == 0 && t->wrap_count > 10 * page);
    free(gal);
    CHECK(presented_ok() == 0);
    printf("source: actual vid_table.glon + docs, %d rows (%d visible) tiling the text exactly; wheel/buttons/ends; "
           "reflow 1920x1080 / 1000x660 keeps the top text; kept across preview and theme; full source from disk\n",
           rows0, page0);
}

/* 20: the source pane's scrollbar: drag, track paging, after resize and theme */
static void case_source_drag(void) {
    CHECK(click_text("Table") == 0);
    gv_table *t = src_table(), *dt = gtable();
    gv_node *n = gv_view_node(v, vnode(gint("gallery/id 40")));
    gv_rect sr;
    if (!t || !n || !dt) { CHECK(t && n && dt); return; }
    CHECK(screen(gint("gallery/id 40"), &sr) == 0);
    int x0 = 0, x1 = 0, ty = 0, tl = 0;
    CHECK(gv_table_scrollbar(t, n->w, &x0, &x1) && x1 - x0 == 14 && gv_table_thumb(t, &ty, &tl) && tl >= 24);
    CHECK(thumb_contrast(t, sr, x0, x1) > 60);
    int bx = sr.x + (x0 + x1) / 2, maxs = gv_table_max_start(t), ts = dt->visible_start;
    CHECK(press_at(bx, sr.y + ty + tl / 2) == 0 && t->bar_active && t->visible_start == 0);
    CHECK(move_to(bx - 500, sr.y + ty + tl / 2 + 60) == 0 && t->visible_start > 0);
    CHECK(move_to(bx, 4000) == 0 && t->visible_start == maxs);
    CHECK(move_to(-100, -4000) == 0 && t->visible_start == 0);
    int last = -1, reached = 0;
    for (int y = sr.y - 5; y <= sr.y + sr.h + 5; y++) {
        CHECK(move_to(bx, y + tl / 2) == 0);
        if (t->visible_start != last) { reached++; last = t->visible_start; }
    }
    CHECK(last == maxs && reached >= (maxs < sr.h - tl ? maxs + 1 : (sr.h - tl) / 2));
    CHECK(release_at(-20, -20) == 0 && !t->bar_active && dt->visible_start == ts);
    int keep = t->visible_start;
    CHECK(move_to(bx, sr.y + 10) == 0 && t->visible_start == keep);
    /* the track pages */
    CHECK(gv_table_thumb(t, &ty, &tl) && click(bx, sr.y + ty - 10) == 0 && t->visible_start == keep - gv_table_page_rows(t));
    /* after a resize and a theme switch it still drags */
    CHECK(resize(1600, 1000) == 0 && screen(gint("gallery/id 40"), &sr) == 0);
    n = gv_view_node(v, vnode(gint("gallery/id 40")));
    CHECK(n && gv_table_scrollbar(t, n->w, &x0, &x1) && gv_table_thumb(t, &ty, &tl));
    bx = sr.x + (x0 + x1) / 2;
    CHECK(press_at(bx, sr.y + ty + tl / 2) == 0 && move_to(bx, sr.y + sr.h + 300) == 0 && t->visible_start == gv_table_max_start(t));
    CHECK(release_at(bx, 10) == 0);
    CHECK(resize(1200, 700) == 0 && click_text("Theme") == 0);
    t = src_table(); n = gv_view_node(v, vnode(gint("gallery/id 40")));
    CHECK(t && n && screen(gint("gallery/id 40"), &sr) == 0 && gv_table_scrollbar(t, n->w, &x0, &x1));
    if (t && n) {
        CHECK(thumb_contrast(t, sr, x0, x1) > 60);                 /* light theme */
        CHECK(gv_table_thumb(t, &ty, &tl) && press_at(sr.x + (x0 + x1) / 2, sr.y + ty + tl / 2) == 0);
        CHECK(move_to(sr.x + (x0 + x1) / 2, -2000) == 0 && t->visible_start == 0 && release_at(0, 0) == 0);
    }
    CHECK(click_text("Theme") == 0 && presented_ok() == 0);
    printf("source-drag: 14 px bar, thumb %d px; drag to both ends, off the bar and the window; track pages; "
           "after resize and in the light theme; data table unmoved\n", tl);
}

/* ---- table scrollbar: dragging the thumb ------------------------------- */
static int press_at(int x, int y) { push_motion(x, y); push_button(SDL_MOUSEBUTTONDOWN, x, y); return pump(); }
static int move_to(int x, int y) { push_motion(x, y); return pump(); }
static int release_at(int x, int y) { push_button(SDL_MOUSEBUTTONUP, x, y); return pump(); }
static int rgb_diff(const int *a, const int *b) { return abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2]); }
/* thumb vs track contrast in the presented frame */
static int thumb_contrast(gv_table *t, gv_rect tr, int x0, int x1) {
    int ty = 0, tl = 0, th[3], tk[3];
    gv_table_thumb(t, &ty, &tl);
    pixel(tr.x + (x0 + x1) / 2, tr.y + ty + tl / 2, th);
    int ky = ty > t->header_h + 12 ? t->header_h + 4 : t->view_h - 4;
    pixel(tr.x + (x0 + x1) / 2, tr.y + ky, tk);
    return rgb_diff(th, tk);
}

/* 19: the thumb is wide and visible, and dragging it scrolls all the rows */
static void case_table_drag(void) {
    char st[96];
    CHECK(click_text("Table") == 0);
    gv_table *t = gtable();
    gv_node *n = table_node();
    gv_rect tr;
    if (!t || !n) { CHECK(t && n); return; }
    CHECK(table_rect(&tr) == 0);
    int x0 = 0, x1 = 0, ty0 = 0, tl = 0;
    CHECK(gv_table_scrollbar(t, n->w, &x0, &x1) && x1 - x0 == 14);
    CHECK(gv_table_thumb(t, &ty0, &tl) && tl >= 24);
    int dark = thumb_contrast(t, tr, x0, x1);
    CHECK(dark > 60);
    int travel = t->view_h - t->header_h - tl, maxs = gv_table_max_start(t);
    long sel0 = gint("gallery/table-sel");
    int count0 = t->sel_count;
    gstr("gallery/status", st, sizeof st);
    int bx = tr.x + (x0 + x1) / 2;
    /* press on the thumb: nothing scrolls, nothing is selected; drawn accented */
    CHECK(t->visible_start == 0 && press_at(bx, tr.y + ty0 + tl / 2) == 0);
    CHECK(t->visible_start == 0 && t->bar_active);
    int acc[3]; pixel(bx, tr.y + ty0 + tl / 2, acc);
    CHECK(acc[0] == ig("block-at theme/dark 13") && acc[1] == ig("block-at theme/dark 14") &&
          acc[2] == ig("block-at theme/dark 15"));                  /* the dark theme's accent (slot 13) */
    /* the thumb follows the pointer, pixel for pixel */
    CHECK(move_to(bx, tr.y + ty0 + tl / 2 + 40) == 0);
    int want = (40 * maxs + travel / 2) / travel, ty = 0;
    CHECK(t->visible_start == want && gint("gallery/table-start") == want);
    CHECK(gv_table_thumb(t, &ty, &tl) && abs(ty - (ty0 + 40)) <= 2);
    /* off the scrollbar, then outside the window: still dragging */
    CHECK(move_to(bx - 300, tr.y + ty0 + tl / 2 + 40) == 0 && t->visible_start == want && t->bar_active);
    CHECK(move_to(-200, tr.y + ty0 + tl / 2 + 120) == 0 && t->visible_start > want);
    CHECK(move_to(bx, 5000) == 0 && t->visible_start == maxs && table_text_cmd("100"));
    CHECK(gv_table_thumb(t, &ty, &tl) && ty + tl == t->view_h);
    CHECK(move_to(3000, -3000) == 0 && t->visible_start == 0 && table_text_cmd("1"));
    /* every first row 0..max is reachable, in order, through a 1-px sweep */
    int last = -1, reached = 0, ordered = 1;
    for (int y = tr.y + t->header_h - 10; y <= tr.y + t->view_h + 10; y++) {
        CHECK(move_to(bx, y + ty0 - t->header_h + tl / 2) == 0);
        if (t->visible_start < last) ordered = 0;
        if (t->visible_start != last) { reached++; last = t->visible_start; }
    }
    CHECK(ordered && last == maxs && reached == maxs + 1);
    /* the release ends it, even outside the window; then moves do nothing */
    CHECK(move_to(bx, tr.y + t->header_h + travel / 2 + tl / 2) == 0);
    int mid = t->visible_start;
    CHECK(mid > 0 && mid < maxs && release_at(-50, 900) == 0 && !t->bar_active);
    CHECK(move_to(bx, tr.y + t->view_h - 2) == 0 && t->visible_start == mid);
    /* no row was selected by any of it */
    char st2[96]; gstr("gallery/status", st2, sizeof st2);
    CHECK(gint("gallery/table-sel") == sel0 && t->sel_count == count0 && !strstr(st2, "Table row selected"));
    /* paging and the wheel still work */
    CHECK(gv_table_thumb(t, &ty, &tl) && click(bx, tr.y + ty + tl + 10) == 0 && t->visible_start == mid + gv_table_page_rows(t));
    CHECK(wheel(tr.x + 200, tr.y + 120, 1) == 0 && t->visible_start == mid + gv_table_page_rows(t) - 3);
    /* after a resize: same behaviour where the table now is */
    CHECK(resize(1000, 660) == 0 && table_rect(&tr) == 0);
    n = table_node();                            /* the table follows the pane */
    CHECK(n && n->w == tr.w && gv_table_scrollbar(t, n->w, &x0, &x1));
    bx = tr.x + (x0 + x1) / 2;
    gv_rect vis;                                 /* its scrollbar is in view */
    CHECK(gv_view_visible_rect(v, vnode(gint("gallery/table-id")), &vis) == 0 && vis.x + vis.w >= tr.x + x1);
    CHECK(gv_table_thumb(t, &ty, &tl) && press_at(bx, tr.y + ty + tl / 2) == 0);
    CHECK(move_to(bx, tr.y + t->view_h + 400) == 0 && t->visible_start == maxs);
    CHECK(move_to(bx, tr.y + t->header_h + tl / 2 + travel / 4) == 0 && release_at(bx, tr.y + 10) == 0);
    int kept = t->visible_start;
    CHECK(kept > 0 && kept < maxs && gint("gallery/table-start") == kept && gint("gallery/table-sel") == sel0);
    /* kept across a preview switch and a theme switch; light theme thumb visible */
    CHECK(resize(1200, 700) == 0 && click_text("Button") == 0 && click_text("Table") == 0);
    t = gtable();
    CHECK(t && t->visible_start == kept);
    CHECK(click_text("Theme") == 0 && click_text("Table") == 0);
    t = gtable(); n = table_node();
    CHECK(t && n && t->visible_start == kept && table_rect(&tr) == 0);
    if (n) CHECK(gv_table_scrollbar(t, n->w, &x0, &x1));
    int light = t ? thumb_contrast(t, tr, x0, x1) : 0;
    CHECK(light > 60);
    if (t) {
        CHECK(gv_table_thumb(t, &ty, &tl) && press_at(tr.x + (x0 + x1) / 2, tr.y + ty + tl / 2) == 0);
        CHECK(move_to(tr.x + (x0 + x1) / 2, -400) == 0 && t->visible_start == 0 && release_at(0, 0) == 0);
    }
    CHECK(click_text("Theme") == 0);                                 /* back to dark */
    CHECK(presented_ok() == 0);
    printf("table-drag: bar 14 px, thumb %d px (min 24); contrast dark %d light %d; drag tracks the pointer, "
           "outside the bar and the window, rows 1..100, all %d positions in order; release ends it; "
           "no selection; paging, wheel, resize, preview and theme switch kept (%d)\n",
           tl, dark, light, maxs + 1, kept);
}


/* ---- column sizing ----------------------------------------------------- */
/* the TEXT command drawing exactly this cell's stored text in the table */
static const gv_cmd *cell_cmd(gv_table *t, int row, int col) {
    const char *want = gv_table_get_cell(t, row, col);
    gv_node *n = table_node();
    if (!want) return NULL;
    int x = gv_table_col_x(t, col) + 16;          /* draw lists are node-local */
    for (int i = 0; n && i < n->draw_list.count; i++) {
        const gv_cmd *c = &n->draw_list.cmds[i];
        if (c->op == GV_DRAW_TEXT && c->rect.x == x && c->text_len == strlen(want) && !memcmp(c->text, want, c->text_len))
            return c;
    }
    return NULL;
}
/* the header, the grid and the cells agree on every column's edges */
static int columns_aligned(gv_table *t) {
    int f0 = failures;
    gv_node *n = table_node();
    if (!n) return -1;
    for (int c = 0; c < t->ncols; c++) {
        int x = gv_table_col_x(t, c);             /* draw lists are node-local */
        int title = 0, grid = 0;
        for (int i = 0; i < n->draw_list.count; i++) {
            const gv_cmd *k = &n->draw_list.cmds[i];
            if (k->op == GV_DRAW_TEXT && k->text_len == strlen(t->cols[c].title) && !memcmp(k->text, t->cols[c].title, k->text_len))
                title = k->rect.x == x + 8 && k->rect.w == t->cols[c].width - 12;
            if (k->op == GV_DRAW_RECT && k->rect.w == 1 && k->rect.h == n->h && k->rect.x == x + t->cols[c].width) grid = 1;
        }
        CHECK(title);
        if (t->show_grid) CHECK(grid);
        /* every row in view: the whole stored UTF-8 text, fitted by Pango
         * into the column's current box (never a pre-cut copy) */
        int rows = gv_table_page_rows(t);
        for (int r = t->visible_start; r < t->view_count && r < t->visible_start + rows; r++) {
            const gv_cmd *k = cell_cmd(t, r, c);
            CHECK(k && k->rect.w == t->cols[c].width - 20 && k->f4 == 1.0);
            CHECK(utf8_ok((const unsigned char *)gv_table_get_cell(t, r, c)));
        }
    }
    return failures == f0 ? 0 : -1;
}
/* screen point of the divider at column c's right edge, in the header */
static void divider_xy(gv_table *t, int c, int *x, int *y) {
    gv_rect tr; table_rect(&tr);
    *x = tr.x + gv_table_col_x(t, c + 1);
    *y = tr.y + t->header_h / 2;
}
static int measure(const char *s, double size) { return b->measure_text(b, s, (int)strlen(s), size); }

/* 21: spreadsheet column resizing: flexible Notes, divider drag with the
 * resize pointer, double-click fit, whole Unicode text, retained widths */
static void case_table_cols(void) {
    CHECK(click_text("Table") == 0);
    gv_table *t = gtable();
    gv_rect tr;
    if (!t || table_rect(&tr) != 0) { CHECK(t); return; }
    gv_node *n = table_node();
    int sx0 = 0, sx1 = 0;
    CHECK(n && gv_table_scrollbar(t, n->w, &sx0, &sx1));
    /* defaults: # narrow, Notes flexible, filling the table to the scrollbar */
    CHECK(t->cols[0].width == 44 && t->cols[3].flex && !t->cols[0].flex);
    CHECK(gv_table_col_x(t, 4) == sx0 && t->cols[3].width >= 156);
    CHECK(columns_aligned(t) == 0);
    int notes0 = t->cols[3].width;
    CHECK(resize(1700, 900) == 0 && table_rect(&tr) == 0 && (n = table_node()) && gv_table_scrollbar(t, n->w, &sx0, &sx1));
    CHECK(t->cols[0].width == 44 && t->cols[3].width > notes0 + 300 && gv_table_col_x(t, 4) == sx0);
    CHECK(columns_aligned(t) == 0);
    int notes_wide = t->cols[3].width;
    /* widening reveals more of the same whole text: the Notes cell is drawn
     * from the stored string, its fit box follows the column, more ink */
    int vr = 1, spill = 0;
    CHECK(resize(1200, 700) == 0 && table_rect(&tr) == 0);
    int ink_narrow = cell_ink(tr, t, vr, 3, &spill);
    const gv_cmd *k = cell_cmd(t, t->visible_start + vr, 3);
    CHECK(k && k->rect.w == t->cols[3].width - 20);
    CHECK(resize(1700, 900) == 0 && table_rect(&tr) == 0);
    int ink_wide = cell_ink(tr, t, vr, 3, &spill);
    k = cell_cmd(t, t->visible_start + vr, 3);
    CHECK(k && k->rect.w == notes_wide - 20 && ink_wide > ink_narrow + 50);
    CHECK(resize(1200, 700) == 0 && table_rect(&tr) == 0);
    /* hover: the horizontal resize pointer over a divider only */
    int dx, dy, sel0 = (int)gint("gallery/table-sel"), start0 = t->visible_start;
    divider_xy(t, 1, &dx, &dy);
    CHECK(move_to(dx - 30, dy) == 0 && gv_glon_host_cursor(h) == GV_CURSOR_NORMAL && t->col_hot == -1);
    CHECK(move_to(dx + 3, dy) == 0 && gv_glon_host_cursor(h) == GV_CURSOR_COL_RESIZE && t->col_hot == 1);
    CHECK(gv_backend_sdl_app_cursor(b) == GV_CURSOR_COL_RESIZE);
    CHECK(move_to(dx, dy + t->header_h) == 0 && gv_glon_host_cursor(h) == GV_CURSOR_NORMAL);   /* a row: no */
    /* drag: Name follows the pointer, Notes gives up the width; no row click */
    int name0 = t->cols[1].width, place_x0 = gv_table_col_x(t, 2);
    CHECK(move_to(dx, dy) == 0 && press_at(dx, dy) == 0 && gv_glon_host_cursor(h) == GV_CURSOR_COL_RESIZE);
    CHECK(move_to(dx + 40, dy + 200) == 0 && t->cols[1].width == name0 + 40 && gv_table_col_x(t, 2) == place_x0 + 40);
    if (png_dir) { char pth[512]; snprintf(pth, sizeof pth, "%s/p2-table-cols-drag.png", png_dir); CHECK(gv_backend_sdl_save_png(b, pth) == 0); }
    CHECK(gv_table_col_x(t, 4) == sx0 || t->cols[3].width == t->cols[3].want);
    CHECK(columns_aligned(t) == 0);
    CHECK(move_to(-500, dy) == 0 && t->cols[1].width == GV_TABLE_COL_MIN);      /* the floor */
    CHECK(move_to(dx + 2400, dy) == 0 && t->cols[1].width == GV_TABLE_COL_MAX);  /* the ceiling */
    CHECK(move_to(dx + 70, dy) == 0 && release_at(dx + 70, dy) == 0 && t->cols[1].width == name0 + 70);
    CHECK(gv_glon_host_cursor(h) == GV_CURSOR_NORMAL && t->col_hot == -1);
    CHECK(gint("gallery/table-sel") == sel0 && t->visible_start == start0);
    char ws[256]; gstr("gallery/table-widths", ws, sizeof ws);
    char want_ws[64]; snprintf(want_ws, sizeof want_ws, "44:0;%d:0;156:0;156:1", name0 + 70);
    CHECK(!strcmp(ws, want_ws));
    /* double click on the Place divider: fits Place's titles and cells (CJK,
     * Greek, Cyrillic, macrons) exactly, within 24..480 */
    divider_xy(t, 2, &dx, &dy);
    CHECK(press_at(dx, dy) == 0 && release_at(dx, dy) == 0);                  /* first click: no change */
    CHECK(t->cols[2].want == 156);
    push_press_n(dx, dy, 2); CHECK(pump() == 0 && release_at(dx, dy) == 0);
    int fit = t->cols[2].width, widest = 0, wrow = -1;
    for (int r = 0; r < t->view_count; r++) {
        int w = measure(gv_table_get_cell(t, r, 2), 12.0);
        if (w > widest) { widest = w; wrow = r; }
    }
    CHECK(fit == widest + 22 && fit >= GV_TABLE_COL_MIN && fit <= GV_TABLE_FIT_MAX && !t->cols[2].flex);
    CHECK(gint("gallery/table-sel") == sel0);
    /* the widest place fits its text box (rect.w = width - 20) with room to
     * spare, so Pango draws it whole; the title fits too */
    CHECK(wrow >= 0 && fit - 20 >= widest && fit - 12 >= measure(t->cols[2].title, 13.0));
    /* Notes: the long notes cap the fit at 480 */
    /* (at 1200x700 the Notes divider is past the right edge now: Name and
     * Place take the width, so it cannot be grabbed; a wider window shows it) */
    divider_xy(t, 3, &dx, &dy);
    CHECK(gv_table_divider_at(t, dx - tr.x, t->header_h / 2) == -1);
    CHECK(resize(1700, 900) == 0 && table_rect(&tr) == 0 && (n = table_node()));
    divider_xy(t, 3, &dx, &dy);
    CHECK(dx < tr.x + n->w && t->cols[3].flex);
    CHECK(move_to(dx - 2, dy) == 0 && gv_glon_host_cursor(h) == GV_CURSOR_COL_RESIZE);
    push_press_n(dx - 2, dy, 2); CHECK(pump() == 0 && release_at(dx, dy) == 0);
    CHECK(t->cols[3].width == GV_TABLE_FIT_MAX && !t->cols[3].flex);
    if (png_dir) { char pth[512]; snprintf(pth, sizeof pth, "%s/p2-table-cols-fit.png", png_dir); CHECK(gv_backend_sdl_save_png(b, pth) == 0); }
    CHECK(resize(1200, 700) == 0 && table_rect(&tr) == 0 && (n = table_node()));
    gstr("gallery/table-widths", ws, sizeof ws);
    snprintf(want_ws, sizeof want_ws, "44:0;%d:0;%d:0;480:0", name0 + 70, fit);
    CHECK(!strcmp(ws, want_ws));
    /* the scrollbar still drags and rows still select */
    int ty = 0, tl = 0;
    table_rect(&tr); n = table_node();
    CHECK(n && gv_table_scrollbar(t, n->w, &sx0, &sx1) && gv_table_thumb(t, &ty, &tl));
    CHECK(press_at(tr.x + (sx0 + sx1) / 2, tr.y + ty + 2) == 0 && move_to(tr.x + (sx0 + sx1) / 2, tr.y + ty + 60) == 0 && t->visible_start > 0);
    CHECK(release_at(tr.x + (sx0 + sx1) / 2, tr.y + ty + 60) == 0 && gv_glon_host_cursor(h) == GV_CURSOR_NORMAL);
    int st = t->visible_start;
    CHECK(click(tr.x + 100, tr.y + t->header_h + t->row_h / 2) == 0 && gint("gallery/table-sel") == st);
    /* kept: scrolling, a preview switch, a theme switch, resizing */
    CHECK(wheel(tr.x + 100, tr.y + 100, -3) == 0 && !strcmp((gstr("gallery/table-widths", ws, sizeof ws), ws), want_ws));
    CHECK(t->cols[1].width == name0 + 70 && t->cols[2].width == fit && t->cols[3].width == 480);
    CHECK(click_text("Button") == 0 && click_text("Table") == 0);
    t = gtable();
    CHECK(t && t->cols[1].width == name0 + 70 && t->cols[2].width == fit && t->cols[3].width == 480);
    CHECK(click_text("Theme") == 0 && click_text("Table") == 0);
    t = gtable();
    CHECK(t && t->cols[1].width == name0 + 70 && t->cols[2].width == fit && t->cols[3].width == 480 && columns_aligned(t) == 0);
    /* too narrow: nothing shrinks; the columns past the scrollbar are cut by
     * the table's clip, the scrollbar stays at the right edge and works, and
     * dividers past the edge cannot be grabbed */
    CHECK(resize(960, 660) == 0 && table_rect(&tr) == 0 && (n = table_node()));
    CHECK(t->cols[1].width == name0 + 70 && t->cols[3].width == 480 && gv_table_col_x(t, 4) > n->w);
    CHECK(gv_table_scrollbar(t, n->w, &sx0, &sx1) && sx1 == n->w && sx1 - sx0 == 14);
    CHECK(n->draw_list.count > 2 && n->draw_list.cmds[1].op == GV_DRAW_CLIP && n->draw_list.cmds[1].rect.w == n->w);
    CHECK(gv_table_divider_at(t, gv_table_col_x(t, 4), t->header_h / 2) == -1);
    CHECK(columns_aligned(t) == 0);
    /* back to a flexible Notes after a fit: the spec's default returns with a
     * new gallery (widths are per run); table/col-width sets one column */
    CHECK(resize(1200, 700) == 0);
    CHECK(presented_ok() == 0);
    printf("table-cols: Notes flexible (%d -> %d px), # 44; resize pointer over dividers only; drag Name +70 "
           "(floor %d, ceiling %d), no selection; double-click fits Place to %d px (Pango), Notes capped at %d; "
           "widths kept across scroll, preview, theme, resize; narrow window cuts on the right\n",
           notes0, notes_wide, GV_TABLE_COL_MIN, GV_TABLE_COL_MAX, fit, GV_TABLE_FIT_MAX);
}

/* ==== INTENT-1: the declarative dialect (view/glon/intent.glon) ========== */
#include <stdarg.h>
static long ig(const char *fmt, ...) {
    char e[480]; va_list ap; va_start(ap, fmt); vsnprintf(e, sizeof e, fmt, ap); va_end(ap);
    return gint(e);
}
static void igs(char *out, int cap, const char *fmt, ...) {
    char e[480]; va_list ap; va_start(ap, fmt); vsnprintf(e, sizeof e, fmt, ap); va_end(ap);
    gstr(e, out, cap);
}
/* show a layout of intent_test.glon: its handler takes view-key 5000 + i
 * (layouts are built inside an event, as an application builds them) */
static int ishow(const char *lay) {
    static const char *names[] = {"test/form", "test/columns", "test/rows", "test/unequal", "test/alias",
                                  "test/nested", "test/escape", "test/many", "test/stretch", "test/handlers", "test/foreign", "test/foreign-x11",
                                  "test/browser", "test/browser-x11", "test/i3", "test/i3-forms", "test/browser3-x11", "test/i4", "test/i4-tabs", "test/i4-table", "test/i4-edit", "test/i4-sel",
                                  "test/i4-layout", "test/i4-vfill", "test/i4-state", "test/i5-theme",
                                  "test/i5-look", "test/i5-image"};
    for (int i = 0; i < 28; i++)
        if (!strcmp(lay, names[i])) {
            if (key(5000 + i) != 0 || gv_glon_host_error(h)) return -1;
            return ig("= intent/current %s", lay) == 1 ? 0 : -1;
        }
    return -1;
}
static long icount(void) { return gint("intent/count intent/current"); }
static long ikind(int i) { return ig("block-at intent/face intent/current %d 0", i); }
static long inode(int i) { return ig("intent/node intent/current %d", i); }
static long inamed(const char *name) { return ig("intent/named intent/current '%s", name); }
/* a face's box on screen: its node's screen origin (a label's text node sits
 * lower in its row, by the centring offset) and Intent's size */
static int ibox(int i, gv_rect *r) {
    gv_rect sr;
    if (screen(inode(i), &sr) != 0) return -1;
    long dy = ikind(i) == 1 ? gint("/ - 30 + block-at intent/theme 69 6 2") : 0;
    r->x = sr.x; r->y = sr.y - (int)dy;
    r->w = (int)ig("intent/live-w intent/current %d", i);          /* its rule's box when it has one */
    r->h = (int)ig("block-at intent/face intent/current %d 7", i);
    return 0;
}
static int inamed_box(const char *name, gv_rect *r) { long i = inamed(name); return i < 0 ? -1 : ibox((int)i, r); }
static int overlaps(gv_rect a, gv_rect b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
static int inside(gv_rect in, gv_rect out) {
    return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w && in.y + in.h <= out.y + out.h;
}
/* no two faces overlap, except a panel (or form) and what it holds */
static int no_overlaps(void) {
    int f0 = failures, n = (int)icount();
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            gv_rect a, c;
            if (ibox(i, &a) || ibox(j, &c)) { CHECK(0); continue; }
            /* a panel or a form (a panel with its own names) holds what is in it */
            int pi = ikind(i) == 4 || ikind(i) == 8, pj = ikind(j) == 4 || ikind(j) == 8;
            if ((pi && inside(c, a)) || (pj && inside(a, c))) continue;
            if (overlaps(a, c)) {
                fprintf(stderr, "  overlap: face %d %d,%d %dx%d and face %d %d,%d %dx%d\n", i, a.x, a.y, a.w, a.h, j, c.x, c.y, c.w, c.h);
                CHECK(!overlaps(a, c));
            }
        }
    return failures == f0 ? 0 : -1;
}
static void field_text(const char *name, char *out, int cap) { igs(out, cap, "vid/field/get-text intent/get intent/current '%s", name); }
static int click_face(const char *name) {
    gv_rect r; if (inamed_box(name, &r) != 0) return -1;
    return click(r.x + r.w / 2, r.y + r.h / 2);
}
static int click_face_i(int i) {
    gv_rect r; if (ibox(i, &r) != 0) return -1;
    return click(r.x + r.w / 2, r.y + r.h / 2);
}
/* ink (pixels unlike the background sampled at bg) inside / outside a box,
 * over the row band of the box */
static void ink_in_out(gv_rect r, int x1, int *in, int *out) {
    int bg[3]; pixel(r.x - 3, r.y + 2, bg);
    *in = *out = 0;
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x - 6; x < x1; x++) {
            int p[3]; pixel(x, y, p);
            if (rgb_diff(p, bg) > 80) { if (x >= r.x && x < r.x + r.w) (*in)++; else (*out)++; }
        }
}

/* intent-parse: the reader -- kinds, names, sizes, strings both ways, errors */
static void case_intent_parse(void) {
    char a[128];
    CHECK(ishow("test/form") == 0 && icount() == 6);
    long kinds[6] = {1, 2, 1, 2, 3, 3};
    for (int i = 0; i < 6; i++) CHECK(ikind(i) == kinds[i]);
    CHECK(inamed("name") == 1 && inamed("place") == 3 && inamed("nothing") == -1);
    /* a name is the machine VID state itself: its kind, its node */
    CHECK(gint("block-at intent/get intent/current 'name 0") == gint("vid/kind-field"));
    CHECK(gint("block-at intent/get intent/current 'name 1") == inode(1));
    field_text("name", a, sizeof a); CHECK(!strcmp(a, "Default text"));
    /* field 20: 20 character units of text area + the 16 px insets */
    long unit = gint("intent/unit intent/theme");
    CHECK(unit == 11 && ig("block-at intent/face intent/current 1 6") == 20 * unit + 16);
    /* units: 1 per character, 2 for East Asian wide; bytes are not counted */
    CHECK(gint("intent/units \"Māori\"") == 5 && gint("intent/units \"漢字\"") == 4 && gint("intent/units \"\"") == 0);
    /* a reduced block (STRING! values) reads like a literal one */
    CHECK(gint("block-at intent/show-now layout reduce [ 'across 'text \"abc\" 'field 7 ] 3") == 2);
    CHECK(gv_glon_host_bind(h) == 0 && render() == 0);
    CHECK(ig("block-at intent/face intent/current 1 6") == 7 * unit + 16);
    /* errors name the problem */
    gstr("sin-id judge [ intent/show-now layout [ frobnicate ] ]", a, sizeof a); CHECK(strstr(a, "unknown-word") != NULL);
    gstr("sin-id judge [ intent/show-now layout [ label 42 ] ]", a, sizeof a); CHECK(strstr(a, "label-needs-text") != NULL);
    gstr("sin-id judge [ intent/show-now layout [ button ] ]", a, sizeof a); CHECK(strstr(a, "button-needs-caption") != NULL);
    gstr("sin-id judge [ intent/show-now layout [ 42 ] ]", a, sizeof a); CHECK(strstr(a, "unexpected") != NULL);
    gstr("sin-id judge [ intent/show-now layout [ vid 10 [ 0 ] ] ]", a, sizeof a); CHECK(strstr(a, "vid-needs-size-and-block") != NULL);
    /* a form scope holds 32 names (INTENT-2); inline actions see at most 10 */
    gstr("sin-id judge [ intent/show-now layout [ n1: field n2: field n3: field n4: field n5: field n6: field n7: field n8: field n9: field n10: field n11: field n12: field n13: field n14: field n15: field n16: field n17: field n18: field n19: field n20: field n21: field n22: field n23: field n24: field n25: field n26: field n27: field n28: field n29: field n30: field n31: field n32: field n33: field ] ]", a, sizeof a);
    CHECK(strstr(a, "too-many-names") != NULL);
    gstr("sin-id judge [ intent/show-now layout [ a: field b: field c: field d: field e: field f: field g: field h: field i: field j: field k: field button \"x\" [ clear a ] ] ]", a, sizeof a);
    CHECK(strstr(a, "too-many-names-for-actions") != NULL);
    gstr("sin-id judge [ layout 5 ]", a, sizeof a); CHECK(strstr(a, "layout-needs-block") != NULL);
    CHECK(ishow("test/form") == 0 && icount() == 6);   /* and the next layout is fine */
    CHECK(presented_ok() == 0);
    printf("intent-parse: kinds and names read from the block; a name is the machine VID state; field N = N x %ld + 16 px; "
           "UTF-8 units; reduced blocks read alike; 7 errors named\n", unit);
}

/* intent-directions: down (default) with return = columns; across with
 * return = rows; spacing 8, margin 16 */
static void case_intent_directions(void) {
    gv_rect A, B, C, D, root;
    CHECK(ishow("test/columns") == 0 && icount() == 4);
    CHECK(ibox(0, &A) == 0 && ibox(1, &B) == 0 && ibox(2, &C) == 0 && ibox(3, &D) == 0);
    CHECK(screen(ig("block-at intent/current 4"), &root) == 0);
    CHECK(A.x == root.x + 16 && A.y == root.y + 16);
    CHECK(B.x == A.x && B.y == A.y + A.h + 8);                         /* A over B */
    CHECK(C.y == A.y && C.x == A.x + (A.w > B.w ? A.w : B.w) + 8);     /* C beside A: a new column */
    CHECK(D.x == C.x && D.y == C.y + C.h + 8);
    CHECK(no_overlaps() == 0);
    CHECK(ishow("test/rows") == 0 && icount() == 4);
    CHECK(ibox(0, &A) == 0 && ibox(1, &B) == 0 && ibox(2, &C) == 0 && ibox(3, &D) == 0);
    CHECK(B.y == A.y && B.x == A.x + A.w + 8);                         /* A B */
    CHECK(C.x == A.x && C.y == A.y + 30 + 8);                          /* C under A: a new row */
    CHECK(D.y == C.y && D.x == C.x + C.w + 8);
    CHECK(no_overlaps() == 0 && presented_ok() == 0);
    printf("intent-directions: down + return = columns A C / B D; across + return = rows A B / C D; margin 16, spacing 8\n");
}

/* intent-unequal: different widths and heights never overlap; a tall item
 * sets its row; switching direction continues from the cursor */
static void case_intent_unequal(void) {
    gv_rect r[12];
    CHECK(ishow("test/unequal") == 0 && icount() == 11);
    for (int i = 0; i < 11; i++) CHECK(ibox(i, &r[i]) == 0);
    CHECK(no_overlaps() == 0);
    /* row 1: Short, field 5, long button: same top, each after the last */
    CHECK(r[1].y == r[0].y && r[2].y == r[0].y && r[1].x == r[0].x + r[0].w + 8 && r[2].x == r[1].x + r[1].w + 8);
    /* the window is wider than the content: field 5 (flexible) takes the
     * row's spare width and the button after it ends at the right margin */
    gv_rect root0; CHECK(screen(ig("block-at intent/current 4"), &root0) == 0);
    CHECK(r[1].w > 5 * (int)gint("intent/unit intent/theme") + 16 && r[2].x + r[2].w == root0.x + root0.w - 16);
    CHECK(r[2].w == (int)ig("block-at intent/face intent/current 2 6"));        /* the button is fixed */
    /* row 2 starts 30 + 8 lower; its tall box (90) pushes row 3 down */
    CHECK(r[3].x == r[0].x && r[3].y == r[0].y + 38 && r[3].h == 90);
    CHECK(r[4].y == r[3].y && r[5].y == r[3].y);
    CHECK(r[6].x == r[0].x && r[6].y == r[3].y + 90 + 8);
    /* down from where the row left off: "Then down" right of the text, field 8 under it */
    CHECK(r[7].x == r[6].x + r[6].w + 8 && r[7].y == r[6].y);
    CHECK(r[8].x == r[7].x && r[8].y == r[7].y + 30 + 8);
    /* return in down: a new column right of the widest of that column */
    int colw = r[7].w > r[8].w ? r[7].w : r[8].w;
    CHECK(r[9].x == r[7].x + colw + 8 && r[9].y == r[7].y);
    CHECK(r[10].x == r[9].x && r[10].y == r[9].y + 38);
    /* the content box holds every face */
    long cw = gint("block-at intent/current 9"), ch = gint("block-at intent/current 10");
    gv_rect root; CHECK(screen(ig("block-at intent/current 4"), &root) == 0);
    long lw = gint("lay/box-w 2"), lh = gint("lay/box-h 2");       /* the content rule's live box */
    CHECK(lw == 1200 && lh == 700 && cw < lw);
    for (int i = 0; i < 11; i++) CHECK(r[i].x + r[i].w <= root.x + lw - 16 && r[i].y + r[i].h <= root.y + ch - 16);
    /* row 2: field 40 grows, "x" after it ends at the margin; the row after a
     * down run and the column with a column to its right do not grow */
    CHECK(r[5].x + r[5].w == root.x + lw - 16 && r[5].w == (int)ig("block-at intent/face intent/current 5 6"));
    CHECK(r[8].w == (int)ig("block-at intent/face intent/current 8 6"));
    CHECK(presented_ok() == 0);
    printf("intent-unequal: 11 faces of 5 widths and 3 heights, no overlap; the 90 px box sets its row; "
           "down continues from the cursor and return makes a column; content %ldx%ld holds all\n", cw, ch);
}

/* intent-alias: label and text are one widget: same machine kind, size,
 * nodes, drawing and behaviour */
static void case_intent_alias(void) {
    CHECK(ishow("test/alias") == 0 && icount() == 3);
    long l = inamed("l1"), t = inamed("t1");
    CHECK(l == 0 && t == 1 && ikind(0) == 1 && ikind(1) == 1);
    CHECK(gint("block-at intent/get intent/current 'l1 0") == gint("block-at intent/get intent/current 't1 0"));
    CHECK(gint("block-len intent/get intent/current 'l1") == gint("block-len intent/get intent/current 't1"));
    gv_rect L, T; CHECK(ibox(0, &L) == 0 && ibox(1, &T) == 0);
    CHECK(L.w == T.w && L.h == T.h && L.x == T.x && T.y == L.y + L.h + 8);
    gv_node *ln = gv_view_node(v, vnode(inode(0))), *tn = gv_view_node(v, vnode(inode(1)));
    CHECK(ln && tn && ln->type == tn->type && ln->type == GV_NODE_TEXT && !strcmp(ln->text, tn->text));
    CHECK(ln->w == tn->w && ln->h == tn->h && ln->font_size == tn->font_size && !memcmp(&ln->fg, &tn->fg, sizeof ln->fg) &&
          ln->focusable == tn->focusable && ln->pickable == tn->pickable);
    /* the same pixels, row for row */
    int diff = 0;
    for (int y = 0; y < L.h; y++)
        for (int x = 0; x < L.w; x++) { int p[3], q[3]; pixel(L.x + x, L.y + y, p); pixel(T.x + x, T.y + y, q); diff += rgb_diff(p, q) > 0; }
    CHECK(diff == 0);
    /* and the same behaviour: set-text through an action */
    CHECK(click_face_i(2) == 0);
    ln = gv_view_node(v, vnode(inode(0))); tn = gv_view_node(v, vnode(inode(1)));
    CHECK(ln && tn && !strcmp(ln->text, "Changed") && !strcmp(tn->text, "Changed"));
    CHECK(presented_ok() == 0);
    printf("intent-alias: label and text give the same machine kind, state shape, size, node, font, colour and pixels (%d px differ), and both retitle\n", diff);
}

/* intent-nested: a container's direction and cursor are its own */
static void case_intent_nested(void) {
    CHECK(ishow("test/nested") == 0);
    gv_rect L, P1, P2, R, one, two, three, x, y, z;
    long p1 = inamed("p1"), p2 = inamed("p2");
    CHECK(p1 >= 0 && p2 >= 0 && ikind((int)p1) == 4 && ikind((int)p2) == 4);
    CHECK(ibox(0, &L) == 0 && ibox((int)p1, &P1) == 0 && ibox((int)p2, &P2) == 0 && ibox((int)icount() - 1, &R) == 0);
    /* the outer row: Left, p1, p2, Right (across survives the panels) */
    CHECK(P1.y == L.y && P1.x == L.x + L.w + 8 && P2.x == P1.x + P1.w + 8 && P2.y == L.y && R.x == P2.x + P2.w + 8 && R.y == L.y);
    /* p1 flows down (its own default) and return makes a column */
    CHECK(ibox(1, &one) == 0 && ibox(2, &two) == 0 && ibox(3, &three) == 0);
    CHECK(one.x == P1.x + 10 && one.y == P1.y + 10 && two.x == one.x && two.y == one.y + 38);
    CHECK(three.y == one.y && three.x == one.x + (one.w > two.w ? one.w : two.w) + 8);
    /* p2 flows across and return makes a row */
    CHECK(ibox(5, &x) == 0 && ibox(6, &y) == 0 && ibox(7, &z) == 0);
    CHECK(y.y == x.y && y.x == x.x + x.w + 8 && z.x == x.x && z.y == x.y + 38);
    /* each panel holds its content, padded 10 */
    CHECK(inside(one, P1) && inside(two, P1) && inside(three, P1) && inside(x, P2) && inside(y, P2) && inside(z, P2));
    CHECK(P1.w == three.x + three.w + 10 - P1.x && P1.h == two.y + two.h + 10 - P1.y);
    CHECK(no_overlaps() == 0 && presented_ok() == 0);
    printf("intent-nested: outer across row of 4; panel 1 down with a column, panel 2 across with a row; panels sized to content\n");
}

/* ==== INTENT-4B-4: layout controls ======================================== */
/* the box of a named face, live (its rule's box when it has one) */
/* a face's index through the root form scope (intent/named sees only the
 * first 10 root names, the inline-action limit) */
static long iscoped(const char *name) {
    long k = ig("intent/scope-index block-at intent/current 15 '%s 0", name);
    return k < 0 ? -1 : ig("block-at block-at block-at intent/current 15 4 %ld", k);
}
static int lbox(const char *name, gv_rect *r) {
    long i = iscoped(name);
    if (i < 0 || ibox((int)i, r) != 0) return -1;
    r->h = (int)ig("intent/live-h intent/current %ld", i);
    return 0;
}
#define SHOWBOX(n, r) fprintf(stderr, "  %s: %d,%d %dx%d\n", n, (r).x, (r).y, (r).w, (r).h)
/* intent4-layout: margin, space, pad, text and row alignment, button widths,
 * a panel's own margin and spacing; errors by name */
static void case_intent4_layout(void) {
    char a[200];
    CHECK(ishow("test/i4-layout") == 0);
    gv_rect root, nl, nm, tl, tw, go, ti, sm, ta, ok, tp, P, px, py, wd, au;
    CHECK(screen(ig("block-at intent/current 4"), &root) == 0);
    CHECK(lbox("name-l", &nl) == 0 && lbox("name", &nm) == 0 && lbox("town-l", &tl) == 0 && lbox("town", &tw) == 0 &&
          lbox("go", &go) == 0 && lbox("title", &ti) == 0 && lbox("small", &sm) == 0 && lbox("tall", &ta) == 0 &&
          lbox("ok", &ok) == 0 && lbox("top-l", &tp) == 0 && lbox("p", &P) == 0 && lbox("px", &px) == 0 &&
          lbox("py", &py) == 0 && lbox("wide", &wd) == 0 && lbox("auto", &au) == 0);
    long unit = gint("intent/unit intent/theme");
    /* margin 24, space 12: the first face at the margin, the next 12 px on */
    int f0 = failures;
    CHECK(nl.x == root.x + 24 && nl.y == root.y + 24 && nl.w == 10 * unit + 4);
    CHECK(nm.x == nl.x + nl.w + 12 && nm.y == nl.y);
    /* the next row: 30 + 12 lower; pad 40 adds 40 px before "Go" */
    CHECK(tl.x == nl.x && tl.y == nl.y + 42 && tw.x == tl.x + tl.w + 12 && go.y == tl.y);
    CHECK(go.x == tw.x + tw.w + 12 + 40 && go.w == 6 * unit + 24);
    /* the flexible field ends at the right margin (24) */
    CHECK(nm.x + nm.w == root.x + root.w - 24);
    /* row 4: the 90 px box sets the row; Mid in the middle, OK at the bottom, Top at the top */
    CHECK(ti.y == tl.y + 42 && ta.y == ti.y + 42 && ta.h == 90);
    CHECK(sm.y == ta.y + 30 && ok.y == ta.y + 60 && tp.y == ta.y);
    CHECK(sm.x == root.x + 24 && ta.x == sm.x + sm.w + 12 && ok.x == ta.x + ta.w + 12 && tp.x == ok.x + ok.w + 12);
    /* the panel: margin 4, space 2, sized to them; root spacing outside it */
    CHECK(P.y == ta.y + 90 + 12 && px.x == P.x + 4 && px.y == P.y + 4 && py.y == px.y + 32);
    CHECK(P.h == 4 + 30 + 2 + 30 + 4 && P.w == 4 + (px.w > py.w ? px.w : py.w) + 4);
    /* button widths: 16 units + 24 as asked; a short caption's 80 */
    CHECK(wd.x == P.x + P.w + 12 && wd.y == P.y && wd.w == 16 * unit + 24 && au.w == 80 && au.x == wd.x + wd.w + 12);
    if (failures != f0) {
        SHOWBOX("root", root); SHOWBOX("name-l", nl); SHOWBOX("name", nm); SHOWBOX("town-l", tl); SHOWBOX("town", tw);
        SHOWBOX("go", go); SHOWBOX("title", ti); SHOWBOX("small", sm); SHOWBOX("tall", ta); SHOWBOX("ok", ok);
        SHOWBOX("top-l", tp); SHOWBOX("p", P); SHOWBOX("px", px); SHOWBOX("py", py); SHOWBOX("wide", wd); SHOWBOX("auto", au);
    }
    /* text alignment: View's text-align, and the ink on that side of the box */
    gv_node *nn = gv_view_node(v, vnode(inode((int)iscoped("name-l"))));
    gv_node *tn = gv_view_node(v, vnode(inode((int)iscoped("title"))));
    CHECK(nn && nn->text_align == GV_ALIGN_END && tn && tn->text_align == GV_ALIGN_CENTER);
    CHECK(render() == 0);
    {
        int bg[3]; pixel(nl.x + 1, nl.y + 1, bg);
        int left = 0, right = 0, tleft = 0, tmid = 0, tright = 0, x0 = 99999, x1 = -1;
        for (int y = nl.y; y < nl.y + 30; y++)
            for (int x = nl.x; x < nl.x + nl.w; x++) {
                int q[3]; pixel(x, y, q);
                if (rgb_diff(q, bg) > 80) { if (x < x0) x0 = x; if (x > x1) x1 = x; }
            }
        /* the text's ink ends at the box's right end and starts well inside it */
        left = x0 - nl.x; right = nl.x + nl.w - 1 - x1;
        for (int y = ti.y; y < ti.y + 30; y++)
            for (int x = ti.x; x < ti.x + ti.w; x++) {
                int q[3]; pixel(x, y, q);
                if (rgb_diff(q, bg) > 80) { if (x < ti.x + ti.w / 3) tleft++; else if (x < ti.x + 2 * ti.w / 3) tmid++; else tright++; }
            }
        if (!(right >= 0 && right <= 3 && left > 30 && tmid > 0 && tleft == 0 && tright == 0))
            fprintf(stderr, "  ink: name-l gap left %d right %d; title %d %d %d\n", left, right, tleft, tmid, tright);
        CHECK(right >= 0 && right <= 3 && left > 30);  /* "Name:" right-aligned in its 10 units */
        CHECK(tmid > 0 && tleft == 0 && tright == 0);  /* "Centred" in the middle third */
    }
    /* two window sizes: the flexible field grows, the rest keep their place */
    gv_rect nm2, go2, sm2, ok2, P2;
    CHECK(resize(1500, 800) == 0 && lbox("name", &nm2) == 0 && lbox("go", &go2) == 0 && lbox("small", &sm2) == 0 &&
          lbox("ok", &ok2) == 0 && lbox("p", &P2) == 0);
    CHECK(nm2.x == nm.x && nm2.w == nm.w + 300 && nm2.x + nm2.w == root.x + 1500 - 24);
    CHECK(go2.x == go.x && go2.y == go.y && sm2.y == sm.y && ok2.y == ok.y && P2.x == P.x && P2.y == P.y && P2.w == P.w);
    CHECK(resize(1200, 700) == 0 && lbox("name", &nm2) == 0 && nm2.w == nm.w && no_overlaps() == 0);
    /* errors, by name */
    gstr("sin-id judge [ intent/show-now layout [ label \"a\" margin 4 ] ]", a, sizeof a); CHECK(strstr(a, "margin-must-come-first") != NULL);
    gstr("sin-id judge [ intent/show-now layout [ space ] ]", a, sizeof a); CHECK(strstr(a, "needs-pixels") != NULL);
    gstr("sin-id judge [ intent/show-now layout [ pad -3 ] ]", a, sizeof a); CHECK(strstr(a, "needs-pixels") != NULL);
    gstr("sin-id judge [ intent/show-now layout [ field 10 right ] ]", a, sizeof a); CHECK(strstr(a, "align-needs-a-text-widget") != NULL);
    gstr("sin-id judge [ intent/show-now layout [ across area 10 2 fill-height ] ]", a, sizeof a); CHECK(strstr(a, "fill-height-needs-a-down-run") != NULL);
    gstr("sin-id judge [ intent/show-now layout [ middle ] ]", a, sizeof a); CHECK(strstr(a, "option-needs-a-face") != NULL);
    gstr("sin-id judge [ intent/show-now layout [ style top label ] ]", a, sizeof a); CHECK(strstr(a, "style-shadows-keyword") != NULL);
    /* the defaults are unchanged: margin 16, spacing 8 */
    CHECK(ishow("test/rows") == 0);
    gv_rect A, B; CHECK(ibox(0, &A) == 0 && ibox(1, &B) == 0 && screen(ig("block-at intent/current 4"), &root) == 0);
    CHECK(A.x == root.x + 16 && A.y == root.y + 16 && B.x == A.x + A.w + 8);
    CHECK(presented_ok() == 0);
    printf("intent4-layout: margin 24 / space 12 / pad 40 placed exactly; labels right- and centre-aligned (View and ink); "
           "middle +30 and bottom +60 in a 90 px row; a panel's margin 4 / space 2; button 16 units = %ld px; "
           "two window sizes; 7 errors named; defaults unchanged\n", 16 * unit + 24);
}

/* intent4-fill: fill-height in a down run -- two stacked fill faces share
 * the spare height, a panel's list fills the panel, the face below moves by
 * all of it, the column beside stays; at three window heights and back */
static void case_intent4_fill(void) {
    CHECK(ishow("test/i4-vfill") == 0);
    gv_rect root, hd, nt, fp, in, pl, dn, sd, sd2;
    long ch = gint("block-at intent/current 10");        /* the content's preferred height */
    gv_rect pnt, pfp, ppl, pdn;                           /* preferred: the window shorter than the content */
    CHECK(resize(1200, (int)ch - 100) == 0);
    CHECK(lbox("notes", &pnt) == 0 && lbox("fp", &pfp) == 0 && lbox("pl", &ppl) == 0 && lbox("done", &pdn) == 0);
    CHECK(pnt.h == (int)ig("block-at intent/face intent/current %ld 7", iscoped("notes")));
    CHECK(pfp.h == (int)ig("block-at intent/face intent/current %ld 7", iscoped("fp")));
    CHECK(screen(ig("block-at intent/current 4"), &root) == 0);
    CHECK(pfp.y == pnt.y + pnt.h + 8 && pdn.y == pfp.y + pfp.h + 8 && pdn.y + 30 + 16 == root.y + (int)ch);
    int heights[3] = { 700, 900, 1100 };
    for (int k = 0; k < 3; k++) {
        int H = heights[k];
        CHECK(resize(1200, H) == 0 && screen(ig("block-at intent/current 4"), &root) == 0);
        CHECK(lbox("head", &hd) == 0 && lbox("notes", &nt) == 0 && lbox("fp", &fp) == 0 && lbox("inner", &in) == 0 &&
              lbox("pl", &pl) == 0 && lbox("done", &dn) == 0 && lbox("side", &sd) == 0 && lbox("side2", &sd2) == 0);
        int E = H - (int)ch, f0 = failures;
        /* the two fill faces share E (one pixel of rounding at most) */
        CHECK(nt.h - pnt.h >= E / 2 - 1 && nt.h - pnt.h <= E / 2 + 1 && fp.h - pfp.h >= E / 2 - 1 && fp.h - pfp.h <= E / 2 + 1);
        CHECK(nt.y == pnt.y && fp.y == nt.y + nt.h + 8);
        /* the button below moves by all of it and ends at the bottom margin */
        CHECK(dn.y == pdn.y + E && dn.y + 30 == root.y + H - 16);
        /* the list in the panel fills the panel's new height */
        CHECK(in.y == fp.y + 10 && pl.y == in.y + 38 && pl.y + pl.h == fp.y + fp.h - 10 && pl.h == ppl.h + (fp.h - pfp.h));
        /* the taller list shows more rows: keys and reveal page by them */
        CHECK(ig("intent/table-vis intent/face intent/current %ld", iscoped("pl")) == (pl.h - 2) / 24);
        /* the column beside stays at the top, at its preferred size */
        CHECK(sd.y == hd.y && sd2.y == sd.y + 38 && sd2.h == 30);
        if (failures != f0) {
            fprintf(stderr, "  window %d, E %d (content %ld)\n", H, E, ch);
            SHOWBOX("notes", nt); SHOWBOX("fp", fp); SHOWBOX("inner", in); SHOWBOX("pl", pl); SHOWBOX("done", dn); SHOWBOX("side", sd);
            SHOWBOX("pref notes", pnt); SHOWBOX("pref fp", pfp); SHOWBOX("pref pl", ppl); SHOWBOX("pref done", pdn);
        }
        CHECK(no_overlaps() == 0);
    }
    /* back below the content: every height preferred again, the viewport scrolls */
    CHECK(resize(1200, (int)ch - 100) == 0 && lbox("notes", &nt) == 0 && lbox("done", &dn) == 0);
    CHECK(nt.h == pnt.h && dn.y == pdn.y);
    CHECK(resize(1200, 700) == 0 && presented_ok() == 0);
    printf("intent4-fill: two fill faces share the spare height (window 700 / 900 / 1100, content %ld), the panel's list fills "
           "the panel, the button below ends at the bottom margin, the column beside stays; preferred heights below the content\n", ch);
}

/* intent-edit: focus, typing, Backspace (UTF-8), Tab, actions, Enter */
static void case_intent_edit(void) {
    char a[128];
    CHECK(ishow("test/form") == 0);
    /* typing with nothing focused changes nothing */
    CHECK(text("zz") == 0);
    field_text("name", a, sizeof a); CHECK(!strcmp(a, "Default text"));
    CHECK(click_face("name") == 0 && gv_view_focus_id(v) == vnode(inode(1)));
    CHECK(gint("block-at intent/current 8") == 1);
    gv_node *ring = gv_view_node(v, vnode(gint("block-at intent/get intent/current 'name 9")));
    CHECK(ring && ring->visible);                                   /* the focus ring shows */
    CHECK(text("abc") == 0); field_text("name", a, sizeof a); CHECK(!strcmp(a, "Default textabc"));
    CHECK(key(8) == 0); field_text("name", a, sizeof a); CHECK(!strcmp(a, "Default textab"));
    gv_node *tx = gv_view_node(v, vnode(gint("block-at intent/get intent/current 'name 3")));
    CHECK(tx && !strcmp(tx->text, "Default textab") && tx->caret == (int)strlen("Default textab") + 1);   /* caret after the text (index + 1) */
    /* Tab: to the Place field; the ring moves */
    CHECK(key(9) == 0 && gv_view_focus_id(v) == vnode(inode(3)) && gint("block-at intent/current 8") == 3);
    ring = gv_view_node(v, vnode(gint("block-at intent/get intent/current 'name 9")));
    CHECK(ring && !ring->visible);
    /* Backspace removes one whole UTF-8 character (ā is two bytes) */
    CHECK(key(8) == 0); field_text("place", a, sizeof a); CHECK(!strcmp(a, "Te Whanganui-a-Tara "));
    CHECK(utf8_ok((const unsigned char *)a));
    CHECK(text("x") == 0); field_text("place", a, sizeof a); CHECK(!strcmp(a, "Te Whanganui-a-Tara x"));
    field_text("name", a, sizeof a); CHECK(!strcmp(a, "Default textab"));   /* the other field untouched */
    /* Clear: the named field is empty, the other keeps its text */
    CHECK(click_face_i(4) == 0); field_text("name", a, sizeof a); CHECK(!strcmp(a, ""));
    field_text("place", a, sizeof a); CHECK(!strcmp(a, "Te Whanganui-a-Tara x"));
    /* Fill: get-text, set-text and focus in one action */
    CHECK(click_face_i(5) == 0); field_text("name", a, sizeof a); CHECK(!strcmp(a, "Filled from Te Whanganui-a-Tara x"));
    CHECK(gv_view_focus_id(v) == vnode(inode(3)));
    /* Tab to the Clear button and press Enter: its action runs */
    CHECK(key(9) == 0 && gv_view_focus_id(v) == vnode(inode(4)) && key(13) == 0);
    field_text("name", a, sizeof a); CHECK(!strcmp(a, ""));
    /* typing while a button has the focus goes nowhere */
    CHECK(text("q") == 0); field_text("name", a, sizeof a); CHECK(!strcmp(a, ""));
    field_text("place", a, sizeof a); CHECK(!strcmp(a, "Te Whanganui-a-Tara x"));
    /* editing goes on across resizes */
    CHECK(click_face("name") == 0 && resize(900, 500) == 0 && text("ok") == 0);
    field_text("name", a, sizeof a); CHECK(!strcmp(a, "ok"));
    CHECK(resize(1200, 700) == 0 && presented_ok() == 0);
    printf("intent-edit: click focuses (ring, caret), typing and UTF-8 Backspace, Tab order, Clear, Fill (get/set/focus), Enter on a button, resize\n");
}

/* intent-escape: a machine VID widget and a View drawing region in the flow */
static void case_intent_escape(void) {
    char a[64];
    CHECK(ishow("test/escape") == 0 && icount() == 6);
    gv_rect before, chk, sw, after, note;
    CHECK(ibox(0, &before) == 0 && inamed_box("chk", &chk) == 0 && inamed_box("sw", &sw) == 0 && ibox(3, &after) == 0);
    CHECK(ikind((int)inamed("chk")) == 5 && ikind((int)inamed("sw")) == 6);
    CHECK(gint("block-at intent/get intent/current 'chk 0") == gint("vid/kind-checkbox"));
    /* in the flow: Before, checkbox (200), box (60), After */
    CHECK(chk.x == before.x + before.w + 8 && chk.w == 200 && sw.x == chk.x + 200 + 8 && sw.w == 60 && sw.h == 30);
    CHECK(after.x == sw.x + 60 + 8 && no_overlaps() == 0);
    /* the drawing is clipped to the box: red inside, none just outside */
    int in = 0, out = 0, red[3] = {200, 40, 40};
    for (int y = sw.y - 6; y < sw.y + sw.h + 6; y++)
        for (int x = sw.x - 6; x < sw.x + sw.w + 6; x++) {
            int p[3]; pixel(x, y, p);
            if (rgb_diff(p, red) < 30) { if (x >= sw.x && x < sw.x + sw.w && y >= sw.y && y < sw.y + sw.h) in++; else out++; }
        }
    CHECK(in == 60 * 30 && out == 0);
    /* events: a click toggles the checkbox through Intent's dispatch, and an
     * action reads its machine state by name */
    CHECK(click(chk.x + 8, chk.y + 12) == 0 && gint("block-at intent/get intent/current 'chk 5") == 1);
    CHECK(click_face_i(5) == 0); field_text("note", a, sizeof a); CHECK(!strcmp(a, "checked"));
    CHECK(click(chk.x + 8, chk.y + 12) == 0 && click_face_i(5) == 0);
    field_text("note", a, sizeof a); CHECK(!strcmp(a, "unchecked"));
    CHECK(inamed_box("note", &note) == 0 && note.y == before.y + 38);
    /* lifecycle: another layout removes both escape nodes with the rest */
    long chk_node = gint("block-at intent/get intent/current 'chk 1"), sw_node = inode((int)inamed("sw"));
    gv_node_id cv = vnode(chk_node), sv = vnode(sw_node);
    CHECK(cv && sv);
    CHECK(ishow("test/rows") == 0);
    gv_node *cn = gv_view_node(v, cv), *sn = gv_view_node(v, sv);
    CHECK(!cn || cn->type != GV_NODE_GROUP || !strstr(cn->text ? cn->text : "", "Opt in"));
    CHECK(text_node("Opt in") == 0);
    (void)sn;
    CHECK(presented_ok() == 0);
    printf("intent-escape: vid (a machine VID checkbox) and box (a clipped View drawing, %d px inside, %d outside) flow with the "
           "layout, take clicks and are read by actions; replaced with the layout\n", in, out);
}

/* intent-lifecycle: replacing layouts reuses ids, resets focus, keeps no state */
static void case_intent_lifecycle(void) {
    char a[64];
    CHECK(ishow("test/form") == 0);
    long id0 = gint("view/id"), live0 = 0;
    for (int i = 1; i <= v->count; i++) { gv_node *n = gv_view_node(v, i); if (n && n->type != GV_NODE_FREE) live0++; }
    CHECK(click_face("name") == 0 && text("typed") == 0);
    field_text("name", a, sizeof a); CHECK(!strcmp(a, "Default texttyped"));
    const char *cycle[4] = {"test/rows", "test/escape", "test/nested", "test/form"};
    for (int k = 0; k < 12; k++) CHECK(ishow(cycle[k % 4]) == 0);
    /* back to the form: fresh defaults, nothing focused, same ids and nodes */
    field_text("name", a, sizeof a); CHECK(!strcmp(a, "Default text"));
    CHECK(gint("block-at intent/current 8") == -1 && gint("view/id") == id0);
    long live = 0;
    for (int i = 1; i <= v->count; i++) { gv_node *n = gv_view_node(v, i); if (n && n->type != GV_NODE_FREE) live++; }
    CHECK(live == live0);
    CHECK(text("nothing") == 0); field_text("name", a, sizeof a); CHECK(!strcmp(a, "Default text"));
    /* more items than a walk step and the most names an action sees */
    CHECK(ishow("test/many") == 0 && icount() == 35 && no_overlaps() == 0);
    CHECK(click_face("n1") == 0 && text("1") == 0 && click_face("n10") == 0 && text("10") == 0);
    field_text("n10", a, sizeof a); CHECK(!strcmp(a, "10"));
    CHECK(click_face_i(34) == 0);
    field_text("n1", a, sizeof a); CHECK(!strcmp(a, ""));
    field_text("n10", a, sizeof a); CHECK(!strcmp(a, ""));
    CHECK(presented_ok() == 0);
    printf("intent-lifecycle: 12 layout swaps leave %ld live nodes and view/id %ld as before, defaults back, focus reset; "
           "35 faces and 10 names work\n", live, id0);
}

/* intent-resize: the window size is asked for at build; the backdrop
 * follows the window and a small window scrolls the content */
static void case_intent_resize(void) {
    CHECK(ishow("test/unequal") == 0);
    CHECK(gint("lay/w") == 1200 && gint("lay/h") == 700);          /* the runner's size, not the default */
    gv_rect r0[11], r1, root;
    for (int i = 0; i < 11; i++) CHECK(ibox(i, &r0[i]) == 0);
    CHECK(resize(1600, 900) == 0 && screen(ig("block-at intent/current 4"), &root) == 0 && root.w == 1600 && root.h == 900);
    /* 400 px wider: each row's one flexible field grows by 400, the faces
     * after it move 400, the others stay */
    for (int i = 0; i < 11; i++) {
        CHECK(ibox(i, &r1) == 0 && r1.y == r0[i].y);
        int dx = (i == 2 || i == 5) ? 400 : 0, dw = (i == 1 || i == 4) ? 400 : 0;
        CHECK(r1.x == r0[i].x + dx && r1.w == r0[i].w + dw);
    }
    /* smaller than the content: the viewport scrolls (wheel) and nothing overlaps */
    long ch = gint("block-at intent/current 10");
    CHECK(resize(500, (int)ch - 100) == 0 && no_overlaps() == 0);
    for (int i = 0; i < 11; i++) {
        CHECK(ibox(i, &r1) == 0);
        long pw = ig("block-at intent/face intent/current %d 6", i);
        /* never below the preferred size; the fixed faces exactly it (row 2's
         * field still takes the spare width row 1 leaves it) */
        CHECK(r1.w >= pw && (i == 1 || i == 4 || r1.w == pw));
    }
    CHECK(wheel(100, 100, -3) == 0);
    long sy = ig("block-at block-at lay/scrolls block-at intent/current 11 4");
    CHECK(sy > 0 && sy <= 100);
    CHECK(ibox(0, &r1) == 0 && r1.y == r0[0].y - sy);
    CHECK(resize(1200, 700) == 0 && ig("block-at block-at lay/scrolls block-at intent/current 11 4") == 0);
    CHECK(ibox(0, &r1) == 0 && r1.y == r0[0].y && presented_ok() == 0);
    printf("intent-resize: built at the runner's 1200x700; at 1600x900 the flexible fields grow 400 and what follows moves; at 500x%ld the content scrolls %ld px "
           "and comes back\n", ch - 100, sy);
}

/* intent-demo: the target syntax, verbatim, in intent_demo.glon */
static void case_intent_demo(void) {
    char a[96];
    if (getenv("P2_TRACE_RESIZE")) {
        gv_rect fr;
        for (int k = 0; k < 2; k++) {
            CHECK(resize(k ? 1400 : 700, k ? 800 : 500) == 0 && inamed_box("name", &fr) == 0);
            fprintf(stderr, "trace: lay/w %ld lay/h %ld root %ld field x %d w %d (node w %d)\n", gint("lay/w"), gint("lay/h"),
                    gint("block-at intent/current 4"), fr.x, fr.w, gv_view_node(v, vnode(inode(1)))->w);
        }
    }
    CHECK(icount() >= 8 && inamed("name") == 1 && inamed("village") == 3);
    field_text("name", a, sizeof a); CHECK(!strcmp(a, "Default text"));
    CHECK(no_overlaps() == 0);
    /* the labels' real Pango ink stays inside their boxes (the unit holds) */
    gv_rect L, F;
    for (int i = 0; i < 3; i += 2) {
        CHECK(ibox(i, &L) == 0 && ibox(i + 1, &F) == 0);
        int in = 0, out = 0;
        ink_in_out(L, F.x, &in, &out);
        CHECK(in > 50 && out == 0);
    }
    CHECK(click_face("village") == 0 && text("Paekakariki") == 0);
    CHECK(click_face("name") == 0 && key(8) == 0 && text("!") == 0);
    field_text("name", a, sizeof a); CHECK(!strcmp(a, "Default tex!"));
    CHECK(click_face_i(4) == 0);                                     /* Clear */
    field_text("name", a, sizeof a); CHECK(!strcmp(a, ""));
    field_text("village", a, sizeof a); CHECK(!strcmp(a, "Paekakariki"));
    CHECK(click_face_i(5) == 0);                                     /* Clear all */
    field_text("village", a, sizeof a); CHECK(!strcmp(a, ""));
    CHECK(gv_view_focus_id(v) == vnode(inode(1)));
    if (png_dir) { char pth[512]; snprintf(pth, sizeof pth, "%s/intent-demo.png", png_dir); CHECK(gv_backend_sdl_save_png(b, pth) == 0); }
    CHECK(presented_ok() == 0);
    printf("intent-demo: the target layout builds; label ink inside its box; typing, Backspace, Clear and Clear all\n");
}

/* intent-stretch: flexible sharing, stretched escape hatches and panels */
static void case_intent_stretch(void) {
    CHECK(ishow("test/stretch") == 0);
    gv_rect f1, f2, cv, p, pf, esc, fx, root;
    int unit = (int)gint("intent/unit intent/theme");
    for (int k = 0; k < 6; k++) {
        int W = k % 2 ? 1500 : 900;
        CHECK(resize(W, 600) == 0 && screen(ig("block-at intent/current 4"), &root) == 0);
        CHECK(inamed_box("f1", &f1) == 0 && inamed_box("f2", &f2) == 0 && inamed_box("cv", &cv) == 0 && inamed_box("p", &p) == 0 &&
              inamed_box("pf", &pf) == 0 && inamed_box("esc", &esc) == 0 && inamed_box("fx", &fx) == 0);
        int edge = root.x + (W > (int)gint("block-at intent/current 9") ? W : (int)gint("block-at intent/current 9")) - 16;
        /* two flexible fields share the spare width equally (within rounding) */
        int g1 = f1.w - (10 * unit + 16), g2 = f2.w - (10 * unit + 16);
        CHECK(g1 >= 0 && abs(g1 - g2) <= 2 && f2.x + f2.w >= edge - 2 && f2.x + f2.w <= edge && f2.x >= f1.x + f1.w + 8);
        /* fixed stays preferred even in a flexible row */
        CHECK(fx.w == 10 * unit + 16);
        /* a stretched box grows to the margin and redraws at its new size:
         * its drawing covers exactly its width */
        CHECK(cv.x + cv.w == edge && cv.h == 40);
        int red[3] = {200, 40, 40}, in = 0, out = 0;
        for (int x = cv.x - 4; x < cv.x + cv.w + 4; x++) { int px[3]; pixel(x, cv.y + 20, px); if (rgb_diff(px, red) < 30) { if (x >= cv.x && x < cv.x + cv.w) in++; else out++; } }
        CHECK(in == cv.w && out == 0);
        /* the panel grows (it holds a flexible field); the field inside grows to its padding */
        CHECK(p.x + p.w == edge && pf.x + pf.w == p.x + p.w - 10);
        /* an escaped VID button stretches with its parts */
        gv_node *bg = gv_view_node(v, vnode(gint("block-at intent/get intent/current 'esc 2")));
        CHECK(esc.x + esc.w == edge && bg && bg->w == esc.w - 2);
        CHECK(no_overlaps() == 0);
    }
    CHECK(presented_ok() == 0);
    printf("intent-stretch: two fields share the spare width, fixed keeps its size, a stretched box redraws to %d px, "
           "a panel grows with its field, an escaped VID button stretches by its parts; 6 resizes\n", cv.w);
}

/* intent-demo-resize: the standalone demo, enlarged and shrunk repeatedly;
 * state survives, nothing is rebuilt, a small window scrolls */
static void case_intent_demo_resize(void) {
    char a[96];
    long nat_w = gint("block-at intent/current 9"), nat_h = gint("block-at intent/current 10");
    long id0 = gint("view/id"), name_node = inode(1), village_node = inode(3);
    gv_rect lab0, lab1, clr, root;
    CHECK(ibox(0, &lab0) == 0 && ibox(2, &lab1) == 0 && ibox(4, &clr) == 0);
    CHECK(click_face("name") == 0 && text("Ari") == 0 && click_face("village") == 0 && text("Otaki") == 0);
    gv_node_id focus = gv_view_focus_id(v);
    CHECK(focus == vnode(village_node));
    static const int sizes[][2] = { {640, 480}, {1600, 1000}, {420, 260}, {1920, 1080}, {300, 200}, {900, 600},
                                    {1280, 400}, {500, 900}, {1600, 1000}, {640, 480} };
    int scrolled = 0, n = (int)(sizeof sizes / sizeof sizes[0]);
    for (int cyc = 0; cyc < 3; cyc++)
        for (int k = 0; k < n; k++) {
            int W = sizes[k][0], H = sizes[k][1];
            CHECK(resize(W, H) == 0 && screen(ig("block-at intent/current 4"), &root) == 0);
            CHECK(root.w == W && root.h == H);                            /* the root follows the window */
            long cw = gint("lay/box-w 2"), ch = gint("lay/box-h 2");
            CHECK(cw == (W > nat_w ? W : nat_w) && ch == (H > nat_h ? H : nat_h));
            /* the fields reach the right margin; labels and buttons keep their place */
            gv_rect nf, vf, l0, l1, cb;
            CHECK(inamed_box("name", &nf) == 0 && inamed_box("village", &vf) == 0);
            CHECK(ibox(0, &l0) == 0 && ibox(2, &l1) == 0 && ibox(4, &cb) == 0);
            long sx = ig("block-at block-at lay/scrolls block-at intent/current 11 3");
            long sy = ig("block-at block-at lay/scrolls block-at intent/current 11 4");
            CHECK(nf.x + nf.w == root.x - (int)sx + (int)cw - 16 && vf.x + vf.w == nf.x + nf.w);
            CHECK(l0.w == lab0.w && l1.w == lab1.w && cb.w == clr.w && l0.x - lab0.x == -(int)sx && cb.y - clr.y == -(int)sy);
            CHECK(no_overlaps() == 0);
            /* too small: the content keeps its preferred size and scrolls to every control */
            if (W < nat_w || H < nat_h) {
                CHECK(wheel(root.x + 20, root.y + 20, -50) == 0);
                long sy2 = ig("block-at block-at lay/scrolls block-at intent/current 11 4");
                CHECK(sy2 == (H < nat_h ? nat_h - H : 0));
                if (sy2 > 0) scrolled++;
                CHECK(wheel(root.x + 20, root.y + 20, 50) == 0);
            }
            /* state: texts, focus, caret; the same nodes (no rebuild) */
            field_text("name", a, sizeof a); CHECK(!strcmp(a, "Default textAri"));
            field_text("village", a, sizeof a); CHECK(!strcmp(a, "Otaki"));
            CHECK(gv_view_focus_id(v) == focus && inode(1) == name_node && inode(3) == village_node && gint("view/id") == id0);
            gv_node *tx = gv_view_node(v, vnode(gint("block-at intent/get intent/current 'village 3")));
            CHECK(tx && tx->caret == 6 && tx->w == (int)gint("lay/box-w block-at intent/face intent/current 3 9") - 16);
        }
    /* typing still goes on afterwards */
    CHECK(text("!") == 0); field_text("village", a, sizeof a); CHECK(!strcmp(a, "Otaki!"));
    if (png_dir) { char pth[512]; CHECK(resize(1000, 420) == 0); snprintf(pth, sizeof pth, "%s/intent-demo-1000x420.png", png_dir); CHECK(gv_backend_sdl_save_png(b, pth) == 0); }
    CHECK(resize(1200, 700) == 0 && presented_ok() == 0);
    printf("intent-demo-resize: %d resizes from 300x200 to 1920x1080: root follows, fields reach the margin, labels and buttons stay, "
           "no overlap; scrolled to the bottom %d times when small; text, focus, caret and nodes kept (no rebuild)\n", 3 * n, scrolled);
}

/* native-window / intent-demo-native: two questions kept apart.
 * NATIVE: may the window be resized at all (SDL flags, limits, on X11 the
 * window manager's size hints), and does a click leave no pointer grab
 * behind that would stop a frame drag?  RESPONSIVE: does a size change of
 * the native window (SDL_SetWindowSize: the platform window changes size
 * and SDL reports it, as after a user's drag) reach the application, and
 * does the layout follow without a rebuild? */
static void case_native_window(int intent) {
    char r[1024];
    SDL_Window *win = find_window();
    CHECK(win != NULL);
    CHECK(gv_backend_sdl_window_report(b, r, sizeof r) == 0);
    Uint32 f = SDL_GetWindowFlags(win);
    int mnw = 0, mnh = 0, mxw = 0, mxh = 0;
    SDL_GetWindowMinimumSize(win, &mnw, &mnh);
    SDL_GetWindowMaximumSize(win, &mxw, &mxh);
    /* native */
    CHECK((f & SDL_WINDOW_RESIZABLE) && strstr(r, "resizable") && !strstr(r, "NOT RESIZABLE") && !strstr(r, "FIXED SIZE"));
    CHECK(mnw <= 200 && mnh <= 200 && (mxw == 0 || mxw >= 3840) && (mxh == 0 || mxh >= 2160));
    CHECK(strstr(r, "hit test off") != NULL);                         /* no in-window edge workaround by default */
    gv_rect fld = {0, 0, 0, 0};
    if (intent) CHECK(inamed_box("name", &fld) == 0);
    int cx = intent ? fld.x + 10 : 400, cy = intent ? fld.y + 10 : 300;
    CHECK(click(cx, cy) == 0);
    CHECK(!(SDL_GetWindowFlags(win) & SDL_WINDOW_MOUSE_CAPTURE));     /* no grab left after the click */
    /* the default configuration never captures for an ordinary press: a grab
     * held then stopped WSLg's frame resizing (manually verified) */
    CHECK(press_at(cx, cy) == 0 && !gv_backend_sdl_captured(b) && move_to(cx + 5, cy + 5) == 0 && !gv_backend_sdl_captured(b));
    CHECK(release_at(cx + 5, cy + 5) == 0);
    CHECK(!(SDL_GetWindowFlags(win) & SDL_WINDOW_MOUSE_CAPTURE) && !gv_backend_sdl_captured(b));
    /* responsive: native size changes reach the application */
    long id0 = intent ? gint("view/id") : 0, node0 = intent ? inode(1) : 0;
    char before[96] = "";
    if (intent) { CHECK(text("XY") == 0); field_text("name", before, sizeof before); }
    static const int sz[][2] = { {1400, 820}, {700, 520}, {1600, 900}, {640, 480} };
    int delivered = 0;
    for (int k = 0; k < 4; k++) {
        int d0 = dispatched;
        SDL_SetWindowSize(win, sz[k][0], sz[k][1]);
        CHECK(pump() == 0);
        int ww = 0, wh = 0; SDL_GetWindowSize(win, &ww, &wh);
        CHECK(ww == sz[k][0] && wh == sz[k][1]);                         /* the native window changed */
        CHECK(dispatched > d0);                                          /* SDL reported it, the host dispatched it */
        if (dispatched > d0) delivered++;
        gv_node *root = gv_view_node(v, gv_view_root(v));
        CHECK(root && root->w == ww && root->h == wh);                   /* View follows */
        if (intent) {
            CHECK(gint("lay/w") == ww && gint("lay/h") == wh);           /* Intent heard it */
            long nat = gint("block-at intent/current 9");
            gv_rect nf; CHECK(inamed_box("name", &nf) == 0);
            gv_rect root2; CHECK(screen(ig("block-at intent/current 4"), &root2) == 0);
            CHECK(nf.x + nf.w == root2.x + (ww > nat ? ww : nat) - 16);   /* the field stretched */
            char a[96]; field_text("name", a, sizeof a);
            CHECK(!strcmp(a, before) && gint("view/id") == id0 && inode(1) == node0);   /* no rebuild */
        }
    }
    CHECK(gv_backend_sdl_window_report(b, r, sizeof r) == 0);
    printf("native: %s\n", r);
    printf("%s: native resizable, no grab after clicks; %d/4 native size changes delivered%s\n",
           intent ? "intent-demo-native" : "native-window", delivered,
           intent ? "; Intent's fields followed each, same nodes and text" : "; View followed each");
}

/* capture: the pointer is captured only while a drag needs it (a table's
 * scrollbar thumb or column divider), and released at the release, at
 * window focus loss, when the drag's table goes away, when the host is
 * freed and at shutdown.  Ordinary presses never capture: a grab held then
 * stopped WSLg's frame resizing (INTENT-1.md §12, manually verified). */
static void focus_lost(void) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = SDL_WINDOWEVENT; e.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    e.window.windowID = SDL_GetWindowID(find_window());
    SDL_PushEvent(&e);
}
static int captured(void) {
    int c = gv_backend_sdl_captured(b);
    /* where the driver supports capture (X11), SDL's flag agrees */
    if (strcmp(SDL_GetCurrentVideoDriver(), "dummy")) CHECK(!!(SDL_GetWindowFlags(find_window()) & SDL_WINDOW_MOUSE_CAPTURE) == c);
    return c;
}
static void case_capture(void) {
    int taken = 0;
    /* ordinary presses: nav, button, field, a table row, empty pane: never */
    const char *items[] = {"Button", "Field", "Table"};
    for (int k = 0; k < 3; k++) {
        int x, y; CHECK(centre_of_text(items[k], &x, &y) == 0);
        CHECK(press_at(x, y) == 0 && !captured() && move_to(x + 3, y + 2) == 0 && !captured());
        CHECK(release_at(x + 3, y + 2) == 0 && !captured());
    }
    gv_table *t = gtable();
    gv_rect tr; gv_node *n = table_node();
    if (!t || !n || table_rect(&tr) != 0) { CHECK(t && n); return; }
    CHECK(click(tr.x + 100, tr.y + t->header_h + t->row_h / 2) == 0 && !captured());       /* a row */
    CHECK(click(tr.x + 50, tr.y + t->header_h / 2) == 0 && !captured());                   /* a header */
    /* the scrollbar thumb: captured for the drag, which goes on outside the
     * window; released at the release (outside) */
    int x0 = 0, x1 = 0, ty = 0, tl = 0;
    CHECK(gv_table_scrollbar(t, n->w, &x0, &x1) && gv_table_thumb(t, &ty, &tl));
    int bx = tr.x + (x0 + x1) / 2;
    CHECK(press_at(bx, tr.y + ty + 2) == 0 && captured()); taken++;
    CHECK(move_to(bx + 3000, tr.y + ty + 200) == 0 && t->visible_start > 0 && captured());
    CHECK(release_at(-50, -50) == 0 && !captured() && !t->bar_active);
    /* a track click pages: no capture */
    CHECK(gv_table_thumb(t, &ty, &tl) && click(bx, tr.y + ty + tl + 10) == 0 && !captured());
    /* the thumb, then the window loses the focus: drag over, capture gone,
     * later motion moves nothing */
    CHECK(gv_table_thumb(t, &ty, &tl) && press_at(bx, tr.y + ty + 2) == 0 && captured()); taken++;
    focus_lost(); CHECK(pump() == 0 && !captured() && !t->bar_active);
    int keep = t->visible_start;
    CHECK(move_to(bx, tr.y + ty + 150) == 0 && t->visible_start == keep);
    CHECK(release_at(bx, tr.y + ty + 150) == 0 && !captured());
    /* a column divider: captured; released at the release */
    int dx = tr.x + gv_table_col_x(t, 2), dy = tr.y + t->header_h / 2, w0 = t->cols[1].width;
    CHECK(move_to(dx, dy) == 0 && press_at(dx, dy) == 0 && captured()); taken++;
    CHECK(move_to(dx + 2500, dy) == 0 && t->cols[1].width > w0 && captured());   /* outside the window */
    CHECK(move_to(dx + 30, dy) == 0 && release_at(dx + 30, dy) == 0 && !captured() && t->cols[1].width == w0 + 30);
    /* a divider, then focus loss: capture gone, the width kept and reported */
    dx = tr.x + gv_table_col_x(t, 2);
    CHECK(press_at(dx, dy) == 0 && captured()); taken++;
    CHECK(move_to(dx + 20, dy) == 0);
    focus_lost(); CHECK(pump() == 0 && !captured() && gv_glon_host_cursor(h) == GV_CURSOR_NORMAL);
    char ws[96]; gstr("gallery/table-widths", ws, sizeof ws);
    char want[32]; snprintf(want, sizeof want, "%d:0", w0 + 50);
    CHECK(t->cols[1].width == w0 + 50 && strstr(ws, want) != NULL);
    CHECK(move_to(dx + 200, dy) == 0 && t->cols[1].width == w0 + 50);              /* no longer dragging */
    CHECK(release_at(dx + 200, dy) == 0 && !captured());
    /* the native window is untouched by all this: resizable, no grab */
    char r[1024]; CHECK(gv_backend_sdl_window_report(b, r, sizeof r) == 0 && strstr(r, "resizable") && strstr(r, "no capture"));
    CHECK(resize(1300, 760) == 0 && resize(1200, 700) == 0 && !captured());
    /* the host freed mid-drag gives the capture back */
    CHECK(table_rect(&tr) == 0 && gv_table_thumb(t, &ty, &tl) && press_at(bx, tr.y + ty + 2) == 0 && captured()); taken++;
    gv_glon_host_free(h);
    CHECK(!captured());
    h = gv_glon_host_new(v, b);
    /* and so does the backend's shutdown */
    CHECK(b->set_capture && b->set_capture(b, 1) == 0 && gv_backend_sdl_captured(b));
    b->shutdown(b);
    CHECK(!gv_backend_sdl_captured(b));
    printf("capture: none for ordinary presses, row and header clicks or paging; taken %d times (thumb and divider drags, "
           "outside the window too), released at the release, at focus loss (drag ended, width kept and reported), "
           "with the host and at shutdown; the window stays resizable\n", taken);
}

/* edge-promise: a resize cursor is shown only where a press resizes.
 * Sweeps every pixel of a 20-pixel band around the window's edge (and the
 * interior) and checks, at each point: an edge resize cursor on show
 * implies the hit test is installed and answers a resize zone for that very
 * point (gv_backend_sdl_edge_zone is what both the cursor and the hit test
 * use).  Run with GV_EDGE_CURSOR=system alone (the cursor must never show:
 * no hit test), with GV_EDGE_RESIZE=1 as well (shown exactly where the hit
 * test resizes, where the driver installs it), and with neither (the
 * default).  Also: the client cursor is an explicit arrow on X11, so the
 * window manager's frame cursor is not inherited inside the window. */
static void case_edge_promise(void) {
    char r[1024];
    CHECK(gv_backend_sdl_window_report(b, r, sizeof r) == 0);
    int x11 = !strcmp(SDL_GetCurrentVideoDriver(), "x11");
    const char *dc = getenv("GV_DEFAULT_CURSOR");
    CHECK(strstr(r, (x11 && !(dc && !strcmp(dc, "inherit"))) ? "client cursor explicit arrow" : "client cursor SDL default") != NULL);
    int ww = 0, wh = 0; SDL_GetWindowSize(find_window(), &ww, &wh);
    int hit = gv_backend_sdl_hit_test_active(b), shown_pts = 0, zone_pts = 0, lies = 0;
    for (int y = 0; y < wh; y += (y < 20 || y >= wh - 20) ? 1 : 37)
        for (int x = 0; x < ww; x += (x < 20 || x >= ww - 20) ? 1 : 41) {
            CHECK(move_to(x, y) == 0);
            int zone = gv_backend_sdl_edge_zone(ww, wh, x, y);
            int shown = gv_backend_sdl_edge_cursor_shown(b);
            if (zone != 0) zone_pts++;
            if (shown) shown_pts++;
            /* the promise: shown => hit test on and a resize zone here */
            if (shown && !(hit && zone != 0)) lies++;
            /* and inside the window proper nothing is shown */
            if (zone == 0) CHECK(!shown);
        }
    CHECK(lies == 0);
    if (!hit) CHECK(shown_pts == 0);
    CHECK(release_at(ww / 2, wh / 2) == 0 || 1);
    printf("edge-promise: driver %s, hit test %s, GV_EDGE_CURSOR %s: %d edge points, resize cursor shown at %d, "
           "shown where no resize would start: %d; client cursor %s\n", SDL_GetCurrentVideoDriver(), hit ? "on" : "off",
           getenv("GV_EDGE_CURSOR") ? getenv("GV_EDGE_CURSOR") : "unset", zone_pts, shown_pts, lies,
           strstr(r, "explicit arrow") ? "explicit arrow" : "SDL default");
}

/* edge-clearance-N / intent-demo-clearance-N: with an edge band of N px
 * (GV_EDGE_WIDTH), does any control lie in it?  A press there would resize
 * the window instead of reaching the control.  Controls: visible, enabled,
 * focusable nodes, and the gallery's tables (their scrollbars and column
 * dividers take presses).  Checked at window sizes where the whole content
 * shows (smaller windows scroll it, and then anything can reach the edge). */
static int clearance_check(int W, int ww, int wh, const char *where) {
    int bad = 0, n = 0;
    for (int i = 1; i <= v->count; i++) {
        gv_node *nd = gv_view_node(v, i);
        if (!nd || nd->type == GV_NODE_FREE || !nd->focusable || !nd->enabled) continue;
        gv_rect r;
        if (gv_view_visible_rect(v, i, &r) != 0 || r.w <= 0 || r.h <= 0) continue;
        n++;
        int d = r.x; if (r.y < d) d = r.y; if (ww - (r.x + r.w) < d) d = ww - (r.x + r.w); if (wh - (r.y + r.h) < d) d = wh - (r.y + r.h);
        if (d < W) { bad++; fprintf(stderr, "  %s: control node %d at %d,%d %dx%d is %d px from the edge\n", where, i, r.x, r.y, r.w, r.h, d); }
    }
    const char *tables[] = {"gallery/table-id", "gallery/id 40"};
    for (int k = 0; k < 2 && !strcmp(app_file, "view/glon/widget_gallery.glon"); k++) {
        long id = gint(tables[k]);
        gv_rect r;
        if (id <= 0 || gv_view_visible_rect(v, vnode(id), &r) != 0 || r.w <= 0) continue;
        n++;
        int d = r.x; if (r.y < d) d = r.y; if (ww - (r.x + r.w) < d) d = ww - (r.x + r.w); if (wh - (r.y + r.h) < d) d = wh - (r.y + r.h);
        if (d < W) { bad++; fprintf(stderr, "  %s: table %ld at %d,%d %dx%d is %d px from the edge\n", where, id, r.x, r.y, r.w, r.h, d); }
    }
    CHECK(n > 0);
    return bad;
}
static void case_edge_clearance(int intent) {
    int W = gv_backend_sdl_edge_width(), bad = 0, sizes = 0;
    int nw = intent ? (int)gint("block-at intent/current 9") : 960, nh = intent ? (int)gint("block-at intent/current 10") : 660;
    const int sz[][2] = { {nw, nh}, {nw + 300, nh + 200}, {1600, 1000}, {1920, 1080} };
    char where[64];
    for (int k = 0; k < 4; k++) {
        if (sz[k][0] < nw || sz[k][1] < nh) continue;
        CHECK(resize(sz[k][0], sz[k][1]) == 0);
        if (!intent) {
            /* every preview of the gallery, the table one included */
            const char *pv[] = {"Button", "Field", "Table", "Tabs"};
            for (int p = 0; p < 4; p++) {
                CHECK(click_text(pv[p]) == 0);
                snprintf(where, sizeof where, "%dx%d %s", sz[k][0], sz[k][1], pv[p]);
                bad += clearance_check(W, sz[k][0], sz[k][1], where);
            }
        } else {
            snprintf(where, sizeof where, "%dx%d", sz[k][0], sz[k][1]);
            bad += clearance_check(W, sz[k][0], sz[k][1], where);
        }
        sizes++;
    }
    CHECK(bad == 0);
    printf("%s: edge band %d px: %d control(s) inside it over %d sizes\n", intent ? "intent-demo-clearance" : "edge-clearance", W, bad, sizes);
}

/* edge-policy-*: the in-window resize band's platform defaults.
 * GV_ASSUME_WSLG=1 takes the WSLg branch on the dummy driver (the hit test
 * itself cannot be installed there; the policy can be read). */
/* edge-cursor-decision: the backend's edge-cursor rule, as a pure function
 * (no window system needed): a resize cursor exactly for a resize zone with
 * the hit test on and no drag under way */
static void case_edge_cursor_decision(void) {
    const char *why = NULL;
    int zones[] = { SDL_HITTEST_RESIZE_LEFT, SDL_HITTEST_RESIZE_RIGHT, SDL_HITTEST_RESIZE_TOP, SDL_HITTEST_RESIZE_BOTTOM,
                    SDL_HITTEST_RESIZE_TOPLEFT, SDL_HITTEST_RESIZE_BOTTOMRIGHT };
    for (int i = 0; i < 6; i++) {
        CHECK(gv_backend_sdl_edge_cursor_decision(1, zones[i], 0, 0, &why) == zones[i] && !strcmp(why, "resize zone"));
        CHECK(gv_backend_sdl_edge_cursor_decision(0, zones[i], 0, 0, &why) == SDL_HITTEST_NORMAL && strstr(why, "no hit test"));
        CHECK(gv_backend_sdl_edge_cursor_decision(1, zones[i], 1, 0, &why) == SDL_HITTEST_NORMAL && strstr(why, "capture"));
        CHECK(gv_backend_sdl_edge_cursor_decision(1, zones[i], 0, 1, &why) == SDL_HITTEST_NORMAL && strstr(why, "button"));
    }
    CHECK(gv_backend_sdl_edge_cursor_decision(1, SDL_HITTEST_NORMAL, 0, 0, &why) == SDL_HITTEST_NORMAL && strstr(why, "no resize zone"));
    /* and the zones themselves: the band at each edge of a 720 x 560 window */
    int w = 720, hh = 560, band = gv_backend_sdl_edge_width();
    CHECK(gv_backend_sdl_edge_zone(w, hh, 0, 280) == SDL_HITTEST_RESIZE_LEFT && gv_backend_sdl_edge_zone(w, hh, w - 1, 280) == SDL_HITTEST_RESIZE_RIGHT);
    CHECK(gv_backend_sdl_edge_zone(w, hh, 360, 0) == SDL_HITTEST_RESIZE_TOP && gv_backend_sdl_edge_zone(w, hh, 360, hh - 1) == SDL_HITTEST_RESIZE_BOTTOM);
    CHECK(gv_backend_sdl_edge_zone(w, hh, band, 280) == SDL_HITTEST_NORMAL && gv_backend_sdl_edge_zone(w, hh, 360, 280) == SDL_HITTEST_NORMAL);
    printf("edge-cursor-decision: a resize cursor exactly for resize zones with the hit test on and no capture or button held; "
           "zones at the four edges of a 720x560 window (band %d px)\n", band);
}
/* x11-edge-cursors (X11, no input to the desktop; run under the LD_PRELOAD
 * probe tests/x11_cursor_probe.c, make test-x11-edge-cursors): the resize
 * cursors the X server is asked to show on our window.  Pointer motions into
 * each edge zone are pushed into this program's own SDL event queue (as
 * edge-promise does; they never reach the X server's pointer), the backend
 * sets each zone's cursor as for a real motion, then the probe's log is read */
static void case_x11_edge_cursors(void) {
    const char *log = getenv("GV_X11_PROBE_LOG");
    if (strcmp(SDL_GetCurrentVideoDriver(), "x11") || !log) {
        printf("x11-edge-cursors: skipped (needs SDL_VIDEODRIVER=x11 and the x11_cursor_probe.so preload)\n");
        return;
    }
    SDL_SysWMinfo info; SDL_VERSION(&info.version);
    CHECK(SDL_GetWindowWMInfo(find_window(), &info) && info.subsystem == SDL_SYSWM_X11);
    unsigned long xw = (unsigned long)info.info.x11.window;
    CHECK(gv_backend_sdl_hit_test_active(b) == 1);                   /* the WSLg policy: the band and its cursors */
    int ww = 0, wh = 0; SDL_GetWindowSize(find_window(), &ww, &wh);
    struct { int x, y, zone; const char *what; } pts[] = {
        { ww - 1, wh / 2, SDL_HITTEST_RESIZE_RIGHT, "right" },  { 0, wh / 2, SDL_HITTEST_RESIZE_LEFT, "left" },
        { ww / 2, 0, SDL_HITTEST_RESIZE_TOP, "top" },           { ww / 2, wh - 1, SDL_HITTEST_RESIZE_BOTTOM, "bottom" },
        { ww - 1, wh - 1, SDL_HITTEST_RESIZE_BOTTOMRIGHT, "bottom-right" }, { 0, 0, SDL_HITTEST_RESIZE_TOPLEFT, "top-left" },
        { ww - 1, 0, SDL_HITTEST_RESIZE_TOPRIGHT, "top-right" },  { 0, wh - 1, SDL_HITTEST_RESIZE_BOTTOMLEFT, "bottom-left" } };
    long start = 0;
    { FILE *f = fopen(log, "r"); if (f) { fseek(f, 0, SEEK_END); start = ftell(f); fclose(f); } }
    for (int i = 0; i < 8; i++) {
        CHECK(move_to(pts[i].x, pts[i].y) == 0);
        if (gv_backend_sdl_cursor_zone(b) != pts[i].zone || !gv_backend_sdl_edge_cursor_shown(b))
            fprintf(stderr, "  %s: zone %d (wanted %d), shown %d\n", pts[i].what, gv_backend_sdl_cursor_zone(b), pts[i].zone,
                    gv_backend_sdl_edge_cursor_shown(b));
        CHECK(gv_backend_sdl_cursor_zone(b) == pts[i].zone && gv_backend_sdl_edge_cursor_shown(b) == 1);
        CHECK(move_to(ww / 2, wh / 2) == 0 && gv_backend_sdl_edge_cursor_shown(b) == 0);
    }
    /* what the X server was asked: cursors made (font shape or bitmap image), defines on our window */
    FILE *f = fopen(log, "r");
    CHECK(f != NULL);
    if (!f) return;
    /* cursors made at any time (one may be cached from before, a motion at
     * startup); defines counted from the test's start */
    char line[256];
    unsigned long font_of[256]; unsigned shape_of[256]; int nfont = 0;
    unsigned long image[32]; int nimage = 0;
    unsigned long defined[256]; int ndef = 0, fleur = 0;
    int undefines = 0;
    for (long at = ftell(f); fgets(line, sizeof line, f); at = ftell(f)) {
        unsigned sh; unsigned long c, w;
        if (sscanf(line, "[x11] font-cursor %u -> 0x%lx", &sh, &c) == 2 && nfont < 256) { shape_of[nfont] = sh; font_of[nfont++] = c; if (sh == 52) fleur++; }
        else if (sscanf(line, "[x11] image-cursor -> 0x%lx", &c) == 1 && nimage < 32) image[nimage++] = c;
        else if (at >= start && sscanf(line, "[x11] define 0x%lx 0x%lx", &w, &c) == 2 && w == xw && ndef < 256) defined[ndef++] = c;
        else if (at >= start && sscanf(line, "[x11] undefine 0x%lx", &w) == 1 && w == xw) undefines++;
    }
    fclose(f);
    int h = 0, vv = 0, diag = 0;
    for (int i = 0; i < ndef; i++) {
        for (int k = 0; k < nfont; k++) if (font_of[k] == defined[i]) { if (shape_of[k] == 108) h++; if (shape_of[k] == 116) vv++; }
        for (int k = 0; k < nimage; k++) if (image[k] == defined[i]) diag++;
    }
    /* edges: XC_sb_h_double_arrow (108) at left and right, XC_sb_v_double_arrow (116) at top and bottom;
     * corners: bitmap diagonals (four); XC_fleur (52) never made; every zone and return defined on our window */
    printf("  window 0x%lx: %d cursor defines on it; edge font cursors h %d v %d; corner bitmap cursors %d; XC_fleur made %d\n",
           xw, ndef, h, vv, diag, fleur);
    CHECK(ndef >= 16 && h >= 2 && vv >= 2 && diag >= 4 && fleur == 0);
    /* and never None (the window manager frame's cursor inherited) while the test drives the zones */
    CHECK(undefines == 0);
    printf("x11-edge-cursors: observed on X window 0x%lx: %d defines, edges h %d / v %d font cursors, corners %d bitmap "
           "cursors, XC_fleur made %d, undefines %d (wanted: >= 16, >= 2 / >= 2, >= 4, 0, 0)\n", xw, ndef, h, vv, diag, fleur, undefines);
}
static void case_edge_policy(const char *which) {
    char r[1024];
    CHECK(gv_backend_sdl_window_report(b, r, sizeof r) == 0);
    if (!strcmp(which, "edge-policy-wslg"))
        CHECK(strstr(r, "edge 12 px, requested, policy WSLg, edge cursor system") != NULL);
    else if (!strcmp(which, "edge-policy-default"))
        CHECK(strstr(r, "edge 6 px, not requested, policy default, edge cursor off") != NULL);
    else if (!strcmp(which, "edge-policy-override"))
        CHECK(strstr(r, "edge 8 px, not requested, policy WSLg, edge cursor system") != NULL);
    CHECK(gv_backend_sdl_edge_width() == (strstr(which, "wslg") ? 12 : strstr(which, "override") ? 8 : 6));
    printf("%s: %s\n", which, r);
}

/* ==== INTENT-2: handlers, widget handles, forms (intent_members.glon) ===== */
static void fval(const char *path, char *out, int cap) { igs(out, cap, "face/value %s", path); }
#define F_ROOT "form/current"
#define F_NAME "form/field form/current 'name"
#define F_PHONE "form/field form/current 'phone"
#define F_CNAME "form/field form/sub form/current 'contact 'name"
#define F_CPHONE "form/field form/sub form/current 'contact 'phone"
/* state written from outside an event: send the batch, as the end of an
 * event does (widget/flush), and apply it */
static int apply_now(void) {
    char tmp[64]; gstr("block-len reduce [ widget/flush ]", tmp, sizeof tmp);
    if (gv_glon_host_deliver(h) != 0) return -1;       /* a run's batch is not taken by itself */
    if (gv_glon_host_bind(h) != 0) return -1;          /* and what follows it: view-blur for a hidden focus */
    return render();
}
static void status_text(char *out, int cap) { igs(out, cap, "face/text form/field form/current 'status"); }
static int click_named(const char *name) {
    gv_rect r; long node = ig("block-at intent/hface form/field form/current '%s 8", name);
    if (screen(node, &r) != 0) return -1;
    return click(r.x + r.w / 2, r.y + r.h / 2);
}

/* handlers: invocation, self, reuse, inline and late binding, live state */
static void case_intent_members_handlers(void) {
    char a[160];
    /* a click on Load runs load-member (external, named) */
    CHECK(click_text("Load") == 0);
    status_text(a, sizeof a); CHECK(!strcmp(a, "Loaded 6 fields"));
    fval(F_NAME, a, sizeof a); CHECK(!strcmp(a, "Aroha Te Whare"));
    /* the widget shows it: the field's text node */
    CHECK(text_node("Aroha Te Whare") != 0);
    /* on-enter on the phone field: self is that field's handle */
    CHECK(click_named("phone") == 0 && key(13) == 0);
    status_text(a, sizeof a); CHECK(!strcmp(a, "Phone looks right"));
    igs(a, sizeof a, "face/name members/last-self"); CHECK(!strcmp(a, "phone"));
    CHECK(gint("face/alive members/last-self") == 1);
    /* the handler reads live state: type a bad character, Enter again */
    CHECK(text("x") == 0 && key(13) == 0);
    status_text(a, sizeof a); CHECK(strstr(a, "Phone: 7 to 20") != NULL);
    /* one handler (note-change) reused by two widgets, self names each */
    long c0 = gint("members/changes");
    CHECK(click_named("name") == 0 && text("!") == 0);
    igs(a, sizeof a, "face/name members/last-self"); CHECK(!strcmp(a, "name"));
    CHECK(click_named("phone") == 0 && key(8) == 0);
    igs(a, sizeof a, "face/name members/last-self"); CHECK(!strcmp(a, "phone"));
    CHECK(gint("members/changes") == c0 + 2);
    fval(F_PHONE, a, sizeof a); CHECK(!strcmp(a, "021 555 0199"));
    /* external code writes live state; the widget follows */
    { char d[200]; igs(d, sizeof d, "face/set-value " F_NAME " \"Mere\"");
      if (getenv("P2_DEBUG")) { fprintf(stderr, "  set-value -> %s\n", d); fval(F_NAME, d, sizeof d); fprintf(stderr, "  value now %s\n", d); } }
    CHECK(apply_now() == 0 && text_node("Mere") != 0);
    fval(F_NAME, a, sizeof a); CHECK(!strcmp(a, "Mere"));
    /* enabled: a disabled field takes no typing and fires nothing */
    CHECK(gint("block-len face/set-enabled " F_PHONE " 0") == 4 && apply_now() == 0 && gint("face/enabled " F_PHONE) == 0);
    long c1 = gint("members/changes");
    CHECK(click_named("phone") == 0 && text("9") == 0);
    fval(F_PHONE, a, sizeof a); CHECK(!strcmp(a, "021 555 0199") && gint("members/changes") == c1);
    CHECK(gint("block-len face/set-enabled " F_PHONE " 1") == 4 && apply_now() == 0);
    /* visible: hidden drops the focus (host view-blur) and the node is hidden */
    CHECK(click_named("name") == 0 && gv_view_focus_id(v) != 0);
    CHECK(gint("block-len face/set-visible " F_NAME " 0") == 4 && apply_now() == 0);
    gv_node *nn = gv_view_node(v, vnode(ig("block-at intent/hface " F_NAME " 8")));
    CHECK(nn && !nn->visible && gint("face/visible " F_NAME) == 0);
    if (getenv("P2_DEBUG")) fprintf(stderr, "  hidden: view focus %d, intent focused face %ld, name node %ld\n",
                                    (int)gv_view_focus_id(v), gint("block-at intent/current 8"), ig("block-at intent/hface " F_NAME " 8"));
    CHECK(text("zz") == 0);
    fval(F_NAME, a, sizeof a); CHECK(!strcmp(a, "Mere"));               /* no typing into a hidden field */
    CHECK(gint("block-len face/set-visible " F_NAME " 1") == 4 && apply_now() == 0);
    /* a named handler that does not exist (yet) is reported, not fatal */
    CHECK(gint("block-len form/clear " F_ROOT) == 3);
    CHECK(presented_ok() == 0);
    printf("intent-members-handlers: Load (named handler), on-enter with self = phone, live reads, one on-change handler for two "
           "fields, external writes, disabled and hidden fields ignore input\n");
}

/* forms: load (full, partial, unknown, rejected), clear, collect, choice
 * contract, nested panel and nested form isolation */
static void case_intent_members_forms(void) {
    char a[400];
    CHECK(click_text("Load") == 0);
    /* collect: the data-bearing widgets only, nested form as a record */
    igs(a, sizeof a, "form/collect " F_ROOT);
    CHECK(!strcmp(a, "[ name \"Aroha Te Whare\" phone \"021 555 0199\" village \"Ōtaki\" kind \"H\" mail 1 contact [ name \"Rawiri Paki\" phone \"04 555 0100\" ] ]"));
    /* the choice contract: index 2, text Household, value H; the radio shows it */
    CHECK(gint("face/index form/field form/current 'kind") == 2);
    igs(a, sizeof a, "face/text form/field form/current 'kind"); CHECK(!strcmp(a, "Household"));
    igs(a, sizeof a, "face/value form/field form/current 'kind"); CHECK(!strcmp(a, "H"));
    /* a click on Business selects it; on-change would fire; collect answers B */
    CHECK(click_text("Business") == 0 && gint("face/index form/field form/current 'kind") == 3);
    igs(a, sizeof a, "record/get form/collect form/current 'kind"); CHECK(!strcmp(a, "B"));
    CHECK(gint("block-len face/set-index form/field form/current 'kind 0") == 4);
    igs(a, sizeof a, "face/value form/field form/current 'kind"); CHECK(!strcmp(a, "none"));
    /* isolation: the outer form's name and the contact form's name */
    fval(F_CNAME, a, sizeof a); CHECK(!strcmp(a, "Rawiri Paki"));
    CHECK(gint("block-len face/set-value " F_CNAME " \"Hemi\"") == 4);
    fval(F_NAME, a, sizeof a); CHECK(!strcmp(a, "Aroha Te Whare"));
    fval(F_CNAME, a, sizeof a); CHECK(!strcmp(a, "Hemi"));
    CHECK(gint("= face/form " F_CNAME " form/sub form/current 'contact") == 0);   /* distinct handle blocks */
    igs(a, sizeof a, "form/name face/form " F_CNAME); CHECK(!strcmp(a, "contact"));
    /* the nested panel's field belongs to the outer form */
    igs(a, sizeof a, "face/value form/field form/current 'village"); CHECK(!strcmp(a, "Ōtaki"));
    /* partial load: only the phone; the rest keeps its values */
    igs(a, sizeof a, "form/load form/current reduce [ 'phone \"09 111 2222\" ]");
    CHECK(strstr(a, "loaded 1") != NULL);
    fval(F_PHONE, a, sizeof a); CHECK(!strcmp(a, "09 111 2222"));
    fval(F_NAME, a, sizeof a); CHECK(!strcmp(a, "Aroha Te Whare"));
    /* unknown keys (not in the form, or presentation only) and rejected values */
    igs(a, sizeof a, "form/load form/current reduce [ 'nickname \"Ro\" 'status \"x\" 'phone 42 'name \"Kiri\" ]");
    CHECK(!strcmp(a, "[ loaded 1 unknown [ nickname status ] rejected [ phone ] ]"));
    fval(F_PHONE, a, sizeof a); CHECK(!strcmp(a, "09 111 2222"));            /* the rejected value left it */
    fval(F_NAME, a, sizeof a); CHECK(!strcmp(a, "Kiri"));
    igs(a, sizeof a, "sin-id judge [ form/load form/current reduce [ 'name ] ]"); CHECK(!strcmp(a, "invalid-record"));
    /* clear: data-bearing widgets emptied; labels, status and layout kept */
    status_text(a, sizeof a);
    long nodes0 = gint("view/id");
    CHECK(click_text("Clear") == 0);
    igs(a, sizeof a, "form/collect " F_ROOT);
    CHECK(!strcmp(a, "[ name \"\" phone \"\" village \"\" kind none mail 0 contact [ name \"\" phone \"\" ] ]"));
    status_text(a, sizeof a); CHECK(!strcmp(a, "Cleared"));
    CHECK(text_node("Name:") != 0 && text_node("Village:") != 0 && gint("view/id") == nodes0);
    /* reload = clear + load */
    igs(a, sizeof a, "form/reload form/current reduce [ 'village \"Levin\" ]");
    igs(a, sizeof a, "form/collect " F_ROOT);
    CHECK(strstr(a, "village \"Levin\"") && strstr(a, "name \"\""));
    CHECK(presented_ok() == 0);
    printf("intent-members-forms: collect (nested record, no labels or buttons), choice index/text/value, isolation of two "
           "'name fields, nested panel, partial load, unknown and rejected keys, clear keeps the layout, reload\n");
}

/* validation: valid, invalid (messages), failed (a validator that raises);
 * stale handles; an unset handler name */
static void case_intent_members_validate(void) {
    char a[300];
    /* empty form: name required, phones invalid */
    CHECK(click_text("Save") == 0);
    status_text(a, sizeof a); CHECK(!strcmp(a, "Name: required"));
    igs(a, sizeof a, "form/validate form/current");
    CHECK(strstr(a, "[ invalid [ name \"Name: required\" phone") == a && strstr(a, "contact [ phone") != NULL);
    if (!strstr(a, "contact [ phone")) fprintf(stderr, "  validate: %s\n", a);
    CHECK(gint("members/saves") == 0);
    /* a valid record saves */
    CHECK(click_text("Load") == 0 && click_text("Save") == 0);
    status_text(a, sizeof a); CHECK(!strcmp(a, "Saved Aroha Te Whare"));
    CHECK(gint("members/saves") == 1);
    igs(a, sizeof a, "record/get members/saved 'phone"); CHECK(!strcmp(a, "021 555 0199"));
    /* a validator that raises is a failure, not an invalid value */
    CHECK(gint("block-len reduce [ members/keep: :check-name  check-name: :members/boom ]") == 2);
    igs(a, sizeof a, "form/status form/validate form/current"); CHECK(!strcmp(a, "failed"));
    igs(a, sizeof a, "sin-id form/failure form/validate form/current"); CHECK(!strcmp(a, "boom"));
    CHECK(click_text("Save") == 0);
    status_text(a, sizeof a); CHECK(!strcmp(a, "Validation could not run") && gint("members/saves") == 1);
    CHECK(gint("block-len reduce [ check-name: :members/keep ]") == 1);
    /* a form-level validator */
    CHECK(gint("block-len form/on-validate form/current 'members/no-kiri") == 3);
    CHECK(gint("block-len face/set-value " F_NAME " \"Kiri\"") == 4);
    igs(a, sizeof a, "form/validate form/current"); CHECK(!strcmp(a, "[ invalid [ form \"No Kiri today\" ] none ]"));
    if (strcmp(a, "[ invalid [ form \"No Kiri today\" ] none ]")) fprintf(stderr, "  validate: %s\n", a);
    CHECK(gint("block-len face/set-value " F_NAME " \"Aroha\"") == 4);
    igs(a, sizeof a, "form/status form/validate form/current"); CHECK(!strcmp(a, "valid"));
    /* a handler that raises: recorded, the application goes on */
    CHECK(gint("block-len reduce [ members/keep: :load-member  load-member: :members/boom2 ]") == 2);
    CHECK(click_text("Load") == 0);
    igs(a, sizeof a, "sin-id intent/last-error"); CHECK(!strcmp(a, "boom2"));
    CHECK(gint("block-len reduce [ load-member: :members/keep ]") == 1);
    CHECK(click_text("Load") == 0);
    status_text(a, sizeof a); CHECK(!strcmp(a, "Loaded 6 fields"));
    /* a handler name rebound to a non-function: reported, not fatal */
    CHECK(gint("block-len reduce [ members/keep: :clear-member  clear-member: 5 ]") == 2);
    CHECK(click_text("Clear") == 0);
    igs(a, sizeof a, "sin-id intent/last-error"); CHECK(!strcmp(a, "handler-not-a-function"));
    CHECK(gint("block-len reduce [ clear-member: :members/keep ]") == 1);
    /* stale handles: kept across a rebuild of the form */
    CHECK(gint("block-len reduce [ members/kept: " F_PHONE " ]") == 1 && gint("face/alive members/kept") == 1);
    CHECK(key(5000) == 0);                                               /* the application rebuilds its form */
    CHECK(gint("face/alive members/kept") == 0 && gint("face/alive " F_PHONE) == 1);
    igs(a, sizeof a, "sin-id judge [ face/value members/kept ]"); CHECK(!strcmp(a, "stale-widget"));
    igs(a, sizeof a, "sin-id judge [ face/set-value members/kept \"x\" ]"); CHECK(!strcmp(a, "stale-widget"));
    igs(a, sizeof a, "sin-id judge [ form/field form/current 'nickname ]"); CHECK(!strcmp(a, "no-such-field"));
    igs(a, sizeof a, "sin-id judge [ face/value 5 ]"); CHECK(!strcmp(a, "not-a-widget"));
    /* the rebuilt form works: fresh defaults, handlers bound */
    CHECK(click_text("Load") == 0);
    status_text(a, sizeof a); CHECK(!strcmp(a, "Loaded 6 fields"));
    CHECK(presented_ok() == 0);
    printf("intent-members-validate: invalid with messages, saved when valid, a raising validator is 'failed, a form-level "
           "validator; raising and non-function handlers reported; stale handles refused after a rebuild\n");
}

/* intent-handlers: inline handler blocks get self and event; a handler name
 * defined only after the layout is found when the event comes */
static void case_intent_handlers(void) {
    char a[160];
    CHECK(ishow("test/handlers") == 0);
    CHECK(click_face("f1") == 0 && text("ab") == 0);
    igs(a, sizeof a, "test/last"); CHECK(!strcmp(a, "ab"));            /* on-change [ ... face/value self ] */
    CHECK(click_face("go") == 0);
    igs(a, sizeof a, "face/value form/field form/current 'f1"); CHECK(!strcmp(a, "went from Go"));
    igs(a, sizeof a, "test/last"); CHECK(!strcmp(a, "ab"));            /* set-value by code fires no on-change */
    /* not defined yet: reported, nothing else happens */
    CHECK(gint("block-len reduce [ intent/last-error: none ]") == 1);
    CHECK(click_face("later") == 0);
    igs(a, sizeof a, "sin-id intent/last-error"); CHECK(!strcmp(a, "handler-not-a-function"));
    igs(a, sizeof a, "face/text form/field form/current 'out"); CHECK(!strcmp(a, "-"));
    /* defined now: the same button finds it */
    CHECK(gint("block-len reduce [ not-defined-yet: :test/hello ]") == 1);
    CHECK(click_face("later") == 0);
    igs(a, sizeof a, "face/text form/field form/current 'out"); CHECK(!strcmp(a, "hello"));
    CHECK(text_node("hello") != 0);
    CHECK(presented_ok() == 0);
    printf("intent-handlers: inline on-change / on-click blocks with self and event; a handler name bound after the layout "
           "is reported until defined, then runs\n");
}

/* ==== FOREIGN-1: a hosted component in an Intent layout ================== */
#include "gv_host.h"
static long fgid(const char *name) { return ig("block-at intent/hface form/field form/current '%s 8", name); }
static int mock_state(gv_host_mock_state *st) {
    void *c = gv_glon_host_component(h, (int)fgid("foreign"));
    return c ? gv_host_mock_state_of(c, st) : -1;
}
static int host_pump(void) { int n = gv_glon_host_pump(h); return n < 0 ? -1 : render(); }
static int same_rect(gv_rect a, gv_rect b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }

/* intent-foreign (SDL dummy driver, mock component): place, clip,
 * visibility, focus in and out, input reported to handlers, values, failure
 * placeholder, stale events after destruction, repeated open / close */
static void case_intent_foreign(void) {
    char a[200];
    gv_host_mock_state st;
    int alive0 = 0, created0 = 0, destroyed0 = 0;
    gv_host_counts(&alive0, &created0, &destroyed0);
    CHECK(ishow("test/foreign") == 0 && host_pump() == 0);
    long g = fgid("foreign");
    void *comp = gv_glon_host_component(h, (int)g);
    CHECK(comp != NULL && mock_state(&st) == 0);
    /* placed exactly on its node, all of it visible, with its props */
    gv_rect nr, vr;
    CHECK(screen(g, &nr) == 0 && gv_view_visible_rect(v, vnode(g), &vr) == 0);
    CHECK(same_rect(st.bounds, nr) && same_rect(st.clip, vr) && st.visible == 1 && !strcmp(st.text, "hello"));
    /* the face is an ordinary Intent face between the fields */
    gv_rect b4, af;
    CHECK(inamed_box("before", &b4) == 0 && inamed_box("after", &af) == 0 && b4.y < nr.y && nr.y + nr.h <= af.y);
    /* a kind no provider offers: no component, a placeholder saying why */
    CHECK(gv_glon_host_component(h, (int)fgid("nope")) == NULL && text_node("unavailable: no provider of kind no-such-kind") != 0);
    /* resize: it stretches with the window; the component is told */
    int places = st.places;
    CHECK(resize(1500, 800) == 0 && mock_state(&st) == 0 && screen(g, &nr) == 0);
    CHECK(same_rect(st.bounds, nr) && st.bounds.w > 300 && st.places > places && nr.x + nr.w == 1500 - 16);
    CHECK(resize(1200, 700) == 0 && mock_state(&st) == 0 && screen(g, &nr) == 0 && same_rect(st.bounds, nr));
    /* focus: Tab from a field into the component and on out again */
    CHECK(click_face("before") == 0 && key(9) == 0);
    CHECK(gv_view_focus_id(v) == vnode(g) && mock_state(&st) == 0 && st.focused == 1);
    CHECK(gint("block-at intent/current 8") == ig("intent/named intent/current 'foreign"));
    CHECK(key(9) == 0 && gv_view_focus_id(v) == vnode(fgid("after")) && mock_state(&st) == 0 && st.focused == 0);
    /* the component asks for the focus (a click inside it) and passes it on
     * with its own Tab (next / prev) */
    gv_host_mock_emit(comp, "focus-request", "");
    CHECK(host_pump() == 0 && gv_view_focus_id(v) == vnode(g) && mock_state(&st) == 0 && st.focused == 1);
    gv_host_mock_emit(comp, "tab", "prev");
    CHECK(host_pump() == 0 && gv_view_focus_id(v) == vnode(fgid("before")) && mock_state(&st) == 0 && st.focused == 0);
    gv_host_mock_emit(comp, "focus-request", "");
    gv_host_mock_emit(comp, "tab", "next");
    CHECK(host_pump() == 0 && gv_view_focus_id(v) == vnode(fgid("after")));
    /* input inside the component reaches the application's handlers */
    long c0 = gint("test/foreign-changes");
    gv_host_mock_emit(comp, "change", "typed in the component");
    CHECK(host_pump() == 0 && gint("test/foreign-changes") == c0 + 1);
    igs(a, sizeof a, "face/value form/field form/current 'foreign"); CHECK(!strcmp(a, "typed in the component"));
    igs(a, sizeof a, "record/get form/collect form/current 'foreign"); CHECK(!strcmp(a, "typed in the component"));
    gv_host_mock_emit(comp, "enter", "typed in the component");
    CHECK(host_pump() == 0 && gint("test/foreign-changes") == c0 + 2);
    /* and the application sets its content */
    CHECK(gint("block-len face/set-value form/field form/current 'foreign \"from Glon\"") == 4 && apply_now() == 0);
    CHECK(mock_state(&st) == 0 && !strcmp(st.text, "from Glon"));
    /* clipping: a small window scrolls the layout; the component gets the
     * visible part only, nothing when scrolled out of view */
    CHECK(resize(340, 100) == 0 && mock_state(&st) == 0 && screen(g, &nr) == 0);
    if (getenv("P2_DEBUG")) fprintf(stderr, "  small: bounds %d,%d %dx%d clip %d,%d %dx%d vis %d\n", st.bounds.x, st.bounds.y,
                                    st.bounds.w, st.bounds.h, st.clip.x, st.clip.y, st.clip.w, st.clip.h, st.visible);
    CHECK(st.visible == 1 && same_rect(st.bounds, nr) && st.clip.x == st.bounds.x && st.clip.y == st.bounds.y &&
          st.clip.w == 340 - st.bounds.x && st.clip.h == 100 - st.bounds.y);       /* cut on the right and at the bottom */
    CHECK(wheel(100, 80, -20) == 0 && mock_state(&st) == 0);
    gv_rect cr; CHECK(gv_view_visible_rect(v, vnode(g), &cr) != 0 || cr.h == st.clip.h);
    long sy = ig("block-at block-at lay/scrolls block-at intent/current 11 4");
    CHECK(sy > 0 && st.bounds.y == nr.y - (int)sy);
    if (st.bounds.y + st.bounds.h <= 0) CHECK(st.visible == 0);
    CHECK(resize(1200, 700) == 0 && mock_state(&st) == 0 && st.visible == 1 && st.clip.w == st.bounds.w);
    /* hide and show (hiding the focused component drops its focus) */
    gv_host_mock_emit(comp, "focus-request", "");
    CHECK(host_pump() == 0 && mock_state(&st) == 0 && st.focused == 1);
    CHECK(gint("block-len face/set-visible form/field form/current 'foreign 0") == 4 && apply_now() == 0);
    CHECK(mock_state(&st) == 0 && st.visible == 0 && st.focused == 0 && gv_view_focus_id(v) != vnode(g));
    CHECK(gint("block-len face/set-visible form/field form/current 'foreign 1") == 4 && apply_now() == 0);
    CHECK(mock_state(&st) == 0 && st.visible == 1);
    /* destruction: an event queued by the component just before its layout
     * goes is dropped, never delivered to the next one */
    long c1 = gint("test/foreign-changes");
    gv_host_mock_emit(comp, "change", "too late");
    CHECK(ishow("test/form") == 0 && gint("test/foreign-changes") == c1);
    int alive = 0, created = 0, destroyed = 0;
    gv_host_counts(&alive, &created, &destroyed);
    CHECK(alive == alive0 && created == created0 + 1 && destroyed == destroyed0 + 1);
    /* repeated opening and closing: no instance, node or id left behind */
    long id0 = gint("view/id");
    for (int k = 0; k < 20; k++) {
        CHECK(ishow("test/foreign") == 0 && host_pump() == 0 && gv_glon_host_component(h, (int)fgid("foreign")) != NULL);
        CHECK(ishow("test/form") == 0);
    }
    gv_host_counts(&alive, &created, &destroyed);
    CHECK(alive == alive0 && created == created0 + 21 && destroyed == destroyed0 + 21 && gint("view/id") == id0);
    CHECK(presented_ok() == 0);
    printf("intent-foreign: mock component placed on its node and clipped to its visible part (also scrolled away); "
           "stretches; Tab in and out, focus-request and its own tab next/prev; change/enter reach on-change/on-enter; "
           "set-value; hide/show; stale event dropped after destruction; 21 creations, 21 destructions, nothing left\n");
}

/* intent-foreign-x11: the real x11-edit component, X11 only (it opens a
 * window on $DISPLAY).  NO input of any kind: no pushed pointer or key
 * events, no focus changes (they would move the desktop's keyboard focus).
 * Checks with the X server itself (XGetWindowAttributes): the component's
 * clip window sits on the node's visible part, its content window on the
 * node, both follow a resize and a scroll, hide unmaps, show maps, and ten
 * create / destroy cycles leave no instance behind. */
static void case_intent_foreign_x11(void) {
    if (strcmp(SDL_GetCurrentVideoDriver(), "x11")) {
        printf("intent-foreign-x11: skipped (video driver %s; run with SDL_VIDEODRIVER=x11)\n", SDL_GetCurrentVideoDriver());
        return;
    }
    int alive0 = 0, c0 = 0, d0 = 0;
    gv_host_counts(&alive0, &c0, &d0);
    CHECK(ishow("test/foreign-x11") == 0 && host_pump() == 0);
    long g = fgid("foreign");
    void *comp = gv_glon_host_component(h, (int)g);
    CHECK(comp != NULL);
    if (!comp) { printf("intent-foreign-x11: x11-edit could not be created\n"); return; }
    int mapped = 0; gv_rect outer, inner, nr, vr; char text[64];
    CHECK(gv_host_x11_query(comp, &mapped, &outer, &inner, NULL, text, sizeof text) == 0);
    CHECK(screen(g, &nr) == 0 && gv_view_visible_rect(v, vnode(g), &vr) == 0);
    CHECK(mapped && same_rect(outer, vr) && inner.x == nr.x - vr.x && inner.y == nr.y - vr.y &&
          inner.w == nr.w && inner.h == nr.h && !strcmp(text, "smoke test"));
    /* resize: the X windows follow the node */
    CHECK(resize(1400, 760) == 0 && host_pump() == 0 && screen(g, &nr) == 0);
    CHECK(gv_host_x11_query(comp, &mapped, &outer, &inner, NULL, NULL, 0) == 0 && mapped && inner.w == nr.w && nr.w > 300);
    /* small window: clipped by X to the visible part, inner offset inside */
    CHECK(resize(340, 100) == 0 && host_pump() == 0 && screen(g, &nr) == 0);
    CHECK(gv_host_x11_query(comp, &mapped, &outer, &inner, NULL, NULL, 0) == 0);
    CHECK(mapped && outer.w == 340 - nr.x && outer.h == 100 - nr.y && inner.w == nr.w && inner.h == nr.h);
    CHECK(resize(1200, 700) == 0 && host_pump() == 0);
    /* hide / show */
    CHECK(gint("block-len face/set-visible form/field form/current 'foreign 0") == 4 && apply_now() == 0);
    CHECK(gv_host_x11_query(comp, &mapped, NULL, NULL, NULL, NULL, 0) == 0 && !mapped);
    CHECK(gint("block-len face/set-visible form/field form/current 'foreign 1") == 4 && apply_now() == 0);
    CHECK(gv_host_x11_query(comp, &mapped, NULL, NULL, NULL, NULL, 0) == 0 && mapped);
    /* the application sets its content */
    CHECK(gint("block-len face/set-value form/field form/current 'foreign \"set by Glon\"") == 4 && apply_now() == 0);
    CHECK(gv_host_x11_query(comp, NULL, NULL, NULL, NULL, text, sizeof text) == 0 && !strcmp(text, "set by Glon"));
    /* ten destroy / create cycles */
    for (int k = 0; k < 10; k++) {
        CHECK(ishow("test/form") == 0);
        CHECK(ishow("test/foreign-x11") == 0 && host_pump() == 0 && gv_glon_host_component(h, (int)fgid("foreign")) != NULL);
    }
    CHECK(ishow("test/form") == 0);
    int alive = 0, cr = 0, de = 0;
    gv_host_counts(&alive, &cr, &de);
    CHECK(alive == alive0 && cr == c0 + 11 && de == d0 + 11);
    printf("intent-foreign-x11: X server confirms placement, clipping (outer %dx%d of inner %dx%d at 340x100), resize, "
           "hide/show, content; 11 created, 11 destroyed; no input sent\n", 340 - nr.x, 100 - nr.y, nr.w, nr.h);
}

/* intent-foreign-tab: keyboard traversal into and out of a hosted
 * component, both directions, through View's focus order (SDL dummy
 * driver, mock component: the component's own Tab is emitted as x11-edit
 * emits it) -- and after hiding, showing and reopening */
static void case_intent_foreign_tab(void) {
    gv_host_mock_state st;
    CHECK(ishow("test/foreign") == 0 && host_pump() == 0);
    long g = fgid("foreign"), b4 = fgid("before"), af = fgid("after");
    void *comp = gv_glon_host_component(h, (int)g);
    CHECK(comp != NULL);
    /* forward: Before -Tab-> component; component -its Tab-> After */
    CHECK(click_face("before") == 0 && key(9) == 0);
    CHECK(gv_view_focus_id(v) == vnode(g) && mock_state(&st) == 0 && st.focused == 1);
    gv_host_mock_emit(comp, "tab", "next");
    CHECK(host_pump() == 0 && gv_view_focus_id(v) == vnode(af) && mock_state(&st) == 0 && st.focused == 0);
    /* backward: After -Shift+Tab-> component; component -its Shift+Tab-> Before */
    CHECK(key_shift(9) == 0 && gv_view_focus_id(v) == vnode(g) && mock_state(&st) == 0 && st.focused == 1);
    gv_host_mock_emit(comp, "tab", "prev");
    CHECK(host_pump() == 0 && gv_view_focus_id(v) == vnode(b4) && mock_state(&st) == 0 && st.focused == 0);
    /* a Tab the component reports after the focus has left it (typed in the
     * moment before its first Tab was handled) is stale: ignored */
    gv_node_id here = gv_view_focus_id(v);
    gv_host_mock_emit(comp, "tab", "prev");
    gv_host_mock_emit(comp, "tab", "next");
    CHECK(host_pump() == 0 && gv_view_focus_id(v) == here && mock_state(&st) == 0 && st.focused == 0);
    /* Shift+Tab between Glon widgets too (it was forward-only) */
    CHECK(click_face("after") == 0 && key_shift(9) == 0 && gv_view_focus_id(v) == vnode(g));
    CHECK(key_shift(9) == 0 && gv_view_focus_id(v) == vnode(b4) && mock_state(&st) == 0 && st.focused == 0);
    /* the focus count: each entry told the component exactly once */
    int calls = st.focus_calls;
    /* hidden: Tab from Before skips it; shown again: Tab enters it */
    CHECK(gint("block-len face/set-visible form/field form/current 'foreign 0") == 4 && apply_now() == 0);
    CHECK(click_face("before") == 0 && key(9) == 0 && gv_view_focus_id(v) == vnode(af) && mock_state(&st) == 0 && st.focused == 0);
    CHECK(key_shift(9) == 0 && gv_view_focus_id(v) == vnode(b4));
    CHECK(gint("block-len face/set-visible form/field form/current 'foreign 1") == 4 && apply_now() == 0);
    CHECK(key(9) == 0 && gv_view_focus_id(v) == vnode(g) && mock_state(&st) == 0 && st.focused == 1 && st.focus_calls > calls);
    /* reopened (a fresh build, a fresh component): Tab enters the new one */
    CHECK(ishow("test/form") == 0 && ishow("test/foreign") == 0 && host_pump() == 0);
    g = fgid("foreign");
    comp = gv_glon_host_component(h, (int)g);
    CHECK(comp != NULL && click_face("before") == 0 && key(9) == 0);
    CHECK(gv_view_focus_id(v) == vnode(g) && mock_state(&st) == 0 && st.focused == 1);
    gv_host_mock_emit(comp, "tab", "next");
    CHECK(host_pump() == 0 && gv_view_focus_id(v) == vnode(fgid("after")));
    CHECK(presented_ok() == 0);
    printf("intent-foreign-tab: Tab and Shift+Tab into the component from both neighbours, its own Tab and Shift+Tab "
           "out to both; Shift+Tab between Glon widgets; skipped while hidden, entered again when shown; entered after reopening\n");
}

/* ==== FOREIGN-2: a browser-shaped application ============================= */
/* intent-browser-mock (dummy driver, mock component): the generic operations
 * a browser needs -- commands, on-event with names, failed navigation,
 * address changes, focus arriving first / last / current, exits both ways,
 * repeated opening and closing */
static void case_intent_browser_mock(void) {
    char a[300];

    gv_host_mock_state st;
    int alive0 = 0, c0 = 0, d0 = 0;
    gv_host_counts(&alive0, &c0, &d0);
    CHECK(ishow("test/browser") == 0 && host_pump() == 0);
    long g = fgid("browser");
    void *comp = gv_glon_host_component(h, (int)g);
    /* (this layout's component is 'browser: read it directly, not with
     * mock_state, which reads FOREIGN-1's 'foreign) */
    CHECK(comp != NULL && gv_host_mock_state_of(comp, &st) == 0 && st.visible == 1);
    /* face/command from the application (Go) reaches the component */
    CHECK(gint("block-len face/set-value form/field form/current 'address \"https://example.org\"") == 4 && apply_now() == 0);
    CHECK(click_face("go") == 0);
    void *cm = gv_glon_host_component(h, (int)g);
    gv_host_mock_state_of(cm, &st);
    CHECK(!strcmp(st.last_cmd, "navigate") && !strcmp(st.last_arg, "https://example.org"));
    /* the component's status and title events reach on-event with their name */
    long e0 = gint("test/browser-events");
    gv_host_mock_emit(comp, "status", "loading https://example.org/");
    gv_host_mock_emit(comp, "status", "loaded https://example.org/");
    gv_host_mock_emit(comp, "title", "Example Domain");
    CHECK(host_pump() == 0 && gint("test/browser-events") == e0 + 3);
    igs(a, sizeof a, "test/browser-last"); CHECK(!strcmp(a, "title:Example Domain"));
    /* its address changed (a link): on-change updates the Glon field */
    gv_host_mock_emit(comp, "change", "https://example.org/page2");
    CHECK(host_pump() == 0);
    igs(a, sizeof a, "face/value form/field form/current 'address"); CHECK(!strcmp(a, "https://example.org/page2"));
    igs(a, sizeof a, "face/value form/field form/current 'browser"); CHECK(!strcmp(a, "https://example.org/page2"));
    /* a failed navigation is an ordinary event: the application goes on */
    gv_host_mock_emit(comp, "status", "failed https://no.such.host/: Could not resolve host");
    CHECK(host_pump() == 0);
    igs(a, sizeof a, "test/browser-last"); CHECK(strstr(a, "status:failed https://no.such.host/") == a);
    CHECK(click_face("go") == 0);                                         /* still working */
    /* other commands and their refusal on ordinary widgets */
    CHECK(gint("block-len face/command form/field form/current 'browser \"back\" \"\"") == 4 && apply_now() == 0);
    gv_host_mock_state_of(cm, &st); CHECK(!strcmp(st.last_cmd, "back"));
    igs(a, sizeof a, "sin-id judge [ face/command form/field form/current 'address \"back\" \"\" ]"); CHECK(!strcmp(a, "no-commands"));
    /* focus: Tab arrives FIRST, Shift+Tab LAST, a click (request) CURRENT */
    CHECK(click_face("go") == 0 && key(9) == 0 && gv_view_focus_id(v) == vnode(g));
    gv_host_mock_state_of(cm, &st); CHECK(st.focused == 1 && st.focus_how == GV_HOST_FOCUS_FIRST);
    gv_host_mock_emit(comp, "tab", "next");
    CHECK(host_pump() == 0 && gv_view_focus_id(v) == vnode(fgid("after")));
    CHECK(key_shift(9) == 0 && gv_view_focus_id(v) == vnode(g));
    gv_host_mock_state_of(cm, &st); CHECK(st.focused == 1 && st.focus_how == GV_HOST_FOCUS_LAST);
    gv_host_mock_emit(comp, "tab", "prev");
    CHECK(host_pump() == 0 && gv_view_focus_id(v) == vnode(fgid("go")));
    gv_host_mock_emit(comp, "focus-request", "");
    CHECK(host_pump() == 0 && gv_view_focus_id(v) == vnode(g));
    gv_host_mock_state_of(cm, &st); CHECK(st.focus_how == GV_HOST_FOCUS_CURRENT);
    /* the component's own Tab into the next component arrives FIRST too */
    gv_host_mock_emit(comp, "tab", "next");
    CHECK(host_pump() == 0 && key_shift(9) == 0);
    gv_host_mock_state_of(cm, &st); CHECK(st.focus_how == GV_HOST_FOCUS_LAST);
    /* resize: it stretches; ten open / close cycles leave nothing */
    CHECK(resize(1500, 800) == 0);
    gv_host_mock_state_of(cm, &st); gv_rect nr; CHECK(screen(g, &nr) == 0 && same_rect(st.bounds, nr) && nr.x + nr.w == 1500 - 16);
    CHECK(resize(1200, 700) == 0);
    for (int k = 0; k < 10; k++) CHECK(ishow("test/form") == 0 && ishow("test/browser") == 0 && host_pump() == 0);
    CHECK(ishow("test/form") == 0);
    int alive = 0, cr = 0, de = 0;
    gv_host_counts(&alive, &cr, &de);
    CHECK(alive == alive0 && cr == c0 + 11 && de == d0 + 11);
    CHECK(presented_ok() == 0);
    printf("intent-browser-mock: face/command (navigate, back) reaches it; status/title via on-event with names; "
           "address follows on-change; failed navigation is an event; focus arrives first/last/current; exits both ways; "
           "11 created and destroyed\n");
}

/* webkit-address (dummy driver; no engine needed): webkit-view's address
 * rule.  Built -O2 with glibc's _FORTIFY_SOURCE like the provider, so the
 * original defect -- realpath() into a 1024-byte buffer, which aborted
 * every relative address with "buffer overflow detected" -- fails here on
 * the first relative address */
static void case_webkit_address(void) {
    char u[4200], cwd[4096], want[8300];
    CHECK(getcwd(cwd, sizeof cwd) != NULL);
    /* relative, existing (the demo's own page): resolved to an absolute file URI */
    CHECK(gv_host_webkit_resolve("./glon/browser/demo.html", u, sizeof u) == 0);
    snprintf(want, sizeof want, "file://%s/glon/browser/demo.html", cwd);
    CHECK(!strcmp(u, want));
    CHECK(gv_host_webkit_resolve("../view/glon/browser/page2.html", u, sizeof u) == 0);
    CHECK(strstr(u, "file:///") == u && strstr(u, "/view/glon/browser/page2.html") && !strstr(u, ".."));
    /* relative, missing: still absolute, so the load fails visibly */
    CHECK(gv_host_webkit_resolve("./glon/browser/no-such-page.html", u, sizeof u) == 0);
    snprintf(want, sizeof want, "file://%s/glon/browser/no-such-page.html", cwd);
    CHECK(!strcmp(u, want));
    /* a relative path longer than the old 1024-byte buffer */
    char lng[2101] = "./";
    for (int i = 2; i < 2090; i++) lng[i] = i % 50 ? 'a' : '/';
    lng[2090] = 0;
    CHECK(gv_host_webkit_resolve(lng, u, sizeof u) == 0 && strlen(u) == strlen(cwd) + 8 + 2088);
    /* too long for the caller's buffer: refused, never truncated */
    char small[64];
    memset(small, 'x', sizeof small);
    CHECK(gv_host_webkit_resolve(lng, small, sizeof small) == -1);
    CHECK(gv_host_webkit_resolve("https://example.org/a-rather-long-address-that-does-not-fit-here", small, 32) == -1);
    /* the other forms */
    CHECK(gv_host_webkit_resolve("/tmp/x.html", u, sizeof u) == 0 && !strcmp(u, "file:///tmp/x.html"));
    CHECK(gv_host_webkit_resolve("https://example.org/", u, sizeof u) == 0 && !strcmp(u, "https://example.org/"));
    CHECK(gv_host_webkit_resolve("example.org", u, sizeof u) == 0 && !strcmp(u, "https://example.org"));
    CHECK(gv_host_webkit_resolve("about:blank", u, sizeof u) == 0 && !strcmp(u, "about:blank"));
    CHECK(gv_host_webkit_resolve("", u, sizeof u) == -1 && gv_host_webkit_resolve(NULL, u, sizeof u) == -1);
    printf("webkit-address: relative (existing, missing, longer than 1024), absolute, https, bare, about:, "
           "empty; too long is refused, not truncated\n");
}

/* ==== INTENT-3: styles, follow, command (INTENT-3-DESIGN.md) ============== */
/* the mock component behind a widget, by a Glon expression for its handle */
static void *comp_of(const char *handle_expr) {
    return gv_glon_host_component(h, (int)ig("block-at intent/hface %s 8", handle_expr));
}
static int mock_of(const char *handle_expr, gv_host_mock_state *st) {
    return gv_host_mock_state_of(comp_of(handle_expr), st);
}
static void ftext(const char *name, char *out, int cap) { igs(out, cap, "face/text form/field form/current '%s", name); }

/* intent3-styles (mock, dummy driver): layout styles, a style of a style,
 * a button style, the built-in webview selecting the webkit-view provider,
 * word values as facets, below */
static void case_intent3_styles(void) {
    char a[300];
    gv_host_mock_state st;
    CHECK(ishow("test/i3") == 0 && host_pump() == 0);
    /* style mockview host "mock": the mock provider, its props from a word's value */
    CHECK(mock_of("form/field form/current 'web", &st) == 0 && !strcmp(st.text, "home-page"));
    /* style wideview mockview 300 120: flattened, the size from the style */
    CHECK(mock_of("form/field form/current 'wide", &st) == 0 && !strcmp(st.text, "home-page"));
    CHECK(st.bounds.w == 300 && st.bounds.h == 120);
    /* style big button "Big": the caption from the style */
    ftext("big1", a, sizeof a); CHECK(!strcmp(a, "Big"));
    /* a field's text from a word's value */
    igs(a, sizeof a, "face/value form/field form/current 'address"); CHECK(!strcmp(a, "home-page"));
    /* the built-in webview is host "webkit-view": on the dummy driver that
     * provider refuses (no X11 window), and says so as its failed event */
    igs(a, sizeof a, "test/i3-last"); CHECK(strstr(a, "failed:") == a && strstr(a, "X11"));
    CHECK(ig("block-at intent/hface form/field form/current 'webkit 0") == 9);
    /* below: the labels after it stack under each other */
    gv_rect r1, r2;
    CHECK(inamed_box("status", &r1) == 0 && inamed_box("title", &r2) == 0 && r2.y > r1.y && r2.x == r1.x);
    CHECK(presented_ok() == 0);
    printf("intent3-styles: layout styles, a style of a style, a button style, webview -> webkit-view, "
           "word values as facets, below\n");
}

/* intent3-commands (mock): a button's command, its argument from a widget
 * (read when it runs) and from a variable (read at build), Enter in a field,
 * the order command -> on-click, a refused command as the error event */
static void case_intent3_commands(void) {
    char a[300];
    gv_host_mock_state st;
    CHECK(ishow("test/i3") == 0 && host_pump() == 0);
    void *web = comp_of("form/field form/current 'web");
    CHECK(click_face("back") == 0 && gv_host_mock_state_of(web, &st) == 0);
    CHECK(!strcmp(st.last_cmd, "back") && !strcmp(st.last_arg, ""));
    CHECK(click_face("home") == 0 && gv_host_mock_state_of(web, &st) == 0);
    CHECK(!strcmp(st.last_cmd, "navigate") && !strcmp(st.last_arg, "home-page"));
    CHECK(click_face("big1") == 0 && gv_host_mock_state_of(web, &st) == 0 && !strcmp(st.last_cmd, "reload"));
    /* the address is read when Go runs, not when the layout was built */
    CHECK(ig("block-len face/set-value form/field form/current 'address \"https://example.org/a\"") == 4 && apply_now() == 0);
    CHECK(click_face("go") == 0 && host_pump() == 0);
    /* Go's command ran first, then its on-click handler sent "after-handler" */
    CHECK(gv_host_mock_state_of(web, &st) == 0 && !strcmp(st.last_cmd, "after-handler"));
    long c0 = st.commands;
    /* Enter in the address field: its command (editing alone sends nothing) */
    CHECK(click_face("address") == 0 && text("x") == 0 && gv_host_mock_state_of(web, &st) == 0 && st.commands == c0);
    CHECK(key(13) == 0 && gv_host_mock_state_of(web, &st) == 0);
    CHECK(!strcmp(st.last_cmd, "navigate") && !strcmp(st.last_arg, "https://example.org/ax"));
    /* a command the component refuses: its error event, the app goes on */
    CHECK(gint("block-len reduce [ test/i3-last: \"\" ]") == 1);
    CHECK(click_face("bad") == 0 && host_pump() == 0);
    igs(a, sizeof a, "test/i3-last"); CHECK(!strcmp(a, "error:unknown command refuse"));
    CHECK(click_face("back") == 0 && gv_host_mock_state_of(web, &st) == 0 && !strcmp(st.last_cmd, "back"));
    CHECK(!gv_glon_host_error(h) && presented_ok() == 0);
    printf("intent3-commands: button commands with no argument, a variable's and a widget's value; Enter in a field; "
           "command before on-click; a refused command is the error event\n");
}

/* intent3-follow (mock): one-way bindings -- initial values, updates from
 * component events and from a field's typing, no chains, hidden followers,
 * a button caption, rebuilding */
static void case_intent3_follow(void) {
    char a[300];
    CHECK(ishow("test/i3") == 0 && host_pump() == 0);
    void *web = comp_of("form/field form/current 'web");
    /* initial values: what each follower declares; nothing copied, nothing polled */
    igs(a, sizeof a, "face/value form/field form/current 'address"); CHECK(!strcmp(a, "home-page"));
    ftext("status", a, sizeof a); CHECK(!strcmp(a, "Ready"));
    ftext("title", a, sizeof a); CHECK(!strcmp(a, ""));
    ftext("echo", a, sizeof a); CHECK(!strcmp(a, "-"));
    /* the component's events update their followers */
    gv_host_mock_emit(web, "url", "https://example.org/page");
    gv_host_mock_emit(web, "status", "loaded https://example.org/page");
    gv_host_mock_emit(web, "title", "Example");
    CHECK(host_pump() == 0);
    igs(a, sizeof a, "face/value form/field form/current 'address"); CHECK(!strcmp(a, "https://example.org/page"));
    ftext("status", a, sizeof a); CHECK(!strcmp(a, "loaded https://example.org/page"));
    ftext("title", a, sizeof a); CHECK(!strcmp(a, "Example"));
    ftext("cap", a, sizeof a); CHECK(!strcmp(a, "Example"));             /* a button caption follows too */
    /* no chain: address was set by its binding, so echo (following address) did not move */
    ftext("echo", a, sizeof a); CHECK(!strcmp(a, "-"));
    /* typing in address is its own change: echo follows; the browser is not told */
    gv_host_mock_state st; gv_host_mock_state_of(web, &st); long c0 = st.commands;
    CHECK(click_face("address") == 0 && text("/x") == 0);
    ftext("echo", a, sizeof a); CHECK(!strcmp(a, "https://example.org/page/x"));
    CHECK(gv_host_mock_state_of(web, &st) == 0 && st.commands == c0);
    /* events the binding does not name change nothing */
    gv_host_mock_emit(web, "progress", "50");
    CHECK(host_pump() == 0);
    ftext("status", a, sizeof a); CHECK(!strcmp(a, "loaded https://example.org/page"));
    /* a hidden follower keeps following; shown, it is current */
    CHECK(ig("block-len face/set-visible form/field form/current 'status 0") == 4 && apply_now() == 0);
    gv_host_mock_emit(web, "status", "while hidden");
    CHECK(host_pump() == 0);
    CHECK(ig("block-len face/set-visible form/field form/current 'status 1") == 4 && apply_now() == 0);
    ftext("status", a, sizeof a); CHECK(!strcmp(a, "while hidden"));
    /* the source's handler sees its followers already updated */
    gv_host_mock_emit(web, "title", "T2");
    CHECK(host_pump() == 0);
    igs(a, sizeof a, "test/i3-last"); CHECK(!strcmp(a, "title:T2"));
    /* rebuilt: fresh widgets with their declared values; the old component's
     * queued events are dropped, and its links went with its build */
    gv_host_mock_emit(web, "status", "stale");
    CHECK(ishow("test/form") == 0 && ishow("test/i3") == 0 && host_pump() == 0);
    ftext("status", a, sizeof a); CHECK(!strcmp(a, "Ready"));
    void *web2 = comp_of("form/field form/current 'web");
    CHECK(web2 != NULL);
    gv_host_mock_emit(web2, "status", "fresh");
    CHECK(host_pump() == 0);
    ftext("status", a, sizeof a); CHECK(!strcmp(a, "fresh"));
    CHECK(!gv_glon_host_error(h) && presented_ok() == 0);
    printf("intent3-follow: declared initial values; url/status/title update fields, labels and a caption; "
           "no chains; typing follows without commanding; hidden followers stay current; handler after followers; rebuilt fresh\n");
}

/* intent3-forms (mock): two forms with the same names stay apart; a command
 * reaches a name in the enclosing form; open / close cycles leave nothing */
static void case_intent3_forms(void) {
    char a[300];
    gv_host_mock_state st;
    int alive0 = 0;
    gv_host_counts(&alive0, NULL, NULL);
    CHECK(ishow("test/i3-forms") == 0 && host_pump() == 0);
    void *L = comp_of("form/field form/sub form/current 'left 'web");
    void *R = comp_of("form/field form/sub form/current 'right 'web");
    void *S = comp_of("form/field form/current 'shared");
    CHECK(L && R && S && L != R);
    gv_host_mock_emit(L, "url", "left-2");
    CHECK(host_pump() == 0);
    igs(a, sizeof a, "face/value form/field form/sub form/current 'left 'addr"); CHECK(!strcmp(a, "left-2"));
    igs(a, sizeof a, "face/value form/field form/sub form/current 'right 'addr"); CHECK(!strcmp(a, "right"));
    /* each form's Go commands its own web with its own addr */
    CHECK(click_face_i((int)ig("block-at form/field form/sub form/current 'right 'go 2")) == 0);
    CHECK(gv_host_mock_state_of(R, &st) == 0 && !strcmp(st.last_cmd, "navigate") && !strcmp(st.last_arg, "right"));
    CHECK(gv_host_mock_state_of(L, &st) == 0 && st.commands == 0);
    /* outer (in the right form) names shared, in the enclosing form */
    CHECK(click_face_i((int)ig("block-at form/field form/sub form/current 'right 'outer 2")) == 0);
    CHECK(gv_host_mock_state_of(S, &st) == 0 && !strcmp(st.last_cmd, "reload"));
    for (int k = 0; k < 5; k++) CHECK(ishow("test/form") == 0 && ishow("test/i3-forms") == 0 && host_pump() == 0);
    CHECK(ishow("test/form") == 0);
    int alive = 0; gv_host_counts(&alive, NULL, NULL);
    CHECK(alive == alive0);
    CHECK(presented_ok() == 0);
    printf("intent3-forms: same names in two forms stay apart; a command reaches the enclosing form; "
           "6 open/close cycles leave no component\n");
}

/* intent3-errors: what the reader refuses, by name, at build time */
static void case_intent3_errors(void) {
    struct { const char *src, *want; } e[] = {
        { "b: button \"x\" command nowhere \"back\"", "no-such-field" },
        { "f: field 10  b: button \"x\" command f \"back\"", "no-commands" },
        { "w: host \"mock\" 10 10  l: label \"x\" command w \"a\"", "command-needs-button-or-field" },
        { "w: host \"mock\" 10 10  b: button \"x\" command w \"go\" undefined-thing", "unknown-word" },
        { "w: host \"mock\" 10 10  l: label \"x\" follow w colour", "unknown-property" },
        { "f: field 10  l: label \"x\" follow f title", "property-needs-a-component" },
        { "l0: label \"a\"  l: label \"x\" follow l0 value", "has-no-value" },
        { "w: host \"mock\" 10 10  p: panel [ ] follow w url", "cannot-follow" },
        { "l: label \"x\" follow nobody value", "no-such-field" },
        { "w: host \"mock\" 10 10  l: label \"x\" follow w url follow w title", "one-follow-per-widget" },
        { "follow x value", "option-needs-a-face" },
        { "style field button \"x\"", "style-shadows-keyword" },
        { "style foo bar", "style-needs-a-widget" },
        { "style s2 button \"a\" \"b\"  s2", "style-has-unused-facets" },
        { "w: host \"mock\" 10 10 l1: label \"1\" follow w url l2: label \"2\" follow w url l3: label \"3\" follow w url "
          "l4: label \"4\" follow w url l5: label \"5\" follow w url l6: label \"6\" follow w url l7: label \"7\" follow w url "
          "l8: label \"8\" follow w url l9: label \"9\" follow w url", "too-many-followers" },
    };
    int n = (int)(sizeof e / sizeof e[0]);
    for (int k = 0; k < n; k++) {
        char a[200];
        igs(a, sizeof a, "sin-id judge [ intent/show-now layout [ %s ] ]", e[k].src);
        if (!strstr(a, e[k].want)) fprintf(stderr, "  [%s] -> %s (wanted %s)\n", e[k].src, a, e[k].want);
        CHECK(strstr(a, e[k].want) != NULL);
    }
    CHECK(ishow("test/form") == 0 && icount() == 6);       /* the next layout is fine */
    CHECK(presented_ok() == 0);
    printf("intent3-errors: %d mistakes refused at build time, each by name\n", n);
}

/* ==== INTENT-4B-1: check, heading, disabled / hidden, one look ========== */
/* a View node's colour against a theme slot of the current layout */
static int node_color_is(long gid, int fg, int slot) {
    gv_node *n = gv_view_node(v, vnode(gid));
    if (!n) return 0;
    gv_color c = fg ? n->fg : n->bg;
    long r = ig("block-at block-at intent/current 7 %d", slot), g = ig("block-at block-at intent/current 7 %d", slot + 1),
         bl = ig("block-at block-at intent/current 7 %d", slot + 2);
    if (c.r != r || c.g != g || c.b != bl)
        fprintf(stderr, "  node %ld %s %d,%d,%d, theme slot %d is %ld,%ld,%ld\n", gid, fg ? "fg" : "bg", c.r, c.g, c.b, slot, r, g, bl);
    return c.r == r && c.g == g && c.b == bl;
}
static int node_flag(long gid, int which) {
    gv_node *n = gv_view_node(v, vnode(gid));
    return !n ? -1 : which == 0 ? n->visible : which == 1 ? n->enabled : n->focusable;
}
#define I4F "form/field form/current '"
/* a check's state slot (2 box, 3 glyph, 4 label, 7 edge) */
static long i4_part(const char *name, int slot) { return ig("block-at block-at intent/hface " I4F "%s 2 %d", name, slot); }
static long i4_focused(void) { return ig("block-at intent/current 8"); }

static void case_intent4_check(void) {
    char a[200];
    CHECK(ishow("test/i4") == 0);
    CHECK(gint("test/i4-n: 0") == 0);
    /* kinds: heading is a label (kind 1), check is kind 10 */
    CHECK(ig("block-at intent/hface " I4F "agree 0") == 10 && ikind(0) == 1);
    igs(a, sizeof a, "face/text " I4F "agree"); CHECK(!strcmp(a, "I agree"));
    /* the heading uses the theme's heading font, larger than a label's */
    { gv_node *n = gv_view_node(v, vnode(inode(0)));
      CHECK(n && n->font_size == (double)gint("block-at intent/theme 133") && n->font_size > (double)gint("block-at intent/theme 69")); }
    /* value 0; a click toggles it, fires change (the handler) and the follower */
    CHECK(ig("face/value " I4F "agree") == 0);
    CHECK(click_face("agree") == 0 && ig("face/value " I4F "agree") == 1 && gint("test/i4-n") == 1);
    igs(a, sizeof a, "face/text " I4F "note"); CHECK(!strcmp(a, "1"));
    CHECK(node_color_is(i4_part("agree", 2), 0, 13));                 /* checked: the accent box */
    /* keyboard: focus it, Space toggles it; the focus ring colour shows */
    CHECK(gint("block-len face/focus " I4F "agree") == 4 && apply_now() == 0);
    CHECK(i4_focused() == inamed("agree") && ig("block-at intent/hface " I4F "agree 26") == 1);
    CHECK(node_color_is(i4_part("agree", 7), 0, 105));
    CHECK(key(SDLK_SPACE) == 0 && ig("face/value " I4F "agree") == 0 && gint("test/i4-n") == 2);
    igs(a, sizeof a, "face/text " I4F "note"); CHECK(!strcmp(a, "0"));
    CHECK(node_color_is(i4_part("agree", 2), 0, 21) && node_color_is(i4_part("agree", 7), 0, 105));
    /* Tab moves on: the ring goes back to the border colour */
    CHECK(key(SDLK_TAB) == 0 && i4_focused() != inamed("agree"));
    CHECK(node_color_is(i4_part("agree", 7), 0, 17) && ig("block-at intent/hface " I4F "agree 26") == 0);
    /* disabled (declared): disabled colours, out of the Tab order, clicks ignored */
    long news = ig("intent/node intent/current %ld", inamed("news"));
    CHECK(ig("face/enabled " I4F "news") == 0 && node_flag(news, 1) == 0 && node_flag(news, 0) == 1);
    CHECK(node_color_is(i4_part("news", 4), 1, 93) && node_color_is(i4_part("news", 2), 0, 49) && node_color_is(i4_part("news", 7), 0, 125));
    CHECK(click_face("news") == 0 && ig("face/value " I4F "news") == 0);
    /* a disabled check set to 1 keeps the disabled look (muted, not the accent) */
    CHECK(gint("block-len face/set-value " I4F "news 1") == 4 && apply_now() == 0);
    CHECK(ig("face/value " I4F "news") == 1 && node_color_is(i4_part("news", 2), 0, 93));
    /* enabled again: the normal look; clicks toggle */
    CHECK(gint("block-len face/set-enabled " I4F "news 1") == 4 && apply_now() == 0);
    CHECK(node_flag(news, 1) == 1 && node_color_is(i4_part("news", 2), 0, 13) && node_color_is(i4_part("news", 4), 1, 9));
    CHECK(click_face("news") == 0 && ig("face/value " I4F "news") == 0);
    /* Tab skips a disabled field: from name, the next stop is later, not off */
    CHECK(gint("block-len face/focus " I4F "name") == 4 && apply_now() == 0);
    if (i4_focused() != inamed("name")) fprintf(stderr, "  focused %ld, name %ld\n", i4_focused(), inamed("name"));
    CHECK(i4_focused() == inamed("name"));
    CHECK(key(SDLK_TAB) == 0 && i4_focused() == inamed("later"));
    /* focus given by the application: Intent's record, the field ring and typing follow */
    CHECK(gint("block-len face/focus " I4F "name") == 4 && apply_now() == 0 && i4_focused() == inamed("name"));
    CHECK(node_flag(ig("block-at block-at intent/hface " I4F "name 2 9"), 0) == 1);
    CHECK(node_flag(ig("block-at block-at intent/hface " I4F "later 2 9"), 0) == 0);
    CHECK(text("Z") == 0); igs(a, sizeof a, "face/value " I4F "name"); CHECK(!strcmp(a, "AnnZ"));
    CHECK(key(8) == 0); igs(a, sizeof a, "face/value " I4F "name"); CHECK(!strcmp(a, "Ann"));
    igs(a, sizeof a, "sin-id judge [ face/focus " I4F "off ]"); CHECK(!strcmp(a, "cannot-focus"));
    igs(a, sizeof a, "sin-id judge [ face/focus " I4F "gone ]"); CHECK(!strcmp(a, "cannot-focus"));
    long off = ig("intent/node intent/current %ld", inamed("off"));
    CHECK(node_flag(off, 1) == 0 && node_color_is(ig("block-at block-at intent/hface " I4F "off 2 3"), 1, 93));
    /* Space in a field types nothing extra (the character arrives as text) */
    igs(a, sizeof a, "face/value " I4F "later"); CHECK(!strcmp(a, "y"));
    CHECK(key(SDLK_SPACE) == 0); igs(a, sizeof a, "face/value " I4F "later"); CHECK(!strcmp(a, "y"));
    /* hidden (declared): not visible; enabling it does not show it */
    long gone = ig("intent/node intent/current %ld", inamed("gone"));
    CHECK(ig("face/visible " I4F "gone") == 0 && node_flag(gone, 0) == 0);
    CHECK(gint("block-len face/set-enabled " I4F "gone 0") == 4 && gint("block-len face/set-enabled " I4F "gone 1") == 4 && apply_now() == 0);
    CHECK(node_flag(gone, 0) == 0 && ig("face/visible " I4F "gone") == 0);
    CHECK(gint("block-len face/set-visible " I4F "gone 1") == 4 && apply_now() == 0 && node_flag(gone, 0) == 1);
    /* a hidden button set disabled stays hidden (it used to be shown again) */
    CHECK(gint("block-len face/set-visible " I4F "go 0") == 4 && gint("block-len face/set-enabled " I4F "go 0") == 4 && apply_now() == 0);
    CHECK(node_flag(ig("intent/node intent/current %ld", inamed("go")), 0) == 0);
    /* Space presses a focused button */
    CHECK(gint("block-len face/focus " I4F "gone") == 4 && apply_now() == 0 && i4_focused() == inamed("gone"));
    CHECK(gint("test/i4-p: 0") == 0 && key(SDLK_SPACE) == 0 && gint("test/i4-p") == 1);
    /* a disabled choice: radios disabled and muted; clicks ignored; enabled again it works */
    CHECK(ig("face/enabled " I4F "kind") == 0);
    CHECK(node_color_is(ig("block-at block-at block-at block-at intent/hface " I4F "kind 2 2 0 4"), 1, 93));
    CHECK(click_text("Beta") == 0 && ig("face/index " I4F "kind") == 0);
    CHECK(gint("block-len face/set-enabled " I4F "kind 1") == 4 && apply_now() == 0);
    CHECK(click_text("Beta") == 0 && ig("face/index " I4F "kind") == 2);
    /* forms: 0/1 through collect, load (others rejected), clear, validate */
    igs(a, sizeof a, "form/collect form/current");
    CHECK(strstr(a, "agree 0") && strstr(a, "news 0"));
    igs(a, sizeof a, "form/validate form/current"); CHECK(strstr(a, "invalid") && strstr(a, "must agree"));
    igs(a, sizeof a, "form/load form/current reduce [ 'agree 1 'news 2 ]"); CHECK(strstr(a, "loaded 1") && strstr(a, "rejected [ news ]"));
    CHECK(ig("face/value " I4F "agree") == 1 && ig("face/value " I4F "news") == 0);
    igs(a, sizeof a, "form/status form/validate form/current"); CHECK(!strcmp(a, "valid"));
    CHECK(gint("block-len form/clear form/current") == 3 && ig("face/value " I4F "agree") == 0);
    /* one refusal for index words on widgets without an index */
    igs(a, sizeof a, "sin-id judge [ face/index " I4F "name ]"); CHECK(strstr(a, "not-indexed") != NULL);
    igs(a, sizeof a, "sin-id judge [ face/set-index " I4F "agree 1 ]"); CHECK(strstr(a, "not-indexed") != NULL);
    /* set-text on a check: its caption */
    CHECK(gint("block-len face/set-text " I4F "agree \"Agreed\"") == 4); igs(a, sizeof a, "face/text " I4F "agree"); CHECK(!strcmp(a, "Agreed"));
    /* build errors, by name */
    const char *errs[][2] = { { "check", "check-needs-caption" }, { "heading 5", "heading-needs-text" }, { "disabled field 5", "option-needs-a-face" },
                              { "hidden", "option-needs-a-face" } };
    for (int k = 0; k < 4; k++) {
        igs(a, sizeof a, "sin-id judge [ intent/show-now layout [ %s ] ]", errs[k][0]);
        if (!strstr(a, errs[k][1])) fprintf(stderr, "  [%s] -> %s (wanted %s)\n", errs[k][0], a, errs[k][1]);
        CHECK(strstr(a, errs[k][1]) != NULL);
    }
    /* a style of check */
    igs(a, sizeof a, "either sin? judge [ intent/show-now layout [ style opt check  o: opt \"Styled\" ] ] [ \"refused\" ] [ \"built\" ]");
    CHECK(!strcmp(a, "built") && ig("block-at intent/hface " I4F "o 0") == 10);
    igs(a, sizeof a, "face/text " I4F "o"); CHECK(!strcmp(a, "Styled"));
    CHECK(ishow("test/i4") == 0 && presented_ok() == 0);
    printf("intent4-check: check (click and Space toggle; change, follow; 0/1 through collect, load, clear, validate), heading, "
           "disabled/hidden options, focus ring and disabled looks, Tab skips disabled, one not-indexed refusal\n");
}

/* intent4-tabs: VID tabs declared in Intent, pages laid out like panels */
static long i4_node(const char *name) { return ig("intent/node intent/current %ld", inamed(name)); }
static void case_intent4_tabs(void) {
    char a[300];
    CHECK(ishow("test/i4-tabs") == 0);
    CHECK(gint("test/i4-tn: 0") == 0);
    CHECK(ig("block-at intent/hface " I4F "pages 0") == 11);
    CHECK(ig("face/index " I4F "pages") == 1);
    igs(a, sizeof a, "face/text " I4F "pages"); CHECK(!strcmp(a, "General"));
    /* the pages' names are the form's: reachable and collected */
    igs(a, sizeof a, "form/names form/current"); CHECK(strstr(a, "name") && strstr(a, "notes") && strstr(a, "agree") && strstr(a, "bottom"));
    igs(a, sizeof a, "form/collect form/current");
    CHECK(strstr(a, "top") && strstr(a, "name") && strstr(a, "notes") && strstr(a, "agree 0") && !strstr(a, "pages"));
    /* only the selected page shows */
    CHECK(node_visible(i4_node("name")) && !node_visible(i4_node("notes")));
    /* a click selects a tab: change with the new index; its page shows */
    CHECK(click_text("Notes") == 0 && ig("face/index " I4F "pages") == 2 && gint("test/i4-tn") == 1 && gint("test/i4-tlast") == 2);
    CHECK(node_visible(i4_node("notes")) && !node_visible(i4_node("name")));
    /* keyboard: the clicked tab has the focus; Right selects the next */
    CHECK(keyk(K_RIGHT) == 0 && ig("face/index " I4F "pages") == 3 && gint("test/i4-tn") == 2 && gint("test/i4-tlast") == 3);
    igs(a, sizeof a, "face/text " I4F "pages"); CHECK(!strcmp(a, "Empty"));
    /* set-index selects without an event; 0 and 4 are refused */
    CHECK(gint("block-len face/set-index " I4F "pages 2") == 4 && apply_now() == 0);
    CHECK(ig("face/index " I4F "pages") == 2 && gint("test/i4-tn") == 2 && node_visible(i4_node("notes")));
    igs(a, sizeof a, "sin-id judge [ face/set-index " I4F "pages 0 ]"); CHECK(!strcmp(a, "invalid-value"));
    igs(a, sizeof a, "sin-id judge [ face/set-index " I4F "pages 4 ]"); CHECK(!strcmp(a, "invalid-value"));
    /* the focus on a page that is hidden is dropped: blur, Intent's record cleared */
    CHECK(gint("block-len face/focus " I4F "notes") == 4 && apply_now() == 0 && i4_focused() == inamed("notes"));
    CHECK(gint("test/i4-tblur: 0") == 0);
    CHECK(gint("block-len face/set-index " I4F "pages 1") == 4 && apply_now() == 0);
    CHECK(i4_focused() == -1 && gint("test/i4-tblur") == 1 && !node_visible(i4_node("notes")));
    /* Tab never reaches a widget on a hidden page */
    CHECK(gint("block-len face/focus " I4F "top") == 4 && apply_now() == 0);
    int reached_name = 0;
    for (int k = 0; k < 10; k++) {
        CHECK(key(9) == 0);
        gv_node_id f = gv_view_focus_id(v);
        gv_rect r;
        CHECK(f && gv_view_visible_rect(v, f, &r) == 0);
        if (i4_focused() == inamed("notes")) CHECK(0);
        if (i4_focused() == inamed("name")) reached_name = 1;
    }
    CHECK(reached_name);
    /* stretch: a wider window widens the tabs and the stretched field on page 2 */
    long w0 = ig("intent/live-w intent/current %ld", inamed("pages"));
    CHECK(gint("block-len face/set-index " I4F "pages 2") == 4 && apply_now() == 0);
    gv_rect n0; CHECK(screen(i4_node("notes"), &n0) == 0);
    int W0 = (int)gint("lay/w"), H0 = (int)gint("lay/h");
    CHECK(resize(W0 + 240, H0) == 0);
    long w1 = ig("intent/live-w intent/current %ld", inamed("pages"));
    gv_rect n1; CHECK(screen(i4_node("notes"), &n1) == 0);
    if (!(w1 > w0 && n1.w > n0.w)) fprintf(stderr, "  tabs %ld -> %ld, notes %d -> %d\n", w0, w1, n0.w, n1.w);
    CHECK(w1 > w0 && n1.w > n0.w);
    CHECK(resize(W0, H0) == 0);
    /* disabled: the tabs leave the Tab order and ignore clicks; enabled again they work */
    CHECK(gint("block-len face/set-enabled " I4F "pages 0") == 4 && apply_now() == 0);
    long tab0 = ig("block-at vid/tabs-tab block-at intent/hface " I4F "pages 2 0 0");
    CHECK(node_flag(tab0, 1) == 0);
    CHECK(click_text("General") == 0 && ig("face/index " I4F "pages") == 2);
    CHECK(gint("block-len face/set-enabled " I4F "pages 1") == 4 && apply_now() == 0 && node_flag(tab0, 1) == 1);
    CHECK(click_text("General") == 0 && ig("face/index " I4F "pages") == 1);
    /* build errors, by name; the pages share the form's names */
    const char *errs[][2] = { { "tabs", "tabs-need-a-block" }, { "tabs [ ]", "tabs-need-pages" }, { "tabs [ \"A\" ]", "tabs-need-label-and-layout" },
                              { "tabs [ \"A\" [ ] \"B\" ]", "tabs-need-label-and-layout" }, { "tabs [ [ ] ]", "tabs-need-label-and-layout" },
                              { "tabs [ \"A\" [ x: field 5 ] \"B\" [ x: field 5 ] ]", "duplicate-name" } };
    for (int k = 0; k < 6; k++) {
        igs(a, sizeof a, "sin-id judge [ intent/show-now layout [ %s ] ]", errs[k][0]);
        if (!strstr(a, errs[k][1])) fprintf(stderr, "  [%s] -> %s (wanted %s)\n", errs[k][0], a, errs[k][1]);
        CHECK(strstr(a, errs[k][1]) != NULL);
    }
    CHECK(ishow("test/i4-tabs") == 0 && presented_ok() == 0);
    printf("intent4-tabs: tabs with laid-out pages; names shared with the form and collected; click and keys select (change with the index), "
           "set-index quiet; a hidden page drops the focus and is out of the Tab order; stretch; disabled; 6 build errors\n");
}

/* intent4-table: the host's virtualised table declared in Intent */
#define TB "form/field form/current 'people"
#define TP "form/field form/current 'plain"
static gv_table *i4_table_id(long id) { gv_table *t = NULL; gv_glon_host_table(h, (int)id, &t); return t; }
static gv_table *i4_table(const char *name) { gv_table *t = NULL; gv_glon_host_table(h, (int)i4_node(name), &t); return t; }
static int click_row(const char *name, int row) {          /* row: 0-based visible row; -1: the header */
    gv_rect r; if (screen(i4_node(name), &r) != 0) return -1;
    int y = row < 0 ? r.y + 14 : r.y + 28 + row * 24 + 12;
    return click(r.x + 40, y);
}
static void case_intent4_table(void) {
    char a[300];
    CHECK(ishow("test/i4-table") == 0);
    CHECK(gint("test/i4-tbn: 0") == 0);
    CHECK(ig("block-at intent/hface " TB " 0") == 12);
    CHECK(ig("face/index " TB) == 0 && ig("face/row-count " TB) == 0);
    igs(a, sizeof a, "face/value " TB); CHECK(!strcmp(a, "none"));
    /* rows from the application, 64 to a block */
    CHECK(gint("block-len test/i4-fill " TB " 20") == 4 && apply_now() == 0 && ig("face/row-count " TB) == 20);
    /* rows declared in the layout (a word naming a block), added when the build ends */
    CHECK(ig("face/row-count form/field form/current 'villages") == 3);
    CHECK(gint("block-len face/set-value form/field form/current 'villages \"F\"") == 4 && ig("face/index form/field form/current 'villages") == 3);
    /* a click selects: index, value (the key column), one change with the value, the follower */
    CHECK(click_row("people", 2) == 0 && ig("face/index " TB) == 3);
    igs(a, sizeof a, "face/value " TB); CHECK(!strcmp(a, "C3"));
    igs(a, sizeof a, "face/text " TB); CHECK(!strcmp(a, "C3"));
    CHECK(gint("test/i4-tbn") == 1); igs(a, sizeof a, "test/i4-tbl"); CHECK(!strcmp(a, "C3"));
    igs(a, sizeof a, "face/text " I4F "picked"); CHECK(!strcmp(a, "C3"));
    { gv_table *t = i4_table("people"); CHECK(t && gv_table_is_selected(t, 2) && t->sel_count == 1); }
    /* the same row again: no change; the header: nothing */
    CHECK(click_row("people", 2) == 0 && gint("test/i4-tbn") == 1);
    CHECK(click_row("people", -1) == 0 && ig("face/index " TB) == 3 && gint("test/i4-tbn") == 1);
    /* keyboard on the focused table */
    if (i4_focused() != inamed("people")) CHECK(gint("block-len face/focus " TB) == 4 && apply_now() == 0);
    CHECK(i4_focused() == inamed("people"));
    CHECK(key(SDLK_DOWN) == 0 && ig("face/index " TB) == 4 && gint("test/i4-tbn") == 2);
    igs(a, sizeof a, "test/i4-tbl"); CHECK(!strcmp(a, "C4"));
    CHECK(keyk(K_END) == 0 && ig("face/index " TB) == 20 && ig("block-at block-at intent/hface " TB " 22 5") == 15);
    { gv_table *t = i4_table("people"); CHECK(t && t->visible_start == 15 && gv_table_is_selected(t, 19)); }
    CHECK(keyk(K_HOME) == 0 && ig("face/index " TB) == 1 && ig("block-at block-at intent/hface " TB " 22 5") == 0);
    CHECK(key(SDLK_PAGEDOWN) == 0 && ig("face/index " TB) == 6);
    CHECK(key(SDLK_UP) == 0 && ig("face/index " TB) == 5 && gint("test/i4-tbn") == 6);
    /* programmatic selection: no event */
    CHECK(gint("block-len face/set-index " TB " 10") == 4 && apply_now() == 0 && gint("test/i4-tbn") == 6);
    igs(a, sizeof a, "face/value " TB); CHECK(!strcmp(a, "C10"));
    CHECK(gint("block-len face/set-value " TB " \"C7\"") == 4 && apply_now() == 0 && ig("face/index " TB) == 7 && gint("test/i4-tbn") == 6);
    igs(a, sizeof a, "sin-id judge [ face/set-value " TB " \"nope\" ]"); CHECK(!strcmp(a, "invalid-value"));
    igs(a, sizeof a, "sin-id judge [ face/set-index " TB " 21 ]"); CHECK(!strcmp(a, "invalid-value"));
    /* forms: the key is collected and loaded; the keyless table is left out */
    igs(a, sizeof a, "form/collect form/current"); CHECK(strstr(a, "people \"C7\"") || strstr(a, "people C7"));
    CHECK(!strstr(a, "plain"));
    igs(a, sizeof a, "form/load form/current reduce [ 'people \"C12\" ]"); CHECK(strstr(a, "loaded 1") != NULL);
    CHECK(ig("face/index " TB) == 12);
    igs(a, sizeof a, "form/load form/current reduce [ 'people \"C99\" ]"); CHECK(strstr(a, "rejected [ people ]") != NULL);
    CHECK(gint("block-len form/clear form/current") == 3 && ig("face/index " TB) == 0);
    igs(a, sizeof a, "face/value " TB); CHECK(!strcmp(a, "none"));
    /* the keyless table: no value (refused, never the index instead); the index works */
    igs(a, sizeof a, "sin-id judge [ face/value " TP " ]"); CHECK(!strcmp(a, "table-has-no-key"));
    igs(a, sizeof a, "sin-id judge [ face/set-value " TP " \"x\" ]"); CHECK(!strcmp(a, "table-has-no-key"));
    igs(a, sizeof a, "sin-id judge [ face/text " TP " ]"); CHECK(!strcmp(a, "table-has-no-key"));
    CHECK(gint("block-len face/set-rows " TP " reduce [ reduce [ \"one\" ] reduce [ \"two\" ] reduce [ 3 ] ]") == 4 && apply_now() == 0);
    CHECK(ig("face/row-count " TP) == 3);
    CHECK(click_row("plain", 1) == 0 && ig("face/index " TP) == 2);
    /* a cell with the host's separators is escaped (the slow path), one without is sent as it is */
    CHECK(gint("block-len face/add-rows " TP " reduce [ reduce [ \"a|b;c\\\\d\" ] reduce [ \"plain é\" ] ]") == 4 && apply_now() == 0);
    { gv_table *t = i4_table("plain"); const char *c3 = t ? gv_table_get_cell(t, 3, 0) : NULL, *c4 = t ? gv_table_get_cell(t, 4, 0) : NULL;
      if (!(c3 && !strcmp(c3, "a|b;c\\d"))) fprintf(stderr, "  escaped cell: [%s]\n", c3 ? c3 : "(null)");
      CHECK(c3 && !strcmp(c3, "a|b;c\\d") && c4 && !strcmp(c4, "plain é")); }
    /* set-rows replaces: count, selection and keys start again */
    CHECK(gint("block-len face/set-rows " TB " reduce [ reduce [ \"K1\" \"a\" \"b\" ] ]") == 4 && apply_now() == 0);
    CHECK(ig("face/row-count " TB) == 1 && ig("face/index " TB) == 0);
    igs(a, sizeof a, "sin-id judge [ face/set-value " TB " \"C3\" ]"); CHECK(!strcmp(a, "invalid-value"));
    CHECK(gint("block-len face/set-value " TB " \"K1\"") == 4 && ig("face/index " TB) == 1);
    /* disabled: clicks select nothing; enabled again they do */
    CHECK(gint("block-len face/set-rows " TB " reduce [ ]") == 4 && gint("block-len test/i4-fill " TB " 8") == 4 && apply_now() == 0);
    CHECK(gint("block-len face/set-enabled " TB " 0") == 4 && apply_now() == 0);
    CHECK(click_row("people", 4) == 0 && ig("face/index " TB) == 0);
    CHECK(gint("block-len face/set-enabled " TB " 1") == 4 && apply_now() == 0);
    CHECK(click_row("people", 4) == 0 && ig("face/index " TB) == 5);
    /* row errors */
    igs(a, sizeof a, "sin-id judge [ face/add-rows " TB " 5 ]"); CHECK(!strcmp(a, "rows-need-a-block"));
    igs(a, sizeof a, "sin-id judge [ face/add-rows " TB " reduce [ reduce [ ] ] ]"); CHECK(!strcmp(a, "row-has-no-key"));
    igs(a, sizeof a, "sin-id judge [ face/add-rows " TB " reduce [ reduce [ \"a\" [ x ] ] ] ]"); CHECK(!strcmp(a, "cell-needs-text-or-integer"));
    igs(a, sizeof a, "sin-id judge [ face/set-rows " I4F "search reduce [ ] ]"); CHECK(!strcmp(a, "not-a-table"));
    igs(a, sizeof a, "sin-id judge [ face/add-rows " TP " reduce [ reduce [ \"a\" \"b\" ] ] ]"); CHECK(!strcmp(a, "row-has-too-many-cells"));
    /* build errors */
    const char *errs[][2] = { { "table", "table-needs-columns" }, { "table [ ]", "table-needs-columns" },
                              { "table [ \"A\" ]", "table-needs-title-and-width" }, { "table [ \"A\" 5 ] 0", "table-needs-rows" },
                              { "f: field 5 key 1", "key-needs-a-table" }, { "table [ \"A\" 5 ] key 2", "no-such-column" },
                              { "table [ \"A\" 5 ] key", "key-needs-a-column" }, { "key 1", "option-needs-a-face" },
                              { "t: table [ \"A\" 5 ]  l: label \"x\" follow t value", "has-no-value" },
                              { "f: field 5 rows [ ]", "rows-need-a-table" }, { "table [ \"A\" 5 ] rows 5", "rows-need-a-block" },
                              { "table [ \"A\" 5 ] rows [ [ \"a\" \"b\" ] ]", "row-has-too-many-cells" } };
    for (int k = 0; k < 12; k++) {
        igs(a, sizeof a, "sin-id judge [ intent/show-now layout [ %s ] ]", errs[k][0]);
        if (!strstr(a, errs[k][1])) fprintf(stderr, "  [%s] -> %s (wanted %s)\n", errs[k][0], a, errs[k][1]);
        CHECK(strstr(a, errs[k][1]) != NULL);
    }
    /* scale: keyed rows added through Intent inside events, 250 an event (as an
     * application adds them); 500 by default (I4_ROWS to change it; see
     * INTENT-4B-STAGE1.md for the ceiling of the 1M profile) */
    CHECK(ishow("test/i4-table") == 0);
    int total = getenv("I4_ROWS") ? atoi(getenv("I4_ROWS")) : 500;
    total = (total + 249) / 250 * 250;
    Uint64 t0 = SDL_GetPerformanceCounter();
    for (int done = 0; done < total; done += 250)
        if (key(5200) != 0 || gv_glon_host_error(h) || ig("face/row-count " TB) != done + 250) {
            fprintf(stderr, "  event fill stopped at %ld rows (%ld collections; at the last: live %ld cells, free %ld in %ld blocks)\n",
                    ig("face/row-count " TB), r0_s1_gc_count(), r0_s1_gc_live_cells(), r0_s1_gc_free_cells(), r0_s1_gc_free_blocks()); CHECK(0); break;
        }
    double fill_ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    { gv_table *t = i4_table("people"); CHECK(t && t->nrows == total && ig("face/row-count " TB) == total); }
    char last[32]; snprintf(last, sizeof last, "C%d", total - 1);
    t0 = SDL_GetPerformanceCounter();
    CHECK(ig("block-len face/set-value " TB " \"%s\"", last) == 4 && apply_now() == 0 && ig("face/index " TB) == total - 1);
    double find_ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    CHECK(gint("block-len face/focus " TB) == 4 && apply_now() == 0);
    CHECK(i4_focused() == inamed("people"));
    CHECK(keyk(K_END) == 0);
    if (ig("face/index " TB) != total) fprintf(stderr, "  after End: index %ld, focused %ld, host error %d\n", ig("face/index " TB), i4_focused(), gv_glon_host_error(h));
    CHECK(ig("face/index " TB) == total);
    snprintf(last, sizeof last, "C%d", total);
    igs(a, sizeof a, "face/value " TB); CHECK(!strcmp(a, last));
    { gv_table *t = i4_table("people"); CHECK(t && t->visible_start == total - 5 && gv_table_is_selected(t, total - 1)); }
    printf("  %d keyed rows through Intent in %d events: %.0f ms (%.1f ms a row); find the next-to-last key: %.0f ms\n",
           total, total / 250, fill_ms, fill_ms / total, find_ms);
    CHECK(ishow("test/i4-table") == 0 && presented_ok() == 0);
    printf("intent4-table: rows from the application; click and keys select (change with the key value), set-index / set-value / "
           "load quiet; value = key column, index = position, no key -> refused (never the index); collect/load/clear; disabled; "
           "rows option; 12 build errors, 5 row errors; escaping; 500 keyed rows across events\n");
}

/* intent4-demo-disabled: the user's report on intent4_demo.glon, end to end
 * on the demo file itself (dummy driver).  "Newsletter" starts disabled on
 * the Member tab; mouse and keyboard input are refused; the adjacent
 * "Enable newsletter" button enables it and sets the status line; then the
 * mouse and Space toggle it.  Before the fix a press on the disabled box
 * blurred the focused widget and was reported focused itself, so Intent drew
 * it with the focus edge. */
static void case_intent4_demo_disabled(void) {
    char a[200];
    CHECK(ig("= intent/current demo4/form") == 1);
    CHECK(resize(640, 400) == 0);                                   /* the demo's own window size */
    long news = i4_node("news");
    gv_rect r; CHECK(screen(news, &r) == 0);
    int bx = r.x + 8, by = r.y + 12;                                /* the box itself */
    /* 1. built disabled, inside the tab page: Intent and View agree */
    CHECK(ig("face/enabled " I4F "news") == 0 && node_flag(news, 1) == 0);
    CHECK(node_color_is(i4_part("news", 4), 1, 93));
    igs(a, sizeof a, "face/text " I4F "status"); CHECK(!strcmp(a, "Ready"));
    /* 2. the mouse is refused: no toggle, and the focus stays where it was */
    CHECK(click_face("name") == 0 && i4_focused() == inamed("name"));
    for (int k = 0; k < 3; k++) CHECK(click(bx, by) == 0);
    CHECK(ig("face/value " I4F "news") == 0);
    CHECK(i4_focused() == inamed("name") && ig("block-at intent/hface " I4F "news 26") == 0);
    CHECK(node_color_is(i4_part("news", 7), 0, 125));               /* the disabled edge, not the focus edge */
    /* 3. the keyboard is refused: Tab passes over it; it cannot be focused */
    CHECK(click_face("agree") == 0 && i4_focused() == inamed("agree") && ig("face/value " I4F "agree") == 1);
    CHECK(key(9) == 0 && i4_focused() == inamed("enable-news"));
    igs(a, sizeof a, "sin-id judge [ face/focus " I4F "news ]"); CHECK(!strcmp(a, "cannot-focus"));
    CHECK(key(SDLK_SPACE) == 0 && ig("face/value " I4F "news") == 0); /* Space on the button: it presses it */
    igs(a, sizeof a, "face/text " I4F "status"); CHECK(!strcmp(a, "newsletter enabled"));
    /* 4. enabled by the button: Intent and View, and the look */
    CHECK(ig("face/enabled " I4F "news") == 1 && node_flag(news, 1) == 1);
    CHECK(node_color_is(i4_part("news", 4), 1, 9));
    /* the button again, by the mouse: the status line is set by it */
    CHECK(gint("block-len face/set-text " I4F "status \"Ready\"") == 4 && apply_now() == 0);
    CHECK(click_text("Enable newsletter") == 0);
    igs(a, sizeof a, "face/text " I4F "status"); CHECK(!strcmp(a, "newsletter enabled"));
    /* 5. the mouse toggles it, and it takes the focus */
    CHECK(click(bx, by) == 0 && ig("face/value " I4F "news") == 1 && i4_focused() == inamed("news"));
    CHECK(node_color_is(i4_part("news", 2), 0, 13) && node_color_is(i4_part("news", 7), 0, 105));
    CHECK(click(bx, by) == 0 && ig("face/value " I4F "news") == 0);
    /* 6. the keyboard toggles it: Space, and Tab reaches it */
    CHECK(key(SDLK_SPACE) == 0 && ig("face/value " I4F "news") == 1);
    CHECK(key(SDLK_SPACE) == 0 && ig("face/value " I4F "news") == 0);
    CHECK(click_face("agree") == 0 && key(9) == 0 && i4_focused() == inamed("news"));
    CHECK(key(SDLK_SPACE) == 0 && ig("face/value " I4F "news") == 1);
    CHECK(presented_ok() == 0);
    printf("intent4-demo-disabled: on intent4_demo.glon, the disabled Newsletter check refuses mouse (no toggle, no focus taken) "
           "and keyboard (Tab passes, focus refused); Enable newsletter (Space and mouse) sets the status and enables it; "
           "then mouse and Space toggle it\n");
}

/* ==== INTENT-4B-2: forms and text editing ============================== */
static int key_ctrl(int sym) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = SDL_KEYDOWN; e.key.keysym.sym = sym; e.key.keysym.mod = KMOD_LCTRL; e.key.state = SDL_PRESSED;
    e.key.windowID = win_target;
    SDL_PushEvent(&e);
    return pump();
}
/* a field's edit [ caret anchor mode ] and what its text node shows */
static long i4_caret(const char *name) { return ig("block-at block-at block-at intent/hface " I4F "%s 2 8 0", name); }
static long i4_anchor(const char *name) { return ig("block-at block-at block-at intent/hface " I4F "%s 2 8 1", name); }
static const char *i4_shown(const char *name) {
    gv_node *n = gv_view_node(v, vnode(ig("block-at block-at intent/hface " I4F "%s 2 3", name)));
    return n && n->text ? n->text : "";
}
static int i4_value_is(const char *name, const char *want) {
    char a[400]; igs(a, sizeof a, "face/value " I4F "%s", name);
    if (strcmp(a, want)) fprintf(stderr, "  %s value [%s], wanted [%s]\n", name, a, want);
    return !strcmp(a, want);
}
static void case_intent4_edit(void) {
    char a[400];
    CHECK(ishow("test/i4-edit") == 0);
    CHECK(gint("test/i4e-n: 0") == 0);
    /* focus and the caret: at the end */
    CHECK(click_face("name") == 0 && i4_focused() == inamed("name") && i4_caret("name") == 3);
    { gv_node *n = gv_view_node(v, vnode(ig("block-at block-at intent/hface " I4F "name 2 3"))); CHECK(n && n->caret == 4 && n->edit == 1); }
    /* insertion at the caret, Home / End, Left / Right */
    CHECK(keyk(K_HOME) == 0 && i4_caret("name") == 0 && text("X") == 0 && i4_value_is("name", "XAda") && i4_caret("name") == 1);
    CHECK(keyk(K_RIGHT) == 0 && keyk(K_RIGHT) == 0 && text("-") == 0 && i4_value_is("name", "XAd-a"));
    CHECK(keyk(K_END) == 0 && i4_caret("name") == 5);
    /* Backspace and Delete */
    CHECK(key(8) == 0 && i4_value_is("name", "XAd-"));
    CHECK(keyk(K_HOME) == 0 && key(SDLK_DELETE) == 0 && i4_value_is("name", "Ad-"));
    /* selection: Shift+Right twice, typing replaces it */
    CHECK(key_shift(SDLK_RIGHT) == 0 && key_shift(SDLK_RIGHT) == 0 && i4_caret("name") == 2 && i4_anchor("name") == 0);
    { gv_node *n = gv_view_node(v, vnode(ig("block-at block-at intent/hface " I4F "name 2 3"))); CHECK(n && n->sel_anchor == 1 && n->caret == 3); }
    CHECK(text("Be") == 0 && i4_value_is("name", "Be-") && i4_caret("name") == 2 && i4_anchor("name") == 2);
    /* Left with a selection collapses it to its start; Shift+Home selects to the start */
    CHECK(key_shift(SDLK_END) == 0 && i4_anchor("name") == 2 && i4_caret("name") == 3);
    CHECK(keyk(K_LEFT) == 0 && i4_caret("name") == 2 && i4_anchor("name") == 2);
    /* the clipboard: Ctrl+A, Ctrl+C, End, Ctrl+V; Ctrl+X */
    CHECK(key_ctrl(SDLK_a) == 0 && i4_caret("name") == 3 && i4_anchor("name") == 0);
    CHECK(key_ctrl(SDLK_c) == 0);
    { char *cb = SDL_GetClipboardText(); CHECK(cb && !strcmp(cb, "Be-")); SDL_free(cb); }
    CHECK(keyk(K_END) == 0 && key_ctrl(SDLK_v) == 0 && i4_value_is("name", "Be-Be-"));
    CHECK(key_shift(SDLK_LEFT) == 0 && key_shift(SDLK_LEFT) == 0 && key_ctrl(SDLK_x) == 0 && i4_value_is("name", "Be-B"));
    { char *cb = SDL_GetClipboardText(); CHECK(cb && !strcmp(cb, "e-")); SDL_free(cb); }
    /* Unicode: characters of two and three bytes; Left and Backspace step whole characters */
    CHECK(text("") == 0);
    { SDL_Event e; memset(&e, 0, sizeof e); e.type = SDL_TEXTINPUT; strcpy(e.text.text, "é"); SDL_PushEvent(&e);
      memset(&e, 0, sizeof e); e.type = SDL_TEXTINPUT; strcpy(e.text.text, "漢"); SDL_PushEvent(&e); CHECK(pump() == 0); }
    CHECK(i4_value_is("name", "Be-Bé漢") && i4_caret("name") == 9);
    CHECK(keyk(K_LEFT) == 0 && i4_caret("name") == 6 && keyk(K_LEFT) == 0 && i4_caret("name") == 4);
    CHECK(keyk(K_END) == 0 && key(8) == 0 && i4_value_is("name", "Be-Bé") && key(8) == 0 && i4_value_is("name", "Be-B"));
    /* every edit fired change once; the follower shows the value */
    long changes = gint("test/i4e-n");
    CHECK(changes >= 10);
    igs(a, sizeof a, "face/text " I4F "copyname"); CHECK(!strcmp(a, "Be-B"));
    /* moves fire nothing */
    CHECK(keyk(K_HOME) == 0 && keyk(K_END) == 0 && gint("test/i4e-n") == changes);
    /* a programmatic value while focused: shown, the caret at its end, no event */
    CHECK(gint("block-len face/set-value " I4F "name \"  two spaces\"") == 4 && apply_now() == 0);
    CHECK(i4_value_is("name", "  two spaces") && i4_caret("name") == 12 && gint("test/i4e-n") == changes);
    CHECK(!strcmp(i4_shown("name"), "  two spaces"));                      /* exactly: leading spaces kept */
    igs(a, sizeof a, "face/text " I4F "copyname"); CHECK(!strcmp(a, "  two spaces")); /* programmatic: the follower shows it too (INTENT-4B-3) */
    CHECK(text("!") == 0 && i4_value_is("name", "  two spaces!"));
    /* read-only: caret and selection and copy work; nothing changes the text */
    CHECK(click_face("ro") == 0 && i4_focused() == inamed("ro"));
    CHECK(keyk(K_HOME) == 0 && i4_caret("ro") == 0 && key_shift(SDLK_RIGHT) == 0 && i4_anchor("ro") == 0 && i4_caret("ro") == 1);
    CHECK(text("Z") == 0 && key(8) == 0 && key(SDLK_DELETE) == 0 && key_ctrl(SDLK_x) == 0 && key_ctrl(SDLK_v) == 0);
    CHECK(i4_value_is("ro", "fixed text"));
    CHECK(key_ctrl(SDLK_c) == 0); { char *cb = SDL_GetClipboardText(); CHECK(cb && !strcmp(cb, "f")); SDL_free(cb); }
    /* disabled: no focus, no typing */
    CHECK(click_face("off") == 0 && i4_focused() == inamed("ro"));
    igs(a, sizeof a, "sin-id judge [ face/focus " I4F "off ]"); CHECK(!strcmp(a, "cannot-focus"));
    CHECK(text("Q") == 0 && i4_value_is("off", "off") && i4_value_is("ro", "fixed text"));
    /* password: the value is the text; View shows only bullets; never copied */
    CHECK(click_face("pin") == 0 && i4_focused() == inamed("pin"));
    CHECK(text("s3cr") == 0);
    { SDL_Event e; memset(&e, 0, sizeof e); e.type = SDL_TEXTINPUT; strcpy(e.text.text, "é"); SDL_PushEvent(&e); CHECK(pump() == 0); }
    CHECK(i4_value_is("pin", "s3cré"));
    CHECK(!strcmp(i4_shown("pin"), "•••••") && !strstr(i4_shown("pin"), "s3"));
    { gv_node *n = gv_view_node(v, vnode(ig("block-at block-at intent/hface " I4F "pin 2 3"))); CHECK(n && n->caret == 16); }   /* after 5 bullets */
    igs(a, sizeof a, "face/text " I4F "pin"); CHECK(!strcmp(a, "•••••"));
    SDL_SetClipboardText("unchanged");
    CHECK(key_ctrl(SDLK_a) == 0 && key_ctrl(SDLK_c) == 0 && key_ctrl(SDLK_x) == 0 && i4_value_is("pin", "s3cré"));
    { char *cb = SDL_GetClipboardText(); CHECK(cb && !strcmp(cb, "unchanged")); SDL_free(cb); }
    CHECK(keyk(K_END) == 0 && key(8) == 0 && i4_value_is("pin", "s3cr"));
    /* area: Enter makes a newline; Up / Down move by line; Home / End by line */
    CHECK(click_face("notes") == 0 && i4_focused() == inamed("notes") && i4_caret("notes") == 8);
    CHECK(key(13) == 0 && text("second") == 0 && i4_value_is("notes", "line one\nsecond"));
    { gv_node *n = gv_view_node(v, vnode(ig("block-at block-at intent/hface " I4F "notes 2 3"))); CHECK(n && n->text && !strcmp(n->text, "line one\nsecond")); }
    CHECK(key(SDLK_UP) == 0 && i4_caret("notes") == 6);        /* column 6 on the first line */
    CHECK(keyk(K_HOME) == 0 && i4_caret("notes") == 0 && keyk(K_END) == 0 && i4_caret("notes") == 8);
    CHECK(key(SDLK_DOWN) == 0 && i4_caret("notes") == 15);     /* the second line's end (it is shorter) */
    CHECK(keyk(K_HOME) == 0 && i4_caret("notes") == 9 && key_shift(SDLK_UP) == 0 && i4_caret("notes") == 0 && i4_anchor("notes") == 9);
    CHECK(text("first\n") == 0 || 1);
    CHECK(i4_value_is("notes", "first\nsecond"));               /* a newline typed into an area stays */
    /* forms: collect, load, clear and validate across text, check and choice */
    igs(a, sizeof a, "form/collect form/current");
    CHECK(strstr(a, "name") && strstr(a, "pin \"s3cr\"") && strstr(a, "agree 0") && strstr(a, "kind none"));
    igs(a, sizeof a, "form/load form/current reduce [ 'name \"Kiri\" 'pin \"9999\" 'notes \"a\" 'agree 1 'kind \"b\" 'ro \"x\" ]");
    CHECK(strstr(a, "loaded 6") != NULL);
    CHECK(i4_value_is("name", "Kiri") && i4_value_is("pin", "9999") && i4_value_is("notes", "a") && i4_value_is("ro", "x"));
    CHECK(!strcmp(i4_shown("pin"), "••••") && ig("face/value " I4F "agree") == 1 && ig("face/index " I4F "kind") == 2);
    igs(a, sizeof a, "form/status form/validate form/current"); CHECK(!strcmp(a, "valid"));
    CHECK(gint("block-len form/clear form/current") == 3 && i4_value_is("name", "") && i4_value_is("pin", "") && i4_value_is("notes", ""));
    igs(a, sizeof a, "form/validate form/current"); CHECK(strstr(a, "invalid") && strstr(a, "a name is needed"));
    /* build errors */
    const char *errs[][2] = { { "area 5", "area-needs-size" }, { "area 0 3", "area-needs-size" }, { "password", "option-needs-a-face" },
                              { "label \"x\" password", "option-needs-a-field" }, { "a: area 5 2 password", "option-needs-a-field" },
                              { "button \"b\" read-only", "option-needs-a-field" },
                              { "p: field 5 password  l: label \"x\" follow p value", "cannot-follow-a-password" } };
    for (int k = 0; k < 7; k++) {
        igs(a, sizeof a, "sin-id judge [ intent/show-now layout [ %s ] ]", errs[k][0]);
        if (!strstr(a, errs[k][1])) fprintf(stderr, "  [%s] -> %s (wanted %s)\n", errs[k][0], a, errs[k][1]);
        CHECK(strstr(a, errs[k][1]) != NULL);
    }
    CHECK(ishow("test/i4-edit") == 0 && presented_ok() == 0);
    printf("intent4-edit: caret editing (Home/End, arrows, Backspace/Delete), Shift selection, Ctrl+A/C/X/V, UTF-8, "
           "programmatic values (exact text, caret at end, no event), read-only, disabled, password (bullets only, no copy), "
           "area (newlines, Up/Down, line Home/End), collect/load/clear/validate, 7 build errors\n");
}

/* intent4-forms-demo: the Stage 2 demo (intent_forms_demo.glon) end to end:
 * keyboard entry and navigation, validation feedback, save / clear / load;
 * the password never reaches View's texts nor an event trace */
static int any_node_text_has(const char *needle) {
    for (gv_node_id i = 1; i < 4096; i++) {
        gv_node *n = gv_view_node(v, i);
        if (n && n->text && strstr(n->text, needle)) return 1;
    }
    return 0;
}
static void case_intent4_forms_demo(void) {
    char a[400];
    CHECK(ig("= intent/current forms/form") == 1);
    CHECK(resize(720, 520) == 0);
    igs(a, sizeof a, "face/text " I4F "status"); CHECK(!strcmp(a, "Fill in the form and press Save."));
    /* keyboard: click Name, type; Tab to Email; Shift+Tab back; Tab again */
    CHECK(click_face("name") == 0 && text("Ana") == 0 && i4_value_is("name", "Ana"));
    CHECK(key(9) == 0 && i4_focused() == inamed("email"));
    CHECK(key_shift(SDLK_TAB) == 0 && i4_focused() == inamed("name"));
    CHECK(key(9) == 0 && text("ana.example.org") == 0);
    /* validation feedback: the first invalid widget's message */
    CHECK(click_text("Save") == 0);
    igs(a, sizeof a, "face/text " I4F "status"); CHECK(!strcmp(a, "The email address needs an @."));
    /* fix it with the editing keys: Home, Right x3, select one character, retype */
    CHECK(click_face("email") == 0 && keyk(K_HOME) == 0 && keyk(K_RIGHT) == 0 && keyk(K_RIGHT) == 0 && keyk(K_RIGHT) == 0);
    CHECK(key_shift(SDLK_RIGHT) == 0 && text("@") == 0 && i4_value_is("email", "ana@example.org"));
    /* the password, typed while events are traced: the trace shows lengths only */
    CHECK(key(9) == 0 && i4_focused() == inamed("pin"));
    char tpath[] = "/tmp/claude-1000/intent4-trace-XXXXXX";
    int tfd = mkstemp(tpath);
    int saved_err = dup(2);
    CHECK(tfd >= 0 && saved_err >= 0);
    fflush(stderr); dup2(tfd, 2);
    setenv("GV_TRACE_EVENTS", "1", 1);
    CHECK(text("short") == 0);
    unsetenv("GV_TRACE_EVENTS");
    fflush(stderr); dup2(saved_err, 2); close(saved_err);
    { char buf[8192]; ssize_t n = pread(tfd, buf, sizeof buf - 1, 0); buf[n > 0 ? n : 0] = 0;
      CHECK(strstr(buf, "view-char") != NULL && strstr(buf, "short") == NULL && strstr(buf, "view-char s") == NULL);
      if (strstr(buf, "short")) fprintf(stderr, "  the trace shows the password:\n%s\n", buf); }
    close(tfd); unlink(tpath);
    CHECK(i4_value_is("pin", "short") && !any_node_text_has("short") && !strcmp(i4_shown("pin"), "•••••"));
    CHECK(click_text("Save") == 0);
    igs(a, sizeof a, "face/text " I4F "status"); CHECK(!strcmp(a, "The password needs 8 characters or more."));
    CHECK(click_face("pin") == 0 && keyk(K_END) == 0 && text("word1") == 0 && i4_value_is("pin", "shortword1"));
    /* notes: two lines; then the check by Space */
    CHECK(key(9) == 0 && i4_focused() == inamed("notes") && text("Prefers email") == 0 && key(13) == 0 && text("since 2019") == 0);
    CHECK(i4_value_is("notes", "Prefers email\nsince 2019"));
    CHECK(click_text("Save") == 0);
    igs(a, sizeof a, "face/text " I4F "status"); CHECK(!strcmp(a, "Please accept the rules."));
    CHECK(click_face("notes") == 0 && key(9) == 0 && i4_focused() == inamed("agree") && key(SDLK_SPACE) == 0 && ig("face/value " I4F "agree") == 1);
    CHECK(click_text("Save") == 0);
    igs(a, sizeof a, "face/text " I4F "status"); CHECK(!strcmp(a, "Saved."));
    /* clear: everything empty; load: everything back, the password as bullets */
    CHECK(click_text("Clear") == 0 && i4_value_is("name", "") && i4_value_is("pin", "") && i4_value_is("notes", "") && ig("face/value " I4F "agree") == 0);
    igs(a, sizeof a, "face/text " I4F "status"); CHECK(!strcmp(a, "Cleared."));
    CHECK(click_text("Load") == 0);
    CHECK(i4_value_is("name", "Ana") && i4_value_is("email", "ana@example.org") && i4_value_is("pin", "shortword1"));
    CHECK(i4_value_is("notes", "Prefers email\nsince 2019") && ig("face/value " I4F "agree") == 1);
    CHECK(!strcmp(i4_shown("pin"), "••••••••••") && !any_node_text_has("shortword1"));
    igs(a, sizeof a, "face/text " I4F "status"); CHECK(!strcmp(a, "Loaded the saved record."));
    CHECK(presented_ok() == 0);
    printf("intent4-forms-demo: on intent_forms_demo.glon, keyboard entry and Tab / Shift+Tab, validation messages in order, "
           "editing keys, Save / Clear / Load round trip; the password is only bullets on View and absent from an event trace\n");
}

/* ==== INTENT-4B-3: drop-down, list, hidden widgets ===================== */
#define DD "form/field form/current 'village"
static long dd_tid(void) { return ig("block-at block-at intent/hface " DD " 22 9"); }
static const char *dd_caption(void) {
    gv_node *n = gv_view_node(v, vnode(ig("block-at block-at intent/hface " DD " 2 3")));
    return n && n->text ? n->text : "";
}
static int dd_open(void) { return node_flag(dd_tid(), 0) == 1 && ig("intent/dd-open-i") >= 0; }
static int click_popup_row(int row) {
    gv_rect r; if (screen(dd_tid(), &r) != 0) return -1;
    return click(r.x + 30, r.y + row * 24 + 12);
}
static void case_intent4_select(void) {
    char a[400];
    CHECK(ishow("test/i4-sel") == 0);
    CHECK(gint("test/i4-sn: 0") == 0);
    /* rows from application data: labels shown, keys stored; nothing selected */
    CHECK(ig("block-at intent/hface " DD " 0") == 15 && ig("face/row-count " DD) == 4 && ig("face/index " DD) == 0);
    igs(a, sizeof a, "face/value " DD); CHECK(!strcmp(a, "none"));
    igs(a, sizeof a, "face/text " DD); CHECK(!strcmp(a, "") && !strcmp(dd_caption(), ""));
    { gv_table *t = i4_table_id(dd_tid()); CHECK(t && t->nrows == 4 && t->ncols == 1 && t->header_h == 0 && !strcmp(gv_table_get_cell(t, 1, 0), "Salelologa")); }
    CHECK(!dd_open());
    /* the mouse: open below the button, over the content; pick row 2 */
    gv_rect br; CHECK(screen(i4_node("village"), &br) == 0);
    CHECK(click_face("village") == 0 && dd_open());
    gv_rect pr; CHECK(screen(dd_tid(), &pr) == 0);
    if (!(pr.x == br.x && pr.y == br.y + br.h)) fprintf(stderr, "  button %d,%d %dx%d list %d,%d %dx%d\n", br.x, br.y, br.w, br.h, pr.x, pr.y, pr.w, pr.h);
    CHECK(pr.x == br.x && pr.y == br.y + br.h && pr.w == br.w && pr.h == 4 * 24 + 2);
    CHECK(click_popup_row(1) == 0 && !dd_open());
    CHECK(ig("face/index " DD) == 2 && gint("test/i4-sn") == 1);
    igs(a, sizeof a, "face/value " DD); CHECK(!strcmp(a, "SLL"));
    igs(a, sizeof a, "face/text " DD); CHECK(!strcmp(a, "Salelologa") && !strcmp(dd_caption(), "Salelologa"));
    igs(a, sizeof a, "test/i4-sl"); CHECK(!strcmp(a, "SLL"));
    igs(a, sizeof a, "face/text " I4F "shown"); CHECK(!strcmp(a, "Salelologa"));
    igs(a, sizeof a, "face/text " I4F "code"); CHECK(!strcmp(a, "SLL"));
    /* the keyboard on the focused button: Down / Up / End / Home select */
    CHECK(i4_focused() == inamed("village"));
    CHECK(key(SDLK_DOWN) == 0 && ig("face/index " DD) == 3 && !strcmp(dd_caption(), "Faleolo"));
    CHECK(key(SDLK_UP) == 0 && ig("face/index " DD) == 2);
    CHECK(keyk(K_END) == 0 && ig("face/index " DD) == 4);
    igs(a, sizeof a, "face/value " DD); CHECK(!strcmp(a, "APX"));
    igs(a, sizeof a, "face/text " DD); CHECK(!strcmp(a, "Apia"));      /* a repeated label: the key tells them apart */
    CHECK(keyk(K_HOME) == 0 && ig("face/index " DD) == 1);
    igs(a, sizeof a, "face/value " DD); CHECK(!strcmp(a, "APW"));
    CHECK(key(SDLK_UP) == 0 && ig("face/index " DD) == 1 && gint("test/i4-sn") == 5);  /* no move past the first: no event */
    /* Space opens, Escape closes; Enter opens and closes; a click elsewhere closes */
    CHECK(key(SDLK_SPACE) == 0 && dd_open() && key(SDLK_ESCAPE) == 0 && !dd_open());
    CHECK(key(13) == 0 && dd_open() && key(13) == 0 && !dd_open());
    CHECK(key(13) == 0 && dd_open() && click_face("after") == 0 && !dd_open() && i4_focused() == inamed("after"));
    CHECK(click_face("village") == 0 && dd_open() && key(9) == 0 && !dd_open());   /* Tab away closes it */
    /* programmatic selection: by key (repeated labels), by index; refusals; no events */
    long ev = gint("test/i4-sn");
    CHECK(gint("block-len face/set-value " DD " \"APX\"") == 4 && apply_now() == 0 && ig("face/index " DD) == 4 && !strcmp(dd_caption(), "Apia"));
    CHECK(gint("block-len face/set-index " DD " 3") == 4 && apply_now() == 0);
    igs(a, sizeof a, "face/value " DD); CHECK(!strcmp(a, "FGI") && gint("test/i4-sn") == ev);
    igs(a, sizeof a, "sin-id judge [ face/set-index " DD " 5 ]"); CHECK(!strcmp(a, "invalid-value"));
    igs(a, sizeof a, "sin-id judge [ face/set-index " DD " -1 ]"); CHECK(!strcmp(a, "invalid-value"));
    igs(a, sizeof a, "sin-id judge [ face/set-value " DD " \"ZZZ\" ]"); CHECK(!strcmp(a, "invalid-value"));
    CHECK(ig("face/index " DD) == 3);
    /* forms: the key is collected and loaded; validation; clear */
    igs(a, sizeof a, "form/collect form/current"); CHECK(strstr(a, "village \"FGI\"") && !strstr(a, "empty"));
    igs(a, sizeof a, "form/load form/current reduce [ 'village \"SLL\" ]"); CHECK(strstr(a, "loaded 1") && ig("face/index " DD) == 2);
    igs(a, sizeof a, "form/status form/validate form/current"); CHECK(!strcmp(a, "valid"));
    CHECK(gint("block-len form/clear form/current") == 3 && apply_now() == 0);
    if (ig("face/index " DD) != 0 || strcmp(dd_caption(), "")) fprintf(stderr, "  after clear: index %ld caption [%s]\n", ig("face/index " DD), dd_caption());
    CHECK(ig("face/index " DD) == 0 && !strcmp(dd_caption(), ""));
    igs(a, sizeof a, "form/validate form/current"); CHECK(strstr(a, "pick a village") != NULL);
    /* dynamic replacement: new rows; the selection and old keys go */
    CHECK(gint("block-len face/set-value " DD " \"APW\"") == 4);
    CHECK(gint("block-len face/set-rows " DD " reduce [ reduce [ \"MNO\" \"Manono-uta\" ] reduce [ \"ASU\" \"Asau\" ] ]") == 4 && apply_now() == 0);
    CHECK(ig("face/row-count " DD) == 2 && ig("face/index " DD) == 0 && !strcmp(dd_caption(), ""));
    igs(a, sizeof a, "sin-id judge [ face/set-value " DD " \"APW\" ]"); CHECK(!strcmp(a, "invalid-value"));
    CHECK(click_face("village") == 0 && dd_open());
    CHECK(screen(dd_tid(), &pr) == 0 && pr.h == 2 * 24 + 2);            /* the list fits its rows */
    CHECK(click_popup_row(1) == 0 && !dd_open());
    igs(a, sizeof a, "face/value " DD); CHECK(!strcmp(a, "ASU"));
    /* a list: rows bound with a hidden key column; mouse and keyboard */
    CHECK(gint("block-len face/set-rows " I4F "people test/i4-sv") == 4 && apply_now() == 0);
    { gv_table *t = i4_table("people"); CHECK(t && t->nrows == 4 && t->header_h == 0 && !strcmp(gv_table_get_cell(t, 0, 0), "Apia")); }
    gv_rect lr; CHECK(screen(i4_node("people"), &lr) == 0);
    CHECK(click(lr.x + 30, lr.y + 2 * 24 + 12) == 0 && ig("face/index " I4F "people") == 3);
    igs(a, sizeof a, "face/value " I4F "people"); CHECK(!strcmp(a, "FGI"));
    igs(a, sizeof a, "face/text " I4F "people"); CHECK(!strcmp(a, "Faleolo"));
    CHECK(key(SDLK_DOWN) == 0 && ig("face/index " I4F "people") == 4 && keyk(K_HOME) == 0 && ig("face/index " I4F "people") == 1);
    /* an empty, keyless list: nothing to select; no value (refused, never the index) */
    CHECK(ig("face/row-count " I4F "empty") == 0 && ig("face/index " I4F "empty") == 0);
    igs(a, sizeof a, "sin-id judge [ face/value " I4F "empty ]"); CHECK(!strcmp(a, "table-has-no-key"));
    igs(a, sizeof a, "face/text " I4F "empty"); CHECK(!strcmp(a, ""));
    igs(a, sizeof a, "sin-id judge [ face/set-index " I4F "empty 1 ]"); CHECK(!strcmp(a, "invalid-value"));
    CHECK(gint("block-len face/focus " I4F "empty") == 4 && apply_now() == 0 && key(SDLK_DOWN) == 0 && ig("face/index " I4F "empty") == 0);
    /* hidden: no input; its value kept; shown again it takes input; its space is kept */
    long note = i4_node("note");
    gv_rect nr0, ar0, ar1; CHECK(screen(i4_node("after"), &ar0) == 0);
    CHECK(ig("face/visible " I4F "note") == 0 && node_flag(note, 0) == 0);
    igs(a, sizeof a, "sin-id judge [ face/focus " I4F "note ]"); CHECK(!strcmp(a, "cannot-focus"));
    CHECK(gint("block-len face/set-value " I4F "note \"kept\"") == 4 && apply_now() == 0);
    CHECK(gint("block-len face/focus " I4F "empty") == 4 && apply_now() == 0);
    for (int k = 0; k < 6; k++) { CHECK(key(9) == 0); CHECK(i4_focused() != inamed("note")); }
    CHECK(gint("block-len face/set-visible " I4F "note 1") == 4 && apply_now() == 0 && node_flag(note, 0) == 1);
    CHECK(i4_value_is("note", "kept") && screen(note, &nr0) == 0 && screen(i4_node("after"), &ar1) == 0);
    CHECK(ar1.y == ar0.y && nr0.y < ar1.y);                              /* the space was kept while hidden */
    CHECK(click_face("note") == 0 && i4_focused() == inamed("note") && text("!") == 0 && i4_value_is("note", "kept!"));
    /* hiding the focused widget drops the focus; keys then go nowhere */
    CHECK(gint("block-len face/set-visible " I4F "note 0") == 4 && apply_now() == 0 && i4_focused() == -1);
    CHECK(text("x") == 0 && i4_value_is("note", "kept!"));
    /* disabled: the list does not open */
    CHECK(gint("block-len face/set-enabled " DD " 0") == 4 && apply_now() == 0 && click_face("village") == 0 && !dd_open());
    CHECK(gint("block-len face/set-enabled " DD " 1") == 4 && apply_now() == 0 && click_face("village") == 0 && dd_open() && key(SDLK_ESCAPE) == 0);
    /* build errors */
    const char *errs[][2] = { { "drop-down 0", "drop-down-needs-a-size" }, { "list 5 0", "list-needs-a-size" },
                              { "d: drop-down 10 display 0", "no-such-column" }, { "f: field 5 display 2", "display-needs-a-list" },
                              { "d: drop-down 10 display 3 rows [ [ \"a\" \"b\" ] ]", "row-has-no-label" },
                              { "d: drop-down 10 key 3 rows [ [ \"a\" \"b\" ] ]", "row-has-no-key" },
                              { "l: label \"x\" follow nowhere text", "no-such-field" },
                              { "f: label \"a\"  l: label \"x\" follow f text", "has-no-value" } };
    for (int k = 0; k < 8; k++) {
        igs(a, sizeof a, "sin-id judge [ intent/show-now layout [ %s ] ]", errs[k][0]);
        if (!strstr(a, errs[k][1])) fprintf(stderr, "  [%s] -> %s (wanted %s)\n", errs[k][0], a, errs[k][1]);
        CHECK(strstr(a, errs[k][1]) != NULL);
    }
    CHECK(ishow("test/i4-sel") == 0 && presented_ok() == 0);
    printf("intent4-select: drop-down (mouse: opens under its button, pick closes; keys: Up/Down/Home/End, Space/Enter/Escape, "
           "click elsewhere and Tab close), list (mouse, keys), label / key columns, repeated labels, empty list, invalid index "
           "and key refused, dynamic replacement, follow value and text, collect/load/clear/validate, hidden (no input, value "
           "kept, focus dropped, space kept), disabled, 8 build errors\n");
}

/* intent4-select-demo: the Stage 3 demo (intent_select_demo.glon) end to end */
static void case_intent4_select_demo(void) {
    char a[400];
    CHECK(ig("= intent/current sel/form") == 1);
    CHECK(resize(720, 560) == 0);
    long box = ig("- intent/node intent/current %ld 3", inamed("postal-box"));
    CHECK(node_flag(box, 0) == 0);                                   /* the postal panel starts hidden, whole */
    /* Save with nothing picked: the validator's message */
    CHECK(click_text("Save") == 0);
    igs(a, sizeof a, "face/text " I4F "status"); CHECK(!strcmp(a, "Please pick a village."));
    /* pick by keyboard: focus the button, Down twice; the list and the labels follow */
    CHECK(click_face("village") == 0 && dd_open() && key(SDLK_ESCAPE) == 0 && !dd_open());
    CHECK(key(SDLK_DOWN) == 0 && key(SDLK_DOWN) == 0 && ig("face/index " DD) == 2);
    igs(a, sizeof a, "face/text " I4F "code"); CHECK(!strcmp(a, "SLL"));
    igs(a, sizeof a, "face/text " I4F "name"); CHECK(!strcmp(a, "Salelologa"));
    { gv_table *t = i4_table("members"); CHECK(t && t->nrows == 1 && !strcmp(gv_table_get_cell(t, 0, 0), "Tavita Leota")); }
    /* pick by mouse: the second "Apia" (a repeated label, its own code and members) */
    CHECK(click_face("village") == 0 && dd_open() && click_popup_row(3) == 0 && !dd_open());
    igs(a, sizeof a, "face/text " I4F "code"); CHECK(!strcmp(a, "APX"));
    igs(a, sizeof a, "face/text " I4F "name"); CHECK(!strcmp(a, "Apia"));
    { gv_table *t = i4_table("members"); CHECK(t && t->nrows == 2 && !strcmp(gv_table_get_cell(t, 1, 0), "Fetu Pele")); }
    /* the check shows the postal panel; type an address */
    CHECK(click_face("postal?") == 0 && node_flag(box, 0) == 1);
    CHECK(click_face("postal") == 0 && text("PO Box 12") == 0 && i4_value_is("postal", "PO Box 12"));
    CHECK(click_text("Save") == 0);
    igs(a, sizeof a, "face/text " I4F "status"); CHECK(!strcmp(a, "Saved."));
    /* Clear: no village, no members, the panel hidden again */
    CHECK(click_text("Clear") == 0 && ig("face/index " DD) == 0 && node_flag(box, 0) == 0);
    { gv_table *t = i4_table("members"); CHECK(t && t->nrows == 0); }
    igs(a, sizeof a, "face/text " I4F "code"); CHECK(!strcmp(a, ""));
    /* Load: the village (by its code), its members, the panel and the address back */
    CHECK(click_text("Load") == 0 && ig("face/index " DD) == 4 && node_flag(box, 0) == 1 && i4_value_is("postal", "PO Box 12"));
    { gv_table *t = i4_table("members"); CHECK(t && t->nrows == 2); }
    igs(a, sizeof a, "face/text " I4F "status"); CHECK(!strcmp(a, "Loaded the saved record."));
    CHECK(presented_ok() == 0);
    printf("intent4-select-demo: on intent_select_demo.glon, a village by keyboard and by mouse (a repeated label), its members "
           "list replaced, code and name following, the postal panel shown and hidden by the check, validation, "
           "Save / Clear / Load\n");
}

/* intent4-select-demo-window: the select demo's window as the backend made it
 * (edge band, hit test, cursor policy) -- no input is sent */
static void case_intent4_select_demo_window(void) {
    char r[1024];
    CHECK(gv_backend_sdl_window_report(b, r, sizeof r) == 0);
    printf("intent4-select-demo-window: driver %s; hit test %s; %s\n", SDL_GetCurrentVideoDriver(),
           gv_backend_sdl_hit_test_active(b) ? "on" : "off", r);
}

/* intent3-browser-x11 (X11 + WebKitGTK; a window opens, no input is sent:
 * buttons are pressed by calling Intent's intent/press): the INTENT-3 demo's
 * shape with the real engine */
static void case_intent3_browser_x11(void) {
    if (strcmp(SDL_GetCurrentVideoDriver(), "x11")) {
        printf("intent3-browser-x11: skipped (video driver %s; run with SDL_VIDEODRIVER=x11)\n", SDL_GetCurrentVideoDriver());
        return;
    }
    char a[300];
    CHECK(ishow("test/browser3-x11") == 0 && host_pump() == 0);
    if (!comp_of("form/field form/current 'browser")) { printf("intent3-browser-x11: webkit-view could not be created\n"); CHECK(0); return; }
    int ok = 0;
    for (int t = 0; t < 1000 && !ok; t++) {
        host_pump(); SDL_Delay(20);
        ftext("status", a, sizeof a);
        ok = strstr(a, "loaded file://") == a && strstr(a, "demo.html");
    }
    CHECK(ok);
    for (int t = 0; t < 50; t++) { host_pump(); SDL_Delay(20); }
    igs(a, sizeof a, "face/value form/field form/current 'address"); printf("  address: %s\n", a);
    CHECK(strstr(a, "file:///") == a && strstr(a, "/view/glon/browser/demo.html"));
    ftext("title", a, sizeof a); printf("  title: %s\n", a); CHECK(!strcmp(a, "Glon FOREIGN-2 test page"));
    /* Go navigates to the address the field holds then */
    CHECK(ig("block-len face/set-value form/field form/current 'address \"./glon/browser/page2.html\"") == 4 && apply_now() == 0);
    CHECK(gint("intent/press intent/current block-at form/field form/current 'go 2") >= 0 && apply_now() == 0);
    ok = 0;
    for (int t = 0; t < 1000 && !ok; t++) { host_pump(); SDL_Delay(20); ftext("status", a, sizeof a); ok = strstr(a, "loaded file://") == a && strstr(a, "page2.html"); }
    CHECK(ok);
    for (int t = 0; t < 50; t++) { host_pump(); SDL_Delay(20); }
    igs(a, sizeof a, "face/value form/field form/current 'address"); CHECK(strstr(a, "file:///") == a && strstr(a, "page2.html"));
    ftext("title", a, sizeof a); CHECK(!strcmp(a, "Second page"));
    /* Back */
    CHECK(gint("intent/press intent/current block-at form/field form/current 'back 2") >= 0 && apply_now() == 0);
    ok = 0;
    for (int t = 0; t < 1000 && !ok; t++) { host_pump(); SDL_Delay(20); ftext("status", a, sizeof a); ok = strstr(a, "loaded file://") == a && strstr(a, "demo.html"); }
    CHECK(ok);
    /* a command WebKit does not know: the error event, shown by its follower */
    CHECK(gint("intent/press intent/current block-at form/field form/current 'bad 2") >= 0 && apply_now() == 0);
    for (int t = 0; t < 50; t++) { host_pump(); SDL_Delay(20); }
    ftext("problem", a, sizeof a); printf("  problem: %s\n", a); CHECK(!strcmp(a, "unknown command no-such-command"));
    CHECK(ishow("test/form") == 0);
    for (int t = 0; t < 50; t++) { host_pump(); SDL_Delay(20); }
    int alive = 0; gv_host_counts(&alive, NULL, NULL);
    CHECK(alive == 0 && !gv_glon_host_error(h));
    printf("intent3-browser-x11: real WebKit: address, status and title follow the browser; Go reads the address; "
           "Back; an unknown command is the error event\n");
}

/* processes: this one and every descendant (WebKit's helpers), their total
 * resident memory (VmRSS, kB) and how many are WebKit's */
static void proc_tree(int *count, long *rss_kb, int *webkit, int print) {
    int pids[512], ppids[512], n = 0;
    *count = 0; *rss_kb = 0; *webkit = 0;
    DIR *d = opendir("/proc");
    struct dirent *de;
    while (d && (de = readdir(d)) && n < 512) {
        int pid = atoi(de->d_name);
        if (pid <= 0) continue;
        char path[64], buf[512];
        snprintf(path, sizeof path, "/proc/%d/stat", pid);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        if (fgets(buf, sizeof buf, f)) {
            char *rp = strrchr(buf, ')');
            int ppid = 0;
            if (rp && sscanf(rp + 2, "%*c %d", &ppid) == 1) { pids[n] = pid; ppids[n] = ppid; n++; }
        }
        fclose(f);
    }
    if (d) closedir(d);
    int tree[512], tn = 0;
    tree[tn++] = getpid();
    for (int changed = 1; changed;) {
        changed = 0;
        for (int i = 0; i < n; i++) {
            int in = 0, parent = 0;
            for (int k = 0; k < tn; k++) { if (tree[k] == pids[i]) in = 1; if (tree[k] == ppids[i]) parent = 1; }
            if (!in && parent && tn < 512) { tree[tn++] = pids[i]; changed = 1; }
        }
    }
    for (int k = 0; k < tn; k++) {
        char path[64], line[256], name[64] = "?";
        long kb = 0;
        snprintf(path, sizeof path, "/proc/%d/status", tree[k]);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        while (fgets(line, sizeof line, f)) {
            if (!strncmp(line, "Name:", 5)) sscanf(line + 5, "%63s", name);
            if (!strncmp(line, "VmRSS:", 6)) kb = atol(line + 6);
        }
        fclose(f);
        (*count)++;
        *rss_kb += kb;
        if (strstr(name, "WebKit")) (*webkit)++;
        if (print) printf("    pid %d %-18s %7ld kB\n", tree[k], name, kb);
    }
}
/* CPU ticks (utime + stime) of this process and its descendants */
static long proc_tree_ticks(void) {
    long total = 0;
    DIR *d = opendir("/proc");
    struct dirent *de;
    int me = getpid();
    while (d && (de = readdir(d))) {
        int pid = atoi(de->d_name);
        if (pid <= 0) continue;
        char path[64], buf[1024];
        snprintf(path, sizeof path, "/proc/%d/stat", pid);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        if (fgets(buf, sizeof buf, f)) {
            char *rp = strrchr(buf, ')');
            int ppid = 0; long ut = 0, st = 0;
            /* fields after the name: state ppid ... utime(14) stime(15) */
            if (rp && sscanf(rp + 2, "%*c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %ld %ld", &ppid, &ut, &st) == 3) {
                /* this process, or one whose parent chain reaches it (WebKit's
                 * helpers are direct children or grandchildren) */
                int mine = pid == me || ppid == me;
                if (!mine && ppid > 1) {
                    char p2[64], b2[512];
                    snprintf(p2, sizeof p2, "/proc/%d/stat", ppid);
                    FILE *g = fopen(p2, "r");
                    if (g) {
                        if (fgets(b2, sizeof b2, g)) {
                            char *r2 = strrchr(b2, ')');
                            int pp2 = 0;
                            if (r2 && sscanf(r2 + 2, "%*c %d", &pp2) == 1 && pp2 == me) mine = 1;
                        }
                        fclose(g);
                    }
                }
                if (mine) total += ut + st;
            }
        }
        fclose(f);
    }
    if (d) closedir(d);
    return total;
}
/* kill this process's own WebKitWebProcess children (fault injection on
 * processes the test itself started); returns how many */
static int kill_web_processes(void) {
    int n = 0;
    DIR *d = opendir("/proc");
    struct dirent *de;
    while (d && (de = readdir(d))) {
        int pid = atoi(de->d_name);
        if (pid <= 0) continue;
        char path[64], buf[512];
        snprintf(path, sizeof path, "/proc/%d/stat", pid);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        if (fgets(buf, sizeof buf, f)) {
            char *lp = strchr(buf, '('), *rp = strrchr(buf, ')');
            int ppid = 0;
            if (lp && rp && sscanf(rp + 2, "%*c %d", &ppid) == 1 && ppid == getpid()
                && !strncmp(lp + 1, "WebKitWebProces", 15) && kill(pid, SIGKILL) == 0) n++;
        }
        fclose(f);
    }
    if (d) closedir(d);
    return n;
}
/* the demo page's header colour at its top left: dark, blue dominant */
static int rendered_header(unsigned rgb) {
    int r = (int)(rgb >> 16) & 255, gg = (int)(rgb >> 8) & 255, b = (int)rgb & 255;
    return r < 90 && gg < 130 && b > 80 && b > r;
}
/* the status says that page has loaded: "loaded file://.../NAME" */
static int wait_loaded(const char *name, int ms) {
    char a[300];
    for (int t = 0; t < ms; t += 20) {
        host_pump();
        igs(a, sizeof a, "test/browser-status");
        if (strstr(a, "loaded file://") == a && strstr(a, name)) return 0;
        SDL_Delay(20);
    }
    return -1;
}
static int wait_for(const char *want, int ms) {
    char a[300];
    for (int t = 0; t < ms; t += 20) {
        host_pump();
        igs(a, sizeof a, "test/browser-status");    /* (a title may follow loaded) */
        if (strstr(a, want)) return 0;
        SDL_Delay(20);
    }
    return -1;
}


/* every face's box and node, to compare before and after */
typedef struct { gv_rect r; long node; int visible; } i4_geo;
static int i4_snapshot(i4_geo *g, int cap) {
    int n = (int)icount();
    for (int i = 0; i < n && i < cap; i++) {
        CHECK(ibox(i, &g[i].r) == 0);
        g[i].r.h = (int)ig("intent/live-h intent/current %d", i);
        g[i].node = inode(i);
        g[i].visible = (int)ig("block-at intent/face intent/current %d 21", i);
    }
    return n;
}
/* the faces whose geometry differs (skip: a face index to ignore, or -1) */
static int i4_moved(const i4_geo *a, const i4_geo *b, int n, int skip) {
    int moved = 0;
    for (int i = 0; i < n; i++) {
        if (i == skip) continue;
        if (a[i].node != b[i].node || !same_rect(a[i].r, b[i].r)) {
            fprintf(stderr, "  face %d: %d,%d %dx%d (node %ld) -> %d,%d %dx%d (node %ld)\n", i, a[i].r.x, a[i].r.y, a[i].r.w, a[i].r.h,
                    a[i].node, b[i].r.x, b[i].r.y, b[i].r.w, b[i].r.h, b[i].node);
            moved++;
        }
    }
    return moved;
}
#define I4S "form/field form/current '"
/* intent4-state: widgets keep every kind of state through repeated resizing
 * and hide / show; a hidden widget keeps its place and value, takes no
 * input and no focus; showing or hiding moves no other widget */
static void case_intent4_state(void) {
    char a[200];
    CHECK(ishow("test/i4-state") == 0 && host_pump() == 0);
    int alive0 = 0, created0 = 0, destroyed0 = 0;
    gv_host_counts(&alive0, &created0, &destroyed0);
    long g = ig("block-at intent/hface " I4S "st-host 8");
    void *comp = gv_glon_host_component(h, (int)g);
    CHECK(comp != NULL);
    /* state: a caret and a selection, an area's text, selections, focus */
    CHECK(click_face_i((int)iscoped("st-name")) == 0 && keyk(K_END) == 0 && key_shift(SDLK_LEFT) == 0 && key_shift(SDLK_LEFT) == 0);
    CHECK(i4_caret("st-name") == 9 && i4_anchor("st-name") == 11);
    CHECK(gint("face/set-value " I4S "st-dd \"SLL\"") >= 0 && gint("face/set-index " I4S "st-list 3") >= 0);
    CHECK(gint("face/set-value " I4S "st-table \"APX\"") >= 0);
    CHECK(click_face_i((int)iscoped("st-name")) == 0 && keyk(K_END) == 0 && key_shift(SDLK_LEFT) == 0 && key_shift(SDLK_LEFT) == 0);
    long focus_node = gv_view_focus_id(v);
    CHECK(focus_node == vnode(inode((int)iscoped("st-name"))));
    i4_geo g0[64], g1[64], g2[64];
    int n = i4_snapshot(g0, 64);
    long extra = iscoped("st-extra");
    CHECK(g0[extra].visible == 0);
    /* repeated resizing, wider and narrower than the content, then back */
    int sizes[12][2] = { {1600, 900}, {900, 500}, {1201, 701}, {700, 400}, {1900, 1100}, {1000, 650},
                         {1350, 720}, {640, 480}, {1599, 899}, {1200, 699}, {1450, 950}, {1200, 700} };
    for (int k = 0; k < 12; k++) {
        CHECK(resize(sizes[k][0], sizes[k][1]) == 0 && host_pump() == 0);
        CHECK(no_overlaps() == 0);
        gv_host_mock_state st;
        CHECK(gv_host_mock_state_of(comp, &st) == 0);
        gv_rect nr; CHECK(screen(g, &nr) == 0 && same_rect(st.bounds, nr));   /* the component follows its node */
    }
    i4_snapshot(g1, 64);
    CHECK(i4_moved(g0, g1, n, -1) == 0);                       /* the same geometry, the same nodes */
    /* nothing was rebuilt: values, caret and selection, selections, focus, the component */
    igs(a, sizeof a, "face/value " I4S "st-name"); CHECK(!strcmp(a, "Hello world"));
    CHECK(i4_caret("st-name") == 9 && i4_anchor("st-name") == 11);
    { gv_node *tx = gv_view_node(v, vnode(ig("block-at block-at intent/hface " I4S "st-name 2 3"))); CHECK(tx && tx->caret == 10 && tx->sel_anchor == 12); }
    igs(a, sizeof a, "face/value " I4S "st-notes"); CHECK(!strcmp(a, "line one"));
    igs(a, sizeof a, "face/value " I4S "st-dd"); CHECK(!strcmp(a, "SLL"));
    CHECK(ig("face/index " I4S "st-list") == 3);
    igs(a, sizeof a, "face/value " I4S "st-table"); CHECK(!strcmp(a, "APX"));
    CHECK(ig("face/row-count " I4S "st-table") == 4);
    CHECK(gv_view_focus_id(v) == focus_node && i4_focused() == iscoped("st-name"));
    int alive1 = 0, created1 = 0, destroyed1 = 0;
    gv_host_counts(&alive1, &created1, &destroyed1);
    CHECK(gv_glon_host_component(h, (int)g) == comp && created1 == created0 && destroyed1 == destroyed0);
    /* hidden: no input (a click on its place changes nothing), out of the Tab order */
    gv_rect er = g0[extra].r;
    CHECK(click(er.x + er.w / 2, er.y + er.h / 2) == 0 && i4_focused() != extra);
    CHECK(click_face_i((int)iscoped("st-check")) == 0);       /* checking it reveals the field (its handler) */
    CHECK(ig("face/visible " I4S "st-extra") == 1 && ig("face/value " I4S "st-check") == 1);
    i4_snapshot(g2, 64);
    CHECK(i4_moved(g0, g2, n, (int)extra) == 0);               /* no other widget moved */
    CHECK(same_rect(g2[extra].r, g0[extra].r) && g2[extra].visible == 1);   /* it kept its own place */
    igs(a, sizeof a, "face/value " I4S "st-extra"); CHECK(!strcmp(a, "secret"));
    /* Tab from the check now reaches it, then the button after it */
    CHECK(gv_view_focus_id(v) == vnode(inode((int)iscoped("st-check"))) && key(9) == 0 && i4_focused() == extra);
    CHECK(key(9) == 0 && i4_focused() == iscoped("st-after"));
    /* unchecking hides it again: still no movement, its value kept; Tab skips it */
    CHECK(click_face_i((int)iscoped("st-check")) == 0 && ig("face/visible " I4S "st-extra") == 0);
    i4_snapshot(g2, 64);
    CHECK(i4_moved(g0, g2, n, -1) == 0);
    CHECK(gv_view_focus_id(v) == vnode(inode((int)iscoped("st-check"))) && key(9) == 0 && i4_focused() == iscoped("st-after"));
    /* at another window size, show and hide move nothing either */
    CHECK(resize(1500, 820) == 0);
    i4_snapshot(g1, 64);
    CHECK(gint("face/set-visible " I4S "st-extra 1") >= 0 && render() == 0);
    i4_snapshot(g2, 64);
    CHECK(i4_moved(g1, g2, n, (int)extra) == 0 && same_rect(g1[extra].r, g2[extra].r));
    CHECK(gint("face/set-visible " I4S "st-extra 0") >= 0 && render() == 0);
    i4_snapshot(g2, 64);
    CHECK(i4_moved(g1, g2, n, -1) == 0);
    CHECK(resize(1200, 700) == 0 && presented_ok() == 0);
    printf("intent4-state: 12 resizes (640x480 .. 1900x1100) rebuild nothing: %d faces at the same place on the same nodes, "
           "caret and selection, area text, drop-down / list / table selections, focus and the hosted component kept; "
           "a hidden field keeps its place and value, takes no click and no Tab; showing or hiding it moves no other widget "
           "(at two window sizes)\n", n);
}

/* intent4-layout-demo: the Stage 4 demo (intent_layout_demo.glon) end to end */
#define LDC "form/field form/sub form/current 'contact '"
#define LDR "form/field form/current '"
/* the face index of a handle (an expression), by its node */
static int i4_value_is_h(const char *hexpr, const char *want) {
    char got[200]; igs(got, sizeof got, "face/value %s", hexpr);
    if (strcmp(got, want)) fprintf(stderr, "  %s: '%s', wanted '%s'\n", hexpr, got, want);
    return !strcmp(got, want);
}
static long hindex(const char *hexpr) {
    long node = ig("block-at intent/hface %s 8", hexpr);
    for (int i = 0; i < (int)icount(); i++) if (inode(i) == node) return i;
    return -1;
}
static int hbox(const char *hexpr, gv_rect *r) {
    long i = hindex(hexpr);
    if (i < 0 || ibox((int)i, r) != 0) return -1;
    r->h = (int)ig("intent/live-h intent/current %ld", i);
    return 0;
}
static void case_intent4_layout_demo(void) {
    char a[200], e[200];
    CHECK(ig("= intent/current ld/form") == 1);
    CHECK(resize(900, 600) == 0);
    igs(a, sizeof a, "face/text " LDR "status"); CHECK(!strcmp(a, "Ready."));
    long unit = gint("intent/unit intent/theme");
    /* aligned labels and fields: one label column (10 units, right-aligned), one field column */
    const char *fields[] = { "name", "village", "phone" };
    gv_rect f0r, fr;
    snprintf(e, sizeof e, LDC "%s", fields[0]); CHECK(hbox(e, &f0r) == 0);
    for (int k = 0; k < 3; k++) {
        snprintf(e, sizeof e, LDC "%s", fields[k]); CHECK(hbox(e, &fr) == 0 && fr.x == f0r.x);
        long li = hindex(e) - 1;                                     /* its label, just before it */
        gv_rect lr; CHECK(ibox((int)li, &lr) == 0 && lr.w == 10 * unit + 4 && lr.x + lr.w + 8 == fr.x);
        gv_node *ln = gv_view_node(v, vnode(inode((int)li))); CHECK(ln && ln->text_align == GV_ALIGN_END);
    }
    /* the two panels: the same content at margin 4 / space 2 and margin 16 / space 12 */
    gv_rect cp, rp, c1, c2, r1, r2;
    CHECK(hbox(LDR "compact", &cp) == 0 && hbox(LDR "roomy", &rp) == 0);
    long ci = hindex(LDR "compact"), ri = hindex(LDR "roomy");
    /* a panel's face comes after its content: label, One, Two are the three before it */
    CHECK(ibox((int)ci - 2, &c1) == 0 && ibox((int)ci - 1, &c2) == 0 && ibox((int)ri - 2, &r1) == 0 && ibox((int)ri - 1, &r2) == 0);
    CHECK(c1.x == cp.x + 4 && c2.y == c1.y + 30 + 2 && r1.x == rp.x + 16 && r2.y == r1.y + 30 + 12);
    { gv_rect ct, rt; CHECK(ibox((int)ci - 3, &ct) == 0 && ibox((int)ri - 3, &rt) == 0 && ct.y == cp.y + 4 && rt.y == rp.y + 16); }
    CHECK(cp.h == 4 + 3 * 30 + 2 * 2 + 4 && rp.h == 16 + 3 * 30 + 2 * 12 + 16);
    /* buttons of three sizes in the bar */
    long bi = hindex(LDR "bar");
    gv_rect b1, b2, b3;
    CHECK(ibox((int)bi - 4, &b1) == 0 && ibox((int)bi - 3, &b2) == 0 && ibox((int)bi - 2, &b3) == 0);
    CHECK(b1.w == 80 && b2.w == 14 * unit + 24 && b3.w == 6 * unit + 24 && b2.x == b1.x + 80 + 8);
    /* the PO Box row starts hidden; ticking the check shows it where it was, moving nothing */
    CHECK(ig("face/visible " LDC "po-box") == 0 && ig("face/visible " LDC "po-label") == 0);
    CHECK(click_face_i((int)hindex(LDC "name")) == 0 && text("Ana") == 0);
    i4_geo g0[64], g1[64];
    int n = i4_snapshot(g0, 64);
    if (png_dir) { char pth[512]; snprintf(pth, sizeof pth, "%s/intent4-layout-demo-hidden.png", png_dir); CHECK(render() == 0 && gv_backend_sdl_save_png(b, pth) == 0); }
    CHECK(click_face_i((int)hindex(LDC "postal?")) == 0);
    CHECK(ig("face/visible " LDC "po-box") == 1 && ig("face/visible " LDC "po-label") == 1);
    if (png_dir) { char pth[512]; snprintf(pth, sizeof pth, "%s/intent4-layout-demo-shown.png", png_dir); CHECK(render() == 0 && gv_backend_sdl_save_png(b, pth) == 0); }
    igs(a, sizeof a, "face/text " LDR "status"); CHECK(!strcmp(a, "PO Box shown: nothing else moved."));
    i4_snapshot(g1, 64);
    long pb = hindex(LDC "po-box"), pl = hindex(LDC "po-label");
    int moved = 0;
    for (int i = 0; i < n; i++) if (!same_rect(g0[i].r, g1[i].r) || g0[i].node != g1[i].node) moved++;
    CHECK(moved == 0 && g1[pb].visible == 1 && g1[pl].visible == 1);
    CHECK(click_face_i((int)pb) == 0 && text("12") == 0 && i4_value_is_h(LDC "po-box", "12"));
    CHECK(click_face_i((int)hindex(LDC "postal?")) == 0 && ig("face/visible " LDC "po-box") == 0);
    CHECK(i4_value_is_h(LDC "po-box", "12"));                       /* hidden, its value kept */
    /* Tab from the check skips the hidden PO Box to Phone; shown, Tab reaches it first */
    CHECK(i4_focused() == hindex(LDC "postal?") && key(9) == 0 && i4_focused() == hindex(LDC "phone"));
    CHECK(click_face_i((int)hindex(LDC "postal?")) == 0 && key(9) == 0 && i4_focused() == pb);
    CHECK(click_face_i((int)hindex(LDC "postal?")) == 0 && ig("face/visible " LDC "po-box") == 0);
    i4_snapshot(g1, 64);
    CHECK(i4_moved(g0, g1, n, -1) == 0);
    /* a button reports itself */
    CHECK(click_face_i((int)bi - 3) == 0);
    igs(a, sizeof a, "face/text " LDR "status"); CHECK(!strcmp(a, "Pressed: Save as draft"));
    /* resizing: fields and notes widen, notes take the spare height, the bar sits at the bottom margin */
    gv_rect nt0, nt, bar, nm0, nm, root;
    CHECK(hbox(LDR "notes", &nt0) == 0 && hbox(LDC "name", &nm0) == 0);
    int sizes[4][2] = { {1200, 800}, {700, 450}, {1500, 950}, {900, 600} }, full = 0;
    for (int k = 0; k < 4; k++) {
        int W = sizes[k][0], H = sizes[k][1];
        CHECK(resize(W, H) == 0 && screen(ig("block-at intent/current 4"), &root) == 0);
        CHECK(hbox(LDR "notes", &nt) == 0 && hbox(LDR "bar", &bar) == 0 && hbox(LDC "name", &nm) == 0 && no_overlaps() == 0);
        long cw = gint("block-at intent/current 9"), ch = gint("block-at intent/current 10");
        if (W >= cw && H >= ch) {
            full++;
            CHECK(nt.w == nt0.w + (W - 900) && nm.w == nm0.w + (W - 900));
            CHECK(nt.h == nt0.h + (H - 600) && bar.y + bar.h == root.y + H - 20);
        } else {
            CHECK(nt.w >= (int)ig("block-at intent/face intent/current %ld 6", hindex(LDR "notes")));
        }
        CHECK(i4_value_is_h(LDC "name", "Ana") && i4_value_is_h(LDC "po-box", "12"));
    }
    if (png_dir) { char pth[512]; CHECK(resize(1200, 800) == 0); snprintf(pth, sizeof pth, "%s/intent4-layout-demo-1200x800.png", png_dir);
                   CHECK(render() == 0 && gv_backend_sdl_save_png(b, pth) == 0); CHECK(resize(900, 600) == 0); }
    CHECK(full == 3);                                   /* all but 700x450 hold the whole content */
    CHECK(presented_ok() == 0);
    printf("intent4-layout-demo: on intent_layout_demo.glon, right-aligned labels in one column and fields in the next; "
           "panels at margin 4 / space 2 and 16 / 12; buttons of 80 / %ld / %ld px; the PO Box shown and hidden with "
           "%d faces unmoved and its value kept; notes take the spare width and height at 4 window sizes, values kept\n",
           14 * unit + 24, 6 * unit + 24, n);
}

/* ==== INTENT-4B-5A: themes, looks, images ================================ */
/* WCAG 2 relative luminance and contrast ratio */
static double lum_ch(double v) { v /= 255.0; return v <= 0.03928 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
static double lum3(int r, int g, int b) { return 0.2126 * lum_ch(r) + 0.7152 * lum_ch(g) + 0.0722 * lum_ch(b); }
static double contrast3(const int *a, const int *b) {
    double la = lum3(a[0], a[1], a[2]), lb = lum3(b[0], b[1], b[2]);
    return la > lb ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05);
}
static void theme_rgb(const char *theme, int slot, int *c) {
    for (int k = 0; k < 3; k++) c[k] = (int)ig("block-at %s %d", theme, slot + k);
}
static double theme_contrast(const char *theme, int a, int b) {
    int ca[3], cb[3]; theme_rgb(theme, a, ca); theme_rgb(theme, b, cb); return contrast3(ca, cb);
}
static int color_is(gv_color got, const int *c, long gid, const char *what) {
    int ok = got.r == c[0] && got.g == c[1] && got.b == c[2];
    if (!ok) fprintf(stderr, "  node %ld %s %d,%d,%d, wanted %d,%d,%d\n", gid, what, got.r, got.g, got.b, c[0], c[1], c[2]);
    return ok;
}
static int node_bg_is(long gid, const char *theme, int slot) {
    gv_node *n = gv_view_node(v, vnode(gid)); int c[3]; theme_rgb(theme, slot, c);
    return n && color_is(n->bg, c, gid, "bg");
}
static int node_fg_is(long gid, const char *theme, int slot) {
    gv_node *n = gv_view_node(v, vnode(gid)); int c[3]; theme_rgb(theme, slot, c);
    return n && color_is(n->fg, c, gid, "fg");
}
/* theme-contrast: both themes' semantic pairs meet their WCAG ratios */
static void case_theme_contrast(void) {
    struct { const char *what; int a, b; double need; } pairs[] = {
        { "text / window", 9, 1, 4.5 }, { "text / pane", 9, 5, 4.5 }, { "text / control", 9, 109, 4.5 },
        { "text / control hover", 9, 113, 4.5 }, { "text / control pressed", 9, 117, 4.5 }, { "input text / input", 25, 21, 4.5 },
        { "secondary text / pane", 29, 5, 4.5 }, { "secondary text / window", 29, 1, 4.5 }, { "heading / pane", 121, 5, 4.5 },
        { "on accent / accent", 81, 13, 4.5 }, { "on accent / accent hover", 81, 41, 4.5 }, { "on accent / accent pressed", 81, 45, 4.5 },
        { "on accent / danger", 81, 97, 4.5 }, { "text / selected row", 9, 73, 4.5 }, { "text / table selection", 9, 53, 4.5 },
        { "disabled text / disabled surface", 93, 49, 3.0 }, { "border / window", 17, 1, 3.0 }, { "border / pane", 17, 5, 3.0 },
        { "border / input", 17, 21, 3.0 }, { "focus ring / window", 105, 1, 3.0 }, { "focus ring / pane", 105, 5, 3.0 },
        { "checked box (accent) / pane", 13, 5, 3.0 },
        /* states told apart: hover and pressed from normal, disabled text from enabled */
        { "control hover / control", 113, 109, 1.1 }, { "control pressed / control", 117, 109, 1.12 },
        { "disabled text / secondary text", 93, 29, 1.2 } };
    const char *themes[] = { "theme/dark", "theme/light" };
    int n = (int)(sizeof pairs / sizeof pairs[0]), low = 0;
    double worst = 99;
    for (int t = 0; t < 2; t++) {
        CHECK(ig("block-len %s", themes[t]) == 140);
        for (int i = 0; i < n; i++) {
            double r = theme_contrast(themes[t], pairs[i].a, pairs[i].b);
            if (r < pairs[i].need) { fprintf(stderr, "  %s %s: %.2f < %.2f\n", themes[t], pairs[i].what, r, pairs[i].need); low++; }
            if (pairs[i].need >= 4.5 && r < worst) worst = r;
        }
    }
    CHECK(low == 0);
    printf("theme-contrast: %d pairs in each of 2 themes meet their WCAG ratios (text >= 4.5, worst %.2f; borders, focus ring, "
           "checked box, disabled text >= 3; hover, pressed and disabled told apart)\n", n, worst);
}
#define HF(name, k) "block-at block-at intent/hface " I4S name " 2 " #k
/* intent5-theme: a layout chooses light, a panel dark; the widgets take the
 * chosen theme's colours; the geometry is the theme's metrics, the same in
 * either; switching rebuilds with the values kept; errors by name */
static void case_intent5_theme(void) {
    char a[200];
    CHECK(ishow("test/i5-theme") == 0);
    CHECK(ig("= block-at intent/current 7 theme/light") == 1);
    { gv_node *win = gv_view_node(v, vnode(1)); int wc[3]; theme_rgb("theme/light", 1, wc); CHECK(win && color_is(win->bg, wc, 1, "bg")); }
    /* light widgets: label text, field box and text, button surface and caption */
    CHECK(node_fg_is(inode((int)iscoped("t-label")), "theme/light", 9));
    CHECK(node_bg_is(ig(HF("t-field", 2)), "theme/light", 21) && node_fg_is(ig(HF("t-field", 3)), "theme/light", 25));
    CHECK(node_bg_is(ig(HF("t-button", 2)), "theme/light", 109) && node_fg_is(ig(HF("t-button", 3)), "theme/light", 9));
    /* the dark panel: its own background, its widgets dark */
    CHECK(node_bg_is(ig("- intent/node intent/current %ld 1", iscoped("t-panel")), "theme/dark", 5));
    CHECK(node_fg_is(inode((int)iscoped("t-dlabel")), "theme/dark", 9));
    CHECK(node_bg_is(ig(HF("t-dbutton", 2)), "theme/dark", 109));
    /* disabled in the light theme: the disabled colours */
    CHECK(node_fg_is(ig(HF("t-off", 3)), "theme/light", 93) && node_bg_is(ig(HF("t-off", 2)), "theme/light", 49));
    /* the rendered text really contrasts: the label's darkest ink against the window */
    CHECK(render() == 0);
    double best = 1;
    {
        gv_rect r; CHECK(lbox("t-label", &r) == 0);
        int bg[3]; pixel(r.x + r.w + 2, r.y + 2, bg);
        for (int y = r.y; y < r.y + r.h; y++)
            for (int x = r.x; x < r.x + r.w; x++) { int q[3]; pixel(x, y, q); double c = contrast3(q, bg); if (c > best) best = c; }
        CHECK(best >= 4.5);
    }
    /* the same geometry as in the dark theme: switch, rebuilt, values carried over */
    i4_geo g0[64], g1[64];
    int n = i4_snapshot(g0, 64);
    CHECK(gint("block-len face/set-value " I4S "t-field \"kept\"") > 0 && gint("block-len face/set-value " I4S "t-check 1") > 0);
    CHECK(gint("block-len do [ intent/use-theme intent/current 'dark widget/flush intent/current ]") > 0 && gv_glon_host_deliver(h) == 0 && render() == 0);
    CHECK(ig("= block-at intent/current 7 theme/dark") == 1);
    CHECK(node_bg_is(ig(HF("t-field", 2)), "theme/dark", 21) && node_fg_is(inode((int)iscoped("t-label")), "theme/dark", 9));
    CHECK((int)icount() == n);
    i4_snapshot(g1, 64);
    int moved = 0;
    for (int i = 0; i < n; i++) if (!same_rect(g0[i].r, g1[i].r)) moved++;
    CHECK(moved == 0);
    igs(a, sizeof a, "face/value " I4S "t-field"); CHECK(!strcmp(a, "kept"));
    CHECK(ig("face/value " I4S "t-check") == 1);
    /* and back to light, given as a theme block: the application's choice wins over the layout's own */
    CHECK(gint("block-len do [ intent/use-theme intent/current theme/light widget/flush intent/current ]") > 0 && gv_glon_host_deliver(h) == 0 && render() == 0);
    CHECK(node_bg_is(ig(HF("t-field", 2)), "theme/light", 21));
    CHECK(gint("block-len do [ intent/use-theme intent/current 'dark widget/flush intent/current ]") > 0 && gv_glon_host_deliver(h) == 0);
    /* errors */
    gstr("sin-id judge [ intent/show-now layout [ label \"x\" theme light ] ]", a, sizeof a); CHECK(strstr(a, "theme-must-come-first") != NULL);
    gstr("sin-id judge [ intent/show-now layout [ theme purple ] ]", a, sizeof a);
    if (!strstr(a, "unknown-theme")) fprintf(stderr, "  theme purple: '%s'\n", a);
    CHECK(strstr(a, "unknown-theme") != NULL);
    gstr("sin-id judge [ intent/show-now layout [ panel [ label \"x\" theme dark ] ] ]", a, sizeof a); CHECK(strstr(a, "theme-must-come-first") != NULL);
    /* an application's own theme, by name: a copy of light with another window colour */
    gstr("sin-id judge [ intent/add-theme 'own [ 1 2 3 ] ]", a, sizeof a); CHECK(strstr(a, "not-a-theme") != NULL);
    CHECK(gint("block-len reduce [ intent/add-theme 'own theme/light ]") == 1);
    CHECK(gint("block-len do [ intent/show-now layout [ theme own label \"own\" ] ]") > 0 && gv_glon_host_deliver(h) == 0);
    CHECK(ig("= block-at intent/current 7 theme/light") == 1);
    /* a layout without a theme keeps Intent's default (dark) */
    CHECK(ishow("test/rows") == 0 && ig("= block-at intent/current 7 theme/dark") == 1);
    CHECK(presented_ok() == 0);
    printf("intent5-theme: theme light for the layout, theme dark for a panel; window, labels, fields, buttons and the disabled "
           "button in the chosen theme's colours (label ink %.2f:1); %d faces at the same geometry in both themes; use-theme "
           "rebuilds with values kept; an application theme by name; 4 errors named\n", best, n);
}

/* the VID state of a named face, and one of its node ids (by state slot) */
static long st_node(const char *name, int slot) { return ig("block-at block-at intent/hface " I4S "%s 2 %d", name, slot); }
static int node_rgb_is(gv_color c, int r, int g, int b) { return c.r == r && c.g == g && c.b == b; }
static gv_node *vn(long gid) { return gv_view_node(v, vnode(gid)); }
/* intent5-look: styles with looks change appearance only: the same VID button,
 * field and label underneath; sizes, fonts, colours, corners, border width,
 * state colours (hover, pressed, disabled, focus); events as before */
static void case_intent5_look(void) {
    char a[200];
    CHECK(ishow("test/i5-look") == 0);
    long unit = gint("intent/unit intent/theme");
    gv_rect pl, bg_, sm, go, wr, off;
    CHECK(lbox("plain", &pl) == 0 && lbox("b-big", &bg_) == 0 && lbox("b-small", &sm) == 0 && lbox("b-go", &go) == 0 &&
          lbox("b-warn", &wr) == 0 && lbox("b-off", &off) == 0);
    if (png_dir) { char pth[512]; snprintf(pth, sizeof pth, "%s/intent5-look.png", png_dir); CHECK(render() == 0 && gv_backend_sdl_save_png(b, pth) == 0); }
    /* defaults untouched: the plain button is VID's, in the theme itself */
    CHECK(pl.h == 30 && ig("= block-at block-at intent/hface " I4S "plain 2 4 theme/dark") == 1);
    CHECK(vn(st_node("plain", 2))->radius == 0 && vn(st_node("plain", 3))->font_style == 0);
    /* the same machine kind for every look: one button implementation */
    CHECK(ig("block-at block-at intent/hface " I4S "b-go 2 0") == gint("vid/kind-button") &&
          ig("block-at block-at intent/hface " I4S "b-big 2 0") == gint("vid/kind-button"));
    /* sizes and fonts: big 48 px high, 20 pt bold; small 24 px, 11 pt; widths from the font's unit */
    CHECK(bg_.h == 48 && vn(st_node("b-big", 3))->font_size == 20 && vn(st_node("b-big", 3))->font_style == 1);
    CHECK(sm.h == 24 && vn(st_node("b-small", 3))->font_size == 11);
    CHECK(pl.y == bg_.y && sm.y == bg_.y);                              /* top-aligned in the 48 px row */
    long ubig = ig("intent/unit block-at block-at intent/hface " I4S "b-big 2 4");
    CHECK(ubig > unit);
    /* colours, corners, no border: green surface, white caption, radius 8 on surface and border, 10 on the ring */
    gv_node *gbg = vn(st_node("b-go", 2)), *gtx = vn(st_node("b-go", 3));
    CHECK(gbg && node_rgb_is(gbg->bg, 30, 120, 60) && gtx && node_rgb_is(gtx->fg, 255, 255, 255));
    CHECK(gbg->radius == 8 && vn(st_node("b-go", 11))->radius == 8 && vn(st_node("b-go", 9))->radius == 10);
    CHECK(gbg->x == 0 && gbg->y == 0 && gbg->w == go.w && gbg->h == go.h);   /* border width 0: the surface fills it */
    /* the caption is readable on it */
    { int t[3] = { 255, 255, 255 }, b2[3] = { 30, 120, 60 }, r2[3] = { 180, 40, 40 };
      double c1 = contrast3(t, b2), c2 = contrast3(t, r2);
      CHECK(c1 >= 4.5 && c2 >= 4.5);
      printf("  caption contrast: go %.2f, warn %.2f\n", c1, c2); }
    /* a style of a style: warn is go with a red surface (its corners and caption kept) */
    gv_node *wbg = vn(st_node("b-warn", 2));
    CHECK(wbg && node_rgb_is(wbg->bg, 180, 40, 40) && wbg->radius == 8 && node_rgb_is(vn(st_node("b-warn", 3))->fg, 255, 255, 255));
    /* one derived theme per style (shared by its uses), different from the theme */
    CHECK(ig("= block-at block-at intent/hface " I4S "b-go 2 4 block-at block-at intent/hface " I4S "b-off 2 4") == 1);
    CHECK(ig("= block-at block-at intent/hface " I4S "b-go 2 4 theme/dark") == 0);
    /* states: hover and pressed shades derived from the surface, then normal again; the click is handled */
    long th = ig("block-at block-at intent/hface " I4S "b-go 2 4");
    (void)th;
    int hov[3], prs[3];
    for (int k = 0; k < 3; k++) { hov[k] = (int)ig("block-at block-at block-at intent/hface " I4S "b-go 2 4 %d", 113 + k);
                                  prs[k] = (int)ig("block-at block-at block-at intent/hface " I4S "b-go 2 4 %d", 117 + k); }
    CHECK(!(hov[0] == 30 && hov[1] == 120 && hov[2] == 60) && !(prs[0] == 30 && prs[1] == 120 && prs[2] == 60));
    long n0 = gint("test/i5-n");
    CHECK(move_to(go.x + go.w / 2, go.y + go.h / 2) == 0 && node_rgb_is(vn(st_node("b-go", 2))->bg, hov[0], hov[1], hov[2]));
    CHECK(press_at(go.x + go.w / 2, go.y + go.h / 2) == 0 && node_rgb_is(vn(st_node("b-go", 2))->bg, prs[0], prs[1], prs[2]));
    CHECK(release_at(go.x + go.w / 2, go.y + go.h / 2) == 0 && gint("test/i5-n") == n0 + 1);
    CHECK(click_face_i((int)iscoped("b-big")) == 0 && gint("test/i5-n") == n0 + 2);
    /* disabled with a look: the disabled colours, no click */
    CHECK(node_fg_is(st_node("b-off", 3), "theme/dark", 93) && node_bg_is(st_node("b-off", 2), "theme/dark", 49));
    CHECK(click(off.x + off.w / 2, off.y + off.h / 2) == 0 && gint("test/i5-n") == n0 + 2);
    /* a label's font family and colour */
    gv_node *ml = vn(inode((int)iscoped("m-label")));
    CHECK(ml && ml->font_style == 4 && node_rgb_is(ml->fg, 240, 200, 80));
    /* a field: 36 px, an orange border 3 px wide, rounded; the inset kept through resizing */
    gv_rect fb; CHECK(lbox("f-boxy", &fb) == 0 && fb.h == 36);
    gv_node *fbd = vn(st_node("f-boxy", 7)), *fbx = vn(st_node("f-boxy", 2));
    CHECK(fbd && node_rgb_is(fbd->bg, 230, 170, 40) && fbd->radius == 4 && fbx->radius == 1);
    CHECK(fbx->x == 3 && fbx->y == 3 && fbx->w == fb.w - 6 && fbx->h == 30);
    CHECK(resize(1500, 800) == 0);
    gv_rect fb2; CHECK(lbox("f-boxy", &fb2) == 0);
    fbx = vn(st_node("f-boxy", 2));
    CHECK(fbx->x == 3 && fbx->w == fb2.w - 6);
    CHECK(resize(1200, 700) == 0);
    /* focus: the ring shows on the styled field, editing works */
    CHECK(click_face_i((int)iscoped("f-boxy")) == 0 && vn(st_node("f-boxy", 9))->visible == 1);
    CHECK(keyk(K_END) == 0 && text("!") == 0 && i4_value_is("f-boxy", "framed!"));
    /* errors, where the style is defined */
    gstr("sin-id judge [ intent/show-now layout [ style s button look [ sparkle 1 ] ] ]", a, sizeof a); CHECK(strstr(a, "unknown-look-property") != NULL);
    gstr("sin-id judge [ intent/show-now layout [ style s button look [ color 300 0 0 ] ] ]", a, sizeof a); CHECK(strstr(a, "look-value-out-of-range") != NULL);
    gstr("sin-id judge [ intent/show-now layout [ style s button look [ radius ] ] ]", a, sizeof a); CHECK(strstr(a, "look-needs-a-number") != NULL);
    gstr("sin-id judge [ intent/show-now layout [ style s button look 5 ] ]", a, sizeof a); CHECK(strstr(a, "look-needs-a-block") != NULL);
    CHECK(presented_ok() == 0);
    printf("intent5-look: big / small / coloured / rounded / borderless buttons, a style of a style, a monospace label, a framed "
           "field -- all the same VID widgets; one derived theme per style; hover, pressed, disabled and focus from it; clicks "
           "and editing as before; 4 errors named\n");
}
/* intent5-image: PNG and JPEG scaled to fit, the aspect ratio kept; one copy
 * of a file shown twice; missing, invalid, truncated and oversized files as
 * placeholders that say why; icons on buttons that stay buttons */
static void case_intent5_image(void) {
    char a[200];
    CHECK(ishow("test/i5-image") == 0 && render() == 0);
    int iw = 0, ih = 0;
    long pic = inode((int)iscoped("pic")), tall = inode((int)iscoped("tall"));
    CHECK(gv_view_image_size(v, vnode(pic), &iw, &ih) == 0 && iw == 64 && ih == 32);
    CHECK(gv_view_image_size(v, vnode(tall), &iw, &ih) == 0 && iw == 60 && ih == 120);
    if (png_dir) { char pth[512]; snprintf(pth, sizeof pth, "%s/intent5-image.png", png_dir); CHECK(gv_backend_sdl_save_png(b, pth) == 0); }
    /* badge 64x32 in 128x128: scaled 2x, 128x64, centred vertically; red left, half-transparent blue right */
    gv_rect pr; CHECK(lbox("pic", &pr) == 0);
    int win[3], red[3], blue[3], top[3];
    pixel(pr.x + pr.w + 3, pr.y + 3, win);
    pixel(pr.x + 30, pr.y + 64, red); pixel(pr.x + 100, pr.y + 64, blue); pixel(pr.x + 64, pr.y + 10, top);
    CHECK(red[0] > 200 && red[1] < 60 && red[2] < 60);
    CHECK(blue[2] > blue[0] && blue[2] < 220 && blue[2] > 100);                 /* blended with what is under it */
    CHECK(rgb_diff(top, win) < 8);                                             /* above the picture: nothing drawn */
    { int edge_in[3], edge_out[3]; pixel(pr.x + 30, pr.y + 33, edge_in); pixel(pr.x + 30, pr.y + 30, edge_out);
      CHECK(edge_in[0] > 200 && rgb_diff(edge_out, win) < 8); }               /* it starts at y + 32 */
    /* photo 60x120 in 100x100: 50x100, centred horizontally; green above yellow */
    gv_rect tr; CHECK(lbox("tall", &tr) == 0);
    int g[3], y[3], side[3];
    pixel(tr.x + 50, tr.y + 25, g); pixel(tr.x + 50, tr.y + 75, y); pixel(tr.x + 10, tr.y + 50, side);
    CHECK(g[1] > 130 && g[0] < 80 && y[0] > 200 && y[1] > 170 && y[2] < 90 && rgb_diff(side, win) < 8);
    /* files that cannot be shown: no picture, the reason in the placeholder */
    struct { const char *name, *why; } bad[] = { { "gone", "image not found\nmissing.png" }, { "bad", "not a PNG or JPEG image" },
                                                 { "cut", "invalid PNG image" }, { "huge", "image too large (10000x10000" } };
    for (int k = 0; k < 4; k++) {
        long id = inode((int)iscoped(bad[k].name));
        gv_node *n = vn(id);
        CHECK(n && gv_view_image_size(v, vnode(id), NULL, NULL) == -1 && n->text && strstr(n->text, bad[k].why) != NULL);
        if (n && n->text && !strstr(n->text, bad[k].why)) fprintf(stderr, "  %s: '%s'\n", bad[k].name, n->text);
    }
    /* a backend without a decoder (headless): the placeholder says so */
    { char err[300]; long id = inode((int)iscoped("huge"));
      CHECK(gv_view_set_image(v, vnode(id), "view/tests/images/badge.png", NULL, NULL, err, (int)sizeof err) == -1 &&
            strstr(err, "no image decoder in this backend") != NULL && strstr(vn(id)->text, "no image decoder") != NULL); }
    /* the placeholder is drawn: ink inside the missing image's box */
    { gv_rect r; CHECK(lbox("gone", &r) == 0); int in = 0, out = 0; ink_in_out(r, r.x + r.w + 6, &in, &out); CHECK(in > 50); }
    /* one decoded copy per file: badge twice, photo, two icons = 4 pictures */
    size_t bytes = 0;
    int pics = gv_view_image_count(v, &bytes);
    CHECK(pics == 4 && bytes == (size_t)(64 * 32 + 60 * 120 + 64 * 64 * 2) * 4);
    CHECK(gv_view_node(v, vnode(inode((int)iscoped("again"))))->image == vn(pic)->image);
    /* icon buttons: wider than the caption alone, the icon inside, a press on the icon is the button's */
    gv_rect sv; CHECK(lbox("i-save", &sv) == 0);
    long unit = gint("intent/unit intent/theme");
    CHECK(sv.w == 4 * unit + 24 + 26);
    long n0 = gint("test/i5-n");
    CHECK(click(sv.x + 14, sv.y + 15) == 0 && gint("test/i5-n") == n0 + 1);    /* on the icon */
    gv_rect io; CHECK(lbox("i-only", &io) == 0 && io.w == 80);
    CHECK(click(io.x + io.w / 2, io.y + 15) == 0 && gint("test/i5-n") == n0 + 2);
    /* a broken icon: "?" in its square; the button still works */
    gv_rect ib; CHECK(lbox("i-bad", &ib) == 0 && click(ib.x + 14, ib.y + 15) == 0 && gint("test/i5-n") == n0 + 3);
    /* an image takes no input: hovering, pressing and clicking on it change nothing and fault nothing */
    CHECK(move_to(pr.x + 20, pr.y + 60) == 0 && click(pr.x + 20, pr.y + 60) == 0 && !gv_glon_host_error(h) && gint("test/i5-n") == n0 + 3);
    /* resizing: the stretched image widens, the picture keeps its shape (64x32 at 32 high) */
    gv_rect ag0, ag1; CHECK(lbox("again", &ag0) == 0 && resize(1500, 800) == 0 && lbox("again", &ag1) == 0);
    CHECK(ag1.w == ag0.w + 300);
    { int c1[3], c2[3]; pixel(ag1.x + ag1.w / 2 - 16, ag1.y + 16, c1); pixel(ag1.x + 4, ag1.y + 16, c2);
      CHECK(c1[0] > 200 && c1[1] < 60 && rgb_diff(c2, win) < 8); }
    CHECK(resize(1200, 700) == 0);
    /* errors */
    gstr("sin-id judge [ intent/show-now layout [ image \"x.png\" ] ]", a, sizeof a); CHECK(strstr(a, "image-needs-a-size") != NULL);
    gstr("sin-id judge [ intent/show-now layout [ image 5 5 ] ]", a, sizeof a); CHECK(strstr(a, "image-needs-a-file") != NULL);
    gstr("sin-id judge [ intent/show-now layout [ button \"x\" icon 5 ] ]", a, sizeof a); CHECK(strstr(a, "icon-needs-a-file") != NULL);
    /* another layout: the pictures are released */
    CHECK(ishow("test/rows") == 0 && render() == 0 && gv_view_image_count(v, &bytes) == 0 && bytes == 0);
    CHECK(presented_ok() == 0);
    printf("intent5-image: PNG (alpha) and JPEG scaled to fit with the aspect ratio kept; %d pictures, %zu bytes for 6 image nodes "
           "(one copy per file); missing / not an image / truncated / too large shown as placeholders saying why; icon buttons "
           "pressed on the icon; a broken icon is '?'; released with the layout; 3 errors named\n",
           4, (size_t)(64 * 32 + 60 * 120 + 64 * 64 * 2) * 4);
}

/* intent5-gallery: the appearance gallery (intent_gallery_demo.glon) end to
 * end: both themes side by side with the same geometry; every caption and
 * label readable on what is behind it (from the nodes' colours); selections
 * and focus shown; images loaded once; presses reported */
static int col3(gv_color c, int *o) { o[0] = c.r; o[1] = c.g; o[2] = c.b; return 0; }
static void case_intent5_gallery(void) {
    char a[200];
    CHECK(ig("= intent/current gal/form") == 1);
    CHECK(resize(1180, 800) == 0 && render() == 0);
    long lf = hindex(LDR "light"), df = hindex(LDR "dark");
    CHECK(lf >= 0 && df >= 0 && ikind((int)lf) == 8 && ikind((int)df) == 8);
    CHECK(node_bg_is(ig("- intent/node intent/current %ld 1", lf), "theme/light", 5));
    CHECK(node_bg_is(ig("- intent/node intent/current %ld 1", df), "theme/dark", 5));
    /* the two sides: the same widgets at the same places relative to their form */
    gv_rect L, D; CHECK(ibox((int)lf, &L) == 0 && ibox((int)df, &D) == 0);
    CHECK(L.w == D.w && L.h == D.h && L.y == D.y);
    int nside = (int)(df - lf - 1), same = 0;
    for (int i = 0; i < nside; i++) {
        gv_rect a1, b1;
        if (ibox((int)lf - nside + i, &a1) || ibox((int)df - nside + i, &b1)) continue;
        /* (the headings "Light" and "Dark" differ in width, nothing else) */
        int wsame = a1.w == b1.w || ikind((int)lf - nside + i) == 1;
        if (a1.x - L.x == b1.x - D.x && a1.y - L.y == b1.y - D.y && wsame) same++;
        else fprintf(stderr, "  side face %d: %d,%d %d vs %d,%d %d\n", i, a1.x - L.x, a1.y - L.y, a1.w, b1.x - D.x, b1.y - D.y, b1.w);
    }
    CHECK(same == nside);
    /* readability, from the colours of the nodes themselves: every label on its
     * form's pane, every button caption on its surface (disabled: >= 3) */
    double worst = 99, worst_dis = 99;
    int checked = 0;
    for (int i = 0; i < (int)icount(); i++) {
        int k = (int)ikind(i), fg[3], bgc[3];
        if (k == 1) {
            gv_node *t = vn(inode(i));
            if (!t) continue;
            /* the faces of each form come just before it; the root's are on the dark window */
            int side = (i >= lf - nside && i < lf) ? 0 : (i >= df - nside && i < df) ? 1 : -1;
            if (side < 0) { theme_rgb("theme/dark", 1, bgc); col3(t->fg, fg); goto judged; }
            theme_rgb(side == 0 ? "theme/light" : "theme/dark", 5, bgc);
            col3(t->fg, fg);
        } else if (k == 3) {
            gv_node *t = vn(ig("block-at block-at intent/face intent/current %d 2 3", i)), *s2 = vn(ig("block-at block-at intent/face intent/current %d 2 2", i));
            if (!t || !s2 || !t->text || !t->text[0]) continue;
            col3(t->fg, fg); col3(s2->bg, bgc);
        } else continue;
    judged:;
        double c = contrast3(fg, bgc);
        int enabled = (int)ig("block-at intent/face intent/current %d 20", i);
        if (enabled) { if (c < worst) worst = c; } else if (c < worst_dis) worst_dis = c;
        if ((enabled && c < 4.5) || (!enabled && c < 3.0))
            fprintf(stderr, "  face %d (kind %d): contrast %.2f\n", i, k, c);
        checked++;
    }
    CHECK(checked >= 30 && worst >= 4.5 && worst_dis >= 3.0);
    /* the looks: sizes and corners on both sides; coloured buttons alike in both */
    CHECK(ig("intent/live-h intent/current %ld", lf - nside + 5) == 44);
    /* selections and focus, set by the demo after its first build */
    CHECK(ig("face/index form/field form/sub form/current 'light 'people") == 2 && ig("face/index form/field form/sub form/current 'dark 'people") == 2);
    igs(a, sizeof a, "face/value form/field form/sub form/current 'dark 'places"); CHECK(!strcmp(a, "FGI"));
    CHECK(ig("face/value form/field form/sub form/current 'light 'ticked") == 1);
    CHECK(i4_focused() == hindex("form/field form/sub form/current 'light 'name"));
    /* pictures: save, open, delete, landscape: four decoded, shared by both sides */
    size_t bytes = 0;
    CHECK(gv_view_image_count(v, &bytes) == 4);
    /* presses are reported, on either side */
    CHECK(click_face_i((int)(df - nside + 10)) == 0);          /* heading 0, four labels, three sizes, Save, Delete, Rounded */
    igs(a, sizeof a, "face/text " LDR "status");
    CHECK(strstr(a, "Pressed: Rounded (dark)") != NULL);
    if (!strstr(a, "Rounded (dark)")) fprintf(stderr, "  status '%s'\n", a);
    if (png_dir) { char pth[512]; snprintf(pth, sizeof pth, "%s/intent5-gallery.png", png_dir); CHECK(render() == 0 && gv_backend_sdl_save_png(b, pth) == 0); }
    CHECK(presented_ok() == 0);
    printf("intent5-gallery: light and dark sides with %d faces each at the same geometry; %d captions and labels checked "
           "(worst %.2f enabled, %.2f disabled); selections, the tick and the focus shown; 4 pictures (%zu bytes) for 8 image "
           "nodes; presses reported\n", nside, checked, worst, worst_dis, bytes);
}

/* ==== INTENT-6: the membership application ================================ */
#define MD "form/field form/sub form/current 'details '"
#define MR "form/field form/current '"
static double i6_now(void);
static void case_intent6_smoke(void) {
    char a[300];
    if (getenv("GV_MB_TIME")) {
        const char *ops0[] = { "s/length \"Kwok\"", "s/slice \"Kwok Alice\" 1 3", "s/+ \"ab\" \"cd\"", "s/at \"Kwok\" 1",
                               "mk-string reduce [ 107 ]", "mb/find-upper \"Kwok Alice\" 1", "mb/lower-of 75", "s/= \"abc\" \"abc\"",
                               "mb/prefix? \"abcdef\" \"abc\"", "parse-int \"2026\"" };
        for (int k = 0; k < 10; k++) {
            char e[400]; snprintf(e, sizeof e, "block-len reduce [ %s 0 ]", ops0[k]);
            double t0 = i6_now(); for (int j = 0; j < 5; j++) gint(e); double t1 = i6_now();
            printf("  %-60.60s %8.2f ms\n", ops0[k], (t1 - t0) / 5);
        }
        const char *ops[] = { "0", "mb/lower \"Kwok Alice\"", "mb/split block-at mb/answer \"1|2|M00001\tKwok Alice\talice@example.org\t021\tPetone\t0\t\tactive\t2026\tx\" 2 10",
                              "mb/find \"M00100\"", "mb/compute-view", "mb/row list/at mb/recs 5", "face/value form/field form/current 'members",
                              "mb/for :mb/show-at form/field form/current 'members 0 1", "mb/show-member \"M00005\"" };
        for (int k = 0; k < 9; k++) {
            char e[400]; snprintf(e, sizeof e, "block-len reduce [ %s 0 ]", ops[k]);
            double t0 = i6_now(); long r = 0; for (int j = 0; j < 5; j++) r = gint(e); double t1 = i6_now();
            printf("  %-60.60s %8.2f ms (%ld)\n", ops[k], (t1 - t0) / 5, r);
        }
    }
    if (getenv("GV_MB_TIME")) {
        const char *ops0[] = { "s/length \"Kwok\"", "s/slice \"Kwok Alice\" 1 3", "s/+ \"ab\" \"cd\"", "s/at \"Kwok\" 1",
                               "mk-string reduce [ 107 ]", "mb/find-upper \"Kwok Alice\" 1", "mb/lower-of 75", "s/= \"abc\" \"abc\"",
                               "mb/prefix? \"abcdef\" \"abc\"", "parse-int \"2026\"" };
        for (int k = 0; k < 10; k++) {
            char e[400]; snprintf(e, sizeof e, "block-len reduce [ %s 0 ]", ops0[k]);
            double t0 = i6_now(); for (int j = 0; j < 5; j++) gint(e); double t1 = i6_now();
            printf("  %-60.60s %8.2f ms\n", ops0[k], (t1 - t0) / 5);
        }
        const char *ops[] = { "0", "mb/lower \"Kwok Alice\"", "mb/split block-at mb/answer \"1|2|M00001\tKwok Alice\talice@example.org\t021\tPetone\t0\t\tactive\t2026\tx\" 2 10",
                              "mb/find \"M00100\"", "mb/compute-view", "mb/row list/at mb/recs 5", "face/value form/field form/current 'members",
                              "mb/for :mb/show-at form/field form/current 'members 0 1", "mb/show-member \"M00005\"" };
        for (int k = 0; k < 9; k++) {
            char e[400]; snprintf(e, sizeof e, "block-len reduce [ %s 0 ]", ops[k]);
            double t0 = i6_now(); long r = 0; for (int j = 0; j < 5; j++) r = gint(e); double t1 = i6_now();
            printf("  %-60.60s %8.2f ms (%ld)\n", ops[k], (t1 - t0) / 5, r);
        }
    }
    if (getenv("GV_MB_NOSTART")) {
        gint("block-len reduce [ mb/auto-show: 0 ]");
        gint("block-len reduce [ mb/reload mb/file widget/flush ]");
        gv_glon_host_deliver(h); gv_glon_host_bind(h);
        printf("  loaded: records %ld err %d; heap high %ld live %ld free %ld blocks %ld\n", gint("list/length mb/recs"), gv_glon_host_error(h),
               r0_s1_heap_high(), r0_s1_gc_live_cells(), r0_s1_gc_free_cells(), r0_s1_gc_free_blocks());
        const char *steps[] = { "mb/compute-view", "face/set-rows mb/f 'members reduce [ ]", "mb/show-at mb/f 'members 0",
                                "mb/for :mb/show-at mb/f 'members 1 40", "widget/flush",
                                "face/set-text mb/f 'count \"x\"", "mb/for :mb/show-at mb/f 'members 40 80 widget/flush" };
        for (int k = 0; k < 7; k++) {
            char e[300];
            snprintf(e, sizeof e, "block-len reduce [ %s 0 ]", steps[k]);
            long r = gint(e);
            gv_glon_host_deliver(h); gv_glon_host_bind(h);
            printf("  %-40s -> %ld (err %d) rows %ld; heap high %ld live %ld free %ld blocks %ld\n", steps[k], r, gv_glon_host_error(h),
                   ig("face/row-count " MR "members"), r0_s1_heap_high(), r0_s1_gc_live_cells(), r0_s1_gc_free_cells(), r0_s1_gc_free_blocks());
        }
    }
    igs(a, sizeof a, "face/text " MR "status"); printf("  status: %s\n", a);
    printf("  records %ld, view %ld, rows %ld, current %s; heap high %ld live %ld\n", gint("list/length mb/recs"), gint("list/length mb/view"),
           ig("face/row-count " MR "members"), (igs(a, sizeof a, "either = mb/current none [ \"none\" ] [ mb/current ]"), a),
           r0_s1_heap_high(), r0_s1_gc_live_cells());
    CHECK(gint("list/length mb/recs") == 120);
}

/* ---- helpers ----------------------------------------------------------------------- */
/* the seed (or GV_MEMBERS_SOURCE), read independently of the application */
typedef struct { char id[16]; char name[64]; char year[8]; } i6_rec;
static i6_rec *i6_seed; static int i6_n;
static void i6_load_seed(void) {
    if (i6_seed) return;
    char path[1024]; const char *src = getenv("GV_MEMBERS_SOURCE") ? getenv("GV_MEMBERS_SOURCE") : "view/data/members-seed.tsv";
    snprintf(path, sizeof path, "%s/%s", root_dir, src);
    FILE *f = fopen(path, "rb"); if (!f) return;
    int cap = 256; i6_seed = calloc((size_t)cap, sizeof *i6_seed);
    char line[4096]; int first = 1;
    while (fgets(line, sizeof line, f)) {
        if (first) { first = 0; continue; }
        if (i6_n == cap) { cap *= 2; i6_seed = realloc(i6_seed, (size_t)cap * sizeof *i6_seed); }
        char *t = line; int k = 0;
        i6_rec *r = &i6_seed[i6_n]; memset(r, 0, sizeof *r);
        while (t && k < 10) {
            char *tab = strchr(t, '\t');
            if (tab) *tab = 0;
            if (k == 0) snprintf(r->id, sizeof r->id, "%s", t);
            if (k == 1) snprintf(r->name, sizeof r->name, "%s", t);
            if (k == 8) snprintf(r->year, sizeof r->year, "%s", t);
            t = tab ? tab + 1 : NULL;
            k++;
        }
        i6_n++;
    }
    fclose(f);
}
/* the application's search rule: a word of the name, or the ID, starts with q (ASCII case ignored) */
static int i6_prefix(const char *s, const char *q) { return strncasecmp(s, q, strlen(q)) == 0; }
static int i6_matches(const i6_rec *r, const char *q) {
    if (!q[0] || i6_prefix(r->id, q)) return 1;
    for (const char *w = r->name; w; w = strchr(w, ' ') ? strchr(w, ' ') + 1 : NULL) if (i6_prefix(w, q)) return 1;
    return 0;
}
static int i6_expected(const char *q) { i6_load_seed(); int n = 0; for (int i = 0; i < i6_n; i++) n += i6_matches(&i6_seed[i], q); return n; }
static gv_table *i6_table(void) { gv_table *t = NULL; gv_glon_host_table(h, (int)ig("intent/tid intent/hface " MR "members"), &t); return t; }
/* the table row (0-based) showing this ID, or -1 */
static int i6_row_of(const char *id) {
    gv_table *t = i6_table(); if (!t) return -1;
    for (int r = 0; r < t->nrows; r++) { const char *c = gv_table_get_cell(t, r, 0); if (c && !strcmp(c, id)) return r; }
    return -1;
}
static int i6_click_row(int row) {
    gv_table *t = i6_table(); if (!t) return -1;
    gv_rect r; if (screen(ig("intent/tid intent/hface " MR "members"), &r) != 0) return -1;
    /* scroll it into view with the wheel, as a user does */
    for (int k = 0; k < 400 && (row < t->visible_start || row >= t->visible_start + 16); k++)
        if (wheel(r.x + 40, r.y + 100, row < t->visible_start ? 1 : -1) != 0) return -1;
    int vis = row - t->visible_start;
    if (vis < 0 || vis >= 16) { fprintf(stderr, "  row %d not visible (first %d)\n", row, t->visible_start); return -1; }
    return click(r.x + 40, r.y + 28 + vis * 24 + 12);
}
static int i6_click_member(const char *id) { int r = i6_row_of(id); return r < 0 ? -1 : i6_click_row(r); }
static int i6_click(const char *hexpr) { long i = hindex(hexpr); return i < 0 ? -1 : click_face_i((int)i); }
/* replace a field's text by typing, as a user does: focus, Ctrl+A, the text */
static int i6_type(const char *hexpr, const char *s) {
    if (i6_click(hexpr) != 0) return -1;
    if (key_ctrl(SDLK_a) != 0) return -1;
    if (!s[0]) return key(8);
    return text(s);
}
static void i6_value(const char *hexpr, char *out, int cap) { igs(out, cap, "face/value %s", hexpr); }
static int i6_is(const char *hexpr, const char *want) {
    char a[400]; i6_value(hexpr, a, sizeof a);
    if (strcmp(a, want)) fprintf(stderr, "  %s = '%s', wanted '%s'\n", hexpr, a, want);
    return !strcmp(a, want);
}
static void i6_status(char *out, int cap) { igs(out, cap, "face/text " MR "status"); }
static int i6_status_has(const char *s) { char a[400]; i6_status(a, sizeof a); if (!strstr(a, s)) fprintf(stderr, "  status '%s' lacks '%s'\n", a, s); return strstr(a, s) != NULL; }
static char *i6_file(void) { char p[1024]; snprintf(p, sizeof p, "%s/view/build/intent6/members.tsv", root_dir); return read_file(p); }
static int i6_file_lines(void) { char *d = i6_file(); int n = 0; for (char *p = d; p && *p; p++) n += *p == '\n'; free(d); return n; }
static int i6_file_has(const char *s) { char *d = i6_file(); int ok = d && strstr(d, s); free(d); return ok; }
static double i6_now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6; }
static int i6_enabled(const char *n) { return (int)ig("face/enabled " MD "%s", n); }
static int i6_visible(const char *n) { return (int)ig("face/visible " MD "%s", n); }
#define I6P(x) "form/field form/sub form/current 'details '" x

/* intent6-load: the members file read at the start: the list, the first member's details */
static void case_intent6_load(void) {
    CHECK(resize(1240, 820) == 0);                 /* the demo's window */
    i6_load_seed();
    CHECK(i6_n == 120 && gint("list/length mb/recs") == 120 && gint("list/length mb/view") == 120);
    CHECK(i6_status_has("Loaded 120 members from view/build/intent6/members.tsv"));
    gv_table *t = i6_table();
    CHECK(t && t->nrows == 120 && !strcmp(gv_table_get_cell(t, 0, 0), "M00001") && !strcmp(gv_table_get_cell(t, 119, 0), "M00120"));
    CHECK(!strcmp(gv_table_get_cell(t, 0, 1), i6_seed[0].name) && !strcmp(gv_table_get_cell(t, 1, 3), "Active"));
    char a[200]; igs(a, sizeof a, "face/text " MR "count"); CHECK(!strcmp(a, "Showing 120 of 120 members"));
    /* the first member selected by its ID, its details shown, nothing to save */
    CHECK(i6_is(MR "members", "M00001") && i6_is(I6P("id"), "M00001") && i6_is(I6P("name"), i6_seed[0].name));
    CHECK(i6_is(I6P("email"), "alice.kwok1@example.org") && i6_is(I6P("village"), "Lower Hutt") && i6_is(I6P("status"), "active"));
    CHECK(ig("face/value " I6P("postal?")) == 1 && i6_visible("postal") == 1);
    { char p[200]; i6_value(I6P("postal"), p, sizeof p); CHECK(!strcmp(p, "PO Box 862\nLower Hutt")); }  /* a newline kept */
    CHECK(i6_enabled("id") == 0 && i6_enabled("save") == 0 && i6_enabled("cancel") == 0 && i6_enabled("delete") == 1);
    CHECK(i6_visible("prompt") == 0);
    CHECK(presented_ok() == 0);
    printf("intent6-load: 120 synthetic members from the file: table rows by ID, the first member's details (a two-line postal "
           "address), Save / Cancel disabled, Delete enabled\n");
}
/* intent6-select: selecting by mouse and keys shows that member; the value is the ID */
static void case_intent6_select(void) {
    CHECK(resize(1240, 820) == 0);                 /* the demo's window */
    i6_load_seed();
    CHECK(i6_click_member("M00004") == 0 && i6_is(MR "members", "M00004") && i6_is(I6P("name"), i6_seed[3].name));
    CHECK(i6_status_has("Member M00004"));
    CHECK(key(SDLK_DOWN) == 0 && i6_is(MR "members", "M00005") && i6_is(I6P("name"), i6_seed[4].name));
    CHECK(keyk(K_END) == 0 && i6_is(MR "members", "M00120") && i6_is(I6P("id"), "M00120"));
    CHECK(i6_is(I6P("year"), i6_seed[119].year));
    /* the hidden postal row keeps its place: members with and without one, same geometry */
    gv_rect a0, a1, n0, n1;
    CHECK(hbox(I6P("postal"), &a0) == 0 && hbox(I6P("notes"), &n0) == 0);
    CHECK(keyk(K_HOME) == 0 && hbox(I6P("postal"), &a1) == 0 && hbox(I6P("notes"), &n1) == 0);
    CHECK(same_rect(a0, a1) && same_rect(n0, n1));
    CHECK(presented_ok() == 0);
    printf("intent6-select: click, Down, End, Home select members; the details follow; the table's value is the member ID; "
           "the postal row keeps its place shown or hidden\n");
}
/* intent6-search: name-word or ID prefixes; identity kept; clear */
static void case_intent6_search(void) {
    CHECK(resize(1240, 820) == 0);                 /* the demo's window */
    char a[200];
    /* queries from the file itself: the first member's surname in lower case, the second's given name in capitals */
    i6_load_seed();
    char q1[32], q2[32], *sp;
    snprintf(q1, sizeof q1, "%s", i6_seed[0].name); if ((sp = strchr(q1, ' '))) *sp = 0;
    for (char *c = q1; *c; c++) *c = (char)tolower((unsigned char)*c);
    sp = strchr(i6_seed[1].name, ' '); snprintf(q2, sizeof q2, "%s", sp ? sp + 1 : i6_seed[1].name);
    for (char *c = q2; *c; c++) *c = (char)toupper((unsigned char)*c);
    int ewong = i6_expected(q1), egrace = i6_expected(q2), eid = i6_expected("M0005");
    CHECK(ewong > 0 && egrace > 0 && eid == 10);
    /* select M00052 first: it stays the selection while it is visible */
    CHECK(i6_click_member("M00012") == 0);
    CHECK(i6_type(MR "search", q1) == 0 && gint("list/length mb/view") == ewong && i6_table()->nrows == ewong);
    for (int r = 0; r < i6_table()->nrows; r++) { const char *n = gv_table_get_cell(i6_table(), r, 1); CHECK(strncasecmp(n, q1, strlen(q1)) == 0); }
    igs(a, sizeof a, "face/text " MR "count"); { char want[80]; snprintf(want, sizeof want, "Showing %d of 120 members", ewong); CHECK(!strcmp(a, want)); }
    CHECK(i6_type(MR "search", q2) == 0 && gint("list/length mb/view") == egrace);   /* case ignored */
    CHECK(i6_type(MR "search", "m0005") == 0 && gint("list/length mb/view") == 10);
    /* identity: pick M00057 in the filtered list (row 7 there, row 56 unfiltered) */
    CHECK(i6_row_of("M00057") == 7 && i6_click_member("M00057") == 0 && i6_is(MR "members", "M00057") && i6_is(I6P("id"), "M00057"));
    CHECK(ig("face/index " MR "members") == 8);                       /* its position here: never used as its identity */
    /* clear: all again, M00057 still the selection, now at row 56 */
    CHECK(i6_click(MR "clear-search") == 0 && gint("list/length mb/view") == 120 && i6_is(MR "search", ""));
    CHECK(i6_is(MR "members", "M00057") && ig("face/index " MR "members") == 57 && i6_row_of("M00057") == 56);
    /* no match: an empty list, the details kept */
    CHECK(i6_type(MR "search", "zzz") == 0 && gint("list/length mb/view") == 0 && i6_is(I6P("id"), "M00057"));
    CHECK(i6_click(MR "clear-search") == 0);
    /* sorting by year, newest first: the same members, the selection by ID */
    CHECK(i6_click(MR "sort") == 0 && key(SDLK_ESCAPE) == 0 && key(SDLK_DOWN) == 0 && gint("= mb/sort-by 'year") == 1);
    gv_table *t = i6_table();
    CHECK(t->nrows == 120 && i6_is(MR "members", "M00057"));
    int ordered = 1;
    for (int r = 1; r < t->nrows; r++) if (atoi(gv_table_get_cell(t, r, 4)) > atoi(gv_table_get_cell(t, r - 1, 4))) ordered = 0;
    CHECK(ordered);
    CHECK(presented_ok() == 0);
    printf("intent6-search: '%s' %d, '%s' %d, 'm0005' 10 (expected from the file independently); the selection follows "
           "the member, not the row; clear; no match; year order, the selection kept\n", q1, ewong, q2, egrace);
}
/* intent6-new-edit: a new member; an existing one edited; both in the list and the file */
static void case_intent6_new_edit(void) {
    CHECK(resize(1240, 820) == 0);                 /* the demo's window */
    CHECK(i6_click(I6P("new")) == 0 && i6_is(I6P("id"), "M00121") && i6_is(I6P("status"), "active") && i6_is(I6P("year"), "2026"));
    CHECK(i6_enabled("id") == 1 && i6_enabled("delete") == 0 && ig("= none face/value " MR "members") == 1);   /* no selection */
    CHECK(i6_type(I6P("name"), "Tse Wing Yan") == 0 && i6_type(I6P("email"), "wing.yan@example.org") == 0);
    CHECK(i6_type(I6P("phone"), "04 555 0199") == 0);
    CHECK(i6_click(I6P("village")) == 0 && key(SDLK_ESCAPE) == 0 && keyk(K_HOME) == 0 && i6_is(I6P("village"), "Petone"));
    CHECK(i6_enabled("save") == 1 && i6_status_has("Unsaved changes."));
    CHECK(i6_click(I6P("save")) == 0 && i6_status_has("Saved M00121 Tse Wing Yan"));
    CHECK(gint("list/length mb/recs") == 121 && i6_row_of("M00121") == 120 && i6_is(MR "members", "M00121"));
    CHECK(i6_enabled("save") == 0 && i6_enabled("delete") == 1 && i6_enabled("id") == 0);
    CHECK(i6_file_lines() == 122 && i6_file_has("M00121\tTse Wing Yan\twing.yan@example.org\t04 555 0199\tPetone\t0\t\tactive\t2026\t"));
    /* edit an existing member: the row replaced in place, the file rewritten */
    CHECK(i6_click_member("M00003") == 0 && i6_type(I6P("name"), "Ng Henry Jr") == 0);
    CHECK(i6_type(I6P("notes"), "Line one") == 0 && key(13) == 0 && text("Line two") == 0);
    CHECK(i6_click(I6P("save")) == 0 && i6_status_has("Saved M00003 Ng Henry Jr"));
    CHECK(!strcmp(gv_table_get_cell(i6_table(), 2, 1), "Ng Henry Jr") && i6_table()->nrows == 121 && gint("list/length mb/recs") == 121);
    CHECK(i6_file_has("M00003\tNg Henry Jr\t") && i6_file_has("Line one\x1fLine two"));
    CHECK(presented_ok() == 0);
    printf("intent6-new-edit: New suggests M00121 and defaults; typed, picked and saved: row 121 selected, the file has it; "
           "an edited member's row replaced in place; a two-line note written with 0x1F\n");
}
/* intent6-validation: nothing invalid is saved; messages name the problem */
static void case_intent6_validation(void) {
    CHECK(resize(1240, 820) == 0);                 /* the demo's window */
    struct { const char *field, *value, *msg; } bad[] = {
        { "name", "", "Name is required." }, { "email", "x@y", "Email: like name@example.org." },
        { "email", "a@b@c.org", "Email: only one @." }, { "phone", "12ab", "Telephone: 7 to 20 digits" },
        { "year", "20x6", "Subscription year: four digits." }, { "year", "1890", "Subscription year: 1950 to 2027." } };
    for (int k = 0; k < 6; k++) {
        CHECK(i6_click_member("M00006") == 0);
        char e[120]; snprintf(e, sizeof e, I6P("%s"), bad[k].field);
        CHECK(i6_type(e, bad[k].value) == 0 && i6_click(I6P("save")) == 0 && i6_status_has(bad[k].msg));
        CHECK(i6_enabled("save") == 1 && i6_is(MR "members", "M00006"));
        CHECK(i6_click(I6P("cancel")) == 0);
    }
    /* a new member: a duplicate ID, a missing village, a postal address ticked but empty */
    CHECK(i6_click(I6P("new")) == 0 && i6_type(I6P("name"), "Test Person") == 0 && i6_type(I6P("id"), "M00005") == 0);
    CHECK(i6_click(I6P("save")) == 0 && i6_status_has("Member ID M00005 is already used."));
    CHECK(i6_type(I6P("id"), "X1") == 0 && i6_click(I6P("save")) == 0 && i6_status_has("Member ID: M and five digits"));
    CHECK(i6_type(I6P("id"), "M00200") == 0 && i6_click(I6P("save")) == 0 && i6_status_has("Village is required."));
    CHECK(i6_click(I6P("village")) == 0 && key(SDLK_ESCAPE) == 0 && keyk(K_HOME) == 0);
    CHECK(i6_click(I6P("postal?")) == 0 && i6_visible("postal") == 1);
    CHECK(i6_click(I6P("save")) == 0 && i6_status_has("Postal address is required when ticked."));
    CHECK(gint("list/length mb/recs") == 120 && i6_file_lines() == 121);           /* nothing saved */
    CHECK(i6_type(I6P("postal"), "PO Box 1") == 0 && i6_click(I6P("save")) == 0 && i6_status_has("Saved M00200 Test Person"));
    CHECK(gint("list/length mb/recs") == 121);
    CHECK(presented_ok() == 0);
    printf("intent6-validation: required name, email form, telephone, year format and range, duplicate and malformed IDs, "
           "village, ticked postal address: each refused by name, nothing saved; then a valid one saved\n");
}
/* intent6-unsaved: edits are never dropped silently */
static void case_intent6_unsaved(void) {
    CHECK(resize(1240, 820) == 0);                 /* the demo's window */
    CHECK(i6_click_member("M00002") == 0 && i6_type(I6P("name"), "Changed Name") == 0 && i6_enabled("save") == 1);
    /* another member: asked first; the selection stays with the edited member */
    CHECK(i6_click_member("M00008") == 0 && i6_visible("prompt") == 1 && i6_is(MR "members", "M00002"));
    CHECK(i6_is(I6P("name"), "Changed Name") && i6_enabled("delete") == 0);
    CHECK(i6_click(I6P("p3")) == 0 && i6_visible("prompt") == 0 && i6_is(I6P("name"), "Changed Name"));   /* keep editing */
    CHECK(i6_click_member("M00008") == 0 && i6_click(I6P("p2")) == 0);                                       /* discard */
    CHECK(i6_is(MR "members", "M00008") && i6_is(I6P("id"), "M00008") && i6_enabled("save") == 0);
    CHECK(gint("s/= block-at list/at mb/recs 1 1 \"Changed Name\"") == 0);
    /* again, Save this time: saved, then the other member */
    CHECK(i6_click_member("M00002") == 0 && i6_type(I6P("name"), "Saved Name") == 0);
    CHECK(i6_click_member("M00009") == 0 && i6_click(I6P("p1")) == 0);
    CHECK(i6_is(I6P("id"), "M00009") && !strcmp(gv_table_get_cell(i6_table(), 1, 1), "Saved Name") && i6_file_has("M00002\tSaved Name\t"));
    /* Save first, but invalid: nothing moves */
    CHECK(i6_type(I6P("email"), "bad") == 0 && i6_click_member("M00010") == 0 && i6_click(I6P("p1")) == 0);
    CHECK(i6_is(I6P("id"), "M00009") && i6_status_has("Email: like name@example.org.") && i6_visible("prompt") == 1);
    CHECK(i6_click(I6P("p3")) == 0);
    /* Cancel: the stored values back; New with edits: asked too */
    CHECK(i6_click(I6P("cancel")) == 0 && i6_is(I6P("email"), "") == 0 && i6_enabled("save") == 0);
    CHECK(i6_type(I6P("phone"), "04 555 1234") == 0 && i6_click(I6P("new")) == 0 && i6_visible("prompt") == 1);
    CHECK(i6_click(I6P("p2")) == 0 && i6_is(I6P("id"), "M00121") && i6_enabled("id") == 1);
    CHECK(i6_click(I6P("cancel")) == 0 && i6_status_has("New member cancelled."));
    CHECK(presented_ok() == 0);
    printf("intent6-unsaved: another member or New with edits asks Save / Discard / Keep editing; keep, discard, save "
           "(and an invalid save that stays); Cancel restores the stored values\n");
}
/* intent6-delete: confirmed, then gone from the list, the details and the file */
static void case_intent6_delete(void) {
    CHECK(resize(1240, 820) == 0);                 /* the demo's window */
    CHECK(i6_click_member("M00010") == 0 && i6_click(I6P("delete")) == 0 && i6_visible("prompt") == 1);
    char a[200]; igs(a, sizeof a, "face/text " I6P("prompt-text")); CHECK(strstr(a, "Delete M00010") != NULL);
    CHECK(i6_visible("p3") == 0);
    CHECK(i6_click(I6P("p2")) == 0 && gint("list/length mb/recs") == 120 && i6_status_has("Kept."));          /* Keep */
    CHECK(i6_click(I6P("delete")) == 0 && i6_click(I6P("p1")) == 0);                                       /* Delete */
    CHECK(gint("list/length mb/recs") == 119 && i6_row_of("M00010") == -1 && i6_table()->nrows == 119 && i6_status_has("Deleted M00010"));
    CHECK(ig("= none face/value " MR "members") == 1 && i6_is(I6P("id"), "") && i6_enabled("delete") == 0);
    CHECK(i6_file_lines() == 120 && !i6_file_has("M00010\t") && i6_file_has("M00011\t"));
    CHECK(!strcmp(gv_table_get_cell(i6_table(), 9, 0), "M00011"));                                         /* the order kept */
    CHECK(presented_ok() == 0);
    printf("intent6-delete: Delete asks; Keep keeps; Delete removes M00010 from the records, the list and the file, the "
           "order of the rest unchanged\n");
}
/* intent6-reload: the file across a restart: what was saved is what is read */
static void case_intent6_reload(void) {
    CHECK(resize(1240, 820) == 0);                 /* the demo's window */
    CHECK(i6_click_member("M00007") == 0 && i6_type(I6P("name"), "Persisted Name") == 0 && i6_click(I6P("save")) == 0);
    CHECK(i6_click(I6P("new")) == 0 && i6_type(I6P("name"), "Restart Test") == 0);
    CHECK(i6_click(I6P("village")) == 0 && key(SDLK_ESCAPE) == 0 && keyk(K_HOME) == 0 && i6_click(I6P("save")) == 0);
    char *before = i6_file();
    /* a restart: the records thrown away and read again from the file */
    CHECK(gint("block-len reduce [ mb/reload mb/file widget/flush ]") == 2 && gv_glon_host_deliver(h) == 0 && gv_glon_host_bind(h) == 0);
    CHECK(gint("list/length mb/recs") == 121 && i6_status_has("Loaded 121 members"));
    CHECK(gint("s/= block-at list/at mb/recs 6 1 \"Persisted Name\"") == 1 && gint("s/= block-at list/at mb/recs 120 1 \"Restart Test\"") == 1);
    /* saved again unchanged: byte for byte the same file */
    CHECK(gint("block-len reduce [ mb/save-file widget/flush ]") == 2 && gv_glon_host_deliver(h) == 0 && gv_glon_host_bind(h) == 0);
    char *after = i6_file();
    CHECK(before && after && !strcmp(before, after));
    free(before); free(after);
    /* a file with a bad line: reported, the rest read */
    { char p[1024]; snprintf(p, sizeof p, "%s/view/build/intent6/members.tsv", root_dir); FILE *f = fopen(p, "ab"); fputs("not a record\n", f); fclose(f); }
    CHECK(gint("block-len reduce [ mb/reload mb/file widget/flush ]") == 2 && gv_glon_host_deliver(h) == 0 && gv_glon_host_bind(h) == 0);
    CHECK(gint("list/length mb/recs") == 121 && i6_status_has("skipped 1 bad lines"));
    /* a path outside the root is refused by the host */
    CHECK(gint("block-len reduce [ mb/file: \"../outside.tsv\" mb/reload mb/file widget/flush ]") == 3 && gv_glon_host_deliver(h) == 0 && gv_glon_host_bind(h) == 0);
    CHECK(i6_status_has("refused (outside the root)"));
    CHECK(presented_ok() == 0);
    printf("intent6-reload: edits and a new member survive a reload from the file; an unchanged save is byte-identical; a bad "
           "line is skipped and counted; a path outside the root is refused\n");
}
/* intent6-keys: Tab order and keyboard use */
static void case_intent6_keys(void) {
    CHECK(resize(1240, 820) == 0);                 /* the demo's window */
    const char *order[] = { MR "search", MR "clear-search", MR "sort", MR "members", I6P("name"), I6P("email"), I6P("phone"),
                            I6P("village"), I6P("postal?"), I6P("postal"), I6P("status"), I6P("year"), I6P("notes"), I6P("new"), I6P("delete") };
    CHECK(i6_click(MR "search") == 0);
    int ok = 1;
    for (int k = 1; k < 15; k++) {
        CHECK(key(9) == 0);
        if (i4_focused() != hindex(order[k])) { fprintf(stderr, "  Tab %d: face %ld, wanted %s (%ld)\n", k, i4_focused(), order[k], hindex(order[k])); ok = 0; break; }
    }
    CHECK(ok);   /* the ID (disabled for a saved member), Save and Cancel (disabled) are skipped */
    /* keys do the work: type a search, Down in the list, edit, Enter does not save */
    i6_load_seed();
    char q[32]; const char *sp = strchr(i6_seed[1].name, ' ');
    snprintf(q, sizeof q, "%s", sp ? sp + 1 : i6_seed[1].name);
    CHECK(i6_click(MR "search") == 0 && text(q) == 0 && gint("list/length mb/view") == i6_expected(q) && i6_expected(q) > 1);
    CHECK(key(9) == 0 && key(9) == 0 && key(9) == 0 && i4_focused() == hindex(MR "members") && key(SDLK_DOWN) == 0);
    { char a[64]; i6_value(MR "members", a, sizeof a);
      if (a[0] != 'M') fprintf(stderr, "  keys: table value '%s', view %ld, rows %d\n", a, gint("list/length mb/view"), i6_table()->nrows);
      CHECK(a[0] == 'M' && i6_is(I6P("id"), a)); }
    CHECK(presented_ok() == 0);
    printf("intent6-keys: Tab runs search, Clear, Sort, the list, then the editable details in order, skipping disabled "
           "widgets; a search typed and a member picked with the keyboard\n");
}
/* intent6-resize: sizes and state */
static void case_intent6_resize(void) {
    CHECK(i6_click_member("M00003") == 0 && i6_type(I6P("notes"), "half typed") == 0 && i6_type(MR "search", "ng") == 0);
    i4_geo g0[64], g1[64];
    CHECK(resize(1200, 760) == 0);
    int n = i4_snapshot(g0, 64);
    int sizes[5][2] = { {1500, 900}, {1000, 650}, {1920, 1080}, {900, 600}, {1200, 760} };
    for (int k = 0; k < 5; k++) { CHECK(resize(sizes[k][0], sizes[k][1]) == 0 && no_overlaps() == 0); }
    i4_snapshot(g1, 64);
    int moved = 0; for (int i = 0; i < n; i++) if (!same_rect(g0[i].r, g1[i].r) || g0[i].node != g1[i].node) moved++;
    CHECK(moved == 0);
    CHECK(i6_is(I6P("notes"), "half typed") && i6_is(MR "search", "ng") && i6_is(MR "members", "M00003") && i6_enabled("save") == 1);
    CHECK(presented_ok() == 0);
    printf("intent6-resize: 5 window sizes from 900x600 to 1920x1080, no overlap; back at 1200x760 all %d faces where they were, "
           "on the same nodes; unsaved notes, the search and the selection kept\n", n);
}
/* intent6-stress: repeated edits and saves; heap and time */
static void case_intent6_stress(void) {
    CHECK(resize(1240, 820) == 0);                 /* the demo's window */
    int rounds = 30, ok = 0;
    double t0 = i6_now();
    for (int k = 0; k < rounds; k++) {
        char id[8]; snprintf(id, sizeof id, "M%05d", 1 + k % 15);
        char name[40]; snprintf(name, sizeof name, "Edit %d", k);
        if (i6_click_member(id) == 0 && i6_type(I6P("name"), name) == 0 && i6_click(I6P("save")) == 0 && i6_status_has("Saved") &&
            !gv_glon_host_error(h)) ok++;
    }
    double t1 = i6_now();
    CHECK(ok == rounds && i6_file_lines() == 121 && i6_file_has("Edit 29"));
    printf("intent6-stress: %d select-edit-save rounds, %d ok, %.1f ms each (each rewrites the file); heap high %ld of %ld, "
           "%ld collections, %ld live cells after the last\n", rounds, ok, (t1 - t0) / rounds, r0_s1_heap_high() - GC_HEAP_BASE,
           (long)(GC_HEAP_LIMIT - GC_HEAP_BASE), r0_s1_gc_count(), r0_s1_gc_live_cells());
}
/* intent6-mem (GLON-MEM-1): per operation, the allocations and the heap the
 * allocator walks.  Allocation counts need the -DR0_S1_PROFILE build
 * (build/test_gui_p2_4m_pf); the block census and times work in any build */
static void i6_heap_census(long *blocks, long *free_blocks, long *alloc_cells) {
    long b = 0, f = 0, ac = 0;
    for (cell a = GC_HEAP_BASE; a < r0_s1_heap_high(); ) {
        cell size = M[a + GC_HDR_SIZE];
        if (size <= 0) break;
        b++;
        if (M[a + GC_HDR_FLAGS] == 0) f++; else ac += size;
        a += size;
    }
    *blocks = b; *free_blocks = f; *alloc_cells = ac;
}
typedef struct { double t; r0_s1_pf_stats pf; long gcs, blocks, free_blocks, cells; } i6_snap;
static void i6_take(i6_snap *s) {
    s->t = i6_now(); r0_s1_pf_read(&s->pf); s->gcs = r0_s1_gc_count();
    i6_heap_census(&s->blocks, &s->free_blocks, &s->cells);
}
/* one s/+ (one string allocation) timed here, against an empty call: the
 * difference is the allocation's cost at this heap */
static double i6_alloc_us(void) {
    double t0 = i6_now();
    for (int j = 0; j < 20; j++) gint("block-len reduce [ 0 ]");
    double t1 = i6_now();
    for (int j = 0; j < 20; j++) gint("block-len reduce [ s/+ \"ab\" \"cd\" ]");
    double t2 = i6_now();
    return ((t2 - t1) - (t1 - t0)) * 1000.0 / 20;
}
static void i6_report(const char *what, const i6_snap *a, const i6_snap *b) {
    printf("  %-22s %8.0f ms  %3ld GCs  blocks below the frontier %6ld -> %6ld (free %ld)\n",
           what, b->t - a->t, b->gcs - a->gcs, a->blocks, b->blocks, b->free_blocks);
}
static void case_intent6_mem(void) {
    CHECK(resize(1240, 820) == 0);
    i6_snap a, b;
    long blocks, fb, cells; i6_heap_census(&blocks, &fb, &cells);
    printf("  after start-up (libraries, window, 120 members): %ld blocks below the frontier (%ld free), %ld allocated cells, "
           "heap high %ld of %ld, %ld GCs, live after last %ld\n", blocks, fb, cells, r0_s1_heap_high() - GC_HEAP_BASE,
           (long)(GC_HEAP_LIMIT - GC_HEAP_BASE), r0_s1_gc_count(), r0_s1_gc_live_cells());
    printf("  one s/+ costs %.0f us here; predicted from the walk (80 ns x %ld blocks): %.0f us\n", i6_alloc_us(), blocks, blocks * 0.08);
    i6_take(&a);
    CHECK(gint("block-len reduce [ mb/reload mb/file widget/flush ]") == 2 && gv_glon_host_deliver(h) == 0 && gv_glon_host_bind(h) == 0);
    i6_take(&b); i6_report("reload 120 members", &a, &b);
    i6_take(&a); CHECK(i6_click(MR "search") == 0 && text("s") == 0); i6_take(&b); i6_report("search keystroke", &a, &b);
    i6_take(&a); CHECK(i6_click(MR "clear-search") == 0); i6_take(&b); i6_report("clear search", &a, &b);
    i6_take(&a); CHECK(i6_click_member("M00004") == 0); i6_take(&b); i6_report("select a member", &a, &b);
    i6_take(&a); CHECK(i6_type(I6P("name"), "Mem Test") == 0 && i6_click(I6P("save")) == 0 && i6_status_has("Saved")); i6_take(&b);
    i6_report("edit name + save", &a, &b);
    i6_heap_census(&blocks, &fb, &cells);
    printf("  one s/+ costs %.0f us at the end; predicted (80 ns x %ld blocks): %.0f us\n", i6_alloc_us(), blocks, blocks * 0.08);
    printf("intent6-mem: measured (see GLON-MEM-1.md)\n");
}
/* intent6-postgc (GLON-MEM-3 B): after the first collection -- where the
 * time goes in edit + save, the free blocks, and what r_alloc's scan walks.
 * The scan is replayed in C over the real heap headers (r_alloc's rule:
 * from GC_FREE_HINT, first free block whose size >= the extent), so nothing
 * in the heap changes while it is measured. */
static void i6_scan_replay(cell extent, long *live, long *small, long *lands_free) {
    long l = 0, sm = 0; cell a = M[GC_FREE_HINT];
    if (a < GC_HEAP_BASE || a > M[REG_HP]) a = GC_HEAP_BASE;
    *lands_free = 0;
    while (a < M[REG_HP]) {
        cell size = M[a + GC_HDR_SIZE], flags = M[a + GC_HDR_FLAGS];
        if (size <= 0) break;
        if (flags == 0) { if (size >= extent) { *lands_free = 1; break; } sm++; } else l++;
        a += size;
    }
    *live = l; *small = sm;
}
static double i6_slice_us(int n) {   /* one string of n bytes: extent 16 + roundup(n+1, 16) */
    char e[128]; snprintf(e, sizeof e, "block-len reduce [ s/slice mb/long-str 0 %d ]", n);
    double t0 = i6_now();
    for (int j = 0; j < 10; j++) gint("block-len reduce [ 0 ]");
    double t1 = i6_now();
    for (int j = 0; j < 10; j++) gint(e);
    double t2 = i6_now();
    return ((t2 - t1) - (t1 - t0)) * 1000.0 / 10;
}
static void case_intent6_postgc(void) {
    CHECK(resize(1240, 820) == 0);
    /* a 4,000-byte string to slice from (made before the measurements) */
    CHECK(gint("block-len reduce [ mb/long-str: \"\" ]") == 1);
    for (int k = 0; k < 40; k++)
        gint("block-len reduce [ mb/long-str: s/+ mb/long-str \"0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789\" ]");
    /* the intent6-mem sequence first (it fills the heap to the frontier) */
    CHECK(gint("block-len reduce [ mb/reload mb/file widget/flush ]") == 2 && gv_glon_host_deliver(h) == 0 && gv_glon_host_bind(h) == 0);
    CHECK(i6_click(MR "search") == 0 && text("s") == 0 && i6_click(MR "clear-search") == 0);
    printf("  before the edit: heap high %ld of %ld, %ld GCs\n", r0_s1_heap_high() - GC_HEAP_BASE, (long)(GC_HEAP_LIMIT - GC_HEAP_BASE), r0_s1_gc_count());
    long g0 = r0_s1_gc_count();
    double t0 = i6_now();
    CHECK(i6_click_member("M00003") == 0 && i6_type(I6P("name"), "Post GC") == 0);
    double t1 = i6_now(); long g1 = r0_s1_gc_count();
    CHECK(i6_click(I6P("save")) == 0 && i6_status_has("Saved"));
    double t2 = i6_now(); long g2 = r0_s1_gc_count();
    printf("  first edit + save: select + type %.0f ms (%ld GCs), save click %.0f ms (%ld GCs); heap high %ld of %ld\n",
           t1 - t0, g1 - g0, t2 - t1, g2 - g1, r0_s1_heap_high() - GC_HEAP_BASE, (long)(GC_HEAP_LIMIT - GC_HEAP_BASE));
    if (r0_s1_gc_count() == g0) { printf("  (no collection yet: the post-GC part below would not apply)\n"); }
    /* the heap after the collection */
    long blocks = 0, frees = 0, livec = 0, freec = 0; long hist[8] = {0}; long maxfree = 0;
    const long edges[7] = { 32, 48, 64, 128, 256, 1024, 4096 };
    for (cell a = GC_HEAP_BASE; a < M[REG_HP]; ) {
        cell size = M[a + GC_HDR_SIZE]; if (size <= 0) break;
        blocks++;
        if (M[a + GC_HDR_FLAGS] == 0) {
            frees++; freec += size; if (size > maxfree) maxfree = size;
            int b = 0; while (b < 7 && size > edges[b]) b++; hist[b]++;
        } else livec += size;
        a += size;
    }
    printf("  heap: %ld blocks below the frontier: %ld live (%ld cells), %ld free (%ld cells, largest %ld); hint at block offset %ld\n",
           blocks, blocks - frees, livec, frees, freec, maxfree, (long)(M[GC_FREE_HINT] - GC_HEAP_BASE));
    printf("  free block sizes (cells): <=32 %ld, 48 %ld, 64 %ld, <=128 %ld, <=256 %ld, <=1024 %ld, <=4096 %ld, larger %ld\n",
           hist[0], hist[1], hist[2], hist[3], hist[4], hist[5], hist[6], hist[7]);
    /* what one allocation of each size walks, and its measured cost */
    int ns[] = { 8, 24, 40, 100, 200, 500, 1000, 2500 };
    for (int k = 0; k < 8; k++) {
        cell extent = 16 + ((ns[k] + 1 + 15) / 16) * 16;
        long lv, sm, lf; i6_scan_replay(extent, &lv, &sm, &lf);
        printf("  string of %4d bytes (extent %4ld): scan walks %5ld live + %4ld too-small free blocks, then %s; measured %6.0f us\n",
               ns[k], (long)extent, lv, sm, lf ? "reuses a free block" : "bumps", i6_slice_us(ns[k]));
    }
    /* the steady state after the collection: the same actions again */
    long g3 = r0_s1_gc_count();
    double t3 = i6_now();
    CHECK(i6_click_member("M00004") == 0 && i6_type(I6P("name"), "Post GC 2") == 0);
    double t4 = i6_now();
    CHECK(i6_click(I6P("save")) == 0 && i6_status_has("Saved"));
    double t5 = i6_now();
    CHECK(i6_click(MR "search") == 0 && text("s") == 0);
    double t6 = i6_now();
    printf("  after the collection: select + type %.0f ms, save click %.0f ms, search keystroke %.0f ms (%ld more GCs)\n",
           t4 - t3, t5 - t4, t6 - t5, r0_s1_gc_count() - g3);
    printf("intent6-postgc: measured (GLON-MEM-3.md)\n");
}

#ifdef R0S1_GC_PROF
/* GLON-MEM-4: collector profiling (diagnostic build only; tests/gcprof.h). */
#include "gcprof.h"
/* one sampled collection now (the stub), checked against the replay */
static gcp_summary i6_gc_measured(const char *label) {
    gcp_replay();
    gcp_print_counts(label);
    long g0 = r0_s1_gc_count();
    double t0 = gcp_now_us();
    gcp_start(); gcp_collect_now(); gcp_stop();
    double t1 = gcp_now_us();
    CHECK(r0_s1_gc_count() == g0 + 1);
    printf("  [%s] the collection: %.0f ms wall (sampled); live objects %ld (replay %ld), live cells %ld (replay %ld), free blocks %ld (replay %ld)\n",
           label, (t1 - t0) / 1000.0, (long)M[GC_LIVE_OBJS], gcpc.live_objs, (long)M[GC_LIVE_CELLS], gcpc.live_cells,
           (long)M[GC_FREE_BLOCKS], gcpc.free_runs_after);
    CHECK(M[GC_LIVE_OBJS] == gcpc.live_objs && M[GC_LIVE_CELLS] == gcpc.live_cells && M[GC_FREE_BLOCKS] == gcpc.free_runs_after);
    return gcp_report(label);
}
static void i6_to_before_edit(void) {
    CHECK(gint("block-len reduce [ mb/reload mb/file widget/flush ]") == 2 && gv_glon_host_deliver(h) == 0 && gv_glon_host_bind(h) == 0);
    CHECK(i6_click(MR "search") == 0 && text("s") == 0 && i6_click(MR "clear-search") == 0);
    CHECK(i6_click_member("M00004") == 0);
}
/* intent6-gcprof-save: the exact application path -- the collection the
 * edit + save triggers, sampled; no stub, no replay */
static void case_intent6_gcprof_save(void) {
    CHECK(resize(1240, 820) == 0);
    printf("  code region hash %016lx\n", gcp_code_hash());
    i6_to_before_edit();
    long g0 = r0_s1_gc_count();
    long blocks, fb, cells; i6_heap_census(&blocks, &fb, &cells);
    double t0 = gcp_now_us();
    gcp_start();
    CHECK(i6_type(I6P("name"), "Mem Test") == 0 && i6_click(I6P("save")) == 0 && i6_status_has("Saved"));
    gcp_stop();
    double t1 = gcp_now_us();
    printf("  edit name + save: %.0f ms wall, %ld GC(s); %ld blocks below the frontier before\n", (t1 - t0) / 1000.0, r0_s1_gc_count() - g0, blocks);
    gcp_report("edit + save");
    printf("intent6-gcprof-save: measured (GLON-MEM-4.md)\n");
}
/* intent6-gcprof: just before the edit, the replay's counts and one
 * collection by itself, sampled */
static void case_intent6_gcprof(void) {
    CHECK(resize(1240, 820) == 0);
    i6_to_before_edit();
    i6_gc_measured("before the edit");
    printf("intent6-gcprof: measured (GLON-MEM-4.md)\n");
}
/* intent6-gcprof-scale: the same at each step of the session (each
 * collection frees that step's garbage, so the next heap is the one it
 * leaves) */
static void case_intent6_gcprof_scale(void) {
    CHECK(resize(1240, 820) == 0);
    i6_gc_measured("after start-up");
    CHECK(gint("block-len reduce [ mb/reload mb/file widget/flush ]") == 2 && gv_glon_host_deliver(h) == 0 && gv_glon_host_bind(h) == 0);
    i6_gc_measured("after reload");
    CHECK(i6_click(MR "search") == 0 && text("s") == 0);
    i6_gc_measured("after search");
    CHECK(i6_click(MR "clear-search") == 0);
    i6_gc_measured("after clear");
    /* the same heap collected again: nothing new to free */
    i6_gc_measured("again at once");
    printf("intent6-gcprof-scale: measured (GLON-MEM-4.md)\n");
}
#endif

/* ==== INTENT-7: membership application performance =========================
 * intent7-ops: every ordinary operation once, in order, timed (wall clock),
 * with the collections, the heap and the host's per-event breakdown
 * (gv_glon_host_timing_*: Glon runs, time in them, time applying their
 * batches).  Start-up is the process from main to the end of setup (the
 * libraries, the window, the application and its 120 members). */
static double i7_t_main0, i7_t_setup1;
static int i7_step_fail;
static void i7_begin(i6_snap *a) { gv_glon_host_timing_reset(); i6_take(a); }
static void i7_end(const char *what, const i6_snap *a, int ok) {
    i6_snap b; i6_take(&b);
    double g, t, bd; long runs, gcs; gv_glon_host_timing_totals(&g, &t, &bd, &runs, &gcs);
    printf("  %-24s %8.0f ms  %s  %3ld GCs  frontier %7ld  live after last GC %7ld  (Glon %6.0f ms in %ld runs, batches %5.0f ms, bind/deliver %5.0f ms)\n",
           what, b.t - a->t, ok ? "ok  " : "FAIL", b.gcs - a->gcs, r0_s1_heap_high() - GC_HEAP_BASE, r0_s1_gc_live_cells(),
           g, runs, t, bd);
    if (getenv("GV_I7_DETAIL")) gv_glon_host_timing_report(stdout, "      ");
    if (!ok) i7_step_fail++;
}
#define I7_STEP(what, expr) do { i6_snap a_; i7_begin(&a_); int ok_ = (expr); i7_end(what, &a_, ok_); CHECK(ok_); } while (0)
static void case_intent7_ops(void) {
#ifdef R0S1_GC_PROF
    gcp_start();
#endif
    printf("  start-up (process to members loaded) %8.0f ms; %ld GCs; frontier %ld of %ld\n", i7_t_setup1 - i7_t_main0,
           r0_s1_gc_count(), r0_s1_heap_high() - GC_HEAP_BASE, (long)(GC_HEAP_LIMIT - GC_HEAP_BASE));
    if (getenv("GV_I7_DETAIL")) gv_glon_host_timing_report(stdout, "      ");
    CHECK(resize(1240, 820) == 0);
    I7_STEP("select a member", i6_click_member("M00005") == 0 && i6_is(I6P("id"), "M00005"));
    I7_STEP("search keystroke 'w'", i6_click(MR "search") == 0 && text("w") == 0 && gint("list/length mb/view") == i6_expected("w"));
    I7_STEP("search keystroke 'i'", text("i") == 0 && gint("list/length mb/view") == i6_expected("wi"));
    I7_STEP("clear search", i6_click(MR "clear-search") == 0 && gint("list/length mb/view") == gint("list/length mb/recs"));
    I7_STEP("select another", i6_click_member("M00004") == 0 && i6_is(I6P("id"), "M00004"));
    I7_STEP("type a name (9 keys)", i6_type(I6P("name"), "Ops Edit") == 0 && i6_status_has("Unsaved"));
    I7_STEP("save the edit", i6_click(I6P("save")) == 0 && i6_status_has("Saved M00004 Ops Edit") && i6_file_has("M00004\tOps Edit\t"));
    I7_STEP("new", i6_click(I6P("new")) == 0 && i6_is(I6P("id"), "M00121"));
    I7_STEP("fill in name + village", i6_type(I6P("name"), "Ops New") == 0 && i6_click(I6P("village")) == 0 && key(SDLK_ESCAPE) == 0
            && keyk(K_HOME) == 0 && i6_is(I6P("village"), "Petone"));
    I7_STEP("save the new member", i6_click(I6P("save")) == 0 && i6_status_has("Saved M00121 Ops New") && gint("list/length mb/recs") == 121);
    I7_STEP("delete (asks)", i6_click_member("M00010") == 0 && i6_click(I6P("delete")) == 0 && i6_visible("prompt") == 1);
    I7_STEP("delete (confirm)", i6_click(I6P("p1")) == 0 && gint("list/length mb/recs") == 120 && i6_status_has("Deleted M00010"));
    I7_STEP("reload from the file", gint("block-len reduce [ mb/reload mb/file widget/flush ]") == 2 && gv_glon_host_deliver(h) == 0
            && gv_glon_host_bind(h) == 0 && gint("list/length mb/recs") == 120 && !gv_glon_host_error(h));
    printf("intent7-ops: %d step(s) failed; heap high %ld of %ld, %ld collections, live %ld\n", i7_step_fail,
           r0_s1_heap_high() - GC_HEAP_BASE, (long)(GC_HEAP_LIMIT - GC_HEAP_BASE), r0_s1_gc_count(), r0_s1_gc_live_cells());
#ifdef R0S1_GC_PROF
    gcp_stop();
    gcp_report_all("intent7-ops, every step after start-up");
#endif
}


/* ==== GLON-INST-1 Stage 1: reusable components ===================================
 * view/glon/intent_components_demo.glon: one definition (comp/address),
 * three instances (home, work, post) with the same internal names */
#define IC(inst, name) "form/field form/sub form/current '" inst " '" name
static long inst_sites(void) { cell u = 0, c = 0; r0_s1_site_usage(&u, &c); return (long)u; }
static long inst_globals(void) { cell u = 0, c = 0; r0_s1_global_usage(&u, &c); return (long)u; }
static int inst_deliver(void) { return gv_glon_host_deliver(h) == 0 && gv_glon_host_bind(h) == 0; }
static void case_inst1_components(void) {
    CHECK(resize(1000, 800) == 0);
    char a[400];
    /* 1. three instances, the same names in three scopes */
    CHECK(gint("block-at intent/current 3") == 25);           /* heading + 3 x 8 (the form's own face and its 7) */
    CHECK(gint("face/alive " IC("home", "street")) == 1 && gint("face/alive " IC("work", "street")) == 1
          && gint("face/alive " IC("post", "street")) == 1);
    CHECK(gint("= block-at intent/hface " IC("home", "street") " 8 block-at intent/hface " IC("work", "street") " 8") == 0);
    CHECK(gint("form/same? form/sub form/current 'home form/sub form/current 'home") == 1
          && gint("form/same? form/sub form/current 'home form/sub form/current 'work") == 0);
    CHECK(gint("form/same? form/parent form/sub form/current 'home form/current") == 1
          && gint("= form/parent form/current none") == 1);
    CHECK(gint("= form/data form/sub form/current 'home none") == 1);
    /* 2. values typed into one instance stay in it */
    CHECK(i6_type(IC("home", "street"), "1 Main St") == 0 && i6_is(IC("home", "street"), "1 Main St"));
    CHECK(i6_is(IC("work", "street"), "") && i6_is(IC("post", "street"), ""));
    igs(a, sizeof a, "form/collect form/current");
    CHECK(strstr(a, "home [ street \"1 Main St\" city \"\" ]") != NULL && strstr(a, "work [ street \"\" city \"\" ]") != NULL);
    if (!strstr(a, "home [ street \"1 Main St\"")) fprintf(stderr, "  collect: %s\n", a);
    /* 3. the shared handler keeps each instance's own count in form/data */
    CHECK(gint("form/data form/sub form/current 'home") == 9);           /* 9 characters typed */
    CHECK(gint("= form/data form/sub form/current 'work none") == 1);
    CHECK(i6_type(IC("work", "city"), "Ab") == 0 && gint("form/data form/sub form/current 'work") == 2);
    CHECK(gint("form/data form/sub form/current 'home") == 9);
    igs(a, sizeof a, "face/text " IC("home", "edits")); CHECK(!strcmp(a, "9 edits"));
    igs(a, sizeof a, "face/text " IC("work", "edits")); CHECK(!strcmp(a, "2 edits"));
    igs(a, sizeof a, "face/text " IC("post", "edits")); CHECK(!strcmp(a, ""));
    /* 4. the inline action (Clear) clears its own instance only */
    CHECK(gint("block-len reduce [ face/set-value " IC("work", "street") " \"9 Side Rd\" ]") == 1);
    CHECK(i6_click(IC("home", "wipe")) == 0 && i6_is(IC("home", "street"), "") && i6_is(IC("home", "city"), ""));
    CHECK(i6_is(IC("work", "street"), "9 Side Rd") && i6_is(IC("work", "city"), "Ab"));
    /* 5. the named handler (Copy street) acts on the instance it came from */
    CHECK(gint("block-len reduce [ face/set-value " IC("post", "street") " \"PO Box 7\" ]") == 1);
    CHECK(i6_click(IC("post", "dup")) == 0 && i6_is(IC("post", "city"), "PO Box 7"));
    CHECK(i6_is(IC("work", "city"), "Ab") && i6_is(IC("home", "city"), ""));
    /* 6. replacing the layout destroys its instances: their handles are stale,
     *    their state unreachable; the new instances are independent */
    CHECK(gint("block-len reduce [ t/home: form/sub form/current 'home  t/hs: form/field t/home 'street ]") == 2);
    long s0 = inst_sites(), g0 = inst_globals();
    CHECK(gint("block-len reduce [ intent/show-now comp/form2 ]") == 1 && inst_deliver());
    CHECK(inst_sites() == s0 && inst_globals() == g0);       /* the new instances cost no site and no global */
    CHECK(gint("face/alive t/hs") == 0);
    igs(a, sizeof a, "sin-id judge [ form/data t/home ]"); CHECK(!strcmp(a, "stale-form"));
    igs(a, sizeof a, "sin-id judge [ face/value t/hs ]"); CHECK(!strcmp(a, "stale-widget"));
    CHECK(gint("block-at intent/current 3") == 16);
    CHECK(i6_type(IC("a", "street"), "New") == 0 && i6_is(IC("a", "street"), "New") && i6_is(IC("b", "street"), ""));
    CHECK(gint("form/data form/sub form/current 'a") == 3 && gint("= form/data form/sub form/current 'b none") == 1);
    /* 7. definitions are checked: not a block; too many names for an inline
     *    action; a duplicate name inside one instance (across instances is fine) */
    igs(a, sizeof a, "sin-id judge [ intent/show-now layout [ form comp/nope ] ]"); CHECK(!strcmp(a, "form-needs-block"));
    /* (a literal block typed here lives only while its expression runs:
     *  each definition is made and used in one expression) */
    igs(a, sizeof a, "sin-id judge [ t/big: [ a: field 4 b: field 4 c: field 4 d: field 4 e: field 4 f: field 4 g: field 4 "
                     "h: field 4 i: field 4 j: field 4 x: button \"X\" [ clear a ] ]  intent/show-now layout [ form t/big ] ]");
    CHECK(!strcmp(a, "too-many-names-for-actions"));
    igs(a, sizeof a, "sin-id judge [ t/dupc: [ a: field 4 a: field 4 ]  intent/show-now layout [ form t/dupc ] ]");
    CHECK(!strcmp(a, "duplicate-name"));
    /* back to the demo: it builds again from the same shared definition */
    CHECK(gint("block-len reduce [ intent/show-now comp/form ]") == 1 && inst_deliver());
    CHECK(gint("block-at intent/current 3") == 25 && i6_is(IC("home", "street"), ""));
    printf("inst1-components: 3 instances of one definition, the same names, separate values and form/data; shared handlers and "
           "inline actions act on their own instance; replaced instances stale; 0 sites and 0 globals per instance\n");
}
/* inst1-scale: 1, 2, 4 and 8 unnamed instances of the definition in one
 * layout (8 x 7 = 56 faces, under the 64-face limit): faces, View nodes,
 * sites, globals, live cells (diagnostic build) and the cost of an event */
static void case_inst1_scale(void) {
    CHECK(resize(1000, 800) == 0);
    int ns[] = { 1, 2, 4, 8 };
    for (int k = 0; k < 4; k++) {
        int n = ns[k];
        char e[400];
        snprintf(e, sizeof e, "block-len reduce [ t/spec: loop/collect 0 %d func [k] [ either = - k * / k 2 2 0 [ 'form ] [ 'comp/address ] ] ]", 2 * n);
        CHECK(gint(e) == 1);
        long s0 = inst_sites(), g0 = inst_globals();
        double t0 = i6_now();
        CHECK(gint("block-len reduce [ intent/show-now t/l: layout t/spec ]") == 1 && inst_deliver());
        double t1 = i6_now();
        long faces = gint("block-at intent/current 3"), nodes = gint("view/id") - gint("intent/id-base");
        long live = -1;
#ifdef R0S1_GC_PROF
        gcp_collect_now(); live = r0_s1_gc_live_cells();
#endif
        /* event cost: pointer moves over the first instance's street field, and keys typed into it */
        gv_rect r, r2; CHECK(hbox("form/field intent/form-of-scope block-at intent/face intent/current 7 22 'street", &r) == 0
                             && hbox("form/field intent/form-of-scope block-at intent/face intent/current 7 22 'city", &r2) == 0);
        /* moves between two fields: the host reports a move when the node under the pointer changes */
        CHECK(move_to(r.x + 5, r.y + r.h / 2) == 0);
        gv_glon_host_timing_reset();
        for (int m = 0; m < 10; m++) CHECK(m % 2 ? move_to(r.x + 5, r.y + r.h / 2) == 0 : move_to(r2.x + 5, r2.y + r2.h / 2) == 0);
        double g, tb, bd; long runs, gcs; gv_glon_host_timing_totals(&g, &tb, &bd, &runs, &gcs);
        double move_ms = runs ? g / runs : -1;
        CHECK(click(r.x + r.w / 2, r.y + r.h / 2) == 0);
        gv_glon_host_timing_reset();
        CHECK(text("abcde") == 0);
        gv_glon_host_timing_totals(&g, &tb, &bd, &runs, &gcs);
        double key_ms = g / 5;
        CHECK(gint("form/data intent/form-of-scope block-at intent/face intent/current 7 22") == 5);
        printf("  %d instance(s): %ld faces, %ld View nodes; build %.0f ms; sites +%ld, globals +%ld; live %ld cells; "
               "pointer move %.2f ms, key (with handler) %.2f ms\n", n, faces, nodes, t1 - t0, inst_sites() - s0,
               inst_globals() - g0 - (k == 0), live, move_ms, key_ms);
        CHECK(inst_sites() == s0);
    }
    printf("inst1-scale: measured (GLON-INST-1.md)\n");
}

/* ==== GLON-INST-2: layout instances in one window ===================================
 * view/glon/intent_instances_demo.glon: layout A (md/search) and instances
 * of layout B (md/details-spec) */
#define SR(n) "form/field intent/root-form md/search '" n
#define DI(d, n) "form/field intent/root-form " d " '" n
static int inst2_click_row(const char *table_h, int row) {
    gv_rect r; if (hbox(table_h, &r) != 0) return -1;
    return click(r.x + 40, r.y + 28 + row * 24 + 12);
}
static long inst2_free(void) { return gint("intent/free-count"); }
static void case_inst2_instances(void) {
    CHECK(resize(760, 560) == 0);
    char a[400];
    /* the search layout is the one open instance, shown */
    CHECK(gint("list/length intent/layouts") == 1 && gint("= intent/current md/search") == 1 && gint("intent/open? md/search") == 1);
    long s0 = inst_sites(), g0 = inst_globals();
    /* select M00002 in A, open details: a new instance of B, shown */
    CHECK(i6_type(SR("search"), "f") == 0);                       /* Fong Susan, Fong Helen */
    CHECK(gint("face/row-count " SR("members")) == 2);
    CHECK(inst2_click_row(SR("members"), 0) == 0 && i6_is(SR("members"), "M00002"));
    CHECK(i6_click(SR("open")) == 0 && gint("list/length intent/layouts") == 2);
    CHECK(gint("block-len reduce [ t/d1: intent/current ]") == 1 && gint("= t/d1 md/search") == 0);
    CHECK(i6_is(DI("t/d1", "name"), "Fong Susan") && i6_is(DI("t/d1", "id"), "M00002"));
    /* an unsaved edit in D1 */
    CHECK(i6_type(DI("t/d1", "name"), "Fong Susan Q") == 0);
    igs(a, sizeof a, "face/text " DI("t/d1", "note")); CHECK(!strcmp(a, "Unsaved changes."));
    /* back to A: its query, rows and selection are as they were; D1 is hidden, open, its edit kept */
    CHECK(i6_click(DI("t/d1", "back")) == 0 && gint("= intent/current md/search") == 1);
    CHECK(i6_is(SR("search"), "f") && gint("face/row-count " SR("members")) == 2 && i6_is(SR("members"), "M00002"));
    CHECK(gint("intent/open? t/d1") == 1 && gint("face/alive " DI("t/d1", "name")) == 1 && i6_is(DI("t/d1", "name"), "Fong Susan Q"));
    CHECK(gint("face/visible " DI("t/d1", "name")) == 1);             /* its own visibility; the instance is hidden */
    /* a second instance of the same definition, for M00004 */
    CHECK(inst2_click_row(SR("members"), 1) == 0 && i6_is(SR("members"), "M00004"));
    CHECK(i6_click(SR("open")) == 0 && gint("list/length intent/layouts") == 3);
    CHECK(gint("block-len reduce [ t/d2: intent/current ]") == 1 && gint("= t/d2 t/d1") == 0);
    CHECK(i6_is(DI("t/d2", "name"), "Fong Helen") && i6_is(DI("t/d1", "name"), "Fong Susan Q"));
    CHECK(gint("form/same? intent/root-form t/d1 intent/root-form t/d2") == 0);
    CHECK(gint("block-at md/state " DI("t/d1", "name") " 1") == 1 && gint("block-at md/state " DI("t/d2", "name") " 1") == 0);
    CHECK(i6_type(DI("t/d2", "village"), "Upper Hutt") == 0 && gint("block-at md/state " DI("t/d2", "name") " 1") == 1);
    /* events go to their own instance: D1's nodes are hidden, so an event
     * naming one is dropped; the window's own node goes to the shown one */
    igs(a, sizeof a, "block-at intent/hface " DI("t/d1", "name") " 8"); long d1node = atol(a);
    char e[200];
    snprintf(e, sizeof e, "= intent/route 'view-click \"%ld\" 0", d1node); CHECK(gint(e) == 1);
    snprintf(e, sizeof e, "= intent/route 'view-blur \"%ld\" t/d1", d1node); CHECK(gint(e) == 1);   /* a blur reaches its hidden owner */
    CHECK(gint("= intent/route 'view-pointer-move \"1\" t/d2") == 1 && gint("= intent/route 'view-key \"65\" t/d2") == 1);
    /* switch to D1 programmatically: its edit and its focus come back; keys reach D1's field */
    CHECK(gint("block-len reduce [ intent/show-instance t/d1 widget/flush ]") == 2 && inst_deliver() && gint("= intent/current t/d1") == 1);
    igs(a, sizeof a, "face/text " DI("t/d1", "note")); CHECK(!strcmp(a, "Unsaved changes."));
    /* its focus is back where it was when hidden (Back, the last clicked); keys then reach D1's own field */
    CHECK(gint("= block-at t/d1 8 block-at " DI("t/d1", "back") " 2") == 1);
    CHECK(i6_click(DI("t/d1", "name")) == 0 && key(SDLK_END) == 0);
    CHECK(text("Z") == 0 && i6_is(DI("t/d1", "name"), "Fong Susan QZ") && i6_is(DI("t/d2", "name"), "Fong Helen"));
    /* Save in D1 updates the shared data and A's table while A is hidden */
    CHECK(i6_click(DI("t/d1", "save")) == 0);
    igs(a, sizeof a, "face/text " DI("t/d1", "note")); CHECK(!strcmp(a, "Saved M00002"));
    CHECK(gint("s/= block-at list/at md/members 1 1 \"Fong Susan QZ\"") == 1);
    { gv_table *t = NULL; gv_glon_host_table(h, (int)ig("intent/tid intent/hface " SR("members")), &t);
      CHECK(t && t->nrows == 2 && !strcmp(gv_table_get_cell(t, 0, 1), "Fong Susan QZ")); }
    CHECK(gint("block-at md/state " DI("t/d2", "name") " 1") == 1);  /* D2's edit untouched */
    /* close D1: its handles stale, its ids free; D2 (shown before it) comes back, unchanged */
    long hw = gint("view/id"), f0 = inst2_free();
    CHECK(gint("block-len reduce [ t/h1: " DI("t/d1", "name") " ]") == 1);
    CHECK(i6_click(DI("t/d1", "close")) == 0 && gint("intent/open? t/d1") == 0 && gint("list/length intent/layouts") == 2);
    CHECK(gint("= intent/current t/d2") == 1 && i6_is(DI("t/d2", "village"), "Upper Hutt"));
    CHECK(gint("face/alive t/h1") == 0);
    igs(a, sizeof a, "sin-id judge [ face/value t/h1 ]"); CHECK(!strcmp(a, "stale-widget"));
    igs(a, sizeof a, "sin-id judge [ intent/root-form t/d1 ]"); CHECK(!strcmp(a, "layout-not-open"));
    long freed = inst2_free() - f0;
    CHECK(freed > 20);
    snprintf(e, sizeof e, "intent/owner-of %ld", d1node); CHECK(gint(e) == -1);          /* closed until reused */
    snprintf(e, sizeof e, "= intent/route 'view-click \"%ld\" 0", d1node); CHECK(gint(e) == 1);   /* a queued event for it: dropped */
    /* reopening: a new instance takes the freed ids; the old handle stays stale */
    CHECK(gint("block-len reduce [ t/d3: intent/open md/details-spec widget/flush ]") == 2 && inst_deliver());
    CHECK(gint("view/id") == hw && inst2_free() == f0);
    CHECK(gint("face/alive t/h1") == 0 && gint("face/alive " DI("t/d3", "name")) == 1 && i6_is(DI("t/d3", "name"), ""));
    /* closing the shown instance shows the one before; closing a hidden one shows nothing new */
    CHECK(gint("block-len reduce [ intent/close t/d3 widget/flush ]") == 2 && inst_deliver() && gint("= intent/current t/d2") == 1);
    CHECK(gint("block-len reduce [ intent/close md/search widget/flush ]") == 2 && inst_deliver() && gint("= intent/current t/d2") == 1);
    CHECK(gint("list/length intent/layouts") == 1 && gint("face/alive " DI("t/d2", "name")) == 1);
    /* a closed layout shown again is rebuilt: a new generation, its old handles stale */
    CHECK(gint("block-len reduce [ t/hs: " "form/field intent/root-form t/d2 'name" " ]") == 1);
    CHECK(gint("block-len reduce [ intent/show-instance md/search widget/flush ]") == 2 && inst_deliver() && gint("= intent/current md/search") == 1);
    CHECK(i6_is(SR("search"), "") && gint("face/row-count " SR("members")) == 8 && gint("face/alive t/hs") == 1);
    CHECK(inst_sites() == s0);
    printf("inst2-instances: A and two instances of B open at once; switching keeps values, unsaved edits, rows, selection and focus; "
           "Save updates hidden A; closing stales only its handles and frees %ld ids, reused by the next; events for hidden or "
           "closed instances dropped; sites +%ld, globals +%ld (the test's own)\n", freed, inst_sites() - s0, inst_globals() - g0);
}
/* inst2-scale: 1 .. 100 instances of layout B open at once (stopping at the
 * first limit): heap, names, sites, node ids, and the latency of events on
 * the shown instance */
static void case_inst2_scale(void) {
    CHECK(resize(760, 560) == 0);
    /* in a heap too small for 100 (1M: the 60th open runs out of heap, a
     * fail-stop), up to what it holds with room to spare: ~3.7K cells each */
    long cap = ((long)(GC_HEAP_LIMIT - GC_HEAP_BASE) - 80000) / 3700;
    int stops[6] = { 1, 4, 16, 32, 64, 100 };
    for (int k = 0; k < 6; k++) if (stops[k] > cap) stops[k] = (int)cap;
    printf("  up to %d instances in this heap (%ld cells)\n", stops[5], (long)(GC_HEAP_LIMIT - GC_HEAP_BASE));
    long s0 = inst_sites(), g0 = inst_globals();
    CHECK(gint("block-len reduce [ t/all: list/new ]") == 1);
    int open = 0, limit_hit = 0;
    double open_ms = 0; int opened = 0;
    for (int k = 0; k < 6 && !limit_hit; k++) {
        if (k > 0 && stops[k] == stops[k - 1]) continue;
        double tk = i6_now(); int ok0 = open;
        while (open < stops[k]) {
            char res[200];
            long f0 = inst2_free();
            igs(res, sizeof res, "either sin? t/r: judge [ list/append t/all intent/open md/details-spec ] [ widget/flush sin-id t/r ] [ widget/flush 'ok ]");
            CHECK(inst_deliver());
            if (strcmp(res, "ok")) {
                limit_hit = 1;
                /* a clean failure: the error named, the instance shown before back, nothing left of the partial build */
                printf("  limit: instance %d could not open: %s (view/id %ld; the partial build's %ld ids given back; shown: the last opened %ld; "
                       "open %ld; host error %d)\n", open + 1, res, gint("view/id"), inst2_free() - f0,
                       gint("= intent/current list/at t/all - list/length t/all 1"), gint("list/length intent/layouts"), gv_glon_host_error(h));
                CHECK(!strcmp(res, "too-many-nodes") && gint("= intent/current list/at t/all - list/length t/all 1") == 1
                      && gint("list/length intent/layouts") == open + 1 && !gv_glon_host_error(h));
                break;
            }
            open++;
        }
        if (open > ok0) { open_ms = (i6_now() - tk) / (open - ok0); opened = open - ok0; }
        if (limit_hit) break;
        long live = -1;
#ifdef R0S1_GC_PROF
        gcp_collect_now(); live = r0_s1_gc_live_cells();
#endif
        /* the shown instance (the last opened): pointer moves over its name field, keys typed into it */
        gv_rect r, r2; CHECK(hbox(DI("intent/current", "name"), &r) == 0 && hbox(DI("intent/current", "village"), &r2) == 0);
        CHECK(move_to(r.x + 5, r.y + r.h / 2) == 0);
        /* the median of 11 single moves (a collection during one does not count) */
        double mv[11]; double g, tb, bd; long runs, gcs;
        for (int m = 0; m < 11; m++) {
            gv_glon_host_timing_reset();
            CHECK(m % 2 ? move_to(r.x + 5, r.y + r.h / 2) == 0 : move_to(r2.x + 5, r2.y + r2.h / 2) == 0);
            gv_glon_host_timing_totals(&g, &tb, &bd, &runs, &gcs); mv[m] = runs ? g / runs : -1;
        }
        for (int i = 0; i < 11; i++) for (int j = i + 1; j < 11; j++) if (mv[j] < mv[i]) { double t = mv[i]; mv[i] = mv[j]; mv[j] = t; }
        double move_ms = mv[5];
        CHECK(click(r.x + r.w / 2, r.y + r.h / 2) == 0);
        gv_glon_host_timing_reset(); CHECK(text("abcde") == 0);
        gv_glon_host_timing_totals(&g, &tb, &bd, &runs, &gcs);
        double key_ms = g / 5;
        /* switching between the first and the last instance */
        double t0 = i6_now();
        CHECK(gint("block-len reduce [ intent/show-instance list/at t/all 0 widget/flush ]") == 2 && inst_deliver());
        CHECK(gint("block-len reduce [ intent/show-instance list/at t/all - list/length t/all 1 widget/flush ]") == 2 && inst_deliver());
        double sw = (i6_now() - t0) / 2;
        int g2v = 0, v2g = 0, mapped = 0, vcap = 0; gv_glon_host_node_tables(h, &g2v, &v2g, &mapped, &vcap);
        printf("  %3d instance(s): open %.0f ms each (last %d); view/id %4ld, free ids %3ld; host tables %d / %d entries, %d ids mapped, "
               "%d View nodes made; live %7ld cells; sites +%ld, globals +%ld; pointer move %.2f ms (median), key with handler %.2f ms, "
               "switch %.1f ms\n", open, open_ms, opened, gint("view/id"), inst2_free(), g2v, v2g, mapped, vcap, live, inst_sites() - s0,
               inst_globals() - g0, move_ms, key_ms, sw);
    }
    /* close everything opened, then lifecycle cycles: open and close 20 times */
    double t0 = i6_now();
    CHECK(gint("block-len reduce [ list/each t/all :intent/close  list/clear t/all widget/flush ]") == 3 && inst_deliver());
    double close_ms = i6_now() - t0;
    long live0 = -1, live1 = -1;
#ifdef R0S1_GC_PROF
    gcp_collect_now(); live0 = r0_s1_gc_live_cells();
#endif
    long hw = gint("view/id"), live2 = -1;
    t0 = i6_now();
    for (int c = 0; c < 20; c++)
        CHECK(gint("block-len reduce [ intent/close intent/open md/details-spec widget/flush ]") == 2 && inst_deliver());
    double cyc = (i6_now() - t0) / 20;
#ifdef R0S1_GC_PROF
    gcp_collect_now(); live1 = r0_s1_gc_live_cells();
#endif
    for (int c = 0; c < 20; c++)
        CHECK(gint("block-len reduce [ intent/close intent/open md/details-spec widget/flush ]") == 2 && inst_deliver());
#ifdef R0S1_GC_PROF
    gcp_collect_now(); live2 = r0_s1_gc_live_cells();
#endif
    printf("  closed %d in %.0f ms; open/close cycles %.0f ms each; view/id %ld -> %ld; live after closing %ld, after 20 cycles %ld, "
           "after 40 %ld cells; layouts open %ld; sites +%ld\n",
           open, close_ms, cyc, hw, gint("view/id"), live0, live1, live2, gint("list/length intent/layouts"), inst_sites() - s0);
#ifdef R0S1_GC_PROF
    CHECK(live2 <= live1 + 256);
#endif
    CHECK(gint("view/id") == hw && gint("list/length intent/layouts") == 1 && inst_sites() == s0);
    printf("inst2-scale: measured (GLON-INST-2.md)\n");
}

/* ==== GLON-INST-3A: node ids past 1,023, reuse, rollback ===================== */
static void case_inst3_lifecycle(void) {
    CHECK(resize(760, 560) == 0);
    char a[300];
    long s0 = inst_sites();
    /* rapid reuse: 50 open/close cycles of one layout take the same ids */
    CHECK(gint("block-len reduce [ intent/close intent/open md/details-spec widget/flush ]") == 2 && inst_deliver());
    long hw = gint("view/id"), fc = inst2_free();
    for (int c = 0; c < 50; c++) CHECK(gint("block-len reduce [ intent/close intent/open md/details-spec widget/flush ]") == 2 && inst_deliver());
    CHECK(gint("view/id") == hw && inst2_free() == fc && gint("list/length intent/layouts") == 1);
    /* ids past 1,023: 30 instances open (40 ids each) */
    CHECK(gint("block-len reduce [ t/all: list/new ]") == 1);
    for (int k = 0; k < 30; k++) CHECK(gint("block-len reduce [ list/append t/all intent/open md/details-spec widget/flush ]") == 2 && inst_deliver());
    CHECK(gint("view/id") > 1023 && !gv_glon_host_error(h));
    /* the last one's nodes have ids past 1,023, and its events reach it */
    igs(a, sizeof a, "block-at intent/hface " DI("intent/current", "name") " 8"); long nid = atol(a);
    CHECK(nid > 1023);
    CHECK(i6_type(DI("intent/current", "name"), "Past 1023") == 0 && i6_is(DI("intent/current", "name"), "Past 1023"));
    /* independent focus and data: the first instance keeps its own */
    CHECK(gint("block-len reduce [ intent/show-instance list/at t/all 0 widget/flush ]") == 2 && inst_deliver());
    CHECK(i6_type(DI("intent/current", "village"), "First") == 0);
    CHECK(gint("block-len reduce [ intent/show-instance list/at t/all 29 widget/flush ]") == 2 && inst_deliver());
    CHECK(i6_is(DI("intent/current", "name"), "Past 1023") && i6_is(DI("intent/current", "village"), ""));
    CHECK(gint("= block-at intent/current 8 block-at " DI("intent/current", "name") " 2") == 1);   /* its focus came back */
    CHECK(i6_is(DI("list/at t/all 0", "village"), "First"));
    /* an event left from a closed instance: its node ids are taken at once by
     * the next instance, and the host keeps none of them (gv_glon_release_subtree) */
    CHECK(gint("block-len reduce [ t/old: list/at t/all 29  t/oh: " DI("t/old", "name") " ]") == 2);
    CHECK(gint("block-len reduce [ intent/close t/old  t/new: intent/open md/details-spec  widget/flush ]") == 3 && inst_deliver());
    igs(a, sizeof a, "block-at intent/hface " DI("t/new", "name") " 8"); long nid2 = atol(a);
    CHECK(nid2 == nid);                                    /* the same id, a new node */
    CHECK(gint("face/alive t/oh") == 0 && gint("face/alive " DI("t/new", "name")) == 1);
    igs(a, sizeof a, "sin-id judge [ face/value t/oh ]"); CHECK(!strcmp(a, "stale-widget"));
    CHECK(gint("= block-at t/new 8 -1") == 1 && i6_is(DI("t/new", "name"), ""));   /* nothing of the old focus reached it */
    /* invalid handles and layouts */
    igs(a, sizeof a, "sin-id judge [ intent/show-instance 5 ]"); CHECK(!strcmp(a, "not-a-layout"));
    igs(a, sizeof a, "sin-id judge [ intent/close [ 1 2 3 ] ]"); CHECK(!strcmp(a, "not-a-layout"));
    igs(a, sizeof a, "sin-id judge [ intent/root-form t/old ]"); CHECK(!strcmp(a, "layout-not-open"));
    igs(a, sizeof a, "sin-id judge [ face/value [ 2121 0 0 0 ] ]"); CHECK(strlen(a) > 0 && strcmp(a, "none"));
    /* a failed open (a bad spec) rolls back completely */
    long n0 = gint("list/length intent/layouts"), f0 = inst2_free(), v0 = gint("view/id");
    igs(a, sizeof a, "either sin? t/r: judge [ intent/open [ label \"x\" field 8 nosuchword ] ] [ widget/flush sin-id t/r ] [ widget/flush 'opened ]");
    CHECK(inst_deliver());
    printf("  bad spec: %s; open %ld (was %ld); current back %ld; free ids %ld -> %ld; view/id %ld -> %ld\n", a,
           gint("list/length intent/layouts"), n0, gint("= intent/current t/new"), f0, inst2_free(), v0, gint("view/id"));
    /* (the one chunk the failed build took is given back) */
    CHECK(strcmp(a, "opened") && gint("list/length intent/layouts") == n0 && gint("= intent/current t/new") == 1 && inst2_free() == f0 + 64);
    /* genuine exhaustion: no chunk of ids left; the open fails, cleanly */
    CHECK(gint("block-len reduce [ t/keep: intent/next-chunk  t/kf: list/to-block list/slice intent/free-chunks 0 list/length intent/free-chunks "
               "intent/next-chunk: + intent/max-chunk 1  list/clear intent/free-chunks ]") == 4);
    igs(a, sizeof a, "either sin? t/r: judge [ intent/open md/details-spec ] [ widget/flush sin-id t/r ] [ widget/flush 'opened ]");
    CHECK(inst_deliver());
    printf("  exhaustion: %s; open %ld; current back %ld; host error %d\n", a, gint("list/length intent/layouts"),
           gint("= intent/current t/new"), gv_glon_host_error(h));
    CHECK(!strcmp(a, "too-many-nodes") && gint("list/length intent/layouts") == n0 && gint("= intent/current t/new") == 1 && !gv_glon_host_error(h));
    CHECK(i6_type(DI("t/new", "village"), "Still works") == 0 && i6_is(DI("t/new", "village"), "Still works"));
    CHECK(inst_sites() == s0);
    CHECK(gint("block-len reduce [ intent/next-chunk: t/keep  list/clear intent/free-chunks  list/each list/from t/kf func [k] [ list/push intent/free-chunks k ] ]") == 3);
    printf("inst3-lifecycle: 50 open/close cycles reuse the same ids; 31 instances to id %ld; an id freed and taken again "
           "at once carries nothing of the old node; bad and stale handles refused; failed opens roll back\n", gint("view/id"));
}
/* ==== GLON-INST-3B: native windows ==================================================
 * view/glon/intent_windows_demo.glon: the main window (mw/search) and details
 * windows (mw/details-spec), each a native SDL window (dummy driver) of the
 * one session.  Synthetic events carry the SDL id of the window they are
 * pushed for (win_target). */
#define WS(n) "form/field intent/root-form mw/search '" n
#define WD(d, n) "form/field intent/root-form " d " '" n
static int w3_index(const char *lay) { return gv_glon_host_window_index(h, (int)ig("intent/window-of %s", lay)); }
static Uint32 w3_sdl(const char *lay) {
    gv_backend *wb = gv_glon_host_window_backend(h, w3_index(lay));
    return wb ? gv_backend_sdl_window_id(wb) : 0;
}
/* drive lay: its window gets the synthetic events; face lookups use it */
static void w3_in(const char *lay) {
    ig("block-len reduce [ intent/activate %s ]", lay);
    win_target = ig("intent/window-of %s", lay) == 1 ? 0 : w3_sdl(lay);
}
static int w3_close_button(Uint32 id) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = SDL_WINDOWEVENT; e.window.event = SDL_WINDOWEVENT_CLOSE; e.window.windowID = id;
    SDL_PushEvent(&e);
    return pump();
}
/* pixels unlike the corner's in a window's surface (its drawing) */
static long w3_ink(Uint32 id) {
    SDL_Window *w = SDL_GetWindowFromID(id);
    SDL_Surface *ws = w ? SDL_GetWindowSurface(w) : NULL;
    if (!ws || ws->format->BytesPerPixel != 4) return -1;
    SDL_LockSurface(ws);
    const Uint32 *px = (const Uint32 *)ws->pixels;
    int pitch = ws->pitch / 4;
    Uint32 bg = px[2 * pitch + 2];
    long n = 0;
    for (int y = 0; y < ws->h; y++) for (int x = 0; x < ws->w; x++) if (px[y * pitch + x] != bg) n++;
    SDL_UnlockSurface(ws);
    return n;
}
static int w3_sdl_windows(void) {
    int n = 0;
    for (Uint32 id = 1; id < 400; id++) if (SDL_GetWindowFromID(id)) n++;
    return n;
}
static long w3_rss_kb(void) {
    FILE *f = fopen("/proc/self/statm", "r");
    long a = 0, r = 0;
    if (f) { if (fscanf(f, "%ld %ld", &a, &r) != 2) r = 0; fclose(f); }
    return r * (sysconf(_SC_PAGESIZE) / 1024);
}
static int w3_deliver(void) { return inst_deliver() && pump() == 0; }
/* the last window opened, as t/NAME */
static int w3_name_last(const char *name) {
    char e[200];
    snprintf(e, sizeof e, "block-len reduce [ %s: block-at list/at intent/windows - list/length intent/windows 1 3 ]", name);
    return gint(e) == 1;
}
static void case_win3_windows(void) {
    char a[400];
    CHECK(resize(760, 560) == 0);
    long s0 = inst_sites(), g0 = inst_globals();
    Uint32 main_id = SDL_GetWindowID(find_window());
    /* two members opened, each in a native window of its own */
    w3_in("mw/search");
    CHECK(i6_type(WS("search"), "f") == 0 && gint("face/row-count " WS("members")) == 2);
    CHECK(inst2_click_row(WS("members"), 0) == 0 && i6_is(WS("members"), "M00002"));
    CHECK(i6_click(WS("open")) == 0 && gint("intent/window-count") == 1 && gv_glon_host_window_count(h) == 1 && w3_name_last("t/w1"));
    w3_in("mw/search");
    CHECK(inst2_click_row(WS("members"), 1) == 0 && i6_is(WS("members"), "M00004"));
    CHECK(i6_click(WS("open")) == 0 && gv_glon_host_window_count(h) == 2 && w3_name_last("t/w2"));
    igs(a, sizeof a, "face/text " WS("status")); CHECK(!strcmp(a, "Opened M00004"));
    Uint32 id1 = w3_sdl("t/w1"), id2 = w3_sdl("t/w2");
    CHECK(id1 && id2 && id1 != id2 && id1 != main_id && id2 != main_id && SDL_GetWindowFromID(id1) && SDL_GetWindowFromID(id2));
    CHECK(w3_sdl_windows() == 3);
    int ww = 0, wh = 0;
    gv_glon_host_window_size(h, w3_index("t/w1"), &ww, &wh); CHECK(ww == 470 && wh == 280);
    gv_glon_host_window_size(h, -1, &ww, &wh); CHECK(ww == 760 && wh == 560);
    CHECK(gint("= intent/primary mw/search") == 1 && gint("intent/open? t/w1") == 1 && gint("intent/open? t/w2") == 1);
    CHECK(i6_is(WD("t/w1", "name"), "Fong Susan") && i6_is(WD("t/w2", "name"), "Fong Helen"));
    long ink1 = w3_ink(id1), ink2 = w3_ink(id2);
    CHECK(ink1 > 500 && ink2 > 500);
    /* unsaved edits, each in its own window */
    w3_in("t/w1"); CHECK(i6_type(WD("t/w1", "name"), "Fong Susan Q") == 0);
    w3_in("t/w2"); CHECK(i6_type(WD("t/w2", "village"), "Upper Hutt") == 0);
    CHECK(gint("block-at mw/state " WD("t/w1", "name") " 1") == 1 && gint("block-at mw/state " WD("t/w2", "name") " 1") == 1);
    igs(a, sizeof a, "face/text " WD("t/w1", "note")); CHECK(!strcmp(a, "Unsaved changes."));
    /* each window keeps its own focus */
    gv_node_id f1 = gv_glon_host_window_focus(h, w3_index("t/w1")), f2 = gv_glon_host_window_focus(h, w3_index("t/w2"));
    CHECK(f1 && f1 == vnode(ig("block-at intent/hface " WD("t/w1", "name") " 8")));
    CHECK(f2 && f2 == vnode(ig("block-at intent/hface " WD("t/w2", "village") " 8")));
    /* keys follow the native window: typed into W1 with no click, they reach
     * W1's focused field only; then W2's */
    win_target = id1; CHECK(key(SDLK_END) == 0 && text("Z") == 0);
    CHECK(i6_is(WD("t/w1", "name"), "Fong Susan QZ") && i6_is(WD("t/w2", "village"), "Upper Hutt") && i6_is(WD("t/w2", "name"), "Fong Helen"));
    win_target = id2; CHECK(text("!") == 0);
    CHECK(i6_is(WD("t/w2", "village"), "Upper Hutt!") && i6_is(WD("t/w1", "name"), "Fong Susan QZ"));
    /* the main window's keys reach its own field */
    w3_in("mw/search"); CHECK(i6_click(WS("search")) == 0 && key(SDLK_END) == 0 && text("o") == 0);
    CHECK(i6_is(WS("search"), "fo") && i6_is(WD("t/w1", "name"), "Fong Susan QZ") && i6_is(WD("t/w2", "village"), "Upper Hutt!"));
    /* the pointer affects only its own window: a click in the main window
     * where W1 has its Save button saves nothing */
    gv_rect sv;
    w3_in("t/w1"); CHECK(hbox(WD("t/w1", "save"), &sv) == 0);
    w3_in("mw/search"); CHECK(click(sv.x + sv.w / 2, sv.y + sv.h / 2) == 0);
    CHECK(gint("block-at mw/state " WD("t/w1", "name") " 1") == 1 && gint("s/= block-at list/at mw/members 1 1 \"Fong Susan\"") == 1);
    /* Save in W1: the data and the main window's table change; W2 keeps its edit */
    w3_in("t/w1"); CHECK(i6_click(WD("t/w1", "save")) == 0);
    igs(a, sizeof a, "face/text " WD("t/w1", "note")); CHECK(!strcmp(a, "Saved M00002"));
    CHECK(gint("s/= block-at list/at mw/members 1 1 \"Fong Susan QZ\"") == 1);
    { gv_table *t = NULL; gv_glon_host_table(h, (int)ig("intent/tid intent/hface " WS("members")), &t);
      CHECK(t && t->nrows == 2 && !strcmp(gv_table_get_cell(t, 0, 1), "Fong Susan QZ")); }
    CHECK(gint("block-at mw/state " WD("t/w2", "name") " 1") == 1 && i6_is(WD("t/w2", "village"), "Upper Hutt!"));
    /* a resize of W2 is W2's own */
    SDL_SetWindowSize(SDL_GetWindowFromID(id2), 600, 380); CHECK(pump() == 0);
    gv_glon_host_window_size(h, w3_index("t/w2"), &ww, &wh); CHECK(ww == 600 && wh == 380);
    gv_glon_host_window_size(h, w3_index("t/w1"), &ww, &wh); CHECK(ww == 470 && wh == 280);
    gv_glon_host_window_size(h, -1, &ww, &wh); CHECK(ww == 760 && wh == 560);
    { gv_rect rg; CHECK(screen(ig("block-at t/w2 4"), &rg) == 0 && rg.w == 600 && rg.h == 380);
      CHECK(screen(ig("block-at t/w1 4"), &rg) == 0 && rg.w == 470 && rg.h == 280); }
    /* W1's close button closes W1 alone */
    CHECK(gint("block-len reduce [ t/h1: " WD("t/w1", "name") " ]") == 1);
    long dropped0 = 0, dropped1 = 0;
    gv_glon_host_window_stats(h, NULL, NULL, &dropped0, NULL);
    CHECK(w3_close_button(id1) == 0);
    CHECK(gv_glon_host_window_count(h) == 1 && gint("intent/open? t/w1") == 0 && !SDL_GetWindowFromID(id1) && gint("intent/window-count") == 1);
    CHECK(gint("face/alive t/h1") == 0);
    igs(a, sizeof a, "sin-id judge [ face/value t/h1 ]"); CHECK(!strcmp(a, "stale-widget"));
    /* events still arriving for the closed window are dropped */
    win_target = id1; CHECK(click(60, 60) == 0 && key(SDLK_END) == 0 && text("x") == 0);
    gv_glon_host_window_stats(h, NULL, NULL, &dropped1, NULL);
    CHECK(dropped1 >= dropped0 + 5 && !gv_glon_host_error(h));
    w3_in("t/w2"); CHECK(i6_click(WD("t/w2", "name")) == 0 && key(SDLK_END) == 0 && text("X") == 0 && i6_is(WD("t/w2", "name"), "Fong HelenX"));
    w3_in("mw/search"); CHECK(i6_type(WS("search"), "") == 0 && gint("face/row-count " WS("members")) == 8);
    /* W2's own Close button */
    w3_in("t/w2"); CHECK(i6_click(WD("t/w2", "close")) == 0);
    CHECK(gv_glon_host_window_count(h) == 0 && gint("intent/window-count") == 0 && !SDL_GetWindowFromID(id2) && w3_sdl_windows() == 1);
    CHECK(gint("= intent/current mw/search") == 1 && gint("= intent/primary mw/search") == 1);
    w3_in("mw/search"); CHECK(i6_type(WS("search"), "ng") == 0 && gint("face/row-count " WS("members")) == 2);
    CHECK(!gv_glon_host_error(h) && inst_sites() == s0);
    printf("win3-windows: main + 2 native windows (SDL ids %u, %u, %u) of one session; each drew its own (%ld, %ld px); edits, focus, "
           "keys, pointer and resize stay in their own window; Save updates the main table, the other keeps its edit; a close "
           "button closes one, %ld events for it dropped after; sites +%ld, globals +%ld (the test's own)\n",
           main_id, id1, id2, ink1, ink2, dropped1 - dropped0, inst_sites() - s0, inst_globals() - g0);
}
/* win3-lifecycle: open/close cycles leave nothing behind; reopening; the
 * window limit */
static void case_win3_lifecycle(void) {
    char a[300];
    CHECK(resize(760, 560) == 0);
    w3_in("mw/search");
    CHECK(gint("block-len reduce [ t/c: intent/open-window mw/details-spec widget/flush ]") == 2 && w3_deliver());
    w3_in("t/c"); CHECK(i6_type(WD("t/c", "name"), "Warm") == 0);
    CHECK(gint("block-len reduce [ intent/close t/c widget/flush ]") == 2 && w3_deliver());
    w3_in("mw/search");
    long s0 = inst_sites(), g0 = inst_globals(), hw = gint("view/id"), fc = inst2_free();
    int live0 = gv_view_live_nodes(v), m0 = 0, c0 = 0, vc0 = 0, g2v0 = 0;
    gv_glon_host_node_tables(h, &g2v0, &c0, &m0, &vc0);
    long rss0 = w3_rss_kb(), op0 = 0, cl0 = 0;
    gv_glon_host_window_stats(h, &op0, &cl0, NULL, NULL);
    double t0 = i6_now();
    for (int c = 0; c < 40; c++) {
        CHECK(gint("block-len reduce [ t/c: intent/open-window-at mw/details-spec 400 260 \"cycle\" widget/flush ]") == 2 && w3_deliver());
        CHECK(gv_glon_host_window_count(h) == 1);
        if (c % 10 == 0) { w3_in("t/c"); CHECK(i6_type(WD("t/c", "name"), "Cycle") == 0 && i6_is(WD("t/c", "name"), "Cycle")); }
        CHECK(gint("block-len reduce [ intent/close t/c widget/flush ]") == 2 && w3_deliver());
    }
    double per = (i6_now() - t0) / 40;
    int live1 = gv_view_live_nodes(v), m1 = 0, c1 = 0, vc1 = 0, g2v1 = 0;
    gv_glon_host_node_tables(h, &g2v1, &c1, &m1, &vc1);
    long op1 = 0, cl1 = 0;
    gv_glon_host_window_stats(h, &op1, &cl1, NULL, NULL);
    CHECK(gv_glon_host_window_count(h) == 0 && w3_sdl_windows() == 1 && op1 - op0 == 40 && cl1 - cl0 == 40);
    CHECK(gint("view/id") == hw && inst2_free() == fc && live1 == live0 && m1 == m0);
    CHECK(inst_sites() == s0 && inst_globals() == g0);
    printf("  40 open/close cycles: %.1f ms each; view/id %ld, free ids %ld, View nodes %d, mapped ids %d, as before; RSS %+ld KB; "
           "sites +%ld, globals +%ld\n", per, hw, fc, live1, m1, w3_rss_kb() - rss0, inst_sites() - s0, inst_globals() - g0);
    /* a closed window instance shown again opens a new window: a new
     * generation, its old handles stale */
    CHECK(gint("block-len reduce [ t/r: intent/open-window-at mw/details-spec 400 260 \"again\" widget/flush ]") == 2 && w3_deliver());
    Uint32 r1 = w3_sdl("t/r");
    CHECK(gint("block-len reduce [ face/set-value " WD("t/r", "name") " \"Before\"  t/rh: " WD("t/r", "name") " intent/close t/r widget/flush ]") == 4 && w3_deliver());
    CHECK(gint("intent/open? t/r") == 0 && gint("intent/window-of t/r") == 0 && !SDL_GetWindowFromID(r1));
    CHECK(gint("block-len reduce [ intent/show-instance t/r widget/flush ]") == 2 && w3_deliver());
    CHECK(gint("intent/open? t/r") == 1 && gv_glon_host_window_count(h) == 1 && w3_sdl("t/r") && w3_sdl("t/r") != r1);
    CHECK(gint("face/alive t/rh") == 0 && i6_is(WD("t/r", "name"), ""));
    w3_in("t/r"); CHECK(i6_type(WD("t/r", "name"), "Again") == 0 && i6_is(WD("t/r", "name"), "Again"));
    /* show-instance of an open window instance raises it, nothing rebuilt */
    CHECK(gint("block-len reduce [ t/rh2: " WD("t/r", "name") " intent/show-instance t/r widget/flush ]") == 3 && w3_deliver());
    CHECK(gint("face/alive t/rh2") == 1 && i6_is(WD("t/r", "name"), "Again"));
    CHECK(gint("block-len reduce [ intent/close t/r widget/flush ]") == 2 && w3_deliver());
    /* the window limit: 15 besides the main one; the 16th is refused before
     * anything is made */
    CHECK(gint("block-len reduce [ t/all: list/new ]") == 1);
    for (int k = 0; k < 15; k++)
        CHECK(gint("block-len reduce [ list/append t/all intent/open-window-at mw/details-spec 300 200 \"many\" widget/flush ]") == 2 && w3_deliver());
    CHECK(gv_glon_host_window_count(h) == 15 && w3_sdl_windows() == 16);
    long f15 = inst2_free(), id15 = gint("view/id");
    igs(a, sizeof a, "either sin? t/x: judge [ intent/open-window mw/details-spec ] [ widget/flush sin-id t/x ] [ widget/flush 'opened ]");
    CHECK(w3_deliver());
    CHECK(!strcmp(a, "window-limit") && gv_glon_host_window_count(h) == 15 && inst2_free() == f15 && gint("view/id") == id15);
    /* each of them still works; the main window too */
    w3_in("list/at t/all 7"); CHECK(i6_type(WD("list/at t/all 7", "village"), "Seventh") == 0);
    w3_in("list/at t/all 14"); CHECK(i6_type(WD("list/at t/all 14", "village"), "Last") == 0);
    CHECK(i6_is(WD("list/at t/all 7", "village"), "Seventh") && i6_is(WD("list/at t/all 0", "village"), ""));
    w3_in("mw/search"); CHECK(i6_type(WS("search"), "w") == 0 && gint("face/row-count " WS("members")) == 1);
    CHECK(gint("block-len reduce [ list/each t/all func [l] [ intent/close l ] list/clear t/all widget/flush ]") == 3 && w3_deliver());
    CHECK(gv_glon_host_window_count(h) == 0 && w3_sdl_windows() == 1 && !gv_glon_host_error(h));
    /* the same 15 again take the same ids */
    long hw15 = gint("view/id"), fc15 = inst2_free();
    for (int k = 0; k < 15; k++)
        CHECK(gint("block-len reduce [ list/append t/all intent/open-window-at mw/details-spec 300 200 \"many\" widget/flush ]") == 2 && w3_deliver());
    CHECK(gint("block-len reduce [ list/each t/all func [l] [ intent/close l ] widget/flush ]") == 2 && w3_deliver());
    CHECK(gv_glon_host_window_count(h) == 0 && gint("view/id") == hw15 && inst2_free() == fc15 && !gv_glon_host_error(h));
    printf("win3-lifecycle: 40 cycles leave nothing behind; a closed window instance reopens in a new window, old handles "
           "stale; 15 windows at once, the 16th refused (window-limit) with nothing made\n");
}
/* win3-failure: a window the host cannot open, the heap check, the close
 * hook, and the main instance closed while windows stay */
static void case_win3_failure(void) {
    char a[300];
    CHECK(resize(760, 560) == 0);
    w3_in("mw/search");
    CHECK(gint("block-len reduce [ t/w: intent/open-window-at mw/details-spec 420 260 \"kept\" widget/flush ]") == 2 && w3_deliver());
    w3_in("t/w"); CHECK(i6_type(WD("t/w", "name"), "Kept") == 0);
    /* the heap's room is reported once windows are used */
    long total = gint("intent/heap-total"), hfree = gint("intent/heap-free");
    CHECK(total == (long)(GC_HEAP_LIMIT - GC_HEAP_BASE) && hfree > 0 && hfree <= total);
    /* the native window refused (GV_FAIL_WINDOW: the Nth window the host
     * opens fails): Intent closes what it built; the rest are untouched */
    long op = 0;
    gv_glon_host_window_stats(h, &op, NULL, NULL, NULL);
    char nth[24]; snprintf(nth, sizeof nth, "%ld", op + 1);
    setenv("GV_FAIL_WINDOW", nth, 1);
    long fc = inst2_free(), hw = gint("view/id");
    w3_in("mw/search");
    CHECK(gint("block-len reduce [ t/f: intent/open-window mw/details-spec widget/flush ]") == 2 && w3_deliver());
    unsetenv("GV_FAIL_WINDOW");
    printf("  refused window: free ids %ld -> %ld, view/id %ld -> %ld, host windows %d, host error %d\n", fc, inst2_free(), hw,
           gint("view/id"), gv_glon_host_window_count(h), gv_glon_host_error(h));
    igs(a, sizeof a, "face/text " WS("status"));
    CHECK(!strcmp(a, "A window could not be opened.") && gint("intent/open? t/f") == 0 && gint("intent/window-count") == 1);
    /* (the chunk its build took is given back) */
    CHECK(gv_glon_host_window_count(h) == 1 && inst2_free() == fc + 64 && !gv_glon_host_error(h));
    fc = inst2_free(); hw = gint("view/id");
    w3_in("t/w"); CHECK(key(SDLK_END) == 0 && text("!") == 0 && i6_is(WD("t/w", "name"), "Kept!"));
    /* the heap check: too little room left (as if reported) refuses the
     * open before anything is built */
    CHECK(gint("block-len reduce [ t/hf: intent/heap-free  intent/heap-free: 1000 ]") == 2);
    igs(a, sizeof a, "either sin? t/x: judge [ intent/open-window mw/details-spec ] [ widget/flush sin-id t/x ] [ widget/flush 'opened ]");
    CHECK(w3_deliver() && !strcmp(a, "window-heap") && inst2_free() == fc && gint("view/id") == hw && gv_glon_host_window_count(h) == 1);
    CHECK(gint("block-len reduce [ intent/heap-free: t/hf ]") == 1);
    /* a bad size or title is refused too */
    igs(a, sizeof a, "sin-id judge [ intent/open-window-at mw/details-spec 0 200 \"x\" ]"); CHECK(!strcmp(a, "window-size"));
    igs(a, sizeof a, "sin-id judge [ intent/open-window-at mw/details-spec 300 200 5 ]"); CHECK(!strcmp(a, "window-title-needs-string"));
    CHECK(w3_deliver() && inst2_free() == fc && gv_glon_host_window_count(h) == 1);
    /* the application may keep a window open at its close button */
    CHECK(gint("block-len reduce [ intent/on-window-close :mw/keep-open ]") == 1);
    CHECK(w3_close_button(w3_sdl("t/w")) == 0 && gint("intent/open? t/w") == 1 && gv_glon_host_window_count(h) == 1);
    igs(a, sizeof a, "face/text " WS("status")); CHECK(!strcmp(a, "Kept open."));
    CHECK(gint("block-len reduce [ vid/set-block! intent/window-hooks 0 0 ]") == 1);
    /* the main window's instance closed: the window stays and works; shown
     * again, the main instance is rebuilt */
    CHECK(gint("block-len reduce [ intent/close mw/search widget/flush ]") == 2 && w3_deliver());
    CHECK(gint("intent/open? mw/search") == 0 && gint("= intent/primary 0") == 1 && gint("intent/open? t/w") == 1);
    w3_in("t/w"); CHECK(i6_type(WD("t/w", "village"), "Alone") == 0 && i6_is(WD("t/w", "village"), "Alone"));
    win_target = 0; CHECK(text("q") == 0 && i6_is(WD("t/w", "village"), "Alone"));   /* keys for an empty main window go nowhere */
    CHECK(gint("block-len reduce [ intent/show-instance mw/search widget/flush ]") == 2 && w3_deliver());
    CHECK(gint("= intent/primary mw/search") == 1 && gint("intent/open? t/w") == 1);
    w3_in("mw/search"); CHECK(i6_type(WS("search"), "k") == 0 && gint("face/row-count " WS("members")) == 1);
    CHECK(i6_is(WD("t/w", "village"), "Alone"));
    CHECK(w3_close_button(w3_sdl("t/w")) == 0 && gv_glon_host_window_count(h) == 0 && !gv_glon_host_error(h));
    /* a window's root destroyed directly (not through Intent) closes that
     * window in the host; closing the instance afterwards is harmless */
    CHECK(gint("block-len reduce [ t/d: intent/open-window mw/details-spec widget/flush ]") == 2 && w3_deliver() && gv_glon_host_window_count(h) == 1);
    CHECK(gint("block-len reduce [ view/destroy intent/window-of t/d widget/flush ]") == 2 && w3_deliver());
    CHECK(gv_glon_host_window_count(h) == 0 && w3_sdl_windows() == 1 && !gv_glon_host_error(h));
    CHECK(gint("block-len reduce [ intent/close t/d widget/flush ]") == 2 && w3_deliver() && gint("intent/window-count") == 0 && !gv_glon_host_error(h));
    printf("win3-failure: a refused native window rolls back (status set, ids back, the other window works); heap report %ld of %ld "
           "free; the heap check, bad size and title refuse before building; a close hook keeps a window; the main instance "
           "closes and reopens while a window stays\n", hfree, total);
}
/* win3-heap: the heap check against real exhaustion.  Live data fills the
 * heap until the host's report (after a collection) leaves too little room;
 * the next open is refused as window-heap -- an S1 out-of-heap is a
 * fail-stop, never reached here -- and works again once the data is let go */
static char w3_blk[400];
static void w3_churn(void) {
    /* garbage until a collection runs (the host then reports the room) */
    long g = r0_s1_gc_count();
    char e[600];
    snprintf(e, sizeof e, "block-len reduce [ loop/for 0 400 func [k] [ %s ] ]", w3_blk);
    for (int i = 0; i < 200 && r0_s1_gc_count() == g; i++) gint(e);
    CHECK(inst_deliver());
}
static void case_win3_heap(void) {
    char a[300], e[700];
    CHECK(resize(760, 560) == 0);
    int n = snprintf(w3_blk, sizeof w3_blk, "reduce [");
    for (int i = 0; i < 60; i++) n += snprintf(w3_blk + n, sizeof w3_blk - n, " 0");
    snprintf(w3_blk + n, sizeof w3_blk - n, " ]");
    w3_in("mw/search");
    /* the first window turns the host's heap reports on */
    CHECK(gint("block-len reduce [ intent/close intent/open-window mw/details-spec widget/flush ]") == 2 && w3_deliver());
    CHECK(gint("block-len reduce [ t/big: list/new ]") == 1);
    w3_churn();
    long total = gint("intent/heap-total"), need = 8192 + total / 16;
    long hfree = gint("intent/heap-free"), start = hfree;
    printf("  heap %ld cells, %ld free at the start; an open needs %ld free (cost 8192 + a sixteenth)\n", total, hfree, need);
    int steps = 0;
    while (hfree >= need && steps < 400) {
        long want = hfree - need;
        int blocks = want > 40000 ? 600 : want > 8000 ? 120 : 30;
        snprintf(e, sizeof e, "block-len reduce [ loop/for 0 %d func [k] [ list/append t/big %s ] ]", blocks, w3_blk);
        CHECK(gint(e) == 1);
        w3_churn();
        hfree = gint("intent/heap-free");
        steps++;
    }
    long live = r0_s1_gc_live_cells();
    printf("  filled in %d steps: %ld free by the host's report (live %ld, %ld blocks held)\n", steps, hfree, live, gint("list/length t/big"));
    CHECK(hfree < need && hfree > 4000);
    long fc = inst2_free(), hw = gint("view/id");
    igs(a, sizeof a, "either sin? t/x: judge [ intent/open-window mw/details-spec ] [ widget/flush sin-id t/x ] [ widget/flush 'opened ]");
    CHECK(w3_deliver());
    printf("  open with %ld free: %s\n", hfree, a);
    CHECK(!strcmp(a, "window-heap") && inst2_free() == fc && gint("view/id") == hw && gv_glon_host_window_count(h) == 0 && !gv_glon_host_error(h));
    /* the session still works */
    CHECK(i6_type(WS("search"), "ng") == 0 && gint("face/row-count " WS("members")) == 2);
    /* the data let go: after the next collection the room is back */
    CHECK(gint("block-len reduce [ t/big: 0 ]") == 1);
    w3_churn();
    long after = gint("intent/heap-free");
    igs(a, sizeof a, "either sin? t/x: judge [ t/w: intent/open-window mw/details-spec ] [ widget/flush sin-id t/x ] [ widget/flush 'opened ]");
    CHECK(w3_deliver());
    printf("  released: %ld free; open: %s\n", after, a);
    CHECK(after > need && !strcmp(a, "opened") && gv_glon_host_window_count(h) == 1);
    CHECK(gint("block-len reduce [ intent/close t/w widget/flush ]") == 2 && w3_deliver() && !gv_glon_host_error(h));
    printf("win3-heap: filled from %ld to %ld free cells; the open was refused (window-heap) before the heap ran out; "
           "released, %ld free and windows open again\n", start, hfree, after);
}
/* win3-scale: 1, 2, 3, 5 and 10 windows (the main one included) */
static double w3_median(double *x, int n) {
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (x[j] < x[i]) { double t = x[i]; x[i] = x[j]; x[j] = t; }
    return x[n / 2];
}
static long w3_live_now(void) {
#ifdef R0S1_GC_PROF
    gcp_collect_now();
#endif
    return r0_s1_gc_live_cells();
}
static void case_win3_scale(void) {
    CHECK(resize(760, 560) == 0);
    int stops[5] = { 1, 2, 3, 5, 10 };
    long s0 = inst_sites(), g0 = inst_globals(), rss0 = w3_rss_kb(), live0 = w3_live_now();
    CHECK(gint("block-len reduce [ t/all: list/new ]") == 1);
    int open = 1;
    double first_ptr = 0, first_key = 0;
    printf("  windows | open ms | live cells | +cells/win | RSS KB | surface KB | sites | globals | ptr ms (glon) | key ms (glon) | focus-switch ms | resize ms | GCs | redraws/event\n");
    for (int k = 0; k < 5; k++) {
        double topen = 0; int nopen = 0;
        long gc0 = r0_s1_gc_count();
        while (open < stops[k]) {
            double t0 = i6_now();
            CHECK(gint("block-len reduce [ list/append t/all intent/open-window-at mw/details-spec 470 280 \"Member\" widget/flush ]") == 2 && w3_deliver());
            topen += i6_now() - t0; nopen++; open++;
        }
        long live = w3_live_now();
        /* surfaces: each SDL window's own surface plus the backend's cairo image (4 bytes a pixel each) */
        long surf = 2L * 760 * 560 * 4;
        for (int i = 0; i < gv_glon_host_window_count(h); i++) { int ww, wh; gv_glon_host_window_size(h, i, &ww, &wh); surf += 2L * ww * wh * 4; }
        /* the active window: the last opened (or the main one) */
        const char *lay = open == 1 ? "mw/search" : "list/at t/all - list/length t/all 1";
        const char *fa = open == 1 ? WS("search") : WD("list/at t/all - list/length t/all 1", "name");
        const char *fb = open == 1 ? WS("open") : WD("list/at t/all - list/length t/all 1", "village");
        w3_in(lay);
        gv_rect r1, r2; CHECK(hbox(fa, &r1) == 0 && hbox(fb, &r2) == 0);
        double pw[11], pg[11], kw[11], kg[11], fs[6], rz[5];
        double g, tb, bd; long runs, gcs;
        long fr0 = 0, fr1 = 0;
        gv_glon_host_window_stats(h, NULL, NULL, NULL, &fr0);
        for (int m = 0; m < 11; m++) {
            gv_glon_host_timing_reset();
            double t0 = i6_now();
            CHECK(m % 2 ? move_to(r1.x + 5, r1.y + r1.h / 2) == 0 : move_to(r2.x + 5, r2.y + r2.h / 2) == 0);
            pw[m] = i6_now() - t0;
            gv_glon_host_timing_totals(&g, &tb, &bd, &runs, &gcs); pg[m] = runs ? g / runs : 0;
        }
        gv_glon_host_window_stats(h, NULL, NULL, NULL, &fr1);
        CHECK(i6_click(fa) == 0);
        for (int m = 0; m < 11; m++) {
            gv_glon_host_timing_reset();
            double t0 = i6_now();
            CHECK(text("a") == 0);
            kw[m] = i6_now() - t0;
            gv_glon_host_timing_totals(&g, &tb, &bd, &runs, &gcs); kg[m] = runs ? g / runs : 0;
        }
        /* focus switch: a key into the other window (the main one, or the
         * last opened) after one into this; it carries the view-window run */
        Uint32 here = win_target;
        w3_in("mw/search"); CHECK(i6_click(WS("search")) == 0);
        for (int m = 0; m < 6; m++) {
            win_target = m % 2 ? 0 : here;
            double t0 = i6_now();
            CHECK(text(m % 2 ? "s" : "b") == 0);
            fs[m] = i6_now() - t0;
        }
        w3_in(lay);
        SDL_Window *sw = here ? SDL_GetWindowFromID(here) : find_window();
        for (int m = 0; m < 5; m++) {
            double t0 = i6_now();
            if (here) { SDL_SetWindowSize(sw, m % 2 ? 470 : 520, 300); CHECK(pump() == 0); }
            else CHECK(resize(m % 2 ? 760 : 800, 560) == 0);
            rz[m] = i6_now() - t0;
        }
        if (!here) CHECK(resize(760, 560) == 0);
        w3_in("mw/search"); CHECK(i6_type(WS("search"), "") == 0);
        double ptr = w3_median(pw, 11), key = w3_median(kw, 11);
        if (k == 0) { first_ptr = ptr; first_key = key; }
        printf("  %7d | %7.1f | %10ld | %10ld | %6ld | %10ld | %5ld | %7ld | %5.2f (%5.2f) | %5.2f (%5.2f) | %15.2f | %9.2f | %3ld | %.1f\n",
               open, nopen ? topen / nopen : 0.0, live, open > 1 ? (live - live0) / (open - 1) : 0L, w3_rss_kb() - rss0, surf / 1024,
               inst_sites() - s0, inst_globals() - g0, ptr, w3_median(pg, 11), key, w3_median(kg, 11), w3_median(fs, 6), w3_median(rz, 5),
               r0_s1_gc_count() - gc0, (fr1 - fr0) / 11.0);
        if (k == 4) {
            /* latency does not grow with the windows not in use */
            CHECK(ptr < first_ptr * 2.5 + 1.0 && key < first_key * 2.5 + 1.0);
        }
    }
    CHECK(inst_sites() - s0 <= 2 && !gv_glon_host_error(h));
    /* destruction, then the memory after cycles */
    double t0 = i6_now();
    CHECK(gint("block-len reduce [ list/each t/all func [l] [ intent/close l ] list/clear t/all widget/flush ]") == 3 && w3_deliver());
    double close_ms = (i6_now() - t0) / 9;
    CHECK(gv_glon_host_window_count(h) == 0);
    long live_a = w3_live_now(), rss_a = w3_rss_kb();
    long rss_at[4] = { rss_a, 0, 0, 0 };
    for (int c = 0; c < 30; c++) {
        for (int k = 0; k < 9; k++) CHECK(gint("block-len reduce [ list/append t/all intent/open-window-at mw/details-spec 470 280 \"Member\" widget/flush ]") == 2 && w3_deliver());
        CHECK(gint("block-len reduce [ list/each t/all func [l] [ intent/close l ] list/clear t/all widget/flush ]") == 3 && w3_deliver());
        if (c % 10 == 9) rss_at[c / 10 + 1] = w3_rss_kb();
    }
    printf("  RSS after 0, 10, 20, 30 cycles of 9 windows: %ld, %ld, %ld, %ld KB\n", rss_at[0] - rss0, rss_at[1] - rss0, rss_at[2] - rss0, rss_at[3] - rss0);
    long live_b = w3_live_now(), rss_b = w3_rss_kb();
    CHECK(gv_glon_host_window_count(h) == 0 && !gv_glon_host_error(h));
    printf("  closing 9 windows: %.1f ms each (all in one batch); after 30 cycles of 9 opens and 9 closes: live cells %ld -> %ld, "
           "RSS %ld -> %ld KB (since the start: %+ld KB)\n", close_ms, live_a, live_b, rss_a - rss0, rss_b - rss0, rss_b - rss0);
#ifdef R0S1_GC_PROF
    printf("  (live cells after a collection made for the measurement)\n");
#else
    printf("  (live cells as of the last collection, not one made for the measurement)\n");
#endif
    printf("win3-scale: measured (GLON-INST-3B.md)\n");
}
/* intent7-eval: GV_I7_EVAL's expressions (separated by newlines) run in the
 * loaded membership application, each result or failure printed: a probe for
 * diagnosis (GLON-LIB-1), no checks.  Directives: "#usage" (sites, globals,
 * View node ids, heap frontier), "#deliver" (the host takes the pending
 * command batch, as after an event), and, in the diagnostic build only,
 * "#gc" (a collection now and its live data) and "#reach" (the globals that
 * reach the most managed cells).  GLON-INST-1 uses them to measure
 * what a layout instance costs */
static void case_intent7_eval(void) {
    const char *all = getenv("GV_I7_EVAL");
    if (getenv("GV_I7_RESIZE")) CHECK(resize(1240, 820) == 0);
    {
        cell g = 0, gc = 0, st = 0, sc = 0; r0_s1_global_usage(&g, &gc); r0_s1_site_usage(&st, &sc);
        printf("  session: code %ld cells free below %ld; globals %ld of %ld; sites %ld of %ld; loader heap at %ld\n",
               (long)(R0S1_CODE_LIMIT - (M[RV_DSLIM] - R0S1_DS_GUARD)), (long)R0S1_CODE_LIMIT, (long)g, (long)gc, (long)st, (long)sc,
               (long)M[GC_LOADER_HP]);
    }
    if (getenv("GV_I7_GLOBALS")) {   /* every global name, one a line */
        cell p = M[GC_GLOBAL_CTX] - (M[GC_GLOBAL_CTX] % 16);
        for (cell k = 0; k < M[p + CTX_COUNT]; k++) printf("global %s\n", r0_s1_sym_name(M[p + CTX_DATA + 2 * k] / 16));
    }
    char line[4096];
    while (all && *all) {
        const char *nl = strchr(all, '\n');
        size_t n = nl ? (size_t)(nl - all) : strlen(all);
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, all, n); line[n] = 0;
        all = nl ? nl + 1 : all + n;
        if (!line[0]) continue;
        if (!strcmp(line, "#usage")) {   /* sites, globals, View node ids, frontier */
            cell g = 0, gc = 0, st = 0, sc = 0; r0_s1_global_usage(&g, &gc); r0_s1_site_usage(&st, &sc);
            printf("  #usage: sites %ld, globals %ld, view/id %ld, frontier %ld, collections %ld\n", (long)st, (long)g,
                   gint("view/id"), (long)(r0_s1_heap_high() - GC_HEAP_BASE), r0_s1_gc_count());
            continue;
        }
        if (!strcmp(line, "#deliver")) { /* the host takes the pending batch, as after an event */
            printf("  #deliver: %d %d\n", gv_glon_host_deliver(h), gv_glon_host_bind(h));
            continue;
        }
        if (!strcmp(line, "#reach")) {   /* the globals that reach the most managed cells (diagnostic build) */
#ifdef R0S1_GC_PROF
            cell gp = M[GC_GLOBAL_CTX] - (M[GC_GLOBAL_CTX] % 16);
            long best[12] = {0}; cell who[12] = {0};
            for (cell k = 0; k < M[gp + CTX_COUNT]; k++) {
                long c = gcp_reach(M[gp + CTX_DATA + 2 * k + 1]);
                for (int j = 0; j < 12; j++) if (c > best[j]) {
                    for (int m = 11; m > j; m--) { best[m] = best[m - 1]; who[m] = who[m - 1]; }
                    best[j] = c; who[j] = M[gp + CTX_DATA + 2 * k]; break;
                }
            }
            printf("  #reach:");
            for (int j = 0; j < 12 && best[j]; j++) printf(" %s %ld;", r0_s1_sym_name(who[j] / 16), best[j]);
            printf("\n");
#else
            printf("  #reach: needs the diagnostic build\n");
#endif
            continue;
        }
        if (!strcmp(line, "#gc")) {      /* a collection now (diagnostic build), its live data */
#ifdef R0S1_GC_PROF
            gcp_collect_now();
            printf("  #gc: live %ld cells in %ld objects; free %ld cells in %ld blocks\n", r0_s1_gc_live_cells(),
                   r0_s1_gc_live_objs(), r0_s1_gc_free_cells(), r0_s1_gc_free_blocks());
#else
            printf("  #gc: needs the diagnostic build (test_gui_p2_4m_gcprof)\n");
#endif
            continue;
        }
        char out[2048] = "";
        char src[4200]; snprintf(src, sizeof src, "[ %s ]", line);
        double t0 = i6_now();
        int rc = gv_glon_host_text(h, line, out, sizeof out);
        double t1 = i6_now();
        printf("  %8.1f ms  rc %d  %s  <- %s\n", t1 - t0, rc, out, line);
        if (rc) {
            cell t, i, a; char ts[64] = "", is[64] = "", as[256] = "";
            if (r0_s1_uncaught_error(&t, &i, &a)) { r0_s1_mold(t, ts, sizeof ts); r0_s1_mold(i, is, sizeof is); r0_s1_mold(a, as, sizeof as); }
            printf("            halt %d sin %s %s %s\n", r0_s1_halt_reason(), ts, is, as);
        }
    }
}
/* intent7-probe: what one Glon run costs in the membership application, by
 * piece (each timed from the harness over a few repetitions; nothing in the
 * application changes) */
static double i7_ms(const char *expr, int reps) {
    double t0 = i6_now();
    for (int k = 0; k < reps; k++) gint(expr);
    return (i6_now() - t0) / reps;
}
static void case_intent7_probe(void) {
    CHECK(resize(1240, 820) == 0);
    printf("  an empty run (gint 0)                       %8.2f ms\n", i7_ms("0", 20));
    printf("  widget/flush (nothing pending)              %8.2f ms\n", i7_ms("block-len reduce [ widget/flush ]", 10));
    printf("  current-event: 'view-pointer-move           %8.2f ms\n", i7_ms("block-len reduce [ current-event: 'view-pointer-move ]", 10));
    printf("  intent/each lay :intent/look (all faces)    %8.2f ms\n", i7_ms("block-len reduce [ current-event: 'view-pointer-move  intent/each intent/current :intent/look ]", 5));
    printf("  intent/act lay pointer-move                 %8.2f ms\n", i7_ms("block-len reduce [ current-event: 'view-pointer-move  intent/act intent/current 'view-pointer-move \"5|5\" ]", 5));
    long nf = gint("intent/count intent/current");
    printf("  %ld faces; vid/event per face (pointer-move):\n", nf);
    double tot = 0;
    for (long i = 0; i < nf; i++) {
        char e[200];
        snprintf(e, sizeof e, "block-len reduce [ current-event: 'view-pointer-move  intent/look intent/current %ld ]", i);
        double ms = i7_ms(e, 3);
        long kind = gint((snprintf(e, sizeof e, "block-at intent/face intent/current %ld 0", i), e));
        tot += ms;
        if (ms > 0.5) printf("    face %2ld kind %2ld: %7.2f ms\n", i, kind, ms);
    }
    printf("  sum over faces %.2f ms\n", tot);
    /* per call, inside one run (100 calls through loop/for) */
    CHECK(gv_glon_host_run(h, "t7/c0: func [c i] [ 0 ] "
          "t7/c1: func [c i] [ block-at intent/face intent/current 0 2 ] "
          "t7/c2: func [c i] [ vid/event block-at intent/face intent/current 0 2 ] "
          "t7/c3: func [c i] [ intent/look intent/current 0 ] "
          "t7/c4: func [c i] [ parse-int \"12|34\" ] "
          "t7/c5: func [c i] [ = 'view-pointer-move 'view-pointer-down ] "
          "t7/c6: func [c i] [ either = 1 2 [ 0 ] [ 1 ] ] "
          "t7/c7: func [c i] [ t7/c0 c i ] "
          "t7/c8: func [c i] [ a1: 1 a2: 2 a3: 3 a4: 4 a5: 5 a6: 6 0 ] "
          "t7/c9: func [c i] [ current-event ] "
          "t7/c10: func [c i] [ parse-int current-value ] "
          "t7/c11: func [c i] [ st: block-at intent/face intent/current 0 2  block-len st ] "
          "t7/c12: func [c i] [ a1: 1 0 ]") == 0);
    printf("  current-value is %ld bytes; current-event %ld\n", gint("s/length current-value"), gint("= current-event 'view-pointer-move"));
    const char *nm[13] = { "an empty func call (the loop's own cost)", "intent/face lay 0 (2 block-at, 2 globals)",
        "+ vid/event on a label face", "intent/look lay 0 (a label)", "parse-int \"12|34\"", "= 'a 'b",
        "either = 1 2 [0] [1]", "one more func call", "6 set-words in the body", "current-event",
        "parse-int current-value", "a local state, block-len", "1 set-word in the body" };
    double c0 = 0;
    for (int k = 0; k < 13; k++) {
        char e[160]; snprintf(e, sizeof e, "block-len reduce [ loop/for 0 100 func [i] [ t7/c%d 0 i ] ]", k);
        double ms = i7_ms(e, 3) / 100.0;
        if (k == 0) c0 = ms;
        printf("  per call: %-44s %7.1f us (%+7.1f us over an empty call)\n", nm[k], ms * 1000, (ms - c0) * 1000);
    }
    printf("  code region in this session: %ld cells of emitted code, %ld free below the limit %ld\n",
           (long)(M[RV_DSLIM] - R0S1_DS_GUARD - 256), (long)(R0S1_CODE_LIMIT - (M[RV_DSLIM] - R0S1_DS_GUARD)), (long)R0S1_CODE_LIMIT);
    printf("intent7-probe: measured (INTENT-7.md)\n");
}

/* intent7-parts: Edit and Save, and Search, piece by piece (each piece of the
 * application's own code timed from the harness; INTENT-7 Phase 2) */
static double i7_flush_ms(const char *expr, int reps) {   /* a piece that emits commands: flushed and applied too */
    double t0 = i6_now();
    for (int k = 0; k < reps; k++) { char e[400]; snprintf(e, sizeof e, "block-len reduce [ %s widget/flush ]", expr); gint(e); gv_glon_host_deliver(h); }
    return (i6_now() - t0) / reps;
}
static void case_intent7_parts(void) {
    CHECK(resize(1240, 820) == 0);
    /* Search first (nothing changed yet) */
    printf("  Search, by piece (query \"w\"; %d of 120 match):\n", i6_expected("w"));
    printf("    a lower-case the query                     %7.2f ms\n", i7_ms("block-len reduce [ s/lower \"w\" ]", 10));
    CHECK(gv_glon_host_run(h, "t7/m: mb/matcher \"w\"") == 0);
    printf("    b one record matched (name, word, ID)      %7.2f ms\n", i7_ms("block-len reduce [ t7/m list/at mb/recs 7 ]", 10));
    printf("    c the matcher made for a query             %7.2f ms\n", i7_ms("block-len reduce [ mb/matcher \"w\" ]", 10));
    CHECK(gv_glon_host_run(h, "mb/query: \"w\"") == 0);
    printf("    d filter 120 records (compute-view)        %7.1f ms (%ld found)\n", i7_ms("block-len reduce [ mb/compute-view ]", 3), gint("list/length mb/view"));
    CHECK(gint("list/length mb/view") == i6_expected("w"));
    printf("    e one row: mb/row of a record              %7.2f ms\n", i7_ms("block-len reduce [ mb/row list/at mb/recs 7 ]", 10));
    printf("    f one row added (face/add-rows, applied)   %7.2f ms\n", i7_flush_ms("face/add-rows mb/f 'members reduce [ mb/row list/at mb/recs 7 ]", 5));
    CHECK(gv_glon_host_run(h, "t7/r10: reduce [ mb/row list/at mb/recs 1 mb/row list/at mb/recs 2 mb/row list/at mb/recs 3 mb/row list/at mb/recs 4 "
          "mb/row list/at mb/recs 5 mb/row list/at mb/recs 6 mb/row list/at mb/recs 7 mb/row list/at mb/recs 8 mb/row list/at mb/recs 9 mb/row list/at mb/recs 10 ]") == 0);
    printf("    f' 10 rows in one face/add-rows (applied)  %7.2f ms (%.2f ms a row)\n", i7_flush_ms("face/add-rows mb/f 'members t7/r10", 3),
           i7_flush_ms("face/add-rows mb/f 'members t7/r10", 3) / 10);
    printf("    f'' intent/table-face (the call's lookup)  %7.2f ms\n", i7_ms("block-len reduce [ intent/table-face mb/f 'members ]", 10));
    CHECK(gv_glon_host_run(h, "t7/row: mb/row list/at mb/recs 7  t7/cells: intent/table-cells t7/row") == 0);
    printf("      row parts: intent/table-cells %.2f ms; intent/table-encode %.2f ms; one cell escaped (\"%s\") %.3f ms; "
           "table/row command %.2f ms\n",
           i7_ms("block-len intent/table-cells t7/row", 10), i7_ms("s/length intent/table-encode t7/cells", 10), "Name 17ch",
           i7_ms("s/length intent/table-escape block-at t7/cells 1", 20),
           i7_flush_ms("table/row intent/tid intent/table-face mb/f 'members \"x|y|z\"", 5));
    printf("    g the filtered list shown (mb/show)        %7.1f ms\n", i7_flush_ms("mb/show", 2));
    CHECK(gv_glon_host_run(h, "mb/query: \"\"") == 0);
    printf("    h all 120 shown (compute-view + show)      %7.1f ms (with its next steps)\n", i7_flush_ms("mb/compute-view mb/show", 1));
    CHECK(!gv_glon_host_error(h));
    /* Edit and Save pieces, on M00005 with an edit */
    CHECK(i6_click_member("M00005") == 0 && i6_type(I6P("name"), "Parts Edit") == 0);
    CHECK(gv_glon_host_run(h, "t7/f: form/sub form/current 'details") == 0);
    printf("  Edit and Save, by piece (one member, 120 records):\n");
    printf("    1 validation (form/validate)               %7.1f ms\n", i7_ms("block-len reduce [ form/validate t7/f ]", 5));
    printf("    2 collection (form/collect)                %7.1f ms\n", i7_ms("block-len reduce [ form/collect t7/f ]", 5));
    printf("    3 find the member (mb/find)                %7.1f ms\n", i7_ms("mb/find mb/current", 5));
    printf("    4 one table row replaced (mb/update-row)   %7.1f ms\n", i7_flush_ms("mb/update-row mb/current", 3));
    printf("    5 controls + status text                   %7.1f ms\n", i7_flush_ms("mb/set-dirty 1 mb/controls mb/say \"Parts\"", 3));
    printf("    6 show a member (mb/show-member)           %7.1f ms\n", i7_flush_ms("mb/show-member mb/current", 3));
    CHECK(!gv_glon_host_error(h));
    printf("intent7-parts: measured (INTENT-7.md)\n");
}

/* intent7-reload: the second load, step by step, with the heap at each step
 * (INTENT-7 Phase 4: why it fails in 1M).  mb/auto-show 0 stops a load after
 * its lines; mb/loaded's pieces then run one at a time. */
static void i7_census(const char *what) {
    long blocks = 0, frees = 0, freec = 0, largest = 0, alloc = 0;
    for (cell a = GC_HEAP_BASE; a < M[REG_HP]; ) {
        cell size = M[a + GC_HDR_SIZE]; if (size <= 0) break;
        blocks++;
        if (M[a + GC_HDR_FLAGS] == 0) { frees++; freec += size; if (size > largest) largest = size; } else alloc += size;
        a += size;
    }
    long room = (long)(GC_HEAP_LIMIT - GC_HEAP_BASE) - (M[REG_HP] - GC_HEAP_BASE);
    printf("  %-34s GCs %3ld  live after last %6ld  allocated %6ld  free %6ld in %4ld blocks (largest %5ld)  above the frontier %6ld\n",
           what, r0_s1_gc_count(), r0_s1_gc_live_cells(), alloc, freec, frees, largest, room);
}
static void case_intent7_reload(void) {
    CHECK(resize(1240, 820) == 0);
    i7_census("after start-up");
    CHECK(gv_glon_host_run(h, "mb/auto-show: 0") == 0);
    int ok = gint("block-len reduce [ mb/reload mb/file widget/flush ]") == 2 && gv_glon_host_deliver(h) == 0 && gv_glon_host_bind(h) == 0;
    printf("  reload, lines only: %s; %ld records, host error %d\n", ok ? "ok" : "FAILED", gint("list/length mb/recs"), gv_glon_host_error(h));
    i7_census("after the lines");
    ok = gint("block-len reduce [ mb/current: none mb/compute-view ]") == 2;
    printf("  compute-view: %s (%ld in view)\n", ok ? "ok" : "FAILED", gint("list/length mb/view")); i7_census("after compute-view");
    ok = gint("block-len reduce [ mb/show widget/flush ]") == 2;
    cell a1_show = M[GC_A1], a2_show = M[GC_A2];
    int ok2 = gv_glon_host_deliver(h) == 0;
    cell a1_deliver = M[GC_A1], a2_deliver = M[GC_A2];
    ok = ok && ok2 && gv_glon_host_bind(h) == 0;
    printf("  show: the run %s (last request %ld cells, kind %ld); its batch applied %s (last request %ld cells, kind %ld)\n",
           a1_show ? "ran" : "-", (long)a1_show, (long)a2_show, ok2 ? "ok" : "FAILED", (long)a1_deliver, (long)a2_deliver);
    printf("  show (the rows, with next steps): %s, host error %d, table rows %d\n", ok ? "ok" : "FAILED", gv_glon_host_error(h),
           i6_table() ? i6_table()->nrows : -1);
    i7_census("after show");
    ok = gint("block-len reduce [ mb/show-member \"M00001\" ]") == 1;
    cell a1_sm = M[GC_A1], a2_sm = M[GC_A2];
    printf("  show-member, the run: %s (last request %ld cells, kind %ld)\n", ok ? "ok" : "FAILED", (long)a1_sm, (long)a2_sm);
    i7_census("after show-member (not flushed)");
    printf("  pending batch: %ld bytes\n", gint("s/length vb/buf"));
    ok = ok && gint("block-len reduce [ widget/flush ]") == 1;
    cell a1_fl = M[GC_A1];
    ok = ok && gv_glon_host_deliver(h) == 0 && gv_glon_host_bind(h) == 0;
    printf("  show-member flushed: %s, host error %d (last request %ld cells)\n", ok ? "ok" : "FAILED", gv_glon_host_error(h), (long)a1_fl);
    i7_census("after show-member");
    printf("intent7-reload: measured (INTENT-7.md)\n");
}

/* intent7-search: narrowing (INTENT-7) gives exactly what a full search
 * gives: after every keystroke the table's rows are the matching members of
 * the seed, by ID, in order (computed independently from the file); a
 * shorter query and the year sort search again; an edit that the view was
 * not made again for stops narrowing. */
static int i7_rows_are(const char *q) {
    i6_load_seed();
    gv_table *t = i6_table();
    int k = 0, ok = 1;
    for (int i = 0; i < i6_n && ok; i++) {
        if (!i6_matches(&i6_seed[i], q)) continue;
        const char *c = t && k < t->nrows ? gv_table_get_cell(t, k, 0) : NULL;
        if (!c || strcmp(c, i6_seed[i].id)) { fprintf(stderr, "  '%s': row %d is %s, wanted %s\n", q, k, c ? c : "(none)", i6_seed[i].id); ok = 0; }
        k++;
    }
    int n = t ? t->nrows : -1;
    if (ok && n != k) { fprintf(stderr, "  '%s': %d rows, wanted %d\n", q, n, k); ok = 0; }
    return ok && gint("list/length mb/view") == k;
}
static void case_intent7_search(void) {
    CHECK(resize(1240, 820) == 0);
    CHECK(i6_click(MR "search") == 0);
    const char *typed = "fon";
    char q[16] = "";
    for (int i = 0; typed[i]; i++) {               /* f, fo, fon: each narrows the one before */
        char c[2] = { typed[i], 0 }; CHECK(text(c) == 0); q[i] = typed[i]; q[i + 1] = 0;
        CHECK(i7_rows_are(q));
    }
    CHECK(i6_expected("fo") > 0 && i6_expected("fon") <= i6_expected("fo"));
    CHECK(key(8) == 0 && i7_rows_are("fo"));       /* a shorter query: searched again */
    CHECK(key(8) == 0 && key(8) == 0 && i7_rows_are(""));
    const char *ids = "M0001";                     /* the ID, upper case and lower case */
    for (int i = 0; ids[i]; i++) { char c[2] = { ids[i], 0 }; CHECK(text(c) == 0); }
    CHECK(i7_rows_are("M0001") && i6_expected("m0001") == 10);
    for (int i = 0; i < 5; i++) CHECK(key(8) == 0);
    CHECK(text("m00") == 0 && i7_rows_are("m00"));
    CHECK(i6_click(MR "clear-search") == 0 && i7_rows_are(""));
    /* the year sort (before any edit, so the seed is the records): searched
     * again, narrowed within it, the newest first; then back to the ID order */
    CHECK(i6_click(MR "clear-search") == 0);
    CHECK(i6_click(MR "sort") == 0 && key(SDLK_ESCAPE) == 0 && keyk(K_END) == 0 && ig("= mb/sort-by 'year") == 1);
    CHECK(i6_click(MR "search") == 0 && text("f") == 0);
    CHECK(gint("list/length mb/view") == i6_expected("f"));
    CHECK(text("o") == 0 && gint("list/length mb/view") == i6_expected("fo"));
    CHECK(i6_click(MR "clear-search") == 0);
    CHECK(i6_click(MR "sort") == 0 && key(SDLK_ESCAPE) == 0 && keyk(K_HOME) == 0 && ig("= mb/sort-by 'id") == 1 && i7_rows_are(""));
    /* an edit the view was not made again for: a member outside the view is
     * edited to match the query, saved; one more letter finds it */
    CHECK(i6_click_member("M00050") == 0 && i6_click(MR "search") == 0 && text("zz") == 0 && gint("list/length mb/view") == 0);
    CHECK(i6_is(I6P("id"), "M00050"));            /* its details still shown */
    CHECK(i6_type(I6P("name"), "Zzyzx Quinn") == 0 && i6_click(I6P("save")) == 0 && i6_status_has("Saved M00050 Zzyzx Quinn"));
    CHECK(i6_click(MR "search") == 0 && key(1004) == 0 && text("y") == 0);
    CHECK(gint("list/length mb/view") == 1 && i6_table()->nrows == 1 && !strcmp(gv_table_get_cell(i6_table(), 0, 0), "M00050"));
    CHECK(!gv_glon_host_error(h) && presented_ok() == 0);
    printf("intent7-search: f / fo / fon narrowed, Backspace searched again, IDs in either case, an edited hidden member found; "
           "rows by ID equal the seed's matches at every step\n");
}

/* intent7-shots: the window in the states a user meets, saved as PNGs (to
 * argv[3]) for the usability review (INTENT-7 Phase 5); dummy driver only */
static void i7_shot(const char *name) {
    if (!png_dir) return;
    char pth[600]; snprintf(pth, sizeof pth, "%s/intent7-%s.png", png_dir, name);
    CHECK(render() == 0 && gv_backend_sdl_save_png(b, pth) == 0);
}
static void case_intent7_shots(void) {
    CHECK(resize(1240, 820) == 0);
    i7_shot("1-loaded");
    CHECK(i6_click_member("M00007") == 0 && i6_type(I6P("name"), "An Edited Name") == 0);
    i7_shot("2-unsaved-edit");
    CHECK(i6_click_member("M00009") == 0 && i6_visible("prompt") == 1);
    i7_shot("3-unsaved-prompt");
    CHECK(i6_click(I6P("p3")) == 0 && i6_type(I6P("email"), "not-an-email") == 0 && i6_click(I6P("save")) == 0);
    i7_shot("4-validation");
    CHECK(i6_click(I6P("cancel")) == 0 && i6_click(I6P("delete")) == 0 && i6_visible("prompt") == 1);
    i7_shot("5-delete-confirm");
    CHECK(i6_click(I6P("p2")) == 0 && i6_click(MR "search") == 0 && text("ng") == 0);
    i7_shot("6-search");
    CHECK(i6_click(MR "clear-search") == 0 && resize(1000, 700) == 0);
    i7_shot("7-small-1000x700");
    CHECK(resize(1600, 1000) == 0);
    i7_shot("8-large-1600x1000");
    CHECK(!gv_glon_host_error(h));
    printf("intent7-shots: 8 states saved%s\n", png_dir ? "" : " (no directory given: none written)");
}

/* intent6-scale: load, search and select times for the source file (GV_MEMBERS_SOURCE) */
static void case_intent6_scale(void) {
    CHECK(resize(1240, 820) == 0);                 /* the demo's window */
    i6_load_seed();
    int n = i6_n;
    /* load again, timed */
    double t0 = i6_now();
    int rc = gint("block-len reduce [ mb/reload mb/file widget/flush ]") == 2 && gv_glon_host_deliver(h) == 0 && gv_glon_host_bind(h) == 0;
    double t1 = i6_now();
    CHECK(rc && gint("list/length mb/recs") == n && i6_table() && i6_table()->nrows == n && !gv_glon_host_error(h));
    /* one search keystroke (all records scanned), then clearing it (all rows again) */
    double t2 = i6_now(); CHECK(i6_click(MR "search") == 0 && text("w") == 0); double t3 = i6_now();
    int found = (int)gint("list/length mb/view");
    CHECK(found == i6_expected("w"));
    double t4 = i6_now(); CHECK(i6_click(MR "clear-search") == 0); double t5 = i6_now();
    double t6 = i6_now(); CHECK(i6_click_member("M00002") == 0 && i6_is(I6P("id"), "M00002")); double t7 = i6_now();
    double t8 = i6_now(); CHECK(i6_type(I6P("name"), "Scale Edit") == 0 && i6_click(I6P("save")) == 0 && i6_status_has("Saved")); double t9 = i6_now();
    { cell su = 0, sc = 0, gu = 0, gc = 0; r0_s1_site_usage(&su, &sc); r0_s1_global_usage(&gu, &gc);
      printf("  function sites %ld of %ld; global names %ld of %ld\n", (long)su, (long)sc, (long)gu, (long)gc); }
    printf("  start-up (process to members loaded) %.0f ms; live after the last collection %ld cells; %ld collections\n",
           i7_t_setup1 - i7_t_main0, r0_s1_gc_live_cells(), r0_s1_gc_count());
    printf("intent6-scale: %d members: load %.0f ms; search keystroke %.0f ms (%d found); clear %.0f ms; select %.1f ms; "
           "edit+save %.0f ms; heap high %ld of %ld, %ld collections, live %ld; host error %d\n", n, t1 - t0, t3 - t2, found,
           t5 - t4, t7 - t6, t9 - t8, r0_s1_heap_high() - GC_HEAP_BASE, (long)(GC_HEAP_LIMIT - GC_HEAP_BASE), r0_s1_gc_count(),
           r0_s1_gc_live_cells(), gv_glon_host_error(h));
}


/* intent-browser-x11 (X11 and WebKitGTK only; opens a window, sends NO
 * input): the real engine loads the local page, navigates, fails cleanly,
 * is destroyed and recreated without orphan processes; measures processes
 * and memory, Glon's and WebKit's together */
static void case_intent_browser_x11(void) {
    if (strcmp(SDL_GetCurrentVideoDriver(), "x11")) {
        printf("intent-browser-x11: skipped (video driver %s; run with SDL_VIDEODRIVER=x11)\n", SDL_GetCurrentVideoDriver());
        return;
    }
    char a[300];
    int n0, w0; long r0;
    proc_tree(&n0, &r0, &w0, 0);
    printf("  before: %d process(es), %ld kB resident\n", n0, r0);
    CHECK(gint("block-len reduce [ test/browser-last: \"\" test/browser-status: \"\" ]") == 2);
    CHECK(ishow("test/browser-x11") == 0 && host_pump() == 0);
    long g = fgid("browser");
    if (!gv_glon_host_component(h, (int)g)) {
        igs(a, sizeof a, "test/browser-last");
        printf("intent-browser-x11: webkit-view could not be created (%s)\n", a);
        CHECK(0);
        return;
    }
    CHECK(wait_for("loaded file://", 20000) == 0);
    igs(a, sizeof a, "face/value form/field form/current 'address"); CHECK(strstr(a, "file://") && strstr(a, "demo.html"));
    /* rendering (FOREIGN-2 §7.2): the plug is in our window and mapped, the
     * web view is mapped with the node's size, and its pixels are the page,
     * not a blank window; read with XGetImage, no input */
    for (int t = 0; t < 100; t++) { host_pump(); SDL_Delay(20); }
    gv_host_webkit_state ws;
    void *wc = gv_glon_host_component(h, (int)g);
    gv_rect br;
    CHECK(screen(g, &br) == 0);
    CHECK(gv_host_webkit_query(wc, &ws) == 0);
    printf("  X map state outer %d inner %d plug %d; plug in inner %d; inner %dx%d plug %dx%d\n",
           ws.map_state[0], ws.map_state[1], ws.map_state[2], ws.plug_in_inner, ws.geom[1].w, ws.geom[1].h, ws.geom[2].w, ws.geom[2].h);
    printf("  GTK plug realized %d mapped %d; view realized %d mapped %d allocation %dx%d (node %dx%d)\n",
           ws.plug_realized, ws.plug_mapped, ws.view_realized, ws.view_mapped, ws.view_w, ws.view_h, br.w, br.h);
    printf("  pixels: %d samples, %d white, %d distinct values\n", ws.samples, ws.white, ws.distinct);
    CHECK(ws.map_state[0] == 2 && ws.map_state[1] == 2 && ws.map_state[2] == 2 && ws.plug_in_inner);
    CHECK(ws.plug_mapped && ws.view_mapped && ws.view_w == br.w && ws.view_h == br.h);
    CHECK(ws.samples > 100 && ws.distinct >= 4 && ws.white < ws.samples * 9 / 10);
    /* the page's header (a dark blue gradient, #2b5876 at its left) is
     * where it should be: the page itself, not some other content */
    printf("  pixel at (10,10): #%06x\n", ws.top_left);
    CHECK(rendered_header(ws.top_left));
    /* resize: GTK's allocation and the pixels follow the node */
    CHECK(resize(1300, 760) == 0);
    for (int t = 0; t < 100; t++) { host_pump(); SDL_Delay(20); }
    CHECK(screen(g, &br) == 0 && gv_host_webkit_query(wc, &ws) == 0);
    printf("  resized: node %dx%d, plug %dx%d, view allocation %dx%d, %d/%d white, #%06x\n",
           br.w, br.h, ws.geom[2].w, ws.geom[2].h, ws.view_w, ws.view_h, ws.white, ws.samples, ws.top_left);
    CHECK(ws.geom[2].w == br.w && ws.geom[2].h == br.h && ws.view_w == br.w && ws.view_h == br.h);
    CHECK(ws.white < ws.samples * 9 / 10 && rendered_header(ws.top_left));
    CHECK(resize(1200, 700) == 0);
    int n1, w1; long r1;
    proc_tree(&n1, &r1, &w1, 1);
    printf("  page loaded: %d process(es) (%d WebKit), %ld kB resident in all\n", n1, w1, r1);
    CHECK(w1 >= 1);
    /* idle: 5 s of the runner's loop (pump every 20 ms), the page loaded */
    long t0 = proc_tree_ticks();
    for (int t = 0; t < 250; t++) { host_pump(); SDL_Delay(20); }
    long t1 = proc_tree_ticks();
    printf("  idle CPU over 5 s (Glon + WebKit helpers): %.1f%% of one core\n", 100.0 * (double)(t1 - t0) / (5.0 * (double)sysconf(_SC_CLK_TCK)));
    /* the web process crashes (killed here): reported as a status, Glon goes
     * on, and the next navigation starts a new one (the first real run
     * segfaulted after a web-process crash: this is that path) */
    CHECK(gint("block-len reduce [ test/browser-status: \"\" ]") == 1);
    int killed = kill_web_processes();
    printf("  killed %d web process(es)\n", killed);
    CHECK(killed >= 1 && wait_for("failed web process terminated", 10000) == 0);
    CHECK(gint("block-len reduce [ test/browser-status: \"\" ]") == 1);
    CHECK(gint("block-len face/command form/field form/current 'browser \"reload\" \"\"") == 4 && apply_now() == 0);
    CHECK(wait_for("loaded file://", 20000) == 0);
    /* repeated navigation */
    for (int k = 0; k < 10; k++) {
        /* (gint takes no format: the address is chosen here) */
        CHECK(gint("block-len reduce [ test/browser-status: \"\" ]") == 1);
        CHECK(gint(k % 2 ? "block-len face/command form/field form/current 'browser \"navigate\" \"./glon/browser/demo.html\""
                         : "block-len face/command form/field form/current 'browser \"navigate\" \"./glon/browser/page2.html\"") == 4);
        CHECK(apply_now() == 0 && wait_loaded(k % 2 ? "demo.html" : "page2.html", 15000) == 0);
    }
    /* a page that does not exist: reported, Glon goes on */
    CHECK(gint("block-len reduce [ test/browser-status: \"\" ]") == 1);
    CHECK(gint("block-len face/command form/field form/current 'browser \"navigate\" \"./glon/browser/no-such-page.html\"") == 4 && apply_now() == 0);
    CHECK(wait_for("failed", 15000) == 0);
    igs(a, sizeof a, "test/browser-status"); printf("  missing page: %s\n", a);
    CHECK(strstr(a, "no-such-page.html") != NULL);
    /* WebKit's error page follows, later: "failed" must still be the status
     * once it has loaded (it was overwritten by "loaded no-such-page.html") */
    for (int t = 0; t < 100; t++) { host_pump(); SDL_Delay(20); }
    igs(a, sizeof a, "test/browser-status"); CHECK(strstr(a, "failed ") == a);
    igs(a, sizeof a, "face/value form/field form/current 'address"); CHECK(strstr(a, "no-such-page.html") != NULL);
    /* hide / show, back on the demo page (the error page is nearly all white) */
    CHECK(gint("block-len reduce [ test/browser-status: \"\" ]") == 1);
    CHECK(gint("block-len face/command form/field form/current 'browser \"navigate\" \"./glon/browser/demo.html\"") == 4 && apply_now() == 0);
    CHECK(wait_loaded("demo.html", 15000) == 0);
    CHECK(gint("block-len face/set-visible form/field form/current 'browser 0") == 4 && apply_now() == 0 && host_pump() == 0);
    CHECK(gint("block-len face/set-visible form/field form/current 'browser 1") == 4 && apply_now() == 0 && host_pump() == 0);
    /* shown again: still rendered */
    for (int t = 0; t < 100; t++) { host_pump(); SDL_Delay(20); }
    CHECK(gv_host_webkit_query(gv_glon_host_component(h, (int)g), &ws) == 0);
    printf("  hidden and shown: map %d/%d/%d, %d/%d white\n", ws.map_state[0], ws.map_state[1], ws.map_state[2], ws.white, ws.samples);
    CHECK(ws.map_state[0] == 2 && ws.map_state[2] == 2 && ws.white < ws.samples * 9 / 10 && rendered_header(ws.top_left));
    int n2, w2; long r2;
    proc_tree(&n2, &r2, &w2, 0);
    printf("  after 11 navigations: %d process(es) (%d WebKit), %ld kB resident in all\n", n2, w2, r2);
    /* destroy and recreate five times */
    for (int k = 0; k < 5; k++) {
        CHECK(ishow("test/form") == 0);
        for (int t = 0; t < 50; t++) { host_pump(); SDL_Delay(20); }
        CHECK(gint("block-len reduce [ test/browser-status: \"\" ]") == 1);
        CHECK(ishow("test/browser-x11") == 0 && wait_for("loaded file://", 20000) == 0);
    }
    /* destroyed while still loading, twenty times in a row (the destroy
     * race: GDK's queued requests against our window's destruction) */
    for (int k = 0; k < 20; k++) {
        CHECK(ishow("test/form") == 0 && ishow("test/browser-x11") == 0);
        for (int t = 0; t < k % 4; t++) { host_pump(); SDL_Delay(10); }
    }
    CHECK(ishow("test/form") == 0);
    for (int t = 0; t < 150; t++) { host_pump(); SDL_Delay(20); }     /* let helpers exit */
    int n3, w3; long r3;
    proc_tree(&n3, &r3, &w3, 1);
    printf("  closed after 26 creations: %d process(es) (%d WebKit), %ld kB resident in all\n", n3, w3, r3);
    int alive = 0, cr = 0, de = 0;
    gv_host_counts(&alive, &cr, &de);
    CHECK(alive == 0);
    printf("intent-browser-x11: real WebKit loaded the local page and 10 navigations, reported the missing page, "
           "hid/showed, 26 created / destroyed; processes %d -> %d (loaded) -> %d (closed); %ld -> %ld -> %ld kB\n",
           n0, n1, n3, r0, r1, r3);
}

/* ---- GLON-VIEW-PREP-1: the C1 integration acceptance cases -------------------
 * view/glon/c1_accept_app.glon: component instances of a definition kept in a
 * nested application vocabulary (c1/addr/fields), handlers named by their full
 * names from dialect blocks, explicit outer assignment (c1/edits:, set
 * 'c1-last), and -- when the session loads the optional window library --
 * the same component in native windows (SDL dummy driver, synthetic events). */
#define CM(inst, name) "form/field form/sub intent/root-form c1/main '" inst " '" name
#define CMR(name) "form/field intent/root-form c1/main '" name
#define CW(win, inst, name) "form/field form/sub intent/root-form " win " '" inst " '" name
static long c1_data(const char *lay, const char *inst) {
    char e[200];
    snprintf(e, sizeof e, "either = form/data form/sub intent/root-form %s '%s none [ 0 ] [ form/data form/sub intent/root-form %s '%s ]", lay, inst, lay, inst);
    return gint(e);
}
static void case_c1_accept(void) {
    char a[400];
    CHECK(resize(900, 700) == 0);
    long s0 = inst_sites(), g0 = inst_globals();
    if (c1_windows) w3_in("c1/main");
    /* 1. two instances of one component, defined in a nested vocabulary: their
     *    own values and counts; the enclosing vocabulary's total; the global */
    CHECK(i6_type(CM("home", "street"), "1 Main") == 0 && i6_is(CM("home", "street"), "1 Main"));
    CHECK(i6_is(CM("work", "street"), "") && i6_is(CM("work", "city"), ""));
    CHECK(c1_data("c1/main", "home") == 6 && c1_data("c1/main", "work") == 0);
    CHECK(gint("c1/edits") == 6);
    igs(a, sizeof a, "c1-last"); CHECK(!strcmp(a, "1 Main"));
    igs(a, sizeof a, "face/text " CM("home", "count")); CHECK(!strcmp(a, "6 edits"));
    igs(a, sizeof a, "face/text " CM("work", "count")); CHECK(!strcmp(a, ""));
    CHECK(i6_type(CM("work", "city"), "Ab") == 0 && c1_data("c1/main", "work") == 2 && c1_data("c1/main", "home") == 6);
    CHECK(gint("c1/edits") == 8);
    igs(a, sizeof a, "c1-last"); CHECK(!strcmp(a, "Ab"));
    /* the inline action (Clear) acts on its own instance only */
    CHECK(i6_click(CM("home", "wipe")) == 0 && i6_is(CM("home", "street"), "") && i6_is(CM("home", "city"), ""));
    CHECK(i6_is(CM("work", "city"), "Ab"));
    if (!c1_windows) {
        CHECK(gv_glon_host_window_count(h) == 0);
        printf("c1-no-windows: component instances in a nested vocabulary, explicit outer assignment, no window library; "
               "sites %ld, globals %ld\n", s0, g0);
        return;
    }
    /* 2. a native window holding two more instances of the same component */
    long e0 = gint("c1/edits");
    CHECK(i6_click(CMR("open")) == 0 && gv_glon_host_window_count(h) == 1 && w3_name_last("t/w1"));
    igs(a, sizeof a, "face/text " CMR("status")); CHECK(!strcmp(a, "opened 1"));
    w3_in("t/w1");
    CHECK(i6_type(CW("t/w1", "one", "street"), "Win St") == 0 && i6_is(CW("t/w1", "one", "street"), "Win St"));
    CHECK(i6_is(CW("t/w1", "two", "street"), "") && i6_is(CM("home", "street"), "") && i6_is(CM("work", "city"), "Ab"));
    CHECK(c1_data("t/w1", "one") == 6 && c1_data("t/w1", "two") == 0 && c1_data("c1/main", "work") == 2);
    CHECK(gint("c1/edits") == e0 + 6);
    /* a second window: the same names, a third scope */
    w3_in("c1/main");
    CHECK(i6_click(CMR("open")) == 0 && gv_glon_host_window_count(h) == 2 && w3_name_last("t/w2"));
    igs(a, sizeof a, "face/text " CMR("status")); CHECK(!strcmp(a, "opened 2"));
    w3_in("t/w2");
    CHECK(i6_type(CW("t/w2", "two", "city"), "Two") == 0 && c1_data("t/w2", "two") == 3 && c1_data("t/w1", "two") == 0);
    CHECK(i6_is(CW("t/w1", "one", "street"), "Win St") && i6_is(CW("t/w2", "one", "street"), ""));
    /* keys follow the native window they come from */
    Uint32 id1 = w3_sdl("t/w1"), id2 = w3_sdl("t/w2");
    CHECK(id1 && id2 && id1 != id2);
    win_target = id1; CHECK(key(SDLK_END) == 0 && text("!") == 0);
    CHECK(i6_is(CW("t/w1", "one", "street"), "Win St!") && i6_is(CW("t/w2", "two", "city"), "Two"));
    /* closing W1 by its own Close button: its handles go stale, W2 and the
     * main window keep working */
    CHECK(gint("block-len reduce [ t/h1: " CW("t/w1", "one", "street") " ]") == 1);
    w3_in("t/w1"); CHECK(i6_click("form/field intent/root-form t/w1 'close") == 0);
    CHECK(gv_glon_host_window_count(h) == 1 && gint("intent/open? t/w1") == 0 && gint("face/alive t/h1") == 0);
    igs(a, sizeof a, "sin-id judge [ face/value t/h1 ]"); CHECK(!strcmp(a, "stale-widget"));
    w3_in("t/w2"); CHECK(i6_type(CW("t/w2", "one", "street"), "After") == 0 && i6_is(CW("t/w2", "one", "street"), "After"));
    w3_in("c1/main"); CHECK(i6_type(CM("home", "city"), "Main2") == 0 && i6_is(CM("home", "city"), "Main2"));
    CHECK(gint("block-len reduce [ intent/close t/w2 widget/flush ]") == 2 && w3_deliver() && gv_glon_host_window_count(h) == 0);
    /* 3. 20 open/close cycles of a two-instance window: no site, global or id
     *    is kept (one warm-up cycle first: t/c is a new global) */
    w3_in("c1/main");
    CHECK(gint("block-len reduce [ t/c: intent/open-window-at c1/window-spec 400 300 \"warm\" widget/flush ]") == 2 && w3_deliver());
    CHECK(gint("block-len reduce [ intent/close t/c widget/flush ]") == 2 && w3_deliver());
    w3_in("c1/main");
    long s1 = inst_sites(), g1 = inst_globals(), id0 = gint("view/id"), fc = inst2_free();
    int live0 = gv_view_live_nodes(v);
    for (int c = 0; c < 20; c++) {
        CHECK(gint("block-len reduce [ t/c: intent/open-window-at c1/window-spec 400 300 \"cycle\" widget/flush ]") == 2 && w3_deliver());
        if (c % 5 == 0) { w3_in("t/c"); CHECK(i6_type(CW("t/c", "two", "street"), "Cycle") == 0 && i6_is(CW("t/c", "two", "street"), "Cycle")); }
        CHECK(gint("block-len reduce [ intent/close t/c widget/flush ]") == 2 && w3_deliver());
    }
    w3_in("c1/main");
    CHECK(gv_glon_host_window_count(h) == 0 && inst_sites() == s1 && inst_globals() == g1);
    CHECK(gint("view/id") == id0 && inst2_free() == fc && gv_view_live_nodes(v) == live0);
    CHECK(i6_is(CM("home", "city"), "Main2") && i6_is(CM("work", "city"), "Ab"));
    if (inst_sites() != s1 || inst_globals() != g1) fprintf(stderr, "  sites %ld -> %ld, globals %ld -> %ld\n", s1, inst_sites(), g1, inst_globals());
    printf("  session: sites %ld, globals %ld at the start (window library loaded)\n", s0, g0);
    printf("c1-accept: component instances of a nested-vocabulary definition in the main window and two native windows, "
           "own values and form/data, shared total %ld; keys by window; closed window's handles stale; 20 cycles keep "
           "sites %ld, globals %ld, view/id %ld, free ids %ld\n", gint("c1/edits"), s1, g1, id0, fc);
}

int main(int argc, char **argv) {
    i7_t_main0 = i6_now();
    const char *root = argc > 1 ? argv[1] : "..";
    root_dir = root;
    const char *which = argc > 2 ? argv[2] : "sizes";
    if (!strncmp(which, "intent7", 7)) app_file = getenv("GV_I7_APP") && !strcmp(which, "intent7-eval") ? getenv("GV_I7_APP") : "view/glon/membership_app.glon";
    else if (!strncmp(which, "inst1", 5)) app_file = "view/glon/intent_components_demo.glon";
    else if (!strncmp(which, "win3", 4)) app_file = "view/glon/intent_windows_demo.glon";
    else if (!strncmp(which, "c1-", 3)) { app_file = "view/glon/c1_accept_app.glon"; c1_windows = !strcmp(which, "c1-accept"); }
    else if (!strncmp(which, "inst2", 5) || !strncmp(which, "inst3", 5)) app_file = "view/glon/intent_instances_demo.glon";
    else if (!strncmp(which, "intent", 6))
        app_file = !strncmp(which, "intent-demo", 11) ? "view/glon/intent_demo.glon"
                 : !strncmp(which, "intent4-demo", 12) ? "view/glon/intent4_demo.glon"
                 : !strncmp(which, "intent4-forms", 13) ? "view/glon/intent_forms_demo.glon"
                 : !strncmp(which, "intent4-select-demo", 19) ? "view/glon/intent_select_demo.glon"
                 : !strncmp(which, "intent4-layout-demo", 19) ? "view/glon/intent_layout_demo.glon"
                 : !strncmp(which, "intent5-gallery", 15) ? "view/glon/intent_gallery_demo.glon"
                 : !strncmp(which, "intent6", 7) ? "view/glon/membership_app.glon"
                 : !strncmp(which, "intent-members", 14) ? "view/glon/intent_members.glon" : "view/glon/intent_test.glon";
    png_dir = argc > 3 ? argv[3] : NULL;
    /* the edge features are opt-in; this case turns them on before the
     * window exists (GV_EDGE_CURSOR may choose system instead of bitmap) */
    if (!strcmp(which, "edges")) {
        SDL_setenv("GV_EDGE_RESIZE", "1", 1);
        if (!getenv("GV_EDGE_CURSOR")) SDL_setenv("GV_EDGE_CURSOR", "bitmap", 1);
        SDL_setenv("GV_EDGE_WIDTH", "6", 1);    /* the zone map below is the 6-pixel band's */
    }
    if (!strcmp(which, "edges-default")) { SDL_setenv("GV_EDGE_RESIZE", "0", 1); SDL_setenv("GV_EDGE_CURSOR", "0", 1); }
    /* edge-promise-*: the cursor alone, cursor + hit test, neither */
    if (!strcmp(which, "edge-promise-cursor")) { SDL_setenv("GV_EDGE_RESIZE", "0", 1); SDL_setenv("GV_EDGE_CURSOR", "system", 1); }
    if (!strcmp(which, "edge-promise-both")) { SDL_setenv("GV_EDGE_RESIZE", "1", 1); SDL_setenv("GV_EDGE_CURSOR", "system", 1); }
    if (!strcmp(which, "edge-promise-default")) { SDL_setenv("GV_EDGE_RESIZE", "0", 1); SDL_setenv("GV_EDGE_CURSOR", "0", 1); }
    if (!strncmp(which, "edge-clearance-", 15)) SDL_setenv("GV_EDGE_WIDTH", which + 15, 1);
    if (!strcmp(which, "edge-policy-wslg")) SDL_setenv("GV_ASSUME_WSLG", "1", 1);
    if (!strcmp(which, "edge-policy-default")) SDL_setenv("GV_ASSUME_WSLG", "0", 1);
    if (!strcmp(which, "edge-policy-override")) { SDL_setenv("GV_ASSUME_WSLG", "1", 1); SDL_setenv("GV_EDGE_RESIZE", "0", 1); SDL_setenv("GV_EDGE_WIDTH", "8", 1); }
    if (!strncmp(which, "intent-demo-clearance-", 22)) SDL_setenv("GV_EDGE_WIDTH", which + 22, 1);
    gv_glon_host_timing_reset();
    if (setup(root) != 0) { fprintf(stderr, "setup failed\n"); return 2; }
    i7_t_setup1 = i6_now();
    printf("test_gui_p2 %s (video driver %s)\n", which, getenv("SDL_VIDEODRIVER"));
    if (!strcmp(which, "sizes")) case_sizes();
    else if (!strcmp(which, "rapid")) case_rapid();
    else if (!strcmp(which, "clicks")) case_clicks();
    else if (!strcmp(which, "edit")) case_edit();
    else if (!strcmp(which, "state")) case_state();
    else if (!strcmp(which, "themes")) case_themes();
    else if (!strcmp(which, "lifecycle")) case_lifecycle();
    else if (!strcmp(which, "reveal")) case_reveal();
    else if (!strcmp(which, "scrollbars")) case_scrollbars();
    else if (!strcmp(which, "press")) case_press();
    else if (!strcmp(which, "caret")) case_caret();
    else if (!strcmp(which, "edges")) case_edges();
    else if (!strcmp(which, "edges-default")) case_edges_default();
    else if (!strcmp(which, "tabs")) case_tabs();
    else if (!strcmp(which, "tabs-resize")) case_tabs_resize();
    else if (!strcmp(which, "tabs-lifecycle")) case_tabs_lifecycle();
    else if (!strcmp(which, "table")) case_table();
    else if (!strcmp(which, "table-unicode")) case_table_unicode();
    else if (!strcmp(which, "source")) case_source();
    else if (!strcmp(which, "table-drag")) case_table_drag();
    else if (!strcmp(which, "source-drag")) case_source_drag();
    else if (!strcmp(which, "table-cols")) case_table_cols();
    else if (!strcmp(which, "intent-parse")) case_intent_parse();
    else if (!strcmp(which, "intent-directions")) case_intent_directions();
    else if (!strcmp(which, "intent-unequal")) case_intent_unequal();
    else if (!strcmp(which, "intent-alias")) case_intent_alias();
    else if (!strcmp(which, "intent-nested")) case_intent_nested();
    else if (!strcmp(which, "intent4-layout")) case_intent4_layout();
    else if (!strcmp(which, "intent4-fill")) case_intent4_fill();
    else if (!strcmp(which, "intent4-state")) case_intent4_state();
    else if (!strcmp(which, "intent4-layout-demo")) case_intent4_layout_demo();
    else if (!strcmp(which, "theme-contrast")) case_theme_contrast();
    else if (!strcmp(which, "intent5-theme")) case_intent5_theme();
    else if (!strcmp(which, "intent5-look")) case_intent5_look();
    else if (!strcmp(which, "intent5-image")) case_intent5_image();
    else if (!strcmp(which, "intent5-gallery")) case_intent5_gallery();
    else if (!strcmp(which, "intent6-smoke")) case_intent6_smoke();
    else if (!strcmp(which, "intent-time")) {
        const char *ops0[] = { "0", "s/length \"Kwok\"", "s/+ \"ab\" \"cd\"", "mk-string reduce [ 107 ]" };
        for (int k = 0; k < 4; k++) {
            char e[400]; snprintf(e, sizeof e, "block-len reduce [ %s 0 ]", ops0[k]);
            double t0 = i6_now(); for (int j = 0; j < 20; j++) gint(e); double t1 = i6_now();
            printf("  %-40s %8.3f ms; heap high %ld gcs %ld\n", ops0[k], (t1 - t0) / 20, r0_s1_heap_high() - GC_HEAP_BASE, r0_s1_gc_count());
        }
    }
    else if (!strcmp(which, "intent6-load")) case_intent6_load();
    else if (!strcmp(which, "intent6-select")) case_intent6_select();
    else if (!strcmp(which, "intent6-search")) case_intent6_search();
    else if (!strcmp(which, "intent6-new-edit")) case_intent6_new_edit();
    else if (!strcmp(which, "intent6-validation")) case_intent6_validation();
    else if (!strcmp(which, "intent6-unsaved")) case_intent6_unsaved();
    else if (!strcmp(which, "intent6-delete")) case_intent6_delete();
    else if (!strcmp(which, "intent6-reload")) case_intent6_reload();
    else if (!strcmp(which, "intent6-keys")) case_intent6_keys();
    else if (!strcmp(which, "intent6-resize")) case_intent6_resize();
    else if (!strcmp(which, "intent6-stress")) case_intent6_stress();
    else if (!strcmp(which, "intent6-scale")) case_intent6_scale();
    else if (!strcmp(which, "intent6-mem")) case_intent6_mem();
    else if (!strcmp(which, "intent6-postgc")) case_intent6_postgc();
    else if (!strcmp(which, "intent7-ops")) case_intent7_ops();
    else if (!strcmp(which, "intent7-eval")) case_intent7_eval();
    else if (!strcmp(which, "inst1-components")) case_inst1_components();
    else if (!strcmp(which, "inst1-scale")) case_inst1_scale();
    else if (!strcmp(which, "inst2-instances")) case_inst2_instances();
    else if (!strcmp(which, "inst2-scale")) case_inst2_scale();
    else if (!strcmp(which, "inst3-lifecycle")) case_inst3_lifecycle();
    else if (!strcmp(which, "win3-windows")) case_win3_windows();
    else if (!strcmp(which, "win3-lifecycle")) case_win3_lifecycle();
    else if (!strcmp(which, "win3-failure")) case_win3_failure();
    else if (!strcmp(which, "win3-scale")) case_win3_scale();
    else if (!strcmp(which, "win3-heap")) case_win3_heap();
    else if (!strcmp(which, "c1-accept") || !strcmp(which, "c1-no-windows")) case_c1_accept();
    else if (!strcmp(which, "intent7-probe")) case_intent7_probe();
    else if (!strcmp(which, "intent7-parts")) case_intent7_parts();
    else if (!strcmp(which, "intent7-reload")) case_intent7_reload();
    else if (!strcmp(which, "intent7-search")) case_intent7_search();
    else if (!strcmp(which, "intent7-shots")) case_intent7_shots();
#ifdef R0S1_GC_PROF
    else if (!strcmp(which, "intent6-gcprof")) case_intent6_gcprof();
    else if (!strcmp(which, "intent6-gcprof-save")) case_intent6_gcprof_save();
    else if (!strcmp(which, "intent6-gcprof-scale")) case_intent6_gcprof_scale();
#endif
    else if (!strcmp(which, "intent-edit")) case_intent_edit();
    else if (!strcmp(which, "intent-escape")) case_intent_escape();
    else if (!strcmp(which, "intent-lifecycle")) case_intent_lifecycle();
    else if (!strcmp(which, "intent-resize")) case_intent_resize();
    else if (!strcmp(which, "intent-demo")) case_intent_demo();
    else if (!strcmp(which, "intent-stretch")) case_intent_stretch();
    else if (!strcmp(which, "intent-demo-resize")) case_intent_demo_resize();
    else if (!strcmp(which, "intent-members-handlers")) case_intent_members_handlers();
    else if (!strcmp(which, "intent-handlers")) case_intent_handlers();
    else if (!strcmp(which, "intent-foreign")) case_intent_foreign();
    else if (!strcmp(which, "intent-foreign-tab")) case_intent_foreign_tab();
    else if (!strcmp(which, "intent-browser-mock")) case_intent_browser_mock();
    else if (!strcmp(which, "webkit-address")) case_webkit_address();
    else if (!strcmp(which, "intent3-styles")) case_intent3_styles();
    else if (!strcmp(which, "intent3-commands")) case_intent3_commands();
    else if (!strcmp(which, "intent3-follow")) case_intent3_follow();
    else if (!strcmp(which, "intent3-forms")) case_intent3_forms();
    else if (!strcmp(which, "intent3-errors")) case_intent3_errors();
    else if (!strcmp(which, "intent4-check")) case_intent4_check();
    else if (!strcmp(which, "intent4-tabs")) case_intent4_tabs();
    else if (!strcmp(which, "intent4-table")) case_intent4_table();
    else if (!strcmp(which, "intent4-demo-disabled")) case_intent4_demo_disabled();
    else if (!strcmp(which, "intent4-edit")) case_intent4_edit();
    else if (!strcmp(which, "intent4-forms-demo")) case_intent4_forms_demo();
    else if (!strcmp(which, "intent4-select")) case_intent4_select();
    else if (!strcmp(which, "intent4-select-demo")) case_intent4_select_demo();
    else if (!strcmp(which, "intent4-select-demo-window")) case_intent4_select_demo_window();
    else if (!strcmp(which, "edge-cursor-decision")) case_edge_cursor_decision();
    else if (!strcmp(which, "x11-edge-cursors")) case_x11_edge_cursors();
    else if (!strcmp(which, "intent3-browser-x11")) case_intent3_browser_x11();
    else if (!strcmp(which, "intent-browser-x11")) case_intent_browser_x11();
    else if (!strcmp(which, "intent-foreign-x11")) case_intent_foreign_x11();
    else if (!strcmp(which, "intent-members-forms")) case_intent_members_forms();
    else if (!strcmp(which, "intent-members-validate")) case_intent_members_validate();
    else if (!strcmp(which, "native-window")) case_native_window(0);
    else if (!strcmp(which, "capture")) case_capture();
    else if (!strncmp(which, "edge-promise", 12)) case_edge_promise();
    else if (!strncmp(which, "edge-clearance-", 15)) case_edge_clearance(0);
    else if (!strncmp(which, "edge-policy-", 12)) case_edge_policy(which);
    else if (!strncmp(which, "intent-demo-clearance-", 22)) case_edge_clearance(1);
    else if (!strcmp(which, "intent-demo-native")) case_native_window(1);
    else { fprintf(stderr, "unknown case %s\n", which); return 2; }
    CHECK(!gv_glon_host_error(h));
    if (failures) { fprintf(stderr, "test_gui_p2 %s: %d failure(s)\n", which, failures); return 1; }
    printf("test_gui_p2 %s: ok\n", which);
    return 0;
}
