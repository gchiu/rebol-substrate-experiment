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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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
#define UI_TOOL_W   62
#define UI_RIGHT_W  252
#define UI_GAP      8
#define UI_MARGIN   24
#define HISTORY_LIMIT 16
#define SCRIPT_MAX_BYTES 262144
#define SCRIPT_CMD_BYTES (1024 * 1024)

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
static SDL_Rect ui_brush_bounds, ui_eraser_bounds;
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
    int right_h = (avail_h * 3) / 5;
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
static Uint32 *snapshot_canvas(void) {
    Uint32 *px = malloc((size_t)PAINT_W * PAINT_H * sizeof(Uint32));
    if (!px) return NULL;
    begin_canvas();
    if (SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888,
                             px, PAINT_W * sizeof(Uint32)) != 0) {
        free(px);
        return NULL;
    }
    return px;
}
static int restore_canvas(const Uint32 *px) {
    begin_canvas();
    if (SDL_UpdateTexture(canvas, NULL, px, PAINT_W * sizeof(Uint32)) != 0) return -1;
    dirty = 1;
    return 0;
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
static int load_image_file(const char *path) {
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
    dirty = 1;
    return 0;
}

static void request_open(void) {
    open_error[0] = 0;
    if (unsaved) {
        confirm_open = 1;
    } else {
        open_dialog = 1;
    }
}
static void request_open_script(void) {
    script_dialog = 1;
    script_error[0] = 0;
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
    free(script_source);
    script_source = src;
    script_source_len = len;
    script_loaded = 1;
    snprintf(script_path, sizeof script_path, "%s", path);
    script_error[0] = 0;
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
    } else if (line[0] == 'C' && line[1] == 0) {
        if (!history_replaying) history_record_clear();
        clear_canvas();
        unsaved = 1;
    } else if (line[0] == 'T' && line[1] == ' ') {
        SDL_SetWindowTitle(win, line + 2);
    } else if (line[0] == 'S' && line[1] == ' ') {
        if (save_png(line + 2) == 0) { *saved = 1; unsaved = 0; }
    } else if (line[0] == 'I' && line[1] == ' ') {
        int c, z, t, d;
        if (sscanf(line + 2, "%d %d %d %d", &c, &z, &t, &d) == 4) {
            ui_color = c; ui_size = z; ui_tool = t; ui_drawing = d;
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

static int app_call(const char *src);
static int process_script_commands(char *buf, int *saved) {
    char *p = buf;
    while (p && *p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        if (*p) {
            if (!((p[0] == 'L' || p[0] == 'E' || p[0] == 'C') &&
                  (p[1] == ' ' || p[1] == 0)))
                return -1;
            command(p, saved);
        }
        if (!nl) break;
        p = nl + 1;
    }
    return 0;
}
static int run_script(void) {
    if (!script_loaded || !script_source) {
        snprintf(script_error, sizeof script_error, "No script loaded.");
        return -1;
    }
    int unsaved_before = unsaved;
    Uint32 *pre = snapshot_canvas();
    if (!pre) {
        snprintf(script_error, sizeof script_error, "Cannot snapshot drawing layer.");
        return -1;
    }
    cell save_lhp = M[GC_LOADER_HP];
    char *cmds = malloc(SCRIPT_CMD_BYTES);
    char status[32] = "";
    int failed = 0;
    script_error[0] = 0;
    script_message[0] = 0;

    if (!cmds) {
        snprintf(script_error, sizeof script_error, "Out of memory for script commands.");
        failed = 1;
    }
    if (!failed && app_call("paint/script-reset") != 0) {
        snprintf(script_error, sizeof script_error, "Cannot reset script sink.");
        failed = 1;
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
    if (!failed && (glon_call("paint/script-status", status, sizeof status) != 0 ||
                    strcmp(status, "OK") != 0)) {
        snprintf(script_error, sizeof script_error,
                 "Script exceeded the drawing-operation limit.");
        failed = 1;
    }
    if (!failed && glon_call("paint/script-result", cmds, SCRIPT_CMD_BYTES) != 0) {
        snprintf(script_error, sizeof script_error, "Cannot read script drawing commands.");
        failed = 1;
    }
    if (!failed && strlen(cmds) >= SCRIPT_CMD_BYTES - 1) {
        snprintf(script_error, sizeof script_error, "Script command buffer is too large.");
        failed = 1;
    }
    if (!failed) {
        int saved = 0;
        history_replaying = 1;
        int arc = process_script_commands(cmds, &saved);
        history_replaying = 0;
        if (arc != 0) {
            snprintf(script_error, sizeof script_error, "Script emitted an unsupported command.");
            failed = 1;
        }
    } else {
        history_replaying = 0;
    }
    if (failed) {
        if (pre) restore_canvas(pre);
        unsaved = unsaved_before;
    } else {
        history_stack_clear(&redo_stack);
        history_push(&undo_stack, pre);
        pre = NULL;
        unsaved = 1;
        snprintf(script_message, sizeof script_message, "Script applied.");
        dirty = 1;
    }
    if (pre) free(pre);
    free(cmds);
    app_call("paint/title");  /* refresh the C UI mirror after script state changes */
    M[GC_LOADER_HP] = save_lhp;
    return failed ? -1 : 0;
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
    char src[128], cmdbuf[2048];
    snprintf(src, sizeof src, "paint/key %d", map_key(sym, mod));
    if (glon_call(src, cmdbuf, sizeof cmdbuf) != 0) return -1;
    process_commands(cmdbuf, saved);
    return 0;
}
static int event_mouse(int type, int wx, int wy, int button, int *saved) {
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
static int dispatch_canvas_pointer(int type, int wx, int wy, int button, int *saved) {
    if (point_in_ui(wx, wy)) return 0;
    if (!point_in_rect(&layout.image, wx, wy)) return 0;
    return event_mouse(type, wx, wy, button, saved);
}

static int sdl_init(int hidden) {
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
            if (nk_menu_item_label(nk, "New / Clear", NK_TEXT_LEFT)) app_key_code(99);
            if (nk_menu_item_label(nk, "Open PNG / JPEG...", NK_TEXT_LEFT)) request_open();
            if (nk_menu_item_label(nk, "Open Script...", NK_TEXT_LEFT)) request_open_script();
            if (nk_menu_item_label(nk, "Save PNG", NK_TEXT_LEFT)) app_key_code(115);
            if (script_loaded) {
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
            if (nk_menu_item_label(nk, "Clear Canvas", NK_TEXT_LEFT)) app_key_code(99);
            if (history_can_undo()) {
                if (nk_menu_item_label(nk, "Undo  Ctrl+Z", NK_TEXT_LEFT)) history_undo();
            } else {
                nk_widget_disable_begin(nk);
                nk_menu_item_label(nk, "Undo  Ctrl+Z", NK_TEXT_LEFT);
                nk_widget_disable_end(nk);
            }
            if (history_can_redo()) {
                if (nk_menu_item_label(nk, "Redo  Ctrl+Y", NK_TEXT_LEFT)) history_redo();
            } else {
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
        nk_widget_disable_begin(nk);
        nk_button_label(nk, "Rectangle");
        nk_widget_disable_end(nk);
        nk_layout_row_dynamic(nk, 18, 1);
        nk_label(nk, "size", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 24, 1);
        nk_label(nk, ui_tool == 2 ? "eraser" : "brush", NK_TEXT_LEFT);
    }
    nk_end(nk);
}
static void ui_right_panel(void) {
    if (nk_begin(nk, "##inspector", ui_rect(layout.right.x, layout.right.y,
                                            layout.right.w, layout.right.h),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER)) {
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
                     ui_color == 3 ? "Green" : "Yellow", NK_TEXT_LEFT);
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
        nk_label(nk, "TOOL PROPERTIES", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 20, 1);
        nk_label(nk, ui_tool == 2 ? "Eraser" : "Brush", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 20, 1);
        nk_label(nk, "Opacity 100% (fixed)", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 20, 1);
        nk_label(nk, "Hardness 100% (fixed)", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 20, 1);
        nk_label(nk, "Rectangle: unavailable", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 20, 1);
        nk_label(nk, "SCRIPT", NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 28, 1);
        if (script_loaded) {
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
        char s[160];
        snprintf(s, sizeof s, "Canvas 800 x 600   |   Tool: %s   |   Colour: %d   |   Size: %d px",
                 ui_tool == 2 ? "Eraser" : "Brush", ui_color, ui_size);
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
                confirm_open = 0; open_dialog = 1; open_error[0] = 0;
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
                if (load_image_file(open_path) == 0) open_dialog = 0;
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
            if (load_script_file(script_path) == 0) script_dialog = 0;
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
    int ww = 0, wh = 0, tw = 0, th = 0;
    SDL_GetWindowSize(win, &ww, &wh);
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
        SDL_Event e;
        if (!SDL_WaitEventTimeout(&e, 12)) continue;
        update_layout();
        nk_input_begin(nk);

        int pending_down = 0, pending_up = 0, pending_motion = 0;
        int down_ui = 0;
        int dx = 0, dy = 0, mx = 0, my = 0;
        SDL_KeyboardEvent keys[16];
        int nkeys = 0;
        do {
            if (e.type == SDL_QUIT) quit_flag = 1;
            else if (e.type == SDL_WINDOWEVENT &&
                     (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                      e.window.event == SDL_WINDOWEVENT_RESIZED ||
                      e.window.event == SDL_WINDOWEVENT_EXPOSED)) dirty = 1;
            else if (e.type == SDL_KEYDOWN) {
                if (nkeys < (int)(sizeof keys / sizeof keys[0])) keys[nkeys++] = e.key;
            } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                if (e.button.button == SDL_BUTTON_LEFT) {
                    pending_down = 1; dx = e.button.x; dy = e.button.y;
                    down_ui = open_dialog || confirm_open || script_dialog || show_about ||
                              point_in_ui(dx, dy);
                }
            } else if (e.type == SDL_MOUSEBUTTONUP) {
                if (e.button.button == SDL_BUTTON_LEFT) pending_up = 1;
            } else if (e.type == SDL_MOUSEMOTION) {
                pending_motion = 1; mx = e.motion.x; my = e.motion.y;
                perf_raw_motions++;
            }
            nk_sdl_handle_event(&e);
        } while (SDL_PollEvent(&e));
        nk_sdl_handle_grab();
        nk_input_end(nk);

        update_layout();
        ui_build();

        if (pending_down) {
            int down_inside = !down_ui && !point_in_ui(dx, dy) &&
                              point_in_rect(&layout.image, dx, dy);
            if (down_inside) {
                history_begin_stroke();   /* snapshot before the first pixel */
                if (dispatch_canvas_pointer(SDL_MOUSEBUTTONDOWN, dx, dy,
                                            SDL_BUTTON_LEFT, &saved) == 0) {
                    dragging = 1; last_wx = dx; last_wy = dy;
                } else {
                    history_cancel_stroke();
                }
            } else {
                history_cancel_stroke();
            }
        }
        if (pending_motion && dragging) {
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
        if (pending_up && dragging) {
            event_mouse(SDL_MOUSEBUTTONUP, last_wx, last_wy, SDL_BUTTON_LEFT, &saved);
            history_commit_stroke();
            dragging = 0;
        }
        /* Key actions happen after pending painting, so a save key in the
         * same batch exports the stroke, not the previous frame. */
        for (int k = 0; k < nkeys; k++) {
            if (open_dialog || confirm_open || script_dialog) continue;  /* Nuklear owns text keys */
            if ((keys[k].keysym.mod & KMOD_CTRL) &&
                (keys[k].keysym.sym == SDLK_z || keys[k].keysym.sym == SDLK_y)) {
                if (dragging) {
                    event_mouse(SDL_MOUSEBUTTONUP, last_wx, last_wy, SDL_BUTTON_LEFT, &saved);
                    history_commit_stroke();
                    dragging = 0;
                }
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

    /* Wide PNG: aspect-fit to 800x200, centred vertically over white. */
    if (load_image_file("tests/fixtures/wide.png") != 0) return 3;
    Uint8 r = 0, g = 0, b = 0;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 || !pixel_is(r,g,b,220,20,20)) return 4;
    if (read_canvas_pixel(400, 100, &r, &g, &b) != 0 || !pixel_is(r,g,b,255,255,255)) return 5;
    if (read_canvas_pixel(100, 100, &r, &g, &b) != 0 || !pixel_is(r,g,b,255,255,255)) return 5;
    if (unsaved) return 6;
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

static int script_test(void) {
    int saved = 0;
    if (app_init() != 0) return 2;
    clear_canvas();
    history_free();
    unsaved = 0;

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
    if (argc > 1 && strcmp(argv[1], "--coalesce-test") == 0) {
        if (sdl_init(1) != 0) return 2;
        if (ui_init() != 0) return 2;
        int rc = coalesce_test();
        ui_shutdown();
        SDL_DestroyTexture(canvas); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
        fprintf(stderr, "glon-paint: coalesce-test %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "--resize-test") == 0) {
        if (sdl_init(1) != 0) return 2;
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
