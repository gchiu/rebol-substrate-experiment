#ifndef GV_BACKEND_H
#define GV_BACKEND_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gv_color {
    uint8_t r, g, b, a;
} gv_color;

typedef struct gv_rect {
    int x, y, w, h;
} gv_rect;

typedef struct gv_transform {
    double a, b, c, d, e, f;
} gv_transform;

typedef enum gv_draw_op {
    GV_DRAW_PUSH = 0,
    GV_DRAW_POP,
    GV_DRAW_TRANSFORM,
    GV_DRAW_CLIP,
    GV_DRAW_RECT,
    GV_DRAW_LINE,
    GV_DRAW_TEXT,
    GV_DRAW_IMAGE
} gv_draw_op;

typedef struct gv_cmd {
    gv_draw_op op;
    gv_rect rect;
    gv_color color;
    gv_color color2;
    double f1, f2, f3, f4;
    /* INTENT-4B-2, text: f5 selection anchor byte index + 1 (0: none; the
     * selection runs from it to the caret, f3); f6 1 = an editable text,
     * clipped to rect and scrolled so the caret stays visible */
    double f5, f6;
    const char *text;
    uint32_t text_len;
    const uint8_t *pixels;
    int image_w, image_h;
    gv_transform transform;
    /* INTENT-4B-5A: rect: f1 corner radius; text: font_style (bit 0 bold,
     * bits 1-2 family); image: format 1 = premultiplied ARGB32, stride 4w
     * (else RGBA8), drawn scaled into rect */
    int font_style;
    int format;
} gv_cmd;

typedef struct gv_display_list {
    gv_cmd *cmds;
    int count;
    int cap;
    int overflow;
} gv_display_list;

void gv_display_list_init(gv_display_list *dl);
void gv_display_list_free(gv_display_list *dl);
int gv_display_list_add(gv_display_list *dl, const gv_cmd *cmd);

typedef enum gv_cap {
    GV_CAP_WINDOW = 0,
    GV_CAP_HEADLESS,
    GV_CAP_NATIVE_CONTROLS,
    GV_CAP_NATIVE_TEXT,
    GV_CAP_TSF,
    GV_CAP_UIA,
    GV_CAP_SPELLCHECK,
    GV_CAP_IME,
    GV_CAP_CLIPBOARD,
    GV_CAP_TOUCH,
    GV_CAP_ACCESSIBILITY,
    GV_CAP_HIGH_DPI,
    GV_CAP_IMAGE_DECODE,
    GV_CAP_TEXT_SHAPE,
    GV_CAP_COUNT
} gv_cap;

typedef enum gv_event_type {
    GV_EVENT_NONE = 0,
    GV_EVENT_QUIT,
    GV_EVENT_POINTER_MOVE,
    GV_EVENT_POINTER_DOWN,
    GV_EVENT_POINTER_UP,
    GV_EVENT_KEY_DOWN,
    GV_EVENT_KEY_UP,
    GV_EVENT_TEXT,
    GV_EVENT_WHEEL,
    GV_EVENT_RESIZE,
    GV_EVENT_FOCUS,
    GV_EVENT_TICK,
    /* GLON-INST-3B: a secondary native window's close button */
    GV_EVENT_CLOSE
} gv_event_type;

typedef struct gv_event {
    gv_event_type type;
    int x, y;
    int key;
    unsigned int mods;
    unsigned int codepoint;
    int width, height;
    int id;
} gv_event;

typedef struct gv_backend_config {
    int width;
    int height;
    const char *title;
    int headless;
    int vsync;
} gv_backend_config;

typedef struct gv_backend gv_backend;

struct gv_backend {
    int (*init)(gv_backend *b, const gv_backend_config *cfg);
    void (*shutdown)(gv_backend *b);
    void (*destroy)(gv_backend *b);
    int (*create_window)(gv_backend *b, int w, int h, const char *title);
    void (*destroy_window)(gv_backend *b);
    int (*resize_window)(gv_backend *b, int w, int h);
    int (*begin_frame)(gv_backend *b);
    int (*draw)(gv_backend *b, const gv_display_list *dl);
    int (*end_frame)(gv_backend *b);
    int (*poll_event)(gv_backend *b, gv_event *ev);
    int (*has_cap)(gv_backend *b, gv_cap cap);
    void *(*native_handle)(gv_backend *b);
    int (*native_control_create)(gv_backend *b, int kind, gv_rect rect,
                                 const char *text, void **out);
    void (*native_control_destroy)(gv_backend *b, void *ctrl);
    int (*native_control_set_text)(gv_backend *b, void *ctrl, const char *text);
    int (*native_control_get_text)(gv_backend *b, void *ctrl, char *buf, int cap);
    int (*native_control_set_focus)(gv_backend *b, void *ctrl, int focus);
    int (*set_clipboard)(gv_backend *b, const char *text);
    int (*get_clipboard)(gv_backend *b, char *buf, int cap);
    void *impl;
    char name[32];
    /* optional (GUI-P2; last, so earlier offsets are unchanged): break one
     * line of UTF-8 text (no newline) into rows no wider than width px at font
     * size, at the backend's own shaping and
     * line-break positions: byte offsets/lengths into s, answers the count
     * (at most max).  NULL where the backend cannot measure text. */
    int (*wrap_text)(gv_backend *b, const char *s, int len, double size, int width,
                     int *starts, int *lens, int max);
    /* optional (GUI-P2, appended): the drawn width in px of UTF-8 text at a
     * font size; NULL where the backend cannot measure text */
    int (*measure_text)(gv_backend *b, const char *s, int len, double size);
    /* optional (GUI-P2, appended): the pointer's shape over the window,
     * GV_CURSOR_*; NULL where the backend has no pointer */
    void (*set_cursor)(gv_backend *b, int kind);
    /* optional (GUI-P2, appended): keep receiving pointer motion and the
     * release while the pointer is outside the window (1), or stop (0).
     * Asked for only while a drag needs it (a scrollbar thumb, a column
     * divider): a grab held otherwise stops the window manager's frame
     * resizing (WSLg).  The backend also drops it on its own at a release
     * with no button held, at window focus loss (reported as
     * GV_EVENT_FOCUS, key 0) and at shutdown. */
    int (*set_capture)(gv_backend *b, int on);
    /* INTENT-4B-5A, optional: decode a PNG or JPEG file into premultiplied
     * ARGB32 pixels (malloc'd, stride 4w): 0, or -1 with the reason in err.
     * Backends without it (headless) show image nodes as placeholders. */
    int (*decode_image)(gv_backend *b, const char *path, uint8_t **pixels, int *w, int *h, char *err, int errcap);
};
#define GV_CURSOR_NORMAL 0
#define GV_CURSOR_COL_RESIZE 1

gv_backend *gv_backend_headless(void);
gv_backend *gv_backend_sdl(void);

uint64_t gv_backend_headless_hash(gv_backend *b);
int gv_backend_sdl_save_png(gv_backend *b, const char *path);
/* GUI-P2 window-edge resizing: the resize zone (an SDL_HitTestResult value,
 * 0 = none) at x, y in a w x h window; the zone whose cursor is shown; and
 * whether the platform accepted the edge hit test (not the dummy driver). */
int gv_backend_sdl_edge_zone(int w, int h, int x, int y);
int gv_backend_sdl_cursor_zone(gv_backend *b);
/* GUI-P2: 1 while an edge-zone resize cursor is shown (only with the hit
 * test installed, so a press there resizes) */
int gv_backend_sdl_edge_cursor_shown(gv_backend *b);
/* GUI-P2: the edge band's width in px (GV_EDGE_WIDTH, 4..16, default 6) */
int gv_backend_sdl_edge_width(void);
/* GUI-P2: one line on the native window: video driver, SDL flags
 * (resizable, capture), sizes and limits, and on X11 the size hints and
 * the actions the window manager allows (GV_TRACE_WINDOW=1 prints it) */
int gv_backend_sdl_window_report(gv_backend *b, char *out, int cap);
/* INTENT-4B-3: the edge-cursor decision (the zone whose cursor to show, and
 * why): a pure function of the hit test, the zone, a capture and buttons */
int gv_backend_sdl_edge_cursor_decision(int hit_test, int zone, int captured, int buttons, const char **why);
/* GUI-P2: 1 while the application holds a pointer capture (asked for
 * through set_capture and not yet dropped) */
int gv_backend_sdl_captured(gv_backend *b);
/* GUI-P2: the shape the application last asked for (GV_CURSOR_*) */
int gv_backend_sdl_app_cursor(gv_backend *b);
int gv_backend_sdl_hit_test_active(gv_backend *b);
int gv_native_available(void);
/* GLON-INST-3B: how a host makes and closes secondary windows of a
 * backend kind (gv_glon_host_set_window_ops); a host without one makes
 * headless ones */
typedef struct gv_window_ops {
    gv_backend *(*make)(void);
    void (*close)(gv_backend *b);
    void (*set_title)(gv_backend *b, const char *title);
    void (*raise)(gv_backend *b);
    unsigned (*window_id)(gv_backend *b);
    long (*dropped)(void);
} gv_window_ops;
const gv_window_ops *gv_backend_sdl_window_ops(void);
/* GLON-INST-3B: secondary native windows in the same SDL session (their
 * events routed by window id; closing one leaves SDL running) */
gv_backend *gv_backend_sdl_secondary(void);
void gv_backend_sdl_close_secondary(gv_backend *b);
unsigned gv_backend_sdl_window_id(gv_backend *b);
long gv_backend_sdl_dropped_events(void);
void gv_backend_sdl_set_title(gv_backend *b, const char *title);
void gv_backend_sdl_raise(gv_backend *b);

#ifdef __cplusplus
}
#endif

#endif