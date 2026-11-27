#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include "gv_glon_host.h"
#include "r0_s1.h"
#include "r0_s1_g1a_live.h"
#include "gv_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GLON-INST-3A: the Glon-id -> View-node and View-node -> Glon-id tables
 * grow on demand (doubling, from 1,024 / 2,048 entries) up to
 * GV_GLON_ID_LIMIT ids each: at most 256 KB a table.  A node id past the
 * limit is refused deterministically (as a fixed 1,024 was).  Intent's id
 * allocator stops one below it (intent/max-node). */
#define GV_GLON_ID_LIMIT 65536
#define GV_GLON_MAX_NODES (h->g2v_cap)
#define GV_GLON_MAX_VIEW_NODES (h->v2g_cap)
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
static void gv_glon_release_subtree(gv_glon_host *h, gv_node_id v);
static void gv_glon_host_command(gv_glon_host *h, char *p);
static void gv_glon_hosts_release(gv_glon_host *h, int gid);
static void gv_glon_sync_hosts(gv_glon_host *h);
static void gv_glon_capture(gv_glon_host *h, int on);
static int gv_glon_table_wrap(void *ctx, const char *s, int len, double size, int width,
                              int *starts, int *lens, int max) {
    gv_backend *b = (gv_backend *)ctx;
    return b->wrap_text(b, s, len, size, width, starts, lens, max);
}
static int gv_glon_table_measure(void *ctx, const char *s, int len, double size) {
    gv_backend *b = (gv_backend *)ctx;
    return b->measure_text(b, s, len, size);
}
static void gv_rebuild_display(gv_glon_host *h);
static int gv_send_text(gv_glon_host *h, const char *token, const char *text);
static void gv_glon_windows_close_all(gv_glon_host *h);
static void gv_glon_window_enter(gv_glon_host *h, int w);
static void gv_glon_window_command(gv_glon_host *h, const char *cmd, char *p);
static void gv_glon_focus_any(gv_glon_host *h, gv_node_id v);
static void gv_glon_windows_check_focus(gv_glon_host *h);
static int gv_glon_win_by_gid(gv_glon_host *h, int gid);
static void gv_glon_window_close(gv_glon_host *h, int idx);
static int gv_glon_enter_node_window(gv_glon_host *h, gv_node_id v);
/* GUI-P2 measurement: a -DR0_S1_PROFILE runtime resets its counters at the
 * start of every run, so the host adds them up after each run.  Compiled to
 * nothing in an ordinary build. */
static long gv_pf_allocs, gv_pf_cells, gv_pf_steps;
static void gv_pf_after_run(void) {
#ifdef R0_S1_PROFILE
    r0_s1_pf_stats s;
    r0_s1_pf_read(&s);
    gv_pf_allocs += s.allocs;
    gv_pf_cells += s.alloc_cells;
    gv_pf_steps += s.blkeval;
#endif
}
/* INTENT-7: per-event timing (always compiled; two clock reads an event).
 * For each event name: how many Glon runs, the time in the Glon run itself
 * (evaluation, its allocations and collections), the time applying the
 * batch the run left (f1b_take: the View and table commands, the file
 * facility) and the collections during the run.  Read with
 * gv_glon_host_timing_report; zeroed by gv_glon_host_timing_reset. */
#define GV_TIMING_MAX 48
typedef struct { char name[32]; long runs, gcs; double glon_ms, take_ms; } gv_timing_row;
static gv_timing_row gv_timing[GV_TIMING_MAX];
static int gv_timing_n;
static double gv_timing_bind_ms, gv_timing_deliver_ms;
static long gv_timing_binds, gv_timing_delivers;
static double gv_timing_now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}
static gv_timing_row *gv_timing_row_for(const char *name) {
    for (int i = 0; i < gv_timing_n; i++) if (!strcmp(gv_timing[i].name, name)) return &gv_timing[i];
    if (gv_timing_n >= GV_TIMING_MAX) return &gv_timing[GV_TIMING_MAX - 1];
    gv_timing_row *r = &gv_timing[gv_timing_n++];
    memset(r, 0, sizeof *r);
    snprintf(r->name, sizeof r->name, "%s", name);
    return r;
}
void gv_glon_host_timing_reset(void) {
    gv_timing_n = 0; gv_timing_bind_ms = gv_timing_deliver_ms = 0; gv_timing_binds = gv_timing_delivers = 0;
}
void gv_glon_host_timing_totals(double *glon_ms, double *take_ms, double *bind_ms, long *runs, long *gcs) {
    double g = 0, t = 0; long n = 0, c = 0;
    for (int i = 0; i < gv_timing_n; i++) { g += gv_timing[i].glon_ms; t += gv_timing[i].take_ms; n += gv_timing[i].runs; c += gv_timing[i].gcs; }
    if (glon_ms) *glon_ms = g;
    if (take_ms) *take_ms = t;
    if (bind_ms) *bind_ms = gv_timing_bind_ms + gv_timing_deliver_ms;
    if (runs) *runs = n;
    if (gcs) *gcs = c;
}
void gv_glon_host_timing_report(FILE *out, const char *indent) {
    for (int i = 0; i < gv_timing_n; i++) {
        gv_timing_row *r = &gv_timing[i];
        fprintf(out, "%s%-18s %5ld runs  Glon %8.1f ms  batch %7.1f ms  %ld GCs\n", indent, r->name, r->runs, r->glon_ms, r->take_ms, r->gcs);
    }
    if (gv_timing_binds || gv_timing_delivers)
        fprintf(out, "%s%-18s %5ld binds %6.1f ms, %ld delivers %6.1f ms\n", indent, "(host)", gv_timing_binds, gv_timing_bind_ms,
                gv_timing_delivers, gv_timing_deliver_ms);
}
/* interpreter block-evaluation steps summed over every run (profile builds) */
long gv_glon_host_pf_steps(void) { return gv_pf_steps; }
void gv_glon_host_pf_totals(long *allocs, long *cells) {
    if (allocs) *allocs = gv_pf_allocs;
    if (cells) *cells = gv_pf_cells;
}
static int gv_view_next_int(char **p, int *out);
static int gv_view_next_word(char **p, char *buf, int cap);
static void gv_view_rest(char **p, char *buf, int cap);

struct gv_glon_host {
    gv_view *view;
    gv_backend *backend;
    int error;
    gv_display_list display;
    gv_node_id *glon_to_view; int g2v_cap;   /* grow: gv_glon_room */
    gv_node_id *view_to_glon; int v2g_cap;
    /* GLON-INST-3B: secondary native windows (struct gv_glon_win); while one
     * is current (cur_win >= 0) the first window's root, focus, size, hover
     * and backend wait in prim; reported_win: the window Glon last heard of
     * (view-window), -2 unknown */
    struct gv_glon_win *wins; int nwins; int cur_win; int reported_win;
    struct { gv_node_id root, focus; int width, height, last_move_gid; gv_backend *backend; } prim;
    int heap_watch;             /* view heap-watch 1: view-heap after collections */
    long heap_reported_gc;      /* the collection count last reported */
    int win_failed_gid[8]; char win_failed_why[8][16]; int win_failed_n;   /* window-failed to send */
    long win_dropped;           /* events for a window closed before they were dispatched */
    long win_opened, win_closed, win_refused;
    const gv_window_ops *wops;  /* how secondary windows are made (NULL: headless) */
    int windows_lib;            /* load view/glon/intent_windows.glon after intent.glon */
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
    int take_pending;
    int ref_chunks;
    int ref_takes;
    int ref_errors;
    int view_caps_pending;
    int view_query_pending;
    int view_query_id;
    int view_query_parent;
    int view_get_pending;
    /* INTENT-6: application text files below the root (gv_glon_file_command) */
    char file_path[2048];       /* the file being written (file begin) */
    char *file_buf;             /* its lines so far */
    size_t file_len, file_cap;
    int file_overflow;
    int file_saved_q;           /* a commit to answer (file-saved), or 0 */
    char file_saved[300];
    int file_read_q;            /* a read to answer (file-line ..., file-done), or 0 */
    int next_q;                 /* INTENT-6: "next Q" asked: Glon hears "next" "Q" after the batch */
    char file_read_path[2048];
    int blur_pending_gid;       /* GUI-P2: focus dropped by hiding; tell Glon */
    int scroll_report_id;       /* GUI-P2: a table paged by command; tell Glon */
    /* FOREIGN-1: hosted foreign components, one per View node (gv_host.h) */
    struct gv_hosted {
        struct gv_glon_host *h;
        int gid, gen;                       /* gen: bumped when the slot is freed */
        const gv_host_provider *p;
        void *inst;                         /* NULL: free, or creation failed */
        gv_rect bounds, clip;
        int visible, focused, placed;
    } hosted[16];
    /* events emitted by components, delivered after pump (never inside a
     * provider call); an entry whose slot generation moved on is dropped */
    struct gv_host_event { int slot, gen, gid; char name[24]; char detail[256]; } hq[64];
    int hq_n;
    int focus_how;              /* FOREIGN-2: how the next focus arrives (GV_HOST_FOCUS_*) */
    int col_drag_gid;           /* GUI-P2: table whose column divider is dragged */
    int col_drag_col, col_drag_x0, col_drag_w0;
    char col_drag_before[GV_TABLE_MAX_COLS * 16];   /* widths at the press */
    int press_clicks;           /* GUI-P2: clicks of the press being handled (2 = double) */
    int cursor;                 /* GUI-P2: pointer shape asked of the backend */
    int drag_table_gid;         /* GUI-P2: table whose scrollbar thumb is dragged */
    int drag_off;               /* pointer y minus thumb top at the press */
    int last_move_gid;          /* runner: Glon id under the pointer at the last move */
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

/* GLON-INST-3B: a secondary native window: an extra window root in the one
 * View (so the id tables and every command stay shared) with its own
 * backend (an SDL window of the same SDL session), size, focus, hover and
 * display list. */
#define GV_GLON_MAX_WINDOWS 16
#define GV_GLON_WIN_SLOTS (2 * GV_GLON_MAX_WINDOWS)   /* with room for refused ("ghost") ones */
struct gv_glon_win {
    int gid;                    /* the Glon id of its root */
    gv_node_id root;
    gv_backend *backend;
    int width, height;
    gv_node_id focus;           /* its View focus while another window is current */
    int last_move_gid;
    int force;                  /* an event reached it: draw it again */
    gv_display_list display;
    long frames;
};
/* GLON-INST-3A: make index id usable in the table (*t, *cap), doubling it;
 * 0 when it is, -1 past GV_GLON_ID_LIMIT or out of memory */
static int gv_glon_room(gv_node_id **t, int *cap, int id) {
    if (id <= 0 || id >= GV_GLON_ID_LIMIT) return -1;
    if (id < *cap) return 0;
    int n = *cap ? *cap : 1024;
    while (n <= id) n *= 2;
    if (n > GV_GLON_ID_LIMIT) n = GV_GLON_ID_LIMIT;
    gv_node_id *p = (gv_node_id *)realloc(*t, (size_t)n * sizeof(gv_node_id));
    if (!p) return -1;
    memset(p + *cap, 0, (size_t)(n - *cap) * sizeof(gv_node_id));
    *t = p; *cap = n;
    return 0;
}
#define GV_G2V_ROOM(h, id) gv_glon_room(&(h)->glon_to_view, &(h)->g2v_cap, (int)(id))
#define GV_V2G_ROOM(h, v) gv_glon_room(&(h)->view_to_glon, &(h)->v2g_cap, (int)(v))
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
    gv_pf_after_run();
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
    /* tables in text mode wrap with the backend's text shaping if it has it */
    if (backend && backend->wrap_text) gv_table_set_wrapper(gv_glon_table_wrap, backend);
    /* column auto-fit measures with it too */
    if (backend && backend->measure_text) gv_table_set_measure(gv_glon_table_measure, backend);
    if (GV_G2V_ROOM(h, 1023) != 0 || GV_V2G_ROOM(h, 2047) != 0) { free(h->glon_to_view); free(h->view_to_glon); free(h); return NULL; }
    if (view) {
        h->glon_to_view[1] = gv_view_root(view);
        if (h->glon_to_view[1] > 0 && GV_V2G_ROOM(h, h->glon_to_view[1]) == 0)
            h->view_to_glon[h->glon_to_view[1]] = 1;
    }
    h->cur_win = -1;
    h->reported_win = -1;
    h->heap_reported_gc = -1;
    h->wins = (struct gv_glon_win *)calloc(GV_GLON_WIN_SLOTS, sizeof *h->wins);
    if (!h->wins) { free(h->glon_to_view); free(h->view_to_glon); free(h); return NULL; }
    r0_s1_g1a_live_set_host_call(gv_glon_host_call, h);
    return h;
}

void gv_glon_host_free(gv_glon_host *h) {
    if (!h) return;
    /* a drag in progress gives its capture back */
    if (h->backend && h->backend->set_capture && (h->drag_table_gid || h->col_drag_gid))
        h->backend->set_capture(h->backend, 0);
    for (int i = 0; i < h->table_count; i++) gv_table_free(h->tables[i]);
    for (int i = 0; i < 16; i++) if (h->hosted[i].gid) gv_glon_hosts_release(h, h->hosted[i].gid);
    for (int i = 0; i < h->draw_text_count; i++) free(h->draw_texts[i]);
    free(h->draw_texts);
    free(h->file_buf);
    gv_display_list_free(&h->display);
    gv_glon_windows_close_all(h);
    free(h->wins);
    free(h->glon_to_view);
    free(h->view_to_glon);
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

/* INTENT-4B-5A: View's image decoder is the backend's */
static int gv_glon_host_decode(void *ctx, const char *path, uint8_t **pixels, int *w, int *hh, char *err, int cap) {
    gv_glon_host *h = (gv_glon_host *)ctx;
    return h->backend->decode_image(h->backend, path, pixels, w, hh, err, cap);
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
    if (strncmp(cmd, "window-", 7) == 0 || strcmp(cmd, "heap-watch") == 0) { gv_glon_window_command(h, cmd, p); return; }
    if (strcmp(cmd, "new") == 0) {
        int id, parent, type;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &parent) ||
            !gv_view_next_int(&p, &type)) { h->error = 1; return; }
        if (GV_G2V_ROOM(h, id) != 0) {
            fprintf(stderr, "gv-glon: new %d refused: outside 1..%d\n", id, GV_GLON_ID_LIMIT - 1);
            h->error = 1; return;
        }
        if (id == 1) return;
        if (h->glon_to_view[id]) {
            fprintf(stderr, "gv-glon: create %d refused: id is still live\n", id);
            h->error = 1; return;
        }
        gv_node_id pid = 0;
        if (parent > 0 && parent < GV_GLON_MAX_NODES) pid = h->glon_to_view[parent];
        gv_node_id v = gv_view_add(h->view, pid, gv_map_type(type));
        if (!v) {
            fprintf(stderr, "gv-glon: create %d refused: parent %d is not live\n", id, parent);
            h->error = 1; return;
        }
        h->glon_to_view[id] = v;
        if (v > 0 && GV_V2G_ROOM(h, v) == 0) h->view_to_glon[v] = (gv_node_id)id;
    } else if (strcmp(cmd, "create") == 0) {
        int id, parent, type, x, y, w, hh, vis, en, focus, pick;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &parent) ||
            !gv_view_next_int(&p, &type) || !gv_view_next_int(&p, &x) ||
            !gv_view_next_int(&p, &y) || !gv_view_next_int(&p, &w) ||
            !gv_view_next_int(&p, &hh) || !gv_view_next_int(&p, &vis) ||
            !gv_view_next_int(&p, &en) || !gv_view_next_int(&p, &focus) ||
            !gv_view_next_int(&p, &pick)) return;
        if (GV_G2V_ROOM(h, id) != 0) {
            fprintf(stderr, "gv-glon: create %d refused: outside 1..%d\n", id, GV_GLON_ID_LIMIT - 1);
            h->error = 1; return;
        }
        if (id == 1) { gv_view_set_rect(h->view, h->glon_to_view[1], x, y, w, hh); return; }
        if (h->glon_to_view[id]) {
            fprintf(stderr, "gv-glon: create %d refused: id is still live\n", id);
            h->error = 1; return;
        }
        gv_node_id pid = 0;
        if (parent > 0 && parent < GV_GLON_MAX_NODES) pid = h->glon_to_view[parent];
        gv_node_id v = gv_view_add(h->view, pid, gv_map_type(type));
        if (!v) {
            fprintf(stderr, "gv-glon: create %d refused: parent %d is not live\n", id, parent);
            h->error = 1; return;
        }
        h->glon_to_view[id] = v;
        if (v > 0 && GV_V2G_ROOM(h, v) == 0) h->view_to_glon[v] = (gv_node_id)id;
        gv_view_set_rect(h->view, v, x, y, w, hh);
        gv_view_set_flags(h->view, v, vis, en, focus, pick);
    } else if (strcmp(cmd, "host") == 0) {
        gv_glon_host_command(h, p);
    } else if (strcmp(cmd, "destroy") == 0) {
        int id;
        if (!gv_view_next_int(&p, &id)) return;
        /* GLON-INST-3B: a secondary window's root destroyed is that window closed */
        if (h->nwins && gv_glon_win_by_gid(h, id) >= 0) { gv_glon_window_close(h, gv_glon_win_by_gid(h, id)); return; }
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            if (v) {
                gv_glon_release_subtree(h, v);
                gv_view_destroy(h->view, v);
                h->last_move_gid = -1;      /* ids may be reused: re-send hover */
                if (h->nwins) gv_glon_windows_check_focus(h);
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
            if (v) {
                gv_node_id before = gv_view_focus_id(h->view);
                gv_view_set_flags(h->view, v, vis, en, focus, pick);
                /* GUI-P2: hiding a subtree that holds the keyboard focus drops
                 * the focus (keys must not reach an invisible widget); the
                 * application hears view-blur for that node at the next bind */
                if (!vis) {
                    gv_node_id had = gv_view_drop_focus_within(h->view, v);
                    /* INTENT-2: hiding the focused node itself: View's
                     * set_flags clears the focus on its own, so the drop
                     * above finds none; the blur is still owed */
                    if (!had && before == v && !gv_view_focus_id(h->view)) {
                        had = before;
                        gv_node *fn = gv_view_node(h->view, v);
                        if (fn) fn->has_focus = 0;
                    }
                    if (had && had < GV_GLON_MAX_VIEW_NODES && h->view_to_glon[had])
                        h->blur_pending_gid = (int)h->view_to_glon[had];
                }
            }
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
    } else if (strcmp(cmd, "caret") == 0) {
        /* GUI-P2: "caret id index" (index < 0: none); INTENT-4B-2: an
         * optional anchor ("caret id index anchor") selects anchor..index */
        int id, index, anchor = -1;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &index)) return;
        int has_anchor = gv_view_next_int(&p, &anchor);
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            if (v) {
                if (has_anchor) gv_view_set_selection(h->view, v, index, anchor);
                else gv_view_set_caret(h->view, v, index);
            }
        }
    } else if (strcmp(cmd, "edit") == 0) {
        /* INTENT-4B-2: "edit id 1|0": an editable text node (clipped to its
         * box and scrolled so the caret stays visible) */
        int id, on;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &on)) return;
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            if (v) gv_view_set_edit(h->view, v, on);
        }
    } else if (strcmp(cmd, "text-exact") == 0) {
        /* INTENT-4B-2: "text-exact id TEXT": TEXT is every byte after the one
         * space that follows the id -- not trimmed, unlike "text" -- and 0x1F
         * stands for a newline (a command line cannot hold one).  Editable
         * texts need it: their caret indexes count every byte. */
        int id;
        if (!gv_view_next_int(&p, &id)) return;
        if (*p == ' ') p++;
        char text[4096];
        int n = 0;
        while (p[n] && n < (int)sizeof text - 1) { text[n] = p[n] == 0x1F ? '\n' : p[n]; n++; }
        text[n] = 0;
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            if (v) gv_view_set_text(h->view, v, text);
        }
    } else if (strcmp(cmd, "text-align") == 0) {
        int id, align;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &align)) return;
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            if (v) gv_view_set_text_align(h->view, v, align);
        }
    } else if (strcmp(cmd, "radius") == 0 || strcmp(cmd, "font-style") == 0) {
        /* INTENT-4B-5A: "radius id px" (a rect's corners), "font-style id
         * bits" (bit 0 bold; bits 1-2 family: 0 sans, 1 serif, 2 monospace) */
        int id, val;
        if (!gv_view_next_int(&p, &id) || !gv_view_next_int(&p, &val)) return;
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            if (v) {
                if (cmd[0] == 'r') gv_view_set_radius(h->view, v, val);
                else gv_view_set_font_style(h->view, v, val);
            }
        }
    } else if (strcmp(cmd, "image") == 0) {
        /* INTENT-4B-5A: "image id PATH" -- PATH (every byte after the space
         * that follows the id; relative to the application root) shown in
         * image node id, decoded by the backend once per path.  A file that
         * cannot be shown leaves a placeholder saying why, and a line on
         * stderr. */
        int id;
        if (!gv_view_next_int(&p, &id)) return;
        if (*p == ' ') p++;
        char full[2048], err[512];
        if (p[0] == '/' || !h->root[0]) snprintf(full, sizeof full, "%s", p);
        else snprintf(full, sizeof full, "%s/%s", h->root, p);
        if (id > 0 && id < GV_GLON_MAX_NODES) {
            gv_node_id v = h->glon_to_view[id];
            if (v && gv_view_set_image(h->view, v, full, h->backend && h->backend->decode_image ? gv_glon_host_decode : NULL,
                                       h, err, (int)sizeof err) != 0)
                fprintf(stderr, "[image] node %d: %s (%s)\n", id, err, full);
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
            /* GLON-INST-3B: a node of another window takes that window's focus */
            if (v) { if (h->nwins) gv_glon_focus_any(h, v); else gv_view_focus(h->view, v); }
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


/* ---- INTENT-6: application text files ---------------------------------------
 * A deliberately small facility so that an application can keep its data:
 * whole text files of lines, below the application root only (a relative
 * path, no "..", no leading "/"), 16 MB at most.
 *   file begin PATH     start a new file in memory
 *   file line TEXT      append one line (0x1F in TEXT stands for a newline:
 *                       vb/s-esc writes it, and the file keeps it)
 *   file commit Q       write it: PATH.tmp, then renamed over PATH; Glon
 *                       hears "file-saved" "Q|ok|bytes" or "Q|error|reason"
 *   file read Q PATH    Glon hears "file-line" "Q|N|text" for every line
 *                       (0x1F given back as a newline, a trailing \r
 *                       dropped), then "file-done" "Q|lines" or
 *                       "Q|-1|reason"
 * Nothing is read or written anywhere else; there is no delete, list or
 * append-to-existing. */
#define GV_FILE_MAX (16u * 1024u * 1024u)
static int gv_file_path(gv_glon_host *h, const char *rel, char *out, size_t cap) {
    if (!rel[0] || rel[0] == '/' || strstr(rel, "..") || strlen(rel) > 900) return -1;
    if (strlen(h->root) + strlen(rel) + 2 > cap) return -1;
    snprintf(out, cap, "%s/%s", h->root[0] ? h->root : ".", rel);
    return 0;
}
static void gv_glon_file_command(gv_glon_host *h, const char *arg, int len) {
    char cmd[16]; int k = 0;
    while (k < len && k < 15 && arg[k] != ' ') { cmd[k] = arg[k]; k++; }
    cmd[k] = 0;
    const char *rest = k < len ? arg + k + 1 : arg + len;
    int rlen = (int)(len - (rest - arg));
    if (rlen < 0) rlen = 0;
    if (!strcmp(cmd, "begin")) {
        char rel[1024]; snprintf(rel, sizeof rel, "%.*s", rlen, rest);
        if (gv_file_path(h, rel, h->file_path, sizeof h->file_path) != 0) {
            fprintf(stderr, "gv-glon: file %s refused (outside the root)\n", rel);
            h->file_path[0] = 0;
        }
        h->file_len = 0; h->file_overflow = 0;
    } else if (!strcmp(cmd, "line")) {
        size_t need = h->file_len + (size_t)rlen + 1;
        if (need > GV_FILE_MAX) { h->file_overflow = 1; return; }
        if (need > h->file_cap) {
            size_t cap = h->file_cap ? h->file_cap : 65536;
            while (cap < need) cap *= 2;
            char *nb = realloc(h->file_buf, cap);
            if (!nb) { h->file_overflow = 1; return; }
            h->file_buf = nb; h->file_cap = cap;
        }
        memcpy(h->file_buf + h->file_len, rest, (size_t)rlen);
        h->file_len += (size_t)rlen;
        h->file_buf[h->file_len++] = '\n';
    } else if (!strcmp(cmd, "commit")) {
        h->file_saved_q = atoi(rest);
        if (!h->file_path[0]) snprintf(h->file_saved, sizeof h->file_saved, "error|no file begun (or refused)");
        else if (h->file_overflow) snprintf(h->file_saved, sizeof h->file_saved, "error|larger than %u bytes", GV_FILE_MAX);
        else {
            char tmp[2100]; snprintf(tmp, sizeof tmp, "%s.tmp", h->file_path);
            FILE *f = fopen(tmp, "wb");
            int ok = f && fwrite(h->file_buf ? h->file_buf : "", 1, h->file_len, f) == h->file_len;
            if (f && fclose(f) != 0) ok = 0;
            if (ok && rename(tmp, h->file_path) != 0) ok = 0;
            if (ok) snprintf(h->file_saved, sizeof h->file_saved, "ok|%zu", h->file_len);
            else snprintf(h->file_saved, sizeof h->file_saved, "error|cannot write the file");
        }
        if (h->file_saved_q <= 0) h->file_saved_q = 1;
    } else if (!strcmp(cmd, "read")) {
        int q = atoi(rest);
        const char *p = rest;
        while (p < arg + len && *p != ' ') p++;
        if (p < arg + len) p++;
        char rel[1024]; snprintf(rel, sizeof rel, "%.*s", (int)(arg + len - p), p);
        h->file_read_q = q > 0 ? q : 1;
        if (gv_file_path(h, rel, h->file_read_path, sizeof h->file_read_path) != 0) {
            fprintf(stderr, "gv-glon: file %s refused (outside the root)\n", rel);
            h->file_read_path[0] = 0;
        }
    }
}
/* the answers, after the batch that asked */
static void gv_glon_file_answers_once(gv_glon_host *h);
/* the answers, and the steps asked for with "next", until none is pending
 * (at most 100,000 steps: a runaway loop ends) */
static void gv_glon_file_answers(gv_glon_host *h) {
    for (int guard = 0; guard < 100000 && !h->error && (h->file_saved_q || h->file_read_q || h->next_q); guard++) {
        if (h->file_saved_q || h->file_read_q) { gv_glon_file_answers_once(h); continue; }
        char q[24];
        snprintf(q, sizeof q, "%d", h->next_q);
        h->next_q = 0;
        if (gv_send_text(h, "next", q) != 0) break;
    }
}
static void gv_glon_file_answers_once(gv_glon_host *h) {
    char head[400];
    if (h->file_saved_q) {
        snprintf(head, sizeof head, "%d|%s", h->file_saved_q, h->file_saved);
        h->file_saved_q = 0;
        gv_send_text(h, "file-saved", head);
    }
    if (h->file_read_q) {
        int q = h->file_read_q;
        h->file_read_q = 0;
        char *data = h->file_read_path[0] ? gv_read_file(h->file_read_path) : NULL;
        if (!data) {
            snprintf(head, sizeof head, "%d|-1|%s", q, h->file_read_path[0] ? "cannot read the file" : "refused (outside the root)");
            gv_send_text(h, "file-done", head);
            return;
        }
        size_t total = strlen(data);
        if (total > GV_FILE_MAX) {
            free(data);
            snprintf(head, sizeof head, "%d|-1|larger than %u bytes", q, GV_FILE_MAX);
            gv_send_text(h, "file-done", head);
            return;
        }
        int n = 0;
        char *line = data;
        char *out = NULL; size_t outcap = 0;
        while (*line) {
            char *end = strchr(line, '\n');
            size_t l = end ? (size_t)(end - line) : strlen(line);
            if (l && line[l - 1] == '\r') l--;
            if (outcap < l + 32) { outcap = l + 64; char *nb = realloc(out, outcap); if (!nb) break; out = nb; }
            int hl = snprintf(out, outcap, "%d|%d|", q, ++n);
            for (size_t i = 0; i < l; i++) out[hl + i] = line[i] == 0x1f ? '\n' : line[i];
            out[hl + l] = 0;
            if (gv_send_text(h, "file-line", out) != 0) break;
            if (!end) break;
            line = end + 1;
        }
        free(out);
        free(data);
        snprintf(head, sizeof head, "%d|%d", q, n);
        gv_send_text(h, "file-done", head);
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
        else if (strncmp(line, "file ", 5) == 0)
            gv_glon_file_command(h, line + 5, (int)strlen(line + 5));
        else if (strncmp(line, "next ", 5) == 0) {
            /* INTENT-6: continue in a new event.  One event's batch is built
             * in the managed heap and must stay small (INTENT-6.md 7), so an
             * application splits long work (rows, file lines) into steps;
             * after this batch Glon hears "next" "Q" and does the next one */
            h->next_q = atoi(line + 5);
            if (h->next_q <= 0) h->next_q = 1;
        }
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
        h->take_pending = 1;
}

static int f1b_is(cell v, int tag) { return r0_tag(v) == tag; }

#define F1B_MAX_CHUNKS 1024
#define F1B_MAX_BYTES  (1L << 20)
static int f1b_take(gv_glon_host *h) {
    h->take_pending = 0;
    h->ref_takes++;
    cell saved_lh = M[GC_LOADER_HP];
    int perr = 0;
    cell prog = r0_s1_parse("[ widget/take ]", &perr);
    int n = perr ? 0 : r0_s1_run_persistent(prog);
    gv_pf_after_run();
    M[GC_LOADER_HP] = saved_lh;
    if (perr || n < 1 || !r0_s1_ran_cleanly()) { h->ref_errors++; return -1; }
    cell v = r0_s1_result(0, n);
    static cell flushes[F1B_MAX_CHUNKS], chunks[F1B_MAX_CHUNKS];
    int nf = 0, nc = 0;
    long bytes = 0;
    while (!(r0_tag(v) == 0 && v == 0)) {
        if (!f1b_is(v, T_BLOCK) || M[r0_untag(v)] != 2 || nf >= F1B_MAX_CHUNKS) goto bad;
        flushes[nf++] = M[r0_untag(v) + BLK_DATA];
        v = M[r0_untag(v) + BLK_DATA + 1];
    }
    for (int f = nf - 1; f >= 0; f--) {
        int first = nc;
        cell w = flushes[f];
        while (w != 0) {
            if (!f1b_is(w, T_BLOCK) || M[r0_untag(w)] != 2 || nc >= F1B_MAX_CHUNKS) goto bad;
            cell sv = M[r0_untag(w) + BLK_DATA];
            if (!f1b_is(sv, T_STRING)) goto bad;
            cell len = M[r0_untag(sv)];
            if (len < 0 || len >= 4096) goto bad; /* one command line max */
            if (len > 0 && M[r0_untag(sv) + len] != '\n') goto bad;
            bytes += len;
            if (bytes > F1B_MAX_BYTES) goto bad;
            if (len > 0) chunks[nc++] = sv;
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
            if (byte <= 0 || byte > 255) goto bad;
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

void gv_glon_host_node_tables(gv_glon_host *h, int *g2v_cap, int *v2g_cap, int *mapped, int *view_cap) {
    if (!h) return;
    if (g2v_cap) *g2v_cap = h->g2v_cap;
    if (v2g_cap) *v2g_cap = h->v2g_cap;
    if (mapped) { int n = 0; for (int i = 1; i < h->g2v_cap; i++) n += h->glon_to_view[i] != 0; *mapped = n; }
    if (view_cap) *view_cap = h->view ? gv_view_node_capacity(h->view) : 0;
}
void gv_glon_host_f1b_stats(gv_glon_host *h, int *takes, int *chunks, int *errors) {
    if (takes) *takes = h->ref_takes;
    if (chunks) *chunks = h->ref_chunks;
    if (errors) *errors = h->ref_errors;
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

/* INTENT-4B-3: the desktop VID loader, with or without the Intent dialect
 * (edit_core.glon, vid_edit.glon, intent.glon).  A session holds at most
 * SITE_PARENT_CAP (640) func literals; the Intent dialect is about 350 of
 * them, so a machine-VID application that does not use it (the gallery)
 * loads without it and keeps the room it had before INTENT-1. */
static int gv_glon_host_load_desktop_vid(gv_glon_host *h, const char *root,
                                         const char *app_path, int with_intent) {
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
        /* GLON-LIB-1: value-tag and block-set! (for vid_look.glon, intent.glon
         * and list.glon), then, in Intent sessions only, the collection and
         * text vocabulary */
        "view/glon/core_words.glon",
        "demo/shop/list.glon",
        "demo/shop/strings-text.glon",
        "view/glon/vid_table.glon",
        "view/glon/vid_table_data.glon",
        "view/glon/theme_lib.glon",
        "view/glon/vocab_lib.glon",
        "view/glon/vid_widgets.glon",
        "view/glon/vid_nav.glon",
        "view/glon/vid_fields.glon",
        "view/glon/vid_forms.glon",
        "view/glon/vid_layout.glon",
        "view/glon/vid_caret.glon",
        "view/glon/vid_tabs.glon",
        /* INTENT-4B-1: checkbox / radio focus and disabled looks */
        "view/glon/vid_check.glon",
        "view/glon/int_text_cache.glon",
        "view/glon/view_cmd.glon",
        /* INTENT-4B-5A: looks (derived themes), corner radius, font style,
         * images (after view_cmd.glon, whose command buffer it writes) */
        "view/glon/vid_look.glon",
        /* INTENT-4B-2: the one text-editing core, and editable VID fields
         * (after view_cmd.glon, whose command buffer they write) */
        "view/glon/edit_core.glon",
        "view/glon/vid_edit.glon",
        /* INTENT-1: the declarative dialect over machine VID (defines
         * view, layout, the intent words and a default do-event that an
         * application replaces) */
        "view/glon/intent.glon"
    };
    char path[2048];
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        if (!with_intent && (strstr(files[i], "edit_core.glon") || strstr(files[i], "vid_edit.glon") ||
                             strstr(files[i], "intent.glon") || strstr(files[i], "list.glon") ||
                             strstr(files[i], "strings-text.glon")))
            continue;
        if (root && root[0]) snprintf(path, sizeof path, "%s/%s", root, files[i]);
        else snprintf(path, sizeof path, "%s", files[i]);
        if (gv_load_file(h, path) != 0) return -1;
    }
    /* GLON-INST-3B: the window words, only for a session that opens windows */
    if (with_intent && h->windows_lib) {
        if (root && root[0]) snprintf(path, sizeof path, "%s/view/glon/intent_windows.glon", root);
        else snprintf(path, sizeof path, "view/glon/intent_windows.glon");
        if (gv_load_file(h, path) != 0) return -1;
    }
    if (app_path[0] == '/') snprintf(path, sizeof path, "%s", app_path);
    else if (root && root[0]) snprintf(path, sizeof path, "%s/%s", root, app_path);
    else snprintf(path, sizeof path, "%s", app_path);
    return gv_load_file(h, path);
}

int gv_glon_host_load_vocab_vid_app(gv_glon_host *h, const char *root,
                                    const char *app_path) {
    return gv_glon_host_load_desktop_vid(h, root, app_path, 1);
}

int gv_glon_host_load_machine_vid_app(gv_glon_host *h, const char *root,
                                      const char *app_path) {
    return gv_glon_host_load_desktop_vid(h, root, app_path, 0);
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
        /* INTENT-4B-2: GUI-4's editing operations, shared with VID fields */
        "view/glon/edit_core.glon",
        "view/glon/gui3_lib.glon",
        "view/glon/int_text_cache.glon",
        "view/glon/view_cmd.glon"
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

/* GUI-3 behaviour with VID presentation (view/glon/vid_gui3.glon): the GUI-3
 * stack plus the WG-3 theme and the VID widget primitives. */
int gv_glon_host_load_vid_gui3_app(gv_glon_host *h, const char *root,
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
        /* INTENT-4B-2: GUI-4's editing operations, shared with VID fields */
        "view/glon/edit_core.glon",
        "view/glon/gui3_lib.glon",
        "view/glon/theme_lib.glon",
        "view/glon/vid_widgets.glon",
        "view/glon/vid_gui3.glon",
        "view/glon/int_text_cache.glon",
        "view/glon/view_cmd.glon"
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
    gv_pf_after_run();
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

/* A run's widget/flush batch-ref host call is not seen by the host, so its
 * View commands wait for the next event's take; this delivers them now (the
 * run's own result, if any, must be read first). */
int gv_glon_host_deliver(gv_glon_host *h) {
    if (!h) return -1;
    double t0 = gv_timing_now_ms();
    int rc = f1b_take(h);
    /* GUI-P2: the commands may have destroyed nodes; the display list must
     * not keep pointing at their freed text (a frame drawn from it showed
     * garbage and made Pango warn about invalid UTF-8) */
    gv_rebuild_display(h);
    gv_glon_sync_hosts(h);
    gv_timing_delivers++; gv_timing_deliver_ms += gv_timing_now_ms() - t0;
    return rc;
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
    if (GV_G2V_ROOM(h, id) != 0) { h->error = 1; return; }
    if (id == 1) {
        h->glon_to_view[1] = gv_view_root(h->view);
        if (h->glon_to_view[1] > 0 && GV_V2G_ROOM(h, h->glon_to_view[1]) == 0)
            h->view_to_glon[h->glon_to_view[1]] = 1;
        gv_view_set_rect(h->view, h->glon_to_view[1], x, y, w, hh);
        return;
    }
    gv_node_id p = 0;
    if (parent > 0 && parent < GV_GLON_MAX_NODES) p = h->glon_to_view[parent];
    gv_node_id v = gv_view_add(h->view, p, gv_map_type(type));
    if (!v) { h->error = 1; return; }
    h->glon_to_view[id] = v;
    if (v > 0 && GV_V2G_ROOM(h, v) == 0) h->view_to_glon[v] = (gv_node_id)id;
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
            if (h->table_count >= GV_GLON_MAX_TABLES || GV_G2V_ROOM(h, id) != 0 ||
                gv_glon_table(h, id) != NULL || nrows < 0) {
                h->error = 1;
                return;
            }
            gv_node_id p = 0;
            if (parent > 0 && parent < GV_GLON_MAX_NODES) p = h->glon_to_view[parent];
            gv_node_id v = gv_view_add(h->view, p, GV_NODE_CUSTOM);
            if (!v) { h->error = 1; return; }
            h->glon_to_view[id] = v;
            if (v > 0 && GV_V2G_ROOM(h, v) == 0)
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
    gv_pf_after_run();
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
    int was = h->cur_win;
    if (was >= 0) gv_glon_window_enter(h, -1);   /* GLON-INST-3B: the first window's list */
    gv_view_layout(h->view);
    gv_display_list_free(&h->display);
    gv_display_list_init(&h->display);
    gv_view_build_display_list(h->view, &h->display);
    if (was >= 0) gv_glon_window_enter(h, was);
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
    if (!h || GV_G2V_ROOM(h, id) != 0 || h->table_count >= GV_GLON_MAX_TABLES)
        return NULL;
    if (gv_glon_table(h, id)) return NULL;
    gv_node_id pid = 0;
    if (parent > 0 && parent < GV_GLON_MAX_NODES) pid = h->glon_to_view[parent];
    gv_node_id v = gv_view_add(h->view, pid, GV_NODE_CUSTOM);
    if (!v) return NULL;
    h->glon_to_view[id] = v;
    if (v > 0 && GV_V2G_ROOM(h, v) == 0) h->view_to_glon[v] = (gv_node_id)id;
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
            /* INTENT-6: gv_table_field moves its cursor; the copy is freed from
             * where it began (it was freed from the cursor: an invalid free on
             * the first set-row ever sent) */
            char *cur = copy;
            if (copy) {
                int col = 0;
                char *field = gv_table_field(&cur, '|');
                while (field && col < t->ncols) {
                    gv_table_unescape(field);
                    gv_table_set_cell_inplace(t, row, col, field);
                    col++;
                    if (!*cur) break;
                    field = gv_table_field(&cur, '|');
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
        } else if (strcmp(cmd, "widths") == 0) {
            /* GUI-P2: requested column widths, as table-columns reports them */
            gv_table_apply_widths(t, rest);
        } else if (strcmp(cmd, "col-width") == 0) {
            /* col-width id c w */
            char *q = rest;
            int c = (int)strtol(q, &q, 10);
            gv_table_set_col_width(t, c, (int)strtol(q, &q, 10));
        } else if (strcmp(cmd, "text-reset") == 0) {
            /* GUI-P2 text mode: one wrapped document ("text-reset id size") */
            gv_table_text_reset(t, atof(rest));
        } else if (strcmp(cmd, "text-chunk") == 0) {
            /* the bytes after the id and one space, untrimmed; 0x1F = newline */
            const char *raw = arg;
            int k = 0;
            while (k < len && arg[k] == ' ') k++;
            while (k < len && arg[k] != ' ') k++;     /* "text-chunk" */
            if (k < len) k++;
            while (k < len && arg[k] != ' ') k++;     /* the id */
            if (k < len) k++;                         /* one space */
            gv_table_text_append(t, raw + k, len - k, 1);
        } else if (strcmp(cmd, "text-file") == 0) {
            /* a file below the application root: relative, no "..", 2 MB at most */
            if (rest[0] && rest[0] != '/' && !strstr(rest, "..")) {
                char path[8192];
                snprintf(path, sizeof path, "%s/%s", h->root, rest);
                char *data = gv_read_file(path);
                if (data) {
                    int n = (int)strlen(data);
                    if (n <= 2 * 1024 * 1024) gv_table_text_append(t, data, n, 0);
                    free(data);
                } else {
                    fprintf(stderr, "gv-glon: text-file %s: cannot read\n", rest);
                }
            } else {
                fprintf(stderr, "gv-glon: text-file %s refused (outside the root)\n", rest);
            }
        } else if (strcmp(cmd, "page") == 0) {
            /* one page up (-1) or down (1); Glon hears table-scroll after */
            gv_table_scroll(t, (atoi(rest) < 0 ? -1 : 1) * gv_table_page_rows(t));
            h->scroll_report_id = id;
        } else if (strcmp(cmd, "text-top") == 0) {
            /* the row holding this byte of the document at the top */
            gv_table_text_top(t, atoi(rest));
        } else if (strcmp(cmd, "deselect") == 0) {
            /* GUI-P2: "select" only ever adds a row; these take rows away */
            gv_table_select(t, atoi(rest), 0);
        } else if (strcmp(cmd, "clear-selection") == 0) {
            gv_table_clear_selection(t);
        } else if (strcmp(cmd, "header") == 0) {
            /* INTENT-4B-3: "header id 0|1": without its header row (a list) */
            t->header_h = atoi(rest) ? 28 : 0;
            t->dirty = 1;
        } else if (strcmp(cmd, "focus") == 0) {
            int row = 0, col = 0;
            char *q = rest;
            gv_view_next_int(&q, &row);
            gv_view_next_int(&q, &col);
            /* row < 0: no focused cell (GUI-P2; it used to be ignored) */
            if (row < 0) { t->focus_active = 0; t->dirty = 1; }
            else gv_table_focus(t, row, col);
        } else if (strcmp(cmd, "theme") == 0) {
            int v[40];          /* INTENT-7: 33 with the selection colour */
            int n = gv_parse_ints(rest, v, 40);
            if (n >= 9) {
                gv_color bg = {(uint8_t)v[0], (uint8_t)v[1], (uint8_t)v[2], (uint8_t)v[3]};
                gv_color panel = {(uint8_t)v[4], (uint8_t)v[5], (uint8_t)v[6], (uint8_t)v[7]};
                gv_color header = {(uint8_t)v[8], (uint8_t)v[9], (uint8_t)v[10], (uint8_t)v[11]};
                gv_color text = {(uint8_t)v[12], (uint8_t)v[13], (uint8_t)v[14], (uint8_t)v[15]};
                gv_color accent = {(uint8_t)v[16], (uint8_t)v[17], (uint8_t)v[18], (uint8_t)v[19]};
                gv_color border = {(uint8_t)v[20], (uint8_t)v[21], (uint8_t)v[22], (uint8_t)v[23]};
                gv_color row_alt = {(uint8_t)v[24], (uint8_t)v[25], (uint8_t)v[26], (uint8_t)v[27]};
                gv_table_set_theme(t, bg, panel, header, text, accent, border, row_alt, v[28]);
                /* INTENT-7: an optional ninth colour, the selected rows' */
                if (n >= 33) gv_table_set_selection_color(t, (gv_color){(uint8_t)v[29], (uint8_t)v[30], (uint8_t)v[31], (uint8_t)v[32]});
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
    /* the first table that holds rows (a text-mode table has none) */
    gv_table *t = NULL;
    for (int i = 0; i < h->table_count && !t; i++)
        if (h->tables[i] && !h->tables[i]->text_mode) t = h->tables[i];
    if (!t) return;
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

static void gv_glon_file_answers(gv_glon_host *h);
static void gv_glon_send_pending_queries(gv_glon_host *h) {
    if (!h) return;
    if (h->file_saved_q || h->file_read_q || h->next_q) gv_glon_file_answers(h);
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
        int get_was = h->nwins ? gv_glon_enter_node_window(h, h->glon_to_view[h->view_get_id]) : -3;
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
            else if (h->view_get_prop == 4) {
                /* GUI-4B2C: the node's rectangle in the content coordinates
                 * of its nearest enclosing viewport: "vp x y w h" */
                gv_node_id vp = n->parent;
                while (vp) {
                    gv_node *pn = gv_view_node(h->view, vp);
                    if (!pn || pn->type == GV_NODE_VIEWPORT) break;
                    vp = pn->parent;
                }
                gv_node *vn = gv_view_node(h->view, vp);
                gv_rect nr, vr;
                if (h->view->layout_dirty) gv_view_layout(h->view);
                if (vn && vp < GV_GLON_MAX_VIEW_NODES &&
                    gv_view_screen_rect(h->view, h->glon_to_view[h->view_get_id], &nr) == 0 &&
                    gv_view_screen_rect(h->view, vp, &vr) == 0)
                    snprintf(value, sizeof value, "%d %d %d %d %d", (int)h->view_to_glon[vp],
                             nr.x - vr.x + vn->cam_x, nr.y - vr.y + vn->cam_y, nr.w, nr.h);
            }
            else if (h->view_get_prop == 3)
                snprintf(value, sizeof value, "%d %d %d %d %d %d %d %d",
                         n->bg.r, n->bg.g, n->bg.b, n->bg.a,
                         n->fg.r, n->fg.g, n->fg.b, n->fg.a);
        }
        snprintf(out, sizeof out, "%d|%d|%s", h->view_get_query,
                 h->view_get_prop, value);
        if (get_was != -3) gv_glon_window_enter(h, get_was);
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

static int gv_glon_host_bind_inner(gv_glon_host *h);
int gv_glon_host_bind(gv_glon_host *h) {
    double t0 = gv_timing_now_ms();
    int rc = gv_glon_host_bind_inner(h);
    gv_timing_binds++; gv_timing_bind_ms += gv_timing_now_ms() - t0;
    return rc;
}
static int gv_glon_host_bind_inner(gv_glon_host *h) {
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
    for (int k = 0; k < 8 && h->view_get_pending; k++)
        gv_glon_send_pending_queries(h);
    if (h->blur_pending_gid) {
        char ids[16];
        snprintf(ids, sizeof ids, "%d", h->blur_pending_gid);
        h->blur_pending_gid = 0;
        gv_send_text(h, "view-blur", ids);
    }
    if (h->scroll_report_id) {
        gv_table *t = gv_glon_table(h, h->scroll_report_id);
        h->scroll_report_id = 0;
        if (t) {
            char payload[48];
            snprintf(payload, sizeof payload, "%d %d %d", t->id, t->visible_start, gv_table_text_top_byte(t));
            gv_send_text(h, "table-scroll", payload);
        }
    }
    /* GLON-INST-3B: windows the host could not open, and the heap's room
     * (after each collection, once asked for: view heap-watch 1) */
    while (h->win_failed_n > 0) {
        char payload[48];
        int k = --h->win_failed_n;
        snprintf(payload, sizeof payload, "%d %s", h->win_failed_gid[k], h->win_failed_why[k]);
        gv_send_text(h, "window-failed", payload);
    }
    if (h->heap_watch && r0_s1_gc_count() != h->heap_reported_gc) {
        char payload[48];
        long fr = 0, total = 0;
        h->heap_reported_gc = r0_s1_gc_count();
        gv_glon_host_heap_room(&fr, &total);
        snprintf(payload, sizeof payload, "%ld %ld", fr, total);
        gv_send_text(h, "view-heap", payload);
    }
    gv_rebuild_display(h);
    gv_glon_sync_hosts(h);
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
    if (getenv("GV_TRACE_EVENTS")) {
        /* INTENT-4B-2: typed text (a password among it) is not traced unless
         * asked for: view-char and the legacy view-set-text show their length */
        if (!getenv("GV_TRACE_TEXT") && (!strcmp(token, "view-char") || !strcmp(token, "view-set-text")))
            fprintf(stderr, "[event] %s (%d bytes)\n", token, (int)strlen(text));
        else
            fprintf(stderr, "[event] %s %s\n", token, text);
    }
    h->take_pending = 0;
    gv_timing_row *tr = gv_timing_row_for(token);
    long tr_gc0 = r0_s1_gc_count();
    double tr_t0 = gv_timing_now_ms();
    int rc = r0_s1_g1a_live_event_bytes(token, (const unsigned char *)text,
                                        (unsigned int)strlen(text), out,
                                        sizeof out, &out_len);
    double tr_t1 = gv_timing_now_ms();
#ifdef R0S1_GC_PROF
    M[REG_IP] = 0;      /* diagnostic build: a sampled IP of 0 means no S1 is running */
#endif
    tr->runs++; tr->glon_ms += tr_t1 - tr_t0; tr->gcs += r0_s1_gc_count() - tr_gc0;
    gv_pf_after_run();
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
    if (h->take_pending) {
        double t2 = gv_timing_now_ms();
        int trc = f1b_take(h);
        tr->take_ms += gv_timing_now_ms() - t2;
        if (trc != 0) return -1;
    }
    return 0;
}

/* GUI-P2: a table's scroll position changed in the host (wheel, scrollbar):
 * redraw and tell the application, "table-scroll id first-row top-byte"
 * (top-byte: in text mode the document byte at the top, else 0) */
static int gv_glon_table_scrolled(gv_glon_host *h, gv_table *t) {
    gv_glon_render_tables(h);
    char payload[48];
    snprintf(payload, sizeof payload, "%d %d %d", t->id, t->visible_start, gv_table_text_top_byte(t));
    if (gv_send_text(h, "table-scroll", payload) != 0) return -1;
    return gv_glon_host_bind(h);
}

int gv_glon_host_wheel(gv_glon_host *h, int x, int y, int dx, int dy) {
    if (!h) return -1;
    if (h->view->layout_dirty) gv_view_layout(h->view);
    /* GUI-P2: a table under the pointer takes a vertical wheel first (3 rows
     * a step); at its first or last row the wheel goes on to the viewports */
    for (int i = 0; dy && i < h->table_count; i++) {
        gv_table *t = h->tables[i];
        if (!t || t->id <= 0 || t->id >= GV_GLON_MAX_NODES || !h->glon_to_view[t->id]) continue;
        gv_rect tr = {0, 0, 0, 0};
        if (gv_view_visible_rect(h->view, h->glon_to_view[t->id], &tr) != 0) continue;
        if (x < tr.x || y < tr.y || x >= tr.x + tr.w || y >= tr.y + tr.h) continue;
        int before = t->visible_start;
        gv_table_scroll(t, dy * 3);
        if (t->visible_start != before) return gv_glon_table_scrolled(h, t);
        break;
    }
    int best_gid = 0;
    long best_area = 0;
    for (gv_node_id v = 1; v < GV_GLON_MAX_VIEW_NODES; v++) {
        gv_node *n = gv_view_node(h->view, v);
        if (!n || n->type != GV_NODE_VIEWPORT || !n->visible) continue;
        /* only the part of the viewport that is shown takes the wheel: a
         * viewport scrolled or clipped out of an ancestor never does */
        gv_rect sr = {0, 0, 0, 0};
        if (gv_view_visible_rect(h->view, v, &sr) != 0) continue;
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

/* Destroying a View node destroys its whole subtree, so every Glon id in the
 * subtree is released, not only the root's.  Otherwise a released View slot
 * that View hands out again would still be reachable through a stale Glon
 * id, and the Glon id could never be created again. */
static void gv_glon_release_subtree(gv_glon_host *h, gv_node_id v) {
    gv_node *n = gv_view_node(h->view, v);
    if (!n) return;
    for (gv_node_id c = n->first_child; c;) {
        gv_node *cn = gv_view_node(h->view, c);
        if (!cn) break;
        gv_node_id next = cn->next_sibling;
        gv_glon_release_subtree(h, c);
        c = next;
    }
    if (v > 0 && v < GV_GLON_MAX_VIEW_NODES) {
        int gid = (int)h->view_to_glon[v];
        if (gid > 0 && gid < GV_GLON_MAX_NODES && h->glon_to_view[gid] == v) {
            gv_glon_hosts_release(h, gid);
            h->glon_to_view[gid] = 0;
            /* GLON-INST-3A: no host state keeps a released id, so Intent may
             * give the id to a new node at once and no later event names
             * the old one (a blur owed, a table being dragged, a report) */
            if (h->blur_pending_gid == gid) h->blur_pending_gid = 0;
            if (h->scroll_report_id == gid) h->scroll_report_id = 0;
            if (h->drag_table_gid == gid || h->col_drag_gid == gid) {
                if (h->backend && h->backend->set_capture) h->backend->set_capture(h->backend, 0);
                if (h->drag_table_gid == gid) h->drag_table_gid = 0;
                if (h->col_drag_gid == gid) h->col_drag_gid = 0;
            }
            if (h->last_move_gid == gid) h->last_move_gid = -1;
            for (int i = 0; i < h->table_count; i++) {
                if (h->tables[i] && h->tables[i]->id == gid) {
                    gv_table_free(h->tables[i]);
                    for (int j = i; j < h->table_count - 1; j++) h->tables[j] = h->tables[j + 1];
                    h->table_count--;
                    break;
                }
            }
        }
        h->view_to_glon[v] = 0;
    }
}

/* Pointer input at window coordinates, dispatched exactly as the SDL runner
 * does: hit-test the View (clip- and scroll-aware), then send move / down +
 * focus + click / up to the Glon node that was hit.  kind 0 move, 1 down,
 * 2 up.  *hit_gid (optional) receives the Glon id hit, 0 for none. */
/* GUI-P2: the pointer while a table's scrollbar thumb is held: moves (from
 * anywhere, inside the table or not) place the thumb; the release ends it.
 * No row is clicked.  Answers 1 when it took the event. */
static int gv_glon_table_drag(gv_glon_host *h, int kind, int y) {
    if (!h->drag_table_gid) return 0;
    gv_table *t = gv_glon_table(h, h->drag_table_gid);
    gv_node_id v = h->drag_table_gid < GV_GLON_MAX_NODES ? h->glon_to_view[h->drag_table_gid] : 0;
    gv_rect sr = {0, 0, 0, 0};
    if (!t || !v || gv_view_screen_rect(h->view, v, &sr) != 0) {
        h->drag_table_gid = 0;             /* the table went away */
        gv_glon_capture(h, 0);
        return 0;
    }
    if (kind == 2) {
        h->drag_table_gid = 0;
        t->bar_active = 0;
        gv_glon_capture(h, 0);
        h->last_move_gid = -1;             /* hover is re-sent at the next move */
        gv_glon_render_tables(h);
        return 0;                          /* the release still reaches the application */
    }
    if (kind == 0) {
        if (gv_table_drag_thumb(t, (y - sr.y) - h->drag_off)) gv_glon_table_scrolled(h, t);
        return 1;
    }
    return 0;
}

static void gv_glon_set_cursor(gv_glon_host *h, int kind);
static int gv_glon_table_columns_changed(gv_glon_host *h, gv_table *t);
/* GUI-P2: pointer capture, held exactly while a drag that must follow the
 * pointer outside the window is in progress (a table's scrollbar thumb or
 * column divider); never for ordinary presses */
static void gv_glon_capture(gv_glon_host *h, int on) {
    if (h->backend && h->backend->set_capture) h->backend->set_capture(h->backend, on);
}
/* end every drag (window focus lost): the thumb stops, a column keeps the
 * width it has and the widths are reported, the capture is released */
static void gv_glon_end_drags(gv_glon_host *h) {
    if (h->drag_table_gid) {
        gv_table *t = gv_glon_table(h, h->drag_table_gid);
        if (t) t->bar_active = 0;
        h->drag_table_gid = 0;
    }
    if (h->col_drag_gid) {
        gv_table *t = gv_glon_table(h, h->col_drag_gid);
        h->col_drag_gid = 0;
        if (t) {
            t->col_hot = -1;
            char now[sizeof h->col_drag_before];
            gv_table_widths_text(t, now, (int)sizeof now);
            if (strcmp(now, h->col_drag_before) != 0) gv_glon_table_columns_changed(h, t);
        }
        gv_glon_set_cursor(h, GV_CURSOR_NORMAL);
    }
    gv_glon_capture(h, 0);
    h->last_move_gid = -1;
    gv_glon_render_tables(h);
    gv_rebuild_display(h);
}

/* GUI-P2: the pointer's shape, asked of the backend only when it changes */
static void gv_glon_set_cursor(gv_glon_host *h, int kind) {
    if (kind == h->cursor) return;
    h->cursor = kind;
    if (h->backend && h->backend->set_cursor) h->backend->set_cursor(h->backend, kind);
}
int gv_glon_host_cursor(gv_glon_host *h) { return h ? h->cursor : 0; }

/* the table (and its screen rect) whose header divider is under x, y:
 * answers the column, or -1 */
static int gv_glon_divider_under(gv_glon_host *h, int x, int y, gv_table **tp, gv_rect *rp) {
    for (int i = 0; i < h->table_count; i++) {
        gv_table *t = h->tables[i];
        if (!t || t->text_mode || t->id <= 0 || t->id >= GV_GLON_MAX_NODES || !h->glon_to_view[t->id]) continue;
        gv_rect vr = {0, 0, 0, 0}, sr = {0, 0, 0, 0};
        if (gv_view_visible_rect(h->view, h->glon_to_view[t->id], &vr) != 0) continue;
        if (x < vr.x || y < vr.y || x >= vr.x + vr.w || y >= vr.y + vr.h) continue;
        if (gv_view_screen_rect(h->view, h->glon_to_view[t->id], &sr) != 0) continue;
        int c = gv_table_divider_at(t, x - sr.x, y - sr.y);
        if (c >= 0) { if (tp) *tp = t; if (rp) *rp = sr; return c; }
    }
    return -1;
}
static void gv_glon_set_hot(gv_glon_host *h, gv_table *hot, int col) {
    int changed = 0;
    for (int i = 0; i < h->table_count; i++) {
        gv_table *t = h->tables[i];
        if (!t) continue;
        int want = t == hot ? col : -1;
        if (t->col_hot != want) { t->col_hot = want; t->dirty = 1; changed = 1; }
    }
    if (changed) { gv_glon_render_tables(h); gv_rebuild_display(h); }
}
/* tell the application a table's requested column widths changed:
 * "table-columns id want:flex;want:flex;..." (table/widths takes it back) */
static int gv_glon_table_columns_changed(gv_glon_host *h, gv_table *t) {
    char payload[GV_TABLE_MAX_COLS * 16 + 16];
    int n = snprintf(payload, sizeof payload, "%d ", t->id);
    gv_table_widths_text(t, payload + n, (int)sizeof payload - n);
    gv_glon_render_tables(h);
    if (gv_send_text(h, "table-columns", payload) != 0) return -1;
    return gv_glon_host_bind(h);
}
/* GUI-P2: a column divider held: moves (from anywhere) set the column's
 * width from the press; the release ends it and reports the widths.
 * Answers 1 when it took the event. */
static int gv_glon_col_drag(gv_glon_host *h, int kind, int x) {
    if (!h->col_drag_gid) return 0;
    gv_table *t = gv_glon_table(h, h->col_drag_gid);
    if (!t) { h->col_drag_gid = 0; gv_glon_set_cursor(h, GV_CURSOR_NORMAL); gv_glon_capture(h, 0); return 0; }
    if (kind == 0) {
        if (gv_table_set_col_width(t, h->col_drag_col, h->col_drag_w0 + (x - h->col_drag_x0)) > 0) {
            gv_glon_render_tables(h);
            gv_rebuild_display(h);
        }
        return 1;
    }
    if (kind == 2) {
        char now[sizeof h->col_drag_before];
        gv_table_widths_text(t, now, (int)sizeof now);
        int changed = strcmp(now, h->col_drag_before) != 0;
        h->col_drag_gid = 0;
        gv_glon_capture(h, 0);
        t->col_hot = -1;
        t->dirty = 1;
        h->last_move_gid = -1;
        gv_glon_set_cursor(h, GV_CURSOR_NORMAL);
        if (changed) gv_glon_table_columns_changed(h, t);
        else { gv_glon_render_tables(h); gv_rebuild_display(h); }
        return 1;                          /* not a click on whatever is under it */
    }
    return 1;
}

int gv_glon_host_pointer_at(gv_glon_host *h, int kind, int x, int y, int *hit_gid) {
    if (!h) return -1;
    if (gv_glon_table_drag(h, kind, y)) { if (hit_gid) *hit_gid = 0; return 0; }
    if (gv_glon_col_drag(h, kind, x)) { if (hit_gid) *hit_gid = 0; return 0; }
    if (h->table_count > 0 && !h->drag_table_gid && (kind == 0 || kind == 1)) {
        if (h->view->layout_dirty) gv_view_layout(h->view);
        gv_table *dt = NULL;
        gv_rect dr = {0, 0, 0, 0};
        int dc = gv_glon_divider_under(h, x, y, &dt, &dr);
        if (kind == 0) {
            /* over a header divider: the horizontal resize pointer */
            gv_glon_set_cursor(h, dc >= 0 ? GV_CURSOR_COL_RESIZE : GV_CURSOR_NORMAL);
            gv_glon_set_hot(h, dt, dc);
        } else if (dc >= 0) {
            /* a press on a divider resizes (double click: fits) the column
             * to its left; it is not a click on the table */
            if (h->press_clicks >= 2) {
                int w = gv_table_fit_width(dt, dc);
                gv_table_set_col_width(dt, dc, w);
                gv_glon_set_hot(h, dt, dc);
                if (hit_gid) *hit_gid = 0;
                return gv_glon_table_columns_changed(h, dt);
            }
            h->col_drag_gid = dt->id;
            h->col_drag_col = dc;
            h->col_drag_x0 = x;
            h->col_drag_w0 = dt->cols[dc].width;    /* a flexible one: the width it shows */
            gv_table_widths_text(dt, h->col_drag_before, (int)sizeof h->col_drag_before);
            gv_glon_capture(h, 1);          /* the drag follows the pointer out of the window */
            gv_glon_set_cursor(h, GV_CURSOR_COL_RESIZE);
            gv_glon_set_hot(h, dt, dc);
            if (hit_gid) *hit_gid = 0;
            return 0;
        }
    }
    gv_node_id id = 0;
    int lx = 0, ly = 0, gid = 0;
    if (h->view->layout_dirty) gv_view_layout(h->view);
    if (gv_view_hit_test(h->view, x, y, &id, &lx, &ly) && id)
        gid = gv_glon_host_glon_id(h, id);
    if (hit_gid) *hit_gid = gid;
    if (getenv("GV_TRACE_EVENTS"))
        fprintf(stderr, "[pointer] kind=%d at %d,%d view=%d glon=%d\n", kind, x, y, (int)id, gid);
    if (kind == 0) {
        /* hover only changes when the node under the pointer changes; a move
         * within the same node is not worth a Glon event */
        if (gid == h->last_move_gid) return 0;
        h->last_move_gid = gid;
        return gv_glon_host_pointer_move(h, gid);
    }
    if (kind == 2) return gv_glon_host_pointer_up(h, gid, x, y);
    if (!gid) return 0;
    gv_node_id old = gv_view_focus_id(h->view);
    int old_gid = old ? gv_glon_host_glon_id(h, old) : 0;
    /* the pressed node takes View's keyboard focus too (if it can): the
     * runner used to tell the application about the focus without moving
     * View's, so Tab continued from a stale node and a hidden subtree could
     * not drop a focus View did not know about.
     * INTENT-4B-1 fix: the blur and the focus are reported only when View
     * actually moved the focus.  A node that cannot take it (disabled,
     * hidden or not focusable) used to blur the focused widget and be
     * reported focused itself, so an application drew a disabled checkbox
     * as focused and lost the real focus. */
    int took = gv_view_focus(h->view, id) == 0 && gv_view_focus_id(h->view) == id;
    if (took && old_gid && old_gid != gid && gv_glon_host_blur(h, old_gid) != 0) return -1;
    if (gv_glon_host_pointer_down(h, gid, x, y) != 0) return -1;
    if (took && gv_glon_host_focus(h, gid) != 0) return -1;
    return gv_glon_host_click_xy(h, gid, x, y);
}

/* Coalesce a batch of queued input before dispatch: a pointer move that is
 * followed by another move (with only moves in between) is dropped,
 * consecutive wheel events at the same point are summed, and only the last
 * of several window resizes is kept (a drag of the window edge queues one
 * per motion step; only the final size needs a layout).  Order of every
 * other event is kept.  Answers the new count. */
int gv_glon_host_coalesce(gv_event *ev, int n) {
    int out = 0;
    for (int i = 0; i < n; i++) {
        if (ev[i].type == GV_EVENT_RESIZE) {
            int later = 0;
            for (int j = i + 1; j < n && !later; j++) later = ev[j].type == GV_EVENT_RESIZE;
            if (later) continue;
        }
        if (ev[i].type == GV_EVENT_POINTER_MOVE && i + 1 < n &&
            ev[i + 1].type == GV_EVENT_POINTER_MOVE)
            continue;
        if (ev[i].type == GV_EVENT_WHEEL && out > 0 &&
            ev[out - 1].type == GV_EVENT_WHEEL &&
            ev[out - 1].x == ev[i].x && ev[out - 1].y == ev[i].y) {
            ev[out - 1].key += ev[i].key;
            ev[out - 1].mods = (unsigned int)((int)ev[out - 1].mods + (int)ev[i].mods);
            continue;
        }
        ev[out++] = ev[i];
    }
    return out;
}

int gv_glon_host_dispatch(gv_glon_host *h, const gv_event *ev) {
    if (!h || !ev) return -1;
    gv_view *v = h->view;
    /* GLON-INST-3B: with secondary windows open, Glon hears which window
     * the keyboard and the rest now follow (view-window, its root's Glon id)
     * before the first event from another window; a pointer move does not
     * need it (it reaches a node, routed by owner) */
    if (h->nwins > 0 && h->reported_win != h->cur_win && ev->type != GV_EVENT_POINTER_MOVE &&
        ev->type != GV_EVENT_NONE && ev->type != GV_EVENT_TICK && !(ev->type == GV_EVENT_FOCUS && !ev->key)) {
        char ids[16];
        snprintf(ids, sizeof ids, "%d", h->cur_win < 0 ? 1 : h->wins[h->cur_win].gid);
        h->reported_win = h->cur_win;
        if (gv_send_text(h, "view-window", ids) != 0 || gv_glon_host_bind(h) != 0) return -1;
        v = h->view;
    }
    switch (ev->type) {
    case GV_EVENT_CLOSE: {
        /* a secondary window's close button: Glon decides (Intent closes
         * the instance in it, or asks the application) */
        if (h->cur_win < 0) return 0;
        char ids[16];
        snprintf(ids, sizeof ids, "%d", h->wins[h->cur_win].gid);
        if (gv_send_text(h, "window-close", ids) != 0) return -1;
        return gv_glon_host_bind(h);
    }
    case GV_EVENT_POINTER_MOVE: return gv_glon_host_pointer_at(h, 0, ev->x, ev->y, NULL);
    case GV_EVENT_POINTER_DOWN: {
        h->press_clicks = (int)ev->mods;               /* clicks (SDL), 0 when unknown */
        int rc = gv_glon_host_pointer_at(h, 1, ev->x, ev->y, NULL);
        h->press_clicks = 0;
        return rc;
    }
    case GV_EVENT_POINTER_UP:   return gv_glon_host_pointer_at(h, 2, ev->x, ev->y, NULL);
    case GV_EVENT_WHEEL:        return gv_glon_host_wheel(h, ev->x, ev->y, ev->key, (int)ev->mods);
    case GV_EVENT_FOCUS:
        /* the window lost the focus: no drag (and no capture) survives it */
        if (!ev->key && (h->drag_table_gid || h->col_drag_gid)) gv_glon_end_drags(h);
        return 0;
    case GV_EVENT_TEXT: {
        char text[8];
        unsigned int cp = ev->codepoint;
        int n = 0;
        if (cp < 0x80) text[n++] = (char)cp;
        else if (cp < 0x800) {
            text[n++] = (char)(0xC0 | (cp >> 6));
            text[n++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp >= 0x10000) {        /* INTENT-4B-2: four bytes (emoji, rare CJK) */
            text[n++] = (char)(0xF0 | (cp >> 18));
            text[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            text[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            text[n++] = (char)(0x80 | (cp & 0x3F));
        } else {
            text[n++] = (char)(0xE0 | (cp >> 12));
            text[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            text[n++] = (char)(0x80 | (cp & 0x3F));
        }
        text[n] = 0;
        if (gv_glon_host_char(h, text) != 0) return -1;
        return gv_glon_host_char_event(h, text);
    }
    case GV_EVENT_KEY_DOWN:
        if (ev->key == 8) {
            if (gv_glon_host_backspace(h) != 0) return -1;
            return gv_glon_host_key(h, 8);
        }
        if (ev->key == 9) {
            gv_node_id old = gv_view_focus_id(v);
            int old_gid = old ? gv_glon_host_glon_id(h, old) : 0;
            if (old_gid && gv_glon_host_blur(h, old_gid) != 0) return -1;
            /* FOREIGN-1: Shift+Tab goes backwards (it did not before) */
            int back = (ev->mods & 0x1) || (ev->mods & 0x2);
            h->focus_how = back ? GV_HOST_FOCUS_LAST : GV_HOST_FOCUS_FIRST;
            gv_view_focus_next(v, back);
            if (getenv("GV_TRACE_HOST"))
                fprintf(stderr, "[host] %s: View focus %d -> %d (glon %d -> %d)\n", back ? "Shift+Tab" : "Tab",
                        (int)old, (int)gv_view_focus_id(v), old_gid, gv_glon_host_glon_id(h, gv_view_focus_id(v)));
            int gid = gv_glon_host_glon_id(h, gv_view_focus_id(v));
            return gid ? gv_glon_host_focus(h, gid) : 0;
        }
        return gv_glon_host_key_mods(h, ev->key, ev->mods);
    case GV_EVENT_RESIZE:
        /* GUI-P2: the application hears about it (view-resize "w h") and
         * can lay itself out again; before, only View's root changed size */
        return gv_glon_host_resize(h, ev->width, ev->height);
    default:
        return 0;
    }
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
            /* GUI-P2: a click on the scrollbar track pages (on the thumb:
             * nothing); it is not a row click */
            int bar = gv_table_scrollbar_hit(t, sr.w, x - sr.x, y - sr.y);
            if (bar != 2) {
                if (bar) gv_table_scroll(t, bar * gv_table_page_rows(t));
                else {
                    /* on the thumb: drag it until the button is released */
                    int ty = 0, tl = 0;
                    gv_table_thumb(t, &ty, &tl);
                    h->drag_table_gid = glon_id;
                    h->drag_off = (y - sr.y) - ty;
                    t->bar_active = 1;
                    gv_glon_capture(h, 1);  /* the drag follows the pointer out of the window */
                }
                return gv_glon_table_scrolled(h, t);
            }
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
        /* INTENT-4B-2: Shift+Up / Shift+Down (selection in a multi-line text);
         * Up and Down themselves keep SDL's codes, as before */
        else if (key == 1073741906) key = 1019;
        else if (key == 1073741905) key = 1020;
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
    if (getenv("GV_TRACE_WINDOW")) fprintf(stderr, "[window] host: view-resize %dx%d sent to the application\n", width, height);
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

/* ==== FOREIGN-1: hosted foreign components (gv_host.h, FOREIGN-1.md) ======== */
static struct gv_hosted *gv_glon_hosted(gv_glon_host *h, int gid) {
    for (int i = 0; i < 16; i++) if (h->hosted[i].gid == gid && gid) return &h->hosted[i];
    return NULL;
}
static void gv_glon_queue(gv_glon_host *h, struct gv_hosted *s, const char *name, const char *detail) {
    if (h->hq_n >= 64) return;                      /* a flood: later events are dropped */
    struct gv_host_event *e = &h->hq[h->hq_n++];
    e->slot = (int)(s - h->hosted);
    e->gen = s->gen;
    e->gid = s->gid;
    snprintf(e->name, sizeof e->name, "%s", name);
    snprintf(e->detail, sizeof e->detail, "%s", detail ? detail : "");
}
/* the env's emit: from the component, during its own calls */
static void gv_glon_host_emit(void *token, const char *name, const char *detail) {
    struct gv_hosted *s = (struct gv_hosted *)token;
    if (!s->gid || !s->inst) return;
    gv_glon_queue(s->h, s, name, detail);
}
/* host new ID KIND [props...]  -- attach a component to the existing node ID
 * host cmd ID COMMAND [arg...] -- a content command for it */
static void gv_glon_host_command(gv_glon_host *h, char *p) {
    char sub[16], kind[64];
    int id;
    if (!gv_view_next_word(&p, sub, sizeof sub) || !gv_view_next_int(&p, &id)) { h->error = 1; return; }
    if (!strcmp(sub, "new")) {
        if (!gv_view_next_word(&p, kind, sizeof kind)) { h->error = 1; return; }
        char props[512];
        gv_view_rest(&p, props, sizeof props);
        struct gv_hosted *s = gv_glon_hosted(h, id);
        if (!s) for (int i = 0; i < 16 && !s; i++) if (!h->hosted[i].gid) s = &h->hosted[i];
        if (!s || id <= 0 || id >= GV_GLON_MAX_NODES || !h->glon_to_view[id]) { h->error = 1; return; }
        if (s->gid) gv_glon_hosts_release(h, s->gid);
        int gen = s->gen;
        memset(s, 0, sizeof *s);
        s->h = h;
        s->gid = id;
        s->gen = gen;
        s->p = gv_host_find(kind);
        void *parent = h->backend && h->backend->native_handle ? h->backend->native_handle(h->backend) : NULL;
#ifdef _WIN32
        gv_host_env env = { parent, parent ? "win32" : "none", gv_glon_host_emit, s };
#else
        gv_host_env env = { parent, parent ? "x11" : "none", gv_glon_host_emit, s };
#endif
        char err[160] = "";
        if (!s->p) snprintf(err, sizeof err, "no provider of kind %s", kind);
        else s->inst = s->p->create(&env, props, err, (int)sizeof err);
        if (!s->inst) gv_glon_queue(h, s, "failed", err[0] ? err : "creation failed");
        else gv_glon_queue(h, s, "created", kind);
    } else if (!strcmp(sub, "cmd")) {
        struct gv_hosted *s = gv_glon_hosted(h, id);
        char c[32], arg[512];
        if (!gv_view_next_word(&p, c, sizeof c)) { h->error = 1; return; }
        gv_view_rest(&p, arg, sizeof arg);
        /* INTENT-3: a command the component refuses is its "error" event
         * (on-event, follow ... error), not silence */
        if (s && s->inst && s->p->command(s->inst, c, arg) < 0) {
            char why[96];
            snprintf(why, sizeof why, "unknown command %s", c);
            gv_glon_queue(h, s, "error", why);
        }
    } else h->error = 1;
}
/* the node went away (its subtree destroyed, the host freed) */
static void gv_glon_hosts_release(gv_glon_host *h, int gid) {
    struct gv_hosted *s = gv_glon_hosted(h, gid);
    if (!s) return;
    if (s->inst) s->p->destroy(s->inst);
    int gen = s->gen + 1;                     /* queued events of this instance are now stale */
    memset(s, 0, sizeof *s);
    s->gen = gen;
}
/* visible: the node and every ancestor visible */
static int gv_glon_shown(gv_view *v, gv_node_id id) {
    for (gv_node_id c = id; c;) {
        gv_node *n = gv_view_node(v, c);
        if (!n || !n->visible) return 0;
        c = n->parent;
    }
    return 1;
}
/* tell every component where it is, what of it shows and whether it has
 * the focus -- only what changed */
static void gv_glon_sync_hosts(gv_glon_host *h) {
    for (int i = 0; i < 16; i++) {
        struct gv_hosted *s = &h->hosted[i];
        if (!s->gid || !s->inst) continue;
        gv_node_id v = h->glon_to_view[s->gid];
        if (!v) continue;
        gv_rect b = {0, 0, 0, 0}, c = {0, 0, 0, 0};
        if (h->view->layout_dirty) gv_view_layout(h->view);
        gv_view_screen_rect(h->view, v, &b);
        int vis = gv_glon_shown(h->view, v) && gv_view_visible_rect(h->view, v, &c) == 0 && c.w > 0 && c.h > 0;
        if (!vis) c = (gv_rect){0, 0, 0, 0};
        if (!s->placed || vis != s->visible || memcmp(&b, &s->bounds, sizeof b) || memcmp(&c, &s->clip, sizeof c)) {
            s->bounds = b;
            s->clip = c;
            s->visible = vis;
            s->placed = 1;
            s->p->place(s->inst, b, c, vis);
        }
        int want = gv_view_focus_id(h->view) == v && vis;
        if (want != s->focused) {
            s->focused = want;
            /* how it arrived: Tab (its first control), Shift+Tab (its last),
             * otherwise where it was (a click, a request, code) */
            int how = want ? (h->focus_how ? h->focus_how : GV_HOST_FOCUS_CURRENT) : GV_HOST_FOCUS_OFF;
            int took = s->p->focus(s->inst, how);
            if (getenv("GV_TRACE_HOST"))
                fprintf(stderr, "[host] component %d (%s): focus %d -> it answered %d\n", s->gid, s->p->kind, want, took);
        }
    }
    h->focus_how = 0;              /* used up: a later change is a click or code */
}
/* run every component's own events, then deliver what they emitted.
 * Answers how many events were delivered (the caller redraws if any) */
int gv_glon_host_pump(gv_glon_host *h) {
    if (!h) return -1;
    for (int i = 0; i < 16; i++)
        if (h->hosted[i].gid && h->hosted[i].inst) h->hosted[i].p->pump(h->hosted[i].inst);
    int delivered = 0;
    for (int k = 0; k < h->hq_n; k++) {
        struct gv_host_event e = h->hq[k];
        struct gv_hosted *s = &h->hosted[e.slot];
        if (s->gen != e.gen || s->gid != e.gid) continue;      /* destroyed since: dropped */
        gv_node_id v = h->glon_to_view[e.gid];
        if (!strcmp(e.name, "tab") && !s->focused) {
            /* a Tab the component saw after the focus had already left it
             * (keys typed in the moment before its tab was handled): View
             * moved on, so the request is stale -- honoured, it made the
             * focus jump back (WSLg trace, 2026-11-25) */
            if (getenv("GV_TRACE_HOST"))
                fprintf(stderr, "[host] component %d: tab %s ignored (it does not have the focus)\n", e.gid, e.detail);
        } else if (!strcmp(e.name, "focus-request") || !strcmp(e.name, "tab")) {
            /* View decides the focus; the component follows (sync) */
            gv_node_id old = gv_view_focus_id(h->view);
            int old_gid = old ? gv_glon_host_glon_id(h, old) : 0;
            gv_node_id next = v;
            h->focus_how = GV_HOST_FOCUS_CURRENT;
            if (!strcmp(e.name, "tab")) {
                gv_view_focus(h->view, v);
                int back = !strcmp(e.detail, "prev");
                h->focus_how = back ? GV_HOST_FOCUS_LAST : GV_HOST_FOCUS_FIRST;
                next = gv_view_focus_next(h->view, back);
            }
            if (next && next != old) {
                int how = h->focus_how;
                if (old_gid) gv_glon_host_blur(h, old_gid);
                gv_view_focus(h->view, next);
                int ng = gv_glon_host_glon_id(h, next);
                h->focus_how = how;          /* the blur's bind used it up */
                if (ng) gv_glon_host_focus(h, ng);
            }
            if (getenv("GV_TRACE_HOST"))
                fprintf(stderr, "[host] component %d: %s%s -> View focus %d (glon %d)\n", e.gid, e.name,
                        !strcmp(e.name, "tab") ? (!strcmp(e.detail, "prev") ? " prev" : " next") : "",
                        (int)gv_view_focus_id(h->view), gv_glon_host_glon_id(h, gv_view_focus_id(h->view)));
        } else {
            char payload[320];
            snprintf(payload, sizeof payload, "%d %s %s", e.gid, e.name, e.detail);
            gv_send_text(h, "host-event", payload);
            gv_glon_host_bind(h);
        }
        delivered++;
    }
    h->hq_n = 0;
    gv_glon_sync_hosts(h);
    return delivered;
}
/* tests: the component instance on node GID (NULL: none or failed) */
void *gv_glon_host_component(gv_glon_host *h, int gid) {
    struct gv_hosted *s = h ? gv_glon_hosted(h, gid) : NULL;
    return s ? s->inst : NULL;
}

/* ==== GLON-INST-3B: secondary native windows ================================
 * One Glon session, one View, one id space.  Each secondary window is an
 * extra window root in the View with its own backend (an SDL window of the
 * same SDL session; a headless one under a headless first window), size,
 * focus, hover and display list.  The host lays out, hit-tests, types and
 * draws in one window at a time: the window context (gv_glon_window_enter)
 * makes that window's root, focus and size the View's own, and its backend
 * the host's.  Everything else -- the id tables, tables, the Glon session --
 * is shared. */
#ifdef GV_MANAGED_CELLS
#define GV_GLON_MANAGED ((long)GV_MANAGED_CELLS)
#else
#define GV_GLON_MANAGED ((long)(GC_HEAP_LIMIT - GC_HEAP_BASE))
#endif
/* the managed heap's room: the cells not live after the last collection
 * (before the first: not yet used), and its size */
void gv_glon_host_heap_room(long *free_cells, long *total) {
    long used = r0_s1_gc_count() > 0 ? r0_s1_gc_live_cells() : r0_s1_heap_high() - GC_HEAP_BASE;
    if (total) *total = GV_GLON_MANAGED;
    if (free_cells) *free_cells = GV_GLON_MANAGED - used;
}
static gv_node_id gv_glon_prim_root(gv_glon_host *h) { return h->cur_win < 0 ? h->view->root : h->prim.root; }
/* the window a root is (-1 the first, -3 none) */
static int gv_glon_win_of_root(gv_glon_host *h, gv_node_id root) {
    if (!root) return -3;
    if (root == gv_glon_prim_root(h)) return -1;
    for (int i = 0; i < h->nwins; i++) if (h->wins[i].root == root) return i;
    return -3;
}
static int gv_glon_win_by_gid(gv_glon_host *h, int gid) {
    for (int i = 0; i < h->nwins; i++) if (h->wins[i].gid == gid) return i;
    return -3;
}
static void gv_glon_window_enter(gv_glon_host *h, int w) {
    if (!h || w == h->cur_win || w < -1 || w >= h->nwins) return;
    gv_view *v = h->view;
    if (h->cur_win < 0) {
        h->prim.root = v->root; h->prim.focus = v->focus; h->prim.width = v->width; h->prim.height = v->height;
        h->prim.last_move_gid = h->last_move_gid; h->prim.backend = h->backend;
    } else {
        struct gv_glon_win *W = &h->wins[h->cur_win];
        W->focus = v->focus; W->width = v->width; W->height = v->height; W->last_move_gid = h->last_move_gid;
    }
    if (w < 0) {
        v->root = h->prim.root; v->focus = h->prim.focus; v->width = h->prim.width; v->height = h->prim.height;
        h->last_move_gid = h->prim.last_move_gid; h->backend = h->prim.backend;
    } else {
        struct gv_glon_win *W = &h->wins[w];
        v->root = W->root; v->focus = W->focus; v->width = W->width; v->height = W->height;
        h->last_move_gid = W->last_move_gid; h->backend = W->backend;
    }
    v->layout_dirty = gv_view_window_dirty(v, v->root);
    h->cur_win = w;
}
static int gv_glon_enter_node_window(gv_glon_host *h, gv_node_id v) {
    int was = h->cur_win;
    int w = gv_glon_win_of_root(h, gv_view_window_of(h->view, v));
    if (w >= -1) gv_glon_window_enter(h, w);
    return was;
}
static void gv_glon_focus_any(gv_glon_host *h, gv_node_id v) {
    int was = gv_glon_enter_node_window(h, v);
    gv_view_focus(h->view, v);
    gv_glon_window_enter(h, was);
}
/* a window's remembered focus that was destroyed (or reused elsewhere) is
 * dropped */
static void gv_glon_windows_check_focus(gv_glon_host *h) {
    gv_view *v = h->view;
    if (h->cur_win >= 0 && h->prim.focus &&
        (!gv_view_valid(v, h->prim.focus) || gv_view_window_of(v, h->prim.focus) != h->prim.root)) h->prim.focus = 0;
    for (int i = 0; i < h->nwins; i++) {
        struct gv_glon_win *W = &h->wins[i];
        if (i == h->cur_win || !W->focus) continue;
        if (!gv_view_valid(v, W->focus) || gv_view_window_of(v, W->focus) != W->root) W->focus = 0;
    }
}
void gv_glon_host_set_window_ops(gv_glon_host *h, const gv_window_ops *ops) { if (h) h->wops = ops; }
void gv_glon_host_use_windows(gv_glon_host *h, int on) { if (h) h->windows_lib = on; }
static void gv_glon_close_backend(gv_glon_host *h, gv_backend *b) {
    if (!b) return;
    if (h->wops) h->wops->close(b);
    else b->destroy(b);
}
static void gv_glon_window_failed(gv_glon_host *h, int gid, const char *why) {
    if (getenv("GV_TRACE_WINDOW")) fprintf(stderr, "[window] window %d not opened: %s\n", gid, why);
    if (h->win_failed_n >= 8) return;
    h->win_failed_gid[h->win_failed_n] = gid;
    snprintf(h->win_failed_why[h->win_failed_n], sizeof h->win_failed_why[0], "%s", why);
    h->win_failed_n++;
}
static void gv_glon_window_close(gv_glon_host *h, int idx) {
    gv_node_id cur_root = h->view->root;
    int was = h->cur_win;
    gv_glon_window_enter(h, -1);
    struct gv_glon_win *W = &h->wins[idx];
    gv_glon_release_subtree(h, W->root);
    gv_view_destroy(h->view, W->root);
    if (W->root > 0 && W->root < GV_GLON_MAX_VIEW_NODES) h->view_to_glon[W->root] = 0;
    if (W->gid > 0 && W->gid < GV_GLON_MAX_NODES) h->glon_to_view[W->gid] = 0;
    gv_glon_close_backend(h, W->backend);
    gv_display_list_free(&W->display);
    h->wins[idx] = h->wins[h->nwins - 1];
    memset(&h->wins[h->nwins - 1], 0, sizeof h->wins[0]);
    h->nwins--;
    h->win_closed++;
    h->reported_win = -2;                   /* indices moved: say it again */
    if (was >= 0 && was != idx) {
        int ni = gv_glon_win_of_root(h, cur_root);
        if (ni >= 0) gv_glon_window_enter(h, ni);
    }
}
static void gv_glon_windows_close_all(gv_glon_host *h) {
    if (!h || !h->wins) return;
    gv_glon_window_enter(h, -1);
    for (int i = 0; i < h->nwins; i++) {
        gv_glon_close_backend(h, h->wins[i].backend);
        gv_display_list_free(&h->wins[i].display);
    }
    h->nwins = 0;
}
/* view window-new GID W H TITLE / window-close GID / window-title GID TEXT /
 * window-raise GID / heap-watch 0|1 */
static void gv_glon_window_command(gv_glon_host *h, const char *cmd, char *p) {
    if (!strcmp(cmd, "heap-watch")) {
        int on = 0;
        gv_view_next_int(&p, &on);
        h->heap_watch = on;
        h->heap_reported_gc = -1;           /* the next bind reports */
        return;
    }
    int gid = 0;
    if (!gv_view_next_int(&p, &gid)) { h->error = 1; return; }
    if (!strcmp(cmd, "window-new")) {
        int w = 0, hh = 0;
        char title[256];
        gv_view_next_int(&p, &w);
        gv_view_next_int(&p, &hh);
        gv_view_rest(&p, title, sizeof title);
        const char *why = NULL;
        gv_backend *b = NULL;
        const char *fail = getenv("GV_FAIL_WINDOW");     /* test switch: the Nth window opened fails */
        int real = 0;
        for (int i = 0; i < h->nwins; i++) real += h->wins[i].backend != NULL;
        if (gid <= 1 || GV_G2V_ROOM(h, gid) != 0 || h->glon_to_view[gid] || h->nwins >= GV_GLON_WIN_SLOTS) {
            /* no root can stand for it: what Glon builds on it is refused */
            gv_glon_window_failed(h, gid, "id");
            return;
        }
        if (real >= GV_GLON_MAX_WINDOWS) why = "limit";
        else if (w <= 0 || hh <= 0 || w > 16384 || hh > 16384) why = "size";
        else if (fail && atol(fail) == h->win_opened + h->win_refused + 1) why = "native";
        if (!why) {
            b = h->wops ? h->wops->make() : gv_backend_headless();
            gv_backend_config cfg = {w, hh, title, 0, 0};
            if (!b) why = "memory";
            else if (b->init(b, &cfg) != 0 || b->create_window(b, w, hh, title) != 0) {
                gv_glon_close_backend(h, b);
                b = NULL;
                why = "native";
            }
        }
        /* refused: a root with no native window (a "ghost") still stands for
         * it, so the rest of the batch builds on it without errors; Glon
         * hears window-failed and closes it, which removes the ghost */
        if (why) { gv_glon_close_backend(h, b); b = NULL; gv_glon_window_failed(h, gid, why); h->win_refused++; }
        gv_node_id root = gv_view_add_window(h->view);
        if (!root || GV_V2G_ROOM(h, root) != 0) {
            if (root) gv_view_destroy(h->view, root);
            gv_glon_close_backend(h, b);
            if (!why) { gv_glon_window_failed(h, gid, "view"); h->win_refused++; }
            return;
        }
        if (w <= 0 || hh <= 0) { w = 1; hh = 1; }
        gv_node *rn = gv_view_node(h->view, root);
        rn->base_w = rn->w = w;
        rn->base_h = rn->h = hh;
        h->glon_to_view[gid] = root;
        h->view_to_glon[root] = gid;
        struct gv_glon_win *W = &h->wins[h->nwins++];
        memset(W, 0, sizeof *W);
        W->gid = gid; W->root = root; W->backend = b; W->width = w; W->height = hh;
        W->last_move_gid = -1; W->force = 1;
        gv_display_list_init(&W->display);
        if (b) h->win_opened++;
        if (getenv("GV_TRACE_WINDOW")) fprintf(stderr, "[window] window %d opened (%dx%d, SDL id %u)\n", gid, w, hh,
                                               h->wops ? h->wops->window_id(b) : 0);
        return;
    }
    int idx = gv_glon_win_by_gid(h, gid);
    if (idx < 0) return;                    /* closed already (or never opened) */
    if (!strcmp(cmd, "window-close")) gv_glon_window_close(h, idx);
    else if (!strcmp(cmd, "window-title")) {
        char title[256];
        gv_view_rest(&p, title, sizeof title);
        if (h->wops) h->wops->set_title(h->wins[idx].backend, title);
    } else if (!strcmp(cmd, "window-raise")) {
        if (h->wops) h->wops->raise(h->wins[idx].backend);
    }
}

/* the open native windows (refused ones, until Glon closes them, are not) */
int gv_glon_host_window_count(gv_glon_host *h) {
    int n = 0;
    for (int i = 0; h && i < h->nwins; i++) n += h->wins[i].backend != NULL;
    return n;
}
int gv_glon_host_window_gid(gv_glon_host *h, int i) { return h && i >= 0 && i < h->nwins ? h->wins[i].gid : 0; }
int gv_glon_host_window_index(gv_glon_host *h, int gid) { return h ? gv_glon_win_by_gid(h, gid) : -3; }
gv_backend *gv_glon_host_window_backend(gv_glon_host *h, int i) { return h && i >= 0 && i < h->nwins ? h->wins[i].backend : NULL; }
const gv_display_list *gv_glon_host_window_display(gv_glon_host *h, int i) { return h && i >= 0 && i < h->nwins ? &h->wins[i].display : NULL; }
gv_node_id gv_glon_host_window_root(gv_glon_host *h, int i) { return h && i >= 0 && i < h->nwins ? h->wins[i].root : 0; }
/* a window's View focus (the first window: i = -1) and size */
gv_node_id gv_glon_host_window_focus(gv_glon_host *h, int i) {
    if (!h || i < -1 || i >= h->nwins) return 0;
    if (i == h->cur_win) return h->view->focus;
    return i < 0 ? h->prim.focus : h->wins[i].focus;
}
void gv_glon_host_window_size(gv_glon_host *h, int i, int *w, int *hh) {
    *w = *hh = 0;
    if (!h || i < -1 || i >= h->nwins) return;
    if (i == h->cur_win) { *w = h->view->width; *hh = h->view->height; return; }
    if (i < 0) { *w = h->prim.width; *hh = h->prim.height; } else { *w = h->wins[i].width; *hh = h->wins[i].height; }
}
void gv_glon_host_window_stats(gv_glon_host *h, long *opened, long *closed, long *dropped, long *frames) {
    long f = 0;
    for (int i = 0; h && i < h->nwins; i++) f += h->wins[i].frames;
    if (opened) *opened = h ? h->win_opened : 0;
    if (closed) *closed = h ? h->win_closed : 0;
    if (dropped) *dropped = (h ? h->win_dropped : 0) + (h && h->wops ? h->wops->dropped() : 0);
    if (frames) *frames = f;
}
/* one event of secondary window i, in its context */
int gv_glon_host_dispatch_window(gv_glon_host *h, int i, const gv_event *ev) {
    if (!h || !ev) return -1;
    if (i < 0 || i >= h->nwins) { h->win_dropped++; return 0; }
    gv_glon_window_enter(h, i);
    h->wins[i].force = 1;
    int rc = gv_glon_host_dispatch(h, ev);
    gv_glon_window_enter(h, -1);
    return rc;
}
/* every secondary window's pending events, each in its own context; the
 * events left for a window that one of them closed are dropped */
int gv_glon_host_pump_windows(gv_glon_host *h) {
    if (!h) return -1;
    int total = 0, rc = 0;
    int gids[GV_GLON_MAX_WINDOWS], n = h->nwins;
    for (int i = 0; i < n; i++) gids[i] = h->wins[i].gid;
    for (int i = 0; i < n; i++) {
        int idx = gv_glon_win_by_gid(h, gids[i]);
        if (idx < 0) continue;
        gv_event evs[128];
        int m = 0;
        gv_backend *b = h->wins[idx].backend;
        if (!b) continue;                   /* a refused window has none */
        while (m < 128 && b->poll_event(b, &evs[m]) > 0) m++;
        m = gv_glon_host_coalesce(evs, m);
        for (int k = 0; k < m; k++) {
            idx = gv_glon_win_by_gid(h, gids[i]);
            if (idx < 0) { h->win_dropped += m - k; break; }
            if (gv_glon_host_dispatch_window(h, idx, &evs[k]) != 0) rc = -1;
            total++;
        }
    }
    return rc ? -1 : total;
}
/* draw the secondary windows that changed (all: force) */
int gv_glon_host_render_windows(gv_glon_host *h, int force) {
    if (!h) return -1;
    int rc = 0;
    for (int i = 0; i < h->nwins; i++) {
        struct gv_glon_win *W = &h->wins[i];
        if (!W->backend) continue;
        if (!force && !W->force && !gv_view_window_changed(h->view, W->root)) continue;
        gv_glon_window_enter(h, i);
        gv_view *v = h->view;
        if (v->layout_dirty || gv_view_window_dirty(v, v->root)) {
            gv_view_layout(v);
            /* its tables follow the sizes just laid out */
            for (int t = 0; t < h->table_count; t++) {
                gv_table *tb = h->tables[t];
                gv_node_id tv = tb ? gv_glon_host_view_node(h, tb->id) : 0;
                gv_node *tn = tv ? gv_view_node(v, tv) : NULL;
                if (tn && gv_view_window_of(v, tv) == W->root) gv_table_render(v, tv, tb, (gv_rect){0, 0, tn->w, tn->h});
            }
        }
        gv_display_list_free(&W->display);
        gv_display_list_init(&W->display);
        gv_view_build_display_list(v, &W->display);
        gv_backend *b = W->backend;
        if (b->begin_frame(b) != 0 || b->draw(b, &W->display) != 0 || b->end_frame(b) != 0) rc = -1;
        W->force = 0;
        W->frames++;
        gv_glon_window_enter(h, -1);
    }
    return rc;
}

