/* desktop/glon_desktop.c -- native Glon desktop host (OS-independent part).
 *
 * The installed application/runtime side of the desktop proof.  It supplies
 * ONLY primitive host capabilities -- a TCP byte stream, a filesystem read,
 * and a process/open -- through the small OS boundary in glon_host.h, and
 * drives the loaded Glon application through the existing browser-agnostic
 * request/reply boundary (r0_s1_g1a_live.c).  It contains no application or
 * protocol semantics: the URL endpoint mapping, the logical name -> path
 * decision, and the HTTP body are decided in Glon (desktop/app.glon).
 *
 * This file has NO OS-specific code; see glon_host_posix.c and
 * glon_host_windows.c.  It adds no S1 primitive, no native id, and no
 * evaluator/parser change.
 *
 * Usage:
 *   glon-desktop [--port N] [--root DIR] [--no-browser] [--lib FILE]... [APP.glon]
 */

#include "r0_s1.h"
#include "r0_s1_g1a_live.h"
#include "m1_layout.h"
#include "glon_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SOURCE  (1u << 20)
#define MAX_REQUEST 65536
#define MAX_BODY    (1u << 20)

/* ---- host-call capture --------------------------------------------------- */

static char hc_op[256];
static int  hc_op_len;
static char hc_arg[16384];
static int  hc_arg_len;
static int  hc_have;

static void capture_host_call(const char *op, int op_len,
                              const char *arg, int arg_len, void *user) {
    (void)user;
    if (op_len < 0) op_len = 0;
    if (op_len > (int)sizeof hc_op - 1) op_len = (int)sizeof hc_op - 1;
    if (arg_len < 0) arg_len = 0;
    if (arg_len > (int)sizeof hc_arg - 1) arg_len = (int)sizeof hc_arg - 1;
    memcpy(hc_op, op, (size_t)op_len);  hc_op[op_len] = 0;
    memcpy(hc_arg, arg, (size_t)arg_len); hc_arg[arg_len] = 0;
    hc_op_len = op_len;
    hc_arg_len = arg_len;
    hc_have = 1;
}

static void capture_reset(void) { hc_have = 0; hc_op_len = hc_arg_len = 0; }

/* ---- primitive: filesystem read (portable C stdio) ----------------------- */

static int fs_read(const char *path, char *out, int cap, int *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(out, 1, (size_t)(cap - 1), f);
    int too_big = !feof(f);
    fclose(f);
    if (too_big) return -1;
    out[n] = 0;
    if (out_len) *out_len = (int)n;
    return 0;
}

/* ---- source loading ------------------------------------------------------ */

static char *read_source(const char *path, char *err, size_t errcap) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errcap, "cannot open"); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); snprintf(err, errcap, "cannot seek"); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); snprintf(err, errcap, "cannot measure"); return NULL; }
    if ((unsigned long)n > MAX_SOURCE) { fclose(f); snprintf(err, errcap, "too large"); return NULL; }
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); snprintf(err, errcap, "out of memory"); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(buf); snprintf(err, errcap, "cannot read"); return NULL; }
    buf[got] = 0;
    return buf;
}

static void strip_comments(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
}

static int load_file(const char *path) {
    char err[512];
    char *src = read_source(path, err, sizeof err);
    if (!src) { fprintf(stderr, "glon-desktop: %s: %s\n", path, err); return -1; }
    strip_comments(src);
    int perr = 0;
    cell prog = r0_s1_parse(src, &perr);
    free(src);
    if (perr) { fprintf(stderr, "glon-desktop: %s: parse error\n", path); return -1; }
    r0_s1_run_persistent(prog);
    if (!r0_s1_ran_cleanly()) {
        fprintf(stderr, "glon-desktop: %s: did not run cleanly\n", path);
        return -1;
    }
    return 0;
}

/* ---- HTTP transport (generic; bytes only) ------------------------------- */

static const char *content_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (!strcmp(dot, ".html")) return "text/html; charset=utf-8";
    if (!strcmp(dot, ".js"))   return "text/javascript; charset=utf-8";
    if (!strcmp(dot, ".wasm")) return "application/wasm";
    if (!strcmp(dot, ".css"))  return "text/css; charset=utf-8";
    if (!strcmp(dot, ".glon")) return "text/plain; charset=utf-8";
    if (!strcmp(dot, ".txt"))  return "text/plain; charset=utf-8";
    if (!strcmp(dot, ".json")) return "application/json";
    if (!strcmp(dot, ".png"))  return "image/png";
    if (!strcmp(dot, ".jpg") || !strcmp(dot, ".jpeg")) return "image/jpeg";
    if (!strcmp(dot, ".gif"))  return "image/gif";
    if (!strcmp(dot, ".svg"))  return "image/svg+xml";
    if (!strcmp(dot, ".mp3"))  return "audio/mpeg";
    if (!strcmp(dot, ".wav"))  return "audio/wav";
    if (!strcmp(dot, ".ogg"))  return "audio/ogg";
    if (!strcmp(dot, ".mp4"))  return "video/mp4";
    if (!strcmp(dot, ".webm")) return "video/webm";
    if (!strcmp(dot, ".pdf"))  return "application/pdf";
    return "application/octet-stream";
}

static void send_http(glon_socket fd, int status, const char *type,
                      const char *body, int blen) {
    char head[512];
    const char *reason = (status == 200) ? "OK" : (status == 404 ? "Not Found" : "Error");
    int n = snprintf(head, sizeof head,
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n"
                     "Cache-Control: no-store\r\n"
                     "\r\n",
                     status, reason, type, blen);
    if (glon_tcp_write_all(fd, head, n) != 0) return;
    if (blen > 0) glon_tcp_write_all(fd, body, blen);
}

static int url_decode(const char *in, char *out, int cap) {
    int n = 0;
    for (const char *p = in; *p && n < cap - 1; p++) {
        if (*p == '%' && p[1] && p[2]) {
            int hi = (p[1] >= '0' && p[1] <= '9') ? p[1] - '0'
                   : (p[1] >= 'a' && p[1] <= 'f') ? p[1] - 'a' + 10
                   : (p[1] >= 'A' && p[1] <= 'F') ? p[1] - 'A' + 10 : -1;
            int lo = (p[2] >= '0' && p[2] <= '9') ? p[2] - '0'
                   : (p[2] >= 'a' && p[2] <= 'f') ? p[2] - 'a' + 10
                   : (p[2] >= 'A' && p[2] <= 'F') ? p[2] - 'A' + 10 : -1;
            if (hi >= 0 && lo >= 0) { out[n++] = (char)(hi * 16 + lo); p += 2; continue; }
        }
        out[n++] = (*p == '+') ? ' ' : *p;
    }
    out[n] = 0;
    return n;
}

/* Read one query parameter (the part after "key=" up to '&') and URL-decode
 * it into out.  Returns 1 when present. */
static int query_get(const char *q, const char *key, char *out, int cap) {
    out[0] = 0;
    if (!q) return 0;
    char pat[64];
    snprintf(pat, sizeof pat, "%s=", key);
    const char *p = strstr(q, pat);
    if (!p) return 0;
    p += strlen(pat);
    char raw[2048];
    int k = 0;
    while (p[k] && p[k] != '&' && k < (int)sizeof raw - 1) { raw[k] = p[k]; k++; }
    raw[k] = 0;
    url_decode(raw, out, cap);
    return 1;
}

static void handle_static(glon_socket fd, const char *root, const char *target) {
    char rel[2048];
    if (!strcmp(target, "/") || target[0] == 0) strcpy(rel, "index.html");
    else snprintf(rel, sizeof rel, "%s", target + 1);
    if (strstr(rel, "..")) { send_http(fd, 404, "text/plain", "not found", 9); return; }
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    static char body[MAX_BODY];
    int blen = 0;
    if (fs_read(path, body, (int)sizeof body, &blen) != 0) {
        send_http(fd, 404, "text/plain", "not found", 9);
        return;
    }
    send_http(fd, 200, content_type(rel), body, blen);
}

/* ---- DATA PLANE: stream a file straight to the socket -------------------- */
/* The bytes go from the filesystem to the network here; they are never placed
 * in a Glon value, never in the event boundary, and never in JavaScript. */
static void stream_file(glon_socket fd, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { send_http(fd, 404, "text/plain", "not found", 9); return; }

    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0) { size = ftell(f); rewind(f); }
    if (size < 0) { fclose(f); send_http(fd, 500, "text/plain", "unsized", 7); return; }

    char head[512];
    int hn = snprintf(head, sizeof head,
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %ld\r\n"
                      "Connection: close\r\n"
                      "Cache-Control: no-store\r\n"
                      "\r\n",
                      content_type(path), size);
    if (glon_tcp_write_all(fd, head, hn) != 0) { fclose(f); return; }

    static char chunk[65536];
    long sent = 0;
    size_t r;
    while ((r = fread(chunk, 1, sizeof chunk, f)) > 0) {
        if (glon_tcp_write_all(fd, chunk, (int)r) != 0) break;
        sent += (long)r;
    }
    fclose(f);
    fprintf(stderr, "glon-desktop: data plane streamed '%s' (%ld bytes)\n", path, sent);
}

/* The resource endpoint.  Glon (control plane) maps the logical name to a
 * path; the host then streams that file (data plane).  The logical name is
 * never used as a path by the host. */
static void handle_resource(glon_socket fd, const char *name) {
    static char out[8192];
    int out_len = 0;

    capture_reset();
    if (r0_s1_g1a_live_event_bytes("resource-path", (const unsigned char *)name,
                                   (unsigned int)strlen(name),
                                   out, (int)sizeof out, &out_len) != 0) {
        send_http(fd, 500, "text/plain", "resource dispatch failed", 24);
        return;
    }
    if (!hc_have || strcmp(hc_op, "serve-file") != 0 || hc_arg_len == 0) {
        send_http(fd, 404, "text/plain", "unknown resource", 16);
        return;
    }
    fprintf(stderr, "glon-desktop: control plane mapped resource '%s' -> '%s'\n",
            name, hc_arg);
    stream_file(fd, hc_arg);
}

/* The small control-plane endpoint.  The requested key is handed to the loaded
 * Glon application, which decides what to read; the host performs the primitive
 * read and feeds the bytes back, and Glon decides the response body.  The
 * host never maps a logical key to a path. */
static void handle_native_read(glon_socket fd, const char *key) {
    static char out[MAX_BODY];
    int out_len = 0;

    capture_reset();
    if (r0_s1_g1a_live_event_bytes("native-read", (const unsigned char *)key,
                                   (unsigned int)strlen(key),
                                   out, (int)sizeof out, &out_len) != 0) {
        send_http(fd, 500, "text/plain", "native dispatch failed", 22);
        return;
    }
    if (!hc_have || strcmp(hc_op, "fs-read") != 0) {
        send_http(fd, 500, "text/plain", "application produced no fs-read", 30);
        return;
    }
    /* Authority is Glon's: the host never turns the browser string into a
     * path. If Glon did not authorise the name it supplies no path, and the
     * host refuses without touching the filesystem. */
    if (hc_arg_len == 0) {
        fprintf(stderr, "glon-desktop: rejected unauthorised control key '%s'\n", key);
        send_http(fd, 404, "text/plain", "unknown key", 11);
        return;
    }
    char path[sizeof hc_arg];
    memcpy(path, hc_arg, sizeof path);
    path[sizeof path - 1] = 0;
    fprintf(stderr, "glon-desktop: native Glon requested fs-read '%s'\n", path);

    static char body[MAX_BODY];
    int blen = 0;
    if (fs_read(path, body, (int)sizeof body, &blen) != 0) {
        snprintf(body, sizeof body, "** could not read %s", path);
        blen = (int)strlen(body);
    }
    fprintf(stderr, "glon-desktop: filesystem returned %d bytes\n", blen);

    capture_reset();
    if (r0_s1_g1a_live_event_bytes("fs-read-reply", (const unsigned char *)body,
                                   (unsigned int)blen,
                                   out, (int)sizeof out, &out_len) != 0) {
        send_http(fd, 500, "text/plain", "native reply dispatch failed", 28);
        return;
    }
    if (!hc_have || strcmp(hc_op, "http-respond") != 0) {
        send_http(fd, 500, "text/plain", "application produced no response", 31);
        return;
    }
    fprintf(stderr, "glon-desktop: native Glon produced %d response bytes\n", hc_arg_len);
    send_http(fd, 200, "text/plain; charset=utf-8", hc_arg, hc_arg_len);
}

static void handle_connection(glon_socket fd, const char *root) {
    static char req[MAX_REQUEST];
    int n = 0;
    while (n < (int)sizeof req - 1) {
        int r = glon_tcp_read(fd, req + n, (int)sizeof req - 1 - n);
        if (r <= 0) break;
        n += r;
        req[n] = 0;
        if (strstr(req, "\r\n\r\n") || strstr(req, "\n\n")) break;
    }
    if (n <= 0) return;

    char method[16] = {0}, target[4096] = {0};
    if (sscanf(req, "%15s %4095s", method, target) != 2) {
        send_http(fd, 400, "text/plain", "bad request", 11);
        return;
    }
    (void)method;

    char *q = strchr(target, '?');
    int is_native = 0, is_resource = 0;
    char key[1024] = {0};
    if (q) {
        *q = 0;
        const char *qs = q + 1;
        if (!strcmp(target, "/native/read")) {
            is_native = 1;
            query_get(qs, "path", key, (int)sizeof key);
        } else if (!strcmp(target, "/resource")) {
            is_resource = 1;
            query_get(qs, "name", key, (int)sizeof key);
        }
    }

    if (is_native) handle_native_read(fd, key);
    else if (is_resource) handle_resource(fd, key);
    else handle_static(fd, root, target);
}

/* ---- main ---------------------------------------------------------------- */

int main(int argc, char **argv) {
    const char *app = "desktop/app.glon";
    const char *root = "desktop";
    int port = 0;
    int no_browser = 0;
    const char *libs[8];
    int nlibs = 0;
    libs[nlibs++] = "glon-lib/prelude.glon";
    libs[nlibs++] = "glon-lib/strings.glon";
    libs[nlibs++] = "desktop/emit.glon";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--root") && i + 1 < argc) root = argv[++i];
        else if (!strcmp(argv[i], "--no-browser")) no_browser = 1;
        else if (!strcmp(argv[i], "--lib") && i + 1 < argc && nlibs < 8) libs[nlibs++] = argv[++i];
        else app = argv[i];
    }

    if (glon_host_init() != 0) {
        fprintf(stderr, "glon-desktop: network initialisation failed\n");
        return 2;
    }
    glon_ignore_sigpipe();

    cell main_entry = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = main_entry;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    for (int i = 0; i < nlibs; i++)
        if (load_file(libs[i]) != 0) return 2;
    if (load_file(app) != 0) return 2;

    r0_s1_g1a_live_set_host_call(capture_host_call, 0);

    int bound = port;
    glon_socket srv = glon_tcp_listen("127.0.0.1", port, &bound);
    if (srv == GLON_INVALID_SOCKET) {
        fprintf(stderr, "glon-desktop: cannot listen on 127.0.0.1:%d\n", port);
        return 2;
    }

    char url[64];
    snprintf(url, sizeof url, "http://127.0.0.1:%d/", bound);
    printf("glon-desktop: application running at %s\n", url);
    printf("glon-desktop: native Glon process %ld is the application; the browser is Glon's View\n", glon_process_id());
    fflush(stdout);

    if (!no_browser) glon_open_browser(url);

    for (;;) {
        glon_socket conn = glon_tcp_accept(srv);
        if (conn == GLON_INVALID_SOCKET) break;
        handle_connection(conn, root);
        glon_tcp_close(conn);
    }
    glon_tcp_close(srv);
    glon_host_shutdown();
    return 0;
}
