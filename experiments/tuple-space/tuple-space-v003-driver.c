/* experiments/tuple-space/tuple-space-v003-driver.c
 *
 * Native/ELF driver for the Glon v0.03 tuple-space experiment.  It is a
 * laboratory harness only: it reads the Glon library and test sources,
 * injects a `collect` raw helper bound to the real collector entry, seeds the
 * M1 scheduler environment (exactly as r0_s1_m1_tests.c does), parses and
 * runs.  It contains NO tuple-space or scheduling semantics.
 *
 * Build (separate from the repository's own test binary) with the experiment
 * task-count override; see experiments/tuple-space/Makefile.
 */

#include "r0_s1.h"
#include "m1_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fprintf(stderr, "oom\n"); exit(2); }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
    fclose(f);
    return buf;
}

/* The Glon loader has no comment syntax; the source is authored with ';'
 * comments for humans, so the harness strips them before parsing. */
static char *strip_comments(const char *in) {
    size_t n = strlen(in);
    char *out = malloc(n + 1);
    size_t o = 0;
    int in_string = 0;
    for (size_t i = 0; i < n; i++) {
        char c = in[i];
        if (in_string) {
            out[o++] = c;
            if (c == '"') in_string = 0;
            continue;
        }
        if (c == '"') { in_string = 1; out[o++] = c; continue; }
        if (c == ';') { while (i < n && in[i] != '\n') i++; if (i < n) out[o++] = '\n'; continue; }
        out[o++] = c;
    }
    out[o] = 0;
    return out;
}

int main(int argc, char **argv) {
    const char *lib_path  = (argc > 1) ? argv[1] : "tuple-space-v003.glon";
    const char *test_path = (argc > 2) ? argv[2] : "tuple-space-v003-tests.glon";

    char *lib  = strip_comments(read_file(lib_path));
    char *test = strip_comments(read_file(test_path));

    /* Seed the scheduler environment: evaluator entry point, cursor, and every
     * task slot EMPTY.  Pool/GC state is seeded by r0_s1_init itself. */
    int err = 0;
    cell main_entry = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = main_entry;
    M[M1_CURSOR] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    cell collect_addr = r0_s1_gc_collect_addr();

    /* collect: bound to the real collector entry. */
    char head[128];
    snprintf(head, sizeof head,
             "[ collect: raw [ CALL %ld ARITY 0 EXIT ] ", (long)collect_addr);

    size_t total = strlen(head) + strlen(lib) + strlen(test) + 8;
    char *prog = malloc(total);
    snprintf(prog, total, "%s%s %s ]", head, lib, test);

    cell block = r0_s1_parse(prog, &err);
    if (err) { fprintf(stderr, "parse error %d\n", err); return 2; }

    int N = r0_s1_run(block);

    /* results: [check-count checks] */
    if (N != 2) { fprintf(stderr, "expected N=2, got N=%d\n", N); return 2; }
    cell count_tag = r0_s1_result(0, N);
    cell checks    = r0_s1_result(1, N);
    long count = (long)(count_tag / 16);

    if (r0_tag(checks) != T_BLOCK) {
        fprintf(stderr, "checks is not a block\n");
        return 2;
    }
    cell bp = (cell)(checks - T_BLOCK);

    long pass = 0, fail = 0;
    for (long i = 0; i < count; i++) {
        cell v = M[bp + 2 + i];
        if (v == mk_int(1)) pass++;
        else { fail++; printf("  FAIL: tuple-space check #%ld\n", i); }
    }

    printf("tuple-space v0.03 (Glon): %ld/%ld checks passed\n", pass, count);
    if (fail) {
        printf("RESULT: FAIL\n");
        return 1;
    }
    printf("RESULT: PASS\n");
    return 0;
}
