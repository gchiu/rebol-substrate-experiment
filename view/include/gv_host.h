#ifndef GV_HOST_H
#define GV_HOST_H

/* gv_host.h -- foreign view hosting, phase 1 (FOREIGN-1.md).
 *
 * A *provider* puts an independently rendered component (a native child
 * surface, later a web view, a PDF viewer, a native editor) inside a View
 * node.  The host (gv_glon_host.c) owns the node's place in the View tree
 * and decides geometry, clipping, visibility and focus; the component owns
 * its pixels, its input while it has the focus, and its own event loop.
 *
 *   host -> component          component -> host (env->emit)
 *   place (bounds, clip, vis)  "focus-request"  the user pressed into it
 *   focus (on / off)           "tab" next|prev  keyboard focus leaves it
 *   command (text, text)       "change" value / "enter" value / "click" x y
 *   pump (its events)          any other name: passed to the application
 *   destroy (exactly once)
 *
 * Rectangles are window coordinates.  `clip` is the part of `bounds` the
 * component may show (clip and viewport ancestors, the window); an empty
 * clip or visible = 0 means it must show nothing.  Emitted events are
 * queued by the host and delivered after pump(); none are delivered for an
 * instance after its destroy().  Nothing here is browser specific. */

#include "gv_view.h"

#define GV_HOST_ABI 1

/* focus(): how the keyboard focus arrived (FOREIGN-2; compatible with the
 * first contract, whose 1 meant "on") */
#define GV_HOST_FOCUS_OFF     0
#define GV_HOST_FOCUS_CURRENT 1
#define GV_HOST_FOCUS_FIRST   2
#define GV_HOST_FOCUS_LAST    3

typedef struct gv_host_env {
    /* the platform parent the component may attach a child surface to: an
     * X11 Window id (cast through uintptr_t) or an HWND; NULL when the
     * backend has no native window (the dummy driver) */
    void *native_parent;
    const char *platform;          /* "x11", "win32", "none" */
    void (*emit)(void *token, const char *name, const char *detail);
    void *token;                   /* given back with every emit */
} gv_host_env;

typedef struct gv_host_provider {
    int abi;                       /* GV_HOST_ABI */
    const char *kind;              /* "mock", "x11-edit", ... */
    /* make an instance; NULL with a reason in err when it cannot (no
     * display, missing runtime): the host then shows nothing and reports
     * host-state failed to the application */
    void *(*create)(const gv_host_env *env, const char *props, char *err, int errcap);
    void (*place)(void *inst, gv_rect bounds, gv_rect clip, int visible);
    /* take (non-zero) or give back (0) the keyboard; answers 1 if it
     * accepts.  The non-zero value says how the focus arrived, for
     * components with a focus chain of their own (FOREIGN-2):
     * GV_HOST_FOCUS_CURRENT (a click, a request), GV_HOST_FOCUS_FIRST (Tab
     * into it: its first control), GV_HOST_FOCUS_LAST (Shift+Tab into it:
     * its last).  A component without a chain treats any non-zero as on. */
    int (*focus)(void *inst, int how);
    /* content commands, e.g. "set-text" "hello"; answers 0 when handled */
    int (*command)(void *inst, const char *cmd, const char *arg);
    /* the component's own events, once per loop iteration; answers the
     * number it handled */
    int (*pump)(void *inst);
    void (*destroy)(void *inst);
} gv_host_provider;

/* providers, by kind; mock and x11-edit are built in */
int gv_host_register(const gv_host_provider *p);
const gv_host_provider *gv_host_find(const char *kind);

/* the mock provider's record of what it was told (tests): it renders
 * nothing; it stands in for a component in the dummy-driver tests */
typedef struct gv_host_mock_state {
    gv_rect bounds, clip;
    int visible, focused, places, focus_calls, commands;
    char text[256];
    int focus_how;                 /* the last focus() value (GV_HOST_FOCUS_*) */
    char last_cmd[32], last_arg[256];
} gv_host_mock_state;
int gv_host_mock_state_of(void *inst, gv_host_mock_state *out);
/* emit as the component would (a user's input inside it), for tests */
void gv_host_mock_emit(void *inst, const char *name, const char *detail);
/* x11-edit, for tests: the X server's view of its windows */
int gv_host_x11_query(void *inst, int *outer_mapped, gv_rect *outer, gv_rect *inner, int *clicks, char *text, int cap);
/* instances alive / created / destroyed, every provider */
/* webkit-view's diagnostics (read only): X map state (0 unmapped,
 * 1 unviewable, 2 viewable) and geometry of [outer, inner, plug]; GTK's view
 * of the plug and the web view; a sample of the inner window's pixels */
typedef struct gv_host_webkit_state {
    int map_state[3];
    gv_rect geom[3];
    int plug_in_inner;
    int plug_realized, plug_mapped, view_realized, view_mapped, view_w, view_h;
    int samples, white, distinct;
    unsigned top_left;               /* 0xRRGGBB at (10, 10) of the inner window */
} gv_host_webkit_state;
int gv_host_webkit_query(void *inst, gv_host_webkit_state *q);
/* webkit-view's address-to-URI rule (gv_host_webkit.c; tested without the
 * engine): 0, or -1 if empty or too long for out (never truncated) */
int gv_host_webkit_resolve(const char *arg, char *out, int cap);
void gv_host_counts(int *alive, int *created, int *destroyed);

#endif
