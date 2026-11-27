#define _POSIX_C_SOURCE 200809L
#include "gv_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int gv_table_sort_col;
static int gv_table_sort_dir;

static int gv_table_cmp(const void *a, const void *b) {
    const gv_table_row *ra = (const gv_table_row *)a;
    const gv_table_row *rb = (const gv_table_row *)b;
    int c = gv_table_sort_col;
    if (c < 0 || c >= ra->ncols || c >= rb->ncols) return 0;
    const char *sa = ra->cells[c].text ? ra->cells[c].text : "";
    const char *sb = rb->cells[c].text ? rb->cells[c].text : "";
    int r = strcmp(sa, sb);
    return gv_table_sort_dir < 0 ? -r : r;
}

static char *gv_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

char *gv_table_field(char **p, char sep) {
    char *s = *p;
    char *start = s;
    while (*s) {
        if (*s == '\\' && s[1]) { s += 2; continue; }
        if (*s == sep) { *s = 0; *p = s + 1; return start; }
        s++;
    }
    *p = s;
    return start;
}

void gv_table_unescape(char *s) {
    char *r = s;
    char *w = s;
    while (*r) {
        if (*r == '\\' && r[1]) {
            r++;
            if (*r == 'n') *w++ = '\n';
            else *w++ = *r;
            r++;
        } else {
            *w++ = *r++;
        }
    }
    *w = 0;
}

static int gv_table_clamp_col(gv_table *t, int col) {
    if (col < 0) return 0;
    if (col >= t->ncols) return t->ncols > 0 ? t->ncols - 1 : 0;
    return col;
}

static int gv_table_cell_readonly(gv_table *t, int row, int col) {
    if (!t || row < 0 || row >= t->view_count || col < 0 || col >= t->ncols)
        return 0;
    gv_table_row *r = &t->rows[t->view_index[row]];
    if (col >= r->ncols) return 0;
    return r->cells[col].readonly;
}

int gv_table_rebuild_view(gv_table *t) {
    if (!t) return -1;
    if (t->text_mode) return 0;          /* display rows come from wrapping */
    if (t->sort_col >= 0 && t->sort_col < t->ncols) {
        gv_table_sort_col = t->sort_col;
        gv_table_sort_dir = t->sort_dir;
        qsort(t->rows, (size_t)t->nrows, sizeof(gv_table_row), gv_table_cmp);
    }
    int n = 0;
    for (int i = 0; i < t->nrows; i++) {
        int match = 1;
        if (t->has_filter && t->filter[0]) {
            match = 0;
            for (int c = 0; c < t->ncols; c++) {
                const char *s = t->rows[i].cells[c].text;
                if (s && strstr(s, t->filter)) { match = 1; break; }
            }
        }
        if (match) t->view_index[n++] = i;
    }
    t->view_count = n;
    if (t->visible_start >= n) t->visible_start = n > 0 ? n - 1 : 0;
    t->dirty = 1;
    return 0;
}

gv_table *gv_table_create(int id, int nrows) {
    if (nrows < 0) return NULL;
    gv_table *t = (gv_table *)calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->id = id;
    t->row_h = 24;
    t->col_hot = -1;
    t->header_h = 28;
    t->sort_col = -1;
    t->sort_dir = 1;
    t->bg = (gv_color){24, 26, 30, 255};
    t->panel = (gv_color){42, 48, 58, 255};
    t->header = (gv_color){52, 58, 68, 255};
    t->text = (gv_color){235, 238, 242, 255};
    t->accent = (gv_color){70, 130, 200, 255};
    t->border = (gv_color){70, 76, 86, 255};
    t->row_alt = (gv_color){36, 41, 50, 255};
    t->show_grid = 1;
    t->focus_id = -1;
    t->edit_id = -1;
    t->cap = nrows > 0 ? nrows : 1;
    t->rows = (gv_table_row *)calloc((size_t)t->cap, sizeof(gv_table_row));
    t->view_index = (int *)malloc((size_t)(t->cap > 0 ? t->cap : 1) * sizeof(int));
    if (!t->rows || !t->view_index) {
        free(t->rows); free(t->view_index); free(t); return NULL;
    }
    t->ncols = 4;
    const char *titles[4] = {"Name", "Village", "Status", "Donation"};
    int widths[4] = {180, 140, 90, 90};
    for (int c = 0; c < 4; c++) {
        t->cols[c].title = gv_strdup(titles[c]);
        t->cols[c].width = widths[c];
        t->cols[c].align = c == 3 ? 2 : 0;
    }
    for (int i = 0; i < nrows; i++) {
        gv_table_row *r = &t->rows[t->nrows++];
        r->id = i + 1;
        r->ncols = 4;
        r->cells = (gv_table_cell *)calloc(4, sizeof(gv_table_cell));
        if (!r->cells) { t->nrows--; break; }
        char b[4][64];
        snprintf(b[0], sizeof b[0], "Member %d", i + 1);
        snprintf(b[1], sizeof b[1], "Village %d", (i * 7) % 97 + 1);
        snprintf(b[2], sizeof b[2], "%s", (i % 5 == 0) ? "Lapsed" : "Active");
        snprintf(b[3], sizeof b[3], "%d", 10 + (i * 7) % 90);
        for (int c = 0; c < 4; c++) {
            r->cells[c].kind = GV_TABLE_CELL_TEXT;
            r->cells[c].readonly = 0;
            r->cells[c].renderer = 0;
            r->cells[c].text = gv_strdup(b[c]);
        }
    }
    gv_table_rebuild_view(t);
    return t;
}

void gv_table_free(gv_table *t) {
    if (!t) return;
    for (int i = 0; i < t->nrows; i++) {
        int n = t->rows[i].ncols;
        for (int c = 0; c < n; c++) free(t->rows[i].cells[c].text);
        free(t->rows[i].cells);
    }
    free(t->rows);
    free(t->view_index);
    free(t->sel);
    free(t->doc);
    free(t->wrap_start);
    free(t->wrap_len);
    for (int c = 0; c < t->ncols; c++) free(t->cols[c].title);
    free(t);
}

int gv_table_set_column(gv_table *t, int col, const char *title, int width,
                        int align) {
    if (!t || col < 0 || col >= GV_TABLE_MAX_COLS) return -1;
    free(t->cols[col].title);
    t->cols[col].title = gv_strdup(title ? title : "");
    t->cols[col].width = width > 0 ? width : 100;
    t->cols[col].want = t->cols[col].width;
    t->cols[col].flex = 0;
    t->cols[col].user = 0;
    t->cols[col].align = align;
    if (col + 1 > t->ncols) t->ncols = col + 1;
    t->dirty = 1;
    return 0;
}

int gv_table_set_columns(gv_table *t, const char *spec) {
    if (!t || !spec) return -1;
    char *copy = gv_strdup(spec);
    if (!copy) return -1;
    int col = 0;
    char *p = copy;
    char *field = gv_table_field(&p, ';');
    while (field && *field && col < GV_TABLE_MAX_COLS) {
        char *q = field;
        char *title = gv_table_field(&q, ':');
        char *width_s = gv_table_field(&q, ':');
        char *align_s = q && *q ? gv_table_field(&q, ':') : NULL;
        char *flex_s = q && *q ? q : NULL;        /* optional: 1 = flexible */
        gv_table_unescape(title);
        int width = width_s ? atoi(width_s) : 120;
        int align = align_s ? atoi(align_s) : 0;
        if (gv_table_set_column(t, col, title, width, align) != 0) {
            free(copy);
            return -1;
        }
        t->cols[col].flex = flex_s ? atoi(flex_s) != 0 : 0;
        col++;
        if (!*p) break;
        field = gv_table_field(&p, ';');
    }
    t->ncols = col;
    t->dirty = 1;
    free(copy);
    return 0;
}

int gv_table_set_theme(gv_table *t, gv_color bg, gv_color panel, gv_color header,
                       gv_color text, gv_color accent, gv_color border,
                       gv_color row_alt, int show_grid) {
    if (!t) return -1;
    t->bg = bg;
    t->panel = panel;
    t->header = header;
    t->text = text;
    t->accent = accent;
    t->border = border;
    t->row_alt = row_alt;
    t->show_grid = show_grid;
    t->dirty = 1;
    return 0;
}

void gv_table_set_selection_color(gv_table *t, gv_color selection) {
    if (!t) return;
    t->selection = selection;
    t->dirty = 1;
}

void gv_table_sort(gv_table *t, int col, int dir) {
    if (!t || col < 0 || col >= t->ncols) return;
    t->sort_col = col;
    t->sort_dir = dir < 0 ? -1 : 1;
    gv_table_rebuild_view(t);
}

void gv_table_filter(gv_table *t, const char *text) {
    if (!t) return;
    snprintf(t->filter, sizeof t->filter, "%s", text ? text : "");
    t->has_filter = text && text[0];
    gv_table_rebuild_view(t);
}

static int gv_table_set_cell_common(gv_table *t, int row, int col,
                                    const char *text, int force) {
    if (!t || row < 0 || row >= t->view_count || col < 0 || col >= t->ncols)
        return -1;
    gv_table_row *r = &t->rows[t->view_index[row]];
    if (col >= r->ncols) return -1;
    if (!force && r->cells[col].readonly) return -1;
    char *copy = gv_strdup(text ? text : "");
    if (!copy) return -1;
    free(r->cells[col].text);
    r->cells[col].text = copy;
    t->dirty = 1;
    return 0;
}

int gv_table_set_cell(gv_table *t, int row, int col, const char *text) {
    int rc = gv_table_set_cell_common(t, row, col, text, 0);
    if (rc == 0) gv_table_rebuild_view(t);
    return rc;
}

int gv_table_set_cell_inplace(gv_table *t, int row, int col, const char *text) {
    return gv_table_set_cell_common(t, row, col, text, 0);
}

const char *gv_table_get_cell(gv_table *t, int row, int col) {
    if (!t || row < 0 || row >= t->view_count || col < 0 || col >= t->ncols) return NULL;
    gv_table_row *r = &t->rows[t->view_index[row]];
    if (col >= r->ncols) return NULL;
    return r->cells[col].text;
}

int gv_table_row_id(gv_table *t, int row) {
    if (!t || row < 0 || row >= t->view_count) return -1;
    return t->rows[t->view_index[row]].id;
}

int gv_table_find_row(gv_table *t, int id) {
    if (!t) return -1;
    for (int i = 0; i < t->view_count; i++) {
        if (t->rows[t->view_index[i]].id == id) return i;
    }
    return -1;
}

int gv_table_select_id(gv_table *t, int id, int selected) {
    if (!t) return -1;
    int idx = gv_table_find_row(t, id);
    if (idx < 0) return -1;
    for (int i = 0; i < t->sel_count; i++) {
        if (t->sel[i] == id) {
            if (!selected) {
                for (int j = i; j < t->sel_count - 1; j++) t->sel[j] = t->sel[j + 1];
                t->sel_count--;
                t->dirty = 1;
                return 0;
            }
            return 0;
        }
    }
    if (selected) {
        if (t->sel_count == t->sel_cap) {
            int cap = t->sel_cap ? t->sel_cap * 2 : 16;
            int *p = (int *)realloc(t->sel, (size_t)cap * sizeof(int));
            if (!p) return -1;
            t->sel = p; t->sel_cap = cap;
        }
        t->sel[t->sel_count++] = id;
        t->dirty = 1;
    }
    return 0;
}

int gv_table_is_selected_id(gv_table *t, int id) {
    if (!t) return 0;
    for (int i = 0; i < t->sel_count; i++) if (t->sel[i] == id) return 1;
    return 0;
}

int gv_table_select(gv_table *t, int row, int selected) {
    int id = gv_table_row_id(t, row);
    if (id < 0) return -1;
    return gv_table_select_id(t, id, selected);
}

int gv_table_is_selected(gv_table *t, int row) {
    int id = gv_table_row_id(t, row);
    return id < 0 ? 0 : gv_table_is_selected_id(t, id);
}

void gv_table_clear_selection(gv_table *t) {
    if (!t || t->sel_count == 0) return;
    t->sel_count = 0;
    t->dirty = 1;
}

int gv_table_selected_id(gv_table *t) {
    if (!t || t->sel_count <= 0) return -1;
    return t->sel[0];
}

static int gv_table_row_new_id(gv_table *t) {
    int max = 0;
    for (int i = 0; i < t->nrows; i++) if (t->rows[i].id > max) max = t->rows[i].id;
    return max + 1;
}

int gv_table_insert_row(gv_table *t, int row, const char *const *cells, int n) {
    if (!t || row < 0 || row > t->view_count || n < 0 || n > t->ncols) return -1;
    if (t->nrows == t->cap) {
        int cap = t->cap * 2;
        gv_table_row *p = (gv_table_row *)malloc((size_t)cap * sizeof(gv_table_row));
        int *vi = (int *)malloc((size_t)cap * sizeof(int));
        if (!p || !vi) { free(p); free(vi); return -1; }
        if (t->nrows > 0) {
            memcpy(p, t->rows, (size_t)t->nrows * sizeof(gv_table_row));
            memcpy(vi, t->view_index, (size_t)t->nrows * sizeof(int));
        }
        free(t->rows);
        free(t->view_index);
        t->rows = p;
        t->view_index = vi;
        t->cap = cap;
    }
    int insert = (row < t->view_count) ? t->view_index[row] : t->nrows;
    gv_table_row newrow;
    memset(&newrow, 0, sizeof(newrow));
    newrow.id = gv_table_row_new_id(t);
    newrow.ncols = t->ncols;
    newrow.cells = (gv_table_cell *)calloc((size_t)t->ncols, sizeof(gv_table_cell));
    if (!newrow.cells) return -1;
    for (int c = 0; c < t->ncols; c++) {
        newrow.cells[c].kind = GV_TABLE_CELL_TEXT;
        newrow.cells[c].readonly = 0;
        newrow.cells[c].renderer = 0;
        newrow.cells[c].text = gv_strdup(c < n && cells[c] ? cells[c] : "");
    }
    memmove(&t->rows[insert + 1], &t->rows[insert], (size_t)(t->nrows - insert) * sizeof(gv_table_row));
    t->rows[insert] = newrow;
    t->nrows++;
    gv_table_rebuild_view(t);
    return 0;
}

int gv_table_insert_row_text(gv_table *t, int row, const char *text) {
    if (!t || !text) return -1;
    char *copy = gv_strdup(text);
    if (!copy) return -1;
    const char *cells[GV_TABLE_MAX_COLS];
    int n = 0;
    char *p = copy;
    char *field = gv_table_field(&p, '|');
    while (field && n < GV_TABLE_MAX_COLS) {
        gv_table_unescape(field);
        cells[n++] = field;
        if (!*p) break;
        field = gv_table_field(&p, '|');
    }
    int rc = gv_table_insert_row(t, row, cells, n);
    free(copy);
    return rc;
}

int gv_table_delete_row(gv_table *t, int row) {
    if (!t || row < 0 || row >= t->view_count) return -1;
    int idx = t->view_index[row];
    gv_table_row *r = &t->rows[idx];
    int id = r->id;
    for (int c = 0; c < r->ncols; c++) free(r->cells[c].text);
    free(r->cells);
    memmove(&t->rows[idx], &t->rows[idx + 1], (size_t)(t->nrows - idx - 1) * sizeof(gv_table_row));
    t->nrows--;
    for (int i = 0; i < t->sel_count; ) {
        if (t->sel[i] == id) {
            for (int j = i; j < t->sel_count - 1; j++) t->sel[j] = t->sel[j + 1];
            t->sel_count--;
        } else {
            i++;
        }
    }
    if (t->focus_id == id) { t->focus_id = -1; t->focus_active = 0; }
    if (t->edit_id == id) { t->edit_id = -1; t->edit_active = 0; }
    gv_table_rebuild_view(t);
    return 0;
}

int gv_table_row_at(gv_table *t, int y) {
    if (!t || y < t->header_h) return -1;
    int idx = (y - t->header_h) / (t->row_h > 0 ? t->row_h : 1) + t->visible_start;
    if (idx < 0 || idx >= t->view_count) return -1;
    return idx;
}

int gv_table_column_at(gv_table *t, int x) {
    if (!t || x < 0) return -1;
    int cx = 0;                          /* columns start at the table's left edge */
    for (int c = 0; c < t->ncols; c++) {
        if (x < cx + t->cols[c].width) return c;
        cx += t->cols[c].width;
    }
    return t->ncols > 0 ? t->ncols - 1 : -1;
}

/* ---- column sizing ------------------------------------------------------- */
static gv_table_measure_fn gv_table_measurer;
static void *gv_table_measurer_ctx;
void gv_table_set_measure(gv_table_measure_fn fn, void *ctx) {
    gv_table_measurer = fn;
    gv_table_measurer_ctx = ctx;
}
/* without a shaping backend: the wrap estimate's advances */
static int gv_table_measure_estimate(const char *s, int len, double size) {
    double w = 0;
    for (int i = 0; i < len;) {
        unsigned char c = (unsigned char)s[i];
        int k = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
        w += c < 0x80 ? size * 0.55 : (c >= 0xE2 && k >= 3) ? size : size * 0.6;
        i += k;
    }
    return (int)(w + 0.5);
}
static int gv_table_measure(const char *s, double size) {
    if (!s || !*s) return 0;
    int len = (int)strlen(s);
    return gv_table_measurer ? gv_table_measurer(gv_table_measurer_ctx, s, len, size)
                             : gv_table_measure_estimate(s, len, size);
}

static int gv_table_clamp(int w, int lo, int hi) { return w < lo ? lo : w > hi ? hi : w; }

int gv_table_layout_columns(gv_table *t, int viewport_w) {
    if (!t || t->ncols <= 0) return 0;
    t->view_w = viewport_w;
    int bar = 0, x0 = 0, x1 = 0;
    if (gv_table_scrollbar(t, viewport_w, &x0, &x1)) bar = x1 - x0;
    int avail = viewport_w - bar, sum = 0, flex = -1;
    for (int c = 0; c < t->ncols; c++) {
        t->cols[c].width = t->cols[c].want > 0 ? t->cols[c].want : 100;
        sum += t->cols[c].width;
        if (t->cols[c].flex && flex < 0) flex = c;
    }
    /* the flexible column takes the spare width; it never shrinks below its
     * requested width, so a narrow table cuts the columns on the right */
    if (flex >= 0 && sum < avail) t->cols[flex].width += avail - sum;
    return 1;
}

int gv_table_col_x(gv_table *t, int c) {
    if (!t) return 0;
    int x = 0;
    for (int i = 0; i < c && i < t->ncols; i++) x += t->cols[i].width;
    return x;
}

int gv_table_divider_at(gv_table *t, int x, int y) {
    if (!t || t->text_mode || t->ncols <= 0 || y < 0 || y >= t->header_h) return -1;
    int best = -1, bestd = GV_TABLE_DIVIDER_HIT + 1, edge = 0;
    for (int c = 0; c < t->ncols; c++) {
        edge += t->cols[c].width;
        if (edge > t->view_w + GV_TABLE_DIVIDER_HIT) break;     /* cut off: not reachable */
        int d = abs(x - edge);
        if (d <= bestd) { best = c; bestd = d; }               /* ties: the right-hand one */
    }
    return bestd <= GV_TABLE_DIVIDER_HIT ? best : -1;
}

int gv_table_set_col_width(gv_table *t, int c, int width) {
    if (!t || c < 0 || c >= t->ncols) return -1;
    int w = gv_table_clamp(width, GV_TABLE_COL_MIN, GV_TABLE_COL_MAX);
    if (w == t->cols[c].want && !t->cols[c].flex && t->cols[c].user) return 0;
    t->cols[c].want = w;
    t->cols[c].flex = 0;
    t->cols[c].user = 1;
    t->cols[c].width = w;
    t->dirty = 1;
    return 1;
}

/* padding around the drawn text (gv_table_render / gv_table_draw_cell):
 * a title starts 8 px into its column, a cell's text 16 px, and both end
 * 4 px before the column's right edge */
int gv_table_fit_width(gv_table *t, int c) {
    if (!t || c < 0 || c >= t->ncols) return -1;
    int w = gv_table_measure(t->cols[c].title, 13.0) + 12;
    int n = t->view_count < GV_TABLE_FIT_ROWS ? t->view_count : GV_TABLE_FIT_ROWS;
    for (int i = 0; i < n; i++) {
        gv_table_row *r = &t->rows[t->view_index[i]];
        if (c >= r->ncols) continue;
        gv_table_cell *cell = &r->cells[c];
        int extra = cell->kind == GV_TABLE_CELL_CHECKBOX ? 26 + 20 :
                    cell->kind == GV_TABLE_CELL_INDICATOR ? 24 + 20 : 20;
        int cw = gv_table_measure(cell->text, 12.0) + extra;
        if (cw > w) w = cw;
    }
    return gv_table_clamp(w + 2, GV_TABLE_COL_MIN, GV_TABLE_FIT_MAX);
}

int gv_table_widths_text(gv_table *t, char *buf, int cap) {
    if (!t || !buf || cap <= 0) return -1;
    int n = 0;
    buf[0] = 0;
    for (int c = 0; c < t->ncols && n < cap; c++)
        n += snprintf(buf + n, (size_t)(cap - n), "%s%d:%d", c ? ";" : "", t->cols[c].want, t->cols[c].flex);
    return n < cap ? 0 : -1;
}

int gv_table_apply_widths(gv_table *t, const char *text) {
    if (!t || !text) return -1;
    const char *p = text;
    for (int c = 0; c < t->ncols && *p; c++) {
        while (*p == ' ') p++;
        if (*p != ';' && *p) {
            int w = atoi(p), f = 0;
            const char *q = p;
            while (*q && *q != ';' && *q != ':') q++;
            if (*q == ':') f = atoi(q + 1);
            if (w > 0) {
                t->cols[c].want = gv_table_clamp(w, GV_TABLE_COL_MIN, GV_TABLE_COL_MAX);
                t->cols[c].flex = f != 0;
                t->cols[c].user = 1;
            }
        }
        while (*p && *p != ';') p++;
        if (*p == ';') p++;
    }
    t->dirty = 1;
    return 0;
}

int gv_table_visible_rows(gv_table *t, int viewport_h) {
    if (!t || viewport_h <= t->header_h || t->row_h <= 0) return 0;
    int n = (viewport_h - t->header_h) / t->row_h + 1;
    return n > t->view_count ? t->view_count : n;
}

int gv_table_next_row(gv_table *t, int row, int delta) {
    if (!t || t->view_count <= 0) return -1;
    int idx = row;
    if (idx < 0) idx = 0;
    idx += delta;
    if (idx < 0) idx = 0;
    if (idx >= t->view_count) idx = t->view_count - 1;
    return idx;
}

int gv_table_page(gv_table *t, int viewport_h, int delta) {
    int step = gv_table_visible_rows(t, viewport_h) - 1;
    if (step < 1) step = 1;
    return gv_table_next_row(t, -1, delta * step);
}

int gv_table_page_rows(gv_table *t) {
    if (!t || t->row_h <= 0 || t->view_h <= t->header_h) return 0;
    return (t->view_h - t->header_h) / t->row_h;
}

int gv_table_max_start(gv_table *t) {
    if (!t) return 0;
    int page = gv_table_page_rows(t);
    /* before the first render the viewport is unknown: the old limit */
    if (page <= 0) return t->view_count > 0 ? t->view_count - 1 : 0;
    return t->view_count > page ? t->view_count - page : 0;
}

int gv_table_scrollbar(gv_table *t, int viewport_w, int *x0, int *x1) {
    if (!t || gv_table_page_rows(t) <= 0 || t->view_count <= gv_table_page_rows(t)) return 0;
    if (x0) *x0 = viewport_w - GV_TABLE_BAR_W;
    if (x1) *x1 = viewport_w;
    return 1;
}

int gv_table_thumb(gv_table *t, int *y, int *len) {
    int page = gv_table_page_rows(t);
    if (!t || page <= 0 || t->view_count <= page) return 0;
    int th = t->view_h - t->header_h;
    int l = th * page / t->view_count;
    if (l < GV_TABLE_THUMB_MIN) l = GV_TABLE_THUMB_MIN;
    if (l > th) l = th;
    int maxs = gv_table_max_start(t);
    int pos = maxs > 0 ? (th - l) * t->visible_start / maxs : 0;
    if (y) *y = t->header_h + pos;
    if (len) *len = l;
    return 1;
}

int gv_table_scrollbar_hit(gv_table *t, int viewport_w, int x, int y) {
    int x0 = 0, x1 = 0, ty = 0, tl = 0;
    if (!gv_table_scrollbar(t, viewport_w, &x0, &x1) || x < x0 || x >= x1 || y < t->header_h) return 2;
    gv_table_thumb(t, &ty, &tl);
    return y < ty ? -1 : y >= ty + tl ? 1 : 0;
}

int gv_table_drag_thumb(gv_table *t, int y) {
    int ty = 0, tl = 0;
    if (!gv_table_thumb(t, &ty, &tl)) return 0;
    int travel = t->view_h - t->header_h - tl;
    int maxs = gv_table_max_start(t);
    int pos = y - t->header_h;
    if (pos < 0) pos = 0;
    if (pos > travel) pos = travel;
    /* the start whose thumb position is nearest to pos */
    int start = travel > 0 ? (pos * maxs + travel / 2) / travel : 0;
    if (start == t->visible_start) return 0;
    t->visible_start = start;
    t->dirty = 1;
    return 1;
}

int gv_table_scroll(gv_table *t, int delta) {
    if (!t) return -1;
    int start = t->visible_start + delta;
    if (start < 0) start = 0;
    int max = gv_table_max_start(t);
    if (start > max) start = max;
    t->visible_start = start;
    t->dirty = 1;
    return start;
}

int gv_table_focus(gv_table *t, int row, int col) {
    int id = gv_table_row_id(t, row);
    if (id < 0) return -1;
    return gv_table_focus_id(t, id, col);
}

int gv_table_focus_id(gv_table *t, int id, int col) {
    if (!t || gv_table_find_row(t, id) < 0) return -1;
    t->focus_id = id;
    t->focus_col = gv_table_clamp_col(t, col);
    t->focus_active = 1;
    t->dirty = 1;
    return 0;
}

int gv_table_focus_row(gv_table *t) {
    if (!t || t->focus_id < 0) return -1;
    return gv_table_find_row(t, t->focus_id);
}

int gv_table_focus_col(gv_table *t) {
    return t ? t->focus_col : 0;
}

int gv_table_move_focus(gv_table *t, int dr, int dc) {
    if (!t) return -1;
    int row = gv_table_focus_row(t);
    if (row < 0) row = 0;
    int nr = gv_table_next_row(t, row, dr);
    int nc = gv_table_clamp_col(t, t->focus_col + dc);
    if (nr < 0) return -1;
    return gv_table_focus(t, nr, nc);
}

int gv_table_begin_edit(gv_table *t, int row, int col) {
    int id = gv_table_row_id(t, row);
    if (id < 0 || col < 0 || col >= t->ncols) return -1;
    if (gv_table_cell_readonly(t, row, col)) return -1;
    t->edit_id = id;
    t->edit_col = col;
    t->edit_active = 1;
    snprintf(t->edit_text, sizeof t->edit_text, "%s",
             gv_table_get_cell(t, row, col) ? gv_table_get_cell(t, row, col) : "");
    t->focus_id = id;
    t->focus_col = col;
    t->focus_active = 1;
    t->dirty = 1;
    return 0;
}

int gv_table_set_edit_text(gv_table *t, const char *text) {
    if (!t || !t->edit_active) return -1;
    snprintf(t->edit_text, sizeof t->edit_text, "%s", text ? text : "");
    t->dirty = 1;
    return 0;
}

int gv_table_commit_edit(gv_table *t) {
    if (!t || !t->edit_active || t->edit_id < 0) return -1;
    int row = gv_table_find_row(t, t->edit_id);
    if (row < 0 || gv_table_cell_readonly(t, row, t->edit_col)) {
        t->edit_active = 0;
        return -1;
    }
    int rc = gv_table_set_cell_inplace(t, row, t->edit_col, t->edit_text);
    if (rc == 0 && (t->sort_col == t->edit_col || t->has_filter))
        gv_table_rebuild_view(t);
    t->edit_active = 0;
    t->dirty = 1;
    return rc;
}

int gv_table_cancel_edit(gv_table *t) {
    if (!t) return -1;
    t->edit_active = 0;
    t->dirty = 1;
    return 0;
}

int gv_table_set_readonly(gv_table *t, int row, int col, int readonly) {
    if (!t || row < 0 || row >= t->view_count || col < 0 || col >= t->ncols)
        return -1;
    gv_table_row *r = &t->rows[t->view_index[row]];
    if (col >= r->ncols) return -1;
    r->cells[col].readonly = readonly ? 1 : 0;
    t->dirty = 1;
    return 0;
}

int gv_table_set_cell_kind(gv_table *t, int row, int col, int kind) {
    if (!t || row < 0 || row >= t->view_count || col < 0 || col >= t->ncols)
        return -1;
    gv_table_row *r = &t->rows[t->view_index[row]];
    if (col >= r->ncols) return -1;
    r->cells[col].kind = kind;
    t->dirty = 1;
    return 0;
}

int gv_table_set_cell_renderer(gv_table *t, int row, int col, int renderer) {
    if (!t || row < 0 || row >= t->view_count || col < 0 || col >= t->ncols)
        return -1;
    gv_table_row *r = &t->rows[t->view_index[row]];
    if (col >= r->ncols) return -1;
    r->cells[col].renderer = renderer;
    t->dirty = 1;
    return 0;
}

int gv_table_resize_column(gv_table *t, int col, int width) {
    if (!t || col < 0 || col >= t->ncols || width < 20) return -1;
    t->cols[col].width = width;
    t->dirty = 1;
    return 0;
}

int gv_table_clear(gv_table *t) {
    if (!t) return -1;
    for (int i = 0; i < t->nrows; i++) {
        for (int c = 0; c < t->rows[i].ncols; c++)
            free(t->rows[i].cells[c].text);
        free(t->rows[i].cells);
    }
    t->nrows = 0;
    t->view_count = 0;
    t->visible_start = 0;
    t->sel_count = 0;
    t->focus_id = -1;
    t->focus_col = 0;
    t->focus_active = 0;
    t->edit_id = -1;
    t->edit_active = 0;
    t->dirty = 1;
    return 0;
}

int gv_table_load_rows(gv_table *t, const char *data) {
    if (!t || !data) return -1;
    gv_table_clear(t);
    char *copy = gv_strdup(data);
    if (!copy) return -1;
    char *p = copy;
    char *row = gv_table_field(&p, ';');
    while (row) {
        if (*row) {
            if (gv_table_insert_row_text(t, t->view_count, row) != 0) {
                free(copy);
                return -1;
            }
        }
        if (!*p) break;
        row = gv_table_field(&p, ';');
    }
    free(copy);
    return 0;
}

static int gv_table_parse_ints(const char *s, int *out, int max) {
    int n = 0;
    char *end = NULL;
    while (*s && n < max) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        long v = strtol(s, &end, 10);
        if (end == s) break;
        out[n++] = (int)v;
        s = end;
    }
    return n;
}

static void gv_table_batch_line(gv_table *t, char *line) {
    while (*line == ' ' || *line == '\t') line++;
    int n = 0;
    int a[4] = {0, 0, 0, 0};
    char *cmd = line;
    char *rest = strchr(line, ' ');
    if (rest) {
        *rest++ = 0;
        n = gv_table_parse_ints(rest, a, 4);
    }
    if (strcmp(cmd, "clear") == 0) {
        gv_table_clear(t);
    } else if (strcmp(cmd, "columns") == 0) {
        gv_table_set_columns(t, rest ? rest : "");
    } else if (strcmp(cmd, "row") == 0) {
        if (rest) gv_table_insert_row_text(t, t->view_count, rest);
    } else if (strcmp(cmd, "sort") == 0 && n >= 2) {
        gv_table_sort(t, a[0], a[1]);
    } else if (strcmp(cmd, "filter") == 0) {
        gv_table_filter(t, rest ? rest : "");
    } else if (strcmp(cmd, "select") == 0 && n >= 1) {
        gv_table_select(t, a[0], 1);
    } else if (strcmp(cmd, "focus") == 0 && n >= 2) {
        gv_table_focus(t, a[0], a[1]);
    } else if (strcmp(cmd, "resize") == 0 && n >= 2) {
        gv_table_resize_column(t, a[0], a[1]);
    } else if (strcmp(cmd, "readonly") == 0 && n >= 3) {
        gv_table_set_readonly(t, a[0], a[1], a[2]);
    } else if (strcmp(cmd, "renderer") == 0 && n >= 3) {
        gv_table_set_cell_renderer(t, a[0], a[1], a[2]);
    } else if (strcmp(cmd, "kind") == 0 && n >= 3) {
        gv_table_set_cell_kind(t, a[0], a[1], a[2]);
    } else if (strcmp(cmd, "edit") == 0 && n >= 2) {
        gv_table_begin_edit(t, a[0], a[1]);
        if (rest) {
            char *p = rest;
            int dummy[4];
            int used = gv_table_parse_ints(p, dummy, 4);
            (void)used;
            char *text = strchr(p, ' ');
            for (int i = 0; i < 2 && text; i++) text = strchr(text + 1, ' ');
            if (text) gv_table_set_edit_text(t, text + 1);
        }
    } else if (strcmp(cmd, "commit") == 0) {
        gv_table_commit_edit(t);
    } else if (strcmp(cmd, "cancel") == 0) {
        gv_table_cancel_edit(t);
    } else if (strcmp(cmd, "scroll") == 0 && n >= 1) {
        gv_table_scroll(t, a[0]);
    }
}

int gv_table_batch(gv_table *t, const char *script) {
    if (!t || !script) return -1;
    char *copy = gv_strdup(script);
    if (!copy) return -1;
    char *save = NULL;
    for (char *line = strtok_r(copy, ";", &save); line;
         line = strtok_r(NULL, ";", &save)) {
        gv_table_batch_line(t, line);
    }
    free(copy);
    gv_table_rebuild_view(t);
    return 0;
}

int gv_table_status(gv_table *t, char *buf, int cap) {
    if (!t || !buf || cap <= 0) return -1;
    int n = snprintf(buf, (size_t)cap,
                     "rows=%d visible=%d selected=%d sort=%d/%d filter=%s focus=%d/%d edit=%d/%d",
                     t->view_count, gv_table_visible_rows(t, 480), t->sel_count,
                     t->sort_col, t->sort_dir, t->has_filter ? t->filter : "",
                     t->focus_id, t->focus_col, t->edit_id, t->edit_active);
    return n >= cap ? -1 : 0;
}

static void gv_table_add_text(gv_display_list *dl, int x, int y, double size,
                              const char *text, gv_color color) {
    if (!text) return;
    gv_cmd c;
    memset(&c, 0, sizeof c);
    c.op = GV_DRAW_TEXT;
    c.rect.x = x;
    c.rect.y = y;
    c.text = text;
    c.text_len = (uint32_t)strlen(text);
    c.f1 = size;
    c.color = color;
    gv_display_list_add(dl, &c);
}

static void gv_table_add_cmd(gv_display_list *dl, gv_draw_op op, int x, int y, int w, int h) {
    gv_cmd c;
    memset(&c, 0, sizeof c);
    c.op = op;
    c.rect.x = x; c.rect.y = y; c.rect.w = w; c.rect.h = h;
    gv_display_list_add(dl, &c);
}

/* A cell's text, fitted to max_w.  The whole UTF-8 string is passed on with
 * the width and an "ellipsize" flag (f4): a backend that shapes text (Pango)
 * shortens it at a character boundary and adds an ellipsis, so a multibyte
 * sequence is never split and CJK / Latin widths are measured, not guessed
 * from byte counts.  The cell is also clipped, so nothing can draw past it. */
static void gv_table_add_text_clipped(gv_display_list *dl, int x, int y,
                                    double size, const char *text,
                                    int max_w, int align, gv_color color) {
    if (!text || max_w <= 0) return;
    gv_table_add_cmd(dl, GV_DRAW_PUSH, 0, 0, 0, 0);
    gv_table_add_cmd(dl, GV_DRAW_CLIP, x, y - 4, max_w, (int)size + 12);
    gv_cmd c;
    memset(&c, 0, sizeof c);
    c.op = GV_DRAW_TEXT;
    c.rect.x = x;
    c.rect.y = y;
    c.rect.w = max_w;
    c.text = text;
    c.text_len = (uint32_t)strlen(text);
    c.f1 = size;
    c.f2 = align == 1 || align == 2 ? (double)align : 0.0;  /* column alignment */
    c.f4 = 1.0;
    c.color = color;
    gv_display_list_add(dl, &c);
    gv_table_add_cmd(dl, GV_DRAW_POP, 0, 0, 0, 0);
}

static void gv_table_add_rect(gv_display_list *dl, int x, int y, int w, int h,
                              gv_color color) {
    gv_cmd c;
    memset(&c, 0, sizeof c);
    c.op = GV_DRAW_RECT;
    c.rect.x = x; c.rect.y = y; c.rect.w = w; c.rect.h = h;
    c.color = color;
    gv_display_list_add(dl, &c);
}

static void gv_table_append_renderer(gv_display_list *dl, gv_view *v,
                                     int renderer, int dx, int dy) {
    if (!renderer || !v) return;
    gv_node *n = gv_view_node(v, (gv_node_id)renderer);
    if (!n) return;
    for (int i = 0; i < n->draw_list.count; i++) {
        gv_cmd c = n->draw_list.cmds[i];
        if (c.op == GV_DRAW_RECT || c.op == GV_DRAW_TEXT ||
            c.op == GV_DRAW_LINE || c.op == GV_DRAW_CLIP) {
            c.rect.x += dx;
            c.rect.y += dy;
        } else if (c.op == GV_DRAW_TRANSFORM) {
            c.transform.e += dx;
            c.transform.f += dy;
        }
        gv_display_list_add(dl, &c);
    }
}

static int gv_table_cell_checked(const char *text) {
    if (!text || !text[0]) return 0;
    return text[0] == '1' || text[0] == 'y' || text[0] == 'Y' ||
           text[0] == 't' || text[0] == 'T';
}

static gv_color gv_table_indicator_color(const char *text) {
    if (text && strcmp(text, "Active") == 0) return (gv_color){70, 180, 110, 255};
    if (text && strcmp(text, "Lapsed") == 0) return (gv_color){210, 90, 90, 255};
    return (gv_color){70, 130, 200, 255};
}

static void gv_table_draw_cell(gv_view *v, gv_display_list *dl,
                               gv_table_row *rowp, int col, int x,
                               int y, int avail, int align, gv_color text_color) {
    gv_table_cell *cell = &rowp->cells[col];
    const char *s = cell->text ? cell->text : "";
    int kind = cell->kind;
    if (cell->renderer) {
        gv_table_append_renderer(dl, v, cell->renderer, x, y);
    }
    if (kind == GV_TABLE_CELL_CHECKBOX) {
        gv_color box = cell->readonly ? (gv_color){90, 96, 104, 255}
                                      : (gv_color){235, 238, 242, 255};
        gv_table_add_rect(dl, x + 6, y + 5, 14, 14, box);
        if (gv_table_cell_checked(s))
            gv_table_add_rect(dl, x + 8, y + 7, 10, 10,
                              (gv_color){70, 130, 200, 255});
        gv_table_add_text_clipped(dl, x + 26, y + 5, 12.0, s, avail - 26, 0, text_color);
    } else if (kind == GV_TABLE_CELL_INDICATOR) {
        gv_table_add_rect(dl, x + 6, y + 6, 12, 12,
                          gv_table_indicator_color(s));
        gv_table_add_text_clipped(dl, x + 24, y + 5, 12.0, s, avail - 24, 0, text_color);
    } else {
        gv_table_add_text_clipped(dl, x + 8, y + 5, 12.0, s, avail - 8, align, text_color);
    }
}

/* ---- text mode ----------------------------------------------------------- */
static gv_table_wrap_fn gv_table_wrapper;
static void *gv_table_wrapper_ctx;
void gv_table_set_wrapper(gv_table_wrap_fn fn, void *ctx) {
    gv_table_wrapper = fn;
    gv_table_wrapper_ctx = ctx;
}

/* the estimate without a shaping backend: CJK and other wide scripts one em,
 * other text 0.55 em; breaks after a space when it can, never inside a
 * UTF-8 sequence */
static int gv_table_wrap_estimate(void *ctx, const char *s, int len, double size,
                                  int width, int *starts, int *lens, int max) {
    (void)ctx;
    int n = 0, row = 0;
    while (row < len && n < max) {
        double w = 0;
        int i = row, last_space = -1;
        while (i < len) {
            unsigned char c = (unsigned char)s[i];
            int k = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
            if (i + k > len) k = len - i;
            double cw = c < 0x80 ? size * 0.55 : (c >= 0xE2 && k >= 3) ? size : size * 0.6;
            if (w + cw > width && i > row) break;
            w += cw;
            if (c == ' ') last_space = i;
            i += k;
        }
        int end = i;
        if (i < len && last_space > row) end = last_space + 1;
        starts[n] = row; lens[n] = end - row; n++;
        row = end;
    }
    return n;
}

int gv_table_text_reset(gv_table *t, double size) {
    if (!t) return -1;
    t->text_mode = 1;
    t->doc_len = 0;
    t->doc_size = size > 0 ? size : 11.0;
    t->row_h = (int)(t->doc_size * 1.45 + 0.5);
    t->header_h = 0;
    t->doc_dirty = 1;
    t->dirty = 1;
    t->doc_top_req = -1;
    return 0;
}

int gv_table_text_top(gv_table *t, int byte) {
    if (!t || !t->text_mode) return -1;
    t->doc_top_req = byte < 0 ? 0 : byte;
    t->dirty = 1;
    return 0;
}

int gv_table_text_top_byte(gv_table *t) {
    if (!t || !t->text_mode || t->visible_start >= t->wrap_count) return 0;
    return t->wrap_start[t->visible_start];
}

int gv_table_text_append(gv_table *t, const char *s, int n, int unit_sep) {
    if (!t || !s || n <= 0) return 0;
    if (t->doc_len + n + 1 > t->doc_cap) {
        int cap = t->doc_cap ? t->doc_cap : 4096;
        while (cap < t->doc_len + n + 1) cap *= 2;
        char *p = (char *)realloc(t->doc, (size_t)cap);
        if (!p) return -1;
        t->doc = p; t->doc_cap = cap;
    }
    for (int i = 0; i < n; i++) t->doc[t->doc_len++] = unit_sep && s[i] == 0x1F ? '\n' : s[i];
    t->doc[t->doc_len] = 0;
    t->doc_dirty = 1;
    t->dirty = 1;
    return 0;
}

static int gv_table_wrap_push(gv_table *t, int start, int len) {
    if (t->wrap_count == t->wrap_cap) {
        int cap = t->wrap_cap ? t->wrap_cap * 2 : 256;
        int *a = (int *)realloc(t->wrap_start, (size_t)cap * sizeof(int));
        if (!a) return -1;
        t->wrap_start = a;
        int *b = (int *)realloc(t->wrap_len, (size_t)cap * sizeof(int));
        if (!b) return -1;
        t->wrap_len = b;
        t->wrap_cap = cap;
    }
    t->wrap_start[t->wrap_count] = start;
    t->wrap_len[t->wrap_count] = len;
    t->wrap_count++;
    return 0;
}

int gv_table_text_layout(gv_table *t, int width) {
    if (!t || !t->text_mode) return 0;
    if (width < 20) width = 20;
    /* the byte to keep on top: one asked for, else (same text) the byte
     * shown at the top before */
    int anchor = t->doc_top_req >= 0 ? t->doc_top_req
               : !t->doc_dirty && t->visible_start < t->wrap_count ? t->wrap_start[t->visible_start] : -1;
    t->doc_top_req = -1;
    if (!t->doc_dirty && width == t->wrap_w) {
        if (anchor < 0) return 0;
        int r = 0;
        while (r + 1 < t->wrap_count && t->wrap_start[r + 1] <= anchor) r++;
        t->visible_start = r;
        return 1;
    }
    gv_table_wrap_fn fn = gv_table_wrapper ? gv_table_wrapper : gv_table_wrap_estimate;
    t->wrap_count = 0;
    int starts[512], lens[512];
    int p = 0;
    while (p <= t->doc_len) {
        int e = p;
        while (e < t->doc_len && t->doc[e] != '\n') e++;
        if (e == p) gv_table_wrap_push(t, p, 0);
        else {
            int done = p;
            while (done < e) {
                int n = fn(gv_table_wrapper_ctx, t->doc + done, e - done, t->doc_size, width, starts, lens, 512);
                if (n <= 0) { gv_table_wrap_push(t, done, e - done); break; }
                for (int i = 0; i < n; i++) gv_table_wrap_push(t, done + starts[i], lens[i]);
                int adv = starts[n - 1] + lens[n - 1];
                if (adv <= 0) { gv_table_wrap_push(t, done, e - done); break; }
                done += adv;
                if (n < 512) break;
            }
        }
        if (e >= t->doc_len) break;
        p = e + 1;
    }
    t->view_count = t->wrap_count;
    if (anchor >= 0) {
        int r = 0;
        while (r + 1 < t->wrap_count && t->wrap_start[r + 1] <= anchor) r++;
        t->visible_start = r;
    }
    t->doc_dirty = 0;
    t->wrap_w = width;
    return 1;
}

int gv_table_text_row(gv_table *t, int i, int *start, int *len) {
    if (!t || !t->text_mode || i < 0 || i >= t->wrap_count) return -1;
    if (start) *start = t->wrap_start[i];
    if (len) *len = t->wrap_len[i];
    return 0;
}

/* the vertical scrollbar of the last rendered viewport */
static void gv_table_draw_scrollbar(gv_display_list *dl, gv_table *t, gv_rect viewport) {
    int sx0 = 0, sx1 = 0, thy = 0, thl = 0;
    if (!gv_table_scrollbar(t, viewport.w, &sx0, &sx1) || !gv_table_thumb(t, &thy, &thl)) return;
    /* track in the header colour; thumb in the text colour, half
     * transparent, so it shows on both light and dark themes; accent
     * while it is dragged */
    gv_color thumb = t->text;
    thumb.a = 150;
    gv_table_add_rect(dl, viewport.x + sx0, viewport.y + t->header_h, sx1 - sx0,
                      viewport.h - t->header_h, t->header);
    gv_table_add_rect(dl, viewport.x + sx0 + 2, viewport.y + thy, sx1 - sx0 - 4, thl,
                      t->bar_active ? t->accent : thumb);
}

/* text mode: the wrapped document, only the rows in view, each the text's
 * own bytes (no ellipsis, nothing cut: rows are whole wrapped lines) */
static int gv_table_render_text(gv_view *v, gv_node_id node, gv_table *t, gv_rect viewport) {
    gv_display_list dl;
    gv_display_list_init(&dl);
    t->view_h = viewport.h;
    gv_table_text_layout(t, viewport.w - 8 - GV_TABLE_BAR_W - 6);
    if (t->visible_start > gv_table_max_start(t)) t->visible_start = gv_table_max_start(t);
    gv_table_add_cmd(&dl, GV_DRAW_PUSH, 0, 0, 0, 0);
    gv_table_add_cmd(&dl, GV_DRAW_CLIP, viewport.x, viewport.y, viewport.w, viewport.h);
    gv_table_add_rect(&dl, viewport.x, viewport.y, viewport.w, viewport.h, t->panel);
    int y = viewport.y + 2;
    for (int r = t->visible_start; r < t->wrap_count && y < viewport.y + viewport.h; r++) {
        if (t->wrap_len[r] > 0) {
            gv_cmd c;
            memset(&c, 0, sizeof c);
            c.op = GV_DRAW_TEXT;
            c.rect.x = viewport.x + 8;
            c.rect.y = y;
            c.text = t->doc + t->wrap_start[r];
            c.text_len = (uint32_t)t->wrap_len[r];
            c.f1 = t->doc_size;
            c.color = t->text;
            gv_display_list_add(&dl, &c);
        }
        y += t->row_h;
    }
    gv_table_draw_scrollbar(&dl, t, viewport);
    gv_table_add_cmd(&dl, GV_DRAW_POP, 0, 0, 0, 0);
    gv_view_set_draw_list(v, node, &dl);
    int ops = dl.count;
    gv_display_list_free(&dl);
    t->dirty = 0;
    return ops;
}

int gv_table_render(gv_view *v, gv_node_id node, gv_table *t, gv_rect viewport) {
    if (!v || !t || !node) return -1;
    if (t->text_mode) return gv_table_render_text(v, node, t, viewport);
    gv_display_list dl;
    gv_display_list_init(&dl);
    gv_color bg = t->bg;
    gv_color panel = t->panel;
    gv_color header = t->header;
    gv_color accent = t->accent;
    gv_color text = t->text;
    gv_color border = t->border;
    gv_color muted = {140, 148, 158, 255};
    /* the table draws only inside its own rectangle (a partial last row used
     * to spill below it) */
    t->view_h = viewport.h;
    if (t->visible_start > gv_table_max_start(t)) t->visible_start = gv_table_max_start(t);
    gv_table_layout_columns(t, viewport.w);
    gv_table_add_cmd(&dl, GV_DRAW_PUSH, 0, 0, 0, 0);
    gv_table_add_cmd(&dl, GV_DRAW_CLIP, viewport.x, viewport.y, viewport.w, viewport.h);
    gv_table_add_rect(&dl, viewport.x, viewport.y, viewport.w, viewport.h, bg);
    int header_y = viewport.y;
    gv_table_add_rect(&dl, viewport.x, header_y, viewport.w,
                      t->ncols > 0 ? t->header_h : 0, header);
    int cx = viewport.x + 8;
    for (int c = 0; c < t->ncols; c++) {
        /* titles are fitted and clipped to their column like cells */
        gv_table_add_text_clipped(&dl, cx, header_y + 8, 13.0, t->cols[c].title,
                                  t->cols[c].width - 12, t->cols[c].align, text);
        cx += t->cols[c].width;
    }
    int y = viewport.y + (t->ncols > 0 ? t->header_h : 0);
    int visible = gv_table_visible_rows(t, viewport.h);
    int focus_row = gv_table_focus_row(t);
    int edit_row = gv_table_find_row(t, t->edit_id);
    for (int r = 0; r < visible; r++) {
        int row = t->visible_start + r;
        if (row < 0 || row >= t->view_count) break;
        gv_table_row *rowp = &t->rows[t->view_index[row]];
        int selected = gv_table_is_selected_id(t, rowp->id);
        gv_color rowbg = selected ? (t->selection.a ? t->selection : accent) : (row % 2 ? t->row_alt : panel);
        gv_table_add_rect(&dl, viewport.x, y, viewport.w, t->row_h, rowbg);
        int cx2 = viewport.x + 8;
        for (int c = 0; c < t->ncols && c < rowp->ncols; c++) {
            gv_color cell_text = rowp->cells[c].readonly ? muted : text;
            int avail = t->cols[c].width - 12;
            gv_table_draw_cell(v, &dl, rowp, c, cx2, y, avail, t->cols[c].align, cell_text);
            cx2 += t->cols[c].width;
        }
        if (t->focus_active && row == focus_row) {
            int fx = viewport.x;
            for (int c = 0; c < t->focus_col && c < t->ncols; c++)
                fx += t->cols[c].width;
            int fw = t->focus_col < t->ncols ? t->cols[t->focus_col].width : 40;
            gv_table_add_rect(&dl, fx, y, fw, 2, accent);
            gv_table_add_rect(&dl, fx, y + t->row_h - 2, fw, 2, accent);
            gv_table_add_rect(&dl, fx, y, 2, t->row_h, accent);
            gv_table_add_rect(&dl, fx + fw - 2, y, 2, t->row_h, accent);
        }
        if (t->edit_active && row == edit_row) {
            int ex = viewport.x;
            for (int c = 0; c < t->edit_col && c < t->ncols; c++)
                ex += t->cols[c].width;
            int ew = t->edit_col < t->ncols ? t->cols[t->edit_col].width : 80;
            gv_table_add_rect(&dl, ex, y, ew, t->row_h, (gv_color){30, 34, 42, 255});
            gv_table_add_text(&dl, ex + 8, y + 5, 12.0, t->edit_text, text);
            gv_table_add_rect(&dl, ex, y, ew, 2, accent);
            gv_table_add_rect(&dl, ex, y + t->row_h - 2, ew, 2, accent);
            gv_table_add_rect(&dl, ex, y, 2, t->row_h, accent);
            gv_table_add_rect(&dl, ex + ew - 2, y, 2, t->row_h, accent);
        }
        y += t->row_h;
    }
    if (t->show_grid && t->ncols > 0) {
        int gx = viewport.x;
        for (int c = 0; c < t->ncols; c++) {
            gx += t->cols[c].width;
            gv_table_add_rect(&dl, gx, viewport.y, 1, viewport.h, border);
        }
        int gy = viewport.y + t->header_h;
        int rows = gv_table_visible_rows(t, viewport.h);
        for (int r = 0; r <= rows; r++) {
            gv_table_add_rect(&dl, viewport.x, gy, viewport.w, 1, border);
            gy += t->row_h;
        }
    }
    /* the header divider under the pointer or being dragged */
    if (t->col_hot >= 0 && t->col_hot < t->ncols)
        gv_table_add_rect(&dl, viewport.x + gv_table_col_x(t, t->col_hot + 1) - 1, viewport.y, 3,
                          t->header_h, accent);
    /* vertical scrollbar: track and a thumb sized and placed by the visible
     * share of the rows (a click on the track pages, gv_glon_host) */
    gv_table_draw_scrollbar(&dl, t, viewport);
    gv_table_add_cmd(&dl, GV_DRAW_POP, 0, 0, 0, 0);
    gv_view_set_draw_list(v, node, &dl);
    int ops = dl.count;
    gv_display_list_free(&dl);
    t->dirty = 0;
    return ops;
}
