/* glon_http.h - optional native HTTPS listener for the Glon CLI.
 *
 * This module lives beside the frozen S1 substrate and the evaluator, like the
 * optional file module.  CivetWeb owns listening sockets, TLS, HTTP parsing,
 * connection lifetime and protocol limits.  The Glon session owns application
 * semantics: the handler closure, request dispatch and response generation.
 *
 * The bridge is the existing persistent-session request/reply pattern:
 *
 *   host -> Glon   [ http/dispatch <id> <method> <path> <query> <body> <headers> <peer> ]
 *   Glon -> host   [ http/take-response ]
 *                  => [id status ctype headers body] | NONE
 *
 * Request `body` is a STRING! of at most --http-max-body bytes.  Request
 * `headers` is a block of alternating STRING! name/value pairs (bounded);
 * `peer` is the transport remote address (not a client-supplied forwarding header).
 * Response `headers` is NONE or the same alternating-block shape.  A response
 * body may be a STRING! or a nested block of STRING!/BLOCK! parts; the host
 * flattens it in order into one bounded transport buffer.  The host adds
 * Content-Type, Content-Length and Connection itself.
 *
 * No new S1 primitive, HOST id, parser/evaluator change or parallel error
 * system is introduced.  The request id is an opaque integer; native pointers
 * never cross the boundary.  Only one CivetWeb worker thread is configured, so
 * the Glon evaluator is never entered concurrently.
 */
#ifndef GLON_HTTP_H
#define GLON_HTTP_H

#include <stddef.h>

#define GLON_HTTP_RESPONSE_CAP (64u * 1024u) /* one Glon-generated response body */
#define GLON_HTTP_REQUEST_BODY_DEFAULT 2048u
#define GLON_HTTP_REQUEST_BODY_HARD    4096u /* managed heap is only 7,232 cells */
#define GLON_HTTP_TYPE_CAP   128u
#define GLON_HTTP_URI_CAP    2048u
#define GLON_HTTP_QUERY_CAP  2048u
#define GLON_HTTP_METHOD_CAP 32u
#define GLON_HTTP_MAX_HEADERS 8u
#define GLON_HTTP_HEADER_NAME_CAP 64u
#define GLON_HTTP_HEADER_VALUE_CAP 256u
#define GLON_HTTP_MAX_REQUEST_HEADERS 16u

typedef int (*glon_http_pump_fn)(void *user);

typedef struct {
    const char *bind;              /* listening address; default 127.0.0.1 */
    const char *cert;              /* combined PEM certificate + private key */
    unsigned max_requests;         /* 0 = run until signal / http/stop */
    unsigned request_timeout_ms;   /* per-connection idle timeout */
    unsigned max_request_size;     /* CivetWeb header/request bound */
    unsigned max_body;             /* request body cap; 1..4096, default 2048 */
    int keep_alive;                /* 0 = close after each response */
    const char *stop_event;        /* Windows named event for orderly stop */
    /* Optional synchronous service pump for host-backed operations needed
     * before the response (currently the security module).  It runs only while
     * the current request is active; return 1 if it processed a request, 0 if
     * none, -1 on failure.  The file pump remains post-response. */
    glon_http_pump_fn pre_pump;
    void *pre_pump_user;
} glon_http_config;

void glon_http_config_default(glon_http_config *cfg);

/* Read the listener configuration the application registered with
 * `http/listen`.  Returns 1 and writes the port when configured, 0 when the
 * application did not start a listener, -1 on a session error. */
int glon_http_take_listen(int *port, char *err, size_t errcap);

/* Run the listener.  Returns 0 after a clean stop (including "no listener was
 * configured"), -1 on a startup/runtime failure.  A configured listener with a
 * missing or unusable certificate is a failure: there is no insecure fallback.
 * `report` prints request/GC counters at shutdown. */
int glon_http_run(const glon_http_config *cfg, int report,
                  glon_http_pump_fn pump, void *pump_user,
                  char *err, size_t errcap);

#endif /* GLON_HTTP_H */
