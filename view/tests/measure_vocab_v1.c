#include "r0_s1.h"
#include "s1.h"
#include "m1_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char *read_source(const char *path, char *err, size_t errcap) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errcap, "cannot open"); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f); snprintf(err, errcap, "cannot seek"); return NULL;
    }
    long n = ftell(f);
    if (n < 0) { fclose(f); snprintf(err, errcap, "cannot measure"); return NULL; }
    if ((unsigned long)n > (1u << 20)) {
        fclose(f); snprintf(err, errcap, "source is larger than 1 MiB"); return NULL;
    }
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); snprintf(err, errcap, "out of memory"); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(buf); snprintf(err, errcap, "cannot read"); return NULL; }
    buf[got] = 0;
    return buf;
}

static void strip_comments(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') {
            while (*r && *r != '\n') r++;
            continue;
        }
        *w++ = *r++;
    }
    *w = 0;
}

static int run_file(const char *path, r0_s1_outcome *out) {
    char err[512];
    char *src = read_source(path, err, sizeof err);
    if (!src) {
        fprintf(stderr, "vocab-v1: %s: %s\n", path, err);
        return -1;
    }
    strip_comments(src);
    int perr = 0;
    cell prog = r0_s1_parse(src, &perr);
    free(src);
    if (perr) r0_s1_session_parse_error(out);
    else r0_s1_session_run_block(prog, out);
    if (out->status != R0S1_OUT_OK) {
        fprintf(stderr, "vocab-v1: %s: status=%s detail=%s\n", path,
                r0_s1_outcome_status_name(out->status),
                r0_s1_outcome_detail_name(out->detail));
        return -1;
    }
    return 0;
}

static void print_stats(const char *label) {
    cell globals = 0, cap = 0;
    r0_s1_global_usage(&globals, &cap);
    printf("%s globals=%ld/%ld heap_high=%ld gc=%ld live_cells=%ld live_objs=%ld reclaimed=%ld free_cells=%ld code_size=%ld\n",
           label, (long)globals, (long)cap, r0_s1_heap_high(), r0_s1_gc_count(),
           r0_s1_gc_live_cells(), r0_s1_gc_live_objs(), r0_s1_gc_reclaimed(),
           r0_s1_gc_free_cells(), r0_s1_code_size());
}

int main(int argc, char **argv) {
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: measure_vocab_v1 BASELINE.glon LOOP.glon [VOCAB.glon]\n");
        return 2;
    }

    cell main_entry = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = main_entry;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;

    static const char *libs[] = {
        "glon-lib/prelude.glon",
        "glon-lib/strings.glon",
        "glon-lib/strings-ext.glon",
        "view/glon/view_lib.glon",
        "view/glon/vocab_lib.glon",
        "view/glon/vocab_v1_bench_timing.glon"
    };
    if (argc == 4) libs[5] = argv[3];
    for (unsigned i = 0; i < sizeof libs / sizeof libs[0]; i++) {
        r0_s1_outcome oc;
        if (run_file(libs[i], &oc) != 0) return 2;
    }
    print_stats("loaded");

    char err[512];
    cell loops[2];
    for (int k = 0; k < 2; k++) {
        char *src = read_source(argv[k + 1], err, sizeof err);
        if (!src) {
            fprintf(stderr, "vocab-v1: %s: %s\n", argv[k + 1], err);
            return 2;
        }
        strip_comments(src);
        int perr = 0;
        loops[k] = r0_s1_parse(src, &perr);
        free(src);
        if (perr) {
            r0_s1_outcome oc;
            r0_s1_session_parse_error(&oc);
            fprintf(stderr, "vocab-v1: %s: parse error: %s\n", argv[k + 1],
                    r0_s1_outcome_detail_name(oc.detail));
            return 2;
        }
    }

    enum { RUNS = 1000 };
    clock_t base_start = clock();
    for (int i = 0; i < RUNS; i++) {
        r0_s1_outcome oc;
        r0_s1_session_run_block(loops[0], &oc);
        if (oc.status != R0S1_OUT_OK) {
            fprintf(stderr, "vocab-v1: baseline run %d failed: %s/%s\n", i,
                    r0_s1_outcome_status_name(oc.status),
                    r0_s1_outcome_detail_name(oc.detail));
            return 1;
        }
    }
    double base_elapsed = (double)(clock() - base_start) / (double)CLOCKS_PER_SEC;

    clock_t start = clock();
    for (int i = 0; i < RUNS; i++) {
        r0_s1_outcome oc;
        r0_s1_session_run_block(loops[1], &oc);
        if (oc.status != R0S1_OUT_OK) {
            fprintf(stderr, "vocab-v1: loop run %d failed: %s/%s\n", i,
                    r0_s1_outcome_status_name(oc.status),
                    r0_s1_outcome_detail_name(oc.detail));
            return 1;
        }
    }
    double elapsed = (double)(clock() - start) / (double)CLOCKS_PER_SEC;
    double delta = elapsed - base_elapsed;
    if (delta < 0) delta = 0;
    printf("lookup runs=%d lookups_per_run=80 total_seconds=%.6f baseline_seconds=%.6f ns_per_lookup=%.1f\n",
           RUNS, elapsed, base_elapsed, delta * 1e9 / ((double)RUNS * 80.0));
    print_stats("after");

    r0_s1_outcome demo;
    if (run_file("view/glon/vocab_v1_demo.glon", &demo) != 0) return 1;
    print_stats("demo");
    return 0;
}