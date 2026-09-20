/* standalone/glon.c -- a minimal, self-contained browser embedding of the
 * frozen R0-on-S1 runtime.  This file is the ONLY new C code for W2; it is a
 * portability/host layer, not a change to evaluator semantics.
 *
 * It:
 *   - provides the tiny libc surface the frozen s1.c / r0_s1_runtime.c need
 *     (printf/fprintf/putchar/fflush, malloc, memcpy/memset/strcmp/strncmp/
 *     strlen, isspace/isdigit, stdout/stderr) WITHOUT libc;
 *   - declares the two generic host imports (host_print, host_set_text);
 *   - exports a small generic ABI (glon_alloc / glon_init / glon_load /
 *     glon_call) for a handwritten JS bootloader.
 *
 * No generated Emscripten JS runtime, no virtual filesystem, no --embed-file.
 */
#include "r0_s1.h"
#include "r0_s1_g1a.h"
#include "m1_layout.h"
#include <stdarg.h>

/* ---- host imports (provided by the handwritten host.js) ------------------ */
__attribute__((import_module("env"), import_name("host_print")))
void host_print(const unsigned char *ptr, unsigned int len);

__attribute__((import_module("env"), import_name("host_set_text")))
void host_set_text(unsigned int handle, unsigned int value);

/* G1A: write rendered HTML into the DOM element tagged data-glon-id="handle" */
__attribute__((import_module("env"), import_name("host_set_html")))
void host_set_html(unsigned int handle, const unsigned char *ptr, unsigned int len);

/* ---- minimal libc surface (no libc linked) ------------------------------- */

/* tiny stdio shim: FILE is just an integer fd */
typedef struct { int fd; } GLON_FILE;
typedef GLON_FILE FILE;
static GLON_FILE _stdout = { 1 };
static GLON_FILE _stderr = { 2 };
FILE *stdout = &_stdout;
FILE *stderr = &_stderr;

/* a small formatting scratch buffer flushed straight to host_print */
static unsigned char outbuf[256];

static unsigned int append_uint(unsigned char *p, unsigned int v) {
    unsigned char tmp[16];
    unsigned int n = 0;
    do { tmp[n++] = (unsigned char)('0' + (v % 10)); v /= 10; } while (v);
    while (n) *p++ = tmp[--n];
    return (unsigned int)(p - outbuf);
}

static unsigned int append_int(unsigned char *p, long v) {
    unsigned int len;
    if (v < 0) { *p++ = '-'; len = append_uint(p, (unsigned int)(-v)) + 1; }
    else len = append_uint(p, (unsigned int)v);
    return len;
}

static unsigned int append_str(unsigned char *p, const char *s) {
    while (*s) *p++ = (unsigned char)*s++;
    return (unsigned int)(p - outbuf);
}

/* A small vfprintf for the exact formats the frozen code uses: literal text,
 * %d/%ld and %s.  Writes into outbuf; caller decides where to send it. */
static int vformat(unsigned char *p, const char *fmt, va_list ap) {
    for (; *fmt; fmt++) {
        if (*fmt != '%') { *p++ = (unsigned char)*fmt; continue; }
        fmt++;
        if (*fmt == 'l') fmt++;               /* %ld -> treated as %d */
        if (*fmt == 'd' || *fmt == 'i') p = outbuf + append_int(p, va_arg(ap, long));
        else if (*fmt == 's')     p = outbuf + append_str(p, va_arg(ap, const char *));
        else if (*fmt == 'c')     { *p++ = (unsigned char)va_arg(ap, int); }
        else { *p++ = '%'; *p++ = (unsigned char)*fmt; }
    }
    *p = 0;
    return (int)(p - outbuf);
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    /* The frozen s1.c uses printf for exactly one purpose: HOST_PRINT
     * ("%ld\n").  We special-case it here so a single large integer (the
     * WEB_SET_INT protocol: handle * 1000000 + value) becomes a host_set_text
     * call, and any ordinary number becomes a plain host_print line. */
    int len = vformat(outbuf, fmt, ap);
    va_end(ap);
    if (len > 0 && outbuf[len - 1] == '\n') {
        /* a lone integer on stdout: parse it back out */
        unsigned char *s = outbuf;
        long v = 0; int neg = 0;
        if (*s == '-') { neg = 1; s++; }
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
        if (neg) v = -v;
        if (*s == '\n' && s == outbuf + len - 1) {
            if (v >= 1000000) {
                host_set_text((unsigned int)(v / 1000000), (unsigned int)(v % 1000000));
                return 0;
            }
        }
    }
    host_print(outbuf, (unsigned int)len);
    return len;
}

int fprintf(FILE *f, const char *fmt, ...) {
    (void)f;
    va_list ap;
    va_start(ap, fmt);
    int len = vformat(outbuf, fmt, ap);
    va_end(ap);
    host_print(outbuf, (unsigned int)len);
    return len;
}

int putchar(int c) {
    outbuf[0] = (unsigned char)c;
    host_print(outbuf, 1);
    return c;
}

int fflush(FILE *f) { (void)f; return 0; }

/* ---- memory -------------------------------------------------------------- */

static unsigned char heap[1 << 16];   /* 64 KiB bump heap */
static unsigned int heap_used = 0;

void *malloc(unsigned int n) {
    unsigned int a = (heap_used + 7u) & ~7u;
    if (a + n > sizeof(heap)) return (void *)0;
    heap_used = a + n;
    return (void *)(heap + a);
}

void free(void *p) { (void)p; }

void *memcpy(void *d, const void *s, unsigned int n) {
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    while (n--) *dd++ = *ss++;
    return d;
}

void *memmove(void *d, const void *s, unsigned int n) {
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    if (dd < ss) { while (n--) *dd++ = *ss++; }
    else { dd += n; ss += n; while (n--) *--dd = *--ss; }
    return d;
}

void *memset(void *d, int c, unsigned int n) {
    unsigned char *dd = (unsigned char *)d;
    while (n--) *dd++ = (unsigned char)c;
    return d;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, unsigned int n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (unsigned char)*a - (unsigned char)*b : 0;
}

unsigned int strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (unsigned int)(p - s);
}

int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
int isdigit(int c) { return c >= '0' && c <= '9'; }

/* ---- GLON runtime ABI ---------------------------------------------------- */

static int inited = 0;
/* GLON source buffer for glon_load().  The shop bundle (demo/shop/bundle.glon,
 * inlined as <script type="application/glon"> in app.html) grows with the
 * application; the G1E bundle is ~4.7 KiB and outgrew the old 4 KiB limit.
 * 16 KiB leaves comfortable headroom while staying well below the 64 KiB bump
 * heap (static buffers live in linear memory, not the bump heap). */
static unsigned char srcbuf[16384];

__attribute__((export_name("glon_alloc")))
int glon_alloc(unsigned int len) { return (int)(intptr_t)malloc(len + 1); }

__attribute__((export_name("glon_init")))
int glon_init(void) {
    if (!inited) {
        cell main_entry = r0_s1_init();
        /* Seed the M1 multitasking environment (same cells the native test
         * driver seeds).  This is host/run-driver setup only; no scheduling
         * policy lives here.  The cells are unused when the loaded program is
         * not a multitasking program. */
        M[M1_MAIN_ENTRY_CELL] = main_entry;
        M[M1_CURSOR] = 0;
        M[M1_STRESS_CNT] = 0;
        for (int i = 0; i < M1_MAX_TASKS; i++)
            M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
        inited = 1;
    }
    return 0;
}

/* strip `;;` line comments (the R0 reader does not know about them) */
static void strip_comments(unsigned char *s) {
    unsigned char *r = s, *w = s;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
}

__attribute__((export_name("glon_load")))
int glon_load(const unsigned char *src, unsigned int len) {
    if (len >= sizeof(srcbuf)) return -1;
    unsigned int i;
    for (i = 0; i < len; i++) srcbuf[i] = src[i];
    srcbuf[len] = 0;
    strip_comments(srcbuf);
    int err = 0;
    cell prog = r0_s1_parse((const char *)srcbuf, &err);
    if (err) return -2;
    /* G1E load-on-demand: glon_load may be called repeatedly (bootstrap, then
     * each demo on first selection). Each call must EXTEND the persistent
     * machine (managed heap + loader heap + global context), not reset it, so
     * use the persistent-run entry point exactly as the route/event bridge does.
     * The parsed block lives in the loader heap and copies everything out of
     * srcbuf, so srcbuf is safe to overwrite on the next load. */
    int N = r0_s1_run_persistent(prog);
    return (N < 0) ? -3 : 0;
}

__attribute__((export_name("glon_call")))
int glon_call(const unsigned char *name, unsigned int len) {
    if (len == 0 || len > 60) return -1;
    unsigned char src[128];
    unsigned int i = 0;
    src[i++] = '['; src[i++] = ' ';
    src[i++] = 'd'; src[i++] = 'o'; src[i++] = ' ';
    unsigned int j;
    for (j = 0; j < len; j++) src[i++] = name[j];
    src[i++] = ' ';
    src[i++] = ']';
    src[i] = 0;
    int err = 0;
    cell b = r0_s1_parse((const char *)src, &err);
    if (err) return -2;
    int N = r0_s1_run(b);
    return (N < 0) ? -3 : 0;
}

/* G1A: route a token into Glon.  The host bridge forwards a browser event
 * (a [data-glon-route] click or the initial location) as a single word token;
 * Glon's `route` block selects the fragment, mutates state, and the renderer
 * assembles the HTML, which is written to the DOM via host_set_html.  JS holds
 * no routing/application logic. */
static unsigned char htmlbuf[16384];

__attribute__((export_name("glon_route")))
int glon_route(const unsigned char *token, unsigned int len) {
    if (len == 0 || len > 63) return -1;
    unsigned char tok[64];
    unsigned int i;
    for (i = 0; i < len; i++) tok[i] = token[i];
    tok[i] = 0;

    int out_len = 0;
    if (r0_s1_g1a_route((const char *)tok, (char *)htmlbuf, (int)sizeof htmlbuf, &out_len) != 0)
        return -2;

    host_set_html(1u, htmlbuf, (unsigned int)out_len);
    return 0;
}

/* G1C: forward a generic application event into Glon.  A [data-glon-event]
 * click carries a single event token; JS forwards it here without knowing what
 * it means.  Glon's `do-event` block interprets the token, mutates persistent
 * state, and re-renders the current view, which is written to the DOM via
 * host_set_html.  JS holds no application semantics. */
__attribute__((export_name("glon_event")))
int glon_event(const unsigned char *token, unsigned int len) {
    if (len == 0 || len > 63) return -1;
    unsigned char tok[64];
    unsigned int i;
    for (i = 0; i < len; i++) tok[i] = token[i];
    tok[i] = 0;

    int out_len = 0;
    if (r0_s1_g1a_event((const char *)tok, (char *)htmlbuf, (int)sizeof htmlbuf, &out_len) != 0)
        return -2;

    host_set_html(1u, htmlbuf, (unsigned int)out_len);
    return 0;
}

/* G1D: forward a generic application event with an opaque VALUE into Glon.
 * A [data-glon-event] click may carry a value (read from a [data-glon-input]
 * element sharing the token name); JS forwards the token and value here
 * without knowing what either means.  Glon interprets both, mutates state, and
 * re-renders the current view.  JS holds no application semantics. */
__attribute__((export_name("glon_event_value")))
int glon_event_value(const unsigned char *token, unsigned int tlen,
                     const unsigned char *value, unsigned int vlen) {
    if (tlen == 0 || tlen > 63) return -1;
    if (vlen > 200) return -1;
    unsigned char tok[64];
    unsigned char val[201];
    unsigned int i;
    for (i = 0; i < tlen; i++) tok[i] = token[i];
    tok[i] = 0;
    for (i = 0; i < vlen; i++) val[i] = value[i];
    val[i] = 0;

    int out_len = 0;
    if (r0_s1_g1a_event_value((const char *)tok, (const char *)val,
                              (char *)htmlbuf, (int)sizeof htmlbuf, &out_len) != 0)
        return -2;

    host_set_html(1u, htmlbuf, (unsigned int)out_len);
    return 0;
}

/* Emscripten standalone-CRT stack shims: exported by the CRT even under
 * -nostdlib, but never called by our handwritten host.  No-op is fine. */
void *emscripten_stack_get_current(void) { return (void *)0; }
void _emscripten_stack_restore(void *p) { (void)p; }
