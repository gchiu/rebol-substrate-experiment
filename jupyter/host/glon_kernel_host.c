/* jupyter/host/glon_kernel_host.c -- a persistent native Glon session host.
 *
 * One process = one Glon session. The host initialises the runtime ONCE, loads
 * the notebook libraries, then runs cells one after another in the SAME
 * runtime, so definitions persist from cell to cell. It is infrastructure
 * around Glon: it adds no language semantics, and it classifies each outcome
 * with the structured session facade (r0_s1_session.c), never by reading text.
 *
 * Usage:
 *   glon-kernel-host --result-fd N [--lib FILE]...
 *
 * Channels:
 *   stdin (fd 0)   requests, framed (below)
 *   fd N           results, framed; N is inherited from the parent and named
 *                  explicitly on the command line (no fixed fd is assumed)
 *   stdout (fd 1)  raw Glon `print` output, never framed
 *   stderr (fd 2)  raw runtime diagnostics ([dump], stack sentry, ...)
 *
 * Framing (both directions): a 4-byte big-endian payload length, then the
 * payload. Binary-safe; no newline or NUL delimiters.
 *   request payload:  one opcode byte, then its argument
 *     'E' <source bytes>   execute one cell (source without the outer [ ])
 *     'R'                  restart: a fresh runtime, libraries reloaded
 *     'Q'                  quit (EOF on stdin also quits)
 *   result payload:   one JSON object (UTF-8). Every request gets exactly one
 *                     result; the host also sends one {"op":"ready"} result at
 *                     startup (and after a restart, {"op":"restart"}).
 *
 * Output attribution: stdout and stderr are replaced by counting streams, so
 * each execute result reports the exact number of bytes the cell wrote to
 * each ("stdout_bytes", "stderr_bytes"). Everything a cell writes is flushed
 * before its result is sent, so a reader that has consumed that many bytes of
 * each stream has all of the cell's output -- no markers on stdout. The host
 * writes output as the cell produces it; the parent must drain stdout/stderr
 * concurrently (the host never buffers a cell's output, so it cannot deadlock
 * on its own).
 *
 * Portability: POSIX read/write on inherited file descriptors, and
 * fopencookie (glibc and musl) for the counting streams. macOS would use
 * funopen; native Windows would need a different fd-passing scheme. Linux/WSL
 * is the milestone-1 target. */

#define _GNU_SOURCE
#include "r0_s1.h"
#include "m1_layout.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_REQUEST (1u << 20)
#define MAX_LIBS 16

static int result_fd = -1;
static const char *libs[MAX_LIBS];
static int nlibs;
static char init_error[512];

/* ---- counting stdout / stderr ------------------------------------------- */

static unsigned long out_bytes, err_bytes;

static ssize_t counting_write(void *cookie, const char *buf, size_t n) {
    int fd = (int)(intptr_t)cookie;
    size_t done = 0;
    while (done < n) {
        ssize_t w = write(fd, buf + done, n - done);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        done += (size_t)w;
    }
    if (fd == 1) out_bytes += done; else err_bytes += done;
    return done ? (ssize_t)done : -1;
}

static void install_counting_streams(void) {
    cookie_io_functions_t io = { NULL, counting_write, NULL, NULL };
    FILE *o = fopencookie((void *)(intptr_t)1, "w", io);
    FILE *e = fopencookie((void *)(intptr_t)2, "w", io);
    if (!o || !e) { perror("glon-kernel-host: fopencookie"); exit(1); }
    setvbuf(o, NULL, _IOLBF, 0);
    setvbuf(e, NULL, _IONBF, 0);
    stdout = o;
    stderr = e;
}

/* ---- framed I/O --------------------------------------------------------- */

static int read_exact(int fd, void *p, size_t n) {
    unsigned char *b = p;
    while (n) {
        ssize_t r = read(fd, b, n);
        if (r == 0) return 0;                       /* EOF */
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        b += r; n -= (size_t)r;
    }
    return 1;
}

static void write_all(int fd, const void *p, size_t n) {
    const unsigned char *b = p;
    while (n) {
        ssize_t w = write(fd, b, n);
        if (w < 0) { if (errno == EINTR) continue; exit(3); }
        b += w; n -= (size_t)w;
    }
}

/* growable JSON buffer */
static char *jb;
static size_t jlen, jcap;
static void jput(const char *s, size_t n) {
    if (jlen + n + 1 > jcap) {
        jcap = (jlen + n + 1) * 2;
        jb = realloc(jb, jcap);
        if (!jb) exit(4);
    }
    memcpy(jb + jlen, s, n);
    jlen += n;
    jb[jlen] = 0;
}
static void js(const char *s) { jput(s, strlen(s)); }
static void jnum(long v) { char t[32]; snprintf(t, sizeof t, "%ld", v); js(t); }
static void jstr(const char *s) {
    js("\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"') js("\\\"");
        else if (c == '\\') js("\\\\");
        else if (c == '\n') js("\\n");
        else if (c == '\t') js("\\t");
        else if (c < 0x20) { char t[8]; snprintf(t, sizeof t, "\\u%04x", c); js(t); }
        else jput((const char *)&c, 1);
    }
    js("\"");
}
static void send_result(void) {
    unsigned char h[4] = { (unsigned char)(jlen >> 24), (unsigned char)(jlen >> 16),
                           (unsigned char)(jlen >> 8), (unsigned char)jlen };
    write_all(result_fd, h, 4);
    write_all(result_fd, jb, jlen);
    jlen = 0;
}

/* ---- session ------------------------------------------------------------ */

static char molded[8192];
static void jvalue(cell v) {
    r0_s1_mold(v, molded, (int)sizeof molded);
    jstr(molded);
}

static void jsession(void) {
    int symbols = 0;
    while (r0_s1_sym_name(symbols)) symbols++;
    cell gcount = 0, gcap = 0;
    r0_s1_global_usage(&gcount, &gcap);
    js(",\"session\":{\"symbols\":"); jnum(symbols);
    js(",\"loader_free\":"); jnum((long)(R0S1_HEAP_LIMIT - M[GC_LOADER_HP]));
    js(",\"globals\":"); jnum((long)gcount);
    js(",\"global_capacity\":"); jnum((long)gcap);
    js("}");
}

static char filebuf[1 << 16];
static int load_lib(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(init_error, sizeof init_error, "cannot open %s", path); return -1; }
    size_t n = fread(filebuf, 1, sizeof filebuf - 1, f);
    int too_big = !feof(f);
    fclose(f);
    if (too_big) { snprintf(init_error, sizeof init_error, "%s is too large", path); return -1; }
    filebuf[n] = 0;
    char *r = filebuf, *w = filebuf;             /* strip ;; line comments */
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
    int err = 0;
    cell prog = r0_s1_parse(filebuf, &err);
    if (err) {
        snprintf(init_error, sizeof init_error, "%s does not parse (%d)", path, r0_s1_parse_error_kind());
        return -1;
    }
    r0_s1_run_persistent(prog);
    if (!r0_s1_ran_cleanly()) {
        snprintf(init_error, sizeof init_error, "%s did not load cleanly", path);
        return -1;
    }
    return 0;
}

/* a fresh runtime with the libraries loaded; seeds the task cells exactly as
 * standalone/glon.c's glon_init does */
static int init_session(void) {
    init_error[0] = 0;
    cell main_entry = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = main_entry;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
    for (int i = 0; i < nlibs; i++)
        if (load_lib(libs[i]) != 0) return -1;
    return 0;
}

static void reply_session_state(const char *op) {
    js("{\"op\":"); jstr(op);
    if (init_error[0]) { js(",\"status\":\"ERROR\",\"message\":"); jstr(init_error); }
    else js(",\"status\":\"OK\"");
    js(",\"libs\":[");
    for (int i = 0; i < nlibs; i++) { if (i) js(","); jstr(libs[i]); }
    js("]");
    jsession();
    js("}");
    send_result();
}

static void execute(const char *src, unsigned int len) {
    out_bytes = err_bytes = 0;
    r0_s1_outcome oc;
    r0_s1_session_run(src, len, &oc);
    fflush(stdout);
    fflush(stderr);
    js("{\"op\":\"execute\",\"status\":"); jstr(r0_s1_outcome_status_name(oc.status));
    js(",\"detail\":"); jstr(r0_s1_outcome_detail_name(oc.detail));
    js(",\"values\":[");
    if (oc.status == R0S1_OUT_OK)
        for (int i = 0; i < oc.count; i++) { if (i) js(","); jvalue(r0_s1_result(i, oc.count)); }
    js("],\"sin\":");
    if (oc.status == R0S1_OUT_UNCAUGHT_SIN) {
        js("{\"type\":"); jvalue(oc.sin_type);
        js(",\"id\":"); jvalue(oc.sin_id);
        js(",\"arg\":"); jvalue(oc.sin_arg);
        js("}");
    } else {
        js("null");
    }
    js(",\"stdout_bytes\":"); jnum((long)out_bytes);
    js(",\"stderr_bytes\":"); jnum((long)err_bytes);
    jsession();
    js("}");
    send_result();
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--result-fd") && i + 1 < argc) result_fd = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--lib") && i + 1 < argc && nlibs < MAX_LIBS) libs[nlibs++] = argv[++i];
        else {
            fprintf(stderr, "usage: glon-kernel-host --result-fd N [--lib FILE]...\n");
            return 2;
        }
    }
    if (result_fd < 0) {
        fprintf(stderr, "glon-kernel-host: --result-fd is required\n");
        return 2;
    }
    install_counting_streams();

    int ok = init_session() == 0;
    reply_session_state("ready");
    if (!ok) return 1;

    static char req[MAX_REQUEST];
    for (;;) {
        unsigned char h[4];
        int r = read_exact(0, h, 4);
        if (r <= 0) return 0;                        /* EOF: the parent is done */
        uint32_t n = ((uint32_t)h[0] << 24) | ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | h[3];
        if (n == 0 || n > MAX_REQUEST) {
            js("{\"op\":\"error\",\"status\":\"PROTOCOL_ERROR\",\"message\":\"bad request length\"}");
            send_result();
            return 2;
        }
        if (read_exact(0, req, n) <= 0) return 0;
        switch (req[0]) {
        case 'E':
            execute(req + 1, n - 1);
            break;
        case 'R': {
            int ok2 = init_session() == 0;
            reply_session_state("restart");
            if (!ok2) return 1;
            break;
        }
        case 'Q':
            js("{\"op\":\"quit\",\"status\":\"OK\"}");
            send_result();
            return 0;
        default:
            js("{\"op\":\"error\",\"status\":\"PROTOCOL_ERROR\",\"message\":\"unknown request\"}");
            send_result();
            return 2;
        }
    }
}
