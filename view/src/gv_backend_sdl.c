#include "gv_backend.h"

#include <SDL.h>
#include <SDL_syswm.h>
#include <cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <errno.h>
#include <setjmp.h>
#include <jpeglib.h>

void *gv_native_control_create(void *parent, int kind, gv_rect rect,
                               const char *text);
void gv_native_control_destroy(void *ctrl);
int gv_native_control_set_text(void *ctrl, const char *text);
int gv_native_control_get_text(void *ctrl, char *buf, int cap);
int gv_native_control_set_focus(void *ctrl, int focus);

typedef struct gv_sdl {
    SDL_Window *window;
    SDL_Surface *window_surface;
    SDL_Surface *screen;
    cairo_surface_t *cairo_surface;
    cairo_t *cr;
    int width;
    int height;
    int headless;
    int shown;
    gv_event queue[128];
    int qhead;
    int qtail;
    char clipboard[4096];
    int clipboard_set;
    int cursor_zone;            /* GUI-P2: resize zone under the pointer */
    int app_cursor;             /* GUI-P2: shape asked for by the application */
    SDL_Cursor *col_cursor;     /* GUI-P2: horizontal resize (table column divider) */
    SDL_Cursor *cursors[10];    /* indexed by SDL_HitTestResult */
    int hit_test;               /* SDL_SetWindowHitTest accepted */
    int captured;               /* GUI-P2: pointer capture held for a drag */
    SDL_Cursor *arrow;          /* GUI-P2: explicit arrow on X11 (see gv_sdl_create_window) */
    int last_mx, last_my;       /* GUI-P2: the last motion seen (edge trace) */
    int edge_requested;         /* GUI-P2: the edge band was asked for (policy or GV_EDGE_RESIZE) */
    int policy_wslg;            /* GUI-P2: the WSLg defaults applied */
    int edge_cursor_shown;      /* GUI-P2: an edge-zone resize cursor is on show */
    /* GLON-INST-3B: several native windows.  SDL has one event queue for all
     * of them; each window's events are routed by SDL window id into its own
     * pending queue (gv_sdl_route).  A secondary window's close button is
     * GV_EVENT_CLOSE (the first window's stays GV_EVENT_QUIT). */
    int secondary;
    Uint32 window_id;
    SDL_Event pend[256];
    int phead, ptail;
} gv_sdl;

/* GLON-INST-3B: the open windows, by SDL window id */
#define GV_SDL_MAX_WINDOWS 64
static struct { Uint32 id; gv_sdl *x; } gv_sdl_windows[GV_SDL_MAX_WINDOWS];
static long gv_sdl_dropped;            /* events for a window no longer open */
static void gv_sdl_register(gv_sdl *x) {
    for (int i = 0; i < GV_SDL_MAX_WINDOWS; i++)
        if (!gv_sdl_windows[i].x) { gv_sdl_windows[i].id = x->window_id; gv_sdl_windows[i].x = x; return; }
}
static void gv_sdl_unregister(gv_sdl *x) {
    for (int i = 0; i < GV_SDL_MAX_WINDOWS; i++)
        if (gv_sdl_windows[i].x == x) { gv_sdl_windows[i].x = NULL; gv_sdl_windows[i].id = 0; }
    x->phead = x->ptail = 0;
}
static Uint32 gv_sdl_event_window(const SDL_Event *e) {
    switch (e->type) {
    case SDL_WINDOWEVENT: return e->window.windowID;
    case SDL_KEYDOWN: case SDL_KEYUP: return e->key.windowID;
    case SDL_TEXTINPUT: return e->text.windowID;
    case SDL_TEXTEDITING: return e->edit.windowID;
    case SDL_MOUSEMOTION: return e->motion.windowID;
    case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: return e->button.windowID;
    case SDL_MOUSEWHEEL: return e->wheel.windowID;
    default: return 0;
    }
}
/* the window an event belongs to: by its window id; no id (a synthetic
 * event, SDL_QUIT) or the first window's id: the first window; an id no
 * longer open: none (the event is dropped) */
static gv_sdl *gv_sdl_route(const SDL_Event *e) {
    Uint32 id = gv_sdl_event_window(e);
    gv_sdl *first = NULL;
    for (int i = 0; i < GV_SDL_MAX_WINDOWS; i++) {
        gv_sdl *w = gv_sdl_windows[i].x;
        if (!w) continue;
        if (id && gv_sdl_windows[i].id == id) return w;
        if (!w->secondary && !first) first = w;
    }
    if (!id) return first;
    for (int i = 0; i < GV_SDL_MAX_WINDOWS; i++) if (gv_sdl_windows[i].x == first && first && first->window_id == id) return first;
    return NULL;
}
static void gv_sdl_pend(gv_sdl *x, const SDL_Event *e) {
    int n = (int)(sizeof x->pend / sizeof x->pend[0]);
    if ((x->ptail + 1) % n == x->phead) { gv_sdl_dropped++; return; }   /* full: dropped */
    x->pend[x->ptail] = *e;
    x->ptail = (x->ptail + 1) % n;
}

/* GUI-P2: resizing from the window's edge.  The window manager draws (or
 * not) resize cursors on its own frame; inside the window, the outer
 * GV_EDGE_WIDTH pixels (default 6) are a resize zone too: the pointer shows the matching
 * resize cursor there, and a press starts a window-manager resize (SDL hit
 * test), so the edge behaves the same whatever the platform frame does. */
/* the band's width in pixels (corners twice as long): GV_EDGE_WIDTH,
 * 4..16, default 6.  One value, read by the cursor and the hit test alike */
static int gv_sdl_edge = 6;
int gv_backend_sdl_edge_width(void) { return gv_sdl_edge; }
int gv_backend_sdl_edge_zone(int w, int h, int x, int y) {
    if (x < 0 || y < 0 || x >= w || y >= h || w < 4 * gv_sdl_edge || h < 4 * gv_sdl_edge)
        return SDL_HITTEST_NORMAL;
    int l = x < gv_sdl_edge, r = x >= w - gv_sdl_edge;
    int t = y < gv_sdl_edge, bt = y >= h - gv_sdl_edge;
    /* corners: a little larger, as desktops do */
    int lc = x < 2 * gv_sdl_edge, rc = x >= w - 2 * gv_sdl_edge;
    int tc = y < 2 * gv_sdl_edge, bc = y >= h - 2 * gv_sdl_edge;
    if ((t && lc) || (l && tc)) return SDL_HITTEST_RESIZE_TOPLEFT;
    if ((t && rc) || (r && tc)) return SDL_HITTEST_RESIZE_TOPRIGHT;
    if ((bt && lc) || (l && bc)) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    if ((bt && rc) || (r && bc)) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    if (t) return SDL_HITTEST_RESIZE_TOP;
    if (bt) return SDL_HITTEST_RESIZE_BOTTOM;
    if (l) return SDL_HITTEST_RESIZE_LEFT;
    if (r) return SDL_HITTEST_RESIZE_RIGHT;
    return SDL_HITTEST_NORMAL;
}
static int gv_sdl_trace_edge(void) { const char *t = getenv("GV_TRACE_EDGE"); return t && t[0] == '1'; }
static SDL_HitTestResult gv_sdl_hit_test(SDL_Window *win, const SDL_Point *p, void *data) {
    (void)data;
    int w = 0, h = 0;
    SDL_GetWindowSize(win, &w, &h);
    int z = gv_backend_sdl_edge_zone(w, h, p->x, p->y);
    /* GV_TRACE_EDGE=1: SDL asks this at a press; a resize zone makes SDL
     * hand the drag to the window manager (_NET_WM_MOVERESIZE on X11) */
    if (gv_sdl_trace_edge())
        fprintf(stderr, "[edge] %u hit test at %d,%d of %dx%d: zone %d%s\n", (unsigned)SDL_GetTicks(), p->x, p->y, w, h, z,
                z ? " -> resize handed to the window manager" : " (normal: the application gets the press)");
    return (SDL_HitTestResult)z;
}
/* GV_EDGE_CURSOR selects how the edge zones change the pointer:
 *   unset      the platform default: "system" under WSLg (see
 *              gv_sdl_edge_policy), else not at all
 *   0          not at all: the platform's pointer is left alone
 *   system     SDL system cursors (themed, via libXcursor on X11); back to
 *              SDL's default cursor outside the zones
 *   bitmap     monochrome cursors made here (core X cursors on X11),
 *              including the normal arrow, so no "None" cursor is used */
static int gv_sdl_wslg_default;   /* set by gv_sdl_edge_policy at window creation */
static int gv_sdl_cursor_mode(void) {
    const char *m = getenv("GV_EDGE_CURSOR");
    if (!m) return gv_sdl_wslg_default ? 1 : 0;
    if (!strcmp(m, "system")) return 1;
    if (!strcmp(m, "bitmap")) return 2;
    return 0;
}
/* 16x16 monochrome cursors: rows of '#' (black), '.' (white), ' ' (clear) */
static const char *gv_sdl_bitmaps[5][16] = {
    { "#.              ", "##.             ", "#.#.            ", "#..#.           ", "#...#.          ",
      "#....#.         ", "#.....#.        ", "#......#.       ", "#.......#.      ", "#....#####.     ",
      "#..#.#.         ", "#.# #..#.       ", "##.  #..#.      ", "#.    #..#.     ", "      ####      ", "                " },
    { "                ", "                ", "                ", "                ", "   #        #   ",
      "  ##        ##  ", " #.##########.# ", "#..............#", " #.##########.# ", "  ##        ##  ",
      "   #        #   ", "                ", "                ", "                ", "                ", "                " },
    { "       #        ", "      #.#       ", "     #...#      ", "    #.....#     ", "      #.#       ",
      "      #.#       ", "      #.#       ", "      #.#       ", "      #.#       ", "      #.#       ",
      "      #.#       ", "    #.....#     ", "     #...#      ", "      #.#       ", "       #        ", "                " },
    { "######          ", "#....#          ", "#...#           ", "#..#.#          ", "#.# #.#         ",
      "##   #.#        ", "      #.#       ", "       #.#      ", "        #.#     ", "         #.#   #", 
      "          #.# ##", "           #.#.#", "          #...# ", "         #....# ", "         ###### ", "                " },
    { "          ######", "          #....#", "           #...#", "          #.#..#", "         #.# #.#",
      "        #.#   ##", "       #.#      ", "      #.#       ", "     #.#        ", "#   #.#         ",
      "## #.#          ", "#.#.#           ", " #...#          ", " #....#         ", " ######         ", "                " },
};
static SDL_Cursor *gv_sdl_bitmap_cursor(int which, int hx, int hy) {
    Uint8 data[32], mask[32];
    memset(data, 0, sizeof data); memset(mask, 0, sizeof mask);
    for (int y = 0; y < 16; y++)
        for (int xx = 0; xx < 16; xx++) {
            char c = gv_sdl_bitmaps[which][y][xx];
            int bit = 0x80 >> (xx & 7), i = y * 2 + xx / 8;
            if (c == '#') { data[i] |= bit; mask[i] |= bit; }
            else if (c == '.') mask[i] |= bit;
        }
    return SDL_CreateCursor(data, mask, 16, 16, hx, hy);
}

/* the normal pointer: the explicit arrow where one was made (X11), else
 * SDL's default */
static void gv_sdl_normal_cursor(gv_sdl *x) {
    if (x->arrow) SDL_SetCursor(x->arrow);
    else if (SDL_GetDefaultCursor()) SDL_SetCursor(SDL_GetDefaultCursor());
}

/* The edge-cursor decision, one pure function (tested: test_gui_p2
 * edge-cursor-decision).  A resize cursor is shown for a resize zone only
 * when a press there resizes (the hit test is installed) and no drag is
 * under way (a capture held, or a button down: the press that started it was
 * no resize, and the hit test is asked only at a press).  Answers the zone
 * whose cursor to show (SDL_HITTEST_NORMAL: the normal pointer) and why. */
int gv_backend_sdl_edge_cursor_decision(int hit_test, int zone, int captured, int buttons, const char **why) {
    const char *w = "normal: no resize zone";
    int show = zone;
    if (zone != SDL_HITTEST_NORMAL) {
        if (!hit_test) { show = SDL_HITTEST_NORMAL; w = "held back: no hit test (a press there would not resize)"; }
        else if (captured) { show = SDL_HITTEST_NORMAL; w = "held back: a pointer capture (a drag) is held"; }
        else if (buttons) { show = SDL_HITTEST_NORMAL; w = "held back: SDL reports a mouse button down"; }
        else w = "resize zone";
    }
    if (why) *why = w;
    return show;
}

static void gv_sdl_set_cursor_zone(gv_sdl *x, int zone) {
    if (zone == x->cursor_zone) return;
    x->cursor_zone = zone;
    int mode = gv_sdl_cursor_mode();
    const char *why = "";
    Uint32 buttons = SDL_GetMouseState(NULL, NULL);
    int shown_zone = gv_backend_sdl_edge_cursor_decision(1, zone, x->captured, buttons != 0, &why);
    /* GV_TRACE_CURSOR=1: every decision and its reason (INTENT-4B-3: a
     * resize cursor held back by a capture or a button used to say nothing) */
    if (getenv("GV_TRACE_CURSOR"))
        fprintf(stderr, "[cursor] zone %d -> %s (capture %d, buttons 0x%x, hit test %s, cursor mode %d)\n", zone, why,
                x->captured, (unsigned)buttons, x->hit_test ? "on" : "off", mode);
    zone = shown_zone;
    /* a resize cursor only where a press resizes: with the hit test
     * installed (GV_EDGE_RESIZE=1, a driver that supports it).  Without it
     * the zone is still tracked, but the pointer stays normal (an arrow
     * that promised a resize there would lie) */
    if (!x->hit_test) {
        if (x->edge_cursor_shown) { gv_sdl_normal_cursor(x); x->edge_cursor_shown = 0; }
        if (getenv("GV_TRACE_CURSOR") && mode)
            fprintf(stderr, "[cursor] zone %d: no resize cursor (no hit test: a press there would not resize)\n", zone);
        return;
    }
    /* GV_TRACE_CURSOR=1: which zone, and whether SDL could make and set
     * the cursor (diagnosing platforms that do not show it) */
    int trace = getenv("GV_TRACE_CURSOR") != NULL;
    SDL_SystemCursor id = SDL_SYSTEM_CURSOR_ARROW;   /* replaced below for a zone */
    switch (zone) {
    case SDL_HITTEST_RESIZE_TOPLEFT: case SDL_HITTEST_RESIZE_BOTTOMRIGHT: id = SDL_SYSTEM_CURSOR_SIZENWSE; break;
    case SDL_HITTEST_RESIZE_TOPRIGHT: case SDL_HITTEST_RESIZE_BOTTOMLEFT: id = SDL_SYSTEM_CURSOR_SIZENESW; break;
    case SDL_HITTEST_RESIZE_TOP: case SDL_HITTEST_RESIZE_BOTTOM: id = SDL_SYSTEM_CURSOR_SIZENS; break;
    case SDL_HITTEST_RESIZE_LEFT: case SDL_HITTEST_RESIZE_RIGHT: id = SDL_SYSTEM_CURSOR_SIZEWE; break;
    default: break;
    }
    if (mode == 0) {
        if (trace) fprintf(stderr, "[cursor] zone %d (pointer left to the platform: GV_EDGE_CURSOR unset)\n", zone);
        return;
    }
    if (zone <= 0 || zone >= 10) {                  /* back to the normal pointer */
        if (mode == 2) {
            if (!x->cursors[0]) x->cursors[0] = gv_sdl_bitmap_cursor(0, 0, 0);
            if (x->cursors[0]) SDL_SetCursor(x->cursors[0]);
        } else gv_sdl_normal_cursor(x);
        x->edge_cursor_shown = 0;
        if (trace) fprintf(stderr, "[cursor] normal pointer (%s)\n", mode == 2 ? "bitmap arrow" : x->arrow ? "explicit arrow" : "SDL default");
        return;
    }
    /* SDL 2.0.20's X11 driver makes both diagonal system cursors XC_fleur,
     * the four-way MOVE arrow (X11_CreateSystemCursor): at a corner the
     * system mode showed a move cursor, not a diagonal resize.  Under X11 the
     * diagonals use the bitmap shapes instead (the horizontal and vertical
     * system cursors are right: XC_sb_h/v_double_arrow) */
    int diagonal = id == SDL_SYSTEM_CURSOR_SIZENWSE || id == SDL_SYSTEM_CURSOR_SIZENESW;
    int x11 = !strcmp(SDL_GetCurrentVideoDriver(), "x11");
    if (!x->cursors[zone]) {
        if (mode == 2 || (diagonal && x11)) {
            int which = id == SDL_SYSTEM_CURSOR_SIZEWE ? 1 : id == SDL_SYSTEM_CURSOR_SIZENS ? 2
                      : id == SDL_SYSTEM_CURSOR_SIZENWSE ? 3 : 4;
            x->cursors[zone] = gv_sdl_bitmap_cursor(which, 7, 7);
        } else x->cursors[zone] = SDL_CreateSystemCursor(id);
    }
    if (x->cursors[zone]) { SDL_SetCursor(x->cursors[zone]); x->edge_cursor_shown = 1; }
    if (trace)
        fprintf(stderr, "[cursor] resize zone %d: system cursor %d %s (video driver %s, hit test %s)\n", zone, (int)id,
                x->cursors[zone] && SDL_GetCursor() == x->cursors[zone] ? "set" : "NOT set",
                SDL_GetCurrentVideoDriver(), x->hit_test ? "on" : "off");
}
/* GUI-P2: the application's pointer shape: the themed system horizontal
 * resize cursor over a table column divider, SDL's default otherwise.
 * GV_TABLE_CURSOR=0 leaves the pointer alone (the shape is still recorded). */
static void gv_sdl_set_cursor(gv_backend *b, int kind) {
    gv_sdl *x = (gv_sdl *)b->impl;
    if (!x || kind == x->app_cursor) return;
    x->app_cursor = kind;
    const char *env = getenv("GV_TABLE_CURSOR");
    if (env && !strcmp(env, "0")) return;
    if (kind == GV_CURSOR_COL_RESIZE) {
        if (!x->col_cursor) x->col_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
        if (x->col_cursor) SDL_SetCursor(x->col_cursor);
    } else {
        gv_sdl_normal_cursor(x);
    }
    if (getenv("GV_TRACE_CURSOR"))
        fprintf(stderr, "[cursor] application shape %d (%s)\n", kind,
                kind == GV_CURSOR_COL_RESIZE ? (x->col_cursor ? "column resize" : "column resize NOT made") : "default");
}
int gv_backend_sdl_app_cursor(gv_backend *b) {
    return b && b->impl ? ((gv_sdl *)b->impl)->app_cursor : -1;
}
int gv_backend_sdl_edge_cursor_shown(gv_backend *b) {
    return b && b->impl ? ((gv_sdl *)b->impl)->edge_cursor_shown : 0;
}
int gv_backend_sdl_cursor_zone(gv_backend *b) {
    return b && b->impl ? ((gv_sdl *)b->impl)->cursor_zone : -1;
}
int gv_backend_sdl_hit_test_active(gv_backend *b) {
    return b && b->impl ? ((gv_sdl *)b->impl)->hit_test : 0;
}

static void gv_sdl_free_surfaces(gv_sdl *x) {
    if (x->cr) {
        cairo_destroy(x->cr);
        x->cr = NULL;
    }
    if (x->cairo_surface) {
        cairo_surface_destroy(x->cairo_surface);
        x->cairo_surface = NULL;
    }
    if (x->screen) {
        SDL_FreeSurface(x->screen);
        x->screen = NULL;
    }
    x->window_surface = NULL;
}

static int gv_sdl_make_surfaces(gv_sdl *x) {
    gv_sdl_free_surfaces(x);
    /* Cairo's ARGB32 is a native-endian 32-bit word 0xAARRGGBB, which is
     * SDL_PIXELFORMAT_ARGB8888.  (SDL_PIXELFORMAT_ARGB32 means the byte order
     * A,R,G,B in memory -- BGRA8888 on little-endian machines -- so a real
     * window showed alpha as blue and red/green swapped.)  The frame is
     * copied, not alpha-blended, onto the window: blending let earlier
     * frames show through. */
    x->screen = SDL_CreateRGBSurfaceWithFormat(0, x->width, x->height, 32,
                                               SDL_PIXELFORMAT_ARGB8888);
    if (!x->screen) return -1;
    SDL_SetSurfaceBlendMode(x->screen, SDL_BLENDMODE_NONE);
    x->cairo_surface = cairo_image_surface_create_for_data(
        (unsigned char *)x->screen->pixels, CAIRO_FORMAT_ARGB32,
        x->width, x->height, x->screen->pitch);
    if (cairo_surface_status(x->cairo_surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(x->cairo_surface);
        x->cairo_surface = NULL;
        SDL_FreeSurface(x->screen);
        x->screen = NULL;
        return -1;
    }
    x->cr = cairo_create(x->cairo_surface);
    if (!x->cr) {
        gv_sdl_free_surfaces(x);
        return -1;
    }
    x->window_surface = SDL_GetWindowSurface(x->window);
    return x->window_surface ? 0 : -1;
}

static void gv_sdl_set_source(cairo_t *cr, gv_color c) {
    cairo_set_source_rgba(cr, (double)c.r / 255.0, (double)c.g / 255.0,
                          (double)c.b / 255.0, (double)c.a / 255.0);
}

static int gv_sdl_init(gv_backend *b, const gv_backend_config *cfg) {
    (void)b;
    if (cfg && cfg->headless) {
        SDL_SetHint("SDL_VIDEODRIVER", "dummy");
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "gv_sdl: SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    return 0;
}

/* GUI-P2: pointer capture, only while the host's drag asks for it.
 * (Captured on every press until 5057b73: the grab stopped WSLg's frame
 * resizing; manually verified with GV_CAPTURE=0 on 2026-11-25.)
 * GV_CAPTURE=0 refuses it altogether (a diagnostic switch). */
static int gv_sdl_set_capture(gv_backend *b, int on) {
    if (!b || !b->impl) return -1;
    gv_sdl *x = (gv_sdl *)b->impl;
    on = on ? 1 : 0;
    const char *env = getenv("GV_CAPTURE");
    if (on && env && !strcmp(env, "0")) on = 0;
    if (on == x->captured) return 0;
    x->captured = on;
    int rc = x->window ? SDL_CaptureMouse(on ? SDL_TRUE : SDL_FALSE) : 0;
    if (getenv("GV_TRACE_WINDOW"))
        fprintf(stderr, "[window] capture %s%s\n", on ? "taken" : "released", rc ? " (not supported by this driver)" : "");
    return 0;
}
int gv_backend_sdl_captured(gv_backend *b) {
    return b && b->impl ? ((gv_sdl *)b->impl)->captured : 0;
}

static void gv_sdl_shutdown(gv_backend *b) {
    if (!b || !b->impl) return;
    gv_sdl *x = (gv_sdl *)b->impl;
    gv_sdl_set_capture(b, 0);
    gv_sdl_free_surfaces(x);
    for (int i = 0; i < 10; i++)
        if (x->cursors[i]) { SDL_FreeCursor(x->cursors[i]); x->cursors[i] = NULL; }
    if (x->col_cursor) { SDL_FreeCursor(x->col_cursor); x->col_cursor = NULL; }
    if (x->arrow) { SDL_SetCursor(SDL_GetDefaultCursor()); SDL_FreeCursor(x->arrow); x->arrow = NULL; }
    gv_sdl_unregister(x);
    if (x->window) {
        SDL_DestroyWindow(x->window);
        x->window = NULL;
    }
    SDL_Quit();
}

static void gv_sdl_destroy(gv_backend *b) {
    gv_sdl_shutdown(b);
    free(b);
}

/* ---- window diagnostics (GV_TRACE_WINDOW=1) ------------------------------
 * Whether the native window can be resized is decided outside Glon: by the
 * window flags SDL gives it and, on X11, by the size hints the window
 * manager reads (WM_NORMAL_HINTS: a minimum equal to the maximum means a
 * fixed size) and the actions it then allows (_NET_WM_ALLOWED_ACTIONS,
 * set after mapping), and by any pointer grab the application holds while
 * the user reaches for the frame.  This reports them, and every native
 * window event, without changing anything.  Xlib is opened at run time
 * (no link dependency), only when asked for. */
static int gv_sdl_trace_window(void) { const char *t = getenv("GV_TRACE_WINDOW"); return t && t[0] == '1'; }
static const char *gv_sdl_window_event_name(int e) {
    switch (e) {
    case SDL_WINDOWEVENT_SHOWN: return "shown";
    case SDL_WINDOWEVENT_HIDDEN: return "hidden";
    case SDL_WINDOWEVENT_EXPOSED: return "exposed";
    case SDL_WINDOWEVENT_MOVED: return "moved";
    case SDL_WINDOWEVENT_RESIZED: return "resized";
    case SDL_WINDOWEVENT_SIZE_CHANGED: return "size-changed";
    case SDL_WINDOWEVENT_MINIMIZED: return "minimized";
    case SDL_WINDOWEVENT_MAXIMIZED: return "maximized";
    case SDL_WINDOWEVENT_RESTORED: return "restored";
    case SDL_WINDOWEVENT_ENTER: return "enter";
    case SDL_WINDOWEVENT_LEAVE: return "leave";
    case SDL_WINDOWEVENT_FOCUS_GAINED: return "focus-gained";
    case SDL_WINDOWEVENT_FOCUS_LOST: return "focus-lost";
    case SDL_WINDOWEVENT_CLOSE: return "close";
    case SDL_WINDOWEVENT_TAKE_FOCUS: return "take-focus";
    /* SDL sends it right after a press its hit test handed to the window
     * manager (_NET_WM_MOVERESIZE on X11); the press itself never reaches us */
    case SDL_WINDOWEVENT_HIT_TEST: return "hit-test";
    default: return "other";
    }
}
/* the X11 side: size hints and allowed actions ("" when not X11) */
static void gv_sdl_x11_report(SDL_Window *win, char *out, int cap) {
    out[0] = 0;
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(win, &info) || info.subsystem != SDL_SYSWM_X11) return;
    static void *lib;
    if (!lib) lib = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (!lib) { snprintf(out, (size_t)cap, " x11: libX11 not loadable"); return; }
    Status (*get_hints)(Display *, Window, XSizeHints *, long *) =
        (Status (*)(Display *, Window, XSizeHints *, long *))dlsym(lib, "XGetWMNormalHints");
    Atom (*intern)(Display *, const char *, Bool) = (Atom (*)(Display *, const char *, Bool))dlsym(lib, "XInternAtom");
    int (*get_prop)(Display *, Window, Atom, long, long, Bool, Atom, Atom *, int *, unsigned long *,
                    unsigned long *, unsigned char **) =
        (int (*)(Display *, Window, Atom, long, long, Bool, Atom, Atom *, int *, unsigned long *,
                 unsigned long *, unsigned char **))dlsym(lib, "XGetWindowProperty");
    char *(*atom_name)(Display *, Atom) = (char *(*)(Display *, Atom))dlsym(lib, "XGetAtomName");
    int (*xfree)(void *) = (int (*)(void *))dlsym(lib, "XFree");
    if (!get_hints || !intern || !get_prop || !atom_name || !xfree) return;
    Display *d = info.info.x11.display;
    Window w = info.info.x11.window;
    XSizeHints sh;
    long supplied = 0;
    int n = 0;
    memset(&sh, 0, sizeof sh);
    if (get_hints(d, w, &sh, &supplied))
        n += snprintf(out + n, (size_t)(cap - n), " x11: hints min %s %dx%d max %s %dx%d%s",
                      (sh.flags & PMinSize) ? "set" : "unset", sh.min_width, sh.min_height,
                      (sh.flags & PMaxSize) ? "set" : "unset", sh.max_width, sh.max_height,
                      ((sh.flags & PMinSize) && (sh.flags & PMaxSize) && sh.min_width == sh.max_width &&
                       sh.min_height == sh.max_height) ? " (FIXED SIZE)" : "");
    else
        n += snprintf(out + n, (size_t)(cap - n), " x11: no WM_NORMAL_HINTS");
    Atom type = 0; int fmt = 0; unsigned long count = 0, after = 0; unsigned char *data = NULL;
    Atom allowed = intern(d, "_NET_WM_ALLOWED_ACTIONS", False);
    if (get_prop(d, w, allowed, 0, 64, False, XA_ATOM, &type, &fmt, &count, &after, &data) == Success && data) {
        n += snprintf(out + n, (size_t)(cap - n), "; allowed actions:");
        for (unsigned long i = 0; i < count && n < cap - 40; i++) {
            char *nm = atom_name(d, ((Atom *)data)[i]);
            if (nm) { n += snprintf(out + n, (size_t)(cap - n), " %s", nm + (strncmp(nm, "_NET_WM_ACTION_", 15) ? 0 : 15)); xfree(nm); }
        }
        xfree(data);
    } else {
        n += snprintf(out + n, (size_t)(cap - n), "; allowed actions: none set (yet)");
    }
}
/* one line about the window: driver, flags, sizes, X11 hints, capture */
int gv_backend_sdl_window_report(gv_backend *b, char *out, int cap) {
    if (!b || !b->impl || !out || cap <= 0) return -1;
    gv_sdl *x = (gv_sdl *)b->impl;
    out[0] = 0;
    if (!x->window) return -1;
    Uint32 f = SDL_GetWindowFlags(x->window);
    int w = 0, h = 0, mnw = 0, mnh = 0, mxw = 0, mxh = 0;
    SDL_GetWindowSize(x->window, &w, &h);
    SDL_GetWindowMinimumSize(x->window, &mnw, &mnh);
    SDL_GetWindowMaximumSize(x->window, &mxw, &mxh);
    int n = snprintf(out, (size_t)cap, "driver %s; %s%s%s%s%s; size %dx%d; min %dx%d; max %dx%d (0 = none); hit test %s "
                     "(edge %d px, %s, policy %s, edge cursor %s); client cursor %s",
                     SDL_GetCurrentVideoDriver(), (f & SDL_WINDOW_RESIZABLE) ? "resizable" : "NOT RESIZABLE",
                     (f & SDL_WINDOW_HIDDEN) ? " hidden" : " shown", (f & SDL_WINDOW_MAXIMIZED) ? " maximized" : "",
                     (f & SDL_WINDOW_MOUSE_CAPTURE) ? " MOUSE CAPTURED" : " no capture",
                     (f & SDL_WINDOW_INPUT_GRABBED) ? " INPUT GRABBED" : "",
                     w, h, mnw, mnh, mxw, mxh, x->hit_test ? "on" : "off", gv_sdl_edge,
                     x->edge_requested ? "requested" : "not requested", x->policy_wslg ? "WSLg" : "default",
                     gv_sdl_cursor_mode() == 1 ? "system" : gv_sdl_cursor_mode() == 2 ? "bitmap" : "off",
                     x->arrow ? "explicit arrow" : "SDL default (on X11: inherits the frame's)");
    if (n > 0 && n < cap) gv_sdl_x11_report(x->window, out + n, cap - n);
    return 0;
}
static void gv_sdl_trace(gv_backend *b, const char *when) {
    if (!gv_sdl_trace_window()) return;
    char r[1024];
    gv_backend_sdl_window_report(b, r, sizeof r);
    fprintf(stderr, "[window] %s: %s\n", when, r);
}

/* GUI-P2: the edge band's platform default.  Under WSLg (X11 inside WSL),
 * the window manager's own border is a few pixels wide (Weston's 8-pixel
 * grip, reachable only where our window does not cover it); a 12-pixel band
 * inside the window, with resize cursors, is the preferred resize target
 * there (manually verified by the user, 2026-11-25).  Elsewhere the band is
 * off unless asked for.  GV_ASSUME_WSLG=1 takes the WSLg branch on any
 * driver (tests); GV_EDGE_RESIZE / GV_EDGE_WIDTH / GV_EDGE_CURSOR override.
 * Answers whether the hit test is wanted by default. */
static int gv_sdl_is_wslg(void) {
    const char *a = getenv("GV_ASSUME_WSLG");
    if (a) return a[0] == '1';
    return !strcmp(SDL_GetCurrentVideoDriver(), "x11") &&
           (getenv("WSL_DISTRO_NAME") || access("/mnt/wslg", F_OK) == 0);
}
static int gv_sdl_edge_policy(gv_sdl *x) {
    gv_sdl_wslg_default = gv_sdl_is_wslg();
    x->policy_wslg = gv_sdl_wslg_default;
    gv_sdl_edge = gv_sdl_wslg_default ? 12 : 6;
    return gv_sdl_wslg_default;
}

static int gv_sdl_create_window(gv_backend *b, int w, int h, const char *title) {
    if (!b || !b->impl) return -1;
    gv_sdl *x = (gv_sdl *)b->impl;
    x->width = w;
    x->height = h;
    x->window = SDL_CreateWindow(title ? title : "Glon/View",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 w, h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    if (!x->window) {
        fprintf(stderr, "gv_sdl: CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }
    x->window_id = SDL_GetWindowID(x->window);
    gv_sdl_register(x);
    /* the in-window resize band: on by default under WSLg (12 px, system
     * cursors), opt-in elsewhere (GV_EDGE_RESIZE=1).  (The pointer once
     * vanished over windows during the edge-cursor work: that was WSLg
     * session state, GUI-P2 §12.) */
    /* the edge band: its platform default, then any explicit setting */
    int want = gv_sdl_edge_policy(x);
    const char *ew = getenv("GV_EDGE_WIDTH");
    if (ew && atoi(ew) > 0) gv_sdl_edge = atoi(ew) < 4 ? 4 : atoi(ew) > 16 ? 16 : atoi(ew);
    const char *er = getenv("GV_EDGE_RESIZE");
    if (er) want = er[0] == '1';
    x->edge_requested = want;
    if (want)
        x->hit_test = SDL_SetWindowHitTest(x->window, gv_sdl_hit_test, NULL) == 0;
    /* X11: SDL 2's default cursor is None, which X turns into "inherit the
     * parent's cursor".  Our parent is the window manager's frame window:
     * after the pointer crossed the frame, its resize arrow stayed on over
     * our client area, where a press does not resize (WSLg/Weston,
     * 2026-11-25).  An explicit arrow keeps the frame's cursors on the
     * frame.  GV_DEFAULT_CURSOR=inherit restores SDL's default
     * (diagnostic). */
    const char *dc = getenv("GV_DEFAULT_CURSOR");
    if (!strcmp(SDL_GetCurrentVideoDriver(), "x11") && !(dc && !strcmp(dc, "inherit"))) {
        x->arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
        if (x->arrow) SDL_SetCursor(x->arrow);
    }
    gv_sdl_trace(b, "created");
    return gv_sdl_make_surfaces(x);
}

static void gv_sdl_destroy_window(gv_backend *b) {
    if (!b || !b->impl) return;
    gv_sdl *x = (gv_sdl *)b->impl;
    gv_sdl_free_surfaces(x);
    gv_sdl_unregister(x);
    if (x->window) {
        SDL_DestroyWindow(x->window);
        x->window = NULL;
    }
}

static int gv_sdl_resize_window(gv_backend *b, int w, int h) {
    if (!b || !b->impl) return -1;
    gv_sdl *x = (gv_sdl *)b->impl;
    if (w <= 0 || h <= 0) return -1;
    /* During SDL_WINDOWEVENT_RESIZED the window already has the new size;
     * setting it again re-enters the platform resize path and can leave the
     * window surface stale. */
    if (x->window) {
        int ww = 0, wh = 0;
        SDL_GetWindowSize(x->window, &ww, &wh);
        if (ww != w || wh != h) SDL_SetWindowSize(x->window, w, h);
    }
    x->width = w;
    x->height = h;
    return gv_sdl_make_surfaces(x);
}

static int gv_sdl_begin_frame(gv_backend *b) {
    if (!b || !b->impl) return -1;
    gv_sdl *x = (gv_sdl *)b->impl;
    if (!x->cr) return -1;
    cairo_save(x->cr);
    cairo_set_source_rgb(x->cr, 0.11, 0.12, 0.14);
    cairo_set_operator(x->cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(x->cr);
    cairo_set_operator(x->cr, CAIRO_OPERATOR_OVER);
    cairo_identity_matrix(x->cr);
    cairo_set_line_width(x->cr, 1.0);
    cairo_restore(x->cr);
    return 0;
}

/* INTENT-4B-2: an editable text (c->f6): clipped to its box (rect.w x
 * rect.h) and moved so the caret stays inside it -- a horizontal shift for
 * a single line, a vertical one for several; the selection (from the anchor
 * c->f5 to the caret c->f3, byte indexes + 1) is drawn behind the text in
 * the text colour at a quarter opacity, line by line, with Pango's own
 * measurement of the shaped text. */
static void gv_sdl_draw_edit(cairo_t *cr, const gv_cmd *c, PangoLayout *layout, double size) {
    int len = (int)c->text_len;
    int caret = c->f3 >= 1.0 ? (int)c->f3 - 1 : -1;
    int anchor = c->f5 >= 1.0 ? (int)c->f5 - 1 : -1;
    if (caret > len) caret = len;
    if (anchor > len) anchor = len;
    double w = c->rect.w > 0 ? c->rect.w : 1e9, hh = c->rect.h > 0 ? c->rect.h : 1e9;
    double ox = 0, oy = 0;
    PangoRectangle pos = { 0, 0, 0, 0 };
    if (caret >= 0) {
        pango_layout_index_to_pos(layout, caret, &pos);
        double cx = (double)pos.x / PANGO_SCALE, cy = (double)pos.y / PANGO_SCALE;
        double ch = (double)pos.height / PANGO_SCALE;
        if (ch < 1) ch = size + 2;
        if (cx + 2 > w) ox = cx + 2 - w;
        if (cy + ch > hh) oy = cy + ch - hh;
    }
    cairo_save(cr);
    cairo_rectangle(cr, c->rect.x, c->rect.y, c->rect.w > 0 ? c->rect.w : 1e6, c->rect.h > 0 ? c->rect.h : 1e6);
    cairo_clip(cr);
    cairo_translate(cr, c->rect.x - ox, c->rect.y - oy);
    if (caret >= 0 && anchor >= 0 && anchor != caret) {
        int a = anchor < caret ? anchor : caret, b = anchor < caret ? caret : anchor;
        gv_color sc = c->color;
        sc.a = (uint8_t)(sc.a / 4);
        gv_sdl_set_source(cr, sc);
        PangoLayoutIter *it = pango_layout_get_iter(layout);
        do {
            PangoLayoutLine *line = pango_layout_iter_get_line_readonly(it);
            int ls = line->start_index, le = line->start_index + line->length;
            int s0 = a > ls ? a : ls, s1 = b < le ? b : le;
            int y0 = 0, y1 = 0;
            pango_layout_iter_get_line_yrange(it, &y0, &y1);
            if (s0 < s1 || (a <= ls && b > le)) {   /* a range on this line, or a line wholly inside it */
                int *ranges = NULL, n = 0;
                pango_layout_line_get_x_ranges(line, s0, s1 > s0 ? s1 : s0, &ranges, &n);
                for (int i = 0; i < n; i++) {
                    double x0 = (double)ranges[2 * i] / PANGO_SCALE, x1 = (double)ranges[2 * i + 1] / PANGO_SCALE;
                    if (x1 - x0 < 3 && b > le) x1 = x0 + 3;        /* the line's newline is selected */
                    cairo_rectangle(cr, x0, (double)y0 / PANGO_SCALE, x1 - x0, (double)(y1 - y0) / PANGO_SCALE);
                }
                g_free(ranges);
                cairo_fill(cr);
            }
        } while (pango_layout_iter_next_line(it));
        pango_layout_iter_free(it);
    }
    cairo_move_to(cr, 0, 0);
    gv_sdl_set_source(cr, c->color);
    pango_cairo_update_layout(cr, layout);
    pango_cairo_show_layout(cr, layout);
    if (caret >= 0) {
        double ch = (double)pos.height / PANGO_SCALE;
        if (ch < 1) ch = size + 2;
        cairo_rectangle(cr, (double)pos.x / PANGO_SCALE, (double)pos.y / PANGO_SCALE, 2, ch);
        cairo_fill(cr);
    }
    cairo_restore(cr);
}

static void gv_sdl_draw_cmd(cairo_t *cr, const gv_cmd *c) {
    switch (c->op) {
    case GV_DRAW_PUSH:
        cairo_save(cr);
        break;
    case GV_DRAW_POP:
        cairo_restore(cr);
        break;
    case GV_DRAW_TRANSFORM: {
        cairo_matrix_t m;
        cairo_matrix_init(&m, c->transform.a, c->transform.b, c->transform.c,
                          c->transform.d, c->transform.e, c->transform.f);
        cairo_transform(cr, &m);
        break;
    }
    case GV_DRAW_CLIP:
        cairo_rectangle(cr, c->rect.x, c->rect.y, c->rect.w, c->rect.h);
        cairo_clip(cr);
        break;
    case GV_DRAW_RECT:
        if (c->f1 > 0 && c->rect.w > 0 && c->rect.h > 0) {
            /* INTENT-4B-5A: rounded corners (radius at most half the box) */
            double r = c->f1, x0 = c->rect.x, y0 = c->rect.y, w = c->rect.w, h = c->rect.h;
            if (r > w / 2) r = w / 2;
            if (r > h / 2) r = h / 2;
            cairo_new_sub_path(cr);
            cairo_arc(cr, x0 + w - r, y0 + r, r, -G_PI / 2, 0);
            cairo_arc(cr, x0 + w - r, y0 + h - r, r, 0, G_PI / 2);
            cairo_arc(cr, x0 + r, y0 + h - r, r, G_PI / 2, G_PI);
            cairo_arc(cr, x0 + r, y0 + r, r, G_PI, 3 * G_PI / 2);
            cairo_close_path(cr);
        } else
            cairo_rectangle(cr, c->rect.x, c->rect.y, c->rect.w, c->rect.h);
        gv_sdl_set_source(cr, c->color);
        cairo_fill(cr);
        break;
    case GV_DRAW_LINE:
        cairo_move_to(cr, c->rect.x, c->rect.y);
        cairo_line_to(cr, c->rect.x + c->rect.w, c->rect.y + c->rect.h);
        gv_sdl_set_source(cr, c->color);
        cairo_set_line_width(cr, c->f1 > 0 ? c->f1 : 1.0);
        cairo_stroke(cr);
        break;
    case GV_DRAW_TEXT: {
        double size = c->f1 > 0 ? c->f1 : 12.0;
        char *tmp = (char *)malloc((size_t)c->text_len + 1);
        if (!tmp) return;
        if (c->text_len) memcpy(tmp, c->text, c->text_len);
        tmp[c->text_len] = 0;
        PangoLayout *layout = pango_cairo_create_layout(cr);
        PangoFontDescription *desc = pango_font_description_new();
        /* INTENT-4B-5A: font style bit 0 bold, bits 1-2 family */
        int fam = (c->font_style >> 1) & 3;
        pango_font_description_set_family(desc, fam == 1 ? "serif" : fam == 2 ? "monospace" : "sans-serif");
        if (c->font_style & 1) pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);
        pango_font_description_set_size(desc, (int)(size * PANGO_SCALE));
        pango_layout_set_font_description(layout, desc);
        pango_layout_set_width(layout, -1);
        pango_layout_set_text(layout, tmp, (int)c->text_len);
        if (c->f6 >= 1.0) {
            gv_sdl_draw_edit(cr, c, layout, size);
            pango_font_description_free(desc);
            g_object_unref(layout);
            free(tmp);
            break;
        }
        if (c->f4 >= 1.0 && c->rect.w > 0) {
            /* GUI-P2: fit to rect.w, shortened with an ellipsis at a
             * character (grapheme) boundary, never inside a UTF-8 sequence */
            pango_layout_set_width(layout, c->rect.w * PANGO_SCALE);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
            pango_layout_set_single_paragraph_mode(layout, TRUE);
            /* aligned inside its own box (rect.x .. rect.x + rect.w) */
            if (c->f2 > 0)
                pango_layout_set_alignment(layout, c->f2 < 1.5 ? PANGO_ALIGN_CENTER : PANGO_ALIGN_RIGHT);
        }
        double tx = c->rect.x;
        if (c->f2 > 0 && c->rect.w > 0 && c->f4 < 1.0) {
            int pw = 0, ph = 0;
            pango_layout_get_pixel_size(layout, &pw, &ph);
            if (pw < c->rect.w)
                tx = c->f2 < 1.5 ? (c->rect.w - pw) / 2.0 : (double)(c->rect.w - pw);
            else
                tx = 0;
        }
        cairo_move_to(cr, tx, c->rect.y);
        gv_sdl_set_source(cr, c->color);
        pango_cairo_update_layout(cr, layout);
        pango_cairo_show_layout(cr, layout);
        if (c->f3 >= 1.0) {
            /* GUI-P2: text caret at a byte index, placed by Pango's own
             * measurement of the shaped text, 2 pixels wide, text colour */
            int index = (int)c->f3 - 1;
            if (index > (int)c->text_len) index = (int)c->text_len;
            PangoRectangle pos;
            pango_layout_index_to_pos(layout, index, &pos);
            double cx = tx + (double)pos.x / PANGO_SCALE;
            double cy = c->rect.y + (double)pos.y / PANGO_SCALE;
            double ch = (double)pos.height / PANGO_SCALE;
            if (ch < 1) ch = size + 2;
            cairo_rectangle(cr, cx, cy, 2, ch);
            cairo_fill(cr);
        }
        pango_font_description_free(desc);
        g_object_unref(layout);
        free(tmp);
        break;
    }
    case GV_DRAW_IMAGE: {
        if (!c->pixels || c->image_w <= 0 || c->image_h <= 0) break;
        if (c->format == 1) {
            /* INTENT-4B-5A: decoded once, premultiplied ARGB32: drawn as is,
             * scaled into rect */
            if (c->rect.w <= 0 || c->rect.h <= 0) break;
            cairo_surface_t *s = cairo_image_surface_create_for_data((unsigned char *)c->pixels, CAIRO_FORMAT_ARGB32,
                                                                     c->image_w, c->image_h, c->image_w * 4);
            if (cairo_surface_status(s) == CAIRO_STATUS_SUCCESS) {
                cairo_save(cr);
                cairo_translate(cr, c->rect.x, c->rect.y);
                cairo_scale(cr, (double)c->rect.w / c->image_w, (double)c->rect.h / c->image_h);
                cairo_set_source_surface(cr, s, 0, 0);
                cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
                cairo_rectangle(cr, 0, 0, c->image_w, c->image_h);
                cairo_fill(cr);
                cairo_restore(cr);
            }
            cairo_surface_destroy(s);
            break;
        }
        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32,
                                                   c->image_w);
        size_t bytes = (size_t)stride * (size_t)c->image_h;
        uint8_t *data = (uint8_t *)calloc(1, bytes);
        if (!data) return;
        for (int y = 0; y < c->image_h; y++) {
            for (int x = 0; x < c->image_w; x++) {
                const uint8_t *p = c->pixels + ((size_t)y * c->image_w + x) * 4;
                uint8_t a = p[3];
                uint8_t *d = data + (size_t)y * stride + (size_t)x * 4;
                d[0] = (uint8_t)((p[2] * a + 127) / 255);
                d[1] = (uint8_t)((p[1] * a + 127) / 255);
                d[2] = (uint8_t)((p[0] * a + 127) / 255);
                d[3] = a;
            }
        }
        cairo_surface_t *s = cairo_image_surface_create_for_data(
            data, CAIRO_FORMAT_ARGB32, c->image_w, c->image_h, stride);
        if (cairo_surface_status(s) == CAIRO_STATUS_SUCCESS) {
            cairo_set_source_surface(cr, s, c->rect.x, c->rect.y);
            cairo_rectangle(cr, c->rect.x, c->rect.y, c->rect.w, c->rect.h);
            cairo_fill(cr);
        }
        cairo_surface_destroy(s);
        free(data);
        break;
    }
    }
}

static int gv_sdl_draw(gv_backend *b, const gv_display_list *dl) {
    if (!b || !b->impl || !((gv_sdl *)b->impl)->cr) return -1;
    gv_sdl *x = (gv_sdl *)b->impl;
    cairo_save(x->cr);
    for (int i = 0; dl && i < dl->count; i++) {
        gv_sdl_draw_cmd(x->cr, &dl->cmds[i]);
    }
    cairo_restore(x->cr);
    return 0;
}

static int gv_sdl_end_frame(gv_backend *b) {
    if (!b || !b->impl) return -1;
    gv_sdl *x = (gv_sdl *)b->impl;
    if (!x->cr) return -1;
    /* A live X11 resize can replace the window surface asynchronously.  Never
     * present through a surface whose dimensions do not match the target. */
    if (!x->window_surface || x->window_surface->w != x->width ||
        x->window_surface->h != x->height) {
        x->window_surface = SDL_GetWindowSurface(x->window);
        if (!x->window_surface) return -1;
        if (x->window_surface->w != x->width || x->window_surface->h != x->height)
            return -1;
    }
    cairo_surface_flush(x->cairo_surface);
    cairo_surface_mark_dirty(x->cairo_surface);
    if (x->screen->w == x->window_surface->w &&
        x->screen->h == x->window_surface->h)
        SDL_BlitSurface(x->screen, NULL, x->window_surface, NULL);
    else
        SDL_BlitScaled(x->screen, NULL, x->window_surface, NULL);
    SDL_UpdateWindowSurface(x->window);
    /* GV_RECORD_DIR=dir: every presented frame is also written there as a
     * PNG named by wall-clock microseconds (frames from successive processes
     * sort in order), for making a video of a session or a test run */
    {
        const char *rec = getenv("GV_RECORD_DIR");
        if (rec && rec[0]) {
            static int seq;
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            char path[1024];
            snprintf(path, sizeof path, "%s/%010lld%06ld-%06d.png", rec, (long long)ts.tv_sec,
                     ts.tv_nsec / 1000, seq++);
            cairo_surface_write_to_png(x->cairo_surface, path);
        }
    }
    if (!x->shown) {
        SDL_ShowWindow(x->window);
        x->shown = 1;
    }
    return 0;
}

static int gv_sdl_poll_event(gv_backend *b, gv_event *ev) {
    if (!b || !b->impl || !ev) return -1;
    gv_sdl *x = (gv_sdl *)b->impl;
    if (x->qhead != x->qtail) {
        *ev = x->queue[x->qhead];
        x->qhead = (x->qhead + 1) % (int)(sizeof x->queue / sizeof x->queue[0]);
        return 1;
    }
    /* SDL events that do not map to a View event (window expose/shown,
     * text editing, ...) are skipped here: answering 0 for them made a
     * runner stop draining the queue at the first one, so the input queued
     * behind it waited for a later frame. */
    SDL_Event e;
  next_sdl_event:
    if (x->phead != x->ptail) {
        e = x->pend[x->phead];
        x->phead = (x->phead + 1) % (int)(sizeof x->pend / sizeof x->pend[0]);
    } else {
        if (!SDL_PollEvent(&e)) {
            memset(ev, 0, sizeof *ev);
            ev->type = GV_EVENT_NONE;
            return 0;
        }
        /* GLON-INST-3B: another window's event waits in its own queue; one
         * for a window already closed is dropped */
        gv_sdl *dest = gv_sdl_route(&e);
        if (dest != x) {
            if (dest) gv_sdl_pend(dest, &e); else gv_sdl_dropped++;
            goto next_sdl_event;
        }
    }
    memset(ev, 0, sizeof *ev);
    switch (e.type) {
    case SDL_QUIT:
        ev->type = GV_EVENT_QUIT;
        break;
    case SDL_MOUSEMOTION:
        if (getenv("GV_TRACE_CURSOR") && (e.motion.x < 12 || e.motion.y < 12 ||
                                          e.motion.x >= x->width - 12 || e.motion.y >= x->height - 12))
            fprintf(stderr, "[cursor] motion at %d,%d of %dx%d\n", e.motion.x, e.motion.y, x->width, x->height);
        {
            /* the zone from the window's current size, as the hit test
             * computes it (the stored size lags a resize until its event) */
            int cw = x->width, chh = x->height;
            SDL_GetWindowSize(x->window, &cw, &chh);
            gv_sdl_set_cursor_zone(x, gv_backend_sdl_edge_zone(cw, chh, e.motion.x, e.motion.y));
            x->last_mx = e.motion.x;
            x->last_my = e.motion.y;
        }
        ev->type = GV_EVENT_POINTER_MOVE;
        ev->x = e.motion.x;
        ev->y = e.motion.y;
        break;
    case SDL_MOUSEBUTTONDOWN:
        /* GUI-P2: while a button is held the window keeps getting the
         * pointer's motion outside it (a scrollbar drag that leaves the
         * window continues until the release) */
        /* no capture here: the host asks for one (set_capture) only when
         * a drag starts that must follow the pointer out of the window */
        if (gv_sdl_trace_edge()) {
            int cw = 0, chh = 0;
            SDL_GetWindowSize(x->window, &cw, &chh);
            int z = gv_backend_sdl_edge_zone(cw, chh, e.button.x, e.button.y);
            fprintf(stderr, "[edge] %u press reached the application at %d,%d of %dx%d: zone %d, resize cursor %s%s\n",
                    (unsigned)SDL_GetTicks(), e.button.x, e.button.y, cw, chh, z, x->edge_cursor_shown ? "SHOWN" : "not shown",
                    (z && x->hit_test) ? "  ** MISMATCH: a zone press should have gone to the window manager **" : "");
        }
        if (gv_sdl_trace_window()) fprintf(stderr, "[window] button %d down at %d,%d\n", e.button.button, e.button.x, e.button.y);
        ev->type = GV_EVENT_POINTER_DOWN;
        ev->x = e.button.x;
        ev->y = e.button.y;
        ev->key = e.button.button;
        ev->mods = e.button.clicks;      /* GUI-P2: 2 for a double click (platform timing) */
        break;
    case SDL_MOUSEBUTTONUP:
        /* the last button up: no drag can still need the capture */
        if (x->captured && !(SDL_GetMouseState(NULL, NULL) & ~SDL_BUTTON(e.button.button)))
            gv_sdl_set_capture(b, 0);
        if (gv_sdl_trace_window()) {
            char r[1024];
            gv_backend_sdl_window_report(b, r, sizeof r);
            fprintf(stderr, "[window] button %d up at %d,%d; after release: %s\n", e.button.button, e.button.x, e.button.y,
                    strstr(r, "MOUSE CAPTURED") ? "MOUSE STILL CAPTURED" : "no capture");
        }
        ev->type = GV_EVENT_POINTER_UP;
        ev->x = e.button.x;
        ev->y = e.button.y;
        ev->key = e.button.button;
        break;
    case SDL_KEYDOWN:
        ev->type = GV_EVENT_KEY_DOWN;
        ev->key = e.key.keysym.sym;
        ev->mods = e.key.keysym.mod;
        break;
    case SDL_KEYUP:
        ev->type = GV_EVENT_KEY_UP;
        ev->key = e.key.keysym.sym;
        ev->mods = e.key.keysym.mod;
        break;
    case SDL_MOUSEWHEEL:
        ev->type = GV_EVENT_WHEEL;
        {
            int mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            ev->x = mx;
            ev->y = my;
        }
        /* GV_EVENT_WHEEL carries scroll-offset steps (key = dx, mods = dy as
         * a signed int): positive moves toward the end of the content.  SDL
         * reports wheel-up / away from the user as y > 0, so y is negated;
         * a "flipped" (natural-scrolling) device reports the opposite sign. */
        {
            int wx = e.wheel.x, wy = e.wheel.y;
            if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) { wx = -wx; wy = -wy; }
            /* GUI-P2: Shift turns a vertical wheel into a horizontal one, as
             * desktop applications do (wheel down = toward the right) */
            if (wx == 0 && (SDL_GetModState() & KMOD_SHIFT)) { wx = -wy; wy = 0; }
            ev->key = wx;
            ev->mods = (unsigned int)(-wy);
        }
        break;
    case SDL_TEXTINPUT: {
        /* INTENT-4B-2: every character of the text, UTF-8 decoded (before,
         * only an ASCII byte was kept: "é" or an IME's "漢字" was lost); the
         * first is this event, the others wait in the queue, in order */
        ev->type = GV_EVENT_TEXT;
        ev->codepoint = 0;
        int first = 1;
        const unsigned char *p = (const unsigned char *)e.text.text;
        while (*p) {
            unsigned int cp = *p, n = 0;
            if (cp < 0x80) n = 0;
            else if ((cp & 0xE0) == 0xC0) { cp &= 0x1F; n = 1; }
            else if ((cp & 0xF0) == 0xE0) { cp &= 0x0F; n = 2; }
            else if ((cp & 0xF8) == 0xF0) { cp &= 0x07; n = 3; }
            else { p++; continue; }                 /* a stray continuation byte */
            p++;
            int ok = 1;
            for (unsigned int k = 0; k < n; k++, p++) {
                if ((*p & 0xC0) != 0x80) { ok = 0; break; }
                cp = (cp << 6) | (*p & 0x3F);
            }
            if (!ok || cp == 0) continue;
            if (first) { ev->codepoint = cp; first = 0; }
            else {
                int next = (x->qtail + 1) % (int)(sizeof x->queue / sizeof x->queue[0]);
                if (next != x->qhead) {
                    gv_event q; memset(&q, 0, sizeof q);
                    q.type = GV_EVENT_TEXT;
                    q.codepoint = cp;
                    x->queue[x->qtail] = q;
                    x->qtail = next;
                }
            }
        }
        if (first) goto next_sdl_event;             /* nothing decodable */
        break;
    }
    case SDL_WINDOWEVENT:
        if (gv_sdl_trace_window()) {
            char when[64];
            snprintf(when, sizeof when, "event %s %d %d", gv_sdl_window_event_name(e.window.event),
                     (int)e.window.data1, (int)e.window.data2);
            if (e.window.event == SDL_WINDOWEVENT_SHOWN || e.window.event == SDL_WINDOWEVENT_MAXIMIZED ||
                e.window.event == SDL_WINDOWEVENT_RESIZED || e.window.event == SDL_WINDOWEVENT_HIT_TEST)
                gv_sdl_trace(b, when);
            else
                fprintf(stderr, "[window] %s\n", when);
        }
        if (gv_sdl_trace_edge()) {
            if (e.window.event == SDL_WINDOWEVENT_LEAVE || e.window.event == SDL_WINDOWEVENT_ENTER)
                fprintf(stderr, "[edge] %u pointer %s the window (last motion inside at %d,%d)\n", (unsigned)SDL_GetTicks(),
                        e.window.event == SDL_WINDOWEVENT_LEAVE ? "left" : "entered", x->last_mx, x->last_my);
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                fprintf(stderr, "[edge] %u window size now %dx%d\n", (unsigned)SDL_GetTicks(), (int)e.window.data1, (int)e.window.data2);
        }
        if (e.window.event == SDL_WINDOWEVENT_LEAVE)
            gv_sdl_set_cursor_zone(x, SDL_HITTEST_NORMAL);
        /* GLON-INST-3B: a window's close button: the first window ends the
         * application (as SDL_QUIT does when it is the only one); another
         * window asks for itself to be closed */
        if (e.window.event == SDL_WINDOWEVENT_CLOSE) {
            ev->type = x->secondary ? GV_EVENT_CLOSE : GV_EVENT_QUIT;
            break;
        }
        /* focus lost (another window, a window-manager action): no capture
         * may outlive it, and the host ends any drag (GV_EVENT_FOCUS 0) */
        if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST || e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) gv_sdl_set_capture(b, 0);
            ev->type = GV_EVENT_FOCUS;
            ev->key = e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED;
            break;
        }
        if (e.window.event == SDL_WINDOWEVENT_RESIZED ||
            e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            int w, h;
            SDL_GetWindowSize(x->window, &w, &h);
            if (w != x->width || h != x->height) {
                gv_sdl_resize_window(b, w, h);
            }
            ev->type = GV_EVENT_RESIZE;
            ev->width = w;
            ev->height = h;
        }
        break;
    default:
        memset(ev, 0, sizeof *ev);
        ev->type = GV_EVENT_NONE;
        break;
    }
    if (ev->type == GV_EVENT_NONE) goto next_sdl_event;
    return 1;
}

/* GUI-P2: Pango's own line breaking (word, else character) at width px,
 * with the font the frames are drawn with, so rows wrap exactly where the
 * drawn text would overflow and never inside a UTF-8 sequence */
static PangoContext *gv_sdl_measure_ctx(void) {
    static PangoContext *ctx;
    if (!ctx) ctx = pango_font_map_create_context(pango_cairo_font_map_get_default());
    return ctx;
}
/* GUI-P2: the logical width Pango gives the text with the drawing font */
static int gv_sdl_measure_text(gv_backend *b, const char *s, int len, double size) {
    (void)b;
    PangoContext *ctx = gv_sdl_measure_ctx();
    if (!ctx || !s || len <= 0) return 0;
    PangoLayout *layout = pango_layout_new(ctx);
    PangoFontDescription *desc = pango_font_description_new();
    pango_font_description_set_family(desc, "sans-serif");
    pango_font_description_set_size(desc, (int)(size * PANGO_SCALE));
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, s, len);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(layout, &w, &h);
    pango_font_description_free(desc);
    g_object_unref(layout);
    return w;
}
static int gv_sdl_wrap_text(gv_backend *b, const char *s, int len, double size, int width,
                            int *starts, int *lens, int max) {
    (void)b;
    PangoContext *ctx = gv_sdl_measure_ctx();
    if (!ctx || !s || len <= 0 || max <= 0) return 0;
    PangoLayout *layout = pango_layout_new(ctx);
    PangoFontDescription *desc = pango_font_description_new();
    pango_font_description_set_family(desc, "sans-serif");
    pango_font_description_set_size(desc, (int)(size * PANGO_SCALE));
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_width(layout, width * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(layout, s, len);
    int n = pango_layout_get_line_count(layout);
    if (n > max) n = max;
    for (int i = 0; i < n; i++) {
        PangoLayoutLine *line = pango_layout_get_line_readonly(layout, i);
        starts[i] = line->start_index;
        lens[i] = line->length;
    }
    pango_font_description_free(desc);
    g_object_unref(layout);
    return n;
}

static int gv_sdl_has_cap(gv_backend *b, gv_cap cap) {
    (void)b;
    switch (cap) {
    case GV_CAP_WINDOW:
    case GV_CAP_CLIPBOARD:
    case GV_CAP_TEXT_SHAPE:
        return 1;
    default:
        return 0;
    }
}

static void *gv_sdl_native_handle(gv_backend *b) {
    if (!b || !b->impl) return NULL;
    gv_sdl *x = (gv_sdl *)b->impl;
#ifdef _WIN32
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(x->window, &info)) {
        return (void *)info.info.win.window;
    }
#else
    /* FOREIGN-1: on X11 the window's id, as the parent of hosted native
     * surfaces (gv_host.h); NULL under other drivers (dummy, Wayland) */
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (x->window && SDL_GetWindowWMInfo(x->window, &info) && info.subsystem == SDL_SYSWM_X11)
        return (void *)(uintptr_t)info.info.x11.window;
#endif
    return NULL;
}

static int gv_sdl_native_control_create(gv_backend *b, int kind, gv_rect rect,
                                        const char *text, void **out) {
    if (!b || !b->impl || !out) return -1;
    void *h = gv_sdl_native_handle(b);
    if (!h) return -1;
    *out = gv_native_control_create(h, kind, rect, text);
    return *out ? 0 : -1;
}

static void gv_sdl_native_control_destroy(gv_backend *b, void *ctrl) {
    (void)b;
    gv_native_control_destroy(ctrl);
}

static int gv_sdl_native_control_set_text(gv_backend *b, void *ctrl,
                                          const char *text) {
    (void)b;
    return gv_native_control_set_text(ctrl, text);
}

static int gv_sdl_native_control_get_text(gv_backend *b, void *ctrl,
                                          char *buf, int cap) {
    (void)b;
    return gv_native_control_get_text(ctrl, buf, cap);
}

static int gv_sdl_native_control_set_focus(gv_backend *b, void *ctrl, int focus) {
    (void)b;
    return gv_native_control_set_focus(ctrl, focus);
}

static int gv_sdl_set_clipboard(gv_backend *b, const char *text) {
    if (!b || !b->impl || !text) return -1;
    gv_sdl *x = (gv_sdl *)b->impl;
    if (SDL_SetClipboardText(text) != 0) return -1;
    snprintf(x->clipboard, sizeof x->clipboard, "%s", text);
    x->clipboard_set = 1;
    return 0;
}

static int gv_sdl_get_clipboard(gv_backend *b, char *buf, int cap) {
    if (!b || !b->impl || !buf || cap <= 0) return -1;
    gv_sdl *x = (gv_sdl *)b->impl;
    if (x->clipboard_set) {
        snprintf(buf, (size_t)cap, "%s", x->clipboard);
        return 0;
    }
    char *s = SDL_GetClipboardText();
    if (!s) return -1;
    snprintf(buf, (size_t)cap, "%s", s);
    SDL_free(s);
    return 0;
}

/* ---- INTENT-4B-5A: PNG (cairo) and JPEG (libjpeg) decoding -------------- */
#define GV_IMAGE_MAX_SIDE 8192
#define GV_IMAGE_MAX_PIXELS (16 * 1024 * 1024)
static int gv_image_too_large(long w, long h, char *err, int cap) {
    if (w <= 0 || h <= 0 || w > GV_IMAGE_MAX_SIDE || h > GV_IMAGE_MAX_SIDE || w * h > GV_IMAGE_MAX_PIXELS) {
        snprintf(err, (size_t)cap, "image too large (%ldx%ld; at most %d per side, %d pixels)", w, h, GV_IMAGE_MAX_SIDE,
                 GV_IMAGE_MAX_PIXELS);
        return 1;
    }
    return 0;
}
static int gv_decode_png(const char *path, const unsigned char *head, uint8_t **out, int *ow, int *oh, char *err, int cap) {
    long w = ((long)head[16] << 24) | ((long)head[17] << 16) | ((long)head[18] << 8) | head[19];
    long h = ((long)head[20] << 24) | ((long)head[21] << 16) | ((long)head[22] << 8) | head[23];
    if (gv_image_too_large(w, h, err, cap)) return -1;        /* checked before any pixel is decoded */
    cairo_surface_t *s = cairo_image_surface_create_from_png(path);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
        snprintf(err, (size_t)cap, "invalid PNG image (%s)", cairo_status_to_string(cairo_surface_status(s)));
        cairo_surface_destroy(s);
        return -1;
    }
    cairo_surface_flush(s);
    int iw = cairo_image_surface_get_width(s), ih = cairo_image_surface_get_height(s);
    int stride = cairo_image_surface_get_stride(s);
    cairo_format_t f = cairo_image_surface_get_format(s);
    const unsigned char *src = cairo_image_surface_get_data(s);
    uint8_t *px = (f == CAIRO_FORMAT_ARGB32 || f == CAIRO_FORMAT_RGB24) && src ? malloc((size_t)iw * ih * 4) : NULL;
    if (!px) { snprintf(err, (size_t)cap, "invalid PNG image (format)"); cairo_surface_destroy(s); return -1; }
    for (int y = 0; y < ih; y++) {
        memcpy(px + (size_t)y * iw * 4, src + (size_t)y * stride, (size_t)iw * 4);
        if (f == CAIRO_FORMAT_RGB24)
            for (int x = 0; x < iw; x++) ((uint32_t *)(px + (size_t)y * iw * 4))[x] |= 0xff000000u;
    }
    cairo_surface_destroy(s);
    *out = px; *ow = iw; *oh = ih;
    return 0;
}
struct gv_jpeg_err { struct jpeg_error_mgr mgr; jmp_buf jump; char msg[JMSG_LENGTH_MAX]; };
static void gv_jpeg_fail(j_common_ptr j) {
    struct gv_jpeg_err *e = (struct gv_jpeg_err *)j->err;
    (*j->err->format_message)(j, e->msg);
    longjmp(e->jump, 1);
}
static int gv_decode_jpeg(const char *path, uint8_t **out, int *ow, int *oh, char *err, int cap) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, (size_t)cap, "cannot open the image"); return -1; }
    struct jpeg_decompress_struct j;
    struct gv_jpeg_err e;
    uint8_t *volatile px = NULL;
    memset(&j, 0, sizeof j);
    j.err = jpeg_std_error(&e.mgr);
    e.mgr.error_exit = gv_jpeg_fail;
    e.msg[0] = 0;
    if (setjmp(e.jump)) {
        snprintf(err, (size_t)cap, "invalid JPEG image (%s)", e.msg);
        jpeg_destroy_decompress(&j);
        fclose(f);
        free(px);
        return -1;
    }
    jpeg_create_decompress(&j);
    jpeg_stdio_src(&j, f);
    jpeg_read_header(&j, TRUE);
    if (gv_image_too_large((long)j.image_width, (long)j.image_height, err, cap)) {
        jpeg_destroy_decompress(&j); fclose(f); return -1;
    }
    j.out_color_space = JCS_EXT_BGRA;                /* little-endian ARGB32, opaque */
    jpeg_start_decompress(&j);
    int w = (int)j.output_width, h = (int)j.output_height;
    px = malloc((size_t)w * h * 4);
    if (!px) { snprintf(err, (size_t)cap, "out of memory"); jpeg_destroy_decompress(&j); fclose(f); return -1; }
    while (j.output_scanline < j.output_height) {
        JSAMPROW row = px + (size_t)j.output_scanline * w * 4;
        jpeg_read_scanlines(&j, &row, 1);
    }
    jpeg_finish_decompress(&j);
    jpeg_destroy_decompress(&j);
    fclose(f);
    *out = px; *ow = w; *oh = h;
    return 0;
}
static int gv_sdl_decode_image(gv_backend *b, const char *path, uint8_t **pixels, int *w, int *h, char *err, int cap) {
    (void)b;
    unsigned char head[24] = { 0 };
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, (size_t)cap, errno == ENOENT ? "image not found" : "cannot open the image"); return -1; }
    size_t n = fread(head, 1, sizeof head, f);
    fclose(f);
    if (n >= 24 && !memcmp(head, "\x89PNG\r\n\x1a\n", 8)) return gv_decode_png(path, head, pixels, w, h, err, cap);
    if (n >= 3 && head[0] == 0xff && head[1] == 0xd8 && head[2] == 0xff) return gv_decode_jpeg(path, pixels, w, h, err, cap);
    snprintf(err, (size_t)cap, "not a PNG or JPEG image");
    return -1;
}

gv_backend *gv_backend_sdl(void) {
    gv_backend *b = (gv_backend *)calloc(1, sizeof(*b));
    if (!b) return NULL;
    gv_sdl *x = (gv_sdl *)calloc(1, sizeof(*x));
    if (!x) {
        free(b);
        return NULL;
    }
    snprintf(b->name, sizeof b->name, "sdl-cairo");
    b->impl = x;
    b->init = gv_sdl_init;
    b->shutdown = gv_sdl_shutdown;
    b->destroy = gv_sdl_destroy;
    b->create_window = gv_sdl_create_window;
    b->destroy_window = gv_sdl_destroy_window;
    b->resize_window = gv_sdl_resize_window;
    b->begin_frame = gv_sdl_begin_frame;
    b->draw = gv_sdl_draw;
    b->end_frame = gv_sdl_end_frame;
    b->poll_event = gv_sdl_poll_event;
    b->has_cap = gv_sdl_has_cap;
    b->native_handle = gv_sdl_native_handle;
    b->native_control_create = gv_sdl_native_control_create;
    b->native_control_destroy = gv_sdl_native_control_destroy;
    b->decode_image = gv_sdl_decode_image;
    b->native_control_set_text = gv_sdl_native_control_set_text;
    b->native_control_get_text = gv_sdl_native_control_get_text;
    b->native_control_set_focus = gv_sdl_native_control_set_focus;
    b->set_clipboard = gv_sdl_set_clipboard;
    b->get_clipboard = gv_sdl_get_clipboard;
    b->wrap_text = gv_sdl_wrap_text;
    b->measure_text = gv_sdl_measure_text;
    b->set_cursor = gv_sdl_set_cursor;
    b->set_capture = gv_sdl_set_capture;
    return b;
}

int gv_backend_sdl_save_png(gv_backend *b, const char *path) {
    if (!b || !b->impl || !path) return -1;
    gv_sdl *x = (gv_sdl *)b->impl;
    if (!x->cairo_surface) return -1;
    cairo_surface_flush(x->cairo_surface);
    cairo_surface_mark_dirty(x->cairo_surface);
    return cairo_surface_write_to_png(x->cairo_surface, path);
}

/* ---- GLON-INST-3B: secondary native windows -------------------------------
 * A secondary window is a backend of its own (its window, surfaces and
 * pending events) in the same SDL session: initialised with SDL_InitSubSystem
 * and closed with SDL_QuitSubSystem, so closing it never ends SDL for the
 * others (the first window's shutdown still calls SDL_Quit). */
gv_backend *gv_backend_sdl_secondary(void) {
    gv_backend *b = gv_backend_sdl();
    if (b) ((gv_sdl *)b->impl)->secondary = 1;
    return b;
}
void gv_backend_sdl_close_secondary(gv_backend *b) {
    if (!b || !b->impl) return;
    gv_sdl *x = (gv_sdl *)b->impl;
    gv_sdl_set_capture(b, 0);
    gv_sdl_free_surfaces(x);
    for (int i = 0; i < 10; i++)
        if (x->cursors[i]) { SDL_FreeCursor(x->cursors[i]); x->cursors[i] = NULL; }
    if (x->col_cursor) { SDL_FreeCursor(x->col_cursor); x->col_cursor = NULL; }
    if (x->arrow) { SDL_FreeCursor(x->arrow); x->arrow = NULL; }
    gv_sdl_unregister(x);
    if (x->window) { SDL_DestroyWindow(x->window); x->window = NULL; }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    free(x);
    free(b);
}
unsigned gv_backend_sdl_window_id(gv_backend *b) {
    return b && b->impl ? (unsigned)((gv_sdl *)b->impl)->window_id : 0;
}
long gv_backend_sdl_dropped_events(void) { return gv_sdl_dropped; }
/* a window's title (a secondary window's caption) */
void gv_backend_sdl_set_title(gv_backend *b, const char *title) {
    if (b && b->impl && ((gv_sdl *)b->impl)->window) SDL_SetWindowTitle(((gv_sdl *)b->impl)->window, title ? title : "");
}
void gv_backend_sdl_raise(gv_backend *b) {
    if (b && b->impl && ((gv_sdl *)b->impl)->window) SDL_RaiseWindow(((gv_sdl *)b->impl)->window);
}
static const gv_window_ops gv_sdl_window_ops = {
    gv_backend_sdl_secondary, gv_backend_sdl_close_secondary, gv_backend_sdl_set_title,
    gv_backend_sdl_raise, gv_backend_sdl_window_id, gv_backend_sdl_dropped_events
};
const gv_window_ops *gv_backend_sdl_window_ops(void) { return &gv_sdl_window_ops; }
