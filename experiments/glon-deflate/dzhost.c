/* experiments/glon-deflate/dzhost.c -- the C stand-in host for the Glon
 * DEFLATE decoder. It links the UNCHANGED runtime and GLON_LIVE dispatcher
 * (r0_s1_g1a_live.c: the layer behind glon-live.wasm's glon_event_bytes) and
 * plays the host: it reads a compressed file, passes its bytes to Glon in
 * chunks, asks Glon for decompressed bytes, and writes what Glon printed.
 *
 * The host does NO decompression. It moves bytes (feed), forwards requests
 * (read n) and reads Glon's framed output. All DEFLATE logic is inflate.glon.
 *
 *   dzhost [options] FILE
 *     -w W        window bytes kept by the decoder (default 768: zlib windowBits <= 10)
 *     -c LIST     compressed chunk sizes, comma separated, used in turn
 *                 (default 128): a feed is sent only when Glon answers N
 *     -r LIST     read sizes, comma separated, used in turn (default 100)
 *     -o OUT      write the decompressed bytes to OUT
 *     -p MODE     progress: 1 lines, 2 ANSI in-place (no data output)
 *     -u          progress without a known total (byte counters only)
 *     -E          eager: feed all input (and finish) before the first read
 *     -s          print a STATS line (timings, memory, stack) on stderr
 *     -L DIR      library directory (default: the source tree, see libs[])
 *
 * Exit: 0 the stream ended ('done'); 3 Glon reported an error (the id is
 * printed as "ERROR id" on stderr); 1 a machine/host failure; 2 usage.
 */
#include "r0_s1.h"
#include "r0_s1_g1a_live.h"
#include "m1_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

static char filebuf[1 << 17], wrapbuf[(1 << 17) + 16], outbuf[4096];

static double now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static int load_src(const char *src0) {
    const char *src = src0, *p = src0;
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    if (*p != '[') { snprintf(wrapbuf, sizeof wrapbuf, "[ %s ]", src0); src = wrapbuf; }
    int err = 0; cell b = r0_s1_parse(src, &err);
    if (err) return -2;
    r0_s1_run_persistent(b);
    return r0_s1_ran_cleanly() ? 0 : -3;
}
static int load_file(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) { fprintf(stderr, "dzhost: cannot open %s\n", path); return -1; }
    size_t n = fread(filebuf, 1, sizeof filebuf - 1, f); fclose(f); filebuf[n] = 0;
    char *r = filebuf, *w = filebuf;
    while (*r) { if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; } *w++ = *r++; }
    *w = 0;
    return load_src(filebuf);
}

/* ---- measurements (read from the runtime's existing counters) ---------- */
static long max_live = 0, base_live = -1, max_rs = 0, gc_seen = 0, last_free = 0, last_blocks = 0;

/* F1 diagnostic: inspect the non-moving heap at a host/machine failure. */
static void dump_heap_state(void) {
    long addr = GC_HEAP_BASE, hp = (long)M[REG_HP];
    long free_cells = 0, free_blocks = 0, largest = 0;
    fprintf(stderr, "HEAPDUMP base=%ld hp=%ld span=%ld\n",
            (long)GC_HEAP_BASE, hp, hp - (long)GC_HEAP_BASE);
    while (addr < hp) {
        long size = (long)M[addr];
        long flags = (long)M[addr + GC_HDR_FLAGS];
        if (size <= 0 || addr + size > hp) {
            fprintf(stderr, "HEAPDUMP corrupt header at %ld size=%ld flags=%ld\n", addr, size, flags);
            break;
        }
        long alloc = flags & GC_FLAG_ALLOC;
        long kind = (flags >> 2) & 7;
        if (!alloc) {
            free_cells += size; free_blocks++;
            if (size > largest) largest = size;
            fprintf(stderr, "HEAPDUMP free addr=%ld size=%ld\n", addr, size);
        } else if (kind == GC_KIND_STRING) {
            long len = (long)M[addr + GC_HDR_STRIDE];
            fprintf(stderr, "HEAPDUMP live addr=%ld size=%ld kind=%ld strlen=%ld waste=%ld\n",
                    addr, size, kind, len, size - GC_HDR_STRIDE - len);
        } else {
            fprintf(stderr, "HEAPDUMP live addr=%ld size=%ld kind=%ld\n", addr, size, kind);
        }
        addr += size;
    }
    fprintf(stderr, "HEAPDUMP free_cells=%ld free_blocks=%ld largest_free=%ld live_counter=%ld free_counter=%ld request_extent=%ld request_kind=%ld alloc_return=%ld\n",
            free_cells, free_blocks, largest,
            r0_s1_gc_live_cells(), r0_s1_gc_free_cells(),
            (long)M[GC_A1], (long)M[GC_A2], (long)M[(long)M[REG_RP]]);
}
static void sample(void) {
    long g = r0_s1_gc_count();
    if (g != gc_seen) {
        gc_seen = g;
        long l = r0_s1_gc_live_cells();
        if (l > max_live) max_live = l;
        last_free = r0_s1_gc_free_cells(); last_blocks = r0_s1_gc_free_blocks();
    }
    long rs = 24576 - (long)r0_s1_rp_min(); if (rs > max_rs) max_rs = rs;
}

/* ---- one dispatch: the reply is the one status byte ---------------------- */
static long n_disp = 0;
static int dispatch(const char *tok, const unsigned char *data, int n, double *acc) {
    int out_len = 0;
    double t0 = now();
    int rc = r0_s1_g1a_live_event_bytes(tok, data, (unsigned)n, outbuf, (int)sizeof outbuf, &out_len);
    if (acc) *acc += now() - t0;
    n_disp++;
    sample();
    if (rc != 0 || out_len < 1) return 0;
    return (unsigned char)outbuf[0];
}
static int dispatch_int(const char *tok, long v, double *acc) {
    char t[32]; snprintf(t, sizeof t, "%ld", v);
    return dispatch(tok, (const unsigned char *)t, (int)strlen(t), acc);
}

static int parse_list(const char *s, long *v, int cap) {
    int n = 0;
    while (*s && n < cap) { v[n++] = strtol(s, (char **)&s, 10); if (*s == ',') s++; }
    return n;
}

/* ---- Glon's stdout, captured in data mode -------------------------------- */
static FILE *cap_f; static long cap_off = 0;
static FILE *out_f;
static long out_bytes = 0;
static char err_id[64] = "";
static unsigned char *capbuf; static size_t capcap = 0;
/* parse the frames Glon printed since the last call */
static int drain(void) {
    fflush(stdout);
    struct stat sb; if (fstat(fileno(cap_f), &sb) != 0) return -1;
    long end = (long)sb.st_size;
    if (end <= cap_off) return 0;
    size_t n = (size_t)(end - cap_off);
    if (n + 1 > capcap) { capcap = n + 1; capbuf = realloc(capbuf, capcap); }
    if (pread(fileno(cap_f), capbuf, n, cap_off) != (ssize_t)n) return -1;
    capbuf[n] = 0;
    /* only whole frames are consumed; a partial one waits for the next drain */
    size_t i = 0;
    while (i < n) {
        if (capbuf[i] == 'D' && capbuf[i + 1] == ' ') {
            long len = strtol((char *)capbuf + i + 2, NULL, 10);
            unsigned char *nl = memchr(capbuf + i, '\n', n - i);
            if (!nl) return -1;
            size_t s = (size_t)(nl - capbuf) + 1;
            if (s + (size_t)len + 1 > n) break;
            if (capbuf[s + (size_t)len] != '\n') return -1;
            if (out_f) fwrite(capbuf + s, 1, (size_t)len, out_f);
            out_bytes += len;
            i = s + (size_t)len + 1;
        } else if (capbuf[i] == 'E' && capbuf[i + 1] == ' ') {
            unsigned char *nl = memchr(capbuf + i, '\n', n - i);
            size_t e = nl ? (size_t)(nl - capbuf) : n;
            size_t k = e - i - 2; if (k > sizeof err_id - 1) k = sizeof err_id - 1;
            memcpy(err_id, capbuf + i + 2, k); err_id[k] = 0;
            i = e + 1;
        } else {                      /* anything else is unexpected output */
            unsigned char *nl = memchr(capbuf + i, '\n', n - i);
            fprintf(stderr, "dzhost: unexpected output: %.*s\n", (int)((nl ? nl : capbuf + n) - (capbuf + i)), capbuf + i);
            i = nl ? (size_t)(nl - capbuf) + 1 : n;
        }
    }
    cap_off += (long)i;
    return 0;
}

int main(int argc, char **argv) {
    long win = 768, chunks[256] = { 128 }, reads[256] = { 100 };
    int nchunks = 1, nreads = 1, progress = 0, unknown = 0, eager = 0, stats = 0;
    const char *outpath = NULL, *libdir = NULL;
    int a = 1;
    for (; a < argc && argv[a][0] == '-'; a++) {
        const char *o = argv[a];
        if (!strcmp(o, "-w") && a + 1 < argc) win = atol(argv[++a]);
        else if (!strcmp(o, "-c") && a + 1 < argc) nchunks = parse_list(argv[++a], chunks, 256);
        else if (!strcmp(o, "-r") && a + 1 < argc) nreads = parse_list(argv[++a], reads, 256);
        else if (!strcmp(o, "-o") && a + 1 < argc) outpath = argv[++a];
        else if (!strcmp(o, "-p") && a + 1 < argc) progress = atoi(argv[++a]);
        else if (!strcmp(o, "-L") && a + 1 < argc) libdir = argv[++a];
        else if (!strcmp(o, "-u")) unknown = 1;
        else if (!strcmp(o, "-E")) eager = 1;
        else if (!strcmp(o, "-s")) stats = 1;
        else { fprintf(stderr, "dzhost: unknown option %s\n", o); return 2; }
    }
    if (a != argc - 1 || nchunks < 1 || nreads < 1) {
        fprintf(stderr, "usage: dzhost [-w W] [-c LIST] [-r LIST] [-o OUT] [-p 1|2] [-u] [-E] [-s] FILE\n");
        return 2;
    }
    for (int i = 0; i < nchunks; i++) if (chunks[i] < 1 || chunks[i] > 12000) { fprintf(stderr, "dzhost: chunk sizes are 1..12000\n"); return 2; }
    for (int i = 0; i < nreads; i++) if (reads[i] < 1) { fprintf(stderr, "dzhost: read sizes must be >= 1\n"); return 2; }

    FILE *f = fopen(argv[a], "rb");
    if (!f) { fprintf(stderr, "dzhost: cannot open %s\n", argv[a]); return 2; }
    fseek(f, 0, SEEK_END); long insz = ftell(f); rewind(f);
    unsigned char *in = malloc((size_t)insz + 1);
    if (insz && fread(in, 1, (size_t)insz, f) != (size_t)insz) { fprintf(stderr, "dzhost: read error\n"); return 2; }
    fclose(f);

    /* ---- the Glon session: libraries, then the app --------------------- */
    double t_init0 = now();
    cell me = r0_s1_init(); M[M1_MAIN_ENTRY_CELL] = me; M[M1_CURSOR] = 0; M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++) M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
    static const char *libs[] = { "jupyter/prelude.glon", "demo/shop/strings.glon", "demo/shop/strings-ext.glon",
                                  "experiments/glon-deflate/inflate.glon", "experiments/glon-deflate/progress.glon",
                                  "experiments/glon-deflate/app.glon", NULL };
    char root[4096];
    if (libdir) snprintf(root, sizeof root, "%s", libdir);
    else {   /* the repository root, two levels above this executable's source dir */
        ssize_t k = readlink("/proc/self/exe", root, sizeof root - 1); if (k <= 0) return 2; root[k] = 0;
        for (int up = 0; up < 4; up++) { char *s = strrchr(root, '/'); if (s) *s = 0; }  /* build/dzhost -> repo */
    }
    cell lhp_libs0 = M[GC_LOADER_HP];
    for (int i = 0; libs[i]; i++) {
        char path[8192]; snprintf(path, sizeof path, "%s/%s", root, libs[i]);
        if (load_file(path) != 0) { fprintf(stderr, "dzhost: library did not load: %s\n", path); return 1; }
    }
    cell used, capb; r0_s1_global_usage(&used, &capb);
    long lib_loader = (long)(M[GC_LOADER_HP] - lhp_libs0);
    double t_libs = now() - t_init0;
    r0_s1_g1a_live_set_host_call(NULL, NULL);

    if (!progress) {   /* Glon prints its frames to stdout: capture them */
        fflush(stdout);
        cap_f = tmpfile(); dup2(fileno(cap_f), 1);
        if (outpath) { out_f = fopen(outpath, "wb"); if (!out_f) { fprintf(stderr, "dzhost: cannot write %s\n", outpath); return 2; } }
    }
    cell lhp0 = M[GC_LOADER_HP];
    double t_open = 0, t_read = 0, t_feed = 0, t_first = -1;
    double t_run0 = now();
    dispatch_int("show", progress, &t_open);
    dispatch_int("total", unknown ? 0 : insz, &t_open);
    int st = dispatch_int("open", win, &t_open);
    long fed = 0, nfeeds = 0, nreads_done = 0; int ci = 0, ri = 0, finished = 0, rc = 1;
    if (eager) {
        while (fed < insz) {
            long k = chunks[ci++ % nchunks]; if (k > insz - fed) k = insz - fed;
            st = dispatch("feed", in + fed, (int)k, &t_feed); fed += k; nfeeds++;
        }
        st = dispatch("finish", NULL, 0, &t_feed); finished = 1;
    }
    for (;;) {
        if (st == 'R') {
            long n = reads[ri++ % nreads];
            st = dispatch_int("read", n, &t_read); nreads_done++;
            if (!progress && drain() != 0) { fprintf(stderr, "dzhost: bad frame\n"); st = 0; }
            if (t_first < 0 && (progress || out_bytes > 0)) t_first = now() - t_run0;
        } else if (st == 'N') {
            if (fed < insz) {
                long k = chunks[ci++ % nchunks]; if (k > insz - fed) k = insz - fed;
                st = dispatch("feed", in + fed, (int)k, &t_feed); fed += k; nfeeds++;
            } else if (!finished) { st = dispatch("finish", NULL, 0, &t_feed); finished = 1; }
            else { fprintf(stderr, "dzhost: need-input after finish\n"); rc = 1; break; }
            if (st == 'N' || st == 'R') st = 'R';   /* after a feed, ask again */
        } else if (st == 'D') { rc = 0; break; }
        else if (st == 'E') {
            if (!progress) drain();
            fprintf(stderr, "ERROR %s\n", err_id[0] ? err_id : "?");
            rc = 3; break;
        } else {
            fprintf(stderr, "dzhost: dispatch failed (machine halt %d; last collection: %ld live, %ld free in %ld blocks)\n",
                    r0_s1_halt_reason(), r0_s1_gc_live_cells(), last_free, last_blocks);
            if (getenv("GLON_F1_DIAG")) dump_heap_state();
            rc = 1; break;
        }
    }
    double t_run = now() - t_run0;
    if (!progress) { drain(); fflush(stdout); }
    if (out_f) fclose(out_f);
    if (stats) {
        cell lhp1 = M[GC_LOADER_HP];
        fprintf(stderr, "STATS in=%ld fed=%ld out=%ld window=%ld feeds=%ld reads=%ld dispatches=%ld "
                "libs_ms=%.1f open_ms=%.2f first_out_ms=%.1f read_ms=%.1f feed_ms=%.1f run_ms=%.1f "
                "us_per_read=%.1f out_kb_s=%.2f in_kb_s=%.2f gcs=%ld max_live=%ld heap=%ld max_rs=%ld "
                "loader_delta=%ld lib_loader=%ld bindings=%ld code_end=%ld\n",
                insz, fed, out_bytes, win, nfeeds, nreads_done, n_disp,
                t_libs * 1e3, t_open * 1e3, t_first * 1e3, t_read * 1e3, t_feed * 1e3, t_run * 1e3,
                nreads_done ? t_read * 1e6 / (double)nreads_done : 0.0,
                t_run > 0 ? (double)out_bytes / 1024.0 / t_run : 0.0,
                t_run > 0 ? (double)fed / 1024.0 / t_run : 0.0,
                gc_seen, max_live, (long)(GC_HEAP_LIMIT - GC_HEAP_BASE), max_rs,
                (long)(lhp1 - lhp0), lib_loader, (long)used, (long)asm_here());
    }
    (void)base_live;
    return rc;
}
