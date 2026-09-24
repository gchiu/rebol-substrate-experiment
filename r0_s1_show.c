/* r0_s1_show.c - run one Glon program and describe its outcome as text.
 *
 * Shared by the native primer doc-tests and the WASM host's glon_run export,
 * so the text a reader sees in the browser primer is exactly the text the
 * native suite checks. It formats the REAL results left by the run; it does
 * no evaluation of its own.
 *
 * Outcome text:
 *   clean run        the results, molded and separated by one space
 *                    (no results: "none")
 *   uncaught SIN!    "** uncaught #[SIN! type id arg]"
 *   context full     "** halted: context full (no SIN!: a machine-level fail-stop)"
 *   other halt       "** halted (no SIN!: a machine-level fail-stop)"
 *   parse error      "** parse error"
 *
 * Molding is deliberately minimal: integers, none, the four word forms,
 * blocks (nested), strings, SIN! and an opaque #[...] for everything else.
 *
 * No libc: the WASM build links without one (see standalone/glon.c). */

#include "r0_s1.h"

typedef struct { char *p; int n, cap; } out_t;

static void put_ch(out_t *o, char c) {
    if (o->n < o->cap - 1) o->p[o->n++] = c;
}
static void put_s(out_t *o, const char *s) {
    while (s && *s) put_ch(o, *s++);
}
static void put_int(out_t *o, long v) {
    char tmp[24];
    int k = 0;
    unsigned long u = (v < 0) ? (unsigned long)(-(v + 1)) + 1 : (unsigned long)v;
    if (v < 0) put_ch(o, '-');
    do { tmp[k++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (k) put_ch(o, tmp[--k]);
}

static void put_word(out_t *o, cell v) {
    const char *name = r0_s1_sym_name(word_id(v));
    if (name) put_s(o, name);
    else { put_s(o, "#[word "); put_int(o, (long)word_id(v)); put_ch(o, ']'); }
}

static void mold(out_t *o, cell v, int depth) {
    cell p = v & ~(cell)15;
    switch ((int)(v & 15)) {
    case T_INT:  put_int(o, (long)(v / 16)); break;
    case T_NONE: put_s(o, "none"); break;
    case T_WORD: put_word(o, v); break;
    case T_SET:  put_word(o, v); put_ch(o, ':'); break;
    case T_GET:  put_ch(o, ':'); put_word(o, v); break;
    case T_LIT:  put_ch(o, '\''); put_word(o, v); break;
    case T_BLOCK: {
        if (depth > 6) { put_s(o, "[...]"); break; }
        cell n = M[p + BLK_COUNT];
        put_ch(o, '[');
        for (cell i = 0; i < n; i++) {
            if (i >= 32) { put_s(o, " ..."); break; }
            put_ch(o, ' ');
            mold(o, M[p + BLK_DATA + i], depth + 1);
        }
        put_s(o, " ]");
        break;
    }
    case T_STRING: {
        cell n = M[p];
        put_ch(o, '"');
        for (cell i = 0; i < n && i < 200; i++) put_ch(o, (char)M[p + 1 + i]);
        put_ch(o, '"');
        break;
    }
    case T_ERROR:
        put_s(o, "#[SIN! ");
        mold(o, M[p + ERR_TYPE], depth + 1); put_ch(o, ' ');
        mold(o, M[p + ERR_ID], depth + 1);   put_ch(o, ' ');
        mold(o, M[p + ERR_ARG], depth + 1);
        put_ch(o, ']');
        break;
    case T_CLOSURE: put_s(o, "#[func]"); break;
    case T_NATIVE:  put_s(o, "#[native]"); break;
    case T_RAW:     put_s(o, "#[raw]"); break;
    case T_CONTEXT: put_s(o, "#[context]"); break;
    case T_USER:    put_s(o, "#[object]"); break;
    case T_BOUND:   put_s(o, "#[bound]"); break;
    default:        put_s(o, "#[?]"); break;
    }
}

/* the program source, wrapped as "[ src ]" (the loader's top-level form) */
static char progbuf[16400];

int r0_s1_show_run(const char *src, unsigned int len, char *out, int cap) {
    out_t o = { out, 0, cap };
    if (cap <= 0) return 0;
    if (len + 4 >= sizeof progbuf) {
        put_s(&o, "** program too long");
        o.p[o.n] = 0;
        return o.n;
    }
    /* wrap and strip ;; line comments (the reader does not know them) */
    unsigned int w = 0, r = 0;
    progbuf[w++] = '[';
    progbuf[w++] = ' ';
    while (r < len) {
        if (src[r] == ';' && r + 1 < len && src[r + 1] == ';') {
            while (r < len && src[r] != '\n') r++;
            continue;
        }
        progbuf[w++] = src[r++];
    }
    progbuf[w++] = ' ';
    progbuf[w++] = ']';
    progbuf[w] = 0;

    int err = 0;
    cell prog = r0_s1_parse(progbuf, &err);
    if (err) {
        put_s(&o, "** parse error");
    } else {
        int N = r0_s1_run_persistent(prog);
        cell t, i, a;
        if (r0_s1_ran_cleanly() && N >= 0) {
            if (N == 0) put_s(&o, "none");
            for (int k = 0; k < N; k++) {
                if (k) put_ch(&o, ' ');
                mold(&o, r0_s1_result(k, N), 0);
            }
        } else if (r0_s1_uncaught_error(&t, &i, &a)) {
            put_s(&o, "** uncaught #[SIN! ");
            mold(&o, t, 1); put_ch(&o, ' ');
            mold(&o, i, 1); put_ch(&o, ' ');
            mold(&o, a, 1);
            put_ch(&o, ']');
        } else {
            if (r0_s1_halt_reason() == R0S1_HALT_CONTEXT_FULL)
                put_s(&o, "** halted: context full (no SIN!: a machine-level fail-stop)");
            else
                put_s(&o, "** halted (no SIN!: a machine-level fail-stop)");
        }
    }
    o.p[o.n] = 0;
    return o.n;
}
