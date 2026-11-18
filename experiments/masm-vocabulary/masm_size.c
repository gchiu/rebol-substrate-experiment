/* masm_size.c - measure the emitted S1 code size of find.glon's MASM block.
 * Parses the source without running it; no frozen/runtime change. */
#include "r0_s1.h"
#include "s1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    char *s = malloc((size_t)n + 1);
    if (!s) { fclose(f); return NULL; }
    size_t got = fread(s, 1, (size_t)n, f); fclose(f);
    s[got] = 0; return s;
}

static void strip_comments(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') { while (*r && *r != '\n') r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: masm_size FILE.glon\n"); return 2; }
    char *src = read_file(argv[1]);
    if (!src) { perror(argv[1]); return 2; }
    strip_comments(src);
    r0_s1_init();
    cell before = asm_here();
    int err = 0;
    cell prog = r0_s1_parse(src, &err);
    free(src);
    if (err || prog == R0_NONE) { fprintf(stderr, "parse failed\n"); return 1; }
    cell after = asm_here();
    printf("masm_code_cells %ld\n", (long)(after - before));
    printf("masm_code_bytes %ld\n", (long)((after - before) * (cell)sizeof(cell)));
    return 0;
}
