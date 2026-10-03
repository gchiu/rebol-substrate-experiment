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
#include "glon_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SOURCE  (1u << 20)
#define MAX_REQUEST 65536
#define MAX_BODY    (1u << 20)

/* ---- app model state (host machinery; no application policy) ------------- */

static int g_app_mode;
static glon_app_t g_app;
static char g_app_dir[1024];
static char g_data_dir[1024];
static glon_grants_t g_grant_store;
static char g_granted[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX];
static int  g_ngranted;
static char g_eff[GLON_APP_LIST_MAX][GLON_APP_NAME_MAX];
static int  g_neff;
static char g_last_dest[2048];
static int  g_last_dest_set;

/* The host's ACTUAL implementations, registered here, independent of the
 * capability catalogue.  The catalogue supplies canonical SEMANTICS; this list
 * supplies IMPLEMENTATION.  Effective authority requires BOTH:
 *
 *     host implementation  INTERSECT  catalogue-known semantics
 *       INTERSECT  manifest request  INTERSECT  exact per-app grant
 *
 * A catalogue entry alone does not make a capability implemented; a host
 * implementation without trusted catalogue metadata cannot be explained and
 * therefore fails closed. */
static const char *const HOST_IMPL[] = {
    "net/connect", "file/app-write", "open/folder", "process/spawn", "view/open", NULL
};

static glon_catalogue_t g_catalogue;
static const char *g_impl[GLON_CAT_MAX + 1];   /* implemented AND catalogue-known */

static int host_implements(const char *cap) {
    for (int i = 0; HOST_IMPL[i]; i++) if (strcmp(HOST_IMPL[i], cap) == 0) return 1;
    return 0;
}

static const glon_capability_t *catalogue_known(const char *cap) {
    return glon_catalogue_find(&g_catalogue, cap);
}

static void build_impl(void) {
    int n = 0;
    for (int i = 0; HOST_IMPL[i] && n < GLON_CAT_MAX; i++)
        if (catalogue_known(HOST_IMPL[i]))
            g_impl[n++] = HOST_IMPL[i];
    g_impl[n] = NULL;
}

static int effective_has(const char *cap) {
    for (int i = 0; i < g_neff; i++) if (!strcmp(g_eff[i], cap)) return 1;
    return 0;
}

static int granted_has(const char *cap) {
    for (int i = 0; i < g_ngranted; i++) if (!strcmp(g_granted[i], cap)) return 1;
    return 0;
}

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

/* Map a URL to a served file.  In D6 core mode this is the desktop/ root.  In
 * app mode it mounts the app's View plus the host core and the requested
 * modules.  This is host transport configuration, not application policy. */
static int resolve_file_path(const char *target, char *out, int cap) {
    if (strstr(target, "..")) return -1;
    if (!g_app_mode) {
        char rel[2048];
        if (!strcmp(target, "/") || target[0] == 0) strcpy(rel, "index.html");
        else snprintf(rel, sizeof rel, "%s", target + 1);
        snprintf(out, cap, "desktop/%s", rel);
        return 0;
    }
    if (!strcmp(target, "/") || !strcmp(target, "/index.html")) snprintf(out, cap, "desktop/index.html");
    else if (!strcmp(target, "/view.glon")) snprintf(out, cap, "%s/%s", g_app_dir, g_app.view);
    else if (!strcmp(target, "/e2e.html")) snprintf(out, cap, "%s/e2e.html", g_app_dir);
    else if (!strcmp(target, "/strings.glon")) snprintf(out, cap, "modules/strings.glon");
    else if (!strcmp(target, "/fetch.glon")) snprintf(out, cap, "modules/fetch.glon");
    else if (!strcmp(target, "/glon.wasm")) snprintf(out, cap, "desktop/glon.wasm");
    else if (!strcmp(target, "/browser-host.js")) snprintf(out, cap, "desktop/browser-host.js");
    else if (!strcmp(target, "/emit.glon")) snprintf(out, cap, "desktop/emit.glon");
    else if (!strcmp(target, "/prelude.glon")) snprintf(out, cap, "desktop/prelude.glon");
    else if (target[0] == '/') snprintf(out, cap, "desktop/%s", target + 1);
    else return -1;
    return 0;
}

static void handle_static(glon_socket fd, const char *target) {
    char path[4096];
    if (resolve_file_path(target, path, (int)sizeof path) != 0) {
        send_http(fd, 404, "text/plain", "not found", 9);
        return;
    }
    static char body[MAX_BODY];
    int blen = 0;
    if (fs_read(path, body, (int)sizeof body, &blen) != 0) {
        send_http(fd, 404, "text/plain", "not found", 9);
        return;
    }
    send_http(fd, 200, content_type(path), body, blen);
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

/* ---- app endpoints ------------------------------------------------------- */

static int dispatch_value(const char *token, const char *value) {
    static char out[8192];
    int out_len = 0;
    capture_reset();
    return r0_s1_g1a_live_event_bytes(token, (const unsigned char *)value,
                                      (unsigned int)strlen(value),
                                      out, (int)sizeof out, &out_len);
}

/* Forward one downloader output record through native Glon to the browser.
 * Returns nonzero when the browser has gone away (cancellation): the write to
 * the client socket fails, which signals the spawn layer to kill the child. */
static int spawn_progress_cb(const char *record, int len, void *user) {
    glon_socket fd = (glon_socket)(intptr_t)user;
    static char out[8192];
    int out_len = 0;
    capture_reset();
    if (r0_s1_g1a_live_event_bytes("fetch-output", (const unsigned char *)record,
                                   (unsigned int)len, out, (int)sizeof out, &out_len) == 0 &&
        hc_have && strcmp(hc_op, "progress") == 0) {
        if (glon_tcp_write_all(fd, hc_arg, hc_arg_len) != 0) return 1;
        if (glon_tcp_write_all(fd, "\n", 1) != 0) return 1;
    }
    return 0;
}

static void handle_api_permissions(glon_socket fd) {
    char buf[8192];
    int n = 0;
    n += snprintf(buf + n, sizeof buf - (size_t)n, "app-id: %s\n", g_app.app_id);
    n += snprintf(buf + n, sizeof buf - (size_t)n, "name: %s\n", g_app.pkg_name);
    n += snprintf(buf + n, sizeof buf - (size_t)n, "version: %s\n", g_app.version);
    n += snprintf(buf + n, sizeof buf - (size_t)n, "modules:");
    for (int i = 0; i < g_app.nmodules; i++) n += snprintf(buf + n, sizeof buf - (size_t)n, " %s", g_app.modules[i]);
    n += snprintf(buf + n, sizeof buf - (size_t)n, "\nrequested:");
    for (int i = 0; i < g_app.npermissions; i++) n += snprintf(buf + n, sizeof buf - (size_t)n, " %s", g_app.permissions[i]);
    n += snprintf(buf + n, sizeof buf - (size_t)n, "\ngranted:");
    for (int i = 0; i < g_ngranted; i++) n += snprintf(buf + n, sizeof buf - (size_t)n, " %s", g_granted[i]);
    n += snprintf(buf + n, sizeof buf - (size_t)n, "\neffective:");
    for (int i = 0; i < g_neff; i++) n += snprintf(buf + n, sizeof buf - (size_t)n, " %s", g_eff[i]);
    n += snprintf(buf + n, sizeof buf - (size_t)n, "\n");

    /* Per-permission canonical semantics (trusted) + purpose (app claim) +
     * local policy (granted/effective). */
    for (int i = 0; i < g_app.npermissions; i++) {
        const char *perm = g_app.permissions[i];
        const glon_capability_t *c = glon_catalogue_find(&g_catalogue, perm);
        const char *purpose = glon_app_purpose(&g_app, perm);
        n += snprintf(buf + n, sizeof buf - (size_t)n, "permission:\n");
        n += snprintf(buf + n, sizeof buf - (size_t)n, "  id: %s\n", perm);
        if (c) {
            n += snprintf(buf + n, sizeof buf - (size_t)n, "  label: %s\n", c->label);
            n += snprintf(buf + n, sizeof buf - (size_t)n, "  class: %s\n", c->klass);
            n += snprintf(buf + n, sizeof buf - (size_t)n, "  risk: %s\n", c->risk);
            n += snprintf(buf + n, sizeof buf - (size_t)n, "  allows: %s\n", c->allows);
            n += snprintf(buf + n, sizeof buf - (size_t)n, "  known: yes\n");
        } else {
            n += snprintf(buf + n, sizeof buf - (size_t)n, "  label: (unknown capability)\n");
            n += snprintf(buf + n, sizeof buf - (size_t)n, "  class: unknown\n");
            n += snprintf(buf + n, sizeof buf - (size_t)n, "  risk: unknown\n");
            n += snprintf(buf + n, sizeof buf - (size_t)n, "  allows: \n");
            n += snprintf(buf + n, sizeof buf - (size_t)n, "  known: no\n");
        }
        n += snprintf(buf + n, sizeof buf - (size_t)n, "  implemented: %s\n", host_implements(perm) ? "yes" : "no");
        n += snprintf(buf + n, sizeof buf - (size_t)n, "  purpose: %s\n", purpose ? purpose : "");
        n += snprintf(buf + n, sizeof buf - (size_t)n, "  granted: %s\n", granted_has(perm) ? "yes" : "no");
        n += snprintf(buf + n, sizeof buf - (size_t)n, "  effective: %s\n", effective_has(perm) ? "yes" : "no");
    }
    send_http(fd, 200, "text/plain; charset=utf-8", buf, n);
}

/* The download endpoint.  Body = "url\nname".  Authority is checked against
 * the effective permission set (net/connect + file/app-write; NOT
 * process/spawn).  The application decides the logical fetch; the host's
 * trusted service implements it (internally with curl) and streams progress
 * back through Glon.  The app never names an executable. */
static void handle_api_fetch(glon_socket fd, const char *body) {
    if (!g_app_mode) { send_http(fd, 404, "text/plain", "no app", 6); return; }

    char url[2048] = {0}, name[512] = {0};
    const char *nl = strchr(body, '\n');
    if (nl) {
        int ulen = (int)(nl - body);
        if (ulen >= (int)sizeof url) ulen = (int)sizeof url - 1;
        memcpy(url, body, (size_t)ulen);
        url[ulen] = 0;
        snprintf(name, sizeof name, "%s", nl + 1);
    } else {
        snprintf(url, sizeof url, "%s", body);
    }

    if (!effective_has("net/connect") || !effective_has("file/app-write")) {
        fprintf(stderr, "glon-desktop: fetch denied: effective permissions lack net/connect or file/app-write\n");
        send_http(fd, 403, "text/plain", "forbidden: net/connect or file/app-write not effective", 55);
        return;
    }

    char dest[2048];
    if (glon_app_resolve_write(g_data_dir, g_app.app_id, name, dest, (int)sizeof dest) != 0) {
        fprintf(stderr, "glon-desktop: rejected destination name '%s'\n", name);
        send_http(fd, 400, "text/plain", "bad destination", 15);
        return;
    }
    fprintf(stderr, "glon-desktop: fetch url='%s' dest='%s'\n", url, dest);

    dispatch_value("fetch-dest", dest);
    dispatch_value("fetch-url", url);
    dispatch_value("fetch-go", "");

    /* The application must authorise the logical fetch.  It names no exe. */
    if (!hc_have || strcmp(hc_op, "fetch-run") != 0 ||
        hc_arg_len == 0 || (size_t)strlen(hc_arg) + 1 >= (size_t)hc_arg_len) {
        send_http(fd, 400, "text/plain", "application did not authorise fetch", 34);
        return;
    }

    /* Trusted host service implementation detail: curl, with a fixed vector.
     * The destination is the host-resolved path (never the app's echo). */
    char *argv[9];
    argv[0] = (char *)"curl";
    argv[1] = (char *)"--location";
    argv[2] = (char *)"--fail";
    argv[3] = (char *)"--proto";
    argv[4] = (char *)"=http,https";
    argv[5] = (char *)"--output";
    argv[6] = dest;
    argv[7] = url;
    argv[8] = NULL;

    const char *hdr = "HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/plain; charset=utf-8\r\n"
                      "Connection: close\r\n"
                      "Cache-Control: no-store\r\n\r\n";
    glon_tcp_write_all(fd, hdr, (int)strlen(hdr));

    int exit_code = 0;
    int rc = glon_spawn_stream("curl", argv, spawn_progress_cb, (void *)(intptr_t)fd, &exit_code);

    if (rc == 1) {
        /* Client cancelled. Terminate the partial download and leave no partial
         * file behind: the app-write area contains complete files only. */
        remove(dest);
        fprintf(stderr, "glon-desktop: download cancelled; removed partial '%s'\n", dest);
        return;
    }
    if (rc != 0 || exit_code != 0) {
        remove(dest);
        fprintf(stderr, "glon-desktop: download failed (exit %d); removed partial '%s'\n", exit_code, dest);
        glon_tcp_write_all(fd, "error download failed\n", 21);
        return;
    }

    /* Report completion first (the download is on disk); the SHA-256 follows
     * once computed, so the View reaches "done" promptly. */
    glon_tcp_write_all(fd, "done\n", 5);

    char hex[128];
    if (glon_sha256(dest, hex, (int)sizeof hex) != 0)
        snprintf(hex, sizeof hex, "unavailable");
    char line[256];
    int ln = snprintf(line, sizeof line, "sha256 %s\n", hex);
    glon_tcp_write_all(fd, line, ln);

    capture_reset();
    dispatch_value("fetch-done", "done");

    snprintf(g_last_dest, sizeof g_last_dest, "%s", dest);
    g_last_dest_set = 1;
    fprintf(stderr, "glon-desktop: fetch complete exit=%d sha256=%s\n", exit_code, hex);
}

static void handle_api_open(glon_socket fd) {
    if (!g_app_mode) { send_http(fd, 404, "text/plain", "no app", 6); return; }
    /* Authority is checked before state: an unauthorised caller always 403s. */
    if (!effective_has("open/folder")) { send_http(fd, 403, "text/plain", "forbidden", 9); return; }
    if (!g_last_dest_set) {
        send_http(fd, 400, "text/plain", "nothing downloaded", 18);
        return;
    }
    char dir[2048];
    snprintf(dir, sizeof dir, "%s", g_last_dest);
    char *slash = strrchr(dir, '/');
    if (!slash) { send_http(fd, 400, "text/plain", "bad path", 8); return; }
    *slash = 0;
    if (glon_open_path(dir) != 0) {
        fprintf(stderr, "glon-desktop: failed to launch explorer for folder '%s'\n", dir);
        send_http(fd, 500, "text/plain", "could not open folder", 21);
        return;
    }
    /* The process was created; visibility is not asserted here. */
    send_http(fd, 200, "text/plain", "requested", 9);
}

static void handle_connection(glon_socket fd) {
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

    /* read the request body if Content-Length is present */
    const char *body = "";
    const char *hdr_end = strstr(req, "\r\n\r\n");
    int hdr_len = hdr_end ? (int)(hdr_end - req) + 4 : n;
    if (!hdr_end) { const char *he2 = strstr(req, "\n\n"); hdr_len = he2 ? (int)(he2 - req) + 2 : n; }
    const char *cl = strstr(req, "Content-Length:");
    if (!cl) cl = strstr(req, "content-length:");
    if (cl) {
        int clen = atoi(cl + 15);
        if (clen < 0) clen = 0;
        if (clen > (int)sizeof req - hdr_len - 1) clen = (int)sizeof req - hdr_len - 1;
        while (n - hdr_len < clen) {
            int r = glon_tcp_read(fd, req + n, (int)sizeof req - 1 - n);
            if (r <= 0) break;
            n += r;
            req[n] = 0;
        }
        body = req + hdr_len;
    }

    char method[16] = {0}, target[4096] = {0};
    if (sscanf(req, "%15s %4095s", method, target) != 2) {
        send_http(fd, 400, "text/plain", "bad request", 11);
        return;
    }

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

    if (!strcmp(target, "/api/permissions")) handle_api_permissions(fd);
    else if (!strcmp(target, "/api/fetch")) handle_api_fetch(fd, body);
    else if (!strcmp(target, "/api/open")) handle_api_open(fd);
    else if (is_native) handle_native_read(fd, key);
    else if (is_resource) handle_resource(fd, key);
    else handle_static(fd, target);
}

/* ---- main ---------------------------------------------------------------- */

int main(int argc, char **argv) {
    const char *core_app = "desktop/app.glon";
    const char *install_id = NULL;
    const char *install_conf = "desktop/install.conf";
    const char *cap_conf = "desktop/capabilities.conf";
    const char *manifest = NULL;
    const char *grants = "desktop/grants.conf";
    int port = 0;
    int no_browser = 0;

    glon_host_init();

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-browser")) no_browser = 1;
        else if (!strcmp(argv[i], "--install") && i + 1 < argc) { install_id = argv[++i]; g_app_mode = 1; }
        else if (!strcmp(argv[i], "--install-conf") && i + 1 < argc) install_conf = argv[++i];
        else if (!strcmp(argv[i], "--capabilities") && i + 1 < argc) cap_conf = argv[++i];
        else if (!strcmp(argv[i], "--manifest") && i + 1 < argc) manifest = argv[++i];
        else if (!strcmp(argv[i], "--grants") && i + 1 < argc) grants = argv[++i];
        else if (!strcmp(argv[i], "--data") && i + 1 < argc) snprintf(g_data_dir, sizeof g_data_dir, "%s", argv[++i]);
        else core_app = argv[i];
    }
    glon_ignore_sigpipe();

    cell main_entry = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = main_entry;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    if (g_app_mode) {
        /* 1. Trusted installation state binds identity to an installed package.
         *    The command line names an id, never a filesystem path. */
        if (glon_install_resolve(install_conf, install_id, g_app_dir, (int)sizeof g_app_dir) != 0) {
            fprintf(stderr, "glon-desktop: application id '%s' is not installed (registry: %s)\n",
                    install_id, install_conf);
            return 2;
        }
        /* 2. The manifest DECLARES identity; it must match the installed
         *    identity.  A manifest may never authenticate itself. */
        char mpath[1400];
        if (!manifest) { snprintf(mpath, sizeof mpath, "%s/glon-app.manifest", g_app_dir); manifest = mpath; }
        int mrc = glon_app_manifest_load(manifest, &g_app);
        if (mrc == -2) {
            fprintf(stderr, "glon-desktop: manifest '%s' has a missing or malformed application id\n", manifest);
            return 2;
        }
        if (mrc != 0) {
            fprintf(stderr, "glon-desktop: cannot read manifest '%s'\n", manifest);
            return 2;
        }
        if (strcmp(g_app.app_id, install_id) != 0) {
            fprintf(stderr, "glon-desktop: manifest identity '%s' does not match installed identity '%s'"
                            " (a manifest may not authenticate itself)\n", g_app.app_id, install_id);
            return 2;
        }
        /* 3. Trusted capability catalogue: canonical semantics and the set of
         *    capabilities the host implements.  Unknown ids fail closed. */
        if (glon_catalogue_load(cap_conf, &g_catalogue) != 0) {
            fprintf(stderr, "glon-desktop: cannot read capability catalogue '%s'\n", cap_conf);
            return 2;
        }
        build_impl();
        for (int i = 0; i < g_app.npermissions; i++)
            if (!catalogue_known(g_app.permissions[i]))
                fprintf(stderr, "glon-desktop: unknown capability: %s\n", g_app.permissions[i]);
        for (int i = 0; i < g_catalogue.ncaps; i++)
            if (!host_implements(g_catalogue.caps[i].id))
                fprintf(stderr, "glon-desktop: catalogue capability not implemented by host: %s\n",
                        g_catalogue.caps[i].id);
        for (int i = 0; HOST_IMPL[i]; i++)
            if (!catalogue_known(HOST_IMPL[i]))
                fprintf(stderr, "glon-desktop: host implementation without trusted catalogue metadata: %s (fail closed)\n",
                        HOST_IMPL[i]);

        /* 4. Authority is granted to the installed application id, exactly. */
        if (glon_app_load_grants(grants, &g_grant_store) != 0) {
            fprintf(stderr, "glon-desktop: cannot read grant store '%s'\n", grants);
            return 2;
        }
        g_ngranted = glon_app_grants_for(&g_grant_store, install_id, g_granted);
        if (g_ngranted > GLON_APP_LIST_MAX) g_ngranted = GLON_APP_LIST_MAX;
        g_neff = glon_app_effective(&g_app, g_granted, g_ngranted, g_impl, g_eff);
        if (g_neff > GLON_APP_LIST_MAX) g_neff = GLON_APP_LIST_MAX;
        if (!*g_data_dir) snprintf(g_data_dir, sizeof g_data_dir, "desktop/appdata");

        printf("glon-desktop: app-id %s\n", install_id);
        printf("glon-desktop: app '%s' %s\n", g_app.pkg_name, g_app.version);
        printf("glon-desktop: modules:");
        for (int i = 0; i < g_app.nmodules; i++) printf(" %s", g_app.modules[i]);
        printf("\n");
        printf("glon-desktop: requested permissions:");
        for (int i = 0; i < g_app.npermissions; i++) printf(" %s", g_app.permissions[i]);
        printf("\n");
        printf("glon-desktop: granted permissions:");
        for (int i = 0; i < g_ngranted; i++) printf(" %s", g_granted[i]);
        printf("\n");
        printf("glon-desktop: EFFECTIVE permissions:");
        for (int i = 0; i < g_neff; i++) printf(" %s", g_eff[i]);
        printf("\n");
        fflush(stdout);

        /* core host libraries, then exactly the requested modules */
        if (load_file("glon-lib/prelude.glon") != 0) return 2;
        if (load_file("desktop/emit.glon") != 0) return 2;
        for (int i = 0; i < g_app.nmodules; i++) {
            char mfile[512];
            snprintf(mfile, sizeof mfile, "modules/%s.glon", g_app.modules[i]);
            FILE *probe = fopen(mfile, "rb");
            if (!probe) {
                fprintf(stderr, "glon-desktop: required module '%s' not found in the local module store (%s)\n",
                        g_app.modules[i], mfile);
                return 2;
            }
            fclose(probe);
            if (load_file(mfile) != 0) return 2;
        }
        char entry[1400];
        snprintf(entry, sizeof entry, "%s/%s", g_app_dir, g_app.entry);
        if (load_file(entry) != 0) return 2;

        glon_mkdirs(g_data_dir);
        char dl[1600];
        snprintf(dl, sizeof dl, "%s/%s/downloads", g_data_dir, install_id);
        glon_mkdirs(dl);
    } else {
        const char *libs[8];
        int nlibs = 0;
        libs[nlibs++] = "glon-lib/prelude.glon";
        libs[nlibs++] = "glon-lib/strings.glon";
        libs[nlibs++] = "desktop/emit.glon";
        for (int i = 0; i < nlibs; i++)
            if (load_file(libs[i]) != 0) return 2;
        if (load_file(core_app) != 0) return 2;
    }

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
        handle_connection(conn);
        glon_tcp_close(conn);
    }
    glon_tcp_close(srv);
    glon_host_shutdown();
    return 0;
}
