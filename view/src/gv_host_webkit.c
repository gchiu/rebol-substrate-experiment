/* gv_host_webkit.c -- "webkit-view": a real browser engine as a foreign
 * component (FOREIGN-2.md).
 *
 * WebKitGTK 4.1 (GTK 3) inside the application's SDL window.  GTK 3's
 * GtkPlug is the client half of the XEmbed protocol: a GTK toplevel made to
 * live inside a window of another program.  The plug is created directly
 * inside this component's own X window (gtk_plug_new(inner)), so it never
 * appears as a toplevel; this provider plays the embedder's half:
 *
 *   SDL's X11 window (the application's)
 *     └─ outer: the visible part of the node (clip) -- X clips to it
 *          └─ inner: the whole component -- the XEmbed socket
 *               └─ GtkPlug (GDK's window) └─ WebKitWebView
 *                                              (WebKitWebProcess, WebKitNetworkProcess)
 *
 * Its loops: pump() runs GTK's main loop without blocking (WebKit's
 * IPC with its helper processes runs on it) and drains this provider's own
 * X connection (XEmbed messages from the plug).  Pointer input goes from the
 * X server straight to GDK's windows; the keyboard follows the X focus,
 * which focus() hands to the plug with XEmbed's focus-in (first / last /
 * current control, from the contract's focus direction).
 *
 * GTK, GLib, WebKitGTK and Xlib are opened at run time: no link or build
 * dependency; without WebKitGTK installed, create() answers why.  GDK is
 * limited to its X11 backend (under WSLg it would pick Wayland, where
 * GtkPlug does not exist).  GTK 4 (WebKitGTK 6.0) removed GtkPlug: this
 * hosting model needs GTK 3. */

#include "gv_host.h"

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

void gv_host_count_create(void);
void gv_host_count_destroy(void);

/* ---- the libraries -------------------------------------------------------------- */
typedef void *gpointer;
typedef struct { uint32_t domain; int code; char *message; } gv_gerror;   /* GError's layout */
static struct {
    int loaded;                                   /* 0 not tried, 1 ok, -1 failed */
    char why[160];
    /* glib / gobject */
    unsigned long (*connect)(gpointer, const char *, void (*)(void), gpointer, void *, int);
    unsigned (*disconnect_matched)(gpointer, int, unsigned, unsigned, void *, void *, gpointer);
    /* gdk / gtk */
    void (*allowed_backends)(const char *);
    int (*init_check)(int *, char ***);
    gpointer (*plug_new)(unsigned long);
    unsigned long (*plug_get_id)(gpointer);
    void (*container_add)(gpointer, gpointer);
    void (*show_all)(gpointer);
    void (*widget_destroy)(gpointer);
    int (*events_pending)(void);
    int (*iteration_do)(int);
    void (*gdk_flush)(void);
    /* webkit */
    gpointer (*view_new)(void);
    void (*load_uri)(gpointer, const char *);
    void (*load_html)(gpointer, const char *, const char *);
    void (*go_back)(gpointer);
    void (*go_forward)(gpointer);
    void (*reload)(gpointer);
    void (*stop)(gpointer);
    const char *(*get_uri)(gpointer);
    const char *(*get_title)(gpointer);
    int (*widget_realized)(gpointer);
    int (*widget_mapped)(gpointer);
    int (*alloc_w)(gpointer);
    int (*alloc_h)(gpointer);
    /* xlib */
    Display *(*xopen)(const char *);
    int (*xclose)(Display *);
    Window (*xcreate)(Display *, Window, int, int, unsigned, unsigned, unsigned, unsigned long, unsigned long);
    int (*xdestroy)(Display *, Window);
    int (*xmap)(Display *, Window);
    int (*xunmap)(Display *, Window);
    int (*xmove_resize)(Display *, Window, int, int, unsigned, unsigned);
    int (*xpending)(Display *);
    int (*xnext)(Display *, XEvent *);
    int (*xflush)(Display *);
    int (*xsync)(Display *, Bool);
    int (*xfocus)(Display *, Window, int, Time);
    Atom (*xatom)(Display *, const char *, Bool);
    Status (*xsend)(Display *, Window, Bool, long, XEvent *);
    int (*xselect)(Display *, Window, long);
    Status (*xattrs)(Display *, Window, XWindowAttributes *);
    XImage *(*ximage)(Display *, Drawable, int, int, unsigned, unsigned, unsigned long, int);
    Status (*xtree)(Display *, Window, Window *, Window *, Window **, unsigned *);
    int (*xfree)(void *);
} W;

static void *gv_wk_open(const char *const *names, char *why, int cap) {
    for (int i = 0; names[i]; i++) {
        void *l = dlopen(names[i], RTLD_NOW | RTLD_GLOBAL);
        if (l) return l;
    }
    snprintf(why, (size_t)cap, "%s not installed (e.g. sudo apt install libwebkit2gtk-4.1-0)", names[0]);
    return NULL;
}
static int gv_wk_load(void) {
    if (W.loaded) return W.loaded;
    W.loaded = -1;
    static const char *const gtk[] = { "libgtk-3.so.0", NULL };
    static const char *const wk[] = { "libwebkit2gtk-4.1.so.0", "libwebkit2gtk-4.0.so.37", NULL };
    static const char *const x[] = { "libX11.so.6", NULL };
    void *lg = gv_wk_open(gtk, W.why, sizeof W.why);
    if (!lg) return -1;
    void *lw = gv_wk_open(wk, W.why, sizeof W.why);
    if (!lw) return -1;
    void *lx = gv_wk_open(x, W.why, sizeof W.why);
    if (!lx) return -1;
    /* GTK exports glib/gobject/gdk symbols through its dependencies (RTLD_GLOBAL) */
#define S(lib, f, n) if (!(*(void **)&W.f = dlsym(lib, n))) { snprintf(W.why, sizeof W.why, "missing %s", n); return -1; }
    S(lg, connect, "g_signal_connect_data") S(lg, disconnect_matched, "g_signal_handlers_disconnect_matched")
    S(lg, allowed_backends, "gdk_set_allowed_backends") S(lg, init_check, "gtk_init_check")
    S(lg, plug_new, "gtk_plug_new") S(lg, plug_get_id, "gtk_plug_get_id")
    S(lg, container_add, "gtk_container_add") S(lg, show_all, "gtk_widget_show_all")
    S(lg, widget_destroy, "gtk_widget_destroy") S(lg, events_pending, "gtk_events_pending")
    S(lg, iteration_do, "gtk_main_iteration_do") S(lg, gdk_flush, "gdk_flush")
    S(lw, view_new, "webkit_web_view_new") S(lw, load_uri, "webkit_web_view_load_uri")
    S(lw, load_html, "webkit_web_view_load_html") S(lw, go_back, "webkit_web_view_go_back")
    S(lw, go_forward, "webkit_web_view_go_forward") S(lw, reload, "webkit_web_view_reload")
    S(lw, stop, "webkit_web_view_stop_loading") S(lw, get_uri, "webkit_web_view_get_uri")
    S(lw, get_title, "webkit_web_view_get_title")
    S(lg, widget_realized, "gtk_widget_get_realized") S(lg, widget_mapped, "gtk_widget_get_mapped")
    S(lg, alloc_w, "gtk_widget_get_allocated_width") S(lg, alloc_h, "gtk_widget_get_allocated_height")
    S(lx, xopen, "XOpenDisplay") S(lx, xclose, "XCloseDisplay") S(lx, xcreate, "XCreateSimpleWindow")
    S(lx, xdestroy, "XDestroyWindow") S(lx, xmap, "XMapWindow") S(lx, xunmap, "XUnmapWindow")
    S(lx, xmove_resize, "XMoveResizeWindow") S(lx, xpending, "XPending") S(lx, xnext, "XNextEvent")
    S(lx, xflush, "XFlush") S(lx, xsync, "XSync") S(lx, xfocus, "XSetInputFocus")
    S(lx, xatom, "XInternAtom") S(lx, xsend, "XSendEvent") S(lx, xselect, "XSelectInput")
    S(lx, xattrs, "XGetWindowAttributes") S(lx, ximage, "XGetImage") S(lx, xtree, "XQueryTree") S(lx, xfree, "XFree")
#undef S
    /* GtkPlug is X11 only: never let GDK choose Wayland (WSLg offers both) */
    W.allowed_backends("x11");
    if (!W.init_check(NULL, NULL)) { snprintf(W.why, sizeof W.why, "GTK could not open the X display"); return -1; }
    W.loaded = 1;
    return 1;
}

/* ---- the component -------------------------------------------------------------------- */
typedef struct gv_webkit {
    unsigned magic;
    gv_host_env env;
    Display *d;
    Window parent, outer, inner, plug_x;
    Atom xembed;
    gpointer plug, view;
    int w, h, mapped, focused, plug_mapped;
    /* a failed load: WebKit ends it with FINISHED (the old page's address)
     * and then loads its own error page for the failed address; both are
     * part of the failure, so neither reports "loaded" (FOREIGN-2 §2) */
    int failing;                      /* a load failed; WebKit's error page may follow */
    int error_page;                   /* the error page's load is under way */
    int held;                         /* a load STARTED while failing: reported at COMMITTED */
    char failed_uri[1024];
    char held_uri[1024];
    char uri[1024];
} gv_webkit;
#define GV_WEBKIT_MAGIC 0x776b7677u

static int gv_wk_trace(void) { return getenv("GV_TRACE_HOST") != NULL; }

/* XEmbed (freedesktop.org XEmbed protocol 0) */
enum { XEMBED_EMBEDDED_NOTIFY = 0, XEMBED_WINDOW_ACTIVATE = 1, XEMBED_WINDOW_DEACTIVATE = 2,
       XEMBED_REQUEST_FOCUS = 3, XEMBED_FOCUS_IN = 4, XEMBED_FOCUS_OUT = 5, XEMBED_FOCUS_NEXT = 6,
       XEMBED_FOCUS_PREV = 7 };
enum { XEMBED_FOCUS_CURRENT = 0, XEMBED_FOCUS_FIRST = 1, XEMBED_FOCUS_LAST = 2 };
static void gv_wk_xembed(gv_webkit *k, long opcode, long detail, long data1) {
    if (!k->plug_x) return;
    XEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.xclient.type = ClientMessage;
    ev.xclient.window = k->plug_x;
    ev.xclient.message_type = k->xembed;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = CurrentTime;
    ev.xclient.data.l[1] = opcode;
    ev.xclient.data.l[2] = detail;
    ev.xclient.data.l[3] = data1;
    ev.xclient.data.l[4] = 0;
    W.xsend(k->d, k->plug_x, False, NoEventMask, &ev);
    W.xflush(k->d);
}

/* WebKit signals -> the contract's events */
static void gv_wk_on_load(gpointer view, int load_event, gpointer data) {
    gv_webkit *k = (gv_webkit *)data;
    if (!k || k->magic != GV_WEBKIT_MAGIC) return;
    const char *u = W.get_uri(view);
    char line[1100];
    if (gv_wk_trace()) fprintf(stderr, "[webkit-view] load-changed %d %s (failing %d, error page %d)\n", load_event, u ? u : "", k->failing, k->error_page);
    /* After a failed load WebKit ends it (FINISHED, the old address), then
     * loads its error page for the failed address -- asynchronously, and
     * that load's STARTED still reports the OLD address (traced,
     * FOREIGN-2 §7.2).  So while a failure is pending, a STARTED is held
     * until its COMMITTED says what it is: the failed address there means
     * the error page, which reports no status ("failed" stays the last
     * one); anything else is a real navigation, reported then. */
    if (load_event == 0) {                               /* WEBKIT_LOAD_STARTED */
        k->error_page = 0;
        if (k->failing) {
            k->held = 1;
            snprintf(k->held_uri, sizeof k->held_uri, "%s", u ? u : "");
            return;
        }
        snprintf(line, sizeof line, "loading %s", u ? u : "");
        k->env.emit(k->env.token, "status", line);
    } else if (load_event == 2) {                        /* COMMITTED: the page's address */
        if (k->failing && u && !strcmp(u, k->failed_uri)) {
            k->error_page = 1;                           /* the error page: its address is the failed one */
            k->held = 0;
        } else if (k->held || k->failing) {
            snprintf(line, sizeof line, "loading %s", k->held ? k->held_uri : (u ? u : ""));
            k->env.emit(k->env.token, "status", line);
            k->held = 0;
            k->failing = 0;
        }
        snprintf(k->uri, sizeof k->uri, "%s", u ? u : "");
        k->env.emit(k->env.token, "change", k->uri);
        k->env.emit(k->env.token, "url", k->uri);      /* the property by name (follow ... url) */
    } else if (load_event == 3) {                        /* FINISHED */
        if (k->error_page) { k->error_page = 0; k->failing = 0; return; }
        if (k->failing) return;                          /* the failed load itself ended */
        snprintf(line, sizeof line, "loaded %s", u ? u : "");
        k->env.emit(k->env.token, "status", line);
    }
}
/* the page's title arrives on its own, often after FINISHED:
 * notify::title (GObject*, GParamSpec*, gpointer) */
static void gv_wk_on_title(gpointer view, gpointer pspec, gpointer data) {
    (void)pspec;
    gv_webkit *k = (gv_webkit *)data;
    if (!k || k->magic != GV_WEBKIT_MAGIC) return;
    const char *t = W.get_title(view);
    if (t && *t) k->env.emit(k->env.token, "title", t);
}
static int gv_wk_on_fail(gpointer view, int load_event, const char *uri, gv_gerror *err, gpointer data) {
    (void)view; (void)load_event;
    gv_webkit *k = (gv_webkit *)data;
    if (!k || k->magic != GV_WEBKIT_MAGIC) return 0;
    char line[1100];
    snprintf(line, sizeof line, "failed %s: %s", uri ? uri : "", err && err->message ? err->message : "unknown error");
    k->env.emit(k->env.token, "status", line);
    snprintf(k->failed_uri, sizeof k->failed_uri, "%s", uri ? uri : "");
    k->failing = 1;
    k->held = 0;
    return 0;                                            /* WebKit shows its own error page */
}
static void gv_wk_on_crash(gpointer view, int reason, gpointer data) {
    (void)view;
    gv_webkit *k = (gv_webkit *)data;
    if (!k || k->magic != GV_WEBKIT_MAGIC) return;
    char line[80];
    snprintf(line, sizeof line, "failed web process terminated (reason %d)", reason);
    k->env.emit(k->env.token, "status", line);
}

/* The URI for an address typed by a user or given by the application:
 * "https://..." "file:///..." "about:" "data:" as given; "/path" becomes
 * file://; "./x" "../x" are resolved against the working directory (the
 * demo runs from view/); a bare name gets https://.  Writes at most cap
 * bytes; returns 0, or -1 (empty, or too long for out -- never truncated:
 * a cut-off address would load some other page).
 *
 * FOREIGN-2 correction: realpath() was given a 1024-byte buffer.  Its
 * contract is a buffer of PATH_MAX bytes (4096), so a long enough path
 * would have been written past it; glibc's _FORTIFY_SOURCE check aborted
 * every relative address instead ("*** buffer overflow detected ***").
 * realpath now allocates its own result. */
int gv_host_webkit_resolve(const char *arg, char *out, int cap) {
    int n;
    if (!arg || !*arg || !out || cap <= 0) return -1;
    if (strstr(arg, "://") || !strncmp(arg, "about:", 6) || !strncmp(arg, "data:", 5)) n = snprintf(out, (size_t)cap, "%s", arg);
    else if (arg[0] == '/') n = snprintf(out, (size_t)cap, "file://%s", arg);
    else if (!strncmp(arg, "./", 2) || !strncmp(arg, "../", 3)) {
        char *abs = realpath(arg, NULL);
        if (abs) {
            n = snprintf(out, (size_t)cap, "file://%s", abs);
            free(abs);
        } else {
            /* not there: still an absolute file URI, so the load fails
             * visibly (load-failed names the path) */
            char *cwd = getcwd(NULL, 0);
            n = snprintf(out, (size_t)cap, "file://%s/%s", cwd ? cwd : "", arg + (arg[1] == '/' ? 2 : 0));
            free(cwd);
        }
    }
    else n = snprintf(out, (size_t)cap, "https://%s", arg);
    return n < 0 || n >= cap ? -1 : 0;
}
static void gv_wk_navigate(gv_webkit *k, const char *arg) {
    char u[4200];
    if (!arg || !*arg) return;
    if (gv_host_webkit_resolve(arg, u, (int)sizeof u) < 0) {
        k->env.emit(k->env.token, "status", "failed address too long");
        return;
    }
    W.load_uri(k->view, u);
}

static void *gv_wk_create(const gv_host_env *env, const char *props, char *err, int errcap) {
    if (!env || !env->native_parent || !env->platform || strcmp(env->platform, "x11")) {
        snprintf(err, (size_t)errcap, "webkit-view needs an X11 window (platform %s)", env && env->platform ? env->platform : "?");
        return NULL;
    }
    if (gv_wk_load() < 0) { snprintf(err, (size_t)errcap, "%s", W.why); return NULL; }
    gv_webkit *k = (gv_webkit *)calloc(1, sizeof *k);
    if (!k) { snprintf(err, (size_t)errcap, "out of memory"); return NULL; }
    k->d = W.xopen(NULL);
    if (!k->d) { free(k); snprintf(err, (size_t)errcap, "cannot open the X display"); return NULL; }
    k->magic = GV_WEBKIT_MAGIC;
    k->env = *env;
    k->parent = (Window)(uintptr_t)env->native_parent;
    k->xembed = W.xatom(k->d, "_XEMBED", False);
    k->outer = W.xcreate(k->d, k->parent, 0, 0, 1, 1, 0, 0, 0xffffff);
    k->inner = W.xcreate(k->d, k->outer, 0, 0, 1, 1, 0, 0, 0xffffff);
    W.xselect(k->d, k->inner, SubstructureNotifyMask);
    W.xmap(k->d, k->inner);
    W.xsync(k->d, False);
    /* the plug inside our socket window; the web view inside the plug */
    k->plug = W.plug_new((unsigned long)k->inner);
    k->view = W.view_new();
    W.container_add(k->plug, k->view);
    W.connect(k->view, "load-changed", (void (*)(void))gv_wk_on_load, k, NULL, 0);
    W.connect(k->view, "load-failed", (void (*)(void))gv_wk_on_fail, k, NULL, 0);
    W.connect(k->view, "web-process-terminated", (void (*)(void))gv_wk_on_crash, k, NULL, 0);
    W.connect(k->view, "notify::title", (void (*)(void))gv_wk_on_title, k, NULL, 0);
    W.show_all(k->plug);
    for (int i = 0; i < 20 && W.events_pending(); i++) W.iteration_do(0);
    k->plug_x = (Window)W.plug_get_id(k->plug);
    gv_wk_xembed(k, XEMBED_EMBEDDED_NOTIFY, 0, (long)k->inner);
    if (props && *props) gv_wk_navigate(k, props);
    gv_host_count_create();
    if (gv_wk_trace()) fprintf(stderr, "[webkit-view] created: plug 0x%lx in 0x%lx\n", (unsigned long)k->plug_x, (unsigned long)k->inner);
    return k;
}

static void gv_wk_place(void *inst, gv_rect b, gv_rect c, int visible) {
    gv_webkit *k = (gv_webkit *)inst;
    if (!visible || c.w <= 0 || c.h <= 0) {
        if (k->mapped) { W.xunmap(k->d, k->outer); k->mapped = 0; }
        W.xflush(k->d);
        return;
    }
    W.xmove_resize(k->d, k->outer, c.x, c.y, (unsigned)c.w, (unsigned)c.h);
    W.xmove_resize(k->d, k->inner, b.x - c.x, b.y - c.y, (unsigned)b.w, (unsigned)b.h);
    if (k->plug_x) {
        /* XEmbed: the embedder sizes AND maps the client window -- GtkPlug
         * never maps itself in a foreign socket (FOREIGN-2 §7.2: without
         * this the page loaded but only our own white window showed) */
        W.xmove_resize(k->d, k->plug_x, 0, 0, (unsigned)b.w, (unsigned)b.h);
        if (!k->plug_mapped) { W.xmap(k->d, k->plug_x); k->plug_mapped = 1; }
    }
    k->w = b.w;
    k->h = b.h;
    if (!k->mapped) { W.xmap(k->d, k->outer); k->mapped = 1; }
    W.xflush(k->d);
}

static int gv_wk_focus(void *inst, int how) {
    gv_webkit *k = (gv_webkit *)inst;
    if (how && k->mapped && k->plug_x) {
        long detail = how == GV_HOST_FOCUS_FIRST ? XEMBED_FOCUS_FIRST
                    : how == GV_HOST_FOCUS_LAST ? XEMBED_FOCUS_LAST : XEMBED_FOCUS_CURRENT;
        W.xfocus(k->d, k->plug_x, RevertToParent, CurrentTime);
        gv_wk_xembed(k, XEMBED_WINDOW_ACTIVATE, 0, 0);
        gv_wk_xembed(k, XEMBED_FOCUS_IN, detail, 0);
        k->focused = 1;
    } else if (!how && k->focused) {
        gv_wk_xembed(k, XEMBED_FOCUS_OUT, 0, 0);
        gv_wk_xembed(k, XEMBED_WINDOW_DEACTIVATE, 0, 0);
        W.xfocus(k->d, k->parent, RevertToParent, CurrentTime);
        k->focused = 0;
    }
    W.xflush(k->d);
    if (gv_wk_trace()) fprintf(stderr, "[webkit-view] focus(%d) -> %d\n", how, k->focused);
    return k->focused;
}

static int gv_wk_command(void *inst, const char *cmd, const char *arg) {
    gv_webkit *k = (gv_webkit *)inst;
    if (!strcmp(cmd, "set-text") || !strcmp(cmd, "navigate")) gv_wk_navigate(k, arg);
    else if (!strcmp(cmd, "load-html")) W.load_html(k->view, arg ? arg : "", "about:blank");
    else if (!strcmp(cmd, "back")) W.go_back(k->view);
    else if (!strcmp(cmd, "forward")) W.go_forward(k->view);
    else if (!strcmp(cmd, "reload")) W.reload(k->view);
    else if (!strcmp(cmd, "stop")) W.stop(k->view);
    else return -1;
    return 0;
}

static int gv_wk_pump(void *inst) {
    gv_webkit *k = (gv_webkit *)inst;
    int n = 0;
    /* GTK's loop (WebKit's IPC, drawing, input to its windows): bounded */
    for (int i = 0; i < 200 && W.events_pending(); i++) { W.iteration_do(0); n++; }
    /* the plug's XEmbed requests to us, its embedder */
    while (W.xpending(k->d) > 0) {
        XEvent ev;
        W.xnext(k->d, &ev);
        n++;
        if (ev.type != ClientMessage || ev.xclient.message_type != k->xembed) continue;
        long op = ev.xclient.data.l[1];
        if (gv_wk_trace()) fprintf(stderr, "[webkit-view] XEmbed message %ld from the plug\n", op);
        if (op == XEMBED_REQUEST_FOCUS) k->env.emit(k->env.token, "focus-request", "");
        else if (op == XEMBED_FOCUS_NEXT) k->env.emit(k->env.token, "tab", "next");
        else if (op == XEMBED_FOCUS_PREV) k->env.emit(k->env.token, "tab", "prev");
    }
    return n;
}

static void gv_wk_destroy(void *inst) {
    gv_webkit *k = (gv_webkit *)inst;
    if (k->focused) W.xfocus(k->d, k->parent, RevertToParent, CurrentTime);
    /* no callback may reach the host after this */
    W.disconnect_matched(k->view, 16 /* G_SIGNAL_MATCH_DATA */, 0, 0, NULL, NULL, k);
    k->magic = 0;
    W.widget_destroy(k->plug);                          /* the web view with it */
    for (int i = 0; i < 50 && W.events_pending(); i++) W.iteration_do(0);
    /* GDK's unmap / destroy of the plug's window are only queued on GDK's
     * own X connection: they must reach the server before our window (the
     * plug's ancestor) goes, or GDK's requests name a window that no longer
     * exists -- BadWindow, which GDK makes fatal (SIGTRAP; FOREIGN-2 §7.2) */
    W.gdk_flush();
    W.xdestroy(k->d, k->outer);
    W.xflush(k->d);
    W.xclose(k->d);
    free(k);
    gv_host_count_destroy();
}

/* diagnostics, read only (tests, FOREIGN-2 §7.2): what X and GTK say about
 * the component's windows and widgets, and what its pixels are */
int gv_host_webkit_query(void *inst, gv_host_webkit_state *q) {
    gv_webkit *k = (gv_webkit *)inst;
    if (!k || k->magic != GV_WEBKIT_MAGIC || !q) return -1;
    memset(q, 0, sizeof *q);
    XWindowAttributes a;
    Window wins[3] = { k->outer, k->inner, k->plug_x };
    for (int i = 0; i < 3; i++) {
        if (!wins[i] || !W.xattrs(k->d, wins[i], &a)) continue;
        q->map_state[i] = a.map_state;                 /* 0 unmapped, 1 unviewable, 2 viewable */
        q->geom[i].x = a.x; q->geom[i].y = a.y; q->geom[i].w = a.width; q->geom[i].h = a.height;
    }
    Window root = 0, parent = 0, *kids = NULL;
    unsigned nk = 0;
    if (k->plug_x && W.xtree(k->d, k->plug_x, &root, &parent, &kids, &nk)) {
        q->plug_in_inner = parent == k->inner;
        if (kids) W.xfree(kids);
    }
    q->plug_realized = W.widget_realized(k->plug);
    q->plug_mapped = W.widget_mapped(k->plug);
    q->view_realized = W.widget_realized(k->view);
    q->view_mapped = W.widget_mapped(k->view);
    q->view_w = W.alloc_w(k->view);
    q->view_h = W.alloc_h(k->view);
    /* the pixels of the inner window (what is on the screen there): a grid
     * of samples; white ones, and how many distinct values */
    if (q->map_state[1] == IsViewable && q->geom[1].w > 0 && q->geom[1].h > 0) {
        XImage *im = W.ximage(k->d, k->inner, 0, 0, (unsigned)q->geom[1].w, (unsigned)q->geom[1].h, AllPlanes, ZPixmap);
        if (im) {
            unsigned long seen[16];
            int ns = 0;
            for (int y = 2; y < q->geom[1].h; y += 7)
                for (int x = 2; x < q->geom[1].w; x += 7) {
                    unsigned long px = XGetPixel(im, x, y) & 0xffffff;
                    q->samples++;
                    if (px == 0xffffff) q->white++;
                    int known = 0;
                    for (int i = 0; i < ns; i++) if (seen[i] == px) known = 1;
                    if (!known && ns < 16) seen[ns++] = px;
                }
            q->distinct = ns;
            if (q->geom[1].w > 10 && q->geom[1].h > 10) q->top_left = (unsigned)(XGetPixel(im, 10, 10) & 0xffffff);
            /* GV_WEBKIT_SNAPSHOT=file.ppm: the inner window as a PPM image,
             * once (an existing file is left alone: "x" = O_EXCL) */
            const char *snap = getenv("GV_WEBKIT_SNAPSHOT");
            FILE *f = snap && *snap ? fopen(snap, "wbx") : NULL;
            if (f) {
                fprintf(f, "P6\n%d %d\n255\n", q->geom[1].w, q->geom[1].h);
                for (int y = 0; y < q->geom[1].h; y++)
                    for (int x = 0; x < q->geom[1].w; x++) {
                        unsigned long px = XGetPixel(im, x, y);
                        unsigned char rgb[3] = { (unsigned char)(px >> 16), (unsigned char)(px >> 8), (unsigned char)px };
                        fwrite(rgb, 1, 3, f);
                    }
                fclose(f);
            }
            XDestroyImage(im);
        }
    }
    return 0;
}

const gv_host_provider gv_host_webkit_provider = {
    GV_HOST_ABI, "webkit-view", gv_wk_create, gv_wk_place, gv_wk_focus, gv_wk_command, gv_wk_pump, gv_wk_destroy
};
