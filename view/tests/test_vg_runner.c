/* test_vg_runner.c -- Checkpoint B: one VID-styled button and one editable
 * field built on GUI-3 behaviour (vid_gui3.glon), driven through the
 * interactive SDL runner's path.
 *
 * SDL events are pushed into the SDL backend (dummy video driver), read back
 * with poll_event, coalesced and routed with gv_glon_host_dispatch, exactly
 * as glon_demo --vocab-vid does; frames are drawn and presented as the
 * runner draws them.  Checks use View geometry, Glon state and the pixels
 * presented in the window.
 *
 * Usage: test_vg_runner ROOT CASE
 *   look hover press edit keys disabled theme gc */
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
#define W 480
#define H 240

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
static __attribute__((unused)) int key(int sym) {
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
static __attribute__((unused)) int click_text(const char *t) {
    int x, y;
    if (centre_of_text(t, &x, &y) != 0) { fprintf(stderr, "no visible text %s\n", t); return -1; }
    return click(x, y);
}


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
    if (gv_backend_sdl_save_png(b, "/tmp/vg-runner-frame.png") != 0) return -1;
    long bad = window_vs_png("/tmp/vg-runner-frame.png", s);
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


static int keym(int sym, int mod) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = SDL_KEYDOWN; e.key.keysym.sym = sym; e.key.keysym.mod = (Uint16)mod; e.key.state = SDL_PRESSED;
    SDL_PushEvent(&e);
    return pump();
}
#define K_LEFT 1073741904
#define K_HOME 1073741898
static int setup(const char *root) {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    v = gv_view_new(W, H);
    b = gv_backend_sdl();
    gv_backend_config cfg; memset(&cfg, 0, sizeof cfg);
    cfg.width = W; cfg.height = H; cfg.title = "vg";
    if (!v || !b || b->init(b, &cfg) != 0 || b->create_window(b, W, H, "vg") != 0) return -1;
    h = gv_glon_host_new(v, b);
    /* the runner's --vid-gui3 path */
    if (!h || gv_glon_host_load_vid_gui3_app(h, root, "view/tests/fixtures/vg_app.glon") != 0) return -1;
    if (gv_glon_host_build_app(h, NULL) != 0 || gv_glon_host_bind(h) != 0 || gv_glon_host_error(h)) return -1;
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    return render();
}
static int node_of(const char *key) { char e[64]; snprintf(e, sizeof e, "gui3/find-node-by-key '%s", key); return (int)gint(e); }
static int rect_of_key(const char *key, gv_rect *r) {
    gv_node_id n = gv_glon_host_view_node(h, node_of(key));
    if (!n) return -1;
    if (v->layout_dirty) gv_view_layout(v);
    return gv_view_visible_rect(v, n, r);
}
/* theme colour slot idx of the current theme */
static void slot(int idx, int *rgb) {
    char e[64];
    for (int k = 0; k < 3; k++) { snprintf(e, sizeof e, "block-at vg/theme %d", idx + k); rgb[k] = (int)gint(e); }
}
static int same(const int *a, const int *bb) { return a[0] == bb[0] && a[1] == bb[1] && a[2] == bb[2]; }
static int surface_is(const char *key, int idx) {
    gv_rect r; int px[3], want[3];
    if (rect_of_key(key, &r) != 0) return 0;
    CHECK(render() == 0);
    pixel(r.x + 4, r.y + 4, px);
    slot(idx, want);
    if (!same(px, want)) fprintf(stderr, "%s surface %d,%d,%d, slot %d is %d,%d,%d\n", key, px[0], px[1], px[2], idx, want[0], want[1], want[2]);
    return same(px, want);
}
static int edge_is(const char *key, int idx) {
    gv_rect r; int px[3], want[3];
    if (rect_of_key(key, &r) != 0) return 0;
    CHECK(render() == 0);
    pixel(r.x, r.y + r.h / 2, px);
    slot(idx, want);
    if (!same(px, want)) fprintf(stderr, "%s edge %d,%d,%d, slot %d is %d,%d,%d\n", key, px[0], px[1], px[2], idx, want[0], want[1], want[2]);
    return same(px, want);
}
static int ring_is(const char *key, int idx) {
    gv_rect r; int px[3], want[3];
    if (rect_of_key(key, &r) != 0) return 0;
    CHECK(render() == 0);
    pixel(r.x - 2, r.y + r.h / 2, px);
    slot(idx, want);
    return same(px, want);
}
static int acts(void) { return (int)gint("block-at gui3/app 0"); }
static void last(char *k, int cap) { gstr("block-at gui3/app 1", k, cap); }
static void ftext(char *t, int cap) { gstr("t/text", t, cap); }
static void centre_key(const char *key, int *x, int *y) { gv_rect r; CHECK(rect_of_key(key, &r) == 0); *x = r.x + r.w / 2; *y = r.y + r.h / 2; }

/* B1: the faces carry the WG-3 theme's colours (the slots VID's own widgets use) */
static void case_look(void) {
    CHECK(presented_ok() == 0);
    CHECK(surface_is("save", 13));        /* primary: accent */
    CHECK(surface_is("cancel", 109));     /* secondary: control surface */
    CHECK(edge_is("cancel", 17));         /* border */
    CHECK(edge_is("save", 45));           /* primary border */
    CHECK(surface_is("name", 21));        /* input surface */
    CHECK(edge_is("name", 17));
    CHECK(text_node("Save") && text_node("Cancel") && text_node("Name"));
    int px[3], want[3];
    pixel(2, 2, px); slot(5, want);
    CHECK(same(px, want));                /* panel (style vg-panel, from slot 5) */
    printf("look: accent / control / input surfaces and borders come from theme slots 13/109/21/17/45\n");
}

/* B2: GUI-3 hover drives the VID look */
static void case_hover(void) {
    int x, y;
    centre_key("cancel", &x, &y);
    push_motion(x, y); CHECK(pump() == 0);
    CHECK(surface_is("cancel", 113));
    centre_key("save", &x, &y);
    push_motion(x, y); CHECK(pump() == 0);
    CHECK(surface_is("save", 41) && surface_is("cancel", 109));
    push_motion(5, 5); CHECK(pump() == 0);
    CHECK(surface_is("save", 13));
    CHECK(presented_ok() == 0);
    printf("hover: control 109->113, accent 13->41, back on leave\n");
}

/* B3: GUI-3 pointer capture: pressed look, one activation on release,
 * none when released outside */
static void case_press(void) {
    char k[64]; int x, y;
    centre_key("save", &x, &y);
    push_motion(x, y); push_button(SDL_MOUSEBUTTONDOWN, x, y); CHECK(pump() == 0);
    CHECK(surface_is("save", 45));
    CHECK(acts() == 0);
    push_button(SDL_MOUSEBUTTONUP, x, y); CHECK(pump() == 0);
    CHECK(acts() == 1);
    CHECK(surface_is("save", 41));         /* released over it: hover, not pressed */
    last(k, sizeof k); CHECK(strstr(k, "save") != NULL);
    CHECK(click(x, y) == 0);
    CHECK(acts() == 2);
    /* press on Cancel, release outside: no activation */
    centre_key("cancel", &x, &y);
    push_motion(x, y); push_button(SDL_MOUSEBUTTONDOWN, x, y); CHECK(pump() == 0);
    push_motion(5, 5); push_button(SDL_MOUSEBUTTONUP, 5, 5); CHECK(pump() == 0);
    CHECK(acts() == 2);
    CHECK(surface_is("cancel", 109));      /* not left pressed */
    /* hover still works after the clicks */
    push_motion(x, y); CHECK(pump() == 0);
    CHECK(surface_is("cancel", 113));
    printf("press: pressed slot 45 while held; 2 activations for 2 clicks; release outside ignored\n");
}

/* B4: GUI-3 editing in the VID-styled field */
static void case_edit(void) {
    char t[96]; int x, y;
    centre_key("name", &x, &y);
    CHECK(click(x, y) == 0);
    CHECK(gint("gui3/current-node") == node_of("name"));
    CHECK(ring_is("name", 105) && edge_is("name", 105));      /* focus ring */
    CHECK(text("ab") == 0);
    ftext(t, sizeof t); CHECK(!strcmp(t, "Nameab"));
    CHECK(keym(K_LEFT, 0) == 0 && keym(8, 0) == 0);          /* delete the a */
    ftext(t, sizeof t); CHECK(!strcmp(t, "Nameb"));
    CHECK(keym(K_HOME, 0) == 0 && text("X") == 0);
    ftext(t, sizeof t); CHECK(!strcmp(t, "XNameb"));
    CHECK(keym('a', 0x40) == 0 && text("Z") == 0);           /* Ctrl+A, type over */
    ftext(t, sizeof t); CHECK(!strcmp(t, "Z"));
    CHECK(text_node("Z") != 0);
    CHECK(presented_ok() == 0);
    printf("edit: click focus + ring; type, Left, Backspace, Home, Ctrl+A replace -> \"%s\"\n", t);
}

/* B5: keyboard focus moves into and between the styled faces; Enter activates */
static void case_keys(void) {
    char k[64];
    int saw_field = 0, saw_save = 0;
    for (int i = 0; i < 6 && !(saw_field && saw_save); i++) {
        CHECK(keym(9, 0) == 0);
        int f = (int)gint("gui3/current-node");
        if (f == node_of("name")) { saw_field = 1; CHECK(ring_is("name", 105)); }
        if (f == node_of("save")) {
            saw_save = 1;
            CHECK(ring_is("save", 105));
            CHECK(keym(13, 0) == 0);
            last(k, sizeof k); CHECK(strstr(k, "save") != NULL);
        }
    }
    CHECK(saw_field && saw_save && acts() == 1);
    printf("keys: Tab reached the field and Save (ring drawn); Enter activated Save\n");
}

/* B6: a disabled styled button looks disabled and ignores the pointer */
static void case_disabled(void) {
    int x, y;
    char e[64]; snprintf(e, sizeof e, "[ gui3/set-enabled %d 0 widget/flush ]", node_of("cancel"));
    CHECK(gv_glon_host_run(h, e) == 0 && gv_glon_host_deliver(h) == 0 && gv_glon_host_bind(h) == 0);
    CHECK(surface_is("cancel", 49) && edge_is("cancel", 125));
    centre_key("cancel", &x, &y);
    CHECK(click(x, y) == 0);
    CHECK(acts() == 0);
    printf("disabled: slots 49/125, click ignored\n");
}

/* B7: switching the theme restyles the same GUI-3 faces from the light theme */
static void case_theme(void) {
    char t[96]; int x, y;
    CHECK(gv_glon_host_run(h, "[ t/theme 'light widget/flush ]") == 0 && gv_glon_host_deliver(h) == 0);
    CHECK(gv_glon_host_bind(h) == 0);
    int want[3]; slot(109, want);
    CHECK(want[0] == 255 && want[1] == 255 && want[2] == 255);     /* light control surface */
    CHECK(surface_is("cancel", 109) && surface_is("save", 13) && surface_is("name", 21));
    centre_key("name", &x, &y);
    CHECK(click(x, y) == 0 && text("!") == 0);
    ftext(t, sizeof t); CHECK(!strcmp(t, "Name!"));
    long id0 = gint("view/id");
    CHECK(gv_glon_host_run(h, "[ t/theme 'dark widget/flush ]") == 0 && gv_glon_host_deliver(h) == 0 && gv_glon_host_bind(h) == 0);
    CHECK(gint("view/id") == id0);
    CHECK(surface_is("cancel", 109));
    CHECK(presented_ok() == 0);
    printf("theme: light then dark; colours follow the theme; view/id %ld kept\n", id0);
}

/* B8: forced collections keep the styled faces working */
static void case_gc(void) {
    char t[96]; int x, y;
    CHECK(gv_glon_host_run(h, "[ gc/collect gc/collect ]") == 0);
    centre_key("name", &x, &y);
    CHECK(click(x, y) == 0 && text("q") == 0);
    ftext(t, sizeof t); CHECK(!strcmp(t, "Nameq"));
    CHECK(gv_glon_host_run(h, "[ gc/collect ]") == 0);
    centre_key("save", &x, &y);
    CHECK(click(x, y) == 0 && acts() == 1);
    CHECK(surface_is("save", 41));                 /* still hovered */
    printf("gc: 3 collections; edit and activation still work\n");
}

int main(int argc, char **argv) {
    const char *root = argc > 1 ? argv[1] : "..";
    const char *which = argc > 2 ? argv[2] : "look";
    if (setup(root) != 0) { fprintf(stderr, "setup failed\n"); return 2; }
    if (!strcmp(which, "look")) case_look();
    else if (!strcmp(which, "hover")) case_hover();
    else if (!strcmp(which, "press")) case_press();
    else if (!strcmp(which, "edit")) case_edit();
    else if (!strcmp(which, "keys")) case_keys();
    else if (!strcmp(which, "disabled")) case_disabled();
    else if (!strcmp(which, "theme")) case_theme();
    else if (!strcmp(which, "gc")) case_gc();
    else { fprintf(stderr, "unknown case %s\n", which); return 2; }
    CHECK(!gv_glon_host_error(h));
    if (failures) { fprintf(stderr, "test_vg_runner %s: %d failure(s)\n", which, failures); return 1; }
    printf("test_vg_runner %s: ok\n", which);
    return 0;
}
