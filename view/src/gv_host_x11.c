/* gv_host_x11.c -- "x11-edit": an independently rendered foreign component
 * (FOREIGN-1.md).
 *
 * A one-line text editor living in its own X11 child window, on its OWN X
 * connection, drawn with core Xlib (not Cairo, not View), with its own event
 * loop (pump) and its own keyboard focus.  It stands for any component that
 * brings a native surface and a loop of its own (a web view, a PDF viewer,
 * a native editor).
 *
 *   SDL's X11 window (the application's)
 *     └─ outer: the visible part of the node (clip) -- X clips to it
 *          └─ inner: the whole component, offset inside outer
 *
 * Mouse and keys go to it directly from the X server while it has the
 * focus; it tells the host what matters (focus-request, tab, change, enter,
 * click).  Xlib is opened at run time (dlopen): no link dependency, and a
 * build without X11 simply cannot create one (create answers why). */

#include "gv_host.h"

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

void gv_host_count_create(void);
void gv_host_count_destroy(void);

/* ---- Xlib, opened at run time --------------------------------------------- */
static struct {
    int loaded;
    Display *(*open)(const char *);
    int (*close)(Display *);
    Window (*create)(Display *, Window, int, int, unsigned, unsigned, unsigned, unsigned long, unsigned long);
    int (*destroy)(Display *, Window);
    int (*map)(Display *, Window);
    int (*unmap)(Display *, Window);
    int (*move_resize)(Display *, Window, int, int, unsigned, unsigned);
    int (*select)(Display *, Window, long);
    int (*pending)(Display *);
    int (*next)(Display *, XEvent *);
    int (*flush)(Display *);
    int (*focus)(Display *, Window, int, Time);
    GC (*gc)(Display *, Drawable, unsigned long, XGCValues *);
    int (*free_gc)(Display *, GC);
    int (*fg)(Display *, GC, unsigned long);
    int (*fill)(Display *, Drawable, GC, int, int, unsigned, unsigned);
    int (*rect)(Display *, Drawable, GC, int, int, unsigned, unsigned);
    int (*text)(Display *, Drawable, GC, int, int, const char *, int);
    int (*lookup)(XKeyEvent *, char *, int, KeySym *, XComposeStatus *);
    Status (*attrs)(Display *, Window, XWindowAttributes *);
    int (*sync)(Display *, Bool);
    int (*get_focus)(Display *, Window *, int *);
} X;

static int gv_x11_load(char *err, int errcap) {
    if (X.loaded) return X.loaded > 0 ? 0 : -1;
    void *l = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    X.loaded = -1;
    if (!l) { snprintf(err, (size_t)errcap, "libX11 not loadable"); return -1; }
#define GV_SYM(f, n) if (!(*(void **)&X.f = dlsym(l, n))) { snprintf(err, (size_t)errcap, "libX11 lacks %s", n); return -1; }
    GV_SYM(open, "XOpenDisplay") GV_SYM(close, "XCloseDisplay") GV_SYM(create, "XCreateSimpleWindow")
    GV_SYM(destroy, "XDestroyWindow") GV_SYM(map, "XMapWindow") GV_SYM(unmap, "XUnmapWindow")
    GV_SYM(move_resize, "XMoveResizeWindow") GV_SYM(select, "XSelectInput") GV_SYM(pending, "XPending")
    GV_SYM(next, "XNextEvent") GV_SYM(flush, "XFlush") GV_SYM(focus, "XSetInputFocus")
    GV_SYM(gc, "XCreateGC") GV_SYM(free_gc, "XFreeGC") GV_SYM(fg, "XSetForeground")
    GV_SYM(fill, "XFillRectangle") GV_SYM(rect, "XDrawRectangle") GV_SYM(text, "XDrawString")
    GV_SYM(lookup, "XLookupString") GV_SYM(attrs, "XGetWindowAttributes") GV_SYM(sync, "XSync")
    GV_SYM(get_focus, "XGetInputFocus")
#undef GV_SYM
    X.loaded = 1;
    return 0;
}

/* ---- the component -------------------------------------------------------------- */
typedef struct gv_x11edit {
    unsigned magic;
    gv_host_env env;
    Display *d;
    Window parent, outer, inner;
    GC gc;
    int w, h, mapped, focused, clicks;
    int reclaims;                        /* focus taken back from our own window (see pump) */
    char text[256];
} gv_x11edit;
#define GV_X11EDIT_MAGIC 0x78656469u

/* 24-bit TrueColor pixels (the X server of WSLg and of ordinary desktops) */
static unsigned long gv_rgb(int r, int g, int b) { return ((unsigned long)r << 16) | ((unsigned long)g << 8) | (unsigned long)b; }

static void gv_x11edit_draw(gv_x11edit *e) {
    if (!e->mapped || e->w <= 0 || e->h <= 0) return;
    X.fg(e->d, e->gc, e->focused ? gv_rgb(20, 40, 70) : gv_rgb(38, 40, 46));
    X.fill(e->d, e->inner, e->gc, 0, 0, (unsigned)e->w, (unsigned)e->h);
    X.fg(e->d, e->gc, e->focused ? gv_rgb(90, 160, 255) : gv_rgb(110, 116, 128));
    X.rect(e->d, e->inner, e->gc, 0, 0, (unsigned)(e->w - 1), (unsigned)(e->h - 1));
    X.rect(e->d, e->inner, e->gc, 1, 1, (unsigned)(e->w - 3), (unsigned)(e->h - 3));
    char line[320];
    snprintf(line, sizeof line, "%s%s", e->text, e->focused ? "_" : "");
    X.fg(e->d, e->gc, gv_rgb(240, 240, 240));
    X.text(e->d, e->inner, e->gc, 10, 20, line, (int)strlen(line));
    if (e->h > 46) {
        char info[160];
        snprintf(info, sizeof info, "x11-edit: own X connection, Xlib drawing; %d click(s); %dx%d", e->clicks, e->w, e->h);
        X.fg(e->d, e->gc, gv_rgb(150, 156, 168));
        X.text(e->d, e->inner, e->gc, 10, 40, info, (int)strlen(info));
    }
    X.flush(e->d);
}

static void *gv_x11edit_create(const gv_host_env *env, const char *props, char *err, int errcap) {
    if (!env || !env->native_parent || !env->platform || strcmp(env->platform, "x11")) {
        snprintf(err, (size_t)errcap, "x11-edit needs an X11 window (platform %s)", env && env->platform ? env->platform : "?");
        return NULL;
    }
    if (gv_x11_load(err, errcap) != 0) return NULL;
    gv_x11edit *e = (gv_x11edit *)calloc(1, sizeof *e);
    if (!e) { snprintf(err, (size_t)errcap, "out of memory"); return NULL; }
    e->d = X.open(NULL);                 /* its own connection to the same server */
    if (!e->d) { free(e); snprintf(err, (size_t)errcap, "cannot open the X display"); return NULL; }
    e->magic = GV_X11EDIT_MAGIC;
    e->env = *env;
    e->parent = (Window)(uintptr_t)env->native_parent;
    e->outer = X.create(e->d, e->parent, 0, 0, 1, 1, 0, 0, gv_rgb(38, 40, 46));
    e->inner = X.create(e->d, e->outer, 0, 0, 1, 1, 0, 0, gv_rgb(38, 40, 46));
    X.select(e->d, e->inner, ExposureMask | ButtonPressMask | KeyPressMask | FocusChangeMask);
    X.map(e->d, e->inner);               /* shown when outer is mapped (place) */
    e->gc = X.gc(e->d, e->inner, 0, NULL);
    snprintf(e->text, sizeof e->text, "%s", props ? props : "");
    X.flush(e->d);
    gv_host_count_create();
    return e;
}

static void gv_x11edit_place(void *inst, gv_rect b, gv_rect c, int visible) {
    gv_x11edit *e = (gv_x11edit *)inst;
    if (!visible || c.w <= 0 || c.h <= 0) {
        if (e->mapped) { X.unmap(e->d, e->outer); e->mapped = 0; }
        X.flush(e->d);
        return;
    }
    X.move_resize(e->d, e->outer, c.x, c.y, (unsigned)c.w, (unsigned)c.h);
    X.move_resize(e->d, e->inner, b.x - c.x, b.y - c.y, (unsigned)b.w, (unsigned)b.h);
    e->w = b.w;
    e->h = b.h;
    if (!e->mapped) { X.map(e->d, e->outer); e->mapped = 1; }
    gv_x11edit_draw(e);
}

static int gv_x11edit_trace(void) { return getenv("GV_TRACE_HOST") != NULL; }
/* who has the X keyboard focus now (after the server processed our requests) */
static Window gv_x11edit_focus_now(gv_x11edit *e) {
    Window w = 0;
    int revert = 0;
    X.sync(e->d, False);
    X.get_focus(e->d, &w, &revert);
    return w;
}
static int gv_x11edit_focus(void *inst, int on) {
    gv_x11edit *e = (gv_x11edit *)inst;
    if (on && e->mapped) X.focus(e->d, e->inner, RevertToParent, CurrentTime);
    else if (!on && e->focused) X.focus(e->d, e->parent, RevertToParent, CurrentTime);
    e->focused = on && e->mapped;
    gv_x11edit_draw(e);
    if (gv_x11edit_trace()) {
        Window f = gv_x11edit_focus_now(e);
        fprintf(stderr, "[x11-edit] focus(%d): XSetInputFocus(%s); X focus now 0x%lx (%s)\n", on,
                on ? "inner" : "application window", (unsigned long)f,
                f == e->inner ? "the component" : f == e->parent ? "the application window" : "elsewhere");
    }
    X.flush(e->d);
    return e->focused;
}

static int gv_x11edit_command(void *inst, const char *cmd, const char *arg) {
    gv_x11edit *e = (gv_x11edit *)inst;
    if (!strcmp(cmd, "set-text")) {
        snprintf(e->text, sizeof e->text, "%s", arg ? arg : "");
        gv_x11edit_draw(e);
        return 0;
    }
    return -1;
}

static int gv_x11edit_pump(void *inst) {
    gv_x11edit *e = (gv_x11edit *)inst;
    int n = 0;
    /* the host gave us the focus, but the X focus went back to the
     * application's own window (a keyboard-initiated change may be undone by
     * the toolkit or the window manager): take it again.  Only from our own
     * application window -- never from another application. */
    if (e->focused && e->mapped && gv_x11edit_focus_now(e) == e->parent) {
        X.focus(e->d, e->inner, RevertToParent, CurrentTime);
        e->reclaims++;
        if (gv_x11edit_trace())
            fprintf(stderr, "[x11-edit] X focus was on the application window while the host gives it to the component: taken again (%d)\n", e->reclaims);
        X.flush(e->d);
    }
    while (X.pending(e->d) > 0) {
        XEvent ev;
        X.next(e->d, &ev);
        n++;
        if (ev.type == Expose && ev.xexpose.count == 0) gv_x11edit_draw(e);
        else if (ev.type == ButtonPress) {
            e->clicks++;
            char xy[32];
            snprintf(xy, sizeof xy, "%d %d", ev.xbutton.x, ev.xbutton.y);
            e->env.emit(e->env.token, "focus-request", "");
            e->env.emit(e->env.token, "click", xy);
            gv_x11edit_draw(e);
        } else if (ev.type == KeyPress) {
            char buf[8];
            KeySym ks = 0;
            int len = X.lookup(&ev.xkey, buf, sizeof buf, &ks, NULL);
            if (gv_x11edit_trace()) fprintf(stderr, "[x11-edit] key 0x%lx arrived in the component%s\n", (unsigned long)ks,
                                            e->focused ? "" : " (not focused: ignored)");
            /* keys typed while the host was taking the focus away: not ours */
            if (!e->focused) continue;
            size_t tl = strlen(e->text);
            if (ks == XK_Tab || ks == XK_ISO_Left_Tab) {
                e->env.emit(e->env.token, "tab", (ks == XK_ISO_Left_Tab || (ev.xkey.state & ShiftMask)) ? "prev" : "next");
            } else if (ks == XK_Return || ks == XK_KP_Enter) {
                e->env.emit(e->env.token, "enter", e->text);
            } else if (ks == XK_BackSpace) {
                if (tl > 0) { e->text[tl - 1] = 0; e->env.emit(e->env.token, "change", e->text); }
            } else if (len == 1 && (unsigned char)buf[0] >= 32 && (unsigned char)buf[0] < 127 && tl + 1 < sizeof e->text) {
                e->text[tl] = buf[0];
                e->text[tl + 1] = 0;
                e->env.emit(e->env.token, "change", e->text);
            }
            gv_x11edit_draw(e);
        } else if (ev.type == FocusIn || ev.type == FocusOut) {
            /* the host decides the focus; the X focus only follows it */
            if (gv_x11edit_trace())
                fprintf(stderr, "[x11-edit] X %s mode %d detail %d (host says focused %d)\n", ev.type == FocusIn ? "FocusIn" : "FocusOut",
                        ev.xfocus.mode, ev.xfocus.detail, e->focused);
            gv_x11edit_draw(e);
        }
    }
    return n;
}

static void gv_x11edit_destroy(void *inst) {
    gv_x11edit *e = (gv_x11edit *)inst;
    if (e->focused) X.focus(e->d, e->parent, RevertToParent, CurrentTime);
    X.destroy(e->d, e->outer);           /* and inner with it */
    X.free_gc(e->d, e->gc);
    X.flush(e->d);
    X.close(e->d);
    e->magic = 0;
    free(e);
    gv_host_count_destroy();
}

/* tests: what the X server says about the component's windows (outer: the
 * clip window in the parent's coordinates; inner: inside outer) */
int gv_host_x11_query(void *inst, int *outer_mapped, gv_rect *outer, gv_rect *inner, int *clicks, char *text, int cap) {
    /* (also traces who has the X focus, with GV_TRACE_HOST) */
    gv_x11edit *e = (gv_x11edit *)inst;
    if (!e || e->magic != GV_X11EDIT_MAGIC) return -1;
    XWindowAttributes a, b;
    X.sync(e->d, False);
    if (!X.attrs(e->d, e->outer, &a) || !X.attrs(e->d, e->inner, &b)) return -1;
    if (outer_mapped) *outer_mapped = a.map_state != IsUnmapped;
    if (outer) *outer = (gv_rect){a.x, a.y, a.width, a.height};
    if (inner) *inner = (gv_rect){b.x, b.y, b.width, b.height};
    if (clicks) *clicks = e->clicks;
    if (text) snprintf(text, (size_t)cap, "%s", e->text);
    return 0;
}

const gv_host_provider gv_host_x11_provider = {
    GV_HOST_ABI, "x11-edit", gv_x11edit_create, gv_x11edit_place, gv_x11edit_focus, gv_x11edit_command,
    gv_x11edit_pump, gv_x11edit_destroy
};
