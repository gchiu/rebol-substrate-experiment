#define _POSIX_C_SOURCE 200809L
#include "gv_glon_host.h"
#include "r0_s1.h"
#include "r0_s1_g1a_live.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GV_GLON_MAX_NODES 1024
#define GV_GLON_MAX_VIEW_NODES 2048
#define GV_GLON_MAX_ARGS 2048
#define GV_GLON_MAX_BINDS 64
#define GV_GLON_MAX_TEXT 131072
#define GV_GLON_MAX_TABLES 32

typedef struct gv_bind {
    int id;
    int prop;
    char word[64];
} gv_bind;

static gv_node_type gv_map_type(int t);
static int gv_parse_ints(const char *s, int *out, int max);
static int gv_word_value(gv_glon_host *h, const char *word, char *buf, int cap);
static void gv_apply_spec(gv_glon_host *h, int *args, int count);
static void gv_glon_host_call(const char *op, int op_len, const char *arg,
                              int arg_len, void *user);
static void gv_glon_table_command(gv_glon_host *h, const char *arg, int len);
static void gv_glon_batch_command(gv_glon_host *h, const char *arg, int len);
static int gv_send_text(gv_glon_host *h, const char *token, const char *text);
static int gv_view_next_int(char **p, int *out);
static int gv_view_next_word(char **p, char *buf, int cap);
static void gv_view_rest(char **p, char *buf, int cap);

struct gv_glon_host {
    gv_view *view;
    gv_backend *backend;
    int error;
    gv_display_list display;
    gv_node_id glon_to_view[GV_GLON_MAX_NODES];
    gv_node_id view_to_glon[GV_GLON_MAX_VIEW_NODES];
    gv_bind binds[GV_GLON_MAX_BINDS];
    int bind_count;
    char root[1024];
    char text[GV_GLON_MAX_TEXT];
    size_t text_len;
    gv_table *tables[GV_GLON_MAX_TABLES];
    int table_count;
    int table_sort_col;
    int table_sort_dir;
    int table_select_row;
    int table_policy_init;
    char table_filter[GV_TABLE_MAX_TEXT];
    int view_batch;
    int last_batch_len;
    int max_batch_len;
    int take_pending;         /* F1B: last fragment was "batch-ref" */
    int ref_chunks;           /* F1B: chunks applied from references */
    int ref_takes;            /* F1B: widget/take runs */
    int ref_errors;           /* F1B: rejected reference batches */
    int view_caps_pending;
    int view_query_pending;
    int view_query_id;
    int view_query_parent;
    int view_get_pending;
    int view_get_query;
    int view_get_id;
    int view_get_prop;
    int clipboard_paste_pending;
    int table_query_pending;
    int table_query_type;
    int table_query_id;
    int table_query_a;
    int table_query_b;
    int table_query_count;
    int table_query_q[16];
    int table_query_row[16];
    int table_query_col[16];
    char **draw_texts;
    int draw_text_count;
    int draw_text_cap;
};

static char *gv_read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(buf); return NULL; }
    buf[got] = 0;
    return buf;
}

static void gv_strip_comments(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') {
            while (*r && *r != '\n') r++;
            continue;
        }
        *w++ = *r++;
    }
    *w = 0;
}

static int gv_load_file(gv_glon_host *h, const char *path) {
    char *src = gv_read_file(path);
    if (!src) {
        fprintf(stderr, "gv-glon: cannot read %s\n", path);
        h->error = 1;
        return -1;
    }
    gv_strip_comments(src);
    int perr = 0;
    cell prog = r0_s1_parse(src, &perr);
    free(src);
    r0_s1_outcome oc;
    if (perr) r0_s1_session_parse_error(&oc);
    else r0_s1_session_run_block(prog, &oc);
    if (oc.status != R0S1_OUT_OK) {
        cell t, i, a;
        int has_sin = r0_s1_uncaught_error(&t, &i, &a);
        char detail[512] = "";
        if (has_sin) {
            char ts[64], is[64], as[64];
            r0_s1_mold(t, ts, sizeof ts);
            r0_s1_mold(i, is, sizeof is);
            r0_s1_mold(a, as, sizeof as);
            snprintf(detail, sizeof detail, " sin=%s/%s/%s", ts, is, as);
        }
        fprintf(stderr, "gv-glon: %s did not run cleanly (%s / %s, halt=%d%s)\n",
                path, r0_s1_outcome_status_name(oc.status),
                r0_s1_outcome_detail_name(oc.detail), r0_s1_halt_reason(), detail);
        h->error = 1;
        return -1;
    }
    return 0;
}

static char *gv_host_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}



gv_glon_host *gv_glon_host_new(gv_view *view, gv_backend *backend) {
    gv_glon_host *h = (gv_glon_host *)calloc(1, sizeof(*h));
    if (!h) return NULL;
    h->view = view;
    h->backend = backend;
    gv_display_list_init(&h->display);
    if (view) {
        h->glon_to_view[1] = gv_view_root(view);
        if (h->glon_to_view[1] > 0 &&
            h->glon_to_view[1] < GV_GLON_MAX_VIEW_NODES) {
            h->view_to_glon[h->glon_to_view[1]] = 1;
        }
    }
    r0_s1_g1a_live_set_host_call(gv_glon_host_call, h);
    return h;
}

void gv_glon_host_free(gv_glon_host *h) {
    if (!h) return;
    for (int i = 0; i < h->table_count; i++) gv_table_free(h->tables[i]);
    for (int i = 0; i < h->draw_text_count; i++) free(h->draw_texts[i]);
    free(h->draw_texts);
    gv_display_list_free(&h->display);
    free(h);
}

static int gv_r0_ready;

static const char *gv_view_dup_text(gv_glon_host *h, const char *text) {
    if (!text) text = "";
    if (h->draw_text_count == h->draw_text_cap) {
        int cap = h->draw_text_cap ? h->draw_text_cap * 2 : 32;
        char **p = (char **)realloc(h->draw_texts, (size_t)cap * sizeof(char *));
        if (!p) return NULL;
        h->draw_texts = p;
        h->draw_text_cap = cap;
    }
    size_t n = strlen(text) + 1;
    char *copy = (char *)malloc(n);
    if (!copy) return NULL;
    memcpy(copy, text, n);
    h->draw_texts[h->draw_text_count++] = copy;
    return copy;
}

static int gv_view_next_int(char **p, int *out) {
    char *s = *p;
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    if (*s < '0' || *s > '9') return 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s) return 0;
    *out = (int)(neg ? -v : v);
    *p = end;
    return 1;
}

static int gv_view_next_word(char **p, char *buf, int cap) {
    char *s = *p;
    while (*s == ' ' || *s == '\t') s++;
    int n = 0;
    while (*s && *s != ' ' && *s != '\t' && n < cap - 1) buf[n++] = *s++;
    buf[n] = 0;
    *p = s;
    return n > 0;
}

static void gv_view_rest(char **p, char *buf, int cap) {
    char *s = *p;
    while (*s == ' ' || *s == '\t') s++;
    snprintf(buf, (size_t)cap, "%s", s);
}

static void gv_view_command(gv_glon_host *h, const char *arg, int len) {
    char buf[4096];
    int n = len;
    if (n < 0) n = 0;
    if (n >= (int)sizeof buf) n = (int)sizeof buf - 1;
    memcpy(buf, arg, (size_t)n);
    buf[n] = 0;
    char *p = buf;
    char cmd[32];
    if (!gv_view_next_word(&p, cmd, sizeof cmd)) return;
    if (strcmp(cmd, "new") == 0) {
        int id, parent, type;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &parent) ||
            !gv_view_next_int(&p, &type)) { h->error = 1; return; }
        if (id <= 0 || id >= GV_GLON_MAX_NODES) { h->error = 1; return; }
        if (id == 1) return;
        if (h->glon_to_view[id]) { h->error = 1; return; }
        gv_node_id pid = 0;
        if (parent > 0 && parent < GV_GLON_MAX_NODES) pid = h->glon_to_view[parent];
        gv_node_id v = gv_view_add(h->view, pid, gv_map_type(type));
        if (!v) { h->error = 1; return; }
        h->glon_to_view[id] = v;
        if (v > 0 && v < GV_GLON_MAX_VIEW_NODES) h->view_to_glon[v] = (gv_node_id)id;
    } else if (strcmp(cmd, "create") == 0) {
        int id, parent, type, x, y, w, hh, vis, en, focus, pick;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &parent) ||
            !gv_view_next_int(&p, &type) || !gv_view_next_int(&p, &x) ||
            !gv_view_next_int(&p, &y) || !gv_view_next_int(&p, &w) ||
            !gv_view_next_int(&p, &hh) || !gv_view_next_int(&p, &vis) ||
            !gv_view_next_int(&p, &en) || !gv_view_next_int(&p, &focus) ||
            !gv_view_next_int(&p, &pick)) return;
        if (id <= 0 || id >= GV_GLON_MAX_NODES) { h->error = 1; return; }
        if (id == 1) { gv_view_set_rect(h->view, gv_view_root(h->view), x, y, w, hh); return; }
        if (h->glon_to_view[id]) { h->error = 1; return; }
        gv_node_id pid = 0;
        if (parent > 0 && parent < GV_GLON_MAX_NODES) pid = h->glon_to_view[parent];
        gv_node_id v = gv_view_add(h->view, pid, gv_map_type(type));
        if (!v) { h->error = 1; return; }
        h->glon_to_view[id] = v;
        if (v > 0 && v < GV_GLON_MAX_VIEW_NODES) h->view_to_glon[v] = (gv_node_id)id;
        gv_view_set_rect(h->view, v, x, y, w, hh);
        gv_view_set_flags(h->view, v, vis, en, focus, pick);
    } else if (strcmp(cmd, "destroy") == 0) {
        int id;
        if (!gv_view_next_int(&p, &id)) return;
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            if (v) {
                if (v > 0 && v < GV_GLON_MAX_VIEW_NODES) h->view_to_glon[v] = 0;
                gv_view_destroy(h->view, v);
            }
            h->glon_to_view[id] = 0;
        }
    } else if (strcmp(cmd, "rect") == 0) {
        int id, x, y, w, hh;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &x) ||
            !gv_view_next_int(&p, &y) || !gv_view_next_int(&p, &w) ||
            !gv_view_next_int(&p, &hh)) return;
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            if (v) gv_view_set_rect(h->view, v, x, y, w, hh);
        }
    } else if (strcmp(cmd, "text") == 0) {
        int id;
        char text[2048];
        if (!gv_view_next_int(&p, &id)) return;
        gv_view_rest(&p, text, sizeof text);
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            if (v) gv_view_set_text(h->view, v, text);
        }
    } else if (strcmp(cmd, "bg") == 0) {
        int id, r, g, b, a;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &r) ||
            !gv_view_next_int(&p, &g) || !gv_view_next_int(&p, &b) ||
            !gv_view_next_int(&p, &a)) return;
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node *n = gv_view_node(h->view, h->glon_to_view[id]);
            if (n) {
                gv_color fg = n->fg;
                gv_view_set_colors(h->view, h->glon_to_view[id],
                                   (gv_color){(uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a}, fg);
            }
        }
    } else if (strcmp(cmd, "fg") == 0) {
        int id, r, g, b, a;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &r) ||
            !gv_view_next_int(&p, &g) || !gv_view_next_int(&p, &b) ||
            !gv_view_next_int(&p, &a)) return;
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node *n = gv_view_node(h->view, h->glon_to_view[id]);
            if (n) {
                gv_color bg = n->bg;
                gv_view_set_colors(h->view, h->glon_to_view[id], bg,
                                   (gv_color){(uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a});
            }
        }
    } else if (strcmp(cmd, "colors") == 0) {
        int id, r, g, b, a, fr, fg, fb, fa;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &r) ||
            !gv_view_next_int(&p, &g) || !gv_view_next_int(&p, &b) ||
            !gv_view_next_int(&p, &a) || !gv_view_next_int(&p, &fr) ||
            !gv_view_next_int(&p, &fg) || !gv_view_next_int(&p, &fb) ||
            !gv_view_next_int(&p, &fa)) return;
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            if (v) gv_view_set_colors(h->view, v,
                                      (gv_color){(uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a},
                                      (gv_color){(uint8_t)fr, (uint8_t)fg, (uint8_t)fb, (uint8_t)fa});
        }
    } else if (strcmp(cmd, "flags") == 0) {
        int id, vis, en, focus, pick;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &vis) ||
            !gv_view_next_int(&p, &en) || !gv_view_next_int(&p, &focus) ||
            !gv_view_next_int(&p, &pick)) return;
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            if (v) gv_view_set_flags(h->view, v, vis, en, focus, pick);
        }
    } else if (strcmp(cmd, "layout") == 0) {
        int id, dir, align, spacing, pad;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &dir) ||
            !gv_view_next_int(&p, &align) || !gv_view_next_int(&p, &spacing) ||
            !gv_view_next_int(&p, &pad)) return;
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            if (v) gv_view_set_layout(h->view, v, dir, align, spacing, pad);
        }
    } else if (strcmp(cmd, "minmax") == 0) {
        int id, minw, minh, maxw, maxh;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &minw) ||
            !gv_view_next_int(&p, &minh) || !gv_view_next_int(&p, &maxw) ||
            !gv_view_next_int(&p, &maxh)) return;
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            if (v) gv_view_set_minmax(h->view, v, minw, minh, maxw, maxh);
        }
    } else if (strcmp(cmd, "fill") == 0) {
        int id, fx, fy;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &fx) ||
            !gv_view_next_int(&p, &fy)) return;
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            if (v) gv_view_set_fill(h->view, v, fx, fy);
        }
    } else if (strcmp(cmd, "font") == 0) {
        int id, size;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &size)) return;
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            if (v) gv_view_set_font(h->view, v, (double)size);
        }
    } else if (strcmp(cmd, "text-align") == 0) {
        int id, align;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &align)) return;
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            if (v) gv_view_set_text_align(h->view, v, align);
        }
    } else if (strcmp(cmd, "parent") == 0 || strcmp(cmd, "reparent") == 0) {
        int id, parent;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &parent)) return;
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            gv_node_id pid = 0;
            if (parent > 0 && parent < GV_GLON_MAX_NODES) pid = h->glon_to_view[parent];
            if (v) gv_view_set_parent(h->view, v, pid);
        }
    } else if (strcmp(cmd, "focus") == 0) {
        int id;
        if (!gv_view_next_int(&p, &id)) return;
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            if (v) gv_view_focus(h->view, v);
        }
    } else if (strcmp(cmd, "viewport") == 0) {
        int id, x, y;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &x) ||
            !gv_view_next_int(&p, &y)) return;
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            if (v) gv_view_set_viewport(h->view, v, x, y, 1.0);
        }
    } else if (strcmp(cmd, "invalidate") == 0) {
        int id, layout;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &layout)) return;
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            if (v) gv_view_invalidate(h->view, v, layout);
        }
    } else if (strcmp(cmd, "begin") == 0) {
        h->view_batch++;
    } else if (strcmp(cmd, "end") == 0) {
        if (h->view_batch > 0) h->view_batch--;
    } else if (strcmp(cmd, "capabilities") == 0) {
        h->view_caps_pending = 1;
    } else if (strcmp(cmd, "enumerate") == 0) {
        int id, query;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &query)) return;
        h->view_query_pending = 1;
        h->view_query_id = query;
        h->view_query_parent = id;
    } else if (strcmp(cmd, "get") == 0) {
        int id, prop, query;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &prop) ||
            !gv_view_next_int(&p, &query)) return;
        h->view_get_pending = 1;
        h->view_get_query = query;
        h->view_get_id = id;
        h->view_get_prop = prop;
    } else if (strcmp(cmd, "clipboard-set") == 0) {
        char text[4096];
        gv_view_rest(&p, text, sizeof text);
        if (h->backend && h->backend->set_clipboard)
            h->backend->set_clipboard(h->backend, text);
    } else if (strcmp(cmd, "clipboard-paste") == 0) {
        h->clipboard_paste_pending = 1;
    } else if (strcmp(cmd, "draw-clear") == 0) {
        int id;
        if (!gv_view_next_int(&p, &id)) return;
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            gv_node *n = gv_view_node(h->view, v);
            if (n) {
                gv_display_list_free(&n->draw_list);
                gv_display_list_init(&n->draw_list);
                gv_view_invalidate(h->view, v, 0);
            }
        }
    } else if (strcmp(cmd, "draw-rect") == 0) {
        int id, x, y, w, hh, r, g, b, a;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &x) ||
            !gv_view_next_int(&p, &y) || !gv_view_next_int(&p, &w) ||
            !gv_view_next_int(&p, &hh) || !gv_view_next_int(&p, &r) ||
            !gv_view_next_int(&p, &g) || !gv_view_next_int(&p, &b) ||
            !gv_view_next_int(&p, &a)) return;
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            gv_node *n = gv_view_node(h->view, v);
            if (n) {
                gv_cmd c;
                memset(&c, 0, sizeof c);
                c.op = GV_DRAW_RECT;
                c.rect.x = x; c.rect.y = y; c.rect.w = w; c.rect.h = hh;
                c.color.r = (uint8_t)r; c.color.g = (uint8_t)g;
                c.color.b = (uint8_t)b; c.color.a = (uint8_t)a;
                gv_display_list_add(&n->draw_list, &c);
                gv_view_invalidate(h->view, v, 0);
            }
        }
    } else if (strcmp(cmd, "draw-text") == 0) {
        int id, x, y, size, r, g, b, a;
        char text[2048];
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &x) ||
            !gv_view_next_int(&p, &y) || !gv_view_next_int(&p, &size) ||
            !gv_view_next_int(&p, &r) || !gv_view_next_int(&p, &g) ||
            !gv_view_next_int(&p, &b) || !gv_view_next_int(&p, &a)) return;
        gv_view_rest(&p, text, sizeof text);
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            gv_node *n = gv_view_node(h->view, v);
            if (n) {
                const char *copy = gv_view_dup_text(h, text);
                if (!copy) { h->error = 1; return; }
                gv_cmd c;
                memset(&c, 0, sizeof c);
                c.op = GV_DRAW_TEXT;
                c.rect.x = x; c.rect.y = y;
                c.text = copy;
                c.text_len = (uint32_t)strlen(copy);
                c.f1 = (double)size;
                c.color.r = (uint8_t)r; c.color.g = (uint8_t)g;
                c.color.b = (uint8_t)b; c.color.a = (uint8_t)a;
                gv_display_list_add(&n->draw_list, &c);
                gv_view_invalidate(h->view, v, 0);
            }
        }
    }
}


static void gv_glon_batch_command(gv_glon_host *h, const char *arg, int len) {
    h->last_batch_len = len;
    if (len > h->max_batch_len) h->max_batch_len = len;
    char buf[32768];
    int n = len;
    if (n < 0) n = 0;
    if (n >= (int)sizeof buf) n = (int)sizeof buf - 1;
    memcpy(buf, arg, (size_t)n);
    buf[n] = 0;
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        while (*line == ' ' || *line == '\t') line++;
        if (strncmp(line, "view ", 5) == 0)
            gv_view_command(h, line + 5, (int)strlen(line + 5));
        else if (strncmp(line, "table ", 6) == 0)
            gv_glon_table_command(h, line + 6, (int)strlen(line + 6));
    }
}

static void gv_glon_host_call(const char *op, int op_len, const char *arg,
                              int arg_len, void *user) {
    gv_glon_host *h = (gv_glon_host *)user;
    if (!h) return;
    if (getenv("GV_DEBUG"))
        fprintf(stderr, "host-call op=%.*s arg=%.*s\n", op_len, op, arg_len, arg);
    if (op_len == 4 && strncmp(op, "view", 4) == 0)
        gv_view_command(h, arg, arg_len);
    else if (op_len == 5 && strncmp(op, "table", 5) == 0)
        gv_glon_table_command(h, arg, arg_len);
    else if (op_len == 5 && strncmp(op, "batch", 5) == 0)
        gv_glon_batch_command(h, arg, arg_len);
    else if (op_len == 9 && strncmp(op, "batch-ref", 9) == 0)
        h->take_pending = 1;              /* F1B: fetch after the dispatch */
}

/* F1B experiment: by-reference batch transport.  After a dispatch whose final
 * fragment was "batch-ref", run the existing Glon function widget/take, which
 * returns (and forgets) the published chunk lists.  The value is read with the
 * runtime's result API; nothing is written into Glon memory.  The structure is
 * validated completely before any command is applied (all or nothing). */
#define F1B_MAX_CHUNKS 1024
#define F1B_MAX_BYTES  (1L << 20)
static int f1b_is(cell v, int tag) { return r0_tag(v) == tag; }
static int f1b_take(gv_glon_host *h) {
    h->take_pending = 0;
    h->ref_takes++;
    cell saved_lh = M[GC_LOADER_HP];
    int perr = 0;
    cell prog = r0_s1_parse("[ widget/take ]", &perr);
    int n = perr ? 0 : r0_s1_run_persistent(prog);
    M[GC_LOADER_HP] = saved_lh;
    if (perr || n < 1 || !r0_s1_ran_cleanly()) { h->ref_errors++; return -1; }
    cell v = r0_s1_result(0, n);
    static cell flushes[F1B_MAX_CHUNKS], chunks[F1B_MAX_CHUNKS];
    int nf = 0, nc = 0;
    long bytes = 0;
    /* outer list (newest flush first): [ inner older-outer ] ... 0 */
    while (!(r0_tag(v) == 0 && v == 0)) {
        if (!f1b_is(v, T_BLOCK) || M[r0_untag(v)] != 2 || nf >= F1B_MAX_CHUNKS) goto bad;
        flushes[nf++] = M[r0_untag(v) + BLK_DATA];
        v = M[r0_untag(v) + BLK_DATA + 1];
    }
    /* oldest flush first; inner lists are newest chunk first */
    for (int f = nf - 1; f >= 0; f--) {
        int first = nc;
        cell w = flushes[f];
        while (!(w == 0)) {
            if (!f1b_is(w, T_BLOCK) || M[r0_untag(w)] != 2 || nc >= F1B_MAX_CHUNKS) goto bad;
            cell sv = M[r0_untag(w) + BLK_DATA];
            if (!f1b_is(sv, T_STRING)) goto bad;
            cell len = M[r0_untag(sv)];
            if (len < 0 || len >= 32768) goto bad;
            if (len > 0 && M[r0_untag(sv) + len] != '\n') goto bad;   /* ends on a command */
            bytes += len;
            if (bytes > F1B_MAX_BYTES) goto bad;
            if (len > 0) chunks[nc++] = sv;                       /* empty: nothing queued */
            w = M[r0_untag(w) + BLK_DATA + 1];
        }
        for (int a = first, b = nc - 1; a < b; a++, b--) { cell t = chunks[a]; chunks[a] = chunks[b]; chunks[b] = t; }
    }
    static char buf[32768];
    for (int c = 0; c < nc; c++) {
        cell p = r0_untag(chunks[c]);
        cell len = M[p];
        for (cell i = 0; i < len; i++) {
            cell byte = M[p + 1 + i];
            if (byte <= 0 || byte > 255) goto bad;                 /* no NUL / non-byte */
            buf[i] = (char)byte;
        }
        buf[len] = 0;
        if (getenv("F1B_TRACE"))
            fprintf(stderr, "[f1b] chunk %d/%d bytes=%ld first=\"%.24s\"\n", c + 1, nc, (long)len, buf);
        gv_glon_batch_command(h, buf, (int)len);
        h->ref_chunks++;
    }
    return 0;
bad:
    fprintf(stderr, "gv-glon: rejected batch-ref structure\n");
    h->ref_errors++;
    h->error = 1;
    return -1;
}

void gv_glon_host_f1b_stats(gv_glon_host *h, int *takes, int *chunks, int *errors) {
    *takes = h->ref_takes; *chunks = h->ref_chunks; *errors = h->ref_errors;
}

int gv_glon_host_load(gv_glon_host *h, const char *root) {
    if (!h) return -1;
    snprintf(h->root, sizeof h->root, "%s", root ? root : ".");
    if (!gv_r0_ready) {
        r0_s1_init();
        gv_r0_ready = 1;
    }
    static const char *files[] = {
        "jupyter/prelude.glon",
        "demo/shop/strings.glon",
        "demo/shop/strings-ext.glon",
        "demo/shop/case.glon",
        "view/glon/demo.glon",
    };
    char path[2048];
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        if (root && root[0]) snprintf(path, sizeof path, "%s/%s", root, files[i]);
        else snprintf(path, sizeof path, "%s", files[i]);
        if (gv_load_file(h, path) != 0) return -1;
    }
    return 0;
}

int gv_glon_host_load_app(gv_glon_host *h, const char *root,
                          const char *app_path) {
    if (!h || !app_path) return -1;
    snprintf(h->root, sizeof h->root, "%s", root ? root : ".");
    if (!gv_r0_ready) {
        r0_s1_init();
        gv_r0_ready = 1;
    }
    static const char *files[] = {
        "jupyter/prelude.glon",
        "demo/shop/strings.glon",
        "demo/shop/strings-ext.glon",
        "demo/shop/case.glon",
        "view/glon/view_lib.glon",
        "view/glon/vid_table.glon",
    };
    char path[2048];
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        if (root && root[0]) snprintf(path, sizeof path, "%s/%s", root, files[i]);
        else snprintf(path, sizeof path, "%s", files[i]);
        if (gv_load_file(h, path) != 0) return -1;
    }
    if (app_path[0] == '/') snprintf(path, sizeof path, "%s", app_path);
    else if (root && root[0]) snprintf(path, sizeof path, "%s/%s", root, app_path);
    else snprintf(path, sizeof path, "%s", app_path);
    return gv_load_file(h, path);
}

int gv_glon_host_load_vid_app(gv_glon_host *h, const char *root,
                              const char *app_path) {
    if (!h || !app_path) return -1;
    snprintf(h->root, sizeof h->root, "%s", root ? root : ".");
    if (!gv_r0_ready) {
        r0_s1_init();
        gv_r0_ready = 1;
    }
    static const char *files[] = {
        "jupyter/prelude.glon",
        "demo/shop/strings.glon",
        "demo/shop/strings-ext.glon",
        "demo/shop/case.glon",
        "view/glon/view_lib.glon",
        "view/glon/vid_table.glon",
        "view/glon/vid_table_data.glon",
        "view/glon/theme_lib.glon",
        "view/glon/vid_widgets.glon",
        "view/glon/vid_nav.glon",
        "view/glon/vid_fields.glon",
        "view/glon/vid_forms.glon",
    };
    char path[2048];
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        if (root && root[0]) snprintf(path, sizeof path, "%s/%s", root, files[i]);
        else snprintf(path, sizeof path, "%s", files[i]);
        if (gv_load_file(h, path) != 0) return -1;
    }
    if (app_path[0] == '/') snprintf(path, sizeof path, "%s", app_path);
    else if (root && root[0]) snprintf(path, sizeof path, "%s/%s", root, app_path);
    else snprintf(path, sizeof path, "%s", app_path);
    return gv_load_file(h, path);
}

int gv_glon_host_load_vocab_vid_app(gv_glon_host *h, const char *root,
                                    const char *app_path) {
    if (!h || !app_path) return -1;
    snprintf(h->root, sizeof h->root, "%s", root ? root : ".");
    if (!gv_r0_ready) {
        r0_s1_init();
        gv_r0_ready = 1;
    }
    static const char *files[] = {
        "jupyter/prelude.glon",
        "demo/shop/strings.glon",
        "demo/shop/strings-ext.glon",
        "demo/shop/case.glon",
        "view/glon/view_lib.glon",
        "view/glon/vid_table.glon",
        "view/glon/vid_table_data.glon",
        "view/glon/theme_lib.glon",
        "view/glon/vocab_lib.glon",
        "view/glon/vid_widgets.glon",
        "view/glon/vid_nav.glon",
        "view/glon/vid_fields.glon",
        "view/glon/vid_forms.glon"
    };
    char path[2048];
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        if (root && root[0]) snprintf(path, sizeof path, "%s/%s", root, files[i]);
        else snprintf(path, sizeof path, "%s", files[i]);
        if (gv_load_file(h, path) != 0) return -1;
    }
    if (app_path[0] == '/') snprintf(path, sizeof path, "%s", app_path);
    else if (root && root[0]) snprintf(path, sizeof path, "%s/%s", root, app_path);
    else snprintf(path, sizeof path, "%s", app_path);
    return gv_load_file(h, path);
}

int gv_glon_host_load_gui_app(gv_glon_host *h, const char *root,
                              const char *app_path) {
    if (!h || !app_path) return -1;
    snprintf(h->root, sizeof h->root, "%s", root ? root : ".");
    if (!gv_r0_ready) {
        r0_s1_init();
        gv_r0_ready = 1;
    }
    static const char *files[] = {
        "jupyter/prelude.glon",
        "demo/shop/strings.glon",
        "demo/shop/strings-ext.glon",
        "view/glon/view_lib.glon",
        "view/glon/vocab_lib.glon",
        "demo/shop/case.glon",
        "view/glon/component_lib.glon",
        "view/glon/gui_core.glon"
    };
    char path[2048];
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        if (root && root[0]) snprintf(path, sizeof path, "%s/%s", root, files[i]);
        else snprintf(path, sizeof path, "%s", files[i]);
        if (gv_load_file(h, path) != 0) return -1;
    }
    if (app_path[0] == '/') snprintf(path, sizeof path, "%s", app_path);
    else if (root && root[0]) snprintf(path, sizeof path, "%s/%s", root, app_path);
    else snprintf(path, sizeof path, "%s", app_path);
    return gv_load_file(h, path);
}

int gv_glon_host_load_gui3_app(gv_glon_host *h, const char *root,
                               const char *app_path) {
    if (!h || !app_path) return -1;
    snprintf(h->root, sizeof h->root, "%s", root ? root : ".");
    if (!gv_r0_ready) {
        r0_s1_init();
        gv_r0_ready = 1;
    }
    static const char *files[] = {
        "jupyter/prelude.glon",
        "demo/shop/strings.glon",
        "demo/shop/strings-ext.glon",
        "view/glon/view_lib.glon",
        "demo/shop/case.glon",
        "view/glon/component_lib.glon",
        "view/glon/gui3_lib.glon"
    };
    char path[2048];
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        if (root && root[0]) snprintf(path, sizeof path, "%s/%s", root, files[i]);
        else snprintf(path, sizeof path, "%s", files[i]);
        if (gv_load_file(h, path) != 0) return -1;
    }
    if (app_path[0] == '/') snprintf(path, sizeof path, "%s", app_path);
    else if (root && root[0]) snprintf(path, sizeof path, "%s/%s", root, app_path);
    else snprintf(path, sizeof path, "%s", app_path);
    return gv_load_file(h, path);
}

int gv_glon_host_build_app(gv_glon_host *h, const char *spec_path) {
    if (!h) return -1;
    if (spec_path) {
        char path[2048];
        if (spec_path[0] == '/') snprintf(path, sizeof path, "%s", spec_path);
        else if (h->root[0]) snprintf(path, sizeof path, "%s/%s", h->root, spec_path);
        else snprintf(path, sizeof path, "%s", spec_path);
        char *text = gv_read_file(path);
        if (!text) {
            fprintf(stderr, "gv-glon: cannot read %s\n", path);
            h->error = 1;
            return -1;
        }
        int args[GV_GLON_MAX_ARGS];
        int count = gv_parse_ints(text, args, GV_GLON_MAX_ARGS);
        free(text);
        if (count < 0) { h->error = 1; return -1; }
        gv_apply_spec(h, args, count);
        if (h->error) return -1;
    }
    if (!spec_path) {
        if (gv_send_text(h, "view-build", "") != 0) return -1;
    }
    return gv_glon_host_bind(h);
}

int gv_glon_host_run(gv_glon_host *h, const char *src) {
    if (!h || !src) return -1;
    cell saved_lh = M[GC_LOADER_HP];
    int perr = 0;
    cell prog = r0_s1_parse(src, &perr);
    r0_s1_outcome oc;
    if (perr) r0_s1_session_parse_error(&oc);
    else r0_s1_session_run_block(prog, &oc);
    M[GC_LOADER_HP] = saved_lh;
    if (oc.status != R0S1_OUT_OK) {
        fprintf(stderr, "gv-glon: run failed status=%s detail=%s halt=%d\n",
                r0_s1_outcome_status_name(oc.status),
                r0_s1_outcome_detail_name(oc.detail), r0_s1_halt_reason());
        h->error = 1;
        return -1;
    }
    return 0;
}

static int gv_parse_ints(const char *s, int *out, int max) {
    int count = 0;
    const char *p = s;
    while (*p && count < max) {
        while (*p && (*p < '0' || *p > '9') && *p != '-') p++;
        if (!*p) break;
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end == p) break;
        out[count++] = (int)v;
        p = end;
    }
    return count;
}

static int gv_word_value(gv_glon_host *h, const char *word, char *buf, int cap);
static gv_table *gv_glon_table(gv_glon_host *h, int glon_id);
static void gv_glon_render_tables(gv_glon_host *h);

static int gv_arg(int *args, int *idx, int count, gv_glon_host *h) {
    if (*idx >= count) { h->error = 1; return 0; }
    return args[(*idx)++];
}

static gv_node_type gv_map_type(int t) {
    switch (t) {
    case 1: return GV_NODE_WINDOW;
    case 2: return GV_NODE_GROUP;
    case 3: return GV_NODE_TEXT;
    case 4: return GV_NODE_RECT;
    case 5: return GV_NODE_IMAGE;
    case 6: return GV_NODE_TRANSFORM;
    case 7: return GV_NODE_CLIP;
    case 8: return GV_NODE_VIEWPORT;
    case 9: return GV_NODE_CUSTOM;
    case 10: return GV_NODE_NATIVE_CONTROL;
    default: return GV_NODE_GROUP;
    }
}

static void gv_spec_create(gv_glon_host *h, int id, int parent, int type,
                           int x, int y, int w, int hh, int flags,
                           const char *text, int textlen) {
    if (id <= 0 || id >= GV_GLON_MAX_NODES) { h->error = 1; return; }
    if (id == 1) {
        h->glon_to_view[1] = gv_view_root(h->view);
        if (h->glon_to_view[1] > 0 &&
            h->glon_to_view[1] < GV_GLON_MAX_VIEW_NODES) {
            h->view_to_glon[h->glon_to_view[1]] = 1;
        }
        gv_view_set_rect(h->view, h->glon_to_view[1], x, y, w, hh);
        return;
    }
    gv_node_id p = 0;
    if (parent > 0 && parent < GV_GLON_MAX_NODES) p = h->glon_to_view[parent];
    gv_node_id v = gv_view_add(h->view, p, gv_map_type(type));
    if (!v) { h->error = 1; return; }
    h->glon_to_view[id] = v;
    if (v > 0 && v < GV_GLON_MAX_VIEW_NODES) h->view_to_glon[v] = (gv_node_id)id;
    gv_view_set_rect(h->view, v, x, y, w, hh);
    gv_view_set_flags(h->view, v, 1, 1, (flags & 8) ? 1 : 0,
                      (flags & 1) ? 1 : 0);
    gv_view_set_fill(h->view, v, (flags & 2) ? 1 : 0, (flags & 4) ? 1 : 0);
    if (textlen > 0 && text) {
        char *buf = (char *)malloc((size_t)textlen + 1);
        if (buf) {
            memcpy(buf, text, (size_t)textlen);
            buf[textlen] = 0;
            gv_view_set_text(h->view, v, buf);
            free(buf);
        }
    }
}

static void gv_spec_draw(gv_glon_host *h, int id, int len, int *args, int *idx,
                         int count) {
    if (id <= 0 || id >= GV_GLON_MAX_NODES || len < 0) { h->error = 1; return; }
    gv_node_id v = h->glon_to_view[id];
    if (!v) return;
    gv_display_list dl;
    gv_display_list_init(&dl);
    int i = 0;
    while (i < len && *idx < count) {
        int op = gv_arg(args, idx, count, h);
        gv_cmd c;
        memset(&c, 0, sizeof c);
        if (op == 1) {
            c.op = GV_DRAW_RECT;
            c.rect.x = gv_arg(args, idx, count, h);
            c.rect.y = gv_arg(args, idx, count, h);
            c.rect.w = gv_arg(args, idx, count, h);
            c.rect.h = gv_arg(args, idx, count, h);
            c.color.r = (uint8_t)gv_arg(args, idx, count, h);
            c.color.g = (uint8_t)gv_arg(args, idx, count, h);
            c.color.b = (uint8_t)gv_arg(args, idx, count, h);
            c.color.a = (uint8_t)gv_arg(args, idx, count, h);
            gv_display_list_add(&dl, &c);
            i += 9;
        } else if (op == 2) {
            c.op = GV_DRAW_LINE;
            c.rect.x = gv_arg(args, idx, count, h);
            c.rect.y = gv_arg(args, idx, count, h);
            c.rect.w = gv_arg(args, idx, count, h);
            c.rect.h = gv_arg(args, idx, count, h);
            c.f1 = (double)gv_arg(args, idx, count, h);
            c.color.r = (uint8_t)gv_arg(args, idx, count, h);
            c.color.g = (uint8_t)gv_arg(args, idx, count, h);
            c.color.b = (uint8_t)gv_arg(args, idx, count, h);
            c.color.a = (uint8_t)gv_arg(args, idx, count, h);
            gv_display_list_add(&dl, &c);
            i += 10;
        } else {
            h->error = 1;
            break;
        }
    }
    gv_view_set_draw_list(h->view, v, &dl);
    gv_display_list_free(&dl);
}

static void gv_apply_spec(gv_glon_host *h, int *args, int count) {
    int idx = 0;
    while (idx < count) {
        int op = gv_arg(args, &idx, count, h);
        switch (op) {
        case 1: {
            int id = gv_arg(args, &idx, count, h);
            int parent = gv_arg(args, &idx, count, h);
            int type = gv_arg(args, &idx, count, h);
            int x = gv_arg(args, &idx, count, h);
            int y = gv_arg(args, &idx, count, h);
            int w = gv_arg(args, &idx, count, h);
            int hh = gv_arg(args, &idx, count, h);
            int flags = gv_arg(args, &idx, count, h);
            int textlen = gv_arg(args, &idx, count, h);
            char *text = NULL;
            if (textlen > 0) {
                text = (char *)malloc((size_t)textlen + 1);
                if (!text) { h->error = 1; return; }
                for (int i = 0; i < textlen; i++) text[i] = (char)gv_arg(args, &idx, count, h);
                text[textlen] = 0;
            }
            gv_spec_create(h, id, parent, type, x, y, w, hh, flags, text, textlen);
            free(text);
            break;
        }
        case 2: {
            int id = gv_arg(args, &idx, count, h);
            int dir = gv_arg(args, &idx, count, h);
            int align = gv_arg(args, &idx, count, h);
            int spacing = gv_arg(args, &idx, count, h);
            int padding = gv_arg(args, &idx, count, h);
            if (id > 0 && id < GV_GLON_MAX_NODES) {
                gv_node_id v = h->glon_to_view[id];
                if (v) gv_view_set_layout(h->view, v, dir, align, spacing, padding);
            }
            break;
        }
        case 3: {
            int id = gv_arg(args, &idx, count, h);
            gv_color bg, fg;
            bg.r = (uint8_t)gv_arg(args, &idx, count, h);
            bg.g = (uint8_t)gv_arg(args, &idx, count, h);
            bg.b = (uint8_t)gv_arg(args, &idx, count, h);
            bg.a = (uint8_t)gv_arg(args, &idx, count, h);
            fg.r = (uint8_t)gv_arg(args, &idx, count, h);
            fg.g = (uint8_t)gv_arg(args, &idx, count, h);
            fg.b = (uint8_t)gv_arg(args, &idx, count, h);
            fg.a = (uint8_t)gv_arg(args, &idx, count, h);
            if (id > 0 && id < GV_GLON_MAX_NODES) {
                gv_node_id v = h->glon_to_view[id];
                if (v) gv_view_set_colors(h->view, v, bg, fg);
            }
            break;
        }
        case 5: {
            int id = gv_arg(args, &idx, count, h);
            int size = gv_arg(args, &idx, count, h);
            if (id > 0 && id < GV_GLON_MAX_NODES) {
                gv_node_id v = h->glon_to_view[id];
                if (v) gv_view_set_font(h->view, v, (double)size);
            }
            break;
        }
        case 6: {
            int id = gv_arg(args, &idx, count, h);
            gv_transform t;
            t.a = (double)gv_arg(args, &idx, count, h) / 65536.0;
            t.b = (double)gv_arg(args, &idx, count, h) / 65536.0;
            t.c = (double)gv_arg(args, &idx, count, h) / 65536.0;
            t.d = (double)gv_arg(args, &idx, count, h) / 65536.0;
            t.e = (double)gv_arg(args, &idx, count, h) / 65536.0;
            t.f = (double)gv_arg(args, &idx, count, h) / 65536.0;
            if (id > 0 && id < GV_GLON_MAX_NODES) {
                gv_node_id v = h->glon_to_view[id];
                if (v) gv_view_set_transform(h->view, v, t);
            }
            break;
        }
        case 7: {
            int id = gv_arg(args, &idx, count, h);
            int len = gv_arg(args, &idx, count, h);
            gv_spec_draw(h, id, len, args, &idx, count);
            break;
        }
        case 8: {
            int id = gv_arg(args, &idx, count, h);
            int prop = gv_arg(args, &idx, count, h);
            int len = gv_arg(args, &idx, count, h);
            if (h->bind_count < GV_GLON_MAX_BINDS && len > 0 && len < 64) {
                gv_bind *b = &h->binds[h->bind_count++];
                b->id = id;
                b->prop = prop;
                for (int i = 0; i < len; i++) b->word[i] = (char)gv_arg(args, &idx, count, h);
                b->word[len] = 0;
            } else {
                for (int i = 0; i < len; i++) gv_arg(args, &idx, count, h);
            }
            break;
        }
        case 9: {
            int id = gv_arg(args, &idx, count, h);
            int parent = gv_arg(args, &idx, count, h);
            int x = gv_arg(args, &idx, count, h);
            int y = gv_arg(args, &idx, count, h);
            int w = gv_arg(args, &idx, count, h);
            int hh = gv_arg(args, &idx, count, h);
            int nrows = gv_arg(args, &idx, count, h);
            if (h->table_count >= GV_GLON_MAX_TABLES ||
                gv_glon_table(h, id) != NULL || nrows < 0) {
                h->error = 1;
                return;
            }
            gv_node_id p = 0;
            if (parent > 0 && parent < GV_GLON_MAX_NODES) p = h->glon_to_view[parent];
            gv_node_id v = gv_view_add(h->view, p, GV_NODE_CUSTOM);
            if (!v) { h->error = 1; return; }
            h->glon_to_view[id] = v;
            if (v > 0 && v < GV_GLON_MAX_VIEW_NODES)
                h->view_to_glon[v] = (gv_node_id)id;
            gv_view_set_rect(h->view, v, x, y, w, hh);
            gv_view_set_flags(h->view, v, 1, 1, 1, 1);
            gv_table *t = gv_table_create(id, nrows);
            if (!t) { h->error = 1; return; }
            h->tables[h->table_count++] = t;
            gv_glon_render_tables(h);
            break;
        }
        case 10: {
            int id = gv_arg(args, &idx, count, h);
            int cmd = gv_arg(args, &idx, count, h);
            int a = gv_arg(args, &idx, count, h);
            int b = gv_arg(args, &idx, count, h);
            gv_table *t = gv_glon_table(h, id);
            if (!t) { h->error = 1; return; }
            if (cmd == 2 || cmd == 4) {
                int len = gv_arg(args, &idx, count, h);
                char *text = (char *)malloc((size_t)len + 1);
                if (!text) { h->error = 1; return; }
                for (int i = 0; i < len; i++) text[i] = (char)gv_arg(args, &idx, count, h);
                text[len] = 0;
                int rc = gv_glon_host_table_command(h, id, cmd, a, b, text);
                free(text);
                if (rc != 0) { h->error = 1; return; }
            } else {
                if (gv_glon_host_table_command(h, id, cmd, a, b, NULL) != 0) {
                    h->error = 1;
                    return;
                }
            }
            break;
        }
        default:
            fprintf(stderr, "gv-glon: unknown spec op %d\n", op);
            h->error = 1;
            return;
        }
    }
}

static int gv_word_value(gv_glon_host *h, const char *word, char *buf, int cap) {
    char src[512];
    snprintf(src, sizeof src, "[ values [ %s ] ]", word);
    cell saved_lh = M[GC_LOADER_HP];
    int perr = 0;
    cell prog = r0_s1_parse(src, &perr);
    if (perr) { M[GC_LOADER_HP] = saved_lh; h->error = 1; return -1; }
    int n = r0_s1_run_persistent(prog);
    if (!r0_s1_ran_cleanly() || n < 1) { M[GC_LOADER_HP] = saved_lh; h->error = 1; return -1; }
    cell v = r0_s1_result(0, n);
    int len = r0_s1_mold(v, buf, cap - 1);
    M[GC_LOADER_HP] = saved_lh;
    if (len < 0) len = 0;
    if (len >= cap) len = cap - 1;
    buf[len] = 0;
    char *p = buf;
    if (*p == '"') p++;
    size_t blen = strlen(p);
    if (blen > 0 && p[blen - 1] == '"') p[blen - 1] = 0;
    memmove(buf, p, strlen(p) + 1);
    return 0;
}

static void gv_rebuild_display(gv_glon_host *h) {
    gv_view_layout(h->view);
    gv_display_list_free(&h->display);
    gv_display_list_init(&h->display);
    gv_view_build_display_list(h->view, &h->display);
}

static gv_table *gv_glon_table(gv_glon_host *h, int glon_id) {
    if (!h || glon_id <= 0 || glon_id >= GV_GLON_MAX_NODES) return NULL;
    for (int i = 0; i < h->table_count; i++) {
        if (h->tables[i] && h->tables[i]->id == glon_id) return h->tables[i];
    }
    return NULL;
}

static gv_table *gv_glon_table_new(gv_glon_host *h, int id, int parent,
                                   int x, int y, int w, int hh) {
    if (!h || id <= 0 || id >= GV_GLON_MAX_NODES || h->table_count >= GV_GLON_MAX_TABLES)
        return NULL;
    if (gv_glon_table(h, id)) return NULL;
    gv_node_id pid = 0;
    if (parent > 0 && parent < GV_GLON_MAX_NODES) pid = h->glon_to_view[parent];
    gv_node_id v = gv_view_add(h->view, pid, GV_NODE_CUSTOM);
    if (!v) return NULL;
    h->glon_to_view[id] = v;
    if (v > 0 && v < GV_GLON_MAX_VIEW_NODES) h->view_to_glon[v] = (gv_node_id)id;
    gv_view_set_rect(h->view, v, x, y, w, hh);
    gv_view_set_flags(h->view, v, 1, 1, 1, 1);
    gv_table *t = gv_table_create(id, 0);
    if (!t) return NULL;
    gv_table_set_columns(t, "");
    h->tables[h->table_count++] = t;
    return t;
}

static void gv_glon_table_command(gv_glon_host *h, const char *arg, int len) {
    char buf[4096];
    int n = len;
    if (n < 0) n = 0;
    if (n >= (int)sizeof buf) n = (int)sizeof buf - 1;
    memcpy(buf, arg, (size_t)n);
    buf[n] = 0;
    char *p = buf;
    char cmd[32];
    if (!gv_view_next_word(&p, cmd, sizeof cmd)) return;
    if (strcmp(cmd, "new") == 0) {
        int id, parent, x, y, w, hh;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &parent) ||
            !gv_view_next_int(&p, &x) || !gv_view_next_int(&p, &y) ||
            !gv_view_next_int(&p, &w) || !gv_view_next_int(&p, &hh)) { h->error = 1; return; }
        if (!gv_glon_table_new(h, id, parent, x, y, w, hh)) { h->error = 1; return; }
    } else {
        int id;
        char rest[3584];
        if (!gv_view_next_int(&p, &id)) { h->error = 1; return; }
        gv_table *t = gv_glon_table(h, id);
        if (!t) { h->error = 1; return; }
        gv_view_rest(&p, rest, sizeof rest);
        if (strcmp(cmd, "columns") == 0) {
            gv_table_set_columns(t, rest);
        } else if (strcmp(cmd, "rows") == 0) {
            gv_table_load_rows(t, rest);
        } else if (strcmp(cmd, "row") == 0) {
            gv_table_insert_row_text(t, t->view_count, rest);
        } else if (strcmp(cmd, "set-row") == 0) {
            int row = atoi(rest);
            char *q = rest;
            while (*q == ' ' || *q == '\t') q++;
            while (*q && *q != ' ' && *q != '\t') q++;
            char *text = q;
            while (*text == ' ' || *text == '\t') text++;
            char *copy = gv_host_strdup(text);
            if (copy) {
                int col = 0;
                char *field = gv_table_field(&copy, '|');
                while (field && col < t->ncols) {
                    gv_table_unescape(field);
                    gv_table_set_cell_inplace(t, row, col, field);
                    col++;
                    if (!*copy) break;
                    field = gv_table_field(&copy, '|');
                }
                free(copy);
            }
        } else if (strcmp(cmd, "set-cell") == 0) {
            int row = 0, col = 0;
            char *q = rest;
            gv_view_next_int(&q, &row);
            gv_view_next_int(&q, &col);
            char *text = q;
            while (*text == ' ') text++;
            gv_table_unescape(text);
            gv_table_set_cell_inplace(t, row, col, text);
        } else if (strcmp(cmd, "delete") == 0) {
            gv_table_delete_row(t, atoi(rest));
        } else if (strcmp(cmd, "clear") == 0) {
            gv_table_clear(t);
        } else if (strcmp(cmd, "sort") == 0) {
            int col = 0, dir = 1;
            char *q = rest;
            gv_view_next_int(&q, &col);
            gv_view_next_int(&q, &dir);
            gv_table_sort(t, col, dir);
        } else if (strcmp(cmd, "filter") == 0) {
            gv_table_filter(t, rest);
        } else if (strcmp(cmd, "select") == 0) {
            int row = atoi(rest);
            gv_table_select(t, row, 1);
        } else if (strcmp(cmd, "focus") == 0) {
            int row = 0, col = 0;
            char *q = rest;
            gv_view_next_int(&q, &row);
            gv_view_next_int(&q, &col);
            gv_table_focus(t, row, col);
        } else if (strcmp(cmd, "theme") == 0) {
            int v[32];
            int n = gv_parse_ints(rest, v, 32);
            if (n >= 9) {
                gv_color bg = {(uint8_t)v[0], (uint8_t)v[1], (uint8_t)v[2], (uint8_t)v[3]};
                gv_color panel = {(uint8_t)v[4], (uint8_t)v[5], (uint8_t)v[6], (uint8_t)v[7]};
                gv_color header = {(uint8_t)v[8], (uint8_t)v[9], (uint8_t)v[10], (uint8_t)v[11]};
                gv_color text = {(uint8_t)v[12], (uint8_t)v[13], (uint8_t)v[14], (uint8_t)v[15]};
                gv_color accent = {(uint8_t)v[16], (uint8_t)v[17], (uint8_t)v[18], (uint8_t)v[19]};
                gv_color border = {(uint8_t)v[20], (uint8_t)v[21], (uint8_t)v[22], (uint8_t)v[23]};
                gv_color row_alt = {(uint8_t)v[24], (uint8_t)v[25], (uint8_t)v[26], (uint8_t)v[27]};
                gv_table_set_theme(t, bg, panel, header, text, accent, border, row_alt, v[28]);
            }
        } else if (strcmp(cmd, "resize") == 0) {
            int col = 0, width = 0;
            char *q = rest;
            gv_view_next_int(&q, &col);
            gv_view_next_int(&q, &width);
            gv_table_resize_column(t, col, width);
        } else if (strcmp(cmd, "readonly") == 0) {
            int row = 0, col = 0, on = 0;
            char *q = rest;
            gv_view_next_int(&q, &row);
            gv_view_next_int(&q, &col);
            gv_view_next_int(&q, &on);
            gv_table_set_readonly(t, row, col, on);
        } else if (strcmp(cmd, "kind") == 0) {
            int row = 0, col = 0, kind = 0;
            char *q = rest;
            gv_view_next_int(&q, &row);
            gv_view_next_int(&q, &col);
            gv_view_next_int(&q, &kind);
            gv_table_set_cell_kind(t, row, col, kind);
        } else if (strcmp(cmd, "renderer") == 0) {
            int row = 0, col = 0, node = 0;
            char *q = rest;
            gv_view_next_int(&q, &row);
            gv_view_next_int(&q, &col);
            gv_view_next_int(&q, &node);
            gv_table_set_cell_renderer(t, row, col, node);
        } else if (strcmp(cmd, "edit") == 0) {
            int row = 0, col = 0;
            char *q = rest;
            gv_view_next_int(&q, &row);
            gv_view_next_int(&q, &col);
            char *text = q;
            while (*text == ' ') text++;
            gv_table_unescape(text);
            if (gv_table_begin_edit(t, row, col) == 0)
                gv_table_set_edit_text(t, text);
        } else if (strcmp(cmd, "edit-text") == 0) {
            gv_table_unescape(rest);
            gv_table_set_edit_text(t, rest);
        } else if (strcmp(cmd, "commit") == 0) {
            gv_table_commit_edit(t);
        } else if (strcmp(cmd, "cancel") == 0) {
            gv_table_cancel_edit(t);
        } else if (strcmp(cmd, "scroll") == 0) {
            gv_table_scroll(t, atoi(rest));
        } else if (strcmp(cmd, "batch") == 0) {
            gv_table_batch(t, rest);
        } else if (strcmp(cmd, "get") == 0) {
            int row = 0, col = 0, query = 0;
            char *q = rest;
            gv_view_next_int(&q, &row);
            gv_view_next_int(&q, &col);
            gv_view_next_int(&q, &query);
            if (h->table_query_count < 16) {
                int q = h->table_query_count++;
                h->table_query_q[q] = query;
                h->table_query_row[q] = row;
                h->table_query_col[q] = col;
                h->table_query_id = id;
            }
        } else if (strcmp(cmd, "status") == 0) {
            h->table_query_pending = 1;
            h->table_query_type = 2;
            h->table_query_id = id;
            h->view_query_id = atoi(rest);
        } else if (strcmp(cmd, "destroy") == 0) {
            gv_node_id v = h->glon_to_view[id];
            if (v) {
                if (v > 0 && v < GV_GLON_MAX_VIEW_NODES) h->view_to_glon[v] = 0;
                gv_view_destroy(h->view, v);
            }
            h->glon_to_view[id] = 0;
            for (int i = 0; i < h->table_count; i++) {
                if (h->tables[i] && h->tables[i]->id == id) {
                    gv_table_free(h->tables[i]);
                    for (int j = i; j < h->table_count - 1; j++)
                        h->tables[j] = h->tables[j + 1];
                    h->table_count--;
                    break;
                }
            }
            return;
        } else {
            h->error = 1;
            return;
        }
    }
    gv_glon_render_tables(h);
}

static void gv_glon_render_tables(gv_glon_host *h) {
    if (!h || !h->view) return;
    for (int i = 0; i < h->table_count; i++) {
        gv_table *t = h->tables[i];
        if (!t) continue;
        gv_node_id v = gv_glon_host_view_node(h, t->id);
        gv_node *n = gv_view_node(h->view, v);
        if (!n) continue;
        gv_table_render(h->view, v, t, (gv_rect){0, 0, n->w, n->h});
    }
}

int gv_glon_host_table(gv_glon_host *h, int glon_id, gv_table **out) {
    if (!h || !out) return -1;
    gv_table *t = gv_glon_table(h, glon_id);
    if (!t) return -1;
    *out = t;
    return 0;
}

int gv_glon_host_table_command(gv_glon_host *h, int glon_id, int cmd, int a,
                               int b, const char *text) {
    gv_table *t = gv_glon_table(h, glon_id);
    if (!t) return -1;
    switch (cmd) {
    case 1: gv_table_sort(t, a, b); break;
    case 2: gv_table_filter(t, text); break;
    case 3: gv_table_select(t, a, b != 0); break;
    case 4: gv_table_set_cell(t, a, b, text); break;
    case 5: gv_table_scroll(t, a); break;
    case 6: gv_table_select(t, a, b != 0); break;
    default: return -1;
    }
    gv_glon_render_tables(h);
    return 0;
}

int gv_glon_host_table_status(gv_glon_host *h, int glon_id, char *buf, int cap) {
    gv_table *t = gv_glon_table(h, glon_id);
    if (!t) return -1;
    return gv_table_status(t, buf, cap);
}

static int gv_glon_int_word(gv_glon_host *h, const char *word, int fallback) {
    char buf[64];
    if (gv_word_value(h, word, buf, sizeof buf) != 0) return fallback;
    return atoi(buf);
}

static void gv_glon_apply_table_policy(gv_glon_host *h) {
    if (!h || h->table_count <= 0) return;
    gv_table *t = h->tables[0];
    int col = gv_glon_int_word(h, "v/table-sort-col", h->table_sort_col);
    int dir = gv_glon_int_word(h, "v/table-sort-dir", h->table_sort_dir);
    int sel = gv_glon_int_word(h, "v/table-select-row", h->table_select_row);
    char filter[GV_TABLE_MAX_TEXT];
    if (gv_word_value(h, "v/table-filter", filter, sizeof filter) != 0)
        snprintf(filter, sizeof filter, "%s", h->table_filter);
    if (!h->table_policy_init) {
        h->table_sort_col = col;
        h->table_sort_dir = dir;
        h->table_select_row = sel;
        snprintf(h->table_filter, sizeof h->table_filter, "%s", filter);
        h->table_policy_init = 1;
        gv_glon_host_table_command(h, t->id, 1, col, dir, NULL);
        if (filter[0]) gv_glon_host_table_command(h, t->id, 2, 0, 0, filter);
        if (sel >= 0) gv_glon_host_table_command(h, t->id, 3, sel, 1, NULL);
        return;
    }
    if (col != h->table_sort_col || dir != h->table_sort_dir) {
        gv_glon_host_table_command(h, t->id, 1, col, dir, NULL);
        h->table_sort_col = col;
        h->table_sort_dir = dir;
    }
    if (strcmp(filter, h->table_filter) != 0) {
        gv_glon_host_table_command(h, t->id, 2, 0, 0, filter);
        snprintf(h->table_filter, sizeof h->table_filter, "%s", filter);
    }
    if (sel != h->table_select_row && sel >= 0) {
        gv_glon_host_table_command(h, t->id, 3, sel, 1, NULL);
        h->table_select_row = sel;
    }
}

static void gv_glon_send_pending_queries(gv_glon_host *h) {
    if (!h) return;
    if (h->clipboard_paste_pending) {
        char buf[4096];
        buf[0] = 0;
        if (h->backend && h->backend->get_clipboard &&
            h->backend->get_clipboard(h->backend, buf, sizeof buf) == 0 &&
            buf[0]) {
            gv_send_text(h, "view-char", buf);
        }
        h->clipboard_paste_pending = 0;
    }
    if (h->view_caps_pending) {
        char caps[256];
        snprintf(caps, sizeof caps, "headless");
        if (h->backend && h->backend->has_cap &&
            h->backend->has_cap(h->backend, GV_CAP_WINDOW))
            snprintf(caps, sizeof caps, "window,headless");
        h->view_caps_pending = 0;
        gv_send_text(h, "view-capabilities", caps);
    }
    if (h->table_query_count > 0) {
        gv_table *t = gv_glon_table(h, h->table_query_id);
        for (int i = 0; i < h->table_query_count; i++) {
            char out[1024];
            const char *cell = t ? gv_table_get_cell(t, h->table_query_row[i], h->table_query_col[i]) : NULL;
            snprintf(out, sizeof out, "%d|%s", h->table_query_q[i], cell ? cell : "");
            gv_send_text(h, "table-cell", out);
        }
        h->table_query_count = 0;
    }
    if (h->view_get_pending) {
        char out[1024];
        char value[512];
        gv_node *n = gv_view_node(h->view, h->glon_to_view[h->view_get_id]);
        value[0] = 0;
        if (n) {
            if (h->view_get_prop == 0 && n->text)
                snprintf(value, sizeof value, "%s", n->text);
            else if (h->view_get_prop == 1)
                snprintf(value, sizeof value, "%d %d %d %d", n->x, n->y, n->w, n->h);
            else if (h->view_get_prop == 2)
                snprintf(value, sizeof value, "%d %d %d %d", n->visible, n->enabled,
                         n->focusable, n->pickable);
            else if (h->view_get_prop == 3)
                snprintf(value, sizeof value, "%d %d %d %d %d %d %d %d",
                         n->bg.r, n->bg.g, n->bg.b, n->bg.a,
                         n->fg.r, n->fg.g, n->fg.b, n->fg.a);
        }
        snprintf(out, sizeof out, "%d|%d|%s", h->view_get_query,
                 h->view_get_prop, value);
        h->view_get_pending = 0;
        gv_send_text(h, "view-get", out);
    }
    if (h->view_query_pending) {
        char out[1024];
        int n = snprintf(out, sizeof out, "%d|", h->view_query_id);
        gv_node_id parent = 0;
        if (h->view_query_parent > 0 &&
            h->view_query_parent < GV_GLON_MAX_NODES)
            parent = h->glon_to_view[h->view_query_parent];
        gv_node *pn = gv_view_node(h->view, parent);
        int first = 1;
        for (gv_node_id c = pn ? pn->first_child : 0; c && n < (int)sizeof out - 16;
             c = gv_view_node(h->view, c) ? gv_view_node(h->view, c)->next_sibling : 0) {
            int gid = 0;
            if (c > 0 && c < GV_GLON_MAX_VIEW_NODES) gid = (int)h->view_to_glon[c];
            if (gid > 0) {
                n += snprintf(out + n, sizeof out - (size_t)n, "%s%d", first ? "" : ",", gid);
                first = 0;
            }
        }
        h->view_query_pending = 0;
        gv_send_text(h, "view-enumerate", out);
    }
}

int gv_glon_host_bind(gv_glon_host *h) {
    if (!h) return -1;
    for (int i = 0; i < h->bind_count; i++) {
        gv_bind *b = &h->binds[i];
        char val[GV_GLON_MAX_TEXT];
        if (gv_word_value(h, b->word, val, sizeof val) != 0) continue;
        if (b->id <= 0 || b->id >= GV_GLON_MAX_NODES) continue;
        gv_node_id v = h->glon_to_view[b->id];
        if (!v) continue;
        if (b->prop == 0) {
            const char *text = strcmp(b->word, "v/field-text") == 0 ? h->text : val;
            gv_view_set_text(h->view, v, text);
        } else if (b->prop == 1) {
            gv_node *node = gv_view_node(h->view, v);
            int on = atoi(val) != 0;
            gv_color bg = on ? (gv_color){70, 130, 200, 255}
                             : (gv_color){24, 26, 30, 255};
            gv_color fg = node ? node->fg : (gv_color){235, 238, 242, 255};
            gv_view_set_colors(h->view, v, bg, fg);
        }
    }
    gv_glon_apply_table_policy(h);
    gv_glon_render_tables(h);
    gv_glon_send_pending_queries(h);
    gv_rebuild_display(h);
    return 0;
}

int gv_glon_host_build(gv_glon_host *h) {
    if (!h) return -1;
    char path[2048];
    snprintf(path, sizeof path, "%s/view/glon/demo.spec", h->root);
    char *text = gv_read_file(path);
    if (!text) {
        fprintf(stderr, "gv-glon: cannot read %s\n", path);
        h->error = 1;
        return -1;
    }
    int args[GV_GLON_MAX_ARGS];
    int count = gv_parse_ints(text, args, GV_GLON_MAX_ARGS);
    free(text);
    if (count < 0) { h->error = 1; return -1; }
    gv_apply_spec(h, args, count);
    if (h->error) return -1;
    if (gv_word_value(h, "v/field-text", h->text, sizeof h->text) == 0) {
        h->text_len = strlen(h->text);
    }
    return gv_glon_host_bind(h);
}

void gv_glon_host_batch_stats(gv_glon_host *h, int *last, int *max) {
    if (!h) return;
    if (last) *last = h->last_batch_len;
    if (max) *max = h->max_batch_len;
}

int gv_glon_host_text(gv_glon_host *h, const char *word, char *buf, int cap) {
    if (!h) return -1;
    if (strcmp(word, "v/field-text") == 0) {
        snprintf(buf, (size_t)cap, "%s", h->text);
        return 0;
    }
    return gv_word_value(h, word, buf, cap);
}

static int gv_send_text(gv_glon_host *h, const char *token, const char *text) {
    char out[256]; int out_len = 0;
    h->take_pending = 0;
    int rc = r0_s1_g1a_live_event_bytes(token, (const unsigned char *)text,
                                        (unsigned int)strlen(text), out,
                                        sizeof out, &out_len);
    if (!r0_s1_ran_cleanly()) {
        cell t, i, a;
        int has_sin = r0_s1_uncaught_error(&t, &i, &a);
        char detail[512] = "";
        if (has_sin) {
            char ts[64], is[64], as[64];
            r0_s1_mold(t, ts, sizeof ts);
            r0_s1_mold(i, is, sizeof is);
            r0_s1_mold(a, as, sizeof as);
            snprintf(detail, sizeof detail, " sin=%s/%s/%s", ts, is, as);
        }
        fprintf(stderr, "gv-glon: event %s rc=%d clean=%d halt=%d%s\n", token,
                rc, r0_s1_ran_cleanly(), r0_s1_halt_reason(), detail);
        h->error = 1; return -1;
    }
    if (h->take_pending && f1b_take(h) != 0) return -1;   /* F1B */
    return 0;
}

int gv_glon_host_wheel(gv_glon_host *h, int x, int y, int dx, int dy) {
    if (!h) return -1;
    int best_gid = 0;
    long best_area = 0;
    for (gv_node_id v = 1; v < GV_GLON_MAX_VIEW_NODES; v++) {
        gv_node *n = gv_view_node(h->view, v);
        if (!n || n->type != GV_NODE_VIEWPORT || !n->visible) continue;
        gv_rect sr = {0, 0, 0, 0};
        if (gv_view_screen_rect(h->view, v, &sr) != 0) continue;
        if (x < sr.x || y < sr.y || x >= sr.x + sr.w || y >= sr.y + sr.h) continue;
        int gid = (int)h->view_to_glon[v];
        if (gid <= 0) continue;
        long area = (long)sr.w * sr.h;
        if (!best_gid || area < best_area) {
            best_gid = gid;
            best_area = area;
        }
    }
    if (best_gid > 0) {
        char value[96];
        snprintf(value, sizeof value, "%d|%d|%d", best_gid, dx, dy);
        if (gv_send_text(h, "view-wheel", value) != 0) return -1;
        return gv_glon_host_bind(h);
    }
    return 0;
}

int gv_glon_host_click(gv_glon_host *h, int glon_id) {
    if (!h) return -1;
    char ids[16];
    snprintf(ids, sizeof ids, "%d", glon_id);
    if (gv_send_text(h, "view-click", ids) != 0) return -1;
    return gv_glon_host_bind(h);
}

int gv_glon_host_click_xy(gv_glon_host *h, int glon_id, int x, int y) {
    if (!h) return -1;
    gv_table *t = gv_glon_table(h, glon_id);
    if (t) {
        gv_node_id v = h->glon_to_view[glon_id];
        gv_rect sr = {0, 0, 0, 0};
        if (gv_view_screen_rect(h->view, v, &sr) == 0) {
            int row = gv_table_row_at(t, y - sr.y);
            int col = gv_table_column_at(t, x - sr.x);
            char payload[40];
            snprintf(payload, sizeof payload, "%04d %04d %04d", glon_id, row, col);
            if (gv_send_text(h, "table-click", payload) != 0) return -1;
            return gv_glon_host_bind(h);
        }
    }
    return gv_glon_host_click(h, glon_id);
}

int gv_glon_host_pointer_move(gv_glon_host *h, int glon_id) {
    if (!h) return -1;
    char ids[16];
    snprintf(ids, sizeof ids, "%d", glon_id);
    if (gv_send_text(h, "view-pointer-move", ids) != 0) return -1;
    return gv_glon_host_bind(h);
}

int gv_glon_host_pointer_down(gv_glon_host *h, int glon_id, int x, int y) {
    (void)x; (void)y;
    if (!h) return -1;
    char ids[16];
    snprintf(ids, sizeof ids, "%d", glon_id);
    if (gv_send_text(h, "view-pointer-down", ids) != 0) return -1;
    return gv_glon_host_bind(h);
}

int gv_glon_host_pointer_up(gv_glon_host *h, int glon_id, int x, int y) {
    (void)x; (void)y;
    if (!h) return -1;
    char ids[16];
    snprintf(ids, sizeof ids, "%d", glon_id);
    if (gv_send_text(h, "view-pointer-up", ids) != 0) return -1;
    return gv_glon_host_bind(h);
}

int gv_glon_host_focus(gv_glon_host *h, int glon_id) {
    if (!h) return -1;
    char ids[16];
    snprintf(ids, sizeof ids, "%d", glon_id);
    if (gv_send_text(h, "view-focus", ids) != 0) return -1;
    return gv_glon_host_bind(h);
}

int gv_glon_host_blur(gv_glon_host *h, int glon_id) {
    if (!h) return -1;
    char ids[16];
    snprintf(ids, sizeof ids, "%d", glon_id);
    if (gv_send_text(h, "view-blur", ids) != 0) return -1;
    return gv_glon_host_bind(h);
}

int gv_glon_host_char(gv_glon_host *h, const char *text) {
    if (!h || !text) return -1;
    size_t used = h->text_len;
    size_t add = strlen(text);
    if (used + add >= sizeof h->text) return -1;
    memcpy(h->text + used, text, add + 1);
    h->text_len = used + add;
    if (gv_send_text(h, "view-set-text", h->text) != 0) return -1;
    return gv_glon_host_bind(h);
}

int gv_glon_host_backspace(gv_glon_host *h) {
    if (!h) return -1;
    if (h->text_len == 0) return 0;
    h->text[--h->text_len] = 0;
    if (gv_send_text(h, "view-set-text", h->text) != 0) return -1;
    return gv_glon_host_bind(h);
}

int gv_glon_host_key_mods(gv_glon_host *h, int key, unsigned int mods) {
    if (!h) return -1;
    int ctrl = (mods & 0x40) || (mods & 0x400);
    int shift = (mods & 0x1) || (mods & 0x2);
    if (ctrl) {
        if (key == 97 || key == 65) key = 1011;
        else if (key == 99 || key == 67) key = 1012;
        else if (key == 120 || key == 88) key = 1013;
        else if (key == 118 || key == 86) key = 1014;
    } else if (shift) {
        if (key == 1073741904) key = 1015;
        else if (key == 1073741903) key = 1016;
        else if (key == 1073741898) key = 1017;
        else if (key == 1073741901) key = 1018;
    } else {
        if (key == 1073741904) key = 1001;
        else if (key == 1073741903) key = 1002;
        else if (key == 1073741898) key = 1003;
        else if (key == 1073741901) key = 1004;
        else if (key == 1073741899) key = 1021;
        else if (key == 1073741902) key = 1022;
    }
    char ids[16];
    snprintf(ids, sizeof ids, "%d", key);
    if (gv_send_text(h, "view-key", ids) != 0) return -1;
    return gv_glon_host_bind(h);
}

int gv_glon_host_key(gv_glon_host *h, int key) {
    return gv_glon_host_key_mods(h, key, 0);
}

int gv_glon_host_char_event(gv_glon_host *h, const char *text) {
    if (!h || !text) return -1;
    if (gv_send_text(h, "view-char", text) != 0) return -1;
    return gv_glon_host_bind(h);
}

int gv_glon_host_tick(gv_glon_host *h) {
    if (!h) return -1;
    if (gv_send_text(h, "view-tick", "") != 0) return -1;
    return gv_glon_host_bind(h);
}

int gv_glon_host_resize(gv_glon_host *h, int width, int height) {
    if (!h || width <= 0 || height <= 0) return -1;
    gv_view_resize(h->view, width, height);
    gv_view_layout(h->view);
    char payload[32];
    snprintf(payload, sizeof payload, "%d %d", width, height);
    if (gv_send_text(h, "view-resize", payload) != 0) return -1;
    return gv_glon_host_bind(h);
}

int gv_glon_host_pointer_cancel(gv_glon_host *h, int glon_id) {
    if (!h) return -1;
    char ids[16];
    snprintf(ids, sizeof ids, "%d", glon_id);
    if (gv_send_text(h, "view-pointer-cancel", ids) != 0) return -1;
    return gv_glon_host_bind(h);
}

int gv_glon_host_error(gv_glon_host *h) { return h ? h->error : 1; }
const gv_display_list *gv_glon_host_display(gv_glon_host *h) { return h ? &h->display : NULL; }
gv_node_id gv_glon_host_view_node(gv_glon_host *h, int glon_id) {
    if (!h || glon_id <= 0 || glon_id >= GV_GLON_MAX_NODES) return 0;
    return h->glon_to_view[glon_id];
}
int gv_glon_host_glon_id(gv_glon_host *h, gv_node_id view_node) {
    if (!h || view_node <= 0 || view_node >= GV_GLON_MAX_VIEW_NODES) return 0;
    return (int)h->view_to_glon[view_node];
}