/* test_wg_runner.c -- Checkpoint A: the WG-3 widget gallery driven through
 * the interactive SDL runner's path.
 *
 * SDL events are pushed into the SDL backend (dummy video driver), read back
 * with poll_event, coalesced and routed with gv_glon_host_dispatch, exactly
 * as glon_demo --vocab-vid does; frames are drawn and presented as the
 * runner draws them.  Checks use View geometry, Glon state and the pixels
 * presented in the window.
 *
 * Usage: test_wg_runner ROOT CASE [HEADLESS_PNG]
 *   launch nav type hover theme resize checks keys clear perf profile */
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
#define W 1200
#define H 700

static long gint(const char *expr) {
    char buf[96] = "";
    if (gv_glon_host_text(h, expr, buf, sizeof buf) != 0) return -999999;
    return atol(buf);
}
static void gstr(const char *expr, char *out, int cap) { out[0] = 0; gv_glon_host_text(h, expr, out, cap); }

static int render(void) {
    const gv_display_list *dl = gv_glon_host_display(h);
    if (!dl || b->begin_frame(b) != 0 || b->draw(b, dl) != 0 || b->end_frame(b) != 0) return -1;
    return 0;
}
/* one runner iteration: drain, coalesce, dispatch, draw once */
static int pump(void) {
    gv_event evs[256];
    int n = 0, rc = 0;
    while (n < 256 && b->poll_event(b, &evs[n]) > 0) n++;
    n = gv_glon_host_coalesce(evs, n);
    for (int i = 0; i < n; i++) if (gv_glon_host_dispatch(h, &evs[i]) != 0) rc = -1;
    if (n > 0 && render() != 0) rc = -1;
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

/* the View node showing exactly this text (first match), its screen centre */
static gv_node_id text_node(const char *t) {
    for (int i = 1; i <= v->count; i++) {
        gv_node *n = gv_view_node(v, i);
        if (n && n->type == GV_NODE_TEXT && n->text && !strcmp(n->text, t)) return (gv_node_id)i;
    }
    return 0;
}
static int centre_of_text(const char *t, int *x, int *y) {
    gv_node_id n = text_node(t);
    gv_rect r;
    if (!n) return -1;
    if (v->layout_dirty) gv_view_layout(v);
    if (gv_view_visible_rect(v, n, &r) != 0) return -1;
    if (getenv("WG_TRACE")) fprintf(stderr, "[text] %s node %d at %d,%d %dx%d\n", t, (int)n, r.x, r.y, r.w, r.h);
    *x = r.x + r.w / 2; *y = r.y + r.h / 2;
    return 0;
}
static int click_text(const char *t) {
    int x, y;
    if (centre_of_text(t, &x, &y) != 0) { fprintf(stderr, "no visible text %s\n", t); return -1; }
    return click(x, y);
}
static void status(char *out, int cap) { gstr("gallery/status", out, cap); }

static SDL_Window *find_window(void) {
    for (Uint32 id = 1; id < 16; id++) { SDL_Window *w = SDL_GetWindowFromID(id); if (w) return w; }
    return NULL;
}
/* pixels presented in the window against a PNG (the frame, or the headless
 * harness's screenshot); answers the number of differing pixels */
static long window_vs_png(const char *png, int *s) {
    SDL_Window *w = find_window();
    SDL_Surface *ws = w ? SDL_GetWindowSurface(w) : NULL;
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
            Uint32 px = 0;
            memcpy(&px, (Uint8 *)ws->pixels + y * ws->pitch + x * ws->format->BytesPerPixel, ws->format->BytesPerPixel);
            Uint8 r, g, bb;
            SDL_GetRGB(px, ws->format, &r, &g, &bb);
            Uint32 c = *(Uint32 *)(d + y * stride + x * 4);
            int cr = (c >> 16) & 255, cg = (c >> 8) & 255, cb = c & 255;
            if (abs(cr - r) > 1 || abs(cg - g) > 1 || abs(cb - bb) > 1) {
                if (!bad && s) { s[0] = x; s[1] = y; s[2] = r; s[3] = g; s[4] = bb; s[5] = cr; s[6] = cg; s[7] = cb; }
                bad++;
            }
        }
    SDL_UnlockSurface(ws);
    cairo_surface_destroy(cs);
    return bad;
}
static long presented_ok(void) {
    int s[8] = {0};
    if (gv_backend_sdl_save_png(b, "/tmp/wg-runner-frame.png") != 0) return -1;
    long bad = window_vs_png("/tmp/wg-runner-frame.png", s);
    if (bad) fprintf(stderr, "window differs from frame at %d,%d: %d,%d,%d vs %d,%d,%d\n", s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]);
    return bad;
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

static int setup(const char *root) {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    v = gv_view_new(W, H);
    b = gv_backend_sdl();
    gv_backend_config cfg; memset(&cfg, 0, sizeof cfg);
    cfg.width = W; cfg.height = H; cfg.title = "wg";
    if (!v || !b || b->init(b, &cfg) != 0 || b->create_window(b, W, H, "wg") != 0) return -1;
    h = gv_glon_host_new(v, b);
    /* the runner's --vocab-vid path */
    if (!h || gv_glon_host_load_machine_vid_app(h, root, "view/glon/widget_gallery.glon") != 0) return -1;
    if (gv_glon_host_build_app(h, NULL) != 0 || gv_glon_host_bind(h) != 0 || gv_glon_host_error(h)) return -1;
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    return render();
}

/* A1: launches; the window shows what was drawn, and that matches the
 * headless screenshot of the same build */
static void case_launch(const char *headless_png) {
    CHECK(presented_ok() == 0);
    if (headless_png) {
        int s[8] = {0};
        long bad = window_vs_png(headless_png, s);
        if (bad) fprintf(stderr, "window differs from headless at %d,%d: %d,%d,%d vs %d,%d,%d\n", s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]);
        CHECK(bad == 0);
    }
    int bg[3]; pixel(4, 4, bg);
    CHECK(bg[0] == 31 && bg[1] == 34 && bg[2] == 40);       /* WG-3 dark root panel */
    printf("launch: presented frame equals the rendered frame%s; window bg %d,%d,%d\n",
           headless_png ? " and the headless screenshot" : "", bg[0], bg[1], bg[2]);
}

/* A2: pointer navigation through the nav list */
static void case_nav(void) {
    char st[96];
    const char *items[] = { "Window", "Panel", "Label", "Checkbox", "Radio", "Field", "Table", "Vocabulary", "Button" };
    long idx[] = { 0, 1, 2, 4, 5, 6, 7, 8, 3 };
    for (int i = 0; i < 9; i++) {
        CHECK(click_text(items[i]) == 0);
        if (gint("gallery/id 38") != idx[i]) fprintf(stderr, "after %s: selected %ld\n", items[i], gint("gallery/id 38"));
        CHECK(gint("gallery/id 38") == idx[i]);
        status(st, sizeof st);
        CHECK(strstr(st, "Selected widget") != NULL);
        /* the nav row shows the selection */
        char e[64]; snprintf(e, sizeof e, "block-at gallery/id %ld 5", idx[i]);
        CHECK(gint(e) == 1);
    }
    CHECK(click_text("Activate") == 0);
    status(st, sizeof st);
    CHECK(strstr(st, "Button activated") != NULL);
    CHECK(presented_ok() == 0);
    CHECK(!gv_glon_host_error(h));
    printf("nav: 9 selections by pointer, Activate pressed, view/id %ld\n", gint("view/id"));
}

/* A3: typing into the field through SDL text input */
static void case_type(void) {
    char st[96];
    CHECK(click_text("Field") == 0);
    CHECK(click_text("Type here") == 0);
    CHECK(text("Hi") == 0);
    status(st, sizeof st);
    CHECK(strstr(st, "Field: Hi") != NULL);
    CHECK(text_node("Hi") != 0);
    CHECK(key(8) == 0);                           /* Backspace */
    status(st, sizeof st);
    CHECK(strstr(st, "Field: H") != NULL);
    CHECK(click_text("Clear") == 0);
    status(st, sizeof st);
    CHECK(strstr(st, "Field cleared") != NULL);
    CHECK(presented_ok() == 0);
    printf("type: Hi typed, backspaced, cleared\n");
}

/* A4: hover over nav rows and buttons changes their look */
static void case_hover(void) {
    int x, y, before[3], after[3], away[3];
    CHECK(centre_of_text("Radio", &x, &y) == 0);
    pixel(x - 80, y, before);
    push_motion(x, y); CHECK(pump() == 0);
    CHECK(gint("block-at gallery/id 5 7") == 1);         /* nav state hovered */
    pixel(x - 80, y, after);
    CHECK(memcmp(before, after, sizeof before) != 0);
    push_motion(x, y + 300); CHECK(pump() == 0);
    CHECK(gint("block-at gallery/id 5 7") == 0);
    pixel(x - 80, y, away);
    CHECK(memcmp(before, away, sizeof before) == 0);
    printf("hover: nav row %d,%d,%d -> %d,%d,%d -> back\n", before[0], before[1], before[2], after[0], after[1], after[2]);
}

/* A5: theme switching through the button; ids do not accumulate */
static void case_theme(void) {
    int bg[3];
    long id0 = gint("view/id");
    for (int i = 0; i < 4; i++) CHECK(click_text("Theme") == 0);
    CHECK(gint("view/id") == id0);
    CHECK(click_text("Theme") == 0);
    pixel(4, 4, bg);
    CHECK(bg[0] == 250 && bg[1] == 250 && bg[2] == 252);     /* WG-3 light root panel */
    CHECK(presented_ok() == 0);
    CHECK(click_text("Field") == 0 && click_text("Type here") == 0 && text("ok") == 0);
    char st[96]; status(st, sizeof st);
    CHECK(strstr(st, "Field: ok") != NULL);
    printf("theme: 5 switches, view/id %ld -> %ld, light bg %d,%d,%d\n", id0, gint("view/id"), bg[0], bg[1], bg[2]);
}

/* A6: resizing; geometry and targeting agree afterwards */
static void case_resize(void) {
    int sizes[][2] = { {1000, 600}, {1400, 800}, {1200, 700} };
    for (int i = 0; i < 3; i++) {
        gv_event ev; memset(&ev, 0, sizeof ev);
        ev.type = GV_EVENT_RESIZE; ev.width = sizes[i][0]; ev.height = sizes[i][1];
        CHECK(gv_glon_host_dispatch(h, &ev) == 0);
        CHECK(click_text("Checkbox") == 0);
        CHECK(gint("gallery/id 38") == 4);
        CHECK(click_text("Button") == 0);
        CHECK(gint("gallery/id 38") == 3);
    }
    CHECK(render() == 0);
    printf("resize: 3 sizes, navigation still lands\n");
}

/* A7: checkbox and radio by pointer */
static void case_checks(void) {
    char st[96];
    CHECK(click_text("Checkbox") == 0);
    CHECK(click_text("Mailing list") == 0);
    status(st, sizeof st); CHECK(strstr(st, "Checkbox 2 checked") != NULL);
    CHECK(click_text("Mailing list") == 0);
    status(st, sizeof st); CHECK(strstr(st, "Checkbox 2 unchecked") != NULL);
    CHECK(click_text("Radio") == 0);
    CHECK(click_text("Business") == 0);
    status(st, sizeof st); CHECK(strstr(st, "Radio: Business") != NULL);
    CHECK(gint("block-at gallery/id 18 5") == 1 && gint("block-at gallery/id 16 5") == 0);
    printf("checks: checkbox toggled twice, radio moved\n");
}

/* A8: keyboard: the runner's Tab walks the View focus order */
static void case_keys(void) {
    int seen = 0;
    gv_node_id last = 0;
    for (int i = 0; i < 6; i++) {
        CHECK(key(9) == 0);
        gv_node_id f = gv_view_focus_id(v);
        if (f && f != last) seen++;
        last = f;
    }
    CHECK(seen >= 5);
    char st[96]; status(st, sizeof st);
    CHECK(strstr(st, "Focus") != NULL);
    printf("keys: Tab moved View focus %d times; status \"%s\"\n", seen, st);
}

static double ms_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; }
static void case_perf(void) {
    int ax, ay, bx, by;
    CHECK(centre_of_text("Radio", &ax, &ay) == 0 && centre_of_text("Field", &bx, &by) == 0);
    double t0 = ms_now();
    for (int i = 0; i < 10; i++) { push_motion(ax, ay); pump(); push_motion(bx, by); pump(); }
    double t1 = ms_now();
    for (int i = 0; i < 6; i++) CHECK(click_text(i & 1 ? "Field" : "Radio") == 0);
    double t2 = ms_now();
    CHECK(click_text("Type here") == 0);
    double t3 = ms_now();
    for (int i = 0; i < 10; i++) CHECK(text("x") == 0);
    double t4 = ms_now();
    CHECK(click_text("Theme") == 0);
    double t5 = ms_now();
    printf("perf (ms incl. frame): hover change %.1f, preview switch %.1f, keystroke %.1f, theme %.0f\n",
           (t1 - t0) / 20, (t2 - t1) / 6, (t4 - t3) / 10, t5 - t4);
}

/* the gallery field (slot 32): displayed View text, VID state text and the
 * host's input buffer (v/field-text) */
static void field_views(const char *tag, char *shown, char *state, char *host) {
    shown[0] = state[0] = host[0] = 0;
    long tx = gint("block-at gallery/id 32 3");
    gv_node *n = gv_view_node(v, gv_glon_host_view_node(h, (int)tx));
    if (n && n->text) snprintf(shown, 96, "%s", n->text);
    gstr("block-at gallery/id 32 5", state, 96);
    gstr("v/field-text", host, 96);
    printf("  %-28s shown=\"%s\" state=%s host=\"%s\"\n", tag, shown, state, host);
}
/* Clear empties the field for good: blur, refocus, a theme switch (which
 * rebuilds every widget) and forced collections keep it empty, and the next
 * keystroke starts from empty (the host's stale input buffer is not used) */
static int field_empty(const char *tag) {
    char a[96], b2[96], c[96];
    field_views(tag, a, b2, c);
    return a[0] == 0 && b2[0] == 0;
}
static int click_field(void) {
    gv_rect r;
    gv_node_id fn = gv_glon_host_view_node(h, (int)gint("block-at gallery/id 32 1"));
    if (!fn || gv_view_visible_rect(v, fn, &r) != 0) return -1;
    return click(r.x + 20, r.y + r.h / 2);
}
static void case_clear(void) {
    char a[96], b2[96], c[96];
    CHECK(click_text("Field") == 0 && click_text("Type here") == 0 && text("Hi") == 0);
    field_views("typed Hi", a, b2, c);
    CHECK(!strcmp(a, "Hi") && !strcmp(b2, "Hi"));
    CHECK(click_text("Clear") == 0);
    CHECK(field_empty("after Clear"));
    CHECK(key(9) == 0 && key(9) == 0);                  /* blur: focus moves on */
    CHECK(field_empty("after blur (Tab x2)"));
    CHECK(click_field() == 0);                          /* refocus */
    CHECK(field_empty("after refocus"));
    CHECK(click_text("Theme") == 0);                    /* rebuilds every widget */
    CHECK(click_text("Field") == 0);
    CHECK(field_empty("after theme + reselect"));
    CHECK(gv_glon_host_run(h, "[ gc/collect gc/collect ]") == 0);
    CHECK(render() == 0);
    CHECK(field_empty("after forced GC"));
    CHECK(presented_ok() == 0);
    CHECK(click_field() == 0 && text("z") == 0);
    field_views("after typing z", a, b2, c);
    CHECK(!strcmp(a, "z") && !strcmp(b2, "z"));        /* not "Hiz" */
    CHECK(key(8) == 0);
    CHECK(field_empty("after Backspace"));
}

static void case_profile(void) {
    const char *mb[] = { "[ gallery/show-source widget/flush ]", "[ gallery/sync-nav 3 0 widget/flush ]",
        "[ gallery/set-status \"x\" widget/flush ]",
        "[ vid/button gallery/theme gallery/id 37 0 0 100 30 \"B\" widget/flush ]",
        "[ vid/label gallery/theme gallery/id 37 0 0 100 30 \"L\" widget/flush ]",
        "[ vid/field gallery/theme gallery/id 37 0 0 100 30 \"F\" 0 widget/flush ]",
        "[ view/rect 5 1 2 3 4 widget/flush ]", "[ widget/flush ]",
        "[ gallery/clear-preview-ids 12 widget/flush ]",
        "[ s/from-int 5 ]", "[ s/from-int 5000 ]", "[ s/raw-cat \"ab\" \"cd\" ]", "[ s/+ \"ab\" \"cd\" ]",
        "[ view/send \"x 1\" ]", "[ block-at gallery/theme 9 ]", "[ view/next-id ]",
        "[ vid/fg-color gallery/theme 5 9 ]", "[ 1 ]", "[ + 1 2 ]", "[ f: func [a] [ a ] f 1 ]", NULL };
    for (int k = 0; mb[k]; k++) {
        double m0 = ms_now();
        for (int i = 0; i < 5; i++) { gv_glon_host_run(h, mb[k]); gv_glon_host_deliver(h); }
        printf("  %-70s %.1f ms\n", mb[k], (ms_now() - m0) / 5);
    }
    double m0 = ms_now();
    gv_glon_host_run(h, "[ gallery/select 6 gallery/src-field widget/flush ]"); gv_glon_host_deliver(h);
    double m1 = ms_now();
    gv_glon_host_bind(h);
    double m2 = ms_now();
    render();
    printf("  select %.1f ms, bind %.1f, frame %.1f\n", m1 - m0, m2 - m1, ms_now() - m2);
}

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : "..";
    const char *which = argc > 2 ? argv[2] : "launch";
    if (setup(root) != 0) { fprintf(stderr, "setup failed\n"); return 2; }
    if (!strcmp(which, "launch")) case_launch(argc > 3 ? argv[3] : NULL);
    else if (!strcmp(which, "nav")) case_nav();
    else if (!strcmp(which, "type")) case_type();
    else if (!strcmp(which, "hover")) case_hover();
    else if (!strcmp(which, "theme")) case_theme();
    else if (!strcmp(which, "resize")) case_resize();
    else if (!strcmp(which, "checks")) case_checks();
    else if (!strcmp(which, "keys")) case_keys();
    else if (!strcmp(which, "perf")) case_perf();
    else if (!strcmp(which, "profile")) case_profile();
    else if (!strcmp(which, "clear")) case_clear();
    else { fprintf(stderr, "unknown case %s\n", which); return 2; }
    CHECK(!gv_glon_host_error(h));
    if (failures) { fprintf(stderr, "test_wg_runner %s: %d failure(s)\n", which, failures); return 1; }
    printf("test_wg_runner %s: ok\n", which);
    return 0;
}
