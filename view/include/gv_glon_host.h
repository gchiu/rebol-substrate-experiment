#ifndef GV_GLON_HOST_H
#define GV_GLON_HOST_H

#include <stdio.h>
#include "gv_view.h"
#include "gv_backend.h"
#include "gv_table.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gv_glon_host gv_glon_host;

gv_glon_host *gv_glon_host_new(gv_view *view, gv_backend *backend);
void gv_glon_host_free(gv_glon_host *h);

int gv_glon_host_load(gv_glon_host *h, const char *root);
int gv_glon_host_load_app(gv_glon_host *h, const char *root,
                          const char *app_path);
int gv_glon_host_load_vid_app(gv_glon_host *h, const char *root,
                              const char *app_path);
int gv_glon_host_load_vocab_vid_app(gv_glon_host *h, const char *root,
                                    const char *app_path);
/* INTENT-4B-3: the same desktop VID stack without the Intent dialect (and
 * its editing layers), for machine-VID applications such as the gallery */
int gv_glon_host_load_machine_vid_app(gv_glon_host *h, const char *root,
                                      const char *app_path);
int gv_glon_host_load_gui_app(gv_glon_host *h, const char *root,
                              const char *app_path);
int gv_glon_host_load_gui3_app(gv_glon_host *h, const char *root,
                               const char *app_path);
int gv_glon_host_load_vid_gui3_app(gv_glon_host *h, const char *root,
                                   const char *app_path);
int gv_glon_host_run(gv_glon_host *h, const char *src);
int gv_glon_host_deliver(gv_glon_host *h);
int gv_glon_host_bind(gv_glon_host *h);
int gv_glon_host_build(gv_glon_host *h);
int gv_glon_host_build_app(gv_glon_host *h, const char *spec_path);

int gv_glon_host_click(gv_glon_host *h, int glon_id);
int gv_glon_host_click_xy(gv_glon_host *h, int glon_id, int x, int y);
int gv_glon_host_pointer_move(gv_glon_host *h, int glon_id);
int gv_glon_host_pointer_down(gv_glon_host *h, int glon_id, int x, int y);
int gv_glon_host_pointer_up(gv_glon_host *h, int glon_id, int x, int y);
int gv_glon_host_pointer_cancel(gv_glon_host *h, int glon_id);
int gv_glon_host_resize(gv_glon_host *h, int width, int height);
int gv_glon_host_focus(gv_glon_host *h, int glon_id);
int gv_glon_host_blur(gv_glon_host *h, int glon_id);
int gv_glon_host_char(gv_glon_host *h, const char *text);
int gv_glon_host_backspace(gv_glon_host *h);
int gv_glon_host_key(gv_glon_host *h, int key);
int gv_glon_host_key_mods(gv_glon_host *h, int key, unsigned int mods);
/* dx, dy are scroll-offset steps: positive moves toward the end of the
 * content (wheel down / right), negative toward the start. */
int gv_glon_host_wheel(gv_glon_host *h, int x, int y, int dx, int dy);
int gv_glon_host_pointer_at(gv_glon_host *h, int kind, int x, int y, int *hit_gid);
/* GUI-P2: the pointer shape the host last asked for (GV_CURSOR_*) */
int gv_glon_host_cursor(gv_glon_host *h);
/* FOREIGN-1: run the hosted components' own event loops and deliver what
 * they emitted (call once per loop iteration; answers events delivered) */
int gv_glon_host_pump(gv_glon_host *h);
/* FOREIGN-1 tests: the component instance hosted on node GID, or NULL */
void *gv_glon_host_component(gv_glon_host *h, int gid);
/* One backend input event, routed as the interactive runner does it
 * (pointer, wheel, text, keys incl. Tab focus order, resize). */
int gv_glon_host_dispatch(gv_glon_host *h, const gv_event *ev);
int gv_glon_host_coalesce(gv_event *ev, int n);
/* allocation calls and cells summed over every run so far; nonzero only when
 * built with -DR0_S1_PROFILE (GUI-P2 measurement) */
void gv_glon_host_pf_totals(long *allocs, long *cells);
long gv_glon_host_pf_steps(void);
void gv_glon_host_f1b_stats(gv_glon_host *h, int *takes, int *chunks, int *errors);
/* GLON-INST-3A: the node-id tables: entries allocated (Glon id -> View node,
 * View node -> Glon id), the ids mapped now, and the View's node capacity */
void gv_glon_host_node_tables(gv_glon_host *h, int *g2v_cap, int *v2g_cap, int *mapped, int *view_cap);
int gv_glon_host_char_event(gv_glon_host *h, const char *text);
int gv_glon_host_tick(gv_glon_host *h);

int gv_glon_host_text(gv_glon_host *h, const char *word, char *buf, int cap);
void gv_glon_host_batch_stats(gv_glon_host *h, int *last, int *max);
int gv_glon_host_table(gv_glon_host *h, int glon_id, gv_table **out);
int gv_glon_host_table_command(gv_glon_host *h, int glon_id, int cmd, int a,
                               int b, const char *text);
int gv_glon_host_table_status(gv_glon_host *h, int glon_id, char *buf, int cap);
int gv_glon_host_error(gv_glon_host *h);
/* INTENT-7: per-event timing, always on (two clock reads an event).  For each
 * event name since the last reset: Glon runs, the time in the runs, the time
 * applying their batches, collections.  One host per process (file-level). */
void gv_glon_host_timing_reset(void);
void gv_glon_host_timing_report(FILE *out, const char *indent);
void gv_glon_host_timing_totals(double *glon_ms, double *take_ms, double *bind_ms, long *runs, long *gcs);
const gv_display_list *gv_glon_host_display(gv_glon_host *h);
gv_node_id gv_glon_host_view_node(gv_glon_host *h, int glon_id);
int gv_glon_host_glon_id(gv_glon_host *h, gv_node_id view_node);

/* GLON-INST-3B: secondary native windows (GLON-INST-3B.md).  Glon opens
 * one with "view window-new GID W H TITLE" and closes it with "view
 * window-close GID"; the runner pumps and draws them after the first window:
 * gv_glon_host_pump_windows, then gv_glon_host_render_windows.  Index i runs
 * over the open secondary windows (-1: the first window). */
void gv_glon_host_set_window_ops(gv_glon_host *h, const gv_window_ops *ops);
/* load Intent's window words (view/glon/intent_windows.glon) with an Intent
 * application: call before loading it */
void gv_glon_host_use_windows(gv_glon_host *h, int on);
int gv_glon_host_window_count(gv_glon_host *h);
int gv_glon_host_window_gid(gv_glon_host *h, int i);
int gv_glon_host_window_index(gv_glon_host *h, int gid);
gv_backend *gv_glon_host_window_backend(gv_glon_host *h, int i);
const gv_display_list *gv_glon_host_window_display(gv_glon_host *h, int i);
gv_node_id gv_glon_host_window_root(gv_glon_host *h, int i);
gv_node_id gv_glon_host_window_focus(gv_glon_host *h, int i);
void gv_glon_host_window_size(gv_glon_host *h, int i, int *w, int *hh);
void gv_glon_host_window_stats(gv_glon_host *h, long *opened, long *closed, long *dropped, long *frames);
int gv_glon_host_dispatch_window(gv_glon_host *h, int i, const gv_event *ev);
int gv_glon_host_pump_windows(gv_glon_host *h);
int gv_glon_host_render_windows(gv_glon_host *h, int force);
void gv_glon_host_heap_room(long *free_cells, long *total);

#ifdef __cplusplus
}
#endif

#endif