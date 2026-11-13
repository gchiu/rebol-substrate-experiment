#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
/* Glon Paint native host (SDL2 + libpng).
 *
 * Glon owns all application state and decisions. This host only:
 *   - creates/tears down an SDL2 window, renderer and persistent canvas;
 *   - translates SDL mouse/key events into calls to the loaded Glon words
 *     paint/down, paint/move, paint/up and paint/key;
 *   - executes the small command strings those Glon words return.
 *
 * Commands (one per newline):
 *   L x1 y1 x2 y2 size r g b
 *   E x1 y1 x2 y2 size r g b
 *   C
 *   T title text
 *   I color size tool drawing
 *   S path.png
 *   Q
 *
 * Paint state and paint-policy decisions stay in Glon. This file only chooses
 * desktop/UI placement and transports events and command strings.
 */
#include "r0_s1.h"
#include "s1.h"
#include <SDL.h>
#include <SDL_image.h>
#include <png.h>
#ifdef HAVE_NFD
#include "third_party/nfd/nfd.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>
#include <limits.h>
#include <unistd.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_SDL_RENDERER_IMPLEMENTATION
#include "third_party/nuklear.h"
#include "third_party/nuklear_sdl_renderer.h"

#define PAINT_W 800
#define PAINT_H 600
#define PAINT_ROOT_DEFAULT "../.."

#define UI_MENU_H   36
#define UI_STATUS_H 32
#define UI_TOOL_W   124
#define UI_RIGHT_W  252
#define UI_GAP      8
#define UI_MARGIN   24
#define HISTORY_LIMIT 16
#define SCRIPT_MAX_BYTES 262144
#define SCRIPT_CMD_BYTES (1024 * 1024)
#define REPLAY_MAX_COMMANDS 4096

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *background;
static SDL_Texture *canvas;
static SDL_Texture *composite;
static struct nk_context *nk;
static int dirty = 1;
static int quit_flag = 0;
static int ui_color = 1;
static int ui_size = 4;
static int ui_tool = 1;
static int ui_drawing = 0;
static int ui_call_diameter = 24;
static int ui_pressure = 180;
static int ui_opacity = 255;
static int ui_smooth_strokes = 1;
static int ui_show_brush_tip = 1;
static int ui_stroke_duration_index = 2;  /* 600 ms */
static int ui_fill_shapes = 0;
static int ui_bucket_tolerance = 0;
static int bucket_last_commands = 0;
static size_t bucket_last_bytes = 0;
static char bucket_last_text[SCRIPT_CMD_BYTES];
static char bucket_message[160] = "";
static int shape_active = 0;
static int shape_tool = 0;
static int shape_start_x = 0, shape_start_y = 0;
static int shape_end_x = 0, shape_end_y = 0;
static int shape_shift = 0;
static int shape_last_commands = 0;
static size_t shape_last_bytes = 0;
static char shape_last_text[SCRIPT_CMD_BYTES];
static const int ui_stroke_durations[] = { 150, 300, 600, 1200 };
static int show_about = 0;
static long perf_raw_motions = 0, perf_app_moves = 0, perf_renders = 0;
typedef struct { Uint32 *pixels; } HistoryEntry;
typedef struct { HistoryEntry items[HISTORY_LIMIT]; int count; } HistoryStack;
static HistoryStack undo_stack, redo_stack;
static Uint32 *stroke_snapshot = NULL;
static int unsaved = 0;
static int open_dialog = 0;
static int confirm_open = 0;
static char open_path[1024] = "";
static char open_error[256] = "";
static int script_dialog = 0;
static int script_loaded = 0;
static char script_path[1024] = "";
static char script_error[256] = "";
static char script_message[256] = "";
static char *script_source = NULL;
static size_t script_source_len = 0;
static int history_replaying = 0;
static int replay_busy(void);
static int replay_nav_available(void);
static int replay_locked(void);
typedef enum {
    REPLAY_READY = 0, REPLAY_PLAYING, REPLAY_PAUSED, REPLAY_COMPLETE,
    REPLAY_ENDED, REPLAY_ERROR
} ReplayState;
static ReplayState replay_state = REPLAY_READY;
static char replay_buf[SCRIPT_CMD_BYTES];
static int replay_offsets[REPLAY_MAX_COMMANDS];
static int replay_lengths[REPLAY_MAX_COMMANDS];
static int replay_count = 0;
static int replay_pos = 0;
static int replay_prepared = 0;
static int replay_active = 0;
static Uint32 *replay_pre = NULL;
static int replay_unsaved = 0;
static Uint64 replay_last_tick = 0;
static Uint64 replay_accum_ns = 0;
static int replay_speed_index = 1;  /* 5 operations/second */
static const int replay_speeds[] = { 1, 5, 20, 60 };
static char replay_error[256] = "";
typedef struct {
    int x1, y1, p1, x2, y2, p2, dia, op, r, g, b;
} PressureCommand;
static int replay_animating = 0;
static PressureCommand replay_anim_cmd;
static Uint32 *replay_cmd_pre = NULL;   /* canvas immediately before this command */
static Uint32 *replay_anim_work = NULL; /* reusable partial-frame buffer */
static Uint64 replay_anim_elapsed_ms = 0;
static int replay_anim_duration_ms = 600;
static int replay_anim_progress = 0;     /* 0..999, presentation only */
static int replay_anim_x = 0, replay_anim_y = 0, replay_anim_diameter = 0;
static long replay_anim_frames = 0;
static long replay_anim_uploads = 0;
static Uint64 replay_anim_total_ns = 0;
static Uint64 replay_anim_worst_ns = 0;
#ifdef HAVE_NFD
static int nfd_state = 0;  /* 0 unknown, 1 ready, -1 unavailable */
static char last_image_dir[1024] = "";
static char last_script_dir[1024] = "";
#endif
static SDL_Rect ui_brush_bounds, ui_eraser_bounds, ui_calligraphy_bounds;
static SDL_Rect ui_line_bounds, ui_rect_bounds, ui_ellipse_bounds;
static SDL_Rect ui_bucket_bounds, ui_bucket_tol_bounds;
static SDL_Rect ui_color_bounds[4];
static SDL_Rect ui_size_bounds;

typedef struct {
    SDL_Rect menu, toolbar, workspace, right, layers, status, image;
} Layout;
static Layout layout;

static int read_file(const char *path, char **out) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "glon-paint: cannot open %s\n", path); return -1; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    char *s = malloc((size_t)n + 1);
    if (!s) { fclose(f); return -1; }
    if (fread(s, 1, (size_t)n, f) != (size_t)n) { free(s); fclose(f); return -1; }
    s[n] = 0; fclose(f); *out = s; return 0;
}

static int load_source(const char *root, const char *rel) {
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    char *src = NULL;
    if (read_file(path, &src) != 0) return -1;
    /* The native CLI strips ;; comments before parsing. Keep the same loader
     * contract here; inline single ';' comments are not part of Glon. */
    char *r = src, *w = src;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
    int perr = 0;
    cell prog = r0_s1_parse(src, &perr);
    free(src);
    if (perr) {
        fprintf(stderr, "glon-paint: parse failed: %s (%d)\n", rel, perr);
        return -1;
    }
    r0_s1_outcome oc;
    r0_s1_session_run_block(prog, &oc);
    if (oc.status != R0S1_OUT_OK) {
        fprintf(stderr, "glon-paint: load failed: %s: %s/%s\n", rel,
                r0_s1_outcome_status_name(oc.status),
                r0_s1_outcome_detail_name(oc.detail));
        return -1;
    }
    return 0;
}

/* Call a Glon word, copying its returned STRING! before the next invocation. */
static int glon_call(const char *src, char *out, size_t cap) {
    /* Each SDL event is a throw-away source cell. Its parsed block lives in
     * the permanent loader heap, so save the frontier and restore it after
     * copying the result. This is the same scratch-dispatch contract used by
     * r0_s1_g1a_live.c. The loaded app words and managed result are not
     * loader objects, so this reclaims only the transient parse. */
    cell save_lhp = M[GC_LOADER_HP];
    r0_s1_outcome oc;
    int rc = r0_s1_session_run(src, (unsigned)strlen(src), &oc);
    out[0] = 0;
    int failed = 0;
    if (rc != R0S1_OUT_OK || oc.count < 1) {
        fprintf(stderr, "glon-paint: %s: %s/%s\n", src,
                r0_s1_outcome_status_name(oc.status),
                r0_s1_outcome_detail_name(oc.detail));
        failed = 1;
    } else {
        cell v = r0_s1_result(0, oc.count);
        if ((v & 15) != T_STRING) {
            fprintf(stderr, "glon-paint: %s did not return a string\n", src);
            failed = 1;
        } else {
            cell p = v & ~(cell)15;
            long n = (long)M[p];
            if (n < 0 || (size_t)n >= cap) n = (long)cap - 1;
            for (long i = 0; i < n; i++) out[i] = (char)(M[p + 1 + i] & 255);
            out[n] = 0;
        }
    }
    M[GC_LOADER_HP] = save_lhp;
    return failed ? -1 : 0;
}

static void set_color(int r, int g, int b, int a) {
    SDL_SetRenderDrawColor(ren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a);
}
static void begin_canvas(void) { SDL_SetRenderTarget(ren, canvas); }
static void clear_background(void) {
    SDL_SetRenderTarget(ren, background);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_RenderClear(ren);
    SDL_SetRenderTarget(ren, NULL);
}

/* ---- G2 layout and image/canvas geometry -------------------------------- */
static int point_in_rect(const SDL_Rect *r, int x, int y) {
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}
static void fit_image(int ww, int wh, SDL_Rect *image) {
    int ws_x = UI_TOOL_W;
    int ws_y = UI_MENU_H;
    int ws_w = ww - UI_TOOL_W - UI_RIGHT_W;
    int ws_h = wh - UI_MENU_H - UI_STATUS_H;
    if (ws_w < 2 * UI_MARGIN) ws_w = 2 * UI_MARGIN;
    if (ws_h < 2 * UI_MARGIN) ws_h = 2 * UI_MARGIN;
    int max_w = ws_w - 2 * UI_MARGIN;
    int max_h = ws_h - 2 * UI_MARGIN;
    double sx = (double)max_w / PAINT_W;
    double sy = (double)max_h / PAINT_H;
    double scale = sx < sy ? sx : sy;
    if (scale < 0) scale = 0;
    image->w = (int)(PAINT_W * scale);
    image->h = (int)(PAINT_H * scale);
    if (image->w < 1) image->w = 1;
    if (image->h < 1) image->h = 1;
    image->x = ws_x + (ws_w - image->w) / 2;
    image->y = ws_y + (ws_h - image->h) / 2;
}
static void update_layout(void) {
    int ww = 0, wh = 0;
    SDL_GetWindowSize(win, &ww, &wh);
    if (ww < 1) ww = 1;
    if (wh < 1) wh = 1;
    int avail_h = wh - UI_MENU_H - UI_STATUS_H;
    if (avail_h < 1) avail_h = 1;
    layout.menu = (SDL_Rect){ 0, 0, ww, UI_MENU_H };
    layout.status = (SDL_Rect){ 0, wh - UI_STATUS_H, ww, UI_STATUS_H };
    layout.toolbar = (SDL_Rect){ 0, UI_MENU_H, UI_TOOL_W, avail_h };
    int right_h = (avail_h * 3) / 4;
    if (right_h < 1) right_h = 1;
    if (right_h > avail_h - UI_GAP) right_h = avail_h - UI_GAP;
    if (right_h < 1) right_h = 1;
    layout.right = (SDL_Rect){ ww - UI_RIGHT_W, UI_MENU_H, UI_RIGHT_W, right_h };
    int layer_y = UI_MENU_H + right_h + UI_GAP;
    int layer_h = wh - UI_STATUS_H - layer_y;
    if (layer_h < 1) layer_h = 1;
    layout.layers = (SDL_Rect){ ww - UI_RIGHT_W, layer_y, UI_RIGHT_W, layer_h };
    int ws_w = ww - UI_TOOL_W - UI_RIGHT_W;
    if (ws_w < 1) ws_w = 1;
    layout.workspace = (SDL_Rect){ UI_TOOL_W, UI_MENU_H, ws_w, avail_h };
    fit_image(ww, wh, &layout.image);
}
static int map_window_to_canvas_for(int ww, int wh, int wx, int wy, int *cx, int *cy) {
    SDL_Rect image;
    fit_image(ww, wh, &image);
    if (!point_in_rect(&image, wx, wy)) return 0;
    int x = (int)(((int64_t)(wx - image.x) * PAINT_W) / image.w);
    int y = (int)(((int64_t)(wy - image.y) * PAINT_H) / image.h);
    if (x < 0) x = 0;
    if (x >= PAINT_W) x = PAINT_W - 1;
    if (y < 0) y = 0;
    if (y >= PAINT_H) y = PAINT_H - 1;
    *cx = x; *cy = y;
    return 1;
}
static int map_window_to_canvas(int ww, int wh, int wx, int wy, int *cx, int *cy) {
    return map_window_to_canvas_for(ww, wh, wx, wy, cx, cy);
}
static int map_window_to_canvas_clamped(int ww, int wh, int wx, int wy,
                                        int *cx, int *cy) {
    SDL_Rect image;
    fit_image(ww, wh, &image);
    if (wx < image.x) wx = image.x;
    if (wy < image.y) wy = image.y;
    if (wx >= image.x + image.w) wx = image.x + image.w - 1;
    if (wy >= image.y + image.h) wy = image.y + image.h - 1;
    int x = (int)(((int64_t)(wx - image.x) * PAINT_W) / image.w);
    int y = (int)(((int64_t)(wy - image.y) * PAINT_H) / image.h);
    if (x < 0) x = 0;
    if (x >= PAINT_W) x = PAINT_W - 1;
    if (y < 0) y = 0;
    if (y >= PAINT_H) y = PAINT_H - 1;
    *cx = x; *cy = y;
    return 1;
}
static int canvas_to_window(int cx, int cy, int *wx, int *wy) {
    if (layout.image.w <= 0 || layout.image.h <= 0) return 0;
    *wx = layout.image.x + (int)((int64_t)cx * layout.image.w / PAINT_W);
    *wy = layout.image.y + (int)((int64_t)cy * layout.image.h / PAINT_H);
    return 1;
}
static int point_in_ui(int x, int y) {
    if (show_about || open_dialog || confirm_open || script_dialog) return 1;
    return point_in_rect(&layout.menu, x, y) ||
           point_in_rect(&layout.toolbar, x, y) ||
           point_in_rect(&layout.right, x, y) ||
           point_in_rect(&layout.layers, x, y) ||
           point_in_rect(&layout.status, x, y) ||
           (nk && (int)nk->input.mouse.pos.x == x &&
                  (int)nk->input.mouse.pos.y == y &&
                  nk_window_is_any_hovered(nk));
}
static void clear_canvas(void) {
    begin_canvas();
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
    SDL_RenderClear(ren);
    dirty = 1;
}
static void draw_line(int x1, int y1, int x2, int y2, int size,
                      int r, int g, int b, int a) {
    begin_canvas();
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    set_color(r, g, b, a);
    int radius = size > 1 ? size / 2 : 0;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (radius == 0 || dx * dx + dy * dy <= radius * radius)
                SDL_RenderDrawLine(ren, x1 + dx, y1 + dy, x2 + dx, y2 + dy);
        }
    }
    dirty = 1;
}
static void build_composite(void) {
    SDL_SetRenderTarget(ren, composite);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_RenderClear(ren);
    SDL_SetTextureBlendMode(background, SDL_BLENDMODE_NONE);
    SDL_RenderCopy(ren, background, NULL, NULL);
    SDL_SetTextureBlendMode(canvas, SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(ren, canvas, NULL, NULL);
    SDL_SetRenderTarget(ren, NULL);
}
static void draw_circle_outline(int cx, int cy, int radius, int r, int g, int b) {
    if (radius < 1) radius = 1;
    SDL_SetRenderDrawColor(ren, (Uint8)r, (Uint8)g, (Uint8)b, 255);
    int x = radius, y = 0, err = 1 - radius;
    while (x >= y) {
        SDL_RenderDrawPoint(ren, cx + x, cy + y);
        SDL_RenderDrawPoint(ren, cx + y, cy + x);
        SDL_RenderDrawPoint(ren, cx - y, cy + x);
        SDL_RenderDrawPoint(ren, cx - x, cy + y);
        SDL_RenderDrawPoint(ren, cx - x, cy - y);
        SDL_RenderDrawPoint(ren, cx - y, cy - x);
        SDL_RenderDrawPoint(ren, cx + y, cy - x);
        SDL_RenderDrawPoint(ren, cx + x, cy - y);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}
/* ---- geometric tools ----------------------------------------------------
 * Shapes are committed as ordinary L commands. The host owns only the integer
 * geometry/presentation; the Glon command dispatcher remains the only drawing
 * path. */
typedef struct { int minx, miny, maxx, maxy; } ShapeBox;
static void palette_rgb(int c, int *r, int *g, int *b) {
    switch (c) {
    case 2: *r = 40;  *g = 120; *b = 220; break;
    case 3: *r = 40;  *g = 160; *b = 80;  break;
    case 4: *r = 230; *g = 170; *b = 40;  break;
    case 5: *r = 0;   *g = 0;   *b = 0;   break;
    default: *r = 220; *g = 60; *b = 60; break;
    }
}
static int shape_clamp_x(int x) {
    if (x < 0) return 0;
    if (x >= PAINT_W) return PAINT_W - 1;
    return x;
}
static int shape_clamp_y(int y) {
    if (y < 0) return 0;
    if (y >= PAINT_H) return PAINT_H - 1;
    return y;
}
static int shape_abs(int v) { return v < 0 ? -v : v; }
static void shape_constrain_line(int x1, int y1, int *x2, int *y2, int shift) {
    if (!shift) return;
    int dx = *x2 - x1, dy = *y2 - y1;
    int adx = shape_abs(dx), ady = shape_abs(dy);
    int sx = dx >= 0 ? 1 : -1, sy = dy >= 0 ? 1 : -1;
    if (adx >= 2 * ady) {
        *y2 = y1;
    } else if (ady >= 2 * adx) {
        *x2 = x1;
    } else {
        int len = adx < ady ? adx : ady;
        *x2 = x1 + sx * len;
        *y2 = y1 + sy * len;
    }
}
static void shape_constrain_square(int x1, int y1, int *x2, int *y2, int shift) {
    if (!shift) return;
    int dx = *x2 - x1, dy = *y2 - y1;
    int adx = shape_abs(dx), ady = shape_abs(dy);
    int side = adx > ady ? adx : ady;
    int sx = dx >= 0 ? 1 : -1, sy = dy >= 0 ? 1 : -1;
    int room_x = sx > 0 ? PAINT_W - 1 - x1 : x1;
    int room_y = sy > 0 ? PAINT_H - 1 - y1 : y1;
    if (side > room_x) side = room_x;
    if (side > room_y) side = room_y;
    *x2 = x1 + sx * side;
    *y2 = y1 + sy * side;
}
static void shape_make_box(int x1, int y1, int x2, int y2, ShapeBox *b) {
    b->minx = shape_clamp_x(x1 < x2 ? x1 : x2);
    b->maxx = shape_clamp_x(x1 > x2 ? x1 : x2);
    b->miny = shape_clamp_y(y1 < y2 ? y1 : y2);
    b->maxy = shape_clamp_y(y1 > y2 ? y1 : y2);
}
/* Largest positive x whose pixel centre is inside the ellipse. */
static int ellipse_extent(int rx, int ry, int ay) {
    if (rx <= 0 || ry <= 0 || ay < 0 || ay > ry) return -1;
    int64_t a = (int64_t)rx * rx;
    int64_t bb = (int64_t)ry * ry;
    int64_t rhs = a * bb;
    int lo = 0, hi = rx, best = 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int64_t lhs = (int64_t)mid * mid * bb + (int64_t)ay * ay * a;
        if (lhs <= rhs) { best = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    return best;
}
/* Trace an ellipse boundary as a bounded polygon. Points are in canvas
 * coordinates; the caller connects consecutive points. */
static int ellipse_polygon(const ShapeBox *b, int *xs, int *ys, int cap) {
    int cx = (b->minx + b->maxx) / 2;
    int cy = (b->miny + b->maxy) / 2;
    int rx = (b->maxx - b->minx) / 2;
    int ry = (b->maxy - b->miny) / 2;
    if (rx < 1 || ry < 1 || cap < 4) return 0;
    const int steps = 36;
    int n = 0;
    for (int k = 0; k <= steps && n < cap; k++) {
        int off = -ry + (int)((int64_t)2 * ry * k / steps);
        int x = ellipse_extent(rx, ry, shape_abs(off));
        xs[n] = cx + x; ys[n] = cy + off; n++;
    }
    for (int k = steps - 1; k >= 1 && n < cap; k--) {
        int off = -ry + (int)((int64_t)2 * ry * k / steps);
        int x = ellipse_extent(rx, ry, shape_abs(off));
        xs[n] = cx - x; ys[n] = cy + off; n++;
    }
    return n;
}
static void preview_canvas_line(int x1, int y1, int x2, int y2,
                                int r, int g, int b) {
    int wx1 = 0, wy1 = 0, wx2 = 0, wy2 = 0;
    if (!canvas_to_window(x1, y1, &wx1, &wy1) ||
        !canvas_to_window(x2, y2, &wx2, &wy2)) return;
    SDL_SetRenderDrawColor(ren, (Uint8)r, (Uint8)g, (Uint8)b, 255);
    SDL_RenderDrawLine(ren, wx1, wy1, wx2, wy2);
}
static void draw_shape_preview(void) {
    if (!shape_active) return;
    if (layout.image.w <= 0 || layout.image.h <= 0) return;
    int x2 = shape_end_x, y2 = shape_end_y;
    int x1 = shape_start_x, y1 = shape_start_y;
    int r = 0, g = 0, b = 0;
    palette_rgb(ui_color, &r, &g, &b);
    SDL_RenderSetClipRect(ren, &layout.image);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    if (shape_tool == 4) {
        shape_constrain_line(x1, y1, &x2, &y2, shape_shift);
        preview_canvas_line(x1, y1, x2, y2, r, g, b);
    } else {
        shape_constrain_square(x1, y1, &x2, &y2, shape_shift);
        ShapeBox box;
        shape_make_box(x1, y1, x2, y2, &box);
        if (shape_tool == 5) {
            int wx0 = 0, wy0 = 0, wx1 = 0, wy1 = 0;
            if (canvas_to_window(box.minx, box.miny, &wx0, &wy0) &&
                canvas_to_window(box.maxx + 1, box.maxy + 1, &wx1, &wy1)) {
                SDL_Rect rr = { wx0, wy0, wx1 - wx0, wy1 - wy0 };
                if (ui_fill_shapes) {
                    SDL_SetRenderDrawColor(ren, (Uint8)r, (Uint8)g, (Uint8)b, 170);
                    SDL_RenderFillRect(ren, &rr);
                } else {
                    SDL_SetRenderDrawColor(ren, (Uint8)r, (Uint8)g, (Uint8)b, 255);
                    SDL_RenderDrawRect(ren, &rr);
                }
            }
        } else if (shape_tool == 6) {
            int cx = (box.minx + box.maxx) / 2;
            int cy = (box.miny + box.maxy) / 2;
            int rx = (box.maxx - box.minx) / 2;
            int ry = (box.maxy - box.miny) / 2;
            if (rx >= 1 && ry >= 1) {
                if (ui_fill_shapes) {
                    SDL_SetRenderDrawColor(ren, (Uint8)r, (Uint8)g, (Uint8)b, 170);
                    for (int y = box.miny; y <= box.maxy; y++) {
                        int ext = ellipse_extent(rx, ry, shape_abs(y - cy));
                        if (ext < 0) continue;
                        int wx0 = 0, wy0 = 0, wx1 = 0, wy1 = 0;
                        if (!canvas_to_window(cx - ext, y, &wx0, &wy0) ||
                            !canvas_to_window(cx + ext + 1, y + 1, &wx1, &wy1))
                            continue;
                        SDL_Rect rr = { wx0, wy0, wx1 - wx0, wy1 - wy0 };
                        SDL_RenderFillRect(ren, &rr);
                    }
                } else {
                    int xs[96], ys[96];
                    int n = ellipse_polygon(&box, xs, ys, 96);
                    for (int i = 0; i < n; i++) {
                        int j = (i + 1) % n;
                        preview_canvas_line(xs[i], ys[i], xs[j], ys[j], r, g, b);
                    }
                }
            }
        }
    }
    SDL_RenderSetClipRect(ren, NULL);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
}

/* Presentation-only brush tip. It is drawn directly to the window after the
 * canvas, never into the drawing layer, composite or exported PNG. */
static void draw_brush_tip_indicator(void) {
    if (!ui_show_brush_tip || !replay_animating || replay_anim_diameter <= 0)
        return;
    if (layout.image.w <= 0 || layout.image.h <= 0) return;
    int wx = 0, wy = 0;
    if (!canvas_to_window(replay_anim_x, replay_anim_y, &wx, &wy)) return;
    int scaled = (int)((int64_t)replay_anim_diameter * layout.image.w / PAINT_W);
    int radius = scaled / 2;
    if (radius < 2) radius = 2;
    draw_circle_outline(wx, wy, radius + 1, 0, 0, 0);
    draw_circle_outline(wx, wy, radius, 255, 255, 255);
}
static void render_frame(void) {
    update_layout();
    SDL_SetRenderTarget(ren, NULL);
    SDL_RenderSetViewport(ren, NULL);
    SDL_RenderSetClipRect(ren, NULL);
    SDL_SetRenderDrawColor(ren, 22, 23, 27, 255);
    SDL_RenderClear(ren);
    SDL_SetRenderDrawColor(ren, 43, 45, 51, 255);
    SDL_RenderFillRect(ren, &layout.workspace);
    if (layout.image.w > 0 && layout.image.h > 0) {
        SDL_Rect shadow = { layout.image.x - 3, layout.image.y - 3,
                            layout.image.w + 6, layout.image.h + 6 };
        SDL_SetRenderDrawColor(ren, 8, 9, 11, 255);
        SDL_RenderFillRect(ren, &shadow);
        SDL_SetTextureBlendMode(background, SDL_BLENDMODE_NONE);
        SDL_RenderCopy(ren, background, NULL, &layout.image);
        SDL_SetTextureBlendMode(canvas, SDL_BLENDMODE_BLEND);
        SDL_RenderCopy(ren, canvas, NULL, &layout.image);
        SDL_SetRenderDrawColor(ren, 92, 96, 106, 255);
        SDL_RenderDrawRect(ren, &layout.image);
    }
    draw_shape_preview();
    draw_brush_tip_indicator();
    dirty = 0;
}
static void present_frame(void) {
    render_frame();
    SDL_RenderPresent(ren);
}
static void render_ui(void) {
    render_frame();
    if (nk) nk_sdl_render(NK_ANTI_ALIASING_ON);
}
static void present_ui(void) {
    render_ui();
    SDL_RenderPresent(ren);
}

static int save_png(const char *path) {
    build_composite();
    SDL_SetRenderTarget(ren, composite);
    Uint32 *pixels = malloc((size_t)PAINT_W * PAINT_H * sizeof(Uint32));
    if (!pixels) { SDL_SetRenderTarget(ren, NULL); return -1; }
    if (SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888,
                             pixels, PAINT_W * sizeof(Uint32)) != 0) {
        SDL_SetRenderTarget(ren, NULL);
        free(pixels); return -1;
    }
    SDL_SetRenderTarget(ren, NULL);
    FILE *f = fopen(path, "wb");
    if (!f) { free(pixels); return -1; }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png ? png_create_info_struct(png) : NULL;
    if (!png || !info) { if (png) png_destroy_write_struct(&png, NULL); fclose(f); free(pixels); return -1; }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info); fclose(f); free(pixels); return -1;
    }
    png_init_io(png, f);
    png_set_IHDR(png, info, PAINT_W, PAINT_H, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    png_bytep row = malloc((size_t)PAINT_W * 4);
    if (!row) { png_destroy_write_struct(&png, &info); fclose(f); free(pixels); return -1; }
    SDL_PixelFormat *fmt = SDL_AllocFormat(SDL_PIXELFORMAT_ARGB8888);
    for (int y = 0; y < PAINT_H; y++) {
        for (int x = 0; x < PAINT_W; x++) {
            Uint8 r, g, b, a;
            SDL_GetRGBA(pixels[y * PAINT_W + x], fmt, &r, &g, &b, &a);
            row[x * 4 + 0] = r; row[x * 4 + 1] = g; row[x * 4 + 2] = b; row[x * 4 + 3] = a;
        }
        png_write_row(png, row);
    }
    SDL_FreeFormat(fmt);
    free(row); free(pixels);
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    fclose(f);
    fprintf(stderr, "glon-paint: saved %s\n", path);
    return 0;
}

/* ---- bounded canvas history --------------------------------------------- */
/* Snapshot history: one pre-operation 800x600 ARGB image per entry. This is
 * deliberately not a recorded-command history; it is the safe fallback for
 * exact canvas undo until the scripting milestone can record/replay commands. */
static int read_canvas_into(Uint32 *px) {
    begin_canvas();
    return SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888,
                                px, PAINT_W * sizeof(Uint32));
}
static Uint32 *snapshot_canvas(void) {
    Uint32 *px = malloc((size_t)PAINT_W * PAINT_H * sizeof(Uint32));
    if (!px) return NULL;
    if (read_canvas_into(px) != 0) {
        free(px);
        return NULL;
    }
    return px;
}
static int restore_canvas(const Uint32 *px) {
    /* UpdateTexture must not run while the canvas is bound as the render
     * target; WSLg's GL renderer otherwise leaves stale readback data. */
    SDL_SetRenderTarget(ren, NULL);
    if (SDL_UpdateTexture(canvas, NULL, px, PAINT_W * sizeof(Uint32)) != 0) return -1;
    dirty = 1;
    return 0;
}
static int pressure_diameter(int max_diameter, int pressure) {
    if (max_diameter < 1) max_diameter = 1;
    if (pressure < 0) pressure = 0;
    if (pressure > 255) pressure = 255;
    return (max_diameter * pressure + 127) / 255;
}
/* Deterministic integer calligraphy segment. Coordinates are pixel centres
 * (2*x+1, 2*y+1 in doubled units); the diameter at a projected point is
 * interpolated linearly and rounded to nearest. Coverage is hard-edged; the
 * resulting alpha is `opacity`, and overlapping coverage uses max-alpha rather
 * than repeated source-over so joins do not darken. */
static void raster_pressure_line(Uint32 *pixels,
                                 int x1, int y1, int p1, int x2, int y2, int p2,
                                 int max_diameter, int opacity,
                                 int r, int g, int b) {
    if (opacity <= 0 || max_diameter <= 0) return;
    int d1 = pressure_diameter(max_diameter, p1);
    int d2 = pressure_diameter(max_diameter, p2);
    if (d1 <= 0 && d2 <= 0) return;
    int maxd = d1 > d2 ? d1 : d2;
    int pad = (maxd + 2) / 2 + 1;
    int minx = (x1 < x2 ? x1 : x2) - pad;
    int maxx = (x1 > x2 ? x1 : x2) + pad;
    int miny = (y1 < y2 ? y1 : y2) - pad;
    int maxy = (y1 > y2 ? y1 : y2) + pad;
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= PAINT_W) maxx = PAINT_W - 1;
    if (maxy >= PAINT_H) maxy = PAINT_H - 1;
    int64_t ax = (int64_t)x1 * 2, ay = (int64_t)y1 * 2;
    int64_t bx = (int64_t)x2 * 2, by = (int64_t)y2 * 2;
    int64_t dx = bx - ax, dy = by - ay;
    int64_t len2 = dx * dx + dy * dy;
    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            int64_t sx = (int64_t)x * 2 + 1, sy = (int64_t)y * 2 + 1;
            int64_t t = 0;
            if (len2 > 0) {
                t = (sx - ax) * dx + (sy - ay) * dy;
                if (t < 0) t = 0;
                if (t > len2) t = len2;
            }
            int64_t nx = ax, ny = ay;
            if (len2 > 0) {
                nx += dx * t / len2;
                ny += dy * t / len2;
            }
            int diam = len2 > 0
                ? (int)(((int64_t)d1 * (len2 - t) + (int64_t)d2 * t + len2 / 2) / len2)
                : d1;
            if (diam <= 0) continue;
            int64_t ddx = sx - nx, ddy = sy - ny;
            if (ddx * ddx + ddy * ddy <= (int64_t)diam * diam) {
                Uint32 *q = &pixels[(size_t)y * PAINT_W + x];
                int olda = (int)((*q >> 24) & 255);
                if (opacity > olda || (opacity == olda && opacity > 0))
                    *q = ((Uint32)opacity << 24) | ((Uint32)r << 16) |
                         ((Uint32)g << 8) | (Uint32)b;
            }
        }
    }
}
static void draw_pressure_line(int x1, int y1, int p1, int x2, int y2, int p2,
                               int max_diameter, int opacity,
                               int r, int g, int b) {
    if (opacity <= 0 || max_diameter <= 0) return;
    if (pressure_diameter(max_diameter, p1) <= 0 &&
        pressure_diameter(max_diameter, p2) <= 0) return;
    Uint32 *pixels = snapshot_canvas();
    if (!pixels) return;
    raster_pressure_line(pixels, x1, y1, p1, x2, y2, p2,
                         max_diameter, opacity, r, g, b);
    SDL_UpdateTexture(canvas, NULL, pixels, PAINT_W * sizeof(Uint32));
    free(pixels);
    dirty = 1;
}

static void history_stack_clear(HistoryStack *h) {
    for (int i = 0; i < h->count; i++) free(h->items[i].pixels);
    h->count = 0;
}
static void history_push(HistoryStack *h, Uint32 *px) {
    if (!px) return;
    if (h->count >= HISTORY_LIMIT) {
        free(h->items[0].pixels);
        memmove(&h->items[0], &h->items[1],
                sizeof(h->items[0]) * (HISTORY_LIMIT - 1));
        h->count = HISTORY_LIMIT - 1;
    }
    h->items[h->count++].pixels = px;
}
static Uint32 *history_pop(HistoryStack *h) {
    if (h->count <= 0) return NULL;
    return h->items[--h->count].pixels;
}
static size_t history_bytes(void) {
    size_t n = (size_t)(undo_stack.count + redo_stack.count) *
               PAINT_W * PAINT_H * sizeof(Uint32);
    if (stroke_snapshot) n += PAINT_W * PAINT_H * sizeof(Uint32);
    return n;
}
static int history_can_undo(void) { return undo_stack.count > 0; }
static int history_can_redo(void) { return redo_stack.count > 0; }
static int history_begin_stroke(void) {
    if (stroke_snapshot) free(stroke_snapshot);
    stroke_snapshot = snapshot_canvas();
    history_stack_clear(&redo_stack);
    return stroke_snapshot ? 0 : -1;
}
static void history_commit_stroke(void) {
    if (stroke_snapshot) {
        history_push(&undo_stack, stroke_snapshot);
        stroke_snapshot = NULL;
    }
}
static void history_cancel_stroke(void) {
    free(stroke_snapshot);
    stroke_snapshot = NULL;
}
static void history_record_clear(void) {
    if (stroke_snapshot) history_commit_stroke();
    Uint32 *px = snapshot_canvas();
    if (px) {
        history_stack_clear(&redo_stack);
        history_push(&undo_stack, px);
    }
}
static int history_undo(void) {
    if (replay_locked()) return 0;
    if (stroke_snapshot) history_commit_stroke();
    if (undo_stack.count <= 0) return 0;
    Uint32 *current = snapshot_canvas();
    if (!current) return -1;
    history_push(&redo_stack, current);
    Uint32 *previous = history_pop(&undo_stack);
    if (restore_canvas(previous) != 0) {
        free(previous);
        return -1;
    }
    free(previous);
    unsaved = 1;
    return 1;
}
static int history_redo(void) {
    if (replay_locked()) return 0;
    if (stroke_snapshot) history_commit_stroke();
    if (redo_stack.count <= 0) return 0;
    Uint32 *current = snapshot_canvas();
    if (!current) return -1;
    history_push(&undo_stack, current);
    Uint32 *next = history_pop(&redo_stack);
    if (restore_canvas(next) != 0) {
        free(next);
        return -1;
    }
    free(next);
    unsaved = 1;
    return 1;
}
static int history_shortcut(SDL_Keycode sym, Uint16 mod) {
    if (!(mod & KMOD_CTRL)) return 0;
    if (sym == SDLK_z && (mod & KMOD_SHIFT)) { history_redo(); return 1; }
    if (sym == SDLK_z) { history_undo(); return 1; }
    if (sym == SDLK_y) { history_redo(); return 1; }
    return 0;
}
static void history_free(void) {
    history_stack_clear(&undo_stack);
    history_stack_clear(&redo_stack);
    history_cancel_stroke();
}

/* Load a PNG/JPEG into the immutable background. The document stays 800x600:
 * the image is aspect-fit over white and the drawing layer is reset only after
 * decoding and scaling succeed. Invalid files leave the current document as-is. */
static void replay_forget(void);
static void replay_stop(void);
static void replay_cancel_for_lifecycle(void);
static int load_image_file(const char *path) {
    if (replay_locked()) return -1;
    if (!path || !*path) {
        snprintf(open_error, sizeof open_error, "Enter a PNG or JPEG path.");
        return -1;
    }
    SDL_Texture *img = IMG_LoadTexture(ren, path);
    if (!img) {
        snprintf(open_error, sizeof open_error, "Cannot open image: %s", IMG_GetError());
        return -1;
    }
    int iw = 0, ih = 0;
    if (SDL_QueryTexture(img, NULL, NULL, &iw, &ih) != 0 || iw <= 0 || ih <= 0) {
        snprintf(open_error, sizeof open_error, "Invalid image dimensions.");
        SDL_DestroyTexture(img);
        return -1;
    }
    SDL_Texture *newbg = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                           SDL_TEXTUREACCESS_TARGET, PAINT_W, PAINT_H);
    if (!newbg) {
        snprintf(open_error, sizeof open_error, "Cannot allocate background.");
        SDL_DestroyTexture(img);
        return -1;
    }
    double scale_x = (double)PAINT_W / (double)iw;
    double scale_y = (double)PAINT_H / (double)ih;
    double scale = scale_x < scale_y ? scale_x : scale_y;
    int dw = (int)(iw * scale + 0.5);
    int dh = (int)(ih * scale + 0.5);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    if (dw > PAINT_W) dw = PAINT_W;
    if (dh > PAINT_H) dh = PAINT_H;
    SDL_Rect dst = { (PAINT_W - dw) / 2, (PAINT_H - dh) / 2, dw, dh };
    SDL_SetRenderTarget(ren, newbg);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_RenderClear(ren);
    SDL_SetTextureBlendMode(img, SDL_BLENDMODE_BLEND);
    int copy_rc = SDL_RenderCopy(ren, img, NULL, &dst);
    SDL_SetRenderTarget(ren, NULL);
    SDL_DestroyTexture(img);
    if (copy_rc != 0) {
        snprintf(open_error, sizeof open_error, "Cannot scale image: %s", SDL_GetError());
        SDL_DestroyTexture(newbg);
        return -1;
    }

    SDL_DestroyTexture(background);
    background = newbg;
    clear_canvas();
    history_free();
    unsaved = 0;
    open_error[0] = 0;
    replay_forget();
    dirty = 1;
    return 0;
}

static int load_script_file(const char *path);
#ifdef HAVE_NFD
static void remember_directory(char *dst, size_t cap, const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash) { snprintf(dst, cap, "."); return; }
    size_t n = (size_t)(slash - path);
    if (n == 0) n = 1;
    if (n >= cap) n = cap - 1;
    memcpy(dst, path, n);
    dst[n] = 0;
}
static void nfd_match_backend(void) {
    const char *driver = SDL_GetCurrentVideoDriver();
    if (!driver) return;
    if (strcmp(driver, "x11") == 0) setenv("GDK_BACKEND", "x11", 1);
    else if (strcmp(driver, "wayland") == 0) setenv("GDK_BACKEND", "wayland", 1);
}
static int nfd_ready(void) {
    if (nfd_state == 0) {
        nfd_match_backend();
        nfd_state = (NFD_Init() == NFD_OKAY) ? 1 : -1;
    }
    return nfd_state == 1;
}
#endif
static void show_image_chooser(void) {
#ifdef HAVE_NFD
    if (nfd_ready()) {
        nfdu8filteritem_t filters[] = { { "Images", "png,jpg,jpeg" } };
        nfdopendialogu8args_t args = {0};
        args.filterList = filters;
        args.filterCount = 1;
        args.defaultPath = last_image_dir[0] ? last_image_dir : NULL;
        args.title = "Open Image";
        nfdu8char_t *path = NULL;
        nfdresult_t result = NFD_OpenDialogU8_With(&path, &args);
        if (result == NFD_OKAY) {
            if (load_image_file(path) == 0) {
                remember_directory(last_image_dir, sizeof last_image_dir, path);
            } else {
                snprintf(open_path, sizeof open_path, "%s", path);
                open_dialog = 1;
            }
            NFD_FreePathU8(path);
            return;
        }
        if (result == NFD_CANCEL) return;
        snprintf(open_error, sizeof open_error, "Native dialog error: %s",
                 NFD_GetError() ? NFD_GetError() : "unknown");
        open_dialog = 1;
        return;
    }
#endif
    snprintf(open_error, sizeof open_error,
             "Native file dialog unavailable; enter a path below.");
    open_dialog = 1;
}
static void show_script_chooser(void) {
#ifdef HAVE_NFD
    if (nfd_ready()) {
        nfdu8filteritem_t filters[] = { { "Glon scripts", "glon" } };
        nfdopendialogu8args_t args = {0};
        args.filterList = filters;
        args.filterCount = 1;
        args.defaultPath = last_script_dir[0] ? last_script_dir : NULL;
        args.title = "Open Glon Script";
        nfdu8char_t *path = NULL;
        nfdresult_t result = NFD_OpenDialogU8_With(&path, &args);
        if (result == NFD_OKAY) {
            if (load_script_file(path) == 0) {
                remember_directory(last_script_dir, sizeof last_script_dir, path);
            } else {
                snprintf(script_path, sizeof script_path, "%s", path);
                script_dialog = 1;
            }
            NFD_FreePathU8(path);
            return;
        }
        if (result == NFD_CANCEL) return;
        snprintf(script_error, sizeof script_error, "Native dialog error: %s",
                 NFD_GetError() ? NFD_GetError() : "unknown");
        script_dialog = 1;
        return;
    }
#endif
    snprintf(script_error, sizeof script_error,
             "Native file dialog unavailable; enter a path below.");
    script_dialog = 1;
}
static void request_open(void) {
    if (replay_locked()) return;
    open_error[0] = 0;
    if (unsaved) {
        confirm_open = 1;
    } else {
        show_image_chooser();
    }
}
static void request_open_script(void) {
    script_error[0] = 0;
    show_script_chooser();
}
static int read_script_file(const char *path, char **out, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(script_error, sizeof script_error, "Cannot open script: %.180s", path); return -1; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long n = ftell(f);
    if (n < 0 || (unsigned long)n > SCRIPT_MAX_BYTES) {
        snprintf(script_error, sizeof script_error, "Script exceeds %d bytes.", SCRIPT_MAX_BYTES);
        fclose(f); return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    char *src = malloc((size_t)n + 1);
    if (!src) { fclose(f); snprintf(script_error, sizeof script_error, "Out of memory loading script."); return -1; }
    if (fread(src, 1, (size_t)n, f) != (size_t)n) {
        free(src); fclose(f); snprintf(script_error, sizeof script_error, "Cannot read script."); return -1;
    }
    src[n] = 0; fclose(f);
    char *r = src, *w = src;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
    *out = src; *out_len = (size_t)(w - src);
    return 0;
}
static int load_script_file(const char *path) {
    if (!path || !*path) { snprintf(script_error, sizeof script_error, "Enter a .glon path."); return -1; }
    char *src = NULL; size_t len = 0;
    if (read_script_file(path, &src, &len) != 0) return -1;
    cell save_lhp = M[GC_LOADER_HP];
    int perr = 0;
    r0_s1_parse(src, &perr);
    if (perr) {
        r0_s1_outcome oc;
        r0_s1_session_parse_error(&oc);
        snprintf(script_error, sizeof script_error, "Invalid script: %s/%s",
                 r0_s1_outcome_status_name(oc.status),
                 r0_s1_outcome_detail_name(oc.detail));
        M[GC_LOADER_HP] = save_lhp;
        free(src);
        return -1;
    }
    M[GC_LOADER_HP] = save_lhp;
    /* Loading a document is a lifecycle action: leave any replay/animation
     * state and restore the pre-playback drawing before installing script B. */
    replay_cancel_for_lifecycle();
    free(script_source);
    script_source = src;
    script_source_len = len;
    script_loaded = 1;
    snprintf(script_path, sizeof script_path, "%s", path);
    script_error[0] = 0;
    replay_forget();
    snprintf(script_message, sizeof script_message, "Loaded script.");
    return 0;
}

/* Execute one Glon command. C does not decide brush/tool state. */
static void command(const char *line, int *saved) {
    if ((line[0] == 'L' || line[0] == 'E') && line[1] == ' ') {
        int x1, y1, x2, y2, size, r, g, b;
        if (sscanf(line + 2, "%d %d %d %d %d %d %d %d",
                   &x1, &y1, &x2, &y2, &size, &r, &g, &b) == 8) {
            draw_line(x1, y1, x2, y2, size, r, g, b, line[0] == 'E' ? 0 : 255);
            unsaved = 1;
        }
    } else if (line[0] == 'P' && line[1] == ' ') {
        int x1, y1, p1, x2, y2, p2, dia, op, r, g, b;
        if (sscanf(line + 2, "%d %d %d %d %d %d %d %d %d %d %d",
                   &x1, &y1, &p1, &x2, &y2, &p2, &dia, &op, &r, &g, &b) == 11) {
            draw_pressure_line(x1, y1, p1, x2, y2, p2, dia, op, r, g, b);
            unsaved = 1;
        }
    } else if (line[0] == 'C' && line[1] == 0) {
        if (!history_replaying) history_record_clear();
        clear_canvas();
        unsaved = 1;
    } else if (line[0] == 'T' && line[1] == ' ') {
        SDL_SetWindowTitle(win, line + 2);
    } else if (line[0] == 'S' && line[1] == ' ') {
        if (save_png(line + 2) == 0) { *saved = 1; unsaved = 0; }
    } else if (line[0] == 'I' && line[1] == ' ') {
        int c, z, t, d, cd, pr, op;
        if (sscanf(line + 2, "%d %d %d %d %d %d %d", &c, &z, &t, &d,
                   &cd, &pr, &op) == 7) {
            ui_color = c; ui_size = z; ui_tool = t; ui_drawing = d;
            ui_call_diameter = cd; ui_pressure = pr; ui_opacity = op;
        }
    } else if (line[0] == 'Q' && line[1] == 0) {
        quit_flag = 1;
    }
}

static void process_commands(char *buf, int *saved) {
    char *p = buf;
    while (p && *p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        if (*p) command(p, saved);
        if (!nl) break;
        p = nl + 1;
    }
}

static int shape_append_cmd(char *out, size_t cap, size_t *used, int *count,
                            int x1, int y1, int x2, int y2, int size,
                            int r, int g, int b) {
    char line[128];
    int n = snprintf(line, sizeof line, "L %d %d %d %d %d %d %d %d\n",
                     x1, y1, x2, y2, size, r, g, b);
    if (n < 0 || *used + (size_t)n + 1 > cap) return -1;
    memcpy(out + *used, line, (size_t)n);
    *used += (size_t)n;
    out[*used] = 0;
    (*count)++;
    return 0;
}
/* Build the exact standard L-command stream for a shape. This is the only
 * place geometric tools turn a gesture into drawing commands. */
static int shape_build_commands(int tool, int x1, int y1, int x2, int y2,
                                int shift, int fill, int size,
                                int r, int g, int b,
                                char *out, size_t cap, size_t *bytes) {
    out[0] = 0;
    *bytes = 0;
    int count = 0;
    size_t used = 0;
    x1 = shape_clamp_x(x1); y1 = shape_clamp_y(y1);
    x2 = shape_clamp_x(x2); y2 = shape_clamp_y(y2);
    if (tool == 4) {
        shape_constrain_line(x1, y1, &x2, &y2, shift);
        x2 = shape_clamp_x(x2); y2 = shape_clamp_y(y2);
        if (shape_append_cmd(out, cap, &used, &count, x1, y1, x2, y2,
                             size, r, g, b) != 0) return 0;
    } else if (tool == 5 || tool == 6) {
        shape_constrain_square(x1, y1, &x2, &y2, shift);
        ShapeBox box;
        shape_make_box(x1, y1, x2, y2, &box);
        if (box.maxx <= box.minx || box.maxy <= box.miny) return 0;
        if (tool == 5) {
            if (fill) {
                for (int y = box.miny; y <= box.maxy; y++) {
                    if (shape_append_cmd(out, cap, &used, &count,
                                         box.minx, y, box.maxx, y, 1,
                                         r, g, b) != 0) return 0;
                }
            } else {
                if (shape_append_cmd(out, cap, &used, &count,
                                     box.minx, box.miny, box.maxx, box.miny,
                                     size, r, g, b) != 0) return 0;
                if (shape_append_cmd(out, cap, &used, &count,
                                     box.maxx, box.miny, box.maxx, box.maxy,
                                     size, r, g, b) != 0) return 0;
                if (shape_append_cmd(out, cap, &used, &count,
                                     box.maxx, box.maxy, box.minx, box.maxy,
                                     size, r, g, b) != 0) return 0;
                if (shape_append_cmd(out, cap, &used, &count,
                                     box.minx, box.maxy, box.minx, box.miny,
                                     size, r, g, b) != 0) return 0;
            }
        } else {
            int cx = (box.minx + box.maxx) / 2;
            int cy = (box.miny + box.maxy) / 2;
            int rx = (box.maxx - box.minx) / 2;
            int ry = (box.maxy - box.miny) / 2;
            if (rx < 1 || ry < 1) return 0;
            if (fill) {
                for (int y = box.miny; y <= box.maxy; y++) {
                    int ext = ellipse_extent(rx, ry, shape_abs(y - cy));
                    if (ext < 0) continue;
                    if (shape_append_cmd(out, cap, &used, &count,
                                         cx - ext, y, cx + ext, y, 1,
                                         r, g, b) != 0) return 0;
                }
            } else {
                int xs[96], ys[96];
                int n = ellipse_polygon(&box, xs, ys, 96);
                for (int i = 0; i < n; i++) {
                    int j = (i + 1) % n;
                    if (shape_append_cmd(out, cap, &used, &count,
                                         xs[i], ys[i], xs[j], ys[j],
                                         size, r, g, b) != 0) return 0;
                }
            }
        }
    }
    *bytes = used;
    return count;
}
static void shape_begin_gesture(int tool, int x, int y, int shift) {
    shape_active = 1;
    shape_tool = tool;
    shape_start_x = shape_clamp_x(x);
    shape_start_y = shape_clamp_y(y);
    shape_end_x = shape_start_x;
    shape_end_y = shape_start_y;
    shape_shift = shift ? 1 : 0;
    shape_last_commands = 0;
    shape_last_bytes = 0;
    shape_last_text[0] = 0;
}
static void shape_update_gesture(int x, int y, int shift) {
    if (!shape_active) return;
    shape_end_x = shape_clamp_x(x);
    shape_end_y = shape_clamp_y(y);
    shape_shift = shift ? 1 : 0;
    dirty = 1;
}
static void shape_cancel_gesture(void) {
    shape_active = 0;
    shape_last_commands = 0;
    shape_last_bytes = 0;
    shape_last_text[0] = 0;
    dirty = 1;
}
static int shape_finish_gesture(void) {
    if (!shape_active) return 0;
    shape_active = 0;
    int r = 0, g = 0, b = 0;
    palette_rgb(ui_color, &r, &g, &b);
    size_t bytes = 0;
    int count = shape_build_commands(shape_tool, shape_start_x, shape_start_y,
                                     shape_end_x, shape_end_y, shape_shift,
                                     ui_fill_shapes, ui_size, r, g, b,
                                     shape_last_text, sizeof shape_last_text,
                                     &bytes);
    shape_last_commands = count;
    shape_last_bytes = bytes;
    if (count <= 0) {
        shape_last_commands = 0;
        shape_last_bytes = 0;
        shape_last_text[0] = 0;
        dirty = 1;
        return 0;
    }
    if (history_begin_stroke() != 0) {
        shape_last_commands = 0;
        shape_last_bytes = 0;
        shape_last_text[0] = 0;
        return 0;
    }
    int saved = 0;
    char *p = shape_last_text;
    while (*p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        if (*p) command(p, &saved);
        if (nl) *nl = '\n';
        if (!nl) break;
        p = nl + 1;
    }
    history_commit_stroke();
    unsaved = 1;
    dirty = 1;
    return count;
}

typedef struct { int x, y; } BucketSeed;
static int bucket_color_match(Uint32 a, Uint32 b, int tolerance) {
    if (tolerance <= 0) return a == b;
    int da = shape_abs((int)((a >> 24) & 255) - (int)((b >> 24) & 255));
    int dr = shape_abs((int)((a >> 16) & 255) - (int)((b >> 16) & 255));
    int dg = shape_abs((int)((a >> 8) & 255) - (int)((b >> 8) & 255));
    int db = shape_abs((int)(a & 255) - (int)(b & 255));
    return da <= tolerance && dr <= tolerance &&
           dg <= tolerance && db <= tolerance;
}
/* Iterative scanline flood fill. The mask is accumulated as contiguous
 * horizontal spans and emitted only as ordinary L commands. */
static int bucket_fill(int sx, int sy, int tolerance) {
    bucket_last_commands = 0;
    bucket_last_bytes = 0;
    bucket_last_text[0] = 0;
    bucket_message[0] = 0;
    if (sx < 0 || sx >= PAINT_W || sy < 0 || sy >= PAINT_H) {
        snprintf(bucket_message, sizeof bucket_message, "Fill point is outside the canvas.");
        return -1;
    }
    if (tolerance < 0) tolerance = 0;
    if (tolerance > 255) tolerance = 255;
    int r = 0, g = 0, b = 0;
    palette_rgb(ui_color, &r, &g, &b);
    Uint32 replacement = ((Uint32)255 << 24) | ((Uint32)r << 16) |
                         ((Uint32)g << 8) | (Uint32)b;
    Uint32 *px = snapshot_canvas();
    if (!px) {
        snprintf(bucket_message, sizeof bucket_message, "Cannot read drawing layer.");
        return -1;
    }
    size_t pixels = (size_t)PAINT_W * PAINT_H;
    Uint32 target = px[(size_t)sy * PAINT_W + sx];
    if (target == replacement) {
        free(px);
        snprintf(bucket_message, sizeof bucket_message,
                 "No change: target already matches the selected colour.");
        return 0;
    }
    Uint8 *visited = calloc(pixels, 1);
    size_t cap = 1024, top = 0;
    BucketSeed *stack = cap ? malloc(cap * sizeof *stack) : NULL;
    if (!visited || !stack) {
        free(visited); free(stack); free(px);
        snprintf(bucket_message, sizeof bucket_message, "Cannot allocate fill workspace.");
        return -1;
    }
    size_t used = 0;
    int failed = 0;
    stack[top++] = (BucketSeed){ sx, sy };
    while (top > 0 && !failed) {
        BucketSeed seed = stack[--top];
        int x = seed.x, y = seed.y;
        size_t row = (size_t)y * PAINT_W;
        if (visited[row + (size_t)x] ||
            !bucket_color_match(px[row + (size_t)x], target, tolerance))
            continue;
        int x1 = x, x2 = x;
        while (x1 > 0 && !visited[row + (size_t)(x1 - 1)] &&
               bucket_color_match(px[row + (size_t)(x1 - 1)], target, tolerance))
            x1--;
        while (x2 + 1 < PAINT_W && !visited[row + (size_t)(x2 + 1)] &&
               bucket_color_match(px[row + (size_t)(x2 + 1)], target, tolerance))
            x2++;
        for (int xi = x1; xi <= x2; xi++) {
            px[row + (size_t)xi] = replacement;
            visited[row + (size_t)xi] = 1;
        }
        if (bucket_last_commands >= REPLAY_MAX_COMMANDS ||
            shape_append_cmd(bucket_last_text, sizeof bucket_last_text, &used,
                             &bucket_last_commands, x1, y, x2, y, 1,
                             r, g, b) != 0) {
            failed = 1;
            break;
        }
        for (int dy = -1; dy <= 1 && !failed; dy += 2) {
            int yy = y + dy;
            if (yy < 0 || yy >= PAINT_H) continue;
            size_t row2 = (size_t)yy * PAINT_W;
            int xi = x1;
            while (xi <= x2) {
                if (!visited[row2 + (size_t)xi] &&
                    bucket_color_match(px[row2 + (size_t)xi], target, tolerance)) {
                    int start = xi;
                    while (xi <= x2 && !visited[row2 + (size_t)xi] &&
                           bucket_color_match(px[row2 + (size_t)xi], target, tolerance))
                        xi++;
                    if (top == cap) {
                        size_t ncap = cap < pixels ? cap * 2 : pixels;
                        if (ncap <= cap) { failed = 1; break; }
                        BucketSeed *next = realloc(stack, ncap * sizeof *stack);
                        if (!next) { failed = 1; break; }
                        stack = next;
                        cap = ncap;
                    }
                    stack[top++] = (BucketSeed){ start, yy };
                } else {
                    xi++;
                }
            }
        }
    }
    free(visited); free(stack);
    if (failed) {
        free(px);
        bucket_last_commands = 0;
        bucket_last_bytes = 0;
        bucket_last_text[0] = 0;
        snprintf(bucket_message, sizeof bucket_message,
                 "Fill is too complex to encode within the command limit.");
        return -1;
    }
    bucket_last_bytes = used;
    if (bucket_last_commands <= 0) {
        free(px);
        snprintf(bucket_message, sizeof bucket_message, "No pixels matched the fill point.");
        return 0;
    }
    if (history_begin_stroke() != 0) {
        free(px);
        bucket_last_commands = 0;
        bucket_last_bytes = 0;
        bucket_last_text[0] = 0;
        snprintf(bucket_message, sizeof bucket_message, "Cannot snapshot drawing layer.");
        return -1;
    }
    int saved = 0;
    char *p = bucket_last_text;
    while (*p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        if (*p) command(p, &saved);
        if (nl) *nl = '\n';
        if (!nl) break;
        p = nl + 1;
    }
    history_commit_stroke();
    unsaved = 1;
    dirty = 1;
    free(px);
    return bucket_last_commands;
}

static int app_call(const char *src);
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    int overflow;
} ScriptSink;
static ssize_t script_sink_write(void *cookie, const char *data, size_t n) {
    ScriptSink *sink = cookie;
    if (n > sink->cap - sink->len) {
        sink->overflow = 1;
        return 0;
    }
    memcpy(sink->buf + sink->len, data, n);
    sink->len += n;
    return (ssize_t)n;
}
static int script_sink_begin(FILE **saved_stdout, FILE **stream, ScriptSink *sink) {
    cookie_io_functions_t fns;
    memset(&fns, 0, sizeof fns);
    fns.write = script_sink_write;
    *stream = fopencookie(sink, "w", fns);
    if (!*stream) return -1;
    fflush(stdout);
    *saved_stdout = stdout;
    stdout = *stream;
    return 0;
}
static int script_sink_end(FILE *saved_stdout, FILE *stream, ScriptSink *sink) {
    int rc = 0;
    if (fflush(stream) != 0) rc = -1;
    stdout = saved_stdout;
    if (fclose(stream) != 0) rc = -1;
    if (sink->overflow) rc = -1;
    return rc;
}
static int prepare_script_commands(char *out, size_t cap, int *count,
                                   int *offsets, int *lengths) {
    *count = 0;
    if (!script_loaded || !script_source) {
        snprintf(script_error, sizeof script_error, "No script loaded.");
        return -1;
    }
    cell save_lhp = M[GC_LOADER_HP];
    char *cmds = malloc(SCRIPT_CMD_BYTES);
    char status[32] = "";
    ScriptSink sink = { cmds, 0, cmds ? SCRIPT_CMD_BYTES - 1 : 0, 0 };
    FILE *saved_stdout = NULL;
    FILE *stream = NULL;
    int stream_enabled = 0;
    int captured = 0;
    int failed = 0;
    script_error[0] = 0;
    if (!cmds) {
        snprintf(script_error, sizeof script_error, "Out of memory for script commands.");
        failed = 1;
    }
    if (!failed && app_call("paint/script-reset") != 0) {
        snprintf(script_error, sizeof script_error, "Cannot reset script sink.");
        failed = 1;
    }
    if (!failed && app_call("paint/script-set-stream 1") != 0) {
        snprintf(script_error, sizeof script_error, "Cannot enable script streaming.");
        failed = 1;
    } else if (!failed) {
        stream_enabled = 1;
        if (script_sink_begin(&saved_stdout, &stream, &sink) != 0) {
            snprintf(script_error, sizeof script_error, "Cannot allocate script stream.");
            failed = 1;
        } else {
            captured = 1;
        }
    }
    if (!failed) {
        int perr = 0;
        cell prog = r0_s1_parse(script_source, &perr);
        if (perr) {
            r0_s1_outcome oc;
            r0_s1_session_parse_error(&oc);
            snprintf(script_error, sizeof script_error, "Script parse failed: %s/%s",
                     r0_s1_outcome_status_name(oc.status),
                     r0_s1_outcome_detail_name(oc.detail));
            failed = 1;
        } else {
            r0_s1_outcome oc;
            r0_s1_session_run_block(prog, &oc);
            if (oc.status != R0S1_OUT_OK) {
                snprintf(script_error, sizeof script_error, "Script failed: %s/%s",
                         r0_s1_outcome_status_name(oc.status),
                         r0_s1_outcome_detail_name(oc.detail));
                failed = 1;
            }
        }
    }
    if (captured) {
        captured = 0;
        if (script_sink_end(saved_stdout, stream, &sink) != 0) {
            if (!failed)
                snprintf(script_error, sizeof script_error,
                         "Script command stream exceeds its bound.");
            failed = 1;
        }
    }
    if (stream_enabled) {
        stream_enabled = 0;
        app_call("paint/script-set-stream 0");
    }
    if (!failed) {
        cmds[sink.len] = 0;
        if (sink.len >= SCRIPT_CMD_BYTES - 1) {
            snprintf(script_error, sizeof script_error,
                     "Script command buffer is too large.");
            failed = 1;
        }
    }
    if (!failed && (glon_call("paint/script-status", status, sizeof status) != 0 ||
                    strcmp(status, "OK") != 0)) {
        snprintf(script_error, sizeof script_error,
                 "Script exceeded the drawing-operation limit.");
        failed = 1;
    }
    if (!failed) {
        char *p = cmds;
        size_t used = 0;
        int n = 0;
        while (p && *p) {
            char *nl = strchr(p, '\n');
            if (nl) *nl = 0;
            if (*p) {
                if (!((p[0] == 'L' || p[0] == 'E' || p[0] == 'P' || p[0] == 'C') &&
                      (p[1] == ' ' || p[1] == 0))) {
                    snprintf(script_error, sizeof script_error,
                             "Script emitted an unsupported command.");
                    failed = 1;
                    break;
                }
                int len = (int)strlen(p);
                if (n >= REPLAY_MAX_COMMANDS || used + (size_t)len + 1 > cap) {
                    snprintf(script_error, sizeof script_error,
                             "Script command buffer exceeds its bound.");
                    failed = 1;
                    break;
                }
                offsets[n] = (int)used;
                lengths[n] = len;
                memcpy(out + used, p, (size_t)len);
                out[used + len] = 0;
                used += (size_t)len + 1;
                n++;
            }
            if (!nl) break;
            p = nl + 1;
        }
        *count = n;
    }
    if (failed) *count = 0;
    free(cmds);
    app_call("paint/title");  /* refresh the C UI mirror after script state changes */
    M[GC_LOADER_HP] = save_lhp;
    return failed ? -1 : 0;
}

static int parse_pressure_command(const char *line, PressureCommand *pc) {
    if (line[0] != 'P' || line[1] != ' ') return 0;
    return sscanf(line + 2, "%d %d %d %d %d %d %d %d %d %d %d",
                  &pc->x1, &pc->y1, &pc->p1, &pc->x2, &pc->y2, &pc->p2,
                  &pc->dia, &pc->op, &pc->r, &pc->g, &pc->b) == 11;
}
/* Explicit integer interpolation, rounded to nearest with ties away from zero.
 * den is positive; num is clamped by the callers. */
static int lerp_int(int a, int b, int num, int den) {
    int64_t v = (int64_t)a * (den - num) + (int64_t)b * num;
    if (v >= 0) return (int)((v + den / 2) / den);
    return (int)(-((-v + den / 2) / den));
}
static size_t replay_pixels_bytes(void) {
    return (size_t)PAINT_W * PAINT_H * sizeof(Uint32);
}
static int replay_anim_alloc(void) {
    if (!replay_cmd_pre)
        replay_cmd_pre = malloc(replay_pixels_bytes());
    if (!replay_anim_work)
        replay_anim_work = malloc(replay_pixels_bytes());
    if (!replay_cmd_pre || !replay_anim_work) {
        free(replay_cmd_pre); replay_cmd_pre = NULL;
        free(replay_anim_work); replay_anim_work = NULL;
        return -1;
    }
    return 0;
}
static void replay_anim_free(void) {
    free(replay_cmd_pre); replay_cmd_pre = NULL;
    free(replay_anim_work); replay_anim_work = NULL;
    replay_animating = 0;
    replay_anim_elapsed_ms = 0;
    replay_anim_progress = 0;
    replay_anim_x = replay_anim_y = replay_anim_diameter = 0;
}
static size_t replay_anim_memory_bytes(void) {
    size_t n = 0;
    if (replay_cmd_pre) n += replay_pixels_bytes();
    if (replay_anim_work) n += replay_pixels_bytes();
    return n;
}
static int replay_next_pressure(PressureCommand *pc) {
    if (replay_pos < 0 || replay_pos >= replay_count) return 0;
    return parse_pressure_command(replay_buf + replay_offsets[replay_pos], pc);
}
/* Render progress `permille` of the active pressure command. The pre-command
 * canvas is copied into one reusable work buffer, the shared rasteriser draws
 * the partial segment, and the result is uploaded once. No GPU readback and no
 * frame-history allocation occur here. */
static int replay_anim_render_at(int permille) {
    if (!replay_animating || !replay_cmd_pre || !replay_anim_work) return -1;
    if (permille < 0) permille = 0;
    if (permille > 999) permille = 999;
    PressureCommand *pc = &replay_anim_cmd;
    int px = lerp_int(pc->x1, pc->x2, permille, 1000);
    int py = lerp_int(pc->y1, pc->y2, permille, 1000);
    int pp = lerp_int(pc->p1, pc->p2, permille, 1000);
    Uint64 t0 = SDL_GetPerformanceCounter();
    memcpy(replay_anim_work, replay_cmd_pre, replay_pixels_bytes());
    raster_pressure_line(replay_anim_work, pc->x1, pc->y1, pc->p1,
                         px, py, pp, pc->dia, pc->op, pc->r, pc->g, pc->b);
    if (SDL_UpdateTexture(canvas, NULL, replay_anim_work,
                          PAINT_W * sizeof(Uint32)) != 0) return -1;
    Uint64 t1 = SDL_GetPerformanceCounter();
    Uint64 ns = (t1 - t0) * 1000000000ULL /
                (Uint64)SDL_GetPerformanceFrequency();
    replay_anim_total_ns += ns;
    if (ns > replay_anim_worst_ns) replay_anim_worst_ns = ns;
    replay_anim_frames++;
    replay_anim_uploads++;
    replay_anim_progress = permille;
    replay_anim_x = px;
    replay_anim_y = py;
    replay_anim_diameter = pressure_diameter(pc->dia, pp);
    dirty = 1;
    return 0;
}
static int replay_anim_begin(void) {
    if (replay_animating) return 0;
    if (!replay_active || replay_pos >= replay_count) return 0;
    PressureCommand pc;
    if (!replay_next_pressure(&pc)) return 0;
    if (replay_anim_alloc() != 0) {
        snprintf(replay_error, sizeof replay_error,
                 "Cannot allocate animation buffers.");
        replay_state = REPLAY_ERROR;
        return -1;
    }
    if (read_canvas_into(replay_cmd_pre) != 0) {
        snprintf(replay_error, sizeof replay_error,
                 "Cannot snapshot pre-command drawing layer.");
        replay_state = REPLAY_ERROR;
        return -1;
    }
    replay_anim_cmd = pc;
    replay_animating = 1;
    replay_anim_elapsed_ms = 0;
    replay_anim_duration_ms = ui_stroke_durations[ui_stroke_duration_index];
    if (replay_anim_duration_ms < 1) replay_anim_duration_ms = 1;
    replay_accum_ns = 0;
    unsaved = 1;
    return replay_anim_render_at(0) == 0 ? 1 : -1;
}
/* Finish the active presentation. apply_full restores the pre-command canvas
 * and dispatches the original full command once through the authoritative
 * renderer; otherwise the temporary partial pixels are discarded. */
static int replay_anim_finish(int apply_full) {
    if (!replay_animating) return 0;
    if (!replay_cmd_pre) return -1;
    if (restore_canvas(replay_cmd_pre) != 0) return -1;
    replay_animating = 0;
    replay_anim_elapsed_ms = 0;
    replay_anim_progress = 0;
    replay_anim_x = replay_anim_y = replay_anim_diameter = 0;
    if (apply_full) {
        if (replay_pos < 0 || replay_pos >= replay_count) return -1;
        int saved = 0;
        int old = history_replaying;
        history_replaying = 1;
        command(replay_buf + replay_offsets[replay_pos], &saved);
        history_replaying = old;
        replay_pos++;
        dirty = 1;
    }
    return 0;
}
static void replay_forget(void) {
    free(replay_pre);
    replay_pre = NULL;
    replay_anim_free();
    replay_prepared = 0;
    replay_count = replay_pos = 0;
    replay_active = 0;
    replay_state = REPLAY_READY;
    replay_error[0] = 0;
}

/* Test helper: load a literal standard-command stream into the replay buffer
 * without rerunning Glon. Used to prove shape-generated L commands replay. */
static int replay_load_command_text(const char *text) {
    replay_forget();
    const char *p = text;
    int n = 0;
    size_t used = 0;
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        if (len > 0) {
            if (n >= REPLAY_MAX_COMMANDS ||
                used + (size_t)len + 1 > sizeof replay_buf) return -1;
            replay_offsets[n] = (int)used;
            replay_lengths[n] = len;
            memcpy(replay_buf + used, p, (size_t)len);
            replay_buf[used + len] = 0;
            used += (size_t)len + 1;
            n++;
        }
        if (!nl) break;
        p = nl + 1;
    }
    replay_count = n;
    replay_pos = 0;
    replay_prepared = 1;
    replay_active = 0;
    replay_state = REPLAY_READY;
    replay_error[0] = 0;
    return 0;
}
static int replay_prepare(void) {
    if (shape_active) shape_cancel_gesture();
    if (replay_active) {
        snprintf(replay_error, sizeof replay_error, "Playback is active.");
        replay_state = REPLAY_ERROR;
        return -1;
    }
    free(replay_pre);
    replay_pre = NULL;
    replay_anim_free();
    replay_prepared = 0;
    replay_count = replay_pos = 0;
    replay_error[0] = 0;
    if (prepare_script_commands(replay_buf, sizeof replay_buf, &replay_count,
                                replay_offsets, replay_lengths) != 0) {
        replay_state = REPLAY_ERROR;
        snprintf(replay_error, sizeof replay_error, "%s", script_error);
        return -1;
    }
    replay_prepared = 1;
    replay_state = REPLAY_READY;
    snprintf(script_message, sizeof script_message, "Prepared %d commands.", replay_count);
    return 0;
}

static void replay_apply_one(void) {
    if (replay_pos < 0 || replay_pos >= replay_count) return;
    int saved = 0;
    int old = history_replaying;
    history_replaying = 1;
    command(replay_buf + replay_offsets[replay_pos], &saved);
    history_replaying = old;
    replay_pos++;
    dirty = 1;
}

static void replay_complete(void) {
    if (replay_animating) return;
    if (replay_pos < replay_count) return;
    replay_state = REPLAY_COMPLETE;
    replay_active = 1;  /* temporary replay drawing is still owned by replay */
    unsaved = 1;
    replay_pos = replay_count;
    snprintf(replay_error, sizeof replay_error, "Complete; press End to commit this prefix.");
    dirty = 1;
}

static int replay_play(void) {
    if (shape_active) shape_cancel_gesture();
    if (replay_state == REPLAY_PLAYING) return 0;
    if (!replay_prepared && replay_prepare() != 0) return -1;
    if (replay_state == REPLAY_COMPLETE && replay_pre) {
        replay_anim_free();
        if (restore_canvas(replay_pre) != 0) return -1;
        replay_pos = 0;
        replay_active = 1;
    } else if (!replay_active) {
        free(replay_pre);
        replay_pre = snapshot_canvas();
        if (!replay_pre) {
            snprintf(replay_error, sizeof replay_error, "Cannot snapshot drawing layer.");
            replay_state = REPLAY_ERROR;
            return -1;
        }
        replay_unsaved = unsaved;
        replay_pos = 0;
        replay_active = 1;
        replay_animating = 0;
        replay_anim_elapsed_ms = 0;
        replay_anim_progress = 0;
        replay_anim_x = replay_anim_y = replay_anim_diameter = 0;
    }
    replay_state = REPLAY_PLAYING;
    replay_last_tick = SDL_GetTicks64();
    replay_accum_ns = 0;
    return 0;
}

static void replay_pause(void) {
    if (replay_state == REPLAY_PLAYING) {
        replay_state = REPLAY_PAUSED;
        replay_accum_ns = 0;
    }
}

static int replay_step(void) {
    if (!replay_prepared && replay_prepare() != 0) return -1;
    if (replay_state == REPLAY_COMPLETE) return 0;
    if (!replay_active) {
        free(replay_pre);
        replay_pre = snapshot_canvas();
        if (!replay_pre) {
            snprintf(replay_error, sizeof replay_error, "Cannot snapshot drawing layer.");
            replay_state = REPLAY_ERROR;
            return -1;
        }
        replay_unsaved = unsaved;
        replay_pos = 0;
        replay_active = 1;
    }
    replay_state = REPLAY_PAUSED;
    if (replay_animating) {
        if (replay_anim_finish(1) != 0) return -1;
    } else if (replay_pos < replay_count) {
        replay_apply_one();
    }
    if (replay_pos >= replay_count) replay_complete();
    else dirty = 1;
    return 0;
}

static int replay_reconstruct(int target) {
    if (!replay_pre || !replay_prepared || target < 0 || target > replay_count) return -1;
    if (replay_animating && replay_anim_finish(0) != 0) return -1;
    if (restore_canvas(replay_pre) != 0) return -1;
    int old = history_replaying;
    history_replaying = 1;
    for (int i = 0; i < target; i++) {
        int saved = 0;
        command(replay_buf + replay_offsets[i], &saved);
    }
    history_replaying = old;
    replay_pos = target;
    replay_active = 1;
    replay_anim_elapsed_ms = 0;
    replay_anim_progress = 0;
    replay_anim_x = replay_anim_y = replay_anim_diameter = 0;
    dirty = 1;
    return 0;
}

static int replay_step_back(void) {
    if (!replay_prepared || !replay_pre) return 0;
    if (!replay_animating && replay_pos <= 0) return 0;
    int target = replay_pos > 0 ? replay_pos - 1 : 0;
    if (replay_reconstruct(target) != 0) return -1;
    replay_state = REPLAY_PAUSED;
    replay_accum_ns = 0;
    return 1;
}

static int replay_step_forward(void) {
    if (!replay_prepared) return 0;
    if (replay_animating) {
        replay_state = REPLAY_PAUSED;
        if (replay_anim_finish(1) != 0) return -1;
        if (replay_pos >= replay_count) replay_complete();
        else dirty = 1;
        return 1;
    }
    if (replay_pos >= replay_count) return 0;
    if (!replay_active) {
        free(replay_pre);
        replay_pre = snapshot_canvas();
        if (!replay_pre) {
            snprintf(replay_error, sizeof replay_error, "Cannot snapshot drawing layer.");
            replay_state = REPLAY_ERROR;
            return -1;
        }
        replay_unsaved = unsaved;
        replay_pos = 0;
        replay_active = 1;
    }
    replay_state = REPLAY_PAUSED;
    replay_apply_one();
    if (replay_pos >= replay_count) replay_complete();
    return 1;
}

static int replay_nav_available(void) {
    return replay_prepared && replay_pre &&
           (replay_state == REPLAY_PLAYING || replay_state == REPLAY_PAUSED ||
            replay_state == REPLAY_COMPLETE);
}

static int replay_locked(void) {
    return replay_busy() || replay_nav_available();
}

static void replay_end(void) {
    if (!replay_nav_available() || !replay_pre) return;
    /* A partially animated command is never committed. Restore the completed
     * command boundary before snapshotting the prefix. */
    if (replay_animating && replay_anim_finish(0) != 0) return;
    Uint32 *current = snapshot_canvas();
    if (!current) {
        snprintf(replay_error, sizeof replay_error, "Cannot snapshot current replay prefix.");
        return;
    }
    int changed = memcmp(current, replay_pre,
                         (size_t)PAINT_W * PAINT_H * sizeof(Uint32)) != 0;
    free(current);
    if (changed) {
        history_stack_clear(&redo_stack);
        history_push(&undo_stack, replay_pre);
        replay_pre = NULL;
        unsaved = 1;
    } else {
        free(replay_pre);
        replay_pre = NULL;
        unsaved = replay_unsaved;
    }
    replay_active = 0;
    replay_prepared = 0;
    replay_count = replay_pos = 0;
    replay_state = REPLAY_ENDED;
    replay_error[0] = 0;
    replay_anim_free();
    snprintf(script_message, sizeof script_message,
             changed ? "Replay prefix committed to document history."
                     : "Replay ended at unchanged canvas; no document entry.");
    dirty = 1;
}

static void replay_stop(void) {
    if (!replay_active && !replay_pre && !replay_nav_available() &&
        !replay_animating) return;
    replay_anim_free();
    if (replay_pre) restore_canvas(replay_pre);
    free(replay_pre);
    replay_pre = NULL;
    replay_active = 0;
    replay_prepared = 0;
    replay_count = replay_pos = 0;
    replay_state = REPLAY_READY;
    replay_accum_ns = 0;
    replay_last_tick = 0;
    unsaved = replay_unsaved;
    replay_error[0] = 0;
    dirty = 1;
}
static void replay_cancel_for_lifecycle(void) {
    if (shape_active) shape_cancel_gesture();
    if (replay_active || replay_pre || replay_animating || replay_nav_available())
        replay_stop();
}

static int replay_pump(void) {
    if (replay_state != REPLAY_PLAYING || !replay_prepared) return 0;
    Uint64 now = SDL_GetTicks64();
    Uint64 elapsed = now >= replay_last_tick ? now - replay_last_tick : 0;
    replay_last_tick = now;
    if (replay_animating) {
        /* Bound one catch-up step so a scheduler stall cannot jump the whole
         * stroke; progress still comes only from monotonic elapsed time. */
        Uint64 step = elapsed > 250 ? 250 : elapsed;
        replay_anim_elapsed_ms += step;
        Uint64 dur = replay_anim_duration_ms > 0
                   ? (Uint64)replay_anim_duration_ms : 1;
        if (replay_anim_elapsed_ms >= dur) {
            if (replay_anim_finish(1) != 0) {
                snprintf(replay_error, sizeof replay_error,
                         "Cannot complete animated stroke.");
                replay_state = REPLAY_ERROR;
                return 0;
            }
            replay_accum_ns = 0;
            if (replay_pos >= replay_count) replay_complete();
            return 1;
        }
        int permille = (int)((replay_anim_elapsed_ms * 1000) / dur);
        if (permille > 999) permille = 999;
        if (replay_anim_render_at(permille) != 0) {
            snprintf(replay_error, sizeof replay_error,
                     "Cannot render animated stroke.");
            replay_state = REPLAY_ERROR;
            return 0;
        }
        return 1;
    }
    Uint64 ns_per = 1000000000ULL / (Uint64)replay_speeds[replay_speed_index];
    replay_accum_ns += elapsed * 1000000ULL;
    int applied = 0;
    while (replay_pos < replay_count && replay_accum_ns >= ns_per && applied < 2) {
        PressureCommand pc;
        if (ui_smooth_strokes && replay_next_pressure(&pc)) {
            int began = replay_anim_begin();
            if (began < 0) return 0;
            if (began > 0) { applied++; break; }
        }
        replay_apply_one();
        replay_accum_ns -= ns_per;
        applied++;
    }
    if (replay_accum_ns > ns_per * 4) replay_accum_ns = ns_per * 4;
    if (replay_pos >= replay_count) replay_complete();
    return applied;
}

/* Test/demo helper: finish the prepared replay, rendering partial frames
 * deterministically (no wall-clock dependency). */
static int replay_finish_smooth(int steps) {
    while (replay_pos < replay_count || replay_animating) {
        if (replay_animating) {
            for (int i = 1; i <= steps; i++) {
                int p = (int)((int64_t)i * 1000 / (steps + 1));
                if (p > 999) p = 999;
                if (replay_anim_render_at(p) != 0) return -1;
            }
            if (replay_anim_finish(1) != 0) return -1;
        } else {
            PressureCommand pc;
            if (ui_smooth_strokes && replay_next_pressure(&pc)) {
                int began = replay_anim_begin();
                if (began < 0) return -1;
                if (began == 0) replay_apply_one();
            } else {
                replay_apply_one();
            }
        }
    }
    replay_complete();
    return replay_state == REPLAY_COMPLETE ? 0 : -1;
}
static int replay_nav_key(SDL_Keycode sym, Uint16 mod) {
    if (!replay_nav_available() || !(mod & KMOD_CTRL)) return 0;
    if (sym == SDLK_z) {
        if (mod & KMOD_SHIFT) replay_step_forward();
        else replay_step_back();
        return 1;
    }
    if (sym == SDLK_y) {
        replay_step_forward();
        return 1;
    }
    return 0;
}

static int replay_busy(void) {
    return replay_state == REPLAY_PLAYING || replay_state == REPLAY_PAUSED;
}

static int run_script(void) {
    if (replay_locked()) {
        snprintf(script_error, sizeof script_error, "Playback is active.");
        return -1;
    }
    replay_forget();
    char *cmds = malloc(SCRIPT_CMD_BYTES);
    int count = 0;
    int offsets[REPLAY_MAX_COMMANDS], lengths[REPLAY_MAX_COMMANDS];
    if (!cmds || prepare_script_commands(cmds, SCRIPT_CMD_BYTES, &count,
                                          offsets, lengths) != 0) {
        free(cmds);
        return -1;
    }
    Uint32 *pre = snapshot_canvas();
    if (!pre) {
        free(cmds);
        snprintf(script_error, sizeof script_error, "Cannot snapshot drawing layer.");
        return -1;
    }
    int unsaved_before = unsaved;
    history_replaying = 1;
    for (int i = 0; i < count; i++) {
        int saved = 0;
        command(cmds + offsets[i], &saved);
    }
    history_replaying = 0;
    history_stack_clear(&redo_stack);
    history_push(&undo_stack, pre);
    unsaved = 1;
    snprintf(script_message, sizeof script_message, "Script applied.");
    dirty = 1;
    free(cmds);
    (void)unsaved_before;
    return 0;
}

static int map_key(SDL_Keycode k, Uint16 mod) {
    switch (k) {
        case SDLK_1: case SDLK_KP_1: return '1';
        case SDLK_2: case SDLK_KP_2: return '2';
        case SDLK_3: case SDLK_KP_3: return '3';
        case SDLK_4: case SDLK_KP_4: return '4';
        case SDLK_PLUS: case SDLK_KP_PLUS: return '+';
        case SDLK_MINUS: case SDLK_KP_MINUS: return '-';
        case SDLK_EQUALS: return (mod & KMOD_SHIFT) ? '+' : '=';
        case SDLK_e: case SDLK_b: case SDLK_c: case SDLK_s: return (int)('a' + (k - SDLK_a));
        case SDLK_ESCAPE: return 27;
        default: return (int)k;
    }
}

static int event_key(SDL_Keycode sym, Uint16 mod, int *saved) {
    if (replay_locked()) {
        if (map_key(sym, mod) == 'c') replay_cancel_for_lifecycle();
        else return 0;
    }
    char src[128], cmdbuf[2048];
    snprintf(src, sizeof src, "paint/key %d", map_key(sym, mod));
    if (glon_call(src, cmdbuf, sizeof cmdbuf) != 0) return -1;
    process_commands(cmdbuf, saved);
    return 0;
}
static int event_mouse(int type, int wx, int wy, int button, int *saved) {
    if (replay_locked()) return 0;
    int ww, wh, x, y;
    SDL_GetWindowSize(win, &ww, &wh);
    if (!map_window_to_canvas(ww, wh, wx, wy, &x, &y)) return -1;
    char src[128], cmdbuf[4096];
    if (type == SDL_MOUSEBUTTONDOWN)
        snprintf(src, sizeof src, "paint/down %d %d %d", x, y, button);
    else if (type == SDL_MOUSEBUTTONUP)
        snprintf(src, sizeof src, "paint/up %d %d %d", x, y, button);
    else
        snprintf(src, sizeof src, "paint/move %d %d", x, y);
    if (glon_call(src, cmdbuf, sizeof cmdbuf) != 0) return -1;
    process_commands(cmdbuf, saved);
    return 0;
}

static int app_init(void) {
    char cmdbuf[2048];
    int saved = 0;
    if (glon_call("paint/init", cmdbuf, sizeof cmdbuf) != 0) return -1;
    process_commands(cmdbuf, &saved);
    return 0;
}
static int app_call(const char *src) {
    char cmdbuf[2048];
    int saved = 0;
    if (glon_call(src, cmdbuf, sizeof cmdbuf) != 0) return -1;
    process_commands(cmdbuf, &saved);
    return 0;
}
static int app_key_code(int code) {
    char src[64];
    snprintf(src, sizeof src, "paint/key %d", code);
    return app_call(src);
}
/* Clear is a document lifecycle action: cancel replay/animation first, then
 * let Glon emit and the host execute the ordinary Clear command. */
static void document_clear(void) {
    replay_cancel_for_lifecycle();
    app_key_code(99);
}
static int app_set_color(int c) {
    char src[64];
    snprintf(src, sizeof src, "paint/set-color %d", c);
    return app_call(src);
}
static int app_set_size(int n) {
    char src[64];
    snprintf(src, sizeof src, "paint/set-size %d", n);
    return app_call(src);
}
static int app_set_tool(int t) {
    char src[64];
    snprintf(src, sizeof src, "paint/set-tool %d", t);
    return app_call(src);
}
static int app_set_calligraphy_diameter(int n) {
    char src[64];
    snprintf(src, sizeof src, "paint/set-calligraphy-diameter %d", n);
    return app_call(src);
}
static int app_set_pressure(int n) {
    char src[64];
    snprintf(src, sizeof src, "paint/set-pressure %d", n);
    return app_call(src);
}
static int app_set_ink_opacity(int n) {
    char src[64];
    snprintf(src, sizeof src, "paint/set-ink-opacity %d", n);
    return app_call(src);
}
static const char *tool_name(int t) {
    switch (t) {
    case 2: return "Eraser";
    case 3: return "Calligraphy";
    case 4: return "Line";
    case 5: return "Rectangle";
    case 6: return "Ellipse";
    case 7: return "Bucket";
    default: return "Brush";
    }
}
static int dispatch_canvas_pointer(int type, int wx, int wy, int button, int *saved) {
    if (replay_locked()) return 0;
    if (point_in_ui(wx, wy)) return 0;
    if (!point_in_rect(&layout.image, wx, wy)) return 0;
    return event_mouse(type, wx, wy, button, saved);
}

static int wslg_wayland_session(void) {
    return getenv("WSL_DISTRO_NAME") && getenv("WAYLAND_DISPLAY");
}
typedef enum {
    DECOR_DEFAULT = 0, DECOR_PATCHED_LIBDECOR, DECOR_EXTERNAL_LIBDECOR,
    DECOR_DISABLED
} DecorMode;
static DecorMode decor_mode = DECOR_DEFAULT;
static char libdecor_plugin_dir[PATH_MAX] = "";
static const char *decor_mode_name(void) {
    switch (decor_mode) {
    case DECOR_PATCHED_LIBDECOR: return "libdecor-patched";
    case DECOR_EXTERNAL_LIBDECOR: return "libdecor-external";
    case DECOR_DISABLED: return "disabled";
    default: return "default";
    }
}
static int libdecor_legacy_plugin_abi(void) {
    void *handle = dlopen("libdecor-0.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!handle) return 0;
    int legacy = dlsym(handle, "libdecor_configuration_get_window_size") != NULL;
    dlclose(handle);
    return legacy;
}
static int paint_libdecor_plugin_dir(char *out, size_t cap) {
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0 || (size_t)n >= sizeof exe - 1) return 0;
    exe[n] = 0;
    char *slash = strrchr(exe, '/');
    if (!slash) return 0;
    *slash = 0;
    int written = snprintf(out, cap, "%s/libdecor", exe);
    if (written < 0 || (size_t)written >= cap) return 0;
    return access(out, R_OK) == 0;
}
static void configure_decorations(void) {
    const char *video_driver = getenv("SDL_VIDEODRIVER");
    if (!wslg_wayland_session() ||
        (video_driver && strcmp(video_driver, "wayland") != 0))
        return;
    const char *allow = getenv("SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR");
    if (allow && strcmp(allow, "0") == 0) {
        decor_mode = DECOR_DISABLED;
        return;
    }
    if (getenv("LIBDECOR_PLUGIN_DIR")) {
        decor_mode = DECOR_EXTERNAL_LIBDECOR;
        return;
    }
    if (libdecor_legacy_plugin_abi() &&
        paint_libdecor_plugin_dir(libdecor_plugin_dir,
                                  sizeof libdecor_plugin_dir)) {
        setenv("LIBDECOR_PLUGIN_DIR", libdecor_plugin_dir, 1);
        decor_mode = DECOR_PATCHED_LIBDECOR;
        return;
    }
    /* No ABI-compatible patched decoration plugin. Prefer an undecorated
     * window over a stale decoration shadow, but keep this visible. */
    setenv("SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR", "0", 1);
    decor_mode = DECOR_DISABLED;
    fprintf(stderr, "glon-paint: no patched libdecor plugin; window decorations disabled\n");
}
static int sdl_init(int hidden) {
    /* WSLg's X11/Xwayland path leaves the Windows pointer hidden over SDL X11
     * windows even when SDL says the cursor is visible. Prefer Wayland there;
     * an explicit SDL_VIDEODRIVER still wins. */
    if (!getenv("SDL_VIDEODRIVER") && wslg_wayland_session())
        setenv("SDL_VIDEODRIVER", "wayland", 1);
    /* WSLg has no server-side decoration protocol. SDL must use client-side
     * libdecor decorations, but its stock shadow surface is left stale by the
     * compositor on maximize. Select our patched cairo plugin, which keeps the
     * border surface mapped but transparent while maximized, so the title bar,
     * drag and resize borders stay available without the ghost shadow. */
    configure_decorations();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "glon-paint: SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    int img_flags = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(img_flags) & img_flags) != img_flags) {
        fprintf(stderr, "glon-paint: SDL_image: %s\n", IMG_GetError());
        SDL_Quit();
        return -1;
    }
    Uint32 flags = hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;
    win = SDL_CreateWindow("Glon Paint", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           1200, 800, flags | SDL_WINDOW_RESIZABLE);
    if (!win) { fprintf(stderr, "glon-paint: window: %s\n", SDL_GetError()); SDL_Quit(); return -1; }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "glon-paint: renderer: %s\n", SDL_GetError()); SDL_DestroyWindow(win); SDL_Quit(); return -1; }
    background = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_TARGET, PAINT_W, PAINT_H);
    canvas = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                               SDL_TEXTUREACCESS_TARGET, PAINT_W, PAINT_H);
    composite = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_TARGET, PAINT_W, PAINT_H);
    if (!background || !canvas || !composite) {
        fprintf(stderr, "glon-paint: canvas: %s\n", SDL_GetError());
        if (background) SDL_DestroyTexture(background);
        if (canvas) SDL_DestroyTexture(canvas);
        if (composite) SDL_DestroyTexture(composite);
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); IMG_Quit(); SDL_Quit();
        return -1;
    }
    clear_background();
    clear_canvas();
    update_layout();
    return 0;
}

static void ui_style_dark(void) {
    struct nk_color table[NK_COLOR_COUNT];
    memcpy(table, nk_default_color_style, sizeof table);
    table[NK_COLOR_TEXT] = nk_rgba(226, 228, 232, 255);
    table[NK_COLOR_WINDOW] = nk_rgba(33, 35, 40, 255);
    table[NK_COLOR_HEADER] = nk_rgba(43, 46, 52, 255);
    table[NK_COLOR_BORDER] = nk_rgba(66, 70, 78, 255);
    table[NK_COLOR_BUTTON] = nk_rgba(52, 56, 64, 255);
    table[NK_COLOR_BUTTON_HOVER] = nk_rgba(67, 73, 84, 255);
    table[NK_COLOR_BUTTON_ACTIVE] = nk_rgba(78, 116, 165, 255);
    table[NK_COLOR_TOGGLE] = nk_rgba(52, 56, 64, 255);
    table[NK_COLOR_TOGGLE_HOVER] = nk_rgba(70, 78, 92, 255);
    table[NK_COLOR_TOGGLE_CURSOR] = nk_rgba(75, 132, 196, 255);
    table[NK_COLOR_SELECT] = nk_rgba(48, 52, 60, 255);
    table[NK_COLOR_SELECT_ACTIVE] = nk_rgba(61, 103, 153, 255);
    table[NK_COLOR_SLIDER] = nk_rgba(47, 51, 58, 255);
    table[NK_COLOR_SLIDER_CURSOR] = nk_rgba(75, 132, 196, 255);
    table[NK_COLOR_SLIDER_CURSOR_HOVER] = nk_rgba(92, 152, 220, 255);
    table[NK_COLOR_SLIDER_CURSOR_ACTIVE] = nk_rgba(110, 172, 238, 255);
    table[NK_COLOR_PROPERTY] = nk_rgba(45, 49, 56, 255);
    table[NK_COLOR_EDIT] = nk_rgba(28, 30, 35, 255);
    table[NK_COLOR_EDIT_CURSOR] = nk_rgba(220, 225, 232, 255);
    table[NK_COLOR_COMBO] = nk_rgba(40, 43, 49, 255);
    table[NK_COLOR_CHART] = nk_rgba(45, 49, 56, 255);
    table[NK_COLOR_CHART_COLOR] = nk_rgba(75, 132, 196, 255);
    table[NK_COLOR_CHART_COLOR_HIGHLIGHT] = nk_rgba(120, 180, 245, 255);
    table[NK_COLOR_SCROLLBAR] = nk_rgba(35, 38, 43, 255);
    table[NK_COLOR_SCROLLBAR_CURSOR] = nk_rgba(80, 86, 96, 255);
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = nk_rgba(100, 108, 120, 255);
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba(120, 130, 145, 255);
    table[NK_COLOR_TAB_HEADER] = nk_rgba(48, 52, 60, 255);
    table[NK_COLOR_KNOB] = nk_rgba(47, 51, 58, 255);
    table[NK_COLOR_KNOB_CURSOR] = nk_rgba(75, 132, 196, 255);
    table[NK_COLOR_KNOB_CURSOR_HOVER] = nk_rgba(92, 152, 220, 255);
    table[NK_COLOR_KNOB_CURSOR_ACTIVE] = nk_rgba(110, 172, 238, 255);
    nk_style_from_table(nk, table);
    nk->style.window.padding = nk_vec2(6, 6);
    nk->style.window.group_padding = nk_vec2(4, 4);
    nk->style.window.spacing = nk_vec2(4, 4);
    nk->style.menu_button.padding = nk_vec2(8, 4);
}
static int ui_init(void) {
    nk = nk_sdl_init(win, ren);
    if (!nk) return -1;
    struct nk_font_atlas *atlas = NULL;
    nk_sdl_font_stash_begin(&atlas);
    struct nk_font_config config = nk_font_config(0);
    struct nk_font *font = nk_font_atlas_add_default(atlas, 13, &config);
    nk_sdl_font_stash_end();
    if (font) nk_style_set_font(nk, &font->handle);
    ui_style_dark();
    return 0;
}
static void ui_shutdown(void) {
    if (nk) { nk_sdl_shutdown(); nk = NULL; }
}
static struct nk_rect ui_rect(int x, int y, int w, int h) {
    return nk_rect((float)x, (float)y, (float)w, (float)h);
}
static void ui_menus(void) {
    if (nk_begin(nk, "##menubar", ui_rect(layout.menu.x, layout.menu.y,
                                          layout.menu.w, layout.menu.h),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER)) {
        nk_menubar_begin(nk);
        nk_layout_row_static(nk, 22, 64, 4);
        if (nk_menu_begin_label(nk, "File", NK_TEXT_LEFT, nk_vec2(190, 240))) {
            nk_layout_row_dynamic(nk, 24, 1);
            if (nk_menu_item_label(nk, "New / Clear", NK_TEXT_LEFT)) document_clear();
            if (!replay_locked()) {
                if (nk_menu_item_label(nk, "Open Image...", NK_TEXT_LEFT)) request_open();
            } else {
                nk_widget_disable_begin(nk);
                nk_menu_item_label(nk, "Open Image...", NK_TEXT_LEFT);
                nk_widget_disable_end(nk);
            }
            /* Loading a script cancels replay on success. */
            if (nk_menu_item_label(nk, "Open Script...", NK_TEXT_LEFT)) request_open_script();
            if (nk_menu_item_label(nk, "Save PNG", NK_TEXT_LEFT)) app_key_code(115);
            if (script_loaded && !replay_locked()) {
                if (nk_menu_item_label(nk, "Run Script", NK_TEXT_LEFT)) run_script();
            } else {
                nk_widget_disable_begin(nk);
                nk_menu_item_label(nk, "Run Script", NK_TEXT_LEFT);
                nk_widget_disable_end(nk);
            }
            nk_widget_disable_begin(nk);
            nk_menu_item_label(nk, "Export...", NK_TEXT_LEFT);
            nk_widget_disable_end(nk);
            if (nk_menu_item_label(nk, "Quit", NK_TEXT_LEFT)) app_key_code(27);
            nk_menu_end(nk);
        }
        if (nk_menu_begin_label(nk, "Edit", NK_TEXT_LEFT, nk_vec2(170, 170))) {
            nk_layout_row_dynamic(nk, 24, 1);
            if (nk_menu_item_label(nk, "Clear Canvas", NK_TEXT_LEFT)) document_clear();
            if (replay_nav_available()) {
                if (replay_pos > 0) {
                    if (nk_menu_item_label(nk, "Replay Step Back  Ctrl+Z", NK_TEXT_LEFT))
                        replay_step_back();
                } else {
                    nk_widget_disable_begin(nk);
                    nk_menu_item_label(nk, "Replay Step Back  Ctrl+Z", NK_TEXT_LEFT);
                    nk_widget_disable_end(nk);
                }
                if (replay_pos < replay_count) {
                    if (nk_menu_item_label(nk, "Replay Step Forward  Ctrl+Y", NK_TEXT_LEFT))
                        replay_step_forward();
                } else {
                    nk_widget_disable_begin(nk);
                    nk_menu_item_label(nk, "Replay Step Forward  Ctrl+Y", NK_TEXT_LEFT);
                    nk_widget_disable_end(nk);
                }
            } else if (!replay_locked() && history_can_undo()) {
                if (nk_menu_item_label(nk, "Undo  Ctrl+Z", NK_TEXT_LEFT)) history_undo();
            } else {
                nk_widget_disable_begin(nk);
                nk_menu_item_label(nk, "Undo  Ctrl+Z", NK_TEXT_LEFT);
                nk_widget_disable_end(nk);
            }
            if (!replay_nav_available() && !replay_locked() && history_can_redo()) {
                if (nk_menu_item_label(nk, "Redo  Ctrl+Y", NK_TEXT_LEFT)) history_redo();
            } else if (!replay_nav_available()) {
                nk_widget_disable_begin(nk);
                nk_menu_item_label(nk, "Redo  Ctrl+Y", NK_TEXT_LEFT);
                nk_widget_disable_end(nk);
            }
            nk_menu_end(nk);
        }
        if (nk_menu_begin_label(nk, "View", NK_TEXT_LEFT, nk_vec2(160, 140))) {
            nk_layout_row_dynamic(nk, 24, 1);
            if (nk_menu_item_label(nk, "Reset Window Size", NK_TEXT_LEFT)) {
                SDL_SetWindowSize(win, 1200, 800);
                dirty = 1;
            }
            nk_widget_disable_begin(nk);
            nk_menu_item_label(nk, "Toggle Grid", NK_TEXT_LEFT);
            nk_widget_disable_end(nk);
            nk_menu_end(nk);
        }
        if (nk_menu_begin_label(nk, "Help", NK_TEXT_LEFT, nk_vec2(150, 110))) {
            nk_layout_row_dynamic(nk, 24, 1);
            if (nk_menu_item_label(nk, "About Glon Paint", NK_TEXT_LEFT)) show_about = 1;
            nk_menu_end(nk);
        }
        nk_menubar_end(nk);
    }
    nk_end(nk);
}
static void ui_toolbar(void) {
    if (nk_begin(nk, "##toolbar", ui_rect(layout.toolbar.x, layout.toolbar.y,
                                         layout.toolbar.w, layout.toolbar.h),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER)) {
        nk_layout_row_dynamic(nk, 22, 1);
        nk_label(nk, "TOOLS", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 34, 1);
        struct nk_rect bb = nk_widget_bounds(nk);
        ui_brush_bounds = (SDL_Rect){ (int)bb.x, (int)bb.y, (int)bb.w, (int)bb.h };
        if (nk_option_label(nk, "Brush", ui_tool == 1)) app_set_tool(1);
        nk_layout_row_dynamic(nk, 34, 1);
        struct nk_rect eb = nk_widget_bounds(nk);
        ui_eraser_bounds = (SDL_Rect){ (int)eb.x, (int)eb.y, (int)eb.w, (int)eb.h };
        if (nk_option_label(nk, "Eraser", ui_tool == 2)) app_set_tool(2);
        nk_layout_row_dynamic(nk, 34, 1);
        struct nk_rect cb2 = nk_widget_bounds(nk);
        ui_calligraphy_bounds = (SDL_Rect){ (int)cb2.x, (int)cb2.y, (int)cb2.w, (int)cb2.h };
        if (nk_option_label(nk, "Calligraphy", ui_tool == 3)) app_set_tool(3);
        nk_layout_row_dynamic(nk, 34, 1);
        struct nk_rect lb = nk_widget_bounds(nk);
        ui_line_bounds = (SDL_Rect){ (int)lb.x, (int)lb.y, (int)lb.w, (int)lb.h };
        if (nk_option_label(nk, "Line", ui_tool == 4)) app_set_tool(4);
        nk_layout_row_dynamic(nk, 34, 1);
        struct nk_rect rb = nk_widget_bounds(nk);
        ui_rect_bounds = (SDL_Rect){ (int)rb.x, (int)rb.y, (int)rb.w, (int)rb.h };
        if (nk_option_label(nk, "Rectangle", ui_tool == 5)) app_set_tool(5);
        nk_layout_row_dynamic(nk, 34, 1);
        struct nk_rect elb = nk_widget_bounds(nk);
        ui_ellipse_bounds = (SDL_Rect){ (int)elb.x, (int)elb.y, (int)elb.w, (int)elb.h };
        if (nk_option_label(nk, "Ellipse", ui_tool == 6)) app_set_tool(6);
        nk_layout_row_dynamic(nk, 34, 1);
        struct nk_rect bk = nk_widget_bounds(nk);
        ui_bucket_bounds = (SDL_Rect){ (int)bk.x, (int)bk.y, (int)bk.w, (int)bk.h };
        if (nk_option_label(nk, "Bucket", ui_tool == 7)) app_set_tool(7);
        nk_layout_row_dynamic(nk, 18, 1);
        nk_label(nk, "size", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 24, 1);
        nk_label(nk, tool_name(ui_tool), NK_TEXT_LEFT);
    }
    nk_end(nk);
}
static const char *replay_state_name(void) {
    switch (replay_state) {
    case REPLAY_PLAYING: return "Playing";
    case REPLAY_PAUSED: return "Paused";
    case REPLAY_COMPLETE: return "Complete";
    case REPLAY_ENDED: return "Ended";
    case REPLAY_ERROR: return "Error";
    default: return "Ready";
    }
}
static void ui_right_panel(void) {
    if (nk_begin(nk, "##inspector", ui_rect(layout.right.x, layout.right.y,
                                            layout.right.w, layout.right.h),
                 NK_WINDOW_BORDER)) {
        nk_layout_row_dynamic(nk, 22, 1);
        nk_label(nk, "COLOUR", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 34, 4);
        struct nk_rect cb = nk_widget_bounds(nk);
        ui_color_bounds[0] = (SDL_Rect){ (int)cb.x, (int)cb.y, (int)cb.w, (int)cb.h };
        if (nk_button_color(nk, nk_rgb(220, 60, 60))) app_set_color(1);
        cb = nk_widget_bounds(nk);
        ui_color_bounds[1] = (SDL_Rect){ (int)cb.x, (int)cb.y, (int)cb.w, (int)cb.h };
        if (nk_button_color(nk, nk_rgb(40, 120, 220))) app_set_color(2);
        cb = nk_widget_bounds(nk);
        ui_color_bounds[2] = (SDL_Rect){ (int)cb.x, (int)cb.y, (int)cb.w, (int)cb.h };
        if (nk_button_color(nk, nk_rgb(40, 160, 80))) app_set_color(3);
        cb = nk_widget_bounds(nk);
        ui_color_bounds[3] = (SDL_Rect){ (int)cb.x, (int)cb.y, (int)cb.w, (int)cb.h };
        if (nk_button_color(nk, nk_rgb(230, 170, 40))) app_set_color(4);
        nk_layout_row_dynamic(nk, 20, 1);
        nk_label(nk, ui_color == 1 ? "Red" : ui_color == 2 ? "Blue" :
                     ui_color == 3 ? "Green" : ui_color == 5 ? "Black" :
                     "Yellow", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 20, 1);
        nk_label(nk, "BRUSH SIZE", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 28, 1);
        struct nk_rect sb = nk_widget_bounds(nk);
        ui_size_bounds = (SDL_Rect){ (int)sb.x, (int)sb.y, (int)sb.w, (int)sb.h };
        int new_size = ui_size;
        if (nk_slider_int(nk, 1, &new_size, 64, 1)) {
            if (new_size != ui_size) app_set_size(new_size);
        }
        char bsz[32]; snprintf(bsz, sizeof bsz, "%d px", ui_size);
        nk_layout_row_dynamic(nk, 20, 1);
        nk_label(nk, bsz, NK_TEXT_RIGHT);
        nk_layout_row_dynamic(nk, 20, 1);
        nk_label(nk, "CALLIGRAPHY", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 20, 1);
        char cds[32]; snprintf(cds, sizeof cds, "Max diameter %d px", ui_call_diameter);
        nk_label(nk, cds, NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 26, 1);
        int new_dia = ui_call_diameter;
        if (nk_slider_int(nk, 1, &new_dia, 64, 1) && new_dia != ui_call_diameter)
            app_set_calligraphy_diameter(new_dia);
        nk_layout_row_dynamic(nk, 20, 1);
        char prs[32]; snprintf(prs, sizeof prs, "Pressure %d", ui_pressure);
        nk_label(nk, prs, NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 26, 1);
        int new_pressure = ui_pressure;
        if (nk_slider_int(nk, 0, &new_pressure, 255, 1) && new_pressure != ui_pressure)
            app_set_pressure(new_pressure);
        nk_layout_row_dynamic(nk, 20, 1);
        char ops[32]; snprintf(ops, sizeof ops, "Ink opacity %d", ui_opacity);
        nk_label(nk, ops, NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 26, 1);
        int new_opacity = ui_opacity;
        if (nk_slider_int(nk, 0, &new_opacity, 255, 1) && new_opacity != ui_opacity)
            app_set_ink_opacity(new_opacity);
        nk_layout_row_dynamic(nk, 20, 1);
        nk_label(nk, "TOOL PROPERTIES", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 20, 1);
        nk_label(nk, tool_name(ui_tool), NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 20, 1);
        nk_label(nk, ui_tool == 3 ? "Circular pressure tip" :
                     ui_tool >= 4 ? "Standard geometric shape" :
                     "Opacity 100% (fixed)", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 20, 1);
        nk_label(nk, ui_tool >= 4 ? "Stroke = brush size" :
                     "Hardness 100% (fixed)", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 24, 1);
        nk_checkbox_label(nk, "Fill shapes", &ui_fill_shapes);
        nk_layout_row_dynamic(nk, 20, 1);
        char bt[48];
        snprintf(bt, sizeof bt, "Bucket tolerance %d", ui_bucket_tolerance);
        nk_label(nk, bt, NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 26, 1);
        struct nk_rect tb = nk_widget_bounds(nk);
        ui_bucket_tol_bounds = (SDL_Rect){ (int)tb.x, (int)tb.y, (int)tb.w, (int)tb.h };
        nk_slider_int(nk, 0, &ui_bucket_tolerance, 255, 1);
        if (bucket_message[0]) nk_label_wrap(nk, bucket_message);
        nk_layout_row_dynamic(nk, 20, 1);
        nk_label(nk, "SCRIPT", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 28, 1);
        if (script_loaded && !replay_locked()) {
            if (nk_button_label(nk, "Run Script")) run_script();
        } else {
            nk_widget_disable_begin(nk);
            nk_button_label(nk, "Run Script");
            nk_widget_disable_end(nk);
        }
        nk_layout_row_dynamic(nk, 18, 1);
        if (script_error[0]) nk_label_wrap(nk, script_error);
        else if (script_message[0]) nk_label_wrap(nk, script_message);
        else nk_label(nk, "Open Script from File", NK_TEXT_LEFT);

        nk_layout_row_dynamic(nk, 20, 1);
        nk_label(nk, "REPLAY (not document Undo)", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 18, 1);
        char rs[160];
        snprintf(rs, sizeof rs, "%s  %d%s/%d  %d ops/s  %s", replay_state_name(),
                 replay_pos, replay_animating ? "+1" : "", replay_count,
                 replay_speeds[replay_speed_index],
                 ui_smooth_strokes ? "smooth" : "whole");
        nk_label(nk, rs, NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 24, 2);
        int smooth_before = ui_smooth_strokes;
        if (nk_checkbox_label(nk, "Smooth strokes", &ui_smooth_strokes)) {
            if (smooth_before && !ui_smooth_strokes && replay_animating) {
                int was_playing = replay_state == REPLAY_PLAYING;
                if (replay_anim_finish(1) == 0) {
                    if (replay_pos >= replay_count) replay_complete();
                    else {
                        replay_state = was_playing ? REPLAY_PLAYING : REPLAY_PAUSED;
                        replay_accum_ns = 0;
                        replay_last_tick = SDL_GetTicks64();
                    }
                }
            }
        }
        nk_checkbox_label(nk, "Brush tip", &ui_show_brush_tip);
        nk_layout_row_dynamic(nk, 20, 1);
        nk_label(nk, "Stroke duration", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 24, 4);
        static const char *stroke_durations[] = { "0.15s", "0.3s", "0.6s", "1.2s" };
        for (int i = 0; i < 4; i++) {
            if (nk_option_label(nk, stroke_durations[i],
                                ui_stroke_duration_index == i))
                ui_stroke_duration_index = i;
        }
        nk_layout_row_dynamic(nk, 28, 3);
        if (replay_state != REPLAY_PLAYING && script_loaded) {
            if (nk_button_label(nk, "Play")) replay_play();
        } else {
            nk_widget_disable_begin(nk);
            nk_button_label(nk, "Play");
            nk_widget_disable_end(nk);
        }
        if (replay_state == REPLAY_PLAYING) {
            if (nk_button_label(nk, "Pause")) replay_pause();
        } else {
            nk_widget_disable_begin(nk);
            nk_button_label(nk, "Pause");
            nk_widget_disable_end(nk);
        }
        if (replay_nav_available()) {
            if (nk_button_label(nk, "Stop")) replay_stop();
        } else {
            nk_widget_disable_begin(nk);
            nk_button_label(nk, "Stop");
            nk_widget_disable_end(nk);
        }
        nk_layout_row_dynamic(nk, 28, 3);
        if (replay_nav_available() && (replay_pos > 0 || replay_animating)) {
            if (nk_button_label(nk, "Back")) replay_step_back();
        } else {
            nk_widget_disable_begin(nk);
            nk_button_label(nk, "Back");
            nk_widget_disable_end(nk);
        }
        if (script_loaded &&
            (replay_state != REPLAY_PLAYING || replay_animating) &&
            replay_pos < replay_count) {
            if (nk_button_label(nk, "Step")) replay_step_forward();
        } else {
            nk_widget_disable_begin(nk);
            nk_button_label(nk, "Step");
            nk_widget_disable_end(nk);
        }
        if (replay_nav_available()) {
            if (nk_button_label(nk, "End")) replay_end();
        } else {
            nk_widget_disable_begin(nk);
            nk_button_label(nk, "End");
            nk_widget_disable_end(nk);
        }
        nk_layout_row_dynamic(nk, 24, 4);
        static const char *speeds[] = { "1/s", "5/s", "20/s", "60/s" };
        for (int i = 0; i < 4; i++) {
            if (nk_option_label(nk, speeds[i], replay_speed_index == i)) {
                replay_speed_index = i;
                replay_accum_ns = 0;
            }
        }
        if (replay_error[0]) nk_label_wrap(nk, replay_error);
    }
    nk_end(nk);
}
static void ui_layers(void) {
    if (nk_begin(nk, "##layers", ui_rect(layout.layers.x, layout.layers.y,
                                         layout.layers.w, layout.layers.h),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER)) {
        nk_layout_row_dynamic(nk, 22, 1);
        nk_label(nk, "LAYERS", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 28, 1);
        nk_label(nk, "[x] Canvas (working layer)", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 18, 1);
        nk_label(nk, "1 layer", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 24, 2);
        nk_widget_disable_begin(nk);
        nk_button_label(nk, "+");
        nk_button_label(nk, "-");
        nk_widget_disable_end(nk);
    }
    nk_end(nk);
}
static void ui_status(void) {
    if (nk_begin(nk, "##status", ui_rect(layout.status.x, layout.status.y,
                                         layout.status.w, layout.status.h),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER)) {
        nk_layout_row_dynamic(nk, 18, 1);
        char s[200];
#ifdef HAVE_NFD
        const char *chooser = (nfd_state == -1) ? "path fallback" : "native";
#else
        const char *chooser = "path fallback";
#endif
        snprintf(s, sizeof s, "Canvas 800 x 600   |   Tool: %s   |   Colour: %d   |   Size: %d px   |   Calligraphy: %d/%d/%d   |   Chooser: %s",
                 tool_name(ui_tool), ui_color, ui_size, ui_call_diameter,
                 ui_pressure, ui_opacity, chooser);
        nk_label(nk, s, NK_TEXT_LEFT);
    }
    nk_end(nk);
}
static void ui_file_dialogs(void) {
    int ww = 0, wh = 0;
    SDL_GetWindowSize(win, &ww, &wh);
    if (confirm_open) {
        int w = 430, h = 150;
        struct nk_rect r = ui_rect((ww - w) / 2, (wh - h) / 2, w, h);
        if (nk_begin(nk, "Unsaved Drawing", r,
                     NK_WINDOW_TITLE | NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
            nk_layout_row_dynamic(nk, 24, 1);
            nk_label_wrap(nk, "Current drawing has unsaved changes. Opening another image resets the drawing and undo/redo history.");
            nk_layout_row_dynamic(nk, 28, 2);
            if (nk_button_label(nk, "Discard and Open")) {
                confirm_open = 0; open_error[0] = 0;
                show_image_chooser();
            }
            if (nk_button_label(nk, "Cancel")) confirm_open = 0;
        }
        nk_end(nk);
    }
    if (open_dialog) {
        int w = 560, h = 210;
        struct nk_rect r = ui_rect((ww - w) / 2, (wh - h) / 2, w, h);
        if (nk_begin(nk, "Open Image", r,
                     NK_WINDOW_TITLE | NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
            nk_layout_row_dynamic(nk, 22, 1);
            nk_label(nk, "PNG or JPEG path:", NK_TEXT_LEFT);
            nk_layout_row_dynamic(nk, 30, 1);
            nk_edit_string_zero_terminated(nk, NK_EDIT_FIELD, open_path,
                                           (int)sizeof open_path, nk_filter_default);
            nk_layout_row_dynamic(nk, 20, 1);
            if (open_error[0]) nk_label_wrap(nk, open_error);
            else nk_label(nk, "The image is aspect-fit over white at 800x600.", NK_TEXT_LEFT);
            nk_layout_row_dynamic(nk, 30, 2);
            if (nk_button_label(nk, "Open")) {
                if (load_image_file(open_path) == 0) {
                    open_dialog = 0;
#ifdef HAVE_NFD
                    remember_directory(last_image_dir, sizeof last_image_dir, open_path);
#endif
                }
            }
            if (nk_button_label(nk, "Cancel")) open_dialog = 0;
        }
        nk_end(nk);
    }
}
static void ui_script_dialog(void) {
    if (!script_dialog) return;
    int ww = 0, wh = 0;
    SDL_GetWindowSize(win, &ww, &wh);
    int w = 600, h = 220;
    struct nk_rect r = ui_rect((ww - w) / 2, (wh - h) / 2, w, h);
    if (nk_begin(nk, "Open Glon Script", r,
                 NK_WINDOW_TITLE | NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(nk, 22, 1);
        nk_label(nk, "Glon source path:", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 30, 1);
        nk_edit_string_zero_terminated(nk, NK_EDIT_FIELD, script_path,
                                       (int)sizeof script_path, nk_filter_default);
        nk_layout_row_dynamic(nk, 20, 1);
        if (script_error[0]) nk_label_wrap(nk, script_error);
        else nk_label(nk, "The script is validated now; Run Script executes it.", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 30, 2);
        if (nk_button_label(nk, "Load Script")) {
            if (load_script_file(script_path) == 0) {
                script_dialog = 0;
#ifdef HAVE_NFD
                remember_directory(last_script_dir, sizeof last_script_dir, script_path);
#endif
            }
        }
        if (nk_button_label(nk, "Cancel")) script_dialog = 0;
    }
    nk_end(nk);
}
static void ui_build(void) {
    update_layout();
    ui_menus();
    ui_toolbar();
    ui_right_panel();
    ui_layers();
    ui_status();
    ui_file_dialogs();
    ui_script_dialog();
    if (show_about) {
        int w = 360, h = 160;
        int ww = 0, wh = 0; SDL_GetWindowSize(win, &ww, &wh);
        struct nk_rect r = ui_rect((ww - w) / 2, (wh - h) / 2, w, h);
        if (nk_begin(nk, "About Glon Paint", r,
                     NK_WINDOW_TITLE | NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
            nk_layout_row_dynamic(nk, 24, 1);
            nk_label(nk, "Glon Paint G2", NK_TEXT_LEFT);
            nk_layout_row_dynamic(nk, 22, 1);
            nk_label_wrap(nk, "A small Glon-owned painting surface. "
                              "The Glon VM, allocator and loader are unchanged.");
            nk_layout_row_dynamic(nk, 30, 1);
            if (nk_button_label(nk, "Close")) show_about = 0;
        }
        nk_end(nk);
    }
}
static void push_mouse(Uint32 type, int x, int y) {
    SDL_Event e; memset(&e, 0, sizeof e); e.type = type;
    if (type == SDL_MOUSEMOTION) { e.motion.x = x; e.motion.y = y; e.motion.state = SDL_BUTTON_LMASK; }
    else { e.button.x = x; e.button.y = y; e.button.button = SDL_BUTTON_LEFT; e.button.state = (type == SDL_MOUSEBUTTONDOWN) ? SDL_PRESSED : SDL_RELEASED; }
    SDL_PushEvent(&e);
}
static void push_key_mod(SDL_Keycode k, Uint16 mod) {
    SDL_Event e; memset(&e, 0, sizeof e); e.type = SDL_KEYDOWN;
    e.key.keysym.sym = k; e.key.keysym.mod = mod; SDL_PushEvent(&e);
}
static void push_key(SDL_Keycode k) { push_key_mod(k, 0); }
static long loader_used(void) { return (long)(M[GC_LOADER_HP] - R0S1_HEAP_BASE); }
static long loader_free_cells(void) { return (long)(R0S1_HEAP_LIMIT - M[GC_LOADER_HP]); }

static int read_output_pixel(int x, int y, Uint8 *rr, Uint8 *gg, Uint8 *bb) {
    SDL_SetRenderTarget(ren, NULL);
    SDL_Rect r = { x, y, 1, 1 };
    Uint32 px = 0;
    if (SDL_RenderReadPixels(ren, &r, SDL_PIXELFORMAT_ARGB8888, &px, 4) != 0) return -1;
    SDL_PixelFormat *fmt = SDL_AllocFormat(SDL_PIXELFORMAT_ARGB8888);
    if (!fmt) return -1;
    Uint8 a;
    SDL_GetRGBA(px, fmt, rr, gg, bb, &a);
    SDL_FreeFormat(fmt);
    return 0;
}
static Uint32 *read_output_frame(int *w, int *h) {
    SDL_SetRenderTarget(ren, NULL);
    int ow = 0, oh = 0;
    if (SDL_GetRendererOutputSize(ren, &ow, &oh) != 0 || ow < 1 || oh < 1)
        return NULL;
    Uint32 *px = malloc((size_t)ow * (size_t)oh * sizeof(Uint32));
    if (!px) return NULL;
    if (SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888,
                             px, ow * (int)sizeof(Uint32)) != 0) {
        free(px);
        return NULL;
    }
    *w = ow; *h = oh;
    return px;
}
static void frame_pixel(const Uint32 *px, int stride, int x, int y,
                        Uint8 *rr, Uint8 *gg, Uint8 *bb) {
    Uint32 v = px[(size_t)y * (size_t)stride + (size_t)x];
    *rr = (Uint8)((v >> 16) & 255);
    *gg = (Uint8)((v >> 8) & 255);
    *bb = (Uint8)(v & 255);
}
static int read_canvas_pixel(int x, int y, Uint8 *rr, Uint8 *gg, Uint8 *bb) {
    build_composite();
    SDL_SetRenderTarget(ren, composite);
    SDL_Rect r = { x, y, 1, 1 };
    Uint32 px = 0;
    if (SDL_RenderReadPixels(ren, &r, SDL_PIXELFORMAT_ARGB8888, &px, 4) != 0) {
        SDL_SetRenderTarget(ren, NULL);
        return -1;
    }
    SDL_PixelFormat *fmt = SDL_AllocFormat(SDL_PIXELFORMAT_ARGB8888);
    if (!fmt) { SDL_SetRenderTarget(ren, NULL); return -1; }
    Uint8 a;
    SDL_GetRGBA(px, fmt, rr, gg, bb, &a);
    SDL_FreeFormat(fmt);
    SDL_SetRenderTarget(ren, NULL);
    return 0;
}
static int resize_to(int w, int h) {
    SDL_SetWindowSize(win, w, h);
    SDL_PumpEvents();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_WINDOWEVENT &&
            (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
             e.window.event == SDL_WINDOWEVENT_RESIZED ||
             e.window.event == SDL_WINDOWEVENT_EXPOSED)) dirty = 1;
    }
    dirty = 1;
    render_frame();
    SDL_RenderPresent(ren);
    int ww = 0, wh = 0, tw = 0, th = 0;
    int ow = 0, oh = 0;
    SDL_GetWindowSize(win, &ww, &wh);
    SDL_GetRendererOutputSize(ren, &ow, &oh);
    if (ow < w || oh < h) return -1;
    if (ww != w || wh != h) return -1;
    if (SDL_QueryTexture(canvas, NULL, NULL, &tw, &th) != 0 ||
        tw != PAINT_W || th != PAINT_H) return -1;
    return 0;
}
static int resize_test(void) {
    int saved = 0;
    if (app_init() != 0) return 2;
    if (event_key(SDLK_1, 0, &saved) != 0) return 3;
    if (app_set_size(8) != 0) return 3;
    if (event_key(SDLK_c, 0, &saved) != 0) return 4;

    int x = -1, y = -1;
    if (map_window_to_canvas_for(1200, 800, 0, 0, &x, &y)) {
        fprintf(stderr, "resize-test: menu point mapped into canvas\n");
        return 5;
    }
    if (map_window_to_canvas_for(1200, 800, 1199, 799, &x, &y)) {
        fprintf(stderr, "resize-test: status point mapped into canvas\n");
        return 5;
    }

    struct { int w, h, cx, cy; } cases[] = {
        { 1200, 800, 100, 100 },
        { 1600, 1200, 400, 300 },
        { 600, 900, 600, 150 },
        { 1000, 500, 200, 480 },
        { 1200, 800, 700, 500 },
        { 900, 1200, 80, 60 },
        { 1200, 800, 100, 100 },
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        if (resize_to(cases[i].w, cases[i].h) != 0) {
            fprintf(stderr, "resize-test: resize to %dx%d failed\n", cases[i].w, cases[i].h);
            return 6;
        }
        int64_t lhs = (int64_t)layout.image.w * PAINT_H;
        int64_t rhs = (int64_t)layout.image.h * PAINT_W;
        if (lhs < 0 || rhs < 0 || (lhs > rhs ? lhs - rhs : rhs - lhs) > PAINT_W) {
            fprintf(stderr, "resize-test: image aspect wrong at %dx%d: %dx%d\n",
                    cases[i].w, cases[i].h, layout.image.w, layout.image.h);
            return 7;
        }
        if (layout.image.x < layout.workspace.x ||
            layout.image.y < layout.workspace.y ||
            layout.image.x + layout.image.w > layout.workspace.x + layout.workspace.w ||
            layout.image.y + layout.image.h > layout.workspace.y + layout.workspace.h ||
            abs((layout.image.x + layout.image.w / 2) -
                (layout.workspace.x + layout.workspace.w / 2)) > 1 ||
            abs((layout.image.y + layout.image.h / 2) -
                (layout.workspace.y + layout.workspace.h / 2)) > 1) {
            fprintf(stderr, "resize-test: image not centred/margined at %dx%d\n",
                    cases[i].w, cases[i].h);
            return 7;
        }
        int wx = 0, wy = 0;
        if (!canvas_to_window(cases[i].cx, cases[i].cy, &wx, &wy)) return 8;
        int mapped = map_window_to_canvas_for(cases[i].w, cases[i].h, wx, wy, &x, &y);
        if (!mapped || abs(x - cases[i].cx) > 2 || abs(y - cases[i].cy) > 2) {
            fprintf(stderr, "resize-test: map %dx%d canvas(%d,%d) -> win(%d,%d) -> "
                    "canvas(%d,%d) mapped=%d image=%d,%d,%d,%d\n",
                    cases[i].w, cases[i].h, cases[i].cx, cases[i].cy, wx, wy,
                    x, y, mapped, layout.image.x, layout.image.y,
                    layout.image.w, layout.image.h);
            return 9;
        }
        if (event_mouse(SDL_MOUSEBUTTONDOWN, wx, wy, SDL_BUTTON_LEFT, &saved) != 0 ||
            event_mouse(SDL_MOUSEBUTTONUP, wx, wy, SDL_BUTTON_LEFT, &saved) != 0)
            return 10;
        Uint8 r = 0, g = 0, b = 0;
        if (read_canvas_pixel(cases[i].cx, cases[i].cy, &r, &g, &b) != 0 ||
            r != 220 || g != 60 || b != 60) {
            fprintf(stderr, "resize-test: canvas (%d,%d) not painted at %dx%d\n",
                    cases[i].cx, cases[i].cy, cases[i].w, cases[i].h);
            return 11;
        }
        /* Verify the presented output, not only the logical canvas: the
         * painted pixel, workspace margin, shadow and outline must all be at
         * the current destination rectangle after the resize. */
        render_frame();
        int fw = 0, fh = 0;
        Uint32 *frame = read_output_frame(&fw, &fh);
        if (!frame || fw < cases[i].w || fh < cases[i].h) {
            free(frame);
            fprintf(stderr, "resize-test: output readback failed at %dx%d\n",
                    cases[i].w, cases[i].h);
            return 14;
        }
        frame_pixel(frame, fw, wx, wy, &r, &g, &b);
        if (r != 220 || g != 60 || b != 60) {
            free(frame);
            fprintf(stderr, "resize-test: output (%d,%d) not painted at %dx%d: %u,%u,%u\n",
                    wx, wy, cases[i].w, cases[i].h, r, g, b);
            return 14;
        }
        frame_pixel(frame, fw, layout.workspace.x + 2, layout.workspace.y + 2,
                    &r, &g, &b);
        if (r != 43 || g != 45 || b != 51) {
            free(frame);
            fprintf(stderr, "resize-test: workspace pixel wrong at %dx%d: %u,%u,%u\n",
                    cases[i].w, cases[i].h, r, g, b);
            return 15;
        }
        frame_pixel(frame, fw, layout.image.x - 1, layout.image.y - 1, &r, &g, &b);
        if (r != 8 || g != 9 || b != 11) {
            free(frame);
            fprintf(stderr, "resize-test: shadow pixel wrong at %dx%d: %u,%u,%u\n",
                    cases[i].w, cases[i].h, r, g, b);
            return 16;
        }
        frame_pixel(frame, fw, layout.image.x, layout.image.y + layout.image.h / 2,
                    &r, &g, &b);
        if (r != 92 || g != 96 || b != 106) {
            free(frame);
            fprintf(stderr, "resize-test: document outline wrong at %dx%d: %u,%u,%u\n",
                    cases[i].w, cases[i].h, r, g, b);
            return 17;
        }
        free(frame);
    }
    Uint8 r, g, b;
    if (read_canvas_pixel(100, 100, &r, &g, &b) != 0 || r != 220 || g != 60 || b != 60)
        return 12;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 || r != 220 || g != 60 || b != 60)
        return 12;
    if (read_canvas_pixel(80, 60, &r, &g, &b) != 0 || r != 220 || g != 60 || b != 60)
        return 12;
    if (event_key(SDLK_s, 0, &saved) != 0 || !saved) return 13;
    printf("resize-test: resizes=%u canvas=%dx%d image=%dx%d result=ok\n",
           (unsigned)(sizeof cases / sizeof cases[0]), PAINT_W, PAINT_H,
           layout.image.w, layout.image.h);
    present_frame();
    return 0;
}
static int move_stress(int moves) {
    int saved = 0;
    if (app_init() != 0) return 2;
    long loader0 = loader_used(), loader_max = loader0;
    int symbols0 = 0; while (r0_s1_sym_name(symbols0)) symbols0++;
    int wx = 0, wy = 0;
    if (!canvas_to_window(10, 10, &wx, &wy) ||
        event_mouse(SDL_MOUSEBUTTONDOWN, wx, wy, SDL_BUTTON_LEFT, &saved) != 0) return 3;
    Uint64 t0 = SDL_GetPerformanceCounter();
    int failed_at = -1;
    for (int i = 0; i < moves; i++) {
        int cx = 10 + (i % 760), cy = 10 + ((i / 760) % 560);
        if (!canvas_to_window(cx, cy, &wx, &wy)) return 4;
        if (event_mouse(SDL_MOUSEMOTION, wx, wy, 0, &saved) != 0) { failed_at = i; break; }
        long used = loader_used();
        if (used > loader_max) loader_max = used;
    }
    Uint64 t1 = SDL_GetPerformanceCounter();
    double elapsed_ms = (double)(t1 - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    if (failed_at < 0) {
        if (!canvas_to_window(760, 560, &wx, &wy) ||
            event_mouse(SDL_MOUSEBUTTONUP, wx, wy, SDL_BUTTON_LEFT, &saved) != 0) return 5;
        if (event_key(SDLK_s, 0, &saved) != 0) return 6;
    }
    printf("move-stress: moves=%d result=%s failed_at=%d loader_used_start=%ld "
           "loader_used_end=%ld loader_used_max=%ld loader_free=%ld "
           "managed_high=%ld gc_collects=%ld gc_live_cells=%ld elapsed_ms=%.2f\n",
           moves, failed_at < 0 ? "ok" : "FAILED", failed_at,
           loader0, loader_used(), loader_max, loader_free_cells(),
           r0_s1_heap_high(), r0_s1_gc_count(), r0_s1_gc_live_cells(), elapsed_ms);
    int symbols1 = 0; while (r0_s1_sym_name(symbols1)) symbols1++;
    printf("move-stress: symbols_start=%d symbols_end=%d\n", symbols0, symbols1);
    if (failed_at >= 0) return 7;
    long growth = loader_used() - loader0;
    if (growth > 16) {
        fprintf(stderr, "move-stress: loader grew by %ld cells (expected bounded)\n", growth);
        return 8;
    }
    return saved ? 0 : 9;
}
static int self_test(void) {
    int saved = 0;
    if (app_init() != 0) return 2;
    int x1, y1, x2, y2, x3, y3, x4, y4, x5, y5, x6, y6;
    if (!canvas_to_window(20, 20, &x1, &y1) ||
        !canvas_to_window(100, 100, &x2, &y2) ||
        !canvas_to_window(160, 60, &x3, &y3) ||
        !canvas_to_window(200, 100, &x4, &y4) ||
        !canvas_to_window(260, 100, &x5, &y5)) return 3;
    (void)x6; (void)y6;
    /* SDL queue path: clear, colour, size, stroke, eraser stroke, save, quit. */
    push_key(SDLK_c); push_key(SDLK_2); push_key(SDLK_PLUS);
    push_mouse(SDL_MOUSEBUTTONDOWN, x1, y1); push_mouse(SDL_MOUSEMOTION, x2, y2);
    push_mouse(SDL_MOUSEMOTION, x3, y3); push_mouse(SDL_MOUSEBUTTONUP, x3, y3);
    push_key(SDLK_e);
    push_mouse(SDL_MOUSEBUTTONDOWN, x4, y4); push_mouse(SDL_MOUSEMOTION, x5, y5);
    push_mouse(SDL_MOUSEBUTTONUP, x5, y5);
    push_key(SDLK_s); push_key(SDLK_ESCAPE);
    while (!quit_flag) {
        SDL_Event e;
        if (!SDL_WaitEventTimeout(&e, 20)) { if (dirty) present_frame(); continue; }
        do {
            if (e.type == SDL_QUIT) quit_flag = 1;
            else if (e.type == SDL_WINDOWEVENT &&
                     (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                      e.window.event == SDL_WINDOWEVENT_RESIZED ||
                      e.window.event == SDL_WINDOWEVENT_EXPOSED)) dirty = 1;
            else if (e.type == SDL_KEYDOWN) event_key(e.key.keysym.sym, e.key.keysym.mod, &saved);
            else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP)
                event_mouse(e.type, e.button.x, e.button.y, e.button.button, &saved);
            else if (e.type == SDL_MOUSEMOTION)
                event_mouse(e.type, e.motion.x, e.motion.y, 0, &saved);
            if (dirty) present_frame();
        } while (SDL_PollEvent(&e));
    }
    return saved ? 0 : 3;
}

static void ui_handle(SDL_Event *e) {
    nk_sdl_handle_event(e);
}
static void ui_click_at(SDL_Rect r) {
    SDL_Event e;
    int x = r.x + r.w / 2, y = r.y + r.h / 2;
    memset(&e, 0, sizeof e);
    e.type = SDL_MOUSEMOTION; e.motion.x = x; e.motion.y = y;
    nk_input_begin(nk); ui_handle(&e); nk_input_end(nk); ui_build(); nk_clear(nk);

    memset(&e, 0, sizeof e);
    e.type = SDL_MOUSEBUTTONDOWN; e.button.x = x; e.button.y = y;
    e.button.button = SDL_BUTTON_LEFT; e.button.clicks = 1;
    nk_input_begin(nk); ui_handle(&e); nk_input_end(nk); ui_build(); nk_clear(nk);

    memset(&e, 0, sizeof e);
    e.type = SDL_MOUSEBUTTONUP; e.button.x = x; e.button.y = y;
    e.button.button = SDL_BUTTON_LEFT; e.button.clicks = 1;
    nk_input_begin(nk); ui_handle(&e); nk_input_end(nk); ui_build(); nk_clear(nk);
    update_layout();
}

static int ui_test(void) {
    int saved = 0;
    if (app_init() != 0) return 2;
    if (event_key(SDLK_1, 0, &saved) != 0) return 3;
    if (app_set_size(8) != 0) return 4;
    if (event_key(SDLK_c, 0, &saved) != 0) return 5;
    update_layout();
    nk_input_begin(nk);
    nk_input_end(nk);
    ui_build();
    render_ui();
    Uint8 pr = 0, pg = 0, pb = 0;
    if (read_output_pixel(layout.image.x + layout.image.w / 2,
                          layout.image.y + layout.image.h / 2,
                          &pr, &pg, &pb) != 0 ||
        pr != 255 || pg != 255 || pb != 255) return 6;
    if (read_output_pixel(layout.right.x + 6, layout.right.y + 6,
                          &pr, &pg, &pb) != 0 ||
        (pr == 22 && pg == 23 && pb == 27)) return 6;
    SDL_RenderPresent(ren);

    int64_t lhs = (int64_t)layout.image.w * PAINT_H;
    int64_t rhs = (int64_t)layout.image.h * PAINT_W;
    if (lhs < 0 || rhs < 0 || (lhs > rhs ? lhs - rhs : rhs - lhs) > PAINT_W)
        return 6;
    if (layout.image.x < layout.workspace.x ||
        layout.image.y < layout.workspace.y ||
        layout.image.x + layout.image.w > layout.workspace.x + layout.workspace.w ||
        layout.image.y + layout.image.h > layout.workspace.y + layout.workspace.h)
        return 7;

    if (ui_eraser_bounds.w <= 0 || ui_brush_bounds.w <= 0 ||
        ui_color_bounds[1].w <= 0 || ui_size_bounds.w <= 0) return 8;
    if (ui_tool != 1) return 9;
    ui_click_at(ui_eraser_bounds);
    if (ui_tool != 2) return 9;
    ui_click_at(ui_brush_bounds);
    if (ui_tool != 1) return 9;
    if (ui_color != 1) return 9;
    ui_click_at(ui_color_bounds[1]);
    if (ui_color != 2) return 9;
    ui_click_at(ui_color_bounds[0]);
    if (ui_color != 1) return 9;
    if (ui_calligraphy_bounds.w <= 0) return 90;
    ui_click_at(ui_calligraphy_bounds);
    if (ui_tool != 3) return 91;
    if (ui_line_bounds.w <= 0 || ui_rect_bounds.w <= 0 || ui_ellipse_bounds.w <= 0 ||
        ui_bucket_bounds.w <= 0 || ui_bucket_tol_bounds.w <= 0)
        return 97;
    ui_click_at(ui_line_bounds);
    if (ui_tool != 4) return 98;
    ui_click_at(ui_rect_bounds);
    if (ui_tool != 5) return 99;
    ui_click_at(ui_ellipse_bounds);
    if (ui_tool != 6) return 100;
    ui_click_at(ui_bucket_bounds);
    if (ui_tool != 7) return 101;
    if (app_set_tool(1) != 0) return 92;
    if (app_set_calligraphy_diameter(40) != 0 || ui_call_diameter != 40) return 93;
    if (app_set_pressure(99) != 0 || ui_pressure != 99) return 94;
    if (app_set_ink_opacity(128) != 0 || ui_opacity != 128) return 95;
    if (app_set_calligraphy_diameter(24) != 0 || app_set_pressure(180) != 0 ||
        app_set_ink_opacity(255) != 0) return 96;
    Uint8 r = 0, g = 0, b = 0;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 ||
        r != 255 || g != 255 || b != 255) return 10;
    /* A click on each fixed UI region must not reach the canvas. */
    int ux = layout.toolbar.x + layout.toolbar.w / 2;
    int uy = layout.toolbar.y + 40;
    if (dispatch_canvas_pointer(SDL_MOUSEBUTTONDOWN, ux, uy, SDL_BUTTON_LEFT, &saved) != 0)
        return 9;
    ux = layout.right.x + 20; uy = layout.right.y + 40;
    if (dispatch_canvas_pointer(SDL_MOUSEBUTTONDOWN, ux, uy, SDL_BUTTON_LEFT, &saved) != 0)
        return 9;
    ux = layout.status.x + layout.status.w / 2; uy = layout.status.y + 10;
    if (dispatch_canvas_pointer(SDL_MOUSEBUTTONDOWN, ux, uy, SDL_BUTTON_LEFT, &saved) != 0)
        return 9;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 ||
        r != 255 || g != 255 || b != 255) return 10;

    /* The same setters used by the inspector controls must reach Glon. */
    if (app_set_color(2) != 0 || ui_color != 2) return 11;
    if (app_set_tool(2) != 0 || ui_tool != 2) return 12;
    if (app_set_size(17) != 0 || ui_size != 17) return 13;
    if (app_set_tool(1) != 0 || app_set_color(1) != 0 || app_set_size(8) != 0) return 14;
    int wx = 0, wy = 0;
    if (!canvas_to_window(400, 300, &wx, &wy) ||
        dispatch_canvas_pointer(SDL_MOUSEBUTTONDOWN, wx, wy, SDL_BUTTON_LEFT, &saved) != 0 ||
        dispatch_canvas_pointer(SDL_MOUSEBUTTONUP, wx, wy, SDL_BUTTON_LEFT, &saved) != 0)
        return 15;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 ||
        r != 220 || g != 60 || b != 60) return 16;
    if (event_key(SDLK_s, 0, &saved) != 0 || !saved) return 17;
    ui_build();
    present_ui();
    printf("ui-test: toolbar_no_stroke=1 inspector_no_stroke=1 status_no_stroke=1 "
           "controls=ok canvas=%dx%d image=%dx%d result=ok\n",
           PAINT_W, PAINT_H, layout.image.w, layout.image.h);
    return 0;
}

static void run_app_ui(void) {
    update_layout();
    nk_input_begin(nk);
    nk_input_end(nk);
    ui_build();
    present_ui();

    int saved = 0;
    int dragging = 0, last_wx = 0, last_wy = 0;
    while (!quit_flag) {
        if (replay_pump() > 0) {
            update_layout();
            nk_input_begin(nk);
            nk_input_end(nk);
            ui_build();
            present_ui();
            perf_renders++;
        }
        SDL_Event e;
        int wait_ms = (replay_state == REPLAY_PLAYING) ? 4 : 12;
        if (!SDL_WaitEventTimeout(&e, wait_ms)) continue;
        update_layout();
        nk_input_begin(nk);

        int pending_down = 0, pending_up = 0, pending_motion = 0;
        int pending_cancel_shape = 0;
        int down_ui = 0;
        int dx = 0, dy = 0, mx = 0, my = 0, up_x = 0, up_y = 0;
        SDL_KeyboardEvent keys[16];
        int nkeys = 0;
        do {
            if (e.type == SDL_QUIT) quit_flag = 1;
            else if (e.type == SDL_WINDOWEVENT &&
                     (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                      e.window.event == SDL_WINDOWEVENT_RESIZED ||
                      e.window.event == SDL_WINDOWEVENT_EXPOSED)) dirty = 1;
            else if (e.type == SDL_WINDOWEVENT &&
                     e.window.event == SDL_WINDOWEVENT_FOCUS_LOST &&
                     shape_active) pending_cancel_shape = 1;
            else if (e.type == SDL_KEYDOWN) {
                if (nkeys < (int)(sizeof keys / sizeof keys[0])) keys[nkeys++] = e.key;
            } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                if (e.button.button == SDL_BUTTON_LEFT) {
                    pending_down = 1; dx = e.button.x; dy = e.button.y;
                    down_ui = open_dialog || confirm_open || script_dialog || show_about ||
                              point_in_ui(dx, dy);
                } else if (e.button.button == SDL_BUTTON_RIGHT && shape_active) {
                    pending_cancel_shape = 1;
                }
            } else if (e.type == SDL_MOUSEBUTTONUP) {
                if (e.button.button == SDL_BUTTON_LEFT) {
                    pending_up = 1; up_x = e.button.x; up_y = e.button.y;
                }
            } else if (e.type == SDL_MOUSEMOTION) {
                pending_motion = 1; mx = e.motion.x; my = e.motion.y;
                perf_raw_motions++;
            }
            nk_sdl_handle_event(&e);
        } while (SDL_PollEvent(&e));
        nk_sdl_handle_grab();
        nk_input_end(nk);

        replay_pump();
        update_layout();
        ui_build();
        if (replay_busy()) {
            pending_down = pending_up = pending_motion = 0;
            if (shape_active) shape_cancel_gesture();
            dragging = 0;
        }
        if (pending_cancel_shape) {
            if (shape_active) shape_cancel_gesture();
            dragging = 0;
            pending_down = pending_up = pending_motion = 0;
        }

        if (pending_down) {
            int down_inside = !down_ui && !point_in_ui(dx, dy) &&
                              point_in_rect(&layout.image, dx, dy);
            if (down_inside) {
                if (ui_tool == 7) {
                    int cx = 0, cy = 0, ww = 0, wh = 0;
                    SDL_GetWindowSize(win, &ww, &wh);
                    map_window_to_canvas_clamped(ww, wh, dx, dy, &cx, &cy);
                    bucket_fill(cx, cy, ui_bucket_tolerance);
                    dragging = 0; last_wx = dx; last_wy = dy;
                } else if (ui_tool >= 4) {
                    int cx = 0, cy = 0, ww = 0, wh = 0;
                    SDL_GetWindowSize(win, &ww, &wh);
                    map_window_to_canvas_clamped(ww, wh, dx, dy, &cx, &cy);
                    shape_begin_gesture(ui_tool, cx, cy,
                                        (SDL_GetModState() & KMOD_SHIFT) != 0);
                    dragging = 1; last_wx = dx; last_wy = dy;
                } else {
                    history_begin_stroke();   /* snapshot before the first pixel */
                    if (dispatch_canvas_pointer(SDL_MOUSEBUTTONDOWN, dx, dy,
                                                SDL_BUTTON_LEFT, &saved) == 0) {
                        dragging = 1; last_wx = dx; last_wy = dy;
                    } else {
                        history_cancel_stroke();
                    }
                }
            } else {
                if (!shape_active) history_cancel_stroke();
            }
        }
        if (pending_motion && dragging) {
            if (shape_active) {
                int cx = 0, cy = 0, ww = 0, wh = 0;
                SDL_GetWindowSize(win, &ww, &wh);
                map_window_to_canvas_clamped(ww, wh, mx, my, &cx, &cy);
                shape_update_gesture(cx, cy,
                                     (SDL_GetModState() & KMOD_SHIFT) != 0);
                last_wx = mx; last_wy = my;
                perf_app_moves++;
            } else {
                int motion_inside = !point_in_ui(mx, my) && point_in_rect(&layout.image, mx, my);
                if (motion_inside && dispatch_canvas_pointer(SDL_MOUSEMOTION, mx, my, 0, &saved) == 0) {
                    last_wx = mx; last_wy = my;
                    perf_app_moves++;
                } else {
                    event_mouse(SDL_MOUSEBUTTONUP, last_wx, last_wy, SDL_BUTTON_LEFT, &saved);
                    history_commit_stroke();
                    dragging = 0;
                }
            }
        }
        if (pending_up && dragging) {
            if (shape_active) {
                int cx = 0, cy = 0, ww = 0, wh = 0;
                SDL_GetWindowSize(win, &ww, &wh);
                map_window_to_canvas_clamped(ww, wh, up_x, up_y, &cx, &cy);
                shape_update_gesture(cx, cy,
                                     (SDL_GetModState() & KMOD_SHIFT) != 0);
                shape_finish_gesture();
                dragging = 0;
            } else {
                event_mouse(SDL_MOUSEBUTTONUP, last_wx, last_wy, SDL_BUTTON_LEFT, &saved);
                history_commit_stroke();
                dragging = 0;
            }
        }
        /* Key actions happen after pending painting, so a save key in the
         * same batch exports the stroke, not the previous frame. */
        for (int k = 0; k < nkeys; k++) {
            if (open_dialog || confirm_open || script_dialog) continue;  /* Nuklear owns text keys */
            if (shape_active && keys[k].keysym.sym == SDLK_ESCAPE) {
                shape_cancel_gesture();
                dragging = 0;
                continue;
            }
            if (shape_active &&
                map_key(keys[k].keysym.sym, keys[k].keysym.mod) == 'c') {
                shape_cancel_gesture();
                dragging = 0;
            }
            if ((keys[k].keysym.mod & KMOD_CTRL) &&
                (keys[k].keysym.sym == SDLK_z || keys[k].keysym.sym == SDLK_y)) {
                if (shape_active) {
                    shape_cancel_gesture();
                    dragging = 0;
                } else if (dragging) {
                    event_mouse(SDL_MOUSEBUTTONUP, last_wx, last_wy, SDL_BUTTON_LEFT, &saved);
                    history_commit_stroke();
                    dragging = 0;
                }
                if (replay_nav_available())
                    replay_nav_key(keys[k].keysym.sym, keys[k].keysym.mod);
                else
                    history_shortcut(keys[k].keysym.sym, keys[k].keysym.mod);
            } else {
                event_key(keys[k].keysym.sym, keys[k].keysym.mod, &saved);
            }
        }
        present_ui();
        perf_renders++;
    }
}

static int test_stroke(int cx0, int cy0, int cx1, int cy1, int steps) {
    int wx = 0, wy = 0;
    if (!canvas_to_window(cx0, cy0, &wx, &wy)) return -1;
    history_begin_stroke();
    if (event_mouse(SDL_MOUSEBUTTONDOWN, wx, wy, SDL_BUTTON_LEFT, &(int){0}) != 0) {
        history_cancel_stroke();
        return -1;
    }
    for (int i = 1; i <= steps; i++) {
        int cx = cx0 + (cx1 - cx0) * i / steps;
        int cy = cy0 + (cy1 - cy0) * i / steps;
        if (!canvas_to_window(cx, cy, &wx, &wy) ||
            event_mouse(SDL_MOUSEMOTION, wx, wy, 0, &(int){0}) != 0) {
            history_cancel_stroke();
            return -1;
        }
    }
    if (!canvas_to_window(cx1, cy1, &wx, &wy) ||
        event_mouse(SDL_MOUSEBUTTONUP, wx, wy, SDL_BUTTON_LEFT, &(int){0}) != 0) {
        history_cancel_stroke();
        return -1;
    }
    history_commit_stroke();
    return 0;
}
static int pixel_is(Uint8 r, Uint8 g, Uint8 b, Uint8 cr, Uint8 cg, Uint8 cb) {
    return r == cr && g == cg && b == cb;
}
static int image_test(void) {
    int saved = 0;
    if (app_init() != 0) return 2;
    clear_canvas();
    history_free();
    unsaved = 0;
    Uint8 r = 0, g = 0, b = 0;

    /* Paths with spaces go through the same loader. */
    if (load_image_file("tests/fixtures/wide space.png") != 0) return 3;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,220,20,20)) return 3;

    /* Wide PNG: aspect-fit to 800x200, centred vertically over white. */
    if (load_image_file("tests/fixtures/wide.png") != 0) return 3;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,220,20,20)) return 4;
    if (read_canvas_pixel(400, 100, &r, &g, &b) != 0 || !pixel_is(r,g,b,255,255,255)) return 5;
    if (read_canvas_pixel(100, 100, &r, &g, &b) != 0 || !pixel_is(r,g,b,255,255,255)) return 5;
    if (unsaved) return 6;
#ifdef HAVE_NFD
    nfd_state = -1;  /* automated test uses the path-dialog fallback */
#endif
    request_open();
    if (!open_dialog || confirm_open) return 7;
    open_dialog = 0;

    /* Brush over the image, then eraser reveals the immutable background. */
    if (app_set_color(2) != 0 || app_set_tool(1) != 0 || app_set_size(8) != 0) return 7;
    if (test_stroke(300, 300, 500, 300, 10) != 0) return 8;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,40,120,220)) return 9;
    if (app_set_tool(2) != 0) return 10;
    if (test_stroke(300, 300, 500, 300, 10) != 0) return 11;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,220,20,20)) return 12;
    if (history_undo() != 1 ||
        read_canvas_pixel(400, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,40,120,220)) return 13;
    if (history_redo() != 1 ||
        read_canvas_pixel(400, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,220,20,20)) return 14;

    /* Clear removes only the drawing layer; undo restores the stroke. */
    if (app_set_tool(1) != 0 || test_stroke(300, 300, 500, 300, 10) != 0) return 15;
    if (event_key(SDLK_c, 0, &saved) != 0) return 16;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,220,20,20)) return 17;
    if (history_undo() != 1 ||
        read_canvas_pixel(400, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,40,120,220)) return 18;
    if (history_redo() != 1 ||
        read_canvas_pixel(400, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,220,20,20)) return 19;

    /* Tall JPEG: aspect-fit to 150x600, centred horizontally over white. */
    if (load_image_file("tests/fixtures/tall.jpg") != 0) return 20;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 ||
        !(g > 150 && r < 80 && b < 100)) return 21;
    if (read_canvas_pixel(50, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,255,255,255)) return 22;
    if (read_canvas_pixel(775, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,255,255,255)) return 22;
    if (undo_stack.count != 0 || redo_stack.count != 0) return 23;

    /* Transparent PNG pixels become white, opaque pixels remain visible. */
    if (load_image_file("tests/fixtures/alpha.png") != 0) return 24;
    if (read_canvas_pixel(200, 100, &r, &g, &b) != 0 || !pixel_is(r,g,b,255,255,255)) return 25;
    if (read_canvas_pixel(500, 500, &r, &g, &b) != 0 || !pixel_is(r,g,b,20,60,220)) return 26;

    /* Invalid input must not reset the current document or history. */
    if (load_image_file("tests/fixtures/tall.jpg") != 0) return 27;
    if (app_set_color(2) != 0 || app_set_tool(1) != 0 ||
        test_stroke(100, 100, 200, 200, 3) != 0) return 28;
    int undo_before = undo_stack.count;
    if (load_image_file("tests/fixtures/invalid.png") == 0) return 29;
    if (open_error[0] == 0) return 30;
    if (undo_stack.count != undo_before) return 31;
    if (read_canvas_pixel(150, 150, &r, &g, &b) != 0 || !pixel_is(r,g,b,40,120,220)) return 32;
    if (!unsaved) return 33;

    /* Loading a new image resets drawing and history after confirmation. */
    if (!unsaved) return 34;
    request_open();
    if (!confirm_open) return 35;
    confirm_open = 0;
    if (load_image_file("tests/fixtures/wide.png") != 0) return 36;
    if (undo_stack.count != 0 || redo_stack.count != 0) return 37;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,220,20,20)) return 38;

    /* Export a composite containing both the imported image and a new stroke. */
    if (app_set_color(2) != 0 || app_set_tool(1) != 0 ||
        test_stroke(300, 300, 500, 300, 10) != 0) return 39;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,40,120,220)) return 39;
    if (event_key(SDLK_s, 0, &saved) != 0 || !saved) return 39;
    printf("image-test: png=ok jpeg=ok alpha=ok scale_center=ok immutable=ok "
           "eraser=ok clear=ok undo=ok invalid=ok history_reset=ok result=ok\n");
    history_free();
    return 0;
}

static int drawing_alpha_at(int x, int y) {
    Uint32 *px = snapshot_canvas();
    if (!px) return -1;
    int a = (int)((px[(size_t)y * PAINT_W + x] >> 24) & 255);
    free(px);
    return a;
}
static int drawing_any_ink(void) {
    Uint32 *px = snapshot_canvas();
    if (!px) return -1;
    int any = 0;
    for (int i = 0; i < PAINT_W * PAINT_H; i++)
        if ((px[i] >> 24) & 255) { any = 1; break; }
    free(px);
    return any;
}
static void calligraphy_cmd(int x1, int y1, int p1, int x2, int y2, int p2,
                            int dia, int op) {
    char buf[256];
    int saved = 0;
    snprintf(buf, sizeof buf, "P %d %d %d %d %d %d %d %d 220 60 60",
             x1, y1, p1, x2, y2, p2, dia, op);
    command(buf, &saved);
}
static int calligraphy_test(void) {
    int saved = 0;
    if (app_init() != 0) return 2;
    if (pressure_diameter(24, 0) != 0 || pressure_diameter(24, 128) != 12 ||
        pressure_diameter(24, 255) != 24) return 3;

    clear_canvas(); history_free();
    calligraphy_cmd(100, 100, 0, 100, 100, 0, 24, 255);
    if (drawing_any_ink() != 0) return 4;

    clear_canvas();
    calligraphy_cmd(100, 100, 128, 100, 100, 128, 24, 255);
    if (drawing_alpha_at(100, 100) != 255 || drawing_alpha_at(107, 100) != 0) return 5;

    clear_canvas();
    calligraphy_cmd(100, 100, 255, 100, 100, 255, 24, 255);
    if (drawing_alpha_at(100, 111) != 255 || drawing_alpha_at(100, 113) != 0) return 6;

    /* Pressure interpolates along a connected segment. */
    clear_canvas();
    if (pressure_diameter(24, 0) != 0) return 7;
    calligraphy_cmd(100, 100, 0, 200, 100, 255, 24, 255);
    if (drawing_alpha_at(100, 100) != 0 || drawing_alpha_at(150, 100) != 255 ||
        drawing_alpha_at(200, 100) != 255) return 8;

    /* A click and a very short segment produce marks. */
    clear_canvas(); calligraphy_cmd(120, 120, 200, 120, 120, 200, 24, 255);
    if (drawing_alpha_at(120, 120) != 255) return 9;
    clear_canvas(); calligraphy_cmd(120, 120, 255, 121, 120, 255, 24, 255);
    if (drawing_alpha_at(120, 120) != 255 || drawing_alpha_at(121, 120) != 255) return 10;

    /* Connected segments do not leave a gap or darken the join. */
    clear_canvas();
    calligraphy_cmd(100, 140, 255, 200, 140, 255, 24, 128);
    calligraphy_cmd(200, 140, 255, 300, 140, 255, 24, 128);
    if (drawing_alpha_at(200, 140) != 128) return 11;

    /* Clipping at both canvas edges. */
    clear_canvas(); calligraphy_cmd(-100, 300, 255, -40, 300, 255, 24, 255);
    if (drawing_any_ink() != 0) return 12;
    calligraphy_cmd(790, 300, 255, 850, 300, 255, 24, 255);
    if (drawing_alpha_at(799, 300) != 255) return 13;

    /* Opacity is constant per coverage, and alpha stays bounded at a join. */
    clear_canvas(); calligraphy_cmd(150, 200, 255, 150, 200, 255, 24, 128);
    if (drawing_alpha_at(150, 200) != 128) return 14;
    Uint8 ar = 0, ag = 0, ab = 0;
    if (read_canvas_pixel(150, 200, &ar, &ag, &ab) != 0 ||
        (ar == 255 && ag == 255 && ab == 255)) return 14;

    /* Repeated command sequences produce byte-identical drawing layers. */
    clear_canvas();
    calligraphy_cmd(80, 250, 40, 220, 250, 220, 40, 200);
    calligraphy_cmd(220, 250, 220, 360, 280, 60, 40, 200);
    Uint32 *first = snapshot_canvas();
    if (!first) return 15;
    clear_canvas();
    calligraphy_cmd(80, 250, 40, 220, 250, 220, 40, 200);
    calligraphy_cmd(220, 250, 220, 360, 280, 60, 40, 200);
    Uint32 *second = snapshot_canvas();
    if (!second) { free(first); return 15; }
    int same = memcmp(first, second, (size_t)PAINT_W * PAINT_H * sizeof(Uint32)) == 0;
    free(first); free(second);
    if (!same) return 16;

    /* One complete calligraphy mouse stroke is one undo operation. */
    clear_canvas(); history_free();
    if (app_set_tool(3) != 0 || app_set_pressure(255) != 0 ||
        app_set_calligraphy_diameter(24) != 0 || app_set_ink_opacity(255) != 0) return 17;
    Uint32 *blank = snapshot_canvas();
    if (!blank) return 18;
    if (test_stroke(100, 300, 300, 300, 8) != 0 || undo_stack.count != 1) return 19;
    Uint32 *stroke = snapshot_canvas();
    if (!stroke) { free(blank); return 20; }
    if (memcmp(blank, stroke, (size_t)PAINT_W * PAINT_H * sizeof(Uint32)) == 0) return 21;
    if (history_undo() != 1) return 22;
    Uint32 *undone = snapshot_canvas();
    if (!undone || memcmp(blank, undone, (size_t)PAINT_W * PAINT_H * sizeof(Uint32)) != 0) return 23;
    free(undone);
    if (history_redo() != 1) return 24;
    Uint32 *redone = snapshot_canvas();
    if (!redone || memcmp(stroke, redone, (size_t)PAINT_W * PAINT_H * sizeof(Uint32)) != 0) return 25;
    free(blank); free(stroke); free(redone);

    /* Pressure changes during a mouse stroke affect subsequent segments. */
    clear_canvas(); history_free();
    if (app_set_tool(3) != 0 || app_set_pressure(0) != 0 ||
        app_set_calligraphy_diameter(24) != 0 || app_set_ink_opacity(255) != 0) return 26;
    int mx = 0, my = 0;
    if (!canvas_to_window(100, 400, &mx, &my)) return 26;
    history_begin_stroke();
    if (event_mouse(SDL_MOUSEBUTTONDOWN, mx, my, SDL_BUTTON_LEFT, &saved) != 0) return 26;
    if (app_set_pressure(255) != 0 || !canvas_to_window(300, 400, &mx, &my) ||
        event_mouse(SDL_MOUSEMOTION, mx, my, 0, &saved) != 0) return 26;
    if (event_mouse(SDL_MOUSEBUTTONUP, mx, my, SDL_BUTTON_LEFT, &saved) != 0) return 26;
    history_commit_stroke();
    if (drawing_alpha_at(100, 400) != 0 || drawing_alpha_at(300, 400) != 255 ||
        undo_stack.count != 1) return 26;

    /* Script execution, repeated determinism, loader bound and PNG export. */
    clear_canvas(); history_free();
    if (load_script_file("examples/calligraphy.glon") != 0) return 26;
    long loader0 = loader_used();
    if (run_script() != 0) return 27;
    if (drawing_alpha_at(80, 100) == 0 || drawing_alpha_at(300, 100) == 0 ||
        drawing_alpha_at(650, 300) == 0) return 28;
    if (drawing_alpha_at(430, 560) != 0) return 29;
    Uint32 *script1 = snapshot_canvas();
    if (!script1) return 30;
    if (run_script() != 0) return 31;
    Uint32 *script2 = snapshot_canvas();
    if (!script2) { free(script1); return 32; }
    int script_same = memcmp(script1, script2, (size_t)PAINT_W * PAINT_H * sizeof(Uint32)) == 0;
    free(script1); free(script2);
    if (!script_same) return 33;
    if (loader_used() != loader0) return 34;
    if (event_key(SDLK_s, 0, &saved) != 0 || !saved) return 35;

    /* Calligraphy over an imported background. */
    if (load_image_file("tests/fixtures/tall.jpg") != 0) return 36;
    if (load_script_file("examples/calligraphy.glon") != 0) return 37;
    if (run_script() != 0) return 38;
    Uint8 r = 0, g = 0, b = 0;
    if (read_canvas_pixel(775, 300, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,255,255,255)) return 39;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 || (r == 255 && g == 255 && b == 255)) return 40;

    printf("calligraphy-test: pressure=ok interpolation=ok zero=ok short=ok "
           "joins=ok clipping=ok opacity=ok repeat=ok undo=ok script=ok "
           "background=ok loader_used=%ld result=ok\n", loader_used());
    history_free();
    return 0;
}

static int replay_snapshots_equal(const Uint32 *a, const Uint32 *b) {
    return memcmp(a, b, (size_t)PAINT_W * PAINT_H * sizeof(Uint32)) == 0;
}
static int replay_finish_steps(void) {
    while (replay_pos < replay_count) {
        if (replay_step() != 0) return -1;
    }
    return replay_state == REPLAY_COMPLETE ? 0 : -1;
}
static Uint32 *replay_prefix_snapshot(const Uint32 *initial, int prefix) {
    if (restore_canvas(initial) != 0 || prefix < 0 || prefix > replay_count) return NULL;
    int old = history_replaying;
    history_replaying = 1;
    for (int i = 0; i < prefix; i++) {
        int saved = 0;
        command(replay_buf + replay_offsets[i], &saved);
    }
    history_replaying = old;
    return snapshot_canvas();
}
static int replay_nav_prefix_test(const char *script, const Uint32 *initial) {
    if (load_script_file(script) != 0 || replay_prepare() != 0) return 1;
    if (replay_finish_steps() != 0) return 2;
    int total = replay_count;
    int history_before = undo_stack.count;
    int targets[] = { total - 1, 2, total / 2, 0, total };
    for (unsigned i = 0; i < sizeof targets / sizeof targets[0]; i++) {
        int target = targets[i];
        if (target < 0) target = 0;
        while (replay_pos > target) if (replay_step_back() != 1) return 3;
        while (replay_pos < target) if (replay_step_forward() != 1) return 4;
        if (undo_stack.count != history_before) return 5;
        Uint32 *expected = replay_prefix_snapshot(initial, target);
        Uint32 *actual = snapshot_canvas();
        if (!expected || !actual || !replay_snapshots_equal(expected, actual)) {
            free(expected); free(actual); return 6;
        }
        free(expected); free(actual);
    }
    if (history_undo() != 0) return 7;  /* replay mode blocks document Undo */
    replay_end();
    if (replay_nav_available()) return 8;
    history_free(); replay_forget();
    return 0;
}

static int replay_compare_paths(const char *script, const Uint32 *initial) {
    if (load_script_file(script) != 0) return 1;
    restore_canvas(initial);
    history_free(); replay_forget();
    if (run_script() != 0) return 2;
    Uint32 *immediate = snapshot_canvas();
    if (!immediate) return 3;
    restore_canvas(initial);
    history_free(); replay_forget();
    if (replay_prepare() != 0) { free(immediate); return 4; }
    if (replay_finish_steps() != 0) { free(immediate); return 5; }
    Uint32 *replayed = snapshot_canvas();
    if (!replayed) { free(immediate); return 6; }
    int same = replay_snapshots_equal(immediate, replayed);
    free(immediate); free(replayed);
    replay_forget();
    return same ? 0 : 7;
}
static int replay_end_semantics_test(void) {
    clear_canvas(); history_free(); replay_forget();
    Uint32 *initial = snapshot_canvas();
    if (!initial) return 1;

    /* End at zero: unchanged canvas, no redundant document entry. */
    if (load_script_file("examples/calligraphy.glon") != 0 ||
        replay_prepare() != 0 || replay_finish_steps() != 0) return 2;
    while (replay_pos > 0) if (replay_step_back() != 1) return 3;
    replay_end();
    if (undo_stack.count != 0 || redo_stack.count != 0) return 4;
    Uint32 *zero = snapshot_canvas();
    if (!zero || !replay_snapshots_equal(initial, zero)) return 5;
    free(zero);

    /* End at an intermediate prefix commits exactly that visible prefix. */
    restore_canvas(initial); history_free(); replay_forget();
    if (load_script_file("examples/calligraphy.glon") != 0 ||
        replay_prepare() != 0 || replay_finish_steps() != 0) return 6;
    int target = 5;
    if (target > replay_count) target = replay_count;
    while (replay_pos > target) if (replay_step_back() != 1) return 7;
    Uint32 *prefix = replay_prefix_snapshot(initial, target);
    if (!prefix) return 8;
    replay_end();
    if (undo_stack.count != 1) return 9;
    Uint32 *committed = snapshot_canvas();
    if (!committed || !replay_snapshots_equal(prefix, committed)) return 10;
    free(committed);
    if (history_undo() != 1) return 11;
    Uint32 *undone = snapshot_canvas();
    if (!undone || !replay_snapshots_equal(initial, undone)) return 12;
    free(undone);
    if (history_redo() != 1) return 13;
    Uint32 *redone = snapshot_canvas();
    if (!redone || !replay_snapshots_equal(prefix, redone)) return 14;
    free(redone); free(prefix);

    /* Stop after stepping back restores the original and adds no history. */
    restore_canvas(initial); history_free(); replay_forget();
    if (load_script_file("examples/calligraphy.glon") != 0 ||
        replay_prepare() != 0 || replay_finish_steps() != 0) return 15;
    while (replay_pos > 3) if (replay_step_back() != 1) return 16;
    replay_stop();
    if (undo_stack.count != 0) return 17;
    Uint32 *stopped = snapshot_canvas();
    if (!stopped || !replay_snapshots_equal(initial, stopped)) return 18;
    free(stopped);

    /* Stop preserves an existing document redo entry. */
    restore_canvas(initial); history_free(); replay_forget();
    if (load_script_file("examples/rectangle.glon") != 0 || run_script() != 0) return 19;
    Uint32 *rect = snapshot_canvas();
    if (!rect) return 20;
    if (history_undo() != 1 || redo_stack.count != 1) return 21;
    if (load_script_file("examples/calligraphy.glon") != 0 ||
        replay_prepare() != 0 || replay_step() != 0) return 22;
    replay_stop();
    if (redo_stack.count != 1) return 23;
    Uint32 *after_stop = snapshot_canvas();
    if (!after_stop || !replay_snapshots_equal(initial, after_stop)) return 24;
    free(after_stop);
    if (history_redo() != 1) return 25;
    Uint32 *rect_again = snapshot_canvas();
    if (!rect_again || !replay_snapshots_equal(rect, rect_again)) return 26;
    free(rect_again); free(rect);

    /* A genuine End commit invalidates redo history. */
    restore_canvas(initial); history_free(); replay_forget();
    if (load_script_file("examples/rectangle.glon") != 0 || run_script() != 0) return 27;
    if (history_undo() != 1 || redo_stack.count != 1) return 28;
    if (load_script_file("examples/calligraphy.glon") != 0 ||
        replay_prepare() != 0 || replay_finish_steps() != 0) return 29;
    replay_end();
    if (redo_stack.count != 0 || undo_stack.count != 1) return 30;

    /* Repeated prepare/play/End cycles keep loader use bounded. */
    history_free(); replay_forget();
    if (load_script_file("examples/calligraphy.glon") != 0) return 31;
    long loader0 = loader_used();
    for (int rep = 0; rep < 3; rep++) {
        if (replay_prepare() != 0 || replay_finish_steps() != 0) return 32;
        replay_end();
        history_free(); replay_forget();
    }
    if (loader_used() != loader0) return 33;

    free(initial);
    history_free(); replay_forget();
    return 0;
}


static long canvas_alpha_count(const Uint32 *px) {
    long n = 0;
    for (int i = 0; i < PAINT_W * PAINT_H; i++)
        if (((px[i] >> 24) & 255) != 0) n++;
    return n;
}
static int canvas_alpha_at(const Uint32 *px, int x, int y) {
    if (x < 0 || x >= PAINT_W || y < 0 || y >= PAINT_H) return 0;
    return (int)((px[(size_t)y * PAINT_W + x] >> 24) & 255);
}
/* Load calligraphy.glon, start playback and advance to the first P command.
 * Leaves replay active and positioned at that pressure command. */
static int smooth_prepare_first_path(const char *script, const Uint32 *initial,
                                     int *first) {
    restore_canvas(initial);
    history_free();
    replay_forget();
    if (load_script_file(script) != 0) return -1;
    restore_canvas(initial);
    if (replay_prepare() != 0) return -1;
    if (replay_play() != 0) return -1;
    PressureCommand pc;
    while (replay_pos < replay_count && !replay_next_pressure(&pc))
        replay_apply_one();
    if (replay_pos >= replay_count) return -1;
    *first = replay_pos;
    return 0;
}
static int smooth_prepare_first(const Uint32 *initial, int *first) {
    return smooth_prepare_first_path("examples/calligraphy.glon", initial, first);
}
static int smooth_animation_test(void) {
    int saved = 0;
    if (app_init() != 0) return 2;

    /* Explicit pressure and interpolation arithmetic. */
    if (lerp_int(20, 240, 0, 1000) != 20) return 3;
    if (lerp_int(20, 240, 250, 1000) != 75) return 4;
    if (lerp_int(20, 240, 500, 1000) != 130) return 5;
    if (lerp_int(20, 240, 750, 1000) != 185) return 6;
    if (lerp_int(20, 240, 1000, 1000) != 240) return 7;
    if (pressure_diameter(48, 20) != 4 || pressure_diameter(48, 240) != 45) return 8;

    /* A nonempty starting layer makes End/Stop and undo checks meaningful. */
    clear_canvas();
    history_free();
    unsaved = 0;
    if (app_set_tool(1) != 0 || app_set_color(2) != 0 || app_set_size(12) != 0) return 9;
    if (test_stroke(80, 80, 250, 160, 5) != 0) return 10;
    history_free();
    Uint32 *initial = snapshot_canvas();
    if (!initial) return 11;

    ui_smooth_strokes = 1;
    ui_stroke_duration_index = 2;
    replay_speed_index = 1;
    int first = 0;
    if (smooth_prepare_first(initial, &first) != 0) return 12;
    if (first < 1) return 13;
    Uint32 *prefix_before = replay_prefix_snapshot(initial, first);
    if (!prefix_before) return 14;
    if (replay_anim_begin() != 1 || !replay_animating || replay_pos != first) return 15;
    if (replay_anim_memory_bytes() != 2 * replay_pixels_bytes()) return 16;

    const int percents[] = { 0, 250, 500, 750, 999 };
    Uint32 *frame[5] = { 0 };
    long counts[5] = { 0 };
    frame[0] = snapshot_canvas();
    if (!frame[0]) return 17;
    counts[0] = canvas_alpha_count(frame[0]);
    int anim_x[5] = { replay_anim_x };
    int anim_d[5] = { replay_anim_diameter };
    for (int i = 1; i < 5; i++) {
        if (replay_anim_render_at(percents[i]) != 0) return 18;
        frame[i] = snapshot_canvas();
        if (!frame[i]) return 19;
        counts[i] = canvas_alpha_count(frame[i]);
        anim_x[i] = replay_anim_x;
        anim_d[i] = replay_anim_diameter;
    }
    for (int i = 1; i < 5; i++) {
        if (replay_snapshots_equal(frame[i - 1], frame[i])) return 20;
        if (counts[i] < counts[i - 1]) return 21;
        if (anim_x[i] < anim_x[i - 1]) return 80;
        if (anim_d[i] < anim_d[i - 1]) return 81;
    }
    if (counts[4] <= counts[0]) return 22;
    /* Thin-to-thick stroke grows from 80,100 towards 300,100. */
    if (canvas_alpha_at(frame[1], 100, 100) == 0) return 23;
    if (canvas_alpha_at(frame[1], 280, 100) != 0) return 24;
    if (canvas_alpha_at(frame[3], 280, 100) != 0) return 25;
    if (canvas_alpha_at(frame[4], 280, 100) == 0) return 26;
    /* The brush-tip overlay is presentation-only: drawing the window frame
     * must not change the drawing-layer pixels. */
    Uint32 *overlay_before = snapshot_canvas();
    render_frame();
    Uint32 *overlay_after = snapshot_canvas();
    if (!overlay_before || !overlay_after ||
        !replay_snapshots_equal(overlay_before, overlay_after)) return 82;
    free(overlay_before); free(overlay_after);

    /* Completing the command restores the pre-command layer and applies the
     * original full command once. */
    if (replay_anim_finish(1) != 0) return 27;
    Uint32 *after_full = snapshot_canvas();
    Uint32 *expected_full = replay_prefix_snapshot(initial, first + 1);
    if (!after_full || !expected_full ||
        !replay_snapshots_equal(after_full, expected_full)) return 28;
    free(after_full); free(expected_full);
    for (int i = 0; i < 5; i++) free(frame[i]);
    free(prefix_before);

    /* Pause freezes the partial presentation; resume keeps progress/canvas. */
    if (smooth_prepare_first(initial, &first) != 0) return 29;
    if (replay_anim_begin() != 1 || replay_anim_render_at(500) != 0) return 30;
    Uint32 *paused = snapshot_canvas();
    if (!paused) return 31;
    replay_pause();
    if (replay_state != REPLAY_PAUSED || !replay_animating ||
        replay_anim_progress != 500) return 32;
    if (replay_play() != 0 || replay_state != REPLAY_PLAYING ||
        !replay_animating || replay_anim_progress != 500) return 33;
    Uint32 *resumed = snapshot_canvas();
    if (!resumed || !replay_snapshots_equal(paused, resumed)) return 34;
    free(paused); free(resumed);
    if (replay_anim_finish(1) != 0) return 35;

    /* Step during a partial stroke completes exactly that command. */
    if (smooth_prepare_first(initial, &first) != 0) return 36;
    if (replay_anim_begin() != 1 || replay_anim_render_at(500) != 0) return 37;
    int pos_before = replay_pos;
    if (replay_step() != 0 || replay_state != REPLAY_PAUSED || replay_animating) return 38;
    if (replay_pos != pos_before + 1) return 39;
    Uint32 *stepped = snapshot_canvas();
    Uint32 *step_ref = replay_prefix_snapshot(initial, pos_before + 1);
    if (!stepped || !step_ref || !replay_snapshots_equal(stepped, step_ref)) return 40;
    free(stepped); free(step_ref);

    /* Back during a partial stroke discards it and moves to the previous
     * completed boundary. */
    if (smooth_prepare_first(initial, &first) != 0) return 41;
    if (replay_anim_begin() != 1 || replay_anim_render_at(500) != 0) return 42;
    pos_before = replay_pos;
    if (replay_step_back() != 1 || replay_state != REPLAY_PAUSED || replay_animating) return 43;
    int back_target = pos_before > 0 ? pos_before - 1 : 0;
    if (replay_pos != back_target) return 44;
    Uint32 *backed = snapshot_canvas();
    Uint32 *back_ref = replay_prefix_snapshot(initial, back_target);
    if (!backed || !back_ref || !replay_snapshots_equal(backed, back_ref)) return 45;
    free(backed); free(back_ref);

    /* Stop during a partial stroke restores the pre-playback layer. */
    if (smooth_prepare_first(initial, &first) != 0) return 46;
    if (replay_anim_begin() != 1 || replay_anim_render_at(500) != 0) return 47;
    replay_stop();
    if (replay_animating || replay_nav_available() || undo_stack.count != 0) return 48;
    Uint32 *stopped = snapshot_canvas();
    if (!stopped || !replay_snapshots_equal(initial, stopped)) return 49;
    free(stopped);
    if (replay_anim_memory_bytes() != 0) return 50;

    /* End during a partial stroke commits only the completed prefix. */
    if (smooth_prepare_first(initial, &first) != 0) return 51;
    prefix_before = replay_prefix_snapshot(initial, first);
    if (!prefix_before) return 52;
    if (replay_anim_begin() != 1 || replay_anim_render_at(500) != 0) return 53;
    replay_end();
    if (replay_animating || replay_nav_available() || undo_stack.count != 1) return 54;
    Uint32 *ended = snapshot_canvas();
    if (!ended || !replay_snapshots_equal(prefix_before, ended)) return 55;
    free(ended);
    if (history_undo() != 1) return 56;
    Uint32 *undone = snapshot_canvas();
    if (!undone || !replay_snapshots_equal(initial, undone)) return 57;
    free(undone);
    if (history_redo() != 1) return 58;
    Uint32 *redone = snapshot_canvas();
    if (!redone || !replay_snapshots_equal(prefix_before, redone)) return 59;
    free(redone); free(prefix_before);

    /* Resize during animation and verify the brush-tip mapping. */
    if (smooth_prepare_first(initial, &first) != 0) return 60;
    if (replay_anim_begin() != 1 || replay_anim_render_at(500) != 0) return 61;
    if (resize_to(1600, 1200) != 0) return 62;
    int wx = 0, wy = 0, cx = 0, cy = 0;
    if (!canvas_to_window(replay_anim_x, replay_anim_y, &wx, &wy) ||
        !point_in_rect(&layout.image, wx, wy) ||
        !map_window_to_canvas(1600, 1200, wx, wy, &cx, &cy) ||
        abs(cx - replay_anim_x) > 2 || abs(cy - replay_anim_y) > 2) return 63;
    if (replay_anim_render_at(750) != 0 || replay_anim_finish(1) != 0) return 64;
    if (resize_to(1200, 800) != 0) return 65;

    /* Performance sample: 200 partial frames of the first stroke. */
    if (smooth_prepare_first(initial, &first) != 0) return 66;
    if (replay_anim_begin() != 1) return 67;
    replay_anim_frames = 0;
    replay_anim_uploads = 0;
    replay_anim_total_ns = 0;
    replay_anim_worst_ns = 0;
    Uint64 perf0 = SDL_GetPerformanceCounter();
    for (int i = 0; i < 200; i++) {
        int p = (int)((int64_t)i * 999 / 199);
        if (replay_anim_render_at(p) != 0) return 68;
    }
    Uint64 perf1 = SDL_GetPerformanceCounter();
    double wall_ms = (double)(perf1 - perf0) * 1000.0 /
                     (double)SDL_GetPerformanceFrequency();
    double avg_ms = replay_anim_frames
        ? (double)replay_anim_total_ns / 1e6 / (double)replay_anim_frames : 0.0;
    double worst_ms = (double)replay_anim_worst_ns / 1e6;
    size_t anim_bytes = replay_anim_memory_bytes();
    long perf_frames = replay_anim_frames;
    long perf_uploads = replay_anim_uploads;
    if (perf_frames != 200 || perf_uploads != 200) return 69;
    if (replay_anim_finish(1) != 0) return 70;

    /* Final smooth replay must equal immediate execution for several duration
     * and playback-speed settings. */
    restore_canvas(initial);
    history_free(); replay_forget();
    if (load_script_file("examples/calligraphy.glon") != 0 || run_script() != 0) return 71;
    Uint32 *immediate = snapshot_canvas();
    if (!immediate) return 72;
    for (int variant = 0; variant < 3; variant++) {
        ui_stroke_duration_index = variant == 0 ? 0 : (variant == 1 ? 2 : 3);
        replay_speed_index = variant == 0 ? 0 : (variant == 1 ? 1 : 3);
        if (smooth_prepare_first(initial, &first) != 0) return 73;
        if (replay_finish_smooth(4) != 0) return 74;
        Uint32 *final = snapshot_canvas();
        if (!final || !replay_snapshots_equal(immediate, final)) return 75;
        free(final);
        replay_stop();
    }

    /* Repeated smooth playback does not grow the loader. */
    long loader0 = loader_used();
    for (int rep = 0; rep < 3; rep++) {
        if (smooth_prepare_first(initial, &first) != 0) return 76;
        if (replay_finish_smooth(3) != 0) return 77;
        replay_stop();
        replay_forget();
    }
    if (loader_used() != loader0) return 78;

    printf("smooth-test: interpolation=ok progression=ok direction=ok pause=ok "
           "resume=ok step=ok back=ok stop=ok end=ok undo=ok resize=ok "
           "speed=ok equality=ok loader=%ld sample_frames=%ld "
           "uploads_per_frame=1 extra_bytes=%zu avg_frame_ms=%.3f "
           "worst_frame_ms=%.3f sample_wall_ms=%.2f result=ok\n",
           loader_used(), perf_frames, anim_bytes, avg_ms, worst_ms, wall_ms);

    free(immediate);
    free(initial);
    history_free(); replay_forget();
    (void)saved;
    return 0;
}




static int yong_frames(void) {
    if (app_init() != 0) return 2;
    clear_canvas();
    history_free();
    replay_forget();
    unsaved = 0;
    Uint32 *initial = snapshot_canvas();
    if (!initial) return 3;
    if (load_script_file("examples/yong.glon") != 0) return 4;
    if (replay_prepare() != 0) return 5;
    /* Command boundaries: Clear + dot, + horizontal, + vertical, + left sweep,
     * + right sweep. */
    int stages[] = { 3, 6, 11, 14, 19 };
    const char *names[] = { "glon-yong-stage1-dot.png",
                            "glon-yong-stage2-horizontal.png",
                            "glon-yong-stage3-vertical-hook.png",
                            "glon-yong-stage4-left-sweep.png",
                            "glon-yong-stage5-complete.png" };
    if (replay_count < stages[4]) return 6;
    for (int i = 0; i < 5; i++) {
        Uint32 *prefix = replay_prefix_snapshot(initial, stages[i]);
        if (!prefix) return 7;
        free(prefix);
        if (save_png(names[i]) != 0) return 8;
    }
    printf("yong-frames: stages=%s,%s,%s,%s,%s commands=%d result=ok\n",
           names[0], names[1], names[2], names[3], names[4], replay_count);
    restore_canvas(initial);
    free(initial);
    history_free(); replay_forget();
    return 0;
}



static int geometry_do(int tool, int x1, int y1, int x2, int y2, int shift) {
    shape_begin_gesture(tool, x1, y1, shift);
    shape_update_gesture(x2, y2, shift);
    return shape_finish_gesture();
}
static int geometry_test(void) {
    int saved = 0;
    if (app_init() != 0) return 2;

    /* Toolbar/keyboard tool selection reaches Glon and the C mirror. */
    if (event_key(SDLK_l, 0, &saved) != 0 || ui_tool != 4) return 67;
    if (event_key(SDLK_r, 0, &saved) != 0 || ui_tool != 5) return 68;
    if (event_key(SDLK_o, 0, &saved) != 0 || ui_tool != 6) return 69;

    /* Straight line, one command, one undo entry. */
    clear_canvas(); history_free(); replay_forget(); unsaved = 0;
    if (app_set_tool(4) != 0 || app_set_color(2) != 0 || app_set_size(8) != 0)
        return 3;
    int line_cmds = 0, rect_outline_cmds = 0, rect_fill_cmds = 0;
    int ellipse_outline_cmds = 0, ellipse_fill_cmds = 0;
    size_t line_bytes = 0, rect_outline_bytes = 0, rect_fill_bytes = 0;
    size_t ellipse_outline_bytes = 0, ellipse_fill_bytes = 0;
    ui_fill_shapes = 0;
    Uint32 *line_pre = snapshot_canvas();
    if (!line_pre) return 4;
    int n = geometry_do(4, 100, 100, 200, 100, 0);
    if (n != 1 || shape_last_commands != 1 || shape_last_bytes == 0 ||
        shape_last_bytes >= SCRIPT_CMD_BYTES) return 5;
    line_cmds = n; line_bytes = shape_last_bytes;
    Uint8 r = 0, g = 0, b = 0;
    if (read_canvas_pixel(150, 100, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,40,120,220)) return 6;
    Uint32 *line_post = snapshot_canvas();
    if (!line_post) return 7;
    if (history_undo() != 1) return 8;
    Uint32 *line_undo = snapshot_canvas();
    if (!line_undo || !replay_snapshots_equal(line_pre, line_undo)) return 9;
    free(line_undo);
    if (history_redo() != 1) return 10;
    Uint32 *line_redo = snapshot_canvas();
    if (!line_redo || !replay_snapshots_equal(line_post, line_redo)) return 11;
    free(line_redo); free(line_post); free(line_pre);

    /* Rectangle outline and fill. */
    clear_canvas(); history_free(); replay_forget();
    if (app_set_tool(5) != 0 || app_set_color(3) != 0 || app_set_size(4) != 0)
        return 12;
    ui_fill_shapes = 0;
    n = geometry_do(5, 100, 100, 300, 250, 0);
    if (n != 4) return 13;
    rect_outline_cmds = n; rect_outline_bytes = shape_last_bytes;
    if (read_canvas_pixel(200, 100, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,40,160,80)) return 14;
    if (read_canvas_pixel(200, 175, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,255,255,255)) return 15;
    clear_canvas(); history_free();
    ui_fill_shapes = 1;
    n = geometry_do(5, 100, 100, 300, 250, 0);
    if (n != 151) return 16;
    rect_fill_cmds = n; rect_fill_bytes = shape_last_bytes;
    if (read_canvas_pixel(200, 175, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,40,160,80)) return 17;
    if (shape_last_bytes >= SCRIPT_CMD_BYTES) return 18;

    /* Reversed drag produces the same filled rectangle. */
    Uint32 *rect_normal = snapshot_canvas();
    if (!rect_normal) return 19;
    clear_canvas(); history_free();
    n = geometry_do(5, 300, 250, 100, 100, 0);
    if (n != 151) return 20;
    Uint32 *rect_reversed = snapshot_canvas();
    if (!rect_reversed || !replay_snapshots_equal(rect_normal, rect_reversed))
        return 21;
    free(rect_reversed); free(rect_normal);

    /* Shift makes a square. */
    clear_canvas(); history_free();
    ui_fill_shapes = 0;
    if (app_set_color(2) != 0 || app_set_size(4) != 0) return 22;
    n = geometry_do(5, 100, 100, 300, 180, 1);
    if (n != 4) return 23;
    if (read_canvas_pixel(300, 200, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,40,120,220)) return 24;

    /* Ellipse outline and fill. */
    clear_canvas(); history_free();
    if (app_set_tool(6) != 0 || app_set_color(4) != 0 || app_set_size(4) != 0)
        return 25;
    ui_fill_shapes = 0;
    n = geometry_do(6, 100, 100, 300, 300, 0);
    if (n < 48 || n > 96) return 26;
    ellipse_outline_cmds = n; ellipse_outline_bytes = shape_last_bytes;
    if (read_canvas_pixel(200, 100, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,230,170,40)) return 27;
    if (read_canvas_pixel(200, 200, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,255,255,255)) return 28;
    clear_canvas(); history_free();
    ui_fill_shapes = 1;
    n = geometry_do(6, 100, 100, 300, 300, 0);
    if (n < 100 || n > 220) return 29;
    ellipse_fill_cmds = n; ellipse_fill_bytes = shape_last_bytes;
    if (read_canvas_pixel(200, 200, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,230,170,40)) return 30;
    if (read_canvas_pixel(105, 105, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,255,255,255)) return 31;

    /* Shift makes a circle. */
    clear_canvas(); history_free();
    ui_fill_shapes = 0;
    n = geometry_do(6, 100, 100, 300, 200, 1);
    if (n < 48 || n > 96) return 32;
    if (read_canvas_pixel(200, 100, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,230,170,40)) return 33;
    if (read_canvas_pixel(300, 200, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,230,170,40)) return 34;

    /* Zero-size rectangle/ellipse do nothing and create no history. */
    clear_canvas(); history_free();
    Uint32 *zero_pre = snapshot_canvas();
    if (!zero_pre) return 35;
    int undo_before = undo_stack.count;
    n = geometry_do(5, 200, 200, 200, 200, 0);
    if (n != 0 || undo_stack.count != undo_before) return 36;
    n = geometry_do(6, 200, 200, 200, 260, 0);
    if (n != 0 || undo_stack.count != undo_before) return 37;
    Uint32 *zero_post = snapshot_canvas();
    if (!zero_post || !replay_snapshots_equal(zero_pre, zero_post)) return 38;
    free(zero_pre); free(zero_post);

    /* Zero-length line is a dot. */
    clear_canvas(); history_free();
    n = geometry_do(4, 200, 200, 200, 200, 0);
    if (n != 1) return 39;
    if (read_canvas_pixel(200, 200, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,230,170,40)) return 40;

    /* Clipping: a drag extending outside the canvas is clipped and bounded. */
    clear_canvas(); history_free();
    ui_fill_shapes = 1;
    n = geometry_do(5, 700, 500, 900, 700, 0);
    if (n != 100) return 41;
    if (read_canvas_pixel(750, 550, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,230,170,40)) return 42;
    if (read_canvas_pixel(799, 599, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,230,170,40)) return 43;
    if (shape_last_bytes >= SCRIPT_CMD_BYTES) return 44;

    /* Generated commands are standard L commands and replay to the same RGBA. */
    clear_canvas(); history_free(); replay_forget();
    if (app_set_color(1) != 0 || app_set_size(4) != 0) return 45;
    ui_fill_shapes = 0;
    Uint32 *initial = snapshot_canvas();
    if (!initial) return 46;
    n = geometry_do(6, 250, 150, 550, 450, 0);
    if (n <= 0) return 47;
    Uint32 *immediate = snapshot_canvas();
    if (!immediate) return 48;
    if (replay_load_command_text(shape_last_text) != 0) return 49;
    restore_canvas(initial);
    if (replay_play() != 0 || replay_finish_steps() != 0) return 50;
    Uint32 *replayed = snapshot_canvas();
    if (!replayed || !replay_snapshots_equal(immediate, replayed)) return 51;
    replay_stop();
    free(replayed); free(immediate); free(initial);

    /* Cancelled drag leaves the canvas and undo history untouched. */
    clear_canvas(); history_free(); replay_forget();
    Uint32 *cancel_pre = snapshot_canvas();
    if (!cancel_pre) return 64;
    int cancel_undo = undo_stack.count;
    shape_begin_gesture(5, 100, 100, 0);
    shape_update_gesture(300, 300, 0);
    shape_cancel_gesture();
    if (shape_active || undo_stack.count != cancel_undo) return 65;
    Uint32 *cancel_post = snapshot_canvas();
    if (!cancel_post || !replay_snapshots_equal(cancel_pre, cancel_post)) return 66;
    free(cancel_pre); free(cancel_post);

    /* Lifecycle: shape gesture, then Clear and Load B. */
    clear_canvas(); history_free(); replay_forget();
    if (app_set_tool(5) != 0 || app_set_color(2) != 0 || app_set_size(4) != 0)
        return 54;
    ui_fill_shapes = 0;
    n = geometry_do(5, 100, 100, 200, 200, 0);
    if (n != 4) return 55;
    document_clear();
    if (read_canvas_pixel(150, 150, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,255,255,255)) return 56;
    /* Clear and Load both cancel an in-progress preview instead of committing. */
    clear_canvas(); history_free();
    shape_begin_gesture(5, 100, 100, 0);
    shape_update_gesture(300, 300, 0);
    document_clear();
    if (shape_active) return 61;
    if (read_canvas_pixel(200, 200, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,255,255,255)) return 62;
    shape_begin_gesture(6, 100, 100, 0);
    shape_update_gesture(300, 300, 0);
    if (load_script_file("examples/calligraphy.glon") != 0 || shape_active)
        return 63;
    if (run_script() != 0) return 57;
    document_clear();
    clear_canvas(); history_free(); replay_forget();
    if (app_set_tool(5) != 0 || app_set_color(1) != 0 || app_set_size(4) != 0)
        return 58;
    ui_fill_shapes = 0;
    if (geometry_do(5, 150, 150, 350, 350, 0) != 4) return 59;
    if (save_png("glon-paint.png") != 0) return 60;

    printf("geometry-test: line=ok rect_outline=ok rect_fill=ok reversed=ok "
           "square=ok ellipse_outline=ok ellipse_fill=ok circle=ok zero=ok "
           "line_dot=ok clipping=ok undo=ok redo=ok replay_equal=ok "
           "cancel=ok lifecycle=ok line=%d/%zuB rect_outline=%d/%zuB "
           "rect_fill=%d/%zuB ellipse_outline=%d/%zuB "
           "ellipse_fill=%d/%zuB result=ok\n",
           line_cmds, line_bytes, rect_outline_cmds, rect_outline_bytes,
           rect_fill_cmds, rect_fill_bytes, ellipse_outline_cmds,
           ellipse_outline_bytes, ellipse_fill_cmds, ellipse_fill_bytes);
    history_free(); replay_forget();
    (void)saved;
    return 0;
}


static int bucket_do(int x, int y, int tolerance) {
    return bucket_fill(x, y, tolerance);
}
static int bucket_test(void) {
    int saved = 0;
    if (app_init() != 0) return 2;
    if (event_key(SDLK_g, 0, &saved) != 0 || ui_tool != 7) return 69;

    /* Fill inside a closed rectangle outline. */
    clear_canvas(); history_free(); replay_forget(); unsaved = 0;
    if (app_set_tool(5) != 0 || app_set_color(2) != 0 || app_set_size(4) != 0)
        return 3;
    ui_fill_shapes = 0;
    if (geometry_do(5, 100, 100, 300, 250, 0) != 4) return 4;
    history_free();
    Uint32 *rect_pre = snapshot_canvas();
    if (!rect_pre) return 5;
    if (app_set_color(1) != 0) return 6;
    Uint64 t0 = SDL_GetPerformanceCounter();
    int n = bucket_do(200, 175, 0);
    Uint64 t1 = SDL_GetPerformanceCounter();
    if (n < 100 || n > 200 || bucket_last_bytes == 0 ||
        bucket_last_bytes >= SCRIPT_CMD_BYTES) return 7;
    int rect_cmds = n;
    size_t rect_bytes = bucket_last_bytes;
    double rect_ms = (double)(t1 - t0) * 1000.0 /
                     (double)SDL_GetPerformanceFrequency();
    Uint8 r = 0, g = 0, b = 0;
    if (read_canvas_pixel(200, 175, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,220,60,60)) return 8;
    if (read_canvas_pixel(200, 100, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,40,120,220)) return 9;
    if (read_canvas_pixel(50, 50, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,255,255,255)) return 10;
    Uint32 *rect_post = snapshot_canvas();
    if (!rect_post) return 11;
    if (history_undo() != 1) return 12;
    Uint32 *rect_undo = snapshot_canvas();
    if (!rect_undo || !replay_snapshots_equal(rect_pre, rect_undo)) return 13;
    free(rect_undo);
    if (history_redo() != 1) return 14;
    Uint32 *rect_redo = snapshot_canvas();
    if (!rect_redo || !replay_snapshots_equal(rect_post, rect_redo)) return 15;
    free(rect_redo); free(rect_post); free(rect_pre);

    /* Fill inside an ellipse outline. */
    clear_canvas(); history_free();
    if (app_set_color(2) != 0 || app_set_size(4) != 0) return 16;
    ui_fill_shapes = 0;
    if (geometry_do(6, 350, 100, 550, 300, 0) < 48) return 17;
    history_free();
    if (app_set_color(3) != 0) return 18;
    n = bucket_do(450, 200, 0);
    if (n <= 0) return 19;
    if (read_canvas_pixel(450, 200, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,40,160,80)) return 20;
    if (read_canvas_pixel(450, 100, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,40,120,220)) return 21;
    if (read_canvas_pixel(360, 110, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,255,255,255)) return 22;

    /* Fill inside an irregular closed freehand outline. */
    clear_canvas(); history_free();
    if (app_set_tool(1) != 0 || app_set_color(2) != 0 || app_set_size(8) != 0)
        return 23;
    if (test_stroke(150, 150, 250, 120, 5) != 0 ||
        test_stroke(250, 120, 300, 220, 5) != 0 ||
        test_stroke(300, 220, 230, 300, 5) != 0 ||
        test_stroke(230, 300, 140, 260, 5) != 0 ||
        test_stroke(140, 260, 150, 150, 5) != 0) return 24;
    history_free();
    if (app_set_color(1) != 0) return 25;
    n = bucket_do(220, 200, 0);
    if (n <= 0) return 26;
    if (read_canvas_pixel(220, 200, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,220,60,60)) return 27;
    if (read_canvas_pixel(150, 150, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,40,120,220)) return 28;
    if (read_canvas_pixel(110, 110, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,255,255,255)) return 29;

    /* Tolerance: two adjacent semi-transparent bands, 10 alpha apart. */
    for (int pass = 0; pass < 2; pass++) {
        clear_canvas(); history_free();
        int saved_local = 0;
        command("L 300 300 500 300 20 220 60 60 255", &saved_local);
        command("L 300 320 500 320 20 230 70 70 255", &saved_local);
        Uint32 *tol_pre = snapshot_canvas();
        if (!tol_pre) return 30;
        if (app_set_color(2) != 0) return 31;
        int tol = pass == 0 ? 0 : 15;
        n = bucket_do(400, 300, tol);
        if (n <= 0) { free(tol_pre); return 32; }
        Uint32 *tol_post = snapshot_canvas();
        if (!tol_post) { free(tol_pre); return 33; }
        Uint32 a = tol_post[(size_t)300 * PAINT_W + 400];
        Uint32 c = tol_post[(size_t)325 * PAINT_W + 400];
        if (pass == 0) {
            if ((a & 0xffffff) != 0x2878dc) {
                free(tol_pre); free(tol_post); return 34;
            }
            if ((c & 0xffffff) != 0xe64646) {
                free(tol_pre); free(tol_post); return 35;
            }
        } else {
            if ((a & 0xffffff) != 0x2878dc ||
                (c & 0xffffff) != 0x2878dc) {
                free(tol_pre); free(tol_post); return 36;
            }
        }
        free(tol_pre); free(tol_post);
    }

    /* Black fill uses the script-only palette entry. */
    clear_canvas(); history_free();
    if (app_set_color(5) != 0) return 37;
    n = bucket_do(400, 300, 0);
    if (n != 600) return 38;
    if (read_canvas_pixel(0, 0, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,0,0,0)) return 39;
    if (read_canvas_pixel(799, 599, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,0,0,0)) return 40;

    /* Already matching target is a no-op and adds no history. */
    clear_canvas(); history_free();
    if (app_set_color(1) != 0) return 41;
    if (bucket_do(400, 300, 0) != 600) return 42;
    history_free();
    int undo_before = undo_stack.count;
    Uint32 *noop_pre = snapshot_canvas();
    if (!noop_pre) return 43;
    n = bucket_do(400, 300, 0);
    if (n != 0 || undo_stack.count != undo_before) return 44;
    Uint32 *noop_post = snapshot_canvas();
    if (!noop_post || !replay_snapshots_equal(noop_pre, noop_post)) return 45;
    free(noop_pre); free(noop_post);

    /* Open boundary reaching the canvas edges. */
    clear_canvas(); history_free();
    if (app_set_color(2) != 0 || app_set_size(4) != 0) return 46;
    ui_fill_shapes = 0;
    if (geometry_do(4, 400, 0, 400, 599, 0) != 1) return 47;
    history_free();
    if (app_set_color(1) != 0) return 48;
    n = bucket_do(100, 300, 0);
    if (n < 500 || n > 600) return 49;
    if (read_canvas_pixel(0, 300, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,220,60,60)) return 50;
    if (read_canvas_pixel(397, 300, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,220,60,60)) return 51;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,40,120,220)) return 51;
    if (read_canvas_pixel(403, 300, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,255,255,255)) return 52;
    if (read_canvas_pixel(799, 300, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,255,255,255)) return 53;

    /* Large 800x600 fill: bounded command stream and execution. */
    clear_canvas(); history_free();
    if (app_set_color(3) != 0) return 54;
    Uint64 large0 = SDL_GetPerformanceCounter();
    n = bucket_do(400, 300, 0);
    Uint64 large1 = SDL_GetPerformanceCounter();
    if (n != 600 || bucket_last_bytes == 0 ||
        bucket_last_bytes >= SCRIPT_CMD_BYTES ||
        bucket_last_commands > REPLAY_MAX_COMMANDS) return 55;
    int large_cmds = n;
    size_t large_bytes = bucket_last_bytes;
    double large_ms = (double)(large1 - large0) * 1000.0 /
                      (double)SDL_GetPerformanceFrequency();
    if (read_canvas_pixel(0, 0, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,40,160,80)) return 56;
    if (read_canvas_pixel(799, 599, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,40,160,80)) return 57;

    /* Replay equality for the fill command stream. */
    clear_canvas(); history_free(); replay_forget();
    if (app_set_color(2) != 0) return 58;
    Uint32 *replay_initial = snapshot_canvas();
    if (!replay_initial) return 59;
    if (bucket_do(400, 300, 0) != 600) return 60;
    Uint32 *replay_immediate = snapshot_canvas();
    if (!replay_immediate) return 61;
    if (replay_load_command_text(bucket_last_text) != 0) return 62;
    restore_canvas(replay_initial);
    Uint64 rep0 = SDL_GetPerformanceCounter();
    if (replay_play() != 0 || replay_finish_steps() != 0) return 63;
    Uint64 rep1 = SDL_GetPerformanceCounter();
    double replay_ms = (double)(rep1 - rep0) * 1000.0 /
                       (double)SDL_GetPerformanceFrequency();
    Uint32 *replay_after = snapshot_canvas();
    if (!replay_after || !replay_snapshots_equal(replay_immediate, replay_after))
        return 64;
    replay_stop();
    free(replay_after); free(replay_immediate); free(replay_initial);

    /* Load -> Fill -> Clear -> Load remains usable. */
    clear_canvas(); history_free(); replay_forget();
    if (load_script_file("examples/yong.glon") != 0 || run_script() != 0)
        return 65;
    if (app_set_color(1) != 0 || bucket_do(400, 300, 0) <= 0) return 66;
    document_clear();
    if (load_script_file("examples/calligraphy.glon") != 0 || run_script() != 0)
        return 67;
    if (save_png("glon-paint.png") != 0) return 68;

    printf("bucket-test: rect=ok circle=ok irregular=ok colours=ok tolerance=ok "
           "noop=ok open_edge=ok large=ok undo=ok redo=ok replay_equal=ok "
           "lifecycle=ok rect200x150=%d/%zuB/%.3fms full800x600=%d/%zuB/%.3fms "
           "replay800x600=%.3fms result=ok\n",
           rect_cmds, rect_bytes, rect_ms, large_cmds, large_bytes, large_ms,
           replay_ms);
    history_free(); replay_forget();
    (void)saved;
    return 0;
}

static int lifecycle_test(void) {
    int saved = 0;
    if (app_init() != 0) return 2;
    clear_canvas();
    history_free();
    replay_forget();
    unsaved = 0;
    if (app_set_tool(1) != 0 || app_set_color(2) != 0 || app_set_size(12) != 0)
        return 3;
    if (test_stroke(80, 80, 250, 160, 5) != 0) return 4;
    history_free();
    Uint32 *initial = snapshot_canvas();
    if (!initial) return 5;

    /* Run A, then Clear through the keyboard path used by the UI. */
    restore_canvas(initial);
    history_free(); replay_forget();
    if (load_script_file("examples/yong.glon") != 0) return 6;
    if (run_script() != 0) return 7;
    if (replay_locked() || replay_state != REPLAY_READY ||
        replay_prepared || replay_pre) return 8;
    Uint32 *yong_run = snapshot_canvas();
    if (!yong_run) return 9;
    if (event_key(SDLK_c, 0, &saved) != 0) return 10;
    Uint8 r = 0, g = 0, b = 0;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,255,255,255)) return 11;
    if (history_undo() != 1) return 12;
    Uint32 *undo_clear = snapshot_canvas();
    if (!undo_clear || !replay_snapshots_equal(yong_run, undo_clear)) return 13;
    free(undo_clear); free(yong_run);

    /* Load B after Clear, then Clear, then load A again. */
    if (load_script_file("examples/calligraphy.glon") != 0) return 14;
    if (run_script() != 0) return 15;
    if (event_key(SDLK_c, 0, &saved) != 0) return 16;
    if (load_script_file("examples/yong.glon") != 0) return 17;
    if (run_script() != 0) return 18;
    /* Load B without an intervening Clear. */
    if (load_script_file("examples/rectangle.glon") != 0) return 19;
    if (run_script() != 0) return 20;

    /* Clear while replay is actively Playing. */
    restore_canvas(initial);
    history_free(); replay_forget();
    if (load_script_file("examples/yong.glon") != 0 ||
        replay_prepare() != 0 || replay_play() != 0) return 64;
    if (replay_state != REPLAY_PLAYING || !replay_locked()) return 65;
    if (event_key(SDLK_c, 0, &saved) != 0) return 66;
    if (replay_locked() || replay_state != REPLAY_READY || replay_animating)
        return 67;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,255,255,255)) return 68;

    /* Clear from a paused replay: cancel replay, restore the pre-playback
     * document, then execute the ordinary Clear command. */
    restore_canvas(initial);
    history_free(); replay_forget();
    if (load_script_file("examples/yong.glon") != 0 ||
        replay_prepare() != 0 || replay_play() != 0) return 21;
    replay_pause();
    if (!replay_locked() || !replay_nav_available()) return 22;
    document_clear();
    if (replay_locked() || replay_state != REPLAY_READY ||
        replay_animating || replay_pre) return 23;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,255,255,255)) return 24;
    if (history_undo() != 1) return 25;
    Uint32 *undo_paused_clear = snapshot_canvas();
    if (!undo_paused_clear || !replay_snapshots_equal(initial, undo_paused_clear))
        return 26;
    free(undo_paused_clear);

    /* Clear from Complete through the keyboard path. */
    restore_canvas(initial);
    history_free(); replay_forget();
    if (load_script_file("examples/yong.glon") != 0 ||
        replay_prepare() != 0 || replay_play() != 0) return 27;
    if (replay_finish_smooth(4) != 0 || replay_state != REPLAY_COMPLETE) return 28;
    if (event_key(SDLK_c, 0, &saved) != 0) return 29;
    if (replay_locked() || replay_state != REPLAY_READY || replay_animating)
        return 30;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,255,255,255)) return 31;

    /* Load B while paused: replay is cancelled, pre-playback canvas is
     * restored, and loading does not add a document history entry. */
    restore_canvas(initial);
    history_free(); replay_forget();
    if (load_script_file("examples/yong.glon") != 0 ||
        replay_prepare() != 0 || replay_play() != 0) return 32;
    replay_pause();
    if (load_script_file("examples/calligraphy.glon") != 0) return 33;
    if (replay_locked() || replay_state != REPLAY_READY ||
        replay_prepared || replay_pre || replay_animating) return 34;
    if (undo_stack.count != 0 || redo_stack.count != 0) return 63;
    if (strcmp(script_path, "examples/calligraphy.glon") != 0) return 35;
    Uint32 *after_load = snapshot_canvas();
    if (!after_load || !replay_snapshots_equal(initial, after_load)) return 36;
    free(after_load);
    if (run_script() != 0) return 37;

    /* Stop then Clear. */
    restore_canvas(initial);
    history_free(); replay_forget();
    if (load_script_file("examples/yong.glon") != 0 ||
        replay_prepare() != 0 || replay_play() != 0) return 38;
    replay_pause();
    replay_stop();
    if (event_key(SDLK_c, 0, &saved) != 0) return 39;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,255,255,255)) return 40;

    /* End then Clear: End commits the replay prefix; Clear adds one ordinary
     * document entry, and undo returns to the ended prefix. */
    restore_canvas(initial);
    history_free(); replay_forget();
    if (load_script_file("examples/yong.glon") != 0 ||
        replay_prepare() != 0 || replay_play() != 0) return 41;
    if (replay_finish_smooth(4) != 0) return 42;
    replay_end();
    if (replay_nav_available()) return 43;
    int undo_after_end = undo_stack.count;
    Uint32 *ended = snapshot_canvas();
    if (!ended) return 44;
    if (event_key(SDLK_c, 0, &saved) != 0) return 45;
    if (undo_stack.count != undo_after_end + 1) return 46;
    if (history_undo() != 1) return 47;
    Uint32 *undo_ended_clear = snapshot_canvas();
    if (!undo_ended_clear || !replay_snapshots_equal(ended, undo_ended_clear))
        return 48;
    free(undo_ended_clear); free(ended);
    if (history_undo() != 1) return 49;
    Uint32 *undo_end = snapshot_canvas();
    if (!undo_end || !replay_snapshots_equal(initial, undo_end)) return 50;
    free(undo_end);

    /* Clear during a partial smooth stroke. */
    restore_canvas(initial);
    history_free(); replay_forget();
    int first = 0;
    if (smooth_prepare_first_path("examples/yong.glon", initial, &first) != 0 ||
        first < 1) return 51;
    if (replay_anim_begin() != 1 || replay_anim_render_at(500) != 0) return 52;
    document_clear();
    if (replay_locked() || replay_state != REPLAY_READY ||
        replay_animating || replay_anim_memory_bytes() != 0) return 53;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 ||
        !pixel_is(r,g,b,255,255,255)) return 54;

    /* A failed load leaves the UI usable: Clear still cancels replay, and a
     * subsequent valid load/run succeeds. */
    restore_canvas(initial);
    history_free(); replay_forget();
    if (load_script_file("examples/yong.glon") != 0 ||
        replay_prepare() != 0 || replay_play() != 0) return 55;
    replay_pause();
    if (load_script_file("/tmp/no-such-glon-lifecycle.glon") == 0) return 56;
    document_clear();
    if (replay_locked() || replay_state != REPLAY_READY) return 57;
    if (load_script_file("examples/calligraphy.glon") != 0) return 58;
    if (run_script() != 0) return 59;

    /* Repeated load/run/clear/load cycles keep the loader bounded. */
    clear_canvas();
    history_free(); replay_forget();
    long loader0 = loader_used();
    for (int rep = 0; rep < 3; rep++) {
        if (load_script_file("examples/yong.glon") != 0 ||
            run_script() != 0) return 60;
        document_clear();
        if (load_script_file("examples/calligraphy.glon") != 0 ||
            run_script() != 0) return 61;
        document_clear();
    }
    if (loader_used() != loader0) return 62;

    printf("lifecycle-test: run_clear=ok load_a_load_b=ok "
           "load_a_clear_load_b=ok paused_clear=ok complete_clear=ok "
           "paused_load_b=ok stop_clear=ok end_clear=ok partial_clear=ok "
           "failed_load_recovery=ok loader=%ld result=ok\n", loader_used());
    free(initial);
    history_free(); replay_forget();
    return 0;
}

static int yong_test(void) {
    int saved = 0;
    if (app_init() != 0) return 2;

    /* A nonempty starting layer makes End/Stop and undo checks meaningful. */
    clear_canvas();
    history_free();
    unsaved = 0;
    if (app_set_tool(1) != 0 || app_set_color(2) != 0 || app_set_size(12) != 0)
        return 3;
    if (test_stroke(80, 80, 250, 160, 5) != 0) return 4;
    history_free();
    Uint32 *initial = snapshot_canvas();
    if (!initial) return 5;

    /* Immediate execution and black ink. */
    restore_canvas(initial);
    history_free(); replay_forget();
    if (load_script_file("examples/yong.glon") != 0 || run_script() != 0) return 6;
    Uint32 *immediate = snapshot_canvas();
    if (!immediate) return 7;
    Uint8 r = 0, g = 0, b = 0;
    if (read_canvas_pixel(383, 128, &r, &g, &b) != 0 || !pixel_is(r,g,b,0,0,0)) return 8;
    if (read_canvas_pixel(405, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,0,0,0)) return 9;
    if (read_canvas_pixel(300, 410, &r, &g, &b) != 0 || !pixel_is(r,g,b,0,0,0)) return 10;
    if (read_canvas_pixel(560, 425, &r, &g, &b) != 0 || !pixel_is(r,g,b,0,0,0)) return 11;
    if (read_canvas_pixel(50, 50, &r, &g, &b) != 0 || !pixel_is(r,g,b,255,255,255)) return 12;

    /* One undoable script operation; undo/redo remain exact. */
    if (undo_stack.count != 1) return 13;
    if (history_undo() != 1) return 14;
    Uint32 *undone = snapshot_canvas();
    if (!undone || !replay_snapshots_equal(initial, undone)) return 15;
    free(undone);
    if (history_redo() != 1) return 16;
    Uint32 *redone = snapshot_canvas();
    if (!redone || !replay_snapshots_equal(immediate, redone)) return 17;
    free(redone);

    /* Command vocabulary and limits: one Clear plus 18 pressure segments. */
    restore_canvas(initial);
    history_free(); replay_forget();
    if (load_script_file("examples/yong.glon") != 0) return 18;
    if (replay_prepare() != 0) return 19;
    int pressure_segments = 0;
    size_t command_bytes = 0;
    for (int i = 0; i < replay_count; i++) {
        const char *cmd = replay_buf + replay_offsets[i];
        if (cmd[0] == 'P' && cmd[1] == ' ') pressure_segments++;
        command_bytes += (size_t)replay_lengths[i] + 1;
    }
    int total_commands = replay_count;
    if (total_commands != 19 || pressure_segments != 18) return 20;
    if (command_bytes >= SCRIPT_CMD_BYTES ||
        total_commands > REPLAY_MAX_COMMANDS) return 21;

    /* Smooth replay must equal immediate execution exactly. */
    if (replay_play() != 0) return 22;
    if (replay_finish_smooth(4) != 0) return 23;
    Uint32 *smooth = snapshot_canvas();
    if (!smooth || !replay_snapshots_equal(immediate, smooth)) return 24;
    free(smooth);
    replay_stop();

    /* Pause/resume on a partially animated 永 stroke. */
    int first = 0;
    if (smooth_prepare_first_path("examples/yong.glon", initial, &first) != 0 ||
        first < 1) return 25;
    if (replay_anim_begin() != 1 || replay_anim_render_at(500) != 0) return 26;
    if (replay_anim_memory_bytes() != 2 * replay_pixels_bytes()) return 27;
    replay_pause();
    if (replay_state != REPLAY_PAUSED || !replay_animating ||
        replay_anim_progress != 500) return 28;
    if (replay_play() != 0 || replay_state != REPLAY_PLAYING ||
        !replay_animating || replay_anim_progress != 500) return 29;
    if (replay_anim_finish(1) != 0) return 30;

    /* Step completes a partial command. */
    if (smooth_prepare_first_path("examples/yong.glon", initial, &first) != 0) return 31;
    if (replay_anim_begin() != 1 || replay_anim_render_at(500) != 0) return 32;
    int pos_before = replay_pos;
    if (replay_step() != 0 || replay_state != REPLAY_PAUSED || replay_animating ||
        replay_pos != pos_before + 1) return 33;
    Uint32 *stepped = snapshot_canvas();
    Uint32 *step_ref = replay_prefix_snapshot(initial, pos_before + 1);
    if (!stepped || !step_ref || !replay_snapshots_equal(stepped, step_ref)) return 34;
    free(stepped); free(step_ref);

    /* Back discards a partial command and restores the previous boundary. */
    if (smooth_prepare_first_path("examples/yong.glon", initial, &first) != 0) return 35;
    if (replay_anim_begin() != 1 || replay_anim_render_at(500) != 0) return 36;
    pos_before = replay_pos;
    if (replay_step_back() != 1 || replay_animating) return 37;
    int back_target = pos_before > 0 ? pos_before - 1 : 0;
    if (replay_pos != back_target) return 38;
    Uint32 *backed = snapshot_canvas();
    Uint32 *back_ref = replay_prefix_snapshot(initial, back_target);
    if (!backed || !back_ref || !replay_snapshots_equal(backed, back_ref)) return 39;
    free(backed); free(back_ref);

    /* Stop restores pre-playback. */
    if (smooth_prepare_first_path("examples/yong.glon", initial, &first) != 0) return 40;
    if (replay_anim_begin() != 1 || replay_anim_render_at(500) != 0) return 41;
    replay_stop();
    Uint32 *stopped = snapshot_canvas();
    if (!stopped || !replay_snapshots_equal(initial, stopped) ||
        replay_animating || undo_stack.count != 0) return 42;
    free(stopped);

    /* End during a partial command commits only the completed prefix. */
    if (smooth_prepare_first_path("examples/yong.glon", initial, &first) != 0) return 43;
    Uint32 *prefix_before = replay_prefix_snapshot(initial, first);
    if (!prefix_before) return 44;
    if (replay_anim_begin() != 1 || replay_anim_render_at(500) != 0) return 45;
    replay_end();
    if (replay_animating || undo_stack.count != 1) return 46;
    Uint32 *ended = snapshot_canvas();
    if (!ended || !replay_snapshots_equal(prefix_before, ended)) return 47;
    free(ended);
    if (history_undo() != 1) return 48;
    Uint32 *undo_end = snapshot_canvas();
    if (!undo_end || !replay_snapshots_equal(initial, undo_end)) return 49;
    free(undo_end);
    if (history_redo() != 1) return 50;
    Uint32 *redo_end = snapshot_canvas();
    if (!redo_end || !replay_snapshots_equal(prefix_before, redo_end)) return 51;
    free(redo_end); free(prefix_before);

    /* Repeated smooth playback leaves the loader bounded. */
    long loader0 = loader_used();
    for (int rep = 0; rep < 3; rep++) {
        if (smooth_prepare_first_path("examples/yong.glon", initial, &first) != 0 ||
            replay_finish_smooth(3) != 0) return 52;
        replay_stop();
        replay_forget();
    }
    if (loader_used() != loader0) return 53;

    printf("yong-test: load=ok black=ok undo=ok redo=ok commands=%d "
           "pressure_segments=%d command_bytes=%zu smooth_equal=ok pause=ok "
           "step=ok back=ok stop=ok end=ok loader=%ld result=ok\n",
           total_commands, pressure_segments, command_bytes, loader_used());
    free(immediate);
    free(initial);
    history_free(); replay_forget();
    (void)saved;
    return 0;
}

static int smooth_demo_path(const char *script, const char *prefix) {
    if (app_init() != 0) return 2;
    clear_canvas();
    history_free();
    replay_forget();
    unsaved = 0;
    Uint32 *initial = snapshot_canvas();
    if (!initial) return 3;
    ui_smooth_strokes = 1;
    ui_stroke_duration_index = 2;
    int first = 0;
    if (smooth_prepare_first_path(script, initial, &first) != 0) return 4;
    if (replay_anim_begin() != 1) return 5;

    char names[5][256];
    snprintf(names[0], sizeof names[0], "%s-start.png", prefix);
    snprintf(names[1], sizeof names[1], "%s-25.png", prefix);
    snprintf(names[2], sizeof names[2], "%s-50.png", prefix);
    snprintf(names[3], sizeof names[3], "%s-75.png", prefix);
    snprintf(names[4], sizeof names[4], "%s-final.png", prefix);
    if (save_png(names[0]) != 0) return 6;
    const int percents[] = { 250, 500, 750 };
    for (int i = 0; i < 3; i++) {
        if (replay_anim_render_at(percents[i]) != 0 || save_png(names[i + 1]) != 0)
            return 7;
    }
    /* Finish the demonstrated stroke authoritatively, then the rest smoothly. */
    if (replay_anim_finish(1) != 0 || replay_finish_smooth(4) != 0) return 8;
    if (save_png(names[4]) != 0) return 9;
    Uint32 *smooth_final = snapshot_canvas();
    if (!smooth_final) return 10;

    restore_canvas(initial);
    history_free(); replay_forget();
    if (load_script_file(script) != 0 || run_script() != 0) return 11;
    Uint32 *immediate = snapshot_canvas();
    if (!immediate) return 12;
    int same = replay_snapshots_equal(smooth_final, immediate);
    free(smooth_final); free(immediate); free(initial);
    printf("smooth-demo: script=%s start=%s p25=%s p50=%s p75=%s final=%s "
           "final_equal_immediate=%d result=%s\n",
           script, names[0], names[1], names[2], names[3], names[4],
           same, same ? "ok" : "FAILED");
    history_free(); replay_forget();
    return same ? 0 : 13;
}
static int smooth_demo(void) {
    return smooth_demo_path("examples/calligraphy.glon", "glon-smooth");
}
static int yong_demo(void) {
    return smooth_demo_path("examples/yong.glon", "glon-yong");
}


static int mandala_demo(void) {
    int saved = 0;
    if (app_init() != 0) return 2;
    clear_canvas(); history_free(); replay_forget(); unsaved = 0;
    Uint32 *initial = snapshot_canvas();
    if (!initial) return 3;

    Uint64 t0 = SDL_GetPerformanceCounter();
    if (load_script_file("examples/mandala.glon") != 0) return 4;
    Uint64 t1 = SDL_GetPerformanceCounter();
    double load_ms = (double)(t1 - t0) * 1000.0 /
                     (double)SDL_GetPerformanceFrequency();
    size_t script_bytes = script_source_len;

    t0 = SDL_GetPerformanceCounter();
    if (run_script() != 0) return 5;
    t1 = SDL_GetPerformanceCounter();
    double immediate_ms = (double)(t1 - t0) * 1000.0 /
                          (double)SDL_GetPerformanceFrequency();
    Uint32 *immediate = snapshot_canvas();
    if (!immediate) return 6;
    if (save_png("glon-mandala-final.png") != 0) return 7;

    /* Prepare replay and count its command stream. */
    restore_canvas(initial); history_free(); replay_forget();
    if (load_script_file("examples/mandala.glon") != 0) return 8;
    t0 = SDL_GetPerformanceCounter();
    if (replay_prepare() != 0) return 9;
    t1 = SDL_GetPerformanceCounter();
    double prepare_ms = (double)(t1 - t0) * 1000.0 /
                        (double)SDL_GetPerformanceFrequency();
    int commands = replay_count;
    size_t command_bytes = 0;
    for (int i = 0; i < replay_count; i++) command_bytes += replay_lengths[i] + 1;

    /* Pause, step/back and stop return to the pre-playback canvas. */
    if (replay_play() != 0) return 10;
    replay_pause();
    if (replay_state != REPLAY_PAUSED) return 11;
    int before = replay_pos;
    if (replay_step_forward() != 1 || replay_pos != before + 1) return 12;
    if (replay_step_back() != 1 || replay_pos != before) return 13;
    replay_stop();
    Uint32 *stopped = snapshot_canvas();
    if (!stopped || !replay_snapshots_equal(initial, stopped)) return 14;
    free(stopped);

    /* Capture 25/50/75% replay prefixes for visual inspection. */
    if (replay_prepare() != 0 || replay_play() != 0) return 15;
    int targets[3] = { replay_count / 4, replay_count / 2, replay_count * 3 / 4 };
    const char *names[3] = { "glon-mandala-25.png", "glon-mandala-50.png",
                             "glon-mandala-75.png" };
    for (int i = 0; i < 3; i++) {
        if (replay_reconstruct(targets[i]) != 0) return 16;
        if (save_png(names[i]) != 0) return 17;
    }

    /* Full smooth replay from the pre-playback canvas. */
    if (replay_reconstruct(0) != 0) return 18;
    t0 = SDL_GetPerformanceCounter();
    if (replay_finish_smooth(4) != 0) return 18;
    t1 = SDL_GetPerformanceCounter();
    double replay_ms = (double)(t1 - t0) * 1000.0 /
                       (double)SDL_GetPerformanceFrequency();
    Uint32 *replayed = snapshot_canvas();
    if (save_png("glon-mandala-replay-final.png") != 0) return 20;
    if (!replayed || !replay_snapshots_equal(immediate, replayed)) {
        free(replayed);
        return 19;
    }
    free(replayed);

    /* End commits the completed replay as one document action. */
    replay_end();
    if (replay_nav_available()) return 21;
    if (history_undo() != 1) return 22;
    Uint32 *undone = snapshot_canvas();
    if (!undone || !replay_snapshots_equal(initial, undone)) return 23;
    free(undone);
    if (history_redo() != 1) return 24;
    Uint32 *redone = snapshot_canvas();
    if (!redone || !replay_snapshots_equal(immediate, redone)) return 25;
    free(redone);

    /* Repeated Load -> Clear -> Load. */
    restore_canvas(initial); history_free(); replay_forget();
    if (load_script_file("examples/mandala.glon") != 0 || run_script() != 0)
        return 26;
    document_clear();
    if (load_script_file("examples/mandala.glon") != 0 || run_script() != 0)
        return 27;
    Uint32 *again = snapshot_canvas();
    if (!again || !replay_snapshots_equal(immediate, again)) return 28;
    free(again);

    printf("mandala-demo: source_bytes_stripped=%zu commands=%d command_bytes=%zu "
           "load_ms=%.2f immediate_ms=%.2f prepare_ms=%.2f "
           "smooth_replay_ms=%.2f final_equal=1 lifecycle=ok result=ok\n",
           script_bytes, commands, command_bytes, load_ms, immediate_ms,
           prepare_ms, replay_ms);
    free(immediate);
    free(initial);
    history_free(); replay_forget();
    (void)saved;
    return 0;
}


static int stream_test(void) {
    int saved = 0;
    if (app_init() != 0) return 2;
    clear_canvas(); history_free(); replay_forget(); unsaved = 0;
    Uint32 *initial = snapshot_canvas();
    if (!initial) return 3;
    if (load_script_file("tests/fixtures/stream-100.glon") != 0) return 4;
    if (run_script() != 0) return 5;
    Uint32 *immediate = snapshot_canvas();
    if (!immediate) return 6;
    if (undo_stack.count != 1) return 7;
    if (history_undo() != 1) return 8;
    Uint32 *undone = snapshot_canvas();
    if (!undone || !replay_snapshots_equal(initial, undone)) return 9;
    free(undone);
    if (history_redo() != 1) return 10;
    Uint32 *redone = snapshot_canvas();
    if (!redone || !replay_snapshots_equal(immediate, redone)) return 11;
    free(redone);

    restore_canvas(initial); history_free(); replay_forget();
    if (load_script_file("tests/fixtures/stream-100.glon") != 0) return 12;
    if (replay_prepare() != 0) return 13;
    int commands = replay_count;
    size_t command_bytes = 0;
    for (int i = 0; i < replay_count; i++) command_bytes += replay_lengths[i] + 1;
    if (commands != 101 || command_bytes < 3000) return 14;
    if (replay_play() != 0 || replay_finish_steps() != 0) return 15;
    Uint32 *replayed = snapshot_canvas();
    if (!replayed || !replay_snapshots_equal(immediate, replayed)) return 16;
    free(replayed);
    replay_stop();

    /* Replay pause/step/back/stop remains usable after a long stream. */
    restore_canvas(initial); history_free(); replay_forget();
    if (load_script_file("tests/fixtures/stream-100.glon") != 0 ||
        replay_prepare() != 0 || replay_play() != 0) return 17;
    replay_pause();
    int before = replay_pos;
    if (replay_step_forward() != 1 || replay_pos != before + 1) return 18;
    if (replay_step_back() != 1 || replay_pos != before) return 19;
    replay_stop();
    Uint32 *stopped = snapshot_canvas();
    if (!stopped || !replay_snapshots_equal(initial, stopped)) return 20;
    free(stopped);

    /* End commits the full long stream; undo/redo are exact. */
    restore_canvas(initial); history_free(); replay_forget();
    if (load_script_file("tests/fixtures/stream-100.glon") != 0 ||
        replay_prepare() != 0 || replay_play() != 0 ||
        replay_finish_steps() != 0) return 21;
    replay_end();
    if (replay_nav_available()) return 22;
    if (history_undo() != 1) return 23;
    Uint32 *end_undo = snapshot_canvas();
    if (!end_undo || !replay_snapshots_equal(initial, end_undo)) return 24;
    free(end_undo);
    if (history_redo() != 1) return 25;
    Uint32 *end_redo = snapshot_canvas();
    if (!end_redo || !replay_snapshots_equal(immediate, end_redo)) return 26;
    free(end_redo);

    /* Repeated Load -> Clear -> Load after the long stream. */
    restore_canvas(initial); history_free(); replay_forget();
    if (load_script_file("tests/fixtures/stream-100.glon") != 0 || run_script() != 0)
        return 27;
    document_clear();
    if (load_script_file("tests/fixtures/stream-100.glon") != 0 || run_script() != 0)
        return 28;
    Uint32 *again = snapshot_canvas();
    if (!again || !replay_snapshots_equal(immediate, again)) return 29;
    free(again);
    if (save_png("glon-paint.png") != 0) return 30;

    printf("stream-test: commands=%d command_bytes=%zu immediate_replay_equal=ok "
           "undo_redo=ok pause_step_back_stop=ok end=ok repeated_load=ok "
           "result=ok\n", commands, command_bytes);
    free(immediate); free(initial);
    history_free(); replay_forget();
    (void)saved;
    return 0;
}

static int replay_test(void) {
    int saved = 0;
    if (app_init() != 0) return 2;

    /* Immediate versus replay: blank layer. */
    clear_canvas(); history_free(); replay_forget();
    Uint32 *blank = snapshot_canvas();
    if (!blank) return 3;
    if (replay_compare_paths("examples/calligraphy.glon", blank) != 0) return 4;
    if (replay_compare_paths("examples/replay-demo.glon", blank) != 0) return 5;
    if (replay_nav_prefix_test("examples/replay-demo.glon", blank) != 0) return 5;
    free(blank); blank = NULL;

    /* Imported JPEG background. */
    if (load_image_file("tests/fixtures/tall.jpg") != 0) return 6;
    Uint32 *jpeg_initial = snapshot_canvas();
    if (!jpeg_initial) return 7;
    if (replay_compare_paths("examples/calligraphy.glon", jpeg_initial) != 0) return 8;
    free(jpeg_initial);

    /* Nonempty drawing layer. */
    clear_canvas(); history_free(); replay_forget();
    if (app_set_tool(1) != 0 || app_set_color(2) != 0 || app_set_size(12) != 0) return 9;
    if (test_stroke(80, 80, 250, 160, 5) != 0) return 10;
    history_free();
    Uint32 *nonempty = snapshot_canvas();
    if (!nonempty) return 11;
    if (replay_compare_paths("examples/replay-demo.glon", nonempty) != 0) return 12;
    free(nonempty);
    if (replay_end_semantics_test() != 0) return 12;

    /* Single-step, pause/resume, speed, stop and conflicts. */
    clear_canvas(); history_free(); replay_forget();
    if (load_script_file("examples/calligraphy.glon") != 0) return 13;
    Uint32 *initial = snapshot_canvas();
    if (!initial) return 14;
    if (replay_prepare() != 0) return 15;
    if (replay_step() != 0 || replay_pos != 1 || replay_state != REPLAY_PAUSED) return 16;
    replay_stop();
    Uint32 *stopped = snapshot_canvas();
    if (!stopped || !replay_snapshots_equal(initial, stopped)) return 17;
    free(stopped);

    if (replay_prepare() != 0) return 18;
    if (replay_play() != 0 || replay_state != REPLAY_PLAYING) return 19;
    if (dispatch_canvas_pointer(SDL_MOUSEBUTTONDOWN, 100, 100, SDL_BUTTON_LEFT, &saved) != 0)
        return 20;
    if (history_undo() != 0) return 21;
    if (run_script() == 0) return 22;
    replay_pause();
    if (replay_state != REPLAY_PAUSED) return 23;
    if (replay_step() != 0) return 24;
    if (replay_play() != 0) return 25;
    replay_speed_index = 3; replay_accum_ns = 0;
    if (replay_finish_steps() != 0) return 26;
    Uint32 *final = snapshot_canvas();
    if (!final) return 27;
    Uint32 *immediate_ref = NULL;
    restore_canvas(initial); history_free(); replay_forget();
    if (load_script_file("examples/calligraphy.glon") != 0 || run_script() != 0) return 28;
    immediate_ref = snapshot_canvas();
    if (!immediate_ref || !replay_snapshots_equal(final, immediate_ref)) return 29;

    /* Bidirectional replay navigation against exact command prefixes. */
    restore_canvas(initial); history_free(); replay_forget();
    if (load_script_file("examples/calligraphy.glon") != 0) return 30;
    if (replay_prepare() != 0 || replay_finish_steps() != 0) return 31;
    int nav_total = replay_count;
    int history_before_nav = undo_stack.count;
    if (history_before_nav != 0) return 32;
    int nav_targets[] = { nav_total - 1, nav_total - 3, 2, 0, nav_total / 2, nav_total };
    for (unsigned i = 0; i < sizeof nav_targets / sizeof nav_targets[0]; i++) {
        int target = nav_targets[i];
        if (target < 0) target = 0;
        while (replay_pos > target) if (replay_step_back() != 1) return 33;
        while (replay_pos < target) if (replay_step_forward() != 1) return 34;
        if (undo_stack.count != history_before_nav) return 35;
        Uint32 *expected = replay_prefix_snapshot(initial, target);
        Uint32 *actual = snapshot_canvas();
        if (!expected || !actual || !replay_snapshots_equal(expected, actual)) {
            free(expected); free(actual); return 36;
        }
        free(expected); free(actual);
    }
    /* Ctrl+Z/Ctrl+Y route to replay commands, not document history. */
    if (replay_nav_key(SDLK_z, KMOD_CTRL) != 1 || replay_pos != nav_total - 1) return 37;
    if (replay_nav_key(SDLK_y, KMOD_CTRL) != 1 || replay_pos != nav_total) return 38;
    if (replay_nav_key(SDLK_z, KMOD_CTRL) != 1 ||
        replay_pos != nav_total - 1) return 39;
    if (replay_nav_key(SDLK_z, KMOD_CTRL | KMOD_SHIFT) != 1 ||
        replay_pos != nav_total) return 40;
    /* A second playback must restart from pre-playback and not add a document entry. */
    if (replay_play() != 0 || replay_finish_steps() != 0) return 41;
    Uint32 *second = snapshot_canvas();
    if (!second || !replay_snapshots_equal(final, second)) return 42;
    free(second);
    if (undo_stack.count != history_before_nav) return 43;
    if (history_undo() != 0) return 44;  /* replay navigation blocks document Undo */
    replay_end();
    if (replay_nav_available()) return 45;
    if (history_undo() != 1) return 43;
    Uint32 *undone = snapshot_canvas();
    if (!undone || !replay_snapshots_equal(initial, undone)) return 44;
    free(undone);
    if (history_redo() != 1) return 45;
    Uint32 *redone = snapshot_canvas();
    if (!redone || !replay_snapshots_equal(final, redone)) return 46;
    free(redone);
    free(immediate_ref); immediate_ref = NULL;
    free(final); final = NULL;

    /* Preparation failure leaves canvas/history untouched. */
    restore_canvas(initial); history_free(); replay_forget();
    Uint32 *pre_failure = snapshot_canvas();
    if (!pre_failure) return 41;
    if (load_script_file("tests/fixtures/runtime-error.glon") != 0) return 42;
    if (replay_prepare() == 0 || replay_state != REPLAY_ERROR) return 43;
    Uint32 *post_failure = snapshot_canvas();
    if (!post_failure || !replay_snapshots_equal(pre_failure, post_failure)) return 44;
    free(pre_failure); free(post_failure);

    /* Resize during playback and PNG export. */
    restore_canvas(initial); history_free(); replay_forget();
    if (load_script_file("examples/calligraphy.glon") != 0) return 45;
    if (replay_prepare() != 0 || replay_play() != 0) return 46;
    if (resize_to(1600, 1200) != 0) return 47;
    if (replay_finish_steps() != 0) return 48;
    replay_end();
    if (event_key(SDLK_s, 0, &saved) != 0 || !saved) return 49;
    if (resize_to(1200, 800) != 0) return 50;

    long loader0 = loader_used();
    Uint64 t0 = SDL_GetPerformanceCounter();
    replay_forget(); history_free();
    if (load_script_file("examples/calligraphy.glon") != 0) return 51;
    int measured_commands = 0, measured_bytes = 0;
    for (int rep = 0; rep < 3; rep++) {
        if (replay_prepare() != 0 || replay_finish_steps() != 0) return 52;
        measured_commands = replay_count;
        measured_bytes = 0;
        for (int i = 0; i < replay_count; i++) measured_bytes += replay_lengths[i] + 1;
        replay_stop();
        replay_forget();
    }
    Uint64 t1 = SDL_GetPerformanceCounter();
    if (loader_used() != loader0) return 53;
    double ms = (double)(t1 - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    printf("replay-test: blank=ok jpeg=ok nonempty=ok nav=ok step=ok pause=ok speed=ok "
           "stop=ok end=ok undo=ok repeat=ok failure=ok resize=ok loader=%ld "
           "commands=%d buffer_bytes=%d prepare_repeat_ms=%.2f result=ok\n",
           loader_used(), measured_commands, measured_bytes, ms);
    free(initial);
    history_free(); replay_forget();
    return 0;
}

static int script_test(void) {
    int saved = 0;
    if (app_init() != 0) return 2;
    clear_canvas();
    history_free();
    unsaved = 0;

    /* A script path containing spaces loads through the same parser. */
    if (load_script_file("tests/fixtures/script space.glon") != 0) return 3;
    if (run_script() != 0) return 3;
    history_free();
    clear_canvas();

    /* Loading validates but does not execute. */
    if (load_script_file("examples/rectangle.glon") != 0) return 3;
    Uint8 r = 0, g = 0, b = 0;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,255,255,255)) return 4;
    if (undo_stack.count != 0) return 5;

    /* Rectangle is one undoable script operation. */
    if (run_script() != 0) return 6;
    if (undo_stack.count != 1) return 7;
    if (read_canvas_pixel(100, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,220,60,60)) return 8;
    if (read_canvas_pixel(400, 100, &r, &g, &b) != 0 || !pixel_is(r,g,b,220,60,60)) return 8;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,255,255,255)) return 9;
    if (history_undo() != 1 ||
        read_canvas_pixel(400, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,255,255,255)) return 10;
    if (history_redo() != 1 ||
        read_canvas_pixel(100, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,220,60,60)) return 11;

    /* Running the same script again is deterministic and remains bounded. */
    long loader0 = loader_used();
    if (run_script() != 0) return 12;
    if (read_canvas_pixel(100, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,220,60,60)) return 13;
    if (loader_used() != loader0) return 14;
    if (undo_stack.count != 2) return 15;

    /* Drawing order: the second blue stroke must be on top. */
    if (load_script_file("tests/fixtures/order.glon") != 0) return 16;
    if (run_script() != 0) return 17;
    if (read_canvas_pixel(400, 200, &r, &g, &b) != 0 || !pixel_is(r,g,b,40,120,220)) return 18;

    /* Landscape uses the same vocabulary, not an embedded image. */
    if (load_script_file("examples/landscape.glon") != 0) return 19;
    if (run_script() != 0) return 20;
    if (read_canvas_pixel(400, 20, &r, &g, &b) != 0 || !pixel_is(r,g,b,40,120,220)) return 21;
    if (read_canvas_pixel(100, 350, &r, &g, &b) != 0 || !pixel_is(r,g,b,40,160,80)) return 22;
    if (read_canvas_pixel(500, 400, &r, &g, &b) != 0 || !pixel_is(r,g,b,220,60,60)) return 23;
    if (read_canvas_pixel(200, 400, &r, &g, &b) != 0 || !pixel_is(r,g,b,220,60,60)) return 24;
    if (read_canvas_pixel(700, 80, &r, &g, &b) != 0 || !pixel_is(r,g,b,230,170,40)) return 25;

    /* Manual painting still works after a script. */
    if (app_set_color(2) != 0 || app_set_tool(1) != 0 || app_set_size(8) != 0) return 26;
    if (test_stroke(300, 300, 500, 300, 5) != 0) return 27;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,40,120,220)) return 28;

    /* Script over an imported background; composite export remains 800x600. */
    if (load_image_file("tests/fixtures/tall.jpg") != 0) return 29;
    if (load_script_file("examples/rectangle.glon") != 0) return 30;
    if (run_script() != 0) return 31;
    if (read_canvas_pixel(400, 100, &r, &g, &b) != 0 || !pixel_is(r,g,b,220,60,60)) return 32;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 || !(g > 150 && r < 80 && b < 100)) return 33;
    if (event_key(SDLK_s, 0, &saved) != 0 || !saved) return 34;

    /* Invalid and runtime-failing scripts leave the current canvas untouched. */
    int before = undo_stack.count;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0) return 35;
    Uint8 oldr = r, oldg = g, oldb = b;
    if (load_script_file("/tmp/no-such-glon-script.glon") == 0 || !script_error[0]) return 36;
    if (undo_stack.count != before) return 37;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 ||
        r != oldr || g != oldg || b != oldb) return 38;
    if (load_script_file("tests/fixtures/invalid-script.glon") == 0) return 36;
    if (undo_stack.count != before) return 37;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 ||
        r != oldr || g != oldg || b != oldb) return 38;
    if (load_script_file("tests/fixtures/runtime-error.glon") != 0) return 39;
    if (run_script() == 0) return 40;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 ||
        r != oldr || g != oldg || b != oldb) return 41;
    if (undo_stack.count != before) return 42;

    /* Operation limit fails the run before any command is applied. */
    FILE *f = fopen("/tmp/glon-script-limit.glon", "wb");
    if (!f) return 43;
    for (int i = 0; i < 4097; i++) fprintf(f, "paint/draw-line 0 0 1 1\n");
    fclose(f);
    if (load_script_file("/tmp/glon-script-limit.glon") == 0) {
        if (run_script() == 0) return 44;
    } else if (!script_error[0]) {
        return 44;
    }
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 ||
        r != oldr || g != oldg || b != oldb) return 45;
    remove("/tmp/glon-script-limit.glon");

    printf("script-test: load=ok rectangle=ok order=ok landscape=ok repeat=ok "
           "undo=ok image=ok invalid=ok limit=ok loader_used=%ld result=ok\n",
           loader_used());
    history_free();
    return 0;
}

static int undo_test(void) {
    int saved = 0;
    if (app_init() != 0) return 2;
    if (app_set_color(1) != 0 || app_set_size(4) != 0) return 3;
    clear_canvas();
    history_free();

    /* A stroke with many moves is one operation; undo restores the blank. */
    if (test_stroke(100, 100, 700, 500, 100) != 0) return 4;
    if (undo_stack.count != 1 || !history_can_undo()) return 5;
    Uint8 r = 0, g = 0, b = 0;
    if (history_undo() != 1 || undo_stack.count != 0 || !history_can_redo()) return 6;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 ||
        r != 255 || g != 255 || b != 255) return 7;
    if (history_redo() != 1 || undo_stack.count != 1) return 8;
    if (read_canvas_pixel(100, 100, &r, &g, &b) != 0 ||
        r != 220 || g != 60 || b != 60) return 9;

    /* Repeated undo/redo over several operations. */
    history_free();
    clear_canvas();
    if (test_stroke(150, 100, 300, 200, 5) != 0) return 10;
    if (test_stroke(350, 100, 500, 200, 5) != 0) return 11;
    if (test_stroke(550, 100, 700, 200, 5) != 0) return 12;
    int before = undo_stack.count;
    if (before != 3) return 13;
    for (int i = 0; i < before; i++) if (history_undo() != 1) return 14;
    if (undo_stack.count != 0 || redo_stack.count != before) return 15;
    for (int i = 0; i < before; i++) if (history_redo() != 1) return 16;
    if (undo_stack.count != before || redo_stack.count != 0) return 17;

    /* Clear/undo restores the previous canvas; clear/redo returns blank. */
    if (event_key(SDLK_c, 0, &saved) != 0) return 18;
    if (read_canvas_pixel(400, 100, &r, &g, &b) != 0 ||
        r != 255 || g != 255 || b != 255) return 19;
    if (history_undo() != 1) return 20;
    if (read_canvas_pixel(350, 100, &r, &g, &b) != 0 ||
        r != 220 || g != 60 || b != 60) return 21;
    if (history_redo() != 1) return 22;
    if (read_canvas_pixel(350, 100, &r, &g, &b) != 0 ||
        r != 255 || g != 255 || b != 255) return 23;

    /* Drawing after undo discards redo. */
    if (test_stroke(100, 400, 250, 500, 4) != 0) return 24;
    if (history_undo() != 1 || !history_can_redo()) return 25;
    if (test_stroke(300, 400, 450, 500, 4) != 0) return 26;
    if (redo_stack.count != 0 || history_can_redo()) return 27;

    /* An eraser stroke is one operation and can be undone exactly. */
    history_free();
    clear_canvas();
    if (app_set_tool(1) != 0 || app_set_color(1) != 0) return 28;
    if (test_stroke(100, 300, 700, 300, 20) != 0) return 28;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 ||
        r != 220 || g != 60 || b != 60) return 28;
    if (app_set_tool(2) != 0) return 28;
    if (test_stroke(100, 300, 700, 300, 20) != 0) return 28;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 ||
        r != 255 || g != 255 || b != 255) return 28;
    if (history_undo() != 1) return 28;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 ||
        r != 220 || g != 60 || b != 60) return 28;
    if (history_redo() != 1) return 28;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 ||
        r != 255 || g != 255 || b != 255) return 28;
    if (app_set_tool(1) != 0) return 28;

    /* Keyboard shortcut mapping (the run loop calls the same helper). */
    if (history_undo() != 1) return 29;
    if (!history_shortcut(SDLK_y, KMOD_CTRL) || undo_stack.count < 1) return 29;
    if (history_shortcut(SDLK_z, KMOD_CTRL) != 1 || undo_stack.count < 0) return 30;
    if (!history_shortcut(SDLK_z, KMOD_CTRL | KMOD_SHIFT)) return 31;
    if (history_shortcut(SDLK_z, 0) != 0) return 32;

    /* Bounded history under many operations. */
    history_free();
    clear_canvas();
    for (int i = 0; i < HISTORY_LIMIT + 6; i++) {
        if (test_stroke(50 + i, 50, 100 + i, 100, 2) != 0) return 34;
    }
    if (undo_stack.count != HISTORY_LIMIT) return 35;
    if (redo_stack.count != 0) return 36;
    size_t bound = (size_t)HISTORY_LIMIT * PAINT_W * PAINT_H * sizeof(Uint32);
    if (history_bytes() > bound) return 37;
    printf("undo-test: grouped_stroke=ok repeated=ok clear=ok redo_invalidation=ok "
           "shortcuts=ok undo_depth=%d redo_depth=%d history_bytes=%zu limit=%d result=ok\n",
           undo_stack.count, redo_stack.count, history_bytes(), HISTORY_LIMIT);
    history_free();
    return 0;
}

static int coalesce_test(void) {
    int saved = 0;
    if (app_init() != 0) return 2;
    if (event_key(SDLK_1, 0, &saved) != 0) return 3;
    if (app_set_size(8) != 0) return 4;
    if (event_key(SDLK_c, 0, &saved) != 0) return 5;
    history_free();  /* keep the coalescing test's undo depth to its own stroke */
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!canvas_to_window(100, 100, &x0, &y0) ||
        !canvas_to_window(700, 500, &x1, &y1)) return 6;
    push_mouse(SDL_MOUSEBUTTONDOWN, x0, y0);
    for (int i = 1; i <= 1000; i++) {
        int cx = 100 + (i * 600) / 1000;
        int cy = 100 + (i * 400) / 1000;
        int wx = 0, wy = 0;
        if (!canvas_to_window(cx, cy, &wx, &wy)) return 7;
        push_mouse(SDL_MOUSEMOTION, wx, wy);
    }
    push_mouse(SDL_MOUSEBUTTONUP, x1, y1);
    push_key_mod(SDLK_z, KMOD_CTRL);       /* exercise run-loop undo */
    push_key_mod(SDLK_y, KMOD_CTRL);       /* then redo */
    push_key(SDLK_s);
    push_key(SDLK_ESCAPE);
    perf_raw_motions = perf_app_moves = perf_renders = 0;
    Uint64 t0 = SDL_GetPerformanceCounter();
    run_app_ui();
    Uint64 t1 = SDL_GetPerformanceCounter();
    double ms = (double)(t1 - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    printf("coalesce-test: raw_motions=%ld app_moves=%ld renders=%ld elapsed_ms=%.2f "
           "undo_depth=%d redo_depth=%d\n",
           perf_raw_motions, perf_app_moves, perf_renders, ms,
           undo_stack.count, redo_stack.count);
    if (perf_raw_motions < 1000) return 8;
    if (perf_app_moves >= perf_raw_motions) return 9;
    if (perf_app_moves < 1) return 10;
    if (perf_renders >= perf_raw_motions) return 11;
    return 0;
}



static int stream_probe(int n, size_t cap_bytes) {
    if (n < 1) n = 1;
    if (cap_bytes < 32) cap_bytes = SCRIPT_CMD_BYTES - 1;
    char *buf = malloc(cap_bytes + 1);
    if (!buf) return 2;
    ScriptSink sink = { buf, 0, cap_bytes, 0 };
    FILE *saved_stdout = NULL;
    FILE *stream = NULL;
    int captured = 0;
    int failed = 0;
    r0_s1_outcome last_oc;
    memset(&last_oc, 0, sizeof last_oc);
    if (app_call("paint/script-reset") != 0) failed = 1;
    if (!failed && app_call("paint/script-set-stream 1") != 0) failed = 1;
    if (!failed && script_sink_begin(&saved_stdout, &stream, &sink) != 0) failed = 1;
    else if (!failed) captured = 1;
    int done = 0;
    while (!failed && done < n) {
        char batch[8192];
        size_t used = 0;
        int m = snprintf(batch, sizeof batch, "do [\n");
        if (m < 0) { failed = 1; break; }
        used += (size_t)m;
        for (int j = 0; j < 50 && done < n; j++, done++) {
            m = snprintf(batch + used, sizeof batch - used,
                         "    paint/draw-line %d 100 %d 101\n",
                         100 + done % 500, 101 + done % 500);
            if (m < 0 || (size_t)m >= sizeof batch - used) { failed = 1; break; }
            used += (size_t)m;
        }
        m = snprintf(batch + used, sizeof batch - used, "]\n");
        if (m < 0 || (size_t)m >= sizeof batch - used) { failed = 1; break; }
        used += (size_t)m;
        int rc = r0_s1_session_run(batch, (unsigned)used, &last_oc);
        if (rc != R0S1_OUT_OK) { failed = 1; break; }
    }
    if (captured) {
        captured = 0;
        if (script_sink_end(saved_stdout, stream, &sink) != 0) failed = 1;
    }
    app_call("paint/script-set-stream 0");
    char status[32] = "";
    if (glon_call("paint/script-status", status, sizeof status) != 0)
        snprintf(status, sizeof status, "ERR");
    int commands = 0;
    for (size_t i = 0; i < sink.len; i++) if (buf[i] == '\n') commands++;
    int recovered = -1;
    if (failed || sink.overflow || strcmp(status, "OK") != 0) {
        /* Recovery probe: a fresh cleared sink must accept a short stream. */
        char small[1024];
        int m = snprintf(small, sizeof small,
                         "do [\n    paint/draw-line 10 10 20 20\n]\n");
        char *rbuf = malloc(cap_bytes + 1);
        ScriptSink rsink = { rbuf, 0, cap_bytes, 0 };
        FILE *rsaved = NULL, *rstream = NULL;
        int ok = rbuf && app_call("paint/script-reset") == 0 &&
                 app_call("paint/script-set-stream 1") == 0 &&
                 script_sink_begin(&rsaved, &rstream, &rsink) == 0;
        if (ok && r0_s1_session_run(small, (unsigned)m, &last_oc) == R0S1_OUT_OK) {
            if (script_sink_end(rsaved, rstream, &rsink) == 0) recovered = 1;
        } else {
            if (rstream) { stdout = rsaved; fclose(rstream); }
            recovered = 0;
        }
        app_call("paint/script-set-stream 0");
        free(rbuf);
    }
    printf("stream-probe n=%d cap=%zu failed=%d status=%s emitted=%zu commands=%d "
           "overflow=%d recovered=%d heap_high=%ld gc=%ld live_cells=%ld loader=%ld\n",
           n, cap_bytes, failed, status, sink.len, commands, sink.overflow,
           recovered, r0_s1_heap_high(), r0_s1_gc_count(), r0_s1_gc_live_cells(),
           (long)(M[GC_LOADER_HP] - R0S1_HEAP_BASE));
    free(buf);
    return failed || sink.overflow || strcmp(status, "OK") != 0 ? 3 : 0;
}

static int run_source_old_sink(const char *src, unsigned len, char *out, size_t cap,
                               int *out_len) {
    *out_len = 0;
    if (app_call("paint/script-reset") != 0) return -1;
    if (app_call("paint/script-set-stream 0") != 0) return -1;
    r0_s1_outcome oc;
    int rc = r0_s1_session_run(src, len, &oc);
    if (rc != R0S1_OUT_OK) return -1;
    char status[32] = "";
    if (glon_call("paint/script-status", status, sizeof status) != 0 ||
        strcmp(status, "OK") != 0) return -1;
    if (glon_call("paint/script-result", out, cap) != 0) return -1;
    *out_len = (int)strlen(out);
    return 0;
}
static int run_source_stream(const char *src, unsigned len, char *out, size_t cap,
                             int *out_len, int *overflow) {
    *out_len = 0; *overflow = 0;
    if (app_call("paint/script-reset") != 0) return -1;
    if (app_call("paint/script-set-stream 1") != 0) return -1;
    ScriptSink sink = { out, 0, cap, 0 };
    FILE *saved = NULL, *stream = NULL;
    if (script_sink_begin(&saved, &stream, &sink) != 0) {
        app_call("paint/script-set-stream 0");
        return -1;
    }
    r0_s1_outcome oc;
    int rc = r0_s1_session_run(src, len, &oc);
    int end_rc = script_sink_end(saved, stream, &sink);
    app_call("paint/script-set-stream 0");
    *overflow = sink.overflow;
    if (rc != R0S1_OUT_OK || end_rc != 0) return -1;
    char status[32] = "";
    if (glon_call("paint/script-status", status, sizeof status) != 0 ||
        strcmp(status, "OK") != 0) return -1;
    out[sink.len] = 0;
    *out_len = (int)sink.len;
    return 0;
}
static int stream_compare(const char *path) {
    if (load_script_file(path) != 0) return 2;
    char *old = malloc(SCRIPT_CMD_BYTES);
    char *fresh = malloc(SCRIPT_CMD_BYTES);
    int old_len = 0, new_len = 0, overflow = 0;
    int a = old && fresh ? run_source_old_sink(script_source,
                                               (unsigned)script_source_len,
                                               old, SCRIPT_CMD_BYTES, &old_len) : -1;
    int b = old && fresh ? run_source_stream(script_source,
                                             (unsigned)script_source_len,
                                             fresh, SCRIPT_CMD_BYTES - 1,
                                             &new_len, &overflow) : -1;
    int equal = a == 0 && b == 0 && old_len == new_len && !overflow &&
                memcmp(old, fresh, (size_t)old_len) == 0;
    printf("stream-compare script=%s old_rc=%d new_rc=%d old_bytes=%d new_bytes=%d "
           "overflow=%d byte_equal=%d result=%s\n",
           path, a, b, old_len, new_len, overflow, equal,
           equal ? "ok" : "FAILED");
    free(old); free(fresh); replay_forget();
    return equal ? 0 : 3;
}

static int stack_probe(const char *kind, int n, int interval) {
    if (n < 1) n = 1;
    if (interval < 1) interval = 0;
    size_t cap = SCRIPT_MAX_BYTES + 8192;
    char *src = malloc(cap);
    if (!src) return 2;
    size_t used = 0;
#define APP(...) do { \
        int _w = snprintf(src + used, cap - used, __VA_ARGS__); \
        if (_w < 0 || (size_t)_w >= cap - used) { free(src); return 2; } \
        used += (size_t)_w; \
    } while (0)
    APP("paint/script-reset\n");
    if (strcmp(kind, "nop4") == 0)
        APP("probe/nop4: func [a b c d] [ none ]\n");
    int done = 0;
    while (done < n) {
        APP("do [\n");
        for (int j = 0; j < 20 && done < n; j++, done++) {
            if (strcmp(kind, "arith") == 0) {
                APP("    + 1 2\n");
            } else if (strcmp(kind, "setsize") == 0) {
                APP("    paint/set-size 4\n");
            } else if (strcmp(kind, "nop4") == 0) {
                APP("    probe/nop4 1 2 3 4\n");
            } else {
                if (strcmp(kind, "drawlong") == 0)
                    APP("    paint/draw-line %d %d %d %d\n",
                        1000000 + done, 1000001, 1000002 + done, 1000003);
                else
                    APP("    paint/draw-line %d 100 %d 101\n",
                        100 + done % 500, 101 + done % 500);
                if (interval > 0 && ((done + 1) % interval) == 0)
                    APP("    paint/script-reset\n");
            }
        }
        APP("]\n");
    }
#undef APP
    cell loader0 = M[GC_LOADER_HP];
    r0_s1_outcome oc;
    int rc = r0_s1_session_run(src, (unsigned)used, &oc);
    long free_total = 0, free_blocks = 0, largest_free = 0, live_objs = 0;
    {
        cell hp = M[REG_HP];
        for (cell p = GC_HEAP_BASE; p < hp; ) {
            cell sz = M[p];
            if (sz <= 0 || p + sz > hp) break;
            cell flags = M[p + GC_HDR_FLAGS];
            if (flags == 0) {
                free_total += sz;
                free_blocks++;
                if (sz > largest_free) largest_free = sz;
            } else {
                live_objs++;
            }
            p += sz;
        }
    }
    cell sp_start = r0_s1_sp_start(), sp_end = r0_s1_sp_end();
    cell sp_min = r0_s1_sp_min();
    cell rp_start = r0_s1_rp_start(), rp_end = r0_s1_rp_end();
    cell rp_min = r0_s1_rp_min();
    long cmd_len = -1;
    if (rc == R0S1_OUT_OK) {
        char *cmds = malloc(SCRIPT_CMD_BYTES);
        if (cmds) {
            if (glon_call("paint/script-result", cmds, SCRIPT_CMD_BYTES) == 0)
                cmd_len = (long)strlen(cmds);
            free(cmds);
        }
    }
    if (rc != R0S1_OUT_OK) {
        fprintf(stderr, "stack-probe code around IP %ld:", (long)r0_s1_ip_end());
        cell ip = r0_s1_ip_end();
        for (cell a = ip - 12; a <= ip + 8; a++)
            fprintf(stderr, " %ld:%ld", (long)a, (long)M[a]);
        fprintf(stderr, "\n");
    }
    printf("stack-probe kind=%s n=%d interval=%d rc=%d status=%s detail=%s "
           "halt=%d sentry=%d sp=%ld..%ld min=%ld depth=%ld "
           "rp=%ld..%ld min=%ld depth=%ld host_calls=%ld code=%ld "
           "heap_high=%ld gc=%ld live_cells=%ld free_total=%ld free_blocks=%ld "
           "largest_free=%ld live_objs=%ld loader=%ld cmd_bytes=%ld src_bytes=%zu "
           "alloc=%ld collect=%ld lookup=%ld\n",
           kind, n, interval, rc,
           r0_s1_outcome_status_name(oc.status),
           r0_s1_outcome_detail_name(oc.detail),
           r0_s1_halt_reason(), r0_s1_stack_sentry_fired(),
           (long)sp_start, (long)sp_end, (long)sp_min,
           (long)(sp_start - sp_min),
           (long)rp_start, (long)rp_end, (long)rp_min,
           (long)(rp_min - rp_start),
           (long)r0_s1_host_calls(), (long)r0_s1_code_size(),
           r0_s1_heap_high(), r0_s1_gc_count(), r0_s1_gc_live_cells(),
           free_total, free_blocks, largest_free, live_objs,
           (long)(M[GC_LOADER_HP] - loader0), cmd_len, used,
           (long)r0_s1_alloc_addr(), (long)r0_s1_gc_collect_addr(),
           (long)r0_s1_lookup_addr());
    free(src);
    return 0;
}

int main(int argc, char **argv) {
    const char *root = getenv("PAINT_ROOT");
    if (!root) root = PAINT_ROOT_DEFAULT;
    if (r0_s1_init() == 0) {
        fprintf(stderr, "glon-paint: runtime init failed\n");
        return 2;
    }
    const char *libs[] = { "jupyter/prelude.glon", "demo/shop/strings.glon",
                           "demo/shop/strings-ext.glon",
                           "experiments/glon-gui-paint/paint.glon", NULL };
    for (int i = 0; libs[i]; i++) if (load_source(root, libs[i]) != 0) return 2;
    if (argc > 3 && strcmp(argv[1], "--stack-probe") == 0)
        return stack_probe(argv[2], atoi(argv[3]),
                           argc > 4 ? atoi(argv[4]) : 0);
    if (argc > 2 && strcmp(argv[1], "--stream-probe") == 0)
        return stream_probe(atoi(argv[2]),
                            argc > 3 && argv[3][0] ? (size_t)strtoul(argv[3], NULL, 10) : 0);
    if (argc > 2 && strcmp(argv[1], "--stream-compare") == 0)
        return stream_compare(argv[2]);
    if (argc > 1 && strcmp(argv[1], "--ui-test") == 0) {
        if (sdl_init(1) != 0) return 2;
        if (ui_init() != 0) return 2;
        int rc = ui_test();
        ui_shutdown();
        SDL_DestroyTexture(canvas); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
        fprintf(stderr, "glon-paint: ui-test %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "--undo-test") == 0) {
        if (sdl_init(1) != 0) return 2;
        int rc = undo_test();
        history_free();
        SDL_DestroyTexture(canvas); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
        fprintf(stderr, "glon-paint: undo-test %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "--image-test") == 0) {
        if (sdl_init(1) != 0) return 2;
        int rc = image_test();
        history_free();
        SDL_DestroyTexture(background); SDL_DestroyTexture(canvas); SDL_DestroyTexture(composite);
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); IMG_Quit(); SDL_Quit();
        fprintf(stderr, "glon-paint: image-test %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "--script-test") == 0) {
        if (sdl_init(1) != 0) return 2;
        int rc = script_test();
        history_free();
        free(script_source);
        SDL_DestroyTexture(background); SDL_DestroyTexture(canvas); SDL_DestroyTexture(composite);
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); IMG_Quit(); SDL_Quit();
        fprintf(stderr, "glon-paint: script-test %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "--stream-test") == 0) {
        if (sdl_init(1) != 0) return 2;
        int rc = stream_test();
        history_free(); replay_forget(); free(script_source);
        SDL_DestroyTexture(background); SDL_DestroyTexture(canvas); SDL_DestroyTexture(composite);
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); IMG_Quit(); SDL_Quit();
        fprintf(stderr, "glon-paint: stream-test %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "--mandala-demo") == 0) {
        if (sdl_init(1) != 0) return 2;
        int rc = mandala_demo();
        history_free(); replay_forget(); free(script_source);
        SDL_DestroyTexture(background); SDL_DestroyTexture(canvas); SDL_DestroyTexture(composite);
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); IMG_Quit(); SDL_Quit();
        fprintf(stderr, "glon-paint: mandala-demo %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "--replay-test") == 0) {
        if (sdl_init(1) != 0) return 2;
        int rc = replay_test();
        history_free(); replay_forget(); free(script_source);
        SDL_DestroyTexture(background); SDL_DestroyTexture(canvas); SDL_DestroyTexture(composite);
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); IMG_Quit(); SDL_Quit();
        fprintf(stderr, "glon-paint: replay-test %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "--smooth-test") == 0) {
        if (sdl_init(1) != 0) return 2;
        int rc = smooth_animation_test();
        history_free(); replay_forget(); free(script_source);
        SDL_DestroyTexture(background); SDL_DestroyTexture(canvas); SDL_DestroyTexture(composite);
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); IMG_Quit(); SDL_Quit();
        fprintf(stderr, "glon-paint: smooth-test %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "--smooth-demo") == 0) {
        if (sdl_init(1) != 0) return 2;
        int rc = smooth_demo();
        history_free(); replay_forget(); free(script_source);
        SDL_DestroyTexture(background); SDL_DestroyTexture(canvas); SDL_DestroyTexture(composite);
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); IMG_Quit(); SDL_Quit();
        fprintf(stderr, "glon-paint: smooth-demo %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "--bucket-test") == 0) {
        if (sdl_init(1) != 0) return 2;
        int rc = bucket_test();
        history_free(); replay_forget(); free(script_source);
        SDL_DestroyTexture(background); SDL_DestroyTexture(canvas); SDL_DestroyTexture(composite);
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); IMG_Quit(); SDL_Quit();
        fprintf(stderr, "glon-paint: bucket-test %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "--geometry-test") == 0) {
        if (sdl_init(1) != 0) return 2;
        int rc = geometry_test();
        history_free(); replay_forget(); free(script_source);
        SDL_DestroyTexture(background); SDL_DestroyTexture(canvas); SDL_DestroyTexture(composite);
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); IMG_Quit(); SDL_Quit();
        fprintf(stderr, "glon-paint: geometry-test %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "--lifecycle-test") == 0) {
        if (sdl_init(1) != 0) return 2;
        int rc = lifecycle_test();
        history_free(); replay_forget(); free(script_source);
        SDL_DestroyTexture(background); SDL_DestroyTexture(canvas); SDL_DestroyTexture(composite);
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); IMG_Quit(); SDL_Quit();
        fprintf(stderr, "glon-paint: lifecycle-test %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "--yong-test") == 0) {
        if (sdl_init(1) != 0) return 2;
        int rc = yong_test();
        history_free(); replay_forget(); free(script_source);
        SDL_DestroyTexture(background); SDL_DestroyTexture(canvas); SDL_DestroyTexture(composite);
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); IMG_Quit(); SDL_Quit();
        fprintf(stderr, "glon-paint: yong-test %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "--yong-frames") == 0) {
        if (sdl_init(1) != 0) return 2;
        int rc = yong_frames();
        history_free(); replay_forget(); free(script_source);
        SDL_DestroyTexture(background); SDL_DestroyTexture(canvas); SDL_DestroyTexture(composite);
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); IMG_Quit(); SDL_Quit();
        fprintf(stderr, "glon-paint: yong-frames %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "--yong-demo") == 0) {
        if (sdl_init(1) != 0) return 2;
        int rc = yong_demo();
        history_free(); replay_forget(); free(script_source);
        SDL_DestroyTexture(background); SDL_DestroyTexture(canvas); SDL_DestroyTexture(composite);
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); IMG_Quit(); SDL_Quit();
        fprintf(stderr, "glon-paint: yong-demo %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "--calligraphy-test") == 0) {
        if (sdl_init(1) != 0) return 2;
        int rc = calligraphy_test();
        history_free();
        free(script_source);
        SDL_DestroyTexture(background); SDL_DestroyTexture(canvas); SDL_DestroyTexture(composite);
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); IMG_Quit(); SDL_Quit();
        fprintf(stderr, "glon-paint: calligraphy-test %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "--coalesce-test") == 0) {
        if (sdl_init(1) != 0) return 2;
        if (ui_init() != 0) return 2;
        int rc = coalesce_test();
        ui_shutdown();
        SDL_DestroyTexture(canvas); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
        fprintf(stderr, "glon-paint: coalesce-test %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "--cursor-test") == 0) {
        if (sdl_init(1) != 0) return 2;
        int cursor_before = SDL_ShowCursor(SDL_QUERY);
        const char *driver = SDL_GetCurrentVideoDriver();
        const char *libdecor = getenv("SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR");
        printf("cursor-test: video=%s cursor=%d decorations=%s libdecor=%s plugin=%s\n",
               driver ? driver : "(none)", cursor_before, decor_mode_name(),
               libdecor ? libdecor : "(default)",
               libdecor_plugin_dir[0] ? libdecor_plugin_dir : "(none)");
        if (wslg_wayland_session() && driver && strcmp(driver, "wayland") == 0 &&
            decor_mode != DECOR_PATCHED_LIBDECOR) {
            fprintf(stderr, "cursor-test: WSLg Wayland needs the patched libdecor "
                    "plugin (decorations=%s)\n", decor_mode_name());
            return 5;
        }
#ifdef HAVE_NFD
        int ready = nfd_ready();
        const char *gdk = getenv("GDK_BACKEND");
        printf("cursor-test: nfd=%d gdk=%s cursor_after=%d\n",
               ready, gdk ? gdk : "(unset)", SDL_ShowCursor(SDL_QUERY));
        if (ready && driver && gdk &&
            (strcmp(driver, "x11") == 0 || strcmp(driver, "wayland") == 0) &&
            strcmp(gdk, driver) != 0) {
            fprintf(stderr, "cursor-test: NFD/GTK backend does not match SDL\n");
            return 3;
        }
#endif
        if (SDL_ShowCursor(SDL_QUERY) != cursor_before) return 4;
        SDL_DestroyTexture(background); SDL_DestroyTexture(canvas); SDL_DestroyTexture(composite);
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
#ifdef HAVE_NFD
        if (nfd_state == 1) NFD_Quit();
#endif
        IMG_Quit(); SDL_Quit();
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--nfd-probe") == 0) {
#ifdef HAVE_NFD
        if (nfd_ready()) {
            printf("nfd-probe: ready\n");
            NFD_Quit();
            return 0;
        }
        printf("nfd-probe: unavailable: %s\n", NFD_GetError() ? NFD_GetError() : "unknown");
        return 1;
#else
        printf("nfd-probe: unavailable: built without GTK/NFD\n");
        return 1;
#endif
    }
    if (argc > 1 && strcmp(argv[1], "--resize-test") == 0) {
        if (sdl_init(0) != 0) return 2;
        int rc = resize_test();
        SDL_DestroyTexture(canvas); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
        fprintf(stderr, "glon-paint: resize-test %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 2 && strcmp(argv[1], "--move-stress") == 0) {
        int n = atoi(argv[2]);
        if (n < 1) n = 10000;
        if (sdl_init(1) != 0) return 2;
        int rc = move_stress(n);
        SDL_DestroyTexture(canvas); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
        fprintf(stderr, "glon-paint: move-stress %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "--self-test") == 0) {
        if (sdl_init(1) != 0) return 2;
        int rc = self_test();
        SDL_DestroyTexture(canvas); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
        fprintf(stderr, "glon-paint: self-test %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (sdl_init(0) != 0) return 2;
    if (ui_init() != 0) return 2;
    if (app_init() != 0) {
        ui_shutdown(); SDL_DestroyTexture(canvas); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
        return 2;
    }
    run_app_ui();
    history_free();
    ui_shutdown();
    SDL_DestroyTexture(canvas); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
