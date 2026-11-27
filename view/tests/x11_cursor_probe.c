/* view/tests/x11_cursor_probe.c -- an LD_PRELOAD probe of what a program
 * asks the X server about cursors and window-manager moves / resizes.
 *
 *   cc -shared -fPIC -o x11_cursor_probe.so tests/x11_cursor_probe.c -ldl
 *   GV_X11_PROBE_LOG=/tmp/probe.log LD_PRELOAD=$PWD/x11_cursor_probe.so PROGRAM
 *
 * It records, one line each (to GV_X11_PROBE_LOG, else stderr):
 *   font-cursor SHAPE -> CURSOR      XCreateFontCursor (SDL's system cursors)
 *   image-cursor -> CURSOR           XcursorImageLoadCursor (SDL's bitmap cursors)
 *   define WINDOW CURSOR             XDefineCursor
 *   undefine WINDOW                  XUndefineCursor
 *   client-message ATOM to WINDOW about WINDOW: l0 l1 l2 l3 l4   XSendEvent
 *     (_NET_WM_MOVERESIZE: l2 is the direction, 0..7 resize, 8 move)
 * and passes every call on.  SDL 2.0.20 links libX11 and libXcursor
 * directly, so these calls go through the dynamic linker and are seen.  The
 * probe unsets LD_PRELOAD so child processes do not inherit it.  It makes no
 * request of its own except XGetAtomName for a client message's type.
 */
#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

static FILE *probe_out(void) {
    static FILE *f;
    if (!f) {
        const char *p = getenv("GV_X11_PROBE_LOG");
        f = p ? fopen(p, "a") : NULL;
        if (!f) f = stderr;
        setvbuf(f, NULL, _IOLBF, 0);
    }
    return f;
}
__attribute__((constructor)) static void probe_init(void) { unsetenv("LD_PRELOAD"); }

Cursor XCreateFontCursor(Display *d, unsigned int shape) {
    static Cursor (*real)(Display *, unsigned int);
    if (!real) real = (Cursor (*)(Display *, unsigned int))dlsym(RTLD_NEXT, "XCreateFontCursor");
    Cursor c = real(d, shape);
    fprintf(probe_out(), "[x11] font-cursor %u -> 0x%lx\n", shape, (unsigned long)c);
    return c;
}
typedef struct _XcursorImage XcursorImage;
Cursor XcursorImageLoadCursor(Display *d, const XcursorImage *img) {
    static Cursor (*real)(Display *, const XcursorImage *);
    if (!real) real = (Cursor (*)(Display *, const XcursorImage *))dlsym(RTLD_NEXT, "XcursorImageLoadCursor");
    Cursor c = real ? real(d, img) : 0;
    fprintf(probe_out(), "[x11] image-cursor -> 0x%lx\n", (unsigned long)c);
    return c;
}
int XDefineCursor(Display *d, Window w, Cursor c) {
    static int (*real)(Display *, Window, Cursor);
    if (!real) real = (int (*)(Display *, Window, Cursor))dlsym(RTLD_NEXT, "XDefineCursor");
    fprintf(probe_out(), "[x11] define 0x%lx 0x%lx\n", (unsigned long)w, (unsigned long)c);
    return real(d, w, c);
}
int XUndefineCursor(Display *d, Window w) {
    static int (*real)(Display *, Window);
    if (!real) real = (int (*)(Display *, Window))dlsym(RTLD_NEXT, "XUndefineCursor");
    fprintf(probe_out(), "[x11] undefine 0x%lx\n", (unsigned long)w);
    return real(d, w);
}
Status XSendEvent(Display *d, Window w, Bool prop, long mask, XEvent *ev) {
    static Status (*real)(Display *, Window, Bool, long, XEvent *);
    static char *(*name)(Display *, Atom);
    if (!real) real = (Status (*)(Display *, Window, Bool, long, XEvent *))dlsym(RTLD_NEXT, "XSendEvent");
    if (!name) name = (char *(*)(Display *, Atom))dlsym(RTLD_NEXT, "XGetAtomName");
    if (ev && ev->type == ClientMessage) {
        char *n = ev->xclient.message_type && name ? name(d, ev->xclient.message_type) : NULL;
        fprintf(probe_out(), "[x11] client-message %s to 0x%lx about 0x%lx: %ld %ld %ld %ld %ld\n", n ? n : "(none)",
                (unsigned long)w, (unsigned long)ev->xclient.window, ev->xclient.data.l[0], ev->xclient.data.l[1],
                ev->xclient.data.l[2], ev->xclient.data.l[3], ev->xclient.data.l[4]);
        if (n) XFree(n);
    }
    return real(d, w, prop, mask, ev);
}
