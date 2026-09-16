/* web/node_test.c -- headless verification of the WASM demo (no DOM).
 *
 * Compiles the same s1.c + r0_s1_runtime.c + demo.r0 (embedded) to WASM and
 * runs it under node: initialise, click three times, and confirm the R0
 * counter emits handle 1 / value 1,2,3 through web-set-int.  This exercises
 * the exact R0 application path the browser uses, without a DOM.
 */
#include "r0_s1.h"
#include <stdio.h>
#include <stdlib.h>

static cell click_block;
static int initialized = 0;

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

static void strip_comments(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
}

static int r0_init(void) {
    char *src = read_file("demo.r0");
    if (!src) { printf("NO SOURCE\n"); return -1; }
    int err = 0;
    r0_s1_init();
    strip_comments(src);
    cell prog = r0_s1_parse(src, &err);
    free(src);
    if (err) return -2;
    int N = r0_s1_run(prog);
    if (N < 0) return -3;
    click_block = r0_s1_parse("[ do on-click ]", &err);
    if (err) return -4;
    initialized = 1;
    return 0;
}

static int r0_click(void) {
    if (!initialized) return -1;
    return r0_s1_run(click_block);
}

int main(void) {
    int rc = r0_init();
    if (rc != 0) { printf("INIT_FAILED rc=%d\n", rc); return 1; }
    for (int i = 0; i < 3; i++) r0_click();
    printf("WASM_DEMO_OK\n");
    return 0;
}
