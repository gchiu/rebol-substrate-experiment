/* glon.c - the native Glon command-line runner.
 *
 * A thin host around the existing persistent-session facade (r0_s1_session_run
 * / r0_s1_session_run_block): it initialises ONE runtime, loads the shipped
 * core libraries, then runs each FILE in that same session so definitions
 * persist across files. It adds no language semantics and no parser/evaluator
 * code; every outcome is classified by the runtime's own structured state.
 *
 * Usage:
 *   glon FILE...
 *
 * A FILE is ordinary Glon source (a top-level sequence, optionally wrapped in
 * one outer [ ... ]; `;;` line comments are allowed), exactly the source form
 * the native session host and the WASM glon_load accept.
 *
 * The core libraries are located beside the executable in glon-lib/:
 *   glon-lib/prelude.glon   the smallest ordinary core vocabulary
 *   glon-lib/strings.glon   immutable STRING! operations (s/+ s/= s/length s/print)
 * They are ordinary Glon source, parsed and run by the existing loader; they
 * are not compiled into the binary.
 *
 * Exit status: 0 all files clean; 1 a Glon parse/SIN!/resource/machine error;
 * 2 usage, IO or library failure.
 */

#define _GNU_SOURCE
#include "r0_s1.h"
#include "m1_layout.h"
#ifdef GLON_MODULE_FILE
#include "glon_io_file.h"
#include "glon_io_file_cli.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_SOURCE (1u << 20)

/* ---- source files -------------------------------------------------------- */

static char *read_source(const char *path, char *err, size_t errcap) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errcap, "cannot open"); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f); snprintf(err, errcap, "cannot seek"); return NULL;
    }
    long n = ftell(f);
    if (n < 0) { fclose(f); snprintf(err, errcap, "cannot measure"); return NULL; }
    if ((unsigned long)n > MAX_SOURCE) {
        fclose(f);
        snprintf(err, errcap, "source is larger than %u bytes", (unsigned)MAX_SOURCE);
        return NULL;
    }
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); snprintf(err, errcap, "out of memory"); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(buf); snprintf(err, errcap, "cannot read"); return NULL; }
    buf[got] = 0;
    return buf;
}

/* strip `;;` line comments, exactly as the native host and WASM loader do */
static void strip_comments(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
}

/* ---- library discovery --------------------------------------------------- */

static char exe_dir[2048];

static int find_exe_dir(void) {
    ssize_t n = readlink("/proc/self/exe", exe_dir, sizeof exe_dir - 1);
    if (n <= 0) return -1;
    exe_dir[n] = 0;
    char *slash = strrchr(exe_dir, '/');
    if (slash) *slash = 0; else strcpy(exe_dir, ".");
    return 0;
}

static int load_library(const char *path) {
    char err[512];
    char *src = read_source(path, err, sizeof err);
    if (!src) { fprintf(stderr, "glon: %s: %s\n", path, err); return -1; }

    strip_comments(src);
    int perr = 0;
    cell prog = r0_s1_parse(src, &perr);
    free(src);

    r0_s1_outcome oc;
    if (perr) r0_s1_session_parse_error(&oc);
    else r0_s1_session_run_block(prog, &oc);
    if (oc.status != R0S1_OUT_OK) {
        fprintf(stderr, "glon: %s: library did not load\n", path);
        return -1;
    }
    return 0;
}

static int load_default_libraries(void) {
    if (find_exe_dir() != 0) {
        fprintf(stderr, "glon: cannot locate the executable directory\n");
        return -1;
    }
    static const char *names[] = { "prelude.glon", "strings.glon" };
    char path[4096];
    for (unsigned k = 0; k < sizeof names / sizeof names[0]; k++) {
        snprintf(path, sizeof path, "%s/glon-lib/%s", exe_dir, names[k]);
        if (load_library(path) != 0) return -1;
    }
#ifdef GLON_MODULE_FILE
    /* strings-ext is needed by file.glon's whole-file write loop.  Both are
     * ordinary Glon libraries; the minimal binary never loads them. */
    static const char *module_names[] = { "strings-ext.glon", "file.glon", "file-stream.glon" };
    for (unsigned k = 0; k < sizeof module_names / sizeof module_names[0]; k++) {
        snprintf(path, sizeof path, "%s/glon-lib/%s", exe_dir, module_names[k]);
        if (load_library(path) != 0) return -1;
    }
#endif
    return 0;
}

/* ---- outcome reporting --------------------------------------------------- */

static void report_outcome(const r0_s1_outcome *out, const char *path) {
    switch (out->status) {
    case R0S1_OUT_OK:
        if (out->count <= 0) return;
        if (out->count == 1 && r0_s1_result(0, 1) == R0_NONE) return;
        {
            char buf[4096];
            for (int i = 0; i < out->count; i++) {
                if (i) putchar(' ');
                r0_s1_mold(r0_s1_result(i, out->count), buf, (int)sizeof buf);
                fputs(buf, stdout);
            }
            putchar('\n');
        }
        return;
    case R0S1_OUT_PARSE_ERROR:
        fprintf(stderr, "glon: %s: ** parse error", path);
        if (out->detail != R0S1_DETAIL_NONE)
            fprintf(stderr, ": %s", r0_s1_outcome_detail_name(out->detail));
        fputc('\n', stderr);
        return;
    case R0S1_OUT_RESOURCE_ERROR:
        fprintf(stderr, "glon: %s: ** resource error: %s\n",
                path, r0_s1_outcome_detail_name(out->detail));
        return;
    case R0S1_OUT_UNCAUGHT_SIN: {
        char t[1024], i[1024], a[1024];
        r0_s1_mold(out->sin_type, t, sizeof t);
        r0_s1_mold(out->sin_id, i, sizeof i);
        r0_s1_mold(out->sin_arg, a, sizeof a);
        fprintf(stderr, "glon: %s: ** uncaught #[SIN! %s %s %s]\n", path, t, i, a);
        return;
    }
    case R0S1_OUT_HALT:
        if (out->detail == R0S1_DETAIL_CONTEXT_FULL)
            fprintf(stderr, "glon: %s: ** halted: context full (no SIN!: a machine-level fail-stop)\n", path);
        else
            fprintf(stderr, "glon: %s: ** halted (no SIN!: a machine-level fail-stop)\n", path);
        return;
    default:
        fprintf(stderr, "glon: %s: ** unknown session outcome\n", path);
        return;
    }
}

#ifdef GLON_MODULE_FILE
/* ---- optional native file module driver --------------------------------- */

#define IO_REPLY_SRC_CAP 32768

static int parse_uint_opt(const char *s, unsigned *out) {
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (!s[0] || !end || *end || v > 0x7ffffffful) return -1;
    *out = (unsigned)v;
    return 0;
}

static void io_usage(void) {
    fprintf(stderr,
        "usage: glon [--io-root DIR] [--allow-file-read] [--allow-file-write]\n"
        "            [--io-max-handles N] [--io-max-file BYTES]\n"
        "            [--io-delay-ms N] [--io-short N] [--io-progress FILE]\n"
        "            [--io-report] [--gc-report] [--io-cancel-before-reply] FILE...\n");
}

static int parse_io_options(int argc, char **argv, int *first_file,
                            glon_io_policy *policy, const char **progress,
                            int *report, int *gc_report, int *cancel_before_reply) {
    int i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1] == '-') {
        const char *a = argv[i];
        if (!strcmp(a, "--")) { i++; break; }
        else if (!strcmp(a, "--io-root")) {
            if (++i >= argc) return -1;
            policy->root = argv[i];
        } else if (!strcmp(a, "--allow-file-read")) {
            policy->allow_read = 1;
        } else if (!strcmp(a, "--allow-file-write")) {
            policy->allow_write = 1;
        } else if (!strcmp(a, "--io-max-handles")) {
            unsigned v; if (++i >= argc || parse_uint_opt(argv[i], &v)) return -1;
            policy->max_handles = v;
        } else if (!strcmp(a, "--io-max-file")) {
            unsigned v; if (++i >= argc || parse_uint_opt(argv[i], &v) || v == 0) return -1;
            policy->max_file = v;
        } else if (!strcmp(a, "--io-delay-ms")) {
            unsigned v; if (++i >= argc || parse_uint_opt(argv[i], &v)) return -1;
            policy->delay_ms = v;
        } else if (!strcmp(a, "--io-short")) {
            unsigned v; if (++i >= argc || parse_uint_opt(argv[i], &v) || v == 0) return -1;
            policy->short_io = v;
        } else if (!strcmp(a, "--io-progress")) {
            if (++i >= argc) return -1;
            *progress = argv[i];
        } else if (!strcmp(a, "--io-report")) {
            *report = 1;
        } else if (!strcmp(a, "--gc-report")) {
            *gc_report = 1;
        } else if (!strcmp(a, "--io-cancel-before-reply")) {
            *cancel_before_reply = 1;
        } else {
            return -1;
        }
        i++;
    }
    *first_file = i;
    return 0;
}

static int run_expression(const char *src, r0_s1_outcome *oc) {
    /* Reply/take sources are transient host-built expressions.  Reclaim their
     * loader blocks after the turn so sustained streaming does not exhaust the
     * loader heap.  The generated source only calls existing library functions
     * and builds managed strings; it does not define persistent closures. */
    cell save_lhp = M[GC_LOADER_HP];
    int perr = 0;
    cell prog = r0_s1_parse(src, &perr);
    if (perr) {
        r0_s1_session_parse_error(oc);
        M[GC_LOADER_HP] = save_lhp;
        return -1;
    }
    int r = r0_s1_session_run_block(prog, oc);
    M[GC_LOADER_HP] = save_lhp;
    return r;
}

static int take_request(uint8_t *buf, size_t cap, size_t *len, int *pending,
                        char *err, size_t errcap) {
    static const char expr[] = "[ io/take-request ]";
    r0_s1_outcome oc;
    (void)run_expression(expr, &oc);
    if (oc.status != R0S1_OUT_OK) {
        snprintf(err, errcap, "io/take-request failed (status %d)", oc.status);
        return -1;
    }
    if (oc.count <= 0 ||
        (oc.count == 1 && r0_s1_result(0, oc.count) == R0_NONE)) {
        *pending = 0;
        return 0;
    }
    if (oc.count != 1 || r0_tag(r0_s1_result(0, oc.count)) != T_BLOCK) {
        snprintf(err, errcap, "io/take-request did not return a BLOCK or NONE");
        return -1;
    }

    cell p = r0_untag(r0_s1_result(0, oc.count));
    if (M[p] != 3) {
        snprintf(err, errcap, "io/take-request block shape");
        return -1;
    }
    cell idv = M[p + BLK_DATA];
    cell opv = M[p + BLK_DATA + 1];
    cell fields = M[p + BLK_DATA + 2];
    if (r0_tag(idv) != T_INT || r0_tag(opv) != T_INT ||
        r0_tag(fields) != T_BLOCK) {
        snprintf(err, errcap, "io/take-request field types");
        return -1;
    }
    uint32_t id = (uint32_t)int_val(idv);
    unsigned op = (unsigned)int_val(opv);
    if (id == 0 || op < GLON_IO_OP_OPEN || op > GLON_IO_OP_CLOSE) {
        snprintf(err, errcap, "io/take-request id/op");
        return -1;
    }
    cell fp = r0_untag(fields);
    cell nf = M[fp];
    if (nf > GLON_IO_MAX_FIELDS) {
        snprintf(err, errcap, "io/take-request too many fields");
        return -1;
    }
    size_t off = 0;
    if (cap < 7) { snprintf(err, errcap, "request buffer too small"); return -1; }
    buf[off++] = GLON_IO_REQUEST_MAGIC;
    buf[off++] = (uint8_t)op;
    glon_io_put_u32(buf + off, id); off += 4;
    buf[off++] = (uint8_t)nf;
    for (cell i = 0; i < nf; i++) {
        cell sv = M[fp + BLK_DATA + i];
        if (r0_tag(sv) != T_STRING) {
            snprintf(err, errcap, "io/take-request field is not a STRING!");
            return -1;
        }
        cell sp = r0_untag(sv);
        cell sl = M[sp];
        if (sl < 0 || (size_t)sl > GLON_IO_MAX_FRAME ||
            off + 4 + (size_t)sl > cap) {
            snprintf(err, errcap, "io/take-request frame overflows");
            return -1;
        }
        glon_io_put_u32(buf + off, (uint32_t)sl); off += 4;
        for (cell j = 0; j < sl; j++) buf[off++] = (uint8_t)M[sp + 1 + j];
    }
    *len = off;
    *pending = 1;
    return 0;
}

static int reply_field(const uint8_t *frame, size_t len, unsigned index,
                       const uint8_t **bytes, uint32_t *flen) {
    if (len < 7 || frame[0] != GLON_IO_REPLY_MAGIC) return -1;
    unsigned count = frame[6];
    if (count > 2 || index >= count) return -1;
    size_t off = 7;
    for (unsigned i = 0; i < count; i++) {
        if (off + 4 > len) return -1;
        uint32_t n = glon_io_get_u32(frame + off);
        off += 4;
        if (off + n > len) return -1;
        if (i == index) { *bytes = frame + off; *flen = n; return 0; }
        off += n;
    }
    return -1;
}

static int run_io_reply(const uint8_t *frame, size_t len, uint32_t expected_id,
                        uint8_t op, char *err, size_t errcap) {
    if (len < 7 || frame[0] != GLON_IO_REPLY_MAGIC) {
        snprintf(err, errcap, "bad reply magic/length");
        return -1;
    }
    uint32_t id = glon_io_get_u32(frame + 1);
    unsigned status = frame[5];
    if (status > GLON_IO_BUSY) {
        snprintf(err, errcap, "unknown reply status %u", status);
        return -1;
    }
    if (id != expected_id) {
        snprintf(err, errcap, "reply id %u does not match request %u", id, expected_id);
        return -1;
    }

    char src[IO_REPLY_SRC_CAP];
    int n = snprintf(src, sizeof src, "[ io/reply %u '%s ",
                     id, glon_io_status_word(status));
    if (n < 0 || (size_t)n >= sizeof src) return -1;

    if (status == GLON_IO_OK && op == GLON_IO_OP_OPEN) {
        const uint8_t *a, *b; uint32_t al, bl;
        if (reply_field(frame, len, 0, &a, &al) || reply_field(frame, len, 1, &b, &bl) ||
            al != 4 || bl != 4) {
            snprintf(err, errcap, "malformed open reply");
            return -1;
        }
        n += snprintf(src + n, sizeof src - (size_t)n, "%u %u ]",
                      glon_io_get_u32(a), glon_io_get_u32(b));
    } else if (status == GLON_IO_OK && op == GLON_IO_OP_READ) {
        const uint8_t *a; uint32_t al;
        if (reply_field(frame, len, 0, &a, &al) || al > GLON_IO_MAX_CHUNK) {
            snprintf(err, errcap, "malformed/oversized read reply");
            return -1;
        }
        n += snprintf(src + n, sizeof src - (size_t)n, "mk-string [");
        for (uint32_t i = 0; i < al && (size_t)n + 5 < sizeof src; i++)
            n += snprintf(src + n, sizeof src - (size_t)n, " %u", a[i]);
        n += snprintf(src + n, sizeof src - (size_t)n, " ] 0 ]");
    } else if (status == GLON_IO_OK && op == GLON_IO_OP_WRITE) {
        const uint8_t *a; uint32_t al;
        if (reply_field(frame, len, 0, &a, &al) || al != 4) {
            snprintf(err, errcap, "malformed write reply");
            return -1;
        }
        n += snprintf(src + n, sizeof src - (size_t)n, "%u 0 ]",
                      glon_io_get_u32(a));
    } else {
        n += snprintf(src + n, sizeof src - (size_t)n, "0 0 ]");
    }
    if (n < 0 || (size_t)n >= sizeof src) {
        snprintf(err, errcap, "reply source too large");
        return -1;
    }

    r0_s1_outcome oc;
    (void)run_expression(src, &oc);
    if (oc.status != R0S1_OUT_OK) {
        report_outcome(&oc, "io/reply");
        snprintf(err, errcap, "io/reply callback failed (status %d)", oc.status);
        return -1;
    }
    return 0;
}

static int run_source(const char *path) {
    char err[512];
    char *src = read_source(path, err, sizeof err);
    if (!src) { fprintf(stderr, "glon: %s: %s\n", path, err); return -1; }
    strip_comments(src);
    int perr = 0;
    cell prog = r0_s1_parse(src, &perr);
    free(src);
    r0_s1_outcome oc;
    if (perr) r0_s1_session_parse_error(&oc);
    else r0_s1_session_run_block(prog, &oc);
    report_outcome(&oc, path);
    return oc.status == R0S1_OUT_OK ? 0 : -1;
}

static void gc_report(const char *tag) {
    fprintf(stderr,
            "glon: gc: %s collect=%ld live_cells=%ld live_objs=%ld "
            "free_cells=%ld free_blocks=%ld heap_high=%ld reclaimed=%ld\n",
            tag, r0_s1_gc_count(), r0_s1_gc_live_cells(),
            r0_s1_gc_live_objs(), r0_s1_gc_free_cells(),
            r0_s1_gc_free_blocks(), r0_s1_heap_high(),
            r0_s1_gc_reclaimed());
}

static int drain_io(glon_io_cli *io, const char *progress,
                    int *progress_done, int report, int gc_report_enabled,
                    int cancel_before_reply) {
    long requests = 0;
    for (;;) {
        uint8_t req[GLON_IO_MAX_FRAME], rep[GLON_IO_MAX_FRAME];
        size_t req_len = 0, rep_len = 0;
        int pending = 0;
        char err[512];

        if (take_request(req, sizeof req, &req_len, &pending, err, sizeof err) != 0) {
            fprintf(stderr, "glon: io: %s\n", err);
            return -1;
        }
        if (!pending) {
            if (gc_report_enabled) gc_report("job-end");
            return 0;
        }
        requests++;
        if (gc_report_enabled && requests % 100 == 0) gc_report("stream");

        glon_io_request q;
        if (glon_io_decode_request(req, req_len, &q, err, sizeof err) != 0) {
            fprintf(stderr, "glon: io: %s\n", err);
            return -1;
        }
        int st = glon_io_cli_submit(io, req, req_len, err, sizeof err);
        if (st != GLON_IO_OK) {
            fprintf(stderr, "glon: io: submit: %s\n", err);
            return -1;
        }

        if (progress && !*progress_done) {
            *progress_done = 1;
            if (run_source(progress) != 0) {
                fprintf(stderr, "glon: io: progress job failed\n");
                glon_io_cli_cancel(io);
                (void)glon_io_cli_wait(io, rep, sizeof rep, &rep_len, err, sizeof err);
                return -1;
            }
            fprintf(stderr, "glon: io: progress job completed while request %u was pending\n",
                    q.request_id);
        }

        if (cancel_before_reply) {
            fprintf(stderr, "glon: io: cancelling request %u before reply\n", q.request_id);
            glon_io_cli_cancel(io);
            (void)glon_io_cli_wait(io, rep, sizeof rep, &rep_len, err, sizeof err);
            return -1;
        }

        if (glon_io_cli_wait(io, rep, sizeof rep, &rep_len, err, sizeof err) != 0) {
            fprintf(stderr, "glon: io: wait: %s\n", err);
            return -1;
        }
        if (report)
            fprintf(stderr, "glon: io: reply %u status %u\n", q.request_id, rep[5]);
        if (run_io_reply(rep, rep_len, q.request_id, q.op, err, sizeof err) != 0) {
            if (gc_report_enabled) gc_report("failure");
            fprintf(stderr, "glon: io: %s\n", err);
            return -1;
        }
    }
}

static void report_io(glon_io_cli *io) {
    unsigned failed = 0;
    unsigned open = glon_io_cli_open_handles(io);
    unsigned closed = glon_io_cli_cleanup(io, &failed);
    fprintf(stderr, "glon: io: handles_open=%u cleanup_closed=%u close_failed=%u\n",
            open, closed, failed);
}
#endif /* GLON_MODULE_FILE */

/* ---- main ---------------------------------------------------------------- */

int main(int argc, char **argv) {
#ifdef GLON_MODULE_FILE
    glon_io_policy io_policy;
    glon_io_policy_default(&io_policy);
    const char *io_progress = NULL;
    int io_report = 0, io_gc_report = 0, io_cancel = 0, first_file = 1;
    if (parse_io_options(argc, argv, &first_file, &io_policy, &io_progress,
                         &io_report, &io_gc_report, &io_cancel) != 0) {
        io_usage();
        return 2;
    }
#else
    int first_file = 1;
#endif
    if (first_file >= argc) {
        fprintf(stderr, "usage: glon FILE...\n");
        return 2;
    }

    cell main_entry = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = main_entry;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    if (load_default_libraries() != 0) return 2;

#ifdef GLON_MODULE_FILE
    if (io_gc_report) gc_report("start");
    glon_io_cli *io = NULL;
    char io_err[512];
    if (glon_io_cli_init(&io, &io_policy, io_err, sizeof io_err) != 0) {
        fprintf(stderr, "glon: io: %s\n", io_err);
        return 2;
    }
#endif

    int rc = 0;
#ifdef GLON_MODULE_FILE
    int io_reported = 0;
#endif
    for (int i = first_file; i < argc; i++) {
        char err[512];
        char *src = read_source(argv[i], err, sizeof err);
        if (!src) { fprintf(stderr, "glon: %s: %s\n", argv[i], err); rc = 2; break; }

        strip_comments(src);
        int perr = 0;
        cell prog = r0_s1_parse(src, &perr);
        free(src);

        r0_s1_outcome oc;
        if (perr) r0_s1_session_parse_error(&oc);
        else r0_s1_session_run_block(prog, &oc);

        report_outcome(&oc, argv[i]);
        if (oc.status != R0S1_OUT_OK) { rc = 1; break; }

#ifdef GLON_MODULE_FILE
        int progress_done = 0;
        if (drain_io(io, io_progress, &progress_done, io_report,
                     io_gc_report, io_cancel) != 0) {
            rc = 1; break;
        }
        if (io_report) { report_io(io); io_reported = 1; }
        else (void)glon_io_cli_cleanup(io, NULL);
#else
        /* minimal build: no file module, no request/reply driver */
#endif
    }

#ifdef GLON_MODULE_FILE
    /* Early errors can leave handles from a job that never reached its
     * per-file report.  Always reclaim them; report only when requested. */
    {
        unsigned failed = 0;
        unsigned open = glon_io_cli_open_handles(io);
        unsigned closed = glon_io_cli_cleanup(io, &failed);
        if (io_report && !io_reported)
            fprintf(stderr, "glon: io: handles_open=%u cleanup_closed=%u close_failed=%u\n",
                    open, closed, failed);
    }
    glon_io_cli_destroy(io);
#endif
    return rc;
}
