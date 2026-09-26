/* r0_s1_g1a_live.c -- experimental GLON_LIVE host-boundary layer.
 *
 * Compiled only into the Live Translate WASM build and its focused native
 * test.  It adds no S1 opcode, no parser/evaluator change, no native id, and
 * no change to r0_s1_g1a.c; it uses the same public r0_s1_parse /
 * r0_s1_run_persistent / r0_s1_g1a_render_fragment surface as the base G1A
 * dispatcher.  See r0_s1_g1a_live.h.
 */

#include "r0_s1_g1a_live.h"
#include "r0_s1_g1a.h"
#include "r0_s1.h"

/* The wrapper source is only the tiny dispatch skeleton; inbound value bytes
 * are emitted as decimal into the mk-string blocks.  The old 200-byte cap is
 * gone; the only bound here is this host source buffer. */
#define LIVE_SRC_CAP   65536
#define LIVE_OP_CAP    256
#define LIVE_ARG_CAP   8192
#define LIVE_CHUNK     400
#define LIVE_MAX_BYTES 12000

static char live_src[LIVE_SRC_CAP];
static char live_op[LIVE_OP_CAP];
static char live_arg[LIVE_ARG_CAP];

static r0_s1_g1a_live_host_call_fn live_host_call;
static void *live_host_call_user;

void r0_s1_g1a_live_set_host_call(r0_s1_g1a_live_host_call_fn fn, void *user) {
    live_host_call = fn;
    live_host_call_user = user;
}

/* ---- tiny libc-free source builder -------------------------------------- */
typedef struct { char *out; int len; int cap; } live_buf;

static void lb_byte(live_buf *b, char c) {
    if (b->len < b->cap - 1) b->out[b->len++] = c;
}
static void lb_str(live_buf *b, const char *s) {
    while (*s) lb_byte(b, *s++);
}
static void lb_int(live_buf *b, long v) {
    char tmp[24];
    int n = 0;
    unsigned long u;
    if (v < 0) { u = (unsigned long)(-(v + 1)) + 1u; lb_byte(b, '-'); }
    else u = (unsigned long)v;
    do { tmp[n++] = (char)('0' + (int)(u % 10)); u /= 10; } while (u);
    while (n) lb_byte(b, tmp[--n]);
}

static int live_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int live_token_ok(const char *token) {
    int n = 0;
    for (const char *c = token; *c; c++) {
        if (*c == '[' || *c == ']' || *c == '\'' || *c == ':' ||
            *c == ' ' || *c == '\t' || *c == '\r' || *c == '\n')
            return 0;
        n++;
    }
    return n > 0;
}

/* ---- request-fragment recognition --------------------------------------- */
/* A host-call request fragment is a byte-list block:
 *   byte 0 = 0x01, then op bytes, 0x00, then arg bytes. */
static int live_frag_byte(cell frag, cell i) {
    cell p = r0_untag(frag);
    return (int)int_val(M[p + BLK_DATA + i]);
}

static int live_finish(cell frag, char *out, int cap, int *out_len) {
    if (out_len) *out_len = 0;
    if (r0_tag(frag) == T_BLOCK) {
        cell p = r0_untag(frag);
        cell n = M[p];
        if (n >= 1 && live_frag_byte(frag, 0) == 1) {
            cell i = 1;
            int opn = 0;
            while (i < n && live_frag_byte(frag, i) != 0) {
                if (opn < LIVE_OP_CAP) live_op[opn++] = (char)live_frag_byte(frag, i);
                i++;
            }
            if (i < n) {                          /* 0x00 separator present */
                int argn = 0;
                i++;                              /* skip the separator */
                while (i < n) {
                    if (argn < LIVE_ARG_CAP) live_arg[argn++] = (char)live_frag_byte(frag, i);
                    i++;
                }
                if (live_host_call) {
                    live_host_call(live_op, opn, live_arg, argn, live_host_call_user);
                    return 0;                     /* request handled; no HTML */
                }
            }
        }
    }
    return r0_s1_g1a_render_fragment(frag, out, cap, out_len);
}

/* ---- one dispatch -------------------------------------------------------- */
/* data != 0 means "bind current-value from these bytes" (dlen may be 0). */
static int live_dispatch(const char *var, const char *dispatcher, const char *token,
                         const unsigned char *data, unsigned int dlen,
                         char *out, int cap, int *out_len) {
    if (out_len) *out_len = 0;
    if (!live_token_ok(token)) return -1;
    if (dlen > LIVE_MAX_BYTES) return -1;

    live_buf b = { live_src, 0, LIVE_SRC_CAP };
    lb_byte(&b, '[');
    lb_byte(&b, ' ');
    lb_str(&b, var);
    lb_str(&b, ": '");
    lb_str(&b, token);

    if (data != 0) {
        unsigned int off = 0;
        int first = 1;
        do {
            unsigned int n = dlen - off;
            if (n > LIVE_CHUNK) n = LIVE_CHUNK;
            if (first) {
                lb_str(&b, " current-value: mk-string [");
                first = 0;
            } else {
                lb_str(&b, " current-value: s/+ current-value mk-string [");
            }
            for (unsigned int k = 0; k < n; k++) {
                lb_byte(&b, ' ');
                lb_int(&b, (long)data[off + k]);
            }
            lb_str(&b, " ]");
            off += n;
        } while (off < dlen);
    }

    lb_str(&b, " do ");
    lb_str(&b, dispatcher);
    lb_str(&b, " ]");
    if (b.len >= b.cap - 1) return -1;
    live_src[b.len] = 0;

    cell save_lhp = M[GC_LOADER_HP];
    M[G1_VIS] = 0;                            /* same default as r0_s1_g1a.c */
    int err = 0;
    cell blk = r0_s1_parse(live_src, &err);
    if (err) return -1;
    int N = r0_s1_run_persistent(blk);
    M[GC_LOADER_HP] = save_lhp;
    if (N < 1) return -1;
    return live_finish(r0_s1_result(0, N), out, cap, out_len);
}

int r0_s1_g1a_live_route(const char *token, char *out, int cap, int *out_len) {
    return live_dispatch("current-route", "route", token, 0, 0, out, cap, out_len);
}

int r0_s1_g1a_live_event(const char *token, char *out, int cap, int *out_len) {
    return live_dispatch("current-event", "do-event", token, 0, 0, out, cap, out_len);
}

int r0_s1_g1a_live_event_value(const char *token, const char *value,
                               char *out, int cap, int *out_len) {
    return live_dispatch("current-event", "do-event", token,
                         (const unsigned char *)value, (unsigned int)live_strlen(value),
                         out, cap, out_len);
}

int r0_s1_g1a_live_event_bytes(const char *token,
                               const unsigned char *data, unsigned int dlen,
                               char *out, int cap, int *out_len) {
    return live_dispatch("current-event", "do-event", token, data, dlen,
                         out, cap, out_len);
}
