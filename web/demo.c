/* web/demo.c -- browser host/platform adapter for the R0-on-S1 WASM demo.
 *
 * This file is the ONLY browser-specific C code.  It uses the frozen
 * r0_s1_init / r0_s1_parse / r0_s1_run API to (a) load and initialise the
 * demo.r0 application and (b) run the `on-click` handler on demand.  It
 * contains no R0 evaluator, control, or binding semantics.
 */
#include "r0_s1.h"
#include <stdio.h>
#include <stdlib.h>
#include <emscripten.h>

static cell click_block;      /* the parsed "[ on-click ]" program */
static int initialized = 0;

/* read the embedded demo.r0 source (loaded into the WASM virtual filesystem
 * at build time with `--embed-file`) */
static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(buf); return NULL; }
    buf[sz] = 0;
    fclose(f);
    return buf;
}

/* strip `;;` line comments (the R0 reader does not know about them) */
static void strip_comments(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
}

/* one-time initialisation: load demo.r0, run its top level (counter: 0, bind
 * web-set-int + on-click), and prepare the per-click program. */
EMSCRIPTEN_KEEPALIVE
int r0_init(void) {
    char *src = read_file("demo.r0");
    if (!src) { printf("web/demo: demo.r0 not found\n"); return -1; }
    int err = 0;
    r0_s1_init();
    strip_comments(src);
    cell prog = r0_s1_parse(src, &err);
    free(src);
    if (err) { printf("web/demo: parse error\n"); return -2; }
    int N = r0_s1_run(prog);
    if (N < 0) { printf("web/demo: init run error\n"); return -3; }
    click_block = r0_s1_parse("[ do on-click ]", &err);
    if (err) { printf("web/demo: click-program parse error\n"); return -4; }
    initialized = 1;
    return 0;
}

/* exported entry: run the R0 `on-click` handler once (called from JS on each
 * button click).  Returns the result arity (unused by the glue). */
EMSCRIPTEN_KEEPALIVE
int r0_click(void) {
    if (!initialized) return -1;
    return r0_s1_run(click_block);
}

int main(void) {
    int rc = r0_init();
    printf("R0-S1 WASM demo ready (rc=%d)\n", rc);
    return 0;
}
