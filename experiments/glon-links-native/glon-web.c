/* experiments/glon-links-native/glon-web.c -- a native Glon host with a
 * host-backed DOM: fetch a URL (libcurl), parse it (libxml2's HTML parser), and
 * let a Glon program walk the parsed document through lightweight handles.
 *
 *   glon-web [--raw | --check] [--collect] [--measure] PROGRAM... URL
 *   glon-web --materialise PROGRAM URL
 *
 * It embeds the UNCHANGED runtime and the GLON_LIVE dispatcher
 * (r0_s1_g1a_live.c, the layer behind glon-live.wasm): Glon asks for something
 * by ending a dispatch with a host-call request, and this host answers with the
 * next event (r0_s1_g1a_live_event_bytes). No S1, runtime or parser change.
 *
 * WHAT RUNS WHERE
 *   host (this file)  HTTP/HTTPS/file fetch; HTML parsing (libxml2); keeping the
 *                     document; a cursor over it; encoding the next events in
 *                     the batch format of ../glon-dom-host/hostwalk.glon,
 *                     reporting for EVERY element the attributes Glon named;
 *                     freeing the document when Glon releases it.
 *   Glon (PROGRAM)    decoding the events (hw/), the it/ iterator protocol,
 *                     deciding which elements are links and what their text is
 *                     (lk/event), building and printing the results.
 *
 * HANDLES. Glon only ever holds a document handle, an integer slot*256+gen.
 * Node pointers never leave this file. A handle is valid from web-loaded until
 * Glon sends dom-release; the slot's generation then changes, so a stale or
 * forged handle is rejected (web-error), never dereferenced.
 *
 * --raw      print each link exactly, as netstrings (for checking)
 * --check    --raw, then compare Glon's links with this host's own reference
 *            list ([href, text content] of every <a href>, from libxml2) and
 *            print PASS or FAIL. The reference is never shown to Glon.
 * --collect  Glon keeps the links in a block and prints them at the end
 * --measure  force a collection after every dispatch and report Glon's live
 *            managed heap, return stack, loader growth, time and host memory
 * --materialise  for comparison: copy the WHOLE document into Glon as
 *            [ name attrs children ] blocks (Glon source), then run lk/extract
 *            with dom/walk (the DOM v001 / glon-dom-links approach)
 *
 * dom.glon (DOM v001) is loaded for it/close, the iterator protocol's
 * subtree-skip word, which it defines, and for --materialise's dom/walk.
 *
 * Libraries are loaded from glon-web-lib/ beside the executable (the Makefile
 * copies them there), then each PROGRAM file in order (a later file may redefine
 * words of an earlier one, as the tests do).
 */
#define _GNU_SOURCE
#include "r0_s1.h"
#include "r0_s1_g1a_live.h"
#include "m1_layout.h"
#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int opt_raw, opt_check, opt_collect, opt_measure, opt_mat;

/* ---- timing and host memory accounting ------------------------------------- */
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static double t_fetch, t_parse, t_encode, t_glon;

/* libxml2's allocations are counted through xmlMemSetup: the size of a parsed
 * document is the memory libxml2 holds for it (current and peak bytes) */
static long xml_cur, xml_peak;
typedef struct { size_t n; size_t pad; } Hdr;
static void *xm_malloc(size_t n) {
    Hdr *h = malloc(sizeof(Hdr) + n); if (!h) return NULL;
    h->n = n; xml_cur += (long)n; if (xml_cur > xml_peak) xml_peak = xml_cur; return h + 1;
}
static void xm_free(void *p) { if (!p) return; Hdr *h = (Hdr *)p - 1; xml_cur -= (long)h->n; free(h); }
static void *xm_realloc(void *p, size_t n) {
    if (!p) return xm_malloc(n);
    Hdr *h = (Hdr *)p - 1; size_t old = h->n;
    Hdr *g = realloc(h, sizeof(Hdr) + n); if (!g) return NULL;
    g->n = n; xml_cur += (long)n - (long)old; if (xml_cur > xml_peak) xml_peak = xml_cur; return g + 1;
}
static char *xm_strdup(const char *s) { size_t n = strlen(s) + 1; char *d = xm_malloc(n); if (d) memcpy(d, s, n); return d; }

/* ---- fetching --------------------------------------------------------------- */
#define MAX_PAGE (16L << 20)
typedef struct { char *p; size_t n, cap; } Buf;
static size_t on_data(char *d, size_t sz, size_t nm, void *u) {
    Buf *b = u; size_t n = sz * nm;
    if (b->n + n > (size_t)MAX_PAGE) return 0;              /* too large: abort */
    if (b->n + n + 1 > b->cap) { b->cap = (b->n + n + 1) * 2; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, d, n); b->n += n; b->p[b->n] = 0; return n;
}
static long fetched_bytes;
/* 0, or an error message in err */
static int fetch(const char *url, Buf *b, char *err, size_t errcap) {
    CURL *c = curl_easy_init(); if (!c) { snprintf(err, errcap, "curl init failed"); return -1; }
    char cerr[CURL_ERROR_SIZE] = "";
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS | CURLPROTO_FILE));
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));   /* never redirect to file: */
    curl_easy_setopt(c, CURLOPT_USERAGENT, "glon-web/0.1 (experiment)");
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, cerr);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, b);
    CURLcode rc = curl_easy_perform(c);
    long status = 0; curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK) { snprintf(err, errcap, "%s", cerr[0] ? cerr : curl_easy_strerror(rc)); return -1; }
    if (status >= 400) { snprintf(err, errcap, "HTTP %ld", status); return -1; }
    fetched_bytes = (long)b->n;
    return 0;
}

/* ---- documents and handles -------------------------------------------------- */
typedef struct { xmlNodePtr n; xmlNodePtr next; } Frame;   /* next: the next child to visit */
typedef struct {
    int live, gen;
    htmlDocPtr doc;
    Frame *stack; int sp, cap;
    int started, ended;
    unsigned char *pend; int pend_n, pend_cap;    /* records not yet sent */
} Doc;
#define SLOTS 8
static Doc docs[SLOTS];
static long max_stack, max_docs, live_docs, handles_rejected;

static Doc *lookup(int h) {                        /* NULL for a stale or forged handle */
    int slot = h / 256, gen = h % 256;
    if (h < 256 || slot >= SLOTS || !docs[slot].live || docs[slot].gen != gen) { handles_rejected++; return NULL; }
    return &docs[slot];
}
static int adopt(htmlDocPtr doc) {
    for (int s = 1; s < SLOTS; s++) if (!docs[s].live) {
        Doc *d = &docs[s]; int gen = (d->gen + 1) % 256; if (gen == 0) gen = 1;
        memset(d, 0, sizeof *d); d->gen = gen; d->live = 1; d->doc = doc;
        live_docs++; if (live_docs > max_docs) max_docs = live_docs;
        return s * 256 + gen;
    }
    return -1;
}
static void release(Doc *d) {
    xmlFreeDoc(d->doc); free(d->stack); free(d->pend);
    int gen = d->gen; memset(d, 0, sizeof *d); d->gen = gen;    /* keep gen: the old handle stays stale */
    live_docs--;
}

/* ---- the cursor and the batch encoder (the format of hostwalk.glon) ---------- */
static int relevant(xmlNodePtr n) {
    return n->type == XML_ELEMENT_NODE || n->type == XML_TEXT_NODE || n->type == XML_CDATA_SECTION_NODE;
}
static xmlNodePtr first_relevant(xmlNodePtr n) { while (n && !relevant(n)) n = n->next; return n; }

static unsigned char rec[1 << 17]; static int rn;
static void r8(int v) { rec[rn++] = (unsigned char)v; }
static void r16(int v) { r8((v >> 8) & 255); r8(v & 255); }
static void rbytes(const void *p, int n) { memcpy(rec + rn, p, (size_t)n); rn += n; }
static void pend(Doc *d) {                         /* queue rec[0..rn) as one record */
    if (d->pend_n + rn + 2 > d->pend_cap) { d->pend_cap = (d->pend_n + rn + 2) * 2; d->pend = realloc(d->pend, (size_t)d->pend_cap); }
    d->pend[d->pend_n++] = (unsigned char)(rn >> 8); d->pend[d->pend_n++] = (unsigned char)rn;   /* length prefix (host-side only) */
    memcpy(d->pend + d->pend_n, rec, (size_t)rn); d->pend_n += rn; rn = 0;
}
static char names[16][64]; static int nnames;

static void visit(Doc *d, xmlNodePtr n, int maxbytes) {
    int depth = d->sp;
    if (n->type != XML_ELEMENT_NODE) {            /* text: split to fit a batch, never inside a UTF-8 sequence */
        const unsigned char *t = n->content ? n->content : (const unsigned char *)"";
        int len = (int)strlen((const char *)t), room = maxbytes - 5 > 1 ? maxbytes - 5 : 1, off = 0;
        do {
            int end = off + room < len ? off + room : len;
            while (end < len && end > off + 1 && (t[end] & 0xC0) == 0x80) end--;
            rn = 0; r8(4); r16(depth); r16(end - off); rbytes(t + off, end - off); pend(d);
            off = end;
        } while (off < len);
        return;
    }
    xmlNodePtr k = first_relevant(n->children);
    int nl = (int)strlen((const char *)n->name); if (nl > 255) nl = 255;
    rn = 0; r8(k ? 1 : 3); r16(depth); r8(nl); rbytes(n->name, nl);
    for (int a = 0; a < nnames; a++) {
        xmlChar *v = xmlGetProp(n, (const xmlChar *)names[a]);
        if (!v) { r16(65535); continue; }
        int vl = (int)strlen((const char *)v); if (vl > 65534) vl = 65534;
        if (vl > (int)sizeof rec - rn - 16) vl = (int)sizeof rec - rn - 16;
        r16(vl); rbytes(v, vl); xmlFree(v);
    }
    pend(d);
    if (k) {
        if (d->sp == d->cap) { d->cap = d->cap ? d->cap * 2 : 32; d->stack = realloc(d->stack, sizeof(Frame) * (size_t)d->cap); }
        d->stack[d->sp].n = n; d->stack[d->sp].next = k; d->sp++;
        if (d->sp > max_stack) max_stack = d->sp;
    }
}
static void step(Doc *d, int maxbytes) {
    if (!d->started) { d->started = 1; visit(d, xmlDocGetRootElement(d->doc), maxbytes); return; }
    if (d->sp == 0) { rn = 0; r8(5); r16(0); pend(d); d->ended = 1; return; }
    Frame *t = &d->stack[d->sp - 1];
    if (t->next) { xmlNodePtr c = t->next; t->next = first_relevant(c->next); visit(d, c, maxbytes); return; }
    d->sp--; rn = 0; r8(2); r16(d->sp); pend(d);
}
static void close_at(Doc *d, int depth) {          /* it/close: finish the open element at depth */
    d->pend_n = 0;
    while (d->sp - 1 > depth) d->sp--;
    if (d->sp > 0) d->stack[d->sp - 1].next = NULL;
}
static unsigned char batch[1 << 17]; static int blen;
static long n_events, n_batches, n_bytes;
static void make_batch(Doc *d, int max, int maxbytes) {
    double t0 = now(); blen = 0; int count = 0, off = 0;
    while (count < max) {
        if (off >= d->pend_n) {
            d->pend_n = 0; off = 0;
            if (d->ended) break;
            step(d, maxbytes); continue;
        }
        int len = (d->pend[off] << 8) | d->pend[off + 1];
        if (count > 0 && blen + len > maxbytes) break;       /* at least one event */
        memcpy(batch + blen, d->pend + off + 2, (size_t)len); blen += len; off += len + 2; count++;
    }
    if (off > 0) { memmove(d->pend, d->pend + off, (size_t)(d->pend_n - off)); d->pend_n -= off; }
    n_events += count; n_batches++; n_bytes += blen; t_encode += now() - t0;
}

/* ---- the reference list (for --check only; Glon never sees it) ---------------- */
typedef struct { char *href, *text; } Link;
static Link refs[100000]; static int nref;
static void collect_refs(xmlNodePtr n) {
    for (; n; n = n->next) {
        if (n->type != XML_ELEMENT_NODE) continue;
        if (!xmlStrcasecmp(n->name, (const xmlChar *)"a")) {
            xmlChar *h = xmlGetProp(n, (const xmlChar *)"href");
            if (h && nref < 100000) {
                xmlChar *t = xmlNodeGetContent(n);
                refs[nref].href = strdup((char *)h); refs[nref].text = strdup(t ? (char *)t : ""); nref++;
                if (t) xmlFree(t);
            }
            if (h) xmlFree(h);
        }
        collect_refs(n->children);
    }
}

/* ---- Glon ------------------------------------------------------------------------ */
static char exedir[4096];
static int load_src(const char *src0) {
    static char wrap[1 << 21];
    const char *src = src0, *p = src0;
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    if (*p != '[') { snprintf(wrap, sizeof wrap, "[ %s ]", src0); src = wrap; }
    int err = 0; cell b = r0_s1_parse(src, &err);
    if (err) return -2;
    r0_s1_run_persistent(b);
    return r0_s1_ran_cleanly() ? 0 : -3;
}
static char budget_txt[4096]; static int budget_n;
static int load_file_raw(const char *path);
/* load a file, noting its bindings, loader cells and code cells (for --measure) */
static int load_file(const char *path) {
    cell c1, c2, cap, l1 = M[GC_LOADER_HP], h1 = asm_here();
    r0_s1_global_usage(&c1, &cap);
    int rc = load_file_raw(path);
    r0_s1_global_usage(&c2, &cap);
    const char *base = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    budget_n += snprintf(budget_txt + budget_n, sizeof budget_txt - (size_t)budget_n,
                         "#   %-18s +%3ld bindings +%5ld loader cells +%4ld code cells\n",
                         base, (long)(c2 - c1), (long)(M[GC_LOADER_HP] - l1), (long)(asm_here() - h1));
    return rc;
}
static int load_file_raw(const char *path) {
    static char fb[1 << 20];
    FILE *f = fopen(path, "rb"); if (!f) { fprintf(stderr, "glon-web: cannot open %s\n", path); return -1; }
    size_t n = fread(fb, 1, sizeof fb - 1, f); fclose(f); fb[n] = 0;
    char *r = fb, *w = fb;
    while (*r) { if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; } *w++ = *r++; }
    *w = 0;
    int rc = load_src(fb);
    if (rc) fprintf(stderr, "glon-web: %s failed to load (%d)\n", path, rc);
    return rc;
}
static const char *libs[] = { "prelude.glon", "emit.glon", "strings.glon", "strings-ext.glon", "iter.glon",
                              "dom.glon", "list-block.glon", "links.glon", "hostwalk.glon", NULL };

static char op[256], arg[8192]; static int have_call;
static void on_host_call(const char *o, int ol, const char *a, int al, void *u) {
    (void)u; if (ol > 255) ol = 255; if (al > 8191) al = 8191;
    memcpy(op, o, (size_t)ol); op[ol] = 0; memcpy(arg, a, (size_t)al); arg[al] = 0; have_call = 1;
}
static long max_live, max_rs, dispatches;
static long glon_live(void) {                      /* a forced collection (its parse is given back) */
    cell hp = M[GC_LOADER_HP]; load_src("m-gc 0"); M[GC_LOADER_HP] = hp; return r0_s1_gc_live_cells();
}
static int dispatch(const char *tok, const void *data, int n) {
    char out[256]; int out_len = 0; have_call = 0;
    double t0 = now();
    int rc = r0_s1_g1a_live_event_bytes(tok, (const unsigned char *)data, (unsigned)n, out, (int)sizeof out, &out_len);
    t_glon += now() - t0; dispatches++;
    long rs = 24576 - (long)r0_s1_rp_min(); if (rs > max_rs) max_rs = rs;
    if (opt_measure) { long lv = glon_live(); if (lv > max_live) max_live = lv; }
    return rc;
}
static int reply_error(const char *msg) { return dispatch("web-error", msg, (int)strlen(msg)); }

/* --materialise: the document as Glon source, [ name attrs children ] blocks */
static char *ms; static size_t msn, mscap; static int ms_too_big;
static void mput(const char *s, size_t n) {
    if (msn + n + 1 > mscap) { mscap = (msn + n + 1) * 2; ms = realloc(ms, mscap); }
    memcpy(ms + msn, s, n); msn += n; ms[msn] = 0;
}
static void mstr(const char *s) { mput(s, strlen(s)); }
static void mbytes(const unsigned char *t, int len) {   /* a string as mk-string [ bytes ], 500 per literal */
    if (len == 0) { mstr("\"\""); return; }
    int first = 1;
    for (int off = 0; off < len; off += 500) {
        mstr(first ? " mk-string [" : " s/+ mk-string ["); first = 0;   /* (joined right to left) */
        char num[8];
        for (int i = off; i < len && i < off + 500; i++) { snprintf(num, sizeof num, " %d", t[i]); mstr(num); }
        mstr(" ]");
    }
}
static void mnode(xmlNodePtr n) {
    if (n->type != XML_ELEMENT_NODE) { const unsigned char *t = n->content ? n->content : (const unsigned char *)""; mbytes(t, (int)strlen((const char *)t)); return; }
    mstr(" reduce ["); mbytes(n->name, (int)strlen((const char *)n->name));
    xmlChar *h = xmlGetProp(n, (const xmlChar *)"href");
    if (h) { mstr(" reduce [ \"href\""); mbytes(h, (int)strlen((char *)h)); mstr(" ]"); xmlFree(h); } else mstr(" none");
    xmlNodePtr k = first_relevant(n->children);
    if (!k) mstr(" none"); else {
        int count = 0; mstr(" reduce [");
        for (; k; k = first_relevant(k->next)) { mnode(k); count++; }
        if (count > 250) ms_too_big = 1;                  /* a block holds at most 512 values */
        mstr(" ]");
    }
    mstr(" ]");
}

int main(int argc, char **argv) {
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; i++) {
        if (!strcmp(argv[i], "--raw")) opt_raw = 1;
        else if (!strcmp(argv[i], "--check")) opt_check = opt_raw = 1;
        else if (!strcmp(argv[i], "--collect")) opt_collect = 1;
        else if (!strcmp(argv[i], "--measure")) opt_measure = 1;
        else if (!strcmp(argv[i], "--materialise")) opt_mat = opt_measure = 1;
        else { fprintf(stderr, "glon-web: unknown option %s\n", argv[i]); return 2; }
    }
    if (argc - i < 2) {
        fprintf(stderr, "usage: glon-web [--raw|--check] [--collect] [--measure] PROGRAM... URL\n"
                        "       glon-web --materialise PROGRAM URL\n");
        return 2;
    }
    const char *url = argv[argc - 1]; int first_prog = i, last_prog = argc - 2;
    xmlMemSetup(xm_free, xm_malloc, xm_realloc, xm_strdup);
    xmlInitParser(); curl_global_init(CURL_GLOBAL_DEFAULT);
    { char tmp[4096]; ssize_t n = readlink("/proc/self/exe", tmp, sizeof tmp - 1); tmp[n > 0 ? n : 0] = 0;
      snprintf(exedir, sizeof exedir, "%s", dirname(tmp)); }

    cell me = r0_s1_init(); M[M1_MAIN_ENTRY_CELL] = me; M[M1_CURSOR] = 0; M[M1_STRESS_CNT] = 0;
    for (int t = 0; t < M1_MAX_TASKS; t++) M[M1_TASK_TABLE + t * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
    for (int l = 0; libs[l]; l++) {
        char p[4200]; snprintf(p, sizeof p, "%s/glon-web-lib/%s", exedir, libs[l]);
        if (load_file(p)) return 2;
    }
    if (load_src("[ m-gc: masm 1 [ DROP CALL collect LIT 0 ARITY 1 EXIT ] ]")) return 2;
    for (int f = first_prog; f <= last_prog; f++) if (load_file(argv[f])) return 1;
    if (opt_collect && load_src("web/collect: 1")) return 1;
    if (opt_raw && load_src("web/raw: 1")) return 1;
    cell used, cap; r0_s1_global_usage(&used, &cap);
    long base_live = glon_live(), loader0 = (long)M[GC_LOADER_HP], gc0 = r0_s1_gc_count();
    r0_s1_g1a_live_set_host_call(on_host_call, 0);
    fflush(stdout);
    double t_start = now();

    if (opt_mat) {     /* ---- comparison: the whole document copied into Glon ---- */
        Buf b = {0}; char err[CURL_ERROR_SIZE + 64];
        double t0 = now(); if (fetch(url, &b, err, sizeof err)) { printf("error: %s\n", err); return 1; } t_fetch = now() - t0;
        t0 = now(); htmlDocPtr doc = htmlReadMemory(b.p, (int)b.n, url, NULL, HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET);
        t_parse = now() - t0; free(b.p);
        if (!doc || !xmlDocGetRootElement(doc)) { printf("error: not HTML\n"); return 1; }
        mstr("page:"); mnode(xmlDocGetRootElement(doc));
        long src_bytes = (long)msn;
        if (ms_too_big) { printf("# materialise: an element has over 250 children: its block exceeds the parser's 512 values\n"); }
        long l0 = (long)M[GC_LOADER_HP]; t0 = now();
        int rc = ms_too_big ? -9 : load_src(ms);
        long loader = (long)M[GC_LOADER_HP] - l0;
        long live_page = rc == 0 ? glon_live() : -1;
        int rc2 = rc == 0 ? load_src("mat/links: lk/extract page  print block-len mat/links") : -1;
        double t_g = now() - t0;
        long live_res = rc2 == 0 ? glon_live() : -1;
        fflush(stdout);
        if (rc == 0 && rc2 == 0)
            printf("# materialise: source %ld bytes; load ok; extract ok; loader cells %ld; managed live: base %ld, "
                   "with page %ld, with page and links %ld; Glon %.0f ms; libxml2 peak %ld bytes\n",
                   src_bytes, loader, base_live, live_page, live_res, t_g * 1000, xml_peak);
        else
            printf("# materialise: source %ld bytes; %s; libxml2 peak %ld bytes\n", src_bytes,
                   rc == -9 ? "load refused (a block would be too large)" : rc ? "load FAILED (loader or managed heap exhausted)"
                                                                         : "extract FAILED (managed heap exhausted)", xml_peak);
        xmlFreeDoc(doc);
        return rc == 0 && rc2 == 0 ? 0 : 1;
    }

    /* capture Glon's output if it must be checked */
    FILE *cap_f = NULL; int saved = -1;
    if (opt_check) { cap_f = tmpfile(); saved = dup(1); dup2(fileno(cap_f), 1); }

    int rc = dispatch("start", url, (int)strlen(url));
    while (rc == 0 && have_call) {
        if (!strcmp(op, "web-load")) {
            Buf b = {0}; char err[CURL_ERROR_SIZE + 64];
            double t0 = now(); int f = fetch(arg, &b, err, sizeof err); t_fetch += now() - t0;
            if (f) { free(b.p); rc = reply_error(err); continue; }
            t0 = now();
            htmlDocPtr doc = htmlReadMemory(b.p, (int)b.n, arg, NULL,
                                            HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET);
            t_parse += now() - t0; free(b.p);
            if (!doc || !xmlDocGetRootElement(doc)) { if (doc) xmlFreeDoc(doc); rc = reply_error("the page is not HTML"); continue; }
            if (opt_check) { nref = 0; collect_refs(xmlDocGetRootElement(doc)); }
            int h = adopt(doc);
            if (h < 0) { xmlFreeDoc(doc); rc = reply_error("too many open documents"); continue; }
            char hs[16]; snprintf(hs, sizeof hs, "%d", h);
            rc = dispatch("web-loaded", hs, (int)strlen(hs));
        } else if (!strcmp(op, "dom-next")) {
            int h = 0, max = 0, cd = -1, mb = 0; char nm[1024] = "";
            if (sscanf(arg, "%d %d %d %1023s %d", &h, &max, &cd, nm, &mb) != 5 || max <= 0 || mb <= 0) { rc = reply_error("bad dom-next request"); continue; }
            Doc *d = lookup(h);
            if (!d) { char m[64]; snprintf(m, sizeof m, "stale or unknown handle %d", h); rc = reply_error(m); continue; }
            nnames = 0;
            for (char *tok = strtok(nm, ","); tok && nnames < 16; tok = strtok(NULL, ",")) snprintf(names[nnames++], 64, "%s", tok);
            if (cd >= 0) close_at(d, cd);
            make_batch(d, max, mb);
            rc = dispatch("dom-events", batch, blen);
        } else if (!strcmp(op, "dom-release")) {
            Doc *d = lookup(atoi(arg));
            if (!d) { char m[64]; snprintf(m, sizeof m, "stale or unknown handle %.32s", arg); rc = reply_error(m); continue; }
            release(d);
            /* tell Glon; a program with more to do answers with its next request
             * (links.glon ignores the event, so its conversation ends here) */
            rc = dispatch("dom-released", arg, (int)strlen(arg));
        } else {
            char m[300]; snprintf(m, sizeof m, "unknown request %s", op); rc = reply_error(m);
        }
    }
    double t_total = now() - t_start;
    int leaked = 0;
    for (int s = 0; s < SLOTS; s++) if (docs[s].live) { leaked++; release(&docs[s]); }

    int pass = 1;
    if (opt_check) {
        fflush(stdout); dup2(saved, 1); close(saved);
        rewind(cap_f); static char o[1 << 24]; size_t on = fread(o, 1, sizeof o - 1, cap_f); o[on] = 0;
        fputs(o, stdout);
        /* Glon's netstrings: href, text, href, text ... then "# n links" */
        char *p = o; int k = 0, bad = 0;
        while (*p >= '0' && *p <= '9') {
            char *f[2]; int fl[2];
            for (int j = 0; j < 2; j++) { fl[j] = atoi(p); f[j] = strchr(p, ':') + 1; p = f[j] + fl[j] + 1; }
            if (k >= nref || (int)strlen(refs[k].href) != fl[0] || memcmp(refs[k].href, f[0], (size_t)fl[0])
                || (int)strlen(refs[k].text) != fl[1] || memcmp(refs[k].text, f[1], (size_t)fl[1])) {
                if (bad++ < 5) printf("MISMATCH at link %d\n", k + 1);
            }
            k++;
        }
        pass = rc == 0 && !bad && k == nref;
        printf("%s: Glon printed %d links; libxml2's own list has %d\n", pass ? "PASS" : "FAIL", k, nref);
    }
    if (rc != 0) printf("glon-web: a Glon dispatch failed (%d)\n", rc);
    if (opt_measure) {
        printf("# measure: files loaded:\n%s", budget_txt);
        printf("# measure: session: loader %ld used, %ld free; code ends at %ld\n",
               (long)(loader0 - R0S1_HEAP_BASE), (long)(R0S1_HEAP_LIMIT - loader0), (long)asm_here());
        printf("# measure: events %ld, batches %ld, batch bytes %ld, dispatches %ld\n", n_events, n_batches, n_bytes, dispatches);
        printf("# measure: Glon bindings %ld/%ld; managed live: base %ld, peak %ld (+%ld), after %ld; collections %ld;"
               " return stack peak %ld cells; loader growth %ld cells\n",
               (long)used, (long)cap, base_live, max_live, max_live - base_live, glon_live(), r0_s1_gc_count() - gc0,
               max_rs, (long)M[GC_LOADER_HP] - loader0);
        printf("# measure: time: fetch %.1f ms, parse %.1f ms, host encoding %.1f ms, Glon %.1f ms (%.0f us/event), total %.1f ms\n",
               t_fetch * 1000, t_parse * 1000, t_encode * 1000, t_glon * 1000, n_events ? t_glon * 1e6 / n_events : 0, t_total * 1000);
        printf("# measure: host memory: page %ld bytes fetched; libxml2 peak %ld bytes, now %ld; cursor stack peak %ld frames"
               " (%ld bytes); documents open at most %ld, leaked %d; handles rejected %ld\n",
               fetched_bytes, xml_peak, xml_cur, max_stack, max_stack * (long)sizeof(Frame), max_docs, leaked, handles_rejected);
    }
    xmlCleanupParser(); curl_global_cleanup();
    return rc == 0 && pass && !leaked ? 0 : 1;
}
