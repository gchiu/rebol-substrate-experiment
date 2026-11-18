/* c_find.c - standalone reference/performance implementation of s/find.
 *
 * This is not linked into Glon and is not a HOST service.  It exists only to
 * compare the Glon reference and MASM implementations against native C.
 *
 *   c_find TEXTFILE PATTERNFILE [ITERATIONS]
 *
 * Prints: result <offset> [time_ms <n>]
 * Empty pattern -> 0.  No match -> -1.  The files are never modified.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

static uint8_t *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *p = malloc((size_t)n + 1);
    if (!p) { fclose(f); return NULL; }
    size_t got = fread(p, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(p); return NULL; }
    p[got] = 0;
    *len = got;
    return p;
}

static long find_first(const uint8_t *text, size_t n,
                       const uint8_t *pat, size_t m) {
    if (m == 0) return 0;
    if (m > n) return -1;
    for (size_t i = 0; i <= n - m; i++) {
        size_t j = 0;
        while (j < m && text[i + j] == pat[j]) j++;
        if (j == m) return (long)i;
    }
    return -1;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: c_find TEXTFILE PATTERNFILE [ITERATIONS]\n");
        return 2;
    }
    size_t n = 0, m = 0;
    uint8_t *text = read_file(argv[1], &n);
    uint8_t *pat = read_file(argv[2], &m);
    if (!text || !pat) {
        fprintf(stderr, "c_find: cannot read input\n");
        free(text); free(pat);
        return 2;
    }
    long iterations = (argc == 4) ? strtol(argv[3], NULL, 10) : 1;
    if (iterations < 1) iterations = 1;
    long result = -1;
    double t0 = now_ms();
    for (long k = 0; k < iterations; k++) result = find_first(text, n, pat, m);
    double elapsed = now_ms() - t0;
    printf("result %ld", result);
    if (argc == 4) printf(" time_ms %.6f", elapsed);
    putchar('\n');
    free(text); free(pat);
    return 0;
}
