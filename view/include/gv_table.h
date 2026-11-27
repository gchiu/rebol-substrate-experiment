#ifndef GV_TABLE_H
#define GV_TABLE_H

#include "gv_view.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GV_TABLE_MAX_COLS 32
#define GV_TABLE_MAX_TEXT 256
#define GV_TABLE_DEFAULT_ROWS 300

typedef enum gv_table_cell_kind {
    GV_TABLE_CELL_TEXT = 0,
    GV_TABLE_CELL_CHECKBOX = 1,
    GV_TABLE_CELL_INDICATOR = 2
} gv_table_cell_kind;

typedef struct gv_table_cell {
    char *text;
    int kind;
    int readonly;
    int renderer;
} gv_table_cell;

typedef struct gv_table_row {
    int id;
    gv_table_cell *cells;
    int ncols;
} gv_table_row;

typedef struct gv_table_col {
    char *title;
    int width;           /* laid-out width (gv_table_layout_columns) */
    int align;
    /* GUI-P2 column sizing */
    int want;            /* requested width: the spec's, a drag's or a fit's */
    int flex;            /* takes the spare width of the table (never less than want) */
    int user;            /* set by a drag or a fit (sent back to the application) */
} gv_table_col;

#define GV_TABLE_COL_MIN 24      /* narrowest a drag or a fit makes a column */
#define GV_TABLE_COL_MAX 1600    /* widest a drag makes a column */
#define GV_TABLE_FIT_MAX 480     /* widest an auto-fit makes a column */
#define GV_TABLE_FIT_ROWS 5000   /* rows an auto-fit measures (from the first in view order) */
#define GV_TABLE_DIVIDER_HIT 4   /* px either side of a header divider */

typedef struct gv_table {
    int id;
    gv_table_col cols[GV_TABLE_MAX_COLS];
    int ncols;
    gv_table_row *rows;
    int nrows;
    int cap;
    int *view_index;
    int view_count;
    int sort_col;
    int sort_dir;
    char filter[GV_TABLE_MAX_TEXT];
    int has_filter;
    int *sel;
    int sel_count;
    int sel_cap;
    int row_h;
    int header_h;
    int visible_start;
    int dirty;
    gv_color bg;
    gv_color panel;
    gv_color header;
    gv_color text;
    gv_color accent;
    /* INTENT-7: the selected rows' background, when the theme gives one
     * (alpha > 0); otherwise the accent, as before.  The accent stays the
     * focus ring's and the borders' colour. */
    gv_color selection;
    gv_color border;
    gv_color row_alt;
    int show_grid;
    int view_h;          /* height of the last rendered viewport (scroll limit) */
    int bar_active;      /* the scrollbar thumb is being dragged (drawn accented) */
    int view_w;          /* width of the last rendered viewport */
    int col_hot;         /* header divider hovered or dragged (drawn accented), -1 none */
    /* text mode (GUI-P2): one document instead of rows; its display rows are
     * its lines wrapped at the table's width (rows[] stays empty) */
    int text_mode;
    char *doc;           /* UTF-8, lines separated by '\n' */
    int doc_len, doc_cap;
    double doc_size;
    int doc_dirty;       /* changed since the last wrap */
    int wrap_w;          /* width of the last wrap */
    int *wrap_start, *wrap_len;   /* display rows: byte ranges of text */
    int wrap_count, wrap_cap;
    int doc_top_req;     /* byte to bring to the top at the next layout, or -1 */
    int focus_id;
    int focus_col;
    int focus_active;
    int edit_id;
    int edit_col;
    int edit_active;
    char edit_text[GV_TABLE_MAX_TEXT];
} gv_table;

gv_table *gv_table_create(int id, int nrows);
void gv_table_free(gv_table *t);
int gv_table_set_column(gv_table *t, int col, const char *title, int width,
                        int align);
int gv_table_set_columns(gv_table *t, const char *spec);
int gv_table_set_cell(gv_table *t, int row, int col, const char *text);
int gv_table_set_cell_inplace(gv_table *t, int row, int col, const char *text);
const char *gv_table_get_cell(gv_table *t, int row, int col);
void gv_table_sort(gv_table *t, int col, int dir);
void gv_table_filter(gv_table *t, const char *text);
int gv_table_row_id(gv_table *t, int row);
int gv_table_find_row(gv_table *t, int id);
int gv_table_select(gv_table *t, int row, int selected);
int gv_table_is_selected(gv_table *t, int row);
int gv_table_select_id(gv_table *t, int id, int selected);
int gv_table_is_selected_id(gv_table *t, int id);
int gv_table_selected_id(gv_table *t);
/* no row selected */
void gv_table_clear_selection(gv_table *t);
int gv_table_insert_row(gv_table *t, int row, const char *const *cells, int n);
int gv_table_insert_row_text(gv_table *t, int row, const char *text);
int gv_table_delete_row(gv_table *t, int row);
int gv_table_row_at(gv_table *t, int y);
int gv_table_column_at(gv_table *t, int x);
int gv_table_visible_rows(gv_table *t, int viewport_h);
int gv_table_render(gv_view *v, gv_node_id node, gv_table *t, gv_rect viewport);
int gv_table_next_row(gv_table *t, int row, int delta);
int gv_table_page(gv_table *t, int viewport_h, int delta);
int gv_table_scroll(gv_table *t, int delta);
/* rows that fit wholly below the header in the last rendered viewport */
int gv_table_page_rows(gv_table *t);
/* the largest visible_start: the last row is the bottom full row */
int gv_table_max_start(gv_table *t);
/* the vertical scrollbar (track) of the last rendered viewport, x0..x1 in
 * table coordinates, or 0 when every row fits */
int gv_table_scrollbar(gv_table *t, int viewport_w, int *x0, int *x1);
/* the thumb of the scrollbar: its y and length in table coordinates */
int gv_table_thumb(gv_table *t, int *y, int *len);
/* where x, y (table coordinates) falls on the scrollbar: -1 above the thumb,
 * 1 below it, 0 on it, 2 not on the scrollbar */
int gv_table_scrollbar_hit(gv_table *t, int viewport_w, int x, int y);
/* scroll so that the thumb's top is at y (table coordinates), clamped;
 * answers 1 when the first visible row changed */
int gv_table_drag_thumb(gv_table *t, int y);
/* ---- column sizing (GUI-P2) ---------------------------------------------
 * Columns are laid out at each render: every column at its requested width,
 * the flexible one (spec field 4 = 1) widened to fill the table up to the
 * scrollbar.  When the table is narrower than the columns, nothing shrinks:
 * the columns keep their widths and those past the right edge are cut by the
 * table's clip.  Column c spans [x(c), x(c) + width) from the table's left
 * edge, x(0) = 0; its divider is its right edge. */
int gv_table_layout_columns(gv_table *t, int viewport_w);
/* left edge of column c (c == ncols: right edge of the last) */
int gv_table_col_x(gv_table *t, int c);
/* the column whose right-edge divider is within GV_TABLE_DIVIDER_HIT px of x
 * in the header row (table coordinates), or -1 */
int gv_table_divider_at(gv_table *t, int x, int y);
/* a requested width (clamped): the column is no longer flexible */
int gv_table_set_col_width(gv_table *t, int c, int width);
/* the width that shows the title and every measured cell whole, clamped to
 * GV_TABLE_COL_MIN..GV_TABLE_FIT_MAX */
int gv_table_fit_width(gv_table *t, int c);
/* the requested widths as text, "want:flex" per column separated by ';', and
 * back (a missing or empty entry keeps the column) */
int gv_table_widths_text(gv_table *t, char *buf, int cap);
int gv_table_apply_widths(gv_table *t, const char *text);
/* text width in px at a font size; the host installs the backend's (Pango),
 * else an estimate is used */
typedef int (*gv_table_measure_fn)(void *ctx, const char *s, int len, double size);
void gv_table_set_measure(gv_table_measure_fn fn, void *ctx);

/* ---- text mode ----------------------------------------------------------
 * A wrapper breaks one line (no '\n') of UTF-8 text into display rows no
 * wider than width pixels at the given font size, at character boundaries:
 * it fills starts/lens (byte offsets into s) and answers the row count (at
 * most max).  The host installs the backend's (Pango); without one a
 * UTF-8-safe estimate is used. */
typedef int (*gv_table_wrap_fn)(void *ctx, const char *s, int len, double size,
                                int width, int *starts, int *lens, int max);
void gv_table_set_wrapper(gv_table_wrap_fn fn, void *ctx);
/* empty document at this font size (scroll position kept) */
int gv_table_text_reset(gv_table *t, double size);
/* append bytes; with unit_sep, byte 0x1F stands for a newline (command
 * lines cannot carry one) */
int gv_table_text_append(gv_table *t, const char *s, int n, int unit_sep);
/* wrap at width if the text or the width changed; the first visible row
 * keeps showing the same byte of text */
int gv_table_text_layout(gv_table *t, int width);
/* bring the row holding this byte to the top (at the next layout) */
int gv_table_text_top(gv_table *t, int byte);
/* the byte at the start of the top row */
int gv_table_text_top_byte(gv_table *t);
/* display row i: byte offset and length in t->doc */
int gv_table_text_row(gv_table *t, int i, int *start, int *len);
#define GV_TABLE_BAR_W 14        /* scrollbar track width */
#define GV_TABLE_THUMB_MIN 24    /* shortest thumb */
int gv_table_status(gv_table *t, char *buf, int cap);
int gv_table_rebuild_view(gv_table *t);
/* INTENT-7: the selected rows' background (alpha 0: the accent) */
void gv_table_set_selection_color(gv_table *t, gv_color selection);
int gv_table_set_theme(gv_table *t, gv_color bg, gv_color panel, gv_color header,
                       gv_color text, gv_color accent, gv_color border,
                       gv_color row_alt, int show_grid);
int gv_table_focus(gv_table *t, int row, int col);
int gv_table_focus_id(gv_table *t, int id, int col);
int gv_table_focus_row(gv_table *t);
int gv_table_focus_col(gv_table *t);
int gv_table_move_focus(gv_table *t, int dr, int dc);
int gv_table_begin_edit(gv_table *t, int row, int col);
int gv_table_set_edit_text(gv_table *t, const char *text);
int gv_table_commit_edit(gv_table *t);
int gv_table_cancel_edit(gv_table *t);
int gv_table_set_readonly(gv_table *t, int row, int col, int readonly);
int gv_table_set_cell_kind(gv_table *t, int row, int col, int kind);
int gv_table_set_cell_renderer(gv_table *t, int row, int col, int renderer);
int gv_table_resize_column(gv_table *t, int col, int width);
int gv_table_clear(gv_table *t);
int gv_table_load_rows(gv_table *t, const char *data);
int gv_table_batch(gv_table *t, const char *script);
void gv_table_unescape(char *s);
char *gv_table_field(char **p, char sep);

#ifdef __cplusplus
}
#endif

#endif
