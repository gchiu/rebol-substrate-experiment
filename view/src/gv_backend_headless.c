#include "gv_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void gv_display_list_init(gv_display_list *dl) {
    if (!dl) return;
    dl->cmds = NULL;
    dl->count = 0;
    dl->cap = 0;
    dl->overflow = 0;
}

void gv_display_list_free(gv_display_list *dl) {
    if (!dl) return;
    free(dl->cmds);
    dl->cmds = NULL;
    dl->count = 0;
    dl->cap = 0;
    dl->overflow = 0;
}

int gv_display_list_add(gv_display_list *dl, const gv_cmd *cmd) {
    if (!dl || !cmd) return -1;
    if (dl->count == dl->cap) {
        int cap = dl->cap ? dl->cap * 2 : 16;
        void *p = realloc(dl->cmds, (size_t)cap * sizeof(gv_cmd));
        if (!p) {
            dl->overflow = 1;
            return -1;
        }
        dl->cmds = (gv_cmd *)p;
        dl->cap = cap;
    }
    dl->cmds[dl->count++] = *cmd;
    return 0;
}

typedef struct gv_headless {
    int width;
    int height;
    uint64_t hash;
    int frames;
    gv_event queue[64];
    int qhead;
    int qtail;
    char clipboard[1024];
    int clipboard_set;
} gv_headless;

static uint64_t gv_hash_byte(uint64_t h, uint8_t v) {
    h ^= (uint64_t)v;
    return h * UINT64_C(1099511628211);
}

static uint64_t gv_hash_u32(uint64_t h, uint32_t v) {
    for (int i = 0; i < 4; i++) {
        h = gv_hash_byte(h, (uint8_t)(v >> (i * 8)));
    }
    return h;
}

static uint64_t gv_hash_i32(uint64_t h, int32_t v) {
    return gv_hash_u32(h, (uint32_t)v);
}

static uint64_t gv_hash_double(uint64_t h, double v) {
    uint64_t bits;
    memcpy(&bits, &v, sizeof bits);
    for (int i = 0; i < 8; i++) {
        h = gv_hash_byte(h, (uint8_t)(bits >> (i * 8)));
    }
    return h;
}

static uint64_t gv_hash_str(uint64_t h, const char *s, uint32_t n) {
    if (!s || n == 0) return gv_hash_u32(h, 0);
    for (uint32_t i = 0; i < n; i++) {
        h = gv_hash_byte(h, (uint8_t)s[i]);
    }
    return h;
}

static int gv_headless_init(gv_backend *b, const gv_backend_config *cfg) {
    if (!b) return -1;
    gv_headless *h = (gv_headless *)calloc(1, sizeof(*h));
    if (!h) return -1;
    h->width = cfg && cfg->width > 0 ? cfg->width : 640;
    h->height = cfg && cfg->height > 0 ? cfg->height : 480;
    h->hash = UINT64_C(1469598103934665603);
    h->hash = gv_hash_i32(h->hash, h->width);
    h->hash = gv_hash_i32(h->hash, h->height);
    if (cfg && cfg->title) {
        h->hash = gv_hash_str(h->hash, cfg->title, (uint32_t)strlen(cfg->title));
    }
    b->impl = h;
    return 0;
}

static void gv_headless_shutdown(gv_backend *b) {
    if (!b) return;
    free(b->impl);
    b->impl = NULL;
}

static void gv_headless_destroy(gv_backend *b) {
    gv_headless_shutdown(b);
    free(b);
}

static int gv_headless_create_window(gv_backend *b, int w, int h, const char *title) {
    if (!b || !b->impl) return -1;
    gv_headless *x = (gv_headless *)b->impl;
    x->width = w;
    x->height = h;
    x->hash = gv_hash_i32(x->hash, w);
    x->hash = gv_hash_i32(x->hash, h);
    if (title) x->hash = gv_hash_str(x->hash, title, (uint32_t)strlen(title));
    return 0;
}

static void gv_headless_destroy_window(gv_backend *b) {
    (void)b;
}

static int gv_headless_resize_window(gv_backend *b, int w, int h) {
    if (!b || !b->impl) return -1;
    gv_headless *x = (gv_headless *)b->impl;
    x->width = w;
    x->height = h;
    x->hash = gv_hash_i32(x->hash, w);
    x->hash = gv_hash_i32(x->hash, h);
    return 0;
}

static int gv_headless_begin_frame(gv_backend *b) {
    if (!b || !b->impl) return -1;
    ((gv_headless *)b->impl)->frames++;
    return 0;
}

static int gv_headless_draw(gv_backend *b, const gv_display_list *dl) {
    if (!b || !b->impl) return -1;
    gv_headless *x = (gv_headless *)b->impl;
    if (dl) {
        for (int i = 0; i < dl->count; i++) {
            const gv_cmd *c = &dl->cmds[i];
            x->hash = gv_hash_u32(x->hash, (uint32_t)c->op);
            x->hash = gv_hash_i32(x->hash, c->rect.x);
            x->hash = gv_hash_i32(x->hash, c->rect.y);
            x->hash = gv_hash_i32(x->hash, c->rect.w);
            x->hash = gv_hash_i32(x->hash, c->rect.h);
            x->hash = gv_hash_u32(x->hash, (uint32_t)c->color.r |
                                  ((uint32_t)c->color.g << 8) |
                                  ((uint32_t)c->color.b << 16) |
                                  ((uint32_t)c->color.a << 24));
            x->hash = gv_hash_u32(x->hash, (uint32_t)c->color2.r |
                                  ((uint32_t)c->color2.g << 8) |
                                  ((uint32_t)c->color2.b << 16) |
                                  ((uint32_t)c->color2.a << 24));
            x->hash = gv_hash_double(x->hash, c->f1);
            x->hash = gv_hash_double(x->hash, c->f2);
            x->hash = gv_hash_double(x->hash, c->f3);
            x->hash = gv_hash_double(x->hash, c->f4);
            x->hash = gv_hash_double(x->hash, c->transform.a);
            x->hash = gv_hash_double(x->hash, c->transform.b);
            x->hash = gv_hash_double(x->hash, c->transform.c);
            x->hash = gv_hash_double(x->hash, c->transform.d);
            x->hash = gv_hash_double(x->hash, c->transform.e);
            x->hash = gv_hash_double(x->hash, c->transform.f);
            x->hash = gv_hash_u32(x->hash, c->text_len);
            x->hash = gv_hash_str(x->hash, c->text, c->text_len);
            x->hash = gv_hash_i32(x->hash, c->image_w);
            x->hash = gv_hash_i32(x->hash, c->image_h);
        }
    }
    return 0;
}

static int gv_headless_end_frame(gv_backend *b) {
    (void)b;
    return 0;
}

static int gv_headless_poll_event(gv_backend *b, gv_event *ev) {
    if (!b || !b->impl || !ev) return -1;
    gv_headless *x = (gv_headless *)b->impl;
    if (x->qhead == x->qtail) {
        memset(ev, 0, sizeof *ev);
        ev->type = GV_EVENT_NONE;
        return 0;
    }
    *ev = x->queue[x->qhead];
    x->qhead = (x->qhead + 1) % (int)(sizeof x->queue / sizeof x->queue[0]);
    return 1;
}

static int gv_headless_has_cap(gv_backend *b, gv_cap cap) {
    (void)b;
    return cap == GV_CAP_WINDOW || cap == GV_CAP_HEADLESS;
}

static void *gv_headless_native_handle(gv_backend *b) {
    (void)b;
    return NULL;
}

static int gv_headless_native_control_create(gv_backend *b, int kind, gv_rect rect,
                                             const char *text, void **out) {
    (void)b; (void)kind; (void)rect; (void)text;
    if (out) *out = NULL;
    return -1;
}

static void gv_headless_native_control_destroy(gv_backend *b, void *ctrl) {
    (void)b; (void)ctrl;
}

static int gv_headless_native_control_set_text(gv_backend *b, void *ctrl,
                                               const char *text) {
    (void)b; (void)ctrl; (void)text;
    return -1;
}

static int gv_headless_native_control_get_text(gv_backend *b, void *ctrl,
                                               char *buf, int cap) {
    (void)b; (void)ctrl; (void)buf; (void)cap;
    return -1;
}

static int gv_headless_native_control_set_focus(gv_backend *b, void *ctrl, int focus) {
    (void)b; (void)ctrl; (void)focus;
    return -1;
}

static int gv_headless_set_clipboard(gv_backend *b, const char *text) {
    (void)b; (void)text;
    return -1;
}

static int gv_headless_get_clipboard(gv_backend *b, char *buf, int cap) {
    (void)b; (void)buf; (void)cap;
    return -1;
}

gv_backend *gv_backend_headless(void) {
    gv_backend *b = (gv_backend *)calloc(1, sizeof(*b));
    if (!b) return NULL;
    snprintf(b->name, sizeof b->name, "headless");
    b->init = gv_headless_init;
    b->shutdown = gv_headless_shutdown;
    b->destroy = gv_headless_destroy;
    b->create_window = gv_headless_create_window;
    b->destroy_window = gv_headless_destroy_window;
    b->resize_window = gv_headless_resize_window;
    b->begin_frame = gv_headless_begin_frame;
    b->draw = gv_headless_draw;
    b->end_frame = gv_headless_end_frame;
    b->poll_event = gv_headless_poll_event;
    b->has_cap = gv_headless_has_cap;
    b->native_handle = gv_headless_native_handle;
    b->native_control_create = gv_headless_native_control_create;
    b->native_control_destroy = gv_headless_native_control_destroy;
    b->native_control_set_text = gv_headless_native_control_set_text;
    b->native_control_get_text = gv_headless_native_control_get_text;
    b->native_control_set_focus = gv_headless_native_control_set_focus;
    b->set_clipboard = gv_headless_set_clipboard;
    b->get_clipboard = gv_headless_get_clipboard;
    return b;
}

uint64_t gv_backend_headless_hash(gv_backend *b) {
    if (!b || !b->impl) return 0;
    return ((gv_headless *)b->impl)->hash;
}