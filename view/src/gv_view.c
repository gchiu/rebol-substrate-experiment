#include "gv_view.h"
#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static gv_transform mat_identity(void) {
    gv_transform t = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    return t;
}

static gv_transform mat_translate(double x, double y) {
    gv_transform t = {1.0, 0.0, 0.0, 1.0, x, y};
    return t;
}

static gv_transform mat_scale(double sx, double sy) {
    gv_transform t = {sx, 0.0, 0.0, sy, 0.0, 0.0};
    return t;
}

static gv_transform mat_mul(gv_transform m, gv_transform n) {
    gv_transform r;
    r.a = m.a * n.a + m.c * n.b;
    r.b = m.b * n.a + m.d * n.b;
    r.c = m.a * n.c + m.c * n.d;
    r.d = m.b * n.c + m.d * n.d;
    r.e = m.a * n.e + m.c * n.f + m.e;
    r.f = m.b * n.e + m.d * n.f + m.f;
    return r;
}

static void mat_apply(gv_transform m, double x, double y, double *ox,
                      double *oy) {
    *ox = m.a * x + m.c * y + m.e;
    *oy = m.b * x + m.d * y + m.f;
}

static int mat_invert(gv_transform m, gv_transform *out) {
    double det = m.a * m.d - m.b * m.c;
    if (det == 0.0) return 0;
    double id = 1.0 / det;
    out->a = m.d * id;
    out->b = -m.b * id;
    out->c = -m.c * id;
    out->d = m.a * id;
    out->e = (m.c * m.f - m.d * m.e) * id;
    out->f = (m.b * m.e - m.a * m.f) * id;
    return 1;
}

static gv_transform node_local_matrix(const gv_node *n) {
    gv_transform t = mat_translate((double)n->x, (double)n->y);
    return mat_mul(t, n->transform);
}

static int gv_node_visible(const gv_node *n) {
    return n && n->type != GV_NODE_FREE && n->visible;
}

static char *gv_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static int gv_text_width(const char *s, double size) {
    if (!s) return 0;
    int n = (int)strlen(s);
    return (int)((double)n * size * 0.55 + 0.5);
}

static int gv_text_height(double size) {
    return (int)(size * 1.2 + 0.5);
}

static gv_node *gv_get(gv_view *v, gv_node_id id) {
    if (!v || id <= 0 || id > v->count) return NULL;
    gv_node *n = &v->nodes[id - 1];
    return n->type == GV_NODE_FREE ? NULL : n;
}

gv_node *gv_view_node(gv_view *v, gv_node_id id) {
    return gv_get(v, id);
}

int gv_view_valid(gv_view *v, gv_node_id id) {
    return gv_get(v, id) != NULL;
}

static int gv_view_grow(gv_view *v) {
    if (v->count < v->cap) return 0;
    int cap = v->cap ? v->cap * 2 : 16;
    gv_node *p = (gv_node *)realloc(v->nodes, (size_t)cap * sizeof(gv_node));
    if (!p) return -1;
    memset(p + v->cap, 0, (size_t)(cap - v->cap) * sizeof(gv_node));
    v->nodes = p;
    v->cap = cap;
    return 0;
}

static void gv_mark_layout(gv_view *v, gv_node_id id) {
    for (gv_node_id cur = id; cur; ) {
        gv_node *n = gv_get(v, cur);
        if (!n) break;
        n->layout_dirty = 1;
        cur = n->parent;
    }
    v->layout_dirty = 1;
}

static void gv_mark_paint(gv_view *v, gv_node_id id) {
    for (gv_node_id cur = id; cur; ) {
        gv_node *n = gv_get(v, cur);
        if (!n) break;
        n->paint_dirty = 1;
        cur = n->parent;
    }
}

static void gv_detach(gv_view *v, gv_node_id id) {
    gv_node *n = gv_get(v, id);
    if (!n || !n->parent) return;
    gv_node *p = gv_get(v, n->parent);
    if (!p) {
        n->parent = 0;
        return;
    }
    if (p->first_child == id) p->first_child = n->next_sibling;
    if (p->last_child == id) p->last_child = n->prev_sibling;
    if (n->prev_sibling) gv_get(v, n->prev_sibling)->next_sibling = n->next_sibling;
    if (n->next_sibling) gv_get(v, n->next_sibling)->prev_sibling = n->prev_sibling;
    n->parent = 0;
    n->next_sibling = 0;
    n->prev_sibling = 0;
}

static void gv_append(gv_view *v, gv_node_id parent, gv_node_id id) {
    gv_node *p = gv_get(v, parent);
    gv_node *n = gv_get(v, id);
    if (!p || !n) return;
    n->parent = parent;
    n->prev_sibling = p->last_child;
    n->next_sibling = 0;
    if (p->last_child) gv_get(v, p->last_child)->next_sibling = id;
    else p->first_child = id;
    p->last_child = id;
}

gv_view *gv_view_new(int width, int height) {
    gv_view *v = (gv_view *)calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->width = width > 0 ? width : 640;
    v->height = height > 0 ? height : 480;
    v->layout_dirty = 1;
    gv_node_id root = gv_view_add(v, 0, GV_NODE_WINDOW);
    if (!root) {
        free(v);
        return NULL;
    }
    gv_node *r = gv_get(v, root);
    r->x = 0;
    r->y = 0;
    r->w = v->width;
    r->h = v->height;
    r->bg = (gv_color){24, 26, 30, 255};
    r->fg = (gv_color){235, 238, 242, 255};
    r->pickable = 0;
    return v;
}

void gv_view_free(gv_view *v) {
    if (!v) return;
    for (int i = 0; i < GV_VIEW_IMAGES; i++) { free(v->images[i].path); free(v->images[i].pixels); }
    for (int i = 0; i < v->count; i++) {
        gv_node *n = &v->nodes[i];
        if (n->type == GV_NODE_FREE) continue;
        free(n->text);
        free(n->native_text);
        gv_display_list_free(&n->draw_list);
    }
    free(v->nodes);
    free(v->tab);
    free(v);
}

gv_node_id gv_view_root(gv_view *v) {
    return v ? v->root : 0;
}

/* GLON-INST-3B: a second (third, ...) window root: a window node with no
 * parent that does not replace v->root.  The host makes it the view's root,
 * size and focus while it lays out, draws, hit-tests or types in that
 * window (gv_glon_host.c, window context). */
static gv_node_id gv_view_add_node(gv_view *v, gv_node_id parent, gv_node_type type, int extra_root);
gv_node_id gv_view_add_window(gv_view *v) { return v ? gv_view_add_node(v, 0, GV_NODE_WINDOW, 1) : 0; }
gv_node_id gv_view_add(gv_view *v, gv_node_id parent, gv_node_type type) { return gv_view_add_node(v, parent, type, 0); }
static gv_node_id gv_view_add_node(gv_view *v, gv_node_id parent, gv_node_type type, int extra_root) {
    if (!v || type == GV_NODE_FREE) return 0;
    if (parent) {
        gv_node *p = gv_get(v, parent);
        if (!p) return 0;
    } else if (type != GV_NODE_WINDOW || (v->root && !extra_root)) {
        return 0;
    }

    gv_node_id id = 0;
    if (v->free_head) {
        id = v->free_head;
        gv_node *f = &v->nodes[id - 1];
        v->free_head = f->free_next;
    } else {
        if (gv_view_grow(v) != 0) return 0;
        id = ++v->count;
    }
    gv_node *n = &v->nodes[id - 1];
    memset(n, 0, sizeof(*n));
    n->type = type;
    n->parent = parent;
    n->visible = 1;
    n->enabled = 1;
    n->pickable = type == GV_NODE_RECT || type == GV_NODE_TEXT ||
                  type == GV_NODE_IMAGE || type == GV_NODE_CUSTOM ||
                  type == GV_NODE_NATIVE_CONTROL;
    n->font_size = 12.0;
    n->align = GV_ALIGN_START;
    n->cam_scale = 1.0;
    n->transform = mat_identity();
    n->fg = (gv_color){235, 238, 242, 255};
    v->live_count++;
    v->nodes_created++;

    if (parent) {
        gv_append(v, parent, id);
    } else if (!extra_root) {
        v->root = id;
    }
    gv_mark_layout(v, id);
    return id;
}

/* INTENT-4B-5A: a node lets go of its picture; the last one frees it */
static void gv_image_release(gv_view *v, gv_node *n) {
    if (!n->image) return;
    gv_image_entry *e = &v->images[n->image - 1];
    n->image = 0;
    if (--e->refs > 0) return;
    free(e->path); free(e->pixels);
    memset(e, 0, sizeof *e);
}

static void gv_destroy_node(gv_view *v, gv_node_id id) {
    gv_node *n = gv_get(v, id);
    if (!n) return;
    gv_node_id child = n->first_child;
    while (child) {
        gv_node *c = gv_get(v, child);
        gv_node_id next = c ? c->next_sibling : 0;
        gv_destroy_node(v, child);
        child = next;
    }
    if (v->focus == id) v->focus = 0;
    gv_detach(v, id);
    gv_image_release(v, n);
    free(n->text);
    free(n->native_text);
    gv_display_list_free(&n->draw_list);
    memset(n, 0, sizeof(*n));
    n->type = GV_NODE_FREE;
    n->free_next = v->free_head;
    v->free_head = id;
    v->live_count--;
    v->nodes_destroyed++;
    if (v->root == id) v->root = 0;
    v->layout_dirty = 1;
}
/* GLON-INST-3B: the window root a node lies in (0: none) */
gv_node_id gv_view_window_of(gv_view *v, gv_node_id id) {
    gv_node_id last = 0;
    for (gv_node_id cur = id; cur; ) {
        gv_node *n = gv_get(v, cur);
        if (!n) return 0;
        last = cur;
        cur = n->parent;
    }
    gv_node *r = last ? gv_get(v, last) : NULL;
    return r && r->type == GV_NODE_WINDOW ? last : 0;
}
/* GLON-INST-3B: whether a window root needs layout (its flag: set by any
 * change inside it, cleared when it is laid out as v->root) */
int gv_view_window_dirty(gv_view *v, gv_node_id root) {
    gv_node *r = gv_get(v, root);
    return r ? r->layout_dirty : 0;
}

void gv_view_destroy(gv_view *v, gv_node_id id) {
    if (!gv_view_valid(v, id)) return;
    gv_node_id parent = gv_get(v, id)->parent;
    gv_destroy_node(v, id);
    if (parent && gv_get(v, parent)) gv_mark_layout(v, parent);   /* GLON-INST-3B: its window */
}

int gv_view_set_parent(gv_view *v, gv_node_id id, gv_node_id parent) {
    gv_node *n = gv_get(v, id);
    if (!n) return -1;
    if (parent) {
        gv_node *p = gv_get(v, parent);
        if (!p) return -1;
        for (gv_node_id cur = parent; cur; ) {
            if (cur == id) return -1;
            gv_node *q = gv_get(v, cur);
            if (!q) break;
            cur = q->parent;
        }
    }
    gv_detach(v, id);
    if (parent) gv_append(v, parent, id);
    gv_mark_layout(v, id);
    return 0;
}

void gv_view_set_rect(gv_view *v, gv_node_id id, int x, int y, int w, int h) {
    gv_node *n = gv_get(v, id);
    if (!n) return;
    n->x = x;
    n->y = y;
    n->base_w = w;
    n->base_h = h;
    n->w = w;
    n->h = h;
    gv_mark_layout(v, id);
}

void gv_view_set_text(gv_view *v, gv_node_id id, const char *text) {
    gv_node *n = gv_get(v, id);
    if (!n) return;
    free(n->text);
    n->text = text ? gv_strdup(text) : NULL;
    gv_mark_layout(v, id);
}

void gv_view_set_text_align(gv_view *v, gv_node_id id, int align) {
    gv_node *n = gv_get(v, id);
    if (!n) return;
    n->text_align = align;
    gv_mark_layout(v, id);
}

void gv_view_set_radius(gv_view *v, gv_node_id id, int radius) {
    gv_node *n = gv_get(v, id);
    if (!n) return;
    n->radius = radius < 0 ? 0 : radius;
    gv_mark_layout(v, id);
}
void gv_view_set_font_style(gv_view *v, gv_node_id id, int style) {
    gv_node *n = gv_get(v, id);
    if (!n) return;
    n->font_style = style & 7;
    gv_mark_layout(v, id);
}
int gv_view_set_image(gv_view *v, gv_node_id id, const char *path, gv_image_decoder gv_decoder, void *ctx,
                      char *err, int errcap) {
    gv_node *n = gv_get(v, id);
    char why[256] = "";
    if (err && errcap > 0) err[0] = 0;
    if (!n || !path) return -1;
    gv_image_release(v, n);
    int slot = -1, free_slot = -1;
    for (int i = 0; i < GV_VIEW_IMAGES; i++) {
        if (v->images[i].path && !strcmp(v->images[i].path, path)) { slot = i; break; }
        if (!v->images[i].path && free_slot < 0) free_slot = i;
    }
    if (slot < 0) {
        uint8_t *px = NULL; int w = 0, h = 0;
        if (!gv_decoder) snprintf(why, sizeof why, "no image decoder in this backend");
        else if (free_slot < 0) snprintf(why, sizeof why, "too many different images (%d)", GV_VIEW_IMAGES);
        else if (gv_decoder(ctx, path, &px, &w, &h, why, (int)sizeof why) == 0 && px && w > 0 && h > 0) {
            gv_image_entry *e = &v->images[free_slot];
            e->path = gv_strdup(path); e->pixels = px; e->w = w; e->h = h; e->refs = 0;
            slot = free_slot;
        } else { free(px); if (!why[0]) snprintf(why, sizeof why, "cannot read the image"); }
    }
    if (slot < 0) {
        /* the placeholder says why */
        char msg[400];
        const char *base = strrchr(path, '/');
        /* two lines in the placeholder: the reason, then the file */
        snprintf(msg, sizeof msg, "%s\n%s", why, base ? base + 1 : path);
        free(n->text);
        n->text = gv_strdup(msg);
        if (err && errcap > 0) snprintf(err, (size_t)errcap, "%s: %s", why, base ? base + 1 : path);
        gv_mark_layout(v, id);
        return -1;
    }
    v->images[slot].refs++;
    n->image = slot + 1;
    free(n->text);
    n->text = NULL;
    gv_mark_layout(v, id);
    return 0;
}
int gv_view_image_size(gv_view *v, gv_node_id id, int *w, int *h) {
    gv_node *n = gv_get(v, id);
    if (!n || !n->image) return -1;
    if (w) *w = v->images[n->image - 1].w;
    if (h) *h = v->images[n->image - 1].h;
    return 0;
}
int gv_view_image_count(gv_view *v, size_t *bytes) {
    int k = 0; size_t b = 0;
    for (int i = 0; v && i < GV_VIEW_IMAGES; i++)
        if (v->images[i].path) { k++; b += (size_t)v->images[i].w * (size_t)v->images[i].h * 4; }
    if (bytes) *bytes = b;
    return k;
}

void gv_view_set_caret(gv_view *v, gv_node_id id, int index) {
    gv_node *n = gv_get(v, id);
    if (!n) return;
    n->caret = index < 0 ? 0 : index + 1;
    n->sel_anchor = 0;
    gv_mark_paint(v, id);
}

void gv_view_set_selection(gv_view *v, gv_node_id id, int index, int anchor) {
    gv_node *n = gv_get(v, id);
    if (!n) return;
    n->caret = index < 0 ? 0 : index + 1;
    n->sel_anchor = (index < 0 || anchor < 0 || anchor == index) ? 0 : anchor + 1;
    gv_mark_paint(v, id);
}

void gv_view_set_edit(gv_view *v, gv_node_id id, int on) {
    gv_node *n = gv_get(v, id);
    if (!n) return;
    n->edit = on ? 1 : 0;
    gv_mark_paint(v, id);
}

void gv_view_set_colors(gv_view *v, gv_node_id id, gv_color bg, gv_color fg) {
    gv_node *n = gv_get(v, id);
    if (!n) return;
    n->bg = bg;
    n->fg = fg;
    gv_mark_paint(v, id);
}

void gv_view_set_font(gv_view *v, gv_node_id id, double size) {
    gv_node *n = gv_get(v, id);
    if (!n) return;
    n->font_size = size;
    gv_mark_layout(v, id);
}

void gv_view_set_layout(gv_view *v, gv_node_id id, int dir, int align,
                        int spacing, int padding) {
    gv_node *n = gv_get(v, id);
    if (!n) return;
    n->layout_dir = dir;
    n->align = align;
    n->spacing = spacing;
    n->padding = padding;
    gv_mark_layout(v, id);
}

void gv_view_set_fill(gv_view *v, gv_node_id id, int fill_x, int fill_y) {
    gv_node *n = gv_get(v, id);
    if (!n) return;
    n->fill_x = fill_x;
    n->fill_y = fill_y;
    gv_mark_layout(v, id);
}

void gv_view_set_minmax(gv_view *v, gv_node_id id, int min_w, int min_h,
                        int max_w, int max_h) {
    gv_node *n = gv_get(v, id);
    if (!n) return;
    n->min_w = min_w;
    n->min_h = min_h;
    n->max_w = max_w;
    n->max_h = max_h;
    gv_mark_layout(v, id);
}

void gv_view_set_transform(gv_view *v, gv_node_id id, gv_transform t) {
    gv_node *n = gv_get(v, id);
    if (!n) return;
    n->transform = t;
    gv_mark_layout(v, id);
}

void gv_view_set_flags(gv_view *v, gv_node_id id, int visible, int enabled,
                       int focusable, int pickable) {
    gv_node *n = gv_get(v, id);
    if (!n) return;
    n->visible = visible;
    n->enabled = enabled;
    n->focusable = focusable;
    n->pickable = pickable;
    if (focusable && !visible && v->focus == id) v->focus = 0;
    gv_mark_layout(v, id);
}

void gv_view_set_event(gv_view *v, gv_node_id id, int token) {
    gv_node *n = gv_get(v, id);
    if (!n) return;
    n->event_token = token;
    gv_mark_paint(v, id);
}

void gv_view_set_viewport(gv_view *v, gv_node_id id, int x, int y,
                          double scale) {
    gv_node *n = gv_get(v, id);
    if (!n) return;
    n->cam_x = x;
    n->cam_y = y;
    n->cam_scale = scale != 0.0 ? scale : 1.0;
    gv_mark_layout(v, id);
}

void gv_view_set_draw_list(gv_view *v, gv_node_id id,
                           const gv_display_list *dl) {
    gv_node *n = gv_get(v, id);
    if (!n) return;
    gv_display_list_free(&n->draw_list);
    gv_display_list_init(&n->draw_list);
    if (dl) {
        for (int i = 0; i < dl->count; i++) {
            gv_display_list_add(&n->draw_list, &dl->cmds[i]);
        }
    }
    gv_mark_paint(v, id);
}

void gv_view_invalidate(gv_view *v, gv_node_id id, int layout) {
    if (!v) return;
    if (layout) gv_mark_layout(v, id);
    else gv_mark_paint(v, id);
}

static void gv_measure(gv_view *v, gv_node_id id, int *ow, int *oh) {
    gv_node *n = gv_get(v, id);
    *ow = 0;
    *oh = 0;
    if (!n) return;
    int w = n->base_w;
    int h = n->base_h;

    switch (n->type) {
    case GV_NODE_TEXT:
        if (w <= 0) w = gv_text_width(n->text, n->font_size);
        if (h <= 0) h = gv_text_height(n->font_size);
        break;
    case GV_NODE_GROUP: {
        int content_w = 0;
        int content_h = 0;
        int visible = 0;
        int max_child_h = 0;
        int max_child_w = 0;
        for (gv_node_id c = n->first_child; c; c = gv_get(v, c)->next_sibling) {
            gv_node *cn = gv_get(v, c);
            if (!gv_node_visible(cn)) continue;
            int cw, ch;
            gv_measure(v, c, &cw, &ch);
            if (n->layout_dir == GV_LAYOUT_H) {
                content_w += cw;
                if (max_child_h < ch) max_child_h = ch;
            } else if (n->layout_dir == GV_LAYOUT_V) {
                content_h += ch;
                if (max_child_w < cw) max_child_w = cw;
            } else {
                if (cn->x + cw > content_w) content_w = cn->x + cw;
                if (cn->y + ch > content_h) content_h = cn->y + ch;
            }
            visible++;
        }
        if (n->layout_dir == GV_LAYOUT_H) {
            content_w += (visible > 0 ? n->spacing * (visible - 1) : 0);
            content_w += 2 * n->padding;
            content_h = max_child_h + 2 * n->padding;
        } else if (n->layout_dir == GV_LAYOUT_V) {
            content_h += (visible > 0 ? n->spacing * (visible - 1) : 0);
            content_h += 2 * n->padding;
            content_w = max_child_w + 2 * n->padding;
        } else {
            content_w += n->padding;
            content_h += n->padding;
        }
        if (w <= 0) w = content_w;
        if (h <= 0) h = content_h;
        break;
    }
    case GV_NODE_TRANSFORM:
    case GV_NODE_CLIP:
    case GV_NODE_VIEWPORT: {
        int content_w = 0;
        int content_h = 0;
        for (gv_node_id c = n->first_child; c; c = gv_get(v, c)->next_sibling) {
            gv_node *cn = gv_get(v, c);
            if (!gv_node_visible(cn)) continue;
            int cw, ch;
            gv_measure(v, c, &cw, &ch);
            if (cn->x + cw > content_w) content_w = cn->x + cw;
            if (cn->y + ch > content_h) content_h = cn->y + ch;
        }
        if (w <= 0) w = content_w;
        if (h <= 0) h = content_h;
        break;
    }
    default:
        break;
    }

    if (n->min_w > 0 && w < n->min_w) w = n->min_w;
    if (n->min_h > 0 && h < n->min_h) h = n->min_h;
    if (n->max_w > 0 && w > n->max_w) w = n->max_w;
    if (n->max_h > 0 && h > n->max_h) h = n->max_h;
    *ow = w > 0 ? w : 0;
    *oh = h > 0 ? h : 0;
}

static int gv_align_offset(int align, int content, int size) {
    switch (align) {
    case GV_ALIGN_CENTER:
        return (content - size) / 2;
    case GV_ALIGN_END:
        return content - size;
    case GV_ALIGN_STRETCH:
        return 0;
    default:
        return 0;
    }
}

static void gv_layout_node(gv_view *v, gv_node_id id, int avail_w,
                           int avail_h) {
    gv_node *n = gv_get(v, id);
    if (!n) return;

    if (n->type == GV_NODE_WINDOW) {
        n->x = 0;
        n->y = 0;
        if (n->w <= 0) n->w = v->width;
        if (n->h <= 0) n->h = v->height;
        avail_w = n->w;
        avail_h = n->h;
    } else {
        n->w = avail_w;
        n->h = avail_h;
    }

    if (n->type == GV_NODE_TEXT || n->type == GV_NODE_RECT ||
        n->type == GV_NODE_IMAGE || n->type == GV_NODE_CUSTOM ||
        n->type == GV_NODE_NATIVE_CONTROL) {
        if (n->w <= 0 || n->h <= 0) {
            int mw, mh;
            gv_measure(v, id, &mw, &mh);
            if (n->w <= 0) n->w = mw;
            if (n->h <= 0) n->h = mh;
        }
        return;
    }

    if (n->layout_dir == GV_LAYOUT_NONE) {
        for (gv_node_id c = n->first_child; c; c = gv_get(v, c)->next_sibling) {
            gv_node *cn = gv_get(v, c);
            if (!gv_node_visible(cn)) continue;
            int cw, ch;
            gv_measure(v, c, &cw, &ch);
            if (cn->w <= 0) cn->w = cw;
            if (cn->h <= 0) cn->h = ch;
            gv_layout_node(v, c, cn->w, cn->h);
        }
        return;
    }

    int content_w = avail_w - 2 * n->padding;
    int content_h = avail_h - 2 * n->padding;
    if (content_w < 0) content_w = 0;
    if (content_h < 0) content_h = 0;

    int count = 0;
    int fill_count = 0;
    int fixed_main = 0;
    int spacing_total = 0;
    for (gv_node_id c = n->first_child; c; c = gv_get(v, c)->next_sibling) {
        gv_node *cn = gv_get(v, c);
        if (!gv_node_visible(cn)) continue;
        int cw, ch;
        gv_measure(v, c, &cw, &ch);
        if (n->layout_dir == GV_LAYOUT_H) {
            fixed_main += cw;
            if (cn->fill_x) fill_count++;
        } else {
            fixed_main += ch;
            if (cn->fill_y) fill_count++;
        }
        count++;
    }
    if (count > 1) spacing_total = n->spacing * (count - 1);

    int remaining = (n->layout_dir == GV_LAYOUT_H ? content_w : content_h) -
                    fixed_main - spacing_total;
    if (remaining < 0) remaining = 0;
    int fill_share = fill_count > 0 ? remaining / fill_count : 0;
    int fill_extra = fill_count > 0 ? remaining % fill_count : 0;

    int cursor = n->padding;
    int index = 0;
    for (gv_node_id c = n->first_child; c; c = gv_get(v, c)->next_sibling) {
        gv_node *cn = gv_get(v, c);
        if (!gv_node_visible(cn)) continue;
        int cw, ch;
        gv_measure(v, c, &cw, &ch);
        int main = n->layout_dir == GV_LAYOUT_H ? cw : ch;
        int cross = n->layout_dir == GV_LAYOUT_H ? ch : cw;
        int is_fill = n->layout_dir == GV_LAYOUT_H ? cn->fill_x : cn->fill_y;
        if (is_fill) {
            main = fill_share + (fill_extra > 0 ? 1 : 0);
            if (fill_extra > 0) fill_extra--;
        }

        int cross_fill = n->layout_dir == GV_LAYOUT_H ? cn->fill_y : cn->fill_x;
        int content_cross = n->layout_dir == GV_LAYOUT_H ? content_h : content_w;
        if (cross_fill || cn->align == GV_ALIGN_STRETCH) {
            cross = content_cross;
        } else if (cross > content_cross) {
            cross = content_cross;
        }

        int main_pos = cursor;
        int cross_pos = n->padding;
        if (n->layout_dir == GV_LAYOUT_H) {
            cross_pos += gv_align_offset(n->align, content_h, cross);
            cn->x = main_pos;
            cn->y = cross_pos;
            cn->w = main;
            cn->h = cross;
        } else {
            cross_pos += gv_align_offset(n->align, content_w, cross);
            cn->x = cross_pos;
            cn->y = main_pos;
            cn->w = cross;
            cn->h = main;
        }

        gv_layout_node(v, c, cn->w, cn->h);
        cursor += main + n->spacing;
        index++;
    }
}

static void gv_build_tab(gv_view *v, gv_node_id id) {
    gv_node *n = gv_get(v, id);
    if (!n || !gv_node_visible(n)) return;
    if (n->focusable && n->enabled) {
        if (v->tab_count == v->tab_cap) {
            int cap = v->tab_cap ? v->tab_cap * 2 : 16;
            gv_node_id *p = (gv_node_id *)realloc(v->tab,
                                                  (size_t)cap * sizeof(gv_node_id));
            if (!p) return;
            v->tab = p;
            v->tab_cap = cap;
        }
        v->tab[v->tab_count++] = id;
    }
    for (gv_node_id c = n->first_child; c; c = gv_get(v, c)->next_sibling) {
        gv_build_tab(v, c);
    }
}

void gv_view_layout(gv_view *v) {
    if (!v || !v->root) return;
    gv_layout_node(v, v->root, v->width, v->height);
    v->tab_count = 0;
    gv_build_tab(v, v->root);
    v->tab_root = v->root;
    v->layout_dirty = 0;
    gv_node *rn = gv_get(v, v->root);
    if (rn) rn->layout_dirty = 0;
    v->layouts++;
}

/* Children carry absolute matrices, so once a clip rectangle is set in the
 * node's own space the current transform is undone again (the clip stays in
 * force); otherwise a composing backend would apply the node's offset twice. */
static void gv_emit_untransform(gv_transform m, gv_display_list *dl) {
    gv_transform inv;
    if (!mat_invert(m, &inv)) return;
    gv_cmd c;
    memset(&c, 0, sizeof c);
    c.op = GV_DRAW_TRANSFORM;
    c.transform = inv;
    gv_display_list_add(dl, &c);
}

/* INTENT-4B-5A: an image node: its picture scaled to fit the box, aspect
 * ratio kept, centred; without one, a placeholder (an outline and the reason,
 * or "?" in a small box) in the node's foreground colour */
static void gv_emit_image(gv_view *v, gv_node *n, gv_transform m, gv_display_list *dl) {
    gv_cmd c;
    memset(&c, 0, sizeof c); c.op = GV_DRAW_PUSH; gv_display_list_add(dl, &c);
    memset(&c, 0, sizeof c); c.op = GV_DRAW_TRANSFORM; c.transform = m; gv_display_list_add(dl, &c);
    memset(&c, 0, sizeof c); c.op = GV_DRAW_CLIP; c.rect.w = n->w; c.rect.h = n->h; gv_display_list_add(dl, &c);
    if (n->image) {
        gv_image_entry *e = &v->images[n->image - 1];
        double s = (double)n->w / e->w, sy = (double)n->h / e->h;
        if (sy < s) s = sy;
        int dw = (int)(e->w * s + 0.5), dh = (int)(e->h * s + 0.5);
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_IMAGE;
        c.rect.x = (n->w - dw) / 2; c.rect.y = (n->h - dh) / 2; c.rect.w = dw; c.rect.h = dh;
        c.pixels = e->pixels; c.image_w = e->w; c.image_h = e->h; c.format = 1;
        gv_display_list_add(dl, &c);
    } else if (n->text && n->w > 0 && n->h > 0) {
        int edges[4][4] = { {0, 0, n->w - 1, 0}, {0, n->h - 1, n->w - 1, 0}, {0, 0, 0, n->h - 1}, {n->w - 1, 0, 0, n->h - 1} };
        for (int k = 0; k < 4; k++) {
            memset(&c, 0, sizeof c); c.op = GV_DRAW_LINE;
            c.rect.x = edges[k][0]; c.rect.y = edges[k][1]; c.rect.w = edges[k][2]; c.rect.h = edges[k][3];
            c.color = n->fg; c.f1 = 1; gv_display_list_add(dl, &c);
        }
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_TEXT;
        const char *t = n->w < 80 ? "?" : n->text;
        double size = n->font_size > 0 ? n->font_size : 12;
        int lines = n->w < 80 ? 1 : 2;
        c.rect.x = 4; c.rect.y = (int)((n->h - lines * (size + 6)) / 2); c.rect.w = n->w - 8;
        if (c.rect.y < 2) c.rect.y = 2;
        if (n->w < 80) { c.rect.x = 0; c.rect.w = n->w; c.f2 = GV_ALIGN_CENTER; }
        c.text = t; c.text_len = (uint32_t)strlen(t); c.f1 = size; c.color = n->fg;
        gv_display_list_add(dl, &c);
    }
    memset(&c, 0, sizeof c); c.op = GV_DRAW_POP; gv_display_list_add(dl, &c);
}

static void gv_emit_node(gv_view *v, gv_node_id id, gv_transform parent,
                         gv_display_list *dl) {
    gv_node *n = gv_get(v, id);
    if (!gv_node_visible(n)) return;

    gv_transform m = mat_mul(parent, node_local_matrix(n));
    gv_transform child_m = m;
    if (n->type == GV_NODE_VIEWPORT) {
        child_m = mat_mul(m, mat_scale(n->cam_scale, n->cam_scale));
        child_m = mat_mul(child_m, mat_translate(-(double)n->cam_x,
                                                 -(double)n->cam_y));
    }

    gv_cmd c;
    switch (n->type) {
    case GV_NODE_RECT:
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_PUSH;
        gv_display_list_add(dl, &c);
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_TRANSFORM;
        c.transform = m;
        gv_display_list_add(dl, &c);
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_RECT;
        c.rect.x = 0;
        c.rect.y = 0;
        c.rect.w = n->w;
        c.rect.h = n->h;
        c.color = n->bg;
        c.f1 = n->radius;
        gv_display_list_add(dl, &c);
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_POP;
        gv_display_list_add(dl, &c);
        break;
    case GV_NODE_IMAGE:
        gv_emit_image(v, n, m, dl);
        break;
    case GV_NODE_TEXT:
        if (!n->text) break;
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_PUSH;
        gv_display_list_add(dl, &c);
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_TRANSFORM;
        c.transform = m;
        gv_display_list_add(dl, &c);
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_TEXT;
        c.rect.x = 0;
        c.rect.y = 0;
        if (n->text_align == GV_ALIGN_CENTER && n->w > 0) {
            int tw = gv_text_width(n->text, n->font_size);
            if (tw < n->w) c.rect.x = (n->w - tw) / 2;
        } else if (n->text_align == GV_ALIGN_END && n->w > 0) {
            int tw = gv_text_width(n->text, n->font_size);
            if (tw < n->w) c.rect.x = n->w - tw;
        }
        if ((n->text_align == GV_ALIGN_CENTER || n->text_align == GV_ALIGN_END) && n->w > 0) {
            /* box width and alignment, so a backend that can measure shaped
             * text places it exactly; rect.x above is the estimate fallback */
            c.rect.w = n->w;
            c.f2 = (double)n->text_align;
        }
        c.text = n->text;
        c.text_len = (uint32_t)strlen(n->text);
        c.f1 = n->font_size;
        c.f3 = (double)n->caret;        /* caret byte index + 1, 0 = none */
        c.f5 = (double)n->sel_anchor;   /* selection anchor + 1, 0 = none */
        c.font_style = n->font_style;
        if (n->edit) {                  /* clipped to the box, scrolled to the caret */
            c.f6 = 1.0;
            c.rect.w = n->w;
            c.rect.h = n->h;
        }
        c.color = n->fg;
        gv_display_list_add(dl, &c);
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_POP;
        gv_display_list_add(dl, &c);
        break;
    case GV_NODE_CUSTOM:
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_PUSH;
        gv_display_list_add(dl, &c);
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_TRANSFORM;
        c.transform = m;
        gv_display_list_add(dl, &c);
        for (int i = 0; i < n->draw_list.count; i++) {
            gv_display_list_add(dl, &n->draw_list.cmds[i]);
        }
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_POP;
        gv_display_list_add(dl, &c);
        break;
    case GV_NODE_CLIP:
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_PUSH;
        gv_display_list_add(dl, &c);
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_TRANSFORM;
        c.transform = m;
        gv_display_list_add(dl, &c);
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_CLIP;
        c.rect.x = 0;
        c.rect.y = 0;
        c.rect.w = n->w;
        c.rect.h = n->h;
        gv_display_list_add(dl, &c);
        gv_emit_untransform(m, dl);
        for (gv_node_id ch = n->first_child; ch; ch = gv_get(v, ch)->next_sibling) {
            gv_emit_node(v, ch, m, dl);
        }
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_POP;
        gv_display_list_add(dl, &c);
        break;
    case GV_NODE_VIEWPORT:
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_PUSH;
        gv_display_list_add(dl, &c);
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_TRANSFORM;
        c.transform = m;
        gv_display_list_add(dl, &c);
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_CLIP;
        c.rect.x = 0;
        c.rect.y = 0;
        c.rect.w = n->w;
        c.rect.h = n->h;
        gv_display_list_add(dl, &c);
        gv_emit_untransform(m, dl);
        for (gv_node_id ch = n->first_child; ch; ch = gv_get(v, ch)->next_sibling) {
            gv_emit_node(v, ch, child_m, dl);
        }
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_POP;
        gv_display_list_add(dl, &c);
        break;
    case GV_NODE_NATIVE_CONTROL:
        memset(&c, 0, sizeof c);
        c.op = GV_DRAW_RECT;
        c.rect.x = 0;
        c.rect.y = 0;
        c.rect.w = n->w;
        c.rect.h = n->h;
        c.color = (gv_color){70, 130, 180, 255};
        gv_display_list_add(dl, &c);
        break;
    case GV_NODE_GROUP:
    case GV_NODE_TRANSFORM:
    case GV_NODE_WINDOW:
        if (n->bg.a > 0 && n->w > 0 && n->h > 0) {
            memset(&c, 0, sizeof c);
            c.op = GV_DRAW_PUSH;
            gv_display_list_add(dl, &c);
            memset(&c, 0, sizeof c);
            c.op = GV_DRAW_TRANSFORM;
            c.transform = m;
            gv_display_list_add(dl, &c);
            memset(&c, 0, sizeof c);
            c.op = GV_DRAW_RECT;
            c.rect.x = 0;
            c.rect.y = 0;
            c.rect.w = n->w;
            c.rect.h = n->h;
            c.color = n->bg;
            gv_display_list_add(dl, &c);
            memset(&c, 0, sizeof c);
            c.op = GV_DRAW_POP;
            gv_display_list_add(dl, &c);
        }
        if (n->draw_list.count > 0) {
            memset(&c, 0, sizeof c);
            c.op = GV_DRAW_PUSH;
            gv_display_list_add(dl, &c);
            memset(&c, 0, sizeof c);
            c.op = GV_DRAW_TRANSFORM;
            c.transform = m;
            gv_display_list_add(dl, &c);
            for (int i = 0; i < n->draw_list.count; i++) {
                gv_display_list_add(dl, &n->draw_list.cmds[i]);
            }
            memset(&c, 0, sizeof c);
            c.op = GV_DRAW_POP;
            gv_display_list_add(dl, &c);
        }
        /* (INTENT-4B-5A: a group with a radius is a rounded widget that draws
         * its own rounded focus ring; a square outline would show at its corners) */
        if (n->has_focus && n->w > 1 && n->h > 1 && n->radius == 0) {
            memset(&c, 0, sizeof c);
            c.op = GV_DRAW_PUSH;
            gv_display_list_add(dl, &c);
            memset(&c, 0, sizeof c);
            c.op = GV_DRAW_TRANSFORM;
            c.transform = m;
            gv_display_list_add(dl, &c);
            int edges[4][4] = {{0, 0, n->w, 1}, {0, n->h - 1, n->w, 1},
                               {0, 0, 1, n->h}, {n->w - 1, 0, 1, n->h}};
            for (int i = 0; i < 4; i++) {
                memset(&c, 0, sizeof c);
                c.op = GV_DRAW_RECT;
                c.rect.x = edges[i][0];
                c.rect.y = edges[i][1];
                c.rect.w = edges[i][2];
                c.rect.h = edges[i][3];
                c.color = n->fg;
                gv_display_list_add(dl, &c);
            }
            memset(&c, 0, sizeof c);
            c.op = GV_DRAW_POP;
            gv_display_list_add(dl, &c);
        }
        for (gv_node_id ch = n->first_child; ch; ch = gv_get(v, ch)->next_sibling) {
            gv_emit_node(v, ch, m, dl);
        }
        break;
    default:
        break;
    }
}

void gv_view_build_display_list(gv_view *v, gv_display_list *dl) {
    if (!v || !dl || !v->root) return;
    gv_emit_node(v, v->root, mat_identity(), dl);
    v->draw_ops += dl->count;
    gv_node *rn = gv_get(v, v->root);          /* GLON-INST-3B: this window is drawn */
    if (rn) rn->paint_dirty = 0;
}
/* GLON-INST-3B: whether a window root has changed since it was last drawn
 * (laid out or painted) */
int gv_view_window_changed(gv_view *v, gv_node_id root) {
    gv_node *r = gv_get(v, root);
    return r ? (r->layout_dirty || r->paint_dirty) : 0;
}

static int gv_hit_node(gv_view *v, gv_node_id id, gv_transform parent,
                       double x, double y, gv_node_id *out, double *ox,
                       double *oy) {
    gv_node *n = gv_get(v, id);
    if (!gv_node_visible(n)) return 0;

    gv_transform base = mat_mul(parent, node_local_matrix(n));
    gv_transform child_m = base;
    if (n->type == GV_NODE_VIEWPORT) {
        child_m = mat_mul(base, mat_scale(n->cam_scale, n->cam_scale));
        child_m = mat_mul(child_m, mat_translate(-(double)n->cam_x,
                                                 -(double)n->cam_y));
    }
    gv_transform inv;
    if (!mat_invert(base, &inv)) return 0;
    double px, py;
    mat_apply(inv, x, y, &px, &py);

    if (n->type == GV_NODE_CLIP || n->type == GV_NODE_VIEWPORT) {
        if (px < 0 || py < 0 || px >= (double)n->w || py >= (double)n->h) {
            return 0;
        }
    }

    for (gv_node_id c = n->last_child; c; c = gv_get(v, c)->prev_sibling) {
        gv_node *cn = gv_get(v, c);
        if (!cn || !gv_node_visible(cn)) continue;
        if (gv_hit_node(v, c, child_m, x, y, out, ox, oy)) return 1;
    }

    int inside = px >= 0 && py >= 0 && px < (double)n->w && py < (double)n->h;
    if (inside && n->pickable) {
        *out = id;
        *ox = px;
        *oy = py;
        return 1;
    }
    return 0;
}

int gv_view_hit_test(gv_view *v, int x, int y, gv_node_id *id, int *lx,
                     int *ly) {
    if (!v || !v->root || !id) return 0;
    v->hit_tests++;
    double px = 0.0, py = 0.0;
    gv_node_id found = 0;
    if (!gv_hit_node(v, v->root, mat_identity(), (double)x, (double)y,
                     &found, &px, &py)) {
        *id = 0;
        return 0;
    }
    *id = found;
    if (lx) *lx = (int)(px + 0.5);
    if (ly) *ly = (int)(py + 0.5);
    return 1;
}

static gv_transform gv_node_matrix(gv_view *v, gv_node_id id) {
    gv_transform m = mat_identity();
    gv_node_id chain[64];
    int n = 0;
    for (gv_node_id cur = id; cur && n < 64;) {
        gv_node *node = gv_get(v, cur);
        if (!node) break;
        chain[n++] = cur;
        cur = node->parent;
    }
    for (int i = n - 1; i >= 0; i--) {
        gv_node *node = gv_get(v, chain[i]);
        if (!node) continue;
        m = mat_mul(m, node_local_matrix(node));
        if (node->type == GV_NODE_VIEWPORT && chain[i] != id) {
            m = mat_mul(m, mat_scale(node->cam_scale, node->cam_scale));
            m = mat_mul(m, mat_translate(-(double)node->cam_x,
                                         -(double)node->cam_y));
        }
    }
    return m;
}

int gv_view_local_point(gv_view *v, gv_node_id id, int x, int y, int *lx,
                        int *ly) {
    gv_node *n = gv_get(v, id);
    if (!n || !lx || !ly) return -1;
    gv_transform inv;
    if (!mat_invert(gv_node_matrix(v, id), &inv)) return -1;
    double px, py;
    mat_apply(inv, (double)x, (double)y, &px, &py);
    *lx = (int)(px + 0.5);
    *ly = (int)(py + 0.5);
    return 0;
}

int gv_view_dispatch(gv_view *v, const gv_event *ev, gv_event_handler fn,
                     void *user) {
    if (!v || !ev || !fn) return 0;
    gv_node_id target = 0;
    if (ev->type == GV_EVENT_POINTER_MOVE || ev->type == GV_EVENT_POINTER_DOWN ||
        ev->type == GV_EVENT_POINTER_UP) {
        int lx = 0, ly = 0;
        if (!gv_view_hit_test(v, ev->x, ev->y, &target, &lx, &ly)) return 0;
    } else if (ev->type == GV_EVENT_KEY_DOWN || ev->type == GV_EVENT_KEY_UP ||
               ev->type == GV_EVENT_TEXT) {
        target = v->focus;
        if (!target) return 0;
    } else {
        target = v->focus;
        if (!target) return 0;
    }

    for (gv_node_id cur = target; cur;) {
        gv_node *n = gv_get(v, cur);
        if (!n) break;
        int lx = 0, ly = 0;
        gv_view_local_point(v, cur, ev->x, ev->y, &lx, &ly);
        if (fn(v, cur, ev, lx, ly, user) == 0) return 1;
        cur = n->parent;
    }
    return 1;
}

int gv_view_focus(gv_view *v, gv_node_id id) {
    gv_node *n = gv_get(v, id);
    if (!n || !n->focusable || !n->enabled || !n->visible) return -1;
    if (v->focus && v->focus != id) {
        gv_node *old = gv_get(v, v->focus);
        if (old) old->has_focus = 0;
    }
    n->has_focus = 1;
    v->focus = id;
    gv_mark_paint(v, id);
    return 0;
}

gv_node_id gv_view_focus_id(gv_view *v) {
    return v ? v->focus : 0;
}

gv_node_id gv_view_drop_focus_within(gv_view *v, gv_node_id id) {
    if (!v || !v->focus || !id) return 0;
    for (gv_node_id c = v->focus; c;) {
        gv_node *n = gv_get(v, c);
        if (!n) return 0;
        if (c == id) {
            gv_node_id had = v->focus;
            gv_node *f = gv_get(v, had);
            if (f) { f->has_focus = 0; gv_mark_paint(v, had); }
            v->focus = 0;
            return had;
        }
        c = n->parent;
    }
    return 0;
}

int gv_view_focus_next(gv_view *v, int backwards) {
    if (!v) return 0;
    /* GLON-INST-3B: the Tab order is that of the window laid out last; in
     * another window's context it is built again for this one */
    if (v->tab_root != v->root) { v->tab_count = 0; gv_build_tab(v, v->root); v->tab_root = v->root; }
    if (v->tab_count == 0) return 0;
    int index = -1;
    for (int i = 0; i < v->tab_count; i++) {
        if (v->tab[i] == v->focus) {
            index = i;
            break;
        }
    }
    if (index < 0) index = backwards ? 0 : -1;
    int next = backwards ? (index - 1 + v->tab_count) % v->tab_count
                         : (index + 1) % v->tab_count;
    return gv_view_focus(v, v->tab[next]) == 0 ? v->tab[next] : 0;
}

void gv_view_resize(gv_view *v, int width, int height) {
    if (!v || width <= 0 || height <= 0) return;
    v->width = width;
    v->height = height;
    if (v->root) {
        gv_node *r = gv_get(v, v->root);
        if (r) {
            r->base_w = width;
            r->base_h = height;
            r->w = width;
            r->h = height;
        }
        if (r) r->layout_dirty = 1;
    }
    v->layout_dirty = 1;
}

int gv_view_live_nodes(gv_view *v) {
    return v ? v->live_count : 0;
}

int gv_view_node_capacity(gv_view *v) {
    return v ? v->count : 0;
}

int gv_view_screen_rect(gv_view *v, gv_node_id id, gv_rect *out) {
    gv_node *n = gv_get(v, id);
    if (!n || !out) return -1;
    gv_transform m = gv_node_matrix(v, id);
    double xs[4], ys[4];
    int k = 0;
    double x0 = 0, y0 = 0, x1 = n->w, y1 = n->h;
    mat_apply(m, x0, y0, &xs[k], &ys[k]); k++;
    mat_apply(m, x1, y0, &xs[k], &ys[k]); k++;
    mat_apply(m, x1, y1, &xs[k], &ys[k]); k++;
    mat_apply(m, x0, y1, &xs[k], &ys[k]);
    double minx = xs[0], maxx = xs[0], miny = ys[0], maxy = ys[0];
    for (int i = 1; i < 4; i++) {
        if (xs[i] < minx) minx = xs[i];
        if (xs[i] > maxx) maxx = xs[i];
        if (ys[i] < miny) miny = ys[i];
        if (ys[i] > maxy) maxy = ys[i];
    }
    /* round to nearest; (int)(v + 0.5) truncates toward zero, which put a
     * node scrolled above or left of the origin one pixel off (-140 -> -139) */
    out->x = (int)floor(minx + 0.5);
    out->y = (int)floor(miny + 0.5);
    out->w = (int)floor(maxx - minx + 0.5);
    out->h = (int)floor(maxy - miny + 0.5);
    return 0;
}

static void gv_rect_clip(gv_rect *a, const gv_rect *b) {
    int x0 = a->x > b->x ? a->x : b->x;
    int y0 = a->y > b->y ? a->y : b->y;
    int x1 = a->x + a->w < b->x + b->w ? a->x + a->w : b->x + b->w;
    int y1 = a->y + a->h < b->y + b->h ? a->y + a->h : b->y + b->h;
    a->x = x0; a->y = y0;
    a->w = x1 > x0 ? x1 - x0 : 0;
    a->h = y1 > y0 ? y1 - y0 : 0;
}

int gv_view_visible_rect(gv_view *v, gv_node_id id, gv_rect *out) {
    gv_node *n = gv_get(v, id);
    if (!n || !out) return -1;
    if (gv_view_screen_rect(v, id, out) != 0) return -1;
    for (gv_node_id cur = id; cur;) {
        gv_node *c = gv_get(v, cur);
        if (!c || !c->visible) return -1;
        if (cur != id && (c->type == GV_NODE_CLIP || c->type == GV_NODE_VIEWPORT ||
                          c->type == GV_NODE_WINDOW || !c->parent)) {
            gv_rect cr;
            if (gv_view_screen_rect(v, cur, &cr) != 0) return -1;
            gv_rect_clip(out, &cr);
        }
        cur = c->parent;
    }
    gv_rect win = {0, 0, v->width, v->height};
    gv_rect_clip(out, &win);
    return out->w > 0 && out->h > 0 ? 0 : -1;
}

int gv_view_stats_nodes_created(gv_view *v) { return v ? v->nodes_created : 0; }
int gv_view_stats_nodes_destroyed(gv_view *v) { return v ? v->nodes_destroyed : 0; }
int gv_view_stats_layouts(gv_view *v) { return v ? v->layouts : 0; }
int gv_view_stats_hit_tests(gv_view *v) { return v ? v->hit_tests : 0; }
int gv_view_stats_draw_ops(gv_view *v) { return v ? v->draw_ops : 0; }