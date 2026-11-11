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
#include "s1_prof.h"
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

/* Profiling counters are reset by every r0_s1 run/dispatch.  Accumulate their
 * per-run deltas so one STATS line covers the whole session. */
static long pf_allocs_total, pf_cells_total, pf_ctx_total, pf_raw_total;
static r0_s1_pf_stats pf_prev;
static int pf_seen;
static void pf_step(void) {
    r0_s1_pf_stats now; r0_s1_pf_read(&now);
    long da, dc, dx, dr;
    if (!pf_seen || now.allocs < pf_prev.allocs) da = now.allocs; else da = now.allocs - pf_prev.allocs;
    if (!pf_seen || now.alloc_cells < pf_prev.alloc_cells) dc = now.alloc_cells; else dc = now.alloc_cells - pf_prev.alloc_cells;
    if (!pf_seen || now.alloc_ctx < pf_prev.alloc_ctx) dx = now.alloc_ctx; else dx = now.alloc_ctx - pf_prev.alloc_ctx;
    if (!pf_seen || now.raw < pf_prev.raw) dr = now.raw; else dr = now.raw - pf_prev.raw;
    pf_allocs_total += da; pf_cells_total += dc; pf_ctx_total += dx; pf_raw_total += dr;
    pf_prev = now; pf_seen = 1;
}

/* B10: all PF_* runtime counters, accumulated across dispatches. */
static r0_s1_pf_stats b10_pf_sum, b10_pf_prev;
static int b10_pf_seen;
#define B10_DELTA(field) do { \
    long v = now.field; \
    if (!b10_pf_seen || v < b10_pf_prev.field) b10_pf_sum.field += v; \
    else b10_pf_sum.field += v - b10_pf_prev.field; \
} while (0)
static void b10_pf_step(void) {
    r0_s1_pf_stats now; r0_s1_pf_read(&now);
    B10_DELTA(closure); B10_DELTA(subexpr); B10_DELTA(blkeval);
    B10_DELTA(lookup); B10_DELTA(lk_slots); B10_DELTA(lk_parent);
    B10_DELTA(native); B10_DELTA(nat_le); B10_DELTA(nat_sub); B10_DELTA(nat_add);
    B10_DELTA(nat_either); B10_DELTA(nat_other);
    B10_DELTA(allocs); B10_DELTA(alloc_cells); B10_DELTA(alloc_ctx); B10_DELTA(alloc_frame);
    B10_DELTA(raw); B10_DELTA(promote); B10_DELTA(lex_direct);
    B10_DELTA(lex_local); B10_DELTA(lex_parent);
    B10_DELTA(hash_probes); B10_DELTA(hash_hits); B10_DELTA(hash_misses);
    B10_DELTA(hash_collisions); B10_DELTA(hash_fallback); B10_DELTA(hash_fallback_slots);
    if (!b10_pf_seen || now.max_depth > b10_pf_sum.max_depth) b10_pf_sum.max_depth = now.max_depth;
    b10_pf_prev = now; b10_pf_seen = 1;
}
static void b10_pf_reset(void) {
    memset(&b10_pf_sum, 0, sizeof b10_pf_sum);
    r0_s1_pf_read(&b10_pf_prev); b10_pf_seen = 1;
}

/* F1 diagnostic: inspect the non-moving heap at a host/machine failure. */
static void dump_one_string(const char *name, long tagged) {
    long payload = tagged - T_STRING;
    if (payload < GC_HEAP_BASE || payload + 16 >= GC_HEAP_LIMIT) {
        fprintf(stderr, "HEAPDUMP %s=invalid tagged=%ld\n", name, tagged);
        return;
    }
    long len = (long)M[payload];
    if (len < 0 || payload + 1 + len >= GC_HEAP_LIMIT) {
        fprintf(stderr, "HEAPDUMP %s=invalidpayload tagged=%ld len=%ld\n", name, tagged, len);
        return;
    }
    fprintf(stderr, "HEAPDUMP %s tagged=%ld payload=%ld len=%ld bytes=", name, tagged, payload, len);
    long shown = len < 24 ? len : 24;
    for (long i = 0; i < shown; i++) fprintf(stderr, "%02lx", (long)M[payload + 1 + i] & 255);
    if (len > shown) fprintf(stderr, "..");
    fprintf(stderr, " tail=");
    long from = len > 8 ? len - 8 : 0;
    for (long i = from; i < len; i++) fprintf(stderr, "%02lx", (long)M[payload + 1 + i] & 255);
    fprintf(stderr, "\n");
}

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
    fprintf(stderr, "HEAPDUMP free_cells=%ld free_blocks=%ld largest_free=%ld live_counter=%ld free_counter=%ld request_extent=%ld request_kind=%ld alloc_return=%ld scratchA=%ld scratchB=%ld scratchC=%ld scratchF=%ld\n",
            free_cells, free_blocks, largest,
            r0_s1_gc_live_cells(), r0_s1_gc_free_cells(),
            (long)M[GC_A1], (long)M[GC_A2], (long)M[(long)M[REG_RP]],
            (long)M[RV_SCRATCH_A], (long)M[RV_SCRATCH_B], (long)M[RV_SCRATCH_C], (long)M[RV_SCRATCH_F]);
    dump_one_string("scratchA_string", (long)M[RV_SCRATCH_A]);
    dump_one_string("scratchB_string", (long)M[RV_SCRATCH_B]);
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
    pf_step();
    b10_pf_step();
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
                                  "experiments/glon-deflate/f2buf.glon",
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
        pf_step();
    }
    cell used, capb; r0_s1_global_usage(&used, &capb);
    long allocs_lib = pf_allocs_total, cells_lib = pf_cells_total;
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
    s1_prof_reset();
    b10_pf_reset();
    long allocs_open = pf_allocs_total, cells_open = pf_cells_total;
    long ctx_open = pf_ctx_total, raw_open = pf_raw_total;
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
        pf_step();
        long b9_ops = 0, b9_hosts = 0;
        for (int oi = 0; oi < 8; oi++) b9_ops += s1_prof_op_count[oi];
        for (int hi = 0; hi < 16; hi++) b9_hosts += s1_prof_host_count[hi];
        fprintf(stderr, "B9_PROFILE ops=%ld hostcalls=%ld cycles=%llu\n",
                b9_ops, b9_hosts, (unsigned long long)s1_prof_run_cycles);
        fprintf(stderr, "B10_OPS lit=%ld dup=%ld drop=%ld fetch=%ld store=%ld zbranch=%ld host=%ld halt=%ld\n",
                s1_prof_op_count[OP_LIT], s1_prof_op_count[OP_DUP], s1_prof_op_count[OP_DROP],
                s1_prof_op_count[OP_FETCH], s1_prof_op_count[OP_STORE],
                s1_prof_op_count[OP_ZBRANCH], s1_prof_op_count[OP_HOST], s1_prof_op_count[OP_HALT]);
        fprintf(stderr, "B10_HOST add=%ld sub=%ld mul=%ld div=%ld mod=%ld neg=%ld eq=%ld ne=%ld lt=%ld gt=%ld le=%ld ge=%ld alloc=%ld putchar=%ld print=%ld dump=%ld\n",
                s1_prof_host_count[HOST_ADD], s1_prof_host_count[HOST_SUB], s1_prof_host_count[HOST_MUL],
                s1_prof_host_count[HOST_DIV], s1_prof_host_count[HOST_MOD], s1_prof_host_count[HOST_NEG],
                s1_prof_host_count[HOST_EQ], s1_prof_host_count[HOST_NE], s1_prof_host_count[HOST_LT],
                s1_prof_host_count[HOST_GT], s1_prof_host_count[HOST_LE], s1_prof_host_count[HOST_GE],
                s1_prof_host_count[HOST_ALLOC], s1_prof_host_count[HOST_PUTCHAR],
                s1_prof_host_count[HOST_PRINT], s1_prof_host_count[HOST_DUMP]);
        fprintf(stderr, "B10_PROFILE closure=%ld subexpr=%ld blkeval=%ld lookup=%ld lk_slots=%ld lk_parent=%ld native=%ld nat_le=%ld nat_sub=%ld nat_add=%ld nat_either=%ld nat_other=%ld allocs=%ld alloc_cells=%ld alloc_ctx=%ld alloc_frame=%ld raw=%ld max_depth=%ld promote=%ld lex_direct=%ld lex_local=%ld lex_parent=%ld hash_probes=%ld hash_hits=%ld hash_misses=%ld hash_collisions=%ld hash_fallback=%ld hash_fallback_slots=%ld\n",
                b10_pf_sum.closure, b10_pf_sum.subexpr, b10_pf_sum.blkeval,
                b10_pf_sum.lookup, b10_pf_sum.lk_slots, b10_pf_sum.lk_parent,
                b10_pf_sum.native, b10_pf_sum.nat_le, b10_pf_sum.nat_sub,
                b10_pf_sum.nat_add, b10_pf_sum.nat_either, b10_pf_sum.nat_other,
                b10_pf_sum.allocs, b10_pf_sum.alloc_cells, b10_pf_sum.alloc_ctx,
                b10_pf_sum.alloc_frame, b10_pf_sum.raw, b10_pf_sum.max_depth,
                b10_pf_sum.promote, b10_pf_sum.lex_direct, b10_pf_sum.lex_local,
                b10_pf_sum.lex_parent, b10_pf_sum.hash_probes, b10_pf_sum.hash_hits,
                b10_pf_sum.hash_misses, b10_pf_sum.hash_collisions,
                b10_pf_sum.hash_fallback, b10_pf_sum.hash_fallback_slots);
        fprintf(stderr, "STATS in=%ld fed=%ld out=%ld window=%ld feeds=%ld reads=%ld dispatches=%ld "
                "libs_ms=%.1f open_ms=%.2f first_out_ms=%.1f read_ms=%.1f feed_ms=%.1f run_ms=%.1f "
                "us_per_read=%.1f out_kb_s=%.2f in_kb_s=%.2f gcs=%ld max_live=%ld heap=%ld max_rs=%ld "
                "loader_delta=%ld lib_loader=%ld bindings=%ld code_end=%ld "
                "allocs_total=%ld allocs_lib=%ld allocs_open=%ld allocs_decode=%ld "
                "alloc_cells_total=%ld alloc_cells_lib=%ld alloc_cells_open=%ld "
                "alloc_cells_decode=%ld allocs_ctx_decode=%ld allocs_raw_decode=%ld "
                "live_objs=%ld reclaimed=%ld\n",
                insz, fed, out_bytes, win, nfeeds, nreads_done, n_disp,
                t_libs * 1e3, t_open * 1e3, t_first * 1e3, t_read * 1e3, t_feed * 1e3, t_run * 1e3,
                nreads_done ? t_read * 1e6 / (double)nreads_done : 0.0,
                t_run > 0 ? (double)out_bytes / 1024.0 / t_run : 0.0,
                t_run > 0 ? (double)fed / 1024.0 / t_run : 0.0,
                gc_seen, max_live, (long)(GC_HEAP_LIMIT - GC_HEAP_BASE), max_rs,
                (long)(lhp1 - lhp0), lib_loader, (long)used, (long)asm_here(),
                pf_allocs_total, allocs_lib, allocs_open, pf_allocs_total - allocs_open,
                pf_cells_total, cells_lib, cells_open, pf_cells_total - cells_open,
                pf_ctx_total - ctx_open, pf_raw_total - raw_open,
                r0_s1_gc_live_objs(), r0_s1_gc_reclaimed());
    }
    (void)base_live;
    return rc;
}
