#ifndef GV_VIEW_H
#define GV_VIEW_H

#include "gv_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int gv_node_id;

typedef enum gv_node_type {
    GV_NODE_FREE = 0,
    GV_NODE_WINDOW,
    GV_NODE_GROUP,
    GV_NODE_RECT,
    GV_NODE_TEXT,
    GV_NODE_IMAGE,
    GV_NODE_TRANSFORM,
    GV_NODE_CLIP,
    GV_NODE_VIEWPORT,
    GV_NODE_CUSTOM,
    GV_NODE_NATIVE_CONTROL
} gv_node_type;

typedef enum gv_layout_dir {
    GV_LAYOUT_NONE = 0,
    GV_LAYOUT_H,
    GV_LAYOUT_V
} gv_layout_dir;

typedef enum gv_align {
    GV_ALIGN_START = 0,
    GV_ALIGN_CENTER,
    GV_ALIGN_END,
    GV_ALIGN_STRETCH
} gv_align;

typedef struct gv_node {
    gv_node_type type;
    gv_node_id parent;
    gv_node_id first_child;
    gv_node_id last_child;
    gv_node_id next_sibling;
    gv_node_id prev_sibling;
    gv_node_id free_next;

    int x, y, w, h;
    int base_w, base_h;
    int layout_dir;
    int align;
    int spacing;
    int padding;
    int fill_x;
    int fill_y;
    int min_w, min_h;
    int max_w, max_h;

    gv_transform transform;
    gv_color bg;
    gv_color fg;
    char *text;
    double font_size;
    int text_align;
    int caret;          /* text caret: byte index + 1, 0 = none (GUI-P2) */
    int sel_anchor;     /* selection anchor: byte index + 1, 0 = none (INTENT-4B-2) */
    int edit;           /* 1: editable text, clipped and scrolled to the caret */

    int visible;
    int enabled;
    int focusable;
    int pickable;
    int has_focus;
    int event_token;

    int layout_dirty;
    int paint_dirty;

    gv_display_list draw_list;
    void *native;
    int native_kind;
    char *native_text;

    int cam_x, cam_y;
    double cam_scale;

    /* INTENT-4B-5A appearance: a rect's corner radius (px); a text's font
     * style (bit 0 bold; bits 1-2 family: 0 sans, 1 serif, 2 monospace); an
     * image node's picture: slot + 1 in the view's image cache (0: none; a
     * failed load leaves the reason in text, drawn as a placeholder) */
    int radius;
    int font_style;
    int image;
} gv_node;

/* INTENT-4B-5A: decoded pictures, shared by every node showing the same
 * path (one copy in memory).  Pixels are premultiplied ARGB32 in native
 * byte order, stride 4 * w (cairo's format). */
#define GV_VIEW_IMAGES 64
typedef struct gv_image_entry {
    char *path;
    uint8_t *pixels;
    int w, h;
    int refs;
} gv_image_entry;
/* path -> pixels (malloc'd; the view keeps them), or -1 and a reason in
 * err.  The host passes the rendering backend's decoder (gv_backend
 * decode_image); ctx is given back to it. */
typedef int (*gv_image_decoder)(void *ctx, const char *path, uint8_t **pixels, int *w, int *h, char *err, int errcap);

typedef struct gv_view {
    gv_node *nodes;
    int count;
    int cap;
    gv_node_id free_head;
    int live_count;

    gv_node_id root;
    gv_node_id focus;
    gv_node_id *tab;
    int tab_count;
    int tab_cap;
    gv_node_id tab_root;        /* GLON-INST-3B: the window root tab[] was built for */

    int width;
    int height;
    int layout_dirty;
    uint64_t frame;

    int nodes_created;
    int nodes_destroyed;
    int layouts;
    int hit_tests;
    int draw_ops;

    gv_image_entry images[GV_VIEW_IMAGES];
} gv_view;

typedef int (*gv_event_handler)(gv_view *v, gv_node_id id,
                                const gv_event *ev, int lx, int ly,
                                void *user);

gv_view *gv_view_new(int width, int height);
void gv_view_free(gv_view *v);

gv_node_id gv_view_root(gv_view *v);
gv_node_id gv_view_add(gv_view *v, gv_node_id parent, gv_node_type type);
/* GLON-INST-3B: an extra window root (a native window of its own); v->root
 * stays the first window's */
gv_node_id gv_view_add_window(gv_view *v);
/* GLON-INST-3B: the window root a node lies in (0: none); whether a window
 * root needs layout */
gv_node_id gv_view_window_of(gv_view *v, gv_node_id id);
int gv_view_window_dirty(gv_view *v, gv_node_id root);
int gv_view_window_changed(gv_view *v, gv_node_id root);
int gv_view_valid(gv_view *v, gv_node_id id);
gv_node *gv_view_node(gv_view *v, gv_node_id id);
int gv_view_set_parent(gv_view *v, gv_node_id id, gv_node_id parent);
void gv_view_destroy(gv_view *v, gv_node_id id);

void gv_view_set_rect(gv_view *v, gv_node_id id, int x, int y, int w, int h);
void gv_view_set_text(gv_view *v, gv_node_id id, const char *text);
void gv_view_set_text_align(gv_view *v, gv_node_id id, int align);
/* A text caret before byte index (clamped to the text), drawn by the backend
 * where it measures the text; index < 0 removes it. */
void gv_view_set_caret(gv_view *v, gv_node_id id, int index);
/* INTENT-4B-2: caret with a selection anchor (anchor < 0 or = index: none) */
void gv_view_set_selection(gv_view *v, gv_node_id id, int index, int anchor);
/* INTENT-4B-2: 1 = an editable text (clipped to its box, scrolled to the caret) */
void gv_view_set_edit(gv_view *v, gv_node_id id, int on);
void gv_view_set_colors(gv_view *v, gv_node_id id, gv_color bg, gv_color fg);
void gv_view_set_font(gv_view *v, gv_node_id id, double size);
void gv_view_set_layout(gv_view *v, gv_node_id id, int dir, int align,
                        int spacing, int padding);
void gv_view_set_fill(gv_view *v, gv_node_id id, int fill_x, int fill_y);
void gv_view_set_minmax(gv_view *v, gv_node_id id, int min_w, int min_h,
                        int max_w, int max_h);
void gv_view_set_transform(gv_view *v, gv_node_id id, gv_transform t);
void gv_view_set_flags(gv_view *v, gv_node_id id, int visible, int enabled,
                       int focusable, int pickable);
void gv_view_set_event(gv_view *v, gv_node_id id, int token);
void gv_view_set_viewport(gv_view *v, gv_node_id id, int x, int y,
                          double scale);
void gv_view_set_draw_list(gv_view *v, gv_node_id id,
                           const gv_display_list *dl);

/* INTENT-4B-5A appearance */
void gv_view_set_radius(gv_view *v, gv_node_id id, int radius);
void gv_view_set_font_style(gv_view *v, gv_node_id id, int style);
/* show the picture at path in an image node, scaled to fit its box with its
 * aspect ratio kept (decoded once per path, with dec; NULL: no decoder);
 * 0, or -1 with the reason in the node's text (drawn as a placeholder) and
 * in err */
int gv_view_set_image(gv_view *v, gv_node_id id, const char *path, gv_image_decoder dec, void *ctx,
                      char *err, int errcap);
/* the node's picture size (0 and w h), or -1 */
int gv_view_image_size(gv_view *v, gv_node_id id, int *w, int *h);
/* decoded pictures held and their bytes */
int gv_view_image_count(gv_view *v, size_t *bytes);

void gv_view_invalidate(gv_view *v, gv_node_id id, int layout);
void gv_view_layout(gv_view *v);
void gv_view_build_display_list(gv_view *v, gv_display_list *dl);

int gv_view_hit_test(gv_view *v, int x, int y, gv_node_id *id,
                     int *lx, int *ly);
int gv_view_local_point(gv_view *v, gv_node_id id, int x, int y,
                        int *lx, int *ly);
int gv_view_dispatch(gv_view *v, const gv_event *ev, gv_event_handler fn,
                     void *user);

int gv_view_focus(gv_view *v, gv_node_id id);
int gv_view_focus_next(gv_view *v, int backwards);
gv_node_id gv_view_focus_id(gv_view *v);
/* If the focused node is id or inside id's subtree, the focus is dropped
 * (no node has it) and the node that had it is answered; else 0.  Used when
 * a subtree is hidden: an invisible node must not keep keyboard focus. */
gv_node_id gv_view_drop_focus_within(gv_view *v, gv_node_id id);

void gv_view_resize(gv_view *v, int width, int height);

int gv_view_live_nodes(gv_view *v);
int gv_view_node_capacity(gv_view *v);
int gv_view_screen_rect(gv_view *v, gv_node_id id, gv_rect *out);
/* The part of the node's screen rectangle that is actually shown: its screen
 * rectangle clipped by every ancestor clip/viewport and by the window.
 * Answers 0 and a non-empty rectangle, or -1 when nothing of it is visible
 * (hidden, clipped out or scrolled out). */
int gv_view_visible_rect(gv_view *v, gv_node_id id, gv_rect *out);

int gv_view_stats_nodes_created(gv_view *v);
int gv_view_stats_nodes_destroyed(gv_view *v);
int gv_view_stats_layouts(gv_view *v);
int gv_view_stats_hit_tests(gv_view *v);
int gv_view_stats_draw_ops(gv_view *v);

#ifdef __cplusplus
}
#endif

#endif