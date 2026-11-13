/* Glon AVP G5f - native SDL2 host for the Marine/Alien corridor.
 *
 * Glon owns the world data, player movement, circle/wall collision, ray/wall
 * intersection, occlusion, door state, Alien state/ray visibility, tracker
 * values and vertical span calculation. The host only:
 *   - supplies SDL window/input/timing,
 *   - runs pre-parsed Glon column blocks and one Glon tick block,
 *   - reads the bounded HOST_PRINT span stream,
 *   - rasterises those spans into a 320x200 framebuffer,
 *   - presents and measures the frame.
 *
 * The projection is sampled at 160 columns (2 framebuffer pixels each) for
 * interactive speed; the logical framebuffer remains 320x200.
 * This is not a texture/BSP renderer. */
#define _GNU_SOURCE
#include "r0_s1.h"
#include "m1_layout.h"
#include "textures.h"
#include "alien_sprites.h"
#include "g5j_textures.h"
#include "object_sprites.h"
#ifdef DOOM_S1_PROFILE
#include "s1_prof.h"
#endif
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <SDL2/SDL_opengl.h>
#include <unistd.h>

#define FB_W 320
#define FB_H 200
#define PROJ_COLS 160
#define PROJ_SCALE 2
#define TICK_HZ 60
#define MAX_CAPTURE (1u << 20)
#define RENDER_INTS (PROJ_COLS * 5)
#define DETAIL_MAX 64

typedef struct {
    int top[PROJ_COLS], bot[PROJ_COLS], color[PROJ_COLS];
    int u[PROJ_COLS], shade[PROJ_COLS], material[PROJ_COLS];
    uint32_t hash;
} Spans;

typedef struct { int visible, x1, y1, x2, y2, color; } DetailLine;
typedef struct { int n; DetailLine line[DETAIL_MAX]; uint32_t hash; } Detail;

typedef struct {
    int valid[FB_H];
    int bx[FB_H], by[FB_H], sx[FB_H], sy[FB_H], shade[FB_H];
    uint32_t hash;
} TexRows;

typedef struct {
    int door_open, door_blocked, door_target;
    int ax, ay, health, state, detected;
    int td2, tside, tdepth;
    int visible, left, right, top, bot;
    int light;
} Status;

typedef struct { int id, depth, left, right, top, bot, visible; int x,y,hw,hd,h; } Obj;
typedef struct { int depth, l, r, t, b, pct; const uint32_t *src; int sw, sh; } SpriteDraw;
typedef struct {
    double sim_ms, proj_ms, texrow_ms, raster_ms, present_ms, frame_ms;
    double sprite_ms;
    long heap_high, gc_count, live_cells;
} Metrics;

typedef struct {
    int px, py, angle, simple, floor_c, ceil_c;
    Status st;
} ViewSig;

typedef struct {
    int valid;
    ViewSig sig;
    Spans s;
    TexRows tr;
} ViewCache;

#ifdef DOOM_S1_PROFILE
typedef struct {
    long op[8], host[16];
    long branches_taken;
    long hist[256];
    unsigned long long cycles;
} ProfAccum;
static int prof_on;
static ProfAccum prof_wall, prof_tex, prof_status, prof_tick;
static void prof_begin(void) { if (prof_on) s1_prof_reset(); }
static void prof_collect(ProfAccum *a) {
    if (!prof_on) return;
    for (int i = 0; i < 8; i++) a->op[i] += s1_prof_op_count[i];
    for (int i = 0; i < 16; i++) a->host[i] += s1_prof_host_count[i];
    a->branches_taken += s1_prof_branch_taken;
    for (int i = 0; i < 256; i++) a->hist[i] += s1_prof_ip_hist[i];
    a->cycles += s1_prof_run_cycles;
}
#endif

static const char *root_dir = "../..";
static int simple_mode = 0;
static int stationary_enabled = 0;
static int raw_wall_mode = 0;
static int checker_walls = 0;
static int linear_filter = 0;
static int new_textures = 0;
static int smooth_shading = 0;
static int flat_surfaces = 0;
static int objects_mode = 0;
static Obj current_objs[6];
static int current_objs_valid = 0;
static int last_px = 320;
static int last_py = 320;
static double st_current_angle = 0;
static int get_objects(Obj out[6]);
static int alien_cover = 0;
static int cover_view = 0;
static int need_texrows(void) { return !simple_mode && !flat_surfaces; }
static uint32_t sim_tick = 0;
static double last_sprite_ms = 0.0;
static cell reset_block, sector_block, report_block, valid_block, collision_block;
static cell activate_block, status_block, detail_block, texrows_block;
static cell floor_line_block, ceil_line_block, objects_block, objects_on_block;
static cell alien_collision_block, los_wall_block, los_clear_block;
static cell render_blocks[2];
static cell tick_blocks[64];

static void draw_buffer(uint32_t *fb, const Spans *s, const Detail *d,
                        const TexRows *tr, const Status *st,
                        int floor_c, int ceil_c);
static int wall_tex_v(int y, int top, int bot);
static int alien_frame_index(const Status *st);
static void draw_alien_sprite(uint32_t *fb, const Status *st);

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
        fprintf(stderr, "halt_reason=%d spmin=%ld rpmin=%ld code=%ld\n",
                r0_s1_halt_reason(), (long)r0_s1_sp_min(), (long)r0_s1_rp_min(),
                r0_s1_code_size());
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
        fprintf(stderr, "halt_reason=%d spmin=%ld rpmin=%ld code=%ld\n",
                r0_s1_halt_reason(), (long)r0_s1_sp_min(), (long)r0_s1_rp_min(),
                r0_s1_code_size());
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
    if (objects_mode) {
        snprintf(path, sizeof path, "%s/experiments/glon-doom/objects.glon", root_dir);
        load_file(path);
    }
    if (raw_wall_mode) {
        snprintf(path, sizeof path, "%s/experiments/glon-doom/raw-wall.glon", root_dir);
        load_file(path);
    }
}

static void build_blocks(void) {
    reset_block = parse_source("[ doom/reset ]", "reset");
    sector_block = parse_source("[ doom/sector-report ]", "sector");
    report_block = parse_source("[ doom/report ]", "report");
    valid_block = parse_source("[ doom/valid ]", "valid");
    activate_block = parse_source("[ doom/activate ]", "activate");
    status_block = parse_source("[ do doom/status-block ]", "status");
    objects_block = parse_source("[ do doom/object-report-block ]", "objects");
    objects_on_block = parse_source("[ doom/objects-on: 1 ]", "objects on");
    texrows_block = parse_source("[ do doom/texrows-block ]", "texrows");
    if (!objects_mode) {
    floor_line_block = parse_source("[ doom/detail-line 768 256 768 384 0 5 ]", "floor wall base");
    ceil_line_block = parse_source("[ doom/detail-line 768 256 768 384 128 5 ]", "ceiling wall top");

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

    const char *alien_probe_src =
        "[ "
        "doom/hit: 0 doom/check-all 300 143 256 print doom/hit "
        "doom/hit: 0 doom/check-all 753 200 256 print doom/hit "
        "doom/hit: 0 doom/check-all 900 271 256 print doom/hit "
        "doom/hit: 0 doom/check-all 1265 600 256 print doom/hit "
        "doom/hit: 0 doom/check-all 1200 1009 256 print doom/hit "
        "doom/hit: 0 doom/check-all 1167 700 256 print doom/hit "
        "doom/hit: 0 doom/check-all 900 399 256 print doom/hit "
        "doom/hit: 0 doom/check-all 783 500 256 print doom/hit "
        "doom/hit: 0 doom/check-all 300 625 256 print doom/hit "
        "doom/hit: 0 doom/check-all 143 300 256 print doom/hit "
        "doom/hit: 0 doom/check-all 500 500 256 print doom/hit "
        "]";
    alien_collision_block = parse_source(alien_probe_src, "alien collision probes");
    los_wall_block = parse_source(
        "[ doom/px: 320 doom/py: 440 doom/door-blocked: 0 doom/los 1200 440 print doom/occluded ]",
        "wall los");
    los_clear_block = parse_source(
        "[ doom/px: 320 doom/py: 320 doom/door-blocked: 0 doom/los 1120 320 print doom/occluded ]",
        "clear los");
    }

    int tick_count = objects_mode ? 16 : 64;
    for (int m = 0; m < tick_count; m++) {
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
                                     "doom/sx: %d do %s ", sx,
                                     raw_wall_mode ? "doom/raw-column-block"
                                                   : "doom/render-column-block");
        }
        snprintf(src + used, cap - used, "]");
        render_blocks[b] = parse_source(src, "render");
        free(src);
    }
    if (objects_mode) {
        if (run_block(objects_on_block, "objects on") != 0) fail("objects enable failed");
        if (alien_cover) {
            cell b = parse_source("[ doom/alien-x: 920 doom/alien-y: 270 doom/alien-state: 1 doom/alien-detected: 1 ]", "alien cover");
            if (run_block(b, "alien cover") != 0) fail("alien cover failed");
        }
    }
}

static int get_state(int *x, int *y, int *a) {
    int v[3];
    if (run_capture(report_block, v, 3, "report") != 0) return -1;
    *x = v[0]; *y = v[1]; *a = v[2]; last_px = *x; last_py = *y;
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
static int get_status(Status *st) {
    int v[17];
#ifdef DOOM_S1_PROFILE
    prof_begin();
#endif
    int grc = run_capture(status_block, v, 17, "status");
#ifdef DOOM_S1_PROFILE
    prof_collect(&prof_status);
#endif
    if (grc != 0) return -1;
    st->door_open = v[0]; st->door_blocked = v[1]; st->door_target = v[2];
    st->ax = v[3]; st->ay = v[4]; st->health = v[5]; st->state = v[6];
    st->detected = v[7]; st->td2 = v[8]; st->tside = v[9]; st->tdepth = v[10];
    st->visible = v[11]; st->left = v[12]; st->right = v[13];
    st->top = v[14]; st->bot = v[15]; st->light = v[16];
    if (objects_mode && get_objects(current_objs) == 0) {
        current_objs_valid = 1;
        if (st->door_blocked && last_px < 768)
            for (int i = 0; i < 6; i++) current_objs[i].visible = 0;
    }
    return 0;
}
static int get_objects(Obj out[6]) {
    int v[72];
    if (run_capture(objects_block, v, 72, "objects") != 0) return -1;
    for (int i = 0; i < 6; i++) {
        int *p = v + i*12;
        out[i].id=p[0]; out[i].depth=p[1]; out[i].left=p[2]; out[i].right=p[3];
        out[i].top=p[4]; out[i].bot=p[5]; out[i].visible=p[6];
        out[i].x=p[7]; out[i].y=p[8]; out[i].hw=p[9]; out[i].hd=p[10]; out[i].h=p[11];
    }
    return 0;
}

static int get_detail(Detail *d) {
    Capture c;
    capture_begin(&c);
    int rc = run_block(detail_block, "detail");
    capture_end(&c);
    if (rc != 0) { free(c.buf); return -1; }
    char *end;
    long n = strtol(c.buf, &end, 10);
    if (end == c.buf || n < 0 || n > DETAIL_MAX) { free(c.buf); return -1; }
    int vals[1 + DETAIL_MAX * 6];
    if (parse_ints(&c, vals, 1 + (int)n * 6, "detail") != 0) { free(c.buf); return -1; }
    d->n = (int)n;
    for (int i = 0; i < d->n; i++) {
        d->line[i].visible = vals[1 + i * 6];
        d->line[i].x1 = vals[2 + i * 6];
        d->line[i].y1 = vals[3 + i * 6];
        d->line[i].x2 = vals[4 + i * 6];
        d->line[i].y2 = vals[5 + i * 6];
        d->line[i].color = vals[6 + i * 6];
    }
    d->hash = fnv1a(&vals[1], (size_t)n * 6 * sizeof(int));
    free(c.buf);
    return 0;
}

static int get_texrows(TexRows *r) {
    Capture c;
    capture_begin(&c);
#ifdef DOOM_S1_PROFILE
    prof_begin();
#endif
    int rc = run_block(texrows_block, "texrows");
#ifdef DOOM_S1_PROFILE
    prof_collect(&prof_tex);
#endif
    capture_end(&c);
    if (rc != 0) { free(c.buf); return -1; }
    char *end;
    long n = strtol(c.buf, &end, 10);
    if (end == c.buf || n != FB_H) { free(c.buf); return -1; }
    int vals[1 + FB_H * 7];
    if (parse_ints(&c, vals, 1 + (int)n * 7, "texrows") != 0) { free(c.buf); return -1; }
    for (int y = 0; y < FB_H; y++) {
        r->valid[y] = 0; r->bx[y] = r->by[y] = r->sx[y] = r->sy[y] = 0;
        r->shade[y] = 0;
    }
    for (int i = 0; i < n; i++) {
        int y = vals[1 + i * 7];
        if (y < 0) { free(c.buf); return -1; }
        if (y >= FB_H) continue;
        r->valid[y] = vals[2 + i * 7];
        r->bx[y] = vals[3 + i * 7];
        r->by[y] = vals[4 + i * 7];
        r->sx[y] = vals[5 + i * 7];
        r->sy[y] = vals[6 + i * 7];
        r->shade[y] = vals[7 + i * 7];
    }
    r->hash = fnv1a(&vals[1], (size_t)n * 7 * sizeof(int));
    free(c.buf);
    return 0;
}

static int reset(void) { return run_block(reset_block, "reset"); }
static int tick(int mask) {
    if (objects_mode) mask &= 15;
#ifdef DOOM_S1_PROFILE
    prof_begin();
#endif
    int rc = run_block(tick_blocks[mask & 63], "tick");
#ifdef DOOM_S1_PROFILE
    prof_collect(&prof_tick);
#endif
    if (rc == 0) sim_tick++;
    return rc;
}

static int render_spans(Spans *s, long *ns) {
    int vals[RENDER_INTS];
    Capture c;
    long t0 = now_ns();
    capture_begin(&c);
#ifdef DOOM_S1_PROFILE
    prof_begin();
#endif
    int rc = run_block(render_blocks[0], "render");
#ifdef DOOM_S1_PROFILE
    if (rc == 0) prof_collect(&prof_wall);
    if (rc == 0) prof_begin();
#endif
    if (rc == 0) rc = run_block(render_blocks[1], "render");
#ifdef DOOM_S1_PROFILE
    if (rc == 0) prof_collect(&prof_wall);
#endif
    capture_end(&c);
    long t1 = now_ns();
    if (ns) *ns = t1 - t0;
    if (rc != 0) { free(c.buf); return -1; }
    rc = parse_ints(&c, vals, RENDER_INTS, "render");
    if (rc == 0) {
        for (int i = 0; i < PROJ_COLS; i++) {
            s->top[i] = vals[i * 5];
            s->bot[i] = vals[i * 5 + 1];
            s->color[i] = vals[i * 5 + 2];
            s->u[i] = vals[i * 5 + 3];
            int meta = vals[i * 5 + 4];
            s->shade[i] = meta & 3;
            s->material[i] = meta >> 2;
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

static int approach_closed_door(void) {
    if (reset() != 0) return -1;
    return run_ticks(1, 60);
}
static int open_door_near(void) {
    if (approach_closed_door() != 0) return -1;
    if (run_block(activate_block, "activate") != 0) return -1;
    return run_ticks(0, 12);
}

static int self_test(void) {
    int fails = 0, x, y, a;
    Status st;
    fails += expect_eq("framebuffer width", FB_W, 320);
    fails += expect_eq("framebuffer height", FB_H, 200);
    fails += expect_eq("wall ray count", PROJ_COLS, 160);
    fails += expect_eq("pixels per wall ray", PROJ_SCALE, 2);
    load_session();
    build_blocks();
    int floor_c, ceil_c;
    if (get_sector(&floor_c, &ceil_c) != 0) return 2;

    if (reset() != 0 || get_status(&st) != 0) return 2;
    fails += expect_eq("start x", (get_state(&x,&y,&a), x), 320);
    fails += expect_eq("start y", y, 320);
    fails += expect_eq("start angle", a, 0);
    fails += expect_eq("door initially closed progress", st.door_open, 0);
    fails += expect_eq("door initially blocked", st.door_blocked, 1);
    fails += expect_eq("door initially target", st.door_target, 0);
    fails += expect_eq("alien initial x", st.ax, 1120);
    fails += expect_eq("alien initial y", st.ay, 320);
    fails += expect_eq("alien initial health", st.health, 100);
    fails += expect_eq("alien initial state", st.state, 0);
    fails += expect_eq("alien initially undetected", st.detected, 0);

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
    reset(); run_ticks(4, 15); run_ticks(1, 60); run_ticks(8, 60); run_ticks(1, 40);
    get_state(&x, &y, &a);
    fails += expect_near("corner x", x, 744, 8);
    fails += expect_near("corner y", y, 152, 8);
    fails += expect_eq("corner valid", get_valid(), 1);

    /* Closed door blocks the player before the corridor opening. */
    reset(); run_ticks(1, 100); get_state(&x, &y, &a);
    fails += expect_near("closed door collision", x, 744, 8);
    fails += expect_eq("closed door player valid", get_valid(), 1);
    if (get_status(&st) != 0) return 2;
    fails += expect_eq("closed door status", st.door_blocked, 1);

    /* Activating far from the door does nothing. */
    reset(); run_block(activate_block, "activate"); get_status(&st);
    fails += expect_eq("far activation target", st.door_target, 0);

    /* Open the door and move through it. */
    if (open_door_near() != 0) return 2;
    get_status(&st);
    fails += expect_eq("door open progress", st.door_open, 48);
    fails += expect_eq("door open target", st.door_target, 64);
    fails += expect_eq("door open unblocked", st.door_blocked, 0);
    run_ticks(1, 20); get_state(&x, &y, &a);
    fails += expect_near("through open door x", x, 904, 8);
    fails += expect_eq("through open door valid", get_valid(), 1);

    /* Preserved G5a corridor traversal, now through the open door. */
    if (open_door_near() != 0) return 2;
    run_ticks(1, 100); get_state(&x, &y, &a);
    fails += expect_near("corridor end", x, 1256, 8);
    if (open_door_near() != 0) return 2;
    run_ticks(1, 60); run_ticks(16, 64); run_ticks(1, 100);
    get_state(&x, &y, &a);
    fails += expect_eq("corridor traversal x > door", x > 768, 1);
    fails += expect_near("corridor north end y", y, 1000, 8);
    fails += expect_eq("corridor angle", a, 64);
    fails += expect_eq("corridor valid", get_valid(), 1);

    /* Close the door again. */
    if (approach_closed_door() != 0) return 2;
    run_block(activate_block, "activate");
    run_ticks(0, 12);
    run_block(activate_block, "activate");
    run_ticks(0, 12);
    get_status(&st);
    fails += expect_eq("door closed target", st.door_target, 0);
    fails += expect_eq("door closed blocked", st.door_blocked, 1);

    /* Door visibility is consistent with collision state. */
    Spans s;
    long proj_ns;
    reset(); if (render_spans(&s, &proj_ns) != 0) return 2;
    fails += expect_eq("closed door centre colour", s.color[PROJ_COLS / 2], 4);
    fails += expect_eq("closed door hides alien", (get_status(&st), st.visible), 0);
    if (open_door_near() != 0 || render_spans(&s, &proj_ns) != 0) return 2;
    get_status(&st);
    fails += expect_eq("open door centre colour", s.color[PROJ_COLS / 2], 3);
    fails += expect_eq("open door shows alien sprite", st.visible, 1);
    int edge_h = s.bot[0] - s.top[0], centre_h = s.bot[PROJ_COLS/2] - s.top[PROJ_COLS/2];
    fails += expect_eq("open wall occlusion hierarchy", edge_h > centre_h, 1);
    fails += expect_eq("alien sprite rect bounds",
                       st.left < st.right && st.top < st.bot, 1);
    {
        Status fs = st;
        sim_tick = 0;
        fs.state = 0; fails += expect_eq("alien idle frame", alien_frame_index(&fs), 0);
        fs.state = 1; sim_tick = 0; fails += expect_eq("alien walk frame A", alien_frame_index(&fs), 1);
        sim_tick = 8; fails += expect_eq("alien walk frame B", alien_frame_index(&fs), 2);
        fs.state = 2; fails += expect_eq("alien attack frame", alien_frame_index(&fs), 3);
        uint32_t *tfb = malloc((size_t)FB_W * FB_H * sizeof *tfb);
        for (int i = 0; i < FB_W * FB_H; i++) tfb[i] = 0xff112233u;
        fs.visible = 1; fs.left = 100; fs.right = 180; fs.top = 30; fs.bot = 150;
        draw_alien_sprite(tfb, &fs);
        fails += expect_eq("alien sprite transparent corner", tfb[0], 0xff112233u);
        int changed = 0;
        for (int i = 0; i < FB_W * FB_H; i++) if (tfb[i] != 0xff112233u) changed++;
        fails += expect_eq("alien sprite draws pixels", changed > 0, 1);
        free(tfb);
    }

    /* Alien detection/movement: it approaches but respects the closed door. */
    if (approach_closed_door() != 0) return 2;
    get_status(&st);
    fails += expect_eq("alien detected near door", st.detected, 1);
    fails += expect_eq("alien detects in range state", st.state >= 1, 1);
    int ax_before = st.ax;
    run_ticks(0, 100);
    get_status(&st);
    fails += expect_eq("alien waits behind closed door", st.ax, ax_before);
    fails += expect_eq("alien blocked state not attack", st.state, 1);

    /* After opening, the Alien can approach and reach attack range. */
    if (open_door_near() != 0) return 2;
    get_status(&st);
    fails += expect_eq("open door reveals alien", st.visible, 1);
    int ax_open = st.ax;
    run_ticks(0, 60);
    get_status(&st);
    fails += expect_eq("alien approaches after opening", st.ax < ax_open, 1);
    run_ticks(0, 120);
    get_status(&st);
    fails += expect_eq("alien proximity attack state", st.state, 2);
    fails += expect_eq("alien proximity within attack range", st.td2 <= 9216, 1);
    fails += expect_eq("alien/player valid after proximity", get_valid(), 1);

    /* Tracker direction is non-zero when the Alien is off-axis. */
    if (open_door_near() != 0) return 2;
    run_ticks(4, 10); /* strafe north while facing east */
    get_status(&st);
    fails += expect_eq("tracker detected", st.detected, 1);
    fails += expect_eq("tracker direction non-zero", st.tside != 0, 1);
    fails += expect_eq("tracker distance non-zero", st.td2 > 0, 1);

    int probes[11];
    if (reset() != 0 || run_capture(collision_block, probes, 11, "collision probes") != 0) return 2;
    for (int i = 0; i < 10; i++) {
        char name[32]; snprintf(name, sizeof name, "linedef %d collision", i);
        fails += expect_eq(name, probes[i], 1);
    }
    fails += expect_eq("clear space", probes[10], 0);

    int aprobes[11];
    if (run_capture(alien_collision_block, aprobes, 11, "alien collision probes") != 0) return 2;
    for (int i = 0; i < 10; i++) {
        char name[40]; snprintf(name, sizeof name, "alien linedef %d collision", i);
        fails += expect_eq(name, aprobes[i], 1);
    }
    fails += expect_eq("alien clear space", aprobes[10], 0);

    int los;
    if (run_capture(los_wall_block, &los, 1, "wall los") != 0) return 2;
    fails += expect_eq("alien wall occlusion", los, 1);
    if (run_capture(los_clear_block, &los, 1, "clear los") != 0) return 2;
    fails += expect_eq("alien clear line of sight", los, 0);
    reset();

    Spans sa, sb, sf;
    reset();
    if (render_spans(&sa, &proj_ns) != 0) return 2;
    reset();
    if (render_spans(&sb, NULL) != 0) return 2;
    int bounds = 1;
    for (int i = 0; i < PROJ_COLS; i++)
        if (sa.top[i] < 0 || sa.bot[i] > FB_H - 1 || sa.top[i] > sa.bot[i] ||
            sa.color[i] < 1 || sa.color[i] > 4 ||
            sa.shade[i] < 0 || sa.shade[i] > 3 ||
            sa.u[i] < 0 || sa.u[i] > 2048) bounds = 0;
    fails += expect_eq("framebuffer spans", bounds, 1);
    fails += expect_eq("wall panel world anchor", sa.u[PROJ_COLS / 2], 320);
    fails += expect_eq("wall panel u not screen x", sa.u[PROJ_COLS / 2] != 160, 1);
    fails += expect_eq("wall shade far/near level", sa.shade[PROJ_COLS / 2], 2);
    fails += expect_eq("edge wall visible", sa.color[0], 1);
    fails += expect_eq("door material selected", sa.material[PROJ_COLS / 2], 5);
    int material_ok = 1;
    for (int i = 0; i < PROJ_COLS; i++)
        if (sa.material[i] < 0 || sa.material[i] > 5) material_ok = 0;
    fails += expect_eq("wall material bounds", material_ok, 1);
    fails += expect_eq("wall material deterministic",
                       fnv1a(sa.material, sizeof sa.material) ==
                       fnv1a(sb.material, sizeof sb.material), 1);
    fails += expect_eq("identical frame wall hash", sa.hash == sb.hash, 1);
    fails += expect_eq("deterministic shading", sa.shade[0] == sb.shade[0] &&
                       sa.shade[PROJ_COLS / 2] == sb.shade[PROJ_COLS / 2], 1);
    fails += expect_eq("panel u spans world surface", sa.u[0] != sa.u[PROJ_COLS - 1], 1);

    /* Translation changes floor projections but not the centre door's world u. */
    reset(); run_ticks(1, 10);
    if (render_spans(&sf, NULL) != 0) return 2;
    fails += expect_eq("anchored door panel u under translation", sf.u[PROJ_COLS / 2], 320);
    fails += expect_eq("nearer door shade increases", sf.shade[PROJ_COLS / 2] >= sa.shade[PROJ_COLS / 2], 1);

    /* Turning changes projected ceiling detail. */
    reset(); run_ticks(16, 1);
    if (render_spans(&sf, NULL) != 0) return 2;
    reset(); run_ticks(1, 30);
    if (render_spans(&sf, NULL) != 0) return 2;
    fails += expect_eq("distance shading threshold", sf.shade[PROJ_COLS / 2], 3);
    fails += expect_eq("near shade brighter", sf.shade[PROJ_COLS / 2] > sa.shade[PROJ_COLS / 2], 1);

    /* Texture coordinate and framebuffer checks. */
    TexRows tr0, tr1, tr2;
    reset();
    if (render_spans(&sa, NULL) != 0 || get_texrows(&tr0) != 0 ||
        get_texrows(&tr1) != 0 || get_status(&st) != 0) return 2;
    fails += expect_eq("texture row count/valid", tr0.valid[99] && !tr0.valid[100] &&
                       tr0.valid[101], 1);
    fails += expect_eq("texture rows deterministic", tr0.hash == tr1.hash, 1);
    int row_shade_ok = 1;
    for (int y = 0; y < FB_H; y++)
        if (tr0.shade[y] < 0 || tr0.shade[y] > 3) row_shade_ok = 0;
    fails += expect_eq("texture row shade bounds", row_shade_ok, 1);
    fails += expect_eq("wall texture v top", wall_tex_v(10, 10, 20), 0);
    fails += expect_eq("wall texture v bottom", wall_tex_v(20, 10, 20), 63);
    fails += expect_eq("wall texture v midpoint", wall_tex_v(15, 10, 20), 32);

    int wall_base = sa.bot[PROJ_COLS / 2];
    int wall_top = sa.top[PROJ_COLS / 2];
    int centre_sx = (PROJ_COLS / 2) * PROJ_SCALE + PROJ_SCALE - 1;
    int64_t fwx = (int64_t)tr0.bx[wall_base] + (int64_t)tr0.sx[wall_base] * centre_sx;
    int64_t fwy = (int64_t)tr0.by[wall_base] + (int64_t)tr0.sy[wall_base] * centre_sx;
    int64_t cwx = (int64_t)tr0.bx[wall_top] + (int64_t)tr0.sx[wall_top] * centre_sx;
    int64_t cwy = (int64_t)tr0.by[wall_top] + (int64_t)tr0.sy[wall_top] * centre_sx;
    fails += expect_near("floor/wall base world x", (int)(fwx >> 16), 768, 24);
    fails += expect_near("floor/wall base world y", (int)(fwy >> 16), 320, 24);
    fails += expect_near("ceiling/wall top world x", (int)(cwx >> 16), 768, 24);
    fails += expect_near("ceiling/wall top world y", (int)(cwy >> 16), 320, 24);

    reset(); run_ticks(1, 10); get_texrows(&tr2);
    fails += expect_eq("floor/ceiling rows move with translation", tr2.hash != tr0.hash, 1);
    reset(); run_ticks(16, 1); get_texrows(&tr2);
    fails += expect_eq("floor/ceiling rows respond to turn", tr2.hash != tr0.hash, 1);

    /* Native textured rasterisation hashes. */
    uint32_t *fb = malloc((size_t)FB_W * FB_H * sizeof *fb);
    if (!fb) return 2;
    reset();
    if (render_spans(&sa, NULL) != 0 || get_texrows(&tr0) != 0 ||
        get_status(&st) != 0) { free(fb); return 2; }
    draw_buffer(fb, &sa, NULL, &tr0, &st, floor_c, ceil_c);
    uint32_t fh1 = fnv1a(fb, (size_t)FB_W * FB_H * sizeof *fb);
    reset();
    if (render_spans(&sa, NULL) != 0 || get_texrows(&tr0) != 0 ||
        get_status(&st) != 0) { free(fb); return 2; }
    draw_buffer(fb, &sa, NULL, &tr0, &st, floor_c, ceil_c);
    uint32_t fh2 = fnv1a(fb, (size_t)FB_W * FB_H * sizeof *fb);
    fails += expect_eq("deterministic textured framebuffer", fh1 == fh2, 1);
    reset(); run_ticks(1, 10);
    if (render_spans(&sa, NULL) != 0 || get_texrows(&tr0) != 0 ||
        get_status(&st) != 0) { free(fb); return 2; }
    draw_buffer(fb, &sa, NULL, &tr0, &st, floor_c, ceil_c);
    fails += expect_eq("textured framebuffer changes with movement",
                       fh1 != fnv1a(fb, (size_t)FB_W * FB_H * sizeof *fb), 1);
    free(fb);

    /* Determinism includes door, Alien and sprite status. */
    reset(); run_ticks(1, 60); run_block(activate_block, "activate");
    run_ticks(0, 12); run_ticks(1, 20);
    get_state(&x, &y, &a); get_status(&st); render_spans(&s, NULL);
    int x1=x, y1=y, a1=a, ax1=st.ax, ay1=st.ay, st1=st.state, vis1=st.visible;
    uint32_t h1=s.hash, sh1=fnv1a(&st, sizeof st);
    reset(); run_ticks(1, 60); run_block(activate_block, "activate");
    run_ticks(0, 12); run_ticks(1, 20);
    get_state(&x, &y, &a); get_status(&st); render_spans(&s, NULL);
    fails += expect_eq("repeated simulation determinism",
                       x1==x && y1==y && a1==a && ax1==st.ax && ay1==st.ay &&
                       st1==st.state && vis1==st.visible && h1==s.hash &&
                       sh1==fnv1a(&st, sizeof st), 1);

    cell loader0 = M[GC_LOADER_HP];
    for (int i = 0; i < 20; i++) {
        if (render_spans(&s, NULL) != 0 || get_status(&st) != 0) return 2;
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

static void build_projection_blocks(void) {
    reset_block = parse_source("[ doom/reset ]", "reset");
    floor_line_block = parse_source("[ doom/detail-line 768 256 768 384 0 5 ]", "floor wall base");
    ceil_line_block = parse_source("[ doom/detail-line 768 256 768 384 128 5 ]", "ceiling wall top");
    for (int b = 0; b < 2; b++) {
        size_t cap = 256 + (PROJ_COLS / 2) * 64;
        char *src = malloc(cap);
        if (!src) fail("out of memory building projection render block");
        size_t used = 0;
        used += (size_t)snprintf(src + used, cap - used, "[ ");
        for (int i = b * (PROJ_COLS / 2); i < (b + 1) * (PROJ_COLS / 2); i++) {
            int sx = i * PROJ_SCALE + PROJ_SCALE / 2;
            used += (size_t)snprintf(src + used, cap - used,
                                     "doom/sx: %d do %s ", sx,
                                     raw_wall_mode ? "doom/raw-column-block"
                                                   : "doom/render-column-block");
        }
        snprintf(src + used, cap - used, "]");
        render_blocks[b] = parse_source(src, "projection render");
        free(src);
    }
}

static int projection_test(void) {
    load_session();
    load_file("tests/detail.glon");
    detail_block = parse_source("[ do doom/detail-block ]", "detail");
    build_projection_blocks();
    int fails = 0;
    Spans sf;
    const int cams[][3] = {
        {500, 320, 0}, {550, 180, 32}, {700, 150, 64},
        {600, 150, 96}, {900, 320, 128}
    };
    Detail d1, d2, df;
    reset();
    if (get_detail(&d1) != 0) return 2;
    reset();
    if (get_detail(&d2) != 0) return 2;
    fails += expect_eq("detail line count", d1.n, 4);
    fails += expect_eq("identical frame detail hash", d1.hash == d2.hash, 1);
    reset();
    if (run_block(parse_source("[ doom/px: 330 doom/aim ]", "detail translate"), "detail translate") != 0 ||
        get_detail(&df) != 0) return 2;
    fails += expect_eq("floor detail moves on translation", df.hash != d1.hash, 1);
    reset();
    if (run_block(parse_source("[ doom/angle: 1 doom/aim ]", "detail turn"), "detail turn") != 0 ||
        get_detail(&df) != 0) return 2;
    fails += expect_eq("ceiling detail responds to turn", df.hash != d1.hash, 1);
    int visible_checks = 0, aligned = 0;
    int angle_seen[5] = {0,0,0,0,0};
    for (size_t ci = 0; ci < sizeof cams / sizeof cams[0]; ci++) {
        char src[160];
        snprintf(src, sizeof src, "[ doom/px: %d doom/py: %d doom/angle: %d doom/aim ]",
                 cams[ci][0], cams[ci][1], cams[ci][2]);
        cell cam = parse_source(src, "camera");
        if (run_block(cam, "camera") != 0 || render_spans(&sf, NULL) != 0) return 2;
        int lf[6], lc[6];
        if (run_capture(floor_line_block, lf, 6, "floor wall base") != 0 ||
            run_capture(ceil_line_block, lc, 6, "ceiling wall top") != 0) return 2;
        int ai = (cams[ci][2] + 16) / 32;
        if (ai > 4) ai = 4;
        for (int which = 0; which < 2; which++) {
            int *ln = which ? lc : lf;
            if (!ln[0]) continue;
            int xm = (ln[1] + ln[3]) / 2;
            int ym = (ln[2] + ln[4]) / 2;
            if (xm < 0 || xm >= FB_W || ym < 0 || ym >= FB_H) continue;
            int col = xm * PROJ_COLS / FB_W;
            if (col < 0) col = 0;
            if (col >= PROJ_COLS) col = PROJ_COLS - 1;
            if (sf.color[col] != 4) continue;
            int wall_y = which ? sf.top[col] : sf.bot[col];
            if (wall_y < 0 || wall_y >= FB_H) continue;
            visible_checks++;
            int d = ym > wall_y ? ym - wall_y : wall_y - ym;
            if (d <= 4) aligned++;
            else printf("FAIL %s alignment angle=%d pos=%d,%d line_y=%d wall_y=%d\n",
                        which ? "ceiling" : "floor", cams[ci][2],
                        cams[ci][0], cams[ci][1], ym, wall_y);
            angle_seen[ai] = 1;
        }
    }
    fails += expect_eq("floor/ceiling wall alignment samples", aligned, visible_checks);
    printf("INFO projection alignment: visible=%d aligned=%d angles=%d%d%d%d%d\n",
           visible_checks, aligned, angle_seen[0], angle_seen[1], angle_seen[2],
           angle_seen[3], angle_seen[4]);
    {
        cell forward_line = parse_source("[ doom/detail-line 100 320 1000 320 0 5 ]", "near clip");
        cell reverse_line = parse_source("[ doom/detail-line 1000 320 100 320 0 5 ]", "near clip reverse");
        int a6[6], b6[6];
        reset();
        if (run_capture(forward_line, a6, 6, "near clip") != 0 ||
            run_capture(reverse_line, b6, 6, "near clip reverse") != 0) return 2;
        fails += expect_eq("near-plane clipped line visible", a6[0], 1);
        fails += expect_eq("reversed line visible", b6[0], 1);
        fails += expect_eq("endpoint order independent", a6[0] == b6[0], 1);
    }
    printf("projection-test: %s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}

/* ---- Scripted trace ---------------------------------------------------- */

static int scripted_trace(void) {
    load_session();
    build_blocks();
    Status st;
    reset();
    int x, y, a;
    get_state(&x, &y, &a);
    printf("trace start: x=%d y=%d angle=%d\n", x, y, a);
    struct { int mask, n; const char *name; } steps[] = {
        {1, 60, "approach-closed-door"}, {0, 12, "activate-door"},
        {0, 60, "observe-alien"}, {1, 20, "enter-corridor"},
        {16, 64, "turn-left-90"}, {4, 15, "strafe-left"}
    };
    for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        if (i == 1) {
            if (run_block(activate_block, "activate") != 0) return 2;
        }
        if (run_ticks(steps[i].mask, steps[i].n) != 0) return 2;
        get_state(&x, &y, &a);
        if (get_status(&st) != 0) return 2;
        printf("trace %-20s x=%d y=%d angle=%d door=%d/%d alien=%d,%d state=%d det=%d vis=%d\n",
               steps[i].name, x, y, a, st.door_open, st.door_blocked,
               st.ax, st.ay, st.state, st.detected, st.visible);
    }
    return 0;
}

static void draw_buffer(uint32_t *fb, const Spans *s, const Detail *d,
                        const TexRows *tr, const Status *st,
                        int floor_c, int ceil_c);

/* ---- Deterministic screenshot walkthrough ------------------------------ */

static int save_ppm(const char *path, const uint32_t *fb) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", FB_W, FB_H);
    for (int i = 0; i < FB_W * FB_H; i++) {
        uint32_t c = fb[i];
        unsigned char rgb[3] = { (unsigned char)((c >> 16) & 255),
                                 (unsigned char)((c >> 8) & 255),
                                 (unsigned char)(c & 255) };
        if (fwrite(rgb, 1, 3, f) != 3) { fclose(f); return -1; }
    }
    fclose(f);
    return 0;
}

static int shot_walkthrough(const char *dir) {
    load_session();
    build_blocks();
    int floor_c, ceil_c;
    if (get_sector(&floor_c, &ceil_c) != 0) return 2;
    uint32_t *fb = malloc((size_t)FB_W * FB_H * sizeof *fb);
    if (!fb) fail("out of memory");
    char path[4096];
    const char *names[] = { "room", "near-door", "corridor", "alien", "inside", "turn" };
    int stage = 0;
    Spans s; Detail d; TexRows tr; Status st;
    for (;;) {
        if (render_spans(&s, NULL) != 0 || (need_texrows() && get_texrows(&tr) != 0) || get_status(&st) != 0) {
            free(fb); return 2;
        }
        draw_buffer(fb, &s, &d, &tr, &st, floor_c, ceil_c);
        snprintf(path, sizeof path, "%s/%s.ppm", dir, names[stage]);
        if (save_ppm(path, fb) != 0) { free(fb); return 2; }
        int px, py, pa;
        get_state(&px, &py, &pa);
        printf("shot %s: player=%d,%d door=%d/%d alien=%d,%d state=%d vis=%d\n",
               names[stage], px, py, st.door_open, st.door_blocked,
               st.ax, st.ay, st.state, st.visible);
        stage++;
        if (stage == 1) { if (run_ticks(1, 37) != 0) break; }
        else if (stage == 2) { if (run_block(activate_block, "activate") != 0) break; if (run_ticks(0, 12) != 0) break; }
        else if (stage == 3) { if (run_ticks(0, 60) != 0) break; }
        else if (stage == 4) { if (run_ticks(1, 48) != 0) break; }
        else if (stage == 5) { if (run_ticks(1, 25) != 0 || run_ticks(16, 64) != 0) break; }
        else break;
    }
    free(fb);
    return stage == 6 ? 0 : 2;
}

/* ---- SDL presentation -------------------------------------------------- */

static uint32_t palette(int id) {
    switch (id) {
    case 1: return 0xff4a5059u;  /* steel room wall */
    case 2: return 0xff66707au;  /* lighter corridor steel */
    case 3: return 0xff3b5570u;  /* blue turn wall */
    case 4: return 0xffa47b3bu;  /* amber door */
    case 5: return 0xff4d5963u;  /* floor/ceiling grid */
    case 6: return 0xff242a2fu;  /* floor base */
    case 7: return 0xff151d24u;  /* ceiling base */
    case 8: return 0xffcaa84fu;  /* floor centreline */
    case 9: return 0xffeaf4dcu;  /* overhead light */
    case 10: return 0xff3f8b90u; /* edge marking */
    default: return 0xff101010u;
    }
}

static uint32_t scale_color(uint32_t c, int pct) {
    int r = (int)((c >> 16) & 255), g = (int)((c >> 8) & 255), b = (int)(c & 255);
    r = r * pct / 100; g = g * pct / 100; b = b * pct / 100;
    return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
static uint32_t shade_color(uint32_t c, int shade) {
    static const int pct[4] = { 48, 64, 82, 100 };
    if (shade < 0) shade = 0;
    if (shade > 3) shade = 3;
    return scale_color(c, pct[shade]);
}

static void plot(uint32_t *fb, int x, int y, uint32_t c) {
    if (x >= 0 && x < FB_W && y >= 0 && y < FB_H) fb[y * FB_W + x] = c;
}

static const uint32_t *wall_texture(int material) {
    if (new_textures) {
        if (material == 0 || material == 2 || material == 4) return tex_panel_v2;
        if (material == 1 || material == 3) return tex_service_v2;
    }
    switch (material) {
    case 0: return tex_wall_panel;
    case 1: return tex_wall_service;
    case 2: return tex_vent_grille;
    case 3: return tex_rib;
    case 4: return tex_pipes;
    case 5: return tex_door;
    default: return tex_wall_panel;
    }
}

static int wall_tex_v(int y, int top, int bot) {
    int den = bot - top;
    if (den <= 0) return 0;
    int v = (y - top) * 64 / den;
    if (v < 0) v = 0;
    if (v > 63) v = 63;
    return v;
}

static int smooth_wall_pct(int top, int bot, int shade) {
    int h = bot - top + 1;
    int pct;
    if (h >= 123) pct = 100;
    else if (h >= 61) pct = 82 + (h - 61) * 18 / 62;
    else if (h >= 31) pct = 64 + (h - 31) * 18 / 30;
    else pct = 48 + h * 16 / 31;
    (void)shade;
    return pct;
}

static uint32_t checker_tex(int u, int v) {
    int cell = ((u >> 3) + (v >> 3)) & 1;
    int minor = ((u & 7) == 0 || (v & 7) == 0);
    int major = ((u & 31) == 0 || (v & 31) == 0);
    int grad = ((u + v) * 19) & 31;
    if (major) return 0xfffff0c0u;
    if (minor) return 0xffd8d0b0u;
    int r = 40 + grad, g = 58 + grad, b = 66 + grad;
    if (cell) { r = 150 - grad; g = 65 + grad / 2; b = 52; }
    else { r = 35; g = 74 + grad; b = 82 + grad; }
    return 0xff000000u | ((uint32_t)r<<16) | ((uint32_t)g<<8) | (uint32_t)b;
}

static uint32_t sample_wall_tex(int material, int u, int y, int top, int bot, int shade) {
    const uint32_t *tex = wall_texture(material);
    if (checker_walls) {
        int tu = (u >> 2) & 63;
        int tv = wall_tex_v(y, top, bot);
        return shade_color(checker_tex(tu, tv), shade);
    }
    int tu = (material == 5) ? (u - 256) * 64 / 128 : (u >> 2) & 63;
    if (tu < 0) tu = 0;
    if (tu > 63) tu = 63;
    int v = wall_tex_v(y, top, bot);
    uint32_t base = tex[v * 64 + tu];
    if (smooth_shading) return scale_color(base, smooth_wall_pct(top, bot, shade));
    return shade_color(base, shade);
}

static void draw_tex_planes(uint32_t *fb, const TexRows *r) {
    for (int y = 0; y < FB_H; y++) {
        if (!r->valid[y]) continue;
        const uint32_t *tex = (y > FB_H / 2) ? tex_floor : tex_ceiling_light;
        int shade = r->shade[y];
        for (int x = 0; x < FB_W; x++) {
            int64_t wx = (int64_t)r->bx[y] + (int64_t)r->sx[y] * x;
            int64_t wy = (int64_t)r->by[y] + (int64_t)r->sy[y] * x;
            int tx = (int)((wx >> 16) >> 2) & 63;
            int ty = (int)((wy >> 16) >> 2) & 63;
            uint32_t c = tex[ty * 64 + tx];
            fb[y * FB_W + x] = shade_color(c, shade);
        }
    }
}

static uint32_t shade_argb(uint32_t c, int pct) {
    int a = (int)(c >> 24), r = (int)((c >> 16) & 255), g = (int)((c >> 8) & 255), b = (int)(c & 255);
    r = r * pct / 100; g = g * pct / 100; b = b * pct / 100;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static const uint32_t *alien_frame_for(const Status *st) {
    if (st->state == 2) return alien_attack;
    if (st->state == 1) {
        if (st->tside > 600 || st->tside < -600) return alien_side;
        return ((sim_tick / 8) & 1) ? alien_walk2 : alien_walk1;
    }
    if (st->tside > 700 || st->tside < -700) return alien_side;
    return alien_idle;
}

static const uint32_t *object_sprite(int id) {
    switch (id) { case 0: return obj_drum; case 1: return obj_crate;
                  case 2: return obj_cabinet; default: return obj_container; }
}

static void draw_sprite_rect(uint32_t *fb, const uint32_t *src, int sw, int sh,
                             int l, int r, int t, int b, int pct) {
    if (l < 0) l = 0;
    if (r >= FB_W) r = FB_W - 1;
    if (t < 0) t = 0;
    if (b >= FB_H) b = FB_H - 1;
    int dw = r - l + 1, dh = b - t + 1;
    if (dw <= 0 || dh <= 0) return;
    for (int y = t; y <= b; y++) {
        int sy = (y - t) * sh / dh;
        if (sy < 0) sy = 0;
        if (sy >= sh) sy = sh - 1;
        for (int x = l; x <= r; x++) {
            int sx = (x - l) * sw / dw;
            if (sx < 0) sx = 0;
            if (sx >= sw) sx = sw - 1;
            uint32_t c = src[sy * sw + sx];
            int a = (int)(c >> 24);
            if (!a) continue;
            c = shade_argb(c, pct);
            if (a == 255) { fb[y * FB_W + x] = c; continue; }
            int da = 255 - a;
            int dr = (int)((fb[y*FB_W+x] >> 16) & 255);
            int dg = (int)((fb[y*FB_W+x] >> 8) & 255);
            int db = (int)(fb[y*FB_W+x] & 255);
            int rr = ((int)((c >> 16) & 255) * a + dr * da) / 255;
            int gg = ((int)((c >> 8) & 255) * a + dg * da) / 255;
            int bb = ((int)(c & 255) * a + db * da) / 255;
            fb[y*FB_W+x] = 0xff000000u | ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | (uint32_t)bb;
        }
    }
}

static int alien_frame_index(const Status *st) {
    const uint32_t *f = alien_frame_for(st);
    if (f == alien_idle) return 0;
    if (f == alien_walk1) return 1;
    if (f == alien_walk2) return 2;
    if (f == alien_attack) return 3;
    return 4;
}

static void draw_alien_sprite(uint32_t *fb, const Status *st) {
    if (!st->visible) return;
    const uint32_t *src = alien_frame_for(st);
    int pct = st->tdepth < 256 ? 112 : st->tdepth < 512 ? 100 : st->tdepth < 1024 ? 86 : 72;
    draw_sprite_rect(fb, src, ALIEN_W, ALIEN_H, st->left, st->right, st->top, st->bot, pct);
}

static void draw_tracker(uint32_t *fb, const Status *st) {
    const int cx = 42, cy = 168, rad = 22;
    for (int y = 142; y < 196; y++)
        for (int x = 6; x < 78; x++) fb[y * FB_W + x] = 0xff0d1117u;
    uint32_t door_c = st->door_blocked ? 0xff9a6b35u : 0xff3eaa58u;
    for (int y = 146; y < 150; y++)
        for (int x = 10; x < 74; x++) fb[y * FB_W + x] = door_c;
    for (int y = -rad; y <= rad; y++)
        for (int x = -rad; x <= rad; x++) {
            int d2 = x * x + y * y;
            if (d2 >= (rad - 2) * (rad - 2) && d2 <= rad * rad)
                plot(fb, cx + x, cy + y, 0xff55635au);
        }
    for (int y = -rad + 3; y <= rad - 3; y++) plot(fb, cx, cy + y, 0xff27322bu);
    for (int x = -rad + 3; x <= rad - 3; x++) plot(fb, cx + x, cy, 0xff27322bu);
    plot(fb, cx, cy - rad + 2, 0xffb8e0a0u);
    if (st->detected) {
        int bx = cx + st->tside * 18 / 700;
        int by = cy - st->tdepth * 18 / 700;
        if (bx < cx - rad + 3) bx = cx - rad + 3;
        if (bx > cx + rad - 3) bx = cx + rad - 3;
        if (by < cy - rad + 3) by = cy - rad + 3;
        if (by > cy + rad - 3) by = cy + rad - 3;
        uint32_t c = st->state == 2 ? 0xffe05050u : 0xff6fe08au;
        for (int y = -2; y <= 2; y++)
            for (int x = -2; x <= 2; x++) plot(fb, bx + x, by + y, c);
    } else {
        for (int y = -1; y <= 1; y++)
            for (int x = -1; x <= 1; x++) plot(fb, cx + x, cy + y, 0xff4d5c52u);
    }
}

static void draw_buffer(uint32_t *fb, const Spans *s, const Detail *d,
                        const TexRows *tr, const Status *st,
                        int floor_c, int ceil_c) {
    (void)d;
    if (flat_surfaces) {
        for (int y = 0; y < FB_H; y++) {
            int d = y < FB_H / 2 ? (FB_H / 2 - y) : (y - FB_H / 2);
            int pct = 58 + d * 48 / (FB_H / 2);
            uint32_t base = (y < FB_H / 2) ? 0xff26323au : 0xff3b474du;
            uint32_t c = scale_color(base, pct);
            for (int x = 0; x < FB_W; x++) fb[y * FB_W + x] = c;
        }
        for (int x = 0; x < FB_W; x++) fb[(FB_H / 2) * FB_W + x] = 0xff11171bu;
    } else {
        for (int y = 0; y < FB_H; y++)
            for (int x = 0; x < FB_W; x++)
                fb[y * FB_W + x] = (y < FB_H / 2) ? palette(ceil_c) : palette(floor_c);
    }
    if (!flat_surfaces && !simple_mode && tr) draw_tex_planes(fb, tr);
    for (int i = 0; i < PROJ_COLS; i++) {
        int top = s->top[i], bot = s->bot[i];
        if (top < 0) top = 0;
        if (bot >= FB_H) bot = FB_H - 1;
        if (top > bot) continue;
        int color = s->color[i], u = s->u[i], shade = s->shade[i];
        int material = s->material[i];
        uint32_t flat = palette(color);
        for (int y = top; y <= bot; y++) {
            uint32_t c = simple_mode ? shade_color(flat, shade)
                                     : sample_wall_tex(material, u, y, top, bot, shade);
            for (int x = i * PROJ_SCALE; x < (i + 1) * PROJ_SCALE; x++)
                fb[y * FB_W + x] = c;
        }
    }
    long sp0 = now_ns();
    if (objects_mode) {
        if (current_objs_valid) {
            SpriteDraw list[7]; int n = 0;
            for (int i = 0; i < 6; i++) if (current_objs[i].visible) {
                list[n].depth = current_objs[i].depth; list[n].l = current_objs[i].left; list[n].r = current_objs[i].right;
                list[n].t = current_objs[i].top; list[n].b = current_objs[i].bot; list[n].src = object_sprite(current_objs[i].id);
                list[n].sw = OBJ_W; list[n].sh = OBJ_H; list[n].pct = current_objs[i].depth < 256 ? 100 : 86; n++;
            }
            if (st->visible) { list[n].depth = st->tdepth; list[n].l = st->left; list[n].r = st->right;
                list[n].t = st->top; list[n].b = st->bot; list[n].src = alien_frame_for(st);
                list[n].sw = ALIEN_W; list[n].sh = ALIEN_H;
                list[n].pct = st->tdepth < 256 ? 112 : st->tdepth < 512 ? 100 : 86; n++; }
            for (int i = 1; i < n; i++) { int j=i; while (j>0 && list[j-1].depth < list[j].depth) { SpriteDraw z=list[j]; list[j]=list[j-1]; list[j-1]=z; j--; } }
            for (int i = 0; i < n; i++) draw_sprite_rect(fb, list[i].src, list[i].sw, list[i].sh, list[i].l, list[i].r, list[i].t, list[i].b, list[i].pct);
        } else draw_alien_sprite(fb, st);
    } else draw_alien_sprite(fb, st);
    long sp1 = now_ns();
    last_sprite_ms = (double)(sp1 - sp0) / 1e6;
    draw_tracker(fb, st);
}

static void apply_scale_hint(void) {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, linear_filter ? "1" : "0");
}

static int sdl_present(SDL_Renderer *ren, SDL_Texture *tex, uint32_t *fb,
                       const Spans *s, const Detail *d, const TexRows *tr,
                       const Status *st, int floor_c, int ceil_c,
                       double *raster_ms, double *present_ms) {
    long r0 = now_ns();
    draw_buffer(fb, s, d, tr, st, floor_c, ceil_c);
    long r1 = now_ns();
    if (raster_ms) *raster_ms = (double)(r1 - r0) / 1e6;
    if (SDL_UpdateTexture(tex, NULL, fb, FB_W * (int)sizeof(uint32_t)) != 0) return -1;
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
    long t1 = now_ns();
    if (present_ms) *present_ms = (double)(t1 - r1) / 1e6;
    return 0;
}


/* ---- G5g: controlled benchmarks, view cache and loader diagnostics ------ */

static int set_camera(int x, int y, int angle) {
    char src[160];
    snprintf(src, sizeof src,
             "[ doom/px: %d doom/py: %d doom/angle: %d doom/aim ]", x, y, angle);
    cell b = parse_source(src, "fixed camera");
    return run_block(b, "fixed camera");
}

static double median_of(double *v, int n) {
    for (int i = 1; i < n; i++) {
        double key = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
        v[j + 1] = key;
    }
    return n & 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) * 0.5;
}

static void sample_summary(const char *name, double *v, int n, double *mean_out) {
    double sum = 0, min = v[0];
    for (int i = 0; i < n; i++) { sum += v[i]; if (v[i] < min) min = v[i]; }
    double mean = sum / n, var = 0;
    for (int i = 0; i < n; i++) { double d = v[i] - mean; var += d * d; }
    double med = median_of(v, n);
    printf("%s min=%.2f med=%.2f mean=%.2f sd=%.2f\n",
           name, min, med, mean, n > 1 ? sqrt(var / (n - 1)) : 0.0);
    if (mean_out) *mean_out = mean;
}

static int bench_fixed(int frames, int camx, int camy, int cama) {
    if (frames < 1) return 2;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 2;
    SDL_Window *win = SDL_CreateWindow("Glon AVP G5g - fixed benchmark",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       960, 600, SDL_WINDOW_HIDDEN);
    if (!win) fail(SDL_GetError());
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) fail(SDL_GetError());
    apply_scale_hint();
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H);
    if (!tex) fail(SDL_GetError());
    uint32_t *fb = malloc((size_t)FB_W * FB_H * sizeof *fb);
    double *wall = malloc((size_t)frames * sizeof *wall);
    double *row = malloc((size_t)frames * sizeof *row);
    double *raster = malloc((size_t)frames * sizeof *raster);
    double *present = malloc((size_t)frames * sizeof *present);
    double *total = malloc((size_t)frames * sizeof *total);
    if (!fb || !wall || !row || !raster || !present || !total) fail("out of memory");

    load_session();
    build_blocks();
    if (reset() != 0 || set_camera(camx, camy, cama) != 0) return 2;
    int floor_c, ceil_c;
    if (get_sector(&floor_c, &ceil_c) != 0) return 2;
    Spans s; Detail det; TexRows tr; Status st;
    if (get_status(&st) != 0) return 2;
    if (need_texrows() && get_texrows(&tr) != 0) return 2;

    const int warm = 20;
    long w0 = now_ns();
    for (int i = 0; i < warm; i++) {
        long a = now_ns();
        if (render_spans(&s, NULL) != 0) return 2;
        if (need_texrows() && get_texrows(&tr) != 0) return 2;
        if (get_status(&st) != 0) return 2;
        double rm, pm;
        if (sdl_present(ren, tex, fb, &s, &det, need_texrows() ? &tr : NULL, &st,
                        floor_c, ceil_c, &rm, &pm) != 0) return 2;
        (void)a;
    }
    long w1 = now_ns();

    for (int i = 0; i < frames; i++) {
        long t0 = now_ns();
        if (render_spans(&s, NULL) != 0) return 2;
        long t1 = now_ns();
        if (need_texrows() && get_texrows(&tr) != 0) return 2;
        if (get_status(&st) != 0) return 2;
        long t2 = now_ns();
        double rm, pm;
        if (sdl_present(ren, tex, fb, &s, &det, need_texrows() ? &tr : NULL, &st,
                        floor_c, ceil_c, &rm, &pm) != 0) return 2;
        long t3 = now_ns();
        wall[i] = (t1 - t0) / 1e6;
        row[i] = (t2 - t1) / 1e6;
        raster[i] = rm; present[i] = pm;
        total[i] = (t3 - t0) / 1e6;
    }
    printf("fixed frames=%d warmup=%d camera=%d,%d,%d mode=%s\n",
           frames, warm, camx, camy, cama, simple_mode ? "simple" : "textured");
    printf("fixed warmup_wall=%.2fms\n", (double)(w1 - w0) / warm / 1e6);
    sample_summary("fixed wall", wall, frames, NULL);
    sample_summary("fixed texrow", row, frames, NULL);
    sample_summary("fixed raster", raster, frames, NULL);
    sample_summary("fixed present", present, frames, NULL);
    sample_summary("fixed total", total, frames, NULL);
    printf("fixed hash spans=%08x rows=%08x status=%08x\n", s.hash,
           simple_mode ? 0 : tr.hash, fnv1a(&st, sizeof st));
    printf("fixed loader=%ld used=%ld free=%ld code=%ld heap_high=%ld gc=%ld\n",
           (long)M[GC_LOADER_HP], (long)M[GC_LOADER_HP] - R0S1_HEAP_BASE,
           R0S1_HEAP_LIMIT - (long)M[GC_LOADER_HP], r0_s1_code_size(),
           r0_s1_heap_high(), r0_s1_gc_count());

    free(wall); free(row); free(raster); free(present); free(total); free(fb);
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

static int cache_frame(ViewCache *c, uint32_t *fb, int floor_c, int ceil_c, int force) {
    int px, py, pa;
    if (get_state(&px, &py, &pa) != 0) return -1;
    Status st;
    if (get_status(&st) != 0) return -1;
    ViewSig sig;
    sig.px = px; sig.py = py; sig.angle = pa; sig.simple = simple_mode;
    sig.floor_c = floor_c; sig.ceil_c = ceil_c;
    sig.st = st; sig.st.light = 0; /* stationary cache ignores the light pulse */
    if (!force && c->valid && memcmp(&sig, &c->sig, sizeof sig) == 0) return 0;
    if (render_spans(&c->s, NULL) != 0) return -1;
    if (need_texrows() && get_texrows(&c->tr) != 0) return -1;
    draw_buffer(fb, &c->s, NULL, simple_mode ? NULL : &c->tr, &st, floor_c, ceil_c);
    c->sig = sig;
    c->valid = 1;
    return 1;
}

static int stationary_test(void) {
    uint32_t *fb = malloc((size_t)FB_W * FB_H * sizeof *fb);
    if (!fb) return 2;
    load_session();
    build_blocks();
    if (reset() != 0 || set_camera(320, 320, 0) != 0) return 2;
    int floor_c, ceil_c;
    if (get_sector(&floor_c, &ceil_c) != 0) return 2;
    ViewCache cache; memset(&cache, 0, sizeof cache);
    int fails = 0;
    int miss = cache_frame(&cache, fb, floor_c, ceil_c, 0);
    if (miss < 0) return 2;
    uint32_t h0 = fnv1a(fb, (size_t)FB_W * FB_H * sizeof *fb);
    fails += expect_eq("stationary first frame is a miss", miss, 1);
    int sx0, sy0, sa0; Status st0;
    get_state(&sx0, &sy0, &sa0); get_status(&st0);
    if (tick(0) != 0) return 2;
    miss = cache_frame(&cache, fb, floor_c, ceil_c, 0);
    if (miss < 0) return 2;
    fails += expect_eq("stationary unchanged frame is a hit", miss, 0);
    fails += expect_eq("stationary cached pixels identical",
                       fnv1a(fb, (size_t)FB_W * FB_H * sizeof *fb), h0);
    int sx1, sy1, sa1; Status st1;
    get_state(&sx1, &sy1, &sa1); get_status(&st1);
    fails += expect_eq("stationary cache does not alter player",
                       sx0 == sx1 && sy0 == sy1 && sa0 == sa1, 1);
    st0.light = st1.light = 0;  /* the light pulse advances independently */
    fails += expect_eq("stationary cache does not alter status",
                       memcmp(&st0, &st1, sizeof st0) == 0, 1);

    if (tick(32) != 0) return 2;  /* turn right */
    miss = cache_frame(&cache, fb, floor_c, ceil_c, 0);
    if (miss < 0) return 2;
    uint32_t h1 = fnv1a(fb, (size_t)FB_W * FB_H * sizeof *fb);
    fails += expect_eq("camera turn invalidates cache", miss, 1);
    fails += expect_eq("camera turn changes pixels", h1 != h0, 1);

    if (reset() != 0 || set_camera(320, 320, 0) != 0) return 2;
    memset(&cache, 0, sizeof cache);
    if (cache_frame(&cache, fb, floor_c, ceil_c, 0) < 0) return 2;
    uint32_t hd = fnv1a(fb, (size_t)FB_W * FB_H * sizeof *fb);
    cell door = parse_source("[ doom/door-target: 64 ]", "door target");
    if (run_block(door, "door target") != 0 || run_ticks(0, 12) != 0) return 2;
    miss = cache_frame(&cache, fb, floor_c, ceil_c, 0);
    if (miss < 0) return 2;
    uint32_t h2 = fnv1a(fb, (size_t)FB_W * FB_H * sizeof *fb);
    fails += expect_eq("door state invalidates cache", miss, 1);
    fails += expect_eq("door change alters pixels", h2 != hd, 1);

    ViewCache fresh; memset(&fresh, 0, sizeof fresh);
    if (cache_frame(&fresh, fb, floor_c, ceil_c, 1) < 0) return 2;
    fails += expect_eq("forced uncached frame matches cached path",
                       fnv1a(fb, (size_t)FB_W * FB_H * sizeof *fb), h2);

    free(fb);
    printf("stationary-test: %s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}

static int sdl_present_cached(SDL_Renderer *ren, SDL_Texture *tex, uint32_t *fb,
                              int redraw) {
    if (redraw && SDL_UpdateTexture(tex, NULL, fb, FB_W * (int)sizeof(uint32_t)) != 0)
        return -1;
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
    return 0;
}

static int stationary_bench(int frames, int throttle) {
    if (frames < 4) return 2;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 2;
    SDL_Window *win = SDL_CreateWindow("Glon AVP G5g - stationary benchmark",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       960, 600, SDL_WINDOW_HIDDEN);
    if (!win) fail(SDL_GetError());
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) fail(SDL_GetError());
    apply_scale_hint();
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H);
    if (!tex) fail(SDL_GetError());
    uint32_t *fb = malloc((size_t)FB_W * FB_H * sizeof *fb);
    if (!fb) fail("out of memory");
    load_session();
    build_blocks();
    if (reset() != 0 || set_camera(320, 320, 0) != 0) return 2;
    int floor_c, ceil_c;
    if (get_sector(&floor_c, &ceil_c) != 0) return 2;
    ViewCache cache; memset(&cache, 0, sizeof cache);
    int hits = 0, misses = 0;
    double hit_ms = 0, miss_ms = 0, sim_ms = 0, present_ms = 0;
    double input_ms = -1;
    struct timespec cpu0, cpu1, wall0, wall1;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu0);
    clock_gettime(CLOCK_MONOTONIC, &wall0);
    for (int i = -5; i < frames; i++) {
        long t0 = now_ns();
        if (tick(0) != 0) return 2;
        int inject = (i == frames / 2);
        if (inject && tick(32) != 0) return 2;
        long t1 = now_ns();
        int miss = cache_frame(&cache, fb, floor_c, ceil_c, 0);
        if (miss < 0) return 2;
        long t2 = now_ns();
        if (sdl_present_cached(ren, tex, fb, miss) != 0) return 2;
        long t3 = now_ns();
        if (i >= 0) {
            if (miss) { misses++; miss_ms += (double)(t3 - t0) / 1e6; }
            else { hits++; hit_ms += (double)(t3 - t0) / 1e6; }
            sim_ms += (double)(t1 - t0) / 1e6;
            present_ms += (double)(t3 - t2) / 1e6;
            if (inject) input_ms = (double)(t3 - t0) / 1e6;
        }
        if (throttle) {
            long spent = t3 - t0;
            long target = 1000000000L / TICK_HZ;
            if (spent < target) SDL_Delay((Uint32)((target - spent + 999999) / 1000000));
        }
    }
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu1);
    clock_gettime(CLOCK_MONOTONIC, &wall1);
    double cpu_ms = ((double)(cpu1.tv_sec - cpu0.tv_sec) * 1e9 +
                     (double)(cpu1.tv_nsec - cpu0.tv_nsec)) / 1e6;
    double wall_ms = ((double)(wall1.tv_sec - wall0.tv_sec) * 1e9 +
                      (double)(wall1.tv_nsec - wall0.tv_nsec)) / 1e6;
    printf("stationary frames=%d throttle=%d hits=%d misses=%d hit_rate=%.1f%%\n",
           frames, throttle, hits, misses, 100.0 * hits / (hits + misses));
    printf("stationary full_frame=%.2fms cached_frame=%.2fms sim=%.2fms present=%.2fms input_to_update=%.2fms\n",
           misses ? miss_ms / misses : 0.0, hits ? hit_ms / hits : 0.0,
           sim_ms / frames, present_ms / frames, input_ms);
    printf("stationary cpu=%.2fms/frame wall=%.2fms cpu_pct=%.1f%%\n",
           cpu_ms / frames, wall_ms / frames, wall_ms > 0 ? 100.0 * cpu_ms / wall_ms : 0.0);
    printf("stationary loader=%ld heap_high=%ld gc=%ld\n",
           (long)M[GC_LOADER_HP], r0_s1_heap_high(), r0_s1_gc_count());
    free(fb);
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

static int loader_report(void) {
    init_runtime();
    printf("loader report: base=%ld limit=%ld reserved=%ld\n",
           R0S1_HEAP_BASE, R0S1_HEAP_LIMIT, R0S1_HEAP_LIMIT - R0S1_HEAP_BASE);
    printf("loader after init used=%ld free=%ld code=%ld\n",
           (long)M[GC_LOADER_HP] - R0S1_HEAP_BASE,
           R0S1_HEAP_LIMIT - (long)M[GC_LOADER_HP], r0_s1_code_size());
    char path[4096];
    snprintf(path, sizeof path, "%s/jupyter/prelude.glon", root_dir);
    load_file(path);
    printf("loader after prelude used=%ld free=%ld code=%ld\n",
           (long)M[GC_LOADER_HP] - R0S1_HEAP_BASE,
           R0S1_HEAP_LIMIT - (long)M[GC_LOADER_HP], r0_s1_code_size());
    snprintf(path, sizeof path, "%s/experiments/glon-doom/doom.glon", root_dir);
    load_file(path);
    if (objects_mode) {
        snprintf(path, sizeof path, "%s/experiments/glon-doom/objects.glon", root_dir);
        load_file(path);
    }
    printf("loader after game used=%ld free=%ld code=%ld\n",
           (long)M[GC_LOADER_HP] - R0S1_HEAP_BASE,
           R0S1_HEAP_LIMIT - (long)M[GC_LOADER_HP], r0_s1_code_size());
    if (raw_wall_mode) {
        snprintf(path, sizeof path, "%s/experiments/glon-doom/raw-wall.glon", root_dir);
        load_file(path);
        printf("loader after raw-wall used=%ld free=%ld asm_here=%ld\n",
               (long)M[GC_LOADER_HP] - R0S1_HEAP_BASE,
               R0S1_HEAP_LIMIT - (long)M[GC_LOADER_HP], (long)asm_here());
    }
    build_blocks();
    long hp = (long)M[GC_LOADER_HP];
    cell gc, cap;
    r0_s1_global_usage(&gc, &cap);
    printf("loader after host blocks used=%ld free=%ld code=%ld asm_here=%ld globals=%ld/%ld\n",
           hp - R0S1_HEAP_BASE, R0S1_HEAP_LIMIT - hp, r0_s1_code_size(),
           (long)asm_here(), gc, cap);
    printf("code bytes? cells=%ld ip_start=%ld ip_end=%ld\n",
           r0_s1_code_size(), r0_s1_ip_start(), r0_s1_ip_end());
    int made = 0;
    const int probes[] = {128, 256, 320, 384, 448};
    for (size_t pi = 0; pi < sizeof probes / sizeof probes[0]; pi++) {
        int n = probes[pi];
        size_t capn = (size_t)n * 4 + 8;
        char *src = malloc(capn);
        if (!src) return 2;
        size_t w = 0;
        w += (size_t)snprintf(src + w, capn - w, "[");
        for (int i = 0; i < n; i++)
            w += (size_t)snprintf(src + w, capn - w, " %d", i & 255);
        snprintf(src + w, capn - w, " ]");
        long before = (long)M[GC_LOADER_HP];
        int err = 0;
        cell b = r0_s1_parse(src, &err);
        if (err) {
            printf("loader table probe n=%d refused kind=%d free_before=%ld\n",
                   n, r0_s1_parse_error_kind(), R0S1_HEAP_LIMIT - before);
            free(src);
            continue;
        }
        (void)b;
        made = n;
        printf("loader table probe n=%d delta=%ld used=%ld free=%ld\n",
               n, (long)M[GC_LOADER_HP] - before,
               (long)M[GC_LOADER_HP] - R0S1_HEAP_BASE,
               R0S1_HEAP_LIMIT - (long)M[GC_LOADER_HP]);
        free(src);
    }
    printf("loader table probe largest=%d; game has no masm (all loader data, runtime code resident)\n", made);
    return 0;
}

#ifdef DOOM_S1_PROFILE
static void print_prof(const char *name, const ProfAccum *a, int frames) {
    static const char *opname[8] = {"LIT","DUP","DROP","@","!","0BRANCH","HOST","HALT"};
    static const char *hname[16] = {"+","-","*","/","mod","neg","=","!=","<",">",
                                    "<=",">=","alloc","putchar","print","dump"};
    long total = 0, hosttotal = 0;
    for (int i = 0; i < 8; i++) total += a->op[i];
    for (int i = 0; i < 16; i++) hosttotal += a->host[i];
    printf("profile %s per_frame ops=%ld host=%ld branches=%ld taken=%ld cycles=%.0f\n",
           name, total / frames, hosttotal / frames, a->op[5] / frames,
           a->branches_taken / frames, (double)a->cycles / frames);
    printf("profile %s opcodes:", name);
    for (int i = 0; i < 8; i++) printf(" %s=%ld", opname[i], a->op[i] / frames);
    printf("\nprofile %s host:", name);
    for (int i = 0; i < 16; i++) if (a->host[i]) printf(" %s=%ld", hname[i], a->host[i] / frames);
    printf("\n");
}

static int opcode_bench(int frames) {
    load_session();
    build_blocks();
    if (reset() != 0 || set_camera(320, 320, 0) != 0) return 2;
    int floor_c, ceil_c;
    if (get_sector(&floor_c, &ceil_c) != 0) return 2;
    Spans s; TexRows tr; Status st;
    memset(&prof_wall, 0, sizeof prof_wall);
    memset(&prof_tex, 0, sizeof prof_tex);
    memset(&prof_status, 0, sizeof prof_status);
    memset(&prof_tick, 0, sizeof prof_tick);
    prof_on = 1;
    for (int i = 0; i < 3; i++) {
        if (tick(0) != 0 || render_spans(&s, NULL) != 0) return 2;
        if (need_texrows() && get_texrows(&tr) != 0) return 2;
        if (get_status(&st) != 0) return 2;
    }
    memset(&prof_wall, 0, sizeof prof_wall);
    memset(&prof_tex, 0, sizeof prof_tex);
    memset(&prof_status, 0, sizeof prof_status);
    memset(&prof_tick, 0, sizeof prof_tick);
    for (int i = 0; i < frames; i++) {
        if (tick(0) != 0) return 2;
        if (render_spans(&s, NULL) != 0) return 2;
        if (need_texrows() && get_texrows(&tr) != 0) return 2;
        if (get_status(&st) != 0) return 2;
    }
    prof_on = 0;
    printf("opcode frames=%d mode=%s loader=%ld heap_high=%ld gc=%ld\n",
           frames, simple_mode ? "simple" : "textured",
           (long)M[GC_LOADER_HP], r0_s1_heap_high(), r0_s1_gc_count());
    print_prof("wall", &prof_wall, frames);
    print_prof("texrow", &prof_tex, frames);
    print_prof("status", &prof_status, frames);
    print_prof("tick", &prof_tick, frames);
    /* top execution buckets in the wall stage */
    long *h = prof_wall.hist;
    for (int k = 0; k < 8; k++) {
        int best = -1;
        for (int i = 0; i < 256; i++) if (h[i] > 0 && (best < 0 || h[i] > h[best])) best = i;
        if (best < 0) break;
        printf("profile wall bucket ip=%d count_per_frame=%ld\n", best * 256, h[best] / frames);
        h[best] = 0;
    }
    return 0;
}
#endif

static void print_metrics(const Metrics *m) {
    printf("metrics: sim=%.2fms wall=%.2fms texrow=%.2fms raster=%.2fms "
           "sprite=%.2fms present=%.2fms frame=%.2fms fps=%.1f "
           "heap_high=%ld gc=%ld live=%ld\n",
           m->sim_ms, m->proj_ms, m->texrow_ms, m->raster_ms, m->sprite_ms, m->present_ms,
           m->frame_ms, m->frame_ms > 0.0 ? 1000.0 / m->frame_ms : 0.0,
           m->heap_high, m->gc_count, m->live_cells);
}

static int set_close_pose(void) {
    reset();
    const char *src;
    if (!alien_cover) {
        src = "[ doom/px: 700 doom/py: 320 doom/angle: 0 doom/aim "
              "doom/door-open: 64 doom/door-target: 64 doom/door-blocked: 0 "
              "doom/alien-x: 850 doom/alien-y: 320 doom/alien-state: 2 "
              "doom/alien-health: 100 doom/alien-detected: 1 ]";
    } else {
        int ay = cover_view == 1 ? 292 : cover_view == 2 ? 330 : 270;
        int py = cover_view == 3 ? 370 : 320;
        static char buf[320];
        snprintf(buf, sizeof buf,
                 "[ doom/px: 700 doom/py: %d doom/angle: 0 doom/aim "
                 "doom/door-open: 64 doom/door-target: 64 doom/door-blocked: 0 "
                 "doom/alien-x: 920 doom/alien-y: %d doom/alien-state: 1 "
                 "doom/alien-health: 100 doom/alien-detected: 1 ]", py, ay);
        src = buf;
    }
    cell pose = parse_source(src, "close pose");
    return run_block(pose, "close pose");
}

static int bench(int frames, int bench_alien) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 2;
    SDL_Window *win = SDL_CreateWindow("Glon AVP G5i - benchmark",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       960, 600, SDL_WINDOW_SHOWN);
    if (!win) fail(SDL_GetError());
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) fail(SDL_GetError());
    apply_scale_hint();
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H);
    if (!tex) fail(SDL_GetError());
    uint32_t *fb = malloc((size_t)FB_W * FB_H * sizeof *fb);
    if (!fb) fail("out of memory");

    load_session();
    build_blocks();
    if (bench_alien && set_close_pose() != 0) return 2;
    int floor_c, ceil_c;
    if (get_sector(&floor_c, &ceil_c) != 0) return 2;
    Spans s;
    Detail det;
    TexRows tr;
    Status st;
    if (get_status(&st) != 0) return 2;
    if (need_texrows() && get_texrows(&tr) != 0) return 2;
    Metrics sum = {0};
    long wall0 = now_ns();
    for (int i = 0; i < frames; i++) {
        long t0 = now_ns();
        if (tick(i & 63) != 0) return 2;
        long t1 = now_ns();
        long proj_ns;
        if (render_spans(&s, &proj_ns) != 0) return 2;
        if (need_texrows() && get_texrows(&tr) != 0) return 2;
        if (get_status(&st) != 0) return 2;
        long t2 = now_ns();
        double raster_ms, present_ms;
        if (sdl_present(ren, tex, fb, &s, &det, need_texrows() ? &tr : NULL, &st,
                        floor_c, ceil_c, &raster_ms, &present_ms) != 0) return 2;
        long t3 = now_ns();
        sum.sim_ms += (double)(t1 - t0) / 1e6;
        sum.proj_ms += (double)proj_ns / 1e6;
        sum.texrow_ms += (double)(t2 - t1 - proj_ns) / 1e6;
        sum.raster_ms += raster_ms;
        sum.sprite_ms += last_sprite_ms;
        sum.present_ms += present_ms;
        sum.frame_ms += (double)(t3 - t0) / 1e6;
        SDL_PumpEvents();
        SDL_Event e;
        while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT) { i = frames; break; }
    }
    long wall1 = now_ns();
    sum.sim_ms /= frames; sum.proj_ms /= frames; sum.texrow_ms /= frames;
    sum.raster_ms /= frames; sum.sprite_ms /= frames; sum.present_ms /= frames; sum.frame_ms /= frames;
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
    SDL_Window *win = SDL_CreateWindow("Glon AVP G5i | W/S/A/D move  arrows turn  E door  R textured/simple  Esc quit",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       960, 600, SDL_WINDOW_SHOWN);
    if (!win) fail(SDL_GetError());
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) fail(SDL_GetError());
    apply_scale_hint();
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
    Detail det;
    TexRows tr;
    Status st;
    if (get_status(&st) != 0) return 2;
    if (need_texrows() && get_texrows(&tr) != 0) return 2;
    int running = 1, frames = 0;
    double acc = 0.0, fps_start = (double)now_ns(), sim_ms = 0, proj_ms = 0;
    double texrow_ms = 0, raster_ms = 0, present_ms = 0;
    ViewCache live_cache; memset(&live_cache, 0, sizeof live_cache);
    long last = now_ns();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT ||
                (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) running = 0;
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_e && !e.key.repeat)
                run_block(activate_block, "activate");
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_r && !e.key.repeat) {
                simple_mode = !simple_mode;
                live_cache.valid = 0;
                printf("render mode: %s\n", simple_mode ? "simple" : "detailed");
                fflush(stdout);
            }
        }
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
        if (stationary_enabled) {
            long cf0 = now_ns();
            int miss = cache_frame(&live_cache, fb, floor_c, ceil_c, 0);
            if (miss < 0) break;
            if (sdl_present_cached(ren, tex, fb, miss) != 0) break;
            long cf1 = now_ns();
            proj_ms += (double)(cf1 - cf0) / 1e6;
            frames++;
            if (frames % 120 == 0) {
                double secs = ((double)now_ns() - fps_start) / 1e9;
                printf("frame %d stationary: frame=%.2fms fps=%.1f\n", frames,
                       proj_ms / 120, 120 / secs);
                fflush(stdout);
                sim_ms = proj_ms = texrow_ms = raster_ms = present_ms = 0;
                fps_start = (double)now_ns();
            }
            continue;
        }
        long proj_ns;
        if (render_spans(&s, &proj_ns) != 0) break;
        long st0 = now_ns();
        if (need_texrows() && get_texrows(&tr) != 0) break;
        if (get_status(&st) != 0) break;
        long st1 = now_ns();
        proj_ms += (double)proj_ns / 1e6;
        texrow_ms += (double)(st1 - st0 - proj_ns) / 1e6;
        double rm, pm;
        if (sdl_present(ren, tex, fb, &s, &det, need_texrows() ? &tr : NULL, &st,
                        floor_c, ceil_c, &rm, &pm) != 0) break;
        raster_ms += rm;
        present_ms += pm;
        frames++;
        if (frames % 120 == 0) {
            double secs = ((double)now_ns() - fps_start) / 1e9;
            Metrics m = { sim_ms / 120, proj_ms / 120, texrow_ms / 120,
                          raster_ms / 120, present_ms / 120,
                          secs * 1000.0 / 120, 0.0, r0_s1_heap_high(),
                          r0_s1_gc_count(), r0_s1_gc_live_cells() };
            printf("frame %d: ", frames); print_metrics(&m);
            int sx, sy, sa; Status ds;
            if (get_state(&sx, &sy, &sa) == 0 && get_status(&ds) == 0)
                printf("state: player=%d,%d angle=%d door=%d/%d alien=%d,%d state=%d det=%d vis=%d\n",
                       sx, sy, sa, ds.door_open, ds.door_blocked, ds.ax, ds.ay,
                       ds.state, ds.detected, ds.visible);
            fflush(stdout);
            sim_ms = proj_ms = texrow_ms = raster_ms = present_ms = 0;
            fps_start = (double)now_ns();
        }
    }
    free(fb);
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}


static int alien_demo(int state) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 2;
    SDL_Window *win = SDL_CreateWindow("Glon AVP G5i - close Alien",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       960, 600, SDL_WINDOW_SHOWN);
    if (!win) fail(SDL_GetError());
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) fail(SDL_GetError());
    apply_scale_hint();
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H);
    if (!tex) fail(SDL_GetError());
    uint32_t *fb = malloc((size_t)FB_W * FB_H * sizeof *fb);
    if (!fb) fail("out of memory");
    load_session();
    build_blocks();
    int floor_c, ceil_c;
    if (get_sector(&floor_c, &ceil_c) != 0) return 2;
    if (set_close_pose() != 0) return 2;
    if (state == 1) {
        cell w = parse_source("[ doom/alien-state: 1 ]", "walk pose");
        if (run_block(w, "walk pose") != 0) return 2;
    }
    Spans s; Detail det; TexRows tr; Status st;
    if (get_status(&st) != 0 || (need_texrows() && get_texrows(&tr) != 0)) return 2;
    int running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT ||
            (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) running = 0;
        if (render_spans(&s, NULL) != 0 || get_status(&st) != 0) break;
        double rm, pm;
        if (sdl_present(ren, tex, fb, &s, &det, need_texrows() ? &tr : NULL, &st,
                        floor_c, ceil_c, &rm, &pm) != 0) break;
        sim_tick++;
        SDL_Delay(16);
    }
    free(fb); SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}


static int objects_test(void) {
    int fails = 0;
    load_session();
    build_blocks();
    reset();
    Obj a[6], b[6];
    if (get_objects(a) != 0 || get_objects(b) != 0) return 2;
    fails += expect_eq("object record count", 6, 6);
    int deterministic = 1;
    for (int i = 0; i < 6; i++)
        if (a[i].id != b[i].id || a[i].depth != b[i].depth ||
            a[i].left != b[i].left || a[i].visible != b[i].visible) deterministic = 0;
    fails += expect_eq("object records deterministic", deterministic, 1);
    cell hit = parse_source("[ doom/hit: 0 doom/check-all 900 272 100 print doom/hit ]", "object hit");
    cell clear = parse_source("[ doom/hit: 0 doom/check-all 700 320 100 print doom/hit ]", "object clear");
    int v;
    if (run_capture(hit, &v, 1, "object hit") != 0) return 2;
    fails += expect_eq("player object collision", v, 1);
    if (run_capture(clear, &v, 1, "object clear") != 0) return 2;
    fails += expect_eq("player clear of object", v, 0);
    cell alien_hit = parse_source("[ doom/hit: 0 doom/check-all 920 270 16 print doom/hit ]", "alien object hit");
    if (run_capture(alien_hit, &v, 1, "alien object hit") != 0) return 2;
    fails += expect_eq("alien object collision", v, 1);
    printf("objects-test: %s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}


/* ---- G5l OpenGL renderer -------------------------------------------------
 * Static level geometry is a renderer-side ABI copy, uploaded once. Glon
 * supplies camera, door, light phase, object records and Alien state every
 * frame; the GPU performs projection/clipping/raster/depth/texture sampling.
 */
typedef struct { int x1,y1,x2,y2,mat; } GlWall;
static const GlWall gl_walls[10] = {
    {128,128,768,128,1},{768,128,768,256,1},{768,256,1280,256,2},
    {1280,256,1280,1024,3},{1280,1024,1152,1024,3},{1152,1024,1152,384,3},
    {1152,384,768,384,2},{768,384,768,640,1},{768,640,128,640,1},
    {128,640,128,128,1}
};
enum { GT_PANEL, GT_SERVICE, GT_FLOOR, GT_CEIL, GT_DOOR,
       GT_DRUM, GT_CRATE, GT_CABINET, GT_CONTAINER, GT_ALIEN0, GT_ALIEN_N=5, GT_COUNT=GT_ALIEN0+GT_ALIEN_N };
static GLuint gl_tex[GT_COUNT];
static SDL_Window *gl_win;
static SDL_GLContext gl_ctx;
static int gl_w, gl_h;
static int gl_door_closed = 0;

static void gl_upload(GLuint id, const uint32_t *src, int w, int h) {
    uint8_t *rgba = malloc((size_t)w * h * 4);
    if (!rgba) fail("gl texture memory");
    for (int i = 0; i < w*h; i++) {
        uint32_t c = src[i];
        rgba[i*4+0] = (uint8_t)(c >> 16); rgba[i*4+1] = (uint8_t)(c >> 8);
        rgba[i*4+2] = (uint8_t)c; rgba[i*4+3] = (uint8_t)(c >> 24);
    }
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);
}

static int gl_init(int w, int h, int shown) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return -1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    gl_win = SDL_CreateWindow("Glon AVP G5l OpenGL",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | (shown ? 0 : SDL_WINDOW_HIDDEN));
    if (!gl_win) { fprintf(stderr, "gl: window: %s\n", SDL_GetError()); return -1; }
    gl_ctx = SDL_GL_CreateContext(gl_win);
    if (!gl_ctx) { fprintf(stderr, "gl: context: %s\n", SDL_GetError()); return -1; }
    SDL_GL_SetSwapInterval(0);
    printf("gl renderer=%s vendor=%s version=%s\n",
           glGetString(GL_RENDERER), glGetString(GL_VENDOR), glGetString(GL_VERSION));
    glGenTextures(GT_COUNT, gl_tex);
    gl_upload(gl_tex[GT_PANEL], tex_panel_v2, 64,64);
    gl_upload(gl_tex[GT_SERVICE], tex_service_v2,64,64);
    gl_upload(gl_tex[GT_FLOOR], tex_floor,64,64);
    gl_upload(gl_tex[GT_CEIL], tex_ceiling_light,64,64);
    gl_upload(gl_tex[GT_DOOR], tex_door,64,64);
    gl_upload(gl_tex[GT_DRUM], obj_drum,64,96);
    gl_upload(gl_tex[GT_CRATE], obj_crate,64,96);
    gl_upload(gl_tex[GT_CABINET], obj_cabinet,64,96);
    gl_upload(gl_tex[GT_CONTAINER], obj_container,64,96);
    gl_upload(gl_tex[GT_ALIEN0+0], alien_idle,64,128);
    gl_upload(gl_tex[GT_ALIEN0+1], alien_walk1,64,128);
    gl_upload(gl_tex[GT_ALIEN0+2], alien_walk2,64,128);
    gl_upload(gl_tex[GT_ALIEN0+3], alien_attack,64,128);
    gl_upload(gl_tex[GT_ALIEN0+4], alien_side,64,128);
    gl_w=w; gl_h=h; glViewport(0,0,w,h); glClearColor(0.04,0.05,0.06,1); glEnable(GL_DEPTH_TEST);
    return 0;
}

static void gl_gv(double x,double y,double z) { glVertex3d(x/64.0, z/64.0, -y/64.0); }
static void gl_view(double px,double py,double angle) {
    /* map world (x,y,z) -> GL (x,z,-y), y up; yaw +90 - game angle */
    double ex=px/64.0, ey=1.0, ez=-py/64.0;
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef((float)(90.0 - angle*360.0/256.0), 0, 1, 0);
    glTranslatef((float)-ex, (float)-ey, (float)-ez);
}
static void gl_projection(void) {
    double aspect=(double)gl_w/(double)gl_h;
    double fovy=2.0*atan(100.0/246.0), near=0.05, far=40.0;
    double top=near*tan(fovy/2.0), right=top*aspect;
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glFrustum(-right,right,-top,top,near,far);
}
static void gl_wall(const GlWall *w, int door) {
    int mat=w->mat; GLuint t = door?GT_DOOR:(mat==1||mat==3?GT_SERVICE:GT_PANEL);
    double dx=w->x2-w->x1, dy=w->y2-w->y1, len=sqrt(dx*dx+dy*dy);
    glBindTexture(GL_TEXTURE_2D, gl_tex[t]);
    glColor3f(1,1,1);
    glBegin(GL_QUADS);
    glTexCoord2f(0,0); gl_gv(w->x1,w->y1,0);
    glTexCoord2f(len/256.0,0); gl_gv(w->x2,w->y2,0);
    glTexCoord2f(len/256.0,1); gl_gv(w->x2,w->y2,128);
    glTexCoord2f(0,1); gl_gv(w->x1,w->y1,128);
    glEnd();
}
static void gl_plane(double z, GLuint tex, double off) {
    glBindTexture(GL_TEXTURE_2D, gl_tex[tex]);
    glBegin(GL_QUADS);
    glTexCoord2f(128/256.0,128/256.0); gl_gv(128,128,z);
    glTexCoord2f(1280/256.0,128/256.0); gl_gv(1280,128,z);
    glTexCoord2f(1280/256.0,1024/256.0); gl_gv(1280,1024,z);
    glTexCoord2f(128/256.0,1024/256.0); gl_gv(128,1024,z);
    glEnd();
}
static void gl_billboard(GLuint tex, double x,double y,double wworld,double hworld) {
    double a=0; (void)a;
    /* camera-facing axes in world/game coordinates */
    double ca=cos(st_current_angle*2.0*M_PI/256.0), sa=sin(st_current_angle*2.0*M_PI/256.0);
    double rx=sa, ry=-ca; /* right */
    double cx=x/64.0, cy=y/64.0, cz=hworld/2.0;
    glBindTexture(GL_TEXTURE_2D, tex); glColor3f(1,1,1);
    glBegin(GL_QUADS);
    glTexCoord2f(0,0); gl_gv(x - rx*wworld*32, y - ry*wworld*32, 0);
    glTexCoord2f(1,0); gl_gv(x + rx*wworld*32, y + ry*wworld*32, 0);
    glTexCoord2f(1,1); gl_gv(x + rx*wworld*32, y + ry*wworld*32, hworld*64);
    glTexCoord2f(0,1); gl_gv(x - rx*wworld*32, y - ry*wworld*32, hworld*64);
    glEnd();
    (void)cx;(void)cy;(void)cz;
}
static void gl_scene(const Status *st, const Obj *objs, int nobj) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl_projection(); gl_view(last_px, last_py, st_current_angle);
    glEnable(GL_TEXTURE_2D); glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
    gl_plane(0,GT_FLOOR,0);
    float l=0.70f + 0.16f*(float)sin((double)st->light*0.09);
    glColor3f(l,l,l); gl_plane(128,GT_CEIL,0); glColor3f(1,1,1);
    for (int i=0;i<10;i++) gl_wall(&gl_walls[i],0);
    if (st->door_blocked) {
        GlWall d={768,256,768,384,5}; gl_wall(&d,1);
    }
    /* localized illuminated ceiling strips; Glon supplies light phase */
    glDisable(GL_TEXTURE_2D); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE);
    glDepthMask(GL_FALSE);
    float pulse=0.08f+0.08f*(float)((sin((double)st->light*0.09)+1.0)*0.5);
    glColor4f(1.0f,0.82f,0.52f,pulse);
    for (int i=0;i<4;i++) {
        double x=400+i*220;
        glBegin(GL_QUADS); gl_gv(x-40,300,127); gl_gv(x+40,300,127); gl_gv(x+40,360,127); gl_gv(x-40,360,127); glEnd();
    }
    glColor4f(1,1,1,1); glDisable(GL_BLEND); glDepthMask(GL_TRUE);
    /* depth-tested, alpha-tested billboards */
    glEnable(GL_TEXTURE_2D); glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER,0.25f);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    for (int i=0;i<nobj;i++) {
        if (!objs[i].visible) continue;
        GLuint t = GT_DRUM + (objs[i].id == 0 ? 0 : objs[i].id == 1 ? 1 : objs[i].id == 2 ? 2 : 3);
        glDepthMask(GL_TRUE);
        gl_billboard(t, objs[i].x, objs[i].y, objs[i].hw*2.0/64.0, objs[i].h/64.0);
    }
    if (st->visible) {
        const uint32_t *f = alien_frame_for(st);
        int idx = f==alien_walk1?1:f==alien_walk2?2:f==alien_attack?3:f==alien_side?4:0;
        glDepthMask(GL_TRUE);
        gl_billboard(gl_tex[GT_ALIEN0+idx], st->ax, st->ay, 0.75, 1.5);
    }
    glDisable(GL_ALPHA_TEST); glDisable(GL_BLEND);
    glFinish();
}

static int gl_bench(int frames, int w, int h) {
    if (gl_init(w,h,0) != 0) return 2;
    load_session(); build_blocks();
    Spans s; Detail det; TexRows tr; Status st; Obj objs[6];
    double sim=0, gpu=0, total=0; int n=0;
    for (int i=0;i<frames;i++) {
        long t0=now_ns();
        if (tick(i&63)!=0) return 2;
        int px,py,pa; if (get_state(&px,&py,&pa)!=0) return 2; st_current_angle=pa;
        if (get_status(&st)!=0) return 2;
        if (objects_mode) memcpy(objs,current_objs,sizeof current_objs);
        long t1=now_ns();
        int nobj=objects_mode?6:0;
        gl_scene(&st,objs,nobj);
        long t2=now_ns(); SDL_GL_SwapWindow(gl_win); long t3=now_ns();
        sim+=(t1-t0)/1e6; gpu+=(t2-t1)/1e6; total+=(t3-t0)/1e6; n++;
    }
    printf("gl bench renderer=%s resolution=%dx%d frames=%d sim=%.2fms submit=%.2fms total=%.2fms fps=%.1f loader=%ld heap=%ld\n",
           glGetString(GL_RENDERER), w,h,n,sim/n,gpu/n,total/n,1000.0/(total/n),(long)M[GC_LOADER_HP],r0_s1_heap_high());
    SDL_GL_DeleteContext(gl_ctx); SDL_DestroyWindow(gl_win); SDL_Quit();
    return 0;
}

static void gl_fixed_pose(void) {
    reset();
    if (alien_cover) { if (set_close_pose()!=0) fail("gl cover pose"); return; }
    cell p=parse_source("[ doom/px: 616 doom/py: 320 doom/angle: 0 doom/aim doom/door-open: 48 doom/door-target: 64 doom/door-blocked: 0 doom/alien-x: 990 doom/alien-y: 320 doom/alien-state: 1 doom/alien-health: 100 doom/alien-detected: 1 ]", "gl pose");
    if (run_block(p,"gl pose")!=0) fail("gl pose");
}
static int gl_demo(int w,int h) {
    if (gl_init(w,h,1)!=0) return 2;
    load_session(); build_blocks();
    if (gl_door_closed) reset(); else gl_fixed_pose();
    Spans s; Detail det; TexRows tr; Status st; Obj objs[6];
    int px,py,pa; get_state(&px,&py,&pa); st_current_angle=pa;
    int running=1;
    while(running){ SDL_Event e; while(SDL_PollEvent(&e)) if(e.type==SDL_QUIT||(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_ESCAPE))running=0;
        tick(0); get_state(&px,&py,&pa); st_current_angle=pa; get_status(&st); int nobj=0; if(objects_mode) {get_objects(objs);nobj=6;}
        gl_scene(&st,objs,nobj); SDL_GL_SwapWindow(gl_win); sim_tick++; SDL_Delay(16);
    }
    SDL_GL_DeleteContext(gl_ctx); SDL_DestroyWindow(gl_win); SDL_Quit(); return 0;
}
static int gl_shots(const char *dir,int w,int h) {
    if (gl_init(w,h,0)!=0) return 2; load_session(); build_blocks(); gl_fixed_pose();
    Status st; Obj objs[6]; int px,py,pa; get_state(&px,&py,&pa); st_current_angle=pa; get_status(&st); int nobj=0;
    if(objects_mode){get_objects(objs);nobj=6;}
    gl_scene(&st,objs,nobj);
    uint8_t *buf=malloc((size_t)w*h*4); if(!buf)return 2;
    glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,buf);
    char path[4096]; snprintf(path,sizeof path,"%s/corridor.ppm",dir);
    FILE *f=fopen(path,"wb"); if(!f)return 2; fprintf(f,"P6\n%d %d\n255\n",w,h);
    for(int y=h-1;y>=0;y--) for(int x=0;x<w;x++){uint8_t*p=buf+(y*w+x)*4;fwrite(p,1,3,f);} fclose(f); free(buf);
    printf("gl shot %dx%d %s\n",w,h,path);
    SDL_GL_DeleteContext(gl_ctx); SDL_DestroyWindow(gl_win); SDL_Quit(); return 0;
}

int main(int argc, char **argv) {
    const char *env = getenv("DOOM_ROOT");
    if (env && *env) root_dir = env;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--raw-wall") == 0) raw_wall_mode = 1;
        if (strcmp(argv[i], "--checker") == 0) checker_walls = 1;
        if (strcmp(argv[i], "--new-textures") == 0) new_textures = 1;
        if (strcmp(argv[i], "--smooth-shading") == 0) smooth_shading = 1;
        if (strcmp(argv[i], "--flat-surfaces") == 0) flat_surfaces = 1;
        if (strcmp(argv[i], "--renderer") == 0 && i+1<argc && strcmp(argv[i+1],"gl")==0) { /* routed below */ }
        if (strcmp(argv[i], "--objects") == 0) objects_mode = 1;
        if (strcmp(argv[i], "--alien-cover") == 0) { objects_mode = 1; alien_cover = 1; }
        if (strcmp(argv[i], "--demo-partial") == 0) { objects_mode = 1; alien_cover = 1; cover_view = 1; }
        if (strcmp(argv[i], "--demo-emerge") == 0) { objects_mode = 1; alien_cover = 1; cover_view = 2; }
        if (strcmp(argv[i], "--demo-side") == 0) { objects_mode = 1; alien_cover = 1; cover_view = 3; }
        if (strcmp(argv[i], "--gl-door") == 0) gl_door_closed = 1;
        if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc &&
            strcmp(argv[i + 1], "linear") == 0) linear_filter = 1;
    }
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--objects-test") == 0) { objects_mode = 1; return objects_test(); }
    if (argc > 1 && strcmp(argv[1], "--demo-close") == 0) return alien_demo(2);
    if (argc > 1 && strcmp(argv[1], "--demo-walk") == 0) return alien_demo(1);
    if (argc > 2 && strcmp(argv[1], "--raw-wall") == 0) {
        if (strcmp(argv[2], "--self-test") == 0) return self_test();
        if (strcmp(argv[2], "--loader-report") == 0) return loader_report();
        if (strcmp(argv[2], "--bench") == 0) {
            int bench_alien = 0;
            for (int i = 4; i < argc; i++) {
                if (strcmp(argv[i], "simple") == 0) simple_mode = 1;
                if (strcmp(argv[i], "alien") == 0) bench_alien = 1;
            }
            return bench(atoi(argv[3]), bench_alien);
        }
    }
    if (argc > 2 && strcmp(argv[1], "--gl-bench") == 0) {
        int w=argc>3?atoi(argv[3]):640, h=argc>4?atoi(argv[4]):400;
        for(int i=5;i<argc;i++) if(strcmp(argv[i],"objects")==0) objects_mode=1;
        return gl_bench(atoi(argv[2]),w,h);
    }
    if (argc > 1 && strcmp(argv[1], "--gl-demo") == 0) {
        int w=argc>2?atoi(argv[2]):640, h=argc>3?atoi(argv[3]):400;
        for(int i=4;i<argc;i++) if(strcmp(argv[i],"objects")==0) objects_mode=1;
        return gl_demo(w,h);
    }
    if (argc > 2 && strcmp(argv[1], "--gl-shots") == 0) {
        int w=argc>3?atoi(argv[3]):640, h=argc>4?atoi(argv[4]):400;
        for(int i=5;i<argc;i++) if(strcmp(argv[i],"objects")==0) objects_mode=1;
        return gl_shots(argv[2],w,h);
    }
    if (argc > 1 && strcmp(argv[1], "--self-test") == 0) return self_test();
    if (argc > 1 && strcmp(argv[1], "--projection-test") == 0) {
        int rc = projection_test();
        fprintf(stderr, "glon-doom: projection-test %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "--trace") == 0) return scripted_trace();
    if (argc > 2 && strcmp(argv[1], "--shots") == 0) return shot_walkthrough(argv[2]);
    if (argc > 1 && strcmp(argv[1], "--stationary-test") == 0) return stationary_test();
    if (argc > 1 && strcmp(argv[1], "--loader-report") == 0) return loader_report();
    if (argc > 1 && strcmp(argv[1], "--simple") == 0) simple_mode = 1;
    if (argc > 1 && strcmp(argv[1], "--stationary") == 0) stationary_enabled = 1;
    if (argc > 2 && strcmp(argv[1], "--bench") == 0) {
        int bench_alien = 0;
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "simple") == 0) simple_mode = 1;
            if (strcmp(argv[i], "alien") == 0) bench_alien = 1;
        }
        return bench(atoi(argv[2]), bench_alien);
    }
    if (argc > 2 && strcmp(argv[1], "--bench-fixed") == 0) {
        int frames = atoi(argv[2]);
        int i = 3;
        int x = 320, y = 320, a = 0;
        if (i < argc && strcmp(argv[i], "simple") == 0) { simple_mode = 1; i++; }
        if (i + 2 < argc) { x = atoi(argv[i]); y = atoi(argv[i + 1]); a = atoi(argv[i + 2]); }
        return bench_fixed(frames, x, y, a);
    }
    if (argc > 2 && strcmp(argv[1], "--stationary-bench") == 0) {
        int throttle = argc > 3 && strcmp(argv[3], "throttle") == 0;
        return stationary_bench(atoi(argv[2]), throttle);
    }
#ifdef DOOM_S1_PROFILE
    if (argc > 2 && strcmp(argv[1], "--opcode-bench") == 0)
        return opcode_bench(atoi(argv[2]));
#endif
#ifdef DOOM_S1_PROFILE
    if (argc > 2 && strcmp(argv[1], "--opcode-bench-simple") == 0) {
        simple_mode = 1;
        return opcode_bench(atoi(argv[2]));
    }
#endif
    return interactive();
}
