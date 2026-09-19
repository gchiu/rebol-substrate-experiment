/* r0_s1_g1a.c - G1A template engine + routing primitive (see r0_s1_g1a.h).
 *
 * This file uses only the public R0/S1 API (r0_s1_parse / r0_s1_run /
 * r0_s1_result / the M[] arena) plus the tag macros from r0_s1.h. It adds no
 * primitive, HOST service, native id, or evaluator/parser change. It is
 * compiled into BOTH the native test binary and the standalone WASM module
 * (where it is linked against the standalone/glon.c libc shims under
 * -nostdlib). To stay independent of both libc and those shims it avoids all
 * libc calls (its own tiny byte/string/integer helpers below).
 */

#include "r0_s1_g1a.h"
#include "r0_s1.h"

/* ---- tiny append helpers over a plain C buffer --------------------------- */

typedef struct { char *out; int len; int cap; } g1a_buf;

static void g1a_byte(g1a_buf *b, char c) {
    if (b->len < b->cap - 1) b->out[b->len++] = c;
}

static void g1a_int(g1a_buf *b, long v) {
    char tmp[24];
    int n = 0;
    unsigned long u;
    if (v < 0) { u = (unsigned long)(-(v + 1)) + 1u; }
    else u = (unsigned long)v;
    do { tmp[n++] = (char)('0' + (int)(u % 10)); u /= 10; } while (u);
    if (v < 0) g1a_byte(b, '-');
    while (n) g1a_byte(b, tmp[--n]);
}

/* ---- result stringification (embedded-block value -> rendered text) ------ */
/* INT -> decimal. STRING! -> its bytes. NONE / anything else -> nothing. */

static void g1a_stringify(g1a_buf *b, cell v) {
    if (v == R0_NONE) return;
    if (r0_tag(v) == T_INT) { g1a_int(b, (long)int_val(v)); return; }
    if (r0_tag(v) == T_STRING) {
        cell p = r0_untag(v);
        cell len = M[p];                 /* byte count */
        for (cell i = 0; i < len; i++) g1a_byte(b, (char)M[p + 1 + i]);
        return;
    }
    /* other types (WORD/SET/GET/LIT/BLOCK/...) contribute no inline text */
}

/* read one byte of a fragment (a BLOCK of T_INT byte codes) at index i */
static int g1a_frag_byte(cell fragment, cell i) {
    cell p = r0_untag(fragment);
    cell e = M[p + BLK_DATA + i];
    return (r0_tag(e) == T_INT) ? (int)int_val(e) : 0;
}

int r0_s1_g1a_render_fragment(cell fragment, char *out, int cap, int *out_len) {
    if (out_len) *out_len = 0;
    if (r0_tag(fragment) != T_BLOCK) return -1;

    g1a_buf b = { out, 0, cap };
    cell p = r0_untag(fragment);
    cell count = M[p];                 /* byte count */

    cell i = 0;
    while (i < count) {
        /* look for the embedded-Glon opener "<%" */
        if (i + 1 < count &&
            g1a_frag_byte(fragment, i) == '<' &&
            g1a_frag_byte(fragment, i + 1) == '%') {

            /* find the closer "%>" */
            cell j = i + 2;
            while (j + 1 < count &&
                   !(g1a_frag_byte(fragment, j) == '%' &&
                     g1a_frag_byte(fragment, j + 1) == '>'))
                j++;

            if (j + 1 >= count) {
                /* malformed: no closing "%>". Emit the rest literally. */
                for (cell k = i; k < count; k++) g1a_byte(&b, (char)g1a_frag_byte(fragment, k));
                break;
            }

            /* extract the embedded code bytes [i+2, j) */
            char code[512];
            int clen = 0;
            for (cell k = i + 2; k < j && clen < 511; k++)
                code[clen++] = (char)g1a_frag_byte(fragment, k);
            code[clen] = 0;

            /* evaluate the embedded block; skip silently on parse/run error */
            int err = 0;
            cell blk = r0_s1_parse(code, &err);
            if (!err) {
                int N = r0_s1_run(blk);
                if (N >= 1) g1a_stringify(&b, r0_s1_result(0, N));
            }

            i = j + 2;
        } else {
            g1a_byte(&b, (char)g1a_frag_byte(fragment, i));
            i++;
        }
    }

    if (b.len < cap) b.out[b.len] = 0;   /* NUL-terminate when there is room */
    if (out_len) *out_len = b.len;
    return 0;
}

static int g1a_dispatch(const char *var, const char *dispatcher, const char *token,
                        const char *value, char *out, int cap, int *out_len);

int r0_s1_g1a_route(const char *token, char *out, int cap, int *out_len) {
    return g1a_dispatch("current-route", "route", token, 0, out, cap, out_len);
}

int r0_s1_g1a_event(const char *token, char *out, int cap, int *out_len) {
    /* token-only events still bind current-value to an empty byte-list so the
     * dispatcher may always reference it safely. */
    return g1a_dispatch("current-event", "do-event", token, "", out, cap, out_len);
}

int r0_s1_g1a_event_value(const char *token, const char *value,
                          char *out, int cap, int *out_len) {
    return g1a_dispatch("current-event", "do-event", token, value, out, cap, out_len);
}

/* append one byte of the value as a decimal token (avoid libc) */
static int g1a_src_byte(char *src, int n, int cap, int b) {
    char d[4]; int k = 0;
    if (b == 0) { d[k++] = '0'; }
    else { while (b > 0) { d[k++] = (char)('0' + (b % 10)); b /= 10; } }
    if (n < cap) src[n++] = ' ';
    while (k > 0 && n < cap) src[n++] = d[--k];
    return n;
}

/* Shared primitive: bind `var` to the token word, optionally bind
 * `current-value` to a byte-list (when `value` != NULL), `do` the named
 * dispatcher block, then render the HTML it produced. */
static int g1a_dispatch(const char *var, const char *dispatcher, const char *token,
                        const char *value, char *out, int cap, int *out_len) {
    if (out_len) *out_len = 0;

    /* validate the token is a single safe word (no delimiters that would
     * break the source construction) */
    int tlen = 0;
    for (const char *c = token; *c; c++) {
        if (*c == '[' || *c == ']' || *c == '\'' || *c == ':' ||
            *c == ' ' || *c == '\t' || *c == '\r' || *c == '\n')
            return -1;
        tlen++;
    }
    if (tlen == 0) return -1;

    /* build "[ VAR: 'TOKEN[ current-value: [BYTES]] do DISPATCHER ]" */
    char src[1200];
    int n = 0, cap_src = (int)sizeof src - 1;
    src[n++] = '['; src[n++] = ' ';
    for (const char *c = var; *c && n < cap_src; c++) src[n++] = *c;
    const char *pre = ": '";
    for (; *pre && n < cap_src; pre++) src[n++] = *pre;
    for (const char *c = token; *c && n < cap_src; c++) src[n++] = *c;
    if (value != 0) {
        const char *vp = " current-value: [";
        for (; *vp && n < cap_src; vp++) src[n++] = *vp;
        for (const char *c = value; *c && n < cap_src - 4; c++)
            n = g1a_src_byte(src, n, cap_src, (unsigned char)*c);
        const char *ve = " ]";
        for (; *ve && n < cap_src; ve++) src[n++] = *ve;
    }
    const char *post = " do ";
    for (; *post && n < cap_src; post++) src[n++] = *post;
    for (const char *c = dispatcher; *c && n < cap_src; c++) src[n++] = *c;
    const char *post2 = " ]";
    for (; *post2 && n < cap_src; post2++) src[n++] = *post2;
    src[n] = 0;

    int err = 0;
    cell blk = r0_s1_parse(src, &err);
    if (err) return -1;

    int N = r0_s1_run(blk);
    if (N < 1) return -1;               /* no fragment produced */

    cell frag = r0_s1_result(0, N);
    return r0_s1_g1a_render_fragment(frag, out, cap, out_len);
}
