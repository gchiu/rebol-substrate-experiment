/* Glon AVP G5b - native SDL2 host for the Marine/Alien corridor.
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
    int door_open, door_blocked, door_target;
    int ax, ay, health, state, detected;
    int td2, tside, tdepth;
    int visible, left, right, top, bot;
} Status;

typedef struct {
    double sim_ms, proj_ms, present_ms, frame_ms;
    long heap_high, gc_count, live_cells;
} Metrics;

static const char *root_dir = "../..";
static cell reset_block, sector_block, report_block, valid_block, collision_block;
static cell activate_block, status_block;
static cell alien_collision_block, los_wall_block, los_clear_block;
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
    activate_block = parse_source("[ doom/activate ]", "activate");
    status_block = parse_source("[ do doom/status-block ]", "status");

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
static int get_status(Status *st) {
    int v[16];
    if (run_capture(status_block, v, 16, "status") != 0) return -1;
    st->door_open = v[0]; st->door_blocked = v[1]; st->door_target = v[2];
    st->ax = v[3]; st->ay = v[4]; st->health = v[5]; st->state = v[6];
    st->detected = v[7]; st->td2 = v[8]; st->tside = v[9]; st->tdepth = v[10];
    st->visible = v[11]; st->left = v[12]; st->right = v[13];
    st->top = v[14]; st->bot = v[15];
    return 0;
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

    reset(); if (render_spans(&s, &proj_ns) != 0) return 2;
    int bounds = 1;
    for (int i = 0; i < PROJ_COLS; i++)
        if (s.top[i] < 0 || s.bot[i] > FB_H - 1 || s.top[i] > s.bot[i] ||
            s.color[i] < 1 || s.color[i] > 4) bounds = 0;
    fails += expect_eq("framebuffer spans", bounds, 1);
    fails += expect_eq("edge wall visible", s.color[0], 1);

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

/* ---- SDL presentation -------------------------------------------------- */

static uint32_t palette(int id) {
    switch (id) {
    case 1: return 0xff8b3636u;
    case 2: return 0xff676b73u;
    case 3: return 0xff3a5a8eu;
    case 4: return 0xff8a6a3au;
    case 6: return 0xff5b4a32u;
    case 7: return 0xff222630u;
    default: return 0xff101010u;
    }
}

static void plot(uint32_t *fb, int x, int y, uint32_t c) {
    if (x >= 0 && x < FB_W && y >= 0 && y < FB_H) fb[y * FB_W + x] = c;
}

/* A deliberately simple, original Alien silhouette. Glon supplies visibility
 * and the projected screen rectangle; this function only rasterises those
 * pixels. */
static void draw_alien_sprite(uint32_t *fb, const Status *st) {
    if (!st->visible) return;
    int l = st->left, r = st->right, t = st->top, b = st->bot;
    if (l < 0) l = 0;
    if (r >= FB_W) r = FB_W - 1;
    if (t < 0) t = 0;
    if (b >= FB_H) b = FB_H - 1;
    if (l >= r || t >= b) return;
    int w = r - l + 1, h = b - t + 1, cx = (l + r) / 2;
    uint32_t body = 0xff4f7b48u, dark = 0xff294027u, eye = 0xffdf4d4du;
    int head_h = h * 45 / 100;
    for (int y = t; y <= b; y++) {
        int yy = y - t;
        int hw;
        if (yy < head_h) {
            int rem = head_h - yy;
            hw = w * (28 + 22 * (yy < head_h / 2 ? yy : rem) / (head_h / 2 + 1)) / 100;
        } else {
            int rem = b - y;
            hw = w * (32 + 14 * rem / (h - head_h + 1)) / 100;
        }
        if (hw < 1) hw = 1;
        for (int x = cx - hw; x <= cx + hw; x++) plot(fb, x, y, body);
        plot(fb, cx - hw, y, dark);
        plot(fb, cx + hw, y, dark);
    }
    int eye_y = t + h * 22 / 100;
    int eye_dx = w / 12;
    for (int dy = 0; dy < 2; dy++)
        for (int dx = 0; dx < 2; dx++) {
            plot(fb, cx - eye_dx + dx, eye_y + dy, eye);
            plot(fb, cx + eye_dx + dx, eye_y + dy, eye);
        }
}

static void draw_tracker(uint32_t *fb, const Status *st) {
    const int cx = 42, cy = 168, rad = 22;
    for (int y = 142; y < 196; y++)
        for (int x = 6; x < 78; x++) fb[y * FB_W + x] = 0xff0d1117u;
    /* Door status: green when open, amber when blocked. */
    uint32_t door_c = st->door_blocked ? 0xff9a6b35u : 0xff3eaa58u;
    for (int y = 146; y < 150; y++)
        for (int x = 10; x < 74; x++) fb[y * FB_W + x] = door_c;
    /* Tracker ring, forward tick and crosshair. */
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

static void draw_buffer(uint32_t *fb, const Spans *s, const Status *st,
                        int floor_c, int ceil_c) {
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
    draw_alien_sprite(fb, st);
    draw_tracker(fb, st);
}

static int sdl_present(SDL_Renderer *ren, SDL_Texture *tex, uint32_t *fb,
                       const Spans *s, const Status *st, int floor_c, int ceil_c,
                       double *present_ms) {
    draw_buffer(fb, s, st, floor_c, ceil_c);
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
    SDL_Window *win = SDL_CreateWindow("Glon AVP G5b - benchmark",
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
    Status st;
    if (get_status(&st) != 0) return 2;
    Metrics sum = {0};
    long wall0 = now_ns();
    for (int i = 0; i < frames; i++) {
        long t0 = now_ns();
        if (tick(i & 63) != 0) return 2;
        long t1 = now_ns();
        long proj_ns;
        if (render_spans(&s, &proj_ns) != 0) return 2;
        if (get_status(&st) != 0) return 2;
        long t2 = now_ns();
        double present_ms;
        if (sdl_present(ren, tex, fb, &s, &st, floor_c, ceil_c, &present_ms) != 0) return 2;
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
    SDL_Window *win = SDL_CreateWindow("Glon AVP G5b | W/S/A/D move  Left/Right turn  E door  Esc quit",
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
    Status st;
    if (get_status(&st) != 0) return 2;
    int running = 1, frames = 0;
    double acc = 0.0, fps_start = (double)now_ns(), sim_ms = 0, proj_ms = 0, present_ms = 0;
    long last = now_ns();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT ||
                (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) running = 0;
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_e && !e.key.repeat)
                run_block(activate_block, "activate");
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
        long proj_ns;
        if (render_spans(&s, &proj_ns) != 0) break;
        long st0 = now_ns();
        if (get_status(&st) != 0) break;
        proj_ms += (double)(proj_ns + now_ns() - st0) / 1e6;
        double pm;
        if (sdl_present(ren, tex, fb, &s, &st, floor_c, ceil_c, &pm) != 0) break;
        present_ms += pm;
        frames++;
        if (frames % 120 == 0) {
            double secs = ((double)now_ns() - fps_start) / 1e9;
            Metrics m = { sim_ms / 120, proj_ms / 120, present_ms / 120,
                          secs * 1000.0 / 120, r0_s1_heap_high(),
                          r0_s1_gc_count(), r0_s1_gc_live_cells() };
            printf("frame %d: ", frames); print_metrics(&m);
            int sx, sy, sa; Status ds;
            if (get_state(&sx, &sy, &sa) == 0 && get_status(&ds) == 0)
                printf("state: player=%d,%d angle=%d door=%d/%d alien=%d,%d state=%d det=%d vis=%d\n",
                       sx, sy, sa, ds.door_open, ds.door_blocked, ds.ax, ds.ay,
                       ds.state, ds.detected, ds.visible);
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
