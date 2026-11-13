/* Glon Doom G5a - native SDL2 host for a Glon-authored room/corridor.
 *
 * Glon owns the world data, player movement, circle/wall collision, ray/wall
 * intersection, occlusion and vertical span calculation. The host only:
 *   - supplies SDL window/input/timing,
 *   - runs pre-parsed Glon column blocks and one Glon tick block,
 *   - reads the bounded HOST_PRINT span stream,
 *   - rasterises those spans into a 320x200 framebuffer,
 *   - presents and measures the frame.
 *
 * The projection is sampled at 80 columns (4 framebuffer pixels each) for
 * interactive speed; the logical framebuffer is the requested 320x200.
 * This is not a texture/BSP renderer. */
#define _GNU_SOURCE
#include "r0_s1.h"
#include "m1_layout.h"
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define FB_W 320
#define FB_H 200
#define PROJ_COLS 160
#define PROJ_SCALE 2
#define TICK_HZ 60
#define MAX_CAPTURE (1u << 20)
#define RENDER_INTS (PROJ_COLS * 3)

typedef struct {
    int top[PROJ_COLS], bot[PROJ_COLS], color[PROJ_COLS];
    uint32_t hash;
} Spans;

typedef struct {
    double sim_ms, proj_ms, present_ms, frame_ms;
    long heap_high, gc_count, live_cells;
} Metrics;

static const char *root_dir = "../..";
static cell reset_block, sector_block, report_block, valid_block, collision_block;
static cell render_blocks[2];
static cell tick_blocks[64];

static long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000L + ts.tv_nsec;
}

static uint32_t fnv1a(const void *data, size_t n) {
    const unsigned char *p = data;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

static void fail(const char *what) {
    fprintf(stderr, "glon-doom: %s\n", what);
    exit(2);
}

/* ---- Glon loading / blocking ------------------------------------------- */

static char *read_source(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0 || n > (1L << 20)) { fclose(f); return NULL; }
    rewind(f);
    char *b = malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    b[n] = 0;
    *len = (size_t)n;
    return b;
}

static void strip_comments(char *s) {
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

static cell parse_source(const char *src, const char *label) {
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) {
        r0_s1_outcome oc;
        r0_s1_session_parse_error(&oc);
        fprintf(stderr, "glon-doom: %s: parse error (%s)\n", label,
                r0_s1_outcome_detail_name(oc.detail));
        exit(2);
    }
    return b;
}

static void load_file(const char *path) {
    size_t n;
    char *src = read_source(path, &n);
    if (!src) { fprintf(stderr, "glon-doom: cannot read %s\n", path); exit(2); }
    strip_comments(src);
    cell block = parse_source(src, path);
    free(src);
    r0_s1_outcome oc;
    r0_s1_session_run_block(block, &oc);
    if (oc.status != R0S1_OUT_OK) {
        fprintf(stderr, "glon-doom: %s: load %s (%s)\n", path,
                r0_s1_outcome_status_name(oc.status),
                r0_s1_outcome_detail_name(oc.detail));
        exit(2);
    }
}

static int run_block(cell block, const char *label) {
    r0_s1_outcome oc;
    r0_s1_session_run_block(block, &oc);
    if (oc.status != R0S1_OUT_OK) {
        fprintf(stderr, "glon-doom: %s: %s (%s)\n", label,
                r0_s1_outcome_status_name(oc.status),
                r0_s1_outcome_detail_name(oc.detail));
        return -1;
    }
    return 0;
}

typedef struct { FILE *saved, *stream; char *buf; size_t len; } Capture;

static void capture_begin(Capture *c) {
    memset(c, 0, sizeof *c);
    c->saved = stdout;
    c->stream = open_memstream(&c->buf, &c->len);
    if (!c->stream) fail("open_memstream failed");
    stdout = c->stream;
}
static void capture_end(Capture *c) {
    fflush(stdout);
    fclose(c->stream);
    stdout = c->saved;
}

static int parse_ints(const Capture *c, int *out, int expected, const char *label) {
    char *p = c->buf, *end;
    int n = 0;
    while (p && *p && n < expected) {
        long v = strtol(p, &end, 10);
        if (end == p) break;
        out[n++] = (int)v;
        p = end;
    }
    while (p && (*p == '\n' || *p == ' ' || *p == '\t')) p++;
    if (n != expected || (p && *p)) {
        fprintf(stderr, "glon-doom: %s: expected %d integers, got %d\n", label, expected, n);
        return -1;
    }
    return 0;
}

static int run_capture(cell block, int *out, int expected, const char *label) {
    Capture c;
    capture_begin(&c);
    int rc = run_block(block, label);
    capture_end(&c);
    if (rc != 0) { free(c.buf); return -1; }
    rc = parse_ints(&c, out, expected, label);
    free(c.buf);
    return rc;
}

static void init_runtime(void) {
    cell main_entry = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = main_entry;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
}

static void load_session(void) {
    char path[4096];
    init_runtime();
    snprintf(path, sizeof path, "%s/jupyter/prelude.glon", root_dir);
    load_file(path);
    snprintf(path, sizeof path, "%s/experiments/glon-doom/doom.glon", root_dir);
    load_file(path);
}

static void build_blocks(void) {
    reset_block = parse_source("[ doom/reset ]", "reset");
    sector_block = parse_source("[ doom/sector-report ]", "sector");
    report_block = parse_source("[ doom/report ]", "report");
    valid_block = parse_source("[ doom/valid ]", "valid");

    const char *collision_src =
        "[ "
        "doom/hit: 0 doom/check-all 300 147 400 print doom/hit "
        "doom/hit: 0 doom/check-all 749 200 400 print doom/hit "
        "doom/hit: 0 doom/check-all 900 275 400 print doom/hit "
        "doom/hit: 0 doom/check-all 1261 600 400 print doom/hit "
        "doom/hit: 0 doom/check-all 1200 1005 400 print doom/hit "
        "doom/hit: 0 doom/check-all 1171 700 400 print doom/hit "
        "doom/hit: 0 doom/check-all 900 403 400 print doom/hit "
        "doom/hit: 0 doom/check-all 787 500 400 print doom/hit "
        "doom/hit: 0 doom/check-all 300 621 400 print doom/hit "
        "doom/hit: 0 doom/check-all 147 300 400 print doom/hit "
        "doom/hit: 0 doom/check-all 500 500 400 print doom/hit "
        "]";
    collision_block = parse_source(collision_src, "collision probes");

    for (int m = 0; m < 64; m++) {
        char src[128];
        snprintf(src, sizeof src, "[ doom/tick %d %d %d %d %d %d ]",
                 !!(m & 1), !!(m & 2), !!(m & 4), !!(m & 8), !!(m & 16), !!(m & 32));
        tick_blocks[m] = parse_source(src, "tick");
    }

    for (int b = 0; b < 2; b++) {
        size_t cap = 256 + (PROJ_COLS / 2) * 64;
        char *src = malloc(cap);
        if (!src) fail("out of memory building render block");
        size_t used = 0;
        used += (size_t)snprintf(src + used, cap - used, "[ ");
        for (int i = b * (PROJ_COLS / 2); i < (b + 1) * (PROJ_COLS / 2); i++) {
            int sx = i * PROJ_SCALE + PROJ_SCALE / 2;
            used += (size_t)snprintf(src + used, cap - used,
                                     "doom/sx: %d do doom/render-column-block ", sx);
        }
        snprintf(src + used, cap - used, "]");
        render_blocks[b] = parse_source(src, "render");
        free(src);
    }
}

static int get_state(int *x, int *y, int *a) {
    int v[3];
    if (run_capture(report_block, v, 3, "report") != 0) return -1;
    *x = v[0]; *y = v[1]; *a = v[2];
    return 0;
}
static int get_sector(int *floor_c, int *ceil_c) {
    int v[2];
    if (run_capture(sector_block, v, 2, "sector") != 0) return -1;
    *floor_c = v[0]; *ceil_c = v[1];
    return 0;
}
static int get_valid(void) {
    int v;
    if (run_capture(valid_block, &v, 1, "valid") != 0) return -1;
    return v;
}
static int reset(void) { return run_block(reset_block, "reset"); }
static int tick(int mask) { return run_block(tick_blocks[mask & 63], "tick"); }

static int render_spans(Spans *s, long *ns) {
    int vals[RENDER_INTS];
    Capture c;
    long t0 = now_ns();
    capture_begin(&c);
    int rc = run_block(render_blocks[0], "render");
    if (rc == 0) rc = run_block(render_blocks[1], "render");
    capture_end(&c);
    long t1 = now_ns();
    if (ns) *ns = t1 - t0;
    if (rc != 0) { free(c.buf); return -1; }
    rc = parse_ints(&c, vals, RENDER_INTS, "render");
    if (rc == 0) {
        for (int i = 0; i < PROJ_COLS; i++) {
            s->top[i] = vals[i * 3];
            s->bot[i] = vals[i * 3 + 1];
            s->color[i] = vals[i * 3 + 2];
        }
        s->hash = fnv1a(vals, sizeof vals);
    }
    free(c.buf);
    return rc;
}

/* ---- Tests ------------------------------------------------------------- */

static int expect_near(const char *name, int got, int want, int tol) {
    if (got < want - tol || got > want + tol) {
        printf("FAIL %-24s got=%d want=%d+/-%d\n", name, got, want, tol);
        return 1;
    }
    printf("PASS %-24s got=%d\n", name, got);
    return 0;
}
static int expect_eq(const char *name, int got, int want) {
    return expect_near(name, got, want, 0);
}

static int run_ticks(int mask, int n) {
    for (int i = 0; i < n; i++) if (tick(mask) != 0) return -1;
    return 0;
}

static int self_test(void) {
    int fails = 0, x, y, a;
    load_session();
    build_blocks();
    int floor_c, ceil_c;
    if (get_sector(&floor_c, &ceil_c) != 0) return 2;
    if (reset() != 0) return 2;
    get_state(&x, &y, &a);
    fails += expect_eq("start x", x, 320);
    fails += expect_eq("start y", y, 320);
    fails += expect_eq("start angle", a, 0);

    reset(); run_ticks(1, 10); get_state(&x, &y, &a);
    fails += expect_eq("forward x", x, 400); fails += expect_eq("forward y", y, 320);
    reset(); run_ticks(2, 10); get_state(&x, &y, &a);
    fails += expect_eq("backward x", x, 240);
    reset(); run_ticks(8, 10); get_state(&x, &y, &a);
    fails += expect_eq("strafe right y", y, 240);
    reset(); run_ticks(4, 10); get_state(&x, &y, &a);
    fails += expect_eq("strafe left y", y, 400);
    reset(); run_ticks(16, 1); get_state(&x, &y, &a);
    fails += expect_eq("turn left angle", a, 1);
    run_ticks(32, 1); get_state(&x, &y, &a);
    fails += expect_eq("turn right angle", a, 0);

    reset(); run_ticks(2, 100); get_state(&x, &y, &a);
    fails += expect_near("west wall", x, 152, 8);
    fails += expect_eq("west wall valid", get_valid(), 1);
    reset(); run_ticks(8, 100); get_state(&x, &y, &a);
    fails += expect_near("south wall", y, 152, 8);
    reset(); run_ticks(4, 100); get_state(&x, &y, &a);
    fails += expect_near("north wall", y, 616, 8);
    reset(); run_ticks(4, 15); run_ticks(1, 100); get_state(&x, &y, &a);
    fails += expect_near("room east wall", x, 744, 8);
    fails += expect_eq("room east wall valid", get_valid(), 1);
    reset(); run_ticks(1, 200); get_state(&x, &y, &a);
    fails += expect_near("corridor end", x, 1256, 8);
    reset(); run_ticks(1, 110); run_ticks(16, 64); run_ticks(1, 100); get_state(&x, &y, &a);
    fails += expect_eq("corridor x", x, 1200);
    fails += expect_near("corridor north end", y, 1000, 8);
    fails += expect_eq("corridor angle", a, 64);
    fails += expect_eq("corridor valid", get_valid(), 1);
    reset(); run_ticks(4, 15); run_ticks(1, 60); run_ticks(8, 60); run_ticks(1, 40);
    get_state(&x, &y, &a);
    fails += expect_near("corner x", x, 744, 8);
    fails += expect_near("corner y", y, 152, 8);
    fails += expect_eq("corner valid", get_valid(), 1);

    int probes[11];
    if (run_capture(collision_block, probes, 11, "collision probes") != 0) return 2;
    for (int i = 0; i < 10; i++) {
        char name[32]; snprintf(name, sizeof name, "linedef %d collision", i);
        fails += expect_eq(name, probes[i], 1);
    }
    fails += expect_eq("clear space", probes[10], 0);

    Spans s;
    long proj_ns;
    if (reset() != 0 || render_spans(&s, &proj_ns) != 0) return 2;
    int bounds = 1;
    for (int i = 0; i < PROJ_COLS; i++)
        if (s.top[i] < 0 || s.bot[i] > FB_H - 1 || s.top[i] > s.bot[i] ||
            s.color[i] < 1 || s.color[i] > 3) bounds = 0;
    fails += expect_eq("framebuffer spans", bounds, 1);
    fails += expect_eq("centre wall visible", s.color[PROJ_COLS / 2], 3);
    fails += expect_eq("edge wall visible", s.color[0], 1);
    int edge_h = s.bot[0] - s.top[0], centre_h = s.bot[PROJ_COLS/2] - s.top[PROJ_COLS/2];
    fails += expect_eq("occlusion/projection hierarchy", edge_h > centre_h, 1);

    reset(); run_ticks(1, 37); run_ticks(16, 64); run_ticks(1, 80);
    get_state(&x, &y, &a); render_spans(&s, NULL);
    int x1=x, y1=y, a1=a; uint32_t h1=s.hash;
    reset(); run_ticks(1, 37); run_ticks(16, 64); run_ticks(1, 80);
    get_state(&x, &y, &a); render_spans(&s, NULL);
    fails += expect_eq("fixed-tick determinism", x1==x && y1==y && a1==a && h1==s.hash, 1);

    cell loader0 = M[GC_LOADER_HP];
    for (int i = 0; i < 20; i++) {
        if (render_spans(&s, NULL) != 0) return 2;
        if (run_ticks(i & 63, 1) != 0) return 2;
    }
    cell loader1 = M[GC_LOADER_HP];
    fails += expect_eq("repeated frames loader growth", loader1 - loader0, 0);
    printf("INFO floor/ceiling colours: %d/%d; loader cells: %ld\n",
           floor_c, ceil_c, (long)loader0);

    long session_loader[3];
    for (int k = 0; k < 3; k++) {
        load_session();
        session_loader[k] = M[GC_LOADER_HP];
    }
    fails += expect_eq("repeated session loader", session_loader[0] == session_loader[1] &&
                         session_loader[1] == session_loader[2], 1);

    printf("INFO managed heap high=%ld gc=%ld live=%ld\n",
           r0_s1_heap_high(), r0_s1_gc_count(), r0_s1_gc_live_cells());
    printf("self-test: %s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}

/* ---- Scripted trace ---------------------------------------------------- */

static int scripted_trace(void) {
    load_session();
    build_blocks();
    reset();
    int x, y, a;
    get_state(&x, &y, &a);
    printf("trace start: x=%d y=%d angle=%d\n", x, y, a);
    struct { int mask, n; const char *name; } steps[] = {
        {1, 110, "forward-to-turn"}, {16, 64, "turn-left-90"},
        {1, 100, "corridor-forward"}, {2, 20, "back"},
        {32, 64, "turn-right-90"}, {4, 15, "strafe-left"},
        {8, 15, "strafe-right"}
    };
    for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        if (run_ticks(steps[i].mask, steps[i].n) != 0) return 2;
        get_state(&x, &y, &a);
        printf("trace %-18s x=%d y=%d angle=%d valid=%d\n",
               steps[i].name, x, y, a, get_valid());
    }
    return 0;
}

/* ---- SDL presentation -------------------------------------------------- */

static uint32_t palette(int id) {
    switch (id) {
    case 1: return 0xff8b3636u;
    case 2: return 0xff676b73u;
    case 3: return 0xff3a5a8eu;
    case 6: return 0xff5b4a32u;
    case 7: return 0xff222630u;
    default: return 0xff101010u;
    }
}

static void draw_buffer(uint32_t *fb, const Spans *s, int floor_c, int ceil_c) {
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++)
            fb[y * FB_W + x] = (y < FB_H / 2) ? palette(ceil_c) : palette(floor_c);
    for (int i = 0; i < PROJ_COLS; i++) {
        int top = s->top[i], bot = s->bot[i];
        if (top < 0) top = 0;
        if (bot >= FB_H) bot = FB_H - 1;
        if (top > bot) continue;
        uint32_t c = palette(s->color[i]);
        for (int y = top; y <= bot; y++)
            for (int x = i * PROJ_SCALE; x < (i + 1) * PROJ_SCALE; x++)
                fb[y * FB_W + x] = c;
    }
}

static int sdl_present(SDL_Renderer *ren, SDL_Texture *tex, uint32_t *fb,
                       const Spans *s, int floor_c, int ceil_c, double *present_ms) {
    draw_buffer(fb, s, floor_c, ceil_c);
    long t0 = now_ns();
    if (SDL_UpdateTexture(tex, NULL, fb, FB_W * (int)sizeof(uint32_t)) != 0) return -1;
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
    long t1 = now_ns();
    if (present_ms) *present_ms = (double)(t1 - t0) / 1e6;
    return 0;
}

static void print_metrics(const Metrics *m) {
    printf("metrics: sim=%.2fms proj=%.2fms present=%.2fms frame=%.2fms fps=%.1f "
           "heap_high=%ld gc=%ld live=%ld\n",
           m->sim_ms, m->proj_ms, m->present_ms, m->frame_ms,
           m->frame_ms > 0.0 ? 1000.0 / m->frame_ms : 0.0,
           m->heap_high, m->gc_count, m->live_cells);
}

static int bench(int frames) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 2;
    SDL_Window *win = SDL_CreateWindow("Glon Doom G5a - benchmark",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       960, 600, SDL_WINDOW_SHOWN);
    if (!win) fail(SDL_GetError());
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) fail(SDL_GetError());
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H);
    if (!tex) fail(SDL_GetError());
    uint32_t *fb = malloc((size_t)FB_W * FB_H * sizeof *fb);
    if (!fb) fail("out of memory");

    load_session();
    build_blocks();
    int floor_c, ceil_c;
    if (get_sector(&floor_c, &ceil_c) != 0) return 2;
    Spans s;
    Metrics sum = {0};
    long wall0 = now_ns();
    for (int i = 0; i < frames; i++) {
        long t0 = now_ns();
        if (tick(i & 63) != 0) return 2;
        long t1 = now_ns();
        long proj_ns;
        if (render_spans(&s, &proj_ns) != 0) return 2;
        long t2 = now_ns();
        double present_ms;
        if (sdl_present(ren, tex, fb, &s, floor_c, ceil_c, &present_ms) != 0) return 2;
        long t3 = now_ns();
        sum.sim_ms += (double)(t1 - t0) / 1e6;
        sum.proj_ms += (double)(t2 - t1) / 1e6;
        sum.present_ms += present_ms;
        sum.frame_ms += (double)(t3 - t0) / 1e6;
        SDL_PumpEvents();
        SDL_Event e;
        while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT) { i = frames; break; }
    }
    long wall1 = now_ns();
    sum.sim_ms /= frames; sum.proj_ms /= frames; sum.present_ms /= frames;
    sum.frame_ms /= frames;
    sum.heap_high = r0_s1_heap_high();
    sum.gc_count = r0_s1_gc_count();
    sum.live_cells = r0_s1_gc_live_cells();
    printf("bench frames=%d wall_fps=%.1f\n", frames,
           frames * 1e9 / (double)(wall1 - wall0));
    print_metrics(&sum);
    free(fb);
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

static int interactive(void) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 2;
    SDL_Window *win = SDL_CreateWindow("Glon Doom G5a - one room, one corridor",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       960, 600, SDL_WINDOW_SHOWN);
    if (!win) fail(SDL_GetError());
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) fail(SDL_GetError());
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H);
    if (!tex) fail(SDL_GetError());
    uint32_t *fb = malloc((size_t)FB_W * FB_H * sizeof *fb);
    if (!fb) fail("out of memory");

    load_session();
    build_blocks();
    int floor_c, ceil_c;
    if (get_sector(&floor_c, &ceil_c) != 0) return 2;
    Spans s;
    int running = 1, frames = 0;
    double acc = 0.0, fps_start = (double)now_ns(), sim_ms = 0, proj_ms = 0, present_ms = 0;
    long last = now_ns();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_QUIT ||
                (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) running = 0;
        const Uint8 *k = SDL_GetKeyboardState(NULL);
        int mask = (k[SDL_SCANCODE_W] ? 1 : 0) | (k[SDL_SCANCODE_S] ? 2 : 0) |
                   (k[SDL_SCANCODE_A] ? 4 : 0) | (k[SDL_SCANCODE_D] ? 8 : 0) |
                   (k[SDL_SCANCODE_LEFT] ? 16 : 0) | (k[SDL_SCANCODE_RIGHT] ? 32 : 0);
        long tn = now_ns();
        double dt = (double)(tn - last) / 1e9;
        last = tn;
        if (dt > 0.10) dt = 0.10;
        acc += dt;
        long ts = now_ns();
        int ticks = 0;
        while (acc >= 1.0 / TICK_HZ && ticks < 10) {
            if (tick(mask) != 0) { running = 0; break; }
            acc -= 1.0 / TICK_HZ;
            ticks++;
        }
        long te = now_ns();
        sim_ms += (double)(te - ts) / 1e6;
        long proj_ns;
        if (render_spans(&s, &proj_ns) != 0) break;
        proj_ms += (double)proj_ns / 1e6;
        double pm;
        if (sdl_present(ren, tex, fb, &s, floor_c, ceil_c, &pm) != 0) break;
        present_ms += pm;
        frames++;
        if (frames % 120 == 0) {
            double secs = ((double)now_ns() - fps_start) / 1e9;
            Metrics m = { sim_ms / 120, proj_ms / 120, present_ms / 120,
                          secs * 1000.0 / 120, r0_s1_heap_high(),
                          r0_s1_gc_count(), r0_s1_gc_live_cells() };
            printf("frame %d: ", frames); print_metrics(&m);
            fflush(stdout);
            sim_ms = proj_ms = present_ms = 0;
            fps_start = (double)now_ns();
        }
    }
    free(fb);
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

int main(int argc, char **argv) {
    const char *env = getenv("DOOM_ROOT");
    if (env && *env) root_dir = env;
    if (argc > 1 && strcmp(argv[1], "--self-test") == 0) return self_test();
    if (argc > 1 && strcmp(argv[1], "--trace") == 0) return scripted_trace();
    if (argc > 2 && strcmp(argv[1], "--bench") == 0) return bench(atoi(argv[2]));
    return interactive();
}
