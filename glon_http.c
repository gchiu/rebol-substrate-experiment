/* glon_http.c - optional native HTTPS listener for the Glon CLI.
 *
 * CivetWeb (MIT, vendored in third_party/civetweb) provides the networking,
 * TLS, HTTP parsing, request-size limits and connection cleanup.  This file
 * only bridges a completed HTTP request into the persistent Glon session and
 * transports the response bytes back.  It contains no application HTML and no
 * routing policy.
 *
 * See glon_http.h for the boundary and docs/design/GLON-HTTPS-SERVER-G1.md for
 * the design and limitations.
 */

#define _GNU_SOURCE
#include "glon_http.h"
#include "r0_s1.h"
#include "m1_layout.h"

#include "civetweb.h"

#ifdef _WIN32
#include "glon_win_compat.h"
#else
#include <pthread.h>
#include <signal.h>
#include <strings.h>
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GLON_HTTP_SRC_CAP 65536
#define GLON_HTTP_CTYPE_CAP ((size_t)GLON_HTTP_TYPE_CAP)

typedef struct {
    int status;
    char ctype[GLON_HTTP_TYPE_CAP];
    unsigned header_count;
    char header_names[GLON_HTTP_MAX_HEADERS][GLON_HTTP_HEADER_NAME_CAP];
    char header_values[GLON_HTTP_MAX_HEADERS][GLON_HTTP_HEADER_VALUE_CAP];
    size_t body_len;
    unsigned char body[GLON_HTTP_RESPONSE_CAP];
} http_response;

typedef struct {
    glon_http_config cfg;
    glon_http_pump_fn pre_pump;
    void *pre_pump_user;
    glon_http_pump_fn pump;
    void *pump_user;
    pthread_mutex_t lock;
    volatile int stop_requested;      /* read by the main polling loop */
    volatile int fatal;               /* read by the main polling loop */
    volatile unsigned long completed; /* read by the main polling loop */
    unsigned long next_id;
    unsigned long current_id;
    unsigned long requests;
    unsigned long errors;
    unsigned long write_failures;
    unsigned long rejected;
} http_state;

static http_state http;

static volatile sig_atomic_t signal_stop_flag;

/* ---- source builder ------------------------------------------------------ */

typedef struct {
    char *out;
    size_t len;
    size_t cap;
    int overflow;
} http_src;

static void hs_byte(http_src *b, char c) {
    if (b->len + 1 >= b->cap) { b->overflow = 1; return; }
    b->out[b->len++] = c;
}

static void hs_text(http_src *b, const char *s) {
    while (*s) hs_byte(b, *s++);
}

static void hs_uint(http_src *b, unsigned long v) {
    char tmp[32];
    int n = 0;
    do { tmp[n++] = (char)('0' + (int)(v % 10)); v /= 10; } while (v);
    while (n) hs_byte(b, tmp[--n]);
}

/* Emit one ordinary Glon managed string from raw host bytes.  This is the same
 * technique the optional file module uses for reply bytes. */
static void hs_string(http_src *b, const unsigned char *bytes, size_t n) {
    hs_text(b, "mk-string [");
    for (size_t i = 0; i < n; i++) {
        hs_byte(b, ' ');
        hs_uint(b, (unsigned long)bytes[i]);
    }
    hs_text(b, " ]");
}

/* ---- one session turn ---------------------------------------------------- */

static int http_run_expression(const char *src, r0_s1_outcome *oc) {
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

static void http_outcome_text(const r0_s1_outcome *oc, char *err, size_t errcap) {
    switch (oc->status) {
    case R0S1_OUT_OK:
        snprintf(err, errcap, "ok");
        break;
    case R0S1_OUT_PARSE_ERROR:
        snprintf(err, errcap, "parse error (%s)",
                 r0_s1_outcome_detail_name(oc->detail));
        break;
    case R0S1_OUT_RESOURCE_ERROR:
        snprintf(err, errcap, "resource error (%s)",
                 r0_s1_outcome_detail_name(oc->detail));
        break;
    case R0S1_OUT_UNCAUGHT_SIN:
        snprintf(err, errcap, "uncaught SIN! (%s)",
                 r0_s1_outcome_status_name(oc->status));
        break;
    case R0S1_OUT_HALT:
        snprintf(err, errcap, "machine halt (%s)",
                 r0_s1_outcome_detail_name(oc->detail));
        break;
    default:
        snprintf(err, errcap, "unknown session outcome %d", oc->status);
        break;
    }
}

/* ---- Glon-side listener configuration ----------------------------------- */

int glon_http_take_listen(int *port, char *err, size_t errcap) {
    r0_s1_outcome oc;
    if (http_run_expression("[ http/take-listen ]", &oc) != 0 ||
        oc.status != R0S1_OUT_OK) {
        http_outcome_text(&oc, err, errcap);
        return -1;
    }
    if (oc.count <= 0 ||
        (oc.count == 1 && r0_s1_result(0, oc.count) == R0_NONE)) {
        return 0;
    }
    if (oc.count != 1) {
        snprintf(err, errcap, "http/take-listen returned %d values", oc.count);
        return -1;
    }
    cell v = r0_s1_result(0, oc.count);
    if (r0_tag(v) != T_BLOCK) {
        snprintf(err, errcap, "http/take-listen did not return a BLOCK or NONE");
        return -1;
    }
    cell p = r0_untag(v);
    if (M[p] != 2) {
        snprintf(err, errcap, "http/take-listen block shape");
        return -1;
    }
    cell pv = M[p + BLK_DATA + 1];
    if (r0_tag(pv) != T_INT) {
        snprintf(err, errcap, "http/take-listen port is not an integer");
        return -1;
    }
    long n = (long)int_val(pv);
    if (n < 1 || n > 65535) {
        snprintf(err, errcap, "http/take-listen port %ld is out of range", n);
        return -1;
    }
    *port = (int)n;
    return 1;
}

/* ---- request dispatch and response extraction --------------------------- */

static int http_dispatch(unsigned long id, const char *method,
                         const char *path, const char *query,
                         const unsigned char *body, size_t body_len,
                         const struct mg_request_info *ri,
                         char *err, size_t errcap) {
    char src[GLON_HTTP_SRC_CAP];
    http_src b = { src, 0, sizeof src, 0 };

    hs_text(&b, "[ http/dispatch ");
    hs_uint(&b, id);
    hs_byte(&b, ' ');
    hs_string(&b, (const unsigned char *)method, strlen(method));
    hs_byte(&b, ' ');
    hs_string(&b, (const unsigned char *)path, strlen(path));
    hs_byte(&b, ' ');
    hs_string(&b, (const unsigned char *)query, strlen(query));
    hs_byte(&b, ' ');
    hs_string(&b, body, body_len);
    hs_byte(&b, ' ');
    hs_text(&b, "reduce [");
    unsigned nh = ri ? ri->num_headers : 0;
    if (nh > GLON_HTTP_MAX_REQUEST_HEADERS)
        nh = GLON_HTTP_MAX_REQUEST_HEADERS;
    for (unsigned i = 0; i < nh; i++) {
        const char *name = ri->http_headers[i].name ? ri->http_headers[i].name : "";
        const char *value = ri->http_headers[i].value ? ri->http_headers[i].value : "";
        hs_byte(&b, ' ');
        hs_string(&b, (const unsigned char *)name, strlen(name));
        hs_byte(&b, ' ');
        hs_string(&b, (const unsigned char *)value, strlen(value));
    }
    hs_text(&b, " ] ");
    hs_string(&b, (const unsigned char *)(ri && ri->remote_addr ? ri->remote_addr : ""),
              strlen(ri && ri->remote_addr ? ri->remote_addr : ""));
    hs_text(&b, " ]");
    if (b.overflow || b.len + 1 >= b.cap) {
        snprintf(err, errcap, "dispatch source is too large");
        return -1;
    }
    src[b.len] = 0;

    r0_s1_outcome oc;
    if (http_run_expression(src, &oc) != 0 || oc.status != R0S1_OUT_OK) {
        http_outcome_text(&oc, err, errcap);
        return -1;
    }
    return 0;
}

static int copy_glon_string(cell v, unsigned char *out, size_t cap,
                            size_t *out_len, char *err, size_t errcap) {
    if (r0_tag(v) != T_STRING) {
        snprintf(err, errcap, "response field is not a STRING!");
        return -1;
    }
    cell p = r0_untag(v);
    cell n = M[p];
    if (n < 0 || (size_t)n > cap) {
        snprintf(err, errcap, "response field is too large");
        return -1;
    }
    for (cell i = 0; i < n; i++) {
        unsigned char c = (unsigned char)M[p + 1 + i];
        if ((c < 32 && c != 9) || c == 127) {
            snprintf(err, errcap, "header contains a control byte");
            return -1;
        }
        out[i] = c;
    }
    *out_len = (size_t)n;
    return 0;
}

/* A response body may be a STRING! or a nested block of STRING!/BLOCK!
 * parts.  The host flattens it in order into one bounded transport buffer, so
 * an application can compose a long page without repeated string copying. */
static int http_copy_body_part(cell v, unsigned char *out, size_t cap,
                               size_t *used, int depth,
                               char *err, size_t errcap) {
    if (depth > 256) {
        snprintf(err, errcap, "response body nesting is too deep");
        return -1;
    }
    if (r0_tag(v) == T_STRING) {
        cell p = r0_untag(v);
        cell n = M[p];
        if (n < 0 || (size_t)n > cap - *used) {
            snprintf(err, errcap, "response body is too large");
            return -1;
        }
        for (cell i = 0; i < n; i++) out[*used + (size_t)i] = (unsigned char)M[p + 1 + i];
        *used += (size_t)n;
        return 0;
    }
    if (r0_tag(v) == T_BLOCK) {
        cell p = r0_untag(v);
        cell n = M[p];
        for (cell i = 0; i < n; i++) {
            if (http_copy_body_part(M[p + BLK_DATA + i], out, cap, used,
                                    depth + 1, err, errcap) != 0)
                return -1;
        }
        return 0;
    }
    snprintf(err, errcap, "response body part is not a STRING! or BLOCK!");
    return -1;
}

static int http_take_body(cell bv, http_response *out, char *err, size_t errcap) {
    out->body_len = 0;
    return http_copy_body_part(bv, out->body, sizeof out->body,
                               &out->body_len, 0, err, errcap);
}

static int copy_header_string(cell v, char *out, size_t cap,
                              char *err, size_t errcap) {
    size_t n = 0;
    if (copy_glon_string(v, (unsigned char *)out, cap - 1, &n, err, errcap) != 0)
        return -1;
    out[n] = 0;
    return 0;
}

/* `headers` is NONE or an alternating block of STRING! name/value pairs.  The
 * host owns Content-Type, Content-Length and Connection; those names are
 * filtered when the response is written. */
static int http_take_headers(cell hv, http_response *out,
                             char *err, size_t errcap) {
    out->header_count = 0;
    if (hv == R0_NONE) return 0;
    if (r0_tag(hv) != T_BLOCK) {
        snprintf(err, errcap, "response headers are not a BLOCK or NONE");
        return -1;
    }
    cell hp = r0_untag(hv);
    cell n = M[hp];
    if (n < 0 || (n % 2) != 0) {
        snprintf(err, errcap, "response headers need name/value pairs");
        return -1;
    }
    unsigned pairs = (unsigned)(n / 2);
    if (pairs > GLON_HTTP_MAX_HEADERS) {
        snprintf(err, errcap, "too many response headers");
        return -1;
    }
    for (unsigned i = 0; i < pairs; i++) {
        cell nv = M[hp + BLK_DATA + i * 2];
        cell vv = M[hp + BLK_DATA + i * 2 + 1];
        char *name = out->header_names[out->header_count];
        char *value = out->header_values[out->header_count];
        if (copy_header_string(nv, name, GLON_HTTP_HEADER_NAME_CAP,
                               err, errcap) != 0) return -1;
        if (name[0] == 0) {
            snprintf(err, errcap, "empty response header name");
            return -1;
        }
        if (copy_header_string(vv, value, GLON_HTTP_HEADER_VALUE_CAP,
                               err, errcap) != 0) return -1;
        out->header_count++;
    }
    return 0;
}

static int http_take_response(unsigned long expected, http_response *out,
                              int *have, char *err, size_t errcap) {
    r0_s1_outcome oc;
    *have = 0;
    if (http_run_expression("[ http/take-response ]", &oc) != 0 ||
        oc.status != R0S1_OUT_OK) {
        http_outcome_text(&oc, err, errcap);
        return -1;
    }
    if (oc.count <= 0 ||
        (oc.count == 1 && r0_s1_result(0, oc.count) == R0_NONE)) {
        return 0;
    }
    if (oc.count != 1) {
        snprintf(err, errcap, "http/take-response returned %d values", oc.count);
        return -1;
    }
    cell v = r0_s1_result(0, oc.count);
    if (r0_tag(v) != T_BLOCK) {
        snprintf(err, errcap, "http/take-response did not return a BLOCK or NONE");
        return -1;
    }
    cell p = r0_untag(v);
    if (M[p] != 5) {
        snprintf(err, errcap, "http/take-response block shape");
        return -1;
    }
    cell idv = M[p + BLK_DATA];
    cell sv = M[p + BLK_DATA + 1];
    cell tv = M[p + BLK_DATA + 2];
    cell hv = M[p + BLK_DATA + 3];
    cell bv = M[p + BLK_DATA + 4];
    if (r0_tag(idv) != T_INT || r0_tag(sv) != T_INT) {
        snprintf(err, errcap, "response id/status are not integers");
        return -1;
    }
    if ((unsigned long)int_val(idv) != expected) {
        snprintf(err, errcap, "response id does not match the active request");
        return -1;
    }
    long status = (long)int_val(sv);
    if (status < 100 || status > 599) {
        snprintf(err, errcap, "response status %ld is out of range", status);
        return -1;
    }
    out->status = (int)status;
    size_t tlen = 0;
    if (copy_glon_string(tv, (unsigned char *)out->ctype,
                         GLON_HTTP_CTYPE_CAP - 1, &tlen, err, errcap) != 0) {
        return -1;
    }
    out->ctype[tlen] = 0;
    if (http_take_headers(hv, out, err, errcap) != 0) return -1;
    if (http_take_body(bv, out, err, errcap) != 0) return -1;
    *have = 1;
    return 0;
}

static int http_app_stopped(void) {
    r0_s1_outcome oc;
    if (http_run_expression("[ http/stopped? ]", &oc) != 0 ||
        oc.status != R0S1_OUT_OK || oc.count != 1) {
        return 0;
    }
    cell v = r0_s1_result(0, oc.count);
    if (r0_tag(v) != T_INT) return 0;
    return int_val(v) != 0;
}

/* ---- HTTP transport ------------------------------------------------------ */

static int http_header_managed(const char *name) {
    return strcasecmp(name, "Content-Type") == 0
        || strcasecmp(name, "Content-Length") == 0
        || strcasecmp(name, "Connection") == 0
        || strcasecmp(name, "Transfer-Encoding") == 0;
}

static int http_send_response(struct mg_connection *conn,
                              const http_response *r) {
    char len[32];
    snprintf(len, sizeof len, "%lu", (unsigned long)r->body_len);
    if (mg_response_header_start(conn, r->status) < 0) return -1;
    if (mg_response_header_add(conn, "Content-Type", r->ctype, -1) < 0) return -1;
    if (mg_response_header_add(conn, "Content-Length", len, -1) < 0) return -1;
    if (!http.cfg.keep_alive &&
        mg_response_header_add(conn, "Connection", "close", -1) < 0) return -1;
    if (mg_response_header_add(conn, "Cache-Control", "no-store", -1) < 0) return -1;
    for (unsigned i = 0; i < r->header_count; i++) {
        if (http_header_managed(r->header_names[i])) continue;
        if (mg_response_header_add(conn, r->header_names[i],
                                   r->header_values[i], -1) < 0) return -1;
    }
    if (mg_response_header_send(conn) < 0) return -1;
    if (r->body_len > 0) {
        int n = mg_write(conn, r->body, r->body_len);
        if (n < 0 || (size_t)n != r->body_len) return -1;
    }
    return 0;
}

static int http_send_text(struct mg_connection *conn, int status,
                          const char *ctype, const char *text) {
    http_response r;
    size_t n = strlen(text);
    if (n > sizeof r.body) n = sizeof r.body;
    memset(&r, 0, sizeof r);
    r.status = status;
    snprintf(r.ctype, sizeof r.ctype, "%s", ctype);
    r.body_len = n;
    memcpy(r.body, text, n);
    return http_send_response(conn, &r);
}

static int http_begin_request(struct mg_connection *conn) {
    const struct mg_request_info *ri = mg_get_request_info(conn);
    if (!ri) return 0;

    const char *method = ri->request_method ? ri->request_method : "GET";
    const char *path = ri->local_uri ? ri->local_uri
                      : (ri->request_uri ? ri->request_uri : "/");
    /* CivetWeb parses the query separately; Glon receives it raw (not
     * URL-decoded) and the path is the decoded local URI. */
    const char *query = ri->query_string ? ri->query_string : "";
    if (strlen(method) >= GLON_HTTP_METHOD_CAP ||
        strlen(path) >= GLON_HTTP_URI_CAP ||
        strlen(query) >= GLON_HTTP_QUERY_CAP) {
        http.rejected++;
        return http_send_text(conn, 414, "text/plain; charset=utf-8",
                              "URI or query too long") == 0 ? 414 : 500;
    }
    long long declared = ri->content_length;
    int may_have_body = strcasecmp(method, "POST") == 0
                     || strcasecmp(method, "PUT") == 0
                     || strcasecmp(method, "PATCH") == 0;
    if (declared < 0) {
        if (may_have_body) {
            http.rejected++;
            return http_send_text(conn, 411, "text/plain; charset=utf-8",
                                  "Content-Length required") == 0 ? 411 : 500;
        }
        declared = 0;
    }
    if ((unsigned long long)declared > http.cfg.max_body ||
        (unsigned long long)declared > GLON_HTTP_REQUEST_BODY_HARD) {
        http.rejected++;
        return http_send_text(conn, 413, "text/plain; charset=utf-8",
                              "request body too large") == 0 ? 413 : 500;
    }

    unsigned char body[GLON_HTTP_REQUEST_BODY_HARD];
    size_t body_len = 0;
    while (body_len < (size_t)declared) {
        int n = mg_read(conn, body + body_len,
                        (size_t)declared - body_len);
        if (n <= 0) {
            http.rejected++;
            return http_send_text(conn, 400, "text/plain; charset=utf-8",
                                  "incomplete request body") == 0 ? 400 : 500;
        }
        body_len += (size_t)n;
    }

    int reply_status = 500;
    pthread_mutex_lock(&http.lock);
    unsigned long id = ++http.next_id;
    if (id == 0) id = ++http.next_id;   /* ids are opaque; skip 0 */
    http.current_id = id;
    http.requests++;

    char err[256];
    if (http_dispatch(id, method, path, query, body, body_len, ri,
                      err, sizeof err) != 0) {
        fprintf(stderr, "glon: http: dispatch %lu: %s\n", id, err);
        http.errors++;
        http.fatal = 1;
        if (http_send_text(conn, 500, "text/plain; charset=utf-8",
                           "Glon dispatch failed") == 0) reply_status = 500;
    } else {
        if (http.pre_pump) {
            int pr;
            do {
                pr = http.pre_pump(http.pre_pump_user);
                if (pr < 0) {
                    fprintf(stderr, "glon: http: security pump failed for request %lu\n", id);
                    http.errors++;
                    http.fatal = 1;
                    break;
                }
            } while (pr > 0);
        }
        http_response r;
        int have = 0;
        if (http.fatal) {
            if (http_send_text(conn, 500, "text/plain; charset=utf-8",
                               "Glon security operation failed") == 0) reply_status = 500;
        } else if (http_take_response(id, &r, &have, err, sizeof err) != 0) {
            fprintf(stderr, "glon: http: response %lu: %s\n", id, err);
            http.errors++;
            http.fatal = 1;
            if (http_send_text(conn, 500, "text/plain; charset=utf-8",
                               "Glon response failed") == 0) reply_status = 500;
        } else if (!have) {
            http.errors++;
            if (http_send_text(conn, 500, "text/plain; charset=utf-8",
                               "no response") == 0) reply_status = 500;
        } else {
            reply_status = r.status;
            if (http_send_response(conn, &r) != 0) {
                http.write_failures++;   /* usually a client disconnect */
            }
            http.completed++;
        }
    }
    if (!http.fatal && http_app_stopped()) http.stop_requested = 1;
    {
        r0_s1_outcome clear_oc;
        (void)http_run_expression("[ http/clear-current ]", &clear_oc);
    }
    http.current_id = 0;

    /* Optional host pump (the file module's asynchronous request queue).  It
     * enters the Glon evaluator, so it runs under the same lock as dispatch:
     * with a worker pool this keeps evaluator ownership single-threaded. */
    if (http.pump && http.pump(http.pump_user) != 0) {
        http.errors++;
        http.fatal = 1;
    }

    pthread_mutex_unlock(&http.lock);

    return reply_status;
}

static void http_signal_handler(int sig) {
    (void)sig;
    signal_stop_flag = 1;
}

#ifdef _WIN32
/* A named event gives the Windows test driver and an embedding host an orderly
 * shutdown path that SIGTERM cannot provide (Windows maps SIGTERM to process
 * termination).  The name is UTF-8 on the Glon command line. */
static HANDLE http_open_stop_event(const char *name) {
    WCHAR wname[512];
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1,
                                wname, (int)(sizeof wname / sizeof wname[0]));
    if (n <= 0) return NULL;
    return OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, wname);
}
#endif

void glon_http_config_default(glon_http_config *cfg) {
    memset(cfg, 0, sizeof *cfg);
    cfg->bind = "127.0.0.1";
    cfg->max_requests = 0;
    cfg->request_timeout_ms = 10000;
    cfg->max_request_size = 8192;
    cfg->max_body = GLON_HTTP_REQUEST_BODY_DEFAULT;
    cfg->num_threads = 1;
    cfg->keep_alive = 0;
}

int glon_http_run(const glon_http_config *cfg, int report,
                  glon_http_pump_fn pump, void *pump_user,
                  char *err, size_t errcap) {
    int port = 0;
    int lr = glon_http_take_listen(&port, err, errcap);
    if (lr < 0) return -1;
    if (lr == 0) return 0;                 /* application did not listen */

    if (!cfg->cert || !cfg->cert[0]) {
        snprintf(err, errcap,
                 "the application configured a listener but --http-cert was not supplied");
        return -1;
    }

    memset(&http, 0, sizeof http);
    http.cfg = *cfg;
    http.pre_pump = cfg->pre_pump;
    http.pre_pump_user = cfg->pre_pump_user;
    http.pump = pump;
    http.pump_user = pump_user;
    pthread_mutex_init(&http.lock, NULL);
#ifdef _WIN32
    HANDLE stop_event = NULL;
    if (cfg->stop_event && cfg->stop_event[0]) {
        stop_event = http_open_stop_event(cfg->stop_event);
        if (!stop_event) {
            snprintf(err, errcap, "cannot open shutdown event (win32 %lu)",
                     (unsigned long)GetLastError());
            pthread_mutex_destroy(&http.lock);
            return -1;
        }
    }
#endif

    char ports[256];
    snprintf(ports, sizeof ports, "%s:%d%s", cfg->bind, port, "s");

    char timeout[32], maxreq[32], nthreads_buf[32];
    snprintf(timeout, sizeof timeout, "%u", cfg->request_timeout_ms);
    snprintf(maxreq, sizeof maxreq, "%u", cfg->max_request_size);
    int keep_alive = cfg->keep_alive;
    /* Bounded worker pool.  The Glon session stays serialised by http.lock, so
     * extra workers only add connection-level overlap, never concurrent
     * evaluator entry.  Default 1 preserves the original behaviour. */
    unsigned nthreads = cfg->num_threads;
    if (nthreads == 0) nthreads = 1;
    if (nthreads > GLON_HTTP_MAX_THREADS) nthreads = GLON_HTTP_MAX_THREADS;
    snprintf(nthreads_buf, sizeof nthreads_buf, "%u", nthreads);

    const char *options[] = {
        "listening_ports", ports,
        "num_threads", nthreads_buf,
        "enable_keep_alive", keep_alive ? "yes" : "no",
        "request_timeout_ms", timeout,
        "max_request_size", maxreq,
        "ssl_certificate", cfg->cert,
        "document_root", ".",
        "decode_url", "yes",
        NULL
    };

    struct mg_callbacks callbacks;
    memset(&callbacks, 0, sizeof callbacks);
    callbacks.begin_request = http_begin_request;

#ifdef _WIN32
    signal(SIGINT, http_signal_handler);
    signal(SIGTERM, http_signal_handler);
    signal_stop_flag = 0;
#else
    struct sigaction sa, old_int, old_term;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = http_signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, &old_int);
    sigaction(SIGTERM, &sa, &old_term);
    signal_stop_flag = 0;
#endif

    struct mg_context *ctx = mg_start(&callbacks, NULL, options);
    if (!ctx) {
        snprintf(err, errcap,
                 "HTTPS listener could not start (bind/TLS configuration failed); "
                 "no insecure fallback was attempted");
#ifdef _WIN32
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
#else
        sigaction(SIGINT, &old_int, NULL);
        sigaction(SIGTERM, &old_term, NULL);
#endif
#ifdef _WIN32
        if (stop_event) CloseHandle(stop_event);
#endif
        pthread_mutex_destroy(&http.lock);
        return -1;
    }
    fprintf(stderr, "glon: http: listening on https://%s:%d\n", cfg->bind, port);

    while (!signal_stop_flag && !http.stop_requested && !http.fatal) {
        if (cfg->max_requests && http.completed >= cfg->max_requests) break;
#ifdef _WIN32
        if (stop_event && WaitForSingleObject(stop_event, 0) == WAIT_OBJECT_0)
            http.stop_requested = 1;
#endif
        usleep(10000);
    }

    mg_stop(ctx);
#ifdef _WIN32
    if (stop_event) CloseHandle(stop_event);
#endif
#ifdef _WIN32
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
#else
    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGTERM, &old_term, NULL);
#endif
    if (pump && pump(pump_user) != 0) {
        http.errors++;
        http.fatal = 1;
    }
    pthread_mutex_destroy(&http.lock);

    if (report) {
        fprintf(stderr,
                "glon: http: requests=%lu completed=%lu errors=%lu "
                "rejected=%lu write_failures=%lu gc=%ld live_cells=%ld "
                "live_objs=%ld heap_high=%ld free_cells=%ld\n",
                http.requests, http.completed, http.errors, http.rejected,
                http.write_failures, r0_s1_gc_count(),
                r0_s1_gc_live_cells(), r0_s1_gc_live_objs(),
                r0_s1_heap_high(), r0_s1_gc_free_cells());
    }

    snprintf(err, errcap, "stopped");
    return http.fatal ? -1 : 0;
}
