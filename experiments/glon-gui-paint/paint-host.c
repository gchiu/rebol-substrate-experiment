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
 *   C
 *   T title text
 *   S path.png
 *   Q
 *
 * There are no matching or paint-policy decisions in this file.
 */
#include "r0_s1.h"
#include "s1.h"
#include <SDL.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define PAINT_W 800
#define PAINT_H 600
#define PAINT_ROOT_DEFAULT "../.."

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *canvas;
static int dirty = 1;
static int quit_flag = 0;

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

static void set_color(int r, int g, int b) {
    SDL_SetRenderDrawColor(ren, (Uint8)r, (Uint8)g, (Uint8)b, 255);
}
static void begin_canvas(void) { SDL_SetRenderTarget(ren, canvas); }

/* The 800x600 canvas is stretched to fill the current window. SDL mouse
 * coordinates are window coordinates, so invert that same scale before the
 * event reaches Glon. Window size (not renderer pixels) is the correct input
 * because SDL mouse events use window units, including on HiDPI displays. */
static int map_window_to_canvas(int ww, int wh, int wx, int wy, int *cx, int *cy) {
    if (ww <= 0 || wh <= 0) return 0;
    if (wx < 0) wx = 0;
    if (wy < 0) wy = 0;
    if (wx >= ww) wx = ww - 1;
    if (wy >= wh) wy = wh - 1;
    int x = (int)(((int64_t)wx * PAINT_W) / ww);
    int y = (int)(((int64_t)wy * PAINT_H) / wh);
    if (x >= PAINT_W) x = PAINT_W - 1;
    if (y >= PAINT_H) y = PAINT_H - 1;
    *cx = x; *cy = y;
    return 1;
}
static void clear_canvas(void) {
    begin_canvas();
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_RenderClear(ren);
    dirty = 1;
}
static void draw_line(int x1, int y1, int x2, int y2, int size,
                      int r, int g, int b) {
    begin_canvas();
    set_color(r, g, b);
    int radius = size > 1 ? size / 2 : 0;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (radius == 0 || dx * dx + dy * dy <= radius * radius)
                SDL_RenderDrawLine(ren, x1 + dx, y1 + dy, x2 + dx, y2 + dy);
        }
    }
    dirty = 1;
}
static void render_frame(void) {
    SDL_SetRenderTarget(ren, NULL);
    SDL_RenderSetViewport(ren, NULL);
    SDL_RenderSetClipRect(ren, NULL);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_RenderClear(ren);                 /* fill newly exposed window area */
    SDL_RenderCopy(ren, canvas, NULL, NULL); /* stretch full 800x600 canvas */
    dirty = 0;
}
static void present_frame(void) {
    render_frame();
    SDL_RenderPresent(ren);
}

static int save_png(const char *path) {
    begin_canvas();
    Uint32 *pixels = malloc((size_t)PAINT_W * PAINT_H * sizeof(Uint32));
    if (!pixels) return -1;
    if (SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888,
                             pixels, PAINT_W * sizeof(Uint32)) != 0) {
        free(pixels); return -1;
    }
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

/* Execute one Glon command. C does not decide brush/tool state. */
static void command(const char *line, int *saved) {
    if (line[0] == 'L' && line[1] == ' ') {
        int x1, y1, x2, y2, size, r, g, b;
        if (sscanf(line + 2, "%d %d %d %d %d %d %d %d",
                   &x1, &y1, &x2, &y2, &size, &r, &g, &b) == 8)
            draw_line(x1, y1, x2, y2, size, r, g, b);
    } else if (line[0] == 'C' && line[1] == 0) {
        clear_canvas();
    } else if (line[0] == 'T' && line[1] == ' ') {
        SDL_SetWindowTitle(win, line + 2);
    } else if (line[0] == 'S' && line[1] == ' ') {
        if (save_png(line + 2) == 0) *saved = 1;
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

static int sdl_init(int hidden) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "glon-paint: SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    Uint32 flags = hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;
    win = SDL_CreateWindow("Glon Paint", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           PAINT_W, PAINT_H, flags | SDL_WINDOW_RESIZABLE);
    if (!win) { fprintf(stderr, "glon-paint: window: %s\n", SDL_GetError()); SDL_Quit(); return -1; }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "glon-paint: renderer: %s\n", SDL_GetError()); SDL_DestroyWindow(win); SDL_Quit(); return -1; }
    canvas = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                               SDL_TEXTUREACCESS_TARGET, PAINT_W, PAINT_H);
    if (!canvas) { fprintf(stderr, "glon-paint: canvas: %s\n", SDL_GetError()); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit(); return -1; }
    clear_canvas();
    return 0;
}

static void push_mouse(Uint32 type, int x, int y) {
    SDL_Event e; memset(&e, 0, sizeof e); e.type = type;
    if (type == SDL_MOUSEMOTION) { e.motion.x = x; e.motion.y = y; e.motion.state = SDL_BUTTON_LMASK; }
    else { e.button.x = x; e.button.y = y; e.button.button = SDL_BUTTON_LEFT; e.button.state = (type == SDL_MOUSEBUTTONDOWN) ? SDL_PRESSED : SDL_RELEASED; }
    SDL_PushEvent(&e);
}
static void push_key(SDL_Keycode k) {
    SDL_Event e; memset(&e, 0, sizeof e); e.type = SDL_KEYDOWN; e.key.keysym.sym = k; SDL_PushEvent(&e);
}
static long loader_used(void) { return (long)(M[GC_LOADER_HP] - R0S1_HEAP_BASE); }
static long loader_free_cells(void) { return (long)(R0S1_HEAP_LIMIT - M[GC_LOADER_HP]); }

static int read_canvas_pixel(int x, int y, Uint8 *rr, Uint8 *gg, Uint8 *bb) {
    begin_canvas();
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
static int output_black_pixels(void) {
    int w = 0, h = 0;
    if (SDL_GetRendererOutputSize(ren, &w, &h) != 0 || w <= 0 || h <= 0) return -1;
    Uint32 *pix = malloc((size_t)w * h * sizeof(Uint32));
    if (!pix) return -1;
    SDL_SetRenderTarget(ren, NULL);
    if (SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, pix,
                             w * (int)sizeof(Uint32)) != 0) {
        free(pix);
        return -1;
    }
    SDL_PixelFormat *fmt = SDL_AllocFormat(SDL_PIXELFORMAT_ARGB8888);
    if (!fmt) { free(pix); return -1; }
    int black = 0;
    for (int i = 0; i < w * h; i++) {
        Uint8 r, g, b, a;
        SDL_GetRGBA(pix[i], fmt, &r, &g, &b, &a);
        if (r == 0 && g == 0 && b == 0) black++;
    }
    SDL_FreeFormat(fmt);
    free(pix);
    return black;
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
    static const struct { int ww, wh, wx, wy, cx, cy; } maps[] = {
        { 800, 600, 0, 0, 0, 0 },
        { 800, 600, 799, 599, 799, 599 },
        { 1600, 1200, 800, 600, 400, 300 },
        { 600, 900, 450, 225, 600, 150 },
        { 1000, 500, 250, 400, 200, 480 },
    };
    for (unsigned i = 0; i < sizeof maps / sizeof maps[0]; i++) {
        int x = -1, y = -1;
        if (!map_window_to_canvas(maps[i].ww, maps[i].wh, maps[i].wx, maps[i].wy, &x, &y) ||
            x != maps[i].cx || y != maps[i].cy) {
            fprintf(stderr, "resize-test: map case %u failed (%d,%d -> %d,%d, want %d,%d)\n",
                    i, maps[i].wx, maps[i].wy, x, y, maps[i].cx, maps[i].cy);
            return 2;
        }
    }
    int saved = 0;
    if (app_init() != 0) return 3;
    if (event_key(SDLK_1, 0, &saved) != 0) return 4;
    if (event_key(SDLK_c, 0, &saved) != 0) return 5;
    struct { int w, h, wx, wy, cx, cy; } cases[] = {
        { 800, 600, 100, 100, 100, 100 },
        { 1600, 1200, 800, 600, 400, 300 },
        { 600, 900, 450, 225, 600, 150 },
        { 1000, 500, 250, 400, 200, 480 },
        { 800, 600, 700, 500, 700, 500 },
        { 1200, 800, 120, 80, 80, 60 },
        { 800, 600, 100, 100, 100, 100 },
    };
    int total_black = 0;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        if (resize_to(cases[i].w, cases[i].h) != 0) {
            fprintf(stderr, "resize-test: resize to %dx%d failed\n", cases[i].w, cases[i].h);
            return 6;
        }
        int black = output_black_pixels();
        if (black < 0) {
            fprintf(stderr, "resize-test: output readback failed at %dx%d\n", cases[i].w, cases[i].h);
            return 7;
        }
        total_black += black;
        SDL_RenderPresent(ren);
        if (event_mouse(SDL_MOUSEBUTTONDOWN, cases[i].wx, cases[i].wy,
                        SDL_BUTTON_LEFT, &saved) != 0 ||
            event_mouse(SDL_MOUSEBUTTONUP, cases[i].wx, cases[i].wy,
                        SDL_BUTTON_LEFT, &saved) != 0)
            return 8;
        Uint8 r = 0, g = 0, b = 0;
        if (read_canvas_pixel(cases[i].cx, cases[i].cy, &r, &g, &b) != 0 ||
            r != 220 || g != 60 || b != 60) {
            fprintf(stderr, "resize-test: draw at window (%d,%d) did not map to canvas "
                    "(%d,%d): got %u,%u,%u\n", cases[i].wx, cases[i].wy,
                    cases[i].cx, cases[i].cy, r, g, b);
            return 9;
        }
    }
    /* Every earlier stroke must survive the whole resize sequence. */
    Uint8 r, g, b;
    if (read_canvas_pixel(100, 100, &r, &g, &b) != 0 || r != 220 || g != 60 || b != 60)
        return 10;
    if (read_canvas_pixel(400, 300, &r, &g, &b) != 0 || r != 220 || g != 60 || b != 60)
        return 10;
    if (read_canvas_pixel(80, 60, &r, &g, &b) != 0 || r != 220 || g != 60 || b != 60)
        return 10;
    if (event_key(SDLK_s, 0, &saved) != 0 || !saved) return 11;
    if (total_black != 0) {
        fprintf(stderr, "resize-test: %d black output pixels after resize\n", total_black);
        return 12;
    }
    printf("resize-test: maps=%u resizes=%u black_pixels=%d canvas=%dx%d result=ok\n",
           (unsigned)(sizeof maps / sizeof maps[0]),
           (unsigned)(sizeof cases / sizeof cases[0]), total_black, PAINT_W, PAINT_H);
    present_frame();
    return 0;
}
static int move_stress(int moves) {
    int saved = 0;
    if (app_init() != 0) return 2;
    long loader0 = loader_used(), loader_max = loader0;
    int symbols0 = 0; while (r0_s1_sym_name(symbols0)) symbols0++;
    if (event_mouse(SDL_MOUSEBUTTONDOWN, 10, 10, SDL_BUTTON_LEFT, &saved) != 0) return 3;
    int failed_at = -1;
    for (int i = 0; i < moves; i++) {
        int x = 10 + (i % 760), y = 10 + ((i / 760) % 560);
        if (event_mouse(SDL_MOUSEMOTION, x, y, 0, &saved) != 0) { failed_at = i; break; }
        long used = loader_used();
        if (used > loader_max) loader_max = used;
    }
    if (failed_at < 0) {
        if (event_mouse(SDL_MOUSEBUTTONUP, 770, 570, SDL_BUTTON_LEFT, &saved) != 0) return 4;
        if (event_key(SDLK_s, 0, &saved) != 0) return 5;
    }
    printf("move-stress: moves=%d result=%s failed_at=%d loader_used_start=%ld "
           "loader_used_end=%ld loader_used_max=%ld loader_free=%ld "
           "managed_high=%ld gc_collects=%ld gc_live_cells=%ld\n",
           moves, failed_at < 0 ? "ok" : "FAILED", failed_at,
           loader0, loader_used(), loader_max, loader_free_cells(),
           r0_s1_heap_high(), r0_s1_gc_count(), r0_s1_gc_live_cells());
    int symbols1 = 0; while (r0_s1_sym_name(symbols1)) symbols1++;
    printf("move-stress: symbols_start=%d symbols_end=%d\n", symbols0, symbols1);
    if (failed_at >= 0) return 6;
    long growth = loader_used() - loader0;
    if (growth > 16) {
        fprintf(stderr, "move-stress: loader grew by %ld cells (expected bounded)\n", growth);
        return 7;
    }
    return saved ? 0 : 8;
}
static int self_test(void) {
    int saved = 0;
    if (app_init() != 0) return 2;
    /* SDL queue path: clear, colour, size, stroke, eraser stroke, save, quit. */
    push_key(SDLK_c); push_key(SDLK_2); push_key(SDLK_PLUS);
    push_mouse(SDL_MOUSEBUTTONDOWN, 20, 20); push_mouse(SDL_MOUSEMOTION, 100, 100);
    push_mouse(SDL_MOUSEMOTION, 160, 60); push_mouse(SDL_MOUSEBUTTONUP, 160, 60);
    push_key(SDLK_e);
    push_mouse(SDL_MOUSEBUTTONDOWN, 200, 100); push_mouse(SDL_MOUSEMOTION, 260, 100);
    push_mouse(SDL_MOUSEBUTTONUP, 260, 100);
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
    int saved = 0;
    if (app_init() != 0) { SDL_DestroyTexture(canvas); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit(); return 2; }
    present_frame();
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
    SDL_DestroyTexture(canvas); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
