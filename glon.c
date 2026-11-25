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
#ifdef GLON_MODULE_HTTP
#include "glon_http.h"
#endif
#ifdef GLON_MODULE_SEC
#include "glon_sec.h"
#include <openssl/crypto.h>
#endif
#ifdef GLON_MODULE_SQLITE
#include "glon_sqlite.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#elif defined(__APPLE__)
#include <unistd.h>
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

#define MAX_SOURCE (1u << 20)

#ifdef GLON_MODULE_SQLITE
static glon_sqlite_service sqlite_service;
static int sqlite_loaded;
#endif

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
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, exe_dir, (DWORD)sizeof exe_dir);
    if (n == 0 || n >= sizeof exe_dir) return -1;
    exe_dir[n] = 0;
    char *slash = strrchr(exe_dir, '\\');
    if (!slash) slash = strrchr(exe_dir, '/');
    if (slash) *slash = 0; else strcpy(exe_dir, ".");
    return 0;
#elif defined(__APPLE__)
    char raw[sizeof exe_dir];
    uint32_t size = (uint32_t)sizeof raw;
    if (_NSGetExecutablePath(raw, &size) != 0) return -1;
    if (!realpath(raw, exe_dir)) {
        if (strlen(raw) >= sizeof exe_dir) return -1;
        strcpy(exe_dir, raw);
    }
    char *slash = strrchr(exe_dir, '/');
    if (slash) *slash = 0; else strcpy(exe_dir, ".");
    return 0;
#else
    ssize_t n = readlink("/proc/self/exe", exe_dir, sizeof exe_dir - 1);
    if (n <= 0) return -1;
    exe_dir[n] = 0;
    char *slash = strrchr(exe_dir, '/');
    if (slash) *slash = 0; else strcpy(exe_dir, ".");
    return 0;
#endif
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
#if defined(GLON_MODULE_FILE) || defined(GLON_MODULE_HTTP) || defined(GLON_MODULE_SEC) || defined(GLON_MODULE_SQLITE) || defined(GLON_MODULE_TASK)
    /* strings-ext supplies s/from-int, used by HTTP, file, sec and sqlite. */
    snprintf(path, sizeof path, "%s/glon-lib/strings-ext.glon", exe_dir);
    if (load_library(path) != 0) return -1;
#endif
#ifdef GLON_MODULE_FILE
    /* Ordinary Glon libraries; the minimal binary never loads them. */
    static const char *module_names[] = { "file.glon", "file-stream.glon" };
    for (unsigned k = 0; k < sizeof module_names / sizeof module_names[0]; k++) {
        snprintf(path, sizeof path, "%s/glon-lib/%s", exe_dir, module_names[k]);
        if (load_library(path) != 0) return -1;
    }
#endif
#ifdef GLON_MODULE_HTTP
    /* Ordinary Glon over the host request/reply boundary; never loaded by the
     * minimal binary. */
    snprintf(path, sizeof path, "%s/glon-lib/http.glon", exe_dir);
    if (load_library(path) != 0) return -1;
#endif
#ifdef GLON_MODULE_SEC
    /* Ordinary Glon over the optional host security service. */
    snprintf(path, sizeof path, "%s/glon-lib/sec.glon", exe_dir);
    if (load_library(path) != 0) return -1;
#endif
    return 0;
}

#ifdef GLON_MODULE_SQLITE
/* Load SQLite vocabulary only when a source actually references it.  This
 * keeps a combined binary from spending global bindings in applications (such
 * as G3B) that never use SQLite. */
static int load_sqlite_library(void) {
    char path[4096];
    snprintf(path, sizeof path, "%s/glon-lib/sqlite.glon", exe_dir);
    if (load_library(path) != 0) return -1;
    sqlite_loaded = 1;
    return 0;
}
#endif

#ifdef GLON_MODULE_TASK
/* Load the cooperative-task vocabulary only when a source references it, so a
 * combined binary does not spend global bindings in applications that never
 * spawn tasks. */
static int task_loaded;
static int load_task_library(void) {
    char path[4096];
    snprintf(path, sizeof path, "%s/glon-lib/task.glon", exe_dir);
    if (load_library(path) != 0) return -1;
    task_loaded = 1;
    return 0;
}
#endif

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
            fprintf(stderr, "glon: %s: ** halted (reason %d: no SIN!: a machine-level fail-stop)\n",
                    path, r0_s1_halt_reason());
        return;
    default:
        fprintf(stderr, "glon: %s: ** unknown session outcome\n", path);
        return;
    }
}

#if defined(GLON_MODULE_SEC) || defined(GLON_MODULE_SQLITE)
#ifdef GLON_MODULE_HTTP
static int pump_pre(void *user);
#endif
#endif

#if defined(GLON_MODULE_FILE) || defined(GLON_MODULE_HTTP) || defined(GLON_MODULE_SQLITE) || defined(GLON_MODULE_TASK)
/* ---- optional module options -------------------------------------------- */

typedef struct {
#ifdef GLON_MODULE_FILE
    glon_io_policy io_policy;
    const char *io_progress;
    int io_report;
    int io_gc_report;
    int io_cancel;
#endif
#ifdef GLON_MODULE_HTTP
    glon_http_config http;
    int http_report;
#endif
#ifdef GLON_MODULE_SQLITE
    glon_sqlite_config sqlite;
    int sqlite_report;
#endif
} module_options;

#if defined(GLON_MODULE_FILE) || defined(GLON_MODULE_HTTP) || defined(GLON_MODULE_TASK)
static int parse_uint_opt(const char *s, unsigned *out) {
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (!s[0] || !end || *end || v > 0x7ffffffful) return -1;
    *out = (unsigned)v;
    return 0;
}
#endif

static void module_options_init(module_options *o) {
    memset(o, 0, sizeof *o);
#ifdef GLON_MODULE_FILE
    glon_io_policy_default(&o->io_policy);
#endif
#ifdef GLON_MODULE_HTTP
    glon_http_config_default(&o->http);
#if defined(GLON_MODULE_SEC) || defined(GLON_MODULE_SQLITE)
    o->http.pre_pump = pump_pre;
    o->http.pre_pump_user = NULL;
#endif
#endif
#ifdef GLON_MODULE_SQLITE
    glon_sqlite_config_default(&o->sqlite);
    o->sqlite_report = 0;
#endif
}

static void module_usage(void) {
    fprintf(stderr, "usage: glon");
#ifdef GLON_MODULE_FILE
    fprintf(stderr,
        " [--io-root DIR] [--allow-file-read] [--allow-file-write]\n"
        "            [--io-max-handles N] [--io-max-file BYTES]\n"
        "            [--io-delay-ms N] [--io-short N] [--io-progress FILE]\n"
        "            [--io-fail-close] [--io-fail-rename]\n"
        "            [--io-report] [--gc-report] [--io-cancel-before-reply]");
#endif
#ifdef GLON_MODULE_HTTP
    fprintf(stderr,
        " [--http-cert FILE] [--http-bind ADDR] [--http-max-requests N]\n"
        "            [--http-timeout-ms N] [--http-max-request-size N]\n"
        "            [--http-max-body N] [--http-threads N] [--http-bridge]\n"
        "            [--http-keep-alive]\n"
        "            [--http-report] [--http-stop-event NAME]");
#endif
#ifdef GLON_MODULE_SQLITE
    fprintf(stderr,
        " [--io-root DIR] [--allow-file-read] [--allow-file-write]"
        " [--sqlite-report]");
#endif
    fprintf(stderr, " FILE...\n");
}

static int parse_module_options(int argc, char **argv, int *first_file,
                                module_options *o) {
    int i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1] == '-') {
        const char *a = argv[i];
        if (!strcmp(a, "--")) { i++; break; }
#ifdef GLON_MODULE_FILE
        else if (!strcmp(a, "--io-root")) {
            if (++i >= argc) return -1;
            o->io_policy.root = argv[i];
        } else if (!strcmp(a, "--allow-file-read")) {
            o->io_policy.allow_read = 1;
        } else if (!strcmp(a, "--allow-file-write")) {
            o->io_policy.allow_write = 1;
        } else if (!strcmp(a, "--io-max-handles")) {
            unsigned v; if (++i >= argc || parse_uint_opt(argv[i], &v)) return -1;
            o->io_policy.max_handles = v;
        } else if (!strcmp(a, "--io-max-file")) {
            unsigned v; if (++i >= argc || parse_uint_opt(argv[i], &v) || v == 0) return -1;
            o->io_policy.max_file = v;
        } else if (!strcmp(a, "--io-delay-ms")) {
            unsigned v; if (++i >= argc || parse_uint_opt(argv[i], &v)) return -1;
            o->io_policy.delay_ms = v;
        } else if (!strcmp(a, "--io-short")) {
            unsigned v; if (++i >= argc || parse_uint_opt(argv[i], &v) || v == 0) return -1;
            o->io_policy.short_io = v;
        } else if (!strcmp(a, "--io-fail-close")) {
            o->io_policy.fail_close = 1;
        } else if (!strcmp(a, "--io-fail-rename")) {
            o->io_policy.fail_rename = 1;
        } else if (!strcmp(a, "--io-progress")) {
            if (++i >= argc) return -1;
            o->io_progress = argv[i];
        } else if (!strcmp(a, "--io-report")) {
            o->io_report = 1;
        } else if (!strcmp(a, "--gc-report")) {
            o->io_gc_report = 1;
        } else if (!strcmp(a, "--io-cancel-before-reply")) {
            o->io_cancel = 1;
        }
#endif
#ifdef GLON_MODULE_SQLITE
        else if (!strcmp(a, "--io-root")) {
            if (++i >= argc) return -1;
            o->sqlite.root = argv[i];
        } else if (!strcmp(a, "--allow-file-read")) {
            o->sqlite.allow_read = 1;
        } else if (!strcmp(a, "--allow-file-write")) {
            o->sqlite.allow_write = 1;
        } else if (!strcmp(a, "--sqlite-report")) {
            o->sqlite_report = 1;
        }
#endif
#ifdef GLON_MODULE_HTTP
        else if (!strcmp(a, "--http-cert")) {
            if (++i >= argc) return -1;
            o->http.cert = argv[i];
        } else if (!strcmp(a, "--http-bind")) {
            if (++i >= argc || !argv[i][0]) return -1;
            o->http.bind = argv[i];
        } else if (!strcmp(a, "--http-max-requests")) {
            if (++i >= argc || parse_uint_opt(argv[i], &o->http.max_requests)) return -1;
        } else if (!strcmp(a, "--http-timeout-ms")) {
            unsigned v; if (++i >= argc || parse_uint_opt(argv[i], &v) || v == 0) return -1;
            o->http.request_timeout_ms = v;
        } else if (!strcmp(a, "--http-max-request-size")) {
            unsigned v; if (++i >= argc || parse_uint_opt(argv[i], &v) || v == 0) return -1;
            o->http.max_request_size = v;
        } else if (!strcmp(a, "--http-max-body")) {
            unsigned v; if (++i >= argc || parse_uint_opt(argv[i], &v) ||
                           v == 0 || v > GLON_HTTP_REQUEST_BODY_HARD) return -1;
            o->http.max_body = v;
        } else if (!strcmp(a, "--http-keep-alive")) {
            o->http.keep_alive = 1;
        } else if (!strcmp(a, "--http-threads")) {
            unsigned v; if (++i >= argc || parse_uint_opt(argv[i], &v) ||
                           v == 0 || v > GLON_HTTP_MAX_THREADS) return -1;
            o->http.num_threads = v;
        } else if (!strcmp(a, "--http-bridge")) {
            o->http.bridge_mode = 1;
        } else if (!strcmp(a, "--http-report")) {
            o->http_report = 1;
        } else if (!strcmp(a, "--http-stop-event")) {
            if (++i >= argc || !argv[i][0]) return -1;
            o->http.stop_event = argv[i];
        }
#endif
        else {
            return -1;
        }
        i++;
    }
    *first_file = i;
    return 0;
}
#endif /* GLON_MODULE_FILE || GLON_MODULE_HTTP || GLON_MODULE_SQLITE || GLON_MODULE_TASK */

#if defined(GLON_MODULE_FILE) || defined(GLON_MODULE_SEC) || defined(GLON_MODULE_SQLITE) || defined(GLON_MODULE_TASK)
/* ---- shared host expression runner --------------------------------------- */

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
#endif

#ifdef GLON_MODULE_FILE
/* ---- optional native file module driver --------------------------------- */

#define IO_REPLY_SRC_CAP 32768

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
    if (id == 0 || op < GLON_IO_OP_OPEN || op > GLON_IO_OP_DISCARD) {
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
    if (status > GLON_IO_STALE) {
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

    if (status == GLON_IO_OK && (op == GLON_IO_OP_OPEN || op == GLON_IO_OP_TEMP_OPEN)) {
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
        /* The loader parser has a 512-value block-literal limit, so a large
         * read reply is emitted as a chain of <=400-byte mk-string blocks. */
        const uint32_t chunk = 400;
        uint32_t chunks = al ? (al + chunk - 1) / chunk : 0;
        for (uint32_t c = 1; c < chunks; c++)
            n += snprintf(src + n, sizeof src - (size_t)n, "s/+ ");
        if (chunks == 0) {
            n += snprintf(src + n, sizeof src - (size_t)n, "mk-string [ ]");
        } else {
            for (uint32_t c = 0; c < chunks; c++) {
                uint32_t start = c * chunk;
                uint32_t end = start + chunk;
                if (end > al) end = al;
                n += snprintf(src + n, sizeof src - (size_t)n, "mk-string [");
                for (uint32_t i = start; i < end && (size_t)n + 6 < sizeof src; i++)
                    n += snprintf(src + n, sizeof src - (size_t)n, " %u", a[i]);
                n += snprintf(src + n, sizeof src - (size_t)n, " ]");
            }
        }
        n += snprintf(src + n, sizeof src - (size_t)n, " 0 ]");
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

        if (!*progress_done) {
            *progress_done = 1;
            if (progress && run_source(progress) != 0) {
                fprintf(stderr, "glon: io: progress job failed\n");
                glon_io_cli_cancel(io);
                (void)glon_io_cli_wait(io, rep, sizeof rep, &rep_len, err, sizeof err);
                return -1;
            }
            if (progress)
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

/* Host pump for the HTTP worker: drain pending asynchronous file requests only
 * after an HTTP dispatch turn has completed. */
static int pump_file_io(void *user) {
    int progress_done = 1;
    return drain_io((glon_io_cli *)user, NULL, &progress_done, 0, 0, 0);
}
#endif /* GLON_MODULE_FILE */


#ifdef GLON_MODULE_SEC
/* ---- optional security host driver -------------------------------------- */

#define SEC_REPLY_SRC_CAP 16384

static int append_bytes_expr(char *src, size_t cap, int *used,
                             const unsigned char *bytes, size_t len) {
    int n = *used;
    if (n < 0 || (size_t)n >= cap) return -1;
    n += snprintf(src + n, cap - (size_t)n, "mk-string [");
    if (n < 0 || (size_t)n >= cap) return -1;
    for (size_t i = 0; i < len; i++) {
        n += snprintf(src + n, cap - (size_t)n, " %u", (unsigned)bytes[i]);
        if (n < 0 || (size_t)n >= cap) return -1;
    }
    n += snprintf(src + n, cap - (size_t)n, " ]");
    if (n < 0 || (size_t)n >= cap) return -1;
    *used = n;
    return 0;
}

static int take_sec_request(glon_sec_request *req, uint32_t *id, int *pending,
                            char *err, size_t errcap) {
    static const char expr[] = "[ sec/take-request ]";
    r0_s1_outcome oc;
    (void)run_expression(expr, &oc);
    *pending = 0;
    if (oc.status != R0S1_OUT_OK) {
        snprintf(err, errcap, "sec/take-request failed (status %d)", oc.status);
        return -1;
    }
    if (oc.count <= 0 ||
        (oc.count == 1 && r0_s1_result(0, oc.count) == R0_NONE))
        return 0;
    if (oc.count != 1 || r0_tag(r0_s1_result(0, oc.count)) != T_BLOCK) {
        snprintf(err, errcap, "sec/take-request did not return a BLOCK or NONE");
        return -1;
    }
    cell p = r0_untag(r0_s1_result(0, oc.count));
    if (M[p] != 3) {
        snprintf(err, errcap, "sec/take-request block shape");
        return -1;
    }
    cell idv = M[p + BLK_DATA];
    cell opv = M[p + BLK_DATA + 1];
    cell fields = M[p + BLK_DATA + 2];
    if (r0_tag(idv) != T_INT || r0_tag(opv) != T_INT ||
        r0_tag(fields) != T_BLOCK) {
        snprintf(err, errcap, "sec/take-request field types");
        return -1;
    }
    uint32_t rid = (uint32_t)int_val(idv);
    unsigned op = (unsigned)int_val(opv);
    if (rid == 0 || op < GLON_SEC_OP_RANDOM || op > GLON_SEC_OP_TIME) {
        snprintf(err, errcap, "sec/take-request id/op");
        return -1;
    }
    cell fp = r0_untag(fields);
    cell nf = M[fp];
    if (nf < 0 || nf > GLON_SEC_MAX_FIELDS) {
        snprintf(err, errcap, "sec/take-request too many fields");
        return -1;
    }
    memset(req, 0, sizeof *req);
    req->op = op;
    req->nfields = (unsigned)nf;
    for (cell i = 0; i < nf; i++) {
        cell sv = M[fp + BLK_DATA + i];
        if (r0_tag(sv) != T_STRING) {
            snprintf(err, errcap, "sec field is not a STRING!");
            return -1;
        }
        cell sp = r0_untag(sv);
        cell sl = M[sp];
        if (sl < 0 || (size_t)sl > GLON_SEC_FIELD_CAP) {
            snprintf(err, errcap, "sec field is too large");
            return -1;
        }
        req->field_len[i] = (size_t)sl;
        for (cell j = 0; j < sl; j++)
            req->fields[i][j] = (unsigned char)M[sp + 1 + j];
    }
    *id = rid;
    *pending = 1;
    return 0;
}

static int run_sec_reply(uint32_t id, int ok,
                         const unsigned char *v1, size_t v1len,
                         const unsigned char *v2, size_t v2len,
                         char *err, size_t errcap) {
    char src[SEC_REPLY_SRC_CAP];
    int n = snprintf(src, sizeof src, "[ sec/reply %u '%s ",
                     id, ok ? "ok" : "error");
    if (n < 0 || (size_t)n >= sizeof src) {
        snprintf(err, errcap, "sec reply source too large");
        return -1;
    }
    if (append_bytes_expr(src, sizeof src, &n, v1, v1len) != 0 ||
        (n += snprintf(src + n, sizeof src - (size_t)n, " "), n < 0) ||
        append_bytes_expr(src, sizeof src, &n, v2, v2len) != 0) {
        snprintf(err, errcap, "sec reply source too large");
        return -1;
    }
    n += snprintf(src + n, sizeof src - (size_t)n, " ]");
    if (n < 0 || (size_t)n >= sizeof src) {
        snprintf(err, errcap, "sec reply source too large");
        return -1;
    }
    r0_s1_outcome oc;
    (void)run_expression(src, &oc);
    if (oc.status != R0S1_OUT_OK) {
        report_outcome(&oc, "sec/reply");
        snprintf(err, errcap, "sec/reply callback failed (status %d)", oc.status);
        return -1;
    }
    return 0;
}

/* Returns -1 on an error, 0 when no request was pending, and 1 after at least
 * one request/reply turn was completed.  The caller may loop until 0. */
static int drain_sec(char *err, size_t errcap) {
    int processed = 0;
    for (;;) {
        glon_sec_request req;
        uint32_t id = 0;
        int pending = 0;
        if (take_sec_request(&req, &id, &pending, err, errcap) != 0)
            return -1;
        if (!pending)
            return processed;
        unsigned char out1[GLON_SEC_OUT_CAP];
        unsigned char out2[GLON_SEC_OUT_CAP];
        size_t out1_len = 0, out2_len = 0;
        char op_err[128] = {0};
        int ok = glon_sec_execute(&req, out1, &out1_len, out2, &out2_len,
                                  op_err, sizeof op_err) == 0;
        OPENSSL_cleanse(&req, sizeof req);
        int rr = ok
            ? run_sec_reply(id, 1, out1, out1_len, out2, out2_len, err, errcap)
            : run_sec_reply(id, 0, (const unsigned char *)op_err,
                            strlen(op_err), NULL, 0, err, errcap);
        OPENSSL_cleanse(out1, sizeof out1);
        OPENSSL_cleanse(out2, sizeof out2);
        if (rr != 0)
            return -1;
        processed = 1;
    }
}

#endif /* GLON_MODULE_SEC */

#ifdef GLON_MODULE_SQLITE
/* ---- optional SQLite host driver ---------------------------------------- */

#define SQLITE_REPLY_SRC_CAP 65536

static int sqlite_append_bytes_expr(char *src, size_t cap, int *used,
                                    const unsigned char *bytes, size_t len) {
    int n = *used;
    if (n < 0 || (size_t)n >= cap) return -1;
    n += snprintf(src + n, cap - (size_t)n, "mk-string [");
    if (n < 0 || (size_t)n >= cap) return -1;
    for (size_t i = 0; i < len; i++) {
        n += snprintf(src + n, cap - (size_t)n, " %u", (unsigned)bytes[i]);
        if (n < 0 || (size_t)n >= cap) return -1;
    }
    n += snprintf(src + n, cap - (size_t)n, " ]");
    if (n < 0 || (size_t)n >= cap) return -1;
    *used = n;
    return 0;
}

static int sqlite_append_row_expr(char *src, size_t cap, int *used,
                                  const unsigned char *row, size_t len) {
    if (len < 1) return -1;
    unsigned ncols = row[0];
    size_t pos = 1;
    int n = *used;
    n += snprintf(src + n, cap - (size_t)n, "reduce [");
    if (n < 0 || (size_t)n >= cap) return -1;
    for (unsigned col = 0; col < ncols; col++) {
        if (pos >= len) return -1;
        unsigned type = row[pos++];
        n += snprintf(src + n, cap - (size_t)n, " ");
        if (type == 0) {
            n += snprintf(src + n, cap - (size_t)n, "none");
        } else if (type == 1) {
            if (pos + 8 > len) return -1;
            uint64_t u = 0;
            for (int i = 0; i < 8; i++) u = (u << 8) | row[pos + (size_t)i];
            pos += 8;
            long long v = (long long)u;
            /* Glon native integers are about +-2^59 (WASM is smaller).  Keep
             * SQLite's full int64 range representable without feeding an
             * out-of-range source literal to the parser. */
            if (v > 576460752303423487LL || v < -576460752303423488LL) {
                char dec[32];
                int dn = snprintf(dec, sizeof dec, "%lld", v);
                if (dn < 0 || (size_t)dn >= sizeof dec) return -1;
                n += snprintf(src + n, cap - (size_t)n, "reduce [ 'big-int ");
                if (n < 0 || (size_t)n >= cap) return -1;
                if (sqlite_append_bytes_expr(src, cap, &n,
                                              (const unsigned char *)dec,
                                              (size_t)dn) != 0) return -1;
                n += snprintf(src + n, cap - (size_t)n, " ]");
                if (n < 0 || (size_t)n >= cap) return -1;
            } else {
                n += snprintf(src + n, cap - (size_t)n, "%lld", v);
            }
        } else if (type == 2 || type == 3 || type == 4) {
            if (pos + 4 > len) return -1;
            uint32_t blen = ((uint32_t)row[pos] << 24) |
                            ((uint32_t)row[pos + 1] << 16) |
                            ((uint32_t)row[pos + 2] << 8) |
                            (uint32_t)row[pos + 3];
            pos += 4;
            if (blen > len - pos) return -1;
            if (type == 2) {
                if (sqlite_append_bytes_expr(src, cap, &n, row + pos, blen) != 0)
                    return -1;
            } else {
                n += snprintf(src + n, cap - (size_t)n, "reduce [ '%s ",
                              type == 3 ? "blob" : "real");
                if (n < 0 || (size_t)n >= cap) return -1;
                if (sqlite_append_bytes_expr(src, cap, &n, row + pos, blen) != 0)
                    return -1;
                n += snprintf(src + n, cap - (size_t)n, " ]");
            }
            pos += blen;
        } else {
            return -1;
        }
        if (n < 0 || (size_t)n >= cap) return -1;
    }
    n += snprintf(src + n, cap - (size_t)n, " ]");
    if (n < 0 || (size_t)n >= cap) return -1;
    *used = n;
    return 0;
}

static int take_sqlite_request(glon_sqlite_request *req, uint32_t *id,
                               int *pending, char *err, size_t errcap) {
    static const char expr[] = "[ sqlite/take-request ]";
    r0_s1_outcome oc;
    (void)run_expression(expr, &oc);
    *pending = 0;
    if (oc.status != R0S1_OUT_OK) {
        snprintf(err, errcap, "sqlite/take-request failed (status %d)", oc.status);
        return -1;
    }
    if (oc.count <= 0 ||
        (oc.count == 1 && r0_s1_result(0, oc.count) == R0_NONE))
        return 0;
    if (oc.count != 1 || r0_tag(r0_s1_result(0, oc.count)) != T_BLOCK) {
        snprintf(err, errcap, "sqlite/take-request did not return a BLOCK or NONE");
        return -1;
    }
    cell p = r0_untag(r0_s1_result(0, oc.count));
    if (M[p] != 3) {
        snprintf(err, errcap, "sqlite/take-request block shape");
        return -1;
    }
    cell idv = M[p + BLK_DATA];
    cell opv = M[p + BLK_DATA + 1];
    cell fields = M[p + BLK_DATA + 2];
    if (r0_tag(idv) != T_INT || r0_tag(opv) != T_INT ||
        r0_tag(fields) != T_BLOCK) {
        snprintf(err, errcap, "sqlite/take-request field types");
        return -1;
    }
    uint32_t rid = (uint32_t)int_val(idv);
    unsigned op = (unsigned)int_val(opv);
    if (rid == 0 || op < GLON_SQLITE_OP_OPEN || op > GLON_SQLITE_OP_CLOSE) {
        snprintf(err, errcap, "sqlite/take-request id/op");
        return -1;
    }
    cell fp = r0_untag(fields);
    cell nf = M[fp];
    if (nf < 0 || nf > GLON_SQLITE_MAX_FIELDS) {
        snprintf(err, errcap, "sqlite/take-request too many fields");
        return -1;
    }
    memset(req, 0, sizeof *req);
    req->op = op;
    req->nfields = (unsigned)nf;
    for (cell i = 0; i < nf; i++) {
        cell sv = M[fp + BLK_DATA + i];
        if (r0_tag(sv) != T_STRING) {
            snprintf(err, errcap, "sqlite field is not a STRING!");
            return -1;
        }
        cell sp = r0_untag(sv);
        cell sl = M[sp];
        if (sl < 0 || (size_t)sl > GLON_SQLITE_FIELD_CAP) {
            snprintf(err, errcap, "sqlite field is too large");
            return -1;
        }
        req->field_len[i] = (size_t)sl;
        for (cell j = 0; j < sl; j++)
            req->fields[i][j] = (unsigned char)M[sp + 1 + j];
    }
    *id = rid;
    *pending = 1;
    return 0;
}

static int run_sqlite_reply(uint32_t id, int op,
                            const glon_sqlite_result *result,
                            char *err, size_t errcap) {
    char src[SQLITE_REPLY_SRC_CAP];
    const char *word = glon_sqlite_status_word(result->status);
    int n = snprintf(src, sizeof src, "[ sqlite/reply %u '%s ", id, word);
    if (n < 0 || (size_t)n >= sizeof src) return -1;
    if (result->status == GLON_SQLITE_ROW) {
        if (sqlite_append_row_expr(src, sizeof src, &n, result->data, result->len) != 0)
            return -1;
    } else if (result->status == GLON_SQLITE_OK) {
        if (sqlite_append_bytes_expr(src, sizeof src, &n, result->data, result->len) != 0)
            return -1;
    } else if (result->status == GLON_SQLITE_DONE) {
        if (sqlite_append_bytes_expr(src, sizeof src, &n, NULL, 0) != 0) return -1;
    } else {
        if (sqlite_append_bytes_expr(src, sizeof src, &n,
                                     (const unsigned char *)result->error,
                                     strlen(result->error)) != 0) return -1;
    }
    n += snprintf(src + n, sizeof src - (size_t)n, " mk-string [ ] ]");
    if (n < 0 || (size_t)n >= sizeof src) return -1;
    (void)op;
    r0_s1_outcome oc;
    (void)run_expression(src, &oc);
    if (oc.status != R0S1_OUT_OK) {
        report_outcome(&oc, "sqlite/reply");
        snprintf(err, errcap, "sqlite/reply callback failed (status %d)", oc.status);
        return -1;
    }
    return 0;
}

static int drain_sqlite(glon_sqlite_service *svc, char *err, size_t errcap) {
    int processed = 0;
    for (;;) {
        glon_sqlite_request req;
        glon_sqlite_result result;
        uint32_t id = 0;
        int pending = 0;
        if (take_sqlite_request(&req, &id, &pending, err, errcap) != 0)
            return -1;
        if (!pending)
            return processed;
        (void)glon_sqlite_execute(svc, &req, &result, err, errcap);
        if (run_sqlite_reply(id, req.op, &result, err, errcap) != 0)
            return -1;
        processed = 1;
    }
}
#endif /* GLON_MODULE_SQLITE */

#if defined(GLON_MODULE_SEC) || defined(GLON_MODULE_SQLITE)
/* Drain every pre-response service until all queues are empty.  A callback may
 * enqueue another service, so both are polled again after each turn. */
static int drain_pre(char *err, size_t errcap) {
    int any = 0;
    for (;;) {
        int did = 0;
#ifdef GLON_MODULE_SEC
        int r = drain_sec(err, errcap);
        if (r < 0) return -1;
        if (r > 0) { did = 1; any = 1; }
#endif
#ifdef GLON_MODULE_SQLITE
        if (sqlite_loaded) {
            int q = drain_sqlite(&sqlite_service, err, errcap);
            if (q < 0) return -1;
            if (q > 0) { did = 1; any = 1; }
        }
#endif
        if (!did) return any;
    }
}

#ifdef GLON_MODULE_HTTP
static int pump_pre(void *user) {
    (void)user;
    char err[512];
    int r = drain_pre(err, sizeof err);
    if (r < 0) fprintf(stderr, "glon: pre-service pump: %s\n", err);
    return r;
}
#endif
#endif

#ifdef GLON_MODULE_TASK
/* M2 host pump: run the cooperative scheduler, then drain asynchronous
 * file/SQLite requests, and repeat until nothing makes progress.  A task that
 * suspended on I/O (task-wait) is woken by the reply callback (task-wake), so
 * the next run-tasks resumes it.  This is the only host-side scheduling help;
 * all policy stays in task.glon. */
#ifdef GLON_MODULE_FILE
static glon_io_cli *g_task_io;
#endif
static int tasks_pending(void) {
    r0_s1_outcome oc;
    if (run_expression("[ tasks/pending? ]", &oc) != 0 ||
        oc.status != R0S1_OUT_OK || oc.count != 1) return 0;
    cell v = r0_s1_result(0, oc.count);
    return (r0_tag(v) == T_INT && int_val(v) != 0) ? 1 : 0;
}
static int pump_tasks(const module_options *opts, char *err, size_t errcap) {
    for (long guard = 0; guard < 1000000; guard++) {
        r0_s1_outcome oc;
        if (run_expression("[ run-tasks ]", &oc) != 0 ||
            oc.status != R0S1_OUT_OK) {
            if (oc.status == R0S1_OUT_HALT)
                snprintf(err, errcap, "run-tasks halted (reason %d)",
                         r0_s1_halt_reason());
            else
                snprintf(err, errcap, "run-tasks failed (status %d)", oc.status);
            return -1;
        }
        int did = 0;
#ifdef GLON_MODULE_FILE
        {
            int pd = 0;
            if (drain_io(g_task_io, opts->io_progress, &pd, opts->io_report,
                         opts->io_gc_report, opts->io_cancel) != 0) {
                snprintf(err, errcap, "file drain failed");
                return -1;
            }
            if (pd) did = 1;
        }
#endif
#if defined(GLON_MODULE_SEC) || defined(GLON_MODULE_SQLITE)
        {
            char pe[512];
            int pr = drain_pre(pe, sizeof pe);
            if (pr < 0) { snprintf(err, errcap, "%s", pe); return -1; }
            if (pr > 0) did = 1;
        }
#endif
        if (!did) break;
    }
    return 0;
}
#endif

/* ---- main ---------------------------------------------------------------- */

int main(int argc, char **argv) {
#ifdef _WIN32
    /* Glon strings and files are byte sequences.  Keep stdout/stderr in binary
     * mode so the CLI's output is byte-identical to POSIX, without CRLF
     * translation by the CRT. */
    (void)_setmode(_fileno(stdout), _O_BINARY);
    (void)_setmode(_fileno(stderr), _O_BINARY);
#endif
#if defined(GLON_MODULE_FILE) || defined(GLON_MODULE_HTTP) || defined(GLON_MODULE_SQLITE) || defined(GLON_MODULE_TASK)
    module_options opts;
    module_options_init(&opts);
    int first_file = 1;
    if (parse_module_options(argc, argv, &first_file, &opts) != 0) {
        module_usage();
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
    for (int i = 0; i < M1_MAX_TASKS; i++) {
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_GEN] = 0;
    }

    if (load_default_libraries() != 0) return 2;

#ifdef GLON_MODULE_FILE
    if (opts.io_gc_report) gc_report("start");
    glon_io_cli *io = NULL;
    char io_err[512];
    if (glon_io_cli_init(&io, &opts.io_policy, io_err, sizeof io_err) != 0) {
        fprintf(stderr, "glon: io: %s\n", io_err);
        return 2;
    }
#ifdef GLON_MODULE_TASK
    g_task_io = io;
#endif
#endif

#ifdef GLON_MODULE_SQLITE
#ifdef GLON_MODULE_FILE
    /* The file and SQLite modules share one --io-root/capability policy. */
    opts.sqlite.root = opts.io_policy.root;
    opts.sqlite.allow_read = opts.io_policy.allow_read;
    opts.sqlite.allow_write = opts.io_policy.allow_write;
#endif
    {
        char sqlite_err[512];
        if (glon_sqlite_init(&sqlite_service, &opts.sqlite,
                             sqlite_err, sizeof sqlite_err) != 0) {
            fprintf(stderr, "glon: sqlite: %s\n", sqlite_err);
            return 2;
        }
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

#ifdef GLON_MODULE_SQLITE
        if (!sqlite_loaded && strstr(src, "sqlite/")) {
            if (load_sqlite_library() != 0) {
                free(src);
                rc = 2;
                break;
            }
        }
#endif
        strip_comments(src);
#ifdef GLON_MODULE_TASK
        if (!task_loaded && (strstr(src, "spawn") || strstr(src, "run-tasks") ||
                             strstr(src, "task-wait") || strstr(src, "tasks/"))) {
            if (load_task_library() != 0) {
                free(src);
                rc = 2;
                break;
            }
        }
#endif
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
        if (drain_io(io, opts.io_progress, &progress_done, opts.io_report,
                     opts.io_gc_report, opts.io_cancel) != 0) {
            rc = 1; break;
        }
        if (opts.io_report) { report_io(io); io_reported = 1; }
        else (void)glon_io_cli_cleanup(io, NULL);
#else
        /* minimal build: no file module, no request/reply driver */
#endif

#if defined(GLON_MODULE_SEC) || defined(GLON_MODULE_SQLITE)
        {
            char pre_err[512];
            if (drain_pre(pre_err, sizeof pre_err) < 0) {
                fprintf(stderr, "glon: service pump: %s\n", pre_err);
                rc = 1;
                break;
            }
        }
#endif
    }

#ifdef GLON_MODULE_TASK
    /* Run spawned cooperative tasks to completion, draining async I/O between
     * scheduler turns so a task can suspend on I/O and be resumed. */
    if (rc == 0 && task_loaded && tasks_pending()) {
        char terr[512];
        if (pump_tasks(&opts, terr, sizeof terr) != 0) {
            fprintf(stderr, "glon: task: %s\n", terr);
            rc = 1;
        }
    }
#endif

#ifdef GLON_MODULE_HTTP
    /* Start only after every source file has been loaded, and only if the
     * application actually called http/listen.  The native listener owns the
     * sockets/TLS/HTTP parser; the loaded Glon handler owns the response. */
    if (rc == 0) {
        char http_err[512];
#ifdef GLON_MODULE_FILE
        int hr = glon_http_run(&opts.http, opts.http_report,
                               pump_file_io, io, http_err, sizeof http_err);
#else
        int hr = glon_http_run(&opts.http, opts.http_report,
                               NULL, NULL, http_err, sizeof http_err);
#endif
        if (hr != 0) {
            fprintf(stderr, "glon: http: %s\n", http_err);
            rc = 2;
        }
    }
#endif

#ifdef GLON_MODULE_SQLITE
    {
        unsigned dbs = 0, stmts = 0;
        glon_sqlite_destroy(&sqlite_service, &dbs, &stmts);
        if (opts.sqlite_report)
            fprintf(stderr, "glon: sqlite: closed_dbs=%u finalized_stmts=%u\n",
                    dbs, stmts);
    }
#endif

#ifdef GLON_MODULE_FILE
    /* Early errors can leave handles from a job that never reached its
     * per-file report.  Always reclaim them; report only when requested. */
    {
        unsigned failed = 0;
        unsigned open = glon_io_cli_open_handles(io);
        unsigned closed = glon_io_cli_cleanup(io, &failed);
        if (opts.io_report && !io_reported)
            fprintf(stderr, "glon: io: handles_open=%u cleanup_closed=%u close_failed=%u\n",
                    open, closed, failed);
    }
    glon_io_cli_destroy(io);
#endif

    return rc;
}
