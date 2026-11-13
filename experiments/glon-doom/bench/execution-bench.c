/* G5g isolated execution benchmarks.
 *
 * This program is deliberately outside the AvP game loader. It measures:
 *   - frozen S1 micro-loops assembled directly with the s1.h assembler API;
 *   - equivalent Glon recursion workloads through the normal evaluator;
 *   - an isolated 160-ray / 10-wall Glon workload;
 *   - native C loop floors for comparison.
 *
 * The native numbers are algorithmic floors. They are NOT a measurement of
 * "interpreter dispatch alone": recursion, lookup, source representation and
 * stack discipline differ between the Glon and C forms.
 */
#define _GNU_SOURCE
#include "r0_s1.h"
#include "m1_layout.h"
#include "s1.h"
#include "s1_prof.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile uintptr_t sink;
static long bench_int(cell v) { return (long)(r0_untag(v) / 16); }

static long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000L + ts.tv_nsec;
}

static int dcmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median(double *v, int n) {
    qsort(v, (size_t)n, sizeof *v, dcmp);
    return n & 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) * 0.5;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n < 0 || n > (1L << 20)) { fclose(f); return NULL; }
    rewind(f);
    char *b = malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    b[n] = 0;
    /* strip ;; comments, as the host does */
    char *r = b, *w = b;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') {
            while (*r && *r != '\n') r++;
            continue;
        }
        *w++ = *r++;
    }
    *w = 0;
    return b;
}

static int load_glon(const char *path) {
    char *src = read_file(path);
    if (!src) { fprintf(stderr, "bench: cannot read %s\n", path); return -1; }
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    free(src);
    if (err) {
        r0_s1_outcome oc;
        r0_s1_session_parse_error(&oc);
        fprintf(stderr, "bench: parse %s: %s\n", path, r0_s1_outcome_detail_name(oc.detail));
        return -1;
    }
    r0_s1_outcome oc;
    r0_s1_session_run_block(b, &oc);
    if (oc.status != R0S1_OUT_OK) {
        fprintf(stderr, "bench: load %s: %s\n", path,
                r0_s1_outcome_detail_name(oc.detail));
        return -1;
    }
    return 0;
}

static cell parse_block(const char *path) {
    char *src = read_file(path);
    if (!src) return R0_NONE;
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    free(src);
    return err ? R0_NONE : b;
}

/* ---- S1 microbenchmarks ------------------------------------------------ */

static void init_runtime(void) {
    cell main_entry = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = main_entry;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
}

enum { S1_EMPTY, S1_ADD, S1_MUL, S1_DIV, S1_BRANCH, S1_MEM };

static cell build_s1_loop(int kind, int n) {
    asm_reset();
    s1_reset();
    M[20] = 0; M[21] = 1; M[22] = 0;
    asm_lit(n); asm_lit(20); asm_store();
    cell loop = asm_here();
    asm_lit(20); asm_fetch();
    cell end = asm_zbranch_fwd();
    switch (kind) {
    case S1_ADD: asm_lit(1); asm_lit(2); asm_host(HOST_ADD); asm_drop(); break;
    case S1_MUL: asm_lit(1000); asm_lit(3); asm_host(HOST_MUL); asm_drop(); break;
    case S1_DIV: asm_lit(1000000); asm_lit(3); asm_host(HOST_DIV); asm_drop(); break;
    case S1_BRANCH:
        asm_lit(1); asm_lit(1); asm_host(HOST_EQ);
        cell skip = asm_zbranch_fwd();
        asm_lit(1); asm_drop();
        asm_patch_here(skip);
        asm_lit(0); asm_lit(1); asm_host(HOST_EQ);
        cell skip2 = asm_zbranch_fwd();
        asm_lit(1); asm_drop();
        asm_patch_here(skip2);
        break;
    case S1_MEM:
        asm_lit(22); asm_fetch(); asm_lit(1); asm_host(HOST_ADD);
        asm_lit(22); asm_store(); break;
    default: break;
    }
    asm_lit(20); asm_fetch(); asm_lit(1); asm_host(HOST_SUB);
    asm_lit(20); asm_store();
    asm_branch(loop);
    asm_patch_here(end);
    asm_lit(21); asm_fetch();
    asm_halt();
    return asm_here();
}

static void run_s1_suite(int n, int reps) {
    static const char *name[] = {"empty", "add", "mul", "div", "branch", "memory"};
    printf("s1 n=%d reps=%d\n", n, reps);
    for (int k = S1_EMPTY; k <= S1_MEM; k++) {
        double t[32];
        if (reps > 32) reps = 32;
        build_s1_loop(k, n);
        for (int r = 0; r < reps + 2; r++) {
            long a = now_ns();
            s1_reset();
            M[20] = 0; M[21] = 1; M[22] = 0;
            s1_run(256);
            long b = now_ns();
            if (r >= 2) t[r - 2] = (double)(b - a) / n;
        }
        double med = median(t, reps);
        printf("s1 %-6s ns/iter=%.3f checksum=%ld\n", name[k], med, (long)M[21]);
        sink += (uintptr_t)M[21];
    }
}

/* ---- Glon macrobenchmarks --------------------------------------------- */

static int run_glon(cell block, int reps, const char *name, int levels) {
    r0_s1_outcome oc;
    long t0 = now_ns();
    int n = 0;
    for (int i = 0; i < reps; i++) {
        r0_s1_session_run_block(block, &oc);
        if (oc.status != R0S1_OUT_OK) {
            fprintf(stderr, "bench: %s: %s\n", name, r0_s1_outcome_detail_name(oc.detail));
            return -1;
        }
        n = oc.count;
    }
    long t1 = now_ns();
    cell sum = 0;
    for (int i = 0; i < n; i++) sum += bench_int(r0_s1_result(i, n));
    printf("glon %-7s reps=%d levels=%d ns/call=%.1f ns/level=%.2f results=%d checksum=%ld\n",
           name, reps, levels, (double)(t1 - t0) / reps,
           levels ? (double)(t1 - t0) / reps / levels : 0.0, n, (long)sum);
    return 0;
}

static void run_glon_suite(int reps) {
    init_runtime();
    if (load_glon("../../../jupyter/prelude.glon") != 0) exit(2);
    if (load_glon("arithmetic.glon") != 0) exit(2);
    struct { const char *src, *name; int levels; } w[] = {
        {"[ b/add 60 0 ]", "add", 60},
        {"[ b/mul 60 1 ]", "mul", 60},
        {"[ b/div 60 1000000 ]", "div", 60},
        {"[ b/branch 60 1 ]", "branch", 60},
        {"[ b/memory 60 0 ]", "memory", 60},
        {"[ b/tree 8 ]", "tree", 256},
    };
    for (size_t i = 0; i < sizeof w / sizeof w[0]; i++) {
        int err = 0;
        cell b = r0_s1_parse(w[i].src, &err);
        if (err) { fprintf(stderr, "bench: parse %s\n", w[i].name); continue; }
        run_glon(b, reps, w[i].name, w[i].levels);
    }
}

/* ---- Isolated 160-ray wall workload ----------------------------------- */

static intptr_t native_wall(int sx) {
    intptr_t px = 320, py = 320, dirx = 64, diry = 0, planex = 0, planey = -42;
    intptr_t rayx = dirx + planex * (2 * sx - 319) / 319;
    intptr_t rayy = diry + planey * (2 * sx - 319) / 319;
    intptr_t best = 100000, bc = 0, t, hx, hy;
#define CHECK_H(a,b,c,d,e,f) do { \
    if (rayy != 0) { t = (a - py) * 64 / rayy; \
        if (t > 0) { hx = px + t * rayx / 64; \
            if (hx >= b && hx <= c && t < best) { best = t; bc = d; } } } \
    (void)e; (void)f; } while (0)
#define CHECK_V(a,b,c,d,e,f) do { \
    if (rayx != 0) { t = (a - px) * 64 / rayx; \
        if (t > 0) { hy = py + t * rayy / 64; \
            if (hy >= b && hy <= c && t < best) { best = t; bc = d; } } } \
    (void)e; (void)f; } while (0)
    CHECK_H(128, 128, 768, 1, 0, 0);
    CHECK_V(768, 128, 256, 1, 0, 0);
    CHECK_H(256, 768, 1280, 2, 0, 0);
    CHECK_V(1280, 256, 1024, 3, 0, 0);
    CHECK_H(1024, 1152, 1280, 3, 0, 0);
    CHECK_V(1152, 384, 1024, 3, 0, 0);
    CHECK_H(384, 768, 1152, 2, 0, 0);
    CHECK_V(768, 384, 640, 1, 0, 0);
    CHECK_H(640, 128, 768, 1, 0, 0);
    CHECK_V(128, 128, 640, 1, 0, 0);
#undef CHECK_H
#undef CHECK_V
    intptr_t half = 246 * 64 / (best < 1 ? 1 : best);
    intptr_t top = 100 - half, bot = 100 + half;
    if (top < 0) top = 0;
    if (bot > 199) bot = 199;
    return best + bc + top;
}

static void run_wall_suite(int reps) {
    init_runtime();
    if (load_glon("../../../jupyter/prelude.glon") != 0) exit(2);
    if (load_glon("wall.glon") != 0) exit(2);
    cell block = parse_block("wall-loop.glon");
    if (block == R0_NONE) exit(2);
    double t[32];
    if (reps > 32) reps = 32;
    long gsum = 0;
    for (int r = 0; r < reps + 2; r++) {
        long a = now_ns();
        r0_s1_outcome oc;
        r0_s1_session_run_block(block, &oc);
        long b = now_ns();
        if (oc.status != R0S1_OUT_OK) { fprintf(stderr, "bench: wall failed\n"); exit(2); }
        if (r == reps + 1 && oc.count > 0)
            gsum = bench_int(r0_s1_result(0, oc.count));
        if (r >= 2) t[r - 2] = (double)(b - a) / 1e6;
    }
    long nsum = 0;
    const int native_frames = 1000;
    long na = now_ns();
    for (int f = 0; f < native_frames; f++) {
        nsum = 0;
        for (int i = 0; i < 160; i++) nsum += native_wall(i * 2 + 1);
    }
    long nb = now_ns();
    double native_ms = (double)(nb - na) / 1e6 / native_frames;
    printf("wall glon_rays=160 wall_checks=1600 ns/frame=%.1f ms=%.3f checksum=%ld\n",
           median(t, reps) * 1e6, median(t, reps), gsum);
    printf("wall native_rays=160 wall_checks=1600 ms=%.6f checksum=%ld\n",
           native_ms, nsum);
    printf("wall checksums_equal=%d\n", gsum == nsum);
}


/* ---- Prepared (masm) wall prototype ----------------------------------- */

static intptr_t native_wall5(int sx, int px, int py, int dirx, int diry,
                             int planex, int planey, int door, int out[5]) {
    intptr_t rayx = dirx + planex * (2 * sx - 319) / 319;
    intptr_t rayy = diry + planey * (2 * sx - 319) / 319;
    intptr_t best = 100000, packed = 0, t, hx, hy;
#define HCHECK(coord,lo,hi,pack) do { \
    if (rayy != 0) { t = ((coord) - py) * 64 / rayy; \
      if (t > 0) { hx = px + t * rayx / 64; \
        if (hx >= (lo) && hx <= (hi) && t < best) { best = t; packed = (pack); } } } } while (0)
#define VCHECK(coord,lo,hi,pack) do { \
    if (rayx != 0) { t = ((coord) - px) * 64 / rayx; \
      if (t > 0) { hy = py + t * rayy / 64; \
        if (hy >= (lo) && hy <= (hi) && t < best) { best = t; packed = (pack); } } } } while (0)
    HCHECK(128,128,768,8);
    VCHECK(768,128,256,9);
    HCHECK(256,768,1280,18);
    VCHECK(1280,256,1024,28);
    HCHECK(1024,1152,1280,24);
    VCHECK(1152,384,1024,27);
    HCHECK(384,768,1152,18);
    VCHECK(768,384,640,9);
    HCHECK(640,128,768,8);
    VCHECK(128,128,640,11);
    if (door) VCHECK(768,256,384,37);
#undef HCHECK
#undef VCHECK
    if (packed == 0) {
        out[0] = 100; out[1] = 100; out[2] = 0; out[3] = 0; out[4] = 0;
        return best;
    }
    int color = (int)(packed / 8), bmat = (int)(packed % 8);
    int u = (bmat == 0 || bmat == 2)
          ? (int)(px + best * rayx / 64)
          : (int)(py + best * rayy / 64);
    int half = (int)(246 * 64 / best);
    int top = 100 - half, bot = 100 + half;
    if (top < 0) top = 0;
    if (bot > 199) bot = 199;
    int shade = best < 256 ? 3 : best < 512 ? 2 : best < 1024 ? 1 : 0;
    out[0] = top; out[1] = bot; out[2] = color; out[3] = u;
    out[4] = bmat * 4 + shade;
    return best;
}

typedef struct { FILE *saved, *stream; char *buf; size_t len; } BenchCapture;
static void cap_begin(BenchCapture *c) {
    memset(c, 0, sizeof *c);
    c->saved = stdout;
    c->stream = open_memstream(&c->buf, &c->len);
    if (!c->stream) { perror("open_memstream"); exit(2); }
    stdout = c->stream;
}
static void cap_end(BenchCapture *c) {
    fflush(stdout);
    fclose(c->stream);
    stdout = c->saved;
}

static void run_prepared_suite(int reps) {
    init_runtime();
    long l0 = now_ns();
    if (load_glon("../../../jupyter/prelude.glon") != 0) exit(2);
    if (load_glon("prepared.glon") != 0) exit(2);
    long l1 = now_ns();
    printf("prepared load_ms=%.2f code_cells=%ld asm_here=%ld loader_used=%ld loader_free=%ld\n",
           (double)(l1 - l0) / 1e6, r0_s1_code_size(), (long)asm_here(),
           (long)M[GC_LOADER_HP] - R0S1_HEAP_BASE,
           R0S1_HEAP_LIMIT - (long)M[GC_LOADER_HP]);
    struct { int sx, px, py, dirx, diry, planex, planey, door; } cases[] = {
        {1,320,320,64,0,0,-42,0},
        {319,320,320,64,0,0,-42,0},
        {160,320,320,64,0,0,-42,0},
        {1,320,320,0,64,42,0,0},
        {319,320,320,0,64,42,0,1},
        {1,320,320,1,0,0,0,0},       /* rayx=1, rayy=0 */
        {1,320,320,0,1,0,0,0},       /* rayx=0, rayy=1 */
        {1,128,128,64,0,0,-42,1},
        {1,1280,1024,64,0,0,-42,1},
        {3,900,500,64,0,0,-42,0}
    };
    int failures = 0;
    for (size_t ci = 0; ci < sizeof cases / sizeof cases[0]; ci++) {
        char src[256];
        snprintf(src, sizeof src, "[ w/ray-at %d %d %d %d %d %d %d %d ]",
                 cases[ci].sx, cases[ci].px, cases[ci].py, cases[ci].dirx,
                 cases[ci].diry, cases[ci].planex, cases[ci].planey,
                 cases[ci].door);
        int err = 0; cell b = r0_s1_parse(src, &err);
        if (err) { printf("prepared case %zu parse error\n", ci); failures++; continue; }
        r0_s1_outcome oc; r0_s1_session_run_block(b, &oc);
        int got[5], want[5];
        if (oc.status != R0S1_OUT_OK || oc.count != 5) {
            printf("prepared case %zu status=%d count=%d\n", ci, oc.status, oc.count);
            failures++; continue;
        }
        for (int i = 0; i < 5; i++) got[i] = (int)bench_int(r0_s1_result(i, oc.count));
        native_wall5(cases[ci].sx, cases[ci].px, cases[ci].py, cases[ci].dirx,
                     cases[ci].diry, cases[ci].planex, cases[ci].planey,
                     cases[ci].door, want);
        int ok = memcmp(got, want, sizeof got) == 0;
        if (!ok) {
            printf("prepared case %zu got=%d,%d,%d,%d,%d want=%d,%d,%d,%d,%d\n",
                   ci, got[0],got[1],got[2],got[3],got[4],
                   want[0],want[1],want[2],want[3],want[4]);
            failures++;
        }
    }
    printf("prepared correctness cases=%zu failures=%d\n",
           sizeof cases / sizeof cases[0], failures);
    /* Repeated single-ray execution. */
    int err = 0; cell b = r0_s1_parse("[ w/ray-at 1 320 320 64 0 0 -42 0 ]", &err);
    double t[32]; if (reps > 32) reps = 32;
    for (int r = 0; r < reps + 2; r++) {
        long a = now_ns();
        r0_s1_outcome oc; r0_s1_session_run_block(b, &oc);
        long e = now_ns();
        if (oc.status != R0S1_OUT_OK) { printf("prepared timed run failed\n"); exit(2); }
        if (r >= 2) t[r - 2] = (double)(e - a) / 1e3;
    }
    printf("prepared single_ray_us=%.1f\n", median(t, reps));
    /* Full 160-ray / 10-wall frame through the same workers. */
    cell frame = parse_block("prepared-frame.glon");
    if (frame == R0_NONE) { printf("prepared frame parse failed\n"); exit(2); }
    BenchCapture cap; cap_begin(&cap);
    long a = now_ns();
    r0_s1_outcome fo; r0_s1_session_run_block(frame, &fo);
    long e = now_ns();
    cap_end(&cap);
    if (fo.status != R0S1_OUT_OK) { printf("prepared frame failed %d\n", fo.status); exit(2); }
    char *p = cap.buf, *endp;
    int n = 0; long sum = 0;
    while (p && *p && n < 800) {
        long v = strtol(p, &endp, 10);
        if (endp == p) break;
        sum += v; n++; p = endp;
    }
    long nsum = 0;
    for (int i = 0; i < 160; i++) {
        int o5[5];
        native_wall5(i * 2 + 1, 320, 320, 64, 0, 0, -42, 0, o5);
        for (int j = 0; j < 5; j++) nsum += o5[j];
    }
    printf("prepared frame_values=%d checksum=%ld native_checksum=%ld checksums_equal=%d frame_ms=%.3f\n",
           n, sum, nsum, sum == nsum, (double)(e - a) / 1e6);
#ifdef S1_PROFILE
    /* one instrumented single-ray and full-frame replay, counts only */
    s1_prof_reset();
    r0_s1_outcome po; r0_s1_session_run_block(b, &po);
    long pops = 0, phost = 0;
    for (int i = 0; i < 8; i++) pops += s1_prof_op_count[i];
    for (int i = 0; i < 16; i++) phost += s1_prof_host_count[i];
    s1_prof_reset();
    r0_s1_outcome fo2; r0_s1_session_run_block(frame, &fo2);
    long fops = 0, fhost = 0;
    for (int i = 0; i < 8; i++) fops += s1_prof_op_count[i];
    for (int i = 0; i < 16; i++) fhost += s1_prof_host_count[i];
    printf("prepared profile single_ops=%ld single_host=%ld frame_ops=%ld frame_host=%ld\n",
           pops, phost, fops, fhost);
#endif
    free(cap.buf);
}

/* ---- Native arithmetic loop floors ------------------------------------ */

static void run_native_suite(int n) {
    volatile uintptr_t acc = 1;
    long t0 = now_ns();
    for (int i = 0; i < n; i++) acc += 1 + 2;
    long t1 = now_ns();
    printf("native add ns/op=%.3f checksum=%lu\n", (double)(t1 - t0) / n, (unsigned long)acc);
    t0 = now_ns();
    for (int i = 0; i < n; i++) acc += 1000 * 3;
    t1 = now_ns();
    printf("native mul ns/op=%.3f checksum=%lu\n", (double)(t1 - t0) / n, (unsigned long)acc);
    t0 = now_ns();
    for (int i = 0; i < n; i++) acc += 1000000 / 3;
    t1 = now_ns();
    printf("native div ns/op=%.3f checksum=%lu\n", (double)(t1 - t0) / n, (unsigned long)acc);
    t0 = now_ns();
    for (int i = 0; i < n; i++) acc += (acc > 0) ? 1 : 2;
    t1 = now_ns();
    printf("native branch ns/op=%.3f checksum=%lu\n", (double)(t1 - t0) / n, (unsigned long)acc);
    volatile uintptr_t mem = 0;
    t0 = now_ns();
    for (int i = 0; i < n; i++) { mem = mem + 1; acc += mem; }
    t1 = now_ns();
    printf("native memory ns/op=%.3f checksum=%lu\n", (double)(t1 - t0) / n, (unsigned long)acc);
    sink += acc + mem;
}

int main(int argc, char **argv) {
    int n = 200000, reps = 7;
    int do_s1 = 1, do_glon = 1, do_wall = 1, do_native = 1, do_prepared = 0;
    if (argc > 1 && strcmp(argv[1], "--s1") == 0) { do_glon = do_wall = do_native = 0; }
    if (argc > 1 && strcmp(argv[1], "--glon") == 0) { do_s1 = do_wall = do_native = 0; }
    if (argc > 1 && strcmp(argv[1], "--wall") == 0) { do_s1 = do_glon = do_native = 0; }
    if (argc > 1 && strcmp(argv[1], "--native") == 0) { do_s1 = do_glon = do_wall = 0; }
    if (argc > 1 && strcmp(argv[1], "--prepared") == 0) { do_s1 = do_glon = do_wall = do_native = 0; do_prepared = 1; }
    if (argc > 2) n = atoi(argv[2]);
    if (argc > 3) reps = atoi(argv[3]);
    if (do_s1) run_s1_suite(n, reps);
    if (do_native) run_native_suite(n);
    if (do_glon) run_glon_suite(reps);
    if (do_wall) run_wall_suite(reps);
    if (do_prepared) run_prepared_suite(reps);
    printf("bench sink=%lu\n", (unsigned long)sink);
    return 0;
}
