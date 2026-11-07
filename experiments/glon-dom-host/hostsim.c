/* experiments/glon-dom-host/hostsim.c -- native harness for the host-backed DOM
 * walk. It links the UNCHANGED runtime and GLON_LIVE dispatcher
 * (r0_s1_g1a_live.c, the layer behind glon-live.wasm's host_call and
 * glon_event_bytes) and plays the HOST in C, over a synthetic DOM it owns.
 *
 * The C host implements exactly the protocol and batch encoding of host.js
 * (see hostwalk.glon). It exists so that Glon's memory can be MEASURED (the
 * WASM build does not export M), on documents of any size, and so the
 * conversation can be tested without a browser.
 *
 *   hostsim [-q] BLOCKS LINKS DEPTH MAX   one document: BLOCKS content blocks
 *        (8 nodes each), LINKS of them holding <a href="/link/i">Link <b>i</b></a>,
 *        one chain DEPTH elements deep, MAX events per batch
 *   hostsim budget                        bindings / loader / code per file
 *
 * Prints one summary line ("RESULT ...") and checks the links Glon printed
 * against the C host's own [href textContent] list.
 */
#include "r0_s1.h"
#include "r0_s1_g1a_live.h"
#include "m1_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---- the synthetic DOM (owned by the "host") ------------------------------ */
typedef struct Node {
    int text;                 /* 1: a text node */
    const char *name;         /* element name, or the text */
    const char *href;         /* the href attribute, or NULL */
    struct Node **kids; int nkids, cap;
} Node;
static long nodes_made = 0;
static Node *mk(int text, const char *name, const char *href) {
    Node *n = calloc(1, sizeof *n); n->text = text; n->name = name; n->href = href; nodes_made++; return n;
}
static void add(Node *p, Node *c) {
    if (p->nkids == p->cap) { p->cap = p->cap ? p->cap * 2 : 4; p->kids = realloc(p->kids, sizeof(Node *) * p->cap); }
    p->kids[p->nkids++] = c;
}
static char *str(const char *s) { char *d = malloc(strlen(s) + 1); strcpy(d, s); return d; }

/* the expected links, in document order */
static char exp_href[2000][32], exp_text[2000][32]; static int nexp = 0;

static Node *document(int blocks, int links, int depth) {
    Node *html = mk(0, "html", NULL), *body = mk(0, "body", NULL);
    add(html, body);
    Node *h1 = mk(0, "h1", NULL); add(h1, mk(1, "Synthetic page", NULL)); add(body, h1);
    for (int i = 0; i < blocks; i++) {
        /* <div><p>Lorem ipsum <em>dolor</em> sit</p><span>x</span></div>, or with a link */
        Node *div = mk(0, "div", NULL), *p = mk(0, "p", NULL), *em = mk(0, "em", NULL);
        add(p, mk(1, "Lorem ipsum ", NULL)); add(em, mk(1, "dolor", NULL)); add(p, em); add(p, mk(1, " sit", NULL));
        add(div, p);
        int is_link = nexp < links && i >= (int)((long)nexp * blocks / links);
        if (is_link) {
            char h[32], t[32], num[16];
            snprintf(h, sizeof h, "/link/%d", nexp); snprintf(num, sizeof num, "%d", nexp);
            snprintf(t, sizeof t, "Link %d", nexp);
            Node *a = mk(0, "a", str(h)), *b = mk(0, "b", NULL);
            add(a, mk(1, "Link ", NULL)); add(b, mk(1, str(num), NULL)); add(a, b); add(div, a);
            strcpy(exp_href[nexp], h); strcpy(exp_text[nexp], t); nexp++;
        } else {
            Node *span = mk(0, "span", NULL); add(span, mk(1, "x", NULL)); add(div, span);
        }
        add(body, div);
    }
    /* one deep chain: <div><div>...<i>deep</i>...</div></div> */
    Node *cur = body;
    for (int d = 0; d < depth; d++) { Node *c = mk(0, "div", NULL); add(cur, c); cur = c; }
    if (depth > 0) add(cur, mk(1, "deep", NULL));
    return html;
}

/* ---- the host cursor and the batch encoder (as host.js) ----------------------- */
typedef struct { Node *n; int next; } Frame;
static Frame stack[4096]; static int sp = 0, started = 0, ended = 0; static Node *root;
static unsigned char batch[16384]; static int blen;
static long host_events = 0, host_batches = 0, host_bytes = 0, max_stack = 0;

static void put8(int v) { batch[blen++] = (unsigned char)v; }
static void put16(int v) { put8((v >> 8) & 255); put8(v & 255); }
static void putbytes(const char *s, int n) { memcpy(batch + blen, s, (size_t)n); blen += n; }

static void visit(Node *n) {
    int depth = sp;
    if (n->text) {
        int len = (int)strlen(n->name);
        put8(4); put16(depth); put16(len); putbytes(n->name, len);
    } else {
        int has = n->nkids > 0;
        put8(has ? 1 : 3); put16(depth);
        put8((int)strlen(n->name)); putbytes(n->name, (int)strlen(n->name));
        /* the one requested attribute, href (the request names it) */
        if (n->href) { put16((int)strlen(n->href)); putbytes(n->href, (int)strlen(n->href)); }
        else put16(65535);
        if (has) { stack[sp].n = n; stack[sp].next = 0; sp++; if (sp > max_stack) max_stack = sp; }
    }
}
static void step(void) {           /* encode the next event */
    if (!started) { started = 1; visit(root); return; }
    if (sp == 0) { put8(5); put16(0); ended = 1; return; }
    Frame *t = &stack[sp - 1];
    if (t->next < t->n->nkids) { visit(t->n->kids[t->next++]); return; }
    sp--; put8(2); put16(sp);
}
static void make_batch(int max, int close_depth, int maxbytes) {
    blen = 0;
    if (close_depth >= 0) {        /* it/close: pop to the element at close_depth, finish it */
        while (sp - 1 > close_depth) sp--;
        if (sp > 0) stack[sp - 1].next = stack[sp - 1].n->nkids;
    }
    /* at least one event; then stop before the budget could be passed (the
     * largest record here is under 64 bytes) */
    for (int k = 0; k < max && !ended && (k == 0 || blen + 64 <= maxbytes); k++) { step(); host_events++; }
    host_batches++; host_bytes += blen;
}

/* ---- the Glon side ------------------------------------------------------------- */
static char filebuf[65536], wrapbuf[65600], outbuf[65536];
static int load_src(const char *src0) {
    const char *src = src0; const char *p = src0;
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    if (*p != '[') { snprintf(wrapbuf, sizeof wrapbuf, "[ %s ]", src0); src = wrapbuf; }
    int err = 0; cell b = r0_s1_parse(src, &err);
    if (err) return -2;
    r0_s1_run_persistent(b);
    return r0_s1_ran_cleanly() ? 0 : -3;
}
static int load_file(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) { fprintf(stderr, "cannot open %s\n", path); return -1; }
    size_t n = fread(filebuf, 1, sizeof filebuf - 1, f); fclose(f); filebuf[n] = 0;
    char *r = filebuf, *w = filebuf;
    while (*r) { if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; } *w++ = *r++; }
    *w = 0;
    return load_src(filebuf);
}
static const char *libs[] = {
    "../../jupyter/prelude.glon", "emit.glon", "../../demo/shop/strings.glon", "../../demo/shop/strings-ext.glon",
    "../glon-iter/iter.glon", "../glon-dom-links/list-block.glon", "../glon-dom-links/links.glon",
    "hostwalk.glon", "app.glon", NULL };

static char op[256], arg[8192]; static int have_call = 0;
static void on_host_call(const char *o, int ol, const char *a, int al, void *u) {
    (void)u; memcpy(op, o, (size_t)ol); op[ol] = 0; memcpy(arg, a, (size_t)al); arg[al] = 0; have_call = 1;
}

/* captured Glon output: the runtime prints with putchar/printf to stdout, so the
 * harness reads it back from a temporary file */
/* a forced collection; the loader heap used to parse "m-gc 0" is given back, as
 * the live dispatcher gives back its wrapper's, so it does not count as growth */
static long gc_live(void) {
    cell hp = M[GC_LOADER_HP]; load_src("m-gc 0"); M[GC_LOADER_HP] = hp;
    return r0_s1_gc_live_cells();
}

static int dispatch(const char *tok, const unsigned char *data, int n) {
    int out_len = 0; have_call = 0;
    return r0_s1_g1a_live_event_bytes(tok, data, (unsigned)n, outbuf, (int)sizeof outbuf, &out_len);
}

int main(int argc, char **argv) {
    int quiet = argc > 1 && !strcmp(argv[1], "-q"); if (quiet) { argv++; argc--; }
    int budget = argc > 1 && !strcmp(argv[1], "budget");
    if (!budget && argc < 5) { fprintf(stderr, "usage: hostsim [-q] BLOCKS LINKS DEPTH MAX | budget\n"); return 2; }

    cell me = r0_s1_init(); M[M1_MAIN_ENTRY_CELL] = me; M[M1_CURSOR] = 0; M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++) M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
    for (int i = 0; libs[i]; i++) {
        cell c1, c2, cap, l1 = M[GC_LOADER_HP], h1 = asm_here();
        r0_s1_global_usage(&c1, &cap);
        if (load_file(libs[i]) != 0) { printf("load failed: %s\n", libs[i]); return 1; }
        r0_s1_global_usage(&c2, &cap);
        if (budget) printf("  %-18s +%3ld bindings  +%5ld loader cells  +%4ld code cells\n",
                           strrchr(libs[i], '/') ? strrchr(libs[i], '/') + 1 : libs[i],
                           (long)(c2 - c1), (long)(M[GC_LOADER_HP] - l1), (long)(asm_here() - h1));
    }
    if (load_src("[ m-gc: masm 1 [ DROP CALL collect LIT 0 ARITY 1 EXIT ] ]") != 0) return 1;
    cell used, cap; r0_s1_global_usage(&used, &cap);
    long base_live = gc_live();
    if (budget) {
        printf("  TOTAL %ld/%ld bindings; loader %ld used, %ld free; code ends at %ld; managed live %ld of %ld\n",
               (long)used, (long)cap, (long)(M[GC_LOADER_HP] - R0S1_HEAP_BASE), (long)(R0S1_HEAP_LIMIT - M[GC_LOADER_HP]),
               (long)asm_here(), base_live, (long)(GC_HEAP_LIMIT - GC_HEAP_BASE));
        return 0;
    }
    int blocks = atoi(argv[1]), links = atoi(argv[2]), depth = atoi(argv[3]), max = atoi(argv[4]);
    /* BYTES=n overrides Glon's byte budget per batch (app.glon web/bytes); the
     * events per batch come from the command line (web/max) */
    char set[96];
    snprintf(set, sizeof set, "web/max: %d", max); load_src(set);
    if (getenv("BYTES")) { snprintf(set, sizeof set, "web/bytes: %d", atoi(getenv("BYTES"))); load_src(set); }
    root = document(blocks, links, depth);
    r0_s1_g1a_live_set_host_call(on_host_call, 0);

    /* Glon prints its results to stdout: send them to a file to check them */
    fflush(stdout);
    FILE *cap_f = tmpfile(); int saved = dup(1); dup2(fileno(cap_f), 1);

    cell lhp0 = M[GC_LOADER_HP];
    long max_live = 0, max_rs = 0, dispatches = 0; double glon_s = 0;
    long gc0 = r0_s1_gc_count();
    const char *url = "synthetic";
    /* REPEAT=n runs the whole conversation n times in the same machine (as a
     * browser session does); the last run's output is checked */
    int repeats = getenv("REPEAT") ? atoi(getenv("REPEAT")) : 1, rc = 0, ok = 1;
    long run_live[64]; int nrun = 0;
    for (int rep = 0; rep < repeats && ok && rc == 0; rep++) {
    sp = 0; started = 0; ended = 0; host_events = host_batches = host_bytes = 0;
    clock_t t0 = clock();
    rc = dispatch("web-load", (const unsigned char *)url, (int)strlen(url));
    glon_s += (double)(clock() - t0) / CLOCKS_PER_SEC; dispatches++;
    ok = rc == 0 && have_call && !strcmp(op, "web-load");
    if (ok) { t0 = clock(); rc = dispatch("web-loaded", (const unsigned char *)"1", 1);
              glon_s += (double)(clock() - t0) / CLOCKS_PER_SEC; dispatches++; }
    while (ok && rc == 0 && have_call && !strcmp(op, "dom-next")) {
        int doc, mx, cd, mb = 0; char names[256] = "";
        sscanf(arg, "%d %d %d %255s %d", &doc, &mx, &cd, names, &mb);
        if (doc != 1 || strcmp(names, "href") || mb <= 0) { ok = 0; break; }
        make_batch(mx, cd, mb);
        long rs = 24576 - (long)r0_s1_rp_min();
        t0 = clock();
        rc = dispatch("dom-events", batch, blen);
        glon_s += (double)(clock() - t0) / CLOCKS_PER_SEC; dispatches++;
        rs = 24576 - (long)r0_s1_rp_min(); if (rs > max_rs) max_rs = rs;
        long lv = gc_live(); if (lv > max_live) max_live = lv;
    }
    if (nrun < 64) run_live[nrun++] = gc_live();
    }
    long gcs = r0_s1_gc_count() - gc0;
    long after_live = gc_live();
    cell lhp1 = M[GC_LOADER_HP];

    fflush(stdout); dup2(saved, 1); close(saved);
    /* read Glon's output: LINKS n, then netstrings, then EVENTS e BATCHES b */
    rewind(cap_f); static char outtxt[1 << 20]; size_t on = fread(outtxt, 1, sizeof outtxt - 1, cap_f); outtxt[on] = 0;
    int nl = -1, matched = 0; long ev = -1, nb = -1;
    char *s = NULL;
    for (char *q = strstr(outtxt, "LINKS "); q; q = strstr(q + 1, "LINKS ")) s = q;   /* the last run */
    if (s) {
        nl = atoi(s + 6); s = strchr(s, '\n') + 1;
        for (int i = 0; i < nl && s; i++) {
            char f[2][256];
            for (int j = 0; j < 2; j++) { int len = atoi(s); char *c = strchr(s, ':') + 1; memcpy(f[j], c, (size_t)len); f[j][len] = 0; s = c + len + 1; }
            if (i < nexp && !strcmp(f[0], exp_href[i]) && !strcmp(f[1], exp_text[i])) matched++;
        }
        char *e = strstr(s, "EVENTS "); if (e) sscanf(e, "EVENTS %ld BATCHES %ld", &ev, &nb);
    }
    int pass = ok && rc == 0 && nl == nexp && matched == nexp && ev == host_events;
    if (!quiet || !pass) fputs(outtxt, stdout);
    printf("RESULT %s nodes=%ld links=%d/%d events=%ld batches=%ld bytes=%ld depth=%ld "
           "glon_ms=%.1f us_per_event=%.1f max_live=%ld base_live=%ld after_live=%ld gcs=%ld "
           "max_return_stack=%ld loader_delta=%ld dispatches=%ld\n",
           pass ? "PASS" : "FAIL", nodes_made, matched, nexp, host_events, host_batches, host_bytes, max_stack,
           glon_s * 1000, host_events ? glon_s * 1e6 / host_events : 0, max_live, base_live, after_live, gcs,
           max_rs, (long)(lhp1 - lhp0), dispatches);
    if (nrun > 1) {
        printf("live after each run:");
        for (int i = 0; i < nrun; i++) printf(" %ld", run_live[i]);
        printf("\n");
    }
    return pass ? 0 : 1;
}
